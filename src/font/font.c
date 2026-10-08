/* src/font/font.c - see font.h. stb_truetype is vendored in vendor/. */
#include "font/font.h"
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wpedantic"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../vendor/stb_truetype.h"
#pragma GCC diagnostic pop

#include "font/atlas_ascii.h"

_Static_assert(sizeof(stbtt_fontinfo) <= FONT_OPAQUE_BYTES, "font blob too small");

#define FONT_ASCII_FIRST 0x20u
#define FONT_ASCII_LAST  0x7Eu

static stbtt_fontinfo *info_of(font_t *f) { return (stbtt_fontinfo *)(void *)f->info; }
static const stbtt_fontinfo *cinfo_of(const font_t *f)
{
    return (const stbtt_fontinfo *)(const void *)f->info;
}

static int32_t round_f(float x)
{
    return x >= 0.0f ? (int32_t)(x + 0.5f) : -(int32_t)(-x + 0.5f);
}

static int cp_valid(uint32_t cp)
{
    return cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

static int glyph_of(const font_t *f, uint32_t cp, int *g)
{
    if (!cp_valid(cp)) return FONT_ERR_MISSING;
    *g = stbtt_FindGlyphIndex(cinfo_of(f), (int)cp);
    return *g == 0 ? FONT_ERR_MISSING : FONT_OK;
}

static void metrics_of_glyph(const font_t *f, int g, font_metric *m)
{
    int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphHMetrics(cinfo_of(f), g, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(cinfo_of(f), g, f->scale, f->scale, &x0, &y0, &x1, &y1);
    m->advance = round_f((float)adv * f->scale);
    m->bearing_x = x0;
    m->bearing_y = y0;
    m->w = x1 > x0 ? (uint32_t)(x1 - x0) : 0u;
    m->h = y1 > y0 ? (uint32_t)(y1 - y0) : 0u;
}

int font_init(font_t *f, const unsigned char *ttf, size_t len)
{
    if (!f || !ttf || len < 12) return FONT_ERR_ARG;
    memset(f->info, 0, sizeof f->info);
    if (!stbtt_InitFont(info_of(f), ttf, 0)) return FONT_ERR_INIT;
    f->data = ttf;
    f->len = len;
    f->px = 0;
    f->scale = 0.0f;
    memset(&f->cell, 0, sizeof f->cell);
    return FONT_OK;
}

int font_set_px(font_t *f, uint32_t px)
{
    if (!f || !f->data || px == 0 || px > 4096u) return FONT_ERR_ARG;
    int a = 0, d = 0, gap = 0;
    f->px = px;
    f->scale = stbtt_ScaleForPixelHeight(cinfo_of(f), (float)px);
    stbtt_GetFontVMetrics(cinfo_of(f), &a, &d, &gap);
    int32_t asc = round_f((float)a * f->scale);
    int32_t dsc = round_f((float)(-d) * f->scale);
    f->cell.ascent = (uint32_t)asc;
    f->cell.descent = (uint32_t)dsc;
    f->cell.cell_h = (uint32_t)(asc + dsc);
    uint32_t maxadv = 0;
    for (uint32_t cp = FONT_ASCII_FIRST; cp <= FONT_ASCII_LAST; cp++) {
        int g = 0;
        font_metric m;
        if (glyph_of(f, cp, &g) != FONT_OK) continue;
        metrics_of_glyph(f, g, &m);
        if (m.advance > 0 && (uint32_t)m.advance > maxadv) maxadv = (uint32_t)m.advance;
    }
    f->cell.cell_w = maxadv;
    return FONT_OK;
}

int font_cell_metrics(const font_t *f, font_cell *out)
{
    if (!f || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    *out = f->cell;
    return FONT_OK;
}

int font_glyph_metrics(const font_t *f, uint32_t cp, font_metric *out)
{
    int g = 0;
    if (!f || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    int r = glyph_of(f, cp, &g);
    if (r != FONT_OK) return r;
    metrics_of_glyph(f, g, out);
    return FONT_OK;
}

int font_raster_glyph(font_t *f, uint32_t cp, edit_arena *arena, font_bitmap *out)
{
    int g = 0;
    if (!f || !arena || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    out->pixels = NULL;
    out->w = 0;
    out->h = 0;
    memset(&out->m, 0, sizeof out->m);
    int r = glyph_of(f, cp, &g);
    if (r != FONT_OK) return r;
    metrics_of_glyph(f, g, &out->m);
    out->w = out->m.w;
    out->h = out->m.h;
    if (out->w == 0 || out->h == 0) return FONT_OK;
    uint8_t *px = edit_arena_alloc(arena, (size_t)out->w * out->h, 1);
    if (!px) return FONT_ERR_NOMEM;
    /* stb writes a box-sized bitmap whose origin is the box's top-left. */
    stbtt_MakeGlyphBitmap(cinfo_of(f), px, (int)out->w, (int)out->h, (int)out->w,
                          f->scale, f->scale, g);
    out->pixels = px;
    return FONT_OK;
}

int font_place_in_cell(const font_t *f, const font_bitmap *b, uint8_t *cell,
                       uint32_t cell_w, uint32_t cell_h)
{
    if (!f || !b || !cell || f->px == 0) return FONT_ERR_ARG;
    if (!b->pixels) return FONT_OK;
    for (uint32_t y = 0; y < b->h; y++) {
        int64_t cy = (int64_t)f->cell.ascent + b->m.bearing_y + (int64_t)y;
        if (cy < 0 || cy >= (int64_t)cell_h) continue;
        for (uint32_t x = 0; x < b->w; x++) {
            int64_t cx = (int64_t)b->m.bearing_x + (int64_t)x;
            if (cx < 0 || cx >= (int64_t)cell_w) continue;
            cell[(size_t)cy * cell_w + (size_t)cx] = b->pixels[(size_t)y * b->w + x];
        }
    }
    return FONT_OK;
}

const font_metric *font_ascii_glyph(uint32_t cp)
{
    if (cp < FONT_ASCII_FIRST || cp > FONT_ASCII_LAST) return NULL;
    return &font_atlas_metrics[cp - FONT_ASCII_FIRST];
}

const uint8_t *font_ascii_pixels(size_t *len)
{
    if (len) *len = sizeof font_atlas_pixels;
    return font_atlas_pixels;
}

font_cell font_ascii_cell(void)
{
    font_cell c = { FONT_ATLAS_CELL_W, FONT_ATLAS_CELL_H, FONT_ATLAS_ASCENT,
                    FONT_ATLAS_CELL_H - FONT_ATLAS_ASCENT };
    return c;
}

uint32_t font_ascii_px(void) { return FONT_ATLAS_PX; }

void font_atlas_init(font_atlas *a, uint32_t max_pages)
{
    memset(a, 0, sizeof *a);
    a->max_pages = max_pages > FONT_ATLAS_MAX_PAGES ? FONT_ATLAS_MAX_PAGES : max_pages;
}

static void place(font_atlas *a, uint32_t p, uint32_t w, uint32_t h,
                  uint32_t *page, uint32_t *x, uint32_t *y, int new_shelf)
{
    font_atlas_page *pg = &a->pages[p];
    if (new_shelf) {
        pg->shelf_y = pg->used_y;
        pg->shelf_h = h;
        pg->cur_x = 0;
        pg->used_y += h;
    }
    *page = p;
    *x = pg->cur_x;
    *y = pg->shelf_y;
    pg->cur_x += w;
}

int font_atlas_alloc(font_atlas *a, uint32_t w, uint32_t h,
                     uint32_t *page, uint32_t *x, uint32_t *y)
{
    if (!a || !page || !x || !y) return FONT_ERR_ARG;
    if (w == 0 || h == 0 || w > FONT_ATLAS_PAGE_DIM || h > FONT_ATLAS_PAGE_DIM)
        return FONT_ERR_NOMEM;
    for (uint32_t p = 0; p < a->npages; p++) {
        font_atlas_page *pg = &a->pages[p];
        if (h <= pg->shelf_h && pg->cur_x + w <= FONT_ATLAS_PAGE_DIM) {
            place(a, p, w, h, page, x, y, 0);
            return FONT_OK;
        }
        if (pg->used_y + h <= FONT_ATLAS_PAGE_DIM) {
            place(a, p, w, h, page, x, y, 1);
            return FONT_OK;
        }
    }
    if (a->npages < a->max_pages) {
        uint32_t p = a->npages++;
        place(a, p, w, h, page, x, y, 1);
        return FONT_OK;
    }
    return FONT_ERR_NOMEM;
}
