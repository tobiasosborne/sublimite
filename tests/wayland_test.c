/* Runtime selection policy is a truth table over the environment, not a
 * timing/count budget. Headless init must not contact either display. */
#include "wayland/plat.h"
#include "os/os.h"
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
#include "raster/raster.h"
#include "work/work.h"
#include "trace/trace.h"

typedef struct observed { bool focus, key, release, button, motion, leave, wheel, clip, close, lost, repeat, blink, work, stop_key, priority_key; unsigned wheels; plat *platform; int work_fd; } observed;
static void observe(void *user, const plat_event *ev)
{
    observed *o = user;
    if (ev->kind == PLAT_EV_FOCUS) o->focus = ev->focused;
    if (ev->kind == PLAT_EV_KEY && ev->press) { assert(ev->utf8_len == sizeof "a" - 1 && ev->utf8[0] == 'a'); o->key = true; if (ev->repeat) o->repeat = true;
        if (o->stop_key) { o->priority_key = true; plat_quit(o->platform); } }
    if (ev->kind == PLAT_EV_KEY && !ev->press) o->release = true;
    if (ev->kind == PLAT_EV_BUTTON && ev->press) { assert(ev->code == 1 && ev->buttons == 1); o->button = true; }
    if (ev->kind == PLAT_EV_LEAVE) o->leave = true;
    if (ev->kind == PLAT_EV_MOTION) { assert(ev->x == 1 && ev->y == 1); o->motion = true; }
    if (ev->kind == PLAT_EV_WHEEL) { assert(ev->dy == PLAT_WHEEL_UNIT); o->wheel = true; o->wheels++; }
    if (ev->kind == PLAT_EV_CLIPBOARD) { if (ev->clip_ok) o->clip = true; else { assert(ev->code == 2); o->lost = true; } }
    if (ev->kind == PLAT_EV_CLOSE) o->close = true;
}
static void blink(void *user)
{ observed *o = user; o->blink = true; plat_quit(o->platform); }
static void work(void *user)
{ observed *o = user; uint64_t ticks; assert(read(o->work_fd, &ticks, sizeof ticks) == sizeof ticks); o->work = true; plat_quit(o->platform); }
/* edit-b6ai.6: without wp_presentation a frame arms only its frame callback
 * and that callback's done is the frame's completion (decisions/edit-b6ai.6.md). */
static void presentation_fallback_test(void)
{
    mock_shm = (mock_shm_options){.enabled = true};
    pid_t child = mock_start_options(true, false, NULL, 0, 0, true);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_wake = NULL};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK && !plat_present_available(&p));
    assert(wayland_shm(&p)); /* the raster fallback's buffer factory */
    uint32_t id = 1; /* (P) first nonzero frame identity */
    assert(wayland_frame_arm(&p, id) == PLAT_OK);
    bool ready = true; uint64_t ns = 0, msc = 1;
    assert(wayland_frame_result(&p, id, &ready, &ns, &msc) == PLAT_OK && !ready);
    wl_surface_commit(wayland_surface(&p));
    assert(wl_display_roundtrip(wayland_display(&p)) >= 0);
    assert(wayland_frame_result(&p, id, &ready, &ns, &msc) == PLAT_OK && ready && ns && !msc);
    assert(!wayland_frame_discarded(&p, id));
    plat_shutdown(&p); mock_join(child); mock_shm = (mock_shm_options){0};
    puts("wayland_test: missing wp_presentation arms a frame callback whose done completes the frame");
}

/* ---- edit-b6ai.6: Wayland CPU raster backend over wl_shm --------------- */
/* (P) Grid: the P2.5 live-pixel fixture of tests/raster_test.c, 4 x 6 cells
 * of 8 x 16 px = 32 x 96 px, one 8 x 16 glyph page. The surface adds four
 * buffer px on each axis (36 x 100) so both margins are painted and damaged;
 * both extents are even so scale 2 divides them. Resize grows each axis by
 * four more (40 x 104). Reserve: 64 x 128, the raster live fixture's. */
#define WLR_COLS 4u
#define WLR_ROWS 6u
#define WLR_CW 8u
#define WLR_CH 16u
#define WLR_W 36u
#define WLR_H 100u
#define WLR_RW 40u
#define WLR_RH 104u
#define WLR_BACKGROUND 0x203040u /* any opaque RGB distinct from the cell colours */
typedef struct wlr_fixture {
    plat p; work_pool pool; render_backend b; void *state; render_config cfg;
    render_cell cells[WLR_COLS * WLR_ROWS]; uint64_t bits[1]; render_grid g;
    uint8_t pixels[WLR_CW * WLR_CH]; render_atlas_page page; render_glyph glyph;
    mock_shm_shared *shared; pid_t child;
    uint32_t width, height, resize_w, resize_h, device, displayed, last_id; uint64_t t5, t6;
    bool resized;
} wlr_fixture;
static void wlr_device(void *user, uint32_t id, uint64_t ns)
{ wlr_fixture *f = user; assert(id == f->last_id); f->device++; f->t5 = ns; }
static void wlr_displayed(void *user, uint32_t id, uint64_t ns)
{ wlr_fixture *f = user; assert(id == f->last_id); f->displayed++; f->t6 = ns; }
static void wlr_work(const work_msg *m, void *user)
{
    render_event ev = {RENDER_EVENT_WORK, m->generation, 0, m};
    (void)render_backend_event(user, &ev); /* stale frames report FRAME; ignored */
}
static void wlr_complete(void *user, uint32_t id, uint64_t ns, uint64_t msc)
{ (void)id; (void)ns; (void)msc; wlr_fixture *f = user; assert(raster_poll_completions(&f->b) == RENDER_OK); }
static void wlr_event(void *user, const plat_event *ev)
{
    wlr_fixture *f = user;
    if (ev->kind == PLAT_EV_RESIZE) { f->resized = true; f->resize_w = ev->w; f->resize_h = ev->h; }
}
static void wlr_pump(wlr_fixture *f)
{
    (void)work_mailbox_drain(&f->pool, wlr_work, &f->b);
    plat_callbacks cb = {.ud = f, .on_event = wlr_event, .on_present_complete = wlr_complete};
    f->p.quit = false; assert(plat_run_for(&f->p, &cb, 1) == PLAT_OK); /* (P) minimum nonzero wait in plat's ms ABI */
    assert(raster_poll_completions(&f->b) == RENDER_OK);
}
static void wlr_compare(wlr_fixture *f)
{
    if (f->shared->width != f->width || f->shared->height != f->height)
        fprintf(stderr, "wayland_test: screen %ux%u, surface %ux%u, commits %u\n", f->shared->width, f->shared->height, f->width, f->height, f->shared->commits);
    assert(f->shared->width == f->width && f->shared->height == f->height);
    uint32_t want[WLR_COLS * WLR_CW * WLR_ROWS * WLR_CH];
    raster_scene scene = {f->cfg.dims, f->cells, &f->glyph, 1, &f->page, 1, 0};
    for (uint32_t row = 0; row < WLR_ROWS; row++)
        raster_row_scalar(&scene, want + (size_t)row * WLR_CH * WLR_COLS * WLR_CW, WLR_COLS * WLR_CW, row);
    size_t bad = 0;
    for (uint32_t y = 0; y < f->height; y++) for (uint32_t x = 0; x < f->width; x++) {
        uint32_t expected = x < WLR_COLS * WLR_CW && y < WLR_ROWS * WLR_CH ?
            want[(size_t)y * WLR_COLS * WLR_CW + x] : WLR_BACKGROUND;
        if ((f->shared->screen[(size_t)y * f->width + x] & 0xffffffu) != expected) bad++;
    }
    if (bad) fprintf(stderr, "wayland_test: %zu raster screen pixels differ\n", bad);
    assert(!bad);
}
/* Present frame id and wait for its completion; returns the post-submit
 * allocation count (present request marshalling and completion dispatch). */
static size_t wlr_finish(wlr_fixture *f, uint32_t id)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* (G) editor_test's existing settle deadline */
    size_t allocations = 0; int rc;
    for (;;) {
        edit_malloc_guard_begin(); rc = render_backend_present(&f->b, id); allocations += edit_malloc_guard_end();
        if (rc != RENDER_ERR_BUSY) break;
        wlr_pump(f); assert(trace_now_ns() < deadline);
    }
    assert(rc == RENDER_OK);
    while (f->b.active) {
        edit_malloc_guard_begin(); wlr_pump(f); allocations += edit_malloc_guard_end();
        assert(trace_now_ns() < deadline);
    }
    assert(f->device == f->displayed && f->t6 >= f->t5);
    return allocations;
}
static void wlr_open(wlr_fixture *f, bool presentation, bool present_always, int32_t scale, bool immediate, uint32_t resize_after)
{
    memset(f, 0, sizeof *f);
    f->shared = mmap(NULL, sizeof *f->shared, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(f->shared != MAP_FAILED);
    int32_t unit = scale ? scale : 1;
    mock_shm = (mock_shm_options){.enabled = true, .present_always = present_always, .release_immediately = immediate,
        .configure_set = true, .configure_w = (int32_t)WLR_W / unit, .configure_h = (int32_t)WLR_H / unit,
        .resize_after = resize_after, .resize_w = (int32_t)WLR_RW / unit, .resize_h = (int32_t)WLR_RH / unit, .shared = f->shared};
    f->child = mock_start_options(true, false, NULL, 0, scale, !presentation);
    plat_config pc = {.width = WLR_W, .height = WLR_H, .work_wake = NULL};
    assert(plat_init_backend(&f->p, &pc, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    assert(f->p.width == WLR_W && f->p.height == WLR_H);
    f->width = WLR_W; f->height = WLR_H;
    assert(wayland_buffer_scale(&f->p) == (uint32_t)unit);
    plat_event ev; while (plat_poll_event(&f->p, &ev)) { /* retire the mock's startup input */ }
    assert(!work_pool_init(&f->pool, 1, RASTER_JOBS));
    for (size_t i = 0; i < sizeof f->pixels; i++) f->pixels[i] = (uint8_t)(i * 7u); /* raster_test's ramp */
    f->page = (render_atlas_page){f->pixels, sizeof f->pixels, WLR_CW, WLR_CW, WLR_CH};
    f->glyph = (render_glyph){65, 0, 0, 0, WLR_CW, WLR_CH};
    f->cfg = (render_config){.dims = {WLR_COLS, WLR_ROWS, WLR_CW, WLR_CH}, .max_width = 64, .max_height = 128,
        .max_cells = WLR_COLS * WLR_ROWS, .max_glyphs = 1, .max_pages = 1, .max_atlas_bytes = sizeof f->pixels,
        .platform = &f->p, .workers = &f->pool, .hooks = {wlr_device, wlr_displayed, f}};
    assert(render_cpu_backend(&f->b) == RENDER_OK);
    render_backend_info info; assert(render_backend_query(&f->b, &info) == RENDER_OK);
    f->state = aligned_alloc(info.state_align, (info.state_size + info.state_align - 1u) / info.state_align * info.state_align);
    assert(f->state);
    int rc = render_backend_init(&f->b, &f->cfg, f->state, info.state_size);
    if (rc) fprintf(stderr, "wayland_test: raster init returned %d\n", rc);
    assert(rc == RENDER_OK);
    assert(raster_platform_completion(&f->b) && raster_completion_fd(&f->b) < 0);
    assert(render_grid_init(&f->g, f->cfg.dims, f->cells, WLR_COLS * WLR_ROWS, f->bits, 1) == RENDER_OK);
    f->g.pages = &f->page; f->g.page_count = 1; f->g.glyphs = &f->glyph; f->g.glyph_count = 1;
    f->g.surface_background = WLR_BACKGROUND;
    for (uint32_t i = 0; i < WLR_COLS * WLR_ROWS; i++)
        f->cells[i] = (render_cell){i % 3 ? 65u : 0u, i % 3 ? 0u : RENDER_NO_SLOT, 0xf0e0d0u - i * 0x010101u, i * 0x050505u,
            (uint16_t)(i % 5 == 0 ? RENDER_ATTR_UNDERLINE : i % 7 == 0 ? RENDER_ATTR_INVERSE : 0), 0};
}
static void wlr_close(wlr_fixture *f)
{
    assert(wl_display_roundtrip(wayland_display(&f->p)) >= 0 && !f->shared->held_destroyed);
    render_backend_shutdown(&f->b); free(f->state);
    work_pool_shutdown(&f->pool); plat_shutdown(&f->p); mock_join(f->child);
    munmap(f->shared, sizeof *f->shared); mock_shm = (mock_shm_options){0};
}
static void wlr_full(wlr_fixture *f, uint32_t id)
{
    f->last_id = id;
    assert(render_frame_begin(&f->g, id) == RENDER_OK && render_mark_full(&f->g) == RENDER_OK);
    render_strip full = {0, WLR_ROWS};
    assert(render_backend_submit(&f->b, &f->g, &full, 1) == RENDER_OK);
}
/* One raster run: full frame, ten partial edits (raster_test's disconnected
 * rows), four caret-only frames, then optionally a surface resize. */
static void wlr_case(const char *name, bool presentation, bool present_always, int32_t scale, bool immediate, bool resize)
{
    /* (P) resize fires after the fifteenth buffer commit: frames 1..15 below. */
    uint32_t frames = 1u + 10u + 4u;
    wlr_fixture f; wlr_open(&f, presentation, present_always, scale, immediate, resize ? frames : 0);
    wlr_full(&f, 1);
    size_t present_allocs = wlr_finish(&f, 1), typing_allocs = 0;
    wlr_compare(&f);
    raster_metrics m; assert(raster_frame_metrics(&f.b, &m) && m.frame_id == 1);
    uint32_t kind = !presentation ? RASTER_PRESENT_FRAME_CALLBACK : RASTER_PRESENT_FEEDBACK;
    assert(m.present_kind == kind);
    uint64_t ust = 1, msc = 1; assert(raster_last_present(&f.b, &ust, &msc));
    assert(presentation ? msc == 1 : !msc); /* mock feedback sequence is one; no MSC without presentation */
    uint32_t initial_fg = f.cells[8].fg;
    for (uint32_t id = 2; id < 12; id++) {
        render_strip strips[3]; size_t count = 0; f.last_id = id;
        edit_malloc_guard_begin();
        f.cells[8].fg = (initial_fg + id * 0x010203u) & 0xffffffu;
        assert(render_frame_begin(&f.g, id) == RENDER_OK && render_mark_rows(&f.g, 2, 1) == RENDER_OK);
        if (id % 2 == 0) {
            f.cells[0].bg = id * 0x030201u; f.cells[20].bg = id * 0x010307u;
            assert(render_mark_rows(&f.g, 0, 1) == RENDER_OK && render_mark_rows(&f.g, 5, 1) == RENDER_OK);
        } else {
            f.cells[12].bg = id * 0x010305u; assert(render_mark_rows(&f.g, 3, 1) == RENDER_OK);
        }
        assert(render_dirty_strips(&f.g, strips, 3, &count) == RENDER_OK);
        int rc = render_backend_submit(&f.b, &f.g, strips, count);
        typing_allocs += edit_malloc_guard_end(); assert(rc == RENDER_OK);
        present_allocs += wlr_finish(&f, id);
        wlr_compare(&f);
        assert(raster_frame_metrics(&f.b, &m) && m.frame_id == id);
        assert(m.present_kind == (presentation && !present_always ? RASTER_PRESENT_DISCARDED : kind));
    }
    for (uint32_t id = 12; id < 16; id++) {
        f.last_id = id;
        assert(render_frame_begin(&f.g, id) == RENDER_OK);
        f.cells[9].attrs ^= RENDER_ATTR_CURSOR;
        f.cells[9].fg = (f.cells[9].fg ^ 0x808080u) & 0xffffffu;
        f.cells[9].bg = (f.cells[9].bg ^ 0x303030u) & 0xffffffu;
        assert(render_mark_rows(&f.g, 2, 1) == RENDER_OK);
        render_strip one = {2, 1};
        raster_set_caret_only(&f.b, true);
        edit_malloc_guard_begin(); int rc = render_backend_submit(&f.b, &f.g, &one, 1); typing_allocs += edit_malloc_guard_end();
        assert(rc == RENDER_OK);
        present_allocs += wlr_finish(&f, id);
        assert(raster_frame_metrics(&f.b, &m) && m.jobs == 0 && m.inline_cells == 1);
        wlr_compare(&f);
    }
    raster_set_caret_only(&f.b, false);
    if (edit_malloc_guard_active()) assert(!typing_allocs);
    /* Release/reuse: release-on-replace alternates two slots; immediate
     * release reuses the most recently written one. No buffer per frame. */
    assert(f.shared->commits == frames && f.shared->distinct == (immediate ? 1u : 2u));
    assert(f.shared->scale == (uint32_t)(scale ? scale : 1));
    if (resize) {
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* (G) settle deadline as above */
        while (!f.resized) { wlr_pump(&f); assert(trace_now_ns() < deadline); }
        assert(f.resize_w == WLR_RW && f.resize_h == WLR_RH && f.p.width == WLR_RW);
        f.width = f.resize_w; f.height = f.resize_h; /* frames below draw at the new size */
        for (uint32_t id = 16; id < 18; id++) { wlr_full(&f, id); present_allocs += wlr_finish(&f, id); wlr_compare(&f); }
        /* (P) each slot recreated once at the new size, as it is next used. */
        assert(f.shared->distinct == (immediate ? 2u : 4u));
    }
    if (resize) frames += 2u; /* (P) frames 16 and 17 at the new size */
    printf("wayland_test: raster %s passed: frames=%u commits=%u buffers=%u damage_rects=%u releases=%u typing_mallocs=%zu post_submit_mallocs=%zu guard=%s\n",
        name, frames, f.shared->commits, f.shared->distinct, f.shared->damage_rects, f.shared->releases,
        typing_allocs, present_allocs, edit_malloc_guard_active() ? "active" : "ASan-inert");
    wlr_close(&f);
}
static void raster_unsupported_test(void)
{
    /* No wl_shm global: the raster backend refuses at init. */
    pid_t child = mock_start(true);
    plat p; plat_config pc = {.width = WLR_W, .height = WLR_H, .work_wake = NULL};
    assert(plat_init_backend(&p, &pc, PLAT_BACKEND_WAYLAND) == PLAT_OK && !wayland_shm(&p));
    work_pool pool; assert(!work_pool_init(&pool, 1, RASTER_JOBS));
    render_backend b; assert(render_cpu_backend(&b) == RENDER_OK);
    render_backend_info info; assert(render_backend_query(&b, &info) == RENDER_OK);
    void *state = aligned_alloc(info.state_align, (info.state_size + info.state_align - 1u) / info.state_align * info.state_align);
    render_config cfg = {.dims = {WLR_COLS, WLR_ROWS, WLR_CW, WLR_CH}, .max_width = 64, .max_height = 128,
        .max_cells = WLR_COLS * WLR_ROWS, .max_glyphs = 1, .max_pages = 1, .max_atlas_bytes = WLR_CW * WLR_CH,
        .platform = &p, .workers = &pool};
    assert(render_backend_init(&b, &cfg, state, info.state_size) == RENDER_ERR_UNSUPPORTED);
    free(state); work_pool_shutdown(&pool); plat_shutdown(&p); mock_join(child);
    puts("wayland_test: raster without wl_shm is unsupported");
}
static void raster_test(void)
{
    raster_unsupported_test();
    wlr_case("presentation-feedback", true, true, 0, false, false);
    wlr_case("presentation-discard", true, false, 0, false, false);
    wlr_case("no-presentation", false, false, 0, false, false);
    wlr_case("immediate-release", true, true, 0, true, false);
    wlr_case("scale-2", true, true, 1 + 1, false, false); /* (P) first integer scale above unity */
    wlr_case("resize", false, false, 0, false, true);
    wlr_case("resize-immediate-scale-2", true, true, 1 + 1, true, true);
}
/* Editor on the Wayland raster fallback: wp_presentation is absent, so GL
 * refuses at init and the editor must select raster (no X11 change). The
 * typing bracket is the editor's own on_ingress -> on_submit (law 2). */
typedef struct wl_counted { bool active; size_t allocations, frames; } wl_counted;
static void wl_ingress(void *ctx, uint64_t seq, uint64_t ns)
{ wl_counted *c = ctx; (void)seq; (void)ns; if (!c->active) { edit_malloc_guard_begin(); c->active = true; } }
static void wl_submitted(void *ctx, const editor_frame *frame)
{ wl_counted *c = ctx; (void)frame; if (c->active) { c->allocations += edit_malloc_guard_end(); c->active = false; } c->frames++; }
static void wl_settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* (G) editor_test's existing settle deadline */
    for (;;) {
        int rc = editor_step(e, 20); assert(rc == EDITOR_OK || rc == EDITOR_MORE); /* editor_test's step slice */
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return;
        assert(trace_now_ns() < deadline);
    }
}
static void editor_raster_test(void)
{
    mock_shm_shared *shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(shared != MAP_FAILED);
    mock_shm = (mock_shm_options){.enabled = true, .configure_set = true, .configure_w = 320, .configure_h = 240, .shared = shared};
    pid_t child = mock_start_options(true, false, NULL, 0, 0, true);
    render_backend b = {0}; assert(editor_backend_select(&b, NULL) == 0); /* GL, the application default */
    wl_counted c = {0};
    editor_config cfg = {.cols = 30, .rows = 6, .raster_fallback = true, .platform_backend = PLAT_BACKEND_WAYLAND,
        .hook_ctx = &c, .on_ingress = wl_ingress, .on_submit = wl_submitted};
    editor *e = NULL; int rc = editor_open(&e, &cfg, &b);
    if (rc) fprintf(stderr, "wayland_test: editor_open on Wayland raster returned %d\n", rc);
    assert(rc == EDITOR_OK);
    assert(!strcmp(b.info.name, "cpu-raster") && raster_platform_completion(&b));
    assert(editor_get_stats(e).backend_init_error == RENDER_ERR_UNSUPPORTED);
    wl_settle(e);
    assert(shared->commits && shared->width && shared->height);
    size_t before = c.allocations; c.allocations = 0;
    /* (P) editor_test's allocation case: alternating insert/backspace keys,
     * here one hundred, settled after every key so each frame completes. */
    for (unsigned i = 0; i < 100; i++) {
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = i % 2 ? XKB_KEY_BackSpace : 'x'};
        if (!(i % 2)) { ev.utf8[0] = 'x'; ev.utf8_len = 1; }
        assert(editor_inject(e, &ev) == EDITOR_OK);
        wl_settle(e);
    }
    assert(!c.active);
    if (c.allocations) fprintf(stderr, "wayland_test: editor raster allocation diagnostic: allocations=%zu\n", c.allocations);
    if (edit_malloc_guard_active()) assert(!c.allocations);
    uint32_t commits = shared->commits;
    editor_close(e); mock_join(child);
    printf("wayland_test: editor on Wayland without wp_presentation fell back to raster: startup_bracket=%zu typing_mallocs=%zu frames=%zu commits=%u guard=%s\n",
        before, c.allocations, c.frames, commits, edit_malloc_guard_active() ? "active" : "ASan-inert");
    munmap(shared, sizeof *shared); mock_shm = (mock_shm_options){0};
}
static void submitted(void *user, const editor_frame *frame)
{ (void)frame; *(bool *)user = true; }
static void allocation_test(void)
{
    pid_t child = mock_start(true);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_wake = NULL};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    plat_event event;
    while (plat_poll_event(&p, &event)) { /* retire startup input */ }
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_flush(wayland_display(&p)) >= 0);
    struct pollfd native = {wl_display_get_fd(wayland_display(&p)), POLLIN, 0};
    assert(poll(&native, 1, -1) > 0); /* (P) one actual native fd; no time budget */
    const char *power = bench_evidence_tag(); /* BAT0 checked before counting */
    edit_malloc_guard_begin();
    assert(!wl_display_prepare_read(wayland_display(&p)));
    assert(!wl_display_read_events(wayland_display(&p)));
    assert(wl_display_dispatch_pending(wayland_display(&p)) >= 0);
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
    plat_display_info shared;
    assert(plat_display_size(PLAT_BACKEND_WAYLAND, &shared) == PLAT_OK);
    assert(shared.dpi == info.dpi && shared.work_width == info.width && shared.work_height == info.height);
    assert(info.max_width == info.width && info.max_height == info.height); /* one output */
    assert(shared.max_width == info.max_width && shared.max_height == info.max_height);
    assert(plat_display_interval_ns(NULL) == 0);
    plat unknown = {.backend = PLAT_BACKEND_WAYLAND};
    assert(plat_display_interval_ns(&unknown) == 0);
    mock_join(child);
    /* edit-b6ai.16: a second, unity-scale output wider and taller than the
     * scaled one (fixture: 3 x 5 logical px, distinct axes). The selected
     * buffer scale stays the higher one; the reservation covers the wide
     * output at that scale: 3*scale x 5*scale buffer px. */
    mock_wide_output_w = 3; mock_wide_output_h = 5;
    child = mock_start_options(true, false, NULL, 0, scale, false);
    assert(wayland_display_size(&info) == PLAT_OK);
    assert(info.scale == (uint32_t)scale && info.width == info.scale && info.height == info.scale);
    assert(info.max_width == 3u * (uint32_t)scale && info.max_height == 5u * (uint32_t)scale);
    mock_join(child);
    mock_wide_output_w = mock_wide_output_h = 0;
    child = mock_start_options(true, false, NULL, 0, scale, false);
    plat p; plat_config cfg = {.width = (uint32_t)scale, .height = (uint32_t)scale, .work_wake = NULL};
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
    os_wake work_wake; assert(os_wake_init(&work_wake) == OS_OK);
    int work_fd = os_wake_fd(&work_wake);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_wake = &work_wake};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    assert(plat_present_available(&p) && wayland_egl_window(&p));
    assert(p.width == cfg.width + 1 && p.height == cfg.height); /* independent configure dimensions */
    observed o = {.platform = &p, .work_fd = work_fd}; plat_callbacks cb = {.ud = &o, .on_event = observe, .on_blink = blink, .on_work = work};
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    assert(o.focus && o.key && o.release && o.button && o.motion && o.leave && o.wheel);
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
    assert(wl_display_roundtrip(wayland_display(&p)) >= 0);
    assert(wayland_frame_result(&p, id, &ready, &ns, &msc) == PLAT_OK && ready && ns && msc);
    assert(wayland_frame_arm(&p, ++id) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p));
    assert(wl_display_roundtrip(wayland_display(&p)) >= 0);
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
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_flush(wayland_display(&p)) >= 0);
    struct pollfd native = {wl_display_get_fd(wayland_display(&p)), POLLIN, 0};
    assert(poll(&native, 1, -1) > 0); /* wait for the actual mock key packet, no duration budget */
    o.stop_key = true; o.work = false;
    assert(write(work_fd, &wake, sizeof wake) == sizeof wake);
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && o.priority_key && !o.work);
    puts("wayland_test: native key precedes simultaneous worker wake");
    p.quit = false; o.stop_key = false;
    assert(plat_run_for(&p, &cb, 0) == PLAT_OK && o.work); p.quit = false;
    while (!o.repeat) assert(plat_run_for(&p, &cb, 0) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_roundtrip(wayland_display(&p)) >= 0);
    o.repeat = false; assert(plat_run_for(&p, &cb, 0) == PLAT_OK && !o.focus && !o.repeat);
    plat_shutdown(&p); plat_shutdown(&p); mock_join(child); os_wake_close(&work_wake); cfg.work_wake = NULL;
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
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_wake = NULL};
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
    /* (P) program name plus one acceptance-probe argument: the edit-1zm probe,
     * green since edit-b6ai.6 (frame-callback completion + wl_shm raster). */
    if (argc == 1 + 1 && !strcmp(argv[1], "--require-presentation-fallback")) {
        presentation_fallback_test(); raster_test(); return 0;
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
        .work_wake = NULL};
    /* Unit dimensions are the minimum nonzero legal surface, derived from
     * the platform dimension contract. */
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_NO_DISPLAY);
    plat_shutdown(&p);
    puts("wayland_test: selection and headless lifecycle passed");
#ifdef EDIT_HAVE_WAYLAND_SERVER
    presentation_fallback_test();
    raster_test();
    editor_raster_test();
    allocation_test();
    output_test();
    protocol_test();
#else
    puts("wayland_test: mock protocol unavailable (optional server headers absent)");
#endif
    return 0;
}
