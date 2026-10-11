/* Headless integration for Ctrl+S and crash recovery. Fixture byte lengths are
 * strlen(marker), geometry follows the status text plus gutter/chrome, and all
 * waits use EDITOR_IDLE_NS (G, existing loop-policy bound, not a latency claim). */
#include "editor/private.h"
#include "trace/trace.h"
#include <fcntl.h>
#include <dirent.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "editor_session_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct fixture {
    char directory[IPC_PATH_CAP], path[IPC_PATH_CAP], log[IPC_PATH_CAP];
    render_backend backend;
    editor *e;
} fixture;
static uint64_t deadline(void) { return trace_now_ns() + EDITOR_IDLE_NS; }
static int turn(editor *e) { int rc = editor_step(e, 0); return rc == EDITOR_OK || rc == EDITOR_MORE ? 0 : rc; }
static int settle(editor *e)
{
    uint64_t limit = deadline();
    do { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int press(editor *e, uint32_t symbol, uint16_t mods, const char *text)
{
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = symbol, .mods = mods, .t0_ns = trace_now_ns()};
    if (text) { event.utf8_len = (uint8_t)strlen(text); memcpy(event.utf8, text, event.utf8_len); }
    T(editor_inject(e, &event) == 0); return settle(e);
}
static int saved(editor *e)
{
    uint64_t limit = deadline();
    while (editor_save_active(e)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return settle(e);
}
static int open_fixture(fixture *f, bool journalling, bool missing)
{
    strcpy(f->directory, "build/editor-session-XXXXXX"); T(mkdtemp(f->directory));
    T(snprintf(f->path, sizeof f->path, "%s/source", f->directory) > 0);
    T(snprintf(f->log, sizeof f->log, "%s/journal", f->directory) > 0);
    int fd;
    if (!missing) { fd = open(f->path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0); T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd); }
    T(render_null_backend(&f->backend) == 0);
    editor_config config = {.path = f->path, .journal_path = journalling ? f->log : NULL,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    T(editor_open(&f->e, &config, &f->backend) == 0); return settle(f->e);
}
static void close_fixture(fixture *f)
{
    editor_close(f->e); unlink(f->path); unlink(f->log); (void)rmdir(f->directory);
}
static int disk_is(const char *path, const char *expected)
{
    int fd = open(path, O_RDONLY); T(fd >= 0);
    size_t len = strlen(expected); uint8_t *bytes = malloc(len + 1); T(bytes);
    ssize_t n = read(fd, bytes, len + 1); close(fd);
    bool match = n == (ssize_t)len && !memcmp(bytes, expected, len); free(bytes); T(match); return 0;
}
typedef struct held_save { atomic_bool entered, release; int phase; } held_save;
static void hold(void *ctx, int phase)
{
    held_save *h = ctx; if (phase != h->phase) return;
    atomic_store(&h->entered, true); while (!atomic_load(&h->release)) sched_yield();
}
static int enter_hold(editor *e, held_save *h)
{
    uint64_t limit = deadline();
    while (!atomic_load(&h->entered)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return 0;
}
typedef struct held_bulk { atomic_bool entered, release; } held_bulk;
static void hold_bulk(work_ctx *ctx)
{
    held_bulk *h = ctx->arg; atomic_store(&h->entered, true);
    while (!atomic_load(&h->release) && !work_should_stop(ctx)) sched_yield();
}
static int repeated_save(bool journalling, bool missing)
{
    fixture f = {0}; T(open_fixture(&f, journalling, missing) == 0);
    T(press(f.e, 'x', 0, "x") == 0);
    /* The interposer is process-wide. Hold the bulk lane so legitimate
     * worker checkpoint allocations cannot be attributed to the UI request. */
    held_bulk bulk = {0}; work_handle handle = work_submit(&f.e->pool, (work_job){hold_bulk, &bulk, 0, WORK_BULK}); T(handle.epoch);
    uint64_t limit = deadline();
    while (!atomic_load(&bulk.entered)) { T(trace_now_ns() < limit); sched_yield(); }
    edit_malloc_guard_begin(); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0);
    size_t allocations = edit_malloc_guard_end(); if (edit_malloc_guard_active()) T(!allocations);
    atomic_store(&bulk.release, true);
    T(saved(f.e) == 0); T(disk_is(f.path, missing ? "x" : "xbase") == 0);
    T(!editor_tab(f.e, 0)->modified && !f.e->buffer->operation_error);
    T(press(f.e, 'y', 0, "y") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    T(disk_is(f.path, missing ? "xy" : "xybase") == 0); T(!editor_tab(f.e, 0)->modified);
    close_fixture(&f); return 0;
}
static int post_cutoff(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    T(press(f.e, 'x', 0, "x") == 0);
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN};
    T(savectl_set_step_hook(f.e->buffer->save, hold, &held) == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(f.e, &held) == 0);
    savectl_model model = savectl_get_model(f.e->buffer->save);
    T(model.busy && !model.journal_leased && !model.saving_frame_pending);
    T(model.saving_submitted_ns >= model.request_ns);
    T(disk_is(f.path, "base") == 0);
    T(press(f.e, 'y', 0, "y") == 0); T(editor_tab(f.e, 0)->modified);
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0); T(editor_tab(f.e, 0)->modified);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    T(disk_is(f.path, "xybase") == 0); T(!editor_tab(f.e, 0)->modified);
    close_fixture(&f); return 0;
}
static int refuse_submit(render_backend *backend, const render_grid *grid, const render_strip *strips, size_t count)
{ (void)backend; (void)grid; (void)strips; (void)count; return RENDER_ERR_BUSY; }
static int saving_frame_busy(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0); T(press(f.e, 'x', 0, "x") == 0);
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(f.e->buffer->save, hold, &held) == 0);
    int (*submit)(render_backend *, const render_grid *, const render_strip *, size_t) = f.backend.ops.submit;
    f.backend.ops.submit = refuse_submit;
    uint32_t before = f.e->grid.frame_id;
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = 's', .mods = PLAT_MOD_CTRL, .t0_ns = trace_now_ns()};
    T(editor_inject(f.e, &event) == 0); T(enter_hold(f.e, &held) == 0);
    savectl_model model = savectl_get_model(f.e->buffer->save);
    T(model.saving_frame_pending && !model.saving_submitted_ns);
    T(f.e->grid.frame_id == before + 1); /* G: render_frame_begin advances once per admitted frame */
    const char *status = "saving"; size_t row = (size_t)(f.e->grid.dims.rows - 1) * f.e->grid.dims.cols;
    for (size_t i = 0; i < strlen(status); i++) T(f.e->grid.cells[row + i].glyph_index == (uint8_t)status[i]);
    f.backend.ops.submit = submit; T(settle(f.e) == 0);
    model = savectl_get_model(f.e->buffer->save);
    T(!model.saving_frame_pending && model.saving_submitted_ns >= event.t0_ns);
    T(f.e->grid.frame_id == before + 1);
    atomic_store(&held.release, true); T(saved(f.e) == 0); close_fixture(&f); return 0;
}
static int shared_target(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    uint64_t id; T(editor_add_buffer(f.e, f.path, NULL, 0, &id) == 0); T(settle(f.e) == 0);
    T(press(f.e, 'z', 0, "z") == 0); T(editor_select_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(press(f.e, 'x', 0, "x") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    T(!editor_tab(f.e, 0)->modified && editor_tab(f.e, 1)->modified);
    T(editor_select_tab(f.e, 1) == 0); T(settle(f.e) == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0); T(disk_is(f.path, "zbase") == 0);
    T(!f.e->buffer->operation_error && !editor_tab(f.e, 1)->modified);
    close_fixture(&f); return 0;
}
static int failed_inactive(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    T(press(f.e, 'x', 0, "x") == 0); editor_buffer *owner = f.e->buffer;
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(owner->save, hold, &held) == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(f.e, &held) == 0);
    uint64_t id; T(editor_add_buffer(f.e, NULL, NULL, 0, &id) == 0); T(settle(f.e) == 0);
    int fd = open(f.path, O_WRONLY | O_TRUNC); T(fd >= 0); T(write(fd, "external", strlen("external")) == (ssize_t)strlen("external")); close(fd);
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(owner->save_error && !f.e->buffer->save_error && !f.e->error);
    T(press(f.e, 'y', 0, "y") == 0); T(editor_length(f.e) == strlen("y"));
    T(disk_is(f.path, "external") == 0);
    close_fixture(&f); return 0;
}
static void crash_at_step(void *ctx, int phase)
{ if (phase == *(const int *)ctx) _exit(0); }
static int interrupted_save(int phase)
{
    char root[] = "build/editor-crash-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(first, sizeof first, "%s/session-XXXXXX", root) > 0);
    T(snprintf(next, sizeof next, "%s/session-XXXXXX", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    fd = mkstemp(first); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = first, .cols = 32, .rows = 3}; /* status width + chrome */
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(press(e, 'x', 0, "x") == 0);
        T(savectl_set_step_hook(e->buffer->save, crash_at_step, &phase) == 0);
        T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0);
        uint64_t limit = deadline();
        for (;;) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fd = mkstemp(next); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = next, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    T(tabs_count(&e->tabs) == 1); uint8_t bytes[sizeof "xbase" - 1];
    T(editor_read(e, 0, bytes, sizeof bytes) == 0 && !memcmp(bytes, "xbase", sizeof bytes));
    T(editor_tab(e, 0)->modified); T(access(first, F_OK) < 0);
    T(settle(e) == 0); T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(disk_is(source, "xbase") == 0 && !editor_tab(e, 0)->modified);
    editor_close(e); unlink(source); unlink(next);
    char lock[IPC_PATH_CAP]; T(snprintf(lock, sizeof lock, "%s.lock", next) > 0); unlink(lock);
    /* A killed file-write worker can leave its unpublished temp. This fixture
     * owns the directory and cleans those entries without changing file.c. */
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
        char path[IPC_PATH_CAP]; T(snprintf(path, sizeof path, "%s/%s", root, entry->d_name) > 0); unlink(path);
    }
    closedir(dir); T(rmdir(root) == 0); return 0;
}
static int unavailable_target_parent(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    char missing[IPC_PATH_CAP]; T(snprintf(missing, sizeof missing, "%s/missing/source", f.directory) > 0);
    uint64_t id; T(editor_add_buffer(f.e, missing, NULL, 0, &id) == 0); T(settle(f.e) == 0);
    T(press(f.e, 'y', 0, "y") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0);
    T(f.e->buffer->save_error && !f.e->error && editor_tab(f.e, tabs_active_index(&f.e->tabs))->modified);
    T(editor_select_tab(f.e, 0) == 0); T(settle(f.e) == 0); T(press(f.e, 'x', 0, "x") == 0);
    editor_close(f.e); f.e = NULL; f.backend = (render_backend){0};
    T(render_null_backend(&f.backend) == 0);
    editor_config config = {.journal_path = f.log, .start_empty = true, .cols = 32, .rows = 3};
    T(editor_open(&f.e, &config, &f.backend) == 0); T(settle(f.e) == 0);
    T(editor_select_tab(f.e, 1) == 0); T(settle(f.e) == 0);
    uint8_t byte; T(editor_read(f.e, 0, &byte, sizeof byte) == 0 && byte == 'y');
    T(f.e->buffer->save_error && !f.e->error && editor_tab(f.e, 1)->modified);
    close_fixture(&f); return 0;
}
static int existing_journal(void)
{
    char root[] = "build/editor-existing-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/journal", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(press(e, 'x', 0, "x") == 0); _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    uint8_t bytes[sizeof "xbase" - 1];
    T(editor_read(e, 0, bytes, sizeof bytes) == 0 && !memcmp(bytes, "xbase", sizeof bytes));
    T(editor_tab(e, 0)->modified); T(settle(e) == 0);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0); T(disk_is(source, "xbase") == 0);
    editor_close(e); unlink(source); unlink(log);
    char lock[IPC_PATH_CAP]; T(snprintf(lock, sizeof lock, "%s.lock", log) > 0); unlink(lock);
    T(rmdir(root) == 0); return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(unavailable_target_parent() == 0); T(existing_journal() == 0);
    T(repeated_save(false, false) == 0); T(repeated_save(true, false) == 0); T(repeated_save(true, true) == 0);
    T(interrupted_save(FILE_STEP_TEMP_WRITTEN) == 0); T(interrupted_save(FILE_STEP_RENAMED) == 0);
    T(post_cutoff() == 0); T(saving_frame_busy() == 0); T(shared_target() == 0); T(failed_inactive() == 0);
    puts("editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed");
    return 0;
}
