/* trace_bench.c - cost of trace_record(). Gate: p50 < 50 ns (G), see docs/decisions/P0.3.md.
 * 10^6 calls in 1000 batches of 1000; each batch is timed with CLOCK_MONOTONIC
 * and divided by 1000, so the timer read is amortised out. Also reports the
 * cost of a bare clock_gettime for context. Exit 1 on gate miss. */
#include "../src/trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BATCHES 1000u
#define BATCH 1000u
#define GATE_P50_NS 50u

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static uint64_t pct(uint64_t *v, size_t n, unsigned p) {
    size_t rank = (n * p + 99u) / 100u;
    if (rank < 1) rank = 1;
    return v[rank - 1];
}

int main(void) {
    static uint64_t per_call[BATCHES];
    static uint64_t clk[BATCHES];
    struct timespec t0, t1;
    int rc = 0;

    trace_init();
    if (trace_thread_register() < 0) {
        fprintf(stderr, "no ring\n");
        return 1;
    }

    for (unsigned b = 0; b < BATCHES; b++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (unsigned i = 0; i < BATCH; i++) {
            trace_record(TRACE_T2_MUTATION_DONE, b * BATCH + i);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000u + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        per_call[b] = ns / BATCH;
    }
    for (unsigned b = 0; b < BATCHES; b++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (unsigned i = 0; i < BATCH; i++) {
            struct timespec s;
            clock_gettime(CLOCK_MONOTONIC, &s);
            __asm__ __volatile__("" : : "r"(&s) : "memory");
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000u + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        clk[b] = ns / BATCH;
    }

    /* Input-event append (P0.6): mixed key/pointer/resize appends, same method. */
    static uint64_t in_call[BATCHES];
    for (unsigned b = 0; b < BATCHES; b++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (unsigned i = 0; i < BATCH; i++) {
            switch (i & 3u) {
            case 0: trace_input_key((uint64_t)i, TRACE_IN_KEY_DOWN, 0x61u, 0x1u, 0, "a", 1); break;
            case 1: trace_input_pointer((uint64_t)i, TRACE_IN_POINTER_MOVE, (int32_t)i, 5, 0, 0); break;
            case 2: trace_input_wheel((uint64_t)i, 0, 65536, 0); break;
            default: trace_input_resize((uint64_t)i, i, i); break;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000u + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        in_call[b] = ns / BATCH;
    }
    qsort(in_call, BATCHES, sizeof in_call[0], cmp_u64);
    qsort(per_call, BATCHES, sizeof per_call[0], cmp_u64);
    qsort(clk, BATCHES, sizeof clk[0], cmp_u64);
    {
        uint64_t p50 = pct(per_call, BATCHES, 50), p99 = pct(per_call, BATCHES, 99);
        printf("trace_record   n=10^6 batches=%u x %u: p50=%llu ns p99=%llu ns (gate p50 < %u)\n",
               BATCHES, BATCH, (unsigned long long)p50, (unsigned long long)p99, GATE_P50_NS);
        printf("clock_gettime  (reference, same method): p50=%llu ns p99=%llu ns\n",
               (unsigned long long)pct(clk, BATCHES, 50), (unsigned long long)pct(clk, BATCHES, 99));
        uint64_t ip50 = pct(in_call, BATCHES, 50), ip99 = pct(in_call, BATCHES, 99);
        char bat[32] = "n/a";
        FILE *bf = fopen("/sys/class/power_supply/BAT0/status", "r");
        if (bf != NULL) {
            if (fgets(bat, sizeof bat, bf) == NULL) strcpy(bat, "n/a");
            bat[strcspn(bat, "\n")] = '\0';
            fclose(bf);
        }
        printf("trace_input    n=10^6 batches=%u x %u: p50=%llu ns p99=%llu ns (gate p50 < %u) (M)[%s]\n",
               BATCHES, BATCH, (unsigned long long)ip50, (unsigned long long)ip99, GATE_P50_NS, bat);
        if (ip50 >= GATE_P50_NS) {
            printf("trace_bench: INPUT GATE MISS\n");
            rc = 1;
        }
        if (p50 >= GATE_P50_NS) {
            printf("trace_bench: GATE MISS\n");
            rc = 1;
        } else {
            printf("trace_bench: PASS\n");
        }
    }
    return rc;
}
