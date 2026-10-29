/* Startup only: validate and replay before any editor frame is submitted. Keep
 * the prior artifact until a complete recovered checkpoint is durable. */
/* Unported editor storage in private.h still names pthread types. */
#if defined(_WIN32)
#include <pthread.h>
#endif
#include "editor/private.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void path_slashes(char *path)
{
#if defined(_WIN32)
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
#else
    (void)path;
#endif
}
static void path_parent(char *path, char *slash)
{
    bool root = slash == path;
#if defined(_WIN32)
    if (slash == path + 2 && path[1] == ':') root = true;
#endif
    if (!slash) strcpy(path, ".");
    else if (root) slash[1] = 0;
    else *slash = 0;
}
static bool canonical_path(const char *path, char *out, size_t cap)
{
    if (os_path_canonical(path, out, cap) != OS_OK) return false;
    path_slashes(out); return true;
}

/* Portable ownership transfer; the legacy private.h entry remains for Linux. */
int editor_restore_defer_file(editor *e, const char *directory, const char *source, os_file lease);

/* edit-4da: the cost scan decides which buffers can replay only their ops.
 * COPY/whole-text buffers keep the existing materialised recovery path. */
typedef struct recovery_cost_buffer {
    struct recovery_cost_buffer *next;
    uint64_t id, full_bytes, prefix_bytes, deleted_bytes, len;
    char path[IPC_PATH_CAP];
    bool mapped;
} recovery_cost_buffer;
typedef struct recovery_cost {
    recovery_cost_buffer *buffers;
    uint64_t threshold;
} recovery_cost;
static recovery_cost_buffer *cost_find(const recovery_cost *cost, uint64_t id)
{
    for (recovery_cost_buffer *b = cost ? cost->buffers : NULL; b; b = b->next)
        if (b->id == id) return b;
    return NULL;
}
static void cost_fini(recovery_cost *cost)
{
    while (cost->buffers) {
        recovery_cost_buffer *b = cost->buffers; cost->buffers = b->next; free(b);
    }
}
typedef struct recovered_buffer {
    struct recovered_buffer *next;
    uint64_t id;
    /* edit-28u: gap buffer. Text = bytes[0, gap) + bytes[gap + gap_len,
     * len + gap_len). Checkpoint ops arrive at nondecreasing offsets, so
     * replaying them moves each byte a bounded number of times (linear), not
     * once per op as a flat memmove would (O(ops * len)). */
    uint8_t *bytes;
    size_t len, gap, gap_len;
    char target[IPC_PATH_CAP];
    journal_view_state view;
    bool full_view;
    editor_buffer *installed;
    bool save_pending;
    bool conflict; /* edit-btj: BASE no longer matches disk; edits are not replayed */
    char retained[IPC_PATH_CAP];
    /* edit-f3i: the INSERT/DELETE records since the last BASE, in order, so
     * install can apply them to the opened BASE generation (the tree keeps
     * ORIGINAL spans and its checkpoints stay differences from BASE). Each
     * op is OP_HEADER bytes (type, offset, length) plus the inserted bytes.
     * ops_valid is false when the log could not be kept (whole-text install,
     * as before). */
    editor_base_id base_id;
    uint8_t *ops;
    size_t ops_len, ops_cap;
    bool ops_valid;
    bool mapped_replay; /* no flat text or whole-BASE fallback */
    bool rewritten; /* edit-fi2: replayed from a retained copy; the target now differs */
    uint8_t pick;   /* edit-zizz: recovered_pick, used only by a partial restore */
} recovered_buffer;
/* edit-zizz: what an over-capacity restore does with each buffer. */
enum recovered_pick { PICK_DROP, PICK_LIVE, PICK_CLOSED, PICK_UNSAVED /* not yet placed */ };
/* (P) op header: 1 type byte + 8-byte offset + 8-byte length. */
#define OP_HEADER 17u
typedef struct recovered_session {
    recovered_buffer *buffers;
    uint64_t *tabs;
    size_t count, active;
    bool tabs_valid;
    uint32_t width, height;
    bool window_chosen; /* WINDOW v2 with JOURNAL_WINDOW_CHOSEN */
    bool tolerant;      /* edit-btj second pass: a changed BASE marks its buffer */
    bool conflicts;     /* some buffer was marked */
    uint64_t paint;     /* edit-dzk1: last PAINT id; nonzero = died in that tab's first paint */
    const recovery_cost *cost;
    /* edit-zizz: the session does not fit; install only picked buffers. */
    bool partial;
    size_t unsaved, unsaved_closed, dropped;
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
static void gap_move(recovered_buffer *b, size_t at)
{
    if (at < b->gap) memmove(b->bytes + at + b->gap_len, b->bytes + at, b->gap - at);
    else if (at > b->gap) memmove(b->bytes + b->gap, b->bytes + b->gap + b->gap_len, at - b->gap);
    b->gap = at;
}
/* Close the gap: bytes[0, len) is the text, as install expects. */
static void gap_close(recovered_buffer *b) { gap_move(b, b->len); }
static int gap_insert(recovered_buffer *b, size_t at, const uint8_t *data, size_t n)
{
    if (n > SIZE_MAX - b->len) return JOURNAL_NOMEM;
    gap_move(b, at);
    if (b->gap_len < n) {
        /* Grow by a quarter of the text beyond the request: a geometric
         * factor keeps the total suffix moves of an append run linear. */
        size_t extra = b->len / 4u, cap = b->len + b->gap_len;
        if (extra > SIZE_MAX - b->len - n) extra = 0;
        size_t want = b->len + n + extra;
        uint8_t *bytes = realloc(b->bytes, want ? want : 1); if (!bytes) return JOURNAL_NOMEM;
        size_t tail = cap - b->gap - b->gap_len, gap_len = want - b->len;
        memmove(bytes + b->gap + gap_len, bytes + b->gap + b->gap_len, tail);
        b->bytes = bytes; b->gap_len = gap_len;
    }
    memcpy(b->bytes + b->gap, data, n);
    b->gap += n; b->gap_len -= n; b->len += n; return 0;
}
static void op_log(recovered_buffer *b, uint32_t type, uint64_t off, uint64_t len, const uint8_t *data)
{
    if (!b->ops_valid) return;
    size_t extra = type == JOURNAL_INSERT ? (size_t)len : 0u;
    if (extra > SIZE_MAX - OP_HEADER - b->ops_len) { b->ops_valid = false; return; }
    size_t need = b->ops_len + OP_HEADER + extra;
    if (need > b->ops_cap) {
        /* (P) doubling: amortised O(1) copies per logged byte. */
        size_t cap = b->ops_cap > SIZE_MAX / 2 ? need : b->ops_cap * 2;
        if (cap < need) cap = need;
        uint8_t *ops = realloc(b->ops, cap);
        if (!ops) { b->ops_valid = false; free(b->ops); b->ops = NULL; b->ops_len = b->ops_cap = 0; return; }
        b->ops = ops; b->ops_cap = cap;
    }
    uint8_t *p = b->ops + b->ops_len;
    p[0] = (uint8_t)type; memcpy(p + 1, &off, 8); memcpy(p + 9, &len, 8);
    if (extra) memcpy(p + OP_HEADER, data, extra);
    b->ops_len = need;
}
static void fini(recovered_session *s)
{
    while (s->buffers) {
        recovered_buffer *b = s->buffers; s->buffers = b->next;
        free(b->bytes); free(b->ops); free(b);
    }
    free(s->tabs); *s = (recovered_session){0};
}
/* edit-fi0p F7: the bytes must belong to the recorded BASE, independently
 * of replay's earlier prefix check. Hold and validate the actual descriptor
 * across the complete read, and verify its pathname still selects it. */
static int materialize_base(recovered_buffer *b, const journal_base *base)
{
    os_file source = OS_FILE_INIT;
    if (os_file_open(&source, base->path, OS_OPEN_READ | OS_OPEN_NONBLOCK) != OS_OK) return JOURNAL_BASE_CHANGED;
    os_stat st;
    file_id before = {0}, after = {0}, named = {0};
    int rc = os_file_stat(&source, &st) ? JOURNAL_BASE_CHANGED : 0;
    if (!rc) {
        file_id_from_stat(&before, &st);
        if (st.kind != OS_KIND_FILE || before.dev != base->device || before.ino != base->inode ||
            before.size != base->size || before.mtime_ns != base->mtime_ns || before.ctime_ns != base->ctime_ns)
            rc = JOURNAL_BASE_CHANGED;
    }
    size_t done = 0;
    while (!rc && done < b->len) {
        size_t n;
        if (os_file_read(&source, b->bytes + done, b->len - done, &n) != OS_OK || !n) { rc = JOURNAL_BASE_CHANGED; break; }
        done += n;
    }
    if (!rc && os_file_stat(&source, &st)) rc = JOURNAL_BASE_CHANGED;
    if (!rc) {
        file_id_from_stat(&after, &st);
        if (file_id_stat_path(base->path, &named) || file_id_diff(&before, &after) || file_id_diff(&after, &named))
            rc = JOURNAL_BASE_CHANGED;
    }
    journal_base captured;
    if (!rc) {
        rc = journal_base_from_prefix(base->path, &after, b->bytes, b->len, &captured);
        if (!rc && !editor_base_id_matches(&b->base_id, &captured)) rc = JOURNAL_BASE_CHANGED;
    }
    os_file_close(&source); return rc;
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
    if (r->type == JOURNAL_PAINT) { s->paint = r->buffer_id; return 0; } /* edit-dzk1 */
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
        const recovery_cost_buffer *cost = cost_find(s->cost, r->buffer_id);
        b->mapped_replay = cost && cost->mapped;
        b->conflict = false;
        if (s->tolerant && *path) {
            /* edit-btj: the first pass stopped at a changed BASE. Its edits
             * cannot be replayed onto other bytes: open the disk text and keep
             * the edits in the set-aside source journal. */
            int check = journal_check_base(&base);
            if (check && check != JOURNAL_BASE_CHANGED) return check;
            if (check) {
                free(b->bytes); b->bytes = NULL; b->len = b->gap = b->gap_len = 0;
                b->conflict = s->conflicts = true; strcpy(b->target, path);
                return 0;
            }
        }
        /* edit-f3i: a BASE restarts the text, so the op log restarts too. */
        b->base_id = editor_base_id_of(&base); b->ops_len = 0; b->ops_valid = true;
        free(b->bytes); b->len = b->gap = (size_t)base.size; b->gap_len = 0;
        b->bytes = !b->mapped_replay && b->len ? malloc(b->len) : NULL;
        if (!b->mapped_replay && b->len && !b->bytes) { b->len = b->gap = 0; return JOURNAL_NOMEM; }
        strcpy(b->target, path);
        const char *leaf = strrchr(path, '/');
        if (leaf && !strncmp(leaf + 1, ".sublimite-base-", strlen(".sublimite-base-"))) strcpy(b->retained, path);
        if (*path && !b->mapped_replay) {
            int rc = materialize_base(b, &base); if (rc) return rc;
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
    if (b->mapped_replay) {
        uint64_t n = r->type == JOURNAL_INSERT ? r->size - sizeof off : get64(p + sizeof off);
        if (r->type == JOURNAL_INSERT) {
            if (n > SIZE_MAX - b->len) return JOURNAL_NOMEM;
            b->len += (size_t)n;
        } else if (r->type == JOURNAL_DELETE) {
            if (n > b->len - off) return JOURNAL_INVALID;
            b->len -= (size_t)n;
        } else return JOURNAL_INVALID;
        op_log(b, r->type, off, n, r->type == JOURNAL_INSERT ? p + sizeof off : NULL);
        return b->ops_valid ? 0 : JOURNAL_NOMEM;
    }
    if (r->type == JOURNAL_INSERT) {
        int rc = gap_insert(b, (size_t)off, p + 8, r->size - 8);
        if (!rc) op_log(b, JOURNAL_INSERT, off, r->size - 8, p + 8);
        return rc;
    }
    if (r->type == JOURNAL_DELETE) {
        uint64_t n = get64(p + 8); if (n > b->len - off) return JOURNAL_INVALID;
        gap_move(b, (size_t)off); b->gap_len += (size_t)n; b->len -= (size_t)n;
        op_log(b, JOURNAL_DELETE, off, n, NULL); return 0;
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
static bool base_id_equal(const editor_base_id *a, const editor_base_id *b)
{
    return a->valid && b->valid && a->size == b->size && a->mtime_ns == b->mtime_ns && a->inode == b->inode &&
        a->device == b->device && a->ctime_ns == b->ctime_ns && a->prefix_crc == b->prefix_crc &&
        a->prefix_len == b->prefix_len;
}
/* edit-fi2: does the opened target still hold exactly the retained copy's
 * bytes? Startup only; the copy is within the startup scan bound because the
 * cost scan charged its whole BASE (no mapped replay for a retained BASE). */
static bool same_file(piece_tree *tree, const char *path, uint64_t size)
{
    if (piece_len(tree) != size) return false;
    os_file source = OS_FILE_INIT;
    if (os_file_open(&source, path, OS_OPEN_READ | OS_OPEN_NOFOLLOW) != OS_OK) return false;
    uint8_t disk[JOURNAL_INSERT_SLICE_BYTES], text[JOURNAL_INSERT_SLICE_BYTES]; /* reuse the wire slice as a chunk */
    bool same = true;
    for (uint64_t off = 0; same && off < size;) {
        size_t n = size - off < sizeof disk ? (size_t)(size - off) : sizeof disk;
        size_t got;
        same = os_file_pread(&source, disk, n, off, &got) == OS_OK && got == n &&
            !piece_read(tree, off, text, n) && !memcmp(disk, text, n);
        off += n;
    }
    os_file_close(&source); return same;
}
/* Ops were validated against the replayed length when logged; a failing
 * piece op leaves its own call's content unchanged and the caller then
 * replaces the whole text. */
static int apply_ops(piece_tree *tree, const recovered_buffer *r)
{
    for (size_t at = 0; at < r->ops_len;) {
        const uint8_t *p = r->ops + at; uint64_t off, len;
        memcpy(&off, p + 1, 8); memcpy(&len, p + 9, 8);
        int rc = p[0] == JOURNAL_INSERT ? piece_insert(tree, off, p + OP_HEADER, (size_t)len) :
            piece_delete(tree, off, len, NULL);
        if (rc) return rc;
        at += OP_HEADER + (p[0] == JOURNAL_INSERT ? (size_t)len : 0u);
    }
    return 0;
}
static int install_buffer(editor *e, recovered_buffer *r)
{
    if (!r->mapped_replay) gap_close(r);
    uint64_t id;
    editor_buffer *b = NULL;
    int rc;
    if (r->mapped_replay) {
        /* (P) A threshold of one maps every nonempty file. Preflight already
         * required configured MMAP; keep a changed/shrunken target mapped too.
         * Prepare/install directly: add_buffer's mapped branch runs the loop
         * to submit its preview, before the startup renderer is initialised. */
        uint64_t threshold = e->cfg.copy_threshold; e->cfg.copy_threshold = 1;
        rc = editor_buffer_prepare(e, r->target, NULL, 0, &b);
        e->cfg.copy_threshold = threshold;
        if (!rc) rc = editor_install_buffer(e, b, &id);
        if (rc) { editor_buffer_destroy(b); return rc; }
    } else {
        rc = editor_add_recovered_buffer(e, *r->target ? r->target : NULL, &id);
        b = e->buffer;
    }
    if (rc) return rc;
    /* Attach without running editor_step or starting scans of unreplayed text. */
    while (b->pending_open && !file_open_ready(b->file)) {
        if (os_wake_wait(work_pool_wake(&e->pool), -1) != OS_OK) return EDITOR_ERR_IO;
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        if (b->operation_error) return EDITOR_ERR_IO;
    }
    if (b->pending_open) {
        if (r->mapped_replay) {
            if (file_check(b->file, NULL) || file_attach(b->file, b->tree)) return EDITOR_ERR_IO;
            b->pending_open = false;
            piece_destroy(b->preview); b->preview = NULL;
        } else {
            e->dirty = false;
            rc = editor_adopt_open(e, b); if (rc) return rc;
        }
    }
    if (r->conflict && !r->mapped_replay) { /* edit-btj: disk text, clean, view from the start */
        (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), false);
        r->installed = b; return editor_activate(e);
    }
    bool clean = r->conflict || (!r->save_pending &&
        (r->mapped_replay ? !r->ops_len : same_bytes(b, r)));
    undo_clear(&b->undo);
    /* edit-f3i: when the opened generation is the journalled BASE, replay
     * the logged ops onto it, so the tree keeps ORIGINAL spans and its
     * checkpoints are differences from BASE (edit-28u). Otherwise, or if
     * an op fails, the whole recovered text replaces the tree as before. */
    rc = r->conflict ? 0 : 1;
    /* edit-fi2: BASE was a retained private copy (TARGET the real file). If
     * the file still holds the copy's bytes, replay onto it as above; if it
     * was rewritten, the whole recovered text replaces it and a notice says
     * so. An interrupted save's copy (save_pending) keeps the whole-text
     * install: its target may hold either generation. */
    bool from_copy = !r->conflict && r->ops_valid && *r->target && *r->retained && !r->save_pending &&
        strcmp(r->target, r->retained) && same_file(b->tree, r->retained, r->base_id.size);
    r->rewritten = !r->conflict && *r->retained && !r->save_pending && !from_copy && !clean;
    if (!r->conflict && r->ops_valid && *r->target && (from_copy || base_id_equal(&b->origin, &r->base_id)) &&
        piece_len(b->tree) == r->base_id.size) {
        rc = apply_ops(b->tree, r);
        if (!rc && piece_len(b->tree) != r->len) rc = 1;
    }
    if (rc) {
        /* A failed mapped install must never fall back to a whole-BASE copy. */
        if (r->mapped_replay) return EDITOR_ERR_IO;
        rc = piece_delete(b->tree, 0, piece_len(b->tree), NULL);
        if (!rc) rc = piece_insert(b->tree, 0, r->bytes, r->len);
    }
    if (rc) return EDITOR_ERR_MEMORY;
    b->revision++; b->saved_lost = !clean;
    if (r->mapped_replay) editor_large_init(b);
    else b->lines = piece_line_count(b->tree);
    b->history_count = b->history_cursor = b->history_head = b->saved_cursor = 0;
    b->soft_count = b->soft_cursor = b->soft_head = 0;
    b->soft_base_redo = 0;
    b->mark_valid = false;
    b->last_key_valid = false;
    size_t len = (size_t)piece_len(b->tree);
    lineidx_destroy(b->index); b->index = lineidx_create(len);
    if (!b->index || lineidx_build_reserve(b->index)) return EDITOR_ERR_MEMORY;
    piece_snapshot *snapshot = piece_snapshot_take(b->tree); if (!snapshot) return EDITOR_ERR_MEMORY;
    rc = lineidx_bind_snapshot(b->index, snapshot); piece_snapshot_release(snapshot);
    if (rc) return EDITOR_ERR_MEMORY;
    b->index_dirty = true;
    view_state state = b->initial;
    if (!r->conflict) {
        state.selection.cursor = r->view.position.cursor > len ? len : r->view.position.cursor;
        state.selection.anchor = r->view.position.anchor > len ? len : r->view.position.anchor;
        state.first_byte = r->view.position.scroll_byte > len ? len : r->view.position.scroll_byte;
        state.first_line = r->full_view ? r->view.first_line : piece_byte_to_line(b->tree, state.first_byte);
        state.visual_byte = state.first_byte; state.hscroll = r->view.position.scroll_x;
        if (r->full_view) {
            state.selection.preferred_col = r->view.preferred_col;
            state.first_line = r->view.first_line;
            state.visual_byte = r->view.visual_byte; /* unused unwrapped value may outlive a deletion */
            state.wrap = (r->view.flags & JOURNAL_VIEW_WRAP) != 0;
            state.approximate = (r->view.flags & JOURNAL_VIEW_APPROXIMATE) != 0;
            state.visual_end = (r->view.flags & JOURNAL_VIEW_VISUAL_END) != 0;
        }
    }
    b->initial = state; e->v.state = state;
    e->tabs.slots[e->tabs.active].state = state;
    (void)tabs_set_modified(&e->tabs, tabs_active_index(&e->tabs), !clean);
    if (r->mapped_replay) editor_large_start(e, b);
    r->installed = b; return editor_activate(e);
}
/* edit-dzk1: the session's last PAINT names a known buffer that would be
 * restored as a live tab. */
static bool unpainted_live(const recovered_session *s)
{
    bool known = false;
    for (const recovered_buffer *b = s->buffers; s->paint && b; b = b->next) known = known || b->id == s->paint;
    if (!known || !s->tabs_valid) return known;
    for (size_t i = 0; i < s->count; i++) if (s->tabs[i] == s->paint) return true;
    return false;
}
/* edit-zizz: does the regular file at path hold exactly these bytes? Round 2
 * (review): the descriptor's generation (identity, size, mtime, ctime) must
 * be unchanged across the whole read and the name must still select it, so
 * a file rewritten between or after chunks never compares equal. */
static bool same_generation(const os_stat *a, const os_stat *b)
{
    return os_identity_equal(&a->id, &b->id) && a->kind == b->kind && a->size == b->size &&
        a->mtime_ns == b->mtime_ns && a->ctime_ns == b->ctime_ns;
}
static bool disk_equals(const char *path, const uint8_t *bytes, size_t len)
{
    os_file source = OS_FILE_INIT;
    if (os_file_open(&source, path, OS_OPEN_READ | OS_OPEN_NONBLOCK) != OS_OK) return false;
    os_stat st, after, named;
    bool same = os_file_stat(&source, &st) == OS_OK && st.kind == OS_KIND_FILE && st.size == len;
    uint8_t disk[JOURNAL_INSERT_SLICE_BYTES]; /* reuse the wire slice as a chunk, as same_file does */
    for (size_t off = 0; same && off < len;) {
        size_t n = len - off < sizeof disk ? len - off : sizeof disk, got;
        same = os_file_pread(&source, disk, n, off, &got) == OS_OK && got == n && !memcmp(disk, bytes + off, n);
        off += n;
    }
    same = same && os_file_stat(&source, &after) == OS_OK && same_generation(&st, &after) &&
        os_stat_path(path, 0, &named) == OS_OK && same_generation(&st, &named);
    os_file_close(&source); return same;
}
/* edit-zizz: a buffer whose loss would lose text. An untitled buffer with
 * content, or a file whose replayed bytes differ from the file install would
 * open (the same comparison install_buffer's clean flag makes). A buffer
 * without ops after a BASE that is the file itself replayed exactly the BASE
 * bytes, which replay validated against the file. A retained private BASE
 * copy (edit-fi2) is compared with the target like any edited buffer. An
 * interrupted save always counts as unsaved. A changed BASE (edit-btj
 * conflict) installs only the disk text: its edits live in the source
 * journal, which a session with conflicts sets aside whole, so its tab ranks
 * as unmodified. */
static bool unsaved_buffer(recovered_buffer *r)
{
    if (r->conflict) return false;
    if (r->save_pending) return true;
    if (!*r->target) return r->len != 0;
    if (r->ops_valid && !r->ops_len && !*r->retained) return false;
    if (r->mapped_replay) return true; /* ops onto a mapped BASE: no flat text to compare */
    gap_close(r);
    return !disk_equals(r->target, r->bytes, r->len);
}
/* edit-zizz (coordinator follow-up): an unmodified file tab whose target no
 * longer exists carries no bytes; the over-capacity path never reopens it.
 * Only a definite absence counts: any other stat failure keeps the tab. */
static bool target_vanished(const recovered_buffer *r)
{
    os_stat st;
    os_err rc = *r->target ? os_stat_path(r->target, 0, &st) : OS_OK;
    return rc == OS_E_NOENT || rc == OS_E_NOTDIR;
}
/* edit-zizz: pick what an over-capacity session restores (decided by Tobias
 * 2026-10-29). Every unsaved buffer first: live while live room remains,
 * then closed; then unmodified tabs in recorded tab order while live room
 * remains, except tabs whose file vanished; the rest are dropped. Capacity
 * follows session_fits: a closed buffer needs one temporary live slot before
 * editor_close_tab retains it,
 * and the edit-dzk1 unpainted buffer is never live. False when some unsaved
 * buffer does not fit: the caller defers the whole source. */
static bool plan_partial(const editor *e, recovered_session *s)
{
    size_t used = 0, total = 0, unsaved = 0, held = 0;
    for (size_t i = 0; i < e->buffer_capacity; i++) if (e->buffers[i]) used++;
    size_t room = e->tabs.capacity - tabs_count(&e->tabs);
    size_t closed_room = e->tabs.closed_capacity - tabs_closed_count(&e->tabs);
    size_t slots = e->buffer_capacity - used;
    recovered_buffer *unpainted = s->paint ? find(s, s->paint) : NULL;
    for (recovered_buffer *b = s->buffers; b; b = b->next) {
        total++; b->pick = PICK_DROP;
        if (!unsaved_buffer(b)) continue;
        if (b == unpainted) { b->pick = PICK_CLOSED; held++; }
        else { b->pick = PICK_UNSAVED; unsaved++; }
    }
    size_t live = unsaved, closed = held;
    if (closed || live > room) {
        size_t open = room ? room - 1u : 0; /* the temporary slot */
        if (live > open) { closed += live - open; live = open; }
    }
    s->unsaved = unsaved + held; s->unsaved_closed = closed;
    if ((closed && !room) || closed > closed_room || live + closed > slots) return false;
    /* Unsaved buffers in tab order, then the closed ones, take live slots. */
    size_t spare = room - live - (closed ? 1u : 0u), left = live;
    if (spare > slots - live - closed) spare = slots - live - closed;
    for (size_t i = 0; left && s->tabs_valid && i < s->count; i++) {
        recovered_buffer *b = find(s, s->tabs[i]);
        if (b && b->pick == PICK_UNSAVED) { b->pick = PICK_LIVE; left--; }
    }
    for (recovered_buffer *b = s->buffers; b; b = b->next) if (b->pick == PICK_UNSAVED) {
        if (left) { b->pick = PICK_LIVE; left--; }
        else b->pick = PICK_CLOSED;
    }
    /* Unmodified tabs in recorded tab order (buffer order without TABS). */
    size_t kept = 0;
    if (s->tabs_valid) {
        for (size_t i = 0; kept < spare && i < s->count; i++) {
            recovered_buffer *b = find(s, s->tabs[i]);
            if (b && b != unpainted && b->pick == PICK_DROP && !target_vanished(b)) { b->pick = PICK_LIVE; kept++; }
        }
    } else {
        for (recovered_buffer *b = s->buffers; kept < spare && b; b = b->next)
            if (b != unpainted && b->pick == PICK_DROP && !target_vanished(b)) { b->pick = PICK_LIVE; kept++; }
    }
    s->dropped = total - live - closed - kept;
    s->partial = true; return true;
}
/* edit-zizz: install the picked buffers. Live tabs keep their recorded order
 * (unsaved buffers that were closed follow them); the active tab, or its
 * right (else left) live neighbour, stays active, as for edit-dzk1's skip. */
static int install_partial(editor *e, recovered_session *s, recovered_buffer *unpainted, bool unpainted_was_live,
    size_t *count, size_t *active)
{
    size_t installed = 0, before = 0;
    bool found = !s->tabs_valid; /* without TABS the first buffer is active */
    int rc = 0;
    for (size_t i = 0; !rc && s->tabs_valid && i < s->count; i++) {
        recovered_buffer *b = find(s, s->tabs[i]);
        if (!b) return EDITOR_ERR_IO;
        if (b == unpainted || b->pick != PICK_LIVE || b->installed) continue;
        if (i == s->active) found = true;
        else if (i < s->active) before++;
        rc = install_buffer(e, b);
        if (!rc) installed++;
    }
    for (recovered_buffer *b = s->buffers; !rc && b; b = b->next)
        if (b != unpainted && b->pick == PICK_LIVE && !b->installed) { rc = install_buffer(e, b); if (!rc) installed++; }
    for (recovered_buffer *b = s->buffers; !rc && b; b = b->next) if (b != unpainted && b->pick == PICK_CLOSED) {
        rc = install_buffer(e, b);
        if (!rc) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
    }
    if (!rc && unpainted && unpainted->pick == PICK_CLOSED) {
        rc = install_buffer(e, unpainted);
        if (!rc) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
        if (!rc && unpainted_was_live) {
            e->recovery_notice = "a restored tab died in its first paint last time; it is closed (reopen: Ctrl+Shift+T)";
            e->recovery_notice_mutations = e->stats.mutations;
        }
    }
    *count = installed;
    *active = found || before < installed ? before : installed ? installed - 1u : 0;
    return rc;
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
     * closed tabs so unrelated recovery content survives the new checkpoint.
     * edit-dzk1: the source launch died inside the first paint of the tab its
     * last PAINT names. Restoring it live would repeat that death on every
     * launch, so it is installed last and closed (the newest closed tab, the
     * one reopen restores) with a notice; its text and edits are kept. */
    recovered_buffer *unpainted = s->paint ? find(s, s->paint) : NULL;
    bool unpainted_was_live = unpainted_live(s);
    size_t count = s->count, restored_active = s->active, skipped = SIZE_MAX;
    if (!rc && s->partial) rc = install_partial(e, s, unpainted, unpainted_was_live, &count, &restored_active);
    for (size_t i = 0; !rc && !s->partial && i < s->count; i++) {
        recovered_buffer *b = find(s, s->tabs[i]);
        if (!b) { rc = EDITOR_ERR_IO; break; }
        if (b == unpainted) { skipped = i; continue; }
        rc = install_buffer(e, b);
    }
    for (recovered_buffer *b = s->buffers; !rc && !s->partial && b; b = b->next) if (!b->installed && b != unpainted) {
        rc = install_buffer(e, b);
        if (!rc && s->tabs_valid) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
    }
    if (!rc && !s->partial && unpainted) {
        rc = install_buffer(e, unpainted);
        if (!rc) rc = editor_close_tab(e, tabs_active_index(&e->tabs));
        if (!rc && unpainted_was_live) {
            e->recovery_notice = "a restored tab died in its first paint last time; it is closed (reopen: Ctrl+Shift+T)";
            e->recovery_notice_mutations = e->stats.mutations;
        }
    }
    if (skipped != SIZE_MAX) { /* the same tab, or its right (else left) neighbour, stays active */
        count--;
        if (restored_active > skipped || (restored_active == skipped && restored_active == count && count)) restored_active--;
    }
    if (!rc && live) rc = editor_select_tab(e, active);
    else if (!rc && count) rc = editor_select_tab(e, restored_active);
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
    /* edit-ivv: refused by the shared directory guard. The recovered text is
     * installed and the source stays on disk under its lease until the owed
     * checkpoint lands (editor_checkpoint_owed, on the journal tick). */
    if (rc == JOURNAL_BUSY) { e->busy_checkpoint = true; rc = 0; }
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
    os_file source = OS_FILE_INIT;
    if (os_file_open(&source, path, OS_OPEN_READ | OS_OPEN_NOFOLLOW) != OS_OK) return JOURNAL_IO;
    os_stat st; uint8_t *bytes = NULL; size_t size = 0, done = 0; int rc = 0;
    if (os_file_stat(&source, &st) != OS_OK || st.size > SIZE_MAX) rc = JOURNAL_IO;
    else { size = (size_t)st.size; bytes = size ? malloc(size) : NULL; if (size && !bytes) rc = JOURNAL_NOMEM; }
    while (!rc && done < size) {
        size_t n;
        if (os_file_read(&source, bytes + done, size - done, &n) != OS_OK || !n) { rc = JOURNAL_IO; break; }
        done += n;
    }
    os_file_close(&source);
    journal_replay_result result;
    s->tolerant = true;
    if (!rc) rc = journal_replay_bytes(bytes, size, restore_record, s, &result);
    free(bytes); return rc;
}
/* edit-btj: never delete a recovery source. Rename it beside itself under a
 * dotted name, which session discovery skips; never replace an existing name. */
int editor_set_aside(const char *path, char *out, size_t cap)
{
    uint64_t now = os_wall_ns();
    /* (P) nanoseconds per second, matching the previous timespec suffix. */
    int n = snprintf(out, cap, "%s.set-aside-%llu%09llu", path,
        (unsigned long long)(now / UINT64_C(1000000000)), (unsigned long long)(now % UINT64_C(1000000000)));
    if (n < 0 || (size_t)n >= cap) { *out = 0; return EDITOR_ERR_ARG; }
    if (os_rename(path, out, OS_RENAME_NOREPLACE) != OS_OK) { *out = 0; return EDITOR_ERR_IO; }
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
    if (unpainted_live(s)) live--; /* edit-dzk1: restored closed instead */
    size_t closed = buffers - live;
    size_t available = e->tabs.capacity - tabs_count(&e->tabs);
    return buffers <= e->buffer_capacity - used && live <= available &&
        (!closed || live < available) &&
        closed <= e->tabs.closed_capacity - tabs_closed_count(&e->tabs);
}
static int restore_source(editor *e, const char *directory, const char *previous, const recovery_cost *cost)
{
    char lock_path[IPC_PATH_CAP];
    recovered_session session = {.cost = cost}; journal_replay_result result;
    int rc = journal_replay_file(previous, restore_record, &session, &result);
    /* edit-btj: a discovered journal never blocks launch. A changed BASE gets
     * a tolerant pass; any other failure sets the source aside and opens empty.
     * An explicitly named existing journal keeps the strict contract. */
    bool aside = false;
    if (rc && !e->recover_existing) {
        fini(&session);
        session.cost = cost;
        rc = replay_tolerant(previous, &session);
        if (rc) { fini(&session); aside = true; rc = 0; }
    }
    if (!rc && e->recover_existing && !session.buffers) rc = EDITOR_ERR_IO;
    if (!rc && session.buffers) {
    if (!e->recover_existing && !session_fits(e, &session) && !plan_partial(e, &session)) {
            /* Do not partially install or evict a dirty closed tab. Every
             * skipped source stays discoverable on subsequent launches.
             * edit-zizz: only a source whose unsaved buffers do not all fit
             * gets here; the notice counts them. */
            (void)snprintf(e->session_notice, sizeof e->session_notice,
                "additional session recovery deferred; journal retained (tab capacity: %zu unsaved tab%s)",
                session.unsaved, session.unsaved == 1u ? "" : "s");
            e->recovery_notice = e->session_notice;
            e->recovery_notice_mutations = e->stats.mutations;
            fini(&session); return 0;
        }
        const char *prior = e->recovery_notice;
        rc = install_session(e, &session);
        if (!rc) e->startup_restored = true;
        else if (!e->recover_existing) {
            /* Partly installed state cannot be unwound here: set the source
             * aside and let editor_open start again without discovery. */
            (void)editor_set_aside(previous, e->recovery_aside, sizeof e->recovery_aside);
            e->recovery_retry = true; e->recovering = true;
            fini(&session); return EDITOR_ERR_IO;
        }
        if (!rc && session.partial && e->recovery_notice == prior) {
            /* edit-zizz: say what a partial restore left out; a later notice
             * (changed BASE, set-aside source) still takes precedence. */
            size_t n = session.unsaved, k = session.unsaved_closed, d = session.dropped;
            char head[sizeof e->session_notice] = "", tail[sizeof e->session_notice] = "";
            if (n && k) (void)snprintf(head, sizeof head, "restored %zu unsaved tab%s (%zu closed: Ctrl+Shift+T)", n, n == 1u ? "" : "s", k);
            else if (n) (void)snprintf(head, sizeof head, "restored %zu unsaved tab%s", n, n == 1u ? "" : "s");
            if (d) (void)snprintf(tail, sizeof tail, "%zu unmodified tab%s from the last session %s not reopened",
                d, d == 1u ? "" : "s", d == 1u ? "was" : "were");
            (void)snprintf(e->session_notice, sizeof e->session_notice, "%s%s%s", head, *head && *tail ? "; " : "", tail);
            if (*e->session_notice) {
                e->recovery_notice = e->session_notice;
                e->recovery_notice_mutations = e->stats.mutations;
            }
        }
    }
    for (recovered_buffer *b = session.buffers; !rc && !aside && !session.conflicts && b; b = b->next) if (b->rewritten) {
        static const char recovered[] = "file changed on disk since this session; its unsaved edits were recovered";
        if (session.partial && e->recovery_notice == e->session_notice) {
            /* edit-zizz round 2: keep the partial restore's account in view. */
            size_t used = strlen(e->session_notice);
            if (!strstr(e->session_notice, recovered))
                (void)snprintf(e->session_notice + used, sizeof e->session_notice - used, "; %s", recovered);
        } else e->recovery_notice = recovered;
        e->recovery_notice_mutations = e->stats.mutations;
    }
    /* edit-zizz round 2: a partial restore that dropped buffers never deletes
     * its source. Dropping rests on a comparison with files that can change
     * at any time; the set-aside source (dotted name, never discovered, its
     * BASE references pinned by journal_retire_directory's scan of set-aside
     * sessions) still replays every dropped buffer. */
    bool kept = !rc && !aside && !session.conflicts && session.partial && session.dropped;
    if (kept) {
        static const char kept_notice[] = "the old session journal was set aside";
        if (e->recovery_notice == e->session_notice) {
            size_t used = strlen(e->session_notice);
            (void)snprintf(e->session_notice + used, sizeof e->session_notice - used, "; %s", kept_notice);
        } else e->recovery_notice = kept_notice;
        e->recovery_notice_mutations = e->stats.mutations;
    }
    if (!rc && (aside || session.conflicts || kept)) {
        const char *text = kept ? NULL : session.conflicts && !aside ?
            "file changed on disk since this session; its unsaved edits were set aside" :
            "session recovery failed; its journal was set aside";
        if (!text) { /* kept: its notice is set above */ }
        else if (session.partial && e->recovery_notice == e->session_notice) {
            /* edit-zizz: keep what the partial restore left out in view too. */
            size_t used = strlen(e->session_notice);
            (void)snprintf(e->session_notice + used, sizeof e->session_notice - used, "; %s", text);
        } else e->recovery_notice = text;
        e->recovery_notice_mutations = e->stats.mutations;
        (void)editor_set_aside(previous, e->recovery_aside, sizeof e->recovery_aside);
        if (!lock_name(lock_path, sizeof lock_path, previous)) (void)os_unlink(lock_path);
        os_dir dir = OS_DIR_INIT;
        if (os_dir_open(&dir, directory) == OS_OK) { (void)os_dir_sync(&dir); os_dir_close(&dir); }
        fini(&session); return 0;
    }
    /* Failure never retires the recovery source. Corrupt/torn tails are replay's
     * validated prefix, so the durable replacement checkpoint contains exactly
     * the accepted session. */
    if (!rc && session.buffers && e->busy_checkpoint) {
        /* edit-ivv: no durable replacement yet; retire after the owed one. */
        if (!e->recover_existing) strcpy(e->busy_source, previous); /* both IPC_PATH_CAP */
    } else if (!rc && session.buffers) {
        if (!e->recover_existing && os_unlink(previous) != OS_OK) rc = EDITOR_ERR_IO;
        else if (!e->recover_existing) {
            if (!lock_name(lock_path, sizeof lock_path, previous)) (void)os_unlink(lock_path);
        }
        os_dir dir = OS_DIR_INIT;
        if (os_dir_open(&dir, directory) != OS_OK || os_dir_sync(&dir) != OS_OK) rc = EDITOR_ERR_IO;
        os_dir_close(&dir);
        if (!rc) {
            /* The durable CURRENT checkpoint no longer references private
             * generations retained by an interrupted save. Retire only journal
             * retention names beside this session's canonical directory. */
            char canonical[IPC_PATH_CAP];
            if (canonical_path(directory, canonical, sizeof canonical)) {
                os_dir retained = OS_DIR_INIT;
                if (os_dir_open(&retained, canonical) == OS_OK) for (recovered_buffer *b = session.buffers; b; b = b->next) {
                    size_t length = strlen(canonical);
                    if (*b->retained && !strncmp(b->retained, canonical, length) && b->retained[length] == '/' &&
                        !strchr(b->retained + length + 1, '/')) (void)os_unlink_at(&retained, b->retained + length + 1);
                }
                os_dir_close(&retained);
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
static bool newer_source(int64_t a, const char *a_path, int64_t b, const char *b_path)
{
    return a > b || (a == b && strcmp(a_path, b_path) > 0);
}

/* edit-4da: preserve fm4's single-read-pass model, but an op-only mapped
 * install touches the acquisition prefix and deleted ORIGINAL bytes, not
 * every kept BASE byte. (P) UINT64_MAX is the over-bound sentinel. */
static void cost_add(uint64_t *total, uint64_t size)
{
    if (*total > JOURNAL_STARTUP_SCAN_BYTES || size > JOURNAL_STARTUP_SCAN_BYTES - *total) *total = UINT64_MAX;
    else *total += size;
}
static int cost_record(void *ctx, const journal_record *r)
{
    recovery_cost *cost = ctx;
    recovery_cost_buffer *b = cost_find(cost, r->buffer_id);
    if (r->type == JOURNAL_BASE) {
        journal_base base; char path[IPC_PATH_CAP];
        if (journal_decode_base(r, &base, path, sizeof path)) return 0;
        if (!b) {
            b = calloc(1, sizeof *b); if (!b) return JOURNAL_NOMEM;
            b->id = r->buffer_id; b->mapped = true; b->next = cost->buffers; cost->buffers = b;
        }
        cost_add(&b->full_bytes, base.size);
        cost_add(&b->prefix_bytes, base.size < FILE_PREFIX_MAX ? base.size : FILE_PREFIX_MAX);
        b->mapped = b->mapped && *path && base.size > FILE_PREFIX_MAX && base.size >= cost->threshold && base.size <= SIZE_MAX;
        b->len = base.size; strcpy(b->path, path);
    } else if (b && r->type == JOURNAL_TARGET) {
        if (strcmp(b->path, (const char *)r->data)) {
            char canonical[IPC_PATH_CAP];
            if (!canonical_path((const char *)r->data, canonical, sizeof canonical) || strcmp(b->path, canonical)) b->mapped = false;
        }
    } else if (b && r->type == JOURNAL_VIEW) {
        journal_view_state saved_view;
        if (journal_decode_view_state(r, &saved_view) ||
            (r->size != JOURNAL_VIEW_STATE_BYTES && saved_view.position.scroll_byte)) b->mapped = false;
    } else if (b && (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE)) {
        uint64_t off = get64(r->data);
        uint64_t n = r->type == JOURNAL_INSERT ? r->size - sizeof off : get64(r->data + sizeof off);
        /* Malformed ranges remain the replay path's to reject. They cannot
         * make an otherwise over-bound flat recovery eligible for startup. */
        if (off > b->len) { b->mapped = false; return 0; }
        if (r->type == JOURNAL_INSERT) {
            if (n > UINT64_MAX - b->len) b->mapped = false;
            else b->len += n;
        } else {
            if (n > b->len - off) { b->mapped = false; return 0; }
            if (!off && n == b->len) b->mapped = false; /* pre-28u whole-text shape */
            cost_add(&b->deleted_bytes, n); b->len -= n;
        }
    }
    return 0;
}
static bool restore_deferred(const editor *e, const char *path, uint64_t size, recovery_cost *cost)
{
    if (size > JOURNAL_STARTUP_SCAN_BYTES) return true;
    cost->threshold = e->cfg.copy_threshold ? e->cfg.copy_threshold : file_default_copy_threshold();
    uint64_t total = size; journal_replay_result result;
    if (journal_scan_file(path, cost_record, cost, &result)) return true;
    for (const recovery_cost_buffer *b = cost->buffers; b; b = b->next) {
        cost_add(&total, b->mapped ? b->prefix_bytes : b->full_bytes);
        if (b->mapped) cost_add(&total, b->deleted_bytes);
    }
    return total > JOURNAL_STARTUP_SCAN_BYTES;
}
static int discover_source(editor *e, const char *directory, const char *before,
    int64_t cutoff, char *previous, int64_t *newest, os_file *previous_file, uint64_t *size)
{
    os_dir dir = OS_DIR_INIT; os_dir_iter it;
    if (os_dir_open(&dir, directory) != OS_OK) return EDITOR_ERR_IO;
    if (os_dir_iter_open(&it, &dir) != OS_OK) { os_dir_close(&dir); return EDITOR_ERR_IO; }
    char current[IPC_PATH_CAP]; strcpy(current, e->cfg.journal_path); path_slashes(current);
    const char *entry; os_err next;
    while ((next = os_dir_next(&it, &entry)) == OS_OK && entry) {
        if (strncmp(entry, "session-", strlen("session-")) || strchr(entry, '.')) continue;
        char candidate[IPC_PATH_CAP], lock_path[IPC_PATH_CAP];
        int n = snprintf(candidate, sizeof candidate, "%s/%s", directory, entry);
        if (n < 0 || (size_t)n >= sizeof candidate || !strcmp(candidate, current)) continue;
        os_file source = OS_FILE_INIT;
        if (os_file_open_at(&source, &dir, entry, OS_OPEN_READ | OS_OPEN_NOFOLLOW | OS_OPEN_NONBLOCK) != OS_OK) continue;
        os_stat st;
        os_err stated = os_file_stat(&source, &st); os_file_close(&source);
        if (stated != OS_OK || st.kind != OS_KIND_FILE || !st.size) continue;
        if (*before && !newer_source(cutoff, before, st.mtime_ns, candidate)) continue;
        n = snprintf(lock_path, sizeof lock_path, ".%s.lock", entry);
        if (n < 0 || (size_t)n >= sizeof lock_path) continue;
        os_file lease = OS_FILE_INIT;
        if (os_file_open_at(&lease, &dir, lock_path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_NOFOLLOW) != OS_OK ||
            os_file_try_lock(&lease, OS_LOCK_EXCLUSIVE) != OS_OK) { os_file_close(&lease); continue; }
        if (!os_file_is_open(previous_file) || newer_source(st.mtime_ns, candidate, *newest, previous)) {
            os_file_close(previous_file);
            *previous_file = lease; *newest = st.mtime_ns; strcpy(previous, candidate); *size = st.size;
        } else os_file_close(&lease);
    }
    os_dir_iter_close(&it); os_dir_close(&dir);
    return next == OS_OK ? 0 : EDITOR_ERR_IO;
}

int editor_session_restore(editor *e)
{
    char normalized[IPC_PATH_CAP];
    if (strlen(e->cfg.journal_path) >= sizeof normalized) return EDITOR_ERR_ARG;
    strcpy(normalized, e->cfg.journal_path); path_slashes(normalized);
    const char *path = normalized;
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    if (!e->recover_existing && strncmp(name, "session-", strlen("session-"))) return 0;
    char lock_path[IPC_PATH_CAP];
    if (lock_name(lock_path, sizeof lock_path, path)) return EDITOR_ERR_ARG;
    if (e->cfg.session_lease) {
        /* edit-cxo: the creator took this lease before the journal name was
         * visible (journal_session_create). Share its open file description:
         * a second flock on a new description would conflict with it. Refuse
         * a descriptor that does not guard this journal's lock name. */
#if defined(_WIN32)
        /* editor_config still carries the Linux lease API until W2.4d. */
        return EDITOR_ERR_ARG;
#else
        os_stat held, named; os_file lease = OS_FILE_INIT;
        os_file creator = os_file_from_fd(*e->cfg.session_lease);
        if (os_file_dup(&lease, &creator) != OS_OK) return EDITOR_ERR_IO;
        e->session_lock = os_file_fd(&lease);
        if (os_file_stat(&lease, &held) != OS_OK || os_stat_path(lock_path, 0, &named) != OS_OK ||
            !os_identity_equal(&held.id, &named.id)) return EDITOR_ERR_IO;
#endif
    } else {
        os_file lease = OS_FILE_INIT;
        if (os_file_open(&lease, lock_path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_NOFOLLOW) != OS_OK) return EDITOR_ERR_IO;
#if defined(_WIN32)
        e->session_lock = lease;
#else
        e->session_lock = os_file_fd(&lease);
#endif
        if (os_file_try_lock(&lease, OS_LOCK_EXCLUSIVE) != OS_OK) return EDITOR_ERR_IO;
    }
    e->session_lock_owned = true;
    if (e->recovery_skip) return 0; /* edit-btj retry: own lease only, no discovery */
    char directory[IPC_PATH_CAP]; strcpy(directory, path);
    char *slash = strrchr(directory, '/');
    path_parent(directory, slash);
    /* Housekeeping is best effort: a failed barrier leaves safe retired bytes,
     * and a busy lease preserves the source. Never block session recovery. */
    (void)journal_retire_directory(directory, path);
    if (e->recover_existing) return restore_source(e, directory, path, NULL);
    char before[IPC_PATH_CAP] = "";
    int64_t cutoff = 0;
    for (;;) {
        char previous[IPC_PATH_CAP] = ""; os_file previous_file = OS_FILE_INIT;
        int64_t newest = 0; uint64_t size = 0;
        int rc = discover_source(e, directory, before, cutoff, previous, &newest, &previous_file, &size);
        if (rc || !os_file_is_open(&previous_file)) { os_file_close(&previous_file); return rc; }
        strcpy(before, previous); cutoff = newest;
        recovery_cost cost = {0};
        if (restore_deferred(e, previous, size, &cost)) {
            cost_fini(&cost);
            /* edit-fm4: a GB-scale orphan is not replayed, copied or renamed
             * before the first frame. It stays discoverable under its own
             * name, byte for byte, and the lease is released for later. */
            e->recovery_deferred++;
            /* edit-qct: listed as a recoverable closed tab holding the lease,
             * restored lazily on reopen (restore.c). A full closed-tab pool
             * leaves the remainder counted only, as fm4 did. */
            bool listed = editor_restore_defer_file(e, directory, previous, previous_file) == 0;
            e->recovery_notice = listed ? "large session journal kept as a closed tab; reopen it to restore" :
                "large session journal kept for recovery; not replayed at startup (closed-tab pool full)";
            e->recovery_notice_mutations = e->stats.mutations;
            if (!listed) os_file_close(&previous_file);
            continue;
        }
        rc = restore_source(e, directory, previous, &cost);
        cost_fini(&cost);
        /* edit-ivv: a source awaiting the owed checkpoint keeps its lease. */
#if defined(_WIN32)
        if (!rc && *e->busy_source && !os_file_is_open(&e->busy_source_lease)) e->busy_source_lease = previous_file;
#else
        if (!rc && *e->busy_source && e->busy_source_lease < 0) e->busy_source_lease = os_file_fd(&previous_file);
#endif
        else os_file_close(&previous_file);
        if (rc) return rc;
        if (e->busy_checkpoint) return 0; /* edit-ivv: later sources stay discoverable */
    }
}
/* edit-ivv: the owed checkpoint is durable; retire the source as
 * restore_source does after its own checkpoint. Retained generations it named
 * are left to journal_retire_directory's orphan-BASE sweep. */
void editor_recovery_owed_done(editor *e)
{
    e->busy_checkpoint = false;
    if (*e->busy_source) {
        char lock_path[IPC_PATH_CAP], directory[IPC_PATH_CAP];
        (void)os_unlink(e->busy_source);
        if (!lock_name(lock_path, sizeof lock_path, e->busy_source)) (void)os_unlink(lock_path);
        strcpy(directory, e->busy_source);
        char *slash = strrchr(directory, '/');
        path_parent(directory, slash);
        os_dir dir = OS_DIR_INIT;
        if (os_dir_open(&dir, directory) == OS_OK) { (void)os_dir_sync(&dir); os_dir_close(&dir); }
        e->busy_source[0] = 0;
    }
#if defined(_WIN32)
    os_file_close(&e->busy_source_lease);
#else
    os_file lease = os_file_from_fd(e->busy_source_lease); os_file_close(&lease);
    e->busy_source_lease = -1; /* (P) no descriptor */
#endif
}
