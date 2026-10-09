/* Scalar reference and SSE2 cell blit (P2.5). Blend per render.h:
 * (fg*a + bg*(255-a) + 127)/255 per channel over R8 coverage. */
#include "raster/raster.h"
#include <emmintrin.h>
#include <string.h>

static uint32_t blend_px(uint32_t fg, uint32_t bg, uint32_t a)
{
    uint32_t out = 0;
    for (unsigned sh = 0; sh < 24; sh += 8) {
        uint32_t f = (fg >> sh) & 255u, b = (bg >> sh) & 255u;
        out |= ((f * a + b * (255u - a) + 127u) / 255u) << sh;
    }
    return out;
}

typedef struct cell_geom {
    uint32_t fg, bg, x0, rw, gw, gh;
    const uint8_t *cov;     /* glyph rect origin or NULL */
    size_t cov_stride;
    bool underline;
} cell_geom;

/* Returns false when the cell draws nothing itself (wide continuation). */
static bool cell_setup(const raster_scene *s, uint32_t col, const render_cell *c, cell_geom *g)
{
    if (c->attrs & RENDER_ATTR_WIDE_RIGHT) return false;
    uint32_t cw = s->dims.cell_w, ch = s->dims.cell_h;
    g->rw = (c->attrs & RENDER_ATTR_WIDE_LEFT) ? cw * 2u : cw;
    g->x0 = col * cw;
    g->fg = c->fg; g->bg = c->bg;
    if (c->attrs & RENDER_ATTR_INVERSE) { g->fg = c->bg; g->bg = c->fg; }
    g->underline = (c->attrs & RENDER_ATTR_UNDERLINE) != 0;
    g->cov = NULL; g->gw = g->gh = 0; g->cov_stride = 0;
    if (c->atlas_slot != RENDER_NO_SLOT && c->atlas_slot < s->glyph_count) {
        const render_glyph *gl = &s->glyphs[c->atlas_slot];
        if (gl->page < s->page_count) {
            const render_atlas_page *pg = &s->pages[gl->page];
            g->gw = gl->w < g->rw ? gl->w : g->rw;
            g->gh = gl->h < ch ? gl->h : ch;
            g->cov = pg->pixels + (size_t)gl->y * pg->stride + gl->x;
            g->cov_stride = pg->stride;
        }
    }
    return true;
}

void raster_row_scalar(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row)
{
    uint32_t cols = s->dims.cols, ch = s->dims.cell_h;
    for (uint32_t col = 0; col < cols; col++) {
        const render_cell *c = &s->cells[(size_t)row * cols + col];
        cell_geom g;
        if (!cell_setup(s, col, c, &g)) continue;
        for (uint32_t y = 0; y < ch; y++) {
            uint32_t *d = dst + (size_t)y * stride_px + g.x0;
            for (uint32_t x = 0; x < g.rw; x++) d[x] = g.bg | s->alpha_or;
            if (g.cov != NULL && y < g.gh) {
                const uint8_t *src = g.cov + (size_t)y * g.cov_stride;
                for (uint32_t x = 0; x < g.gw; x++)
                    d[x] = blend_px(g.fg, g.bg, src[x]) | s->alpha_or;
            }
            if (g.underline && y == ch - 1u)
                for (uint32_t x = 0; x < g.rw; x++) d[x] = g.fg | s->alpha_or;
        }
    }
}

/* x = fg*a + bg*(255-a) = bg*255 + (fg-bg)*a. The true x is in [0,65025], so
 * u16 modular arithmetic is exact and one mullo suffices. Then
 * (x+127)/255 == (t + (t>>8)) >> 8 with t = x + 128 (exhaustively tested).
 * a == 0 yields bg and a == 255 yields fg, so no branch is needed. */
static inline __m128i blend4(__m128i diff16, __m128i bg255, __m128i alpha_or, uint32_t cov4)
{
    const __m128i zero = _mm_setzero_si128(), c128 = _mm_set1_epi16(128);
    __m128i c = _mm_cvtsi32_si128((int)cov4);
    c = _mm_unpacklo_epi8(c, c);
    c = _mm_unpacklo_epi16(c, c);               /* a0 x4, a1 x4, a2 x4, a3 x4 */
    __m128i alo = _mm_unpacklo_epi8(c, zero), ahi = _mm_unpackhi_epi8(c, zero);
    __m128i tlo = _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(diff16, alo), bg255), c128);
    __m128i thi = _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(diff16, ahi), bg255), c128);
    tlo = _mm_srli_epi16(_mm_add_epi16(tlo, _mm_srli_epi16(tlo, 8)), 8);
    thi = _mm_srli_epi16(_mm_add_epi16(thi, _mm_srli_epi16(thi, 8)), 8);
    return _mm_or_si128(_mm_packus_epi16(tlo, thi), alpha_or);
}

static inline void fill_px(uint32_t *d, uint32_t n, uint32_t v)
{
    __m128i vv = _mm_set1_epi32((int)v);
    uint32_t x = 0;
    for (; x + 4u <= n; x += 4u) _mm_storeu_si128((__m128i *)(void *)(d + x), vv);
    for (; x < n; x++) d[x] = v;
}

static inline void blit_cell(const cell_geom *g, uint32_t *dst, size_t stride_px,
                              uint32_t ch, uint32_t alpha_or)
{
    const __m128i zero = _mm_setzero_si128(), aor = _mm_set1_epi32((int)alpha_or);
    uint32_t bgp = g->bg | alpha_or, fgp = g->fg | alpha_or;
    __m128i bgv = _mm_set1_epi32((int)bgp), fgv = _mm_set1_epi32((int)fgp);
    __m128i fg16 = _mm_unpacklo_epi8(_mm_set1_epi32((int)g->fg), zero);
    __m128i bg16 = _mm_unpacklo_epi8(_mm_set1_epi32((int)g->bg), zero);
    __m128i diff16 = _mm_sub_epi16(fg16, bg16);
    __m128i bg255 = _mm_mullo_epi16(bg16, _mm_set1_epi16(255));
    for (uint32_t y = 0; y < ch; y++) {
        uint32_t *d = dst + (size_t)y * stride_px + g->x0;
        if (g->underline && y == ch - 1u) { fill_px(d, g->rw, fgp); continue; }
        uint32_t x = 0;
        if (g->cov != NULL && y < g->gh) {
            const uint8_t *src = g->cov + (size_t)y * g->cov_stride;
            for (; x + 4u <= g->gw; x += 4u) {
                uint32_t cov4;
                memcpy(&cov4, src + x, 4);
                __m128i v = cov4 == 0 ? bgv : cov4 == UINT32_MAX ? fgv : blend4(diff16, bg255, aor, cov4);
                _mm_storeu_si128((__m128i *)(void *)(d + x), v);
            }
            for (; x < g->gw; x++) d[x] = blend_px(g->fg, g->bg, src[x]) | alpha_or;
        }
        fill_px(d + x, g->rw - x, bgp);
    }
}

void raster_row_sse2(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row)
{
    uint32_t cols = s->dims.cols, ch = s->dims.cell_h;
    for (uint32_t col = 0; col < cols; col++) {
        const render_cell *c = &s->cells[(size_t)row * cols + col];
        cell_geom g;
        if (!cell_setup(s, col, c, &g)) continue;
        blit_cell(&g, dst, stride_px, ch, s->alpha_or);
    }
}

void raster_partition(uint32_t total, uint32_t njobs, uint32_t job, uint32_t *lo, uint32_t *hi)
{
    *lo = (uint32_t)((uint64_t)total * job / njobs);
    *hi = (uint32_t)((uint64_t)total * (job + 1u) / njobs);
}

void raster_for_rows(const render_strip *strips, size_t count, uint32_t lo, uint32_t hi,
                     raster_row_fn fn, void *user)
{
    uint32_t ord = 0;
    for (size_t i = 0; i < count && ord < hi; i++) {
        uint32_t n = strips[i].row_count, end = ord + n;
        if (end > lo) {
            uint32_t a = lo > ord ? lo - ord : 0, b = (hi < end ? hi : end) - ord;
            for (uint32_t r = a; r < b; r++) fn(user, strips[i].first_row + r);
        }
        ord = end;
    }
}

static inline void fill_stream(uint32_t *d, uint32_t n, uint32_t v)
{
    uint32_t x = 0;
    while (x < n && ((uintptr_t)(d + x) & 15u)) d[x++] = v;
    __m128i vv = _mm_set1_epi32((int)v);
    for (; x + 4u <= n; x += 4u) _mm_stream_si128((__m128i *)(void *)(d + x), vv);
    for (; x < n; x++) d[x] = v;
}

typedef struct stream_blit {
    cell_geom g;
    __m128i diff, bg255, bg, fg;
    bool active;
} stream_blit;

static inline void stream_line(const stream_blit *b, uint32_t *dst, size_t stride_px,
                                uint32_t y, uint32_t ch, __m128i aor, uint32_t alpha_or)
{
    if (!b->active) return;
    const cell_geom *g = &b->g;
    uint32_t *d = dst + (size_t)y * stride_px + g->x0;
    if (g->underline && y == ch - 1u) { fill_stream(d, g->rw, g->fg | alpha_or); return; }
    uint32_t x = 0;
    if (g->cov && y < g->gh) {
        const uint8_t *src = g->cov + (size_t)y * g->cov_stride;
        for (; x + 4u <= g->gw; x += 4u) {
            uint32_t cov4;
            memcpy(&cov4, src + x, 4);
            __m128i v = cov4 == 0 ? b->bg : cov4 == UINT32_MAX ? b->fg : blend4(b->diff, b->bg255, aor, cov4);
            _mm_stream_si128((__m128i *)(void *)(d + x), v);
        }
        for (; x < g->gw; x++) d[x] = blend_px(g->fg, g->bg, src[x]) | alpha_or;
    }
    fill_stream(d + x, g->rw - x, g->bg | alpha_or);
}

void raster_row_sse2_stream(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row)
{
    uint32_t cw = s->dims.cell_w, cols = s->dims.cols, ch = s->dims.cell_h;
    if ((cw != 8 && cw != 16) || ((uintptr_t)dst & 15u) || stride_px % 16u || (cols * cw) % 16u) {
        raster_row_sse2(s, dst, stride_px, row); return;
    }
    const __m128i zero = _mm_setzero_si128(), aor = _mm_set1_epi32((int)s->alpha_or);
    uint32_t step = cw == 8 ? 2u : 1u;
    for (uint32_t col = 0; col < cols; col += step) {
        stream_blit blit[2];
        for (uint32_t k = 0; k < step; k++) {
            stream_blit *b = &blit[k];
            b->active = cell_setup(s, col + k, &s->cells[(size_t)row * cols + col + k], &b->g);
            if (!b->active) continue;
            __m128i f = _mm_unpacklo_epi8(_mm_set1_epi32((int)b->g.fg), zero);
            __m128i bg = _mm_unpacklo_epi8(_mm_set1_epi32((int)b->g.bg), zero);
            b->diff = _mm_sub_epi16(f, bg);
            b->bg255 = _mm_mullo_epi16(bg, _mm_set1_epi16(255));
            b->bg = _mm_set1_epi32((int)(b->g.bg | s->alpha_or));
            b->fg = _mm_set1_epi32((int)(b->g.fg | s->alpha_or));
        }
        for (uint32_t y = 0; y < ch; y++) {
            stream_line(&blit[0], dst, stride_px, y, ch, aor, s->alpha_or);
            if (step == 2) stream_line(&blit[1], dst, stride_px, y, ch, aor, s->alpha_or);
        }
    }
    _mm_sfence(); /* publish all streaming stores before XShm reads this strip */
}

static const uint32_t *palette_for(raster_palette *palette, uint32_t fg, uint32_t bg)
{
    uint32_t hash = fg * 0x9e3779b1u ^ bg * 0x85ebca77u;
    hash ^= hash >> 16;
    raster_palette_entry *entry = &palette->entries[hash % RASTER_PALETTE_SLOTS];
    if (!entry->valid || entry->fg != fg || entry->bg != bg) {
        if (palette->rebuilds >= RASTER_PALETTE_SLOTS) return NULL;
        palette->rebuilds++;
        const __m128i zero = _mm_setzero_si128();
        __m128i f = _mm_unpacklo_epi8(_mm_set1_epi32((int)fg), zero);
        __m128i b = _mm_unpacklo_epi8(_mm_set1_epi32((int)bg), zero);
        __m128i diff = _mm_sub_epi16(f, b), bg255 = _mm_mullo_epi16(b, _mm_set1_epi16(255));
        for (uint32_t i = 0; i < 256; i += 4) {
            uint32_t coverage = 0x03020100u + i * 0x01010101u;
            _mm_storeu_si128((__m128i *)(void *)&entry->pixels[i], blend4(diff, bg255, zero, coverage));
        }
        entry->fg = fg; entry->bg = bg; entry->valid = true;
    }
    return entry->pixels;
}

void raster_row_cached(const raster_scene *s, uint32_t *dst, size_t stride_px,
                       uint32_t row, raster_palette *palette)
{
    uint32_t cols = s->dims.cols, ch = s->dims.cell_h;
    const __m128i aor = _mm_set1_epi32((int)s->alpha_or);
    for (uint32_t col = 0; col < cols; col++) {
        cell_geom g;
        if (!cell_setup(s, col, &s->cells[(size_t)row * cols + col], &g)) continue;
        const uint32_t *table = g.cov ? palette_for(palette, g.fg, g.bg) : NULL;
        if (g.cov && !table) { blit_cell(&g, dst, stride_px, ch, s->alpha_or); continue; }
        for (uint32_t y = 0; y < ch; y++) {
            uint32_t *d = dst + (size_t)y * stride_px + g.x0;
            if (g.underline && y == ch - 1u) { fill_px(d, g.rw, g.fg | s->alpha_or); continue; }
            uint32_t x = 0;
            if (table && y < g.gh) {
                const uint8_t *src = g.cov + (size_t)y * g.cov_stride;
                for (; x + 4u <= g.gw; x += 4) {
                    __m128i v = _mm_setr_epi32((int)table[src[x]], (int)table[src[x + 1]],
                        (int)table[src[x + 2]], (int)table[src[x + 3]]);
                    _mm_storeu_si128((__m128i *)(void *)(d + x), _mm_or_si128(v, aor));
                }
                for (; x < g.gw; x++) d[x] = table[src[x]] | s->alpha_or;
            }
            fill_px(d + x, g.rw - x, g.bg | s->alpha_or);
        }
    }
}
