#include "editor/editor.h"
#include "base/base.h"
#include "editor/private.h"
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* (G) Reuse the public editor fixture and retry bound in
 * scroll_test.c:editor_observability/settle_editor; these are fixture controls,
 * not new timing budgets. LF width below is character + newline = 2 bytes. */
#define PROBE_BYTES 400u
#define PROBE_COLS 32u
#define PROBE_ROWS 8u
#define PROBE_RETRIES 10000u
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "editor_scroll_test:%d: FAIL %s\n", __LINE__, #c); result = 1; goto done; } } while (0)
typedef struct observer { editor_frame last; editor_refresh last_refresh; size_t frames, refreshes; } observer;
static void submitted(void *ctx, const editor_frame *frame)
{ observer *o = ctx; o->last = *frame; o->frames++; }
static void refreshed(void *ctx, const editor_refresh *refresh)
{ observer *o = ctx; o->last_refresh = *refresh; o->refreshes++; }
static int delayed_present(render_backend *backend, uint32_t id)
{ (void)backend; (void)id; return 0; }
static int settle(editor *e)
{
    /* Existing scroll integration probe's retry bound, not a latency budget. */
    for (unsigned i = 0; i < PROBE_RETRIES; i++) {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) return rc;
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    }
    return EDITOR_MORE;
}
typedef struct held_source { piece_snapshot *snap; _Atomic bool entered, release; } held_source;
static size_t held_span(void *ctx, uint64_t off, const uint8_t **out)
{
    held_source *h = ctx;
    atomic_store(&h->entered, true);
    while (!atomic_load(&h->release)) sched_yield();
    piece_iter it; size_t n = 0; piece_iter_begin_snapshot(&it, h->snap, off);
    return piece_iter_next(&it, out, &n) ? n : 0;
}
static void held_job(work_ctx *job)
{
    held_source *h = job->arg; atomic_store(&h->entered, true);
    while (!atomic_load(&h->release)) sched_yield();
}
static int eof_typing(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; held_source hold = {0};
    editor_config cfg = {.cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    work_handle handle = work_submit(&e->pool, (work_job){held_job, &hold, 0, WORK_BULK});
    CHECK(handle.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    uint64_t baseline = backend.stats.submitted_frames;
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
    edit_malloc_guard_begin();
    int rc = editor_inject(e, &event); if (!rc) rc = settle(e);
    size_t allocations = edit_malloc_guard_end();
    CHECK(rc == 0 && backend.stats.submitted_frames > baseline);
    CHECK(editor_length(e) == 1 && editor_view(e).selection.cursor == 1);
    CHECK(!edit_malloc_guard_active() || allocations == 0);
    puts("editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS");
done:
    atomic_store(&hold.release, true); editor_close(e); return result;
}
static int cancellation(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; held_source hold = {0};
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .max_cols = PROBE_COLS, .max_rows = PROBE_ROWS, .wrap_mode = -1};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    editor_buffer *b = e->buffer;
    CHECK(!b->scroll_resident.generation);
    hold.snap = b->scroll_snapshot;
    b->scroll_resident.snapshot.ctx = &hold; b->scroll_resident.snapshot.span = held_span;
    uint64_t submitted_frames = backend.stats.submitted_frames;
    /* Travel past the prepared rows so this test requires a worker window. */
    plat_event event = {.kind = PLAT_EV_WHEEL, .dy = (int32_t)(SCROLL_WHEEL_UNIT * cfg.rows)};
    CHECK(editor_inject(e, &event) == 0);
    for (unsigned retry = 0; retry < PROBE_RETRIES && !atomic_load(&hold.entered); retry++) {
        CHECK(editor_step(e, 0) >= 0); sched_yield();
    }
    CHECK(atomic_load(&hold.entered) && editor_get_stats(e).pending);
    CHECK(editor_view(e).first_byte == 0 && backend.stats.submitted_frames == submitted_frames);
    event = (plat_event){.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
    edit_malloc_guard_begin();
    int rc = editor_inject(e, &event); if (!rc) rc = settle(e);
    size_t allocations = edit_malloc_guard_end();
    CHECK(rc == 0 && (!edit_malloc_guard_active() || allocations == 0));
    CHECK(b->scroll_snapshot == hold.snap && b->scroll_resident.closing);
    uint8_t live = 0, old = 0;
    CHECK(editor_read(e, 0, &live, 1) == 0 && live == 'x');
    CHECK(piece_snapshot_read(hold.snap, 0, &old, 1) == 0 && old == 'a');
    atomic_store(&hold.release, true);
    for (unsigned retry = 0; retry < PROBE_RETRIES && b->scroll_snapshot == hold.snap && b->scroll_revision != b->revision; retry++) {
        CHECK(editor_step(e, 0) >= 0); sched_yield();
    }
    /* Piece's arena may reuse the physically retired snapshot header. Its
     * revision/bridge lease, rather than pointer inequality, proves renewal. */
    CHECK(b->scroll_snapshot != hold.snap || (b->scroll_revision == b->revision && !b->scroll_resident.closing));
    CHECK(editor_view(e).first_byte == 0 && editor_view(e).selection.cursor == 1);
    printf("editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=%zu guard=%s PASS\n",
        allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
done:
    atomic_store(&hold.release, true); editor_close(e); return result;
}
static int resized(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; observer o = {0};
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1, .hook_ctx = &o, .on_submit = submitted};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    plat_event event = {.kind = PLAT_EV_WHEEL, .dy = 1};
    CHECK(editor_inject(e, &event) == 0 && settle(e) == 0);
    int64_t origin = o.last.scroll.origin_y_q8;
    /* Halve both fixture dimensions: the text clip and sidebar policy change. */
    event = (plat_event){.kind = PLAT_EV_RESIZE,
        .w = cfg.cols / 2u * editor_grid(e)->dims.cell_w, .h = cfg.rows / 2u * editor_grid(e)->dims.cell_h};
    CHECK(editor_inject(e, &event) == 0 && settle(e) == 0);
    CHECK(o.last.scroll.origin_y_q8 == origin && o.last.scroll.layout_rows == editor_grid(e)->dims.rows);
    CHECK(render_grid_validate(editor_grid(e)) == 0);
    puts("editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS");
done:
    editor_close(e); return result;
}
int main(void)
{
    int result = 0;
    editor *e = NULL; render_backend backend = {0}; observer o = {0};
    /* Fixture: one ASCII character plus LF per line; enough rows for wheel
     * movement away from either endpoint. Dimensions reuse scroll_test's
     * public-editor probe, including its fixed tab row and sidebar. */
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1, .hook_ctx = &o,
        .on_submit = submitted, .on_refresh = refreshed};
    CHECK(render_null_backend(&backend) == 0);
    CHECK(editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    view_selection selection = editor_view(e).selection;
    size_t baseline = o.frames;
    plat_event wheel = {.kind = PLAT_EV_WHEEL, .dy = 1, .smooth = true};
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(o.frames > baseline);
    CHECK(o.last.scroll.origin_y_q8 == -(int64_t)(SCROLL_CORE_ROWS * editor_grid(e)->dims.cell_h));
    CHECK(o.last.scroll.layout_rows == cfg.rows); /* visible rows = surface minus tab; plus overscan */
    const render_grid *grid = editor_grid(e);
    CHECK(grid->region.pixels && grid->region.x == grid->scroll.cols * grid->dims.cell_w);
    CHECK(grid->region.y == grid->scroll.first_row * grid->dims.cell_h);
    CHECK(grid->region.height == grid->scroll.rows * grid->dims.cell_h);
    CHECK(render_grid_validate(grid) == 0);
    CHECK(o.refreshes == o.frames && o.last_refresh.synthetic && o.last_refresh.metadata_valid);
    CHECK(o.last_refresh.displayed_frame_id == o.last.id);
    CHECK(o.last_refresh.frame.scroll.origin_y_q8 == o.last.scroll.origin_y_q8);
    CHECK(o.last_refresh.frame.present_ns && o.last_refresh.completion_ns >= o.last_refresh.frame.present_ns);
    CHECK(editor_view(e).selection.cursor == selection.cursor && editor_view(e).selection.anchor == selection.anchor);
    wheel.dy = 256; /* plat/scroll's units per notch; three logical rows. */
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(editor_view(e).first_byte == 3u * 2u); /* notch rows * fixture line bytes */
    editor_frame last = o.last;
    uint64_t seq = o.last_refresh.refresh_sequence;
    CHECK(editor_on_refresh(e, last.id, seq + 1u, o.last_refresh.completion_ns, true) == 0);
    CHECK(o.last_refresh.frame.scroll.first_byte == last.scroll.first_byte);
    CHECK(editor_on_refresh(e, last.id, seq + 1u, o.last_refresh.completion_ns, true) == EDITOR_ERR_ARG);
    CHECK(editor_on_refresh(e, 0, seq + 2u, o.last_refresh.completion_ns, true) == 0);
    CHECK(!o.last_refresh.metadata_valid);
    backend.ops.present = delayed_present;
    wheel.dy = 1;
    CHECK(editor_inject(e, &wheel) == 0);
    for (unsigned retry = 0; retry < PROBE_RETRIES && !backend.active; retry++) CHECK(editor_step(e, 0) >= 0);
    CHECK(backend.active && backend.presented && backend.active_frame != last.id);
    uint32_t active = backend.active_frame;
    CHECK(editor_on_refresh(e, last.id, seq + 3u, o.last_refresh.completion_ns, true) == 0);
    CHECK(o.last_refresh.metadata_valid && o.last_refresh.frame.scroll.first_byte == last.scroll.first_byte);
    CHECK(backend.active && backend.active_frame == active);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_PRESENT_COMPLETE, last.id, 0) == RENDER_ERR_FRAME);
    CHECK(backend.active && backend.active_frame == active);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_DEVICE_DONE, active, 0) == 0);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_PRESENT_COMPLETE, active, 0) == 0 && settle(e) == 0);
    puts("editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS");
done:
    editor_close(e);
    if (!result) result = cancellation();
    if (!result) result = resized();
    if (!result) result = eof_typing();
    return result;
}
