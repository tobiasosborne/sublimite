/* libFuzzer target for src/font (P2.3c, edit-e6x.10). The input is a whole
 * font file (TTF/OTF/TTC); libFuzzer hands it over in an exact-size heap
 * buffer, so any read past data[size-1] by font_init_index, the bounded cmap,
 * metrics or the stb rasteriser is an ASan error. Properties:
 *   - font_init_index returns FONT_OK or FONT_ERR_INIT/ARG, never crashes;
 *   - after FONT_OK, set_px, metrics and raster of a spread of codepoints
 *     (ASCII, Latin-1, CJK, symbols, input-derived) return clean codes;
 *   - a successful raster has pixels iff w*h > 0 and the arena is rewound. */
#include "font/font.h"
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

static edit_arena arena;
static int arena_ready;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
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
