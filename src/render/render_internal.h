#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include <stdatomic.h>
#include <string.h>
#include "render/render_driver.h"

#define RENDER_LIST_END UINT32_MAX

typedef struct render_node {
    uint64_t epoch, ordinal;
    uint32_t prev, next, version;
} render_node;
typedef struct render_delivery {
    render_node *nodes;
    uint64_t ordinal, watermark;
    uint32_t head, tail, count, cursor;
} render_delivery;

typedef struct render_buffers {
    render_strip *runs;
    render_cell *cells;
    uint32_t *region_rgb;
    render_delta *deltas;
    uint8_t *r8;
} render_buffers;

typedef enum render_lease { RENDER_LEASE_LIVE, RENDER_LEASE_RELEASED } render_lease;
typedef struct render_evidence { uint64_t ns; render_clock clock; } render_evidence;
struct render {
    base_arena *arena;
    base_arena_mark_t mark;
    render_setup setup;
    render_backend_caps caps;
    void *driver;
    render_binding binding;
    uint64_t epoch;
    uint32_t *sheet_versions;
    render_delivery delivery;
    render_buffers buffers;
    render_packet packet;
    _Atomic(const render_packet *) command;
    render_counters stats;
    render_state bind_previous;
    render_rc progress;
    render_lease lease;
    render_device device;
    render_presentation presentation;
    render_evidence fence, display;
    render_rect region;
    render_scroll scroll;
    uint32_t background;
};

static inline bool render_product(size_t a, size_t b, size_t *out)
{
    return !__builtin_mul_overflow(a, b, out);
}

static inline void *render_storage(base_arena *a, size_t count, size_t elem, size_t align)
{
    size_t bytes;
    if (!count || !render_product(count, elem, &bytes)) return NULL;
    void *p = base_arena_alloc(a, bytes, align);
    if (p) memset(p, 0, bytes);
    return p;
}
#define RENDER_STORAGE(a, count, type) render_storage(a, count, sizeof(type), _Alignof(type))

static inline bool render_same_frame(render_frame_id a, render_frame_id b)
{
    return a.epoch == b.epoch && a.frame == b.frame;
}

static inline bool render_same_generation(render_generation a, render_generation b)
{
    return a.owner == b.owner && a.generation == b.generation;
}

static inline bool render_inside(render_rect r, uint32_t w, uint32_t h)
{
    return r.x <= w && r.width <= w - r.x && r.y <= h && r.height <= h - r.y;
}

static inline bool render_empty(render_rect r)
{
    return r.width == 0 && r.height == 0 && r.x == 0 && r.y == 0;
}

static inline bool render_same_rect(render_rect a, render_rect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static inline void render_copy_rows(void *dst, const void *src, size_t width,
                                    size_t stride, uint32_t rows)
{
    if (width == stride) {
        memcpy(dst, src, width * rows);
        return;
    }
    for (uint32_t row = 0; row < rows; ++row)
        memcpy((unsigned char *)dst + row * width, (const unsigned char *)src + row * stride, width);
}

static inline render_rc render_reason(render_need *need, render_field field, render_rc rc,
                                     size_t required, size_t supported)
{
    if (need) *need = (render_need){field, required, supported};
    return rc;
}

/* Shared sheet/runtime check. Offsets are formed only after these checks. */
static inline bool render_tile_size(const render_binding *b, const render_tile_view *v,
                                    render_image_id expected, size_t *bytes)
{
    render_rect r = v->rect;
    size_t last;
    return v->image.slot == expected.slot && v->image.version == expected.version &&
        v->page && v->span && v->span <= RENDER_TILE_MAX_SPAN &&
        r.width && r.height && r.width <= (size_t)b->cell_width * v->span &&
        r.height <= b->cell_height && v->stride >= v->page_width &&
        render_inside(r, v->page_width, v->page_height) &&
        render_product(r.y + r.height - 1u, v->stride, &last) &&
        last <= v->length && (size_t)r.x + r.width <= v->length - last &&
        render_product(r.width, r.height, bytes);
}

static inline void render_copy_tile(uint8_t *dst, const render_tile_view *v)
{
    render_rect r = v->rect;
    render_copy_rows(dst, v->page + (size_t)r.y * v->stride + r.x, r.width, v->stride, r.height);
}
#endif
