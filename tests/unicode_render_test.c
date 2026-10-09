/* P4.11: real fallback handoff, corpus coverage, computed headless screenshot. */
#include "layout/layout.h"
#include "raster/raster.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) EDIT_ASSERT(c)

typedef struct fixture {
    edit_arena files, storage, reference;
    font_t primary;
    font_family family;
    font_fallback fallback;
    font_cache cache;
    int load_result, ready;
    render_grid grid;
    render_cell *cells;
    uint64_t dirty[8], row_byte[300];
    uint32_t row_used[300], frame;
    layout l;
} fixture;

static void prepare_job(work_ctx *ctx)
{
    fixture *f = ctx->arg;
    font_fallback_discover(&f->fallback, "libfontconfig.so.1");
    f->load_result = font_family_load(&f->family, &f->primary, &f->fallback, &f->files);
    work_msg msg = {0}; msg.kind = FONT_FALLBACK_MSG_KIND;
    CHECK(work_publish(ctx, &msg));
}
static void prepared(const work_msg *msg, void *arg)
{
    CHECK(msg->kind == FONT_FALLBACK_MSG_KIND);
    ((fixture *)arg)->ready = 1;
}
static void setup(fixture *f, uint32_t px)
{
    memset(f, 0, sizeof *f);
    CHECK(edit_arena_init(&f->files, 64u << 20) == 0);
    CHECK(edit_arena_init(&f->storage, 16u << 20) == 0);
    CHECK(edit_arena_init(&f->reference, 1u << 20) == 0);
    size_t len = 0;
    unsigned char *bytes = font_load_file("vendor/DejaVuSansMono.ttf", &f->files, &len);
    CHECK(bytes && font_init(&f->primary, bytes, len) == FONT_OK);
    CHECK(font_set_px(&f->primary, px) == FONT_OK);
    work_pool *pool = malloc(sizeof *pool);
    CHECK(pool && work_pool_init(pool, 1, 0) == 0);
    work_handle handle = work_submit(pool, (work_job){prepare_job, f, 1, WORK_BULK});
    CHECK(handle.epoch);
    struct pollfd fd = {work_pool_eventfd(pool), POLLIN, 0};
    while (!f->ready) {
        CHECK(poll(&fd, 1, 10000) > 0);
        (void)work_mailbox_drain(pool, prepared, f);
    }
    CHECK(!pthread_equal(f->fallback.worker, pthread_self()));
    work_pool_shutdown(pool); free(pool);
    CHECK(f->load_result == FONT_OK);
    CHECK(font_cache_init(&f->cache, &f->family, &f->storage, 3, 8192, 1u << 20, 1u << 20) == FONT_OK);
    printf("unicode fonts px=%u cjk=%s index=%u emoji=%s index=%u worker=yes\n", px,
           f->fallback.cjk[0] ? f->fallback.cjk : "none", f->fallback.cjk_index,
           f->fallback.emoji[0] ? f->fallback.emoji : "none", f->fallback.emoji_index);
}
static void teardown(fixture *f)
{
    free(f->cells); edit_arena_free(&f->reference);
    edit_arena_free(&f->storage); edit_arena_free(&f->files);
}
static void grid_setup(fixture *f, uint32_t cols, uint32_t rows)
{
    free(f->cells);
    f->cells = malloc((size_t)cols * rows * sizeof *f->cells);
    CHECK(f->cells && rows <= 300);
    CHECK(render_grid_init(&f->grid, (render_dims){cols, rows, f->cache.cell.cell_w, f->cache.cell.cell_h},
                           f->cells, (size_t)cols * rows, f->dirty, 8) == RENDER_OK);
    CHECK(font_cache_bind(&f->cache, &f->grid) == FONT_OK);
    layout_config cfg = {0}; cfg.fg = 0xdddddd; cfg.bg = 0x102030;
    cfg.glyph = font_cache_glyph; cfg.glyph_ctx = &f->cache; cfg.slice_clusters = 500;
    CHECK(layout_init(&f->l, &f->grid, &cfg, f->row_byte, f->row_used) == LAYOUT_DONE);
}
static void show(fixture *f, piece_tree *tree, uint64_t start, uint64_t line, uint32_t scroll)
{
    CHECK(render_frame_begin(&f->grid, ++f->frame) == RENDER_OK);
    CHECK(layout_begin(&f->l, tree, (layout_viewport){start, line, scroll, 1}) == LAYOUT_DONE);
    int rc;
    do { rc = layout_run(&f->l); } while (rc == LAYOUT_MORE);
    CHECK(rc == LAYOUT_DONE && render_grid_validate(&f->grid) == RENDER_OK);
}
static piece_tree *make_tree(const uint8_t *bytes, size_t len)
{
    piece_allocator al = piece_default_allocator(); piece_tree *tree = piece_create(&al);
    CHECK(tree && piece_init_copy(tree, bytes, len) == PIECE_OK);
    return tree;
}
static font_t *covering(fixture *f, uint32_t cp)
{
    font_metric metric;
    for (uint32_t i = 0; i < f->family.count; i++) {
        int rc = font_glyph_metrics(&f->family.faces[i], cp, &metric);
        CHECK(rc == FONT_OK || rc == FONT_ERR_MISSING);
        if (rc == FONT_OK) return &f->family.faces[i];
    }
    return NULL;
}
static int covered(fixture *f, const uint8_t *bytes, size_t len)
{
    for (size_t off = 0; off < len;) {
        utf8_step s = utf8_decode(bytes + off, len - off); off += s.len;
        if (!s.valid || (!font_cluster_ignorable(s.cp) && !covering(f, s.cp))) return 0;
    }
    return 1;
}

/* Independent image oracle: render each outline separately with the existing
 * font placement API, then union coverage. Base e and Mono acute are both
 * cell-relative (metrics checked); fallback faces are moved to primary baseline.
 * This never uses cache pixels/slots, or the cache composer. */
static void reference_image(fixture *f, const uint32_t *cps, size_t count, uint32_t width, uint8_t *out)
{
    uint32_t w = f->cache.cell.cell_w * width, h = f->cache.cell.cell_h;
    memset(out, 0, (size_t)w * h);
    uint8_t layer[1920]; CHECK((size_t)w * h <= sizeof layer);
    for (size_t i = 0; i < count; i++) {
        if (font_cluster_ignorable(cps[i])) continue;
        font_t *face = covering(f, cps[i]); CHECK(face);
        font_bitmap bitmap; edit_arena_reset(&f->reference);
        CHECK(font_raster_glyph(face, cps[i], &f->reference, &bitmap) == FONT_OK);
        font_t positioned = *face; positioned.cell.ascent = f->cache.cell.ascent;
        memset(layer, 0, sizeof layer);
        CHECK(font_place_in_cell(&positioned, &bitmap, layer, w, h) == FONT_OK);
        for (size_t p = 0; p < (size_t)w * h; p++) if (layer[p] > out[p]) out[p] = layer[p];
    }
}
static uint32_t blend(uint32_t fg, uint32_t bg, uint8_t coverage)
{
    uint32_t result = 0;
    for (uint32_t shift = 0; shift <= 16; shift += 8) {
        uint32_t a = (fg >> shift) & 255u, b = (bg >> shift) & 255u;
        result |= ((a * coverage + b * (255u - coverage) + 127u) / 255u) << shift;
    }
    return result;
}
static void screenshots(fixture *f)
{
    /* e+acute, CJK, grinning face, valid replacement character, then FOUR
     * invalid bytes (overlong and truncated sequence), and trailing ASCII. */
    const uint8_t bytes[] = "e\xcc\x81\xe4\xb8\xad\xf0\x9f\x98\x80\xef\xbf\xbd\xc0\xaf\xe2\x82Z";
    const uint32_t cps[] = {'e', 0x301, 0x4e2d, 0x1f600, 0xfffd, '?', '?', '?', '?', 'Z'};
    const uint32_t starts[] = {0, 1, 3, 5, 6, 7, 8, 9, 10};
    const uint32_t widths[] = {1, 2, 2, 1, 1, 1, 1, 1, 1};
    const size_t indices[] = {0, 2, 3, 4, 5, 6, 7, 8, 9};
    grid_setup(f, 12, 1); piece_tree *tree = make_tree(bytes, sizeof bytes - 1);
    show(f, tree, 0, 0, 0);
    CHECK(f->cells[0].glyph_index != '?'); /* the original RED assertion */
    CHECK(f->cells[1].attrs == RENDER_ATTR_WIDE_LEFT && f->cells[2].attrs == RENDER_ATTR_WIDE_RIGHT);
    CHECK(f->cells[3].attrs == RENDER_ATTR_WIDE_LEFT && f->cells[4].attrs == RENDER_ATTR_WIDE_RIGHT);
    for (size_t i = 6; i < 10; i++) CHECK(f->cells[i].glyph_index == '?' && f->cells[i].attrs == RENDER_ATTR_INVERSE);
    CHECK(f->cells[5].attrs == 0 && f->cells[10].glyph_index == 'Z');
    uint32_t stride = 12u * f->cache.cell.cell_w, h = f->cache.cell.cell_h;
    size_t pixels = (size_t)stride * h;
    uint32_t *expected = malloc(pixels * sizeof *expected), *scalar = malloc(pixels * sizeof *scalar);
    uint32_t *simd = malloc(pixels * sizeof *simd); CHECK(expected && scalar && simd);
    for (size_t p = 0; p < pixels; p++) expected[p] = 0x102030;
    uint8_t image[1920];
    for (size_t i = 0; i < 9; i++) {
        /* Missing system CJK/emoji: limit expectation to the available set. */
        uint32_t replacement = '?'; const uint32_t *cp = cps + indices[i];
        size_t count = i == 0 ? 2u : 1u;
        if (!covering(f, *cp)) { cp = &replacement; count = 1; }
        reference_image(f, cp, count, widths[i], image);
        uint32_t w = widths[i] * f->cache.cell.cell_w;
        for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
            uint32_t fg = i >= 4 && i <= 7 ? 0x102030u : 0xdddddd;
            uint32_t bg = i >= 4 && i <= 7 ? 0xddddddu : 0x102030;
            expected[(size_t)y * stride + starts[i] * f->cache.cell.cell_w + x] = blend(fg, bg, image[(size_t)y * w + x]);
        }
    }
    raster_scene scene = {f->grid.dims, f->cells, f->grid.glyphs, f->grid.glyph_count,
                          f->grid.pages, f->grid.page_count, 0};
    raster_row_scalar(&scene, scalar, stride, 0); raster_row_sse2(&scene, simd, stride, 0);
    CHECK(memcmp(expected, scalar, pixels * sizeof *scalar) == 0);
    CHECK(memcmp(expected, simd, pixels * sizeof *simd) == 0);
    /* Wide clipping never leaves half a glyph, at either edge. */
    show(f, tree, 0, 0, 2); CHECK(f->cells[0].atlas_slot == RENDER_NO_SLOT && f->cells[0].attrs == 0);
    grid_setup(f, 2, 1); show(f, tree, 0, 0, 0); CHECK(f->cells[1].atlas_slot == RENDER_NO_SLOT);
    printf("unicode screenshot: computed expectation == scalar == SSE2, px=%u; combining/wide/invalid/clipping passed\n", f->primary.px);
    free(expected); free(scalar); free(simd); piece_destroy(tree);
}

static void corpus(fixture *f)
{
    size_t len = 0;
    uint8_t *bytes = font_load_file("/tmp/edit-corpus/unicode.txt", &f->files, &len);
    CHECK(bytes && len);
    uint8_t *seen = calloc(0x110000u, 1); CHECK(seen);
    for (size_t off = 0; off < len;) {
        utf8_step s = utf8_decode(bytes + off, len - off); CHECK(s.valid);
        seen[s.cp] = 1; off += s.len;
    }
    printf("unicode corpus fallback-covered set:");
    for (uint32_t cp = 0x20; cp < 0x110000u; cp++) {
        if (!seen[cp] || cp == 0x7f || font_cluster_ignorable(cp) || !covering(f, cp)) continue;
        printf(" U+%04X", cp);
        uint8_t encoded[8]; size_t n = 0;
        int width = utf8_cell_width(cp);
        if (width == 0) { encoded[n++] = 'e'; width = 1; }
        n += utf8_encode(cp, encoded + n);
        uint32_t slot = UINT32_MAX;
        CHECK(font_cache_glyph(&f->cache, encoded, n, (uint32_t)width, &slot) == FONT_OK);
        CHECK(slot != '?' - 0x20u);
    }
    puts(""); printf("unicode corpus uncovered set:");
    for (uint32_t cp = 0x20; cp < 0x110000u; cp++)
        if (seen[cp] && !font_cluster_ignorable(cp) && !covering(f, cp)) printf(" U+%04X", cp);
    puts(""); free(seen);
    grid_setup(f, 360, 300); piece_tree *tree = make_tree(bytes, len);
    size_t start = 0, line = 0, good = 0, absent = 0;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    while (start < len) {
        show(f, tree, start, line, 0);
        for (uint32_t row = 0; row < 300 && start < len; row++, line++) {
            uint32_t col = 0;
            while (start < len && bytes[start] != '\n') {
                const uint8_t *p = bytes + start; size_t avail = len - start;
                if (*p == '\t') { col += 4u - col % 4u; start++; continue; }
                int width; size_t n = utf8_cluster(p, avail, &width);
                CHECK(n && width >= 0 && width <= 2);
                if (width > 0 && col + (uint32_t)width <= 360u) {
                    render_cell *cell = &f->cells[(size_t)row * 360u + col];
                    if (covered(f, p, n)) {
                        /* '?' itself is legitimate, but nothing else covered
                         * may silently fall back to the placeholder. */
                        if (!(n == 1 && *p == '?')) CHECK(cell->glyph_index != '?');
                        if (width == 2) CHECK(cell->attrs & RENDER_ATTR_WIDE_LEFT);
                        good++;
                    } else { CHECK(cell->glyph_index == '?'); absent++; }
                }
                col += (uint32_t)width; start += n;
            }
            if (start < len) start++;
        }
    }
    size_t mallocs = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    CHECK(mallocs == 0);
    CHECK(good > 0 && f->cache.hits > 0);
    printf("unicode corpus: covered=%zu uncovered=%zu clusters; no tofu for covered set; all viewport grids valid\n", good, absent);
    printf("unicode corpus layout/cold-cache mallocs: %zu (%s)\n", mallocs,
           edit_malloc_guard_active() ? "guard active" : "ASan guard inert");
    piece_destroy(tree);
}

static void cache_contracts(fixture *f)
{
    edit_arena arena; CHECK(edit_arena_init(&arena, 6u << 20) == 0);
    font_cache cache;
    /* Init rollback, unready discovery, and primary-only degradation. */
    edit_arena small = {arena.base, 32, 3};
    CHECK(font_cache_init(&cache, &f->family, &small, 1, 4, 32, 1024) == FONT_ERR_NOMEM && small.used == 3);
    font_family primary; font_fallback unavailable = {0};
    CHECK(font_family_load(&primary, &f->primary, &unavailable, &arena) == FONT_ERR_ARG);
    CHECK(font_family_load(&primary, &f->primary, NULL, &arena) == FONT_OK && primary.count == 1);
    CHECK(font_cache_init(&cache, &primary, &arena, 2, 4, 32, 1u << 20) == FONT_OK);
    uint32_t page, x, y, first, second;
    /* Occupy first runtime page: next glyph must use fallback page TWO. */
    CHECK(font_atlas_alloc(&cache.atlas, FONT_ATLAS_PAGE_DIM, FONT_ATLAS_PAGE_DIM, &page, &x, &y) == FONT_OK);
    const uint8_t acute[] = "e\xcc\x81", diaeresis[] = "e\xcc\x88";
    CHECK(font_cache_glyph(&cache, acute, 3, 1, &first) == FONT_OK);
    CHECK(cache.glyphs[first].page == 2);
    uint8_t saved[1920]; render_glyph rect = cache.glyphs[first];
    for (uint32_t row = 0; row < rect.h; row++)
        memcpy(saved + (size_t)row * rect.w, cache.pages[rect.page].pixels + (size_t)(rect.y + row) * FONT_ATLAS_PAGE_DIM + rect.x, rect.w);
    CHECK(font_cache_glyph(&cache, diaeresis, 3, 1, &second) == FONT_OK && second != first);
    CHECK(font_cache_glyph(&cache, acute, 3, 2, &second) == FONT_OK && second != first);
    const uint8_t missing[] = {0xf4, 0x8f, 0xbf, 0xbf};
    CHECK(font_cache_glyph(&cache, missing, 4, 1, &second) == FONT_ERR_MISSING);
    CHECK(font_cache_glyph(&cache, (const uint8_t *)"A", 1, 1, &second) == FONT_ERR_NOMEM);
    uint64_t misses = cache.misses; size_t keys = cache.key_used, used = arena.used;
    font_atlas atlas = cache.atlas;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    for (size_t i = 0; i < 10000; i++) {
        CHECK(font_cache_glyph(&cache, acute, 3, 1, &second) == FONT_OK && second == first);
        CHECK(font_cache_glyph(&cache, missing, 4, 1, &second) == FONT_ERR_MISSING);
    }
    size_t mallocs = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    CHECK(mallocs == 0 && keys == cache.key_used && used == arena.used && misses == cache.misses);
    CHECK(memcmp(&atlas, &cache.atlas, sizeof atlas) == 0);
    for (uint32_t row = 0; row < rect.h; row++)
        CHECK(memcmp(saved + (size_t)row * rect.w, cache.pages[rect.page].pixels + (size_t)(rect.y + row) * FONT_ATLAS_PAGE_DIM + rect.x, rect.w) == 0);
    puts("unicode cache: immutable multi-page rectangles, exact keys/widths, exhaustion and cached misses passed");
    printf("unicode no-malloc: %zu allocations over 10000 cached lookups; no arena/page growth (%s)\n", mallocs,
           edit_malloc_guard_active() ? "guard active" : "ASan guard inert");
    edit_arena_free(&arena);
}

int main(void)
{
    fixture *f = malloc(sizeof *f); CHECK(f);
    setup(f, 15); screenshots(f); corpus(f); cache_contracts(f); teardown(f);
    setup(f, 30); screenshots(f); teardown(f);
    free(f);
    puts("unicode_render_test: all passed (headless)");
    return 0;
}
