#include "editor/editor.h"
#include "base/base.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <string.h>

#define MODEL_BYTES 4096u
#define MODEL_STEPS 256u
typedef struct model { uint8_t bytes[MODEL_BYTES]; size_t len, cursor, anchor, preferred; } model;
typedef struct history { model before, after; } history;
static void settle(editor *e)
{
    for (unsigned i = 0; i < 10000; i++) {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) fprintf(stderr, "editor_fuzz loop error rc=%d sequence=%llu cursor=%llu\n", rc,
            (unsigned long long)editor_get_stats(e).input_sequence, (unsigned long long)editor_view(e).selection.cursor);
        EDIT_ASSERT(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return;
    }
    EDIT_ASSERT(false);
}
static size_t lo(const model *m) { return m->cursor < m->anchor ? m->cursor : m->anchor; }
static size_t hi(const model *m) { return m->cursor > m->anchor ? m->cursor : m->anchor; }
static size_t ceiling(const model *m, size_t target)
{
    size_t pos = 0;
    while (pos < target) pos += utf8_grapheme_next(m->bytes + pos, m->len - pos);
    return pos;
}
static size_t previous(const model *m, size_t at)
{
    size_t pos = 0, last = 0;
    while (pos < at) { last = pos; pos += utf8_grapheme_next(m->bytes + pos, m->len - pos); }
    return last;
}
static size_t next(const model *m, size_t at)
{
    return at < m->len ? at + utf8_grapheme_next(m->bytes + at, m->len - at) : at;
}
static void replace(model *m, size_t at, size_t n, const uint8_t *p, size_t add)
{
    EDIT_ASSERT(at <= m->len && n <= m->len - at && m->len - n + add <= MODEL_BYTES);
    memmove(m->bytes + at + add, m->bytes + at + n, m->len - at - n);
    if (add) memcpy(m->bytes + at, p, add);
    m->len = m->len - n + add; m->cursor = m->anchor = ceiling(m, at + add); m->preferred = SIZE_MAX;
}
static size_t line_start(const model *m, size_t at)
{
    while (at && m->bytes[at - 1] != '\n') at--; return at;
}
static size_t line_end(const model *m, size_t at)
{
    while (at < m->len && m->bytes[at] != '\n') at++; return at;
}
static size_t column_at(const model *m, size_t at)
{
    size_t pos = line_start(m, at), col = 0;
    while (pos < at) { size_t n = utf8_grapheme_next(m->bytes + pos, m->len - pos);
        int w = utf8_cluster_width(m->bytes + pos, n); pos += n; col += (size_t)w; }
    return col;
}
static size_t at_column(const model *m, size_t start, size_t col)
{
    size_t pos = start, cells = 0, end = line_end(m, start);
    while (pos < end) {
        if (cells >= col) break;
        size_t n = utf8_grapheme_next(m->bytes + pos, end - pos);
        int w = utf8_cluster_width(m->bytes + pos, n);
        if ((size_t)w > col - cells) break;
        cells += (size_t)w; pos += n;
    }
    return pos;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > MODEL_STEPS * 4u) size = MODEL_STEPS * 4u;
    render_backend b = {0}; EDIT_ASSERT(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = (const uint8_t *)"ab\ncd", .initial_len = 5,
        .cols = 32, .rows = 8, .max_cols = 40, .max_rows = 12, .history_keys = MODEL_STEPS, .arena_bytes = 4u * 1024u * 1024u};
    editor *e = NULL; EDIT_ASSERT(editor_open(&e, &cfg, &b) == 0); settle(e);
    model m = {.bytes = {'a','b','\n','c','d'}, .len = 5, .preferred = SIZE_MAX};
    history hist[MODEL_STEPS]; size_t nh = 0, applied = 0;
    for (size_t i = 0; i + 3 < size; i += 4) {
        model before = m; bool mutation = false, shift = (data[i + 1] & 1u) != 0;
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
        switch (data[i] % 16u) {
        case 0: case 1: case 2: {
            const char *texts[] = {"a", "x", "中", "e\xcc\x81", "\xcc\x81", "👩", "\xe2\x80\x8d", "\xff"};
            const char *text = texts[data[i + 2] % 8u]; size_t n = strlen(text);
            ev.keysym = 'a'; memcpy(ev.utf8, text, n); ev.utf8_len = (uint8_t)n;
            replace(&m, lo(&m), hi(&m) - lo(&m), (const uint8_t *)text, n); mutation = true; break;
        }
        case 3: {
            uint8_t ch = '\n'; ev.keysym = XKB_KEY_Return;
            replace(&m, lo(&m), hi(&m) - lo(&m), &ch, 1); mutation = true; break;
        }
        case 4: case 5: {
            bool back = data[i] % 16u == 4; ev.keysym = back ? XKB_KEY_BackSpace : XKB_KEY_Delete;
            size_t a = lo(&m), z = hi(&m);
            if (a == z) { if (back && a) a = previous(&m, a); else if (!back && z < m.len) z = next(&m, z); }
            if (z > a) { replace(&m, a, z - a, NULL, 0); mutation = true; }
            break;
        }
        case 6: case 7: {
            bool left = data[i] % 16u == 6; ev.keysym = left ? XKB_KEY_Left : XKB_KEY_Right;
            ev.mods = shift ? PLAT_MOD_SHIFT : 0;
            if (!shift && m.cursor != m.anchor) m.cursor = left ? lo(&m) : hi(&m);
            else if (left && m.cursor) m.cursor = previous(&m, m.cursor);
            else if (!left && m.cursor < m.len) m.cursor = next(&m, m.cursor);
            if (!shift) m.anchor = m.cursor; m.preferred = SIZE_MAX; break;
        }
        case 8: case 9: {
            bool up = data[i] % 16u == 8; ev.keysym = up ? XKB_KEY_Up : XKB_KEY_Down;
            ev.mods = shift ? PLAT_MOD_SHIFT : 0;
            if (m.preferred == SIZE_MAX) m.preferred = column_at(&m, m.cursor);
            size_t start = line_start(&m, m.cursor), end = line_end(&m, m.cursor);
            if (up) m.cursor = start ? at_column(&m, line_start(&m, start - 1), m.preferred) : 0;
            else m.cursor = end < m.len ? at_column(&m, end + 1, m.preferred) : m.len;
            if (!shift) m.anchor = m.cursor; break;
        }
        case 10: case 11: {
            bool redo = data[i] % 16u == 11;
            ev.keysym = redo ? XKB_KEY_Z : XKB_KEY_z;
            ev.mods = (uint16_t)(PLAT_MOD_CTRL | (redo ? PLAT_MOD_SHIFT : 0));
            if (redo && applied < nh) { m = hist[applied++].after; m.preferred = SIZE_MAX; }
            if (!redo && applied) { m = hist[--applied].before; m.preferred = SIZE_MAX; }
            break;
        }
        case 12: ev.kind = PLAT_EV_FOCUS; ev.focused = shift; break;
        case 13: ev.kind = PLAT_EV_RESIZE; ev.w = 160u + data[i + 2]; ev.h = 64u + data[i + 3]; break;
        case 14: ev.kind = PLAT_EV_EXPOSE; break;
        default: ev.press = false; ev.keysym = 'x'; ev.utf8_len = 1; ev.utf8[0] = 'x'; break;
        }
        if (mutation) { nh = applied; hist[nh++] = (history){before, m}; applied = nh; }
        EDIT_ASSERT(editor_inject(e, &ev) == 0); settle(e);
        uint8_t actual[MODEL_BYTES]; EDIT_ASSERT(editor_length(e) == m.len);
        EDIT_ASSERT(editor_read(e, 0, actual, m.len) == 0 && !memcmp(actual, m.bytes, m.len));
        view_selection s = editor_view(e).selection;
        if (s.cursor != m.cursor || s.anchor != m.anchor) {
            fprintf(stderr, "editor_fuzz step=%zu op=%u sym=%u got=%llu/%llu model=%zu/%zu len=%zu\n", i / 4, data[i] % 16u, ev.keysym,
                (unsigned long long)s.cursor, (unsigned long long)s.anchor, m.cursor, m.anchor, m.len);
            for (size_t j = 0; j < m.len; j++) fprintf(stderr, "%02x", m.bytes[j]);
            fprintf(stderr, " before=%zu/%zu preferred=%zu actual_preferred=%llu\n", before.cursor, before.anchor, before.preferred,
                (unsigned long long)s.preferred_col);
        }
        EDIT_ASSERT(s.cursor == m.cursor && s.anchor == m.anchor);
    }
    editor_close(e); return 0;
}
