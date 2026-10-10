/* Frozen conformance suite: every backend uses the common adapter. To run a
 * real backend unchanged, compile with -DRENDER_TEST_EXTERNAL and link a driver
 * defining render_test_prepare(b,cfg), render_test_pump(b), render_test_cleanup().
 * prepare calls its factory and supplies platform/workers/arena capacities;
 * pump routes work/fence/Present events on UI. Driver setup is outside guard.
 * Null is the default. Hardware drivers must not fake T5/T6.
 */
#include "render/render.h"
#include "base/base.h"
#include "font/font.h"
#include "trace/trace_fmt.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "render_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct fixture {
    render_grid g;
    render_cell cells[24];
    uint64_t bits[1];
    uint8_t pixels[128];
    render_atlas_page page;
    render_glyph glyph;
} fixture;

static int setup(fixture *f)
{
    memset(f, 0, sizeof *f);
    f->page = (render_atlas_page){f->pixels, sizeof f->pixels, 8, 8, 16};
    f->glyph = (render_glyph){65, 0, 0, 0, 8, 16};
    CHECK(render_grid_init(&f->g, (render_dims){4, 6, 8, 16}, f->cells, 24, f->bits, 1) == RENDER_OK);
    f->g.pages = &f->page; f->g.page_count = 1;
    f->g.glyphs = &f->glyph; f->g.glyph_count = 1;
    for (size_t i = 0; i < 24; i++)
        f->cells[i] = (render_cell){65, 0, 0xffffff, 0x102030, 0, 0};
    return 0;
}

static int strips_test(void)
{
    fixture f;
    CHECK(setup(&f) == 0);
    CHECK(render_frame_begin(&f.g, 1) == RENDER_OK);
    render_strip s[3] = {{99,99}, {99,99}, {99,99}};
    size_t n = 99;
    CHECK(render_dirty_strips(&f.g, s, 3, &n) == RENDER_OK && n == 0);
    CHECK(render_mark_rows(&f.g, 1, 2) == RENDER_OK);
    CHECK(render_mark_rows(&f.g, 4, 1) == RENDER_OK);
    CHECK(render_dirty_strips(&f.g, s, 3, &n) == RENDER_OK && n == 2);
    CHECK(s[0].first_row == 1 && s[0].row_count == 2);
    CHECK(s[1].first_row == 4 && s[1].row_count == 1);
    s[0] = (render_strip){99,99};
    CHECK(render_dirty_strips(&f.g, s, 1, &n) == RENDER_ERR_CAPACITY && n == 2);
    CHECK(s[0].first_row == 99 && s[0].row_count == 99);
    CHECK(render_dirty_strips(&f.g, NULL, 0, &n) == RENDER_ERR_CAPACITY && n == 2);
    CHECK(render_mark_rows(&f.g, 0, 6) == RENDER_OK);
    CHECK(render_dirty_strips(&f.g, s, 3, &n) == RENDER_OK && n == 1);
    CHECK(s[0].first_row == 0 && s[0].row_count == 6);
    CHECK(render_frame_begin(&f.g, 2) == RENDER_OK && f.bits[0] == 0);
    f.bits[0] = UINT64_MAX << 6; /* padding does not dirty rows */
    CHECK(render_dirty_strips(&f.g, NULL, 0, &n) == RENDER_OK && n == 0);
    CHECK(render_mark_full(&f.g) == RENDER_OK);
    CHECK(render_dirty_strips(&f.g, s, 3, &n) == RENDER_OK && n == 1);
    CHECK(s[0].first_row == 0 && s[0].row_count == 6);
    /* Word boundary: 63,64 coalesce; final valid bit 128 survives. */
    render_grid g;
    render_cell cells[129]; uint64_t bits[3];
    CHECK(render_grid_init(&g, (render_dims){1,129,1,1}, cells, 129, bits, 3) == RENDER_OK);
    CHECK(render_frame_begin(&g, 1) == RENDER_OK);
    CHECK(render_mark_rows(&g, 63, 2) == RENDER_OK);
    CHECK(render_mark_rows(&g, 128, 1) == RENDER_OK);
    CHECK(render_dirty_strips(&g, s, 3, &n) == RENDER_OK && n == 2);
    CHECK(s[0].first_row == 63 && s[0].row_count == 2);
    CHECK(s[1].first_row == 128 && s[1].row_count == 1);
    return 0;
}

static int arguments_test(void)
{
    fixture f; CHECK(setup(&f) == 0);
    render_grid saved = f.g;
    CHECK(render_grid_init(NULL, f.g.dims, f.cells, 24, f.bits, 1) == RENDER_ERR_ARG);
    CHECK(render_grid_init(&f.g, (render_dims){0,6,8,16}, f.cells, 24, f.bits, 1) == RENDER_ERR_BOUNDS);
    CHECK(memcmp(&saved, &f.g, sizeof saved) == 0);
    CHECK(render_grid_init(&f.g, (render_dims){UINT32_MAX,6,8,16}, f.cells, 24, f.bits, 1) == RENDER_ERR_BOUNDS);
    CHECK(render_grid_init(&f.g, f.g.dims, NULL, 24, f.bits, 1) == RENDER_ERR_ARG);
    CHECK(render_grid_init(&f.g, f.g.dims, f.cells, 23, f.bits, 1) == RENDER_ERR_CAPACITY);
    CHECK(render_grid_init(&f.g, f.g.dims, f.cells, 24, f.bits, 0) == RENDER_ERR_CAPACITY);
    CHECK(render_mark_full(&f.g) == RENDER_ERR_STATE);
    CHECK(render_frame_begin(&f.g, 0) == RENDER_ERR_FRAME);
    CHECK(render_frame_begin(&f.g, 10) == RENDER_OK);
    CHECK(render_frame_begin(&f.g, 10) == RENDER_ERR_FRAME);
    CHECK(render_frame_begin(&f.g, 9) == RENDER_ERR_FRAME);
    CHECK(render_mark_rows(&f.g, 6, 1) == RENDER_ERR_BOUNDS);
    CHECK(render_mark_rows(&f.g, 1, UINT32_MAX) == RENDER_ERR_BOUNDS);
    CHECK(render_mark_rows(&f.g, 6, 0) == RENDER_OK);
    CHECK(render_mark_rows(&f.g, 7, 0) == RENDER_ERR_BOUNDS);
    CHECK(render_mark_rows(NULL, 0, 1) == RENDER_ERR_ARG);
    size_t n = 7; render_strip s[3];
    CHECK(render_dirty_strips(NULL, s, 3, &n) == RENDER_ERR_ARG && n == 7);
    CHECK(render_dirty_strips(&f.g, s, 3, NULL) == RENDER_ERR_ARG);
    CHECK(render_dirty_strips(&f.g, NULL, 1, &n) == RENDER_ERR_ARG);
    CHECK(render_grid_validate(NULL) == RENDER_ERR_ARG);
    CHECK(render_frame_begin(&f.g, UINT32_MAX) == RENDER_OK);
    CHECK(render_frame_begin(&f.g, 1) == RENDER_ERR_FRAME);
    return 0;
}

static int cells_test(void)
{
    fixture f; CHECK(setup(&f) == 0);
    CHECK(render_grid_validate(&f.g) == RENDER_OK);
    render_cell saved = f.cells[0];
    f.cells[0].atlas_slot = 1; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[0] = saved; f.cells[0].glyph_index = 66; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[0] = saved; f.cells[0].attrs = 0x100; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[0] = saved; f.cells[0].reserved = 1; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[0] = saved; f.cells[0].fg = 0xff000000; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[0] = saved; f.cells[0].attrs = RENDER_ATTR_WIDE_LEFT;
    CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[1] = (render_cell){0, RENDER_NO_SLOT, saved.fg, saved.bg, RENDER_ATTR_WIDE_RIGHT, 0};
    CHECK(render_grid_validate(&f.g) == RENDER_OK);
    f.cells[1].attrs |= RENDER_ATTR_SELECTION; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    f.cells[1].attrs = RENDER_ATTR_WIDE_RIGHT;
    f.cells[1].bg++; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL); f.cells[1].bg--;
    f.cells[1].glyph_index = 1; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL); f.cells[1].glyph_index = 0;
    f.cells[0] = saved; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL); /* orphan right */
    f.cells[1] = saved; f.cells[3].attrs = RENDER_ATTR_WIDE_LEFT;
    CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL); f.cells[3] = saved;
    f.cells[0].attrs = RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_WIDE_RIGHT;
    CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL); f.cells[0] = saved;
    f.page.stride = 7; CHECK(render_grid_validate(&f.g) == RENDER_ERR_BOUNDS); f.page.stride = 8;
    f.page.pixels_len--; CHECK(render_grid_validate(&f.g) == RENDER_ERR_CAPACITY); f.page.pixels_len++;
    f.page.stride = SIZE_MAX; CHECK(render_grid_validate(&f.g) == RENDER_ERR_BOUNDS); f.page.stride = 8;
    f.glyph.x = UINT32_MAX; CHECK(render_grid_validate(&f.g) == RENDER_ERR_BOUNDS); f.glyph.x = 0;
    f.glyph.page = 1; CHECK(render_grid_validate(&f.g) == RENDER_ERR_BOUNDS); f.glyph.page = 0;
    f.glyph.w = 0; CHECK(render_grid_validate(&f.g) == RENDER_ERR_BOUNDS); f.glyph.w = 8;
    /* Actual baked ASCII page and a second runtime-style page in one grid. */
    const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
    CHECK(a != NULL);
    render_atlas_page pages[2] = {
        {a->pixels, a->pixels_len, (size_t)a->cell.cell_w * 95,
         a->cell.cell_w * 95, a->cell.cell_h},
        {f.pixels, sizeof f.pixels, 8, 8, 16}
    };
    render_glyph glyphs[2] = {{65,0,33 * 8,0,8,15}, {66,1,0,0,8,15}};
    f.g.dims.cell_h = 15; f.g.pages = pages; f.g.page_count = 2;
    f.g.glyphs = glyphs; f.g.glyph_count = 2;
    f.cells[1].glyph_index = 66; f.cells[1].atlas_slot = 1;
    CHECK(render_grid_validate(&f.g) == RENDER_OK);
    /* Full two-cell bitmap, inverse/underline applied consistently to pair. */
    CHECK(setup(&f) == 0);
    uint8_t wide_pixels[256] = {0};
    f.page = (render_atlas_page){wide_pixels,sizeof wide_pixels,16,16,16};
    f.glyph.w = 16;
    for (size_t i = 0; i < 24; i++) f.cells[i] = (render_cell){0,RENDER_NO_SLOT,0xffffff,0,0,0};
    f.cells[0] = (render_cell){65,0,0xffffff,0,
        RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_INVERSE | RENDER_ATTR_UNDERLINE,0};
    f.cells[1].attrs = RENDER_ATTR_WIDE_RIGHT | RENDER_ATTR_INVERSE | RENDER_ATTR_UNDERLINE;
    CHECK(render_grid_validate(&f.g) == RENDER_OK);
    f.cells[0].attrs &= (uint16_t)~RENDER_ATTR_WIDE_LEFT;
    CHECK(render_grid_validate(&f.g) == RENDER_ERR_CELL);
    return 0;
}

typedef struct delivery { uint32_t t5_id, t6_id; size_t t5_count, t6_count; uint64_t t5_ns, t6_ns; } delivery;
static void done_hook(void *u, uint32_t id, uint64_t ns)
{ delivery *d = u; d->t5_id = id; d->t5_ns = ns; d->t5_count++; }
static void complete_hook(void *u, uint32_t id, uint64_t ns)
{ delivery *d = u; d->t6_id = id; d->t6_ns = ns; d->t6_count++; }

typedef struct init_task { render_backend *b; render_config *cfg; void *state; size_t bytes; int result; } init_task;
static void *init_worker(void *u)
{
    init_task *t = u;
    (void)trace_thread_register();
    t->result = render_backend_init(t->b, t->cfg, t->state, t->bytes);
    return NULL;
}
static int init_on_worker(render_backend *b, render_config *cfg, void *state, size_t bytes)
{
    init_task task = {b,cfg,state,bytes,RENDER_ERR_INIT}; pthread_t thread;
    CHECK(pthread_create(&thread, NULL, init_worker, &task) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    return task.result;
}
static int shutdown_on_ui(render_backend *b)
{
    render_backend_shutdown(b);
    return 0;
}

#ifdef RENDER_TEST_EXTERNAL
int render_test_prepare(render_backend *b, render_config *cfg);
int render_test_pump(render_backend *b);
void render_test_cleanup(void);
#else
static int render_test_prepare(render_backend *b, render_config *cfg) { (void)cfg; return render_null_backend(b); }
static int render_test_pump(render_backend *b) { (void)b; return RENDER_OK; }
static void render_test_cleanup(void) { }
#endif

static int finish_frame(render_backend *b, uint32_t id)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    int rc;
    do {
        rc = render_backend_present(b, id);
        if (rc == RENDER_ERR_BUSY) CHECK(render_test_pump(b) == RENDER_OK);
        CHECK(trace_now_ns() < deadline);
    } while (rc == RENDER_ERR_BUSY);
    CHECK(rc == RENDER_OK);
    while (b->active) {
        CHECK(render_test_pump(b) == RENDER_OK);
        CHECK(trace_now_ns() < deadline);
    }
    return 0;
}

static int backend_test(void)
{
    fixture f; CHECK(setup(&f) == 0);
    render_backend b = {0}; delivery d = {0};
    render_config cfg = {.dims = {4,6,8,16}, .max_width = 32, .max_height = 128, .max_cells = 32, .max_glyphs = 4,
        .max_pages = 2, .max_atlas_bytes = 256, .hooks = {done_hook,complete_hook,&d}};
    CHECK(render_test_prepare(&b, &cfg) == RENDER_OK);
    render_backend_info info;
    CHECK(render_backend_query(&b, &info) == RENDER_OK && info.name != NULL);
    CHECK(info.state_size > 0 && info.state_align > 0);
    CHECK(render_backend_query(NULL, &info) == RENDER_ERR_ARG);
    edit_arena state_arena; CHECK(edit_arena_init(&state_arena, info.state_size + info.state_align) == 0);
    void *state = edit_arena_alloc(&state_arena, info.state_size, info.state_align);
    CHECK(state != NULL);
    CHECK(render_backend_present(&b, 1) == RENDER_ERR_STATE);
    CHECK(init_on_worker(&b, &cfg, state, info.state_size - 1) == RENDER_ERR_CAPACITY);
    CHECK(init_on_worker(&b, &cfg, state, info.state_size) == RENDER_OK);
    CHECK(init_on_worker(&b, &cfg, state, info.state_size) == RENDER_ERR_STATE);
    CHECK(render_frame_begin(&f.g, 1) == RENDER_OK);
    render_strip full = {0,6}, bad = {5,2};
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_ERR_STRIPS); /* first full */
    CHECK(render_mark_full(&f.g) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, &bad, 1) == RENDER_ERR_STRIPS);
    CHECK(render_backend_submit(&b, &f.g, NULL, 1) == RENDER_ERR_ARG);
    render_strip split[2] = {{0,3}, {3,3}};
    CHECK(render_backend_submit(&b, &f.g, split, 2) == RENDER_ERR_STRIPS);
    f.g.dims.cell_w++;
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_BOUNDS);
    f.g.dims.cell_w--;
    f.page.pixels_len = cfg.max_atlas_bytes + 1;
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_CAPACITY);
    f.page.pixels_len = sizeof f.pixels;
    f.cells[0].atlas_slot = 99;
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_CELL);
    f.cells[0].atlas_slot = 0;
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_BUSY);
    CHECK(render_backend_resize(&b, cfg.dims) == RENDER_ERR_BUSY);
    CHECK(render_backend_present(&b, 2) == RENDER_ERR_FRAME);
    /* Caller may overwrite cells/metadata while pending; atlas bytes wait for T5. */
    CHECK(render_frame_begin(&f.g, 2) == RENDER_OK);
    f.cells[0].bg++; f.glyph.glyph_index = 99;
    CHECK(finish_frame(&b, 1) == 0);
    f.cells[0].bg--; f.glyph.glyph_index = 65; f.pixels[0] = 255;
    CHECK(d.t5_count == 1 && d.t6_count == 1 && d.t5_id == 1 && d.t6_id == 1);
    CHECK(d.t5_ns > 0 && d.t6_ns >= d.t5_ns);
    CHECK(render_backend_present(&b, 1) == RENDER_ERR_STATE);
    f.g.frame_id = 1;
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_ERR_FRAME);
    f.g.frame_id = 2;
    CHECK(render_mark_rows(&f.g, 2, 1) == RENDER_OK);
    render_strip row = {2,1};
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_STRIPS);
    CHECK(render_backend_submit(&b, &f.g, &row, 1) == RENDER_OK);
    CHECK(finish_frame(&b, 2) == 0);
    render_stats stats;
    CHECK(render_backend_stats(&b, &stats) == RENDER_OK);
    CHECK(stats.submitted_frames == 2 && stats.presented_frames == 2);
    CHECK(stats.submitted_cells == 28 && stats.submitted_strips == 2);
    CHECK(render_frame_begin(&f.g, 3) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_OK);
    CHECK(finish_frame(&b, 3) == 0);
    CHECK(render_backend_resize(&b, (render_dims){0,6,8,16}) == RENDER_ERR_BOUNDS);
    CHECK(render_backend_resize(&b, (render_dims){4,9,8,16}) == RENDER_ERR_CAPACITY);
    CHECK(render_backend_resize(&b, (render_dims){4,6,9,16}) == RENDER_ERR_CAPACITY);
    CHECK(render_backend_resize(&b, (render_dims){4,6,8,32}) == RENDER_ERR_CAPACITY);
    CHECK(render_backend_resize(&b, cfg.dims) == RENDER_OK);
    CHECK(render_frame_begin(&f.g, 4) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_ERR_STRIPS);
    CHECK(render_mark_full(&f.g) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_OK);
    CHECK(finish_frame(&b, 4) == 0);
    /* 10k typing edits including begin/fill/damage/submit/present/events. */
    edit_malloc_guard_begin();
    int failed = 0;
    for (uint32_t id = 5; id < 10005; id++) {
        render_strip strips[3]; size_t count = 0;
        if (render_frame_begin(&f.g, id) != RENDER_OK) { failed = 1; break; }
        f.cells[8].bg = id;
        if (render_mark_rows(&f.g, 2, 1) != RENDER_OK ||
            render_dirty_strips(&f.g, strips, 3, &count) != RENDER_OK ||
            render_backend_submit(&b, &f.g, strips, count) != RENDER_OK ||
            finish_frame(&b, id) != 0) { failed = 1; break; }
    }
    size_t allocations = edit_malloc_guard_end();
    CHECK(!failed);
    if (edit_malloc_guard_active()) { CHECK(allocations == 0); printf("render no-malloc: 10000 frames, %zu allocations\n", allocations); }
    else printf("render no-malloc: ASan guard inactive; release run required\n");
    CHECK(d.t5_count == 10004 && d.t6_count == 10004 && d.t5_id == 10004 && d.t6_id == 10004);
    CHECK(shutdown_on_ui(&b) == 0);
    CHECK(shutdown_on_ui(&b) == 0);
    CHECK(render_backend_submit(&b, &f.g, &row, 1) == RENDER_ERR_STATE);
    edit_arena_free(&state_arena); render_test_cleanup();
    return 0;
}

/* Delayed test backend exercises asynchronous common-adapter states/failure
 * retry without depending on GPU/X11 scheduling. Caller-owned snapshot. */
typedef struct delayed_state { render_cell cell; render_glyph glyph; uint8_t pixel; int fail_submit, fail_present, fail_resize; } delayed_state;
static int delayed_init(render_backend *b, const render_config *c) { (void)c; memset(b->state, 0, sizeof(delayed_state)); return RENDER_OK; }
static int delayed_resize(render_backend *b, render_dims dims) { (void)dims; delayed_state *s = b->state; return s->fail_resize ? RENDER_ERR_DEVICE : RENDER_OK; }
static int delayed_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t n)
{
    (void)strips; (void)n; delayed_state *s = b->state;
    if (s->fail_submit) return RENDER_ERR_DEVICE;
    s->cell = g->cells[0]; s->glyph = g->glyphs[0]; s->pixel = g->pages[0].pixels[0]; return RENDER_OK;
}
static int delayed_present(render_backend *b, uint32_t id) { (void)id; delayed_state *s = b->state; return s->fail_present ? RENDER_ERR_BUSY : RENDER_OK; }
static int delayed_event(render_backend *b, const render_event *e) { (void)b; (void)e; return RENDER_ERR_UNSUPPORTED; }
static void delayed_shutdown(render_backend *b) { (void)b; }
static int async_test(void)
{
    fixture f; CHECK(setup(&f) == 0); delivery d = {0}; delayed_state state;
    render_backend b = {.info = {"delayed-test",sizeof state,_Alignof(delayed_state),3},
        .ops = {delayed_init,delayed_resize,delayed_submit,delayed_present,delayed_event,delayed_shutdown}};
    render_config cfg = {.dims = {4,6,8,16}, .max_width = 32, .max_height = 128, .max_cells = 24, .max_glyphs = 1,
        .max_pages = 1, .max_atlas_bytes = 128, .hooks = {done_hook,complete_hook,&d}};
    CHECK(init_on_worker(&b, &cfg, &state, sizeof state) == RENDER_OK);
    CHECK(render_frame_begin(&f.g, 7) == RENDER_OK); CHECK(render_mark_full(&f.g) == RENDER_OK);
    render_strip full = {0,6};
    state.fail_submit = 1; CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_DEVICE);
    CHECK(!b.active && b.last_frame == 0);
    state.fail_submit = 0; CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_OK);
    f.cells[0].bg++; f.glyph.glyph_index++; f.pixels[0]++;
    CHECK(state.cell.bg == 0x102030 && state.glyph.glyph_index == 65 && state.pixel == 0);
    CHECK(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 6, 1) == RENDER_ERR_FRAME);
    CHECK(render_backend_signal(&b, RENDER_EVENT_WORK, 7, 1) == RENDER_ERR_ARG);
    CHECK(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 7, 1) == RENDER_OK);
    CHECK(d.t5_count == 0); /* device done before present: withhold T5 */
    CHECK(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 7, 1) == RENDER_ERR_STATE);
    state.fail_present = 1; CHECK(render_backend_present(&b, 7) == RENDER_ERR_BUSY);
    CHECK(d.t5_count == 0 && b.stats.presented_frames == 0);
    state.fail_present = 0; CHECK(render_backend_present(&b, 7) == RENDER_OK);
    CHECK(d.t5_count == 1 && d.t5_id == 7 && d.t5_ns == b.submitted_ns);
    CHECK(render_backend_present(&b, 7) == RENDER_ERR_STATE);
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_ERR_BUSY);
    render_event complete = {RENDER_EVENT_PRESENT_COMPLETE,7,trace_now_ns(),NULL};
    CHECK(render_backend_event(&b, &complete) == RENDER_OK && !b.active);
    CHECK(d.t6_count == 1 && d.t6_id == 7);
    CHECK(render_backend_event(&b, &complete) == RENDER_ERR_FRAME);
    state.fail_resize = 1; CHECK(render_backend_resize(&b, (render_dims){3,6,8,16}) == RENDER_ERR_DEVICE);
    CHECK(b.config.dims.cols == 4);
    /* T6 delivered before T5: keep busy until device acknowledgement. */
    f.cells[0].bg--; f.glyph.glyph_index--; CHECK(render_frame_begin(&f.g, 8) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_OK);
    CHECK(render_backend_present(&b, 8) == RENDER_OK);
    CHECK(render_backend_signal(&b, RENDER_EVENT_PRESENT_COMPLETE, 8, 0) == RENDER_OK && b.active);
    CHECK(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 8, 0) == RENDER_OK && !b.active);
    CHECK(d.t5_count == 2 && d.t6_count == 2);
    CHECK(render_frame_begin(&f.g, 9) == RENDER_OK);
    CHECK(render_backend_submit(&b, &f.g, NULL, 0) == RENDER_OK);
    CHECK(shutdown_on_ui(&b) == 0);
    CHECK(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 9, 0) == RENDER_ERR_STATE);
    CHECK(d.t5_count == 2 && d.t6_count == 2);
    return 0;
}

static void *trace_worker(void *u)
{
    (void)u; (void)trace_thread_register();
    render_trace_device_done(NULL, 42, 12345);
    render_trace_present_complete(NULL, 42, 23456);
    return NULL;
}
static int trace_test(void)
{
    trace_reset(); pthread_t thread;
    CHECK(pthread_create(&thread, NULL, trace_worker, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    render_trace_device_done(NULL, 43, 0); render_trace_present_complete(NULL, 43, 0);
    fixture f; CHECK(setup(&f) == 0);
    render_backend b = {0}; CHECK(render_null_backend(&b) == RENDER_OK);
    render_config cfg = {.dims = {4,6,8,16}, .max_width = 32, .max_height = 128, .max_cells = 24, .max_glyphs = 1,
        .max_pages = 1, .max_atlas_bytes = 128};
    _Alignas(16) uint8_t state[64];
    CHECK(init_on_worker(&b, &cfg, state, sizeof state) == RENDER_OK);
    CHECK(render_null_backend(&b) == RENDER_ERR_STATE);
    CHECK(render_frame_begin(&f.g, 44) == RENDER_OK && render_mark_full(&f.g) == RENDER_OK);
    render_strip full = {0,6};
    CHECK(render_backend_submit(&b, &f.g, &full, 1) == RENDER_OK);
    CHECK(render_backend_present(&b, 44) == RENDER_OK);
    CHECK(shutdown_on_ui(&b) == 0);
    FILE *file = tmpfile(); CHECK(file != NULL); CHECK(trace_dump(file) == 0); rewind(file);
    trace_loaded loaded; CHECK(trace_fmt_load_dump(file, &loaded) == 0); fclose(file);
    CHECK(loaded.nrecs == 7);
    size_t seen = 0, frame_seen = 0; uint64_t submitted_ns = 0;
    for (size_t i = 0; i < loaded.nrecs; i++) {
        trace_rec r = loaded.recs[i];
        if (r.frame_id == 44) {
            if (frame_seen == 0) { CHECK(r.ev == TRACE_T4_PRESENT_SUBMITTED); submitted_ns = r.ns; }
            if (frame_seen == 1) CHECK(r.ev == TRACE_T5_DEVICE_DONE && r.ns >= submitted_ns);
            if (frame_seen == 2) CHECK(r.ev == TRACE_T6_PRESENT_COMPLETE && r.ns >= submitted_ns);
            frame_seen++; continue;
        }
        CHECK(r.frame_id == 42 || r.frame_id == 43);
        CHECK(r.ev == TRACE_T5_DEVICE_DONE || r.ev == TRACE_T6_PRESENT_COMPLETE);
        if (r.frame_id == 42) { CHECK(r.ns == (r.ev == TRACE_T5_DEVICE_DONE ? 12345u : 23456u)); seen++; }
        else CHECK(r.ns > 0);
    }
    CHECK(seen == 2 && frame_seen == 3); trace_fmt_dump_free(&loaded);
    return 0;
}

/* Native conformance uses an explicit output lane. It is separate from the
 * frozen timing suite: a diagnostic lane must never fabricate Present T6. */
#ifdef RENDER_TEST_NATIVE
typedef struct render_native_lane {
    void *user;
    int (*window_size)(void *,uint32_t,uint32_t);
    int (*paint)(void *,render_backend *,const render_grid *,const render_strip *);
    int (*pixels)(void *,uint32_t *,size_t);
    int (*close)(void *,bool);
} render_native_lane;
static int render_native_resize_contract(render_backend *b,const render_native_lane *lane)
{
    const render_dims shapes[]={{2,2,4,4},{3,3,4,4},{2,1,3,5},{3,2,3,5},
        {3,3,4,4},{2,1,3,5},{3,3,4,4},{2,1,3,5}};
    /* Last transitions reuse one native surface at the same dimensions:
     * margins must erase previously painted cells in both swap buffers. */
    const uint32_t margins[][2]={{0,0},{1,3},{2,2},{2,1},
        {0,0},{6,7},{0,0},{6,7}};
    render_cell cells[16]; uint64_t dirty[1]; uint32_t pixels[256];
    for (size_t step=0;step<sizeof shapes/sizeof shapes[0];step++) {
        render_dims dims=shapes[step];
        uint32_t width=dims.cols*dims.cell_w+margins[step][0];
        uint32_t height=dims.rows*dims.cell_h+margins[step][1];
        CHECK(lane->window_size(lane->user,width,height)==0);
        CHECK(render_backend_resize(b,dims)==RENDER_OK);
        render_grid grid;
        CHECK(render_grid_init(&grid,dims,cells,16,dirty,1)==RENDER_OK);
        for (size_t i=0;i<(size_t)dims.cols*dims.rows;i++)
            cells[i]=(render_cell){0,RENDER_NO_SLOT,0,0x234567u+(uint32_t)i*0x10101u,0,0};
        CHECK(render_frame_begin(&grid,(uint32_t)step+1)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
        render_strip strip={0,dims.rows};
        CHECK(lane->paint(lane->user,b,&grid,&strip)==0);
        CHECK(lane->pixels(lane->user,pixels,256)==0);
        for (uint32_t y=0;y<height;y++) for (uint32_t x=0;x<width;x++) {
            uint32_t expected=0;
            if (x<dims.cols*dims.cell_w && y<dims.rows*dims.cell_h)
                expected=cells[(size_t)(y/dims.cell_h)*dims.cols+x/dims.cell_w].bg;
            if (pixels[(size_t)y*width+x]!=expected)
                fprintf(stderr,"native resize step=%zu pixel=%u,%u got=%08x expected=%08x\n",
                    step,x,y,pixels[(size_t)y*width+x],expected);
            CHECK(pixels[(size_t)y*width+x]==expected);
        }
    }
    puts("render native resize: PASS (grow/shrink, changed cells, fractional margins, native window pixels)");
    return 0;
}
static int render_native_close_contract(render_backend *b,const render_native_lane *lane)
{
    render_cell cells[16]; uint64_t dirty[1]; render_grid grid;
    for (size_t i=0;i<16;i++) cells[i]=(render_cell){0,RENDER_NO_SLOT,0,0x345678,0,0};
    CHECK(render_grid_init(&grid,b->config.dims,cells,16,dirty,1)==RENDER_OK);
    CHECK(render_frame_begin(&grid,20)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
    render_strip strip={0,grid.dims.rows};
    CHECK(render_backend_submit(b,&grid,&strip,1)==RENDER_OK);
    CHECK(render_backend_present(b,20)==RENDER_OK && b->active);
    CHECK(render_backend_resize(b,b->config.dims)==RENDER_ERR_BUSY);
    CHECK(lane->close(lane->user,false)==0 && b->active); /* WM close request */
    CHECK(lane->close(lane->user,true)==0 && b->active);  /* native destroy */
    render_backend_shutdown(b); render_backend_shutdown(b);
    CHECK(!b->initialized && render_backend_present(b,20)==RENDER_ERR_STATE);
    puts("render native close: PASS (WM close/destroy pending, quiescent cleanup, idempotence)");
    return 0;
}
#endif

#ifndef RENDER_TEST_EXTERNAL
/* P2-1 section 38 diagnostic: the required product change belongs to GL,
 * outside this worker's source edit set. Exercise the real lease and resize
 * functions without a display/context or fabricated native completion. */
#define render_gl_backend lease_review_factory
#define gl_present_complete lease_review_present_complete
#define gl_completion_status lease_review_completion_status
#define gl_buffer_mode lease_review_buffer_mode
#define gl_device_name lease_review_device_name
#define gl_displayed_msc lease_review_displayed_msc
#define gl_read_pixels lease_review_read_pixels
#define gl_cells_acquire lease_review_cells_acquire
#define gl_cells_submit lease_review_cells_submit
#include "../src/gl/gl.c"
#undef render_gl_backend
#undef gl_present_complete
#undef gl_completion_status
#undef gl_buffer_mode
#undef gl_device_name
#undef gl_displayed_msc
#undef gl_read_pixels
#undef gl_cells_acquire
#undef gl_cells_submit
static int lease_resize_gate(void)
{
    render_cell slots[4], caller[4]; uint64_t dirty=0; render_grid grid;
    gl_state state={.upload=GL_UPLOAD_PERSISTENT,.dims={2,2,4,4},.cell_bytes=sizeof slots};
    state.ring[0].cells=slots;
    render_backend b={0}; CHECK(lease_review_factory(&b)==RENDER_OK);
    b.state=&state; b.initialized=true;
    b.config=(render_config){.dims=state.dims,.max_cells=4,.max_width=16,.max_height=16};
    CHECK(render_grid_init(&grid,state.dims,caller,4,&dirty,1)==RENDER_OK);
    CHECK(lease_review_cells_acquire(&b,&grid,false)==RENDER_OK && state.leased);
    int rc=render_backend_resize(&b,(render_dims){1,2,4,4});
    printf("P2-1 section 38: resize during mapped lease=%d (BUSY=%d) leased=%d\n",rc,RENDER_ERR_BUSY,state.leased);
    CHECK(rc==RENDER_ERR_BUSY && grid.cells==slots && state.dims.cols==2);
    return 0;
}
int main(int argc,char **argv)
{
    if (argc>2 && !strcmp(argv[1],"--review") && !strcmp(argv[2],"P2-1-38-gate")) return lease_resize_gate();
#else
int main(void)
{
#endif
    trace_init(); CHECK(trace_thread_register() >= 0);
    CHECK(strips_test() == 0); CHECK(arguments_test() == 0); CHECK(cells_test() == 0);
    CHECK(backend_test() == 0); CHECK(async_test() == 0); CHECK(trace_test() == 0);
    printf("render_test: PASS (strips, bounds, cells, lifecycle, ownership, hooks, allocator)\n");
    return 0;
}
