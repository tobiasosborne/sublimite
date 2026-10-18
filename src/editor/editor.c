#include "editor/private.h"
#include "trace/trace.h"
#include "gl/gl.h"
#include "raster/raster.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sys/epoll.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

static bool runnable(const editor *e);
static int refresh_caret(editor *e);

static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static void record_slice(editor *e, uint64_t start)
{
    uint64_t elapsed = trace_now_ns() - start; e->stats.slices++;
    if (elapsed > e->stats.longest_slice_ns) e->stats.longest_slice_ns = elapsed;
}
bool editor_line_start_before(editor *e, uint64_t end, uint64_t *start)
{
    /* Certify a line start from the bytes, never inspecting over one chunk. */
    uint8_t bytes[4096]; uint64_t pos = end, left = LINEIDX_CHUNK;
    while (pos && left) {
        size_t take = (size_t)min64(pos, min64(left, sizeof bytes));
        pos -= take; left -= take;
        if (piece_read(e->tree, pos, bytes, take)) return false;
        for (size_t i = take; i; i--) if (bytes[i - 1u] == '\n') {
            *start = pos + i; return true;
        }
    }
    if (pos) return false;
    *start = 0; return true;
}
void editor_seed_view_lines(editor *e)
{
    /* Legacy empty-TYPE repair clears view's query cache after a mutation.
     * Layout has already rebased this certified logical-line anchor. Restore
     * it before the continuation so repair never replays the file prefix. */
    view *v = &e->v;
    if (e->buffer->lg.top_estimated || e->lay.src != e->tree ||
        (e->full_pending && !e->repair_line_valid)) return;
    uint64_t byte = e->full_pending ? e->repair_line_byte : e->lay.first_byte;
    uint64_t line = e->full_pending ? e->repair_line_number : e->lay.first_line;
    if (byte > piece_len(e->tree)) return;
    unsigned slot = v->line_next++ % 32u;
    v->lines[slot].byte = byte; v->lines[slot].line = line; v->lines[slot].valid = true;
    uint64_t cursor = view_busy(v) ? v->restore_state.selection.cursor : v->state.selection.cursor;
    if (byte && line && cursor <= byte) {
        /* Left/backspace at the first visible line needs the preceding
         * anchor too. Exclude the newline immediately before this start. */
        uint64_t previous;
        if (editor_line_start_before(e, byte - 1u, &previous)) {
            slot = v->line_next++ % 32u;
            v->lines[slot].byte = previous; v->lines[slot].line = line - 1u;
            v->lines[slot].valid = true;
        }
    }
    if (v->query_kind && v->query_pos < byte) {
        /* Restart the active query through the newly installed cache rather
         * than continuing its pre-anchor scan. The phase/target stay owned
         * by view and it selects an eligible anchor on its next call. */
        v->query_kind = 0;
        v->query_pos = byte;
    }
}
static int layout_slice(editor *e)
{
    layout *l = &e->lay;
    if (l->wrap) return layout_run(l);
    /* Ordinary rows carry their starts forward from the bounded layout scan.
     * Only a clipped long tail needs a query; the partial index bounds that
     * query to a chunk and may supply an approximate start. */
    uint32_t end = l->row_end;
    if (l->phase == 0 && l->row < end) {
        uint64_t line = l->first_line + l->row;
        if (e->buffer->lg.lines_exact && line >= e->buffer->lines) e->row_byte[l->row] = LAYOUT_VOID_ROW;
        else if (e->row_byte[l->row] == LAYOUT_VOID_ROW && e->buffer->index) {
            /* Preserve following rows for a clipped tail close to its newline.
             * Never search farther than one chunk, even with no index. */
            uint64_t pos = l->pos, total = piece_len(e->tree), left = 0;
            if (l->row && e->row_byte[l->row - 1u] != LAYOUT_VOID_ROW &&
                l->row_scanned >= LAYOUT_LONG_LINE && pos >= e->row_byte[l->row - 1u] &&
                pos - e->row_byte[l->row - 1u] == l->row_scanned && pos < total)
                left = total - pos;
            if (left > LINEIDX_CHUNK) left = LINEIDX_CHUNK;
            uint8_t bytes[4096]; bool found = false;
            while (left) {
                size_t take = left < sizeof bytes ? (size_t)left : sizeof bytes;
                if (piece_read(e->tree, pos, bytes, take)) break;
                const uint8_t *nl = memchr(bytes, '\n', take);
                if (nl) {
                    e->row_byte[l->row] = pos + (uint64_t)(nl - bytes) + 1u;
                    found = true; break;
                }
                pos += take; left -= take;
            }
            if (!found) {
                lineidx_src src = editor_source(e->buffer);
                lineidx_result start = lineidx_line_to_byte(e->buffer->index, &src, line);
                /* Approximate byte density does not certify a logical row. */
                if (start.exact) e->row_byte[l->row] = start.value;
                l->approximate |= !start.exact;
            }
        }
    }
    if (l->row < end) l->row_end = l->row + 1u;
    int rc = layout_run(l);
    l->row_end = end;
    return rc == LAYOUT_DONE && layout_busy(l) ? LAYOUT_MORE : rc;
}
static int fail(editor *e, int cause)
{
    e->stats.error_cause = cause;
    e->error = cause > 0 ? EDITOR_ERR_HISTORY : cause;
    return e->error;
}
int editor_begin_frame(editor *e)
{
    e->caret_only = false;
    e->glyph_only = false;
    if (e->dirty) return 0;
    font_async_frame(e->fonts, &e->grid, &e->text_grid, e->backend->active);
    if (e->grid.frame_id == UINT32_MAX) return RENDER_ERR_FRAME;
    int rc = render_frame_begin(&e->grid, e->grid.frame_id + 1u);
    if (!rc) rc = render_frame_begin(&e->text_grid, e->grid.frame_id);
    if (!rc) rc = render_frame_begin(&e->map_grid, e->grid.frame_id);
    if (!rc) { e->dirty = true; e->frame = (editor_frame){.id = e->grid.frame_id}; }
    return rc;
}
int editor_full_layout(editor *e)
{
    e->caret_only = false;
    int rc = editor_begin_frame(e); if (rc) return rc;
    if (e->buffer->source_stale) {
        e->lay.row = e->lay.row_end; e->full_pending = e->extra_rows = false;
        e->paint_ready = true; return render_mark_full(&e->grid);
    }
    rc = render_mark_full(&e->grid); if (rc) return rc;
    e->paint_ready = false;
    view_state s = e->v.state;
    if (!s.wrap) {
        scroll_state planned = e->buffer->scroll_display;
        planned.config = e->buffer->scroll.config;
        rc = scroll_plan_frame(&planned, e->text_grid.dims.cols * e->grid.dims.cell_w,
            e->v.config.rows * e->grid.dims.cell_h, /* visible text rows: no find panel (edit-73j) */
            e->submitted_plan_valid ? &e->submitted_plan : NULL, &e->scroll_plan);
        if (rc) return rc;
    } else {
        e->scroll_plan = (scroll_frame_plan){.first_byte = s.visual_byte,
            .clip_width = e->text_grid.dims.cols * e->grid.dims.cell_w,
            .clip_height = e->v.config.rows * e->grid.dims.cell_h,
            .row_height = e->grid.dims.cell_h, .layout_rows = e->text_grid.dims.rows,
            .full_damage = true};
    }
    uint64_t total = piece_len(e->tree);
    if (s.first_byte > total) {
        /* Defensive: never feed layout a top past EOF; editor_scroll_edit
         * transforms the displayed top with every edit (edit-d50 fix). */
        s.first_line = piece_byte_to_line(e->tree, total);
        s.first_byte = piece_line_to_byte(e->tree, s.first_line);
        e->v.state.first_byte = s.first_byte; e->v.state.first_line = s.first_line;
    }
    if (s.visual_byte > total) s.visual_byte = e->v.state.visual_byte = s.first_byte;
    uint32_t hs = s.hscroll > UINT32_MAX ? UINT32_MAX : (uint32_t)s.hscroll;
    layout_viewport vp = {s.first_byte, s.first_line, hs, e->buffer->lines};
    layout_set_cursor_visual(&e->lay, e->visible && e->focused && tabs_count(&e->tabs) ? s.selection.cursor : UINT64_MAX, s.visual_end);
    layout_set_selection(&e->lay, min64(s.selection.cursor, s.selection.anchor), max64(s.selection.cursor, s.selection.anchor));
    e->extra_rows = false; e->full_pending = false;
    /* Establish the incoming gutter/text width before asking for a visual
     * seed; a preceding tab or resize can have different query geometry. */
    trace_record(TRACE_STARTUP_FIRST_LAYOUT, 0);
    e->lay.line_count_estimated = !e->buffer->lg.lines_exact;
    rc = layout_begin(&e->lay, e->tree, vp); if (rc) return rc;
    if (s.wrap && s.visual_byte != s.first_byte) {
        layout_wrap_row first; bool approximate=false;
        if (e->visual_seed_valid && e->visual_seed.start==s.visual_byte && e->visual_seed.line_start==s.first_byte) {
            first=e->visual_seed; approximate=first.approximate; rc=0;
        } else if (s.visual_byte > s.first_byte && s.visual_byte - s.first_byte > LAYOUT_BYTE_BUDGET)
            /* edit-1vun: the top row continues the line at first_byte (the
             * view's wrapped invariant); beyond layout's resident budget an
             * unanchored query asks piece for the line, which on an unindexed
             * mapping counts newlines from byte 0 on the UI thread. */
            rc = layout_visual_row_anchored(&e->lay, e->tree, s.visual_byte, s.first_byte, e->lay.first_line, &first, &approximate);
        else rc = layout_visual_row(&e->lay, e->tree, s.visual_byte, 0, &first, &approximate);
        if (!rc) {
            rc = layout_begin_visual(&e->lay, e->tree, vp, &first);
            e->v.state.visual_byte = first.start;
            e->v.state.approximate |= approximate;
        }
    }
    e->visual_seed_valid=false;
    e->v.config.cols = e->lay.text_cols ? e->lay.text_cols : 1;
    return rc;
}
static uint32_t cursor_row(const editor *e, uint64_t byte)
{
    uint32_t r = 0;
    while (r + 1 < e->text_grid.dims.rows && e->row_byte[r + 1] <= byte) r++;
    return r;
}
static int refresh_cursor(editor *e, uint64_t old_cursor, bool caret)
{
    editor_large_relabel_top(e); /* edit-wv7: before layout reads the label */
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
    if (!caret && (s.selection.cursor != s.selection.anchor || e->old_selection.cursor != e->old_selection.anchor)) { lo = 0; hi = e->text_grid.dims.rows; }
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
int editor_refresh_cursor(editor *e, uint64_t old_cursor)
{
    e->caret_only = false;
    editor_clip_selection(e);
    int rc = editor_scroll_follow(e); if (rc) return rc;
    return refresh_cursor(e, old_cursor, false);
}
/* The caller already adopted a certified viewport (editor_scroll_place). */
int editor_refresh_placed(editor *e, uint64_t old_cursor)
{
    e->caret_only = false;
    editor_clip_selection(e);
    return refresh_cursor(e, old_cursor, false);
}
void editor_route_work(const work_msg *msg, void *ctx)
{
    editor *e = ctx;
    if (msg->kind == JOURNAL_RETAIN_MESSAGE) return; /* edit-fi2: wake-up; retain.c polls the handle */
    if (editor_fif_receive(e, msg)) return; /* edit-n95: counts only; assembly is a UI service */
    if (editor_font_receive(e, msg)) return;
    if (editor_map_receive(e, msg)) return;
    if (e->find.private_ && findui_accept(&e->find, msg)) {
        if (editor_begin_frame(e)) e->error=EDITOR_ERR_HISTORY;
        e->paint_ready=false; return;
    }
    if (!editor_save_leased(e) && e->journal && journal_receive(e->journal, msg)) { e->journal_waiting = false; return; }
    if (editor_restore_receive(e, msg)) return; /* edit-qct: completion only wakes the loop */
    if (editor_font_event(e, msg)) return;
    if (msg->kind == LINEIDX_MSG_PROGRESS) {
        for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers && e->buffers[i] && e->buffers[i]->index)
            (void)lineidx_poll(e->buffers[i]->index);
        return;
    }
    if (msg->kind == SAVECTL_MESSAGE) {
        for (size_t i = 0; i < e->buffer_capacity; i++)
            if (e->buffers[i] && e->buffers[i]->save && savectl_receive(e->buffers[i]->save, msg)) return;
    }
    if (editor_large_receive(e, msg)) return;
    if (msg->kind == SCROLL_RESIDENT_MSG) return; /* bridge's bound receiver owns adoption */
    file_msg fm;
    if (file_msg_decode(msg, &fm) == 0) {
        if (fm.kind == FILE_MSG_PREFIX_READY) {
            /* UI observation of the worker's completed prefix read. */
            trace_record(TRACE_STARTUP_FILE_PREFIX_READ, 0);
            trace_record(TRACE_T1_DEQUEUE, editor_large_trace_frame(fm.f));
        }
        else if (fm.kind == FILE_MSG_OPEN_READY)
            trace_record(TRACE_T2_MUTATION_DONE, editor_large_trace_frame(fm.f));
        editor_large_file_msg(e, &fm);
        if (fm.f == e->opening) e->open_error = fm.status;
        else for (size_t i = 0; i < e->buffer_capacity; i++) {
            editor_buffer *owner = e->buffers[i];
            if (!owner || owner->file != fm.f) continue;
            if (fm.kind == FILE_MSG_SAVE_DONE && fm.generation != owner->lg.save_gen) break;
            owner->operation_error = fm.status; owner->operation_errno = fm.err_no;
            owner->operation_kind = fm.kind; owner->operation_generation = fm.generation;
            break;
        }
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
static bool gpu_pending(const editor *e)
{
    return (e->backend->info.capabilities & RENDER_CAP_GPU) &&
        e->backend->active && e->backend->presented;
}
static void platform_complete(void *ctx, uint32_t serial, uint64_t ust, uint64_t msc)
{
    editor *e = ctx;
    if (!gpu_pending(e)) return;
    int rc = gl_present_complete(e->backend, serial, ust, msc);
    if (rc && rc != RENDER_ERR_FRAME && rc != RENDER_ERR_STATE) e->error = rc;
}
/* A clean caret frame has no work waiting for completion. Observe its XCB
 * queue on the next existing blink/input turn, without a completion-only wake.
 * When damage is waiting, arm the same epoll set used by platform on_work. */
static int raster_poll_set(editor *e)
{
    int fd = raster_completion_fd(e->backend);
    if (e->caret_only && !e->dirty && !e->queue_count && !e->resize_pending) fd = -1;
    if (fd == e->raster_poll_fd) return 0;
    if (e->raster_poll_fd >= 0 && epoll_ctl(e->poll_fd, EPOLL_CTL_DEL, e->raster_poll_fd, NULL))
        return EDITOR_ERR_IO;
    e->raster_poll_fd = -1;
    if (fd >= 0) {
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
        if (epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &ev)) return EDITOR_ERR_IO;
        e->raster_poll_fd = fd;
    }
    return 0;
}
static int pump(editor *e, int timeout)
{
    int probe_rc = editor_backend_poll_probe(e); if (probe_rc) return probe_rc;
    bool active = e->backend->active;
    int raster_rc = raster_poll_completions(e->backend);
    if (raster_rc) return fail(e, raster_rc);
    if (active && !e->backend->active && runnable(e)) timeout = 0;
    raster_rc = raster_poll_set(e); if (raster_rc) return raster_rc;
    if (gpu_pending(e)) {
        work_msg msg = {.kind = GL_POLL_MESSAGE};
        render_event ev = {RENDER_EVENT_WORK, e->backend->active_frame, 0, &msg};
        int rc = render_backend_event(e->backend, &ev);
        if (rc) return fail(e, rc);
        /* A completed frame may unblock already prepared/input work. */
        if (!e->backend->active) timeout = 0;
    }
    if (e->has_platform && e->queue_count < EDITOR_INPUT_CAP) {
        plat_callbacks cb = {.ud = e, .on_event = platform_event, .on_work = platform_work,
            .on_present_complete = platform_complete};
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
    probe_rc = editor_backend_poll_probe(e); if (probe_rc) return probe_rc;
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
    trace_record(TRACE_STARTUP_FIRST_PRESENT, 0);
    e->startup_presented = true;
    e->active_frame.present_ns = b->submitted_ns; /* the adapter's actual T4 */
    for (size_t i = 0; i < sizeof e->refresh_frames / sizeof e->refresh_frames[0]; i++)
        if (e->refresh_frames[i].id == e->active_frame.id) e->refresh_frames[i] = e->active_frame;
    if (e->active_frame.last_sequence) e->stats.presented_sequence = e->active_frame.last_sequence;
    if (e->cfg.on_present) e->cfg.on_present(e->cfg.hook_ctx, &e->active_frame);
    editor_refresh_flush(e);
    return 0;
}
static int submit(editor *e)
{
    /* A bounded consumer (including minimap) may adopt the final index range
     * after this turn's pump. Publish before this frame can leave the editor. */
    editor_large_index_progress(e);
    if (e->op_count || editor_clip_busy(e) || editor_scroll_waiting(e) || e->action == EDITOR_ACTION_REPLAY_WORK || e->action == EDITOR_ACTION_GOTO || !e->dirty || !e->paint_ready || e->full_pending || layout_busy(&e->lay) || view_busy(&e->v) || e->extra_rows || e->backend->active) return 0;
    int metadata = editor_journal_metadata(e);
    if (metadata == JOURNAL_BUSY) return 0;
    if (metadata) { e->stats.journal_error = metadata; return EDITOR_ERR_IO; }
    size_t count = 0;
    int rc = editor_compose(e); if (rc) return rc;
    editor_save_overlay(e);
    editor_goto_overlay(e);
    editor_large_index_progress(e);
    if (e->full_pending) return 0;
    rc = render_dirty_strips(&e->grid, e->strips, e->strip_cap, &count); if (rc) return rc;
    if (e->caret_only && !count) {
        e->dirty = false; /* Visibility changed outside the viewport: no pixels to submit. */
        return 0;
    }
    trace_record(TRACE_T3_RENDER_DONE, e->grid.frame_id);
    raster_set_caret_only(e->backend, e->caret_only);
    rc = render_backend_submit(e->backend, &e->grid, e->strips, count);
    if (rc == RENDER_ERR_BUSY) return 0;
    if (rc) return rc;
    trace_record(TRACE_STARTUP_FIRST_SUBMIT, 0);
    e->frame.submit_ns = trace_now_ns(); e->active_frame = e->frame;
    editor_save_submitted(e, e->frame.submit_ns);
    e->active_frame.scroll = e->scroll_plan;
    e->active_frame.selection = e->v.state.selection;
    e->active_frame.wrapped = e->v.state.wrap;
    if (e->v.state.wrap) e->active_frame.wrap_seed = e->lay.wrap_rows[0];
    e->active_frame.source_id = e->buffer->id;
    e->active_frame.source_generation = e->buffer->source_generation;
    e->active_frame.source_revision = e->buffer->revision;
    e->active_frame.geometry_generation = e->geometry_generation;
    e->active_frame.sidebar_revision = e->buffer->map.revision;
    uint64_t identity = 0; /* rolling identity from the exact submitted sidebar pixels/cells */
    for (uint32_t r = e->tab_rows; r < e->grid.dims.rows; r++)
        for (uint32_t c = e->text_grid.dims.cols; c < e->grid.dims.cols; c++) {
            const render_cell *cell = &e->grid.cells[(size_t)r * e->grid.dims.cols + c];
            identity = (identity << 1) | (identity >> 63);
            identity ^= (uint64_t)cell->glyph_index ^ ((uint64_t)cell->bg << 32) ^ cell->fg;
        }
    const render_pixels *region = &e->grid.region;
    if (region->pixels) for (uint32_t y = 0; y < region->height; y++)
        for (uint32_t x = 0; x < region->width; x++) {
            identity = (identity << 1) | (identity >> 63);
            identity ^= region->pixels[(size_t)y * region->stride + x];
        }
    e->active_frame.sidebar_identity = identity;
    e->frame = e->active_frame;
    e->refresh_frames[e->refresh_next] = e->active_frame;
    e->refresh_next = (e->refresh_next + 1u) % (sizeof e->refresh_frames / sizeof e->refresh_frames[0]);
    if (e->buffer->source_stale && !e->buffer->prompt_ns) {
        e->buffer->prompt_ns = e->frame.submit_ns;
        e->buffer->prompt_serial = e->event_serial;
        e->buffer->prompt_generation = e->buffer->source_generation;
    }
    if (e->frame.last_sequence) e->stats.submitted_sequence = e->frame.last_sequence;
    e->dirty = false;
    if (e->cfg.on_submit) e->cfg.on_submit(e->cfg.hook_ctx, &e->frame);
    e->submitted_plan = e->scroll_plan; e->submitted_plan_valid = true;
    return 0;
}
int editor_resize_now(editor *e, uint32_t width, uint32_t height)
{
    /* A journal from a larger display must not exceed the reserved surface. */
    if (width > e->font.max_width) width = e->font.max_width;
    if (height > e->font.max_height) height = e->font.max_height;
    e->font.window_width = width; e->font.window_height = height;
    render_dims dims = e->grid.dims;
    dims.cols = width / dims.cell_w; dims.rows = height / dims.cell_h;
    if (!dims.cols) dims.cols = 1;
    if (!dims.rows) dims.rows = 1;
    if (dims.cols > e->max_cols) dims.cols = e->max_cols;
    if (dims.rows > e->max_rows) dims.rows = e->max_rows;
    if (dims.cols == e->grid.dims.cols && dims.rows == e->grid.dims.rows) return 0;
    return editor_resize_grid(e, dims);
}
int editor_resize_grid(editor *e, render_dims dims)
{
    uint32_t cols = e->font.max_width / dims.cell_w, rows_limit = e->font.max_height / dims.cell_h;
    if (!cols || !rows_limit) return EDITOR_ERR_CAPACITY;
    if (dims.cols > cols) dims.cols = cols;
    if (dims.rows > rows_limit) dims.rows = rows_limit;
    if ((e->backend->info.capabilities & RENDER_CAP_RASTER_POOL) &&
        (size_t)dims.cols * dims.rows > RASTER_FRAME_CELL_LIMIT)
        dims.rows = RASTER_FRAME_CELL_LIMIT / dims.cols;
    /* Recovery resizes the prepaid grids before platform/backend creation.
     * Backend init receives those final dimensions from editor_open. */
    int rc = e->backend->initialized ? render_backend_resize(e->backend, dims) : 0;
    if (rc) return rc;
    e->grid.dims = dims;
    e->grid.scroll = (render_scroll){0}; /* the old rectangle belongs to the old surface */
    e->tab_rows = dims.rows > 1 ? 1u : 0u; e->map_cols = dims.cols >= 24 ? 8u : 0u;
    e->text_grid.dims = dims; e->text_grid.dims.cols -= e->map_cols; e->text_grid.dims.rows -= e->tab_rows;
    rc = font_async_bind(e->fonts, &e->grid); if (rc) return rc;
    rc = font_async_bind(e->fonts, &e->text_grid); if (rc) return rc;
    return editor_text_geometry(e);
}
/* Text rows below the tab strip, less the find panel (edit-73j), plus the
 * P3.4 overscan row; shared by surface resizes and panel open/close. */
int editor_text_geometry(editor *e)
{
    render_dims dims = e->grid.dims; int rc;
    /* edit-yey: the menu bar row sits above the tab strip while visible and
     * only when a text row remains (one bar + one strip + one text row). */
    e->menu_rows = editor_menu_visible(e) && dims.rows > 2u ? 1u : 0u;
    e->tab_rows = (dims.rows > 1u ? 1u : 0u) + e->menu_rows;
    e->text_grid.dims.rows = dims.rows - e->tab_rows;
    uint32_t available=e->text_grid.dims.rows;
    /* P: query row; replacement mode adds its field and hints row. Recompute
     * the desired size, since the last surface may have clipped the panel. */
    /* edit-n95: find in files adds the Where field and its hints row. */
    e->panel_rows=e->palette_open ? EDITOR_FIND_PANEL_ROWS : e->find_visible ?
        (findui_get_state(&e->find).replace_mode || e->fif.mode ? EDITOR_FIND_PANEL_ROWS : 1u) : 0u;
    if (e->panel_rows>=available) e->panel_rows=available>1 ? available-1 : 0;
    e->text_grid.dims.rows-=e->panel_rows;
    e->map_grid.dims.rows = (e->text_grid.dims.rows * dims.cell_h + 1u) / 2u;
    e->v.config.rows = e->text_grid.dims.rows;
    if (!e->v.state.wrap) e->text_grid.dims.rows++;
    /* Reinit clears cells for a changed compact stride; retain open-time wrap storage. */
    layout_wrap_row *rows = e->lay.wrap_rows, *plan = e->lay.wrap_plan; uint32_t cap = e->lay.wrap_capacity;
    uint32_t active_rows = e->text_grid.dims.rows;
    if (e->v.state.wrap) e->text_grid.dims.rows++;
    rc = layout_init(&e->lay, &e->text_grid, &e->layout_cfg, e->row_byte, e->row_used);
    e->text_grid.dims.rows = active_rows; if (rc) return rc;
    e->lay.wrap_rows = rows; e->lay.wrap_plan = plan; e->lay.wrap_capacity = cap;
    e->lay.cfg.tab_width = e->buffer->style.width; e->lay.tab = e->buffer->style.width;
    rc = layout_set_wrap(&e->lay, e->v.state.wrap); if (rc) return rc;
    e->geometry_generation++;
    rc = editor_scroll_resize(e); if (rc) return rc;
    return editor_full_layout(e);
}
static int resize(editor *e)
{
    if (!e->resize_pending || e->backend->active || e->dirty) return 0;
    e->resize_pending = false;
    return editor_resize_now(e, e->resize_w, e->resize_h);
}
static int nonkey(editor *e, const plat_event *ev)
{
    /* P: button identity belongs to the chain even on clipboard dispatch. */
    if (ev->kind == PLAT_EV_BUTTON && ev->press && ev->code != e->click_button) e->click_ns = 0;
    if (ev->kind == PLAT_EV_CLIPBOARD || (ev->kind == PLAT_EV_BUTTON && ev->code == 2))
        return editor_clip_event(e, ev);
    if (ev->kind == PLAT_EV_CLOSE) { e->quit = true; return 0; }
    if (ev->kind == PLAT_EV_EXPOSE) return editor_full_layout(e);
    if (ev->kind == PLAT_EV_RESIZE) {
        e->drag_tab = SIZE_MAX; e->drag_id = 0; e->map_drag = false; e->text_drag=false;
        e->font.window_width = ev->w; e->font.window_height = ev->h;
        e->resize_w = ev->w; e->resize_h = ev->h; e->resize_pending = true; return 0;
    }
    if (ev->kind == PLAT_EV_FOCUS) {
        e->focused = ev->focused; undo_break_burst(e->undo);
        if (!ev->focused) { int rc = editor_palette_close(e); if (!rc) rc = editor_menu_reset(e); if (rc) return rc; }
        if (ev->focused && e->buffer->file) {
            (void)file_check(e->buffer->file, NULL);
            int rc = editor_check_sources(e); if (rc) return rc;
        }
        keys_reset(&e->keys); tabs_mru_release(&e->tabs);
        e->drag_tab = SIZE_MAX; e->drag_id = 0; e->map_drag = false; e->text_drag=false;
        editor_restart_blink(e, trace_now_ns());
        return refresh_caret(e);
    }
    if (ev->kind == PLAT_EV_KEYMAP) { keys_reset(&e->keys); tabs_mru_release(&e->tabs); return 0; }
    if (ev->kind == PLAT_EV_WHEEL) return editor_handle_wheel(e, ev);
    if (ev->kind == PLAT_EV_BUTTON || ev->kind == PLAT_EV_MOTION) {
        if (!e->buffer->source_stale) { int rc = 0; if (editor_menu_pointer(e, ev, &rc) || rc) return rc; }
        if (e->go.active && (ev->kind == PLAT_EV_MOTION || !ev->press)) return 0;
        if (e->go.active) { int cancel = editor_goto_cancel(e); if (cancel) return cancel; }
        return e->buffer->source_stale && !editor_event_control(e, ev) ? 0 : editor_pointer(e, ev);
    }
    return 0;
}
void editor_restart_blink(editor *e, uint64_t now)
{
    e->last_input = now; e->visible = true; e->blinking = e->focused;
    e->next_blink = e->focused ? now + EDITOR_BLINK_NS : 0;
}
static int refresh_caret(editor *e)
{
    /* Retain decorations and minimize composition only if this turn started
     * from a fully prepared grid. Concurrent input/layout takes the full path. */
    if (e->buffer->source_stale) return 0;
    bool glyphs = e->dirty && e->glyph_only;
    bool caret = !e->dirty && e->paint_ready && !layout_busy(&e->lay) &&
                 !view_busy(&e->v) && !e->queue_count && !e->resize_pending;
    int rc = refresh_cursor(e, e->v.state.selection.cursor, caret);
    /* A blink sharing a glyph repaint still changes no minimap input. Keep
     * its text-only composition, but never give pixel damage the raster
     * cursor-only hint (cell metadata may be unchanged on glyph arrival). */
    if (glyphs && !rc && !e->grid.full_frame && !e->full_pending) e->glyph_only = true;
    if (caret && !rc && !e->grid.full_frame && !e->full_pending) { e->caret_only = true; e->paint_ready = true; }
    return rc;
}
static int blink(editor *e, uint64_t now)
{
    if (!e->blinking || now < e->next_blink) return 0;
    if (now - e->last_input >= EDITOR_IDLE_NS) {
        e->blinking = false; e->next_blink = 0;
        if (e->visible) return 0;
        e->visible = true;
    } else { e->visible = !e->visible; e->stats.blinks++; e->next_blink = now + EDITOR_BLINK_NS; }
    return refresh_caret(e);
}

static bool queued_owner_live(const editor *e, size_t at)
{
    if (!e->queue_owner[at]) return true;
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *tab = tabs_at(&e->tabs, i);
        if (tab->id != e->queue_owner[at]) continue;
        for (size_t slot = 0; slot < e->buffer_capacity; slot++) {
            const editor_buffer *b = e->buffers[slot];
            if (b && !b->retired && b->id == tab->id) return b->source_generation == e->queue_generation[at] && !b->source_stale;
        }
    }
    return false;
}
static size_t queue_pick(const editor *e)
{
    /* Journal replay ranges borrow tree storage until every staged record drains. */
    if (e->file_command != KEYS_ACTION_NONE) return SIZE_MAX;
    if (editor_save_leased(e) || (e->journal && (e->op_count || e->journal_metadata_pending))) return SIZE_MAX;
    if (editor_clip_busy(e)) {
        for (size_t n = 0; n < e->queue_count; n++)
            if (e->queue[(e->queue_head + n) % EDITOR_INPUT_CAP].kind == PLAT_EV_CLIPBOARD) return n;
        return SIZE_MAX;
    }
    for (size_t n = 0; n < e->queue_count; n++) {
        size_t at = (e->queue_head + n) % EDITOR_INPUT_CAP;
        const plat_event *event=&e->queue[at];
        if (!e->buffer->source_stale && !editor_event_control(e,event) &&
            (event->kind==PLAT_EV_BUTTON || event->kind==PLAT_EV_MOTION) &&
            (layout_busy(&e->lay) || e->full_pending || !e->paint_ready)) return SIZE_MAX;
        if (!queued_owner_live(e, at) || editor_event_control(e, event)) return n;
        if (e->queue_owner[at] && e->queue_owner[at] != e->buffer->id) continue;
        /* edit-qct: a placeholder is inert; its keys wait for the restore. */
        if ((!e->buffer->pending_open && !e->buffer->placeholder) || e->buffer->source_stale) return n;
    }
    return SIZE_MAX;
}
static void queue_remove(editor *e, size_t n)
{
    /* Only bypassed parked entries move; ordinary head dispatch is constant time. */
    if (!n) { e->queue_head = (e->queue_head + 1) % EDITOR_INPUT_CAP; e->queue_count--; return; }
    for (size_t i = n; i + 1 < e->queue_count; i++) {
        size_t dst = (e->queue_head + i) % EDITOR_INPUT_CAP, src = (dst + 1) % EDITOR_INPUT_CAP;
        e->queue[dst] = e->queue[src]; e->queue_owner[dst] = e->queue_owner[src];
        e->queue_generation[dst] = e->queue_generation[src]; e->queue_serial[dst] = e->queue_serial[src];
    }
    e->queue_count--;
}
static bool runnable(const editor *e)
{
    bool can_drain = queue_pick(e) != SIZE_MAX;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        const editor_buffer *b = e->buffers[i];
        if (b && b->index && !b->retired && !b->source_stale && !b->pending_open &&
            (lineidx_build_needs_poll(b->index) || (b->index_dirty && !lineidx_building(b->index)))) return true;
    }
    return (e->file_command != KEYS_ACTION_NONE && !e->dirty && !e->backend->active) ||
           (editor_pointer_scrolling(e) && !e->backend->active && !editor_scroll_waiting(e) && !e->wheel_pending && !editor_clip_busy(e)) ||
           (editor_scroll_waiting(e) && !e->buffer->scroll_resident.pending) ||
           (editor_font_pending(e) && !e->font.handle.epoch) ||
           (e->font.received && !e->backend->active) || editor_clip_runnable(e) ||
           e->action == EDITOR_ACTION_CLIP || e->action == EDITOR_ACTION_REPLAY_WORK || e->action == EDITOR_ACTION_GOTO || e->source_choice ||
           (e->queue_count && can_drain) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
           e->find_replace_request || e->find_replace_active || e->find_select_pending || editor_wheel_ready(e) || editor_fif_runnable(e) ||
           (e->dirty && !e->backend->active && !editor_clip_busy(e)) || (e->resize_pending && !e->backend->active && !e->dirty);
}
static int wait_timeout(editor *e, int requested, uint64_t now)
{
    if (!e->checkpoint_retry_ns && runnable(e)) return 0;
    /* A private XCB event or unsignalled GPU fence can outlast its sole
     * platform callback. Retry only an outstanding presented GPU frame.
     * Once T5/T6 release that slot, no GL timer/job/poll remains at idle. */
    if (gpu_pending(e) && (requested < 0 || requested > 1)) requested = 1;
    uint64_t deadline = e->checkpoint_retry_ns ? e->checkpoint_retry_ns : e->blinking ? e->next_blink : UINT64_MAX;
    /* edit-bt1: wake for a deferred scroll slice's retry tick. */
    if (e->buffer->scroll_retry_ns && e->buffer->scroll_pending && e->buffer->scroll_retry_ns < deadline)
        deadline = e->buffer->scroll_retry_ns;
    if (e->journal && !editor_save_leased(e)) {
        journal_stats s = journal_get_stats(e->journal);
        if (s.error) e->stats.journal_error = s.error;
        if (!editor_save_leased(e) && !e->journal_waiting && s.accepted_sequence > s.durable_sequence) {
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
    e->queue[at] = *event; e->queue_serial[at] = ++e->event_serial;
    bool parked = (e->buffer->pending_open || e->buffer->placeholder) && !editor_event_control(e, event);
    e->queue_owner[at] = parked ? e->buffer->id : 0;
    e->queue_generation[at] = parked ? e->buffer->source_generation : 0;
    e->queue_count++; return 0;
}
static int close_result(editor *e, int timeout_ms)
{
    if (e->checkpoint_retry_ns && trace_now_ns() < e->checkpoint_retry_ns) {
        int pumped = pump(e, wait_timeout(e, timeout_ms, trace_now_ns()));
        if (pumped) return fail(e, pumped);
        if (trace_now_ns() < e->checkpoint_retry_ns) return EDITOR_OK;
    }
    int rc = editor_flush(e);
    if (rc == JOURNAL_BUSY) {
        /* edit-dr9 (E): reuse the existing durability interval as the retry
         * tick. Keep quit, text, queued ops and the open journal until then;
         * poll the deadline rather than spinning or latching fail(). */
        e->checkpoint_retry_ns = trace_now_ns() + JOURNAL_DEFAULT_SYNC_NS;
        return EDITOR_MORE;
    }
    e->checkpoint_retry_ns = 0; /* (P) no pending retry */
    return rc ? fail(e, EDITOR_ERR_IO) : EDITOR_CLOSED;
}
int editor_step(editor *e, int timeout_ms)
{
    if (!e || !e->tree) return EDITOR_ERR_ARG;
    if (e->quit) return close_result(e, timeout_ms);
    if (e->error) return e->error;
    if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
    font_async_pump(e->fonts);
    if (!e->font.handle.epoch)
        font_async_pump(e->font.atlas[1u - e->font.current].fonts);
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
    int rc = present(e);
    if (!rc) rc = pump(e, wait_timeout(e, timeout_ms, trace_now_ns()));
    editor_refresh_flush(e);
    if (!rc && e->find.private_) (void)findui_maintain(&e->find);
    if (!rc) rc = editor_find_service(e);
    if (!rc) rc = editor_fif_service(e);
    if (!rc) rc = editor_map_service(e);
    if (!rc) editor_hl_service(e);
    /* edit-601: restore failures are notices (restore.c restore_fail); a
     * nonzero return is only a failed reactivation of the editor itself. */
    if (!rc) rc = editor_restore_service(e);
    if (!rc) rc = editor_palette_sync(e);
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
    if (e->quit) return close_result(e, timeout_ms);
    if (rc) return fail(e, rc);
    editor_journal_staged(e);
    if (e->journal_metadata_pending) { rc = editor_journal_changed(e); if (rc) return fail(e, rc); }
    editor_large_index_progress(e);
    rc = editor_clip_poll(e); if (rc) return fail(e, rc);
    rc = editor_scroll_slice(e); if (rc) return fail(e, rc);
    /* Like the scroll slice: one attempt per turn; readiness only decides
     * whether the input loop may spin on it or the pump may block. */
    if (e->wheel_pending) { rc = editor_wheel_continue(e); if (rc) return fail(e, rc); }
    uint64_t start = trace_now_ns();
    if (e->queue_count) e->caret_only = false;
    rc = e->action == EDITOR_ACTION_REPLAY_WORK ? 0 : blink(e, start);
    if (!rc) rc = editor_font_poll(e);
    if (!rc && e->action != EDITOR_ACTION_REPLAY_WORK) rc = resize(e);
    record_slice(e, start); if (rc) return rc;
    for (;;) {
        e->stats.input_checks++;
        uint64_t slice = trace_now_ns();
        if (!view_busy(&e->v) && e->action != EDITOR_ACTION_REPLAY_WORK && !e->find_replace_active) {
            rc = editor_preferences_apply(e); if (rc) return fail(e, rc);
        }
        bool applied = e->find_replace_active || e->action == EDITOR_ACTION_CLIP || e->action == EDITOR_ACTION_REPLAY_WORK ||
            view_busy(&e->v) || queue_pick(e) != SIZE_MAX;
        if (e->find_replace_active) {
            if (queue_pick(e) != SIZE_MAX) {
                size_t picked=queue_pick(e), at=(e->queue_head+picked)%EDITOR_INPUT_CAP;
                plat_event ev=e->queue[at];
                if (ev.kind==PLAT_EV_KEY && ev.keysym==XKB_KEY_Escape) {
                    queue_remove(e,picked); (void)editor_find_key(e,&ev);
                } else if (ev.kind==PLAT_EV_CLOSE) { queue_remove(e,picked); e->quit=true; }
            }
            rc=editor_find_replace_continue(e);
        } else if (e->action == EDITOR_ACTION_CLIP || e->action == EDITOR_ACTION_REPLAY_WORK || view_busy(&e->v)) rc = editor_continue_action(e);
        else if (queue_pick(e) != SIZE_MAX) {
            size_t picked = queue_pick(e), at = (e->queue_head + picked) % EDITOR_INPUT_CAP;
            plat_event ev = e->queue[at]; bool live = queued_owner_live(e, at);
            e->dispatch_serial = e->queue_serial[at];
            queue_remove(e, picked);
            rc = !live ? 0 : ev.kind == PLAT_EV_KEY ? editor_handle_key(e, &ev) : nonkey(e, &ev);
        } else if (e->action == EDITOR_ACTION_GOTO) {
            rc = editor_continue_action(e);
        } else if (editor_wheel_ready(e)) rc = editor_wheel_continue(e);
        else if (layout_busy(&e->lay)) {
            rc = layout_slice(e);
            if (rc >= 0) rc = 0;
        } else if (e->full_pending) {
            rc = editor_full_layout(e);
        } else if (e->extra_rows) {
            e->extra_rows = false;
            rc = layout_relayout_rows(&e->lay, e->extra_first, e->extra_end - e->extra_first);
            if (rc >= 0) rc = 0;
        } else if (e->dirty && !e->paint_ready) rc = editor_paint_prepare(e);
        else break;
        /* Protect every mutation before yielding to layout or publishing a
         * frame. Successful journal calls establish the page-cache cutoff. */
        editor_session_publish(e, e->buffer);
        editor_journal_staged(e);
        if (!rc && applied) rc = editor_journal_changed(e);
        record_slice(e, slice);
        if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
        if (rc) return fail(e, rc);
        if (e->quit || trace_now_ns() - start >= EDITOR_SLICE_NS) break;
    }
    if (e->quit) return close_result(e, timeout_ms);
    rc = editor_scroll_slice(e); if (rc) return fail(e, rc);
    rc = editor_pointer_tick(e); if (rc) return fail(e, rc);
    if (e->dirty || e->journal_metadata_pending) { rc = editor_journal_changed(e); if (rc) return fail(e, rc); }
    editor_hl_request(e);
    uint64_t submit_start = trace_now_ns(); rc = submit(e);
    record_slice(e, submit_start); if (rc) return rc;
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
    rc = present(e);
    if (!rc && e->journal && !editor_save_leased(e) && !e->journal_waiting) {
        journal_stats s = journal_get_stats(e->journal); uint64_t now = trace_now_ns();
        /* Pump only when the default batch/deadline admission is due. A
         * successful pump has scheduled a job; its mailbox advances sync.
         * BUSY on admission is retried, since it can mean a full work pool. */
        if (s.accepted_sequence > s.durable_sequence &&
            (e->op_count || e->journal_metadata_pending || s.pending_bytes >= JOURNAL_PAGE - JOURNAL_HEADER || now - s.last_sync_ns >= UINT64_C(1000000000))) {
            int jr = journal_pump(e->journal, now, e->op_count != 0 || e->journal_metadata_pending);
            if (!jr) e->journal_waiting = true;
            else if (jr != JOURNAL_BUSY) e->stats.journal_error = jr;
        }
    }
    if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
    if (rc) return fail(e, rc);
    if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
    if (e->quit) return close_result(e, timeout_ms);
    return runnable(e) ? EDITOR_MORE : EDITOR_OK;
}
int editor_run(editor *e)
{
    int rc; do { rc = editor_step(e, -1); } while (rc == EDITOR_OK || rc == EDITOR_MORE);
    return rc == EDITOR_CLOSED ? 0 : rc;
}
int editor_set_cursor(editor *e, uint64_t byte)
{
    if (!e || e->buffer->source_stale || e->buffer->pending_open || byte > piece_len(e->tree) || editor_activation_busy(e)) return EDITOR_ERR_ARG;
    uint64_t old = e->v.state.selection.cursor;
    e->old_selection = e->v.state.selection;
    e->v.state.selection.cursor = e->v.state.selection.anchor = byte;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->v.state.first_line = piece_byte_to_line(e->tree, byte);
    e->v.state.first_byte = piece_line_to_byte(e->tree, e->v.state.first_line);
    e->v.state.visual_byte = e->v.state.first_byte; e->v.state.visual_end = false;
    e->v.state.hscroll = 0; undo_break_burst(e->undo);
    int rc = editor_scroll_seek(e, e->v.state.first_byte); if (rc) return rc;
    rc = refresh_cursor(e, old, false);
    return rc ? rc : editor_journal_changed(e);
}
int editor_jump_line(editor *e, uint64_t line)
{
    if (!e || editor_activation_busy(e) || e->buffer->source_stale || e->buffer->pending_open) return EDITOR_ERR_ARG;
    if (e->buffer->index) {
        (void)lineidx_poll(e->buffer->index);
        /* Exact piece queries are warm after open; publication remains the
         * prerequisite for positioning in the active buffer. */
        if (!lineidx_complete(e->buffer->index)) {
            if (!e->buffer->index_dirty && (!e->buffer->file ||
                file_open_mode(e->buffer->file) == FILE_MODE_COPY))
                (void)lineidx_build_prioritize(e->buffer->index);
            return EDITOR_MORE;
        }
    }
    return editor_set_cursor(e, piece_line_to_byte(e->tree, line));
}
uint64_t editor_length(const editor *e)
{
    if (!e || !tabs_count(&e->tabs)) return 0;
    const piece_snapshot *snap = undo_replay_snapshot(e->undo);
    return snap ? piece_snapshot_len(snap) : piece_len(e->tree);
}
int editor_read(const editor *e, uint64_t off, uint8_t *dst, size_t len)
{
    if (!e) return EDITOR_ERR_ARG;
    const piece_snapshot *snap = undo_replay_snapshot(e->undo);
    return snap ? piece_snapshot_read(snap, off, dst, len) : piece_read(e->tree, off, dst, len);
}
view_state editor_view(const editor *e) { return e ? e->v.state : (view_state){0}; }
editor_stats editor_get_stats(const editor *e)
{
    if (!e) return (editor_stats){0};
    editor_stats s = e->stats;
    s.focused = e->focused; s.blinking = e->blinking; s.cursor_visible = e->visible;
    s.render_active = e->backend->active;
    s.font_pending = e->font.requested_generation != e->font.applied_generation;
    s.source_stale = e->buffer->source_stale; s.open_pending = e->buffer->pending_open;
    s.pending = e->file_command != KEYS_ACTION_NONE || editor_pointer_scrolling(e) || editor_scroll_waiting(e) || editor_font_pending(e) || editor_clip_pending(e) || e->action == EDITOR_ACTION_CLIP ||
        e->find_replace_request || e->find_replace_active || e->find_select_pending || (e->find.private_ && findui_get_state(&e->find).searching) || e->wheel_pending || editor_fif_pending(e) ||
        e->action == EDITOR_ACTION_REPLAY_WORK || e->action == EDITOR_ACTION_GOTO || e->dirty || e->queue_count || view_busy(&e->v) || e->resize_pending || e->full_pending || e->extra_rows;
    s.tabs = tabs_count(&e->tabs); s.active_tab = tabs_active_index(&e->tabs); s.minimap_stale = e->buffer->map.stale;
    return s;
}
bool editor_index_complete(editor *e) { return e && (!e->buffer->index || lineidx_complete(e->buffer->index)); }
uint64_t editor_line_count(const editor *e) { return e ? e->buffer->lines : 0; }
int editor_flush(editor *e)
{
    if (!e) return EDITOR_ERR_ARG;
    /* Explicit flush/close is off-path. Finish all admitted save leases before
     * touching the borrowed journal or releasing buffer arenas. */
    e->save_flushing = true;
    while (editor_save_active(e)) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        if (e->journal_pool_ready && !editor_save_leased(e)) (void)work_mailbox_drain(&e->journal_pool, editor_route_work, e);
        editor_save_tick(e);
        if (!editor_save_leased(e)) editor_journal_staged(e);
        sched_yield();
    }
    e->save_flushing = false;
    if (!e->journal) return 0;
    while (e->op_count && !e->stats.journal_error) {
        size_t before = e->op_count;
        editor_journal_staged(e);
        if (e->op_count && e->op_count == before) {
            int jr = journal_flush(e->journal); if (jr) break;
        }
    }
    int metadata = 0;
    if (!e->stats.journal_error) do {
        metadata = editor_journal_metadata(e);
        if (metadata == JOURNAL_BUSY) { int jr = journal_flush(e->journal); if (jr) { metadata = jr; break; } }
    } while (metadata == JOURNAL_BUSY);
    int rc = metadata ? metadata : journal_flush(e->journal);
    e->journal_waiting = false;
    if (rc == JOURNAL_FULL || rc == JOURNAL_IO || e->stats.journal_error)
        return editor_checkpoint(e);
    return rc;
}
