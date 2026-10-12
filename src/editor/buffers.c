#include "editor/private.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <sys/epoll.h>

typedef struct piece_block { struct piece_block *next; size_t bin; } piece_block;
_Static_assert(sizeof(piece_block) == 16, "piece payload alignment");
static void *piece_alloc(void *ctx, size_t n)
{
    editor_piece_storage *s = ctx;
    if (n > SIZE_MAX - sizeof(piece_block)) return NULL;
    pthread_mutex_lock(&s->mutex);
    s->requests++;
    if (s->requests == s->fail_request) { pthread_mutex_unlock(&s->mutex); return NULL; }
    /* Exact sizes avoid doubling add chunks/original storage. The fixed
     * class table bounds lookup independently of retired slab count. */
    size_t bin = 0;
    while (bin < s->classes && s->size[bin] != n) bin++;
    if (bin == EDITOR_PIECE_BINS) { pthread_mutex_unlock(&s->mutex); return NULL; }
    if (bin == s->classes) { s->size[bin] = n; s->classes++; }
    piece_block *block = s->free[bin];
    if (block) s->free[bin] = block->next;
    else block = edit_arena_alloc(s->arena, n + sizeof *block, 16);
    if (block) block->bin = bin;
    pthread_mutex_unlock(&s->mutex);
    return block ? block + 1 : NULL;
}
static void piece_free(void *ctx, void *p, size_t n)
{
    if (!p) return;
    editor_piece_storage *s = ctx;
    piece_block *block = (piece_block *)p - 1;
    pthread_mutex_lock(&s->mutex);
    size_t bin = block->bin;
    EDIT_ASSERT(bin < s->classes && s->size[bin] == n);
    block->next = s->free[bin]; s->free[bin] = block;
    pthread_mutex_unlock(&s->mutex);
}
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
static int wait_open_ready(editor *e, editor_buffer *b)
{
    while (!file_open_ready(b->file)) {
        struct pollfd ready = {work_pool_eventfd(&e->pool), POLLIN, 0};
        if (poll(&ready, 1, 100) < 0 && errno != EINTR) return EDITOR_ERR_IO;
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        if (e->error || (e->opening == b->file && e->open_error) || b->source_stale) return EDITOR_ERR_IO;
    }
    return b->source_stale ? EDITOR_ERR_IO : 0;
}
lineidx_src editor_source(editor_buffer *b)
{ return (lineidx_src){b->pending_open ? b->preview : b->tree, piece_len(b->pending_open ? b->preview : b->tree), tree_span, NULL}; }
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
    if (b->map_snapshot) {
        work_cancel(b->pool,b->map_handle);
        while (!work_handle_finished(b->pool,b->map_handle)) sched_yield();
        (void)work_mailbox_bind(b->pool,b->map_handle,0,NULL,NULL);
        piece_snapshot_release(b->map_snapshot);
    }
    file_watch_path_stop(&b->target_watch);
    file_source_release(b->target_source);
    editor_scroll_close(b);
    editor_large_close(b);
    minimap_fini(&b->map);
    if (b->index) lineidx_destroy(b->index);
    if (b->file) file_close(b->file);
    if (b->undo_ready) undo_destroy(&b->undo);
    if (b->tree) piece_destroy(b->tree);
    if (b->preview) piece_destroy(b->preview);
    if (b->piece_storage.ready) pthread_mutex_destroy(&b->piece_storage.mutex);
    edit_arena_free(&b->arena); free(b);
}
/* file_watch_start now lends one process fd. epoll owns one registration per
 * editor; retiring, reloading or failing one buffer must preserve its peers. */
static void unpoll_source(editor *e, editor_buffer *b)
{
    int fd = b->watch_fd;
    b->watch_fd = -1;
    if (fd < 0) return;
    if (fd == e->preferences_watchfd) return;
    for (size_t i = 0; i < e->buffer_capacity; i++)
        if (e->buffers[i] && e->buffers[i] != b && e->buffers[i]->watch_fd == fd) return;
    (void)epoll_ctl(e->poll_fd, EPOLL_CTL_DEL, fd, NULL);
}
void editor_retire_buffers(editor *e)
{
    if (editor_save_active(e) || e->op_count) return; /* staged tree ranges still borrow buffer arenas */
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i] && e->buffers[i]->retired) {
        editor_buffer *b = e->buffers[i];
        b->source_stale = true;
        (void)work_mailbox_bind(&e->pool, b->lg.warm_h, 1, NULL, NULL);
        (void)work_mailbox_bind(&e->pool, b->lg.find_h, b->lg.find_gen, NULL, NULL);
        work_cancel(&e->pool, b->lg.warm_h); work_cancel(&e->pool, b->lg.find_h);
        work_cancel(&e->pool, b->lg.find_retiring_h);
        if (b->index) { lineidx_build_cancel(b->index); (void)lineidx_poll(b->index); }
        unpoll_source(e, b);
        if (editor_scroll_retire(b) == EDITOR_MORE) continue;
        /* file_close has no public logical-retirement/physical-ready split.
         * Retain its complete storage until explicit quiescent shutdown; never
         * guess at the private open/save/check handles or wait in this pump. */
        if (editor_clip_holds(e, b) || (b->save && savectl_get_model(b->save).can_recover_cancel) ||
            b->find_leases || b->map_snapshot || b->file || !work_handle_finished(&e->pool, b->lg.warm_h) ||
            !work_handle_finished(&e->pool, b->lg.find_h) ||
            !work_handle_finished(&e->pool, b->lg.find_retiring_h) || (b->index && lineidx_building(b->index))) continue;
        editor_session_forget(e, b);
        if (b->save) { savectl_close_begin(b->save); if (savectl_destroy(b->save) != SAVECTL_OK) continue; b->save = NULL; }
        editor_buffer_destroy(b); e->buffers[i] = NULL;
        break; /* at most one physically ready buffer per maintenance turn */
    }
}
int editor_buffer_prepare(editor *e, const char *path, const uint8_t *bytes, size_t len, editor_buffer **out)
{
    *out = NULL;
    if ((!bytes && len) || (path && strlen(path) >= IPC_PATH_CAP)) return EDITOR_ERR_ARG;
    editor_buffer *b = calloc(1, sizeof *b); if (!b) return EDITOR_ERR_MEMORY;
    b->pool = &e->pool; b->watch_fd = -1;
    int rc = 0;
    b->base.path = "";
    if (path) {
        memcpy(b->path, path, strlen(path) + 1);
        file_open_opts opts = {.copy_threshold = e->cfg.copy_threshold};
        rc = file_open_begin(&e->pool, path, &opts, &b->file);
        if (rc) goto fail;
        e->opening = b->file; e->open_error = 0;
        while (b->file && (!file_prefix_ready(b->file) ||
               (file_size(b->file) <= FILE_PREFIX_MAX && !file_open_ready(b->file)))) {
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
        if (b->file) {
            /* File's nonblocking worker acquisition has already rejected
             * nonregular descriptors and supplied the canonical identity and
             * owned prefix from one validated opened generation. */
            if (e->journal) {
                size_t prefix_size = 0; const uint8_t *prefix = file_prefix(b->file, &prefix_size);
                rc = journal_base_from_prefix(file_path(b->file), file_prefix_identity(b->file),
                    prefix, prefix_size, &b->base);
                if (rc) goto fail;
            }
            b->watch_fd = file_watch_start(b->file);
            struct epoll_event event = {.events = EPOLLIN, .data.fd = b->watch_fd};
            if (b->watch_fd < 0 || (epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, b->watch_fd, &event) && errno != EEXIST)) {
                rc = EDITOR_ERR_IO; goto fail;
            }
            if (file_check(b->file, NULL)) { rc = EDITOR_ERR_IO; goto fail; }
        }
    }
    size_t content = b->file ? (file_open_mode(b->file) == FILE_MODE_COPY ? (size_t)file_size(b->file) : 0) : len;
    size_t reserve = e->cfg.arena_bytes ? e->cfg.arena_bytes : 64u * 1024u * 1024u;
    if (content > SIZE_MAX - reserve) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = edit_arena_init(&b->arena, reserve + content); if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
    b->piece_storage.arena = &b->arena;
    if (pthread_mutex_init(&b->piece_storage.mutex, NULL)) { rc = EDITOR_ERR_MEMORY; goto fail; }
    b->piece_storage.ready = true;
    piece_allocator a = {&b->piece_storage, piece_alloc, piece_free};
    b->tree = piece_create(&a); if (!b->tree) { rc = EDITOR_ERR_MEMORY; goto fail; }
    b->pending_open = b->file && file_size(b->file) > FILE_PREFIX_MAX;
    if (b->pending_open) {
        size_t prefix_len = 0; const uint8_t *prefix = file_prefix(b->file, &prefix_len);
        b->preview = piece_create(&a); if (!b->preview) { rc = EDITOR_ERR_MEMORY; goto fail; }
        rc = piece_init_copy(b->preview, prefix, prefix_len);
    } else rc = b->file ? file_attach(b->file, b->tree) : piece_init_copy(b->tree, bytes, len);
    if (rc) goto fail;
    if (!b->file) {
        /* edit-bml (P): use the file detector and its existing prefix bound;
         * retain the dominant ending even after all original text is cut. */
        file_prefix_info info;
        file_prefix_scan(bytes ? bytes : (const uint8_t *)"",
            len < FILE_PREFIX_MAX ? len : FILE_PREFIX_MAX, &info);
        b->paste_eol = info.dominant;
    }
    if (e->journal && b->file && (!*b->base.path || file_check(b->file, NULL))) {
        rc = EDITOR_ERR_IO; goto fail;
    }
    if (b->pending_open) {
        if (file_open_mode(b->file) == FILE_MODE_MMAP) editor_large_init(b);
        else { b->lines = piece_line_count(b->preview); b->lg.lines_exact = true; b->lg.warm_done = true; }
    } else editor_large_init(b);
    b->history_cap = e->cfg.history_keys ? e->cfg.history_keys : 32768;
    if (b->history_cap > (SIZE_MAX - 8) / (2 * sizeof(editor_delta))) { rc = EDITOR_ERR_ARG; goto fail; }
    rc = undo_init(&b->undo, b->tree, 2 * b->history_cap + 8); if (rc) goto fail;
    b->undo_ready = true;
    b->history = edit_arena_alloc(&b->arena, b->history_cap * sizeof *b->history, 16);
    /* Soft undo uses the same configured command capacity as text history;
     * reserve before input, never allocate when a selection changes. */
    b->soft_history = edit_arena_alloc(&b->arena, b->history_cap * sizeof *b->soft_history, _Alignof(editor_soft_delta));
    minimap_row *rows = edit_arena_alloc(&b->arena, e->map_capacity * sizeof *rows, 8);
    if (!b->history || !b->soft_history || !rows) { rc = EDITOR_ERR_MEMORY; goto fail; }
    rc = minimap_init(&b->map, rows, e->map_capacity); if (rc) goto fail;
    b->map_rows = rows;
    /* Worker summaries match the published map: pixel-row capacity (edit-d5o). */
    b->map_worker_rows=edit_arena_alloc(&b->arena,e->map_capacity*sizeof *b->map_worker_rows,8);
    if (!b->map_worker_rows || minimap_init(&b->map_worker,b->map_worker_rows,e->map_capacity)) {
        rc=EDITOR_ERR_MEMORY; goto fail;
    }
    piece_snapshot *snap = piece_snapshot_take(b->pending_open ? b->preview : b->tree); if (!snap) { rc = EDITOR_ERR_MEMORY; goto fail; }
    indent_code ir = indent_detect(snap, &b->style); piece_snapshot_release(snap);
    b->detected_style = b->style;
    editor_preferences_buffer(e, b);
    if (ir != INDENT_OK) { rc = EDITOR_ERR_ARG; goto fail; }
    /* Mapped startup's full-size index belongs after prefix submission too.
     * Keep a bounded unbuilt index for preview consumers in the meantime. */
    b->index = lineidx_create(b->pending_open && file_open_mode(b->file) == FILE_MODE_MMAP ?
        piece_len(b->preview) : b->pending_open ? file_size(b->file) : piece_len(b->tree));
    if (!b->index) { rc = EDITOR_ERR_MEMORY; goto fail; }
    if (lineidx_build_reserve(b->index)) { rc = EDITOR_ERR_MEMORY; goto fail; }
    if (!b->pending_open) {
        snap = piece_snapshot_take(b->tree); if (!snap) { rc = EDITOR_ERR_MEMORY; goto fail; }
        rc = lineidx_bind_snapshot(b->index, snap); piece_snapshot_release(snap);
        if (rc) { rc = EDITOR_ERR_MEMORY; goto fail; }
        if (piece_len(b->tree) <= MINIMAP_SMALL_BYTES) {
            lineidx_src src = editor_source(b);
            (void)lineidx_seek_line(b->index, &src, b->lines, MINIMAP_SMALL_BYTES);
        } else {
            snap = piece_snapshot_take(b->tree); if (!snap) { rc = EDITOR_ERR_MEMORY; goto fail; }
            lineidx_src src = {snap, piece_snapshot_len(snap), snapshot_span, snapshot_release};
            if (lineidx_build_start(b->index, &e->pool, &src)) { piece_snapshot_release(snap); rc = EDITOR_ERR_MEMORY; goto fail; }
        }
        editor_large_start(e, b);
    }
    b->initial = (view_state){.selection = {.preferred_col = VIEW_PREFERRED_UNSET},
        .wrap = e->cfg.wrap_mode ? e->cfg.wrap_mode > 0 : view_wrap_default(b->path)};
    rc = editor_scroll_init(e, b); if (rc) goto fail;
    *out = b; return 0;
fail:
    e->opening = NULL;
    unpoll_source(e, b);
    editor_buffer_destroy(b); return rc > 0 ? EDITOR_ERR_IO : rc;
}
int editor_adopt_open(editor *e, editor_buffer *b)
{
    if (b->retired || b->source_stale || !b->pending_open || !file_open_ready(b->file)) return 0;
    if (b == e->buffer && (e->dirty || e->backend->active || editor_activation_busy(e))) return 0;
    if (file_check(b->file, NULL) || (e->journal && !*b->base.path)) {
        if (!b->id) return EDITOR_ERR_IO; /* unpublished preparation fails locally */
        b->operation_error = FILE_ERR_CHANGED; b->operation_kind = FILE_MSG_OPEN_FAILED;
        return editor_check_sources(e);
    }
    int rc = file_attach(b->file, b->tree); if (rc) return EDITOR_ERR_IO;
    b->pending_open = false;
    b->revision++; /* invalidate the preview's resident scroll source */
    editor_large_init(b);
    if (file_open_mode(b->file) == FILE_MODE_MMAP) {
        lineidx_destroy(b->index);
        b->index = lineidx_create(piece_len(b->tree));
        if (!b->index) return EDITOR_ERR_MEMORY;
    }
    piece_snapshot *snap = piece_snapshot_take(b->tree); if (!snap) return EDITOR_ERR_MEMORY;
    rc = lineidx_bind_snapshot(b->index, snap); piece_snapshot_release(snap);
    if (rc) return EDITOR_ERR_MEMORY;
    snap = piece_snapshot_take(b->tree); if (!snap) return EDITOR_ERR_MEMORY;
    lineidx_src src = {snap, piece_snapshot_len(snap), snapshot_span, snapshot_release};
    if (lineidx_build_start(b->index, &e->pool, &src)) { piece_snapshot_release(snap); return EDITOR_ERR_MEMORY; }
    /* Submit before the remaining open-path reservations/warm preparation. */
    if (lineidx_build_reserve(b->index)) return EDITOR_ERR_MEMORY;
    editor_large_start(e, b);
    if (b == e->buffer) { rc = editor_activate(e); if (rc) return rc; }
    piece_destroy(b->preview); b->preview = NULL;
    rc = editor_save_setup(e, b); if (rc) { b->save_error = b->operation_error = rc; b->save_errno = errno; }
    editor_session_publish(e, b);
    return 0;
}
static editor_buffer *find_buffer(editor *e, piece_tree *tree)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i] && e->buffers[i]->tree == tree) return e->buffers[i];
    return e->empty;
}
bool editor_activation_busy(const editor *e)
{ return editor_clip_busy(e) || editor_save_leased(e) || e->find_replace_active || view_busy(&e->v) || e->action == EDITOR_ACTION_REPLAY_WORK || e->action == EDITOR_ACTION_GOTO; }
int editor_activate(editor *e)
{
    if (editor_activation_busy(e)) return EDITOR_ERR_ARG;
    e->drag_tab = SIZE_MAX; e->drag_id = 0; e->map_drag = false;
    e->map_drag_id = e->map_drag_generation = 0;
    e->find_scroll=e->visual_seed_valid=false;
    e->text_drag = e->wheel_pending = false; e->click_ns = 0; e->wheel.subrow_q8 = 0; e->wrap_wheel_rows = 0;
    const tabs_tab *tab = tabs_active(&e->tabs);
    view_state live = e->v.state;
    e->buffer = tab ? find_buffer(e, tab->buffer) : e->empty;
    bool config_wrap = e->buffer->config_wrap;
    editor_preferences_buffer(e, e->buffer);
    if (e->preferences.wrap_set) live.wrap = e->preferences.wrap;
    else if (config_wrap) live.wrap = e->buffer->initial.wrap;
    if (e->buffer->source_stale) e->buffer->prompt_ns = 0;
    if (!e->buffer->map.initialized) {
        int rc = minimap_init(&e->buffer->map, e->buffer->map_rows, e->map_capacity); if (rc) return rc;
    }
    e->tree = e->buffer->pending_open ? e->buffer->preview : e->buffer->tree; e->undo = &e->buffer->undo;
    view_config vc = {e->buffer->style.width, e->text_grid.dims.rows, 1, NULL, NULL};
    e->lay.row = e->lay.row_end; /* discard the outgoing layout continuation */
    e->lay.src = NULL; e->lay.cfg.tab_width = e->buffer->style.width; e->lay.tab = e->buffer->style.width;
    view_init(&e->v, e->tree, &vc);
    int rc = view_wrap_file(&e->v, &e->lay, e->buffer->path); if (rc) return EDITOR_ERR_ARG;
    /* Binding/default setters reset affinity/scroll. Restore the complete
     * retained state, including an explicit caller wrap override, afterward. */
    rc = layout_set_wrap(&e->lay, live.wrap); if (rc) return rc;
    e->v.state = live;
    e->geometry_generation++; /* wrap/tab-width geometry belongs to this activation */
    /* Text rows exclude the find panel's rows (edit-73j). */
    e->text_grid.dims.rows = e->grid.dims.rows - e->tab_rows - e->panel_rows + (live.wrap ? 0u : 1u);
    e->v.config.rows = e->grid.dims.rows - e->tab_rows - e->panel_rows;
    e->go.active = false;
    keys_reset(&e->keys); e->action = EDITOR_ACTION_NONE;
    e->old_selection = live.selection; editor_restart_blink(e, trace_now_ns());
    if (!live.wrap) {
        rc = editor_scroll_resize(e); if (rc) return rc;
        if (live.first_byte != e->buffer->scroll.first_byte) {
            rc = editor_scroll_seek(e, live.first_byte); if (rc) return rc;
        }
    }
    rc = editor_full_layout(e);
    if (!rc && !e->journal) editor_session_metadata(e);
    return rc ? rc : editor_journal_changed(e);
}
static int protected_record(editor *e, uint32_t type, uint64_t id, const uint8_t *data, size_t size)
{
    int rc;
    while ((rc = journal_append_protected(e->journal, type, id, data, size)) == JOURNAL_BUSY) {
        rc = journal_flush(e->journal); if (rc) return rc;
    }
    return rc;
}
int editor_register_journal(editor *e, editor_buffer *b, uint64_t id)
{
    int rc = 0;
    if (e->journal) {
        if (b->file) rc = file_check(b->file, NULL);
        uint8_t data[JOURNAL_BASE_MAX]; size_t size = 0;
        if (!rc) rc = journal_encode_base(&b->base, data, sizeof data, &size);
        if (!rc) rc = protected_record(e, JOURNAL_BASE, id, data, size);
        if (!rc && !b->file && piece_len(b->tree)) {
            piece_iter it; piece_iter_begin(&it, b->tree, 0); const uint8_t *p; size_t n; uint64_t off = 0;
            while (!rc && piece_iter_next(&it, &p, &n)) {
                size_t progress = 0;
                while (progress < n) {
                    rc = journal_insert_step(e->journal, id, off, p, n, &progress);
                    if (rc == JOURNAL_BUSY) { rc = journal_flush(e->journal); if (rc) break; }
                    else if (rc) break;
                }
                off += n;
            }
        }
        if (!rc && *b->path) rc = protected_record(e, JOURNAL_TARGET, id, (const uint8_t *)b->path, strlen(b->path) + 1);
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
    b->id = *id; b->source_generation = 1; b->session_slot = slot; e->buffers[slot] = b;
    return 0;
}
int editor_add_buffer(editor *e, const char *path, const uint8_t *bytes, size_t len, uint64_t *id)
{
    if (!e || !id || editor_activation_busy(e) || tabs_count(&e->tabs) == e->tabs.capacity) return EDITOR_ERR_ARG;
    if (e->error || e->stats.journal_error) return EDITOR_ERR_IO;
    editor_retire_buffers(e);
    size_t free_slot = 0;
    while (free_slot < e->buffer_capacity && e->buffers[free_slot]) free_slot++;
    if (free_slot == e->buffer_capacity) return EDITOR_ERR_CAPACITY;
    editor_buffer *b = NULL; int rc = editor_buffer_prepare(e, path, bytes, len, &b);
    if (!rc) rc = editor_register_journal(e, b, e->tabs.next_id);
    if (!rc) rc = install(e, b, id);
    if (!rc) rc = editor_activate(e);
    if (!rc && b->pending_open && file_open_mode(b->file) == FILE_MODE_MMAP) {
        /* Retain mapped startup's full-tree return contract, including immediate
         * byte jumps and snapshots. Input remains queued while the bounded
         * prefix frame is submitted and retired, then adoption is maintenance. */
        e->opening = b->file; e->open_error = 0;
        while (e->dirty || e->backend->active) {
            rc = editor_step(e, 1);
            if (rc != EDITOR_OK && rc != EDITOR_MORE) break;
            rc = e->open_error ? EDITOR_ERR_IO : 0;
            if (rc) break;
        }
        if (!rc && b->pending_open) rc = wait_open_ready(e, b);
        if (!rc) rc = editor_adopt_open(e, b);
        e->opening = NULL;
    }
    if (!rc) {
        int save_rc = editor_save_setup(e, b);
        if (save_rc) { b->save_error = b->operation_error = save_rc; b->save_errno = errno; }
        editor_session_metadata(e);
    }
    if (rc && b && !b->id) { unpoll_source(e, b); editor_buffer_destroy(b); }
    return rc;
}
int editor_select_tab(editor *e, size_t index)
{
    if (!e || editor_activation_busy(e)) return EDITOR_ERR_ARG;
    int rc = tabs_select(&e->tabs, index, &e->v.state); return rc ? EDITOR_ERR_ARG : editor_activate(e);
}
int editor_cycle_tab(editor *e, bool reverse)
{
    if (!e || editor_activation_busy(e)) return EDITOR_ERR_ARG;
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
    if (!e || editor_activation_busy(e)) return EDITOR_ERR_ARG;
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
    if (!e || editor_activation_busy(e)) return EDITOR_ERR_ARG;
    uint64_t id; int rc = tabs_reopen(&e->tabs, &e->v.state, &id);
    if (rc == TABS_ERR_EMPTY || rc == TABS_ERR_CAPACITY) return 0;
    return rc ? rc : editor_activate(e);
}
const tabs_tab *editor_tab(const editor *e, size_t index) { return e ? tabs_at(&e->tabs, index) : NULL; }
const render_grid *editor_grid(const editor *e) { return e ? &e->grid : NULL; }
void editor_modified(editor *e)
{
    editor_buffer *b = e->buffer;
    if (b->save) savectl_modified(b->save);
    editor_session_publish(e, b);
    if (tabs_count(&e->tabs)) (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), b->saved_lost || b->history_cursor != b->saved_cursor);
}
static int position_buffer(editor *e, editor_buffer *b, uint32_t line, uint32_t col)
{
    /* Keep the requested coordinates until acquisition and a worker seek
     * certify the source. Density estimates cannot clamp launch intent. */
    if (b->pending_open && (line > 1 || col > 1)) {
        int rc = wait_open_ready(e, b); if (rc) return rc;
        rc = editor_adopt_open(e, b); if (rc) return rc;
    }
    piece_tree *source = b->pending_open ? b->preview : b->tree;
    uint64_t ln = line ? (uint64_t)line - 1 : 0;
    uint64_t start = 0, end = piece_len(source);
    if (ln) {
        piece_snapshot *snap = piece_snapshot_take(source);
        if (!snap) return EDITOR_ERR_MEMORY;
        lineidx_src owned = {snap, piece_snapshot_len(snap), snapshot_span, snapshot_release};
        lineidx_build_cancel(b->index);
        while (lineidx_seek_start_owned(b->index, &e->pool, &owned, ln, SIZE_MAX)) {
            (void)lineidx_poll(b->index);
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            if (e->error || b->source_stale || file_snapshot_faulted(snap)) {
                piece_snapshot_release(snap); return EDITOR_ERR_IO;
            }
            nanosleep(&(struct timespec){0, 100000}, NULL);
        }
        lineidx_result target;
        while (!lineidx_seek_result(b->index, &target)) {
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            if (e->error || b->source_stale || file_changed(b->file)) return EDITOR_ERR_IO;
            nanosleep(&(struct timespec){0, 100000}, NULL);
        }
        /* Seeking stops as soon as the requested LF is found, including
         * before the target chunk is globally counted. Its offset itself is
         * the proof; only a proven EOF may clamp the requested line. */
        while (lineidx_building(b->index)) {
            (void)lineidx_poll(b->index);
            nanosleep(&(struct timespec){0, 100000}, NULL);
        }
        start = target.value;
        lineidx_result total = lineidx_line_count(b->index);
        if (total.exact && ln >= total.value) {
            ln = total.value - 1u;
            lineidx_src current = editor_source(b);
            lineidx_result anchor = lineidx_line_to_byte(b->index, &current, ln);
            if (!anchor.exact) return EDITOR_ERR_IO;
            start = anchor.value;
        }
        b->index_dirty = !lineidx_complete(b->index);
    }
    size_t n = end - start > VIEW_SCAN_BOUND ? VIEW_SCAN_BOUND : (size_t)(end - start);
    int rc = piece_read(source, start, e->indent_bytes, n); if (rc) return rc;
    /* The seek proves the target line; this already resident bounded sample
     * proves following rows too. A low density estimate must not suppress
     * the certified target viewport while the full index is still pending. */
    uint64_t proven_lines = ln + 1u;
    for (size_t i = 0; i < n; i++) proven_lines += e->indent_bytes[i] == '\n' ? 1u : 0u;
    if (b->lines < proven_lines) b->lines = proven_lines;
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
    if (!b->initial.wrap) {
        /* Owned seek already certified this byte/line pair. Preserve its
         * ordinal while the global index is partial; a density label must
         * not replace an explicit launch coordinate. */
        b->scroll.first_byte = start; b->scroll.first_line = b->scroll.anchor_line = ln;
        b->scroll.request = SCROLL_READY; b->scroll.pending_rows = 0;
        b->scroll_display = b->scroll; b->scroll_display_stale = false;
    }
    return 0;
}
int editor_open_request(editor *e, const ipc_request *request, ipc_token token)
{
    if (!e || !request || request->count > IPC_MAX_PATHS || editor_activation_busy(e) ||
        (request->stdin_size && !request->stdin_data)) return EDITOR_ERR_ARG;
    if (e->error || e->stats.journal_error) return EDITOR_ERR_IO;
    size_t count = request->count + (request->has_stdin ? 1u : 0u);
    /* Startup already recovered the requested associations. Reuse them before
     * opening new requests; an argument-free launch keeps the restored session. */
    if (e->startup_restored && !request->has_stdin && tabs_count(&e->tabs)) {
        e->startup_restored = false;
        bool restored = !request->count;
        size_t selected = tabs_active_index(&e->tabs);
        if (request->count) {
            restored = true;
            for (size_t i = 0; i < request->count; i++) {
                bool found = false;
                char canonical[IPC_PATH_CAP];
                const char *path = realpath(request->paths[i].path, canonical) ? canonical : request->paths[i].path;
                for (size_t j = 0; j < tabs_count(&e->tabs); j++) {
                    const tabs_tab *tab = tabs_at(&e->tabs, j);
                    char other[IPC_PATH_CAP]; const char *target = realpath(tab->path, other) ? other : tab->path;
                    if (!strcmp(path, target)) { found = true; selected = j; break; }
                }
                if (!found) { restored = false; break; }
            }
        }
        if (restored && !request->wait) return editor_select_tab(e, selected);
    }
    if (!count) count = 1;
    if (count > e->tabs.capacity - tabs_count(&e->tabs) || count > UINT64_MAX - e->tabs.next_id) return EDITOR_ERR_CAPACITY;
    size_t wait_slot = IPC_MAX_CLIENTS;
    if (request->wait && token) {
        for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) if (!e->waits[i].token) { wait_slot = i; break; }
        if (wait_slot == IPC_MAX_CLIENTS) return EDITOR_ERR_CAPACITY;
    }
    editor_retire_buffers(e);
    size_t available = 0;
    for (size_t i = 0; i < e->buffer_capacity; i++) available += e->buffers[i] == NULL;
    if (count > available) return EDITOR_ERR_CAPACITY;
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
    if (rc) {
        for (size_t i = 0; i < count; i++) if (prepared[i]) {
            unpoll_source(e, prepared[i]); editor_buffer_destroy(prepared[i]);
        }
        return rc;
    }
    /* All fallible file/memory work precedes tab publication. The reserved tab
     * and buffer slots make this commit allocation-free. */
    for (size_t i = 0; i < count; i++) {
        uint64_t id; rc = install(e, prepared[i], &id);
        if (rc) return rc; /* defensive: all slot/name/id bounds were checked */
        if (wait_slot < IPC_MAX_CLIENTS) prepared[i]->wait_token = token;
    }
    for (size_t i = 0; i < count; i++) { rc = editor_save_setup(e, prepared[i]); if (rc) { prepared[i]->save_error = prepared[i]->operation_error = rc; prepared[i]->save_errno = errno; } }
    if (wait_slot < IPC_MAX_CLIENTS) e->waits[wait_slot] = (editor_wait){token, count};
    return editor_activate(e);
}
static ipc_result open_request(const ipc_request *request, ipc_token token, void *ctx)
{
    int rc = editor_open_request(ctx, request, token);
    return !rc ? IPC_OK : rc == EDITOR_ERR_CAPACITY ? IPC_LIMIT : IPC_REJECTED;
}
static void sweep_waits(editor *e)
{
    ipc_token dropped[IPC_MAX_CLIENTS]; size_t count = 0;
    for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) {
        ipc_token token = e->waits[i].token;
        if (token && !ipc_server_token_live(e->cfg.server, token)) {
            dropped[count++] = token;
            e->waits[i] = (editor_wait){0};
        }
    }
    /* Fixed reservations bound this pass. Clear the member side too, so
     * closing/reopening a retained buffer cannot carry a dead association. */
    for (size_t i = 0; count && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || !b->wait_token) continue;
        for (size_t at = 0; at < count; at++) if (b->wait_token == dropped[at]) {
            b->wait_token = 0; break;
        }
    }
}
int editor_poll_sources(editor *e)
{
    int preferences_rc = editor_preferences_poll(e); if (preferences_rc) return preferences_rc;
    struct epoll_event events[16];
    int event_count = epoll_wait(e->poll_fd, events, 16, 0);
    if (event_count < 0 && errno != EINTR) return EDITOR_ERR_IO;
    for (int at = 0; at < event_count; at++) for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (b && !b->retired && b->file && b->watch_fd == events[at].data.fd) {
            b->watch_pending = true; (void)file_watch_poll(b->file);
        }
    }
    size_t messages = work_mailbox_drain(&e->pool, editor_route_work, e);
    if (e->journal_pool_ready && !editor_save_leased(e)) messages += work_mailbox_drain(&e->journal_pool, editor_route_work, e);
    if (messages) for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (b && !b->retired && b->file && b->watch_pending && !b->source_stale) (void)file_watch_poll(b->file);
    }
    int raster_rc = raster_poll_completions(e->backend);
    if (raster_rc) return raster_rc;
    editor_retire_buffers(e);
    editor_save_tick(e);
    int command_rc = editor_file_command_service(e); if (command_rc) return command_rc;
    file_watch_dispatch();
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (b && !b->retired && b->target_rebound && file_watch_path_take(&b->target_watch)) {
            file_id identity;
            if (file_id_stat_path(b->path, &identity) || !b->save ||
                file_id_diff(&identity, savectl_target_identity(b->save))) {
                b->operation_kind = FILE_MSG_OPEN_FAILED;
                b->operation_error = FILE_ERR_CHANGED;
            }
        }
    }
    int source_rc = editor_check_sources(e); if (source_rc) return source_rc;
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i]) {
        int rc = editor_adopt_open(e, e->buffers[i]); if (rc) return rc;
    }
    /* Opening can allocate. Finish queued input and submit its containing
     * frame before admitting setup work, even if the view itself is idle. */
    if (e->cfg.server && !e->opening && !e->queue_count && !editor_activation_busy(e) &&
        !(e->dirty && e->frame.last_sequence)) {
        uint64_t drops = e->cfg.server->wait_drops;
        ipc_result rc = ipc_server_drain(e->cfg.server, open_request, e);
        if (e->cfg.server->wait_drops != drops) sweep_waits(e);
        if (rc != IPC_OK) return EDITOR_ERR_IO;
    }
    /* One eligible index per turn, including inactive tabs. Replay's checkpoint
     * exposes provisional tree bytes, so leave its index alone until commit. */
    for (size_t checked = 0; checked < e->buffer_capacity; checked++) {
        size_t at = e->index_cursor;
        e->index_cursor = (at + 1u) % e->buffer_capacity;
        editor_buffer *b = e->buffers[at];
        if (!b || !b->index || (!b->index_dirty && !lineidx_building(b->index)) || b->retired || b->pending_open || b->source_stale ||
            (b == e->buffer && e->action == EDITOR_ACTION_REPLAY_WORK)) continue;
        lineidx_src src = editor_source(b);
        (void)lineidx_poll(b->index);
        bool before = lineidx_complete(b->index) && !lineidx_building(b->index);
        if (b->index_dirty && !lineidx_building(b->index)) {
            (void)lineidx_refresh(b->index, &src);
            if (!lineidx_complete(b->index)) {
                piece_snapshot *snap = piece_snapshot_take(b->tree);
                if (snap) {
                    lineidx_src immutable = {snap, piece_snapshot_len(snap), snapshot_span, snapshot_release};
                    if (lineidx_build_start_prepaid(b->index, &e->pool, &immutable)) {
                        piece_snapshot_release(snap);
                        /* Unreserved legacy/injected indexes still make bounded
                         * progress, independent of estimated line counts. */
                        (void)lineidx_seek_line(b->index, &src, UINT64_MAX, LINEIDX_CHUNK);
                    }
                } else (void)lineidx_seek_line(b->index, &src, UINT64_MAX, LINEIDX_CHUNK);
            }
        }
        bool ready = lineidx_complete(b->index) && !lineidx_building(b->index);
        if (ready) {
            lineidx_result total = lineidx_line_count(b->index);
            b->lines = total.value; b->index_dirty = false;
        }
        if (b == e->buffer && b->map.stale && ready && (!before || b->map.revision != b->revision || !e->dirty)) {
            int rc = editor_begin_frame(e); if (rc) return rc;
        }
        break;
    }
    return e->error;
}

/* Explicit Reload and clean-source refresh share replacement, view clamping,
 * empty undo history, journal reconciliation and logical tab identity. */
static int reload_source(editor *e, editor_buffer *b, size_t tab_index)
{
    size_t slot = 0;
    while (slot < e->buffer_capacity && e->buffers[slot]) slot++;
    if (slot == e->buffer_capacity || (b->file && file_save_busy(b->file)) || e->op_count) return 0;
    editor_buffer *replacement = NULL;
    int rc = editor_buffer_prepare(e, b->path, NULL, 0, &replacement);
    if (rc) return 0;
    if (replacement->pending_open) {
        e->opening = replacement->file; e->open_error = 0;
        rc = wait_open_ready(e, replacement);
        if (!rc) rc = editor_adopt_open(e, replacement);
        e->opening = NULL;
    }
    if (rc) { unpoll_source(e, replacement); editor_buffer_destroy(replacement); return 0; }
    bool active = b == e->buffer;
    editor_buffer *previous_buffer = e->buffer;
    view_state previous = e->v.state;
    const tabs_tab *tab = tabs_at(&e->tabs, tab_index);
    view_state previous_tab = tab->state, state = active ? previous : previous_tab;
    bool modified = tab->modified;
    uint64_t len = piece_len(replacement->tree);
    if (state.selection.cursor > len) state.selection.cursor = len;
    if (state.selection.anchor > len) state.selection.anchor = len;
    if (state.first_byte > len) state.first_byte = len;
    state.first_line = piece_byte_to_line(replacement->tree, state.first_byte);
    state.first_byte = piece_line_to_byte(replacement->tree, state.first_line);
    if (state.visual_byte > len) state.visual_byte = state.first_byte;
    replacement->session_slot = slot;
    replacement->id = b->id; replacement->source_generation = b->source_generation + 1;
    replacement->wait_token = b->wait_token; replacement->initial = state;
    /* Stage the replacement tab/state for the complete checkpoint, rolling it
     * back on failure. Retired generations retain leases, but are not recovery
     * state. Inactive tabs must checkpoint their clamped replacement view too. */
    e->buffers[slot] = replacement; b->retired = true;
    if (active) e->buffer = replacement;
    rc = tabs_replace(&e->tabs, tab_index, replacement->tree, &replacement->undo, &state, &e->v.state);
    EDIT_ASSERT(rc == 0); /* replacement keeps the existing logical tab */
    if (e->journal) { rc = journal_flush(e->journal); if (!rc) rc = editor_checkpoint(e); }
    if (rc) {
        e->buffers[slot] = NULL; b->retired = false;
        (void)tabs_replace(&e->tabs, tab_index, b->tree, &b->undo, &previous_tab, &e->v.state);
        (void)tabs_set_modified(&e->tabs, tab_index, modified);
        e->buffer = previous_buffer; e->v.state = previous;
        unpoll_source(e, replacement);
        editor_buffer_destroy(replacement); return 0;
    }
    lineidx_build_cancel(b->index);
    work_cancel(&e->pool, b->lg.warm_h); work_cancel(&e->pool, b->lg.find_h);
    b->lg.find_running = false;
    b->wait_token = 0;
    editor_session_forget(e, b);
    rc = editor_save_setup(e, replacement); if (rc) replacement->operation_error = rc;
    unpoll_source(e, b);
    if (active) return editor_activate(e);
    editor_session_metadata(e);
    return editor_journal_changed(e);
}

int editor_source_resolve(editor *e, bool reload)
{
    if (!e || !e->buffer->source_stale) return EDITOR_ERR_ARG;
    e->source_choice = reload ? 2u : 1u;
    e->source_choice_id = e->buffer->id; e->source_choice_generation = e->buffer->source_generation; return 0;
}
int editor_check_sources(editor *e)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || b->retired || (!b->file && !b->target_rebound) || editor_save_active(e)) continue;
        bool clean = !b->saved_lost && b->history_cursor == b->saved_cursor;
        if (b->source_stale && !clean) continue;
        if (b->watch_pending && b->file) (void)file_watch_poll(b->file);
        bool original_changed = b->file && (!b->target_rebound || file_open_mode(b->file) == FILE_MODE_MMAP) && file_changed(b->file);
        if (!b->source_stale && !original_changed && b->operation_kind != FILE_MSG_OPEN_FAILED) continue;
        if (clean && !b->pending_open && (b->operation_kind != FILE_MSG_OPEN_FAILED || b->target_rebound)) {
            /* Setup can allocate. Finish input through its containing frame,
             * and release active view/save/clipboard leases before replacing. */
            if (editor_activation_busy(e) || e->backend->active || e->queue_count ||
                (e->dirty && e->frame.last_sequence) || e->op_count) continue;
            size_t tab_index = 0;
            while (tab_index < tabs_count(&e->tabs) && tabs_at(&e->tabs, tab_index)->id != b->id) tab_index++;
            if (tab_index == tabs_count(&e->tabs)) continue; /* refresh retained closed tabs when reopened */
            int rc = reload_source(e, b, tab_index);
            if (rc || b->retired) return rc; /* at most one replacement in a maintenance turn */
            /* Failed acquisition/reconciliation retains the old resources and
             * exposes the existing recovery choices rather than losing text. */
        }
        if (b->source_stale) continue;
        b->source_stale = true; b->prompt_ns = 0; b->lg.find_running = false;
        lineidx_build_cancel(b->index);
        work_cancel(&e->pool, b->lg.warm_h); work_cancel(&e->pool, b->lg.find_h);
        if (b == e->buffer) {
            if (e->action == EDITOR_ACTION_REPLAY_WORK) {
                undo_clear(e->undo); b->history_count = b->history_cursor = b->history_head = 0;
                b->soft_count = b->soft_cursor = b->soft_head = 0;
                b->soft_base_redo = 0;
                b->mark_valid = false;
                b->last_key_valid = false;
                b->saved_lost = true;
            }
            undo_break_burst(e->undo); e->action = EDITOR_ACTION_NONE; e->v.busy = false;
            e->lay.row = e->lay.row_end; e->extra_rows = e->full_pending = false;
            int rc = editor_begin_frame(e); if (rc) return rc;
            e->paint_ready = true; rc = render_mark_full(&e->grid); if (rc) return rc;
        }
    }
    if (!e->source_choice || editor_save_active(e)) return 0;
    unsigned choice = e->source_choice; e->source_choice = 0;
    editor_buffer *b = e->buffer;
    if (!b->source_stale || b->id != e->source_choice_id || b->source_generation != e->source_choice_generation) return 0;
    if (choice == 1) {
        if (b->pending_open) return 0; /* failed acquisition cannot become editable */
        int rc = b->target_rebound && (!b->file || file_open_mode(b->file) == FILE_MODE_COPY) ? 0 : file_resolve_keep(b->file);
        if (rc) return 0; /* Unsafe mapped keep retains the visible prompt. */
        journal_base previous = b->base;
        if (e->journal) {
            rc = journal_capture_base(b->path, &b->base);
            if (!rc) rc = journal_flush(e->journal);
            if (!rc) rc = editor_checkpoint(e);
            if (rc) {
                b->base = previous;
                b->base.path = *previous.path ? b->base.captured_path : "";
                return 0;
            }
        }
        if (b->save) {
            file_id identity;
            if (file_id_stat_path(b->save_path, &identity) == FILE_OK)
                (void)savectl_rebase_target(b->save, &identity);
        }
        b->saved_lost = true;
        if (b->target_rebound) { b->operation_kind = 0; b->operation_error = 0; }
        b->source_stale = false; b->prompt_ns = 0; editor_modified(e);
        editor_large_keep(b);
        return editor_full_layout(e);
    }
    return reload_source(e, b, tabs_active_index(&e->tabs));
}
