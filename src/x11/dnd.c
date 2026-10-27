/* XDND target. (P) Wire constants/word indices below come from XDND v5:
 * five 32-bit ClientMessage words, three inline type atoms, version in the
 * top byte (shift 24), bit zero = type-list/accept, versions one/two introduce
 * drop timestamps/Finished. X11 property formats are 8 or 32 bits.
 * No XdndProxy support; the source must address our window directly. */
#include "dnd.h"
#include "clip.h"
#include "private.h"
#include "trace/trace.h"
#include <xcb/xcbext.h>
#include <stdlib.h>
#include <string.h>

#define C(p) ((xcb_connection_t *)x11_private(p)->conn)
#define D(p) (x11_private(p) ? (x11_dnd *)x11_private(p)->dnd : NULL)
enum { XDND_VERSION = 5, VERSION_SHIFT = 24 };
typedef struct x11_dnd {
    xcb_atom_t aware, enter, position, status, leave, drop, finished, selection,
               uri, copy, types, property;
    xcb_window_t source, denied_source;
    uint32_t version, denied_version, drop_time;
    bool active, offered, receiving, reading, position_pending, drop_pending;
    xcb_get_property_cookie_t cookie;
    uint64_t deadline;
} x11_dnd;

static void reply(plat *p, xcb_window_t source, xcb_atom_t type, bool ok)
{
    x11_dnd *d = D(p);
    xcb_client_message_event_t m = { .response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = source, .type = type };
    m.data.data32[0] = x11_private(p)->win;
    m.data.data32[1] = ok ? 1u : 0u;
    m.data.data32[type == d->status ? 4 : 2] = d->copy;
    xcb_send_event(C(p), 0, source, XCB_EVENT_MASK_NO_EVENT, (const char *)&m);
    xcb_flush(C(p));
}
static void finish(plat *p, xcb_window_t source, uint32_t version, bool ok)
{
    if (version >= 2) reply(p, source, D(p)->finished, ok);
}
static void reset(plat *p)
{
    x11_dnd *d = D(p);
    if (d->reading) xcb_discard_reply(C(p), d->cookie.sequence);
    d->active = d->offered = d->reading = d->position_pending = d->drop_pending = false;
    d->deadline = 0;
    /* Retain source/version to reject a late Drop after Leave/timeout. */
}
static void begin_drop(plat *p)
{
    x11_dnd *d = D(p);
    d->drop_pending = false;
    if (!d->offered || x11_clip_drop_request(p, d->selection, d->uri, d->property, d->drop_time) != PLAT_OK) {
        finish(p, d->source, d->version, false); reset(p); return;
    }
    d->receiving = true; d->deadline = 0; /* clip.c owns transfer deadlines */
}
int x11_dnd_init(plat *p)
{
    x11_dnd *d = calloc(1, sizeof *d);
    if (!d) return PLAT_ERR_FAIL;
    const char *names[] = { "XdndAware", "XdndEnter", "XdndPosition", "XdndStatus", "XdndLeave",
        "XdndDrop", "XdndFinished", "XdndSelection", "text/uri-list", "XdndActionCopy", "XdndTypeList", "EDIT_DROP" };
    xcb_atom_t *dest[] = { &d->aware, &d->enter, &d->position, &d->status, &d->leave, &d->drop,
                          &d->finished, &d->selection, &d->uri, &d->copy, &d->types, &d->property };
    /* (P) Atom count is derived from the protocol names above. Init may wait. */
    xcb_intern_atom_cookie_t cookies[sizeof names / sizeof names[0]];
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        cookies[i] = xcb_intern_atom(C(p), 0, (uint16_t)strlen(names[i]), names[i]);
    bool ok = true;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(C(p), cookies[i], NULL);
        *dest[i] = r ? r->atom : XCB_ATOM_NONE;
        if (!*dest[i]) ok = false;
        free(r);
    }
    if (!ok) { free(d); return PLAT_ERR_FAIL; }
    x11_private(p)->dnd = d;
    uint32_t version = XDND_VERSION;
    xcb_change_property(C(p), XCB_PROP_MODE_REPLACE, x11_private(p)->win, d->aware, XCB_ATOM_ATOM, 32, 1, &version);
    xcb_flush(C(p));
    return PLAT_OK;
}
void x11_dnd_destroy(plat *p)
{
    x11_dnd *d = D(p);
    if (!d) return;
    reset(p); free(d); x11_private(p)->dnd = NULL;
}
void x11_dnd_event(plat *p, const xcb_client_message_event_t *m)
{
    x11_dnd *d = D(p);
    if (!d || m->format != 32 || m->window != x11_private(p)->win || !m->data.data32[0]) return;
    xcb_window_t source = m->data.data32[0];
    if (m->type == d->enter) {
        uint32_t version = m->data.data32[1] >> VERSION_SHIFT;
        if (version > XDND_VERSION) return;
        if (d->active && source != d->source) {
            d->denied_source = source; d->denied_version = version; return;
        }
        if (d->receiving) return;
        reset(p); d->source = source; d->version = version; d->active = true;
        d->deadline = x11_clip_peer_deadline(p);
        if (m->data.data32[1] & 1u) {
            d->cookie = xcb_get_property(C(p), 0, source, d->types, XCB_ATOM_ATOM, 0,
                X11_CLIP_CHUNK_MAX / sizeof(xcb_atom_t)); /* (P) existing clipboard slice bound */
            d->reading = true; xcb_flush(C(p));
        } else {
            for (size_t i = 2; i < sizeof m->data.data32 / sizeof m->data.data32[0]; i++)
                if (m->data.data32[i] == d->uri) d->offered = true;
        }
    } else if (m->type == d->position) {
        if (!d->active || source != d->source || d->receiving) { reply(p, source, d->status, false); return; }
        d->deadline = x11_clip_peer_deadline(p);
        if (d->reading) d->position_pending = true;
        else reply(p, source, d->status, d->offered);
    } else if (m->type == d->leave) {
        /* XDND can send Leave after Drop; keep the accepted transfer alive. */
        if (d->active && source == d->source && !d->receiving) reset(p);
    } else if (m->type == d->drop) {
        if (d->active && source == d->source && !d->receiving) {
            d->drop_time = d->version >= 1 ? m->data.data32[2] : XCB_CURRENT_TIME;
            if (d->reading) d->drop_pending = true;
            else begin_drop(p);
        } else if (source != d->source || !d->receiving) {
            if (source == d->denied_source) finish(p, source, d->denied_version, false);
            else if (source == d->source) finish(p, source, d->version, false);
        }
    }
}
bool x11_dnd_poll(plat *p)
{
    x11_dnd *d = D(p);
    if (!d || !d->active || d->receiving) return false;
    if (d->deadline && trace_now_ns() >= d->deadline) {
        if (d->position_pending) reply(p, d->source, d->status, false);
        finish(p, d->source, d->version, false); reset(p); return true;
    }
    if (!d->reading) return false;
    void *reply_data = NULL; xcb_generic_error_t *error = NULL;
    if (!xcb_poll_for_reply(C(p), d->cookie.sequence, &reply_data, &error)) return false;
    d->reading = false;
    xcb_get_property_reply_t *r = reply_data;
    if (r && !error && r->type == XCB_ATOM_ATOM && r->format == 32 && !r->bytes_after) {
        const xcb_atom_t *types = xcb_get_property_value(r);
        size_t count = (size_t)xcb_get_property_value_length(r) / sizeof *types;
        for (size_t i = 0; i < count; i++) if (types[i] == d->uri) d->offered = true;
    }
    free(reply_data); free(error);
    if (d->position_pending) { d->position_pending = false; reply(p, d->source, d->status, d->offered); }
    if (d->drop_pending) begin_drop(p);
    return true;
}
uint64_t x11_dnd_deadline(const plat *p)
{ const x11_dnd *d = D(p); return d ? d->deadline : 0; }
void x11_dnd_complete(plat *p, bool ok)
{
    x11_dnd *d = D(p);
    if (!d || !d->receiving) return;
    finish(p, d->source, d->version, ok);
    if (ok) { plat_event e = { .kind = PLAT_EV_DROP }; (void)x11_push_event(p, &e); }
    d->receiving = false; reset(p);
}
const uint8_t *x11_plat_drop_data(const plat *p, size_t *len)
{ return x11_clip_drop_data(p, len); }
