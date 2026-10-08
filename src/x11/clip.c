/* clip.c - selection ownership and requests (P2.2). Allocation: owned and received buffers are malloc'd
 * (clipboard is not on the typing path). INCR transfers are a known gap: requests that come back as INCR
 * fail; we refuse to serve data larger than one X request (BIG-REQUESTS makes that ~16 MB). */
#include "clip.h"
#include <xcb/xcbext.h>
#include <stdlib.h>
#include <string.h>

#define C(p) ((xcb_connection_t *)(p)->conn)
#define NSEL 2

typedef struct x11_clip {
    uint8_t *own[NSEL];
    size_t own_len[NSEL];
    uint32_t own_time[NSEL];
    xcb_get_selection_owner_cookie_t owner_cookie[NSEL];
    xcb_get_property_cookie_t property_cookie[NSEL];
    bool owner_pending[NSEL], property_pending[NSEL];
    uint32_t local_requests[NSEL];
    size_t max_bytes;                 /* BIG-REQUESTS negotiation happens at init */
    uint8_t *got;
    size_t got_len;
    xcb_atom_t a_sel[NSEL];            /* CLIPBOARD, PRIMARY */
    xcb_atom_t a_utf8, a_targets, a_prop, a_textplain, a_incr, a_timestamp, a_string;
} x11_clip;

#define CL(p) ((x11_clip *)(p)->clip)

int x11_clip_init(plat *p) {
    x11_clip *c = calloc(1, sizeof *c);
    if (!c) return PLAT_ERR_FAIL;
    static const char *const names[] = { "CLIPBOARD", "UTF8_STRING", "TARGETS", "EDIT_SELECTION",
        "text/plain;charset=utf-8", "INCR", "TIMESTAMP" };
    xcb_intern_atom_cookie_t ck[7];
    for (int i = 0; i < 7; i++) ck[i] = xcb_intern_atom(C(p), 0, (uint16_t)strlen(names[i]), names[i]);
    xcb_atom_t *dst[7] = { &c->a_sel[0], &c->a_utf8, &c->a_targets, &c->a_prop, &c->a_textplain, &c->a_incr, &c->a_timestamp };
    for (int i = 0; i < 7; i++) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(C(p), ck[i], NULL);
        *dst[i] = r ? r->atom : XCB_ATOM_NONE;
        free(r);
    }
    c->a_sel[1] = XCB_ATOM_PRIMARY;
    c->a_string = XCB_ATOM_STRING;
    c->max_bytes = (size_t)xcb_get_maximum_request_length(C(p)) * 4;
    p->clip = c;
    return PLAT_OK;
}

void x11_clip_destroy(plat *p) {
    x11_clip *c = CL(p);
    if (!c) return;
    for (int i = 0; i < NSEL; i++) {
        if (c->owner_pending[i]) xcb_discard_reply(C(p), c->owner_cookie[i].sequence);
        if (c->property_pending[i]) xcb_discard_reply(C(p), c->property_cookie[i].sequence);
        free(c->own[i]);
    }
    free(c->got);
    free(c);
    p->clip = NULL;
}

size_t x11_utf8_to_latin1(const uint8_t *s, size_t len, uint8_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < len;) {
        uint8_t b = s[i];
        if (b < 0x80) { out[n++] = b; i++; }
        else if ((b & 0xe0) == 0xc0 && i + 1 < len && (s[i + 1] & 0xc0) == 0x80) {
            uint32_t cp = ((uint32_t)(b & 0x1f) << 6) | (s[i + 1] & 0x3fu);
            out[n++] = cp <= 0xff ? (uint8_t)cp : (uint8_t)'?'; i += 2;
        } else {
            out[n++] = '?';
            i++;
            while (i < len && (s[i] & 0xc0) == 0x80) i++;      /* skip rest of the sequence */
        }
    }
    return n;
}

size_t x11_latin1_to_utf8(const uint8_t *s, size_t len, uint8_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < 0x80) out[n++] = s[i];
        else { out[n++] = (uint8_t)(0xc0 | (s[i] >> 6)); out[n++] = (uint8_t)(0x80 | (s[i] & 0x3f)); }
    }
    return n;
}

static int which_of(const x11_clip *c, xcb_atom_t sel) {
    for (int i = 0; i < NSEL; i++) if (c->a_sel[i] == sel) return i;
    return -1;
}

int plat_clip_set(plat *p, int which, const void *utf8, size_t len) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL) return PLAT_ERR_FAIL;
    uint8_t *copy = malloc(len ? len : 1);
    if (!copy) return PLAT_ERR_FAIL;
    if (len) memcpy(copy, utf8, len);
    if (c->owner_pending[which]) xcb_discard_reply(C(p), c->owner_cookie[which].sequence);
    free(c->own[which]);
    c->own[which] = copy; c->own_len[which] = len;
    uint32_t t = p->last_time;
    xcb_set_selection_owner(C(p), (xcb_window_t)p->win, c->a_sel[which], t);
    c->owner_cookie[which] = xcb_get_selection_owner(C(p), c->a_sel[which]);
    c->owner_pending[which] = true;
    c->own_time[which] = t;
    xcb_flush(C(p));
    return PLAT_OK;
}

static int store_got(x11_clip *c, const uint8_t *d, size_t n) {
    uint8_t *b = malloc(n ? n : 1);
    if (!b) return -1;
    if (n) memcpy(b, d, n);
    free(c->got);
    c->got = b; c->got_len = n;
    return 0;
}

int plat_clip_request(plat *p, int which) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL) return PLAT_ERR_FAIL;
    if (c->owner_pending[which]) {
        if (c->local_requests[which] == UINT32_MAX) return PLAT_ERR_FAIL;
        c->local_requests[which]++;
        return PLAT_OK;
    }
    if (c->own[which]) {            /* we own it: deliver without a round trip */
        plat_event ev;
        memset(&ev, 0, sizeof ev);
        ev.kind = PLAT_EV_CLIPBOARD; ev.clip_which = (uint8_t)which;
        ev.clip_ok = store_got(c, c->own[which], c->own_len[which]) == 0;
        ev.code = ev.clip_ok ? 0 : 1;
        x11_push_event(p, &ev);
        return PLAT_OK;
    }
    xcb_convert_selection(C(p), (xcb_window_t)p->win, c->a_sel[which], c->a_utf8, c->a_prop, 0);
    xcb_flush(C(p));
    return PLAT_OK;
}

const uint8_t *plat_clip_data(const plat *p, size_t *len) {
    const x11_clip *c = CL(p);
    if (!c) { *len = 0; return NULL; }
    *len = c->got_len;
    return c->got;
}

static void refuse(plat *p, const xcb_selection_request_event_t *rq) {
    xcb_selection_notify_event_t n;
    memset(&n, 0, sizeof n);
    n.response_type = XCB_SELECTION_NOTIFY;
    n.time = rq->time; n.requestor = rq->requestor; n.selection = rq->selection; n.target = rq->target;
    n.property = XCB_ATOM_NONE;
    xcb_send_event(C(p), 0, rq->requestor, 0, (const char *)&n);
    xcb_flush(C(p));
}

static void serve(plat *p, const xcb_selection_request_event_t *rq) {
    x11_clip *c = CL(p);
    int w = which_of(c, rq->selection);
    xcb_atom_t prop = rq->property ? rq->property : rq->target;
    if (w < 0 || !c->own[w] || rq->owner != p->win) { refuse(p, rq); return; }
    size_t maxb = c->max_bytes;
    xcb_connection_t *cn = C(p);
    if (rq->target == c->a_targets) {
        xcb_atom_t t[5] = { c->a_targets, c->a_timestamp, c->a_utf8, c->a_textplain, c->a_string };
        xcb_change_property(cn, XCB_PROP_MODE_REPLACE, rq->requestor, prop, XCB_ATOM_ATOM, 32, 5, t);
    } else if (rq->target == c->a_timestamp) {
        uint32_t t = c->own_time[w];
        xcb_change_property(cn, XCB_PROP_MODE_REPLACE, rq->requestor, prop, XCB_ATOM_INTEGER, 32, 1, &t);
    } else if (rq->target == c->a_utf8 || rq->target == c->a_textplain) {
        if (c->own_len[w] + 64 > maxb) { refuse(p, rq); return; }     /* INCR gap */
        xcb_change_property(cn, XCB_PROP_MODE_REPLACE, rq->requestor, prop, rq->target, 8,
                            (uint32_t)c->own_len[w], c->own[w]);
    } else if (rq->target == c->a_string) {
        if (c->own_len[w] + 64 > maxb) { refuse(p, rq); return; }
        uint8_t *l = malloc(c->own_len[w] ? c->own_len[w] : 1);
        if (!l) { refuse(p, rq); return; }
        size_t n = x11_utf8_to_latin1(c->own[w], c->own_len[w], l);
        xcb_change_property(cn, XCB_PROP_MODE_REPLACE, rq->requestor, prop, XCB_ATOM_STRING, 8, (uint32_t)n, l);
        free(l);
    } else { refuse(p, rq); return; }
    xcb_selection_notify_event_t n;
    memset(&n, 0, sizeof n);
    n.response_type = XCB_SELECTION_NOTIFY;
    n.time = rq->time; n.requestor = rq->requestor; n.selection = rq->selection; n.target = rq->target;
    n.property = prop;
    xcb_send_event(cn, 0, rq->requestor, 0, (const char *)&n);
    xcb_flush(cn);
}

bool x11_clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev) {
    x11_clip *c = CL(p);
    if (!c) return false;
    uint8_t t = e->response_type & 0x7f;
    memset(ev, 0, sizeof *ev);
    ev->kind = PLAT_EV_CLIPBOARD;
    if (t == XCB_SELECTION_REQUEST) { serve(p, (const xcb_selection_request_event_t *)e); return false; }
    if (t == XCB_SELECTION_CLEAR) {
        const xcb_selection_clear_event_t *x = (const xcb_selection_clear_event_t *)e;
        int w = which_of(c, x->selection);
        if (w < 0) return false;
        free(c->own[w]); c->own[w] = NULL; c->own_len[w] = 0;
        ev->clip_which = (uint8_t)w; ev->code = 2;
        return true;
    }
    if (t == XCB_SELECTION_NOTIFY) {
        const xcb_selection_notify_event_t *x = (const xcb_selection_notify_event_t *)e;
        int w = which_of(c, x->selection);
        if (w < 0) return false;
        ev->clip_which = (uint8_t)w; ev->code = 1;
        if (c->property_pending[w]) {
            xcb_discard_reply(C(p), c->property_cookie[w].sequence);
            c->property_pending[w] = false;
        }
        if (x->property == XCB_ATOM_NONE) return true;
        c->property_cookie[w] = xcb_get_property(C(p), 1, (xcb_window_t)p->win, x->property,
                                                XCB_GET_PROPERTY_TYPE_ANY, 0, 0x3fffffff);
        c->property_pending[w] = true;
        xcb_flush(C(p));
    }
    return false;
}

bool x11_clip_poll(plat *p) {
    x11_clip *c = CL(p);
    if (!c) return false;
    bool progress = false;
    for (int w = 0; w < NSEL; w++) {
        void *reply = NULL;
        xcb_generic_error_t *error = NULL;
        if (c->owner_pending[w] &&
            xcb_poll_for_reply(C(p), c->owner_cookie[w].sequence, &reply, &error)) {
            progress = true;
            c->owner_pending[w] = false;
            xcb_get_selection_owner_reply_t *r = reply;
            bool ok = !error && r && r->owner == p->win;
            free(r); free(error);
            if (!ok && c->own_time[w]) {
                /* Same timestamp fallback as before, without waiting in a callback. */
                c->own_time[w] = 0;
                xcb_set_selection_owner(C(p), p->win, c->a_sel[w], XCB_CURRENT_TIME);
                c->owner_cookie[w] = xcb_get_selection_owner(C(p), c->a_sel[w]);
                c->owner_pending[w] = true;
                xcb_flush(C(p));
            } else {
                if (!ok) {
                    free(c->own[w]); c->own[w] = NULL; c->own_len[w] = 0;
                    plat_event ev = { .kind = PLAT_EV_CLIPBOARD, .code = 2, .clip_which = (uint8_t)w };
                    x11_push_event(p, &ev);
                }
                while (c->local_requests[w]) {
                    c->local_requests[w]--;
                    plat_clip_request(p, w);
                }
            }
        }
        reply = NULL; error = NULL;
        if (!c->property_pending[w] ||
            !xcb_poll_for_reply(C(p), c->property_cookie[w].sequence, &reply, &error)) continue;
        progress = true;
        c->property_pending[w] = false;
        xcb_get_property_reply_t *r = reply;
        plat_event ev = { .kind = PLAT_EV_CLIPBOARD, .code = 1, .clip_which = (uint8_t)w };
        if (!r || error) { free(r); free(error); x11_push_event(p, &ev); continue; }
        const uint8_t *v = xcb_get_property_value(r);
        size_t n = (size_t)xcb_get_property_value_length(r);
        if (r->format == 8 && r->type == c->a_utf8) {
            if (store_got(c, v, n) == 0) { ev.clip_ok = true; ev.code = 0; }
        } else if (r->format == 8 && r->type == c->a_string) {
            uint8_t *u = malloc(n * 2 + 1);
            if (u) {
                size_t m = x11_latin1_to_utf8(v, n, u);
                if (store_got(c, u, m) == 0) { ev.clip_ok = true; ev.code = 0; }
                free(u);
            }
        }                           /* INCR (type == a_incr) and other types: failure, see header */
        free(r);
        x11_push_event(p, &ev);
    }
    return progress;
}
