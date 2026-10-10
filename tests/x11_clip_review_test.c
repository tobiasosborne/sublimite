/* White-box clipboard regressions: deterministic ownership/queue/worker states,
 * real X transport. Including the implementation avoids production test hooks. */
#include "../src/x11/clip.c"
#include <stdio.h>

#define REQUIRE(x, message) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", message); return false; } } while (0)
typedef struct fixture { plat p; x11_input in; } fixture;
static bool fixture_init(fixture *f) {
    memset(f, 0, sizeof *f);
    f->p.conn = xcb_connect(":99", NULL);
    REQUIRE(!xcb_connection_has_error(C(&f->p)), "connect DISPLAY=:99");
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(C(&f->p))).data;
    f->p.win = xcb_generate_id(C(&f->p));
    xcb_create_window(C(&f->p), XCB_COPY_FROM_PARENT, f->p.win, screen->root,
                      0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, NULL);
    f->p.in = &f->in;
    REQUIRE(x11_clip_init(&f->p) == PLAT_OK, "clipboard init");
    return true;
}
static void fixture_destroy(fixture *f) {
    x11_clip_destroy(&f->p); xcb_destroy_window(C(&f->p), f->p.win); xcb_disconnect(C(&f->p));
}
static bool local_set(fixture *f, int which, const char *data, size_t len) {
    REQUIRE(plat_clip_set(&f->p, which, data, len) == PLAT_OK, "local set");
    CL(&f->p)->claiming[which] = 0; CL(&f->p)->owner_deadline[which] = 0;
    CL(&f->p)->confirmed[which] = true;
    return true;
}
static bool borrowed_paste(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    x11_clip_set_budget(&f.p, 12);
    REQUIRE(local_set(&f, 0, "before", 6) && local_set(&f, 1, "second", 6), "owners");
    REQUIRE(plat_clip_request(&f.p, 0) == PLAT_OK && x11_clip_poll(&f.p), "local completion");
    plat_event ev; REQUIRE(x11_q_pop(&f.in, &ev) && ev.clip_ok, "paste event");
    size_t len; const uint8_t *borrow = plat_clip_data(&f.p, &len);
    REQUIRE(len == 6 && borrow && !memcmp(borrow, "before", 6), "borrowed bytes");
    REQUIRE(plat_clip_set(&f.p, 0, "after!", 6) == PLAT_ERR_FAIL,
            "budget replacement must refuse while previous paste is borrowed");
    REQUIRE(plat_clip_data(&f.p, &len) == borrow && len == 6 && !memcmp(borrow, "before", 6),
            "borrow remains valid without another paste completion");
    x11_clip_set_budget(&f.p, 18);
    REQUIRE(plat_clip_set(&f.p, 0, "after!", 6) == PLAT_OK && !memcmp(borrow, "before", 6),
            "admitted replacement also preserves borrow");
    REQUIRE(x11_clip_mem(&f.p) == 18, "retained borrow charged to budget");
    fixture_destroy(&f); return true;
}
static bool completion_batches(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    REQUIRE(local_set(&f, 0, "old", 3), "owner");
    for (unsigned i = 0; i < MAX_WAITERS; i++)
        REQUIRE(plat_clip_request(&f.p, 0) == PLAT_OK, "first maximum local batch");
    REQUIRE(plat_clip_set(&f.p, 0, "new", 3) == PLAT_OK, "replace before delivery");
    REQUIRE(plat_clip_request(&f.p, 0) == PLAT_ERR_FAIL,
            "aggregate outstanding cap includes ready and confirming requests");
    CL(&f.p)->claiming[0] = 0; CL(&f.p)->confirmed[0] = true;
    flush_local(&f.p, 0);
    /* Exercise partial emission against a nearly full ring, independently of
     * runtime's usual empty-queue poll policy. */
    plat_event ev = { .kind = PLAT_EV_MOTION };
    for (unsigned i = 0; i < X11_QUEUE_CAP - 2u; i++) REQUIRE(x11_q_push(&f.in, &ev), "fill ring");
    REQUIRE(deliver_local(&f.p, 0), "partial delivery");
    REQUIRE(f.in.q_dropped == 0, "partial batch must not overflow event ring");
    unsigned completed = 0;
    while (x11_q_pop(&f.in, &ev)) if (ev.kind == PLAT_EV_CLIPBOARD) completed++;
    REQUIRE(plat_clip_set(&f.p, 0, "later", 5) == PLAT_OK, "replace between partial deliveries");
    CL(&f.p)->claiming[0] = 0; CL(&f.p)->confirmed[0] = true;
    while (completed < MAX_WAITERS) {
        REQUIRE(x11_clip_poll(&f.p), "retained completion progress");
        while (x11_q_pop(&f.in, &ev)) {
            REQUIRE(ev.kind == PLAT_EV_CLIPBOARD && ev.clip_ok, "completion status");
            size_t len; const uint8_t *bytes = plat_clip_data(&f.p, &len);
            REQUIRE(len == 3 && bytes && !memcmp(bytes, "new", 3), "partial batch retains its data association");
            completed++;
        }
    }
    REQUIRE(completed == MAX_WAITERS && f.in.q_dropped == 0, "one completion per accepted request");
    REQUIRE(plat_clip_request(&f.p, 0) == PLAT_OK, "capacity reusable after delivery");
    fixture_destroy(&f); return true;
}
static bool deferred_failures(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    plat_event ev = { .kind = PLAT_EV_MOTION };
    for (unsigned i = 0; i < X11_QUEUE_CAP; i++) REQUIRE(x11_q_push(&f.in, &ev), "fill ring");
    for (unsigned i = 0; i < MAX_WAITERS; i++) result(&f.p, 1, false, 1);
    result(&f.p, 0, false, 2);
    REQUIRE(f.in.q_dropped == 0 && CL(&f.p)->failed[1] == MAX_WAITERS, "failures retained without queue refusal");
    REQUIRE(plat_clip_request(&f.p, 1) == PLAT_ERR_FAIL, "deferred failures count toward admission cap");
    while (x11_q_pop(&f.in, &ev)) { }
    unsigned failed = 0, lost = 0;
    while (x11_clip_busy(&f.p)) {
        REQUIRE(x11_clip_poll(&f.p), "deferred failure progress");
        while (x11_q_pop(&f.in, &ev)) {
            REQUIRE(ev.kind == PLAT_EV_CLIPBOARD && !ev.clip_ok, "failure status");
            if (ev.code == 1) failed++; else if (ev.code == 2) lost++;
        }
    }
    REQUIRE(failed == MAX_WAITERS && lost == 1 && f.in.q_dropped == 0, "all deferred events emitted exactly once");
    fixture_destroy(&f); return true;
}
static bool remote_partial_batch(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    x11_clip *c = CL(&f.p); clip_receive *r = &c->rx[1];
    r->sink.buf = x11_clip_buf_new(6); REQUIRE(r->sink.buf != NULL, "receive blob");
    memcpy(r->sink.buf->data, "remote", 6); c->mem += 6; r->cap = 6;
    r->state = 5; r->fin_ok = true; r->waiters = MAX_WAITERS; r->win = xcb_generate_id(C(&f.p));
    xcb_create_window(C(&f.p), 0, r->win, f.p.win, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, 0, NULL);
    plat_event ev = { .kind = PLAT_EV_MOTION };
    for (unsigned i = 0; i < X11_QUEUE_CAP - 2u; i++) REQUIRE(x11_q_push(&f.in, &ev), "fill ring");
    rx_complete(&f.p, 1);
    REQUIRE(r->waiters == MAX_WAITERS - 2u && r->sink.buf && !f.in.q_dropped, "receive retains remaining completions and bytes");
    unsigned completed = 0;
    while (x11_q_pop(&f.in, &ev)) if (ev.kind == PLAT_EV_CLIPBOARD) completed++;
    REQUIRE(x11_clip_poll(&f.p), "remaining receive delivery");
    while (x11_q_pop(&f.in, &ev)) {
        size_t len; const uint8_t *bytes = plat_clip_data(&f.p, &len);
        REQUIRE(ev.clip_ok && ev.clip_which == 1 && len == 6 && bytes && !memcmp(bytes, "remote", 6), "receive data association");
        completed++;
    }
    REQUIRE(completed == MAX_WAITERS && !r->state && c->mem == 6, "complete receive retains only borrowed paste");
    /* Failed admission can leave charged capacity without an allocated sink. */
    r->state = 5; r->fin_ok = false; r->waiters = 1; r->cap = 4096; c->mem += r->cap;
    r->win = xcb_generate_id(C(&f.p));
    xcb_create_window(C(&f.p), 0, r->win, f.p.win, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, 0, NULL);
    rx_complete(&f.p, 1);
    REQUIRE(c->mem == 6, "failed receive releases charged capacity even without a sink blob");
    REQUIRE(x11_q_pop(&f.in, &ev) && ev.code == 1, "failed receive emits its completion");
    fixture_destroy(&f); return true;
}
static bool saturated_multiple(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    REQUIRE(local_set(&f, 0, "ordered", 7), "owner");
    x11_clip *c = CL(&f.p);
    xcb_selection_request_event_t rq = { .response_type = XCB_SELECTION_REQUEST, .owner = f.p.win,
        .requestor = f.p.win, .selection = c->sel[0], .target = c->multiple, .time = 0 };
    xcb_atom_t pair[2] = { c->utf8, c->prop[1] };
    xcb_change_property(C(&f.p), XCB_PROP_MODE_REPLACE, f.p.win, c->prop[0], c->atom_pair, 32, 2, pair);
    rq.property = c->prop[0];
    for (unsigned i = 0; i < NJOB; i++) serve(&f.p, &rq);
    for (unsigned i = 0; i < NJOB; i++) REQUIRE(c->jobs[i].state == 1, "all MULTIPLE slots occupied");
    /* The latest invalid MULTIPLE has no property; its failure must not be
     * published while an older indistinguishable tuple is still active. */
    rq.property = XCB_ATOM_NONE; serve(&f.p, &rq);
    xcb_get_input_focus_reply_t *bar = xcb_get_input_focus_reply(C(&f.p), xcb_get_input_focus(C(&f.p)), NULL);
    REQUIRE(bar != NULL, "server barrier"); free(bar);
    unsigned notified = 0; xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(C(&f.p)))) {
        if ((event->response_type & 0x7fu) == XCB_SELECTION_NOTIFY) {
            const xcb_selection_notify_event_t *n = (const xcb_selection_notify_event_t *)event;
            if (n->target == c->multiple) notified++;
        }
        free(event);
    }
    if (notified) for (unsigned i = 0; i < NJOB; i++)
        REQUIRE(!c->jobs[i].state, "saturated refusal must not overtake retained older MULTIPLE jobs");
    REQUIRE(notified == NJOB + 1u, "bounded saturation retires older tuples then refuses newest");
    /* Also exercise the otherwise-valid ninth request and reuse of emptied slots. */
    rq.property = c->prop[0];
    for (unsigned i = 0; i < NJOB + 1u; i++) serve(&f.p, &rq);
    REQUIRE(x11_clip_busy(&f.p) == 0, "valid saturated request follows same ordered refusal path");
    serve(&f.p, &rq); REQUIRE(c->jobs[0].state == 1, "job slot reusable after saturation");
    fixture_destroy(&f); return true;
}
typedef struct blocked_worker { _Atomic bool started, release; } blocked_worker;
static void block_worker(work_ctx *ctx) {
    blocked_worker *b = ctx->arg;
    atomic_store(&b->started, true);
    while (!atomic_load(&b->release) && !work_should_stop(ctx)) {
        struct timespec ts = { 0, 100000 }; (void)nanosleep(&ts, NULL);
    }
}
static bool prompt_admission(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    x11_clip *c = CL(&f.p);
    REQUIRE(pool_ready(c), "prepare worker for blocked-worker admission test");
    blocked_worker b; atomic_init(&b.started, false); atomic_init(&b.release, false);
    work_handle h = work_submit(c->pool, (work_job){ .fn = block_worker, .arg = &b, .cls = WORK_BULK });
    REQUIRE(h.epoch != 0, "blocked worker submitted");
    uint64_t end = trace_now_ns() + UINT64_C(2000000000);
    while (!atomic_load(&b.started) && trace_now_ns() < end) {
        struct timespec ts = { 0, 100000 }; (void)nanosleep(&ts, NULL);
    }
    REQUIRE(atomic_load(&b.started), "worker started");
    clip_blob *old = x11_clip_buf_new(SLICE_BYTES);
    REQUIRE(old != NULL, "deferred blob"); c->mem += old->cap; blob_unref(c, old);
    REQUIRE(c->frees_out == 1, "free pending behind blocked worker");
    x11_clip_set_budget(&f.p, SLICE_BYTES);
    uint64_t elapsed = 0; bool refused = true;
    for (unsigned i = 0; i < 4; i++) {
        x11_clip_buf *candidate = x11_clip_buf_new(SLICE_BYTES);
        REQUIRE(candidate != NULL, "candidate");
        uint64_t t0 = trace_now_ns();
        int rc = x11_clip_set_buf(&f.p, 0, candidate);
        elapsed += trace_now_ns() - t0; refused = refused && rc == PLAT_ERR_FAIL;
    }
    atomic_store(&b.release, true);
    end = trace_now_ns() + UINT64_C(2000000000);
    while (c->frees_out && trace_now_ns() < end) {
        (void)work_mailbox_drain(c->pool, on_work_msg, c);
        struct timespec ts = { 0, 100000 }; (void)nanosleep(&ts, NULL);
    }
    printf("blocked-worker admission average (M)[AC]: %llu ns over four back-to-back calls\n",
           (unsigned long long)(elapsed / 4u));
    REQUIRE(refused, "blocked admission refuses synchronously");
    REQUIRE(elapsed / 4u < UINT64_C(2000000), "admission must return promptly rather than sleep for deferred frees");
    REQUIRE(c->frees_out == 0 && c->mem == 0, "completion releases admission budget");
    REQUIRE(x11_clip_set_buf(&f.p, 0, x11_clip_buf_new(SLICE_BYTES)) == PLAT_OK, "retry after worker completion");
    fixture_destroy(&f); return true;
}
static bool eager_aligned_pool(void) {
    fixture f; REQUIRE(fixture_init(&f), "fixture");
    REQUIRE(CL(&f.p)->pool != NULL, "clipboard worker must be initialized before runtime dispatch");
    REQUIRE((uintptr_t)CL(&f.p)->pool % _Alignof(work_pool) == 0, "clipboard pool has required alignment");
    fixture_destroy(&f); return true;
}
int main(int argc, char **argv) {
    bool all = argc == 1;
    trace_init(); trace_thread_register();
    if (all || !strcmp(argv[1], "1")) { if (!borrowed_paste()) return 1; puts("clipboard review §1: PASS"); }
    if (all || !strcmp(argv[1], "26")) { if (!completion_batches() || !deferred_failures() || !remote_partial_batch()) return 1; puts("clipboard review §26: PASS"); }
    if (all || !strcmp(argv[1], "27")) { if (!saturated_multiple()) return 1; puts("clipboard review §27: PASS"); }
    if (all || !strcmp(argv[1], "28")) {
        bool prompt = prompt_admission(); bool eager = eager_aligned_pool();
        if (!prompt || !eager) return 1;
        puts("clipboard review §28: PASS");
    }
    return 0;
}
