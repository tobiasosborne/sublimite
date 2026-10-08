/* tests/font_test.c - P2.3 (edit-e6x.3): bake consistency, runtime raster,
 * missing glyphs, shelf allocator. Reads vendor/DejaVuSansMono.ttf from the
 * repo root (make check runs from there). */
#include "base/base.h"
#include "font/font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static unsigned char *load_ttf(size_t *len)
{
    FILE *fp = fopen("vendor/DejaVuSansMono.ttf", "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n); /* test code, not the typing path */
    if (!b || fread(b, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); free(b); return NULL; }
    fclose(fp);
    *len = (size_t)n;
    return b;
}

static void test_bake_consistency(font_t *f)
{
    font_metric live, baked = {0, 0, 0, 0, 0};
    CHECK(font_glyph_metrics(f, 'M', &live) == FONT_OK);
    const font_metric *b = font_ascii_glyph('M');
    CHECK(b != NULL);
    if (b) baked = *b;
    CHECK(live.advance == baked.advance);
    CHECK(live.bearing_x == baked.bearing_x);
    CHECK(live.bearing_y == baked.bearing_y);
    CHECK(live.w == baked.w);
    CHECK(live.h == baked.h);
    font_cell c;
    font_cell_metrics(f, &c);
    font_cell ac = font_ascii_cell();
    CHECK(c.cell_w == ac.cell_w && c.cell_h == ac.cell_h && c.ascent == ac.ascent);
    CHECK(font_ascii_px() == 30u);
    size_t plen = 0;
    font_ascii_pixels(&plen);
    CHECK(plen == 95u * ac.cell_w * ac.cell_h);
    CHECK(plen <= 64u * 1024u);
    CHECK(font_ascii_glyph(0x1Fu) == NULL && font_ascii_glyph(0x7Fu) == NULL);
}

static void test_raster(font_t *f, edit_arena *a)
{
    font_bitmap b;
    CHECK(font_raster_glyph(f, 0xE9u, a, &b) == FONT_OK);      /* e-acute */
    CHECK(b.w > 0 && b.h > 0 && b.pixels != NULL);
    int any = 0;
    for (size_t i = 0; b.pixels && i < (size_t)b.w * b.h; i++) any |= b.pixels[i] != 0;
    CHECK(any);

    int r = font_raster_glyph(f, 0x3042u, a, &b);             /* hiragana a */
    CHECK(r == FONT_OK || r == FONT_ERR_MISSING);
    printf("U+3042 raster result: %d\n", r);

    CHECK(font_raster_glyph(f, 0x1F600u, a, &b) == FONT_ERR_MISSING); /* emoji */
    CHECK(font_raster_glyph(f, 0xD800u, a, &b) == FONT_ERR_MISSING);  /* surrogate */
    CHECK(font_raster_glyph(f, 0x110000u, a, &b) == FONT_ERR_MISSING);
}

static void test_shelf(void)
{
    font_atlas at;
    font_atlas_init(&at, 1);
    uint32_t pg, x, y, n = 0, ok = 0;
    uint32_t lastx = 0, lasty = 0;
    for (;;) {
        int r = font_atlas_alloc(&at, 64, 64, &pg, &x, &y);
        if (r != FONT_OK) { CHECK(r == FONT_ERR_NOMEM); break; }
        CHECK(pg == 0 && x + 64 <= FONT_ATLAS_PAGE_DIM && y + 64 <= FONT_ATLAS_PAGE_DIM);
        CHECK(n == 0 || y > lasty || (y == lasty && x > lastx));
        lastx = x; lasty = y;
        n++;
        ok++;
        if (n > 1000) break;
    }
    CHECK(ok == 256u);   /* 16 x 16 cells of 64 px in one 1024 page */
    CHECK(font_atlas_alloc(&at, 1, 1, &pg, &x, &y) == FONT_ERR_NOMEM);

    font_atlas two;
    font_atlas_init(&two, 2);
    CHECK(font_atlas_alloc(&two, 1024, 1024, &pg, &x, &y) == FONT_OK && pg == 0);
    CHECK(font_atlas_alloc(&two, 1, 1, &pg, &x, &y) == FONT_OK && pg == 1);
    CHECK(font_atlas_alloc(&two, 1025, 1, &pg, &x, &y) == FONT_ERR_NOMEM);
}

int main(void)
{
    size_t len = 0;
    unsigned char *ttf = load_ttf(&len);
    if (!ttf) { fprintf(stderr, "font_test: cannot read vendor/DejaVuSansMono.ttf\n"); return 1; }
    font_t f;
    CHECK(font_init(&f, ttf, len) == FONT_OK);
    CHECK(font_set_px(&f, 30) == FONT_OK);
    edit_arena a;
    CHECK(edit_arena_init(&a, 1u << 20) == 0);

    test_bake_consistency(&f);
    test_raster(&f, &a);
    test_shelf();

    edit_arena_free(&a);
    free(ttf);
    if (failures) { fprintf(stderr, "font_test: %d failure(s)\n", failures); return 1; }
    printf("font_test: all passed\n");
    return 0;
}
