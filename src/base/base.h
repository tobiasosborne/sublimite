#ifndef EDIT_BASE_H
#define EDIT_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define EDIT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define EDIT_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* The only sanctioned abort() site. */
#define EDIT_ASSERT(x)                                                        \
    do {                                                                      \
        if (EDIT_UNLIKELY(!(x))) {                                            \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,        \
                    __LINE__, #x);                                            \
            abort();                                                          \
        }                                                                     \
    } while (0)

const char *edit_version(void);

/* ---- Arena: bump allocator over one mmap'd region ----------------------
 * Design: the region is mmap'd PROT_READ|PROT_WRITE|MAP_NORESERVE up front.
 * The kernel commits pages lazily on first touch, so there is no mprotect
 * step and no 64 KiB commit bookkeeping. Returns NULL on exhaustion. */
typedef struct edit_arena {
    unsigned char *base;
    size_t         size;
    size_t         used;
} edit_arena;

typedef size_t edit_arena_mark_t;

/* Returns 0 on success, -1 on mmap failure or size == 0. */
int   edit_arena_init(edit_arena *a, size_t size);
/* align must be a power of two; returns NULL on exhaustion. */
void *edit_arena_alloc(edit_arena *a, size_t size, size_t align);
edit_arena_mark_t edit_arena_mark(const edit_arena *a);
void  edit_arena_reset_to_mark(edit_arena *a, edit_arena_mark_t m);
void  edit_arena_reset(edit_arena *a);
void  edit_arena_free(edit_arena *a);

/* ---- Pool: fixed-size blocks in one mmap, intrusive free list ----------
 * get/put are O(1). Blocks are carved lazily (bump index) so init touches
 * no memory beyond the first page. */
typedef struct edit_pool {
    unsigned char *base;
    size_t         stride;      /* block_size rounded up to block_align */
    size_t         capacity;
    size_t         fresh;       /* blocks handed out by bump so far */
    size_t         live;
    void          *free_head;   /* intrusive singly linked free list */
    size_t         map_bytes;
} edit_pool;

/* block_align: power of two, <= 4096. Returns 0 on success, -1 on error. */
int   edit_pool_init(edit_pool *p, size_t block_size, size_t block_align,
                     size_t capacity);
void *edit_pool_get(edit_pool *p);             /* NULL when full */
/* Returns false (and does nothing) if ptr is not a block of this pool. */
bool  edit_pool_put(edit_pool *p, void *ptr);
size_t edit_pool_count_live(const edit_pool *p);
void  edit_pool_free(edit_pool *p);

/* ---- SIMD dispatch: cached after first call ---------------------------- */
bool edit_cpu_has_avx2(void);
bool edit_cpu_has_popcnt(void);

/* ---- Malloc guard for the no-malloc law --------------------------------
 * Interposes malloc/calloc/realloc (glibc __libc_* underneath) and counts
 * calls while a guard is active. Under AddressSanitizer the interposition
 * is compiled out (ASan owns malloc); edit_malloc_guard_active() reports
 * whether counting is real, and the test skips the count check if not. */
bool   edit_malloc_guard_active(void);
void   edit_malloc_guard_begin(void);
/* Returns the number of malloc/calloc/realloc calls since begin. */
size_t edit_malloc_guard_end(void);

#endif
