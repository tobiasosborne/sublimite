/* edit-457.10 / P4.10: large-file behavioural contract through the editor loop.
 * A ~1.6 MB generated fixture is forced into mmap mode (copy_threshold 4 KiB),
 * so the estimate -> exact swap, find, save and background-worker paths run in
 * `make check`. The 1 GiB / 10 GiB rows live in bench/editor_large_bench.c. */
#include "editor/editor.h"
#include "editor/large.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "base/base.h"
#include "trace/trace.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_large_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
#define A_LINES 20000u
#define B_LINES 1500u
#define B_LEN 1000u
#define TOTAL_LINES (A_LINES + B_LINES + 1u)
#define LOOSE_STEP_NS UINT64_C(250000000)   /* loose assert, loaded box */

typedef struct hooks { bool active, suspended; size_t allocations, frames; } hooks;
static void ingress(void *c, uint64_t s, uint64_t n) { hooks *h = c; (void)s; (void)n; if (!h->active) { edit_malloc_guard_begin(); h->active = true; } }
static void submitted(void *c, const editor_frame *f) { hooks *h = c; (void)f; if (h->active) { h->allocations += edit_malloc_guard_end(); h->active = false; } h->frames++; }
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

int main(void)
{
    trace_init(); (void)trace_thread_register();
    char path[] = "build/editor-large-test-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    T(write_fixture(path) == 0);
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
