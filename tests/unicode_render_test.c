/* P4.11: real fallback handoff, corpus coverage, computed headless screenshot. */
#include "layout/layout.h"
#include "raster/raster.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) EDIT_ASSERT(c)

/* Pixel/outcome oracles await bounded continuation; first-slice/work-budget
 * regressions below call font_cache_glyph directly. No typing allocations. */
static int complete_glyph(void *ctx, const uint8_t *bytes, size_t len,
                          uint32_t width, uint32_t *slot)
{
    int rc; size_t calls = 0;
    do {
        rc = font_cache_glyph(ctx, bytes, len, width, slot);
        CHECK(++calls <= len + 1u);
    } while (rc == FONT_MORE);
    return rc;
}

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

static void prepared(const work_msg *msg, void *arg)
{
    fixture *f = arg;
    if (font_fallback_event(&f->fallback, msg)) f->ready = 1;
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
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    CHECK(pool && work_pool_init(pool, 1, 0) == 0);
    work_handle handle = work_submit(pool, (work_job){font_fallback_job, &f->fallback, 1, WORK_BULK});
    CHECK(handle.epoch);
    struct pollfd fd = {work_pool_eventfd(pool), POLLIN, 0};
    while (!f->ready) {
        CHECK(poll(&fd, 1, 10000) > 0);
        (void)work_mailbox_drain(pool, prepared, f);
    }
    CHECK(!pthread_equal(f->fallback.worker, pthread_self()));
    work_pool_shutdown(pool); free(pool);
    f->load_result = font_family_load(&f->family, &f->primary, &f->fallback, &f->files);
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
    int rc; uint64_t calls = 0;
    do {
        rc = layout_run(&f->l);
        CHECK(++calls <= piece_len(tree) + f->grid.dims.rows + 1u);
    } while (rc == LAYOUT_MORE);
    CHECK(rc == LAYOUT_DONE && render_grid_validate(&f->grid) == RENDER_OK);
    /* The public layout continuation must finish composition itself. An
     * external preparation/restart loop would conceal dropped FONT_MORE. */
    if (f->l.cfg.glyph == font_cache_glyph)
        CHECK(((font_cache *)f->l.cfg.glyph_ctx)->pending_len == 0);
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
        CHECK(complete_glyph(&f->cache, encoded, n, (uint32_t)width, &slot) == FONT_OK);
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
    CHECK(complete_glyph(&cache, acute, 3, 1, &first) == FONT_OK);
    CHECK(cache.glyphs[first].page == 2);
    uint8_t saved[1920]; render_glyph rect = cache.glyphs[first];
    for (uint32_t row = 0; row < rect.h; row++)
        memcpy(saved + (size_t)row * rect.w, cache.pages[rect.page].pixels + (size_t)(rect.y + row) * FONT_ATLAS_PAGE_DIM + rect.x, rect.w);
    CHECK(complete_glyph(&cache, diaeresis, 3, 1, &second) == FONT_OK && second != first);
    CHECK(complete_glyph(&cache, acute, 3, 2, &second) == FONT_OK && second != first);
    const uint8_t missing[] = {0xf4, 0x8f, 0xbf, 0xbf};
    CHECK(complete_glyph(&cache, missing, 4, 1, &second) == FONT_ERR_MISSING);
    CHECK(complete_glyph(&cache, (const uint8_t *)"A", 1, 1, &second) == FONT_ERR_NOMEM);
    uint64_t misses = cache.misses; size_t keys = cache.key_used, used = arena.used;
    font_atlas atlas = cache.atlas;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    for (size_t i = 0; i < 10000; i++) {
        CHECK(complete_glyph(&cache, acute, 3, 1, &second) == FONT_OK && second == first);
        CHECK(complete_glyph(&cache, missing, 4, 1, &second) == FONT_ERR_MISSING);
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

/* Review regressions use independent, deterministic expectations. */
#define REVIEW(c) do { if (!(c)) { fprintf(stderr, "review FAIL: %s\n", #c); return 1; } } while (0)
static int review_marks(fixture *f)
{
    const uint8_t bytes[] = "e\xcd\xa1";
    const uint32_t cps[] = {'e', 0x361}; uint8_t expected[1920];
    reference_image(f, cps, 2, 1, expected);
    font_metric m; REVIEW(font_glyph_metrics(&f->primary, 0x361, &m) == FONT_OK);
    REVIEW(m.bearing_x < 0 && m.advance == (int32_t)f->cache.cell.cell_w);
    uint32_t slot; REVIEW(complete_glyph(&f->cache, bytes, sizeof bytes - 1, 1, &slot) == FONT_OK);
    render_glyph g = f->cache.glyphs[slot];
    for (uint32_t y = 0; y < g.h; y++)
        REVIEW(memcmp(expected + (size_t)y * g.w, f->cache.pages[g.page].pixels +
               (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0);
    printf("review §2 GREEN: U+0361 independent pixels px=%u\n", f->primary.px);
    return 0;
}
static int review_proportional_mark(fixture *f)
{
    size_t len = 0; unsigned char *bytes = font_load_file("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", &f->files, &len);
    REVIEW(bytes); font_t proportional;
    REVIEW(font_init(&proportional, bytes, len) == FONT_OK && font_set_px(&proportional, f->primary.px) == FONT_OK);
    uint32_t mark = 0; font_metric metric;
    for (uint32_t cp = 0x300; cp <= 0x20ff; cp++) {
        if (utf8_cell_width(cp) != 0 || font_cluster_ignorable(cp)) continue;
        if (font_glyph_metrics(&f->primary, cp, &metric) != FONT_ERR_MISSING) continue;
        if (font_glyph_metrics(&proportional, cp, &metric) == FONT_OK && metric.advance == 0 && metric.w && metric.h) { mark = cp; break; }
    }
    REVIEW(mark); font_family family = {{f->primary,proportional},2};
    edit_arena arena; REVIEW(edit_arena_init(&arena, 4u << 20) == 0); font_cache cache;
    REVIEW(font_cache_init(&cache, &family, &arena, 1, 32, 256, 1u << 20) == FONT_OK);
    uint8_t key[5] = {'e'}, expected[1920], layer[1920]; size_t n = 1u + utf8_encode(mark, key + 1);
    uint32_t base[] = {'e'}; reference_image(f, base, 1, 1, expected);
    font_metric base_metric; REVIEW(font_glyph_metrics(&f->primary, 'e', &base_metric) == FONT_OK);
    edit_arena_reset(&f->reference); font_bitmap bitmap;
    REVIEW(font_raster_glyph(&proportional, mark, &f->reference, &bitmap) == FONT_OK && bitmap.m.advance == 0);
    bitmap.m.bearing_x += base_metric.advance; proportional.cell.ascent = cache.cell.ascent;
    memset(layer, 0, sizeof layer);
    REVIEW(font_place_in_cell(&proportional, &bitmap, layer, cache.cell.cell_w, cache.cell.cell_h) == FONT_OK);
    for (size_t i = 0; i < (size_t)cache.cell.cell_w * cache.cell.cell_h; i++) if (layer[i] > expected[i]) expected[i] = layer[i];
    uint32_t slot; REVIEW(complete_glyph(&cache, key, n, 1, &slot) == FONT_OK);
    render_glyph g = cache.glyphs[slot];
    for (uint32_t y = 0; y < g.h; y++) REVIEW(memcmp(expected + (size_t)y * g.w,
        cache.pages[g.page].pixels + (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0);
    edit_arena_free(&arena);
    printf("review §2 GREEN: zero-advance proportional fallback mark U+%04X px=%u\n", mark, f->primary.px); return 0;
}
static int review_controls(fixture *f)
{
    const uint32_t controls[] = {0x34f,0x180b,0x180c,0x180d,0x180f,0xfe0e,0xfe0f,0x200c,0x200d,0xe0100,0xe0020};
    edit_arena arena; REVIEW(edit_arena_init(&arena, 5u << 20) == 0);
    font_family primary; REVIEW(font_family_load(&primary, &f->primary, NULL, &arena) == FONT_OK);
    for (unsigned mode = 0; mode < 2; mode++) {
        edit_arena_reset(&arena); font_cache cache;
        REVIEW(font_cache_init(&cache, mode ? &f->family : &primary, &arena, 1, 128, 1024, 1u << 20) == FONT_OK);
        uint8_t expected[1920]; const uint32_t base[] = {'e'}; reference_image(f, base, 1, 1, expected);
        for (size_t i = 0; i < sizeof controls / sizeof controls[0]; i++) {
            uint8_t bytes[5] = {'e'}; size_t n = 1u + utf8_encode(controls[i], bytes + 1);
            uint32_t slot; REVIEW(complete_glyph(&cache, bytes, n, 1, &slot) == FONT_OK);
            render_glyph g = cache.glyphs[slot];
            for (uint32_t y = 0; y < g.h; y++)
                REVIEW(memcmp(expected + (size_t)y * g.w, cache.pages[g.page].pixels +
                       (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0);
        }
    }
    edit_arena_free(&arena); puts("review §3 GREEN: invisible controls preserve embedded/discovered base pixels");
    return 0;
}
static int review_invalid(fixture *f)
{
    const uint8_t bad[][6] = {{0xf4,0x8f,0xbf,0xbf,0xff,0}, {'e',0xff,0,0,0,0},
                             {0xf4,0x8f,0xbf,0xbf,0xe2,0x82}, {'e',0xe2,0x82,0,0,0}};
    const size_t lengths[] = {5,2,6,3};
    for (size_t i = 0; i < 4; i++) {
        size_t keys = f->cache.key_used, glyphs = f->cache.glyph_count;
        font_atlas atlas = f->cache.atlas; uint32_t slot = 123;
        REVIEW(complete_glyph(&f->cache, bad[i], lengths[i], 1, &slot) == FONT_ERR_ARG);
        REVIEW(slot == 123 && keys == f->cache.key_used && glyphs == f->cache.glyph_count);
        REVIEW(memcmp(&atlas, &f->cache.atlas, sizeof atlas) == 0 && f->cache.scratch.used == 0);
    }
    puts("review §11 GREEN: malformed suffixes return ARG without admission"); return 0;
}

static int review_slices(fixture *f)
{
    uint8_t bytes[FONT_CLUSTER_MAX_BYTES]; bytes[0] = 'e';
    for (size_t i = 1; i + 1 < sizeof bytes; i += 2) { bytes[i] = 0xcc; bytes[i + 1] = 0x81; }
    for (size_t len = sizeof bytes - 3u; len <= sizeof bytes - 1u; len += 2) {
        uint64_t before = f->cache.rasterizations; uint32_t slot = 123;
        int rc = font_cache_glyph(&f->cache, bytes, len, 1, &slot);
        REVIEW(rc == FONT_MORE && slot == 123);
        REVIEW(f->cache.rasterizations - before <= FONT_COMPOSE_MAX_GLYPHS);
        size_t calls = 1;
        while (rc == FONT_MORE && calls++ <= 8192) {
            before = f->cache.rasterizations;
            rc = font_cache_glyph(&f->cache, bytes, len, 1, &slot);
            REVIEW(f->cache.rasterizations - before <= FONT_COMPOSE_MAX_GLYPHS);
        }
        REVIEW(rc == FONT_OK && calls < 8192 && slot < f->cache.glyph_count);
        uint8_t expected[1920]; const uint32_t cps[] = {'e',0x301}; reference_image(f, cps, 2, 1, expected);
        render_glyph g = f->cache.glyphs[slot];
        for (uint32_t y = 0; y < g.h; y++) REVIEW(memcmp(expected + (size_t)y * g.w,
            f->cache.pages[g.page].pixels + (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0);
    }
    /* Direct layout must also survive a segmented key spanning many decode
     * slices and window refills, without external font preparation. */
    bytes[0] = 'n'; grid_setup(f, 2, 1);
    piece_tree *tree = make_tree(bytes, sizeof bytes - 1u);
    REVIEW(render_frame_begin(&f->grid, ++f->frame) == RENDER_OK);
    REVIEW(layout_begin(&f->l, tree, (layout_viewport){0,0,0,1}) == LAYOUT_DONE);
    int layout_rc; size_t slices = 0;
    do {
        uint64_t rasters = f->cache.rasterizations;
        layout_rc = layout_run(&f->l);
        REVIEW(f->cache.rasterizations - rasters <= FONT_COMPOSE_MAX_GLYPHS);
        REVIEW(++slices < 20000);
    } while (layout_rc == LAYOUT_MORE);
    REVIEW(layout_rc == LAYOUT_DONE && !layout_approximate(&f->l));
    REVIEW(f->cells[0].glyph_index != '?' && !f->cache.pending_len);
    piece_destroy(tree);
    puts("review §4 GREEN: long cluster and extension yield bounded slices, then exact pixels; direct layout completes"); return 0;
}
static int review_probes(fixture *f)
{
    edit_arena arena; REVIEW(edit_arena_init(&arena, 6u << 20) == 0); font_cache cache;
    REVIEW(font_cache_init(&cache, &f->family, &arena, 1, 8192, 65536, 1u << 20) == FONT_OK);
    uint8_t bytes[4]; uint32_t slot;
    for (uint32_t cp = 0xf0000; cp < 0xf2000; cp++) {
        size_t n = utf8_encode(cp, bytes); uint64_t before = cache.probes;
        REVIEW(complete_glyph(&cache, bytes, n, 1, &slot) == FONT_ERR_MISSING);
        REVIEW(cache.probes - before <= 2u * FONT_CACHE_MAX_PROBES);
    }
    /* Deliberate collision pressure: every primary-table bucket occupied. */
    for (size_t i = 0; i < cache.capacity; i++) cache.entries[i] = (font_cache_entry){0,0,1,1,0,FONT_OK};
    uint64_t before = cache.probes;
    REVIEW(complete_glyph(&cache, (const uint8_t *)"\xc3\xa9", 2, 1, &slot) == FONT_ERR_NOMEM);
    REVIEW(cache.probes - before <= 2u * FONT_CACHE_MAX_PROBES);
    edit_arena_free(&arena); puts("review §5 GREEN: saturated negatives and primary collisions have fixed probe bounds"); return 0;
}
static int fail_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot)
{
    (void)ctx; (void)bytes; (void)len; (void)width; (void)slot; return FONT_ERR_NOMEM;
}
static int review_recovery(fixture *f)
{
    /* Exercise the public layout continuation, without show's preparation
     * loop. Two cold clusters must finish even though each needs many slices. */
    for (unsigned wrap = 0; wrap < 2; wrap++) {
        uint8_t text[270]; size_t n = 0;
        for (unsigned base = 0; base < 2; base++) {
            text[n++] = base ? 'u' : 'n';
            for (unsigned i = 0; i < 64 + wrap; i++) { text[n++] = 0xcc; text[n++] = base ? 0x88 : 0x83; }
            text[n++] = ' ';
        }
        grid_setup(f, 4, 1);
        if (wrap) {
            REVIEW(layout_wrap_init(&f->l, &f->storage) == LAYOUT_DONE);
            REVIEW(layout_set_wrap(&f->l, true) == LAYOUT_DONE);
        }
        piece_tree *input = make_tree(text, n);
        REVIEW(render_frame_begin(&f->grid, ++f->frame) == RENDER_OK);
        REVIEW(layout_begin(&f->l, input, (layout_viewport){0,0,0,1}) == LAYOUT_DONE);
        int rc; size_t calls = 0;
        do {
            uint64_t rasters = f->cache.rasterizations;
            rc = layout_run(&f->l);
            REVIEW(f->cache.rasterizations - rasters <= 2u * FONT_COMPOSE_MAX_GLYPHS);
            REVIEW(++calls < 1000);
        } while (rc == LAYOUT_MORE);
        REVIEW(rc == LAYOUT_DONE && !layout_approximate(&f->l));
        REVIEW(f->cells[0].glyph_index != '?' && f->cells[2].glyph_index != '?');
        REVIEW(!f->cache.pending_len);
        piece_destroy(input);
    }
    grid_setup(f, 2, 1); piece_tree *tree = make_tree((const uint8_t *)"\xc3\xa9", 2);
    f->l.cfg.glyph = fail_glyph; show(f, tree, 0, 0, 0);
    REVIEW(layout_approximate(&f->l)); REVIEW(f->cells[0].glyph_index == '?');
    f->l.cfg.glyph = font_cache_glyph; show(f, tree, 0, 0, 0);
    REVIEW(!layout_approximate(&f->l) && f->cells[0].glyph_index != '?'); piece_destroy(tree);
    grid_setup(f, 2, 1);
    REVIEW(layout_wrap_init(&f->l, &f->storage) == LAYOUT_DONE);
    REVIEW(layout_set_wrap(&f->l, true) == LAYOUT_DONE);
    f->l.cfg.glyph = fail_glyph; tree = make_tree((const uint8_t *)"\xc3\xa9", 2);
    show(f, tree, 0, 0, 0); REVIEW(layout_approximate(&f->l)); piece_destroy(tree);
    edit_arena arena; REVIEW(edit_arena_init(&arena, 4u << 20) == 0); font_cache cache;
    REVIEW(font_cache_init(&cache, &f->family, &arena, 1, 4, 32, 1u << 20) == FONT_OK);
    uint32_t slot; uint8_t bytes[4];
    for (uint32_t cp = 0xf0000; cp < 0xf0004; cp++)
        REVIEW(complete_glyph(&cache, bytes, utf8_encode(cp, bytes), 1, &slot) == FONT_ERR_MISSING);
    REVIEW(complete_glyph(&cache, (const uint8_t *)"\xc3\xa9", 2, 1, &slot) == FONT_OK);
    edit_arena_free(&arena);
    uint8_t long_bytes[35]; long_bytes[0] = 'e';
    for (size_t i = 1; i < sizeof long_bytes; i += 2) { long_bytes[i] = 0xcc; long_bytes[i + 1] = 0x81; }
    grid_setup(f, 4, 1); tree = make_tree(long_bytes, sizeof long_bytes); show(f, tree, 0, 0, 0);
    REVIEW(!layout_approximate(&f->l) && f->cells[0].glyph_index != '?'); piece_destroy(tree);
    puts("review §6 GREEN: exhaustion is approximate/retryable; negatives reserve positive capacity"); return 0;
}

static int review_sequences(fixture *f)
{
    const uint32_t sequences[][5] = {{'n',0x303,0,0,0},{'u',0x308,0,0,0},
        {'e',0x301,0x308,0,0},{'e',0x361,0,0,0},{0x2600,0x200d,0x2601,0,0},
        {0x2600,0xfe0e,0,0,0},{0x2600,0xfe0f,0,0,0}};
    const size_t counts[] = {2,2,3,2,3,2,2};
    const uint32_t widths[] = {1,1,1,1,2,1,2};
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        uint8_t bytes[24], expected[1920]; size_t len = 0;
        for (size_t j = 0; j < counts[i]; j++) {
            REVIEW(sequences[i][j] == 0x200d || sequences[i][j] == 0xfe0e || sequences[i][j] == 0xfe0f || covering(f, sequences[i][j]));
            len += utf8_encode(sequences[i][j], bytes + len);
        }
        int width = 0; REVIEW(utf8_cluster(bytes, len, &width) == len && width == (int)widths[i]);
        grid_setup(f, 4, 1); piece_tree *tree = make_tree(bytes, len); show(f, tree, 0, 0, 0);
        REVIEW(f->cells[0].glyph_index != '?' && !layout_approximate(&f->l));
        if (width == 2) REVIEW(f->cells[0].attrs == RENDER_ATTR_WIDE_LEFT && f->cells[1].attrs == RENDER_ATTR_WIDE_RIGHT);
        else REVIEW(f->cells[0].attrs == 0);
        render_glyph g = f->grid.glyphs[f->cells[0].atlas_slot];
        REVIEW(g.w == widths[i] * f->cache.cell.cell_w && g.h == f->cache.cell.cell_h);
        reference_image(f, sequences[i], counts[i], widths[i], expected);
        for (uint32_t y = 0; y < g.h; y++) REVIEW(memcmp(expected + (size_t)y * g.w,
            f->grid.pages[g.page].pixels + (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0);
        piece_destroy(tree);
    }
    printf("review §12 GREEN: multi-mark/wider-mark/ZWJ/VS pixels and utf8 layout widths px=%u\n", f->primary.px);
    return 0;
}

typedef struct deferred_font {
    render_cell cells[4]; render_glyph glyphs[512]; render_atlas_page pages[9];
    raster_scene scene; uint32_t pixels[1920]; unsigned t5, t6;
} deferred_font;
static int deferred_init(render_backend *b, const render_config *config)
{
    deferred_font *d = b->state; memset(d, 0, sizeof *d);
    return config->dims.cols == 4 && config->dims.rows == 1 ? RENDER_OK : RENDER_ERR_ARG;
}
static int deferred_resize(render_backend *b, render_dims dims)
{
    (void)b; (void)dims; return RENDER_OK;
}
static int deferred_submit(render_backend *b, const render_grid *grid, const render_strip *strips, size_t count)
{
    (void)strips; (void)count; deferred_font *d = b->state;
    if (grid->glyph_count > 512 || grid->page_count > 9) return RENDER_ERR_CAPACITY;
    memcpy(d->cells, grid->cells, sizeof d->cells);
    memcpy(d->glyphs, grid->glyphs, grid->glyph_count * sizeof *d->glyphs);
    memcpy(d->pages, grid->pages, grid->page_count * sizeof *d->pages);
    d->scene = (raster_scene){grid->dims,d->cells,d->glyphs,grid->glyph_count,d->pages,grid->page_count,0};
    return RENDER_OK;
}
static int deferred_present(render_backend *b, uint32_t frame)
{
    (void)b; (void)frame; return RENDER_OK;
}
static int deferred_event(render_backend *b, const render_event *event)
{
    if (event->kind != RENDER_EVENT_WORK) return RENDER_ERR_UNSUPPORTED;
    deferred_font *d = b->state;
    raster_row_scalar(&d->scene, d->pixels, d->scene.dims.cols * d->scene.dims.cell_w, 0);
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, b->active_frame, 0);
    if (rc == RENDER_OK) rc = render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, b->active_frame, 0);
    if (rc != RENDER_OK) fprintf(stderr, "deferred signal rc=%d active=%u presented=%d\n", rc, b->active_frame, b->presented);
    return rc;
}
static void deferred_shutdown(render_backend *b) { (void)b; }
static void deferred_t5(void *arg, uint32_t frame, uint64_t ns)
{
    (void)frame; (void)ns; ((deferred_font *)arg)->t5++;
}
static void deferred_t6(void *arg, uint32_t frame, uint64_t ns)
{
    (void)frame; (void)ns; ((deferred_font *)arg)->t6++;
}
static int review_lifetime(fixture *f)
{
    edit_arena old_storage; REVIEW(edit_arena_init(&old_storage, 4u << 20) == 0); font_cache old;
    REVIEW(font_cache_init(&old, &f->family, &old_storage, 1, 4, 4096, 1u << 20) == FONT_OK);
    grid_setup(f, 4, 1); REVIEW(font_cache_bind(&old, &f->grid) == FONT_OK); f->l.cfg.glyph_ctx = &old;
    const uint8_t bytes[] = "e\xcc\x81"; piece_tree *tree = make_tree(bytes, sizeof bytes - 1);
    show(f, tree, 0, 0, 0); piece_destroy(tree);
    render_backend backend = {0}; deferred_font state;
    backend.info = (render_backend_info){"font-deferred", sizeof state, _Alignof(deferred_font), RENDER_CAP_HEADLESS};
    backend.ops = (render_backend_ops){deferred_init,deferred_resize,deferred_submit,deferred_present,deferred_event,deferred_shutdown};
    render_config config = {0}; config.dims = f->grid.dims;
    config.max_width = 64; config.max_height = 30;
    uint32_t stride = 4u * f->cache.cell.cell_w, height = f->cache.cell.cell_h;
    config.max_cells = 4; config.max_glyphs = 512; config.max_pages = 9; config.max_atlas_bytes = 9u << 20;
    config.hooks = (render_hooks){deferred_t5,deferred_t6,&state};
    REVIEW(render_backend_init(&backend, &config, &state, sizeof state) == RENDER_OK);
    render_strip strip = {0,1};
    REVIEW(render_backend_submit(&backend, &f->grid, &strip, 1) == RENDER_OK);
    uint32_t submitted_frame = f->frame;
    uint32_t expected[1920]; raster_row_scalar(&state.scene, expected, stride, 0);
    REVIEW(render_backend_present(&backend, f->frame) == RENDER_OK && state.t5 == 0);
    font_family replacement_family = f->family;
    for (uint32_t i = 0; i < replacement_family.count; i++)
        REVIEW(font_set_px(&replacement_family.faces[i], f->primary.px == 15 ? 30u : 15u) == FONT_OK);
    edit_arena replacement_storage; REVIEW(edit_arena_init(&replacement_storage, 4u << 20) == 0); font_cache replacement;
    REVIEW(font_cache_init(&replacement, &replacement_family, &replacement_storage, 1, 128, 4096, 1u << 20) == FONT_OK);
    render_dims replacement_dims = {4,1,replacement.cell.cell_w,replacement.cell.cell_h};
    REVIEW(render_backend_resize(&backend, replacement_dims) == RENDER_ERR_BUSY);
    /* The pending backend owns metadata, but still borrows old R8 pixels. */
    grid_setup(f, 4, 1); REVIEW(font_cache_bind(&old, &f->grid) == FONT_OK); f->l.cfg.glyph_ctx = &old;
    tree = make_tree((const uint8_t *)"u\xcc\x88", 3); show(f, tree, 0, 0, 0); piece_destroy(tree);
    tree = make_tree((const uint8_t *)"n\xcc\x83", 3); show(f, tree, 0, 0, 0); piece_destroy(tree);
    tree = make_tree((const uint8_t *)"\xc3\xa9", 2); show(f, tree, 0, 0, 0); piece_destroy(tree);
    REVIEW(old.resource_error == FONT_ERR_NOMEM && layout_approximate(&f->l));
    REVIEW(f->cells[0].glyph_index == '?');
    REVIEW(state.t5 == 0 && render_backend_submit(&backend, &f->grid, &strip, 1) == RENDER_ERR_BUSY);
    work_msg completion = {0};
    int event_rc = render_backend_event(&backend, &(render_event){RENDER_EVENT_WORK, submitted_frame, 0, &completion});
    if (event_rc != RENDER_OK) fprintf(stderr, "deferred event rc=%d\n", event_rc);
    REVIEW(event_rc == RENDER_OK);
    REVIEW(state.t5 == 1 && state.t6 == 1);
    REVIEW(memcmp(expected, state.pixels, (size_t)stride * height * sizeof *expected) == 0);
    /* Only now retire pixels and replace the cache/grid, including pixel size. */
    edit_arena_free(&old_storage);
    REVIEW(render_backend_resize(&backend, replacement_dims) == RENDER_OK);
    render_cell replacement_cells[4];
    for (size_t i = 0; i < 4; i++) replacement_cells[i] = (render_cell){0,RENDER_NO_SLOT,0,0,0,0};
    render_grid replacement_grid; uint64_t dirty;
    REVIEW(render_grid_init(&replacement_grid, replacement_dims, replacement_cells, 4, &dirty, 1) == RENDER_OK);
    REVIEW(font_cache_bind(&replacement, &replacement_grid) == FONT_OK);
    REVIEW(render_frame_begin(&replacement_grid, ++f->frame) == RENDER_OK);
    layout replacement_layout; uint64_t row_byte; uint32_t row_used;
    layout_config replacement_config = f->l.cfg;
    replacement_config.glyph_ctx = &replacement;
    REVIEW(layout_init(&replacement_layout, &replacement_grid, &replacement_config, &row_byte, &row_used) == LAYOUT_DONE);
    tree = make_tree((const uint8_t *)"\xc3\xa9", 2);
    REVIEW(layout_begin(&replacement_layout, tree, (layout_viewport){0,0,0,1}) == LAYOUT_DONE);
    int layout_rc; size_t retries = 0;
    do { layout_rc = layout_run(&replacement_layout); REVIEW(++retries < 100); } while (layout_rc == LAYOUT_MORE);
    REVIEW(layout_rc == LAYOUT_DONE && !layout_approximate(&replacement_layout));
    REVIEW(replacement_cells[0].glyph_index != '?' && replacement.resource_error == 0);
    piece_destroy(tree);
    REVIEW(render_backend_submit(&backend, &replacement_grid, &strip, 1) == RENDER_OK);
    REVIEW(render_backend_present(&backend, f->frame) == RENDER_OK);
    REVIEW(render_backend_event(&backend, &(render_event){RENDER_EVENT_WORK,f->frame,0,&completion}) == RENDER_OK);
    REVIEW(state.t5 == 2 && state.t6 == 2);
    render_backend_shutdown(&backend); edit_arena_free(&replacement_storage);
    grid_setup(f, 4, 1);
    printf("review §13 GREEN: delayed T5 preserves pixels; exhausted cache replaced and correct content retried, px=%u\n", f->primary.px);
    return 0;
}

int main(void)
{
    fixture *f = malloc(sizeof *f); CHECK(f);
    const char *review = getenv("FONT_REVIEW");
    if (review) {
        int result = 0;
        for (uint32_t px = 15; px <= 30; px += 15) {
            setup(f, px);
            if (strcmp(review, "2") == 0) result |= review_marks(f) | review_proportional_mark(f);
            if (strcmp(review, "3") == 0) result |= review_controls(f);
            if (strcmp(review, "4") == 0) result |= review_slices(f);
            if (strcmp(review, "5") == 0) result |= review_probes(f);
            if (strcmp(review, "6") == 0) result |= review_recovery(f);
            if (strcmp(review, "12") == 0) result |= review_sequences(f);
            if (strcmp(review, "13") == 0) result |= review_lifetime(f);
            if (strcmp(review, "11") == 0) result |= review_invalid(f);
            teardown(f);
        }
        free(f); return result;
    }
    setup(f, 15); CHECK(review_marks(f) == 0); CHECK(review_proportional_mark(f) == 0); CHECK(review_controls(f) == 0); CHECK(review_invalid(f) == 0); CHECK(review_slices(f) == 0); CHECK(review_probes(f) == 0); CHECK(review_recovery(f) == 0); CHECK(review_sequences(f) == 0); CHECK(review_lifetime(f) == 0); screenshots(f); corpus(f); cache_contracts(f); teardown(f);
    setup(f, 30); CHECK(review_marks(f) == 0); CHECK(review_proportional_mark(f) == 0); CHECK(review_controls(f) == 0); CHECK(review_sequences(f) == 0); CHECK(review_lifetime(f) == 0); screenshots(f); teardown(f);
    free(f);
    puts("unicode_render_test: all passed (headless)");
    return 0;
}
