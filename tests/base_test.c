#include "base/base.h"
#include <string.h>

static void test_version(void) {
    const char *v = edit_version();
    EDIT_ASSERT(v != NULL);
    EDIT_ASSERT(strlen(v) > 0);
    EDIT_ASSERT(EDIT_LIKELY(1 + 1 == 2));
    puts(v);
}

static void test_arena(void) {
    edit_arena a;
    EDIT_ASSERT(edit_arena_init(&a, 1 << 20) == 0);

    void *p1 = edit_arena_alloc(&a, 3, 1);
    EDIT_ASSERT(p1 != NULL);
    void *p2 = edit_arena_alloc(&a, 17, 64);
    EDIT_ASSERT(p2 != NULL);
    EDIT_ASSERT(((uintptr_t)p2 & 63) == 0);
    void *p3 = edit_arena_alloc(&a, 8, 4096);
    EDIT_ASSERT(p3 != NULL);
    EDIT_ASSERT(((uintptr_t)p3 & 4095) == 0);
    EDIT_ASSERT(edit_arena_alloc(&a, 8, 3) == NULL); /* non-pow2 align */

    edit_arena_mark_t m = edit_arena_mark(&a);
    void *q = edit_arena_alloc(&a, 1000, 16);
    EDIT_ASSERT(q != NULL);
    memset(q, 0xab, 1000);
    edit_arena_reset_to_mark(&a, m);
    void *q2 = edit_arena_alloc(&a, 1000, 16);
    EDIT_ASSERT(q2 == q); /* mark/reset rewinds exactly */

    edit_arena_reset(&a);
    EDIT_ASSERT(edit_arena_alloc(&a, 1, 1) == a.base);
    edit_arena_free(&a);

    /* exhaustion returns NULL, never aborts */
    EDIT_ASSERT(edit_arena_init(&a, 4096) == 0);
    EDIT_ASSERT(edit_arena_alloc(&a, 4000, 1) != NULL);
    EDIT_ASSERT(edit_arena_alloc(&a, 200, 1) == NULL);
    EDIT_ASSERT(edit_arena_alloc(&a, (size_t)-1, 1) == NULL);
    edit_arena_free(&a);
    puts("arena: ok");
}

static void test_pool(void) {
    edit_pool p;
    EDIT_ASSERT(edit_pool_init(&p, 24, 16, 4) == 0);
    void *b[4];
    for (int i = 0; i < 4; i++) {
        b[i] = edit_pool_get(&p);
        EDIT_ASSERT(b[i] != NULL);
        EDIT_ASSERT(((uintptr_t)b[i] & 15) == 0);
    }
    EDIT_ASSERT(edit_pool_get(&p) == NULL); /* full */
    EDIT_ASSERT(edit_pool_count_live(&p) == 4);
    EDIT_ASSERT(!edit_pool_put(&p, (char *)b[0] + 1)); /* misaligned */
    EDIT_ASSERT(edit_pool_put(&p, b[2]));
    EDIT_ASSERT(edit_pool_count_live(&p) == 3);
    EDIT_ASSERT(edit_pool_get(&p) == b[2]); /* LIFO reuse */
    edit_pool_free(&p);

    /* 10 000 get/put ops with no libc malloc inside the guard. */
    EDIT_ASSERT(edit_pool_init(&p, 64, 64, 256) == 0);
    void *held[256];
    size_t n = 0;
    edit_malloc_guard_begin();
    for (int i = 0; i < 10000; i++) {
        if (n < 256 && ((i & 3) != 3 || n == 0)) {
            void *x = edit_pool_get(&p);
            EDIT_ASSERT(x != NULL);
            held[n++] = x;
        } else {
            EDIT_ASSERT(edit_pool_put(&p, held[--n]));
        }
    }
    size_t mallocs = edit_malloc_guard_end();
    while (n) EDIT_ASSERT(edit_pool_put(&p, held[--n]));
    EDIT_ASSERT(edit_pool_count_live(&p) == 0);
    edit_pool_free(&p);

    if (edit_malloc_guard_active()) {
        printf("malloc guard: %zu libc malloc calls in 10000 pool ops\n",
               mallocs);
        EDIT_ASSERT(mallocs == 0);
    } else {
        puts("malloc guard: SKIP (AddressSanitizer owns malloc)");
    }
    puts("pool: ok");
}

static void test_cpu(void) {
    printf("cpu: avx2=%d popcnt=%d\n", (int)edit_cpu_has_avx2(),
           (int)edit_cpu_has_popcnt());
    EDIT_ASSERT(edit_cpu_has_avx2() == edit_cpu_has_avx2());
}

int main(void) {
    test_version();
    test_arena();
    test_pool();
    test_cpu();
    puts("base_test: PASS");
    return 0;
}
