/* Failure recovery at explicit flush/exit only; never on the typing path. */
#include "editor/private.h"
#include <stdlib.h>
#include <string.h>

static size_t recorded_tabs(const editor *e, uint8_t *out);
static void put64(uint8_t *p, uint64_t value)
{ for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (8u * i)); }
void editor_encode_view(view_state state, uint8_t out[JOURNAL_VIEW_STATE_BYTES])
{
    journal_view_state v = {.position = {state.selection.cursor, state.selection.anchor,
        state.first_byte, state.hscroll}, .preferred_col = state.selection.preferred_col,
        .first_line = state.first_line, .visual_byte = state.visual_byte,
        .flags = (state.wrap ? JOURNAL_VIEW_WRAP : 0) |
                 (state.approximate ? JOURNAL_VIEW_APPROXIMATE : 0) |
                 (state.visual_end ? JOURNAL_VIEW_VISUAL_END : 0)};
    journal_encode_view_state(&v, out);
}
editor_base_id editor_base_id_of(const journal_base *base)
{
    return (editor_base_id){base->size, base->mtime_ns, base->inode, base->device, base->ctime_ns,
        base->prefix_crc, base->prefix_len, *base->path != 0};
}
bool editor_base_id_matches(const editor_base_id *id, const journal_base *base)
{
    return id->valid && *base->path && id->size == base->size && id->mtime_ns == base->mtime_ns &&
        id->inode == base->inode && id->device == base->device && id->ctime_ns == base->ctime_ns &&
        id->prefix_crc == base->prefix_crc && id->prefix_len == base->prefix_len;
}
/* edit-28u: a modified text whose BASE fits the file module's synchronous
 * prefix (FILE_PREFIX_MAX) keeps the self-contained legacy shape, so a journal
 * set aside after its BASE changed still holds the whole text for manual
 * replay; larger texts reference the original BASE bytes (decision doc). */
bool editor_checkpoint_references(const editor_base_id *origin, const journal_base *base)
{ return editor_base_id_matches(origin, base) && base->size > FILE_PREFIX_MAX; }
/* edit-f3i: index the reference snapshot's spans by address. */
static int ref_order(const void *a, const void *b)
{
    uintptr_t x = ((const editor_ref_span *)a)->at, y = ((const editor_ref_span *)b)->at;
    return x < y ? -1 : x > y;
}
int editor_ref_map_build(editor_ref_map *m, const piece_snapshot *s)
{
    *m = (editor_ref_map){0};
    size_t capacity = 0; uint64_t off = 0; const uint8_t *p; size_t n;
    piece_iter it; piece_iter_begin_snapshot(&it, s, 0);
    while (piece_iter_next(&it, &p, &n)) {
        uintptr_t at = (uintptr_t)p;
        editor_ref_span *last = m->count ? &m->spans[m->count - 1] : NULL;
        if (last && last->at + last->len == at && last->off + last->len == off) last->len += n;
        else {
            if (m->count == capacity) {
                /* (P) doubling: amortised O(1) per span. */
                size_t grown = capacity ? capacity * 2 : 16;
                editor_ref_span *spans = grown > SIZE_MAX / sizeof *spans ? NULL : realloc(m->spans, grown * sizeof *spans);
                if (!spans) { editor_ref_map_free(m); return JOURNAL_NOMEM; }
                m->spans = spans; capacity = grown;
            }
            m->spans[m->count++] = (editor_ref_span){at, n, off};
        }
        off += n;
    }
    if (m->count > 1) qsort(m->spans, m->count, sizeof *m->spans, ref_order);
    return 0;
}
void editor_ref_map_free(editor_ref_map *m) { free(m->spans); *m = (editor_ref_map){0}; }
void editor_saved_reference(editor_buffer *b, piece_snapshot *s)
{
    if (b->saved_snapshot) piece_snapshot_release(b->saved_snapshot);
    editor_ref_map_free(&b->saved_map);
    b->saved_snapshot = s;
}
/* Reference offset of the n bytes at p: returns how many of them share one
 * answer (>= 1), with *off = PIECE_ADD when they are not reference bytes. */
static size_t ref_locate(const editor_ref_map *m, uintptr_t p, size_t n, uint64_t *off)
{
    size_t lo = 0, hi = m->count;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (m->spans[mid].at <= p) lo = mid + 1; else hi = mid; }
    if (lo) {
        const editor_ref_span *r = &m->spans[lo - 1];
        if (p - r->at < r->len) {
            uint64_t left = r->len - (p - r->at);
            *off = r->off + (p - r->at); return left < n ? (size_t)left : n;
        }
    }
    *off = PIECE_ADD;
    return lo < m->count && m->spans[lo].at - p < n ? (size_t)(m->spans[lo].at - p) : n;
}
/* edit-28u: replay's text before these records is BASE's bytes. The text so
 * far is always out[0, dst) followed by base[src, base_size): a DELETE at dst
 * drops the base gap before the next kept span, an INSERT at dst adds bytes
 * before it, and the trailing DELETE drops the unkept base tail. A span is
 * kept when its BASE offset is known: its ORIGINAL offset (original_is_base)
 * or, edit-f3i, its offset in the reference text BASE holds (ref). Without
 * either the legacy shape results: DELETE all, then INSERT all. */
int editor_text_ops(piece_iter *it, uint64_t base_size, bool original_is_base, const editor_ref_map *ref,
                    editor_text_op op, void *ctx)
{
    uint64_t dst = 0, src = 0, original; const uint8_t *p; size_t n; int rc;
    if (original_is_base) ref = NULL;
    if (!original_is_base && !ref && base_size) {
        rc = op(ctx, JOURNAL_DELETE, 0, base_size, NULL); if (rc) return rc;
        src = base_size;
    }
    while (piece_iter_next_source(it, &p, &n, &original)) {
        while (n) {
            uint64_t at = original_is_base ? original : PIECE_ADD; size_t take = n;
            if (ref) take = ref_locate(ref, (uintptr_t)p, n, &at);
            /* Kept spans arrive in increasing BASE order (piece.h for the
             * original; edits never reorder saved bytes); any other shape
             * is written out as bytes, which is always correct. */
            if (at != PIECE_ADD && at >= src && at <= base_size && take <= base_size - at) {
                if (at > src) { rc = op(ctx, JOURNAL_DELETE, dst, at - src, NULL); if (rc) return rc; }
                src = at + take; dst += take;
            } else for (size_t left = take; left;) {
                size_t slice = left < JOURNAL_INSERT_SLICE_BYTES ? left : JOURNAL_INSERT_SLICE_BYTES;
                rc = op(ctx, JOURNAL_INSERT, dst, slice, p + (take - left)); if (rc) return rc;
                dst += slice; left -= slice;
            }
            p += take; n -= take;
        }
    }
    return src < base_size ? op(ctx, JOURNAL_DELETE, dst, base_size - src, NULL) : 0;
}
/* edit-28u: two passes over the same unmutated state (private.h). */
int editor_records_begin(editor_records *c)
{
    *c = (editor_records){0};
    c->scratch = malloc(JOURNAL_MAX_RECORD); return c->scratch ? 0 : JOURNAL_NOMEM;
}
uint8_t *editor_records_add(editor_records *c, uint32_t type, uint64_t id, size_t size)
{
    if (size > JOURNAL_MAX_RECORD - JOURNAL_HEADER) { c->error = JOURNAL_INVALID; return NULL; }
    if (!c->records) {
        if (c->bytes > SIZE_MAX - size) { c->error = JOURNAL_NOMEM; return NULL; }
        c->count++; c->bytes += size;
        memset(c->scratch, 0, size); return c->scratch;
    }
    if (c->count == c->capacity || size > c->bytes - c->used) { c->error = JOURNAL_INVALID; return NULL; }
    uint8_t *bytes = c->payload + c->used; c->used += size;
    c->records[c->count++] = (journal_record){type, id, 0, bytes, size};
    return bytes;
}
int editor_records_text(void *ctx, uint32_t type, uint64_t off, uint64_t len, const uint8_t *bytes)
{
    editor_records_sink *t = ctx;
    uint8_t *p = editor_records_add(t->records, type, t->id, type == JOURNAL_DELETE ? 16u : (size_t)len + 8u);
    if (!p) return t->records->error;
    put64(p, off);
    if (type == JOURNAL_DELETE) put64(p + 8, len);
    else memcpy(p + 8, bytes, (size_t)len);
    return 0;
}
int editor_records_fill(editor_records *c)
{
    free(c->scratch); c->scratch = NULL;
    if (c->count > SIZE_MAX / sizeof(journal_record)) return JOURNAL_NOMEM;
    if (c->bytes && edit_arena_init(&c->arena, c->bytes)) return JOURNAL_NOMEM;
    c->arena_ready = c->bytes != 0;
    c->payload = c->bytes ? edit_arena_alloc(&c->arena, c->bytes, 1) : NULL;
    c->records = calloc(c->count ? c->count : 1, sizeof *c->records);
    c->capacity = c->count; c->count = 0; c->error = 0;
    return !c->records || (c->bytes && !c->payload) ? JOURNAL_NOMEM : 0;
}
int editor_records_check(const editor_records *c)
{ return c->count != c->capacity || c->used != c->bytes ? JOURNAL_INVALID : 0; }
void editor_records_free(editor_records *c)
{
    free(c->scratch); free(c->records);
    if (c->arena_ready) edit_arena_free(&c->arena);
    *c = (editor_records){0};
}
static int build_checkpoint(editor *e, editor_records *c)
{
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i];
        /* edit-5u2: an evicted buffer whose save is in flight is still
         * recovery state (edit-9u3). Its target may be replaced at any moment
         * and its retained generation belongs to the save token, so it is kept
         * as a closed tab with an empty BASE and its full text, as CURRENT
         * cancellation encodes it: recovery compares that text with the disk. */
        bool kept = b && b->retired;
        if (b) b->retain_building = false;
        if (!b || b->placeholder || b->derived || (kept && !editor_session_retains(e, b, NULL))) continue; /* edit-qct; edit-n95 */
        const journal_base *base = &b->base, empty = {.path = ""};
        if (kept) base = &empty;
        bool identical = !kept && b->revision == b->saved_text_revision &&
            editor_base_id_matches(&b->saved_text, base) && piece_len(b->tree) == base->size;
        /* edit-fi2: a modified text whose BASE has a retained private copy
         * names the copy as BASE (same bytes, immutable) and the real file as
         * TARGET, so recovery no longer depends on the mutable original. */
        bool retained = !kept && !identical && !b->pending_open && editor_retain_matches(b, base);
        const journal_base *record = retained ? &b->retain.copy : base;
        /* Preserve named BASE association. A source conflict must preserve the
         * preceding recovery file, rather than silently relabel the new tree. */
        if (*record->path && journal_check_base(record)) return JOURNAL_BASE_CHANGED;
        size_t path_len = strlen(record->path) + 1;
        size_t base_size = 40 + path_len + (*record->path ? 8u : 0u);
        uint8_t *p = editor_records_add(c, JOURNAL_BASE, b->id, base_size); if (!p) return c->error;
        int rc = journal_encode_base(record, p, base_size, &base_size); if (rc) return rc;
        b->retain_building = retained;
        if (*b->path) {
            p = editor_records_add(c, JOURNAL_TARGET, b->id, strlen(b->path) + 1); if (!p) return c->error;
            memcpy(p, b->path, strlen(b->path) + 1);
        }
        /* Before adoption the tree is empty and preview is only a prefix.
         * BASE already describes the intact file; preserve it without content
         * replacement. An adopted empty tree still represents a deletion.
         * edit-28u: a text equal to BASE (a completed save of this revision,
         * or an untouched original) needs no text records; otherwise only the
         * differences from BASE are written when the original is BASE. */
        if (!b->pending_open && !identical) {
            /* edit-f3i: after a save, BASE is the saved file and the tree's
             * original is not: encode against the saved revision instead.
             * edit-fi2: against an immutable copy, small texts reference it
             * too (28u decision 5 guarded against a mutable BASE). */
            bool original = !kept && (editor_checkpoint_references(&b->origin, base) ||
                (retained && editor_base_id_matches(&b->origin, base)));
            const editor_ref_map *ref = NULL;
            if (!kept && !original && b->saved_snapshot && (editor_checkpoint_references(&b->saved_text, base) ||
                (retained && editor_base_id_matches(&b->saved_text, base)))) {
                if (!b->saved_map.spans) { rc = editor_ref_map_build(&b->saved_map, b->saved_snapshot); if (rc) return rc; }
                ref = &b->saved_map;
            }
            piece_iter it; piece_iter_begin(&it, b->tree, 0);
            editor_records_sink sink = {c, b->id};
            rc = editor_text_ops(&it, base->size, original, ref, editor_records_text, &sink);
            if (rc) return rc;
        }
        view_state state = b == e->buffer ? e->v.state : b->initial;
        if (kept) (void)editor_session_retains(e, b, &state);
        for (size_t j = 0; j < tabs_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_at(&e->tabs, j);
            if (tab->id == b->id && b != e->buffer) state = tab->state;
        }
        for (size_t j = 0; j < tabs_closed_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_closed_at(&e->tabs, j);
            if (tab->id == b->id) state = tab->state;
        }
        p = editor_records_add(c, JOURNAL_VIEW, b->id, JOURNAL_VIEW_STATE_BYTES); if (!p) return c->error;
        editor_encode_view(state, p);
    }
    size_t tabs = tabs_count(&e->tabs);
    if (tabs > (JOURNAL_MAX_RECORD - 16) / 8) return JOURNAL_INVALID;
    uint8_t *p = editor_records_add(c, JOURNAL_TABS, 0, 16 + recorded_tabs(e, NULL) * 8); if (!p) return c->error;
    (void)recorded_tabs(e, p);
    p = editor_records_add(c, JOURNAL_WINDOW, 0, JOURNAL_WINDOW_BYTES); if (!p) return c->error;
    journal_encode_window(&(journal_window){e->font.window_width, e->font.window_height, JOURNAL_WINDOW_CHOSEN}, p);
    return 0;
}
/* edit-qct: TABS lists every live tab except placeholders, whose IDs have no
 * BASE in this journal (recovery would reject the session). With out, write
 * the record payload; returns the recorded count. */
static size_t recorded_tabs(const editor *e, uint8_t *out)
{
    size_t count = 0, active = 0, live = tabs_count(&e->tabs), current = tabs_active_index(&e->tabs);
    for (size_t i = 0; i < live; i++) {
        const tabs_tab *tab = tabs_at(&e->tabs, i);
        const editor_buffer *b = (const editor_buffer *)((const uint8_t *)tab->undo - offsetof(editor_buffer, undo));
        if (b->placeholder || b->derived) continue; /* edit-n95: Find Results is derived */
        if (i == current) active = count;
        if (out) put64(out + 16 + count * 8, tab->id);
        count++;
    }
    if (out) { put64(out, count); put64(out + 8, active); }
    return count;
}
int editor_checkpoint(editor *e)
{
    editor_records c;
    int rc = editor_records_begin(&c);
    if (!rc) rc = build_checkpoint(e, &c);
    if (!rc) rc = editor_records_fill(&c);
    if (!rc) rc = build_checkpoint(e, &c);
    if (!rc) rc = editor_records_check(&c);
    if (!rc) rc = journal_rotate(e->journal, c.records, c.count);
    if (!rc) {
        e->op_count = e->stage_used = 0; e->stats.journal_error = 0;
        e->journal_group_open = false;
        editor_retain_checkpointed(e); /* edit-fi2 */
    }
    editor_records_free(&c);
    return rc;
}

/* Coalesce metadata against the last accepted record. Reserved encoding storage
 * makes this path allocation-free. A refused record remains pending until a
 * durability fence returns credit; never publish a newer containing frame. */
int editor_journal_metadata(editor *e)
{
    editor_session_metadata(e);
    if (!e->journal || e->recovering) return 0;
    /* No BASE has been installed before tabs' first ID is consumed. Preserve
     * the empty-placeholder contract on failed startup; a last-tab close has
     * consumed an ID and must still publish the empty live order. */
    if (e->tabs.next_id == 1 + e->recovery_placeholder_ids && !recorded_tabs(e, NULL)) return 0; /* edit-qct: placeholder IDs are not tabs */
    if (editor_save_leased(e)) return 0; /* save checkpoint owns the journal fence */
    if (e->op_count) {
        e->journal_metadata_pending = true; return JOURNAL_BUSY;
    }
    e->journal_metadata_pending = true;
    for (size_t closed = 0; closed < 2; closed++) {
        size_t count = closed ? tabs_closed_count(&e->tabs) : tabs_count(&e->tabs);
        for (size_t i = 0; i < count; i++) {
            const tabs_tab *tab = closed ? tabs_closed_at(&e->tabs, i) : tabs_at(&e->tabs, i);
            /* Every editor tab borrows the embedded buffer-owned undo log.
             * Recover that owner in constant work, avoiding a buffer-table
             * search for each metadata record. Tab leases preserve its arena. */
            if (!tab->undo) return JOURNAL_INVALID;
            editor_buffer *b = (editor_buffer *)((uint8_t *)tab->undo - offsetof(editor_buffer, undo));
            if (b->id != tab->id) return JOURNAL_INVALID;
            if (b->placeholder || b->derived) continue; /* edit-qct; edit-n95 */
            view_state state = b == e->buffer ? e->v.state : tab->state;
            uint8_t data[JOURNAL_VIEW_STATE_BYTES]; editor_encode_view(state, data);
            journal_record record_view = {JOURNAL_VIEW, b->id, 0, data, sizeof data};
            journal_view_state v;
            int rc = journal_decode_view_state(&record_view, &v); if (rc) return rc;
            if (!b->journal_view_valid || memcmp(&v, &b->journal_view_state, sizeof v)) {
                rc = journal_append_protected(e->journal, JOURNAL_VIEW, b->id, data, sizeof data);
                if (rc) return rc;
                b->journal_view_state = v; b->journal_view_valid = true;
            }
        }
    }
    uint8_t *data = e->journal_tabs;
    size_t count = recorded_tabs(e, data), size = 16 + count * 8;
    if (size != e->journal_tabs_size || memcmp(data, e->journal_tabs_cached, size)) {
        int rc = journal_append_protected(e->journal, JOURNAL_TABS, 0, data, size); if (rc) return rc;
        memcpy(e->journal_tabs_cached, data, size); e->journal_tabs_size = size;
    }
    uint32_t width = e->font.window_width, height = e->font.window_height;
    if (width != e->journal_width || height != e->journal_height) {
        uint8_t window[JOURNAL_WINDOW_BYTES];
        journal_encode_window(&(journal_window){width, height, JOURNAL_WINDOW_CHOSEN}, window);
        int rc = journal_append_protected(e->journal, JOURNAL_WINDOW, 0, window, sizeof window); if (rc) return rc;
        e->journal_width = width; e->journal_height = height;
    }
    e->journal_metadata_pending = false;
    return 0;
}

int editor_journal_changed(editor *e)
{
    if (!e->journal) { editor_session_publish(e, e->buffer); return 0; }
    int rc = editor_journal_metadata(e);
    if (rc == JOURNAL_BUSY) return 0; /* retained for the next credit fence */
    if (rc) { e->stats.journal_error = rc; return EDITOR_ERR_IO; }
    return 0;
}
