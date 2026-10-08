/* Counting interposition for malloc/calloc/realloc, used by the no-malloc
 * law tests. Underneath we call glibc's exported __libc_* entry points, so
 * no dlsym bootstrap (and no recursion) is involved. Compiled out under
 * AddressSanitizer, which owns malloc itself. */
#include "base/base.h"

#include <stddef.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define EDIT_GUARD_ASAN 1
#else
#define EDIT_GUARD_ASAN 0
#endif

#if !EDIT_GUARD_ASAN && defined(__GLIBC__)

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);

static int guard_on;               /* set between begin/end */
static size_t guard_count;         /* calls counted while guard_on */

static void count_call(void) {
    if (__atomic_load_n(&guard_on, __ATOMIC_RELAXED))
        __atomic_fetch_add(&guard_count, 1, __ATOMIC_RELAXED);
}

void *malloc(size_t n) {
    count_call();
    return __libc_malloc(n);
}

void *calloc(size_t n, size_t m) {
    count_call();
    return __libc_calloc(n, m);
}

void *realloc(void *p, size_t n) {
    count_call();
    return __libc_realloc(p, n);
}

bool edit_malloc_guard_active(void) { return true; }

void edit_malloc_guard_begin(void) {
    __atomic_store_n(&guard_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&guard_on, 1, __ATOMIC_RELAXED);
}

size_t edit_malloc_guard_end(void) {
    __atomic_store_n(&guard_on, 0, __ATOMIC_RELAXED);
    return __atomic_load_n(&guard_count, __ATOMIC_RELAXED);
}

#else

bool edit_malloc_guard_active(void) { return false; }
void edit_malloc_guard_begin(void) {}
size_t edit_malloc_guard_end(void) { return 0; }
#endif
