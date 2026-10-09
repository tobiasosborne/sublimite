/* layout (P3.1): see layout.h. */
#include "layout/layout.h"
#include "utf8/utf8.h"
#include <string.h>

#define L_REFILL_MARGIN 128u      /* bytes guaranteed ahead of a cluster unless EOF */
#define L_SKIP_UNIT 256u          /* skipped bytes per budget unit */

static inline render_cell blank_cell(const layout *l)
{
    return (render_cell){0, RENDER_NO_SLOT, l->cfg.fg, l->cfg.bg, 0, 0};
}

void layout_ascii_glyphs(const font_ascii_atlas *a, render_glyph *out)
{
    for (uint32_t i = 0; i < LAYOUT_ASCII_GLYPHS; i++)
        out[i] = (render_glyph){0x20u + i, 0, i * a->cell.cell_w, 0, a->cell.cell_w, a->cell.cell_h};
}

static uint32_t digits_of(uint64_t v)
{
    uint32_t d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

static uint32_t gutter_width(const layout *l, uint64_t line_count)
{
    if (!l->cfg.gutter) return 0;
    uint32_t w = digits_of(line_count) + 1;
    return w < l->grid->dims.cols ? w : l->grid->dims.cols;
}

static void set_gw(layout *l, uint32_t gw)
{
    l->gw = gw;
    l->text_cols = l->grid->dims.cols - gw;
}

int layout_init(layout *l, render_grid *g, const layout_config *cfg,
                uint64_t *row_byte, uint32_t *row_used)
{
    if (!l || !g || !cfg || !row_byte || !row_used || !g->cells) return LAYOUT_ERR_ARG;
    memset(l, 0, offsetof(layout, win));
    l->grid = g;
    l->cfg = *cfg;
    if (l->cfg.tab_width == 0) l->cfg.tab_width = 4;
    l->tab = l->cfg.tab_width;
    l->row_byte = row_byte;
    l->row_used = row_used;
    l->cursor = UINT64_MAX;
    l->sel_lo = l->sel_hi = 0;
    set_gw(l, 0);
    size_t n = (size_t)g->dims.cols * g->dims.rows;
    render_cell b = blank_cell(l);
    for (size_t i = 0; i < n; i++) g->cells[i] = b;
    for (uint32_t r = 0; r < g->dims.rows; r++) { row_used[r] = 0; row_byte[r] = LAYOUT_VOID_ROW; }
    return LAYOUT_DONE;
}

static int src_read(const layout *l, uint64_t off, uint8_t *dst, size_t n)
{
    int rc = l->is_snap ? piece_snapshot_read((const piece_snapshot *)l->src, off, dst, n)
                        : piece_read((const piece_tree *)l->src, off, dst, n);
    return rc == PIECE_OK ? 0 : -1;
}

static int begin_common(layout *l, layout_viewport vp)
{
    if (vp.first_byte > l->total) return LAYOUT_ERR_ARG;
    if (vp.first_line == LAYOUT_LINE_UNKNOWN) {
        vp.first_line = l->is_snap ? piece_snapshot_byte_to_line((const piece_snapshot *)l->src, vp.first_byte)
                                   : piece_byte_to_line((const piece_tree *)l->src, vp.first_byte);
    }
    l->first_byte = vp.first_byte;
    l->first_line = vp.first_line;
    l->approximate = vp.hscroll > LAYOUT_MAX_HSCROLL;
    l->hscroll = l->approximate ? LAYOUT_MAX_HSCROLL : vp.hscroll;
    l->line_count = vp.line_count;
    uint32_t gw = gutter_width(l, l->line_count);
    if (gw != l->gw) {                      /* old gutter cells may be anywhere: re-clear */
        for (uint32_t r = 0; r < l->grid->dims.rows; r++) l->row_used[r] = l->grid->dims.cols;
    }
    set_gw(l, gw);
    l->row_byte[0] = vp.first_byte;
    l->row = 0;
    l->row_end = l->grid->dims.rows;
    l->phase = 0;
    l->win_len = l->wi = 0; l->win_eof = false;
    return render_mark_full(l->grid);
}

int layout_begin(layout *l, const piece_tree *t, layout_viewport vp)
{
    if (!l || !t) return LAYOUT_ERR_ARG;
    l->src = t; l->is_snap = 0;
    l->total = piece_len(t);
    if (vp.line_count == LAYOUT_LINE_UNKNOWN || vp.line_count == 0) vp.line_count = piece_line_count(t);
    return begin_common(l, vp);
}

int layout_begin_snapshot(layout *l, const piece_snapshot *s, layout_viewport vp)
{
    if (!l || !s) return LAYOUT_ERR_ARG;
    l->src = s; l->is_snap = 1;
    l->total = piece_snapshot_len(s);
    if (vp.line_count == LAYOUT_LINE_UNKNOWN || vp.line_count == 0) vp.line_count = piece_snapshot_line_count(s);
    return begin_common(l, vp);
}

void layout_set_cursor(layout *l, uint64_t byte)
{
    l->cursor = byte;
    l->marks = l->cursor != UINT64_MAX || l->sel_hi > l->sel_lo;
}

void layout_set_selection(layout *l, uint64_t lo, uint64_t hi)
{
    l->sel_lo = lo; l->sel_hi = hi;
    l->marks = l->cursor != UINT64_MAX || l->sel_hi > l->sel_lo;
}

bool layout_approximate(const layout *l) { return l->approximate; }
uint32_t layout_gutter_width(const layout *l) { return l->gw; }
bool layout_busy(const layout *l) { return l->row < l->row_end; }

static void refill(layout *l)
{
    uint64_t left = l->total - l->pos;
    uint32_t n = left < LAYOUT_WIN ? (uint32_t)left : LAYOUT_WIN;
    if (n && src_read(l, l->pos, l->win, n) != 0) n = 0, l->total = l->pos;   /* source shrank: treat as EOF */
    l->win_pos = l->pos;
    l->win_len = n;
    l->wi = 0;
    l->win_eof = l->pos + n >= l->total;
}

/* Cursor / selection style of the cell for the character at byte p. */
static inline void style_at(const layout *l, uint64_t p, uint32_t *fg, uint32_t *bg, uint16_t *at)
{
    if (p == l->cursor) { *fg = l->cfg.cursor_fg; *bg = l->cfg.cursor_bg; *at |= RENDER_ATTR_CURSOR; }
    else if (p >= l->sel_lo && p < l->sel_hi) { *fg = l->cfg.sel_fg; *bg = l->cfg.sel_bg; *at |= RENDER_ATTR_SELECTION; }
}

static void write_gutter(layout *l, render_cell *row, uint64_t line, bool number)
{
    render_cell b = {0, RENDER_NO_SLOT, l->cfg.gutter_fg, l->cfg.gutter_bg, 0, 0};
    for (uint32_t i = 0; i < l->gw; i++) row[i] = b;
    if (!number || l->gw < 2) return;
    uint32_t i = l->gw - 1;            /* last cell is the separator */
    uint64_t v = line + 1;
    do {
        uint32_t d = (uint32_t)(v % 10);
        v /= 10;
        if (i == 0) break;             /* number wider than the gutter (cannot happen with exact count) */
        i--;
        row[i].glyph_index = '0' + d;
        row[i].atlas_slot = '0' + d - 0x20u;
    } while (v);
}

/* Finish the current row: clear stale cells beyond the written extent. */
static void end_row(layout *l, render_cell *row, uint32_t written)
{
    uint32_t cols = l->grid->dims.cols, used = l->row_used[l->row];
    if (used > cols) used = cols;
    if (written < used) {
        render_cell b = blank_cell(l);
        for (uint32_t i = written; i < used; i++) row[i] = b;
    }
    l->row_used[l->row] = written;
    l->row++;
    l->phase = 0;
}

static inline render_cell *row_ptr(layout *l)
{
    return l->grid->cells + (size_t)l->row * l->grid->dims.cols;
}

int layout_run(layout *l)
{
    if (!l || !l->grid) return LAYOUT_ERR_ARG;
    const uint32_t budget = l->cfg.slice_clusters ? l->cfg.slice_clusters : UINT32_MAX;
    const uint32_t text_cols = l->text_cols, gw = l->gw, tab = l->tab;
    const uint64_t hscroll = l->hscroll;
    const uint32_t nrows = l->grid->dims.rows;
    uint32_t units = 0;

    while (l->row < l->row_end) {
        render_cell *row = row_ptr(l);
        if (l->phase == 0) {
            uint64_t start = l->row_byte[l->row];
            if (start == LAYOUT_VOID_ROW) {
                write_gutter(l, row, 0, false);
                if (l->row + 1 < nrows) l->row_byte[l->row + 1] = LAYOUT_VOID_ROW;
                end_row(l, row, gw);
                continue;
            }
            write_gutter(l, row, l->first_line + l->row, true);
            l->pos = start; l->col = 0; l->vis = 0; l->phase = 1;
            if (l->win_len && start >= l->win_pos && start - l->win_pos <= l->win_len) {
                l->wi = (uint32_t)(start - l->win_pos);        /* line start is inside the window: reuse it */
            } else {
                l->win_len = l->wi = 0; l->win_eof = false;
            }
            if (text_cols == 0) l->phase = 2;
        }
        render_cell *tx = row + gw;
        bool ended = false, nl = false;
        if (l->phase == 1) {
            while (units < budget) {
                if (l->wi + L_REFILL_MARGIN > l->win_len && !l->win_eof) refill(l);
                if (l->wi >= l->win_len) { ended = true; break; }
                const uint8_t *p = l->win + l->wi;
                uint32_t b = p[0];
                if (l->vis >= text_cols) { l->phase = 2; break; }
                if (b == '\n') { ended = true; nl = true; break; }
                size_t avail = l->win_len - l->wi;
                uint64_t at = l->pos;
                if (b >= 0x20 && b < 0x7f && (avail < 2 || p[1] < 0x80)) {      /* ASCII fast path */
                    if (l->col >= hscroll) {
                        render_cell *c = &tx[l->vis++];
                        c->fg = l->cfg.fg; c->bg = l->cfg.bg; c->attrs = 0; c->reserved = 0;
                        if (b == ' ') { c->glyph_index = 0; c->atlas_slot = RENDER_NO_SLOT; }
                        else { c->glyph_index = b; c->atlas_slot = b - 0x20u; }
                        if (l->marks) style_at(l, at, &c->fg, &c->bg, &c->attrs);
                    }
                    l->col++; l->pos++; l->wi++; units++;
                    continue;
                }
                if (b == '\r' && avail >= 2 && p[1] == '\n') { l->pos++; l->wi++; continue; }
                if (b == '\t') {
                    uint32_t w = tab - (uint32_t)(l->col % tab);
                    for (uint32_t k = 0; k < w && l->vis < text_cols; k++) {
                        if (l->col + k < hscroll) continue;
                        render_cell *c = &tx[l->vis++];
                        *c = blank_cell(l);
                        if (l->marks && !(k > 0 && at == l->cursor)) style_at(l, at, &c->fg, &c->bg, &c->attrs);
                    }
                    l->col += w; l->pos++; l->wi++; units++;
                    continue;
                }
                /* general path: control, invalid, non-ASCII cluster */
                uint32_t slot = '?' - 0x20u, gidx = '?';
                uint16_t attrs = 0;
                size_t clen = 1;
                int w = 1;
                if (b < 0x20 || b == 0x7f) {
                    attrs = RENDER_ATTR_INVERSE;
                } else {
                    clen = utf8_cluster(p, avail, &w);
                    if (b >= 0x80 && !utf8_decode(p, avail).valid) {
                        attrs = RENDER_ATTR_INVERSE; clen = 1; w = 1;
                    } else if (w == 0) {
                        l->pos += clen; l->wi += (uint32_t)clen; units++;
                        continue;
                    } else if (clen == 1 && b < 0x80) {
                        gidx = b; slot = b - 0x20u;
                        if (b == ' ') { gidx = 0; slot = RENDER_NO_SLOT; }
                    } else {
                        uint32_t s2 = 0;
                        if (l->cfg.glyph && l->cfg.glyph(l->cfg.glyph_ctx, p, clen, (uint32_t)w, &s2) == 0 &&
                            (s2 == RENDER_NO_SLOT || s2 < l->grid->glyph_count)) {
                            slot = s2;
                            gidx = s2 == RENDER_NO_SLOT ? 0 : l->grid->glyphs[s2].glyph_index;
                        } else {
                            gidx = '?';
                        }
                    }
                }
                uint64_t c0 = l->col, c1 = l->col + (uint32_t)w;
                if (c1 > hscroll) {
                    uint32_t fg = l->cfg.fg, bg = l->cfg.bg;
                    if (l->marks) style_at(l, at, &fg, &bg, &attrs);
                    if (w == 1) {
                        render_cell *c = &tx[l->vis++];
                        *c = (render_cell){slot == RENDER_NO_SLOT ? 0 : gidx, slot, fg, bg, attrs, 0};
                    } else if (c0 < hscroll || l->vis + 2 > text_cols) {
                        /* wide cluster cut by the left or right edge: blank cell(s) */
                        uint32_t room = text_cols - l->vis;
                        uint32_t cells = c0 < hscroll ? (uint32_t)(c1 - hscroll) : 1u;
                        if (cells > room) cells = room;
                        for (uint32_t k = 0; k < cells; k++) {
                            tx[l->vis] = blank_cell(l);
                            if (c0 >= hscroll) { tx[l->vis].bg = bg; }
                            l->vis++;
                        }
                    } else {
                        render_cell *c = &tx[l->vis];
                        c[0] = (render_cell){slot == RENDER_NO_SLOT ? 0 : gidx, slot, fg, bg,
                                             (uint16_t)(attrs | RENDER_ATTR_WIDE_LEFT), 0};
                        c[1] = (render_cell){0, RENDER_NO_SLOT, fg, bg,
                                             (uint16_t)(attrs | RENDER_ATTR_WIDE_RIGHT), 0};
                        l->vis += 2;
                    }
                }
                l->col = c1; l->pos += clen; l->wi += (uint32_t)clen; units++;
            }
            if (l->phase == 1 && !ended) break;       /* budget exhausted */
        }
        if (l->phase == 2 && !ended) {
            /* skip the rest of the line, bounded per call */
            for (;;) {
                if (units >= budget) goto more;
                if (l->wi >= l->win_len && !l->win_eof) refill(l);
                if (l->wi >= l->win_len) { ended = true; break; }
                const uint8_t *q = l->win + l->wi;
                size_t rem = l->win_len - l->wi;
                const uint8_t *nlp = memchr(q, '\n', rem);
                size_t adv = nlp ? (size_t)(nlp - q) : rem;
                l->pos += adv; l->wi += (uint32_t)adv;
                units += (uint32_t)(adv / L_SKIP_UNIT) + 1;
                if (nlp) { ended = true; nl = true; break; }
            }
        }
        if (!ended) break;
        /* row complete: cursor sitting on the newline / EOF draws a blank cursor cell */
        uint32_t written = gw + l->vis;
        if (l->marks && l->pos == l->cursor && l->vis < text_cols && l->col >= hscroll) {
            render_cell *c = &tx[l->vis];
            *c = blank_cell(l);
            c->fg = l->cfg.cursor_fg; c->bg = l->cfg.cursor_bg; c->attrs = RENDER_ATTR_CURSOR;
            written++;
        }
        if (l->row + 1 < nrows) l->row_byte[l->row + 1] = nl ? l->pos + 1 : LAYOUT_VOID_ROW;
        end_row(l, row, written);
    }
    return l->row < l->row_end ? LAYOUT_MORE : LAYOUT_DONE;
more:
    return LAYOUT_MORE;
}

int layout_relayout_rows(layout *l, uint32_t first, uint32_t count)
{
    if (!l || !l->src) return LAYOUT_ERR_STATE;
    if (layout_busy(l)) return LAYOUT_ERR_STATE;
    uint32_t rows = l->grid->dims.rows;
    if (first > rows || count > rows - first) return LAYOUT_ERR_ARG;
    int rc = render_mark_rows(l->grid, first, count);
    if (rc != RENDER_OK) return LAYOUT_ERR_STATE;
    l->row = first; l->row_end = first + count; l->phase = 0;
    l->win_len = l->wi = 0; l->win_eof = false;
    return count ? LAYOUT_MORE : LAYOUT_DONE;
}

static uint32_t row_of(const layout *l, uint64_t off)
{
    uint32_t lo = 0, hi = l->grid->dims.rows;     /* last r with row_byte[r] <= off (void rows are max) */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (l->row_byte[mid] <= off) lo = mid; else hi = mid;
    }
    return lo;
}

static void shift_rows(layout *l, uint32_t from, int64_t delta)
{
    for (uint32_t r = from; r < l->grid->dims.rows; r++)
        if (l->row_byte[r] != LAYOUT_VOID_ROW) l->row_byte[r] = (uint64_t)((int64_t)l->row_byte[r] + delta);
}

int layout_edit(layout *l, uint64_t off, uint64_t old_len, uint64_t new_len,
                uint64_t old_nl, uint64_t new_nl)
{
    if (!l || !l->src || l->is_snap) return LAYOUT_ERR_STATE;
    const piece_tree *t = (const piece_tree *)l->src;
    int64_t delta = (int64_t)new_len - (int64_t)old_len;
    bool was_busy = layout_busy(l);
    l->total = piece_len(t);
    l->win_len = l->wi = 0; l->win_eof = false;
    uint32_t rows = l->grid->dims.rows;
    uint64_t lc = l->line_count + new_nl - old_nl;
    l->line_count = lc;

    if (off < l->first_byte) {
        if (off + old_len > l->first_byte || (off + old_len == l->first_byte && old_nl))
            return LAYOUT_RESET;
        l->first_byte = (uint64_t)((int64_t)l->first_byte + delta);
        shift_rows(l, 0, delta);
        if (old_nl == new_nl && !was_busy) return LAYOUT_DONE;
        l->first_line = (uint64_t)((int64_t)l->first_line + (int64_t)new_nl - (int64_t)old_nl);
        layout_viewport vp = {l->first_byte, l->first_line, l->hscroll, l->line_count};
        return begin_common(l, vp) == RENDER_OK ? LAYOUT_MORE : LAYOUT_ERR_STATE;
    }
    if (was_busy || gutter_width(l, lc) != l->gw) {
        /* busy: restart; gutter width changed: geometry changes, redo everything */
        layout_viewport vp = {l->first_byte, l->first_line, l->hscroll, lc};
        return begin_common(l, vp) == RENDER_OK ? LAYOUT_MORE : LAYOUT_ERR_STATE;
    }
    uint32_t r = row_of(l, off);
    uint32_t first = r, count;
    if (old_nl == 0 && new_nl == 0) {
        shift_rows(l, r + 1, delta);
        count = 1;
    } else {
        count = rows - r;
    }
    int rc = render_mark_rows(l->grid, first, count);
    if (rc != RENDER_OK) return LAYOUT_ERR_STATE;
    l->row = first; l->row_end = first + count; l->phase = 0;
    return LAYOUT_MORE;
}
