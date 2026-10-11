/* edit-a6v: headless startup/resize, including a pre-DPI session journal.
 * (P/E) fixture inputs are the bead's panel and m3g's launch policy:
 * width = floor(2880*.60) = 1728, height = floor(1800*.70) = 1260.
 * Grid extents are floor(window pixels / atlas cell pixels). No timed gate. */
#include "editor/editor.h"
#include "editor/runtime.h"
#include "base/base.h"
#include "journal/journal.h"
#include "work/work.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "window_size_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct frame_capture {
    editor *e;
    uint32_t width, height;
    render_dims first, last;
    bool submitted, mismatch;
} frame_capture;
static void submitted(void *ctx, const editor_frame *frame)
{
    frame_capture *capture = ctx;
    (void)frame;
    render_dims dims = editor_grid(capture->e)->dims;
    if (!capture->submitted) capture->first = dims;
    capture->submitted = true; capture->last = dims;
    if (dims.cols != capture->width / dims.cell_w ||
        dims.rows != capture->height / dims.cell_h) capture->mismatch = true;
}
static int settle(editor *e)
{
    do { T(editor_step(e, 0) >= 0); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int sized_window(const char *journal_path)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Exercise the CPU adapter's frame-capacity checks without opening X. */
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    frame_capture capture = {0};
    editor_runtime_window(2880, 1800, &capture.width, &capture.height);
    editor_config cfg = {.font_dpi = 243.0, .window_width = capture.width,
        .window_height = capture.height, .max_window_width = 2880, .max_window_height = 1800,
        .journal_path = journal_path, .on_submit = submitted, .hook_ctx = &capture};
    T(editor_open(&capture.e, &cfg, &backend) == 0);
    render_dims dims = editor_grid(capture.e)->dims;
    T(dims.cols == cfg.window_width / dims.cell_w);
    T(dims.rows == cfg.window_height / dims.cell_h);
    T(backend.config.dims.cols == dims.cols && backend.config.dims.rows == dims.rows);
    T(backend.config.max_width >= cfg.window_width && backend.config.max_height >= cfg.window_height);
    T(settle(capture.e) == 0);
    T(capture.submitted && !capture.mismatch);
    T(capture.first.cols == dims.cols && capture.first.rows == dims.rows);
    T(capture.last.cols * dims.cell_w == (cfg.window_width / dims.cell_w) * dims.cell_w);
    T(capture.last.rows * dims.cell_h == (cfg.window_height / dims.cell_h) * dims.cell_h);
    if (journal_path) {
        uint8_t bytes[sizeof "restored" - 1];
        T(editor_length(capture.e) == sizeof bytes);
        T(editor_read(capture.e, 0, bytes, sizeof bytes) == 0);
        T(!memcmp(bytes, "restored", sizeof bytes));
    }
    /* ConfigureNotify enters through the same PLAT_EV_RESIZE path. The
     * enlarged fixture is the supplied work area, already reserved at INIT. */
    capture.width = cfg.max_window_width; capture.height = cfg.max_window_height;
    plat_event resize = {.kind = PLAT_EV_RESIZE, .w = capture.width, .h = capture.height};
    T(editor_inject(capture.e, &resize) == 0);
    edit_malloc_guard_begin(); T(settle(capture.e) == 0);
    size_t allocations = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) T(allocations == 0); /* (G) law 2 */
    /* A queued old-size frame may retire before resize; the settled frame
     * and backend must both use the incoming native dimensions. */
    T(capture.last.cols == capture.width / dims.cell_w && capture.last.rows == capture.height / dims.cell_h);
    T(backend.config.dims.cols == capture.last.cols && backend.config.dims.rows == capture.last.rows);
    editor_close(capture.e);
    return 0;
}
/* edit-457.8 WINDOW v2: copy an editor-written journal, rewriting every WINDOW
 * record as the legacy 8-byte schema (no chosen flag) or as a 12-byte chosen
 * record of the given size. Fixture arrays bound a short single-tab session. */
typedef struct rewrite {
    journal_record records[64];
    uint8_t bytes[1u << 16];
    size_t count, used;
    bool overflow;
} rewrite;
static int keep(void *ctx, const journal_record *r)
{
    rewrite *w = ctx;
    if (w->count == 64 || r->size > sizeof w->bytes - w->used) { w->overflow = true; return 1; }
    memcpy(w->bytes + w->used, r->data, r->size);
    w->records[w->count] = *r; w->records[w->count].data = w->bytes + w->used;
    w->used += r->size; w->count++;
    return 0;
}
static void put32le(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int rewrite_window(const char *from, const char *to, bool chosen, uint32_t width, uint32_t height)
{
    static rewrite w; w = (rewrite){0};
    journal_replay_result rr; T(journal_replay_file(from, keep, &w, &rr) == 0 && !rr.corrupt && !w.overflow);
    static uint8_t window[JOURNAL_WINDOW_BYTES];
    put32le(window, width); put32le(window + 4, height); put32le(window + 8, JOURNAL_WINDOW_CHOSEN);
    size_t windows = 0;
    for (size_t i = 0; i < w.count; i++) if (w.records[i].type == JOURNAL_WINDOW) {
        w.records[i].data = window; w.records[i].size = chosen ? JOURNAL_WINDOW_BYTES : JOURNAL_WINDOW_LEGACY_BYTES;
        windows++;
    }
    T(windows);
    work_pool pool; T(work_pool_init(&pool, 1, 0) == 0);
    journal *j = NULL; T(journal_open(&j, to, &pool, NULL) == 0);
    T(journal_rotate(j, w.records, w.count) == 0 && journal_flush(j) == 0);
    journal_close(j); work_pool_shutdown(&pool);
    return 0;
}
/* Native startup passes its DPI-derived size as a default. A legacy record
 * keeps that default; a chosen v2 record restores its own size. */
static int restored_window(const char *journal_path, uint32_t width, uint32_t height)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    frame_capture capture = {.width = width, .height = height};
    uint32_t launch_w, launch_h; editor_runtime_window(2880, 1800, &launch_w, &launch_h);
    editor_config cfg = {.font_dpi = 243.0, .window_width = launch_w, .window_height = launch_h,
        .window_size_default = true, .max_window_width = 2880, .max_window_height = 1800,
        .journal_path = journal_path, .on_submit = submitted, .hook_ctx = &capture};
    T(editor_open(&capture.e, &cfg, &backend) == 0);
    render_dims dims = editor_grid(capture.e)->dims;
    T(dims.cols == width / dims.cell_w && dims.rows == height / dims.cell_h);
    T(backend.config.dims.cols == dims.cols && backend.config.dims.rows == dims.rows);
    T(settle(capture.e) == 0);
    T(capture.submitted && !capture.mismatch);
    uint8_t bytes[sizeof "restored" - 1];
    T(editor_length(capture.e) == sizeof bytes && editor_read(capture.e, 0, bytes, sizeof bytes) == 0);
    T(!memcmp(bytes, "restored", sizeof bytes));
    editor_close(capture.e);
    return 0;
}
int main(void)
{
    T(sized_window(NULL) == 0);
    puts("window_size_test: configured initial grid, submitted frame and resize passed");
    char root[] = "build/window-size-XXXXXX"; T(mkdtemp(root));
    char path[sizeof root + sizeof "/journal"], lock[sizeof root + sizeof "/.journal.lock"];
    T(snprintf(path, sizeof path, "%s/journal", root) > 0);
    T(snprintf(lock, sizeof lock, "%s/.journal.lock", root) > 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Legacy defaults derive their pixel size from cols*cell_w, rows*cell_h.
     * Journal that size, then reopen at the bead's display-derived geometry. */
    editor_config cfg = {.journal_path = path, .initial = (const uint8_t *)"restored",
        .initial_len = sizeof "restored" - 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    editor_close(e);
    T(sized_window(path) == 0);
    puts("window_size_test: recovered legacy geometry preserves configured surface and submitted frame size");
    /* (G, fixture) a chosen size inside the reserved work area and distinct
     * from the DPI default on both axes; same pixels journal_test uses. */
    uint32_t launch_w, launch_h; editor_runtime_window(2880, 1800, &launch_w, &launch_h);
    char legacy[sizeof root + sizeof "/legacy"], chosen[sizeof root + sizeof "/chosen"];
    char legacy_lock[sizeof root + sizeof "/.legacy.lock"], chosen_lock[sizeof root + sizeof "/.chosen.lock"];
    T(snprintf(legacy, sizeof legacy, "%s/legacy", root) > 0 && snprintf(chosen, sizeof chosen, "%s/chosen", root) > 0);
    T(snprintf(legacy_lock, sizeof legacy_lock, "%s/.legacy.lock", root) > 0);
    T(snprintf(chosen_lock, sizeof chosen_lock, "%s/.chosen.lock", root) > 0);
    T(rewrite_window(path, legacy, false, 1373, 911) == 0);
    T(restored_window(legacy, launch_w, launch_h) == 0);
    puts("window_size_test: legacy 8-byte WINDOW keeps the DPI default");
    T(rewrite_window(path, chosen, true, 1373, 911) == 0);
    T(restored_window(chosen, 1373, 911) == 0);
    puts("window_size_test: chosen 12-byte WINDOW restores over the DPI default");
    T(unlink(path) == 0); T(unlink(lock) == 0);
    T(unlink(legacy) == 0); T(unlink(chosen) == 0);
    (void)unlink(legacy_lock); (void)unlink(chosen_lock);
    T(rmdir(root) == 0);
    return 0;
}
