#include "editor/private.h"
#include "trace/trace.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>

static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static int fail(editor *e, int cause)
{
    e->stats.error_cause = cause;
    e->error = cause > 0 ? EDITOR_ERR_HISTORY : cause;
    return e->error;
}
int editor_begin_frame(editor *e)
{
    if (e->dirty) return 0;
    if (e->grid.frame_id == UINT32_MAX) return RENDER_ERR_FRAME;
    int rc = render_frame_begin(&e->grid, e->grid.frame_id + 1u);
    if (!rc) rc = render_frame_begin(&e->text_grid, e->grid.frame_id);
    if (!rc) rc = render_frame_begin(&e->map_grid, e->grid.frame_id);
    if (!rc) { e->dirty = true; e->frame = (editor_frame){.id = e->grid.frame_id}; }
    return rc;
}
int editor_full_layout(editor *e)
{
    int rc = editor_begin_frame(e); if (rc) return rc;
    rc = render_mark_full(&e->grid); if (rc) return rc;
    e->paint_ready = false;
    view_state s = e->v.state;
    uint32_t hs = s.hscroll > UINT32_MAX ? UINT32_MAX : (uint32_t)s.hscroll;
    layout_viewport vp = {s.first_byte, s.first_line, hs, e->buffer->lines};
    layout_set_cursor_visual(&e->lay, e->visible && e->focused && tabs_count(&e->tabs) ? s.selection.cursor : UINT64_MAX, s.visual_end);
    layout_set_selection(&e->lay, min64(s.selection.cursor, s.selection.anchor), max64(s.selection.cursor, s.selection.anchor));
    e->extra_rows = false; e->full_pending = false;
    /* Establish the incoming gutter/text width before asking for a visual
     * seed; a preceding tab or resize can have different query geometry. */
    rc = layout_begin(&e->lay, e->tree, vp); if (rc) return rc;
    if (s.wrap && s.visual_byte != s.first_byte) {
        layout_wrap_row first; bool approximate;
        rc = layout_visual_row(&e->lay, e->tree, s.visual_byte, 0, &first, &approximate);
        if (!rc) {
            rc = layout_begin_visual(&e->lay, e->tree, vp, &first);
            e->v.state.visual_byte = first.start;
            e->v.state.approximate |= approximate;
        }
    }
    e->v.config.cols = e->lay.text_cols ? e->lay.text_cols : 1;
    return rc;
}
static uint32_t cursor_row(const editor *e, uint64_t byte)
{
    uint32_t r = 0;
    while (r + 1 < e->text_grid.dims.rows && e->row_byte[r + 1] <= byte) r++;
    return r;
}
int editor_refresh_cursor(editor *e, uint64_t old_cursor)
{
    int rc = editor_begin_frame(e); if (rc) return rc;
    e->paint_ready = false;
    view_state s = e->v.state;
    layout_set_cursor_visual(&e->lay, e->visible && e->focused && tabs_count(&e->tabs) ? s.selection.cursor : UINT64_MAX, s.visual_end);
    layout_set_selection(&e->lay, min64(s.selection.cursor, s.selection.anchor), max64(s.selection.cursor, s.selection.anchor));
    if (e->full_pending || !e->lay.src || s.first_byte != e->lay.first_byte || s.first_line != e->lay.first_line ||
        (!s.wrap && s.hscroll != e->lay.hscroll) || (s.wrap && s.visual_byte != e->row_byte[0]))
        return editor_full_layout(e);
    uint32_t a = cursor_row(e, old_cursor), b = cursor_row(e, s.selection.cursor);
    uint32_t lo = a < b ? a : b, hi = (a > b ? a : b) + 1u;
    if (s.selection.cursor != s.selection.anchor || e->old_selection.cursor != e->old_selection.anchor) { lo = 0; hi = e->text_grid.dims.rows; }
    if (layout_busy(&e->lay)) {
        if (lo < e->lay.row || hi > e->lay.row_end) {
            if (e->extra_rows) { if (lo > e->extra_first) lo = e->extra_first; if (hi < e->extra_end) hi = e->extra_end; }
            e->extra_rows = true; e->extra_first = lo; e->extra_end = hi;
        }
        return 0;
    }
    rc = layout_relayout_rows(&e->lay, lo, hi - lo);
    return rc < 0 ? rc : 0;
}
void editor_route_work(const work_msg *msg, void *ctx)
{
    editor *e = ctx;
    if (e->journal && journal_receive(e->journal, msg)) return;
    if (msg->kind == LINEIDX_MSG_PROGRESS) {
        for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers && e->buffers[i] && e->buffers[i]->index)
            (void)lineidx_poll(e->buffers[i]->index);
        return;
    }
    file_msg fm;
    if (file_msg_decode(msg, &fm) == 0) {
        if (fm.f == e->opening) e->open_error = fm.status;
        else if (fm.status && !e->error) e->error = fm.status;
        return;
    }
    if (!e->backend->initialized) return;
    render_event ev = {RENDER_EVENT_WORK, msg->generation, 0, msg};
    int rc = render_backend_event(e->backend, &ev);
    if (rc && rc != RENDER_ERR_UNSUPPORTED && rc != RENDER_ERR_FRAME && rc != RENDER_ERR_STATE && !e->error) e->error = rc;
}
static void platform_event(void *ctx, const plat_event *ev)
{
    editor *e = ctx;
    if (editor_inject(e, ev) && !e->error) e->error = EDITOR_ERR_INPUT_FULL;
    e->pump_stopped = true; plat_quit(&e->platform);
}
static void platform_work(void *ctx)
{
    editor *e = ctx; int rc = editor_poll_sources(e); if (rc) e->error = rc;
    e->pump_stopped = true; plat_quit(&e->platform);
}
static int pump(editor *e, int timeout)
{
    if (e->has_platform) {
        plat_callbacks cb = {.ud = e, .on_event = platform_event, .on_work = platform_work};
        uint64_t before = e->platform.iterations;
        e->platform.quit = false; e->pump_stopped = false;
        int rc = plat_run_for(&e->platform, &cb, timeout);
        e->stats.poll_returns += e->platform.iterations - before;
        if (timeout > 0 && !e->pump_stopped && rc == PLAT_OK) e->stats.poll_returns++;
        if (rc) return fail(e, rc);
    } else if (timeout != 0 && e->pool_ready) {
        struct pollfd p = {e->poll_fd, POLLIN, 0};
        int rc = poll(&p, 1, timeout);
        if (rc >= 0) e->stats.poll_returns++;
        else if (errno != EINTR) return EDITOR_ERR_IO;
    }
    if (e->pool_ready) return editor_poll_sources(e);
    return e->error;
}
static int present(editor *e)
{
    render_backend *b = e->backend;
    if (!b->active || b->presented) return 0;
    int rc = render_backend_present(b, b->active_frame);
    if (rc == RENDER_ERR_BUSY) return 0;
    if (rc) return rc;
    e->active_frame.present_ns = trace_now_ns();
    if (e->active_frame.last_sequence) e->stats.presented_sequence = e->active_frame.last_sequence;
    if (e->cfg.on_present) e->cfg.on_present(e->cfg.hook_ctx, &e->active_frame);
    return 0;
}
static int submit(editor *e)
{
    if (!e->dirty || !e->paint_ready || e->full_pending || layout_busy(&e->lay) || view_busy(&e->v) || e->extra_rows || e->backend->active) return 0;
    size_t count = 0;
    int rc = editor_compose(e); if (rc) return rc;
    rc = render_dirty_strips(&e->grid, e->strips, e->strip_cap, &count); if (rc) return rc;
    trace_record(TRACE_T3_RENDER_DONE, e->grid.frame_id);
    rc = render_backend_submit(e->backend, &e->grid, e->strips, count);
    if (rc == RENDER_ERR_BUSY) return 0;
    if (rc) return rc;
    e->frame.submit_ns = trace_now_ns(); e->active_frame = e->frame;
    if (e->frame.last_sequence) e->stats.submitted_sequence = e->frame.last_sequence;
    e->dirty = false;
    if (e->cfg.on_submit) e->cfg.on_submit(e->cfg.hook_ctx, &e->frame);
    editor_journal_staged(e);
    return 0;
}
static int resize(editor *e)
{
    if (!e->resize_pending || e->backend->active || e->dirty) return 0;
    render_dims dims = e->grid.dims;
    dims.cols = e->resize_w / dims.cell_w; dims.rows = e->resize_h / dims.cell_h;
    if (!dims.cols) dims.cols = 1;
    if (!dims.rows) dims.rows = 1;
    if (dims.cols > e->max_cols) dims.cols = e->max_cols;
    if (dims.rows > e->max_rows) dims.rows = e->max_rows;
    e->resize_pending = false;
    if (dims.cols == e->grid.dims.cols && dims.rows == e->grid.dims.rows) return 0;
    int rc = render_backend_resize(e->backend, dims); if (rc) return rc;
    e->grid.dims = dims;
    e->tab_rows = dims.rows > 1 ? 1u : 0u; e->map_cols = dims.cols >= 24 ? 8u : 0u;
    e->text_grid.dims = dims; e->text_grid.dims.cols -= e->map_cols; e->text_grid.dims.rows -= e->tab_rows;
    e->map_grid.dims.rows = e->text_grid.dims.rows;
    e->v.config.rows = e->text_grid.dims.rows;
    /* Reinit clears cells for a changed compact stride; retain open-time wrap storage. */
    layout_wrap_row *rows = e->lay.wrap_rows, *plan = e->lay.wrap_plan; uint32_t cap = e->lay.wrap_capacity;
    rc = layout_init(&e->lay, &e->text_grid, &e->layout_cfg, e->row_byte, e->row_used); if (rc) return rc;
    e->lay.wrap_rows = rows; e->lay.wrap_plan = plan; e->lay.wrap_capacity = cap;
    e->lay.cfg.tab_width = e->buffer->style.width; e->lay.tab = e->buffer->style.width;
    rc = layout_set_wrap(&e->lay, e->v.state.wrap); if (rc) return rc;
    return editor_full_layout(e);
}
static int nonkey(editor *e, const plat_event *ev)
{
    if (ev->kind == PLAT_EV_CLOSE) { e->quit = true; return 0; }
    if (ev->kind == PLAT_EV_EXPOSE) return editor_full_layout(e);
    if (ev->kind == PLAT_EV_RESIZE) {
        e->resize_w = ev->w; e->resize_h = ev->h; e->resize_pending = true; return 0;
    }
    if (ev->kind == PLAT_EV_FOCUS) {
        e->focused = ev->focused; undo_break_burst(e->undo);
        keys_reset(&e->keys); tabs_mru_release(&e->tabs);
        e->drag_tab = SIZE_MAX; e->map_drag = false;
        editor_restart_blink(e, trace_now_ns());
        return editor_refresh_cursor(e, e->v.state.selection.cursor);
    }
    if (ev->kind == PLAT_EV_KEYMAP) { keys_reset(&e->keys); tabs_mru_release(&e->tabs); return 0; }
    if (ev->kind == PLAT_EV_BUTTON || ev->kind == PLAT_EV_MOTION) return editor_pointer(e, ev);
    return 0;
}
void editor_restart_blink(editor *e, uint64_t now)
{
    e->last_input = now; e->visible = true; e->blinking = e->focused;
    e->next_blink = e->focused ? now + EDITOR_BLINK_NS : 0;
}
static int blink(editor *e, uint64_t now)
{
    if (!e->blinking || now < e->next_blink) return 0;
    if (now - e->last_input >= EDITOR_IDLE_NS) {
        e->blinking = false; e->next_blink = 0;
        if (e->visible) return 0;
        e->visible = true;
    } else { e->visible = !e->visible; e->stats.blinks++; e->next_blink = now + EDITOR_BLINK_NS; }
    return editor_refresh_cursor(e, e->v.state.selection.cursor);
}
static bool runnable(const editor *e)
{
    bool can_drain = !e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES);
    return (e->queue_count && can_drain) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
           (e->dirty && !e->backend->active) || (e->resize_pending && !e->backend->active && !e->dirty) ||
           (e->buffer->index_dirty && !lineidx_building(e->buffer->index));
}
static int wait_timeout(editor *e, int requested, uint64_t now)
{
    if (runnable(e)) return 0;
    uint64_t deadline = e->blinking ? e->next_blink : UINT64_MAX;
    if (e->journal) {
        journal_stats s = journal_get_stats(e->journal);
        if (s.error) e->stats.journal_error = s.error;
        if (s.accepted_sequence > s.durable_sequence) {
            uint64_t next = s.last_sync_ns + UINT64_C(1000000000);
            if (next <= now) next = now + UINT64_C(5000000);
            if (next < deadline) deadline = next;
        }
    }
    if (deadline == UINT64_MAX) return requested;
    uint64_t ms = deadline <= now ? 0 : (deadline - now + UINT64_C(999999)) / UINT64_C(1000000);
    int limit = ms > INT_MAX ? INT_MAX : (int)ms;
    return requested < 0 || requested > limit ? limit : requested;
}
int editor_inject(editor *e, const plat_event *event)
{
    if (!e || !event || event->utf8_len > PLAT_UTF8_MAX) return EDITOR_ERR_ARG;
    /* Closing is control flow, independent of command/view/journal capacity. */
    if (event->kind == PLAT_EV_CLOSE) { e->quit = true; return 0; }
    if (e->queue_count == EDITOR_INPUT_CAP) return EDITOR_ERR_INPUT_FULL;
    size_t at = (e->queue_head + e->queue_count) % EDITOR_INPUT_CAP;
    e->queue[at] = *event; e->queue_count++; return 0;
}
int editor_step(editor *e, int timeout_ms)
{
    if (!e || !e->tree) return EDITOR_ERR_ARG;
    if (e->quit) return EDITOR_CLOSED;
    if (e->error) return e->error;
    if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
    int rc = present(e);
    if (!rc) rc = pump(e, wait_timeout(e, timeout_ms, trace_now_ns()));
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
    if (e->quit) return EDITOR_CLOSED;
    if (rc) return fail(e, rc);
    uint64_t start = trace_now_ns();
    rc = blink(e, start); if (rc) return rc;
    rc = resize(e); if (rc) return rc;
    for (;;) {
        e->stats.input_checks++;
        if (view_busy(&e->v)) rc = editor_continue_action(e);
        else if (e->queue_count && (!e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES))) {
            plat_event ev = e->queue[e->queue_head];
            e->queue_head = (e->queue_head + 1) % EDITOR_INPUT_CAP; e->queue_count--;
            rc = ev.kind == PLAT_EV_KEY ? editor_handle_key(e, &ev) : nonkey(e, &ev);
        } else if (layout_busy(&e->lay)) {
            uint64_t slice = trace_now_ns(); rc = layout_run(&e->lay);
            uint64_t elapsed = trace_now_ns() - slice; e->stats.slices++;
            if (elapsed > e->stats.longest_slice_ns) e->stats.longest_slice_ns = elapsed;
            if (rc >= 0) rc = 0;
        } else if (e->full_pending) {
            rc = editor_full_layout(e);
        } else if (e->extra_rows) {
            e->extra_rows = false;
            rc = layout_relayout_rows(&e->lay, e->extra_first, e->extra_end - e->extra_first);
            if (rc >= 0) rc = 0;
        } else if (e->dirty && !e->paint_ready) rc = editor_paint_prepare(e);
        else break;
        if (rc) return fail(e, rc);
        if (e->quit || trace_now_ns() - start >= EDITOR_SLICE_NS) break;
    }
    rc = submit(e); if (rc) return rc;
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
    rc = present(e);
    if (!rc && e->journal && !e->dirty) {
        int jr = journal_pump(e->journal, trace_now_ns(), false);
        if (jr && jr != JOURNAL_BUSY) e->stats.journal_error = jr;
    }
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
    if (rc) return fail(e, rc);
    if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
    if (e->quit) return EDITOR_CLOSED;
    return runnable(e) ? EDITOR_MORE : EDITOR_OK;
}
int editor_run(editor *e)
{
    int rc; do { rc = editor_step(e, -1); } while (rc == EDITOR_OK || rc == EDITOR_MORE);
    return rc == EDITOR_CLOSED ? 0 : rc;
}
int editor_set_cursor(editor *e, uint64_t byte)
{
    if (!e || byte > piece_len(e->tree) || view_busy(&e->v)) return EDITOR_ERR_ARG;
    uint64_t old = e->v.state.selection.cursor;
    e->old_selection = e->v.state.selection;
    e->v.state.selection.cursor = e->v.state.selection.anchor = byte;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->v.state.first_line = piece_byte_to_line(e->tree, byte);
    e->v.state.first_byte = piece_line_to_byte(e->tree, e->v.state.first_line);
    e->v.state.visual_byte = e->v.state.first_byte; e->v.state.visual_end = false;
    e->v.state.hscroll = 0; undo_break_burst(e->undo);
    return editor_refresh_cursor(e, old);
}
int editor_jump_line(editor *e, uint64_t line)
{
    if (!e) return EDITOR_ERR_ARG;
    if (e->buffer->index) {
        /* Exact piece queries are warm after open; publication is still the
         * prerequisite for line-based benchmark positioning. */
        if (!lineidx_complete(e->buffer->index)) return EDITOR_MORE;
    }
    return editor_set_cursor(e, piece_line_to_byte(e->tree, line));
}
uint64_t editor_length(const editor *e) { return e && tabs_count(&e->tabs) ? piece_len(e->tree) : 0; }
int editor_read(const editor *e, uint64_t off, uint8_t *dst, size_t len) { return e ? piece_read(e->tree, off, dst, len) : EDITOR_ERR_ARG; }
view_state editor_view(const editor *e) { return e ? e->v.state : (view_state){0}; }
editor_stats editor_get_stats(const editor *e)
{
    if (!e) return (editor_stats){0};
    editor_stats s = e->stats;
    s.focused = e->focused; s.blinking = e->blinking; s.cursor_visible = e->visible;
    s.render_active = e->backend->active;
    s.pending = e->dirty || e->queue_count || view_busy(&e->v) || e->resize_pending || e->full_pending || e->extra_rows;
    s.tabs = tabs_count(&e->tabs); s.active_tab = tabs_active_index(&e->tabs); s.minimap_stale = e->buffer->map.stale;
    return s;
}
bool editor_index_complete(editor *e) { return e && (!e->buffer->index || lineidx_complete(e->buffer->index)); }
uint64_t editor_line_count(const editor *e) { return e ? e->buffer->lines : 0; }
int editor_flush(editor *e)
{
    if (!e) return EDITOR_ERR_ARG;
    editor_journal_staged(e);
    if (e->stats.journal_error) return e->stats.journal_error;
    return e->journal ? journal_flush(e->journal) : 0;
}
