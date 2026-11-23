/* (G) Original sensitive fixtures and RNG from v1 tests/find_simd_filter_verify_test.c. */
/* Variant tests for P1.10 simd-filter-verify: Two-Way, filter positions,
 * budget fallback, snapshot span boundaries, long needles, cancellation. */
#include "find_fixture.h"
#include "piece/piece.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool force_sse;
bool __real_base_cpu_has_avx2(void);
bool __wrap_base_cpu_has_avx2(void)
{ return !force_sse && __real_base_cpu_has_avx2(); }

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while (0)

static uint64_t rng_state=0x9e3779b97f4a7c15ull;
static uint32_t rnd(void) { rng_state=rng_state*6364136223846793005ull+1442695040888963407ull; return (uint32_t)(rng_state>>33); }

/* Independent naive leftmost non-overlapping model. */
static uint8_t lowercase(uint8_t b)
{ return b>='A' && b<='Z' ? (uint8_t)(b-'A'+'a') : b; }

static bool word(uint8_t b)
{ return (lowercase(b)>='a' && lowercase(b)<='z') || (b>='0' && b<='9') || b=='_'; }

static void naive(const uint8_t *h,size_t hn,const uint8_t *nd,size_t n,
                  find_literal_options options,find_result *r)
{
    r->total=0; r->stored=0;
    if (n==0) return;
    for (size_t i=0;i+n<=hn;) {
        bool boundary=!options.whole_word ||
            ((!i || !word(h[i-1])) && (i+n==hn || !word(h[i+n])));
        size_t k=0;
        if (options.fold_ascii) {
            while (k<n && lowercase(h[i+k])==lowercase(nd[k])) k++;
        } else if (memcmp(h+i,nd,n)==0) k=n;
        if (k==n && boundary) {
            if (r->stored<FIND_MAX_OFFSETS) r->offsets[r->stored++]=i;
            r->total++; i+=n;
        } else i++;
    }
}
static int same(const find_result *a,const find_result *b)
{ return a->total==b->total && a->stored==b->stored && memcmp(a->offsets,b->offsets,a->stored*sizeof a->offsets[0])==0; }

static find_result got, want;
static int check_mode(const find_source *s,const uint8_t *h,size_t hn,const uint8_t *nd,size_t n,
                      find_literal_options options)
{
    naive(h,hn,nd,n,options,&want);
    for (int mode=0;mode<2;mode++) {
        find_code code=mode ? find_test_literal_two_way(s,nd,n,options,NULL,&got)
                            : find_literal(s,nd,n,options,NULL,&got);
        CHECK(code==FIND_OK);
        if (!same(&got,&want)) {
            fprintf(stderr,"mismatch mode=%d fold=%d word=%d hn=%zu n=%zu got=%llu want=%llu\n",
                    mode,options.fold_ascii,options.whole_word,hn,n,
                    (unsigned long long)got.total,(unsigned long long)want.total);
            return 1;
        }
    }
    /* next() from every found offset and from 0 */
    find_match m;
    uint64_t from=0;
    for (size_t i=0;i<=want.stored && i<8;i++) {
        CHECK(find_literal_next(s,nd,n,options,from,NULL,&m)==FIND_OK);
        if (i<want.stored) { CHECK(m.matched && m.whole.start==want.offsets[i] && m.whole.end==want.offsets[i]+n); from=m.whole.end; }
        else CHECK(!m.matched);
    }
    return 0;
}
static int check_source(const find_source *s,const uint8_t *h,size_t hn,const uint8_t *nd,size_t n)
{ return check_mode(s,h,hn,nd,n,(find_literal_options){0}); }

static int check_bytes(const uint8_t *h,size_t hn,const uint8_t *nd,size_t n)
{
    find_source s={h,hn,NULL};
    return check_source(&s,h,hn,nd,n);
}

static int exhaustive_binary(void)
{
    uint8_t h[16],nd[8];
    for (size_t n=1;n<=7;n++) for (unsigned nb=0;nb<(1u<<n);nb++) {
        for (size_t i=0;i<n;i++) nd[i]=(uint8_t)('a'+((nb>>i)&1u));
        for (size_t hn=0;hn<=13;hn++) for (unsigned hb=0;hb<(1u<<hn);hb+=(hn>10?7:1)) {
            for (size_t i=0;i<hn;i++) h[i]=(uint8_t)('a'+((hb>>i)&1u));
            CHECK(check_bytes(h,hn,nd,n)==0);
        }
    }
    return 0;
}
static int random_bytes(void)
{
    static uint8_t h[6000],nd[200];
    for (int it=0;it<3000;it++) {
        size_t hn=rnd()%6000; if (it%7==0) hn=rnd()%130;
        unsigned alpha=(it%4==0)?1u:(it%4==1)?2u:(it%4==2)?3u:256u;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)('a'+rnd()%alpha);
        size_t n=1+rnd()%(it%3==0?40:200);
        if (it%5==0) { /* periodic needle */
            size_t per=1+rnd()%5; for (size_t i=0;i<n;i++) nd[i]=(uint8_t)('a'+(i%per)%alpha);
            if (it%10==0) nd[n-1]=(uint8_t)(nd[n-1]+1); /* almost periodic */
            for (size_t i=0;i<hn;i+=1+rnd()%9) if (i+n<=hn && rnd()%3==0) memcpy(h+i,nd,n);
        } else if (hn>=n && it%2) { memcpy(nd,h+rnd()%(hn-n+1),n); if (it%3==0) nd[rnd()%n]^=1; }
        else for (size_t i=0;i<n;i++) nd[i]=(uint8_t)('a'+rnd()%alpha);
        CHECK(check_bytes(h,hn,nd,n)==0);
        /* (P) Change the case of selected lowercase haystack bytes. The
         * needle stays as generated and high bytes remain distinct. */
        for (size_t i=0;i<hn;i++)
            if ((i%2) && h[i]>='a' && h[i]<='z') h[i]=(uint8_t)(h[i]-('a'-'A'));
        find_source source={h,hn,NULL};
        CHECK(check_mode(&source,h,hn,nd,n,(find_literal_options){true,it%2!=0})==0);
    }
    return 0;
}
/* Adversarial for the filter: first/last bytes common, rare byte elsewhere. */
static int adversarial(void)
{
    static uint8_t h[200000],nd[64];
    memset(h,'a',sizeof h);
    for (size_t n=2;n<=64;n+=(n<40?1:9)) for (size_t pos=0;pos<n;pos+=1+n/9) {
        memset(nd,'a',n); nd[pos]='b';
        CHECK(check_bytes(h,sizeof h,nd,n)==0);          /* 0 hits */
        for (size_t k=0;k<sizeof h;k+=n+3+(k%31)) h[k]='b'; /* sprinkle b: filter hits */
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
        memset(h,'a',sizeof h);
        size_t at=sizeof h/2; memcpy(h+at,nd,n);
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
        memset(h,'a',sizeof h);
    }
    /* periodic haystack, periodic needle: dense hits and filter blowup */
    for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)(i%7==0?'b':'a');
    for (size_t n=2;n<=64;n++) {
        for (size_t i=0;i<n;i++) nd[i]=(uint8_t)((i+n)%7==0?'b':'a');
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
        for (size_t i=0;i<n;i++) nd[i]=(uint8_t)(i%7==0?'b':'a');
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
    }
    /* needle bytes all identical, hay almost all identical */
    memset(h,'x',sizeof h); h[100000]='y';
    for (size_t n=2;n<=64;n++) { memset(nd,'x',n); CHECK(check_bytes(h,sizeof h,nd,n)==0); }
    return 0;
}
static int long_needles(void)
{
    size_t hn=40000,n=5000;
    uint8_t *h=malloc(hn),*nd=malloc(n);
    CHECK(h&&nd);
    for (int round=0;round<4;round++) {
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)(round==0?'a':'a'+(i%3==0));
        for (size_t i=0;i<n;i++) nd[i]=(uint8_t)(round==0?'a':'a'+(i%3==0));
        if (round==2) nd[n-1]^=1;
        if (round==3) { for (size_t i=0;i<n;i++) nd[i]=(uint8_t)('a'+(i%3==0)); nd[2500]='z'; memcpy(h+7777,nd,n); }
        CHECK(check_bytes(h,hn,nd,n)==0);
        CHECK(check_bytes(h,hn,nd,n-1)==0);
    }
    /* needle longer than haystack and equal to it */
    CHECK(check_bytes(h,100,nd,n)==0);
    CHECK(check_bytes(nd,n,nd,n)==0);
    free(h); free(nd); return 0;
}

/* Fragmented snapshot of h cut at the given ascending cut offsets. */
static int check_snap(const uint8_t *h,size_t hn,const size_t *cuts,size_t ncuts,
                      const uint8_t *nd,size_t n,size_t need_spans)
{
    find_fixture f=find_fixture_snapshot(h,hn,cuts,ncuts);
    CHECK(find_fixture_spans(f)>=need_spans);
    find_source source=find_fixture_source(f);
    int rc=check_source(&source,h,hn,nd,n);
    find_fixture_release(f);
    return rc;
}
static int snapshots(void)
{
    uint8_t h[64],nd[16];
    /* every single split, every two splits, for several needles */
    for (int pat=0;pat<6;pat++) {
        size_t hn=pat<3?40:36,n=(size_t)(2+pat*2);
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)(pat==0?'a':pat==1?'a'+(i%2):'a'+(i%3==0));
        for (size_t i=0;i<n;i++) nd[i]=(uint8_t)(pat==0?'a':pat==1?'a'+(i%2):'a'+(i%3==0));
        if (pat==5) memcpy(nd,h+3,n);
        for (size_t c=1;c<hn;c++) { size_t cuts[1]={c}; CHECK(check_snap(h,hn,cuts,1,nd,n,2)==0); }
        for (size_t c=1;c<hn;c++) for (size_t d=c+1;d<hn;d++) { size_t cuts[2]={c,d}; CHECK(check_snap(h,hn,cuts,2,nd,n,3)==0); }
    }
    /* one byte per span */
    {
        size_t cuts[63],nc=0; for (size_t c=1;c<40;c++) cuts[nc++]=c;
        for (size_t i=0;i<40;i++) h[i]=(uint8_t)('a'+(i%3==0));
        for (size_t n=1;n<=9;n++) { for (size_t i=0;i<n;i++) nd[i]=(uint8_t)('a'+(i%3==0)); CHECK(check_snap(h,40,cuts,nc,nd,n,2)==0); }
    }
    /* random larger: big spans straddling, needle longer than a span, > 2048 */
    static uint8_t big[30000],bn[3000];
    for (int it=0;it<60;it++) {
        size_t hn=2000+rnd()%28000;
        unsigned alpha=(it%3==0)?1u:(it%3==1)?2u:4u;
        for (size_t i=0;i<hn;i++) big[i]=(uint8_t)('a'+rnd()%alpha);
        size_t n=2+rnd()%(it%4==0?3000:150); if (n>hn) n=hn;
        for (size_t i=0;i<n;i++) bn[i]=(uint8_t)('a'+rnd()%alpha);
        if (it%2) { memcpy(bn,big+hn/3,n<hn-hn/3?n:hn-hn/3); }
        size_t cuts[40],nc=1+rnd()%39,prev=0;
        for (size_t k=0;k<nc;k++) { size_t gap=1+rnd()%(it%5==0?30:(hn/nc)); if (prev+gap>=hn) { nc=k; break; } prev+=gap; cuts[k]=prev; }
        CHECK(check_snap(big,hn,cuts,nc,bn,n,1)==0);
    }
    return 0;
}
/* Weak filter: pair passes at 1/5 starts, verify cost n each -> budget trips,
 * deterministic Two-Way takes over; results must still match the model. */
static int budget_fallback(void)
{
    static uint8_t h[200000],nd[80];
    for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)(i%5==4?'b':'a');
    for (size_t m=10;m<=70;m+=3) {
        memset(nd,'a',m); nd[m]='b'; size_t n=m+1;
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
        memset(h+100000,'a',m); h[100000+m]='b';
        CHECK(check_bytes(h,sizeof h,nd,n)==0);
        for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)(i%5==4?'b':'a');
    }
    return 0;
}
static int cancellation(void)
{
    size_t n=3u<<20;
    uint8_t *h=malloc(n),*nd=malloc(n);
    CHECK(h&&nd); memset(h,'a',n); memset(nd,'a',n); nd[n-1]='b';
    atomic_bool flag; atomic_init(&flag,true);
    find_control c={0}; c.cancel=&flag;
    find_source s={h,n,NULL}; find_result r;
    CHECK(find_literal(&s,nd,n, (find_literal_options){0},&c,&r)==FIND_CANCELLED && r.total==0 && r.stored==0);
    CHECK(find_test_literal_two_way(&s,nd,n, (find_literal_options){0},&c,&r)==FIND_CANCELLED);
    find_match m; CHECK(find_literal_next(&s,nd,n, (find_literal_options){0},0,&c,&m)==FIND_CANCELLED && !m.matched);
    atomic_store(&flag,false);
    CHECK(find_literal(&s,nd,n, (find_literal_options){0},&c,&r)==FIND_OK && r.total==0);
    free(h); free(nd); return 0;
}
static int vector_boundaries(void)
{
    uint8_t h[256];
    const uint8_t nd[3]={0,0xff,0x80};
    const size_t positions[]={0,15,16,31,32,47,48,63,64,95,96,127,128,159};
    for (size_t hn=3;hn<=sizeof h;hn++) {
        for (size_t k=0;k<sizeof positions/sizeof positions[0];k++) {
            size_t pos=positions[k];
            if (pos+sizeof nd>hn) continue;
            memset(h,0x55,hn); memcpy(h+pos,nd,sizeof nd);
            CHECK(check_bytes(h,hn,nd,sizeof nd)==0);
            find_source s={h,hn,NULL}; find_match hit;
            CHECK(find_literal_next(&s,nd,sizeof nd, (find_literal_options){0},pos,NULL,&hit)==FIND_OK);
            CHECK(hit.matched && hit.whole.start==pos);
            CHECK(find_literal_next(&s,nd,sizeof nd, (find_literal_options){0},pos+1,NULL,&hit)==FIND_OK);
            CHECK(!hit.matched);
        }
    }
    return 0;
}
static int folded_vectors(void)
{
    /* (P) Cover SSE and AVX block boundaries and ASCII punctuation carrying
     * the same bit as the case pair. The neighbouring '_' and '9' reject
     * otherwise valid matches on different sides, including across pieces. */
    uint8_t text[4*64];
    const uint8_t needle[]={'@','a','[',0xff};
    memset(text,' ',sizeof text);
    for (size_t at=15;at+sizeof needle<sizeof text;at+=32) {
        memcpy(text+at,needle,sizeof needle);
        text[at+1]='A';
    }
    text[14]='_';
    text[47+sizeof needle]='9';
    text[79]='`';
    text[111+2]='{';
    const size_t cuts[]={15,16,17,47,49,51,145};
    find_fixture f=find_fixture_snapshot(text,sizeof text,cuts,sizeof cuts/sizeof cuts[0]);
    find_source flat={text,sizeof text,NULL}, view=find_fixture_source(f);
    for (unsigned sse=0;sse<=1;sse++) {
        force_sse=sse!=0;
        for (unsigned word_only=0;word_only<=1;word_only++) {
            find_literal_options options={true,word_only!=0};
            CHECK(check_mode(&flat,text,sizeof text,needle,sizeof needle,options)==0);
            CHECK(check_mode(&view,text,sizeof text,needle,sizeof needle,options)==0);
        }
    }
    force_sse=false;
    find_fixture_release(f);
    return 0;
}

int main(void)
{
    CHECK(vector_boundaries()==0);
    CHECK(folded_vectors()==0);
    CHECK(exhaustive_binary()==0);
    CHECK(random_bytes()==0);
    CHECK(adversarial()==0);
    CHECK(budget_fallback()==0);
    CHECK(long_needles()==0);
    CHECK(snapshots()==0);
    CHECK(cancellation()==0);
    puts("find_simd_filter_verify_test: sensitive and folded SIMD agree with Two-Way");
    return 0;
}
