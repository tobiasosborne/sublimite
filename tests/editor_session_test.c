/* Headless integration for Ctrl+S and crash recovery. Fixture byte lengths are
 * strlen(marker), geometry follows the status text plus gutter/chrome, and all
 * waits use EDITOR_IDLE_NS (G, existing loop-policy bound, not a latency claim). */
#include "editor/private.h"
#include "trace/trace.h"
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/file.h>
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
static int open_fixture_threshold(fixture *f, bool journalling, bool missing, bool nested_target, uint64_t threshold)
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
        .copy_threshold = threshold,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    T(editor_open(&f->e, &config, &f->backend) == 0); return settle(f->e);
}
/* (P) zero selects file_default_copy_threshold(). */
static int open_fixture_at(fixture *f, bool journalling, bool missing, bool nested_target)
{ return open_fixture_threshold(f, journalling, missing, nested_target, 0); }
static int open_fixture(fixture *f, bool journalling, bool missing)
{ return open_fixture_at(f, journalling, missing, false); }
static void close_fixture(fixture *f)
{
    editor_close(f->e); unlink(f->path); unlink(f->log);
    char lock[IPC_PATH_CAP];
    if (snprintf(lock, sizeof lock, "%s/.journal.lock", f->directory) > 0) (void)unlink(lock);
    /* edit-fi2: a hot exit with unsaved edits keeps the retained BASE copy
     * its journal names (`.sublimite-base-*` beside the journal). */
    DIR *dir = opendir(f->directory); struct dirent *entry;
    while (dir && (entry = readdir(dir))) if (!strncmp(entry->d_name, ".sublimite-base-", strlen(".sublimite-base-")))
        (void)unlinkat(dirfd(dir), entry->d_name, 0);
    if (dir) closedir(dir);
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
/* edit-fkpk (recovery-3 F5): an original acquired as MMAP saved twice. The
 * first save renames over the pathname, which updates the retained inode's
 * ctime (link count 1 -> 0); the second save must still validate the mapped
 * source. (P) copy_threshold 1 selects MMAP for the strlen("base")-byte file
 * (size >= threshold); three saves cover the refresh being repeatable. */
static int mapped_repeated_save(bool journalling)
{
    fixture f = {0}; T(open_fixture_threshold(&f, journalling, false, false, 1) == 0);
    uint64_t limit = deadline();
    while (f.e->buffer->pending_open) { T(turn(f.e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(f.e->buffer->file && file_open_mode(f.e->buffer->file) == FILE_MODE_MMAP);
    T(press(f.e, 'x', 0, "x") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0); T(!editor_tab(f.e, 0)->modified && !f.e->buffer->operation_error);
    T(press(f.e, 'y', 0, "y") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    if (f.e->buffer->save_error) fprintf(stderr, "editor_session_test: fkpk second save error=%d journal=%d\n", f.e->buffer->save_error, (int)journalling);
    T(!f.e->buffer->save_error); T(disk_is(f.path, "xybase") == 0); T(!editor_tab(f.e, 0)->modified);
    T(press(f.e, 'z', 0, "z") == 0); T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f.e) == 0);
    T(!f.e->buffer->save_error); T(disk_is(f.path, "xyzbase") == 0); T(!editor_tab(f.e, 0)->modified);
    close_fixture(&f); return 0;
}
/* edit-ut0m: (P) sizeof "" is the smallest nonzero copy threshold, so
 * strlen("base") selects MMAP. The first edit makes the source dirty and
 * exposes Keep instead of automatic reload. All lengths derive from literals;
 * zero is API success, the first tab/byte offset, or an empty initializer. */
static int mapped_keep_save_body(fixture *f)
{
    uint64_t limit = deadline();
    while (f->e->buffer->pending_open) { T(turn(f->e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(f->e->buffer->file && file_open_mode(f->e->buffer->file) == FILE_MODE_MMAP);
    T(press(f->e, 'x', 0, "x") == 0);
    char replacement[IPC_PATH_CAP];
    T(snprintf(replacement, sizeof replacement, "%s/replacement", f->directory) > 0);
    int fd = open(replacement, O_WRONLY | O_CREAT | O_EXCL, 0600); /* (P) owner read/write */
    T(fd >= 0);
    ssize_t written = write(fd, "disk", strlen("disk"));
    int closed = close(fd);
    T(written == (ssize_t)strlen("disk") && closed == 0);
    T(rename(replacement, f->path) == 0);
    limit = deadline();
    while (!f->e->buffer->source_stale) { T(turn(f->e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(editor_source_resolve(f->e, false) == 0); T(settle(f->e) == 0);
    T(!f->e->buffer->source_stale && editor_tab(f->e, 0)->modified);
    T(disk_is(f->path, "disk") == 0);
    T(press(f->e, 'y', 0, "y") == 0);
    T(press(f->e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f->e) == 0);
    fprintf(stderr, "edit-ut0m: save after mapped rename-over/Keep error=%d journal=%s\n",
        f->e->buffer->save_error, f->e->journal ? "on" : "off");
    T(!f->e->buffer->save_error);
    T(disk_is(f->path, "xybase") == 0); T(!editor_tab(f->e, 0)->modified);
    T(press(f->e, 'z', 0, "z") == 0);
    T(press(f->e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(f->e) == 0);
    T(!f->e->buffer->save_error && !editor_tab(f->e, 0)->modified);
    T(disk_is(f->path, "xyzbase") == 0);
    return 0;
}
/* edit-3620: Keep after an external rename-over of a Save As target whose
 * name can no longer be examined. A search-denied parent makes
 * file_id_stat_path(save_path) return FILE_ERR_IO (EACCES, never accepted as
 * absence); Keep must retain the prompt and say so instead of rebasing
 * nothing and dismissing. Journal-less COPY (the default threshold for the
 * strlen("base")-byte fixture) is the configuration where this stat is the
 * first one Keep performs. (P) 0700 owner rwx, 0 no access; zero is API
 * success or the first tab. Root ignores search permission: skipped. */
static int kept_target_unreadable(void)
{
    if (geteuid() == 0) { puts("edit-3620: skipped (root bypasses directory search permission)"); return 0; }
    fixture f = {0}; T(open_fixture(&f, false, false) == 0);
    char parent[IPC_PATH_CAP], target[IPC_PATH_CAP], replacement[IPC_PATH_CAP];
    T(snprintf(parent, sizeof parent, "%s/target", f.directory) > 0);
    T(snprintf(target, sizeof target, "%s/saved", parent) > 0);
    T(snprintf(replacement, sizeof replacement, "%s/replacement", f.directory) > 0);
    T(mkdir(parent, 0700) == 0);
    editor *e = f.e; editor_buffer *b = e->buffer;
    T(file_open_mode(b->file) == FILE_MODE_COPY);
    e->file_command = KEYS_ACTION_SAVE_AS; strcpy(e->file_command_path, target);
    e->file_command_owner = b->id; e->file_command_generation = b->source_generation;
    uint64_t limit = deadline();
    while (e->file_command != KEYS_ACTION_NONE || e->save_as || !b->target_rebound) {
        T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield();
    }
    T(settle(e) == 0); T(disk_is(target, "base") == 0);
    T(press(e, 'x', 0, "x") == 0); T(editor_tab(e, 0)->modified);
    int fd = open(replacement, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    ssize_t written = write(fd, "disk", strlen("disk")); int closed = close(fd);
    T(written == (ssize_t)strlen("disk") && closed == 0);
    T(rename(replacement, target) == 0);
    limit = deadline();
    while (!b->source_stale) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0);
    file_id probe; T(chmod(parent, 0) == 0);
    int stat_rc = file_id_stat_path(target, &probe);
    e->recovery_notice = NULL;
    int resolve_rc = editor_source_resolve(e, false);
    int settle_rc = resolve_rc ? 0 : settle(e);
    bool stale = b->source_stale; const char *notice = e->recovery_notice;
    T(chmod(parent, 0700) == 0);
    fprintf(stderr, "edit-3620: Keep with unreadable target stat=%d stale=%s notice=%s\n",
        stat_rc, stale ? "yes" : "no", notice ? notice : "(none)");
    T(stat_rc == FILE_ERR_IO && resolve_rc == 0 && settle_rc == 0);
    T(stale && notice && *notice); T(editor_tab(e, 0)->modified);
    /* The prompt stays answerable: once the name is readable again, Keep
     * rebases onto the replacement and the next save writes the buffer. */
    T(editor_source_resolve(e, false) == 0); T(settle(e) == 0);
    T(!b->source_stale && editor_tab(e, 0)->modified && e->recovery_notice != notice);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0); /* the save's own rename no longer reloads b (edit-dxas) */
    T(!e->buffer->save_error && !editor_tab(e, 0)->modified); T(disk_is(target, "xbase") == 0);
    (void)unlink(target); (void)unlink(replacement); (void)rmdir(parent);
    close_fixture(&f); return 0;
}
static int mapped_keep_save(bool journalling)
{
    fixture f = {0};
    int rc = open_fixture_threshold(&f, journalling, false, false, sizeof "");
    if (!rc) rc = mapped_keep_save_body(&f);
    close_fixture(&f);
    char replacement[IPC_PATH_CAP];
    if (snprintf(replacement, sizeof replacement, "%s/replacement", f.directory) > 0) (void)unlink(replacement);
    (void)rmdir(f.directory);
    return rc;
}
/* edit-dxas: Save As, an edit, a plain Ctrl+S, then Ctrl+Z. The save's own
 * rename over the Save As target must not read as an external change: no
 * clean reload replaces the editor_buffer, and Ctrl+Z still undoes the edit
 * typed before the save. Variants: journal on/off, COPY (threshold zero, the
 * default for the strlen("base")-byte fixture) or MMAP ((P) sizeof "" is the
 * smallest nonzero threshold), and a new or an existing target. Zero is API
 * success, the first tab or byte offset. */
static int save_as_then_save_body(fixture *f, bool existing)
{
    editor *e = f->e;
    uint64_t limit = deadline();
    while (e->buffer->pending_open) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    editor_buffer *b = e->buffer; /* compared, never dereferenced, once replaced */
    bool mapped = b->file && file_open_mode(b->file) == FILE_MODE_MMAP;
    char target[IPC_PATH_CAP];
    T(snprintf(target, sizeof target, "%s/saved", f->directory) > 0);
    if (existing) {
        int fd = open(target, O_WRONLY | O_CREAT | O_EXCL, 0600); /* (P) owner read/write */
        T(fd >= 0);
        ssize_t written = write(fd, "old", strlen("old")); int closed = close(fd);
        T(written == (ssize_t)strlen("old") && closed == 0);
    }
    e->file_command = KEYS_ACTION_SAVE_AS; strcpy(e->file_command_path, target);
    e->file_command_owner = b->id; e->file_command_generation = b->source_generation;
    limit = deadline();
    while (e->file_command != KEYS_ACTION_NONE || e->save_as || !b->target_rebound || e->publish_owed || e->publish) {
        T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield();
    }
    T(settle(e) == 0); T(disk_is(target, "base") == 0);
    T(e->buffer == b && !editor_tab(e, 0)->modified);
    T(press(e, 'x', 0, "x") == 0); T(editor_tab(e, 0)->modified);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(disk_is(target, "xbase") == 0);
    bool replaced = e->buffer != b;
    T(press(e, 'z', PLAT_MOD_CTRL, NULL) == 0);
    uint8_t head[sizeof "base" - 1u] = {0};
    bool undone = piece_len(e->buffer->tree) == sizeof head && !piece_read(e->buffer->tree, 0, head, sizeof head) &&
        !memcmp(head, "base", sizeof head);
    fprintf(stderr, "edit-dxas: journal=%s mode=%s target=%s replaced=%s undone=%s\n",
        e->journal ? "on" : "off", mapped ? "mmap" : "copy",
        existing ? "existing" : "new", replaced ? "yes" : "no", undone ? "yes" : "no");
    T(!replaced); T(undone); T(editor_tab(e, 0)->modified);
    (void)unlink(target);
    return 0;
}
static int save_as_then_save(bool journalling, uint64_t threshold, bool existing)
{
    fixture f = {0};
    int rc = open_fixture_threshold(&f, journalling, false, false, threshold);
    if (!rc) rc = save_as_then_save_body(&f, existing);
    char target[IPC_PATH_CAP];
    if (snprintf(target, sizeof target, "%s/saved", f.directory) > 0) (void)unlink(target);
    close_fixture(&f);
    return rc;
}
/* Every variant runs, so the report names which reproduce. */
static int dxas_all(void)
{
    static const bool choices[] = {false, true};
    int rc = 0;
    for (size_t j = 0; j < sizeof choices / sizeof *choices; j++)
        for (size_t m = 0; m < sizeof choices / sizeof *choices; m++)
            for (size_t x = 0; x < sizeof choices / sizeof *choices; x++)
                rc |= save_as_then_save(choices[j], choices[m] ? sizeof "" : 0, choices[x]);
    return rc;
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
/* edit-qcuh: a retired buffer is now physically released once its save
 * resolves, so tests look it up by id instead of dereferencing a stale owner. */
static editor_buffer *buffer_by_id(editor *e, uint64_t id)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i] && e->buffers[i]->id == id) return e->buffers[i];
    return NULL;
}
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
    T(owner->retired); uint64_t owner_id = owner->id;
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0);
    /* The resolved save no longer pins the evicted buffer: it is released. */
    T(!buffer_by_id(f.e, owner_id) && !f.e->error && !f.e->stats.journal_error);
    T(press(f.e, 'y', 0, "y") == 0); T(editor_length(f.e) == strlen("y"));
    close_fixture(&f); return 0;
}
/* edit-qcuh (recovery-3 F3): open and close distinct named files well past
 * closed-stack eviction. Every evicted buffer must give back its slot and
 * its file's descriptors, watch and backing; before the fix a retired named
 * buffer was never destroyed and add_buffer ran out of slots after
 * live + closed opens. (P) capacities: one live tab beyond the launch tab and
 * one retained closed tab, so slots = 2 + 1; the round count is four times
 * that slot count, enough to pass exhaustion several times over. copy == true
 * keeps the default COPY acquisition; false forces MMAP with threshold 1
 * byte (P: the 4-byte fixtures are >= 1). */
static size_t open_descriptors(void)
{
    DIR *descriptors = opendir("/proc/self/fd"); if (!descriptors) return SIZE_MAX;
    size_t count = 0; struct dirent *entry;
    while ((entry = readdir(descriptors))) if (entry->d_name[0] != '.') count++;
    (void)closedir(descriptors); return count;
}
static size_t used_slots(editor *e)
{
    size_t used = 0;
    for (size_t i = 0; i < e->buffer_capacity; i++) used += e->buffers[i] != NULL;
    return used;
}
static int retired_named_release(bool copy)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    editor_close(f.e); f.e = NULL;
    editor_config config = {.path = f.path, .journal_path = f.log, .tab_capacity = 2, .closed_capacity = 1,
        .copy_threshold = copy ? 0 : 1,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* tab + text + status */
    (void)unlink(f.log); T(render_null_backend(&f.backend) == 0);
    T(editor_open(&f.e, &config, &f.backend) == 0); T(settle(f.e) == 0);
    const size_t rounds = 4 * f.e->buffer_capacity;
    size_t baseline = SIZE_MAX;
    for (size_t round = 0; round < rounds; round++) {
        char path[IPC_PATH_CAP];
        T(snprintf(path, sizeof path, "%s/file-%zu", f.directory, round) > 0);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
        T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
        uint64_t id; int rc = editor_add_buffer(f.e, path, NULL, 0, &id);
        if (rc) fprintf(stderr, "editor_session_test: retired_named_release(%s) round %zu add rc %d slots %zu/%zu\n",
                        copy ? "copy" : "mmap", round, rc, used_slots(f.e), f.e->buffer_capacity);
        T(rc == 0); T(settle(f.e) == 0);
        T(tabs_count(&f.e->tabs) == 2 && tabs_active(&f.e->tabs)->id == id);
        T(editor_close_tab(f.e, 1) == 0); T(settle(f.e) == 0);
        T(unlink(path) == 0);
        /* The closed stack is full from round 1 on; round 2 is the first
         * whose add can reuse a retired slot. Measure there. */
        if (round == 2) baseline = open_descriptors();
    }
    /* Retirement releases at most one buffer per maintenance turn. */
    for (size_t turn_count = 0; turn_count < f.e->buffer_capacity; turn_count++) editor_retire_buffers(f.e);
    size_t now = open_descriptors();
    fprintf(stderr, "editor_session_test: retired_named_release(%s) slots %zu (live %zu + closed %zu) fds %zu baseline %zu\n",
            copy ? "copy" : "mmap", used_slots(f.e), tabs_count(&f.e->tabs), tabs_closed_count(&f.e->tabs), now, baseline);
    T(used_slots(f.e) == tabs_count(&f.e->tabs) + tabs_closed_count(&f.e->tabs));
    T(baseline != SIZE_MAX && now <= baseline);
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
    T(owner->retired && tabs_closed_at(&f.e->tabs, 0)->id != owner->id); uint64_t owner_id = owner->id;
    T(journal_flush(f.e->journal) == 0); T(editor_checkpoint(f.e) == 0);
    atomic_store(&held.release, true); T(saved(f.e) == 0);
    T(disk_is(f.path, "xbase") == 0);
    /* Released only after its save resolved (no FINISH or cancel pending). */
    T(!buffer_by_id(f.e, owner_id) && !f.e->error && !f.e->stats.journal_error);
    T(!editor_save_active(f.e));
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
static void fm4_remove_tree(const char *root);
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
    /* The simulated crash child may leave an edit-fi2 retained copy
     * (.sublimite-base-*) that only startup housekeeping sweeps: remove the
     * whole fixture tree instead of a bare rmdir (coordinator, 2026-10-18). */
    fm4_remove_tree(root); T(access(root, F_OK) != 0); return 0;
}
/* edit-djg (P): keep explicit-path recovery strict because its source is the
 * live destination. Fixture bytes derive from the base and typed marker;
 * compare the entire source artifact before/after refusal, with no new budget. */
static int explicit_changed_base(void)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    T(press(f.e, 'x', 0, "x") == 0);
    editor_close(f.e); f.e = NULL;
    int fd = open(f.log, O_RDONLY); T(fd >= 0);
    struct stat before; T(fstat(fd, &before) == 0 && before.st_size > 0);
    size_t len = (size_t)before.st_size;
    uint8_t *original = malloc(len), *after = malloc(len); T(original && after);
    T(read(fd, original, len) == (ssize_t)len); close(fd);
    fd = open(f.path, O_WRONLY | O_TRUNC); T(fd >= 0);
    const char disk[] = "changed on disk";
    T(write(fd, disk, sizeof disk - 1u) == (ssize_t)(sizeof disk - 1u)); close(fd);
    f.backend = (render_backend){0}; T(render_null_backend(&f.backend) == 0);
    editor_config cfg = {.journal_path = f.log, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3}; /* (P) tab + text + status. */
    int rc = editor_open(&f.e, &cfg, &f.backend);
    T(rc == EDITOR_ERR_IO && !f.e);
    fd = open(f.log, O_RDONLY); T(fd >= 0);
    struct stat st; T(fstat(fd, &st) == 0 && st.st_size == before.st_size && st.st_ino == before.st_ino);
    T(read(fd, after, len) == (ssize_t)len); close(fd);
    T(!memcmp(original, after, len)); free(original); free(after);
    T(disk_is(f.path, disk) == 0);
    close_fixture(&f);
    puts("edit-djg: explicit changed BASE rejected with EDITOR_ERR_IO; source journal and disk text intact");
    return 0;
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
 * whole-text (pre-edit-28u shape) checkpoint of a 1 GiB buffer (the brief's size, (P) 2^30 bytes; its source
 * is a sparse file, but every byte of it is read into the checkpoint, so the
 * journal itself has 1 GiB of records, with zero payload pages sparse) whose /tmp-corpus-like BASE has
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
/* (P) Zero payload pages are holes. Both journal write seams see a fresh
 * checkpoint inode here; writing only nonzero page fragments preserves every
 * wire byte while keeping the inherited GB whole-text fixture sparse. */
static ssize_t fm4_sparse_write(void *ctx, int fd, const uint8_t *data, size_t size, uint64_t off)
{
    (void)ctx;
    const uint8_t zero[JOURNAL_PAGE] = {0};
    for (size_t done = 0; done < size;) {
        size_t n = JOURNAL_PAGE - (size_t)((off + done) % JOURNAL_PAGE);
        if (n > size - done) n = size - done;
        if (memcmp(data + done, zero, n) && pwrite(fd, data + done, n, (off_t)(off + done)) != (ssize_t)n) return -1;
        done += n;
    }
    return ftruncate(fd, (off_t)(off + size)) ? -1 : (ssize_t)size;
}
static int fm4_sparse_copy(int in, int out, off_t size)
{
    for (off_t at = 0; at < size;) {
        off_t data = lseek(in, at, SEEK_DATA);
        if (data < 0) { T(errno == ENXIO); break; }
        off_t hole = lseek(in, data, SEEK_HOLE); T(hole > data);
        off_t src = data, dst = data;
        while (src < hole) T(copy_file_range(in, &src, out, &dst, (size_t)(hole - src), 0) > 0);
        at = hole;
    }
    T(ftruncate(out, size) == 0); return 0;
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
        if (press(e, 'x', 0, "x")) _exit(6);
        /* edit-28u: checkpoints now reference an original BASE instead of
         * copying it. Unbinding the original reproduces the pre-28u whole-text
         * checkpoint (DELETE all + every byte as INSERT), the GB-scale orphan
         * shape Tobias's profile holds and older builds still leave behind. */
        e->buffer->origin.valid = false;
        journal_io io = {.write = fm4_sparse_write, .append_write = fm4_sparse_write};
        if (journal_flush(e->journal) || journal_set_io(e->journal, &io) || editor_checkpoint(e)) _exit(6);
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
static int qct_conflict(const char *dir, const char *orphan);
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
    T((uint64_t)st.st_blocks * 512u < (uint64_t)st.st_size); /* (P) POSIX st_blocks counts 512-byte units */
    /* The set-aside copy: same bytes under editor_set_aside's naming. */
    int in = open(orphan, O_RDONLY); T(in >= 0);
    T(snprintf(aside, sizeof aside, "%s.set-aside-1", orphan) > 0);
    int out = open(aside, O_CREAT | O_EXCL | O_WRONLY, 0600); T(out >= 0);
    int copied_ok = fm4_sparse_copy(in, out, st.st_size);
    close(in); close(out); T(copied_ok == 0);
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
    /* edit-qct: listed as one recoverable closed tab naming the source. */
    T(tabs_closed_count(&e->tabs) == 1 && !strcmp(tabs_closed_at(&e->tabs, 0)->title, "corpus"));
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
    return qct_conflict(dir, orphan);
}
/* edit-28u: a checkpointed clean session is hot-exit state (PRD: the tab set
 * comes back). Launch 1 opens a file, edits, saves (the save's finish
 * checkpoint and an explicit one leave BASE-only buffers), quits cleanly.
 * Launches 2 and 3 must each restore the tab; each adopts the previous journal
 * and retires it after the recovered checkpoint is durable, so the profile
 * holds only the latest launch's journal. */
static int clean_launch(const char *dir, const char *source, bool expect_tab, char *journal_path)
{
    int lease = -1;
    T(journal_session_create(dir, journal_path, IPC_PATH_CAP, &lease) == JOURNAL_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = journal_path, .session_lease = &lease,
        .start_empty = source == NULL, .cols = 32, .rows = 3};
    editor *e = NULL; int rc = editor_open(&e, &config, &backend);
    if (!rc) rc = settle(e);
    if (!rc && source) {
        uint64_t limit = deadline();
        while (!rc && e->buffer->pending_open) { rc = turn(e); if (trace_now_ns() >= limit) rc = 1; sched_yield(); }
        if (!rc) rc = press(e, 'x', 0, "x");
        if (!rc) rc = press(e, 's', PLAT_MOD_CTRL, NULL);
        if (!rc) rc = saved(e);
        if (!rc && (e->buffer->save_error || editor_tab(e, 0)->modified)) rc = 1;
        if (!rc) rc = journal_flush(e->journal) || editor_checkpoint(e) ? 1 : 0;
    }
    size_t tabs = rc ? 0 : tabs_count(&e->tabs);
    char title[TABS_TITLE_BYTES + 1] = "";
    if (tabs) snprintf(title, sizeof title, "%s", editor_tab(e, 0)->title);
    bool clean = tabs && !editor_tab(e, 0)->modified;
    if (e) editor_close(e);
    struct stat st;
    if (!lstat(journal_path, &st) && !st.st_size) (void)unlink(journal_path);
    (void)journal_session_release(journal_path, lease); close(lease);
    printf("clean_tab_set: launch tabs=%zu title=%s clean=%s\n", tabs, title, clean ? "yes" : "no");
    T(rc == 0);
    if (expect_tab) { T(tabs == 1 && !strcmp(title, "source") && clean); }
    return 0;
}
static size_t session_journals(const char *dir, char *names, size_t cap)
{
    DIR *d = opendir(dir); if (!d) return SIZE_MAX;
    size_t count = 0, used = 0; struct dirent *entry; *names = 0;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int n = snprintf(names + used, cap - used, "%s ", entry->d_name);
        if (n > 0 && (size_t)n < cap - used) used += (size_t)n;
        if (!strncmp(entry->d_name, "session-", 8) && !strchr(entry->d_name, '.')) count++;
    }
    closedir(d); return count;
}
static int clean_tab_set_body(const char *root)
{
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], journal_path[IPC_PATH_CAP], names[4096];
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root))); /* never the real profile */
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(write(fd, "base\n", 5) == 5); T(close(fd) == 0);
    T(clean_launch(dir, source, false, journal_path) == 0);
    size_t count = session_journals(dir, names, sizeof names);
    printf("clean_tab_set: after launch 1 session journals=%zu [%s]\n", count, names);
    for (int launch = 2; launch <= 3; launch++) {
        T(clean_launch(dir, NULL, true, journal_path) == 0);
        count = session_journals(dir, names, sizeof names);
        printf("clean_tab_set: after launch %d session journals=%zu [%s]\n", launch, count, names);
        /* (P) one: the closed launch's own nonempty journal; the journal it
         * adopted was retired after its recovered checkpoint was durable. */
        T(count == 1);
    }
    return 0;
}
static int clean_tab_set(void)
{
    char made[] = "build/editor-28u-tabs-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved_env = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved_env != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved_env) > 0);
    int rc = clean_tab_set_body(root);
    fm4_remove_tree(root); /* every path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    if (!rc) puts("clean_tab_set: saved clean tab set restored on two relaunches; one session journal");
    return rc;
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
/* edit-4da: post-28u differences against the sparse fm4 BASE. Only the
 * typed byte is checkpointed; the BASE must be mapped on the next launch. */
static int qct_orphan(const char *dir, const char *source, char *journal_path)
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
        if (press(e, 'x', 0, "x")) _exit(6);
        if (journal_flush(e->journal) || editor_checkpoint(e)) _exit(7);
        _exit(0); /* crash: the checkpoint is an orphan */
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    DIR *d = opendir(dir); T(d); struct dirent *entry; int found = 0;
    while ((entry = readdir(d))) if (!strncmp(entry->d_name, "session-", 8) && !strchr(entry->d_name, '.')) {
        T(snprintf(journal_path, IPC_PATH_CAP, "%s/%s", dir, entry->d_name) > 0); found++;
    }
    closedir(d); T(found == 1); return 0;
}
static int qct_count_base(void *ctx, const journal_record *r)
{ if (r->type == JOURNAL_BASE) ++*(size_t *)ctx; return 0; }
typedef struct qct_launch { char journal[IPC_PATH_CAP]; int lease; render_backend backend; editor *e; } qct_launch;
static int qct_start_threshold(const char *dir, qct_launch *l, uint64_t threshold)
{
    l->lease = -1; l->e = NULL;
    T(journal_session_create(dir, l->journal, sizeof l->journal, &l->lease) == JOURNAL_OK);
    T(render_null_backend(&l->backend) == 0);
    editor_config config = {.journal_path = l->journal, .session_lease = &l->lease, .start_empty = true,
        .copy_threshold = threshold, .cols = 32, .rows = 3};
    T(editor_open(&l->e, &config, &l->backend) == 0); return settle(l->e);
}
static int qct_start(const char *dir, qct_launch *l)
{ return qct_start_threshold(dir, l, 0); } /* (P) zero selects the configured default */
static void qct_stop(qct_launch *l)
{
    struct stat st;
    if (l->e) editor_close(l->e);
    l->e = NULL;
    if (!lstat(l->journal, &st) && !st.st_size) (void)unlink(l->journal);
    (void)journal_session_release(l->journal, l->lease); close(l->lease);
}
/* Exactly one recoverable closed tab naming the source; nothing recorded. */
static int qct_listed(qct_launch *l)
{
    editor *e = l->e;
    T(e->recovery_deferred == 1 && e->recovery_notice != NULL);
    T(tabs_count(&e->tabs) == 0 && tabs_closed_count(&e->tabs) == 1);
    const tabs_tab *tab = tabs_closed_at(&e->tabs, 0);
    printf("large_restore: closed tab title=%s path=%s\n", tab->title, tab->path);
    T(!strcmp(tab->title, "corpus"));
    T(strlen(tab->path) > strlen("/corpus") && !strcmp(tab->path + strlen(tab->path) - strlen("/corpus"), "/corpus"));
    /* The placeholder is not recovery state: a checkpoint holds no BASE. */
    T(journal_flush(e->journal) == 0 && editor_checkpoint(e) == 0);
    size_t bases = 0; journal_replay_result result;
    T(journal_scan_file(l->journal, qct_count_base, &bases, &result) == 0); T(bases == 0);
    return 0;
}
/* edit-4da (P): pre-28u whole-text shape = DELETE of every BASE byte,
 * then INSERT of the replacement text. Keeping the replacement short makes
 * journal + prefix fit fm4 while journal + the required BASE read does not. */
static int whole_text_orphan(const char *dir, const char *source, char *path)
{
    int lease = -1;
    T(journal_session_create(dir, path, IPC_PATH_CAP, &lease) == 0);
    work_pool pool;
    bool pooled = work_pool_init(&pool, 1, 0) == 0; /* (P) one journal worker, no reserved jobs */
    int rc = pooled ? 0 : 1;
    journal *j = NULL;
    journal_base base;
    if (!rc) rc = journal_capture_base(source, &base);
    if (!rc) rc = journal_open(&j, path, &pool, NULL);
    /* (P) the sole buffer's journal id is one; replacement is strlen("old"). */
    if (!rc) rc = journal_set_base(j, 1, &base);
    if (!rc) rc = journal_delete(j, 1, 0, base.size);
    if (!rc) rc = journal_insert(j, 1, 0, (const uint8_t *)"old", strlen("old"));
    if (j) { if (!rc) rc = journal_flush(j); journal_close(j); }
    if (pooled) work_pool_shutdown(&pool);
    (void)journal_session_release(path, lease); close(lease);
    T(rc == 0); return 0;
}
static int large_restore_body(const char *root)
{
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], orphan[IPC_PATH_CAP];
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root)) && !strcmp(dir + strlen(root), "/sublimite"));
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(snprintf(source, sizeof source, "%s/corpus", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    int rc = ftruncate(fd, (off_t)FM4_GIB); close(fd); T(rc == 0);
    /* The whole-text case must stay deferred even though its journal is small. */
    T(whole_text_orphan(dir, source, orphan) == 0);
    struct stat before, st;
    T(stat(orphan, &before) == 0 && (uint64_t)before.st_size < JOURNAL_STARTUP_SCAN_BYTES);
    qct_launch l = {0};
    rc = qct_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (!rc) printf("large_restore: whole-text journal bytes=%lld deferred=%zu\n",
        (long long)before.st_size, l.e->recovery_deferred);
    qct_stop(&l); T(rc == 0);
    T(stat(orphan, &st) == 0 && st.st_ino == before.st_ino && st.st_size == before.st_size &&
        st.st_mtim.tv_sec == before.st_mtim.tv_sec && st.st_mtim.tv_nsec == before.st_mtim.tv_nsec);
    T(unlink(orphan) == 0);
    /* Same BASE, now a real differences-only checkpoint created by typing. */
    T(qct_orphan(dir, source, orphan) == 0);
    T(stat(orphan, &st) == 0 && (uint64_t)st.st_size < JOURNAL_STARTUP_SCAN_BYTES);
    /* Forced COPY would still read the whole BASE: leave this same orphan
     * deferred. (P) UINT64_MAX selects COPY for every representable file. */
    rc = qct_start_threshold(dir, &l, UINT64_MAX);
    if (!rc) rc = qct_listed(&l);
    if (!rc) puts("large_restore: differences with COPY acquisition deferred");
    qct_stop(&l); T(rc == 0);
    /* (P) initial eager restore plus relaunch of its installed checkpoint:
     * the regression was that every subsequent launch deferred again. */
    for (unsigned launch = 0; launch < 2u; launch++) {
        uint64_t t0 = trace_now_ns();
        rc = qct_start(dir, &l);
        uint64_t elapsed = trace_now_ns() - t0;
        editor *e = l.e;
        bool tab = !rc && tabs_count(&e->tabs) == 1; /* (P) sole source tab */
        uint8_t head[sizeof "x"] = {0}; /* (P) typed byte and first sparse BASE byte */
        bool text = tab && piece_len(e->buffer->tree) == FM4_GIB + strlen("x") &&
            !piece_read(e->buffer->tree, 0, head, sizeof head) && head[0] == 'x' && head[1] == 0;
        bool dirty = tab && editor_tab(e, 0)->modified && !strcmp(editor_tab(e, 0)->title, "corpus");
        bool mapped = tab && e->buffer->file && file_open_mode(e->buffer->file) == FILE_MODE_MMAP &&
            e->buffer->arena.size < FM4_GIB;
        bool eager = tab && !e->recovery_deferred && !tabs_closed_count(&e->tabs) && !e->recovery_notice;
        bool retired = access(orphan, F_OK) != 0;
        if (!rc) rc = journal_flush(e->journal) || editor_checkpoint(e) ? 1 : 0;
        bool small = !rc && !stat(l.journal, &st) && (uint64_t)st.st_size < JOURNAL_STARTUP_SCAN_BYTES;
        char power[sizeof "Not charging"] = "unknown", load1[sizeof "Not charging"] = "unknown";
        FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
        if (f) { if (!fgets(power, sizeof power, f)) strcpy(power, "unknown"); fclose(f); }
        power[strcspn(power, "\n")] = 0;
        f = fopen("/proc/loadavg", "r");
        if (f) { if (!fgets(load1, sizeof load1, f)) strcpy(load1, "unknown"); fclose(f); }
        load1[strcspn(load1, " \n")] = 0;
        /* (P) 10^6 ns/ms; G4a is the existing fm4 tracked launch bound. */
        printf("large_restore: differences launch=%u tabs=%zu closed=%zu deferred=%zu text=%s dirty=%s mapped=%s "
            "journal_small=%s retired=%s launch_ms=%.3f (M) G4a_p99=%.0f bound=tracked power=%s load1=%s\n",
            launch, e ? tabs_count(&e->tabs) : 0, e ? tabs_closed_count(&e->tabs) : 0,
            e ? e->recovery_deferred : 0, text ? "yes" : "no", dirty ? "yes" : "no", mapped ? "yes" : "no",
            small ? "yes" : "no", retired ? "yes" : "no", (double)elapsed / 1e6,
            (double)FM4_G4A_P99_NS / 1e6, power, load1);
        strcpy(orphan, l.journal);
        qct_stop(&l);
        T(rc == 0 && eager && text && dirty && mapped && small && retired);
    }
    return 0;
}
/* edit-qct: reopening the placeholder of an orphan whose BASE changed never
 * replays onto it (edit-btj): the disk text opens clean and the journal is
 * set aside under its dotted name. */
static int qct_conflict(const char *dir, const char *orphan)
{
    qct_launch l = {0};
    T(qct_start(dir, &l) == 0);
    editor *e = l.e; uint64_t t0 = trace_now_ns(), limit = deadline();
    int rc = tabs_closed_count(&e->tabs) == 1 ? editor_reopen_tab(e) : 1;
    while (!rc && (e->buffer->placeholder || e->buffer->pending_open || !tabs_count(&e->tabs))) {
        rc = turn(e); if (trace_now_ns() >= limit) rc = 1;
        sched_yield();
    }
    if (!rc) rc = settle(e);
    uint8_t head[7] = {0};
    bool disk = !rc && tabs_count(&e->tabs) == 1 && piece_len(e->buffer->tree) == FM4_GIB &&
        !piece_read(e->buffer->tree, 0, head, sizeof head) && !memcmp(head, "changed", sizeof head) && !editor_tab(e, 0)->modified;
    bool notice = e->recovery_notice && strstr(e->recovery_notice, "set aside");
    printf("large_orphans: conflict reopen rc=%d disk_text=%s notice=%s restore_ms=%.3f (M)\n", rc, disk ? "yes" : "no",
        notice ? "yes" : "no", (double)(trace_now_ns() - t0) / 1e6);
    qct_stop(&l);
    T(rc == 0 && disk && notice);
    T(access(orphan, F_OK) != 0); /* renamed aside, never deleted */
    return 0;
}
static int large_restore(void)
{
    char made[] = "build/editor-qct-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved) > 0);
    int rc = large_restore_body(root);
    fm4_remove_tree(root); /* on success and on every failure path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    if (!rc) puts("large_restore: whole-text deferred; differences restore eagerly across hot exits");
    return rc;
}
/* edit-601: lazy-restore recoverability (restore.c). (a) a launch without a
 * session journal never retires the source; (b) a checkpoint failure after the
 * swap rolls back to the placeholder, keeps the source and is not fatal; (c) a
 * scan NOMEM is a notice, not a fatal; (d) duplicate TABS ids stay in bounds.
 * Fixtures reuse the qct shape: a sparse 1 GiB BASE ((P) 2^30, FM4_GIB) plus
 * one typed byte. */
static uint64_t e601_get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8u * i);
    return v;
}
/* (P) the fixtures journal at most a few buffers; 64 ids is headroom. */
typedef struct e601_ids { uint64_t base[64]; size_t n; bool ok; } e601_ids;
static bool e601_known(const e601_ids *s, uint64_t id)
{
    for (size_t i = 0; i < s->n; i++) if (s->base[i] == id) return true;
    return false;
}
/* Every id a record names (TABS entries, edits, VIEW, TARGET) follows its BASE. */
static int e601_record(void *ctx, const journal_record *r)
{
    e601_ids *s = ctx;
    if (r->type == JOURNAL_BASE) { if (s->n < 64) s->base[s->n++] = r->buffer_id; else s->ok = false; return 0; }
    if (r->type == JOURNAL_TABS) {
        uint64_t count = r->size >= 16 ? e601_get64(r->data) : 0;
        for (uint64_t i = 0; i < count && 16 + (i + 1) * 8 <= r->size; i++)
            if (!e601_known(s, e601_get64(r->data + 16 + i * 8))) s->ok = false;
        return 0;
    }
    if (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE || r->type == JOURNAL_VIEW || r->type == JOURNAL_TARGET)
        if (!e601_known(s, r->buffer_id)) s->ok = false;
    return 0;
}
static int e601_consistent(const char *path)
{
    e601_ids s = {.ok = true}; journal_replay_result result;
    T(journal_scan_file(path, e601_record, &s, &result) == 0);
    if (!s.ok) fprintf(stderr, "editor_session_test: %s names an id without BASE\n", path);
    T(s.ok); return 0;
}
static void e601_discard(qct_launch *l)
{
    if (l->e) editor_close(l->e);
    l->e = NULL;
    (void)unlink(l->journal);
    (void)journal_session_release(l->journal, l->lease); close(l->lease);
}
/* Drive until no restore is in flight; every step must return 0 (no fail()). */
static int e601_drive(editor *e)
{
    uint64_t limit = deadline();
    while (e->restore_active) {
        int rc = turn(e);
        if (rc) fprintf(stderr, "editor_session_test: restore step returned %d (stats.error_cause=%d)\n", rc, editor_get_stats(e).error_cause);
        T(rc == 0); T(trace_now_ns() < limit); sched_yield();
    }
    return settle(e);
}
/* edit-4jhi (P): the smallest BASE above the existing eager cost bound is
 * JOURNAL_STARTUP_SCAN_BYTES + one byte. BASE and TARGET are distinct sparse
 * inodes with identical or rewritten TARGET bytes, in the edit-fi2 shape.
 * The sole buffer id is one; zero is success/the insertion offset/first tab;
 * owner-only modes are the POSIX permission bits. No new wait or size bound. */
static int retained_lazy_body(const char *root, qct_launch *launch, bool rewritten, uint64_t threshold, bool trim_tail)
{
    char dir[IPC_PATH_CAP], retained[IPC_PATH_CAP], target[IPC_PATH_CAP], orphan[IPC_PATH_CAP];
    T(snprintf(dir, sizeof dir, "%s/sublimite", root) > 0);
    T(mkdir(dir, 0700) == 0);
    T(snprintf(retained, sizeof retained, "%s/.sublimite-base-retained", dir) > 0);
    T(snprintf(target, sizeof target, "%s/target", root) > 0);
    const uint64_t size = JOURNAL_STARTUP_SCAN_BYTES + 1u;
    const char marker[] = "base\n", edit[] = "x", changed[] = "disk\n";
    /* (P) Trimming strlen(edit) trailing zero bytes cancels the insertion's
     * length change; otherwise replay grows by strlen(edit) (edit-14bi).
     * Zero is the net byte growth when trimming. */
    uint64_t expected_size = size + (trim_tail ? 0u : strlen(edit));
    const char *paths[] = {retained, target};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        int fd = open(paths[i], O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
        int rc = ftruncate(fd, (off_t)size);
        const char *bytes = rewritten && i ? changed : marker;
        if (!rc && pwrite(fd, bytes, strlen(bytes), 0) != (ssize_t)strlen(bytes)) rc = -1;
        T(close(fd) == 0); T(rc == 0);
    }
    journal_base base, disk;
    T(journal_capture_base(retained, &base) == 0);
    T(journal_capture_base(target, &disk) == 0);
    T(base.inode != disk.inode);
    int lease = -1;
    T(journal_session_create(dir, orphan, sizeof orphan, &lease) == 0);
    work_pool pool;
    T(work_pool_init(&pool, 1, 0) == 0); /* (P) sole journal worker; no reserved jobs */
    journal *j = NULL;
    int rc = journal_open(&j, orphan, &pool, NULL);
    if (!rc) rc = journal_set_base(j, 1, &base);
    if (!rc) rc = journal_append(j, JOURNAL_TARGET, 1, (const uint8_t *)target, strlen(target) + 1u);
    if (!rc) rc = journal_insert(j, 1, 0, (const uint8_t *)edit, strlen(edit));
    if (!rc && trim_tail) rc = journal_delete(j, 1, size, strlen(edit));
    if (j) { if (!rc) rc = journal_flush(j); journal_close(j); }
    work_pool_shutdown(&pool);
    (void)journal_session_release(orphan, lease); close(lease);
    T(rc == 0);
    /* (P) retained BASE + distinct TARGET charges the whole BASE at startup,
     * independently of the configured acquisition threshold. */
    T(qct_start_threshold(dir, launch, threshold) == 0);
    editor *e = launch->e;
    T(e->recovery_deferred == 1 && tabs_count(&e->tabs) == 0 && tabs_closed_count(&e->tabs) == 1);
    T(editor_reopen_tab(e) == 0);
    T(e601_drive(e) == 0);
    uint8_t prefix[sizeof edit - 1u + sizeof marker - 1u] = {0};
    int read_rc = piece_read(e->tree, 0, prefix, sizeof prefix);
    bool text = piece_len(e->tree) == expected_size && read_rc == 0 &&
        !memcmp(prefix, "xbase\n", sizeof prefix);
    bool dirty = editor_tab(e, 0) && editor_tab(e, 0)->modified;
    bool aside = *e->recovery_aside != 0;
    bool bound = !strcmp(e->buffer->path, target) && !strcmp(e->buffer->save_path, target);
    const file_id *destination = e->buffer->save ? savectl_target_identity(e->buffer->save) : NULL;
    bound = bound && destination && destination->dev == disk.device && destination->ino == disk.inode &&
        destination->mtime_ns == disk.mtime_ns && destination->ctime_ns == disk.ctime_ns;
    bool original = editor_base_id_matches(&e->buffer->origin, &base) &&
        e->buffer->base.inode == base.inode && !strcmp(e->buffer->base.path, retained);
    printf("edit-4jhi: deferred retained BASE reopen text=%s modified=%s set-aside=%s target=%s\n",
        text ? "yes" : "no", dirty ? "yes" : "no", aside ? "yes" : "no", bound ? "yes" : "no");
    if (!text || !bound) printf("edit-4jhi: len=%llu expected=%llu prefix=%.*s path=%s save_path=%s notice=%s\n",
        (unsigned long long)piece_len(e->tree), (unsigned long long)expected_size, (int)sizeof prefix, prefix,
        e->buffer->path, e->buffer->save_path, e->recovery_notice ? e->recovery_notice : "none");
    fflush(stdout);
    T(text && dirty && !aside && bound && original);
    T(e->buffer->file && file_open_mode(e->buffer->file) ==
        (threshold == UINT64_MAX ? FILE_MODE_COPY : FILE_MODE_MMAP));
    T(lineidx_len(e->buffer->index) == piece_len(e->tree));
    T(access(orphan, F_OK) < 0);
    T(e601_consistent(launch->journal) == 0);
    T(journal_check_base(&base) == 0 && journal_check_base(&disk) == 0);
    /* The replacement checkpoint still references BASE, with TARGET bound
     * separately. A hot exit must recover the insertion exactly once. */
    qct_stop(launch);
    T(qct_start_threshold(dir, launch, threshold) == 0);
    e = launch->e;
    T(e->recovery_deferred == 1 && tabs_closed_count(&e->tabs) == 1);
    T(editor_reopen_tab(e) == 0 && e601_drive(e) == 0);
    T(piece_len(e->tree) == expected_size && piece_read(e->tree, 0, prefix, sizeof prefix) == 0);
    T(!memcmp(prefix, "xbase\n", sizeof prefix) && editor_tab(e, 0)->modified && !*e->recovery_aside);
    T(!strcmp(e->buffer->path, target) && !strcmp(e->buffer->save_path, target));
    printf("edit-4jhi: target=%s acquisition=%s length=%s checkpoint relaunch preserved BASE edits and TARGET\n",
        rewritten ? "rewritten" : "unchanged", threshold == UINT64_MAX ? "COPY" : "MMAP",
        trim_tail ? "unchanged" : "grown");
    e601_discard(launch);
    return 0;
}
static int retained_lazy_case(bool rewritten, uint64_t threshold, bool trim_tail)
{
    char made[] = "build/editor-4jhi-XXXXXX", root[IPC_PATH_CAP];
    T(mkdtemp(made));
    if (!realpath(made, root)) { fm4_remove_tree(made); T(false); }
    qct_launch launch = {.lease = -1}; /* (P) no descriptor before launch */
    int rc = retained_lazy_body(root, &launch, rewritten, threshold, trim_tail);
    if (launch.e) e601_discard(&launch);
    fm4_remove_tree(root); /* sparse fixtures removed on every body exit */
    return rc;
}
static int retained_lazy(void)
{
    /* (P) one maps every nonempty file; UINT64_MAX copies every file size.
     * Exercise both acquisitions and a TARGET whose bytes differ from BASE. */
    T(retained_lazy_case(false, 1, false) == 0);
    T(retained_lazy_case(true, UINT64_MAX, true) == 0);
    /* edit-14bi (P): force COPY, replay the insertion without a compensating
     * deletion, and require the longer text through checkpoint relaunch. */
    T(retained_lazy_case(true, UINT64_MAX, false) == 0);
    return 0;
}
static bool e601_same(const char *path, const struct stat *before)
{
    struct stat st;
    return !stat(path, &st) && st.st_ino == before->st_ino && st.st_size == before->st_size &&
        st.st_mtim.tv_sec == before->st_mtim.tv_sec && st.st_mtim.tv_nsec == before->st_mtim.tv_nsec;
}
/* edit-4da restores differences on a mapped BASE eagerly; forced COPY
 * acquisition at launch keeps the qct orphan deferred ("differences with COPY
 * acquisition deferred", large_restore). The lazy restore under test then
 * opens with the configured default again ((P) zero selects it), mapped as
 * before 4da. */
static int e601_start(const char *dir, qct_launch *l)
{
    /* (P) UINT64_MAX selects COPY for every representable file. */
    int rc = qct_start_threshold(dir, l, UINT64_MAX);
    if (l->e) l->e->cfg.copy_threshold = 0;
    return rc;
}
static int e601_no_journal(const char *dir, const char *orphan)
{
    struct stat before; T(stat(orphan, &before) == 0);
    qct_launch l = {0};
    int rc = e601_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (rc) { e601_discard(&l); T(rc == 0); }
    editor *e = l.e;
    journal *log = e->journal; e->journal = NULL; /* launch journal absent (open failed or disabled) */
    rc = editor_reopen_tab(e);
    if (!rc) rc = e601_drive(e);
    bool live = !rc && tabs_count(&e->tabs) == 1 && !e->buffer->placeholder && !e->restore_active;
    bool kept = e601_same(orphan, &before);
    /* edit-7i5: the restored edits are shown, not the disk text: (P) BASE plus
     * the one typed byte, the byte at offset 0, the tab modified. */
    uint64_t len = piece_len(e->buffer->tree); uint8_t typed = 0;
    bool shown = live && len == FM4_GIB + 1 && piece_read(e->buffer->tree, 0, &typed, 1) == 0 && typed == 'x' &&
        tabs_active(&e->tabs)->modified;
    printf("edit-601 (a): no launch journal rc=%d restored_tab=%s len=%llu first=0x%02x modified=%s source_kept=%s notice=%s\n", rc,
        live ? "yes" : "no", (unsigned long long)len, typed, live && tabs_active(&e->tabs)->modified ? "yes" : "no",
        kept ? "yes" : "no", e->recovery_notice ? e->recovery_notice : "(none)");
    e->journal = log;
    e601_discard(&l);
    T(rc == 0 && live && kept && shown);
    /* edit-7i5: a later launch with a journal finds the kept source and
     * applies its edits once: still BASE + 1, and the source retires. */
    qct_launch m = {0};
    rc = e601_start(dir, &m);
    if (!rc) rc = qct_listed(&m);
    if (rc) { e601_discard(&m); T(rc == 0); }
    e = m.e;
    rc = editor_reopen_tab(e);
    if (!rc) rc = e601_drive(e);
    len = rc ? 0 : piece_len(e->buffer->tree); typed = 0;
    bool once = !rc && !e->buffer->placeholder && len == FM4_GIB + 1 && piece_read(e->buffer->tree, 0, &typed, 1) == 0 &&
        typed == 'x' && tabs_active(&e->tabs)->modified;
    bool retired = access(orphan, F_OK) != 0;
    printf("edit-7i5: relaunch with journal rc=%d len=%llu first=0x%02x source_retired=%s\n", rc,
        (unsigned long long)len, typed, retired ? "yes" : "no");
    int consistent = once ? e601_consistent(m.journal) : 1;
    e601_discard(&m);
    T(rc == 0 && once && retired && consistent == 0);
    return 0;
}
static int e601_checkpoint_failure(const char *dir, const char *orphan)
{
    struct stat before; T(stat(orphan, &before) == 0);
    qct_launch l = {0};
    int rc = e601_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (rc) { e601_discard(&l); T(rc == 0); }
    editor *e = l.e;
    /* Injection: the session directory refuses new names, so journal_rotate's
     * checkpoint temp (and only it; appends use the open descriptor) fails
     * with JOURNAL_IO. (P) 0500 = owner read/search, no write. */
    T(chmod(dir, 0500) == 0);
    char probe[IPC_PATH_CAP]; T(snprintf(probe, sizeof probe, "%s/probe-601", dir) > 0);
    int fd = open(probe, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd >= 0) { /* root ignores the mode: no injection possible */
        close(fd); unlink(probe); T(chmod(dir, 0700) == 0); e601_discard(&l);
        puts("edit-601 (b): skipped, directory mode not enforced for this user"); return 0;
    }
    rc = editor_reopen_tab(e);
    if (!rc) rc = e601_drive(e);
    T(chmod(dir, 0700) == 0); /* (P) owner read/write/search */
    bool settled = !rc && !e->restore_active && tabs_count(&e->tabs) == 1;
    bool inert = settled && !e->buffer->placeholder && piece_len(e->buffer->tree) == 0; /* restore_fail: empty untitled */
    bool notice = e->recovery_notice && strstr(e->recovery_notice, "retained");
    bool kept = e601_same(orphan, &before);
    printf("edit-601 (b): checkpoint failure rc=%d settled=%s placeholder_fallback=%s source_kept=%s notice=%s\n", rc,
        settled ? "yes" : "no", inert ? "yes" : "no", kept ? "yes" : "no", e->recovery_notice ? e->recovery_notice : "(none)");
    int consistent = 1;
    if (settled) {
        /* Metadata appended after the failure, then a later checkpoint: no
         * record names a buffer whose BASE is not in the journal. */
        consistent = journal_flush(e->journal) || e601_consistent(l.journal);
        if (!consistent) consistent = editor_checkpoint(e) || e601_consistent(l.journal);
        if (!consistent) consistent = turn(e);
    }
    e601_discard(&l);
    T(rc == 0 && settled && inert && notice && kept && consistent == 0);
    return 0;
}
static int e601_scan_nomem(const char *dir, const char *orphan)
{
    struct stat before; T(stat(orphan, &before) == 0);
    qct_launch l = {0};
    int rc = e601_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (rc) { e601_discard(&l); T(rc == 0); }
    editor *e = l.e;
    e->restore_fault = JOURNAL_NOMEM; /* the multi-GB journal arena NOMEM */
    rc = editor_reopen_tab(e);
    if (!rc) rc = e601_drive(e);
    bool settled = !rc && !e->restore_active && tabs_count(&e->tabs) == 1 && !e->buffer->placeholder;
    bool notice = e->recovery_notice && strstr(e->recovery_notice, "retained");
    bool kept = e601_same(orphan, &before);
    printf("edit-601 (c): scan NOMEM rc=%d settled=%s source_kept=%s notice=%s\n", rc, settled ? "yes" : "no",
        kept ? "yes" : "no", e->recovery_notice ? e->recovery_notice : "(none)");
    e601_discard(&l);
    T(rc == 0 && settled && notice && kept);
    return 0;
}
/* The qct orphan plus a TABS record listing the one buffer (P) three times. */
static int e601_duplicate_orphan(const char *dir, const char *source, char *journal_path)
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
        if (press(e, 'x', 0, "x")) _exit(6);
        if (journal_flush(e->journal) || editor_checkpoint(e)) _exit(7);
        uint8_t tabs[16 + 3 * 8] = {0}; /* count, active, three ids */
        uint64_t id = e->buffer->id;
        tabs[0] = 3;
        for (unsigned i = 0; i < 3; i++) for (unsigned k = 0; k < 8; k++) tabs[16 + i * 8 + k] = (uint8_t)(id >> (8u * k));
        if (journal_append(e->journal, JOURNAL_TABS, 0, tabs, sizeof tabs) || journal_flush(e->journal)) _exit(8);
        _exit(0); /* crash */
    }
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    DIR *d = opendir(dir); T(d); struct dirent *entry; int found = 0;
    while ((entry = readdir(d))) if (!strncmp(entry->d_name, "session-", 8) && !strchr(entry->d_name, '.')) {
        T(snprintf(journal_path, IPC_PATH_CAP, "%s/%s", dir, entry->d_name) > 0); found++;
    }
    closedir(d); T(found == 1); return 0;
}
static int e601_duplicate_tabs(const char *dir, const char *orphan)
{
    qct_launch l = {0};
    int rc = e601_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (rc) { e601_discard(&l); T(rc == 0); }
    editor *e = l.e;
    rc = editor_reopen_tab(e);
    if (!rc) rc = e601_drive(e);
    uint8_t head = 0;
    bool text = !rc && tabs_count(&e->tabs) == 1 && piece_len(e->buffer->tree) == FM4_GIB + 1 &&
        !piece_read(e->buffer->tree, 0, &head, 1) && head == 'x';
    bool retired = access(orphan, F_OK) != 0;
    printf("edit-601 (d): duplicate TABS ids rc=%d text=%s orphan_retired=%s\n", rc, text ? "yes" : "no", retired ? "yes" : "no");
    e601_discard(&l);
    T(rc == 0 && text && retired);
    return 0;
}
/* Remove an orphan, its lock name and any set-aside copy of it. */
static void e601_forget(const char *dir, const char *orphan)
{
    if (!*orphan) return;
    char name[IPC_PATH_CAP];
    (void)unlink(orphan);
    if (snprintf(name, sizeof name, "%s/.%s.lock", dir, strrchr(orphan, '/') + 1) > 0) (void)unlink(name);
    DIR *d = opendir(dir); struct dirent *entry; const char *leaf = strrchr(orphan, '/') + 1;
    if (d) {
        while ((entry = readdir(d))) if (!strncmp(entry->d_name, leaf, strlen(leaf)) && strstr(entry->d_name, ".set-aside-"))
            (void)unlinkat(dirfd(d), entry->d_name, 0);
        closedir(d);
    }
}
static int e601_body(const char *root)
{
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], orphan[IPC_PATH_CAP] = "";
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root)) && !strcmp(dir + strlen(root), "/sublimite")); /* never the real profile */
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(snprintf(source, sizeof source, "%s/corpus", root) > 0);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    T(ftruncate(fd, (off_t)FM4_GIB) == 0); close(fd);
    /* Each case runs even when an earlier one fails; a retired orphan is
     * written again so the next case still has its source. */
    int (*cases[])(const char *, const char *) = {e601_no_journal, e601_checkpoint_failure, e601_scan_nomem};
    int failed = 0;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (!*orphan || access(orphan, F_OK)) {
            e601_forget(dir, orphan);
            T(qct_orphan(dir, source, orphan) == 0);
        }
        failed |= cases[i](dir, orphan);
    }
    e601_forget(dir, orphan);
    T(e601_duplicate_orphan(dir, source, orphan) == 0);
    failed |= e601_duplicate_tabs(dir, orphan);
    return failed;
}
static int e601_restore_failures(void)
{
    char made[] = "build/editor-601-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved) > 0);
    int rc = e601_body(root);
    char dir[IPC_PATH_CAP];
    if (snprintf(dir, sizeof dir, "%s/sublimite", root) > 0) (void)chmod(dir, 0700); /* (P) undo (b) on a failure path */
    fm4_remove_tree(root); /* on success and on every failure path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    if (!rc) puts("edit-601: no-journal restore keeps the source; checkpoint failure and scan NOMEM are notices; duplicate TABS ids stay in bounds");
    return rc;
}
/* edit-c26 begin: Astra drive 5, task 37 (docs/reviews/astra-vs-sublime-5.md
 * "## 37."). A 64 MiB file opened in place, one typed byte, a checkpoint, one
 * more byte, clean close (hot exit), no-argument relaunch: build 2c48f9c
 * exited 1 with error -103 and set the session aside. Mechanism
 * (docs/decisions/edit-c26.md): the startup install COPY-acquired the BASE
 * into an arena of 64 MiB reserve + file size, then replaced the whole tree
 * (piece_delete of every ORIGINAL byte, which copies them into the add
 * buffer, then piece_insert of the whole recovered text), needing about twice
 * the file size of add space: PIECE_ERR_NOMEM -> EDITOR_ERR_MEMORY; the
 * edit-btj retry then failed its lease check because the failed attempt's
 * teardown had released the launch's own lease file -> -103. Fixed by edit-f3i
 * (9b5ee41), which replays the logged ops onto the opened BASE instead. Size:
 * (M) the drive's corpus file, /tmp/edit-corpus/dense_short.txt st_size =
 * 67,108,864 = 2^26 (build/fixtures/37-restart-fixture.json); content
 * follows tools/mkcorpus gen_dense (lines of 0-3 chars). The bytes are real,
 * not sparse: dense short lines are the drive's shape. */
#define C26_BYTES (UINT64_C(1) << 26)
#define C26_FIXTURE "tests/fixtures/drive5-task37-session.jnl"
/* (M) the drive's journal names this BASE (tools/journal_replay dump). */
#define C26_DRIVE_BASE "/tmp/edit-corpus/dense_short.txt"
static int c26_make_source(const char *path)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    enum { CHUNK = 1 << 20 }; /* (P) 1 MiB write unit; 2^26 / 2^20 = 64 writes */
    char *chunk = malloc(CHUNK); if (!chunk) { close(fd); T(chunk); }
    uint32_t state = UINT32_C(12345); size_t fill = 0; uint64_t written = 0; int rc = 0;
    while (!rc && written < C26_BYTES) {
        /* POSIX rand example LCG; a line is 0-3 chars of space or a-z, then
         * LF, as gen_dense. The last chunk is cut at exactly C26_BYTES. */
        state = state * UINT32_C(1103515245) + UINT32_C(12345);
        unsigned len = (state >> 16) % 4u;
        for (unsigned i = 0; i <= len && fill < CHUNK; i++) {
            state = state * UINT32_C(1103515245) + UINT32_C(12345);
            unsigned r = (state >> 16) % 27u;
            chunk[fill++] = i == len ? '\n' : r == 26u ? ' ' : (char)('a' + (int)r);
        }
        if (fill == CHUNK || written + fill == C26_BYTES) {
            size_t n = written + fill > C26_BYTES ? (size_t)(C26_BYTES - written) : fill;
            if (write(fd, chunk, n) != (ssize_t)n) rc = 1;
            written += n; fill = 0;
        }
    }
    free(chunk); if (close(fd)) rc = 1;
    T(rc == 0); return 0;
}
static int c26_one_journal(const char *dir, char *journal_path)
{
    DIR *d = opendir(dir); T(d); struct dirent *entry; int found = 0, aside = 0;
    while ((entry = readdir(d))) {
        if (strstr(entry->d_name, ".set-aside-")) { fprintf(stderr, "editor_session_test: c26 set-aside %s\n", entry->d_name); aside++; }
        if (!strncmp(entry->d_name, "session-", 8) && !strchr(entry->d_name, '.')) {
            T(snprintf(journal_path, IPC_PATH_CAP, "%s/%s", dir, entry->d_name) > 0); found++;
        }
    }
    closedir(d); T(aside == 0); T(found == 1); return 0;
}
/* Launch 1 as main.c does (leased session) with the file argument: wait for
 * the mapped open, type, checkpoint, type, then a clean close. */
static int c26_first_launch(const char *dir, const char *source, char *journal_path)
{
    fflush(stdout); /* the child's flush must not repeat the parent's buffered lines */
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        int lease = -1;
        if (journal_session_create(dir, journal_path, IPC_PATH_CAP, &lease)) _exit(2);
        render_backend backend = {0}; if (render_null_backend(&backend)) _exit(3);
        editor_config config = {.path = source, .journal_path = journal_path, .session_lease = &lease, .cols = 32, .rows = 3};
        editor *e = NULL; if (editor_open(&e, &config, &backend) || settle(e)) _exit(4);
        uint64_t limit = deadline();
        while (e->buffer->pending_open) { if (turn(e) || trace_now_ns() >= limit) _exit(5); sched_yield(); }
        /* Opened in place by path: above FILE_PREFIX_MAX the open is async
         * (pending_open). Mode is COPY below file_default_copy_threshold()
         * (min(256 MiB, RAM/32), file.c), so COPY on the drive's box. */
        if (!e->buffer->file || file_size(e->buffer->file) <= FILE_PREFIX_MAX) _exit(6);
        printf("c26: first launch mode=%s\n", file_open_mode(e->buffer->file) == FILE_MODE_COPY ? "copy" : "mmap");
        fflush(stdout);
        if (press(e, 'x', 0, "x")) _exit(7);
        if (journal_flush(e->journal) || editor_checkpoint(e)) _exit(8); /* the drive's idle checkpoint */
        if (press(e, 'y', 0, "y")) _exit(9);                              /* the drive's extra character */
        editor_close(e); /* clean close: hot exit keeps the dirty session */
        (void)journal_session_release(journal_path, lease); close(lease);
        _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) fprintf(stderr, "editor_session_test: c26 first launch status=%d\n", status);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return c26_one_journal(dir, journal_path);
}
/* No-argument relaunch as main.c: the dirty tab comes back, nothing is set
 * aside, the adopted journal is retired and the launch's own one remains. */
static int c26_relaunch(const char *dir, char *orphan, const char *head, const char *title)
{
    char journal_path[IPC_PATH_CAP]; int lease = -1;
    T(journal_session_create(dir, journal_path, sizeof journal_path, &lease) == JOURNAL_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = journal_path, .session_lease = &lease, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; uint64_t t0 = trace_now_ns();
    int rc = editor_open(&e, &config, &backend);
    int opened = rc;
    if (!rc) rc = settle(e);
    double ms = (double)(trace_now_ns() - t0) / 1e6;
    size_t n = strlen(head); uint8_t got[8] = {0}; T(n <= sizeof got);
    bool tab = !rc && tabs_count(&e->tabs) == 1;
    bool text = tab && piece_len(e->buffer->tree) == C26_BYTES + n &&
        !piece_read(e->buffer->tree, 0, got, n) && !memcmp(got, head, n);
    bool dirty = tab && editor_tab(e, 0)->modified && !strcmp(editor_tab(e, 0)->title, title);
    bool quiet = !rc && !e->recovery_notice && !*e->recovery_aside && !e->recovery_deferred;
    char power[32] = "unknown", load1[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (!fgets(power, sizeof power, f)) strcpy(power, "unknown"); fclose(f); }
    power[strcspn(power, "\n")] = 0; /* "Not charging" holds a space */
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%31s", load1) != 1) strcpy(load1, "unknown"); fclose(f); }
    printf("c26: relaunch editor_open=%d tabs=%zu text=%s dirty=%s notice=%s restore_ms=%.3f (M) bound=tracked power=%s load1=%s\n",
        opened, rc ? (size_t)0 : tabs_count(&e->tabs), text ? "yes" : "no", dirty ? "yes" : "no",
        !rc && e->recovery_notice ? e->recovery_notice : "none", ms, power, load1);
    if (e) editor_close(e);
    struct stat st;
    if (!lstat(journal_path, &st) && !st.st_size) (void)unlink(journal_path);
    (void)journal_session_release(journal_path, lease); close(lease);
    T(opened == 0); T(rc == 0 && text && dirty && quiet);
    T(access(orphan, F_OK) != 0); /* adopted and retired, never set aside */
    return c26_one_journal(dir, orphan);
}
static int c26_dense_hot_exit_body(const char *root, const char *source)
{
    char dir[IPC_PATH_CAP], orphan[IPC_PATH_CAP], xdg[IPC_PATH_CAP];
    T(snprintf(xdg, sizeof xdg, "%s/live", root) > 0); T(mkdir(xdg, 0700) == 0); /* (P) owner rwx bits */
    T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root))); /* never the real profile */
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(c26_first_launch(dir, source, orphan) == 0);
    struct stat st; T(stat(orphan, &st) == 0);
    printf("c26: hot-exit journal bytes=%lld (M) base_bytes=%llu\n", (long long)st.st_size, (unsigned long long)C26_BYTES);
    /* edit-28u shape: BASE referenced, not copied; journal + BASE within the
     * fm4 startup bound, so startup replays it (no deferral). */
    T((uint64_t)st.st_size < C26_BYTES && (uint64_t)st.st_size + C26_BYTES <= JOURNAL_STARTUP_SCAN_BYTES);
    /* Two relaunches: the restored session must itself restore again. */
    for (int launch = 2; launch <= 3; launch++) T(c26_relaunch(dir, orphan, "xy", "dense_short.txt") == 0);
    fm4_remove_tree(xdg);
    return 0;
}
typedef struct c26_records { journal_record *rec; uint8_t *data; size_t count, cap, used, size; } c26_records;
static int c26_collect(void *ctx, const journal_record *r)
{
    c26_records *s = ctx;
    if (s->count == s->cap || r->size > s->size - s->used) return JOURNAL_FULL;
    memcpy(s->data + s->used, r->data, r->size);
    s->rec[s->count] = *r; s->rec[s->count].data = s->data + s->used;
    s->used += r->size; s->count++; return 0;
}
/* The drive's own journal, with its BASE identity record rewritten to the
 * regenerated 2^26-byte file (inode 6437225 of the drive's corpus cannot be
 * reproduced). Every other record is the drive's, byte for byte. */
static int c26_drive_journal_body(const char *root, const char *source)
{
    char dir[IPC_PATH_CAP], orphan[IPC_PATH_CAP], xdg[IPC_PATH_CAP];
    T(snprintf(xdg, sizeof xdg, "%s/fixture", root) > 0); T(mkdir(xdg, 0700) == 0); /* (P) owner rwx bits */
    T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root)));
    T(editor_make_directory(dir, NULL, NULL) == 0);
    struct stat st; T(stat(C26_FIXTURE, &st) == 0 && st.st_size > 0);
    /* (P) every record carries at least a JOURNAL_HEADER, and payloads are
     * part of the file, so the file size bounds both. */
    c26_records s = {.cap = (size_t)st.st_size / JOURNAL_HEADER, .size = (size_t)st.st_size};
    s.rec = calloc(s.cap, sizeof *s.rec); s.data = malloc(s.size);
    uint8_t base_data[JOURNAL_BASE_MAX]; size_t base_size = 0; char path[IPC_PATH_CAP] = "";
    char head[8] = ""; size_t inserted = 0, bases = 0, targets = 0; int rc = s.rec && s.data ? 0 : 1;
    journal_replay_result result;
    if (!rc) rc = journal_scan_file(C26_FIXTURE, c26_collect, &s, &result) || result.corrupt;
    journal_base fresh; if (!rc) rc = journal_capture_base(source, &fresh);
    if (!rc) rc = journal_encode_base(&fresh, base_data, sizeof base_data, &base_size);
    for (size_t i = 0; !rc && i < s.count; i++) {
        journal_record *r = &s.rec[i];
        if (r->type == JOURNAL_BASE) {
            journal_base old;
            rc = journal_decode_base(r, &old, path, sizeof path);
            if (!rc && (old.size != C26_BYTES || strcmp(path, C26_DRIVE_BASE))) rc = 1;
            r->data = base_data; r->size = base_size; bases++;
        } else if (r->type == JOURNAL_TARGET) {
            /* The checkpoint names the open path too; recovery opens TARGET,
             * so it must name the regenerated file, or the install would open
             * the drive's corpus (a different generation than this BASE). */
            if (strcmp((const char *)r->data, C26_DRIVE_BASE)) rc = 1;
            r->data = (const uint8_t *)source; r->size = strlen(source) + 1; targets++;
        } else if (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE) {
            uint64_t off = 0; for (int b = 7; b >= 0; b--) off = off << 8 | r->data[b]; /* LE wire */
            printf("c26: fixture record %s offset=%llu size=%zu\n", r->type == JOURNAL_INSERT ? "INSERT" : "DELETE",
                (unsigned long long)off, r->size);
            /* (M) the drive's journal: two one-byte INSERTs at 0 and 1, no DELETE. */
            if (r->type != JOURNAL_INSERT || off != inserted || r->size != sizeof off + 1 || inserted + 1 >= sizeof head) rc = 1;
            else head[inserted++] = (char)r->data[sizeof off];
        }
    }
    printf("c26: fixture records=%zu bases=%zu targets=%zu inserted=%zu base=%s head=%s\n", s.count, bases, targets, inserted, path, head);
    /* (M) one BASE, one TARGET, two INSERTs (this test's dump of the fixture). */
    if (!rc && (bases != 1 || targets != 1 || inserted != 2)) rc = 1;
    T(snprintf(orphan, sizeof orphan, "%s/session-C26DRV", dir) > 0);
    work_pool pool; bool pooled = !rc && !work_pool_init(&pool, 1, 0);
    journal *j = NULL;
    if (!rc) rc = !pooled || journal_open(&j, orphan, &pool, NULL) || journal_rotate(j, s.rec, s.count);
    if (j) journal_close(j);
    if (pooled) work_pool_shutdown(&pool);
    free(s.rec); free(s.data);
    T(rc == 0);
    T(c26_relaunch(dir, orphan, head, "dense_short.txt") == 0);
    fm4_remove_tree(xdg);
    return 0;
}
static int c26_dense_hot_exit(void)
{
    char made[] = "build/editor-c26-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    char source[IPC_PATH_CAP]; T(snprintf(source, sizeof source, "%s/dense_short.txt", root) > 0);
    const char *saved_env = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved_env != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved_env) > 0);
    int rc = c26_make_source(source);
    if (!rc) rc = c26_dense_hot_exit_body(root, source);
    if (!rc) rc = c26_drive_journal_body(root, source);
    char sub[IPC_PATH_CAP];
    if (snprintf(sub, sizeof sub, "%s/live", root) > 0) fm4_remove_tree(sub);
    if (snprintf(sub, sizeof sub, "%s/fixture", root) > 0) fm4_remove_tree(sub);
    fm4_remove_tree(root); /* the 64 MiB source too, on success and on every failure path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    if (!rc) puts("c26: dense64 hot-exit session and the drive-5 task-37 journal relaunch with the dirty tab restored");
    return rc;
}
/* edit-c26 end */
/* edit-y5x: install_failure now covers capacity deferral (edit-rzj). Force an
 * actual install error by making a formerly missing TARGET a directory: its
 * empty BASE still replays, but file acquisition rejects the TARGET. All
 * numbers below are (P): zero is empty/success/the first tab, one is a single
 * buffer or absent descriptor; geometry fits the notice plus tab/text/status.
 * Owner-only directory permissions are the POSIX read/write/search bits. */
typedef struct y5x_launch {
    editor *e;
    render_backend backend;
    int lease;
} y5x_launch;
static int leased_install_failure_body(const char *root, bool leased, bool start_empty, y5x_launch *launch)
{
    static const char notice[] = "session recovery failed; its journal was set aside";
    char source[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP], lock[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(first, sizeof first, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(first); T(fd >= 0); T(close(fd) == 0);
    T(render_null_backend(&launch->backend) == 0);
    editor_config config = {.path = source, .journal_path = first,
        .cols = (uint32_t)strlen(notice), .rows = 1u + 1u + 1u};
    T(editor_open(&launch->e, &config, &launch->backend) == 0);
    T(press(launch->e, 'M', 0, "MK") == 0);
    editor_close(launch->e); launch->e = NULL;
    T(mkdir(source, 0700) == 0);
    if (leased) T(journal_session_create(root, next, sizeof next, &launch->lease) == JOURNAL_OK);
    else {
        T(snprintf(next, sizeof next, "%s/session-XXXXXX", root) > 0);
        fd = mkstemp(next); T(fd >= 0); T(close(fd) == 0);
    }
    T(snprintf(lock, sizeof lock, "%s/.%s.lock", root, strrchr(next, '/') + 1) > 0);
    /* (P) The profile consists of source directory + recovery journal + its
     * lock + launch journal + optional launch lock. No other files allowed. */
    fm4_entry before[1u + 1u + 1u + 1u + 1u], after[sizeof before / sizeof before[0]];
    size_t nbefore = fm4_list(root, before, sizeof before / sizeof before[0]); T(nbefore != SIZE_MAX);
    struct stat original; T(stat(first, &original) == 0);
    launch->backend = (render_backend){0}; T(render_null_backend(&launch->backend) == 0);
    config = (editor_config){.journal_path = next, .session_lease = leased ? &launch->lease : NULL,
        .start_empty = start_empty, .cols = (uint32_t)strlen(notice), .rows = 1u + 1u + 1u};
    int rc = editor_open(&launch->e, &config, &launch->backend);
    fprintf(stderr, "edit-y5x: leased=%s start_empty=%s editor_open=%d\n",
        leased ? "yes" : "no", start_empty ? "yes" : "no", rc);
    T(rc == 0 && launch->e);
    editor *e = launch->e;
    T(e->recovery_notice && !strcmp(e->recovery_notice, notice));
    T(e->buffer && piece_len(e->tree) == 0 && tabs_count(&e->tabs) == (start_empty ? 0u : 1u));
    if (start_empty) {
        T(e->buffer == e->empty);
        for (size_t i = 0; i < e->buffer_capacity; i++) T(!e->buffers[i]);
    } else T(!editor_tab(e, 0)->modified);
    T(*e->recovery_aside && access(first, F_OK) < 0 && file_contains(e->recovery_aside, "MK"));
    size_t nafter = fm4_list(root, after, sizeof after / sizeof after[0]);
    T(nafter == nbefore + (leased ? 0u : 1u)); /* (P) only a non-leased launch creates its lock here. */
    for (size_t i = 0; i < nbefore; i++) {
        if (!strcmp(before[i].name, strrchr(next, '/') + 1)) {
            if (start_empty) {
                struct stat empty; T(stat(next, &empty) == 0 && empty.st_size == 0);
                T(empty.st_dev == before[i].st.st_dev && empty.st_ino == before[i].st.st_ino);
            }
            continue; /* a live tab may already have appended to the destination */
        }
        const char *name = !strcmp(before[i].name, strrchr(first, '/') + 1) ?
            strrchr(e->recovery_aside, '/') + 1 : before[i].name;
        size_t j = 0; while (j < nafter && strcmp(name, after[j].name)) j++;
        T(j < nafter);
        const struct stat *a = &before[i].st, *b = &after[j].st;
        T(a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size && a->st_mode == b->st_mode);
        T(a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec);
    }
    struct stat named, held; T(stat(lock, &named) == 0);
    T(e->session_lock_owned);
    T(fstat(e->session_lock, &held) == 0 && held.st_dev == named.st_dev && held.st_ino == named.st_ino);
    if (leased) T(fstat(launch->lease, &held) == 0 && held.st_dev == named.st_dev && held.st_ino == named.st_ino);
    char aside[IPC_PATH_CAP]; strcpy(aside, e->recovery_aside);
    editor_close(e); launch->e = NULL;
    struct stat st; T(stat(aside, &st) == 0 && st.st_ino == original.st_ino && st.st_size == original.st_size);
    if (start_empty) T(access(next, F_OK) < 0 && access(lock, F_OK) < 0);
    else {
        T(stat(next, &st) == 0 && st.st_size > 0);
        T(stat(lock, &st) == 0 && st.st_dev == named.st_dev && st.st_ino == named.st_ino);
        if (leased) T(journal_session_release(next, launch->lease) == JOURNAL_BUSY);
    }
    puts("edit-y5x: empty buffer, set-aside notice, profile preservation and close consistency passed");
    return 0;
}
static int leased_install_failure(bool leased, bool start_empty)
{
    char root[] = "build/editor-y5x-XXXXXX"; T(mkdtemp(root));
    y5x_launch launch = {.lease = -1}; /* (P) no descriptor yet */
    int rc = leased_install_failure_body(root, leased, start_empty, &launch);
    if (launch.e) editor_close(launch.e);
    if (launch.lease >= 0) close(launch.lease);
    char source[IPC_PATH_CAP];
    if (snprintf(source, sizeof source, "%s/source", root) > 0) (void)rmdir(source);
    fm4_remove_tree(root);
    return rc;
}
/* edit-ivv begin: JOURNAL_BUSY from the shared directory guard (edit-dr9)
 * is retried on the journal tick by every checkpoint path, never a hard
 * error. The test holds the guard LOCK_EX on its own open file description,
 * as journal_retire_directory does (journal_test --rotate-busy pattern).
 * (P) zero is success/empty/the first tab/no refusal; one is a single tab.
 * (E) watchdogs reuse EDITOR_IDLE_NS (settle). The two synchronous retries
 * (save worker, close) attempt once, wait one JOURNAL_DEFAULT_SYNC_NS tick and
 * attempt again: the guard is released half a tick (JOURNAL_DEFAULT_SYNC_NS /
 * 2) after the refused attempt starts, between the first attempt and the
 * retry. No new duration or count is introduced. */
static int ivv_hold(const char *dir)
{
    int guard = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (guard >= 0 && flock(guard, LOCK_EX | LOCK_NB)) { close(guard); guard = -1; }
    return guard;
}
static int ivv_text(editor *e, const char *expected)
{
    uint8_t bytes[sizeof "MK"]; uint64_t len = editor_length(e);
    T(len == strlen(expected) && len < sizeof bytes && editor_read(e, 0, bytes, (size_t)len) == 0);
    T(!memcmp(bytes, expected, (size_t)len)); return 0;
}
/* (a) A launch installs a recovered session while another editor's sweep
 * holds the guard: no set-aside, the text stays, the source is retired only
 * after the owed checkpoint lands. */
typedef struct ivv_paths { char source[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP], lock[IPC_PATH_CAP]; } ivv_paths;
static int ivv_recovery_body(const char *root, ivv_paths *p, editor **out, render_backend *backend, int *guard)
{
    char *source = p->source, *first = p->first, *next = p->next, *lock = p->lock;
    T(snprintf(source, IPC_PATH_CAP, "%s/source", root) > 0);
    T(snprintf(first, IPC_PATH_CAP, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(first); T(fd >= 0); T(close(fd) == 0);
    T(render_null_backend(backend) == 0);
    editor_config config = {.path = source, .journal_path = first, .cols = 32, .rows = 3};
    T(editor_open(out, &config, backend) == 0);
    T(press(*out, 'M', 0, "MK") == 0);
    editor_close(*out); *out = NULL;
    T(snprintf(next, IPC_PATH_CAP, "%s/session-XXXXXX", root) > 0);
    fd = mkstemp(next); T(fd >= 0); T(close(fd) == 0);
    T(snprintf(lock, IPC_PATH_CAP, "%s/.%s.lock", root, strrchr(first, '/') + 1) > 0);
    *guard = ivv_hold(root); T(*guard >= 0);
    *backend = (render_backend){0}; T(render_null_backend(backend) == 0);
    config = (editor_config){.journal_path = next, .cols = 32, .rows = 3};
    int rc = editor_open(out, &config, backend);
    editor *e = *out;
    fprintf(stderr, "edit-ivv (a): guard held editor_open=%d refusals=%u owed=%d notice=%s aside=%s source_kept=%s len=%llu\n", rc,
        e ? e->busy_refusals : 0u, e ? e->busy_checkpoint : 0, e && e->recovery_notice ? e->recovery_notice : "(none)",
        e && *e->recovery_aside ? e->recovery_aside : "(none)", access(first, F_OK) ? "no" : "yes",
        e ? (unsigned long long)editor_length(e) : 0ull);
    T(rc == 0 && e);
    T(e->busy_refusals > 0 && e->busy_checkpoint && !*e->recovery_aside && !access(first, F_OK));
    T(!e->recovery_notice); /* (P) one refusal is within the one-tick bound: silent */
    T(ivv_text(e, "MK") == 0);
    T(turn(e) == 0); T(turn(e) == 0); T(!access(first, F_OK) && e->busy_checkpoint);
    T(close(*guard) == 0); *guard = -1;
    uint64_t limit = deadline();
    while (e->busy_checkpoint) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    fprintf(stderr, "edit-ivv (a): guard released refusals=%u source_kept=%s lock_kept=%s\n", e->busy_refusals,
        access(first, F_OK) ? "no" : "yes", access(lock, F_OK) ? "no" : "yes");
    T(e->busy_refusals == 0 && access(first, F_OK) < 0 && access(lock, F_OK) < 0 && e->busy_source_lease < 0);
    T(ivv_text(e, "MK") == 0);
    editor_close(e); *out = NULL;
    T(file_contains(next, "MK"));
    return 0;
}
static int ivv_recovery(void)
{
    char root[] = "build/editor-ivv-XXXXXX"; T(mkdtemp(root));
    editor *e = NULL; render_backend backend = {0}; int guard = -1;
    static ivv_paths paths; /* outlives the editor that borrows journal_path */
    int rc = ivv_recovery_body(root, &paths, &e, &backend, &guard);
    if (guard >= 0) close(guard);
    if (e) editor_close(e);
    fm4_remove_tree(root);
    return rc;
}
/* (b) A lazy restore's checkpoint is refused: the placeholder and the source
 * stay, and the restore completes on a later tick once the guard is free. */
static int ivv_restore_case(const char *dir, const char *orphan)
{
    struct stat before; T(stat(orphan, &before) == 0);
    qct_launch l = {0};
    int rc = e601_start(dir, &l);
    if (!rc) rc = qct_listed(&l);
    if (rc) { e601_discard(&l); T(rc == 0); }
    editor *e = l.e;
    int guard = ivv_hold(dir); if (guard < 0) { e601_discard(&l); T(guard >= 0); }
    rc = editor_reopen_tab(e);
    uint64_t limit = deadline();
    while (!rc && e->restore_active && !e->busy_refusals && trace_now_ns() < limit) { rc = turn(e); sched_yield(); }
    bool waiting = !rc && e->restore_active && e->busy_refusals > 0 && e->buffer->placeholder && e601_same(orphan, &before);
    fprintf(stderr, "edit-ivv (b): guard held rc=%d restore_active=%zu refusals=%u placeholder=%s source_kept=%s notice=%s\n", rc,
        (size_t)e->restore_active, e->busy_refusals, e->buffer->placeholder ? "yes" : "no",
        e601_same(orphan, &before) ? "yes" : "no", e->recovery_notice ? e->recovery_notice : "(none)");
    close(guard);
    if (waiting) rc = e601_drive(e);
    bool live = waiting && !rc && tabs_count(&e->tabs) == 1 && !e->buffer->placeholder && !e->restore_active &&
        piece_len(e->buffer->tree) == FM4_GIB + strlen("x");
    bool retired = access(orphan, F_OK) < 0;
    fprintf(stderr, "edit-ivv (b): guard released rc=%d restored=%s len=%llu source_retired=%s refusals=%u\n", rc, live ? "yes" : "no",
        (unsigned long long)piece_len(e->buffer->tree), retired ? "yes" : "no", e->busy_refusals);
    int consistent = live ? journal_flush(e->journal) || e601_consistent(l.journal) : 1;
    e601_discard(&l);
    T(waiting && live && retired && consistent == 0);
    return 0;
}
static int ivv_restore(void)
{
    char made[] = "build/editor-ivv-XXXXXX"; T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved_env = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved_env != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved_env) > 0);
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], orphan[IPC_PATH_CAP] = "";
    int rc = setenv("XDG_DATA_HOME", root, 1) || journal_default_dir(dir, sizeof dir) || editor_make_directory(dir, NULL, NULL);
    if (!rc) rc = snprintf(source, sizeof source, "%s/corpus", root) <= 0;
    int fd = rc ? -1 : open(source, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd >= 0) { rc = ftruncate(fd, (off_t)FM4_GIB) != 0; close(fd); } else rc = 1; /* sparse */
    if (!rc) rc = qct_orphan(dir, source, orphan);
    if (!rc) rc = ivv_restore_case(dir, orphan);
    fm4_remove_tree(root); /* on success and on every failure path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    return rc;
}
/* (c) A save whose journal rotation (prepare before the write, finish after
 * it) meets the held guard completes after it is released, file and flag. */
static int ivv_save(int phase)
{
    fixture f = {0}; T(open_fixture(&f, true, false) == 0);
    T(press(f.e, 'x', 0, "x") == 0);
    held_save held = {.phase = phase};
    if (phase) T(savectl_set_step_hook(f.e->buffer->save, hold, &held) == 0);
    int guard = phase ? -1 : ivv_hold(f.directory);
    if (!phase) T(guard >= 0);
    T(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0);
    if (phase) { T(enter_hold(f.e, &held) == 0); guard = ivv_hold(f.directory); T(guard >= 0); atomic_store(&held.release, true); }
    uint64_t release = trace_now_ns() + JOURNAL_DEFAULT_SYNC_NS / 2u;
    while (trace_now_ns() < release) { T(turn(f.e) == 0); sched_yield(); }
    T(close(guard) == 0);
    T(saved(f.e) == 0);
    bool modified = editor_tab(f.e, 0)->modified;
    fprintf(stderr, "edit-ivv (c): save %s guard: modified=%s save_error=%d operation_error=%d\n", phase ? "finish" : "prepare",
        modified ? "yes" : "no", f.e->buffer->save_error, f.e->buffer->operation_error);
    T(disk_is(f.path, "xbase") == 0); T(!modified && !f.e->buffer->save_error && !f.e->buffer->operation_error);
    close_fixture(&f); return 0;
}
/* (e) editor_close flushes a failed journal while the guard is held: the
 * checkpoint fallback retries one tick later instead of being skipped. */
typedef struct ivv_release { int guard; uint64_t at; } ivv_release;
static void *ivv_release_later(void *ctx)
{
    ivv_release *r = ctx;
    while (trace_now_ns() < r->at) sched_yield();
    close(r->guard); return NULL;
}
static int ivv_close_body(const char *root, char *log, editor **out, render_backend *backend, bool settle_first)
{
    T(snprintf(log, IPC_PATH_CAP, "%s/session", root) > 0);
    T(render_null_backend(backend) == 0);
    editor_config config = {.journal_path = log, .initial = (const uint8_t *)"checkpoint", .initial_len = strlen("checkpoint")};
    T(editor_open(out, &config, backend) == 0); editor *e = *out;
    T(settle(e) == 0); T(press(e, 'z', 0, "z") == 0); T(editor_flush(e) == 0);
    struct stat before, after; T(stat(log, &before) == 0);
    e->stats.journal_error = JOURNAL_FULL; /* accepted edits held only by a failed journal */
    ivv_release r = {ivv_hold(root), trace_now_ns() + JOURNAL_DEFAULT_SYNC_NS / 2u}; T(r.guard >= 0);
    pthread_t thread; T(pthread_create(&thread, NULL, ivv_release_later, &r) == 0);
    int flushed = settle_first ? editor_flush_settle(e) : 0;
    if (!settle_first) { editor_close(e); *out = NULL; }
    T(pthread_join(thread, NULL) == 0);
    T(stat(log, &after) == 0);
    fprintf(stderr, "edit-ivv (e): %s with guard held: flushed=%d rotated=%s\n", settle_first ? "final flush" : "editor_close",
        flushed, after.st_ino != before.st_ino ? "yes" : "no");
    T(flushed == 0 && after.st_ino != before.st_ino && file_contains(log, "z"));
    return 0;
}
static int ivv_close(bool settle_first)
{
    char root[] = "build/editor-ivv-XXXXXX"; T(mkdtemp(root));
    editor *e = NULL; render_backend backend = {0}; char log[IPC_PATH_CAP];
    int rc = ivv_close_body(root, log, &e, &backend, settle_first);
    if (e) editor_close(e);
    fm4_remove_tree(root);
    return rc;
}
static int ivv_all(void)
{
    int failed = ivv_recovery();
    failed |= ivv_restore();
    failed |= ivv_save(0);
    failed |= ivv_save(FILE_STEP_DIR_SYNCED);
    failed |= ivv_close(false);
    failed |= ivv_close(true);
    if (!failed) puts("edit-ivv: busy directory guard: recovery install, lazy restore, save prepare/finish, close and final flush retry on the journal tick");
    return failed;
}
/* edit-ivv end */
/* edit-uzrb (P): FM4_GIB is the brief's sparse GiB fixture. The COPY
 * threshold is its size plus one byte, keeping full acquisition behind the
 * held bulk lane while the raster lane supplies the preview. Tab indices
 * zero/one follow the small/large pair; reverse them to cover a rank gap.
 * Geometry is the existing fixture's status width and tab/text/status rows.
 * Zero/one also mean API success, Boolean state and string terminators;
 * 0600 is POSIX owner read/write. (E) deadline() reuses EDITOR_IDLE_NS. */
static int pending_open_save(bool pending_first)
{
    fixture f = {0}; held_bulk bulk = {0};
    char large[IPC_PATH_CAP] = {0}; int fd = -1, result = 0;
#define C(x) do { if (!(x)) { fprintf(stderr, "editor_session_test:%d: FAIL %s\n", __LINE__, #x); result = 1; goto done; } } while (0)
    strcpy(f.directory, "build/editor-uzrb-XXXXXX"); C(mkdtemp(f.directory));
    C(snprintf(f.path, sizeof f.path, "%s/source", f.directory) > 0);
    C(snprintf(f.log, sizeof f.log, "%s/journal", f.directory) > 0);
    C(snprintf(large, sizeof large, "%s/large", f.directory) > 0);
    fd = open(f.path, O_WRONLY | O_CREAT | O_EXCL, 0600); C(fd >= 0);
    C(write(fd, "base", strlen("base")) == (ssize_t)strlen("base"));
    C(close(fd) == 0); fd = -1;
    fd = open(large, O_WRONLY | O_CREAT | O_EXCL, 0600); C(fd >= 0);
    C(ftruncate(fd, (off_t)FM4_GIB) == 0); C(close(fd) == 0); fd = -1;
    C(render_null_backend(&f.backend) == 0);
    f.backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config config = {.path = f.path, .journal_path = f.log,
        .copy_threshold = FM4_GIB + 1u,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    C(editor_open(&f.e, &config, &f.backend) == 0); C(settle(f.e) == 0);
    C(press(f.e, 'x', 0, "x") == 0);
    editor_buffer *small = f.e->buffer;
    work_handle handle = work_submit(&f.e->pool, (work_job){hold_bulk, &bulk, 0, WORK_BULK}); C(handle.epoch);
    uint64_t limit = deadline();
    while (!atomic_load(&bulk.entered)) { C(trace_now_ns() < limit); sched_yield(); }
    uint64_t id; C(editor_add_buffer(f.e, large, NULL, 0, &id) == 0);
    editor_buffer *pending = f.e->buffer;
    C(pending->pending_open && !file_open_ready(pending->file));
    if (pending_first) { C(tabs_reorder(&f.e->tabs, 1, 0) == 0); editor_session_metadata(f.e); }
    size_t small_index = pending_first ? 1u : 0u;
    C(editor_select_tab(f.e, small_index) == 0); C(settle(f.e) == 0);
    C(f.e->buffer == small && editor_tab(f.e, small_index)->modified && pending->pending_open);
    edit_malloc_guard_begin();
    int pressed = press(f.e, 's', PLAT_MOD_CTRL, NULL);
    size_t allocations = edit_malloc_guard_end();
    C(pressed == 0); if (edit_malloc_guard_active()) C(!allocations);
    fprintf(stderr, "edit-uzrb: pending %s small tab: pending=%s save_active=%s save_error=%d\n",
        pending_first ? "before" : "after", pending->pending_open ? "yes" : "no",
        editor_save_active(f.e) ? "yes" : "no", small->save_error);
    C(pending->pending_open && editor_save_active(f.e) && !small->save_error && !small->operation_error);
    atomic_store(&bulk.release, true);
    C(saved(f.e) == 0); C(disk_is(f.path, "xbase") == 0);
    C(!editor_tab(f.e, small_index)->modified && !small->save_error && !small->operation_error);
    C(!pending->pending_open);
    C(press(f.e, 'y', 0, "y") == 0); C(press(f.e, 's', PLAT_MOD_CTRL, NULL) == 0);
    C(saved(f.e) == 0); C(disk_is(f.path, "xybase") == 0);
    C(!editor_tab(f.e, small_index)->modified && !small->save_error && !small->operation_error);
done:
    atomic_store(&bulk.release, true);
    if (fd >= 0) close(fd);
    if (f.e) editor_close(f.e);
    fm4_remove_tree(f.directory); /* sparse source and journal on every exit */
#undef C
    if (!result) puts("edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified");
    return result;
}
int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    /* (P) executable plus focused-test option; zero is API success. */
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--explicit-changed-base")) return explicit_changed_base();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-qcuh")) return retired_named_release(true) || retired_named_release(false);
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-ivv")) return ivv_all();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-fkpk")) { int a = mapped_repeated_save(true), b = mapped_repeated_save(false); return a || b; }
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-3620")) return kept_target_unreadable();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-ut0m")) { int a = mapped_keep_save(true), b = mapped_keep_save(false); return a || b; }
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-djg"))
        return explicit_changed_base() || session_artifacts() || changed_base(false) || changed_base(true) || existing_journal();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-4jhi")) return retained_lazy();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-uzrb"))
        return pending_open_save(false) || pending_open_save(true);
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-dxas")) return dxas_all();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--edit-14bi"))
        return retained_lazy_case(true, UINT64_MAX, false);
    T(pending_open_save(false) == 0); T(pending_open_save(true) == 0);
    T(ivv_all() == 0);
    T(retained_lazy() == 0);
    T(leased_install_failure(false, false) == 0);
    T(leased_install_failure(true, false) == 0);
    T(leased_install_failure(true, true) == 0);
    T(explicit_changed_base() == 0);
    T(clean_tab_set() == 0);
    T(large_restore() == 0);
    T(e601_restore_failures() == 0);
    T(large_orphans() == 0);
    T(c26_dense_hot_exit() == 0);
    T(recoverable_save_retry(false, false) == 0); T(recoverable_save_retry(true, false) == 0);
    T(recoverable_save_retry(false, true) == 0); T(recoverable_save_retry(true, true) == 0);
    T(session_artifacts() == 0); T(changed_base(false) == 0); T(changed_base(true) == 0); T(install_failure() == 0);
    T(unavailable_target_parent() == 0); T(existing_journal() == 0);
    T(repeated_save(false, false) == 0); T(repeated_save(true, false) == 0); T(repeated_save(true, true) == 0);
    T(mapped_repeated_save(true) == 0); T(mapped_repeated_save(false) == 0);
    T(mapped_keep_save(true) == 0); T(mapped_keep_save(false) == 0);
    T(kept_target_unreadable() == 0);
    T(dxas_all() == 0);
    T(interrupted_save(FILE_STEP_TEMP_WRITTEN) == 0); T(interrupted_save(FILE_STEP_RENAMED) == 0);
    T(post_cutoff() == 0); T(saving_frame_busy() == 0); T(shared_target() == 0); T(failed_inactive() == 0);
    T(evicted_saving_tab() == 0); T(checkpoint_during_evicted_save() == 0);
    T(crash_after_evicted_checkpoint() == 0);
    T(retired_named_release(true) == 0); T(retired_named_release(false) == 0);
    puts("editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, retired named buffers release slots and fds, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed");
    return 0;
}
