#include "journal/journal.h"
#include "harness.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Default GATE enforces component ceilings; explicit --track is diagnostic.
 * Input/layout/render and real index/find integration belong to the
 * whole-editor G1/G9 runs. All journals use NULL/default options. */
#define SESSION_EDITS 100000u
#define TEXT_CAP 16384u
#define BULK_MESSAGE 0x4a424c4bu
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"journal_bench:%d FAIL %s\n",__LINE__,#x); return 1; } } while(0)
static unsigned append_misses(bench_samples *samples)
{ return bench_p99(samples)>20000 || samples->dropped?1u:0u; }
static unsigned paste_misses(bench_samples *samples)
{ return bench_p50(samples)>5000000 || bench_p99(samples)>15000000 || samples->dropped?1u:0u; }
static void nap(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static int temporary(char *path) { int fd=mkstemp(path); if(fd>=0) close(fd); return fd; }
static void stamp(const char *row)
{
    char power[32], load[64]="unknown"; FILE *f=fopen("/proc/loadavg","r");
    if(f) { if(fscanf(f,"%63s",load)!=1) strcpy(load,"unknown"); fclose(f); }
    printf("TRACK journal_conditions row=%s power=%s %s load1=%s options=default batch_bytes=%u queue_bytes=%u initial_file_limit=%llu sync_bytes=%llu sync_ns=%llu\n",
        row,bench_battery_status(power,sizeof power),bench_evidence_tag(),load,
        JOURNAL_DEFAULT_BATCH_BYTES,2*JOURNAL_DEFAULT_BATCH_BYTES,
        (unsigned long long)JOURNAL_DEFAULT_FILE_BYTES,(unsigned long long)JOURNAL_DEFAULT_SYNC_BYTES,(unsigned long long)JOURNAL_DEFAULT_SYNC_NS);
}
static const char *evidence(char *out, size_t cap)
{
    char load[64]="unknown"; FILE *f=fopen("/proc/loadavg","r");
    if(f) { if(fscanf(f,"%63s",load)!=1) strcpy(load,"unknown"); fclose(f); }
    (void)snprintf(out,cap,"%s load1=%s",bench_evidence_tag(),load); return out;
}
typedef struct router { journal *j; unsigned bulk_done; int bulk_error; } router;
static void route(const work_msg *m, void *ctx)
{
    router *r=ctx;
    if(journal_receive(r->j,m)) return;
    if(m->kind==BULK_MESSAGE) { int rc; memcpy(&rc,m->data,sizeof rc); r->bulk_done++; r->bulk_error|=rc; }
}
static int tick(work_pool *pool, router *r)
{
    work_mailbox_drain(pool,route,r); int rc=journal_pump(r->j,bench_now_ns(),false);
    return rc==JOURNAL_BUSY?0:rc;
}
/* Representative competing bulk jobs, explicit stand-ins rather than a claim
 * of whole-editor integration: index counts newlines, find scans a needle,
 * save writes+syncs a scratch inode. Each CPU slice polls cancellation. */
typedef struct bulk_job { const uint8_t *bytes; size_t size; unsigned mode; int fd; } bulk_job;
static void competing(work_ctx *ctx)
{
    bulk_job *b=ctx->arg; int rc=0; uint64_t sum=0, end=bench_now_ns()+3000000u;
    if(b->mode==2) {
        if(pwrite(b->fd,b->bytes,b->size,0)!=(ssize_t)b->size || fdatasync(b->fd)) rc=1;
    } else do {
        for(size_t off=0;off<b->size;off+=4096) {
            size_t n=b->size-off; if(n>4096) n=4096;
            for(size_t k=0;k<n;k++) sum+=b->bytes[off+k]==(b->mode?'q':'\n');
            if(work_should_stop(ctx)) return;
        }
    } while(bench_now_ns()<end);
    work_msg msg={.kind=BULK_MESSAGE}; memcpy(msg.data,&rc,sizeof rc); memcpy(msg.data+8,&sum,sizeof sum);
    while(!work_should_stop(ctx) && !work_publish(ctx,&msg)) nap();
}
typedef struct text_model { uint8_t bytes[TEXT_CAP]; size_t size; } text_model;
typedef struct restored { piece_tree *trees[2]; unsigned bases, views, tabs, windows; uint64_t edits;
    journal_view view[2]; uint64_t tab_ids[2], tab_active; uint32_t width, height;
} restored;
static uint64_t read64(const uint8_t *p)
{ uint64_t value=0; for(unsigned k=0;k<8;k++) value|=(uint64_t)p[k]<<(8*k); return value; }
static uint32_t read32(const uint8_t *p)
{ uint32_t value=0; for(unsigned k=0;k<4;k++) value|=(uint32_t)p[k]<<(8*k); return value; }

static int restore(void *ctx, const journal_record *r)
{
    restored *c=ctx;
    if(r->type==JOURNAL_TABS) {
        if(read64(r->data)!=2) return 1;
        c->tab_active=read64(r->data+8); c->tab_ids[0]=read64(r->data+16); c->tab_ids[1]=read64(r->data+24);
        c->tabs++; return 0;
    }
    if(r->type==JOURNAL_WINDOW) {
        c->width=read32(r->data); c->height=read32(r->data+4); c->windows++; return 0;
    }
    if(r->buffer_id<1 || r->buffer_id>2) return 1;
    piece_tree *tree=c->trees[r->buffer_id-1];
    if(r->type==JOURNAL_BASE) {
        journal_base b; char path[4097]; if(journal_decode_base(r,&b,path,sizeof path)) return 1;
        c->bases++; if(!*path) return 0;
        int fd=open(path,O_RDONLY|O_CLOEXEC); if(fd<0 || b.size>TEXT_CAP) return 1;
        uint8_t data[TEXT_CAP]; ssize_t n=read(fd,data,(size_t)b.size); close(fd);
        return n==(ssize_t)b.size?piece_init_copy(tree,data,(size_t)b.size):1;
    }
    if(r->type==JOURNAL_VIEW) {
        c->view[r->buffer_id-1]=(journal_view){read64(r->data),read64(r->data+8),read64(r->data+16),read64(r->data+24)};
        c->views++; return 0;
    }
    int rc=journal_apply_piece(tree,r); if(!rc) c->edits++; return rc;
}
static int session(work_pool *pool, size_t payload, bool contention, unsigned *misses)
{
    char tag[128];
    stamp(payload==1?"session_1B":"session_1KiB");
    char path[]="/tmp/journal-session-XXXXXX", basepath[]="/tmp/journal-bench-base-XXXXXX", bulkpath[]="/tmp/journal-bulk-XXXXXX";
    REQUIRE(temporary(path)>=0 && temporary(basepath)>=0 && temporary(bulkpath)>=0);
    int bfd=open(basepath,O_RDWR), bulkfd=open(bulkpath,O_RDWR); REQUIRE(bfd>=0 && bulkfd>=0);
    uint8_t basebytes[128]; for(size_t i=0;i<sizeof basebytes;i++) basebytes[i]=(uint8_t)i;
    REQUIRE(write(bfd,basebytes,sizeof basebytes)==sizeof basebytes); close(bfd);
    journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0); router r={.j=j}; journal_set_message_handler(j,route,&r);
    journal_base base; REQUIRE(journal_capture_base(basepath,&base)==0 && journal_set_base(j,1,&base)==0);
    REQUIRE(journal_set_base(j,2,&(journal_base){.path=""})==0);
    text_model expected[2]={0}; memcpy(expected[0].bytes,basebytes,sizeof basebytes); expected[0].size=sizeof basebytes;
    edit_arena a; REQUIRE(edit_arena_init(&a,SESSION_EDITS*sizeof(uint64_t))==0);
    uint64_t *samples=edit_arena_alloc(&a,SESSION_EDITS*sizeof(uint64_t),16); bench_samples s; bench_samples_init(&s,samples,SESSION_EDITS);
    uint8_t bytes[1024], bulkbytes[65536]; memset(bulkbytes,'q',sizeof bulkbytes);
    bulk_job jobs[3]={{bulkbytes,sizeof bulkbytes,0,bulkfd},{bulkbytes,sizeof bulkbytes,1,bulkfd},{bulkbytes,sizeof bulkbytes,2,bulkfd}};
    unsigned submitted=0, pauses=0; uint64_t pause_ns=0, max_pause_ns=0;
    uint64_t rng=0x9e3779b97f4a7c15ull, next_tick=bench_now_ns()+5000000u, inserted=0, removed=0;
    for(size_t i=0;i<SESSION_EDITS;i++) {
        /* The workload generator delays delivery while prior requests are
         * pending. Enqueue never waits/retries FULL; these waits are reported.
         * Use a conservative byte bound for mixed INSERT/DELETE records. */
        uint64_t pending_limit=JOURNAL_DEFAULT_BATCH_BYTES/2;
        journal_stats pending=journal_get_stats(j);
        if(pending.pending_bytes>=pending_limit) {
            uint64_t wait_start=bench_now_ns(); pauses++;
            do {
                uint64_t now=bench_now_ns();
                if(now>=next_tick) { REQUIRE(tick(pool,&r)==0); next_tick=now+5000000u; }
                REQUIRE(now-wait_start<5000000000ull); nap(); pending=journal_get_stats(j);
            } while(pending.pending_bytes>=pending_limit);
            uint64_t waited=bench_now_ns()-wait_start; pause_ns+=waited; if(waited>max_pause_ns) max_pause_ns=waited;
        }
        rng^=rng<<13; rng^=rng>>7; rng^=rng<<17;
        size_t id=(size_t)(rng&1u); text_model *m=&expected[id];
        bool del=m->size>=payload && (m->size+payload>TEXT_CAP || ((rng>>1)&3u)==0);
        size_t at=(size_t)((rng>>8)%(del?m->size-payload+1:m->size+1));
        memset(bytes,(int)(uint8_t)(rng>>32),payload); int rc;
        BENCH_TIME(&s,rc=del?journal_delete(j,id+1,at,payload):journal_insert(j,id+1,at,bytes,payload));
        if(rc) fprintf(stderr,"journal_bench: enqueue failure edit=%zu rc=%d accepted=%llu written=%llu\n",i,rc,(unsigned long long)journal_get_stats(j).accepted_sequence,(unsigned long long)journal_get_stats(j).written_sequence);
        REQUIRE(rc==JOURNAL_OK);
        if(del) { memmove(m->bytes+at,m->bytes+at+payload,m->size-at-payload); m->size-=payload; removed+=payload; }
        else { memmove(m->bytes+at+payload,m->bytes+at,m->size-at); memcpy(m->bytes+at,bytes,payload); m->size+=payload; inserted+=payload; }
        uint64_t now=bench_now_ns();
        if(now>=next_tick) {
            REQUIRE(tick(pool,&r)==0); next_tick=now+5000000u;
            if(contention && submitted==r.bulk_done && submitted<60) {
                work_handle h=work_submit(pool,(work_job){competing,&jobs[submitted%3],0,WORK_BULK}); REQUIRE(h.epoch!=0); submitted++;
            }
        }
        /* Simulated event-loop batches, outside the measured enqueue. No
         * blocking flush between keys; pump runs at the normal timer tick. */
        if(i%64==63) nap();
    }
    journal_view views[2]={{expected[0].size,13,4099,27},{5,expected[1].size,8187,41}};
    REQUIRE(journal_set_view(j,1,&views[0])==0 && journal_set_view(j,2,&views[1])==0);
    uint64_t ids[]={1,2}; REQUIRE(journal_set_tabs(j,ids,2,1)==0 && journal_set_window(j,1280,720)==0);
    REQUIRE(journal_flush(j)==0);
    while(r.bulk_done<submitted) { work_mailbox_drain(pool,route,&r); nap(); }
    REQUIRE(!r.bulk_error); journal_stats st=journal_get_stats(j); journal_close(j);
    uint64_t p50=bench_p50(&s), p99=bench_p99(&s);
    *misses+=append_misses(&s);
    printf("TRACK journal_append payload=%zu edits=%u p50_ns=%llu p99_ns=%llu gate_p99_ns=20000_(G) within_gate=%d ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=%u bulk=index_scan/find_scan/save_write_standins (M)%s\n",
        payload,SESSION_EDITS,(unsigned long long)p50,(unsigned long long)p99,p99<=20000?1:0,submitted,evidence(tag,sizeof tag));
    printf("TRACK journal_backpressure payload=%zu delivery_pauses=%u total_pause_ms=%.3f max_pause_ms=%.3f measured_enqueue_excludes_delivery_wait=1 (M)%s\n",payload,pauses,(double)pause_ns/1e6,(double)max_pause_ns/1e6,evidence(tag,sizeof tag));
    piece_allocator alloc=piece_default_allocator(); restored c={.trees={piece_create(&alloc),piece_create(&alloc)}}; REQUIRE(c.trees[0] && c.trees[1]);
    journal_replay_result rr; uint64_t begin=bench_now_ns(); REQUIRE(journal_replay_file(path,restore,&c,&rr)==0); uint64_t elapsed=bench_now_ns()-begin;
    for(unsigned id=0;id<2;id++) {
        uint8_t text[TEXT_CAP]; REQUIRE(piece_len(c.trees[id])==expected[id].size && piece_read(c.trees[id],0,text,expected[id].size)==0 && !memcmp(text,expected[id].bytes,expected[id].size)); piece_destroy(c.trees[id]);
    }
    REQUIRE(!rr.corrupt && c.edits==SESSION_EDITS && c.bases==2 && c.views==2 && c.tabs==1 && c.windows==1);
    for(unsigned id=0;id<2;id++) REQUIRE(c.view[id].cursor==views[id].cursor && c.view[id].anchor==views[id].anchor &&
        c.view[id].scroll_byte==views[id].scroll_byte && c.view[id].scroll_x==views[id].scroll_x);
    REQUIRE(c.tab_ids[0]==1 && c.tab_ids[1]==2 && c.tab_active==1 && c.width==1280 && c.height==720);
    printf("TRACK journal_replay payload=%zu edits=%llu records=%llu wire_MB_s=%.2f inserted_payload_MB_s=%.2f records_s=%.0f wire_bytes=%llu inserted_bytes=%llu deleted_bytes=%llu content_verified=1 base_load=1 buffers=2 session_restored=1 (M)%s\n",
        payload,(unsigned long long)c.edits,(unsigned long long)rr.records,(double)st.file_bytes*1000.0/(double)elapsed,(double)inserted*1000.0/(double)elapsed,(double)rr.records*1e9/(double)elapsed,
        (unsigned long long)st.file_bytes,(unsigned long long)inserted,(unsigned long long)removed,evidence(tag,sizeof tag));
    printf("TRACK journal_sync payload=%zu syncs=%llu max_bytes=%llu max_interval_ms=%.3f timer_driven=1 (M)%s\n",payload,(unsigned long long)st.syncs,(unsigned long long)st.max_sync_bytes,(double)st.max_sync_interval_ns/1e6,evidence(tag,sizeof tag));
    edit_arena_free(&a); close(bulkfd); unlink(bulkpath); unlink(basepath); unlink(path); return 0;
}
typedef struct paste_replay { const uint8_t *bytes; size_t size, copied; piece_tree *tree; } paste_replay;
static int restore_paste(void *ctx, const journal_record *r)
{
    paste_replay *c=ctx; if(r->type==JOURNAL_BASE) return 0;
    if(r->type!=JOURNAL_INSERT || r->size-8>c->size-c->copied || memcmp(c->bytes+c->copied,r->data+8,r->size-8)) return 1;
    int rc=journal_apply_piece(c->tree,r); c->copied+=r->size-8; return rc;
}
static int paste(work_pool *pool, unsigned requests, unsigned *misses)
{
    char tag[128];
    stamp("paste_1MB"); size_t n=1000000; uint8_t *bytes=malloc(n); REQUIRE(bytes);
    for(size_t i=0;i<n;i++) bytes[i]=(uint8_t)(i*17u);
    uint64_t times[100]; REQUIRE(requests<=100); bench_samples s; bench_samples_init(&s,times,requests);
    for(unsigned i=0;i<requests;i++) {
        char path[]="/tmp/journal-paste-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
        journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0 && journal_set_base(j,1,&(journal_base){.path=""})==0);
        int rc; edit_malloc_guard_begin(); BENCH_TIME(&s,rc=journal_insert(j,1,0,bytes,n)); size_t allocations=edit_malloc_guard_end();
        REQUIRE(rc==0 && allocations==0); REQUIRE(journal_flush(j)==0); journal_close(j);
        piece_allocator a=piece_default_allocator(); paste_replay c={bytes,n,0,piece_create(&a)}; REQUIRE(c.tree);
        journal_replay_result rr; REQUIRE(journal_replay_file(path,restore_paste,&c,&rr)==0 && !rr.corrupt && c.copied==n && piece_len(c.tree)==n);
        uint8_t text[4096];
        for(size_t off=0;off<n;off+=sizeof text) { size_t k=n-off; if(k>sizeof text) k=sizeof text; REQUIRE(piece_read(c.tree,off,text,k)==0 && !memcmp(text,bytes+off,k)); }
        piece_destroy(c.tree); unlink(path);
    }
    unsigned failures=paste_misses(&s); *misses+=failures;
    printf("TRACK journal_paste bytes=%zu requests=%u p50_ms=%.3f p99_ms=%.3f component_ceiling_ms=5/15_(G) within_gate=%d allocations=0 guard=%s content_verified=1 endpoint=journal_enqueue G9_full_frame_ms=5/15_(G)_unmeasured (M)%s\n",
        n,requests,(double)bench_p50(&s)/1e6,(double)bench_p99(&s)/1e6,failures?0:1,edit_malloc_guard_active()?"active":"inactive",evidence(tag,sizeof tag));
    free(bytes); return 0;
}
static uint64_t worker_cpu_ns(clockid_t clk)
{ struct timespec t; if(clock_gettime(clk,&t)) return 0; return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static int worker_cpu(work_pool *pool)
{
    char tag[128];
    stamp("worker_cpu"); char path[]="/tmp/journal-cpu-XXXXXX"; REQUIRE(temporary(path)>=0);
    journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0);
    clockid_t clk; REQUIRE(pthread_getcpuclockid(pool->threads[0],&clk)==0);
    uint64_t before=worker_cpu_ns(clk), parked_cpu=0; unsigned syncs=8;
    for(unsigned i=0;i<syncs;i++) {
        REQUIRE(journal_insert(j,1,i,(const uint8_t *)"x",1)==0 && journal_pump(j,1,true)==0);
        unsigned waiting=0; while(atomic_load(&pool->mb[0].head)==atomic_load(&pool->mb[0].tail) && waiting++<5000) nap(); REQUIRE(waiting<5000);
        uint64_t start=worker_cpu_ns(clk); struct timespec pause={0,50000000}; nanosleep(&pause,NULL); parked_cpu+=worker_cpu_ns(clk)-start;
        for(unsigned k=0;k<WORK_MAX_JOBS;k++) REQUIRE(atomic_load(&pool->slots[k].busy)==0);
        REQUIRE(journal_flush(j)==0);
    }
    printf("TRACK journal_worker_cpu syncs=%u cpu_us_per_sync=%.3f undrained_50ms_cpu_us_per_sync=%.3f worker_return=1 (M)%s\n",
        syncs,(double)(worker_cpu_ns(clk)-before)/(1000.0*syncs),(double)parked_cpu/(1000.0*syncs),evidence(tag,sizeof tag));
    journal_close(j); unlink(path); return 0;
}
/* TRACK comparison: alternate adjacent variants on the same loaded box.
 * Sum enqueue calls separately from the continuation's off-path fence waits;
 * neither endpoint includes editor mutation/layout/submit. */
static int insert_variants(work_pool *pool)
{
    stamp("insert_variants"); char tag[128];
    size_t size=1000000; uint8_t *bytes=malloc(size); REQUIRE(bytes);
    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(i*17u);
    uint64_t totals[2][16], calls[2048], completion[2][16];
    bench_samples total[2], complete[2], steps;
    for(unsigned v=0;v<2;v++) {
        bench_samples_init(&total[v],totals[v],16);
        bench_samples_init(&complete[v],completion[v],16);
    }
    bench_samples_init(&steps,calls,2048);
    for(unsigned pair=0;pair<16;pair++) for(unsigned order=0;order<2;order++) {
        unsigned variant=(pair+order)%2;
        char path[]="/tmp/journal-variant-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
        journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0);
        size_t progress=0; uint64_t cpu_calls=0, start=bench_now_ns();
        if(!variant) {
            int rc; edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
            rc=journal_insert(j,1,0,bytes,size); cpu_calls=bench_now_ns()-before;
            REQUIRE(edit_malloc_guard_end()==0 && rc==0); progress=size;
        } else while(progress<size) {
            size_t before_progress=progress;
            edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
            int rc=journal_insert_step(j,1,0,bytes,size,&progress);
            uint64_t elapsed=bench_now_ns()-before;
            REQUIRE(edit_malloc_guard_end()==0 && (rc==0 || rc==JOURNAL_BUSY));
            (void)bench_add(&steps,elapsed); cpu_calls+=elapsed;
            if(progress==before_progress) REQUIRE(journal_flush(j)==0);
        }
        (void)bench_add(&total[variant],cpu_calls); (void)bench_add(&complete[variant],bench_now_ns()-start);
        REQUIRE(journal_flush(j)==0); journal_close(j);
        piece_allocator a=piece_default_allocator(); paste_replay replayed={bytes,size,0,piece_create(&a)};
        REQUIRE(replayed.tree); journal_replay_result rr;
        REQUIRE(journal_replay_file(path,restore_paste,&replayed,&rr)==0 && !rr.corrupt && replayed.copied==size && piece_len(replayed.tree)==size);
        piece_destroy(replayed.tree); unlink(path);
    }
    for(unsigned v=0;v<2;v++) printf("TRACK journal_insert_variant variant=%s pairs=16 bytes=%zu enqueue_sum_p50_ms=%.3f enqueue_sum_p99_ms=%.3f protected_completion_p50_ms=%.3f protected_completion_p99_ms=%.3f content_verified=1 allocations=0 (M)%s\n",
        v?"continuation":"one_shot",size,(double)bench_p50(&total[v])/1e6,(double)bench_p99(&total[v])/1e6,
        (double)bench_p50(&complete[v])/1e6,(double)bench_p99(&complete[v])/1e6,evidence(tag,sizeof tag));
    printf("TRACK journal_insert_step calls=%zu p50_us=%.3f p99_us=%.3f input_check_boundaries=1 byte_credit_enforced=1 wall_time_unbounded=1 (M)%s\n",
        steps.n,(double)bench_p50(&steps)/1e3,(double)bench_p99(&steps)/1e3,evidence(tag,sizeof tag));
    REQUIRE(!steps.dropped && !total[0].dropped && !total[1].dropped);
    free(bytes); return 0;
}
typedef struct exhausted_prefix { uint64_t bytes, records; } exhausted_prefix;
static int restore_exhausted(void *ctx, const journal_record *r)
{
    exhausted_prefix *c=ctx; uint64_t offset=0;
    if(r->type!=JOURNAL_INSERT || r->buffer_id!=1 || r->size!=524288+8) return 1;
    for(unsigned k=0;k<8;k++) offset|=(uint64_t)r->data[k]<<(k*8);
    if(offset!=c->bytes || r->sequence!=c->records+1) return 1;
    for(size_t i=8;i<r->size;i++) if(r->data[i]!='x') return 1;
    c->bytes+=r->size-8; c->records++; return 0;
}
static int exhaustion(work_pool *pool)
{
    char tag[128];
    stamp("default_exhaustion"); char path[]="/tmp/journal-full-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
    journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0); router r={.j=j};
    size_t n=524288; uint8_t *bytes=malloc(n); REQUIRE(bytes); memset(bytes,'x',n); uint64_t accepted=0;
    for(;;) {
        int rc=journal_insert(j,1,accepted*n,bytes,n); if(rc==JOURNAL_FULL) break; REQUIRE(rc==0); accepted++;
        /* Wait between logical requests, off path; timer pump, no forced sync. */
        REQUIRE(tick(pool,&r)==0);
        uint64_t start=bench_now_ns();
        while(journal_get_stats(j).pending_bytes) { REQUIRE(tick(pool,&r)==0 && bench_now_ns()-start<5000000000ull); nap(); }
    }
    REQUIRE(journal_flush(j)==JOURNAL_FULL); journal_stats st=journal_get_stats(j);
    REQUIRE(st.accepted_sequence==accepted && st.durable_sequence==accepted && st.file_bytes<=JOURNAL_DEFAULT_FILE_BYTES && st.file_bytes+n>JOURNAL_DEFAULT_FILE_BYTES);
    journal_close(j);
    exhausted_prefix recovered={0}; journal_replay_result rr;
    REQUIRE(journal_replay_file(path,restore_exhausted,&recovered,&rr)==0 && !rr.corrupt && rr.records==accepted && recovered.records==accepted && recovered.bytes==accepted*n);
    printf("TRACK journal_default_exhaustion accepted=%llu wire_bytes=%llu limit=%llu flush=FULL durable_prefix=1 content_verified=1 reason=disk_budget (M)%s\n",(unsigned long long)accepted,(unsigned long long)st.file_bytes,(unsigned long long)st.file_limit_bytes,evidence(tag,sizeof tag));
    free(bytes); unlink(path); return 0;
}
static int idle(work_pool *pool, bool zero_data)
{
    char tag[128];
    stamp(zero_data?"zero_data_sync":"idle_sync"); char path[]="/tmp/journal-idle-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
    journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0); router r={.j=j}; uint8_t bytes[5000]; memset(bytes,'x',sizeof bytes);
    REQUIRE(journal_insert(j,1,0,bytes,zero_data?sizeof bytes:1)==0);
    uint64_t start=bench_now_ns(); bool saw_unsynced=false;
    while(journal_get_stats(j).durable_sequence<1) {
        REQUIRE(tick(pool,&r)==0 && bench_now_ns()-start<5000000000ull);
        journal_stats st=journal_get_stats(j); if(st.written_sequence==1 && !st.durable_sequence) saw_unsynced=true; nap();
    }
    REQUIRE(!zero_data || saw_unsynced); journal_stats st=journal_get_stats(j);
    printf("TRACK journal_%s interval_ms=%.3f bytes=%llu saw_unsynced=%d (M)%s\n",zero_data?"zero_data_sync":"idle_sync",(double)st.max_sync_interval_ns/1e6,(unsigned long long)st.last_sync_bytes,saw_unsynced?1:0,evidence(tag,sizeof tag));
    journal_close(j); unlink(path); return 0;
}

/* This policy function is also used by the real benchmark's final return. */
static int finish_bench(int correctness, bool track, unsigned misses)
{
    return correctness?correctness:(!track && misses?3:0);
}
static int gate_self_check(void)
{
    /* Deterministic injected latency samples; this does not run a benchmark. */
    uint64_t delay[100]; for(unsigned i=0;i<100;i++) delay[i]=20001;
    bench_samples samples; bench_samples_init(&samples,delay,100); samples.n=100;
    unsigned misses=append_misses(&samples);
    REQUIRE(finish_bench(0,false,misses)!=0);
    REQUIRE(finish_bench(0,true,misses)==0 && finish_bench(1,true,misses)==1);
    REQUIRE(finish_bench(0,false,0)==0);
    for(unsigned i=0;i<100;i++) delay[i]=20000;
    REQUIRE(append_misses(&samples)==0);
    samples.dropped=1; REQUIRE(append_misses(&samples)==1); samples.dropped=0;
    for(unsigned i=0;i<100;i++) delay[i]=5000001;
    REQUIRE(paste_misses(&samples)==1 && finish_bench(0,false,paste_misses(&samples))==3);
    for(unsigned i=0;i<100;i++) delay[i]=15000001;
    REQUIRE(paste_misses(&samples)==1 && finish_bench(0,false,paste_misses(&samples))==3);
    for(unsigned i=0;i<100;i++) delay[i]=5000000;
    REQUIRE(paste_misses(&samples)==0);
    puts("journal_bench: gate self-check ok (injected delay fails GATE; TRACK preserves correctness failures)");
    return 0;
}
int main(int argc, char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--gate-self-check")) return gate_self_check();
    bool variants=argc==2 && !strcmp(argv[1],"--insert-variants");
    bool track=variants || (argc==2 && !strcmp(argv[1],"--track")); unsigned misses=0;
    work_pool pool; REQUIRE(work_pool_init(&pool,1,0)==0); int fail=0;
    if(variants) fail=insert_variants(&pool);
    else if(argc==2 && !strcmp(argv[1],"--worker")) fail=worker_cpu(&pool);
    else if(argc==2 && !strcmp(argv[1],"--fixtures")) { fail=paste(&pool,1,&misses); if(!fail) fail=session(&pool,1,false,&misses); }
    else if(argc!=1 && !track) fail=2;
    else { fail=worker_cpu(&pool); if(!fail) fail=paste(&pool,100,&misses); if(!fail) fail=session(&pool,1,true,&misses); if(!fail) fail=session(&pool,1024,true,&misses); if(!fail) fail=exhaustion(&pool); if(!fail) fail=idle(&pool,false); if(!fail) fail=idle(&pool,true); }
    work_pool_shutdown(&pool);
    int status=finish_bench(fail,track,misses);
    printf("journal_bench: correctness=%s mode=%s gate_misses=%u status=%d (complete G1/G9 endpoints require editor harness)\n",
        fail?"FAIL":"ok",track?"TRACK":"GATE",misses,status);
    return status;
}
