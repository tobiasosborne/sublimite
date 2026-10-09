/* P3.1b: caller-reserved single-line column index, snapshot worker + mailbox. */
#include "layout/layout.h"
#include <string.h>
#include <time.h>

/* Fits in work_msg.data; completion is the sole worker -> UI handoff. */
typedef struct checkpoint_result {
    layout_checkpoint_store *store;
    uint64_t end, columns;
    size_t count;
    uint32_t flags;              /* bit 0 complete, bit 1 newline */
} checkpoint_result;
_Static_assert(sizeof(checkpoint_result) <= WORK_MSG_DATA, "checkpoint message");

int layout_checkpoint_init(layout_checkpoint_store *s, edit_arena *arena, uint64_t max_line_bytes)
{
    if (!s || !arena || max_line_bytes > SIZE_MAX - LAYOUT_CHECKPOINT_STRIDE) return LAYOUT_ERR_ARG;
    uint64_t cap = max_line_bytes / LAYOUT_CHECKPOINT_STRIDE + 2;
    if (cap > SIZE_MAX / sizeof(layout_checkpoint)) return LAYOUT_ERR_ARG;
    memset(s, 0, sizeof *s);
    s->entries = edit_arena_alloc(arena, (size_t)cap * sizeof *s->entries, _Alignof(layout_checkpoint));
    if (!s->entries) return LAYOUT_ERR_STATE;
    s->capacity = (size_t)cap;
    s->generation = 1;
    return LAYOUT_DONE;
}

static void checkpoint_worker(work_ctx *ctx)
{
    layout_checkpoint_store *s = ctx->arg;
    piece_snapshot *snapshot = s->job.snapshot;
    uint64_t total = piece_snapshot_len(snapshot), pos = s->job.pos, column = s->job.column;
    size_t count = s->job.count;
    uint64_t next = ((pos - s->job.start) / LAYOUT_CHECKPOINT_STRIDE + 1) * LAYOUT_CHECKPOINT_STRIDE;
    uint8_t window[LAYOUT_WIN];
    size_t wi = 0, len = 0;
    utf8_cseg seg; utf8_cseg_init(&seg);
    bool active = false, complete = false, newline = false;
    for (;;) {
        if (work_should_stop(ctx)) break;
        if (!active && pos - s->job.start >= next) {
            if (count == s->capacity) break;
            s->entries[count++] = (layout_checkpoint){pos, column};
            next = ((pos - s->job.start) / LAYOUT_CHECKPOINT_STRIDE + 1) * LAYOUT_CHECKPOINT_STRIDE;
        }
        if (wi + 8 > len && pos < total) {
            uint64_t left = total - pos;
            len = left < LAYOUT_WIN ? (size_t)left : LAYOUT_WIN;
            if (piece_snapshot_read(snapshot, pos, window, len) != PIECE_OK) break;
            wi = 0;
        }
        if (!active && pos == total) { complete = true; break; }
        const uint8_t *p = window + wi;
        size_t avail = len - wi;
        if (!active) {
            if (*p == '\n') { complete = newline = true; break; }
            if (*p == '\r' && avail >= 2 && p[1] == '\n') { pos++; wi++; continue; }
            if (*p == '\t') { column += s->job.tab - column % s->job.tab; pos++; wi++; continue; }
            if (*p < 0x20 || *p == 0x7f) { column++; pos++; wi++; continue; }
            if (*p >= 0x20 && *p < 0x7f && (avail == 1 || p[1] < 0x80)) {
                /* Printable ASCII batches stop at checkpoints and before the
                 * final ASCII before non-ASCII (which may join a combining mark). */
                size_t n = 0, limit = avail;
                uint64_t until = next - (pos - s->job.start);
                if (until < limit) limit = (size_t)until;
                while (n < limit && p[n] >= 0x20 && p[n] < 0x7f &&
                       (n + 1 < avail ? p[n + 1] < 0x80 : pos + n + 1 == total)) n++;
                if (n) { pos += n; wi += n; column += n; continue; }
            }
            active = true;
        }
        size_t used = 0; int width = 0;
        int rc = utf8_cluster_step(&seg, p, avail, UTF8_GRAPHEME_BUDGET,
                                   pos + avail == total, &used, &width);
        pos += used; wi += used;
        if (rc == UTF8_G_END) { column += (uint32_t)width; active = false; }
        /* MORE can leave a partial UTF-8 tail; force refill at that position. */
        else if (rc == UTF8_G_MORE) len = wi;
    }
    checkpoint_result result = {s, pos, column, count, (complete ? 1u : 0u) | (newline ? 2u : 0u)};
    work_msg msg = {0}; msg.kind = LAYOUT_CHECKPOINT_MSG; msg.generation = ctx->generation;
    memcpy(msg.data, &result, sizeof result);
    piece_snapshot_release(snapshot);
    /* A full mailbox must not strand the single in-flight storage lease. */
    while (!work_should_stop(ctx) && !work_publish(ctx, &msg)) {
        struct timespec ts = {0, 1000000}; (void)nanosleep(&ts, NULL);
    }
}

int layout_checkpoint_request(layout_checkpoint_store *s, work_pool *pool,
                              piece_snapshot *snapshot, const void *source,
                              uint64_t start, uint32_t tab_width)
{
    if (!s || !s->entries || !pool || !snapshot || !source || start > piece_snapshot_len(snapshot))
        return LAYOUT_ERR_ARG;
    if (s->pending) return LAYOUT_ERR_STATE;
    uint32_t tab = tab_width ? tab_width : 4;
    if (s->source != source || s->start != start || s->tab != tab) {
        s->count = 0; s->complete = false; s->generation++;
    }
    s->source = source; s->start = start; s->tab = tab;
    if (s->count == 0) {
        s->entries[0] = (layout_checkpoint){start, 0}; s->count = 1;
    }
    layout_checkpoint cp = s->entries[s->count - 1];
    s->job.store = s; s->job.snapshot = piece_snapshot_retain(snapshot); s->job.source = source;
    s->job.start = start; s->job.pos = cp.byte; s->job.column = cp.column;
    s->job.count = s->count; s->job.tab = tab; s->job.generation = s->generation;
    work_handle handle = work_submit(pool, (work_job){checkpoint_worker, s, s->generation, WORK_BULK});
    if (handle.epoch == 0) { piece_snapshot_release(s->job.snapshot); return LAYOUT_ERR_STATE; }
    s->pending = true;
    return LAYOUT_DONE;
}

bool layout_checkpoint_event(layout_checkpoint_store *s, const work_msg *msg)
{
    if (!s || !msg || msg->kind != LAYOUT_CHECKPOINT_MSG) return false;
    checkpoint_result result; memcpy(&result, msg->data, sizeof result);
    if (result.store != s) return false;
    if (!s->pending) return true;
    s->pending = false;
    if (msg->generation != s->generation) return true;
    s->count = result.count; s->end = result.end; s->columns = result.columns;
    s->complete = (result.flags & 1u) != 0; s->newline = (result.flags & 2u) != 0;
    return true;
}

void layout_checkpoint_invalidate(layout_checkpoint_store *s, uint64_t off,
                                  uint64_t old_len, uint64_t new_len,
                                  uint64_t old_nl, uint64_t new_nl)
{
    (void)new_len; (void)old_nl; (void)new_nl;
    if (!s || !s->entries) return;
    s->generation++;
    if (off < s->start) { s->count = 0; s->complete = false; return; }
    if (s->complete && off > s->end) return;
    size_t lo = 0, hi = s->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s->entries[mid].byte < off) lo = mid + 1; else hi = mid;
    }
    s->count = lo;
    s->complete = false;
    (void)old_len;
}
