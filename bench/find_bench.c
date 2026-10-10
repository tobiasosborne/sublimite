/* P1.10a frozen matrix. Default full fixtures, 31 samples; --quick is TRACK.
 * Every timed sample creates a mapping after request and publishes via work. */
#include "find/find.h"
#include "base/base.h"
#include "trace/trace.h"
#include "harness.h"
#include "find_supervise.h"
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
#define WAIT_NS UINT64_C(30000000000)
#define CPU_GATE_NS UINT64_C(5000000)

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
    atomic_bool begun,done,polling;
    uint64_t published_ns,returned_ns;
    bool published;
    uint64_t cpu_previous, cpu_max, cpu_polls, cancel_cpu;
    bool meter_cpu;
} job;
static uint64_t cpu_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts)) { perror("thread clock"); _exit(2); }
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}
static void cpu_record(job *j,uint64_t now,bool cancelled)
{
    uint64_t interval=now-j->cpu_previous;
    if (interval>j->cpu_max) j->cpu_max=interval;
    j->cpu_previous=now; j->cpu_polls++;
    if (cancelled) j->cancel_cpu=interval;
}
static bool find_bench_should_stop(const work_ctx *ctx)
{
    bool stopped=work_should_stop(ctx);
    job *j=ctx->arg;
    cpu_record(j,cpu_now(),stopped);
    /* Do not let a cancel arriving before the entry poll masquerade as a
     * measurement of the production loop's consecutive CPU slices. */
    if (!stopped && j->cpu_polls>=2)
        atomic_store_explicit(&j->polling,true,memory_order_release);
    return stopped;
}
#include "find_probe.h"
static void pause_briefly(void) { struct timespec t={0,100000}; (void)nanosleep(&t,NULL); }
/* Timeouts exit the isolated process without freeing/joining live arguments. */
static void wait_flag(const atomic_bool *flag,uint64_t deadline,const char *label)
{
    while (!atomic_load_explicit(flag,memory_order_acquire)) {
        if (bench_now_ns()>=deadline) { fprintf(stderr,"find fixture FAIL: %s timeout\n",label); fflush(NULL); _exit(2); }
        pause_briefly();
    }
}
static void retire(work_pool *pool,work_handle h,uint64_t deadline)
{
    while (atomic_load_explicit(&pool->slots[h.slot].busy,memory_order_acquire)) {
        if (bench_now_ns()>=deadline) { fputs("find fixture FAIL: physical retirement timeout\n",stderr); fflush(NULL); _exit(2); }
        pause_briefly();
    }
}
static int cancel_miss(const bench_samples *ack,uint64_t cpu_max)
{
    return !ack->n || ack->dropped || bench_p50(ack)>UINT64_C(1000000) ||
        bench_p99(ack)>CPU_GATE_NS || cpu_max>CPU_GATE_NS;
}
static int self_check_cancel(void)
{
    uint64_t value=UINT64_C(20000000); bench_samples ack;
    bench_samples_init(&ack,&value,1); (void)bench_add(&ack,value);
    bool delayed=cancel_miss(&ack,0)!=0;
    value=100;
    job j={.cpu_previous=100};
    cpu_record(&j,UINT64_C(6000100),true);
    bool cpu=cancel_miss(&ack,j.cpu_max)!=0 && j.cancel_cpu==UINT64_C(6000000);
    value=UINT64_C(1000000);
    bool boundary=!cancel_miss(&ack,CPU_GATE_NS);
    value++;
    bool median=cancel_miss(&ack,0)!=0;
    value=UINT64_C(5000001);
    bool tail=cancel_miss(&ack,0)!=0;
    bool pass=delayed && cpu && boundary && median && tail;
    printf("P1R10 self-check: %s delayed_ack_rejected=%d cancellation_CPU_interval_rejected=%d boundary=%d median=%d tail=%d\n",
           pass?"PASS":"FAIL",delayed,cpu,boundary,median,tail);
    return pass?0:1;
}
static int missing_event(void *arg)
{
    atomic_bool flag; atomic_init(&flag,false);
    wait_flag(&flag,bench_now_ns()+UINT64_C(10000000),arg);
    return 0;
}
static int stuck_shutdown(void *arg)
{ (void)arg; for (;;) pause(); return 0; }
static int self_check_deadlines(void)
{
    int start=find_fixture_supervise(missing_event,"missing start",UINT64_C(1000000000),"missing start");
    int done=find_fixture_supervise(missing_event,"missing completion",UINT64_C(1000000000),"missing completion");
    int cleanup=find_fixture_supervise(stuck_shutdown,NULL,UINT64_C(10000000),"stuck shutdown");
    bool pass=start==2 && done==2 && cleanup==2;
    printf("P1R11 self-check: %s missing_start=%d missing_completion=%d shutdown=%d\n",pass?"PASS":"FAIL",start,done,cleanup);
    return pass?0:1;
}
static void drain(const work_msg *message,void *ud) { (void)message; (*(size_t *)ud)++; }
static void run_job(work_ctx *ctx)
{
    job *j=ctx->arg;
    void *mapping=mmap(NULL,j->n,PROT_READ,MAP_PRIVATE,j->fd,0);
    if(mapping==MAP_FAILED) { j->code=FIND_ERR_MEMORY; atomic_store(&j->done,true); return; }
    find_source s={mapping,j->n,NULL}; find_control c={0}; c.work=ctx;
    if (j->meter_cpu) j->cpu_previous=cpu_now();
    atomic_store_explicit(&j->begun,true,memory_order_release);
    j->code=j->meter_cpu ? find_bench_literal(&s,j->row->needle,j->row->nn,&c,&j->result)
                     : j->regex ? find_regex_search(&s,j->regex,j->scratch,j->sn,&c,&j->result)
                     : find_literal(&s,j->row->needle,j->row->nn,&c,&j->result);
    j->returned_ns=bench_now_ns();
    work_msg msg={0}; msg.kind=1; msg.generation=ctx->generation;
    /* UI owns j, kept live through worker completion/drain. Mailbox is bounded. */
    uintptr_t handle=(uintptr_t)j; memcpy(msg.data,&handle,sizeof handle);
    j->published=work_publish(ctx,&msg); j->published_ns=bench_now_ns();
    (void)munmap(mapping,j->n);
    atomic_store_explicit(&j->done,true,memory_order_release);
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
    bool require_a=strcmp(r->name,"G6_dense_a")==0 || strncmp(r->name,"G6v_",4)==0;
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
        atomic_init(&j.begun,false); atomic_init(&j.done,false); atomic_init(&j.polling,false);
        uint64_t start=bench_now_ns();
        work_handle h=work_submit(pool,(work_job){run_job,&j,1,WORK_BULK});
        if(!h.epoch) { correct=false; break; }
        wait_flag(&j.done,start+WAIT_NS,"search completion");
        retire(pool,h,start+WAIT_NS);
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
    uint64_t logical[SAMPLE_CAP],observed[SAMPLE_CAP],cpu_values[SAMPLE_CAP]; bench_samples ack,stop,cpu;
    bench_samples_init(&ack,logical,SAMPLE_CAP); bench_samples_init(&stop,observed,SAMPLE_CAP);
    bench_samples_init(&cpu,cpu_values,SAMPLE_CAP); uint64_t cpu_max=0;
    bool correct=true;
    for(size_t i=0;i<samples;i++) {
        job j={0}; j.fd=fd; j.n=GIB; j.row=r; j.meter_cpu=true;
        atomic_init(&j.begun,false); atomic_init(&j.done,false); atomic_init(&j.polling,false);
        work_handle h=work_submit(pool,(work_job){run_job,&j,2,WORK_BULK}); if(!h.epoch) { correct=false; break; }
        uint64_t deadline=bench_now_ns()+WAIT_NS;
        wait_flag(&j.begun,deadline,"worker start");
        wait_flag(&j.polling,deadline,"production scan polling");
        uint64_t t=bench_now_ns(); work_cancel(pool,h); (void)bench_add(&ack,bench_now_ns()-t);
        wait_flag(&j.done,deadline,"cancellation completion");
        retire(pool,h,deadline);
        size_t received=0; (void)work_mailbox_drain(pool,drain,&received);
        bool ok=j.code==FIND_CANCELLED && j.result.total==0 && j.result.stored==0 && !j.published && received==0 && j.returned_ns>=t && j.cpu_polls>0;
        (void)bench_add(&stop,ok?j.returned_ns-t:UINT64_MAX); if(!ok) correct=false;
        (void)bench_add(&cpu,ok?j.cpu_max:UINT64_MAX);
        if (j.cpu_max>cpu_max) cpu_max=j.cpu_max;
    }
    int miss=bench_report("G6c_logical",&ack,UINT64_C(1000000),CPU_GATE_NS);
    miss|=bench_report("G6c_production_CPU_interval",&cpu,CPU_GATE_NS,CPU_GATE_NS);
    miss|=cancel_miss(&ack,cpu_max);
    (void)bench_report("G6c_worker_return_TRACK",&stop,0,0);
    printf("FIND cancel %s correctness=%s max_CPU_interval=%llu target=(G)1/5ms logical; every_CPU_interval<=5ms; worker return is wall time (M)%s\n",correct && !miss?"PASS":"MISS",correct?"PASS":"MISS",(unsigned long long)cpu_max,bench_evidence_tag());
    (void)close(fd); return correct && !miss?0:1;
}
typedef struct arguments { int argc; char **argv; } arguments;
static int run_bench(void *arg)
{
    arguments *args=arg; int argc=args->argc; char **argv=args->argv;
    bool quick=false; size_t samples=31;
    const char *only=NULL;
    const char *all_a="/tmp/edit-corpus/all_a_1g.txt";
    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i],"--quick")==0) { quick=true; samples=3; }
        else if(strcmp(argv[i],"--samples")==0 && i+1<argc) {
            char *end=NULL; unsigned long value=strtoul(argv[++i],&end,10);
            if(!end || *end || value==0 || value>SAMPLE_CAP) return 2;
            samples=(size_t)value;
        } else if(strcmp(argv[i],"--row")==0 && i+1<argc) only=argv[++i];
        else if(strcmp(argv[i],"--fixture")==0 && i+1<argc) all_a=argv[++i];
        else { fprintf(stderr,"usage: find_bench [--quick] [--samples 1..1000] [--row NAME] [--fixture ALL_A]\n"); return 2; }
    }
    char power[32]; printf("POWER status=%s (M)%s indicative: concurrent workers; default full gates, quick scaled TRACK\n",bench_battery_status(power,sizeof power),bench_evidence_tag());
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
        {"G6_dense_a",all_a,(const uint8_t *)"a",1,GIB,NULL,80000000,125000000},
        {"G6v_a31b",all_a,near,sizeof near,GIB,NULL,160000000,250000000},
        {"G6v_first_last_middle",all_a,middle,sizeof middle,GIB,NULL,160000000,250000000},
        {"G6v_first_last_early",all_a,early,sizeof early,GIB,NULL,160000000,250000000},
        {"periodic_dense","/tmp/edit-corpus/periodic.txt",periodic,sizeof periodic,PERIODIC_BYTES,NULL,0,0},
        {"regex_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR ",6,GIB,"ERROR +",0,0},
        {"regex_no_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR",5,GIB,"[EW]RROR",0,0}
    };
    trace_init(); work_pool pool; if(work_pool_init(&pool,1,0)!=0) return 2;
    int miss=0;
    bool found=false;
    for(size_t i=0;i<sizeof rows/sizeof rows[0];i++) if(!only || strcmp(only,rows[i].name)==0) {
        found=true; miss|=measure_row(&pool,&rows[i],samples,quick);
    }
    if(!only || strcmp(only,"G6c")==0) { found=true; miss|=measure_cancel(&pool,&rows[3],samples); }
    if(!found) miss=1;
    work_pool_shutdown(&pool);
    printf("find_bench: %s%s\n",miss?"MISS":"PASS",quick?" (quick TRACK only; full gates not tested)":"");
    return miss?1:0;
}
int main(int argc,char **argv)
{
    if (argc==2 && strcmp(argv[1],"--self-check-cancel")==0) return self_check_cancel();
    if (argc==2 && strcmp(argv[1],"--self-check-deadlines")==0) return self_check_deadlines();
    arguments args={argc,argv};
    return find_fixture_supervise(run_bench,&args,UINT64_C(600000000000),"benchmark through shutdown");
}
