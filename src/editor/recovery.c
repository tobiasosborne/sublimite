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
#include <time.h>
#include <unistd.h>

typedef struct recovered_buffer {
    struct recovered_buffer *next;
    uint64_t id;
    uint8_t *bytes;
    size_t len;
    char target[IPC_PATH_CAP];
    journal_view_state view;
    bool full_view;
    editor_buffer *installed;
    bool save_pending;
    bool conflict; /* edit-btj: BASE no longer matches disk; edits are not replayed */
    char retained[IPC_PATH_CAP];
} recovered_buffer;
typedef struct recovered_session {
    recovered_buffer *buffers;
    uint64_t *tabs;
    size_t count, active;
    bool tabs_valid;
    uint32_t width, height;
    bool window_chosen; /* WINDOW v2 with JOURNAL_WINDOW_CHOSEN */
    bool tolerant;      /* edit-btj second pass: a changed BASE marks its buffer */
    bool conflicts;     /* some buffer was marked */
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
    if (r->type == JOURNAL_WINDOW) {
        journal_window w; int rc = journal_decode_window(r, &w); if (rc) return rc;
        s->width = w.width; s->height = w.height; s->window_chosen = (w.flags & JOURNAL_WINDOW_CHOSEN) != 0;
        return 0;
    }
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
        b->conflict = false;
        if (s->tolerant && *path) {
            /* edit-btj: the first pass stopped at a changed BASE. Its edits
             * cannot be replayed onto other bytes: open the disk text and keep
             * the edits in the set-aside source journal. */
            int check = journal_check_base(&base);
            if (check && check != JOURNAL_BASE_CHANGED) return check;
            if (check) {
                free(b->bytes); b->bytes = NULL; b->len = 0;
                b->conflict = s->conflicts = true; strcpy(b->target, path);
                return 0;
            }
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
        b->full_view = r->size == JOURNAL_VIEW_STATE_BYTES;
        return journal_decode_view_state(r, &b->view);
    }
    if (b->conflict && (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE)) return 0;
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
    int rc = editor_add_recovered_buffer(e, *r->target ? r->target : NULL, &id);
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
    if (r->conflict) { /* edit-btj: disk text, clean, view from the start */
        (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), false);
        r->installed = b; return editor_activate(e);
    }
    bool clean = !r->save_pending && same_bytes(b, r);
    undo_clear(&b->undo);
    rc = piece_delete(b->tree, 0, piece_len(b->tree), NULL);
    if (!rc) rc = piece_insert(b->tree, 0, r->bytes, r->len);
    if (rc) return EDITOR_ERR_MEMORY;
    b->revision++; b->saved_lost = !clean; b->lines = piece_line_count(b->tree);
    b->history_count = b->history_cursor = b->history_head = b->saved_cursor = 0;
    b->soft_count = b->soft_cursor = b->soft_head = 0;
    b->soft_base_redo = 0;
    b->mark_valid = false;
    b->last_key_valid = false;
    lineidx_destroy(b->index); b->index = lineidx_create(r->len);
    if (!b->index || lineidx_build_reserve(b->index)) return EDITOR_ERR_MEMORY;
    piece_snapshot *snapshot = piece_snapshot_take(b->tree); if (!snapshot) return EDITOR_ERR_MEMORY;
    rc = lineidx_bind_snapshot(b->index, snapshot); piece_snapshot_release(snapshot);
    if (rc) return EDITOR_ERR_MEMORY;
    b->index_dirty = true;
    view_state state = b->initial;
    state.selection.cursor = r->view.position.cursor > r->len ? r->len : r->view.position.cursor;
    state.selection.anchor = r->view.position.anchor > r->len ? r->len : r->view.position.anchor;
    state.first_byte = r->view.position.scroll_byte > r->len ? r->len : r->view.position.scroll_byte;
    state.first_line = piece_byte_to_line(b->tree, state.first_byte);
    state.visual_byte = state.first_byte; state.hscroll = r->view.position.scroll_x;
    if (r->full_view) {
        state.selection.preferred_col = r->view.preferred_col;
        state.first_line = r->view.first_line;
        state.visual_byte = r->view.visual_byte; /* unused unwrapped value may outlive a deletion */
        state.wrap = (r->view.flags & JOURNAL_VIEW_WRAP) != 0;
        state.approximate = (r->view.flags & JOURNAL_VIEW_APPROXIMATE) != 0;
        state.visual_end = (r->view.flags & JOURNAL_VIEW_VISUAL_END) != 0;
    }
    b->initial = state; e->v.state = state;
    e->tabs.slots[e->tabs.active].state = state;
    (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), !clean);
    r->installed = b; return editor_activate(e);
}
static int install_session(editor *e, recovered_session *s)
{
    /* edit-rzj: appended sessions keep the newest session's active tab and
     * geometry. Source-local IDs are mapped by install_buffer to fresh IDs. */
    size_t live = tabs_count(&e->tabs), active = tabs_active_index(&e->tabs);
    uint64_t threshold = e->cfg.copy_threshold;
    e->cfg.copy_threshold = UINT64_MAX; e->recovering = true;
    journal *log = e->journal; e->journal = NULL;
    /* Policy (edit-a6v + edit-457.8): explicit launch size > journalled chosen
     * size (WINDOW v2) > the DPI default already in font.window_*. Legacy
     * 8-byte records carry older builds' grid pixels, not a chosen size.
     * The native window is created after this from font.window_*. */
    bool launch = !e->cfg.window_size_default;
    uint32_t width = launch && e->cfg.window_width ? e->cfg.window_width :
        s->window_chosen ? s->width : e->font.window_width;
    uint32_t height = launch && e->cfg.window_height ? e->cfg.window_height :
        s->window_chosen ? s->height : e->font.window_height;
    int rc = !e->startup_restored && s->width && s->height ? editor_resize_now(e, width, height) : 0;
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
    if (!rc && live) rc = editor_select_tab(e, active);
    else if (!rc && s->count) rc = editor_select_tab(e, s->active);
    e->cfg.copy_threshold = threshold; e->journal = log; e->recovering = false;
    /* BASE was acquired while journal temporarily disabled. Derive it from the
     * opened generation off-path before publishing the recovered checkpoint. */
    for (recovered_buffer *r = s->buffers; !rc && r; r = r->next) {
        editor_buffer *b = r->installed; if (!b) continue;
        if (b->file) rc = journal_capture_base(file_path(b->file), &b->base);
        if (!rc) {
            int save_rc = editor_save_setup(e, b);
            if (save_rc) { b->save_error = b->operation_error = save_rc; b->save_errno = errno; }
        }
    }
    if (!rc) rc = editor_checkpoint(e);
    if (!rc) editor_session_metadata(e);
    return rc;
}
/* A lease is infrastructure, not a replayable session artifact. Keep its
 * stable inode across checkpoint rotations and clean closes. */
static int lock_name(char *out, size_t cap, const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t parent = slash ? (size_t)(slash - path) + 1 : 0;
    int n = snprintf(out, cap, "%.*s.%s.lock", (int)parent, path, path + parent);
    return n < 0 || (size_t)n >= cap ? EDITOR_ERR_ARG : 0;
}
/* edit-btj: second, tolerant pass over the unchanged source bytes. The
 * streaming replay stops at the first changed BASE without truncating. */
static int replay_tolerant(const char *path, recovered_session *s)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); if (fd < 0) return JOURNAL_IO;
    struct stat st; uint8_t *bytes = NULL; size_t size = 0, done = 0; int rc = 0;
    if (fstat(fd, &st) || st.st_size < 0 || (uint64_t)st.st_size > SIZE_MAX) rc = JOURNAL_IO;
    else { size = (size_t)st.st_size; bytes = size ? malloc(size) : NULL; if (size && !bytes) rc = JOURNAL_NOMEM; }
    while (!rc && done < size) {
        ssize_t n = read(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { rc = JOURNAL_IO; break; }
        done += (size_t)n;
    }
    close(fd);
    journal_replay_result result;
    s->tolerant = true;
    if (!rc) rc = journal_replay_bytes(bytes, size, restore_record, s, &result);
    free(bytes); return rc;
}
/* edit-btj: never delete a recovery source. Rename it beside itself under a
 * dotted name, which session discovery skips; never replace an existing name. */
int editor_set_aside(const char *path, char *out, size_t cap)
{
    struct timespec now; if (clock_gettime(CLOCK_REALTIME, &now)) return EDITOR_ERR_IO;
    int n = snprintf(out, cap, "%s.set-aside-%lld%09ld", path, (long long)now.tv_sec, now.tv_nsec);
    if (n < 0 || (size_t)n >= cap) { *out = 0; return EDITOR_ERR_ARG; }
    if (renameat2(AT_FDCWD, path, AT_FDCWD, out, RENAME_NOREPLACE)) { *out = 0; return EDITOR_ERR_IO; }
    return 0;
}
/* edit-rzj: capacity comes from the configured live/closed tab pools, not a
 * recovery budget. (P) Structural zero/one denote empty/single; a closed
 * buffer needs one temporary live slot before editor_close_tab retains it. */
static bool session_fits(const editor *e, const recovered_session *s)
{
    size_t buffers = 0, used = 0;
    for (const recovered_buffer *b = s->buffers; b; b = b->next) buffers++;
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i]) used++;
    size_t live = s->tabs_valid ? s->count : buffers;
    if (live > buffers) return false;
    size_t closed = buffers - live;
    size_t available = e->tabs.capacity - tabs_count(&e->tabs);
    return buffers <= e->buffer_capacity - used && live <= available &&
        (!closed || live < available) &&
        closed <= e->tabs.closed_capacity - tabs_closed_count(&e->tabs);
}
static int restore_source(editor *e, const char *directory, const char *previous)
{
    char lock_path[IPC_PATH_CAP];
    recovered_session session = {0}; journal_replay_result result;
    int rc = journal_replay_file(previous, restore_record, &session, &result);
    /* edit-btj: a discovered journal never blocks launch. A changed BASE gets
     * a tolerant pass; any other failure sets the source aside and opens empty.
     * An explicitly named existing journal keeps the strict contract. */
    bool aside = false;
    if (rc && !e->recover_existing) {
        fini(&session);
        rc = replay_tolerant(previous, &session);
        if (rc) { fini(&session); aside = true; rc = 0; }
    }
    if (!rc && e->recover_existing && !session.buffers) rc = EDITOR_ERR_IO;
    if (!rc && session.buffers) {
        if (!e->recover_existing && !session_fits(e, &session)) {
            /* Do not partially install or evict a dirty closed tab. Every
             * skipped source stays discoverable on subsequent launches. */
            e->recovery_notice = "additional session recovery deferred; journal retained (tab capacity)";
            e->recovery_notice_mutations = e->stats.mutations;
            fini(&session); return 0;
        }
        rc = install_session(e, &session);
        if (!rc) e->startup_restored = true;
        else if (!e->recover_existing) {
            /* Partly installed state cannot be unwound here: set the source
             * aside and let editor_open start again without discovery. */
            (void)editor_set_aside(previous, e->recovery_aside, sizeof e->recovery_aside);
            e->recovery_retry = true; e->recovering = true;
            fini(&session); return EDITOR_ERR_IO;
        }
    }
    if (!rc && (aside || session.conflicts)) {
        e->recovery_notice = session.conflicts && !aside ?
            "file changed on disk since this session; its unsaved edits were set aside" :
            "session recovery failed; its journal was set aside";
        e->recovery_notice_mutations = e->stats.mutations;
        (void)editor_set_aside(previous, e->recovery_aside, sizeof e->recovery_aside);
        if (!lock_name(lock_path, sizeof lock_path, previous)) (void)unlink(lock_path);
        int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) { (void)fsync(fd); close(fd); }
        fini(&session); return 0;
    }
    /* Failure never retires the recovery source. Corrupt/torn tails are replay's
     * validated prefix, so the durable replacement checkpoint contains exactly
     * the accepted session. */
    if (!rc && session.buffers) {
        if (!e->recover_existing && unlink(previous)) rc = EDITOR_ERR_IO;
        else if (!e->recover_existing) {
            if (!lock_name(lock_path, sizeof lock_path, previous)) (void)unlink(lock_path);
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
    fini(&session);
    return rc ? EDITOR_ERR_IO : 0;
}

/* Descending mtime with a pathname tie breaker gives a stable cursor even
 * when a source is empty or retained because capacity is exhausted.
 * (P) Zero is empty/equality/success; negative descriptors mean no lease.
 * POSIX 0600 grants only the owner read/write on the existing lease file. */
static bool newer_source(struct timespec a, const char *a_path, struct timespec b, const char *b_path)
{
    return a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec &&
        (a.tv_nsec > b.tv_nsec || (a.tv_nsec == b.tv_nsec && strcmp(a_path, b_path) > 0)));
}
static int discover_source(editor *e, const char *directory, const char *before,
    struct timespec cutoff, char *previous, struct timespec *newest, int *previous_fd)
{
    DIR *dir = opendir(directory); if (!dir) return EDITOR_ERR_IO;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "session-", strlen("session-"))) continue;
        /* mkstemp names have no suffix. Ignore retained SAVE originals and
         * checkpoint temporaries, which share the prefix but contain a dot. */
        if (strchr(entry->d_name, '.')) continue;
        char candidate[IPC_PATH_CAP], lock_path[IPC_PATH_CAP];
        int n = snprintf(candidate, sizeof candidate, "%s/%s", directory, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof candidate || !strcmp(candidate, e->cfg.journal_path)) continue;
        int fd = open(candidate, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) continue;
        struct stat st;
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !st.st_size) { close(fd); continue; }
        if (*before && !newer_source(cutoff, before, st.st_mtim, candidate)) { close(fd); continue; }
        if (lock_name(lock_path, sizeof lock_path, candidate)) { close(fd); continue; }
        int lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        close(fd); fd = lock;
        if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB)) { if (fd >= 0) close(fd); continue; }
        if (*previous_fd < 0 || newer_source(st.st_mtim, candidate, *newest, previous)) {
            if (*previous_fd >= 0) close(*previous_fd);
            *previous_fd = fd; *newest = st.st_mtim; strcpy(previous, candidate);
        } else close(fd);
    }
    closedir(dir); return 0;
}
int editor_session_restore(editor *e)
{
    const char *path = e->cfg.journal_path;
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    if (!e->recover_existing && strncmp(name, "session-", strlen("session-"))) return 0;
    char lock_path[IPC_PATH_CAP];
    if (lock_name(lock_path, sizeof lock_path, path)) return EDITOR_ERR_ARG;
    if (e->cfg.session_lease) {
        /* edit-cxo: the creator took this lease before the journal name was
         * visible (journal_session_create). Share its open file description:
         * a second flock on a new description would conflict with it. Refuse
         * a descriptor that does not guard this journal's lock name. */
        struct stat held, named;
        e->session_lock = fcntl(*e->cfg.session_lease, F_DUPFD_CLOEXEC, 0);
        if (e->session_lock < 0 || fstat(e->session_lock, &held) || stat(lock_path, &named) ||
            held.st_dev != named.st_dev || held.st_ino != named.st_ino) return EDITOR_ERR_IO;
    } else {
        e->session_lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (e->session_lock < 0 || flock(e->session_lock, LOCK_EX | LOCK_NB)) return EDITOR_ERR_IO;
    }
    e->session_lock_owned = true;
    if (e->recovery_skip) return 0; /* edit-btj retry: own lease only, no discovery */
    char directory[IPC_PATH_CAP]; strcpy(directory, path);
    char *slash = strrchr(directory, '/');
    if (slash == directory) slash[1] = 0; else if (slash) *slash = 0; else strcpy(directory, ".");
    /* Housekeeping is best effort: a failed barrier leaves safe retired bytes,
     * and a busy lease preserves the source. Never block session recovery. */
    (void)journal_retire_directory(directory, path);
    if (e->recover_existing) return restore_source(e, directory, path);
    char before[IPC_PATH_CAP] = "";
    struct timespec cutoff = {0};
    for (;;) {
        char previous[IPC_PATH_CAP] = ""; int previous_fd = -1;
        struct timespec newest = {0};
        int rc = discover_source(e, directory, before, cutoff, previous, &newest, &previous_fd);
        if (rc || previous_fd < 0) return rc;
        strcpy(before, previous); cutoff = newest;
        rc = restore_source(e, directory, previous);
        close(previous_fd);
        if (rc) return rc;
    }
}
