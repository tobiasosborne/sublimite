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
    uint32_t rows = e->grid.dims.rows - e->tab_rows - e->panel_rows; /* edit-73j find panel rows are not text */
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
/* edit-pd6: drop navigation queued behind an unresolved byte jump. */
static void unqueue(editor_buffer *b) { b->scroll_queued_pages = 0; b->scroll_queued_wheel = 0; b->scroll_queued_pixels = 0; }
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
/* edit-73j: a caller-owned byte seek (wrapped find/minimap navigation)
 * resolved against the buffer's single resident window. Wrapped views do not
 * use b->scroll, so this shares the window without a second source lease. */
int editor_scroll_resolve(editor *e, scroll_state *s, scroll_resolver *r)
{
    editor_buffer *b = e->buffer;
    if (!b->scroll_snapshot || b->scroll_revision != b->revision || b->scroll_resident.closing) {
        int rc = editor_scroll_retire(b);
        if (rc == EDITOR_MORE) return SCROLL_MORE;
        if (rc) return rc;
        rc = install(b); if (rc) return rc;
        *r = (scroll_resolver){0};
    }
    return scroll_resolve_resident(s, r, b->index, &b->scroll_resident, 0, trace_now_ns() + EDITOR_SLICE_NS);
}
int editor_scroll_init(editor *e, editor_buffer *b)
{
    int rc = editor_scroll_retire(b); if (rc) return rc;
    rc = install(b); if (rc) return rc;
    rc = scroll_init(&b->scroll, config(e), extent(b, b->scroll_resident.snapshot.len));
    unqueue(b);
    if (!rc) { b->scroll_ready = true; b->scroll_display = b->scroll; b->scroll_display_stale = false; b->scroll_prefix = lineidx_built_prefix(b->index); }
    return rc;
}
void editor_scroll_mirror(editor *e)
{
    if (e->v.state.wrap || !e->buffer->scroll_ready) return;
    const scroll_state *s = &e->buffer->scroll_display;
    e->v.state.first_byte = s->first_byte; e->v.state.first_line = s->first_line;
    e->v.state.visual_byte = s->first_byte; e->v.state.approximate = s->approximate;
}
/* edit-pd6: a far byte jump (find, minimap, Ctrl+End, goto) is unresolved
 * until the resident resolver lands it; scroll's relative motions reject
 * that state (scroll.c SCROLL_ERR_ARG). Page keys and wheel notches arriving
 * meanwhile are queued here and applied, in that order, to the landed top,
 * before any cursor follow, as if they had arrived after it. */
bool editor_scroll_jumping(const editor *e)
{ return !e->v.state.wrap && e->buffer->scroll_ready && e->buffer->scroll.request == SCROLL_BYTE; }
void editor_scroll_queue(editor *e, int64_t pages, int32_t wheel)
{
    editor_buffer *b = e->buffer;
    b->scroll_queued_pages += pages; /* one per key event: bounded by input */
    int64_t sum = (int64_t)b->scroll_queued_wheel + wheel;
    b->scroll_queued_wheel = sum > INT32_MAX ? INT32_MAX : sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
    b->scroll_pending = true;
}
void editor_scroll_queue_pixels(editor *e, int64_t pixels_q8)
{
    /* edit-o20's scroll_lines chord (scroll_pixels) during a jump. */
    editor_buffer *b = e->buffer;
    int64_t q = b->scroll_queued_pixels;
    b->scroll_queued_pixels = pixels_q8 > 0 && q > INT64_MAX - pixels_q8 ? INT64_MAX :
        pixels_q8 < 0 && q < INT64_MIN - pixels_q8 ? INT64_MIN : q + pixels_q8;
    b->scroll_pending = true;
}
void editor_scroll_cancel_jump(editor *e)
{
    /* An absolute motion (document Home/End) supersedes the jump: return
     * to the displayed READY top, as a mutation does (editor_scroll_edit). */
    editor_buffer *b = e->buffer;
    const scroll_state *d = &b->scroll_display;
    b->scroll.first_line = d->first_line; b->scroll.first_byte = d->first_byte;
    b->scroll.anchor_line = d->anchor_line; b->scroll.subrow_q8 = 0;
    b->scroll.approximate = d->approximate;
    b->scroll.request = SCROLL_READY; b->scroll.pending_rows = 0;
    b->scroll_resolver = b->scroll_follow_resolver = (scroll_resolver){0};
    unqueue(b);
    if (e->find_scroll) { e->find_scroll = false; e->wheel_pending = false; }
}
static int apply_queued(editor_buffer *b)
{
    view_key ignored; int rc = 0;
    for (; !rc && b->scroll_queued_pages > 0; b->scroll_queued_pages--)
        rc = scroll_key_motion(&b->scroll, SCROLL_PAGE_DOWN, &ignored);
    for (; !rc && b->scroll_queued_pages < 0; b->scroll_queued_pages++)
        rc = scroll_key_motion(&b->scroll, SCROLL_PAGE_UP, &ignored);
    if (!rc && b->scroll_queued_wheel) rc = scroll_wheel(&b->scroll, b->scroll_queued_wheel);
    if (!rc && b->scroll_queued_pixels) rc = scroll_pixels(&b->scroll, b->scroll_queued_pixels);
    unqueue(b);
    if (!rc) b->scroll_pending = true;
    return rc;
}
static bool queued(const editor_buffer *b) { return b->scroll_queued_pages || b->scroll_queued_wheel || b->scroll_queued_pixels; }
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
    unqueue(b); /* edit-pd6: a new absolute target supersedes queued motion */
    int rc = scroll_seek_byte(&b->scroll, byte);
    if (!rc) b->scroll_pending = true;
    return rc;
}
bool editor_scroll_place(editor *e, uint64_t byte, uint64_t line, bool approximate)
{
    if (e->v.state.wrap) {
        /* A certified logical start is also a leading visual boundary.
         * Wrapped view owns that byte; b->scroll ordinals are logical, so
         * its EOF policy cannot place or clamp visual rows. Layout derives
         * the rows from this seed; large.c relabels it at index completion. */
        e->v.state.first_byte = e->v.state.visual_byte = byte;
        e->v.state.first_line = line; e->v.state.approximate = approximate;
        e->v.state.visual_end = false; e->v.state.hscroll = 0;
        return true;
    }
    /* An owned seek (large.c byte jump) already proved this line start and
     * its ordinal (exact, or a density estimate the index relabels later).
     * Adopt it as position_buffer does instead of re-deriving the cursor's
     * line through resident windows: those are acquired on the bulk lane,
     * which an unindexed large file's index build, warm or find jobs occupy.
     * Only when scroll's pure seek policy keeps the same top row; an exact
     * EOF clip still takes the resident follow path. */
    if (!e->buffer->scroll_ready) return false;
    editor_buffer *b = e->buffer;
    scroll_state placed = b->scroll; /* current config and extent */
    placed.approximate = approximate;
    if (scroll_seek_line(&placed, line) || placed.first_line != line) return false;
    placed.first_byte = byte; placed.anchor_line = line;
    placed.request = SCROLL_READY; placed.pending_rows = 0;
    b->scroll = b->scroll_display = placed; b->scroll_display_stale = false; unqueue(b);
    b->scroll_resolver = b->scroll_follow_resolver = (scroll_resolver){0};
    b->scroll_following = b->scroll_pending = false;
    b->scroll_prefix = lineidx_built_prefix(b->index);
    editor_scroll_mirror(e);
    return true;
}
int editor_scroll_follow(editor *e)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    b->scroll_cursor = e->v.state.selection.cursor;
    b->scroll_following = true; b->scroll_pending = true;
    /* edit-pd6: motion queued behind an unresolved jump applies to the
     * landed top first; follow after it, never adopt a top derived from the
     * pre-jump scroll state. Edits unqueue (seek), so typing is unaffected. */
    if (b->scroll.request == SCROLL_BYTE && queued(b)) { editor_scroll_mirror(e); return 0; }
    if (e->repair_line_valid && b->scroll_cursor == e->edit_delta.off) {
        /* The deleted preceding LF and bounded backward proof certify the
         * joined cursor line before the replacement layout is available. */
        b->scroll.first_byte = e->repair_line_byte; b->scroll.first_line = e->repair_line_number;
        b->scroll.anchor_line = b->scroll.first_line; b->scroll.pending_rows = 0;
        b->scroll.request = SCROLL_READY;
        b->scroll_display = b->scroll; b->scroll_display_stale = false; b->scroll_following = b->scroll_pending = false;
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
                b->scroll_display = b->scroll; b->scroll_display_stale = false;
                break;
            }
        }
    }
    if (b->scroll_pending && b->lg.lines_exact && !b->lg.top_estimated && !e->v.relative && !view_busy(&e->v)) {
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
            b->scroll = b->scroll_display = followed; b->scroll_display_stale = false;
            b->scroll_following = b->scroll_pending = false;
        }
    }
    if (b->scroll_display_stale && !view_busy(&e->v) && e->v.state.first_byte <= piece_len(e->tree)) {
        /* A completed view restore/repair has normalized the transformed top
         * to a line start and labelled it (view.c P_NORMAL_SETUP). Display
         * that until the pending seek/follow resolves the scroll policy's top;
         * never the pre-edit byte (edit-d50 fix). */
        scroll_state *d = &b->scroll_display;
        d->first_byte = e->v.state.first_byte; d->first_line = d->anchor_line = e->v.state.first_line;
        d->approximate = e->v.state.approximate; d->subrow_q8 = 0;
        b->scroll_display_stale = false;
    }
    editor_scroll_mirror(e);
    if (!b->scroll_pending) b->scroll_prefix = lineidx_built_prefix(b->index);
    return 0;
}
int editor_scroll_edit(editor *e, uint64_t off, uint64_t old, uint64_t add, uint64_t old_nl, uint64_t new_nl)
{
    if (e->v.state.wrap) return 0;
    editor_buffer *b = e->buffer;
    /* The displayed top is a position in the pre-edit text. Transform it
     * with the edit rather than leave it for the resolver: an undo that
     * removes text above it would otherwise lay out a stale byte, past EOF
     * or mid-line under the old label (edit-d50 fix). Wholly above the top
     * is an exact shift. An edit at the top byte leaves it a labelled line
     * start. One that replaces the byte before it loses the label. */
    scroll_state *d = &b->scroll_display;
    uint64_t top = d->first_byte, anchor = top;
    if (anchor >= off + old) anchor = anchor - old + add;
    else if (anchor >= off) anchor = off;
    if (top > off + old) {
        d->first_line = d->first_line - old_nl + new_nl; d->anchor_line = d->first_line;
        d->first_byte = anchor;
    } else if (top > off) {
        d->first_byte = anchor; b->scroll_display_stale = true;
    }
    if (d->first_byte != top) {
        /* The view normalizes this byte to a line start and labels it. */
        e->v.state.first_byte = e->v.state.visual_byte = d->first_byte;
        if (!b->scroll_display_stale) e->v.state.first_line = d->first_line;
    }
    b->scroll = b->scroll_display; /* a mutation cancels unpublished navigation */
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
    if (queued(b) && b->scroll.request != SCROLL_BYTE) { int rc = apply_queued(b); if (rc) return rc; }
    lineidx_result count = lineidx_line_count(b->index);
    size_t prefix = lineidx_built_prefix(b->index);
    /* A lazy large file publishes its logical labels with the complete index
     * (large.c index_progress, the edit-9yd contract): the frame after the
     * last estimated frame is the exact one. Prefix growth alone must not
     * relabel the top early and submit an exact-numbered, dim-gutter frame
     * in between; count.exact flipping below triggers the relabel. */
    bool labels_held = b->lg.lazy && !b->lg.lines_exact;
    if (!b->scroll_pending && b->scroll.request == SCROLL_READY && !b->scroll.approximate &&
        !b->scroll_display_stale && b->scroll.extent.bytes == piece_len(e->tree)) {
        /* A completed follow/place certified the current top independently
         * of the retiring worker's windows. Prefix publication cannot change
         * that exact byte/label pair. Refresh metadata without requesting a
         * replacement unless the pure EOF policy actually moves the top. */
        scroll_state ready = b->scroll;
        int rc = scroll_set_extent(&ready, extent(b, piece_len(e->tree)));
        if (rc) return rc;
        if (ready.request == SCROLL_READY) {
            b->scroll = b->scroll_display = ready;
            b->scroll_prefix = prefix;
        }
    }
    if (!b->scroll_pending && (b->scroll_prefix == prefix || labels_held) && b->scroll.extent.exact == count.exact && b->scroll.extent.bytes == piece_len(e->tree)) {
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
        /* A density-estimated top label is not a position: clamping it to
         * the new count would seek to a guessed line (an overestimate lands
         * at EOF). Keep the certified byte anchor; resolution relabels it. */
        if (b->scroll.approximate && b->scroll.request == SCROLL_READY) b->scroll.extent = current;
        else { int rc = scroll_set_extent(&b->scroll, current); if (rc) return rc; }
    }
    /* One resident continuation per call, with the scroll module's shared
     * byte/callback/deadline bound. The caller checks input between slices;
     * MORE retains the previous frame. */
    uint64_t deadline = trace_now_ns() + EDITOR_SLICE_NS;
    int rc = scroll_resolve_resident(&b->scroll, &b->scroll_resolver,
                                    b->index, &b->scroll_resident, 0, deadline);
    if (!rc && queued(b)) {
        /* edit-pd6: the jump landed; queued motion moves from its top, then
         * resolves (and follows the cursor) on the next slice. */
        rc = apply_queued(b); if (rc) return rc;
        return 0;
    }
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
    b->scroll_display = b->scroll; b->scroll_display_stale = false;
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
