#include "render/render_internal.h"

typedef struct render_installed {
    uint32_t version, width, height, frame;
} render_installed;

typedef enum render_null_phase {
    RENDER_NULL_READY, RENDER_NULL_ISSUE, RENDER_NULL_FENCE, RENDER_NULL_DISPLAY, RENDER_NULL_RELEASE
} render_null_phase;

typedef struct render_null {
    render_cell *cells;
    render_sample *samples;
    render_installed *installed;
    uint32_t *row_tags, *region_rgb;
    uint8_t *atlas;
    size_t tile_stride, overscan_count;
    uint32_t background;
    render_frame_id id;
    render_draw_report draw;
    render_null_phase phase;
} render_null;

static render_backend_caps render_null_caps(const render_setup *s)
{
    return (render_backend_caps){.limits = s->limits, .synthetic = true};
}

static render_rc render_null_open(void **state, base_arena *a, const render_setup *s)
{
    const render_limits *l = &s->limits;
    size_t slots, rows;
    if (__builtin_add_overflow(l->sheet_slots, l->runtime_slots, &slots) ||
        __builtin_add_overflow((size_t)l->rows, 1u, &rows)) return RENDER_CAPACITY;
    render_null *n = RENDER_STORAGE(a, 1, render_null);
    if (!n) return RENDER_CAPACITY;
    n->cells = RENDER_STORAGE(a, l->cells, render_cell);
    n->samples = RENDER_STORAGE(a, l->cells, render_sample);
    n->installed = RENDER_STORAGE(a, slots, render_installed);
    n->row_tags = RENDER_STORAGE(a, rows, uint32_t);
    n->atlas = RENDER_STORAGE(a, l->atlas_bytes, uint8_t);
    n->region_rgb = RENDER_STORAGE(a, l->region_pixels, uint32_t);
    if (!n->cells || !n->samples || !n->installed || !n->row_tags || !n->atlas ||
        (l->region_pixels && !n->region_rgb)) return RENDER_CAPACITY;
    *state = n;
    return RENDER_OK;
}

static render_rc render_null_bind(void *state, const render_binding *b, const render_sheet *s)
{
    render_null *n = state;
    n->tile_stride = (size_t)b->cell_width * b->cell_height * RENDER_TILE_MAX_SPAN;
    memset(n->row_tags, 0, ((size_t)b->rows + b->overscan) * sizeof *n->row_tags);
    memset(n->installed, 0, ((size_t)b->sheet.count + b->runtime.count) * sizeof *n->installed);
    for (size_t i = 0; i < s->count; ++i) {
        const render_tile_view *v = &s->tiles[i];
        render_copy_tile(n->atlas + i * n->tile_stride, v);
        n->installed[i] = (render_installed){v->image.version, v->rect.width, v->rect.height, 0};
    }
    return RENDER_OK;
}

static void render_null_draw_row(render_null *n, const render_packet *p, uint32_t row)
{
    const render_binding *b = p->binding;
    size_t at = (size_t)row * b->cols;
    for (uint32_t x = 0; x < b->cols; ++x) {
        render_cell cell = n->cells[at + x];
        render_sample sample = {.cell = cell};
        render_cell owner = cell;
        if (cell.attrs & RENDER_ATTR_WIDE_RIGHT) {
            owner = n->cells[at + x - 1u];
            sample.x_offset = b->cell_width;
        }
        if (owner.atlas_slot != RENDER_NO_SLOT) {
            size_t slot = render_atlas_index(b, owner.atlas_slot);
            render_installed v = n->installed[slot];
            if (v.version == owner.glyph_index && sample.x_offset < v.width) {
                sample.coverage = n->atlas + slot * n->tile_stride;
                sample.width = v.width;
                sample.height = v.height;
            }
        }
        n->samples[at + x] = sample;
    }
    n->draw.drawn_cells += b->cols;
    ++n->draw.drawn_rows;
    n->draw.damage = render_damage_union(n->draw.damage, render_row_damage(p, row));
}

static void render_null_draw(render_null *n, const render_packet *p)
{
    const render_binding *b = p->binding;
    size_t offset = 0;
    for (size_t i = 0; i < p->offer.run_count; ++i) {
        render_strip run = p->runs[i];
        size_t count = (size_t)run.row_count * b->cols;
        memcpy(n->cells + (size_t)run.first_row * b->cols, p->cells + offset, count * sizeof *p->cells);
        offset += count;
        if (p->tiles)
            for (uint32_t j = 0; j < run.row_count; ++j)
                n->row_tags[run.first_row + j] = p->id.frame;
    }
    bool extra_dirty = p->offer.overscan_dirty ||
        (b->overscan && n->overscan_count < b->cols && n->background != p->offer.background);
    if (extra_dirty) {
        render_cell *extra = n->cells + (size_t)b->rows * b->cols;
        if (p->offer.overscan_dirty) {
            memcpy(extra, p->cells + offset, p->overscan_count * sizeof *extra);
            n->overscan_count = p->overscan_count;
        }
        for (size_t i = n->overscan_count; i < b->cols; ++i)
            extra[i] = (render_cell){.atlas_slot = RENDER_NO_SLOT, .bg = p->offer.background};
        if (p->tiles) n->row_tags[b->rows] = p->id.frame;
    }
    n->background = p->offer.background;
    if (!p->tiles) {
        for (size_t i = 0; i < p->offer.run_count; ++i) {
            render_strip run = p->runs[i];
            for (uint32_t j = 0; j < run.row_count; ++j)
                render_null_draw_row(n, p, run.first_row + j);
        }
        if (extra_dirty) render_null_draw_row(n, p, b->rows);
        return;
    }
    const uint32_t rows = b->rows + b->overscan;
    uint64_t scan_start = os_now_ns();
    for (uint32_t row = 0; row < rows; ++row) {
        const render_cell *cells = n->cells + (size_t)row * b->cols;
        for (uint32_t x = 0; x < b->cols; ++x) {
            uint32_t slot = cells[x].atlas_slot;
            if (render_has_slot(b->runtime, slot) &&
                n->installed[render_atlas_index(b, slot)].frame == p->id.frame)
                n->row_tags[row] = p->id.frame;
        }
    }
    n->draw.scan_cells = (size_t)rows * b->cols;
    n->draw.scan_ns = os_now_ns() - scan_start;
    for (uint32_t row = 0; row < rows; ++row)
        if (n->row_tags[row] == p->id.frame) render_null_draw_row(n, p, row);
}

static render_rc render_null_consume(void *state, const render_packet *p)
{
    render_null *n = state;
    if (n->phase != RENDER_NULL_READY) return RENDER_LOST;
    n->id = p->id;
    n->draw = (render_draw_report){.samples = n->samples, .damage = p->damage, .region = p->region,
        .region_rgb = p->region.width ? n->region_rgb : NULL};
    for (size_t i = 0; i < p->tiles; ++i) {
        render_delta d = p->deltas[i];
        size_t slot = render_atlas_index(p->binding, d.image.slot);
        size_t bytes = (size_t)d.width * d.height;
        memcpy(n->atlas + slot * n->tile_stride, p->r8 + d.offset, bytes);
        n->installed[slot] = (render_installed){d.image.version, d.width, d.height, p->id.frame};
        n->draw.copied_tile_bytes += bytes;
    }
    if (p->offer.region == RENDER_REPLACE)
        memcpy(n->region_rgb, p->region_rgb, (size_t)p->region.width * p->region.height * sizeof(uint32_t));
    render_null_draw(n, p);
    n->phase = RENDER_NULL_ISSUE;
    return RENDER_OK;
}

static render_rc render_null_poll(void *state, render_notice *out)
{
    render_null *n = state;
    *out = (render_notice){.id = n->id};
    switch (n->phase) {
    case RENDER_NULL_READY:
        return RENDER_WAIT;
    case RENDER_NULL_ISSUE:
        out->kind = RENDER_NOTICE_ISSUED;
        out->draw = n->draw;
        n->phase = RENDER_NULL_FENCE;
        break;
    case RENDER_NULL_FENCE:
        out->kind = RENDER_NOTICE_DEVICE_DONE;
        n->phase = RENDER_NULL_DISPLAY;
        break;
    case RENDER_NULL_DISPLAY:
        out->kind = RENDER_NOTICE_PRESENTED;
        n->phase = RENDER_NULL_RELEASE;
        break;
    case RENDER_NULL_RELEASE:
        out->kind = RENDER_NOTICE_RELEASED;
        n->phase = RENDER_NULL_READY;
        return RENDER_OK;
    }
    out->ns = os_now_ns();
    out->clock = RENDER_SYNTHETIC;
    return RENDER_OK;
}

static render_rc render_null_quiesce(void *state)
{
    render_null *n = state;
    n->phase = RENDER_NULL_READY;
    return RENDER_OK;
}

static void render_null_close(void *state)
{
    (void)state;
}

render_driver render_driver_null(void)
{
    static const render_driver_ops ops = {
        render_null_caps, render_null_open, render_null_bind, render_null_consume,
        render_null_poll, render_null_quiesce, render_null_close
    };
    return (render_driver){&ops};
}
