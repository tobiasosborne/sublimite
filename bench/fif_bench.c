#include "fif/fif.h"
#include "../tests/tree_fixture.h"
#include "harness.h"
#include <sched.h>
#include <sys/stat.h>
/* (E) 100 = 1/(1-0.99): descriptive nearest-rank p99 has one tail sample.
 * This is a tracked bench, not the harness's qualified interaction gate. */
#define FIF_BENCH_SAMPLES 100u
/* (E) Aspirations scale the existing G6 A 80/125 ms per decimal GB. The
 * directory/open/read costs are unmodelled, explicitly tracked, never gated. */
#define FIF_BENCH_G6_P50_NS UINT64_C(80000000)
#define FIF_BENCH_G6_P99_NS UINT64_C(125000000)
#define FIF_BENCH_DECIMAL_GB UINT64_C(1000000000)
typedef struct observation { fif_done done; bool received; } observation;
static void receive(const work_msg *msg, void *arg)
{
    observation *seen = arg;
    if (msg->kind == FIF_MSG_DONE) { memcpy(&seen->done, msg->data, sizeof seen->done); seen->received = true; }
}
int main(int argc, char **argv)
{
    char power[sizeof "Not charging"], load[PATH_MAX];
    bench_battery_status(power, sizeof power);
    FILE *load_file = fopen("/proc/loadavg", "r");
    if (!load_file || !fgets(load, sizeof load, load_file)) strcpy(load, "unknown");
    if (load_file) (void)fclose(load_file);
    load[strcspn(load, " \n")] = 0;
    struct stat st; tree_fixture tree = {{0}};
    const char *root = "/tmp/edit-corpus";
    if (argc > 1) root = argv[1];
    if (stat(root, &st) || !S_ISDIR(st.st_mode)) {
        if (tree_fixture_init(&tree)) { puts("fif_bench: fixture unavailable (tracked)"); return 0; }
        root = tree.root;
    }
    printf("fif_bench: (M)%s power=%s load1=%s fixture=%s\n", bench__tag_from_power(power), power, load, root);
    edit_arena arena;
    if (edit_arena_init(&arena, sizeof(work_pool) + _Alignof(work_pool))) goto cleanup;
    work_pool *pool = edit_arena_alloc(&arena, sizeof *pool, _Alignof(work_pool));
    if (!pool || work_pool_init(pool, 1, 0)) { edit_arena_free(&arena); goto cleanup; }
    uint64_t values[FIF_BENCH_SAMPLES]; bench_samples samples;
    bench_samples_init(&samples, values, FIF_BENCH_SAMPLES);
    fif_done last = {0};
    for (size_t i = 0; i < FIF_BENCH_SAMPLES; i++) {
        const uint8_t query[] = "__fif_absent__";
        fif_request request = {.root = root, .query = query, .query_length = sizeof query - 1u,
                               .options = {.match_case = true}};
        fif_job job = {0};
        if (fif_job_init(&job, &request) != FIF_OK) break;
        observation seen = {0}; uint64_t begin = bench_now_ns();
        if (!fif_submit(pool, &job, 0).epoch) { (void)fif_job_dispose(&job); break; }
        while (!fif_job_finished(&job) || work_mailbox_pending(pool)) {
            (void)work_mailbox_drain(pool, receive, &seen); sched_yield();
        }
        if (seen.received) { (void)bench_add(&samples, bench_now_ns() - begin); last = seen.done; }
        (void)fif_job_dispose(&job);
        if (!seen.received || seen.done.code != FIF_OK) break;
    }
    uint64_t aspire50 = (uint64_t)((long double)last.bytes * FIF_BENCH_G6_P50_NS / FIF_BENCH_DECIMAL_GB);
    uint64_t aspire99 = (uint64_t)((long double)last.bytes * FIF_BENCH_G6_P99_NS / FIF_BENCH_DECIMAL_GB);
    printf("fif_bench: bytes=%llu files=%u truncated=%d errors=%u code=%d targets=(E)G6_scaled_by_read_bytes\n",
           (unsigned long long)last.bytes, last.files, last.truncated, last.errors, last.code);
    (void)bench_report("G6f_walk_search_tracked", &samples, aspire50, aspire99);
    work_pool_shutdown(pool); edit_arena_free(&arena);
cleanup:
    if (tree.root[0]) (void)tree_fixture_remove(tree.root);
    return 0;
}
