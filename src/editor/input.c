#include "editor/private.h"
#include "layout/wrap.h"
#include "trace/trace.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <string.h>

static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static undo_state save_selection(const view *v)
{
    undo_state s; memcpy(s.bytes, &v->state.selection.cursor, 8);
    memcpy(s.bytes + 8, &v->state.selection.anchor, 8); return s;
}
static void restore_selection(view *v, const undo_state *s)
{
    memcpy(&v->state.selection.cursor, s->bytes, 8);
    memcpy(&v->state.selection.anchor, s->bytes + 8, 8);
    v->state.selection.preferred_col = VIEW_PREFERRED_UNSET;
}
static uint64_t newlines(const editor *e, uint64_t off, uint64_t len)
{
    uint64_t count = 0; piece_iter it; piece_iter_begin(&it, e->tree, off);
    const uint8_t *p; size_t n;
    while (len && piece_iter_next(&it, &p, &n)) {
        size_t take = (size_t)min64(len, n);
        for (size_t i = 0; i < take; i++) count += p[i] == '\n' ? 1u : 0u;
        len -= take;
    }
    return count;
}
static void joined_line_anchor(editor *e, uint64_t off, uint64_t line)
{
    /* Deleting the newline before the viewport invalidates its start. The
     * deleted newline count certifies the preceding line's number; certify
     * its byte start with a backward scan capped at one index chunk. */
    uint64_t start;
    if (editor_line_start_before(e, off, &start)) {
        e->repair_line_byte = start; e->repair_line_number = line;
        e->repair_line_valid = true;
    }
}
static int changed(editor *e, uint64_t off, uint64_t old, uint64_t add, uint64_t old_nl, uint64_t new_nl)
{
    if (e->buffer->mark_valid) {
        uint64_t *points[] = {&e->buffer->mark.cursor, &e->buffer->mark.anchor};
        for (size_t i = 0; i < sizeof points / sizeof points[0]; i++) {
            uint64_t at = *points[i];
            if (at > off + old) *points[i] = at - old + add;
            else if (at >= off) *points[i] = off + add;
        }
    }
    e->repair_line_valid = false;
    e->wheel_pending=false; e->wrap_wheel_rows=0;
    e->find_range_count=0;
    e->paint_ready = false; e->buffer->revision++;
    e->stats.mutations++;
    e->buffer->lines = e->buffer->lg.lines_exact ? e->buffer->lines - old_nl + new_nl :
        layout_estimated_lines(e->buffer->lines, piece_len(e->tree), old_nl, new_nl);
    e->lay.line_count_estimated = !e->buffer->lg.lines_exact;
    if (e->buffer->index) {
        /* A mutation invalidates outstanding results. Adoption and source
         * retirement belong to the maintenance turn, before input checks. */
        if (lineidx_edit(e->buffer->index, off, old, add)) return EDITOR_ERR_HISTORY;
        e->buffer->index_dirty = true;
    }
    int sr = editor_scroll_edit(e, off, old, add, old_nl, new_nl); if (sr) return sr;
    int rc = editor_begin_frame(e); if (rc) return rc;
    if (!e->lay.src || e->full_pending) { e->full_pending = true; return 0; }
    rc = layout_edit(&e->lay, off, old, add, old_nl, new_nl);
    if (rc == LAYOUT_RESET) {
        if (!e->buffer->lg.top_estimated && off + old == e->lay.first_byte && old_nl <= e->lay.first_line)
            joined_line_anchor(e, off, e->lay.first_line - old_nl);
        e->full_pending = true; return 0;
    }
    return rc < 0 ? rc : 0;
}
static int stage_delete(editor *e, uint64_t off, uint64_t len)
{
    if (!len || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
    e->ops[e->op_count++] = (editor_jop){.off=off, .len=len, .id=e->buffer->id}; return 0;
}
static int stage_insert(editor *e, uint64_t off, const uint8_t *p, size_t n)
{
    if (!n || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
    e->ops[e->op_count++] = (editor_jop){.off=off, .len=n, .at=e->stage_used, .insert=true, .id=e->buffer->id};
    memcpy(e->stage + e->stage_used, p, n); e->stage_used += n; return 0;
}
static int stage_tree(editor *e, uint64_t off, uint64_t n)
{
    if (!n || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
    /* Input remains parked until this borrowed tree range is journaled;
     * editor retirement keeps its arena alive. No post-commit allocation. */
    e->ops[e->op_count++] = (editor_jop){.off=off, .len=n, .insert=true,
        .id=e->buffer->id, .tree=e->tree};
    return 0;
}
void editor_journal_staged(editor *e)
{
    if (editor_save_leased(e) || e->stats.journal_error) return;
    /* Queue dispatch already parks behind staged ranges. A clipboard/find
     * action can stage more mutations across turns; keep its group open until
     * that action finishes. Single-record input has no marker or extra syscall. */
    bool continuing = e->action == EDITOR_ACTION_CLIP || e->find_replace_active;
    if (!e->journal_group_open && e->op_count) {
        bool grouped = continuing || e->op_count > 1u;
        for (size_t i = 0; !grouped && i < e->op_count; i++)
            grouped = e->ops[i].insert && e->ops[i].len > JOURNAL_INSERT_SLICE_BYTES;
        if (grouped) {
            uint64_t before = journal_get_stats(e->journal).accepted_sequence;
            int rc = journal_append_protected(e->journal, JOURNAL_GROUP_BEGIN, 0, NULL, 0);
            if (journal_get_stats(e->journal).accepted_sequence != before) e->journal_group_open = true;
            if (rc) { if (rc != JOURNAL_BUSY) e->stats.journal_error = rc; return; }
        }
    }
drain:
    ;
    size_t consumed = 0;
    while (consumed < e->op_count) {
        editor_jop *op = &e->ops[consumed];
        if (e->stats.journal_error) break;
        uint64_t before = journal_get_stats(e->journal).accepted_sequence;
        int rc;
        if (op->commit) rc = journal_append_protected(e->journal, JOURNAL_GROUP_COMMIT, 0, NULL, 0);
        else if (!op->insert) rc = journal_delete_protected(e->journal, op->id, op->off, op->len);
        else if (op->tree) {
            size_t n = op->len - op->progress < sizeof e->journal_scratch ?
                (size_t)(op->len - op->progress) : sizeof e->journal_scratch;
            rc = piece_read(op->tree, op->off + op->progress, e->journal_scratch, n);
            if (!rc) {
                size_t done = 0;
                rc = journal_insert_step(e->journal, op->id, op->off + op->progress,
                    e->journal_scratch, n, &done);
                op->progress += done;
                if (!rc && op->progress < op->len) rc = JOURNAL_BUSY;
            }
        } else rc = journal_insert_step(e->journal, op->id, op->off,
            e->stage + op->at, (size_t)op->len, &op->progress);
        bool accepted = journal_get_stats(e->journal).accepted_sequence != before;
        if (accepted && (!op->insert || op->progress == op->len)) {
            if (op->commit) e->journal_group_open = false;
            /* journal_records counts data records (== mutations for the
             * accounting harness); group markers are framing, not records. */
            consumed++; if (!op->commit) e->stats.journal_records++;
        }
        if (rc == JOURNAL_BUSY) break;
        if (rc) { e->stats.journal_error = rc; break; }
    }
    e->op_count -= consumed;
    if (e->op_count) memmove(e->ops, e->ops + consumed, e->op_count * sizeof *e->ops);
    else {
        e->stage_used = 0;
        if (e->journal_group_open && !continuing && !e->stats.journal_error) {
            /* Reuse the now-empty reserved op queue for a pending commit.
             * BUSY therefore retains the existing input/save/submit fences. */
            e->ops[0] = (editor_jop){.commit=true}; e->op_count = 1;
            goto drain;
        }
    }
}
static editor_delta *delta_at(editor *e, size_t i) { return &e->buffer->history[(e->buffer->history_head + i) % e->buffer->history_cap]; }
static editor_soft_delta *soft_at(editor_buffer *b, size_t i)
{ return &b->soft_history[(b->soft_head + i) % b->history_cap]; }
static void soft_record(editor *e, bool text)
{
    view_selection after = e->v.state.selection;
    e->selection_command = false;
    if (!text && after.cursor == e->old_selection.cursor && after.anchor == e->old_selection.anchor) return;
    editor_buffer *b = e->buffer;
    b->soft_base_redo = 0;
    uint64_t serial = undo_get_history(e->undo).group_serial;
    if (text && b->soft_cursor == b->soft_count && b->soft_count) {
        editor_soft_delta *last = soft_at(b, b->soft_count - 1u);
        if (last->text && last->serial == serial) { last->after = after; return; }
    }
    b->soft_count = b->soft_cursor;
    if (b->soft_count == b->history_cap) {
        b->soft_head = (b->soft_head + 1u) % b->history_cap;
        b->soft_count--; b->soft_cursor--;
    }
    *soft_at(b, b->soft_count++) = (editor_soft_delta){.before = e->old_selection,
        .after = after, .serial = serial, .text = text};
    b->soft_cursor = b->soft_count;
}
static void soft_replayed(editor *e, bool redo)
{
    editor_buffer *b = e->buffer;
    if (e->soft_replaying) { b->soft_cursor = e->soft_target; e->soft_replaying = false; return; }
    size_t at = b->soft_cursor;
    if (redo) {
        if (b->soft_base_redo) { b->soft_base_redo--; return; }
        while (at < b->soft_count && !soft_at(b, at)->text) at++;
        if (at < b->soft_count) b->soft_cursor = at + 1u;
        else b->soft_head = b->soft_count = b->soft_cursor = 0;
    } else {
        while (at && !soft_at(b, at - 1u)->text) at--;
        if (at) b->soft_cursor = at - 1u;
        else { b->soft_cursor = 0; b->soft_base_redo++; }
    }
}
static void remember(editor *e, editor_delta d)
{
    editor_buffer *b = e->buffer;
    undo_history h = undo_get_history(e->undo);
    if (b->save_tracking && b->history_cursor < b->save_cursor) b->save_tracking = false;
    /* A saved state in the discarded redo branch cannot name the new branch. */
    if (b->history_cursor < b->saved_cursor) b->saved_lost = true;
    b->history_count = b->history_cursor;
    if (b->history_cursor && b->history_serial == h.group_serial) {
        editor_delta *last = delta_at(e, b->history_cursor - 1);
        if (d.add) last->add += d.add;
        else { last->off = d.off; last->old += d.old; }
        last->after = d.after;
        /* A saved boundary extended by this burst no longer names saved bytes. */
        if (b->saved_cursor == b->history_cursor) b->saved_lost = true;
        if (b->save_tracking && b->save_cursor == b->history_cursor) b->save_tracking = false;
    } else {
        if (b->history_count == b->history_cap) {
            b->history_head = (b->history_head + 1) % b->history_cap; b->history_count--; b->history_cursor--;
            if (b->saved_cursor) b->saved_cursor--; else b->saved_lost = true;
            if (b->save_tracking) { if (b->save_cursor) b->save_cursor--; else b->save_tracking = false; }
        }
        *delta_at(e, b->history_count++) = d; b->history_cursor = b->history_count;
    }
    b->history_serial = h.group_serial;
    if (b->history_count > h.undo_groups) {
        size_t drop = b->history_count - h.undo_groups;
        b->history_head = (b->history_head + drop) % b->history_cap;
        b->history_count -= drop; b->history_cursor -= drop;
        if (b->save_tracking) { if (b->save_cursor >= drop) b->save_cursor -= drop; else b->save_tracking = false; }
        if (b->saved_cursor >= drop) b->saved_cursor -= drop;
        else { b->saved_cursor = 0; b->saved_lost = true; }
    }
}
static int end_edit_group(editor *e, const undo_state *after)
{
    e->edit_delta.after = *after;
    if (!e->explicit_group) return 0;
    e->explicit_group = false;
    return undo_group_end(e->undo, after);
}
static void remember_key(editor *e)
{
    if (e->command_sequence == e->stats.input_sequence &&
        e->command_key.kind == PLAT_EV_KEY && e->command_key.press) {
        e->buffer->last_key = e->command_key;
        e->buffer->last_binding = e->command_binding;
        e->buffer->last_key_valid = true;
    }
}
static int finish_edit(editor *e)
{
    if (e->v.state.wrap) {
        layout_wrap_row cursor; bool approximate;
        int query = layout_visual_row_fresh(&e->lay, e->tree, e->v.state.selection.cursor, &cursor, &approximate);
        if (query) return query;
        uint32_t last = e->text_grid.dims.rows - 1;
        const layout_wrap_row *bottom = &e->lay.wrap_rows[last];
        /* Undo mutates outside view_command. Its empty TYPE repairs cluster
         * boundaries, but cached EOF cannot describe newly appended rows. */
        if (cursor.start < e->v.state.visual_byte ||
            (bottom->start != LAYOUT_VOID_ROW && cursor.start > bottom->start)) {
            e->v.state.visual_byte = cursor.start;
            e->v.state.first_line = cursor.line;
            e->v.state.first_byte = cursor.line_start;
        }
        e->v.state.approximate |= approximate;
    }
    undo_state after = save_selection(&e->v);
    int rc = end_edit_group(e, &after); if (rc) return rc;
    remember(e, e->edit_delta); soft_record(e, true); editor_modified(e); e->action = EDITOR_ACTION_NONE;
    remember_key(e);
    trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
    return editor_refresh_cursor(e, e->old_cursor);
}
/* Empty TYPE is the view's boundary repair/follow; its zero-length piece
 * insert is a no-op. Every actual text mutation goes through undo. */
static int repair(editor *e, uint64_t target)
{
    view_change c; int rc;
    if (e->edit_selection_valid) {
        e->v.state.selection = e->edit_selection;
        e->edit_selection_valid = false;
        /* Equal-length transforms also invalidate grapheme boundaries. Repair
         * both endpoints explicitly; legacy empty TYPE only detects length or
         * collapsed-caret changes. */
        rc = view_restore(&e->v, &e->v.state, &c);
    } else {
        e->v.state.selection.cursor = e->v.state.selection.anchor = target;
        rc = view_command(&e->v, VIEW_TYPE, false, NULL, 0, &c);
    }
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) { editor_seed_view_lines(e); e->action = EDITOR_ACTION_REPAIR; return 0; }
    if (rc) return rc;
    return finish_edit(e);
}
static int finish_replay_follow(editor *e)
{
    if (e->replay_anchor_trusted) {
        e->v.state.selection.anchor = e->replay_selection.anchor == e->replay_selection.cursor ?
            e->v.state.selection.cursor : e->replay_selection.anchor;
        e->replay_anchor_trusted = false;
    }
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->action = EDITOR_ACTION_NONE;
    if (e->selection_command) soft_record(e, false);
    return editor_refresh_cursor(e, e->old_cursor);
}
static int replay_follow(editor *e, bool trust_anchor)
{
    e->replay_anchor_trusted = trust_anchor;
    if (trust_anchor) {
        /* Undo/soft-history endpoints already came from completed selections.
         * Follow the cursor without re-scanning a distant, unchanged anchor;
         * new edit plans still normalize both endpoints in repair(). */
        e->replay_selection = e->v.state.selection;
        e->v.state.selection.anchor = e->v.state.selection.cursor;
    }
    /* Trusted history follows the cursor as main's replay did (empty TYPE on
     * a collapsed selection); a new plan's two endpoints need a restore. */
    view_change c;
    int rc = trust_anchor ? view_command(&e->v, VIEW_TYPE, false, NULL, 0, &c) : view_restore(&e->v, &e->v.state, &c);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) { editor_seed_view_lines(e); e->action = EDITOR_ACTION_REPLAY; return 0; }
    if (rc) return rc;
    return finish_replay_follow(e);
}
typedef enum text_kind { TEXT_BYTES, TEXT_ENTER, TEXT_BRACE } text_kind;
static int prefix_error(editor *e, int cause)
{
    e->edit_selection_valid = false;
    undo_state after = save_selection(&e->v);
    (void)end_edit_group(e, &after);
    if (e->edit_delta.old || e->edit_delta.add) { remember(e, e->edit_delta); soft_record(e, true); editor_modified(e); }
    e->action = EDITOR_ACTION_NONE; return cause;
}
static int check_index_edit(editor *e, uint64_t off, uint64_t old, uint64_t add)
{
    if (e->buffer->index && lineidx_edit_check(e->buffer->index, off, old, add)) {
        e->stats.rejected_commands++; return EDITOR_ERR_CAPACITY;
    }
    return 0;
}
static int delete_part(editor *e, uint64_t lo, uint64_t old, undo_kind kind,
                       const undo_state *before)
{
    if (!old) return 0;
    int check = check_index_edit(e, lo, old, 0); if (check) return check;
    uint64_t nl = newlines(e, lo, old);
    undo_state after = *before; memcpy(after.bytes, &lo, 8); memcpy(after.bytes + 8, &lo, 8);
    int rc = undo_delete(e->undo, lo, old, kind, e->edit_time_ns, before, &after); if (rc) return rc;
    e->edit_delta.off = lo; e->edit_delta.old += old;
    e->v.state.selection.cursor = e->v.state.selection.anchor = lo;
    rc = stage_delete(e, lo, old); if (!rc) rc = changed(e, lo, old, 0, nl, 0); return rc;
}
static int mutate_text(editor *e, uint64_t lo, uint64_t old, const uint8_t *text,
                       size_t n, undo_kind kind, text_kind tk)
{
    if (!old && !n && tk == TEXT_BYTES) { e->v.state.selection = e->old_selection; e->action = EDITOR_ACTION_NONE; return editor_refresh_cursor(e, e->old_cursor); }
    if (e->journal && (e->op_count + 3 > EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used)) {
        e->stats.rejected_commands++; return editor_refresh_cursor(e, e->old_cursor);
    }
    if (tk == TEXT_BYTES) {
        int check = check_index_edit(e, lo, old, n); if (check) return check;
    }
    /* Resolve indent against the post-selection logical source before opening
     * an undo group or deleting/staging any bytes. LIMIT rejects one command;
     * a later key retries the same unchanged buffer and selection. */
    indent_edit edit={0};
    indent_code ir=INDENT_OK;
    if (tk == TEXT_ENTER) {
        ir=indent_on_enter_after_delete(e->tree,lo,lo+old,e->indent_bytes,EDITOR_STAGE_BYTES,&n);
        text=e->indent_bytes;
    } else if (tk == TEXT_BRACE) {
        ir=indent_on_close_brace_after_delete(e->tree,lo,lo+old,e->buffer->style,&edit);
        e->indent_bytes[0]=edit.bytes[0]; text=e->indent_bytes; n=edit.length;
    }
    if (ir != INDENT_OK || (e->journal && n > EDITOR_STAGE_BYTES-e->stage_used)) {
        e->stats.rejected_commands++; return editor_refresh_cursor(e,e->old_cursor);
    }
    e->v.state.selection = e->old_selection;
    undo_state before = save_selection(&e->v), after = before;
    e->explicit_group = tk != TEXT_BYTES || (old && n) ||
        (e->old_selection.cursor != e->old_selection.anchor);
    int rc = e->explicit_group ? undo_group_begin(e->undo, &before) : 0; if (rc) return rc;
    e->edit_delta = (editor_delta){.off = lo, .before = before};
    rc = delete_part(e, lo, old, kind, &before); if (rc) return prefix_error(e, rc);
    if (tk == TEXT_BRACE) {
        rc = delete_part(e, edit.lo, edit.hi - edit.lo, UNDO_DELETE, &before); if (rc) return prefix_error(e, rc);
        lo = edit.lo;
    }
    if (n) {
        rc = check_index_edit(e, lo, 0, n); if (rc) return prefix_error(e, rc);
        uint64_t target = lo + n, nl = 0;
        for (size_t i = 0; i < n; i++) nl += text[i] == '\n' ? 1u : 0u;
        memcpy(after.bytes, &target, 8); memcpy(after.bytes + 8, &target, 8);
        rc = undo_insert(e->undo, lo, text, n, e->edit_time_ns, &before, &after);
        if (rc) return prefix_error(e, rc);
        e->edit_delta.add = n; e->v.state.selection.cursor = e->v.state.selection.anchor = target;
        rc = stage_insert(e, lo, text, n); if (!rc) rc = changed(e, lo, 0, n, 0, nl);
        if (rc) return prefix_error(e, rc);
    }
    return repair(e, lo + n);
}
static int mutate(editor *e, uint64_t lo, uint64_t old, const uint8_t *text, size_t n, undo_kind kind)
{ return mutate_text(e, lo, old, text, n, kind, TEXT_BYTES); }
int editor_replace_begin(editor *e, size_t len)
{
    uint64_t lo = min64(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t old = max64(e->old_selection.cursor, e->old_selection.anchor) - lo;
    int rc = check_index_edit(e, lo, old, len); if (rc) return rc;
    undo_state before = save_selection(&e->v);
    undo_break_burst(e->undo);
    rc = undo_group_begin(e->undo, &before); if (rc) return rc;
    e->explicit_group = true;
    e->edit_delta = (editor_delta){.off = lo, .before = before};
    return 0;
}
int editor_replace_delete(editor *e, uint64_t off, uint64_t len)
{
    int rc = delete_part(e, off, len, UNDO_DELETE, &e->edit_delta.before);
    return rc ? prefix_error(e, rc) : 0;
}
int editor_replace_insert(editor *e, uint64_t off, const uint8_t *bytes, size_t len)
{
    undo_state after = e->edit_delta.before;
    uint64_t target = off + len, nl = 0;
    memcpy(after.bytes, &target, sizeof target);
    memcpy(after.bytes + sizeof target, &target, sizeof target);
    for (size_t i = 0; i < len; i++) nl += bytes[i] == '\n' ? 1u : 0u;
    int rc = undo_insert(e->undo, off, bytes, len, e->edit_time_ns, &e->edit_delta.before, &after);
    if (rc) return prefix_error(e, rc);
    e->edit_delta.add += len;
    e->v.state.selection.cursor = e->v.state.selection.anchor = target;
    rc = stage_insert(e, off, bytes, len);
    if (!rc) rc = changed(e, off, 0, len, 0, nl);
    return rc ? prefix_error(e, rc) : 0;
}
int editor_replace_end(editor *e, uint64_t target) { return repair(e, target); }
static bool default_edit_key(keys_action action)
{
    switch (action) {
    case KEYS_ACTION_DUPLICATE_LINE: case KEYS_ACTION_DELETE_LINE:
    case KEYS_ACTION_MOVE_LINE_UP: case KEYS_ACTION_MOVE_LINE_DOWN:
    case KEYS_ACTION_JOIN_LINES: case KEYS_ACTION_INSERT_LINE_AFTER:
    case KEYS_ACTION_INSERT_LINE_BEFORE: case KEYS_ACTION_DELETE_TO_BOL:
    case KEYS_ACTION_DELETE_TO_EOL: case KEYS_ACTION_INDENT:
    case KEYS_ACTION_TOGGLE_COMMENT:
    case KEYS_ACTION_UNINDENT: case KEYS_ACTION_TRANSPOSE:
    case KEYS_ACTION_SORT_LINES: case KEYS_ACTION_UPPER_CASE:
    case KEYS_ACTION_LOWER_CASE: return true;
    case KEYS_ACTION_SHOW_AT_CENTER: case KEYS_ACTION_SKIP_NEXT_OCCURRENCE:
    case KEYS_ACTION_ADD_NEXT_OCCURRENCE:
    case KEYS_ACTION_SUBWORD_LEFT: case KEYS_ACTION_SUBWORD_RIGHT:
    case KEYS_ACTION_GOTO_BRACKET: case KEYS_ACTION_SELECT_BRACKETS:
    case KEYS_ACTION_SET_MARK: case KEYS_ACTION_SELECT_TO_MARK:
    case KEYS_ACTION_SWAP_MARK: case KEYS_ACTION_CLEAR_MARK:
    case KEYS_ACTION_DELETE_TO_MARK: case KEYS_ACTION_YANK:
    case KEYS_ACTION_TOGGLE_OVERWRITE: return true;
    default: return false;
    }
}
static int default_edit_notice(editor *e)
{
    e->stats.unimplemented_actions++;
    e->unimplemented_frame = e->grid.frame_id;
    return editor_refresh_cursor(e, e->old_cursor);
}
static bool same_replacement(const editor *e, const view_edit_plan *plan, const uint8_t *out)
{
    if (plan->old_len != plan->length) return false;
    piece_iter it; piece_iter_begin(&it, e->tree, plan->offset);
    size_t at = 0, n; const uint8_t *bytes;
    while (at < plan->length && piece_iter_next(&it, &bytes, &n)) {
        if (n > plan->length - at) n = plan->length - at;
        if (memcmp(out + at, bytes, n)) return false;
        at += n;
    }
    return at == plan->length;
}
static int show_at_center(editor *e)
{
    lineidx_src source = editor_source(e->buffer);
    lineidx_result cursor = lineidx_byte_to_line(e->buffer->index, &source, e->v.state.selection.cursor);
    if (!cursor.exact) return default_edit_notice(e);
    uint64_t half = e->v.config.rows / 2u; /* midpoint of the visible text rows (3kk adds an overscan text_grid row when unwrapped) */
    uint64_t line = cursor.value > half ? cursor.value - half : 0;
    lineidx_result start;
    uint64_t visual;
    if (e->v.state.wrap) {
        layout_wrap_row row; bool approximate;
        int rc = layout_visual_row(&e->lay, e->tree, e->v.state.selection.cursor, 0, &row, &approximate);
        if (rc || approximate) return default_edit_notice(e);
        if (e->v.state.visual_end && row.start == e->v.state.selection.cursor && row.start) {
            rc = layout_visual_row(&e->lay, e->tree, row.start, -1, &row, &approximate);
            if (rc || approximate) return default_edit_notice(e);
        }
        uint64_t origin = row.start;
        for (uint64_t i = 0; i < half && row.start; i++) {
            rc = layout_visual_row(&e->lay, e->tree, row.start, -1, &row, &approximate);
            if (rc || approximate || origin - row.start > VIEW_SCAN_BOUND) return default_edit_notice(e);
        }
        visual = row.start;
        lineidx_result logical = lineidx_byte_to_line(e->buffer->index, &source, visual);
        if (!logical.exact) return default_edit_notice(e);
        line = logical.value;
        start = lineidx_line_to_byte(e->buffer->index, &source, line);
    } else {
        start = lineidx_line_to_byte(e->buffer->index, &source, line);
        visual = start.value;
    }
    if (!start.exact) return default_edit_notice(e);
    e->v.state.first_line = line; e->v.state.first_byte = start.value;
    e->v.state.visual_byte = visual; e->v.state.approximate = false;
    undo_break_burst(e->undo); e->selection_command = false;
    /* Unwrapped viewports are owned by the scroll state since 3kk. */
    int seek = editor_scroll_seek(e, start.value); if (seek) return seek;
    return editor_full_layout(e);
}
static int default_edit(editor *e, const keys_binding *binding)
{
    view_edit_plan plan;
    uint8_t *out = e->indent_bytes;
    /* The synchronous fast path inherits view's certified scan window; the
     * existing larger staging buffer is shared with indentation and painting. */
    size_t cap = VIEW_WINDOW;
    int rc;
    switch (binding->action) {
    case KEYS_ACTION_DUPLICATE_LINE: rc = view_duplicate_line(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_DELETE_LINE: rc = view_delete_line(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_MOVE_LINE_UP: rc = view_move_line_up(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_MOVE_LINE_DOWN: rc = view_move_line_down(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_JOIN_LINES: rc = view_join_lines(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_INSERT_LINE_AFTER: rc = view_insert_line_after(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_INSERT_LINE_BEFORE: rc = view_insert_line_before(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_DELETE_TO_BOL:
        rc = binding->args.chord == KEYS_CHORD_K ? view_delete_to_soft_bol(&e->v, out, cap, &plan) : view_delete_to_bol(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_DELETE_TO_EOL:
        rc = binding->args.chord == KEYS_CHORD_K ? view_delete_to_soft_eol(&e->v, out, cap, &plan) : view_delete_to_eol(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_INDENT: rc = view_indent(&e->v, e->buffer->style.uses_tabs, out, cap, &plan); break;
    case KEYS_ACTION_UNINDENT: rc = view_unindent(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_TOGGLE_COMMENT: rc = view_toggle_comment(&e->v, e->buffer->path, (binding->args.flags & KEYS_ARG_BLOCK) != 0, out, cap, &plan); break;
    case KEYS_ACTION_TRANSPOSE: rc = view_transpose(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_SORT_LINES: rc = view_sort_lines(&e->v, (binding->args.flags & KEYS_ARG_CASE_SENSITIVE) != 0, out, cap, &plan); break;
    case KEYS_ACTION_UPPER_CASE: rc = view_upper_case(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_LOWER_CASE: rc = view_lower_case(&e->v, out, cap, &plan); break;
    case KEYS_ACTION_SHOW_AT_CENTER: return show_at_center(e);
    case KEYS_ACTION_SKIP_NEXT_OCCURRENCE: rc = view_skip_next_occurrence(&e->v, &plan); break;
    case KEYS_ACTION_ADD_NEXT_OCCURRENCE:
        if (e->v.state.selection.cursor != e->v.state.selection.anchor) return default_edit_notice(e);
        rc = view_select_word(&e->v, &plan); break;
    case KEYS_ACTION_SUBWORD_LEFT: rc = view_subword_left(&e->v, (binding->args.flags & KEYS_ARG_EXTEND) != 0, &plan); break;
    case KEYS_ACTION_SUBWORD_RIGHT: rc = view_subword_right(&e->v, (binding->args.flags & KEYS_ARG_EXTEND) != 0, &plan); break;
    case KEYS_ACTION_GOTO_BRACKET: rc = view_goto_bracket(&e->v, &plan); break;
    case KEYS_ACTION_SELECT_BRACKETS: rc = view_select_brackets(&e->v, &plan); break;
    case KEYS_ACTION_SET_MARK:
        view_set_mark(&e->v, &e->buffer->mark, &e->buffer->mark_valid);
        return editor_refresh_cursor(e, e->old_cursor);
    case KEYS_ACTION_CLEAR_MARK:
        view_clear_mark(&e->buffer->mark_valid);
        return editor_refresh_cursor(e, e->old_cursor);
    case KEYS_ACTION_TOGGLE_OVERWRITE:
        e->buffer->overwrite = !e->buffer->overwrite;
        undo_break_burst(e->undo);
        return editor_refresh_cursor(e, e->old_cursor);
    case KEYS_ACTION_SELECT_TO_MARK: rc = view_select_to_mark(&e->v, &e->buffer->mark, e->buffer->mark_valid, &plan); break;
    case KEYS_ACTION_SWAP_MARK: rc = view_swap_mark(&e->v, &e->buffer->mark, &e->buffer->mark_valid, &plan); break;
    case KEYS_ACTION_DELETE_TO_MARK: rc = view_delete_to_mark(&e->v, &e->buffer->mark, e->buffer->mark_valid, &plan); break;
    case KEYS_ACTION_YANK:
        if (!e->kill_len) return editor_refresh_cursor(e, e->old_cursor);
        rc = view_yank(&e->v, e->kill_bytes, e->kill_len, out, cap, &plan); break;
    default: return EDITOR_ERR_ARG;
    }
    if (rc == PIECE_ERR_NOMEM) return default_edit_notice(e);
    if (rc) return rc;
    undo_break_burst(e->undo);
    if (same_replacement(e, &plan, out)) {
        switch (binding->action) {
        case KEYS_ACTION_DUPLICATE_LINE: case KEYS_ACTION_DELETE_LINE:
        case KEYS_ACTION_MOVE_LINE_UP: case KEYS_ACTION_MOVE_LINE_DOWN:
        case KEYS_ACTION_JOIN_LINES: case KEYS_ACTION_INSERT_LINE_AFTER:
        case KEYS_ACTION_INSERT_LINE_BEFORE: case KEYS_ACTION_DELETE_TO_BOL:
        case KEYS_ACTION_DELETE_TO_EOL: case KEYS_ACTION_INDENT:
        case KEYS_ACTION_TOGGLE_COMMENT:
    case KEYS_ACTION_UNINDENT: case KEYS_ACTION_TRANSPOSE:
        case KEYS_ACTION_SORT_LINES: case KEYS_ACTION_UPPER_CASE:
        case KEYS_ACTION_LOWER_CASE: case KEYS_ACTION_DELETE_TO_MARK:
        case KEYS_ACTION_YANK: remember_key(e); break;
        default: break;
        }
        e->v.state.selection = plan.selection;
        e->selection_command = true;
        return replay_follow(e, false);
    }
    if (e->journal && (e->op_count + 2u > EDITOR_STAGE_OPS ||
        plan.length > EDITOR_STAGE_BYTES - e->stage_used)) return default_edit_notice(e);
    rc = check_index_edit(e, plan.offset, plan.old_len, plan.length); if (rc) return rc;
    bool killing = binding->action == KEYS_ACTION_DELETE_LINE ||
        binding->action == KEYS_ACTION_DELETE_TO_BOL || binding->action == KEYS_ACTION_DELETE_TO_EOL ||
        binding->action == KEYS_ACTION_DELETE_TO_MARK;
    bool forward = binding->action == KEYS_ACTION_DELETE_LINE || binding->action == KEYS_ACTION_DELETE_TO_EOL;
    bool merge = killing && e->kill_len && e->kill_buffer == e->buffer->id &&
        (forward ? plan.offset : plan.offset + plan.old_len) == e->kill_at;
    uint8_t killed[VIEW_WINDOW];
    if (killing) {
        if (plan.old_len > sizeof killed - (merge ? e->kill_len : 0)) return default_edit_notice(e);
        rc = piece_read(e->tree, plan.offset, killed, (size_t)plan.old_len); if (rc) return rc;
    }
    undo_state before = save_selection(&e->v);
    rc = undo_group_begin(e->undo, &before); if (rc) return rc;
    e->explicit_group = true;
    e->edit_delta = (editor_delta){.off = plan.offset, .before = before};
    rc = delete_part(e, plan.offset, plan.old_len, UNDO_DELETE, &before);
    if (rc) return prefix_error(e, rc);
    if (killing) {
        size_t len = (size_t)plan.old_len;
        if (!merge) e->kill_len = 0;
        if (forward) memcpy(e->kill_bytes + e->kill_len, killed, len);
        else { memmove(e->kill_bytes + len, e->kill_bytes, e->kill_len); memcpy(e->kill_bytes, killed, len); }
        e->kill_len += len; e->kill_at = plan.offset; e->kill_buffer = e->buffer->id;
    }
    if (binding->action == KEYS_ACTION_YANK) e->kill_buffer = 0; /* seal the top */
    if (plan.length) {
        rc = editor_replace_insert(e, plan.offset, out, plan.length); if (rc) return rc;
    }
    e->edit_selection = plan.selection; e->edit_selection_valid = true;
    return repair(e, plan.selection.cursor);
}
static int replay_slice(editor *e)
{
    editor_delta d = e->replay_delta; bool redo = e->replay_redo;
    uint64_t old = redo ? d.old : d.add, add = redo ? d.add : d.old;
    undo_change c;
    uint64_t deadline = trace_now_ns() + UINT64_C(100000);
    int rc = redo ? undo_redo_slice(e->undo, 1, 8, deadline, &c) : undo_undo_slice(e->undo, 1, 8, deadline, &c);
    if (rc == UNDO_MORE) { e->action = EDITOR_ACTION_REPLAY_WORK; return 0; }
    e->action = EDITOR_ACTION_NONE;
    if (c.groups) {
        uint64_t after_len = piece_len(e->tree), before_len = e->replay_before_len;
        add = after_len >= before_len ? old + after_len - before_len : old - (before_len - after_len);
        int jr = stage_delete(e, d.off, old); if (!jr) jr = stage_tree(e, d.off, add);
        if (jr) return jr;
        int er = changed(e, d.off, old, add, e->replay_old_nl, newlines(e, d.off, add)); if (er) return er;
        restore_selection(&e->v, redo ? &d.after : &d.before);
        if (redo) e->buffer->history_cursor++; else e->buffer->history_cursor--;
        soft_replayed(e, redo);
        editor_modified(e); trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
        er = replay_follow(e, true); if (er) return er;
    }
    return rc;
}
static int replay_history(editor *e, bool redo)
{
    if ((!redo && !e->buffer->history_cursor) || (redo && e->buffer->history_cursor == e->buffer->history_count)) return editor_refresh_cursor(e, e->old_cursor);
    editor_delta d = *delta_at(e, redo ? e->buffer->history_cursor : e->buffer->history_cursor - 1);
    if (e->journal && e->op_count + 2 > EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
    int check = check_index_edit(e, d.off, redo ? d.old : d.add, redo ? d.add : d.old);
    if (check) return check;
    e->replay_delta = d; e->replay_redo = redo;
    e->replay_old_nl = newlines(e, d.off, redo ? d.old : d.add);
    e->replay_before_len = piece_len(e->tree);
    return replay_slice(e);
}
static int soft_history(editor *e, bool redo)
{
    editor_buffer *b = e->buffer;
    undo_break_burst(e->undo);
    if (redo && b->soft_base_redo) return replay_history(e, true);
    if ((!redo && !b->soft_cursor) || (redo && b->soft_cursor == b->soft_count))
        return replay_history(e, redo);
    size_t at = redo ? b->soft_cursor : b->soft_cursor - 1u;
    editor_soft_delta d = *soft_at(b, at);
    if (d.text) {
        if ((!redo && !b->history_cursor) || (redo && b->history_cursor == b->history_count))
            return editor_refresh_cursor(e, e->old_cursor);
        e->soft_target = redo ? at + 1u : at; e->soft_replaying = true;
        int rc = replay_history(e, redo);
        if (rc) e->soft_replaying = false;
        return rc;
    }
    b->soft_cursor = redo ? at + 1u : at;
    e->v.state.selection = redo ? d.after : d.before;
    return replay_follow(e, true);
}
static bool move_key(keys_action action, view_key *out)
{
    switch (action) {
    case KEYS_ACTION_LEFT: *out = VIEW_LEFT; break;
    case KEYS_ACTION_RIGHT: *out = VIEW_RIGHT; break;
    case KEYS_ACTION_UP: *out = VIEW_UP; break;
    case KEYS_ACTION_DOWN: *out = VIEW_DOWN; break;
    case KEYS_ACTION_WORD_LEFT: *out = VIEW_WORD_LEFT; break;
    case KEYS_ACTION_WORD_RIGHT: *out = VIEW_WORD_RIGHT; break;
    case KEYS_ACTION_PAGE_UP: *out = VIEW_PAGE_UP; break;
    case KEYS_ACTION_PAGE_DOWN: *out = VIEW_PAGE_DOWN; break;
    case KEYS_ACTION_HOME: *out = VIEW_HOME; break;
    case KEYS_ACTION_END: *out = VIEW_END; break;
    case KEYS_ACTION_DOC_HOME: *out = VIEW_DOC_HOME; break;
    case KEYS_ACTION_DOC_END: *out = VIEW_DOC_END; break;
    case KEYS_ACTION_SELECT_ALL: *out = VIEW_SELECT_ALL; break;
    case KEYS_ACTION_SELECT_LINE: *out = VIEW_SELECT_LINE; break;
    default: return false;
    }
    return true;
}
static void pointer_unit_finish(editor *e)
{
    if (!e->pointer_unit_pending) return;
    e->pointer_unit_pending=false;
    uint64_t lo=min64(e->v.state.selection.anchor,e->v.state.selection.cursor);
    uint64_t hi=max64(e->v.state.selection.anchor,e->v.state.selection.cursor);
    if (e->pointer_unit_initial) { e->text_drag_lo=lo; e->text_drag_hi=hi; }
    else if (lo<e->text_drag_lo) {
        e->v.state.selection.anchor=e->text_drag_hi; e->v.state.selection.cursor=lo;
    } else {
        e->v.state.selection.anchor=e->text_drag_lo; e->v.state.selection.cursor=hi>e->text_drag_hi ? hi : e->text_drag_hi;
    }
}
/* Go-to-line owns a prompt, never document text. Preview state is transient;
 * Enter records one selection command, Escape restores the original viewport.
 * Seek continuations use the index's existing bounded foreground query. */
/* Unwrapped viewports are owned by the 3kk scroll state: a top written only
 * into the view is mirrored back to the displayed one on the next refresh.
 * Adopt a certified line start there, or seek to it when the scroll policy
 * clips it (EOF), as show_at_center does. */
static int goto_scroll_to(editor *e, uint64_t byte, uint64_t line, bool approximate)
{
    if (e->v.state.wrap || editor_scroll_place(e, byte, line, approximate)) return 0;
    return editor_scroll_seek(e, byte);
}
static int goto_restore(editor *e)
{
    e->v.state = e->go.original;
    const scroll_state *s = &e->go.original_scroll;
    if (e->v.state.wrap || !e->buffer->scroll_ready) return 0;
    return goto_scroll_to(e, s->first_byte, s->first_line, s->approximate);
}
int editor_goto_cancel(editor *e)
{
    if (!e->go.active) return 0;
    if (e->go.owner == e->buffer->id && e->go.generation == e->buffer->source_generation &&
        !e->buffer->source_stale) { int rc = goto_restore(e); if (rc) return rc; }
    e->go.active = false; e->go.accept = false;
    if (e->action == EDITOR_ACTION_GOTO) e->action = EDITOR_ACTION_NONE;
    keys_reset(&e->keys);
    return editor_full_layout(e);
}
static int goto_continue(editor *e)
{
    editor_goto *go = &e->go;
    if (!go->active) { e->action = EDITOR_ACTION_NONE; return 0; }
    if (go->owner != e->buffer->id || go->generation != e->buffer->source_generation ||
        e->buffer->source_stale) return editor_goto_cancel(e);
    lineidx_src source = editor_source(e->buffer);
    lineidx_result count = lineidx_line_count(e->buffer->index);
    uint64_t line = go->line;
    if (count.exact && line >= count.value) line = count.value - 1u;
    lineidx_result pos = lineidx_seek_line(e->buffer->index, &source, line, VIEW_SCAN_BOUND);
    if (!pos.exact) return 0;
    count = lineidx_line_count(e->buffer->index);
    if (count.exact && line >= count.value) { go->line = count.value - 1u; return 0; }
    e->action = EDITOR_ACTION_NONE;
    e->v.state.selection = (view_selection){pos.value, pos.value, VIEW_PREFERRED_UNSET};
    e->v.state.first_line = line;
    e->v.state.first_byte = e->v.state.visual_byte = pos.value;
    e->v.state.visual_end = false; e->v.state.approximate = false; e->v.state.hscroll = 0;
    editor_seed_view_lines(e);
    int rc = goto_scroll_to(e, pos.value, line, false); if (rc) return rc;
    if (go->accept) {
        e->old_selection = go->original.selection;
        soft_record(e, false);
        go->active = false; keys_reset(&e->keys);
    }
    return editor_full_layout(e);
}
static int goto_query(editor *e)
{
    editor_goto *go = &e->go;
    if (go->kind != KEYS_ACTION_GOTO_LINE) return editor_full_layout(e);
    go->valid = go->length != 0; go->line = 0;
    /* Decimal positional notation: next = previous * base + digit;
     * rearrange against UINT64_MAX before multiplying to avoid overflow. */
    const uint64_t base = '9' - '0' + 1u;
    for (size_t i = 0; i < go->length; i++) {
        uint64_t digit = (uint8_t)go->digits[i] - (uint8_t)'0';
        if (digit >= base || go->line > (UINT64_MAX - digit) / base) { go->valid = false; break; }
        go->line = go->line * base + digit;
    }
    if (go->valid) {
        if (go->line) go->line--; /* UI line numbers begin at one. */
        e->action = EDITOR_ACTION_GOTO;
        return goto_continue(e);
    }
    e->action = EDITOR_ACTION_NONE;
    int rc = goto_restore(e); if (rc) return rc;
    return editor_full_layout(e);
}
static int goto_key(editor *e, const plat_event *ev, keys_action kind)
{
    editor_goto *go = &e->go;
    undo_break_burst(e->undo);
    if (!go->active) {
        *go = (editor_goto){.active = true, .original = e->v.state, .original_scroll = e->buffer->scroll_display,
            .owner = e->buffer->id, .generation = e->buffer->source_generation, .kind = kind};
        if (kind == KEYS_ACTION_SAVE_AS && *e->buffer->path) {
            go->length = go->cursor = strlen(e->buffer->path);
            memcpy(go->digits, e->buffer->path, go->length + 1u); go->selected = true;
        }
        keys_reset(&e->keys);
        return editor_full_layout(e);
    }
    if (ev->keysym == XKB_KEY_Escape) return editor_goto_cancel(e);
    if (ev->keysym == XKB_KEY_Return || ev->keysym == XKB_KEY_KP_Enter) {
        if (go->length && go->kind != KEYS_ACTION_GOTO_LINE) {
            e->file_command = go->kind;
            memcpy(e->file_command_path, go->digits, go->length + 1u);
            e->file_command_owner = go->owner; e->file_command_generation = go->generation;
            go->active = false; keys_reset(&e->keys);
            return editor_full_layout(e);
        }
        if (!go->length) return editor_goto_cancel(e);
        if (!go->valid) return editor_full_layout(e);
        go->accept = true; e->action = EDITOR_ACTION_GOTO;
        return goto_continue(e);
    }
    if ((ev->mods & PLAT_MOD_CTRL) && (ev->keysym == XKB_KEY_a || ev->keysym == XKB_KEY_A)) {
        go->selected = true; return editor_full_layout(e);
    }
    if (ev->keysym == XKB_KEY_Left || ev->keysym == XKB_KEY_Home) {
        go->cursor = ev->keysym == XKB_KEY_Home || go->selected ? 0 : go->cursor ? (go->kind == KEYS_ACTION_GOTO_LINE ? go->cursor - 1u : utf8_grapheme_prev((const uint8_t *)go->digits, go->cursor)) : 0;
        go->selected = false; return editor_full_layout(e);
    }
    if (ev->keysym == XKB_KEY_Right || ev->keysym == XKB_KEY_End) {
        if (ev->keysym == XKB_KEY_End || go->selected) go->cursor = go->length;
        else if (go->cursor < go->length) go->cursor += go->kind == KEYS_ACTION_GOTO_LINE ? 1u : utf8_grapheme_next((const uint8_t *)go->digits + go->cursor, go->length - go->cursor);
        go->selected = false; return editor_full_layout(e);
    }
    if (ev->keysym == XKB_KEY_BackSpace || ev->keysym == XKB_KEY_Delete) {
        if (go->selected) { go->length = go->cursor = 0; go->selected = false; }
        else if (ev->keysym == XKB_KEY_BackSpace && go->cursor) {
            size_t previous = go->kind == KEYS_ACTION_GOTO_LINE ? go->cursor - 1u : utf8_grapheme_prev((const uint8_t *)go->digits, go->cursor);
            size_t removed = go->cursor - previous;
            memmove(go->digits + previous, go->digits + go->cursor, go->length - go->cursor);
            go->cursor = previous; go->length -= removed;
        } else if (ev->keysym == XKB_KEY_Delete && go->cursor < go->length) {
            size_t removed = go->kind == KEYS_ACTION_GOTO_LINE ? 1u : utf8_grapheme_next((const uint8_t *)go->digits + go->cursor, go->length - go->cursor);
            memmove(go->digits + go->cursor, go->digits + go->cursor + removed, go->length - go->cursor - removed);
            go->length -= removed;
        }
        go->digits[go->length] = 0; return goto_query(e);
    }
    if (!(ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_ALT | PLAT_MOD_SUPER)) && ev->utf8_len) {
        for (size_t i = 0; i < ev->utf8_len; i++) {
            if (go->kind == KEYS_ACTION_GOTO_LINE) {
                if (ev->utf8[i] < '0' || ev->utf8[i] > '9') return editor_full_layout(e);
            } else if (!ev->utf8[i] || ev->utf8[i] == '\n' || ev->utf8[i] == '\r') return editor_full_layout(e);
        }
        size_t length = go->selected ? 0 : go->length;
        /* (P) retain the preceding decimal-width limit for go-to-line. */
        size_t cap = go->kind == KEYS_ACTION_GOTO_LINE ? sizeof(uint64_t) * CHAR_BIT : sizeof go->digits - 1u;
        if (ev->utf8_len > cap - length) return editor_full_layout(e);
        if (go->selected) { go->length = go->cursor = 0; go->selected = false; }
        memmove(go->digits + go->cursor + ev->utf8_len, go->digits + go->cursor, go->length - go->cursor);
        memcpy(go->digits + go->cursor, ev->utf8, ev->utf8_len);
        go->length += ev->utf8_len; go->cursor += ev->utf8_len; go->digits[go->length] = 0;
        return goto_query(e);
    }
    return editor_full_layout(e);
}

int editor_continue_action(editor *e)
{
    if (e->action == EDITOR_ACTION_GOTO) return goto_continue(e);
    if (e->action == EDITOR_ACTION_CLIP) return editor_clip_continue(e);
    if (e->action == EDITOR_ACTION_REPLAY_WORK) return replay_slice(e);
    view_change c; int rc = view_continue(&e->v, &c);
    if (rc == VIEW_MORE) editor_scroll_mirror(e);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) return 0;
    if (rc) return rc;
    if (e->action == EDITOR_ACTION_DELETE) {
        uint64_t a = e->old_selection.cursor, b = e->v.state.selection.cursor;
        return mutate(e, min64(a, b), max64(a, b) - min64(a, b), NULL, 0, e->delete_kind);
    }
    if (e->action == EDITOR_ACTION_REPAIR) return finish_edit(e);
    if (e->action == EDITOR_ACTION_REPLAY) return finish_replay_follow(e);
    if (e->selection_command) soft_record(e, false);
    e->action = EDITOR_ACTION_NONE;
    pointer_unit_finish(e);
    return editor_refresh_cursor(e, e->old_cursor);
}
bool editor_event_control(const editor *e, const plat_event *ev)
{
    if (ev->kind != PLAT_EV_KEY) {
        if (ev->kind == PLAT_EV_BUTTON || ev->kind == PLAT_EV_MOTION)
            return ev->y >= 0 && (uint32_t)ev->y < e->grid.dims.cell_h;
        return true;
    }
    if (!ev->press) return true;
    keys_state state = e->keys; const keys_binding *binding = NULL;
    if (config_keys_lookup(&e->preferences, &state, ev->keysym, ev->mods, &binding) != KEYS_MATCH || !binding) return false;
    switch (binding->action) {
    case KEYS_ACTION_NEXT_VIEW_STACK: case KEYS_ACTION_PREVIOUS_VIEW_STACK:
    case KEYS_ACTION_NEXT_VIEW: case KEYS_ACTION_PREVIOUS_VIEW:
    case KEYS_ACTION_CLOSE: case KEYS_ACTION_CLOSE_FILE: case KEYS_ACTION_REOPEN_FILE:
    case KEYS_ACTION_SELECT_TAB: case KEYS_ACTION_SELECT_LAST_TAB:
    case KEYS_ACTION_FONT_SIZE: case KEYS_ACTION_EXIT: case KEYS_ACTION_SAVE:
    case KEYS_ACTION_NEW_FILE: case KEYS_ACTION_OPEN_FILE: return true;
    default: return false;
    }
}
/* edit-91c: Ctrl+End on a lazy large file whose line count is not exact.
 * view's VIEW_DOC_END finishes with an exact line query for EOF; with no
 * index anchor near EOF that is a UI-thread scan of the whole file, seeded
 * only when the index completes (large.c seed_navigation), so the frame at
 * the end waited for the index build (1.39 s (M) on the 1 GiB log corpus,
 * sanitizer build, docs/worker-reports/edit-91c-s6.md). Place the cursor as
 * the byte jump does (large.c editor_large_goto_byte) and let scrollctl
 * resolve the EOF viewport through resident windows with an estimated label;
 * the index relabels it on completion. Page motions keep view's exact query. */
static int large_doc_end(editor *e, bool extend)
{
    view_state *s = &e->v.state;
    uint64_t total = piece_len(e->tree);
    s->selection.cursor = total;
    if (!extend) s->selection.anchor = total;
    s->selection.preferred_col = VIEW_PREFERRED_UNSET;
    s->visual_end = false; s->hscroll = 0;
    if (e->selection_command) soft_record(e, false);
    e->action = EDITOR_ACTION_NONE;
    pointer_unit_finish(e);
    return editor_refresh_cursor(e, e->old_cursor);
}
int editor_handle_key(editor *e, const plat_event *ev)
{
    /* An open go-to-line prompt owns Escape and digits over the find panel. */
    if (!e->go.active && ev->press && ev->keysym==XKB_KEY_Escape && (e->find_visible || e->find_replace_active)) {
        (void)editor_find_key(e,ev); return 0;
    }
    if (e->go.active && (e->buffer->source_stale || e->go.owner != e->buffer->id ||
        e->go.generation != e->buffer->source_generation)) {
        e->go.active = false;
        if (e->action == EDITOR_ACTION_GOTO) e->action = EDITOR_ACTION_NONE;
    }
    if (e->buffer->source_stale && !editor_event_control(e, ev)) {
        if (!e->buffer->prompt_ns || e->buffer->prompt_generation != e->buffer->source_generation ||
            e->dispatch_serial <= e->buffer->prompt_serial || ev->mods || ev->repeat ||
            (ev->t0_ns && ev->t0_ns <= e->buffer->prompt_ns)) return 0;
        if (ev->press && (ev->keysym == 'r' || ev->keysym == 'R')) return editor_source_resolve(e, true);
        if (ev->press && (ev->keysym == 'k' || ev->keysym == 'K')) return editor_source_resolve(e, false);
        return 0;
    }
    if (!ev->press) {
        if (ev->keysym == XKB_KEY_Control_L || ev->keysym == XKB_KEY_Control_R) tabs_mru_release(&e->tabs);
        return 0;
    }
    const keys_binding *binding = NULL;
    keys_result result = e->go.active ? KEYS_NONE : config_keys_lookup(&e->preferences, &e->keys, ev->keysym, ev->mods, &binding);
    /* edit-o20: file/exit/scroll commands bypass find-field consumption;
     * edit-x8i: configured find actions dispatch from the same lookup. */
    bool common = binding && (binding->action == KEYS_ACTION_NEW_FILE || binding->action == KEYS_ACTION_OPEN_FILE ||
        binding->action == KEYS_ACTION_SAVE_AS || binding->action == KEYS_ACTION_EXIT || binding->action == KEYS_ACTION_SCROLL_LINES);
    if (!e->go.active && (editor_find_action(e, ev, binding) || (!common && editor_find_key(e, ev)))) return 0;
    if (ev->keysym == XKB_KEY_Control_L || ev->keysym == XKB_KEY_Control_R ||
        ev->keysym == XKB_KEY_Shift_L || ev->keysym == XKB_KEY_Shift_R ||
        ev->keysym == XKB_KEY_Alt_L || ev->keysym == XKB_KEY_Alt_R ||
        ev->keysym == XKB_KEY_Super_L || ev->keysym == XKB_KEY_Super_R ||
        ev->keysym == XKB_KEY_ISO_Level3_Shift) return 0;
    if (result == KEYS_PREFIX || result == KEYS_CANCELLED) { tabs_mru_release(&e->tabs); return 0; }
    if (result == KEYS_ERR_ARG) return EDITOR_ERR_ARG;
    keys_action action = binding ? binding->action : KEYS_ACTION_NONE;
    /* Redo has priority. With no redo entry, replay the previous successful
     * editing command at the current selection, preserving chord arguments.
     * Resolve before ingress so Ctrl+Y remains one input and one frame. */
    plat_event repeated;
    const plat_event *command = ev;
    bool repeating = action == KEYS_ACTION_REDO_OR_REPEAT &&
        e->buffer->history_cursor == e->buffer->history_count && e->buffer->last_key_valid;
    if (repeating) {
        repeated = e->buffer->last_key; repeated.t0_ns = ev->t0_ns; repeated.repeat = ev->repeat;
        command = &repeated; binding = e->buffer->last_binding;
        action = binding ? binding->action : KEYS_ACTION_NONE;
        result = binding ? KEYS_MATCH : KEYS_NONE;
        undo_break_burst(e->undo);
    }
    if (action == KEYS_ACTION_EXIT) {
        plat_event close_event = {.kind = PLAT_EV_CLOSE};
        return editor_inject(e, &close_event);
    }
    if (action == KEYS_ACTION_NEW_FILE) {
        e->file_command = action; e->file_command_path[0] = '\0';
        e->file_command_owner = e->buffer->id; e->file_command_generation = e->buffer->source_generation;
        undo_break_burst(e->undo);
        return editor_full_layout(e);
    }
    if (action == KEYS_ACTION_CANCEL) {
        tabs_mru_release(&e->tabs);
        if (e->v.state.selection.anchor == e->v.state.selection.cursor) return 0;
        e->old_selection = e->v.state.selection;
        e->v.state.selection.anchor = e->v.state.selection.cursor;
        undo_break_burst(e->undo); soft_record(e, false);
        return editor_full_layout(e);
    }
    if (action == KEYS_ACTION_SAVE) return editor_save_request(e, ev->t0_ns);
    if (action == KEYS_ACTION_FONT_SIZE) return editor_font_zoom(e, binding->args.value);
    view_key move = VIEW_LEFT; bool moving = move_key(action, &move);
    bool back = action == KEYS_ACTION_BACKSPACE || action == KEYS_ACTION_WORD_BACKSPACE;
    bool del = action == KEYS_ACTION_DELETE || action == KEYS_ACTION_WORD_DELETE;
    bool undo = action == KEYS_ACTION_UNDO;
    bool redo = action == KEYS_ACTION_REDO || action == KEYS_ACTION_REDO_OR_REPEAT;
    bool soft = action == KEYS_ACTION_SOFT_UNDO || action == KEYS_ACTION_SOFT_REDO;
    bool stack = action == KEYS_ACTION_NEXT_VIEW_STACK || action == KEYS_ACTION_PREVIOUS_VIEW_STACK;
    bool adjacent = (action == KEYS_ACTION_NEXT_VIEW || action == KEYS_ACTION_PREVIOUS_VIEW) &&
        !(binding && (binding->args.flags & KEYS_ARG_EXTEND));
    bool close = action == KEYS_ACTION_CLOSE || action == KEYS_ACTION_CLOSE_FILE;
    bool reopen = action == KEYS_ACTION_REOPEN_FILE;
    bool tab = stack || adjacent || close || reopen || action == KEYS_ACTION_SELECT_TAB || action == KEYS_ACTION_SELECT_LAST_TAB;
    bool text = action == KEYS_ACTION_INSERT || (result == KEYS_NONE && command->utf8_len &&
        !(command->mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER)) &&
        (!(command->mods & PLAT_MOD_ALT) || (command->mods & PLAT_MOD_ALTGR)));
    bool editing = default_edit_key(action);
    bool go = e->go.active || action == KEYS_ACTION_GOTO_LINE || action == KEYS_ACTION_OPEN_FILE || action == KEYS_ACTION_SAVE_AS;
    bool scroll = action == KEYS_ACTION_SCROLL_LINES;
    if (action == KEYS_ACTION_COPY || action == KEYS_ACTION_CUT ||
        action == KEYS_ACTION_PASTE) {
        e->command_key = *command; e->command_binding = binding;
        e->command_sequence = e->stats.input_sequence + 1u;
        return editor_clip_action(e, action, ev);
    }
    if (!moving && !back && !del && !undo && !redo && !soft && !tab && !text && !editing && !go && !scroll) {
        if (binding) e->stats.unimplemented_actions++;
        if (action == KEYS_ACTION_CANCEL || e->tabs.cycling) tabs_mru_release(&e->tabs);
        if (binding) {
            int rc = editor_begin_frame(e); if (rc) return rc;
            e->unimplemented_frame = e->grid.frame_id;
        }
        return 0;
    }
    if (!stack && e->tabs.cycling) tabs_mru_release(&e->tabs);
    if (!tabs_count(&e->tabs) && !reopen && action != KEYS_ACTION_OPEN_FILE && !e->go.active) return 0;
    e->command_key = *command; e->command_binding = binding;
    e->command_sequence = e->stats.input_sequence + 1u;
    e->old_selection = e->v.state.selection; e->old_cursor = e->old_selection.cursor;
    e->selection_command = moving;
    e->key_ns = trace_now_ns(); e->edit_time_ns = ev->t0_ns ? ev->t0_ns : e->key_ns;
    editor_restart_blink(e, e->key_ns);
    if (e->buffer->last_repeat && !ev->repeat) undo_break_burst(e->undo);
    e->buffer->last_repeat = ev->repeat;
    uint64_t seq = ++e->stats.input_sequence;
    if (e->cfg.on_ingress) e->cfg.on_ingress(e->cfg.hook_ctx, seq, e->key_ns);
    int rc = editor_begin_frame(e); if (rc) return rc;
    if (!e->frame.first_sequence) { e->frame.first_sequence = seq; e->frame.ingress_ns = e->key_ns; }
    e->frame.last_sequence = seq;
    trace_record_at(ev->t0_ns ? ev->t0_ns : e->key_ns, TRACE_T0_INGRESS, e->grid.frame_id);
    trace_record_at(e->key_ns, TRACE_T1_DEQUEUE, e->grid.frame_id);
    trace_input_key(ev->t0_ns ? ev->t0_ns : e->key_ns, TRACE_IN_KEY_DOWN, ev->keysym, ev->mods,
                    ev->repeat, (const char *)ev->utf8, ev->utf8_len);
    if (go) return goto_key(e, ev, action);
    if (scroll) {
        undo_break_burst(e->undo);
        /* edit-pd6: an unresolved byte jump keeps its target and the find
         * host's follow flags; the rows are queued for the landed top. */
        bool jumping = editor_scroll_jumping(e);
        if (!jumping) e->find_scroll = false;
        e->buffer->scroll_following = false;
        if (e->v.state.wrap) {
            e->wheel_pending = true; e->wheel.request = SCROLL_RELATIVE;
            e->wrap_wheel_rows -= binding->args.value;
            return editor_wheel_continue(e);
        }
        /* (P) amount's sign is Sublime's; a row is row_height Q8 pixels. */
        int64_t pixels = -(int64_t)binding->args.value * SCROLL_PIXEL_UNIT * e->buffer->scroll.config.row_height;
        if (jumping) editor_scroll_queue_pixels(e, pixels);
        else {
            rc = scroll_pixels(&e->buffer->scroll, pixels); if (rc) return rc;
            e->wheel_pending = false;
        }
        e->buffer->scroll_pending = true;
        return editor_scroll_slice(e);
    }
    if (tab) {
        undo_break_burst(e->undo);
        if (stack) return editor_cycle_tab(e, action == KEYS_ACTION_PREVIOUS_VIEW_STACK);
        if (close) return editor_close_tab(e, tabs_active_index(&e->tabs));
        if (reopen) return editor_reopen_tab(e);
        size_t count = tabs_count(&e->tabs), index = tabs_active_index(&e->tabs);
        if (adjacent) index = action == KEYS_ACTION_NEXT_VIEW ? (index + 1) % count : (index + count - 1) % count;
        else index = action == KEYS_ACTION_SELECT_LAST_TAB ? count - 1 : (size_t)binding->args.value;
        return index < count ? editor_select_tab(e, index) : 0;
    }
    if (undo || redo) return replay_history(e, redo);
    if (soft) return soft_history(e, action == KEYS_ACTION_SOFT_REDO);
    if (editing) return default_edit(e, binding);
    uint64_t lo = min64(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t old = max64(e->old_selection.cursor, e->old_selection.anchor) - lo;
    if (text) {
        uint8_t ch = binding ? (uint8_t)binding->args.value : 0;
        const uint8_t *p = binding ? &ch : command->utf8; size_t n = binding ? 1 : command->utf8_len;
        text_kind tk = ch == '\n' ? TEXT_ENTER : n == 1 && *p == '}' ? TEXT_BRACE : TEXT_BYTES;
        if (e->buffer->overwrite && !old && tk != TEXT_ENTER) {
            rc = view_overwrite_range(&e->v, p, n, &old);
            if (rc == PIECE_ERR_NOMEM) return default_edit_notice(e);
            if (rc) return rc;
        }
        return mutate_text(e, lo, old, p, n, UNDO_DELETE, tk);
    }
    if (moving) undo_break_burst(e->undo);
    if (back || del) {
        e->delete_kind = back ? UNDO_BACKSPACE : UNDO_DELETE;
        if (old) return mutate(e, lo, old, NULL, 0, e->delete_kind);
        move = back ? (action == KEYS_ACTION_WORD_BACKSPACE ? VIEW_WORD_LEFT : VIEW_LEFT) :
                      (action == KEYS_ACTION_WORD_DELETE ? VIEW_WORD_RIGHT : VIEW_RIGHT);
        e->action = EDITOR_ACTION_DELETE;
    } else e->action = EDITOR_ACTION_MOVE;
    if (moving && !e->v.state.wrap) {
        scroll_key scroll_key_value;
        bool scroll_motion = true;
        if (move == VIEW_PAGE_UP) scroll_key_value = SCROLL_PAGE_UP;
        else if (move == VIEW_PAGE_DOWN) scroll_key_value = SCROLL_PAGE_DOWN;
        else if (move == VIEW_DOC_HOME) scroll_key_value = SCROLL_HOME;
        else if (move == VIEW_DOC_END) scroll_key_value = SCROLL_END;
        else scroll_motion = false;
        if (scroll_motion) {
            /* edit-pd6: during an unresolved byte jump a page is queued
             * for the landed top; document Home/End supersede the jump. */
            bool page = scroll_key_value == SCROLL_PAGE_UP || scroll_key_value == SCROLL_PAGE_DOWN;
            if (page && editor_scroll_jumping(e)) editor_scroll_queue(e, scroll_key_value == SCROLL_PAGE_DOWN ? 1 : -1, 0);
            else {
                if (editor_scroll_jumping(e)) editor_scroll_cancel_jump(e);
                rc = scroll_key_motion(&e->buffer->scroll, scroll_key_value, &move); if (rc) return rc;
            }
            e->buffer->scroll_following = false; e->buffer->scroll_pending = true;
            if (move == VIEW_DOC_END && e->buffer->lg.lazy && !e->buffer->lg.lines_exact)
                return large_doc_end(e, binding && (binding->args.flags & KEYS_ARG_EXTEND));
        }
    }
    view_change c;
    editor_seed_view_lines(e);
    rc = view_command(&e->v, move, moving && binding && (binding->args.flags & KEYS_ARG_EXTEND), NULL, 0, &c);
    if (rc == VIEW_MORE) editor_scroll_mirror(e);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) return 0;
    if (rc) return rc;
    return editor_continue_action(e);
}
/* edit-73j: wrapped wheel input only. 3kk's scroll path owns unwrapped
 * wheel motion (editor_handle_wheel); while wrapped it maintains no state, so
 * displacement accumulates here and steps prepared visual rows. */
static int wrap_wheel(editor *e, const plat_event *ev)
{
    if (!ev->dy) return 0;
    if (e->wheel.request==SCROLL_BYTE) { e->find_scroll=false; e->wheel_pending=false; e->wheel_resolver=(scroll_resolver){0}; }
    if (!e->wheel_pending) {
        uint64_t sub=e->wheel.subrow_q8;
        int rc=scroll_init(&e->wheel,(scroll_config){e->text_grid.dims.rows,1,0},
            (scroll_extent){piece_len(e->tree),e->buffer->lines,e->buffer->lg.lines_exact});
        if (rc) return rc;
        e->wheel.first_line=e->v.state.first_line; e->wheel.first_byte=e->v.state.first_byte;
        e->wheel.subrow_q8=sub; e->wheel_resolver=(scroll_resolver){0};
    }
    /* Relative wheel accumulation has no logical-line clamp. The anchor
     * is an internal displacement accumulator; row boundaries come only
     * from layout_visual_row, including its EOF result. */
    e->wheel.extent.lines=e->wheel.extent.bytes+1u; e->wheel.extent.exact=false;
    e->wheel.first_line=e->wheel.extent.bytes/2u;
    uint64_t before=e->wheel.first_line;
    int rc=scroll_wheel(&e->wheel,ev->dy); if (rc) return rc;
    e->wrap_wheel_rows+=e->wheel.first_line>=before ? (int64_t)(e->wheel.first_line-before) : -(int64_t)(before-e->wheel.first_line);
    e->wheel_pending=e->wrap_wheel_rows!=0;
    return editor_wheel_continue(e);
}
int editor_handle_wheel(editor *e, const plat_event *ev)
{
    if (e->buffer->source_stale || e->buffer->pending_open || !tabs_count(&e->tabs)) return 0;
    /* Wrapped visual metadata is owned by the wrap integration, not the
     * logical line-index adapter. Never reinterpret logical rows as visual:
     * wrapped input steps prepared visual rows (edit-73j, wrap_wheel). */
    if (e->v.state.wrap) return wrap_wheel(e, ev);
    editor_buffer *b = e->buffer;
    /* edit-pd6: an unresolved byte jump keeps its target (and the find
     * host's horizontal follow); the notch is queued for the landed top. */
    bool jumping = editor_scroll_jumping(e);
    if (!jumping) { e->wheel_pending = false; e->find_scroll = false; }
    b->scroll_following = false;
    int rc = 0;
    if (jumping) editor_scroll_queue(e, 0, ev->dy);
    else rc = scroll_wheel(&b->scroll, ev->dy);
    if (rc) return rc;
    b->scroll_pending = true;
    undo_break_burst(e->undo);
    uint64_t now = trace_now_ns(), seq = ++e->stats.input_sequence;
    if (e->cfg.on_ingress) e->cfg.on_ingress(e->cfg.hook_ctx, seq, now);
    rc = editor_begin_frame(e); if (rc) return rc;
    if (!e->frame.first_sequence) { e->frame.first_sequence = seq; e->frame.ingress_ns = now; }
    e->frame.last_sequence = seq;
    trace_record_at(ev->t0_ns ? ev->t0_ns : now, TRACE_T0_INGRESS, e->grid.frame_id);
    trace_record_at(now, TRACE_T1_DEQUEUE, e->grid.frame_id);
    return editor_scroll_slice(e);
}

/* Source-free hit testing from the exact cluster/cell metadata layout drew. */
static bool pointer_byte(editor *e, uint32_t row, uint32_t cell, uint32_t pixel, uint64_t *out)
{
    /* Gutters identify logical lines, independently of text hit storage,
     * horizontal clipping and wrapped continuation columns. */
    if (cell<e->lay.gw) {
        uint64_t start=e->row_byte[row];
        if (start!=LAYOUT_VOID_ROW && e->v.state.wrap) start=e->lay.wrap_rows[row].line_start;
        *out=start==LAYOUT_VOID_ROW ? piece_len(e->tree) : start;
        return true;
    }
    const editor_hit *hit=&e->hits[(size_t)row*e->max_cols+cell];
    if (!hit->width) return false;
    uint64_t position=(uint64_t)hit->within*e->grid.dims.cell_w+pixel;
    *out=position*2u>=(uint64_t)hit->width*e->grid.dims.cell_w ? hit->after : hit->before;
    return true;
}
int editor_pointer(editor *e, const plat_event *ev)
{
    if (ev->kind==PLAT_EV_BUTTON && ev->code==1 && !ev->press) e->text_drag=false;
    int64_t row=ev->y<0 ? -1 : (int64_t)ev->y/e->grid.dims.cell_h-e->tab_rows;
    int64_t col=ev->x<0 ? -1 : (int64_t)ev->x/e->grid.dims.cell_w;
    if (!e->v.state.wrap && !e->panel_rows && row>=0) {
        /* edit-3kk: invert the frame's Q8 origin/overscan transform. */
        uint32_t ordinal; uint64_t within;
        int64_t local=((int64_t)ev->y-(int64_t)e->tab_rows*e->grid.dims.cell_h)*SCROLL_PIXEL_UNIT;
        if (scroll_frame_hit(&e->scroll_plan,local,&ordinal,&within)==SCROLL_OK) row=ordinal;
    }
    uint32_t text_rows=e->v.config.rows; /* visible rows: no overscan, no find panel */
    bool down=ev->kind==PLAT_EV_BUTTON && ev->code==1 && ev->press;
    if (e->text_drag && (e->text_drag_id!=e->buffer->id ||
        e->text_drag_generation!=e->buffer->source_generation)) e->text_drag=false;
    bool motion=ev->kind==PLAT_EV_MOTION && e->text_drag;
    if (e->buffer->pending_open || e->buffer->source_stale ||
        (!down && !motion) || (down && (row<0 || row>=text_rows ||
        col<0 || col>=e->text_grid.dims.cols))) return editor_chrome_pointer(e,ev);
    if (row<0) row=0;
    if (row>=text_rows) row=text_rows-1;
    if (col<0) col=0;
    if (col>=e->text_grid.dims.cols) col=e->text_grid.dims.cols-1;
    uint64_t byte;
    if (layout_busy(&e->lay) || !pointer_byte(e,(uint32_t)row,(uint32_t)col,ev->x<0 ? 0u : (uint32_t)ev->x%e->grid.dims.cell_w,&byte)) return 0;
    e->old_selection=e->v.state.selection; e->old_cursor=e->old_selection.cursor;
    undo_break_burst(e->undo); editor_restart_blink(e,trace_now_ns());
    e->v.state.selection.cursor=byte; e->v.state.visual_end=false;
    e->v.state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    if (down) {
        e->wheel_pending=false; e->find_scroll=false;
        e->text_drag=true; e->text_drag_id=e->buffer->id;
        e->text_drag_generation=e->buffer->source_generation;
        uint64_t now=ev->t0_ns ? ev->t0_ns : trace_now_ns();
        /* E: reuse the existing blink interval as the click-chain interval;
         * no delay is imposed on handling any click. Same cell and buffer only. */
        bool chain=col>=e->lay.gw && e->click_ns && now>=e->click_ns && now-e->click_ns<=EDITOR_BLINK_NS &&
            e->click_row==(uint32_t)row && e->click_col==(uint32_t)col;
        e->click_count=chain ? e->click_count%3u+1u : 1u;
        e->text_drag_mode=(ev->mods&PLAT_MOD_SHIFT) ? 1u : e->click_count;
        e->click_ns=now; e->click_row=(uint32_t)row; e->click_col=(uint32_t)col; e->click_byte=byte;
        if (!(ev->mods&PLAT_MOD_SHIFT)) e->v.state.selection.anchor=byte;
        if (e->click_count>1 && !(ev->mods&PLAT_MOD_SHIFT)) {
            e->pointer_unit_pending=true; e->pointer_unit_initial=true;
            editor_seed_view_lines(e); e->action=EDITOR_ACTION_MOVE;
            view_change change;
            int rc=view_command(&e->v,e->click_count==2 ? VIEW_SELECT_WORD : VIEW_SELECT_LINE,false,NULL,0,&change);
            if (rc==VIEW_MORE) return 0;
            if (rc) return rc;
            e->action=EDITOR_ACTION_NONE; pointer_unit_finish(e);
        }
    } else if (e->text_drag_mode>1) {
        e->v.state.selection.anchor=byte;
        e->pointer_unit_pending=true; e->pointer_unit_initial=false;
        e->action=EDITOR_ACTION_MOVE; editor_seed_view_lines(e);
        view_change change;
        int rc=view_command(&e->v,e->text_drag_mode==2 ? VIEW_SELECT_WORD : VIEW_SELECT_LINE,false,NULL,0,&change);
        if (rc==VIEW_MORE) return 0;
        if (rc) return rc;
        e->action=EDITOR_ACTION_NONE; pointer_unit_finish(e);
    }
    return editor_refresh_cursor(e,e->old_cursor);
}
static int find_horizontal_follow(editor *e)
{
    uint64_t start=e->v.state.first_byte, target=e->v.state.selection.anchor;
    /* An inexact-count seek may adopt an approximate top beyond the match.
     * There is no horizontal column to derive from that row; keep hscroll
     * and let subsequent navigation repair the vertical position. */
    if (target<start) return 0;
    uint64_t offset=target-start, available=piece_len(e->tree)-start;
    size_t n=(size_t)(available<LAYOUT_BYTE_BUDGET ? available : LAYOUT_BYTE_BUDGET);
    uint64_t column=offset;
    if (offset<=n) {
        if (piece_read(e->tree,start,e->indent_bytes,n)) return LAYOUT_ERR_SOURCE;
        size_t at=0; column=0;
        while (at<offset) {
            size_t step=utf8_grapheme_next(e->indent_bytes+at,n-at);
            if (!step || step>offset-at) break;
            column+=e->indent_bytes[at]=='\t' ? e->lay.tab-column%e->lay.tab :
                (uint64_t)utf8_cluster_width(e->indent_bytes+at,step);
            at+=step;
        }
    } else e->v.state.approximate=true;
    uint64_t left=column>e->lay.text_cols ? column-e->lay.text_cols/2u : 0;
    e->v.state.hscroll=left<LAYOUT_MAX_HSCROLL ? left : LAYOUT_MAX_HSCROLL;
    return 0;
}
bool editor_wheel_ready(const editor *e)
{
    if (!e->wheel_pending) return false;
    const editor_buffer *b=e->buffer;
    /* Waiting on 3kk's scroll slice or on its resident window is not work. */
    if (e->find_scroll && !e->v.state.wrap) return !b->scroll_pending;
    if (e->v.state.wrap && e->wheel.request==SCROLL_BYTE) return !b->scroll_resident.pending;
    return true;
}
int editor_wheel_continue(editor *e)
{
    if (!e->wheel_pending) return 0;
    editor_buffer *b=e->buffer;
    if (b->source_stale || b->pending_open || !tabs_count(&e->tabs)) {
        e->wheel_pending=false; e->find_scroll=false; e->wrap_wheel_rows=0; return 0;
    }
    if (e->find_scroll && !e->v.state.wrap) {
        /* The seek went through editor_scroll_seek; once 3kk adopted the
         * target line, follow horizontally (hscroll is not scroll state). */
        if (b->scroll_pending) return 0;
        e->wheel_pending=false; e->find_scroll=false;
        int rc=find_horizontal_follow(e); if (rc) return rc;
        return editor_full_layout(e);
    }
    if (e->v.state.wrap && e->wheel.request!=SCROLL_BYTE) {
        layout_wrap_row next; bool approximate;
        int direction=e->wrap_wheel_rows<0 ? -1 : 1;
        int rc=layout_visual_row(&e->lay,e->tree,e->v.state.visual_byte,direction,&next,&approximate);
        if (rc) { e->wheel_pending=false; e->wrap_wheel_rows=0; return rc; }
        if (next.start==e->v.state.visual_byte) e->wrap_wheel_rows=0;
        else {
            e->v.state.visual_byte=next.start; e->v.state.first_line=next.line;
            e->v.state.first_byte=next.line_start; e->v.state.approximate=approximate;
            e->wrap_wheel_rows-=direction;
        }
        e->wheel_pending=e->wrap_wheel_rows!=0;
        return editor_full_layout(e);
    }
    if (!e->v.state.wrap) { e->wheel_pending=false; e->find_scroll=false; return 0; }
    /* Wrapped byte seek (far find result, pending minimap target): resolve
     * the logical line through the buffer's one resident window, then
     * anchor the visual row. */
    int rc=editor_scroll_resolve(e,&e->wheel,&e->wheel_resolver);
    if (rc==SCROLL_MORE) return 0;
    e->wheel_pending=false;
    if (rc) { e->find_scroll=false; return rc; }
    e->v.state.first_line=e->wheel.first_line;
    e->v.state.first_byte=e->v.state.visual_byte=e->wheel.first_byte;
    b->lg.top_estimated=e->wheel.approximate;
    e->v.state.approximate=e->wheel.approximate;
    if (e->find_scroll) {
        bool approximate;
        rc=layout_visual_row_anchored(&e->lay,e->tree,e->v.state.selection.anchor,
            e->wheel.first_byte,e->wheel.first_line,&e->visual_seed,&approximate);
        if (rc) { e->find_scroll=false; return rc; }
        e->visual_seed_valid=true; e->v.state.visual_byte=e->visual_seed.start;
        e->v.state.approximate|=approximate;
    }
    e->find_scroll=false;
    return editor_full_layout(e);
}

/* findui owns the explicit undo group; these callbacks own every successful
 * mutation's editor index/layout/history/journal accounting. */
findui_mutation_result editor_find_delete(void *ctx, undo_log *undo, uint64_t off, uint64_t len,
    uint64_t when, const undo_state *before, const undo_state *after)
{
    editor *e=ctx; uint64_t nl=newlines(e,off,len);
    int rc=undo_delete(undo,off,len,UNDO_DELETE,when,before,after);
    if (rc) return (findui_mutation_result){rc,false};
    if (off<e->edit_delta.off) e->edit_delta.off=off;
    if (off+len>e->find_replace_end) e->find_replace_end=off+len;
    e->edit_delta.old+=len;
    rc=stage_delete(e,off,len); if (!rc) rc=changed(e,off,len,0,nl,0);
    return (findui_mutation_result){rc,true};
}
findui_mutation_result editor_find_insert(void *ctx, undo_log *undo, uint64_t off, const uint8_t *bytes,
    size_t len, uint64_t when, const undo_state *before, const undo_state *after)
{
    editor *e=ctx; uint64_t nl=0;
    for (size_t i=0;i<len;i++) nl+=bytes[i]=='\n' ? 1u : 0u;
    int rc=undo_insert(undo,off,bytes,len,when,before,after);
    if (rc) return (findui_mutation_result){rc,false};
    if (off<e->edit_delta.off) e->edit_delta.off=off;
    if (off>e->find_replace_end) e->find_replace_end=off;
    e->edit_delta.add+=len;
    rc=stage_insert(e,off,bytes,len); if (!rc) rc=changed(e,off,0,len,0,nl);
    return (findui_mutation_result){rc,true};
}
void editor_find_replace_finish(editor *e)
{
    if (e->v.state.selection.cursor>piece_len(e->tree)) e->v.state.selection.cursor=piece_len(e->tree);
    if (e->v.state.selection.anchor>piece_len(e->tree)) e->v.state.selection.anchor=piece_len(e->tree);
    if (e->edit_delta.old || e->edit_delta.add) {
        /* Replay represents the coalesced interval between the leftmost and
         * rightmost mutation, including unchanged gaps between replacements.
         * A single replacement must never journal/scan the untouched suffix. */
        uint64_t deleted=e->edit_delta.old, inserted=e->edit_delta.add;
        e->edit_delta.old=e->find_replace_end-e->edit_delta.off;
        e->edit_delta.add=e->edit_delta.old-deleted+inserted;
        e->edit_delta.after=save_selection(&e->v);
        remember(e,e->edit_delta); editor_modified(e);
    }
    e->find_replace_active=false; e->find_id=0;
    e->full_pending=true; e->paint_ready=false;
}
