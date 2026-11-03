/* edit-b6ai.8: raising the window for a second launch's handoff on X11.
 * A second client on a private Xvfb plays the window manager: it selects
 * SubstructureRedirect on the root (so it receives EWMH requests sent with
 * SubstructureRedirect|SubstructureNotify), maps windows on MapRequest and
 * records every _NET_ACTIVE_WINDOW client message.
 * (P) Wire values come from EWMH 1.5 _NET_ACTIVE_WINDOW: data32[0] source
 * indication (1 = application), data32[1] timestamp; startup-notification
 * 0.1: the id ends in _TIME<timestamp>. Fixture timestamps are arbitrary
 * nonzero 32-bit values. (E) Deadlines reuse x11_dnd_test's peer-timeout
 * scale: no performance gate, a miss is a hang guard only. */
#include "x11/plat.h"
#include "x11_xvfb.h"
#include "ipc_isolation.h"
#include "editor/editor.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <poll.h>
#include <sys/wait.h>
#include <xcb/xcb.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "x11_activate_test: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(EXIT_FAILURE); } } while (0)
enum { DEADLINE_MS = 10000, QUIET_MS = 200, STEP_MS = 4 };
static uint64_t now_ms(void) { return trace_now_ns() / UINT64_C(1000000); } /* (P) ns/ms */

typedef struct wm {
    xcb_connection_t *c;
    xcb_window_t root, stamp;
    xcb_atom_t active, stamp_prop;
    unsigned messages;
    uint32_t window, source, time, last_stamp;
} wm;
static xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE; free(r); T(a != XCB_ATOM_NONE); return a;
}
static void wm_open(wm *w)
{
    memset(w, 0, sizeof *w); w->c = xcb_connect(NULL, NULL); T(!xcb_connection_has_error(w->c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(w->c)).data; w->root = screen->root;
    uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    /* Only one client may redirect: success proves no other manager runs here. */
    xcb_generic_error_t *error = xcb_request_check(w->c, xcb_change_window_attributes_checked(w->c, w->root, XCB_CW_EVENT_MASK, &mask));
    T(error == NULL);
    w->stamp = xcb_generate_id(w->c); uint32_t own = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(w->c, XCB_COPY_FROM_PARENT, w->stamp, w->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &own);
    w->active = atom(w->c, "_NET_ACTIVE_WINDOW"); w->stamp_prop = atom(w->c, "X11_ACTIVATE_TEST_STAMP");
    xcb_flush(w->c);
}
static void wm_event(wm *w, xcb_generic_event_t *e)
{
    uint8_t t = e->response_type & 0x7f;
    if (t == XCB_MAP_REQUEST) { xcb_map_window(w->c, ((xcb_map_request_event_t *)e)->window); xcb_flush(w->c); }
    if (t == XCB_CONFIGURE_REQUEST) {
        const xcb_configure_request_event_t *r = (const xcb_configure_request_event_t *)e;
        uint32_t v[2] = {r->width, r->height};
        xcb_configure_window(w->c, r->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, v); xcb_flush(w->c);
    }
    if (t == XCB_PROPERTY_NOTIFY) {
        const xcb_property_notify_event_t *n = (const xcb_property_notify_event_t *)e;
        if (n->window == w->stamp && n->atom == w->stamp_prop) w->last_stamp = n->time;
    }
    if (t == XCB_CLIENT_MESSAGE) {
        const xcb_client_message_event_t *m = (const xcb_client_message_event_t *)e;
        if (m->type == w->active && m->format == 32) {
            w->messages++; w->window = m->window; w->source = m->data.data32[0]; w->time = m->data.data32[1];
        }
    }
}
static void wm_pump(wm *w)
{
    xcb_generic_event_t *e;
    while ((e = xcb_poll_for_event(w->c))) { wm_event(w, e); free(e); }
    T(!xcb_connection_has_error(w->c));
}
/* The manager's own fresh server time, by the same zero-length append. */
static uint32_t wm_server_time(wm *w)
{
    w->last_stamp = 0;
    xcb_change_property(w->c, XCB_PROP_MODE_APPEND, w->stamp, w->stamp_prop, XCB_ATOM_STRING, 8, 0, NULL); xcb_flush(w->c);
    uint64_t deadline = now_ms() + DEADLINE_MS;
    while (!w->last_stamp) {
        xcb_generic_event_t *e = xcb_wait_for_event(w->c); T(e != NULL);
        wm_event(w, e); free(e); T(now_ms() < deadline);
    }
    return w->last_stamp;
}
static const plat_callbacks quiet = {0};
static void wait_messages(wm *w, plat *p, editor *e, unsigned want)
{
    uint64_t deadline = now_ms() + DEADLINE_MS;
    while (w->messages < want) {
        if (p) { p->quit = false; (void)plat_run_for(p, &quiet, STEP_MS); }
        if (e) T(editor_step(e, STEP_MS) >= 0);
        if (!p && !e) (void)poll(NULL, 0, STEP_MS);
        wm_pump(w); T(now_ms() < deadline);
    }
    /* No duplicate follows: one request, one message. */
    uint64_t quiet_until = now_ms() + QUIET_MS;
    while (now_ms() < quiet_until) {
        if (p) { p->quit = false; (void)plat_run_for(p, &quiet, STEP_MS); }
        if (e) T(editor_step(e, STEP_MS) >= 0);
        wm_pump(w);
    }
    T(w->messages == want);
}

static void platform_case(wm *w)
{
    plat p; plat_config cfg = {.width = 64, .height = 32, .title = "activate"};
    T(plat_init(&p, &cfg) == PLAT_OK); plat_map(&p);
    uint32_t window = plat_native_x11(&p).window;
    /* 1. Startup id carries the launcher's timestamp: sent as is, at once. */
    T(plat_activate(&p, NULL, "sublimite-4242-host_TIME98765") == PLAT_OK);
    wait_messages(w, NULL, NULL, 1);
    T(w->window == window && w->source == 1 && w->time == 98765u);
    /* 2. No usable timestamp (no startup id, a token only): a fresh server
     * time, obtained asynchronously through the event loop. */
    uint32_t before = wm_server_time(w);
    T(plat_activate(&p, "wayland-token-ignored-on-x11", NULL) == PLAT_OK);
    wait_messages(w, &p, NULL, 2);
    T(w->window == window && w->source == 1 && w->time != 0 && w->time >= before);
    /* 3. A malformed _TIME suffix is not a timestamp: fresh time again. */
    before = wm_server_time(w);
    T(plat_activate(&p, NULL, "host_TIME12x") == PLAT_OK);
    wait_messages(w, &p, NULL, 3);
    T(w->window == window && w->source == 1 && w->time >= before && w->time != 12u);
    plat_shutdown(&p);
    puts("x11_activate_test: _NET_ACTIVE_WINDOW with source 1: startup-id timestamp, fresh server time, malformed id");
}

static int settle(editor *e, wm *w)
{
    uint64_t deadline = now_ms() + DEADLINE_MS;
    do {
        int rc = editor_step(e, STEP_MS); T(rc == EDITOR_OK || rc == EDITOR_MORE); wm_pump(w);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    } while (now_ms() < deadline);
    T(false); return 1;
}
static void write_file(const char *path)
{ FILE *f = fopen(path, "w"); T(f != NULL); T(fputs("x\n", f) >= 0); T(fclose(f) == 0); }
/* End to end: the first launch's own open never raises; a second launch's
 * handoff through the IPC server raises exactly once, with its startup id. */
static void editor_case(wm *w)
{
    const char *runtime = test_ipc_isolate(); T(runtime != NULL);
    ipc_server server = {0}; T(ipc_server_init(&server, runtime) == IPC_OK);
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.cols = 40, .rows = 8, .server = &server};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e, w) == 0);
    T(e->has_platform);
    uint32_t window = plat_native_x11(&e->platform).window;
    char first[4096], second[4096];
    (void)snprintf(first, sizeof first, "%s/first.txt", runtime); write_file(first);
    (void)snprintf(second, sizeof second, "%s/second.txt", runtime); write_file(second);
    unsigned base = w->messages;
    ipc_request own = {.cwd = runtime, .count = 1}; own.paths[0] = (ipc_path){first, 1, 1};
    T(editor_open_request(e, &own, 0) == 0); T(settle(e, w) == 0);
    uint64_t quiet_until = now_ms() + QUIET_MS;
    while (now_ms() < quiet_until) { T(editor_step(e, STEP_MS) >= 0); wm_pump(w); }
    T(w->messages == base);
    size_t tabs = editor_get_stats(e).tabs;
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        ipc_request r = {.cwd = runtime, .count = 1, .activation_token = "e2e-token", .startup_id = "e2e_TIME4242"};
        r.paths[0] = (ipc_path){second, 1, 1};
        _exit(ipc_client_send(runtime, &r, DEADLINE_MS) == IPC_OK ? 0 : 3);
    }
    uint64_t deadline = now_ms() + DEADLINE_MS;
    while (editor_get_stats(e).tabs == tabs) { T(editor_step(e, STEP_MS) >= 0); wm_pump(w); T(now_ms() < deadline); }
    wait_messages(w, NULL, e, base + 1);
    T(w->window == window && w->source == 1 && w->time == 4242u);
    int status = 0; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    editor_close(e); ipc_server_fini(&server); unlink(first); unlink(second);
    puts("x11_activate_test: editor first open does not raise; IPC handoff raises once with its startup id");
}

int main(void)
{
    /* Always a private server: the shared :99 must keep no root redirect. */
    (void)unsetenv("EDIT_DISPLAY");
    if (!xvfb_start()) { fputs("x11_activate_test: FAIL (private Xvfb unavailable)\n", stderr); return EXIT_FAILURE; }
    trace_init(); T(trace_thread_register() >= 0);
    wm w; wm_open(&w);
    platform_case(&w);
    editor_case(&w);
    xcb_disconnect(w.c);
    puts("x11_activate_test: all passed");
    return 0;
}
