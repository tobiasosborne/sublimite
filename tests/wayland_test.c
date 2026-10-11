/* Runtime selection policy is a truth table over the environment, not a
 * timing/count budget. Headless init must not contact either display. */
#include "wayland/plat.h"
#include "wayland/private.h"
#include "base/base.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
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

int main(void)
{
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
    protocol_test();
#else
    puts("wayland_test: mock protocol unavailable (optional server headers absent)");
#endif
    return 0;
}
