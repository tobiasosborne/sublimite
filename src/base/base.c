#include "base/base.h"
#include "os/os.h"

static bool is_pow2(size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

/* Callers check room for the alignment padding before rounding. */
static size_t align_up(size_t n, size_t align)
{
    return (n + (align - 1)) & ~(align - 1);
}

static int reserve(void **out, size_t bytes)
{
    size_t page = os_page_size();
    if (bytes > SIZE_MAX - (page - 1)) return -1;
    return os_vm_reserve(out, align_up(bytes, page)) == OS_OK ? 0 : -1;
}

int base_arena_init(base_arena *a, size_t size)
{
    ASSERT(size != 0);
    *a = (base_arena){0};
    void *mem;
    if (reserve(&mem, size) != 0) return -1;
    a->base = mem;
    a->size = size;
    return 0;
}

void *base_arena_alloc(base_arena *a, size_t size, size_t align)
{
    ASSERT(a->base != NULL && is_pow2(align));
    unsigned char *cursor = a->base + a->used;
    size_t padding = (size_t)(-(uintptr_t)cursor & (align - 1));
    size_t remaining = a->size - a->used;
    if (padding > remaining || size > remaining - padding) return NULL;
    a->used += padding + size;
    return cursor + padding;
}

base_arena_mark_t base_arena_mark(const base_arena *a) { return a->used; }

void base_arena_reset_to_mark(base_arena *a, base_arena_mark_t mark)
{
    ASSERT(mark <= a->used);
    a->used = mark;
}

void base_arena_free(base_arena *a)
{
    if (a->base) os_vm_release(a->base, align_up(a->size, os_page_size()));
    *a = (base_arena){0};
}

int base_pool_init(base_pool *p, size_t block_size, size_t block_align, size_t capacity)
{
    size_t page = os_page_size();
    ASSERT(block_size != 0 && capacity != 0 && is_pow2(block_align) && block_align <= page);
    *p = (base_pool){0};
    if (block_size < sizeof(void *)) block_size = sizeof(void *);
    if (block_align < _Alignof(void *)) block_align = _Alignof(void *);
    if (block_size > SIZE_MAX - (block_align - 1)) return -1;
    size_t stride = align_up(block_size, block_align);
    if (capacity > SIZE_MAX / stride) return -1;

    void *mem;
    if (reserve(&mem, stride * capacity) != 0) return -1;
    p->base = mem;
    p->stride = stride;
    p->capacity = capacity;
    return 0;
}

void *base_pool_get(base_pool *p)
{
    void *block = p->free_head;
    if (block) {
        p->free_head = *(void **)block;
    } else {
        if (p->fresh == p->capacity) return NULL;
        block = p->base + p->fresh * p->stride;
        p->fresh++;
    }
    p->live++;
    return block;
}

void base_pool_put(base_pool *p, void *ptr)
{
    /* Integer addresses also make foreign pointers safe to compare. */
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)p->base;
    ASSERT(p->live != 0 && offset < p->fresh * p->stride && offset % p->stride == 0);
    *(void **)ptr = p->free_head;
    p->free_head = ptr;
    p->live--;
}

void base_pool_reset(base_pool *p)
{
    ASSERT(p->base != NULL);
    p->fresh = 0;
    p->live = 0;
    p->free_head = NULL;
}

void base_pool_free(base_pool *p)
{
    if (p->base)
        os_vm_release(p->base, align_up(p->capacity * p->stride, os_page_size()));
    *p = (base_pool){0};
}

/* Publish the compiler runtime's feature bits once, with no second cache. */
static os_once cpu_once = OS_ONCE_INIT;

static void cpu_detect(void)
{
    __builtin_cpu_init();
}

bool base_cpu_has_avx2(void)
{
    os_once_run(&cpu_once, cpu_detect);
    return __builtin_cpu_supports("avx2") != 0;
}

bool base_cpu_has_sse42(void)
{
    os_once_run(&cpu_once, cpu_detect);
    return __builtin_cpu_supports("sse4.2") != 0;
}
