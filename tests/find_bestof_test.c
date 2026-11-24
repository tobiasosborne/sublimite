/* (G) Fixtures, sizes and RNG from v1 tests/find_bestof_test.c. */
#include "find_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while (0)

typedef struct find_bestof_probe {
    size_t seeks, spans, polls, cancel_at;
} find_bestof_probe;

static find_bestof_probe *active_probe;

void __real_piece_iter_init(piece_iter *it,const piece_view *view,uint64_t off);
void __wrap_piece_iter_init(piece_iter *it,const piece_view *view,uint64_t off)
{
    if (active_probe) active_probe->seeks++;
    __real_piece_iter_init(it,view,off);
}

size_t __real_piece_iter_next(piece_iter *it,const uint8_t **bytes);
size_t __wrap_piece_iter_next(piece_iter *it,const uint8_t **bytes)
{
    size_t n=__real_piece_iter_next(it,bytes);
    if (active_probe && n) active_probe->spans++;
    return n;
}

bool __real_work_should_stop(const work_ctx *ctx);
bool __wrap_work_should_stop(const work_ctx *ctx)
{
    if (!ctx->arg) return __real_work_should_stop(ctx);
    find_bestof_probe *probe=ctx->arg;
    probe->polls++;
    return probe->cancel_at && probe->polls>=probe->cancel_at;
}

static find_code run_mode(const find_source *source,const uint8_t *needle,size_t n,
                          const find_control *control,find_fixture_result *result,int mode)
{
    find_literal_options options={0};
    return mode ? find_fixture_literal_two_way(source,needle,n,options,control,result)
                : find_fixture_literal(source,needle,n,options,control,result);
}

typedef struct test_context {
    uint64_t rng;
    find_fixture_result got, want;
} test_context;
static uint32_t rnd(test_context *t)
{
    t->rng^=t->rng<<13; t->rng^=t->rng>>7; t->rng^=t->rng<<17;
    return (uint32_t)(t->rng>>16);
}
static void oracle(const uint8_t *h,size_t hn,const uint8_t *n,size_t nn,find_fixture_result *r)
{
    r->total=0; r->stored=0;
    if (!nn || nn>hn) return;
    for (size_t i=0;i<=hn-nn;) {
        if (memcmp(h+i,n,nn)==0) {
            if (r->stored<FIND_FIXTURE_OFFSETS) r->offsets[r->stored++]=i;
            r->total++; i+=nn;
        } else i++;
    }
}
static int check_source(test_context *t,const find_source *s,const uint8_t *h,
                        size_t hn,const uint8_t *n,size_t nn)
{
    oracle(h,hn,n,nn,&t->want);
    for (int mode=0;mode<2;mode++) {
        CHECK(run_mode(s,n,nn,NULL,&t->got,mode)==FIND_OK);
        CHECK(t->got.total==t->want.total && t->got.stored==t->want.stored);
        CHECK(memcmp(t->got.offsets,t->want.offsets,t->want.stored*sizeof t->want.offsets[0])==0);
    }
    return 0;
}
static int run_flat(test_context *t,const uint8_t *h,size_t hn,const uint8_t *n,size_t nn)
{
    find_source s={h,hn,NULL};
    return check_source(t,&s,h,hn,n,nn);
}
static int check_next(const find_source *s,const uint8_t *h,size_t hn,
                      const uint8_t *n,size_t nn,size_t off)
{
    size_t want=hn;
    bool found=false;
    if (nn && nn<=hn && off<=hn-nn) {
        for (size_t i=off;i<=hn-nn;i++) if (memcmp(h+i,n,nn)==0) { want=i; found=true; break; }
    }
    find_match m;
    CHECK(find_literal_next(s,n,nn,(find_literal_options){0},off,NULL,&m)==FIND_OK);
    CHECK(m.matched==found && m.groups==0);
    if (found) CHECK(m.whole.start==want && m.whole.end==want+nn);
    else CHECK(m.whole.start==FIND_UNSET && m.whole.end==FIND_UNSET);
    for (size_t i=0;i<FIND_MAX_GROUPS;i++) CHECK(m.captures[i].start==FIND_UNSET && m.captures[i].end==FIND_UNSET);
    return 0;
}
static int snap_check(test_context *t,const uint8_t *h,size_t hn,const size_t *cuts,
                      size_t nc,const uint8_t *n,size_t nn,size_t min_spans)
{
    find_fixture f=find_fixture_snapshot(h,hn,cuts,nc);
    find_source source=find_fixture_source(f);
    CHECK(piece_length(source.view)==hn && find_fixture_spans(f)>=min_spans);
    CHECK(check_source(t,&source,h,hn,n,nn)==0);
    for (unsigned q=0;q<6;q++) CHECK(check_next(&source,h,hn,n,nn,hn?rnd(t)%(hn+1):0)==0);
    CHECK(check_next(&source,h,hn,n,nn,hn)==0);
    find_fixture_release(f);
    return 0;
}
static int byte_counts(test_context *t)
{
    uint8_t h[9000];
    const size_t sizes[]={0,1,7,8,9,15,16,17,255,256,257,1023,1024,1025,
                          4095,4096,4097,8192,9000};
    for (unsigned value=0;value<256;value++) {
        uint8_t n=(uint8_t)value;
        memset(h,n,sizeof h);
        for (size_t i=0;i<sizeof sizes/sizeof sizes[0];i++) CHECK(run_flat(t,h,sizes[i],&n,1)==0);
        for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)(n^(uint8_t)(i%8));
        for (size_t i=0;i<sizeof sizes/sizeof sizes[0];i++) CHECK(run_flat(t,h,sizes[i],&n,1)==0);
        for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)(n^(uint8_t)(i&1));
        CHECK(run_flat(t,h,sizeof h,&n,1)==0);
    }
    return 0;
}
static int periodic_needles(test_context *t)
{
    uint8_t n[96],h[700];
    for (unsigned iter=0;iter<60000;iter++) {
        size_t unit=1+rnd(t)%6,nn=2+rnd(t)%40,hn=rnd(t)%260; uint8_t u[6];
        for (size_t i=0;i<unit;i++) u[i]=(uint8_t)('a'+(rnd(t)&1));
        for (size_t i=0;i<nn;i++) n[i]=u[i%unit];
        if (rnd(t)%3==0) n[rnd(t)%nn]^=1;
        size_t hunit=1+rnd(t)%6;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)(rnd(t)%7==0?'a'+(rnd(t)&1):u[i%(hunit<unit?hunit:unit)]);
        if (hn>nn && rnd(t)%2) memcpy(h+rnd(t)%(hn-nn+1),n,nn);
        CHECK(run_flat(t,h,hn,n,nn)==0);
    }
    memset(h,'a',sizeof h);
    for (size_t nn=1;nn<70;nn++) { memset(n,'a',nn); CHECK(run_flat(t,h,sizeof h,n,nn)==0); }
    for (size_t nn=2;nn<70;nn++) {
        for (size_t i=0;i<nn;i++) n[i]=(uint8_t)((i&1)?'b':'a');
        for (size_t i=0;i<sizeof h;i++) h[i]=(uint8_t)((i&1)?'b':'a');
        CHECK(run_flat(t,h,sizeof h,n,nn)==0);
        h[300]='c'; CHECK(run_flat(t,h,sizeof h,n,nn)==0);
    }
    return 0;
}
static int exhaustive_small(test_context *t)
{
    uint8_t h[12],n[8];
    for (size_t hn=0;hn<=11;hn++) for (unsigned x=0;x<(1u<<hn);x++) {
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)((x>>i)&1u);
        for (size_t nn=1;nn<=7;nn++) {
            if (hn<=8 && nn<=4) continue; /* Already exhaustive in the frozen suite. */
            for (unsigned v=0;v<(1u<<nn);v++) {
                for (size_t k=0;k<nn;k++) n[k]=(uint8_t)((v>>k)&1u);
                CHECK(run_flat(t,h,hn,n,nn)==0);
            }
        }
    }
    return 0;
}
static int binary_bytes(test_context *t)
{
    uint8_t h[4096],n[5];
    for (unsigned iter=0;iter<3000;iter++) {
        size_t hn=rnd(t)%sizeof h,nn=1+rnd(t)%5;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)(rnd(t)%3?rnd(t)%4:rnd(t));
        for (size_t i=0;i<nn;i++) n[i]=(uint8_t)(rnd(t)%3?rnd(t)%4:250+rnd(t)%6);
        if (hn>nn && rnd(t)%2) memcpy(h+rnd(t)%(hn-nn+1),n,nn);
        CHECK(run_flat(t,h,hn,n,nn)==0);
    }
    return 0;
}
static int snapshot_splits(test_context *t)
{
    uint8_t h[64],n[12];
    for (unsigned iter=0;iter<300;iter++) {
        size_t hn=8+rnd(t)%50,nn=1+rnd(t)%9;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)('a'+(rnd(t)%5==0));
        for (size_t i=0;i<nn;i++) n[i]=(uint8_t)('a'+(rnd(t)%4==0));
        if ((rnd(t)&1) && nn<=hn) memcpy(h+rnd(t)%(hn-nn+1),n,nn);
        for (size_t cut=0;cut<=hn;cut++) CHECK(snap_check(t,h,hn,&cut,1,n,nn,hn?1:0)==0);
    }
    for (unsigned iter=0;iter<30;iter++) {
        size_t hn=12+rnd(t)%20,nn=2+rnd(t)%8;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)('a'+(rnd(t)%6==0));
        for (size_t i=0;i<nn;i++) n[i]=(uint8_t)('a'+(rnd(t)%5==0));
        for (size_t i=0;i+nn<=hn;i+=1+rnd(t)%7) memcpy(h+i,n,nn);
        for (size_t c1=0;c1<=hn;c1++) for (size_t c2=c1;c2<=hn;c2++) {
            const size_t cuts[]={c1,c2}; CHECK(snap_check(t,h,hn,cuts,2,n,nn,1)==0);
        }
    }
    for (unsigned iter=0;iter<400;iter++) {
        size_t hn=30+rnd(t)%30,nn=1+rnd(t)%11,cuts[64],nc=0;
        for (size_t i=0;i<hn;i++) h[i]=(uint8_t)('a'+(rnd(t)%5==0));
        for (size_t i=0;i<nn;i++) n[i]=(uint8_t)('a'+(rnd(t)%5==0));
        for (size_t at=1+rnd(t)%3;at<hn;at+=1+rnd(t)%3) cuts[nc++]=at;
        CHECK(snap_check(t,h,hn,cuts,nc,n,nn,2)==0);
    }
    /* Empty needle/source already covered flat; also exercise empty snapshots. */
    CHECK(snap_check(t,NULL,0,NULL,0,NULL,0,0)==0);
    size_t cut=1; CHECK(snap_check(t,h,3,&cut,1,NULL,0,2)==0);
    return 0;
}
static int long_needles(test_context *t)
{
    enum { N=40000 }; uint8_t h[N],n[9000];
    for (size_t i=0;i<N;i++) h[i]=(uint8_t)('a'+(rnd(t)%11==0));
    for (size_t nn=4090;nn<=9000;nn+=1100) {
        memcpy(n,h+12345,nn); memcpy(h+30000,n,nn);
        size_t cuts[40],nc=0;
        for (size_t at=700;at<N;at+=1000+rnd(t)%1500) cuts[nc++]=at;
        CHECK(run_flat(t,h,N,n,nn)==0);
        CHECK(snap_check(t,h,N,cuts,nc,n,nn,2)==0);
    }
    return 0;
}
static int linear_work(test_context *t)
{
    enum { N=32*1024*1024 }; uint8_t *h=malloc(N); CHECK(h); uint8_t n[600];
    memset(h,'a',N); memset(n,'a',31); n[31]='b';
    find_source s={h,N,NULL};
    for (int mode=0;mode<2;mode++) {
        CHECK(run_mode(&s,n,32,NULL,&t->got,mode)==FIND_OK && t->got.total==0);
    }
    for (size_t i=0;i<N;i++) h[i]=(uint8_t)((i&1)?'b':'a');
    for (size_t i=0;i<500;i++) n[i]=(uint8_t)((i&1)?'b':'a');
    n[500]='b'; n[501]='b';
    for (int mode=0;mode<2;mode++) CHECK(run_mode(&s,n,502,NULL,&t->got,mode)==FIND_OK && t->got.total==0);
    memcpy(h+N-1000,n,502);
    for (int mode=0;mode<2;mode++) CHECK(run_mode(&s,n,502,NULL,&t->got,mode)==FIND_OK && t->got.total==1 && t->got.offsets[0]==N-1000);
    memset(h,'a',N); memset(n,'a',64);
    for (int mode=0;mode<2;mode++) CHECK(run_mode(&s,n,64,NULL,&t->got,mode)==FIND_OK && t->got.total==N/64 && t->got.stored==FIND_FIXTURE_OFFSETS && t->got.offsets[FIND_FIXTURE_OFFSETS-1]==(FIND_FIXTURE_OFFSETS-1)*64u);
    free(h); return 0;
}
static int cancel_prep(test_context *t)
{
    size_t nn=1u<<20; uint8_t *n=malloc(nn),h[64]; CHECK(n);
    memset(n,'a',nn); memset(h,'a',sizeof h);
    atomic_bool flag; atomic_init(&flag,true);
    find_control c={0}; c.cancel=&flag; find_source s={h,sizeof h,NULL}; find_match m;
    CHECK(find_fixture_literal(&s,n,nn,(find_literal_options){0},&c,&t->got)==FIND_CANCELLED && t->got.total==0 && t->got.stored==0);
    CHECK(find_literal_next(&s,n,nn,(find_literal_options){0},0,&c,&m)==FIND_CANCELLED && !m.matched && m.whole.start==FIND_UNSET);
    atomic_store(&flag,false);
    CHECK(find_fixture_literal(&s,n,nn,(find_literal_options){0},&c,&t->got)==FIND_OK && t->got.total==0 && t->got.stored==0);
    free(n); return 0;
}
static int regex_prefix_path(test_context *t)
{
    _Alignas(max_align_t) uint8_t mem[FIND_MAX_PROGRAM_BYTES],scratch[FIND_MAX_SCRATCH_BYTES];
    uint8_t h[300]; find_regex *re=NULL; memset(h,'x',sizeof h);
    memcpy(h+10,"ERROR  ",7); memcpy(h+100,"ERROR",5); memcpy(h+200,"ERROR   ",8);
    CHECK(find_regex_bytes()<=sizeof mem);
    CHECK(find_fixture_compile(mem,sizeof mem,(const uint8_t *)"ERROR +",7,&re,NULL)==FIND_OK);
    CHECK(find_regex_scratch_bytes(re)<=sizeof scratch);
    const size_t cuts[]={12,102,202,204};
    find_fixture f=find_fixture_snapshot(h,sizeof h,cuts,4);
    for (unsigned kind=0;kind<2;kind++) {
        find_source s=kind?find_fixture_source(f):(find_source){h,sizeof h,NULL};
        CHECK(find_fixture_regex(&s,re,scratch,sizeof scratch,NULL,&t->got)==FIND_OK);
        CHECK(t->got.total==2 && t->got.stored==2 && t->got.offsets[0]==10 && t->got.offsets[1]==200);
        find_match m;
        CHECK(find_fixture_regex_next(&s,re,11,scratch,sizeof scratch,NULL,&m)==FIND_OK && m.matched && m.whole.start==200 && m.whole.end==208);
        CHECK(find_regex_captures(&s,re,100,scratch,sizeof scratch,NULL,&m)==FIND_OK && !m.matched);
    }
    find_fixture_release(f);
    /* A rejected prefix hit must resume at hit+1, since the next complete
     * prefix may overlap it (the losing decision's regex rewind edge). */
    CHECK(find_fixture_compile(mem,sizeof mem,(const uint8_t *)"aaaa[b]",7,&re,NULL)==FIND_OK);
    const uint8_t overlap[]="aaaaab"; const size_t small_cuts[]={1,2,3,4,5};
    f=find_fixture_snapshot(overlap,6,small_cuts,5);
    find_source s=find_fixture_source(f);
    CHECK(find_fixture_regex(&s,re,scratch,sizeof scratch,NULL,&t->got)==FIND_OK && t->got.total==1 && t->got.offsets[0]==1);
    find_fixture_release(f); return 0;
}
static int snapshot_traversal(test_context *t)
{
    enum { H=16384, N=8192, SPAN=16 }; uint8_t h[H],n[N]; size_t cuts[H/SPAN-1];
    memset(h,'a',sizeof h); memset(n,'a',sizeof n); n[N/2-1]='b'; memcpy(h+4096,n,N);
    for (size_t i=0;i<sizeof cuts/sizeof cuts[0];i++) cuts[i]=(i+1)*SPAN;
    find_fixture f=find_fixture_snapshot(h,H,cuts,sizeof cuts/sizeof cuts[0]);
    size_t spans=find_fixture_spans(f);
    CHECK(spans==H/SPAN);
    find_bestof_probe probe={0};
    find_source s=find_fixture_source(f);
    active_probe=&probe;
    CHECK(find_fixture_literal(&s,n,N,(find_literal_options){0},NULL,&t->got)==FIND_OK && t->got.total==1 && t->got.offsets[0]==4096);
    active_probe=NULL;
    /* (P) At most two forward comparison readers and a possible terminal
     * seek. Bound by one traversal per cursor, with one run of headroom. */
    CHECK(probe.seeks<=3 && probe.spans>0 && probe.spans<=3*spans);
    printf("snapshot traversal: seeks=%zu span_next=%zu\n",probe.seeks,probe.spans);
    CHECK(check_source(t,&s,h,H,n,N)==0);
    find_fixture_release(f);

    /* A periodic needle with a large cut must preserve overlap memory
     * after the first prefix failure without rewinding either cursor. */
    for (size_t i=0;i<H;i++) h[i]=(uint8_t)(i%(N/2)==N/2-1?'b':'a');
    memcpy(n,h,N); h[0]='c';
    f=find_fixture_snapshot(h,H,cuts,sizeof cuts/sizeof cuts[0]);
    s=find_fixture_source(f);
    probe=(find_bestof_probe){0};
    active_probe=&probe;
    CHECK(find_fixture_literal_two_way(&s,n,N,(find_literal_options){0},NULL,&t->got)==FIND_OK);
    active_probe=NULL;
    CHECK(t->got.total==1 && t->got.offsets[0]==N/2);
    CHECK(probe.seeks<=3 && probe.spans>0 && probe.spans<=3*spans);
    CHECK(check_source(t,&s,h,H,n,N)==0);
    find_fixture_release(f);
    return 0;
}
static int poll_boundaries(test_context *t)
{
    /* The losing doc's second-poll preprocessing cancellation, plus every
     * actual poll through matching, no-match, counting, stitching and reader
     * paths. Controls/results are local and the callback is deterministic. */
    enum { H=16384, N=8192 }; uint8_t h[H],n[N]; size_t cuts[H/16-1];
    for (size_t i=0;i<sizeof cuts/sizeof cuts[0];i++) cuts[i]=(i+1)*16;
    for (unsigned kind=0;kind<7;kind++) {
        size_t nn=kind==0?1:kind==1?502:kind==2?2048:N;
        memset(h,'a',sizeof h); memset(n,'a',sizeof n);
        if (kind==1) {
            for (size_t i=0;i<H;i++) h[i]=(uint8_t)((i&1)?'b':'a');
            for (size_t i=0;i<500;i++) n[i]=(uint8_t)((i&1)?'b':'a');
            n[500]='b'; n[501]='b';
        } else if (kind==6) {
            for (size_t i=0;i<H;i++) h[i]=(uint8_t)(i%(N/2)==N/2-1?'b':'a');
            memcpy(n,h,N); h[0]='c';
        } else if (kind!=0) {
            n[nn/2-1]='b';
            if (kind!=5) memcpy(h+4096,n,nn);
        }
        find_fixture f={0};
        if (kind>=2) {
            f=find_fixture_snapshot(h,H,kind==4?NULL:cuts,kind==4?0:sizeof cuts/sizeof cuts[0]);
        }
        find_bestof_probe probe={0}; work_ctx ctx={0}; ctx.arg=&probe;
        find_control c={0}; c.work=&ctx;
        find_source s=f.snapshot?find_fixture_source(f):(find_source){h,H,NULL};
        active_probe=&probe;
        CHECK(find_fixture_literal(&s,n,nn,(find_literal_options){0},&c,&t->got)==FIND_OK);
        size_t polls=probe.polls; CHECK(polls>2);
        for (size_t stop=1;stop<=polls;stop++) {
            probe=(find_bestof_probe){0}; probe.cancel_at=stop;
            t->got.total=99; t->got.stored=99;
            CHECK(find_fixture_literal(&s,n,nn,(find_literal_options){0},&c,&t->got)==FIND_CANCELLED && t->got.total==0 && t->got.stored==0);
            CHECK(probe.polls<=stop+1);
            if (stop==2 && nn==N) CHECK(probe.spans==0); /* Cancelled inside preprocessing. */
        }
        probe=(find_bestof_probe){0}; find_match m;
        CHECK(find_literal_next(&s,n,nn,(find_literal_options){0},0,&c,&m)==FIND_OK);
        polls=probe.polls;
        for (size_t stop=1;stop<=polls;stop++) {
            probe=(find_bestof_probe){0}; probe.cancel_at=stop; memset(&m,0x55,sizeof m);
            CHECK(find_literal_next(&s,n,nn,(find_literal_options){0},0,&c,&m)==FIND_CANCELLED && !m.matched && m.groups==0);
            CHECK(m.whole.start==FIND_UNSET && m.whole.end==FIND_UNSET);
            for (size_t g=0;g<FIND_MAX_GROUPS;g++) CHECK(m.captures[g].start==FIND_UNSET && m.captures[g].end==FIND_UNSET);
        }
        active_probe=NULL;
        if (f.snapshot) find_fixture_release(f);
    }
    return 0;
}
int main(void)
{
    test_context t={0}; t.rng=0x9e3779b97f4a7c15ull;
    CHECK(snapshot_traversal(&t)==0);
    CHECK(poll_boundaries(&t)==0);
    CHECK(byte_counts(&t)==0);
    CHECK(periodic_needles(&t)==0);
    CHECK(exhaustive_small(&t)==0);
    CHECK(binary_bytes(&t)==0);
    CHECK(snapshot_splits(&t)==0);
    CHECK(long_needles(&t)==0);
    CHECK(cancel_prep(&t)==0);
    CHECK(regex_prefix_path(&t)==0);
    CHECK(linear_work(&t)==0);
    puts("find_bestof_test: ok"); return 0;
}
