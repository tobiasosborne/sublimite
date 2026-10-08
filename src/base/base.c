#include "base/base.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

const char *edit_version(void) { return "0.0.1-dev"; }

static int is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }

static size_t align_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}

/* ---- Arena ---- */

int edit_arena_init(edit_arena *a, size_t size) {
    memset(a, 0, sizeof *a);
    if (size == 0) return -1;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return -1;
    a->base = (unsigned char *)p;
    a->size = size;
    return 0;
}

void *edit_arena_alloc(edit_arena *a, size_t size, size_t align) {
    if (!is_pow2(align)) return NULL;
    /* Align the absolute address; base is page-aligned so offsets suffice. */
    size_t off = align_up(a->used, align);
    if (off < a->used || off > a->size || size > a->size - off) return NULL;
    a->used = off + size;
    return a->base + off;
}

edit_arena_mark_t edit_arena_mark(const edit_arena *a) { return a->used; }

void edit_arena_reset_to_mark(edit_arena *a, edit_arena_mark_t m) {
    if (m <= a->used) a->used = m;
}

void edit_arena_reset(edit_arena *a) { a->used = 0; }

void edit_arena_free(edit_arena *a) {
    if (a->base) munmap(a->base, a->size);
    memset(a, 0, sizeof *a);
}

/* ---- Pool ---- */

int edit_pool_init(edit_pool *p, size_t block_size, size_t block_align,
                   size_t capacity) {
    memset(p, 0, sizeof *p);
    if (block_size == 0 || capacity == 0 || !is_pow2(block_align) ||
        block_align > 4096)
        return -1;
    if (block_size < sizeof(void *)) block_size = sizeof(void *);
    if (block_align < _Alignof(void *)) block_align = _Alignof(void *);
    if (block_size > SIZE_MAX - block_align) return -1;
    size_t stride = align_up(block_size, block_align);
    if (capacity > SIZE_MAX / stride) return -1;
    size_t bytes = stride * capacity;
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    size_t map_bytes = align_up(bytes, pg);
    void *m = mmap(NULL, map_bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) return -1;
    p->base = (unsigned char *)m;
    p->stride = stride;
    p->capacity = capacity;
    p->map_bytes = map_bytes;
    return 0;
}

void *edit_pool_get(edit_pool *p) {
    void *blk;
    if (EDIT_LIKELY(p->free_head != NULL)) {
        blk = p->free_head;
        p->free_head = *(void **)blk;
    } else if (p->fresh < p->capacity) {
        blk = p->base + p->fresh * p->stride;
        p->fresh++;
    } else {
        return NULL;
    }
    p->live++;
    return blk;
}

bool edit_pool_put(edit_pool *p, void *ptr) {
    unsigned char *c = (unsigned char *)ptr;
    if (!p->base || c < p->base) return false;
    size_t off = (size_t)(c - p->base);
    if (off >= p->fresh * p->stride || off % p->stride != 0) return false;
    if (p->live == 0) return false;
    *(void **)ptr = p->free_head;
    p->free_head = ptr;
    p->live--;
    return true;
}

size_t edit_pool_count_live(const edit_pool *p) { return p->live; }

void edit_pool_free(edit_pool *p) {
    if (p->base) munmap(p->base, p->map_bytes);
    memset(p, 0, sizeof *p);
}

/* ---- CPU dispatch ---- */

bool edit_cpu_has_avx2(void) {
    static int cached = -1;
    if (cached < 0) {
        __builtin_cpu_init();
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached == 1;
}

bool edit_cpu_has_popcnt(void) {
    static int cached = -1;
    if (cached < 0) {
        __builtin_cpu_init();
        cached = __builtin_cpu_supports("popcnt") ? 1 : 0;
    }
    return cached == 1;
}
