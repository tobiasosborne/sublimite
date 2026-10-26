#include "x11/private.h"
/* P2.2e regressions. Include the implementation only to inject setup failures
 * and inspect ordered dispatch without adding production test hooks. */
#include "base/base.h"
#include "x11/input.h"
#include "x11/clip.h"
#include "x11_xvfb.h"
#include "trace/trace_fmt.h"
#include <xcb/xkb.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static struct {
    bool input_alloc, compose_alloc, raw_stream, extended_alloc;
    unsigned table_unrefs, inject_poll;
    bool raw_focus;
    unsigned flags, raw_count;
    bool raw_release;
} faults;

static void *test_calloc(size_t n, size_t size);
static struct xkb_compose_state *test_compose_new(struct xkb_compose_table *t,
                                                 enum xkb_compose_state_flags flags) {
    return faults.compose_alloc ? NULL : xkb_compose_state_new(t, flags);
}
static void test_table_unref(struct xkb_compose_table *t) {
    faults.table_unrefs++;
    xkb_compose_table_unref(t);
}
static xcb_xkb_per_client_flags_reply_t *test_flags_reply(xcb_connection_t *c,
    xcb_xkb_per_client_flags_cookie_t ck, xcb_generic_error_t **err) {
    xcb_xkb_per_client_flags_reply_t *r = xcb_xkb_per_client_flags_reply(c, ck, err);
    if (faults.flags == 1) { free(r); return NULL; }
    if (r && faults.flags == 2) r->supported = 0;
    if (r && faults.flags == 3) r->value = 0;
    if (faults.flags == 4) {
        free(r);
        xcb_xkb_per_client_flags_cookie_t bad = xcb_xkb_per_client_flags(c, UINT16_MAX, 1, 1, 0, 0, 0);
        return xcb_xkb_per_client_flags_reply(c, bad, err);
    }
    return r;
}
static xcb_generic_event_t *test_queued_event(xcb_connection_t *c) {
    if (faults.raw_count || faults.raw_release) {
        xcb_generic_event_t *e = calloc(1, sizeof *e);
        if (!e) return NULL;
        if (faults.raw_count) { faults.raw_count--; e->response_type = XCB_PROPERTY_NOTIFY; }
        else {
            faults.raw_release = false;
            xcb_key_release_event_t release = { .response_type = XCB_KEY_RELEASE, .detail = 38 };
            memcpy(e, &release, sizeof release);
        }
        return e;
    }
    if (faults.raw_focus) {
        faults.raw_focus = false;
        xcb_focus_out_event_t f = { .response_type = XCB_FOCUS_OUT,
            .mode = XCB_NOTIFY_MODE_NORMAL, .detail = XCB_NOTIFY_DETAIL_NONLINEAR };
        xcb_generic_event_t *e = calloc(1, sizeof *e);
        if (e) memcpy(e, &f, sizeof f);
        return e;
    }
    if (!faults.raw_stream) return xcb_poll_for_queued_event(c);
    /* A perpetually replenished raw queue, independent of server scheduling. */
    xcb_generic_event_t *e = calloc(1, sizeof *e);
    if (e) e->response_type = XCB_PROPERTY_NOTIFY;
    return e;
}
static bool test_clip_poll(plat *p) {
    if (faults.inject_poll && --faults.inject_poll == 0) {
        plat_event e = { .kind = PLAT_EV_KEY, .code = 38, .press = true };
        (void)x11_push_event(p, &e);
        faults.raw_focus = true;
        return false;
    }
    return x11_clip_poll(p);
}
#define x11_clip_poll test_clip_poll
#define xcb_poll_for_queued_event test_queued_event
#define calloc test_calloc
#define xkb_compose_state_new test_compose_new
#define xkb_compose_table_unref test_table_unref
#define xcb_xkb_per_client_flags_reply test_flags_reply
#include "../src/x11/input.c"
#include "../src/x11/x11.c"
#undef x11_clip_poll
#undef xcb_poll_for_queued_event
#undef calloc
#undef xkb_compose_state_new
#undef xkb_compose_table_unref
#undef xcb_xkb_per_client_flags_reply

static void *test_calloc(size_t n, size_t size) {
    if (faults.input_alloc && n == 1 && size == sizeof(x11_input)) return NULL;
    /* Fundamental-alignment allocators cannot own these extended types. */
    if (faults.extended_alloc && size == sizeof(x11_runtime)) return NULL;
    return calloc(n, size);
}
static int failed;
#define CHECK(c, label) do { if (!(c)) { fprintf(stderr, "x11_order_test: FAIL %s\n", label); failed = 1; } } while (0)

typedef struct sink {
    plat *p;
    unsigned keys, chars, clips, wheels, keymaps;
    uint8_t last_text, text[16];
    unsigned text_count;
    unsigned n;
    plat_ev_kind kinds[16];
    bool sustain;
    unsigned blinks, works;
    bool request_on_key, quit_on_event;
} sink;
static void on_event(void *ud, const plat_event *e) {
    sink *s = ud;
    if (e->kind == PLAT_EV_KEY) {
        s->keys++;
        if (e->press && e->utf8_len) {
            s->chars++; s->last_text = e->utf8[0];
            if (s->text_count < 16) s->text[s->text_count++] = e->utf8[0];
        }
    }
    if (e->kind == PLAT_EV_CLIPBOARD) s->clips++;
    if (e->kind == PLAT_EV_WHEEL) s->wheels++;
    if (e->kind == PLAT_EV_KEYMAP) s->keymaps++;
    if (s->n < 16) s->kinds[s->n++] = e->kind;
    if (s->request_on_key && e->kind == PLAT_EV_KEY && e->press)
        CHECK(plat_clip_request(s->p, PLAT_CLIP_PRIMARY) == PLAT_OK, "local request from key callback");
    if (s->sustain) (void)x11_push_event(s->p, e);
    if (s->quit_on_event) plat_quit(s->p);
}
static void blink(void *ud) { ((sink *)ud)->blinks++; }
static void work(void *ud) {
    sink *s = ud;
    uint64_t v;
    if (read(x11_private(s->p)->work_fd, &v, sizeof v) == (ssize_t)sizeof v) s->works++;
}
static bool barrier(xcb_connection_t *c) {
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
    bool ok = r != NULL; free(r); return ok;
}
static bool open_plat(plat *p) {
    plat_config cfg = { "order test", 100, 100, false, NULL, 0 };
    int rc = plat_init(p, &cfg);
    CHECK(rc == PLAT_OK, "fixture initialization");
    if (rc != PLAT_OK) return false;
    x11_clip_set_limits(p, 0, 0, UINT32_MAX);
    plat_set_repeat(p, 0, 0);
    sink s = { .p = p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    plat_run_for(p, &cb, 5);
    return true;
}
static void send_key(plat *p, bool press) {
    xcb_key_press_event_t e = { .response_type = press ? XCB_KEY_PRESS : XCB_KEY_RELEASE,
                              .detail = 38, .event = x11_private(p)->win, .same_screen = 1 };
    xcb_send_event(x11_private(p)->conn, 0, x11_private(p)->win, press ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE,
                   (const char *)&e);
}
static void test_burst(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    for (unsigned i = 0; i < 129; i++) { send_key(&p, true); send_key(&p, false); }
    CHECK(barrier(x11_private(&p)->conn), "burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.keys == 258 && s.chars == 129 && IN(&p)->q_dropped == 0, "§2 raw burst preserves every edit");
    CHECK(plat_clip_set(&p, PLAT_CLIP_PRIMARY, "local", 5) == PLAT_OK, "burst local set");
    CHECK(barrier(x11_private(&p)->conn), "burst local barrier");
    plat_run_for(&p, &cb, 10);
    s.keys = s.chars = s.clips = 0; s.request_on_key = true;
    for (unsigned i = 0; i < 129; i++) { send_key(&p, true); send_key(&p, false); }
    CHECK(barrier(x11_private(&p)->conn), "completion burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.chars == 129 && s.clips == 129 && IN(&p)->q_dropped == 0,
          "§2 raw burst and callback local completions preserve exact counts");
    s.request_on_key = false;
    /* Core fallback motion must not fill the ring ahead of a later edit. */
    void *saved_xi = x11_private(&p)->xi; x11_private(&p)->xi = NULL;
    for (unsigned i = 0; i < X11_QUEUE_CAP + 1; i++) {
        xcb_motion_notify_event_t m = { .response_type = XCB_MOTION_NOTIFY, .event = x11_private(&p)->win };
        xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_POINTER_MOTION, (const char *)&m);
    }
    send_key(&p, true); send_key(&p, false);
    CHECK(barrier(x11_private(&p)->conn), "motion burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.chars == 130 && IN(&p)->q_dropped == 0, "§2 motion burst cannot discard later edit");
    x11_private(&p)->xi = saved_xi;
    plat_shutdown(&p);
}
static void test_local_overflow(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    /* Callback-produced local completions compete with raw input in the same ring. */
    CHECK(plat_clip_set(&p, PLAT_CLIP_PRIMARY, "local", 5) == PLAT_OK, "local set");
    CHECK(barrier(x11_private(&p)->conn), "local barrier");
    plat_run_for(&p, &cb, 10);
    s.clips = 0;
    for (unsigned i = 0; i < X11_QUEUE_CAP; i++) {
        plat_event e = { .kind = PLAT_EV_KEY, .code = i };
        CHECK(x11_push_event(&p, &e), "fill queue");
    }
    int rc = plat_clip_request(&p, PLAT_CLIP_PRIMARY);
    plat_run_for(&p, &cb, 10);
    CHECK(rc != PLAT_OK || s.clips == 1, "§2 accepted local completion is never lost");
    plat_shutdown(&p);
}
static void test_order(void) {
    const plat_ev_kind second[] = { PLAT_EV_FOCUS, PLAT_EV_CLOSE, PLAT_EV_RESIZE, PLAT_EV_EXPOSE };
    for (unsigned i = 0; i < sizeof second / sizeof second[0]; i++) {
        plat p;
        if (!open_plat(&p)) return;
        sink s = { .p = &p };
        plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
        if (second[i] == PLAT_EV_RESIZE) {
            xcb_button_press_event_t b = { .response_type = XCB_BUTTON_PRESS, .detail = 1, .event = x11_private(&p)->win };
            xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_BUTTON_PRESS, (const char *)&b);
        } else send_key(&p, true);
        if (second[i] == PLAT_EV_FOCUS) {
            xcb_focus_out_event_t f = { .response_type = XCB_FOCUS_OUT, .event = x11_private(&p)->win,
                                       .mode = XCB_NOTIFY_MODE_NORMAL, .detail = XCB_NOTIFY_DETAIL_NONLINEAR };
            xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_FOCUS_CHANGE, (const char *)&f);
        } else if (second[i] == PLAT_EV_CLOSE) {
            xcb_client_message_event_t c = { .response_type = XCB_CLIENT_MESSAGE, .format = 32,
                                            .window = x11_private(&p)->win, .type = x11_private(&p)->wm_protocols };
            c.data.data32[0] = x11_private(&p)->wm_delete;
            xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, 0, (const char *)&c);
        } else if (second[i] == PLAT_EV_RESIZE) {
            xcb_configure_notify_event_t c = { .response_type = XCB_CONFIGURE_NOTIFY, .event = x11_private(&p)->win,
                                              .window = x11_private(&p)->win, .width = 101, .height = 101 };
            xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&c);
        } else {
            xcb_expose_event_t e = { .response_type = XCB_EXPOSE, .window = x11_private(&p)->win };
            xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_EXPOSURE, (const char *)&e);
        }
        CHECK(barrier(x11_private(&p)->conn), "order barrier");
        plat_run_for(&p, &cb, 20);
        CHECK(s.n >= 2 && s.kinds[0] == (second[i] == PLAT_EV_RESIZE ? PLAT_EV_BUTTON : PLAT_EV_KEY) &&
              s.kinds[1] == second[i], "§3 preceding input reaches callback first");
        plat_shutdown(&p);
    }
}
static void test_poll_order(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    faults.inject_poll = 2; /* completion is enqueued by the in-drain reply poll */
    plat_run_for(&p, &cb, 10);
    CHECK(s.n == 2 && s.kinds[0] == PLAT_EV_KEY && s.kinds[1] == PLAT_EV_FOCUS,
          "§3 reply polling preserves preceding queued input order");
    plat_shutdown(&p);
}
static void run_slices(bool raw_stream) {
    /* Parent is an independent watchdog: a broken drain cannot defeat it. */
    int fds[2];
    if (pipe(fds) != 0) { CHECK(false, "watchdog pipe"); return; }
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); CHECK(false, "watchdog fork"); return; }
    if (pid == 0) {
        close(fds[0]);
        plat p;
        if (!open_plat(&p)) _exit(2);
        x11_private(&p)->work_fd = eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);
        p.focused = true;
        plat_set_blink(&p, 1);
        sink s = { .p = &p, .sustain = !raw_stream };
        faults.raw_stream = raw_stream;
        plat_callbacks cb = { &s, on_event, blink, work, NULL };
        plat_event e = { .kind = PLAT_EV_CLIPBOARD };
        (void)x11_push_event(&p, &e);
        int rc = plat_run_for(&p, &cb, 80);
        char ok = rc == PLAT_OK && s.blinks && s.works ? 'y' : 'n';
        ssize_t sent = write(fds[1], &ok, 1);
        if (sent != 1) _exit(3);
        close(x11_private(&p)->work_fd);
        plat_shutdown(&p);
        _exit(0);
    }
    close(fds[1]);
    struct pollfd fd = { fds[0], POLLIN, 0 };
    char ok = 0;
    bool done = poll(&fd, 1, 2000) == 1 && read(fds[0], &ok, 1) == 1;
    if (!done) kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(fds[0]);
    CHECK(done && ok == 'y', raw_stream ? "§8 bounded raw drain services timers/work and returns" :
                                         "§8 bounded drain services timers/work and returns under sustained callbacks");
}
static void test_slices(void) { run_slices(false); run_slices(true); }
static void test_rescans(void) {
    for (unsigned mode = 0; mode < 2; mode++) {
        int fds[2];
        if (pipe(fds) != 0) { CHECK(false, "rescan pipe"); return; }
        pid_t pid = fork();
        if (pid < 0) { CHECK(false, "rescan fork"); close(fds[0]); close(fds[1]); return; }
        if (pid == 0) {
            close(fds[0]);
            plat p;
            if (!open_plat(&p)) _exit(2);
            send_key(&p, true);
            if (!barrier(x11_private(&p)->conn)) _exit(3);
            xcb_connection_t *peer = xcb_connect(NULL, NULL);
            xcb_grab_server(peer);
            if (!barrier(peer)) _exit(4);
            sink s = { .p = &p };
            plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
            if (mode == 0) {
                (void)drain(&p, &cb, 0);
                xcb_generic_event_t e = { .response_type = x11_private(&p)->xkb_event };
                ((uint8_t *)&e)[1] = XCB_XKB_MAP_NOTIFY;
                dispatch(&p, &cb, &e);
            } else {
                xcb_ge_generic_event_t e = { .response_type = XCB_GE_GENERIC,
                    .extension = XI(&p)->opcode, .event_type = 1, .length = 0 };
                dispatch(&p, &cb, (xcb_generic_event_t *)&e);
            }
            int rc = plat_run_for(&p, &cb, 20);
            char ok = rc == PLAT_OK && s.keys == 1 ? 'y' : 'n';
            /* Shutdown itself must cancel blocked background I/O. */
            plat_shutdown(&p);
            ssize_t sent = write(fds[1], &ok, 1);
            xcb_ungrab_server(peer); xcb_flush(peer); xcb_disconnect(peer);
            _exit(sent == 1 ? 0 : 5);
        }
        close(fds[1]);
        struct pollfd fd = { fds[0], POLLIN, 0 };
        char ok = 0;
        bool done = poll(&fd, 1, 2000) == 1 && read(fds[0], &ok, 1) == 1;
        if (!done) kill(pid, SIGKILL);
        waitpid(pid, NULL, 0); close(fds[0]);
        CHECK(done && ok == 'y', mode == 0 ? "§12 delayed keymap reply cannot block input/shutdown" :
                                           "§12 delayed device reply cannot block input/shutdown");
    }
}
static void test_rescan_publish(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    struct xkb_keymap *old = xkb_keymap_ref(IN(&p)->keymap);
    xcb_generic_event_t e = { .response_type = x11_private(&p)->xkb_event };
    ((uint8_t *)&e)[1] = XCB_XKB_MAP_NOTIFY;
    for (unsigned i = 0; i < 4; i++) dispatch(&p, &cb, &e);
    for (unsigned i = 0; i < 100 && !s.keymaps; i++) plat_run_for(&p, &cb, 5);
    CHECK(s.keymaps == 1 && IN(&p)->keymap != old && !IN(&p)->rep_active,
          "§12 worker publishes one validated replacement for mapping burst");
    xkb_keymap_unref(old);
    plat_shutdown(&p);
}
/* edit-cx8: a server keymap change (setxkbmap from any client on a shared
 * display) rebuilds the keymap on this worker while the UI thread may have a
 * law-2 typing window open. The process-wide guard must not count that
 * library construction. Only the worker job runs inside the window: it is
 * submitted directly, awaited on the pool eventfd, adopted afterwards. */
static void test_rebuild_guard(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    x11_runtime *rt = IN(&p)->runtime;
    struct xkb_keymap *old = xkb_keymap_ref(IN(&p)->keymap);
    xcb_generic_event_t e = { .response_type = x11_private(&p)->xkb_event };
    ((uint8_t *)&e)[1] = XCB_XKB_MAP_NOTIFY;
    edit_malloc_guard_begin();
    dispatch(&p, &cb, &e);                /* UI boundary + kick inside the window */
    struct pollfd pfd = { work_pool_eventfd(&rt->pool), POLLIN, 0 };
    int ready = 0;
    /* 200 x 10 ms = 2 s (G, liveness only) for one local Xvfb keymap fetch. */
    for (unsigned i = 0; i < 200 && ready <= 0; i++) ready = poll(&pfd, 1, 10);
    size_t allocs = edit_malloc_guard_end();
    for (unsigned i = 0; i < 100 && !s.keymaps; i++) plat_run_for(&p, &cb, 5);
    printf("x11 keymap rebuild: listener=%d ready=%d counted_allocations=%zu guard=%d\n",
           rt->listener.epoch != 0, ready, allocs, edit_malloc_guard_active());
    CHECK(rt->listener.epoch && ready > 0 && s.keymaps == 1 && IN(&p)->keymap != old,
          "edit-cx8 worker rebuild published a replacement keymap");
    CHECK(!edit_malloc_guard_active() || allocs == 0,
          "edit-cx8 keymap worker library allocations outside the law-2 count");
    xkb_keymap_unref(old);
    plat_shutdown(&p);
}
static void test_repeat_order(void) {
    for (unsigned focus = 0; focus < 2; focus++) {
        plat p;
        if (!open_plat(&p)) return;
        p.focused = true;
        plat_set_repeat(&p, 0, 100);
        xcb_key_press_event_t key = { .detail = 38 };
        plat_event ev;
        CHECK(x11_input_key(IN(&p), &key, true, trace_now_ns(), &ev), "§22 start repeat");
        IN(&p)->rep_next_ns = trace_now_ns() - UINT64_C(1000000);
        rearm_repeat(&p);
        faults.raw_count = 256;
        faults.raw_release = !focus;
        faults.raw_focus = focus != 0;
        sink s = { .p = &p };
        plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
        plat_run_for(&p, &cb, 20);
        CHECK(s.chars == 0 && !IN(&p)->rep_active,
              "§22 queued release/focus beyond first slice precedes repeat");
        plat_shutdown(&p);
    }
}
/* Stop the real listener so a test can fill the snapshot slots itself. */
static void stop_listener(x11_runtime *rt) {
    work_cancel(&rt->pool, rt->listener);
    keyboard_kick(rt);                    /* wake its poll; it sees cancellation */
    /* 200 x 10 ms = 2 s (G, liveness only, as test_rebuild_guard). */
    for (unsigned i = 0; i < 200 && !work_handle_finished(&rt->pool, rt->listener); i++) {
        struct pollfd none = { -1, 0, 0 };
        (void)poll(&none, 1, 10);
    }
    CHECK(work_handle_finished(&rt->pool, rt->listener), "§21 listener stops for injected snapshots");
}
/* Model one listener publication plus its UI drain callback. */
static bool inject_snapshot(x11_runtime *rt, const char *layout, bool labeled, uint32_t time, uint32_t kicks) {
    struct xkb_rule_names names = { .layout = layout };
    keyboard_result r = { .labeled = labeled, .label_time = time, .kicks = kicks };
    r.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    r.keymap = r.ctx ? xkb_keymap_new_from_names(r.ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    r.state = r.keymap ? x11_input_prepare_state(r.keymap) : NULL;
    if (!r.state) { keyboard_result_destroy(&r); return false; }
    rt->slots[rt->produced++ % X11_KEYMAP_SLOTS] = r;
    work_msg msg = { .kind = KEYBOARD_MSG_SNAPSHOT };
    keyboard_ready(&msg, rt);
    return true;
}
static void test_map_boundary(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    x11_runtime *rt = IN(&p)->runtime;
    stop_listener(rt);
    /* A pre-boundary key has already reached the translated queue. */
    xcb_key_press_event_t key = { .response_type = XCB_KEY_PRESS, .detail = 29, .event = x11_private(&p)->win };
    dispatch(&p, &cb, (xcb_generic_event_t *)&key);
    xcb_generic_event_t notification = { .response_type = x11_private(&p)->xkb_event };
    ((uint8_t *)&notification)[1] = XCB_XKB_MAP_NOTIFY;
    dispatch(&p, &cb, &notification);
    /* No snapshot yet: retain the following raw release/press in XCB order. */
    key.response_type = XCB_KEY_RELEASE;
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&key);
    key.response_type = XCB_KEY_PRESS;
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&key);
    CHECK(barrier(x11_private(&p)->conn), "§21 staged keys barrier");
    plat_run_for(&p, &cb, 5);
    CHECK(s.keys == 1, "§21 later raw keys wait for matching map publication");
    /* Unlabelled snapshot fetched after this boundary's kick. */
    CHECK(inject_snapshot(rt, "de", false, 0, rt->want_kicks), "§21 replacement German keymap");
    /* Replay raw keys only after the map notification callback. */
    plat_run_for(&p, &cb, 5);
    CHECK(s.keys == 3 && s.keymaps == 1, "§21 staged transitions replay after KEYMAP");
    CHECK(s.last_text == 'z', "§21 staged key uses published German map");
    plat_event translated = {0};
    x11_translate(IN(&p), 29, 0, &translated);
    CHECK(translated.utf8_len == 1 && translated.utf8[0] == 'z', "§21 German Y position translates as z");
    plat_shutdown(&p);
}
/* edit-q0g-s8, display-free: two map notifications, each followed by a key,
 * reach the UI in one drain after the worker already captured both server maps
 * (plus an older snapshot). Each key must use its own generation's map. The
 * oracle for each layout is an independently compiled map. */
static void history_key(plat *p, xkb_keycode_t code, const char *layout, const char *label) {
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = { .layout = layout };
    struct xkb_keymap *km = ctx ? xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    x11_input oracle;
    bool ok = km && x11_input_init(&oracle, km, NULL) == 0;
    CHECK(ok, "§21 independent oracle map");
    plat_event expected = {0}, got = {0};
    if (ok) { x11_translate(&oracle, code, 0, &expected); x11_input_destroy(&oracle); }
    else if (km) xkb_keymap_unref(km);
    if (ctx) xkb_context_unref(ctx);
    xcb_key_press_event_t key = { .detail = (uint8_t)code };
    CHECK(x11_input_key(IN(p), &key, true, trace_now_ns(), &got), "§21 key translated");
    (void)x11_input_key(IN(p), &key, false, trace_now_ns(), &(plat_event){0});
    printf("x11_order_test: map_history %s layout=%s expected=%.*s actual=%.*s\n", label, layout,
           (int)expected.utf8_len, expected.utf8, (int)got.utf8_len, got.utf8);
    CHECK(ok && got.utf8_len == expected.utf8_len && !memcmp(got.utf8, expected.utf8, expected.utf8_len), label);
}
static unsigned history_keymaps(plat *p) {
    unsigned n = 0;
    plat_event ev;
    while (x11_q_pop(IN(p), &ev)) n += ev.kind == PLAT_EV_KEYMAP;
    return n;
}
static void test_map_history(void) {
    x11_plat native = {.repeat_fd = -1};
    plat p = {.backend_state = &native};
    x11_input in;
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names us = { .layout = "us" };
    struct xkb_keymap *km = ctx ? xkb_keymap_new_from_names(ctx, &us, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    int rc = km ? x11_input_init(&in, km, NULL) : -1;
    x11_runtime *rt = rc == 0 ? aligned_alloc(_Alignof(x11_runtime), sizeof *rt) : NULL;
    CHECK(rt != NULL, "§21 display-free fixture");
    if (!rt) {
        if (rc == 0) x11_input_destroy(&in); else if (km) xkb_keymap_unref(km);
        if (ctx) xkb_context_unref(ctx);
        return;
    }
    memset(rt, 0, sizeof *rt);
    in.ctx = ctx;
    rt->owner = &p;
    rt->kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(rt->kick_fd >= 0 && work_pool_init(&rt->pool, 1, 0) == 0, "§21 display-free pool (no listener)");
    in.runtime = rt;
    x11_private(&p)->in = &in;
    /* AD06 is the physical Y position (US y, German z). */
    xkb_keycode_t code = xkb_keymap_key_by_name(km, "AD06");
    CHECK(code <= UINT8_MAX, "§21 fixture key fits core event detail");
    /* Server times (ms): an older change at 50, then the two in this drain. */
    CHECK(inject_snapshot(rt, "fr", true, 50, 0), "§21 older snapshot");
    CHECK(inject_snapshot(rt, "de", true, 100, 0), "§21 first-switch snapshot");
    CHECK(inject_snapshot(rt, "us", true, 200, 0), "§21 second-switch snapshot");
    request_keymap(&p, 100);
    poll_rescans(&p);
    CHECK(!rt->map_blocked && history_keymaps(&p) == 1, "§21 first boundary adopts its own map");
    history_key(&p, code, "de", "§21 key after first switch uses the first map");
    request_keymap(&p, 200);
    poll_rescans(&p);
    CHECK(!rt->map_blocked && history_keymaps(&p) == 1, "§21 second boundary adopts its own map");
    history_key(&p, code, "us", "§21 key after second switch uses the second map");
    CHECK(rt->ready == 0, "§21 older snapshot discarded, adopted slots released");
    /* Protocol limit: the first switch was replaced before the worker fetched
     * (only the 300+ map exists). Best available map, one KEYMAP, and the next
     * boundary it already covers resolves without waiting. */
    CHECK(inject_snapshot(rt, "de", true, 400, 0), "§21 newest-only snapshot");
    request_keymap(&p, 300);
    poll_rescans(&p);
    CHECK(!rt->map_blocked && history_keymaps(&p) == 1, "§21 lost generation uses newest captured map");
    request_keymap(&p, 400);
    poll_rescans(&p);
    CHECK(!rt->map_blocked && history_keymaps(&p) == 0, "§21 covered boundary resolves without a fetch");
    history_key(&p, code, "de", "§21 key after covered boundary");
    /* A boundary the listener never saw (synthetic) is settled by its kick. */
    request_keymap(&p, 400);
    rt->cur_valid = false;
    poll_rescans(&p);
    CHECK(rt->map_blocked, "§21 boundary waits for its kicked fetch");
    CHECK(inject_snapshot(rt, "us", false, 0, rt->want_kicks), "§21 kicked snapshot");
    poll_rescans(&p);
    CHECK(!rt->map_blocked && history_keymaps(&p) == 1, "§21 kicked fetch settles the boundary");
    history_key(&p, code, "us", "§21 key after kicked boundary");
    work_pool_shutdown(&rt->pool);
    for (size_t i = 0; i < X11_KEYMAP_SLOTS; i++) keyboard_result_destroy(&rt->slots[i]);
    close(rt->kick_fd);
    free(rt);
    in.runtime = NULL;
    x11_input_destroy(&in);
}
/* edit-riu: snapshot the actual server map, including custom mappings. Rules
 * names alone (setxkbmap -query) cannot reconstruct an xmodmap/xkbcomp edit. */
static struct {
    char *display;
    char path[sizeof "/tmp/edit-riu-keymap-XXXXXX.xkm"];
    xcb_connection_t *conn;
    xcb_window_t root;
    xcb_atom_t rules_atom;
    xcb_get_property_reply_t *rules;
    bool file_created, dirty;
} saved_map;

static bool run_x_tool(const char *const args[]) {
    pid_t child = fork();
    if (child < 0) return false;
    if (!child) {
        execv(args[0], (char *const *)args);
        _exit(EXIT_FAILURE);
    }
    int status = 0;
    pid_t done;
    do { done = waitpid(child, &status, 0); } while (done < 0 && errno == EINTR);
    return done == child && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}
static void discard_saved_map(void) {
    if (saved_map.conn) xcb_disconnect(saved_map.conn);
    if (saved_map.file_created) unlink(saved_map.path);
    free(saved_map.rules);
    free(saved_map.display);
    memset(&saved_map, 0, sizeof saved_map);
}
static void restore_saved_map(void) {
    if (saved_map.dirty) {
        const char *const args[] = { "/usr/bin/xkbcomp", "-w", "0", saved_map.path,
                                    saved_map.display, NULL }; /* -w 0 disables warnings. */
        CHECK(run_x_tool(args), "§21 restore original server keymap");
        /* xkbcomp restores the map, but not setxkbmap's root-window metadata.
         * Preserve even an absent property so later clients see the old rules. */
        xcb_void_cookie_t cookie;
        if (saved_map.rules->type == XCB_ATOM_NONE) {
            cookie = xcb_delete_property_checked(saved_map.conn, saved_map.root, saved_map.rules_atom);
        } else {
            uint32_t units = (uint32_t)xcb_get_property_value_length(saved_map.rules) /
                             (saved_map.rules->format / CHAR_BIT);
            cookie = xcb_change_property_checked(saved_map.conn, XCB_PROP_MODE_REPLACE, saved_map.root,
                saved_map.rules_atom, saved_map.rules->type, saved_map.rules->format, units,
                xcb_get_property_value(saved_map.rules));
        }
        CHECK(init_request_ok(saved_map.conn, cookie), "§21 restore original keymap rules property");
    }
    discard_saved_map();
}
static bool save_server_map(void) {
    /* xvfb_start() has already selected DISPLAY; plat_init() and its keyboard
     * worker both use xcb_connect(NULL, ...), hence this exact environment. */
    const char *display = getenv("DISPLAY");
    CHECK(display && *display, "§21 selected display exists");
    if (!display || !*display) return false;
    saved_map.display = strdup(display);
    CHECK(saved_map.display != NULL, "§21 save selected display");
    if (!saved_map.display) return false;
    const char *const query[] = { "/usr/bin/setxkbmap", "-display", saved_map.display, "-query", NULL };
    if (!run_x_tool(query)) {
        printf("x11_order_test: SKIP §21 live server maps (setxkbmap unavailable on %s)\n", display);
        goto unavailable;
    }
    int screen_number = 0;
    saved_map.conn = xcb_connect(saved_map.display, &screen_number);
    CHECK(!xcb_connection_has_error(saved_map.conn), "§21 snapshot display connection");
    if (xcb_connection_has_error(saved_map.conn)) goto unavailable;
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(saved_map.conn));
    while (screen_number-- > 0 && screens.rem) xcb_screen_next(&screens);
    CHECK(screens.rem != 0, "§21 snapshot screen exists");
    if (!screens.rem) goto unavailable;
    saved_map.root = screens.data->root;
    saved_map.rules_atom = atom(saved_map.conn, "_XKB_RULES_NAMES");
    CHECK(saved_map.rules_atom != XCB_ATOM_NONE, "§21 keymap rules atom");
    if (!saved_map.rules_atom) goto unavailable;
    saved_map.rules = xcb_get_property_reply(saved_map.conn, xcb_get_property(saved_map.conn, 0,
        saved_map.root, saved_map.rules_atom, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT32_MAX), NULL);
    CHECK(saved_map.rules && !saved_map.rules->bytes_after &&
          (saved_map.rules->type == XCB_ATOM_NONE || saved_map.rules->format == CHAR_BIT),
          "§21 snapshot complete keymap rules property");
    if (!saved_map.rules || saved_map.rules->bytes_after ||
        (saved_map.rules->type != XCB_ATOM_NONE && saved_map.rules->format != CHAR_BIT)) goto unavailable;
    memcpy(saved_map.path, "/tmp/edit-riu-keymap-XXXXXX.xkm", sizeof saved_map.path);
    /* The suffix length comes from the compiled-keymap extension. */
    int fd = mkstemps(saved_map.path, (int)(sizeof ".xkm" - 1));
    CHECK(fd >= 0, "§21 snapshot temporary file");
    if (fd < 0) goto unavailable;
    saved_map.file_created = true;
    close(fd);
    const char *const snapshot[] = { "/usr/bin/xkbcomp", "-w", "0", "-xkm", saved_map.display,
                                    saved_map.path, NULL };
    if (!run_x_tool(snapshot)) {
        printf("x11_order_test: SKIP §21 live server maps (cannot save original map on %s)\n", display);
        goto unavailable;
    }
    /* Mark before the first change: a failed command can partially update XKB. */
    saved_map.dirty = true;
    return true;
unavailable:
    discard_saved_map();
    return false;
}
static bool set_layout(const char *layout) {
    const char *const args[] = { "/usr/bin/setxkbmap", "-display", saved_map.display,
                                "-layout", layout, "-variant", "", "-option", "", NULL };
    return run_x_tool(args);
}
static void test_live_map_boundary(void) {
    if (!save_server_map()) return;
    if (!set_layout("us")) {
        printf("x11_order_test: SKIP §21 live server maps (setxkbmap cannot set US on %s)\n", saved_map.display);
        goto restore;
    }
    plat p;
    if (!open_plat(&p)) goto restore;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    xcb_key_press_event_t key = { .response_type = XCB_KEY_PRESS, .detail = 29, .event = x11_private(&p)->win };
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&key);
    key.response_type = XCB_KEY_RELEASE;
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&key);
    CHECK(barrier(x11_private(&p)->conn), "§21 live before-map keys queued");
    bool changed = set_layout("de");
    CHECK(changed, "§21 actual server map change");
    if (!changed) goto shutdown;
    /* The UI connection owns this test-only grab: only worker snapshot I/O
     * stalls, while UI dispatch and its ordered raw queue remain runnable. */
    xcb_grab_server(x11_private(&p)->conn); CHECK(barrier(x11_private(&p)->conn), "§21 delay snapshot worker");
    key.response_type = XCB_KEY_PRESS;
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&key);
    key.response_type = XCB_KEY_RELEASE;
    xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&key);
    CHECK(barrier(x11_private(&p)->conn), "§21 live after-map keys queued");
    CHECK(plat_run_for(&p, &cb, 20) == PLAT_OK, "§21 delayed snapshot UI remains bounded");
    /* The listener may have captured German before the grab (edit-q0g-s8);
     * either way the post-change key is never translated with US. */
    CHECK((s.chars == 1 && s.last_text == 'y') || (s.chars == 2 && s.last_text == 'z'),
          "§21 post-change key waits for, or uses, the German map while worker delayed");
    xcb_ungrab_server(x11_private(&p)->conn); xcb_flush(x11_private(&p)->conn);
    for (unsigned i = 0; i < 200 && s.keys < 4; i++) plat_run_for(&p, &cb, 5);
    CHECK(s.keys == 4 && s.last_text == 'z' && s.keymaps > 0,
          "§21 immediate post-notification key uses actual German map");
shutdown:
    plat_shutdown(&p);
restore:
    restore_saved_map();
}
/* edit-q0g-s8: two server map replacements, each followed by a key, all
 * queued before one UI drain. Each key must translate with the map that was
 * current when the server queued it. The worker may observe a replacement
 * before the UI does; this fixture waits (liveness bound only) until a keymap
 * result is pending after the first replacement, i.e. the worker kept up with
 * the server. Changes that outrun the worker's fetch are the protocol limit
 * recorded in docs/decisions/edit-q0g-s8.md. */
static void test_map_generations_pending(void) {
    if (!save_server_map()) return;
    if (!set_layout("us")) {
        printf("x11_order_test: SKIP §21 live server maps (setxkbmap cannot set US on %s)\n", saved_map.display);
        goto restore;
    }
    plat p;
    if (!open_plat(&p)) goto restore;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    xcb_key_press_event_t key = { .detail = 29, .event = x11_private(&p)->win };
    for (unsigned i = 0; i < 2; i++) {
        bool changed = set_layout(i ? "us" : "de");
        CHECK(changed, "§21 consecutive server map change");
        if (!changed) goto shutdown;
        /* 200 x 10 ms = 2 s (G, liveness only, as test_rebuild_guard) for one
         * local Xvfb keymap fetch; a miss falls through to the text check. */
        x11_runtime *rt = IN(&p)->runtime;
        for (unsigned w = 0; !i && w < 200 && !work_mailbox_pending(&rt->pool); w++) {
            struct pollfd pfd = { work_pool_eventfd(&rt->pool), POLLIN, 0 };
            (void)poll(&pfd, 1, 10);
        }
        key.response_type = XCB_KEY_PRESS;
        xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&key);
        key.response_type = XCB_KEY_RELEASE;
        xcb_send_event(x11_private(&p)->conn, 0, x11_private(&p)->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&key);
        CHECK(barrier(x11_private(&p)->conn), "§21 generation keys queued");
    }
    for (unsigned i = 0; i < 200 && s.keys < 4; i++) plat_run_for(&p, &cb, 5);
    if (s.text_count != 2 || s.text[0] != 'z' || s.text[1] != 'y')
        fprintf(stderr, "§21 generations: expected=zy actual=%.*s keymaps=%u\n",
                (int)s.text_count, (const char *)s.text, s.keymaps);
    CHECK(s.text_count == 2 && s.text[0] == 'z' && s.text[1] == 'y',
          "§21 exact snapshots retain both successive server mapping generations");
shutdown:
    plat_shutdown(&p);
restore:
    restore_saved_map();
}
static void xi_wheel_button(plat *p, const plat_callbacks *cb, uint16_t source, bool emulated) {
    uint8_t raw[84] = {0};
    raw[0] = XCB_GE_GENERIC; raw[1] = XI(p)->opcode;
    uint32_t length = 12, detail = 5, time = 101, flags = emulated ? (1u << 16) : 0;
    uint16_t type = 4;
    memcpy(raw + 4, &length, 4); memcpy(raw + 8, &type, 2);
    memcpy(raw + 12, &time, 4); memcpy(raw + 16, &detail, 4);
    memcpy(raw + 56, &source, 2); /* XCB shifts wire offset 52 by four */
    memcpy(raw + 60, &flags, 4);
    dispatch(p, cb, (xcb_generic_event_t *)raw);
}
static void test_wheel_pending(void) {
    for (unsigned order = 0; order < 2; order++) {
        plat p;
        if (!open_plat(&p)) return;
        xi2 *x = XI(&p);
        CHECK(x != NULL, "§24 XI2 fixture");
        if (!x) { plat_shutdown(&p); return; }
        x->ndev = 1; x->dev[0].source = 12; x->dev[0].naxes = 1;
        x->dev[0].axis[0] = (xi2_axis){ .num = 3, .inc = 1, .last_ok = true };
        sink s = { .p = &p };
        plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
        uint8_t motion[96] = {0};
        motion[0] = XCB_GE_GENERIC; motion[1] = x->opcode;
        uint16_t type = 6, words = 1, source = 12; uint32_t length = 15, time = 100, value = 1;
        memcpy(motion + 4, &length, 4); memcpy(motion + 8, &type, 2); memcpy(motion + 12, &time, 4);
        memcpy(motion + 54, &words, 2); memcpy(motion + 56, &source, 2);
        motion[84] = 8; memcpy(motion + 88, &value, 4);
        if (!order) xi_wheel_button(&p, &cb, 12, true);
        dispatch(&p, &cb, (xcb_generic_event_t *)motion);
        if (order) xi_wheel_button(&p, &cb, 12, true);
        /* Distinct device at the same/near timestamp and a real discrete notch
         * on the smooth source must both survive, regardless of event order. */
        xi_wheel_button(&p, &cb, 99, false);
        xi_wheel_button(&p, &cb, 12, false);
        plat_run_for(&p, &cb, 5);
        CHECK(s.wheels == 3, "§24 smooth + independent notches counted, own emulation suppressed in both orders");
        uint8_t req[20]; xi2_build_select_events(x->opcode, req, x11_private(&p)->win);
        CHECK((req[16] & 0x30u) == 0x30u, "§24 XI button press/release selected");
        plat_shutdown(&p);
    }
}
static void test_xi_mask_pending(void) {
    xi2 x = { .opcode = 131 };
    uint8_t wire[96] = {0};
    wire[0] = XCB_GE_GENERIC; wire[1] = x.opcode;
    uint32_t length = 16; uint16_t type = 6, words = 1;
    memcpy(wire + 4, &length, 4); memcpy(wire + 8, &type, 2);
    memcpy(wire + 48, &words, 2); memcpy(wire + 50, &words, 2);
    wire[81] = 2; /* XI masks are indexed by button number: bit 9 */
    wire[84] = 1; /* pointer valuator */
    xi2_result r;
    CHECK(xi2_decode(&x, wire, sizeof wire, false, &r) && (r.buttons & (1u << 8)),
          "§19 XI motion preserves held button 9");
}
static void test_xi_group(void) {
    plat p;
    if (!open_plat(&p)) return;
    x11_input *in = IN(&p);
    struct xkb_rule_names names = { .layout = "us,de" };
    struct xkb_keymap *km = xkb_keymap_new_from_names(in->ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    CHECK(km && x11_input_set_keymap(in, km) == 0, "§23 multigroup fixture");
    uint8_t wire[96] = {0};
    wire[0] = XCB_GE_GENERIC; wire[1] = 131;
    uint16_t type = 6, words = 1;
    memcpy(wire + 8, &type, 2);
    memcpy(wire + 48, &words, 2); memcpy(wire + 50, &words, 2);
    wire[84] = 1;
    xi2 x = { .opcode = 131 }; xi2_result r;
    for (unsigned button = 6; button <= 7; button++) {
        wire[80] = (uint8_t)(1u << button);
        CHECK(xi2_decode(&x, wire, sizeof wire, false, &r), "§23 decode extended button");
        plat_event events[2];
        (void)x11_xi2_events(in, &r, trace_now_ns(), events);
        CHECK((in->rep_state & (3u << 13)) == 0, "§23 pointer buttons cannot change repeat group");
        plat_event translated = {0}; x11_translate(in, 29, in->rep_state, &translated);
        CHECK(translated.utf8_len == 1 && translated.utf8[0] == 'y', "§23 repeat remains in US group");
    }
    /* Button 32 requires a second XI mask word (XI bit zero is unused). */
    uint8_t large[104] = {0}; memcpy(large, wire, 80);
    words = 2; memcpy(large + 48, &words, 2); large[84] = 1; large[88] = 1;
    CHECK(xi2_decode(&x, large, sizeof large, false, &r) && (r.buttons & (UINT32_C(1) << 31)),
          "§23 XI motion preserves held button 32");
    plat_shutdown(&p);
}
static void test_repeat_config(void) {
    plat p;
    if (!open_plat(&p)) return;
    plat_set_repeat(&p, 0, UINT32_MAX);
    CHECK(IN(&p)->rep_rate_hz == X11_REPEAT_MAX_HZ, "§10 public setter clamps rate");
    xcb_key_press_event_t a = { .detail = 38 };
    plat_event e;
    x11_input_key(IN(&p), &a, true, trace_now_ns(), &e);
    uint64_t before = trace_now_ns();
    plat_set_repeat(&p, 250, 10);
    CHECK(x11_repeat_deadline(IN(&p)) >= before + UINT64_C(250000000),
          "§10 active configuration restarts delay");
    plat_set_repeat(&p, 0, 0);
    CHECK(!x11_repeat_deadline(IN(&p)), "§10 rate zero cancels active repeat");
    plat_shutdown(&p);
}
static void test_quit(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p, .quit_on_event = true };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    send_key(&p, true); send_key(&p, false);
    CHECK(barrier(x11_private(&p)->conn), "quit barrier");
    CHECK(plat_run_for(&p, &cb, -1) == PLAT_OK && s.keys == 1,
          "§8 quit stops dispatch before later callbacks");
    plat_shutdown(&p);
}
static void test_compose_failure(void) {
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names rn = { "evdev", "pc105", "us", "", "" };
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &rn, XKB_KEYMAP_COMPILE_NO_FLAGS);
    struct xkb_compose_table *ct = xkb_compose_table_new_from_locale(ctx, "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
    CHECK(km && ct, "compose fixture");
    x11_input in;
    faults.compose_alloc = true; faults.table_unrefs = 0;
    int rc = x11_input_init(&in, km, ct);
    faults.compose_alloc = false;
    CHECK(rc == -1 && faults.table_unrefs == 1 && !in.cstate && !in.ctab && !in.keymap,
          "§26 compose allocation failure rejects init and releases table");
    if (rc == 0) x11_input_destroy(&in); else xkb_keymap_unref(km);
    xkb_context_unref(ctx);
}
static void test_setup_failure(void) {
    for (unsigned mode = 0; mode < 5; mode++) {
        plat p;
        faults.input_alloc = mode == 0;
        faults.flags = mode;
        faults.table_unrefs = 0;
        plat_config cfg = { "setup failure", 100, 100, false, NULL, 0 };
        int rc = plat_init(&p, &cfg);
        unsigned unrefs = faults.table_unrefs;
        faults.input_alloc = false; faults.flags = 0;
        CHECK(rc == PLAT_ERR_FAIL, mode == 0 ? "§29 input allocation fails" : "§11 negotiation refusal rejects init");
        if (mode == 0) CHECK(unrefs == 1, "§29 input allocation failure releases compose table");
        if (rc == PLAT_OK) { x11_clip_set_limits(&p, 0, 0, UINT32_MAX); plat_shutdown(&p); }
    }
}
static void test_startup_trace(void) {
    trace_init(); CHECK(trace_thread_register() >= 0, "trace register");
    for (unsigned i = 0; i < 2; i++) {
        trace_reset();
        plat p;
        plat_config cfg = { "startup trace", 100, 100, false, NULL, i ? 0 : UINT64_C(123456789) };
        CHECK(plat_init(&p, &cfg) == PLAT_OK, "trace init");
        plat_map(&p);
        FILE *f = tmpfile();
        CHECK(f && trace_dump(f) == 0, "trace dump");
        rewind(f);
        trace_loaded d;
        CHECK(trace_fmt_load_dump(f, &d) == 0, "trace decode");
        unsigned count = 0;
        for (size_t j = 0; j < d.nrecs; j++) if (d.recs[j].ev == TRACE_T0_INGRESS) {
            count++;
            CHECK(d.recs[j].frame_id == 0 && d.recs[j].ns == cfg.exec_ns, "§30 exact supplied startup timestamp");
        }
        CHECK(count == (i ? 0u : 1u), "§30 startup records known exec time only");
        trace_fmt_dump_free(&d); fclose(f);
        x11_clip_set_limits(&p, 0, 0, UINT32_MAX); plat_shutdown(&p);
    }
}
static void test_alignment(void) {
    plat p;
    faults.extended_alloc = true;
    if (open_plat(&p)) {
        CHECK((uintptr_t)IN(&p)->runtime % _Alignof(x11_runtime) == 0,
              "§6 runtime extended alignment");
        plat_shutdown(&p);
    }
    faults.extended_alloc = false;
}
int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    if (!strcmp(which, "all") || !strcmp(which, "map_history")) {
        test_map_history();   /* display-free: runs before the private Xvfb */
        if (strcmp(which, "all")) {
            printf("x11_order_test: %s %s\n", which, failed ? "FAILED" : "ok");
            return failed;
        }
    }
    if (!xvfb_start()) { fprintf(stderr, "x11_order_test: FAIL no private Xvfb\n"); return 1; }
    /* Registered after xvfb_stop: the original map is restored before Xvfb
     * exits. Normal/failure paths restore explicitly so failure affects main. */
    if (atexit(restore_saved_map) != 0) { CHECK(false, "keymap cleanup registration"); return failed; }
    if (!strcmp(which, "all") || !strcmp(which, "alignment")) test_alignment();
    if (!strcmp(which, "all") || !strcmp(which, "burst")) test_burst();
    if (!strcmp(which, "local_overflow")) test_local_overflow();
    if (!strcmp(which, "all") || !strcmp(which, "order")) { test_order(); test_poll_order(); }
    if (!strcmp(which, "all") || !strcmp(which, "slices")) test_slices();
    if (!strcmp(which, "all") || !strcmp(which, "rescans")) { test_rescans(); test_rescan_publish(); test_rebuild_guard(); }
    if (!strcmp(which, "all") || !strcmp(which, "repeat_order")) test_repeat_order();
    if (!strcmp(which, "all") || !strcmp(which, "map_boundary")) { test_map_boundary(); test_live_map_boundary(); }
    if (!strcmp(which, "all") || !strcmp(which, "map_generations") ||
        !strcmp(which, "map_generations_pending")) test_map_generations_pending();
    if (!strcmp(which, "all") || !strcmp(which, "wheel_pending")) test_wheel_pending();
    if (!strcmp(which, "all") || !strcmp(which, "xi_mask_pending")) { test_xi_mask_pending(); test_xi_group(); }
    if (!strcmp(which, "all") || !strcmp(which, "repeat_config")) test_repeat_config();
    if (!strcmp(which, "all") || !strcmp(which, "quit")) test_quit();
    if (!strcmp(which, "all") || !strcmp(which, "compose")) test_compose_failure();
    if (!strcmp(which, "all") || !strcmp(which, "setup")) test_setup_failure();
    if (!strcmp(which, "all") || !strcmp(which, "trace")) test_startup_trace();
    printf("x11_order_test: %s %s\n", which, failed ? "FAILED" : "ok");
    return failed;
}
