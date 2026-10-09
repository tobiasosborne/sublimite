/* x11.c - X11 window + event loop (P2.1). */
#include "plat.h"
#include "input.h"
#include "xi2.h"
#include "clip.h"
#include <limits.h>
#include "work/work.h"
#include <sys/socket.h>
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
    if (!in) { if (ct) xkb_compose_table_unref(ct); xkb_keymap_unref(km); xkb_context_unref(ctx); return PLAT_ERR_FAIL; }
    if (x11_input_init(in, km, ct) != 0) { free(in); xkb_keymap_unref(km); xkb_context_unref(ctx); return PLAT_ERR_FAIL; }
    in->ctx = ctx;
    p->in = in;
    /* Detectable autorepeat: the server stops sending release+press pairs for held keys (it sends repeated
     * presses without releases, which x11_input drops); our timer produces the repeats. */
    xcb_xkb_per_client_flags_reply_t *f = xcb_xkb_per_client_flags_reply(c,
        xcb_xkb_per_client_flags(c, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                 XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0), NULL);
    bool repeat_ok = f && (f->supported & XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT) &&
                     (f->value & XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT);
    free(f);
    if (!repeat_ok) return PLAT_ERR_FAIL;
    uint16_t ev = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                  XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
    uint16_t parts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP |
                     XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS |
                     XCB_XKB_MAP_PART_KEY_BEHAVIORS | XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    xcb_xkb_select_events(c, XCB_XKB_ID_USE_CORE_KBD, ev, 0, ev, parts, parts, NULL);
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

/* Runtime keymap I/O uses its own connection on the bulk worker. The UI
 * connection only sends/polls device queries; no dispatch path waits for replies. */
typedef struct keyboard_result {
    struct xkb_context *ctx;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    int32_t device;
} keyboard_result;

typedef struct x11_runtime {
    work_pool pool;
    xcb_connection_t *keyboard_conn;
    keyboard_result result;
    bool map_pending, map_dirty, xi_dirty;
    unsigned xi_cookie;
    plat *owner;
} x11_runtime;

static void keyboard_result_destroy(keyboard_result *r) {
    if (r->state) xkb_state_unref(r->state);
    if (r->keymap) xkb_keymap_unref(r->keymap);
    if (r->ctx) xkb_context_unref(r->ctx);
    memset(r, 0, sizeof *r);
}

static void rebuild_keyboard(work_ctx *job) {
    x11_runtime *rt = job->arg;
    keyboard_result r = {0};
    if (!work_should_stop(job)) {
        r.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        r.device = xkb_x11_get_core_keyboard_device_id(rt->keyboard_conn);
        if (r.ctx && r.device >= 0 && !work_should_stop(job))
            r.keymap = xkb_x11_keymap_new_from_device(r.ctx, rt->keyboard_conn, r.device,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (r.keymap && !work_should_stop(job)) r.state = x11_input_prepare_state(r.keymap);
    }
    /* One worker/job at a time; publish provides the synchronization. Shutdown
     * joins before reclaiming this result, even if cancellation drops the message. */
    rt->result = r;
    work_msg msg = { .kind = 1 };
    (void)work_publish(job, &msg);
}

static int setup_runtime(plat *p) {
    x11_runtime *rt = calloc(1, sizeof *rt);
    if (!rt) return PLAT_ERR_FAIL;
    rt->owner = p;
    rt->keyboard_conn = xcb_connect(NULL, NULL);
    if (!rt->keyboard_conn || xcb_connection_has_error(rt->keyboard_conn) ||
        !xkb_x11_setup_xkb_extension(rt->keyboard_conn, 1, 0, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                    NULL, NULL, NULL, NULL) || work_pool_init(&rt->pool, 1, 0) != 0) {
        if (rt->keyboard_conn) xcb_disconnect(rt->keyboard_conn);
        free(rt);
        return PLAT_ERR_FAIL;
    }
    IN(p)->runtime = rt;
    return PLAT_OK;
}

static void request_keymap(plat *p) {
    x11_runtime *rt = IN(p)->runtime;
    if (rt) rt->map_dirty = true; /* coalesce mapping storms into one outstanding build */
}

static void rearm_repeat(plat *p);

static void keyboard_ready(const work_msg *msg, void *ud) {
    (void)msg;
    x11_runtime *rt = ud;
    plat *p = rt->owner;
    keyboard_result *r = &rt->result;
    rt->map_pending = false;
    if (!rt->map_dirty && r->state) {
        x11_input_adopt_keymap(IN(p), r->keymap, r->state);
        if (IN(p)->ctx) xkb_context_unref(IN(p)->ctx);
        IN(p)->ctx = r->ctx;
        p->xkb_dev = r->device;
        rearm_repeat(p);
        memset(r, 0, sizeof *r);
        plat_event ev = { .kind = PLAT_EV_KEYMAP };
        (void)x11_q_push(IN(p), &ev);
    }
    keyboard_result_destroy(r);
}

static void request_xi2_rescan(plat *p) {
    x11_runtime *rt = IN(p)->runtime;
    if (rt) rt->xi_dirty = true;
}

static void poll_rescans(plat *p) {
    x11_runtime *rt = IN(p)->runtime;
    if (!rt) return;
    /* Apply a replacement only after preceding queued input was delivered. */
    if (IN(p)->qh == IN(p)->qt) (void)work_mailbox_drain(&rt->pool, keyboard_ready, rt);
    if (rt->map_dirty && !rt->map_pending) {
        work_job job = { rebuild_keyboard, rt, 0, WORK_BULK };
        work_handle h = work_submit(&rt->pool, job);
        if (h.epoch) { rt->map_pending = true; rt->map_dirty = false; }
    }
    if (!p->xi) return;
    if (rt->xi_cookie) {
        void *reply = NULL;
        xcb_generic_error_t *err = NULL;
        if (xcb_poll_for_reply(C(p), rt->xi_cookie, &reply, &err)) {
            rt->xi_cookie = 0;
            if (reply && !err && !rt->xi_dirty) {
                uint32_t length; memcpy(&length, (const uint8_t *)reply + 4, 4);
                (void)xi2_parse_query_device(XI(p), reply, 32 + (size_t)length * 4);
                p->core_wheel = !xi2_has_scroll(XI(p));
            }
            free(reply); free(err);
        }
    }
    if (rt->xi_dirty && !rt->xi_cookie) {
        uint8_t b[8];
        (void)xi2_build_query_device(XI(p)->opcode, b, 0);
        struct iovec parts[4];
        parts[2].iov_base = b; parts[2].iov_len = sizeof b;
        xcb_protocol_request_t rq = { 1, NULL, b[0], 0 };
        rt->xi_cookie = xcb_send_request(C(p), 0, parts + 2, &rq);
        rt->xi_dirty = false;
        xcb_flush(C(p));
    }
}

static void shutdown_runtime(plat *p) {
    x11_runtime *rt = IN(p)->runtime;
    if (!rt) return;
    if (rt->xi_cookie) xcb_discard_reply(C(p), rt->xi_cookie);
    /* Wake a worker blocked in xkbcommon's synchronous reply wait before join.
     * This is the dedicated worker connection; UI/XI/clipboard are unaffected. */
    (void)shutdown(xcb_get_file_descriptor(rt->keyboard_conn), SHUT_RDWR);
    work_pool_shutdown(&rt->pool);
    keyboard_result_destroy(&rt->result);
    xcb_disconnect(rt->keyboard_conn);
    free(rt);
    IN(p)->runtime = NULL;
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
    x11_repeat_configure(IN(p), delay_ms, rate_hz, trace_now_ns());
    rearm_repeat(p);
}

bool plat_poll_event(plat *p, plat_event *out) { return p->in && x11_q_pop(IN(p), out); }

int plat_init(plat *p, const plat_config *cfg) {
    memset(p, 0, sizeof *p);
    p->timer_fd = p->work_fd = p->repeat_fd = -1;
    if (cfg->exec_ns) trace_record_at(cfg->exec_ns, TRACE_T0_INGRESS, 0);
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
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_PROPERTY_CHANGE,
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
    if (setup_runtime(p) != PLAT_OK) { plat_shutdown(p); return PLAT_ERR_FAIL; }
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
        if (cb->on_event) cb->on_event(cb->ud, &ev);
        break; }
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *x = (xcb_configure_notify_event_t *)e;
        if (x->width != p->width || x->height != p->height) {
            p->width = x->width; p->height = x->height;
            ev.kind = PLAT_EV_RESIZE; ev.w = x->width; ev.h = x->height;
            if (cb->on_event) cb->on_event(cb->ud, &ev);
        }
        break; }
    case XCB_FOCUS_IN: case XCB_FOCUS_OUT: {
        const xcb_focus_in_event_t *f = (const xcb_focus_in_event_t *)e;
        if (!x11_focus_relevant(f->mode, f->detail)) break;    /* WM key grabs, pointer-only focus: keys may still be held */
        p->focused = (t == XCB_FOCUS_IN);
        if (!p->focused) { struct itimerspec z; memset(&z, 0, sizeof z); timerfd_settime(p->timer_fd, 0, &z, NULL); }
        /* releases while unfocused are never seen: forget held keys and any repeat or compose in flight */
        x11_input_focus_reset(IN(p)); rearm_repeat(p);
        ev.kind = PLAT_EV_FOCUS; ev.focused = p->focused;
        if (cb->on_event) cb->on_event(cb->ud, &ev);
        break; }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *x = (xcb_client_message_event_t *)e;
        if (x->type == p->wm_protocols && x->data.data32[0] == p->wm_delete) {
            ev.kind = PLAT_EV_CLOSE; if (cb->on_event) cb->on_event(cb->ud, &ev);
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
        ev.mods = x11_mods_from_state(IN(p), x->state); ev.buttons = x11_input_buttons(IN(p), x->state);
        ev.t0_ns = x11_clock_map(&IN(p)->clock, x->time, trace_now_ns());
        x11_q_push(IN(p), &ev); break; }
    case XCB_SELECTION_REQUEST: case XCB_SELECTION_CLEAR: case XCB_SELECTION_NOTIFY: case XCB_PROPERTY_NOTIFY:
        if (x11_clip_event(p, e, &ev)) x11_q_push(IN(p), &ev);
        break;
    case XCB_GE_GENERIC: {
        xcb_ge_generic_event_t *g = (xcb_ge_generic_event_t *)e;
        if (p->xi && g->extension == XI(p)->opcode) {
            xi2_result r;
            size_t n = 32 + 4 + (size_t)g->length * 4;     /* xcb inserts a 4-byte full_sequence after byte 32 */
            if (xi2_decode(XI(p), (const uint8_t *)e, n, true, &r)) {
                plat_event evs[2];
                if (r.device_changed) request_xi2_rescan(p);
                else if (r.time_ms) p->last_time = r.time_ms;
                int ne = x11_xi2_events(IN(p), &r, trace_now_ns(), evs);
                for (int i = 0; i < ne; i++) {
                    if (evs[i].kind == PLAT_EV_WHEEL) p->xi_scroll_ms = r.time_ms;
                    x11_q_push(IN(p), &evs[i]);
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
            if (xt == XCB_XKB_STATE_NOTIFY) {
                const xcb_xkb_state_notify_event_t *s = (const xcb_xkb_state_notify_event_t *)e;
                (void)x11_mods_from_state(IN(p), (uint32_t)s->mods | ((uint32_t)(s->group & 3u) << 13));
            }
            if (xt == XCB_XKB_NEW_KEYBOARD_NOTIFY || xt == XCB_XKB_MAP_NOTIFY) {
                request_keymap(p);
            }
        }
        break;
    }
}

/* One bounded slice. A true result means more buffered work may remain:
 * poll with timeout zero so timers/work run before the next slice. */
static bool drain(plat *p, const plat_callbacks *cb, uint64_t t_end) {
    uint64_t slice_end = trace_now_ns() + UINT64_C(1000000);
    if (t_end && t_end < slice_end) slice_end = t_end;
    for (unsigned n = 0; n < 64 && !p->quit; n++) {
        if (trace_now_ns() >= slice_end) return true;
        plat_event ev;
        /* Deliver preceding input before dispatching another raw event. This
         * reserves the entire ring for that event's (at most two) translations
         * and prevents direct callbacks/state changes from overtaking it. */
        if (x11_q_pop(IN(p), &ev)) {
            if (cb->on_event) cb->on_event(cb->ud, &ev);
            continue;
        }
        xcb_generic_event_t *e = xcb_poll_for_queued_event(C(p));
        if (!e) e = xcb_poll_for_event(C(p));
        if (e) { dispatch(p, cb, e); free(e); continue; }
        poll_rescans(p);
        bool progress = x11_clip_poll(p);
        /* Reply polling can both enqueue platform events and read X events.
         * Deliver its queued input before any subsequent raw callback. */
        if (IN(p)->qh != IN(p)->qt || progress) continue;
        e = xcb_poll_for_queued_event(C(p));
        if (e) { dispatch(p, cb, e); free(e); continue; }
        return false;
    }
    return true;
}

int plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms) {
    struct pollfd fds[5];
    uint64_t t_end = trace_now_ns() + (timeout_ms > 0 ? (uint64_t)timeout_ms * UINT64_C(1000000) : 0);
    while (!p->quit) {
        /* Expiry/reply progress also runs under a continuously readable X fd.
         * Clipboard internals retain their own transfer bounds. */
        poll_rescans(p);
        (void)x11_clip_poll(p);
        bool buffered = drain(p, cb, timeout_ms > 0 ? t_end : 0);
        if (xcb_connection_has_error(C(p))) return PLAT_ERR_FAIL;
        if (p->quit) break;
        int to = -1;
        if (timeout_ms >= 0) {
            uint64_t now = trace_now_ns();
            if (now >= t_end) break;
            to = (int)((t_end - now + UINT64_C(999999)) / UINT64_C(1000000));
        }
        uint64_t clip_deadline = x11_clip_deadline(p);
        if (clip_deadline) {
            uint64_t now = trace_now_ns();
            uint64_t ms = clip_deadline <= now ? 0 : (clip_deadline - now + UINT64_C(999999)) / UINT64_C(1000000);
            int clip_to = ms > INT_MAX ? INT_MAX : (int)ms;
            if (to < 0 || clip_to < to) to = clip_to;
        }
        if (buffered) to = 0;
        nfds_t n = 0;
        fds[n].fd = xcb_get_file_descriptor(C(p)); fds[n++].events = POLLIN;
        fds[n].fd = p->timer_fd; fds[n++].events = POLLIN;
        fds[n].fd = p->repeat_fd; fds[n++].events = POLLIN;
        int work_i = -1;
        if (p->work_fd >= 0) { work_i = (int)n; fds[n].fd = p->work_fd; fds[n++].events = POLLIN; }
        x11_runtime *rt = IN(p)->runtime;
        int keyboard_i = -1;
        if (rt) {
            keyboard_i = (int)n;
            fds[n].fd = work_pool_eventfd(&rt->pool); fds[n++].events = POLLIN;
        }
        int r = poll(fds, n, to);
        if (r < 0) continue;
        if (r == 0) continue;             /* clipboard deadline: drain expires it, keep running */
        p->iterations++;
        if (keyboard_i >= 0 && (fds[keyboard_i].revents & POLLIN)) poll_rescans(p);
        if (fds[1].revents & POLLIN) {
            uint64_t x; if (read(p->timer_fd, &x, sizeof x) > 0 && cb->on_blink) cb->on_blink(cb->ud);
            if (p->quit) break;
        }
        if (work_i >= 0 && (fds[work_i].revents & POLLIN) && cb->on_work) {
            cb->on_work(cb->ud);
            if (p->quit) break;
        }
        if (fds[2].revents & POLLIN) {
            uint64_t x; ssize_t rr = read(p->repeat_fd, &x, sizeof x); (void)rr;
            plat_event rev;
            uint64_t now = trace_now_ns();
            for (unsigned emitted = 0; emitted < 4 && !p->quit &&
                 IN(p)->qt - IN(p)->qh < X11_QUEUE_CAP && x11_repeat_poll(IN(p), now, &rev); emitted++) {
                trace_record_at(rev.t0_ns, TRACE_T0_INGRESS, 0);
                x11_q_push(IN(p), &rev);
            }
            rearm_repeat(p);
        }
        if (fds[0].revents & (POLLERR | POLLHUP)) return PLAT_ERR_FAIL;
        /* Next turn checks XCB queues even if these callbacks consumed fd readiness. */
        if (xcb_connection_has_error(C(p))) return PLAT_ERR_FAIL;
    }
    return PLAT_OK;
}

int plat_run(plat *p, const plat_callbacks *cb) { return plat_run_for(p, cb, -1); }

void plat_shutdown(plat *p) {
    if (p->in) shutdown_runtime(p);
    x11_clip_save_on_exit(p);
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
