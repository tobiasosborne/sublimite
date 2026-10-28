#ifndef EDIT_WAYLAND_MOCK_H
#define EDIT_WAYLAND_MOCK_H
#ifdef EDIT_HAVE_WAYLAND_SERVER
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include "wayland/protocol/xdg-shell-server.h"
#include "wayland/protocol/presentation-server.h"
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
};
static void mock_destroy(struct wl_client *client, struct wl_resource *resource)
{ (void)client; wl_resource_destroy(resource); }
static void mock_surface_attach(struct wl_client *client, struct wl_resource *r, struct wl_resource *buffer, int32_t x, int32_t y)
{ (void)client; (void)r; (void)buffer; (void)x; (void)y; }
static void mock_damage(struct wl_client *client, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)client; (void)r; (void)x; (void)y; (void)w; (void)h; }
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
        xdg_toplevel_send_configure(m->top, 2, 0, &states); /* minimum unit width plus one; zero height = client chooses */
        xdg_surface_send_configure(m->xdg, wl_display_next_serial(m->display));
        wl_keyboard_send_enter(m->keyboard, wl_display_next_serial(m->display), m->surface, &states);
        wl_array_release(&states);
        wl_keyboard_send_modifiers(m->keyboard, wl_display_next_serial(m->display), 0, 0, 0, 0);
        wl_keyboard_send_key(m->keyboard, wl_display_next_serial(m->display), 0, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_keyboard_send_key(m->keyboard, wl_display_next_serial(m->display), 0, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_pointer_send_enter(m->pointer, wl_display_next_serial(m->display), m->surface, wl_fixed_from_int(1), wl_fixed_from_int(1));
        wl_pointer_send_motion(m->pointer, 0, wl_fixed_from_int(1), wl_fixed_from_int(1));
        wl_pointer_send_button(m->pointer, wl_display_next_serial(m->display), 0, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
        wl_pointer_send_button(m->pointer, wl_display_next_serial(m->display), 0, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
        wl_pointer_send_axis(m->pointer, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(10));
        if (wl_resource_get_version(m->pointer) >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            wl_pointer_send_axis_value120(m->pointer, WL_POINTER_AXIS_VERTICAL_SCROLL, 120);
            wl_pointer_send_frame(m->pointer);
        }
        for (size_t i = 0; i < m->payload_len; i++) {
            wl_keyboard_send_key(m->keyboard, wl_display_next_serial(m->display), (uint32_t)i,
                m->payload[i], m->payload[i] & 1u ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_pointer_send_axis(m->pointer, (uint32_t)i, m->payload[i] & 1u ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL,
                (wl_fixed_t)((int)m->payload[i] - (int)UINT8_MAX / 2) * PLAT_WHEEL_UNIT);
        }
        if (m->payload_len) wl_pointer_send_frame(m->pointer);
        wl_pointer_send_leave(m->pointer, wl_display_next_serial(m->display), m->surface);
        return;
    }
    assert(m->acked);
    if (!m->frame && !m->feedback) {
        if (!m->holding) {
            m->holding = true;
            wl_keyboard_send_key(m->keyboard, wl_display_next_serial(m->display), 0, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        } else {
            m->holding = false;
            wl_keyboard_send_key(m->keyboard, wl_display_next_serial(m->display), 0, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
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
        wl_resource_destroy(m->feedback); m->feedback = NULL; m->discard = true;
    }
}
static void mock_transform(struct wl_client *client, struct wl_resource *r, int32_t transform)
{ (void)client; (void)r; (void)transform; }
static const struct wl_surface_interface mock_surface_ops = {.destroy = mock_destroy, .attach = mock_surface_attach,
    .damage = mock_damage, .frame = mock_frame, .set_opaque_region = mock_region_set, .set_input_region = mock_region_set,
    .commit = mock_commit, .set_buffer_transform = mock_transform, .set_buffer_scale = mock_transform, .damage_buffer = mock_damage};
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
{ assert(version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION); struct wl_resource *r = wl_resource_create(client, &wl_seat_interface, (int)version, id); wl_resource_set_implementation(r, &mock_seat_ops, data, NULL); wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER); }
static void mock_bind_presentation(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{ struct wl_resource *r = wl_resource_create(client, &wp_presentation_interface, (int)version, id); wl_resource_set_implementation(r, &mock_presentation_ops, data, NULL); wp_presentation_send_clock_id(r, (uint32_t)CLOCK_MONOTONIC); }
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
        assert(wl_global_create(m.display, &wl_data_device_manager_interface, 1, &m, mock_bind_manager));
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
