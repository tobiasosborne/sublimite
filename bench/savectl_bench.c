#include "savectl/savectl.h"
#include "trace/trace.h"
#include "harness.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#define SMALL_SAMPLES BENCH_INTERACTION_MIN_N
#define LARGE_SAMPLES BENCH_INTERACTION_MIN_N
#define BATCH_WORKERS 4u
#define BATCH_SAMPLES (BENCH_INTERACTION_MIN_N / BATCH_WORKERS)
_Static_assert(BENCH_INTERACTION_MIN_N % BATCH_WORKERS == 0, "whole populations");
typedef struct save_batch {
    size_t bytes; int code;
    uint64_t ack[BATCH_SAMPLES], durable[BATCH_SAMPLES], transaction[BATCH_SAMPLES];
} save_batch;
_Static_assert(SMALL_SAMPLES >= BENCH_INTERACTION_MIN_N, "G8 small needs qualified samples");
_Static_assert(LARGE_SAMPLES >= BENCH_INTERACTION_MIN_N, "G8 large needs qualified samples");

static void route(const work_msg *msg, void *ctx) { (void)savectl_receive(ctx,msg); }
static void settle(savectl *s, work_pool *pool)
{
    for (;;) {
        (void)work_mailbox_drain(pool,route,s); savectl_tick(s);
        if (!savectl_get_model(s).busy) return;
        struct pollfd pfd={.fd=work_pool_eventfd(pool),.events=POLLIN};
        (void)ppoll(&pfd,1,&(struct timespec){0,100000},NULL);
    }
}
static void put64(uint8_t *p, uint64_t n) { for (unsigned i=0;i<8u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static journal_record base_record(uint8_t *p, const journal_base *b)
{
    put64(p,b->size); put64(p+8,b->mtime_ns); put64(p+16,b->inode); put64(p+24,b->device);
    put32(p+32,b->prefix_crc); put32(p+36,b->prefix_len);
    size_t n=strlen(b->path); memcpy(p+40,b->path,n+1);
    return (journal_record){JOURNAL_BASE,1,0,p,n+41};
}
static double load_stamp(char *power, size_t n)
{
    bench_battery_status(power,n);
    double load=0; FILE *fp=fopen("/proc/loadavg","r");
    if (fp) { if (fscanf(fp,"%lf",&load)!=1) load=-1; fclose(fp); }
    return load;
}
/* Default is a gated verdict (MISS -> exit 1, REFUSED for too few samples -> exit 3);
 * track=true is the explicit descriptive mode (review P4-modules-2 s31/s32). */
static int report(const char *name, bench_samples *samples, uint64_t g50, uint64_t g99,
                   const char *power, double load, bool track, size_t required)
{
    char load_text[32]; (void)snprintf(load_text,sizeof load_text,"%.2f",load);
    return bench_gate_report(name,samples,g50,g99,required,track,power,load_text);
}
static int run_size(save_batch *batch)
{
    size_t bytes=batch->bytes, iterations=BATCH_SAMPLES;
    int rc=1;
    char directory[]="/tmp/edit-savectl-bench-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    char target[128],logpath[128];
    (void)snprintf(target,sizeof target,"%s/target",directory);
    (void)snprintf(logpath,sizeof logpath,"%s/journal",directory);
    /* Anonymous immutable original: preserves a genuinely warm snapshot across
     * replacement, without relying on the missing file mapping guard accessor.
     * Corpus is only read; these are explicitly smaller regression fixtures. */
    uint8_t *source=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (source==MAP_FAILED) goto directory_end;
    int corpus=open("/tmp/edit-corpus/log_1g.txt",O_RDONLY|O_CLOEXEC);
    if (corpus<0) goto source_end;
    size_t off=0;
    while (off<bytes) {
        size_t n=bytes-off; if (n>FILE_PREFIX_MAX) n=FILE_PREFIX_MAX;
        ssize_t got=pread(corpus,source+off,n,(off_t)off);
        if (got<=0) { close(corpus); goto source_end; }
        off+=(size_t)got;
    }
    close(corpus);
    int fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) goto source_end;
    off=0;
    while (off<bytes) {
        size_t n=bytes-off; if (n>FILE_PREFIX_MAX) n=FILE_PREFIX_MAX;
        ssize_t got=write(fd,source+off,n);
        if (got<=0) { close(fd); goto file_end; }
        off+=(size_t)got;
    }
    if (fsync(fd)) { close(fd); goto file_end; }
    close(fd);
    if (mprotect(source,bytes,PROT_READ)) goto file_end;
    work_pool *pool=aligned_alloc(_Alignof(work_pool),sizeof *pool),*jp=aligned_alloc(_Alignof(work_pool),sizeof *jp);
    if (!pool || !jp) { free(pool); free(jp); goto file_end; }
    if ((uintptr_t)pool % _Alignof(work_pool) || (uintptr_t)jp % _Alignof(work_pool)) {
        fputs("savectl_bench: unaligned work pool\n",stderr); goto pool_free;
    }
    if (work_pool_init(pool,1,0)) goto pool_free;
    if (work_pool_init(jp,1,0)) goto pool_end;
    journal *j=NULL;
    if (journal_open(&j,logpath,jp,NULL)) goto jp_end;
    journal_base previous;
    if (journal_capture_base(target,&previous)) goto journal_end;
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator);
    if (!tree || piece_init_mapped(tree,source,bytes,NULL)) { if (tree) piece_destroy(tree); goto journal_end; }
    struct stat st; if (stat(target,&st)) goto tree_end;
    savectl_options options={.pool=pool,.path=target,.source_mode=FILE_MODE_COPY,
        .baseline={.dev=previous.device,.ino=previous.inode,.size=previous.size,.mtime_ns=previous.mtime_ns,
            .mode=(uint32_t)st.st_mode & 07777u,.exists=1},.journal=j,.journal_pool=jp,.buffer_id=1};
    savectl *s=NULL; if (savectl_create(&s,&options,false)) goto tree_end;
    uint64_t ack_data[SMALL_SAMPLES],durable_data[SMALL_SAMPLES],transaction_data[SMALL_SAMPLES];
    bench_samples ack,durable,transaction;
    bench_samples_init(&ack,ack_data,SMALL_SAMPLES); bench_samples_init(&durable,durable_data,SMALL_SAMPLES);
    bench_samples_init(&transaction,transaction_data,SMALL_SAMPLES);
    char power[32]; (void)load_stamp(power,sizeof power);
    for (size_t i=0;i<iterations;++i) {
        if (i%1000u==0) printf("PROGRESS bytes=%zu samples=%zu (M)%s\n",bytes,i,bench__tag_from_power(power));
        uint8_t payload[4137]; journal_record checkpoint=base_record(payload,&previous);
        savectl_modified(s);
        uint64_t start=bench_now_ns();
        if (savectl_save(s,tree,&previous,&checkpoint,1)) goto controller_end;
        (void)bench_add(&ack,bench_now_ns()-start);
        settle(s,pool);
        savectl_model model=savectl_get_model(s);
        if (!model.needs_finish || model.file_error || model.journal_error) {
            fprintf(stderr,"savectl_bench save failure: file=%d journal=%d\n",model.file_error,model.journal_error);
            goto controller_end;
        }
        (void)bench_add(&durable,bench_now_ns()-start);
        previous=*savectl_saved_base(s); previous.path=previous.captured_path;
        checkpoint=base_record(payload,&previous);
        if (savectl_finish(s,&checkpoint,1)) goto controller_end;
        settle(s,pool); model=savectl_get_model(s);
        if (model.state!=SAVECTL_SAVED || model.modified) goto controller_end;
        (void)bench_add(&transaction,bench_now_ns()-start);
    }
    if (ack.n!=BATCH_SAMPLES || durable.n!=BATCH_SAMPLES || transaction.n!=BATCH_SAMPLES ||
        ack.dropped || durable.dropped || transaction.dropped) goto controller_end;
    memcpy(batch->ack,ack.v,sizeof batch->ack);
    memcpy(batch->durable,durable.v,sizeof batch->durable);
    memcpy(batch->transaction,transaction.v,sizeof batch->transaction);
    rc=0;
controller_end:
    settle(s,pool);
    const journal_save *token=savectl_save_token(s);
    /* On error preserve retained generations for diagnosis; success has none. */
    if (token && token->previous_path[0]) fprintf(stderr,"retained recovery: %s\n",token->previous_path);
    while (savectl_destroy(s)==SAVECTL_BUSY) { struct timespec t={0,100000}; nanosleep(&t,NULL); }
tree_end: piece_destroy(tree);
journal_end: journal_close(j);
jp_end: work_pool_shutdown(jp);
pool_end: work_pool_shutdown(pool);
pool_free: free(jp); free(pool);
file_end: (void)unlink(logpath); (void)unlink(target);
source_end: (void)munmap(source,bytes);
directory_end: (void)rmdir(directory);
    return rc;
}
static void *run_batch(void *ctx)
{
    save_batch *batch=ctx;
    batch->code=run_size(batch);
    return NULL;
}
static int run_population(size_t bytes, bool track)
{
    uint64_t wall_start=bench_now_ns();
    save_batch *batches=calloc(BATCH_WORKERS,sizeof *batches);
    if (!batches) return 1;
    pthread_t threads[BATCH_WORKERS]; size_t started=0;
    char power[32]; double load=load_stamp(power,sizeof power);
    for (;started<BATCH_WORKERS;started++) {
        batches[started].bytes=bytes;
        if (pthread_create(&threads[started],NULL,run_batch,&batches[started])) break;
    }
    int failed=started!=BATCH_WORKERS;
    for (size_t i=0;i<started;i++) {
        if (pthread_join(threads[i],NULL) || batches[i].code) failed=1;
    }
    if (failed) { free(batches); return 1; }
    uint64_t a[SMALL_SAMPLES],d[SMALL_SAMPLES],t[SMALL_SAMPLES]; bench_samples ack,durable,transaction;
    bench_samples_init(&ack,a,SMALL_SAMPLES); bench_samples_init(&durable,d,SMALL_SAMPLES);
    bench_samples_init(&transaction,t,SMALL_SAMPLES);
    for (size_t i=0;i<BATCH_WORKERS;i++) for (size_t k=0;k<BATCH_SAMPLES;k++) {
        if (bench_add(&ack,batches[i].ack[k]) || bench_add(&durable,batches[i].durable[k]) ||
            bench_add(&transaction,batches[i].transaction[k])) { free(batches); return 1; }
    }
    free(batches);
    printf("POPULATION bytes=%zu n=%zu workers=%u wall_ns=%llu (M)%s\n",
           bytes,ack.n,BATCH_WORKERS,(unsigned long long)(bench_now_ns()-wall_start),bench__tag_from_power(power));
    int rc=report(bytes>4096u ? "G8s_ack_64KiB" : "G8s_ack_4KiB",&ack,2000000,5000000,
                  power,load,track,BENCH_INTERACTION_MIN_N);
    rc=bench_merge_exit(rc,report(bytes>4096u ? "G8d_64KiB_durable" : "G8d_4KiB_durable",&durable,
                  UINT64_C(10000000),UINT64_C(50000000),power,load,track,BENCH_INTERACTION_MIN_N));
    rc=bench_merge_exit(rc,report(bytes>4096u ? "journal_saved_64KiB" : "journal_saved_4KiB",&transaction,
                  0,0,power,load,track,BENCH_INTERACTION_MIN_N));
    return rc;
}
int main(int argc, char **argv)
{
    bool track=argc==2 && !strcmp(argv[1],"--track");
    if (argc>2 || (argc==2 && !track && strcmp(argv[1],"--gate"))) return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    trace_init();
    puts("savectl_bench: 4 independent runs of 2500 samples per 4KiB/64KiB row; real fsync + journal finish; no 1GB verdict");
    int rc=run_population(4096u,track);
    return bench_merge_exit(rc,run_population(65536u,track));
}
