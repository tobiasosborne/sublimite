/* x11_clip_test.c - selection transfers against a raw xcb peer (P2.2b): INCR both directions, MULTIPLE,
 * STRING fallback, unsolicited/stale selection events, request timeouts, clipboard-manager save and the
 * grab-mode focus rule. The peer is a hand-written ICCCM client so the plat code is not tested against itself.
 * Only missing Xvfb can skip (EDIT_X11_STRICT=1 makes that fatal too). Never takes over a real CLIPBOARD_MANAGER. */
#include "x11/plat.h"
#include "x11_xvfb.h"
#include "x11/clip.h"
#include "x11/input.h"
#include "trace/trace.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static uint64_t now_ms(void) { return trace_now_ns() / 1000000u; }
static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail = 1; } } while (0)

typedef struct sink { int arrived, failed, lost, focus; } sink;
static void on_ev(void *ud, const plat_event *e) {
    sink *s = ud;
    if (e->kind == PLAT_EV_FOCUS) s->focus++;
    if (e->kind != PLAT_EV_CLIPBOARD) return;
    if (e->code == 0) s->arrived++; else if (e->code == 1) s->failed++; else s->lost++;
}

static xcb_atom_t intern(xcb_connection_t *c, const char *n) {
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(n), n), NULL);
    xcb_atom_t a = r ? r->atom : 0;
    free(r);
    return a;
}

enum { OWN_NONE, OWN_PLAIN, OWN_INCR, OWN_NO_UTF8, OWN_SILENT, OWN_INCR_STALL, OWN_INCR_OVERSIZE, OWN_STRING_INCR };
typedef struct rawp {
    xcb_connection_t *c;
    xcb_window_t win;
    xcb_atom_t clipboard, utf8, string, incr, targets, multiple, atom_pair, p1, p2, p3, p4, manager, save;
    /* requester side */
    bool notified, failed, is_incr, done, stall, reject_old_time;
    uint8_t *got; size_t got_len;
    /* owner side */
    int own;
    const uint8_t *data; size_t data_len, off, chunk;
    xcb_window_t req_win; xcb_atom_t req_prop, req_target; bool owner_busy;
    uint32_t lb_force;                       /* INCR header lower bound override (0 = real size) */
    int delay_ms;                            /* OWN_NO_UTF8: answer each request this late */
    xcb_selection_request_event_t dq; uint64_t dq_at;
    xcb_atom_t nprops[8]; int nn;            /* MULTIPLE SelectionNotify properties in arrival order */
} rawp;

static void rp_open(rawp *r) {
    memset(r, 0, sizeof *r);
    r->c = xcb_connect(NULL, NULL);
    xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(r->c)).data;
    r->win = xcb_generate_id(r->c);
    uint32_t m = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(r->c, XCB_COPY_FROM_PARENT, r->win, s->root, 0, 0, 10, 10, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &m);
    r->clipboard = intern(r->c, "CLIPBOARD"); r->utf8 = intern(r->c, "UTF8_STRING"); r->string = XCB_ATOM_STRING;
    r->incr = intern(r->c, "INCR"); r->targets = intern(r->c, "TARGETS"); r->multiple = intern(r->c, "MULTIPLE");
    r->atom_pair = intern(r->c, "ATOM_PAIR"); r->p1 = intern(r->c, "RP_P1"); r->p2 = intern(r->c, "RP_P2");
    r->p3 = intern(r->c, "RP_P3"); r->p4 = intern(r->c, "RP_P4");
    r->manager = intern(r->c, "CLIPBOARD_MANAGER"); r->save = intern(r->c, "SAVE_TARGETS");
    xcb_flush(r->c);
}

static void rp_close(rawp *r) { free(r->got); xcb_disconnect(r->c); }

static void rp_reset_req(rawp *r) {
    r->notified = r->failed = r->is_incr = r->done = false;
    free(r->got); r->got = NULL; r->got_len = 0; r->nn = 0;
}

static void rp_append(rawp *r, const uint8_t *v, size_t n) {
    r->got = realloc(r->got, r->got_len + n + 1);
    memcpy(r->got + r->got_len, v, n);
    r->got_len += n;
}

static void rp_read_prop(rawp *r, xcb_atom_t prop, bool chunk) {
    xcb_get_property_reply_t *g = xcb_get_property_reply(r->c,
        xcb_get_property(r->c, 1, r->win, prop, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x3fffffff), NULL);
    if (!g) { r->failed = true; return; }
    if (!chunk && g->type == r->incr) r->is_incr = true;
    else {
        int n = xcb_get_property_value_length(g);
        if (chunk && n == 0) r->done = true;
        else { rp_append(r, xcb_get_property_value(g), (size_t)n); if (!chunk) r->done = true; }
    }
    free(g);
}

static void rp_send_notify(rawp *r, xcb_window_t to, xcb_atom_t sel, xcb_atom_t target, xcb_atom_t prop, uint32_t time) {
    xcb_selection_notify_event_t n;
    memset(&n, 0, sizeof n);
    n.response_type = XCB_SELECTION_NOTIFY; n.time = time; n.requestor = to; n.selection = sel;
    n.target = target; n.property = prop;
    xcb_send_event(r->c, 0, to, 0, (const char *)&n);
    xcb_flush(r->c);
}

static void rp_put_chunk(rawp *r) {
    size_t n = r->data_len - r->off < r->chunk ? r->data_len - r->off : r->chunk;
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->req_win, r->req_prop, r->req_target, 8, (uint32_t)n, r->data + r->off);
    r->off += n;
    if (n == 0) r->owner_busy = false;
    xcb_flush(r->c);
}

static void rp_request(rawp *r, const xcb_selection_request_event_t *q) {
    if (r->own == OWN_SILENT || r->own == OWN_NONE) {
        r->req_win = q->requestor; r->req_prop = q->property; r->req_target = q->target;
        return;
    }
    if (r->reject_old_time && q->time == 1) rp_send_notify(r, q->requestor, q->selection, q->target, XCB_ATOM_NONE, q->time);
    else if (q->target == r->utf8 && (r->own == OWN_NO_UTF8 || r->own == OWN_STRING_INCR)) { rp_send_notify(r, q->requestor, q->selection, q->target, XCB_ATOM_NONE, q->time); }
    else if (q->target == r->string && r->own == OWN_NO_UTF8) {
        xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, q->requestor, q->property, r->string, 8,
                            (uint32_t)r->data_len, r->data);
        rp_send_notify(r, q->requestor, q->selection, q->target, q->property, q->time);
    } else if ((q->target == r->utf8 && (r->own == OWN_INCR || r->own == OWN_INCR_STALL || r->own == OWN_INCR_OVERSIZE)) ||
               (q->target == r->string && r->own == OWN_STRING_INCR)) {
        uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        uint32_t lb = r->own == OWN_INCR_OVERSIZE ? UINT32_MAX : r->lb_force ? r->lb_force : (uint32_t)r->data_len;
        xcb_change_window_attributes(r->c, q->requestor, XCB_CW_EVENT_MASK, &mask);
        r->req_win = q->requestor; r->req_prop = q->property; r->req_target = q->target; r->off = 0;
        r->owner_busy = r->own != OWN_INCR_STALL;
        xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, q->requestor, q->property, r->incr, 32, 1, &lb);
        rp_send_notify(r, q->requestor, q->selection, q->target, q->property, q->time);
    } else if (q->target == r->utf8 && r->own == OWN_PLAIN) {
        xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, q->requestor, q->property, r->utf8, 8,
                            (uint32_t)r->data_len, r->data);
        rp_send_notify(r, q->requestor, q->selection, q->target, q->property, q->time);
    } else rp_send_notify(r, q->requestor, q->selection, q->target, XCB_ATOM_NONE, q->time);
}

/* One non-blocking step: handles requester notifications and, if it owns CLIPBOARD, requests against it. */
static void rp_step(rawp *r) {
    xcb_generic_event_t *e;
    if (r->dq_at && now_ms() >= r->dq_at) { r->dq_at = 0; rp_request(r, &r->dq); }
    while ((e = xcb_poll_for_event(r->c))) {
        uint8_t t = e->response_type & 0x7f;
        if (t == XCB_SELECTION_NOTIFY) {
            const xcb_selection_notify_event_t *n = (const xcb_selection_notify_event_t *)e;
            r->notified = true;
            if (n->target == r->multiple && r->nn < 8) r->nprops[r->nn++] = n->property;
            if (n->property == XCB_ATOM_NONE) r->failed = true;
            else if (n->target == r->multiple) r->done = true;
            else if (!r->stall) rp_read_prop(r, n->property, false);
        } else if (t == XCB_PROPERTY_NOTIFY) {
            const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)e;
            if (n->window == r->win && n->state == XCB_PROPERTY_NEW_VALUE && r->is_incr && !r->done && !r->stall)
                rp_read_prop(r, n->atom, true);
            if (n->window == r->req_win && n->state == XCB_PROPERTY_DELETE && r->owner_busy && n->atom == r->req_prop &&
                (r->own == OWN_INCR || r->own == OWN_INCR_OVERSIZE || r->own == OWN_STRING_INCR))
                rp_put_chunk(r);
        } else if (t == XCB_SELECTION_REQUEST) {
            const xcb_selection_request_event_t *q = (const xcb_selection_request_event_t *)e;
            if (r->delay_ms && r->own == OWN_NO_UTF8) { r->dq = *q; r->dq_at = now_ms() + (uint64_t)r->delay_ms; }
            else rp_request(r, q);
        }
        free(e);
    }
}

static void rp_own(rawp *r, unsigned mode, const void *data, size_t n, size_t chunk) {
    r->own = (int)mode; r->data = data; r->data_len = n; r->chunk = chunk;
    xcb_set_selection_owner(r->c, r->win, r->clipboard, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(r->c, xcb_get_input_focus(r->c), NULL);
    free(f);
}

static void pump(plat *a, sink *sa, rawp *r, int rounds) {
    plat_callbacks ca = { sa, on_ev, NULL, NULL, NULL };
    for (int i = 0; i < rounds; i++) { plat_run_for(a, &ca, 4); if (r) rp_step(r); }
}


static bool pump_until(plat *a, sink *sa, rawp *r, const bool *flag, const int *counter, int limit_ms) {
    uint64_t end = now_ms() + (uint64_t)limit_ms;
    while (now_ms() < end) {
        pump(a, sa, r, 1);
        if ((flag && *flag) || (counter && *counter)) return true;
    }
    return false;
}

static plat *open_plat(plat *p) {
    plat_config cfg = { "x11 clip", 100, 100, false, -1, 0 };
    int rc = plat_init(p, &cfg);
    if (rc != PLAT_OK) { fprintf(stderr, "x11_clip_test: plat_init rc=%d\n", rc); return NULL; }
    x11_clip_set_limits(p, 0, 0, UINT32_MAX);        /* never talk to a real clipboard manager unless a test asks */
    return p;
}

static uint8_t *pattern(size_t n) {
    uint8_t *b = malloc(n + 1);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)('a' + (i * 7 + i / 13) % 26);
    b[n] = 0;
    return b;
}

static void set_and_confirm(plat *a, sink *sa, rawp *r, int which, const void *d, size_t n) {
    CHECK(plat_clip_set(a, which, d, n) == PLAT_OK, "set");
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(a->conn, xcb_get_input_focus(a->conn), NULL);
    free(f);
    pump(a, sa, r, 3);
}

/* ---- serving ---- */
static void test_serve_incr(plat *a, sink *sa, rawp *r) {
    x11_clip_set_limits(a, 1000, 0, 0);
    size_t n = 10500;
    uint8_t *d = pattern(n);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, d, n);
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 2000);
    CHECK(r->done && !r->failed, "INCR serve completed");
    CHECK(r->is_incr, "large selection is announced with INCR");
    CHECK(r->got_len == n && r->got && memcmp(r->got, d, n) == 0, "INCR served bytes identical (%zu of %zu)", r->got_len, n);
    pump(a, sa, r, 3);
    CHECK(x11_clip_busy(a) == 0, "serve slot released after the zero-length chunk");
    /* small selections stay on the direct path */
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->targets, r->p2, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && !r->is_incr, "TARGETS is never INCR");
    /* STRING over INCR: chunk edges must not split UTF-8 sequences (chunk 7 vs 2-byte e-acute) */
    size_t reps = 400;
    uint8_t *u = malloc(reps * 3 + 1), *want = malloc(reps * 2 + 1);
    size_t un = 0, wn = 0;
    for (size_t i = 0; i < reps; i++) {
        u[un++] = 'x'; u[un++] = 0xc3; u[un++] = 0xa9;           /* x e-acute */
        want[wn++] = 'x'; want[wn++] = 0xe9;
    }
    x11_clip_set_limits(a, 7, 0, 0);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, u, un);
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->string, r->p1, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 4000);
    CHECK(r->done && r->is_incr && r->got_len == wn && memcmp(r->got, want, wn) == 0,
          "STRING INCR converts per chunk without splitting sequences (%zu of %zu)", r->got_len, wn);
    free(d); free(u); free(want);
    x11_clip_set_limits(a, 256 * 1024, 0, 0);
}

static void test_large_and_snapshot(plat *a, sink *sa, rawp *r) {
    x11_clip_set_limits(a, 256 * 1024, 0, 0);
    size_t n = 20u * 1024u * 1024u; uint8_t *d = pattern(n);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, d, n);
    CHECK(plat_clip_set(a, PLAT_CLIP_CLIPBOARD, "x", 64u * 1024u * 1024u + 1u) == PLAT_ERR_FAIL,
          "clipboard buffers have a 64 MiB bound");
    rp_reset_req(r); r->stall = true;
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME); xcb_flush(r->c);
    pump_until(a, sa, r, &r->notified, NULL, 1000);
    CHECK(r->notified && !r->failed, "20 MiB selection served past the one-request limit");
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "replacement", 11);
    r->stall = false;
    if (!r->failed) rp_read_prop(r, r->p1, false);
    pump_until(a, sa, r, &r->done, NULL, 30000);
    CHECK(r->done && r->is_incr && r->got_len == n && memcmp(r->got, d, n) == 0,
          "INCR keeps its immutable snapshot across a replacement (%zu/%zu)", r->got_len, n);
    pump(a, sa, r, 3); CHECK(x11_clip_busy(a) == 0, "large snapshot released");
    free(d);
}

static void test_serve_stalled_requester(plat *a, sink *sa, rawp *r) {
    x11_clip_set_limits(a, 500, 60, 0);
    size_t n = 5000;
    uint8_t *d = pattern(n);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, d, n);
    rp_reset_req(r);
    r->stall = true;                                   /* takes the INCR header, never deletes it */
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->notified, NULL, 1000);
    CHECK(r->notified && r->is_incr == false, "stalled requester sees the header notify");
    CHECK(x11_clip_busy(a) == 1, "INCR transfer in flight (busy %zu)", x11_clip_busy(a));
    CHECK(x11_clip_deadline(a) != 0, "loop is asked to wake for the timeout");
    uint64_t t0 = now_ms();
    while (x11_clip_busy(a) && now_ms() - t0 < 1000) pump(a, sa, r, 1);
    uint64_t took = now_ms() - t0;
    CHECK(x11_clip_busy(a) == 0, "stalled INCR transfer abandoned");
    CHECK(took >= 30 && took < 500, "abandoned after about the 60 ms timeout, not instantly or never (%llu ms)", (unsigned long long)took);
    CHECK(x11_clip_deadline(a) == 0, "no timer left armed");
    r->stall = false;
    rp_reset_req(r);
    x11_clip_set_limits(a, 1u << 20, 0, 0);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p2, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && r->got_len == n && memcmp(r->got, d, n) == 0, "owner still serves after a stalled peer");
    free(d);
    x11_clip_set_limits(a, 256 * 1024, 5000, 0);
}

static void test_multiple(plat *a, sink *sa, rawp *r) {
    const char *txt = "caf\xc3\xa9 multiple";
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, txt, strlen(txt));
    xcb_atom_t bogus = intern(r->c, "NOT_A_TARGET_AT_ALL");
    xcb_atom_t pairs[8] = { r->utf8, r->p2, r->string, r->p3, bogus, r->p4, r->targets, r->p1 + 0 };
    pairs[7] = intern(r->c, "RP_P5");
    xcb_atom_t p5 = pairs[7];
    rp_reset_req(r);
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p1, r->atom_pair, 32, 8, pairs);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p1, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && !r->failed, "MULTIPLE answered");
    xcb_get_property_reply_t *pr = xcb_get_property_reply(r->c,
        xcb_get_property(r->c, 0, r->win, r->p1, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    CHECK(pr && pr->format == 32 && xcb_get_property_value_length(pr) == 32, "pair list preserved");
    if (pr && xcb_get_property_value_length(pr) == 32) {
        const xcb_atom_t *v = xcb_get_property_value(pr);
        CHECK(v[1] == r->p2 && v[3] == r->p3 && v[5] == XCB_ATOM_NONE && v[7] == p5,
              "unconvertible pair gets property None, others keep theirs (%u %u %u %u)", v[1], v[3], v[5], v[7]);
    }
    free(pr);
    xcb_get_property_reply_t *g = xcb_get_property_reply(r->c,
        xcb_get_property(r->c, 0, r->win, r->p2, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    CHECK(g && g->type == r->utf8 && (size_t)xcb_get_property_value_length(g) == strlen(txt) &&
          memcmp(xcb_get_property_value(g), txt, strlen(txt)) == 0, "UTF8 part");
    free(g);
    g = xcb_get_property_reply(r->c, xcb_get_property(r->c, 0, r->win, r->p3, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    CHECK(g && g->type == r->string && xcb_get_property_value_length(g) == (int)strlen(txt) - 1 &&
          ((uint8_t *)xcb_get_property_value(g))[3] == 0xe9, "STRING part is latin-1");
    free(g);
    g = xcb_get_property_reply(r->c, xcb_get_property(r->c, 0, r->win, p5, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    CHECK(g && g->type == XCB_ATOM_ATOM && g->format == 32, "TARGETS part");
    int has_multiple = 0;
    if (g) { const xcb_atom_t *v = xcb_get_property_value(g); for (int i = 0; i < xcb_get_property_value_length(g) / 4; i++) has_multiple |= v[i] == r->multiple; }
    CHECK(has_multiple, "MULTIPLE advertised in TARGETS");
    free(g);
    /* a MULTIPLE whose property is missing is refused, not wedged */
    rp_reset_req(r);
    xcb_delete_property(r->c, r->win, r->p4);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p4, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->notified, NULL, 1000);
    CHECK(r->notified && r->failed, "MULTIPLE without a pair list is refused");
    pump(a, sa, r, 3);
    CHECK(x11_clip_busy(a) == 0, "MULTIPLE job released");
}

/* ---- receiving ---- */
static void test_receive_incr(plat *a, sink *sa, rawp *r) {
    size_t n = 7000;
    uint8_t *d = pattern(n);
    rp_own(r, OWN_INCR, d, n, 777);
    pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0;
    CHECK(plat_clip_request(a, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "request");
    uint64_t receive_end = now_ms() + 3000;
    while (!sa->arrived && !sa->failed && now_ms() < receive_end) pump(a, sa, r, 1);
    size_t got = 0;
    const uint8_t *g = plat_clip_data(a, &got);
    CHECK(sa->arrived == 1 && !sa->failed, "INCR receive arrived (%d ok, %d failed)", sa->arrived, sa->failed);
    CHECK(g && got == n && memcmp(g, d, n) == 0, "INCR received bytes identical (%zu of %zu)", got, n);
    pump(a, sa, r, 3);
    CHECK(x11_clip_busy(a) == 0, "receive state back to idle");
    /* two queued requests while one is in flight both complete */
    sa->arrived = sa->failed = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    uint64_t end = now_ms() + 3000;
    while (sa->arrived + sa->failed < 2 && now_ms() < end) pump(a, sa, r, 1);
    CHECK(sa->arrived == 2, "coalesced requests both answered (%d)", sa->arrived);
    free(d);
}

static void test_receive_string_fallback(plat *a, sink *sa, rawp *r) {
    static const uint8_t lat[] = { 'c', 'a', 'f', 0xe9 };
    rp_own(r, OWN_NO_UTF8, lat, sizeof lat, 0);
    sa->arrived = sa->failed = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 2000);
    size_t got = 0;
    const uint8_t *g = plat_clip_data(a, &got);
    CHECK(sa->arrived == 1 && !sa->failed && g && got == 5 && memcmp(g, "caf\xc3\xa9", 5) == 0,
          "owner without UTF8_STRING: retried as STRING (%d ok, %d failed, %zu bytes)", sa->arrived, sa->failed, got);
}

static void test_receive_string_incr_and_reject(plat *a, sink *sa, rawp *r) {
    uint8_t data[1024]; memset(data, 0xe9, sizeof data);
    rp_own(r, OWN_STRING_INCR, data, sizeof data, 127); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 1000);
    size_t n; const uint8_t *got = plat_clip_data(a, &n); bool ok = got && n == 2048;
    if (ok) for (size_t i = 0; i < n; i += 2) if (got[i] != 0xc3 || got[i + 1] != 0xa9) ok = false;
    CHECK(ok && sa->arrived == 1 && !sa->failed, "STRING INCR fallback expands Latin-1 in bounded chunks");
    rp_own(r, OWN_INCR_OVERSIZE, data, sizeof data, 127); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    uint64_t end = now_ms() + 1000;
    while (now_ms() < end && (!sa->failed || x11_clip_busy(a) || r->owner_busy)) pump(a, sa, r, 1);
    CHECK(sa->failed == 1 && !sa->arrived, "oversized INCR lower bound refused once");
    CHECK(!r->owner_busy && x11_clip_busy(a) == 0, "rejected INCR drained so cooperative owner does not wedge");
}

static void test_receive_unsolicited(plat *a, sink *sa, rawp *r) {
    rp_own(r, OWN_NONE, NULL, 0, 0);
    size_t before = 0;
    plat_clip_data(a, &before);
    sa->arrived = sa->failed = 0;
    xcb_atom_t prop = intern(r->c, "EDIT_SELECTION");
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, a->win, prop, r->utf8, 8, 6, "forged");
    rp_send_notify(r, a->win, r->clipboard, r->utf8, prop, XCB_CURRENT_TIME);
    pump(a, sa, r, 25);
    size_t after = 0;
    plat_clip_data(a, &after);
    CHECK(sa->arrived == 0 && sa->failed == 0, "unsolicited SelectionNotify raised an event (%d/%d)", sa->arrived, sa->failed);
    CHECK(after == before, "unsolicited SelectionNotify replaced the paste buffer");
}

static void test_receive_timeouts(plat *a, sink *sa, rawp *r) {
    x11_clip_set_limits(a, 0, 60, 0);
    rp_own(r, OWN_SILENT, NULL, 0, 0);
    sa->arrived = sa->failed = 0;
    uint64_t t0 = now_ms();
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    CHECK(x11_clip_deadline(a) != 0, "request arms a deadline");
    pump_until(a, sa, r, NULL, &sa->failed, 2000);
    uint64_t took = now_ms() - t0;
    CHECK(sa->failed == 1 && sa->arrived == 0, "silent owner: request fails (%d)", sa->failed);
    CHECK(took >= 30 && took < 800, "failed after about 60 ms (%llu ms)", (unsigned long long)took);
    CHECK(x11_clip_busy(a) == 0 && x11_clip_deadline(a) == 0, "idle again");
    /* INCR owner that sends the header and then goes quiet */
    uint8_t *d = pattern(4000);
    rp_own(r, OWN_INCR_STALL, d, 4000, 500);
    sa->failed = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->failed, 2000);
    CHECK(sa->failed == 1, "INCR owner that stalls: request fails (%d)", sa->failed);
    CHECK(x11_clip_busy(a) == 0, "stalled receive released");
    free(d);
    x11_clip_set_limits(a, 0, 5000, 0);
}

static void test_late_notify_and_old_input_time(plat *a, sink *sa, rawp *r) {
    a->last_time = 0; x11_clip_set_limits(a, 0, 60, 0);
    rp_own(r, OWN_SILENT, NULL, 0, 0); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->failed, 1000);
    CHECK(sa->failed == 1 && r->req_win, "first conversion timed out");
    xcb_window_t old_win = r->req_win; xcb_atom_t old_prop = r->req_prop;
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, old_win, old_prop, r->utf8, 8, 3, "old");
    rp_send_notify(r, old_win, r->clipboard, r->utf8, old_prop, XCB_CURRENT_TIME);
    pump(a, sa, NULL, 3);  /* Leave the new request queued at the peer. */
    CHECK(!sa->arrived && !sa->failed, "late notify from expired conversion cannot complete its replacement");
    r->own = OWN_PLAIN; r->data = (const uint8_t *)"fresh"; r->data_len = 5;
    pump_until(a, sa, r, NULL, &sa->arrived, 1000);
    size_t n; const uint8_t *d = plat_clip_data(a, &n);
    CHECK(sa->arrived == 1 && d && n == 5 && memcmp(d, "fresh", 5) == 0, "new conversion receives fresh data");
    x11_clip_set_limits(a, 0, 5000, 0);
    r->reject_old_time = true; a->last_time = 1; sa->arrived = sa->failed = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 1000);
    CHECK(sa->arrived == 1 && !sa->failed, "remote paste does not reuse an ancient unrelated input timestamp");
    r->reject_old_time = false; a->last_time = 0;
}

static void send_clear(rawp *r, plat *a, uint32_t time) {
    xcb_selection_clear_event_t c;
    memset(&c, 0, sizeof c);
    c.response_type = XCB_SELECTION_CLEAR; c.time = time; c.owner = a->win; c.selection = r->clipboard;
    xcb_send_event(r->c, 0, a->win, 0, (const char *)&c);
    xcb_flush(r->c);
}

static void test_stale_events(plat *a, sink *sa, rawp *r) {
    rp_own(r, OWN_NONE, NULL, 0, 0);
    /* Get an actual server timestamp; 1000 is normally older than the latest owner and
     * triggers CurrentTime fallback, making the old test's comparison meaningless. */
    unsigned stamp_seq = xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p4, XCB_ATOM_INTEGER, 8, 1, "t").sequence;
    xcb_flush(r->c);
    uint32_t stamp = 0;
    while (!stamp) {
        xcb_generic_event_t *e = xcb_wait_for_event(r->c);
        CHECK(e != NULL, "server timestamp");
        if (!e) return;
        if ((e->response_type & 0x7f) == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t *v = (xcb_property_notify_event_t *)e;
            if (v->window == r->win && v->atom == r->p4 && v->state == XCB_PROPERTY_NEW_VALUE &&
                v->sequence == (uint16_t)stamp_seq) stamp = v->time;
        }
        free(e);
    }
    a->last_time = stamp;
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "keep me", 7);
    sa->lost = 0;
    send_clear(r, a, stamp - 500u);                      /* older than our ownership: stale */
    pump(a, sa, r, 10);
    CHECK(sa->lost == 0, "stale SelectionClear ignored");
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && r->got_len == 7 && memcmp(r->got, "keep me", 7) == 0, "data survived the stale clear");
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, stamp - 500u);   /* request older than ownership */
    xcb_flush(r->c);
    pump_until(a, sa, r, &r->notified, NULL, 1000);
    CHECK(r->notified && r->failed, "SelectionRequest older than our ownership refused");
    rp_own(r, OWN_NONE, NULL, 0, 0);                    /* real takeover: the server sends the Clear (forgeries are ignored, #1) */
    pump_until(a, sa, r, NULL, &sa->lost, 1000);
    CHECK(sa->lost == 1, "genuine SelectionClear honoured");
}


/* ===== review fixes (edit-e6x.18, docs/decisions/P2.2d.md) ===== */
static uint32_t server_time(rawp *r) {
    unsigned seq = xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p4, XCB_ATOM_INTEGER, 8, 1, "t").sequence;
    xcb_flush(r->c);
    uint32_t stamp = 0;
    while (!stamp) {
        xcb_generic_event_t *e = xcb_wait_for_event(r->c);
        if (!e) return 0;
        if ((e->response_type & 0x7f) == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t *v = (xcb_property_notify_event_t *)e;
            if (v->window == r->win && v->atom == r->p4 && v->state == XCB_PROPERTY_NEW_VALUE && v->sequence == (uint16_t)seq)
                stamp = v->time;
        }
        free(e);
    }
    return stamp;
}
static void barrier(plat *a) {
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(a->conn, xcb_get_input_focus(a->conn), NULL);
    free(f);
}

/* #1: only a server-generated SelectionClear (real ownership change) may free our data. */
static void test_forged_clear(plat *a, sink *sa, rawp *r) {
    a->last_time = server_time(r);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "keep me", 7);
    sa->lost = 0;
    send_clear(r, a, a->last_time + 1000u);               /* forged by SendEvent: nobody took the selection */
    pump(a, sa, r, 10);
    CHECK(sa->lost == 0, "forged SelectionClear raised a loss event");
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME); xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && r->got_len == 7 && memcmp(r->got, "keep me", 7) == 0, "forged SelectionClear destroyed the clipboard data");
    rp_own(r, OWN_NONE, NULL, 0, 0);                       /* a genuine takeover generates a real Clear */
    pump_until(a, sa, r, NULL, &sa->lost, 1000);
    CHECK(sa->lost == 1, "genuine ownership transfer must still release (lost %d)", sa->lost);
}

/* #4: every local completion sees its own selection's data, also with callback-initiated requests. */
typedef struct loc_sink { plat *a; unsigned done[2]; bool bad, chained; } loc_sink;
static void on_loc(void *ud, const plat_event *e) {
    loc_sink *s = ud;
    if (e->kind != PLAT_EV_CLIPBOARD || e->code != 0) return;
    size_t n = 0; const uint8_t *d = plat_clip_data(s->a, &n);
    const char *want = e->clip_which == 0 ? "clipboard" : "primary"; size_t wn = strlen(want);
    if (!d || n != wn || memcmp(d, want, wn)) s->bad = true;
    s->done[e->clip_which]++;
    if (!s->chained && e->clip_which == 0) { s->chained = true; plat_clip_request(s->a, PLAT_CLIP_PRIMARY); }
}
static void test_local_completion_data(plat *a, sink *sa, rawp *r) {
    a->last_time = 0;
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "clipboard", 9);
    set_and_confirm(a, sa, r, PLAT_CLIP_PRIMARY, "primary", 7);
    loc_sink s = { .a = a }; plat_callbacks cb = { &s, on_loc, NULL, NULL, NULL };
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD); plat_clip_request(a, PLAT_CLIP_PRIMARY);
    uint64_t end = now_ms() + 2000;
    while (now_ms() < end && (s.done[0] < 1 || s.done[1] < 2)) { plat_run_for(a, &cb, 4); rp_step(r); }
    CHECK(s.done[0] == 1 && s.done[1] == 2, "local completions delivered (%u/%u)", s.done[0], s.done[1]);
    CHECK(!s.bad, "a local completion exposed another selection's data");
}

/* #5: a takeover that lands after the confirmation query must not be forgotten. */
static void test_takeover_after_confirmation(plat *a, sink *sa, rawp *r) {
    a->last_time = server_time(r);
    sa->lost = sa->arrived = sa->failed = 0;
    plat_clip_set(a, PLAT_CLIP_CLIPBOARD, "mine", 4);
    barrier(a);                                            /* our claim and owner query are answered (owner = us) */
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);             /* queued behind the confirmation */
    rp_own(r, OWN_PLAIN, "theirs", 6, 0);                  /* the takeover and its Clear arrive after the reply */
    barrier(a);
    pump_until(a, sa, r, NULL, &sa->arrived, 2000);
    size_t n = 0; const uint8_t *d = plat_clip_data(a, &n);
    CHECK(sa->lost == 1, "takeover after confirmation reports the loss (lost %d)", sa->lost);
    CHECK(sa->arrived == 1 && d && n == 6 && memcmp(d, "theirs", 6) == 0, "paste after takeover returned stale data (%zu bytes)", n);
    a->last_time = 0;
}

/* #6: an INCR transfer that ends below its advertised lower bound is a failure. */
static void test_incr_short(plat *a, sink *sa, rawp *r) {
    struct { int mode; const char *what; size_t n; uint32_t lb; } cs[] = {
        { OWN_INCR, "one byte below a 4096 lower bound", 1, 4096 },
        { OWN_INCR, "immediate terminator below the lower bound", 0, 4096 },
        { OWN_STRING_INCR, "STRING transfer below the lower bound", 1, 4096 },
        { OWN_INCR, "control: exactly the lower bound", 1, 1 },
    };
    for (unsigned i = 0; i < 4; i++) {
        r->lb_force = cs[i].lb;
        rp_own(r, (unsigned)cs[i].mode, "x", cs[i].n, 1); pump(a, sa, r, 3);
        sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
        uint64_t end = now_ms() + 2000;
        while (now_ms() < end && !sa->arrived && !sa->failed) pump(a, sa, r, 1);
        pump(a, sa, r, 3);
        if (i < 3) CHECK(sa->failed == 1 && !sa->arrived, "INCR %s must fail (ok %d, failed %d)", cs[i].what, sa->arrived, sa->failed);
        else CHECK(sa->arrived == 1 && !sa->failed, "INCR %s must succeed (ok %d, failed %d)", cs[i].what, sa->arrived, sa->failed);
        CHECK(x11_clip_busy(a) == 0, "INCR short case %u released", i);
    }
    r->lb_force = 0;
}

/* #13: bulk clipboard work is bounded per slice; local pastes share the immutable blob. */
#define SLICE_LIMIT (1024u * 1024u)
static void test_bulk_slices(plat *a, sink *sa, rawp *r) {
    a->last_time = 0; x11_clip_set_limits(a, 256 * 1024, 5000, 0);
    size_t big = 20u * 1024u * 1024u; uint8_t *d = pattern(big);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, d, big);
    (void)x11_clip_max_slice(a, true);
    sa->arrived = sa->failed = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 2000);
    size_t n = 0; const uint8_t *g = plat_clip_data(a, &n);
    CHECK(sa->arrived == 1 && n == big && g && memcmp(g, d, big) == 0, "20 MiB local paste delivered");
    size_t m = x11_clip_max_slice(a, true);
    CHECK(m <= SLICE_LIMIT, "local paste moved %zu bytes in one slice (limit %u)", m, SLICE_LIMIT);
    free(d);
    /* MULTIPLE with 64 direct conversions of ~200 KB each */
    size_t each = 200000; d = pattern(each);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, d, each);
    xcb_atom_t pairs[128], props[64]; char nm[24];
    for (unsigned i = 0; i < 64; i++) { snprintf(nm, sizeof nm, "RP_BULK%u", i); props[i] = intern(r->c, nm); pairs[2 * i] = r->utf8; pairs[2 * i + 1] = props[i]; }
    rp_reset_req(r);
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p1, r->atom_pair, 32, 128, pairs);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p1, XCB_CURRENT_TIME); xcb_flush(r->c);
    (void)x11_clip_max_slice(a, true);
    pump_until(a, sa, r, &r->done, NULL, 10000);
    m = x11_clip_max_slice(a, true);
    CHECK(r->done && !r->failed, "64-pair MULTIPLE answered");
    CHECK(m <= SLICE_LIMIT + 256u * 1024u, "MULTIPLE moved %zu bytes in one slice (limit %u)", m, SLICE_LIMIT + 256u * 1024u);
    for (unsigned i = 0; i < 64; i += 21) {
        xcb_get_property_reply_t *g2 = xcb_get_property_reply(r->c,
            xcb_get_property(r->c, 0, r->win, props[i], XCB_GET_PROPERTY_TYPE_ANY, 0, 0), NULL);
        CHECK(g2 && g2->bytes_after == each, "bulk property %u complete (%u)", i, g2 ? g2->bytes_after : 0u);
        free(g2);
    }
    free(d);
    pump(a, sa, r, 3);
    CHECK(x11_clip_busy(a) == 0, "bulk MULTIPLE released");
}

/* #14: one budget covers owned blobs, the paste buffer and receive buffers. */
static void test_memory_budget(plat *a, sink *sa, rawp *r) {
    (void)a; (void)sa; (void)r;
    plat b; if (!open_plat(&b)) { CHECK(false, "second plat"); return; }
    sink sb; memset(&sb, 0, sizeof sb);
    rawp primary; rp_open(&primary); primary.clipboard = XCB_ATOM_PRIMARY;
    x11_clip_set_budget(&b, 1024u * 1024u);
    uint8_t *d = pattern(600000);
    CHECK(plat_clip_set(&b, PLAT_CLIP_CLIPBOARD, d, 600000) == PLAT_OK, "first 600 KB set within 1 MiB");
    CHECK(plat_clip_set(&b, PLAT_CLIP_PRIMARY, d, 600000) == PLAT_ERR_FAIL, "second 600 KB set must exceed the 1 MiB budget");
    pump(&b, &sb, &primary, 3);
    size_t m0 = x11_clip_mem(&b);
    CHECK(m0 >= 600000 && m0 < 700000, "mem counts the owned blob (%zu)", m0);
    uint8_t *pd = pattern(700000);
    rp_own(&primary, OWN_PLAIN, pd, 700000, 0); pump(&b, &sb, &primary, 3);
    plat_clip_request(&b, PLAT_CLIP_PRIMARY);
    uint64_t end = now_ms() + 3000;
    while (now_ms() < end && !sb.arrived && !sb.failed) pump(&b, &sb, &primary, 1);
    CHECK(sb.failed == 1 && !sb.arrived, "a receive that would exceed the budget must fail (ok %d, failed %d)", sb.arrived, sb.failed);
    pump(&b, &sb, &primary, 3);
    CHECK(x11_clip_mem(&b) == m0, "failed receive released its buffer (%zu vs %zu)", x11_clip_mem(&b), m0);
    /* a local paste shares the owned blob: no second copy */
    set_and_confirm(&b, &sb, &primary, PLAT_CLIP_CLIPBOARD, d, 600000);
    size_t m1 = x11_clip_mem(&b);
    sb.arrived = sb.failed = 0; plat_clip_request(&b, PLAT_CLIP_CLIPBOARD);
    pump_until(&b, &sb, NULL, NULL, &sb.arrived, 1000);
    CHECK(sb.arrived == 1 && x11_clip_mem(&b) <= m1 + 8, "local paste duplicated the blob (%zu -> %zu)", m1, x11_clip_mem(&b));
    free(d); free(pd); rp_close(&primary); plat_shutdown(&b);
}

/* P2.2h (#13 rest): receive growth, copy and Latin-1 conversion run on a worker. While a 64 MiB selection arrives the
 * UI thread only hands chunks over: its bulk bytes stay under one slice budget, each poll stays short, memory is exact. */
#include "base/base.h"
/* G1 typing budget 2.0 ms p99 for the release build; ASan's allocator (quarantine recycling of huge blocks inside the
 * UI thread's own malloc calls) inflates the sanitizer run, which therefore only guards against multi-ms regressions. */
#if defined(__SANITIZE_ADDRESS__)
#define POLL_CPU_LIMIT_NS 20000000u
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define POLL_CPU_LIMIT_NS 20000000u
#endif
#endif
#ifndef POLL_CPU_LIMIT_NS
#define POLL_CPU_LIMIT_NS 2000000u
#endif
static void recv_big(plat *a, sink *sa, rawp *r, unsigned mode, const uint8_t *d, size_t n, size_t expect, const char *what) {
    rp_own(r, mode, d, n, 256 * 1024); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0;
    (void)x11_clip_ui_bytes(a, true); (void)x11_clip_max_poll_ns(a, true, NULL);
    CHECK(plat_clip_request(a, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "%s: request", what);
    uint64_t t0 = now_ms(), end = t0 + 90000;
    while (!sa->arrived && !sa->failed && now_ms() < end) pump(a, sa, r, 1);
    uint64_t took = now_ms() - t0;
    size_t ui = x11_clip_ui_bytes(a, true); uint64_t cpu = 0, worst = x11_clip_max_poll_ns(a, true, &cpu);
    size_t got = 0; const uint8_t *g = plat_clip_data(a, &got);
    CHECK(sa->arrived == 1 && !sa->failed && g && got == expect, "%s: arrived %d failed %d, %zu of %zu bytes", what, sa->arrived, sa->failed, got, expect);
    for (int i = 0; i < 400 && x11_clip_busy(a); i++) pump(a, sa, r, 1);     /* deferred frees of the previous paste */
    CHECK(x11_clip_mem(a) == expect, "%s: mem %zu is not exactly the %zu byte buffer", what, x11_clip_mem(a), expect);
    CHECK(x11_clip_busy(a) == 0, "%s: receive released", what);
    CHECK(ui <= SLICE_LIMIT, "%s: UI thread touched %zu bulk bytes (limit %u)", what, ui, SLICE_LIMIT);
    CHECK(cpu <= POLL_CPU_LIMIT_NS, "%s: worst UI poll CPU %.3f ms (limit %.1f ms)", what, (double)cpu / 1e6, (double)POLL_CPU_LIMIT_NS / 1e6);
    printf("x11_clip_test: %s: %zu MiB in %llu ms, UI bulk bytes %zu, worst UI poll %.3f ms wall / %.3f ms CPU (G1 typing budget 1.0 ms p50 / 2.0 ms p99)\n",
           what, expect >> 20, (unsigned long long)took, ui, (double)worst / 1e6, (double)cpu / 1e6);
}
static void test_large_paste_latency(plat *a, sink *sa, rawp *r) {
    a->last_time = 0; x11_clip_set_limits(a, 256 * 1024, 30000, 0);
    size_t n = 64u * 1024u * 1024u; uint8_t *d = pattern(n);
    recv_big(a, sa, r, OWN_INCR, d, n, n, "64 MiB UTF8 INCR");
    size_t g_len = 0; const uint8_t *g = plat_clip_data(a, &g_len);
    CHECK(g && g_len == n && memcmp(g, d, n) == 0, "64 MiB receive is byte identical");
    /* typing while a paste is pending: a poll with input queued allocates nothing and does no clip work */
    plat_event key = { .kind = PLAT_EV_KEY };
    CHECK(x11_push_event(a, &key), "queue a key");
    if (edit_malloc_guard_active()) {
        edit_malloc_guard_begin(); (void)x11_clip_poll(a); size_t allocs = edit_malloc_guard_end();
        CHECK(allocs == 0, "poll with typing queued made %zu allocations", allocs);
    } else (void)x11_clip_poll(a);
    pump(a, sa, r, 2);
    free(d);
    /* Latin-1 expansion (STRING INCR) doubles the size: 8 MiB of 0xe9 -> 16 MiB, converted off the UI thread */
    size_t m = 8u * 1024u * 1024u; d = malloc(m); memset(d, 0xe9, m);
    recv_big(a, sa, r, OWN_STRING_INCR, d, m, 2 * m, "8 MiB Latin-1 INCR");
    g = plat_clip_data(a, &g_len);
    bool ok = g && g_len == 2 * m;
    for (size_t i = 0; ok && i < g_len; i += 2) if (g[i] != 0xc3 || g[i + 1] != 0xa9) ok = false;
    CHECK(ok, "Latin-1 expansion correct");
    free(d);
    /* the worker enforces the 64 MiB limit on the expanded size: 36 MiB of 0xe9 would become 72 MiB */
    size_t big = 36u * 1024u * 1024u; d = malloc(big); memset(d, 0xe9, big);
    size_t before = x11_clip_mem(a);
    rp_own(r, OWN_STRING_INCR, d, big, 256 * 1024); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    uint64_t end = now_ms() + 60000;
    while (now_ms() < end && !sa->failed && !sa->arrived) pump(a, sa, r, 1);
    CHECK(sa->failed == 1 && !sa->arrived, "expansion beyond 64 MiB must fail (ok %d, failed %d)", sa->arrived, sa->failed);
    for (int i = 0; i < 1000 && (r->owner_busy || x11_clip_busy(a)); i++) pump(a, sa, r, 1);
    CHECK(x11_clip_busy(a) == 0 && x11_clip_mem(a) == before, "failed worker receive released its buffer (%zu vs %zu)", x11_clip_mem(a), before);
    free(d);
    x11_clip_set_limits(a, 0, 5000, 0);
    rp_own(r, OWN_NONE, NULL, 0, 0); pump(a, sa, r, 3);
}

/* Large owners hand over a buffer filled elsewhere: adopting it moves no bytes on the UI thread. */
static void test_zero_copy_set(plat *a, sink *sa, rawp *r) {
    a->last_time = 0;
    size_t n = 20u * 1024u * 1024u;
    x11_clip_buf *b = x11_clip_buf_new(n);
    CHECK(b != NULL, "buf_new");
    if (!b) return;
    uint8_t *src = pattern(n); memcpy(x11_clip_buf_data(b), src, n);     /* the producer's copy, off the clip path */
    (void)x11_clip_ui_bytes(a, true);
    CHECK(x11_clip_set_buf(a, PLAT_CLIP_CLIPBOARD, b) == PLAT_OK, "set_buf");
    CHECK(x11_clip_ui_bytes(a, true) == 0, "adopting a buffer moved bytes on the UI thread");
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(a->conn, xcb_get_input_focus(a->conn), NULL); free(f);
    pump(a, sa, r, 3);
    CHECK(x11_clip_mem(a) >= n && x11_clip_mem(a) < n + 8192, "adopted buffer is accounted (%zu)", x11_clip_mem(a));
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 2000);
    size_t got = 0; const uint8_t *g = plat_clip_data(a, &got);
    CHECK(sa->arrived == 1 && got == n && g && memcmp(g, src, n) == 0, "adopted buffer pastes back identical");
    x11_clip_set_budget(a, n - 1);
    x11_clip_buf *b2 = x11_clip_buf_new(n);
    CHECK(x11_clip_set_buf(a, PLAT_CLIP_PRIMARY, b2) == PLAT_ERR_FAIL, "over-budget adopt fails (and consumes the buffer)");
    x11_clip_set_budget(a, 128u * 1024u * 1024u);
    free(src);
    CHECK(plat_clip_set(a, PLAT_CLIP_CLIPBOARD, "x", 1) == PLAT_OK, "plain set still works");
    pump(a, sa, r, 3);
}

/* #15: the STRING fallback gets a fresh conversion deadline. */
static void test_string_fallback_deadline(plat *a, sink *sa, rawp *r) {
    static const uint8_t lat[] = { 'c', 'a', 'f', 0xe9 };
    x11_clip_set_limits(a, 0, 600, 0);
    rp_own(r, OWN_NO_UTF8, lat, sizeof lat, 0); pump(a, sa, r, 3);
    r->delay_ms = 400;                                      /* refusal at ~400 ms, STRING reply ~400 ms later */
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    uint64_t end = now_ms() + 3000;
    while (now_ms() < end && !sa->arrived && !sa->failed) pump(a, sa, r, 1);
    size_t n = 0; const uint8_t *g = plat_clip_data(a, &n);
    CHECK(sa->arrived == 1 && !sa->failed && g && n == 5 && memcmp(g, "caf\xc3\xa9", 5) == 0,
          "late UTF8 refusal then timely STRING must succeed (ok %d, failed %d)", sa->arrived, sa->failed);
    r->delay_ms = 0; r->dq_at = 0; x11_clip_set_limits(a, 0, 5000, 0);
}

/* #28: notifications for identical MULTIPLE tuples keep arrival order. */
static void test_multiple_order(plat *a, sink *sa, rawp *r) {
    a->last_time = server_time(r);
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "ordered", 7);
    xcb_atom_t good[2] = { r->utf8, r->p4 };
    rp_reset_req(r);
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p1, r->string, 8, 1, "x");   /* malformed */
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p2, r->atom_pair, 32, 2, good);
    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->p3, r->string, 8, 1, "y");   /* malformed */
    uint32_t t = a->last_time;
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p1, t);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p2, t);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->multiple, r->p3, t);
    xcb_flush(r->c);
    uint64_t end = now_ms() + 3000;
    while (now_ms() < end && r->nn < 3) pump(a, sa, r, 1);
    CHECK(r->nn == 3 && r->nprops[0] == XCB_ATOM_NONE && r->nprops[1] == r->p2 && r->nprops[2] == XCB_ATOM_NONE,
          "MULTIPLE notifications out of order: %u %u %u (want none, p2, none)", r->nprops[0], r->nprops[1], r->nprops[2]);
    a->last_time = 0;
}

/* ---- clipboard manager ---- */
typedef struct mgr {
    pthread_t th;
    int ready[2];
    bool answer, got_request, saved_ok;
    uint64_t asked_ms, saved_ms;
    uint8_t *saved; size_t saved_len;
    rawp r;
    plat *a;
} mgr;

static void *mgr_main(void *ud) {
    mgr *m = ud;
    rawp *r = &m->r;
    rp_open(r);
    xcb_set_selection_owner(r->c, r->win, r->manager, XCB_CURRENT_TIME);
    xcb_flush(r->c);
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(r->c, xcb_get_input_focus(r->c), NULL);
    free(f);
    char b = 'r';
    if (write(m->ready[1], &b, 1) != 1) return NULL;
    uint64_t end = now_ms() + 3000;
    xcb_window_t save_req = 0; xcb_atom_t save_sel = 0, save_target = 0, save_prop = 0;
    bool asked = false;
    while (now_ms() < end && !r->done) {
        xcb_generic_event_t *e = xcb_poll_for_event(r->c);
        if (!e) { struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); continue; }
        uint8_t t = e->response_type & 0x7f;
        if (t == XCB_SELECTION_REQUEST) {
            const xcb_selection_request_event_t *q = (const xcb_selection_request_event_t *)e;
            if (q->target == r->save && m->answer && !asked) {
                m->got_request = true; m->asked_ms = now_ms(); asked = true;
                save_req = q->requestor; save_sel = q->selection; save_target = q->target; save_prop = q->property;
                xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME);   /* fetch the data */
                xcb_flush(r->c);
            } else if (q->target == r->save) m->got_request = true;
        } else if (t == XCB_SELECTION_NOTIFY && asked) {
            const xcb_selection_notify_event_t *n = (const xcb_selection_notify_event_t *)e;
            if (n->selection == r->clipboard) {
                if (n->property != XCB_ATOM_NONE) rp_read_prop(r, n->property, false);
                if (r->is_incr) { /* chunks arrive as PropertyNotify */ }
                else { r->done = true; }
            }
        } else if (t == XCB_PROPERTY_NOTIFY && asked && r->is_incr) {
            const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)e;
            if (n->window == r->win && n->state == XCB_PROPERTY_NEW_VALUE) rp_read_prop(r, n->atom, true);
        }
        free(e);
        if (r->done && asked) {
            m->saved_ms = now_ms();
            m->saved = r->got; m->saved_len = r->got_len; r->got = NULL; m->saved_ok = r->got_len > 0;
            rp_send_notify(r, save_req, save_sel, save_target, save_prop, XCB_CURRENT_TIME);
            /* A flush sends bytes; a barrier proves X processed the completion before
             * this fake manager disconnects and destroys its resources. */
            xcb_get_input_focus_reply_t *barrier = xcb_get_input_focus_reply(r->c, xcb_get_input_focus(r->c), NULL);
            free(barrier);
        }
    }
    rp_close(r);
    return NULL;
}

static void test_manager_save(int which, const char *label) {
    plat a;
    if (!open_plat(&a)) { CHECK(0, "open"); return; }
    sink sa; memset(&sa, 0, sizeof sa);
    /* a real manager on this display: never fight it */
    rawp probe;
    rp_open(&probe);
    xcb_get_selection_owner_reply_t *o = xcb_get_selection_owner_reply(probe.c, xcb_get_selection_owner(probe.c, probe.manager), NULL);
    bool taken = o && o->owner != 0;
    free(o);
    rp_close(&probe);
    if (taken) { printf("x11_clip_test: %s skipped (a CLIPBOARD_MANAGER already runs on this display)\n", label); plat_shutdown(&a); return; }
    mgr m;
    memset(&m, 0, sizeof m);
    m.answer = which == 0;
    m.a = &a;
    if (pipe(m.ready) || pthread_create(&m.th, NULL, mgr_main, &m)) { CHECK(0, "thread"); plat_shutdown(&a); return; }
    char b;
    CHECK(read(m.ready[0], &b, 1) == 1, "manager up");
    size_t n = 3000;
    uint8_t *d = pattern(n);
    x11_clip_set_limits(&a, 700, 0, which == 0 ? 2000 : 150);     /* INCR on the way to the manager too */
    set_and_confirm(&a, &sa, NULL, PLAT_CLIP_CLIPBOARD, d, n);
    uint64_t t0 = now_ms();
    plat_shutdown(&a);
    uint64_t took = now_ms() - t0;
    pthread_join(m.th, NULL);
    CHECK(m.got_request, "%s: manager was asked to SAVE_TARGETS", label);
    if (which == 0) {
        CHECK(m.saved_ok && m.saved_len == n && memcmp(m.saved, d, n) == 0, "%s: manager read all %zu bytes (got %zu)", label, n, m.saved_len);
        CHECK(took < 1500, "%s: shutdown returned once the manager confirmed (%llu ms, request +%llu, saved +%llu)", label, (unsigned long long)took, (unsigned long long)(m.asked_ms - t0), (unsigned long long)(m.saved_ms - t0));
    } else {
        CHECK(took >= 100 && took < 1000, "%s: shutdown wait bounded by the 150 ms limit (%llu ms)", label, (unsigned long long)took);
    }
    free(m.saved); free(d);
    close(m.ready[0]); close(m.ready[1]);
}

static void test_no_manager_fast(void) {
    plat a;
    if (!open_plat(&a)) { CHECK(0, "open"); return; }
    sink sa; memset(&sa, 0, sizeof sa);
    x11_clip_set_limits(&a, 0, 0, 2000);
    rawp probe;
    rp_open(&probe);
    xcb_get_selection_owner_reply_t *o = xcb_get_selection_owner_reply(probe.c, xcb_get_selection_owner(probe.c, probe.manager), NULL);
    bool taken = o && o->owner != 0;
    free(o);
    rp_close(&probe);
    set_and_confirm(&a, &sa, NULL, PLAT_CLIP_CLIPBOARD, "x", 1);
    if (taken) x11_clip_set_limits(&a, 0, 0, UINT32_MAX);      /* do not hand test data to a real manager */
    uint64_t t0 = now_ms();
    plat_shutdown(&a);
    uint64_t took = now_ms() - t0;
    CHECK(took < 500, "no manager: shutdown does not wait (%llu ms)", (unsigned long long)took);
}

/* A 60 ms deadline must wake an otherwise idle poll, with no 4 ms pump driving it. */
static void on_timeout(void *ud, const plat_event *e) {
    plat *a = ud;
    if (e->kind == PLAT_EV_CLIPBOARD && e->code == 1) plat_quit(a);
}
static void test_idle_timeout(plat *a, sink *sa, rawp *r) {
    rp_own(r, OWN_SILENT, NULL, 0, 0); pump(a, sa, r, 3);
    x11_clip_set_limits(a, 0, 60, 0);
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    plat_callbacks cb = { a, on_timeout, NULL, NULL, NULL };
    uint64_t start = now_ms();
    plat_run_for(a, &cb, 1000);
    uint64_t elapsed = now_ms() - start;
    CHECK(a->quit && elapsed >= 30 && elapsed < 500, "idle poll wakes for request timeout (%llu ms)", (unsigned long long)elapsed);
    a->quit = false; x11_clip_set_limits(a, 0, 5000, 0);
}
/* A failed owner confirmation must retain bytes if the queued claim succeeds later. */
static void test_owner_confirmation_timeout(plat *a, sink *sa, rawp *r) {
    a->last_time = 0;
    set_and_confirm(a, sa, r, PLAT_CLIP_CLIPBOARD, "before", 6);
    xcb_grab_server(r->c);
    xcb_get_input_focus_reply_t *f = xcb_get_input_focus_reply(r->c, xcb_get_input_focus(r->c), NULL); free(f);
    /* The claim/query are delayed by the grab. Even if the stale claim is rejected later,
     * X still lists us as the real owner; uncertainty must not free the replacement bytes. */
    a->last_time = 1;
    x11_clip_set_limits(a, 0, 60, 0);
    sa->lost = 0;
    plat_clip_set(a, PLAT_CLIP_CLIPBOARD, "after timeout", 13);
    plat_callbacks cb = { sa, on_ev, NULL, NULL, NULL };
    plat_run_for(a, &cb, 120);
    CHECK(sa->lost == 1, "owner confirmation times out");
    xcb_ungrab_server(r->c); xcb_flush(r->c);
    pump(a, sa, r, 5);
    send_clear(r, a, 2); /* newer than the failed claim's 1, but older than actual ownership */
    pump(a, sa, r, 3);
    rp_reset_req(r);
    xcb_convert_selection(r->c, r->win, r->clipboard, r->utf8, r->p1, XCB_CURRENT_TIME); xcb_flush(r->c);
    pump_until(a, sa, r, &r->done, NULL, 1000);
    CHECK(r->done && r->got_len == 13 && memcmp(r->got, "after timeout", 13) == 0,
          "unconfirmed claim retains bytes while still the actual owner");
    x11_clip_set_limits(a, 0, 5000, 0); a->last_time = 0;
}
typedef struct dual_sink { plat *a; unsigned completed[2]; bool bad; } dual_sink;
static void on_dual(void *ud, const plat_event *e) {
    dual_sink *s = ud;
    if (e->kind != PLAT_EV_CLIPBOARD || e->code != 0) return;
    size_t n = 0; const uint8_t *d = plat_clip_data(s->a, &n);
    uint8_t want = e->clip_which == 0 ? 'c' : 'p';
    if (n != 4096 || !d) s->bad = true;
    else for (size_t i = 0; i < n; i++) if (d[i] != want) s->bad = true;
    s->completed[e->clip_which]++;
}
static void test_concurrent_receive(plat *a, sink *sa, rawp *r) {
    rawp primary; rp_open(&primary); primary.clipboard = XCB_ATOM_PRIMARY;
    uint8_t cdata[4096], pdata[4096]; memset(cdata, 'c', sizeof cdata); memset(pdata, 'p', sizeof pdata);
    rp_own(r, OWN_INCR, cdata, sizeof cdata, 512);
    rp_own(&primary, OWN_INCR, pdata, sizeof pdata, 512); pump(a, sa, r, 3);
    a->last_time = 0;
    plat_clip_request(a, PLAT_CLIP_CLIPBOARD); plat_clip_request(a, PLAT_CLIP_PRIMARY);
    dual_sink s = { .a = a }; plat_callbacks cb = { &s, on_dual, NULL, NULL, NULL };
    uint64_t end = now_ms() + 2000;
    while (now_ms() < end && (!s.completed[0] || !s.completed[1])) {
        plat_run_for(a, &cb, 4); rp_step(r); rp_step(&primary);
    }
    CHECK(!s.bad && s.completed[0] == 1 && s.completed[1] == 1, "concurrent PRIMARY/CLIPBOARD have distinct properties and callback data (%u/%u)", s.completed[0], s.completed[1]);
    rp_close(&primary);
}
static void test_paged_property(plat *a, sink *sa, rawp *r) {
    size_t n = 600001; uint8_t *d = pattern(n);
    rp_own(r, OWN_PLAIN, d, n, 0); pump(a, sa, r, 3);
    sa->arrived = sa->failed = 0; plat_clip_request(a, PLAT_CLIP_CLIPBOARD);
    pump_until(a, sa, r, NULL, &sa->arrived, 2000);
    size_t got; const uint8_t *v = plat_clip_data(a, &got);
    CHECK(sa->arrived == 1 && !sa->failed && got == n && v && memcmp(v, d, n) == 0,
          "large direct properties read in bounded pages (%zu/%zu)", got, n);
    free(d);
}

/* ---- focus (review MINOR #11, live) ---- */
static void send_focus(rawp *r, plat *a, bool in, uint8_t mode, uint8_t detail) {
    xcb_focus_in_event_t f;
    memset(&f, 0, sizeof f);
    f.response_type = in ? XCB_FOCUS_IN : XCB_FOCUS_OUT; f.detail = detail; f.event = a->win; f.mode = mode;
    xcb_send_event(r->c, 0, a->win, XCB_EVENT_MASK_FOCUS_CHANGE, (const char *)&f);
    xcb_flush(r->c);
}

static void test_focus_modes(plat *a, sink *sa, rawp *r) {
    x11_input *in = a->in;
    in->down[3] = 0x10;
    sa->focus = 0;
    send_focus(r, a, false, 1 /* NotifyGrab */, 3);
    pump(a, sa, r, 10);
    CHECK(sa->focus == 0 && in->down[3] == 0x10, "NotifyGrab FocusOut keeps held keys (focus events %d)", sa->focus);
    send_focus(r, a, true, 2 /* NotifyUngrab */, 3);
    pump(a, sa, r, 10);
    CHECK(sa->focus == 0 && in->down[3] == 0x10, "NotifyUngrab FocusIn keeps held keys");
    send_focus(r, a, false, 0, 5 /* NotifyPointer */);
    pump(a, sa, r, 10);
    CHECK(sa->focus == 0 && in->down[3] == 0x10, "pointer-detail focus ignored");
    send_focus(r, a, false, 0, 3 /* NotifyNonlinear */);
    pump(a, sa, r, 10);
    CHECK(sa->focus == 1 && in->down[3] == 0, "real focus loss reports and forgets held keys");
}

static void test_wire_decoders(void) {
    xcb_selection_notify_event_t notify = { .response_type = XCB_SELECTION_NOTIFY | 0x80, .requestor = 9,
        .selection = 10, .target = 11, .property = 12, .time = 13 };
    CHECK(x11_clip_notify_matches(&notify, 9, 10, 11, 12, 13), "conversion tuple matches synthetic notify");
    CHECK(!x11_clip_notify_matches(&notify, 8, 10, 11, 12, 13) &&
          !x11_clip_notify_matches(&notify, 9, 10, 8, 12, 13) &&
          !x11_clip_notify_matches(&notify, 9, 10, 11, 12, 8), "wrong requestor, target and timestamp rejected");
    notify.property = 0; CHECK(x11_clip_notify_matches(&notify, 9, 10, 11, 12, 13), "solicited failure matches");
    uint8_t invalid[512], converted[512]; memset(invalid, 0x80, sizeof invalid);
    CHECK(x11_utf8_to_latin1(invalid, sizeof invalid, converted) == sizeof invalid,
          "malformed continuation bytes consume bounded work per STRING output byte");
    uint8_t bytes[48] = {0}; xcb_get_property_reply_t h = {0};
    h.response_type = 1; h.type = 100; h.format = 32; h.length = 1; h.value_len = 1;
    memcpy(bytes, &h, sizeof h); uint32_t lower = 4096; memcpy(bytes + 32, &lower, 4);
    x11_clip_property v;
    CHECK(!x11_clip_decode_property(bytes, 31, &v), "short header rejected");
    CHECK(!x11_clip_decode_property(bytes, 35, &v), "short payload rejected");
    CHECK(x11_clip_decode_property(bytes, 36, &v) &&
          x11_clip_decode_transfer(&v, 101, 102, 100, 0, true) == 1, "valid INCR header parsed");
    CHECK(x11_clip_decode_transfer(&v, 101, 102, 100, 0, false) == -1, "nested INCR rejected");
    lower = UINT32_MAX; memcpy(bytes + 32, &lower, 4);
    CHECK(x11_clip_decode_property(bytes, 36, &v) &&
          x11_clip_decode_transfer(&v, 101, 102, 100, 0, true) == -1, "unbounded INCR lower bound rejected");
    h.format = 8; h.type = 101; h.value_len = 1; memcpy(bytes, &h, sizeof h);
    CHECK(x11_clip_decode_property(bytes, 36, &v) &&
          x11_clip_decode_transfer(&v, 101, 102, 100, 101, false) == 0, "valid text parsed");
    CHECK(x11_clip_decode_transfer(&v, 101, 102, 100, XCB_ATOM_STRING, false) == -1, "chunk type changed");
    h.type = 103; h.format = 32; h.value_len = 4; h.length = 4; memcpy(bytes, &h, sizeof h);
    xcb_atom_t pairs[4] = {101, 104, 102, 105}; memcpy(bytes + 32, pairs, sizeof pairs);
    size_t n = 0; xcb_atom_t out[4];
    CHECK(x11_clip_decode_property(bytes, sizeof bytes, &v) &&
          x11_clip_decode_pairs(&v, 103, out, 2, &n) && n == 2 && memcmp(out, pairs, sizeof pairs) == 0,
          "bounded MULTIPLE pairs parsed");
    CHECK(!x11_clip_decode_pairs(&v, 103, out, 1, &n), "too many pairs rejected");
    v.len = 12; CHECK(!x11_clip_decode_pairs(&v, 103, out, 2, &n), "odd MULTIPLE rejected");
    v.len = 16; v.after = 8; CHECK(!x11_clip_decode_pairs(&v, 103, out, 2, &n), "truncated MULTIPLE rejected");
}

int main(void) {
    test_wire_decoders();
    if (g_fail) return 1;
    if (!xvfb_start()) {
        if (getenv("EDIT_X11_STRICT")) { fprintf(stderr, "x11_clip_test: FAIL (private Xvfb unavailable)\n"); return 1; }
        puts("x11_clip_test: skipped (private Xvfb unavailable)"); return 0;
    }
    trace_init();
    trace_thread_register();
    plat a;
    if (!open_plat(&a)) { fprintf(stderr, "x11_clip_test: FAIL (plat_init after private Xvfb startup)\n"); return 1; }
    sink sa; memset(&sa, 0, sizeof sa);
    rawp r;
    rp_open(&r);
    pump(&a, &sa, &r, 3);
    test_serve_incr(&a, &sa, &r);
    test_large_and_snapshot(&a, &sa, &r);
    test_serve_stalled_requester(&a, &sa, &r);
    test_multiple(&a, &sa, &r);
    test_receive_incr(&a, &sa, &r);
    test_receive_string_fallback(&a, &sa, &r);
    test_receive_string_incr_and_reject(&a, &sa, &r);
    test_receive_unsolicited(&a, &sa, &r);
    test_receive_timeouts(&a, &sa, &r);
    test_late_notify_and_old_input_time(&a, &sa, &r);
    test_idle_timeout(&a, &sa, &r);
    test_concurrent_receive(&a, &sa, &r);
    test_paged_property(&a, &sa, &r);
    test_owner_confirmation_timeout(&a, &sa, &r);
    test_stale_events(&a, &sa, &r);
    test_focus_modes(&a, &sa, &r);
    test_forged_clear(&a, &sa, &r);
    test_local_completion_data(&a, &sa, &r);
    test_takeover_after_confirmation(&a, &sa, &r);
    test_incr_short(&a, &sa, &r);
    test_bulk_slices(&a, &sa, &r);
    test_string_fallback_deadline(&a, &sa, &r);
    test_multiple_order(&a, &sa, &r);
    test_memory_budget(&a, &sa, &r);
    test_zero_copy_set(&a, &sa, &r);
    test_large_paste_latency(&a, &sa, &r);
    rp_close(&r);
    plat_shutdown(&a);
    test_manager_save(0, "manager saves");
    test_manager_save(1, "manager silent");
    test_no_manager_fast();
    if (g_fail) { puts("x11_clip_test: FAILED"); return 1; }
    puts("x11_clip_test: ok");
    return 0;
}
