#include "savectl/savectl.h"
#include "trace/trace.h"
#include "harness.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

static void route(const work_msg *msg, void *ctx) { (void)savectl_receive(ctx,msg); }
static void settle(savectl *s, work_pool *pool)
{
    for (;;) {
        (void)work_mailbox_drain(pool,route,s); savectl_tick(s);
        if (!savectl_get_model(s).busy) return;
        struct pollfd pfd={.fd=work_pool_eventfd(pool),.events=POLLIN};
        (void)poll(&pfd,1,1);
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
static int report(const char *name, bench_samples *samples, uint64_t g50, uint64_t g99,
                   const char *power, double load, bool gates)
{
    uint64_t p50=bench_p50(samples),p99=bench_p99(samples);
    printf("TRACK %s n=%zu p50=%.3f ms p99=%.3f ms (M)%s load1=%.2f power=%s; gate=%.0f/%.0f ms (G)\n",
        name,samples->n,(double)p50/1e6,(double)p99/1e6,bench__tag_from_power(power),load,power,
        (double)g50/1e6,(double)g99/1e6);
    return samples->n==0 || (gates && (p50>g50 || p99>g99));
}
static int run_size(size_t bytes, size_t iterations, bool gates)
{
    int rc=1;
    char directory[]="/tmp/edit-savectl-bench-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    char target[128],logpath[128];
    (void)snprintf(target,sizeof target,"%s/target",directory);
    (void)snprintf(logpath,sizeof logpath,"%s/journal",directory);
    /* Anonymous immutable original: preserves a genuinely warm snapshot across
     * replacement, without relying on the missing file mapping guard accessor.
     * Corpus is only read, never modified. Large fixture lives only in bench. */
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
    work_pool *pool=calloc(1,sizeof *pool),*jp=calloc(1,sizeof *jp);
    if (!pool || !jp) { free(pool); free(jp); goto file_end; }
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
    uint64_t ack_data[128],durable_data[128],transaction_data[128];
    bench_samples ack,durable,transaction;
    bench_samples_init(&ack,ack_data,128); bench_samples_init(&durable,durable_data,128);
    bench_samples_init(&transaction,transaction_data,128);
    char power[32]; double load=load_stamp(power,sizeof power);
    for (size_t i=0;i<iterations;++i) {
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
    rc=report(bytes>FILE_PREFIX_MAX ? "G8s_ack_1GB" : "G8s_ack_1MB",&ack,2000000,5000000,power,load,gates);
    rc|=report(bytes>FILE_PREFIX_MAX ? "G8d_1GB_warm_durable" : "G8d_1MB_durable",&durable,
        bytes>FILE_PREFIX_MAX ? UINT64_C(1500000000) : UINT64_C(10000000),
        bytes>FILE_PREFIX_MAX ? UINT64_C(2500000000) : UINT64_C(50000000),power,load,gates);
    rc|=report(bytes>FILE_PREFIX_MAX ? "journal_saved_1GB" : "journal_saved_1MB",&transaction,0,0,power,load,false);
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
int main(int argc, char **argv)
{
    bool gates=argc==2 && !strcmp(argv[1],"--gate");
    trace_init();
    int rc=run_size(FILE_PREFIX_MAX,128,gates);
    if (!rc) rc=run_size((size_t)1u<<30,7,gates);
    return rc;
}
