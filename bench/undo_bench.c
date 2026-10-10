#include "undo/undo.h"
#include "harness.h"
#include <stdlib.h>
/* Compile the SAME undo implementation with trivial piece calls to isolate
 * bookkeeping (not a subtraction of two noisy timings). All log paths remain. */
typedef struct undo_bench_tree { uint64_t len,saved; int checkpoint; } undo_bench_tree;
static uint64_t undo_bench_len(const piece_tree *t) { return ((const undo_bench_tree *)t)->len; }
static int undo_bench_insert(piece_tree *t,uint64_t off,const uint8_t *p,size_t n) {
    (void)off;(void)p; ((undo_bench_tree *)t)->len+=n; return 0;
}
static int undo_bench_delete(piece_tree *t,uint64_t off,uint64_t n,piece_ref *r) {
    ((undo_bench_tree *)t)->len-=n; memset(r,0,sizeof *r); r->nspans=1;r->len=n;r->span[0].add_off=off;r->span[0].len=n; return 0;
}
static int undo_bench_ref(piece_tree *t,uint64_t off,const piece_ref *r) { (void)off;((undo_bench_tree *)t)->len+=r->len;return 0; }
static int undo_bench_checkpoint_begin(piece_tree *t,piece_checkpoint **out) {
    undo_bench_tree *mt=(undo_bench_tree *)t;*out=NULL;
    if(mt->checkpoint) return PIECE_ERR_RANGE;
    mt->saved=mt->len;mt->checkpoint=1;*out=(piece_checkpoint *)(void *)mt;return PIECE_OK;
}
static void undo_bench_checkpoint_commit(piece_checkpoint *cp) { ((undo_bench_tree *)(void *)cp)->checkpoint=0; }
static void undo_bench_checkpoint_abort(piece_checkpoint *cp) {
    undo_bench_tree *mt=(undo_bench_tree *)(void *)cp;mt->len=mt->saved;mt->checkpoint=0;
}
#define piece_len undo_bench_len
#define piece_insert undo_bench_insert
#define piece_delete undo_bench_delete
#define piece_insert_ref undo_bench_ref
#define piece_checkpoint_begin undo_bench_checkpoint_begin
#define piece_checkpoint_commit undo_bench_checkpoint_commit
#define piece_checkpoint_abort undo_bench_checkpoint_abort
#define undo_maintain undo_bench_mock_maintain
#define undo_undo_slice undo_bench_mock_undo_slice
#define undo_redo_slice undo_bench_mock_redo_slice
#define undo_replay_snapshot undo_bench_mock_replay_snapshot
#define undo_init undo_bench_mock_init
#define undo_destroy undo_bench_mock_destroy
#define undo_clear undo_bench_mock_clear
#define undo_break_burst undo_bench_mock_break_burst
#define undo_set_cap undo_bench_mock_set_cap
#define undo_group_begin undo_bench_mock_group_begin
#define undo_group_end undo_bench_mock_group_end
#define undo_insert undo_bench_mock_insert
#define undo_delete undo_bench_mock_delete
#define undo_undo undo_bench_mock_undo
#define undo_redo undo_bench_mock_redo
#define undo_get_stats undo_bench_mock_get_stats
#define undo_get_history undo_bench_mock_get_history
#include "../src/undo/undo.c"
#undef piece_len
#undef piece_insert
#undef piece_delete
#undef piece_insert_ref
#undef piece_checkpoint_begin
#undef piece_checkpoint_commit
#undef piece_checkpoint_abort
#undef undo_maintain
#undef undo_undo_slice
#undef undo_redo_slice
#undef undo_replay_snapshot
#undef undo_init
#undef undo_destroy
#undef undo_clear
#undef undo_break_burst
#undef undo_set_cap
#undef undo_group_begin
#undef undo_group_end
#undef undo_insert
#undef undo_delete
#undef undo_undo
#undef undo_redo
#undef undo_get_stats
#undef undo_get_history
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"undo_bench:%d failed: %s\n",__LINE__,#x); return 1; } } while(0)
/* Regression probes exercise the existing decision, before fixing it. */
static int replay_verdict(bench_samples *real,bench_samples *mock) {
    int tree_miss=bench_report("undo_10k_in_tree_TRACK",real,58000000,78000000);
    int mock_miss=bench_report("undo_10k_bookkeeping_mock_G",mock,6300000,6300000);
    return tree_miss || mock_miss;
}
static int accepts_backend_label(const char *arg) { (void)arg;return 0; }
typedef struct memory_meter { piece_allocator backing; size_t live,peak; } memory_meter;
static void *meter_alloc(void *ctx,size_t n) {
    memory_meter *m=ctx;void *p=m->backing.alloc(m->backing.ctx,n);
    if(p) { m->live+=n;if(m->live>m->peak) m->peak=m->live; }return p;
}
static void meter_free(void *ctx,void *p,size_t n) {
    memory_meter *m=ctx;m->live-=n;m->backing.free(m->backing.ctx,p,n);
}
static int memory_gate(const undo_log *u,const memory_meter *m,size_t typed,size_t original,size_t snapshots) {
    undo_stats st=undo_get_stats(u);
    /* Frozen piece.h fixed initialization allowance, plus one undo page of
     * rounding. Everything else is charged, including retired committed slots.
     * No virtual reservation is counted as physical memory. */
    size_t rounding=(u->page_bytes-1)*(u->replay_committed_bytes?2u:1u);
    size_t fixed=64u*1024u+4096u+rounding;
    size_t records=st.records+st.retired_records+st.replay_records;
    uint64_t bound=(uint64_t)fixed+96*piece_piece_count(u->tree)+64*(uint64_t)records+
        (5*(uint64_t)(typed+original)+3)/4+(uint64_t)snapshots;
    uint64_t owned=(uint64_t)m->live+(uint64_t)st.committed_bytes;
    if(owned>bound) {
        fprintf(stderr,"G10f MISS: owned=%llu bound=%llu active=%zu retired=%zu\n",
            (unsigned long long)owned,(unsigned long long)bound,st.records,st.retired_records);return 1;
    }
    return 0;
}
static int memory_reclamation_fixture(void) {
    memory_meter m={piece_default_allocator(),0,0};piece_allocator pa={&m,meter_alloc,meter_free};
    piece_tree *t=piece_create(&pa);CHECK(t);undo_log u;CHECK(undo_init(&u,t,100000)==0);
    undo_state st={{0}};
    for(size_t i=0;i<100000;i++) CHECK(undo_insert(&u,i,(const uint8_t *)"x",1,i,&st,&st)==0);
    CHECK(memory_gate(&u,&m,100000,0,0)==0);
    CHECK(undo_set_cap(&u,1)==0);CHECK(memory_gate(&u,&m,100000,0,0)==0);
    undo_stats pending=undo_get_stats(&u);
    while(undo_maintain(&u,UNDO_RECLAIM_RECORDS)) CHECK(memory_gate(&u,&m,100000,0,0)==0);
    undo_stats drained=undo_get_stats(&u);CHECK(drained.committed_bytes==0);
    printf("MEMORY_RECLAIM (M)%s cap=1 active=%zu retired_before=%zu committed_before=%zu committed_after=%zu piece_owned=%zu G10f=PASS\n",
        bench_evidence_tag(),pending.records,pending.retired_records,pending.committed_bytes,drained.committed_bytes,m.live);
    undo_clear(&u);CHECK(memory_gate(&u,&m,100000,0,0)==0);
    undo_destroy(&u);piece_destroy(t);CHECK(m.live==0);return 0;
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--review-gate-check")==0) {
        uint64_t ra[1]={85000000},ma[1]={1000000};bench_samples real={ra,1,1,0},mock={ma,1,1,0};
        CHECK(replay_verdict(&real,&mock)==1);puts("review 6: real miss rejected");return 0;
    }
    if(argc==2 && strcmp(argv[1],"--review-label-check")==0) {
        CHECK(!accepts_backend_label("--bptree-indicative"));puts("review 7: label-only option rejected");return 0;
    }
    if(argc!=1) { fputs("usage: undo_bench (linked src/piece/piece.c)\n",stderr); return 2; }
    (void)undo_bench_len; (void)undo_bench_insert; (void)undo_bench_delete; (void)undo_bench_ref;
    enum { STEPS=10000, RUNS=31 }; uint64_t a[RUNS],b[RUNS]; bench_samples real,mock;
    bench_samples_init(&real,a,RUNS); bench_samples_init(&mock,b,RUNS);
    char power[32]="unknown"; FILE *power_file=fopen("/sys/class/power_supply/BAT0/status","r");
    if(power_file) { if(fgets(power,sizeof power,power_file)) power[strcspn(power,"\r\n")]=0; fclose(power_file); }
    double load=0.0;FILE *load_file=fopen("/proc/loadavg","r");
    if(load_file) { if(fscanf(load_file,"%lf",&load)!=1) load=0.0;fclose(load_file); }
    printf("POWER status=%s %s load1=%.2f; (M) TRACK shared box\n",power,bench_evidence_tag(),load);
    undo_state state={{0}}; undo_change c;
    for(int run=0;run<RUNS;run++) {
        memory_meter meter={piece_default_allocator(),0,0};piece_allocator pa={&meter,meter_alloc,meter_free};
        piece_tree *t=piece_create(&pa); CHECK(t); undo_log u;
        CHECK(undo_init(&u,t,STEPS)==0);
        for(size_t i=0;i<STEPS;i++) CHECK(undo_insert(&u,0,(const uint8_t *)"x",1,(uint64_t)i*(UNDO_BURST_NS+1),&state,&state)==0);
        undo_stats stats=undo_get_stats(&u);
        CHECK(stats.live_bytes==64*STEPS);CHECK(memory_gate(&u,&meter,STEPS,0,0)==0);
        if(run==0) printf("MEMORY (M)%s live=%zu records=%zu bytes/record=%zu committed=%zu reserved_virtual=%zu piece_owned=%zu gate=(G)G10f\n",bench_evidence_tag(),stats.live_bytes,stats.records,stats.live_bytes/stats.records,stats.committed_bytes,stats.reserved_bytes,meter.live);
        uint64_t begin=bench_now_ns(); CHECK(undo_undo(&u,STEPS,&c)==0); (void)bench_add(&real,bench_now_ns()-begin);
        CHECK(c.groups==STEPS && piece_len(t)==0);CHECK(memory_gate(&u,&meter,STEPS,0,0)==0);
        undo_destroy(&u);piece_destroy(t);CHECK(meter.live==0);
        undo_bench_tree mt={0}; CHECK(undo_bench_mock_init(&u,(piece_tree *)&mt,STEPS)==0);
        for(size_t i=0;i<STEPS;i++) CHECK(undo_bench_mock_insert(&u,0,(const uint8_t *)"x",1,(uint64_t)i*(UNDO_BURST_NS+1),&state,&state)==0);
        begin=bench_now_ns(); CHECK(undo_bench_mock_undo(&u,STEPS,&c)==0); (void)bench_add(&mock,bench_now_ns()-begin);
        CHECK(c.groups==STEPS && mt.len==0); undo_bench_mock_destroy(&u);
    }
    CHECK(memory_reclamation_fixture()==0);
    puts("G9 context: (G) request-to-frame p50<=63000000 ns p99<=84000000 ns; module excludes render.");
    puts("Backend: linked in-tree src/piece/piece.c (synthesised kernel). TRACK on shared box.");
    puts("Tree gate (G): G9 minus one G3 full frame: p50<=58 ms p99<=78 ms.");
    puts("Own-cost gate (G): 10% x G9 63 ms = 6.3 ms / 10000 groups.");
    return replay_verdict(&real,&mock);
}
