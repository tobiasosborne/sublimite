#include "editor/private.h"
#include "font/font.h"
#include "trace/trace.h"
#include "raster/raster.h"
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>

void editor_close(editor *e)
{
    if (!e) return;
    config_runtime_close(e->preferences_runtime); e->preferences_runtime = NULL;
    editor_find_close(e);
    if (e->journal && !e->recovering) (void)editor_flush(e);
    editor_session_close(e);
    if (e->journal) {
        /* A failed startup can close before any BASE/metadata was admitted.
         * Such a placeholder carries no recovery state. Nonempty sources,
         * including failed restores, must survive teardown. */
        if (e->session_lock < 0 || e->session_lock_owned) journal_close_discard_empty(e->journal);
        else journal_close(e->journal); /* another editor owns the session */
        e->journal = NULL;
    }
    if (e->session_lock >= 0) close(e->session_lock);
    if (e->journal_pool_ready) work_pool_shutdown(&e->journal_pool);
    editor_clip_stop(e);
    editor_backend_close_probe(e);
    if (e->backend) render_backend_shutdown(e->backend);
    render_backend_shutdown(&e->retired_backend);
    if (e->buffers) for (size_t i = 0; i < e->buffer_capacity; i++) editor_buffer_destroy(e->buffers[i]);
    editor_buffer_destroy(e->empty);
    if (e->pool_ready) work_pool_shutdown(&e->pool);
    for (unsigned i = 0; i < 2; i++) {
        font_async_close(e->font.atlas[i].fonts);
        font_selection_free(&e->font.atlas[i].selection);
    }
    edit_arena_free(&e->font.storage); edit_arena_free(&e->font.scratch);
    if (e->has_platform) plat_shutdown(&e->platform);
    editor_clip_close(e);
    if (e->poll_fd >= 0) close(e->poll_fd);
    if (e->tabs_ready) tabs_fini(&e->tabs);
    edit_arena_free(&e->arena); free(e);
}
static int add_poll_fd(editor *e, int fd)
{
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
    return epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &ev) ? EDITOR_ERR_IO : 0;
}
static int open_editor(editor **out, const editor_config *config, render_backend *backend,
                       bool skip_recovery, bool *retry, char *aside)
{
    if (!out || !config || !backend || (!config->initial && config->initial_len) || backend->initialized) return EDITOR_ERR_ARG;
    *out = NULL;
    render_backend_info info;
    if (render_backend_query(backend, &info)) return EDITOR_ERR_ARG;
    render_backend fallback = {0};
    bool may_fallback = config->raster_fallback && (info.capabilities & RENDER_CAP_GPU);
    if (may_fallback && ((info.capabilities & RENDER_CAP_HEADLESS) ?
        render_null_backend(&fallback) : render_cpu_backend(&fallback))) return EDITOR_ERR_ARG;
    editor *e = aligned_alloc(_Alignof(editor), sizeof *e); if (!e) return EDITOR_ERR_MEMORY;
    memset(e, 0, sizeof *e);
    e->poll_fd = -1; e->raster_poll_fd = -1; e->session_lock = -1; e->recovery_skip = skip_recovery;
    e->preferences_watchfd = -1; e->drag_tab = SIZE_MAX;
    atomic_init(&e->font_logged, false);
    e->cfg = *config; e->preferences_font_override = config->font_points; e->backend = backend; e->focused = e->visible = true;
    e->max_cols = config->max_cols ? config->max_cols : 360;
    e->max_rows = config->max_rows ? config->max_rows : 300;
    uint32_t cols = config->cols ? config->cols : 120, rows = config->rows ? config->rows : 40;
    if (cols > e->max_cols) e->max_cols = cols;
    if (rows > e->max_rows) e->max_rows = rows;
    int rc = EDITOR_ERR_MEMORY;
    const font_ascii_atlas *atlas = NULL;
    if (e->max_cols > 8192 || e->max_rows > 8192) { rc = EDITOR_ERR_ARG; goto fail; }
    size_t live = config->tab_capacity ? config->tab_capacity : 128;
    size_t closed = config->closed_capacity ? config->closed_capacity : 16;
    if (live > 4096 || closed > 4096) { rc = EDITOR_ERR_ARG; goto fail; }
    e->buffer_capacity = live + closed;
    rc = tabs_init(&e->tabs, live, closed); if (rc) goto fail;
    e->tabs_ready = true;
    rc = editor_session_init(e); if (rc) goto fail;
    uint32_t nraster = (may_fallback || (info.capabilities & RENDER_CAP_RASTER_POOL)) ? 4u : 0u;
    if (work_pool_init_foreground(&e->pool, 1, nraster)) { rc = EDITOR_ERR_MEMORY; goto fail; }
    e->pool_ready = true;
    if (config->load_preferences || config->preferences_path) {
        /* Read and parse on the config worker before the DPI atlas is built.
         * Invalid/absent preferences leave the default startup usable. */
        if (config_runtime_open(&e->preferences_runtime, config->preferences_path) == CONFIG_OK) {
            do {
                (void)config_runtime_tick(e->preferences_runtime);
                if (!config_runtime_busy(e->preferences_runtime)) break;
                struct pollfd fd = {config_runtime_eventfd(e->preferences_runtime), POLLIN, 0};
                (void)poll(&fd, 1, 1);
            } while (true);
            if (e->preferences_font_override == 0.0)
                e->cfg.font_points = config_runtime_current(e->preferences_runtime)->font_points;
        }
    }
    rc = editor_font_init(e, &atlas); if (rc) goto fail;
    trace_record(TRACE_STARTUP_FONT_ATLAS_READY, 0); /* edit-2ts startup stage; atlas readied by edit-m3g's DPI path */
    e->font.window_width = config->window_width ? config->window_width : cols * atlas->cell.cell_w;
    e->font.window_height = config->window_height ? config->window_height : rows * atlas->cell.cell_h;
    if (config->window_width) cols = config->window_width / atlas->cell.cell_w;
    if (config->window_height) rows = config->window_height / atlas->cell.cell_h;
    if (!cols) cols = 1;
    if (!rows) rows = 1;
    if (cols > e->max_cols) e->max_cols = cols;
    if (rows > e->max_rows) e->max_rows = rows;
    if (e->max_cols > 8192 || e->max_rows > 8192) { rc = EDITOR_ERR_ARG; goto fail; }
    e->font.max_width = e->max_cols * atlas->cell.cell_w;
    e->font.max_height = e->max_rows * atlas->cell.cell_h;
    if (config->max_window_width) e->font.max_width = config->max_window_width;
    if (config->max_window_height) e->font.max_height = config->max_window_height;
    if (e->font.max_width < e->font.window_width) e->font.max_width = e->font.window_width;
    if (e->font.max_height < e->font.window_height) e->font.max_height = e->font.window_height;
    e->poll_fd = epoll_create1(EPOLL_CLOEXEC); if (e->poll_fd < 0) { rc = EDITOR_ERR_IO; goto fail; }
    rc = add_poll_fd(e, work_pool_eventfd(&e->pool)); if (rc) goto fail;
    if (e->preferences_runtime) {
        rc = add_poll_fd(e, config_runtime_eventfd(e->preferences_runtime)); if (rc) goto fail;
    }
    if (config->server) { rc = add_poll_fd(e, ipc_server_fd(config->server)); if (rc) goto fail; }
    /* P3.4 reservation: maximum visible rows plus the bottom overscan row. */
    size_t reserved_rows = (size_t)e->max_rows + 1u;
    size_t cells = (size_t)e->max_cols * reserved_rows, words = (reserved_rows + 63) / 64;
    uint32_t font_pages; size_t font_glyphs, font_pixels;
    font_async_atlas_limits(&font_pages, &font_glyphs, &font_pixels, e->font.capacity);
    /* (G) strip reservation = existing eight text columns at the largest
     * prepaid font; summary spacing = PRD's two pixels. Height follows the
     * font-independent surface reserve so resize and zoom need no allocation. */
    uint32_t map_width = (e->max_cols < 8u ? e->max_cols : 8u) * e->font.max_cell.cell_w;
    if (map_width > e->font.max_width) map_width = e->font.max_width;
    uint32_t map_height = e->font.max_height;
    e->map_capacity = (map_height + 1u) / 2u;
    e->map_pixel_capacity = (size_t)map_width * map_height;
    size_t reserve = 2 * e->map_pixel_capacity * sizeof(uint32_t) +
        (size_t)e->map_capacity * 8u * sizeof(render_cell) +
        ((size_t)e->map_capacity + 63u) / 64u * sizeof(uint64_t) +
        cells * sizeof(editor_hit) + 2 * cells * sizeof(render_cell) + 3 * words * sizeof(uint64_t) +
        reserved_rows * (2 * sizeof(layout_wrap_row) + sizeof(indent_range) + 64) +
        e->buffer_capacity * sizeof(editor_buffer *) + info.state_size + info.state_align +
        (may_fallback ? fallback.info.state_size + fallback.info.state_align : 0) +
        font_glyphs * words * sizeof(uint64_t) + 2u * 1024u * 1024u;
    rc = edit_arena_init(&e->arena, reserve); if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = editor_clip_init(e); if (rc) goto fail;
    e->buffers = edit_arena_alloc(&e->arena, e->buffer_capacity * sizeof *e->buffers, 8);
    if (!e->buffers) { rc = EDITOR_ERR_MEMORY; goto fail; }
    memset(e->buffers, 0, e->buffer_capacity * sizeof *e->buffers);
    e->journal_tabs = edit_arena_alloc(&e->arena, 16 + live * 8, 8);
    e->journal_tabs_cached = edit_arena_alloc(&e->arena, 16 + live * 8, 8);
    if (!e->journal_tabs || !e->journal_tabs_cached) { rc = EDITOR_ERR_MEMORY; goto fail; }
    e->ops = edit_arena_alloc(&e->arena, EDITOR_STAGE_OPS * sizeof *e->ops, 16);
    e->stage = edit_arena_alloc(&e->arena, EDITOR_STAGE_BYTES, 16);
    e->indent_bytes = edit_arena_alloc(&e->arena, EDITOR_STAGE_BYTES, 16);
    e->ws_ranges = edit_arena_alloc(&e->arena, e->max_rows * sizeof *e->ws_ranges, 8);
    e->font_row_words = words;
    e->font_row_slots = font_glyphs;
    e->font_rows = edit_arena_alloc(&e->arena, font_glyphs * words * sizeof *e->font_rows, _Alignof(uint64_t));
    if (!e->font_rows) { rc = EDITOR_ERR_MEMORY; goto fail; }
    memset(e->font_rows, 0, font_glyphs * words * sizeof *e->font_rows);
    e->strip_cap = ((size_t)e->max_rows + 1) / 2;
    e->strips = edit_arena_alloc(&e->arena, e->strip_cap * sizeof *e->strips, 8);
    e->hits = edit_arena_alloc(&e->arena, cells * sizeof *e->hits, _Alignof(editor_hit));
    e->row_byte = edit_arena_alloc(&e->arena, reserved_rows * sizeof *e->row_byte, 8);
    e->row_used = edit_arena_alloc(&e->arena, reserved_rows * sizeof *e->row_used, 8);
    render_cell *full = edit_arena_alloc(&e->arena, cells * sizeof *full, 16);
    render_cell *text = edit_arena_alloc(&e->arena, cells * sizeof *text, 16);
    /* Minimap is painted in a compact grid with independent row stride. */
    render_cell *map = edit_arena_alloc(&e->arena, (size_t)e->map_capacity * 8 * sizeof *map, 16);
    uint64_t *full_bits = edit_arena_alloc(&e->arena, words * sizeof *full_bits, 8);
    uint64_t *text_bits = edit_arena_alloc(&e->arena, words * sizeof *text_bits, 8);
    size_t map_words = ((size_t)e->map_capacity + 63u) / 64u;
    uint64_t *map_bits = edit_arena_alloc(&e->arena, map_words * sizeof *map_bits, 8);
    e->map_pixels = edit_arena_alloc(&e->arena, e->map_pixel_capacity * sizeof(uint32_t), 16);
    e->map_scratch = edit_arena_alloc(&e->arena, e->map_pixel_capacity * sizeof(uint32_t), 16);
    if (!e->ops || !e->stage || !e->indent_bytes || !e->ws_ranges || !e->strips || !e->hits || !e->row_byte || !e->row_used ||
        !e->map_pixels || !e->map_scratch || !full || !text || !map || !full_bits || !text_bits || !map_bits) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_dims dims = {cols, rows, atlas->cell.cell_w, atlas->cell.cell_h};
    e->tab_rows = rows > 1 ? 1u : 0u; e->map_cols = cols >= 24 ? 8u : 0u;
    render_dims td = dims; td.cols -= e->map_cols; td.rows -= e->tab_rows;
    render_dims md = {8, (td.rows * dims.cell_h + 1u) / 2u, 1, 2};
    rc = render_grid_init(&e->grid, dims, full, cells, full_bits, words); if (rc) goto fail;
    rc = render_grid_init(&e->text_grid, td, text, cells, text_bits, words); if (rc) goto fail;
    rc = render_grid_init(&e->map_grid, md, map, (size_t)e->map_capacity * 8, map_bits, map_words); if (rc) goto fail;
    e->fonts = e->font.atlas[e->font.current].fonts;
    (void)font_async_bind(e->fonts, &e->grid);
    (void)font_async_bind(e->fonts, &e->text_grid);
    e->layout_cfg = (layout_config){.hit = editor_hit_record, .hit_ctx = e, .tab_width = 4, .gutter = true, .slice_clusters = 256,
        .fg = 0xd8dee9, .bg = 0x20242c, .gutter_fg = 0x78808e, .gutter_bg = 0x20242c,
        .cursor_fg = 0x20242c, .cursor_bg = 0xe5e9f0, .sel_fg = 0xffffff, .sel_bg = 0x425471};
    e->layout_cfg.glyph = editor_font_glyph; e->layout_cfg.glyph_ctx = e;
    /* Initialize the reserved row even when the first tab wraps. Later
     * wrap-off activation must never expose uninitialized tail cells. */
    e->text_grid.dims.rows = td.rows + 1u;
    rc = layout_init(&e->lay, &e->text_grid, &e->layout_cfg, e->row_byte, e->row_used);
    e->text_grid.dims.rows = td.rows; if (rc) goto fail;
    e->text_grid.dims.rows = (uint32_t)reserved_rows;
    rc = layout_wrap_init(&e->lay, &e->arena); e->text_grid.dims.rows = td.rows; if (rc) goto fail;
    rc = editor_find_init(e); if (rc) goto fail;
    rc = editor_buffer_prepare(e, NULL, NULL, 0, &e->empty); if (rc) goto fail;
    e->buffer = e->empty; e->tree = e->empty->tree; e->undo = &e->empty->undo;
    view_config vc = {4, td.rows, td.cols > 2 ? td.cols - 2 : 1, NULL, NULL}; view_init(&e->v, e->tree, &vc);
    if (config->journal_path) {
        if (work_pool_init(&e->journal_pool, 1, 0)) { rc = EDITOR_ERR_MEMORY; goto fail; }
        e->journal_pool_ready = true;
        rc = add_poll_fd(e, work_pool_eventfd(&e->journal_pool)); if (rc) goto fail;
        rc = journal_open(&e->journal, config->journal_path, &e->journal_pool, NULL);
        if (rc == JOURNAL_INVALID) {
            struct stat st;
            if (!lstat(config->journal_path, &st) && S_ISREG(st.st_mode) && st.st_size > 0) {
                rc = journal_open_checkpoint(&e->journal, config->journal_path, &e->journal_pool, NULL);
                if (!rc) e->recover_existing = true;
            }
        }
        if (rc) goto fail;
        journal_set_message_handler(e->journal, editor_route_work, e);
    }
    if (config->journal_path) {
        e->recovering = true; /* failed startup teardown must preserve its source */
        rc = editor_session_restore(e); if (rc) goto fail;
        e->recovering = false;
    }
    dims = e->grid.dims;
    if (!(backend->info.capabilities & RENDER_CAP_HEADLESS)) {
        plat_config pc = {"sublimité", e->font.window_width, e->font.window_height, false, e->poll_fd, 0};
        rc = plat_init_backend(&e->platform, &pc, config->platform_backend); if (rc) goto fail;
        e->has_platform = true; plat_set_blink(&e->platform, 0); plat_map(&e->platform);
    }
    void *state = edit_arena_alloc(&e->arena, backend->info.state_size, backend->info.state_align);
    if (!state) { rc = EDITOR_ERR_MEMORY; goto fail; }
    render_config render = {.dims = dims, .max_width = e->font.max_width, .max_height = e->font.max_height,
        .max_cells = cells, .max_glyphs = font_glyphs, .max_pages = font_pages, .max_atlas_bytes = font_pixels,
        .platform = e->has_platform ? &e->platform : NULL, .workers = &e->pool,
        .max_region_width = map_width, .max_region_height = map_height,
        .hooks = {.present_complete = editor_refresh_complete, .user = e}};
    /* Raster rejects Wayland (edit-v0o.1): no raster bootstrap/fallback there;
     * Wayland GL initializes synchronously (strict path). */
    if (may_fallback && e->platform.backend != PLAT_BACKEND_WAYLAND) {
        render_backend requested = *backend;
        void *gpu_state = state;
        *backend = fallback;
        state = edit_arena_alloc(&e->arena, backend->info.state_size, backend->info.state_align);
        if (!state) { rc = EDITOR_ERR_MEMORY; goto fail; }
        trace_record(TRACE_STARTUP_RASTER_FALLBACK, 0);
        rc = editor_backend_init_sync(e, render, state);
        if (!rc) rc = editor_backend_start_probe(e, requested, render, gpu_state);
    } else rc = editor_backend_init_sync(e, render, state);
    if (rc) goto fail;
    if (e->has_platform && (backend->info.capabilities & RENDER_CAP_RASTER_POOL)) {
        rc = plat_set_present_events(&e->platform, false); if (rc) goto fail;
    }
    if (!config->start_empty && !tabs_count(&e->tabs)) {
        uint64_t id; rc = editor_add_buffer(e, config->path, config->initial, config->initial_len, &id);
    } else rc = editor_activate(e);
    if (rc) goto fail;
    rc = editor_preferences_font_init(e); if (rc) goto fail;
    *out = e; return 0;
fail:
    if (retry && e->recovery_retry) { *retry = true; strcpy(aside, e->recovery_aside); }
    editor_close(e); return rc > 0 ? EDITOR_ERR_IO : rc;
}
int editor_open(editor **out, const editor_config *config, render_backend *backend)
{
    bool retry = false; char aside[IPC_PATH_CAP] = "";
    int rc = open_editor(out, config, backend, false, &retry, aside);
    if (!retry) return rc;
    /* edit-btj: installing a discovered session failed after its source was
     * set aside. A partial checkpoint in this launch's destination is set
     * aside too (journal_open requires it empty); then open without discovery. */
    struct stat st; char partial[IPC_PATH_CAP];
    if (!lstat(config->journal_path, &st) && S_ISREG(st.st_mode) && st.st_size > 0)
        (void)editor_set_aside(config->journal_path, partial, sizeof partial);
    rc = open_editor(out, config, backend, true, NULL, NULL);
    if (!rc) {
        strcpy((*out)->recovery_aside, aside);
        (*out)->recovery_notice = "session recovery failed; its journal was set aside";
        (*out)->recovery_notice_mutations = (*out)->stats.mutations;
    }
    return rc;
}
