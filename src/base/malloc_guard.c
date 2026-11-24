#include "base/base.h"

#ifdef BASE_MALLOC_GUARD_RELEASE
#include <stdatomic.h>

/* glibc entry points bypass the interposers without a dlsym bootstrap. */
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);

/* SIZE_MAX encodes the closed window. An open window saturates one below it.
 * An increment and end's exchange have a single modification order even when
 * an allocator on another thread races with the owner closing the window. */
static _Atomic size_t guard_count = SIZE_MAX;

static void count_call(void)
{
    size_t count = atomic_load_explicit(&guard_count, memory_order_relaxed);
    if (count == SIZE_MAX) return;
    while (count < SIZE_MAX - 1 &&
           !atomic_compare_exchange_weak_explicit(&guard_count, &count, count + 1,
                                                  memory_order_relaxed, memory_order_relaxed)) {}
}

void *malloc(size_t n)
{
    count_call();
    return __libc_malloc(n);
}

void *calloc(size_t n, size_t m)
{
    count_call();
    return __libc_calloc(n, m);
}

void *realloc(void *p, size_t n)
{
    count_call();
    return __libc_realloc(p, n);
}

bool base_malloc_guard_active(void) { return true; }

void base_malloc_guard_begin(void)
{
    atomic_store_explicit(&guard_count, 0, memory_order_relaxed);
}

size_t base_malloc_guard_end(void)
{
    return atomic_exchange_explicit(&guard_count, SIZE_MAX, memory_order_relaxed);
}

#else
/* Sanitizer runtimes interpose the allocator themselves. */
bool base_malloc_guard_active(void) { return false; }
void base_malloc_guard_begin(void) {}
size_t base_malloc_guard_end(void) { return 0; }
#endif
