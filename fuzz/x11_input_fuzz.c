/* libFuzzer: arbitrary bytes into the X-server-free input decoders (P2.2): core key/button events, XI2 generic
 * events (wire and xcb layout), QueryDevice replies, and a repeat/clock schedule. Must never crash or trip ASan/UBSan. */
#include "x11/input.h"
#include "x11/xi2.h"
#include "x11/clip.h"
#include <stdlib.h>
#include <string.h>

static x11_input g_in;
static int g_ready;

static void setup(void) {
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names rn = { "evdev", "pc105", "de", "", "" };
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &rn, XKB_KEYMAP_COMPILE_NO_FLAGS);
    static const char compose[] = "<dead_acute> <e> : \"\xc3\xa9\" eacute\n";
    struct xkb_compose_table *ct = xkb_compose_table_new_from_buffer(ctx, compose, sizeof compose - 1, "C",
        XKB_COMPOSE_FORMAT_TEXT_V1, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!km || x11_input_init(&g_in, km, ct) != 0) __builtin_trap();
    g_in.ctx = ctx;
    g_ready = 1;
}

/* Queue interleavings model bounded ingress/delivery, including counter wrap.
 * No XI2/clipboard parser changes belong to this bead. */
static void fuzz_input_order(const uint8_t *data, size_t size) {
    x11_input in;
    memset(&in, 0, sizeof in);
    in.qh = in.qt = UINT32_MAX - X11_QUEUE_CAP / 2;
    uint32_t produced = 0, delivered = 0;
    for (size_t i = 0; i < size; i++) {
        plat_event ev;
        if ((data[i] & 1u) || in.qt - in.qh == X11_QUEUE_CAP) {
            if (x11_q_pop(&in, &ev) && ev.code != delivered++) __builtin_trap();
        }
        if (data[i] & 2u) {
            memset(&ev, 0, sizeof ev);
            ev.kind = (plat_ev_kind)(data[i] % (PLAT_EV_KEYMAP + 1));
            ev.code = produced++;
            if (!x11_q_push(&in, &ev)) __builtin_trap();
        }
    }
    plat_event ev;
    while (x11_q_pop(&in, &ev)) if (ev.code != delivered++) __builtin_trap();
    if (produced != delivered || in.q_dropped) __builtin_trap();
}

static void fuzz_clip_sequences(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    fuzz_input_order(data, size);
    fuzz_clip_sequences(data, size);
    if (!g_ready) setup();
    plat_event e;
    uint64_t now = 1000000000ull;
    if (size >= 32) {                                   /* 32 raw bytes as a core key event */
        xcb_key_press_event_t k;
        memcpy(&k, data, sizeof k);
        for (int i = 0; i < 2; i++) {
            now += 1000000ull * (data[1] % 50);
            x11_input_key(&g_in, &k, (data[0] & 1) != 0, now, &e);
            while (x11_repeat_poll(&g_in, now + 600000000ull, &e)) { now += 5000000ull; if (e.utf8_len > 8) __builtin_trap(); }
            x11_input_button(&g_in, (const xcb_button_press_event_t *)&k, (data[0] & 2) != 0, now, (data[0] & 4) != 0, &e);
        }
        xcb_selection_notify_event_t n; memcpy(&n, data, sizeof n);
        x11_clip_notify_matches(&n, 9, 10, 11, 12, 13);
        n.response_type = XCB_SELECTION_NOTIFY;
        if (!x11_clip_notify_matches(&n, n.requestor, n.selection, n.target, n.property, n.time) ||
            x11_clip_notify_matches(&n, n.requestor ^ 1u, n.selection, n.target, n.property, n.time) ||
            x11_clip_notify_matches(&n, n.requestor, n.selection, n.target ^ 1u, n.property, n.time)) __builtin_trap();
        if (g_in.rep_active == 0 && x11_repeat_deadline(&g_in) != 0) __builtin_trap();
    }
    xi2 x;
    memset(&x, 0, sizeof x);
    x.opcode = size ? data[0] : 0;
    xi2_parse_query_device(&x, data, size);             /* may populate a scroll table from fuzz bytes */
    xi2_result r;
    uint8_t *copy = malloc(size ? size : 1);            /* exact-size heap copy so ASan sees any over-read */
    if (!copy) return 0;
    memcpy(copy, data, size);
    xi2_decode(&x, copy, size, false, &r);
    xi2_decode(&x, copy, size, true, &r);
    /* edit-e6x.20 XI2 ops: a well-shaped QueryDevice reply (master pointer 12, scroll + valuator classes whose numbers,
     * flags and increments come from fuzz bytes) so the populated decoder is reachable; a rejected parse or decode must
     * leave the table untouched (all-or-nothing), an accepted one must keep every axis remainder within [-0.5, 0.5]. */
    if (size >= 16) {
        uint8_t q[32 + 12 + 4 + 4 * 24 + 2 * 44];
        memset(q, 0, sizeof q);
        q[0] = 1; q[8] = 1;
        size_t o = 32, ncls = 0;
        q[o] = 12; q[o + 2] = 1; q[o + 8] = 4;
        o += 16;
        for (size_t c = 0; c < 6; c++) {
            uint8_t b0 = data[c], b1 = data[c + 6];
            if (c < 4) {                                     /* scroll class */
                q[o] = 3; q[o + 2] = 6; q[o + 4] = 12; q[o + 6] = (uint8_t)(2u + (b0 & 7u)); q[o + 8] = (b1 & 1u) ? 1 : 2;
                q[o + 12] = (uint8_t)(b1 & 2u); q[o + 16] = (uint8_t)(b0 >> 3) ? (uint8_t)(b0 >> 3) : 1u;
                o += 24;
            } else {                                         /* valuator class seeding history */
                q[o] = 2; q[o + 2] = 11; q[o + 4] = 12; q[o + 6] = (uint8_t)(2u + (b0 & 7u)); q[o + 28] = b1;
                o += 44;
            }
            ncls++;
        }
        q[32 + 6] = (uint8_t)ncls;
        uint32_t words = (uint32_t)((o - 32) / 4); memcpy(q + 4, &words, 4);
        size_t cut = (data[12] & 1u) ? o : (size_t)data[13] % (o + 1);
        xi2 w; memset(&w, 0, sizeof w); w.opcode = 131;
        xi2 snap = w;
        int rc = xi2_parse_query_device(&w, q, cut);
        if (rc < 0 && memcmp(&w, &snap, sizeof w) != 0) __builtin_trap();
        if (rc >= 0) {
            uint8_t ev[128];
            for (size_t ei = 0; ei < 4; ei++) {
                memset(ev, 0, sizeof ev);
                ev[0] = 35; ev[1] = 131; ev[8] = 6; ev[48] = 1; ev[50] = 1; ev[52] = 12;
                uint32_t mask = (uint32_t)data[(ei + 14) % size] | ((uint32_t)data[(ei + 3) % size] << 8);
                memcpy(ev + 84, &mask, 4);
                memcpy(ev + 88, data + (ei * 5) % (size - 8), 8);
                size_t elen = 88 + 8 * (size_t)__builtin_popcount(mask & 0xffffu);
                if (elen > sizeof ev) elen = sizeof ev;
                if (data[15] & 1u) elen -= (size_t)(data[15] >> 1) % 9u;
                uint8_t *ec = malloc(elen ? elen : 1);
                if (!ec) break;
                memcpy(ec, ev, elen);
                xi2 pre = w;
                if (!xi2_decode(&w, ec, elen, false, &r) && memcmp(&w, &pre, sizeof w) != 0) __builtin_trap();
                free(ec);
                for (uint32_t i = 0; i < w.ndev; i++)
                    for (uint32_t a = 0; a < w.dev[i].naxes; a++)
                        if (!(w.dev[i].axis[a].rem >= -0.5 && w.dev[i].axis[a].rem <= 0.5)) __builtin_trap();
            }
        }
    }
    x11_clip_property v;
    xcb_atom_t pairs[128]; size_t npairs;
    if (x11_clip_decode_property(copy, size, &v)) {
        x11_clip_decode_pairs(&v, 103, pairs, 64, &npairs);
        x11_clip_decode_transfer(&v, 101, 102, 100, 0, true);
        x11_clip_decode_transfer(&v, 101, 102, 100, XCB_ATOM_STRING, false);
    }
    /* Also construct well-shaped replies so the fuzzer explores payload validation,
     * INCR lower bounds, type changes, partial direct-property reads and MULTIPLE lists. */
    size_t wire_size = 32 + ((size + 3u) & ~(size_t)3u);
    uint8_t *wire = calloc(1, wire_size);
    if (wire) {
        xcb_get_property_reply_t h; memset(&h, 0, sizeof h);
        h.response_type = 1; h.type = size ? 100u + data[0] % 4u : 100u;
        h.format = size && (data[0] & 4u) ? 32 : 8;
        h.value_len = (uint32_t)(h.format == 32 ? size / 4u : size);
        h.length = (uint32_t)((size + 3u) / 4u);
        if (size > 1 && (data[1] & 1u)) h.bytes_after = data[1];
        memcpy(wire, &h, 32); memcpy(wire + 32, data, size);
        if (x11_clip_decode_property(wire, wire_size, &v)) {
            x11_clip_decode_pairs(&v, 103, pairs, 64, &npairs);
            x11_clip_decode_transfer(&v, 101, 102, 100, 0, true);
            x11_clip_decode_transfer(&v, 101, 102, 100, 101, false);
        }
        free(wire);
    }
    if (size) {
        uint8_t *converted = malloc(size * 2u);
        if (converted) { x11_utf8_to_latin1(copy, size, converted); x11_latin1_to_utf8(copy, size, converted); free(converted); }
    }
    free(copy);
    x11_clock c;
    memset(&c, 0, sizeof c);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t ms; memcpy(&ms, data + i, 4);
        now += 1000000ull;
        if (x11_clock_map(&c, ms, now) > now) __builtin_trap();
    }
    return 0;
}

/* Optional live operation stream against a raw ICCCM peer. Set
 * EDIT_X11_FUZZ_LIVE=1: missing private Xvfb is fatal, never a silent skip.
 * The byte stream selects bounded transactions and their payload/chunk split;
 * the model checks one completion per accepted request and its exact bytes.
 * No keyboard/window/runtime refactor is needed to exercise clip.c. */
#include "../tests/x11_xvfb.h"
#include "trace/trace.h"
#include <poll.h>

typedef struct clip_peer {
    plat p;
    x11_input queue;
    xcb_connection_t *raw;
    xcb_window_t win;
    xcb_atom_t clipboard, primary, utf8, incr;
    uint8_t bytes[32];
    size_t len, split;
    unsigned mode, phase, completed;
    bool requested, expected_failure;
    int expected_which;
    const uint8_t *expected_bytes;
    size_t expected_len;
    xcb_selection_request_event_t request;
} clip_peer;

static void clip_assert(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "§21 clipboard model: %s\n", what); __builtin_trap(); }
}
static void clip_barrier(xcb_connection_t *c) {
    xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
    clip_assert(reply != NULL, "server barrier"); free(reply);
}
static xcb_atom_t clip_atom(xcb_connection_t *c, const char *name) {
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    clip_assert(reply != NULL, "intern atom"); xcb_atom_t a = reply->atom; free(reply); return a;
}
static xcb_window_t clip_window(xcb_connection_t *c) {
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_window_t win = xcb_generate_id(c);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_generic_error_t *err = xcb_request_check(c, xcb_create_window_checked(c, XCB_COPY_FROM_PARENT,
        win, screen->root, 0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
        XCB_CW_EVENT_MASK, &mask));
    clip_assert(!err, "create peer window"); free(err); return win;
}
static uint32_t clip_server_time(clip_peer *h) {
    unsigned sequence = xcb_change_property(h->raw, XCB_PROP_MODE_REPLACE, h->win,
                                            h->incr, XCB_ATOM_INTEGER, 8, 1, "t").sequence;
    clip_barrier(h->raw);
    uint32_t stamp = 0;
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(h->raw))) {
        if ((event->response_type & 0x7fu) == XCB_PROPERTY_NOTIFY) {
            const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)event;
            if (n->window == h->win && n->atom == h->incr && n->sequence == (uint16_t)sequence) stamp = n->time;
        }
        free(event);
    }
    clip_assert(stamp != 0, "server timestamp for racing claim"); return stamp;
}
static void clip_notify(clip_peer *h, bool wrong_tuple) {
    const xcb_selection_request_event_t *q = &h->request;
    xcb_selection_notify_event_t n = { .response_type = XCB_SELECTION_NOTIFY, .requestor = q->requestor,
        .selection = q->selection, .target = wrong_tuple ? XCB_ATOM_STRING : q->target,
        .property = q->property, .time = q->time };
    xcb_send_event(h->raw, 0, q->requestor, 0, (const char *)&n);
}
static void clip_chunk(clip_peer *h) {
    const xcb_selection_request_event_t *q = &h->request;
    size_t off = h->phase ? h->split : 0;
    size_t len = h->phase == 0 ? h->split : h->phase == 1 ? h->len - h->split : 0;
    if (h->mode == 2) len = 0; /* premature INCR terminator */
    xcb_atom_t type = h->mode == 3 && h->phase == 1 ? XCB_ATOM_STRING : h->utf8;
    xcb_change_property(h->raw, XCB_PROP_MODE_REPLACE, q->requestor, q->property, type, 8,
                        (uint32_t)len, h->bytes + off);
    h->phase++;
}
static void clip_raw_step(clip_peer *h) {
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(h->raw))) {
        uint8_t type = event->response_type & 0x7fu;
        if (type == XCB_SELECTION_REQUEST) {
            h->request = *(const xcb_selection_request_event_t *)event;
            h->requested = true;
            const xcb_selection_request_event_t *q = &h->request;
            clip_assert(q->target == h->utf8, "UTF8 conversion target");
            if (h->mode == 4) { free(event); continue; } /* silent owner -> timeout */
            if (h->mode == 7) clip_notify(h, true); /* must not complete this request */
            if (h->mode >= 1 && h->mode <= 3) {
                uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
                xcb_change_window_attributes(h->raw, q->requestor, XCB_CW_EVENT_MASK, &mask);
                uint32_t lower = (uint32_t)h->len;
                xcb_change_property(h->raw, XCB_PROP_MODE_REPLACE, q->requestor, q->property, h->incr, 32, 1, &lower);
            } else {
                xcb_change_property(h->raw, XCB_PROP_MODE_REPLACE, q->requestor, q->property, h->utf8, 8,
                                    (uint32_t)h->len, h->bytes);
            }
            clip_notify(h, false);
        } else if (type == XCB_PROPERTY_NOTIFY && h->requested && h->mode >= 1 && h->mode <= 3) {
            const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)event;
            if (n->window == h->request.requestor && n->atom == h->request.property &&
                n->state == XCB_PROPERTY_DELETE && h->phase < 3) clip_chunk(h);
        }
        free(event);
    }
}
static void clip_completion(void *ud, const plat_event *out) {
    clip_peer *h = ud;
    if (out->kind != PLAT_EV_CLIPBOARD || out->code == 2) return;
    if (out->clip_which != h->expected_which || out->code != (h->expected_failure ? 1u : 0u))
        fprintf(stderr, "mode=%u selection=%u expected=%d code=%u expected_failure=%d\n",
                h->mode, out->clip_which, h->expected_which, out->code, h->expected_failure);
    clip_assert(out->clip_which == h->expected_which && out->code == (h->expected_failure ? 1u : 0u) &&
                out->clip_ok == !h->expected_failure, "completion selection/status");
    if (!h->expected_failure) {
        size_t got = 0; const uint8_t *bytes = plat_clip_data(&h->p, &got);
        if (got != h->expected_len) fprintf(stderr, "op mode=%u expected=%zu got=%zu\n", h->mode, h->expected_len, got);
        clip_assert(got == h->expected_len && bytes && memcmp(bytes, h->expected_bytes, got) == 0,
                    "completion/data association");
    }
    h->completed++;
}
static void clip_app_step(clip_peer *h, int which, const uint8_t *want, size_t len, bool failure) {
    h->expected_which = which; h->expected_bytes = want; h->expected_len = len; h->expected_failure = failure;
    plat_callbacks cb = { h, clip_completion, NULL, NULL, NULL };
    clip_assert(plat_run_for(&h->p, &cb, 0) == PLAT_OK, "nonblocking clipboard dispatch");
}
static void clip_finish(clip_peer *h, int which, const uint8_t *want, size_t len, bool failure, unsigned count) {
    uint64_t end = trace_now_ns() + UINT64_C(2000000000);
    while (h->completed < count && trace_now_ns() < end) {
        xcb_flush(h->p.conn); xcb_flush(h->raw);
        clip_raw_step(h); clip_app_step(h, which, want, len, failure);
        if (h->completed < count) {
            struct pollfd fds[2] = { { xcb_get_file_descriptor(h->p.conn), POLLIN, 0 },
                                    { xcb_get_file_descriptor(h->raw), POLLIN, 0 } };
            (void)poll(fds, 2, 1);
        }
    }
    clip_assert(h->completed == count, "exactly one completion per request (watchdog)");
    /* Rejections can have property deletion/reply cleanup still in flight.
     * Settle that traffic before the next accepted request, while checking
     * every queued completion against the same model. */
    for (unsigned i = 0; i < 16; i++) {
        clip_barrier(h->raw); clip_barrier(h->p.conn);
        clip_raw_step(h); xcb_flush(h->raw);
        clip_app_step(h, which, want, len, failure);
        clip_assert(h->completed == count, "no duplicate completion");
        if (!x11_clip_busy(&h->p)) break;
    }

}
static void fuzz_clip_sequences(const uint8_t *data, size_t size) {
    if (!getenv("EDIT_X11_FUZZ_LIVE") || !size) return;
    static bool started;
    if (!started) {
        clip_assert(xvfb_start() != 0, "private Xvfb required in live mode");
        trace_init(); trace_thread_register(); started = true;
    }
    clip_peer h; memset(&h, 0, sizeof h);
    h.p.conn = xcb_connect(NULL, NULL); h.raw = xcb_connect(NULL, NULL);
    clip_assert(!xcb_connection_has_error(h.p.conn) && !xcb_connection_has_error(h.raw), "private connections");
    h.p.win = clip_window(h.p.conn); h.win = clip_window(h.raw);
    h.p.in = &h.queue;
    h.p.timer_fd = h.p.repeat_fd = h.p.work_fd = -1;
    clip_assert(x11_clip_init(&h.p) == PLAT_OK, "clipboard initialization");
    x11_clip_set_limits(&h.p, 4, 250, UINT32_MAX);
    h.clipboard = clip_atom(h.raw, "CLIPBOARD"); h.primary = XCB_ATOM_PRIMARY;
    h.utf8 = clip_atom(h.raw, "UTF8_STRING"); h.incr = clip_atom(h.raw, "INCR");
    size_t ops = size < 8 ? size : 8;
    for (size_t i = 0; i < ops; i++) {
        h.mode = data[i] % 8u; h.phase = h.completed = 0; h.requested = false;
        x11_clip_set_limits(&h.p, 0, 250, UINT32_MAX);
        h.len = 2u + data[(i + 1) % size] % 30u;
        h.split = 1u + data[(i + 2) % size] % (h.len - 1u);
        for (size_t j = 0; j < h.len; j++) h.bytes[j] = (uint8_t)('a' + data[(i + j) % size] % 26u);
        int which = (data[i] & 8u) ? PLAT_CLIP_PRIMARY : PLAT_CLIP_CLIPBOARD;
        xcb_atom_t selection = which == PLAT_CLIP_PRIMARY ? h.primary : h.clipboard;
        if (h.mode == 6) {
            /* Alternating local selections: bytes observed from each callback
             * must match that selection, including deferred owner confirmation. */
            clip_assert(plat_clip_set(&h.p, which, h.bytes, h.len) == PLAT_OK, "local claim");
            clip_assert(plat_clip_request(&h.p, which) == PLAT_OK, "pending local request");
            clip_finish(&h, which, h.bytes, h.len, false, 1);
            continue;
        }
        if (h.mode == 5) {
            h.p.last_time = clip_server_time(&h);
            clip_assert(plat_clip_set(&h.p, which, "old", 3) == PLAT_OK, "racing claim");
            clip_barrier(h.p.conn); /* server accepted claim; confirmation still pending */
            clip_assert(plat_clip_request(&h.p, which) == PLAT_OK, "request behind racing confirmation");
        }
        xcb_set_selection_owner(h.raw, h.win, selection, XCB_CURRENT_TIME);
        clip_barrier(h.raw);
        if (h.mode == 5) {
            clip_barrier(h.p.conn);
            clip_finish(&h, which, h.bytes, h.len, false, 1);
            h.p.last_time = 0;
            continue;
        }
        /* Finish ownership loss before asking the raw owner. */
        clip_app_step(&h, which, h.bytes, h.len, false);
        x11_clip_set_limits(&h.p, 0, h.mode == 4 ? 2u : 250u, UINT32_MAX);
        int rc = plat_clip_request(&h.p, which);
        if (rc != PLAT_OK) fprintf(stderr, "fuzz op=%zu mode=%u which=%d rc=%d busy=%zu completed=%u\n",
                                  i, h.mode, which, rc, x11_clip_busy(&h.p), h.completed);
        clip_assert(rc == PLAT_OK, "remote request accepted");
        clip_finish(&h, which, h.bytes, h.len, h.mode == 2 || h.mode == 3 || h.mode == 4, 1);
        clip_assert(h.requested, "raw peer received conversion");
    }
    x11_clip_destroy(&h.p);
    clip_assert(x11_clip_mem(&h.p) == 0, "destroy releases clipboard memory");
    xcb_disconnect(h.p.conn); xcb_disconnect(h.raw);
}
