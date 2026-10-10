/* tests/font_test.c - P2.3 (edit-e6x.3): bake consistency, runtime raster,
 * missing glyphs, shelf allocator. Reads vendor/DejaVuSansMono.ttf from the
 * repo root (make check runs from there). */
#include "base/base.h"
#include "font/font.h"
#include "font/file.h"
#include "layout/layout.h"
#include "work/work.h"
#include "../vendor/stb_truetype.h"   /* declarations only: glyph ids for hostile-glyph tests */
#include "../fuzz/font_cffseed.h"
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
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


/* ---- P2.3c: hostile font data. Every mutated copy lives in an exact-size
 * malloc block so ASan flags any read past len. ---- */
static uint16_t be16(const unsigned char *p) { return (uint16_t)((uint16_t)p[0] * 256u + p[1]); }
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put16(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

/* Offset of the directory record for tag in the offset table at base (0 = none). */
static size_t rec_of(const unsigned char *b, size_t base, const char *tag)
{
    size_t n = be16(b + base + 4);
    for (size_t i = 0; i < n; i++)
        if (memcmp(b + base + 12 + 16 * i, tag, 4) == 0) return base + 12 + 16 * i;
    return 0;
}
static size_t tab_off(const unsigned char *b, const char *tag) { return be32(b + rec_of(b, 0, tag) + 8); }
static size_t tab_len(const unsigned char *b, const char *tag) { return be32(b + rec_of(b, 0, tag) + 12); }

/* Init on an exact-size copy of b[0..n); if it succeeds, raster a few glyphs
 * (a clean result of any kind is fine, a sanitizer report is the failure). */
static int init_exact(const unsigned char *b, size_t n, uint32_t index, int *ok_raster)
{
    unsigned char *c = malloc(n ? n : 1u);
    if (!c) return -99;
    if (n) memcpy(c, b, n);
    font_t f;
    int r = font_init_index(&f, c, n, index);
    if (r == FONT_OK && ok_raster) {
        edit_arena a;
        CHECK(edit_arena_init(&a, 1u << 20) == 0);
        (void)font_set_px(&f, 20);
        static const uint32_t cps[] = { 'A', 'g', 0xE9u, '@', 'W', 0x4E2Du, 0x20ACu, 0x2588u };
        for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) {
            edit_arena_mark_t mk = edit_arena_mark(&a);
            font_bitmap bm;
            font_metric m;
            (void)font_glyph_metrics(&f, cps[i], &m);
            (void)font_raster_glyph(&f, cps[i], &a, &bm);
            edit_arena_reset_to_mark(&a, mk);
        }
        edit_arena_free(&a);
    }
    free(c);
    return r;
}

/* Sparse address-space fixture: no multi-gigabyte disk or physical copy. */
static void test_signed_offsets(const unsigned char *ttf, size_t len)
{
    size_t huge = (size_t)INT_MAX + 4096u;
    unsigned char *bytes = mmap(NULL, huge, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    CHECK(bytes != MAP_FAILED);
    if (bytes == MAP_FAILED) return;
    memcpy(bytes, ttf, len);
    font_t face;
    CHECK(font_init(&face, bytes, huge) == FONT_ERR_INIT);
    CHECK(face.data == NULL);
    size_t head = tab_off(ttf, "head");
    memcpy(bytes + UINT64_C(0x80000000), ttf + head, 54u);
    put32(bytes + rec_of(bytes, 0, "head") + 8u, 0x80000000u);
    CHECK(font_init(&face, bytes, huge) == FONT_ERR_INIT && face.data == NULL);
    memcpy(bytes, "ttcf", 4); put32(bytes + 4u, 0x10000u);
    put32(bytes + 8u, 1); put32(bytes + 12u, 0x80000000u);
    CHECK(font_init_index(&face, bytes, huge, 0) == FONT_ERR_INIT && face.data == NULL);
    CHECK(munmap(bytes, huge) == 0);
    /* stb also narrows cmap-relative offsets during initialization. Reject
     * before its signed byte shifts or relative-offset addition execute. */
    bytes = malloc(len); CHECK(bytes);
    if (!bytes) return;
    memcpy(bytes, ttf, len);
    size_t cmap = tab_off(bytes, "cmap");
    for (uint32_t i = 0; i < be16(bytes + cmap + 2u); i++) {
        size_t record = cmap + 4u + 8u * i;
        put32(bytes + record + 4u, 0x80000000u);
    }
    CHECK(font_init(&face, bytes, len) == FONT_ERR_INIT && face.data == NULL);
    free(bytes);
    if (!failures) puts("P2-1 §3 GREEN: sparse oversized font and cmap-relative offsets refused before stb");
}

static void test_font_bounds(const unsigned char *ttf, size_t len)
{
    static const char *const tags[] = { "head", "hhea", "hmtx", "loca", "glyf", "cmap", "maxp" };
    unsigned char *w = malloc(len);
    CHECK(w != NULL);
    if (!w) return;

    /* truncation: at every table start, and one byte short of every table end */
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        size_t o = tab_off(ttf, tags[i]), l = tab_len(ttf, tags[i]);
        if (init_exact(ttf, o, 0, NULL) != FONT_ERR_INIT) {
            fprintf(stderr, "trunc at start of %s not rejected\n", tags[i]); failures++;
        }
        if (init_exact(ttf, o + l - 1u, 0, NULL) != FONT_ERR_INIT) {
            fprintf(stderr, "trunc inside %s not rejected\n", tags[i]); failures++;
        }
    }
    CHECK(init_exact(ttf, 0, 0, NULL) == FONT_ERR_ARG);
    CHECK(init_exact(ttf, 11, 0, NULL) == FONT_ERR_ARG);
    CHECK(init_exact(ttf, 12, 0, NULL) == FONT_ERR_INIT);
    CHECK(init_exact(ttf, 12 + 16, 0, NULL) == FONT_ERR_INIT);

    /* table record offset/length lying */
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        size_t rec = rec_of(ttf, 0, tags[i]);
        memcpy(w, ttf, len); put32(w + rec + 8, (uint32_t)len + 100u);
        CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
        memcpy(w, ttf, len); put32(w + rec + 8, 0xFFFFFFF0u);
        CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
        memcpy(w, ttf, len); put32(w + rec + 12, 0xFFFFFFFFu);
        CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    }
    memcpy(w, ttf, len); put16(w + 4, 0xFFFFu);              /* numTables huge */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put32(w, 0x12345678u);              /* unknown sfnt version */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);

    /* scalar fields */
    size_t head = tab_off(ttf, "head"), hhea = tab_off(ttf, "hhea"), maxp = tab_off(ttf, "maxp");
    uint32_t ng = be16(ttf + maxp + 4);
    CHECK(ng > 100u);
    memcpy(w, ttf, len); put16(w + maxp + 4, 0xFFFFu);       /* numGlyphs beyond loca */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + maxp + 4, 0);
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + hhea + 34, 0xFFFFu);      /* numberOfHMetrics > numGlyphs */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + hhea + 34, 0);
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + head + 50, 2);            /* unknown loca format */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + head + 18, 0);            /* unitsPerEm == 0 */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + hhea + 4, 0); put16(w + hhea + 6, 0);      /* ascent = descent = 0 */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + hhea + 4, 0xFC00u);       /* negative ascent */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);
    memcpy(w, ttf, len); put16(w + hhea + 6, 0x0400u);       /* positive descent */
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);

    /* loca: non-monotonic, and last entry past glyf */
    size_t loca = tab_off(ttf, "loca");
    int fmt = (int)be16(ttf + head + 50);
    memcpy(w, ttf, len);
    if (fmt) { put32(w + loca + 4 * 10, 0x00FFFFFFu); } else { put16(w + loca + 2 * 10, 0xFFFFu); }
    CHECK(init_exact(w, len, 0, NULL) == FONT_ERR_INIT);

    /* glyph-level: shrink glyph 'A' so its header/contours run past its range;
     * init succeeds, raster must refuse cleanly */
    {
        font_t f0;
        CHECK(font_init(&f0, ttf, len) == FONT_OK);
        CHECK(font_set_px(&f0, 20) == FONT_OK);
        uint32_t gA = (uint32_t)stbtt_FindGlyphIndex((const stbtt_fontinfo *)(const void *)f0.info, 'A');
        CHECK(gA != 0);
        size_t glyf = tab_off(ttf, "glyf");
        uint32_t o = fmt ? be32(ttf + loca + 4 * gA) : 2u * be16(ttf + loca + 2 * gA);
        uint32_t e = fmt ? be32(ttf + loca + 4 * (gA + 1u)) : 2u * be16(ttf + loca + 2 * (gA + 1u));
        CHECK(e > o + 24u);
        edit_arena a;
        CHECK(edit_arena_init(&a, 1u << 16) == 0);
        font_bitmap bm;
        int saw = 0;
        for (uint32_t cut = 0; cut < 24u; cut += 3) {       /* glyph truncated to o+cut bytes */
            memcpy(w, ttf, len);
            if (fmt) put32(w + loca + 4 * (gA + 1u), o + cut); else put16(w + loca + 2 * (gA + 1u), (o + cut) / 2u);
            if (cut % 2 && !fmt) continue;
            unsigned char *c = malloc(len);
            memcpy(c, w, len);
            font_t f;
            if (font_init(&f, c, len) == FONT_OK && font_set_px(&f, 20) == FONT_OK) {
                edit_arena_mark_t mk = edit_arena_mark(&a);
                int r = font_raster_glyph(&f, 'A', &a, &bm);
                if (cut == 0) CHECK(r == FONT_OK || r == FONT_ERR_MISSING);   /* empty glyph */
                else { CHECK(r == FONT_ERR_INIT); saw++; }
                edit_arena_reset_to_mark(&a, mk);
            }
            free(c);
        }
        CHECK(saw > 0);
        /* corrupt contour count: endPts not increasing */
        memcpy(w, ttf, len);
        put16(w + glyf + o + 10, 5); put16(w + glyf + o + 12, 3);
        font_t f;
        if (font_init(&f, w, len) == FONT_OK && font_set_px(&f, 20) == FONT_OK) {
            edit_arena_mark_t mk = edit_arena_mark(&a);
            (void)font_raster_glyph(&f, 'A', &a, &bm);   /* any clean result; ASan referees */
            edit_arena_reset_to_mark(&a, mk);
        }
        /* self-referencing composite: must terminate with an error, not recurse */
        memcpy(w, ttf, len);
        put16(w + glyf + o, 0xFFFFu);                 /* numberOfContours = -1 */
        put16(w + glyf + o + 10, 0x0003u);            /* ARG_WORDS | XY */
        put16(w + glyf + o + 12, gA);                 /* component = itself */
        put32(w + glyf + o + 14, 0);
        if (font_init(&f, w, len) == FONT_OK && font_set_px(&f, 20) == FONT_OK) {
            edit_arena_mark_t mk = edit_arena_mark(&a);
            CHECK(font_raster_glyph(&f, 'A', &a, &bm) == FONT_ERR_INIT);
            edit_arena_reset_to_mark(&a, mk);
        }
        edit_arena_free(&a);
    }

    /* cmap format 4 with a segment count that runs off the table */
    {
        size_t cm = tab_off(ttf, "cmap");
        uint32_t nt = be16(ttf + cm + 2);
        int did = 0;
        for (uint32_t i = 0; i < nt; i++) {
            size_t sub = cm + be32(ttf + cm + 4 + 8 * i + 4);
            if (be16(ttf + sub) == 4) {
                memcpy(w, ttf, len); put16(w + sub + 6, 0xFFFEu);
                (void)init_exact(w, len, 0, &did);
                memcpy(w, ttf, len); put16(w + sub + 14 + 0, 0xFFFFu); /* arbitrary end code */
                (void)init_exact(w, len, 0, &did);
                did = 1;
            }
        }
        CHECK(did);
    }
    free(w);
    printf("font_test: hostile font data rejected without out-of-bounds reads\n");
}

/* Synthetic 2-face TTC: header, face 0 = ttf, face 1 = ttf with a different hhea ascent. */

/* ---- P2.3e: hostile CFF/OTTO data. A synthetic OTF (fuzz/font_cffseed.h) is
 * mutated byte by byte, truncated inside every CFF structure and given
 * out-of-range offsets; the Noto CJK CFF faces on this box get the same
 * truncation treatment. Exact-size malloc blocks: ASan flags any read past
 * len; a stb assert or a hang is also a failure. ---- */
static void on_alarm(int sig)
{
    (void)sig;
    static const char m[] = "font_test: TIMEOUT in CFF test (interpreter has no step budget)\n";
    if (write(2, m, sizeof m - 1) < 0) {}
    _exit(3);
}

/* Init on an exact-size copy; if it works, metrics+raster every glyph and the
 * cmapped codepoints. Returns the init result; any clean error code is fine. */
static int cff_exercise(const unsigned char *b, size_t n, uint32_t index, uint32_t *rastered)
{
    unsigned char *c = malloc(n ? n : 1u);
    if (!c) return -99;
    if (n) memcpy(c, b, n);
    font_t f;
    int r = font_init_index(&f, c, n, index);
    if (r == FONT_OK) {
        edit_arena a;
        CHECK(edit_arena_init(&a, 1u << 20) == 0);
        if (font_set_px(&f, 20) == FONT_OK) {
            static const uint32_t cps[] = { 'A', 'B', 'C', 'g', 0x4E2Du, 0x20ACu };
            for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) {
                edit_arena_mark_t mk = edit_arena_mark(&a);
                font_bitmap bm;
                font_metric m;
                int mr = font_glyph_metrics(&f, cps[i], &m);
                CHECK(mr == FONT_OK || mr == FONT_ERR_MISSING || mr == FONT_ERR_INIT);
                int rr = font_raster_glyph(&f, cps[i], &a, &bm);
                CHECK(rr == FONT_OK || rr == FONT_ERR_MISSING || rr == FONT_ERR_INIT || rr == FONT_ERR_NOMEM);
                if (rr == FONT_OK && bm.pixels && rastered) (*rastered)++;
                edit_arena_reset_to_mark(&a, mk);
            }
        }
        edit_arena_free(&a);
    }
    free(c);
    return r;
}

/* Replace B in the minimal CFF while preserving INDEX and Private offsets. */
static size_t cff_with_outline(unsigned char *buf, size_t cap,
                               const unsigned char *outline, size_t outline_len)
{
    cffseed_layout seed_layout;
    size_t n = cffseed_build(buf, cap, 0, &seed_layout);
    if (!n || outline_len < 3u) return 0;
    size_t data = seed_layout.cs_idx + 11u;
    size_t bstart = data + be16(buf + seed_layout.cs_idx + 7u) - 1u;
    size_t growth = outline_len - 3u;
    if (n + growth > cap || outline_len + 13u > 65535u) return 0;
    memmove(buf + bstart + outline_len, buf + bstart + 3u, n - bstart - 3u);
    memcpy(buf + bstart, outline, outline_len);
    put16(buf + seed_layout.cs_idx + 9u, (uint32_t)(outline_len + 13u));
    put32(buf + seed_layout.cs_off_pos + 11u,
          be32(buf + seed_layout.cs_off_pos + 11u) + (uint32_t)growth);
    put32(buf + seed_layout.rec_cff + 12u, (uint32_t)(seed_layout.cff_len + growth));
    return n + growth;
}

static void test_cff_coordinates(void)
{
    unsigned char *bytes = malloc(65536u), *outline = malloc(60000u);
    CHECK(bytes && outline);
    if (!bytes || !outline) { free(bytes); free(outline); return; }
    edit_arena scratch; CHECK(edit_arena_init(&scratch, 1u << 20) == 0);
    for (unsigned variant = 0; variant < 2; variant++) {
        size_t at = 0;
        outline[at++] = 139; outline[at++] = 139; outline[at++] = 21;
        unsigned lines = variant ? 1u : 7000u;
        for (unsigned i = 0; i < lines; i++) {
            outline[at++] = 28; outline[at++] = 0x7f; outline[at++] = 0xff;
            outline[at++] = 140; outline[at++] = 5;
        }
        outline[at++] = 14;
        size_t n = cff_with_outline(bytes, 65536u, outline, at);
        CHECK(n);
        size_t hhea = tab_off(bytes, "hhea");
        put16(bytes + hhea + 4u, 1); put16(bytes + hhea + 6u, 0);
        font_t face; CHECK(font_init(&face, bytes, n) == FONT_OK);
        CHECK(font_set_px(&face, 15) == FONT_OK);
        font_metric metric; font_bitmap bitmap;
        CHECK(font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT);
        CHECK(font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT);
        CHECK(scratch.used == 0);
    }
    edit_arena_free(&scratch); free(bytes); free(outline);
    if (!failures) puts("P2-1 §4 GREEN: extreme coordinates and small-ascent bitmap rejected");
}

static void test_cff_errors(const unsigned char *ttf, size_t len)
{
    unsigned char bytes[4096]; cffseed_layout seed_layout;
    edit_arena scratch; CHECK(edit_arena_init(&scratch, 1u << 20) == 0);
    font_t face;
    for (unsigned variant = 0; variant < 3; variant++) {
        static const unsigned char no_end[] = {139,139,21};
        static const unsigned char bad_return[] = {11,139,139};
        size_t n = variant == 2 ? cffseed_build(bytes, sizeof bytes, 1, &seed_layout) :
            cff_with_outline(bytes, sizeof bytes, variant ? bad_return : no_end, 3);
        CHECK(n && font_init(&face, bytes, n) == FONT_OK);
        CHECK(font_set_px(&face, 15) == FONT_OK);
        font_metric metric; font_bitmap bitmap;
        CHECK(font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT);
        CHECK(font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT);
        CHECK(font_raster_glyph(&face, 'A', &scratch, &bitmap) == FONT_OK && bitmap.pixels);
        edit_arena_reset(&scratch);
    }
    static const unsigned char empty[] = {139,139,14};
    size_t n = cff_with_outline(bytes, sizeof bytes, empty, 3);
    CHECK(font_init(&face, bytes, n) == FONT_OK && font_set_px(&face, 15) == FONT_OK);
    font_bitmap bitmap;
    CHECK(font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_OK);
    CHECK(!bitmap.pixels && !bitmap.w && !bitmap.h);

    /* Malformed mapped glyph must fall through to a healthy covering face. */
    font_t primary; CHECK(font_init(&primary, ttf, len) == FONT_OK);
    CHECK(font_set_px(&primary, 15) == FONT_OK);
    font_family family; CHECK(font_family_load(&family, &primary, NULL, &scratch) == FONT_OK);
    edit_arena storage; CHECK(edit_arena_init(&storage, 4u << 20) == 0);
    font_cache cache; CHECK(font_cache_init(&cache, &family, &storage, 1, 32, 1024, 1u << 20) == FONT_OK);
    n = cffseed_build(bytes, sizeof bytes, 1, &seed_layout);
    CHECK(font_init(&face, bytes, n) == FONT_OK && font_set_px(&face, 15) == FONT_OK);
    family.faces[1] = primary; family.faces[0] = face; family.count = 2;
    uint32_t slot = RENDER_NO_SLOT;
    CHECK(font_cache_glyph(&cache, (const uint8_t *)"B", 1, 1, &slot) == FONT_OK);
    CHECK(slot != RENDER_NO_SLOT);
    family.count = 1;
    CHECK(font_cache_glyph(&cache, (const uint8_t *)"BB", 2, 2, &slot) == FONT_ERR_INIT);
    edit_arena_free(&storage); edit_arena_free(&scratch);
    if (!failures) puts("P2-1 §30 GREEN: CFF errors rejected, empty glyph preserved, fallback recovers");
}

static uint64_t s9_time(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void test_tt_ui_budget(const unsigned char *ttf, size_t len)
{
    font_t original; CHECK(font_init(&original, ttf, len) == FONT_OK);
    uint32_t glyph = (uint32_t)stbtt_FindGlyphIndex((const stbtt_fontinfo *)(const void *)original.info, 'A');
    size_t loca = tab_off(ttf, "loca"), glyf = tab_off(ttf, "glyf");
    int format = (int)be16(ttf + tab_off(ttf, "head") + 50u);
    uint32_t start = format ? be32(ttf + loca + 4u * glyph) : 2u * be16(ttf + loca + 2u * glyph);
    uint32_t end = format ? be32(ttf + loca + 4u * (glyph + 1u)) : 2u * be16(ttf + loca + 2u * (glyph + 1u));
    size_t outline_len = 14u + 1024u;
    CHECK(end > start && outline_len > end - start);
    size_t growth = outline_len - (end - start);
    unsigned char *bytes = malloc(len + growth); CHECK(bytes);
    if (!bytes) return;
    memcpy(bytes, ttf, glyf + start);
    unsigned char *outline = bytes + glyf + start;
    memset(outline, 0, outline_len);
    put16(outline, 1); put16(outline + 6u, 10); put16(outline + 8u, 10);
    put16(outline + 10u, 1023);
    memset(outline + 14u, 0x31, 1024u); /* on-curve, identical tiny coordinates */
    memcpy(outline + outline_len, ttf + glyf + end, len - glyf - end);
    for (uint32_t i = 0; i < be16(ttf + 4u); i++) {
        size_t rec = 12u + 16u * i;
        uint32_t offset = be32(ttf + rec + 8u);
        if (offset >= glyf + end) put32(bytes + rec + 8u, offset + (uint32_t)growth);
    }
    size_t rec = rec_of(bytes, 0, "glyf");
    put32(bytes + rec + 12u, (uint32_t)(tab_len(ttf, "glyf") + growth));
    loca = tab_off(bytes, "loca");
    for (uint32_t i = glyph + 1u; i <= original.num_glyphs; i++) {
        uint32_t offset = format ? be32(bytes + loca + 4u * i) : 2u * be16(bytes + loca + 2u * i);
        if (format) put32(bytes + loca + 4u * i, offset + (uint32_t)growth);
        else put16(bytes + loca + 2u * i, (offset + (uint32_t)growth) / 2u);
    }
    font_t face; CHECK(font_init(&face, bytes, len + growth) == FONT_OK);
    CHECK(font_set_px(&face, 15) == FONT_OK);
    edit_arena scratch; CHECK(edit_arena_init(&scratch, 1u << 20) == 0);
    font_bitmap bitmap; font_metric metric;
    CHECK(font_raster_glyph(&face, 'A', &scratch, &bitmap) == FONT_OK);
    edit_arena_reset(&scratch);
    CHECK(font_glyph_ui_metrics(&face, 'A', &metric) == FONT_ERR_INIT);
    CHECK(font_raster_glyph_ui(&face, 'A', &scratch, &bitmap) == FONT_ERR_INIT);
    CHECK(scratch.used == 0);
    edit_arena_free(&scratch); free(bytes);
}

static void test_ui_outline_budget(const unsigned char *ttf, size_t len)
{
    test_tt_ui_budget(ttf, len);
    unsigned char bytes[16384], outline[12000];
    size_t at = 0;
    outline[at++] = 139; outline[at++] = 139; outline[at++] = 21;
    /* Thousands of overlapping lines: tiny bitmap, excessive execution. */
    for (unsigned i = 0; i < 2000u; i++) {
        outline[at++] = i % 2u ? 138 : 140;
        outline[at++] = i % 2u ? 138 : 140;
        outline[at++] = 5;
    }
    outline[at++] = 14;
    size_t n = cff_with_outline(bytes, sizeof bytes, outline, at);
    font_t primary = {0}, hostile = {0};
    CHECK(n && font_init(&hostile, bytes, n) == FONT_OK && font_set_px(&hostile, 15) == FONT_OK);
    CHECK(font_init(&primary, ttf, len) == FONT_OK && font_set_px(&primary, 15) == FONT_OK);
    edit_arena storage, files;
    CHECK(edit_arena_init(&storage, 4u << 20) == 0);
    CHECK(edit_arena_init(&files, 1u << 20) == 0);
    font_family family; CHECK(font_family_load(&family, &primary, NULL, &files) == FONT_OK);
    font_cache cache; CHECK(font_cache_init(&cache, &family, &storage, 1, 32, 1024, 1u << 20) == FONT_OK);
    family.faces[0] = hostile;
    uint64_t rejected_max = 0, normal_max = 0;
    for (unsigned i = 0; i < 16u; i++) {
        uint32_t slot = RENDER_NO_SLOT;
        uint64_t start = s9_time();
        CHECK(font_cache_glyph(&cache, (const uint8_t *)"B", 1, 1, &slot) == FONT_ERR_INIT);
        uint64_t elapsed = s9_time() - start;
        if (elapsed > rejected_max) rejected_max = elapsed;
        CHECK(slot == RENDER_NO_SLOT && cache.scratch.used == 0);
        start = s9_time();
        font_bitmap bitmap;
        CHECK(font_raster_glyph_ui(&primary, 0xe9u, &cache.scratch, &bitmap) == FONT_OK);
        elapsed = s9_time() - start;
        if (elapsed > normal_max) normal_max = elapsed;
        edit_arena_reset(&cache.scratch);
    }
    printf("P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: %llu/%llu ns; TRACK only\n",
           (unsigned long long)rejected_max, (unsigned long long)normal_max);
    edit_arena_free(&files); edit_arena_free(&storage);
    if (!failures) puts("P2-1 §31 GREEN: tiny-bitmap high-outline workload refused on UI");
}

static void test_cff_synthetic(void)
{
    signal(SIGALRM, on_alarm);
    for (int variant = 0; variant < 3; variant += 2) {
        unsigned char buf[4096];
        cffseed_layout L;
        size_t n = cffseed_build(buf, sizeof buf, variant, &L);
        CHECK(n > 0);
        if (!n) continue;
        uint32_t rast = 0;
        CHECK(cff_exercise(buf, n, 0, &rast) == FONT_OK);
        CHECK(rast >= 2);                         /* A and B drew pixels */

        /* every byte of the CFF table replaced by 0x00, 0x01, 0x7f, 0x80, 0xff, 0x0a, 0x0e */
        static const unsigned char vals[] = { 0x00, 0x01, 0x7f, 0x80, 0xff, 0x0a, 0x0e, 0x1d };
        unsigned char *m = malloc(n);
        CHECK(m != NULL);
        for (size_t at = L.cff_off; m && at < L.cff_off + L.cff_len; at++) {
            for (size_t k = 0; k < sizeof vals; k++) {
                memcpy(m, buf, n);
                m[at] = vals[k];
                alarm(10);
                (void)cff_exercise(m, n, 0, NULL);
                alarm(0);
            }
        }
        /* truncation: CFF table cut at every length (directory record patched) */
        for (size_t cut = 0; m && cut < L.cff_len; cut++) {
            memcpy(m, buf, L.cff_off + cut);
            put32(m + L.rec_cff + 12, (uint32_t)cut);
            alarm(10);
            CHECK(cff_exercise(m, L.cff_off + cut, 0, NULL) == FONT_ERR_INIT);   /* every cut loses INDEX bytes */
            alarm(0);
        }
        /* 32-bit fields pushed far outside: charstrings offset, INDEX counts and offsets */
        static const uint32_t big[] = { 0x7fffffffu, 0xffffffffu, 0x40000000u, 0x20000000u, 0x10000u, 0x00ffffffu };
        for (size_t k = 0; m && k < sizeof big / sizeof big[0]; k++) {
            memcpy(m, buf, n);
            put32(m + L.cs_off_pos, big[k]);
            alarm(10);
            CHECK(cff_exercise(m, n, 0, NULL) == FONT_ERR_INIT);
            alarm(0);
            memcpy(m, buf, n);
            put32(m + L.priv_pos + 1, big[k]);          /* Subrs offset in Private */
            (void)cff_exercise(m, n, 0, NULL);
            memcpy(m, buf, n);
            put16(m + L.cs_idx, 0xffffu);                 /* count */
            m[L.cs_idx + 2] = (unsigned char)(1 + (k & 3));
            (void)cff_exercise(m, n, 0, NULL);
        }
        free(m);
    }
    /* the budget case: glyph B costs 20^9 interpreter steps without a cap */
    unsigned char buf[4096];
    cffseed_layout L;
    size_t n = cffseed_build(buf, sizeof buf, 1, &L);
    CHECK(n > 0);
    alarm(10);
    uint32_t rast = 0;
    CHECK(cff_exercise(buf, n, 0, &rast) == FONT_OK);
    alarm(0);
    CHECK(rast >= 1);                                 /* A still draws, B is refused or empty */
    printf("font_test: hostile CFF (byte flips, truncations, offsets, subr bomb) handled cleanly\n");
}

static const char *noto_path = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";

static void test_cff_noto(void)
{
    edit_arena fa;
    size_t flen = 0;
    if (access(noto_path, R_OK) != 0) { printf("font_test: %s missing, CFF system-font cases skipped\n", noto_path); return; }
    CHECK(edit_arena_init(&fa, (size_t)192u << 20) == 0);
    unsigned char *b = font_load_file(noto_path, &fa, &flen);
    CHECK(b != NULL);
    if (b) {
        uint32_t rast = 0;
        for (uint32_t idx = 0; idx < 3; idx++) {   /* real CFF CID fonts must still load */
            font_t f;
            CHECK(font_init_index(&f, b, flen, idx) == FONT_OK);
            CHECK(font_set_px(&f, 30) == FONT_OK);
        }
        CHECK(cff_exercise(b, flen, 2, &rast) == FONT_OK);
        CHECK(rast >= 1);
        uint64_t base = be32(b + 12 + 4u * 2u);
        size_t rec = rec_of(b, base, "CFF ");
        size_t off = be32(b + rec + 8), tl = be32(b + rec + 12);
        static const uint64_t cuts[] = { 0, 3, 4, 5, 8, 100, 1000, 100000, 1000003, 0 };
        for (size_t k = 0; k < sizeof cuts / sizeof cuts[0]; k++) {
            size_t cut = cuts[k] ? cuts[k] : tl / 2;
            if (k == 9) cut = tl - 1;
            if (cut >= tl) continue;
            unsigned char *c = malloc(off + cut);
            CHECK(c != NULL);
            if (!c) continue;
            memcpy(c, b, off + cut);
            put32(c + rec + 12, (uint32_t)cut);
            alarm(60);
            (void)cff_exercise(c, off + cut, 2, NULL);
            alarm(0);
            free(c);
        }
    }
    edit_arena_free(&fa);
    printf("font_test: Noto CJK CFF faces load; truncated copies rejected cleanly\n");
}

static void test_ttc(const unsigned char *ttf, size_t len)
{
    size_t hdr = 20;
    size_t tot = hdr + 2 * len;
    unsigned char *b = malloc(tot);
    CHECK(b != NULL);
    if (!b) return;
    memcpy(b, "ttcf", 4); put32(b + 4, 0x00010000u); put32(b + 8, 2);
    put32(b + 12, (uint32_t)hdr); put32(b + 16, (uint32_t)(hdr + len));
    for (int face = 0; face < 2; face++) {
        unsigned char *base = b + hdr + (size_t)face * len;
        memcpy(base, ttf, len);
        size_t n = be16(base + 4);
        for (size_t i = 0; i < n; i++) {       /* table offsets are file-absolute */
            unsigned char *rec = base + 12 + 16 * i;
            put32(rec + 8, be32(rec + 8) + (uint32_t)(hdr + (size_t)face * len));
        }
    }
    /* face 1: raise hhea ascent by 400 units */
    {
        unsigned char *base = b + hdr + len;
        size_t hh = be32(base + rec_of(base, 0, "hhea") + 8);
        put16(b + hh + 4, (uint32_t)be16(b + hh + 4) + 400u);
    }
    font_t f0, f1, fc;
    CHECK(init_exact(b, tot, 0, NULL) == FONT_OK);
    CHECK(init_exact(b, tot, 1, NULL) == FONT_OK);
    CHECK(init_exact(b, tot, 2, NULL) == FONT_ERR_INIT);       /* index past numFonts */
    CHECK(init_exact(b, tot, 0xFFFFFFFFu, NULL) == FONT_ERR_INIT);
    CHECK(init_exact(ttf, len, 1, NULL) == FONT_ERR_INIT);     /* plain TTF has only face 0 */
    CHECK(font_init(&fc, b, tot) == FONT_OK);
    CHECK(font_init_index(&f0, b, tot, 0) == FONT_OK);
    CHECK(font_init_index(&f1, b, tot, 1) == FONT_OK);
    CHECK(f0.face_index == 0 && f1.face_index == 1);
    CHECK(font_set_px(&f0, 20) == FONT_OK && font_set_px(&f1, 20) == FONT_OK && font_set_px(&fc, 20) == FONT_OK);
    CHECK(f1.cell.ascent > f0.cell.ascent);                    /* index is actually honoured */
    CHECK(fc.cell.ascent == f0.cell.ascent);
    /* hostile TTC headers */
    unsigned char *w = malloc(tot);
    memcpy(w, b, tot); put32(w + 8, 0xFFFFFFFFu);              /* numFonts huge */
    CHECK(init_exact(w, tot, 1, NULL) == FONT_ERR_INIT);
    memcpy(w, b, tot); put32(w + 16, (uint32_t)tot);           /* face offset at end */
    CHECK(init_exact(w, tot, 1, NULL) == FONT_ERR_INIT);
    memcpy(w, b, tot); put32(w + 16, 0xFFFFFFFFu);
    CHECK(init_exact(w, tot, 1, NULL) == FONT_ERR_INIT);
    memcpy(w, b, tot); put32(w + 4, 0x00030000u);              /* unknown TTC version */
    CHECK(init_exact(w, tot, 0, NULL) == FONT_ERR_INIT);
    CHECK(init_exact(b, 14, 0, NULL) == FONT_ERR_INIT);        /* header cut */
    free(w);
    free(b);
    printf("font_test: TTC face index honoured\n");
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

/* A bitmap fits, but stb's heap scanline for a wide glyph does not. */
static void test_raster_exhaustion(font_t *f, edit_arena *normal)
{
    CHECK(font_set_px(f, 256) == FONT_OK);
    font_metric m;
    CHECK(font_glyph_metrics(f, 'W', &m) == FONT_OK);
    CHECK(m.w > 64u && m.h > 0);
    size_t bitmap_bytes = (size_t)m.w * m.h;
    edit_arena tiny;
    CHECK(edit_arena_init(&tiny, bitmap_bytes + 768u) == 0);
    font_bitmap b;
    fprintf(stderr, "font_test: wide W scratch exhaustion\n");
    CHECK(font_raster_glyph(f, 'W', &tiny, &b) == FONT_ERR_NOMEM);
    CHECK(b.pixels == NULL);
    CHECK(tiny.used == 0);
    edit_arena_free(&tiny);

    edit_arena_mark_t mk = edit_arena_mark(normal);
    CHECK(font_raster_glyph(f, 'W', normal, &b) == FONT_OK);
    CHECK(b.pixels != NULL && b.w == m.w && b.h == m.h);
    int any = 0;
    for (size_t i = 0; b.pixels && i < bitmap_bytes; i++) any |= b.pixels[i] != 0;
    CHECK(any);
    edit_arena_reset_to_mark(normal, mk);
    printf("font_test: wide W exhaustion returns FONT_ERR_NOMEM; normal arena succeeds\n");
    CHECK(font_set_px(f, 30) == FONT_OK);
}

/* Exhaust bitmap, outline, flattening, edges, scanline and active-edge heap
 * storage, including composite/curved glyphs and non-aligned entry marks. */
static void test_raster_exhaustion_sweep(font_t *f)
{
    static const uint32_t cps[] = { 'W', '@', 0xE9u, 'i' };
    static const size_t scratch[] = {
        0, 1, 16, 32, 64, 128, 256, 512, 768, 1024, 2048, 4096,
        8192, 16384, 32768, 65536
    };
    const size_t capacity = 1u << 20;
    edit_arena a;
    CHECK(edit_arena_init(&a, capacity) == 0);
    if (!a.base) return;
    unsigned char *prefix = edit_arena_alloc(&a, 7, 1);
    memset(prefix, 0xA5, 7);
    edit_arena_mark_t entry = edit_arena_mark(&a);
    for (uint32_t px = 30; px <= 256; px += 226) {
        CHECK(font_set_px(f, px) == FONT_OK);
        for (size_t c = 0; c < sizeof cps / sizeof cps[0]; c++) {
            font_metric m;
            CHECK(font_glyph_metrics(f, cps[c], &m) == FONT_OK);
            size_t bitmap_bytes = (size_t)m.w * m.h;
            int saw_failure = 0, saw_success = 0;
            font_bitmap b;
            a.size = entry + bitmap_bytes - 1u;
            CHECK(font_raster_glyph(f, cps[c], &a, &b) == FONT_ERR_NOMEM);
            CHECK(b.pixels == NULL && a.used == entry);
            for (size_t s = 0; s < sizeof scratch / sizeof scratch[0]; s++) {
                a.size = entry + bitmap_bytes + scratch[s];
                int r = font_raster_glyph(f, cps[c], &a, &b);
                CHECK(r == FONT_ERR_NOMEM || r == FONT_OK);
                if (r == FONT_ERR_NOMEM) {
                    saw_failure = 1;
                    CHECK(b.pixels == NULL && a.used == entry);
                } else {
                    saw_success = 1;
                    CHECK(b.pixels != NULL && a.used == entry + bitmap_bytes);
                }
                CHECK(b.w == m.w && b.h == m.h);
                for (size_t i = 0; i < entry; i++) CHECK(prefix[i] == 0xA5);
                edit_arena_reset_to_mark(&a, entry);
            }
            CHECK(saw_failure && saw_success);
        }
    }
    a.size = capacity;  /* restore mmap extent before munmap */
    edit_arena_free(&a);
    CHECK(font_set_px(f, 30) == FONT_OK);
    printf("font_test: bitmap/scratch exhaustion sweep and arena rollback passed\n");
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
    int bad = 0;
    for (int i = 0; i < 600; i++) {
        int r = font_raster_glyph(f, cps[i % 6], a, &b);
        bad |= r != FONT_OK;
        edit_arena_reset_to_mark(a, mk);
    }
    size_t n = edit_malloc_guard_end();
    printf("font_test: mallocs during 600 rasterisations: %zu\n", n);
    CHECK(n == 0);
    CHECK(!bad);
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

/* P4.11b review §1: a valid replacement cmap keeps all ASCII metrics. */
static void test_atlas_identity(const unsigned char *ttf, size_t len)
{
    size_t extra = 12u + 16u + 95u * 12u;
    unsigned char *copy = malloc(len); CHECK(copy);
    if (!copy) return;
    memcpy(copy, ttf, len);
    size_t rec = rec_of(copy, 0, "cmap");
    unsigned char *cm = copy + be32(copy + rec + 8);
    CHECK(extra <= be32(copy + rec + 12)); memset(cm, 0, be32(copy + rec + 12));
    put16(cm + 2, 1); put16(cm + 4, 3); put16(cm + 6, 10); put32(cm + 8, 12);
    put16(cm + 12, 12); put32(cm + 16, (uint32_t)(extra - 12u)); put32(cm + 24, 95);
    font_t original, modified;
    CHECK(font_init(&original, ttf, len) == FONT_OK);
    for (uint32_t cp = 32; cp < 127; cp++) {
        uint32_t mapped = cp == 'A' ? 'W' : cp == 'W' ? 'A' : cp;
        uint32_t gid = (uint32_t)stbtt_FindGlyphIndex((const stbtt_fontinfo *)(const void *)original.info, (int)mapped);
        unsigned char *g = cm + 28u + (cp - 32u) * 12u;
        put32(g, cp); put32(g + 4, cp); put32(g + 8, gid);
    }
    CHECK(font_init(&modified, copy, len) == FONT_OK);
    CHECK(font_set_px(&original, 30) == FONT_OK && font_set_px(&modified, 30) == FONT_OK);
    for (uint32_t cp = 32; cp < 127; cp++) {
        font_metric a, b;
        CHECK(font_glyph_metrics(&original, cp, &a) == FONT_OK);
        CHECK(font_glyph_metrics(&modified, cp, &b) == FONT_OK);
        CHECK(a.advance == b.advance && a.bearing_x == b.bearing_x && a.bearing_y == b.bearing_y && a.w == b.w && a.h == b.h);
    }
    CHECK(modified.atlas == NULL);
    edit_arena arena; CHECK(edit_arena_init(&arena, 1u << 20) == 0);
    for (uint32_t px = 15; px <= 30; px += 15) {
        CHECK(font_set_px(&original, px) == FONT_OK && original.atlas);
        uint8_t cell[1920];
        const font_ascii_atlas *bake = original.atlas;
        for (uint32_t cp = 32; cp < 127; cp++) {
            font_bitmap b; edit_arena_reset(&arena); memset(cell, 0, sizeof cell);
            CHECK(font_raster_glyph(&original, cp, &arena, &b) == FONT_OK);
            CHECK(font_place_in_cell(&original, &b, cell, bake->cell.cell_w, bake->cell.cell_h) == FONT_OK);
            for (uint32_t y = 0; y < bake->cell.cell_h; y++)
                CHECK(memcmp(cell + (size_t)y * bake->cell.cell_w,
                      bake->pixels + ((size_t)y * 95u + cp - 32u) * bake->cell.cell_w, bake->cell.cell_w) == 0);
        }
    }
    edit_arena_free(&arena); free(copy);
    if (!failures) puts("review §1 GREEN: metrics-preserving cmap rejected; every baked ASCII pixel matches live outlines");
}

static ssize_t short_read(void *cookie, char *buf, size_t n)
{
    (void)cookie; (void)buf; (void)n; return 0;
}
static int short_seek(void *cookie, off64_t *off, int whence)
{
    off64_t *position = cookie;
    if (whence == SEEK_END) *off += 4096;
    else if (whence == SEEK_CUR) *off += *position;
    *position = *off; return 0;
}
static void test_file_rollback(void)
{
    edit_arena arena; CHECK(edit_arena_init(&arena, 8192) == 0);
    CHECK(edit_arena_alloc(&arena, 3, 1) != NULL); size_t mark = arena.used;
    for (unsigned i = 0; i < 3; i++) {
        off64_t pos = 0; cookie_io_functions_t ops = {short_read,NULL,short_seek,NULL};
        FILE *fp = fopencookie(&pos, "rb", ops); CHECK(fp);
        size_t len = 777;
        CHECK(font_load_stream(fp, &arena, &len) == NULL);
        CHECK(arena.used == mark && len == 0);
        fclose(fp);
    }
    size_t len = 888;
    CHECK(font_load_file("/nonexistent/font-review-fixture", &arena, &len) == NULL && len == 0);
    CHECK(font_load_file("vendor/DejaVuSansMono.ttf", &arena, NULL) == NULL);
    edit_arena_free(&arena);
    if (!failures) puts("review §14 GREEN: short reads roll back arena and clear lengths; NULL length rejected");
}

static void on_msg(const work_msg *m, void *ud)
{
    if (m->kind == FONT_FALLBACK_MSG_KIND) (*(int *)ud)++;
}

typedef struct cancel_probe {
    font_fallback fb;
    _Atomic int started, release;
} cancel_probe;
static void cancelled_probe_job(work_ctx *ctx)
{
    cancel_probe *p = ctx->arg; atomic_store(&p->started, 1);
    while (!atomic_load(&p->release)) usleep(1000);
    font_fallback_job(ctx);
}
static void test_cancelled_discovery(void)
{
    cancel_probe p = {0}; work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    work_handle h = work_submit(pool, (work_job){cancelled_probe_job, &p, 7, WORK_BULK}); CHECK(h.epoch);
    for (unsigned i = 0; i < 2000 && !atomic_load(&p.started); i++) usleep(1000);
    CHECK(atomic_load(&p.started)); work_cancel(pool, h); atomic_store(&p.release, 1);
    for (unsigned i = 0; i < 2000 && !work_handle_finished(pool, h); i++) usleep(1000);
    CHECK(work_handle_finished(pool, h)); CHECK(atomic_load(&p.fb.done) == 0);
    work_pool_shutdown(pool); free(pool);
    /* Also cancel after discovery has physically finished but before its
     * queued mailbox result is adopted. This catches the original done
     * side channel at the publication boundary, rather than only at entry. */
    font_fallback queued = {0};
    pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    h = work_submit(pool, (work_job){font_fallback_job, &queued, 17, WORK_BULK}); CHECK(h.epoch);
    for (unsigned i = 0; i < 3000 && !work_handle_finished(pool, h); i++) usleep(1000);
    CHECK(work_handle_finished(pool, h) && work_mailbox_pending(pool));
    CHECK(atomic_load(&queued.done) == 0);
    work_cancel(pool, h);
    int got = 0; (void)work_mailbox_drain(pool, on_msg, &got);
    CHECK(got == 0 && !work_mailbox_pending(pool));
    CHECK(atomic_load(&queued.done) == 0 && queued.cjk[0] == 0 && queued.emoji[0] == 0);
    work_pool_shutdown(pool); free(pool);
    if (!failures) puts("review §7 GREEN: cancelled discovery has no adoptable done side channel");
}
typedef struct pressure_job {
    font_fallback fb;
    _Atomic int filled;
} pressure_job;
static void discovery_pressure(work_ctx *ctx)
{
    pressure_job *p = ctx->arg; work_msg msg = {0}; msg.kind = 1;
    for (uint32_t i = 0; i < WORK_MAILBOX_CAP; i++) CHECK(work_publish(ctx, &msg));
    atomic_store(&p->filled, 1);
    font_fallback_job(ctx); /* fb is the first member */
}
static void test_discovery_pressure(void)
{
    pressure_job p = {0}; work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    work_handle h = work_submit(pool, (work_job){discovery_pressure, &p, 8, WORK_BULK}); CHECK(h.epoch);
    for (unsigned i = 0; i < 2000 && !atomic_load(&p.filled); i++) usleep(1000);
    CHECK(atomic_load(&p.filled));
    /* Leave discovery enough time to reach publication, while the mailbox is full. */
    usleep(100000);
    int got = 0;
    for (unsigned i = 0; i < 2000 && !got; i++) { (void)work_mailbox_drain(pool, on_msg, &got); usleep(1000); }
    CHECK(got == 1);
    work_pool_shutdown(pool); free(pool);
    if (!failures) puts("review §8 GREEN: full mailbox drains, terminal discovery completion arrives");
}

static void test_discovery_cancellation(void)
{
    font_fallback fb = {0}; fb.discovery_program = "/proc/self/exe";
    CHECK(setenv("FONT_REVIEW_CHILD", "1", 1) == 0);
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    work_handle h = work_submit(pool, (work_job){font_fallback_job, &fb, 9, WORK_BULK}); CHECK(h.epoch);
    uint32_t pid = 0;
    for (unsigned i = 0; i < 300 && !(pid = atomic_load(&fb.child_pid)); i++) usleep(1000);
    CHECK(pid != 0); work_cancel(pool, h);
    for (unsigned i = 0; i < 1000 && !work_handle_finished(pool, h); i++) usleep(1000);
    CHECK(work_handle_finished(pool, h)); CHECK(atomic_load(&fb.done) == 0);
    if (pid) CHECK(kill((pid_t)pid, 0) == -1 && errno == ESRCH);
    CHECK(atomic_load(&fb.cancellation_polls) > 0);
    int got = 0; (void)work_mailbox_drain(pool, on_msg, &got); CHECK(got == 0);
    work_pool_shutdown(pool); free(pool); CHECK(unsetenv("FONT_REVIEW_CHILD") == 0);
    if (!failures) puts("review work-scan §9 GREEN: production discovery cancels and reaps an unbounded helper; no adoption");
}

static void test_discovery_inherited_stdout(void)
{
    font_fallback fb = {0}; fb.discovery_program = "/proc/self/exe";
    CHECK(setenv("FONT_REVIEW_CHILD", "inherited", 1) == 0);
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    work_handle handle = work_submit(pool, (work_job){font_fallback_job, &fb, 19, WORK_BULK});
    CHECK(handle.epoch);
    for (unsigned i = 0; i < 300u && !atomic_load(&fb.child_pid); i++) usleep(1000);
    CHECK(atomic_load(&fb.child_pid));
    usleep(100000); /* direct helper exits; descendant keeps the pipe open */
    work_cancel(pool, handle);
    for (unsigned i = 0; i < 250u && !work_handle_finished(pool, handle); i++) usleep(1000);
    CHECK(work_handle_finished(pool, handle));
    CHECK(!atomic_load(&fb.done));
    work_pool_shutdown(pool); free(pool);
    CHECK(unsetenv("FONT_REVIEW_CHILD") == 0);
    if (!failures) puts("P2-1 §32 GREEN: cancellation finishes after helper exits with inherited stdout");
}

static void adopt_runtime(const work_msg *msg, void *arg)
{
    font_runtime *owner = arg;
    if (font_runtime_event(owner, msg)) CHECK(font_runtime_event(owner, msg) == 0);
}

static void test_runtime_owner(void)
{
    edit_arena files, storage;
    CHECK(edit_arena_init(&files, 1u << 20) == 0);
    CHECK(edit_arena_init(&storage, 4u << 20) == 0);
    font_runtime owner;
    font_runtime_config config = {"vendor/DejaVuSansMono.ttf", NULL, 15, 1, 32, 1024, 1u << 20};
    uint32_t pages = 0; size_t glyphs = 0, pixels = 0;
    CHECK(font_runtime_limits(&config, &pages, &glyphs, &pixels) == FONT_OK);
    CHECK(pages == 2 && glyphs == 127 && pixels > 1024u * 1024u);
    CHECK(font_runtime_init(&owner, &config, &files, &storage) == FONT_OK);
    uint32_t slot = RENDER_NO_SLOT;
    CHECK(font_runtime_glyph(&owner, (const uint8_t *)"\xc3\xa9", 2, 1, &slot) == FONT_MORE);
    CHECK(slot == RENDER_NO_SLOT && files.used == 0 && storage.used == 0);
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    CHECK(pool && work_pool_init(pool, 1, 0) == 0);
    work_handle handle = work_submit(pool, (work_job){font_runtime_prepare_job, &owner, 29, WORK_BULK});
    CHECK(handle.epoch);
    for (unsigned i = 0; i < 1000u && !work_handle_finished(pool, handle); i++) usleep(1000);
    CHECK(work_handle_finished(pool, handle) && !owner.adopted);
    CHECK(font_runtime_glyph(&owner, (const uint8_t *)"\xc3\xa9", 2, 1, &slot) == FONT_MORE);
    (void)work_mailbox_receive(pool, handle, 29, adopt_runtime, &owner);
    CHECK(owner.adopted && owner.result == FONT_OK && !pthread_equal(owner.worker, pthread_self()));
    render_cell cells[4]; uint64_t dirty = 0, row_byte = 0; uint32_t row_used = 0;
    render_grid grid;
    CHECK(render_grid_init(&grid, (render_dims){4,1,owner.cache.cell.cell_w,owner.cache.cell.cell_h},
                           cells, 4, &dirty, 1) == RENDER_OK);
    CHECK(font_runtime_bind(&owner, &grid) == FONT_OK);
    layout_config cfg = {0}; cfg.fg = 0xffffff; cfg.glyph = font_runtime_glyph; cfg.glyph_ctx = &owner;
    layout view;
    CHECK(layout_init(&view, &grid, &cfg, &row_byte, &row_used) == LAYOUT_DONE);
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    static const uint8_t text[] = "\xc3\xa9 e\xcc\x81";
    CHECK(tree && piece_init_copy(tree, text, sizeof text - 1u) == PIECE_OK);
    CHECK(render_frame_begin(&grid, 1) == RENDER_OK);
    CHECK(layout_begin(&view, tree, (layout_viewport){0,0,0,1}) == LAYOUT_DONE);
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    int result; unsigned slices = 0;
    do { result = layout_run(&view); CHECK(++slices < 32u); } while (result == LAYOUT_MORE && slices < 32u);
    if (edit_malloc_guard_active()) CHECK(edit_malloc_guard_end() == 0);
    CHECK(result == LAYOUT_DONE && !layout_approximate(&view));
    CHECK(cells[0].glyph_index != '?' && cells[2].glyph_index != '?');
    CHECK(cells[0].atlas_slot >= 95u && cells[2].atlas_slot >= 95u);
    CHECK(render_grid_validate(&grid) == RENDER_OK);
    /* Independently rasterize covered U+00E9 and compare its atlas bytes. */
    uint8_t expected[1920] = {0}; font_bitmap bitmap;
    CHECK(font_raster_glyph(&owner.family.faces[0], 0xe9, &owner.cache.scratch, &bitmap) == FONT_OK);
    CHECK(font_place_in_cell(&owner.family.faces[0], &bitmap, expected, grid.dims.cell_w, grid.dims.cell_h) == FONT_OK);
    render_glyph glyph = grid.glyphs[cells[0].atlas_slot];
    for (uint32_t y = 0; y < glyph.h; y++) CHECK(memcmp(expected + (size_t)y * glyph.w,
        grid.pages[glyph.page].pixels + (size_t)(glyph.y + y) * grid.pages[glyph.page].stride + glyph.x, glyph.w) == 0);
    piece_destroy(tree); work_pool_shutdown(pool); free(pool);
    edit_arena_free(&storage); edit_arena_free(&files);
    if (!failures) puts("P2-1 §29 GREEN: worker-prepared owner adopts through mailbox; Unicode grid/pixels match");
}

typedef struct fallback_adoption { font_fallback *fb; int got; } fallback_adoption;
static void adopt_fallback(const work_msg *msg, void *arg)
{
    fallback_adoption *a = arg;
    if (font_fallback_event(a->fb, msg)) {
        a->got++;
        CHECK(font_fallback_event(a->fb, msg) == 0); /* no repeated adoption */
    }
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
    fallback_adoption adoption = {&fb, 0};
    for (int i = 0; i < 300 && !adoption.got; i++) {   /* up to 30 s */
        if (poll(&pfd, 1, 100) > 0) work_mailbox_drain(&pool, adopt_fallback, &adoption);
    }
    CHECK(adoption.got == 1);
    CHECK(atomic_load(&fb.done) == 1u);
    CHECK(!pthread_equal(fb.worker, me));
    printf("font_test: fallback fontconfig=%d cjk='%s' emoji='%s'\n",
           fb.have_fontconfig, fb.cjk, fb.emoji);
    work_pool_shutdown(&pool);

    /* #10: a second run must not be observable as done until it finishes. */
    font_fallback_reset(&fb);
    CHECK(atomic_load(&fb.done) == 0u);
    CHECK(fb.cjk[0] == 0 && fb.emoji[0] == 0 && fb.cjk_index == 0);
    CHECK(work_pool_init(&pool, 1, 0) == 0);
    CHECK(work_submit(&pool, j).epoch != 0);
    adoption.got = 0;
    pfd.fd = work_pool_eventfd(&pool);
    for (int i = 0; i < 300 && !adoption.got; i++) {
        if (poll(&pfd, 1, 100) > 0) work_mailbox_drain(&pool, adopt_fallback, &adoption);
    }
    CHECK(adoption.got == 1 && atomic_load(&fb.done) == 1u);
    work_pool_shutdown(&pool);
    if (fb.cjk[0]) {                      /* FC_INDEX carried: a TTC must open at that face */
        edit_arena fa;
        size_t flen = 0;
        CHECK(edit_arena_init(&fa, (size_t)128u << 20) == 0);
        unsigned char *fd = font_load_file(fb.cjk, &fa, &flen);
        font_t cf;
        CHECK(fd != NULL);
        if (fd) {
            CHECK(font_init_index(&cf, fd, flen, fb.cjk_index) == FONT_OK);
            CHECK(cf.face_index == fb.cjk_index);
            CHECK(font_set_px(&cf, 30) == FONT_OK);
            font_metric m;
            CHECK(font_glyph_metrics(&cf, 0x4E2Du, &m) == FONT_OK);
        }
        edit_arena_free(&fa);
    }

    static font_fallback none;
    font_fallback_discover(&none, "libnope.so.9");
    CHECK(atomic_load(&none.done) == 1u && none.have_fontconfig == 0);
    CHECK(none.cjk[0] == 0 && none.emoji[0] == 0);
}

int main(int argc, char **argv)
{
    /* fc-match-compatible test child deliberately never returns. */
    if (argc > 1 && strcmp(argv[1], "-f") == 0 && getenv("FONT_REVIEW_CHILD")) {
        if (strcmp(getenv("FONT_REVIEW_CHILD"), "inherited") == 0) {
            pid_t descendant = fork();
            if (descendant < 0) _exit(2);
            if (descendant == 0) { usleep(3000000); _exit(0); }
            _exit(0);
        }
        for (;;) atomic_signal_fence(memory_order_seq_cst);
    }
    size_t len = 0;
    unsigned char *ttf = load_ttf(&len);
    if (!ttf) { fprintf(stderr, "font_test: cannot read vendor/DejaVuSansMono.ttf\n"); return 1; }
    font_t f;
    CHECK(font_init(&f, ttf, len) == FONT_OK);
    CHECK(font_set_px(&f, 30) == FONT_OK);
    edit_arena a;
    CHECK(edit_arena_init(&a, 1u << 20) == 0);

    if (getenv("FONT_REVIEW_POLLING")) { test_discovery_cancellation(); return failures ? 1 : 0; }
    if (getenv("FONT_REVIEW_DISCOVERY")) {
        if (strcmp(getenv("FONT_REVIEW_DISCOVERY"), "7") == 0) test_cancelled_discovery();
        else test_discovery_pressure();
        return failures ? 1 : 0;
    }
    if (getenv("FONT_REVIEW_FILE")) { test_file_rollback(); return failures ? 1 : 0; }
    if (getenv("FONT_REVIEW_S9")) {
        if (strcmp(getenv("FONT_REVIEW_S9"), "4") == 0) test_cff_coordinates();
        else if (strcmp(getenv("FONT_REVIEW_S9"), "30") == 0) test_cff_errors(ttf, len);
        else if (strcmp(getenv("FONT_REVIEW_S9"), "31") == 0) test_ui_outline_budget(ttf, len);
        else if (strcmp(getenv("FONT_REVIEW_S9"), "32") == 0) test_discovery_inherited_stdout();
        else if (strcmp(getenv("FONT_REVIEW_S9"), "29") == 0) test_runtime_owner();
        else test_signed_offsets(ttf, len);
        edit_arena_free(&a); free(ttf);
        return failures ? 1 : 0;
    }
    test_signed_offsets(ttf, len);
    test_file_rollback();
    test_atlas_identity(ttf, len);
    if (getenv("FONT_REVIEW_IDENTITY")) return failures ? 1 : 0;
    test_bake_consistency(&f);
    test_raster(&f, &a);
    test_raster_exhaustion(&f, &a);
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    test_raster_exhaustion_sweep(&f);
    if (edit_malloc_guard_active()) {
        size_t n = edit_malloc_guard_end();
        printf("font_test: mallocs during exhaustion sweep: %zu\n", n);
        CHECK(n == 0);
    }
    test_shelf();
    test_no_malloc(&f, &a);
    test_atlas_px(ttf, len);
    test_font_bounds(ttf, len);
    test_ttc(ttf, len);
    test_cff_coordinates();
    test_cff_errors(ttf, len);
    test_ui_outline_budget(ttf, len);
    test_cff_synthetic();
    test_cff_noto();
    test_runtime_owner();
    test_discovery_inherited_stdout();
    test_discovery_cancellation();
    test_cancelled_discovery();
    test_discovery_pressure();
    test_fallback();

    edit_arena_free(&a);
    free(ttf);
    if (failures) { fprintf(stderr, "font_test: %d failure(s)\n", failures); return 1; }
    printf("font_test: all passed\n");
    return 0;
}
