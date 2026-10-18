/* edit-fi2: retain BASE off the UI thread after the first modification, so
 * recovery survives an external rewrite of the original file (git checkout,
 * a build step). docs/decisions/edit-fi2.md. Nothing here runs on the typing
 * path: the tick runs in source maintenance (editor_poll_sources), the copy
 * is one WORK_BULK job on the journal pool, and the UI never waits for it. */
#include "editor/private.h"
#include <sched.h>
#include <string.h>
#include <unistd.h>

bool editor_retain_matches(const editor_buffer *b, const journal_base *base)
{ return b->retain_state == EDITOR_RETAIN_READY && *base->path && editor_base_id_matches(&b->retain_of, base); }
/* The saved-history marker decides unsaved edits (edit-ret). */
static bool modified(const editor_buffer *b) { return b->saved_lost || b->history_cursor != b->saved_cursor; }
/* A copy is useful only while checkpoints keep BASE bytes by reference
 * (edit-28u original, edit-f3i saved revision). Otherwise the journal text is
 * self-contained (DELETE all + whole text) and needs no BASE bytes. */
static bool references_base(const editor_buffer *b)
{
    return editor_base_id_matches(&b->origin, &b->base) ||
        (b->saved_snapshot && editor_base_id_matches(&b->saved_text, &b->base));
}
static void drop_copy(editor *e, editor_buffer *b)
{
    (void)unlink(b->retain.copy.captured_path);
    b->retain_state = EDITOR_RETAIN_IDLE; b->retain_used = b->retain_building = false;
    if (e) editor_session_publish(e, b);
}
void editor_retain_tick(editor *e)
{
    if (!e->journal || e->recovering || !e->buffers) return;
    bool saving = editor_save_active(e) || editor_save_leased(e), want = false;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || b->placeholder || b->derived) continue;
        switch (b->retain_state) {
        case EDITOR_RETAIN_RUNNING:
            if (!work_handle_finished(b->retain_pool, b->retain_handle)) break;
            b->retain_handle = (work_handle){0};
            b->retain_state = b->retain.error ? EDITOR_RETAIN_FAILED : EDITOR_RETAIN_READY;
            b->retain_used = b->retain_building = false;
            if (b->retain_state == EDITOR_RETAIN_READY) editor_session_publish(e, b);
            break;
        case EDITOR_RETAIN_READY:
            /* BASE was replaced (save, Keep): the copy is no longer written;
             * the replacing checkpoint is durable before BASE changes. */
            if (!editor_base_id_matches(&b->retain_of, &b->base)) { if (!saving) drop_copy(e, b); }
            else if (!b->retain_used) want = true;
            break;
        case EDITOR_RETAIN_FAILED:
            if (!editor_base_id_matches(&b->retain_of, &b->base)) b->retain_state = EDITOR_RETAIN_IDLE;
            break;
        default:
            if (b->retired || b->pending_open || !*b->path || !*b->base.path || !b->base.size ||
                b->base.size > e->retain_limit || !modified(b) || !references_base(b)) break;
            if (journal_retain_start(e->journal, &b->retain, &b->base, &b->retain_handle)) break; /* pool full: next tick */
            b->retain_pool = &e->journal_pool; b->retain_of = editor_base_id_of(&b->base);
            b->retain_state = EDITOR_RETAIN_RUNNING;
        }
    }
    /* Reference the copy with the next complete checkpoint, under the same
     * gate as a clean-source refresh (edit-ret): no queued input, no frame in
     * flight, no staged journal edits, no save owning the journal. Its text
     * records are the edit-28u/f3i differences, O(pieces + edited bytes). */
    if (!want || saving || e->retain_retry == e->stats.mutations + 1u) return;
    if (editor_activation_busy(e) || e->backend->active || e->queue_count ||
        (e->dirty && e->frame.last_sequence) || e->op_count) return;
    int rc = journal_flush(e->journal);
    if (!rc) rc = editor_checkpoint(e);
    /* Retry after the next mutation, not every tick (e.g. another buffer's
     * BASE changed and awaits the user's Keep/Reload, which checkpoints). */
    e->retain_retry = rc ? e->stats.mutations + 1u : 0;
}
/* After a durable checkpoint: copies it referenced are protected; READY copies
 * it did not reference (BASE replaced, text equal to BASE, buffer retired)
 * have no durable reference left and are removed. */
void editor_retain_checkpointed(editor *e)
{
    bool saving = editor_save_active(e) || editor_save_leased(e);
    for (size_t i = 0; e->buffers && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || b->retain_state != EDITOR_RETAIN_READY) continue;
        b->retain_used = b->retain_building; b->retain_building = false;
        if (!b->retain_used && !saving) drop_copy(e, b);
    }
}
/* Buffer teardown. A copy the durable journal still names (referenced, and
 * BASE not replaced since: a save or Keep rotates without it before BASE
 * changes) stays: it is hot-exit state, retired by recovery or startup
 * housekeeping. Every other copy is removed. */
void editor_retain_stop(editor_buffer *b)
{
    if (b->retain_state == EDITOR_RETAIN_RUNNING) {
        work_cancel(b->retain_pool, b->retain_handle);
        while (!work_handle_finished(b->retain_pool, b->retain_handle)) sched_yield();
        if (!b->retain.error) drop_copy(NULL, b);
    } else if (b->retain_state == EDITOR_RETAIN_READY &&
               !(b->retain_used && editor_base_id_matches(&b->retain_of, &b->base))) drop_copy(NULL, b);
    b->retain_state = EDITOR_RETAIN_IDLE;
}
/* edit-9ev6 (recovery-3 F2): a running copy borrows the journal's IO
 * callbacks (journal_retain.io = &j->io) and runs on the journal pool, so
 * editor_close quiesces every buffer's retention before the journal or its
 * pool goes. journal_flush does not join it: it returns at once when the
 * journal prefix is already durable. Same policy as buffer teardown. */
void editor_retain_quiesce(editor *e)
{
    for (size_t i = 0; e->buffers && i < e->buffer_capacity; i++)
        if (e->buffers[i]) editor_retain_stop(e->buffers[i]);
    if (e->empty) editor_retain_stop(e->empty);
}
