#ifndef RENDER_SUBMIT_PREP_H
#define RENDER_SUBMIT_PREP_H

#include "render/render_internal.h"

typedef struct render_stage {
    size_t runs, rows, cells, region_pixels, inspections, tiles, bytes;
    uint32_t endpoint;
} render_stage;

static bool render_same_stamp(render_stamp a, render_stamp b)
{
    return a.surface == b.surface && a.geometry == b.geometry &&
        render_same_generation(a.font, b.font) && a.buffer == b.buffer && a.text == b.text &&
        a.layout_owner == b.layout_owner && a.layout_serial == b.layout_serial &&
        a.layout_top == b.layout_top && a.paint == b.paint && a.composition == b.composition &&
        render_same_frame(a.base, b.base) && a.input_first == b.input_first &&
        a.input_last == b.input_last;
}

static bool render_stamp_current(const render *r, const render_offer *o, const render_source *src)
{
    const render_binding *b = &r->binding;
    render_stamp s = o->stamp;
    return s.surface == b->surface && s.geometry == b->geometry &&
        render_same_generation(s.font, b->font) && render_same_frame(s.base, r->stats.accepted) &&
        s.buffer && s.layout_owner && s.layout_serial && s.paint &&
        s.composition > r->stats.stamp.composition &&
        (!!s.input_first == !!s.input_last) && s.input_first <= s.input_last &&
        render_same_stamp(s, src->current(src->ctx));
}

static bool render_scroll_valid(const render_binding *b, render_scroll s)
{
    if (render_empty(s.clip))
        return !s.origin_y_q8 && !s.first_source_row && !s.source_rows &&
               s.overscan_row == RENDER_NO_ROW;
    if (!s.clip.width || !s.clip.height || !render_inside(s.clip, b->client_width, b->client_height) ||
        s.clip.width > (size_t)b->cols * b->cell_width || !s.source_rows ||
        s.first_source_row >= b->rows || s.source_rows > b->rows - s.first_source_row ||
        s.origin_y_q8 > 0 || s.origin_y_q8 <= -(int64_t)b->cell_height * RENDER_Q8_ONE)
        return false;
    uint64_t source_height = (uint64_t)s.source_rows * b->cell_height;
    uint64_t available = source_height + (b->overscan ? b->cell_height : 0u);
    uint64_t shift = ((uint64_t)-s.origin_y_q8 + RENDER_Q8_ONE - 1u) / RENDER_Q8_ONE;
    return s.clip.height <= source_height && s.clip.height + shift <= available &&
        s.overscan_row == (b->overscan ? b->rows : RENDER_NO_ROW);
}

static bool render_scroll_changed(render_scroll a, render_scroll b)
{
    return !render_same_rect(a.clip, b.clip) || a.first_source_row != b.first_source_row ||
        a.source_rows != b.source_rows || a.overscan_row != b.overscan_row ||
        a.origin_y_q8 != b.origin_y_q8;
}

static bool render_cell_image(const render *r, const render_source *src, render_cell c)
{
    uint32_t slot = c.atlas_slot;
    if (slot == RENDER_NO_SLOT) return c.glyph_index == 0;
    if (!c.glyph_index || (c.attrs & RENDER_ATTR_WIDE_RIGHT)) return false;
    if (render_has_slot(r->binding.sheet, slot))
        return r->sheet_versions[slot - r->binding.sheet.first] == c.glyph_index;
    render_image_id current = {0};
    return render_has_slot(r->binding.runtime, slot) &&
        src->get_identity(src->ctx, slot, &current) == RENDER_IMAGE_INK &&
        current.slot == slot && current.version == c.glyph_index;
}

static bool render_copy_cells(const render *r, const render_source *src, render_cell_span span,
                              render_cell *dst)
{
    render_cell left = {0};
    for (size_t i = 0; i < span.count; ++i) {
        render_cell c = span.cells[i];
        uint16_t width = c.attrs & RENDER_ATTR_WIDTH;
        if ((c.fg | c.bg) > RENDER_RGB_MASK || c.reserved || (c.attrs & ~RENDER_ATTR_MASK) ||
            width == RENDER_ATTR_WIDTH || !render_cell_image(r, src, c))
            return false;
        if (left.attrs & RENDER_ATTR_WIDE_LEFT) {
            if (c.attrs != ((left.attrs & ~RENDER_ATTR_WIDTH) | RENDER_ATTR_WIDE_RIGHT) ||
                c.fg != left.fg || c.bg != left.bg)
                return false;
        } else if (width == RENDER_ATTR_WIDE_RIGHT) {
            return false;
        }
        dst[i] = c;
        left = c;
    }
    return !(left.attrs & RENDER_ATTR_WIDE_LEFT);
}

static render_rc render_copy_row(render *r, const render_source *src, uint32_t row,
                                 render_stage *stage, render_need *need)
{
    render_cell_span span = {0};
    render_rc rc = src->get_row(src->ctx, row, &span);
    if (rc != RENDER_OK) return render_reason(need, RENDER_FIELD_CELL, rc, row, 0);
    bool overscan = row == r->binding.rows;
    const size_t cols = r->binding.cols;
    if ((span.count && !span.cells) || span.count > cols || (!overscan && span.count != cols) ||
        !render_copy_cells(r, src, span, r->buffers.cells + stage->cells))
        return render_reason(need, RENDER_FIELD_CELL, RENDER_INVALID, row, 0);
    stage->cells += span.count;
    if (overscan) r->packet.overscan_count = span.count;
    ++stage->rows;
    return RENDER_OK;
}

static render_rc render_copy_region(render *r, const render_offer *o, const render_source *src,
                                    render_stage *stage, render_need *need)
{
    render_packet *p = &r->packet;
    p->region = o->region == RENDER_KEEP ? r->region : (render_rect){0};
    switch (o->region) {
    case RENDER_KEEP:
        return RENDER_OK;
    case RENDER_REMOVE:
        p->damage = render_damage_union(p->damage, r->region);
        return RENDER_OK;
    case RENDER_REPLACE:
        break;
    }
    render_region_view v = {0};
    render_rc rc = src->get_region(src->ctx, &v);
    if (rc != RENDER_OK) return render_reason(need, RENDER_FIELD_REGION, rc, 0, 0);
    render_rect rect = v.rect;
    const render_limits *l = &r->setup.limits;
    size_t last;
    if (v.mode != RENDER_REPLACE || !v.pixels || !rect.width || !rect.height ||
        !render_inside(rect, r->binding.client_width, r->binding.client_height) ||
        v.stride < rect.width || !render_product(rect.height - 1u, v.stride, &last) ||
        last > v.capacity || rect.width > v.capacity - last ||
        (last + rect.width) > SIZE_MAX / sizeof(uint32_t))
        return render_reason(need, RENDER_FIELD_REGION, RENDER_INVALID, 0, 0);
    if (rect.width > l->region_width)
        return render_reason(need, RENDER_FIELD_REGION_WIDTH, RENDER_CAPACITY, rect.width, l->region_width);
    if (rect.height > l->region_height)
        return render_reason(need, RENDER_FIELD_REGION_HEIGHT, RENDER_CAPACITY, rect.height, l->region_height);
    size_t pixels = (size_t)rect.width * rect.height;
    if (pixels > l->region_pixels)
        return render_reason(need, RENDER_FIELD_REGION_PIXELS, RENDER_CAPACITY, pixels, l->region_pixels);
    for (uint32_t y = 0; y < rect.height; ++y) {
        const uint32_t *row = v.pixels + y * v.stride;
        uint32_t *dst = r->buffers.region_rgb + (size_t)y * rect.width;
        for (uint32_t x = 0; x < rect.width; ++x) {
            if (row[x] > RENDER_RGB_MASK)
                return render_reason(need, RENDER_FIELD_REGION, RENDER_INVALID, 0, 0);
            dst[x] = row[x];
        }
    }
    stage->region_pixels = pixels;
    p->region = rect;
    p->damage = render_damage_union(render_damage_union(p->damage, r->region), rect);
    return RENDER_OK;
}

static render_rc render_copy_tiles(render *r, const render_offer *o, const render_source *src,
                                   render_stage *stage, render_need *need)
{
    render_packet *p = &r->packet;
    const render_slot_range runtime = r->binding.runtime;
    for (uint32_t i = r->delivery.head; i != RENDER_LIST_END &&
         stage->tiles < o->tile_descriptors && stage->bytes < o->tile_bytes;
         i = r->delivery.nodes[i].next) {
        const render_node *node = &r->delivery.nodes[i];
        render_image_id id = {runtime.first + i, node->version}, current = {0};
        ++stage->inspections;
        if (src->get_identity(src->ctx, id.slot, &current) != RENDER_IMAGE_INK ||
            current.slot != id.slot || current.version != id.version)
            return render_reason(need, RENDER_FIELD_TILE, RENDER_STALE, id.slot, 0);
        render_tile_view view = {0};
        size_t length;
        render_rc rc = src->get_tile(src->ctx, id, &view);
        if (rc != RENDER_OK) return render_reason(need, RENDER_FIELD_TILE, rc, id.slot, 0);
        if (!render_tile_size(&r->binding, &view, id, &length))
            return render_reason(need, RENDER_FIELD_TILE, RENDER_INVALID, id.slot, 0);
        if (length > o->tile_bytes - stage->bytes) break;
        r->buffers.deltas[stage->tiles++] = (render_delta){id, view.rect.width, view.rect.height, stage->bytes};
        render_copy_tile(r->buffers.r8 + stage->bytes, &view);
        stage->bytes += length;
        stage->endpoint = i;
    }
    p->tiles = stage->tiles;
    return RENDER_OK;
}

static render_rc render_prepare(render *r, const render_offer *o, const render_source *src,
                                render_stage *stage, render_need *need)
{
    const render_binding *b = &r->binding;
    const render_limits *l = &r->setup.limits;
    render_packet *p = &r->packet;
    bool full = o->mode == RENDER_FULL;
    if (!render_stamp_current(r, o, src) ||
        (!full && (r->stats.state == RENDER_IDLE_FULL || o->stamp.buffer != r->stats.stamp.buffer)))
        return render_reason(need, RENDER_FIELD_STAMP, RENDER_STALE, 0, 0);
    if ((!full && o->mode != RENDER_PATCH) || o->rows != b->rows || o->cols != b->cols ||
        o->background > RENDER_RGB_MASK || !render_scroll_valid(b, o->scroll) ||
        (!render_empty(o->layer_damage) && (!o->layer_damage.width || !o->layer_damage.height ||
          !render_inside(o->layer_damage, b->client_width, b->client_height))))
        return render_reason(need, RENDER_FIELD_BINDING, RENDER_INVALID, 0, 0);
    if ((o->overscan_dirty && !b->overscan) || (full && b->overscan && !o->overscan_dirty))
        return render_reason(need, RENDER_FIELD_OVERSCAN, RENDER_INVALID, 0, 0);
    if ((o->region != RENDER_KEEP && o->region != RENDER_REPLACE && o->region != RENDER_REMOVE) ||
        (full && o->region == RENDER_KEEP))
        return render_reason(need, RENDER_FIELD_REGION, RENDER_INVALID, 0, 0);
    if (o->tile_descriptors > l->tile_descriptors)
        return render_reason(need, RENDER_FIELD_TILE_DESCRIPTORS, RENDER_CAPACITY,
                             o->tile_descriptors, l->tile_descriptors);
    if (o->tile_bytes > l->tile_bytes)
        return render_reason(need, RENDER_FIELD_TILE_BYTES, RENDER_CAPACITY, o->tile_bytes, l->tile_bytes);

    size_t max_runs = b->rows / 2u + b->rows % 2u;
    if (o->run_count > max_runs || (full && o->run_count != 1))
        return render_reason(need, RENDER_FIELD_RUNS, RENDER_INVALID, o->run_count, max_runs);
    uint32_t end = 0;
    for (size_t i = 0; i < o->run_count; ++i) {
        render_strip run = {0};
        render_rc rc = src->get_run(src->ctx, i, &run);
        if (rc != RENDER_OK) return render_reason(need, RENDER_FIELD_RUNS, rc, i, 0);
        if (!run.row_count || run.first_row >= b->rows || run.row_count > b->rows - run.first_row ||
            (i && run.first_row <= end) || (full && (run.first_row || run.row_count != b->rows)))
            return render_reason(need, RENDER_FIELD_RUNS, RENDER_INVALID, i, 0);
        r->buffers.runs[i] = run;
        end = run.first_row + run.row_count;
    }

    p->offer = *o;
    p->overscan_count = 0;
    p->damage = o->layer_damage;
    if (full || o->background != r->background)
        p->damage = (render_rect){0, 0, b->client_width, b->client_height};
    if (render_scroll_changed(o->scroll, r->scroll))
        p->damage = render_damage_union(render_damage_union(p->damage, o->scroll.clip), r->scroll.clip);
    stage->runs = o->run_count;
    for (size_t i = 0; i < stage->runs; ++i) {
        render_strip run = p->runs[i];
        for (uint32_t j = 0; j < run.row_count; ++j) {
            render_rc rc = render_copy_row(r, src, run.first_row + j, stage, need);
            if (rc != RENDER_OK) return rc;
        }
    }
    if (o->overscan_dirty) {
        render_rc rc = render_copy_row(r, src, b->rows, stage, need);
        if (rc != RENDER_OK) return rc;
    }
    render_rc rc = render_copy_region(r, o, src, stage, need);
    if (rc != RENDER_OK) return rc;
    return render_copy_tiles(r, o, src, stage, need);
}
#endif
