#include "editor/private.h"
#include "font/font.h"
#include "trace/trace.h"
#include "raster/raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

typedef struct backend_init { editor *e; render_config config; void *state; int result; bool received; } backend_init;
static void init_worker(work_ctx *job)
{
    backend_init *a = job->arg;
    int result = work_should_stop(job) ? RENDER_ERR_INIT :
        render_backend_init(a->e->backend, &a->config, a->state, a->e->backend->info.state_size);
    work_msg msg = {.kind = UINT32_C(0x4544494e)};
    memcpy(msg.data, &result, sizeof result);
    while (!work_should_stop(job) && !work_publish(job, &msg)) {
        struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
    }
}
static void init_result(const work_msg *msg, void *ctx)
{
    backend_init *a = ctx;
    if (msg->kind != UINT32_C(0x4544494e)) return;
    memcpy(&a->result, msg->data, sizeof a->result); a->received = true;
}
static int init_backend(editor *e, render_config config, void *state)
{
    backend_init arg = {e, config, state, RENDER_ERR_INIT, false};
    work_handle handle = work_submit(&e->pool, (work_job){init_worker, &arg, 0, WORK_BULK});
    if (!handle.epoch) return EDITOR_ERR_MEMORY;
    /* Retain the argument and arena state until both mailbox receipt and
     * physical completion, including before reclaiming a failed attempt. */
    while (!arg.received || !work_handle_finished(&e->pool, handle)) {
        (void)work_mailbox_receive(&e->pool, handle, 0, init_result, &arg);
        if (arg.received && work_handle_finished(&e->pool, handle)) break;
        struct pollfd ready = {work_pool_eventfd(&e->pool), POLLIN, 0};
        (void)poll(&ready, 1, 1);
    }
    return arg.result;
}
void editor_close(editor *e)
{
    if (!e) return;
    if (e->journal) { (void)editor_flush(e); journal_close(e->journal); e->journal = NULL; }
    if (e->backend) render_backend_shutdown(e->backend);
    if (e->buffers) for (size_t i = 0; i < e->buffer_capacity; i++) editor_buffer_destroy(e->buffers[i]);
    editor_buffer_destroy(e->empty);
    if (e->pool_ready) work_pool_shutdown(&e->pool);
    if (e->has_platform) plat_shutdown(&e->platform);
    if (e->poll_fd >= 0) close(e->poll_fd);
    if (e->tabs_ready) tabs_fini(&e->tabs);
    edit_arena_free(&e->arena); free(e);
}
static int add_poll_fd(editor *e, int fd)
{
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
    return epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &ev) ? EDITOR_ERR_IO : 0;
}
int editor_open(editor **out, const editor_config *config, render_backend *backend)
{
    if (!out || !config || !backend || (!config->initial && config->initial_len) || backend->initialized) return EDITOR_ERR_ARG;
    *out = NULL;
    render_backend_info info;
    if (render_backend_query(backend, &info)) return EDITOR_ERR_ARG;
    render_backend fallback = {0};
    bool may_fallback = config->raster_fallback && (info.capabilities & RENDER_CAP_GPU);
    if (may_fallback && render_cpu_backend(&fallback)) return EDITOR_ERR_ARG;
    editor *e = aligned_alloc(_Alignof(editor), sizeof *e); if (!e) return EDITOR_ERR_MEMORY;
    memset(e, 0, sizeof *e);
    e->poll_fd = -1; e->raster_poll_fd = -1; e->drag_tab = SIZE_MAX;
    e->cfg = *config; e->backend = backend; e->focused = e->visible = true;
    e->max_cols = config->max_cols ? config->max_cols : 360;
    e->max_rows = config->max_rows ? config->max_rows : 300;
    uint32_t cols = config->cols ? config->cols : 120, rows = config->rows ? config->rows : 40;
    if (cols > e->max_cols) e->max_cols = cols;
    if (rows > e->max_rows) e->max_rows = rows;
    int rc = EDITOR_ERR_MEMORY;
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(config->font_px ? config->font_px : 15);
    if (!atlas || e->max_cols > 8192 || e->max_rows > 8192) { rc = EDITOR_ERR_ARG; goto fail; }
    size_t live = config->tab_capacity ? config->tab_capacity : 128;
    size_t closed = config->closed_capacity ? config->closed_capacity : 16;
    if (live > 4096 || closed > 4096) { rc = EDITOR_ERR_ARG; goto fail; }
    e->buffer_capacity = live + closed;
    rc = tabs_init(&e->tabs, live, closed); if (rc) goto fail;
    e->tabs_ready = true;
    uint32_t nraster = (may_fallback || (info.capabilities & RENDER_CAP_RASTER_POOL)) ? 4u : 0u;
    if (work_pool_init_foreground(&e->pool, 1, nraster)) { rc = EDITOR_ERR_MEMORY; goto fail; }
    e->pool_ready = true;
    e->poll_fd = epoll_create1(EPOLL_CLOEXEC); if (e->poll_fd < 0) { rc = EDITOR_ERR_IO; goto fail; }
    rc = add_poll_fd(e, work_pool_eventfd(&e->pool)); if (rc) goto fail;
    if (config->server) { rc = add_poll_fd(e, ipc_server_fd(config->server)); if (rc) goto fail; }
    size_t cells = (size_t)e->max_cols * e->max_rows, words = ((size_t)e->max_rows + 63) / 64;
    size_t reserve = 2 * cells * sizeof(render_cell) + 3 * words * sizeof(uint64_t) +
        (size_t)e->max_rows * (2 * sizeof(layout_wrap_row) + sizeof(indent_range) + 64) +
        e->buffer_capacity * sizeof(editor_buffer *) + info.state_size + info.state_align +
        (may_fallback ? fallback.info.state_size + fallback.info.state_align : 0) + 2u * 1024u * 1024u;
    rc = edit_arena_init(&e->arena, reserve); if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
    e->buffers = edit_arena_alloc(&e->arena, e->buffer_capacity * sizeof *e->buffers, 8);
    if (!e->buffers) { rc = EDITOR_ERR_MEMORY; goto fail; }
    memset(e->buffers, 0, e->buffer_capacity * sizeof *e->buffers);
    e->ops = edit_arena_alloc(&e->arena, EDITOR_STAGE_OPS * sizeof *e->ops, 16);
    e->stage = edit_arena_alloc(&e->arena, EDITOR_STAGE_BYTES, 16);
    e->indent_bytes = edit_arena_alloc(&e->arena, EDITOR_STAGE_BYTES, 16);
    e->ws_ranges = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->ws_ranges, 8);
    e->strip_cap = ((size_t)e->max_rows + 1) / 2;
    e->strips = edit_arena_alloc(&e->arena, e->strip_cap * sizeof *e->strips, 8);
    e->row_byte = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->row_byte, 8);
    e->row_used = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->row_used, 8);
    render_cell *full = edit_arena_alloc(&e->arena, cells * sizeof *full, 16);
    render_cell *text = edit_arena_alloc(&e->arena, cells * sizeof *text, 16);
    /* Minimap is painted in a compact grid with independent row stride. */
    render_cell *map = edit_arena_alloc(&e->arena, (size_t)e->max_rows * 8 * sizeof *map, 16);
    uint64_t *full_bits = edit_arena_alloc(&e->arena, words * sizeof *full_bits, 8);
    uint64_t *text_bits = edit_arena_alloc(&e->arena, words * sizeof *text_bits, 8);
    uint64_t *map_bits = edit_arena_alloc(&e->arena, words * sizeof *map_bits, 8);
    if (!e->ops || !e->stage || !e->indent_bytes || !e->ws_ranges || !e->strips || !e->row_byte || !e->row_used ||
        !full || !text || !map || !full_bits || !text_bits || !map_bits) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_dims dims = {cols, rows, atlas->cell.cell_w, atlas->cell.cell_h};
    e->tab_rows = rows > 1 ? 1u : 0u; e->map_cols = cols >= 24 ? 8u : 0u;
    render_dims td = dims; td.cols -= e->map_cols; td.rows -= e->tab_rows;
    render_dims md = td; md.cols = 8;
    rc = render_grid_init(&e->grid, dims, full, cells, full_bits, words); if (rc) goto fail;
    rc = render_grid_init(&e->text_grid, td, text, cells, text_bits, words); if (rc) goto fail;
    rc = render_grid_init(&e->map_grid, md, map, (size_t)e->max_rows * 8, map_bits, words); if (rc) goto fail;
    e->page = (render_atlas_page){atlas->pixels, atlas->pixels_len, (size_t)95 * dims.cell_w, 95 * dims.cell_w, dims.cell_h};
    layout_ascii_glyphs(atlas, e->glyphs);
    e->grid.pages = &e->page; e->grid.page_count = 1; e->grid.glyphs = e->glyphs; e->grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    e->text_grid.pages = &e->page; e->text_grid.page_count = 1; e->text_grid.glyphs = e->glyphs; e->text_grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    e->layout_cfg = (layout_config){.tab_width = 4, .gutter = true, .slice_clusters = 256,
        .fg = 0xd8dee9, .bg = 0x20242c, .gutter_fg = 0x78808e, .gutter_bg = 0x20242c,
        .cursor_fg = 0x20242c, .cursor_bg = 0xe5e9f0, .sel_fg = 0xffffff, .sel_bg = 0x425471};
    rc = layout_init(&e->lay, &e->text_grid, &e->layout_cfg, e->row_byte, e->row_used); if (rc) goto fail;
    e->text_grid.dims.rows = e->max_rows;
    rc = layout_wrap_init(&e->lay, &e->arena); e->text_grid.dims.rows = td.rows; if (rc) goto fail;
    rc = editor_buffer_prepare(e, NULL, NULL, 0, &e->empty); if (rc) goto fail;
    e->buffer = e->empty; e->tree = e->empty->tree; e->undo = &e->empty->undo;
    view_config vc = {4, td.rows, td.cols > 2 ? td.cols - 2 : 1, NULL, NULL}; view_init(&e->v, e->tree, &vc);
    if (!(backend->info.capabilities & RENDER_CAP_HEADLESS)) {
        plat_config pc = {"sublimité", cols * dims.cell_w, rows * dims.cell_h, false, e->poll_fd, 0};
        rc = plat_init(&e->platform, &pc); if (rc) goto fail;
        e->has_platform = true; plat_set_blink(&e->platform, 0); plat_map(&e->platform);
    }
    edit_arena_mark_t backend_mark = edit_arena_mark(&e->arena);
    void *state = edit_arena_alloc(&e->arena, backend->info.state_size, backend->info.state_align);
    if (!state) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_config render = {.dims = dims, .max_width = e->max_cols * dims.cell_w, .max_height = e->max_rows * dims.cell_h,
        .max_cells = cells, .max_glyphs = LAYOUT_ASCII_GLYPHS, .max_pages = 1, .max_atlas_bytes = atlas->pixels_len,
        .platform = e->has_platform ? &e->platform : NULL, .workers = &e->pool};
    rc = init_backend(e, render, state);
    if (rc && may_fallback) {
        e->stats.backend_init_error = rc;
        const char *reason = rc == RENDER_ERR_UNSUPPORTED ? "EGL/GL/Present unavailable" :
            rc == RENDER_ERR_INIT ? "EGL/GL initialization failed" :
            rc == RENDER_ERR_ARG ? "invalid GL configuration" : "backend initialization error";
        fprintf(stderr, "sublimite: EGL init failed: %s (code=%d); using raster; no retry\n", reason, rc);
        /* init owns rollback of its native/heap resources on failure (GL's
         * gl_release). The editor owns the state storage: reclaim the failed
         * attempt before replacing its descriptor. editor_close shuts down
         * the successful backend before freeing that same arena. */
        edit_arena_reset_to_mark(&e->arena, backend_mark);
        *backend = fallback;
        state = edit_arena_alloc(&e->arena, backend->info.state_size, backend->info.state_align);
        if (!state) { rc = EDITOR_ERR_MEMORY; goto fail; }
        rc = init_backend(e, render, state);
    }
    if (rc) goto fail;
    if (e->has_platform && (backend->info.capabilities & RENDER_CAP_RASTER_POOL)) {
        rc = plat_set_present_events(&e->platform, false); if (rc) goto fail;
    }
    if (config->journal_path) {
        rc = journal_open(&e->journal, config->journal_path, &e->pool, NULL); if (rc) goto fail;
        journal_set_message_handler(e->journal, editor_route_work, e);
    }
    if (!config->start_empty) {
        uint64_t id; rc = editor_add_buffer(e, config->path, config->initial, config->initial_len, &id);
    } else rc = editor_activate(e);
    if (rc) goto fail;
    *out = e; return 0;
fail:
    editor_close(e); return rc > 0 ? EDITOR_ERR_IO : rc;
}
