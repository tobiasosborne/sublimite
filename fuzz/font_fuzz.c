/* libFuzzer target for src/font (P2.3c, edit-e6x.10). The input is a whole
 * font file (TTF/OTF/TTC); libFuzzer hands it over in an exact-size heap
 * buffer, so any read past data[size-1] by font_init_index, the bounded cmap,
 * metrics or the stb rasteriser is an ASan error. Properties:
 *   - font_init_index returns FONT_OK or FONT_ERR_INIT/ARG, never crashes;
 *   - after FONT_OK, set_px, metrics and raster of a spread of codepoints
 *     (ASCII, Latin-1, CJK, symbols, input-derived) return clean codes;
 *   - a successful raster has pixels iff w*h > 0 and the arena is rewound. */
#include "font/font.h"
#include "utf8/utf8.h"
#include "font_cffseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

static edit_arena arena;
static int arena_ready;

/* P2.3e CFF seeds: FONT_FUZZ_SEED_DIR=<dir> writes the three synthetic OTFs
 * (cffseed_build variants 0..2) as seed files and exits; run the fuzzer with
 * that dir as corpus. Generated at run time, nothing vendored. */
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    const char *dir = getenv("FONT_FUZZ_SEED_DIR");
    if (!dir) return 0;
    static uint8_t buf[4096];
    for (int v = 0; v < 3; v++) {
        cffseed_layout L;
        size_t n = cffseed_build(buf, sizeof buf, v, &L);
        if (n == 0 || n > sizeof buf) exit(2);
        char path[512];
        snprintf(path, sizeof path, "%s/cff_seed_%d.otf", dir, v);
        FILE *fp = fopen(path, "wb");
        if (!fp || fwrite(buf, 1, n, fp) != n) exit(2);
        fclose(fp);
    }
    exit(0);
}

/* P4.11 mode uses only the trusted embedded face: arbitrary cluster bytes,
 * widths, exact cache keys, cached errors and exhaustion, not malformed-font
 * parsing. FONT_FUZZ_UNICODE=1 selects it for every input in the campaign. */
static int unicode_clusters(const uint8_t *data, size_t size)
{
    if (size == 0) return 0;
    edit_arena files, storage;
    REQUIRE(edit_arena_init(&files, 1u << 20) == 0);
    REQUIRE(edit_arena_init(&storage, 4u << 20) == 0);
    size_t len = 0;
    unsigned char *bytes = font_load_file("vendor/DejaVuSansMono.ttf", &files, &len);
    font_t primary; font_family family; font_cache cache;
    REQUIRE(bytes && font_init(&primary, bytes, len) == FONT_OK && font_set_px(&primary, 15) == FONT_OK);
    REQUIRE(font_family_load(&family, &primary, NULL, &files) == FONT_OK);
    REQUIRE(font_cache_init(&cache, &family, &storage, 1, 1u + data[0] % 32u,
                            1u + data[0], 1u << 20) == FONT_OK);
    for (size_t off = 1; off < size;) {
        int width; size_t n = utf8_cluster(data + off, size - off, &width);
        REQUIRE(n && n <= size - off && width >= 0 && width <= 2);
        if (width != 0 && n <= FONT_CLUSTER_MAX_BYTES) {
            uint32_t a = RENDER_NO_SLOT, b = RENDER_NO_SLOT;
            int ra = font_cache_glyph(&cache, data + off, n, (uint32_t)width, &a);
            REQUIRE(ra == FONT_OK || ra == FONT_ERR_ARG || ra == FONT_ERR_MISSING ||
                    ra == FONT_ERR_NOMEM || ra == FONT_ERR_INIT);
            size_t keys = cache.key_used, glyphs = cache.glyph_count, used = storage.used;
            font_atlas atlas = cache.atlas;
            int rb = font_cache_glyph(&cache, data + off, n, (uint32_t)width, &b);
            REQUIRE(ra == rb && a == b && keys == cache.key_used && glyphs == cache.glyph_count && used == storage.used);
            REQUIRE(memcmp(&atlas, &cache.atlas, sizeof atlas) == 0 && cache.scratch.used == 0);
            if (ra == FONT_OK && a != RENDER_NO_SLOT) {
                REQUIRE(a < cache.glyph_count && cache.glyphs[a].w == cache.cell.cell_w * (uint32_t)width);
                REQUIRE(cache.glyphs[a].page <= cache.atlas.npages && cache.glyphs[a].h == cache.cell.cell_h);
            }
        }
        off += n;
    }
    edit_arena_free(&storage); edit_arena_free(&files);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (getenv("FONT_FUZZ_UNICODE")) return unicode_clusters(data, size);
    if (!arena_ready) {
        REQUIRE(edit_arena_init(&arena, 4u << 20) == 0);
        arena_ready = 1;
    }
    if (size < 12) return 0;
    uint32_t index = data[size - 1] & 3u;
    font_t f;
    int r = font_init_index(&f, data, size, index);
    REQUIRE(r == FONT_OK || r == FONT_ERR_INIT || r == FONT_ERR_ARG);
    if (r != FONT_OK) return 0;
    uint32_t px = 10u + (data[size / 2] % 3u) * 11u;
    REQUIRE(font_set_px(&f, px) == FONT_OK);
    uint32_t cps[16] = { 'A', 'g', '@', 'W', 0xE9u, 0x20ACu, 0x4E2Du, 0x3042u, 0x2588u, 0x1F600u };
    for (size_t i = 10; i < 16; i++) {
        size_t at = (size_t)(i * 2654435761u) % (size - 3u);
        cps[i] = ((uint32_t)data[at] << 8 | data[at + 1]) % 0x30000u;
    }
    for (size_t i = 0; i < 16; i++) {
        font_metric m;
        int mr = font_glyph_metrics(&f, cps[i], &m);
        REQUIRE(mr == FONT_OK || mr == FONT_ERR_MISSING || mr == FONT_ERR_INIT);
        edit_arena_mark_t mk = edit_arena_mark(&arena);
        font_bitmap b;
        int rr = font_raster_glyph(&f, cps[i], &arena, &b);
        REQUIRE(rr == FONT_OK || rr == FONT_ERR_MISSING || rr == FONT_ERR_INIT || rr == FONT_ERR_NOMEM);
        if (rr == FONT_OK) REQUIRE((b.pixels != NULL) == ((size_t)b.w * b.h > 0));
        edit_arena_reset_to_mark(&arena, mk);
        REQUIRE(edit_arena_mark(&arena) == mk);
    }
    return 0;
}
