#include "editor/private.h"
#include "font/font.h"
#include "trace/trace.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>

static void *piece_alloc(void *ctx, size_t n) { return edit_arena_alloc(ctx, n, 16); }
static void piece_free(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static size_t snapshot_span(void *ctx, uint64_t off, const uint8_t **p)
{
    piece_iter it; size_t n = 0; piece_iter_begin_snapshot(&it, ctx, off);
    return piece_iter_next(&it, p, &n) ? n : 0;
}
static void snapshot_release(void *ctx) { piece_snapshot_release(ctx); }
typedef struct backend_init { editor *e; render_config config; void *state; int result; } backend_init;
static void *init_worker(void *ctx)
{
    backend_init *a = ctx; (void)trace_thread_register();
    a->result = render_backend_init(a->e->backend, &a->config, a->state, a->e->backend->info.state_size);
    return NULL;
}
void editor_close(editor *e)
{
    if (!e) return;
    if (e->journal) { (void)editor_flush(e); journal_close(e->journal); }
    if (e->backend) render_backend_shutdown(e->backend);
    if (e->index) lineidx_destroy(e->index);
    if (e->file) file_close(e->file);
    if (e->pool_ready) work_pool_shutdown(&e->pool);
    if (e->has_platform) plat_shutdown(&e->platform);
    if (e->undo_ready) undo_destroy(&e->undo);
    if (e->tree) piece_destroy(e->tree);
    edit_arena_free(&e->arena); free(e);
}
int editor_open(editor **out, const editor_config *config, render_backend *backend)
{
    if (!out || !config || !backend || (!config->initial && config->initial_len) || backend->initialized) return EDITOR_ERR_ARG;
    *out = NULL;
    render_backend_info info;
    if (render_backend_query(backend, &info)) return EDITOR_ERR_ARG;
    editor *e = calloc(1, sizeof *e); if (!e) return EDITOR_ERR_MEMORY;
    e->cfg = *config; e->backend = backend; e->focused = e->visible = true;
    e->max_cols = config->max_cols ? config->max_cols : 360;
    e->max_rows = config->max_rows ? config->max_rows : 300;
    uint32_t cols = config->cols ? config->cols : 120, rows = config->rows ? config->rows : 40;
    if (cols > e->max_cols) e->max_cols = cols;
    if (rows > e->max_rows) e->max_rows = rows;
    e->history_cap = config->history_keys ? config->history_keys : 32768;
    if (e->history_cap > (SIZE_MAX - 8) / (2 * sizeof(editor_delta))) { editor_close(e); return EDITOR_ERR_ARG; }
    int rc = EDITOR_ERR_MEMORY;
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(config->font_px ? config->font_px : 15);
    if (!atlas || e->max_cols > 8192 || e->max_rows > 8192) { editor_close(e); return EDITOR_ERR_ARG; }
    uint32_t nraster = (backend->info.capabilities & RENDER_CAP_RASTER_POOL) ? 4u : 0u;
    if (work_pool_init(&e->pool, 1, nraster)) goto fail;
    e->pool_ready = true;
    if (config->path) {
        rc = file_open_begin(&e->pool, config->path, NULL, &e->file); if (rc) goto fail;
        while (!file_open_ready(e->file)) {
            struct pollfd p = {work_pool_eventfd(&e->pool), POLLIN, 0};
            if (poll(&p, 1, 100) < 0 && errno != EINTR) { rc = EDITOR_ERR_IO; goto fail; }
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            if (e->error) { rc = e->error; goto fail; }
        }
        e->crlf = file_prefix_info_of(e->file)->dominant == FILE_EOL_CRLF;
    }
    size_t content = config->path ? (file_open_mode(e->file) == FILE_MODE_COPY ? (size_t)file_size(e->file) : 0) : config->initial_len;
    size_t reserve = config->arena_bytes ? config->arena_bytes : 64u * 1024u * 1024u;
    if (content > SIZE_MAX - reserve) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = edit_arena_init(&e->arena, reserve + content); if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
    piece_allocator allocator = {&e->arena, piece_alloc, piece_free};
    e->tree = piece_create(&allocator); if (!e->tree) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = e->file ? file_attach(e->file, e->tree) : piece_init_copy(e->tree, config->initial, config->initial_len); if (rc) goto fail;
    /* Warm lazy exact piece counts during open, before accepting input. The
     * startup latency of this warmup is outside this bead's G1 measurement. */
    e->lines = piece_line_count(e->tree);
    rc = undo_init(&e->undo, e->tree, 2 * e->history_cap + 8); if (rc) goto fail;
    e->undo_ready = true;
    e->history = edit_arena_alloc(&e->arena, e->history_cap * sizeof *e->history, 16);
    e->ops = edit_arena_alloc(&e->arena, EDITOR_STAGE_OPS * sizeof *e->ops, 16);
    e->stage = edit_arena_alloc(&e->arena, EDITOR_STAGE_BYTES, 16);
    size_t cells = (size_t)e->max_cols * e->max_rows, words = ((size_t)e->max_rows + 63) / 64;
    e->strip_cap = ((size_t)e->max_rows + 1) / 2;
    render_cell *cell = edit_arena_alloc(&e->arena, cells * sizeof *cell, 16);
    uint64_t *bits = edit_arena_alloc(&e->arena, words * sizeof *bits, 8);
    e->strips = edit_arena_alloc(&e->arena, e->strip_cap * sizeof *e->strips, 8);
    e->row_byte = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->row_byte, 8);
    e->row_used = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->row_used, 8);
    if (!e->history || !e->ops || !e->stage || !cell || !bits || !e->strips || !e->row_byte || !e->row_used) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_dims dims = {cols, rows, atlas->cell.cell_w, atlas->cell.cell_h};
    rc = render_grid_init(&e->grid, dims, cell, cells, bits, words); if (rc) goto fail;
    e->page = (render_atlas_page){atlas->pixels, atlas->pixels_len, (size_t)95 * dims.cell_w, 95 * dims.cell_w, dims.cell_h};
    layout_ascii_glyphs(atlas, e->glyphs);
    e->grid.pages = &e->page; e->grid.page_count = 1; e->grid.glyphs = e->glyphs; e->grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    e->layout_cfg = (layout_config){.tab_width = 4, .gutter = true, .slice_clusters = 32,
        .fg = 0xd8dee9, .bg = 0x20242c, .gutter_fg = 0x78808e, .gutter_bg = 0x20242c,
        .cursor_fg = 0x20242c, .cursor_bg = 0xe5e9f0, .sel_fg = 0xffffff, .sel_bg = 0x425471};
    rc = layout_init(&e->lay, &e->grid, &e->layout_cfg, e->row_byte, e->row_used); if (rc) goto fail;
    view_config vc = {4, rows, cols > 12 ? cols - 12 : 1, NULL, NULL}; view_init(&e->v, e->tree, &vc);
    if (!(backend->info.capabilities & RENDER_CAP_HEADLESS)) {
        plat_config pc = {"sublimité", cols * dims.cell_w, rows * dims.cell_h, false, work_pool_eventfd(&e->pool), 0};
        rc = plat_init(&e->platform, &pc); if (rc) goto fail;
        e->has_platform = true; plat_set_blink(&e->platform, 0); plat_map(&e->platform);
    }
    void *state = edit_arena_alloc(&e->arena, backend->info.state_size, backend->info.state_align);
    if (!state) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_config render = {.dims = dims, .max_width = e->max_cols * dims.cell_w, .max_height = e->max_rows * dims.cell_h,
        .max_cells = cells, .max_glyphs = LAYOUT_ASCII_GLYPHS, .max_pages = 1, .max_atlas_bytes = atlas->pixels_len,
        .platform = e->has_platform ? &e->platform : NULL, .workers = &e->pool};
    backend_init arg = {e, render, state, RENDER_ERR_INIT}; pthread_t thread;
    if (pthread_create(&thread, NULL, init_worker, &arg)) { rc = EDITOR_ERR_MEMORY; goto fail; }
    (void)pthread_join(thread, NULL); rc = arg.result; if (rc) goto fail;
    if (config->journal_path) {
        rc = journal_open(&e->journal, config->journal_path, &e->pool, NULL); if (rc) goto fail;
        journal_set_message_handler(e->journal, editor_route_work, e);
        journal_base base = {0};
        if (config->path) { rc = journal_capture_base(file_path(e->file), &base); if (rc) goto fail; }
        else base.path = "";
        rc = journal_set_base(e->journal, 1, &base); if (rc) goto fail;
        if (!config->path && config->initial_len) {
            rc = journal_insert(e->journal, 1, 0, config->initial, config->initial_len); if (rc) goto fail;
        }
    }
    if (config->path && piece_len(e->tree)) {
        e->index = lineidx_create(piece_len(e->tree)); if (!e->index) { rc = EDITOR_ERR_MEMORY; goto fail; }
        piece_snapshot *snapshot = piece_snapshot_take(e->tree); if (!snapshot) { rc = EDITOR_ERR_MEMORY; goto fail; }
        lineidx_src src = {snapshot, piece_snapshot_len(snapshot), snapshot_span, snapshot_release};
        if (lineidx_build_start(e->index, &e->pool, &src)) { piece_snapshot_release(snapshot); rc = EDITOR_ERR_MEMORY; goto fail; }
    }
    editor_restart_blink(e, trace_now_ns()); rc = editor_full_layout(e); if (rc < 0) goto fail;
    *out = e; return 0;
fail:
    editor_close(e); return rc;
}
