/* P1.10 literal search, variant simd-filter-verify. See
 * docs/decisions/P1.10-simd-filter-verify.md. */
#include "literal.h"
#include <emmintrin.h>
#include <immintrin.h>
#include "base/base.h"

#define WINMAX 4096u      /* stack window for matches straddling snapshot spans */
#define NSNAP_MAX 2048u   /* longest needle using windows; longer: reader Two-Way */

/* Static byte rank: higher = more common in text/code/logs. */
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
/* 0 equal, 1 differ, -1 cancelled; polls every <=1024 compared bytes. */
static int cmp_poll(meter *m,const uint8_t *a,const uint8_t *b,size_t len)
{
    while (len) {
        size_t k=len<1024?len:1024;
        if (charge(m,k)) return -1;
        if (memcmp(a,b,k)!=0) return 1;
        a+=k; b+=k; len-=k;
    }
    return 0;
}

static find_lit_blk scan_sse2(const uint8_t *a,const uint8_t *b,uint8_t c1,uint8_t c2,size_t i,size_t e);
static find_lit_blk scan_avx2(const uint8_t *a,const uint8_t *b,uint8_t c1,uint8_t c2,size_t i,size_t e);
bool find_lit_init(find_lit *l,const uint8_t *nd,size_t n,meter *m,bool two_way)
{
    memset(l,0,sizeof *l);
    l->nd=nd; l->n=n; l->m=m; l->tw=two_way;
    l->scan=edit_cpu_has_avx2()?scan_avx2:scan_sse2;
    unsigned best=256; size_t p1=0;
    for (size_t i=0;i<n;i++) {
        if ((i&63u)==0 && charge(m,64)) return false;
        if (k_rank[nd[i]]<best) { best=k_rank[nd[i]]; p1=i; }
    }
    best=256; size_t p2=n;
    for (size_t i=0;i<n;i++) {
        if ((i&63u)==0 && charge(m,128)) return false;
        if (nd[i]!=nd[p1] && k_rank[nd[i]]<=best) { best=k_rank[nd[i]]; p2=i; }
    }
    if (p2==n) p2=(p1==n-1)?0:n-1;
    l->p1=p1; l->p2=p2; l->c1=nd[p1]; l->c2=nd[p2];
    return true;
}

/* ---- Two-Way (Crochemore-Perrin), no tables, no allocation ------------- */
static bool max_suffix(find_lit *l,bool rev,size_t *ell,size_t *per)
{
    const uint8_t *x=l->nd; size_t m=l->n, ms1=0, j=0, k=1, p=1; /* ms1 = ms+1 */
    while (j+k<m) {
        if (charge(l->m,1)) return false;
        uint8_t a=x[j+k], b=x[ms1-1+k];
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
        int c=cmp_poll(l->m,l->nd,l->nd+per,ell);
        if (c<0) return false;
        periodic=c==0;
    }
    l->ell=ell; l->periodic=periodic;
    l->per=periodic?per:((ell>l->n-ell?ell:l->n-ell)+1);
    l->tw_ready=true; return true;
}
#define UNIT() do { if (charge(m,1)) return -1; } while (0)
/* Leftmost match start in [from, len-n]. contig: hp/len bytes; else reader. */
static inline __attribute__((always_inline))
int tw_core(find_lit *l,bool contig,const uint8_t *hp,reader *rd,uint64_t len,uint64_t from,uint64_t *out)
{
    const uint8_t *nd=l->nd; size_t n=l->n, ell=l->ell, per=l->per; meter *m=l->m;
#define HB(x) (contig ? hp[(size_t)(x)] : get_byte(rd,(x)))
    if (n>len || from>len-n) return 0;
    uint64_t last=len-n, j=from; size_t memory=0;
    if (l->periodic) {
        while (j<=last) {
            size_t i=ell>memory?ell:memory;
            while (i<n) { UNIT(); if (nd[i]!=HB(j+i)) break; i++; }
            if (i>=n) {
                size_t k=ell;
                while (k>memory) { UNIT(); if (nd[k-1]!=HB(j+k-1)) break; k--; }
                UNIT();
                if (k<=memory) { *out=j; return 1; }
                j+=per; memory=n-per;
            } else { UNIT(); j+=i-ell+1; memory=0; }
        }
    } else {
        while (j<=last) {
            size_t i=ell;
            while (i<n) { UNIT(); if (nd[i]!=HB(j+i)) break; i++; }
            if (i>=n) {
                size_t k=ell;
                while (k>0) { UNIT(); if (nd[k-1]!=HB(j+k-1)) break; k--; }
                UNIT();
                if (k==0) { *out=j; return 1; }
                j+=per;
            } else { UNIT(); j+=i-ell+1; }
        }
    }
#undef HB
    return 0;
}
static int tw_buf(find_lit *l,const uint8_t *h,size_t len,size_t from,size_t *out)
{
    if (!tw_prepare(l)) return -1;
    uint64_t o=0; int r=tw_core(l,true,h,NULL,len,from,&o);
    *out=(size_t)o; return r;
}

/* ---- contiguous kernels ------------------------------------------------ */
static int seek_byte(find_lit *l,const uint8_t *h,size_t len,size_t from,size_t *out)
{
    meter *m=l->m; size_t i=from; const __m128i v=_mm_set1_epi8((char)l->c1);
    while (i+16<=len) {
        if (charge(m,32)) return -1;
        unsigned mk=(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(h+i)),v));
        if (mk) { *out=i+(size_t)__builtin_ctz(mk); return 1; }
        i+=16;
    }
    for (;i<len;i++) { if (charge(m,2)) return -1; if (h[i]==l->c1) { *out=i; return 1; } }
    return 0;
}
/* 0 = needle present at p, 1 = differs, -1 cancelled. Charged in full. */
static inline int verify(find_lit *l,const uint8_t *p)
{
    l->verified+=l->n;
    return cmp_poll(l->m,p,l->nd,l->n);
}
/* Pair-filter block scanners: first 32-start block in [i,e) (e-i multiple of
 * 32) with a candidate; mask bit k = start at+k passes both filter bytes. */
static find_lit_blk scan_sse2(const uint8_t *a,const uint8_t *b,uint8_t c1,uint8_t c2,size_t i,size_t e)
{
    const __m128i v1=_mm_set1_epi8((char)c1), v2=_mm_set1_epi8((char)c2);
    for (;i<e;i+=32) {
        const __m128i a0=_mm_loadu_si128((const __m128i *)(const void *)(a+i));
        const __m128i b0=_mm_loadu_si128((const __m128i *)(const void *)(b+i));
        const __m128i a1=_mm_loadu_si128((const __m128i *)(const void *)(a+i+16));
        const __m128i b1=_mm_loadu_si128((const __m128i *)(const void *)(b+i+16));
        unsigned m0=(unsigned)_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(a0,v1),_mm_cmpeq_epi8(b0,v2)));
        unsigned m1=(unsigned)_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(a1,v1),_mm_cmpeq_epi8(b1,v2)));
        if (m0|m1) return (find_lit_blk){i,m0|(m1<<16)};
    }
    return (find_lit_blk){e,0};
}
__attribute__((target("avx2")))
static find_lit_blk scan_avx2(const uint8_t *a,const uint8_t *b,uint8_t c1,uint8_t c2,size_t i,size_t e)
{
    const __m256i v1=_mm256_set1_epi8((char)c1), v2=_mm256_set1_epi8((char)c2);
    for (;i+64<=e;i+=64) {
        const __m256i a0=_mm256_loadu_si256((const __m256i *)(const void *)(a+i));
        const __m256i b0=_mm256_loadu_si256((const __m256i *)(const void *)(b+i));
        const __m256i a1=_mm256_loadu_si256((const __m256i *)(const void *)(a+i+32));
        const __m256i b1=_mm256_loadu_si256((const __m256i *)(const void *)(b+i+32));
        const __m256i x0=_mm256_and_si256(_mm256_cmpeq_epi8(a0,v1),_mm256_cmpeq_epi8(b0,v2));
        const __m256i x1=_mm256_and_si256(_mm256_cmpeq_epi8(a1,v1),_mm256_cmpeq_epi8(b1,v2));
        if (!_mm256_testz_si256(_mm256_or_si256(x0,x1),_mm256_or_si256(x0,x1))) {
            unsigned m0=(unsigned)_mm256_movemask_epi8(x0);
            if (m0) return (find_lit_blk){i,m0};
            return (find_lit_blk){i+32,(unsigned)_mm256_movemask_epi8(x1)};
        }
    }
    for (;i<e;i+=32) {
        const __m256i x=_mm256_and_si256(
            _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(a+i)),v1),
            _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(b+i)),v2));
        unsigned mk=(unsigned)_mm256_movemask_epi8(x);
        if (mk) return (find_lit_blk){i,mk};
    }
    return (find_lit_blk){e,0};
}
/* 1 found (*out), 0 none, -1 cancelled. Starts examined: [from, len-n]. */
static int seek_buf(find_lit *l,const uint8_t *h,size_t len,size_t from,size_t *out)
{
    size_t n=l->n;
    if (n>len || from>len-n) return 0;
    if (n==1) return seek_byte(l,h,len,from,out);
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
            find_lit_blk k=l->scan(a,b,l->c1,l->c2,i,e);
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
                if (v==0) { *out=cand; return 1; }
                if (l->verified>4*l->scanned+8*(uint64_t)n) { l->tw=true; i=cand+1; goto fallback; }
            }
            i=k.at+32;
        }
        for (;i<=last;i++) {
            if (charge(m,3)) return -1;
            l->scanned++;
            if (h[i+l->p1]==l->c1 && h[i+l->p2]==l->c2) {
                int v=verify(l,h+i);
                if (v<0 || charge(m,1)) return -1;
                if (v==0) { *out=i; return 1; }
                if (l->verified>4*l->scanned+8*(uint64_t)n) { l->tw=true; i++; goto fallback; }
            }
        }
        return 0;
    }
fallback:
    return tw_buf(l,h,len,i,out);
}

/* ---- snapshots --------------------------------------------------------- */
static size_t gather(const piece_snapshot *s,uint8_t *dst,uint64_t from,size_t count,meter *m)
{
    piece_iter it; const uint8_t *p; size_t sn, got=0;
    piece_iter_begin_snapshot(&it,s,from);
    while (got<count && piece_iter_next(&it,&p,&sn)) {
        size_t k=sn<count-got?sn:count-got;
        while (k) {
            size_t chunk=k<FIND_POLL_UNITS?k:FIND_POLL_UNITS;
            if (charge(m,chunk)) return 0;
            memcpy(dst+got,p,chunk); got+=chunk; p+=chunk; k-=chunk;
        }
    }
    return got;
}
static int seek_snap(find_lit *l,const find_source *src,uint64_t len,uint64_t from,uint64_t *out)
{
    size_t n=l->n; const piece_snapshot *s=src->snapshot;
    if ((uint64_t)n>len || from>len-n) return 0;
    if (n>NSNAP_MAX) {
        if (!tw_prepare(l)) return -1;
        reader rd=reader_init(src);
        return tw_core(l,false,NULL,&rd,len,from,out);
    }
    uint8_t win[WINMAX];
    piece_iter it; const uint8_t *p; size_t sn, o;
    uint64_t next=from, g=from;
    piece_iter_begin_snapshot(&it,s,from);
    if (!piece_iter_next(&it,&p,&sn)) return 0;
    for (;;) {
        if (next>len-n) return 0;
        if (charge(l->m,1)) return -1;
        int r;
        if (n==1 || sn>=2*n) {
            uint64_t gend=g+sn;
            if (gend-n>=next) {
                r=seek_buf(l,p,sn,(size_t)(next-g),&o);
                if (r!=0) { if (r>0) *out=g+o; return r; }
            }
            if (n>1) {
                uint64_t s0=gend-n+1>next?gend-n+1:next;
                uint64_t e=gend+n-1<len?gend+n-1:len;
                if (e>s0) {
                    size_t wl=gather(s,win,s0,(size_t)(e-s0),l->m);
                    if (l->m->stopped) return -1;
                    r=seek_buf(l,win,wl,0,&o);
                    if (r!=0) { if (r>0) *out=s0+o; return r; }
                }
            }
            next=gend;
            if (!piece_iter_next(&it,&p,&sn)) return 0;
            g=next;
        } else {
            uint64_t rem=len-next; size_t w=rem<WINMAX?(size_t)rem:WINMAX;
            size_t wl=gather(s,win,next,w,l->m);
            if (l->m->stopped) return -1;
            r=seek_buf(l,win,wl,0,&o);
            if (r!=0) { if (r>0) *out=next+o; return r; }
            if (w==rem) return 0;
            next+=w-n+1;
            piece_iter_begin_snapshot(&it,s,next);
            if (!piece_iter_next(&it,&p,&sn)) return 0;
            g=next;
        }
    }
}
int find_lit_seek(find_lit *l,const find_source *s,uint64_t len,uint64_t from,uint64_t *out)
{
    if (s->snapshot) return seek_snap(l,s,len,from,out);
    size_t o=0; int r=seek_buf(l,s->bytes,s->length,(size_t)from,&o);
    *out=o; return r;
}

/* ---- single-byte counting fast path ------------------------------------ */
static find_code count_byte(find_lit *l,const uint8_t *p,size_t sn,uint64_t base,find_result *r)
{
    meter *m=l->m; const __m128i v=_mm_set1_epi8((char)l->c1); size_t i=0;
#define LD(k) _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p+i+(k)*16)),v)
    while (i+64<=sn) {
        if (charge(m,128)) return FIND_CANCELLED;
        uint64_t mk=(uint64_t)(unsigned)_mm_movemask_epi8(LD(0))|((uint64_t)(unsigned)_mm_movemask_epi8(LD(1))<<16)|
                    ((uint64_t)(unsigned)_mm_movemask_epi8(LD(2))<<32)|((uint64_t)(unsigned)_mm_movemask_epi8(LD(3))<<48);
        if (mk) {
            if (r->stored>=FIND_MAX_OFFSETS) { while (mk) { mk&=mk-1; r->total++; } }
            else while (mk) {
                find_code c=add_result(r,base+i+(size_t)__builtin_ctzll(mk));
                if (c!=FIND_OK) return c;
                mk&=mk-1;
            }
        }
        i+=64;
    }
#undef LD
    for (;i<sn;i++) {
        if (charge(m,2)) return FIND_CANCELLED;
        if (p[i]==l->c1) { find_code c=add_result(r,base+i); if (c!=FIND_OK) return c; }
    }
    return FIND_OK;
}

/* ---- public ------------------------------------------------------------ */
find_code find_literal_mode(const find_source *source,const uint8_t *needle,size_t n,
                            const find_control *control,find_result *result,int mode)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->total=0; result->stored=0;
    if (!source_valid(source) || (!needle && n)) return FIND_ERR_ARGUMENT;
    meter m={control,0,false}; find_code code=FIND_OK;
    if (!poll_stop(&m) && n>0) {
        find_lit l; uint64_t len=source_len(source);
        if (find_lit_init(&l,needle,n,&m,mode==1)) {
            if (n==1 && mode==0) {
                if (!source->snapshot) code=count_byte(&l,source->bytes,source->length,0,result);
                else {
                    piece_iter it; const uint8_t *p; size_t sn; uint64_t base=0;
                    piece_iter_begin_snapshot(&it,source->snapshot,0);
                    while (code==FIND_OK && piece_iter_next(&it,&p,&sn)) {
                        code=count_byte(&l,p,sn,base,result); base+=sn;
                    }
                }
            } else {
                uint64_t pos=0, at=0;
                for (;;) {
                    int r=find_lit_seek(&l,source,len,pos,&at);
                    if (r<=0) break;
                    code=add_result(result,at);
                    if (code!=FIND_OK) break;
                    pos=at+n;
                }
            }
        }
    }
    if (poll_stop(&m)) code=FIND_CANCELLED;
    if (code!=FIND_OK) { result->total=0; result->stored=0; }
    return code;
}
find_code find_literal(const find_source *source,const uint8_t *needle,size_t n,
                       const find_control *control,find_result *result)
{ return find_literal_mode(source,needle,n,control,result,0); }

find_code find_literal_next(const find_source *source,const uint8_t *needle,size_t n,uint64_t off,
                            const find_control *control,find_match *match)
{
    if (!match) return FIND_ERR_ARGUMENT;
    clear_match(match);
    if (!source_valid(source) || (!needle && n) || off>source_len(source)) return FIND_ERR_ARGUMENT;
    meter m={control,0,false};
    if (!poll_stop(&m) && n>0) {
        find_lit l; uint64_t at=0;
        if (find_lit_init(&l,needle,n,&m,false) && find_lit_seek(&l,source,source_len(source),off,&at)>0) {
            match->matched=true; match->whole=(find_capture){at,at+n};
        }
    }
    if (poll_stop(&m)) { clear_match(match); return FIND_CANCELLED; }
    return FIND_OK;
}
