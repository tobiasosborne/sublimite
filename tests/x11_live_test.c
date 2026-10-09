/* x11_live_test.c - private-X assertions for startup failures, window/Present/idle,
 * input ordering, and clipboard round trips. A parent watchdog bounds the runner. */
#include "x11/plat.h"
#include "x11_xvfb.h"
#include "x11/clip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

/* Include the implementation only to inject real startup protocol errors. Both
 * checked and unchecked forms go to Xvfb, so the pre-fix code is exercised too. */
#include <xcb/present.h>
static bool startup_fault(const char *name) {
    const char *f = getenv("EDIT_X11_TEST_FAULT");
    return f && strcmp(f, name) == 0;
}
#define WRAP_COLORMAP(name) \
static xcb_void_cookie_t test_##name(xcb_connection_t *c, uint8_t alloc, xcb_colormap_t mid, \
                                    xcb_window_t win, xcb_visualid_t visual) { \
    return name(c, alloc, mid, win, startup_fault("colormap") ? UINT32_MAX : visual); \
}
WRAP_COLORMAP(xcb_create_colormap)
WRAP_COLORMAP(xcb_create_colormap_checked)
#define WRAP_WINDOW(name) \
static xcb_void_cookie_t test_##name(xcb_connection_t *c, uint8_t depth, xcb_window_t win, \
    xcb_window_t parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t border, \
    uint16_t cls, xcb_visualid_t visual, uint32_t mask, const void *values) { \
    return name(c, depth, win, parent, x, y, startup_fault("window") ? 0 : w, h, border, cls, visual, mask, values); \
}
WRAP_WINDOW(xcb_create_window)
WRAP_WINDOW(xcb_create_window_checked)
#define WRAP_PRESENT(name) \
static xcb_void_cookie_t test_##name(xcb_connection_t *c, xcb_present_event_t eid, xcb_window_t win, uint32_t mask) { \
    return name(c, eid, startup_fault("present") ? XCB_WINDOW_NONE : win, mask); \
}
WRAP_PRESENT(xcb_present_select_input)
WRAP_PRESENT(xcb_present_select_input_checked)
#define xcb_create_colormap test_xcb_create_colormap
#define xcb_create_colormap_checked test_xcb_create_colormap_checked
#define xcb_create_window test_xcb_create_window
#define xcb_create_window_checked test_xcb_create_window_checked
#define xcb_present_select_input test_xcb_present_select_input
#define xcb_present_select_input_checked test_xcb_present_select_input_checked
#include "../src/x11/x11.c"
#undef xcb_create_colormap
#undef xcb_create_colormap_checked
#undef xcb_create_window
#undef xcb_create_window_checked
#undef xcb_present_select_input
#undef xcb_present_select_input_checked

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail = 1; } } while (0)

typedef struct sink { int arrived, failed, lost; uint8_t which; } sink;
static void on_ev(void *ud, const plat_event *e) {
    sink *s = ud;
    if (e->kind != PLAT_EV_CLIPBOARD) return;
    s->which = e->clip_which;
    if (e->code == 0) s->arrived++; else if (e->code == 1) s->failed++; else s->lost++;
}

static int open_plat(plat *p) {
    plat_config cfg = { "x11 live", 100, 100, getenv("DISPLAY") == NULL, -1, 0 };
    int r = plat_init(p, &cfg);
    if (r == PLAT_OK) x11_clip_set_limits(p, 0, 0, UINT32_MAX);
    return r;
}

static void pump(plat *a, sink *sa, plat *b, sink *sb, int rounds) {
    plat_callbacks ca = { sa, on_ev, NULL, NULL, NULL }, cb = { sb, on_ev, NULL, NULL, NULL };
    for (int i = 0; i < rounds; i++) { plat_run_for(a, &ca, 10); plat_run_for(b, &cb, 10); }
}

static void roundtrip(plat *a, sink *sa, plat *b, sink *sb, int which, const char *text) {
    size_t n = strlen(text), got = 0;
    CHECK(plat_clip_set(a, which, text, n) == PLAT_OK, "set %d", which);
    /* set now queues ownership: establish server ordering before a different
     * connection requests it, then let the loop complete the owner check. */
    xcb_connection_t *c = a->conn;
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
    CHECK(r != NULL, "ownership request reached server");
    free(r);
    pump(a, sa, b, sb, 1);
    sb->arrived = sb->failed = 0;
    CHECK(plat_clip_request(b, which) == PLAT_OK, "request %d", which);
    for (int i = 0; i < 50 && !sb->arrived && !sb->failed; i++) pump(a, sa, b, sb, 1);
    CHECK(sb->arrived == 1, "arrived (selection %d)", which);
    const uint8_t *d = plat_clip_data(b, &got);
    CHECK(d && got == n && memcmp(d, text, n) == 0, "data matches (selection %d, got %zu bytes)", which, got);
}

#include "x11_test_main.c.in"

static void test_startup_errors(void) {
    (void)test_xcb_create_colormap; (void)test_xcb_create_colormap_checked;
    (void)test_xcb_create_window; (void)test_xcb_create_window_checked;
    (void)test_xcb_present_select_input; (void)test_xcb_present_select_input_checked;
    const uint32_t dims[][2] = { {0, 100}, {100, 0}, {65536, 100}, {100, 65536}, {UINT32_MAX, 100} };
    for (size_t i = 0; i < sizeof dims / sizeof dims[0]; i++) {
        plat p;
        plat_config cfg = { "invalid dimensions", dims[i][0], dims[i][1], false, -1, 0 };
        int rc = plat_init(&p, &cfg);
        CHECK(rc == PLAT_ERR_FAIL, "§22 invalid dimensions %u x %u returned %d", cfg.width, cfg.height, rc);
        if (rc == PLAT_OK) plat_shutdown(&p);
        CHECK(!p.conn && !p.in && !p.clip && !p.present_ok && p.timer_fd == -1 && p.repeat_fd == -1,
              "§22 invalid dimensions leave no resources");
    }
    const char *faults[] = { "colormap", "window", "present" };
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        plat p;
        plat_config cfg = { "injected protocol error", 100, 100, false, -1, 0 };
        setenv("EDIT_X11_TEST_FAULT", faults[i], 1);
        int rc = plat_init(&p, &cfg);
        unsetenv("EDIT_X11_TEST_FAULT");
        CHECK(rc == PLAT_ERR_FAIL, "§22 %s protocol error returned %d", faults[i], rc);
        if (rc == PLAT_OK) plat_shutdown(&p);
        CHECK(!p.conn && !p.in && !p.clip && !p.present_ok && p.timer_fd == -1 && p.repeat_fd == -1,
              "§22 %s failure unwinds resources", faults[i]);
    }
    puts(g_fail ? "§22 startup errors: FAILED" : "§22 startup errors: ok");
}

static int live_main(int argc, char **argv) {
    plat a, b;

    test_startup_errors();
    if (argc > 1 && strcmp(argv[1], "--init") == 0) return g_fail ? 1 : 0;
    int r = open_plat(&a);
    if (r == PLAT_ERR_NO_DISPLAY) { fprintf(stderr, "x11_live_test: private display connection failed\n"); return 1; }
    if (r != PLAT_OK) { fprintf(stderr, "plat_init failed (%d)\n", r); return 1; }
    test_window_contracts(&a);
    if (argc > 1 && strcmp(argv[1], "--contracts") == 0) { plat_shutdown(&a); return g_fail ? 1 : 0; }
    if (open_plat(&b) != PLAT_OK) { CHECK(false, "second connection"); plat_shutdown(&a); return 1; }
    printf("xi2 %s, core wheel fallback %d\n", a.xi ? "active" : "unavailable", a.core_wheel);
    sink sa, sb;
    memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_CLIPBOARD, "h\xc3\xa9llo clipboard \xe2\x82\xac");
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_PRIMARY, "primary text");
    /* large (1 MiB) transfer within one request */
    size_t big = 1u << 20;
    char *bb = malloc(big);
    memset(bb, 'x', big);
    bb[big - 1] = 0;
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_CLIPBOARD, bb);
    free(bb);
    /* ownership moves: b takes CLIPBOARD, a is told */
    sa.lost = 0;
    CHECK(plat_clip_set(&b, PLAT_CLIP_CLIPBOARD, "b owns", 6) == PLAT_OK, "b set");
    for (int i = 0; i < 50 && !sa.lost; i++) pump(&a, &sa, &b, &sb, 1);
    CHECK(sa.lost == 1, "a told it lost the selection");
    /* we own it: request is answered locally */
    sb.arrived = 0;
    plat_clip_request(&b, PLAT_CLIP_CLIPBOARD);
    pump(&a, &sa, &b, &sb, 1);
    size_t got = 0;
    const uint8_t *d = plat_clip_data(&b, &got);
    CHECK(sb.arrived == 1 && d && got == 6 && memcmp(d, "b owns", 6) == 0, "self request");
    /* Pending owner replies can be superseded. A timestamp older than b's
     * ownership must retry with CurrentTime; a local paste waits for confirmation. */
    a.last_time = 1;
    sa.arrived = sa.failed = 0;
    CHECK(plat_clip_set(&a, PLAT_CLIP_CLIPBOARD, "superseded", 10) == PLAT_OK, "pending set");
    CHECK(plat_clip_set(&a, PLAT_CLIP_CLIPBOARD, "latest", 6) == PLAT_OK, "replacement set");
    CHECK(plat_clip_request(&a, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "pending local request");
    CHECK(plat_clip_request(&a, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "second pending local request");
    for (int i = 0; i < 50 && sa.arrived < 2 && !sa.failed; i++) pump(&a, &sa, &b, &sb, 1);
    d = plat_clip_data(&a, &got);
    CHECK(sa.arrived == 2 && !sa.failed && d && got == 6 && memcmp(d, "latest", 6) == 0,
          "timestamp retry and replacement local data");
    sb.arrived = sb.failed = 0;
    CHECK(plat_clip_request(&b, PLAT_CLIP_CLIPBOARD) == PLAT_OK, "replacement peer request");
    for (int i = 0; i < 50 && !sb.arrived && !sb.failed; i++) pump(&a, &sa, &b, &sb, 1);
    d = plat_clip_data(&b, &got);
    CHECK(sb.arrived == 1 && !sb.failed && d && got == 6 && memcmp(d, "latest", 6) == 0,
          "replacement peer data");
    plat_shutdown(&a);
    plat_shutdown(&b);
    if (g_fail) { puts("x11_live_test: FAILED"); return 1; }
    puts("x11_live_test: ok");
    return 0;
}

/* Independent parent watchdog covers startup, callback delivery, and idle poll. */
int main(int argc, char **argv) {
    if (!xvfb_start()) { fprintf(stderr, "x11_live_test: private Xvfb unavailable\n"); return 1; }
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) {
        int rc = live_main(argc, argv);
        fflush(NULL);
        _exit(rc); /* only the parent owns Xvfb's atexit cleanup */
    }
    uint64_t end = trace_now_ns() + UINT64_C(30000000000);
    int status = 0;
    while (trace_now_ns() < end) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        if (done < 0) break;
        struct timespec pause = { 0, 10000000 }; nanosleep(&pause, NULL);
    }
    kill(child, SIGKILL); waitpid(child, &status, 0);
    fprintf(stderr, "§25 FAIL independent watchdog expired\n");
    return 1;
}
