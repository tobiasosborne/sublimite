/* X selections: bounded, asynchronous ICCCM INCR and MULTIPLE (P2.2b). */
#include "clip.h"
#include "input.h"
#include "trace/trace.h"
#include <xcb/xcbext.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <limits.h>

#define C(p) ((xcb_connection_t *)(p)->conn)
#define NSEL 2
#define NSERVE 16u
#define NJOB 8u
#define MAX_PAIRS 64u
#define CHUNK_MAX (256u * 1024u)
#define DATA_MAX (64u * 1024u * 1024u)
#define OWN_BUDGET (128u * 1024u * 1024u)
#define MAX_WAITERS 256u

typedef struct clip_blob { size_t refs, len; uint8_t data[]; } clip_blob;
typedef struct clip_receive {
    uint32_t state;                  /* 0 idle, 1 notify, 2 property reply, 3 INCR chunk */
    uint32_t waiters;
    xcb_atom_t target, type;
    xcb_window_t win;
    uint32_t time, offset;
    bool incr, draining;
    uint64_t drain_end;
    xcb_get_property_cookie_t cookie;
    uint64_t deadline;
    uint8_t *data;
    size_t len, cap;
} clip_receive;
struct clip_job;
typedef struct clip_serve {
    clip_blob *blob;
    struct clip_job *job;
    xcb_window_t win;
    xcb_atom_t prop, target;
    uint64_t offset;
    uint64_t deadline;
    bool deleted, zero, checking;
    uint32_t write, barrier;
} clip_serve;
typedef struct clip_job {
    uint32_t state;                  /* 0 free, 1 MULTIPLE property, 2 checked writes */
    xcb_selection_request_event_t rq;
    clip_blob *blob;
    uint32_t stamp;
    uint64_t deadline;
    uint32_t cookie, barrier;
    uint32_t checks[MAX_PAIRS + 1u], nchecks;
    int32_t slots[MAX_PAIRS];
    xcb_atom_t pairs[MAX_PAIRS * 2u];
    size_t npairs;
    bool multiple;
} clip_job;
typedef struct x11_clip {
    clip_blob *own[NSEL];
    size_t own_bytes, max_chunk, chunk;
    uint32_t own_time[NSEL];
    bool confirmed[NSEL], retried[NSEL];
    bool uncertain[NSEL];
    uint32_t claiming[NSEL];          /* 1 timestamp property, 2 owner reply */
    uint32_t owner_cookie[NSEL], stamp_cookie[NSEL];
    uint64_t owner_deadline[NSEL];
    uint32_t local_requests[NSEL];
    bool clear_deferred[NSEL], clear_pending[NSEL];
    uint32_t clear_cookie[NSEL];
    uint64_t clear_deadline[NSEL];
    clip_receive rx[NSEL];
    clip_serve tx[NSERVE];
    clip_job jobs[NJOB];
    uint32_t timeout_ms, save_ms;
    uint8_t *got;
    size_t got_len;
    xcb_atom_t sel[NSEL], prop[NSEL], claim_prop[NSEL];
    xcb_atom_t utf8, targets, textplain, incr, timestamp, multiple, atom_pair, manager, save, save_prop, null_type;
    bool saving, save_done;
    uint8_t scratch[CHUNK_MAX];
} x11_clip;
#define CL(p) ((x11_clip *)(p)->clip)

static uint64_t deadline_after(uint32_t ms) { return trace_now_ns() + (uint64_t)ms * UINT64_C(1000000); }
static bool older(uint32_t a, uint32_t b) { return a && b && (int32_t)(a - b) < 0; }
static int which_of(const x11_clip *c, xcb_atom_t sel) {
    for (int w = 0; w < NSEL; w++) if (c->sel[w] == sel) return w;
    return -1;
}
static void blob_unref(x11_clip *c, clip_blob *b) {
    if (b && --b->refs == 0) { c->own_bytes -= b->len; free(b); }
}
static void tx_free(plat *p, clip_serve *s) {
    if (s->checking) { xcb_discard_reply(C(p), s->write); xcb_discard_reply(C(p), s->barrier); }
    xcb_window_t win = s->win;
    blob_unref(CL(p), s->blob);
    memset(s, 0, sizeof *s);
    if (win && win != p->win) {
        bool other = false;
        for (unsigned i = 0; i < NSERVE; i++) if (CL(p)->tx[i].blob && CL(p)->tx[i].win == win) other = true;
        if (!other) { uint32_t mask = 0; xcb_change_window_attributes(C(p), win, XCB_CW_EVENT_MASK, &mask); }
    }
}
static void job_free(plat *p, clip_job *j) {
    if (j->state == 1) xcb_discard_reply(C(p), j->cookie);
    if (j->state == 2) {
        xcb_discard_reply(C(p), j->barrier);
        for (unsigned i = 0; i < j->nchecks; i++) xcb_discard_reply(C(p), j->checks[i]);
    }
    blob_unref(CL(p), j->blob);
    memset(j, 0, sizeof *j);
}

int x11_clip_init(plat *p) {
    x11_clip *c = calloc(1, sizeof *c);
    if (!c) return PLAT_ERR_FAIL;
    const char *names[] = { "CLIPBOARD", "UTF8_STRING", "TARGETS", "EDIT_SELECTION", "EDIT_PRIMARY",
        "text/plain;charset=utf-8", "INCR", "TIMESTAMP", "MULTIPLE", "ATOM_PAIR", "CLIPBOARD_MANAGER",
        "SAVE_TARGETS", "EDIT_SAVE_TARGETS", "NULL", "EDIT_CLAIM_CLIPBOARD", "EDIT_CLAIM_PRIMARY" };
    xcb_atom_t *dst[] = { &c->sel[0], &c->utf8, &c->targets, &c->prop[0], &c->prop[1], &c->textplain,
        &c->incr, &c->timestamp, &c->multiple, &c->atom_pair, &c->manager, &c->save, &c->save_prop,
        &c->null_type, &c->claim_prop[0], &c->claim_prop[1] };
    xcb_intern_atom_cookie_t ck[16];
    for (unsigned i = 0; i < 16; i++) ck[i] = xcb_intern_atom(C(p), 0, (uint16_t)strlen(names[i]), names[i]);
    bool ok = true;
    for (unsigned i = 0; i < 16; i++) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(C(p), ck[i], NULL);
        if (!r || !r->atom) ok = false;
        *dst[i] = r ? r->atom : XCB_ATOM_NONE;
        free(r);
    }
    if (!ok) { free(c); return PLAT_ERR_FAIL; }
    c->sel[1] = XCB_ATOM_PRIMARY;
    /* Negotiate once at init (P2.2c); chunks also fit servers without BIG-REQUESTS. */
    (void)xcb_get_maximum_request_length(C(p));
    c->max_chunk = (size_t)xcb_get_setup(C(p))->maximum_request_length * 4u - 64u;
    if (c->max_chunk > CHUNK_MAX) c->max_chunk = CHUNK_MAX;
    c->chunk = c->max_chunk;
    c->timeout_ms = 5000; c->save_ms = 1000;
    p->clip = c;
    return PLAT_OK;
}
void x11_clip_destroy(plat *p) {
    x11_clip *c = CL(p);
    if (!c) return;
    for (unsigned i = 0; i < NJOB; i++) job_free(p, &c->jobs[i]);
    for (unsigned i = 0; i < NSERVE; i++) tx_free(p, &c->tx[i]);
    for (int w = 0; w < NSEL; w++) {
        if (c->claiming[w] == 2) xcb_discard_reply(C(p), c->owner_cookie[w]);
        if (c->rx[w].state == 2) xcb_discard_reply(C(p), c->rx[w].cookie.sequence);
        if (c->clear_pending[w]) xcb_discard_reply(C(p), c->clear_cookie[w]);
        if (c->rx[w].win) xcb_destroy_window(C(p), c->rx[w].win);
        free(c->rx[w].data); blob_unref(c, c->own[w]);
    }
    free(c->got); free(c); p->clip = NULL;
}

/* Consume a whole UTF-8 sequence even when only its Latin-1 replacement fits the output chunk. */
static size_t latin_chunk(const uint8_t *s, size_t len, uint8_t *out, size_t cap, size_t *used) {
    size_t n = 0, i = 0;
    while (i < len && n < cap) {
        uint8_t b = s[i];
        if (b < 0x80) { out[n++] = b; i++; }
        else if ((b & 0xe0) == 0xc0 && i + 1 < len && (s[i + 1] & 0xc0) == 0x80) {
            uint32_t cp = ((uint32_t)(b & 0x1f) << 6) | (s[i + 1] & 0x3fu);
            out[n++] = cp >= 0x80 && cp <= 0xff ? (uint8_t)cp : (uint8_t)'?'; i += 2;
        } else {
            out[n++] = '?';
            size_t sequence = (b & 0xf0) == 0xe0 ? 3u : (b & 0xf8) == 0xf0 ? 4u : 1u;
            i++;
            for (size_t k = 1; k < sequence && i < len && (s[i] & 0xc0) == 0x80; k++) i++;
        }
    }
    *used = i; return n;
}
size_t x11_utf8_to_latin1(const uint8_t *s, size_t len, uint8_t *out) {
    size_t used; return latin_chunk(s, len, out, len, &used);
}
size_t x11_latin1_to_utf8(const uint8_t *s, size_t len, uint8_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < 0x80) out[n++] = s[i];
        else { out[n++] = (uint8_t)(0xc0 | (s[i] >> 6)); out[n++] = (uint8_t)(0x80 | (s[i] & 0x3f)); }
    }
    return n;
}

bool x11_clip_notify_matches(const xcb_selection_notify_event_t *n, xcb_window_t win, xcb_atom_t selection,
                             xcb_atom_t target, xcb_atom_t property, uint32_t time) {
    return (n->response_type & 0x7f) == XCB_SELECTION_NOTIFY && n->requestor == win && n->selection == selection &&
           n->target == target && n->time == time && (!n->property || n->property == property);
}

/* The only reply parsing entry point: also exercised with exact-size fuzz allocations. */
bool x11_clip_decode_property(const uint8_t *bytes, size_t size, x11_clip_property *v) {
    memset(v, 0, sizeof *v);
    if (size < sizeof(xcb_get_property_reply_t)) return false;
    xcb_get_property_reply_t h;
    memcpy(&h, bytes, sizeof h);
    if (h.response_type != 1 || (h.format != 8 && h.format != 32) || !h.type) return false;
    size_t stride = h.format / 8u;
    if ((uint64_t)h.length * 4u > size - sizeof h || (uint64_t)h.value_len * stride > (uint64_t)h.length * 4u)
        return false;
    v->type = h.type; v->format = h.format; v->after = h.bytes_after;
    v->data = bytes + sizeof h; v->len = (size_t)h.value_len * stride;
    return true;
}
bool x11_clip_decode_pairs(const x11_clip_property *v, xcb_atom_t atom_pair, xcb_atom_t *out, size_t cap, size_t *n) {
    *n = 0;
    if (v->type != atom_pair || v->format != 32 || v->after || !v->len || v->len % 8u || v->len / 8u > cap) return false;
    memcpy(out, v->data, v->len); *n = v->len / 8u; return true;
}
int x11_clip_decode_transfer(const x11_clip_property *v, xcb_atom_t utf8, xcb_atom_t textplain,
                             xcb_atom_t incr, xcb_atom_t expected, bool allow_incr) {
    if (v->type == incr) {
        uint32_t lower;
        if (!allow_incr || v->format != 32 || v->len != 4 || v->after) return -1;
        memcpy(&lower, v->data, 4);
        return lower <= DATA_MAX ? 1 : -1;
    }
    if (v->format != 8 || (v->type != utf8 && v->type != textplain && v->type != XCB_ATOM_STRING) ||
        (expected && v->type != expected) || (v->after && (!v->len || v->len % 4u)) ||
        (uint64_t)v->after + v->len > DATA_MAX) return -1;
    return 0;
}
static bool reply_view(const xcb_get_property_reply_t *r, x11_clip_property *v) {
    if (!r) return false;
    return x11_clip_decode_property((const uint8_t *)r, sizeof *r + (size_t)r->length * 4u, v);
}

static void result(plat *p, int w, bool ok, uint32_t code) {
    plat_event e = { .kind = PLAT_EV_CLIPBOARD, .clip_which = (uint8_t)w, .clip_ok = ok, .code = code };
    x11_push_event(p, &e);
}
static bool store_got(x11_clip *c, const uint8_t *d, size_t n) {
    uint8_t *b = malloc(n ? n : 1);
    if (!b) return false;
    if (n) memcpy(b, d, n);
    free(c->got); c->got = b; c->got_len = n; return true;
}
static void claim(plat *p, int w, uint32_t time) {
    x11_clip *c = CL(p);
    c->own_time[w] = time;
    if (!time) {
        c->stamp_cookie[w] = xcb_change_property(C(p), XCB_PROP_MODE_REPLACE, p->win, c->claim_prop[w],
                                                XCB_ATOM_INTEGER, 8, 0, NULL).sequence;
        c->claiming[w] = 1;
    } else {
        xcb_set_selection_owner(C(p), p->win, c->sel[w], time);
        c->owner_cookie[w] = xcb_get_selection_owner(C(p), c->sel[w]).sequence;
        c->claiming[w] = 2;
    }
    c->owner_deadline[w] = deadline_after(c->timeout_ms);
    xcb_flush(C(p));
}
int plat_clip_set(plat *p, int which, const void *utf8, size_t len) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL || (len && !utf8) || len > DATA_MAX || len > OWN_BUDGET - c->own_bytes)
        return PLAT_ERR_FAIL;
    clip_blob *b = malloc(sizeof *b + len);
    if (!b) return PLAT_ERR_FAIL;
    b->refs = 1; b->len = len; if (len) memcpy(b->data, utf8, len);
    if (c->claiming[which] == 2) xcb_discard_reply(C(p), c->owner_cookie[which]);
    if (c->clear_pending[which]) xcb_discard_reply(C(p), c->clear_cookie[which]);
    c->clear_pending[which] = c->clear_deferred[which] = false; c->clear_deadline[which] = 0;
    blob_unref(c, c->own[which]); c->own_bytes += len; c->own[which] = b;
    c->confirmed[which] = false; c->retried[which] = false; c->uncertain[which] = false;
    claim(p, which, p->last_time);
    return PLAT_OK;
}

static void rx_convert(plat *p, int w, xcb_atom_t target) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    r->state = 1; r->target = target; r->type = 0; r->offset = 0; r->incr = false;
    /* Preserve P2.2's CurrentTime conversion behavior: an unrelated old key event
     * must not make a well-behaved owner refuse a later programmatic paste. */
    r->time = XCB_CURRENT_TIME;
    xcb_delete_property(C(p), r->win, c->prop[w]);
    xcb_convert_selection(C(p), r->win, c->sel[w], target, c->prop[w], r->time);
    xcb_flush(C(p));
}
int plat_clip_request(plat *p, int which) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL) return PLAT_ERR_FAIL;
    if (c->claiming[which]) {
        if (c->local_requests[which] == MAX_WAITERS) return PLAT_ERR_FAIL;
        c->local_requests[which]++; return PLAT_OK;
    }
    if (c->confirmed[which] && c->own[which]) {
        bool ok = store_got(c, c->own[which]->data, c->own[which]->len);
        result(p, which, ok, ok ? 0u : 1u); return PLAT_OK;
    }
    clip_receive *r = &c->rx[which];
    if (r->waiters == MAX_WAITERS || r->draining) return PLAT_ERR_FAIL;
    r->waiters++;
    if (r->state) return PLAT_OK;      /* one conversion, separate completion events */
    /* A fresh X resource ID isolates this conversion from late Notify/Property events
     * for a timed-out predecessor. Only two such windows can exist at once. */
    r->win = xcb_generate_id(C(p));
    if (!r->win || r->win == UINT32_MAX) { r->win = 0; r->waiters--; return PLAT_ERR_FAIL; }
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(C(p), 0, r->win, p->win, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
    r->deadline = deadline_after(c->timeout_ms);
    rx_convert(p, which, c->utf8); return PLAT_OK;
}
const uint8_t *plat_clip_data(const plat *p, size_t *len) {
    const x11_clip *c = CL(p); *len = c ? c->got_len : 0; return c ? c->got : NULL;
}
static void rx_finish(plat *p, int w, bool ok) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    if (r->state == 2) xcb_discard_reply(C(p), r->cookie.sequence);
    if (ok) { free(c->got); c->got = r->data; c->got_len = r->len; r->data = NULL; }
    unsigned waiters = r->waiters;
    xcb_window_t request_win = r->win;
    free(r->data); memset(r, 0, sizeof *r);
    xcb_delete_property(C(p), request_win, c->prop[w]);
    xcb_destroy_window(C(p), request_win); xcb_flush(C(p));
    for (unsigned i = 0; i < waiters; i++) result(p, w, ok, ok ? 0u : 1u);
}
static void rx_get(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    /* Delete explicitly only after the entire property has been read. */
    r->cookie = xcb_get_property(C(p), 0, r->win, c->prop[w], XCB_GET_PROPERTY_TYPE_ANY, r->offset,
                                 (uint32_t)((c->max_chunk + 3u) / 4u));
    r->state = 2; xcb_flush(C(p));
}
static bool rx_append(clip_receive *r, const x11_clip_property *v) {
    size_t n = v->len;
    if (v->type == XCB_ATOM_STRING) {
        for (size_t i = 0; i < v->len; i++) if (v->data[i] >= 0x80) n++;
    }
    if (n > DATA_MAX - r->len) return false;
    size_t need = r->len + n;
    if (need > r->cap || !r->data) {
        size_t cap = r->cap ? r->cap : 4096;
        while (cap < need) cap *= 2;
        if (cap > DATA_MAX) cap = DATA_MAX;
        uint8_t *b = realloc(r->data, cap);
        if (!b) return false;
        r->data = b; r->cap = cap;
    }
    if (v->type == XCB_ATOM_STRING) x11_latin1_to_utf8(v->data, v->len, r->data + r->len);
    else if (n) memcpy(r->data + r->len, v->data, n);
    r->len += n; return true;
}
static void rx_reject_incr(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    for (unsigned i = 0; i < r->waiters; i++) result(p, w, false, 1);
    r->waiters = 0; free(r->data); r->data = NULL; r->len = r->cap = 0;
    r->incr = r->draining = true; r->state = 3; r->offset = 0;
    r->deadline = r->drain_end = deadline_after(c->timeout_ms);
    xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
}
static void rx_reply(plat *p, int w, const xcb_get_property_reply_t *reply) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    x11_clip_property v;
    if (!reply_view(reply, &v)) { rx_finish(p, w, false); return; }
    if (r->draining) {
        /* Delete each chunk without retaining its contents. This is bounded by a total
         * drain deadline, even for a peer that sends endless malformed chunks. */
        xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
        if (!v.len && !v.after) rx_finish(p, w, false);
        else { r->state = 3; r->offset = 0; r->deadline = r->drain_end; }
        return;
    }
    int kind = x11_clip_decode_transfer(&v, c->utf8, c->textplain, c->incr, r->type, !r->incr && !r->offset);
    if (kind == 1) {
        /* The advertised size is a lower bound, never an allocation instruction. */
        r->incr = true; r->state = 3; r->deadline = deadline_after(c->timeout_ms);
        xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p)); return;
    }
    if (kind < 0 || !rx_append(r, &v)) {
        if (r->incr || v.type == c->incr) rx_reject_incr(p, w); else rx_finish(p, w, false);
        return;
    }
    r->type = v.type;
    if (v.after) {
        if (v.len / 4u > UINT32_MAX - r->offset) { rx_finish(p, w, false); return; }
        r->offset += (uint32_t)(v.len / 4u); rx_get(p, w); return;
    }
    if (!r->incr || !v.len) { rx_finish(p, w, true); return; }
    r->offset = 0; r->state = 3; r->deadline = deadline_after(c->timeout_ms);
    xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
}

static void notify(plat *p, const xcb_selection_request_event_t *rq, xcb_atom_t prop) {
    xcb_selection_notify_event_t n = { .response_type = XCB_SELECTION_NOTIFY, .time = rq->time,
        .requestor = rq->requestor, .selection = rq->selection, .target = rq->target, .property = prop };
    xcb_send_event(C(p), 0, rq->requestor, 0, (const char *)&n); xcb_flush(C(p));
}
static int tx_new(plat *p, clip_job *j, xcb_atom_t target, xcb_atom_t prop) {
    x11_clip *c = CL(p);
    for (unsigned i = 0; i < NSERVE; i++)
        if (c->tx[i].blob && c->tx[i].win == j->rq.requestor && c->tx[i].prop == prop) return -1;
    for (unsigned i = 0; i < NSERVE; i++) if (!c->tx[i].blob) {
        if (j->rq.requestor != p->win) {
            uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
            xcb_change_window_attributes(C(p), j->rq.requestor, XCB_CW_EVENT_MASK, &mask);
        }
        clip_serve *s = &c->tx[i]; s->blob = j->blob; s->blob->refs++;
        s->job = j; s->win = j->rq.requestor; s->prop = prop; s->target = target;
        s->deadline = deadline_after(c->timeout_ms);
        return (int)i;
    }
    return -1;
}
static unsigned convert(plat *p, clip_job *j, xcb_atom_t target, xcb_atom_t prop, int *slot) {
    x11_clip *c = CL(p); *slot = -1;
    if (!prop || target == c->multiple) return 0;
    for (unsigned i = 0; i < NSERVE; i++)
        if (c->tx[i].blob && c->tx[i].win == j->rq.requestor && c->tx[i].prop == prop) return 0;
    xcb_void_cookie_t ck;
    if (target == c->targets) {
        xcb_atom_t t[] = { c->targets, c->timestamp, c->multiple, c->utf8, c->textplain, XCB_ATOM_STRING, c->save };
        uint32_t nt = j->rq.selection == c->sel[0] ? 7u : 6u;
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, XCB_ATOM_ATOM, 32, nt, t);
    } else if (target == c->timestamp) {
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, XCB_ATOM_INTEGER, 32, 1, &j->stamp);
    } else if (target == c->save && j->rq.selection == c->sel[0]) {
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, c->null_type, 8, 0, NULL);
    } else if (target == c->utf8 || target == c->textplain || target == XCB_ATOM_STRING) {
        if (j->blob->len > c->chunk) {
            *slot = tx_new(p, j, target, prop);
            if (*slot < 0) return 0;
            /* STRING's byte count can shrink, so use zero as its valid lower bound. */
            uint32_t lower = target == XCB_ATOM_STRING ? 0 : (uint32_t)j->blob->len;
            ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, c->incr, 32, 1, &lower);
        } else {
            const uint8_t *data = j->blob->data; size_t len = j->blob->len;
            if (target == XCB_ATOM_STRING) { len = x11_utf8_to_latin1(data, len, c->scratch); data = c->scratch; }
            ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, target, 8, (uint32_t)len, data);
        }
    } else return 0;
    return ck.sequence;
}
static void job_start_writes(plat *p, clip_job *j) {
    x11_clip *c = CL(p);
    j->nchecks = (unsigned)j->npairs;
    for (size_t i = 0; i < j->npairs; i++) {
        xcb_atom_t target = j->pairs[2u * i], prop = j->pairs[2u * i + 1u];
        bool conflict = j->multiple && prop == j->rq.property;
        for (size_t k = 0; k < i; k++) if (prop == j->pairs[2u * k + 1u] && prop) conflict = true;
        j->checks[i] = conflict ? 0 : convert(p, j, target, prop, &j->slots[i]);
        if (conflict) j->slots[i] = -1;
        if (!j->checks[i]) j->pairs[2u * i + 1u] = XCB_ATOM_NONE;
    }
    j->state = 2; j->barrier = xcb_get_input_focus(C(p)).sequence;
    j->deadline = deadline_after(c->timeout_ms); xcb_flush(C(p));
}
static void serve(plat *p, const xcb_selection_request_event_t *rq) {
    x11_clip *c = CL(p); int w = which_of(c, rq->selection);
    if (w < 0 || !c->own[w] || rq->owner != p->win || older(rq->time, c->own_time[w]) ||
        (rq->target == c->multiple && !rq->property)) { notify(p, rq, XCB_ATOM_NONE); return; }
    clip_job *j = NULL;
    for (unsigned i = 0; i < NJOB; i++) if (!c->jobs[i].state) { j = &c->jobs[i]; break; }
    if (!j) { notify(p, rq, XCB_ATOM_NONE); return; }
    j->rq = *rq; j->blob = c->own[w]; j->blob->refs++; j->stamp = c->own_time[w];
    j->deadline = deadline_after(c->timeout_ms); j->multiple = rq->target == c->multiple;
    if (j->multiple) {
        j->state = 1;
        j->cookie = xcb_get_property(C(p), 0, rq->requestor, rq->property, XCB_GET_PROPERTY_TYPE_ANY, 0, MAX_PAIRS * 2u).sequence;
        xcb_flush(C(p));
    } else {
        j->npairs = 1; j->pairs[0] = rq->target; j->pairs[1] = rq->property ? rq->property : rq->target;
        job_start_writes(p, j);
    }
}

static void clear_query(plat *p, int w) {
    x11_clip *c = CL(p);
    if (!c->clear_pending[w]) {
        c->clear_pending[w] = true;
        c->clear_cookie[w] = xcb_get_selection_owner(C(p), c->sel[w]).sequence;
        c->clear_deadline[w] = deadline_after(c->timeout_ms); xcb_flush(C(p));
    }
}
bool x11_clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev) {
    x11_clip *c = CL(p);
    if (!c) return false;
    uint8_t t = e->response_type & 0x7f;
    if (t == XCB_SELECTION_REQUEST) { serve(p, (const xcb_selection_request_event_t *)e); return false; }
    if (t == XCB_SELECTION_CLEAR) {
        const xcb_selection_clear_event_t *x = (const xcb_selection_clear_event_t *)e;
        int w = which_of(c, x->selection);
        if (w < 0 || x->owner != p->win || !c->own[w] || older(x->time, c->own_time[w])) return false;
        if (c->claiming[w]) { c->clear_deferred[w] = true; return false; }
        /* X accepts equal timestamps. A previous owner's Clear can share the new
         * claim's millisecond; confirm that we actually lost it before releasing data. */
        if (!x->time || !c->own_time[w] || c->uncertain[w] || x->time == c->own_time[w]) { clear_query(p, w); return false; }
        blob_unref(c, c->own[w]); c->own[w] = NULL; c->confirmed[w] = false;
        *ev = (plat_event){ .kind = PLAT_EV_CLIPBOARD, .clip_which = (uint8_t)w, .code = 2 };
        return true;
    }
    if (t == XCB_SELECTION_NOTIFY) {
        const xcb_selection_notify_event_t *x = (const xcb_selection_notify_event_t *)e;
        if (c->saving && x11_clip_notify_matches(x, p->win, c->manager, c->save, c->save_prop, XCB_CURRENT_TIME)) {
            c->save_done = true; return false;
        }
        int w = which_of(c, x->selection);
        if (w < 0) return false;
        clip_receive *r = &c->rx[w];
        if (r->state != 1 || !x11_clip_notify_matches(x, r->win, c->sel[w], r->target, c->prop[w], r->time)) return false;
        if (!x->property) {
            if (r->target == c->utf8) rx_convert(p, w, XCB_ATOM_STRING);
            else rx_finish(p, w, false);
        } else rx_get(p, w);
    }
    if (t == XCB_PROPERTY_NOTIFY) {
        const xcb_property_notify_event_t *x = (const xcb_property_notify_event_t *)e;
        for (int w = 0; w < NSEL; w++) {
            if (x->window == p->win && x->atom == c->claim_prop[w] && x->state == XCB_PROPERTY_NEW_VALUE &&
                c->claiming[w] == 1 && x->sequence == (uint16_t)c->stamp_cookie[w]) claim(p, w, x->time);
            clip_receive *r = &c->rx[w];
            if (x->window == r->win && x->atom == c->prop[w] && x->state == XCB_PROPERTY_NEW_VALUE && r->state == 3)
                rx_get(p, w);
        }
        if (x->state == XCB_PROPERTY_DELETE)
            for (unsigned i = 0; i < NSERVE; i++) if (c->tx[i].blob && c->tx[i].win == x->window && c->tx[i].prop == x->atom)
                c->tx[i].deleted = true;
    }
    return false;
}

/* Checked void requests become pollable after the following reply passes their sequence. */
static bool checked_ok(plat *p, unsigned seq) {
    if (!seq) return false;
    void *reply = NULL; xcb_generic_error_t *error = NULL;
    int done = xcb_poll_for_reply(C(p), seq, &reply, &error);
    bool ok = done && !error;
    free(reply); free(error); return ok;
}
static bool barrier_done(plat *p, unsigned seq, bool *ok) {
    void *reply = NULL; xcb_generic_error_t *error = NULL;
    if (!xcb_poll_for_reply(C(p), seq, &reply, &error)) return false;
    *ok = reply && !error; free(reply); free(error); return true;
}
static void owner_finish(plat *p, int w, bool ok) {
    x11_clip *c = CL(p);
    c->claiming[w] = 0; c->owner_deadline[w] = 0; c->confirmed[w] = ok && c->own[w];
    /* Keep the bounded buffer on an uncertain failure: the claim may have succeeded,
     * or a query may have raced. Only SelectionClear or a replacement releases it. */
    c->uncertain[w] = !ok;
    if (!ok) result(p, w, false, 2);
    if (c->clear_deferred[w]) {
        c->clear_deferred[w] = false;
        if (!ok) clear_query(p, w);
    }
    unsigned requests = c->local_requests[w]; c->local_requests[w] = 0;
    if (requests && c->confirmed[w] && c->own[w]) {
        bool stored = store_got(c, c->own[w]->data, c->own[w]->len);
        for (unsigned i = 0; i < requests; i++) result(p, w, stored, stored ? 0u : 1u);
    } else for (unsigned i = 0; i < requests; i++) plat_clip_request(p, w);
}
static void tx_send(plat *p, clip_serve *s) {
    x11_clip *c = CL(p);
    if (s->zero) { tx_free(p, s); return; }
    size_t left = s->blob->len - (size_t)s->offset, n = left < c->chunk ? left : c->chunk;
    const uint8_t *data = s->blob->data + (size_t)s->offset;
    size_t consumed = n;
    if (s->target == XCB_ATOM_STRING) { n = latin_chunk(data, left, c->scratch, c->chunk, &consumed); data = c->scratch; }
    s->write = xcb_change_property_checked(C(p), XCB_PROP_MODE_APPEND, s->win, s->prop, s->target, 8, (uint32_t)n, data).sequence;
    s->barrier = xcb_get_input_focus(C(p)).sequence; s->checking = true;
    s->offset += consumed; s->zero = n == 0; s->deleted = false;
    s->deadline = deadline_after(c->timeout_ms); xcb_flush(C(p));
}
static void job_transfers(plat *p, clip_job *j, bool announced) {
    for (unsigned i = 0; i < NSERVE; i++) {
        clip_serve *t = &CL(p)->tx[i];
        if (t->blob && t->job == j) {
            if (announced) { t->job = NULL; t->deadline = deadline_after(CL(p)->timeout_ms); }
            else tx_free(p, t);
        }
    }
}
static void job_poll(plat *p, clip_job *j, uint64_t now, bool *progress) {
    x11_clip *c = CL(p);
    if (!j->state) return;
    if (now >= j->deadline) {
        job_transfers(p, j, false);
        notify(p, &j->rq, XCB_ATOM_NONE); job_free(p, j); *progress = true; return;
    }
    if (j->state == 1) {
        void *reply = NULL; xcb_generic_error_t *error = NULL;
        if (!xcb_poll_for_reply(C(p), j->cookie, &reply, &error)) return;
        *progress = true; j->state = 0;
        x11_clip_property v;
        bool ok = !error && reply_view(reply, &v) && x11_clip_decode_pairs(&v, c->atom_pair, j->pairs, MAX_PAIRS, &j->npairs);
        free(reply); free(error);
        if (!ok) { notify(p, &j->rq, XCB_ATOM_NONE); job_free(p, j); return; }
        job_start_writes(p, j); return;
    }
    bool ok = false;
    if (!barrier_done(p, j->barrier, &ok)) return;
    *progress = true;
    /* The extra final write in MULTIPLE is checked separately, before its notify. */
    if (j->nchecks > j->npairs) {
        ok = checked_ok(p, j->checks[j->npairs]) && ok;
        job_transfers(p, j, ok);
        notify(p, &j->rq, ok ? j->rq.property : XCB_ATOM_NONE); job_free(p, j); return;
    }
    for (size_t i = 0; i < j->npairs; i++) {
        bool written = checked_ok(p, j->checks[i]) && ok;
        if (!written) {
            j->pairs[2u * i + 1u] = XCB_ATOM_NONE;
            if (j->slots[i] >= 0) tx_free(p, &c->tx[(unsigned)j->slots[i]]);
        }
        j->checks[i] = 0;
    }
    if (!j->multiple) {
        job_transfers(p, j, j->pairs[1] != XCB_ATOM_NONE);
        notify(p, &j->rq, j->pairs[1]); job_free(p, j); return;
    }
    j->checks[j->npairs] = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, j->rq.property,
                                                      c->atom_pair, 32, (uint32_t)(j->npairs * 2u), j->pairs).sequence;
    j->nchecks++; j->barrier = xcb_get_input_focus(C(p)).sequence; xcb_flush(C(p));
}
bool x11_clip_poll(plat *p) {
    x11_clip *c = CL(p); if (!c) return false;
    /* Deliver already queued completions before replacing the single plat_clip_data
     * buffer with another selection's result (both receives can finish together). */
    if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return false;
    bool progress = false; uint64_t now = trace_now_ns();
    for (int w = 0; w < NSEL; w++) {
        if (c->claiming[w] && now >= c->owner_deadline[w]) {
            if (c->claiming[w] == 2) xcb_discard_reply(C(p), c->owner_cookie[w]);
            owner_finish(p, w, false); progress = true;
        }
        if (c->claiming[w] == 2) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), c->owner_cookie[w], &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool ok = r && !error && r->owner == p->win;
                free(reply); free(error); progress = true;
                if (!ok && !c->retried[w] && c->own[w]) { c->retried[w] = true; claim(p, w, 0); }
                else owner_finish(p, w, ok);
            }
        }
        if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return true;
        if (c->clear_pending[w]) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (now >= c->clear_deadline[w]) {
                xcb_discard_reply(C(p), c->clear_cookie[w]); c->clear_pending[w] = false;
                c->clear_deadline[w] = 0; progress = true;
            } else if (xcb_poll_for_reply(C(p), c->clear_cookie[w], &reply, &error)) {
                xcb_get_selection_owner_reply_t *owner = reply;
                if (owner && !error && owner->owner != p->win && c->own[w]) {
                    blob_unref(c, c->own[w]); c->own[w] = NULL; c->confirmed[w] = false;
                    result(p, w, false, 2);
                }
                free(reply); free(error); c->clear_pending[w] = false; c->clear_deadline[w] = 0; progress = true;
            }
        }
        clip_receive *r = &c->rx[w];
        if (r->state && now >= r->deadline) { rx_finish(p, w, false); progress = true; }
        if (r->state == 2) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), r->cookie.sequence, &reply, &error)) {
                r->state = r->incr ? 3u : 1u; progress = true;
                if (error) rx_finish(p, w, false); else rx_reply(p, w, reply);
                free(reply); free(error);
                if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt)
                    return true; /* deliver completed data before replacing it; partial reads stay fair */
            }
        }
    }
    for (unsigned i = 0; i < NJOB; i++) job_poll(p, &c->jobs[i], now, &progress);
    for (unsigned i = 0; i < NSERVE; i++) {
        clip_serve *s = &c->tx[i]; if (!s->blob || s->job) continue;
        if (now >= s->deadline) { tx_free(p, s); progress = true; continue; }
        if (s->checking) {
            bool ok;
            if (!barrier_done(p, s->barrier, &ok)) continue;
            ok = checked_ok(p, s->write) && ok; s->checking = false; progress = true;
            if (!ok) { tx_free(p, s); continue; }
        }
        if (s->deleted) { tx_send(p, s); progress = true; }
    }
    return progress;
}
void x11_clip_set_limits(plat *p, size_t chunk, uint32_t timeout_ms, uint32_t save_timeout_ms) {
    x11_clip *c = CL(p); if (!c) return;
    if (chunk) c->chunk = chunk > c->max_chunk ? c->max_chunk : chunk;
    if (timeout_ms) c->timeout_ms = timeout_ms;
    if (save_timeout_ms) c->save_ms = save_timeout_ms == UINT32_MAX ? 0 : save_timeout_ms;
}
size_t x11_clip_busy(const plat *p) {
    const x11_clip *c = CL(p); if (!c) return 0;
    size_t n = 0;
    for (int w = 0; w < NSEL; w++) if (c->rx[w].state) n++;
    for (unsigned i = 0; i < NSERVE; i++) if (c->tx[i].blob) n++;
    for (unsigned i = 0; i < NJOB; i++) if (c->jobs[i].state) n++;
    return n;
}
uint64_t x11_clip_deadline(const plat *p) {
    const x11_clip *c = CL(p); if (!c) return 0;
    uint64_t d = 0;
#define EARLIER(v) do { uint64_t x = (v); if (x && (!d || x < d)) d = x; } while (0)
    for (int w = 0; w < NSEL; w++) { EARLIER(c->owner_deadline[w]); EARLIER(c->clear_deadline[w]); EARLIER(c->rx[w].deadline); }
    for (unsigned i = 0; i < NSERVE; i++) EARLIER(c->tx[i].deadline);
    for (unsigned i = 0; i < NJOB; i++) EARLIER(c->jobs[i].deadline);
#undef EARLIER
    return d;
}

/* Shutdown is the only bounded wait. Keep serving while the manager fetches data;
 * do not call application callbacks, or synchronously wait for any X reply. */
void x11_clip_save_on_exit(plat *p) {
    x11_clip *c = CL(p);
    if (!c || !c->save_ms || !c->own[0]) return;
    uint64_t end = deadline_after(c->save_ms);
    unsigned owner = 0, actual_owner = 0;
    bool probing = true, actual_probing = true, started = false;
    c->saving = true; c->save_done = false; xcb_flush(C(p));
    while (!c->save_done && trace_now_ns() < end && !xcb_connection_has_error(C(p))) {
        xcb_generic_event_t *e;
        while (trace_now_ns() < end && ((e = xcb_poll_for_queued_event(C(p))) || (e = xcb_poll_for_event(C(p))))) {
            plat_event ev; x11_clip_event(p, e, &ev); free(e);
        }
        if (!started && !c->claiming[0]) {
            actual_owner = xcb_get_selection_owner(C(p), c->sel[0]).sequence;
            owner = xcb_get_selection_owner(C(p), c->manager).sequence;
            started = true; xcb_flush(C(p));
        }
        if (started && actual_probing) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), actual_owner, &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool ours = r && !error && r->owner == p->win;
                free(reply); free(error); actual_probing = false;
                if (!ours) break;
            }
        }
        if (started && !actual_probing && probing) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), owner, &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool found = r && !error && r->owner;
                free(reply); free(error); probing = false;
                if (!found) break;
                xcb_atom_t t[] = { c->utf8, c->textplain, XCB_ATOM_STRING };
                xcb_change_property(C(p), XCB_PROP_MODE_REPLACE, p->win, c->save_prop, XCB_ATOM_ATOM, 32, 3, t);
                xcb_convert_selection(C(p), p->win, c->manager, c->save, c->save_prop, XCB_CURRENT_TIME); xcb_flush(C(p));
            }
        }
        bool progress = x11_clip_poll(p);
        if (c->save_done) break;
        /* Reply polls can queue events; draining again prevents a sleep with buffered work. */
        e = xcb_poll_for_queued_event(C(p));
        if (e) { plat_event ev; x11_clip_event(p, e, &ev); free(e); continue; }
        if (progress) continue;
        uint64_t now = trace_now_ns(), d = x11_clip_deadline(p);
        uint64_t wake = d && d < end ? d : end;
        uint64_t wait_ms = wake <= now ? 0 : (wake - now + UINT64_C(999999)) / UINT64_C(1000000);
        int ms = wait_ms > INT_MAX ? INT_MAX : (int)wait_ms;
        struct pollfd fd = { xcb_get_file_descriptor(C(p)), POLLIN, 0 };
        (void)poll(&fd, 1, ms);
    }
    if (started && probing) xcb_discard_reply(C(p), owner);
    if (started && actual_probing) xcb_discard_reply(C(p), actual_owner);
    c->saving = false;
}
