/* Raw XDND source on x11_xvfb.h's private server; never uses the owner's display.
 * (P) Numeric wire fields use the XDND/core X11 protocol (zero = unset/reject,
 * one = accept, v5, 24-bit version shift, five 32-bit ClientMessage words,
 * three inline types, format 8/32). Unit increments count actual events.
 * (P) Transfer size is the brief's 100 KiB = 100 * 1024 bytes. The chunk size
 * reuses clip.c's 4096-byte initial receive capacity. (E) Pump cadence reuses
 * x11_clip_test's 4 ms. Wait allows the existing 5000 ms peer timeout plus
 * another peer-timeout interval to observe its reply; no performance gate. */
#include "x11/plat.h"
#include "x11/clip.h"
#include "x11_xvfb.h"
#include "trace/trace.h"
#include <xcb/xcb.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } } while (0)
enum { PEER_MS = 5000, WAIT_MS = PEER_MS + PEER_MS, STEP_MS = 4,
       PAYLOAD_SIZE = 100 * 1024, CHUNK_SIZE = 4096, VERSION = 5, VERSION_SHIFT = 24 };
static uint64_t now_ms(void) { return trace_now_ns() / UINT64_C(1000000); } /* (P) ns/ms */

typedef struct peer {
    xcb_connection_t *c;
    xcb_window_t win, target;
    xcb_atom_t aware, enter, position, status, leave, drop, finished, selection,
               uri, copy, types, incr, utf8, plain;
    xcb_client_message_event_t last_status, last_finished;
    unsigned statuses, finishes, requests;
    const uint8_t *data;
    size_t len, off;
    bool incremental, silent, transfer;
    xcb_selection_request_event_t request;
} peer;
typedef struct sink { plat *p; unsigned drops, clips; const uint8_t *expect; size_t len; } sink;

static xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    CHECK(a != XCB_ATOM_NONE, "intern atom"); free(r); return a;
}
static void barrier(peer *r)
{
    xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(r->c, xcb_get_input_focus(r->c), NULL);
    CHECK(reply != NULL, "peer barrier"); free(reply);
}
static bool peer_open(peer *r, plat *p)
{
    memset(r, 0, sizeof *r); r->c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(r->c)) { xcb_disconnect(r->c); return false; }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(r->c)).data;
    r->win = xcb_generate_id(r->c); r->target = plat_native_x11(p).window;
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(r->c, XCB_COPY_FROM_PARENT, r->win, screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
#define AT(field, name) r->field = atom(r->c, name)
    AT(aware, "XdndAware"); AT(enter, "XdndEnter"); AT(position, "XdndPosition"); AT(status, "XdndStatus");
    AT(leave, "XdndLeave"); AT(drop, "XdndDrop"); AT(finished, "XdndFinished"); AT(selection, "XdndSelection");
    AT(uri, "text/uri-list"); AT(copy, "XdndActionCopy"); AT(types, "XdndTypeList"); AT(incr, "INCR");
    AT(utf8, "UTF8_STRING"); AT(plain, "text/plain");
#undef AT
    barrier(r); return true;
}
static void message(peer *r, xcb_atom_t type, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    xcb_client_message_event_t m = { .response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = r->target, .type = type, .data.data32 = { r->win, b, c, d, e } };
    xcb_send_event(r->c, 0, r->target, XCB_EVENT_MASK_NO_EVENT, (const char *)&m); xcb_flush(r->c);
}
static void enter(peer *r, unsigned version, bool uri, bool list)
{
    /* The fourth type exercises the XdndTypeList branch beyond three inline slots. */
    xcb_atom_t offers[] = { r->utf8, r->plain, XCB_ATOM_STRING, uri ? r->uri : r->utf8 };
    if (list) xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, r->win, r->types, XCB_ATOM_ATOM,
                                  32, (uint32_t)(sizeof offers / sizeof offers[0]), offers);
    message(r, r->enter, (version << VERSION_SHIFT) | (list ? 1u : 0u), r->utf8, r->plain,
            uri ? r->uri : XCB_ATOM_STRING);
}
static void own(peer *r, const uint8_t *data, size_t len, bool incr, bool silent)
{
    r->data = data; r->len = len; r->off = 0; r->incremental = incr; r->silent = silent; r->transfer = false;
    xcb_set_selection_owner(r->c, r->win, r->selection, XCB_CURRENT_TIME); barrier(r);
}
static void notify(peer *r, const xcb_selection_request_event_t *q, xcb_atom_t property)
{
    xcb_selection_notify_event_t n = { .response_type = XCB_SELECTION_NOTIFY, .time = q->time,
        .requestor = q->requestor, .selection = q->selection, .target = q->target, .property = property };
    xcb_send_event(r->c, 0, q->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&n); xcb_flush(r->c);
}
static void peer_step(peer *r)
{
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(r->c))) {
        uint8_t type = event->response_type & 0x7fu; /* (P) X SendEvent bit */
        if (type == XCB_CLIENT_MESSAGE) {
            const xcb_client_message_event_t *m = (const xcb_client_message_event_t *)event;
            if (m->type == r->status) { r->statuses++; r->last_status = *m; }
            if (m->type == r->finished) { r->finishes++; r->last_finished = *m; }
        } else if (type == XCB_SELECTION_REQUEST) {
            const xcb_selection_request_event_t *q = (const xcb_selection_request_event_t *)event;
            r->request = *q; r->requests++;
            CHECK(q->selection == r->selection && q->target == r->uri, "requests exact URI selection tuple");
            if (!r->silent) {
                if (r->incremental) {
                    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE, lower = (uint32_t)r->len;
                    xcb_change_window_attributes(r->c, q->requestor, XCB_CW_EVENT_MASK, &mask);
                    xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, q->requestor, q->property, r->incr, 32, 1, &lower);
                    r->transfer = true;
                } else xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, q->requestor, q->property, r->uri, 8,
                                            (uint32_t)r->len, r->data);
                notify(r, q, q->property);
            }
        } else if (type == XCB_PROPERTY_NOTIFY && r->transfer) {
            const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)event;
            if (n->window == r->request.requestor && n->atom == r->request.property && n->state == XCB_PROPERTY_DELETE) {
                size_t len = r->len - r->off; if (len > CHUNK_SIZE) len = CHUNK_SIZE;
                xcb_change_property(r->c, XCB_PROP_MODE_REPLACE, n->window, n->atom, r->uri, 8, (uint32_t)len, r->data + r->off);
                r->off += len; if (!len) r->transfer = false; xcb_flush(r->c);
            }
        }
        free(event);
    }
}
static void on_event(void *ud, const plat_event *e)
{
    sink *s = ud;
    if (e->kind == PLAT_EV_CLIPBOARD) s->clips++;
    if (e->kind != PLAT_EV_DROP) return;
    size_t len = 0; const uint8_t *data = plat_drop_data(s->p, &len);
    s->drops++;
    CHECK(len == s->len && data && !memcmp(data, s->expect, len), "drop callback has exact raw bytes");
}
static void step(plat *p, sink *s, peer *a, peer *b)
{
    plat_callbacks cb = { .ud = s, .on_event = on_event };
    CHECK(plat_run_for(p, &cb, STEP_MS) == PLAT_OK, "platform pump");
    peer_step(a); if (b) peer_step(b);
}
static void wait_counter(plat *p, sink *s, peer *a, peer *b, const unsigned *count, unsigned goal)
{
    uint64_t end = now_ms() + WAIT_MS;
    while (*count < goal && now_ms() < end) step(p, s, a, b);
    CHECK(*count == goal, "expected peer/event count reached");
}
static void position(plat *p, sink *s, peer *a, peer *b, bool accept)
{
    unsigned goal = a->statuses + 1u;
    message(a, a->position, 0, 0, XCB_CURRENT_TIME, a->copy);
    wait_counter(p, s, a, b, &a->statuses, goal);
    CHECK(a->last_status.data.data32[0] == a->target &&
          (a->last_status.data.data32[1] & 1u) == (accept ? 1u : 0u) &&
          a->last_status.data.data32[2] == 0 && a->last_status.data.data32[3] == 0 &&
          a->last_status.data.data32[4] == a->copy, "XdndStatus target/accept/empty rectangle/copy action");
}
static void finished(plat *p, sink *s, peer *a, peer *b, unsigned before, bool success)
{
    wait_counter(p, s, a, b, &a->finishes, before + 1u);
    CHECK(a->last_finished.data.data32[0] == a->target &&
          a->last_finished.data.data32[1] == (success ? 1u : 0u) &&
          a->last_finished.data.data32[2] == a->copy, "XdndFinished target/success/copy action");
}
static void success(plat *p, sink *s, peer *r, bool list, bool incr)
{
    unsigned drops = s->drops, done = r->finishes;
    own(r, s->expect, s->len, incr, false); enter(r, VERSION, true, list); position(p, s, r, NULL, true);
    message(r, r->drop, 0, XCB_CURRENT_TIME, 0, 0); finished(p, s, r, NULL, done, true);
    wait_counter(p, s, r, NULL, &s->drops, drops + 1u);
}
int main(void)
{
    if (!xvfb_start()) { fputs("x11_dnd_test: FAIL (private Xvfb unavailable)\n", stderr); return EXIT_FAILURE; }
    trace_init(); trace_thread_register();
    plat p; plat_config cfg = { .width = 1, .height = 1, .title = "XDND private peer" };
    if (plat_init(&p, &cfg) != PLAT_OK) { fputs("x11_dnd_test: FAIL (plat_init)\n", stderr); return EXIT_FAILURE; }
    peer a, b;
    if (!peer_open(&a, &p)) { plat_shutdown(&p); return EXIT_FAILURE; }
    if (!peer_open(&b, &p)) { xcb_disconnect(a.c); plat_shutdown(&p); return EXIT_FAILURE; }
    static const uint8_t uri[] = "file:///tmp/drop%20file\r\nfile:///tmp/\xc3\xa9\n";
    sink s = { .p = &p, .expect = uri, .len = sizeof uri - 1u }; /* (P) exclude C NUL terminator */
    xcb_get_property_reply_t *aware = xcb_get_property_reply(a.c,
        xcb_get_property(a.c, 0, a.target, a.aware, XCB_ATOM_ATOM, 0, 1), NULL);
    uint32_t version = 0;
    if (aware && xcb_get_property_value_length(aware) == (int)sizeof version) memcpy(&version, xcb_get_property_value(aware), sizeof version);
    CHECK(aware && aware->type == XCB_ATOM_ATOM && aware->format == 32 && version == VERSION, "XdndAware advertises v5 as ATOM/32"); free(aware);
    size_t empty_len = s.len;
    CHECK(!plat_drop_data(&p, &empty_len) && !empty_len, "no data before first drop");
    plat wl_stub = { .backend = PLAT_BACKEND_WAYLAND };
    empty_len = s.len;
    CHECK(!plat_drop_data(&wl_stub, &empty_len) && !empty_len, "Wayland accessor stub has no data");
    success(&p, &s, &a, false, false); success(&p, &s, &a, true, false);
    size_t retained_len = 0; const uint8_t *retained = plat_drop_data(&p, &retained_len);
    /* Clipboard completion must neither replace nor invalidate the drop buffer. */
    static const uint8_t clipboard[] = "independent clipboard";
    CHECK(plat_clip_set(&p, PLAT_CLIP_CLIPBOARD, clipboard, sizeof clipboard) == PLAT_OK, "set local clipboard");
    CHECK(plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "request local clipboard");
    wait_counter(&p, &s, &a, NULL, &s.clips, 1);
    CHECK(plat_drop_data(&p, &retained_len) == retained && retained_len == s.len &&
          !memcmp(retained, s.expect, retained_len), "clipboard completion preserves drop bytes");
    size_t clip_len = 0; const uint8_t *clip = plat_clip_data(&p, &clip_len);
    CHECK(clip && clip_len == sizeof clipboard && !memcmp(clip, clipboard, clip_len), "clipboard keeps its own bytes");
    unsigned drops = s.drops, done = a.finishes;
    enter(&a, VERSION, true, false); position(&p, &s, &a, NULL, true);
    message(&a, a.leave, 0, 0, 0, 0); message(&a, a.drop, 0, XCB_CURRENT_TIME, 0, 0);
    finished(&p, &s, &a, NULL, done, false);
    CHECK(s.drops == drops, "leave-before-drop has no event");
    CHECK(plat_drop_data(&p, &retained_len) == retained && retained_len == s.len, "leave retains last completed drop");
    done = a.finishes; enter(&a, VERSION, false, false); position(&p, &s, &a, NULL, false);
    message(&a, a.drop, 0, XCB_CURRENT_TIME, 0, 0); finished(&p, &s, &a, NULL, done, false);
    CHECK(s.drops == drops, "unsupported offer has no drop event");
    /* Silent first source holds the receive while another source is rejected. */
    own(&a, uri, sizeof uri - 1u, false, true); enter(&a, VERSION, true, false); position(&p, &s, &a, NULL, true);
    done = a.finishes; unsigned requests = a.requests;
    message(&a, a.drop, 0, XCB_CURRENT_TIME, 0, 0); wait_counter(&p, &s, &a, NULL, &a.requests, requests + 1u);
    unsigned other_done = b.finishes;
    enter(&b, VERSION, true, true); position(&p, &s, &b, &a, false);
    message(&b, b.drop, 0, XCB_CURRENT_TIME, 0, 0); finished(&p, &s, &b, &a, other_done, false);
    CHECK(a.finishes == done, "second source cannot finish the first source");
    /* The clipboard's existing deadline must wake a quiet loop without test pumping. */
    plat_callbacks cb = { .ud = &s, .on_event = on_event };
    CHECK(plat_run_for(&p, &cb, WAIT_MS) == PLAT_OK, "idle timeout loop"); peer_step(&a);
    CHECK(a.finishes == done + 1u && a.last_finished.data.data32[1] == 0 && s.drops == drops, "silent source times out with failure and no event");
    /* A late notify from that failed transfer cannot complete the next one. */
    notify(&a, &a.request, a.request.property);
    success(&p, &s, &a, false, false);
    /* Send a deliberately stale nonzero timestamp; peer receives it verbatim.
     * (P) 1 is the earliest non-CurrentTime X timestamp, not a measured age. */
    own(&a, uri, sizeof uri - 1u, false, false); enter(&a, VERSION, true, false); position(&p, &s, &a, NULL, true);
    done = a.finishes; requests = a.requests;
    message(&a, a.drop, 0, 1, 0, 0); finished(&p, &s, &a, NULL, done, true);
    CHECK(a.requests == requests + 1u && a.request.time == 1, "stale drop timestamp passed through");
    uint8_t *large = malloc(PAYLOAD_SIZE);
    CHECK(large != NULL, "INCR fixture allocation");
    if (large) {
        memset(large, 'u', PAYLOAD_SIZE); large[0] = 0; large[PAYLOAD_SIZE - 1u] = UINT8_MAX; /* (P) raw NUL/non-UTF8 boundary bytes */
        s.expect = large; s.len = PAYLOAD_SIZE; success(&p, &s, &a, true, true); free(large);
    }
    CHECK(s.clips == 1, "drop transfers emit no extra clipboard events");
    clip = plat_clip_data(&p, &clip_len);
    CHECK(clip && clip_len == sizeof clipboard && !memcmp(clip, clipboard, clip_len), "drop completion preserves clipboard bytes");
    xcb_disconnect(b.c); xcb_disconnect(a.c); plat_shutdown(&p);
    if (failures) { puts("x11_dnd_test: FAILED"); return EXIT_FAILURE; }
    puts("x11_dnd_test: ok"); return EXIT_SUCCESS;
}
