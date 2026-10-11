#include "editor/private.h"
#include "trace/trace.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* --- helpers ---------------------------------------------------------- */
static uint64_t estimate_line(const editor_buffer *b, uint64_t byte)
{
    const editor_large *lg = &b->lg;
    if (!lg->prefix_bytes) return 0;
    return (uint64_t)((double)byte * (double)lg->prefix_lines / (double)lg->prefix_bytes);
}

/* Terminal phases retain sealed state and yield on mailbox backpressure. */
static void publish_completion(work_ctx *c, uint32_t kind)
{
    work_msg msg = {.kind = kind, .generation = c->generation};
    if (!work_should_stop(c) && !work_publish(c, &msg)) (void)work_continue(c);
}

static void release_find(editor_large_find_job *j)
{
    if (!j) return;
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    free(j);
}
static void retire_find(editor_buffer *b)
{
    editor_large *lg = &b->lg;
    if (lg->find_retiring && work_handle_finished(b->pool, lg->find_retiring_h)) {
        release_find(lg->find_retiring);
        lg->find_retiring = NULL; lg->find_retiring_h = (work_handle){0};
    }
}

/* --- open ------------------------------------------------------------- */
void editor_large_init(editor_buffer *b)
{
    if (b->file && !b->pending_open) trace_record(TRACE_T3_RENDER_DONE, editor_large_trace_frame(b->file));
    editor_large *lg = &b->lg;
    lg->mapped = b->file && file_open_mode(b->file) == FILE_MODE_MMAP;
    lg->lazy = lg->mapped || (b->file && file_size(b->file) > FILE_PREFIX_MAX);
    if (!lg->lazy) { b->lines = piece_line_count(b->tree); lg->lines_exact = true; lg->warm_done = true; return; }
    /* No foreground scan of a mapped original: the bounded prefix gives a
     * newline density; the total is an estimate until the index is published. */
    const file_prefix_info *pi = file_prefix_info_of(b->file); size_t plen = 0;
    (void)file_prefix(b->file, &plen);
    lg->prefix_lines = pi->lf + pi->crlf; lg->prefix_bytes = plen;
    uint64_t size = b->pending_open ? file_size(b->file) : piece_len(b->tree);
    if (plen >= size) { b->lines = lg->prefix_lines + 1; lg->lines_exact = true; }
    else b->lines = estimate_line(b, size) + 1;
}
static void warm_fn(work_ctx *c)
{
    editor_large_warm_job *j = c->arg;
    if (j->sealed) { publish_completion(c, EDITOR_LARGE_MSG_WARM); return; }
    uint64_t len = piece_snapshot_len(j->snap);
    uint64_t deadline = trace_now_ns() + UINT64_C(500000);
    for (unsigned chunks = 0; chunks < 16u && j->offset < len; chunks++) {
        if (work_should_stop(c) || file_snapshot_faulted(j->snap)) goto cancelled;
        uint64_t n = len - j->offset;
        if (n > LINEIDX_CHUNK) n = LINEIDX_CHUNK;
        j->offset += n;
        (void)piece_snapshot_byte_to_line(j->snap, j->offset);
        if (file_snapshot_faulted(j->snap)) goto cancelled;
        if (trace_now_ns() >= deadline) break;
    }
    if (j->offset < len) { (void)work_continue(c); return; }
    (void)piece_snapshot_line_count(j->snap);
    if (work_should_stop(c) || file_snapshot_faulted(j->snap)) goto cancelled;
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    j->sealed = true; publish_completion(c, EDITOR_LARGE_MSG_WARM); return;
cancelled:
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
}
static void buffer_receive(const work_msg *msg, void *ctx);
static void queue_warm(editor *e, editor_buffer *b)
{
    editor_large *lg = &b->lg;
    if (!lg->warm || lg->warm_done || lg->warm_restart || lg->warm_h.epoch) return;
    work_job job = {.fn = warm_fn, .arg = lg->warm, .generation = 1, .cls = WORK_BULK};
    lg->warm_h = work_submit(&e->pool, job); /* reserved at open: no allocation */
    if (lg->warm_h.epoch) (void)work_mailbox_bind(&e->pool, lg->warm_h, 1, buffer_receive, b);
}
void editor_large_start(editor *e, editor_buffer *b)
{
    if (b->file) trace_record(TRACE_T5_DEVICE_DONE, editor_large_trace_frame(b->file));
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
        work_cancel(pool, lg->find_retiring_h);
        while (!work_handle_finished(pool, lg->warm_h) || !work_handle_finished(pool, lg->find_h) ||
               !work_handle_finished(pool, lg->find_retiring_h)) {
            struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
        }
    }
    if (lg->warm) { if (!atomic_exchange(&lg->warm->released, true)) piece_snapshot_release(lg->warm->snap); free(lg->warm); }
    release_find(lg->find); release_find(lg->find_retiring);
    lg->warm = NULL; lg->find = NULL; lg->find_retiring = NULL;
}

void editor_large_keep(editor_buffer *b)
{
    if (b->index && !lineidx_complete(b->index)) b->index_dirty = true;
    if (b->lg.warm && !b->lg.warm_done) b->lg.warm_restart = true;
}
static void restart_warm(editor_buffer *b)
{
    editor_large *lg = &b->lg;
    if (!lg->warm_restart || !work_handle_finished(b->pool, lg->warm_h)) return;
    editor_large_warm_job *j = lg->warm;
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    /* The buffer's recycling allocator was prepaid at open; retries do not
     * release a running worker's snapshot or allocate on the typing path. */
    j->snap = piece_snapshot_take(b->tree);
    if (!j->snap) return;
    atomic_store(&j->released, false); j->offset = 0; j->sealed = false;
    lg->warm_h = (work_handle){0}; lg->warm_restart = false;
}

/* --- estimate -> exact swap ------------------------------------------- */
/* The view's bounded scanner cannot discover a deep earlier page from the
 * current viewport seed: that anchor is AFTER its target. Import certified
 * index anchors before commands instead of replaying the original prefix.
 * Each exact lookup reads at most LINEIDX_CHUNK; storage was scanned by the
 * index worker. The fixed view cache owns the results, with no allocation. */
static void cache_line(editor *e, uint64_t line, uint64_t byte)
{
    view *v = &e->v;
    size_t capacity = sizeof v->lines / sizeof v->lines[0];
    for (size_t i = 0; i < capacity; i++)
        if (v->lines[i].valid && v->lines[i].line == line) return;
    unsigned slot = v->line_next++ % (unsigned)capacity;
    v->lines[slot].byte = byte; v->lines[slot].line = line; v->lines[slot].valid = true;
}
static void seed_line(editor *e, const lineidx_src *src, uint64_t line)
{
    view *v = &e->v;
    size_t capacity = sizeof v->lines / sizeof v->lines[0];
    for (size_t i = 0; i < capacity; i++)
        if (v->lines[i].valid && v->lines[i].line == line) return;
    lineidx_result r = lineidx_line_to_byte(e->buffer->index, src, line);
    if (r.exact) cache_line(e, line, r.value);
}
/* From EOF include its preceding delimiter; from a certified line start
 * exclude that line's preceding delimiter. Count only the requested rows,
 * sharing the view scanner's existing byte cap, and fall back to the index
 * if a long line/page cannot be certified locally. */
static bool previous_start(editor *e, uint64_t end, uint64_t rows, bool line_start, uint64_t *out)
{
    uint8_t bytes[VIEW_WINDOW]; uint64_t left = VIEW_SCAN_BOUND;
    if (line_start && end) end--;
    while (end && left) {
        uint64_t take = end < left ? end : left;
        if (take > sizeof bytes) take = sizeof bytes;
        end -= take; left -= take;
        if (piece_read(e->tree, end, bytes, (size_t)take) ||
            !lineidx_complete(e->buffer->index)) return false;
        size_t n = (size_t)take;
        const uint8_t *nl;
        while ((nl = memrchr(bytes, '\n', n))) {
            if (--rows == 0) { *out = end + (uint64_t)(nl - bytes) + 1u; return true; }
            n = (size_t)(nl - bytes);
        }
    }
    return false;
}
static void seed_previous(editor *e, const lineidx_src *src, uint64_t byte, uint64_t line, uint64_t page)
{
    uint64_t target = line > page ? line - page : 0, start;
    if (!target) cache_line(e, 0, 0);
    else if (previous_start(e, byte, page, true, &start)) cache_line(e, target, start);
    else seed_line(e, src, target);
}
/* Query tags are the view's private Q_BYTE/Q_LINE enum (view/view.c):
 * Q_BYTE starts at one; Q_LINE is its successor. Preserve the tag itself in
 * the existing result cache so a pending command resumes its original phase. */
static void seed_pending_query(editor *e, const lineidx_src *src)
{
    view *v = &e->v;
    enum { BYTE_QUERY = 1, LINE_QUERY = BYTE_QUERY + 1 };
    if (!view_busy(v) || (v->query_kind != BYTE_QUERY && v->query_kind != LINE_QUERY)) return;
    lineidx_result r = v->query_kind == BYTE_QUERY ?
        lineidx_byte_to_line(e->buffer->index, src, v->query_target) :
        lineidx_line_to_byte(e->buffer->index, src, v->query_target);
    if (!r.exact) return;
    size_t capacity = sizeof v->queries / sizeof v->queries[0];
    unsigned slot = v->query_next++ % (unsigned)capacity;
    v->queries[slot].kind = v->query_kind;
    v->queries[slot].target = v->query_target; v->queries[slot].result = r.value;
    v->query_kind = 0;
}
static void seed_navigation(editor *e, editor_buffer *b)
{
    if (e->tree != b->tree) return;
    bool bottom = view_busy(&e->v) && e->v.key == VIEW_DOC_END;
    bool previous = view_busy(&e->v) && e->v.key == VIEW_PAGE_UP;
    if (!view_busy(&e->v) && e->queue_count) {
        const plat_event *ev = &e->queue[e->queue_head];
        if (ev->kind == PLAT_EV_KEY && ev->press) {
            bottom = ev->keysym == XKB_KEY_End && (ev->mods & PLAT_MOD_CTRL);
            previous = ev->keysym == XKB_KEY_Page_Up;
        }
    }
    if (!bottom && !previous) return;
    lineidx_src src = editor_source(b);
    uint32_t frame = editor_large_trace_frame(&b->lg);
    trace_record(TRACE_T1_DEQUEUE, frame);
    seed_pending_query(e, &src);
    uint64_t page = e->v.config.rows;
    if (bottom) {
        /* The exact total certifies EOF's line number. Find its nearby start
         * without scanning the remainder of the last sparse-index chunk. */
        uint64_t last = b->lines - 1u, start = 0;
        if (!last || previous_start(e, src.len, 1, false, &start)) {
            cache_line(e, last, start);
            seed_previous(e, &src, start, last, page);
        } else {
            seed_line(e, &src, last);
            seed_line(e, &src, last > page ? last - page : 0);
        }
    } else if (!view_busy(&e->v) && !e->full_pending && !e->lay.approximate &&
               !b->lg.top_estimated && e->lay.src == b->tree && e->lay.total == src.len &&
               e->lay.first_byte == e->v.state.first_byte && e->lay.first_line == e->v.state.first_line &&
               e->v.state.selection.cursor >= e->lay.first_byte &&
               e->v.state.selection.cursor <= e->lay.pos) {
        /* A visible cursor's previous page is at/after this earlier anchor.
         * A visual page crosses no more logical lines than visual rows.
         * Layout's certified start survives the view's small cache eviction. */
        seed_previous(e, &src, e->lay.first_byte, e->lay.first_line, page);
    } else {
        lineidx_result cursor = lineidx_byte_to_line(b->index, &src, e->v.state.selection.cursor);
        if (cursor.exact) seed_line(e, &src, cursor.value > page ? cursor.value - page : 0);
        uint64_t top = e->v.state.first_line;
        if (!b->lg.top_estimated) seed_line(e, &src, top > page ? top - page : 0);
    }
    trace_record(TRACE_T2_MUTATION_DONE, frame);
}

static void index_progress(editor *e, editor_buffer *b)
{
    editor_large *lg = &b->lg;
    retire_find(b);
    if (!b->source_stale) restart_warm(b);
    if (b->pending_open || b->source_stale || !lg->lazy || !b->index || !lineidx_complete(b->index)) return;
    queue_warm(e, b);
    bool changed = !lg->lines_exact;
    if (changed) {
        lineidx_result total = lineidx_line_count(b->index);
        if (!total.exact) return;
        b->lines = total.value; lg->lines_exact = true;
        if (b->file) trace_record(TRACE_T6_PRESENT_COMPLETE, editor_large_trace_frame(b->file));
    }
    /* The active viewport is the sole live view. Retained tabs are corrected
     * when activated; never apply an inactive buffer's index to this view. */
    if (b != e->buffer) return;
    if (changed || lg->warm_repaint) e->full_pending = true;
    seed_navigation(e, b);
    if (view_busy(&e->v)) return;
    if (lg->warm_repaint) {
        lg->warm_repaint = false; e->paint_ready = false; changed = true;
    }
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
    if (msg->kind == EDITOR_LARGE_MSG_WARM && matches(msg, lg->warm_h)) {
        lg->warm_repaint |= !lg->warm_done; lg->warm_done = true;
    }
    if (msg->kind == EDITOR_LARGE_MSG_FIND && matches(msg, lg->find_h) && msg->generation == lg->find_gen && lg->find_running) {
        lg->find_running = false; lg->find_done = true;
        trace_record(TRACE_T4_PRESENT_SUBMITTED, editor_large_trace_frame(lg->find));
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
typedef struct byte_seek_job {
    piece_snapshot *snap;
    uint64_t target, pos, start, line, cursor, cluster_start;
    utf8_cseg cluster;
    unsigned phase;
    uint8_t window[4096]; uint64_t window_start; size_t window_len;
    bool done, error;
} byte_seek_job;
static void byte_seek_receive(const work_msg *msg, void *ctx)
{
    byte_seek_job *j = ctx;
    if (msg->kind == UINT32_C(0x45463103)) j->done = true;
}
static void byte_seek_scan(byte_seek_job *j, const work_ctx *ctx, uint64_t deadline)
{
    uint64_t length = piece_snapshot_len(j->snap);
    uint8_t bytes[4096];
    size_t budget = LINEIDX_CHUNK;
    while (j->phase < 2 && budget && !j->error) {
        if (ctx && work_should_stop(ctx)) return;
        if (file_snapshot_faulted(j->snap)) { j->error = true; break; }
        if (!j->phase) {
            size_t n = (size_t)(j->target - j->pos);
            if (n > sizeof bytes) n = sizeof bytes;
            if (!n) { j->pos = j->cluster_start = j->start; j->phase = 1; continue; }
            if (piece_snapshot_read(j->snap, j->pos, bytes, n)) { j->error = true; break; }
            for (size_t i = 0; i < n; i++) if (bytes[i] == '\n') {
                j->start = j->pos + i + 1u; j->line++;
            }
            j->pos += n; budget -= n;
        } else {
            if (!j->window_len || j->pos < j->window_start ||
                j->pos - j->window_start + 4u > j->window_len) {
                j->window_start = j->pos;
                j->window_len = (size_t)(length - j->pos);
                if (j->window_len > sizeof j->window) j->window_len = sizeof j->window;
                if (piece_snapshot_read(j->snap, j->pos, j->window, j->window_len)) { j->error = true; break; }
            }
            size_t at = (size_t)(j->pos - j->window_start);
            size_t n = j->window_len - at;
            if (n > budget) n = budget;
            size_t used = 0; int width = 0;
            int rc = utf8_cluster_step(&j->cluster, j->window + at, n, n ? n : 1u,
                                       j->pos + n == length, &used, &width);
            (void)width;
            j->pos += used; budget -= used;
            if (rc == UTF8_G_END) {
                if (j->pos > j->target) { j->cursor = j->cluster_start; j->phase = 2; }
                else if (j->pos == j->target) { j->cursor = j->pos; j->phase = 2; }
                else j->cluster_start = j->pos;
            } else if (j->pos > j->target) { j->cursor = j->cluster_start; j->phase = 2; }
            if (!used && rc != UTF8_G_END) { j->error = true; break; }
        }
        if (trace_now_ns() >= deadline) break;
    }
}
static void byte_seek_fn(work_ctx *ctx)
{
    byte_seek_job *j = ctx->arg;
    byte_seek_scan(j, ctx, trace_now_ns() + UINT64_C(500000));
    if (!j->error && j->phase < 2) { (void)work_continue(ctx); return; }
    if (file_snapshot_faulted(j->snap)) j->error = true;
    work_msg msg = {.kind = UINT32_C(0x45463103), .generation = ctx->generation};
    if (!work_publish(ctx, &msg)) (void)work_continue(ctx);
}
int editor_large_goto_byte(editor *e, uint64_t byte)
{
    if (!e || view_busy(&e->v) || e->buffer->source_stale || e->buffer->pending_open) return EDITOR_ERR_ARG;
    editor_buffer *b = e->buffer;
    uint64_t len = piece_len(b->tree); if (byte > len) byte = len;
    uint64_t start = 0, line = 0; bool exact = false;
    uint64_t probe = byte < EDITOR_LARGE_PROBE_BYTES ? byte : EDITOR_LARGE_PROBE_BYTES;
    uint8_t buf[4096]; bool found = byte <= probe;
    for (uint64_t at = byte; at > byte - probe;) {
        uint64_t n = at - (byte - probe); if (n > sizeof buf) n = sizeof buf;
        if (piece_read(b->tree, at - n, buf, (size_t)n)) return EDITOR_ERR_ARG;
        bool newline = false;
        for (uint64_t i = n; i > 0; i--) if (buf[i - 1] == '\n') {
            start = at - n + i; found = newline = true; break;
        }
        if (newline) break;
        at -= n;
    }
    if (found && b->index) {
        lineidx_src cur = editor_source(b);
        lineidx_result r = lineidx_byte_to_line(b->index, &cur, start);
        exact = r.exact; line = exact ? r.value : estimate_line(b, start);
    }
    /* A bounded coarse window is never imported as a logical start. The
     * owned worker proves long-line starts and repairs all interior bytes.
     * Explicit navigation may wait; no unproved selection is published. */
    byte_seek_job seek = {.target = byte, .start = start, .line = line};
    seek.snap = piece_snapshot_take(b->tree); if (!seek.snap) return EDITOR_ERR_MEMORY;
    if (found) { seek.pos = seek.cluster_start = start; seek.phase = 1; }
    /* The already certified nearby line permits a bounded resident repair.
     * Deep unresolved starts stay on the worker. */
    if (found) byte_seek_scan(&seek, NULL, UINT64_MAX);
    if (!seek.error && seek.phase < 2) {
        work_handle h = work_submit(&e->pool, (work_job){byte_seek_fn, &seek, 1, WORK_BULK});
        if (!h.epoch) { piece_snapshot_release(seek.snap); return EDITOR_ERR_CAPACITY; }
        (void)work_mailbox_bind(&e->pool, h, 1, byte_seek_receive, &seek);
        while (!seek.done || !work_handle_finished(&e->pool, h)) {
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            if (!seek.done) nanosleep(&(struct timespec){0, 100000}, NULL);
        }
        (void)work_mailbox_bind(&e->pool, h, 1, NULL, NULL);
    }
    bool faulted = file_snapshot_faulted(seek.snap); piece_snapshot_release(seek.snap);
    if (seek.error || faulted) return EDITOR_ERR_IO;
    if (!found) exact = true;
    uint64_t old = e->v.state.selection.cursor;
    e->old_selection = e->v.state.selection;
    e->v.state.selection.cursor = e->v.state.selection.anchor = seek.cursor;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->v.state.first_line = seek.line;
    e->v.state.first_byte = e->v.state.visual_byte = seek.start;
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
/* Borrow only the spans actually searched in this invocation. Adjacent
 * snapshot spans certify a contiguous window without an entire-source walk. */
static const uint8_t *find_window(editor_large_find_job *j, size_t length)
{
    piece_iter it; piece_iter_begin_snapshot(&it, j->snap, j->offset);
    const uint8_t *first = NULL, *bytes = NULL; size_t n = 0, covered = 0;
    while (covered < length && piece_iter_next(&it, &bytes, &n)) {
        if (!first) first = bytes;
        if (!bytes || !n || (uintptr_t)bytes != (uintptr_t)first + covered) break;
        if (n > length - covered) n = length - covered;
        covered += n;
    }
    if (covered == length) return first;
    if (piece_snapshot_read(j->snap, j->offset, j->window, length)) return NULL;
    return j->window;
}
static void find_fn(work_ctx *c)
{
    editor_large_find_job *j = c->arg;
    if (j->sealed) { publish_completion(c, EDITOR_LARGE_MSG_FIND); return; }
    if (!j->offset) trace_record(TRACE_T1_DEQUEUE, editor_large_trace_frame(j));
    uint64_t length = piece_snapshot_len(j->snap);
    uint64_t deadline = trace_now_ns() + UINT64_C(500000);
    size_t budget = LINEIDX_CHUNK;
    while (j->offset < length && budget) {
        if (work_should_stop(c) || file_snapshot_faulted(j->snap)) goto cancelled;
        uint64_t remaining = length - j->offset;
        size_t step = remaining > budget ? budget : (size_t)remaining;
        /* At most FIND_MAX_OFFSETS multi-byte matches fit in a window.
         * Thus the last accepted end is retained even in dense sources. */
        size_t maximum = j->needle_len * FIND_MAX_OFFSETS - (j->needle_len - 1u);
        if (j->needle_len > 1u && step > maximum) step = maximum;
        size_t overlap = j->needle_len - 1u;
        if (overlap > remaining - step) overlap = (size_t)(remaining - step);
        size_t n = step + overlap;
        const uint8_t *bytes = find_window(j, n);
        if (!bytes || file_snapshot_faulted(j->snap)) goto cancelled;
        find_source source = {.bytes = bytes, .length = n};
        find_control control = {.work = c}; find_result batch;
        if (find_literal(&source, j->needle, j->needle_len, &control, &batch) != FIND_OK ||
            file_snapshot_faulted(j->snap)) goto cancelled;
        uint64_t end = j->offset + step, next = end;
        if (j->needle_len == 1u) j->result.total += batch.total;
        for (size_t i = 0; i < batch.stored; i++) {
            uint64_t at = j->offset + batch.offsets[i];
            if (at >= end) break;
            if (j->needle_len > 1u) j->result.total++;
            if (j->result.stored < FIND_MAX_OFFSETS) j->result.offsets[j->result.stored++] = at;
            uint64_t match_end = at + j->needle_len;
            if (match_end > next) next = match_end;
        }
        j->offset = next; budget -= step;
        if (trace_now_ns() >= deadline) break;
    }
    if (j->offset < length) { (void)work_continue(c); return; }
    if (work_should_stop(c) || file_snapshot_faulted(j->snap)) goto cancelled;
    j->rc = FIND_OK; goto sealed;
cancelled:
    j->rc = FIND_CANCELLED; j->result.total = 0; j->result.stored = 0;
sealed:
    trace_record(TRACE_T2_MUTATION_DONE, editor_large_trace_frame(j));
    if (!atomic_exchange(&j->released, true)) piece_snapshot_release(j->snap);
    j->sealed = true;
    trace_record(TRACE_T3_RENDER_DONE, editor_large_trace_frame(j));
    publish_completion(c, EDITOR_LARGE_MSG_FIND);
}
int editor_large_find_begin(editor *e, const uint8_t *needle, size_t len)
{
    if (!e || e->buffer->source_stale || e->buffer->pending_open || !needle || !len || len > EDITOR_LARGE_NEEDLE_MAX || e->buffer->lg.find_running) return EDITOR_ERR_ARG;
    editor_buffer *b = e->buffer;
    editor_large *lg = &b->lg;
    retire_find(b);
    if (lg->find) {
        (void)work_mailbox_bind(&e->pool, lg->find_h, lg->find_gen, NULL, NULL);
        work_cancel(&e->pool, lg->find_h);
        if (!work_handle_finished(&e->pool, lg->find_h)) {
            if (lg->find_retiring) return EDITOR_ERR_CAPACITY;
            lg->find_retiring = lg->find; lg->find_retiring_h = lg->find_h;
        } else release_find(lg->find);
        lg->find = NULL; lg->find_h = (work_handle){0};
    }
    editor_large_find_job *j = calloc(1, sizeof *j); if (!j) return EDITOR_ERR_MEMORY;
    j->snap = piece_snapshot_take(b->tree); if (!j->snap) { free(j); return EDITOR_ERR_MEMORY; }
    atomic_init(&j->released, false);
    memcpy(j->needle, needle, len); j->needle_len = len;
    trace_record(TRACE_T0_INGRESS, editor_large_trace_frame(j));
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
