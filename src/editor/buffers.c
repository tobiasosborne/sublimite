#include "editor/private.h"
#include "trace/trace.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

static void *piece_alloc(void *ctx, size_t n) { return edit_arena_alloc(ctx, n, 16); }
static void piece_free(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static size_t tree_span(void *ctx, uint64_t off, const uint8_t **p)
{
    piece_iter it; size_t n = 0; piece_iter_begin(&it, ctx, off);
    return piece_iter_next(&it, p, &n) ? n : 0;
}
static size_t snapshot_span(void *ctx, uint64_t off, const uint8_t **p)
{
    piece_iter it; size_t n = 0; piece_iter_begin_snapshot(&it, ctx, off);
    return piece_iter_next(&it, p, &n) ? n : 0;
}
static void snapshot_release(void *ctx) { piece_snapshot_release(ctx); }
lineidx_src editor_source(editor_buffer *b)
{ return (lineidx_src){b->tree, piece_len(b->tree), tree_span, NULL}; }
minimap_input editor_map_input(editor_buffer *b)
{
    lineidx_src src = editor_source(b);
    if (b->index) (void)lineidx_poll(b->index);
    bool ready = b->index && lineidx_complete(b->index) && !lineidx_building(b->index) && lineidx_len(b->index) == src.len;
    lineidx_result lines = b->index ? lineidx_line_count(b->index) : (lineidx_result){b->lines, false};
    return (minimap_input){src, ready ? lines.value : b->lines, b->revision, ready};
}
void editor_buffer_destroy(editor_buffer *b)
{
    if (!b) return;
    minimap_fini(&b->map);
    if (b->index) lineidx_destroy(b->index);
    if (b->file) file_close(b->file);
    if (b->undo_ready) undo_destroy(&b->undo);
    if (b->tree) piece_destroy(b->tree);
    edit_arena_free(&b->arena); free(b);
}
void editor_retire_buffers(editor *e)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i] && e->buffers[i]->retired) {
        editor_buffer_destroy(e->buffers[i]); e->buffers[i] = NULL;
    }
}
int editor_buffer_prepare(editor *e, const char *path, const uint8_t *bytes, size_t len, editor_buffer **out)
{
    *out = NULL;
    if ((!bytes && len) || (path && strlen(path) >= IPC_PATH_CAP)) return EDITOR_ERR_ARG;
    editor_buffer *b = calloc(1, sizeof *b); if (!b) return EDITOR_ERR_MEMORY;
    int rc = 0;
    b->base.path = "";
    if (path) {
        /* Capture before the file worker loads any bytes, then validate the
         * same generation after attachment and immediately before publication. */
        if (e->journal) (void)journal_capture_base(path, &b->base);
        memcpy(b->path, path, strlen(path) + 1);
        rc = file_open_begin(&e->pool, path, NULL, &b->file);
        if (rc) goto fail;
        e->opening = b->file; e->open_error = 0;
        while (b->file && !file_open_ready(b->file)) {
            struct pollfd p = {work_pool_eventfd(&e->pool), POLLIN, 0};
            if (poll(&p, 1, 100) < 0 && errno != EINTR) { rc = EDITOR_ERR_IO; goto fail; }
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            if (e->error) { rc = e->error; goto fail; }
            if (e->open_error) {
                rc = e->open_error;
                /* Path lookup runs on a worker. Only its installed errno can
                 * distinguish a new buffer from an actual open failure. */
                if (rc != FILE_ERR_IO || file_errno(b->file) != ENOENT) goto fail;
                file_close(b->file); b->file = NULL;
                e->open_error = 0; rc = 0;
            }
        }
        e->opening = NULL;
    }
    size_t content = b->file ? (file_open_mode(b->file) == FILE_MODE_COPY ? (size_t)file_size(b->file) : 0) : len;
    size_t reserve = e->cfg.arena_bytes ? e->cfg.arena_bytes : 64u * 1024u * 1024u;
    if (content > SIZE_MAX - reserve) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = edit_arena_init(&b->arena, reserve + content); if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
    piece_allocator a = {&b->arena, piece_alloc, piece_free};
    b->tree = piece_create(&a); if (!b->tree) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = b->file ? file_attach(b->file, b->tree) : piece_init_copy(b->tree, bytes, len); if (rc) goto fail;
    if (e->journal && b->file && (!*b->base.path || journal_check_base(&b->base) || file_check(b->file, NULL))) {
        rc = EDITOR_ERR_IO; goto fail;
    }
    b->lines = piece_line_count(b->tree);
    b->history_cap = e->cfg.history_keys ? e->cfg.history_keys : 32768;
    if (b->history_cap > (SIZE_MAX - 8) / (2 * sizeof(editor_delta))) { rc = EDITOR_ERR_ARG; goto fail; }
    rc = undo_init(&b->undo, b->tree, 2 * b->history_cap + 8); if (rc) goto fail;
    b->undo_ready = true;
    b->history = edit_arena_alloc(&b->arena, b->history_cap * sizeof *b->history, 16);
    minimap_row *rows = edit_arena_alloc(&b->arena, e->max_rows * sizeof *rows, 8);
    if (!b->history || !rows) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = minimap_init(&b->map, rows, e->max_rows); if (rc) goto fail;
    b->map_rows = rows;
    piece_snapshot *snap = piece_snapshot_take(b->tree); if (!snap) { rc = EDITOR_ERR_MEMORY; goto fail; }
    indent_code ir = indent_detect(snap, &b->style); piece_snapshot_release(snap);
    if (ir != INDENT_OK) { rc = EDITOR_ERR_ARG; goto fail; }
    b->index = lineidx_create(piece_len(b->tree)); if (!b->index) { rc = EDITOR_ERR_MEMORY; goto fail; }
    if (piece_len(b->tree) <= MINIMAP_SMALL_BYTES) {
        lineidx_src src = editor_source(b);
        (void)lineidx_seek_line(b->index, &src, b->lines, MINIMAP_SMALL_BYTES);
    } else {
        snap = piece_snapshot_take(b->tree); if (!snap) { rc = EDITOR_ERR_MEMORY; goto fail; }
        lineidx_src src = {snap, piece_snapshot_len(snap), snapshot_span, snapshot_release};
        if (lineidx_build_start(b->index, &e->pool, &src)) { piece_snapshot_release(snap); rc = EDITOR_ERR_MEMORY; goto fail; }
    }
    b->initial = (view_state){.selection = {.preferred_col = VIEW_PREFERRED_UNSET},
        .wrap = e->cfg.wrap_mode ? e->cfg.wrap_mode > 0 : view_wrap_default(b->path)};
    *out = b; return 0;
fail:
    e->opening = NULL;
    editor_buffer_destroy(b); return rc > 0 ? EDITOR_ERR_IO : rc;
}
static editor_buffer *find_buffer(editor *e, piece_tree *tree)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i] && e->buffers[i]->tree == tree) return e->buffers[i];
    return e->empty;
}
int editor_activate(editor *e)
{
    const tabs_tab *tab = tabs_active(&e->tabs);
    view_state live = e->v.state;
    e->buffer = tab ? find_buffer(e, tab->buffer) : e->empty;
    if (!e->buffer->map.initialized) {
        int rc = minimap_init(&e->buffer->map, e->buffer->map_rows, e->max_rows); if (rc) return rc;
    }
    e->tree = e->buffer->tree; e->undo = &e->buffer->undo;
    view_config vc = {e->buffer->style.width, e->text_grid.dims.rows, 1, NULL, NULL};
    e->lay.row = e->lay.row_end; /* discard the outgoing layout continuation */
    e->lay.src = NULL; e->lay.cfg.tab_width = e->buffer->style.width; e->lay.tab = e->buffer->style.width;
    view_init(&e->v, e->tree, &vc);
    int rc = view_wrap_file(&e->v, &e->lay, e->buffer->path); if (rc) return EDITOR_ERR_ARG;
    /* Binding/default setters reset affinity/scroll. Restore the complete
     * retained state, including an explicit caller wrap override, afterward. */
    rc = layout_set_wrap(&e->lay, live.wrap); if (rc) return rc;
    e->v.state = live;
    keys_reset(&e->keys); e->action = EDITOR_ACTION_NONE;
    e->old_selection = live.selection; editor_restart_blink(e, trace_now_ns());
    return editor_full_layout(e);
}
int editor_register_journal(editor *e, editor_buffer *b, uint64_t id)
{
    int rc = 0;
    if (e->journal) {
        if (b->file) rc = journal_check_base(&b->base);
        if (!rc) rc = journal_set_base(e->journal, id, &b->base);
        if (!rc && !b->file && piece_len(b->tree)) {
            piece_iter it; piece_iter_begin(&it, b->tree, 0); const uint8_t *p; size_t n; uint64_t off = 0;
            while (piece_iter_next(&it, &p, &n)) { rc = journal_insert(e->journal, id, off, p, n); if (rc) break; off += n; }
        }
        if (rc) e->stats.journal_error = rc;
    }
    return rc ? EDITOR_ERR_IO : 0;
}
static int install(editor *e, editor_buffer *b, uint64_t *id)
{
    size_t slot = 0; while (slot < e->buffer_capacity && e->buffers[slot]) slot++;
    if (slot == e->buffer_capacity) return EDITOR_ERR_CAPACITY;
    const char *title = strrchr(b->path, '/'); title = title ? title + 1 : b->path;
    if (!*title) title = "untitled";
    size_t tn = strlen(title); if (tn >= TABS_TITLE_BYTES) tn = TABS_TITLE_BYTES - 1;
    /* Truncation at a grapheme boundary is performed at open, never on switch. */
    size_t end = 0;
    while (end < tn) { size_t n = utf8_grapheme_next((const uint8_t *)title + end, strlen(title) - end); if (n > tn - end) break; end += n; }
    tabs_desc d = {b->tree, &b->undo, b->initial, title, b->path, end, strlen(b->path), false};
    int rc = tabs_open(&e->tabs, &d, &e->v.state, id); if (rc) return EDITOR_ERR_CAPACITY;
    b->id = *id; e->buffers[slot] = b;
    return 0;
}
int editor_add_buffer(editor *e, const char *path, const uint8_t *bytes, size_t len, uint64_t *id)
{
    if (!e || !id || view_busy(&e->v) || tabs_count(&e->tabs) == e->tabs.capacity) return EDITOR_ERR_ARG;
    if (e->error || e->stats.journal_error) return EDITOR_ERR_IO;
    editor_retire_buffers(e);
    editor_buffer *b = NULL; int rc = editor_buffer_prepare(e, path, bytes, len, &b);
    if (!rc) rc = editor_register_journal(e, b, e->tabs.next_id);
    if (!rc) rc = install(e, b, id);
    if (!rc) rc = editor_activate(e);
    if (rc && b && !b->id) editor_buffer_destroy(b);
    return rc;
}
int editor_select_tab(editor *e, size_t index)
{
    if (!e || view_busy(&e->v)) return EDITOR_ERR_ARG;
    int rc = tabs_select(&e->tabs, index, &e->v.state); return rc ? EDITOR_ERR_ARG : editor_activate(e);
}
int editor_cycle_tab(editor *e, bool reverse)
{
    if (!tabs_count(&e->tabs)) return 0;
    int rc = tabs_mru_step(&e->tabs, reverse, &e->v.state); return rc ? rc : editor_activate(e);
}
static void closed_wait(editor *e, editor_buffer *b)
{
    if (!b->wait_token) return;
    for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) if (e->waits[i].token == b->wait_token) {
        if (--e->waits[i].remaining == 0) {
            if (e->cfg.server) (void)ipc_server_report_closed(e->cfg.server, b->wait_token);
            e->waits[i] = (editor_wait){0};
        }
        break;
    }
    b->wait_token = 0;
}
int editor_close_tab(editor *e, size_t index)
{
    if (!e || view_busy(&e->v)) return EDITOR_ERR_ARG;
    const tabs_tab *tab = tabs_at(&e->tabs, index); if (!tab) return EDITOR_ERR_ARG;
    if (e->cfg.allow_close && !e->cfg.allow_close(e->cfg.hook_ctx, tab->id, tab->modified)) return 0;
    editor_buffer *b = find_buffer(e, tab->buffer);
    tabs_tab evicted; int rc = tabs_close(&e->tabs, index, &e->v.state, &evicted); if (rc) return rc;
    closed_wait(e, b);
    minimap_fini(&b->map);
    if (evicted.id) find_buffer(e, evicted.buffer)->retired = true;
    return editor_activate(e);
}
int editor_reopen_tab(editor *e)
{
    if (!e || view_busy(&e->v)) return EDITOR_ERR_ARG;
    uint64_t id; int rc = tabs_reopen(&e->tabs, &e->v.state, &id);
    if (rc == TABS_ERR_EMPTY || rc == TABS_ERR_CAPACITY) return 0;
    return rc ? rc : editor_activate(e);
}
const tabs_tab *editor_tab(const editor *e, size_t index) { return e ? tabs_at(&e->tabs, index) : NULL; }
const render_grid *editor_grid(const editor *e) { return e ? &e->grid : NULL; }
void editor_modified(editor *e)
{
    editor_buffer *b = e->buffer;
    if (tabs_count(&e->tabs)) (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), b->saved_lost || b->history_cursor != b->saved_cursor);
}
static int position_buffer(editor *e, editor_buffer *b, uint32_t line, uint32_t col)
{
    uint64_t ln = line ? (uint64_t)line - 1 : 0;
    if (ln >= b->lines) ln = b->lines - 1;
    uint64_t start = piece_line_to_byte(b->tree, ln), end = piece_len(b->tree);
    if (ln + 1 < b->lines) end = piece_line_to_byte(b->tree, ln + 1);
    size_t n = end - start > VIEW_SCAN_BOUND ? VIEW_SCAN_BOUND : (size_t)(end - start);
    int rc = piece_read(b->tree, start, e->indent_bytes, n); if (rc) return rc;
    size_t at = 0; uint64_t cells = 0, target = col ? (uint64_t)col - 1 : 0;
    while (at < n && cells < target && e->indent_bytes[at] != '\n' &&
           !(e->indent_bytes[at] == '\r' && at + 1 < n && e->indent_bytes[at + 1] == '\n')) {
        utf8_cseg seg; utf8_cseg_init(&seg); size_t step = 0; int width = 0;
        rc = utf8_cluster_step(&seg, e->indent_bytes + at, n - at, n - at,
                              end - start == n, &step, &width);
        if (rc != UTF8_G_END) return EDITOR_ERR_CAPACITY;
        uint64_t w = e->indent_bytes[at] == '\t' ? b->style.width - cells % b->style.width : (uint64_t)width;
        if (w > target - cells) break;
        at += step; cells += w;
    }
    if (at == n && end - start > n && cells < target) return EDITOR_ERR_CAPACITY;
    uint32_t gutter = 0;
    if (e->layout_cfg.gutter) {
        gutter = 2; for (uint64_t value = b->lines; value >= 10; value /= 10) gutter++;
        if (gutter > e->text_grid.dims.cols) gutter = e->text_grid.dims.cols;
    }
    uint32_t width = e->text_grid.dims.cols - gutter;
    if (!width) width = 1;
    if (!b->initial.wrap && cells >= width) b->initial.hscroll = cells - width + 1;
    b->initial.selection.cursor = b->initial.selection.anchor = start + at;
    b->initial.first_byte = start; b->initial.first_line = ln;
    b->initial.visual_byte = b->initial.wrap ? start + at : start;
    return 0;
}
int editor_open_request(editor *e, const ipc_request *request, ipc_token token)
{
    if (!e || !request || request->count > IPC_MAX_PATHS || view_busy(&e->v) ||
        (request->stdin_size && !request->stdin_data)) return EDITOR_ERR_ARG;
    if (e->error || e->stats.journal_error) return EDITOR_ERR_IO;
    size_t count = request->count + (request->has_stdin ? 1u : 0u);
    if (!count) count = 1;
    if (count > e->tabs.capacity - tabs_count(&e->tabs) || count > UINT64_MAX - e->tabs.next_id) return EDITOR_ERR_CAPACITY;
    size_t wait_slot = IPC_MAX_CLIENTS;
    if (request->wait && token) {
        for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) if (!e->waits[i].token) { wait_slot = i; break; }
        if (wait_slot == IPC_MAX_CLIENTS) return EDITOR_ERR_CAPACITY;
    }
    editor_retire_buffers(e);
    editor_buffer *prepared[IPC_MAX_PATHS + 1] = {0}; int rc = 0;
    for (size_t i = 0; i < count; i++) {
        const char *path = i < request->count ? request->paths[i].path : NULL;
        if (i < request->count && (!path || !request->paths[i].line || !request->paths[i].col)) { rc = EDITOR_ERR_ARG; break; }
        bool stdin_buffer = request->has_stdin && i == request->count;
        rc = editor_buffer_prepare(e, path, stdin_buffer ? request->stdin_data : NULL,
                                   stdin_buffer ? request->stdin_size : 0, &prepared[i]);
        if (rc) break;
        if (path) { rc = position_buffer(e, prepared[i], request->paths[i].line, request->paths[i].col); if (rc) break; }
    }
    for (size_t i = 0; !rc && i < count; i++) rc = editor_register_journal(e, prepared[i], e->tabs.next_id + i);
    if (rc) { for (size_t i = 0; i < count; i++) editor_buffer_destroy(prepared[i]); return rc; }
    /* All fallible file/memory work precedes tab publication. The reserved tab
     * and buffer slots make this commit allocation-free. */
    for (size_t i = 0; i < count; i++) {
        uint64_t id; rc = install(e, prepared[i], &id);
        if (rc) return rc; /* defensive: all slot/name/id bounds were checked */
        if (wait_slot < IPC_MAX_CLIENTS) prepared[i]->wait_token = token;
    }
    if (wait_slot < IPC_MAX_CLIENTS) e->waits[wait_slot] = (editor_wait){token, count};
    return editor_activate(e);
}
static ipc_result open_request(const ipc_request *request, ipc_token token, void *ctx)
{
    int rc = editor_open_request(ctx, request, token);
    return !rc ? IPC_OK : rc == EDITOR_ERR_CAPACITY ? IPC_LIMIT : IPC_REJECTED;
}
int editor_poll_sources(editor *e)
{
    struct epoll_event events[2];
    if (epoll_wait(e->poll_fd, events, 2, 0) < 0 && errno != EINTR) return EDITOR_ERR_IO;
    (void)work_mailbox_drain(&e->pool, editor_route_work, e);
    editor_retire_buffers(e);
    /* Opening can allocate. Finish queued input and submit its containing
     * frame before admitting setup work, even if the view itself is idle. */
    if (e->cfg.server && !e->queue_count && !view_busy(&e->v) &&
        !(e->dirty && e->frame.last_sequence)) {
        ipc_result rc = ipc_server_drain(e->cfg.server, open_request, e);
        if (rc != IPC_OK) return EDITOR_ERR_IO;
    }
    /* Index refresh is one existing bounded chunk query per UI turn, outside
     * minimap_fill. Pending revisions paint the module's stale tint. */
    if (e->buffer && e->buffer->index) {
        editor_buffer *b = e->buffer; lineidx_src src = editor_source(b);
        (void)lineidx_poll(b->index);
        bool before = lineidx_complete(b->index) && !lineidx_building(b->index);
        if (b->index_dirty && !lineidx_building(b->index)) {
            if (!lineidx_refresh(b->index, &src) && !lineidx_complete(b->index))
                (void)lineidx_seek_line(b->index, &src, b->lines - 1, LINEIDX_CHUNK);
            b->index_dirty = !lineidx_complete(b->index);
        }
        bool ready = lineidx_complete(b->index) && !lineidx_building(b->index);
        if (b->map.stale && ready && (!before || b->map.revision != b->revision || !e->dirty)) {
            int rc = editor_begin_frame(e); if (rc) return rc;
        }
    }
    return e->error;
}
