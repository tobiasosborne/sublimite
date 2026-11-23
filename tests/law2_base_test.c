#include "base/base.h"
#include "os/os.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool test_guard_active(void)
{
    /* A volatile function pointer forces a real call at release optimization. */
    void *(*volatile allocate)(size_t) = malloc;
    base_malloc_guard_begin();
    void *p = allocate(1);
    size_t count = base_malloc_guard_end();
    bool allocated = p != NULL;
    free(p);

    bool active = base_malloc_guard_active();
    printf("guard positive control: active=%d count=%zu\n", (int)active, count);
    return active && allocated && count > 0;
}

static bool test_allocator_window(void)
{
    base_arena arena;
    base_pool pool;
    size_t page = os_page_size();
    const size_t alignment = _Alignof(max_align_t);
    /* One page for bump and rewind, two blocks for fresh and recycled slots. */
    ASSERT(base_arena_init(&arena, page) == 0);
    ASSERT(base_pool_init(&pool, alignment, alignment, 2) == 0);

    base_malloc_guard_begin();
    base_arena_mark_t mark = base_arena_mark(&arena);
    void *first = base_arena_alloc(&arena, 1, 1);
    void *aligned = base_arena_alloc(&arena, alignment, alignment);
    ASSERT(first != NULL && aligned != NULL);
    memset(aligned, 0xa5, alignment);
    base_arena_reset_to_mark(&arena, mark);
    ASSERT(base_arena_alloc(&arena, 1, 1) == first);
    base_arena_reset(&arena);
    ASSERT(base_arena_alloc(&arena, page, 1) == first);
    ASSERT(base_arena_alloc(&arena, 1, 1) == NULL);

    void *a = base_pool_get(&pool);
    void *b = base_pool_get(&pool);
    ASSERT(a != NULL && b != NULL && a != b);
    ASSERT(base_pool_get(&pool) == NULL);
    base_pool_put(&pool, a);
    ASSERT(base_pool_get(&pool) == a);
    base_pool_put(&pool, b);
    base_pool_put(&pool, a);
    ASSERT(base_pool_count_live(&pool) == 0);
    size_t count = base_malloc_guard_end();

    base_arena_free(&arena);
    base_pool_free(&pool);
    bool active = base_malloc_guard_active();
    printf("arena/pool Law-2 window: active=%d count=%zu\n", (int)active, count);
    return active && count == 0;
}

typedef struct guard_probe {
    bool exempt;
    _Atomic bool run;
    _Atomic bool done;
} guard_probe;

static void allocate_byte(void)
{
    void *(*volatile allocate)(size_t) = malloc;
    void *p = allocate(1);
    ASSERT(p != NULL);
    free(p);
}

static void probe_worker(void *arg)
{
    guard_probe *probe = arg;
    while (!atomic_load(&probe->run)) os_yield();
    if (probe->exempt) base_malloc_guard_exempt_begin();
    allocate_byte();
    if (probe->exempt) base_malloc_guard_exempt_end();
    atomic_store(&probe->done, true);
}

static size_t guard_worker_window(bool exempt)
{
    guard_probe probe = {.exempt = exempt};
    os_thread worker;
    ASSERT(os_thread_start(&worker, probe_worker, &probe) == OS_OK);

    base_malloc_guard_begin();
    base_malloc_guard_exempt_begin();
    base_malloc_guard_exempt_begin();
    allocate_byte();
    base_malloc_guard_exempt_end();
    allocate_byte();
    atomic_store(&probe.run, true);
    while (!atomic_load(&probe.done)) os_yield();
    base_malloc_guard_exempt_end();
    size_t count = base_malloc_guard_end();
    ASSERT(os_thread_join(&worker) == OS_OK);
    return count;
}

static bool test_guard_exempt(void)
{
    /* The calling thread remains exempt while the other thread allocates. */
    size_t exempt = guard_worker_window(true);
    size_t counted = guard_worker_window(false);

    base_malloc_guard_begin();
    allocate_byte();
    void *(*volatile allocate)(size_t, size_t) = calloc;
    void *(*volatile resize)(void *, size_t) = realloc;
    void *p = allocate(1, 1);
    ASSERT(p != NULL);
    /* A larger request exercises realloc separately from the calloc. */
    p = resize(p, 2);
    ASSERT(p != NULL);
    size_t after_close = base_malloc_guard_end();
    free(p);

    bool active = base_malloc_guard_active();
    printf("guard exemptions: active=%d worker_exempt=%zu worker_counted=%zu after_close=%zu\n",
           (int)active, exempt, counted, after_close);
    return active && exempt == 0 && counted == 1 && after_close == 3;
}

typedef struct os_probe {
    os_mutex mutex;
    os_rmutex recursive;
    os_cond cond;
    _Atomic unsigned ready;
    _Atomic bool stop;
} os_probe;

static void contend(void *arg)
{
    os_probe *probe = arg;
    atomic_fetch_add(&probe->ready, 1);
    while (!atomic_load(&probe->stop)) {
        os_mutex_lock(&probe->mutex);
        os_mutex_unlock(&probe->mutex);
        os_rmutex_lock(&probe->recursive);
        os_rmutex_unlock(&probe->recursive);
    }
}

static void wait_on_cond(void *arg)
{
    os_probe *probe = arg;
    os_mutex_lock(&probe->mutex);
    atomic_fetch_add(&probe->ready, 1);
    while (!atomic_load(&probe->stop))
        os_cond_wait(&probe->cond, &probe->mutex);
    os_mutex_unlock(&probe->mutex);
}

static void once_body(void) {}

static bool test_os_window(void)
{
    os_probe probe = {0};
    os_mutex cold_mutex;
    os_rmutex cold_recursive;
    os_cond cold_cond;
    os_once cold_once = OS_ONCE_INIT;
    os_wake wake;
    ASSERT(os_mutex_init(&probe.mutex) == OS_OK);
    ASSERT(os_rmutex_init(&probe.recursive) == OS_OK);
    ASSERT(os_cond_init(&probe.cond) == OS_OK);
    ASSERT(os_mutex_init(&cold_mutex) == OS_OK);
    ASSERT(os_rmutex_init(&cold_recursive) == OS_OK);
    ASSERT(os_cond_init(&cold_cond) == OS_OK);
    ASSERT(os_wake_init(&wake) == OS_OK);

    char path[] = "build/law2-os-XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0 && close(fd) == 0);
    os_file file = OS_FILE_INIT;
    ASSERT(os_file_open(&file, path, OS_OPEN_RDWR) == OS_OK);
    ASSERT(os_unlink(path) == OS_OK);

    os_thread contender, waiter;
    os_mutex_lock(&probe.mutex);
    ASSERT(os_thread_start(&contender, contend, &probe) == OS_OK);
    ASSERT(os_thread_start(&waiter, wait_on_cond, &probe) == OS_OK);
    /* Inherit the v1 probe's record width and loop count. Repeating the
     * operations exercises both cold and established contended paths. */
    char record[64] = "positioned Law-2 I/O", back[sizeof record];
    uint64_t clock_sum = 0;
    base_malloc_guard_begin();
    /* Release the workers inside the window, including the first cond wait. */
    os_mutex_unlock(&probe.mutex);
    while (atomic_load(&probe.ready) != 2) os_yield();
    os_mutex_lock(&cold_mutex);
    os_mutex_unlock(&cold_mutex);
    os_rmutex_lock(&cold_recursive);
    os_rmutex_lock(&cold_recursive);
    os_rmutex_unlock(&cold_recursive);
    os_rmutex_unlock(&cold_recursive);
    os_cond_signal(&cold_cond);
    os_cond_broadcast(&cold_cond);
    os_once_run(&cold_once, once_body);
    for (uint64_t i = 0; i < 1000; i++) {
        clock_sum += os_now_ns() + os_wall_ns() + os_thread_cpu_ns() +
                     os_slice_ns() + os_thread_self();
        os_mutex_lock(&probe.mutex);
        os_cond_signal(&probe.cond);
        os_mutex_unlock(&probe.mutex);
        os_cond_broadcast(&probe.cond);
        os_rmutex_lock(&probe.recursive);
        os_rmutex_lock(&probe.recursive);
        os_rmutex_unlock(&probe.recursive);
        os_rmutex_unlock(&probe.recursive);
        os_wake_signal(&wake);
        os_wake_drain(&wake);

        size_t n;
        uint64_t off = i * sizeof record;
        ASSERT(os_file_pwrite_once(&file, record, sizeof record, off, &n) == OS_OK);
        ASSERT(n == sizeof record);
        ASSERT(os_file_pwrite(&file, record, sizeof record, off, &n) == OS_OK);
        ASSERT(n == sizeof record);
        ASSERT(os_file_pread(&file, back, sizeof back, off, &n) == OS_OK);
        ASSERT(n == sizeof back && memcmp(record, back, n) == 0);
    }
    os_yield();
    size_t count = base_malloc_guard_end();

    os_mutex_lock(&probe.mutex);
    atomic_store(&probe.stop, true);
    os_cond_broadcast(&probe.cond);
    os_mutex_unlock(&probe.mutex);
    ASSERT(os_thread_join(&contender) == OS_OK);
    ASSERT(os_thread_join(&waiter) == OS_OK);
    ASSERT(clock_sum != 0);
    os_file_close(&file);
    os_wake_close(&wake);
    os_cond_destroy(&cold_cond);
    os_rmutex_destroy(&cold_recursive);
    os_mutex_destroy(&cold_mutex);
    os_cond_destroy(&probe.cond);
    os_rmutex_destroy(&probe.recursive);
    os_mutex_destroy(&probe.mutex);

    bool active = base_malloc_guard_active();
    printf("OS Law-2 window: active=%d count=%zu\n", (int)active, count);
    return active && count == 0;
}

int main(void)
{
    /* The v2 OS suite's inherited v1 hang guard also bounds the probes. */
    alarm(120);
    bool proven = test_guard_active();
    bool clean = test_allocator_window();
    if (!proven || !clean) {
        fputs("law2_base_test: FAIL (requires a live counting guard and a clean window)\n",
              stderr);
        return EXIT_FAILURE;
    }
    bool exempt = test_guard_exempt();
    bool os_clean = test_os_window();
    if (!exempt || !os_clean) return EXIT_FAILURE;
    puts("law2_base_test: PASS");
    return EXIT_SUCCESS;
}
