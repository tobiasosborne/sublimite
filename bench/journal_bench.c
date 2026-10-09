#include "journal/journal.h"
#include "harness.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void route(const work_msg *m, void *ctx) { (void)journal_receive(ctx,m); }
typedef struct replay_context { uint64_t records; piece_tree *tree; } replay_context;
static int restore(void *ctx, const journal_record *r)
{
    replay_context *c=ctx;
    int rc=journal_apply_piece(c->tree,r);
    if(!rc) c->records++;
    return rc;
}
static void nap(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static int run(work_pool *pool, size_t payload)
{
    char path[]="/tmp/journal-bench-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 1; close(fd);
    journal *j; journal_options opts={262144,268435456}; if(journal_open(&j,path,pool,&opts)) return 1;
    edit_arena a; if(edit_arena_init(&a,100000*sizeof(uint64_t))) return 1;
    uint64_t *samples=edit_arena_alloc(&a,100000*sizeof(uint64_t),16); bench_samples s; bench_samples_init(&s,samples,100000);
    uint8_t data[1024]; memset(data,'x',sizeof data);
    size_t group=payload==1?1024:128;
    for(size_t i=0;i<100000;i++) {
        int rc; BENCH_TIME(&s,rc=journal_insert(j,1,i*payload,data,payload)); if(rc) { fprintf(stderr,"append rc=%d\n",rc); return 1; }
        if((i+1)%group==0 && journal_flush(j)) return 1;
    }
    if(journal_flush(j)) return 1;
    int fail=bench_report(payload==1?"journal_append_1B_(M)_ns":"journal_append_1KiB_(M)_ns",&s,0,20000);
    journal_stats stats=journal_get_stats(j); journal_close(j);
    piece_allocator allocator=piece_default_allocator();
    replay_context replay={.tree=piece_create(&allocator)};
    if(!replay.tree) return 1;
    journal_replay_result rr; uint64_t begin=bench_now_ns();
    if(journal_replay_file(path,restore,&replay,&rr)) return 1;
    uint64_t elapsed=bench_now_ns()-begin;
    if(replay.records!=100000 || piece_len(replay.tree)!=100000*payload) return 1;
    printf("TRACK journal_replay mode=piece_tree payload=%zu records=%llu MB_s=%.2f records_s=%.0f (M)%s\n",payload,(unsigned long long)replay.records,(double)stats.file_bytes*1000.0/(double)elapsed,(double)replay.records*1e9/(double)elapsed,bench_evidence_tag());
    piece_destroy(replay.tree);
    printf("TRACK journal_sync payload=%zu syncs=%llu max_bytes=%llu last_bytes=%llu max_interval_ms=%.3f (M)%s threshold=64KiB_or_1s\n",payload,(unsigned long long)stats.syncs,(unsigned long long)stats.max_sync_bytes,(unsigned long long)stats.last_sync_bytes,(double)stats.max_sync_interval_ns/1e6,bench_evidence_tag());
    edit_arena_free(&a); unlink(path); return fail;
}
int main(void)
{
    char power[32]; printf("power=%s %s; indicative: concurrent workers may be compiling\n",bench_battery_status(power,sizeof power),bench_evidence_tag());
    work_pool pool; if(work_pool_init(&pool,1,0)) return 1;
    int fail=run(&pool,1); fail|=run(&pool,1024);
    /* No explicit flush: demonstrate the time cadence with a quiet journal. */
    char path[]="/tmp/journal-cadence-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 1; close(fd);
    journal *j; if(journal_open(&j,path,&pool,NULL)) return 1;
    if(journal_insert(j,1,0,(const uint8_t *)"x",1)) return 1;
    uint64_t begin=bench_now_ns();
    while(journal_get_stats(j).durable_sequence<1) {
        work_mailbox_drain(&pool,route,j); int rc=journal_pump(j,bench_now_ns(),false); if(rc && rc!=JOURNAL_BUSY) return 1;
        if(bench_now_ns()-begin>5000000000ull) return 1;
        nap();
    }
    journal_stats st=journal_get_stats(j);
    printf("TRACK journal_idle_sync interval_ms=%.3f bytes=%llu (M)%s\n",(double)st.max_sync_interval_ns/1e6,(unsigned long long)st.last_sync_bytes,bench_evidence_tag());
    journal_close(j); unlink(path); work_pool_shutdown(&pool); return fail;
}
