/* P1.10a frozen matrix. Default full fixtures, 31 samples; --quick is TRACK.
 * Every timed sample creates a mapping after request and publishes via work. */
#include "find/find.h"
#include "base/base.h"
#include "trace/trace.h"
#include "harness.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define GIB ((size_t)1073741824)
#define PERIODIC_BYTES ((size_t)268435456)
#define QUICK_BYTES ((size_t)8388608)
#define SAMPLE_CAP 1000u

typedef struct row {
    const char *name,*file;
    const uint8_t *needle; size_t nn,full_size;
    const char *pattern;
    uint64_t g50,g99;
} row;
typedef struct job {
    int fd; size_t n;
    const row *row;
    const find_regex *regex; void *scratch; size_t sn;
    find_result result;
    find_code code;
    atomic_bool begun,done;
    uint64_t published_ns,returned_ns;
    bool published;
} job;
static void pause_briefly(void) { struct timespec t={0,100000}; (void)nanosleep(&t,NULL); }
static void drain(const work_msg *message,void *ud) { (void)message; (*(size_t *)ud)++; }
static void run_job(work_ctx *ctx)
{
    job *j=ctx->arg;
    void *mapping=mmap(NULL,j->n,PROT_READ,MAP_PRIVATE,j->fd,0);
    if(mapping==MAP_FAILED) { j->code=FIND_ERR_MEMORY; atomic_store(&j->done,true); return; }
    find_source s={mapping,j->n,NULL}; find_control c={0}; c.work=ctx;
    atomic_store_explicit(&j->begun,true,memory_order_release);
    j->code=j->regex ? find_regex_search(&s,j->regex,j->scratch,j->sn,&c,&j->result)
                     : find_literal(&s,j->row->needle,j->row->nn,&c,&j->result);
    j->returned_ns=bench_now_ns();
    work_msg msg={0}; msg.kind=1; msg.generation=ctx->generation;
    /* UI owns j, kept live through worker completion/drain. Mailbox is bounded. */
    uintptr_t handle=(uintptr_t)j; memcpy(msg.data,&handle,sizeof handle);
    j->published=work_publish(ctx,&msg); j->published_ns=bench_now_ns();
    (void)munmap(mapping,j->n);
    atomic_store_explicit(&j->done,true,memory_order_release);
}
static bool write_all(int fd,const uint8_t *p,size_t n)
{
    while(n) {
        ssize_t wrote=write(fd,p,n);
        if(wrote<0 && errno==EINTR) continue;
        if(wrote<=0) return false;
        p+=(size_t)wrote; n-=(size_t)wrote;
    }
    return true;
}
static int all_a_fixture(void)
{
    const char *path="/tmp/edit-corpus/all_a_1g.txt";
    struct stat st;
    if(stat(path,&st)==0 && st.st_size==(off_t)GIB) return 0;
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0) { perror(path); return 1; }
    uint8_t block[1024*1024]; memset(block,'a',sizeof block);
    for(size_t off=0;off<GIB;off+=sizeof block) if(!write_all(fd,block,sizeof block)) {
        perror("all_a write"); (void)close(fd); return 1;
    }
    if(close(fd)!=0) return 1;
    puts("FIXTURE created all_a_1g.txt bytes=1073741824 sequentially (untimed)"); return 0;
}
/* Independent KMP oracle while warming with read(). No mapping is retained.
 * Reset after acceptance implements the specified non-overlapping enumeration. */
static bool warm_oracle(int fd,size_t size,const uint8_t *needle,size_t nn,find_result *want,bool require_a)
{
    size_t failure[64]={0}; uint8_t block[1024*1024];
    if(nn==0 || nn>64 || lseek(fd,0,SEEK_SET)<0) return false;
    for(size_t i=1,k=0;i<nn;i++) {
        while(k && needle[i]!=needle[k]) k=failure[k-1];
        if(needle[i]==needle[k]) k++;
        failure[i]=k;
    }
    want->total=0; want->stored=0;
    size_t off=0,k=0;
    while(off<size) {
        size_t ask=size-off; if(ask>sizeof block) ask=sizeof block;
        ssize_t got=read(fd,block,ask);
        if(got<0 && errno==EINTR) continue;
        if(got<=0) return false;
        for(size_t i=0;i<(size_t)got;i++) {
            uint8_t c=block[i]; if(require_a && c!='a') return false;
            while(k && c!=needle[k]) k=failure[k-1];
            if(c==needle[k]) k++;
            if(k==nn) {
                if(want->stored<FIND_MAX_OFFSETS) want->offsets[want->stored++]=(uint64_t)(off+i+1-nn);
                want->total++; k=0;
            }
        }
        off+=(size_t)got;
    }
    return true;
}
static bool results_equal(const find_result *a,const find_result *b)
{
    return a->total==b->total && a->stored==b->stored &&
           memcmp(a->offsets,b->offsets,a->stored*sizeof a->offsets[0])==0;
}
static int measure_row(work_pool *pool,const row *r,size_t samples,bool quick)
{
    int fd=open(r->file,O_RDONLY); struct stat st;
    if(fd<0 || fstat(fd,&st)!=0 || st.st_size!=(off_t)r->full_size) {
        fprintf(stderr,"fixture missing/wrong size: %s\n",r->file); if(fd>=0) (void)close(fd); return 1;
    }
    size_t n=quick?QUICK_BYTES:r->full_size;
    find_result want;
    bool require_a=strcmp(r->file,"/tmp/edit-corpus/all_a_1g.txt")==0;
    if(!warm_oracle(fd,n,r->needle,r->nn,&want,require_a)) { fprintf(stderr,"warming/oracle failed\n"); (void)close(fd); return 1; }
    if(!quick && strcmp(r->name,"G6_newline")==0 && want.total!=8947841) { fprintf(stderr,"log newline fixture mismatch\n"); (void)close(fd); return 1; }
    if(!quick && strcmp(r->name,"periodic_dense")==0 && (want.total!=8388607 || want.offsets[0]!=6)) { fprintf(stderr,"periodic fixture mismatch\n"); (void)close(fd); return 1; }
    /* [EW]RROR adds WRROR; verify independently that it has no extra hits. */
    if(r->pattern && r->pattern[0]=='[') {
        find_result extra;
        if(!warm_oracle(fd,n,(const uint8_t *)"WRROR",5,&extra,false) || extra.total!=0) { (void)close(fd); return 1; }
    }
    edit_arena arena; if(edit_arena_init(&arena,1024*1024)!=0) { (void)close(fd); return 1; }
    find_regex *regex=NULL; void *scratch=NULL; size_t sn=0;
    if(r->pattern) {
        void *mem=edit_arena_alloc(&arena,find_regex_bytes(),_Alignof(max_align_t));
        if(find_regex_compile(mem,find_regex_bytes(),(const uint8_t *)r->pattern,strlen(r->pattern),&regex,NULL)!=FIND_OK) { edit_arena_free(&arena); (void)close(fd); return 1; }
        sn=find_regex_scratch_bytes(regex); scratch=edit_arena_alloc(&arena,sn,_Alignof(max_align_t));
        size_t pn; (void)find_regex_prefix(regex,&pn);
        if((r->pattern[0]=='[' && pn!=0) || (r->pattern[0]!='[' && pn==0)) { fprintf(stderr,"regex prefix fixture mismatch\n"); edit_arena_free(&arena); (void)close(fd); return 1; }
    }
    printf("ROW name=%s bytes=%zu expected=%llu stored=%zu first=%llu last_stored=%llu mapping=NEW cache=read-warm kind=%s\n",
        r->name,n,(unsigned long long)want.total,want.stored,
        (unsigned long long)(want.stored?want.offsets[0]:FIND_UNSET),
        (unsigned long long)(want.stored?want.offsets[want.stored-1]:FIND_UNSET),quick || !r->g50 ? "TRACK":"GATED");
    fflush(stdout);
    uint64_t values[SAMPLE_CAP]; bench_samples times; bench_samples_init(&times,values,SAMPLE_CAP);
    bool correct=true;
    for(size_t i=0;i<samples;i++) {
        job j={0}; j.fd=fd; j.n=n; j.row=r; j.regex=regex; j.scratch=scratch; j.sn=sn;
        atomic_init(&j.begun,false); atomic_init(&j.done,false);
        uint64_t start=bench_now_ns();
        work_handle h=work_submit(pool,(work_job){run_job,&j,1,WORK_BULK});
        if(!h.epoch) { correct=false; break; }
        while(!atomic_load_explicit(&j.done,memory_order_acquire)) pause_briefly();
        size_t received=0; (void)work_mailbox_drain(pool,drain,&received);
        bool sample_ok=j.code==FIND_OK && j.published && received==1 && results_equal(&j.result,&want);
        if(!sample_ok) {
            fprintf(stderr,"CORRECTNESS MISS name=%s sample=%zu code=%d count=%llu wanted=%llu\n",r->name,i,(int)j.code,(unsigned long long)j.result.total,(unsigned long long)want.total);
            correct=false;
        }
        (void)bench_add(&times,sample_ok?j.published_ns-start:UINT64_MAX);
    }
    uint64_t g50=quick?0:r->g50, g99=quick?0:r->g99;
    int miss=bench_report(r->name,&times,g50,g99);
    printf("FIND %s %s correctness=%s (M)%s; limits=(G); unit=ns\n",r->name,
           !correct || miss ? "MISS": g50?"PASS":"TRACK",correct?"PASS":"MISS",bench_evidence_tag());
    fflush(stdout); edit_arena_free(&arena); (void)close(fd);
    return !correct || (g50 && miss) ? 1:0;
}
static int measure_cancel(work_pool *pool,const row *r,size_t samples)
{
    int fd=open(r->file,O_RDONLY); if(fd<0) return 1;
    find_result want; if(!warm_oracle(fd,GIB,r->needle,r->nn,&want,true)) { (void)close(fd); return 1; }
    uint64_t logical[SAMPLE_CAP],observed[SAMPLE_CAP]; bench_samples ack,stop;
    bench_samples_init(&ack,logical,SAMPLE_CAP); bench_samples_init(&stop,observed,SAMPLE_CAP);
    bool correct=true;
    for(size_t i=0;i<samples;i++) {
        job j={0}; j.fd=fd; j.n=GIB; j.row=r;
        atomic_init(&j.begun,false); atomic_init(&j.done,false);
        work_handle h=work_submit(pool,(work_job){run_job,&j,2,WORK_BULK}); if(!h.epoch) { correct=false; break; }
        while(!atomic_load_explicit(&j.begun,memory_order_acquire) && !atomic_load(&j.done)) pause_briefly();
        uint64_t t=bench_now_ns(); work_cancel(pool,h); (void)bench_add(&ack,bench_now_ns()-t);
        while(!atomic_load_explicit(&j.done,memory_order_acquire)) pause_briefly();
        size_t received=0; (void)work_mailbox_drain(pool,drain,&received);
        bool ok=j.code==FIND_CANCELLED && j.result.total==0 && j.result.stored==0 && !j.published && received==0 && j.returned_ns>=t;
        (void)bench_add(&stop,ok?j.returned_ns-t:UINT64_MAX); if(!ok) correct=false;
    }
    (void)bench_report("G6c_logical_TRACK",&ack,0,0);
    (void)bench_report("G6c_worker_return_TRACK",&stop,0,0);
    printf("FIND cancel TRACK correctness=%s target=(G)1/5ms logical; next_CPU_slice<=5ms; worker return is wall time (M)%s\n",correct?"PASS":"MISS",bench_evidence_tag());
    (void)close(fd); return correct?0:1;
}
int main(int argc,char **argv)
{
    bool quick=false; size_t samples=31;
    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i],"--quick")==0) { quick=true; samples=3; }
        else if(strcmp(argv[i],"--samples")==0 && i+1<argc) {
            char *end=NULL; unsigned long value=strtoul(argv[++i],&end,10);
            if(!end || *end || value==0 || value>SAMPLE_CAP) return 2;
            samples=(size_t)value;
        } else { fprintf(stderr,"usage: find_bench [--quick] [--samples 1..1000]\n"); return 2; }
    }
    char power[32]; printf("POWER status=%s (M)%s indicative: concurrent workers; default full gates, quick scaled TRACK\n",bench_battery_status(power,sizeof power),bench_evidence_tag());
    if(all_a_fixture()!=0) return 2;
    uint8_t near[32],middle[33],early[33],periodic[32];
    memset(near,'a',sizeof near); near[31]='b';
    memset(middle,'a',sizeof middle); middle[16]='b';
    memset(early,'a',sizeof early); early[1]='b';
    int nf=open("/tmp/edit-corpus/needle.txt",O_RDONLY);
    if(nf<0 || read(nf,periodic,sizeof periodic)!=(ssize_t)sizeof periodic || memcmp(periodic,near,sizeof near)!=0) { if(nf>=0) (void)close(nf); fprintf(stderr,"needle fixture mismatch\n"); return 2; }
    (void)close(nf);
    const row rows[]={
        {"G6_ERROR","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR",5,GIB,NULL,80000000,125000000},
        {"G6_newline","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"\n",1,GIB,NULL,80000000,125000000},
        {"G6v_a31b","/tmp/edit-corpus/all_a_1g.txt",near,sizeof near,GIB,NULL,160000000,250000000},
        {"G6v_first_last_middle","/tmp/edit-corpus/all_a_1g.txt",middle,sizeof middle,GIB,NULL,160000000,250000000},
        {"G6v_first_last_early","/tmp/edit-corpus/all_a_1g.txt",early,sizeof early,GIB,NULL,160000000,250000000},
        {"periodic_dense","/tmp/edit-corpus/periodic.txt",periodic,sizeof periodic,PERIODIC_BYTES,NULL,0,0},
        {"regex_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR ",6,GIB,"ERROR +",0,0},
        {"regex_no_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR",5,GIB,"[EW]RROR",0,0}
    };
    trace_init(); work_pool pool; if(work_pool_init(&pool,1,0)!=0) return 2;
    int miss=0;
    for(size_t i=0;i<sizeof rows/sizeof rows[0];i++) miss|=measure_row(&pool,&rows[i],samples,quick);
    miss|=measure_cancel(&pool,&rows[2],samples);
    work_pool_shutdown(&pool);
    printf("find_bench: %s%s\n",miss?"MISS":"PASS",quick?" (quick TRACK only; full gates not tested)":"");
    return miss?1:0;
}
