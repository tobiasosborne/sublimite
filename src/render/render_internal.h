#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include <string.h>
#include "render/render_driver.h"

#define RENDER_LIST_END UINT32_MAX

typedef struct render_node {
    uint64_t epoch, ordinal;
    uint32_t prev, next, version;
} render_node;
typedef struct render_bank {
    render_binding binding;
    render_node *nodes;
    uint64_t epoch, ordinal, watermark;
    uint32_t head, tail, count, cursor;
} render_bank;

struct render_scene {
    render_scene_view view;
    size_t overscan_count;
    uint32_t background;
#ifdef RENDER_TEST_HOOKS
    size_t scan_cells, drawn_rows;
    uint64_t scan_ns;
    render_binding observed_binding;
#endif
};

struct render {
    base_arena *arena;
    base_arena_mark_t mark;
    render_setup setup;
    render_backend_caps caps;
    void *driver;
    os_wake command_wake;
    render_driver_port port;
    render_bank active, staged;
    uint64_t next_epoch;
    bool suspended;
    render_sheet sheet;
    uint8_t *sheet_bytes;
    render_scene scene;
    render_packet packet;
    render_counters stats;
    render_rc progress;
    render_rect region;
    render_scroll scroll;
    uint32_t background;
};

/* Packet arrays are allocated writable once. Only idle staging may write
 * through their const owner-facing representation. */
#define RENDER_WRITE(type, ptr) ((type *)(ptr))
#define RENDER_STORAGE(a, n, t) render_storage((a), (n), sizeof(t), _Alignof(t))

static inline void *render_storage(base_arena *a, size_t n, size_t size, size_t align)
{
    size_t bytes;
    if (!n || __builtin_mul_overflow(n, size, &bytes)) return NULL;
    void *p = base_arena_alloc(a, bytes, align);
    if (p) memset(p, 0, bytes);
    return p;
}
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
    return !(r.width | r.height | r.x | r.y);
}
static inline render_rc render_reason(render_need *need, render_field field, render_rc rc,
                                     size_t required, size_t supported)
{
    *need = (render_need){field, required, supported};
    return rc;
}
#define RENDER_REFUSE(need, field, rc) render_reason((need), (field), (rc), 0, 0)

static inline bool render_tile_extent(const render_binding *b, const render_tile_view *v, size_t *bytes)
{
    return v->span && v->span <= RENDER_TILE_MAX_SPAN && v->rect.width && v->rect.height &&
        v->rect.width <= (size_t)b->cell_width * v->span && v->rect.height <= b->cell_height &&
        !__builtin_mul_overflow((size_t)v->rect.width, v->rect.height, bytes);
}
/* Validate readable rows only after an extent is selected. */
static inline bool render_tile_page(const render_tile_view *v, render_image_id id)
{
    render_rect r = v->rect;
    size_t last;
    return v->image.slot == id.slot && v->image.version == id.version && v->page &&
        v->stride >= v->page_width && render_inside(r, v->page_width, v->page_height) &&
        !__builtin_mul_overflow((size_t)(r.y + r.height - 1u), v->stride, &last) &&
        last <= v->length && (size_t)r.x + r.width <= v->length - last;
}
static inline void render_copy_tile(uint8_t *out, const render_tile_view *v)
{
    const render_rect r = v->rect;
    const uint8_t *src = v->page + (size_t)r.y * v->stride + r.x;
    for (uint32_t y = 0; y < r.height; ++y)
        memcpy(out + (size_t)y * r.width, src + y * v->stride, r.width);
}
static inline render_rect render_damage_union(render_rect a, render_rect b)
{
    if (!a.width) return b;
    if (!b.width) return a;
    uint32_t x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    uint32_t right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    uint32_t bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return (render_rect){x, y, right - x, bottom - y};
}
#endif
