/* Failure recovery at explicit flush/exit only; never on the typing path. */
#include "editor/private.h"
#include <stdlib.h>
#include <string.h>

static void put64(uint8_t *p, uint64_t value)
{ for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (8u * i)); }
static void put32(uint8_t *p, uint32_t value)
{ for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8u * i)); }
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
        editor_buffer *b = e->buffers[i]; if (!b) continue;
        uint64_t len = piece_len(b->tree), chunks = len / 4096u + (len % 4096u != 0 ? 1u : 0u);
        if (chunks > SIZE_MAX - capacity - 3u) return JOURNAL_NOMEM;
        capacity += (size_t)chunks + 3u;
    }
    if (capacity > SIZE_MAX / sizeof(journal_record)) return JOURNAL_NOMEM;
    journal_record *records = calloc(capacity, sizeof *records);
    if (!records) return JOURNAL_NOMEM;
    size_t count = 0; int rc = JOURNAL_NOMEM;
    for (size_t i = 0; i < e->buffer_capacity; i++) {
        editor_buffer *b = e->buffers[i]; if (!b) continue;
        /* Preserve named BASE association. A source conflict must preserve the
         * preceding recovery file, rather than silently relabel the new tree. */
        if (*b->base.path && journal_check_base(&b->base)) { rc = JOURNAL_BASE_CHANGED; goto done; }
        size_t path_len = strlen(b->base.path) + 1;
        uint8_t *p = record(records, &count, JOURNAL_BASE, b->id, 40 + path_len); if (!p) goto done;
        put64(p, b->base.size); put64(p + 8, b->base.mtime_ns); put64(p + 16, b->base.inode); put64(p + 24, b->base.device);
        put32(p + 32, b->base.prefix_crc); put32(p + 36, b->base.prefix_len); memcpy(p + 40, b->base.path, path_len);
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
        view_state state = b == e->buffer ? e->v.state : b->initial;
        for (size_t j = 0; j < tabs_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_at(&e->tabs, j);
            if (tab->id == b->id && b != e->buffer) state = tab->state;
        }
        for (size_t j = 0; j < tabs_closed_count(&e->tabs); j++) {
            const tabs_tab *tab = tabs_closed_at(&e->tabs, j);
            if (tab->id == b->id) state = tab->state;
        }
        p = record(records, &count, JOURNAL_VIEW, b->id, 32); if (!p) goto done;
        put64(p, state.selection.cursor); put64(p + 8, state.selection.anchor);
        put64(p + 16, state.first_byte); put64(p + 24, state.hscroll);
    }
    size_t tabs = tabs_count(&e->tabs);
    uint8_t *p = record(records, &count, JOURNAL_TABS, 0, 16 + tabs * 8); if (!p) goto done;
    put64(p, tabs); put64(p + 8, tabs ? tabs_active_index(&e->tabs) : 0);
    for (size_t i = 0; i < tabs; i++) put64(p + 16 + i * 8, tabs_at(&e->tabs, i)->id);
    p = record(records, &count, JOURNAL_WINDOW, 0, 8); if (!p) goto done;
    put32(p, e->grid.dims.cols * e->grid.dims.cell_w); put32(p + 4, e->grid.dims.rows * e->grid.dims.cell_h);
    rc = journal_rotate(e->journal, records, count);
    if (!rc) { e->op_count = e->stage_used = 0; e->stats.journal_error = 0; }
done:
    for (size_t i = 0; i < count; i++) free((void *)records[i].data);
    free(records); return rc;
}
