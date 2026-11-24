/* libFuzzer target for src/utf8. Input bytes are the text; libFuzzer hands
 * them over in an exact-size heap buffer and every derived view below is its
 * own exact-size heap block, so any read past the view is an ASan error (the
 * fuzz lane is ASan-only). Properties, each against an independent reference:
 *   - decode matches a bit-pattern reference decoder, at full length and
 *     truncated to every k < 4 (len <= k: never uses bytes past n);
 *   - valid units re-encode to the same bytes; re-encoding valid units and
 *     copying invalid bytes rebuilds the input byte-exactly; invalid bytes
 *     have cluster width 1;
 *   - prev from every unit end returns that unit's start; a backward walk
 *     visits exactly the forward units;
 *   - cell_width is 0..2 and the inline and table paths agree;
 *   - grapheme_next advances, never overruns, lands on unit boundaries; the
 *     artwork projection is bounded, works in place, and drops ZWJ, VS15,
 *     VS16 and skin tones;
 *   - grapheme oracle: the first 512 units are segmented by a stateless
 *     UAX #29 implementation (its own GCB classifier, no shortcuts) and must
 *     equal first_grapheme; grapheme_prev from every cluster end and every
 *     unit end inside one returns the cluster start; first_cluster's width
 *     equals the cluster-width rule of utf8.h evaluated on oracle classes;
 *   - resumable step: for random budgets and a random chunking (each call sees
 *     an exact-size copy of what is available, eof only on the last),
 *     utf8_grapheme_step and utf8_cluster_step equal the one-shot calls and no
 *     call exceeds budget + 3 bytes;
 *   - encode of arbitrary 32-bit values decodes back to the value or U+FFFD. */
#include "../tests/utf8_fixture.h"
#include "utf8/tables.h"

#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static utf8_step ref_decode(const uint8_t *p, size_t n)
{
    utf8_step bad = { p[0], 1, 0 };
    uint8_t b = p[0];
    size_t len;
    uint32_t cp, min;
    if (b < 0x80) { utf8_step s = { b, 1, 1 }; return s; }
    if ((b & 0xE0) == 0xC0) { len = 2; cp = b & 0x1Fu; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0Fu; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07u; min = 0x10000; }
    else return bad;
    if (n < len) return bad;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return bad;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return bad;
    utf8_step s = { cp, (uint8_t)len, 1 };
    return s;
}

/* ---- independent grapheme oracle (same shape as tests/utf8_test.c) ---- */
enum { R_OTHER, R_CR, R_LF, R_CTL, R_EXT, R_ZWJ, R_SPC, R_PRE, R_RI, R_L, R_V, R_T, R_LV, R_LVT, R_PICT };
static int ref_in(const utf8_range *t, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < t[mid].lo) hi = mid;
        else if (cp > t[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}
#define RIN(tab, cp) ref_in(tab, NELEM(tab), cp)
static int ref_width(uint32_t cp)
{
    if (cp >= 0x1f3fbu && cp <= 0x1f3ffu) return 0;     /* (G) skin tones have no own cell */
    if (RIN(UCD_ZERO_WIDTH, cp)) return 0;
    if (RIN(UCD_WIDE, cp)) return 2;
    return 1;
}
static int ref_class(int valid, uint32_t cp)
{
    if (!valid) return R_CTL;
    if (cp == '\r') return R_CR;
    if (cp == '\n') return R_LF;
    if (RIN(UCD_CONTROL, cp)) return R_CTL;
    if (cp == 0x200D) return R_ZWJ;
    if (RIN(UCD_EXTEND, cp)) return R_EXT;
    if (RIN(UCD_SPACINGMARK, cp)) return R_SPC;
    if (RIN(UCD_PREPEND, cp)) return R_PRE;
    if (RIN(UCD_REGIONAL_INDICATOR, cp)) return R_RI;
    if (RIN(UCD_HANGUL_L, cp)) return R_L;
    if (RIN(UCD_HANGUL_V, cp)) return R_V;
    if (RIN(UCD_HANGUL_T, cp)) return R_T;
    if (RIN(UCD_HANGUL_LV, cp)) return R_LV;
    if (RIN(UCD_HANGUL_LVT, cp)) return R_LVT;
    if (RIN(UCD_EXT_PICT, cp)) return R_PICT;
    return R_OTHER;
}
static int ref_incb(int valid, uint32_t cp)
{
    if (!valid) return 0;
    if (RIN(UCD_INCB_CONSONANT, cp)) return 1;
    if (RIN(UCD_INCB_LINKER, cp)) return 2;
    if (RIN(UCD_INCB_EXTEND, cp)) return 3;
    return 0;
}
static int ref_break(const int *c, const int *ib, int i)
{
    int a = c[i - 1], b = c[i];
    if (a == R_CR && b == R_LF) return 0;
    if (a == R_CR || a == R_LF || a == R_CTL) return 1;
    if (b == R_CR || b == R_LF || b == R_CTL) return 1;
    if (a == R_L && (b == R_L || b == R_V || b == R_LV || b == R_LVT)) return 0;
    if ((a == R_LV || a == R_V) && (b == R_V || b == R_T)) return 0;
    if ((a == R_LVT || a == R_T) && b == R_T) return 0;
    if (b == R_EXT || b == R_ZWJ || b == R_SPC) return 0;
    if (a == R_PRE) return 0;
    if (ib[i] == 1) {
        int j = i - 1, linker = 0;
        while (j >= 0 && (ib[j] == 2 || ib[j] == 3)) { linker |= ib[j] == 2; j--; }
        if (j >= 0 && ib[j] == 1 && linker) return 0;
    }
    if (a == R_ZWJ && b == R_PICT) {
        int j = i - 2;
        while (j >= 0 && c[j] == R_EXT) j--;
        if (j >= 0 && c[j] == R_PICT) return 0;
    }
    if (a == R_RI && b == R_RI) {
        int k = 0, j = i - 1;
        while (j >= 0 && c[j] == R_RI) { k++; j--; }
        if (k % 2 == 1) return 0;
    }
    return 1;
}
/* The cluster-width rule of utf8.h over one cluster's units u[0..m). */
static int ref_cluster_width(const utf8_step *u, const int *c, int m)
{
    if (m == 0) return 0;
    if (!u[0].valid || c[0] == R_CR || c[0] == R_LF || c[0] == R_CTL) return 1;
    int k = 0;
    while (k < m && ref_width(u[k].cp) == 0) k++;
    if (k == m) return 0;
    int b = c[k], w = ref_width(u[k].cp);
    if (b == R_L || b == R_V || b == R_T || b == R_LV || b == R_LVT) return 2;
    if (w != 1) return w;
    uint32_t base = u[k].cp;
    int kbase = base == '#' || base == '*' || (base >= '0' && base <= '9');
    for (int j = k + 1; j < m; j++) {
        if (b == R_PICT && (u[j].cp == 0xFE0F || (c[j - 1] == R_ZWJ && c[j] == R_PICT))) return 2;
        if (kbase && u[j].cp == 0x20E3) return 2;
    }
    return 1;
}

static uint8_t *exact_copy(const uint8_t *p, size_t n)    /* own heap block: ASan bounds at p+n */
{
    uint8_t *q = malloc(n ? n : 1);
    REQUIRE(q);
    memcpy(q, p, n);
    return q;
}

static uint32_t lcg(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 8;
}

/* (E) v1's cut: the oracle covers the first MU units (its left-context scans
 * are quadratic in the worst case); the view is cut at the same unit, so a
 * cluster that spans the cut is cut for both sides alike. */
enum { MU = 512 };

static void oracle(const uint8_t *data, size_t size)
{
    static int c[MU], ib[MU];
    static utf8_step u[MU];
    static size_t pos[MU + 1];
    int m = 0;
    pos[0] = 0;
    for (size_t o = 0; o < size && m < MU; m++) {
        utf8_step s = utf8_decode(data + o, size - o);
        pos[m] = o;
        u[m] = s;
        c[m] = ref_class(s.valid, s.cp);
        ib[m] = ref_incb(s.valid, s.cp);
        o += s.len;
        pos[m + 1] = o;
    }
    uint8_t *v = exact_copy(data, pos[m]);
    int i = 0;
    for (size_t o = 0; o < pos[m];) {
        int j = i + 1;
        while (j < m && !ref_break(c, ib, j)) j++;
        size_t g = first_grapheme(v + o, pos[m] - o);
        REQUIRE(g == pos[j] - o);
        REQUIRE(utf8_grapheme_prev(v, o + g) == o);
        for (int k = i + 1; k < j; k++)                  /* unit ends inside the cluster */
            REQUIRE(utf8_grapheme_prev(v, pos[k]) == o);
        int w;
        REQUIRE(first_cluster(v + o, pos[m] - o, &w) == g);
        REQUIRE(w == ref_cluster_width(u + i, c + i, j - i));
        o += g;
        i = j;
    }
    free(v);
}

/* utf8_grapheme_step (cluster = 0) or utf8_cluster_step (cluster = 1) over
 * data[0..lim) revealed chunk bytes at a time must equal the one-shot call. */
static void chunked(const uint8_t *data, size_t lim, size_t budget, size_t chunk, int cluster)
{
    int wantw = 0, w = -1, r = UTF8_G_MORE;
    size_t want = cluster ? first_cluster(data, lim, &wantw) : first_grapheme(data, lim);
    utf8_gseg g;
    utf8_cseg c;
    utf8_gseg_init(&g);
    utf8_cseg_init(&c);
    size_t pos = 0, avail = chunk < lim ? chunk : lim, guard = 0;
    while (guard++ < 8 * lim + 16) {
        uint8_t *v = exact_copy(data + pos, avail - pos);
        size_t used = 0;
        int eof = avail == lim;
        r = cluster ? utf8_cluster_step(&c, v, avail - pos, budget, eof, &used, &w)
                    : utf8_grapheme_step(&g, v, avail - pos, budget, eof, &used);
        free(v);
        REQUIRE(used <= avail - pos);
        REQUIRE(used <= (budget ? budget : 1) + 3);
        pos += used;
        if (r == UTF8_G_END) break;
        if (r == UTF8_G_MORE) { REQUIRE(!eof); avail = avail + chunk < lim ? avail + chunk : lim; }
        else REQUIRE(r == UTF8_G_BUDGET);
    }
    REQUIRE(r == UTF8_G_END && pos == want);
    REQUIRE(!cluster || w == wantw);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        REQUIRE(first_grapheme(data, 0) == 0);
        REQUIRE(utf8_test_prev(data, 0) == 0);
        return 0;
    }
    uint8_t *unit = calloc(size + 1, 1);            /* unit[o] = 1 iff o is a unit boundary */
    uint8_t *rebuilt = malloc(size);
    REQUIRE(unit && rebuilt);
    size_t off = 0, units = 0;
    while (off < size) {
        size_t rem = size - off;
        utf8_step s = utf8_decode(data + off, rem), r = ref_decode(data + off, rem);
        REQUIRE(s.cp == r.cp && s.len == r.len && s.valid == r.valid);
        REQUIRE(s.len >= 1 && s.len <= 4 && s.len <= rem);
        if (s.valid) {
            uint8_t e[4];
            REQUIRE(utf8_encode(s.cp, e) == s.len && memcmp(e, data + off, s.len) == 0);
            memcpy(rebuilt + off, e, s.len);
        } else {
            REQUIRE(s.len == 1 && s.cp == data[off] && data[off] >= 0x80);
            rebuilt[off] = (uint8_t)s.cp;           /* invalid: the original byte */
            REQUIRE(first_cluster_width(data + off, rem) == 1);   /* never the width of scalar U+00xx */
        }
        for (size_t k = 1; k < 4 && k < rem; k++) {  /* truncated view, own heap block */
            uint8_t *v = exact_copy(data + off, k);
            utf8_step t = utf8_decode(v, k), tr = ref_decode(v, k);
            REQUIRE(t.cp == tr.cp && t.len == tr.len && t.valid == tr.valid && t.len <= k);
            free(v);
        }
        int w = utf8_test_cell_width(s.cp);
        REQUIRE(w >= 0 && w <= 2);
        REQUIRE(!s.valid || w == ref_width(s.cp));
        unit[off] = 1;
        off += s.len;
        units++;
        REQUIRE(utf8_test_prev(data, off) == off - s.len);
    }
    unit[size] = 1;
    REQUIRE(memcmp(rebuilt, data, size) == 0);      /* byte-exact round trip, malformed input included */
    free(rebuilt);
    size_t back = 0;
    for (size_t o = size; o > 0; back++) {
        size_t p = utf8_test_prev(data, o);
        REQUIRE(p < o && unit[p]);
        o = p;
    }
    REQUIRE(back == units);
    for (off = 0; off < size;) {
        size_t g = first_grapheme(data + off, size - off);
        REQUIRE(g >= 1 && g <= size - off && unit[off + g]);
        uint8_t *source = exact_copy(data + off, g), *artwork = malloc(g);
        REQUIRE(artwork);
        size_t drawn = utf8_cluster_artwork(source, g, artwork);
        REQUIRE(drawn <= g);
        REQUIRE(utf8_cluster_artwork(source, g, source) == drawn && !memcmp(source, artwork, drawn));
        for (size_t at = 0; at < drawn;) {
            utf8_step step = utf8_decode(artwork + at, drawn - at);
            REQUIRE(!step.valid || (step.cp != 0x200du && step.cp != 0xfe0eu && step.cp != 0xfe0fu &&
                (step.cp < 0x1f3fbu || step.cp > 0x1f3ffu)));
            at += step.len;
        }
        free(source);
        free(artwork);
        off += g;
    }
    oracle(data, size);
    {   /* chunked == one-shot, budget and chunking seeded from the input */
        uint32_t seed = 2166136261u;
        for (size_t i = 0; i < size && i < 64; i++) seed = (seed ^ data[i]) * 16777619u;
        /* (P) libFuzzer's default -max_len is 4096; longer corpus files are cut here. */
        size_t lim = size < 4096 ? size : 4096;
        for (int rep = 0; rep < 6; rep++) {
            /* (E) v1's ranges: budgets and refills smaller than a few units,
             * so cuts land inside units and inside clusters. */
            size_t budget = lcg(&seed) % 9;              /* 0..8, 0 means 1 */
            size_t chunk = 1 + lcg(&seed) % 11;          /* bytes revealed per refill */
            chunked(data, lim, budget, chunk, rep & 1);
        }
    }
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t x;
        memcpy(&x, data + i, 4);
        uint8_t e[4];
        size_t l = utf8_encode(x, e);
        utf8_step s = utf8_decode(e, l);
        int scalar = x <= 0x10FFFF && !(x >= 0xD800 && x <= 0xDFFF);
        REQUIRE(s.valid && s.len == l && s.cp == (scalar ? x : 0xFFFDu));
    }
    free(unit);
    return 0;
}
