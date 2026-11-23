#include "base/base.h"
#include "os/os.h"

typedef struct cpu_probe {
    pthread_barrier_t *start;
    bool avx2;
    bool popcnt;
} cpu_probe;

static void probe_cpu(void *arg)
{
    cpu_probe *probe = arg;
    int rc = pthread_barrier_wait(probe->start);
    BASE_ASSERT(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    probe->avx2 = base_cpu_has_avx2();
    probe->popcnt = base_cpu_has_popcnt();
}

static void test_cpu(void)
{
    /* (P) Two unordered first callers are enough to expose a shared cache race. */
    pthread_barrier_t start;
    BASE_ASSERT(pthread_barrier_init(&start, NULL, 2) == 0);
    cpu_probe left = {.start = &start}, right = {.start = &start};
    os_thread a, b;
    BASE_ASSERT(os_thread_start(&a, probe_cpu, &left) == OS_OK);
    BASE_ASSERT(os_thread_start(&b, probe_cpu, &right) == OS_OK);
    BASE_ASSERT(os_thread_join(&a) == OS_OK);
    BASE_ASSERT(os_thread_join(&b) == OS_OK);
    BASE_ASSERT(pthread_barrier_destroy(&start) == 0);

    BASE_ASSERT(left.avx2 == right.avx2 && left.avx2 == base_cpu_has_avx2());
    BASE_ASSERT(left.popcnt == right.popcnt && left.popcnt == base_cpu_has_popcnt());
    printf("cpu: avx2=%d popcnt=%d\n", (int)left.avx2, (int)left.popcnt);
}

static void test_version(void)
{
    const char *version = base_version();
    BASE_ASSERT(version && *version);
    puts(version);
}

static void test_arena(void)
{
    const size_t page = os_page_size();
    base_arena arena;
    BASE_ASSERT(base_arena_init(&arena, 0) == -1);
    BASE_ASSERT(base_arena_init(&arena, SIZE_MAX) == -1);
    /* An odd capacity exposes accidental admission into rounded page padding.
     * Two aligned pages let a request advance to a two-page boundary. */
    size_t capacity = 2 * page + 1;
    BASE_ASSERT(base_arena_init(&arena, capacity) == 0);
    BASE_ASSERT(base_arena_alloc(&arena, 1, 1) == arena.base);
    BASE_ASSERT(base_arena_alloc(&arena, 1, 3) == NULL); /* non-power-of-two */
    base_arena_mark_t mark = base_arena_mark(&arena);
    unsigned char *p = base_arena_alloc(&arena, 1, 2 * page);
    BASE_ASSERT(p && (uintptr_t)p % (2 * page) == 0);
    *p = 0xab;
    base_arena_reset_to_mark(&arena, mark);
    BASE_ASSERT(base_arena_alloc(&arena, 1, 2 * page) == p);
    BASE_ASSERT(*p == 0xab);
    base_arena_reset(&arena);
    base_arena_reset_to_mark(&arena, mark);
    BASE_ASSERT(base_arena_mark(&arena) == 0);
    BASE_ASSERT(base_arena_alloc(&arena, SIZE_MAX, 1) == NULL);
    BASE_ASSERT(base_arena_alloc(&arena, capacity, 1) == arena.base);
    BASE_ASSERT(base_arena_alloc(&arena, 1, 1) == NULL);
    base_arena_free(&arena);
    puts("arena: ok");
}

static void test_pool(void)
{
    const size_t alignment = _Alignof(max_align_t);
    base_pool pool;
    BASE_ASSERT(base_pool_init(&pool, 1, 1, 0) == -1);
    BASE_ASSERT(base_pool_init(&pool, SIZE_MAX, alignment, 1) == -1);
    BASE_ASSERT(base_pool_init(&pool, sizeof(void *), 1, SIZE_MAX) == -1);

    /* Three slots exercise a foreign pointer, an unissued slot and reuse
     * of more than one freed slot in LIFO order. */
    BASE_ASSERT(base_pool_init(&pool, alignment + 1, alignment, 3) == 0);
    unsigned char *a = base_pool_get(&pool);
    BASE_ASSERT(a && (uintptr_t)a % alignment == 0);
    BASE_ASSERT(!base_pool_put(&pool, &pool));
    BASE_ASSERT(!base_pool_put(&pool, a + 1));
    BASE_ASSERT(!base_pool_put(&pool, a + 2 * alignment));
    void *b = base_pool_get(&pool), *c = base_pool_get(&pool);
    BASE_ASSERT(b && c && a != b && b != c && a != c);
    BASE_ASSERT(base_pool_get(&pool) == NULL);
    BASE_ASSERT(base_pool_count_live(&pool) == 3);
    BASE_ASSERT(base_pool_put(&pool, a) && base_pool_put(&pool, c));
    BASE_ASSERT(base_pool_count_live(&pool) == 1);
    BASE_ASSERT(base_pool_get(&pool) == c);
    BASE_ASSERT(base_pool_get(&pool) == a);
    base_pool_free(&pool);

    /* A one-byte payload still needs room and alignment for a free link. */
    BASE_ASSERT(base_pool_init(&pool, 1, 1, 1) == 0);
    a = base_pool_get(&pool);
    BASE_ASSERT(a && (uintptr_t)a % _Alignof(void *) == 0);
    *a = 0x5a;
    BASE_ASSERT(base_pool_put(&pool, a));
    BASE_ASSERT(base_pool_get(&pool) == a);
    base_pool_free(&pool);
    puts("pool: ok");
}

int main(void)
{
    test_cpu();
    test_version();
    test_arena();
    test_pool();
    puts("base_test: PASS");
    return 0;
}
