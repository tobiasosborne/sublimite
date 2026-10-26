/* Windows counting guard for the no-malloc law (edit-177r.3, W0.3). mingw's
 * UCRT has no __libc_* entry points to call underneath, so the release link
 * routes OUR calls through this file with lld's --wrap (mk/win.mk:
 * -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free): every
 * reference to malloc in our objects binds to __wrap_malloc, which counts and
 * forwards to __real_malloc, the CRT import. Win32 and DirectWrite allocate
 * through HeapAlloc and the CRT's own internal calls are inside ucrtbase.dll:
 * both stay outside the count, as docs/design/windows-port.md §5.1 states.
 * Compiled out under AddressSanitizer, which owns malloc (the sanitizer link
 * adds no --wrap). The exemption depth and begin/end protocol are the Linux
 * guard's (src/base/malloc_guard.c). */
#if defined(_WIN32)
#include "base/base.h"

#include <stddef.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer) || \
    defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define EDIT_GUARD_ASAN 1   /* any sanitizer runtime owns malloc: stay inert */
#else
#define EDIT_GUARD_ASAN 0
#endif

#if !EDIT_GUARD_ASAN

extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void  __real_free(void *);

static int guard_on;               /* set between begin/end */
static size_t guard_count;         /* calls counted while guard_on */
static _Thread_local unsigned guard_exempt;   /* per-thread exemption depth */

static void count_call(void) {
    if (__atomic_load_n(&guard_on, __ATOMIC_RELAXED) && !guard_exempt)
        __atomic_fetch_add(&guard_count, 1, __ATOMIC_RELAXED);
}

void *__wrap_malloc(size_t n) {
    count_call();
    return __real_malloc(n);
}

void *__wrap_calloc(size_t n, size_t m) {
    count_call();
    return __real_calloc(n, m);
}

void *__wrap_realloc(void *p, size_t n) {
    count_call();
    return __real_realloc(p, n);
}

void __wrap_free(void *p) { __real_free(p); }   /* nothing to count; wrapped for symmetry */

bool edit_malloc_guard_active(void) { return true; }

void edit_malloc_guard_begin(void) {
    __atomic_store_n(&guard_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&guard_on, 1, __ATOMIC_RELAXED);
}

size_t edit_malloc_guard_end(void) {
    __atomic_store_n(&guard_on, 0, __ATOMIC_RELAXED);
    return __atomic_load_n(&guard_count, __ATOMIC_RELAXED);
}

void edit_malloc_guard_exempt_begin(void) { guard_exempt++; }
void edit_malloc_guard_exempt_end(void) { if (guard_exempt) guard_exempt--; }

#else

bool edit_malloc_guard_active(void) { return false; }
void edit_malloc_guard_begin(void) {}
size_t edit_malloc_guard_end(void) { return 0; }
void edit_malloc_guard_exempt_begin(void) {}
void edit_malloc_guard_exempt_end(void) {}
#endif
#endif /* _WIN32 */
