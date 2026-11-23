#include "base/base.h"
#include "os/os.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* (M) Sample count inherited from the v1 base benchmark. The percentile
 * indices below are ranks in the sorted samples, with times in ns. */
#define N_OPS 1000000u

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    base_pool pool;
    /* v1's 64-byte blocks represent one x86-64 cache line each. */
    ASSERT(base_pool_init(&pool, 64, 64, 64) == 0);
    uint64_t *samples = malloc(sizeof *samples * N_OPS);
    ASSERT(samples != NULL);

    for (unsigned i = 0; i < N_OPS; i++) {
        uint64_t start = os_now_ns();
        void *block = base_pool_get(&pool);
        ASSERT(block != NULL);
        base_pool_put(&pool, block);
        samples[i] = os_now_ns() - start;
    }
    qsort(samples, N_OPS, sizeof *samples, cmp_u64);
    printf("BENCH name=base_pool n=%u p50=%" PRIu64 " p99=%" PRIu64 " unit=ns\n",
           N_OPS, samples[N_OPS / 2 - 1], samples[N_OPS * 99 / 100 - 1]);
    free(samples);
    base_pool_free(&pool);
    return 0;
}
