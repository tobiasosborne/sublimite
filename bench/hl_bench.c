/* Reuse the editor's actual G1 input->T4 and G3 tab->T5 endpoints while a
 * dedicated bulk worker repeatedly parses a snapshot. No paint integration.
 * Refresh must come from the caller's display measurement; no default rate.
 * The existing malloc guard is process-wide: its bench allocation count also
 * includes this worker. Timing rows remain useful; the release hl tests check
 * the allocation-free typing API separately. */
#define main hl_editor_bench_main
#include "editor_bench.c"
#undef main
#include "hl/hl.h"
#include "../tests/hl_fixture.h"

typedef struct hl_load {
    hl_worker worker;
    hl_request request;
    piece_snapshot *snapshot;
    TSRange range;
    uint64_t refresh;
    _Atomic uint64_t slices;
} hl_load;
static bool hl_load_stop(void *payload) { return work_should_stop(payload); }
static void hl_load_run(work_ctx *context)
{
    hl_load *load = context->arg;
    hl_step step = hl_parse_slice(&load->request,
        (hl_slice_control){.payload = context, .should_stop = hl_load_stop});
    atomic_fetch_add_explicit(&load->slices, 1, memory_order_relaxed);
    if (work_should_stop(context)) return;
    if (step == HL_RESULT) hl_result_published(&load->request);
    if (step == HL_DONE) {
        /* Missing base identity forces real parsing again instead of reusing
         * the unchanged root. One persistent lease supplies continuous load. */
        if (!hl_request_init(&load->request, &load->worker, load->snapshot, 1,
                            load->range, load->refresh, 0, NULL, 0)) return;
    }
    (void)work_continue(context);
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        puts("hl_bench: SKIP unknown refresh; pass --refresh-ns=MEASURED_INTERVAL");
        return 0;
    }
    const char prefix[] = "--refresh-ns=";
    if (argc != 2 || strncmp(argv[1], prefix, sizeof prefix - 1u)) return 2;
    char *end = NULL;
    errno = 0;
    unsigned long long refresh = strtoull(argv[1] + sizeof prefix - 1u, &end, 10);
    if (errno || end == argv[1] + sizeof prefix - 1u || *end || !refresh) return 2;
    trace_init();
    (void)trace_thread_register();
    /* Cache-crossing workload derived from two input buffers, with one unit
     * per ASCII byte. Fixture size is structural, not a production capacity. */
    uint8_t text[2u * sizeof ((hl_request *)0)->input];
    memset(text, 'a', sizeof text);
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator);
    if (!source || piece_init_copy(source, text, sizeof text) != PIECE_OK) {
        piece_destroy(source); return 1;
    }
    hl_load load = {.refresh = (uint64_t)refresh,
        .range = {{0, 0}, {0, (uint32_t)sizeof text}, 0, (uint32_t)sizeof text}};
    load.snapshot = piece_snapshot_take(source);
    if (!load.snapshot || !hl_worker_init(&load.worker, hl_fixture_language())) {
        piece_snapshot_release(load.snapshot); piece_destroy(source); return 1;
    }
    work_pool pool;
    if (work_pool_init(&pool, 1, 0)) {
        hl_worker_destroy(&load.worker); piece_snapshot_release(load.snapshot);
        piece_destroy(source); return 1;
    }
    bool initialized = hl_request_init(&load.request, &load.worker, load.snapshot, 1,
                                      load.range, load.refresh, 0, NULL, 0);
    work_handle handle = {0};
    if (initialized) handle = work_submit(&pool, (work_job){hl_load_run, &load, 1, WORK_BULK});
    if (!handle.epoch) {
        work_pool_shutdown(&pool); hl_worker_destroy(&load.worker);
        piece_snapshot_release(load.snapshot); piece_destroy(source); return 1;
    }
    while (!atomic_load_explicit(&load.slices, memory_order_relaxed)) sched_yield();
    stamp tag = power_stamp();
    uint64_t before = atomic_load_explicit(&load.slices, memory_order_relaxed);
    /* Sample count follows the existing G1/G3 harness's p99 sample contract.
     * TRACK prints misses; setup/structural failures remain visible. */
    int type_null = typing_row("null", BENCH_INTERACTION_MIN_N, true, false, 0, 0, NULL, false);
    int tabs_null = tab_row(false, true);
    int type_raster = typing_row("raster", BENCH_INTERACTION_MIN_N, true, false, 0, 0, NULL, false);
    int tabs_raster = tab_row(true, true);
    uint64_t after = atomic_load_explicit(&load.slices, memory_order_relaxed);
    work_cancel(&pool, handle);
    work_pool_shutdown(&pool);
    hl_request_discard(&load.request);
    hl_worker_destroy(&load.worker);
    piece_snapshot_release(load.snapshot);
    piece_destroy(source);
    printf("hl_bench: (M)%s refresh_ns=%llu worker_slices=%llu G1_null=%d G3_null=%d G1_raster=%d G3_raster=%d allocation_counter_scope=process\n",
        tag.power, refresh, (unsigned long long)(after - before),
        type_null, tabs_null, type_raster, tabs_raster);
    return type_null < 0 || tabs_null < 0 || type_raster < 0 || tabs_raster < 0 || after == before ? 1 : 0;
}
