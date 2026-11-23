/* Literal SIMD filter/verify with bounded work and a Two-Way fallback. */
#include "literal.h"
#include <emmintrin.h>
#include <immintrin.h>
#include <limits.h>
#include "base/base.h"

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

bool find_lit_init(find_lit *l,const find_source *source,const uint8_t *nd,size_t n,
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
int find_lit_seek(find_lit *l,const find_source *s,uint64_t len,uint64_t from,uint64_t *out)
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
    if (!v) return true;
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
    find_result *r = sink->result;
    while (mask && r && r->stored < FIND_MAX_OFFSETS) {
        r->offsets[r->stored++] = base + (unsigned)__builtin_ctzll(mask);
        mask &= mask - 1;
    }
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

static find_code literal_result(const find_source *source, const uint8_t *needle, size_t n,
                                find_literal_options options, const find_control *control,
                                find_result *result, bool two_way)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->total = 0;
    result->stored = 0;
    find_sink sink = {.result = result};
    return literal_run(source, needle, n, options, control, &sink, two_way);
}

find_code find_literal(const find_source *source, const uint8_t *needle, size_t n,
                       find_literal_options options, const find_control *control,
                       find_result *result)
{
    return literal_result(source, needle, n, options, control, result, false);
}

#ifdef FIND_TEST_HOOKS
find_code find_test_literal_two_way(const find_source *source, const uint8_t *needle, size_t n,
                                   find_literal_options options, const find_control *control,
                                   find_result *result)
{
    return literal_result(source, needle, n, options, control, result, true);
}
#endif

find_code find_literal_visit(const find_source *source, const uint8_t *needle, size_t n,
                             find_literal_options options, const find_control *control,
                             find_visit *visit)
{
    if (!visit) return FIND_ERR_ARGUMENT;
    visit->total = 0;
    visit->visible = 0;
    if (!visit->emit) return FIND_ERR_ARGUMENT;
    find_sink sink = {.visit = visit};
    return literal_run(source, needle, n, options, control, &sink, false);
}

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
