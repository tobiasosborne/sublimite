#include "render/render.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <limits.h>
#include <string.h>

static bool render_dims_equal(render_dims a, render_dims b)
{
    return a.cols == b.cols && a.rows == b.rows && a.cell_w == b.cell_w && a.cell_h == b.cell_h;
}

static int render_dimensions(render_dims d, size_t *cells, size_t *words)
{
    if (!d.cols || !d.rows || !d.cell_w || !d.cell_h ||
        d.cols > (uint32_t)INT32_MAX / d.cell_w || d.rows > (uint32_t)INT32_MAX / d.cell_h ||
        (size_t)d.cols > SIZE_MAX / d.rows ||
        (size_t)d.cols * d.rows > SIZE_MAX / sizeof(render_cell)) return RENDER_ERR_BOUNDS;
    *cells = (size_t)d.cols * d.rows;
    *words = (size_t)(d.rows / 64u) + (d.rows % 64u != 0u ? 1u : 0u);
    return RENDER_OK;
}

static int render_grid_storage(const render_grid *g)
{
    if (g == NULL || g->cells == NULL || g->dirty == NULL) return RENDER_ERR_ARG;
    size_t cells, words;
    int rc = render_dimensions(g->dims, &cells, &words);
    if (rc != RENDER_OK) return rc;
    if (g->cell_capacity < cells || g->dirty_word_capacity < words) return RENDER_ERR_CAPACITY;
    return RENDER_OK;
}

int render_grid_init(render_grid *g, render_dims dims, render_cell *cells,
                     size_t cell_capacity, uint64_t *dirty, size_t dirty_capacity)
{
    render_grid value = {.dims = dims, .cells = cells, .cell_capacity = cell_capacity,
        .dirty = dirty, .dirty_word_capacity = dirty_capacity};
    if (g == NULL) return RENDER_ERR_ARG;
    int rc = render_grid_storage(&value);
    if (rc == RENDER_OK) *g = value;
    return rc;
}

int render_frame_begin(render_grid *g, uint32_t frame_id)
{
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    if (frame_id == 0 || (g->begun && frame_id <= g->frame_id)) return RENDER_ERR_FRAME;
    size_t words = (size_t)(g->dims.rows / 64u) + (g->dims.rows % 64u != 0u ? 1u : 0u);
    memset(g->dirty, 0, words * sizeof *g->dirty);
    g->frame_id = frame_id; g->begun = true; g->full_frame = false;
    return RENDER_OK;
}

int render_mark_rows(render_grid *g, uint32_t first_row, uint32_t row_count)
{
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    if (!g->begun) return RENDER_ERR_STATE;
    if (first_row > g->dims.rows || row_count > g->dims.rows - first_row) return RENDER_ERR_BOUNDS;
    uint32_t end = first_row + row_count;
    for (uint32_t row = first_row; row < end; row++)
        g->dirty[row / 64u] |= UINT64_C(1) << (row % 64u);
    return RENDER_OK;
}

int render_mark_full(render_grid *g)
{
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    if (!g->begun) return RENDER_ERR_STATE;
    g->full_frame = true;
    return RENDER_OK;
}

static bool render_row_dirty(const render_grid *g, uint32_t row)
{
    return g->full_frame || (g->dirty[row / 64u] & (UINT64_C(1) << (row % 64u))) != 0;
}

static size_t render_strip_count(const render_grid *g)
{
    if (g->full_frame) return 1;
    size_t n = 0; bool previous = false;
    for (uint32_t row = 0; row < g->dims.rows; row++) {
        bool dirty = render_row_dirty(g, row);
        if (dirty && !previous) n++;
        previous = dirty;
    }
    return n;
}

int render_dirty_strips(const render_grid *g, render_strip *out, size_t capacity, size_t *out_count)
{
    if (out_count == NULL || (out == NULL && capacity != 0)) return RENDER_ERR_ARG;
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    if (!g->begun) return RENDER_ERR_STATE;
    size_t required = render_strip_count(g);
    *out_count = required;
    if (capacity < required) return RENDER_ERR_CAPACITY;
    if (g->full_frame) { out[0] = (render_strip){0,g->dims.rows}; return RENDER_OK; }
    size_t n = 0;
    for (uint32_t row = 0; row < g->dims.rows;) {
        if (!render_row_dirty(g, row)) { row++; continue; }
        uint32_t start = row++;
        while (row < g->dims.rows && render_row_dirty(g, row)) row++;
        out[n++] = (render_strip){start,row - start};
    }
    return RENDER_OK;
}

static int render_atlas_validate(const render_grid *g, size_t *bytes)
{
    if ((g->page_count != 0 && g->pages == NULL) || (g->glyph_count != 0 && g->glyphs == NULL))
        return RENDER_ERR_ARG;
    if (g->page_count > UINT32_MAX || g->glyph_count > UINT32_MAX ||
        g->page_count > SIZE_MAX / sizeof(render_atlas_page) ||
        g->glyph_count > SIZE_MAX / sizeof(render_glyph)) return RENDER_ERR_BOUNDS;
    size_t total = 0;
    for (size_t i = 0; i < g->page_count; i++) {
        const render_atlas_page *p = &g->pages[i];
        if (p->pixels == NULL) return RENDER_ERR_ARG;
        if (!p->width || !p->height || p->stride < p->width ||
            (p->height > 1 && p->stride > (SIZE_MAX - p->width) / (p->height - 1u)))
            return RENDER_ERR_BOUNDS;
        size_t required = p->stride * (p->height - 1u) + p->width;
        if (p->pixels_len < required) return RENDER_ERR_CAPACITY;
        if (p->pixels_len > SIZE_MAX - total) return RENDER_ERR_BOUNDS;
        total += p->pixels_len;
    }
    for (size_t i = 0; i < g->glyph_count; i++) {
        const render_glyph *glyph = &g->glyphs[i];
        if (glyph->page >= g->page_count) return RENDER_ERR_BOUNDS;
        const render_atlas_page *page = &g->pages[glyph->page];
        if (!glyph->w || !glyph->h || glyph->x > page->width || glyph->y > page->height ||
            glyph->w > page->width - glyph->x || glyph->h > page->height - glyph->y ||
            (uint64_t)glyph->w > (uint64_t)g->dims.cell_w * 2u || glyph->h > g->dims.cell_h)
            return RENDER_ERR_BOUNDS;
    }
    *bytes = total;
    return RENDER_OK;
}

static int render_cells_validate(const render_grid *g)
{
    size_t count = (size_t)g->dims.cols * g->dims.rows;
    for (size_t i = 0; i < count; i++) {
        const render_cell *c = &g->cells[i];
        uint16_t width = c->attrs & (RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_WIDE_RIGHT);
        if (c->reserved || (c->attrs & (uint16_t)~RENDER_ATTR_MASK) ||
            (c->fg & UINT32_C(0xff000000)) || (c->bg & UINT32_C(0xff000000)) ||
            width == (RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_WIDE_RIGHT)) return RENDER_ERR_CELL;
        if (c->atlas_slot == RENDER_NO_SLOT) {
            if (c->glyph_index != 0) return RENDER_ERR_CELL;
        } else {
            if (c->atlas_slot >= g->glyph_count || width == RENDER_ATTR_WIDE_RIGHT) return RENDER_ERR_CELL;
            const render_glyph *glyph = &g->glyphs[c->atlas_slot];
            if (glyph->glyph_index != c->glyph_index ||
                (width != RENDER_ATTR_WIDE_LEFT && glyph->w > g->dims.cell_w)) return RENDER_ERR_CELL;
        }
        if (width == RENDER_ATTR_WIDE_LEFT) {
            if (i % g->dims.cols == g->dims.cols - 1u) return RENDER_ERR_CELL;
            const render_cell *right = &g->cells[i + 1];
            uint16_t attrs = (c->attrs & (uint16_t)~RENDER_ATTR_WIDE_LEFT) | RENDER_ATTR_WIDE_RIGHT;
            if (right->attrs != attrs || right->fg != c->fg || right->bg != c->bg ||
                right->atlas_slot != RENDER_NO_SLOT || right->glyph_index != 0) return RENDER_ERR_CELL;
        } else if (width == RENDER_ATTR_WIDE_RIGHT) {
            if (i % g->dims.cols == 0 || !(g->cells[i - 1].attrs & RENDER_ATTR_WIDE_LEFT)) return RENDER_ERR_CELL;
        }
    }
    return RENDER_OK;
}

int render_grid_validate(const render_grid *g)
{
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    size_t bytes;
    rc = render_atlas_validate(g, &bytes);
    if (rc != RENDER_OK) return rc;
    return render_cells_validate(g);
}

void render_trace_device_done(void *user, uint32_t frame_id, uint64_t ns)
{
    (void)user;
    if (ns) trace_record_at(ns, TRACE_T5_DEVICE_DONE, frame_id);
    else trace_record(TRACE_T5_DEVICE_DONE, frame_id);
}
void render_trace_present_complete(void *user, uint32_t frame_id, uint64_t ns)
{
    (void)user;
    if (ns) trace_record_at(ns, TRACE_T6_PRESENT_COMPLETE, frame_id);
    else trace_record(TRACE_T6_PRESENT_COMPLETE, frame_id);
}

static bool render_backend_valid(const render_backend *b)
{
    return b != NULL && b->info.name != NULL && b->info.state_size != 0 &&
        b->info.state_align != 0 && (b->info.state_align & (b->info.state_align - 1u)) == 0 &&
        b->ops.init != NULL && b->ops.resize != NULL && b->ops.submit != NULL &&
        b->ops.present != NULL && b->ops.event != NULL && b->ops.shutdown != NULL;
}

int render_backend_query(const render_backend *b, render_backend_info *out)
{
    if (!render_backend_valid(b) || out == NULL) return RENDER_ERR_ARG;
    *out = b->info;
    return RENDER_OK;
}

int render_backend_init(render_backend *b, const render_config *config, void *state, size_t state_bytes)
{
    if (!render_backend_valid(b) || config == NULL || state == NULL) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    if ((uintptr_t)state % b->info.state_align != 0) return RENDER_ERR_ARG;
    if (state_bytes < b->info.state_size) return RENDER_ERR_CAPACITY;
    size_t cells, words;
    int rc = render_dimensions(config->dims, &cells, &words);
    if (rc != RENDER_OK) return rc;
    if (!config->max_width || !config->max_height || config->max_width > (uint32_t)INT32_MAX ||
        config->max_height > (uint32_t)INT32_MAX ||
        (size_t)config->max_width > SIZE_MAX / config->max_height / 4u) return RENDER_ERR_BOUNDS;
    if (cells > config->max_cells || (uint64_t)config->dims.cols * config->dims.cell_w > config->max_width ||
        (uint64_t)config->dims.rows * config->dims.cell_h > config->max_height) return RENDER_ERR_CAPACITY;
    if (config->max_cells > SIZE_MAX / sizeof(render_cell) || config->max_glyphs > UINT32_MAX ||
        config->max_pages > UINT32_MAX || config->max_glyphs > SIZE_MAX / sizeof(render_glyph) ||
        config->max_pages > SIZE_MAX / sizeof(render_atlas_page)) return RENDER_ERR_BOUNDS;
    b->state = state; b->config = *config;
    if (b->config.hooks.device_done == NULL) b->config.hooks.device_done = render_trace_device_done;
    if (b->config.hooks.present_complete == NULL) b->config.hooks.present_complete = render_trace_present_complete;
    b->stats = (render_stats){0}; b->last_frame = 0; b->active = false;
    b->presented = false; b->device_seen = false; b->complete_seen = false;
    b->t5_sent = false; b->t6_sent = false; b->full_required = true;
    rc = b->ops.init(b, &b->config);
    if (rc != RENDER_OK) {
        b->state = NULL; b->config = (render_config){0}; return rc;
    }
    b->initialized = true;
    return RENDER_OK;
}

int render_backend_resize(render_backend *b, render_dims dims)
{
    if (b == NULL) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    if (b->active) return RENDER_ERR_BUSY;
    size_t cells, words;
    int rc = render_dimensions(dims, &cells, &words);
    if (rc != RENDER_OK) return rc;
    if (cells > b->config.max_cells || (uint64_t)dims.cols * dims.cell_w > b->config.max_width ||
        (uint64_t)dims.rows * dims.cell_h > b->config.max_height) return RENDER_ERR_CAPACITY;
    rc = b->ops.resize(b, dims);
    if (rc == RENDER_OK) { b->config.dims = dims; b->full_required = true; }
    return rc;
}

static int render_strips_validate(const render_grid *g, const render_strip *strips, size_t count)
{
    if (count != 0 && strips == NULL) return RENDER_ERR_ARG;
    if (count != render_strip_count(g)) return RENDER_ERR_STRIPS;
    size_t index = 0;
    for (uint32_t row = 0; row < g->dims.rows;) {
        if (!render_row_dirty(g, row)) { row++; continue; }
        uint32_t start = row++;
        while (row < g->dims.rows && render_row_dirty(g, row)) row++;
        if (strips[index].first_row != start || strips[index].row_count != row - start)
            return RENDER_ERR_STRIPS;
        index++;
    }
    return RENDER_OK;
}

static uint64_t render_add_count(uint64_t count, uint64_t add)
{
    return add > UINT64_MAX - count ? UINT64_MAX : count + add;
}

int render_backend_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    if (b == NULL || g == NULL) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    if (b->active) return RENDER_ERR_BUSY;
    int rc = render_grid_storage(g);
    if (rc != RENDER_OK) return rc;
    if (!g->begun) return RENDER_ERR_STATE;
    if (!g->frame_id || g->frame_id <= b->last_frame) return RENDER_ERR_FRAME;
    if (!render_dims_equal(g->dims, b->config.dims)) return RENDER_ERR_BOUNDS;
    if (b->full_required && !g->full_frame) return RENDER_ERR_STRIPS;
    if (g->glyph_count > b->config.max_glyphs || g->page_count > b->config.max_pages)
        return RENDER_ERR_CAPACITY;
    /* CPU submissions have a fixed descriptor-work budget, checked before
     * dereferencing or scanning the table, including zero-damage frames. */
    if ((b->info.capabilities & RENDER_CAP_RASTER_POOL) &&
        g->glyph_count > RASTER_FRAME_GLYPH_LIMIT) return RENDER_ERR_CAPACITY;
    size_t bytes;
    rc = render_atlas_validate(g, &bytes);
    if (rc != RENDER_OK) return rc;
    if (bytes > b->config.max_atlas_bytes) return RENDER_ERR_CAPACITY;
    rc = render_strips_validate(g, strips, count);
    if (rc != RENDER_OK) return rc;
    rc = render_cells_validate(g);
    if (rc != RENDER_OK) return rc;
    /* Implementations must not signal inside submit: adapter activates only
     * after success. Completion mailboxes are dispatched by UI after return. */
    rc = b->ops.submit(b, g, strips, count);
    if (rc != RENDER_OK) return rc;
    b->active = true; b->active_frame = g->frame_id; b->last_frame = g->frame_id;
    b->presented = false; b->device_seen = false; b->complete_seen = false;
    b->t5_sent = false; b->t6_sent = false; b->full_required = false;
    b->submitted_ns = 0; b->device_ns = 0; b->complete_ns = 0;
    b->stats.submitted_frames = render_add_count(b->stats.submitted_frames, 1);
    b->stats.submitted_strips = render_add_count(b->stats.submitted_strips, (uint64_t)count);
    uint64_t submitted = 0;
    for (size_t i = 0; i < count; i++) submitted += (uint64_t)strips[i].row_count * g->dims.cols;
    b->stats.submitted_cells = render_add_count(b->stats.submitted_cells, submitted);
    return RENDER_OK;
}

static void render_deliver(render_backend *b)
{
    if (!b->presented) return;
    if (b->device_seen && !b->t5_sent) {
        uint64_t ns = b->device_ns > b->submitted_ns ? b->device_ns : b->submitted_ns;
        b->t5_sent = true;
        b->config.hooks.device_done(b->config.hooks.user, b->active_frame, ns);
    }
    if (b->complete_seen && !b->t6_sent) {
        b->t6_sent = true;
        uint64_t ns = b->complete_ns > b->submitted_ns ? b->complete_ns : b->submitted_ns;
        b->config.hooks.present_complete(b->config.hooks.user, b->active_frame, ns);
    }
    if (b->t5_sent && b->t6_sent) b->active = false;
}

int render_backend_present(render_backend *b, uint32_t frame_id)
{
    if (b == NULL) return RENDER_ERR_ARG;
    if (!b->initialized || !b->active || b->presented) return RENDER_ERR_STATE;
    if (frame_id != b->active_frame) return RENDER_ERR_FRAME;
    int rc = b->ops.present(b, frame_id);
    if (rc != RENDER_OK) return rc;
    b->submitted_ns = trace_now_ns();
    trace_record_at(b->submitted_ns, TRACE_T4_PRESENT_SUBMITTED, frame_id);
    b->presented = true;
    b->stats.presented_frames = render_add_count(b->stats.presented_frames, 1);
    render_deliver(b);
    return RENDER_OK;
}

int render_backend_signal(render_backend *b, enum render_event_kind kind, uint32_t frame_id, uint64_t ns)
{
    if (b == NULL) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    if (kind != RENDER_EVENT_DEVICE_DONE && kind != RENDER_EVENT_PRESENT_COMPLETE) return RENDER_ERR_ARG;
    if (!b->active || frame_id != b->active_frame) return RENDER_ERR_FRAME;
    if (ns == 0) ns = trace_now_ns();
    if (kind == RENDER_EVENT_DEVICE_DONE) {
        if (b->device_seen) return RENDER_ERR_STATE;
        b->device_ns = ns; b->device_seen = true;
    } else {
        if (b->complete_seen) return RENDER_ERR_STATE;
        b->complete_ns = ns; b->complete_seen = true;
    }
    render_deliver(b);
    return RENDER_OK;
}

int render_backend_event(render_backend *b, const render_event *event)
{
    if (b == NULL || event == NULL) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    if (event->kind == RENDER_EVENT_DEVICE_DONE || event->kind == RENDER_EVENT_PRESENT_COMPLETE)
        return render_backend_signal(b, event->kind, event->frame_id, event->ns);
    if (event->kind != RENDER_EVENT_WORK || event->work == NULL) return RENDER_ERR_ARG;
    if (!b->active || event->frame_id != b->active_frame) return RENDER_ERR_FRAME;
    return b->ops.event(b, event);
}

int render_backend_stats(const render_backend *b, render_stats *out)
{
    if (b == NULL || out == NULL) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    *out = b->stats;
    return RENDER_OK;
}

void render_backend_shutdown(render_backend *b)
{
    if (b == NULL || !b->initialized) return;
    b->initialized = false; b->active = false; /* suppress pending hooks */
    b->ops.shutdown(b);
    b->state = NULL;
    b->config = (render_config){0};
}
