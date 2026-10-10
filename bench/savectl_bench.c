#include "savectl/savectl.h"
#include "trace/trace.h"
#include "harness.h"
#include "savectl_deadline.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#define SMALL_SAMPLES BENCH_INTERACTION_MIN_N
#define LARGE_SAMPLES BENCH_INTERACTION_MIN_N
#define BATCH_WORKERS 1u
#define BATCH_SAMPLES (BENCH_INTERACTION_MIN_N / BATCH_WORKERS)
_Static_assert(BENCH_INTERACTION_MIN_N % BATCH_WORKERS == 0, "whole populations");
typedef struct save_batch {
    size_t bytes; int code; bool drop_completion, cold; size_t iterations;
    uint64_t ack[BATCH_SAMPLES], durable[BATCH_SAMPLES], transaction[BATCH_SAMPLES];
} save_batch;
_Static_assert(SMALL_SAMPLES >= BENCH_INTERACTION_MIN_N, "G8 small needs qualified samples");
_Static_assert(LARGE_SAMPLES >= BENCH_INTERACTION_MIN_N, "G8 large needs qualified samples");

static void route(const work_msg *msg, void *ctx) { (void)savectl_receive(ctx,msg); }
static void discard_completion(const work_msg *msg, void *ctx)
{ if (msg->kind==SAVECTL_MESSAGE) *(bool *)ctx=true; }
static void settle(savectl *s, work_pool *pool, bool drop)
{
    uint64_t deadline = bench_now_ns() + UINT64_C(30000000000); bool discarded=false;
    for (;;) {
        if (drop) {
            bool before=discarded;
            (void)work_mailbox_drain(pool,discard_completion,&discarded);
            if (!before && discarded) puts("PROBE terminal completion discarded; controller still owns borrowed arguments");
            /* Do not let tick selectively receive the message before the
             * intentional drop; service it normally after losing delivery. */
            if (discarded) savectl_tick(s);
        } else { (void)work_mailbox_drain(pool,route,s); savectl_tick(s); }
        if (!savectl_get_model(s).busy) return;
        if (bench_now_ns() >= deadline) savectl_phase_expired(SIGALRM);
        struct pollfd pfd={.fd=work_pool_eventfd(pool),.events=POLLIN};
        (void)ppoll(&pfd,1,&(struct timespec){0,100000},NULL);
    }
}
static void put64(uint8_t *p, uint64_t n) { for (unsigned i=0;i<8u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static journal_record base_record(uint8_t *p, const journal_base *b)
{
    put64(p,b->size); put64(p+8,b->mtime_ns); put64(p+16,b->inode); put64(p+24,b->device);
    put32(p+32,b->prefix_crc); put32(p+36,b->prefix_len|(*b->path?JOURNAL_BASE_V2:0));
    size_t n=strlen(b->path); memcpy(p+40,b->path,n+1); if(*b->path) put64(p+41+n,b->ctime_ns);
    return (journal_record){JOURNAL_BASE,1,0,p,n+41+(*b->path?8u:0u)};
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
static bool correct_saved_file(int source_fd, const char *path, size_t bytes)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC); struct stat metadata;
    if (fd<0) return false;
    bool same=fstat(fd,&metadata)==0 && metadata.st_size==(off_t)bytes;
    uint8_t source_bytes[65536], saved_bytes[65536];
    for (size_t off=0;same && off<bytes;) {
        size_t amount=bytes-off; if (amount>sizeof source_bytes) amount=sizeof source_bytes;
        ssize_t a=pread(source_fd,source_bytes,amount,(off_t)off), b=pread(fd,saved_bytes,amount,(off_t)off);
        if (a<=0 || a!=b || memcmp(source_bytes,saved_bytes,(size_t)a)) { same=false; break; }
        off+=(size_t)a;
    }
    (void)close(fd); return same;
}
static int run_size(save_batch *batch)
{
    size_t bytes=batch->bytes, iterations=batch->iterations ? batch->iterations : BATCH_SAMPLES;
    timer_t deadline_timer;
    if (savectl_deadline_init(&deadline_timer)) return 1;
    if (savectl_deadline_arm(deadline_timer,UINT64_C(60000000000))) { (void)timer_delete(deadline_timer); return 1; }
    int rc=1;
    char directory[]="build/edit-savectl-bench-XXXXXX";
    if (!mkdtemp(directory)) { (void)timer_delete(deadline_timer); return 1; }
    char *directory_path=realpath(directory,NULL);
    if (!directory_path) { (void)rmdir(directory); (void)timer_delete(deadline_timer); return 1; }
    char target[4096],logpath[4096],sourcepath[4096];
    (void)snprintf(target,sizeof target,"%s/target",directory_path);
    (void)snprintf(logpath,sizeof logpath,"%s/journal",directory_path);
    (void)snprintf(sourcepath,sizeof sourcepath,"%s/source",directory_path);
    int corpus=open("/tmp/edit-corpus/log_1g.txt",O_RDONLY|O_CLOEXEC);
    int source_fd=open(sourcepath,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    int fd=open(target,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    uint8_t buffer[FILE_PREFIX_MAX]; size_t off=0;
    if (corpus<0 || source_fd<0 || fd<0) {
        if (corpus>=0) close(corpus);
        if (source_fd>=0) close(source_fd);
        if (fd>=0) close(fd);
        goto directory_end;
    }
    while (off<bytes) {
        size_t n=bytes-off; if (n>sizeof buffer) n=sizeof buffer;
        ssize_t got=pread(corpus,buffer,n,(off_t)off);
        if (got<=0 || write(source_fd,buffer,(size_t)got)!=got || write(fd,buffer,(size_t)got)!=got) {
            close(corpus); close(source_fd); close(fd); goto directory_end;
        }
        off+=(size_t)got;
    }
    close(corpus);
    if (fsync(fd) || fsync(source_fd)) { close(fd); close(source_fd); goto directory_end; }
    close(fd);
    uint8_t *source=mmap(NULL,bytes,PROT_READ,MAP_PRIVATE,source_fd,0);
    if (source==MAP_FAILED) { close(source_fd); goto directory_end; }
    work_pool *pool=aligned_alloc(_Alignof(work_pool),sizeof *pool),*jp=aligned_alloc(_Alignof(work_pool),sizeof *jp);
    if (!pool || !jp) { free(pool); free(jp); goto source_end; }
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
            .mode=(uint32_t)st.st_mode & 07777u,.exists=1},.journal=batch->drop_completion ? NULL : j,.journal_pool=batch->drop_completion ? NULL : jp,.buffer_id=1};
    savectl *s=NULL; if (savectl_create(&s,&options,false)) goto tree_end;
    uint64_t ack_data[SMALL_SAMPLES],durable_data[SMALL_SAMPLES],transaction_data[SMALL_SAMPLES];
    bench_samples ack,durable,transaction;
    bench_samples_init(&ack,ack_data,SMALL_SAMPLES); bench_samples_init(&durable,durable_data,SMALL_SAMPLES);
    bench_samples_init(&transaction,transaction_data,SMALL_SAMPLES);
    char power[32]; (void)load_stamp(power,sizeof power);
    for (size_t i=0;i<iterations;++i) {
        /* Repeat the full source and durable replacement. New file mappings
         * retain warm faults; cold samples must prove nonresidency first. */
        if (savectl_deadline_arm(deadline_timer, UINT64_C(30000000000))) goto controller_end;
        if (madvise(source,bytes,MADV_DONTNEED)) goto controller_end;
        if (batch->cold) {
            if (posix_fadvise(source_fd,0,(off_t)bytes,POSIX_FADV_DONTNEED)) goto controller_end;
            size_t pages=(bytes+4095u)/4096u;
            unsigned char *resident=malloc(pages); if (!resident) goto controller_end;
            int unavailable=mincore(source,bytes,resident);
            if (!unavailable) for (size_t page=0;page<pages;page++) if (resident[page]&1u) { unavailable=1; break; }
            free(resident);
            if (unavailable) { fputs("savectl_bench: UNAVAILABLE cold source eviction not verified\n",stderr); goto controller_end; }
        } else {
            off=0;
            while (off<bytes) {
                ssize_t got=pread(source_fd,buffer,sizeof buffer,(off_t)off);
                if (got<=0) goto controller_end;
                off+=(size_t)got;
            }
        }

        if (i%1000u==0) printf("PROGRESS bytes=%zu samples=%zu (M)%s\n",bytes,i,bench__tag_from_power(power));
        uint8_t payload[JOURNAL_BASE_MAX]; journal_record checkpoint=base_record(payload,&previous);
        savectl_modified(s);
        uint64_t start=bench_now_ns();
        if (savectl_save(s,tree,&previous,&checkpoint,1)) goto controller_end;
        (void)bench_add(&ack,bench_now_ns()-start);
        if (batch->drop_completion) {
            /* Bound receivers normally intercept fallback drain callbacks.
             * Remove this fixture's identity binding to lose its terminal
             * delivery deliberately, before any owner-thread pumping. */
            bool removed=false;
            for (uint32_t slot=0;slot<WORK_MAX_JOBS;slot++) {
                work_slot *entry=&pool->slots[slot];
                if (entry->receive_ud==s) {
                    work_handle handle={slot,entry->receive_epoch};
                    if (work_mailbox_bind(pool,handle,entry->receive_generation,NULL,NULL)) goto controller_end;
                    removed=true;
                }
            }
            if (!removed) goto controller_end;
        }
        settle(s,pool,batch->drop_completion);
        savectl_model model=savectl_get_model(s);
        if (!model.needs_finish || model.file_error || model.journal_error) {
            fprintf(stderr,"savectl_bench save failure: file=%d journal=%d\n",model.file_error,model.journal_error);
            goto controller_end;
        }
        (void)bench_add(&durable,bench_now_ns()-start);
        previous=*savectl_saved_base(s); previous.path=previous.captured_path;
        checkpoint=base_record(payload,&previous);
        if (savectl_finish(s,&checkpoint,1)) goto controller_end;
        settle(s,pool,batch->drop_completion); model=savectl_get_model(s);
        if (model.state!=SAVECTL_SAVED || model.modified) goto controller_end;
        (void)bench_add(&transaction,bench_now_ns()-start);
        if (!correct_saved_file(source_fd,target,bytes)) { fputs("savectl_bench: structural saved content failure\n",stderr); goto controller_end; }
    }
    if (ack.n!=iterations || durable.n!=iterations || transaction.n!=iterations ||
        ack.dropped || durable.dropped || transaction.dropped) goto controller_end;
    memcpy(batch->ack,ack.v,iterations * sizeof *ack.v);
    memcpy(batch->durable,durable.v,iterations * sizeof *durable.v);
    memcpy(batch->transaction,transaction.v,iterations * sizeof *transaction.v);
    rc=0;
controller_end:
    if (savectl_deadline_arm(deadline_timer, UINT64_C(5000000000))) savectl_phase_expired(SIGALRM);
    settle(s,pool,batch->drop_completion);
    const journal_save *token=savectl_save_token(s);
    /* On error preserve retained generations for diagnosis; success has none. */
    if (token && token->previous_path[0]) fprintf(stderr,"retained recovery: %s\n",token->previous_path);
    while (savectl_destroy(s)==SAVECTL_BUSY) { (void)work_mailbox_drain(pool,route,s); savectl_tick(s); struct timespec t={0,100000}; nanosleep(&t,NULL); }
tree_end: piece_destroy(tree);
journal_end: journal_close(j);
jp_end: work_pool_shutdown(jp);
pool_end: work_pool_shutdown(pool);
pool_free: free(jp); free(pool);
source_end: (void)munmap(source,bytes); close(source_fd);
directory_end: (void)unlink(logpath); (void)unlink(target); (void)unlink(sourcepath); (void)rmdir(directory);
    (void)timer_delete(deadline_timer); free(directory_path);
    return rc;
}
static void *run_batch(void *ctx)
{
    save_batch *batch=ctx;
    batch->code=run_size(batch);
    return NULL;
}
typedef struct population_args { size_t bytes; bool track, cold; size_t iterations; } population_args;
static int run_population_impl(size_t bytes, bool track, bool cold, size_t iterations)
{
    uint64_t wall_start=bench_now_ns();
    save_batch *batches=calloc(BATCH_WORKERS,sizeof *batches);
    if (!batches) return 1;
    pthread_t threads[BATCH_WORKERS]; size_t started=0;
    char power[32]; double load=load_stamp(power,sizeof power);
    for (;started<BATCH_WORKERS;started++) {
        batches[started].bytes=bytes; batches[started].cold=cold; batches[started].iterations=iterations;
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
    for (size_t i=0;i<BATCH_WORKERS;i++) for (size_t k=0;k<iterations;k++) {
        if (bench_add(&ack,batches[i].ack[k]) || bench_add(&durable,batches[i].durable[k]) ||
            bench_add(&transaction,batches[i].transaction[k])) { free(batches); return 1; }
    }
    free(batches);
    printf("POPULATION bytes=%zu n=%zu workers=%u wall_ns=%llu (M)%s\n",
           bytes,ack.n,BATCH_WORKERS,(unsigned long long)(bench_now_ns()-wall_start),bench__tag_from_power(power));
    bool guard=bytes<1048576u;
    const char *size_name=guard ? (bytes==4096u ? "4KiB" : "64KiB") : bytes==1048576u ? "1MiB" : cold ? "1GiB_cold_source" : "1GiB_warm_source";
    char row[96]; (void)snprintf(row,sizeof row,"savectl_snapshot_enqueue_%s_regression",size_name);
    int rc=report(row,&ack,2000000,5000000,
                  power,load,track,BENCH_INTERACTION_MIN_N);
    (void)snprintf(row,sizeof row,guard ? "savectl_%s_durable_regression" : "G8d_%s_durable",size_name);
    rc=bench_merge_exit(rc,report(row,&durable,
                  bytes<=1048576u ? UINT64_C(10000000) : cold ? UINT64_C(2200000000) : UINT64_C(1500000000),
                  bytes<=1048576u ? UINT64_C(50000000) : cold ? UINT64_C(3000000000) : UINT64_C(2500000000),power,load,track,BENCH_INTERACTION_MIN_N));
    (void)snprintf(row,sizeof row,"journal_saved_%s",size_name);
    rc=bench_merge_exit(rc,report(row,&transaction,
                  0,0,power,load,track,BENCH_INTERACTION_MIN_N));
    if (iterations<BENCH_INTERACTION_MIN_N) return 3; /* even TRACK cannot qualify a probe */
    return rc;
}
static int run_population(void *argument)
{ population_args *args=argument; return run_population_impl(args->bytes,args->track,args->cold,args->iterations); }
static void stuck_retirement(work_ctx *ctx)
{
    _Atomic bool *entered = ctx->arg; atomic_store(entered, true);
    for (;;) (void)nanosleep(&(struct timespec){0, 1000000}, NULL);
}
static int missing_retirement(void *arg)
{
    (void)arg;
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    if (!pool || work_pool_init(pool, 1, 0)) return 1;
    _Atomic bool entered = false;
    work_handle h = work_submit(pool, (work_job){stuck_retirement, &entered, 1, WORK_BULK});
    if (!h.epoch) return 1;
    while (!atomic_load(&entered)) (void)nanosleep(&(struct timespec){0, 1000000}, NULL);
    puts("PROBE savectl missing retirement entered shutdown"); fflush(stdout);
    work_pool_shutdown(pool); free(pool); return 1;
}
static int missing_completion(void *arg)
{
    (void)arg; save_batch batch = {.bytes=4096, .drop_completion=true};
    puts("PROBE savectl missing completion enters publication wait"); fflush(stdout);
    return run_size(&batch);
}
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0); trace_init();
    if (argc == 2 && (!strcmp(argv[1], "--self-check-missing-completion") || !strcmp(argv[1], "--self-check-missing-retirement"))) {
        bool completion=!strcmp(argv[1], "--self-check-missing-completion");
        int rc=savectl_fixture_supervise(completion ? missing_completion : missing_retirement,NULL,
                                     UINT64_C(100000000),completion ? "savectl missing completion" : "savectl missing retirement");
        printf("PROBE savectl supervised_%s exit=%d expected=2 (G)\n",completion ? "completion" : "retirement",rc);
        int main_exit=bench_merge_exit(0,rc==2 ? 1 : rc);
        printf("PROBE savectl TRACK timeout remains fatal exit=%d expected=1 (G)\n",main_exit);
        return rc==2 && main_exit==1 ? 0 : 1;
    }
    bool track=false, probe=false, guard=false; const char *selected=NULL;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--track")) track=true;
        else if (!strcmp(argv[i],"--gate")) track=false;
        else if (!strcmp(argv[i],"--guard")) guard=true;
        else if (!strcmp(argv[i],"--probe")) probe=true;
        else if (!strcmp(argv[i],"--row") && i+1<argc) selected=argv[++i];
        else return 2;
    }
    puts(guard ? "savectl_bench: small-file regression guards; binding workloads UNAVAILABLE in this mode" : "savectl_bench: full 1MiB / 1GiB warm-source / 1GiB verified cold-source durable replacement; repeated populations; supervised waits/retirement/shutdown");
    const char *names[]={guard ? "4KiB" : "small",guard ? "64KiB" : "warm","cold"}; int result=0; bool ran=false;
    for (size_t i=0;i<(guard ? 2u : 3u);i++) {
        if (selected && strcmp(selected,names[i])) continue;
        ran=true; population_args args={guard ? (i ? 65536u : 4096u) : (i ? 1073741824u : 1048576u),track,!guard && i==2,probe ? 1u : BATCH_SAMPLES};
        uint64_t budget=UINT64_C(60000000000)+(uint64_t)args.iterations*UINT64_C(40000000000);
        int code=savectl_fixture_supervise(run_population,&args,budget,names[i]);
        result=bench_merge_exit(result,code==2 ? 1 : code);
    }
    if (!ran) return 2;
    puts("G8s saving-frame-submitted endpoint UNAVAILABLE: snapshot/enqueue row is a regression guard only");
    return result==0 ? 3 : result;
}
