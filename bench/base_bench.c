#include "base/base.h"
#include <time.h>

#define N_OPS 1000000u

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint64_t *samples;

int main(void) {
    edit_pool p;
    EDIT_ASSERT(edit_pool_init(&p, 64, 64, 64) == 0);
    samples = malloc(sizeof(uint64_t) * N_OPS); /* bench-only, not timed */
    EDIT_ASSERT(samples != NULL);

    volatile uintptr_t sink = 0;
    for (unsigned i = 0; i < N_OPS; i++) {
        uint64_t t0 = now_ns();
        void *b = edit_pool_get(&p);
        EDIT_ASSERT(b != NULL);
        sink ^= (uintptr_t)b;
        EDIT_ASSERT(edit_pool_put(&p, b));
        samples[i] = now_ns() - t0;
    }
    qsort(samples, N_OPS, sizeof samples[0], cmp_u64);
    uint64_t p50 = samples[N_OPS / 2];
    uint64_t p99 = samples[N_OPS * 99 / 100];
    printf("BENCH name=base_pool n=%u p50=%llu p99=%llu\n", N_OPS,
           (unsigned long long)p50, (unsigned long long)p99);
    free(samples);
    edit_pool_free(&p);
    return 0;
}
