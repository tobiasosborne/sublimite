/* P2.2c: reply waits can consume the fd while leaving events in XCB's queue.
 * The peer grabs the server to order injected events before the awaited reply.
 * Unmapped window, disabled timers and a bounded run avoid incidental wakeups. */
#include "x11/plat.h"
#include "trace/trace.h"
#include <xcb/xcb.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/eventfd.h>

#define DELIVERY_NS UINT64_C(5000000) /* (G) regression budget, not end-to-end G1 */

typedef enum trigger { KEY_REPLY, DIRECT_REPLY, BLINK_REPLY, WORK_REPLY, PREQUEUED_REPLY, CLIP_SET } trigger;

typedef struct fixture {
    plat p;
    xcb_connection_t *peer;
    int command[2], ready[2];
    bool triggered, failed, peer_failed, clip_ok;
    trigger mode;
    unsigned keys, closes;
    uint64_t returned_ns, delivered_ns;
    uint64_t set_ns;
} fixture;

static bool put_byte(int fd) {
    const char b = 'x';
    return write(fd, &b, 1) == 1;
}

static bool get_byte(int fd) {
    struct pollfd f = { fd, POLLIN, 0 };
    char b;
    return poll(&f, 1, 1000) == 1 && read(fd, &b, 1) == 1;
}

static bool barrier(xcb_connection_t *c) {
    xcb_get_input_focus_reply_t *r =
        xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
    bool ok = r != NULL;
    free(r);
    return ok;
}

static void send_key(fixture *f, uint8_t code, bool press) {
    xcb_key_press_event_t e;
    memset(&e, 0, sizeof e);
    e.response_type = press ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
    e.detail = code;
    e.event = f->p.win;
    e.same_screen = 1;
    xcb_send_event(f->peer, 0, f->p.win,
                   press ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE,
                   (const char *)&e);
}

static void *inject(void *ud) {
    fixture *f = ud;
    xcb_grab_server(f->peer);
    if (!barrier(f->peer) || !put_byte(f->ready[1]) || !get_byte(f->command[0])) {
        f->peer_failed = true;
        xcb_ungrab_server(f->peer);
        xcb_flush(f->peer);
        return NULL;
    }
    /* The UI has flushed its query and is entering the blocked reply wait.
     * Delay is fixture synchronization only; delivery timing starts after it. */
    struct timespec delay = { 0, 2000000 };
    nanosleep(&delay, NULL);
    send_key(f, 25, true);
    send_key(f, 25, false);
    xcb_client_message_event_t e;
    memset(&e, 0, sizeof e);
    e.response_type = XCB_CLIENT_MESSAGE;
    e.format = 32;
    e.window = f->p.win;
    e.type = f->p.wm_protocols;
    e.data.data32[0] = f->p.wm_delete;
    xcb_send_event(f->peer, 0, f->p.win, 0, (const char *)&e);
    /* Force all injected events to be written before releasing the first
     * connection's query. No peer requests or fd activity follow this. */
    (void)barrier(f->peer);
    xcb_ungrab_server(f->peer);
    xcb_flush(f->peer);
    return NULL;
}

static void maybe_done(fixture *f) {
    if (f->keys == 2 && f->closes == 1 && (f->mode != CLIP_SET || f->clip_ok)) {
        f->delivered_ns = trace_now_ns();
        plat_quit(&f->p);
    }
}

static void wait_in_callback(fixture *f) {
    if (f->triggered) return;
    f->triggered = true;
    pthread_t thread;
    if (pthread_create(&thread, NULL, inject, f) != 0) {
        f->failed = true;
        return;
    }
    if (!get_byte(f->ready[0])) {
        f->failed = true;
        (void)put_byte(f->command[1]);
        pthread_join(thread, NULL);
        return;
    }
    xcb_connection_t *c = f->p.conn;
    if (f->mode == CLIP_SET) {
        /* The server is still grabbed. A synchronous ownership query would
         * block until the peer's bounded command wait expires and ungrabs. */
        uint64_t start = trace_now_ns();
        if (plat_clip_set(&f->p, PLAT_CLIP_PRIMARY, "async", 5) != PLAT_OK) f->failed = true;
        f->set_ns = trace_now_ns() - start;
        if (!put_byte(f->command[1])) f->failed = true;
        if (plat_clip_request(&f->p, PLAT_CLIP_PRIMARY) != PLAT_OK) f->failed = true;
    } else {
        xcb_get_input_focus_cookie_t ck = xcb_get_input_focus(c);
        xcb_flush(c);
        if (!put_byte(f->command[1])) f->failed = true;
        xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(c, ck, NULL);
        if (!r) f->failed = true;
        free(r);
    }
    f->returned_ns = trace_now_ns();
    pthread_join(thread, NULL);
    struct pollfd fd = { xcb_get_file_descriptor(c), POLLIN, 0 };
    if (f->mode != CLIP_SET && poll(&fd, 1, 0) != 0) {
        fprintf(stderr, "FAIL: fixture left connection fd readable\n");
        f->failed = true;
    }
}

static void on_event(void *ud, const plat_event *ev) {
    fixture *f = ud;
    if (ev->kind == PLAT_EV_KEY && ev->code == 24 && ev->press) wait_in_callback(f);
    if (ev->kind == PLAT_EV_EXPOSE && f->mode == DIRECT_REPLY) wait_in_callback(f);
    if (ev->kind == PLAT_EV_KEY && ev->code == 25) f->keys++;
    if (ev->kind == PLAT_EV_CLOSE) f->closes++;
    if (ev->kind == PLAT_EV_CLIPBOARD && f->mode == CLIP_SET) {
        size_t n;
        const uint8_t *data = plat_clip_data(&f->p, &n);
        f->clip_ok = ev->code == 0 && data && n == 5 && memcmp(data, "async", 5) == 0;
        if (!f->clip_ok) f->failed = true;
    }
    maybe_done(f);
}

static void on_blink(void *ud) {
    fixture *f = ud;
    plat_set_blink(&f->p, 0);
    wait_in_callback(f);
}

static void on_work(void *ud) {
    fixture *f = ud;
    uint64_t count;
    if (read(f->p.work_fd, &count, sizeof count) != (ssize_t)sizeof count) f->failed = true;
    wait_in_callback(f);
}

static const char *power_tag(void) {
    static char status[32];
    FILE *fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!fp) return "unknown";
    char *r = fgets(status, sizeof status, fp);
    fclose(fp);
    return r && strncmp(status, "Discharging", 11) == 0 ? "bat" : "AC";
}

static int run_case(trigger mode, const char *name, const char *power) {
    fixture f;
    memset(&f, 0, sizeof f);
    f.mode = mode;
    plat_config cfg = { "x11 stall", 100, 100, false, -1, 0 };
    if (mode == WORK_REPLY) {
        cfg.work_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (cfg.work_eventfd < 0) return 1;
    }
    if (plat_init(&f.p, &cfg) != PLAT_OK) {
        fprintf(stderr, "FAIL: live display initialization\n");
        return 1;
    }
    f.peer = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(f.peer) || pipe(f.command) || pipe(f.ready)) return 1;
    plat_set_repeat(&f.p, 0, 0);
    /* Drain setup events and synchronize creation before starting the fixture. */
    plat_callbacks cb = { &f, on_event, on_blink, on_work, NULL };
    plat_run_for(&f.p, &cb, 5);
    if (!barrier(f.p.conn)) return 1;
    if (mode == PREQUEUED_REPLY) wait_in_callback(&f);
    else if (mode == WORK_REPLY) {
        uint64_t count = 1;
        if (write(f.p.work_fd, &count, sizeof count) != (ssize_t)sizeof count) f.failed = true;
    } else if (mode == BLINK_REPLY) {
        f.p.focused = true;
        plat_set_blink(&f.p, 1);
    } else {
        if (mode == DIRECT_REPLY) {
            xcb_expose_event_t e = { .response_type = XCB_EXPOSE, .window = f.p.win };
            xcb_send_event(f.peer, 0, f.p.win, XCB_EVENT_MASK_EXPOSURE, (const char *)&e);
        } else send_key(&f, 24, true);
        xcb_flush(f.peer);
    }
    int rc = plat_run_for(&f.p, &cb, 80); /* (G) watchdog: failure is a timeout, never a hang */
    uint64_t elapsed = mode == CLIP_SET ? f.set_ns : f.delivered_ns - f.returned_ns;
    bool ok = rc == PLAT_OK && !f.failed && !f.peer_failed && f.triggered && f.returned_ns &&
              f.delivered_ns >= f.returned_ns &&
              elapsed <= DELIVERY_NS && f.keys == 2 && f.closes == 1 &&
              (mode != CLIP_SET || f.clip_ok);
    if (ok) printf("x11_stall_test: %s %.3f ms (M)[%s], budget 5 ms (G)[%s]: ok\n",
                   name, (double)elapsed / 1e6, power, power);
    else fprintf(stderr, "x11_stall_test: FAIL %s stranded events: keys=%u close=%u; "
                         "bounded run timed out or exceeded 5 ms (G)[%s]\n", name, f.keys, f.closes, power);
    close(f.command[0]); close(f.command[1]); close(f.ready[0]); close(f.ready[1]);
    xcb_disconnect(f.peer);
    plat_shutdown(&f.p);
    if (cfg.work_eventfd >= 0) close(cfg.work_eventfd);
    return ok ? 0 : 1;
}

int main(void) {
    if (!getenv("DISPLAY") || !*getenv("DISPLAY")) {
        puts("x11_stall_test: no DISPLAY, skipped");
        return 0;
    }
    const char *power = power_tag(); /* stamp before measuring */
    int failed = 0;
    failed |= run_case(KEY_REPLY, "key callback reply", power);
    failed |= run_case(DIRECT_REPLY, "direct callback reply", power);
    failed |= run_case(BLINK_REPLY, "blink callback reply", power);
    failed |= run_case(WORK_REPLY, "work callback reply", power);
    failed |= run_case(PREQUEUED_REPLY, "queued before run", power);
    failed |= run_case(CLIP_SET, "clipboard set return", power);
    return failed;
}
