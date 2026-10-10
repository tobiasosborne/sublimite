#include "find/find.h"
#include "base/base.h"
#include "trace/trace.h"
#include "../bench/find_supervise.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while (0)

static find_source bytes(const char *s) { return (find_source){(const uint8_t *)s,strlen(s),NULL}; }
static int literal_cases(void)
{
    find_result r;
    find_source s=bytes("aaaaa");
    CHECK(find_literal(&s,(const uint8_t *)"aa",2,NULL,&r)==FIND_OK);
    CHECK(r.total==2 && r.stored==2 && r.offsets[0]==0 && r.offsets[1]==2);
    CHECK(find_literal(&s,NULL,0,NULL,&r)==FIND_OK && r.total==0);
    uint8_t binary[]={0,255,0,255,0}; s=(find_source){binary,sizeof binary,NULL};
    CHECK(find_literal(&s,binary,2,NULL,&r)==FIND_OK && r.total==2 && r.offsets[1]==2);
    uint8_t dense[10000]; memset(dense,'a',sizeof dense); s=(find_source){dense,sizeof dense,NULL};
    CHECK(find_literal(&s,(const uint8_t *)"a",1,NULL,&r)==FIND_OK);
    CHECK(r.total==sizeof dense && r.stored==FIND_MAX_OFFSETS && r.offsets[4095]==4095);
    uint8_t needle[32]; memset(needle,'a',sizeof needle); needle[31]='b';
    CHECK(find_literal(&s,needle,sizeof needle,NULL,&r)==FIND_OK && r.total==0);
    needle[31]='a'; needle[16]='b';
    CHECK(find_literal(&s,needle,sizeof needle,NULL,&r)==FIND_OK && r.total==0);
    CHECK(find_literal(NULL,needle,32,NULL,&r)==FIND_ERR_ARGUMENT && r.total==0);
    s=(find_source){NULL,0,NULL};
    CHECK(find_literal(&s,needle,32,NULL,&r)==FIND_OK && r.total==0);
    return 0;
}

static int literal_edges(void)
{
    find_result r; find_match m; uint8_t data[257], needle[40];
    /* Independent enumeration, every short binary needle/haystack pair. */
    for (size_t hn=0; hn<9; hn++) for (unsigned h=0; h<(1u<<hn); h++) {
        for (size_t i=0;i<hn;i++) data[i]=(uint8_t)((h>>i)&1u);
        find_source s={data,hn,NULL};
        for(size_t nn=1;nn<5;nn++) for(unsigned v=0;v<(1u<<nn);v++) {
            for(size_t k=0;k<nn;k++) needle[k]=(uint8_t)((v>>k)&1u);
            uint64_t want[9]; size_t count=0;
            for(size_t i=0; i+nn<=hn;) {
                size_t k=0; while(k<nn && data[i+k]==needle[k]) k++;
                if(k==nn) { want[count++]=i; i+=nn; } else i++;
            }
            CHECK(find_literal(&s,needle,nn,NULL,&r)==FIND_OK);
            CHECK(r.total==count && r.stored==count);
            for(size_t i=0;i<count;i++) CHECK(r.offsets[i]==want[i]);
        }
    }
    memset(data,'a',sizeof data); memset(needle,'a',sizeof needle);
    data[256]='b'; needle[39]='b'; find_source s={data,sizeof data,NULL};
    CHECK(find_literal(&s,needle,40,NULL,&r)==FIND_OK && r.total==1 && r.offsets[0]==217);
    CHECK(find_literal_next(&s,needle,40,218,NULL,&m)==FIND_OK && !m.matched);
    CHECK(find_literal_next(&s,needle,40,0,NULL,&m)==FIND_OK && m.matched && m.whole.end==257 && m.groups==0);
    CHECK(find_literal_next(&s,NULL,0,0,NULL,&m)==FIND_OK && !m.matched && m.whole.start==FIND_UNSET);
    CHECK(find_literal_next(&s,needle,40,258,NULL,&m)==FIND_ERR_ARGUMENT && !m.matched);
    CHECK(find_literal(&s,NULL,1,NULL,&r)==FIND_ERR_ARGUMENT && r.total==0 && r.stored==0);
    CHECK(find_literal(&s,needle,40,NULL,NULL)==FIND_ERR_ARGUMENT);
    s=(find_source){NULL,1,NULL}; CHECK(find_literal(&s,needle,40,NULL,&r)==FIND_ERR_ARGUMENT);
    /* Read boundaries, including a needle ending at the last mapped byte. */
    long page=sysconf(_SC_PAGESIZE); CHECK(page>0);
    size_t pn=(size_t)page;
    uint8_t *map=mmap(NULL,3*pn,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); CHECK(map!=MAP_FAILED);
    CHECK(mprotect(map+pn,pn,PROT_READ|PROT_WRITE)==0);
    uint8_t *tail=map+2*pn-33; memset(tail,'a',33); tail[32]='b';
    s=(find_source){tail,33,NULL};
    CHECK(find_literal(&s,(const uint8_t *)"ab",2,NULL,&r)==FIND_OK && r.total==1 && r.offsets[0]==31);
    s=(find_source){map+pn,1,NULL}; /* leading guard */
    CHECK(find_literal(&s,(const uint8_t *)"z",1,NULL,&r)==FIND_OK && r.total==0);
    CHECK(munmap(map,3*pn)==0); return 0;
}

static int snapshot_cases(void)
{
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t);
    CHECK(piece_init_copy(t,(const uint8_t *)"acac",4)==PIECE_OK);
    CHECK(piece_insert(t,1,(const uint8_t *)"b",1)==PIECE_OK);
    CHECK(piece_insert(t,4,(const uint8_t *)"b",1)==PIECE_OK);
    piece_snapshot *snap=piece_snapshot_take(t); CHECK(snap);
    piece_iter it; const uint8_t *p; size_t n,spans=0;
    piece_iter_begin_snapshot(&it,snap,0); while(piece_iter_next(&it,&p,&n)) { CHECK(n>0); spans++; }
    CHECK(spans>1); /* fixture must really exercise a boundary */
    CHECK(piece_insert(t,0,(const uint8_t *)"x",1)==PIECE_OK); piece_destroy(t);
    find_source s={NULL,0,snap}; find_result r;
    edit_malloc_guard_begin();
    CHECK(find_literal(&s,(const uint8_t *)"abc",3,NULL,&r)==FIND_OK);
    CHECK(r.total==2 && r.offsets[0]==0 && r.offsets[1]==3);
    find_match literal;
    CHECK(find_literal_next(&s,(const uint8_t *)"abc",3,1,NULL,&literal)==FIND_OK && literal.matched && literal.whole.start==3 && literal.whole.end==6);
    CHECK(edit_malloc_guard_end()==0);
    edit_arena arena; CHECK(edit_arena_init(&arena,1024*1024)==0);
    void *mem=edit_arena_alloc(&arena,find_regex_bytes(),_Alignof(max_align_t)); find_regex *re=NULL;
    CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)"(a)(bc)",7,&re,NULL)==FIND_OK);
    void *scratch=edit_arena_alloc(&arena,find_regex_scratch_bytes(re),_Alignof(max_align_t));
    edit_malloc_guard_begin();
    CHECK(find_regex_search(&s,re,scratch,find_regex_scratch_bytes(re),NULL,&r)==FIND_OK && r.total==2);
    find_match m; CHECK(find_regex_captures(&s,re,3,scratch,find_regex_scratch_bytes(re),NULL,&m)==FIND_OK);
    CHECK(m.matched && m.whole.end==6 && m.captures[1].start==4 && m.captures[1].end==6);
    CHECK(find_regex_next(&s,re,1,scratch,find_regex_scratch_bytes(re),NULL,&m)==FIND_OK && m.matched && m.whole.start==3 && m.whole.end==6);
    CHECK(edit_malloc_guard_end()==0);
    piece_snapshot_release(snap); edit_arena_free(&arena); return 0;
}

static int regex_cases(void)
{
    edit_arena a; CHECK(edit_arena_init(&a,2*1024*1024)==0);
    struct test { const char *pat,*text; uint64_t count,first,end; } cases[]={
        {"[a-c]+","xxabccz",1,2,6}, {"[^a-c]+","abcdx",1,3,5},
        {"ab|abc","zabcab",2,1,4}, {"^a+$","aa\nba\na\n",2,0,2},
        {"(ab)+(c?)","ababczab",2,0,5}, {"a?","ba",3,0,0},
        {"a*","aaa",2,0,3}, {"","xy",3,0,0}, {"$","a\n",2,1,1},
        {".","\nxy",2,1,2}, {"a\\+","xa+",1,1,3},
        {"(a|)b","ab b",2,0,2}, {"(a*)*b","aaab",1,0,4},
        {"[\\]a]+","]aa",1,0,3}, {"[a-c]?z","zzaz",3,0,1},
        {"[--0]+","./-0",1,0,4}
    };
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        edit_arena_reset(&a); find_regex *re=NULL; size_t pn=strlen(cases[i].pat);
        void *mem=edit_arena_alloc(&a,find_regex_bytes(),_Alignof(max_align_t));
        CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)cases[i].pat,pn,&re,NULL)==FIND_OK);
        size_t sn=find_regex_scratch_bytes(re); void *scratch=edit_arena_alloc(&a,sn,_Alignof(max_align_t));
        find_source s=bytes(cases[i].text); find_result r; find_match m;
        CHECK(find_regex_search(&s,re,scratch,sn,NULL,&r)==FIND_OK);
        if(r.total!=cases[i].count) fprintf(stderr,"pattern %s count %llu expected %llu\n",cases[i].pat,(unsigned long long)r.total,(unsigned long long)cases[i].count);
        CHECK(r.total==cases[i].count && r.offsets[0]==cases[i].first);
        CHECK(find_regex_captures(&s,re,r.offsets[0],scratch,sn,NULL,&m)==FIND_OK);
        CHECK(m.matched && m.whole.start==cases[i].first && m.whole.end==cases[i].end);
        CHECK(find_regex_search(&s,re,scratch,0,NULL,&r)==FIND_ERR_MEMORY && r.total==0);
    }
    edit_arena_reset(&a); find_regex *re; void *mem=edit_arena_alloc(&a,find_regex_bytes(),_Alignof(max_align_t));
    const char *pat="(ab)+(c?)";
    CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)pat,strlen(pat),&re,NULL)==FIND_OK);
    size_t sn=find_regex_scratch_bytes(re); void *scratch=edit_arena_alloc(&a,sn,_Alignof(max_align_t));
    find_source s=bytes("ababczab"); find_match m;
    CHECK(find_regex_captures(&s,re,0,scratch,sn,NULL,&m)==FIND_OK && m.groups==2);
    CHECK(m.captures[0].start==2 && m.captures[0].end==4 && m.captures[1].start==4 && m.captures[1].end==5);
    const char *bad[]={"[","[z-a]","[]","(","a**","^*","a)","[[:alpha:]]","a{2}","\\1"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++) {
        size_t at=SIZE_MAX; re=(find_regex *)(uintptr_t)1;
        find_code code=find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)bad[i],strlen(bad[i]),&re,&at);
        CHECK(code!=FIND_OK && re==NULL && at<=strlen(bad[i]));
    }
    pat="abc(def|deg)";
    CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)pat,strlen(pat),&re,NULL)==FIND_OK);
    size_t n; const uint8_t *prefix=find_regex_prefix(re,&n); CHECK(n>0 && n<=5 && prefix && memcmp(prefix,"abcde",n)==0);
    edit_arena_free(&a); return 0;
}

static int regex_contract(void)
{
    edit_arena a; CHECK(edit_arena_init(&a,2*1024*1024)==0);
    void *mem=edit_arena_alloc(&a,find_regex_bytes(),_Alignof(max_align_t));
    find_regex *re=NULL; size_t error=99; find_result r; find_match m;
    struct bad { const char *p; find_code code; } bad[]={
        {"[",FIND_ERR_SYNTAX},{"[]",FIND_ERR_SYNTAX},{"[z-a]",FIND_ERR_SYNTAX},
        {"a**",FIND_ERR_SYNTAX},{"$+",FIND_ERR_SYNTAX},{"a)",FIND_ERR_SYNTAX},
        {"\\",FIND_ERR_SYNTAX},{"a{2}",FIND_ERR_UNSUPPORTED},{"}",FIND_ERR_UNSUPPORTED},
        {"\\1",FIND_ERR_UNSUPPORTED},{"[[:alpha:]]",FIND_ERR_UNSUPPORTED},
        {"[[.a.]]",FIND_ERR_UNSUPPORTED},{"[[=a=]]",FIND_ERR_UNSUPPORTED}
    };
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++) {
        CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)bad[i].p,strlen(bad[i].p),&re,&error)==bad[i].code);
        CHECK(re==NULL && error<=strlen(bad[i].p));
    }
    CHECK(find_regex_compile(NULL,0,(const uint8_t *)"a",1,&re,NULL)==FIND_ERR_ARGUMENT && re==NULL);
    CHECK(find_regex_compile((uint8_t *)mem+1,find_regex_bytes(),NULL,0,&re,NULL)==FIND_ERR_ARGUMENT);
    CHECK(find_regex_compile(mem,0,NULL,0,&re,NULL)==FIND_ERR_MEMORY);
    CHECK(find_regex_compile(mem,find_regex_bytes(),NULL,1,&re,NULL)==FIND_ERR_ARGUMENT);
    uint8_t huge[FIND_MAX_PATTERN+1]; memset(huge,'x',sizeof huge);
    CHECK(find_regex_compile(mem,find_regex_bytes(),huge,sizeof huge,&re,NULL)==FIND_ERR_LIMIT);
    /* Fixed width >256 with no literal prefix needs >256 byte-class states,
     * regardless of the implementation's epsilon/tag representation. */
    uint8_t state_heavy[(FIND_MAX_STATES+1u)*4u];
    for(size_t i=0;i<sizeof state_heavy;i+=4) memcpy(state_heavy+i,"[ab]",4);
    CHECK(find_regex_compile(mem,find_regex_bytes(),state_heavy,sizeof state_heavy,&re,NULL)==FIND_ERR_LIMIT);
    const char *many="()()()()()()()()()";
    CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)many,strlen(many),&re,NULL)==FIND_ERR_LIMIT);
    edit_malloc_guard_begin();
    find_code compile_code=find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)"a",1,&re,NULL);
    size_t compile_allocations=edit_malloc_guard_end();
    CHECK(compile_code==FIND_OK && compile_allocations==0);
    CHECK(find_regex_bytes()>0 && find_regex_bytes()<=FIND_MAX_PROGRAM_BYTES && find_regex_scratch_bytes(NULL)==0 && find_regex_group_count(NULL)==0);
    CHECK(find_regex_prefix(NULL,NULL)==NULL);
    struct cap { const char *p,*text; uint64_t end,a0,b0,a1,b1; size_t groups; } caps[]={
        {"(a|aa)(a?)","aa",2,0,1,1,2,2},
        {"(a*)(a*)","aa",2,0,2,2,2,2},
        {"(a)|(b)","b",1,FIND_UNSET,FIND_UNSET,0,1,2},
        {"(ab)+","abab",4,2,4,FIND_UNSET,FIND_UNSET,1},
        {"()a","a",1,0,0,FIND_UNSET,FIND_UNSET,1},
        {"(a?)*","a",1,0,1,FIND_UNSET,FIND_UNSET,1},
        {"(a(b)?)+","aba",3,2,3,1,2,2}
    };
    for(size_t i=0;i<sizeof caps/sizeof caps[0];i++) {
        CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)caps[i].p,strlen(caps[i].p),&re,&error)==FIND_OK && error==0);
        size_t sn=find_regex_scratch_bytes(re); CHECK(sn>0 && sn<=FIND_MAX_SCRATCH_BYTES); edit_arena_mark_t mark=edit_arena_mark(&a);
        void *scratch=edit_arena_alloc(&a,sn,_Alignof(max_align_t)); CHECK(scratch);
        find_source s=bytes(caps[i].text);
        CHECK(find_regex_captures(&s,re,0,scratch,sn,NULL,&m)==FIND_OK && m.matched && m.whole.end==caps[i].end && m.groups==caps[i].groups);
        CHECK(m.captures[0].start==caps[i].a0 && m.captures[0].end==caps[i].b0);
        if(m.captures[1].start!=caps[i].a1 || m.captures[1].end!=caps[i].b1) fprintf(stderr,"capture pattern=%s actual=[%llu,%llu] expected=[%llu,%llu]\n",caps[i].p,(unsigned long long)m.captures[1].start,(unsigned long long)m.captures[1].end,(unsigned long long)caps[i].a1,(unsigned long long)caps[i].b1);
        CHECK(m.captures[1].start==caps[i].a1 && m.captures[1].end==caps[i].b1);
        CHECK(find_regex_search(&s,re,NULL,sn,NULL,&r)==FIND_ERR_ARGUMENT && r.total==0);
        CHECK(find_regex_search(&s,re,(uint8_t *)scratch+1,sn,NULL,&r)==FIND_ERR_ARGUMENT);
        CHECK(find_regex_captures(&s,re,s.length+1,scratch,sn,NULL,&m)==FIND_ERR_ARGUMENT && !m.matched && m.groups==0);
        CHECK(find_regex_captures(&s,re,0,scratch,0,NULL,&m)==FIND_ERR_MEMORY && m.whole.start==FIND_UNSET);
        edit_arena_reset_to_mark(&a,mark);
    }
    const uint8_t binary_pattern[]={0,'[',0x80,'-',0xff,']','+'};
    CHECK(find_regex_compile(mem,find_regex_bytes(),binary_pattern,sizeof binary_pattern,&re,NULL)==FIND_OK);
    /* Later compiles may need more scratch than this first program. */
    size_t sn=FIND_MAX_SCRATCH_BYTES; void *scratch=edit_arena_alloc(&a,sn,_Alignof(max_align_t)); CHECK(scratch);
    const uint8_t binary[]={0,0x80,0xff,0,0x90}; find_source s={binary,sizeof binary,NULL};
    CHECK(find_regex_search(&s,re,scratch,sn,NULL,&r)==FIND_OK && r.total==2 && r.offsets[1]==3);
    CHECK(find_regex_next(&s,re,1,scratch,sn,NULL,&m)==FIND_OK && m.matched && m.whole.start==3 && m.whole.end==5);
    CHECK(find_regex_captures(&s,re,1,scratch,sn,NULL,&m)==FIND_OK && !m.matched && m.whole.start==FIND_UNSET);
    const char *patterns[]={"","^","$","[^a]+","[-a]+","[a-]+","\\n","a|abc"};
    const char *texts[]={"","x\ny","x\ny","\n","-aa","-aa","n\n","abc"};
    const uint64_t totals[]={1,2,2,1,1,1,1,1};
    for(size_t i=0;i<sizeof patterns/sizeof patterns[0];i++) {
        CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)patterns[i],strlen(patterns[i]),&re,NULL)==FIND_OK);
        s=bytes(texts[i]); CHECK(find_regex_search(&s,re,scratch,sn,NULL,&r)==FIND_OK && r.total==totals[i]);
    }
    uint8_t owned_pattern[]="a(b)";
    CHECK(find_regex_compile(mem,find_regex_bytes(),owned_pattern,4,&re,NULL)==FIND_OK);
    memset(owned_pattern,'x',sizeof owned_pattern);
    s=bytes("ab");
    CHECK(find_regex_captures(&s,re,0,scratch,sn,NULL,&m)==FIND_OK && m.matched && m.groups==1 && m.captures[0].start==1 && m.captures[0].end==2);
    CHECK(find_regex_group_count(re)==1);
    long page=sysconf(_SC_PAGESIZE); CHECK(page>0); size_t pn=(size_t)page;
    uint8_t *map=mmap(NULL,2*pn,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); CHECK(map!=MAP_FAILED);
    CHECK(mprotect(map,pn,PROT_READ|PROT_WRITE)==0); map[pn-1]='a';
    CHECK(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)"a$",2,&re,NULL)==FIND_OK);
    s=(find_source){map+pn-1,1,NULL};
    CHECK(find_regex_search(&s,re,scratch,sn,NULL,&r)==FIND_OK && r.total==1 && r.offsets[0]==0);
    CHECK(munmap(map,2*pn)==0);
    uint8_t dense[6000]; memset(dense,'x',sizeof dense); s=(find_source){dense,sizeof dense,NULL};
    CHECK(find_regex_compile(mem,find_regex_bytes(),NULL,0,&re,NULL)==FIND_OK);
    edit_malloc_guard_begin();
    find_code code=find_regex_search(&s,re,scratch,sn,NULL,&r);
    size_t allocations=edit_malloc_guard_end();
    CHECK(code==FIND_OK && r.total==6001 && r.stored==FIND_MAX_OFFSETS && r.offsets[4095]==4095 && allocations==0);
    CHECK(find_regex_next(&s,re,6000,scratch,sn,NULL,&m)==FIND_OK && m.matched && m.whole.end==6000);
    atomic_bool stop; atomic_init(&stop,true); find_control ctl={0}; ctl.cancel=&stop;
    CHECK(find_regex_search(&s,re,scratch,sn,&ctl,&r)==FIND_CANCELLED && r.total==0);
    CHECK(find_regex_captures(&s,re,0,scratch,sn,&ctl,&m)==FIND_CANCELLED && !m.matched && m.groups==0);
    CHECK(find_regex_next(&s,re,0,scratch,sn,&ctl,&m)==FIND_CANCELLED);
    edit_malloc_guard_begin(); code=find_literal(&s,(const uint8_t *)"x",1,NULL,&r); allocations=edit_malloc_guard_end();
    CHECK(code==FIND_OK && allocations==0);
    edit_arena_free(&a); return 0;
}

typedef struct cancel_job { find_source s; atomic_bool begun,done; find_control control; find_result r; find_code code; } cancel_job;
static void cancel_scan(work_ctx *ctx)
{
    cancel_job *j=ctx->arg; uint8_t needle[32]; memset(needle,'a',32); needle[31]='b';
    j->control.work=ctx;
    atomic_store(&j->begun,true);
    j->code=find_literal(&j->s,needle,32,&j->control,&j->r);
    atomic_store_explicit(&j->done,true,memory_order_release);
}
static int cancellation(void)
{
    edit_arena a; CHECK(edit_arena_init(&a,64*1024*1024)==0);
    uint8_t *data=edit_arena_alloc(&a,a.size,1); memset(data,'a',a.size);
    trace_init(); work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    cancel_job j={0}; j.s=(find_source){data,a.size,NULL};
    atomic_init(&j.begun,false); atomic_init(&j.done,false);
    work_handle h=work_submit(&pool,(work_job){cancel_scan,&j,17,WORK_BULK}); CHECK(h.epoch!=0);
    while(!atomic_load(&j.begun)) { struct timespec wait={0,100000}; nanosleep(&wait,NULL); }
    struct timespec wait={0,1000000}; nanosleep(&wait,NULL);
    work_cancel(&pool,h);
    while(!atomic_load_explicit(&j.done,memory_order_acquire)) { struct timespec pause={0,100000}; nanosleep(&pause,NULL); }
    work_pool_shutdown(&pool);
    /* A fast implementation may finish before this thread is scheduled to
     * cancel. Either completion ordering is valid; precancel below is exact. */
    CHECK((j.code==FIND_CANCELLED || j.code==FIND_OK) && j.r.total==0 && j.r.stored==0);
    atomic_bool flag; atomic_init(&flag,true); find_control ctl={0}; ctl.cancel=&flag;
    CHECK(find_literal(&j.s,(const uint8_t *)"a",1,&ctl,&j.r)==FIND_CANCELLED);
    _Atomic uint32_t gen; atomic_init(&gen,2); ctl.cancel=NULL; ctl.generation=&gen; ctl.expected_generation=1;
    CHECK(find_literal(&j.s,NULL,0,&ctl,&j.r)==FIND_CANCELLED);
    edit_arena_free(&a); return 0;
}
static int run_find_tests(void *unused)
{
    (void)unused;
    CHECK(sizeof(find_result)<=64*1024);
    CHECK(literal_cases()==0); CHECK(literal_edges()==0); CHECK(regex_contract()==0); CHECK(snapshot_cases()==0); CHECK(regex_cases()==0); CHECK(cancellation()==0);
    puts("find_test: ok (frozen P1.10a)"); return 0;
}
int main(void)
{
    /* Keep the frozen assertions intact, but bound lost starts, worker returns
     * and shutdown as one fixture. Timeout kills the child before its storage
     * can be released; the parent never joins an unresponsive worker. */
    return find_fixture_supervise(run_find_tests, NULL, UINT64_C(30000000000),
                                  "frozen find tests through shutdown");
}
