/* edit-a6v: headless startup/resize, including a pre-DPI session journal.
 * (P/E) fixture inputs are the bead's panel and m3g's launch policy:
 * width = floor(2880*.60) = 1728, height = floor(1800*.70) = 1260.
 * Grid extents are floor(window pixels / atlas cell pixels). No timed gate. */
#include "editor/editor.h"
#include "editor/runtime.h"
#include "base/base.h"
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
    T(unlink(path) == 0); T(unlink(lock) == 0); T(rmdir(root) == 0);
    puts("window_size_test: recovered legacy geometry preserves configured surface and submitted frame size");
    return 0;
}
