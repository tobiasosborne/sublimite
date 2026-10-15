/* libFuzzer target for src/font/gsub.c (edit-v16). The input is a raw GSUB
 * table in an exact-size heap buffer, so any read past data[size-1] by the
 * validator, the apply path or the cluster filter is an ASan error.
 * Properties:
 *   - font_gsub_parse returns FONT_OK (state READY, 1..FONT_GSUB_MAX_LOOKUPS
 *     lookups) or an error with state NONE, never crashes, and its validator
 *     work stays within its budget;
 *   - after FONT_OK, font_gsub_apply keeps the glyph count within capacity
 *     and returns >= 0 or FONT_ERR_NOMEM (budget);
 *   - font_gsub_cluster on the fixture face with this table returns FONT_OK
 *     with 1..FONT_GSUB_MAX_GLYPHS nonzero glyphs, or FONT_ERR_MISSING.
 * Seed: FONT_GSUB_FUZZ_SEED_DIR=<dir> writes the fixture's own GSUB table
 * (tests/fixtures/fonts/NotoEmoji-wght.ttf) as a seed file and exits. Run
 * from the repo root. */
#include "font/font.h"
#include "font/gsub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

static unsigned char *fixture;
static size_t fixture_len;
static font_t face;

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    FILE *fp = fopen("tests/fixtures/fonts/NotoEmoji-wght.ttf", "rb");
    if (!fp) { fprintf(stderr, "font_gsub_fuzz: run from the repo root\n"); exit(2); }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fixture = n > 0 ? malloc((size_t)n) : NULL;
    if (!fixture || fread(fixture, 1, (size_t)n, fp) != (size_t)n) exit(2);
    fclose(fp);
    fixture_len = (size_t)n;
    REQUIRE(font_init(&face, fixture, fixture_len) == FONT_OK);
    REQUIRE(face.gsub.table && face.gsub.state == FONT_GSUB_UNPARSED);
    font_gsub g;
    REQUIRE(font_gsub_parse(&g, face.gsub.table, face.gsub.len, NULL) == FONT_OK);
    const char *dir = getenv("FONT_GSUB_FUZZ_SEED_DIR");
    if (dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/noto_emoji_gsub.bin", dir);
        FILE *out = fopen(path, "wb");
        if (!out || fwrite(face.gsub.table, 1, face.gsub.len, out) != face.gsub.len) exit(2);
        fclose(out);
        exit(0);
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    font_gsub g;
    font_gsub_stats st;
    int rc = font_gsub_parse(&g, data, size, &st);
    REQUIRE(st.reads <= st.budget || rc != FONT_OK);
    if (rc != FONT_OK) {
        REQUIRE(g.state == FONT_GSUB_NONE);
    } else {
        REQUIRE(g.state == FONT_GSUB_READY && g.count >= 1u && g.count <= FONT_GSUB_MAX_LOOKUPS);
        /* Glyph arrays: fixture emoji ids plus ids drawn from the input. */
        uint64_t h = UINT64_C(14695981039346656037);
        for (size_t i = 0; i < size && i < 64u; i++) h = (h ^ data[i]) * UINT64_C(1099511628211);
        static const uint16_t fixed[][5] = {
            {606, 18, 691, 0, 0}, {578, 494, 0, 0, 0}, {207, 227, 0, 0, 0}, {1878, 207, 0, 0, 0},
        };
        static const uint16_t skip[] = {19, 20};
        for (size_t k = 0; k < 6u; k++) {
            uint16_t glyphs[FONT_GSUB_MAX_GLYPHS];
            size_t n;
            if (k < 4u) { n = 0; while (n < 5u && fixed[k][n]) { glyphs[n] = fixed[k][n]; n++; } }
            else {
                n = 1u + (size_t)(h % FONT_GSUB_MAX_GLYPHS);
                for (size_t i = 0; i < n; i++) { h = h * UINT64_C(6364136223846793005) + 1u; glyphs[i] = (uint16_t)(h >> 48); }
            }
            int subs = font_gsub_apply(&g, glyphs, &n, FONT_GSUB_MAX_GLYPHS, skip, 2, NULL);
            REQUIRE(subs >= 0 || subs == FONT_ERR_NOMEM);
            REQUIRE(n <= FONT_GSUB_MAX_GLYPHS);
        }
    }
    /* Cluster path: the fixture face carrying this table, validated lazily. */
    font_t f = face;
    f.gsub.table = data; f.gsub.len = (uint32_t)(size <= UINT32_MAX ? size : 0u);
    f.gsub.state = data && size ? FONT_GSUB_UNPARSED : FONT_GSUB_NONE;
    static const char *clusters[] = {"👩‍💻", "👍🏽", "🇦🇺", "👩🏻‍❤️‍💋‍👨🏼", "😀️", "🇦"};
    for (size_t i = 0; i < sizeof clusters / sizeof clusters[0]; i++) {
        uint16_t out[FONT_GSUB_MAX_GLYPHS];
        size_t n = 0;
        int c = font_gsub_cluster(&f, (const uint8_t *)clusters[i], strlen(clusters[i]), out, &n);
        REQUIRE(c == FONT_ERR_MISSING || (c == FONT_OK && n >= 1u && n <= FONT_GSUB_MAX_GLYPHS));
        for (size_t j = 0; c == FONT_OK && j < n; j++) REQUIRE(out[j] != 0);
        REQUIRE(f.gsub.state == FONT_GSUB_READY || f.gsub.state == FONT_GSUB_NONE);
    }
    return 0;
}
