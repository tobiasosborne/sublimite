#include "editor/private.h"
#include "trace/trace.h"
#include <sched.h>
#include <string.h>

/* UI state and continuations live here. Raw bytes belong to snapshot workers;
 * the resident bridge seals windows through work mailboxes before use. */
static size_t snapshot_span(void *ctx, uint64_t off, const uint8_t **out)
{
    piece_iter it; size_t n = 0;
    piece_iter_begin_snapshot(&it, ctx, off);
    return piece_iter_next(&it, out, &n) ? n : 0;
}
static scroll_config config(const editor *e)
{
    uint32_t rows = e->grid.dims.rows - e->tab_rows;
    /* Preserve view's existing policy: follow only a clipped cursor. */
    return (scroll_config){rows, e->grid.dims.cell_h, 0};
}
static scroll_extent extent(editor_buffer *b, uint64_t len)
{
    lineidx_result count = lineidx_line_count(b->index);
    /* A preview's index can describe the whole pending source. */
    if (count.value > len + 1u) count.value = len + 1u;
    return (scroll_extent){len, count.value, count.exact && lineidx_len(b->index) == len};
}
int editor_scroll_retire(editor_buffer *b)
{
    if (!b->scroll_snapshot) return 0;
    int rc = scroll_resident_close(&b->scroll_resident);
    if (rc == SCROLL_MORE) return EDITOR_MORE;
    if (rc) return rc;
    piece_snapshot_release(b->scroll_snapshot); b->scroll_snapshot = NULL;
    b->scroll_resolver = (scroll_resolver){0};
    b->scroll_follow_resolver = (scroll_resolver){0};
    return 0;
}
void editor_scroll_close(editor_buffer *b)
{
    /* Quiescent shutdown only; maintenance never blocks physical retirement. */
    while (editor_scroll_retire(b) == EDITOR_MORE) sched_yield();
}
static int install(editor_buffer *b)
{
    piece_tree *tree = b->pending_open ? b->preview : b->tree;
    piece_snapshot *snap = piece_snapshot_take(tree);
    if (!snap) return EDITOR_ERR_MEMORY;
    lineidx_src source = {snap, piece_snapshot_len(snap), snapshot_span, NULL};
    int rc = scroll_resident_init(&b->scroll_resident, b->pool, &source);
    if (!rc) rc = scroll_resident_bind_snapshot(&b->scroll_resident, snap);
    if (rc) { piece_snapshot_release(snap); return rc; }
    b->scroll_snapshot = snap; b->scroll_revision = b->revision;
    return 0;
}
int editor_scroll_init(editor *e, editor_buffer *b)
{
    int rc = editor_scroll_retire(b); if (rc) return rc;
    rc = install(b); if (rc) return rc;
    rc = scroll_init(&b->scroll, config(e), extent(b, b->scroll_resident.snapshot.len));
    if (!rc) { b->scroll_ready = true; b->scroll_display = b->scroll; b->scroll_prefix = lineidx_built_prefix(b->index); }
    return rc;
}
void editor_scroll_mirror(editor *e)
{
    if (e->v.state.wrap || !e->buffer->scroll_ready) return;
    const scroll_state *s = &e->buffer->scroll_display;
    e->v.state.first_byte = s->first_byte; e->v.state.first_line = s->first_line;
    e->v.state.visual_byte = s->first_byte; e->v.state.approximate = s->approximate;
}
bool editor_scroll_waiting(const editor *e)
{ return !e->v.state.wrap && e->buffer->scroll_pending && !e->buffer->source_stale && !e->buffer->pending_open; }
int editor_scroll_resize(editor *e)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    scroll_config next = config(e);
    if (b->scroll.config.rows == next.rows && b->scroll.config.row_height == next.row_height &&
        b->scroll.config.margin == next.margin) return 0;
    int rc = scroll_resize(&b->scroll, next);
    if (!rc) b->scroll_pending = true;
    return rc;
}
int editor_scroll_seek(editor *e, uint64_t byte)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    b->scroll_following = false; b->scroll_resolver = (scroll_resolver){0};
    int rc = scroll_seek_byte(&b->scroll, byte);
    if (!rc) b->scroll_pending = true;
    return rc;
}
int editor_scroll_follow(editor *e)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    b->scroll_cursor = e->v.state.selection.cursor;
    b->scroll_following = true; b->scroll_pending = true;
    if (e->repair_line_valid && b->scroll_cursor == e->edit_delta.off) {
        /* The deleted preceding LF and bounded backward proof certify the
         * joined cursor line before the replacement layout is available. */
        b->scroll.first_byte = e->repair_line_byte; b->scroll.first_line = e->repair_line_number;
        b->scroll.anchor_line = b->scroll.first_line; b->scroll.pending_rows = 0;
        b->scroll.request = SCROLL_READY;
        b->scroll_display = b->scroll; b->scroll_following = b->scroll_pending = false;
    }
    /* The prepared row starts are already resident and certified by layout.
     * An ordinary edit/motion inside them needs no new raw-source window and
     * must not delay typing behind a bulk copy of unchanged viewport bytes. */
    if (!e->full_pending && e->lay.src == e->tree && !e->lay.approximate) {
        uint64_t cursor = b->scroll_cursor;
        for (uint32_t row = 0; row < e->v.config.rows; row++) {
            uint64_t start = e->row_byte[row];
            uint64_t end = row + 1u < e->text_grid.dims.rows ? e->row_byte[row + 1u] : e->lay.pos;
            if (start == LAYOUT_VOID_ROW || start > cursor || end == LAYOUT_VOID_ROW || cursor >= end) continue;
            b->scroll.first_byte = e->lay.first_byte;
            b->scroll.first_line = e->lay.first_line;
            b->scroll.anchor_line = b->scroll.first_line;
            b->scroll.pending_rows = 0; b->scroll.request = SCROLL_READY;
            scroll_state followed = b->scroll;
            int rc = scroll_follow(&followed, b->scroll.first_line + row);
            if (rc) return rc;
            if (followed.first_line == b->scroll.first_line) {
                b->scroll = followed; b->scroll_following = b->scroll_pending = false;
                b->scroll_display = b->scroll;
                break;
            }
        }
    }
    if (b->scroll_pending && b->lg.lines_exact && !b->lg.top_estimated && !view_busy(&e->v)) {
        /* A completed view motion/repair also certifies a resident logical
         * line and its viewport anchor, including EOF without a following
         * row start. Adopt that byte only when scroll's pure policy selects
         * the same ordinal; never blindly adopt view's vertical following. */
        scroll_state followed = b->scroll;
        followed.request = SCROLL_READY; followed.pending_rows = 0;
        int rc = scroll_follow(&followed, e->v.line); if (rc) return rc;
        if (followed.first_line == e->v.state.first_line) {
            followed.first_byte = e->v.state.first_byte;
            followed.anchor_line = followed.first_line; followed.approximate = false;
            b->scroll = b->scroll_display = followed;
            b->scroll_following = b->scroll_pending = false;
        }
    }
    editor_scroll_mirror(e);
    if (!b->scroll_pending) b->scroll_prefix = lineidx_built_prefix(b->index);
    return 0;
}
int editor_scroll_edit(editor *e, uint64_t off, uint64_t old, uint64_t add)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    b->scroll = b->scroll_display; /* a mutation cancels unpublished navigation */
    uint64_t anchor = b->scroll.first_byte;
    if (anchor >= off + old) anchor = anchor - old + add;
    else if (anchor >= off) anchor = off;
    b->scroll_resolver = (scroll_resolver){0}; b->scroll_following = false;
    int rc = scroll_set_extent(&b->scroll, extent(b, piece_len(e->tree)));
    if (!rc) rc = editor_scroll_seek(e, anchor);
    return rc;
}
int editor_scroll_slice(editor *e)
{
    editor_buffer *b = e->buffer;
    if (e->v.state.wrap || b->source_stale || !b->scroll_ready || b->pending_open || view_busy(&e->v) ||
        e->action == EDITOR_ACTION_REPLAY_WORK) return 0;
    lineidx_result count = lineidx_line_count(b->index);
    size_t prefix = lineidx_built_prefix(b->index);
    if (!b->scroll_pending && b->scroll_prefix == prefix && b->scroll.extent.exact == count.exact && b->scroll.extent.bytes == piece_len(e->tree)) {
        /* Keep the source lease until physical retirement, but do not ask a
         * typing frame to wait for replacement windows it does not need. */
        if (b->scroll_revision != b->revision) (void)editor_scroll_retire(b);
        editor_scroll_mirror(e);
        scroll_frame_plan previous = e->scroll_plan;
        int rc = scroll_plan_frame(&b->scroll, e->text_grid.dims.cols * e->grid.dims.cell_w,
            config(e).rows * e->grid.dims.cell_h,
            e->submitted_plan_valid ? &e->submitted_plan : NULL, &e->scroll_plan);
        if (!rc && previous.origin_y_q8 != e->scroll_plan.origin_y_q8) rc = render_mark_full(&e->grid);
        return rc;
    }
    scroll_state before = b->scroll;
    if (b->scroll_pending && !b->scroll_following && b->scroll.request == SCROLL_RELATIVE &&
        b->scroll.extent.exact && !b->scroll.approximate && e->lay.src == e->tree &&
        !layout_busy(&e->lay) && !e->full_pending && b->scroll.first_line >= b->scroll_display.first_line) {
        uint64_t row = b->scroll.first_line - b->scroll_display.first_line;
        if (row < e->text_grid.dims.rows && e->row_byte[row] != LAYOUT_VOID_ROW) {
            /* Layout already certified this resident line start. The pure
             * exact-count policy already clipped EOF, so warm wheel input
             * needs no worker round trip and can lay out in this turn. */
            b->scroll.first_byte = e->row_byte[row]; b->scroll.anchor_line = b->scroll.first_line;
            b->scroll.pending_rows = 0; b->scroll.request = SCROLL_READY;
            goto adopted;
        }
    }
    if (b->scroll_revision != b->revision || b->scroll_resident.closing) {
        int rc = editor_scroll_retire(b);
        if (rc == EDITOR_MORE) { b->scroll_pending = true; return 0; }
        if (rc) return rc;
        rc = install(b); if (rc) return rc;
    }
    scroll_extent current = extent(b, b->scroll_resident.snapshot.len);
    if (current.bytes != b->scroll.extent.bytes || current.lines != b->scroll.extent.lines ||
        current.exact != b->scroll.extent.exact) {
        int rc = scroll_set_extent(&b->scroll, current); if (rc) return rc;
    }
    /* One resident continuation per call, with the scroll module's shared
     * byte/callback/deadline bound. The caller checks input between slices;
     * MORE retains the previous frame. */
    uint64_t deadline = trace_now_ns() + EDITOR_SLICE_NS;
    int rc = scroll_resolve_resident(&b->scroll, &b->scroll_resolver,
                                    b->index, &b->scroll_resident, 0, deadline);
    if (!rc && b->scroll_following) {
        rc = scroll_follow_cursor_resident(&b->scroll, &b->scroll_follow_resolver,
            b->index, &b->scroll_resident, b->scroll_cursor, 0, deadline);
        if (!rc) b->scroll_following = false;
    }
    if (rc == SCROLL_MORE) { b->scroll_pending = true; return 0; }
    if (rc) return rc;
adopted:
    b->scroll_pending = false;
    b->scroll_prefix = prefix;
    bool moved = e->v.state.first_byte != b->scroll.first_byte ||
        e->v.state.first_line != b->scroll.first_line ||
        e->v.state.approximate != b->scroll.approximate ||
        before.subrow_q8 != b->scroll.subrow_q8;
    b->scroll_display = b->scroll;
    editor_scroll_mirror(e);
    scroll_frame_plan plan;
    int planned = scroll_plan_frame(&b->scroll, e->text_grid.dims.cols * e->grid.dims.cell_w,
        config(e).rows * e->grid.dims.cell_h,
        e->submitted_plan_valid ? &e->submitted_plan : NULL, &plan);
    if (planned) return planned;
    moved |= e->scroll_plan.origin_y_q8 != plan.origin_y_q8;
    e->scroll_plan = plan;
    e->buffer->lg.top_estimated = b->scroll.approximate && !b->lg.lines_exact;
    if (moved) return editor_full_layout(e);
    return 0;
}
