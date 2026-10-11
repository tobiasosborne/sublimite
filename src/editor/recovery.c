/* Startup only: validate and replay before any editor frame is submitted. Keep
 * the prior artifact until a complete recovered checkpoint is durable. */
#include "editor/private.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct recovered_buffer {
    struct recovered_buffer *next;
    uint64_t id;
    uint8_t *bytes;
    size_t len;
    char target[IPC_PATH_CAP];
    journal_view view;
    editor_buffer *installed;
    bool save_pending;
    char retained[IPC_PATH_CAP];
} recovered_buffer;
typedef struct recovered_session {
    recovered_buffer *buffers;
    uint64_t *tabs;
    size_t count, active;
    bool tabs_valid;
    uint32_t width, height;
} recovered_session;
static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) value |= (uint64_t)p[i] << (8u * i);
    return value;
}
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)get64((const uint8_t[8]){p[0], p[1], p[2], p[3], 0, 0, 0, 0}); }
static recovered_buffer *find(recovered_session *s, uint64_t id)
{
    for (recovered_buffer *b = s->buffers; b; b = b->next) if (b->id == id) return b;
    return NULL;
}
static void fini(recovered_session *s)
{
    while (s->buffers) {
        recovered_buffer *b = s->buffers; s->buffers = b->next;
        free(b->bytes); free(b);
    }
    free(s->tabs); *s = (recovered_session){0};
}
static int restore_record(void *ctx, const journal_record *r)
{
    recovered_session *s = ctx;
    const uint8_t *p = r->data;
    if (r->type == JOURNAL_TABS) {
        size_t count = (size_t)get64(p);
        uint64_t *tabs = count ? calloc(count, sizeof *tabs) : NULL;
        if (count && !tabs) return JOURNAL_NOMEM;
        for (size_t i = 0; i < count; i++) tabs[i] = get64(p + 16 + i * 8);
        free(s->tabs); s->tabs = tabs; s->count = count; s->active = (size_t)get64(p + 8); s->tabs_valid = true; return 0;
    }
    if (r->type == JOURNAL_WINDOW) { s->width = get32(p); s->height = get32(p + 4); return 0; }
    if (r->type == JOURNAL_SAVE) {
        recovered_buffer *owner = find(s, r->buffer_id);
        if (owner) owner->save_pending = get32(p + 8) == 1; /* journal's PREPARED wire phase */
        return 0;
    }
    recovered_buffer *b = find(s, r->buffer_id);
    if (r->type == JOURNAL_BASE) {
        journal_base base; char path[IPC_PATH_CAP];
        if (journal_decode_base(r, &base, path, sizeof path) || base.size > SIZE_MAX) return JOURNAL_INVALID;
        if (!b) {
            b = calloc(1, sizeof *b); if (!b) return JOURNAL_NOMEM;
            b->id = r->buffer_id; b->next = s->buffers; s->buffers = b;
        }
        free(b->bytes); b->len = (size_t)base.size; b->bytes = b->len ? malloc(b->len) : NULL;
        if (b->len && !b->bytes) return JOURNAL_NOMEM;
        strcpy(b->target, path);
        const char *leaf = strrchr(path, '/');
        if (leaf && !strncmp(leaf + 1, ".sublimite-base-", strlen(".sublimite-base-"))) strcpy(b->retained, path);
        if (*path) {
            int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK); if (fd < 0) return JOURNAL_IO;
            size_t done = 0;
            while (done < b->len) {
                ssize_t n = read(fd, b->bytes + done, b->len - done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { close(fd); return JOURNAL_IO; }
                done += (size_t)n;
            }
            close(fd);
        }
        return 0;
    }
    if (!b) return JOURNAL_INVALID;
    if (r->type == JOURNAL_TARGET) { strcpy(b->target, (const char *)p); return 0; }
    if (r->type == JOURNAL_VIEW) {
        b->view = (journal_view){get64(p), get64(p + 8), get64(p + 16), get64(p + 24)}; return 0;
    }
    uint64_t off = get64(p);
    if (off > b->len) return JOURNAL_INVALID;
    if (r->type == JOURNAL_INSERT) {
        size_t n = r->size - 8;
        if (n > SIZE_MAX - b->len) return JOURNAL_NOMEM;
        uint8_t *bytes = realloc(b->bytes, b->len + n); if (!bytes) return JOURNAL_NOMEM;
        b->bytes = bytes; memmove(bytes + (size_t)off + n, bytes + (size_t)off, b->len - (size_t)off);
        memcpy(bytes + (size_t)off, p + 8, n); b->len += n; return 0;
    }
    if (r->type == JOURNAL_DELETE) {
        uint64_t n = get64(p + 8); if (n > b->len - off) return JOURNAL_INVALID;
        memmove(b->bytes + (size_t)off, b->bytes + (size_t)(off + n), b->len - (size_t)(off + n));
        b->len -= (size_t)n; return 0;
    }
    return JOURNAL_INVALID;
}
static bool same_bytes(editor_buffer *b, const recovered_buffer *r)
{
    if (!*r->target || piece_len(b->tree) != r->len) return false;
    uint8_t bytes[JOURNAL_INSERT_SLICE_BYTES];
    for (size_t off = 0; off < r->len;) {
        size_t n = r->len - off < sizeof bytes ? r->len - off : sizeof bytes;
        if (piece_read(b->tree, off, bytes, n) || memcmp(bytes, r->bytes + off, n)) return false;
        off += n;
    }
    return true;
}
static int install_buffer(editor *e, recovered_buffer *r)
{
    uint64_t id;
    int rc = editor_add_buffer(e, *r->target ? r->target : NULL, NULL, 0, &id);
    if (rc) return rc;
    editor_buffer *b = e->buffer;
    /* Recovery uses private COPY acquisition. Its remainder must be installed
     * without running editor_step, whose submit would expose unreplayed bytes. */
    while (b->pending_open && !file_open_ready(b->file)) {
        struct pollfd ready = {work_pool_eventfd(&e->pool), POLLIN, 0};
        if (poll(&ready, 1, -1) < 0 && errno != EINTR) return EDITOR_ERR_IO;
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        if (b->operation_error) return EDITOR_ERR_IO;
    }
    if (b->pending_open) {
        e->dirty = false;
        rc = editor_adopt_open(e, b); if (rc) return rc;
    }
    bool clean = !r->save_pending && same_bytes(b, r);
    undo_clear(&b->undo);
    rc = piece_delete(b->tree, 0, piece_len(b->tree), NULL);
    if (!rc) rc = piece_insert(b->tree, 0, r->bytes, r->len);
    if (rc) return EDITOR_ERR_MEMORY;
    b->revision++; b->saved_lost = !clean; b->lines = piece_line_count(b->tree);
    b->history_count = b->history_cursor = b->history_head = b->saved_cursor = 0;
    lineidx_destroy(b->index); b->index = lineidx_create(r->len);
    if (!b->index || lineidx_build_reserve(b->index)) return EDITOR_ERR_MEMORY;
    piece_snapshot *snapshot = piece_snapshot_take(b->tree); if (!snapshot) return EDITOR_ERR_MEMORY;
    rc = lineidx_bind_snapshot(b->index, snapshot); piece_snapshot_release(snapshot);
    if (rc) return EDITOR_ERR_MEMORY;
    b->index_dirty = true;
    view_state state = b->initial;
    state.selection.cursor = r->view.cursor > r->len ? r->len : r->view.cursor;
    state.selection.anchor = r->view.anchor > r->len ? r->len : r->view.anchor;
    state.first_byte = r->view.scroll_byte > r->len ? r->len : r->view.scroll_byte;
    state.first_line = piece_byte_to_line(b->tree, state.first_byte);
    state.visual_byte = state.first_byte; state.hscroll = r->view.scroll_x;
    b->initial = state; e->v.state = state;
    e->tabs.slots[e->tabs.active].state = state;
    (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), !clean);
    r->installed = b; return editor_activate(e);
}
static int install_session(editor *e, recovered_session *s)
{
    uint64_t threshold = e->cfg.copy_threshold;
    e->cfg.copy_threshold = UINT64_MAX; e->recovering = true;
    journal *log = e->journal; e->journal = NULL;
    int rc = 0;
    /* Live order is authoritative. Keep other journalled buffers as retained
     * closed tabs so unrelated recovery content survives the new checkpoint. */
    for (size_t i = 0; !rc && i < s->count; i++) {
        recovered_buffer *b = find(s, s->tabs[i]);
        if (!b) { rc = EDITOR_ERR_IO; break; }
        rc = install_buffer(e, b);
    }
    for (recovered_buffer *b = s->buffers; !rc && b; b = b->next) if (!b->installed) {
        rc = install_buffer(e, b);
        if (!rc && s->tabs_valid) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
    }
    if (!rc && s->count) rc = editor_select_tab(e, s->active);
    if (!rc && s->width && s->height) {
        e->resize_w = s->width; e->resize_h = s->height; e->resize_pending = true;
    }
    e->cfg.copy_threshold = threshold; e->journal = log; e->recovering = false;
    /* BASE was acquired while journal temporarily disabled. Derive it from the
     * opened generation off-path before publishing the recovered checkpoint. */
    for (size_t i = 0; !rc && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i]; if (!b) continue;
        if (b->file) rc = journal_capture_base(file_path(b->file), &b->base);
        if (!rc) rc = editor_save_setup(e, b);
    }
    if (!rc) rc = editor_checkpoint(e);
    if (!rc) editor_session_metadata(e);
    return rc;
}
int editor_session_restore(editor *e)
{
    const char *path = e->cfg.journal_path;
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    if (!e->recover_existing && strncmp(name, "session-", strlen("session-"))) return 0;
    char lock_path[IPC_PATH_CAP];
    int lock_size = snprintf(lock_path, sizeof lock_path, "%s.lock", path);
    if (lock_size < 0 || (size_t)lock_size >= sizeof lock_path) return EDITOR_ERR_ARG;
    e->session_lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (e->session_lock < 0 || flock(e->session_lock, LOCK_EX | LOCK_NB)) return EDITOR_ERR_IO;
    char directory[IPC_PATH_CAP]; strcpy(directory, path);
    char *slash = strrchr(directory, '/');
    if (slash == directory) slash[1] = 0; else if (slash) *slash = 0; else strcpy(directory, ".");
    char previous[IPC_PATH_CAP] = ""; int previous_fd = -1;
    if (e->recover_existing) {
        strcpy(previous, path); previous_fd = dup(e->session_lock);
        if (previous_fd < 0) return EDITOR_ERR_IO;
    }
    else {
        struct timespec newest = {0};
        DIR *dir = opendir(directory); if (!dir) return EDITOR_ERR_IO;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (strncmp(entry->d_name, "session-", strlen("session-"))) continue;
            /* mkstemp names have no suffix. Ignore retained SAVE originals and
             * checkpoint temporaries, which share the prefix but contain a dot. */
            if (strchr(entry->d_name, '.')) continue;
            char candidate[IPC_PATH_CAP];
            int n = snprintf(candidate, sizeof candidate, "%s/%s", directory, entry->d_name);
            if (n < 0 || (size_t)n >= sizeof candidate || !strcmp(candidate, path)) continue;
            int fd = open(candidate, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            if (fd < 0) continue;
            struct stat st;
            if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !st.st_size) { close(fd); continue; }
            int lock_n = snprintf(lock_path, sizeof lock_path, "%s.lock", candidate);
            if (lock_n < 0 || (size_t)lock_n >= sizeof lock_path) { close(fd); continue; }
            int lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
            close(fd); fd = lock;
            if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB)) { if (fd >= 0) close(fd); continue; }
            if (previous_fd < 0 || st.st_mtim.tv_sec > newest.tv_sec ||
                (st.st_mtim.tv_sec == newest.tv_sec && st.st_mtim.tv_nsec > newest.tv_nsec)) {
                if (previous_fd >= 0) close(previous_fd);
                previous_fd = fd; newest = st.st_mtim; strcpy(previous, candidate);
            } else close(fd);
        }
        closedir(dir);
    }
    if (previous_fd < 0) return 0;
    recovered_session session = {0}; journal_replay_result result;
    int rc = journal_replay_file(previous, restore_record, &session, &result);
    if (!rc && e->recover_existing && !session.buffers) rc = EDITOR_ERR_IO;
    if (!rc && session.buffers) { rc = install_session(e, &session); if (!rc) e->startup_restored = true; }
    /* Failure never retires the recovery source. Corrupt/torn tails are replay's
     * validated prefix, so the durable replacement checkpoint contains exactly
     * the accepted session. */
    if (!rc && session.buffers) {
        if (!e->recover_existing && unlink(previous)) rc = EDITOR_ERR_IO;
        else if (!e->recover_existing) {
            int n = snprintf(lock_path, sizeof lock_path, "%s.lock", previous);
            if (n > 0 && (size_t)n < sizeof lock_path) (void)unlink(lock_path);
        }
        int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0 || fsync(fd)) rc = EDITOR_ERR_IO;
        if (fd >= 0) close(fd);
        if (!rc) {
            /* The durable CURRENT checkpoint no longer references private
             * generations retained by an interrupted save. Retire only journal
             * retention names beside this session's canonical directory. */
            char canonical[IPC_PATH_CAP];
            if (realpath(directory, canonical)) for (recovered_buffer *b = session.buffers; b; b = b->next) {
                size_t length = strlen(canonical);
                if (*b->retained && !strncmp(b->retained, canonical, length) && b->retained[length] == '/' &&
                    !strchr(b->retained + length + 1, '/')) (void)unlink(b->retained);
            }
        }
    }
    if (rc) e->recovering = true; /* failed startup close must not mask the prior artifact */
    fini(&session); close(previous_fd);
    return rc ? EDITOR_ERR_IO : 0;
}
