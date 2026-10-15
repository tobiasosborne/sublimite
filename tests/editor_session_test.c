/* Headless integration for Ctrl+S and crash recovery. Fixture byte lengths are
 * strlen(marker), geometry follows the status text plus gutter/chrome, and all
 * waits use EDITOR_IDLE_NS (G, existing loop-policy bound, not a latency claim). */
#include "editor/private.h"
#include "trace/trace.h"
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
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
static int open_fixture_at(fixture *f, bool journalling, bool missing, bool nested_target)
{
    strcpy(f->directory, "build/editor-session-XXXXXX"); T(mkdtemp(f->directory));
    if (nested_target) {
        T(snprintf(f->path, sizeof f->path, "%s/target", f->directory) > 0);
        T(mkdir(f->path, 0700) == 0); /* (P) owner read/write/search Unix bits */
    }
    T(snprintf(f->path, sizeof f->path, "%s/%ssource", f->directory, nested_target ? "target/" : "") > 0);
    T(snprintf(f->log, sizeof f->log, "%s/journal", f->directory) > 0);
    int fd;
    if (!missing) { fd = open(f->path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0); T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd); }
    T(render_null_backend(&f->backend) == 0);
    editor_config config = {.path = f->path, .journal_path = journalling ? f->log : NULL,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    T(editor_open(&f->e, &config, &f->backend) == 0); return settle(f->e);
}
static int open_fixture(fixture *f, bool journalling, bool missing)
{ return open_fixture_at(f, journalling, missing, false); }
static void close_fixture(fixture *f)
{
    editor_close(f->e); unlink(f->path); unlink(f->log);
    char lock[IPC_PATH_CAP];
    if (snprintf(lock, sizeof lock, "%s/.journal.lock", f->directory) > 0) (void)unlink(lock);
    (void)rmdir(f->directory);
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
    /* edit-97i: seed the legacy shared-target state directly in this save
     * fixture. Ordinary opens now select their existing tab. Temporarily
     * hide only the tab association, preserving the original buffer/controller
     * and journal base, then restore it before any editing or save assertion.
     * (P) origin index and empty path length are zero; existing path length
     * derives from the preserved buffer target. No production bypass exists. */
    const tabs_tab *original = editor_tab(f.e, 0);
    editor_buffer *owner = f.e->buffer;
    T(tabs_rename(&f.e->tabs, 0, original->title, original->title_len, "", 0) == 0);
    uint64_t id; T(editor_add_buffer(f.e, f.path, NULL, 0, &id) == 0); T(settle(f.e) == 0);
    T(tabs_rename(&f.e->tabs, 0, original->title, original->title_len, owner->path, strlen(owner->path)) == 0);
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
/* (E) Fault shapes: remove the temporary name after data sync, or close the
 * target directory after rename. Both fail after PREPARED. (P) Zero below
 * denotes C initialization, the first byte, or Unix success/absence. */
typedef struct retry_fault {
    const char *directory;
    struct stat identity;
    bool after_rename;
    atomic_bool removed;
} retry_fault;
static void remove_save_temp(void *ctx, int phase)
{
    retry_fault *fault = ctx;
    if (atomic_load(&fault->removed)) return;
    if (fault->after_rename) {
        if (phase != FILE_STEP_RENAMED) return;
        /* (E) Enumerate actual open descriptors, with no guessed scan bound.
         * Closing the writer's target directory injects its fsync failure. */
        DIR *descriptors = opendir("/proc/self/fd");
        if (!descriptors) return;
        struct dirent *entry;
        while ((entry = readdir(descriptors))) {
            char *end;
            long value = strtol(entry->d_name, &end, 10); /* (P) decimal procfs descriptor names */
            if (*end || value < 0 || value > INT_MAX) continue;
            int fd = (int)value;
            struct stat identity;
            if (!fstat(fd, &identity) && S_ISDIR(identity.st_mode) &&
                identity.st_dev == fault->identity.st_dev && identity.st_ino == fault->identity.st_ino &&
                close(fd) == 0) atomic_store(&fault->removed, true);
        }
        (void)closedir(descriptors); return;
    }
    if (phase != FILE_STEP_FSYNCED) return;
    DIR *directory = opendir(fault->directory);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (!strncmp(entry->d_name, ".source.edit-", strlen(".source.edit-")) &&
            unlinkat(dirfd(directory), entry->d_name, 0) == 0)
            atomic_store(&fault->removed, true);
    (void)closedir(directory);
}
static int recoverable_save_retry(bool other_tab, bool after_rename)
{
    fixture f = {0}; T(open_fixture_at(&f, true, false, after_rename) == 0);
    editor_buffer *owner = f.e->buffer;
    char target_directory[IPC_PATH_CAP];
    T(snprintf(target_directory, sizeof target_directory, "%s%s", f.directory, after_rename ? "/target" : "") > 0);
    retry_fault fault = {.directory = target_directory, .after_rename = after_rename};
    T(stat(target_directory, &fault.identity) == 0);
    T(savectl_set_step_hook(owner->save, remove_save_temp, &fault) == 0);
    T(press(f.e, 'x', 0, "x") == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    savectl_model model = savectl_get_model(owner->save);
    T(atomic_load(&fault.removed) && model.state == SAVECTL_FAILED && model.can_recover_cancel);
    T(model.needs_finish == after_rename);
    T(savectl_save_token(owner->save)->prepared && owner->save_error && !f.e->error);
    T(disk_is(f.path, after_rename ? "xbase" : "base") == 0);
    char retained[IPC_PATH_CAP]; strcpy(retained, savectl_save_token(owner->save)->previous_path);
    T(access(retained, F_OK) == 0);
    char other[IPC_PATH_CAP];
    if (other_tab) {
        T(snprintf(other, sizeof other, "%s/other", f.directory) > 0);
        uint64_t id; T(editor_add_buffer(f.e, other, NULL, 0, &id) == 0); T(settle(f.e) == 0);
    }
    if (!after_rename || other_tab) T(press(f.e, 'y', 0, "y") == 0);
    /* Park the bulk lane so checkpoint allocations cannot be counted as UI
     * allocations. (P) Zero allocations is the binding typing-path contract;
     * the zero job generation is unused by this fixture, as in repeated_save. */
    held_bulk bulk = {0};
    work_handle handle = work_submit(&f.e->pool, (work_job){hold_bulk, &bulk, 0, WORK_BULK}); T(handle.epoch);
    uint64_t limit = deadline();
    while (!atomic_load(&bulk.entered)) { T(trace_now_ns() < limit); sched_yield(); }
    edit_malloc_guard_begin(); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0);
    size_t allocations = edit_malloc_guard_end(); if (edit_malloc_guard_active()) T(!allocations);
    atomic_store(&bulk.release, true); T(saved(f.e) == 0);
    T(disk_is(other_tab ? other : f.path, other_tab ? "y" : after_rename ? "xbase" : "xybase") == 0);
    T(!savectl_get_model(owner->save).can_recover_cancel && access(retained, F_OK) < 0);
    T(!f.e->buffer->save_error && !f.e->buffer->operation_error && !f.e->error);
    T(!tabs_active(&f.e->tabs)->modified);
    if (after_rename && !other_tab) {
        T(press(f.e, 'y', 0, "y") == 0);
        T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
        T(disk_is(f.path, "xybase") == 0); T(!tabs_active(&f.e->tabs)->modified);
    }
    if (other_tab) {
        /* (E) Index zero names the original tab by fixture construction. */
        T(editor_select_tab(f.e, 0) == 0); T(settle(f.e) == 0);
        T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
        T(disk_is(f.path, "xbase") == 0); T(!tabs_active(&f.e->tabs)->modified);
        T(unlink(other) == 0);
    }
    close_fixture(&f);
    if (after_rename) T(rmdir(target_directory) == 0 && rmdir(f.directory) == 0);
    return 0;
}
/* edit-9u3: after the PREPARE hand-off tab controls run while the file write
 * continues. Closing the saving tab and then one more tab evicts it from a
 * closed history of capacity 1 (the smallest nonzero retention, so exactly one
 * further close evicts), the session metadata forgets its leaf, and save
 * completion must still finish with the bytes captured at Ctrl+S. */
static int evicted_saving_tab(void)
{
    fixture f = {0};
    strcpy(f.directory, "build/editor-session-XXXXXX"); T(mkdtemp(f.directory));
    T(snprintf(f.path, sizeof f.path, "%s/source", f.directory) > 0);
    T(snprintf(f.log, sizeof f.log, "%s/journal", f.directory) > 0);
    int fd = open(f.path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    T(render_null_backend(&f.backend) == 0);
    editor_config config = {.path = f.path, .journal_path = f.log, .closed_capacity = 1,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    T(editor_open(&f.e, &config, &f.backend) == 0); T(settle(f.e) == 0);
    uint64_t first, second;
    T(editor_add_buffer(f.e, NULL, NULL, 0, &first) == 0); T(settle(f.e) == 0);
    T(editor_add_buffer(f.e, NULL, NULL, 0, &second) == 0); T(settle(f.e) == 0);
    T(editor_select_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(press(f.e, 'x', 0, "x") == 0); editor_buffer *owner = f.e->buffer;
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(owner->save, hold, &held) == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(f.e, &held) == 0);
    T(!savectl_get_model(owner->save).journal_leased);
    T(editor_close_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(tabs_closed_count(&f.e->tabs) == 1 && tabs_closed_at(&f.e->tabs, 0)->id == owner->id);
    T(editor_close_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(tabs_closed_count(&f.e->tabs) == 1 && tabs_closed_at(&f.e->tabs, 0)->id != owner->id);
    T(owner->retired);
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0);
    T(!owner->save_error && !f.e->error && !f.e->stats.journal_error);
    T(press(f.e, 'y', 0, "y") == 0); T(editor_length(f.e) == strlen("y"));
    close_fixture(&f); return 0;
}
/* edit-5u2: as evicted_saving_tab, but a complete checkpoint (the one a
 * reload of another tab or a journal-repair flush takes) runs while the
 * evicted tab's write is still held. It must keep the saving buffer, so the
 * later finish succeeds, the file holds the Ctrl+S bytes, and recovery from
 * the resulting journal sees the evicted buffer only as a closed tab. */
static int checkpoint_during_evicted_save(void)
{
    fixture f = {0};
    strcpy(f.directory, "build/editor-session-XXXXXX"); T(mkdtemp(f.directory));
    T(snprintf(f.path, sizeof f.path, "%s/source", f.directory) > 0);
    T(snprintf(f.log, sizeof f.log, "%s/journal", f.directory) > 0);
    int fd = open(f.path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    T(render_null_backend(&f.backend) == 0);
    editor_config config = {.path = f.path, .journal_path = f.log, .closed_capacity = 1,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    T(editor_open(&f.e, &config, &f.backend) == 0); T(settle(f.e) == 0);
    uint64_t first, second;
    T(editor_add_buffer(f.e, NULL, NULL, 0, &first) == 0); T(settle(f.e) == 0);
    T(editor_add_buffer(f.e, NULL, NULL, 0, &second) == 0); T(settle(f.e) == 0);
    T(editor_select_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(press(f.e, 'x', 0, "x") == 0); editor_buffer *owner = f.e->buffer;
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(owner->save, hold, &held) == 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(f.e, &held) == 0);
    T(editor_close_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(editor_close_tab(f.e, 0) == 0); T(settle(f.e) == 0);
    T(owner->retired && tabs_closed_at(&f.e->tabs, 0)->id != owner->id);
    T(journal_flush(f.e->journal) == 0); T(editor_checkpoint(f.e) == 0);
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0);
    T(!owner->save_error && !f.e->error && !f.e->stats.journal_error);
    T(!editor_save_active(f.e) && !savectl_get_model(owner->save).needs_finish);
    T(press(f.e, 'y', 0, "y") == 0); T(editor_length(f.e) == strlen("y"));
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
    /* A killed file-write worker can leave its unpublished temp. This fixture
     * owns the directory and cleans those entries without changing file.c. */
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
        char path[IPC_PATH_CAP]; T(snprintf(path, sizeof path, "%s/%s", root, entry->d_name) > 0); unlink(path);
    }
    closedir(dir); T(rmdir(root) == 0); return 0;
}
/* edit-5u2: a process crash right after that complete checkpoint, while the
 * write is still held before rename. The Ctrl+S text of the evicted tab must
 * survive as a closed tab (edit-9u3's retained leaf); the disk still holds the
 * old bytes, so the recovered tab is modified. */
static int crash_after_evicted_checkpoint(void)
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
        editor_config config = {.path = source, .journal_path = first, .closed_capacity = 1,
            .cols = 32, .rows = 3}; /* status width + chrome; capacity 1 as in evicted_saving_tab */
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        uint64_t id;
        T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
        T(press(e, 'x', 0, "x") == 0); editor_buffer *owner = e->buffer;
        static held_save held = {.phase = FILE_STEP_TEMP_WRITTEN};
        T(savectl_set_step_hook(owner->save, hold, &held) == 0);
        T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(e, &held) == 0);
        T(editor_close_tab(e, 0) == 0); T(settle(e) == 0);
        T(editor_close_tab(e, 0) == 0); T(settle(e) == 0); T(owner->retired);
        T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0);
        _exit(0); /* crash: the held write never renames */
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    T(disk_is(source, "base") == 0);
    fd = mkstemp(next); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = next, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    bool found = false;
    for (size_t i = 0; i < tabs_closed_count(&e->tabs); i++) {
        const tabs_tab *tab = tabs_closed_at(&e->tabs, i);
        editor_buffer *b = (editor_buffer *)((uint8_t *)tab->undo - offsetof(editor_buffer, undo));
        uint8_t bytes[sizeof "xbase" - 1];
        if (strcmp(b->path, source) || piece_len(b->tree) != sizeof bytes) continue;
        T(piece_read(b->tree, 0, bytes, sizeof bytes) == 0);
        if (!memcmp(bytes, "xbase", sizeof bytes) && tab->modified) found = true;
    }
    T(found);
    editor_close(e); unlink(source); unlink(next);
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
    char lock[IPC_PATH_CAP]; T(snprintf(lock, sizeof lock, "%s/.journal.lock", root) > 0); unlink(lock);
    T(rmdir(root) == 0); return 0;
}
static int session_artifacts(void)
{
    char root[] = "build/editor-artifacts-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], previous[IPC_PATH_CAP] = "";
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    for (unsigned launch = 0; launch < 3; launch++) {
        char log[IPC_PATH_CAP]; T(snprintf(log, sizeof log, "%s/session-XXXXXX", root) > 0);
        fd = mkstemp(log); T(fd >= 0); T(fchmod(fd, 0644) == 0); close(fd);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.journal_path = log, .start_empty = true, .cols = 32, .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
        ipc_request request = {.count = 1}; request.paths[0] = (ipc_path){source, 1, 1};
        T(editor_open_request(e, &request, 0) == 0); T(tabs_count(&e->tabs) == 1);
        T(settle(e) == 0); editor_close(e);
        if (*previous) T(access(previous, F_OK) < 0);
        strcpy(previous, log);
        DIR *dir = opendir(root); T(dir); struct dirent *entry; unsigned journals = 0;
        while ((entry = readdir(dir))) if (!strncmp(entry->d_name, "session-", 8)) {
            struct stat st; T(fstatat(dirfd(dir), entry->d_name, &st, 0) == 0);
            T(S_ISREG(st.st_mode) && st.st_size > 0 && (st.st_mode & 0777) == 0600);
            journals++;
        }
        T(closedir(dir) == 0 && journals == 1);
    }
    /* Conflict during discovery must preserve the previous recovery source
     * without leaving this launch's empty destination behind. */
    fd = open(source, O_WRONLY | O_TRUNC); T(fd >= 0);
    T(write(fd, "changed", strlen("changed")) == (ssize_t)strlen("changed")); close(fd);
    char failed[IPC_PATH_CAP]; T(snprintf(failed, sizeof failed, "%s/session-XXXXXX", root) > 0);
    fd = mkstemp(failed); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = failed, .start_empty = true, .cols = 32, .rows = 3};
    /* edit-btj: a changed base never blocks launch. The source is renamed
     * aside (never deleted) and this launch's journal becomes CURRENT. */
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0 && e);
    T(access(previous, F_OK) < 0 && *e->recovery_aside && access(e->recovery_aside, F_OK) == 0);
    editor_close(e);
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        T(unlinkat(dirfd(dir), entry->d_name, 0) == 0);
    T(closedir(dir) == 0 && rmdir(root) == 0);
    puts("editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside");
    return 0;
}
/* edit-btj (Astra drive 2 task 12 and its ordinary profile): unsaved typing,
 * kill, then the base file is replaced (changed bytes) or rewritten with the
 * same bytes (new mtime/ctime, as the drive's fixture rewrite did). Launch must
 * succeed showing the disk text; the unchanged second buffer keeps its edits;
 * the journal holding the typed text survives under a set-aside name. */
static bool file_contains(const char *path, const char *needle)
{
    int fd = open(path, O_RDONLY); if (fd < 0) return false;
    struct stat st; if (fstat(fd, &st) || st.st_size <= 0) { close(fd); return false; }
    char *bytes = malloc((size_t)st.st_size); bool found = false;
    if (bytes && read(fd, bytes, (size_t)st.st_size) == st.st_size)
        for (size_t i = 0; !found && i + strlen(needle) <= (size_t)st.st_size; i++) found = !memcmp(bytes + i, needle, strlen(needle));
    free(bytes); close(fd); return found;
}
static int changed_base(bool same_bytes)
{
    char root[] = "build/editor-changed-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], other[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP], third[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0);
    T(snprintf(first, sizeof first, "%s/session-XXXXXX", root) > 0);
    T(snprintf(next, sizeof next, "%s/session-XXXXXX", root) > 0);
    T(snprintf(third, sizeof third, "%s/session-XXXXXX", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    fd = open(other, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "keep", strlen("keep")) == (ssize_t)strlen("keep")); close(fd);
    fd = mkstemp(first); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = first, .cols = 32, .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(press(e, 'M', 0, "MK") == 0); /* one INSERT record carries both bytes */
        uint64_t id; T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(press(e, 'x', 0, "x") == 0); _exit(0); /* no close: a crash leaves the journal */
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fd = open(source, O_WRONLY | O_TRUNC); T(fd >= 0);
    const char *disk = same_bytes ? "base" : "changed on disk";
    T(write(fd, disk, strlen(disk)) == (ssize_t)strlen(disk));
    /* A distinct mtime even on coarse clocks: one second back from now. */
    struct timespec when[2]; T(clock_gettime(CLOCK_REALTIME, &when[0]) == 0); when[0].tv_sec -= 1; when[1] = when[0];
    T(futimens(fd, when) == 0); close(fd);
    fd = mkstemp(next); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = next, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0 && e);
    T(tabs_count(&e->tabs) == 2);
    uint8_t bytes[32] = {0};
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    T(editor_length(e) == strlen(disk) && editor_read(e, 0, bytes, strlen(disk)) == 0 && !memcmp(bytes, disk, strlen(disk)));
    T(!editor_tab(e, 0)->modified);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    T(editor_length(e) == strlen("xkeep") && editor_read(e, 0, bytes, strlen("xkeep")) == 0 && !memcmp(bytes, "xkeep", strlen("xkeep")));
    T(editor_tab(e, 1)->modified);
    T(e->recovery_notice && access(first, F_OK) < 0);
    T(*e->recovery_aside && file_contains(e->recovery_aside, "MK"));
    char aside[IPC_PATH_CAP]; strcpy(aside, e->recovery_aside);
    editor_close(e);
    /* The next launch restores the replacement session normally. */
    fd = mkstemp(third); T(fd >= 0); close(fd);
    backend = (render_backend){0}; T(render_null_backend(&backend) == 0);
    config.journal_path = third; e = NULL;
    T(editor_open(&e, &config, &backend) == 0 && e && !e->recovery_notice && tabs_count(&e->tabs) == 2);
    T(editor_tab(e, 1)->modified && !editor_tab(e, 0)->modified && access(aside, F_OK) == 0);
    editor_close(e);
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        T(unlinkat(dirfd(dir), entry->d_name, 0) == 0);
    T(closedir(dir) == 0 && rmdir(root) == 0);
    return 0;
}
/* edit-rzj: an oversized discovered session stays discoverable without
 * partial installation; normal capacity on the next launch recovers it. */
static int install_failure(void)
{
    char root[] = "build/editor-install-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], other[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0);
    T(snprintf(first, sizeof first, "%s/session-XXXXXX", root) > 0);
    T(snprintf(next, sizeof next, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(first); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = first, .cols = 32, .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(press(e, 'M', 0, "MK") == 0);
        uint64_t id; T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(press(e, 'x', 0, "x") == 0); _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fd = mkstemp(next); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = next, .start_empty = true, .tab_capacity = 1, .closed_capacity = 1, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0 && e);
    T(e->recovery_notice && tabs_count(&e->tabs) == 0 && access(first, F_OK) == 0);
    T(!*e->recovery_aside && file_contains(first, "MK"));
    editor_close(e);
    /* (P) Zero selects the existing default capacity, which can fit both
     * fixture buffers; one slot above deliberately cannot fit them. */
    config.tab_capacity = 0; config.closed_capacity = 0;
    backend = (render_backend){0}; T(render_null_backend(&backend) == 0);
    T(editor_open(&e, &config, &backend) == 0 && e);
    T(!e->recovery_notice && tabs_count(&e->tabs) == 2); /* (P) source + other */
    T(access(first, F_OK) < 0);
    uint8_t bytes[sizeof "MK" - 1]; /* (P) Marker excludes its NUL. */
    T(editor_select_tab(e, 0) == 0);
    T(editor_read(e, 0, bytes, sizeof bytes) == 0 && !memcmp(bytes, "MK", sizeof bytes));
    editor_close(e);
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        T(unlinkat(dirfd(dir), entry->d_name, 0) == 0);
    T(closedir(dir) == 0 && rmdir(root) == 0);
    return 0;
}
/* edit-fm4 (Tobias's M0 profile, 2026-10-13): GB-scale orphan journals must
 * never be replayed, copied or set aside on the startup path. The fixture is a
 * private XDG_DATA_HOME holding one orphan session journal that is a real
 * checkpoint of a 1 GiB buffer (the brief's size, (P) 2^30 bytes; its source
 * is a sparse file, but every byte of it is read into the checkpoint, so the
 * journal itself is 1 GiB of written records) whose /tmp-corpus-like BASE has
 * since changed, plus a 1 GiB set-aside copy of it. Launch must reach input
 * accepted and leave every pre-existing profile file as it was (no replay, no
 * new set-aside); its time is printed against the G4a warm p99 (tracked). (G) G4a, perf/01-perf-target.md
 * §0.2: "Startup, software, warm | exec -> CPU back buffer complete + map
 * requested + input accepted | 25 / 40" ms p50/p99 (A panel). */
#define FM4_GIB (UINT64_C(1) << 30)          /* (P) the brief's 1 GiB fixture */
#define FM4_G4A_P99_NS UINT64_C(40000000)    /* (G) G4a warm p99 = 40 ms; tracked, not enforced */
typedef struct fm4_entry { char name[256]; struct stat st; } fm4_entry;
static size_t fm4_list(const char *dir, fm4_entry *out, size_t cap)
{
    DIR *d = opendir(dir); if (!d) return SIZE_MAX;
    size_t n = 0; struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (n == cap) { closedir(d); return SIZE_MAX; }
        strcpy(out[n].name, entry->d_name);
        if (fstatat(dirfd(d), entry->d_name, &out[n].st, AT_SYMLINK_NOFOLLOW)) { closedir(d); return SIZE_MAX; }
        n++;
    }
    closedir(d); return n;
}
static void fm4_remove_tree(const char *root)
{
    char dir[IPC_PATH_CAP];
    if (snprintf(dir, sizeof dir, "%s/sublimite", root) > 0) {
        DIR *d = opendir(dir); struct dirent *entry;
        if (d) {
            while ((entry = readdir(d))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                (void)unlinkat(dirfd(d), entry->d_name, 0);
            closedir(d);
        }
        (void)rmdir(dir);
    }
    DIR *d = opendir(root); struct dirent *entry;
    if (d) {
        while ((entry = readdir(d))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            (void)unlinkat(dirfd(d), entry->d_name, 0);
        closedir(d);
    }
    (void)rmdir(root);
}
/* Child: open the corpus file, type one byte, checkpoint, crash. */
static int fm4_orphan(const char *dir, const char *source, char *journal_path)
{
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        int lease = -1;
        if (journal_session_create(dir, journal_path, IPC_PATH_CAP, &lease)) _exit(2);
        render_backend backend = {0}; if (render_null_backend(&backend)) _exit(3);
        editor_config config = {.path = source, .journal_path = journal_path, .session_lease = &lease, .cols = 32, .rows = 3};
        editor *e = NULL; if (editor_open(&e, &config, &backend) || settle(e)) _exit(4);
        uint64_t limit = deadline();
        while (e->buffer->pending_open) { if (turn(e) || trace_now_ns() >= limit) _exit(5); sched_yield(); }
        if (press(e, 'x', 0, "x") || journal_flush(e->journal) || editor_checkpoint(e)) _exit(6);
        _exit(0); /* crash: the 1 GiB checkpoint is an orphan */
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* The child's name is mkstemp's: find the one session-* without a dot. */
    DIR *d = opendir(dir); T(d); struct dirent *entry; int found = 0;
    while ((entry = readdir(d))) if (!strncmp(entry->d_name, "session-", 8) && !strchr(entry->d_name, '.')) {
        T(snprintf(journal_path, IPC_PATH_CAP, "%s/%s", dir, entry->d_name) > 0); found++;
    }
    closedir(d); T(found == 1); return 0;
}
static int large_orphans_body(const char *root)
{
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], orphan[IPC_PATH_CAP], aside[IPC_PATH_CAP];
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root)) && !strcmp(dir + strlen(root), "/sublimite")); /* never the real profile */
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(snprintf(source, sizeof source, "%s/corpus", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(ftruncate(fd, (off_t)FM4_GIB) == 0); close(fd);
    T(fm4_orphan(dir, source, orphan) == 0);
    struct stat st; T(stat(orphan, &st) == 0 && (uint64_t)st.st_size > FM4_GIB);
    /* The set-aside copy: same bytes under editor_set_aside's naming. */
    int in = open(orphan, O_RDONLY); T(in >= 0);
    T(snprintf(aside, sizeof aside, "%s.set-aside-1", orphan) > 0);
    int out = open(aside, O_CREAT | O_EXCL | O_WRONLY, 0600); T(out >= 0);
    for (off_t done = 0; done < st.st_size;) {
        ssize_t n = copy_file_range(in, NULL, out, NULL, (size_t)(st.st_size - done), 0);
        T(n > 0); done += n;
    }
    close(in); close(out);
    char renamed[IPC_PATH_CAP]; strcpy(renamed, orphan);
    memcpy(strrchr(renamed, '/') + 1, "session-OLDSET", strlen("session-OLDSET"));
    T(snprintf(aside, sizeof aside, "%s.set-aside-1", renamed) > 0);
    char copied[IPC_PATH_CAP]; T(snprintf(copied, sizeof copied, "%s.set-aside-1", orphan) > 0);
    T(rename(copied, aside) == 0);
    /* The corpus file changed since the orphan was written (BASE mismatch). */
    fd = open(source, O_WRONLY); T(fd >= 0); T(pwrite(fd, "changed", strlen("changed"), 0) == (ssize_t)strlen("changed"));
    close(fd);
    fm4_entry before[64], after[64]; /* (P) fixture holds 6 names; headroom for the launch's own */
    size_t nbefore = fm4_list(dir, before, 64); T(nbefore != SIZE_MAX);
    /* Launch exactly as main.c does: leased session, start_empty. */
    char journal_path[IPC_PATH_CAP]; int lease = -1;
    T(journal_session_create(dir, journal_path, sizeof journal_path, &lease) == JOURNAL_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = journal_path, .session_lease = &lease, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL;
    uint64_t t0 = trace_now_ns();
    int rc = editor_open(&e, &config, &backend);
    if (!rc) rc = settle(e);
    uint64_t elapsed = trace_now_ns() - t0;
    T(rc == 0 && e);
    /* (P) one orphan session in the fixture; the set-aside is not discoverable. */
    T(e->recovery_notice != NULL && e->recovery_deferred == 1 && tabs_count(&e->tabs) == 0);
    T(press(e, 'y', 0, "y") == 0); /* input accepted */
    /* Law 1: G1 is the single enforced perf guard; G4a is a tracked number. */
    char power[32] = "unknown", load1[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fscanf(f, "%31s", power) != 1) strcpy(power, "unknown"); fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%31s", load1) != 1) strcpy(load1, "unknown"); fclose(f); }
    printf("large_orphans: launch_ms=%.3f (M) G4a_p99=%.0f bound=tracked power=%s load1=%s\n",
        (double)elapsed / 1e6, (double)FM4_G4A_P99_NS / 1e6, power, load1);
    editor_close(e);
    if (!lstat(journal_path, &st) && !st.st_size) (void)unlink(journal_path);
    (void)journal_session_release(journal_path, lease); close(lease);
    size_t nafter = fm4_list(dir, after, 64); T(nafter != SIZE_MAX);
    /* Every pre-existing file is still there, same inode, size and mtime; no
     * new set-aside appeared. The launch's own journal may remain. */
    for (size_t i = 0; i < nbefore; i++) {
        if (S_ISREG(before[i].st.st_mode) && !before[i].st.st_size && strstr(before[i].name, ".lock")) continue;
        bool kept = false;
        for (size_t j = 0; j < nafter; j++) if (!strcmp(before[i].name, after[j].name))
            kept = after[j].st.st_ino == before[i].st.st_ino && after[j].st.st_size == before[i].st.st_size &&
                   after[j].st.st_mtim.tv_sec == before[i].st.st_mtim.tv_sec && after[j].st.st_mtim.tv_nsec == before[i].st.st_mtim.tv_nsec;
        if (!kept) fprintf(stderr, "editor_session_test: fm4 profile changed: %s\n", before[i].name);
        T(kept);
    }
    for (size_t j = 0; j < nafter; j++) {
        bool old = false;
        for (size_t i = 0; i < nbefore; i++) old = old || !strcmp(before[i].name, after[j].name);
        if (!old && strstr(after[j].name, ".set-aside-")) fprintf(stderr, "editor_session_test: fm4 new set-aside %s\n", after[j].name);
        T(old || !strstr(after[j].name, ".set-aside-"));
    }
    return 0;
}
static int large_orphans(void)
{
    char made[] = "build/editor-fm4-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved) > 0);
    int rc = large_orphans_body(root);
    fm4_remove_tree(root); /* on success and on every failure path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    return rc;
}
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(large_orphans() == 0);
    T(recoverable_save_retry(false, false) == 0); T(recoverable_save_retry(true, false) == 0);
    T(recoverable_save_retry(false, true) == 0); T(recoverable_save_retry(true, true) == 0);
    T(session_artifacts() == 0); T(changed_base(false) == 0); T(changed_base(true) == 0); T(install_failure() == 0);
    T(unavailable_target_parent() == 0); T(existing_journal() == 0);
    T(repeated_save(false, false) == 0); T(repeated_save(true, false) == 0); T(repeated_save(true, true) == 0);
    T(interrupted_save(FILE_STEP_TEMP_WRITTEN) == 0); T(interrupted_save(FILE_STEP_RENAMED) == 0);
    T(post_cutoff() == 0); T(saving_frame_busy() == 0); T(shared_target() == 0); T(failed_inactive() == 0);
    T(evicted_saving_tab() == 0); T(checkpoint_during_evicted_save() == 0);
    T(crash_after_evicted_checkpoint() == 0);
    puts("editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed");
    return 0;
}
