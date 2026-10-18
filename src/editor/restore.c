/* edit-qct: lazy restore of session journals that startup deferred (edit-fm4).
 *
 * A deferred source becomes a placeholder closed tab: an empty, inert buffer
 * that holds the source's lease and journal path and is never recorded in this
 * launch's journal. Reopening it scans the journal on the WORK_BULK lane into a
 * BASE-relative edit program; the UI opens each BASE through the configured
 * acquisition (mapped for large files), applies the program when the original
 * is attached, swaps the result into the placeholder's tab and checkpoints it.
 * The UI thread never reads the journal; it copies only journalled bytes. */
#include "editor/private.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { RESTORE_IDLE, RESTORE_SCANNING, RESTORE_OPENING, RESTORE_RETRY, RESTORE_DONE }; /* edit-ivv: RETRY waits a journal tick */
typedef struct restore_op { size_t buffer; uint64_t off, len; const uint8_t *data; bool insert; } restore_op;
typedef struct restore_text { uint8_t *bytes; size_t len, gap, gap_len; } restore_text;
struct editor_restore_buffer {
    editor_restore *owner;
    size_t index;
    uint64_t id, length;
    journal_base base;
    char base_path[IPC_PATH_CAP], target[IPC_PATH_CAP];
    journal_view_state view;
    bool full_view, named, save_pending, conflict, dropped;
    size_t edits;
    restore_text text;           /* dropped: whole text materialised on the worker */
    editor_buffer *replacement;  /* prepared, unpublished until install */
};
struct editor_restore {
    work_pool *pool;
    size_t *active;              /* editor.restore_active while SCANNING or OPENING */
    char journal[IPC_PATH_CAP], directory[IPC_PATH_CAP];
    int lease, state, scan_rc;
    int fault;                   /* edit-601 test seam (editor.restore_fault), consumed by one scan */
    work_handle job;
    uint32_t generation;
    _Atomic bool scan_finished;
    /* Worker-written program, read by the UI only after completion. */
    editor_restore_buffer **buffers;
    size_t count, capacity;
    restore_op *ops;
    size_t op_count, op_capacity;
    edit_arena payload;
    uint64_t *tabs;
    size_t tab_count;
    bool tabs_valid, conflicts;
};

static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) value |= (uint64_t)p[i] << (8u * i);
    return value;
}
/* Gap text for dropped buffers; ops arrive at nondecreasing offsets in the
 * whole-text shape, so each byte moves a bounded number of times (as
 * recovery.c's edit-28u gap buffer). */
static void text_move(restore_text *t, size_t at)
{
    if (at < t->gap) memmove(t->bytes + at + t->gap_len, t->bytes + at, t->gap - at);
    else if (at > t->gap) memmove(t->bytes + t->gap, t->bytes + t->gap + t->gap_len, at - t->gap);
    t->gap = at;
}
static int text_insert(restore_text *t, size_t at, const uint8_t *data, size_t n)
{
    if (n > SIZE_MAX - t->len) return JOURNAL_NOMEM;
    text_move(t, at);
    if (t->gap_len < n) {
        /* (P) geometric growth (a quarter of the text) keeps appends linear. */
        size_t extra = t->len / 4u, cap = t->len + t->gap_len;
        if (extra > SIZE_MAX - t->len - n) extra = 0;
        size_t want = t->len + n + extra;
        uint8_t *bytes = realloc(t->bytes, want ? want : 1); if (!bytes) return JOURNAL_NOMEM;
        size_t tail = cap - t->gap - t->gap_len, gap_len = want - t->len;
        memmove(bytes + t->gap + gap_len, bytes + t->gap + t->gap_len, tail);
        t->bytes = bytes; t->gap_len = gap_len;
    }
    memcpy(t->bytes + t->gap, data, n);
    t->gap += n; t->gap_len -= n; t->len += n; return 0;
}
static void text_free(restore_text *t) { free(t->bytes); *t = (restore_text){0}; }

/* ---- worker: journal -> program --------------------------------------- */
typedef struct restore_scan { editor_restore *r; work_ctx *c; } restore_scan;
static editor_restore_buffer *find(editor_restore *r, uint64_t id)
{
    for (size_t i = 0; i < r->count; i++) if (r->buffers[i]->id == id) return r->buffers[i];
    return NULL;
}
static void kill_ops(editor_restore *r, size_t index)
{
    for (size_t k = 0; k < r->op_count; k++) if (r->ops[k].buffer == index) r->ops[k].buffer = SIZE_MAX;
}
static int push_op(editor_restore *r, size_t index, bool insert, uint64_t off, uint64_t len, const uint8_t *data)
{
    if (r->op_count == r->op_capacity) {
        size_t want = r->op_capacity ? r->op_capacity * 2u : 16u; /* (P) doubling: amortised O(1) per op */
        if (want > SIZE_MAX / sizeof *r->ops) return JOURNAL_NOMEM;
        restore_op *ops = realloc(r->ops, want * sizeof *ops); if (!ops) return JOURNAL_NOMEM;
        r->ops = ops; r->op_capacity = want;
    }
    r->ops[r->op_count++] = (restore_op){index, off, len, data, insert};
    return 0;
}
static int scan_record(void *ctx, const journal_record *rec)
{
    restore_scan *s = ctx; editor_restore *r = s->r;
    if (work_should_stop(s->c)) return JOURNAL_CALLBACK;
    const uint8_t *p = rec->data;
    if (rec->type == JOURNAL_WINDOW) return 0;
    if (rec->type == JOURNAL_TABS) {
        if (rec->size < 16) return JOURNAL_INVALID;
        uint64_t count = get64(p);
        if (count > (rec->size - 16) / 8) return JOURNAL_INVALID;
        uint64_t *tabs = count ? realloc(r->tabs, (size_t)count * sizeof *tabs) : r->tabs;
        if (count && !tabs) return JOURNAL_NOMEM;
        for (size_t i = 0; i < count; i++) tabs[i] = get64(p + 16 + i * 8);
        r->tabs = tabs; r->tab_count = (size_t)count; r->tabs_valid = true; return 0;
    }
    editor_restore_buffer *b = find(r, rec->buffer_id);
    if (rec->type == JOURNAL_SAVE) {
        if (b && rec->size >= 12) b->save_pending = (get64((const uint8_t[8]){p[8], p[9], p[10], p[11], 0, 0, 0, 0}) == 1); /* PREPARED */
        return 0;
    }
    if (rec->type == JOURNAL_BASE) {
        if (!b) {
            if (r->count == r->capacity) {
                size_t want = r->capacity ? r->capacity * 2u : 4u; /* (P) doubling */
                editor_restore_buffer **list = realloc(r->buffers, want * sizeof *list); if (!list) return JOURNAL_NOMEM;
                r->buffers = list; r->capacity = want;
            }
            b = calloc(1, sizeof *b); if (!b) return JOURNAL_NOMEM;
            b->owner = r; b->index = r->count; b->id = rec->buffer_id; r->buffers[r->count++] = b;
        }
        if (journal_decode_base(rec, &b->base, b->base_path, sizeof b->base_path)) return JOURNAL_INVALID;
        /* A BASE resets the buffer's text to that file (recovery.c restore_record). */
        kill_ops(r, b->index); text_free(&b->text);
        b->dropped = b->conflict = false; b->length = b->base.size;
        b->named = *b->base_path != 0; strcpy(b->target, b->base_path);
        if (b->named) {
            int check = journal_check_base(&b->base);
            if (check == JOURNAL_BASE_CHANGED) b->conflict = r->conflicts = true; /* edit-btj: never replay onto it */
            else if (check) return check;
        }
        return 0;
    }
    if (!b) return JOURNAL_INVALID;
    if (rec->type == JOURNAL_TARGET) {
        if (!rec->size || rec->size > sizeof b->target || p[rec->size - 1]) return JOURNAL_INVALID;
        memcpy(b->target, p, rec->size); return 0;
    }
    if (rec->type == JOURNAL_VIEW) {
        b->full_view = rec->size == JOURNAL_VIEW_STATE_BYTES;
        return journal_decode_view_state(rec, &b->view);
    }
    if (rec->type != JOURNAL_INSERT && rec->type != JOURNAL_DELETE) return JOURNAL_INVALID;
    if (b->conflict) return 0;
    if (rec->size < (rec->type == JOURNAL_DELETE ? 16u : 8u)) return JOURNAL_INVALID;
    uint64_t off = get64(p);
    if (off > b->length) return JOURNAL_INVALID;
    if (rec->type == JOURNAL_INSERT) {
        size_t n = rec->size - 8;
        if (b->dropped) { int rc = text_insert(&b->text, (size_t)off, p + 8, n); if (rc) return rc; }
        else if (n) {
            uint8_t *copy = edit_arena_alloc(&r->payload, n, 1); if (!copy) return JOURNAL_NOMEM;
            memcpy(copy, p + 8, n);
            int rc = push_op(r, b->index, true, off, n, copy); if (rc) return rc;
        }
        b->length += n; return 0;
    }
    uint64_t n = get64(p + 8);
    if (n > b->length - off) return JOURNAL_INVALID;
    if (b->dropped) {
        text_move(&b->text, (size_t)off); b->text.gap_len += (size_t)n; b->text.len -= (size_t)n;
    } else if (!off && n && n == b->length) {
        /* The whole-text shape (pre-edit-28u checkpoints): everything before
         * is gone. Materialise the new text here; finish() expresses it as
         * BASE-relative edits so the UI never copies the whole text. */
        kill_ops(r, b->index); b->dropped = true;
    } else if (n) { int rc = push_op(r, b->index, false, off, n, NULL); if (rc) return rc; }
    b->length -= n; return 0;
}
/* Common prefix and suffix of the materialised text against BASE; the middle
 * becomes one DELETE + one INSERT. Reads BASE in JOURNAL_INSERT_SLICE_BYTES
 * chunks (an existing journal constant) on this worker. */
static int trim(restore_scan *s, editor_restore_buffer *b)
{
    restore_text *t = &b->text; text_move(t, t->len);
    uint64_t m = b->base.size, n = t->len, p = 0, q = 0, limit = m < n ? m : n;
    int fd = open(b->base_path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || (uint64_t)st.st_size != m) { if (fd >= 0) close(fd); b->conflict = b->owner->conflicts = true; return 0; }
    uint8_t chunk[JOURNAL_INSERT_SLICE_BYTES];
    int rc = 0;
    while (!rc && p < limit) {
        size_t want = limit - p < sizeof chunk ? (size_t)(limit - p) : sizeof chunk, k = 0;
        ssize_t got = pread(fd, chunk, want, (off_t)p);
        if (got <= 0) { rc = got < 0 && errno == EINTR ? 0 : JOURNAL_IO; if (rc) break; continue; }
        while (k < (size_t)got && chunk[k] == t->bytes[p + k]) k++;
        p += k; if (k < (size_t)got) break;
        if (work_should_stop(s->c)) rc = JOURNAL_CALLBACK;
    }
    while (!rc && q < limit - p) {
        uint64_t left = limit - p - q;
        size_t want = left < sizeof chunk ? (size_t)left : sizeof chunk, k = 0;
        ssize_t got = pread(fd, chunk, want, (off_t)(m - q - want));
        if (got != (ssize_t)want) { if (got < 0 && errno == EINTR) continue; rc = JOURNAL_IO; break; }
        while (k < want && chunk[want - 1 - k] == t->bytes[n - q - 1 - k]) k++;
        q += k; if (k < want) break;
        if (work_should_stop(s->c)) rc = JOURNAL_CALLBACK;
    }
    close(fd); if (rc) return rc;
    if (m - p - q) rc = push_op(b->owner, b->index, false, p, m - p - q, NULL);
    if (!rc && n - p - q) rc = push_op(b->owner, b->index, true, p, n - p - q, t->bytes + p);
    return rc;
}
static int scan(editor_restore *r, work_ctx *c)
{
    struct stat st;
    if (r->fault) return r->fault;
    if (stat(r->journal, &st) || st.st_size < 0) return JOURNAL_IO;
    /* (P) every journalled insert byte is a byte of the journal file. */
    if (edit_arena_init(&r->payload, (size_t)st.st_size + 1u)) return JOURNAL_NOMEM;
    restore_scan s = {r, c}; journal_replay_result result;
    /* Read-only: a torn tail is the accepted prefix (as journal_replay_file
     * would truncate to), but the source keeps every byte until retirement. */
    int rc = journal_scan_file(r->journal, scan_record, &s, &result);
    for (size_t i = 0; !rc && i < r->count; i++) {
        editor_restore_buffer *b = r->buffers[i];
        if (b->conflict) { kill_ops(r, i); text_free(&b->text); continue; }
        if (!b->dropped) continue;
        if (b->named) rc = trim(&s, b);
        else { text_move(&b->text, b->text.len); if (b->text.len) rc = push_op(r, i, true, 0, b->text.len, b->text.bytes); }
        if (b->conflict) kill_ops(r, i);
    }
    for (size_t k = 0; k < r->op_count; k++) if (r->ops[k].buffer != SIZE_MAX) r->buffers[r->ops[k].buffer]->edits++;
    if (!rc && !r->count) rc = JOURNAL_INVALID;
    if (!rc && r->tabs_valid) for (size_t i = 0; i < r->tab_count; i++) if (!find(r, r->tabs[i])) r->tabs_valid = false;
    if (!rc && r->tabs_valid) {
        /* edit-601: a repeated TABS id names one tab, so the live order never
         * holds more entries than there are buffers. */
        size_t kept = 0;
        for (size_t i = 0; i < r->tab_count; i++) {
            bool seen = false;
            for (size_t j = 0; j < kept && !seen; j++) seen = r->tabs[j] == r->tabs[i];
            if (!seen) r->tabs[kept++] = r->tabs[i];
        }
        r->tab_count = kept;
    }
    return rc;
}
static void restore_job(work_ctx *c)
{
    editor_restore *r = c->arg;
    r->scan_rc = scan(r, c);
    atomic_store(&r->scan_finished, true);
    work_msg msg = {.kind = EDITOR_RESTORE_MSG, .generation = r->generation};
    memcpy(msg.data, &r, sizeof r);
    /* The completion only wakes the UI loop; service also polls the handle. */
    while (!work_publish(c, &msg) && !work_should_stop(c)) sched_yield();
}

/* ---- lifetime ------------------------------------------------------------- */
static void program_free(editor_restore *r)
{
    for (size_t i = 0; i < r->count; i++) {
        editor_restore_buffer *b = r->buffers[i];
        if (b->replacement) editor_buffer_destroy(b->replacement);
        text_free(&b->text); free(b);
    }
    free(r->buffers); free(r->ops); free(r->tabs);
    if (r->payload.base) edit_arena_free(&r->payload);
    r->buffers = NULL; r->ops = NULL; r->tabs = NULL; r->payload = (edit_arena){0};
    r->count = r->capacity = r->op_count = r->op_capacity = r->tab_count = 0;
    r->tabs_valid = r->conflicts = false;
}
static void job_quiesce(editor_restore *r)
{
    if (!r->job.epoch) return;
    work_cancel(r->pool, r->job);
    while (!work_handle_finished(r->pool, r->job)) sched_yield();
    (void)work_mailbox_bind(r->pool, r->job, 0, NULL, NULL);
    r->job = (work_handle){0};
}
void editor_restore_free(editor_restore *r)
{
    if (!r) return;
    /* An evicted or closing placeholder may still be in flight. */
    if ((r->state == RESTORE_SCANNING || r->state == RESTORE_OPENING || r->state == RESTORE_RETRY) && *r->active) --*r->active;
    job_quiesce(r);
    program_free(r);
    if (r->lease >= 0) close(r->lease);
    free(r);
}

/* ---- startup: placeholder ------------------------------------------------- */
typedef struct restore_probe { char path[IPC_PATH_CAP]; } restore_probe;
/* Stops at the first BASE (or edit): every buffer's edits follow its BASE, so
 * only metadata records precede it and the streaming scan reads one batch. */
static int probe_record(void *ctx, const journal_record *rec)
{
    restore_probe *p = ctx;
    if (rec->type == JOURNAL_BASE) {
        journal_base base;
        if (journal_decode_base(rec, &base, p->path, sizeof p->path)) p->path[0] = 0;
        return JOURNAL_CALLBACK;
    }
    return rec->type == JOURNAL_INSERT || rec->type == JOURNAL_DELETE ? JOURNAL_CALLBACK : 0;
}
int editor_restore_defer(editor *e, const char *directory, const char *source, int lease)
{
    size_t free_slot = 0;
    while (free_slot < e->buffer_capacity && e->buffers[free_slot]) free_slot++;
    /* Bounded by the configured pools: one buffer slot, a transient live tab
     * for tabs_open, and a closed-tab record that evicts nothing. */
    if (free_slot == e->buffer_capacity || tabs_count(&e->tabs) == e->tabs.capacity ||
        tabs_closed_count(&e->tabs) >= e->tabs.closed_capacity) return EDITOR_ERR_CAPACITY;
    restore_probe probe = {""}; journal_replay_result result;
    (void)journal_scan_file(source, probe_record, &probe, &result);
    const char *name = *probe.path ? probe.path : source;
    editor_restore *r = calloc(1, sizeof *r); if (!r) return EDITOR_ERR_MEMORY;
    r->pool = &e->pool; r->active = &e->restore_active; r->lease = -1;
    if (strlen(source) >= sizeof r->journal || strlen(directory) >= sizeof r->directory) { free(r); return EDITOR_ERR_ARG; }
    strcpy(r->journal, source); strcpy(r->directory, directory);
    editor_buffer *b = NULL; int rc = editor_buffer_prepare(e, NULL, NULL, 0, &b);
    if (rc) { free(r); return rc; }
    b->placeholder = true; b->restore = r;
    uint64_t id; rc = editor_install_buffer(e, b, &id);
    if (rc) { editor_buffer_destroy(b); return rc; }
    const char *title; size_t title_len = editor_tab_title(name, &title);
    size_t index = tabs_active_index(&e->tabs);
    rc = tabs_rename(&e->tabs, index, title, title_len, name, strlen(name) < TABS_PATH_BYTES ? strlen(name) : 0);
    tabs_tab evicted;
    int closed = tabs_close(&e->tabs, index, &e->v.state, &evicted);
    EDIT_ASSERT(closed == 0 && !evicted.id); /* (P) capacity checked above */
    (void)rc;
    r->lease = lease; e->recovery_placeholder_ids++;
    return 0;
}

/* ---- UI: reopen, completion, apply, install --------------------------------- */
int editor_restore_start(editor *e, editor_buffer *b)
{
    editor_restore *r = b->restore;
    if (!b->placeholder || !r || r->state != RESTORE_IDLE) return 0;
    program_free(r);
    atomic_store(&r->scan_finished, false); r->scan_rc = 0;
    r->fault = e->restore_fault; e->restore_fault = 0;
    r->generation++;
    r->job = work_submit(&e->pool, (work_job){restore_job, r, r->generation, WORK_BULK});
    if (!r->job.epoch) return 0; /* pool full: the placeholder stays; reopen again */
    r->state = RESTORE_SCANNING; e->restore_active++;
    e->recovery_notice = "restoring large session journal"; e->recovery_notice_mutations = e->stats.mutations;
    return 0;
}
bool editor_restore_receive(editor *e, const work_msg *msg)
{
    if (msg->kind != EDITOR_RESTORE_MSG) return false;
    editor_restore *r; memcpy(&r, msg->data, sizeof r);
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (b && b->restore == r && r->generation == msg->generation) atomic_store(&r->scan_finished, true);
    }
    return true;
}
int editor_restore_apply(editor_buffer *b)
{
    editor_restore_buffer *rb = b->restore_apply; b->restore_apply = NULL;
    if (!rb || rb->conflict) return 0;
    /* edit-btj: the opened generation must be the journalled BASE. */
    if (rb->named && !editor_base_id_matches(&b->origin, &rb->base)) { rb->conflict = rb->owner->conflicts = true; return 0; }
    editor_restore *r = rb->owner;
    for (size_t k = 0; k < r->op_count; k++) {
        const restore_op *op = &r->ops[k];
        if (op->buffer != rb->index) continue;
        int rc = op->insert ? piece_insert(b->tree, op->off, op->data, (size_t)op->len) : piece_delete(b->tree, op->off, op->len, NULL);
        if (rc) return rc == PIECE_ERR_NOMEM ? EDITOR_ERR_MEMORY : EDITOR_ERR_IO;
    }
    return 0;
}
static size_t live_index(const editor *e, uint64_t id)
{
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) if (tabs_at(&e->tabs, i)->id == id) return i;
    return SIZE_MAX;
}
static void drop_replacements(editor *e, editor_restore *r)
{
    for (size_t i = 0; i < r->count; i++) if (r->buffers[i]->replacement) {
        editor_unpoll_buffer(e, r->buffers[i]->replacement);
        editor_buffer_destroy(r->buffers[i]->replacement); r->buffers[i]->replacement = NULL;
    }
}
/* Failure keeps the source: an unreadable or invalid journal is set aside
 * (edit-btj naming), any other failure leaves it discoverable. The
 * placeholder becomes an ordinary empty untitled buffer. edit-601: a restore
 * failure is a notice, never an editor error; a journal append that fails
 * here is recorded in stats.journal_error like any other journal write. */
static int restore_fail(editor *e, editor_buffer *p, int rc)
{
    editor_restore *r = p->restore;
    drop_replacements(e, r);
    char aside[IPC_PATH_CAP];
    if (rc == JOURNAL_INVALID && !editor_set_aside(r->journal, aside, sizeof aside)) {
        strcpy(e->recovery_aside, aside);
        e->recovery_notice = "session recovery failed; its journal was set aside";
    } else e->recovery_notice = "large session journal could not be restored; journal retained";
    e->recovery_notice_mutations = e->stats.mutations;
    program_free(r); r->state = RESTORE_DONE; e->restore_active--;
    if (r->lease >= 0) { close(r->lease); r->lease = -1; }
    p->placeholder = false;
    if (e->recovery_placeholder_ids) e->recovery_placeholder_ids--;
    size_t index = live_index(e, p->id);
    if (index != SIZE_MAX) (void)tabs_rename(&e->tabs, index, "untitled", strlen("untitled"), "", 0);
    if (!editor_register_journal(e, p, p->id)) (void)editor_journal_changed(e);
    return 0;
}
/* Pools for the whole program. (P) the placeholder's tab becomes the first;
 * a closed buffer needs one transient live slot before editor_close_tab
 * retains it (recovery.c). */
static size_t live_count(const editor_restore *r)
{
    size_t live = r->tabs_valid && r->tab_count ? r->tab_count : r->tabs_valid ? 1 : r->count;
    return live > r->count ? r->count : live;
}
static bool fits(const editor *e, const editor_restore *r)
{
    size_t free_slots = 0;
    for (size_t i = 0; i < e->buffer_capacity; i++) if (!e->buffers[i]) free_slots++;
    size_t live = live_count(r), closed = r->count - live;
    size_t extra = live - 1 + (closed ? 1u : 0u);
    return free_slots >= r->count && tabs_count(&e->tabs) + extra <= e->tabs.capacity &&
        tabs_closed_count(&e->tabs) + closed <= e->tabs.closed_capacity;
}
static int defer(editor *e, editor_restore *r)
{
    drop_replacements(e, r);
    program_free(r); r->state = RESTORE_IDLE; e->restore_active--;
    e->recovery_notice = "large session restore deferred; journal retained (tab capacity)";
    e->recovery_notice_mutations = e->stats.mutations;
    return 0;
}
static int prepare_all(editor *e, editor_buffer *p)
{
    editor_restore *r = p->restore;
    if (!fits(e, r)) return defer(e, r);
    for (size_t i = 0; i < r->count; i++) {
        editor_restore_buffer *b = r->buffers[i];
        const char *path = *b->target ? b->target : NULL;
        e->restore_prepare = b->conflict ? NULL : b;
        int rc = editor_buffer_prepare(e, b->conflict && b->named ? b->base_path : path, NULL, 0, &b->replacement);
        e->restore_prepare = NULL;
        if (rc) return restore_fail(e, p, rc);
        if (b->replacement->pending_open) b->replacement->restore_apply = b->conflict ? NULL : b;
    }
    r->state = RESTORE_OPENING;
    return 0;
}
static view_state restored_state(const editor_restore_buffer *rb, const editor_buffer *b)
{
    view_state state = b->initial;
    if (rb->conflict) return state; /* edit-btj: disk text, clean, from the start */
    uint64_t len = piece_len(b->tree);
    state.selection.cursor = rb->view.position.cursor > len ? len : rb->view.position.cursor;
    state.selection.anchor = rb->view.position.anchor > len ? len : rb->view.position.anchor;
    state.first_byte = rb->view.position.scroll_byte > len ? len : rb->view.position.scroll_byte;
    state.visual_byte = state.first_byte; state.hscroll = rb->view.position.scroll_x;
    if (rb->full_view) {
        /* The full record carries the line: no newline count over a mapped
         * original on the UI thread. */
        state.selection.preferred_col = rb->view.preferred_col;
        state.first_line = rb->view.first_line;
        state.visual_byte = rb->view.visual_byte;
        state.wrap = (rb->view.flags & JOURNAL_VIEW_WRAP) != 0;
        state.approximate = (rb->view.flags & JOURNAL_VIEW_APPROXIMATE) != 0;
        state.visual_end = (rb->view.flags & JOURNAL_VIEW_VISUAL_END) != 0;
    } else state.first_line = piece_byte_to_line(b->tree, state.first_byte);
    return state;
}
static void mark(editor *e, size_t index, editor_buffer *b, const editor_restore_buffer *rb)
{
    bool clean = rb->conflict || (!rb->save_pending && !rb->edits);
    b->saved_lost = !clean; b->revision++;
    (void)tabs_set_modified(&e->tabs, index, !clean);
}
/* What install changed, so a failure before the source is retired can put the
 * placeholder back (edit-601). installed[0] is the swapped first buffer. */
typedef struct restore_undo {
    size_t index;                /* the placeholder's tab */
    uint64_t active_id;          /* the tab that was active */
    view_state tab_state;        /* the placeholder tab's state */
    bool modified;
    editor_buffer **installed;
    size_t done;
} restore_undo;
/* Drop every installed tab, give the placeholder its tab back and reactivate.
 * The restored buffers retire as reload_source's outgoing generation does:
 * editor_retire_buffers releases them once frames and workers are done. */
static int rollback(editor *e, editor_buffer *p, const restore_undo *u)
{
    e->recovering = true; /* no metadata names a dropped buffer */
    for (size_t k = u->done; k-- > 1;) {
        editor_buffer *b = u->installed[k];
        size_t at = live_index(e, b->id);
        uint64_t id;
        /* A closed one is the newest closed record (install closes in order). */
        if (at == SIZE_MAX && tabs_closed_count(&e->tabs) && tabs_closed_at(&e->tabs, 0)->id == b->id &&
            !tabs_reopen(&e->tabs, &e->v.state, &id)) at = tabs_active_index(&e->tabs);
        if (at != SIZE_MAX) (void)tabs_drop(&e->tabs, at, &e->v.state);
        b->retired = true;
    }
    if (u->done) {
        (void)tabs_replace(&e->tabs, u->index, p->tree, &p->undo, &u->tab_state, &e->v.state);
        (void)tabs_set_modified(&e->tabs, u->index, u->modified);
        u->installed[0]->retired = true;
    }
    size_t active = live_index(e, u->active_id);
    if (active != SIZE_MAX) (void)tabs_select(&e->tabs, active, &e->v.state);
    e->recovering = false;
    return editor_activate(e);
}
static int install_all(editor *e, editor_buffer *p)
{
    editor_restore *r = p->restore;
    size_t index = live_index(e, p->id);
    if (index == SIZE_MAX || editor_activation_busy(e) || e->op_count || editor_save_active(e)) return 0;
    for (size_t i = 0; i < r->count; i++) {
        editor_buffer *b = r->buffers[i]->replacement;
        if (b->pending_open) {
            int rc = editor_adopt_open(e, b);
            if (rc || b->operation_error) return restore_fail(e, p, rc ? rc : EDITOR_ERR_IO);
            if (b->pending_open) return 0;
        }
    }
    /* Other tabs may have opened while the originals attached. */
    if (!fits(e, r)) return defer(e, r);
    /* Order: journalled live tabs first (their TABS order), then the rest;
     * each buffer once (edit-601: scan dedupes TABS, n stays <= count). */
    editor_restore_buffer **order = calloc(r->count, sizeof *order);
    editor_buffer **installed = calloc(r->count, sizeof *installed);
    if (!order || !installed) { free(order); free(installed); return restore_fail(e, p, JOURNAL_NOMEM); }
    size_t n = 0;
    for (size_t i = 0; r->tabs_valid && i < r->tab_count && n < r->count; i++) {
        editor_restore_buffer *b = find(r, r->tabs[i]);
        bool listed = !b;
        for (size_t j = 0; j < n && !listed; j++) listed = order[j] == b;
        if (!listed) order[n++] = b;
    }
    for (size_t i = 0; i < r->count; i++) {
        bool listed = false;
        for (size_t j = 0; j < n && !listed; j++) listed = order[j] == r->buffers[i];
        if (!listed) order[n++] = r->buffers[i];
    }
    size_t live = live_count(r);
    if (live > n) live = n;
    const tabs_tab *tab = tabs_at(&e->tabs, index);
    restore_undo u = {.index = index, .active_id = tabs_active(&e->tabs)->id,
        .tab_state = index == tabs_active_index(&e->tabs) ? e->v.state : tab->state,
        .modified = tab->modified, .installed = installed};
    /* edit-601: until the checkpoint is durable nothing reaches the journal or
     * the session tree, so no record names a buffer whose BASE is not there. */
    e->recovering = true;
    /* First: replace the placeholder in its own tab (reload_source's swap). */
    editor_buffer *first = order[0]->replacement;
    size_t slot = 0; while (slot < e->buffer_capacity && e->buffers[slot]) slot++;
    view_state state = restored_state(order[0], first);
    first->id = p->id; first->source_generation = p->source_generation; /* parked keys carry over */
    first->session_slot = slot; first->initial = state;
    e->buffers[slot] = first; order[0]->replacement = NULL; installed[u.done++] = first;
    if (e->buffer == p) e->buffer = first;
    int rc = tabs_replace(&e->tabs, index, first->tree, &first->undo, &state, &e->v.state);
    EDIT_ASSERT(rc == 0);
    const char *title; size_t title_len = editor_tab_title(first->path, &title);
    (void)tabs_rename(&e->tabs, index, title, title_len, first->path, strlen(first->path));
    mark(e, index, first, order[0]);
    for (size_t k = 1; !rc && k < n; k++) {
        editor_buffer *b = order[k]->replacement; uint64_t id;
        b->initial = restored_state(order[k], b);
        rc = editor_install_buffer(e, b, &id);
        if (rc) break;
        order[k]->replacement = NULL; installed[u.done++] = b;
        mark(e, tabs_active_index(&e->tabs), b, order[k]);
        if (k >= live) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
    }
    free(order);
    if (!rc) { rc = tabs_select(&e->tabs, live_index(e, first->id), &e->v.state); rc = rc ? EDITOR_ERR_ARG : editor_activate(e); }
    e->recovering = false;
    /* The recovered text is durable here before the source is retired. */
    if (!rc && e->journal) { rc = journal_flush(e->journal); if (!rc) rc = editor_checkpoint(e); }
    if (rc == JOURNAL_BUSY) {
        /* edit-ivv: the shared directory guard refused the checkpoint and
         * nothing was retired. Put the placeholder back, keep the scanned
         * program, the source and its lease, and prepare again once
         * editor_checkpoint_due (one journal tick). */
        int back = rollback(e, p, &u);
        free(installed);
        drop_replacements(e, r);
        r->state = RESTORE_RETRY;
        return back;
    }
    if (rc) {
        int back = rollback(e, p, &u);
        free(installed);
        (void)restore_fail(e, p, rc);
        return back; /* only a failed reactivation is an editor error */
    }
    free(installed);
    /* Irreversible from here: the placeholder retires, then the source. */
    p->retired = true;
    if (e->recovery_placeholder_ids) e->recovery_placeholder_ids--;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || b->retired || b->save || !*b->path) continue;
        int save_rc = editor_save_setup(e, b);
        if (save_rc) { b->save_error = b->operation_error = save_rc; b->save_errno = errno; }
    }
    (void)editor_journal_changed(e); /* session tree and metadata, suppressed above */
    char aside[IPC_PATH_CAP], lock[IPC_PATH_CAP];
    if (!e->journal) {
        /* edit-601: no checkpoint ran, so the source is the only durable copy
         * of these edits: never unlink or set it aside. */
        e->recovery_notice = "large session restored; its journal is kept (this launch has no session journal)";
        e->recovery_notice_mutations = e->stats.mutations;
        if (r->lease >= 0) { close(r->lease); r->lease = -1; }
        program_free(r); r->state = RESTORE_DONE; e->restore_active--;
        return 0;
    }
    if (r->conflicts) {
        e->recovery_notice = "file changed on disk since this session; its unsaved edits were set aside";
        if (!editor_set_aside(r->journal, aside, sizeof aside)) strcpy(e->recovery_aside, aside);
    } else {
        e->recovery_notice = NULL;
        (void)unlink(r->journal);
    }
    e->recovery_notice_mutations = e->stats.mutations;
    const char *leaf = strrchr(r->journal, '/');
    size_t parent = leaf ? (size_t)(leaf - r->journal) + 1 : 0;
    int len = snprintf(lock, sizeof lock, "%.*s.%s.lock", (int)parent, r->journal, r->journal + parent);
    if (len > 0 && (size_t)len < sizeof lock) (void)unlink(lock);
    int fd = open(r->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) { (void)fsync(fd); close(fd); }
    if (r->lease >= 0) { close(r->lease); r->lease = -1; }
    program_free(r); r->state = RESTORE_DONE; e->restore_active--;
    return 0;
}
int editor_restore_service(editor *e)
{
    if (!e->restore_active) return 0;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *p = e->buffers[i];
        if (!p || !p->placeholder || p->retired || !p->restore) continue;
        editor_restore *r = p->restore;
        if (r->state == RESTORE_SCANNING) {
            if (!atomic_load(&r->scan_finished) || !work_handle_finished(&e->pool, r->job)) continue;
            (void)work_mailbox_bind(&e->pool, r->job, 0, NULL, NULL);
            r->job = (work_handle){0};
            if (r->scan_rc) { int rc = restore_fail(e, p, r->scan_rc); if (rc) return rc; continue; }
            int rc = prepare_all(e, p); if (rc) return rc;
        }
        if (r->state == RESTORE_RETRY) {
            if (!editor_checkpoint_due(e)) continue;
            int rc = prepare_all(e, p); if (rc) return rc;
        }
        if (r->state == RESTORE_OPENING) { int rc = install_all(e, p); if (rc) return rc; }
    }
    return 0;
}
