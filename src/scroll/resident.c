#include "scroll/scroll.h"
#include <string.h>
#include <time.h>

/* Window state belongs to UI; filled/error and byte storage belong to the
 * worker until the terminal message seals them. UI never polls worker fields. */
enum { WINDOW_FREE, WINDOW_PENDING, WINDOW_READY, WINDOW_FAILED };
typedef struct resident_result { uint32_t window; int error; } resident_result;
static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static void acquire_window(work_ctx *ctx)
{
    scroll_resident_window *w = ctx->arg;
    const lineidx_src *src = &w->owner->snapshot;
    uint64_t deadline = monotonic_ns() + UINT64_C(500000);
    for (unsigned calls = 0; calls < 256u && w->filled < w->length && !w->error; calls++) {
        if (work_should_stop(ctx)) return;
        const uint8_t *p = NULL;
        size_t n = src->span(src->ctx, w->start + w->filled, &p);
        if (!n || !p) { w->error = SCROLL_ERR_SOURCE; break; }
        if (n > w->length - w->filled) n = w->length - w->filled;
        if (n > 4096u) n = 4096u;
        /* This is the only raw snapshot dereference: storage faults happen
         * on the bulk worker before the resident window is published. */
        memcpy(w->bytes + w->filled, p, n);
        w->filled += n;
        if (monotonic_ns() >= deadline) break;
    }
    if (!w->error && w->filled < w->length) { (void)work_continue(ctx); return; }
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
    w->state = result.error ? WINDOW_FAILED : WINDOW_READY;
}
int scroll_resident_init(scroll_resident *r, work_pool *pool, const lineidx_src *snapshot)
{
    if (!r || !pool || !snapshot || !snapshot->span || snapshot->len > LINEIDX_MAX_LEN) return SCROLL_ERR_ARG;
    memset(r, 0, sizeof *r); r->pool = pool; r->snapshot = *snapshot;
    for (uint32_t i = 0; i < SCROLL_RESIDENT_WINDOWS; i++) { r->windows[i].owner = r; r->windows[i].id = i; }
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
    return finished ? SCROLL_OK : SCROLL_MORE;
}
static size_t resident_span(void *ctx, uint64_t off, const uint8_t **out)
{
    scroll_resident *r = ctx; *out = NULL;
    if (r->closing || off >= r->snapshot.len) return 0;
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
    int rc = following ? scroll_follow_cursor_slice(s, resolver, index, &src, cursor, budget, deadline) :
        scroll_resolve_slice(s, resolver, index, &src, budget, deadline);
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
