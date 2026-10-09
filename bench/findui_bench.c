#include "findui/findui.h"
#include "../bench/harness.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SAMPLES 64u

typedef struct observation {
    pthread_t caller;
    _Atomic uint32_t entered;
    atomic_uint on_caller;
} observation;
static void scan_hook(void *context, uint32_t generation)
{
    observation *seen = context;
    if (pthread_equal(pthread_self(), seen->caller)) atomic_fetch_add(&seen->on_caller, 1);
    atomic_store_explicit(&seen->entered, generation, memory_order_release);
}
static void *allocate(void *context, size_t size)
{ return edit_arena_alloc(context, size, 16); }
static void release(void *context, void *pointer, size_t size)
{ (void)context; (void)pointer; (void)size; }
static void route(const work_msg *message, void *context)
{ (void)findui_accept(context, message); }
static void pause_worker(void)
{ const struct timespec time = {0, 100000}; (void)nanosleep(&time, NULL); }
static bool stamp(char *power, size_t capacity, char *load, size_t load_capacity)
{
    FILE *file = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!file || !fgets(power, (int)capacity, file)) { if (file) (void)fclose(file); return false; }
    (void)fclose(file); power[strcspn(power, "\n")] = 0;
    file = fopen("/proc/loadavg", "r");
    if (!file || !fgets(load, (int)load_capacity, file)) { if (file) (void)fclose(file); return false; }
    (void)fclose(file); load[strcspn(load, " ")] = 0; return true;
}
static work_handle handle_for(const work_pool *pool, uint32_t generation)
{
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        if (atomic_load(&pool->slots[i].busy) && pool->slots[i].job.generation == generation)
            return (work_handle){i, atomic_load(&pool->slots[i].epoch)};
    return (work_handle){0, 0};
}
int main(int argc, char **argv)
{
    bool gate_mode = argc == 2 && strcmp(argv[1], "--gate") == 0;
    if (argc > 2 || (argc == 2 && !gate_mode && strcmp(argv[1], "--track") != 0)) return 2;
    char power[64], load[64];
    if (!stamp(power, sizeof power, load, sizeof load)) { fputs("findui_bench: power/load unavailable\n", stderr); return 1; }
    bool ac = strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0 || strcmp(power, "Not charging") == 0;
    const char *tag = ac ? "AC" : "bat";
    printf("findui_bench: power=%s [%s] load1=%s fixture=/tmp/edit-corpus/log_1g.txt TRACK\n", power, tag, load);
    int fd = open("/tmp/edit-corpus/log_1g.txt", O_RDONLY);
    struct stat metadata;
    if (fd < 0 || fstat(fd, &metadata) || metadata.st_size != (off_t)(UINT64_C(1024) * 1024u * 1024u)) {
        if (fd >= 0) (void)close(fd);
        fputs("findui_bench: missing/wrong log_1g.txt\n", stderr); return 1;
    }
    size_t length = (size_t)metadata.st_size;
    void *mapping = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    (void)close(fd);
    if (mapping == MAP_FAILED) return 1;
    edit_arena arena;
    if (edit_arena_init(&arena, 8u * 1024u * 1024u)) { (void)munmap(mapping, length); return 1; }
    piece_allocator allocator = {&arena, allocate, release}; piece_tree *tree = piece_create(&allocator);
    work_pool *pool = edit_arena_alloc(&arena, sizeof *pool, _Alignof(work_pool));
    int result = 1; bool pool_live = false;
    findui_panel panel = {0}; observation seen;
    seen.caller = pthread_self(); atomic_init(&seen.entered, 0); atomic_init(&seen.on_caller, 0);
    if (!tree || !pool || piece_init_mapped(tree, mapping, length, NULL) || work_pool_init(pool, 1, 0)) goto done;
    pool_live = true;
    findui_config config = {&arena, pool, 2048, 1024, scan_hook, &seen};
    if (findui_init(&panel, &config) != FINDUI_OK) goto done;
    piece_snapshot *snapshot = piece_snapshot_take(tree);
    if (!snapshot) goto done;
    findui_code code = findui_set_source(&panel, snapshot, 1); piece_snapshot_release(snapshot);
    if (code != FINDUI_OK || findui_set_window(&panel, 0, 4096) != FINDUI_OK ||
        findui_show(&panel, true, false) != FINDUI_OK ||
        findui_set_options(&panel, (findui_options){false, true, false}) != FINDUI_OK) goto done;
    const uint8_t needle[] = "__findui_absent_QZvx739__";
    if (findui_set_query(&panel, needle, sizeof needle - 1) != FINDUI_OK) goto done;
    uint64_t values[SAMPLES]; bench_samples samples;
    bench_samples_init(&samples, values, SAMPLES);
    for (size_t i = 0; i < SAMPLES; i++) {
        findui_state old = findui_get_state(&panel);
        uint64_t deadline = bench_now_ns() + UINT64_C(10000000000);
        /* Start every edit while its predecessor is actually scanning the
         * mapped corpus, rather than measuring cancellation of idle jobs. */
        while (atomic_load_explicit(&seen.entered, memory_order_acquire) != old.generation) {
            if (bench_now_ns() >= deadline || findui_service(&panel) > FINDUI_MORE) goto done;
            (void)work_mailbox_drain(pool, route, &panel); pause_worker();
        }
        work_handle old_handle = handle_for(pool, old.generation);
        if (!old_handle.epoch) { fputs("findui_bench: predecessor was not running\n", stderr); goto done; }
        uint64_t start = bench_now_ns();
        code = i % 2u ? findui_edit_query(&panel, old.query_length - 1, 1, NULL, 0)
                      : findui_edit_query(&panel, old.query_length, 0, (const uint8_t *)"x", 1);
        uint64_t elapsed = bench_now_ns() - start;
        findui_state current = findui_get_state(&panel);
        if (code != FINDUI_OK || current.generation != old.generation + 1 ||
            current.cancel_requests != old.cancel_requests + 1 ||
            atomic_load(&pool->slots[old_handle.slot].epoch) == old_handle.epoch) goto done;
        if (bench_add(&samples, elapsed)) goto done;
        (void)work_mailbox_drain(pool, route, &panel);
    }
    uint64_t p50 = bench_p50(&samples), p99 = bench_p99(&samples);
    bool met = p50 <= UINT64_C(1000000) && p99 <= UINT64_C(5000000);
    printf("G6c_findui_keystroke logical_ack (M)[%s] load1=%s n=%zu p50_ms=%.6f p99_ms=%.6f (G)1/5ms comparison=%s verdict=TRACK\n",
           tag, load, samples.n, (double)p50 / 1e6, (double)p99 / 1e6, met ? "within" : "over");
    printf("findui_scan_thread_check (M)[%s] load1=%s calling_thread_scans=%u expected=(G)0\n",
           tag, load, atomic_load(&seen.on_caller));
    result = atomic_load(&seen.on_caller) || (gate_mode && !met) ? 1 : 0;
 done:
    if (panel.private_) {
        while (findui_dispose(&panel) == FINDUI_MORE) {
            (void)work_mailbox_drain(pool, route, &panel); pause_worker();
        }
    }
    if (pool_live) {
        work_pool_shutdown(pool); (void)work_mailbox_drain_bounded(pool, route, &panel, SIZE_MAX, 0);
    }
    if (tree) piece_destroy(tree);
    edit_arena_free(&arena); (void)munmap(mapping, length);
    return result;
}
