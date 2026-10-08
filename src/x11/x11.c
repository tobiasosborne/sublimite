/* x11.c - X11 window + event loop (P2.1). */
#include "plat.h"
#include "trace/trace.h"
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <xcb/xcb.h>
#include <xcb/present.h>

#define C(p) ((xcb_connection_t *)(p)->conn)

static xcb_atom_t atom(xcb_connection_t *c, const char *name) {
    xcb_intern_atom_reply_t *r =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

int plat_init(plat *p, const plat_config *cfg) {
    memset(p, 0, sizeof *p);
    p->timer_fd = p->work_fd = -1;
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
    xcb_flush(c);
    trace_record_at(trace_now_ns(), TRACE_T2_MUTATION_DONE, 0);
    p->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    p->work_fd = cfg->work_eventfd;
    if (p->timer_fd < 0) { plat_shutdown(p); return PLAT_ERR_FAIL; }
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
        ev.kind = PLAT_EV_KEY; ev.press = (t == XCB_KEY_PRESS); ev.code = x->detail; ev.state = x->state;
        ev.x = x->event_x; ev.y = x->event_y; ev.time_ms = x->time;
        trace_record_at(trace_now_ns(), TRACE_T0_INGRESS, 0);
        cb->on_event(cb->ud, &ev); break; }
    case XCB_BUTTON_PRESS: case XCB_BUTTON_RELEASE: {
        xcb_button_press_event_t *x = (xcb_button_press_event_t *)e;
        ev.kind = PLAT_EV_BUTTON; ev.press = (t == XCB_BUTTON_PRESS); ev.code = x->detail; ev.state = x->state;
        ev.x = x->event_x; ev.y = x->event_y; ev.time_ms = x->time;
        cb->on_event(cb->ud, &ev); break; }
    case XCB_MOTION_NOTIFY: {
        xcb_motion_notify_event_t *x = (xcb_motion_notify_event_t *)e;
        ev.kind = PLAT_EV_MOTION; ev.state = x->state; ev.x = x->event_x; ev.y = x->event_y; ev.time_ms = x->time;
        cb->on_event(cb->ud, &ev); break; }
    case XCB_GE_GENERIC: {
        xcb_ge_generic_event_t *g = (xcb_ge_generic_event_t *)e;
        if (p->present_ok && g->extension == p->present_opcode && g->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
            xcb_present_complete_notify_event_t *x = (xcb_present_complete_notify_event_t *)e;
            trace_record_at(trace_now_ns(), TRACE_T6_PRESENT_COMPLETE, x->serial);
            if (cb->on_present_complete) cb->on_present_complete(cb->ud, x->serial, x->ust, x->msc);
        }
        break; }
    default: break;
    }
}

int plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms) {
    struct pollfd fds[3];
    uint64_t t_end = trace_now_ns() + (timeout_ms > 0 ? (uint64_t)timeout_ms * UINT64_C(1000000) : 0);
    while (!p->quit) {
        int to = -1;
        if (timeout_ms >= 0) {
            uint64_t now = trace_now_ns();
            if (now >= t_end) break;
            to = (int)((t_end - now + UINT64_C(999999)) / UINT64_C(1000000));
        }
        nfds_t n = 0;
        fds[n].fd = xcb_get_file_descriptor(C(p)); fds[n++].events = POLLIN;
        fds[n].fd = p->timer_fd; fds[n++].events = POLLIN;
        if (p->work_fd >= 0) { fds[n].fd = p->work_fd; fds[n++].events = POLLIN; }
        int r = poll(fds, n, to);
        if (r < 0) continue;
        if (r == 0) break;
        p->iterations++;
        if (fds[1].revents & POLLIN) {
            uint64_t x; if (read(p->timer_fd, &x, sizeof x) > 0 && cb->on_blink) cb->on_blink(cb->ud);
        }
        if (n > 2 && (fds[2].revents & POLLIN) && cb->on_work) cb->on_work(cb->ud);
        if (fds[0].revents & (POLLERR | POLLHUP)) return PLAT_ERR_FAIL;
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_event(C(p)))) { dispatch(p, cb, e); free(e); }
        if (xcb_connection_has_error(C(p))) return PLAT_ERR_FAIL;
    }
    return PLAT_OK;
}

int plat_run(plat *p, const plat_callbacks *cb) { return plat_run_for(p, cb, -1); }

void plat_shutdown(plat *p) {
    if (p->timer_fd >= 0) close(p->timer_fd);
    if (p->conn) {
        if (p->win) xcb_destroy_window(C(p), p->win);
        xcb_disconnect(C(p));
    }
    memset(p, 0, sizeof *p);
    p->timer_fd = p->work_fd = -1;
}
