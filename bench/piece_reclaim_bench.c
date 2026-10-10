/* P1-1 §13: back-to-back kernel TRACK row, including worker retirement.
 * G1 includes input/render, so this memory-only row reports no timing verdict. */
#include "piece/piece.h"
#include "harness.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct release_job {
    piece_snapshot *snapshot;
    atomic_uint *ready;
    atomic_int *go;
} release_job;
static void *release_worker(void *ctx) {
    release_job *j = ctx;
    atomic_fetch_add_explicit(j->ready, 1, memory_order_release);
    while (!atomic_load_explicit(j->go, memory_order_acquire)) sched_yield();
    piece_snapshot_release(j->snapshot);
    return NULL;
}
static void require(int ok) {
    if (!ok) { fputs("piece_reclaim_bench: fixture failed\n", stderr); exit(2); }
}
static void report(const char *col, unsigned workers, bench_samples *s) {
    printf("BENCH row=worker_reclamation_typing workers=%u col=%s p50=%.3f p99=%.3f unit=us n=%zu gate=none status=TRACK (M)%s\n",
           workers, col, (double)bench_p50(s) / 1000.0, (double)bench_p99(s) / 1000.0,
           s->n, bench_evidence_tag());
}
int main(int argc, char **argv) {
    int quick = argc == 2 && !strcmp(argv[1], "--quick");
    require(argc == 1 || quick);
    size_t pieces = quick ? 20000u : 200000u, repeats = quick ? 16u : 32u;
    char power[32]; bench_battery_status(power, sizeof power);
    printf("# worker_reclamation_typing pieces=%zu repeats=%zu power=%s (M)%s; G1=1/2ms (G), kernel samples TRACK\n",
           pieces, repeats, power, bench_evidence_tag());
    for (unsigned jobs = 1; jobs <= 3; jobs += 2) {
        uint64_t first_data[32], typing_data[32 * 64];
        bench_samples first, typing;
        bench_samples_init(&first, first_data, repeats);
        bench_samples_init(&typing, typing_data, repeats * 64);
        for (size_t r = 0; r < repeats; r++) {
            piece_allocator a = piece_default_allocator();
            piece_tree *t = piece_create(&a); require(t != NULL);
            for (size_t i = 0; i < pieces; i++) require(!piece_insert(t, 0, (const uint8_t *)"x", 1));
            require(piece_piece_count(t) == pieces);
            atomic_uint ready; atomic_init(&ready, 0);
            atomic_int go; atomic_init(&go, 0);
            release_job j[3]; pthread_t th[3];
            for (unsigned i = 0; i < jobs; i++) {
                j[i] = (release_job){piece_snapshot_take(t), &ready, &go};
                require(j[i].snapshot != NULL);
                require(!piece_insert(t, 0, (const uint8_t *)"+", 1));
            }
            require(!piece_delete(t, 0, piece_len(t), NULL));
            require(!piece_insert(t, 0, (const uint8_t *)"x", 1));
            for (unsigned i = 0; i < jobs; i++) require(!pthread_create(&th[i], NULL, release_worker, &j[i]));
            while (atomic_load_explicit(&ready, memory_order_acquire) != jobs) sched_yield();
            atomic_store_explicit(&go, 1, memory_order_release);
            for (unsigned k = 0; k < 64; k++) {
                uint64_t began = bench_now_ns();
                require(!piece_insert(t, 1, (const uint8_t *)"\n", 1));
                require(!piece_delete(t, 1, 1, NULL));
                uint64_t elapsed = bench_now_ns() - began;
                require(!bench_add(&typing, elapsed));
                if (!k) require(!bench_add(&first, elapsed));
            }
            for (unsigned i = 0; i < jobs; i++) require(!pthread_join(th[i], NULL));
            require(piece_len(t) == 1 && piece_line_count(t) == 1);
            piece_destroy(t);
        }
        report("first_edit", jobs, &first); report("insert_delete", jobs, &typing);
    }
    return 0;
}
