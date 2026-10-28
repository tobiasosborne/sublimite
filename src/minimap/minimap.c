#include "minimap.h"
#include <string.h>
#include <limits.h>
#include <emmintrin.h>

static bool is_space(uint8_t c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

int minimap_init(minimap *m, minimap_row *rows, size_t max_rows)
{
    if (!m || !rows || !max_rows || max_rows > SIZE_MAX / sizeof *rows)
        return MINIMAP_ERR_ARG;
    *m = (minimap){ .rows = rows, .capacity = max_rows,
                   .initialized = true, .stale = true };
    return MINIMAP_OK;
}

int minimap_bind(minimap *m, const void *identity)
{
    if (!m || !m->initialized || !identity) return MINIMAP_ERR_ARG;
    if (m->buffer_identity != identity) {
        m->buffer_identity = identity; m->filled = false; m->stale = true;
    }
    return MINIMAP_OK;
}

void minimap_fini(minimap *m)
{
    if (m) memset(m, 0, sizeof *m);
}

bool minimap_stale(const minimap *m, const minimap_input *input)
{
    /* edit-cxho.9: a window prepared while the index builds stays fresh for
     * the same unindexed source; its line count is only an estimate. */
    if (m && input && m->windowed && !m->window_exact)
        return !m->initialized || !m->filled || m->stale || input->index_ready
            || m->revision != input->revision || m->bytes != input->source.len
            || m->identity.ctx != input->source.ctx || m->identity.span != input->source.span;
    return !m || !input || !m->initialized || !m->filled || m->stale
        || !input->index_ready || m->revision != input->revision
        || m->bytes != input->source.len || m->lines != input->lines
        || m->identity.ctx != input->source.ctx || m->identity.span != input->source.span;
}

/* Bounded non-space byte count. SSE2 is the repository's baseline ISA.
 * Signed comparisons deliberately exclude >=0x80 from the whitespace range. */
static uint64_t count_ink(const uint8_t *p, size_t n, uint64_t ink)
{
    size_t j = 0;
    const __m128i eight = _mm_set1_epi8(8), fourteen = _mm_set1_epi8(14);
    const __m128i blank = _mm_set1_epi8(' ');
    while (n - j >= 16 && ink < MINIMAP_LINE_CAP) {
        __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(p + j));
        __m128i range = _mm_and_si128(_mm_cmpgt_epi8(v, eight), _mm_cmpgt_epi8(fourteen, v));
        unsigned int mask = (unsigned int)_mm_movemask_epi8(_mm_or_si128(range, _mm_cmpeq_epi8(v, blank)));
        ink += 16u - (unsigned int)__builtin_popcount(mask); j += 16;
    }
    while (j < n && ink < MINIMAP_LINE_CAP) ink += !is_space(p[j++]);
    return ink < MINIMAP_LINE_CAP ? ink : MINIMAP_LINE_CAP;
}

/* (G) default tab width from editor/open.c, matching layout's stops.
 * Columns saturate at the existing MINIMAP_LINE_CAP: clipped tails cost no
 * character work. Each non-space column contributes a one-pixel mark. */
static void mark_chars(minimap_row *row, const uint8_t *p, size_t n,
                       uint32_t *column)
{
    for (size_t j = 0; j < n && *column < MINIMAP_LINE_CAP; j++) {
        uint8_t c = p[j];
        if (c == '\t') *column += 4u - *column % 4u;
        else {
            if (!is_space(c)) row->marks[*column / 64u] |= UINT64_C(1) << (*column % 64u);
            (*column)++;
        }
    }
}

static void finish_line(minimap *m, uint64_t line, uint64_t start, uint64_t ink)
{
    minimap_row *r = &m->rows[line / m->lines_per_row];
    if (!r->line_count) { r->first_line = line; r->first_byte = start; }
    r->line_count++;
    r->ink += ink;
}

static int small_rows(minimap *m, const minimap_input *input)
{
    uint64_t off = 0, line = 0, ink = 0, start = 0;
    uint32_t column = 0;
    while (off < input->source.len) {
        const uint8_t *p = NULL;
        size_t n = input->source.span(input->source.ctx, off, &p);
        if (!n || !p) return MINIMAP_ERR_SOURCE;
        if (n > input->source.len - off) n = (size_t)(input->source.len - off);
        size_t j = 0;
        while (j < n) {
            const uint8_t *nl = memchr(p + j, '\n', n - j);
            size_t end = nl ? (size_t)(nl - p) : n;
            if (line < input->lines && line % m->lines_per_row == 0)
                mark_chars(&m->rows[line / m->lines_per_row], p + j, end - j, &column);
            ink = count_ink(p + j, end - j, ink);
            if (!nl) break;
            if (line >= input->lines) return MINIMAP_ERR_SOURCE;
            finish_line(m, line++, start, ink);
            start = off + end + 1; ink = 0; column = 0; j = end + 1;
        }
        off += n;
    }
    if (line != input->lines - 1) return MINIMAP_ERR_SOURCE;
    finish_line(m, line, start, ink);
    for (uint32_t r = 0; r < m->active_rows; r++) {
        minimap_row *v = &m->rows[r];
        v->density = (uint8_t)(v->ink * 255 / (v->line_count * MINIMAP_LINE_CAP));
    }
    return MINIMAP_OK;
}

/* floor(a*b/d), b<=d, without overflowing a*b. */
static uint64_t scale(uint64_t a, uint32_t b, uint32_t d)
{
    return (a / d) * b + (a % d) * b / d;
}

/* edit-cxho.9: window walker. Spans are read forward from a cursor; the
 * backward search gathers fixed blocks of MINIMAP_LINE_CAP + 1 bytes (one
 * summary-width line and its newline), so a short line costs one block. */
#define WINDOW_BLOCK ((size_t)MINIMAP_LINE_CAP + 1u)
enum { WALK_LINE = 0, WALK_EOF = 1, WALK_CLIPPED = 2 };

/* Copy [off, off+n) into out; n <= WINDOW_BLOCK. */
static int gather(const lineidx_src *s, uint64_t off, size_t n, uint8_t *out)
{
    size_t done = 0;
    while (done < n) {
        const uint8_t *p = NULL;
        size_t got = s->span(s->ctx, off + done, &p);
        if (!got || !p) return MINIMAP_ERR_SOURCE;
        if (got > n - done) got = n - done;
        memcpy(out + done, p, got); done += got;
    }
    return MINIMAP_OK;
}

/* Start of the line holding byte pos - 1 ... i.e. one past the last '\n' in
 * [pos - MINIMAP_SCAN_BYTES, pos), 0 at the file start. *found false: no
 * newline within the cap (a line longer than the scan bound). */
static int line_start_before(const lineidx_src *s, uint64_t pos, uint64_t *start, bool *found)
{
    uint8_t block[WINDOW_BLOCK];
    uint64_t scanned = 0;
    while (pos > 0 && scanned < MINIMAP_SCAN_BYTES) {
        size_t n = pos < WINDOW_BLOCK ? (size_t)pos : WINDOW_BLOCK;
        int rc = gather(s, pos - n, n, block);
        if (rc != MINIMAP_OK) return rc;
        size_t j = n;
        while (j > 0 && block[j - 1u] != '\n') j--; /* no memrchr on every target */
        if (j) { *start = pos - n + j; *found = true; return MINIMAP_OK; }
        pos -= n; scanned += n;
    }
    *start = pos; *found = pos == 0;
    return MINIMAP_OK;
}

typedef struct walk_cursor {
    const lineidx_src *s;
    uint64_t off;             /* offset of p[0] */
    const uint8_t *p; size_t n;
} walk_cursor;

/* Summarise the line starting at c->off into row and advance past its
 * newline: marks from the first MINIMAP_LINE_CAP bytes, ink counted over
 * the same prefix, the newline searched for at most MINIMAP_SCAN_BYTES. */
static int walk_line(walk_cursor *c, minimap_row *row)
{
    uint64_t scanned = 0, ink = 0; uint32_t column = 0;
    for (;;) {
        if (c->off >= c->s->len) { row->ink = ink; return WALK_EOF; }
        if (!c->n) {
            const uint8_t *p = NULL;
            size_t n = c->s->span(c->s->ctx, c->off, &p);
            if (!n || !p) return MINIMAP_ERR_SOURCE;
            if (n > c->s->len - c->off) n = (size_t)(c->s->len - c->off);
            c->p = p; c->n = n;
        }
        size_t limit = c->n;
        if (limit > MINIMAP_SCAN_BYTES - scanned) limit = (size_t)(MINIMAP_SCAN_BYTES - scanned);
        const uint8_t *nl = memchr(c->p, '\n', limit);
        size_t body = nl ? (size_t)(nl - c->p) : limit;
        if (scanned < MINIMAP_LINE_CAP) {
            size_t k = body;
            if (k > MINIMAP_LINE_CAP - scanned) k = (size_t)(MINIMAP_LINE_CAP - scanned);
            ink = count_ink(c->p, k, ink);
            mark_chars(row, c->p, k, &column);
        }
        scanned += body;
        size_t step = nl ? body + 1u : body;
        c->p += step; c->n -= step; c->off += step;
        if (nl) { row->ink = ink; return WALK_LINE; }
        if (scanned >= MINIMAP_SCAN_BYTES) { row->ink = ink; return WALK_CLIPPED; }
    }
}

static int window_rows(minimap *m, const lineidx_src *src, const minimap_anchor *a,
                       bool exact, uint32_t height)
{
    const minimap_window *w = &a->window;
    uint64_t back = exact ? a->first_line - w->first_line : w->above;
    uint64_t pos = a->first_byte < src->len ? a->first_byte : src->len, start = 0;
    bool found = false;
    int rc = line_start_before(src, pos, &start, &found);
    if (rc != MINIMAP_OK) return rc;
    if (!found) { start = pos; m->window_exact = false; } /* anchor line over the scan bound */
    uint64_t walked = 0;
    while (walked < back && start > 0) {
        uint64_t prev = 0;
        rc = line_start_before(src, start - 1u, &prev, &found);
        if (rc != MINIMAP_OK) return rc;
        if (!found) break; /* the line above is over the scan bound */
        start = prev; walked++;
    }
    uint64_t label = a->first_line > walked ? a->first_line - walked : 0;
    uint32_t want = w->rows < height ? w->rows : height;
    walk_cursor c = {src, start, NULL, 0};
    m->active_rows = 0;
    for (uint32_t i = 0; i < want; i++) {
        minimap_row *r = &m->rows[i];
        r->first_line = label + i; r->first_byte = c.off; r->line_count = 1;
        rc = walk_line(&c, r);
        if (rc < 0) return rc;
        r->density = (uint8_t)(r->ink * 255 / MINIMAP_LINE_CAP);
        m->active_rows = i + 1u;
        if (rc != WALK_LINE) break;
    }
    return MINIMAP_OK;
}

/* Shared tail of minimap_prepare and minimap_prepare_window. */
static int window_prepare(minimap *m, const minimap_input *input, const lineidx_src *src,
                          const minimap_anchor *anchor, uint32_t height)
{
    m->stale = true; m->filled = false; m->height = height;
    m->lines_per_row = 1; m->sampled = true; m->windowed = true;
    m->window_exact = input->index_ready && anchor->window.first_line != MINIMAP_LINE_UNKNOWN
                   && anchor->first_line >= anchor->window.first_line;
    m->anchor = *anchor;
    memset(m->rows, 0, (size_t)height * sizeof *m->rows);
    int rc = window_rows(m, src, anchor, m->window_exact, height);
    if (rc != MINIMAP_OK) return rc;
    m->identity = input->source;
    m->revision = input->revision; m->bytes = input->source.len; m->lines = input->lines;
    m->filled = true; m->stale = false;
    return MINIMAP_OK;
}

int minimap_prepare_window(minimap *m, const minimap_input *input,
                           const lineidx_src *snapshot_source,
                           const minimap_anchor *anchor, uint32_t height)
{
    if (!m || !m->initialized || !input || !input->lines || !height || !anchor
        || !anchor->window.scale || (input->source.len && !input->source.span))
        return MINIMAP_ERR_ARG;
    const lineidx_src *src = snapshot_source ? snapshot_source : &input->source;
    if (src->len != input->source.len || (src->len && !src->span)) return MINIMAP_ERR_ARG;
    if (m->capacity < height) return MINIMAP_ERR_CAPACITY;
    return window_prepare(m, input, src, anchor, height);
}

bool minimap_anchor_moved(const minimap *m, const minimap_anchor *anchor)
{
    if (!m || !anchor || !m->windowed) return true;
    const minimap_window *a = &m->anchor.window, *b = &anchor->window;
    return m->anchor.first_line != anchor->first_line || m->anchor.first_byte != anchor->first_byte
        || a->first_line != b->first_line || a->range != b->range || a->above != b->above
        || a->phase != b->phase || a->rows != b->rows || a->box_top != b->box_top
        || a->box_height != b->box_height || a->travel != b->travel || a->scale != b->scale
        || a->height != b->height || a->scrolls != b->scrolls;
}

int minimap_prepare(minimap *m, const minimap_input *input, uint32_t height)
{
    if (!m || !m->initialized || !input || !input->lines || !height
        || !input->index_ready || (input->source.len && !input->source.span))
        return MINIMAP_ERR_ARG;
    if (m->capacity < height) return MINIMAP_ERR_CAPACITY;
    if (input->source.len > MINIMAP_SMALL_BYTES || input->lines > MINIMAP_SMALL_LINES) {
        /* edit-cxho.9: a large file's whole-map rows are the window at line
         * 0, one real line per row (scale 1, height rows), not samples. */
        minimap_anchor top = {0};
        (void)minimap_window_lines(input->lines, 0, 0, 1, height, &top.window);
        return window_prepare(m, input, &input->source, &top, height);
    }
    m->stale = true; m->filled = false; m->height = height;
    m->lines_per_row = input->lines / height + (input->lines % height != 0);
    m->active_rows = (uint32_t)(input->lines / m->lines_per_row
                        + (input->lines % m->lines_per_row != 0));
    memset(m->rows, 0, (size_t)height * sizeof *m->rows);
    m->sampled = false; m->windowed = false; m->window_exact = false;
    int rc = small_rows(m, input);
    if (rc != MINIMAP_OK) return rc;
    m->identity = input->source;
    m->revision = input->revision; m->bytes = input->source.len; m->lines = input->lines;
    m->filled = true; m->stale = false;
    return MINIMAP_OK;
}

int minimap_publish(minimap *m, const minimap_input *input, const minimap *prepared)
{
    if (!m || !m->initialized || !prepared || !prepared->initialized || m == prepared)
        return MINIMAP_ERR_ARG;
    if (minimap_stale(prepared, input) || m->buffer_identity != prepared->buffer_identity)
        return MINIMAP_ERR_STALE;
    if (m->capacity < prepared->height) return MINIMAP_ERR_CAPACITY;
    /* The two caller-owned row arrays must not overlap. */
    minimap_row *rows = m->rows; size_t capacity = m->capacity;
    memcpy(rows, prepared->rows, (size_t)prepared->height * sizeof *rows);
    *m = *prepared; m->rows = rows; m->capacity = capacity;
    return MINIMAP_OK;
}

static uint32_t blend(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t color = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        uint32_t av = (a >> shift) & 255u, bv = (b >> shift) & 255u;
        color |= ((av * (255 - t) + bv * t + 127) / 255) << shift;
    }
    return color;
}

static int validate(const minimap *m, const minimap_input *input,
                    const render_grid *g, uint32_t col, uint32_t width,
                    const minimap_style *s)
{
    if (!m || !m->initialized || !input || !input->lines || !g || !s
        || !g->begun || !g->dims.cols || !g->dims.rows || !g->cells || !g->dirty
        || !g->dims.cell_w || !g->dims.cell_h
        || g->dims.cols > (uint32_t)INT32_MAX / g->dims.cell_w
        || g->dims.rows > (uint32_t)INT32_MAX / g->dims.cell_h
        || !width || col >= g->dims.cols || width > g->dims.cols - col
        || (input->source.len && !input->source.span)
        || ((s->background | s->density | s->viewport | s->stale) & 0xff000000u))
        return MINIMAP_ERR_ARG;
    if (m->capacity < g->dims.rows
        || g->dims.rows > SIZE_MAX / g->dims.cols
        || (size_t)g->dims.rows * g->dims.cols > SIZE_MAX / sizeof(render_cell)
        || g->cell_capacity < (size_t)g->dims.rows * g->dims.cols
        || g->dirty_word_capacity < (size_t)(g->dims.rows / 64u) + (g->dims.rows % 64u != 0))
        return MINIMAP_ERR_CAPACITY;
    return MINIMAP_OK;
}

bool minimap_pending_band(uint64_t lines, uint32_t rows, uint32_t r,
                          uint64_t first_line, uint64_t end_line)
{
    if (!rows || r >= rows || first_line >= end_line) return false;
    /* edit-cxho.1 (P): fewer lines than rows maps one line per row, as
     * minimap_prepare groups them (lines_per_row = 1), so row r is line r and
     * rows past the last line hold none; overlap is half-open, as for ready
     * summaries. Otherwise row r spans floor(lines * r / rows) up to the next
     * row's start, with the existing inclusive lower-bound overlap rule. */
    if (lines < rows) return r < lines && r < end_line && first_line <= r;
    uint64_t row_line = scale(lines, r, rows);
    uint64_t next_line = scale(lines, r + 1, rows);
    return row_line < end_line && first_line <= next_line;
}

/* pending: fill_cached's cache miss (stale or resized): never prepare and
 * paint the retained or pending map, as with an unready index. */
static int fill(minimap *m, const minimap_input *input, render_grid *grid,
                uint32_t first_col, uint32_t width,
                uint64_t first_line, uint64_t visible_lines,
                const minimap_style *style, bool pending)
{
    int rc = validate(m, input, grid, first_col, width, style);
    if (rc != MINIMAP_OK) return rc;
    bool retained = m->filled && m->height == grid->dims.rows
        && m->identity.ctx == input->source.ctx && m->identity.span == input->source.span;
    bool fresh = !pending && retained && !minimap_stale(m, input);
    if (!pending && input->index_ready && !fresh) {
        rc = minimap_prepare(m, input, grid->dims.rows);
        if (rc != MINIMAP_OK) return rc;
        retained = true;
    } else m->stale = !fresh; /* edit-cxho.9: a fresh unindexed window is ready */
    uint64_t end = first_line;
    if (first_line < input->lines) {
        uint64_t remaining = input->lines - first_line;
        end += visible_lines < remaining ? visible_lines : remaining;
    }
    if (!m->stale) {
        m->painted_first = first_line; m->painted_end = end;
    } else if (retained) {
        /* Keep the last ready image, including its viewport, while the index
         * catches up. Recompose from per-buffer summaries so a tab switch
         * cannot retain another buffer's cells in a shared render grid. */
        first_line = m->painted_first; end = m->painted_end;
    }
    uint32_t background = style->background, ink = style->density;
    /* Without compatible ready summaries, retain the quiet approximate
     * scrollbar density. Ready and retained rows use character marks. */
    uint32_t occupied = 0;
    if (m->stale && !retained) {
        ink = blend(background, ink, 64);
        occupied = (uint32_t)(((uint64_t)64 * width + 254) / 255);
    }
    /* (E) wash = existing 128/4 = 32 coverage; edge = 3*32 = 96. Both
     * below half opacity. Pixel-exact perimeter is in minimap_pixels;
     * this legacy adapter can resolve only whole render cells. */
    uint32_t wash_bg = blend(background, style->viewport, style->hover ? 48u : 32u);
    uint32_t wash_ink = blend(ink, style->viewport, style->hover ? 48u : 32u);
    uint32_t edge_bg = blend(background, style->viewport, 96);
    uint32_t edge_ink = blend(ink, style->viewport, 96);
    for (uint32_t r = 0; r < grid->dims.rows; r++) {
        const minimap_row *v = retained ? &m->rows[r] : NULL;
        bool viewport = v && v->line_count && first_line < end
                     && v->first_line < end && first_line < v->first_line + v->line_count;
        bool top = viewport && (r == 0 || m->rows[r - 1].first_line
                         + m->rows[r - 1].line_count <= first_line);
        bool bottom = viewport && (r + 1 == m->active_rows
                                  || m->rows[r + 1].first_line >= end);
        if (m->stale && !retained)
            viewport = minimap_pending_band(input->lines, grid->dims.rows, r, first_line, end);
        render_cell *dst = &grid->cells[(size_t)r * grid->dims.cols + first_col];
        bool changed = false;
        for (uint32_t c = 0; c < width; c++) {
            bool marked = v ? c < MINIMAP_LINE_CAP
                && (v->marks[c / 64u] & (UINT64_C(1) << (c % 64u)))
                : c < occupied;
            bool edge = viewport && (top || bottom || c == 0 || c + 1 == width);
            render_cell next = { .atlas_slot = RENDER_NO_SLOT, .fg = style->density,
                                 .bg = edge ? (marked ? edge_ink : edge_bg)
                                            : viewport ? (marked ? wash_ink : wash_bg)
                                                       : (marked ? ink : background) };
            /* One existing strip column is the narrowest cell treatment;
             * pending edits with retained summaries get no indicator. */
            if (m->stale && !retained && c == width - 1u) next.bg = style->stale;
            if (memcmp(&dst[c], &next, sizeof next) != 0) {
                dst[c] = next; changed = true;
            }
        }
        if (changed) (void)render_mark_rows(grid, r, 1);
    }
    return MINIMAP_OK;
}
int minimap_fill(minimap *m, const minimap_input *input, render_grid *grid,
                 uint32_t first_col, uint32_t width,
                 uint64_t first_line, uint64_t visible_lines,
                 const minimap_style *style)
{
    return fill(m, input, grid, first_col, width, first_line, visible_lines, style, false);
}

int minimap_pixels(const minimap *m, uint32_t *pixels, size_t capacity,
                   uint32_t width, uint32_t height, size_t stride,
                   uint64_t first_line, uint64_t visible_lines,
                   const minimap_style *style)
{
    if (!m || !m->initialized || !m->filled || !pixels || !style
        || !width || !height || stride < width || height > (uint64_t)m->height * 2
        || ((style->background | style->density | style->viewport | style->stale) & 0xff000000u))
        return MINIMAP_ERR_ARG;
    if ((size_t)(height - 1) > (SIZE_MAX - width) / stride
        || capacity < (size_t)(height - 1) * stride + width)
        return MINIMAP_ERR_CAPACITY;
    if (m->stale) {
        first_line = m->painted_first;
        visible_lines = m->painted_end - m->painted_first;
    }
    uint64_t end = first_line;
    if (first_line < m->lines) {
        uint64_t remaining = m->lines - first_line;
        end += visible_lines < remaining ? visible_lines : remaining;
    }
    /* (G) two pixel scanlines per summary, derived from perf §2.11.
     * The viewport uses the same grouped lines as hit/row_for_line. */
    uint32_t top = height, bottom = 0;
    for (uint32_t r = 0; r < m->active_rows && (uint64_t)r * 2 < height; r++) {
        const minimap_row *v = &m->rows[r];
        if (first_line < end && v->first_line < end && first_line < v->first_line + v->line_count) {
            if (top == height) top = r * 2;
            bottom = (uint32_t)((uint64_t)(r + 1) * 2 < height ? (uint64_t)(r + 1) * 2 : height);
        }
    }
    uint32_t wash_bg = blend(style->background, style->viewport, style->hover ? 48u : 32u);
    uint32_t wash_ink = blend(style->density, style->viewport, style->hover ? 48u : 32u);
    uint32_t edge_bg = blend(style->background, style->viewport, 96);
    uint32_t edge_ink = blend(style->density, style->viewport, 96);
    for (uint32_t y = 0; y < height; y++) {
        const minimap_row *v = &m->rows[y / 2];
        bool band = y >= top && y < bottom;
        for (uint32_t x = 0; x < width; x++) {
            bool mark = y % 2 == 0 && x < MINIMAP_LINE_CAP
                && (v->marks[x / 64u] & (UINT64_C(1) << (x % 64u)));
            bool edge = band && (y == top || y + 1 == bottom || x == 0 || x + 1 == width);
            pixels[(size_t)y * stride + x] = edge ? (mark ? edge_ink : edge_bg)
                : band ? (mark ? wash_ink : wash_bg) : mark ? style->density : style->background;
        }
    }
    return MINIMAP_OK;
}

int minimap_hit(const minimap *m, const minimap_input *input, lineidx *idx,
                int64_t y, minimap_target *out)
{
    if (!m || !input || !out) return MINIMAP_ERR_ARG;
    if (minimap_stale(m, input)) return MINIMAP_ERR_STALE;
    uint32_t row = y < 0 ? 0 : (uint64_t)y >= m->active_rows ? m->active_rows - 1 : (uint32_t)y;
    const minimap_row *v = &m->rows[row];
    /* edit-cxho.9: every row is a real line start found by the walk (or
     * small-file grouping); no index query. idx is kept for the API. */
    (void)idx;
    minimap_target target = {v->first_line, v->first_byte, !m->windowed || m->window_exact};
    *out = target;
    return MINIMAP_OK;
}

int minimap_row_for_line(const minimap *m, uint64_t line, uint32_t *out)
{
    if (!m || !out || !m->initialized || !m->filled) return MINIMAP_ERR_ARG;
    if (m->stale) return MINIMAP_ERR_STALE;
    if (line >= m->lines) line = m->lines - 1;
    /* Windowed rows start at rows[0].first_line (0 for small maps). */
    uint64_t base = m->active_rows ? m->rows[0].first_line : 0;
    uint64_t row = line > base ? (line - base) / m->lines_per_row : 0;
    *out = (uint32_t)(m->active_rows && row >= m->active_rows ? m->active_rows - 1u : row);
    return MINIMAP_OK;
}

int minimap_hit_approx(const minimap_input *input, uint32_t height,
                       int64_t y, minimap_target *out)
{
    if (!input || !input->lines || !height || !out) return MINIMAP_ERR_ARG;
    uint32_t row = y < 0 ? 0 : (uint64_t)y >= height ? height - 1 : (uint32_t)y;
    uint32_t divisor = height > 1 ? height - 1 : 1;
    uint64_t line = scale(input->lines - 1, row, divisor);
    *out = (minimap_target){line, scale(input->source.len, row, divisor), false};
    return MINIMAP_OK;
}

int minimap_fill_cached(minimap *m, const minimap_input *input, render_grid *grid,
                        uint32_t first_col, uint32_t width,
                        uint64_t first_line, uint64_t visible_lines,
                        const minimap_style *style)
{
    int rc = validate(m, input, grid, first_col, width, style);
    if (rc != MINIMAP_OK) return rc;
    if (!m) return MINIMAP_ERR_ARG;
    bool pending = m->height != grid->dims.rows || minimap_stale(m, input);
    return fill(m, input, grid, first_col, width, first_line, visible_lines, style, pending);
}

int minimap_prepare_source(minimap *m, const minimap_input *input,
                            const lineidx_src *snapshot_source, uint32_t height)
{
    if (!input || !snapshot_source || snapshot_source->len != input->source.len)
        return MINIMAP_ERR_ARG;
    minimap_input worker_input = *input; worker_input.source = *snapshot_source;
    int rc = minimap_prepare(m, &worker_input, height);
    if (rc == MINIMAP_OK) m->identity = input->source;
    return rc;
}

/* edit-cxho.8: fixed-scale scrolling window. Pure integer arithmetic; the
 * rounding is documented in minimap.h and docs/decisions/edit-cxho.8.md. */
/* (P) a * b / c in 128 bits (gcc/clang, the only toolchains, incl. llvm-mingw);
 * callers keep a <= c or b <= c so the quotient fits in 64 bits. */
static uint64_t window_mul_div(uint64_t a, uint64_t b, uint64_t c, bool up)
{
    unsigned __int128 p = (unsigned __int128)a * b;
    return (uint64_t)(p / c) + (up && p % c != 0);
}
static void window_place(minimap_window *w, uint64_t first, uint64_t range,
                         uint32_t top, uint32_t box, uint32_t travel,
                         uint32_t scale, uint32_t height, bool scrolls)
{
    /* The viewport's first line sits at y = top: ceil(top / scale) whole or
     * partial lines above it; phase clips the topmost one. */
    uint32_t above = top / scale + (top % scale != 0);
    uint32_t phase = above * scale - top;
    uint64_t slots = ((uint64_t)height + phase + scale - 1u) / scale;
    *w = (minimap_window){ .first_line = first, .range = range, .above = above,
        .phase = phase, .rows = (uint32_t)slots, .box_top = top, .box_height = box,
        .travel = travel, .scale = scale, .height = height, .scrolls = scrolls };
}
static uint32_t window_box(uint64_t visible, uint32_t scale, uint32_t height)
{
    return visible > height / scale ? height : (uint32_t)visible * scale;
}
int minimap_window_lines(uint64_t lines, uint64_t visible, uint64_t first_line,
                         uint32_t scale, uint32_t height, minimap_window *out)
{
    if (!scale || !out) return MINIMAP_ERR_ARG;
    uint32_t box = window_box(visible, scale, height), spare = height - box;
    uint64_t range = lines > visible ? lines - visible : 0;
    /* range * scale > spare without overflow. Not scrolling, the product
     * fits in spare, so the travel is exactly range lines of scale px. */
    bool scrolls = range > spare / scale;
    uint32_t travel = scrolls ? spare : (uint32_t)range * scale;
    uint64_t first = first_line < range ? first_line : range;
    uint32_t top = range ? (uint32_t)window_mul_div(first, travel, range, false) : 0;
    window_place(out, first, range, top, box, travel, scale, height, scrolls);
    /* above <= first: top <= first * scale (travel <= range * scale). */
    out->first_line = first - out->above;
    uint64_t left = lines - out->first_line;
    if (out->rows > left) out->rows = (uint32_t)left;
    return MINIMAP_OK;
}
int minimap_window_bytes(uint64_t bytes, uint64_t visible, uint64_t first_byte,
                         uint32_t scale, uint32_t height, minimap_window *out)
{
    if (!scale || !out) return MINIMAP_ERR_ARG;
    uint32_t box = window_box(visible, scale, height), travel = height - box;
    uint64_t first = first_byte < bytes ? first_byte : bytes;
    uint32_t top = bytes ? (uint32_t)window_mul_div(first, travel, bytes, false) : 0;
    window_place(out, MINIMAP_LINE_UNKNOWN, bytes, top, box, travel, scale, height, true);
    return MINIMAP_OK;
}
bool minimap_window_grab(const minimap_window *w, int64_t y, int64_t *offset)
{
    if (!w || !offset) return false;
    bool inside = y >= (int64_t)w->box_top && y - (int64_t)w->box_top < (int64_t)w->box_height;
    *offset = inside ? y - (int64_t)w->box_top : (int64_t)(w->box_height / 2u);
    return inside;
}
uint64_t minimap_window_seek(const minimap_window *w, int64_t y, int64_t offset)
{
    if (!w || !w->travel) return 0;
    int64_t t; /* y - offset, saturating */
    if (offset > 0 && y < INT64_MIN + offset) t = INT64_MIN;
    else if (offset < 0 && y > INT64_MAX + offset) t = INT64_MAX;
    else t = y - offset;
    uint64_t top = t <= 0 ? 0 : (uint64_t)t > w->travel ? w->travel : (uint64_t)t;
    return window_mul_div(top, w->range, w->travel, true);
}

/* edit-cxho.10: window-geometry pixel dump; see minimap.h. */
int minimap_pixels_window(const minimap *m, uint32_t *pixels, size_t capacity,
                          uint32_t width, uint32_t height, size_t stride,
                          const minimap_frame *f, const minimap_style *style)
{
    if (!pixels || !f || !style || !f->scale || !width || !height || stride < width
        || (!f->pending && (!m || !m->initialized || !m->filled))
        || ((style->background | style->density | style->viewport | style->stale) & 0xff000000u))
        return MINIMAP_ERR_ARG;
    if ((size_t)(height - 1) > (SIZE_MAX - width) / stride
        || capacity < (size_t)(height - 1) * stride + width)
        return MINIMAP_ERR_CAPACITY;
    uint32_t ink = style->density;
    uint32_t occupied = 0;
    int64_t ink_top = 0, ink_end = 0; /* pending: scanlines holding a line */
    if (f->pending) {
        ink = blend(style->background, ink, 64);
        occupied = (uint32_t)(((uint64_t)64 * width + 254) / 255);
        /* (P) lines 0..lines-1 lie line*scale above and (lines-line)*scale
         * below line_y; a span beyond the strip saturates to it. */
        uint64_t span = (uint64_t)height + (uint64_t)(f->line_y < 0 ? -f->line_y : f->line_y);
        uint64_t up = f->line < span / f->scale ? f->line * f->scale : span;
        uint64_t below = f->lines > f->line ? f->lines - f->line : 0;
        uint64_t down = below < span / f->scale ? below * f->scale : span;
        ink_top = f->line_y - (int64_t)up; ink_end = f->line_y + (int64_t)down;
    }
    uint32_t wash_bg = blend(style->background, style->viewport, style->hover ? 48u : 32u);
    uint32_t wash_ink = blend(ink, style->viewport, style->hover ? 48u : 32u);
    uint32_t edge_bg = blend(style->background, style->viewport, 96);
    uint32_t edge_ink = blend(ink, style->viewport, 96);
    int64_t top = f->box_top, bottom = f->box_top + (int64_t)f->box_height;
    for (uint32_t y = 0; y < height; y++) {
        bool band = (int64_t)y >= top && (int64_t)y < bottom;
        bool edge_row = band && ((int64_t)y == top || (int64_t)y + 1 == bottom);
        bool lined = f->pending && (int64_t)y >= ink_top && (int64_t)y < ink_end;
        uint32_t *out = pixels + (size_t)y * stride;
        for (uint32_t x = 0; x < width; x++) {
            bool mark = lined && x < occupied;
            bool edge = band && (edge_row || x == 0 || x + 1 == width);
            out[x] = edge ? (mark ? edge_ink : edge_bg)
                : band ? (mark ? wash_ink : wash_bg) : mark ? ink : style->background;
            if (f->pending && x + 1 == width) out[x] = style->stale;
        }
    }
    if (f->pending) return MINIMAP_OK;
    /* Ready rows: marks on each line's first scanline. A row whose label is
     * more than the strip away from line cannot touch it. */
    uint64_t reach = (uint64_t)height / f->scale + 1u
        + (uint64_t)(f->line_y < 0 ? -f->line_y : f->line_y) / f->scale;
    for (uint32_t r = 0; r < m->active_rows; r++) {
        const minimap_row *v = &m->rows[r];
        if (!v->line_count) continue;
        uint64_t d = v->first_line > f->line ? v->first_line - f->line : f->line - v->first_line;
        if (d > reach) continue;
        int64_t y = f->line_y + (v->first_line >= f->line ? 1 : -1) * (int64_t)d * f->scale;
        if (y < 0 || y >= (int64_t)height) continue;
        bool band = y >= top && y < bottom;
        bool edge_row = band && (y == top || y + 1 == bottom);
        uint32_t *out = pixels + (size_t)y * stride;
        for (uint32_t x = 0; x < width && x < MINIMAP_LINE_CAP; x++) {
            if (!(v->marks[x / 64u] & (UINT64_C(1) << (x % 64u)))) continue;
            bool edge = band && (edge_row || x == 0 || x + 1 == width);
            out[x] = edge ? edge_ink : band ? wash_ink : ink;
        }
    }
    return MINIMAP_OK;
}
