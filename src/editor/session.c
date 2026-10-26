/* Published immutable session graph. Save input pins a root and selected
 * snapshot; complete checkpoint encoding and all filesystem work are bulk jobs. */
/* Unported editor storage in private.h still names pthread types. */
#if defined(_WIN32)
#include <pthread.h>
#endif
#include "editor/private.h"
#include "trace/trace.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdatomic.h>

typedef struct session_node {
    struct session_node *child[2], *next;
    size_t refs, rank, live_count;
    editor_buffer *buffer;
    piece_snapshot *snapshot;
    journal_base base;
    view_state view;
    uint64_t revision;
    bool live;
    /* edit-f3i: the buffer's saved reference at publication (retained) and
     * the generation holding its bytes; encode_leaf diffs against it. */
    piece_snapshot *reference;
    editor_base_id reference_id;
    /* edit-fi2: the buffer's retained BASE copy at publication, if READY. */
    journal_base retained;
    bool has_retained;
} session_node;
typedef struct session_capture {
    struct editor_session *owner;
    session_node *root;
    editor_buffer *selected;
    piece_snapshot *snapshot;
    uint64_t active_id, request_ns;
    uint32_t width, height;
    journal_base replacement;
    bool finish, cancelling;
    /* edit-f3i: finish only: the snapshot the save wrote (retained), whose
     * bytes the replacement BASE holds; maps index references per slot. */
    piece_snapshot *reference;
    editor_ref_map *maps;
    editor_records records;
    /* edit-ze59: a plain complete checkpoint (no save; selected is NULL);
     * used records per session slot whether the leaf named its retained copy. */
    bool plain;
    uint8_t *used;
} session_capture;
/* (P) independent per-slot bits: retained BASE versus complete snapshot. */
enum { SESSION_RETAINED = 1u << 0, SESSION_DETACHED = 1u << 1 };
struct editor_session {
    os_mutex mutex;
    session_node *nodes, *free, *root;
    size_t width;
    session_capture capture;
    editor_buffer *saving;
    bool pending, finishing, blocked, reconciling;
    uint64_t pending_submit_ns;
    uint64_t retry_id, retry_ns;
};
static void put64(uint8_t *p, uint64_t value)
{ for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (8u * i)); }
static void base_copy(journal_base *dst, const journal_base *src)
{
    *dst = *src;
    strcpy(dst->captured_path, src->path);
    dst->path = dst->captured_path;
}
/* Node ownership and the freelist share one mutex; no worker reads live UI
 * state. Releasing a replaced root traverses only its unshared copied path. */
static void node_release(editor_session *s, session_node *n)
{
    if (!n || --n->refs) return;
    node_release(s, n->child[0]); node_release(s, n->child[1]);
    if (n->snapshot) piece_snapshot_release(n->snapshot);
    if (n->reference) piece_snapshot_release(n->reference);
    n->next = s->free; s->free = n;
}
static session_node *node_alloc(editor_session *s)
{
    session_node *n = s->free;
    if (!n) return NULL;
    s->free = n->next; memset(n, 0, sizeof *n); n->refs = 1; return n;
}
static session_node *leaf_at(editor_session *s, size_t slot)
{
    session_node *n = s->root;
    for (size_t span = s->width; n && span > 1; span /= 2) {
        size_t half = span / 2;
        bool right = slot >= half; n = n->child[right ? 1 : 0];
        if (right) slot -= half;
    }
    return n;
}
static session_node *replace(editor_session *s, session_node *old, size_t span,
                             size_t slot, session_node *leaf)
{
    if (span == 1) return leaf;
    session_node *n = node_alloc(s); EDIT_ASSERT(n);
    size_t half = span / 2, side = slot >= half ? 1u : 0u;
    if (old) {
        n->child[1u - side] = old->child[1u - side];
        if (n->child[1u - side]) n->child[1u - side]->refs++;
    }
    n->child[side] = replace(s, old ? old->child[side] : NULL, half,
                            side ? slot - half : slot, leaf);
    n->live_count = (n->child[0] ? n->child[0]->live_count : 0) + (n->child[1] ? n->child[1]->live_count : 0);
    return n;
}
int editor_session_init(editor *e)
{
    editor_session *s = calloc(1, sizeof *s); if (!s) return EDITOR_ERR_MEMORY;
    s->width = 1;
    while (s->width < e->buffer_capacity) s->width *= 2;
    /* Each complete binary graph has width*2-1 nodes. Live and pinned roots
     * plus one path under construction fit in three complete graphs (G,
     * structural reservation, derived from binary-tree ownership). */
    size_t count = 3 * (2 * s->width - 1);
    s->nodes = calloc(count, sizeof *s->nodes);
    if (!s->nodes || os_mutex_init(&s->mutex)) { free(s->nodes); free(s); return EDITOR_ERR_MEMORY; }
    for (size_t i = 0; i < count; i++) { s->nodes[i].next = s->free; s->free = &s->nodes[i]; }
    e->session = s; return 0;
}
static void publish(editor *e, editor_buffer *b, bool live, size_t rank, view_state state)
{
    editor_session *s = e->session;
    if (!s || !b->id || !editor_tab_recorded(b) || e->recovering || /* edit-qct; edit-n95; edit-uzrb */
        (b == e->buffer && (e->action == EDITOR_ACTION_REPLAY_WORK ||
         e->action == EDITOR_ACTION_CLIP || e->find_replace_active || e->journal_group_open))) return;
    os_mutex_lock(&s->mutex);
    session_node *old = leaf_at(s, b->session_slot);
    bool content = !old || old->buffer != b || old->revision != b->revision;
    if (!content && old->live == live && old->rank == rank &&
        !memcmp(&old->view, &state, sizeof state) &&
        old->base.inode == b->base.inode && old->base.ctime_ns == b->base.ctime_ns &&
        !strcmp(old->base.path, b->base.path) && old->reference == b->saved_snapshot &&
        old->has_retained == editor_retain_matches(b, &b->base) &&
        (!old->has_retained || old->retained.inode == b->retain.copy.inode)) { os_mutex_unlock(&s->mutex); return; }
    piece_snapshot *snapshot = content ? piece_snapshot_take(b->tree) : piece_snapshot_retain(old->snapshot);
    /* Publication failure never changes a committed mutation. A later turn
     * retries; save refuses stale publication rather than saving old bytes. */
    if (!snapshot) { os_mutex_unlock(&s->mutex); return; }
    session_node *leaf = node_alloc(s); EDIT_ASSERT(leaf);
    leaf->buffer = b; leaf->snapshot = snapshot; leaf->revision = b->revision;
    leaf->view = state; leaf->live = live; leaf->live_count = live ? 1u : 0u; leaf->rank = rank; base_copy(&leaf->base, &b->base);
    leaf->reference = b->saved_snapshot ? piece_snapshot_retain(b->saved_snapshot) : NULL; /* edit-f3i */
    leaf->reference_id = b->saved_text;
    leaf->has_retained = editor_retain_matches(b, &b->base); /* edit-fi2 */
    if (leaf->has_retained) base_copy(&leaf->retained, &b->retain.copy);
    session_node *root = replace(s, s->root, s->width, b->session_slot, leaf);
    node_release(s, s->root); s->root = root;
    os_mutex_unlock(&s->mutex);
}
void editor_session_publish(editor *e, editor_buffer *b)
{
    if (!e->session || !b->id || b->retired) return;
    session_node *old = leaf_at(e->session, b->session_slot);
    /* edit-uzrb: adoption introduces a leaf into the compact live order.
     * Refresh the other ranks too, including when this newly ready tab is
     * inactive. */
    if (!old && editor_tab_recorded(b)) { editor_session_metadata(e); return; }
    publish(e, b, old ? old->live : true, old ? old->rank : editor_recorded_rank(e, tabs_active_index(&e->tabs)), /* edit-3f2k */
            b == e->buffer ? e->v.state : b->initial);
}
void editor_session_forget(editor *e, editor_buffer *b)
{
    editor_session *s = e->session; if (!s) return;
    os_mutex_lock(&s->mutex);
    session_node *root = replace(s, s->root, s->width, b->session_slot, NULL);
    node_release(s, s->root); s->root = root;
    os_mutex_unlock(&s->mutex);
}
bool editor_session_retains(const editor *e, const editor_buffer *b, view_state *state)
{
    editor_session *s = e->session;
    if (!s || !b || s->saving != b) return false;
    if (state) {
        os_mutex_lock(&s->mutex);
        session_node *leaf = leaf_at(s, b->session_slot);
        *state = leaf && leaf->buffer == b ? leaf->view : b->initial;
        os_mutex_unlock(&s->mutex);
    }
    return true;
}
void editor_session_metadata(editor *e)
{
    if (!e->session || e->recovering) return;
    for (size_t closed = 0; closed < 2; closed++) {
        size_t count = closed ? tabs_closed_count(&e->tabs) : tabs_count(&e->tabs);
        /* edit-3f2k: a live leaf's rank is its TABS position, which skips the
         * unpublished placeholder, derived and (edit-uzrb) pending-open tabs. */
        for (size_t i = 0, rank = 0; i < count; i++) {
            const tabs_tab *tab = closed ? tabs_closed_at(&e->tabs, i) : tabs_at(&e->tabs, i);
            editor_buffer *b = (editor_buffer *)((uint8_t *)tab->undo - offsetof(editor_buffer, undo));
            publish(e, b, !closed, closed ? i : rank, b == e->buffer ? e->v.state : tab->state);
            if (editor_tab_recorded(b)) rank++;
        }
    }
    /* The saving buffer keeps its leaf after eviction until its token retires
     * (edit-9u3): journal_save_finish requires the finish checkpoint to carry
     * the saving buffer's BASE. Retirement defers its destruction and slot. */
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        session_node *leaf = b && b->retired ? leaf_at(e->session, b->session_slot) : NULL;
        if (!leaf) continue;
        if (b != e->session->saving) editor_session_forget(e, b);
        else if (leaf->buffer == b && leaf->live) publish(e, b, false, 0, leaf->view);
    }
}
/* edit-f3i: the reference a leaf's text is encoded against, if any. */
static const piece_snapshot *leaf_reference(const session_capture *c, const session_node *n,
                                            const journal_base *base, bool replaced)
{
    const editor_buffer *b = n->buffer;
    if (editor_checkpoint_references(&b->origin, base) || base->size <= FILE_PREFIX_MAX) return NULL;
    /* The replacement BASE holds exactly the bytes of the snapshot the save
     * wrote; other buffers of that target have unrelated trees. */
    if (replaced) return c->finish && b == c->selected ? c->reference : NULL;
    return n->reference && editor_checkpoint_references(&n->reference_id, base) ? n->reference : NULL;
}
static int encode_leaf(session_capture *c, session_node *n)
{
    editor_buffer *b = n->buffer;
    editor_records *r = &c->records;
    const journal_base *base = &n->base;
    journal_base empty = {.path = ""};
    bool detached = c->used && (c->used[b->session_slot] & SESSION_DETACHED);
    if (detached) base = &empty;
    /* All buffers associated with the replaced target must have a valid new
     * BASE. Full snapshot deltas preserve their own (possibly different) text. */
    bool replaced = (c->finish || c->cancelling) && !strcmp(b->save_path, c->selected->save_path);
    if (replaced) base = &c->replacement;
    bool identical = c->finish && b == c->selected && n->revision == b->save_revision &&
        *base->path && piece_snapshot_len(n->snapshot) == base->size;
    /* edit-fi2: as editor_checkpoint, a retained copy is BASE and the real
     * file TARGET. Not for the buffer being prepared (save_prepare checks and
     * substitutes its previous BASE itself), and only while the copy is
     * intact (worker-side check: the UI may since have removed it). */
    bool preparing = !c->plain && !c->finish && !c->cancelling && b == c->selected;
    bool retained = !detached && !replaced && !preparing && !identical && n->has_retained && !journal_check_base(&n->retained);
    if (c->used) c->used[b->session_slot] = (uint8_t)((retained ? SESSION_RETAINED : 0) |
        (detached ? SESSION_DETACHED : 0));
    const journal_base *record = retained ? &n->retained : base;
    size_t base_size = 40 + strlen(record->path) + 1 + (*record->path ? 8u : 0u);
    uint8_t *data = editor_records_add(r, JOURNAL_BASE, b->id, base_size); if (!data) return r->error;
    int rc = journal_encode_base(record, data, base_size, &base_size); if (rc) return rc;
    if (*b->path) {
        data = editor_records_add(r, JOURNAL_TARGET, b->id, strlen(b->path) + 1); if (!data) return r->error;
        strcpy((char *)data, b->path);
    }
    /* edit-28u: the same text encoding as editor_checkpoint. The finish
     * checkpoint of the saved revision is the replacement BASE itself; the
     * prepare checkpoint references the previous BASE when it is the
     * original (save_prepare substitutes a retained copy of those bytes). */
    if (!identical) {
        /* edit-f3i: a text after a save (or saved while this one was in
         * flight) is encoded against the saved revision. */
        const piece_snapshot *reference = leaf_reference(c, n, base, replaced);
        const editor_ref_map *map = NULL;
        if (reference) {
            if (!c->maps && !(c->maps = calloc(c->owner->width, sizeof *c->maps))) return JOURNAL_NOMEM;
            editor_ref_map *slot = &c->maps[b->session_slot];
            if (!slot->spans) { rc = editor_ref_map_build(slot, reference); if (rc) return rc; }
            map = slot;
        }
        piece_iter it; piece_iter_begin_snapshot(&it, n->snapshot, 0);
        editor_records_sink sink = {r, b->id};
        bool original = editor_checkpoint_references(&b->origin, base) ||
            (retained && editor_base_id_matches(&b->origin, base));
        rc = editor_text_ops(&it, base->size, original, map, editor_records_text, &sink);
        if (rc) return rc;
    }
    data = editor_records_add(r, JOURNAL_VIEW, b->id, JOURNAL_VIEW_STATE_BYTES); if (!data) return r->error;
    editor_encode_view(n->view, data);
    return 0;
}
static int encode_tree(session_capture *c, session_node *n, uint64_t *ids, size_t *count, size_t *active)
{
    if (!n) return 0;
    if (!n->buffer) {
        int rc = encode_tree(c, n->child[0], ids, count, active);
        return rc ? rc : encode_tree(c, n->child[1], ids, count, active);
    }
    int rc = encode_leaf(c, n); if (rc) return rc;
    if (n->live) {
        ids[n->rank] = n->buffer->id; (*count)++;
        if (n->buffer->id == c->active_id) *active = n->rank;
    }
    return 0;
}
static int encode_all(session_capture *c, uint64_t *ids)
{
    editor_records *r = &c->records;
    size_t count = 0, active = 0;
    int rc = encode_tree(c, c->root, ids, &count, &active);
    if (!rc) {
        uint8_t *data = editor_records_add(r, JOURNAL_TABS, 0, 16 + count * 8);
        if (!data) rc = r->error;
        else {
            put64(data, count); put64(data + 8, active);
            for (size_t i = 0; i < count; i++) {
                if (!ids[i]) { rc = JOURNAL_INVALID; break; }
                put64(data + 16 + i * 8, ids[i]);
            }
        }
    }
    if (!rc) {
        uint8_t *data = editor_records_add(r, JOURNAL_WINDOW, 0, JOURNAL_WINDOW_BYTES);
        journal_window w = {c->width, c->height, JOURNAL_WINDOW_CHOSEN};
        if (!data) rc = r->error;
        else journal_encode_window(&w, data);
    }
    return rc;
}
/* edit-f3i: the same two-pass builder as editor_checkpoint (one record array
 * and one payload arena, no per-record allocation); both passes walk the
 * pinned, immutable graph. */
static int prepare(void *ctx, const piece_snapshot *snapshot, savectl_checkpoint *out)
{
    session_capture *c = ctx; (void)snapshot;
    uint64_t *ids = calloc(c->owner->width, sizeof *ids); if (!ids) return JOURNAL_NOMEM;
    editor_records_free(&c->records); /* defensive: one prepare per capture */
    int rc = editor_records_begin(&c->records);
    if (!rc) rc = encode_all(c, ids);
    if (!rc) rc = editor_records_fill(&c->records);
    if (!rc) { memset(ids, 0, c->owner->width * sizeof *ids); rc = encode_all(c, ids); }
    if (!rc) rc = editor_records_check(&c->records);
    free(ids);
    if (!rc && c->plain) { out->records = c->records.records; out->count = c->records.count; } /* edit-ze59 */
    else if (!rc) {
        /* Selected previous identity is frozen in the pinned graph. */
        session_node *n = c->root; size_t slot = c->selected->session_slot;
        for (size_t span = c->owner->width; span > 1; span /= 2) {
            size_t half = span / 2; bool right = slot >= half;
            n = n->child[right ? 1 : 0]; if (right) slot -= half;
        }
        out->previous = n->base; out->records = c->records.records; out->count = c->records.count;
    }
    return rc;
}
static void release(void *ctx)
{
    session_capture *c = ctx;
    editor_records_free(&c->records);
    if (c->maps) for (size_t i = 0; i < c->owner->width; i++) editor_ref_map_free(&c->maps[i]);
    free(c->maps); c->maps = NULL;
    if (c->reference) piece_snapshot_release(c->reference);
    c->reference = NULL;
    os_mutex_lock(&c->owner->mutex); node_release(c->owner, c->root);
    c->root = NULL; os_mutex_unlock(&c->owner->mutex);
}
/* edit-ze59 (recovery-3 F10): a complete checkpoint written by a worker that
 * owns the journal (the save controller's lease pattern: editor_save_leased
 * is true until completion, so the UI neither appends, pumps nor drains the
 * journal; input parks behind the lease exactly as behind a save prepare).
 * The records come from the published immutable graph pinned at start, so
 * the checkpoint describes the state of that UI turn; the UI adopts the
 * result in maintenance (editor_publish_poll). Started only while the journal
 * is quiescent (editor_retain_tick), so the worker's flush has nothing to
 * wait for and a stalled journal worker never leases the journal. */
struct editor_publish {
    session_capture capture;
    journal *journal;
    uint8_t *used; /* per session slot: the leaf's BASE was its retained copy */
    file_source **sources; /* edit-7cj8: independent validation of pinned originals */
    int rc;
    _Atomic bool done;
};
static void publish_free(editor_publish *p)
{
    for (size_t i = 0; i < p->capture.owner->width; i++) file_source_release(p->sources[i]);
    free(p->sources); free(p->used); free(p);
}
/* A source lease is private to this read-only checkpoint. A rename-over may
 * change only the detached inode's ctime; accepting that here never grants a
 * save permission to overwrite the external replacement. The journal lease
 * freezes target bindings until this checkpoint is adopted. */
static int publish_validate(editor_publish *p, session_node *n, bool adopt)
{
    if (!n) return 0;
    /* Eviction can leave an internal node with no children. Only published
     * leaves carry a buffer; child presence is not a leaf discriminator. */
    if (!n->buffer) {
        int rc = publish_validate(p, n->child[0], adopt);
        return rc ? rc : publish_validate(p, n->child[1], adopt);
    }
    file_source *source = p->sources[n->buffer->session_slot];
    if (!source) return 0;
    if (!file_source_validate(source)) return 0;
    if (adopt) {
        file_id replacement;
        if (!file_id_stat_path(n->buffer->path, &replacement) &&
            !file_source_adopt_retired(source, &replacement) && !file_source_validate(source)) return 0;
    }
    return JOURNAL_BASE_CHANGED;
}
static void publish_worker(work_ctx *ctx)
{
    editor_publish *p = ctx->arg;
    savectl_checkpoint out = {0};
    int rc = publish_validate(p, p->capture.root, true);
    if (!rc) rc = prepare(&p->capture, NULL, &out);
    if (!rc) rc = publish_validate(p, p->capture.root, false);
    if (!rc) rc = journal_flush(p->journal);
    if (!rc) rc = journal_rotate(p->journal, out.records, out.count);
    p->rc = rc;
    release(&p->capture);
    atomic_store_explicit(&p->done, true, memory_order_release);
    /* Wake-up only: the UI reads done, never touches p before it. */
    work_msg msg = {.kind = EDITOR_PUBLISH_MESSAGE};
    (void)work_publish(ctx, &msg);
}
int editor_publish_start(editor *e)
{
    editor_session *s = e->session;
    if (!s || !e->journal || e->publish) return EDITOR_ERR_ARG;
    editor_session_metadata(e);
    /* Every buffer the checkpoint must name has a leaf of its current
     * revision; otherwise a later turn retries (publication can lag). */
    size_t live = 0;
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *tab = tabs_at(&e->tabs, i);
        const editor_buffer *b = (const editor_buffer *)((const uint8_t *)tab->undo - offsetof(editor_buffer, undo));
        if (!b->placeholder && !b->derived) live++;
    }
    os_mutex_lock(&s->mutex);
    bool fresh = s->root && s->root->live_count == live;
    for (size_t i = 0; fresh && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || b->placeholder || b->derived || b->retired) continue;
        session_node *leaf = leaf_at(s, b->session_slot);
        fresh = !b->pending_open && leaf && leaf->buffer == b && leaf->revision == b->revision;
    }
    if (!fresh) { os_mutex_unlock(&s->mutex); return EDITOR_ERR_MEMORY; }
    editor_publish *p = calloc(1, sizeof *p);
    uint8_t *used = p ? calloc(s->width, sizeof *used) : NULL;
    file_source **sources = used ? calloc(s->width, sizeof *sources) : NULL;
    if (!sources) { os_mutex_unlock(&s->mutex); free(used); free(p); return EDITOR_ERR_MEMORY; }
    s->root->refs++;
    os_mutex_unlock(&s->mutex);
    p->capture = (session_capture){.owner = s, .root = s->root, .plain = true, .used = used,
        .active_id = tabs_active(&e->tabs) ? tabs_active(&e->tabs)->id : 0,
        .width = e->font.window_width, .height = e->font.window_height};
    p->journal = e->journal; p->used = used; p->sources = sources;
    for (size_t i = 0; e->retain_limit && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b || !editor_retain_unprotected(b)) continue;
        used[b->session_slot] = SESSION_DETACHED;
        if (b->file && file_source_acquire(b->file, &sources[b->session_slot])) {
            release(&p->capture); publish_free(p); return EDITOR_ERR_MEMORY;
        }
    }
    atomic_init(&p->done, false);
    work_handle h = work_submit(&e->pool, (work_job){publish_worker, p, 0, WORK_BULK});
    if (!h.epoch) { release(&p->capture); publish_free(p); return EDITOR_ERR_MEMORY; }
    e->publish = p; e->publish_deadline_ns = 0; /* edit-y315: (P) no exit wait yet */
    return 0;
}
bool editor_publish_poll(editor *e, int *rc)
{
    editor_publish *p = e->publish;
    if (!p || !atomic_load_explicit(&p->done, memory_order_acquire)) return false;
    *rc = p->rc;
    e->publish = NULL;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        if (!b) continue;
        b->retain_building = !p->rc && (p->used[b->session_slot] & SESSION_RETAINED);
        if (!p->rc && (p->used[b->session_slot] & SESSION_DETACHED)) {
            b->base = (journal_base){.path = ""};
            editor_session_publish(e, b);
        }
    }
    publish_free(p);
    return true;
}
/* edit-y315: (E) the deadline reuses the journal tick, JOURNAL_DEFAULT_SYNC_NS,
 * that editor_flush_settle and close_result already wait at exit (edit-ivv,
 * edit-dr9); it counts from the first wait, so later exit flushes on the same
 * job return at once. The busy poll is the existing exit-flush loop's. */
int editor_publish_wait(editor *e)
{
    int rc = 0;
    while (e->publish) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        if (editor_publish_poll(e, &rc)) { editor_checkpoint_published(e, rc); return rc; }
        uint64_t now = trace_now_ns();
        if (!e->publish_deadline_ns) e->publish_deadline_ns = now + JOURNAL_DEFAULT_SYNC_NS;
        if (now >= e->publish_deadline_ns) return EDITOR_MORE;
        os_yield();
    }
    return rc;
}
int editor_save_setup(editor *e, editor_buffer *b)
{
    if (b->save || !*b->path || b->pending_open || e->recovering) return 0;
    char path[IPC_PATH_CAP]; file_id baseline = {0}; file_source *source = NULL;
    if (b->file && !b->target_rebound) {
        strcpy(path, file_path(b->file));
        baseline = *file_prefix_identity(b->file);
    } else {
        char parent[IPC_PATH_CAP]; strcpy(parent, b->path);
#if defined(_WIN32)
        for (char *p = parent; *p; p++) if (*p == '\\') *p = '/';
#endif
        char *slash = strrchr(parent, '/'); const char *name = slash ? slash + 1 : b->path;
        char name_copy[IPC_PATH_CAP]; strcpy(name_copy, name);
        bool root = slash == parent;
#if defined(_WIN32)
        if (slash == parent + 2 && parent[1] == ':') root = true;
#endif
        if (root) slash[1] = 0;
        else if (slash) *slash = 0;
        else strcpy(parent, ".");
        char canonical[IPC_PATH_CAP]; if (os_path_canonical(parent, canonical, sizeof canonical) != OS_OK) return EDITOR_ERR_IO;
#if defined(_WIN32)
        for (char *p = canonical; *p; p++) if (*p == '\\') *p = '/';
#endif
        int n = snprintf(path, sizeof path, "%s/%s", canonical, name_copy);
        if (n < 0 || (size_t)n >= sizeof path) return EDITOR_ERR_ARG;
        int rc = file_id_stat_path(path, &baseline); if (rc) return EDITOR_ERR_IO;
    }
    if (b->file && file_open_mode(b->file) == FILE_MODE_MMAP && file_source_acquire(b->file, &source)) return EDITOR_ERR_IO;
    strcpy(b->save_path, path);
    savectl_options options = {.pool = &e->pool, .path = path, .baseline = baseline,
        .source_mode = FILE_MODE_COPY, .source = source, .journal = e->journal,
        .journal_pool = &e->journal_pool, .buffer_id = b->id};
    int rc = savectl_create(&b->save, &options, b->saved_lost || b->history_cursor != b->saved_cursor);
    /* edit-4jhi: a retained BASE authorises source reads, not writes to a
     * distinct TARGET. savectl_create adopts its source's identity, so bind
     * the independently captured destination before any request can run. */
    if (!rc && b->target_rebound) rc = savectl_rebase_target(b->save, &baseline);
    file_source_release(source); return rc ? EDITOR_ERR_IO : 0;
}
bool editor_save_leased(const editor *e)
{
    return e->publish || (e->session && e->session->saving && e->session->saving->save &&
        savectl_get_model(e->session->saving->save).journal_leased);
}
/* edit-ze59: a worker checkpoint (editor_publish_start) owns the journal like
 * a save prepare does, and it and an in-flight Save As write pin buffers like
 * an active save: every journal, retirement and file-command gate that waits
 * for a save also waits for them. */
bool editor_save_active(const editor *e)
{ return (e->session && e->session->saving && !e->session->blocked) || e->publish || e->save_as; }
static void capture(editor *e, editor_buffer *b, uint64_t request_ns, bool finish)
{
    editor_session *s = e->session; session_capture *c = &s->capture;
    *c = (session_capture){.owner = s, .root = s->root, .selected = b,
        .active_id = tabs_active(&e->tabs) ? tabs_active(&e->tabs)->id : 0,
        .request_ns = request_ns, .width = e->font.window_width,
        .height = e->font.window_height, .finish = finish, .cancelling = s->reconciling && !finish};
    os_mutex_lock(&s->mutex); c->root->refs++;
    session_node *leaf = leaf_at(s, b->session_slot);
    /* No leaf: start() refuses the NULL snapshot instead of dereferencing. */
    c->snapshot = leaf && leaf->buffer == b ? piece_snapshot_retain(leaf->snapshot) : NULL;
    os_mutex_unlock(&s->mutex);
    if (finish) {
        base_copy(&c->replacement, savectl_saved_base(b->save));
        /* edit-f3i: the replacement holds the bytes of the snapshot that
         * save request wrote (request_buffer retained it). */
        c->reference = b->save_snapshot ? piece_snapshot_retain(b->save_snapshot) : NULL;
    }
    else if (s->reconciling) c->replacement.path = "";
}
/* edit-f3i: the in-flight save's snapshot lives from request until the save
 * ends; a completed save of a tracked revision moves it to saved_snapshot. */
static void save_snapshot_drop(editor_buffer *b)
{
    if (b->save_snapshot) piece_snapshot_release(b->save_snapshot);
    b->save_snapshot = NULL;
}
static int start(editor *e, bool finish);
/* (P) Zero in retry bookkeeping denotes no pending identity/timestamp/error,
 * following the existing session/controller return-code contract. */
static void start_failed(editor_session *s, editor_buffer *b, int rc)
{
    b->save_error = b->operation_error = rc; b->save_tracking = false;
    if (s->reconciling) { s->blocked = true; s->reconciling = false; s->retry_id = 0; }
    else { s->saving = NULL; save_snapshot_drop(b); }
}
static int request_buffer(editor *e, editor_buffer *b, uint64_t request_ns)
{
    editor_session *s = e->session;
    /* edit-ze59: Ctrl+S waits out an in-flight Save As (it rebinds the
     * controller); the keystroke is ignored, the file command is not. */
    if (e->save_as) return 0;
    if (!s || !b->save || b->pending_open) {
        if (!b->save) b->save_error = b->operation_error = FILE_ERR_STATE;
        return 0;
    }
    if (s->saving) {
        if (!s->blocked) return 0;
        savectl_model model = savectl_get_model(s->saving->save);
        /* Resolve the old slot even when Ctrl+S belongs to another tab.
         * Keep its identity rather than a potentially evicted buffer pointer. */
        s->retry_id = b->id; s->retry_ns = request_ns;
        s->reconciling = true; s->blocked = false; s->pending = true;
        s->pending_submit_ns = 0;
        s->finishing = model.needs_finish;
        capture(e, s->saving, request_ns ? request_ns : trace_now_ns(), model.needs_finish);
        return editor_begin_frame(e);
    }
    if (b->source_stale) return 0;
    session_node *leaf = leaf_at(s, b->session_slot);
    if (!leaf || leaf->buffer != b || leaf->revision != b->revision || s->root->live_count != editor_recorded_rank(e, tabs_count(&e->tabs))) { /* edit-3f2k, edit-uzrb */
        b->save_error = b->operation_error = FILE_ERR_NOMEM; return 0;
    }
    capture(e, b, request_ns ? request_ns : trace_now_ns(), false);
    save_snapshot_drop(b);
    b->save_snapshot = s->capture.snapshot ? piece_snapshot_retain(s->capture.snapshot) : NULL; /* edit-f3i */
    s->saving = b; s->pending = true; s->finishing = s->blocked = false; s->pending_submit_ns = 0;
    b->save_revision = b->revision; b->save_cursor = b->history_cursor; b->save_tracking = true;
    undo_break_burst(&b->undo);
    if (!e->journal_waiting && !e->op_count && !e->journal_group_open && !e->publish) {
        s->pending = false;
        int rc = start(e, false);
        if (rc) start_failed(s, b, rc);
    }
    return editor_begin_frame(e);
}
int editor_save_request(editor *e, uint64_t request_ns)
{ return request_buffer(e, e->buffer, request_ns); }
static int start(editor *e, bool finish)
{
    session_capture *c = &e->session->capture;
    savectl_request request = {c->snapshot, c, prepare, release, c->request_ns};
    int rc = e->session->reconciling ? savectl_recover_request(c->selected->save, &request, finish) :
             finish ? savectl_finish_request(c->selected->save, &request) :
                      savectl_save_request(c->selected->save, &request);
    if (rc) { piece_snapshot_release(c->snapshot); release(c); }
    return rc;
}
void editor_save_tick(editor *e)
{
    editor_session *s = e->session;
    if (!s || !s->saving || s->blocked) return;
    editor_buffer *b = s->saving;
    savectl_tick(b->save); savectl_model model = savectl_get_model(b->save);
    if (s->pending && !e->journal_waiting && !e->op_count && !e->journal_group_open && !model.busy && !e->publish) {
        s->pending = false;
        int rc = start(e, s->capture.finish);
        if (rc) start_failed(s, b, rc);
        else if (s->pending_submit_ns) {
            savectl_model started = savectl_get_model(b->save);
            (void)savectl_status_frame_submitted(b->save, started.request_id, s->pending_submit_ns);
        } else (void)editor_begin_frame(e);
        return;
    }
    if (s->reconciling && !s->pending && !model.busy && !model.needs_finish && !model.can_recover_cancel) {
        /* CURRENT cancellation uses an empty BASE plus full snapshot deltas;
         * verified finish uses the replacement BASE. Neither marks later
         * edits clean. The queued fresh save establishes its own cutoff. */
        for (size_t i = 0; i < e->buffer_capacity; i++) {
            editor_buffer *other = e->buffers[i];
            if (!other || strcmp(other->save_path, b->save_path)) continue;
            base_copy(&other->base, &s->capture.replacement);
            if (s->finishing) {
                if (other->file) (void)file_resolve_keep(other->file);
                other->source_stale = false; other->watch_pending = false;
            }
            editor_session_publish(e, other);
        }
        uint64_t retry_id = s->retry_id, retry_ns = s->retry_ns;
        s->saving = NULL; s->reconciling = false; s->retry_id = 0; save_snapshot_drop(b);
        for (size_t i = 0; i < e->buffer_capacity; i++) {
            editor_buffer *retry = e->buffers[i];
            if (retry && !retry->retired && retry->id == retry_id) {
                int rc = request_buffer(e, retry, retry_ns);
                if (rc) retry->save_error = retry->operation_error = rc;
                break;
            }
        }
        (void)editor_begin_frame(e); e->paint_ready = false; return;
    }
    if (!model.busy && !s->pending && (model.state == SAVECTL_FAILED ||
        model.state == SAVECTL_EXTERNAL_MODIFIED || model.state == SAVECTL_EXTERNAL_UNMODIFIED)) {
        b->save_error = b->operation_error = model.file_error ? model.file_error : model.journal_error;
        b->save_errno = b->operation_errno = model.err_no; b->save_tracking = false;
        /* Retain recovery ownership until a later Ctrl+S reconciles CURRENT. */
        s->blocked = model.can_recover_cancel || model.needs_finish;
        s->reconciling = false; s->retry_id = 0;
        if (!s->blocked) { s->saving = NULL; save_snapshot_drop(b); }
        (void)editor_begin_frame(e); e->paint_ready = false; return;
    }
    if (model.needs_finish && !model.busy && !s->finishing && !e->journal_waiting && !e->op_count && !e->journal_group_open) {
        editor_session_metadata(e);
        capture(e, b, trace_now_ns(), true);
        int rc = start(e, true);
        if (!rc) s->finishing = true;
        else b->save_error = b->operation_error = rc;
        return;
    }
    if (!model.busy && !model.needs_finish && (!model.saving_frame_pending || e->save_flushing) && !s->pending) {
        if (model.state == SAVECTL_SAVED) {
            /* Finish's complete CURRENT graph rebases every shared target.
             * Only the selected saved cutoff acquires a clean history marker. */
            for (size_t i = 0; i < e->buffer_capacity; i++) {
                editor_buffer *other = e->buffers[i];
                if (!other || strcmp(other->save_path, b->save_path)) continue;
                if (s->finishing) base_copy(&other->base, &s->capture.replacement);
                if (other->file) (void)file_resolve_keep(other->file);
                if (other != b) {
                    other->saved_lost = true;
                    if (other->save) (void)savectl_rebase_target(other->save, savectl_target_identity(b->save));
                    for (size_t j = 0; j < tabs_count(&e->tabs); j++)
                        if (tabs_at(&e->tabs, j)->id == other->id) (void)tabs_set_modified(&e->tabs, j, true);
                }
                other->source_stale = false; other->watch_pending = false;
                editor_session_publish(e, other);
            }
            if (b->save_tracking) { b->saved_cursor = b->save_cursor; b->saved_lost = false; }
            else b->saved_lost = true;
            /* edit-28u: the replacement BASE holds exactly the saved
             * revision's snapshot; checkpoints of that revision write BASE only. */
            if (s->finishing && b->save_tracking) {
                b->saved_text = editor_base_id_of(&b->base); b->saved_text_revision = b->save_revision;
                /* edit-f3i: later checkpoints diff against the saved text. */
                editor_saved_reference(b, b->save_snapshot); b->save_snapshot = NULL;
                editor_session_publish(e, b);
            }
            for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
                const tabs_tab *tab = tabs_at(&e->tabs, i);
                if (tab->id == b->id) (void)tabs_set_modified(&e->tabs, i,
                    b->saved_lost || b->history_cursor != b->saved_cursor);
            }
            b->save_error = b->operation_error = 0; b->save_errno = 0;
        } else { b->save_error = b->operation_error = model.file_error ? model.file_error : model.journal_error; b->save_errno = b->operation_errno = model.err_no; }
        b->save_tracking = false; s->saving = NULL; save_snapshot_drop(b);
        (void)editor_begin_frame(e); e->paint_ready = false;
    }
}
/* Status is an editor-owned decoration composed after paint.c. No dependency
 * on another worker's paint implementation, and submission acknowledges exactly
 * the request whose saving text was included in this frame. */
void editor_save_overlay(editor *e)
{
    const char *status = NULL; editor_buffer *b = e->buffer;
    if (e->session && e->session->saving && (!e->session->blocked || e->session->saving == b)) {
        if (e->session->pending) status = "saving";
        else status = savectl_get_model(e->session->saving->save).status;
    } else if (b->save_error && !b->source_stale) status = "save failed";
    /* edit-djg: panel chrome owns its rows; retain the notice for panel close. */
    else if (!e->panel_rows && e->recovery_notice && e->stats.mutations == e->recovery_notice_mutations) {
        status = e->recovery_notice; e->recovery_notice_shown = true; /* edit-btj */
    }
    if (!status || !*status || !e->grid.dims.rows) return;
    size_t len = strlen(status); uint32_t row = e->grid.dims.rows - 1;
    for (uint32_t col = 0; col < e->grid.dims.cols; col++) {
        uint32_t ch = col < len ? (uint8_t)status[col] : (uint32_t)' ';
        render_cell cell = {ch, ch - ' ', 0xe5e9f0, 0x425471, 0, 0};
        e->grid.cells[(size_t)row * e->grid.dims.cols + col] = cell;
    }
    (void)render_mark_rows(&e->grid, row, 1);
}
void editor_save_submitted(editor *e, uint64_t submitted_ns)
{
    if (!e->session || !e->session->saving) return;
    if (e->session->pending) { e->session->pending_submit_ns = submitted_ns; return; }
    savectl *s = e->session->saving->save; savectl_model model = savectl_get_model(s);
    if (model.saving_frame_pending) (void)savectl_status_frame_submitted(s, model.request_id, submitted_ns);
}
void editor_session_close(editor *e)
{
    if (!e->session) return;
    editor_session *s = e->session;
    /* edit-ze59: the worker checkpoint and a Save As write hold graph and
     * buffer leases; both finish before teardown. */
    for (int rc; e->publish && !editor_publish_poll(e, &rc);) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e); os_yield();
    }
    editor_save_as_close(e);
    if (s->pending) { piece_snapshot_release(s->capture.snapshot); release(&s->capture); s->pending = false; }
    /* Off-path teardown retains every controller/arena until physical work and
     * mailbox retirement. Cancel cannot erase ambiguous journal generations. */
    for (size_t i = 0; e->buffers && i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i]; if (!b || !b->save) continue;
        savectl_close_begin(b->save);
        for (;;) {
            savectl_tick(b->save);
            if (savectl_destroy(b->save) == SAVECTL_OK) break;
            (void)work_mailbox_drain(&e->pool, editor_route_work, e);
            os_yield();
        }
        b->save = NULL;
    }
    os_mutex_lock(&s->mutex); node_release(s, s->root); os_mutex_unlock(&s->mutex);
    os_mutex_destroy(&s->mutex); free(s->nodes); free(s); e->session = NULL;
}
