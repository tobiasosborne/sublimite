/* Failure recovery at explicit flush/exit only; never on the typing path. */
#include "editor/private.h"
#include <stdlib.h>
#include <string.h>

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
static uint8_t *record(journal_record *records, size_t *count, uint32_t type, uint64_t id, size_t size)
{
    uint8_t *bytes = calloc(1, size);
    if (bytes) records[(*count)++] = (journal_record){type, id, 0, bytes, size};
    return bytes;
}
int editor_checkpoint(editor *e)
{
    size_t capacity = 2;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i]; if (!b || b->retired) continue;
        uint64_t len = piece_len(b->tree), chunks = len / 4096u + (len % 4096u != 0 ? 1u : 0u);
        if (chunks > SIZE_MAX - capacity - 4u) return JOURNAL_NOMEM;
        capacity += (size_t)chunks + 4u;
    }
    if (capacity > SIZE_MAX / sizeof(journal_record)) return JOURNAL_NOMEM;
    journal_record *records = calloc(capacity, sizeof *records);
    if (!records) return JOURNAL_NOMEM;
    size_t count = 0; int rc = JOURNAL_NOMEM;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i]; if (!b || b->retired) continue;
        /* Preserve named BASE association. A source conflict must preserve the
         * preceding recovery file, rather than silently relabel the new tree. */
        if (*b->base.path && journal_check_base(&b->base)) { rc = JOURNAL_BASE_CHANGED; goto done; }
        size_t path_len = strlen(b->base.path) + 1;
        size_t base_size = 40 + path_len + (*b->base.path ? 8u : 0u);
        uint8_t *p = record(records, &count, JOURNAL_BASE, b->id, base_size); if (!p) goto done;
        rc = journal_encode_base(&b->base, p, base_size, &base_size); if (rc) goto done;
        rc = JOURNAL_NOMEM;
        if (*b->path) {
            p = record(records, &count, JOURNAL_TARGET, b->id, strlen(b->path) + 1); if (!p) goto done;
            memcpy(p, b->path, strlen(b->path) + 1);
        }
        /* Before adoption the tree is empty and preview is only a prefix.
         * BASE already describes the intact file; preserve it without content
         * replacement. An adopted empty tree still represents a deletion. */
        if (!b->pending_open) {
            if (b->base.size) {
                p = record(records, &count, JOURNAL_DELETE, b->id, 16); if (!p) goto done;
                put64(p + 8, b->base.size);
            }
            uint64_t len = piece_len(b->tree);
            for (uint64_t off = 0; off < len;) {
                size_t n = len - off < 4096u ? (size_t)(len - off) : 4096u;
                p = record(records, &count, JOURNAL_INSERT, b->id, n + 8); if (!p) goto done;
                put64(p, off); rc = piece_read(b->tree, off, p + 8, n); if (rc) goto done;
                rc = JOURNAL_NOMEM; off += n;
            }
        }
        view_state state = b == e->buffer ? e->v.state : b->initial;
        for (size_t j = 0; j < tabs_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_at(&e->tabs, j);
            if (tab->id == b->id && b != e->buffer) state = tab->state;
        }
        for (size_t j = 0; j < tabs_closed_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_closed_at(&e->tabs, j);
            if (tab->id == b->id) state = tab->state;
        }
        p = record(records, &count, JOURNAL_VIEW, b->id, JOURNAL_VIEW_STATE_BYTES); if (!p) goto done;
        editor_encode_view(state, p);
    }
    size_t tabs = tabs_count(&e->tabs);
    uint8_t *p = record(records, &count, JOURNAL_TABS, 0, 16 + tabs * 8); if (!p) goto done;
    put64(p, tabs); put64(p + 8, tabs ? tabs_active_index(&e->tabs) : 0);
    for (size_t i = 0; i < tabs; i++) put64(p + 16 + i * 8, tabs_at(&e->tabs, i)->id);
    p = record(records, &count, JOURNAL_WINDOW, 0, JOURNAL_WINDOW_BYTES); if (!p) goto done;
    journal_encode_window(&(journal_window){e->font.window_width, e->font.window_height, JOURNAL_WINDOW_CHOSEN}, p);
    rc = journal_rotate(e->journal, records, count);
    if (!rc) {
        e->op_count = e->stage_used = 0; e->stats.journal_error = 0;
        e->journal_group_open = false;
    }
done:
    for (size_t i = 0; i < count; i++) free((void *)records[i].data);
    free(records); return rc;
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
    if (e->tabs.next_id == 1 && !tabs_count(&e->tabs)) return 0;
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
    size_t count = tabs_count(&e->tabs), size = 16 + count * 8;
    uint8_t *data = e->journal_tabs;
    put64(data, count); put64(data + 8, count ? tabs_active_index(&e->tabs) : 0);
    for (size_t i = 0; i < count; i++) put64(data + 16 + i * 8, tabs_at(&e->tabs, i)->id);
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
