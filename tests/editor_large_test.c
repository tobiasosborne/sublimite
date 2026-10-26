/* edit-457.10 / P4.10: large-file behavioural contract through the editor loop.
 * A ~1.6 MB generated fixture is forced into mmap mode (copy_threshold 4 KiB),
 * so the estimate -> exact swap, find, save and background-worker paths run in
 * `make check`. edit-9yd also reads the 1 GiB log corpus for gutter publication;
 * edit-2ty additionally asserts trace input-to-submit for the Astra near-end
 * navigation/typing sequence; broader rows live in bench/editor_large_bench.c. */
#include "select.h"
#include "editor/editor.h"
#include "editor/large.h"
#include "editor/private.h"
#include "editor/theme.h"
#include "config/config.h"
#include "raster/raster.h"
#include "base/base.h"
#include "trace/trace.h"
#include "trace/trace_fmt.h"
#include "file/file_test.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_large_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
#define A_LINES 20000u
#define B_LINES 1500u
#define B_LEN 1000u
#define TOTAL_LINES (A_LINES + B_LINES + 1u)
#define LOOSE_STEP_NS UINT64_C(250000000)   /* loose assert, loaded box */

typedef struct hooks {
    bool active, suspended;
    size_t allocations, frames;
    editor *observe;
    uint32_t estimated_fg, first_exact_frame;
    size_t estimated_frames, exact_frames;
    bool gutter_error;
    uint32_t submitted_id;
    uint64_t submit_ns;
    bool complete_on_ingress;
    _Atomic bool *index_release;
} hooks;
static void ingress(void *c, uint64_t s, uint64_t n)
{
    hooks *h = c; (void)s; (void)n;
    if (h->complete_on_ingress) {
        h->complete_on_ingress = false;
        atomic_store(h->index_release, true);
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
        while (!lineidx_complete(h->observe->buffer->index) && trace_now_ns() < deadline) {
            (void)lineidx_poll(h->observe->buffer->index);
            nanosleep(&(struct timespec){0, 100000}, NULL);
        }
    }
    if (!h->active) { edit_malloc_guard_begin(); h->active = true; }
}
static void submitted(void *c, const editor_frame *f)
{
    hooks *h = c;
    if (h->active) { h->allocations += edit_malloc_guard_end(); h->active = false; }
    h->frames++;
    h->submitted_id = f->id; h->submit_ns = f->submit_ns;
    if (!h->observe) return;
    editor *e = h->observe;
    editor_large_status s = editor_large_status_get(e);
    bool estimated = !s.lines_exact || s.top_estimated;
    if (editor_index_complete(e) && estimated) h->gutter_error = true;
    uint32_t fg = estimated ? h->estimated_fg : e->layout_cfg.gutter_fg;
    const render_grid *g = editor_grid(e);
    uint32_t gw = layout_gutter_width(&e->lay);
    size_t digits = 0;
    /* Inspect the cell dump at submit, including the first exact frame. */
    for (uint32_t r = e->tab_rows; r < g->dims.rows; r++) {
        for (uint32_t col = 0; col < gw; col++) {
            const render_cell *cell = &g->cells[(size_t)r * g->dims.cols + col];
            if (cell->glyph_index < '0' || cell->glyph_index > '9') continue;
            digits++;
            if (cell->fg != fg || cell->bg != e->layout_cfg.gutter_bg || cell->attrs)
                h->gutter_error = true;
        }
    }
    if (!digits) h->gutter_error = true;
    if (estimated) h->estimated_frames++;
    else {
        if (!h->exact_frames) h->first_exact_frame = f->id;
        h->exact_frames++;
    }
}
static void io_boundary(void *c, bool entering)
{
    hooks *h = c;
    if (entering && h->active) { h->allocations += edit_malloc_guard_end(); h->active = false; h->suspended = true; }
    else if (!entering && h->suspended) { edit_malloc_guard_begin(); h->active = true; h->suspended = false; }
}
static int write_fixture(const char *path)
{
    FILE *f = fopen(path, "wb"); T(f);
    for (unsigned i = 0; i < A_LINES; i++) fprintf(f, "l%06u\n", i);          /* 8 bytes each */
    for (unsigned k = 0; k < B_LINES; k++) {
        char line[B_LEN]; memset(line, 'a' + (int)(k % 20), B_LEN - 1); line[B_LEN - 1] = '\n';
        if (k == 100 || k == 200 || k == 1000) memcpy(line + 5, "NEEDLE", 6);
        T(fwrite(line, 1, B_LEN, f) == B_LEN);
    }
    T(fwrite("end", 1, 3, f) == 3); T(fclose(f) == 0); return 0;
}
static int step_until(editor *e, bool (*done)(editor *), uint64_t budget_ns)
{
    uint64_t deadline = trace_now_ns() + budget_ns;
    while (!done(e)) {
        uint64_t t = trace_now_ns();
        int rc = editor_step(e, 5); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        uint64_t d = trace_now_ns() - t; T(d < LOOSE_STEP_NS);
        T(trace_now_ns() < deadline);
    }
    return 0;
}
static bool settled(editor *e) { editor_stats s = editor_get_stats(e); return !s.pending && !s.render_active; }
/* Estimated gutter digits in the default (dark) theme: gutter.foreground halved
 * towards gutter.background (0x4c525d before edit-29y2 moved the page to 0x222226). */
static uint32_t dark_estimated_fg(void)
{
    return editor_theme_estimated_gutter(config_theme_color(CONFIG_DARK, "gutter.foreground"),
        config_theme_color(CONFIG_DARK, "gutter.background"));
}
static bool find_done(editor *e) { return editor_large_status_get(e).find_done; }
static bool save_done(editor *e) { return editor_large_status_get(e).save_done; }
static bool all_done(editor *e) { editor_large_status s = editor_large_status_get(e); return s.lines_exact && s.find_done && s.warm_done; }

typedef struct mapped_preview_probe {
    editor *e;
    bool submitted, before_attachment, before_mapping, correct, queued;
    _Atomic bool release_mapping;
    piece_tree *full_tree;
} mapped_preview_probe;
static void mapped_hold_mapping(work_ctx *job)
{
    mapped_preview_probe *probe = job->arg;
    uint64_t deadline = trace_now_ns() + UINT64_C(1000000000);
    while (!atomic_load(&probe->release_mapping) && !work_should_stop(job) && trace_now_ns() < deadline)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
}
static void mapped_preview_submit(void *ctx, const editor_frame *frame)
{
    mapped_preview_probe *probe = ctx; (void)frame;
    editor *e = probe->e;
    if (!e || !e->buffer->file || probe->submitted) return;
    probe->submitted = true;
    probe->full_tree = e->buffer->tree;
    probe->before_attachment = e->buffer->pending_open && e->buffer->preview &&
        e->tree == e->buffer->preview && piece_len(e->buffer->tree) == 0;
    probe->before_mapping = !file_open_ready(e->buffer->file);
    atomic_store(&probe->release_mapping, true);
    const render_grid *grid = editor_grid(e);
    uint32_t gutter = layout_gutter_width(&e->lay);
    const render_cell *cell = &grid->cells[(size_t)e->tab_rows * grid->dims.cols + gutter];
    probe->correct = cell->glyph_index == '?' && (cell->attrs & RENDER_ATTR_INVERSE);
    plat_event arrow = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Right};
    probe->queued = editor_inject(e, &arrow) == 0 && e->queue_count == 1;
}
static int mapped_preview_first(void)
{
    const char *path = "/tmp/edit-corpus/sparse_10g.bin";
    struct stat st; T(stat(path, &st) == 0);
    T((uint64_t)st.st_size == UINT64_C(10) * 1024u * 1024u * 1024u);
    mapped_preview_probe probe = {0};
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL; /* independent acquisition lane */
    editor_config cfg = {.cols = 60, .rows = 8, .wrap_mode = -1,
        .hook_ctx = &probe, .on_submit = mapped_preview_submit};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    probe.e = e;
    work_handle held = work_submit(&e->pool, (work_job){mapped_hold_mapping, &probe, 0, WORK_BULK}); T(held.epoch);
    uint64_t id = 0; T(editor_add_buffer(e, path, NULL, 0, &id) == 0);
    /* Check at the submit boundary, rather than inferring order from timing. */
    if (!probe.submitted) T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(probe.submitted); T(probe.before_attachment); T(probe.correct); T(probe.queued);
    T(probe.before_mapping);
    /* Mapped startup still returns the stable full tree and prepared warm snapshot. */
    T(e->buffer->tree == probe.full_tree);
    T(editor_tab(e, tabs_active_index(&e->tabs))->buffer == probe.full_tree);
    T(!e->buffer->pending_open); T(!e->buffer->preview);
    T(editor_length(e) == (uint64_t)st.st_size);
    T(e->buffer->lg.warm && piece_snapshot_len(e->buffer->lg.warm->snap) == (uint64_t)st.st_size);
    T(e->queue_count == 1); /* Input waits until adoption, outside setup. */
    editor_close(e);
    puts("edit-dnc2 G5: correct mapped prefix submitted before full attachment; stable tree, queued input and prepared warm snapshot retained");
    return 0;
}

/* The file API currently exposes the full mapped source only after attachment.
 * Opt-in red for launching a full index once mapping is ready, before attach. */
static int index_before_attachment_gap(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.start_empty = true, .cols = 60, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    editor_buffer *prepared = NULL;
    T(editor_buffer_prepare(e, "/tmp/edit-corpus/sparse_10g.bin", NULL, 0, &prepared) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!file_open_ready(prepared->file)) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        T(!e->error); T(trace_now_ns() < deadline);
        nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    T(prepared->pending_open && piece_len(prepared->tree) == 0);
    T(lineidx_len(prepared->index) == file_size(prepared->file) && lineidx_building(prepared->index));
    editor_buffer_destroy(prepared); editor_close(e); return 0;
}

/* Driving the prefix frame inside mapped open must not admit another IPC open
 * that switches the active buffer before the caller receives its full tree. */
static int mapped_startup_ipc(void)
{
    char relative[] = "build/editor-dnc2-ipc-XXXXXX";
    T(mkdtemp(relative));
    char runtime[IPC_PATH_CAP]; T(realpath(relative, runtime));
    ipc_server server = {0}; T(ipc_server_init(&server, runtime) == IPC_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 60, .rows = 8, .wrap_mode = -1, .server = &server};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    uint8_t wire[1024]; size_t wire_size = 0;
    ipc_request request = {.cwd = runtime, .has_stdin = true,
        .stdin_data = (const uint8_t *)"later", .stdin_size = 5};
    T(ipc_wire_encode(&request, wire, sizeof wire, &wire_size) == IPC_OK);
    struct sockaddr_un address; socklen_t address_size = sizeof address;
    T(getsockname(server.listener, (struct sockaddr *)&address, &address_size) == 0);
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); T(client >= 0);
    T(connect(client, (struct sockaddr *)&address, address_size) == 0);
    T(write(client, wire, wire_size) == (ssize_t)wire_size);
    uint64_t id = 0;
    T(editor_add_buffer(e, "/tmp/edit-corpus/sparse_10g.bin", NULL, 0, &id) == 0);
    T(tabs_count(&e->tabs) == 2);
    T(tabs_active(&e->tabs)->id == id && e->tree == e->buffer->tree);
    T(editor_length(e) == UINT64_C(10) * 1024u * 1024u * 1024u);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (tabs_count(&e->tabs) != 3) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
    }
    T(editor_length(e) == 5);
    close(client); editor_close(e); ipc_server_fini(&server);
    char lock[128];
    (void)snprintf(lock, sizeof lock, "%s/sublimite-%lu.lock", relative, (unsigned long)getuid());
    T(unlink(lock) == 0); T(rmdir(relative) == 0);
    puts("edit-dnc2 startup: queued IPC open waits for mapped preview adoption and is admitted on a later turn");
    return 0;
}

/* Hold the bulk lane so the unindexed frame cannot race a fast index build. */
static void hold_index(work_ctx *c)
{
    _Atomic bool *release = c->arg;
    while (!atomic_load(release) && !work_should_stop(c))
        nanosleep(&(struct timespec){0, 1000000}, NULL);
}
static size_t gutter_snapshot_span(void *ctx, uint64_t off, const uint8_t **p)
{
    piece_iter it; size_t n = 0; piece_iter_begin_snapshot(&it, ctx, off);
    return piece_iter_next(&it, p, &n) ? n : 0;
}
static void gutter_snapshot_release(void *ctx) { piece_snapshot_release(ctx); }
/* A final index batch can be adopted while a frame is already in progress
 * (for example by minimap's bounded poll). Its submit must publish the exact
 * gutter in that same frame, without a content rescan or stale submission. */
static int index_arrives_during_frame(const char *path)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    hooks h = {.estimated_fg = dark_estimated_fg()};
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = -1,
        .copy_threshold = 4096, .hook_ctx = &h, .on_ingress = ingress,
        .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    _Atomic bool release; atomic_init(&release, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK}); T(held.epoch);
    lineidx_src source = {snap, editor_length(e), gutter_snapshot_span, gutter_snapshot_release};
    T(lineidx_build_start(e->buffer->index, &e->pool, &source) == 0);
    h.observe = e;
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    uint32_t previous = editor_grid(e)->frame_id;
    h.index_release = &release; h.complete_on_ingress = true;
    plat_event arrow = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Right};
    T(editor_inject(e, &arrow) == 0);
    int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
    T(editor_index_complete(e));
    T(editor_large_status_get(e).lines_exact);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_index_complete(e));
    T(!h.gutter_error);
    T(h.first_exact_frame == previous + 1u);
    T(h.allocations == 0);
    editor_close(e);
    puts("edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations");
    return 0;
}
/* edit-xzw: complete the index between scroll turns, after a foreground
 * resident window has already certified the byte anchor. (E) Geometry,
 * mmap threshold, colour and watchdog reuse index_arrives_during_frame;
 * no new duration/count bound. (P) Zero initializers are empty state and
 * return-code checks use the API's success value. */
static int index_completes_between_steps(const char *path)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    hooks h = {.estimated_fg = dark_estimated_fg()};
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = -1,
        .copy_threshold = 4096, .hook_ctx = &h, .on_ingress = ingress,
        .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index);
    /* The replacement index is incomplete even if open's index won its race. */
    e->buffer->lg.lines_exact = false;
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    _Atomic bool release; atomic_init(&release, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK}); T(held.epoch);
    lineidx_src source = {snap, editor_length(e), gutter_snapshot_span, gutter_snapshot_release};
    T(lineidx_build_start(e->buffer->index, &e->pool, &source) == 0);
    h.observe = e;
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    /* (P) Last B row starts after A_LINES fixed-width "l%06u\n" rows
     * (one l + six digits + LF = eight bytes) and B_LINES minus one B rows. */
    uint64_t top_byte = (uint64_t)A_LINES * 8u + (B_LINES - 1u) * B_LEN;
    T(editor_large_goto_byte(e, top_byte) == 0);
    T(editor_scroll_seek(e, top_byte) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_large_status_get(e).top_estimated);
    top_byte = editor_view(e).first_byte;
    /* EOF clipping can move the anchor back by the visible text rows. */
    lineidx_src cur = editor_source(e->buffer);
    uint64_t top_line = piece_byte_to_line(e->tree, top_byte);
    atomic_store(&release, true);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!lineidx_complete(e->buffer->index)) {
        (void)lineidx_poll(e->buffer->index);
        T(trace_now_ns() < deadline);
    }
    T(lineidx_byte_to_line(e->buffer->index, &cur, top_byte).value == top_line);
    editor_large_index_progress(e);
    T(editor_large_status_get(e).lines_exact);
    /* (P) Publication follows the last scroll slice in submit(); the next
     * editor turn adopts the resident relabel before its frame is submitted.
     * A zero timeout pumps this already resident work without waiting. */
    int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
    T(!editor_large_status_get(e).top_estimated);
    T(editor_view(e).first_byte == top_byte);
    T(editor_view(e).first_line == top_line);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(h.exact_frames); T(!h.gutter_error); T(!h.allocations);
    editor_close(e);
    puts("edit-xzw: index completed between steps; exact gutter retains byte anchor");
    return 0;
}
static int gutter_estimates(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    hooks h = {.estimated_fg = dark_estimated_fg()};
    editor_config cfg = {.cols = 60, .rows = 8, .wrap_mode = -1,
        .hook_ctx = &h, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    uint64_t id = 0;
    T(editor_add_buffer(e, "/tmp/edit-corpus/log_1g.txt", NULL, 0, &id) == 0);
    T(editor_length(e) == UINT64_C(1073741824));
    T(editor_large_status_get(e).mapped); T(!editor_large_status_get(e).lines_exact);
    /* Open needs the bulk lane for mapping. Restart its index behind the hold
     * after acquisition, before any large-file frame can be submitted. */
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    _Atomic bool release; atomic_init(&release, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK});
    T(held.epoch);
    lineidx_src src = {snap, editor_length(e), gutter_snapshot_span, gutter_snapshot_release};
    T(lineidx_build_start(e->buffer->index, &e->pool, &src) == 0);
    h.observe = e;
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    const render_grid *g = editor_grid(e);
    uint32_t gw = layout_gutter_width(&e->lay);
    const render_cell *digit = &g->cells[(size_t)e->tab_rows * g->dims.cols + gw - 2u];
    T(digit->glyph_index == '1');
    T(digit->fg != e->layout_cfg.gutter_fg);
    T(h.estimated_frames == 1); T(!h.gutter_error);
    /* The dim style follows configured colours rather than a fixed palette. */
    e->layout_cfg.gutter_fg = 0xe0c080; e->layout_cfg.gutter_bg = 0x204060;
    e->lay.cfg.gutter_bg = e->layout_cfg.gutter_bg; h.estimated_fg = 0x808070;
    T(editor_full_layout(e) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(!h.gutter_error);
    h.allocations = 0;
    plat_event arrow = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Right};
    T(editor_inject(e, &arrow) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(h.allocations == 0); T(!h.gutter_error);
    T(editor_large_goto_byte(e, UINT64_C(536870912)) == 0);
    T(editor_large_status_get(e).top_estimated);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(!h.gutter_error); T(!h.exact_frames);
    /* Unfocus suppresses unrelated blink frames on a loaded index run. */
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    T(editor_inject(e, &focus) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(!h.gutter_error);
    uint32_t estimated_frame = g->frame_id;
    atomic_store(&release, true);
    uint64_t deadline = trace_now_ns() + UINT64_C(20000000000);
    while (!h.exact_frames) {
        int rc = editor_step(e, 5); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < deadline);
    }
    /* No stale estimated submission may slip between publication and exact. */
    T(editor_large_status_get(e).lines_exact);
    T(!editor_large_status_get(e).top_estimated);
    T(h.first_exact_frame == estimated_frame + 1u); T(!h.gutter_error);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    /* A caret-only frame retains the exact gutter without repainting it. */
    focus.focused = true;
    T(editor_inject(e, &focus) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(!h.gutter_error);
    h.observe = NULL;
    T(editor_select_tab(e, 0) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    h.allocations = 0;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_x, .utf8_len = 1, .utf8 = {'x'}};
    T(editor_inject(e, &key) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(h.allocations == 0);
    editor_close(e);
    puts("edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed");
    return 0;
}

/* Completion must survive backpressure in the shared work mailbox. */
static void fill_mailbox(work_ctx *c)
{
    _Atomic bool *full = c->arg;
    work_msg msg = {.kind = UINT32_C(0x45465400)};
    for (unsigned i = 0; i <= WORK_MAILBOX_CAP; i++) if (!work_publish(c, &msg)) break;
    atomic_store(full, true);
}
static void completion_marker(work_ctx *ctx)
{ atomic_store((_Atomic bool *)ctx->arg, true); }
static int mailbox_pressure(editor *e, const uint8_t *needle)
{
    _Atomic bool full; atomic_init(&full, false);
    work_handle h = work_submit(&e->pool, (work_job){fill_mailbox, &full, 0, WORK_BULK});
    T(h.epoch);
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (!atomic_load(&full)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 1000000}, NULL); }
    T(editor_large_find_begin(e, needle, 6) == 0);
    while (!atomic_load(&e->buffer->lg.find->released)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 1000000}, NULL); }
    _Atomic bool marked; atomic_init(&marked, false);
    work_handle marker = work_submit(&e->pool, (work_job){completion_marker, &marked, 0, WORK_BULK}); T(marker.epoch);
    /* Keep the mailbox saturated. A sealed publisher must yield the lane. */
    while (!work_handle_finished(&e->pool, marker)) {
        T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    T(atomic_load(&marked));
    T(step_until(e, find_done, UINT64_C(500000000)) == 0);
    T(editor_large_status_get(e).find_total == 2);
    while (!work_handle_finished(&e->pool, h)) nanosleep(&(struct timespec){0, 1000000}, NULL);
    return 0;
}

/* IPC may prepare several buffers before installing any in the tabs array. */
static int prepared_completion(editor *e)
{
    char path[] = "build/editor-large-prefix-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t bytes[8192]; memset(bytes, 'p', sizeof bytes); bytes[sizeof bytes - 1] = '\n';
    T(write(fd, bytes, sizeof bytes) == (ssize_t)sizeof bytes); close(fd);
    editor_buffer *prepared = NULL;
    T(editor_buffer_prepare(e, path, NULL, 0, &prepared) == 0);
    T(prepared->lg.mapped && prepared->lg.lines_exact);
    T(prepared->lg.warm_h.epoch);
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (!work_handle_finished(&e->pool, prepared->lg.warm_h) || work_mailbox_pending(&e->pool)) {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < deadline);
    }
    T(prepared->lg.warm_done);
    editor_buffer_destroy(prepared); unlink(path);
    return 0;
}

static int mapped_find_boundaries(void)
{
    char path[] = "build/editor-dnc-find-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    size_t length = 2u * FILE_PREFIX_MAX + 17u;
    uint8_t *bytes = malloc(length); T(bytes); memset(bytes, 'x', length);
    memcpy(bytes + LINEIDX_CHUNK - 3u, "abababababab", 12);
    memcpy(bytes + 2u * LINEIDX_CHUNK - 3u, "abababababab", 12);
    T(write(fd, bytes, length) == (ssize_t)length); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = -1, .copy_threshold = 4096};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    /* Leave a bound, unbuilt index with no worker. Find must finish anyway. */
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(length); T(e->buffer->index);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    uint8_t dense[256]; memset(dense, 'x', sizeof dense);
    const uint8_t *needles[] = {(const uint8_t *)"abab", (const uint8_t *)"x", dense};
    const size_t sizes[] = {4, 1, sizeof dense};
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        find_source source = {.bytes = bytes, .length = length}; find_result expected;
        T(find_literal(&source, needles[k], sizes[k], NULL, &expected) == FIND_OK);
        T(editor_large_find_begin(e, needles[k], sizes[k]) == 0);
        T(step_until(e, find_done, UINT64_C(5000000000)) == 0);
        editor_large_status status = editor_large_status_get(e);
        T(status.find_rc == FIND_OK); T(status.find_total == expected.total);
        T(status.find_stored == expected.stored);
        T(memcmp(e->buffer->lg.find->result.offsets, expected.offsets, expected.stored * sizeof expected.offsets[0]) == 0);
        T(!editor_index_complete(e)); T(!status.lines_exact);
    }
    const uint8_t *fault_bytes = NULL; size_t fault_length = 0;
    T(file_snapshot_bytes(snap, NULL, &fault_bytes, &fault_length) == 1);
    T(fault_length == length);
    T(file_test_recover(fault_bytes + 4096u) == 1);
    T(file_snapshot_bytes(snap, NULL, &fault_bytes, &fault_length) == 0);
    T(!fault_bytes && !fault_length);
    T(editor_large_find_begin(e, needles[0], sizes[0]) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!work_handle_finished(&e->pool, e->buffer->lg.find_h)) {
        T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    T(e->buffer->lg.find->rc == FIND_CANCELLED);
    T(e->buffer->lg.find->result.total == 0 && e->buffer->lg.find->result.stored == 0);
    piece_snapshot_release(snap); editor_close(e); free(bytes); unlink(path);
    puts("edit-dnc G6: mapped find before index, window crossings/nonoverlap, dense saturation, bounded offsets and fault refusal passed");
    return 0;
}

/* A cancelled lease may still be inside its callback after Keep. */
typedef struct restart_probe {
    editor_large_find_job *job;
    _Atomic bool entered, release, valid;
} restart_probe;
static void held_find_argument(work_ctx *ctx)
{
    restart_probe *p = ctx->arg;
    atomic_store(&p->entered, true);
    while (!atomic_load(&p->release))
        nanosleep(&(struct timespec){0, 100000}, NULL);
    /* Deliberately finish an indivisible read after logical cancellation. */
    atomic_store(&p->valid, piece_snapshot_len(p->job->snap) == sizeof "xsource\n" - 1);
    p->job->rc = FIND_CANCELLED;
}
static int cancelled_find_restart(void)
{
    char path[] = "build/editor-find-restart-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "source\n", 7) == 7); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    plat_event edit = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
    T(editor_inject(e, &edit) == 0); T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_tab(e, 0)->modified); /* Keep requires an unsaved buffer. */
    editor_large *lg = &e->buffer->lg;
    restart_probe probe = {0};
    probe.job = calloc(1, sizeof *probe.job); T(probe.job);
    probe.job->snap = piece_snapshot_take(e->tree); T(probe.job->snap);
    atomic_init(&probe.job->released, false);
    lg->find = probe.job; lg->find_gen = 1; lg->find_running = true;
    lg->find_h = work_submit(&e->pool, (work_job){held_find_argument, &probe, 1, WORK_BULK});
    T(lg->find_h.epoch); work_handle old = lg->find_h;
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!atomic_load(&probe.entered)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 100000}, NULL); }
    fd = open(path, O_WRONLY | O_TRUNC); T(fd >= 0);
    T(write(fd, "change\n", 7) == 7); close(fd);
    /* Watch identity work shares bulk; focus validation supplies the change. */
    T(file_check(e->buffer->file, NULL) != 0);
    T(editor_check_sources(e) == 0); T(e->buffer->source_stale);
    T(editor_source_resolve(e, false) == 0); T(editor_check_sources(e) == 0);
    T(!e->buffer->source_stale); T(!work_handle_finished(&e->pool, old));
    int rc = editor_large_find_begin(e, (const uint8_t *)"source", 6);
    atomic_store(&probe.release, true);
    T(rc == 0);
    while (!work_handle_finished(&e->pool, old)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 100000}, NULL); }
    T(atomic_load(&probe.valid));
    T(step_until(e, find_done, UINT64_C(5000000000)) == 0);
    T(editor_large_status_get(e).find_total == 1);
    editor_close(e); unlink(path);
    puts("P4-3 section 1: held cancelled find survives Keep and restart passed");
    return 0;
}

static int keep_restarts_background(void)
{
    char path[] = "build/editor-keep-background-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t bytes[8192]; memset(bytes, 'x', sizeof bytes); bytes[8191] = '\n';
    T(write(fd, bytes, sizeof bytes) == (ssize_t)sizeof bytes); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .wrap_mode = -1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    plat_event edit = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
    T(editor_inject(e, &edit) == 0); T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_tab(e, 0)->modified); /* Keep requires an unsaved buffer. */
    /* Typing invalidates the index; restore the original fixture's completed
     * index before expecting editor_large_start to enqueue its warm job. */
    uint64_t indexed = trace_now_ns() + UINT64_C(5000000000); /* existing settle watchdog */
    while (!editor_index_complete(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < indexed); }
    editor_large_close(e->buffer);
    e->buffer->lg.warm_h = (work_handle){0}; e->buffer->lg.warm_done = false;
    _Atomic bool release; atomic_init(&release, false);
    work_handle hold = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK}); T(hold.epoch);
    editor_large_start(e, e->buffer); T(e->buffer->lg.warm_h.epoch);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index);
    T(lineidx_build_reserve(e->buffer->index) == 0);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    lineidx_src src = {snap, piece_snapshot_len(snap), gutter_snapshot_span, gutter_snapshot_release};
    T(lineidx_build_start_prepaid(e->buffer->index, &e->pool, &src) == 0);
    e->buffer->lg.lines_exact = false; e->buffer->index_dirty = false;
    char replacement[] = "build/editor-keep-new-XXXXXX";
    fd = mkstemp(replacement); T(fd >= 0); T(write(fd, "new\n", 4) == 4); close(fd);
    T(rename(replacement, path) == 0); T(file_check(e->buffer->file, NULL) == 1);
    T(editor_check_sources(e) == 0); T(e->buffer->source_stale);
    T(editor_source_resolve(e, false) == 0); T(editor_check_sources(e) == 0);
    T(!e->buffer->source_stale);
    bool dirty = e->buffer->index_dirty;
    atomic_store(&release, true);
    T(dirty);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!editor_index_complete(e) || !editor_large_status_get(e).warm_done) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
    }
    T(editor_line_count(e) == 2); editor_close(e); unlink(path);
    puts("P4-3 section 15: Keep restarts incomplete index and cancelled warm lease passed");
    return 0;
}

static int explicit_coordinates(void)
{
    for (unsigned mapped = 0; mapped < 2; mapped++) {
        char path[] = "build/editor-position-XXXXXX";
        int fd = mkstemp(path); T(fd >= 0);
        size_t length = 2u * FILE_PREFIX_MAX + 200u;
        uint8_t *bytes = malloc(length); T(bytes); memset(bytes, 'x', length);
        for (size_t i = FILE_PREFIX_MAX; i < length; i += 10u) bytes[i] = '\n';
        T(write(fd, bytes, length) == (ssize_t)length); close(fd);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.cols = 40, .rows = 4, .wrap_mode = -1,
            .copy_threshold = mapped ? 1u : length + 1u};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        ipc_request request = {.count = 1};
        request.paths[0] = (ipc_path){.path = path, .line = 100, .col = 3};
        T(editor_open_request(e, &request, 0) == 0);
        T(e->buffer->lines > editor_view(e).first_line);
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        T(editor_view(e).selection.cursor == FILE_PREFIX_MAX + 981u + 2u);
        T(editor_view(e).first_line == 99);
        editor_close(e); free(bytes); unlink(path);
    }
    puts("P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed");
    return 0;
}

static int wrapped_placement(void)
{
    /* (P) Certified logical start after "a\n". Its density label is
     * deliberately the byte length, above its actual zero-based line.
     * The wrapped viewport owns visual bytes, never logical scroll rows. */
    const uint8_t prefix[] = "a\n";
    editor_buffer buffer = {0};
    editor e = {.buffer = &buffer};
    e.v.state.wrap = true;
    e.v.state.visual_end = true;
    e.v.state.hscroll = sizeof prefix - 1u;
    scroll_state before = buffer.scroll;
    T(editor_scroll_place(&e, sizeof prefix - 1u, sizeof prefix - 1u, true));
    T(e.v.state.first_byte == sizeof prefix - 1u);
    T(e.v.state.visual_byte == e.v.state.first_byte);
    T(e.v.state.first_line == sizeof prefix - 1u && e.v.state.approximate);
    T(!e.v.state.visual_end && !e.v.state.hscroll);
    T(memcmp(&buffer.scroll, &before, sizeof before) == 0);
    T(editor_scroll_place(&e, sizeof prefix - 1u, 1u, false));
    T(e.v.state.first_line == 1u && !e.v.state.approximate);
    T(e.v.state.first_byte == sizeof prefix - 1u && e.v.state.visual_byte == e.v.state.first_byte);
    T(memcmp(&buffer.scroll, &before, sizeof before) == 0);
    puts("editor_large_test: wrapped certified placement preserves estimated/exact label PASS");
    return 0;
}
static bool wrapped_index_done(editor *e)
{ return editor_large_status_get(e).lines_exact; }
static int wrapped_byte_jump(void)
{
    /* (E) Reuse gutter_estimates' viewport and watchdog, and write_fixture's
     * mixed-density bytes. (P) Jump at the next B-line start: A_LINES times
     * sizeof "l000000\n" minus its C terminator, plus B_LEN. B_LEN exceeds
     * the viewport columns, so this is a wrapped logical-line fixture.
     * (P) The smallest nonempty copy threshold forces mapped acquisition
     * to finish before the bulk hold, as gutter_estimates requires. */
    char path[] = "build/editor-tn2-wrap-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0); close(fd);
    T(write_fixture(path) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 60, .rows = 8, .wrap_mode = 1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    uint64_t id = 0;
    T(editor_add_buffer(e, path, NULL, 0, &id) == 0);
    T(!e->buffer->pending_open && editor_large_status_get(e).mapped);
    T(!editor_large_status_get(e).lines_exact);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(lineidx_bind_snapshot(e->buffer->index, snap) == 0);
    _Atomic bool release; atomic_init(&release, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK}); T(held.epoch);
    lineidx_src src = {snap, editor_length(e), gutter_snapshot_span, gutter_snapshot_release};
    T(lineidx_build_start(e->buffer->index, &e->pool, &src) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    uint64_t target = (uint64_t)A_LINES * (sizeof "l000000\n" - 1u) + B_LEN;
    T(editor_large_goto_byte(e, target) == 0);
    T(editor_view(e).wrap && editor_view(e).approximate);
    T(editor_large_status_get(e).top_estimated);
    T(editor_view(e).first_byte == target && editor_view(e).visual_byte == target);
    T(editor_view(e).selection.cursor == target && !view_busy(&e->v));
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(e->row_byte[0] == target && e->lay.wrap);
    T(e->lay.wrap_rows[0].line_start == target && e->lay.wrap_rows[0].next < target + B_LEN);
    atomic_store(&release, true);
    T(step_until(e, wrapped_index_done, UINT64_C(5000000000)) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_view(e).first_byte == target && editor_view(e).visual_byte == target);
    T(editor_view(e).first_line == A_LINES + 1u && !editor_large_status_get(e).top_estimated);
    T(!editor_view(e).approximate);
    editor_close(e); unlink(path);
    puts("editor_large_test: wrapped byte jump keeps certified anchor through index publication PASS");
    return 0;
}
static int byte_jump_boundaries(void)
{
    uint8_t *bytes = malloc(200000u); T(bytes); memset(bytes, 'a', 200000u);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = 200000u, .cols = 40, .rows = 4, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    int rc = editor_large_goto_byte(e, 150000u);
    T(rc == 0 || rc == EDITOR_MORE);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (rc == EDITOR_MORE) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
        rc = editor_large_goto_byte(e, 150000u);
    }
    T(rc == 0); T(editor_view(e).first_byte == 0);
    T(editor_view(e).selection.cursor == 150000u);
    editor_close(e); free(bytes);
    const uint8_t clusters[] = "a\xcc\x81" "\xe4\xb8\xad" "\r\n" "z";
    const uint64_t repaired[] = {0, 0, 0, 3, 3, 3, 6, 6, 8, 9};
    backend = (render_backend){0}; T(render_null_backend(&backend) == 0);
    cfg.initial = clusters; cfg.initial_len = sizeof clusters - 1u;
    T(editor_open(&e, &cfg, &backend) == 0);
    for (uint64_t at = 0; at < sizeof repaired / sizeof repaired[0]; at++) {
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        T(editor_large_goto_byte(e, at) == 0);
        T(editor_view(e).selection.cursor == repaired[at]);
    }
    editor_close(e);
    puts("P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed");
    return 0;
}

/* Only the paint operation is inside the fault census; workers are retired. */
static int cold_bracket_paint(bool wrapped_edit)
{
    char path[] = "build/editor-cold-paint-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t block[4096]; memset(block, 'a', sizeof block);
    for (size_t i = 0; i < sizeof block; i += 128u) { block[i] = '('; block[i + 1u] = ')'; block[i + 127u] = '\n'; }
    size_t length = 32u * 1024u * 1024u;
    for (size_t i = 0; i < length; i += sizeof block) T(write(fd, block, sizeof block) == (ssize_t)sizeof block);
    close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4,
        .wrap_mode = wrapped_edit ? 1 : -1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    editor_large_close(e->buffer); lineidx_build_cancel(e->buffer->index);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(length); T(e->buffer->index);
    uint64_t start = length - 4u * 128u;
    e->v.state.first_byte = e->v.state.visual_byte = start;
    e->v.state.first_line = start / 128u;
    e->v.state.selection.cursor = e->v.state.selection.anchor = start;
    e->buffer->lines = length / 128u + 1u;
    e->buffer->lg.warm_done = false;
    T(editor_full_layout(e) == 0);
    while (layout_busy(&e->lay)) T(layout_run(&e->lay) >= 0);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    const uint8_t *mapped = NULL; size_t n = 0;
    T(file_snapshot_bytes(snap, NULL, &mapped, &n) == 1 && n == length);
    T(madvise((void *)mapped, n, MADV_DONTNEED) == 0);
    struct rusage before, after; T(getrusage(RUSAGE_THREAD, &before) == 0);
    if (wrapped_edit) {
        plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_z, .utf8_len = 1, .utf8 = {'z'}};
        T(editor_handle_key(e, &key) == 0);
        while (e->action != EDITOR_ACTION_NONE || view_busy(&e->v)) T(editor_continue_action(e) == 0);
    } else { e->paint_ready = false; T(editor_paint_prepare(e) == 0); }
    T(getrusage(RUSAGE_THREAD, &after) == 0);
    long faults = after.ru_minflt - before.ru_minflt;
    printf("P4-3 section 20: cold deep bracket paint minor faults=%ld (M)[AC], loose bound=128 (G)\n", faults);
    T(faults < 128);
    if (wrapped_edit) T(editor_view(e).selection.cursor == start + 1u);
    else T(e->bracket_mate == start + 1u);
    piece_snapshot_release(snap); editor_close(e); unlink(path);
    return 0;
}

/* Underestimate from the review fixture: a newline-free prefix followed by
 * an equally sized all-newline suffix. Minimum lines = empty-file sentinel;
 * maximum lines = one newline per remaining byte plus the sentinel. */
static int estimated_newline_delete(bool wrapped)
{
    uint8_t bytes[200]; memset(bytes, 'a', 100); memset(bytes + 100, '\n', 100);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes, .cols = 40, .rows = 4,
        .wrap_mode = wrapped ? 1 : -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    e->buffer->lg.lines_exact = false; e->buffer->lg.lazy = true;
    e->buffer->lines = e->lay.line_count = 1;
    e->v.state.selection.cursor = 200; e->v.state.selection.anchor = 100;
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_BackSpace};
    T(editor_handle_key(e, &ev) == 0);
    printf("P4-3 section 19: post-delete estimated lines=%llu bytes=%llu (M)[AC]\n",
        (unsigned long long)editor_line_count(e), (unsigned long long)editor_length(e));
    T(editor_line_count(e) >= 1 && editor_line_count(e) <= editor_length(e) + 1u);
    T(e->lay.line_count >= 1 && e->lay.line_count <= editor_length(e) + 1u);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    ev.keysym = XKB_KEY_z; ev.mods = PLAT_MOD_CTRL;
    T(editor_handle_key(e, &ev) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_length(e) == sizeof bytes);
    T(editor_line_count(e) >= 1 && editor_line_count(e) <= sizeof bytes + 1u);
    T(e->lay.line_count >= 1 && e->lay.line_count <= sizeof bytes + 1u);
    ev.keysym = XKB_KEY_y;
    T(editor_handle_key(e, &ev) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(editor_length(e) == sizeof bytes / 2u);
    T(editor_line_count(e) >= 1 && editor_line_count(e) <= editor_length(e) + 1u);
    T(e->lay.line_count >= 1 && e->lay.line_count <= editor_length(e) + 1u);
    puts("P4-3 estimated newline delete/undo/redo: passed");
    editor_close(e); return 0;
}

static int warm_repaint(void)
{
    char path[] = "build/editor-warm-repaint-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t bytes[8192]; memset(bytes, 'a', sizeof bytes); bytes[8191] = '\n';
    T(write(fd, bytes, sizeof bytes) == (ssize_t)sizeof bytes); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .wrap_mode = -1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!e->buffer->lg.warm_done) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    e->buffer->lg.warm_done = false; e->full_pending = false;
    work_handle h = e->buffer->lg.warm_h; T(h.epoch);
    work_msg msg = {.kind = EDITOR_LARGE_MSG_WARM, .slot_ = h.slot, .epoch_ = h.epoch, .generation = 1};
    T(editor_large_receive(e, &msg)); editor_large_index_progress(e);
    T(e->buffer->lg.warm_done); T(e->full_pending);
    editor_close(e); unlink(path);
    puts("P4-3 section 21: warm completion requests an idle correcting frame passed"); return 0;
}
static int lazy_long_line_rows(void)
{
    char path[] = "build/editor-lazy-long-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    size_t n = 100000u; uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    T(write(fd, bytes, n) == (ssize_t)n); T(write(fd, "\nvisible\n", 9) == 9); close(fd); free(bytes);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .wrap_mode = -1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    editor_large_close(e->buffer); e->buffer->lg.warm_done = false;
    T(editor_full_layout(e) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(e->row_byte[1] == n + 1u);
    puts("P4-3 lazy first viewport following row: passed");
    editor_close(e); unlink(path); return 0;
}

typedef struct lane_probe { _Atomic bool *released; _Atomic bool observed; } lane_probe;
static void observe_lane(work_ctx *ctx)
{
    lane_probe *p = ctx->arg;
    atomic_store(&p->observed, !atomic_load(p->released));
}
static int cooperative_large_jobs(void)
{
    char path[] = "build/editor-large-lane-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t block[4096]; memset(block, 'a', sizeof block);
    for (size_t i = 0; i < 8u * 1024u * 1024u; i += sizeof block)
        T(write(fd, block, sizeof block) == (ssize_t)sizeof block);
    close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .wrap_mode = -1, .copy_threshold = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!e->buffer->lg.warm_done) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    for (unsigned mode = getenv("P4_WARM_LANE") ? 1u : 0u; mode < 2; mode++) {
        _Atomic bool release; atomic_init(&release, false);
        work_handle hold = work_submit(&e->pool, (work_job){hold_index, &release, 0, WORK_BULK}); T(hold.epoch);
        lane_probe p = {0};
        if (mode) {
            editor_large_close(e->buffer); e->buffer->lg.warm_h = (work_handle){0};
            e->buffer->lg.warm_done = false; editor_large_start(e, e->buffer);
            T(e->buffer->lg.warm_h.epoch); p.released = &e->buffer->lg.warm->released;
        } else {
            T(editor_large_find_begin(e, (const uint8_t *)"aa", 2) == 0);
            p.released = &e->buffer->lg.find->released;
        }
        work_handle marker = work_submit(&e->pool, (work_job){observe_lane, &p, 0, WORK_BULK}); T(marker.epoch);
        atomic_store(&release, true);
        deadline = trace_now_ns() + UINT64_C(5000000000);
        while (!work_handle_finished(&e->pool, marker)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
        T(atomic_load(&p.observed));
        while (!atomic_load(p.released)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    }
    editor_close(e); unlink(path);
    puts("P4-3 section 28: production find/warm yield bulk lane before completion passed"); return 0;
}

static int mailbox_yield(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    const uint8_t text[] = "NEEDLE\nNEEDLE\n";
    editor_config cfg = {.initial = text, .initial_len = sizeof text - 1u, .cols = 40, .rows = 4, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(mailbox_pressure(e, (const uint8_t *)"NEEDLE") == 0);
    editor_close(e);
    puts("P4-3 section 28: sealed find publication yields behind a saturated mailbox passed"); return 0;
}

/* edit-2ty: sequence/count from the Astra large-file repro in the bead.
 * Geometry and setup watchdog reuse mapped_startup_ipc's headless viewport
 * and step_until's existing watchdog; latency uses EDITOR_SLICE_NS, not a
 * newly invented timing allowance. Durations are T4 - T0 from the trace ring. */
static int near_end_repro(bool wrapped)
{
    /* The repository's release allocation guard is intentionally inert in
     * sanitizer builds. Performance is asserted on the production build;
     * instrumented builds retain byte/state/trace checks and print timings
     * against the same bound, with no invented sanitizer timing allowance. */
    bool enforce_latency = edit_malloc_guard_active();
    trace_reset(); /* no editors/workers are alive at this setup boundary */
    const char *path = "/tmp/edit-corpus/log_1g.txt";
    struct stat st; T(stat(path, &st) == 0);
    T((uint64_t)st.st_size == UINT64_C(1024) * 1024u * 1024u); /* bead fixture: GiB = 1024 cubed */
    char power[64] = "unavailable";
    FILE *battery = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (battery) { T(fgets(power, sizeof power, battery)); T(fclose(battery) == 0); }
    power[strcspn(power, "\n")] = 0;
    const char *tag = !strcmp(power, "Discharging") ? "[bat]" :
        !strcmp(power, "unavailable") ? "[unknown]" : "[AC]";
    printf("edit-2ty mode=%s power=%s (M)%s\n", wrapped ? "file-default-wrap" : "nowrap", power, tag);
    /* Independent tail oracle: newline bytes read with pread, outside the
     * timed/guarded action. A single index chunk contains the repro's pages;
     * assert this property instead of assuming a corpus line width/count. */
    uint8_t tail[LINEIDX_CHUNK]; uint64_t starts[LINEIDX_CHUNK]; size_t nstarts = 0;
    T(st.st_size >= (off_t)sizeof tail);
    uint64_t tail_at = (uint64_t)st.st_size - sizeof tail;
    int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
    T(pread(fd, tail, sizeof tail, (off_t)tail_at) == (ssize_t)sizeof tail); T(close(fd) == 0);
    for (size_t i = 0; i < sizeof tail; i++)
        if (tail[i] == '\n') starts[nstarts++] = tail_at + i + 1u;
    T(nstarts);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    hooks h = {0};
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = wrapped ? 0 : -1,
        .hook_ctx = &h, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!editor_large_status_get(e).warm_done) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
    }
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    enum { PAGES = 20, ACTIONS = 1 + PAGES + 3 }; /* Ctrl+End + repro pages + Home/End/Z */
    uint32_t frames[ACTIONS]; size_t count = 0; bool completed = true, timely = true;
    uint64_t inserted = 0;
    size_t page = e->v.config.rows;
    T(nstarts > PAGES * page);
    uint64_t preferred = (uint64_t)st.st_size - starts[nstarts - 1u];
    T(editor_view(e).wrap == wrapped);
    for (unsigned action = 0; action < ACTIONS; action++) {
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
        ev.keysym = action == 0 || action == PAGES + 2 ? XKB_KEY_End :
            action <= PAGES ? XKB_KEY_Page_Up : action == PAGES + 1 ? XKB_KEY_Home : XKB_KEY_Z;
        if (!action) ev.mods = PLAT_MOD_CTRL;
        if (action == ACTIONS - 1) { ev.utf8_len = 1; ev.utf8[0] = 'Z'; inserted = editor_view(e).selection.cursor; }
        uint64_t before_cursor = editor_view(e).selection.cursor, visual_expected = before_cursor;
        if (wrapped && action > PAGES && action < ACTIONS - 1u) {
            layout_wrap_row row; bool approximate = false;
            T(layout_visual_row(&e->lay, e->tree, before_cursor, 0, &row, &approximate) == LAYOUT_DONE);
            if (e->v.state.visual_end && row.start == before_cursor && before_cursor)
                T(layout_visual_row(&e->lay, e->tree, before_cursor, -1, &row, &approximate) == LAYOUT_DONE);
            T(!approximate);
            visual_expected = action == PAGES + 1u ? row.start : row.end;
        }
        uint64_t sequence = editor_get_stats(e).input_sequence + 1u;
        ev.t0_ns = trace_now_ns(); T(editor_inject(e, &ev) == 0);
        deadline = ev.t0_ns + UINT64_C(5000000000); /* existing setup/settle watchdog */
        while (editor_get_stats(e).submitted_sequence < sequence && trace_now_ns() < deadline)
            T(editor_step(e, 0) >= 0);
        if (editor_get_stats(e).submitted_sequence < sequence) {
            fprintf(stderr, "edit-2ty stalled: action=%u query_pos=%llu query_target=%llu phase=%u (M)%s\n",
                action, (unsigned long long)e->v.query_pos, (unsigned long long)e->v.query_target, e->v.phase, tag);
            completed = false; break;
        }
        frames[count++] = h.submitted_id;
        if (h.submit_ns - ev.t0_ns >= EDITOR_SLICE_NS) timely = false;
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        size_t pages = action <= PAGES ? action : PAGES;
        size_t line = nstarts - 1u - pages * page;
        uint64_t start = starts[line];
        uint64_t end = line + 1u < nstarts ? starts[line + 1u] - 1u : (uint64_t)st.st_size;
        if (end > start && tail[(size_t)(end - tail_at - 1u)] == '\r') end--;
        uint64_t expected = start + (preferred < end - start ? preferred : end - start);
        if (!action) expected = (uint64_t)st.st_size;
        if (action == PAGES + 1u) expected = start;
        if (action == PAGES + 2u) expected = end;
        if (action == ACTIONS - 1u) expected = end + 1u;
        if (wrapped) {
            if (!action) expected = (uint64_t)st.st_size;
            else if (action <= PAGES) {
                T(editor_view(e).selection.cursor < before_cursor);
                expected = editor_view(e).selection.cursor;
            } else expected = action == ACTIONS - 1u ? before_cursor + 1u : visual_expected;
            /* Certify the submitted logical viewport with the independent
             * tail newline oracle even though the page counts visual rows. */
            size_t first = 0;
            while (first < nstarts && starts[first] != editor_view(e).first_byte) first++;
            T(first < nstarts);
            T(editor_view(e).first_line == editor_line_count(e) - nstarts + first);
        }
        T(editor_view(e).selection.cursor == expected);
        T(editor_view(e).selection.anchor == expected);
    }
    if (completed) {
        uint8_t ch = 0; T(editor_read(e, inserted, &ch, sizeof ch) == 0 && ch == 'Z');
        T(editor_length(e) == (uint64_t)st.st_size + sizeof ch);
    }
    if (completed && !wrapped) {
        /* Native event bursts may queue the next page before the current
         * frame finishes. Verify those commands resume from index anchors. */
        deadline = trace_now_ns() + UINT64_C(5000000000);
        while (!editor_index_complete(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_End, .mods = PLAT_MOD_CTRL};
        uint64_t sequence = editor_get_stats(e).input_sequence + 1u + PAGES;
        T(editor_inject(e, &ev) == 0);
        ev.keysym = XKB_KEY_Page_Up; ev.mods = 0;
        for (unsigned i = 0; i < PAGES; i++) T(editor_inject(e, &ev) == 0);
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
        T(editor_get_stats(e).submitted_sequence == sequence);
        size_t line = nstarts - 1u - PAGES * page;
        uint64_t end = starts[line + 1u] - 1u;
        uint64_t expected = starts[line] + (preferred < end - starts[line] ? preferred : end - starts[line]);
        T(editor_view(e).selection.cursor == expected);
    }
    if (h.active) { h.allocations += edit_malloc_guard_end(); h.active = false; }
    editor_close(e); /* quiesce workers before dumping their rings */
    const char *trace_path = wrapped ? "build/edit-2ty-wrap.trace" : "build/edit-2ty.trace";
    FILE *dump = fopen(trace_path, "wb"); T(dump);
    T(trace_dump(dump) == 0); T(fclose(dump) == 0);
    dump = fopen(trace_path, "rb"); T(dump);
    trace_loaded loaded; T(trace_fmt_load_dump(dump, &loaded) == 0); T(fclose(dump) == 0);
    bool bounded = completed && timely;
    for (size_t action = 0; action < count; action++) {
        uint64_t t0 = UINT64_MAX, t4 = UINT64_MAX;
        for (size_t i = 0; i < loaded.nrecs; i++) {
            trace_rec r = loaded.recs[i];
            if (r.frame_id != frames[action]) continue;
            if (r.ev == TRACE_T0_INGRESS && r.ns < t0) t0 = r.ns;
            if (r.ev == TRACE_T4_PRESENT_SUBMITTED && r.ns < t4) t4 = r.ns;
        }
        T(t0 != UINT64_MAX && t4 != UINT64_MAX && t4 >= t0);
        uint64_t ns = t4 - t0;
        printf("edit-2ty action=%zu %s input_submit_ns=%llu (M)%s=T4-T0 bound_ns=%llu (G)=EDITOR_SLICE_NS gate=%s\n",
            action, action == 0 ? "Ctrl+End" : action <= PAGES ? "PageUp" :
            action == PAGES + 1 ? "Home" : action == PAGES + 2 ? "End" : "Z",
            (unsigned long long)ns, tag, (unsigned long long)EDITOR_SLICE_NS, enforce_latency ? "release" : "TRACK-sanitized");
        if (ns >= EDITOR_SLICE_NS) bounded = false;
    }
    trace_fmt_dump_free(&loaded);
    printf("edit-2ty typing_path_allocations=%zu (M)%s=counting allocator; required=0 (G)=CLAUDE law 2 guard=%s\n",
        h.allocations, tag, enforce_latency ? "active" : "ASan-inert (release required)");
    T(h.allocations == 0); T(completed); T(count == ACTIONS);
    if (enforce_latency) T(bounded);
    puts("edit-2ty near-end input/submit: ok"); return 0;
}

/* edit-91c: Ctrl+End and PageUp/PageDown into rows that are not laid out
 * resolve through resident windows. While the bulk lane is occupied (held
 * here by a blocking job, modelling the 1 GiB index build, warm and find jobs
 * that occupy it after open) an in-core window must still arrive, on the
 * foreground lane, so each follow submits its frame while bulk stays held.
 * hold=false runs the same keys during the real index build and only
 * measures. Budget for Ctrl+End -> first frame at the end: G5 warm, A column
 * (perf/01-perf-target.md section 0.2): open request -> correct first
 * viewport = one 64 KiB read + one full frame, PB 1.025 ms (E), gate
 * 6 / 9 ms p50/p99 (G). A resident window is SCROLL_SCAN_BUDGET = 64 KiB,
 * the same read, followed by the same full frame, so the row transfers.
 * Tracked (law 1), not enforced; the only enforced bound is the suites'
 * existing 5 s watchdog. */
typedef struct lane_hold { _Atomic bool release, running; } lane_hold;
static void hold_lane(work_ctx *c)
{
    lane_hold *l = c->arg; atomic_store(&l->running, true);
    while (!atomic_load(&l->release) && !work_should_stop(c))
        nanosleep(&(struct timespec){0, 1000000}, NULL);
}
#define EDIT_91C_G5_P50_NS UINT64_C(6000000)
#define EDIT_91C_G5_P99_NS UINT64_C(9000000)
static int starved_follows(bool hold, bool after_index)
{
    const char *path = "/tmp/edit-corpus/log_1g.txt";
    struct stat st; T(stat(path, &st) == 0);
    T((uint64_t)st.st_size == UINT64_C(1024) * 1024u * 1024u);
    char power[64] = "unavailable";
    FILE *battery = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (battery) { T(fgets(power, sizeof power, battery)); T(fclose(battery) == 0); }
    power[strcspn(power, "\n")] = 0;
    const char *tag = !strcmp(power, "Discharging") ? "[bat]" :
        !strcmp(power, "unavailable") ? "[unknown]" : "[AC]";
    /* Independent tail oracle over the last two resident windows: the EOF
     * window is [size - 64 KiB, size) because size is a multiple of 64 KiB,
     * so paging up out of it requests a second window while bulk is held. */
    enum { ORACLE = 2u * SCROLL_SCAN_BUDGET };
    static uint8_t tail[ORACLE]; static uint64_t starts[ORACLE]; size_t nstarts = 0;
    uint64_t tail_at = (uint64_t)st.st_size - ORACLE;
    int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
    T(pread(fd, tail, ORACLE, (off_t)tail_at) == (ssize_t)ORACLE); T(close(fd) == 0);
    for (size_t i = 0; i < ORACLE; i++) if (tail[i] == '\n') starts[nstarts++] = tail_at + i + 1u;
    size_t window_lines = 0; /* line starts inside the EOF window */
    for (size_t i = 0; i < nstarts; i++) if (starts[i] >= tail_at + SCROLL_SCAN_BUDGET) window_lines++;
    T(window_lines && window_lines < nstarts);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    hooks h = {0};
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = -1,
        .hook_ctx = &h, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!h.frames) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    if (after_index) {
        /* The agh family: index complete, warm/find still own the bulk lane. */
        while (!editor_index_complete(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
        T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    }
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = {0, 0};
    if (hold) {
        held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK}); T(held.epoch);
        /* The index/warm invocations queued ahead run to their slice end;
         * then the hold owns the lane for the whole key sequence. */
        deadline = trace_now_ns() + UINT64_C(5000000000);
        while (!atomic_load(&lane.running)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    }
    bool index_before = editor_index_complete(e);
    size_t page = e->v.config.rows;
    size_t pages_up = window_lines / page + 2u; /* leaves the EOF window */
    T(pages_up * page < nstarts);
    uint64_t preferred = (uint64_t)st.st_size - starts[nstarts - 1u];
    uint64_t end_ns = 0, worst_page_ns = 0;
    size_t pages = 0;
    /* edit-jf9: page motions must submit without exact index publication.
     * Count (P): an outbound traversal and its return, plus Ctrl+End. */
    size_t actions = 1u + 2u * pages_up;
    for (size_t action = 0; action < actions; action++) {
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
        if (!action) { ev.keysym = XKB_KEY_End; ev.mods = PLAT_MOD_CTRL; }
        else if (action <= pages_up) { ev.keysym = XKB_KEY_Page_Up; pages++; }
        else { ev.keysym = XKB_KEY_Page_Down; pages--; }
        uint64_t sequence = editor_get_stats(e).input_sequence + 1u;
        ev.t0_ns = trace_now_ns(); T(editor_inject(e, &ev) == 0);
        /* edit-jf9 first held-build PageUp: G5 warm A p99 from edit-91c,
         * same resident read + frame. Other motions keep the setup watchdog. */
        deadline = ev.t0_ns + (hold && !after_index && action == 1u ?
            EDIT_91C_G5_P99_NS : UINT64_C(5000000000));
        uint64_t next_report = ev.t0_ns + UINT64_C(100000000);
        while (editor_get_stats(e).submitted_sequence < sequence && trace_now_ns() < deadline) {
            T(editor_step(e, 0) >= 0);
            if (getenv("EDIT_91C_TRACE") && trace_now_ns() >= next_report) {
                next_report += UINT64_C(100000000);
                fprintf(stderr, "edit-91c t=%llu ms scroll_pending=%d resident_pending=%d windows=%u/%u view_busy=%d phase=%u query_pos=%llu index_complete=%d prefix=%zu first_byte=%llu\n",
                    (unsigned long long)((trace_now_ns() - ev.t0_ns) / 1000000u), e->buffer->scroll_pending, e->buffer->scroll_resident.pending,
                    e->buffer->scroll_resident.windows[0].state, e->buffer->scroll_resident.windows[1].state,
                    view_busy(&e->v), e->v.phase, (unsigned long long)e->v.query_pos, editor_index_complete(e),
                    lineidx_built_prefix(e->buffer->index), (unsigned long long)editor_view(e).first_byte);
            }
        }
        if (editor_get_stats(e).submitted_sequence < sequence) {
            fprintf(stderr, "edit-91c stalled: hold=%d action=%zu scroll_pending=%d resident_pending=%d windows=%u/%u "
                "view_busy=%d phase=%u query_pos=%llu query_target=%llu index_complete=%d (M)%s\n",
                hold, action, e->buffer->scroll_pending, e->buffer->scroll_resident.pending,
                e->buffer->scroll_resident.windows[0].state, e->buffer->scroll_resident.windows[1].state,
                view_busy(&e->v), e->v.phase, (unsigned long long)e->v.query_pos, (unsigned long long)e->v.query_target,
                editor_index_complete(e), tag);
            atomic_store(&lane.release, true);
            T(editor_get_stats(e).submitted_sequence >= sequence);
        }
        uint64_t ns = h.submit_ns - ev.t0_ns;
        if (hold && !after_index && action == 1u) T(ns <= EDIT_91C_G5_P99_NS);
        if (action == 1u) printf("edit-jf9 first_page_submit_ns=%llu (M)%s=submit-t0 bound_ns=%llu (G)=G5 warm A p99\n",
            (unsigned long long)ns, tag, (unsigned long long)EDIT_91C_G5_P99_NS);
        if (!action) end_ns = ns; else if (ns > worst_page_ns) worst_page_ns = ns;
        if (hold) T(!work_handle_finished(&e->pool, held)); /* never through the bulk lane */
        size_t line = nstarts - 1u - pages * page;
        uint64_t start = starts[line];
        uint64_t end = line + 1u < nstarts ? starts[line + 1u] - 1u : (uint64_t)st.st_size;
        if (end > start && tail[(size_t)(end - tail_at - 1u)] == '\r') end--;
        uint64_t expected = !action ? (uint64_t)st.st_size : start + (preferred < end - start ? preferred : end - start);
        T(editor_view(e).selection.cursor == expected);
        T(editor_view(e).selection.anchor == expected);
        /* The submitted top is a certified line start from the oracle. */
        size_t first = 0;
        while (first < nstarts && starts[first] != editor_view(e).first_byte) first++;
        T(first < nstarts);
        T(editor_view(e).first_byte <= expected);
    }
    bool index_after = editor_index_complete(e);
    printf("edit-91c hold=%d after_index=%d ctrl_end_submit_ns=%llu worst_page_submit_ns=%llu (M)%s=submit-t0 "
           "budget_ns=%llu/%llu (G)=G5 warm A p50/p99, 64 KiB read + full frame, PB 1.025 ms (E) "
           "index_complete=%d->%d pages=%zu gate=TRACK\n",
        hold, after_index, (unsigned long long)end_ns, (unsigned long long)worst_page_ns, tag,
        (unsigned long long)EDIT_91C_G5_P50_NS, (unsigned long long)EDIT_91C_G5_P99_NS,
        index_before, index_after, pages_up);
    atomic_store(&lane.release, true);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    if (h.active) { h.allocations += edit_malloc_guard_end(); h.active = false; }
    editor_close(e);
    printf("edit-91c %s follows outside laid-out rows: ok\n",
        !hold ? "index-building" : after_index ? "bulk-held after index" : "bulk-held during build");
    return 0;
}
/* lander 6b: edit-pd6 x edit-jf9 composition. Ctrl+End and PageUp arrive in
 * one burst while the bulk/index lane is held, so the PageUp is handled with
 * the EOF byte jump unresolved (pd6 queues its viewport page) and with an
 * inexact count (jf9 moves the cursor byte-relatively). Result must equal the
 * unraced order: cursor one page (config rows, P) above EOF's line at EOF's
 * column, inside a viewport whose top is a certified line start.
 * Oracle and fixture as starved_follows; existing 5 s setup watchdog. */
static int jump_then_page(void)
{
    const char *path = "/tmp/edit-corpus/log_1g.txt";
    struct stat st; T(stat(path, &st) == 0);
    enum { ORACLE = 2u * SCROLL_SCAN_BUDGET };
    static uint8_t tail[ORACLE]; static uint64_t starts[ORACLE]; size_t nstarts = 0;
    uint64_t tail_at = (uint64_t)st.st_size - ORACLE;
    int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
    T(pread(fd, tail, ORACLE, (off_t)tail_at) == (ssize_t)ORACLE); T(close(fd) == 0);
    for (size_t i = 0; i < ORACLE; i++) if (tail[i] == '\n') starts[nstarts++] = tail_at + i + 1u;
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    hooks h = {0};
    editor_config cfg = {.path = path, .cols = 60, .rows = 8, .wrap_mode = -1,
        .hook_ctx = &h, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!h.frames) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK}); T(held.epoch);
    while (!atomic_load(&lane.running)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(!editor_index_complete(e));
    size_t page = e->v.config.rows;
    T(page < nstarts);
    plat_event end = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_End, .mods = PLAT_MOD_CTRL};
    plat_event up = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Page_Up};
    uint64_t sequence = editor_get_stats(e).input_sequence + 2u;
    end.t0_ns = up.t0_ns = trace_now_ns();
    T(editor_inject(e, &end) == 0); T(editor_inject(e, &up) == 0);
    bool queued_seen = false;
    deadline = trace_now_ns() + UINT64_C(5000000000);
    while (editor_get_stats(e).submitted_sequence < sequence || e->buffer->scroll_pending) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
        if (e->buffer->scroll_queued_pages) queued_seen = true;
    }
    T(!work_handle_finished(&e->pool, held));
    uint64_t preferred = (uint64_t)st.st_size - starts[nstarts - 1u];
    size_t line = nstarts - 1u - page;
    uint64_t start = starts[line], stop = starts[line + 1u] - 1u;
    if (stop > start && tail[(size_t)(stop - tail_at - 1u)] == '\r') stop--;
    uint64_t expected = start + (preferred < stop - start ? preferred : stop - start);
    view_state v = editor_view(e);
    T(v.selection.cursor == expected && v.selection.anchor == expected);
    size_t first = 0;
    while (first < nstarts && starts[first] != v.first_byte) first++;
    T(first < nstarts && first <= line && line < first + page);
    printf("lander-6b pd6 x jf9 Ctrl+End then PageUp during held build: queued_seen=%d ok\n", queued_seen);
    atomic_store(&lane.release, true);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    if (h.active) { h.allocations += edit_malloc_guard_end(); h.active = false; }
    editor_close(e);
    return 0;
}
/* edit-jf9: headless counterpart to the held-index editor regression.
 * Fixture (P): repeat a tab/ASCII/CRLF line until its prefix exceeds view's
 * per-call scan bound. Local page motion reads only rows, never that prefix.
 * Geometry reuses the starved_follows fixture; no new timing allowance. */
static int relative_complete(view *v, view_key key, bool shift)
{
    view_change change;
    edit_malloc_guard_begin();
    int rc = view_relative_command(v, key, shift, &change);
    T(!change.changed); T(v->scanned <= VIEW_SCAN_BOUND);
    while (rc == VIEW_MORE) {
        rc = view_continue(v, &change);
        T(!change.changed); T(v->scanned <= VIEW_SCAN_BOUND);
    }
    T(edit_malloc_guard_end() == 0); /* (P) motion reserves all scratch in view */
    return rc;
}
static int relative_motion(void)
{
    const uint8_t line[] = "a\tb\r\n";
    uint8_t text[VIEW_SCAN_BOUND + sizeof line * VIEW_SCAN_BOUND];
    size_t n = sizeof text / (sizeof line - 1u) * (sizeof line - 1u);
    for (size_t at = 0; at < n; at += sizeof line - 1u)
        memcpy(text + at, line, sizeof line - 1u);
    piece_allocator a = piece_default_allocator();
    piece_tree *tree = piece_create(&a); T(tree);
    T(piece_init_copy(tree, text, n) == 0);
    view v; view_config cfg = {.rows = 8, .cols = 60};
    view_init(&v, tree, &cfg);
    v.state.selection.cursor = v.state.selection.anchor = n;
    v.state.approximate = true;
    view_change change;
    int rc = view_relative_command(&v, VIEW_PAGE_UP, false, &change);
    uint64_t scanned = v.scanned;
    while (rc == VIEW_MORE) {
        rc = view_continue(&v, &change); scanned += v.scanned;
    }
    printf("edit-jf9 relative PageUp: rc=%d scanned=%llu bound=%u (P)=VIEW_SCAN_BOUND\n",
        rc, (unsigned long long)scanned, VIEW_SCAN_BOUND);
    T(rc == VIEW_OK);
    T(scanned <= VIEW_SCAN_BOUND); /* local rows fit the existing scan bound */
    T(v.state.selection.cursor == n - cfg.rows * (sizeof line - 1u));
    T(v.state.approximate);
    /* (P) each supported motion, with both selection affinities, against
     * ordinary view's exact implementation. Tabs and CRLF are in the fixture. */
    const view_key motions[] = {VIEW_LEFT, VIEW_RIGHT, VIEW_UP, VIEW_DOWN,
        VIEW_PAGE_UP, VIEW_PAGE_DOWN};
    const uint64_t positions[] = {0, sizeof line - 1u, n,
        n - cfg.rows * (sizeof line - 1u),
        n - cfg.rows * (sizeof line - 1u) + 1u}; /* (P) boundaries/tab */
    for (size_t position = 0; position < sizeof positions / sizeof positions[0]; position++) {
        for (size_t motion = 0; motion < sizeof motions / sizeof motions[0]; motion++) {
            for (unsigned extend = 0; extend <= (unsigned)true; extend++) {
                view exact;
                view_init(&v, tree, &cfg); view_init(&exact, tree, &cfg);
                v.state.selection.cursor = v.state.selection.anchor = positions[position];
                exact.state = v.state;
                T(relative_complete(&v, motions[motion], extend != 0) == VIEW_OK);
                rc = view_command(&exact, motions[motion], extend != 0, NULL, 0, &change);
                while (rc == VIEW_MORE) rc = view_continue(&exact, &change);
                T(rc == VIEW_OK);
                T(v.state.selection.cursor == exact.state.selection.cursor);
                T(v.state.selection.anchor == exact.state.selection.anchor);
                T(v.state.selection.preferred_col == exact.state.selection.preferred_col);
                T(!v.line_next && !v.query_next && !v.query_kind);
            }
        }
    }
    /* Boundary no-ops retain a previously chosen visual column (P: reuse
     * the configured viewport width as that column, beyond the short line). */
    for (size_t motion = 0; motion < sizeof motions / sizeof motions[0]; motion++) {
        view exact;
        view_init(&v, tree, &cfg); view_init(&exact, tree, &cfg);
        v.state.selection.cursor = v.state.selection.anchor =
            motions[motion] == VIEW_LEFT || motions[motion] == VIEW_UP || motions[motion] == VIEW_PAGE_UP ? 0 : n;
        v.state.selection.preferred_col = cfg.cols; exact.state = v.state;
        T(relative_complete(&v, motions[motion], false) == VIEW_OK);
        rc = view_command(&exact, motions[motion], false, NULL, 0, &change);
        while (rc == VIEW_MORE) rc = view_continue(&exact, &change);
        T(rc == VIEW_OK);
        T(v.state.selection.cursor == exact.state.selection.cursor);
        T(v.state.selection.preferred_col == exact.state.selection.preferred_col);
    }
    const uint8_t unicode[] = "a\t👩‍💻\r\nshort\n";
    T(piece_delete(tree, 0, n, NULL) == 0);
    T(piece_insert(tree, 0, unicode, sizeof unicode - 1u) == 0);
    for (size_t motion = 0; motion < sizeof motions / sizeof motions[0]; motion++) {
        view exact;
        view_init(&v, tree, &cfg); view_init(&exact, tree, &cfg);
        /* (P) the complete ASCII/tab/ZWJ cluster precedes the CRLF. */
        v.state.selection.cursor = sizeof "a\t👩‍💻" - 1u;
        exact.state = v.state;
        T(relative_complete(&v, motions[motion], true) == VIEW_OK);
        rc = view_command(&exact, motions[motion], true, NULL, 0, &change);
        while (rc == VIEW_MORE) rc = view_continue(&exact, &change);
        T(rc == VIEW_OK);
        T(v.state.selection.cursor == exact.state.selection.cursor);
        T(v.state.selection.anchor == exact.state.selection.anchor);
        T(v.state.selection.preferred_col == exact.state.selection.preferred_col);
    }
    /* The backward line proof over a newline-free prefix must yield. Cancel
     * restores the completed state without publishing guessed line metadata. */
    T(piece_delete(tree, 0, piece_len(tree), NULL) == 0);
    memset(text, 'a', sizeof text); T(piece_insert(tree, 0, text, sizeof text) == 0);
    view_init(&v, tree, &cfg);
    v.state.selection.cursor = v.state.selection.anchor = sizeof text;
    view_state before = v.state;
    rc = view_relative_command(&v, VIEW_UP, true, &change);
    T(rc == VIEW_MORE); T(v.scanned <= VIEW_SCAN_BOUND);
    view_cancel(&v);
    T(v.state.selection.cursor == before.selection.cursor);
    T(v.state.selection.anchor == before.selection.anchor);
    T(!v.relative && !view_busy(&v));
    T(relative_complete(&v, VIEW_UP, false) == VIEW_OK);
    T(v.state.selection.cursor == 0);
    T(v.state.selection.preferred_col == sizeof text);
    rc = view_restore(&v, &before, &change);
    while (rc == VIEW_MORE) rc = view_continue(&v, &change);
    T(rc == VIEW_OK); T(!v.relative);
    T(v.state.selection.cursor == before.selection.cursor);
    piece_destroy(tree);
    puts("edit-jf9 headless relative motion: ok");
    return 0;
}
/* edit-mmp: keys typed right after open, while the background line index is
 * still building, are applied and never fail editor_step. Fixture (P): a
 * 2 MiB COPY file (2^21 bytes, LF every 64th byte; above FILE_PREFIX_MAX, so
 * the COPY is asynchronous and the buffer lazy: edit-28u case (c)'s file).
 * The edit script is edit-28u's small_child: "EF" at the end, three forward
 * deletes 5 bytes past the prefix, "AB" at byte 100. A bulk hold (below)
 * makes every burst precede index completion; reindex instead edits after
 * the first index and continues while the rebuild runs. MMP_RUNS = 40 opens
 * per case: (M) edit-28u saw the failure in 3 of 40 unheld runs. Counts runs
 * whose editor_step failed and settles that outlived EDITOR_IDLE_NS (E: the
 * existing editor watchdog); settle times are tracked prints, not asserts.
 * EDIT_MMP_REPRO=1 [EDIT_MMP_MIB=n] [EDIT_MMP_REINDEX=1] runs one case alone;
 * sizes above FILE_PREFIX_MAX + 64 KiB stay sparse: the first MiB of rows,
 * the last 64 KiB of rows, one LF per MiB between. GiB sizes run without a
 * journal (this tree's checkpoint copies the whole text). */
#define MMP_RUNS 40u
#define MMP_SMALL ((uint64_t)1 << 21)
#define MMP_GIB ((uint64_t)1 << 30)
#define MMP_ROWS (64u * 1024u)
typedef struct mmp_tally { unsigned failed, overdue, unheld; int first_rc; uint64_t max_settle_ns, max_key_ns; bool keys; int mode; } mmp_tally;
static int mmp_settle(editor *e, mmp_tally *t, bool *bad)
{
    uint64_t start = trace_now_ns(), limit = start + EDITOR_IDLE_NS;
    for (;;) {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) { if (!t->first_rc) t->first_rc = rc; *bad = true; return rc; }
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active && !s.open_pending) break;
        if (trace_now_ns() >= limit) { t->overdue++; return 1; }
    }
    uint64_t d = trace_now_ns() - start; if (d > t->max_settle_ns) t->max_settle_ns = d;
    if (t->keys && d > t->max_key_ns) t->max_key_ns = d;
    return 0;
}
static int mmp_type(editor *e, const char *text, uint32_t sym, mmp_tally *t, bool *bad)
{
    for (const char *c = text; *c; c++) {
        plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym ? sym : (uint8_t)*c,
            .utf8_len = (uint8_t)(sym ? 0u : 1u), .t0_ns = trace_now_ns()};
        if (!sym) key.utf8[0] = (uint8_t)*c;
        T(editor_inject(e, &key) == 0);
        t->keys = true; int rc = mmp_settle(e, t, bad); t->keys = false; if (rc) return rc;
    }
    return 0;
}
static int mmp_fixture(const char *path, uint64_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    bool ok = ftruncate(fd, (off_t)size) == 0;
    static uint8_t rows[MMP_ROWS];
    for (size_t i = 0; i < sizeof rows; i++) rows[i] = i % 64u == 63u ? '\n' : (uint8_t)('a' + i % 26u);
    if (size == MMP_SMALL) {
        for (uint64_t at = 0; ok && at < size; at += sizeof rows) ok = pwrite(fd, rows, sizeof rows, (off_t)at) == (ssize_t)sizeof rows;
    } else {
        for (uint64_t at = 0; ok && at < FILE_PREFIX_MAX; at += sizeof rows) ok = pwrite(fd, rows, sizeof rows, (off_t)at) == (ssize_t)sizeof rows;
        ok = ok && pwrite(fd, rows, sizeof rows, (off_t)(size - sizeof rows)) == (ssize_t)sizeof rows;
        /* One LF per MiB keeps the sparse middle a file of 1 MiB lines (one
         * 4 KiB block per MiB on disk) instead of one ~1 GiB line. */
        for (uint64_t at = FILE_PREFIX_MAX; ok && at + FILE_PREFIX_MAX <= size - sizeof rows; at += FILE_PREFIX_MAX)
            ok = pwrite(fd, "\n", 1, (off_t)(at + FILE_PREFIX_MAX - 1u)) == 1;
    }
    T(close(fd) == 0); T(ok); return 0;
}
/* Deterministic race: a bulk job that yields its lane (work_continue: back
 * of the bulk FIFO) until the asynchronous COPY is ready, then holds the
 * single bulk lane, so the index build that adoption submits queues behind
 * it and every key of the burst lands before the index can complete.
 * (E) The hold is bounded by EDITOR_IDLE_NS, the existing editor watchdog. */
typedef struct mmp_hold { const file *f; _Atomic bool holding, release; } mmp_hold;
static void mmp_hold_fn(work_ctx *c)
{
    mmp_hold *h = c->arg;
    if (!file_open_ready(h->f)) { if (!work_should_stop(c)) (void)work_continue(c); return; }
    atomic_store(&h->holding, true);
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&h->release) && !work_should_stop(c) && trace_now_ns() < limit)
        nanosleep(&(struct timespec){0, 100000}, NULL);
}
/* reindex: wait for the first index, then type near the head (the edit
 * reopens the index) and continue at EOF while the rebuild runs. */
static int mmp_run(const char *path, const char *log, uint64_t size, bool reindex, mmp_tally *t)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    bool bad = false;
    mmp_hold hold = {.f = e->buffer->file}; atomic_init(&hold.holding, false); atomic_init(&hold.release, false);
    work_handle held = {0};
    if (!reindex && e->buffer->file && e->buffer->pending_open) {
        held = work_submit(&e->pool, (work_job){mmp_hold_fn, &hold, 0, WORK_BULK}); T(held.epoch);
    }
    /* (P) Positions: end of file, 5 bytes past the 1 MiB prefix, byte 100. */
    uint64_t p2 = FILE_PREFIX_MAX + 5u, p1 = 100u;
    int rc = mmp_settle(e, t, &bad);
    if (reindex) {
        uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
        while (!rc && !editor_index_complete(e)) {
            rc = editor_step(e, 0);
            if (rc == EDITOR_MORE) rc = 0;
            if (!rc && trace_now_ns() >= limit) { t->overdue++; rc = 1; }
        }
        if (!rc) rc = mmp_settle(e, t, &bad);
        if (!rc) rc = editor_set_cursor(e, p1);
        if (!rc) rc = mmp_type(e, "A", 0, t, &bad);
        if (!rc) rc = editor_set_cursor(e, size + 1u);
        if (!rc) rc = mmp_type(e, "B", 0, t, &bad);
    }
    bool indexing = !editor_index_complete(e);
    t->mode = e->buffer->file ? (int)file_open_mode(e->buffer->file) : -1;
    uint64_t base = editor_length(e);
    if (!rc) rc = editor_set_cursor(e, base);
    if (!rc) rc = mmp_type(e, "EF", 0, t, &bad);
    if (!rc) rc = editor_set_cursor(e, p2);
    if (!rc) rc = mmp_type(e, "ddd", XKB_KEY_Delete, t, &bad);
    if (!rc) rc = editor_set_cursor(e, p1);
    if (!rc) rc = mmp_type(e, "AB", 0, t, &bad);
    if (held.epoch) {
        if (!atomic_load(&hold.holding)) t->unheld++;
        atomic_store(&hold.release, true);
        while (!work_handle_finished(&e->pool, held)) nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    uint64_t len = editor_length(e);
    /* (P) +2 typed, -3 deleted, +2 typed. */
    if (!rc && len != base + 1u) rc = 1;
    if (rc && !bad && !t->first_rc) t->first_rc = rc;
    if (rc) t->failed++;
    if (rc) fprintf(stderr, "editor_large_test: edit-mmp size=%llu reindex=%d rc=%d error_cause=%d indexing_at_burst=%d len=%llu\n",
        (unsigned long long)size, reindex, rc, e->stats.error_cause, indexing, (unsigned long long)len);
    editor_close(e);
    return 0;
}
static int mmp_remove_tree(const char *root)
{
    DIR *d = opendir(root); if (!d) return 0;
    struct dirent *ent; int rc = 0;
    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        char sub[512]; if (snprintf(sub, sizeof sub, "%s/%s", root, ent->d_name) >= (int)sizeof sub) { rc = 1; continue; }
        struct stat st;
        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) rc |= mmp_remove_tree(sub);
        else if (unlink(sub)) rc = 1;
    }
    closedir(d);
    return rmdir(root) ? 1 : rc;
}
/* Each iteration owns a private XDG_DATA_HOME under the fixture root and
 * removes it before the next open, on every path. The 1 GiB mapped case runs
 * without a journal: this tree's checkpoints write the whole text (the
 * pre-edit-28u journal), which a 40-open loop must never do. */
static int mmp_body(const char *root, uint64_t size, bool reindex, mmp_tally *t)
{
    char path[256], log[256], xdg[256];
    T(snprintf(path, sizeof path, "%s/source", root) > 0); T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(snprintf(xdg, sizeof xdg, "%s/xdg", root) > 0);
    T(mmp_fixture(path, size) == 0);
    const char *saved = getenv("XDG_DATA_HOME"); char keep[256] = "";
    if (saved) (void)snprintf(keep, sizeof keep, "%s", saved);
    int rc = 0;
    for (unsigned run = 0; !rc && run < MMP_RUNS; run++) {
        T(mkdir(xdg, 0700) == 0); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
        rc = mmp_run(path, size < MMP_GIB ? log : NULL, size, reindex, t);
        (void)unlink(log);
        if (mmp_remove_tree(xdg)) rc = 1;
    }
    if (saved) (void)setenv("XDG_DATA_HOME", keep, 1); else (void)unsetenv("XDG_DATA_HOME");
    return rc;
}
static int typing_while_indexing(uint64_t size, bool reindex)
{
    char root[] = "build/editor-large-mmp-XXXXXX"; T(mkdtemp(root));
    mmp_tally t = {0};
    int rc = mmp_body(root, size, reindex, &t);
    T(mmp_remove_tree(root) == 0);
    char power[16] = "?"; FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fscanf(f, "%15s", power) != 1) power[0] = '?'; fclose(f); }
    double load1 = 0; f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf", &load1) != 1) load1 = 0; fclose(f); }
    printf("editor_large_test: edit-mmp typing while indexing size=%llu reindex=%d runs=%u failed=%u overdue=%u unheld=%u first_rc=%d max_settle=%.3f ms max_key_settle=%.3f ms (M) mode=%s power=%s load1=%.2f\n",
        (unsigned long long)size, reindex, MMP_RUNS, t.failed, t.overdue, t.unheld, t.first_rc, (double)t.max_settle_ns / 1e6, (double)t.max_key_ns / 1e6, t.mode == (int)FILE_MODE_COPY ? "copy" : t.mode == (int)FILE_MODE_MMAP ? "mmap" : "?", power, load1);
    T(fflush(stdout) == 0);
    T(rc == 0); T(t.failed == 0); T(t.overdue == 0);
    return 0;
}
/* edit-dz0: Ctrl+End on a 1 GiB lazy file, then one typed key before the
 * line index publishes. Fixture (P): mmp_fixture's sparse GiB layout (first
 * MiB and last 64 KiB of 64-byte rows, one LF per MiB between), mapped, no
 * journal, private XDG_DATA_HOME, removed with its root on every exit path.
 * The bulk lane is held from open (hold_lane) so the index cannot complete.
 * Structural assert: the key's view repair asks line_query to count at most
 * VIEW_SCAN_BOUND bytes (P: one view call's charged scan work), never the
 * file prefix (about the file size before the fix). The settle is a tracked
 * print (law 1); EDITOR_IDLE_NS is only the existing settle watchdog (E).
 * wrap: the same key with wrapping on (a .txt log's default). Since edit-wv7
 * it repairs byte-relatively too (0 B (M)); before, Ctrl+End's wrapped
 * placement counted LFs from byte 0 (docs/decisions/edit-wv7.md). */
typedef struct dz0_result { uint64_t line_bytes, settle_ns, size; mmp_tally t; } dz0_result;
static int dz0_keys(editor *e, lane_hold *lane, dz0_result *r)
{
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&lane->running)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    bool bad = false;
    T(mmp_settle(e, &r->t, &bad) == 0);
    T(!editor_index_complete(e)); T(!e->buffer->lg.lines_exact);
    plat_event end = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_End, .mods = PLAT_MOD_CTRL, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &end) == 0);
    T(mmp_settle(e, &r->t, &bad) == 0);
    T(editor_view(e).selection.cursor == r->size);
    T(!editor_index_complete(e));
    uint64_t before = e->v.line_scanned;
    T(mmp_type(e, "x", 0, &r->t, &bad) == 0);
    r->line_bytes = e->v.line_scanned - before; r->settle_ns = r->t.max_key_ns;
    T(!bad);
    T(!editor_index_complete(e));
    T(editor_length(e) == r->size + 1u);
    T(editor_view(e).selection.cursor == r->size + 1u);
    uint8_t got = 0; T(editor_read(e, r->size, &got, 1) == 0); T(got == 'x');
    return 0;
}
static int dz0_body(const char *root, bool wrap, dz0_result *r)
{
    char path[256], xdg[256];
    T(snprintf(path, sizeof path, "%s/source", root) > 0); T(snprintf(xdg, sizeof xdg, "%s/xdg", root) > 0);
    r->size = MMP_GIB;
    T(mmp_fixture(path, r->size) == 0);
    {
        /* Warm page cache for the tail, as for a log just written: the last
         * two 1 MiB lines (P: mmp_fixture's layout, hole line + tail rows).
         * scroll's resident follow takes the foreground lane only for
         * windows in core (edit-91c); a cold hole would queue it on the held
         * bulk lane forever. The pre-fix repair warmed them by scanning. */
        static uint8_t warm[64u * 1024u];
        int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
        bool ok = true;
        for (uint64_t at = r->size - 2u * FILE_PREFIX_MAX; ok && at < r->size; at += sizeof warm)
            ok = pread(fd, warm, sizeof warm, (off_t)at) == (ssize_t)sizeof warm;
        T(close(fd) == 0); T(ok);
    }
    T(mkdir(xdg, 0700) == 0); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .cols = 32, .rows = 3, .wrap_mode = wrap ? 1 : -1};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK});
    int rc = held.epoch ? dz0_keys(e, &lane, r) : 1;
    atomic_store(&lane.release, true);
    while (held.epoch && !work_handle_finished(&e->pool, held)) nanosleep(&(struct timespec){0, 100000}, NULL);
    editor_close(e);
    return rc;
}
static int dz0_key_at_end(bool wrap)
{
    char root[] = "build/editor-large-dz0-XXXXXX"; T(mkdtemp(root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[256] = "";
    if (saved) (void)snprintf(keep, sizeof keep, "%s", saved);
    dz0_result r = {0};
    int rc = dz0_body(root, wrap, &r);
    if (saved) (void)setenv("XDG_DATA_HOME", keep, 1); else (void)unsetenv("XDG_DATA_HOME");
    int removed = mmp_remove_tree(root);
    char power[32] = "?"; FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = 0; fclose(f); }
    double load1 = 0; f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf", &load1) != 1) load1 = 0; fclose(f); }
    printf("editor_large_test: edit-dz0 key at EOF before index wrap=%d size=%llu line_query_bytes=%llu bound=%u (P: VIEW_SCAN_BOUND) key_settle=%.3f ms (M) overdue=%u power=\"%s\" load1=%.2f\n",
        wrap, (unsigned long long)r.size, (unsigned long long)r.line_bytes, VIEW_SCAN_BOUND, (double)r.settle_ns / 1e6, r.t.overdue, power, load1);
    T(fflush(stdout) == 0);
    T(removed == 0); T(rc == 0); T(r.t.overdue == 0);
    T(r.line_bytes <= VIEW_SCAN_BOUND);
    return 0;
}
/* edit-ptn: the dz0 setup (mmp_fixture's sparse 1 GiB layout, tail warmed,
 * bulk lane held, Ctrl+End) with the last 64 KiB (P: MMP_ROWS) rewritten.
 * PTN_CJK: rows of 21 x U+6F22 (3 B each) + LF = 64 B (P), no ASCII pair near
 * the cursor; Left, then one key: the repair seed must not fall back to byte 0.
 * PTN_LONG_TOP: ASCII, the screen's top line is 5000 B (P: > VIEW_SCAN_BOUND)
 * before two 64 B rows; Left, one key. PTN_KEYS: Q at EOF, then Home, End,
 * Ctrl+Left. Probe: view.line_scanned + seek_scanned (line_query counts plus
 * every other byte read to locate a boundary from a seed). Asserted bound VIEW_SCAN_BOUND (P: every line near the cursor is at
 * most 64 B or the 5000 B top line, which is proven from its own start; see
 * docs/decisions/edit-ptn.md for the general bound). Settles: tracked prints. */
enum { PTN_CJK, PTN_LONG_TOP, PTN_KEYS };
static int ptn_tail(const char *path, uint64_t size, int kind)
{
    static uint8_t tail[MMP_ROWS];
    size_t n = sizeof tail;
    for (size_t i = 0; i < n; i++) tail[i] = i % 64u == 63u ? '\n' : (uint8_t)('a' + i % 26u);
    if (kind == PTN_CJK)
        for (size_t i = 0; i + 64u <= n; i += 64u) for (size_t k = 0; k < 21u; k++) memcpy(tail + i + 3u * k, "\xe6\xbc\xa2", 3);
    if (kind == PTN_LONG_TOP) {
        memset(tail + n - 128u - 5001u, 'b', 5001u); tail[n - 129u] = '\n'; tail[n - 128u - 5002u] = '\n';
    }
    int fd = open(path, O_WRONLY); T(fd >= 0);
    bool ok = pwrite(fd, tail, n, (off_t)(size - n)) == (ssize_t)n;
    T(close(fd) == 0); T(ok); return 0;
}
static void ptn_stamp(char *power, size_t cap, double *load1)
{
    snprintf(power, cap, "?"); FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(power, (int)cap, f)) power[strcspn(power, "\n")] = 0; fclose(f); }
    *load1 = 0; f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf", load1) != 1) *load1 = 0; fclose(f); }
}
static int ptn_key(editor *e, mmp_tally *t, const char *name, uint32_t sym, uint16_t mods, char text, uint64_t *worst)
{
    bool bad = false; uint64_t before = e->v.line_scanned + e->v.seek_scanned;
    t->max_key_ns = 0;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym ? sym : (uint8_t)text, .mods = mods,
        .utf8_len = (uint8_t)(sym ? 0u : 1u), .t0_ns = trace_now_ns()};
    if (!sym) key.utf8[0] = (uint8_t)text;
    T(editor_inject(e, &key) == 0);
    t->keys = true; int rc = mmp_settle(e, t, &bad); t->keys = false;
    uint64_t bytes = e->v.line_scanned + e->v.seek_scanned - before; if (bytes > *worst) *worst = bytes;
    char power[32]; double load1; ptn_stamp(power, sizeof power, &load1);
    printf("editor_large_test: edit-ptn key=%s probe_bytes=%llu bound=%u (P: VIEW_SCAN_BOUND) key_settle=%.3f ms (M) rc=%d overdue=%u power=\"%s\" load1=%.2f\n",
        name, (unsigned long long)bytes, VIEW_SCAN_BOUND, (double)t->max_key_ns / 1e6, rc, t->overdue, power, load1);
    T(fflush(stdout) == 0);
    T(rc == 0); T(!bad); T(!editor_index_complete(e));
    return 0;
}
static int ptn_body(const char *root, int kind, uint64_t *worst)
{
    char path[256], xdg[256];
    T(snprintf(path, sizeof path, "%s/source", root) > 0); T(snprintf(xdg, sizeof xdg, "%s/xdg", root) > 0);
    uint64_t size = MMP_GIB;
    T(mmp_fixture(path, size) == 0); T(ptn_tail(path, size, kind) == 0);
    {
        static uint8_t warm[64u * 1024u]; /* as dz0_body: last two 1 MiB lines */
        int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
        bool ok = true;
        for (uint64_t at = size - 2u * FILE_PREFIX_MAX; ok && at < size; at += sizeof warm)
            ok = pread(fd, warm, sizeof warm, (off_t)at) == (ssize_t)sizeof warm;
        T(close(fd) == 0); T(ok);
    }
    T(mkdir(xdg, 0700) == 0); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .cols = 32, .rows = 3, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK});
    int rc = held.epoch ? 0 : 1;
    mmp_tally t = {0}; bool bad = false;
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    while (!rc && !atomic_load(&lane.running)) { if (editor_step(e, 0) < 0 || trace_now_ns() >= deadline) rc = 1; }
    if (!rc) rc = mmp_settle(e, &t, &bad);
    if (!rc && (editor_index_complete(e) || e->buffer->lg.lines_exact)) rc = 1;
    if (!rc) rc = ptn_key(e, &t, "ctrl+end", XKB_KEY_End, PLAT_MOD_CTRL, 0, &(uint64_t){0});
    if (!rc && editor_view(e).selection.cursor != size) rc = 1;
    if (!rc && kind != PTN_KEYS) {
        rc = ptn_key(e, &t, "left", XKB_KEY_Left, 0, 0, worst);
        if (!rc) rc = ptn_key(e, &t, kind == PTN_CJK ? "x-in-cjk" : "x-under-long-top", 0, 0, 'x', worst);
        uint8_t got = 0;
        if (!rc && (editor_read(e, size - 1u, &got, 1) || got != 'x' || editor_view(e).selection.cursor != size)) rc = 1;
    } else if (!rc) {
        rc = ptn_key(e, &t, "Q-at-eof", 0, 0, 'Q', worst);
        if (!rc) rc = ptn_key(e, &t, "home", XKB_KEY_Home, 0, 0, worst);
        /* (P) the last row is 63 bytes + LF, then Q: Home lands on its start. */
        if (!rc && editor_view(e).selection.cursor != size) rc = 1;
        if (!rc) rc = ptn_key(e, &t, "end", XKB_KEY_End, 0, 0, worst);
        if (!rc && editor_view(e).selection.cursor != size + 1u) rc = 1;
        if (!rc) rc = ptn_key(e, &t, "ctrl+left", XKB_KEY_Left, PLAT_MOD_CTRL, 0, worst);
        if (!rc && editor_view(e).selection.cursor != size) rc = 1;
        /* edit-1vun: unwrapped Select All places the cursor at EOF without a
         * line query (before: follow_line's line_at(cursor) from byte 0). */
        uint64_t before_line = e->v.line_scanned;
        if (!rc) rc = ptn_key(e, &t, "ctrl+a", 'a', PLAT_MOD_CTRL, 0, worst);
        uint64_t select_line = e->v.line_scanned - before_line;
        printf("editor_large_test: edit-1vun unwrapped select-all line_scanned=%llu (P: 0)\n", (unsigned long long)select_line);
        if (!rc && (editor_view(e).selection.cursor != size + 1u || editor_view(e).selection.anchor != 0 || select_line != 0)) rc = 1;
    }
    atomic_store(&lane.release, true);
    while (held.epoch && !work_handle_finished(&e->pool, held)) nanosleep(&(struct timespec){0, 100000}, NULL);
    editor_close(e);
    return rc;
}
static int ptn_case(int kind)
{
    char root[] = "build/editor-large-ptn-XXXXXX"; T(mkdtemp(root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[256] = "";
    if (saved) (void)snprintf(keep, sizeof keep, "%s", saved);
    uint64_t worst = 0;
    int rc = ptn_body(root, kind, &worst);
    if (saved) (void)setenv("XDG_DATA_HOME", keep, 1); else (void)unsetenv("XDG_DATA_HOME");
    int removed = mmp_remove_tree(root);
    printf("editor_large_test: edit-ptn case=%d rc=%d worst_probe_bytes=%llu bound=%u\n", kind, rc, (unsigned long long)worst, VIEW_SCAN_BOUND);
    T(fflush(stdout) == 0);
    T(removed == 0); T(rc == 0); T(worst <= VIEW_SCAN_BOUND);
    return 0;
}
/* edit-wv7: wrapped Ctrl+End on an unindexed lazy 1 GiB file, then one key.
 * Fixture and hold as dz0_body (mmp_fixture's sparse GiB layout, tail
 * warmed, bulk lane held from open, private XDG_DATA_HOME, root removed on
 * every exit path), wrap on (.wrap_mode = 1, a .txt log's default).
 * Probe per key: view.line_scanned + seek_scanned. Asserted bound
 * VIEW_SCAN_BOUND (P: the rows near EOF are 64 B (mmp_fixture), so the
 * byte-relative walks read one 256 B quantum each; before the fix the EOF
 * placement counted LFs from byte 0, about 2^30 B). Settles: tracked prints
 * (law 1); EDITOR_IDLE_NS is the existing settle watchdog (E). After the
 * key the lane is released and the index publishes: the key's byte is at
 * EOF, the cursor follows it, and every laid-out wrap row carries its exact
 * logical line (no stale estimated rows). */
static bool wv7_index_done(editor *e) { return editor_large_status_get(e).lines_exact && lineidx_complete(e->buffer->index); }
static int wv7_key(editor *e, mmp_tally *t, const char *name, uint32_t sym, uint16_t mods, char text, uint64_t *worst)
{
    bool bad = false; uint64_t before_line = e->v.line_scanned, before_seek = e->v.seek_scanned;
    uint64_t *probe = e->lay.line_query_probe; T(probe); uint64_t before_queries = *probe;
    t->max_key_ns = 0;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym ? sym : (uint8_t)text, .mods = mods,
        .utf8_len = (uint8_t)(sym ? 0u : 1u), .t0_ns = trace_now_ns()};
    if (!sym) key.utf8[0] = (uint8_t)text;
    T(editor_inject(e, &key) == 0);
    t->keys = true; int rc = mmp_settle(e, t, &bad); t->keys = false;
    uint64_t line = e->v.line_scanned - before_line, seek = e->v.seek_scanned - before_seek;
    if (line + seek > *worst) *worst = line + seek;
    /* edit-1vun: a layout_init would detach the probe and hide queries. */
    T(e->lay.line_query_probe == probe);
    uint64_t queries = *probe - before_queries;
    char power[32]; double load1; ptn_stamp(power, sizeof power, &load1);
    printf("editor_large_test: edit-wv7 wrap=1 key=%s line_scanned=%llu seek_scanned=%llu bound=%u (P: VIEW_SCAN_BOUND) layout_line_queries=%llu key_settle=%.3f ms (M) rc=%d overdue=%u power=\"%s\" load1=%.2f\n",
        name, (unsigned long long)line, (unsigned long long)seek, VIEW_SCAN_BOUND, (unsigned long long)queries,
        (double)t->max_key_ns / 1e6, rc, t->overdue, power, load1);
    T(fflush(stdout) == 0);
    T(rc == 0); T(!bad); T(!editor_index_complete(e));
    /* edit-1vun (P): no visual-row query falls back to a piece line query,
     * which resolves the unindexed mapping's newline counts from byte 0. */
    T(queries == 0);
    return 0;
}
static int wv7_body(const char *root, uint64_t *worst)
{
    char path[256], xdg[256];
    T(snprintf(path, sizeof path, "%s/source", root) > 0); T(snprintf(xdg, sizeof xdg, "%s/xdg", root) > 0);
    uint64_t size = MMP_GIB;
    T(mmp_fixture(path, size) == 0);
    {
        static uint8_t warm[64u * 1024u]; /* as dz0_body: last two 1 MiB lines */
        int fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
        bool ok = true;
        for (uint64_t at = size - 2u * FILE_PREFIX_MAX; ok && at < size; at += sizeof warm)
            ok = pread(fd, warm, sizeof warm, (off_t)at) == (ssize_t)sizeof warm;
        T(close(fd) == 0); T(ok);
    }
    T(mkdir(xdg, 0700) == 0); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .cols = 32, .rows = 3, .wrap_mode = 1};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    uint64_t queries = 0; e->lay.line_query_probe = &queries; /* edit-1vun */
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK});
    int rc = held.epoch ? 0 : 1;
    mmp_tally t = {0}; bool bad = false;
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    while (!rc && !atomic_load(&lane.running)) { if (editor_step(e, 0) < 0 || trace_now_ns() >= deadline) rc = 1; }
    if (!rc) rc = mmp_settle(e, &t, &bad);
    if (!rc && (editor_index_complete(e) || e->buffer->lg.lines_exact || !e->v.state.wrap)) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "ctrl+end", XKB_KEY_End, PLAT_MOD_CTRL, 0, worst);
    if (!rc && editor_view(e).selection.cursor != size) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "x-at-eof", 0, 0, 'x', worst);
    uint8_t got = 0;
    if (!rc && (editor_length(e) != size + 1u || editor_read(e, size, &got, 1) || got != 'x' ||
                editor_view(e).selection.cursor != size + 1u)) rc = 1;
    /* The same wrapped placement for a page and document Home, then back to
     * EOF: (P) a page up from EOF over 64 B rows stays inside the tail. */
    if (!rc) rc = wv7_key(e, &t, "page-up", XKB_KEY_Page_Up, 0, 0, worst);
    if (!rc && editor_view(e).selection.cursor >= size) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "ctrl+home", XKB_KEY_Home, PLAT_MOD_CTRL, 0, worst);
    if (!rc && (editor_view(e).selection.cursor != 0 || editor_view(e).first_byte != 0 ||
                editor_view(e).first_line != 0)) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "ctrl+end-again", XKB_KEY_End, PLAT_MOD_CTRL, 0, worst);
    if (!rc && editor_view(e).selection.cursor != size + 1u) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "ctrl+a", 'a', PLAT_MOD_CTRL, 0, worst);
    if (!rc && (editor_view(e).selection.cursor != size + 1u || editor_view(e).selection.anchor != 0)) rc = 1;
    if (!rc) rc = wv7_key(e, &t, "ctrl+end-collapse", XKB_KEY_End, PLAT_MOD_CTRL, 0, worst);
    if (!rc && (editor_view(e).selection.cursor != size + 1u || editor_view(e).selection.anchor != size + 1u)) rc = 1;
    atomic_store(&lane.release, true);
    while (held.epoch && !work_handle_finished(&e->pool, held)) nanosleep(&(struct timespec){0, 100000}, NULL);
    if (!rc) {
        /* (E) the existing settle watchdog bounds the index build too. */
        uint64_t start = trace_now_ns(); deadline = start + EDITOR_IDLE_NS;
        while (!rc && !wv7_index_done(e)) {
            int s = editor_step(e, 5); if (s != EDITOR_OK && s != EDITOR_MORE) rc = 1;
            if (trace_now_ns() >= deadline) { t.overdue++; rc = 1; }
        }
        if (!rc) rc = mmp_settle(e, &t, &bad);
        char power[32]; double load1; ptn_stamp(power, sizeof power, &load1);
        printf("editor_large_test: edit-wv7 index published after %.3f ms (M) rc=%d power=\"%s\" load1=%.2f\n",
            (double)(trace_now_ns() - start) / 1e6, rc, power, load1);
    }
    if (!rc) {
        view_state s = editor_view(e);
        lineidx_src cur = editor_source(e->buffer);
        if (s.selection.cursor != size + 1u || editor_length(e) != size + 1u) rc = 1;
        if (!rc && (editor_read(e, size, &got, 1) || got != 'x')) rc = 1;
        if (!rc && (s.approximate || editor_large_status_get(e).top_estimated)) rc = 1;
        lineidx_result top = lineidx_byte_to_line(e->buffer->index, &cur, s.first_byte);
        if (!rc && (!top.exact || top.value != s.first_line)) rc = 1;
        bool cursor_row = false;
        for (uint32_t i = 0; !rc && i < e->lay.grid->dims.rows; i++) {
            const layout_wrap_row *r = &e->lay.wrap_rows[i];
            if (r->start == LAYOUT_VOID_ROW) continue;
            lineidx_result l = lineidx_byte_to_line(e->buffer->index, &cur, r->line_start);
            uint8_t prev = '\n';
            if (r->line_start && editor_read(e, r->line_start - 1u, &prev, 1)) rc = 1;
            if (!l.exact || l.value != r->line || prev != '\n' || r->approximate) {
                fprintf(stderr, "editor_large_test: edit-wv7 stale row %u start=%llu line_start=%llu line=%llu exact_line=%llu approximate=%d\n",
                    i, (unsigned long long)r->start, (unsigned long long)r->line_start, (unsigned long long)r->line,
                    (unsigned long long)l.value, r->approximate);
                rc = 1;
            }
            if (size + 1u >= r->start && size + 1u <= r->end) cursor_row = true;
        }
        if (!rc && !cursor_row) rc = 1;
        if (rc) fprintf(stderr, "editor_large_test: edit-wv7 after index cursor=%llu first_byte=%llu first_line=%llu exact=%llu approximate=%d\n",
            (unsigned long long)s.selection.cursor, (unsigned long long)s.first_byte, (unsigned long long)s.first_line,
            (unsigned long long)top.value, s.approximate);
    }
    editor_close(e);
    return rc;
}
static int wv7_case(void)
{
    char root[] = "build/editor-large-wv7-XXXXXX"; T(mkdtemp(root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[256] = "";
    if (saved) (void)snprintf(keep, sizeof keep, "%s", saved);
    uint64_t worst = 0;
    int rc = wv7_body(root, &worst);
    if (saved) (void)setenv("XDG_DATA_HOME", keep, 1); else (void)unsetenv("XDG_DATA_HOME");
    int removed = mmp_remove_tree(root);
    printf("editor_large_test: edit-wv7 rc=%d worst_probe_bytes=%llu bound=%u\n", rc, (unsigned long long)worst, VIEW_SCAN_BOUND);
    T(fflush(stdout) == 0);
    T(removed == 0); T(rc == 0); T(worst <= VIEW_SCAN_BOUND);
    return 0;
}
/* edit-1vun: wrapped Ctrl+End then PageUp on an unindexed lazy 1 GiB file
 * whose last line is long and has no trailing LF, so the top row after
 * Ctrl+End is a continuation. Fixture (P): mmp_fixture's sparse GiB layout
 * with the last MMP_ROWS = 64 KiB rewritten as letters without LF; the last
 * line is then [1023 MiB, 1 GiB) (the last LF of the sparse middle sits at
 * 1023 MiB - 1), so the top row lies about 1 MiB past its line start, beyond
 * LAYOUT_BYTE_BUDGET = 64 KiB (P, layout.h): no resident seed reaches it.
 * Hold, warm tail, private XDG_DATA_HOME and removal as wv7_body.
 * Asserted: zero layout piece line queries per key (probe), a continuation
 * top after Ctrl+End, the cursor at EOF, PageUp moves it up. The relative
 * walk over the long line is its own length in cooperative slices (wv7
 * bound); it is printed, not asserted. Settles: tracked prints (law 1). */
static int vun_key(editor *e, mmp_tally *t, const char *name, uint32_t sym, uint16_t mods)
{
    bool bad = false; uint64_t before_line = e->v.line_scanned, before_seek = e->v.seek_scanned;
    uint64_t *probe = e->lay.line_query_probe; T(probe); uint64_t before_queries = *probe;
    t->max_key_ns = 0;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &key) == 0);
    t->keys = true; int rc = mmp_settle(e, t, &bad); t->keys = false;
    T(e->lay.line_query_probe == probe);
    uint64_t queries = *probe - before_queries;
    char power[32]; double load1; ptn_stamp(power, sizeof power, &load1);
    view_state s = editor_view(e);
    printf("editor_large_test: edit-1vun continuation-top key=%s line_scanned=%llu seek_scanned=%llu layout_line_queries=%llu cursor=%llu first_byte=%llu visual_byte=%llu row0_continuation=%d key_settle=%.3f ms (M) rc=%d overdue=%u power=\"%s\" load1=%.2f\n",
        name, (unsigned long long)(e->v.line_scanned - before_line), (unsigned long long)(e->v.seek_scanned - before_seek),
        (unsigned long long)queries, (unsigned long long)s.selection.cursor, (unsigned long long)s.first_byte,
        (unsigned long long)s.visual_byte, e->lay.wrap_rows[0].continuation, (double)t->max_key_ns / 1e6, rc, t->overdue, power, load1);
    T(fflush(stdout) == 0);
    T(rc == 0); T(!bad); T(!editor_index_complete(e));
    T(e->v.line_scanned == before_line);
    T(queries == 0);
    return 0;
}
static int vun_body(const char *root)
{
    char path[256], xdg[256];
    T(snprintf(path, sizeof path, "%s/source", root) > 0); T(snprintf(xdg, sizeof xdg, "%s/xdg", root) > 0);
    uint64_t size = MMP_GIB;
    T(mmp_fixture(path, size) == 0);
    {
        static uint8_t tail[MMP_ROWS];
        for (size_t i = 0; i < sizeof tail; i++) tail[i] = (uint8_t)('a' + i % 26u);
        int fd = open(path, O_WRONLY); T(fd >= 0);
        bool ok = pwrite(fd, tail, sizeof tail, (off_t)(size - sizeof tail)) == (ssize_t)sizeof tail;
        T(close(fd) == 0); T(ok);
        static uint8_t warm[64u * 1024u]; /* as dz0_body: last two 1 MiB lines */
        fd = open(path, O_RDONLY | O_CLOEXEC); T(fd >= 0);
        for (uint64_t at = size - 2u * FILE_PREFIX_MAX; ok && at < size; at += sizeof warm)
            ok = pread(fd, warm, sizeof warm, (off_t)at) == (ssize_t)sizeof warm;
        T(close(fd) == 0); T(ok);
    }
    T(mkdir(xdg, 0700) == 0); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .cols = 32, .rows = 3, .wrap_mode = 1};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    uint64_t queries = 0; e->lay.line_query_probe = &queries;
    lane_hold lane; atomic_init(&lane.release, false); atomic_init(&lane.running, false);
    work_handle held = work_submit(&e->pool, (work_job){hold_lane, &lane, 0, WORK_BULK});
    int rc = held.epoch ? 0 : 1;
    mmp_tally t = {0}; bool bad = false;
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    while (!rc && !atomic_load(&lane.running)) { if (editor_step(e, 0) < 0 || trace_now_ns() >= deadline) rc = 1; }
    if (!rc) rc = mmp_settle(e, &t, &bad);
    if (!rc && (editor_index_complete(e) || e->buffer->lg.lines_exact || !e->v.state.wrap)) rc = 1;
    if (!rc) rc = vun_key(e, &t, "ctrl+end", XKB_KEY_End, PLAT_MOD_CTRL);
    if (!rc) {
        view_state s = editor_view(e);
        if (s.selection.cursor != size || s.visual_byte == s.first_byte || !e->lay.wrap_rows[0].continuation ||
            s.first_byte != size - FILE_PREFIX_MAX) rc = 1;
    }
    if (!rc) rc = vun_key(e, &t, "page-up", XKB_KEY_Page_Up, 0);
    if (!rc && editor_view(e).selection.cursor >= size) rc = 1;
    atomic_store(&lane.release, true);
    while (held.epoch && !work_handle_finished(&e->pool, held)) nanosleep(&(struct timespec){0, 100000}, NULL);
    editor_close(e);
    return rc;
}
static int vun_case(void)
{
    char root[] = "build/editor-large-vun-XXXXXX"; T(mkdtemp(root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[256] = "";
    if (saved) (void)snprintf(keep, sizeof keep, "%s", saved);
    int rc = vun_body(root);
    if (saved) (void)setenv("XDG_DATA_HOME", keep, 1); else (void)unsetenv("XDG_DATA_HOME");
    int removed = mmp_remove_tree(root);
    printf("editor_large_test: edit-1vun continuation-top rc=%d\n", rc);
    T(fflush(stdout) == 0);
    T(removed == 0); T(rc == 0);
    return 0;
}
/* (P) Adapters retain existing fixture arguments and environment value
 * parsing. Numeric inputs are copied from the original selector branches. */
static int select_edit_dz0_repro(void)
{
    return dz0_key_at_end(getenv("EDIT_DZ0_WRAP") != NULL);
}
static int select_edit_ptn_repro(void)
{
    return ptn_case(atoi(getenv("EDIT_PTN_REPRO")));
}
static int select_edit_mmp_repro(void)
{
    /* (P) Decimal MiB input: radix ten and a binary MiB byte shift. */
    return typing_while_indexing(getenv("EDIT_MMP_MIB") ? (uint64_t)strtoull(getenv("EDIT_MMP_MIB"), NULL, 10) << 20 : MMP_SMALL, getenv("EDIT_MMP_REINDEX") != NULL);
}
static int select_edit_tn2_placement(void)
{
    /* (P) Zero is success after the preceding placement assertion. */
    return 0;
}
static int select_edit_91c_starved(void)
{
    return starved_follows(getenv("EDIT_91C_NOHOLD") == NULL, getenv("EDIT_91C_AFTER_INDEX") != NULL);
}
static int select_edit_2ty_repro(void)
{
    return near_end_repro(getenv("EDIT_2TY_WRAP") != NULL);
}
static int select_p4_cold_wrap(void)
{
    return cold_bracket_paint(true);
}
static int select_p4_estimated_delete(void)
{
    return estimated_newline_delete(getenv("P4_ESTIMATED_WRAP") != NULL);
}
static int select_p4_cold_paint(void)
{
    return cold_bracket_paint(false);
}
int main(void)
{
    trace_init(); (void)trace_thread_register();
    test_select_fn selected;
    static const test_selector select_edit_mmp_repro_cases[] = {
        {"EDIT_MMP_REPRO", select_edit_mmp_repro},
        {"EDIT_PTN_REPRO", select_edit_ptn_repro},
        {"EDIT_DZ0_REPRO", select_edit_dz0_repro},
        {"EDIT_WV7_REPRO", wv7_case},
        {"EDIT_1VUN_REPRO", vun_case},
        {"EDIT_JF9_VIEW", relative_motion},
    };
    selected = TEST_SELECT_ENV(select_edit_mmp_repro_cases);
    if (selected) return selected();
    T(relative_motion() == 0);
    /* edit-xzw: isolate the existing null-backend fixture from IPC/display
     * prerequisites denied by the worker sandbox. */
    if (getenv("EDIT_XZW_REPRO")) goto xzw_fixture;
    static const test_selector select_edit_tn2_wrapped_jump_cases[] = {
        {"EDIT_TN2_WRAPPED_JUMP", wrapped_byte_jump},
    };
    selected = TEST_SELECT_ENV(select_edit_tn2_wrapped_jump_cases);
    if (selected) return selected();
    T(wrapped_placement() == 0);
    static const test_selector select_edit_tn2_placement_cases[] = {
        {"EDIT_TN2_PLACEMENT", select_edit_tn2_placement},
    };
    selected = TEST_SELECT_ENV(select_edit_tn2_placement_cases);
    if (selected) return selected();
    T(wrapped_byte_jump() == 0);
    static const test_selector select_edit_91c_starved_cases[] = {
        {"EDIT_91C_STARVED", select_edit_91c_starved},
    };
    selected = TEST_SELECT_ENV(select_edit_91c_starved_cases);
    if (selected) return selected();
    T(starved_follows(true, false) == 0);
    T(starved_follows(true, true) == 0);
    T(starved_follows(false, false) == 0);
    T(jump_then_page() == 0);
    static const test_selector select_p4_estimated_delete_cases[] = {
        {"P4_ESTIMATED_DELETE", select_p4_estimated_delete},
        {"P4_LAZY_LONG_LINE", lazy_long_line_rows},
        {"P4_COLD_WRAP", select_p4_cold_wrap},
        {"EDIT_2TY_REPRO", select_edit_2ty_repro},
    };
    selected = TEST_SELECT_ENV(select_p4_estimated_delete_cases);
    if (selected) return selected();
    T(near_end_repro(false) == 0);
    T(near_end_repro(true) == 0);
    static const test_selector select_p4_mailbox_yield_cases[] = {
        {"P4_MAILBOX_YIELD", mailbox_yield},
    };
    selected = TEST_SELECT_ENV(select_p4_mailbox_yield_cases);
    if (selected) return selected();
    T(mailbox_yield() == 0);
    static const test_selector select_p4_cooperative_jobs_cases[] = {
        {"P4_COOPERATIVE_JOBS", cooperative_large_jobs},
    };
    selected = TEST_SELECT_ENV(select_p4_cooperative_jobs_cases);
    if (selected) return selected();
    T(cooperative_large_jobs() == 0);
    T(lazy_long_line_rows() == 0);
    static const test_selector select_p4_warm_repaint_cases[] = {
        {"P4_WARM_REPAINT", warm_repaint},
    };
    selected = TEST_SELECT_ENV(select_p4_warm_repaint_cases);
    if (selected) return selected();
    T(warm_repaint() == 0);
    T(estimated_newline_delete(false) == 0);
    T(estimated_newline_delete(true) == 0);
    static const test_selector select_p4_cold_paint_cases[] = {
        {"P4_COLD_PAINT", select_p4_cold_paint},
    };
    selected = TEST_SELECT_ENV(select_p4_cold_paint_cases);
    if (selected) return selected();
    T(cold_bracket_paint(false) == 0);
    T(cold_bracket_paint(true) == 0);
    static const test_selector select_p4_byte_jump_cases[] = {
        {"P4_BYTE_JUMP", byte_jump_boundaries},
    };
    selected = TEST_SELECT_ENV(select_p4_byte_jump_cases);
    if (selected) return selected();
    T(byte_jump_boundaries() == 0);
    static const test_selector select_p4_coordinates_cases[] = {
        {"P4_COORDINATES", explicit_coordinates},
    };
    selected = TEST_SELECT_ENV(select_p4_coordinates_cases);
    if (selected) return selected();
    T(explicit_coordinates() == 0);
    static const test_selector select_p4_keep_background_cases[] = {
        {"P4_KEEP_BACKGROUND", keep_restarts_background},
    };
    selected = TEST_SELECT_ENV(select_p4_keep_background_cases);
    if (selected) return selected();
    T(keep_restarts_background() == 0);
    static const test_selector select_p4_find_restart_cases[] = {
        {"P4_FIND_RESTART", cancelled_find_restart},
    };
    selected = TEST_SELECT_ENV(select_p4_find_restart_cases);
    if (selected) return selected();
    T(cancelled_find_restart() == 0);
    if (getenv("DNC2_REQUIRE_EARLY_INDEX")) T(index_before_attachment_gap() == 0);
    T(mapped_startup_ipc() == 0);
    T(mapped_preview_first() == 0);
    T(mapped_find_boundaries() == 0);
    T(gutter_estimates() == 0);
    T(typing_while_indexing(MMP_SMALL, false) == 0);
    T(typing_while_indexing(MMP_SMALL, true) == 0);
    T(dz0_key_at_end(false) == 0);
    T(dz0_key_at_end(true) == 0);
    T(ptn_case(PTN_CJK) == 0);
    T(ptn_case(PTN_LONG_TOP) == 0);
    T(ptn_case(PTN_KEYS) == 0);
    T(wv7_case() == 0);
    T(vun_case() == 0);
xzw_fixture:;
    char path[] = "build/editor-large-test-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    T(write_fixture(path) == 0);
    T(index_completes_between_steps(path) == 0);
    T(index_arrives_during_frame(path) == 0);
    struct stat st; T(stat(path, &st) == 0); uint64_t size = (uint64_t)st.st_size;
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    hooks h = {0};
    editor_config cfg = {.path = path, .cols = 100, .rows = 30, .copy_threshold = 4096, .closed_capacity = 1,
        .hook_ctx = &h, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    /* 1. Before the index arrives: estimated, not exact, and plausibly sized. */
    editor_large_status s = editor_large_status_get(e);
    T(s.mapped); T(!s.lines_exact); T(editor_line_count(e) > 1);
    T(editor_line_count(e) != TOTAL_LINES);             /* the prefix density is deliberately wrong */
    T(!editor_index_complete(e));
    /* Cooperative indexing must publish before warming can monopolize bulk. */
    T(e->buffer->lg.warm); T(!e->buffer->lg.warm_h.epoch);
    /* The mapped find source is usable without exact newline metadata. */
    piece_snapshot *original = piece_snapshot_take(e->tree); T(original);
    const uint8_t *mapped_bytes = NULL; size_t mapped_length = 0;
    T(file_snapshot_bytes(original, NULL, &mapped_bytes, &mapped_length) == 1);
    T(mapped_bytes && mapped_length == size);
    T(memcmp(mapped_bytes, "l000000\n", 8) == 0);
    T(memcmp(mapped_bytes + mapped_length - 3u, "end", 3) == 0);
    T(!editor_large_status_get(e).lines_exact);
    /* Certification also needs bounded cancellation on larger originals. */
    _Atomic bool blocked; atomic_init(&blocked, false);
    work_handle cancelled_handle = work_submit(&e->pool, (work_job){hold_index, &blocked, 0, WORK_BULK}); T(cancelled_handle.epoch);
    work_ctx cancelled_control = {.pool = &e->pool, .slot = &e->pool.slots[cancelled_handle.slot], .epoch = cancelled_handle.epoch};
    work_cancel(&e->pool, cancelled_handle);
    T(file_snapshot_bytes(original, &cancelled_control, &mapped_bytes, &mapped_length) == 0);
    T(!mapped_bytes && !mapped_length);
    while (!work_handle_finished(&e->pool, cancelled_handle)) nanosleep(&(struct timespec){0, 1000000}, NULL);
    /* 2. Jump by byte before the index: line shown is an estimate. */
    uint64_t target = (uint64_t)A_LINES * 8u + 1000u * B_LEN + 10u;   /* inside exact line 21000 */
    T(editor_large_goto_byte(e, target) == 0);
    T(editor_large_status_get(e).top_estimated);
    T(editor_view(e).first_line > 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    /* 3. A find and the index share the one bulk worker; foreground keeps stepping. */
    const uint8_t needle[] = "NEEDLE";
    T(editor_large_find_begin(e, needle, 6) == 0);
    T(editor_large_find_begin(e, needle, 6) == EDITOR_ERR_ARG);        /* one at a time */
    if (step_until(e, all_done, UINT64_C(3000000000))) { editor_large_status d = editor_large_status_get(e); fprintf(stderr, "exact=%d find=%d warm=%d idx=%d\n", d.lines_exact, d.find_done, d.warm_done, editor_index_complete(e)); return 1; }
    s = editor_large_status_get(e);
    if (s.top_estimated) {
        fprintf(stderr, "edit-xzw completion: estimated=%d pending=%d resident=%d exact_extent=%d approximate=%d view_busy=%d full_pending=%d\n",
            s.top_estimated, e->buffer->scroll_pending, e->buffer->scroll_resident.pending,
            e->buffer->scroll.extent.exact, e->buffer->scroll.approximate, view_busy(&e->v), e->full_pending);
        /* (P) all_done observes jobs, whereas submit can publish the index
         * after this turn's final scroll slice. The next turn can still
         * yield awaiting the foreground window. (E) Reuse the suite's
         * existing settle watchdog, rather than bound worker scheduling
         * by an invented number of editor turns. */
        int rc = step_until(e, settled, UINT64_C(5000000000));
        s = editor_large_status_get(e);
        if (rc || s.top_estimated) {
            /* Quiesce workers before reading their trace rings. */
            editor_close(e);
            FILE *dump = fopen("build/edit-xzw-stale.trace", "wb"); T(dump);
            T(trace_dump(dump) == 0); T(fclose(dump) == 0);
        }
        T(rc == 0);
    }
    T(s.lines_exact); T(!s.top_estimated); T(editor_line_count(e) == TOTAL_LINES);
    T(editor_view(e).first_line == A_LINES + 1000u);                    /* swapped to the exact number */
    T(s.find_rc == 0); T(s.find_total == 3);
    T(s.find_first == (uint64_t)A_LINES * 8u + 100u * B_LEN + 5u);
    T(editor_large_find_begin(e, needle, 0) == EDITOR_ERR_ARG);
    uint64_t line_byte = 0; bool ex = false;
    line_byte = editor_large_line_to_byte(e, A_LINES + 5u, &ex);
    T(ex); T(line_byte == (uint64_t)A_LINES * 8u + 5u * B_LEN);
    /* Index arrival must retain a gutter redraw while a view action is busy. */
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    e->buffer->lg.lines_exact = false; e->full_pending = false; e->v.busy = true;
    editor_large_index_progress(e);
    T(editor_large_status_get(e).lines_exact); T(e->full_pending);
    e->v.busy = false;
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    /* 4. Typing with the index complete allocates nothing and is not blocked. */
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_x, .utf8_len = 1, .utf8 = {'x'}};
    h.allocations = 0;
    T(editor_inject(e, &ev) == 0); T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    T(h.allocations == 0);
    T(editor_length(e) == size + 1);
    piece_snapshot *edited = piece_snapshot_take(e->tree); T(edited);
    T(file_snapshot_bytes(edited, NULL, &mapped_bytes, &mapped_length) == 0);
    T(!mapped_bytes && !mapped_length);
    piece_snapshot_release(edited);
    /* A live old snapshot still certifies its immutable original after edit. */
    T(file_snapshot_bytes(original, NULL, &mapped_bytes, &mapped_length) == 1);
    T(mapped_length == size);
    piece_snapshot_release(original);
    /* 5. Save completes while the loop keeps stepping; the file has the edit. */
    T(editor_large_save_begin(e) == 0);
    T(editor_large_save_begin(e) == EDITOR_ERR_ARG);                   /* one at a time */
    T(step_until(e, save_done, UINT64_C(20000000000)) == 0);

    T(editor_large_status_get(e).save_status == 0);
    T(stat(path, &st) == 0); T((uint64_t)st.st_size == size + 1);
    {
        FILE *f = fopen(path, "rb"); T(f); uint8_t got[2] = {0, 0};
        T(fseek(f, (long)target, SEEK_SET) == 0); T(fread(got, 1, 1, f) == 1); fclose(f);
        uint8_t was; T(editor_read(e, target, &was, 1) == 0); T(got[0] == was);
    }
    T(prepared_completion(e) == 0);
    T(mailbox_pressure(e, needle) == 0);
    /* Per-buffer jobs must complete into their owner while another tab is active. */
    uint64_t second_id = 0;
    T(editor_large_find_begin(e, needle, 6) == 0);
    T(editor_large_save_begin(e) == 0);
    const uint8_t second[] = "other\nNEEDLE\n";
    T(editor_add_buffer(e, NULL, second, sizeof second - 1, &second_id) == 0);
    T(editor_large_status_get(e).lines_exact);
    T(!editor_large_status_get(e).find_done);
    T(!editor_large_status_get(e).save_done);
    T(editor_large_find_begin(e, needle, 6) == 0);
    T(step_until(e, find_done, UINT64_C(3000000000)) == 0);
    T(editor_large_status_get(e).find_total == 1);
    T(!editor_large_status_get(e).save_done);
    T(editor_select_tab(e, 0) == 0);
    T(step_until(e, find_done, UINT64_C(3000000000)) == 0);
    T(step_until(e, save_done, UINT64_C(20000000000)) == 0);
    T(editor_large_status_get(e).find_total == 2); /* typed x splits the third match */
    T(editor_large_status_get(e).save_status == 0);
    T(editor_line_count(e) == TOTAL_LINES);
    /* Queued snapshot jobs survive tab retirement until physical completion. */
    T(editor_large_find_begin(e, needle, 6) == 0);
    T(editor_select_tab(e, 1) == 0);
    T(editor_large_find_begin(e, needle, 6) == 0);
    T(editor_close_tab(e, 0) == 0);
    /* edit-2ye3: the untitled buffer with text is unsaved, so eviction keeps
     * the modified retained tab; Don't save retires this closing buffer. */
    T(editor_close_tab(e, 0) == EDITOR_ERR_CAPACITY);
    T(editor_close_discard(e, 0) == 0);
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    editor_close(e); unlink(path);
    puts("editor_large_test: ok");
    return 0;
}
