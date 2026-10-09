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
static void slice_end(editor *e, uint64_t start)
{
    uint64_t elapsed = trace_now_ns() - start; e->stats.slices++;
    if (elapsed > e->stats.longest_slice_ns) e->stats.longest_slice_ns = elapsed;
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
static int changed(editor *e, uint64_t off, uint64_t old, uint64_t add, uint64_t old_nl, uint64_t new_nl)
{
    e->stats.mutations++; e->lines = e->lines - old_nl + new_nl;
    if (e->index) {
        (void)lineidx_poll(e->index);
        if (lineidx_edit(e->index, off, old, add)) return EDITOR_ERR_HISTORY;
        e->index_dirty = true;
    }
    int rc = editor_begin_frame(e); if (rc) return rc;
    if (!e->lay.src || e->full_pending) { e->full_pending = true; return 0; }
    rc = layout_edit(&e->lay, off, old, add, old_nl, new_nl);
    if (rc == LAYOUT_RESET) { e->full_pending = true; return 0; }
    return rc < 0 ? rc : 0;
}
static int stage_delete(editor *e, uint64_t off, uint64_t len)
{
    if (!len || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
    e->ops[e->op_count++] = (editor_jop){off, len, 0, false}; return 0;
}
static int stage_insert(editor *e, uint64_t off, const uint8_t *p, size_t n)
{
    if (!n || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
    e->ops[e->op_count++] = (editor_jop){off, n, e->stage_used, true};
    memcpy(e->stage + e->stage_used, p, n); e->stage_used += n; return 0;
}
static int stage_tree(editor *e, uint64_t off, uint64_t n)
{
    if (!n || !e->journal) return 0;
    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
    size_t count = (size_t)n;
    int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
    e->ops[e->op_count++] = (editor_jop){off, n, e->stage_used, true}; e->stage_used += count; return 0;
}
void editor_journal_staged(editor *e)
{
    for (size_t i = 0; i < e->op_count; i++) {
        editor_jop op = e->ops[i];
        if (e->stats.journal_error) break;
        int rc = op.insert ? journal_insert(e->journal, 1, op.off, e->stage + op.at, (size_t)op.len) :
                            journal_delete(e->journal, 1, op.off, op.len);
        if (rc) e->stats.journal_error = rc; else e->stats.journal_records++;
    }
    e->op_count = e->stage_used = 0;
}
static editor_delta *delta_at(editor *e, size_t i) { return &e->history[(e->history_head + i) % e->history_cap]; }
static void remember(editor *e, editor_delta d)
{
    e->history_count = e->history_cursor;
    if (e->history_count == e->history_cap) {
        e->history_head = (e->history_head + 1) % e->history_cap; e->history_count--; e->history_cursor--;
    }
    *delta_at(e, e->history_count++) = d; e->history_cursor = e->history_count;
    size_t groups = undo_get_stats(&e->undo).undo_groups;
    while (e->history_count > groups) {
        e->history_head = (e->history_head + 1) % e->history_cap; e->history_count--; e->history_cursor--;
    }
}
static int finish_edit(editor *e)
{
    undo_state after = save_selection(&e->v);
    int rc = undo_group_end(&e->undo, &after); if (rc) return rc;
    remember(e, e->edit_delta); e->action = EDITOR_ACTION_NONE;
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
    if (rc == VIEW_MORE) { e->action = EDITOR_ACTION_REPAIR; return 0; }
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
    if (rc == VIEW_MORE) { e->action = EDITOR_ACTION_REPLAY; return 0; }
    if (rc) return rc;
    return finish_replay_follow(e);
}
static int mutate(editor *e, uint64_t lo, uint64_t old, const uint8_t *text, size_t n, undo_kind kind)
{
    if (!old && !n) { e->v.state.selection = e->old_selection; e->action = EDITOR_ACTION_NONE; return editor_refresh_cursor(e, e->old_cursor); }
    if (e->journal && (e->op_count + 2 > EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used)) return EDITOR_ERR_MEMORY;
    uint64_t old_nl = newlines(e, lo, old), new_nl = 0;
    for (size_t i = 0; i < n; i++) new_nl += text[i] == '\n' ? 1u : 0u;
    e->v.state.selection = e->old_selection;
    undo_state before = save_selection(&e->v), after = before;
    int rc = undo_group_begin(&e->undo, &before); if (rc) return rc;
    e->edit_delta = (editor_delta){lo, 0, 0};
    if (n) {
        uint64_t target = lo + n; memcpy(after.bytes, &target, 8); memcpy(after.bytes + 8, &target, 8);
        rc = undo_insert(&e->undo, lo, text, n, e->key_ns, &before, &after);
        if (rc) { (void)undo_group_end(&e->undo, &before); return rc; }
        e->edit_delta.add = n;
        rc = stage_insert(e, lo, text, n); if (rc) return rc;
        rc = changed(e, lo, 0, n, 0, new_nl); if (rc) return rc;
    }
    if (old) {
        uint64_t target = lo + n; memcpy(after.bytes, &target, 8); memcpy(after.bytes + 8, &target, 8);
        rc = undo_delete(&e->undo, lo + n, old, kind, e->key_ns, &before, &after);
        if (rc) {
            /* Accept and journal a successful insertion prefix. No fallible
             * rollback; the failed piece operation itself changed nothing. */
            e->v.state.selection.cursor = e->v.state.selection.anchor = lo + n;
            if (n) (void)finish_edit(e); else (void)undo_group_end(&e->undo, &before);
            return rc;
        }
        e->edit_delta.old = old;
        rc = stage_delete(e, lo + n, old); if (rc) return rc;
        rc = changed(e, lo + n, old, 0, old_nl, 0); if (rc) return rc;
    }
    return repair(e, lo + n);
}
static int replay_history(editor *e, bool redo)
{
    if ((!redo && !e->history_cursor) || (redo && e->history_cursor == e->history_count)) return editor_refresh_cursor(e, e->old_cursor);
    editor_delta d = *delta_at(e, redo ? e->history_cursor : e->history_cursor - 1);
    uint64_t old = redo ? d.old : d.add, add = redo ? d.add : d.old;
    if (e->journal && (e->op_count + 2 > EDITOR_STAGE_OPS || add > EDITOR_STAGE_BYTES - e->stage_used)) return EDITOR_ERR_MEMORY;
    uint64_t old_nl = newlines(e, d.off, old), before_len = piece_len(e->tree);
    undo_change c; int rc = redo ? undo_redo(&e->undo, 1, &c) : undo_undo(&e->undo, 1, &c);
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
        if (c.groups) { if (redo) e->history_cursor++; else e->history_cursor--; }
        trace_record(TRACE_T2_MUTATION_DONE, e->grid.frame_id);
        er = replay_follow(e); if (er) return er;
    }
    return rc;
}
typedef enum binding { B_IGNORE, B_MOVE, B_TEXT, B_BACKSPACE, B_DELETE, B_UNDO, B_REDO } binding;
/* M0's complete temporary key table; replace this function with P4.3 keys. */
static binding key_binding(const plat_event *ev, view_key *move)
{
    if (ev->kind != PLAT_EV_KEY || !ev->press) return B_IGNORE;
    bool ctrl = (ev->mods & PLAT_MOD_CTRL) != 0;
    if (ctrl && (ev->keysym == XKB_KEY_z || ev->keysym == XKB_KEY_Z))
        return (ev->mods & PLAT_MOD_SHIFT) ? B_REDO : B_UNDO;
    if (ev->mods & (PLAT_MOD_ALT | PLAT_MOD_SUPER)) return B_IGNORE;
    switch (ev->keysym) {
    case XKB_KEY_Left: *move = ctrl ? VIEW_WORD_LEFT : VIEW_LEFT; return B_MOVE;
    case XKB_KEY_Right: *move = ctrl ? VIEW_WORD_RIGHT : VIEW_RIGHT; return B_MOVE;
    case XKB_KEY_Up: *move = VIEW_UP; return B_MOVE;
    case XKB_KEY_Down: *move = VIEW_DOWN; return B_MOVE;
    case XKB_KEY_BackSpace: *move = ctrl ? VIEW_WORD_LEFT : VIEW_LEFT; return B_BACKSPACE;
    case XKB_KEY_Delete: *move = ctrl ? VIEW_WORD_RIGHT : VIEW_RIGHT; return B_DELETE;
    case XKB_KEY_Return: case XKB_KEY_KP_Enter: return ctrl ? B_IGNORE : B_TEXT;
    default: return !ctrl && ev->utf8_len ? B_TEXT : B_IGNORE;
    }
}
int editor_continue_action(editor *e)
{
    view_change c; uint64_t start = trace_now_ns(); int rc = view_continue(&e->v, &c); slice_end(e, start);
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
    view_key move = VIEW_LEFT; binding b = key_binding(ev, &move);
    if (b == B_IGNORE) return 0;
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
    if (b == B_UNDO || b == B_REDO) {
        return replay_history(e, b == B_REDO);
    }
    uint64_t lo = min64(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t old = max64(e->old_selection.cursor, e->old_selection.anchor) - lo;
    if (b == B_TEXT) {
        const uint8_t *p = ev->utf8; size_t n = ev->utf8_len;
        const uint8_t lf[] = {'\n'}, crlf[] = {'\r', '\n'};
        if (ev->keysym == XKB_KEY_Return || ev->keysym == XKB_KEY_KP_Enter) { p = e->crlf ? crlf : lf; n = e->crlf ? 2u : 1u; }
        return mutate(e, lo, old, p, n, UNDO_DELETE);
    }
    undo_break_burst(&e->undo);
    if (b == B_BACKSPACE || b == B_DELETE) {
        e->delete_kind = b == B_BACKSPACE ? UNDO_BACKSPACE : UNDO_DELETE;
        if (old) return mutate(e, lo, old, NULL, 0, e->delete_kind);
        e->action = EDITOR_ACTION_DELETE;
    } else e->action = EDITOR_ACTION_MOVE;
    view_change c; uint64_t start = trace_now_ns();
    rc = view_command(&e->v, move, b == B_MOVE && (ev->mods & PLAT_MOD_SHIFT) != 0, NULL, 0, &c);
    slice_end(e, start);
    if (c.changed) return EDITOR_ERR_HISTORY;
    if (rc == VIEW_MORE) return 0;
    if (rc) return rc;
    return editor_continue_action(e);
}
