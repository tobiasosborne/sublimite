#include "view/view.h"
#include "layout/layout.h"
#include "find/find.h"
#include <string.h>

static uint64_t lower(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t upper(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint8_t byte(const view *v, uint64_t at)
{
    uint8_t ch = 0;
    if (at < piece_len(v->tree)) (void)piece_read(v->tree, at, &ch, sizeof ch);
    return ch;
}
static int begin(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    if (p) memset(p, 0, sizeof *p);
    if (!v || !v->tree || !p || (!out && cap) || v->busy ||
        v->state.selection.cursor > piece_len(v->tree) ||
        v->state.selection.anchor > piece_len(v->tree)) return VIEW_ERR_ARG;
    p->selection = v->state.selection;
    p->selection.preferred_col = VIEW_PREFERRED_UNSET;
    return 0;
}
/* Seek locally, never count the prefix of a large document. The read chunk
 * and aggregate boundary limit both derive from the existing view window. */
static int line_start(const view *v, uint64_t at, uint64_t *start)
{
    uint8_t buf[VIEW_WINDOW]; size_t left = VIEW_SCAN_BOUND;
    while (at && left) {
        size_t n = (size_t)lower(at, lower(left, sizeof buf));
        at -= n; left -= n;
        int rc = piece_read(v->tree, at, buf, n); if (rc) return rc;
        for (size_t i = n; i; i--) if (buf[i - 1u] == '\n') { *start = at + i; return 0; }
    }
    if (at) return PIECE_ERR_NOMEM;
    *start = 0; return 0;
}
static int line_stop(const view *v, uint64_t at, uint64_t *stop)
{
    uint8_t buf[VIEW_WINDOW]; size_t left = VIEW_SCAN_BOUND;
    uint64_t total = piece_len(v->tree);
    while (at < total && left) {
        size_t n = (size_t)lower(total - at, lower(left, sizeof buf)); left -= n;
        int rc = piece_read(v->tree, at, buf, n); if (rc) return rc;
        for (size_t i = 0; i < n; i++) if (buf[i] == '\n') { *stop = at + i + 1u; return 0; }
        at += n;
    }
    if (at < total) return PIECE_ERR_NOMEM;
    *stop = at; return 0;
}
static uint64_t content_end(const view *v, uint64_t start, uint64_t stop)
{
    if (stop > start && byte(v, stop - 1u) == '\n') {
        stop--; if (stop > start && byte(v, stop - 1u) == '\r') stop--;
    }
    return stop;
}
static size_t newline_bytes(const view *v, uint64_t lo, uint64_t end, uint64_t hi, uint8_t *out)
{
    bool crlf = hi - end == sizeof "\r\n" - 1u ||
        (end == hi && lo >= sizeof "\r\n" - 1u && byte(v, lo - 2u) == '\r');
    size_t n = 0;
    if (crlf) out[n++] = '\r';
    out[n++] = '\n'; return n;
}
static int lines(const view *v, uint64_t *lo, uint64_t *hi)
{
    uint64_t a = lower(v->state.selection.cursor, v->state.selection.anchor);
    uint64_t b = upper(v->state.selection.cursor, v->state.selection.anchor);
    if (b > a && byte(v, b - 1u) == '\n') b--;
    int rc = line_start(v, a, lo);
    return rc ? rc : line_stop(v, b, hi);
}
static int copy(const view *v, uint64_t lo, uint64_t hi, uint8_t *out, size_t cap, size_t *n)
{
    if (hi - lo > cap || hi - lo > VIEW_WINDOW) return PIECE_ERR_NOMEM;
    *n = (size_t)(hi - lo);
    if (!*n) return 0;
    return piece_read(v->tree, lo, out, *n);
}
static void caret(view_edit_plan *p, uint64_t at)
{ p->selection.cursor = p->selection.anchor = at; }
int view_duplicate_line(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t a = lower(p->selection.cursor, p->selection.anchor);
    uint64_t b = upper(p->selection.cursor, p->selection.anchor);
    bool selected = a != b;
    if (!selected) { rc = lines(v, &a, &b); if (rc) return rc; }
    rc = copy(v, a, b, out, cap, &p->length); if (rc) return rc;
    p->offset = b;
    if (!selected && (b == a || byte(v, b - 1u) != '\n')) {
        uint8_t newline[sizeof "\r\n" - 1u];
        size_t eol = newline_bytes(v, a, b, b, newline);
        if (eol > cap - p->length) return PIECE_ERR_NOMEM;
        memmove(out + eol, out, p->length); memcpy(out, newline, eol); p->length += eol;
    }
    uint64_t delta = selected ? b - a : p->length;
    p->selection.cursor += delta; p->selection.anchor += delta;
    return 0;
}
int view_delete_line(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t hi; rc = lines(v, &p->offset, &hi); if (rc) return rc;
    p->old_len = hi - p->offset; caret(p, p->offset); return 0;
}
static int move_line(const view *v, bool up, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo, hi; rc = lines(v, &lo, &hi); if (rc) return rc;
    uint64_t start = lo, stop = hi, mid;
    if (up) {
        if (!lo) return 0;
        rc = line_start(v, lo - 1u, &start); mid = lo;
    } else {
        if (hi == piece_len(v->tree) && (hi == lo || byte(v, hi - 1u) != '\n')) return 0;
        rc = line_stop(v, hi, &stop); mid = hi;
    }
    if (rc) return rc;
    uint64_t first_end = content_end(v, start, mid), second_end = content_end(v, mid, stop);
    uint64_t first_len = first_end - start, second_len = second_end - mid;
    size_t separator = (size_t)(mid - first_end), tail = (size_t)(stop - second_end);
    /* Newlines separate positions, not line contents: preserve a missing EOF
     * terminator when the last logical line swaps with its predecessor. */
    if (stop - start > cap || stop - start > VIEW_WINDOW) return PIECE_ERR_NOMEM;
    rc = piece_read(v->tree, mid, out, (size_t)second_len); if (rc) return rc;
    rc = piece_read(v->tree, first_end, out + second_len, separator); if (rc) return rc;
    rc = piece_read(v->tree, start, out + second_len + separator, (size_t)first_len); if (rc) return rc;
    rc = piece_read(v->tree, second_end, out + second_len + separator + first_len, tail); if (rc) return rc;
    p->offset = start; p->old_len = stop - start; p->length = (size_t)p->old_len;
    uint64_t target = up ? start : start + second_len + separator;
    p->selection.cursor = lower(target + p->selection.cursor - lo, stop);
    p->selection.anchor = lower(target + p->selection.anchor - lo, stop);
    return 0;
}
int view_move_line_up(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return move_line(v, true, out, cap, p); }
int view_move_line_down(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return move_line(v, false, out, cap, p); }
int view_join_lines(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo, hi; rc = lines(v, &lo, &hi); if (rc) return rc;
    uint64_t first; rc = line_stop(v, lo, &first); if (rc) return rc;
    if (hi == first) { rc = line_stop(v, hi, &hi); if (rc) return rc; }
    hi = content_end(v, lo, hi);
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t written = 0, seam = 0; uint64_t c = 0, a = 0; bool joined = false;
    for (size_t i = 0; i < n; i++) {
        if (lo + i == p->selection.cursor) c = lo + written;
        if (lo + i == p->selection.anchor) a = lo + written;
        if (src[i] == '\n' || (src[i] == '\r' && i + 1u < n && src[i + 1u] == '\n')) {
            size_t removed_start = i;
            while (written && (out[written - 1u] == ' ' || out[written - 1u] == '\t')) { written--; removed_start--; }
            if (src[i] == '\r') i++;
            while (i + 1u < n && (src[i + 1u] == ' ' || src[i + 1u] == '\t')) i++;
            if (written && i + 1u < n) { if (written == cap) return PIECE_ERR_NOMEM; out[written++] = ' '; }
            if (!joined) seam = written;
            joined = true;
            if (p->selection.cursor >= lo + removed_start && p->selection.cursor <= lo + i + 1u) c = lo + written;
            if (p->selection.anchor >= lo + removed_start && p->selection.anchor <= lo + i + 1u) a = lo + written;
        } else { if (written == cap) return PIECE_ERR_NOMEM; out[written++] = src[i]; }
    }
    p->offset = lo; p->old_len = hi - lo; p->length = written;
    if (v->state.selection.cursor == v->state.selection.anchor) caret(p, lo + seam);
    else {
        p->selection.cursor = v->state.selection.cursor >= hi ? lo + written : c;
        p->selection.anchor = v->state.selection.anchor >= hi ? lo + written : a;
    }
    if (!joined) { p->old_len = 0; p->length = 0; p->selection = v->state.selection; }
    return 0;
}
static int insert_line(const view *v, bool before, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo, hi; rc = line_start(v, p->selection.cursor, &lo); if (rc) return rc;
    rc = line_stop(v, p->selection.cursor, &hi); if (rc) return rc;
    uint64_t end = content_end(v, lo, hi), at = lo;
    while (at < end && (byte(v, at) == ' ' || byte(v, at) == '\t')) at++;
    size_t indent = (size_t)(at - lo);
    uint8_t newline[sizeof "\r\n" - 1u];
    size_t eol = newline_bytes(v, lo, end, hi, newline);
    if (indent > VIEW_WINDOW || indent + eol > cap) return PIECE_ERR_NOMEM;
    p->offset = before ? lo : end; p->length = indent + eol;
    size_t nl_at = before ? indent : 0, indent_at = before ? 0 : eol;
    rc = piece_read(v->tree, lo, out + indent_at, indent); if (rc) return rc;
    memcpy(out + nl_at, newline, eol);
    if (rc) return rc;
    caret(p, p->offset + (before ? indent : p->length)); return 0;
}
int view_insert_line_after(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return insert_line(v, false, out, cap, p); }
int view_insert_line_before(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return insert_line(v, true, out, cap, p); }
int view_delete_to_bol(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    rc = line_start(v, p->selection.cursor, &p->offset); if (rc) return rc;
    /* move_to(extend) retains the original anchor, even for reversed regions. */
    uint64_t anchor = p->selection.anchor;
    if (anchor == p->offset && anchor) p->offset = anchor - (byte(v, anchor - 1u) == '\n' && anchor > 1u && byte(v, anchor - 2u) == '\r' ? 2u : 1u);
    uint64_t end = upper(anchor, p->offset); p->offset = lower(anchor, p->offset);
    p->old_len = end - p->offset; caret(p, p->offset); return 0;
}
int view_delete_to_eol(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t stop; rc = line_stop(v, p->selection.cursor, &stop); if (rc) return rc;
    uint64_t end = content_end(v, p->selection.cursor, stop), anchor = p->selection.anchor;
    if (anchor == end && end < stop) end = stop;
    p->offset = lower(anchor, end); p->old_len = upper(anchor, end) - p->offset;
    caret(p, p->offset); return 0;
}
static int soft_row(const view *v, uint64_t *start, uint64_t *end)
{
    uint64_t cursor = v->state.selection.cursor;
    if (v->state.wrap && v->wrap_layout) {
        layout_wrap_row row; bool approximate = false;
        int rc = layout_visual_row(v->wrap_layout, v->tree, cursor, 0, &row, &approximate);
        if (rc < 0) return VIEW_ERR_ARG;
        if (v->state.visual_end && row.start == cursor && cursor) {
            layout_wrap_row previous;
            rc = layout_visual_row(v->wrap_layout, v->tree, cursor, -1, &previous, &approximate);
            if (rc < 0) return VIEW_ERR_ARG;
            if (!previous.newline && previous.next == cursor) row = previous;
        }
        if (approximate || row.approximate) return PIECE_ERR_NOMEM;
        *start = row.start; *end = row.end; return 0;
    }
    int rc = line_start(v, cursor, start); if (rc) return rc;
    uint64_t stop; rc = line_stop(v, cursor, &stop); if (rc) return rc;
    *end = content_end(v, *start, stop); return 0;
}
static int previous_cluster(const view *v, uint64_t at, uint64_t *previous)
{
    if (!at) { *previous = 0; return 0; }
    uint64_t start; int rc = line_start(v, at - 1u, &start); if (rc) return rc;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, start, at, src, sizeof src, &n); if (rc) return rc;
    *previous = start + utf8_grapheme_prev(src, n); return 0;
}
int view_delete_to_soft_bol(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t start, end; rc = soft_row(v, &start, &end); if (rc) return rc;
    if (end - start > VIEW_WINDOW) return PIECE_ERR_NOMEM;
    uint64_t first = start;
    while (first < end && (byte(v, first) == ' ' || byte(v, first) == '\t')) first++;
    uint64_t target = p->selection.cursor == first ? start : first, anchor = p->selection.anchor;
    if (target == anchor) { rc = previous_cluster(v, target, &target); if (rc) return rc; }
    p->offset = lower(anchor, target); p->old_len = upper(anchor, target) - p->offset;
    caret(p, p->offset); return 0;
}
int view_delete_to_soft_eol(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t start, target; rc = soft_row(v, &start, &target); if (rc) return rc;
    uint64_t anchor = p->selection.anchor;
    if (anchor == target && target < piece_len(v->tree)) {
        uint8_t src[VIEW_WINDOW]; size_t n = (size_t)lower(piece_len(v->tree) - target, sizeof src);
        rc = piece_read(v->tree, target, src, n); if (rc) return rc;
        size_t next = utf8_grapheme_next(src, n);
        if (next == n && target + n < piece_len(v->tree)) return PIECE_ERR_NOMEM;
        target += next;
    }
    p->offset = lower(anchor, target); p->old_len = upper(anchor, target) - p->offset;
    caret(p, p->offset); return 0;
}
static uint64_t mapped_indent(uint64_t endpoint, uint64_t line, size_t removed, size_t added,
                              uint64_t new_line)
{
    if (endpoint < line) return endpoint;
    uint64_t col = endpoint - line;
    return new_line + (col > removed ? col - removed : 0) + added;
}
static int indentation(const view *v, bool remove, bool tabs, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo, hi; rc = lines(v, &lo, &hi); if (rc) return rc;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t width = v->config.tab_width, written = 0, at = 0;
    if (!width) return VIEW_ERR_ARG;
    uint64_t cursor = p->selection.cursor, anchor = p->selection.anchor;
    do {
        size_t start = at, removed = 0, added = remove ? 0 : tabs ? sizeof "\t" - 1u : width;
        if (remove) {
            size_t column = 0;
            while (at + removed < n && column < width &&
                (src[at + removed] == ' ' || src[at + removed] == '\t')) {
                column += src[at + removed] == '\t' ? width - column % width : sizeof " " - 1u;
                removed++;
            }
        }
        size_t end = at;
        while (end < n && src[end++] != '\n') {}
        if (added + end - start - removed > cap - written) return PIECE_ERR_NOMEM;
        uint64_t old_line = lo + start, new_line = lo + written;
        /* An endpoint at the next line start belongs to that next line; the
         * terminal region endpoint merely shifts with preceding edits. */
        if (cursor >= old_line && (cursor < lo + end || end == n)) p->selection.cursor = mapped_indent(cursor, old_line, removed, added, new_line);
        if (anchor >= old_line && (anchor < lo + end || end == n)) p->selection.anchor = mapped_indent(anchor, old_line, removed, added, new_line);
        if (added) { memset(out + written, tabs ? '\t' : ' ', added); written += added; }
        size_t content = end - start - removed;
        if (content) memcpy(out + written, src + start + removed, content);
        written += content;
        at = end;
    } while (at < n);
    p->offset = lo; p->old_len = hi - lo; p->length = written; return 0;
}
int view_indent(const view *v, bool tabs, uint8_t *out, size_t cap, view_edit_plan *p)
{ return indentation(v, false, tabs, out, cap, p); }
int view_unindent(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return indentation(v, true, false, out, cap, p); }
int view_transpose(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t cursor = p->selection.cursor;
    if (cursor != p->selection.anchor || !cursor || cursor == piece_len(v->tree)) return 0;
    uint64_t lo; rc = line_start(v, cursor, &lo); if (rc) return rc;
    if (lo == cursor) { rc = line_start(v, cursor - 1u, &lo); if (rc) return rc; }
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, cursor, src, sizeof src, &n); if (rc) return rc;
    size_t prev = utf8_prev(src, n);
    if (n > 1u && src[n - 1u] == '\n' && src[n - 2u] == '\r') prev--;
    uint8_t next[sizeof(uint32_t)];
    size_t available = (size_t)lower(piece_len(v->tree) - cursor, sizeof next);
    rc = piece_read(v->tree, cursor, next, available); if (rc) return rc;
    utf8_step unit = utf8_decode(next, available);
    size_t length = n - prev + unit.len;
    if (length > cap) return PIECE_ERR_NOMEM;
    memcpy(out, next, unit.len); memcpy(out + unit.len, src + prev, n - prev);
    p->offset = lo + prev; p->old_len = length; p->length = length;
    caret(p, cursor + unit.len); return 0;
}

#include "view/case_tables.h"
static bool in_ranges(uint32_t cp, const uint32_t ranges[][2], size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (ranges[mid][1] < cp) lo = mid + 1u; else hi = mid;
    }
    return lo < n && ranges[lo][0] <= cp;
}
static bool cased(uint32_t cp)
{ return in_ranges(cp, view_cased, sizeof view_cased / sizeof view_cased[0]); }
static bool ignorable(uint32_t cp)
{ return in_ranges(cp, view_case_ignorable, sizeof view_case_ignorable / sizeof view_case_ignorable[0]); }
static bool final_sigma(const uint8_t *src, size_t n, size_t at, size_t len)
{
    size_t before = at; bool found = false;
    while (before) {
        size_t previous = utf8_prev(src, before);
        utf8_step unit = utf8_decode(src + previous, before - previous); before = previous;
        if (ignorable(unit.cp)) continue;
        found = cased(unit.cp); break;
    }
    if (!found) return false;
    for (size_t next = at + len; next < n;) {
        utf8_step unit = utf8_decode(src + next, n - next); next += unit.len;
        if (ignorable(unit.cp)) continue;
        return !cased(unit.cp);
    }
    return true;
}
static const view_case_mapping *mapping(uint32_t cp)
{
    size_t lo = 0, hi = sizeof view_case_mappings / sizeof view_case_mappings[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (view_case_mappings[mid].cp < cp) lo = mid + 1u;
        else hi = mid;
    }
    return lo < sizeof view_case_mappings / sizeof view_case_mappings[0] &&
        view_case_mappings[lo].cp == cp ? &view_case_mappings[lo] : NULL;
}
static bool unit_word(utf8_step unit)
{
    return unit.valid && (unit.cp == '_' ||
        in_ranges(unit.cp, view_word_alpha, sizeof view_word_alpha / sizeof view_word_alpha[0]) ||
        in_ranges(unit.cp, view_word_digit, sizeof view_word_digit / sizeof view_word_digit[0]));
}
static int word_region(const view *v, uint64_t cursor, uint64_t *lo, uint64_t *hi)
{
    uint64_t start, stop;
    int rc = line_start(v, cursor, &start); if (rc) return rc;
    rc = line_stop(v, cursor, &stop); if (rc) return rc;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, start, stop, src, sizeof src, &n); if (rc) return rc;
    size_t at = (size_t)(cursor - start);
    if (at == n || !unit_word(utf8_decode(src + at, n - at))) {
        if (!at) { *lo = *hi = cursor; return 0; }
        at = utf8_prev(src, at);
        if (!unit_word(utf8_decode(src + at, n - at))) { *lo = *hi = cursor; return 0; }
    }
    size_t a = at, b = at;
    while (a) {
        size_t previous = utf8_prev(src, a);
        if (!unit_word(utf8_decode(src + previous, a - previous))) break;
        a = previous;
    }
    while (b < n) {
        utf8_step unit = utf8_decode(src + b, n - b);
        if (!unit_word(unit)) break;
        b += unit.len;
    }
    *lo = start + a; *hi = start + b; return 0;
}
int view_select_word(const view *v, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    if (p->selection.cursor != p->selection.anchor) return VIEW_ERR_ARG;
    return word_region(v, p->selection.cursor, &p->selection.anchor, &p->selection.cursor);
}
int view_skip_next_occurrence(const view *v, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    if (p->selection.cursor == p->selection.anchor) return view_select_word(v, p);
    uint64_t a = lower(p->selection.cursor, p->selection.anchor), b = upper(p->selection.cursor, p->selection.anchor);
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, 0, piece_len(v->tree), src, sizeof src, &n); if (rc) return rc;
    find_source source = {.bytes = src, .length = n}; find_match match;
    find_code code = find_literal_next(&source, src + a, (size_t)(b - a), b, NULL, &match);
    if (code != FIND_OK) return VIEW_ERR_ARG;
    if (!match.matched) {
        source.length = (size_t)a;
        code = find_literal_next(&source, src + a, (size_t)(b - a), 0, NULL, &match);
        if (code != FIND_OK) return VIEW_ERR_ARG;
    }
    if (!match.matched) return 0;
    bool backward = p->selection.cursor < p->selection.anchor;
    p->selection.cursor = backward ? match.whole.start : match.whole.end;
    p->selection.anchor = backward ? match.whole.end : match.whole.start;
    return 0;
}
static int change_case(const view *v, bool up, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo = lower(p->selection.cursor, p->selection.anchor), hi = upper(p->selection.cursor, p->selection.anchor);
    if (lo == hi) {
        rc = word_region(v, lo, &lo, &hi); if (rc) return rc;
    }
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t written = 0;
    for (size_t i = 0; i < n;) {
        if (lo + i == v->state.selection.cursor) p->selection.cursor = lo + written;
        if (lo + i == v->state.selection.anchor) p->selection.anchor = lo + written;
        utf8_step unit = utf8_decode(src + i, n - i);
        const view_case_mapping *m = unit.valid ? mapping(unit.cp) : NULL;
        const uint8_t *bytes = m ? (up ? m->upper : m->lower) : src + i;
        size_t length = m ? (up ? m->upper_len : m->lower_len) : unit.len;
        /* Unicode SpecialCasing Final_Sigma, as Python str.lower(). */
        static const uint8_t sigma[] = "\xcf\x82";
        if (!up && unit.valid && unit.cp == 0x03a3 && final_sigma(src, n, i, unit.len)) {
            bytes = sigma; length = sizeof sigma - 1u;
        }
        if (length > cap - written) return PIECE_ERR_NOMEM;
        memcpy(out + written, bytes, length); written += length; i += unit.len;
    }
    if (v->state.selection.cursor == hi) p->selection.cursor = lo + written;
    if (v->state.selection.anchor == hi) p->selection.anchor = lo + written;
    p->offset = lo; p->old_len = hi - lo; p->length = written; return 0;
}
int view_upper_case(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return change_case(v, true, out, cap, p); }
int view_lower_case(const view *v, uint8_t *out, size_t cap, view_edit_plan *p)
{ return change_case(v, false, out, cap, p); }

static int compare_lines(const uint8_t *src, size_t n, size_t a, size_t b, bool sensitive)
{
    /* Compare Unicode scalars, matching Python str ordering. Case-insensitive
     * sorting compares lower() strings, including expansions, not casefold(). */
    uint8_t left[sizeof view_case_mappings[0].lower], right[sizeof view_case_mappings[0].lower];
    size_t ln = 0, rn = 0, li = 0, ri = 0;
    for (;;) {
        if (li == ln) {
            if (a == n || src[a] == '\n') ln = 0;
            else {
                utf8_step unit = utf8_decode(src + a, n - a);
                const view_case_mapping *m = !sensitive && unit.valid ? mapping(unit.cp) : NULL;
                ln = m ? m->lower_len : unit.len;
                if (!sensitive && unit.cp == 0x03a3 && final_sigma(src, n, a, unit.len)) {
                    static const uint8_t sigma[] = "\xcf\x82";
                    ln = sizeof sigma - 1u; memcpy(left, sigma, ln);
                } else memcpy(left, m ? m->lower : src + a, ln);
                a += unit.len;
            }
            li = 0;
        }
        if (ri == rn) {
            if (b == n || src[b] == '\n') rn = 0;
            else {
                utf8_step unit = utf8_decode(src + b, n - b);
                const view_case_mapping *m = !sensitive && unit.valid ? mapping(unit.cp) : NULL;
                rn = m ? m->lower_len : unit.len;
                if (!sensitive && unit.cp == 0x03a3 && final_sigma(src, n, b, unit.len)) {
                    static const uint8_t sigma[] = "\xcf\x82";
                    rn = sizeof sigma - 1u; memcpy(right, sigma, rn);
                } else memcpy(right, m ? m->lower : src + b, rn);
                b += unit.len;
            }
            ri = 0;
        }
        if (!ln || !rn) return ln ? 1 : rn ? -1 : 0;
        if (left[li] != right[ri]) return left[li] < right[ri] ? -1 : 1;
        li++; ri++;
    }
}
static void merge_lines(const uint8_t *src, size_t n, size_t *order, size_t *scratch,
                        size_t start, size_t stop, bool sensitive)
{
    if (stop - start < 2u) return;
    size_t mid = start + (stop - start) / 2u;
    merge_lines(src, n, order, scratch, start, mid, sensitive);
    merge_lines(src, n, order, scratch, mid, stop, sensitive);
    size_t a = start, b = mid;
    for (size_t i = start; i < stop; i++)
        scratch[i] = a < mid && (b == stop || compare_lines(src, n, order[a], order[b], sensitive) <= 0) ? order[a++] : order[b++];
    memcpy(order + start, scratch + start, (stop - start) * sizeof *order);
}
int view_sort_lines(const view *v, bool sensitive, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    uint64_t lo, hi;
    if (p->selection.cursor == p->selection.anchor) { lo = 0; hi = piece_len(v->tree); }
    else { rc = lines(v, &lo, &hi); if (rc) return rc; }
    /* Default/sort.py shrink-wraps leading/trailing blank lines, and then
     * selects line() (excluding its terminator) rather than full_line(). */
    uint8_t src[VIEW_WINDOW + 1u]; size_t n;
    if (hi - lo > VIEW_WINDOW) return PIECE_ERR_NOMEM;
    rc = copy(v, lo, hi, src, VIEW_WINDOW, &n); if (rc) return rc;
    size_t a = 0, b = n;
    while (a < b && (src[a] == ' ' || src[a] == '\t' || src[a] == '\r' || src[a] == '\n')) a++;
    while (b > a && (src[b - 1u] == ' ' || src[b - 1u] == '\t' || src[b - 1u] == '\r' || src[b - 1u] == '\n')) b--;
    if (a == b) return 0;
    while (a && src[a - 1u] != '\n') a--;
    while (b < n && src[b] != '\n') b++;
    bool crlf = b && src[b - 1u] == '\r'; if (crlf) b--;
    lo += a; n = b - a; memmove(src, src + a, n); src[n] = 0;
    size_t order[VIEW_WINDOW + 1u], scratch[VIEW_WINDOW + 1u], count = 1;
    order[0] = 0;
    for (size_t i = 0; i < n; i++) if (src[i] == '\n') order[count++] = i + 1u;
    merge_lines(src, n, order, scratch, 0, count, sensitive);
    size_t written = 0;
    for (size_t i = 0; i < count; i++) {
        size_t start = order[i], end = start;
        while (end < n && src[end] != '\n') end++;
        if (end > start && src[end - 1u] == '\r') end--;
        size_t eol = i + 1u < count ? crlf ? sizeof "\r\n" - 1u : sizeof "\n" - 1u : 0;
        if (end - start + eol > cap - written) return PIECE_ERR_NOMEM;
        memcpy(out + written, src + start, end - start); written += end - start;
        if (eol) { if (crlf) out[written++] = '\r'; out[written++] = '\n'; }
    }
    p->offset = lo; p->old_len = n; p->length = written;
    p->selection.anchor = lo; p->selection.cursor = lo + written; return 0;
}

static int subword_class(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z') return 'a';
    if (cp >= 'A' && cp <= 'Z') return 'A';
    if (cp >= '0' && cp <= '9') return '0';
    if (cp < 0x80) return 0;
    uint8_t encoded[sizeof(uint32_t)]; size_t n = utf8_encode(cp, encoded);
    const view_case_mapping *m = mapping(cp);
    if (m && (m->upper_len != n || memcmp(m->upper, encoded, n))) return 'a';
    if (m && (m->lower_len != n || memcmp(m->lower, encoded, n))) return 'A';
    if (in_ranges(cp, view_word_alpha, sizeof view_word_alpha / sizeof view_word_alpha[0])) return 'a';
    return in_ranges(cp, view_word_digit, sizeof view_word_digit / sizeof view_word_digit[0]) ? '0' : 0;
}
static bool subword_break(int before, int current, int next)
{
    return !before || !current || (before == 'a' && current == 'A') ||
        (before == 'A' && current == 'A' && next == 'a') ||
        (before == '0') != (current == '0');
}
static int subword(const view *v, bool rightward, bool extend, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    uint64_t lo, hi;
    rc = line_start(v, p->selection.cursor, &lo); if (rc) return rc;
    rc = line_stop(v, p->selection.cursor, &hi); if (rc) return rc;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t cursor = (size_t)(p->selection.cursor - lo), target = cursor;
    if (rightward) {
        size_t at = cursor; int before = 0; bool started = false;
        while (at < n) {
            utf8_step unit = utf8_decode(src + at, n - at); int current = subword_class(unit.cp), next = 0;
            size_t step = utf8_grapheme_next(src + at, n - at);
            if (at + step < n) next = subword_class(utf8_decode(src + at + step, n - at - step).cp);
            if (started && subword_break(before, current, next)) break;
            if (current) started = true;
            before = current; at += step;
        }
        target = at;
    } else {
        if (!cursor && lo) {
            target = 0;
            lo -= lo > 1u && byte(v, lo - 2u) == '\r' ? sizeof "\r\n" - 1u : sizeof "\n" - 1u;
        }
        else {
            size_t at = 0, last = 0; int before = 0;
            while (at < cursor) {
                utf8_step unit = utf8_decode(src + at, n - at); int current = subword_class(unit.cp), next = 0;
                size_t step = utf8_grapheme_next(src + at, n - at);
                if (at + step < n) next = subword_class(utf8_decode(src + at + step, n - at - step).cp);
                if (current && subword_break(before, current, next)) last = at;
                before = current; at += step;
            }
            target = last;
        }
    }
    p->selection.cursor = lo + target;
    if (!extend) p->selection.anchor = p->selection.cursor;
    return 0;
}
int view_subword_left(const view *v, bool extend, view_edit_plan *p)
{ return subword(v, false, extend, p); }
int view_subword_right(const view *v, bool extend, view_edit_plan *p)
{ return subword(v, true, extend, p); }
static bool opening(uint8_t ch) { return ch == '(' || ch == '[' || ch == '{'; }
static bool closing(uint8_t ch) { return ch == ')' || ch == ']' || ch == '}'; }
static uint8_t matching(uint8_t ch) { return ch == '(' ? ')' : ch == '[' ? ']' : '}'; }
static int brackets(const view *v, uint64_t *a, uint64_t *b)
{
    uint64_t cursor = v->state.selection.cursor, total = piece_len(v->tree);
    uint64_t lo = cursor > VIEW_WINDOW / 2u ? cursor - VIEW_WINDOW / 2u : 0;
    uint64_t hi = lower(total, lo + VIEW_WINDOW);
    uint8_t src[VIEW_WINDOW]; size_t n;
    int rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t stack[VIEW_WINDOW], depth = 0; bool found = false;
    *a = *b = cursor;
    for (size_t i = 0; i < n; i++) {
        if (opening(src[i])) stack[depth++] = i;
        else if (closing(src[i]) && depth && matching(src[stack[depth - 1u]]) == src[i]) {
            uint64_t left = lo + stack[--depth], right = lo + i;
            if (left <= cursor && cursor <= right + 1u && (!found || right - left < *b - *a)) {
                *a = left; *b = right; found = true;
            }
        }
    }
    if (!found && (lo || hi < total)) return PIECE_ERR_NOMEM;
    return 0;
}
int view_goto_bracket(const view *v, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    uint64_t a, b; rc = brackets(v, &a, &b); if (rc) return rc;
    if (a != b) caret(p, p->selection.cursor <= a + 1u ? b + 1u : a);
    return 0;
}
int view_select_brackets(const view *v, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    uint64_t a, b; rc = brackets(v, &a, &b); if (rc) return rc;
    if (a != b) { p->selection.anchor = a + 1u; p->selection.cursor = b; }
    return 0;
}
void view_set_mark(const view *v, view_selection *mark, bool *valid)
{ if (v && mark && valid) { *mark = v->state.selection; *valid = true; } }
void view_clear_mark(bool *valid) { if (valid) *valid = false; }
int view_select_to_mark(const view *v, const view_selection *mark, bool valid, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc) return rc;
    if (valid && mark) {
        uint64_t lo = lower(lower(mark->cursor, mark->anchor), lower(p->selection.cursor, p->selection.anchor));
        uint64_t hi = upper(upper(mark->cursor, mark->anchor), upper(p->selection.cursor, p->selection.anchor));
        if (hi > piece_len(v->tree)) return VIEW_ERR_ARG;
        p->selection.anchor = lo; p->selection.cursor = hi;
    }
    return 0;
}
int view_swap_mark(const view *v, view_selection *mark, bool *valid, view_edit_plan *p)
{
    int rc = begin(v, NULL, 0, p); if (rc || !mark || !valid) return rc ? rc : VIEW_ERR_ARG;
    if (*valid) p->selection = *mark;
    view_set_mark(v, mark, valid); p->selection.preferred_col = VIEW_PREFERRED_UNSET; return 0;
}
int view_delete_to_mark(const view *v, const view_selection *mark, bool valid, view_edit_plan *p)
{
    int rc = view_select_to_mark(v, mark, valid, p); if (rc) return rc;
    p->offset = lower(p->selection.cursor, p->selection.anchor);
    p->old_len = upper(p->selection.cursor, p->selection.anchor) - p->offset;
    if (!p->old_len && p->offset) {
        uint64_t previous;
        rc = previous_cluster(v, p->offset, &previous); if (rc) return rc;
        p->old_len = p->offset - previous; p->offset = previous;
    }
    caret(p, p->offset); return 0;
}
int view_yank(const view *v, const uint8_t *text, size_t len, uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    if (!text && len) return VIEW_ERR_ARG;
    if (len > cap) return PIECE_ERR_NOMEM;
    p->offset = lower(p->selection.cursor, p->selection.anchor);
    p->old_len = upper(p->selection.cursor, p->selection.anchor) - p->offset;
    if (len) memcpy(out, text, len);
    p->length = len; caret(p, p->offset + len); return 0;
}
int view_overwrite_range(const view *v, const uint8_t *text, size_t len, uint64_t *old_len)
{
    if (!v || !text || !len || !old_len || v->state.selection.cursor > piece_len(v->tree)) return VIEW_ERR_ARG;
    *old_len = 0;
    uint64_t cursor = v->state.selection.cursor;
    uint8_t src[VIEW_WINDOW]; size_t n = (size_t)lower(piece_len(v->tree) - cursor, sizeof src);
    int rc = piece_read(v->tree, cursor, src, n); if (rc) return rc;
    size_t at = 0;
    for (size_t i = 0; i < len; i += utf8_grapheme_next(text + i, len - i)) {
        if (at == n || src[at] == '\n' || (src[at] == '\r' && at + 1u < n && src[at + 1u] == '\n')) break;
        size_t next = at + utf8_grapheme_next(src + at, n - at);
        if (next == n && cursor + n < piece_len(v->tree)) return PIECE_ERR_NOMEM;
        at = next;
    }
    *old_len = at; return 0;
}

/* Comment delimiters are transcribed from the installed Sublime language
 * Comments.tmPreferences. File suffixes select the ordinary language only;
 * embedded scopes and custom syntax preferences need the syntax module. */
typedef struct comment_style {
    const char *line, *open, *close;
    bool block_no_indent;
} comment_style;
static comment_style comments_for(const char *path)
{
    const char *name = path ? strrchr(path, '/') : NULL;
    name = name ? name + 1 : path;
    const char *ext = name ? strrchr(name, '.') : NULL;
    if (!ext) return (comment_style){0};
    const char *c_like[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".m", ".mm", ".rs", ".js", ".ts", ".json", ".java", ".go"};
    for (size_t i = 0; i < sizeof c_like / sizeof c_like[0]; i++)
        if (!strcmp(ext, c_like[i])) return (comment_style){"// ", "/*", "*/", true};
    if (!strcmp(ext, ".py")) return (comment_style){"# ", NULL, NULL, false};
    if (!strcmp(ext, ".sh") || !strcmp(ext, ".bash")) return (comment_style){"# ", "`# ", "`", false};
    if (!strcmp(ext, ".sql")) return (comment_style){"-- ", "/*", "*/", false};
    if (!strcmp(ext, ".lua")) return (comment_style){"-- ", "--[[", "]]", false};
    if (!strcmp(ext, ".css")) return (comment_style){NULL, "/*", "*/", false};
    if (!strcmp(ext, ".html") || !strcmp(ext, ".htm") || !strcmp(ext, ".xml"))
        return (comment_style){NULL, "<!-- ", " -->", false};
    return (comment_style){0};
}
static bool token_at(const uint8_t *src, size_t n, size_t at, const char *token, size_t len)
{ return len && at <= n && len <= n - at && !memcmp(src + at, token, len); }
static size_t trimmed_token(const char *token, size_t *start)
{
    size_t n = strlen(token); *start = 0;
    while (*start < n && (token[*start] == ' ' || token[*start] == '\t')) (*start)++;
    while (n > *start && (token[n - 1u] == ' ' || token[n - 1u] == '\t')) n--;
    return n - *start;
}
static void comment_rebase(view_edit_plan *p, uint64_t at, size_t removed, size_t added, bool keep_left)
{
    uint64_t low = lower(p->selection.cursor, p->selection.anchor);
    bool selected = p->selection.cursor != p->selection.anchor;
    uint64_t *points[] = {&p->selection.cursor, &p->selection.anchor};
    for (size_t i = 0; i < sizeof points / sizeof points[0]; i++) {
        uint64_t point = *points[i];
        if (point > at + removed) *points[i] = point - removed + added;
        else if (point >= at) {
            if (!removed && keep_left && selected && point == low) continue;
            *points[i] = at + added;
        }
    }
}
/* Locate a surrounding block in a local lexical window. Quoted strings and
 * line comments are skipped, so a delimiter in ordinary code strings does
 * not become an uncomment operation. This is bounded like bracket matching;
 * cross-window and embedded-language scope are not parser-certified. */
static int comment_block(const view *v, comment_style style, uint64_t a, uint64_t b,
                         uint64_t *lo, uint64_t *hi, size_t *prefix, size_t *suffix)
{
    if (!style.open) return 0;
    uint64_t start = a > VIEW_WINDOW / 2u ? a - VIEW_WINDOW / 2u : 0;
    int rc = line_start(v, start, &start); if (rc) return -rc;
    uint64_t stop = lower(piece_len(v->tree), start + VIEW_WINDOW);
    if (b > stop) return 0;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, start, stop, src, sizeof src, &n); if (rc) return -rc;
    size_t os, cs, ol = trimmed_token(style.open, &os), cl = trimmed_token(style.close, &cs);
    size_t ll = style.line ? strlen(style.line) : 0, ls = 0;
    if (ll) ll = trimmed_token(style.line, &ls);
    for (size_t i = 0; i < n;) {
        if (token_at(src, n, i, style.open + os, ol)) {
            size_t end = i + ol;
            while (end < n && !token_at(src, n, end, style.close + cs, cl)) end++;
            if (end == n) return 0;
            if (start + i <= a && start + end + cl >= b) {
                *lo = start + i; *hi = start + end + cl;
                *prefix = ol; *suffix = cl;
                /* Match the spaced preference before its stripped variant. */
                size_t full_open = strlen(style.open), full_close = strlen(style.close);
                if (full_open + full_close <= end + cl - i &&
                    token_at(src, n, i, style.open, full_open) && end >= cs &&
                    token_at(src, n, end - cs, style.close, full_close)) {
                    *prefix = full_open; *suffix = full_close;
                }
                return 1;
            }
            i = end + cl;
        } else if (ll && token_at(src, n, i, style.line + ls, ll)) {
            while (i < n && src[i] != '\n') i++;
        } else if (src[i] == '"' || src[i] == '\'') {
            uint8_t quote = src[i++];
            while (i < n && src[i] != quote && src[i] != '\n') {
                if (src[i] == '\\' && i + 1u < n) i++;
                i++;
            }
            if (i < n) i++;
        } else i++;
    }
    return 0;
}
static int comment_wrap(const view *v, comment_style style, uint64_t lo, uint64_t hi,
                        uint8_t *out, size_t cap, view_edit_plan *p)
{
    size_t prefix = strlen(style.open), suffix = strlen(style.close), n;
    if (prefix > cap || suffix > cap - prefix || hi - lo > cap - prefix - suffix) return PIECE_ERR_NOMEM;
    int rc = copy(v, lo, hi, out + prefix, cap - prefix - suffix, &n); if (rc) return rc;
    memcpy(out, style.open, prefix); memcpy(out + prefix + n, style.close, suffix);
    p->offset = lo; p->old_len = hi - lo; p->length = prefix + n + suffix;
    bool empty = lo == hi;
    comment_rebase(p, hi, 0, suffix, false);
    comment_rebase(p, lo, 0, prefix, true);
    if (empty) caret(p, lo + prefix);
    return 0;
}
int view_toggle_comment(const view *v, const char *path, bool block,
                        uint8_t *out, size_t cap, view_edit_plan *p)
{
    int rc = begin(v, out, cap, p); if (rc) return rc;
    comment_style style = comments_for(path);
    if (!style.line && !style.open) return 0; /* plain text has no comment syntax */
    uint64_t a = lower(p->selection.cursor, p->selection.anchor), b = upper(p->selection.cursor, p->selection.anchor);
    uint64_t lo = 0, hi = 0; size_t prefix = 0, suffix = 0;
    rc = comment_block(v, style, a, b, &lo, &hi, &prefix, &suffix);
    if (rc < 0) return -rc;
    if (rc == 1) {
        size_t n;
        rc = copy(v, lo + prefix, hi - suffix, out, cap, &n); if (rc) return rc;
        p->offset = lo; p->old_len = hi - lo; p->length = n;
        comment_rebase(p, hi - suffix, suffix, 0, false);
        comment_rebase(p, lo, prefix, 0, false);
        return 0;
    }
    rc = lines(v, &lo, &hi); if (rc) return rc;
    uint8_t src[VIEW_WINDOW]; size_t n;
    rc = copy(v, lo, hi, src, sizeof src, &n); if (rc) return rc;
    size_t line_len = style.line ? strlen(style.line) : 0, bare_start = 0;
    size_t bare_len = line_len ? trimmed_token(style.line, &bare_start) : 0;
    bool nonempty = false, remove = line_len != 0;
    uint64_t min_col = UINT64_MAX;
    uint32_t tab = v->config.tab_width;
    for (size_t at = 0;;) {
        size_t end = at; while (end < n && src[end] != '\n') end++;
        size_t content = end; if (content > at && src[content - 1u] == '\r') content--;
        size_t first = at; uint64_t col = 0;
        while (first < content && (src[first] == ' ' || src[first] == '\t')) {
            col += src[first] == '\t' ? tab - col % tab : 1u; first++;
        }
        if (first < content) {
            nonempty = true; min_col = lower(min_col, col);
            if (!token_at(src, content, first, style.line ? style.line + bare_start : NULL, bare_len)) remove = false;
        }
        if (end == n) break;
        at = end + 1u; if (at == n) break;
    }
    if (!nonempty) { remove = false; min_col = UINT64_MAX; }
    if (!remove && block && style.open) return comment_wrap(v, style, a, b, out, cap, p);
    if (!remove && !style.line) {
        /* Ctrl+/ without line comments wraps the sole hard line at its indent;
         * multiple selected lines fall back to the original selection. */
        uint64_t first_stop; rc = line_stop(v, lo, &first_stop); if (rc) return rc;
        if (first_stop == hi) {
            hi = content_end(v, lo, hi);
            if (!style.block_no_indent) while (lo < hi && (byte(v, lo) == ' ' || byte(v, lo) == '\t')) lo++;
            return comment_wrap(v, style, lo, hi, out, cap, p);
        }
        return comment_wrap(v, style, a, b, out, cap, p);
    }
    /* All-blank selections use their common indentation too. */
    if (!nonempty) {
        for (size_t at = 0;;) {
            size_t i = at; uint64_t col = 0;
            while (i < n && (src[i] == ' ' || src[i] == '\t')) { col += src[i] == '\t' ? tab - col % tab : 1u; i++; }
            min_col = lower(min_col, col);
            while (i < n && src[i] != '\n') i++;
            if (i == n || i + 1u == n) break;
            at = i + 1u;
        }
    }
    size_t written = 0;
    for (size_t at = 0;;) {
        size_t end = at; while (end < n && src[end] != '\n') end++;
        size_t next = end < n ? end + 1u : end;
        size_t content = end; if (content > at && src[content - 1u] == '\r') content--;
        size_t first = at; while (first < content && (src[first] == ' ' || src[first] == '\t')) first++;
        bool active = !nonempty || first < content;
        size_t pos = first, removed = 0, added = 0;
        if (active) {
            if (remove) removed = token_at(src, content, first, style.line, line_len) ? line_len : bare_len;
            else {
                pos = at; uint64_t col = 0;
                while (pos < first && col < min_col) {
                    uint64_t step = src[pos] == '\t' ? tab - col % tab : 1u;
                    if (step > min_col - col) break;
                    col += step; pos++;
                }
                added = line_len;
            }
        }
        size_t len = next - at - removed + added;
        if (len > cap - written) return PIECE_ERR_NOMEM;
        memcpy(out + written, src + at, pos - at);
        uint64_t edit_at = lo + written + pos - at;
        if (added) memcpy(out + written + pos - at, style.line, added);
        memcpy(out + written + pos - at + added, src + pos + removed, next - pos - removed);
        comment_rebase(p, edit_at, removed, added, true);
        written += len;
        if (next == n) break;
        at = next;
    }
    p->offset = lo; p->old_len = hi - lo; p->length = written;
    return 0;
}
