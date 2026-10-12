/* Runtime selection policy is a truth table over the environment, not a
 * timing/count budget. Headless init must not contact either display. */
#include "wayland/plat.h"
#include "wayland/private.h"
#include "base/base.h"
#include "editor/editor.h"
#include "../bench/harness.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef EDIT_HAVE_WAYLAND
/* Test-only connect seam: validate the directory-descriptor pathname without
 * a compositor or AF_UNIX bind permissions. All other connects stay real. */
int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    const char *expected = getenv("WAYLAND_TEST_EXPECT_DIRECTORY");
    if (expected && address && address->sa_family == AF_UNIX) {
        const struct sockaddr_un *unix_address = (const void *)address;
        assert(length == sizeof *unix_address);
        assert(!strncmp(unix_address->sun_path, "/proc/self/fd/", sizeof "/proc/self/fd/" - 1));
        char link[sizeof unix_address->sun_path];
        memcpy(link, unix_address->sun_path, sizeof link);
        char *slash = strrchr(link, '/'); assert(slash); *slash = '\0';
        char target[PATH_MAX]; /* (P) kernel maximum pathname ABI */
        ssize_t size = readlink(link, target, sizeof target - 1); assert(size >= 0);
        target[(size_t)size] = '\0'; assert(!strcmp(target, expected));
        assert(!setenv("WAYLAND_TEST_CONNECTED_DIRECTORY", "yes", 1));
        errno = ENOENT; return -1; /* missing socket after directory resolution */
    }
    return (int)syscall(SYS_connect, fd, address, length);
}
#endif
#ifdef EDIT_HAVE_WAYLAND_SERVER
#include <wayland-client.h>
#include <sys/eventfd.h>
#include <poll.h>
#include "wayland_mock.h"

typedef struct observed { bool focus, key, release, button, motion, wheel, clip, close, lost, repeat, blink, work, stop_key, priority_key; unsigned wheels; plat *platform; int work_fd; } observed;
static void observe(void *user, const plat_event *ev)
{
    observed *o = user;
    if (ev->kind == PLAT_EV_FOCUS) o->focus = ev->focused;
    if (ev->kind == PLAT_EV_KEY && ev->press) { assert(ev->utf8_len == sizeof "a" - 1 && ev->utf8[0] == 'a'); o->key = true; if (ev->repeat) o->repeat = true;
        if (o->stop_key) { o->priority_key = true; plat_quit(o->platform); } }
    if (ev->kind == PLAT_EV_KEY && !ev->press) o->release = true;
    if (ev->kind == PLAT_EV_BUTTON && ev->press) { assert(ev->code == 1 && ev->buttons == 1); o->button = true; }
    if (ev->kind == PLAT_EV_MOTION) { assert(ev->x == 1 && ev->y == 1); o->motion = true; }
    if (ev->kind == PLAT_EV_WHEEL) { assert(ev->dy == PLAT_WHEEL_UNIT); o->wheel = true; o->wheels++; }
    if (ev->kind == PLAT_EV_CLIPBOARD) { if (ev->clip_ok) o->clip = true; else { assert(ev->code == 2); o->lost = true; } }
    if (ev->kind == PLAT_EV_CLOSE) o->close = true;
}
static void blink(void *user)
{ observed *o = user; o->blink = true; plat_quit(o->platform); }
static void work(void *user)
{ observed *o = user; uint64_t ticks; assert(read(o->work_fd, &ticks, sizeof ticks) == sizeof ticks); o->work = true; plat_quit(o->platform); }
static void presentation_fallback_test(bool require_fallback)
{
    pid_t child = mock_start_options(true, false, NULL, 0, 0, true);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = -1};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK && !p.present_ok);
    int armed = wayland_frame_arm(&p, 1); /* (P) first nonzero frame identity */
    if (require_fallback) assert(armed == PLAT_OK);
    else assert(armed == PLAT_ERR_FAIL);
    plat_shutdown(&p); mock_join(child);
    puts("wayland_test: missing wp_presentation is detected; CPU fallback remains unsupported");
}
static void submitted(void *user, const editor_frame *frame)
{ (void)frame; *(bool *)user = true; }
static void allocation_test(void)
{
    pid_t child = mock_start(true);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = -1};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    plat_event event;
    while (plat_poll_event(&p, &event)) { /* retire startup input */ }
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_flush(p.conn) >= 0);
    struct pollfd native = {wl_display_get_fd(p.conn), POLLIN, 0};
    assert(poll(&native, 1, -1) > 0); /* (P) one actual native fd; no time budget */
    const char *power = bench_evidence_tag(); /* BAT0 checked before counting */
    edit_malloc_guard_begin();
    assert(!wl_display_prepare_read(p.conn));
    assert(!wl_display_read_events(p.conn));
    assert(wl_display_dispatch_pending(p.conn) >= 0);
    size_t transport = edit_malloc_guard_end();
    /* The red probe showed foreign packet allocation before decoded ingress.
     * Keep it visible. G1's enforced bracket starts at decoded input and ends
     * at submit; present's one-shot protocol objects are counted separately. */
    render_backend backend = {0}; assert(render_null_backend(&backend) == RENDER_OK);
    bool did_submit = false;
    const uint8_t initial[] = "seed";
    editor_config editor_cfg = {.initial = initial, .initial_len = sizeof initial - 1,
        .hook_ctx = &did_submit, .on_submit = submitted};
    editor *e = NULL; assert(editor_open(&e, &editor_cfg, &backend) == EDITOR_OK);
    do { assert(editor_step(e, 0) >= EDITOR_OK); } while (editor_get_stats(e).pending || backend.active);
    did_submit = false;
    edit_malloc_guard_begin();
    /* (P) KEY_A is Linux evdev code 30. Decoder + queue + real editor mutation,
     * layout and null submit stay in the same enforced allocator bracket. */
    assert(wayland_input_key(&p, 30, 0, true, false));
    size_t added = 0;
    while (plat_poll_event(&p, &event)) if (event.kind == PLAT_EV_KEY && event.press) {
        added += event.utf8_len;
        assert(editor_inject(e, &event) == EDITOR_OK);
    }
    while (!did_submit) assert(editor_step(e, 0) >= EDITOR_OK);
    size_t typing = edit_malloc_guard_end(); assert(!typing);
    assert(added && editor_length(e) == sizeof initial - 1 + added);
    edit_malloc_guard_begin();
    assert(wayland_frame_arm(&p, 1) == PLAT_OK); /* (P) first nonzero frame identity */
    size_t present = edit_malloc_guard_end();
    wayland_frame_cancel(&p);
    edit_malloc_guard_begin();
    void *volatile calibration = malloc(sizeof(plat_event)); assert(calibration); free(calibration);
    size_t calibrated = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) assert(calibrated == 1); /* (P) the explicit single malloc call */
    if (edit_malloc_guard_active()) printf("wayland_test: allocation counts transport=%zu typing=%zu post_submit_present=%zu calibration=%zu "
        "(M)%s derivation=read+dispatch,decoded_input_through_editor_submit,frame_arm,single_malloc guard=%s\n",
        transport, typing, present, calibrated, power, "active");
    else puts("wayland_test: native transport, typing and present exercised; allocator counts unavailable under ASan");
    editor_close(e); plat_shutdown(&p); mock_join(child);

}
static void output_test(void)
{
    /* (P) First scaled mode above unity: scale = unity + unity. Desktop base
     * DPI is the established 96 logical pixels/inch convention (E). */
    int32_t scale = 1 + 1;
    pid_t child = mock_start_options(true, false, NULL, 0, scale, false);
    wayland_display_info info;
    assert(wayland_display_size(&info) == PLAT_OK);
    assert(info.scale == (uint32_t)scale && info.width == info.scale && info.height == info.scale);
    assert(info.dpi == 96.0 * scale);
    mock_join(child);
    child = mock_start_options(true, false, NULL, 0, scale, false);
    plat p; plat_config cfg = {.width = (uint32_t)scale, .height = (uint32_t)scale, .work_eventfd = -1};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    assert(p.width == (uint32_t)scale * cfg.width && p.height == cfg.height);
    bool motion = false; plat_event event;
    while (plat_poll_event(&p, &event)) if (event.kind == PLAT_EV_MOTION) {
        assert(event.x == scale && event.y == scale); motion = true;
    }
    assert(motion);
    plat_shutdown(&p); mock_join(child);
    puts("wayland_test: wl_output scale supplies startup DPI and physical mode size");
}
static void protocol_test(void)
{
    pid_t child = mock_start(true);
    int work_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); assert(work_fd >= 0);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = work_fd};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    assert(p.present_ok && wayland_egl_window(&p));
    assert(p.width == cfg.width + 1 && p.height == cfg.height); /* independent configure dimensions */
    observed o = {.platform = &p, .work_fd = work_fd}; plat_callbacks cb = {.ud = &o, .on_event = observe, .on_blink = blink, .on_work = work};
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    assert(o.focus && o.key && o.release && o.button && o.motion && o.wheel);
    /* (P) minimum nonzero representable blink period in plat's ms ABI. */
    plat_set_blink(&p, 1); assert(plat_run_for(&p, &cb, -1) == PLAT_OK && o.blink);
    p.quit = false; plat_set_blink(&p, 0); o.blink = false;
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && !o.blink);
    uint64_t wake = 1; /* eventfd increment for the single work notification */
    assert(write(work_fd, &wake, sizeof wake) == sizeof wake);
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && o.work); p.quit = false;
    assert(o.wheels == 1); /* one detent announced by axis + value120 must emit once */
    /* Byte-sized evdev fixture covers the complete UINT8_MAX domain. The
     * two calls per key derive from press/release; pop immediately so the
     * assertion measures decoding, independently of native transport malloc. */
    edit_malloc_guard_begin();
    for (uint32_t key = 0; key <= UINT8_MAX; key++) {
        (void)wayland_input_key(&p, key, 0, true, false);
        (void)wayland_input_key(&p, key, 0, false, false);
        plat_event event;
        while (plat_poll_event(&p, &event)) assert(event.utf8_len <= sizeof event.utf8);
    }
    size_t allocations = edit_malloc_guard_end(); assert(!allocations);
    puts(edit_malloc_guard_active() ? "wayland_test: decoder allocation guard passed" : "wayland_test: decoder exercised; allocation count inert under sanitizers");
    /* (E) Queue inherits X11_QUEUE_CAP; the byte-domain press/release pair
     * above spans that capacity. Repeat it without popping to force overflow. */
    for (uint32_t key = 0; key <= UINT8_MAX; key++) {
        (void)wayland_input_key(&p, key, 0, true, false);
        (void)wayland_input_key(&p, key, 0, false, false);
    }
    assert(wayland_input_dropped(&p));
    plat_event queued;
    while (plat_poll_event(&p, &queued)) { /* discard the synthetic burst */ }
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    puts("wayland_test: overflow drops input without poisoning the connection");
    assert(plat_clip_request(&p, PLAT_CLIP_PRIMARY) == PLAT_ERR_FAIL);
    assert(plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) == PLAT_OK);
    while (!o.clip) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    size_t len; const uint8_t *bytes = plat_clip_data(&p, &len);
    assert(len == sizeof "mock clipboard" - 1 && !memcmp(bytes, "mock clipboard", len));
    o.clip = false;
    assert(plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) == PLAT_OK);
    size_t retained_len; const uint8_t *retained = plat_clip_data(&p, &retained_len);
    assert(retained == bytes && retained_len == len && !memcmp(bytes, "mock clipboard", len));
    while (!o.clip) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    bytes = plat_clip_data(&p, &len);
    assert(len == sizeof "replacement clipboard" - 1 && !memcmp(bytes, "replacement clipboard", len));
    /* IDs derive from consecutive nonzero frame IDs in render contract. */
    uint32_t id = 1; bool ready; uint64_t ns, msc;
    assert(wayland_frame_arm(&p, id) == PLAT_OK);
    assert(wayland_frame_arm(&p, id + 1) == PLAT_ERR_FAIL);
    assert(wayland_frame_result(&p, id + 1, &ready, &ns, &msc) == PLAT_ERR_FAIL);
    wl_surface_commit(wayland_surface(&p));
    assert(wl_display_roundtrip(p.conn) >= 0);
    assert(wayland_frame_result(&p, id, &ready, &ns, &msc) == PLAT_OK && ready && ns && msc);
    assert(wayland_frame_arm(&p, ++id) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p));
    assert(wl_display_roundtrip(p.conn) >= 0);
    assert(wayland_frame_result(&p, id, &ready, &ns, &msc) == PLAT_ERR_FAIL);
    assert(plat_clip_set(&p, PLAT_CLIP_CLIPBOARD, "owned clipboard", sizeof "owned clipboard" - 1) == PLAT_OK);
    o.clip = false;
    assert(plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) == PLAT_OK);
    retained = plat_clip_data(&p, &retained_len);
    assert(retained == bytes && retained_len == len);
    while (!o.clip) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    bytes = plat_clip_data(&p, &len);
    assert(len == sizeof "owned clipboard" - 1 && !memcmp(bytes, "owned clipboard", len));
    while (!o.close) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    assert(o.lost);
    /* Clamp/delay derives from existing X11_REPEAT_MAX_HZ and the immediate
     * repeat mode; no latency budget is asserted. */
    plat_set_repeat(&p, 0, 1000);
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_flush(p.conn) >= 0);
    struct pollfd native = {wl_display_get_fd(p.conn), POLLIN, 0};
    assert(poll(&native, 1, -1) > 0); /* wait for the actual mock key packet, no duration budget */
    o.stop_key = true; o.work = false;
    assert(write(work_fd, &wake, sizeof wake) == sizeof wake);
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && o.priority_key && !o.work);
    puts("wayland_test: native key precedes simultaneous worker wake");
    p.quit = false; o.stop_key = false;
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && o.work); p.quit = false;
    while (!o.repeat) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_roundtrip(p.conn) >= 0);
    o.repeat = false; assert(plat_run_for(&p, &cb, 0) == PLAT_OK && !o.focus && !o.repeat);
    plat_shutdown(&p); plat_shutdown(&p); mock_join(child); close(work_fd); cfg.work_eventfd = -1;
    child = mock_start(false);
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_FAIL);
    plat_shutdown(&p); mock_join(child);
    puts("wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed");
}
#endif

static void guard_test(void)
{
    /* (P) A Wayland socket can name any compositor; unlike X display numbers
     * there is no protocol-defined private name. Require explicit opt-in. */
    assert(!unsetenv("EDIT_ALLOW_REAL_DISPLAY"));
    assert(!unsetenv("EDIT_WAYLAND_DISPLAY"));
    assert(!setenv("WAYLAND_DISPLAY", "wayland-real", 1));
    assert(!setenv("WAYLAND_SOCKET", "0", 1)); /* stdin descriptor, never connected */
    edit_display_guard();
    assert(!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET"));
    assert(!setenv("EDIT_WAYLAND_DISPLAY", "wayland-private", 1));
    edit_display_guard();
    assert(!strcmp(getenv("WAYLAND_DISPLAY"), "wayland-private"));
    edit_display_guard(); /* explicit private selection is idempotent */
    assert(!strcmp(getenv("WAYLAND_DISPLAY"), "wayland-private"));
    assert(!setenv("EDIT_ALLOW_REAL_DISPLAY", "1", 1));
    assert(!setenv("WAYLAND_DISPLAY", "wayland-real", 1));
    assert(!setenv("WAYLAND_SOCKET", "0", 1)); /* stdin descriptor; never used */
    edit_display_guard();
    assert(!strcmp(getenv("WAYLAND_DISPLAY"), "wayland-real") && getenv("WAYLAND_SOCKET"));
    assert(!unsetenv("EDIT_ALLOW_REAL_DISPLAY"));
    assert(!unsetenv("EDIT_WAYLAND_DISPLAY"));
    assert(!unsetenv("WAYLAND_DISPLAY") && !unsetenv("WAYLAND_SOCKET"));
    puts("wayland_test: real compositor environment refused; private selection and opt-in preserved");
}
#ifdef EDIT_HAVE_WAYLAND
static void socket_path_test(void)
{
    struct sockaddr_un address;
    char name[sizeof address.sun_path + 1]; /* (P) first unrepresentable NUL-terminated Unix path */
    memset(name, 'x', sizeof name - 1); name[sizeof name - 1] = '\0';
    assert(!setenv("WAYLAND_DISPLAY", name, 1));
    assert(!setenv("XDG_RUNTIME_DIR", "/tmp", 1));
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = -1};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_SOCKET_PATH);
    plat_shutdown(&p);
    assert(!unsetenv("WAYLAND_DISPLAY"));
    char root[] = "/tmp/edit-1zm-runtime-XXXXXX"; assert(mkdtemp(root));
    char directory[sizeof root + sizeof name]; /* parent + slash + component + NUL */
    assert(snprintf(directory, sizeof directory, "%s/%s", root, name) > 0);
    /* (P) owner-only directory: read, write, search POSIX permission bits. */
    assert(!mkdir(directory, S_IRWXU));
    assert(!setenv("WAYLAND_TEST_EXPECT_DIRECTORY", directory, 1));
    assert(!setenv("XDG_RUNTIME_DIR", directory, 1));
    assert(!setenv("WAYLAND_DISPLAY", "wayland-test", 1));
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_NO_DISPLAY);
    assert(getenv("WAYLAND_TEST_CONNECTED_DIRECTORY"));
    plat_shutdown(&p);
    assert(!unsetenv("WAYLAND_TEST_EXPECT_DIRECTORY"));
    assert(!unsetenv("WAYLAND_TEST_CONNECTED_DIRECTORY"));
    assert(!unsetenv("WAYLAND_DISPLAY"));
    assert(!setenv("XDG_RUNTIME_DIR", "/tmp", 1));
    assert(!rmdir(directory) && !rmdir(root));
    puts("wayland_test: long runtime directory resolves through its descriptor");
    puts("wayland_test: oversized socket basename returns a distinct error");
}
#endif
int main(int argc, char **argv)
{
#ifdef EDIT_HAVE_WAYLAND_SERVER
    /* (P) program name plus one acceptance-probe argument. This opt-in probe
     * stays red until the separately required Wayland CPU renderer exists. */
    if (argc == 1 + 1 && !strcmp(argv[1], "--require-presentation-fallback")) {
        presentation_fallback_test(true); return 0;
    }
#else
    (void)argc; (void)argv;
#endif
    guard_test();
#ifdef EDIT_HAVE_WAYLAND
    socket_path_test();
#endif
    assert(plat_backend_select(NULL, NULL) == PLAT_BACKEND_X11);
    assert(plat_backend_select("", "") == PLAT_BACKEND_X11);
    assert(plat_backend_select(":99", "wayland-test") == PLAT_BACKEND_X11);
    assert(plat_backend_select(NULL, "wayland-test") == PLAT_BACKEND_WAYLAND);
    assert(plat_backend_select("", "wayland-test") == PLAT_BACKEND_WAYLAND);
    plat p;
    plat_config cfg = {.width = 1, .height = 1, .headless = true,
        .work_eventfd = -1};
    /* Unit dimensions are the minimum nonzero legal surface, derived from
     * the platform dimension contract. */
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_NO_DISPLAY);
    plat_shutdown(&p);
    puts("wayland_test: selection and headless lifecycle passed");
#ifdef EDIT_HAVE_WAYLAND_SERVER
    presentation_fallback_test(false);
    allocation_test();
    output_test();
    protocol_test();
#else
    puts("wayland_test: mock protocol unavailable (optional server headers absent)");
#endif
    return 0;
}
