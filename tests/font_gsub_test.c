/* tests/font_gsub_test.c - edit-v16: bounded GSUB ligature subset for emoji
 * clusters. Fixture: tests/fixtures/fonts/NotoEmoji-wght.ttf (OFL, see
 * PROVENANCE.md). Expected glyph ids and lookup counts are (M): fontTools
 * 4.62.1 dump of that file, 2026-10-16 (docs/decisions/edit-v16.md). */
#include "base/base.h"
#include "font/font.h"
#include "font/gsub.h"
#include "utf8/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

#define FIXTURE "tests/fixtures/fonts/NotoEmoji-wght.ttf"
/* Optional system CJK face for the byte-identity check; absent -> skipped. */
#define CJK_FACE "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"

/* (M) glyph ids in NotoEmoji-wght.ttf (fontTools getGlyphOrder). */
enum {
    GID_WOMAN = 606, GID_TECHNOLOGIST = 1455, GID_THUMBS = 578,
    GID_AREGION = 207, GID_AUSTRALIA = 1626, GID_UNKNOWN_FLAG = 1878,
    GID_KISS = 644, GID_FAMILY_MWGB = 1486, GID_ENGLAND = 1688
};

static unsigned char *load(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *b = n > 0 ? malloc((size_t)n) : NULL; /* test code */
    if (!b || fread(b, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); free(b); return NULL; }
    fclose(fp);
    *len = (size_t)n;
    return b;
}
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static size_t cluster_of(font_t *f, const char *s, uint16_t *out)
{
    size_t n = 0;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    int rc = font_gsub_cluster(f, (const uint8_t *)s, strlen(s), out, &n);
    if (edit_malloc_guard_active()) CHECK(edit_malloc_guard_end() == 0); /* law 2 */
    return rc == FONT_OK ? n : 0;
}

/* Parse: lookup census by type (M, fontTools): LookupList 6 entries, types
 * 4,2,1,6,4,5; DFLT default LangSys enables ccmp = lookups 0,3,4,5. The
 * supported, applied subset is 0 (type 4), 3 (type 6 fmt 3), 4 (type 4). */
static void test_parse(font_t *f)
{
    CHECK(f->gsub.table && f->gsub.len > 10u && f->gsub.state == FONT_GSUB_UNPARSED);
    font_gsub g; font_gsub_stats st;
    CHECK(font_gsub_parse(&g, f->gsub.table, f->gsub.len, &st) == FONT_OK);
    CHECK(g.state == FONT_GSUB_READY);
    CHECK(st.lookups == 6u);
    CHECK(st.by_type[1] == 1u && st.by_type[2] == 1u && st.by_type[4] == 2u &&
          st.by_type[5] == 1u && st.by_type[6] == 1u);
    CHECK(st.enabled == 4u);
    CHECK(st.enabled_by_type[4] == 2u && st.enabled_by_type[5] == 1u && st.enabled_by_type[6] == 1u);
    CHECK(g.count == 3u && g.lookups[0] == 0u && g.lookups[1] == 3u && g.lookups[2] == 4u);
    CHECK(st.reads <= st.budget);
    printf("font_gsub_test: fixture GSUB %u bytes, %u lookups (t1=%u t2=%u t4=%u t5=%u t6=%u), "
           "enabled %u, applied %u, validator reads %llu of budget %llu (M)\n",
           f->gsub.len, st.lookups, st.by_type[1], st.by_type[2], st.by_type[4], st.by_type[5],
           st.by_type[6], st.enabled, g.count, (unsigned long long)st.reads,
           (unsigned long long)st.budget);
}

static void test_clusters(font_t *f)
{
    uint16_t out[FONT_GSUB_MAX_GLYPHS];
    static const struct { const char *name, *s; uint16_t gid; } one[] = {
        {"woman technologist", "👩‍💻", GID_TECHNOLOGIST},
        {"thumbs up medium", "👍🏽", GID_THUMBS},
        {"flag AU", "🇦🇺", GID_AUSTRALIA},
        {"kiss two tones (10 scalars)", "👩🏻‍❤️‍💋‍👨🏼", GID_KISS},
        {"family MWGB", "👨‍👩‍👧‍👦", GID_FAMILY_MWGB},
        {"flag England (tags)", "🏴󠁧󠁢󠁥󠁮󠁧󠁿", GID_ENGLAND},
        {"non-RGI tail stops at ZWJ", "👩‍💻‍😀", GID_TECHNOLOGIST},
    };
    for (size_t i = 0; i < sizeof one / sizeof one[0]; i++) {
        size_t n = cluster_of(f, one[i].s, out);
        utf8_step first = utf8_decode((const uint8_t *)one[i].s, strlen(one[i].s));
        uint32_t alone = font_glyph_id(f, first.cp);
        printf("font_gsub_test: %s -> n=%zu gid=%u (first scalar alone %u)\n",
               one[i].name, n, n ? out[0] : 0u, alone);
        CHECK(n == 1u && out[0] == one[i].gid);
        /* (M) Monochrome Noto Emoji ligates 1F44D 1F3FD back to thumbsUp:
         * the face has no skin-tone artwork, so only the tone is dropped. */
        if (one[i].gid == GID_THUMBS) CHECK(alone == GID_THUMBS);
        else CHECK(out[0] != alone);
    }
    CHECK(font_glyph_id(f, 0x1f469u) == GID_WOMAN && font_glyph_id(f, 0x1f1e6u) == GID_AREGION);
    /* No substitution: callers keep the r8e projection. */
    char longest[4 + 3 * FONT_GSUB_MAX_GLYPHS + 1] = "👩"; /* + 10 x FE0F = 11 scalars */
    for (size_t i = 0; i < FONT_GSUB_MAX_GLYPHS; i++) memcpy(longest + 4 + 3 * i, "\xef\xb8\x8f", 4);
    const char *none[] = {"😀︎", "😀️", "🇦", "é", "a", "👩́", "😀🏻", longest};
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) CHECK(cluster_of(f, none[i], out) == 0u);
    CHECK(f->gsub.state == FONT_GSUB_READY);
}

/* The fixture's type 6 (format 3) lookup deletes a regional indicator that
 * follows an unknown flag (nested type 2, empty sequence). */
static void test_chain(font_t *f)
{
    font_gsub g;
    CHECK(font_gsub_parse(&g, f->gsub.table, f->gsub.len, NULL) == FONT_OK);
    uint16_t glyphs[FONT_GSUB_MAX_GLYPHS] = {GID_UNKNOWN_FLAG, GID_AREGION};
    size_t n = 2; uint32_t ops = 0;
    CHECK(font_gsub_apply(&g, glyphs, &n, FONT_GSUB_MAX_GLYPHS, NULL, 0, &ops) == 1);
    CHECK(n == 1u && glyphs[0] == GID_UNKNOWN_FLAG);
    /* Work census for the apply budget: ten copies of the glyph with the
     * fixture's largest ligature sets (woman: 392 ligatures, (M)). */
    uint32_t worst = 0;
    for (size_t k = 0; k < 3; k++) { /* woman x10; skinTone1 x10; woman/ZWJ alternating */
        for (size_t i = 0; i < FONT_GSUB_MAX_GLYPHS; i++)
            glyphs[i] = k == 0 ? GID_WOMAN : k == 1 ? 492 : (i & 1u ? 18 : GID_WOMAN);
        n = FONT_GSUB_MAX_GLYPHS; ops = 0;
        int rc = font_gsub_apply(&g, glyphs, &n, FONT_GSUB_MAX_GLYPHS, NULL, 0, &ops);
        CHECK(rc >= 0);
        if (ops > worst) worst = ops;
    }
    printf("font_gsub_test: worst measured apply work %u ops over 10-glyph heavy arrays (M)\n", worst);
}

static int parse_copy(const uint8_t *t, size_t len)
{
    uint8_t *copy = malloc(len ? len : 1u); /* exact-size block: ASan sees overreads */
    if (!copy) return FONT_ERR_NOMEM;
    memcpy(copy, t, len);
    font_gsub g;
    int rc = font_gsub_parse(&g, copy, len, NULL);
    CHECK(rc == FONT_OK ? g.state == FONT_GSUB_READY : g.state == FONT_GSUB_NONE);
    if (rc == FONT_OK) { /* accepted prefix: applying must stay in bounds */
        uint16_t glyphs[FONT_GSUB_MAX_GLYPHS] = {GID_WOMAN, 18, 691, GID_AREGION, 227};
        size_t n = 5;
        (void)font_gsub_apply(&g, glyphs, &n, FONT_GSUB_MAX_GLYPHS, NULL, 0, NULL);
    }
    free(copy);
    return rc;
}

static void test_malformed(font_t *f, const unsigned char *ttf, size_t ttf_len)
{
    const uint8_t *t = f->gsub.table; size_t len = f->gsub.len;
    for (size_t l = 0; l <= 10u; l++) CHECK(parse_copy(t, l) == FONT_ERR_INIT);
    CHECK(parse_copy(t, len / 2u) == FONT_ERR_INIT);
    size_t rejected = 0, strides = 97; /* (E) spread of cut points, not a bound */
    for (size_t i = 1; i <= strides; i++) rejected += parse_copy(t, len * i / (strides + 1u)) == FONT_ERR_INIT;
    printf("font_gsub_test: %zu of %zu truncated prefixes rejected\n", rejected, strides);
    CHECK(rejected == strides);

    uint8_t *g = malloc(len);
    CHECK(g != NULL);
    if (!g) return;
    uint32_t lookup_list = be16(t + 8), lookup0 = lookup_list + be16(t + lookup_list + 2u);
    uint32_t sub0 = lookup0 + be16(t + lookup0 + 6u);
    struct { uint32_t at, value; const char *what; } garble[] = {
        {0, 2, "major version 2"},
        {8, 0xffffu, "LookupList offset past the end"},
        {lookup_list, 0xffffu, "LookupList count past the end"},
        {lookup0 + 6u, 0xffffu, "lookup 0 subtable offset past the end"},
        {sub0 + 2u, 0xfff0u, "ligature coverage offset past the end"},
        {sub0 + 4u, 0xffffu, "ligature set count past the end"},
        {sub0 + 0u, 9u, "ligature subtable format 9"},
    };
    for (size_t i = 0; i < sizeof garble / sizeof garble[0]; i++) {
        memcpy(g, t, len);
        put16(g + garble[i].at, garble[i].value);
        font_gsub parsed;
        int rc = font_gsub_parse(&parsed, g, len, NULL);
        if (rc != FONT_ERR_INIT) fprintf(stderr, "garble '%s' accepted (%d)\n", garble[i].what, rc);
        CHECK(rc == FONT_ERR_INIT && parsed.state == FONT_GSUB_NONE);
    }
    free(g);

    /* Whole face with a garbled GSUB: init succeeds (GSUB is lazy), the
     * cluster path disables GSUB for the face and reports no substitution. */
    unsigned char *copy = malloc(ttf_len);
    CHECK(copy != NULL);
    if (!copy) return;
    memcpy(copy, ttf, ttf_len);
    size_t at = (size_t)(t - ttf);
    put16(copy + at + 8u, 0xffffu);
    font_t bad;
    CHECK(font_init(&bad, copy, ttf_len) == FONT_OK);
    uint16_t out[FONT_GSUB_MAX_GLYPHS];
    CHECK(cluster_of(&bad, "👩‍💻", out) == 0u);
    CHECK(bad.gsub.state == FONT_GSUB_NONE);
    free(copy);
}

/* ---- cache-level rendering through the worker resolver path ---- */
typedef struct faces { font_t *primary, *emoji, *cjk; } faces;
static int resolve(void *ctx, uint32_t cp, font_t **face)
{
    faces *f = ctx; font_metric m;
    font_t *order[] = {f->primary, f->emoji, f->cjk};
    for (size_t i = 0; i < 3; i++)
        if (order[i] && font_glyph_metrics(order[i], cp, &m) == FONT_OK) { *face = order[i]; return FONT_OK; }
    return FONT_ERR_MISSING;
}
static int render(font_cache *c, const char *s, uint32_t width, uint8_t *img)
{
    c->pending_len = 0;
    int rc;
    do rc = font_cache_render(c, (const uint8_t *)s, strlen(s), width); while (rc == FONT_MORE);
    memcpy(img, c->image, (size_t)c->cell.cell_w * width * c->cell.cell_h);
    return rc;
}
static uint64_t fnv(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
static int inked(const uint8_t *p, size_t n) { uint8_t v = 0; for (size_t i = 0; i < n; i++) v |= p[i]; return v != 0; }

static uint64_t hash_strings(font_cache *c, const char *const *s, size_t count, uint32_t width, uint8_t *img)
{
    uint64_t h = UINT64_C(14695981039346656037);
    size_t bytes = (size_t)c->cell.cell_w * width * c->cell.cell_h;
    for (size_t i = 0; i < count; i++) {
        CHECK(render(c, s[i], width, img) == FONT_OK);
        CHECK(inked(img, bytes));
        h = fnv(h, img, bytes);
    }
    return h;
}

static void test_render(font_t *emoji)
{
    edit_arena files, storage;
    CHECK(edit_arena_init(&files, 64u << 20) == 0); /* (P) DejaVu + CJK TTC file bytes */
    CHECK(edit_arena_init(&storage, 8u << 20) == 0);
    size_t len = 0;
    unsigned char *bytes = font_load_file("vendor/DejaVuSansMono.ttf", &files, &len);
    font_t primary, cjk; font_family family; font_cache cache;
    CHECK(bytes && font_init(&primary, bytes, len) == FONT_OK && font_set_px(&primary, 15) == FONT_OK);
    CHECK(font_family_load(&family, &primary, NULL, &files) == FONT_OK);
    CHECK(font_cache_init(&cache, &family, &storage, 1, 64, 4096, 1u << 20) == FONT_OK);
    CHECK(font_set_px(emoji, 15) == FONT_OK);
    size_t cjk_len = 0;
    unsigned char *cjk_bytes = font_load_file(CJK_FACE, &files, &cjk_len);
    int have_cjk = cjk_bytes && font_init_index(&cjk, cjk_bytes, cjk_len, 0) == FONT_OK &&
                   font_set_px(&cjk, 15) == FONT_OK;
    faces f = {&family.faces[0], emoji, have_cjk ? &cjk : NULL};
    cache.resolve = resolve; cache.resolve_ctx = &f;

    size_t cell1 = (size_t)cache.cell.cell_w * cache.cell.cell_h, cell2 = cell1 * 2u;
    uint8_t *a = malloc(cell2), *b = malloc(cell2), *c = malloc(cell2);
    CHECK(a && b && c);
    if (!a || !b || !c) { free(a); free(b); free(c); return; }

    /* Latin and CJK never enter the GSUB path: byte-identical images with
     * every face's GSUB enabled and disabled, and equal to the pre-edit-v16
     * build (golden from the red run, same fonts). */
    static const char *latin[] = {"A", "e\xcc\x81", "g", "~", "\xc3\x9f", "W"};
    static const char *cjk_s[] = {"中", "文", "字"};
    uint64_t latin_on = hash_strings(&cache, latin, 6, 1, a);
    uint64_t cjk_on = have_cjk ? hash_strings(&cache, cjk_s, 3, 2, a) : 0;
    uint8_t keep_p = family.faces[0].gsub.state, keep_e = emoji->gsub.state, keep_c = have_cjk ? cjk.gsub.state : 0;
    family.faces[0].gsub.state = emoji->gsub.state = FONT_GSUB_NONE;
    if (have_cjk) cjk.gsub.state = FONT_GSUB_NONE;
    uint64_t latin_off = hash_strings(&cache, latin, 6, 1, a);
    uint64_t cjk_off = have_cjk ? hash_strings(&cache, cjk_s, 3, 2, a) : 0;
    family.faces[0].gsub.state = keep_p; emoji->gsub.state = keep_e;
    if (have_cjk) cjk.gsub.state = keep_c;
    printf("font_gsub_test: latin hash %016llx/%016llx cjk(%s) %016llx/%016llx (on/off)\n",
           (unsigned long long)latin_on, (unsigned long long)latin_off, have_cjk ? CJK_FACE : "absent",
           (unsigned long long)cjk_on, (unsigned long long)cjk_off);
    CHECK(latin_on == latin_off && cjk_on == cjk_off);
    /* (M) red run 2026-10-16 on the pre-edit-v16 font_cache_render: DejaVu
     * Sans Mono (vendored) and NotoSansCJK-Regular.ttc of 19484784 bytes. */
#define LATIN_GOLDEN UINT64_C(0xf99f7576f3aa4907)
#define CJK_GOLDEN UINT64_C(0xb792b4f50b3c92d9)
#define CJK_GOLDEN_LEN ((size_t)19484784)
    CHECK(latin_on == LATIN_GOLDEN);
    if (have_cjk && cjk_len == CJK_GOLDEN_LEN) CHECK(cjk_on == CJK_GOLDEN);

    /* Ligature artwork: one glyph, not the first emoji, not an overlay. */
    CHECK(render(&cache, "👩‍💻", 2, a) == FONT_OK && inked(a, cell2));
    CHECK(render(&cache, "👩", 2, b) == FONT_OK && inked(b, cell2));
    CHECK(memcmp(a, b, cell2) != 0);
    /* GSUB disabled: exactly the r8e first-emoji projection. */
    emoji->gsub.state = FONT_GSUB_NONE;
    CHECK(render(&cache, "👩‍💻", 2, c) == FONT_OK);
    CHECK(memcmp(c, b, cell2) == 0);
    CHECK(render(&cache, "🇦🇺", 2, c) == FONT_OK); /* both indicators overlaid */
    emoji->gsub.state = keep_e;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    CHECK(render(&cache, "🇦🇺", 2, a) == FONT_OK && inked(a, cell2));
    if (edit_malloc_guard_active()) CHECK(edit_malloc_guard_end() == 0); /* law 2 */
    CHECK(memcmp(a, c, cell2) != 0);
    /* A truncated-at-ZWJ tail renders as the leading ligature. */
    CHECK(render(&cache, "👩‍💻", 2, b) == FONT_OK);
    CHECK(render(&cache, "👩‍💻‍😀", 2, a) == FONT_OK && memcmp(a, b, cell2) == 0);
    free(a); free(b); free(c);
    edit_arena_free(&files); edit_arena_free(&storage);
}

static uint64_t now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
/* Tracked numbers (law 1): printed, never asserted. 101 samples so p50 and
 * p99 are order statistics 50 and 99 (E: sample count chosen for that). */
static void timings(font_t *f)
{
    enum { SAMPLES = 101 };
    uint64_t parse[SAMPLES], cluster[SAMPLES];
    uint16_t out[FONT_GSUB_MAX_GLYPHS];
    for (size_t i = 0; i < SAMPLES; i++) {
        font_gsub g;
        uint64_t t0 = now_ns();
        (void)font_gsub_parse(&g, f->gsub.table, f->gsub.len, NULL);
        parse[i] = now_ns() - t0;
        size_t n;
        t0 = now_ns();
        (void)font_gsub_cluster(f, (const uint8_t *)"👩🏻‍❤️‍💋‍👨🏼", sizeof "👩🏻‍❤️‍💋‍👨🏼" - 1u, out, &n);
        cluster[i] = now_ns() - t0;
    }
    qsort(parse, SAMPLES, sizeof parse[0], cmp_u64);
    qsort(cluster, SAMPLES, sizeof cluster[0], cmp_u64);
    char power[32] = "unknown", load[64] = "unknown";
    FILE *fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (fp) { if (!fgets(power, sizeof power, fp)) power[0] = 0; fclose(fp); power[strcspn(power, "\n")] = 0; }
    fp = fopen("/proc/loadavg", "r");
    if (fp) { if (!fgets(load, sizeof load, fp)) load[0] = 0; fclose(fp); load[strcspn(load, "\n")] = 0; }
    printf("font_gsub_test: (M)[%s] load '%s': parse fixture GSUB p50 %llu ns p99 %llu ns; "
           "10-scalar kiss cluster p50 %llu ns p99 %llu ns (tracked, not asserted)\n",
           power, load, (unsigned long long)parse[50], (unsigned long long)parse[99],
           (unsigned long long)cluster[50], (unsigned long long)cluster[99]);
}

int main(void)
{
    size_t len = 0;
    unsigned char *ttf = load(FIXTURE, &len);
    CHECK(ttf != NULL);
    if (!ttf) return 1;
    font_t f;
    CHECK(font_init(&f, ttf, len) == FONT_OK);
    test_parse(&f);
    test_clusters(&f);
    test_chain(&f);
    test_malformed(&f, ttf, len);
    test_render(&f);
    timings(&f);
    free(ttf);
    if (failures) { fprintf(stderr, "font_gsub_test: %d failures\n", failures); return 1; }
    printf("font_gsub_test: all passed\n");
    return 0;
}
