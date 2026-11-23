#include "base/base.h"

enum operation {
    ARENA_ALLOC, ARENA_MARK, ARENA_REWIND, ARENA_RESET,
    POOL_GET, POOL_PUT, POOL_FOREIGN, POOL_INTERIOR
};

/* Each byte gives three opcode bits and five argument bits. Two argument
 * bits select a pool slot, and half the byte range fits in the arena. */
enum { POOL_SLOTS = 1 << 2, ARENA_BYTES = (UINT8_MAX + 1) / 2 };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    const size_t stride = sizeof(max_align_t);
    const size_t block_align = _Alignof(max_align_t);
    base_arena arena;
    base_pool pool;
    BASE_ASSERT(base_arena_init(&arena, ARENA_BYTES) == 0);
    BASE_ASSERT(base_pool_init(&pool, stride, block_align, POOL_SLOTS) == 0);

    size_t used = 0, mark = 0;
    bool live[POOL_SLOTS] = {false};
    for (size_t i = 0; i < len; i++) {
        unsigned arg = data[i] & 31u;
        unsigned slot = arg % POOL_SLOTS;
        enum operation op = (enum operation)(data[i] >> 5);
        switch (op) {
        case ARENA_ALLOC: {
            /* Two bits select alignment and three select request length. */
            size_t align = (size_t)1 << (arg % 4u);
            size_t size = arg / 4u;
            size_t next = used;
            while (((uintptr_t)arena.base + next) % align) next++;
            void *p = base_arena_alloc(&arena, size, align);
            if (next + size > ARENA_BYTES) {
                BASE_ASSERT(p == NULL);
            } else {
                BASE_ASSERT(p == arena.base + next);
                used = next + size;
            }
            break;
        }
        case ARENA_MARK:
            mark = used;
            BASE_ASSERT(base_arena_mark(&arena) == mark);
            break;
        case ARENA_REWIND:
            base_arena_reset_to_mark(&arena, mark);
            if (mark <= used) used = mark;
            break;
        case ARENA_RESET:
            base_arena_reset(&arena);
            used = 0;
            break;
        case POOL_GET: {
            unsigned free_slots = 0;
            for (unsigned j = 0; j < POOL_SLOTS; j++) free_slots += !live[j];
            void *p = base_pool_get(&pool);
            if (free_slots == 0) {
                BASE_ASSERT(p == NULL);
            } else {
                BASE_ASSERT(p != NULL && (uintptr_t)p % block_align == 0);
                uintptr_t offset = (uintptr_t)p - (uintptr_t)pool.base;
                BASE_ASSERT(offset < stride * POOL_SLOTS && offset % stride == 0);
                size_t index = offset / stride;
                BASE_ASSERT(!live[index]);
                live[index] = true;
            }
            break;
        }
        case POOL_PUT:
            if (live[slot]) {
                BASE_ASSERT(base_pool_put(&pool, pool.base + slot * stride));
                live[slot] = false;
            }
            break;
        case POOL_FOREIGN:
            BASE_ASSERT(!base_pool_put(&pool, &used));
            break;
        case POOL_INTERIOR:
            BASE_ASSERT(!base_pool_put(&pool, pool.base + slot * stride + 1));
            break;
        }
        size_t count = 0;
        for (unsigned j = 0; j < POOL_SLOTS; j++) count += live[j];
        BASE_ASSERT(base_pool_count_live(&pool) == count);
        BASE_ASSERT(base_arena_mark(&arena) == used);
    }

    base_pool_free(&pool);
    base_arena_free(&arena);
    return 0;
}
