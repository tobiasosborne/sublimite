/* (P) Fixture length = FILE_PREFIX_MAX + the tail's non-NUL bytes, forcing
 * COPY acquisition beyond the preview. Geometry = status width and tab/text/
 * status rows. Zero/one literals denote empty/success, Boolean states, first
 * tab, unit increments and string terminators; 0600 is POSIX owner read/write.
 * (E) Watchdogs reuse EDITOR_IDLE_NS, the existing editor loop policy. */
#include "editor/private.h"
#include "trace/trace.h"
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_checkpoint_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct held_bulk { atomic_bool entered; } held_bulk;
static void hold_bulk(work_ctx *ctx)
{
    held_bulk *held = ctx->arg;
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    atomic_store(&held->entered, true);
    while (!work_should_stop(ctx) && trace_now_ns() < limit) sched_yield();
}
static int settle(editor *e)
{
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    do {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < limit); sched_yield();
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active || editor_save_active(e));
    return 0;
}
static int cleanup(const char *root)
{
    DIR *directory = opendir(root); T(directory); struct dirent *entry;
    while ((entry = readdir(directory))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        T(unlinkat(dirfd(directory), entry->d_name, 0) == 0);
    T(closedir(directory) == 0 && rmdir(root) == 0);
    return 0;
}
typedef struct checkpoint_shape { uint64_t id; bool base, target, view, replacement; } checkpoint_shape;
static int inspect(void *ctx, const journal_record *r)
{
    checkpoint_shape *shape = ctx;
    if (r->buffer_id != shape->id) return 0;
    if (r->type == JOURNAL_BASE) shape->base = true;
    if (r->type == JOURNAL_TARGET) shape->target = true;
    if (r->type == JOURNAL_VIEW) shape->view = true;
    if (r->type == JOURNAL_DELETE || r->type == JOURNAL_INSERT) shape->replacement = true;
    return 0;
}
static int crash_checkpoint(const char *source, const char *log, size_t len, bool active)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Match editor_large_test's null-backend acquisition lane: prefix work
     * needs a raster worker while the full COPY remains held on bulk. */
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config config = {.journal_path = log, .initial = (const uint8_t *)"other tab",
        .initial_len = sizeof "other tab" - 1, .copy_threshold = len + 1,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    held_bulk held = {0};
    work_handle handle = work_submit(&e->pool, (work_job){hold_bulk, &held, 0, WORK_BULK}); T(handle.epoch);
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&held.entered)) { T(trace_now_ns() < limit); sched_yield(); }
    uint64_t id; T(editor_add_buffer(e, source, NULL, 0, &id) == 0);
    editor_buffer *pending = e->buffer;
    T(pending->pending_open && file_open_mode(pending->file) == FILE_MODE_COPY);
    T(!file_open_ready(pending->file) && piece_len(pending->tree) == 0);
    T(piece_len(pending->preview) == FILE_PREFIX_MAX && pending->base.size == len);
    if (!active) T(editor_select_tab(e, 0) == 0);
    /* Rotate while another tab's full COPY is queued behind the held bulk job.
     * Abrupt exit deliberately skips editor_close and adoption. */
    T(journal_flush(e->journal) == 0);
    T(editor_checkpoint(e) == 0); T(pending->pending_open);
    _exit(0);
}
static int pending_checkpoint(bool active)
{
    char root[] = "build/editor-checkpoint-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    const char tail[] = "\npending tail\n";
    size_t len = FILE_PREFIX_MAX + sizeof tail - 1;
    uint8_t *expected = malloc(len), *actual = malloc(len + 1); T(expected && actual);
    memset(expected, 'x', FILE_PREFIX_MAX); memcpy(expected + FILE_PREFIX_MAX, tail, sizeof tail - 1);
    int fd = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, expected, len) == (ssize_t)len); T(close(fd) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(crash_checkpoint(source, log, len, active));
    int status; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* (P) Pending ID is the next ID after the first tab's ID. */
    checkpoint_shape shape = {.id = 1 + 1}; journal_replay_result replay;
    T(journal_replay_file(log, inspect, &shape, &replay) == 0 && !replay.corrupt);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    T(tabs_count(&e->tabs) == shape.id);
    bool order = !strcmp(editor_tab(e, 0)->title, "untitled");
    bool active_preserved = tabs_active_index(&e->tabs) == (active ? 1u : 0u);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    uint64_t recovered_len = editor_length(e);
    bool intact = recovered_len == len && editor_read(e, 0, actual, len) == 0 && !memcmp(actual, expected, len);
    bool clean = !editor_tab(e, 1)->modified;
    /* Saving recovered contents must preserve the complete on-disk document. */
    plat_event save = {.kind = PLAT_EV_KEY, .press = true, .keysym = 's', .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &save) == 0); T(settle(e) == 0); T(!e->buffer->save_error);
    fd = open(source, O_RDONLY); T(fd >= 0);
    ssize_t disk_len = read(fd, actual, len + 1); T(close(fd) == 0);
    bool disk_intact = disk_len == (ssize_t)len && !memcmp(actual, expected, len);
    editor_close(e); free(actual); free(expected);
    T(cleanup(root) == 0);
    printf("editor_checkpoint_test: %s pending COPY recovered=%llu expected=%zu clean=%s disk_intact=%s\n",
        active ? "active" : "inactive", (unsigned long long)recovered_len, len,
        clean ? "yes" : "no", disk_intact ? "yes" : "no");
    T(fflush(stdout) == 0);
    T(intact); T(clean); T(disk_intact); T(active_preserved); T(order);
    T(shape.base && shape.target && shape.view && !shape.replacement);
    return 0;
}
static int adopted_empty_checkpoint(void)
{
    char root[] = "build/editor-checkpoint-empty-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    const char marker[] = "delete me";
    int fd = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, marker, sizeof marker - 1) == (ssize_t)(sizeof marker - 1)); T(close(fd) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = log,
            .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(!e->buffer->pending_open);
        plat_event select = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'a', .mods = PLAT_MOD_CTRL};
        T(editor_inject(e, &select) == 0); T(settle(e) == 0);
        plat_event remove = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_BackSpace};
        T(editor_inject(e, &remove) == 0); T(settle(e) == 0);
        T(editor_length(e) == 0 && editor_tab(e, 0)->modified);
        T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0); _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    checkpoint_shape shape = {.id = 1}; journal_replay_result replay;
    T(journal_replay_file(log, inspect, &shape, &replay) == 0 && !replay.corrupt);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    bool empty_modified = editor_length(e) == 0 && editor_tab(e, 0)->modified;
    editor_close(e); T(cleanup(root) == 0);
    T(empty_modified); T(shape.base && shape.target && shape.view && shape.replacement);
    puts("editor_checkpoint_test: adopted deletion to empty survives checkpoint/crash/recovery");
    return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(pending_checkpoint(false) == 0); T(pending_checkpoint(true) == 0);
    T(adopted_empty_checkpoint() == 0);
    puts("editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save passed");
    return 0;
}
