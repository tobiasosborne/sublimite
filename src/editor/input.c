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
    e->ops[e->op_count++] = (editor_jop){off, len, 0, false, e->buffer->id}; return 0;
}
static int stage_insert(editor *e, uint64_t off, const uint8_t *p, size_t n)
{
    if (!n || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
    e->ops[e->op_count++] = (editor_jop){off, n, e->stage_used, true, e->buffer->id};
    memcpy(e->stage + e->stage_used, p, n); e->stage_used += n; return 0;
}
static int stage_tree(editor *e, uint64_t off, uint64_t n)
{
    if (!n || !e->journal) return 0;
    while (n) {
        if (e->op_count == EDITOR_STAGE_OPS || e->stage_used == EDITOR_STAGE_BYTES) {
            editor_journal_staged(e);
            if (e->stats.journal_error) return EDITOR_ERR_IO;
        }
        size_t room = EDITOR_STAGE_BYTES - e->stage_used;
        size_t count = n < room ? (size_t)n : room;
        int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
        e->ops[e->op_count++] = (editor_jop){off, count, e->stage_used, true, e->buffer->id};
        e->stage_used += count; off += count; n -= count;
    }
    return 0;
}
void editor_journal_staged(editor *e)
{
    size_t consumed = 0;
    while (consumed < e->op_count) {
        editor_jop op = e->ops[consumed];
        if (e->stats.journal_error) break;
        uint64_t before = journal_get_stats(e->journal).accepted_sequence;
        int rc = op.insert ? journal_insert(e->journal, op.id, op.off, e->stage + op.at, (size_t)op.len) :
                            journal_delete(e->journal, op.id, op.off, op.len);
        /* IO can accept a whole logical call into journal-owned retry storage.
         * Do not duplicate it; FULL/BUSY accept nothing and stay in this queue. */
        if (journal_get_stats(e->journal).accepted_sequence != before) {
            consumed++; e->stats.journal_records++;
        }
        if (rc) { e->stats.journal_error = rc; break; }
    }
    e->op_count -= consumed;
    if (e->op_count) memmove(e->ops, e->ops + consumed, e->op_count * sizeof *e->ops);
    else e->stage_used = 0;
}
static editor_delta *delta_at(editor *e, size_t i) { return &e->buffer->history[(e->buffer->history_head + i) % e->buffer->history_cap]; }
static void remember(editor *e, editor_delta d)
{
    e->buffer->history_count = e->buffer->history_cursor;
    if (e->buffer->history_count == e->buffer->history_cap) {
        e->buffer->history_head = (e->buffer->history_head + 1) % e->buffer->history_cap; e->buffer->history_count--; e->buffer->history_cursor--;
        if (e->buffer->saved_cursor) e->buffer->saved_cursor--; else e->buffer->saved_lost = true;
    }
    *delta_at(e, e->buffer->history_count++) = d; e->buffer->history_cursor = e->buffer->history_count;
    size_t groups = undo_get_stats(e->undo).undo_groups;
    while (e->buffer->history_count > groups) {
        e->buffer->history_head = (e->buffer->history_head + 1) % e->buffer->history_cap; e->buffer->history_count--; e->buffer->history_cursor--;
        if (e->buffer->saved_cursor) e->buffer->saved_cursor--; else e->buffer->saved_lost = true;
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
            (bottom->start != LAYOUT_VOID_ROW && cursor.start > bottom->start))
            e->v.state.visual_byte = cursor.start;
        e->v.state.first_line = piece_byte_to_line(e->tree, e->v.state.visual_byte);
        e->v.state.first_byte = piece_line_to_byte(e->tree, e->v.state.first_line);
        e->v.state.approximate |= approximate;
    }
    undo_state after = save_selection(&e->v);
    int rc = undo_group_end(e->undo, &after); if (rc) return rc;
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
    (void)undo_group_end(e->undo, &after);
    if (e->edit_delta.old || e->edit_delta.add) { remember(e, e->edit_delta); editor_modified(e); }
    e->action = EDITOR_ACTION_NONE; return cause;
}
static int reject_text(editor *e, const undo_state *before)
{
    e->stats.rejected_commands++;
    if (e->edit_delta.old || e->edit_delta.add) return repair(e, e->edit_delta.off);
    int rc = undo_group_end(e->undo, before);
    return rc ? rc : editor_refresh_cursor(e, e->old_cursor);
}
static int delete_part(editor *e, uint64_t lo, uint64_t old, undo_kind kind,
                       const undo_state *before)
{
    if (!old) return 0;
    uint64_t nl = newlines(e, lo, old);
    undo_state after = *before; memcpy(after.bytes, &lo, 8); memcpy(after.bytes + 8, &lo, 8);
    int rc = undo_delete(e->undo, lo, old, kind, e->key_ns, before, &after); if (rc) return rc;
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
    e->v.state.selection = e->old_selection;
    undo_state before = save_selection(&e->v), after = before;
    int rc = undo_group_begin(e->undo, &before); if (rc) return rc;
    e->edit_delta = (editor_delta){lo, 0, 0};
    rc = delete_part(e, lo, old, kind, &before); if (rc) return prefix_error(e, rc);
    if (tk == TEXT_ENTER) {
        indent_code ir = indent_on_enter(e->tree, lo, e->indent_bytes, EDITOR_STAGE_BYTES, &n);
        if (ir != INDENT_OK || (e->journal && n > EDITOR_STAGE_BYTES - e->stage_used)) {
            return reject_text(e, &before);
        }
        text = e->indent_bytes;
    } else if (tk == TEXT_BRACE) {
        indent_edit edit; indent_code ir = indent_on_close_brace(e->tree, lo, e->buffer->style, &edit);
        if (ir != INDENT_OK) return reject_text(e, &before);
        rc = delete_part(e, edit.lo, edit.hi - edit.lo, UNDO_DELETE, &before); if (rc) return prefix_error(e, rc);
        lo = edit.lo; e->indent_bytes[0] = edit.bytes[0]; text = e->indent_bytes; n = edit.length;
    }
    if (n) {
        uint64_t target = lo + n, nl = 0;
        for (size_t i = 0; i < n; i++) nl += text[i] == '\n' ? 1u : 0u;
        memcpy(after.bytes, &target, 8); memcpy(after.bytes + 8, &target, 8);
        rc = undo_insert(e->undo, lo, text, n, e->key_ns, &before, &after);
        if (rc) return prefix_error(e, rc);
        e->edit_delta.add = n; e->v.state.selection.cursor = e->v.state.selection.anchor = target;
        rc = stage_insert(e, lo, text, n); if (!rc) rc = changed(e, lo, 0, n, 0, nl);
        if (rc) return prefix_error(e, rc);
    }
    return repair(e, lo + n);
}
static int mutate(editor *e, uint64_t lo, uint64_t old, const uint8_t *text, size_t n, undo_kind kind)
{ return mutate_text(e, lo, old, text, n, kind, TEXT_BYTES); }
static int replay_history(editor *e, bool redo)
{
    if ((!redo && !e->buffer->history_cursor) || (redo && e->buffer->history_cursor == e->buffer->history_count)) return editor_refresh_cursor(e, e->old_cursor);
    editor_delta d = *delta_at(e, redo ? e->buffer->history_cursor : e->buffer->history_cursor - 1);
    uint64_t old = redo ? d.old : d.add, add = redo ? d.add : d.old;
    if (e->journal && e->op_count + 2 > EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
    uint64_t old_nl = newlines(e, d.off, old), before_len = piece_len(e->tree);
    undo_change c; int rc = redo ? undo_redo(e->undo, 1, &c) : undo_undo(e->undo, 1, &c);
    uint64_t after_len = piece_len(e->tree);
    if (c.records) {
        /* The dirty suffix in undo_change is not a byte delta. Successful
         * replay prefixes affect only this key's replacement interval. */
        add = after_len >= before_len ? old + after_len - before_len : old - (before_len - after_len);
        int jr = stage_delete(e, d.off, old); if (!jr) jr = stage_tree(e, d.off, add);
        if (jr) return jr;
        int er = changed(e, d.off, old, add, old_nl, newlines(e, d.off, add)); if (er) return er;
        if (c.has_state) restore_selection(&e->v, &c.state);
        else e->v.state.selection.cursor = e->v.state.selection.anchor = d.off;
        if (c.groups) { if (redo) e->buffer->history_cursor++; else e->buffer->history_cursor--; }
        editor_modified(e);
        trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
        er = replay_follow(e); if (er) return er;
    }
    return rc;
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
int editor_handle_key(editor *e, const plat_event *ev)
{
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
    view_key move = VIEW_LEFT; bool moving = move_key(action, &move);
    bool back = action == KEYS_ACTION_BACKSPACE || action == KEYS_ACTION_WORD_BACKSPACE;
    bool del = action == KEYS_ACTION_DELETE || action == KEYS_ACTION_WORD_DELETE;
    bool undo = action == KEYS_ACTION_UNDO, redo = action == KEYS_ACTION_REDO;
    bool stack = action == KEYS_ACTION_NEXT_VIEW_STACK || action == KEYS_ACTION_PREVIOUS_VIEW_STACK;
    bool adjacent = action == KEYS_ACTION_NEXT_VIEW || action == KEYS_ACTION_PREVIOUS_VIEW;
    bool close = action == KEYS_ACTION_CLOSE || action == KEYS_ACTION_CLOSE_FILE;
    bool reopen = action == KEYS_ACTION_REOPEN_FILE;
    bool tab = stack || adjacent || close || reopen || action == KEYS_ACTION_SELECT_TAB || action == KEYS_ACTION_SELECT_LAST_TAB;
    bool text = action == KEYS_ACTION_INSERT || (result == KEYS_NONE && ev->utf8_len &&
        !(ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER)) &&
        (!(ev->mods & PLAT_MOD_ALT) || (ev->mods & PLAT_MOD_ALTGR)));
    if (!moving && !back && !del && !undo && !redo && !tab && !text) {
        if (binding) e->stats.unimplemented_actions++;
        if (action == KEYS_ACTION_CANCEL || e->tabs.cycling) tabs_mru_release(&e->tabs);
        return 0;
    }
    if (!stack && e->tabs.cycling) tabs_mru_release(&e->tabs);
    if (!tabs_count(&e->tabs) && !reopen) return 0;
    e->old_selection = e->v.state.selection; e->old_cursor = e->old_selection.cursor;
    e->key_ns = trace_now_ns(); editor_restart_blink(e, e->key_ns);
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
    undo_break_burst(e->undo);
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
