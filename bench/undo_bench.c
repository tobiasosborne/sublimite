#include "undo/undo.h"
#include "harness.h"
#include <stdlib.h>
/* Compile the SAME undo implementation with trivial piece calls to isolate
 * bookkeeping (not a subtraction of two noisy timings). All log paths remain. */
typedef struct undo_bench_tree { uint64_t len; } undo_bench_tree;
static uint64_t undo_bench_len(const piece_tree *t) { return ((const undo_bench_tree *)t)->len; }
static int undo_bench_insert(piece_tree *t,uint64_t off,const uint8_t *p,size_t n) {
    (void)off;(void)p; ((undo_bench_tree *)t)->len+=n; return 0;
}
static int undo_bench_delete(piece_tree *t,uint64_t off,uint64_t n,piece_ref *r) {
    ((undo_bench_tree *)t)->len-=n; memset(r,0,sizeof *r); r->nspans=1;r->len=n;r->span[0].add_off=off;r->span[0].len=n; return 0;
}
static int undo_bench_ref(piece_tree *t,uint64_t off,const piece_ref *r) { (void)off;((undo_bench_tree *)t)->len+=r->len;return 0; }
#define piece_len undo_bench_len
#define piece_insert undo_bench_insert
#define piece_delete undo_bench_delete
#define piece_insert_ref undo_bench_ref
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
#include "../src/undo/undo.c"
#undef piece_len
#undef piece_insert
#undef piece_delete
#undef piece_insert_ref
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
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"undo_bench:%d failed: %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char **argv) {
    int indicative=argc==2 && strcmp(argv[1],"--bptree-indicative")==0;
    if(argc>2 || (argc==2 && !indicative)) { fputs("usage: undo_bench [--bptree-indicative]\n",stderr); return 2; }
    (void)undo_bench_len; (void)undo_bench_insert; (void)undo_bench_delete; (void)undo_bench_ref;
    enum { STEPS=10000, RUNS=31 }; uint64_t a[RUNS],b[RUNS]; bench_samples real,mock;
    bench_samples_init(&real,a,RUNS); bench_samples_init(&mock,b,RUNS);
    char power[32]="unknown"; FILE *power_file=fopen("/sys/class/power_supply/BAT0/status","r");
    if(power_file) { if(fgets(power,sizeof power,power_file)) power[strcspn(power,"\r\n")]=0; fclose(power_file); }
    printf("POWER status=%s %s; concurrent-worker measurements indicative (M)\n",power,bench_evidence_tag());
    undo_state state={{0}}; undo_change c;
    for(int run=0;run<RUNS;run++) {
        piece_allocator pa=piece_default_allocator(); piece_tree *t=piece_create(&pa); CHECK(t); undo_log u;
        CHECK(undo_init(&u,t,STEPS)==0);
        for(size_t i=0;i<STEPS;i++) CHECK(undo_insert(&u,0,(const uint8_t *)"x",1,(uint64_t)i*(UNDO_BURST_NS+1),&state,&state)==0);
        undo_stats stats=undo_get_stats(&u);
        CHECK(stats.live_bytes<=64*stats.records && stats.live_bytes==64*STEPS);
        if(run==0) printf("MEMORY (M)%s live=%zu records=%zu bytes/record=%zu reserved=%zu fixed_scratch=512 gate=(G)64 B/record\n",bench_evidence_tag(),stats.live_bytes,stats.records,stats.live_bytes/stats.records,stats.reserved_bytes);
        uint64_t begin=bench_now_ns(); CHECK(undo_undo(&u,STEPS,&c)==0); (void)bench_add(&real,bench_now_ns()-begin);
        CHECK(c.groups==STEPS && piece_len(t)==0); undo_destroy(&u);piece_destroy(t);
        undo_bench_tree mt={0}; CHECK(undo_bench_mock_init(&u,(piece_tree *)&mt,STEPS)==0);
        for(size_t i=0;i<STEPS;i++) CHECK(undo_bench_mock_insert(&u,0,(const uint8_t *)"x",1,(uint64_t)i*(UNDO_BURST_NS+1),&state,&state)==0);
        begin=bench_now_ns(); CHECK(undo_bench_mock_undo(&u,STEPS,&c)==0); (void)bench_add(&mock,bench_now_ns()-begin);
        CHECK(c.groups==STEPS && mt.len==0); undo_bench_mock_destroy(&u);
    }
    puts("G9 context: (G) request-to-frame p50<=63000000 ns p99<=84000000 ns; module excludes render.");
    puts(indicative?"B+ tree variant: (M) indicative; coordinator reruns after synthesis on a quiet box.":"In-tree reference kernel: TRACK; coordinator reruns after synthesis on a quiet box.");
    (void)bench_report(indicative?"undo_10k_bptree_INDICATIVE_M":"undo_10k_in_tree_TRACK",&real,0,0);
    puts("Own-cost gate (G): 10% x G9 63 ms = 6.3 ms / 10000 groups.");
    return bench_report("undo_10k_bookkeeping_mock_G",&mock,6300000,6300000);
}
