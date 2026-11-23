#ifndef BASE_BASE_H
#define BASE_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

const char *base_version(void);

/* Arena and pool objects are single-owner. Init takes a handle with no
 * live reservation. Each reservation is writable and backed on first touch.
 * Preconditions below are ASSERTed (os.h, A15); a size the OS cannot reserve
 * is a return code. */
typedef struct base_arena {
    unsigned char *base;
    size_t         size;
    size_t         used;
} base_arena;

typedef size_t base_arena_mark_t;

/* size > 0. Returns 0 on success, -1 when the reservation fails. */
int   base_arena_init(base_arena *a, size_t size);
/* Initialized arena, align a power of two. Returns NULL on exhaustion
 * without advancing the cursor. */
void *base_arena_alloc(base_arena *a, size_t size, size_t align);
base_arena_mark_t base_arena_mark(const base_arena *a);
/* m is a mark of this arena at or behind the current cursor. */
void  base_arena_reset_to_mark(base_arena *a, base_arena_mark_t m);
void  base_arena_reset(base_arena *a);
void  base_arena_free(base_arena *a);

/* Fixed-size blocks carved lazily from one reservation. Get and put are
 * O(1) using an intrusive LIFO free list. Init touches no block storage. */
typedef struct base_pool {
    unsigned char *base;
    size_t         stride;      /* bytes per block, including padding and room for a link */
    size_t         capacity;
    size_t         fresh;       /* blocks handed out by bump so far */
    size_t         live;
    void          *free_head;   /* intrusive singly linked free list */
} base_pool;

/* block_size and capacity > 0; block_align a power of two up to the OS page
 * size. Returns 0 on success, -1 when the extent is unrepresentable or the
 * reservation fails. */
int   base_pool_init(base_pool *p, size_t block_size, size_t block_align,
                     size_t capacity);
void *base_pool_get(base_pool *p);             /* NULL when full */
/* Return a currently checked-out block exactly once. A foreign, interior or
 * unissued address fails the ASSERT; a second return is not detected. */
void  base_pool_put(base_pool *p, void *ptr);
size_t base_pool_count_live(const base_pool *p);
void  base_pool_free(base_pool *p);

/* ---- SIMD dispatch: cached after first call ---------------------------- */
bool base_cpu_has_avx2(void);
bool base_cpu_has_popcnt(void);

/* Release builds interpose malloc, calloc and realloc. The guard is inert
 * in sanitizer builds, whose runtime owns those entry points. One owner
 * opens and closes non-overlapping, process-wide counting windows. */
bool   base_malloc_guard_active(void);
void   base_malloc_guard_begin(void);
/* Returns the count of non-exempt calls in the window (saturating). */
size_t base_malloc_guard_end(void);
/* Balanced, nestable exemptions apply only to the calling thread.
 * Bracket only exempt library calls such as post-submit libxcb completion. */
void   base_malloc_guard_exempt_begin(void);
void   base_malloc_guard_exempt_end(void);

#endif
