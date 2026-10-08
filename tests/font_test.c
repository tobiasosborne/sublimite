/* tests/font_test.c - P2.3 (edit-e6x.3): bake consistency, runtime raster,
 * missing glyphs, shelf allocator. Reads vendor/DejaVuSansMono.ttf from the
 * repo root (make check runs from there). */
#include "base/base.h"
#include "font/font.h"
#include "work/work.h"
#include <poll.h>
#include <pthread.h>
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

/* Law 2: font_raster_glyph is on the typing path; no libc malloc after setup.
 * The guard is compiled out under ASan, so the assertion runs in the release
 * binary (build/tests/font_test); the san binary prints a skip. */
static void test_no_malloc(font_t *f, edit_arena *a)
{
    static const uint32_t cps[] = { 'M', 'g', 0xE9u, '@', 'W', '%' };
    font_bitmap b;
    edit_arena_mark_t mk = edit_arena_mark(a);
    CHECK(font_raster_glyph(f, 'M', a, &b) == FONT_OK);  /* warm up */
    edit_arena_reset_to_mark(a, mk);
    if (!edit_malloc_guard_active()) {
        printf("font_test: malloc guard inactive (ASan build), no-malloc check skipped\n");
        return;
    }
    edit_malloc_guard_begin();
    for (int i = 0; i < 600; i++) {
        int r = font_raster_glyph(f, cps[i % 6], a, &b);
        if (r != FONT_OK) break;
        edit_arena_reset_to_mark(a, mk);
    }
    size_t n = edit_malloc_guard_end();
    printf("font_test: mallocs during 600 rasterisations: %zu\n", n);
    CHECK(n == 0);
}

static void test_atlas_px(const unsigned char *ttf, size_t len)
{
    const font_ascii_atlas *a15 = font_ascii_atlas_for_px(15);
    const font_ascii_atlas *a30 = font_ascii_atlas_for_px(30);
    CHECK(a15 != NULL && a30 != NULL && a15 != a30);
    CHECK(font_ascii_atlas_for_px(17) == NULL);
    if (!a15 || !a30) return;
    CHECK(a15->px == 15u && a30->px == 30u);
    CHECK(a15->cell.cell_w < a30->cell.cell_w && a15->cell.cell_h < a30->cell.cell_h);
    CHECK(a15->pixels_len == 95u * a15->cell.cell_w * a15->cell.cell_h);
    CHECK(a30->pixels_len == 95u * a30->cell.cell_w * a30->cell.cell_h);
    CHECK(font_ascii_atlas_glyph(a15, 'M') != NULL && font_ascii_atlas_glyph(a15, 0x7Fu) == NULL);
    /* live font at 15 px agrees with the 15 px bake, and font_set_px picks it */
    font_t f;
    CHECK(font_init(&f, ttf, len) == FONT_OK);
    CHECK(font_set_px(&f, 15) == FONT_OK);
    CHECK(f.atlas == a15);
    font_metric live;
    CHECK(font_glyph_metrics(&f, 'g', &live) == FONT_OK);
    const font_metric *bm = font_ascii_atlas_glyph(a15, 'g');
    CHECK(bm && live.advance == bm->advance && live.w == bm->w && live.h == bm->h &&
          live.bearing_x == bm->bearing_x && live.bearing_y == bm->bearing_y);
    CHECK(font_set_px(&f, 30) == FONT_OK && f.atlas == a30);
    CHECK(font_set_px(&f, 22) == FONT_OK && f.atlas == NULL);
}

static void on_msg(const work_msg *m, void *ud)
{
    if (m->kind == FONT_FALLBACK_MSG_KIND) (*(int *)ud)++;
}

static void test_fallback(void)
{
    static font_fallback fb;     /* static: ~1 KB, lives past the job */
    static work_pool pool;
    CHECK(work_pool_init(&pool, 1, 0) == 0);
    work_job j = { font_fallback_job, &fb, 7u, WORK_BULK };
    pthread_t me = pthread_self();
    CHECK(work_submit(&pool, j).epoch != 0);
    struct pollfd pfd = { work_pool_eventfd(&pool), POLLIN, 0 };
    int got = 0;
    for (int i = 0; i < 300 && !got; i++) {   /* up to 30 s */
        if (poll(&pfd, 1, 100) > 0) work_mailbox_drain(&pool, on_msg, &got);
    }
    CHECK(got == 1);
    CHECK(atomic_load(&fb.done) == 1u);
    CHECK(!pthread_equal(fb.worker, me));
    printf("font_test: fallback fontconfig=%d cjk='%s' emoji='%s' (%.1f ms)\n",
           fb.have_fontconfig, fb.cjk, fb.emoji, (double)fb.elapsed_ns / 1e6);
    work_pool_shutdown(&pool);

    static font_fallback none;
    font_fallback_discover(&none, "libnope.so.9");
    CHECK(atomic_load(&none.done) == 1u && none.have_fontconfig == 0);
    CHECK(none.cjk[0] == 0 && none.emoji[0] == 0);
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
    test_no_malloc(&f, &a);
    test_atlas_px(ttf, len);
    test_fallback();

    edit_arena_free(&a);
    free(ttf);
    if (failures) { fprintf(stderr, "font_test: %d failure(s)\n", failures); return 1; }
    printf("font_test: all passed\n");
    return 0;
}
