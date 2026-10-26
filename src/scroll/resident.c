#include "scroll/scroll.h"
#include "file/file.h"
#include <string.h>
#include "os/os.h"
#if !defined(_WIN32)
/* edit-177r.4: mincore has no frozen os.h entry; coordinator decision pending. */
#include <sys/mman.h>
#endif

/* Window state belongs to UI; filled/error and byte storage belong to the
 * worker until the terminal message seals them. UI never polls worker fields. */
enum { WINDOW_FREE, WINDOW_PENDING, WINDOW_READY, WINDOW_FAILED };
typedef struct resident_result { uint32_t window; int error; } resident_result;
static uint64_t monotonic_ns(void)
{
    return os_now_ns();
}
static void acquire_window(work_ctx *ctx)
{
    scroll_resident_window *w = ctx->arg;
    const lineidx_src *src = &w->owner->snapshot;
    uint64_t deadline = monotonic_ns() + UINT64_C(500000);
    if (file_backing_faulted(w->owner->backing)) w->error = SCROLL_ERR_SOURCE;
    for (unsigned calls = 0; calls < 256u && w->filled < w->length && !w->error; calls++) {
        if (work_should_stop(ctx)) return;
        const uint8_t *p = NULL;
        size_t n = src->span(src->ctx, w->start + w->filled, &p);
        if (file_backing_faulted(w->owner->backing)) { w->error = SCROLL_ERR_SOURCE; break; }
        if (!n || !p) { w->error = SCROLL_ERR_SOURCE; break; }
        if (n > w->length - w->filled) n = w->length - w->filled;
        if (n > 4096u) n = 4096u;
        /* This is the only raw snapshot dereference: storage faults happen
         * on a worker (bulk, or foreground for an in-core window, edit-91c)
         * before the resident window is published. */
        memcpy(w->bytes + w->filled, p, n);
        if (file_backing_faulted(w->owner->backing)) { w->error = SCROLL_ERR_SOURCE; break; }
        w->filled += n;
        if (monotonic_ns() >= deadline) break;
    }
    if (!w->error && w->filled < w->length) { (void)work_continue(ctx); return; }
    if (file_backing_faulted(w->owner->backing)) w->error = SCROLL_ERR_SOURCE;
    work_msg msg = {.kind = SCROLL_RESIDENT_MSG, .generation = ctx->generation};
    resident_result result = {w->id, w->error};
    memcpy(msg.data, &result, sizeof result);
    if (!work_publish(ctx, &msg)) (void)work_continue(ctx);
}
static void receive_window(const work_msg *msg, void *ctx)
{
    scroll_resident_window *w = ctx;
    resident_result result;
    memcpy(&result, msg->data, sizeof result);
    if (msg->kind != SCROLL_RESIDENT_MSG || msg->generation != w->generation || result.window != w->id) return;
    if (file_backing_faulted(w->owner->backing)) result.error = SCROLL_ERR_SOURCE;
    w->error = result.error;
    w->state = result.error ? WINDOW_FAILED : WINDOW_READY;
}
int scroll_resident_init(scroll_resident *r, work_pool *pool, const lineidx_src *snapshot)
{
    if (!r || !pool || !snapshot || !snapshot->span || snapshot->len > LINEIDX_MAX_LEN) return SCROLL_ERR_ARG;
    memset(r, 0, sizeof *r); r->pool = pool; r->snapshot = *snapshot;
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) { r->windows[i].owner = r; r->windows[i].id = i; }
    return SCROLL_OK;
}
int scroll_resident_bind_snapshot(scroll_resident *r, const piece_snapshot *snapshot)
{
    if (!r || !r->pool || !snapshot || r->closing || r->generation || r->backing ||
        piece_snapshot_len(snapshot) != r->snapshot.len) return SCROLL_ERR_ARG;
    r->backing = file_snapshot_backing(snapshot);
    file_backing_acquire(r->backing);
    return SCROLL_OK;
}
void scroll_resident_poll(scroll_resident *r)
{
    if (!r || !r->pool || r->closing) return;
    uint64_t deadline = monotonic_ns() + UINT64_C(500000);
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) {
        scroll_resident_window *w = &r->windows[i];
        if (w->state == WINDOW_PENDING)
            (void)work_mailbox_receive_bounded(r->pool, w->handle, w->generation,
                                             receive_window, w, 4, deadline);
    }
}
int scroll_resident_close(scroll_resident *r)
{
    if (!r || !r->pool) return SCROLL_ERR_ARG;
    r->closing = true;
    bool finished = true;
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) {
        scroll_resident_window *w = &r->windows[i];
        work_cancel(r->pool, w->handle);
        (void)work_mailbox_bind(r->pool, w->handle, w->generation, NULL, NULL);
        if (!work_handle_finished(r->pool, w->handle)) finished = false;
    }
    if (!finished) return SCROLL_MORE;
    file_backing_release(r->backing); r->backing = NULL;
    return SCROLL_OK;
}
/* edit-91c: lane choice for one window copy (docs/decisions/edit-91c.md).
 * The bulk lane is occupied by a large file's index build, warm and find
 * jobs, each a continuation that re-queues behind the others, so a follow
 * outside the laid-out rows waited a whole round of their slices (or, when a
 * test holds the lane, forever). The foreground lane's contract (work.h,
 * work_pool_init_foreground) admits bounded CPU work that polls cancellation
 * every 5 ms CPU and never blocks on I/O. acquire_window copies at most
 * SCROLL_SCAN_BUDGET = 64 KiB in 4 KiB pieces with work_should_stop between
 * them: 65536 B / 6.3e9 B/s (naive scalar rate, perf/00-hardware-profile.md
 * (M)[bat]) = about 10 us (E) per window, far inside that bound. What could
 * block is a major page fault on a file-backed snapshot, so the copy takes
 * the foreground lane only when every page of the window is in core now:
 * mincore over the window's spans, addresses only (piece_iter_next derives
 * pointers without touching bytes), no dereference on the UI thread. A page
 * cached but not yet in this process's page table costs a minor fault,
 * about 1 us (E, perf/01-perf-target.md section 2.4 fault-around estimate):
 * 65536 / 4096 = 16 pages (G, existing constants) x 1 us = 16 us (E). The
 * probe is bounded by the same count: at most 16 spans, one mincore each;
 * a window fragmented into more spans, a page not in core, a failed probe,
 * a page size below 4 KiB or a pool without the lane keep WORK_BULK. The
 * check-to-copy race (eviction under memory pressure between the probe and
 * the copy) is the contract's own stated softness: "OS scheduling and
 * memory-bandwidth contention are not hard wall-time guarantees". */
#define RESIDENT_PROBE_PAGES (SCROLL_SCAN_BUDGET / 4096u)
static work_class window_class(const scroll_resident *r, uint64_t start, size_t length)
{
#if defined(_WIN32)
    /* edit-177r.4: no residency query in os.h; keep blocking faults off the
     * foreground lane until the coordinator resolves the contract gap. */
    (void)r; (void)start; (void)length;
    return WORK_BULK;
#else
    if (!r->pool->foreground_enabled) return WORK_BULK;
    size_t page_size = os_page_size();
    if (page_size < 4096) return WORK_BULK;
    uintptr_t page = (uintptr_t)page_size, mask = ~(page - 1u);
    const lineidx_src *src = &r->snapshot;
    uint64_t off = start, end = start + length;
    unsigned char vec[RESIDENT_PROBE_PAGES + 1u];
    for (unsigned spans = 0; off < end; spans++) {
        if (spans >= RESIDENT_PROBE_PAGES) return WORK_BULK;
        const uint8_t *p = NULL;
        size_t n = src->span(src->ctx, off, &p);
        if (!n || !p) return WORK_BULK;
        if (n > end - off) n = (size_t)(end - off);
        uintptr_t lo = (uintptr_t)p & mask, hi = ((uintptr_t)p + n + page - 1u) & mask;
        size_t pages = (size_t)((hi - lo) / page);
        if (pages > sizeof vec) return WORK_BULK;
        if (mincore((void *)lo, (size_t)(hi - lo), vec)) return WORK_BULK;
        for (size_t i = 0; i < pages; i++) if (!(vec[i] & 1u)) return WORK_BULK;
        off += n;
    }
    return WORK_FOREGROUND;
#endif
}
static size_t resident_span(void *ctx, uint64_t off, const uint8_t **out)
{
    scroll_resident *r = ctx; *out = NULL;
    if (r->closing || file_backing_faulted(r->backing) || off >= r->snapshot.len) return 0;
    uint64_t start = off / SCROLL_SCAN_BUDGET * SCROLL_SCAN_BUDGET;
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) {
        scroll_resident_window *w = &r->windows[i];
        if (w->state == WINDOW_FREE || w->start != start) continue;
        if (w->state == WINDOW_PENDING) { r->pending = true; return 0; }
        if (w->state == WINDOW_FAILED) return 0;
        *out = w->bytes + (size_t)(off - start);
        return w->length - (size_t)(off - start);
    }
    /* A window may be reused only after physical completion; a received
     * message can precede the worker's return. */
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) {
        uint32_t id = (r->replacement + i) % SCROLL_RESIDENT_WINDOWS;
        scroll_resident_window *w = &r->windows[id];
        if (w->state == WINDOW_PENDING || !work_handle_finished(r->pool, w->handle)) continue;
        if (r->generation == UINT32_MAX) return 0;
        (void)work_mailbox_bind(r->pool, w->handle, w->generation, NULL, NULL);
        w->start = start; w->length = (size_t)(r->snapshot.len - start);
        if (w->length > SCROLL_SCAN_BUDGET) w->length = SCROLL_SCAN_BUDGET;
        w->filled = 0; w->error = 0; w->generation = ++r->generation;
        work_class cls = window_class(r, w->start, w->length);
        w->handle = work_submit(r->pool, (work_job){acquire_window, w, w->generation, cls});
        if (!w->handle.epoch && cls == WORK_FOREGROUND) /* lane reserve exhausted */
            w->handle = work_submit(r->pool, (work_job){acquire_window, w, w->generation, WORK_BULK});
        if (!w->handle.epoch) { w->state = WINDOW_FREE; r->pending = true; return 0; }
        w->state = WINDOW_PENDING;
        (void)work_mailbox_bind(r->pool, w->handle, w->generation, receive_window, w);
        r->replacement = (id + 1u) % SCROLL_RESIDENT_WINDOWS;
        r->pending = true; return 0;
    }
    r->pending = true; return 0;
}
static int resident_slice(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                          scroll_resident *r, uint64_t cursor, bool following,
                          uint64_t budget, uint64_t deadline)
{
    if (!r || !r->pool || r->closing) return SCROLL_ERR_ARG;
    if (file_backing_faulted(r->backing)) { resolver->active = false; return SCROLL_ERR_SOURCE; }
    /* One shared wall deadline covers adoption AND resolution. */
    if (!deadline) deadline = monotonic_ns() + UINT64_C(500000);
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) {
        scroll_resident_window *w = &r->windows[i];
        if (w->state == WINDOW_PENDING)
            (void)work_mailbox_receive_bounded(r->pool, w->handle, w->generation,
                                             receive_window, w, 4, deadline);
    }
    r->pending = false;
    lineidx_src src = {r, r->snapshot.len, resident_span, NULL};
    scroll_state before = *s;
    int rc = following ? scroll_follow_cursor_slice(s, resolver, index, &src, cursor, budget, deadline) :
        scroll_resolve_slice(s, resolver, index, &src, budget, deadline);
    if (file_backing_faulted(r->backing)) {
        *s = before; resolver->active = false; return SCROLL_ERR_SOURCE;
    }
    if (rc == SCROLL_ERR_SOURCE && r->pending) {
        resolver->active = true; return SCROLL_MORE;
    }
    return rc;
}
int scroll_resolve_resident(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                            scroll_resident *resident, uint64_t budget, uint64_t deadline_ns)
{ return resident_slice(s, resolver, index, resident, 0, false, budget, deadline_ns); }
int scroll_follow_cursor_resident(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                                  scroll_resident *resident, uint64_t cursor_byte,
                                  uint64_t budget, uint64_t deadline_ns)
{ return resident_slice(s, resolver, index, resident, cursor_byte, true, budget, deadline_ns); }
