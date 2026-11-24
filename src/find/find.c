#include "find_int.h"
#include "base/base.h"
#include <emmintrin.h>
#include <immintrin.h>
#include <limits.h>

/* Literal SIMD filter/verify with bounded work and a Two-Way fallback.
 * The regex matcher uses the same plan for its mandatory prefix. */
typedef struct find_lit_blk { size_t at; unsigned mask; } find_lit_blk;
typedef struct find_lit_filter {
    uint8_t first, second, first_fold_bit, second_fold_bit;
} find_lit_filter;

typedef struct find_lit {
    find_lit_blk (*scan)(const uint8_t *a, const uint8_t *b, find_lit_filter filter,
                         size_t from, size_t end);
    const uint8_t *nd;
    size_t n, p1, p2;
    find_lit_filter filter;
    find_literal_options options;
    bool tw, tw_ready, periodic;
    size_t ell, per;
    uint64_t scanned, verified;
    meter *m;
    reader before, after;
} find_lit;

/* (P) Retain v1's 4 KiB window. Limit the needle to half a window so
 * every full window retires at least half its candidate positions. */
#define FIND_WINDOW_BYTES 4096u
#define FIND_WINDOW_NEEDLE (FIND_WINDOW_BYTES / 2u)

/* (E) v1 literal.c ranks from text/code/log estimates. Higher means more
 * common. Add both ranks for folded pairs as a selectivity heuristic. */
static const uint8_t k_rank[256]={
    3,3,3,3,3,3,3,3,3,63,181,3,3,61,3,3,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    255,87,175,83,77,79,81,165,177,167,91,89,213,205,211,173,
    201,199,197,195,193,191,189,187,185,183,171,169,103,179,101,85,
    75,151,117,133,135,155,127,123,139,147,109,113,137,129,145,149,
    125,107,141,143,153,131,115,121,111,119,105,95,71,93,69,203,
    65,249,215,231,233,253,225,221,237,245,161,207,235,227,243,247,
    223,159,239,241,251,229,209,219,163,217,157,99,73,97,67,60,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
};

static inline bool charge(meter *m,size_t k)
{
    /* step() in the shared regex meter polls just before its state visit.
     * Leave one unit of headroom when combining that work with bulk loads. */
    if (m->units+k>=FIND_POLL_UNITS) (void)poll_stop(m);
    m->units+=(unsigned)k;
    return m->stopped;
}
/* Compare in metered chunks. Fold the same alphabet in verification and
 * factorisation. Keep the canonical needle implicit for arbitrary lengths. */
static int cmp_poll(meter *m,const uint8_t *a,const uint8_t *b,size_t len,bool fold)
{
    while (len) {
        size_t k=len<FIND_POLL_UNITS?len:FIND_POLL_UNITS;
        if (charge(m,k)) return -1;
        if (fold) {
            for (size_t i=0;i<k;i++) if (ascii_fold(a[i])!=ascii_fold(b[i])) return 1;
        } else if (memcmp(a,b,k)!=0) return 1;
        a+=k; b+=k; len-=k;
    }
    return 0;
}

static find_lit_blk scan_sse2(const uint8_t *a,const uint8_t *b,find_lit_filter filter,
                              size_t i,size_t e);
static find_lit_blk scan_avx2(const uint8_t *a,const uint8_t *b,find_lit_filter filter,
                              size_t i,size_t e);

static inline uint8_t key(const find_lit *l, uint8_t byte)
{
    return l->options.fold_ascii ? ascii_fold(byte) : byte;
}

static unsigned rank(uint8_t byte, bool fold)
{
    if (fold && ascii_letter(byte)) {
        uint8_t lower = ascii_fold(byte);
        return (unsigned)k_rank[lower] + k_rank[lower - ('a' - 'A')];
    }
    return k_rank[byte];
}

static bool word_ok(find_lit *l, uint64_t at)
{
    if (!l->options.whole_word) return true;
    meter *m = l->m;
    if (at && (step(m) || ascii_word(get_byte_forward(&l->before, at - 1, m))))
        return false;
    uint64_t end = at + l->n;
    if (end < l->after.len && (step(m) || ascii_word(get_byte_forward(&l->after, end, m))))
        return false;
    return !m->stopped;
}

/* The plan borrows the source and nonempty needle. False means cancellation.
 * The optional word cursors need monotonically increasing candidates. */
static bool find_lit_init(find_lit *l,const find_source *source,const uint8_t *nd,size_t n,
                   find_literal_options options,meter *m,bool two_way)
{
    *l = (find_lit){.nd = nd, .n = n, .options = options, .tw = two_way, .m = m};
    if (options.whole_word) {
        l->before = reader_init(source);
        l->after = l->before;
    }
    if (two_way) return true;
    l->scan=base_cpu_has_avx2()?scan_avx2:scan_sse2;
    unsigned best=UINT_MAX; size_t p1=0;
    /* (P) Charge up to one rank per byte in the first pass, two reads per
     * byte in the second; each batch is one compressed visitor mask wide. */
    for (size_t i=0;i<n;i++) {
        if ((i&63u)==0 && charge(m,64)) return false;
        unsigned r=rank(nd[i],options.fold_ascii);
        if (r<best) { best=r; p1=i; }
    }
    uint8_t first=key(l,nd[p1]);
    best=UINT_MAX; size_t p2=n;
    for (size_t i=0;i<n;i++) {
        if ((i&63u)==0 && charge(m,128)) return false;
        unsigned r=rank(nd[i],options.fold_ascii);
        if (key(l,nd[i])!=first && r<=best) { best=r; p2=i; }
    }
    if (p2==n) p2=(p1==n-1)?0:n-1;
    uint8_t second=key(l,nd[p2]);
    l->p1=p1; l->p2=p2;
    l->filter=(find_lit_filter){first, second,
        options.fold_ascii && ascii_letter(first) ? 'a'-'A' : 0,
        options.fold_ascii && ascii_letter(second) ? 'a'-'A' : 0};
    return true;
}

/* ---- Two-Way (Crochemore-Perrin), no tables, no allocation ------------- */
static bool max_suffix(find_lit *l,bool rev,size_t *ell,size_t *per)
{
    const uint8_t *x=l->nd; size_t m=l->n, ms1=0, j=0, k=1, p=1; /* ms1 = ms+1 */
    while (j+k<m) {
        if (charge(l->m,1)) return false;
        uint8_t a=key(l,x[j+k]), b=key(l,x[ms1-1+k]);
        if (rev ? a>b : a<b) { j+=k; k=1; p=j-ms1+1; }
        else if (a==b) { if (k!=p) k++; else { j+=p; k=1; } }
        else { ms1=j+1; j=ms1; k=1; p=1; }
    }
    *ell=ms1; *per=p; return true;
}
static bool tw_prepare(find_lit *l)
{
    if (l->tw_ready) return true;
    size_t e1,p1,e2,p2;
    if (!max_suffix(l,false,&e1,&p1) || !max_suffix(l,true,&e2,&p2)) return false;
    size_t ell=e1,per=p1;
    if (e2>e1) { ell=e2; per=p2; }
    bool periodic=false;
    if (ell+per<=l->n) {
        int c=cmp_poll(l->m,l->nd,l->nd+per,ell,l->options.fold_ascii);
        if (c<0) return false;
        periodic=c==0;
    }
    l->ell=ell; l->periodic=periodic;
    l->per=periodic?per:((ell>l->n-ell?ell:l->n-ell)+1);
    l->tw_ready=true; return true;
}
#define UNIT() do { if (charge(m,1)) return -1; } while (0)
/* Leftmost match start in [from, len-n]. Snapshot suffix/prefix comparisons
 * use separate forward readers. The critical cut is below the true period.
 * The nonperiodic shift exceeds the prefix length. A prefix failure retires
 * the bytes that cursor read, even with ascending comparisons. A whole-word
 * rejection keeps the same safe shift and periodic overlap memory. */
static inline __attribute__((always_inline))
int tw_core(find_lit *l,bool contig,const uint8_t *hp,reader *rd,reader *left,uint64_t len,uint64_t from,uint64_t base,uint64_t *out)
{
    const uint8_t *nd=l->nd; size_t n=l->n, ell=l->ell, per=l->per; meter *m=l->m;
#define HB(x) (contig ? hp[(size_t)(x)] : get_byte_forward(rd,(x),m))
#define HL(x) (contig ? hp[(size_t)(x)] : get_byte_forward(left,(x),m))
    if (n>len || from>len-n) return 0;
    uint64_t last=len-n, j=from; size_t memory=0;
    if (l->periodic) {
        while (j<=last) {
            size_t i=ell>memory?ell:memory;
            while (i<n) { UNIT(); if (key(l,nd[i])!=key(l,HB(j+i))) break; i++; }
            if (i>=n) {
                size_t k=memory;
                while (k<ell) { UNIT(); if (key(l,nd[k])!=key(l,HL(j+k))) break; k++; }
                UNIT();
                if (k>=ell && word_ok(l,base+j)) { *out=j; return 1; }
                j+=per; memory=n-per;
            } else { UNIT(); j+=i-ell+1; memory=0; }
        }
    } else {
        while (j<=last) {
            size_t i=ell;
            while (i<n) { UNIT(); if (key(l,nd[i])!=key(l,HB(j+i))) break; i++; }
            if (i>=n) {
                size_t k=0;
                while (k<ell) { UNIT(); if (key(l,nd[k])!=key(l,HL(j+k))) break; k++; }
                UNIT();
                if (k==ell && word_ok(l,base+j)) { *out=j; return 1; }
                j+=per;
            } else { UNIT(); j+=i-ell+1; }
        }
    }
#undef HB
#undef HL
    return m->stopped ? -1 : 0;
}
static int tw_buf(find_lit *l,const uint8_t *h,size_t len,size_t from,uint64_t base,size_t *out)
{
    if (!tw_prepare(l)) return -1;
    uint64_t o=0; int r=tw_core(l,true,h,NULL,NULL,len,from,base,&o);
    *out=(size_t)o; return r;
}

/* ---- contiguous kernels ------------------------------------------------ */
/* (P) SSE byte lanes are 16 starts. A comparison and advance cost two
 * units per start, including the tail. */
static int seek_byte(find_lit *l,const uint8_t *h,size_t len,size_t from,size_t *out)
{
    meter *m=l->m; size_t i=from; const __m128i v=_mm_set1_epi8((char)l->filter.first);
    const __m128i fold=_mm_set1_epi8((char)l->filter.first_fold_bit);
    while (i+16<=len) {
        if (charge(m,32)) return -1;
        unsigned mk=(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_or_si128(_mm_loadu_si128((const __m128i *)(const void *)(h+i)),fold),v));
        if (mk) { *out=i+(size_t)__builtin_ctz(mk); return 1; }
        i+=16;
    }
    for (;i<len;i++) { if (charge(m,2)) return -1; if ((h[i]|l->filter.first_fold_bit)==l->filter.first) { *out=i; return 1; } }
    return 0;
}
/* 0 = needle present at p, 1 = differs, -1 cancelled. Charged in full. */
static inline int verify(find_lit *l,const uint8_t *p)
{
    l->verified+=l->n;
    return cmp_poll(l->m,p,l->nd,l->n,l->options.fold_ascii);
}
/* (G) v1 admits four verifications per retired start plus eight needle
 * lengths. The next rejected candidate past that credit forces Two-Way. */
static bool verification_exhausted(const find_lit *l)
{
    return l->verified > 4 * l->scanned + 8 * (uint64_t)l->n;
}

/* Pair-filter block scanners: first 32-start block in [i,e) (e-i multiple of
 * 32) with a candidate; mask bit k = start at+k passes both filter bytes. */
static find_lit_blk scan_sse2(const uint8_t *a,const uint8_t *b,find_lit_filter filter,size_t i,size_t e)
{
    const __m128i v1=_mm_set1_epi8((char)filter.first), v2=_mm_set1_epi8((char)filter.second);
    const __m128i f1=_mm_set1_epi8((char)filter.first_fold_bit);
    const __m128i f2=_mm_set1_epi8((char)filter.second_fold_bit);
    for (;i<e;i+=32) {
        const __m128i a0=_mm_loadu_si128((const __m128i *)(const void *)(a+i));
        const __m128i b0=_mm_loadu_si128((const __m128i *)(const void *)(b+i));
        const __m128i a1=_mm_loadu_si128((const __m128i *)(const void *)(a+i+16));
        const __m128i b1=_mm_loadu_si128((const __m128i *)(const void *)(b+i+16));
        unsigned m0=(unsigned)_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(_mm_or_si128(a0,f1),v1),_mm_cmpeq_epi8(_mm_or_si128(b0,f2),v2)));
        unsigned m1=(unsigned)_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(_mm_or_si128(a1,f1),v1),_mm_cmpeq_epi8(_mm_or_si128(b1,f2),v2)));
        if (m0|m1) return (find_lit_blk){i,m0|(m1<<16)};
    }
    return (find_lit_blk){e,0};
}
__attribute__((target("avx2")))
static find_lit_blk scan_avx2(const uint8_t *a,const uint8_t *b,find_lit_filter filter,size_t i,size_t e)
{
    const __m256i v1=_mm256_set1_epi8((char)filter.first), v2=_mm256_set1_epi8((char)filter.second);
    const __m256i f1=_mm256_set1_epi8((char)filter.first_fold_bit);
    const __m256i f2=_mm256_set1_epi8((char)filter.second_fold_bit);
    for (;i+64<=e;i+=64) {
        const __m256i a0=_mm256_loadu_si256((const __m256i *)(const void *)(a+i));
        const __m256i b0=_mm256_loadu_si256((const __m256i *)(const void *)(b+i));
        const __m256i a1=_mm256_loadu_si256((const __m256i *)(const void *)(a+i+32));
        const __m256i b1=_mm256_loadu_si256((const __m256i *)(const void *)(b+i+32));
        const __m256i x0=_mm256_and_si256(_mm256_cmpeq_epi8(_mm256_or_si256(a0,f1),v1),_mm256_cmpeq_epi8(_mm256_or_si256(b0,f2),v2));
        const __m256i x1=_mm256_and_si256(_mm256_cmpeq_epi8(_mm256_or_si256(a1,f1),v1),_mm256_cmpeq_epi8(_mm256_or_si256(b1,f2),v2));
        if (!_mm256_testz_si256(_mm256_or_si256(x0,x1),_mm256_or_si256(x0,x1))) {
            unsigned m0=(unsigned)_mm256_movemask_epi8(x0);
            if (m0) return (find_lit_blk){i,m0};
            return (find_lit_blk){i+32,(unsigned)_mm256_movemask_epi8(x1)};
        }
    }
    for (;i<e;i+=32) {
        const __m256i x=_mm256_and_si256(
            _mm256_cmpeq_epi8(_mm256_or_si256(_mm256_loadu_si256((const __m256i *)(const void *)(a+i)),f1),v1),
            _mm256_cmpeq_epi8(_mm256_or_si256(_mm256_loadu_si256((const __m256i *)(const void *)(b+i)),f2),v2));
        unsigned mk=(unsigned)_mm256_movemask_epi8(x);
        if (mk) return (find_lit_blk){i,mk};
    }
    return (find_lit_blk){e,0};
}
/* 1 found (*out), 0 none, -1 cancelled. Starts examined: [from, len-n]. */
static int seek_buf(find_lit *l,const uint8_t *h,size_t len,size_t from,uint64_t base,size_t *out)
{
    size_t n=l->n;
    if (n>len || from>len-n) return 0;
    if (n==1 && !l->tw && !l->options.whole_word) return seek_byte(l,h,len,from,out);
    size_t last=len-n, i=from;
    meter *m=l->m;
    if (!l->tw) {
        const uint8_t *a=h+l->p1, *b=h+l->p2;
        for (;;) {
            size_t blocks=(last-i+1)/32;            /* 32-start blocks left */
            if (blocks==0) break;
            /* Reserve two comparisons plus an advance for every start.
             * Every scanner call gets its own charge: verification may poll
             * and reset the meter before the following block is scanned. */
            size_t room=m->units<FIND_POLL_UNITS?(FIND_POLL_UNITS-1-m->units)/3:0;
            if (room<32) {
                if (poll_stop(m)) return -1;
                room=(FIND_POLL_UNITS-1)/3;
            }
            size_t max_blocks=room/32;
            size_t cnt=(blocks>max_blocks?max_blocks:blocks)*32;
            size_t e=i+cnt;
            find_lit_blk k=l->scan(a,b,l->filter,i,e);
            /* AVX2 can load the next 32 starts before returning the first
             * mask. Charge that lookahead conservatively on both kernels. */
            size_t scanned=k.mask?k.at-i+64:cnt;
            if (scanned>cnt) scanned=cnt;
            if (charge(m,3*scanned)) return -1;
            /* Budget credit covers only starts retired by this mask. The
             * AVX2 lookahead is reexamined after verification, not new work. */
            l->scanned+=k.mask?k.at-i+32:cnt;
            if (!k.mask) { i=e; continue; }
            unsigned mask=k.mask;
            while (mask) {
                if (charge(m,1)) return -1;
                size_t cand=k.at+(size_t)__builtin_ctz(mask);
                mask&=mask-1;
                int v=verify(l,h+cand);
                if (v<0 || charge(m,1)) return -1;
                if (v==0 && word_ok(l,base+cand)) { *out=cand; return 1; }
                if (m->stopped) return -1;
                if (verification_exhausted(l)) { l->tw=true; i=cand+1; goto fallback; }
            }
            i=k.at+32;
        }
        for (;i<=last;i++) {
            if (charge(m,3)) return -1;
            l->scanned++;
            if ((h[i+l->p1]|l->filter.first_fold_bit)==l->filter.first &&
                (h[i+l->p2]|l->filter.second_fold_bit)==l->filter.second) {
                int v=verify(l,h+i);
                if (v<0 || charge(m,1)) return -1;
                if (v==0 && word_ok(l,base+i)) { *out=i; return 1; }
                if (m->stopped) return -1;
                if (verification_exhausted(l)) { l->tw=true; i++; goto fallback; }
            }
        }
        return 0;
    }
fallback:
    return tw_buf(l,h,len,i,base,out);
}

/* ---- piece views --------------------------------------------------------- */
static size_t gather(const piece_view *s,uint8_t *dst,uint64_t from,size_t count,meter *m)
{
    piece_iter it; const uint8_t *p; size_t sn, got=0;
    piece_iter_init(&it,s,from);
    while (got<count && (sn=piece_iter_next(&it,&p))) {
        size_t k=sn<count-got?sn:count-got;
        while (k) {
            size_t chunk=k<FIND_POLL_UNITS?k:FIND_POLL_UNITS;
            if (charge(m,chunk)) return 0;
            memcpy(dst+got,p,chunk); got+=chunk; p+=chunk; k-=chunk;
        }
    }
    return got;
}
static int seek_view(find_lit *l,const find_source *src,uint64_t len,uint64_t from,uint64_t *out)
{
    size_t n=l->n; const piece_view *s=src->view;
    if ((uint64_t)n>len || from>len-n) return 0;
    if (l->tw || n>FIND_WINDOW_NEEDLE) {
        if (!tw_prepare(l)) return -1;
        reader rd=reader_init(src),left=reader_init(src);
        return tw_core(l,false,NULL,&rd,&left,len,from,0,out);
    }
    uint8_t win[FIND_WINDOW_BYTES];
    piece_iter it; const uint8_t *p; size_t sn, o;
    uint64_t next=from, g=from;
    piece_iter_init(&it,s,from);
    if (!(sn=piece_iter_next(&it,&p))) return 0;
    for (;;) {
        if (next>len-n) return 0;
        if (charge(l->m,1)) return -1;
        int r;
        if (n==1 || sn>=2*n) {
            uint64_t gend=g+sn;
            if (gend-n>=next) {
                r=seek_buf(l,p,sn,(size_t)(next-g),g,&o);
                if (r!=0) { if (r>0) *out=g+o; return r; }
            }
            if (n>1) {
                uint64_t s0=gend-n+1>next?gend-n+1:next;
                uint64_t e=gend+n-1<len?gend+n-1:len;
                if (e>s0) {
                    size_t wl=gather(s,win,s0,(size_t)(e-s0),l->m);
                    if (l->m->stopped) return -1;
                    r=seek_buf(l,win,wl,0,s0,&o);
                    if (r!=0) { if (r>0) *out=s0+o; return r; }
                }
            }
            next=gend;
            if (!(sn=piece_iter_next(&it,&p))) return 0;
            g=next;
        } else {
            uint64_t rem=len-next; size_t w=rem<FIND_WINDOW_BYTES?(size_t)rem:FIND_WINDOW_BYTES;
            size_t wl=gather(s,win,next,w,l->m);
            if (l->m->stopped) return -1;
            r=seek_buf(l,win,wl,0,next,&o);
            if (r!=0) { if (r>0) *out=next+o; return r; }
            if (w==rem) return 0;
            next+=w-n+1;
            piece_iter_init(&it,s,next);
            if (!(sn=piece_iter_next(&it,&p))) return 0;
            g=next;
        }
    }
}
/* Seek the leftmost acceptable start at or after from in the full source.
 * Return 1 for a match, 0 for exhaustion, -1 for cancellation. */
static int find_lit_seek(find_lit *l,const find_source *s,uint64_t len,uint64_t from,uint64_t *out)
{
    if (s->view) return seek_view(l,s,len,from,out);
    size_t o=0; int r=seek_buf(l,s->bytes,s->length,(size_t)from,0,&o);
    *out=o; return r;
}

/* (P) One compressed mask represents the starts in a uint64_t. A visible
 * single-byte match is a start in the half-open window. */
static uint64_t low_bits(uint64_t n)
{
    return n >= 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1;
}

/* (P) Count in parallel into successive two-bit, four-bit and byte
 * lanes. The sum fits in a byte because a uint64_t has 64 bits. */
static unsigned mask_count(uint64_t bits)
{
    bits -= (bits >> 1) & UINT64_C(0x5555555555555555);
    bits = (bits & UINT64_C(0x3333333333333333)) +
           ((bits >> 2) & UINT64_C(0x3333333333333333));
    bits = (bits + (bits >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
    return (unsigned)((bits * UINT64_C(0x0101010101010101)) >> 56);
}

static uint64_t first_bits(uint64_t mask, uint64_t n)
{
    uint64_t selected = 0;
    while (mask && n) {
        uint64_t bit = mask & (~mask + 1);
        selected |= bit;
        mask ^= bit;
        n--;
    }
    return selected;
}

static bool visit_mask(find_visit *v, meter *m, uint64_t mask, uint64_t base, uint64_t ordinal)
{
    if (v->emit_mask) return !poll_fault(m) && v->emit_mask(v->user, mask, base, ordinal);
    uint64_t selected = ordinal < v->prefix_capacity ?
        first_bits(mask, v->prefix_capacity - ordinal) : 0;
    if (v->wanted >= ordinal && v->wanted - ordinal < 64) {
        uint64_t rank = v->wanted - ordinal;
        uint64_t prefix = first_bits(mask, rank);
        selected |= first_bits(mask & ~prefix, 1);
    }
    uint64_t lo = v->window_start > base ? v->window_start - base : 0;
    uint64_t hi = v->window_end > base ? v->window_end - base : 0;
    uint64_t visible = mask & low_bits(hi) & ~low_bits(lo);
    if (v->visible < v->visible_capacity)
        selected |= first_bits(visible, v->visible_capacity - v->visible);
    v->visible += mask_count(visible);
    if (selected && poll_fault(m)) return false;
    while (selected) {
        unsigned bit = (unsigned)__builtin_ctzll(selected);
        uint64_t index = ordinal + mask_count(mask & low_bits(bit));
        if (!v->emit(v->user, index, (find_capture){base + bit, base + bit + 1}))
            return false;
        selected &= selected - 1;
    }
    return true;
}

static find_code sink_mask(find_sink *sink, meter *m, uint64_t mask, uint64_t base)
{
    unsigned count = mask_count(mask);
    if (sink->total > UINT64_MAX - count) return FIND_ERR_LIMIT;
    if (!visit_mask(sink->visit, m, mask, base, sink->total)) return FIND_CANCELLED;
    sink->total += count;
    return FIND_OK;
}

static find_code count_byte(find_lit *l, const uint8_t *p, size_t n, uint64_t base,
                            find_sink *sink)
{
    const __m128i byte = _mm_set1_epi8((char)l->filter.first);
    const __m128i fold = _mm_set1_epi8((char)l->filter.first_fold_bit);
    size_t i = 0;
    /* (P) Four SSE vectors make one 64-start mask, at two units per start
     * for the comparison and the candidate advance. */
    for (; n - i >= 64; i += 64) {
        if (charge(l->m, 128)) return FIND_CANCELLED;
        uint64_t mask = 0;
        for (unsigned lane = 0; lane < 64; lane += 16) {
            __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)(p + i + lane));
            unsigned matches = (unsigned)_mm_movemask_epi8(
                _mm_cmpeq_epi8(_mm_or_si128(bytes, fold), byte));
            mask |= (uint64_t)matches << lane;
        }
        if (mask) {
            find_code code = sink_mask(sink, l->m, mask, base + i);
            if (code != FIND_OK) return code;
        }
    }
    for (; i < n; i++) {
        if (charge(l->m, 2)) return FIND_CANCELLED;
        if ((p[i] | l->filter.first_fold_bit) == l->filter.first) {
            find_code code = find_sink_add(sink, l->m, (find_capture){base + i, base + i + 1});
            if (code != FIND_OK) return code;
        }
    }
    return FIND_OK;
}

static find_code count_byte_source(find_lit *l, const find_source *source, find_sink *sink)
{
    if (!source->view) return count_byte(l, source->bytes, source->length, 0, sink);
    piece_iter it;
    piece_iter_init(&it, source->view, 0);
    uint64_t base = 0;
    const uint8_t *bytes;
    size_t n;
    while ((n = piece_iter_next(&it, &bytes))) {
        find_code code = count_byte(l, bytes, n, base, sink);
        if (code != FIND_OK) return code;
        base += n;
    }
    return FIND_OK;
}

static find_code literal_run(const find_source *source, const uint8_t *needle, size_t n,
                             find_literal_options options, const find_control *control,
                             find_sink *sink, bool two_way)
{
    if (!source_valid(source) || (!needle && n)) return FIND_ERR_ARGUMENT;
    meter m = meter_init(source, control);
    find_code code = FIND_OK;
    uint64_t len = source_len(source);
    if (!poll_stop(&m) && n && n <= len) {
        find_lit plan;
        if (find_lit_init(&plan, source, needle, n, options, &m, two_way)) {
            if (n == 1 && !options.whole_word && !two_way) {
                code = count_byte_source(&plan, source, sink);
            } else {
                uint64_t at;
                for (uint64_t off = 0; find_lit_seek(&plan, source, len, off, &at) > 0;) {
                    off = at + n;
                    code = find_sink_add(sink, &m, (find_capture){at, off});
                    if (code != FIND_OK) break;
                }
            }
        }
    }
    return find_sink_finish(sink, &m, code);
}

static find_code literal_visit(const find_source *source, const uint8_t *needle, size_t n,
                               find_literal_options options, const find_control *control,
                               find_visit *visit, bool two_way)
{
    if (!visit) return FIND_ERR_ARGUMENT;
    visit->total = 0;
    visit->visible = 0;
    if (!visit->emit) return FIND_ERR_ARGUMENT;
    find_sink sink = {.visit = visit};
    return literal_run(source, needle, n, options, control, &sink, two_way);
}

find_code find_literal_visit(const find_source *source, const uint8_t *needle, size_t n,
                             find_literal_options options, const find_control *control,
                             find_visit *visit)
{
    return literal_visit(source, needle, n, options, control, visit, false);
}

#ifdef FIND_TEST_HOOKS
find_code find_test_literal_visit_two_way(const find_source *source, const uint8_t *needle,
                                         size_t n, find_literal_options options,
                                         const find_control *control, find_visit *visit)
{
    return literal_visit(source, needle, n, options, control, visit, true);
}
#endif

find_code find_literal_next(const find_source *source, const uint8_t *needle, size_t n,
                            find_literal_options options, uint64_t off,
                            const find_control *control, find_match *match)
{
    if (!match) return FIND_ERR_ARGUMENT;
    clear_match(match);
    if (!source_valid(source) || (!needle && n) || off > source_len(source))
        return FIND_ERR_ARGUMENT;
    meter m = meter_init(source, control);
    uint64_t len = source_len(source);
    if (!poll_stop(&m) && n && n <= len - off) {
        find_lit plan;
        uint64_t at;
        if (find_lit_init(&plan, source, needle, n, options, &m, false) &&
            find_lit_seek(&plan, source, len, off, &at) > 0) {
            match->matched = true;
            match->whole = (find_capture){at, at + n};
        }
    }
    if (!poll_stop(&m)) return FIND_OK;
    clear_match(match);
    return FIND_CANCELLED;
}

/* Tagged Thompson byte regex. (P) Two tags per group record start/end,
 * the class bitset covers the uint8_t alphabet, and FIND_NONE is outside
 * FIND_MAX_STATES. The frozen cases and capture ordering come from v1. */
#define FIND_MAGIC UINT32_C(0x66696e64) /* (P) ASCII "find" */
#define FIND_NONE UINT16_MAX

typedef enum state_kind { S_BYTE, S_JUMP, S_SPLIT, S_SAVE, S_BOL, S_EOL, S_MATCH } state_kind;
typedef struct state {
    state_kind kind;
    uint16_t out, other, tag;
    uint8_t cls[32];
} state;
struct find_regex {
    uint32_t magic;
    uint16_t count, start, groups, prefix_len;
    uint8_t prefix[FIND_MAX_STATES];
    state states[FIND_MAX_STATES];
};
/* Compiler: closed Thompson fragments, with a conservative common-prefix
 * calculation. Prefix filtering is sound even for nullable branches/loops. */
typedef struct fragment {
    uint16_t start,end,len;
    bool fixed;
    uint8_t prefix[FIND_MAX_STATES];
} fragment;
typedef struct parser {
    find_regex *re;
    const uint8_t *p;
    size_t n,pos,error;
    unsigned depth;
    find_code code;
} parser;
static void fail(parser *p,find_code code,size_t at)
{ if (p->code==FIND_OK) { p->code=code; p->error=at; } }
static uint16_t emit(parser *p,state_kind kind)
{
    if (p->code!=FIND_OK) return 0;
    if (p->re->count==FIND_MAX_STATES) { fail(p,FIND_ERR_LIMIT,p->pos); return 0; }
    uint16_t i=p->re->count++;
    p->re->states[i].kind=kind;
    p->re->states[i].out=FIND_NONE; p->re->states[i].other=FIND_NONE;
    return i;
}
static fragment blank(parser *p)
{
    fragment f={0}; f.start=emit(p,S_JUMP); f.end=f.start; f.fixed=true; return f;
}
static void class_set(uint8_t *cls,uint8_t c)
{ cls[c/8u]|=(uint8_t)(1u<<(c%8u)); }
static bool class_has(const uint8_t *cls,uint8_t c)
{ return (cls[c/8u] & (uint8_t)(1u<<(c%8u)))!=0; }
static uint8_t class_byte(parser *p)
{
    if (p->pos==p->n) { fail(p,FIND_ERR_SYNTAX,p->pos); return 0; }
    uint8_t c=p->p[p->pos++];
    if (c=='\\') {
        if (p->pos==p->n) { fail(p,FIND_ERR_SYNTAX,p->pos); return 0; }
        c=p->p[p->pos++];
    } else if (c=='[' && p->pos<p->n &&
               (p->p[p->pos]==':' || p->p[p->pos]=='.' || p->p[p->pos]=='='))
        fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
    return c;
}
static void parse_class(parser *p,uint8_t *cls)
{
    bool negate=false, any=false;
    if (p->pos<p->n && p->p[p->pos]=='^') { negate=true; p->pos++; }
    while (p->pos<p->n && p->p[p->pos]!=']' && p->code==FIND_OK) {
        uint8_t lo=class_byte(p); any=true;
        if (p->pos+1<p->n && p->p[p->pos]=='-' && p->p[p->pos+1]!=']') {
            p->pos++; uint8_t hi=class_byte(p);
            if (hi<lo) { fail(p,FIND_ERR_SYNTAX,p->pos); break; }
            for (unsigned c=lo;c<=(unsigned)hi;c++) class_set(cls,(uint8_t)c);
        } else class_set(cls,lo);
    }
    if (!any || p->pos==p->n) fail(p,FIND_ERR_SYNTAX,p->pos);
    else p->pos++;
    if (negate) for (size_t i=0;i<32;i++) cls[i]=(uint8_t)~cls[i];
}
static fragment parse_expr(parser *p);
static fragment parse_atom(parser *p)
{
    fragment f={0}; bool anchor=false;
    uint8_t c=p->p[p->pos++];
    if (c=='(') {
        if (++p->depth>FIND_MAX_DEPTH || p->re->groups==FIND_MAX_GROUPS) {
            fail(p,FIND_ERR_LIMIT,p->pos-1); return f;
        }
        uint16_t g=p->re->groups++;
        uint16_t begin=emit(p,S_SAVE); p->re->states[begin].tag=(uint16_t)(2u*g);
        f=parse_expr(p);
        if (p->pos==p->n || p->p[p->pos]!=')') fail(p,FIND_ERR_SYNTAX,p->pos);
        else p->pos++;
        uint16_t end=emit(p,S_SAVE); p->re->states[end].tag=(uint16_t)(2u*g+1u);
        p->re->states[begin].out=f.start; p->re->states[f.end].out=end;
        f.start=begin; f.end=emit(p,S_JUMP);
        p->re->states[end].out=f.end; p->depth--;
    } else {
        state_kind kind=S_BYTE;
        if (c=='^' || c=='$') { kind=c=='^'?S_BOL:S_EOL; anchor=true; }
        else if (c=='*' || c=='+' || c=='?') fail(p,FIND_ERR_SYNTAX,p->pos-1);
        else if (c=='{' || c=='}') fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
        f.start=emit(p,kind); f.end=emit(p,S_JUMP);
        p->re->states[f.start].out=f.end;
        uint8_t *cls=p->re->states[f.start].cls;
        if (anchor) f.fixed=true;
        else if (c=='[') parse_class(p,cls);
        else if (c=='.') {
            memset(cls,255,32); cls['\n'/8u]&=(uint8_t)~(1u<<('\n'%8u));
        } else {
            if (c=='\\') {
                if (p->pos==p->n) fail(p,FIND_ERR_SYNTAX,p->pos);
                else {
                    c=p->p[p->pos++];
                    if (c>='0' && c<='9') fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
                }
            }
            class_set(cls,c); f.len=1; f.prefix[0]=c; f.fixed=true;
        }
        /* Singleton classes can supply a literal prefix too. */
        if (!anchor && !f.fixed) {
            unsigned count=0; uint8_t only=0;
            for (unsigned v=0;v<256;v++) if (class_has(cls,(uint8_t)v)) { count++; only=(uint8_t)v; }
            if (count==1) { f.fixed=true; f.len=1; f.prefix[0]=only; }
        }
    }
    if (p->code==FIND_OK && p->pos<p->n &&
        (p->p[p->pos]=='*' || p->p[p->pos]=='+' || p->p[p->pos]=='?')) {
        uint8_t q=p->p[p->pos++];
        if (anchor) { fail(p,FIND_ERR_SYNTAX,p->pos-1); return f; }
        uint16_t split=emit(p,S_SPLIT), end=emit(p,S_JUMP);
        p->re->states[split].out=f.start; p->re->states[split].other=end;
        p->re->states[f.end].out=q=='?'?end:split;
        if (q!='+') { f.start=split; f.len=0; }
        f.end=end; f.fixed=false;
        if (p->pos<p->n && (p->p[p->pos]=='*' || p->p[p->pos]=='+' || p->p[p->pos]=='?'))
            fail(p,FIND_ERR_SYNTAX,p->pos);
    }
    return f;
}
static fragment parse_concat(parser *p)
{
    fragment f=blank(p);
    while (p->pos<p->n && p->p[p->pos]!='|' && p->p[p->pos]!=')' && p->code==FIND_OK) {
        fragment right=parse_atom(p);
        p->re->states[f.end].out=right.start; f.end=right.end;
        if (f.fixed) {
            size_t room=FIND_MAX_STATES-(size_t)f.len;
            size_t add=right.len<room?right.len:room;
            memcpy(f.prefix+f.len,right.prefix,add); f.len=(uint16_t)((size_t)f.len+add);
        }
        f.fixed=f.fixed && right.fixed;
    }
    return f;
}
static fragment parse_expr(parser *p)
{
    fragment f=parse_concat(p);
    while (p->pos<p->n && p->p[p->pos]=='|' && p->code==FIND_OK) {
        p->pos++; fragment right=parse_concat(p);
        uint16_t split=emit(p,S_SPLIT), end=emit(p,S_JUMP);
        p->re->states[split].out=f.start; p->re->states[split].other=right.start;
        p->re->states[f.end].out=end; p->re->states[right.end].out=end;
        size_t common=0;
        while (common<f.len && common<right.len && f.prefix[common]==right.prefix[common]) common++;
        f.fixed=f.fixed && right.fixed && f.len==right.len && common==f.len;
        f.len=(uint16_t)common; f.start=split; f.end=end;
    }
    return f;
}
size_t find_regex_bytes(void) { return sizeof(find_regex); }
static find_code find_regex_compile(void *memory,size_t size,const uint8_t *pattern,size_t n,
                             find_regex **out,size_t *error_offset)
{
    if (error_offset) *error_offset=0;
    if (!out) return FIND_ERR_ARGUMENT;
    *out=NULL;
    if (!memory || (uintptr_t)memory%_Alignof(max_align_t)!=0 || (!pattern && n)) return FIND_ERR_ARGUMENT;
    if (size<sizeof(find_regex)) return FIND_ERR_MEMORY;
    if (n>FIND_MAX_PATTERN) { if (error_offset) *error_offset=FIND_MAX_PATTERN; return FIND_ERR_LIMIT; }
    find_regex *re=memory; memset(re,0,sizeof *re);
    parser p={re,pattern,n,0,0,0,FIND_OK}; fragment f=parse_expr(&p);
    if (p.code==FIND_OK && p.pos<n) fail(&p,FIND_ERR_SYNTAX,p.pos);
    if (p.code!=FIND_OK) { if (error_offset) *error_offset=p.error; return p.code; }
    re->states[f.end].kind=S_MATCH;
    re->start=f.start; re->prefix_len=f.len; memcpy(re->prefix,f.prefix,f.len);
    re->magic=FIND_MAGIC; *out=re; return FIND_OK;
}
/* Ordered, tagged Thompson simulation. Deduplicate each state at each boundary
 * on its first DFS visit. Earlier branches and consuming loop arms win ties. */
typedef struct thread {
    uint16_t state;
    uint64_t tags[2*FIND_MAX_GROUPS];
} thread;
typedef struct scratch {
    thread lists[2][FIND_MAX_STATES];
    thread stack[2*FIND_MAX_STATES+1];
    bool seen[FIND_MAX_STATES];
} scratch;
size_t find_regex_scratch_bytes(const find_regex *re) { return re?sizeof(scratch):0; }
/* (G) v1's retry ceiling: 64*(states+1)*(source_bytes+1) visits/candidates,
 * with saturation. The whole request shares this credit, including overlap
 * retries. Exhaustion returns LIMIT and clears the public counts. */
typedef struct regex_meter {
    meter poll;
    uint64_t remaining;
    bool limited;
} regex_meter;
uint64_t find_regex_work_budget(const find_regex *re,uint64_t length)
{
    if (!re || re->magic!=FIND_MAGIC) return 0;
    uint64_t scale=64*((uint64_t)re->count+1);
    return length>=UINT64_MAX/scale ? UINT64_MAX : (length+1)*scale;
}
static regex_meter regex_meter_init(const find_source *source,const find_regex *re,
                                   const find_control *control,uint64_t length)
{
    return (regex_meter){.poll = meter_init(source, control),
                         .remaining = find_regex_work_budget(re, length)};
}
static bool regex_step(regex_meter *m)
{
    if (!m->remaining) { m->limited=true; return true; }
    m->remaining--;
    return step(&m->poll);
}
static bool regex_stopped(const regex_meter *m)
{ return m->limited || m->poll.stopped; }
static size_t closure(reader *r,const find_regex *re,scratch *s,thread seed,
                      uint64_t pos,thread *dest,size_t count,regex_meter *m,find_match *best)
{
    size_t top=0; s->stack[top++]=seed;
    while (top && !regex_stopped(m)) {
        thread t=s->stack[--top];
        if (regex_step(m)) break;
        if (s->seen[t.state]) continue;
        s->seen[t.state]=true; const state *st=&re->states[t.state];
        switch (st->kind) {
        case S_BYTE: dest[count++]=t; break;
        case S_MATCH:
            if (!best->matched || pos>best->whole.end) {
                best->matched=true; best->whole.end=pos;
                for (size_t g=0;g<re->groups;g++) best->captures[g]=(find_capture){t.tags[2*g],t.tags[2*g+1]};
            }
            break;
        case S_SPLIT: {
            thread second=t; second.state=st->other; s->stack[top++]=second;
            t.state=st->out; s->stack[top++]=t; break;
        }
        case S_SAVE:
            t.tags[st->tag]=pos; t.state=st->out; s->stack[top++]=t; break;
        case S_BOL:
            if (pos==0 || get_byte(r,pos-1)=='\n') { t.state=st->out; s->stack[top++]=t; }
            break;
        case S_EOL:
            if (pos==r->len || get_byte(r,pos)=='\n') { t.state=st->out; s->stack[top++]=t; }
            break;
        case S_JUMP: t.state=st->out; s->stack[top++]=t; break;
        }
    }
    return count;
}
static size_t start_candidate(reader *r,const find_regex *re,scratch *s,
                              uint64_t off,regex_meter *m,find_match *hit)
{
    clear_match(hit); hit->groups=re->groups;
    thread seed={0}; seed.state=re->start;
    for (size_t i=0;i<2*FIND_MAX_GROUPS;i++) seed.tags[i]=FIND_UNSET;
    memset(s->seen,0,sizeof s->seen);
    return closure(r,re,s,seed,off,s->lists[0],0,m,hit);
}

static size_t advance_candidate(reader *r,const find_regex *re,scratch *s,
                                uint64_t pos,unsigned current,size_t count,
                                regex_meter *m,find_match *hit)
{
    uint8_t byte=get_byte(r,pos);
    size_t next_count=0;
    memset(s->seen,0,sizeof s->seen);
    for (size_t i=0;i<count && !regex_stopped(m);i++) {
        thread t=s->lists[current][i];
        if (regex_step(m)) break;
        const state *st=&re->states[t.state];
        if (class_has(st->cls,byte)) {
            t.state=st->out;
            next_count=closure(r,re,s,t,pos+1,s->lists[1u-current],next_count,m,hit);
        }
    }
    return next_count;
}

static void anchored(reader *r,const find_regex *re,uint64_t off,scratch *s,regex_meter *m,
                     find_match *hit)
{
    size_t count=start_candidate(r,re,s,off,m,hit);
    unsigned current=0;
    for (uint64_t pos=off;count && pos<r->len && !regex_stopped(m);pos++) {
        count=advance_candidate(r,re,s,pos,current,count,m,hit);
        current=1u-current;
    }
    if (hit->matched) hit->whole.start=off;
}
/* Candidate starts come from the literal kernel (rare-byte filter + verify,
 * Two-Way fallback) when the program has a mandatory literal prefix. */
static bool regex_seek(reader *r,const find_regex *re,uint64_t off,scratch *s,regex_meter *m,
                       find_lit *lit,find_match *hit)
{
    if (re->prefix_len) {
        uint64_t at=0;
        for (uint64_t i=off;;) {
            if (find_lit_seek(lit,r->source,r->len,i,&at)<=0) return false;
            anchored(r,re,at,s,m,hit);
            if (hit->matched) return true;
            if (regex_stopped(m) || at==r->len) return false;
            if (regex_step(m)) return false;
            i=at+1;
        }
    }
    for (uint64_t i=off;;i++) {
        if (regex_step(m)) break;
        anchored(r,re,i,s,m,hit);
        if (hit->matched) return true;
        if (regex_stopped(m) || i==r->len) break;
    }
    return false;
}
static find_code regex_arguments(const find_source *source,const find_regex *re,void *memory,size_t size)
{
    if (!source_valid(source) || !re || re->magic!=FIND_MAGIC || !memory ||
        (uintptr_t)memory%_Alignof(max_align_t)!=0) return FIND_ERR_ARGUMENT;
    return size<sizeof(scratch)?FIND_ERR_MEMORY:FIND_OK;
}
static find_code regex_one(const find_source *source,const find_regex *re,uint64_t off,
                           void *memory,size_t size,const find_control *control,find_match *match,bool scan,
                           uint64_t *budget)
{
    if (!match) return FIND_ERR_ARGUMENT;
    clear_match(match);
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    if (off>source_len(source)) return FIND_ERR_ARGUMENT;
    reader r=reader_init(source); regex_meter m=regex_meter_init(source,re,control,r.len); match->groups=re->groups;
    if (budget) m.remaining=*budget;
    if (!poll_stop(&m.poll)) {
        if (scan) {
            find_lit lit;
            if (!re->prefix_len || find_lit_init(&lit,source,re->prefix,re->prefix_len,(find_literal_options){0},&m.poll,false))
                (void)regex_seek(&r,re,off,memory,&m,&lit,match);
        }
        else anchored(&r,re,off,memory,&m,match);
    }
    if (budget) *budget=m.remaining;
    if (poll_stop(&m.poll)) { clear_match(match); return FIND_CANCELLED; }
    if (m.limited) { clear_match(match); return FIND_ERR_LIMIT; }
    return FIND_OK;
}
find_code find_regex_captures(const find_source *source,const find_regex *re,uint64_t off,
                              void *memory,size_t size,const find_control *control,find_match *match)
{ return regex_one(source,re,off,memory,size,control,match,false,NULL); }
find_code find_regex_next_budget(const find_source *source,const find_regex *re,uint64_t off,
                                void *memory,size_t size,const find_control *control,
                                uint64_t *budget,find_match *match)
{
    if (!budget) { if (match) clear_match(match); return FIND_ERR_ARGUMENT; }
    return regex_one(source,re,off,memory,size,control,match,true,budget);
}
static bool regex_word_boundary(reader *r,find_capture range,regex_meter *m)
{
    bool accepted=true;
    if (range.start) {
        if (regex_step(m)) return false;
        if (ascii_word(get_byte(r,range.start-1))) accepted=false;
    }
    if (range.end<r->len) {
        if (regex_step(m)) return false;
        if (ascii_word(get_byte(r,range.end))) accepted=false;
    }
    return accepted;
}
static find_code regex_search_run(const find_source *source,const find_regex *re,
                                 void *memory,size_t size,const find_control *control,
                                 find_sink *sink,bool whole_word)
{
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    reader r=reader_init(source), boundaries=reader_init(source);
    regex_meter m=regex_meter_init(source,re,control,r.len);
    find_lit lit;
    bool ready=!poll_stop(&m.poll);
    if (ready && re->prefix_len)
        ready=find_lit_init(&lit,source,re->prefix,re->prefix_len,(find_literal_options){0},&m.poll,false);
    if (ready) for (uint64_t off=0;;) {
        find_match hit;
        if (!regex_seek(&r,re,off,memory,&m,&lit,&hit)) break;
        bool accepted=!whole_word || regex_word_boundary(&boundaries,hit.whole,&m);
        if (regex_stopped(&m)) break;
        if (accepted) {
            code=find_sink_add(sink,&m.poll,hit.whole);
            if (code!=FIND_OK) break;
        }
        if (regex_step(&m)) break;
        off=accepted ? hit.whole.end : hit.whole.start;
        if (off==hit.whole.start) {
            if (off==r.len) break;
            off++;
        }
    }
    if (m.limited) code=FIND_ERR_LIMIT;
    return find_sink_finish(sink,&m.poll,code);
}

static find_code regex_visit_run(const find_source *source,const find_regex *re,void *memory,size_t size,
                                 const find_control *control,find_visit *visit,bool whole_word)
{
    if (!visit) return FIND_ERR_ARGUMENT;
    visit->total=0; visit->visible=0;
    if (!visit->emit) return FIND_ERR_ARGUMENT;
    find_sink sink={.visit=visit};
    return regex_search_run(source,re,memory,size,control,&sink,whole_word);
}
find_code find_regex_visit(const find_source *source,const find_regex *re,void *memory,size_t size,
                           const find_control *control,find_visit *visit)
{ return regex_visit_run(source,re,memory,size,control,visit,false); }
find_code find_regex_word_visit(const find_source *source,const find_regex *re,void *memory,size_t size,
                                const find_control *control,find_visit *visit)
{ return regex_visit_run(source,re,memory,size,control,visit,true); }

/* The continuation preserves one candidate's lists and endpoint across
 * yields. Its prefix cursor preserves overlaps from rejected candidates. */
typedef enum stream_phase { STREAM_SEEK, STREAM_MATCH, STREAM_DONE } stream_phase;

typedef struct regex_stream {
    scratch nfa;
    find_source source;
    const find_regex *re;
    reader bytes, prefix, boundaries;
    regex_meter m;
    find_match hit;
    uint64_t candidate, pos, prefix_pos, total;
    size_t count, prefix_matched, failure[FIND_MAX_STATES];
    unsigned current;
    stream_phase phase;
    find_code code;
    bool whole_word;
} regex_stream;

_Static_assert(sizeof(find_regex) <= FIND_MAX_PROGRAM_BYTES, "prepaid regex program");
_Static_assert(sizeof(regex_stream) <= FIND_MAX_SCRATCH_BYTES, "prepaid regex continuation");

size_t find_regex_stream_scratch_bytes(const find_regex *re)
{
    return re ? sizeof(regex_stream) : 0;
}

find_code find_regex_stream_begin(const find_source *source,const find_regex *re,
                                  void *memory,size_t size,bool whole_word)
{
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    if (size<sizeof(regex_stream)) return FIND_ERR_MEMORY;
    regex_stream *s=memory;
    memset(s,0,sizeof *s);
    s->source=*source;
    s->re=re;
    s->whole_word=whole_word;
    s->bytes=reader_init(&s->source);
    s->prefix=reader_init(&s->source);
    s->boundaries=reader_init(&s->source);
    s->m=regex_meter_init(&s->source,re,NULL,s->bytes.len);
    for (size_t i=1;i<re->prefix_len;i++) {
        size_t k=s->failure[i-1];
        while (k && re->prefix[k]!=re->prefix[i]) k=s->failure[k-1];
        s->failure[i]=k+(re->prefix[k]==re->prefix[i]);
    }
    return FIND_OK;
}

find_code find_regex_stream_step(void *memory,const find_control *control,find_visit *visit,bool *done)
{
    if (!done) return FIND_ERR_ARGUMENT;
    *done=false;
    if (!memory || (uintptr_t)memory%_Alignof(max_align_t)!=0 || !visit || !visit->emit)
        return FIND_ERR_ARGUMENT;
    regex_stream *s=memory;
    const find_regex *re=s->re;
    if (!s->total) visit->visible=0;
    find_sink sink={.visit=visit,.total=s->total};
    s->m.poll.control=control;
    uint64_t initial=s->m.remaining;
    size_t prefix_units=0;
    if (s->code!=FIND_OK || poll_stop(&s->m.poll)) goto finish;
    while (initial-s->m.remaining+prefix_units<FIND_POLL_UNITS && !regex_stopped(&s->m)) {
        switch (s->phase) {
        case STREAM_SEEK:
            if (re->prefix_len) {
                bool found=false;
                while (s->prefix_pos<s->bytes.len) {
                    if (step(&s->m.poll)) goto finish;
                    uint8_t byte=get_byte(&s->prefix,s->prefix_pos++);
                    prefix_units++;
                    while (s->prefix_matched && re->prefix[s->prefix_matched]!=byte) {
                        if (step(&s->m.poll)) goto finish;
                        s->prefix_matched=s->failure[s->prefix_matched-1];
                        prefix_units++;
                    }
                    if (re->prefix[s->prefix_matched]==byte) s->prefix_matched++;
                    if (s->prefix_matched==re->prefix_len) {
                        s->candidate=s->prefix_pos-re->prefix_len;
                        s->prefix_matched=s->failure[s->prefix_matched-1];
                        found=true;
                        break;
                    }
                    if (initial-s->m.remaining+prefix_units>=FIND_POLL_UNITS) goto finish;
                }
                if (!found) { s->phase=STREAM_DONE; goto finish; }
            }
            if (!re->prefix_len && regex_step(&s->m)) goto finish;
            s->count=start_candidate(&s->bytes,re,&s->nfa,s->candidate,&s->m,&s->hit);
            s->current=0;
            s->pos=s->candidate;
            s->phase=STREAM_MATCH;
            break;
        case STREAM_MATCH: {
            if (s->count && s->pos<s->bytes.len) {
                s->count=advance_candidate(&s->bytes,re,&s->nfa,s->pos,s->current,s->count,&s->m,&s->hit);
                s->current=1u-s->current;
                s->pos++;
                break;
            }
            s->hit.whole.start=s->candidate;
            bool accepted=s->hit.matched && (!s->whole_word ||
                regex_word_boundary(&s->boundaries,s->hit.whole,&s->m));
            if (regex_stopped(&s->m)) goto finish;
            if (accepted) {
                s->code=find_sink_add(&sink,&s->m.poll,s->hit.whole);
                if (s->code!=FIND_OK) goto finish;
            }
            /* Match the synchronous budget, including retirement of an EOF
             * candidate. A rejected word or empty match advances one byte. */
            if ((re->prefix_len || s->hit.matched) && regex_step(&s->m)) goto finish;
            uint64_t next=accepted?s->hit.whole.end:s->candidate;
            if (next==s->candidate) {
                if (next==s->bytes.len) { s->phase=STREAM_DONE; goto finish; }
                next++;
            }
            s->candidate=next;
            if (accepted) {
                s->prefix_pos=next;
                s->prefix_matched=0;
            }
            s->phase=STREAM_SEEK;
            break;
        }
        case STREAM_DONE:
            goto finish;
        }
    }
finish:
    if (s->m.limited) s->code=FIND_ERR_LIMIT;
    s->code=find_sink_finish(&sink,&s->m.poll,s->code);
    s->m.poll.control=NULL;
    s->total=visit->total;
    *done=s->code==FIND_OK && s->phase==STREAM_DONE;
    return s->code;
}

/* ASCII-insensitive query expansion by literal and class membership. */
static uint8_t other_case(uint8_t byte)
{ return (uint8_t)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte + ('a' - 'A')); }

/* The byte-sensitive compiler owns syntax validation. This transformer only rewrites
 * successfully validated expressions; escaped letters remain literal atoms,
 * and class negation is applied AFTER expanding ASCII case membership. */
typedef struct builder {
    const work_ctx *context;
    uint8_t *bytes;
    size_t length, units, capacity;
    find_code code;
} builder;
static void tick(builder *b)
{
    b->units++;
    if (b->units >= FIND_POLL_UNITS) {
        b->units = 0;
        if (b->context && work_should_stop(b->context)) b->code = FIND_CANCELLED;
    }
}
static void put(builder *b, uint8_t byte)
{
    tick(b);
    if (b->code != FIND_OK) return;
    if (b->length == b->capacity) { b->code = FIND_ERR_LIMIT; return; }
    b->bytes[b->length++] = byte;
}
static void class_atom(builder *b, uint8_t byte)
{
    if (byte == '[' || byte == ']' || byte == '\\' || byte == '-' || byte == '^') put(b, '\\');
    put(b, byte);
}
static void literal_atom(builder *b, uint8_t byte)
{
    if (ascii_letter(byte)) {
        put(b, '['); put(b, byte); put(b, other_case(byte)); put(b, ']');
    } else {
        if (byte == '[' || byte == ']' || byte == '\\' || byte == '(' || byte == ')' ||
            byte == '|' || byte == '.' || byte == '*' || byte == '+' || byte == '?' ||
            byte == '^' || byte == '$' || byte == '{' || byte == '}') put(b, '\\');
        put(b, byte);
    }
}
static uint8_t folded_class_byte(const uint8_t *query, size_t *at)
{
    uint8_t byte = query[(*at)++];
    if (byte == '\\') byte = query[(*at)++];
    return byte;
}
/* (P) One membership bit per possible byte before range serialization. */
static void folded_class(builder *b, const uint8_t *query, size_t *at)
{
    bool bits[256] = {false};
    bool negate = query[*at] == '^';
    if (negate) (*at)++;
    while (query[*at] != ']') {
        uint8_t low = folded_class_byte(query, at), high = low;
        if (query[*at] == '-' && query[*at + 1] != ']') {
            (*at)++; high = folded_class_byte(query, at);
        }
        for (unsigned v = low; v <= (unsigned)high; v++) { tick(b); bits[v] = true; }
    }
    (*at)++;
    for (unsigned v = 'a'; v <= (unsigned)'z'; v++) {
        unsigned upper = v - (unsigned)('a' - 'A');
        if (bits[v] || bits[upper]) { bits[v] = true; bits[upper] = true; }
    }
    put(b, '['); if (negate) put(b, '^');
    for (unsigned v = 0; v < 256; v++) {
        tick(b);
        if (!bits[v]) continue;
        unsigned end = v;
        while (end + 1 < 256 && bits[end + 1]) { tick(b); end++; }
        class_atom(b, (uint8_t)v);
        if (end - v >= 2) { put(b, '-'); class_atom(b, (uint8_t)end); }
        else if (end != v) class_atom(b, (uint8_t)end);
        v = end;
    }
    put(b, ']');
}
find_code find_regex_compile_options(void *program, size_t program_bytes,
    const uint8_t *query, size_t query_length, bool fold_ascii,
    uint8_t *pattern, size_t pattern_capacity, const work_ctx *context,
    find_regex **regex, size_t *error_offset)
{
    if (error_offset) *error_offset = 0;
    if (!regex) return FIND_ERR_ARGUMENT;
    *regex = NULL;
    if (fold_ascii && !pattern) return FIND_ERR_ARGUMENT;
    if (context && work_should_stop(context)) return FIND_CANCELLED;
    find_regex *checked;
    find_code code = find_regex_compile(program, program_bytes, query, query_length,
                                        &checked, error_offset);
    if (code != FIND_OK) return code;
    if (!fold_ascii) {
        if (context && work_should_stop(context)) return FIND_CANCELLED;
        *regex = checked;
        return FIND_OK;
    }
    builder b = {.context = context, .bytes = pattern,
                 .capacity = pattern_capacity, .code = FIND_OK};
    for (size_t at = 0; at < query_length && b.code == FIND_OK;) {
        uint8_t byte = query[at++];
        if (byte == '[') folded_class(&b, query, &at);
        else if (byte == '\\') literal_atom(&b, query[at++]);
        else if (ascii_letter(byte)) literal_atom(&b, byte);
        else put(&b, byte);
    }
    if (b.code != FIND_OK || (context && work_should_stop(context))) {
        if (error_offset && b.code == FIND_ERR_LIMIT) *error_offset = query_length;
        return b.code == FIND_OK ? FIND_CANCELLED : b.code;
    }
    code = find_regex_compile(program, program_bytes, b.bytes, b.length, &checked, error_offset);
    if (code != FIND_OK) {
        if (error_offset) *error_offset = query_length;
        return code;
    }
    if (context && work_should_stop(context)) return FIND_CANCELLED;
    *regex = checked;
    return FIND_OK;
}
