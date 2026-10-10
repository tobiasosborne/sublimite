/* edit-457.10 / P4.10: large-file behavioural contract through the editor loop.
 * A ~1.6 MB generated fixture is forced into mmap mode (copy_threshold 4 KiB),
 * so the estimate -> exact swap, find, save and background-worker paths run in
 * `make check`. edit-9yd also reads the 1 GiB log corpus for gutter publication;
 * the large-file timing rows live in bench/editor_large_bench.c. */
#include "editor/editor.h"
#include "editor/large.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "base/base.h"
#include "trace/trace.h"
#include "file/file_test.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    hooks h = {.estimated_fg = 0x4c525d};
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
static int gutter_estimates(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    hooks h = {.estimated_fg = 0x4c525d};
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
static int mailbox_pressure(editor *e, const uint8_t *needle)
{
    _Atomic bool full; atomic_init(&full, false);
    work_handle h = work_submit(&e->pool, (work_job){fill_mailbox, &full, 0, WORK_BULK});
    T(h.epoch);
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (!atomic_load(&full)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 1000000}, NULL); }
    T(editor_large_find_begin(e, needle, 6) == 0);
    while (!atomic_load(&e->buffer->lg.find->released)) { T(trace_now_ns() < deadline); nanosleep(&(struct timespec){0, 1000000}, NULL); }
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

int main(void)
{
    trace_init(); (void)trace_thread_register();
    if (getenv("DNC2_REQUIRE_EARLY_INDEX")) T(index_before_attachment_gap() == 0);
    T(mapped_startup_ipc() == 0);
    T(mapped_preview_first() == 0);
    T(mapped_find_boundaries() == 0);
    T(gutter_estimates() == 0);
    char path[] = "build/editor-large-test-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    T(write_fixture(path) == 0);
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
    T(editor_close_tab(e, 0) == 0); /* evicts the first retained buffer */
    T(step_until(e, settled, UINT64_C(5000000000)) == 0);
    editor_close(e); unlink(path);
    puts("editor_large_test: ok");
    return 0;
}
