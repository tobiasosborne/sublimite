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

static int sample_ink(const lineidx_src *s, uint64_t off, minimap_row *out)
{
    uint8_t scratch[MINIMAP_SAMPLE_BYTES];
    const uint8_t *sample = scratch;
    size_t have = (size_t)(s->len - off < MINIMAP_SAMPLE_BYTES
                        ? s->len - off : MINIMAP_SAMPLE_BYTES);
    uint64_t boundary = off;
    if (have) {
        const uint8_t *p = NULL;
        size_t n = s->span(s->ctx, off, &p);
        if (!n || !p) return MINIMAP_ERR_SOURCE;
        if (n >= have) sample = p;
        else {
            /* Fragmented piece source: gather only the fixed sampling bound.
             * Contiguous/mapped sources take the zero-copy fast path above. */
            size_t done = 0;
            for (;;) {
                if (n > have - done) n = have - done;
                memcpy(scratch + done, p, n); done += n; off += n;
                if (done == have) break;
                p = NULL; n = s->span(s->ctx, off, &p);
                if (!n || !p) return MINIMAP_ERR_SOURCE;
            }
        }
    }
    size_t begin = 0;
    /* Boundary zero is already a line start. At other boundaries skip the
     * partial line; absent a newline, use the clipped long-line fragment. */
    if (boundary != 0) {
        const uint8_t *nl = memchr(sample, '\n', have);
        if (nl) begin = (size_t)(nl - sample) + 1;
    }
    const uint8_t *nl = memchr(sample + begin, '\n', have - begin);
    size_t end = nl ? (size_t)(nl - sample) : have;
    out->ink = count_ink(sample + begin, end - begin, 0);
    uint32_t column = 0;
    mark_chars(out, sample + begin, end - begin, &column);
    return MINIMAP_OK;
}

static int large_rows(minimap *m, const minimap_input *input)
{
    uint64_t chunks = input->source.len / LINEIDX_CHUNK
                    + (input->source.len % LINEIDX_CHUNK != 0);
    uint32_t samples = m->active_rows < MINIMAP_MAX_SAMPLES ? m->active_rows : MINIMAP_MAX_SAMPLES;
    if (chunks && chunks < samples) samples = (uint32_t)chunks;
    uint32_t previous = UINT32_MAX;
    uint64_t boundary = 0; minimap_row sample = {0};
    for (uint32_t r = 0; r < m->active_rows; r++) {
        minimap_row *v = &m->rows[r];
        v->first_line = (uint64_t)r * m->lines_per_row;
        v->line_count = input->lines - v->first_line;
        if (v->line_count > m->lines_per_row) v->line_count = m->lines_per_row;
        uint32_t group = (uint32_t)((uint64_t)samples * r / m->active_rows);
        if (group != previous) {
            boundary = scale(chunks, group, samples) * LINEIDX_CHUNK;
            memset(&sample, 0, sizeof sample);
            int rc = sample_ink(&input->source, boundary, &sample);
            if (rc != MINIMAP_OK) return rc;
            previous = group;
        }
        v->first_byte = boundary; v->ink = sample.ink;
        memcpy(v->marks, sample.marks, sizeof v->marks);
        v->density = (uint8_t)(sample.ink * 255 / MINIMAP_LINE_CAP);
    }
    return MINIMAP_OK;
}

int minimap_prepare(minimap *m, const minimap_input *input, uint32_t height)
{
    if (!m || !m->initialized || !input || !input->lines || !height
        || !input->index_ready || (input->source.len && !input->source.span))
        return MINIMAP_ERR_ARG;
    if (m->capacity < height) return MINIMAP_ERR_CAPACITY;
    m->stale = true; m->filled = false; m->height = height;
    m->lines_per_row = input->lines / height + (input->lines % height != 0);
    m->active_rows = (uint32_t)(input->lines / m->lines_per_row
                        + (input->lines % m->lines_per_row != 0));
    memset(m->rows, 0, (size_t)height * sizeof *m->rows);
    m->sampled = input->source.len > MINIMAP_SMALL_BYTES || input->lines > MINIMAP_SMALL_LINES;
    int rc = m->sampled ? large_rows(m, input) : small_rows(m, input);
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

int minimap_fill(minimap *m, const minimap_input *input, render_grid *grid,
                 uint32_t first_col, uint32_t width,
                 uint64_t first_line, uint64_t visible_lines,
                 const minimap_style *style)
{
    int rc = validate(m, input, grid, first_col, width, style);
    if (rc != MINIMAP_OK) return rc;
    bool retained = m->filled && m->height == grid->dims.rows
        && m->identity.ctx == input->source.ctx && m->identity.span == input->source.span;
    bool fresh = retained && !minimap_stale(m, input);
    if (input->index_ready && !fresh) {
        rc = minimap_prepare(m, input, grid->dims.rows);
        if (rc != MINIMAP_OK) return rc;
        retained = true;
    } else m->stale = !input->index_ready;
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
    uint32_t wash_bg = blend(background, style->viewport, 32);
    uint32_t wash_ink = blend(ink, style->viewport, 32);
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
        if (m->stale && !retained) {
            uint64_t row_line = scale(input->lines, r, grid->dims.rows);
            uint64_t next_line = scale(input->lines, r + 1, grid->dims.rows);
            viewport = first_line < end && row_line < end
                && first_line <= next_line;
        }
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
    uint32_t wash_bg = blend(style->background, style->viewport, 32);
    uint32_t wash_ink = blend(style->density, style->viewport, 32);
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
    minimap_target target = {v->first_line, v->first_byte, true};
    if (m->sampled) {
        if (!idx || lineidx_len(idx) != input->source.len) return MINIMAP_ERR_ARG;
        lineidx_result pos = lineidx_line_to_byte(idx, &input->source, target.line);
        target.byte = pos.value; target.exact = pos.exact;
    }
    *out = target;
    return MINIMAP_OK;
}

int minimap_row_for_line(const minimap *m, uint64_t line, uint32_t *out)
{
    if (!m || !out || !m->initialized || !m->filled) return MINIMAP_ERR_ARG;
    if (m->stale) return MINIMAP_ERR_STALE;
    if (line >= m->lines) line = m->lines - 1;
    *out = (uint32_t)(line / m->lines_per_row);
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
    minimap_input pending = *input;
    if (m->height != grid->dims.rows || minimap_stale(m, input)) {
        pending.index_ready = false;
    }
    return minimap_fill(m, &pending, grid, first_col, width,
                        first_line, visible_lines, style);
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
