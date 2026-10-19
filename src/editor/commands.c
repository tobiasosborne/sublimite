/* File setup is maintenance, after the input frame retires. Prompt edits use
 * editor-owned storage; buffer creation and filesystem work never run there. */
#include "editor/private.h"
#include "trace/trace.h"
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>

static int canonical_target(const char *input, char out[IPC_PATH_CAP])
{
    if (!*input || strlen(input) >= IPC_PATH_CAP) return EDITOR_ERR_ARG;
    if (realpath(input, out)) return 0;
    if (errno != ENOENT) return EDITOR_ERR_IO;
    char parent[IPC_PATH_CAP], leaf[IPC_PATH_CAP]; strcpy(parent, input);
    char *slash = strrchr(parent, '/'); strcpy(leaf, slash ? slash + 1 : parent);
    if (!*leaf) return EDITOR_ERR_ARG;
    if (slash == parent) slash[1] = '\0';
    else if (slash) *slash = '\0';
    else strcpy(parent, ".");
    char canonical[IPC_PATH_CAP]; if (!realpath(parent, canonical)) return EDITOR_ERR_IO;
    int n = snprintf(out, IPC_PATH_CAP, "%s/%s", canonical, leaf);
    return n < 0 || (size_t)n >= IPC_PATH_CAP ? EDITOR_ERR_ARG : 0;
}
/* edit-ze59 (recovery-3 F10): Save As writes the target on the bulk worker.
 * A journal-less save controller (the save path's worker-owned write, its
 * validators and source guard) writes the snapshot taken at the command;
 * the UI keeps typing and adopts the result in maintenance
 * (editor_save_as_tick): rebind, peers, then a worker checkpoint
 * (publish_owed). The BASE capture of the written target runs on the same
 * worker after the write (release callback). */
struct editor_save_as {
    savectl *writer;
    file_source *source;
    piece_snapshot *snapshot; /* the written snapshot (retained) */
    file_id baseline;
    uint64_t buffer_id;
    char path[IPC_PATH_CAP];
    bool want_base;
    int base_rc;
    journal_base base;
};
static void save_as_written(void *ctx)
{
    editor_save_as *a = ctx;
    if (a->want_base) a->base_rc = journal_capture_base(a->path, &a->base);
}
static void save_as_free(editor_save_as *a)
{
    if (!a) return;
    if (a->snapshot) piece_snapshot_release(a->snapshot);
    file_source_release(a->source);
    free(a);
}
static int save_as(editor *e, const char *input)
{
    editor_buffer *b = e->buffer;
    if (!tabs_count(&e->tabs) || b->source_stale || b->pending_open) return EDITOR_ERR_ARG;
    char path[IPC_PATH_CAP]; int rc = canonical_target(input, path); if (rc) return rc;
    /* edit-hw1: refuse before the write can invalidate a modified peer's
     * differences-only BASE. Saved history, rather than frame dirtiness,
     * determines whether its text may be replaced. */
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *tab = tabs_at(&e->tabs, i);
        editor_buffer *other = (editor_buffer *)((uint8_t *)tab->undo - offsetof(editor_buffer, undo));
        if (other == b || !editor_same_path(path, other->path) ||
            (!other->saved_lost && other->history_cursor == other->saved_cursor)) continue;
        (void)snprintf(e->file_command_notice, sizeof e->file_command_notice,
            "%s is open and modified in another tab", tab->title);
        e->recovery_notice = e->file_command_notice;
        return EDITOR_ERR_ARG;
    }
    if (!strcmp(path, b->save_path)) return editor_save_request(e, 0);
    if (b->save) {
        savectl_model model = savectl_get_model(b->save);
        if (model.busy || model.can_recover_cancel || model.needs_finish) return EDITOR_ERR_ARG;
    }
    file_id baseline;
    rc = file_id_stat_path(path, &baseline); if (rc) return EDITOR_ERR_IO;
    editor_save_as *a = calloc(1, sizeof *a); if (!a) return EDITOR_ERR_MEMORY;
    a->base.path = ""; a->baseline = baseline; a->buffer_id = b->id; strcpy(a->path, path);
    a->want_base = e->journal != NULL;
    /* Preserve the original mapping's guard independently of the new target
     * identity. COPY trees own their bytes and need no old-path guard. */
    if (b->file && file_open_mode(b->file) == FILE_MODE_MMAP && file_source_acquire(b->file, &a->source)) { free(a); return EDITOR_ERR_IO; }
    savectl_options options = {.pool = &e->pool, .path = path, .baseline = baseline,
        .source_mode = a->source ? FILE_MODE_MMAP : FILE_MODE_COPY,
        .validate_source = a->source ? file_source_validate : NULL, .source_ctx = a->source};
    if (savectl_create(&a->writer, &options, true)) { save_as_free(a); return EDITOR_ERR_IO; }
    a->snapshot = piece_snapshot_take(b->tree);
    piece_snapshot *task = a->snapshot ? piece_snapshot_retain(a->snapshot) : NULL;
    savectl_request request = {task, a, NULL, save_as_written, trace_now_ns()};
    rc = task ? savectl_save_request(a->writer, &request) : SAVECTL_NOMEM;
    if (rc) {
        if (task) piece_snapshot_release(task);
        savectl_close_begin(a->writer); (void)savectl_destroy(a->writer); save_as_free(a); return EDITOR_ERR_IO;
    }
    /* No status frame belongs to this internal writer. */
    savectl_model started = savectl_get_model(a->writer);
    (void)savectl_status_frame_submitted(a->writer, started.request_id, trace_now_ns());
    /* The written revision's history position, kept across later edits the
     * way a Ctrl+S cutoff is (input.c adjusts save_cursor). */
    b->save_revision = b->revision; b->save_cursor = b->history_cursor; b->save_tracking = true;
    undo_break_burst(&b->undo);
    e->save_as = a;
    return 0;
}
/* The target is written: rebind the buffer (the former synchronous tail). */
static int save_as_adopt(editor *e, editor_buffer *b, size_t tab, editor_save_as *a, file_id saved)
{
    const char *path = a->path;
    savectl_options options = {.pool = &e->pool, .path = path, .baseline = a->baseline,
        .source_mode = a->source ? FILE_MODE_MMAP : FILE_MODE_COPY,
        .validate_source = a->source ? file_source_validate : NULL, .source_ctx = a->source,
        .journal = e->journal, .journal_pool = &e->journal_pool, .buffer_id = b->id};
    savectl *replacement = NULL;
    if (savectl_create(&replacement, &options, false)) return EDITOR_ERR_IO;
    (void)savectl_rebase_target(replacement, &saved);
    if (b->save) {
        savectl_close_begin(b->save);
        if (savectl_destroy(b->save)) { savectl_close_begin(replacement); (void)savectl_destroy(replacement); return EDITOR_ERR_IO; }
    }
    b->save = replacement;
    file_source_release(b->target_source); b->target_source = a->source; a->source = NULL;
    file_watch_path_stop(&b->target_watch);
    strcpy(b->path, path); strcpy(b->save_path, path); b->target_rebound = true;
    b->base = a->base; b->base.path = *a->base.path ? b->base.captured_path : "";
    /* edit-28u: the captured BASE is the file this revision's snapshot was
     * written to only if it is the very generation the write reported. */
    if (*b->base.path && saved.exists && saved.dev == b->base.device && saved.ino == b->base.inode &&
        saved.size == b->base.size && saved.mtime_ns == b->base.mtime_ns &&
        saved.size == piece_snapshot_len(a->snapshot) && (!saved.metadata_valid || saved.ctime_ns == b->base.ctime_ns)) {
        b->saved_text = editor_base_id_of(&b->base); b->saved_text_revision = b->save_revision;
        /* edit-f3i: the written snapshot is the saved text's reference. */
        editor_saved_reference(b, a->snapshot); a->snapshot = NULL;
    }
    const char *title = strrchr(path, '/'); title = title ? title + 1 : path;
    /* (P) retain the tab module's grapheme-safe title capacity. */
    size_t end = 0, length = strlen(title);
    while (end < length) {
        size_t n = utf8_grapheme_next((const uint8_t *)title + end, length - end);
        if (n >= TABS_TITLE_BYTES - end) break;
        end += n;
    }
    int rc = tabs_rename(&e->tabs, tab, title, end, path, strlen(path));
    if (rc) return EDITOR_ERR_IO;
    /* Edits typed while the target was written stay modified. */
    if (b->save_tracking) { b->saved_cursor = b->save_cursor; b->saved_lost = false; }
    else b->saved_lost = true;
    b->save_tracking = false;
    b->save_error = b->operation_error = 0; b->save_errno = 0;
    (void)tabs_set_modified(&e->tabs, tab, b->saved_lost || b->history_cursor != b->saved_cursor);
    /* Discard our own atomic-replacement notice before registering the target.
     * Old file handles stay alive because piece/undo snapshots can borrow them. */
    file_watch_dispatch();
    int fd = file_watch_path_start(&b->target_watch, b->path);
    if (fd < 0) return EDITOR_ERR_IO;
    struct epoll_event event = {.events = EPOLLIN, .data.fd = fd};
    if (epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &event) && errno != EEXIST) return EDITOR_ERR_IO;
    b->watch_fd = fd; b->watch_pending = false;
    /* Peers with unsaved history keep their text (edited while the target
     * was written): no reload; the new BASE makes their checkpoint whole-text. */
    size_t peers = tabs_count(&e->tabs);
    bool *reload = calloc(peers ? peers : 1u, sizeof *reload); if (!reload) return EDITOR_ERR_MEMORY;
    for (size_t i = 0; i < peers; i++) {
        const tabs_tab *peer = tabs_at(&e->tabs, i);
        editor_buffer *other = (editor_buffer *)((uint8_t *)peer->undo - offsetof(editor_buffer, undo));
        reload[i] = other != b && editor_same_path(path, other->path) &&
            !other->saved_lost && other->history_cursor == other->saved_cursor;
    }
    /* Stage all peer BASEs together so each clean reload's complete checkpoint
     * can validate every target. Retained closed tabs keep the existing policy;
     * open peers adopt the saved bytes below before this command completes. */
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *other = e->buffers[i];
        if (!other || other == b || other->retired || strcmp(other->save_path, path)) continue;
        other->base = b->base;
        other->base.path = *b->base.path ? other->base.captured_path : "";
        if (other->file) (void)file_resolve_keep(other->file);
        if (other->save) (void)savectl_rebase_target(other->save, &saved);
        other->saved_lost = true; other->source_stale = false; other->watch_pending = false;
        other->operation_kind = 0; other->operation_error = 0;
        for (size_t j = 0; j < tabs_count(&e->tabs); j++)
            if (tabs_at(&e->tabs, j)->id == other->id) (void)tabs_set_modified(&e->tabs, j, true);
        editor_session_publish(e, other);
    }
    editor_session_publish(e, b);
    /* A reload replaces the peer's tab in place: tab indices are stable. */
    for (size_t i = 0; !rc && i < peers && i < tabs_count(&e->tabs); i++) {
        if (!reload[i]) continue;
        editor_buffer *other = (editor_buffer *)((uint8_t *)tabs_at(&e->tabs, i)->undo - offsetof(editor_buffer, undo));
        rc = editor_reload_source(e, other, i);
        if (!rc && !other->retired) rc = EDITOR_ERR_IO;
    }
    free(reload);
    if (rc) return EDITOR_ERR_IO;
    /* edit-ze59: the replacing checkpoint is written by a worker
     * (editor_publish_start, from editor_retain_tick); the retained copy of
     * the former BASE stays until it lands. */
    if (e->journal) e->publish_owed = true;
    return b == e->buffer && !e->save_flushing ? editor_full_layout(e) : editor_begin_frame(e);
}
/* Maintenance: adopt a finished Save As write. Between frames, like the file
 * command it continues (flush adopts without the frame gates). */
void editor_save_as_tick(editor *e)
{
    editor_save_as *a = e->save_as; if (!a) return;
    savectl_tick(a->writer);
    savectl_model model = savectl_get_model(a->writer);
    if (model.busy) return;
    if (!e->save_flushing && (e->dirty || e->backend->active || e->op_count || editor_activation_busy(e))) return;
    editor_buffer *b = NULL; size_t tab = SIZE_MAX;
    for (size_t i = 0; i < e->buffer_capacity; i++)
        if (e->buffers[i] && !e->buffers[i]->retired && e->buffers[i]->id == a->buffer_id) b = e->buffers[i];
    for (size_t i = 0; b && i < tabs_count(&e->tabs); i++) if (tabs_at(&e->tabs, i)->id == b->id) tab = i;
    if (b && b->save) { savectl_tick(b->save); if (savectl_get_model(b->save).busy) return; }
    file_id saved = *savectl_target_identity(a->writer);
    savectl_close_begin(a->writer);
    if (savectl_destroy(a->writer)) return; /* job still retiring: next tick */
    a->writer = NULL; e->save_as = NULL;
    /* A closed or retired buffer keeps its former binding; the file is written. */
    int rc = !b || tab == SIZE_MAX ? EDITOR_ERR_ARG : model.state != SAVECTL_SAVED ? EDITOR_ERR_IO :
        a->want_base && a->base_rc ? EDITOR_ERR_IO : save_as_adopt(e, b, tab, a, saved);
    if (b) b->save_tracking = false;
    save_as_free(a);
    if (!rc) return;
    e->recovery_notice = "save as failed"; e->recovery_notice_mutations = e->stats.mutations;
    (void)editor_begin_frame(e);
}
/* A finished write awaits adoption: keep the loop stepping (the worker's
 * terminal message can precede its slot's physical retirement). */
bool editor_save_as_runnable(const editor *e)
{ return e->save_as && !savectl_get_model(e->save_as->writer).busy; }
void editor_save_as_close(editor *e)
{
    editor_save_as *a = e->save_as; if (!a) return;
    savectl_close_begin(a->writer);
    for (;;) {
        savectl_tick(a->writer);
        if (savectl_destroy(a->writer) == SAVECTL_OK) break;
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        sched_yield();
    }
    e->save_as = NULL; save_as_free(a);
}
int editor_file_command_service(editor *e)
{
    if (e->file_command == KEYS_ACTION_NONE || e->dirty || e->backend->active ||
        editor_activation_busy(e) || editor_save_active(e) || e->op_count || e->journal_waiting ||
        e->publish_owed) return 0; /* edit-ze59: one Save As and its checkpoint at a time */
    keys_action command = e->file_command; e->file_command = KEYS_ACTION_NONE;
    if (e->file_command_owner != e->buffer->id || e->file_command_generation != e->buffer->source_generation) return 0;
    e->recovery_notice = NULL;
    int rc = 0; uint64_t id;
    if (command == KEYS_ACTION_NEW_FILE) rc = editor_add_buffer(e, NULL, NULL, 0, &id);
    else if (command == KEYS_ACTION_OPEN_FILE) {
        /* The shared buffer API permits absent paths; this command promises
         * an existing readable regular file. Validate before publishing a tab. */
        int fd = open(e->file_command_path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        struct stat st;
        if (fd < 0) rc = EDITOR_ERR_IO;
        else { if (fstat(fd, &st) || !S_ISREG(st.st_mode)) rc = EDITOR_ERR_IO; close(fd); }
        if (!rc) rc = editor_add_buffer(e, e->file_command_path, NULL, 0, &id);
    } else if (command == KEYS_ACTION_SAVE_AS) rc = save_as(e, e->file_command_path);
    if (!rc) return 0;
    if (!e->recovery_notice) e->recovery_notice = command == KEYS_ACTION_OPEN_FILE ? "open failed" :
        command == KEYS_ACTION_SAVE_AS ? "save as failed" : "new file failed";
    e->recovery_notice_mutations = e->stats.mutations;
    return editor_full_layout(e);
}
