#include "wayland/private.h"
#include "trace/trace.h"
#include <string.h>
#include <unistd.h>
#ifdef EDIT_HAVE_WAYLAND
#include "protocol/xdg-shell-client.h"
#include "protocol/presentation-client.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>

/* (E) Capacity inherits the established platform input queue (X11_QUEUE_CAP).
 * Clipboard ceiling inherits src/x11/clip.c DATA_MAX, not a new budget.
 * (P) Protocol versions/opcodes, keycode offset, buttons and fixed-point units
 * below are defined by wayland-client-protocol.h / Linux input / xkbcommon.
 * (P) ns conversion: SI seconds * 10^9, milliseconds * 10^6. */
#define WL_EVENT_CAP 256u
#define WL_CLIP_MAX (64u * 1024u * 1024u)
#define WL_CLIP_BUDGET (128u * 1024u * 1024u) /* inherits X11 OWN_BUDGET */
#define WL_CLIP_SENDS 16u /* inherits X11 NSERVE */
#define NS_SECOND UINT64_C(1000000000)
#define NS_MS UINT64_C(1000000)
typedef struct wayland_offer {
    struct wayland_offer *next;
    struct wl_data_offer *proxy;
    bool utf8, text;
} wayland_offer;
typedef struct wayland_blob { size_t refs, len; uint8_t bytes[]; } wayland_blob;
typedef struct wayland_tx { int fd; size_t offset; wayland_blob *blob; } wayland_tx;
typedef struct wayland_state {
    plat *p;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *shell;
    struct wl_surface *surface;
    struct xdg_surface *xdg;
    struct xdg_toplevel *top;
    struct wl_egl_window *window;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wl_data_device_manager *manager;
    struct wl_data_device *device;
    struct wl_data_source *source;
    wayland_offer *offers, *selection, *drag;
    struct wp_presentation *presentation;
    struct wp_presentation_feedback *feedback;
    struct wl_callback *frame;
    uint32_t compositor_name, shell_name, seat_name, presentation_name;
    uint32_t pending_width, pending_height;
    uint32_t frame_id, serial, buttons, repeat_key, delay_ms, rate_hz;
    int32_t x, y;
    bool configured, failed, frame_ready, displayed, discarded, notify;
    uint64_t presented_ns, msc;
    uint32_t clock_id;
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    struct xkb_state *keys;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose;
    uint16_t mods;
    int32_t axis[2], detent[2];
    bool axis_seen[2], detent_seen[2], axis_smooth;
    uint32_t axis_time;
    bool clock_valid; uint32_t clock_ms; uint64_t clock_ns;
    plat_event events[WL_EVENT_CAP];
    size_t head, count;
    wayland_blob *owned; uint8_t *received, *incoming;
    size_t received_len, received_cap, incoming_len, incoming_cap, clip_bytes;
    bool local_receive;
    int rx_fd; wayland_tx tx[WL_CLIP_SENDS];
    uint32_t blink_ms;
} wayland_state;
static wayland_state *W(const plat *p) { return p ? p->wayland : NULL; }
static struct wl_display *D(const plat *p) { return p->conn; }
static void push(wayland_state *s, plat_event ev)
{
    if (s->count == WL_EVENT_CAP) { s->failed = true; return; }
    if (!ev.t0_ns) ev.t0_ns = trace_now_ns();
    s->events[(s->head + s->count) % WL_EVENT_CAP] = ev;
    s->count++;
}
bool wayland_plat_poll_event(plat *p, plat_event *out)
{
    wayland_state *s = W(p);
    if (!s || !out || !s->count) return false;
    *out = s->events[s->head]; s->head = (s->head + 1) % WL_EVENT_CAP; s->count--;
    return true;
}
static void timer_arm(int fd, uint32_t delay, uint32_t rate)
{
    struct itimerspec it = {0};
    if (rate) {
        uint64_t ns = NS_SECOND / rate;
        it.it_interval.tv_sec = (time_t)(ns / NS_SECOND);
        it.it_interval.tv_nsec = (long)(ns % NS_SECOND);
        ns = (uint64_t)delay * NS_MS;
        if (!ns) ns = 1; /* timerfd: zero disarms; minimum nonzero SI ns */
        it.it_value.tv_sec = (time_t)(ns / NS_SECOND);
        it.it_value.tv_nsec = (long)(ns % NS_SECOND);
    }
    (void)timerfd_settime(fd, 0, &it, NULL);
}
void wayland_plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz)
{
    wayland_state *s = W(p); if (!s) return;
    s->delay_ms = delay_ms;
    /* At most one repeat per millisecond: same clamp as X11_REPEAT_MAX_HZ. */
    s->rate_hz = rate_hz > 1000u ? 1000u : rate_hz;
    if (s->repeat_key) timer_arm(p->repeat_fd, delay_ms, s->rate_hz);
}
void wayland_plat_set_blink(plat *p, uint32_t ms)
{
    wayland_state *s = W(p); if (!s) return;
    s->blink_ms = ms;
    struct itimerspec it = {0};
    if (p->focused && ms) {
        it.it_value.tv_sec = (time_t)(ms / 1000u);
        it.it_value.tv_nsec = (long)(ms % 1000u) * 1000000L;
        it.it_interval = it.it_value;
    }
    (void)timerfd_settime(p->timer_fd, 0, &it, NULL);
}
static void focus(wayland_state *s, bool focused)
{
    s->p->focused = focused; s->repeat_key = 0;
    timer_arm(s->p->repeat_fd, 0, 0);
    if (!focused && s->keys) {
        (void)xkb_state_update_mask(s->keys, 0, 0, 0, 0, 0, 0); s->mods = 0;
        if (s->compose) xkb_compose_state_reset(s->compose);
    }
    wayland_plat_set_blink(s->p, s->blink_ms);
    push(s, (plat_event){.kind = PLAT_EV_FOCUS, .focused = focused});
}
static void shell_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{ (void)data; xdg_wm_base_pong(shell, serial); }
static const struct xdg_wm_base_listener shell_listener = {.ping = shell_ping};
static void configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    wayland_state *s = data; xdg_surface_ack_configure(surface, serial);
    /* Toplevel dimensions form a configure transaction. Apply only when the
     * serial-bearing surface configure arrives, even if packets are split. */
    uint32_t width = s->pending_width ? s->pending_width : s->p->width;
    uint32_t height = s->pending_height ? s->pending_height : s->p->height;
    if (s->p->width != width || s->p->height != height) {
        s->p->width = width; s->p->height = height;
        if (s->window) wl_egl_window_resize(s->window, (int)width, (int)height, 0, 0);
        push(s, (plat_event){.kind = PLAT_EV_RESIZE, .w = width, .h = height});
    }
    s->configured = true;
    push(s, (plat_event){.kind = PLAT_EV_EXPOSE, .w = s->p->width, .h = s->p->height});
}
static const struct xdg_surface_listener xdg_listener = {.configure = configure};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t width, int32_t height, struct wl_array *states)
{
    (void)top; (void)states; wayland_state *s = data;
    /* xdg-shell zero means client chooses, independently for each dimension. */
    s->pending_width = width > 0 ? (uint32_t)width : s->p->width;
    s->pending_height = height > 0 ? (uint32_t)height : s->p->height;
}
static void top_close(void *data, struct xdg_toplevel *top)
{ (void)top; push(data, (plat_event){.kind = PLAT_EV_CLOSE}); }
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size)
{
    (void)keyboard; wayland_state *s = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size) { close(fd); s->failed = true; return; }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    if (map == MAP_FAILED) { s->failed = true; return; }
    struct xkb_keymap *keymap = map[size - 1] == '\0' ?
        xkb_keymap_new_from_string(s->context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    munmap(map, size);
    struct xkb_state *keys = keymap ? xkb_state_new(keymap) : NULL;
    if (!keys) { xkb_keymap_unref(keymap); s->failed = true; return; }
    xkb_state_unref(s->keys); xkb_keymap_unref(s->keymap);
    s->keys = keys; s->keymap = keymap; s->repeat_key = 0; s->mods = 0;
    timer_arm(s->p->repeat_fd, 0, 0);
    if (s->compose) xkb_compose_state_reset(s->compose);
    push(s, (plat_event){.kind = PLAT_EV_KEYMAP});
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{ (void)keyboard; (void)surface; (void)keys; wayland_state *s = data; s->serial = serial; focus(s, true); }
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface)
{ (void)keyboard; (void)surface; wayland_state *s = data; s->serial = serial; focus(s, false); }
static uint64_t input_stamp(wayland_state *s, uint32_t ms)
{
    uint64_t now = trace_now_ns();
    if (!s->clock_valid) { s->clock_valid = true; s->clock_ms = ms; s->clock_ns = now; return now; }
    uint32_t raw = ms - s->clock_ms;
    int64_t delta = raw <= INT32_MAX ? (int64_t)raw : -((int64_t)UINT32_MAX - raw + 1);
    uint64_t distance = (uint64_t)(delta < 0 ? -delta : delta) * NS_MS;
    uint64_t stamp;
    if (delta < 0) stamp = s->clock_ns >= distance ? s->clock_ns - distance : now;
    else stamp = s->clock_ns <= UINT64_MAX - distance ? s->clock_ns + distance : now;
    /* Server epoch is unspecified. As with X11, first observation calibrates
     * the offset, and an earlier arrival tightens it. Never stamp the future. */
    if (stamp > now) { s->clock_ms = ms; s->clock_ns = now; return now; }
    return stamp;
}
static void text(plat_event *ev, const char *buf, int len)
{
    if (len <= 0 || len > PLAT_UTF8_MAX || (ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_ALT | PLAT_MOD_SUPER)) ||
        (uint8_t)buf[0] < 0x20u || (uint8_t)buf[0] == 0x7fu) return;
    memcpy(ev->utf8, buf, (size_t)len); ev->utf8_len = (uint8_t)len;
}
bool wayland_input_key(plat *p, uint32_t key, uint32_t time, bool press, bool repeat)
{
    wayland_state *s = W(p);
    if (!s || !s->keys || key > UINT32_MAX - 8u) return false; /* xkbcommon evdev offset */
    uint32_t code = key + 8u;
    plat_event ev = {.kind = PLAT_EV_KEY, .code = code, .time_ms = time, .press = press,
        .repeat = repeat, .mods = s->mods, .keysym = xkb_state_key_get_one_sym(s->keys, code), .t0_ns = repeat ? trace_now_ns() : input_stamp(s, time)};
    if (press) {
        char buf[PLAT_UTF8_MAX + 1]; int len = 0;
        if (!repeat && s->compose) {
            (void)xkb_compose_state_feed(s->compose, ev.keysym);
            enum xkb_compose_status status = xkb_compose_state_get_status(s->compose);
            if (status == XKB_COMPOSE_COMPOSING) return false;
            if (status == XKB_COMPOSE_COMPOSED) {
                ev.keysym = xkb_compose_state_get_one_sym(s->compose);
                len = xkb_compose_state_get_utf8(s->compose, buf, sizeof buf);
                xkb_compose_state_reset(s->compose);
            } else if (status == XKB_COMPOSE_CANCELLED) { xkb_compose_state_reset(s->compose); return false; }
            else len = xkb_state_key_get_utf8(s->keys, code, buf, sizeof buf);
        } else len = xkb_state_key_get_utf8(s->keys, code, buf, sizeof buf);
        text(&ev, buf, len);
    }
    push(s, ev); return true;
}
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)keyboard; wayland_state *s = data; s->serial = serial; s->p->last_time = time;
    bool press = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    bool emitted = wayland_input_key(s->p, key, time, press, false);
    if (press && emitted && s->keymap && key <= UINT32_MAX - 8u && xkb_keymap_key_repeats(s->keymap, key + 8u)) {
        s->repeat_key = key + 8u; timer_arm(s->p->repeat_fd, s->delay_ms, s->rate_hz);
    } else if (!press && key <= UINT32_MAX - 8u && s->repeat_key == key + 8u) {
        s->repeat_key = 0; timer_arm(s->p->repeat_fd, 0, 0);
    }
}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    (void)keyboard; wayland_state *s = data; s->serial = serial;
    if (!s->keys) return;
    (void)xkb_state_update_mask(s->keys, depressed, latched, locked, 0, 0, group);
    const char *names[] = {XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CTRL, XKB_MOD_NAME_ALT, XKB_MOD_NAME_LOGO, XKB_MOD_NAME_CAPS, XKB_MOD_NAME_NUM, "Mod5"};
    s->mods = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (xkb_state_mod_name_is_active(s->keys, names[i], XKB_STATE_MODS_EFFECTIVE) > 0)
            s->mods = (uint16_t)(s->mods | (1u << i));
}
static void keyboard_repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{ (void)keyboard; wayland_state *s = data; wayland_plat_set_repeat(s->p, delay > 0 ? (uint32_t)delay : 0, rate > 0 ? (uint32_t)rate : 0); }
static const struct wl_keyboard_listener keyboard_listener = {.keymap = keyboard_keymap, .enter = keyboard_enter,
    .leave = keyboard_leave, .key = keyboard_key, .modifiers = keyboard_modifiers, .repeat_info = keyboard_repeat};
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{ (void)pointer; (void)surface; wayland_state *s = data; s->serial = serial; s->x = wl_fixed_to_int(x); s->y = wl_fixed_to_int(y); }
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{ (void)pointer; (void)surface; wayland_state *s = data; s->serial = serial; }
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)pointer; wayland_state *s = data; s->x = wl_fixed_to_int(x); s->y = wl_fixed_to_int(y);
    push(s, (plat_event){.kind = PLAT_EV_MOTION, .x = s->x, .y = s->y, .buttons = s->buttons, .mods = s->mods, .time_ms = time, .t0_ns = input_stamp(s, time)});
}
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    (void)pointer; wayland_state *s = data; s->serial = serial;
    uint32_t code;
    switch (button) { /* Linux input-event-codes.h BTN_* -> plat.h button ABI */
    case 0x110: code = 1; break; case 0x111: code = 3; break; case 0x112: code = 2; break;
    case 0x113: case 0x116: code = 8; break; case 0x114: case 0x115: code = 9; break; default: return;
    }
    bool press = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (press) s->buttons |= 1u << (code - 1); else s->buttons &= ~(1u << (code - 1));
    push(s, (plat_event){.kind = PLAT_EV_BUTTON, .press = press, .code = code, .buttons = s->buttons,
        .x = s->x, .y = s->y, .mods = s->mods, .time_ms = time, .t0_ns = input_stamp(s, time)});
}
static int32_t axis_add(int32_t old, int64_t delta)
{
    int64_t sum = old + delta;
    return sum > INT32_MAX ? INT32_MAX : sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
}
static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)pointer; wayland_state *s = data;
    plat_event ev = {.kind = PLAT_EV_WHEEL, .smooth = s->axis_smooth, .x = s->x, .y = s->y,
        .mods = s->mods, .buttons = s->buttons, .time_ms = s->axis_time, .t0_ns = input_stamp(s, s->axis_time)};
    ev.dy = s->detent_seen[WL_POINTER_AXIS_VERTICAL_SCROLL] ? s->detent[WL_POINTER_AXIS_VERTICAL_SCROLL] : s->axis[WL_POINTER_AXIS_VERTICAL_SCROLL];
    ev.dx = s->detent_seen[WL_POINTER_AXIS_HORIZONTAL_SCROLL] ? s->detent[WL_POINTER_AXIS_HORIZONTAL_SCROLL] : s->axis[WL_POINTER_AXIS_HORIZONTAL_SCROLL];
    if (s->axis_seen[0] || s->axis_seen[1] || s->detent_seen[0] || s->detent_seen[1]) push(s, ev);
    memset(s->axis, 0, sizeof s->axis); memset(s->detent, 0, sizeof s->detent);
    memset(s->axis_seen, 0, sizeof s->axis_seen); memset(s->detent_seen, 0, sizeof s->detent_seen);
}
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    wayland_state *s = data; if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    /* (E) fallback for continuous surface-unit deltas: libinput's customary
     * wheel step is ten units. Discrete/value120 overrides this per frame. */
    s->axis[axis] = axis_add(s->axis[axis], value / 10); s->axis_seen[axis] = true; s->axis_time = time;
    if (wl_proxy_get_version((struct wl_proxy *)pointer) < WL_POINTER_FRAME_SINCE_VERSION) {
        s->axis_smooth = true; pointer_frame(s, pointer);
    }
}
static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{ (void)pointer; wayland_state *s = data; s->axis_smooth = source != WL_POINTER_AXIS_SOURCE_WHEEL && source != WL_POINTER_AXIS_SOURCE_WHEEL_TILT; }
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{ (void)data; (void)pointer; (void)time; (void)axis; }
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t steps)
{
    (void)pointer; wayland_state *s = data; if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    s->detent[axis] = axis_add(s->detent[axis], (int64_t)steps * PLAT_WHEEL_UNIT); s->detent_seen[axis] = true;
}
static void pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value)
{
    (void)pointer; wayland_state *s = data; if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    /* (P) core protocol: 120 = a detent; plat.h: PLAT_WHEEL_UNIT = a notch. */
    s->detent[axis] = axis_add(s->detent[axis], (int64_t)value * PLAT_WHEEL_UNIT / 120); s->detent_seen[axis] = true;
}
static const struct wl_pointer_listener pointer_listener = {.enter = pointer_enter, .leave = pointer_leave,
    .motion = pointer_motion, .button = pointer_button, .axis = pointer_axis, .frame = pointer_frame,
    .axis_source = pointer_axis_source, .axis_stop = pointer_axis_stop, .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120};
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    wayland_state *s = data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) {
        s->keyboard = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && s->keyboard) {
        wl_keyboard_release(s->keyboard); s->keyboard = NULL; focus(s, false);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
        s->pointer = wl_seat_get_pointer(seat); wl_pointer_add_listener(s->pointer, &pointer_listener, s);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
        wl_pointer_release(s->pointer); s->pointer = NULL; s->buttons = 0;
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data; (void)seat; (void)name; }
static const struct wl_seat_listener seat_listener = {.capabilities = seat_capabilities, .name = seat_name};
static void presentation_clock(void *data, struct wp_presentation *presentation, uint32_t id)
{ (void)presentation; wayland_state *s = data; s->clock_id = id; }
static const struct wp_presentation_listener presentation_listener = {.clock_id = presentation_clock};
static void offer_mime(void *data, struct wl_data_offer *offer, const char *mime)
{
    (void)offer; wayland_offer *o = data;
    if (!strcmp(mime, "text/plain;charset=utf-8")) o->utf8 = true;
    if (!strcmp(mime, "text/plain")) o->text = true;
}
static const struct wl_data_offer_listener offer_listener = {.offer = offer_mime};
static void offer_destroy(wayland_state *s, wayland_offer *o)
{
    wayland_offer **at = &s->offers;
    while (*at && *at != o) at = &(*at)->next;
    if (*at) { *at = o->next; wl_data_offer_destroy(o->proxy); free(o); }
}
static void device_offer(void *data, struct wl_data_device *device, struct wl_data_offer *proxy)
{
    (void)device; wayland_state *s = data;
    wayland_offer *o = calloc(1, sizeof *o);
    if (!o) { wl_data_offer_destroy(proxy); s->failed = true; return; }
    o->proxy = proxy; o->next = s->offers; s->offers = o;
    wl_data_offer_add_listener(proxy, &offer_listener, o);
}
static void device_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    (void)device; (void)surface; (void)x; (void)y; wayland_state *s = data;
    for (wayland_offer *o = s->offers; o; o = o->next) if (o->proxy == offer) s->drag = o;
    if (offer) wl_data_offer_accept(offer, serial, NULL); /* file drops outside this backend's scope */
}
static void device_leave(void *data, struct wl_data_device *device)
{
    (void)device; wayland_state *s = data;
    if (s->drag && s->drag != s->selection) offer_destroy(s, s->drag);
    s->drag = NULL;
}
static void device_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{ (void)data; (void)device; (void)time; (void)x; (void)y; }
static void device_drop(void *data, struct wl_data_device *device) { device_leave(data, device); }
static void device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *proxy)
{
    (void)device; wayland_state *s = data;
    wayland_offer *selection = NULL;
    for (wayland_offer *o = s->offers; o; o = o->next) if (o->proxy == proxy) selection = o;
    if (s->selection && s->selection != selection) offer_destroy(s, s->selection);
    s->selection = selection;
}
static const struct wl_data_device_listener device_listener = {.data_offer = device_offer, .enter = device_enter,
    .leave = device_leave, .motion = device_motion, .drop = device_drop, .selection = device_selection};
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    wayland_state *s = data;
    if (!strcmp(interface, "wl_compositor") && !s->compositor) {
        s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4); s->compositor_name = name;
    } else if (!strcmp(interface, "xdg_wm_base") && !s->shell) {
        s->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1); s->shell_name = name;
        xdg_wm_base_add_listener(s->shell, &shell_listener, s);
    } else if (!strcmp(interface, "wl_seat") && !s->seat && version >= 4) {
        s->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < WL_POINTER_AXIS_VALUE120_SINCE_VERSION ? version : WL_POINTER_AXIS_VALUE120_SINCE_VERSION); s->seat_name = name;
        wl_seat_add_listener(s->seat, &seat_listener, s);
    } else if (!strcmp(interface, "wl_data_device_manager") && !s->manager) {
        s->manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 1);
    } else if (!strcmp(interface, "wp_presentation") && !s->presentation) {
        s->presentation = wl_registry_bind(registry, name, &wp_presentation_interface, 1); s->presentation_name = name;
        wp_presentation_add_listener(s->presentation, &presentation_listener, s);
    }
}
static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)registry; wayland_state *s = data;
    if (name == s->compositor_name || name == s->shell_name || name == s->seat_name || name == s->presentation_name) s->failed = true;
}
static const struct wl_registry_listener registry_listener = {.global = registry_global, .global_remove = registry_remove};
int wayland_plat_init(plat *p, const plat_config *cfg)
{
    memset(p, 0, sizeof *p); p->backend = PLAT_BACKEND_WAYLAND;
    p->timer_fd = p->repeat_fd = p->work_fd = -1;
    if (cfg->exec_ns) trace_record_at(cfg->exec_ns, TRACE_T0_INGRESS, 0);
    if (cfg->headless) return PLAT_ERR_NO_DISPLAY;
    if (!cfg->width || !cfg->height || cfg->width > INT32_MAX || cfg->height > INT32_MAX) return PLAT_ERR_FAIL;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) return PLAT_ERR_NO_DISPLAY;
    p->conn = display; p->width = cfg->width; p->height = cfg->height;
    wayland_state *s = calloc(1, sizeof *s); p->wayland = s;
    if (!s) goto fail;
    s->p = p; s->rx_fd = -1;
    for (size_t i = 0; i < WL_CLIP_SENDS; i++) s->tx[i].fd = -1;
    s->clock_id = UINT32_MAX;
    p->work_fd = cfg->work_eventfd;
    p->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    p->repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (p->timer_fd < 0 || p->repeat_fd < 0) goto fail;
    s->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS); if (!s->context) goto fail;
    const char *locale = setlocale(LC_CTYPE, NULL);
    s->compose_table = xkb_compose_table_new_from_locale(s->context, locale ? locale : "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (s->compose_table) s->compose = xkb_compose_state_new(s->compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    trace_record_at(trace_now_ns(), TRACE_T1_DEQUEUE, 0);
    s->registry = wl_display_get_registry(display); wl_registry_add_listener(s->registry, &registry_listener, s);
    if (wl_display_roundtrip(display) < 0 || !s->compositor || !s->shell || !s->seat) goto fail;
    if (s->manager) {
        s->device = wl_data_device_manager_get_data_device(s->manager, s->seat);
        wl_data_device_add_listener(s->device, &device_listener, s);
    }
    if (wl_display_roundtrip(display) < 0 || s->failed) goto fail;
    s->surface = wl_compositor_create_surface(s->compositor);
    s->xdg = xdg_wm_base_get_xdg_surface(s->shell, s->surface); xdg_surface_add_listener(s->xdg, &xdg_listener, s);
    s->top = xdg_surface_get_toplevel(s->xdg); xdg_toplevel_add_listener(s->top, &top_listener, s);
    xdg_toplevel_set_title(s->top, cfg->title ? cfg->title : "sublimité"); xdg_toplevel_set_app_id(s->top, "sublimite");
    /* xdg-shell requires an empty commit and ack before any EGL buffer. */
    wl_surface_commit(s->surface);
    while (!s->configured && !s->failed) if (wl_display_dispatch(display) < 0) goto fail;
    if (s->failed) goto fail;
    s->window = wl_egl_window_create(s->surface, (int)p->width, (int)p->height); if (!s->window) goto fail;
    p->present_ok = s->presentation != NULL;
    trace_record_at(trace_now_ns(), TRACE_T2_MUTATION_DONE, 0);
    return PLAT_OK;
fail:
    wayland_plat_shutdown(p); return PLAT_ERR_FAIL;
}
void wayland_plat_map(plat *p)
{ if (W(p)) { (void)wl_display_flush(D(p)); trace_record_at(trace_now_ns(), TRACE_T4_PRESENT_SUBMITTED, 0); } }
int wayland_plat_set_present_events(plat *p, bool enabled)
{ (void)enabled; return W(p) ? PLAT_OK : PLAT_ERR_FAIL; }
void *wayland_egl_window(const plat *p) { return W(p) ? W(p)->window : NULL; }
void *wayland_surface(const plat *p) { return W(p) ? W(p)->surface : NULL; }
static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
    (void)time; wayland_state *s = data;
    if (s->frame == callback) { s->frame_ready = true; s->frame = NULL; }
    wl_callback_destroy(callback);
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};
static void feedback_output(void *data, struct wp_presentation_feedback *feedback, struct wl_output *output)
{ (void)data; (void)feedback; (void)output; }
static void feedback_presented(void *data, struct wp_presentation_feedback *feedback, uint32_t hi, uint32_t lo, uint32_t ns, uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
    (void)refresh; (void)flags; wayland_state *s = data;
    if (s->feedback == feedback) {
        uint64_t now = trace_now_ns(), sec = ((uint64_t)hi << 32) | lo;
        s->presented_ns = now;
        if (s->clock_id == (uint32_t)CLOCK_MONOTONIC && ns < NS_SECOND && sec <= (UINT64_MAX - ns) / NS_SECOND) {
            uint64_t stamp = sec * NS_SECOND + ns; if (stamp <= now) s->presented_ns = stamp;
        }
        s->msc = ((uint64_t)seq_hi << 32) | seq_lo;
        s->displayed = true; s->notify = true; s->feedback = NULL;
    }
    wp_presentation_feedback_destroy(feedback);
}
static void feedback_discarded(void *data, struct wp_presentation_feedback *feedback)
{
    wayland_state *s = data;
    if (s->feedback == feedback) { s->discarded = true; s->notify = true; s->feedback = NULL; }
    wp_presentation_feedback_destroy(feedback);
}
static const struct wp_presentation_feedback_listener feedback_listener = {.sync_output = feedback_output,
    .presented = feedback_presented, .discarded = feedback_discarded};
void wayland_frame_cancel(plat *p)
{
    wayland_state *s = W(p); if (!s) return;
    if (s->frame) wl_callback_destroy(s->frame);
    if (s->feedback) wp_presentation_feedback_destroy(s->feedback);
    s->frame = NULL; s->feedback = NULL; s->frame_id = 0; s->notify = false;
}
int wayland_frame_arm(plat *p, uint32_t id)
{
    wayland_state *s = W(p);
    if (!s || s->failed || !s->presentation || !id || (s->frame_id && !s->displayed && !s->discarded)) return PLAT_ERR_FAIL;
    wayland_frame_cancel(p);
    s->frame_id = id; s->displayed = s->discarded = s->frame_ready = false;
    s->presented_ns = s->msc = 0;
    s->frame = wl_surface_frame(s->surface); if (!s->frame) goto fail;
    wl_callback_add_listener(s->frame, &frame_listener, s);
    s->feedback = wp_presentation_feedback(s->presentation, s->surface); if (!s->feedback) goto fail;
    wp_presentation_feedback_add_listener(s->feedback, &feedback_listener, s);
    return PLAT_OK;
fail:
    wayland_frame_cancel(p); return PLAT_ERR_FAIL;
}
int wayland_frame_result(plat *p, uint32_t id, bool *ready, uint64_t *ns, uint64_t *msc)
{
    wayland_state *s = W(p);
    if (!s || !ready || !ns || !msc || id != s->frame_id || s->failed || s->discarded) return PLAT_ERR_FAIL;
    *ready = s->displayed; *ns = s->presented_ns; *msc = s->msc; return PLAT_OK;
}
static void clip_result(wayland_state *s, bool ok, uint32_t code)
{ push(s, (plat_event){.kind = PLAT_EV_CLIPBOARD, .clip_ok = ok, .code = code, .clip_which = PLAT_CLIP_CLIPBOARD}); }
static void blob_unref(wayland_state *s, wayland_blob *blob)
{
    if (blob && !--blob->refs) { s->clip_bytes -= blob->len; free(blob); }
}
static void tx_close(wayland_state *s, wayland_tx *tx)
{ close(tx->fd); tx->fd = -1; blob_unref(s, tx->blob); tx->blob = NULL; }
static void source_target(void *data, struct wl_data_source *source, const char *mime) { (void)data; (void)source; (void)mime; }
static void source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd)
{
    wayland_state *s = data;
    if (source != s->source || !s->owned || (strcmp(mime, "text/plain;charset=utf-8") && strcmp(mime, "text/plain")) ||
        fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return; }
    for (size_t i = 0; i < WL_CLIP_SENDS; i++) if (s->tx[i].fd < 0) {
        s->tx[i] = (wayland_tx){.fd = fd, .blob = s->owned}; s->owned->refs++; return;
    }
    close(fd); /* the established X11 transfer-capacity bound applies here too */
}
static void source_cancelled(void *data, struct wl_data_source *source)
{
    wayland_state *s = data;
    if (s->source == source) { s->source = NULL; blob_unref(s, s->owned); s->owned = NULL; clip_result(s, false, 2); }
    wl_data_source_destroy(source);
}
static const struct wl_data_source_listener source_listener = {.target = source_target, .send = source_send, .cancelled = source_cancelled};
int wayland_plat_clip_set(plat *p, int which, const void *utf8, size_t len)
{
    wayland_state *s = W(p);
    if (!s || which != PLAT_CLIP_CLIPBOARD || !s->manager || !s->device || !s->serial ||
        len > WL_CLIP_MAX || (len && !utf8)) return PLAT_ERR_FAIL;
    size_t reclaim = s->owned && s->owned->refs == 1 ? s->owned->len : 0;
    if (len > WL_CLIP_BUDGET - (s->clip_bytes - reclaim)) return PLAT_ERR_FAIL;
    wayland_blob *copy = malloc(sizeof *copy + len); if (!copy) return PLAT_ERR_FAIL;
    copy->refs = 1; copy->len = len; if (len) memcpy(copy->bytes, utf8, len);
    struct wl_data_source *source = wl_data_device_manager_create_data_source(s->manager);
    if (!source) { free(copy); return PLAT_ERR_FAIL; }
    wl_data_source_add_listener(source, &source_listener, s);
    if (s->source) wl_data_source_destroy(s->source);
    blob_unref(s, s->owned); s->owned = copy; s->clip_bytes += len; s->source = source;
    wl_data_source_offer(s->source, "text/plain;charset=utf-8"); wl_data_source_offer(s->source, "text/plain");
    wl_data_device_set_selection(s->device, s->source, s->serial); return PLAT_OK;
}
static void receive_complete(wayland_state *s)
{
    uint8_t *old = s->received; size_t old_cap = s->received_cap;
    s->received = s->incoming; s->received_len = s->incoming_len; s->received_cap = s->incoming_cap;
    s->incoming = old; s->incoming_cap = old_cap; s->incoming_len = 0;
    clip_result(s, true, 0);
}
int wayland_plat_clip_request(plat *p, int which)
{
    wayland_state *s = W(p);
    if (!s || which != PLAT_CLIP_CLIPBOARD || s->rx_fd >= 0 || s->local_receive) return PLAT_ERR_FAIL;
    if (s->owned && s->source) {
        if (s->incoming_cap < s->owned->len) {
            uint8_t *copy = realloc(s->incoming, s->owned->len); if (!copy) return PLAT_ERR_FAIL;
            s->incoming = copy; s->incoming_cap = s->owned->len;
        }
        s->incoming_len = s->owned->len;
        if (s->incoming_len) memcpy(s->incoming, s->owned->bytes, s->incoming_len);
        s->local_receive = true; return PLAT_OK;
    }
    s->incoming_len = 0;
    if (!s->selection || (!s->selection->utf8 && !s->selection->text)) { clip_result(s, false, 1); return PLAT_OK; }
    int fds[2]; /* pipe's read/write endpoints, derived from POSIX pipe ABI */
    if (pipe2(fds, O_CLOEXEC) < 0) return PLAT_ERR_FAIL;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) { close(fds[0]); close(fds[1]); return PLAT_ERR_FAIL; }
    wl_data_offer_receive(s->selection->proxy, s->selection->utf8 ? "text/plain;charset=utf-8" : "text/plain", fds[1]);
    close(fds[1]); s->rx_fd = fds[0]; return PLAT_OK;
}
const uint8_t *wayland_plat_clip_data(const plat *p, size_t *len)
{ wayland_state *s = W(p); if (len) *len = s ? s->received_len : 0; return s ? s->received : NULL; }
static void clip_receive(wayland_state *s)
{
    /* One system page per pump: bounded by the actual OS page size. */
    long page = sysconf(_SC_PAGESIZE); size_t chunk = page > 0 ? (size_t)page : 1;
    if (s->incoming_len == WL_CLIP_MAX) {
        uint8_t byte; ssize_t n = read(s->rx_fd, &byte, sizeof byte);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        close(s->rx_fd); s->rx_fd = -1;
        if (n == 0) receive_complete(s); else { s->incoming_len = 0; clip_result(s, false, 1); }
        return;
    }
    size_t available = WL_CLIP_MAX - s->incoming_len; if (chunk > available) chunk = available;
    if (s->incoming_cap - s->incoming_len < chunk) {
        size_t need = s->incoming_len + chunk;
        size_t cap = s->incoming_cap ? s->incoming_cap : chunk;
        while (cap < need) cap = cap > WL_CLIP_MAX / 2u ? WL_CLIP_MAX : cap * 2u;
        uint8_t *copy = realloc(s->incoming, cap);
        if (!copy) { close(s->rx_fd); s->rx_fd = -1; s->incoming_len = 0; clip_result(s, false, 1); return; }
        s->incoming = copy; s->incoming_cap = cap;
    }
    ssize_t n = read(s->rx_fd, s->incoming + s->incoming_len, chunk);
    if (n > 0) s->incoming_len += (size_t)n;
    else if (!n || (errno != EAGAIN && errno != EINTR)) {
        close(s->rx_fd); s->rx_fd = -1;
        if (n == 0) receive_complete(s); else { s->incoming_len = 0; clip_result(s, false, 1); }
    }
}
static void clip_send(wayland_state *s, wayland_tx *tx)
{
    if (tx->offset == tx->blob->len) { tx_close(s, tx); return; }
    /* Socket peers may close the pipe. Block SIGPIPE just for this write and
     * consume only a signal generated here; preserve the process disposition. */
    sigset_t set, old, pending; sigemptyset(&set); sigaddset(&set, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &set, &old); (void)sigpending(&pending);
    ssize_t n = write(tx->fd, tx->blob->bytes + tx->offset, tx->blob->len - tx->offset);
    int saved = errno;
    if (n < 0 && saved == EPIPE && !sigismember(&pending, SIGPIPE)) {
        struct timespec zero = {0}; (void)sigtimedwait(&set, NULL, &zero);
    }
    (void)pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (n > 0) tx->offset += (size_t)n;
    else if (n < 0 && saved != EAGAIN && saved != EINTR) tx_close(s, tx);
}
static void deliver(wayland_state *s, const plat_callbacks *cb)
{
    plat_event ev;
    while (!s->p->quit && wayland_plat_poll_event(s->p, &ev)) if (cb->on_event) cb->on_event(cb->ud, &ev);
    if (s->notify && !s->p->quit) {
        s->notify = false;
        if (cb->on_present_complete) cb->on_present_complete(cb->ud, s->frame_id, s->presented_ns, s->msc);
    }
}
int wayland_plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms)
{
    wayland_state *s = W(p); if (!s || !cb) return PLAT_ERR_FAIL;
    uint64_t end = trace_now_ns() + (timeout_ms > 0 ? (uint64_t)timeout_ms * NS_MS : 0);
    bool polled = false;
    while (!p->quit) {
        if (wl_display_dispatch_pending(D(p)) < 0 || s->failed || wl_display_get_error(D(p))) return PLAT_ERR_FAIL;
        if (s->local_receive) { s->local_receive = false; receive_complete(s); }
        deliver(s, cb);
        if (p->quit) break;
        if (timeout_ms >= 0 && polled && trace_now_ns() >= end) break;
        while (wl_display_prepare_read(D(p)) != 0) {
            if (wl_display_dispatch_pending(D(p)) < 0) return PLAT_ERR_FAIL;
        }
        if (s->count || s->notify) { wl_display_cancel_read(D(p)); continue; }
        int flushed = wl_display_flush(D(p));
        if (flushed < 0 && errno != EAGAIN) { wl_display_cancel_read(D(p)); return PLAT_ERR_FAIL; }
        struct pollfd fds[5u + WL_CLIP_SENDS] = {{wl_display_get_fd(D(p)), (short)(POLLIN | (flushed < 0 ? POLLOUT : 0)), 0},
            {p->timer_fd, POLLIN, 0}, {p->repeat_fd, POLLIN, 0}, {p->work_fd, POLLIN, 0},
            {s->rx_fd, POLLIN, 0}};
        for (size_t i = 0; i < WL_CLIP_SENDS; i++) fds[5u + i] = (struct pollfd){s->tx[i].fd, POLLOUT, 0};
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            uint64_t now = trace_now_ns(), left = end > now ? end - now : 0;
            uint64_t ms = left / NS_MS + (left % NS_MS != 0); wait_ms = ms > INT_MAX ? INT_MAX : (int)ms;
        }
        int rc = poll(fds, sizeof fds / sizeof fds[0], wait_ms); polled = true;
        if (rc < 0) { wl_display_cancel_read(D(p)); if (errno == EINTR) continue; return PLAT_ERR_FAIL; }
        if (fds[0].revents & POLLIN) { if (wl_display_read_events(D(p)) < 0) return PLAT_ERR_FAIL; }
        else wl_display_cancel_read(D(p));
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return PLAT_ERR_FAIL;
        if (rc > 0) p->iterations++;
        /* Native release/modifier events precede a repeat arriving together. */
        if (wl_display_dispatch_pending(D(p)) < 0 || s->failed) return PLAT_ERR_FAIL;
        /* A key already read from the wire owns the earlier position. A
         * worker/blink callback can end this pump, so deliver native input
         * before those callbacks can submit a frame containing older text. */
        deliver(s, cb); if (p->quit) break;
        if (fds[4].revents & (POLLIN | POLLHUP | POLLERR)) clip_receive(s);
        for (size_t i = 0; i < WL_CLIP_SENDS; i++)
            if (fds[5u + i].revents & (POLLOUT | POLLHUP | POLLERR)) clip_send(s, &s->tx[i]);
        uint64_t ticks;
        if ((fds[2].revents & POLLIN) && read(p->repeat_fd, &ticks, sizeof ticks) > 0 && s->repeat_key)
            wayland_input_key(s->p, s->repeat_key - 8u, 0, true, true); /* coalesce missed repeats, never burst */
        deliver(s, cb); if (p->quit) break;
        if ((fds[1].revents & POLLIN) && read(p->timer_fd, &ticks, sizeof ticks) > 0 && p->focused && cb->on_blink) cb->on_blink(cb->ud);
        if (!p->quit && (fds[3].revents & POLLIN) && cb->on_work) cb->on_work(cb->ud);
    }
    return PLAT_OK;
}
int wayland_plat_run(plat *p, const plat_callbacks *cb) { return wayland_plat_run_for(p, cb, -1); }
void wayland_plat_shutdown(plat *p)
{
    wayland_state *s = W(p);
    if (s) {
        wayland_frame_cancel(p);
        if (s->rx_fd >= 0) close(s->rx_fd);
        for (size_t i = 0; i < WL_CLIP_SENDS; i++) if (s->tx[i].fd >= 0) tx_close(s, &s->tx[i]);
        if (s->source) wl_data_source_destroy(s->source);
        while (s->offers) offer_destroy(s, s->offers);
        if (s->device) wl_data_device_destroy(s->device);
        if (s->manager) wl_data_device_manager_destroy(s->manager);
        if (s->keyboard) wl_keyboard_release(s->keyboard);
        if (s->pointer) wl_pointer_release(s->pointer);
        if (s->seat) wl_seat_destroy(s->seat);
        if (s->window) wl_egl_window_destroy(s->window);
        if (s->top) xdg_toplevel_destroy(s->top);
        if (s->xdg) xdg_surface_destroy(s->xdg);
        if (s->surface) wl_surface_destroy(s->surface);
        if (s->presentation) wp_presentation_destroy(s->presentation);
        if (s->shell) xdg_wm_base_destroy(s->shell);
        if (s->compositor) wl_compositor_destroy(s->compositor);
        if (s->registry) wl_registry_destroy(s->registry);
        xkb_compose_state_unref(s->compose); xkb_compose_table_unref(s->compose_table);
        xkb_state_unref(s->keys); xkb_keymap_unref(s->keymap); xkb_context_unref(s->context);
        blob_unref(s, s->owned); free(s->received); free(s->incoming); free(s);
    }
    if (p->timer_fd >= 0) close(p->timer_fd);
    if (p->repeat_fd >= 0) close(p->repeat_fd);
    if (p->conn) wl_display_disconnect(D(p));
    memset(p, 0, sizeof *p); p->backend = PLAT_BACKEND_WAYLAND;
    p->timer_fd = p->repeat_fd = p->work_fd = -1;
}
#else
bool wayland_input_key(plat *p, uint32_t key, uint32_t time, bool press, bool repeat)
{ (void)p; (void)key; (void)time; (void)press; (void)repeat; return false; }
int wayland_plat_init(plat *p, const plat_config *cfg)
{ (void)cfg; memset(p, 0, sizeof *p); p->backend = PLAT_BACKEND_WAYLAND; p->timer_fd = p->repeat_fd = p->work_fd = -1; return PLAT_ERR_NO_DISPLAY; }
void wayland_plat_shutdown(plat *p) { (void)p; }
void wayland_plat_map(plat *p) { (void)p; }
void wayland_plat_set_blink(plat *p, uint32_t ms) { (void)p; (void)ms; }
void wayland_plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz) { (void)p; (void)delay_ms; (void)rate_hz; }
int wayland_plat_set_present_events(plat *p, bool enabled) { (void)p; (void)enabled; return PLAT_ERR_FAIL; }
bool wayland_plat_poll_event(plat *p, plat_event *out) { (void)p; (void)out; return false; }
int wayland_plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms) { (void)p; (void)cb; (void)timeout_ms; return PLAT_ERR_NO_DISPLAY; }
int wayland_plat_run(plat *p, const plat_callbacks *cb) { return wayland_plat_run_for(p, cb, -1); }
int wayland_plat_clip_set(plat *p, int which, const void *utf8, size_t len) { (void)p; (void)which; (void)utf8; (void)len; return PLAT_ERR_FAIL; }
int wayland_plat_clip_request(plat *p, int which) { (void)p; (void)which; return PLAT_ERR_FAIL; }
const uint8_t *wayland_plat_clip_data(const plat *p, size_t *len) { (void)p; if (len) *len = 0; return NULL; }
void *wayland_egl_window(const plat *p) { (void)p; return NULL; }
void *wayland_surface(const plat *p) { (void)p; return NULL; }
int wayland_frame_arm(plat *p, uint32_t id) { (void)p; (void)id; return PLAT_ERR_FAIL; }
void wayland_frame_cancel(plat *p) { (void)p; }
int wayland_frame_result(plat *p, uint32_t id, bool *ready, uint64_t *ns, uint64_t *msc)
{ (void)p; (void)id; (void)ready; (void)ns; (void)msc; return PLAT_ERR_FAIL; }
#endif
