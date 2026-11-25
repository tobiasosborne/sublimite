#ifndef RENDER_DRIVER_H
#define RENDER_DRIVER_H

/* Backend implementor interface. App code uses render.h. */
#include "render/render.h"

typedef struct render_packet {
    render_frame_id id;
    render_offer offer;
    const render_binding *binding;
    render_rect damage;
    const render_strip *runs;
    const render_cell *cells;
    size_t overscan_count;
    const uint32_t *region_rgb;
    render_rect region;
    const render_delta *deltas;
    const uint8_t *r8;
    size_t tiles;
} render_packet;

/* Shared dense indexing for a slot previously checked against the binding.
 * Sheet entries precede runtime entries regardless of producer numbering. */
static inline bool render_has_slot(render_slot_range r, uint32_t slot)
{
    return slot >= r.first && slot - r.first < r.count;
}

static inline size_t render_atlas_index(const render_binding *b, uint32_t slot)
{
    return render_has_slot(b->sheet, slot) ? slot - b->sheet.first :
        (size_t)b->sheet.count + (slot - b->runtime.first);
}

/* Damage helpers operate on checked client rectangles. Row damage covers
 * the fixed row and expands the scrolled text through the Q8 successor. */
static inline render_rect render_damage_union(render_rect a, render_rect b)
{
    if (!a.width) return b;
    if (!b.width) return a;
    uint32_t x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    uint32_t right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    uint32_t bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return (render_rect){x, y, right - x, bottom - y};
}

static inline render_rect render_row_damage(const render_packet *p, uint32_t row)
{
    const render_binding *b = p->binding;
    render_scroll s = p->offer.scroll;
    render_rect damage = {0};
    if (row < b->rows)
        damage = (render_rect){0, row * b->cell_height, b->client_width, b->cell_height};
    if (!s.clip.width) return damage;

    uint32_t text_row;
    if (row == s.overscan_row) text_row = s.source_rows;
    else if (row >= s.first_source_row && row - s.first_source_row < s.source_rows)
        text_row = row - s.first_source_row;
    else return damage;

    int64_t q = -s.origin_y_q8;
    int64_t y = (int64_t)text_row * b->cell_height;
    int64_t top = y - (q + RENDER_Q8_ONE - 1u) / RENDER_Q8_ONE;
    int64_t bottom = y + b->cell_height - q / RENDER_Q8_ONE;
    if (top < 0) top = 0;
    if (bottom > s.clip.height) bottom = s.clip.height;
    if (bottom <= top) return damage;
    render_rect text = {s.clip.x, s.clip.y + (uint32_t)top, s.clip.width, (uint32_t)(bottom - top)};
    return render_damage_union(damage, text);
}

/* Query translates platform/native limits before admission, including
 * signed destinations, pitch/segment size, texture/TBO and framebuffer
 * bounds. Open and bind may perform resumable off-path setup. An error
 * from bind preserves the last working scene. consume accepts one sealed
 * packet. MORE/WAIT retries the same command without repeating effects.
 * A successful consume owns the handoff once and can publish separately.
 * The packet, its binding and all of its arrays last through flight
 * retirement. A driver may retain them only within that physical lease.
 * poll returns one authenticated notice on OK. Native work runs on its
 * exclusive owner, reached via work mailboxes or an equivalent prepaid
 * release/acquire command slot. Never issue native work in render_submit.
 * quiesce returns OK only after all physical leases finish, even if a
 * result was cancelled or parked at mailbox fullness. close follows it.
 *
 * Install tiles and apply patches before drawing. Tag installed slots by
 * packet ID and scan retained cells once if any tile arrived, including old
 * version users. Expand to whole pairs, Q8 neighbors and fixed layers.
 * A slot version mismatch contributes zero ink with the original geometry.
 * Resolve inverse after producer styling; draw underline on the final local
 * scanline. Per RGB channel, (fg*a+bg*(255-a)+127)/255 with R8 coverage a.
 * At q=-origin_y_q8 sample the resolved text at y+floor(q/256), and its
 * successor only for nonzero f=q mod 256. Mix ( (256-f)*c0+f*c1+128 )/256.
 * Fractional filtering, sRGB conversion, dithering and extra blending are
 * disabled. Margins use the surface background. Clip fixed overlays last.
 * Failure after partial apply is LOST. Retain the backing across discard. */
typedef struct render_driver_ops {
    render_backend_caps (*query_caps)(const render_setup *);
    render_rc (*open)(void **state, base_arena *, const render_setup *);
    render_rc (*bind)(void *state, const render_binding *, const render_sheet *);
    render_rc (*consume)(void *state, const render_packet *);
    render_rc (*poll)(void *state, render_notice *);
    render_rc (*quiesce)(void *state);
    void (*close)(void *state);
} render_driver_ops;
#endif
