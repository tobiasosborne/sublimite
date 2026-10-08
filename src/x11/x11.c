/* x11.c - X11 window + event loop (P2.1). */
#include "plat.h"
#include "input.h"
#include "xi2.h"
#include "clip.h"
#include "trace/trace.h"
#include <locale.h>
#include <xcb/xcbext.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <poll.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <xcb/xcb.h>
#include <xcb/present.h>

#define C(p) ((xcb_connection_t *)(p)->conn)
#define IN(p) ((x11_input *)(p)->in)
#define XI(p) ((xi2 *)(p)->xi)

static xcb_atom_t atom(xcb_connection_t *c, const char *name) {
    xcb_intern_atom_reply_t *r =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}


/* ---- keyboard setup (P2.2) ---- */
static struct xkb_keymap *load_keymap(plat *p, struct xkb_context *ctx) {
    return xkb_x11_keymap_new_from_device(ctx, C(p), p->xkb_dev, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

static int setup_keyboard(plat *p) {
    xcb_connection_t *c = C(p);
    uint8_t base = 0;
    if (!xkb_x11_setup_xkb_extension(c, 1, 0, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, NULL, NULL, &base, NULL))
        return PLAT_ERR_FAIL;
    p->xkb_event = base;
    p->xkb_dev = xkb_x11_get_core_keyboard_device_id(c);
    if (p->xkb_dev < 0) return PLAT_ERR_FAIL;
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) return PLAT_ERR_FAIL;
    struct xkb_keymap *km = load_keymap(p, ctx);
    if (!km) { xkb_context_unref(ctx); return PLAT_ERR_FAIL; }
    const char *loc = getenv("LC_ALL");
    if (!loc || !*loc) loc = getenv("LC_CTYPE");
    if (!loc || !*loc) loc = getenv("LANG");
    if (!loc || !*loc) loc = "C";
    struct xkb_compose_table *ct = xkb_compose_table_new_from_locale(ctx, loc, XKB_COMPOSE_COMPILE_NO_FLAGS);
    x11_input *in = calloc(1, sizeof *in);
    if (!in) { xkb_keymap_unref(km); xkb_context_unref(ctx); return PLAT_ERR_FAIL; }
    if (x11_input_init(in, km, ct) != 0) { free(in); xkb_keymap_unref(km); xkb_context_unref(ctx); return PLAT_ERR_FAIL; }
    in->ctx = ctx;
    p->in = in;
    /* Detectable autorepeat: the server stops sending release+press pairs for held keys (it sends repeated
     * presses without releases, which x11_input drops); our timer produces the repeats. */
    xcb_xkb_per_client_flags_reply_t *f = xcb_xkb_per_client_flags_reply(c,
        xcb_xkb_per_client_flags(c, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0), NULL);
    free(f);
    uint16_t ev = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY;
    uint16_t parts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP |
                     XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS |
                     XCB_XKB_MAP_PART_KEY_BEHAVIORS | XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    xcb_xkb_select_events(c, (xcb_xkb_device_spec_t)p->xkb_dev, ev, 0, ev, parts, parts, NULL);
    return PLAT_OK;
}

/* ---- XInput 2 setup (P2.2): hand-built requests, see xi2.h ---- */
static xcb_generic_error_t *xi_send(xcb_connection_t *c, uint8_t *buf, size_t len, bool reply, xcb_generic_error_t **err,
                                    void **out_reply) {
    struct iovec parts[4];
    parts[2].iov_base = buf; parts[2].iov_len = len;
    xcb_protocol_request_t rq = { 1, NULL, buf[0], reply ? 0 : 1 };
    unsigned int seq = xcb_send_request(c, reply ? 0 : XCB_REQUEST_CHECKED, parts + 2, &rq);
    *err = NULL;
    if (reply) *out_reply = xcb_wait_for_reply(c, seq, err);
    else { xcb_void_cookie_t ck = { seq }; *err = xcb_request_check(c, ck); }
    return *err;
}

static void setup_xi2(plat *p) {
    xcb_connection_t *c = C(p);
    static const char name[] = "XInputExtension";
    xcb_query_extension_reply_t *q = xcb_query_extension_reply(c, xcb_query_extension(c, sizeof name - 1, name), NULL);
    if (!q || !q->present) { free(q); return; }
    xi2 *x = calloc(1, sizeof *x);
    if (!x) { free(q); return; }
    x->opcode = q->major_opcode;
    free(q);
    uint8_t b[20];
    xcb_generic_error_t *err;
    void *rep = NULL;
    xi2_build_query_version(x->opcode, b, XI2_MIN_MAJOR, XI2_MIN_MINOR);
    xi_send(c, b, 8, true, &err, &rep);
    bool ok = rep && !err;
    if (ok) {
        const uint8_t *r = rep;
        uint16_t maj, min;
        memcpy(&maj, r + 8, 2); memcpy(&min, r + 10, 2);
        ok = maj > XI2_MIN_MAJOR || (maj == XI2_MIN_MAJOR && min >= XI2_MIN_MINOR);
    }
    free(rep); free(err);
    if (!ok) { free(x); return; }
    rep = NULL;
    xi2_build_query_device(x->opcode, b, 0);
    xi_send(c, b, 8, true, &err, &rep);
    if (rep && !err) {
        const uint8_t *r = rep;
        uint32_t l; memcpy(&l, r + 4, 4);
        xi2_parse_query_device(x, r, 32 + (size_t)l * 4);
    }
    free(rep); free(err);
    xi2_build_select_events(x->opcode, b, p->win);
    xi_send(c, b, 20, false, &err, &rep);
    if (err) { free(err); free(x); return; }
    x->active = true;
    p->xi = x;
    p->core_wheel = !xi2_has_scroll(x);
}

static void rescan_xi2(plat *p) {
    xi2 *x = XI(p);
    uint8_t b[8];
    xcb_generic_error_t *err;
    void *rep = NULL;
    xi2_build_query_device(x->opcode, b, 0);
    xi_send(C(p), b, 8, true, &err, &rep);
    if (rep && !err) {
        const uint8_t *r = rep;
        uint32_t l; memcpy(&l, r + 4, 4);
        xi2_parse_query_device(x, r, 32 + (size_t)l * 4);
    }
    free(rep); free(err);
    p->core_wheel = !xi2_has_scroll(x);
}

bool x11_push_event(plat *p, const plat_event *ev) { return x11_q_push(IN(p), ev); }

static void rearm_repeat(plat *p) {
    struct itimerspec it;
    memset(&it, 0, sizeof it);
    uint64_t d = x11_repeat_deadline(IN(p));
    if (d) { it.it_value.tv_sec = (time_t)(d / 1000000000ull); it.it_value.tv_nsec = (long)(d % 1000000000ull); }
    timerfd_settime(p->repeat_fd, TFD_TIMER_ABSTIME, &it, NULL);
}

void plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz) {
    if (!p->in) return;
    IN(p)->rep_delay_ms = delay_ms; IN(p)->rep_rate_hz = rate_hz;
    if (!rate_hz) x11_repeat_cancel(IN(p));
    rearm_repeat(p);
}

bool plat_poll_event(plat *p, plat_event *out) { return p->in && x11_q_pop(IN(p), out); }

int plat_init(plat *p, const plat_config *cfg) {
    memset(p, 0, sizeof *p);
    p->timer_fd = p->work_fd = p->repeat_fd = -1;
    if (cfg->headless) return PLAT_ERR_NO_DISPLAY;
    int scr_n = 0;
    xcb_connection_t *c = xcb_connect(NULL, &scr_n);
    if (!c || xcb_connection_has_error(c)) {
        if (c) xcb_disconnect(c);
        return PLAT_ERR_NO_DISPLAY;
    }
    p->conn = c;
    trace_record_at(trace_now_ns(), TRACE_T1_DEQUEUE, 0);
    const xcb_setup_t *setup = xcb_get_setup(c);
    xcb_screen_iterator_t si = xcb_setup_roots_iterator(setup);
    for (; scr_n > 0 && si.rem; scr_n--) xcb_screen_next(&si);
    xcb_screen_t *s = si.data;
    p->visual = s->root_visual;
    p->depth = s->root_depth;
    for (xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(s); di.rem; xcb_depth_next(&di)) {
        if (di.data->depth != 32) continue;
        xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
        for (; vi.rem; xcb_visualtype_next(&vi))
            if (vi.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR && vi.data->bits_per_rgb_value == 8) {
                p->visual = vi.data->visual_id; p->depth = 32; p->argb = true; goto found;
            }
    }
found:
    p->colormap = xcb_generate_id(c);
    xcb_create_colormap(c, XCB_COLORMAP_ALLOC_NONE, p->colormap, s->root, p->visual);
    p->win = xcb_generate_id(c);
    p->width = cfg->width; p->height = cfg->height;
    uint32_t vals[4] = {
        0, 0,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        p->colormap };
    /* value order: BACK_PIXEL, BORDER_PIXEL, EVENT_MASK, COLORMAP */
    xcb_create_window(c, p->depth, p->win, s->root, 0, 0, (uint16_t)cfg->width, (uint16_t)cfg->height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, p->visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, vals);
    const char *title = cfg->title ? cfg->title : "edit";
    xcb_atom_t a_name = atom(c, "_NET_WM_NAME"), a_utf8 = atom(c, "UTF8_STRING"), a_pid = atom(c, "_NET_WM_PID");
    p->wm_protocols = atom(c, "WM_PROTOCOLS");
    p->wm_delete = atom(c, "WM_DELETE_WINDOW");
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, p->win, a_name, a_utf8, 8, (uint32_t)strlen(title), title);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, p->win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, (uint32_t)strlen(title), title);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, p->win, p->wm_protocols, XCB_ATOM_ATOM, 32, 1, &p->wm_delete);
    uint32_t pid = (uint32_t)getpid();
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, p->win, a_pid, XCB_ATOM_CARDINAL, 32, 1, &pid);
    static const char cls[] = "edit\0edit";
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, p->win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof cls, cls);
    /* Present */
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_present_id);
    if (ext && ext->present) {
        xcb_present_query_version_reply_t *v = xcb_present_query_version_reply(
            c, xcb_present_query_version(c, 1, 0), NULL);
        if (v) {
            p->present_ok = true; p->present_opcode = ext->major_opcode; free(v);
            xcb_present_select_input(c, xcb_generate_id(c), p->win, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY);
        }
    }
    /* P2.2 input: keymap + XI2 + selections. Failure of the keyboard setup is fatal (no input is useless);
     * XI2 absence only degrades the wheel to core buttons. */
    if (setup_keyboard(p) != PLAT_OK) { plat_shutdown(p); return PLAT_ERR_FAIL; }
    setup_xi2(p);
    if (!p->xi) p->core_wheel = true;
    if (x11_clip_init(p) != PLAT_OK) { plat_shutdown(p); return PLAT_ERR_FAIL; }
    xcb_flush(c);
    trace_record_at(trace_now_ns(), TRACE_T2_MUTATION_DONE, 0);
    p->repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    p->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    p->work_fd = cfg->work_eventfd;
    if (p->timer_fd < 0 || p->repeat_fd < 0) { plat_shutdown(p); return PLAT_ERR_FAIL; }
    return PLAT_OK;
}

void plat_map(plat *p) {
    xcb_map_window(C(p), p->win);
    xcb_flush(C(p));
    trace_record_at(trace_now_ns(), TRACE_T4_PRESENT_SUBMITTED, 0);
}

void plat_set_blink(plat *p, uint32_t ms) {
    struct itimerspec it;
    memset(&it, 0, sizeof it);
    if (ms && p->focused) {
        it.it_value.tv_sec = ms / 1000; it.it_value.tv_nsec = (long)(ms % 1000) * 1000000L;
        it.it_interval = it.it_value;
    }
    timerfd_settime(p->timer_fd, 0, &it, NULL);
}

void plat_quit(plat *p) { p->quit = true; }

static void dispatch(plat *p, const plat_callbacks *cb, xcb_generic_event_t *e) {
    plat_event ev;
    memset(&ev, 0, sizeof ev);
    uint8_t t = e->response_type & 0x7f;
    switch (t) {
    case XCB_EXPOSE: {
        xcb_expose_event_t *x = (xcb_expose_event_t *)e;
        ev.kind = PLAT_EV_EXPOSE; ev.w = x->width; ev.h = x->height; ev.x = x->x; ev.y = x->y;
        cb->on_event(cb->ud, &ev); break; }
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *x = (xcb_configure_notify_event_t *)e;
        if (x->width != p->width || x->height != p->height) {
            p->width = x->width; p->height = x->height;
            ev.kind = PLAT_EV_RESIZE; ev.w = x->width; ev.h = x->height;
            cb->on_event(cb->ud, &ev);
        }
        break; }
    case XCB_FOCUS_IN: case XCB_FOCUS_OUT:
        p->focused = (t == XCB_FOCUS_IN);
        if (!p->focused) { struct itimerspec z; memset(&z, 0, sizeof z); timerfd_settime(p->timer_fd, 0, &z, NULL); }
        /* releases while unfocused are never seen: forget held keys and any repeat or compose in flight */
        memset(IN(p)->down, 0, sizeof IN(p)->down);
        x11_repeat_cancel(IN(p)); rearm_repeat(p);
        if (IN(p)->cstate) xkb_compose_state_reset(IN(p)->cstate);
        ev.kind = PLAT_EV_FOCUS; ev.focused = p->focused;
        cb->on_event(cb->ud, &ev); break;
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *x = (xcb_client_message_event_t *)e;
        if (x->type == p->wm_protocols && x->data.data32[0] == p->wm_delete) {
            ev.kind = PLAT_EV_CLOSE; cb->on_event(cb->ud, &ev);
        }
        break; }
    case XCB_KEY_PRESS: case XCB_KEY_RELEASE: {
        xcb_key_press_event_t *x = (xcb_key_press_event_t *)e;
        uint64_t now = trace_now_ns();
        p->last_time = x->time;
        if (x11_input_key(IN(p), x, t == XCB_KEY_PRESS, now, &ev)) {
            if (ev.press) trace_record_at(ev.t0_ns, TRACE_T0_INGRESS, 0);
            x11_q_push(IN(p), &ev);
        }
        rearm_repeat(p);
        break; }
    case XCB_BUTTON_PRESS: case XCB_BUTTON_RELEASE: {
        xcb_button_press_event_t *x = (xcb_button_press_event_t *)e;
        p->last_time = x->time;
        bool core_wheel = p->core_wheel;
        /* XI2 delivers the smooth scroll; its emulated core buttons share the timestamp: drop those */
        if (!core_wheel && p->xi && x->detail >= 4 && x->detail <= 7 && x->time - p->xi_scroll_ms > 2u &&
            p->xi_scroll_ms - x->time > 2u) core_wheel = true;   /* XI2 saw no scroll at this time: legacy device */
        if (x11_input_button(IN(p), x, t == XCB_BUTTON_PRESS, trace_now_ns(), core_wheel, &ev)) x11_q_push(IN(p), &ev);
        break; }
    case XCB_MOTION_NOTIFY: {
        xcb_motion_notify_event_t *x = (xcb_motion_notify_event_t *)e;
        if (p->xi) break;                         /* XI2 supplies motion */
        p->last_time = x->time;
        ev.kind = PLAT_EV_MOTION; ev.state = x->state; ev.x = x->event_x; ev.y = x->event_y; ev.time_ms = x->time;
        ev.mods = x11_mods_from_state(IN(p), x->state); ev.buttons = (x->state >> 8) & 0x1fu;
        ev.t0_ns = x11_clock_map(&IN(p)->clock, x->time, trace_now_ns());
        x11_q_push(IN(p), &ev); break; }
    case XCB_SELECTION_REQUEST: case XCB_SELECTION_CLEAR: case XCB_SELECTION_NOTIFY:
        if (x11_clip_event(p, e, &ev)) x11_q_push(IN(p), &ev);
        break;
    case XCB_GE_GENERIC: {
        xcb_ge_generic_event_t *g = (xcb_ge_generic_event_t *)e;
        if (p->xi && g->extension == XI(p)->opcode) {
            xi2_result r;
            size_t n = 32 + 4 + (size_t)g->length * 4;     /* xcb inserts a 4-byte full_sequence after byte 32 */
            if (xi2_decode(XI(p), (const uint8_t *)e, n, true, &r)) {
                uint64_t now = trace_now_ns();
                if (r.device_changed) rescan_xi2(p);
                if (r.time_ms) p->last_time = r.time_ms;
                uint64_t t0 = x11_clock_map(&IN(p)->clock, r.time_ms, now);
                uint16_t mods = x11_mods_from_state(IN(p), r.mods);
                if (r.motion) {
                    ev.kind = PLAT_EV_MOTION; ev.state = r.mods; ev.x = r.x; ev.y = r.y; ev.time_ms = r.time_ms;
                    ev.mods = mods; ev.buttons = r.buttons; ev.t0_ns = t0;
                    x11_q_push(IN(p), &ev);
                }
                if (r.wheel) {
                    memset(&ev, 0, sizeof ev);
                    ev.kind = PLAT_EV_WHEEL; ev.state = r.mods; ev.x = r.x; ev.y = r.y; ev.time_ms = r.time_ms;
                    ev.mods = mods; ev.buttons = r.buttons; ev.t0_ns = t0; ev.dx = r.dx; ev.dy = r.dy; ev.smooth = true;
                    p->xi_scroll_ms = r.time_ms;
                    x11_q_push(IN(p), &ev);
                }
            }
            break;
        }
        if (p->present_ok && g->extension == p->present_opcode && g->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
            xcb_present_complete_notify_event_t *x = (xcb_present_complete_notify_event_t *)e;
            trace_record_at(trace_now_ns(), TRACE_T6_PRESENT_COMPLETE, x->serial);
            if (cb->on_present_complete) cb->on_present_complete(cb->ud, x->serial, x->ust, x->msc);
        }
        break; }
    default:
        if (p->xkb_event && t == p->xkb_event) {
            /* XKB event: first byte after response_type is xkbType: 0 NewKeyboardNotify, 1 MapNotify */
            uint8_t xt = ((const uint8_t *)e)[1];
            if (xt == XCB_XKB_NEW_KEYBOARD_NOTIFY || xt == XCB_XKB_MAP_NOTIFY) {
                struct xkb_keymap *km = load_keymap(p, IN(p)->ctx);
                if (km && x11_input_set_keymap(IN(p), km) == 0) {
                    rearm_repeat(p);
                    ev.kind = PLAT_EV_KEYMAP; x11_q_push(IN(p), &ev);
                } else if (km) xkb_keymap_unref(km);
            }
        }
        break;
    }
}

static void drain(plat *p, const plat_callbacks *cb) {
    for (;;) {
        xcb_generic_event_t *e;
        /* A reply wait in any callback/dispatch can consume the fd and queue
         * events inside XCB. Check that queue first, then read the socket.
         * Repeat after each direct dispatch callback and each queued callback. */
        while ((e = xcb_poll_for_queued_event(C(p))) || (e = xcb_poll_for_event(C(p)))) {
            dispatch(p, cb, e);
            free(e);
        }
        bool progress = x11_clip_poll(p);
        plat_event ev;
        if (x11_q_pop(IN(p), &ev)) {
            if (cb->on_event) cb->on_event(cb->ud, &ev);
            continue;
        }
        /* Reply polling can itself read events, even if no reply completed. */
        e = xcb_poll_for_queued_event(C(p));
        if (e) { dispatch(p, cb, e); free(e); continue; }
        if (!progress) break;
    }
}

int plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms) {
    struct pollfd fds[4];
    uint64_t t_end = trace_now_ns() + (timeout_ms > 0 ? (uint64_t)timeout_ms * UINT64_C(1000000) : 0);
    while (!p->quit) {
        drain(p, cb);                     /* reach quiescence before every sleeping poll */
        if (xcb_connection_has_error(C(p))) return PLAT_ERR_FAIL;
        if (p->quit) break;
        int to = -1;
        if (timeout_ms >= 0) {
            uint64_t now = trace_now_ns();
            if (now >= t_end) break;
            to = (int)((t_end - now + UINT64_C(999999)) / UINT64_C(1000000));
        }
        nfds_t n = 0;
        fds[n].fd = xcb_get_file_descriptor(C(p)); fds[n++].events = POLLIN;
        fds[n].fd = p->timer_fd; fds[n++].events = POLLIN;
        fds[n].fd = p->repeat_fd; fds[n++].events = POLLIN;
        int work_i = -1;
        if (p->work_fd >= 0) { work_i = (int)n; fds[n].fd = p->work_fd; fds[n++].events = POLLIN; }
        int r = poll(fds, n, to);
        if (r < 0) continue;
        if (r == 0) break;
        p->iterations++;
        if (fds[1].revents & POLLIN) {
            uint64_t x; if (read(p->timer_fd, &x, sizeof x) > 0 && cb->on_blink) cb->on_blink(cb->ud);
            drain(p, cb);
        }
        if (work_i >= 0 && (fds[work_i].revents & POLLIN) && cb->on_work) {
            cb->on_work(cb->ud);
            drain(p, cb);
        }
        if (fds[2].revents & POLLIN) {
            uint64_t x; ssize_t rr = read(p->repeat_fd, &x, sizeof x); (void)rr;
            plat_event rev;
            uint64_t now = trace_now_ns();
            while (x11_repeat_poll(IN(p), now, &rev)) {
                trace_record_at(rev.t0_ns, TRACE_T0_INGRESS, 0);
                x11_q_push(IN(p), &rev);
            }
            rearm_repeat(p);
        }
        if (fds[0].revents & (POLLERR | POLLHUP)) return PLAT_ERR_FAIL;
        drain(p, cb);
        if (xcb_connection_has_error(C(p))) return PLAT_ERR_FAIL;
    }
    return PLAT_OK;
}

int plat_run(plat *p, const plat_callbacks *cb) { return plat_run_for(p, cb, -1); }

void plat_shutdown(plat *p) {
    if (p->timer_fd >= 0) close(p->timer_fd);
    if (p->repeat_fd >= 0) close(p->repeat_fd);
    x11_clip_destroy(p);
    free(p->xi);
    if (p->in) { x11_input_destroy(IN(p)); free(p->in); }
    if (p->conn) {
        if (p->win) xcb_destroy_window(C(p), p->win);
        xcb_disconnect(C(p));
    }
    memset(p, 0, sizeof *p);
    p->timer_fd = p->work_fd = p->repeat_fd = -1;
}
