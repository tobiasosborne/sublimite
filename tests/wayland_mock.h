#ifndef EDIT_WAYLAND_MOCK_H
#define EDIT_WAYLAND_MOCK_H
#ifdef EDIT_HAVE_WAYLAND_SERVER
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include "wayland/protocol/xdg-shell-server.h"
#include "wayland/protocol/presentation-server.h"
#include "wayland/protocol/viewporter-server.h"
#include "wayland/protocol/fractional-scale-v1-server.h"
#include "wayland/protocol/xdg-activation-v1-server.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>
#include <errno.h>

/* The sandbox denies SO_PEERCRED even for our private socketpair. The mock
 * has no authentication semantics: its only peer is the forked test process.
 * Supply that known identity to libwayland-server, leaving every protocol
 * read/write/FD transfer real. No production override is linked. */
int getsockopt(int fd, int level, int option, void *value, socklen_t *len)
{
    if (level == SOL_SOCKET && option == SO_PEERCRED && value && len && *len >= sizeof(struct ucred)) {
        struct ucred cred = {.pid = getppid(), .uid = getuid(), .gid = getgid()};
        memcpy(value, &cred, sizeof cred); *len = sizeof cred; return 0;
    }
    return (int)syscall(SYS_getsockopt, fd, level, option, value, len);
}


/* Protocol literals derive from the core and stable XML. Test input uses
 * Linux KEY_A / BTN_LEFT. Sizes derive from literal lengths, not budgets. */
typedef struct mock_wayland mock_wayland;
/* edit-b6ai.6: wl_shm raster fixtures. Options are set by the test before
 * mock_start_options forks (the child inherits them, as mock_wide_output).
 * The screen is the compositor's damage-driven copy of the surface: each
 * commit copies only the damaged rectangles of the attached buffer into it,
 * then asserts the whole buffer equals the screen. A buffer stale outside its
 * damage, or damage missing a change, therefore fails in the child. The
 * parent reads the screen through a MAP_SHARED mapping created before fork.
 * (P) MOCK_SCREEN_PX bounds the largest fixture surface: the editor case's
 * 320 x 240 configure (tests/wayland_test.c); the raster fixture reserves
 * 64 x 128 and the backend clamps every buffer to its reserve. */
#define MOCK_SCREEN_PX (320u * 240u)
#define MOCK_DAMAGE_CAP 64u /* (E) fixture rect list; overflow damages the whole buffer */
#define MOCK_BUFFER_CAP 8u  /* (P) distinct buffers: two slots x (initial + resized) x two */
/* edit-b6ai.7: fractional fields. viewport_* is the committed wp_viewport
 * destination (zero = never set), viewports/fractions count the add-on
 * objects created. The parent requests runtime changes through request_*:
 * the child reads them at each commit after the first, sends a
 * preferred_scale (request_scale, when it differs from the last one sent) or
 * a toplevel configure of logical size request_w x request_h (then zeroes
 * them), and counts scales_sent. */
typedef struct mock_shm_shared {
    uint32_t commits, distinct, width, height, scale, damage_rects, releases, held_destroyed;
    uint32_t viewport_w, viewport_h, viewports, fractions, scales_sent;
    uint32_t request_scale; int32_t request_w, request_h;
    /* edit-b6ai.8 xdg_activation_v1: input_serial is the last key/button/
     * keyboard-enter serial sent; tokens counts committed token requests,
     * token_serial their set_serial (zero = none) and *_ok whether the seat
     * and surface were the client's; activations counts activate requests,
     * activated holds the last token, activate_surface_ok its surface check. */
    uint32_t input_serial, tokens, token_serial, activations;
    bool token_seat_ok, token_surface_ok, activate_surface_ok;
    char activated[64]; /* (P) longer than both fixture tokens; overflow truncates and fails the compare */
    uint32_t screen[MOCK_SCREEN_PX];
} mock_shm_shared;
typedef struct mock_shm_options {
    bool enabled, present_always, release_immediately, configure_set;
    int32_t configure_w, configure_h;      /* logical toplevel size of the first configure */
    uint32_t resize_after;                 /* buffer commits before a second configure; 0 = none */
    int32_t resize_w, resize_h;            /* logical size of that configure */
    /* edit-b6ai.7: advertise wp_viewporter / wp_fractional_scale_manager_v1;
     * preferred_scale (120ths, 0 = none) is sent as soon as the client
     * creates its wp_fractional_scale_v1. scale_after: buffer commits before
     * a second preferred_scale of scale_to (0 = none). pointer_set replaces
     * the startup pointer position (surface-local, wl_fixed). */
    bool viewporter, fractional, pointer_set;
    bool activation; /* edit-b6ai.8: advertise xdg_activation_v1 */
    uint32_t preferred_scale, scale_after, scale_to;
    wl_fixed_t pointer_x, pointer_y;
    mock_shm_shared *shared;
} mock_shm_options;
static mock_shm_options mock_shm;
typedef struct mock_buffer_seen { struct wl_listener gone; struct wl_resource *buffer; mock_wayland *mock; } mock_buffer_seen;
typedef struct mock_tx {
    mock_wayland *mock; int fd; size_t len;
    uint8_t bytes[sizeof "owned clipboard" - 1]; struct wl_event_source *event;
} mock_tx;
struct mock_wayland {
    struct wl_display *display;
    struct wl_resource *surface, *xdg, *top, *keyboard, *pointer, *frame, *feedback;
    bool configured, acked, discard, defer, holding;
    int32_t output_scale; bool no_presentation;
    const uint8_t *payload; size_t payload_len;
    mock_tx tx[2]; /* parallel requests for the two advertised UTF-8 MIME names */
    size_t tx_done; unsigned rx_requests; struct wl_resource *source;
    /* wl_shm surface state (edit-b6ai.6) */
    struct wl_resource *pending_buffer, *held;
    bool attached, damage_full;
    int32_t scale, pending_scale;
    int32_t damage[MOCK_DAMAGE_CAP][4]; size_t damage_count;
    mock_buffer_seen seen[MOCK_BUFFER_CAP];
    /* edit-b6ai.7 add-ons: wp_viewport double-buffered destination */
    struct wl_resource *viewport, *fraction;
    int32_t dest_w, dest_h; bool dest_pending; uint32_t sent_scale;
    struct wl_resource *seat; /* edit-b6ai.8: compared by set_serial */
    uint32_t tokens_issued;
};
/* edit-b6ai.8: an input-event serial, recorded for the activation fixture. */
static uint32_t mock_input(mock_wayland *m)
{
    uint32_t serial = wl_display_next_serial(m->display);
    if (mock_shm.shared) mock_shm.shared->input_serial = serial;
    return serial;
}
static void mock_destroy(struct wl_client *client, struct wl_resource *resource)
{ (void)client; wl_resource_destroy(resource); }
static void mock_buffer_gone(struct wl_listener *listener, void *data)
{
    mock_buffer_seen *seen = wl_container_of(listener, seen, gone); (void)data;
    /* Legal at teardown, a defect while the backend runs: tests assert the
     * count before shutting the backend down (wl_buffer.destroy while held). */
    if (seen->mock->held == seen->buffer) { seen->mock->held = NULL; if (mock_shm.shared) mock_shm.shared->held_destroyed++; }
    if (seen->mock->pending_buffer == seen->buffer) seen->mock->pending_buffer = NULL;
    seen->buffer = NULL;
}
static void mock_surface_attach(struct wl_client *client, struct wl_resource *r, struct wl_resource *buffer, int32_t x, int32_t y)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    assert(!x && !y); m->pending_buffer = buffer; m->attached = true;
}
static void mock_damage(struct wl_client *client, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)client; (void)x; (void)y; (void)w; (void)h; ((mock_wayland *)wl_resource_get_user_data(r))->damage_full = true; }
static void mock_damage_buffer(struct wl_client *client, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    assert(w > 0 && h > 0);
    if (m->damage_count == MOCK_DAMAGE_CAP) { m->damage_full = true; return; }
    int32_t *d = m->damage[m->damage_count++]; d[0] = x; d[1] = y; d[2] = w; d[3] = h;
}
static void mock_buffer_scale(struct wl_client *client, struct wl_resource *r, int32_t scale)
{ (void)client; assert(scale > 0); ((mock_wayland *)wl_resource_get_user_data(r))->pending_scale = scale; }
static void mock_configure_toplevel(mock_wayland *m, int32_t width, int32_t height)
{
    struct wl_array states; wl_array_init(&states);
    xdg_toplevel_send_configure(m->top, width, height, &states);
    xdg_surface_send_configure(m->xdg, wl_display_next_serial(m->display));
    wl_array_release(&states);
}
/* Commit of an attached shm buffer: damage-driven screen copy, full-buffer
 * equality, release policy, distinct-buffer count and the resize trigger. */
static void mock_commit_buffer(mock_wayland *m)
{
    struct wl_resource *br = m->pending_buffer;
    mock_shm_shared *out = mock_shm.shared;
    if (m->pending_scale) m->scale = m->pending_scale;
    int32_t scale = m->scale ? m->scale : 1;
    if (!br) return;
    assert(out && mock_shm.enabled);
    assert(br != m->held); /* never rewritten or re-attached before release */
    struct wl_shm_buffer *sb = wl_shm_buffer_get(br); assert(sb);
    int32_t w = wl_shm_buffer_get_width(sb), h = wl_shm_buffer_get_height(sb), stride = wl_shm_buffer_get_stride(sb);
    uint32_t format = wl_shm_buffer_get_format(sb);
    assert(format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888);
    assert(w > 0 && h > 0 && w % scale == 0 && h % scale == 0); /* wl_surface invalid_size */
    assert((uint64_t)w * (uint64_t)h <= MOCK_SCREEN_PX && stride >= w * 4);
    bool full = m->damage_full || (uint32_t)w != out->width || (uint32_t)h != out->height;
    out->width = (uint32_t)w; out->height = (uint32_t)h; out->scale = (uint32_t)scale;
    wl_shm_buffer_begin_access(sb);
    const uint8_t *data = wl_shm_buffer_get_data(sb);
    size_t rects = full ? 1u : m->damage_count;
    for (size_t i = 0; i < rects; i++) {
        int32_t x0 = full ? 0 : m->damage[i][0], y0 = full ? 0 : m->damage[i][1];
        int32_t x1 = full ? w : x0 + m->damage[i][2], y1 = full ? h : y0 + m->damage[i][3];
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
        for (int32_t y = y0; y < y1 && x0 < x1; y++)
            memcpy(out->screen + (size_t)y * (size_t)w + (size_t)x0, data + (size_t)y * (size_t)stride + (size_t)x0 * 4u, (size_t)(x1 - x0) * 4u);
    }
    for (int32_t y = 0; y < h; y++)
        assert(!memcmp(out->screen + (size_t)y * (size_t)w, data + (size_t)y * (size_t)stride, (size_t)w * 4u));
    wl_shm_buffer_end_access(sb);
    out->damage_rects += (uint32_t)rects; out->commits++;
    bool known = false;
    for (size_t i = 0; i < MOCK_BUFFER_CAP; i++) if (m->seen[i].buffer == br) known = true;
    for (size_t i = 0; !known && i < MOCK_BUFFER_CAP; i++) if (!m->seen[i].buffer) {
        m->seen[i] = (mock_buffer_seen){.gone = {.notify = mock_buffer_gone}, .buffer = br, .mock = m};
        wl_resource_add_destroy_listener(br, &m->seen[i].gone); out->distinct++; known = true;
    }
    assert(known);
    if (mock_shm.release_immediately) { wl_buffer_send_release(br); out->releases++; }
    else { if (m->held) { wl_buffer_send_release(m->held); out->releases++; } m->held = br; }
    if (mock_shm.resize_after && out->commits == mock_shm.resize_after)
        mock_configure_toplevel(m, mock_shm.resize_w, mock_shm.resize_h);
    if (mock_shm.scale_after && out->commits == mock_shm.scale_after && m->fraction) {
        wp_fractional_scale_v1_send_preferred_scale(m->fraction, mock_shm.scale_to);
        m->sent_scale = mock_shm.scale_to; out->scales_sent++;
    }
}
/* edit-b6ai.7: double-buffered viewport state and the parent's requests. */
static void mock_commit_fractional(mock_wayland *m)
{
    mock_shm_shared *out = mock_shm.shared;
    if (m->dest_pending) {
        m->dest_pending = false;
        if (out) { out->viewport_w = m->dest_w > 0 ? (uint32_t)m->dest_w : 0; out->viewport_h = m->dest_h > 0 ? (uint32_t)m->dest_h : 0; }
    }
    /* The fractional protocol keeps the wl_surface buffer scale at one. */
    if (m->viewport && m->dest_w > 0) assert(m->scale <= 1);
    if (!out) return;
    uint32_t want = __atomic_load_n(&out->request_scale, __ATOMIC_ACQUIRE);
    if (want && want != m->sent_scale && m->fraction) {
        wp_fractional_scale_v1_send_preferred_scale(m->fraction, want);
        m->sent_scale = want; out->scales_sent++;
    }
    int32_t w = __atomic_load_n(&out->request_w, __ATOMIC_ACQUIRE), h = __atomic_load_n(&out->request_h, __ATOMIC_ACQUIRE);
    if (w > 0 && h > 0) {
        __atomic_store_n(&out->request_w, 0, __ATOMIC_RELEASE); __atomic_store_n(&out->request_h, 0, __ATOMIC_RELEASE);
        mock_configure_toplevel(m, w, h);
    }
}
static void mock_frame(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r); assert(!m->frame);
    m->frame = wl_resource_create(client, &wl_callback_interface, 1, id);
}
static void mock_region_set(struct wl_client *client, struct wl_resource *r, struct wl_resource *region)
{ (void)client; (void)r; (void)region; }
static void mock_commit(struct wl_client *client, struct wl_resource *r)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    if (!m->configured) {
        m->configured = true;
        struct wl_array states; wl_array_init(&states);
        if (mock_shm.configure_set) xdg_toplevel_send_configure(m->top, mock_shm.configure_w, mock_shm.configure_h, &states);
        else xdg_toplevel_send_configure(m->top, 2, 0, &states); /* minimum unit width plus one; zero height = client chooses */
        xdg_surface_send_configure(m->xdg, wl_display_next_serial(m->display));
        wl_keyboard_send_enter(m->keyboard, mock_input(m), m->surface, &states);
        wl_array_release(&states);
        wl_keyboard_send_modifiers(m->keyboard, wl_display_next_serial(m->display), 0, 0, 0, 0);
        wl_keyboard_send_key(m->keyboard, mock_input(m), 0, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_keyboard_send_key(m->keyboard, mock_input(m), 0, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_fixed_t px = mock_shm.pointer_set ? mock_shm.pointer_x : wl_fixed_from_int(1);
        wl_fixed_t py = mock_shm.pointer_set ? mock_shm.pointer_y : wl_fixed_from_int(1);
        wl_pointer_send_enter(m->pointer, wl_display_next_serial(m->display), m->surface, px, py);
        wl_pointer_send_motion(m->pointer, 0, px, py);
        wl_pointer_send_button(m->pointer, mock_input(m), 0, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
        wl_pointer_send_button(m->pointer, mock_input(m), 0, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
        wl_pointer_send_axis(m->pointer, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(10));
        if (wl_resource_get_version(m->pointer) >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            wl_pointer_send_axis_value120(m->pointer, WL_POINTER_AXIS_VERTICAL_SCROLL, 120);
            wl_pointer_send_frame(m->pointer);
        }
        for (size_t i = 0; i < m->payload_len; i++) {
            wl_keyboard_send_key(m->keyboard, mock_input(m), (uint32_t)i,
                m->payload[i], m->payload[i] & 1u ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_pointer_send_axis(m->pointer, (uint32_t)i, m->payload[i] & 1u ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL,
                (wl_fixed_t)((int)m->payload[i] - (int)UINT8_MAX / 2) * PLAT_WHEEL_UNIT);
        }
        if (m->payload_len) wl_pointer_send_frame(m->pointer);
        wl_pointer_send_leave(m->pointer, wl_display_next_serial(m->display), m->surface);
        return;
    }
    assert(m->acked);
    if (m->attached) mock_commit_buffer(m);
    else if (m->pending_scale) m->scale = m->pending_scale;
    mock_commit_fractional(m);
    m->attached = false; m->pending_buffer = NULL; m->damage_full = false; m->damage_count = 0; m->pending_scale = 0;
    if (!m->frame && !m->feedback) {
        if (!m->holding) {
            m->holding = true;
            wl_keyboard_send_key(m->keyboard, mock_input(m), 0, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        } else {
            m->holding = false;
            wl_keyboard_send_key(m->keyboard, mock_input(m), 0, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_keyboard_send_leave(m->keyboard, wl_display_next_serial(m->display), m->surface);
        }
        return;
    }
    if (m->frame) { wl_callback_send_done(m->frame, 0); wl_resource_destroy(m->frame); m->frame = NULL; }
    if (m->defer && m->feedback) { m->defer = false; return; }
    if (m->feedback) {
        if (m->discard) wp_presentation_feedback_send_discarded(m->feedback);
        else {
            struct timespec stamp; assert(!clock_gettime(CLOCK_MONOTONIC, &stamp));
            uint64_t seconds = (uint64_t)stamp.tv_sec;
            wp_presentation_feedback_send_presented(m->feedback, (uint32_t)(seconds >> 32), (uint32_t)seconds,
                (uint32_t)stamp.tv_nsec, 0, 0, 1, WP_PRESENTATION_FEEDBACK_KIND_VSYNC);
        }
        wl_resource_destroy(m->feedback); m->feedback = NULL; m->discard = !mock_shm.present_always;
    }
}
static void mock_transform(struct wl_client *client, struct wl_resource *r, int32_t transform)
{ (void)client; (void)r; (void)transform; }
static const struct wl_surface_interface mock_surface_ops = {.destroy = mock_destroy, .attach = mock_surface_attach,
    .damage = mock_damage, .frame = mock_frame, .set_opaque_region = mock_region_set, .set_input_region = mock_region_set,
    .commit = mock_commit, .set_buffer_transform = mock_transform, .set_buffer_scale = mock_buffer_scale, .damage_buffer = mock_damage_buffer};
static void mock_create_surface(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r);
    m->surface = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(r), id);
    wl_resource_set_implementation(m->surface, &mock_surface_ops, m, NULL);
}
static const struct wl_compositor_interface mock_compositor_ops = {.create_surface = mock_create_surface};
static void mock_title(struct wl_client *client, struct wl_resource *r, const char *title)
{ (void)client; (void)r; assert(title && *title); }
static const struct xdg_toplevel_interface mock_top_ops = {.destroy = mock_destroy, .set_title = mock_title, .set_app_id = mock_title};
static void mock_get_top(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r);
    m->top = wl_resource_create(client, &xdg_toplevel_interface, 1, id);
    wl_resource_set_implementation(m->top, &mock_top_ops, m, NULL);
}
static void mock_ack(struct wl_client *client, struct wl_resource *r, uint32_t serial)
{ (void)client; (void)serial; mock_wayland *m = wl_resource_get_user_data(r); m->acked = true; }
static const struct xdg_surface_interface mock_xdg_ops = {.destroy = mock_destroy, .get_toplevel = mock_get_top, .ack_configure = mock_ack};
static void mock_get_xdg(struct wl_client *client, struct wl_resource *r, uint32_t id, struct wl_resource *surface)
{
    mock_wayland *m = wl_resource_get_user_data(r); assert(surface == m->surface);
    m->xdg = wl_resource_create(client, &xdg_surface_interface, 1, id);
    wl_resource_set_implementation(m->xdg, &mock_xdg_ops, m, NULL);
}
static void mock_pong(struct wl_client *client, struct wl_resource *r, uint32_t serial) { (void)client; (void)r; (void)serial; }
static const struct xdg_wm_base_interface mock_shell_ops = {.destroy = mock_destroy, .get_xdg_surface = mock_get_xdg, .pong = mock_pong};
static const struct wl_keyboard_interface mock_keyboard_ops = {.release = mock_destroy};
static const struct wl_pointer_interface mock_pointer_ops = {.release = mock_destroy};
static void mock_keyboard(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r);
    m->keyboard = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(r), id);
    wl_resource_set_implementation(m->keyboard, &mock_keyboard_ops, m, NULL);
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS); assert(ctx);
    struct xkb_rule_names names = {.layout = "us"};
    struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS); assert(map);
    char *text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1); assert(text);
    size_t len = strlen(text) + 1;
    int fd = memfd_create("wayland mock keymap", MFD_CLOEXEC); assert(fd >= 0);
    assert(write(fd, text, len) == (ssize_t)len);
    wl_keyboard_send_keymap(m->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)len); close(fd);
    wl_keyboard_send_repeat_info(m->keyboard, 0, 0); /* disabled: protocol mode, not a duration */
    free(text); xkb_keymap_unref(map); xkb_context_unref(ctx);
}
static void mock_pointer(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r);
    m->pointer = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(r), id);
    wl_resource_set_implementation(m->pointer, &mock_pointer_ops, m, NULL);
}
static const struct wl_seat_interface mock_seat_ops = {.get_pointer = mock_pointer, .get_keyboard = mock_keyboard};
static void mock_feedback(struct wl_client *client, struct wl_resource *r, struct wl_resource *surface, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r); assert(surface == m->surface && !m->feedback);
    m->feedback = wl_resource_create(client, &wp_presentation_feedback_interface, 1, id);
}
static const struct wp_presentation_interface mock_presentation_ops = {.destroy = mock_destroy, .feedback = mock_feedback};
static void mock_offer_receive(struct wl_client *client, struct wl_resource *r, const char *mime, int32_t fd)
{
    (void)client; assert(!strcmp(mime, "text/plain;charset=utf-8")); mock_wayland *m = wl_resource_get_user_data(r);
    const char *text = m->rx_requests++ ? "replacement clipboard" : "mock clipboard";
    size_t len = strlen(text);
    if (m->payload_len) { text = (const char *)m->payload; len = m->payload_len; }
    assert(write(fd, text, len) == (ssize_t)len); close(fd);
}
static const struct wl_data_offer_interface mock_offer_ops = {.receive = mock_offer_receive, .destroy = mock_destroy};
static const struct wl_data_device_interface mock_device_ops;
static void mock_get_device(struct wl_client *client, struct wl_resource *r, uint32_t id, struct wl_resource *seat)
{
    (void)seat; mock_wayland *m = wl_resource_get_user_data(r);
    struct wl_resource *device = wl_resource_create(client, &wl_data_device_interface, 1, id);
    wl_resource_set_implementation(device, &mock_device_ops, m, NULL);
    struct wl_resource *offer = wl_resource_create(client, &wl_data_offer_interface, 1, 0);
    wl_resource_set_implementation(offer, &mock_offer_ops, m, NULL);
    wl_data_device_send_data_offer(device, offer); wl_data_offer_send_offer(offer, "text/plain;charset=utf-8");
    wl_data_device_send_selection(device, offer);
}
static void mock_source_offer(struct wl_client *client, struct wl_resource *r, const char *mime)
{ (void)client; (void)r; assert(!strcmp(mime, "text/plain") || !strcmp(mime, "text/plain;charset=utf-8")); }
static const struct wl_data_source_interface mock_source_ops = {.offer = mock_source_offer, .destroy = mock_destroy};
static void mock_source(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    mock_wayland *m = wl_resource_get_user_data(r);
    m->source = wl_resource_create(client, &wl_data_source_interface, 1, id);
    wl_resource_set_implementation(m->source, &mock_source_ops, m, NULL);
}
static int mock_tx_read(int fd, uint32_t mask, void *data)
{
    (void)mask; mock_tx *tx = data; mock_wayland *m = tx->mock;
    uint8_t byte; ssize_t n = read(fd, &byte, sizeof byte);
    if (n > 0) { assert(tx->len < sizeof tx->bytes); tx->bytes[tx->len++] = byte; }
    else if (!n) {
        assert(tx->len == sizeof tx->bytes && !memcmp(tx->bytes, "owned clipboard", sizeof tx->bytes));
        wl_event_source_remove(tx->event); tx->event = NULL; close(tx->fd); tx->fd = -1;
        if (++m->tx_done == sizeof m->tx / sizeof m->tx[0]) {
            wl_data_source_send_cancelled(m->source); xdg_toplevel_send_close(m->top);
        }
    }
    return 0;
}
static void mock_selection(struct wl_client *client, struct wl_resource *r, struct wl_resource *source, uint32_t serial)
{
    (void)client; assert(source && serial); mock_wayland *m = wl_resource_get_user_data(r);
    const char *mime[] = {"text/plain;charset=utf-8", "text/plain"};
    m->tx_done = 0;
    for (size_t i = 0; i < sizeof mime / sizeof mime[0]; i++) {
        int pipe_fds[2]; assert(!pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK));
        mock_tx *tx = &m->tx[i]; tx->mock = m; tx->fd = pipe_fds[0]; tx->len = 0;
        tx->event = wl_event_loop_add_fd(wl_display_get_event_loop(m->display), tx->fd, WL_EVENT_READABLE, mock_tx_read, tx); assert(tx->event);
        wl_data_source_send_send(source, mime[i], pipe_fds[1]); close(pipe_fds[1]);
    }
}
static const struct wl_data_device_interface mock_device_ops = {.set_selection = mock_selection};
static const struct wl_data_device_manager_interface mock_manager_ops = {.create_data_source = mock_source, .get_data_device = mock_get_device};
static void mock_bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wl_compositor_interface, (int)version, id); wl_resource_set_implementation(r, &mock_compositor_ops, data, NULL); }
static void mock_bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &xdg_wm_base_interface, (int)version, id); wl_resource_set_implementation(r, &mock_shell_ops, data, NULL); xdg_wm_base_send_ping(r, wl_display_next_serial(((mock_wayland *)data)->display)); }
static void mock_bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ assert(version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION); struct wl_resource *r = wl_resource_create(client, &wl_seat_interface, (int)version, id); wl_resource_set_implementation(r, &mock_seat_ops, data, NULL); ((mock_wayland *)data)->seat = r; wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER); }
static void mock_bind_presentation(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wp_presentation_interface, (int)version, id); wl_resource_set_implementation(r, &mock_presentation_ops, data, NULL); wp_presentation_send_clock_id(r, (uint32_t)CLOCK_MONOTONIC); }
/* edit-b6ai.7: wp_viewporter and wp_fractional_scale_manager_v1 */
static void mock_viewport_source(struct wl_client *client, struct wl_resource *r, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w, wl_fixed_t h)
{ (void)client; (void)r; (void)x; (void)y; (void)w; (void)h; assert(false); /* the client never crops */ }
static void mock_viewport_destination(struct wl_client *client, struct wl_resource *r, int32_t w, int32_t h)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    assert((w > 0 && h > 0) || (w == -1 && h == -1)); /* wp_viewport bad_value */
    m->dest_w = w; m->dest_h = h; m->dest_pending = true;
}
static const struct wp_viewport_interface mock_viewport_ops = {.destroy = mock_destroy,
    .set_source = mock_viewport_source, .set_destination = mock_viewport_destination};
static void mock_viewport_gone(struct wl_resource *r) { ((mock_wayland *)wl_resource_get_user_data(r))->viewport = NULL; }
static void mock_get_viewport(struct wl_client *client, struct wl_resource *r, uint32_t id, struct wl_resource *surface)
{
    mock_wayland *m = wl_resource_get_user_data(r); assert(surface == m->surface && !m->viewport);
    m->viewport = wl_resource_create(client, &wp_viewport_interface, 1, id);
    wl_resource_set_implementation(m->viewport, &mock_viewport_ops, m, mock_viewport_gone);
    if (mock_shm.shared) mock_shm.shared->viewports++;
}
static const struct wp_viewporter_interface mock_viewporter_ops = {.destroy = mock_destroy, .get_viewport = mock_get_viewport};
static const struct wp_fractional_scale_v1_interface mock_fraction_ops = {.destroy = mock_destroy};
static void mock_fraction_gone(struct wl_resource *r) { ((mock_wayland *)wl_resource_get_user_data(r))->fraction = NULL; }
static void mock_get_fraction(struct wl_client *client, struct wl_resource *r, uint32_t id, struct wl_resource *surface)
{
    mock_wayland *m = wl_resource_get_user_data(r); assert(surface == m->surface && !m->fraction);
    m->fraction = wl_resource_create(client, &wp_fractional_scale_v1_interface, 1, id);
    wl_resource_set_implementation(m->fraction, &mock_fraction_ops, m, mock_fraction_gone);
    if (mock_shm.shared) mock_shm.shared->fractions++;
    if (mock_shm.preferred_scale) {
        wp_fractional_scale_v1_send_preferred_scale(m->fraction, mock_shm.preferred_scale);
        m->sent_scale = mock_shm.preferred_scale; if (mock_shm.shared) mock_shm.shared->scales_sent++;
    }
}
static const struct wp_fractional_scale_manager_v1_interface mock_fraction_manager_ops = {.destroy = mock_destroy,
    .get_fractional_scale = mock_get_fraction};
static void mock_bind_viewporter(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wp_viewporter_interface, (int)version, id); wl_resource_set_implementation(r, &mock_viewporter_ops, data, NULL); }
static void mock_bind_fraction_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wp_fractional_scale_manager_v1_interface, (int)version, id); wl_resource_set_implementation(r, &mock_fraction_manager_ops, data, NULL); }
/* edit-b6ai.8: xdg_activation_v1. A committed token request is answered at
 * once with "mock-token-<n>"; activate records the token and its surface. */
static void mock_token_serial(struct wl_client *client, struct wl_resource *r, uint32_t serial, struct wl_resource *seat)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    if (mock_shm.shared) { mock_shm.shared->token_serial = serial; mock_shm.shared->token_seat_ok = seat == m->seat; }
}
static void mock_token_app_id(struct wl_client *client, struct wl_resource *r, const char *app_id)
{ (void)client; (void)r; (void)app_id; }
static void mock_token_surface(struct wl_client *client, struct wl_resource *r, struct wl_resource *surface)
{ (void)client; mock_wayland *m = wl_resource_get_user_data(r); if (mock_shm.shared) mock_shm.shared->token_surface_ok = surface == m->surface; }
static void mock_token_commit(struct wl_client *client, struct wl_resource *r)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    char token[32]; /* (P) "mock-token-" plus at most ten uint32_t digits and NUL */
    (void)snprintf(token, sizeof token, "mock-token-%u", ++m->tokens_issued);
    if (mock_shm.shared) mock_shm.shared->tokens++;
    xdg_activation_token_v1_send_done(r, token);
}
static const struct xdg_activation_token_v1_interface mock_token_ops = {.set_serial = mock_token_serial,
    .set_app_id = mock_token_app_id, .set_surface = mock_token_surface, .commit = mock_token_commit, .destroy = mock_destroy};
static void mock_get_token(struct wl_client *client, struct wl_resource *r, uint32_t id)
{
    struct wl_resource *token = wl_resource_create(client, &xdg_activation_token_v1_interface, 1, id);
    wl_resource_set_implementation(token, &mock_token_ops, wl_resource_get_user_data(r), NULL);
}
static void mock_activate(struct wl_client *client, struct wl_resource *r, const char *token, struct wl_resource *surface)
{
    (void)client; mock_wayland *m = wl_resource_get_user_data(r);
    if (!mock_shm.shared) return;
    mock_shm.shared->activations++; mock_shm.shared->activate_surface_ok = surface == m->surface;
    (void)snprintf(mock_shm.shared->activated, sizeof mock_shm.shared->activated, "%s", token);
}
static const struct xdg_activation_v1_interface mock_activation_ops = {.destroy = mock_destroy,
    .get_activation_token = mock_get_token, .activate = mock_activate};
static void mock_bind_activation(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &xdg_activation_v1_interface, (int)version, id); wl_resource_set_implementation(r, &mock_activation_ops, data, NULL); }
static void mock_bind_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wl_data_device_manager_interface, (int)version, id); wl_resource_set_implementation(r, &mock_manager_ops, data, NULL); }
/* (P) Unit logical output = minimum nonzero dimensions; scale is the
 * smallest integer above unity. Thus mode is scale * unit on both axes. */
static void mock_bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    mock_wayland *m = data;
    struct wl_resource *r = wl_resource_create(client, &wl_output_interface, (int)version, id);
    wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "mock", "scaled", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT, m->output_scale, m->output_scale, 0);
    wl_output_send_scale(r, m->output_scale); wl_output_send_done(r);
}
/* edit-b6ai.16: optional second output, unity scale, mode set by the test
 * before mock_start_options forks (the child inherits it). Zero = absent. */
static int32_t mock_wide_output_w, mock_wide_output_h;
static void mock_bind_wide_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *r = wl_resource_create(client, &wl_output_interface, (int)version, id);
    wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "mock", "wide", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT, mock_wide_output_w, mock_wide_output_h, 0);
    wl_output_send_scale(r, 1); wl_output_send_done(r);
}
static void mock_client_gone(struct wl_listener *listener, void *data)
{ (void)data; mock_wayland *m = *(mock_wayland **)(listener + 1); wl_display_terminate(m->display); }
static pid_t mock_start_options(bool shell, bool defer, const uint8_t *payload, size_t len, int32_t scale, bool no_presentation)
{
    int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(sockets[0]); mock_wayland m = {.payload = payload, .payload_len = len, .defer = defer, .output_scale = scale, .no_presentation = no_presentation}; m.display = wl_display_create(); assert(m.display);
        assert(wl_global_create(m.display, &wl_compositor_interface, 4, &m, mock_bind_compositor));
        if (shell) assert(wl_global_create(m.display, &xdg_wm_base_interface, 1, &m, mock_bind_shell));
        assert(wl_global_create(m.display, &wl_seat_interface, WL_POINTER_AXIS_VALUE120_SINCE_VERSION, &m, mock_bind_seat));
        if (!m.no_presentation) assert(wl_global_create(m.display, &wp_presentation_interface, 1, &m, mock_bind_presentation));
        if (m.output_scale) assert(wl_global_create(m.display, &wl_output_interface, WL_OUTPUT_SCALE_SINCE_VERSION, &m, mock_bind_output));
        if (mock_wide_output_w && mock_wide_output_h)
            assert(wl_global_create(m.display, &wl_output_interface, WL_OUTPUT_SCALE_SINCE_VERSION, &m, mock_bind_wide_output));
        assert(wl_global_create(m.display, &wl_data_device_manager_interface, 1, &m, mock_bind_manager));
        if (mock_shm.enabled) assert(!wl_display_init_shm(m.display)); /* libwayland-server's real wl_shm */
        if (mock_shm.viewporter) assert(wl_global_create(m.display, &wp_viewporter_interface, 1, &m, mock_bind_viewporter));
        if (mock_shm.fractional) assert(wl_global_create(m.display, &wp_fractional_scale_manager_v1_interface, 1, &m, mock_bind_fraction_manager));
        if (mock_shm.activation) assert(wl_global_create(m.display, &xdg_activation_v1_interface, 1, &m, mock_bind_activation)); /* (P) v1 */
        struct wl_client *client = wl_client_create(m.display, sockets[1]); if (!client) perror("mock wl_client_create"); assert(client);
        struct { struct wl_listener listener; mock_wayland *mock; } gone = {.listener = {.notify = mock_client_gone}, .mock = &m};
        wl_client_add_destroy_listener(client, &gone.listener);
        wl_display_run(m.display); wl_display_destroy_clients(m.display); wl_display_destroy(m.display); _exit(0);
    }
    close(sockets[1]); char number[sizeof(int) * CHAR_BIT + 1]; /* decimal digits cannot exceed bit count */
    assert(snprintf(number, sizeof number, "%d", sockets[0]) > 0); assert(!setenv("WAYLAND_SOCKET", number, 1));
    return child;
}
static inline pid_t mock_start_ex(bool shell, bool defer, const uint8_t *payload, size_t len)
{ return mock_start_options(shell, defer, payload, len, 0, false); }
static inline pid_t mock_start_payload(bool shell, const uint8_t *payload, size_t len) { return mock_start_ex(shell, false, payload, len); }
static inline pid_t mock_start(bool shell) { return mock_start_payload(shell, NULL, 0); }
static void mock_join(pid_t child)
{ int status; assert(waitpid(child, &status, 0) == child); assert(WIFEXITED(status) && WEXITSTATUS(status) == 0); }
#endif
#endif
