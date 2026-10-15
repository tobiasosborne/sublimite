/* File setup is maintenance, after the input frame retires. Prompt edits use
 * editor-owned storage; buffer creation and filesystem work never run there. */
#include "editor/private.h"
#include <errno.h>
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
static int save_as(editor *e, const char *input)
{
    editor_buffer *b = e->buffer;
    if (!tabs_count(&e->tabs) || b->source_stale || b->pending_open) return EDITOR_ERR_ARG;
    char path[IPC_PATH_CAP]; int rc = canonical_target(input, path); if (rc) return rc;
    if (!strcmp(path, b->save_path)) return editor_save_request(e, 0);
    if (b->save) {
        savectl_model model = savectl_get_model(b->save);
        if (model.busy || model.can_recover_cancel || model.needs_finish) return EDITOR_ERR_ARG;
    }
    file_id baseline;
    rc = file_id_stat_path(path, &baseline); if (rc) return EDITOR_ERR_IO;
    /* Preserve the original mapping's guard independently of the new target
     * identity. COPY trees own their bytes and need no old-path guard. */
    file_source *source = NULL;
    if (b->file && file_open_mode(b->file) == FILE_MODE_MMAP && file_source_acquire(b->file, &source)) return EDITOR_ERR_IO;
    savectl_options options = {.pool = &e->pool, .path = path, .baseline = baseline,
        .source_mode = source ? FILE_MODE_MMAP : FILE_MODE_COPY,
        .validate_source = source ? file_source_validate : NULL, .source_ctx = source,
        .journal = e->journal, .journal_pool = &e->journal_pool, .buffer_id = b->id};
    savectl *replacement = NULL;
    rc = savectl_create(&replacement, &options, false);
    if (rc) { file_source_release(source); return EDITOR_ERR_IO; }
    piece_snapshot *snapshot = piece_snapshot_take(b->tree);
    file_id saved;
    file_save_args args = {.path = path, .snap = snapshot, .expect = &baseline, .out_id = &saved,
        .validate = source ? file_source_validate : NULL, .validate_ctx = source};
    rc = snapshot ? file_save_write(&args) : FILE_ERR_NOMEM;
    if (snapshot) piece_snapshot_release(snapshot);
    journal_base base = {0}; base.path = "";
    if (!rc && e->journal) rc = journal_capture_base(path, &base);
    if (rc) { savectl_close_begin(replacement); (void)savectl_destroy(replacement); file_source_release(source); return EDITOR_ERR_IO; }
    (void)savectl_rebase_target(replacement, &saved);
    if (b->save) {
        savectl_close_begin(b->save);
        rc = savectl_destroy(b->save);
        if (rc) { savectl_close_begin(replacement); (void)savectl_destroy(replacement); file_source_release(source); return EDITOR_ERR_IO; }
    }
    b->save = replacement;
    file_source_release(b->target_source); b->target_source = source;
    file_watch_path_stop(&b->target_watch);
    strcpy(b->path, path); strcpy(b->save_path, path); b->target_rebound = true;
    b->base = base; b->base.path = *base.path ? b->base.captured_path : "";
    /* edit-28u: the captured BASE is the file this revision's snapshot was
     * written to only if it is the very generation the write reported. */
    if (*b->base.path && saved.exists && saved.dev == b->base.device && saved.ino == b->base.inode &&
        saved.size == b->base.size && saved.mtime_ns == b->base.mtime_ns &&
        saved.size == piece_len(b->tree) && (!saved.metadata_valid || saved.ctime_ns == b->base.ctime_ns)) {
        b->saved_text = editor_base_id_of(&b->base); b->saved_text_revision = b->revision;
    }
    const char *title = strrchr(path, '/'); title = title ? title + 1 : path;
    /* (P) retain the tab module's grapheme-safe title capacity. */
    size_t end = 0, length = strlen(title);
    while (end < length) {
        size_t n = utf8_grapheme_next((const uint8_t *)title + end, length - end);
        if (n >= TABS_TITLE_BYTES - end) break;
        end += n;
    }
    rc = tabs_rename(&e->tabs, tabs_active_index(&e->tabs), title, end, path, strlen(path));
    if (rc) return EDITOR_ERR_IO;
    b->saved_cursor = b->history_cursor; b->saved_lost = false;
    b->save_error = b->operation_error = 0; b->save_errno = 0;
    (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), false);
    /* Discard our own atomic-replacement notice before registering the target.
     * Old file handles stay alive because piece/undo snapshots can borrow them. */
    file_watch_dispatch();
    int fd = file_watch_path_start(&b->target_watch, b->path);
    if (fd < 0) return EDITOR_ERR_IO;
    struct epoll_event event = {.events = EPOLLIN, .data.fd = fd};
    if (epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &event) && errno != EEXIST) return EDITOR_ERR_IO;
    b->watch_fd = fd; b->watch_pending = false;
    /* Same-target peers retain their text, as in the normal save completion
     * path, but their recovery BASE and target identity must follow the inode
     * replacement before a complete checkpoint can validate the session. */
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
    if (e->journal) {
        rc = journal_flush(e->journal);
        if (!rc) rc = editor_checkpoint(e);
        if (rc) return EDITOR_ERR_IO;
    }
    return editor_full_layout(e);
}
int editor_file_command_service(editor *e)
{
    if (e->file_command == KEYS_ACTION_NONE || e->dirty || e->backend->active ||
        editor_activation_busy(e) || editor_save_active(e) || e->op_count || e->journal_waiting) return 0;
    keys_action command = e->file_command; e->file_command = KEYS_ACTION_NONE;
    if (e->file_command_owner != e->buffer->id || e->file_command_generation != e->buffer->source_generation) return 0;
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
    if (!rc) { e->recovery_notice = NULL; return 0; }
    e->recovery_notice = command == KEYS_ACTION_OPEN_FILE ? "open failed" :
        command == KEYS_ACTION_SAVE_AS ? "save as failed" : "new file failed";
    e->recovery_notice_mutations = e->stats.mutations;
    return editor_full_layout(e);
}
