#include "editor/private.h"
#include "trace/trace.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- helpers ---------------------------------------------------------- */
static uint64_t estimate_line(const editor_buffer *b, uint64_t byte)
{
    const editor_large *lg = &b->lg;
    if (!lg->prefix_bytes) return 0;
    return (uint64_t)((double)byte * (double)lg->prefix_lines / (double)lg->prefix_bytes);
}

/* The shared mailbox may be full of another buffer's/index's results.
 * Keep the argument alive and retry; close cancels and joins this lease. */
static void publish_completion(work_ctx *c, uint32_t kind)
{
    work_msg msg = {.kind = kind, .generation = c->generation};
    while (!work_should_stop(c) && !work_publish(c, &msg)) {
        struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
    }
}

/* --- open ------------------------------------------------------------- */
void editor_large_init(editor_buffer *b)
{
    editor_large *lg = &b->lg;
    lg->mapped = b->file && file_open_mode(b->file) == FILE_MODE_MMAP;
    lg->lazy = lg->mapped || (b->file && file_size(b->file) > FILE_PREFIX_MAX);
    if (!lg->lazy) { b->lines = piece_line_count(b->tree); lg->lines_exact = true; lg->warm_done = true; return; }
    /* No foreground scan of a mapped original: the bounded prefix gives a
     * newline density; the total is an estimate until the index is published. */
    const file_prefix_info *pi = file_prefix_info_of(b->file); size_t plen = 0;
    (void)file_prefix(b->file, &plen);
    lg->prefix_lines = pi->lf + pi->crlf; lg->prefix_bytes = plen;
    uint64_t size = piece_len(b->tree);
    if (plen >= size) { b->lines = lg->prefix_lines + 1; lg->lines_exact = true; }
    else b->lines = estimate_line(b, size) + 1;
}
static void warm_fn(work_ctx *c)
{
    editor_large_warm_job *j = c->arg;
    uint64_t len = piece_snapshot_len(j->snap), off = 0;
    bool done = true;
    while (off < len) {
        if (work_should_stop(c)) { done = false; break; }
        off += EDITOR_LARGE_WARM_STRIDE; if (off > len) off = len;
        (void)piece_snapshot_byte_to_line(j->snap, off);   /* resolves chunks before off */
    }
    if (done) (void)piece_snapshot_line_count(j->snap);
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    if (done) publish_completion(c, EDITOR_LARGE_MSG_WARM);
}
static void buffer_receive(const work_msg *msg, void *ctx);
static void queue_warm(editor *e, editor_buffer *b)
{
    editor_large *lg = &b->lg;
    if (!lg->warm || lg->warm_done || lg->warm_h.epoch) return;
    work_job job = {.fn = warm_fn, .arg = lg->warm, .generation = 1, .cls = WORK_BULK};
    lg->warm_h = work_submit(&e->pool, job); /* reserved at open: no allocation */
    if (lg->warm_h.epoch) (void)work_mailbox_bind(&e->pool, lg->warm_h, 1, buffer_receive, b);
}
void editor_large_start(editor *e, editor_buffer *b)
{
    editor_large_warm_job *j = b->lg.lazy && !b->lg.warm_done ? calloc(1, sizeof *j) : NULL;
    if (!j) return;
    j->snap = piece_snapshot_take(b->tree); if (!j->snap) { free(j); return; }
    atomic_init(&j->released, false);
    b->lg.warm = j;
    /* Main's index job yields/requeues. Preserve the imported ordering: scan
     * the index first, then warm; keep the snapshot ready without UI malloc. */
    if (b->index && lineidx_complete(b->index)) queue_warm(e, b);
}
void editor_large_close(editor_buffer *b)
{
    editor_large *lg = &b->lg;
    work_pool *pool = b->pool;
    if (pool) {
        (void)work_mailbox_bind(pool, lg->warm_h, 1, NULL, NULL);
        (void)work_mailbox_bind(pool, lg->find_h, lg->find_gen, NULL, NULL);
        work_cancel(pool, lg->warm_h); work_cancel(pool, lg->find_h);
        while (!work_handle_finished(pool, lg->warm_h) || !work_handle_finished(pool, lg->find_h)) {
            struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
        }
    }
    if (lg->warm) { if (!atomic_exchange(&lg->warm->released, true)) piece_snapshot_release(lg->warm->snap); free(lg->warm); }
    if (lg->find) { if (!atomic_exchange(&lg->find->released, true)) piece_snapshot_release(lg->find->snap); free(lg->find); }
    lg->warm = NULL; lg->find = NULL;
}

/* --- estimate -> exact swap ------------------------------------------- */
static void index_progress(editor *e, editor_buffer *b)
{
    editor_large *lg = &b->lg;
    if (b->pending_open || b->source_stale || !lg->lazy || !b->index || !lineidx_complete(b->index)) return;
    queue_warm(e, b);
    bool changed = !lg->lines_exact;
    if (changed) {
        lineidx_result total = lineidx_line_count(b->index);
        if (!total.exact) return;
        b->lines = total.value; lg->lines_exact = true;
    }
    /* The active viewport is the sole live view. Retained tabs are corrected
     * when activated; never apply an inactive buffer's index to this view. */
    if (b != e->buffer) return;
    if (changed) e->full_pending = true;
    if (view_busy(&e->v)) return;
    if (lg->top_estimated) {
        lineidx_src cur = editor_source(b);
        lineidx_result r = lineidx_byte_to_line(b->index, &cur, e->v.state.first_byte);
        if (r.exact) { e->v.state.first_line = r.value; lg->top_estimated = false; changed = true; }
    }
    if (changed) e->full_pending = true;
}
void editor_large_index_progress(editor *e)
{
    for (size_t i = 0; i < e->buffer_capacity; i++)
        if (e->buffers && e->buffers[i]) index_progress(e, e->buffers[i]);
}
void editor_large_file_msg(editor *e, const file_msg *fm)
{
    if (fm->kind != FILE_MSG_SAVE_DONE) return;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers ? e->buffers[i] : NULL;
        if (!b || b->file != fm->f || fm->generation != b->lg.save_gen) continue;
        b->lg.save_running = false; b->lg.save_done = true; b->lg.save_status = fm->status;
        return;
    }
}
static bool matches(const work_msg *msg, work_handle h)
{ return h.epoch && msg->slot_ == h.slot && msg->epoch_ == h.epoch; }
static void buffer_receive(const work_msg *msg, void *ctx)
{
    editor_buffer *b = ctx;
    editor_large *lg = &b->lg;
    if (b->source_stale) return;
    if (msg->kind == EDITOR_LARGE_MSG_WARM && matches(msg, lg->warm_h)) lg->warm_done = true;
    if (msg->kind == EDITOR_LARGE_MSG_FIND && matches(msg, lg->find_h) && msg->generation == lg->find_gen && lg->find_running) {
        lg->find_running = false; lg->find_done = true;
    }
}
bool editor_large_receive(editor *e, const work_msg *msg)
{
    if (msg->kind != EDITOR_LARGE_MSG_WARM && msg->kind != EDITOR_LARGE_MSG_FIND) return false;
    /* Bound receivers also cover not-yet-installed IPC buffers. This fallback
     * keeps the central router safe for any unbound notification. */
    for (size_t i = 0; i < e->buffer_capacity; i++)
        if (e->buffers && e->buffers[i]) buffer_receive(msg, e->buffers[i]);
    if (e->empty) buffer_receive(msg, e->empty);
    return true;
}

/* --- public ----------------------------------------------------------- */
editor_large_status editor_large_status_get(const editor *e)
{
    editor_large_status s = {0};
    if (!e || !e->buffer) return s;
    const editor_buffer *b = e->buffer;
    const editor_large *lg = &b->lg;
    s.mapped = lg->mapped; s.lines_exact = lg->lines_exact; s.top_estimated = lg->top_estimated;
    s.warm_done = lg->warm_done; s.find_running = lg->find_running; s.find_done = lg->find_done;
    if (lg->find_done && lg->find) {
        s.find_rc = (int)lg->find->rc; s.find_total = lg->find->result.total;
        s.find_stored = lg->find->result.stored;
        s.find_first = lg->find->result.stored ? lg->find->result.offsets[0] : UINT64_MAX;
    }
    s.save_running = lg->save_running; s.save_done = lg->save_done; s.save_status = lg->save_status;
    return s;
}
int editor_large_goto_byte(editor *e, uint64_t byte)
{
    if (!e || view_busy(&e->v) || e->buffer->source_stale || e->buffer->pending_open) return EDITOR_ERR_ARG;
    editor_buffer *b = e->buffer;
    uint64_t len = piece_len(b->tree); if (byte > len) byte = len;
    uint64_t probe = byte < EDITOR_LARGE_PROBE_BYTES ? byte : EDITOR_LARGE_PROBE_BYTES, start = byte - probe;
    uint8_t buf[4096]; bool found = false;
    for (uint64_t at = byte; at > byte - probe && !found;) {      /* backward for the previous '\n' */
        uint64_t n = at - (byte - probe); if (n > sizeof buf) n = sizeof buf;
        if (piece_read(b->tree, at - n, buf, (size_t)n)) return EDITOR_ERR_ARG;
        for (uint64_t i = n; i > 0; i--) if (buf[i - 1] == '\n') { start = at - n + i; found = true; break; }
        at -= n;
    }
    uint64_t line; bool exact = true;
    if (b->index) {
        lineidx_src cur = editor_source(b);
        lineidx_result r = lineidx_byte_to_line(b->index, &cur, start);
        line = r.exact ? r.value : estimate_line(b, start); exact = r.exact;
    } else line = piece_byte_to_line(b->tree, start);
    uint64_t old = e->v.state.selection.cursor;
    e->old_selection = e->v.state.selection;
    e->v.state.selection.cursor = e->v.state.selection.anchor = byte;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->v.state.first_line = line; e->v.state.first_byte = e->v.state.visual_byte = start;
    e->v.state.visual_end = false; e->v.state.hscroll = 0;
    b->lg.top_estimated = !exact; undo_break_burst(e->undo);
    return editor_refresh_cursor(e, old);
}
uint64_t editor_large_line_to_byte(editor *e, uint64_t line, bool *exact)
{
    if (!e || e->buffer->source_stale || e->buffer->pending_open) { if (exact) *exact = false; return 0; }
    editor_buffer *b = e->buffer;
    if (!b->index) { if (exact) *exact = true; return piece_line_to_byte(b->tree, line); }
    lineidx_src cur = editor_source(b);
    lineidx_result r = lineidx_line_to_byte(b->index, &cur, line);
    if (exact) *exact = r.exact;
    return r.value;
}
static void find_fn(work_ctx *c)
{
    editor_large_find_job *j = c->arg;
    find_source src = {.snapshot = j->snap}; find_control ctl = {.work = c};
    j->rc = find_literal(&src, j->needle, j->needle_len, &ctl, &j->result);
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    publish_completion(c, EDITOR_LARGE_MSG_FIND);
}
int editor_large_find_begin(editor *e, const uint8_t *needle, size_t len)
{
    if (!e || e->buffer->source_stale || e->buffer->pending_open || !needle || !len || len > EDITOR_LARGE_NEEDLE_MAX || e->buffer->lg.find_running) return EDITOR_ERR_ARG;
    editor_buffer *b = e->buffer;
    editor_large *lg = &b->lg;
    if (lg->find) {
        if (!atomic_exchange(&lg->find->released, true)) piece_snapshot_release(lg->find->snap);
        free(lg->find); lg->find = NULL;
    }
    editor_large_find_job *j = calloc(1, sizeof *j); if (!j) return EDITOR_ERR_MEMORY;
    j->snap = piece_snapshot_take(b->tree); if (!j->snap) { free(j); return EDITOR_ERR_MEMORY; }
    atomic_init(&j->released, false);
    memcpy(j->needle, needle, len); j->needle_len = len;
    lg->find_gen++; lg->find_done = false;
    work_job job = {.fn = find_fn, .arg = j, .generation = lg->find_gen, .cls = WORK_BULK};
    lg->find_h = work_submit(&e->pool, job);
    if (!lg->find_h.epoch) { piece_snapshot_release(j->snap); free(j); return EDITOR_ERR_MEMORY; }
    lg->find = j; lg->find_running = true;
    (void)work_mailbox_bind(&e->pool, lg->find_h, lg->find_gen, buffer_receive, b);
    return 0;
}
int editor_large_save_begin(editor *e)
{
    if (!e || e->buffer->source_stale || e->buffer->pending_open || !e->buffer->file || e->buffer->lg.save_running) return EDITOR_ERR_ARG;
    editor_buffer *b = e->buffer;
    b->lg.save_done = false; b->lg.save_status = 0;
    if (file_save_begin(b->file, b->tree, 0, ++b->lg.save_gen)) return EDITOR_ERR_IO;
    b->lg.save_running = true; return 0;
}
