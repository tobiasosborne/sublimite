#include "editor/private.h"
#include <math.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#define FONT_SIZE_MSG UINT32_C(0x4653495a) /* message tag 'FSIZ' */
static int build_atlas(editor_font_size *f)
{
    int rc = f->legacy ? font_set_px(&f->face, f->prepared_px) : font_set_em_px(&f->face, f->prepared_px);
    if (rc) return rc;
    editor_font_atlas *out = &f->atlas[f->preparing];
    font_cell cell = f->face.cell;
    size_t stride = (size_t)LAYOUT_ASCII_GLYPHS * cell.cell_w;
    size_t bytes = stride * cell.cell_h;
    if (!cell.cell_w || !cell.cell_h || bytes > f->capacity) return FONT_ERR_NOMEM;
    memset(out->pixels, 0, bytes);
    for (uint32_t i = 0; i < LAYOUT_ASCII_GLYPHS; i++) {
        edit_arena_reset(&f->scratch);
        font_bitmap bitmap;
        rc = font_raster_glyph(&f->face, 0x20u + i, &f->scratch, &bitmap);
        if (rc) return rc;
        out->metrics[i] = bitmap.m;
        /* Pack directly into a row of cells, matching the baked atlas layout. */
        for (uint32_t y = 0; y < bitmap.h; y++) {
            int64_t cy = (int64_t)cell.ascent + bitmap.m.bearing_y + y;
            if (cy < 0 || cy >= cell.cell_h) continue;
            for (uint32_t x = 0; x < bitmap.w; x++) {
                int64_t cx = (int64_t)bitmap.m.bearing_x + x;
                if (cx >= 0 && cx < cell.cell_w)
                    out->pixels[(size_t)cy * stride + (size_t)i * cell.cell_w + (size_t)cx] = bitmap.pixels[(size_t)y * bitmap.w + x];
            }
        }
    }
    out->atlas = (font_ascii_atlas){f->prepared_px, cell, out->metrics, out->pixels, bytes};
    return FONT_OK;
}
static void font_worker(work_ctx *ctx)
{
    editor_font_size *f = ctx->arg;
    f->worker = pthread_self();
    int rc = FONT_OK;
    if (!f->face.data) {
        size_t len; const unsigned char *bytes = font_embedded_data(&len);
        rc = font_init(&f->face, bytes, len);
        if (!rc) rc = font_set_em_px(&f->face, EDITOR_FONT_MAX_PX);
        if (!rc) {
            /* Exact INIT resource derivation: two immutable atlas versions,
             * each ASCII-count * maximum cell advance * maximum line height.
             * Scratch inherits tools/atlasbake.c's one-MiB raster arena. */
            f->max_cell = f->face.cell;
            f->capacity = (size_t)LAYOUT_ASCII_GLYPHS * f->max_cell.cell_w * f->max_cell.cell_h;
            if (edit_arena_init(&f->storage, 2u * f->capacity) ||
                edit_arena_init(&f->scratch, 1u << 20)) rc = FONT_ERR_NOMEM;
            if (!rc) for (unsigned i = 0; i < 2; i++)
                f->atlas[i].pixels = edit_arena_alloc(&f->storage, f->capacity, 1);
        }
    }
    if (!rc && !work_should_stop(ctx)) rc = build_atlas(f);
    if (!rc && !work_should_stop(ctx)) {
        editor_font_atlas *a = &f->atlas[f->preparing];
        const font_ascii_atlas *baked = f->legacy ? font_ascii_atlas_for_px(f->prepared_px) : NULL;
        if (baked) a->atlas = *baked;
        if (!a->fonts) {
            /* Both runtime owners are prepaid at INIT, along with both ASCII
             * bands. Zoom reuses the detached version after physical work and
             * backend retirement, while the current owner remains UI-readable. */
            for (unsigned i = 0; i < 2 && !rc; i++)
                rc = font_async_open_atlas(&f->atlas[i].fonts, &a->atlas,
                                           !f->legacy, f->max_cell, ctx->pool);
        } else rc = font_async_reset_atlas(a->fonts, &a->atlas, !f->legacy);
    }
    f->result = rc;
    work_msg msg = {.kind = FONT_SIZE_MSG};
    while (!work_should_stop(ctx) && !work_publish(ctx, &msg)) {
        struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
    }
}
bool editor_font_receive(editor *e, const work_msg *msg)
{
    editor_font_size *f = &e->font;
    if (msg->kind != FONT_SIZE_MSG) return false;
    if (f->handle.epoch && msg->slot_ == f->handle.slot && msg->epoch_ == f->handle.epoch)
        f->received = true;
    return true;
}
static void init_receive(const work_msg *msg, void *ctx) { (void)editor_font_receive(ctx, msg); }
int editor_font_init(editor *e, const font_ascii_atlas **atlas)
{
    editor_font_size *f = &e->font;
    f->legacy = e->cfg.font_dpi == 0.0;
    f->dpi = f->legacy ? 96.0 : e->cfg.font_dpi; /* (E) Xft fallback, explicit test pixels */
    f->default_px = f->legacy ? (e->cfg.font_px ? e->cfg.font_px : 15u) :
        editor_runtime_em_px(f->dpi, e->cfg.font_points != 0.0 ? e->cfg.font_points : EDITOR_FONT_DEFAULT_PT);
    if (!f->default_px || f->default_px > EDITOR_FONT_MAX_PX) return EDITOR_ERR_ARG;
    f->points = f->default_points = (double)f->default_px * 72.0 / f->dpi;
    /* Preserve fractional configured points until reset, rather than accumulating
     * nearest-pixel rounding on repeated +/- point actions. */
    if (!f->legacy) f->points = f->default_points = e->cfg.font_points != 0.0 ? e->cfg.font_points : EDITOR_FONT_DEFAULT_PT;
    f->requested_px = f->prepared_px = f->applied_px = f->default_px;
    f->handle = work_submit(&e->pool, (work_job){font_worker, f, 0, WORK_FOREGROUND});
    if (!f->handle.epoch) return EDITOR_ERR_MEMORY;
    while (!f->received || !work_handle_finished(&e->pool, f->handle)) {
        (void)work_mailbox_receive(&e->pool, f->handle, 0, init_receive, e);
        if (f->received && work_handle_finished(&e->pool, f->handle)) break;
        struct pollfd fd = {work_pool_eventfd(&e->pool), POLLIN, 0}; (void)poll(&fd, 1, 1);
    }
    f->handle = (work_handle){0}; f->received = false;
    if (f->result) return f->result;
    const font_ascii_atlas *baked = f->legacy ? font_ascii_atlas_for_px(f->default_px) : NULL;
    *atlas = baked ? baked : &f->atlas[0].atlas;
    return 0;
}
bool editor_font_pending(const editor *e)
{
    return e->font.handle.epoch || e->font.requested_px != e->font.applied_px;
}
int editor_font_zoom(editor *e, int value)
{
    editor_font_size *f = &e->font;
    /* (P) Sublime's binding argument is one point per step. The lower
     * bound inherits the font API's positive whole em; the upper is the INIT cap. */
    if (!value) f->points = f->default_points;
    else {
        f->points += value;
        double minimum = 72.0 / f->dpi;
        double maximum = EDITOR_FONT_MAX_PX * 72.0 / f->dpi;
        if (f->points < minimum) f->points = minimum;
        if (f->points > maximum) f->points = maximum;
    }
    f->requested_px = value ? editor_runtime_em_px(f->dpi, f->points) : f->default_px;
    return editor_font_poll(e);
}
int editor_font_poll(editor *e)
{
    editor_font_size *f = &e->font;
    if (f->handle.epoch) {
        if (!f->received || !work_handle_finished(&e->pool, f->handle)) return 0;
        if (f->result) return f->result;
        if (f->prepared_px == f->requested_px && f->prepared_px != f->applied_px) {
            if (e->backend->active || view_busy(&e->v) || e->action != EDITOR_ACTION_NONE || e->op_count) return 0;
            const font_ascii_atlas *a = &f->atlas[f->preparing].atlas;
            render_dims dims = e->grid.dims;
            dims.cell_w = a->cell.cell_w; dims.cell_h = a->cell.cell_h;
            dims.cols = f->window_width / dims.cell_w; dims.rows = f->window_height / dims.cell_h;
            if (!dims.cols) dims.cols = 1;
            if (!dims.rows) dims.rows = 1;
            if (dims.cols > e->max_cols) dims.cols = e->max_cols;
            if (dims.rows > e->max_rows) dims.rows = e->max_rows;
            e->fonts = f->atlas[f->preparing].fonts;
            memset(e->font_rows, 0, e->font_row_slots * e->font_row_words * sizeof *e->font_rows);
            int rc = editor_resize_grid(e, dims); if (rc) return rc;
            f->current = f->preparing; f->applied_px = f->prepared_px;
        }
        f->handle = (work_handle){0}; f->received = false;
    }
    if (f->requested_px != f->applied_px) {
        f->preparing = 1u - f->current;
        if (!font_async_idle(f->atlas[f->preparing].fonts)) return 0;
        f->prepared_px = f->requested_px;
        f->handle = work_submit(&e->pool, (work_job){font_worker, f, 0, WORK_FOREGROUND});
        /* A temporarily full queue is retried on an existing editor turn. */
    }
    return 0;
}
