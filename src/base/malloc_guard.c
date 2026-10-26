#if !defined(_WIN32)
/* Counting interposition for malloc/calloc/realloc, used by the no-malloc
 * law tests. Underneath we call glibc's exported __libc_* entry points, so
 * no dlsym bootstrap (and no recursion) is involved. Compiled out under
 * AddressSanitizer and ThreadSanitizer, which own malloc themselves. */
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

#if !EDIT_GUARD_ASAN && defined(__GLIBC__)

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);

static int guard_on;               /* set between begin/end */
static size_t guard_count;         /* calls counted while guard_on */
/* Per-thread exemption depth (edit-cx8): library code that law 2 exempts
 * (P2.0 addendum: libxcb reply/event packets in the raster fence worker's
 * post-submit completion handling; libxkbcommon/libxcb keymap construction
 * on the x11 keymap worker) runs concurrently with a UI typing window.
 * Thread-local, so it never hides another thread's allocation. */
static _Thread_local unsigned guard_exempt;

static void count_call(void) {
    if (__atomic_load_n(&guard_on, __ATOMIC_RELAXED) && !guard_exempt)
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

void edit_malloc_guard_exempt_begin(void) { guard_exempt++; }
void edit_malloc_guard_exempt_end(void) { if (guard_exempt) guard_exempt--; }

#else

bool edit_malloc_guard_active(void) { return false; }
void edit_malloc_guard_begin(void) {}
size_t edit_malloc_guard_end(void) { return 0; }
void edit_malloc_guard_exempt_begin(void) {}
void edit_malloc_guard_exempt_end(void) {}
#endif

#endif /* !defined(_WIN32): edit-177r.4, glibc guard; Windows twin is W0.3. */
