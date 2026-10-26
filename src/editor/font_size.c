#include "editor/private.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "trace/trace.h"

#define FONT_SIZE_MSG UINT32_C(0x4653495a) /* message tag 'FSIZ' */
static int build_atlas(editor_font_size *f)
{
    editor_font_atlas *out = &f->atlas[f->preparing];
    font_t *face = &out->selection.face;
    int rc = f->legacy ? font_set_px(face, f->prepared_px) : font_set_em_px(face, f->prepared_px);
    if (rc) return rc;
    font_cell cell = face->cell;
    size_t stride = (size_t)LAYOUT_ASCII_GLYPHS * cell.cell_w;
    size_t bytes = stride * cell.cell_h;
    if (!cell.cell_w || !cell.cell_h || cell.cell_w > f->max_cell.cell_w ||
        cell.cell_h > f->max_cell.cell_h || bytes > f->capacity) return FONT_ERR_NOMEM;
    memset(out->pixels, 0, bytes);
    uint64_t start = trace_now_ns();
    out->selection.glyph_max_ns = 0;
    for (uint32_t i = 0; i < LAYOUT_ASCII_GLYPHS; i++) {
        uint64_t glyph_start = trace_now_ns();
        edit_arena_reset(&f->scratch);
        font_bitmap bitmap;
        rc = font_raster_glyph(face, 0x20u + i, &f->scratch, &bitmap);
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
        uint64_t elapsed = trace_now_ns() - glyph_start;
        if (elapsed > out->selection.glyph_max_ns) out->selection.glyph_max_ns = elapsed;
    }
    out->selection.atlas_raster_ns = trace_now_ns() - start;
    out->atlas = (font_ascii_atlas){f->prepared_px, cell, out->metrics, out->pixels, bytes};
    return FONT_OK;
}
static int embedded_face(font_t *face)
{
    size_t len; const unsigned char *bytes = font_embedded_data(&len);
    return font_init(face, bytes, len);
}
static void font_worker(work_ctx *ctx)
{
    editor_font_size *f = ctx->arg;
    editor_font_atlas *a = &f->atlas[f->preparing];
    font_selection *selection = &a->selection;
    f->worker = os_thread_self();
    int rc = FONT_OK;
    if (!f->face.data) {
        rc = embedded_face(&f->face);
        if (!rc) rc = font_set_em_px(&f->face, EDITOR_FONT_MAX_PX);
        if (!rc) {
            /* Main's maximum-size bounds prepay both ASCII versions, Unicode
             * cache scratch and minimap pixels. Selection uses these same caps. */
            f->max_cell = f->face.cell;
            f->capacity = (size_t)LAYOUT_ASCII_GLYPHS * f->max_cell.cell_w * f->max_cell.cell_h;
            if (edit_arena_init(&f->storage, 2u * f->capacity) ||
                edit_arena_init(&f->scratch, 1u << 20)) rc = FONT_ERR_NOMEM;
            if (!rc) for (unsigned i = 0; i < 2; i++) {
                f->atlas[i].pixels = edit_arena_alloc(&f->storage, f->capacity, 1);
                if (!f->atlas[i].pixels) rc = FONT_ERR_NOMEM;
            }
        }
    }
    bool custom = selection->family[0] || selection->file[0];
    selection->result = FONT_OK;
    if (!rc && !work_should_stop(ctx)) {
        selection->result = font_selection_load(selection, f->prepared_px, !f->legacy, f->max_cell, ctx);
        if (!selection->result && !custom) rc = embedded_face(&selection->face);
        if (!rc && !selection->result) selection->result = build_atlas(f);
        if (selection->result) {
            font_selection_free(selection);
            rc = embedded_face(&selection->face);
            if (!rc) rc = build_atlas(f);
        }
        if (!rc && f->legacy && (!custom || selection->result)) {
            const font_ascii_atlas *baked = font_ascii_atlas_for_px(f->prepared_px);
            if (baked) a->atlas = *baked;
        }
    }
    if (!rc && !work_should_stop(ctx)) {
        if (!a->fonts) {
            for (unsigned i = 0; i < 2 && !rc; i++)
                rc = font_async_open_atlas(&f->atlas[i].fonts, &a->atlas,
                                           !f->legacy, f->max_cell, ctx->pool);
        } else rc = font_async_reset_atlas(a->fonts, &a->atlas, !f->legacy);
        if (!rc) rc = font_async_set_primary(a->fonts, &selection->face);
        if (!rc) {
            /* edit-6e2: resolve the configured chain here, on the worker. An
             * unresolvable entry is a counted notice, never a failed version. */
            uint32_t skipped = 0;
            int chain = font_async_set_fallbacks(a->fonts, a->fallbacks, &skipped, ctx);
            if (chain != FONT_OK && chain != FONT_MORE) {
                skipped = a->fallbacks[0] ? 1u : 0u; /* one per '\n'-separated entry */
                for (const char *p = a->fallbacks; *p; p++) skipped += *p == '\n';
            }
            a->fallback_skipped = skipped;
            if (skipped && f->chain_logged_generation != f->prepared_generation &&
                !work_should_stop(ctx)) {
                f->chain_logged_generation = f->prepared_generation;
                fprintf(stderr, "sublimite: %u font.fallbacks entr%s skipped (not found)\n",
                        skipped, skipped == 1u ? "y" : "ies");
            }
        }
    }
    if (selection->result != FONT_OK && selection->result != FONT_MORE &&
        selection->face.data && !work_should_stop(ctx)) {
        if (!atomic_exchange(f->logged, true))
            fprintf(stderr, "sublimite: font selection failed; using embedded DejaVu Sans Mono\n");
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
    f->logged = &e->font_logged;
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
        (void)os_wake_wait(work_pool_wake(&e->pool), INT64_C(1000000));
    }
    f->handle = (work_handle){0}; f->received = false;
    if (f->result) return f->result;
    const font_ascii_atlas *baked = f->legacy ? font_ascii_atlas_for_px(f->default_px) : NULL;
    *atlas = baked ? baked : &f->atlas[0].atlas;
    return 0;
}
bool editor_font_pending(const editor *e)
{
    return e->font.handle.epoch || e->font.requested_px != e->font.applied_px ||
        e->font.requested_generation != e->font.applied_generation;
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
        if (f->prepared_px == f->requested_px && f->prepared_generation == f->requested_generation &&
            (f->prepared_px != f->applied_px || f->prepared_generation != f->applied_generation)) {
            if (e->backend->active || e->dirty || view_busy(&e->v) || layout_busy(&e->lay) ||
                editor_clip_busy(e) || e->action != EDITOR_ACTION_NONE || e->op_count) return 0;
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
            if (f->prepared_generation != f->applied_generation) {
                e->stats.font_changes++;
                e->stats.font_error = f->atlas[f->preparing].selection.result;
            }
            e->stats.font_fallback_skipped = f->atlas[f->preparing].fallback_skipped;
            f->current = f->preparing; f->applied_px = f->prepared_px;
            f->applied_generation = f->prepared_generation;
        }
        f->handle = (work_handle){0}; f->received = false;
    }
    if (f->requested_px != f->applied_px || f->requested_generation != f->applied_generation) {
        const font_selection *request = &f->selection;
        if (f->requested_generation == f->applied_generation)
            request = &f->atlas[f->current].selection;
        else if (f->prepared_generation == f->requested_generation &&
                 f->atlas[f->preparing].selection.face.data)
            request = &f->atlas[f->preparing].selection;
        f->preparing = 1u - f->current;
        if (!font_async_idle(f->atlas[f->preparing].fonts)) return 0;
        f->prepared_px = f->requested_px;
        f->prepared_generation = f->requested_generation;
        font_selection *selection = &f->atlas[f->preparing].selection;
        /* Reuse a resolved path even when zoom supersedes a prepared family
         * before adoption. The worker still owns the actual file/face load. */
        if (selection != request) {
            memcpy(selection->family, request->family, sizeof selection->family);
            memcpy(selection->file, request->file, sizeof selection->file);
            selection->index = request->index;
        }
        /* The UI chain request always equals the newest generation's chain. */
        memcpy(f->atlas[f->preparing].fallbacks, f->fallbacks, sizeof f->fallbacks);
        f->handle = work_submit(&e->pool, (work_job){font_worker, f, 0, WORK_FOREGROUND});
        /* A temporarily full queue is retried on an existing editor turn. */
    }
    return 0;
}
