#include "editor/private.h"
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
    e->repair_line_valid = false;
    e->paint_ready = false; e->buffer->revision++;
    e->stats.mutations++; e->buffer->lines = e->buffer->lines - old_nl + new_nl;
    if (e->buffer->index) {
        /* A mutation invalidates outstanding results. Adoption and source
         * retirement belong to the maintenance turn, before input checks. */
        if (lineidx_edit(e->buffer->index, off, old, add)) return EDITOR_ERR_HISTORY;
        e->buffer->index_dirty = true;
    }
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
    if (editor_save_leased(e)) return;
    size_t consumed = 0;
    while (consumed < e->op_count) {
        editor_jop *op = &e->ops[consumed];
        if (e->stats.journal_error) break;
        uint64_t before = journal_get_stats(e->journal).accepted_sequence;
        int rc;
        if (!op->insert) rc = journal_delete_protected(e->journal, op->id, op->off, op->len);
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
            consumed++; e->stats.journal_records++;
        }
        if (rc == JOURNAL_BUSY) break;
        if (rc) { e->stats.journal_error = rc; break; }
    }
    e->op_count -= consumed;
    if (e->op_count) memmove(e->ops, e->ops + consumed, e->op_count * sizeof *e->ops);
    else e->stage_used = 0;
}
static editor_delta *delta_at(editor *e, size_t i) { return &e->buffer->history[(e->buffer->history_head + i) % e->buffer->history_cap]; }
static void remember(editor *e, editor_delta d)
{
    editor_buffer *b = e->buffer;
    undo_history h = undo_get_history(e->undo);
    if (b->save_tracking && b->history_cursor < b->save_cursor) b->save_tracking = false;
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
            (bottom->start != LAYOUT_VOID_ROW && cursor.start > bottom->start))
            e->v.state.visual_byte = cursor.start;
        e->v.state.first_line = piece_byte_to_line(e->tree, e->v.state.visual_byte);
        e->v.state.first_byte = piece_line_to_byte(e->tree, e->v.state.first_line);
        e->v.state.approximate |= approximate;
    }
    undo_state after = save_selection(&e->v);
    int rc = end_edit_group(e, &after); if (rc) return rc;
    remember(e, e->edit_delta); editor_modified(e); e->action = EDITOR_ACTION_NONE;
    trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
    return editor_refresh_cursor(e, e->old_cursor);
}
/* Empty TYPE is the view's boundary repair/follow; its zero-length piece
 * insert is a no-op. Every actual text mutation goes through undo. */
static int repair(editor *e, uint64_t target)
{
    e->v.state.selection.cursor = e->v.state.selection.anchor = target;
    view_change c; int rc = view_command(&e->v, VIEW_TYPE, false, NULL, 0, &c);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) { editor_seed_view_lines(e); e->action = EDITOR_ACTION_REPAIR; return 0; }
    if (rc) return rc;
    return finish_edit(e);
}
static int finish_replay_follow(editor *e)
{
    e->v.state.selection.anchor = e->replay_selection.anchor;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->action = EDITOR_ACTION_NONE;
    return editor_refresh_cursor(e, e->old_cursor);
}
static int replay_follow(editor *e)
{
    e->replay_selection = e->v.state.selection;
    e->v.state.selection.anchor = e->v.state.selection.cursor;
    view_change c; int rc = view_command(&e->v, VIEW_TYPE, false, NULL, 0, &c);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) { editor_seed_view_lines(e); e->action = EDITOR_ACTION_REPLAY; return 0; }
    if (rc) return rc;
    return finish_replay_follow(e);
}
typedef enum text_kind { TEXT_BYTES, TEXT_ENTER, TEXT_BRACE } text_kind;
static int prefix_error(editor *e, int cause)
{
    undo_state after = save_selection(&e->v);
    (void)end_edit_group(e, &after);
    if (e->edit_delta.old || e->edit_delta.add) { remember(e, e->edit_delta); editor_modified(e); }
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
        editor_modified(e); trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
        er = replay_follow(e); if (er) return er;
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
int editor_continue_action(editor *e)
{
    if (e->action == EDITOR_ACTION_CLIP) return editor_clip_continue(e);
    if (e->action == EDITOR_ACTION_REPLAY_WORK) return replay_slice(e);
    view_change c; int rc = view_continue(&e->v, &c);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) return 0;
    if (rc) return rc;
    if (e->action == EDITOR_ACTION_DELETE) {
        uint64_t a = e->old_selection.cursor, b = e->v.state.selection.cursor;
        return mutate(e, min64(a, b), max64(a, b) - min64(a, b), NULL, 0, e->delete_kind);
    }
    if (e->action == EDITOR_ACTION_REPAIR) return finish_edit(e);
    if (e->action == EDITOR_ACTION_REPLAY) return finish_replay_follow(e);
    e->action = EDITOR_ACTION_NONE;
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
    if (keys_lookup(&state, ev->keysym, ev->mods, &binding) != KEYS_MATCH || !binding) return false;
    switch (binding->action) {
    case KEYS_ACTION_NEXT_VIEW_STACK: case KEYS_ACTION_PREVIOUS_VIEW_STACK:
    case KEYS_ACTION_NEXT_VIEW: case KEYS_ACTION_PREVIOUS_VIEW:
    case KEYS_ACTION_CLOSE: case KEYS_ACTION_CLOSE_FILE: case KEYS_ACTION_REOPEN_FILE:
    case KEYS_ACTION_SELECT_TAB: case KEYS_ACTION_SELECT_LAST_TAB: return true;
    default: return false;
    }
}
int editor_handle_key(editor *e, const plat_event *ev)
{
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
    keys_result result = keys_lookup(&e->keys, ev->keysym, ev->mods, &binding);
    if (ev->keysym == XKB_KEY_Control_L || ev->keysym == XKB_KEY_Control_R ||
        ev->keysym == XKB_KEY_Shift_L || ev->keysym == XKB_KEY_Shift_R ||
        ev->keysym == XKB_KEY_Alt_L || ev->keysym == XKB_KEY_Alt_R ||
        ev->keysym == XKB_KEY_Super_L || ev->keysym == XKB_KEY_Super_R ||
        ev->keysym == XKB_KEY_ISO_Level3_Shift) return 0;
    if (result == KEYS_PREFIX || result == KEYS_CANCELLED) { tabs_mru_release(&e->tabs); return 0; }
    if (result == KEYS_ERR_ARG) return EDITOR_ERR_ARG;
    keys_action action = binding ? binding->action : KEYS_ACTION_NONE;
    if (action == KEYS_ACTION_SAVE) return editor_save_request(e, ev->t0_ns);
    view_key move = VIEW_LEFT; bool moving = move_key(action, &move);
    bool back = action == KEYS_ACTION_BACKSPACE || action == KEYS_ACTION_WORD_BACKSPACE;
    bool del = action == KEYS_ACTION_DELETE || action == KEYS_ACTION_WORD_DELETE;
    /* Repeat needs a command record; the existing history only records deltas.
     * With no redo entry replay_history deliberately leaves the buffer alone. */
    bool undo = action == KEYS_ACTION_UNDO;
    bool redo = action == KEYS_ACTION_REDO || action == KEYS_ACTION_REDO_OR_REPEAT;
    bool stack = action == KEYS_ACTION_NEXT_VIEW_STACK || action == KEYS_ACTION_PREVIOUS_VIEW_STACK;
    bool adjacent = action == KEYS_ACTION_NEXT_VIEW || action == KEYS_ACTION_PREVIOUS_VIEW;
    bool close = action == KEYS_ACTION_CLOSE || action == KEYS_ACTION_CLOSE_FILE;
    bool reopen = action == KEYS_ACTION_REOPEN_FILE;
    bool tab = stack || adjacent || close || reopen || action == KEYS_ACTION_SELECT_TAB || action == KEYS_ACTION_SELECT_LAST_TAB;
    bool text = action == KEYS_ACTION_INSERT || (result == KEYS_NONE && ev->utf8_len &&
        !(ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER)) &&
        (!(ev->mods & PLAT_MOD_ALT) || (ev->mods & PLAT_MOD_ALTGR)));
    if (action == KEYS_ACTION_COPY || action == KEYS_ACTION_CUT ||
        action == KEYS_ACTION_PASTE)
        return editor_clip_action(e, action, ev);
    if (!moving && !back && !del && !undo && !redo && !tab && !text) {
        if (binding) e->stats.unimplemented_actions++;
        if (action == KEYS_ACTION_CANCEL || e->tabs.cycling) tabs_mru_release(&e->tabs);
        switch (action) {
        case KEYS_ACTION_FIND: case KEYS_ACTION_REPLACE: case KEYS_ACTION_FIND_NEXT:
        case KEYS_ACTION_FIND_IN_FILES: case KEYS_ACTION_GOTO_FILE: case KEYS_ACTION_GOTO_LINE: {
            int rc = editor_begin_frame(e); if (rc) return rc;
            e->unimplemented_frame = e->grid.frame_id;
            break;
        }
        default: break;
        }
        return 0;
    }
    if (!stack && e->tabs.cycling) tabs_mru_release(&e->tabs);
    if (!tabs_count(&e->tabs) && !reopen) return 0;
    e->old_selection = e->v.state.selection; e->old_cursor = e->old_selection.cursor;
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
    uint64_t lo = min64(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t old = max64(e->old_selection.cursor, e->old_selection.anchor) - lo;
    if (text) {
        uint8_t ch = binding ? (uint8_t)binding->args.value : 0;
        const uint8_t *p = binding ? &ch : ev->utf8; size_t n = binding ? 1 : ev->utf8_len;
        text_kind tk = ch == '\n' ? TEXT_ENTER : n == 1 && *p == '}' ? TEXT_BRACE : TEXT_BYTES;
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
    view_change c;
    editor_seed_view_lines(e);
    rc = view_command(&e->v, move, moving && binding && (binding->args.flags & KEYS_ARG_EXTEND), NULL, 0, &c);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) return 0;
    if (rc) return rc;
    return editor_continue_action(e);
}
