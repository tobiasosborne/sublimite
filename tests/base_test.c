#include "base/base.h"
#include "os/os.h"
#include "assert_death.h"

#include <stdio.h>

typedef struct cpu_probe {
    pthread_barrier_t *start;
    bool avx2;
    bool popcnt;
    bool sse42;
} cpu_probe;

static void probe_cpu(void *arg)
{
    cpu_probe *probe = arg;
    int rc = pthread_barrier_wait(probe->start);
    ASSERT(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    probe->avx2 = base_cpu_has_avx2();
    probe->popcnt = base_cpu_has_popcnt();
    probe->sse42 = base_cpu_has_sse42();
}

static void test_cpu(void)
{
    /* (P) Two unordered first callers are enough to expose a shared cache race. */
    pthread_barrier_t start;
    ASSERT(pthread_barrier_init(&start, NULL, 2) == 0);
    cpu_probe left = {.start = &start}, right = {.start = &start};
    os_thread a, b;
    ASSERT(os_thread_start(&a, probe_cpu, &left) == OS_OK);
    ASSERT(os_thread_start(&b, probe_cpu, &right) == OS_OK);
    ASSERT(os_thread_join(&a) == OS_OK);
    ASSERT(os_thread_join(&b) == OS_OK);
    ASSERT(pthread_barrier_destroy(&start) == 0);

    ASSERT(left.avx2 == right.avx2 && left.avx2 == base_cpu_has_avx2());
    ASSERT(left.popcnt == right.popcnt && left.popcnt == base_cpu_has_popcnt());
    ASSERT(left.sse42 == right.sse42 && left.sse42 == base_cpu_has_sse42());
    /* The predicate reports the compiler runtime's own answer verbatim. */
    __builtin_cpu_init();
    ASSERT(left.sse42 == (__builtin_cpu_supports("sse4.2") != 0));
    printf("cpu: avx2=%d popcnt=%d sse42=%d\n", (int)left.avx2, (int)left.popcnt,
           (int)left.sse42);
}

static void test_version(void)
{
    const char *version = base_version();
    ASSERT(version && *version);
    puts(version);
}

static void test_arena(void)
{
    const size_t page = os_page_size();
    base_arena arena;
    ASSERT(base_arena_init(&arena, SIZE_MAX) == -1);
    /* An odd capacity exposes accidental admission into rounded page padding.
     * Two aligned pages let a request advance to a two-page boundary. */
    size_t capacity = 2 * page + 1;
    ASSERT(base_arena_init(&arena, capacity) == 0);
    ASSERT(base_arena_alloc(&arena, 1, 1) == arena.base);
    base_arena_mark_t mark = base_arena_mark(&arena);
    unsigned char *p = base_arena_alloc(&arena, 1, 2 * page);
    ASSERT(p && (uintptr_t)p % (2 * page) == 0);
    *p = 0xab;
    base_arena_reset_to_mark(&arena, mark);
    ASSERT(base_arena_alloc(&arena, 1, 2 * page) == p);
    ASSERT(*p == 0xab);
    base_arena_reset(&arena);
    ASSERT(base_arena_mark(&arena) == 0);
    ASSERT(base_arena_alloc(&arena, SIZE_MAX, 1) == NULL);
    ASSERT(base_arena_alloc(&arena, capacity, 1) == arena.base);
    ASSERT(base_arena_alloc(&arena, 1, 1) == NULL);
    base_arena_free(&arena);
    puts("arena: ok");
}

static void test_pool(void)
{
    const size_t alignment = _Alignof(max_align_t);
    base_pool pool;
    ASSERT(base_pool_init(&pool, SIZE_MAX, alignment, 1) == -1);
    ASSERT(base_pool_init(&pool, sizeof(void *), 1, SIZE_MAX) == -1);

    /* Three slots exercise reuse of more than one freed slot in LIFO order. */
    ASSERT(base_pool_init(&pool, alignment + 1, alignment, 3) == 0);
    unsigned char *a = base_pool_get(&pool);
    ASSERT(a && (uintptr_t)a % alignment == 0);
    void *b = base_pool_get(&pool), *c = base_pool_get(&pool);
    ASSERT(b && c && a != b && b != c && a != c);
    ASSERT(base_pool_get(&pool) == NULL);
    ASSERT(base_pool_count_live(&pool) == 3);
    base_pool_put(&pool, a);
    base_pool_put(&pool, c);
    ASSERT(base_pool_count_live(&pool) == 1);
    ASSERT(base_pool_get(&pool) == c);
    ASSERT(base_pool_get(&pool) == a);
    base_pool_free(&pool);

    /* A one-byte payload still needs room and alignment for a free link. */
    ASSERT(base_pool_init(&pool, 1, 1, 1) == 0);
    a = base_pool_get(&pool);
    ASSERT(a && (uintptr_t)a % _Alignof(void *) == 0);
    *a = 0x5a;
    base_pool_put(&pool, a);
    ASSERT(base_pool_get(&pool) == a);
    base_pool_free(&pool);

    /* Reset returns every block at once and keeps the reservation: the
     * pool hands out blocks from the first again, as after init. */
    ASSERT(base_pool_init(&pool, alignment + 1, alignment, 3) == 0);
    unsigned char *first = base_pool_get(&pool), *reservation = pool.base;
    unsigned char *second = base_pool_get(&pool);
    base_pool_put(&pool, first); /* a free-list entry must not survive reset */
    base_pool_reset(&pool);
    ASSERT(base_pool_count_live(&pool) == 0 && pool.base == reservation);
    ASSERT(base_pool_get(&pool) == first && base_pool_get(&pool) == second);
    ASSERT(base_pool_get(&pool) != NULL && base_pool_get(&pool) == NULL);
    ASSERT(base_pool_count_live(&pool) == 3);
    base_pool_free(&pool);
    puts("pool: ok");
}

/* Returning a pointer the pool never issued is a caller bug (A15). */
static void put_foreign(void)
{
    base_pool pool;
    if (base_pool_init(&pool, 1, 1, 1) != 0) return;
    base_pool_put(&pool, &pool);
}

int main(void)
{
    /* Before any thread exists: the death child forks a one-thread process. */
    ASSERT(assert_death(put_foreign, "src/base/base.c",
                             "p->live != 0 && offset < p->fresh * p->stride && offset % p->stride == 0"));
    test_cpu();
    test_version();
    test_arena();
    test_pool();
    puts("base_test: PASS");
    return 0;
}
