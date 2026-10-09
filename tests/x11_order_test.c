/* P2.2e regressions. Include the implementation only to inject setup failures
 * and inspect ordered dispatch without adding production test hooks. */
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

static struct {
    bool input_alloc, compose_alloc, raw_stream;
    unsigned table_unrefs, inject_poll;
    bool raw_focus;
    unsigned flags;
} faults;

static void *test_calloc(size_t n, size_t size) {
    if (faults.input_alloc && n == 1 && size == sizeof(x11_input)) return NULL;
    return calloc(n, size);
}
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

static int failed;
#define CHECK(c, label) do { if (!(c)) { fprintf(stderr, "x11_order_test: FAIL %s\n", label); failed = 1; } } while (0)

typedef struct sink {
    plat *p;
    unsigned keys, chars, clips, wheels, keymaps;
    unsigned n;
    plat_ev_kind kinds[16];
    bool sustain;
    unsigned blinks, works;
    bool request_on_key, quit_on_event;
} sink;
static void on_event(void *ud, const plat_event *e) {
    sink *s = ud;
    if (e->kind == PLAT_EV_KEY) { s->keys++; if (e->press && e->utf8_len) s->chars++; }
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
    if (read(s->p->work_fd, &v, sizeof v) == (ssize_t)sizeof v) s->works++;
}
static bool barrier(xcb_connection_t *c) {
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
    bool ok = r != NULL; free(r); return ok;
}
static bool open_plat(plat *p) {
    plat_config cfg = { "order test", 100, 100, false, -1, 0 };
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
                              .detail = 38, .event = p->win, .same_screen = 1 };
    xcb_send_event(p->conn, 0, p->win, press ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE,
                   (const char *)&e);
}
static void test_burst(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    for (unsigned i = 0; i < 129; i++) { send_key(&p, true); send_key(&p, false); }
    CHECK(barrier(p.conn), "burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.keys == 258 && s.chars == 129 && IN(&p)->q_dropped == 0, "§2 raw burst preserves every edit");
    CHECK(plat_clip_set(&p, PLAT_CLIP_PRIMARY, "local", 5) == PLAT_OK, "burst local set");
    CHECK(barrier(p.conn), "burst local barrier");
    plat_run_for(&p, &cb, 10);
    s.keys = s.chars = s.clips = 0; s.request_on_key = true;
    for (unsigned i = 0; i < 129; i++) { send_key(&p, true); send_key(&p, false); }
    CHECK(barrier(p.conn), "completion burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.chars == 129 && s.clips == 129 && IN(&p)->q_dropped == 0,
          "§2 raw burst and callback local completions preserve exact counts");
    s.request_on_key = false;
    /* Core fallback motion must not fill the ring ahead of a later edit. */
    void *saved_xi = p.xi; p.xi = NULL;
    for (unsigned i = 0; i < X11_QUEUE_CAP + 1; i++) {
        xcb_motion_notify_event_t m = { .response_type = XCB_MOTION_NOTIFY, .event = p.win };
        xcb_send_event(p.conn, 0, p.win, XCB_EVENT_MASK_POINTER_MOTION, (const char *)&m);
    }
    send_key(&p, true); send_key(&p, false);
    CHECK(barrier(p.conn), "motion burst barrier");
    plat_run_for(&p, &cb, 20);
    CHECK(s.chars == 130 && IN(&p)->q_dropped == 0, "§2 motion burst cannot discard later edit");
    p.xi = saved_xi;
    plat_shutdown(&p);
}
static void test_local_overflow(void) {
    plat p;
    if (!open_plat(&p)) return;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    /* Callback-produced local completions compete with raw input in the same ring. */
    CHECK(plat_clip_set(&p, PLAT_CLIP_PRIMARY, "local", 5) == PLAT_OK, "local set");
    CHECK(barrier(p.conn), "local barrier");
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
            xcb_button_press_event_t b = { .response_type = XCB_BUTTON_PRESS, .detail = 1, .event = p.win };
            xcb_send_event(p.conn, 0, p.win, XCB_EVENT_MASK_BUTTON_PRESS, (const char *)&b);
        } else send_key(&p, true);
        if (second[i] == PLAT_EV_FOCUS) {
            xcb_focus_out_event_t f = { .response_type = XCB_FOCUS_OUT, .event = p.win,
                                       .mode = XCB_NOTIFY_MODE_NORMAL, .detail = XCB_NOTIFY_DETAIL_NONLINEAR };
            xcb_send_event(p.conn, 0, p.win, XCB_EVENT_MASK_FOCUS_CHANGE, (const char *)&f);
        } else if (second[i] == PLAT_EV_CLOSE) {
            xcb_client_message_event_t c = { .response_type = XCB_CLIENT_MESSAGE, .format = 32,
                                            .window = p.win, .type = p.wm_protocols };
            c.data.data32[0] = p.wm_delete;
            xcb_send_event(p.conn, 0, p.win, 0, (const char *)&c);
        } else if (second[i] == PLAT_EV_RESIZE) {
            xcb_configure_notify_event_t c = { .response_type = XCB_CONFIGURE_NOTIFY, .event = p.win,
                                              .window = p.win, .width = 101, .height = 101 };
            xcb_send_event(p.conn, 0, p.win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&c);
        } else {
            xcb_expose_event_t e = { .response_type = XCB_EXPOSE, .window = p.win };
            xcb_send_event(p.conn, 0, p.win, XCB_EVENT_MASK_EXPOSURE, (const char *)&e);
        }
        CHECK(barrier(p.conn), "order barrier");
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
        p.work_fd = eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);
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
        close(p.work_fd);
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
            if (!barrier(p.conn)) _exit(3);
            xcb_connection_t *peer = xcb_connect(NULL, NULL);
            xcb_grab_server(peer);
            if (!barrier(peer)) _exit(4);
            sink s = { .p = &p };
            plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
            if (mode == 0) {
                xcb_generic_event_t e = { .response_type = p.xkb_event };
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
    xcb_generic_event_t e = { .response_type = p.xkb_event };
    ((uint8_t *)&e)[1] = XCB_XKB_MAP_NOTIFY;
    for (unsigned i = 0; i < 4; i++) dispatch(&p, &cb, &e);
    for (unsigned i = 0; i < 100 && !s.keymaps; i++) plat_run_for(&p, &cb, 5);
    CHECK(s.keymaps == 1 && IN(&p)->keymap != old && !IN(&p)->rep_active,
          "§12 worker publishes one validated replacement for mapping burst");
    xkb_keymap_unref(old);
    plat_shutdown(&p);
}
static void test_wheel_pending(void) {
    plat p;
    if (!open_plat(&p)) return;
    p.core_wheel = false; p.xi_scroll_ms = 100;
    sink s = { .p = &p };
    plat_callbacks cb = { &s, on_event, NULL, NULL, NULL };
    xcb_button_press_event_t b = { .response_type = XCB_BUTTON_PRESS, .detail = 5, .time = 101 };
    dispatch(&p, &cb, (xcb_generic_event_t *)&b);
    plat_run_for(&p, &cb, 5);
    CHECK(s.wheels == 1, "§18 independent legacy notch near smooth timestamp survives");
    plat_shutdown(&p);
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
    CHECK(barrier(p.conn), "quit barrier");
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
        plat_config cfg = { "setup failure", 100, 100, false, -1, 0 };
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
        plat_config cfg = { "startup trace", 100, 100, false, -1, i ? 0 : UINT64_C(123456789) };
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
int main(int argc, char **argv) {
    if (!xvfb_start()) { fprintf(stderr, "x11_order_test: FAIL no private Xvfb\n"); return 1; }
    const char *which = argc > 1 ? argv[1] : "all";
    if (!strcmp(which, "all") || !strcmp(which, "burst")) test_burst();
    if (!strcmp(which, "local_overflow")) test_local_overflow();
    if (!strcmp(which, "all") || !strcmp(which, "order")) { test_order(); test_poll_order(); }
    if (!strcmp(which, "all") || !strcmp(which, "slices")) test_slices();
    if (!strcmp(which, "all") || !strcmp(which, "rescans")) { test_rescans(); test_rescan_publish(); }
    if (!strcmp(which, "wheel_pending")) test_wheel_pending();
    if (!strcmp(which, "xi_mask_pending")) test_xi_mask_pending();
    if (!strcmp(which, "all") || !strcmp(which, "repeat_config")) test_repeat_config();
    if (!strcmp(which, "all") || !strcmp(which, "quit")) test_quit();
    if (!strcmp(which, "all") || !strcmp(which, "compose")) test_compose_failure();
    if (!strcmp(which, "all") || !strcmp(which, "setup")) test_setup_failure();
    if (!strcmp(which, "all") || !strcmp(which, "trace")) test_startup_trace();
    printf("x11_order_test: %s %s\n", which, failed ? "FAILED" : "ok");
    return failed;
}
