/* indent fuzzer: every query against the reference model in
 * tests/indent_model.h, once guarded (bytes outside the query's readable
 * windows on PROT_NONE pages) and once with spans that straddle the window
 * edges. A mismatch or an out-of-window read crashes the fuzzer.
 *
 * Input: [flags][cursor 2][lo 2][hi 2][width][stride][text...]
 *   flags bit 0: map text bytes onto the bytes the module distinguishes
 *   width: 1 + width % INDENT_MAX_WIDTH; stride: chunk length - 1 */
#include "../tests/indent_model.h"

#include <stdio.h>

#define T MODEL_T
enum { HEADER = 9 };
/* (P) a cursor line longer than both typing windows, plus its CRLF. */
#define MAX_TEXT (3u * INDENT_TYPING_BYTES + 2u)

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "indent_fuzz: %s:%d: %s\n", __FILE__, __LINE__, #c); \
    __builtin_trap(); } } while (0)

static uint64_t pick(const uint8_t *d, uint64_t n) { return ((uint64_t)d[0] << 8 | d[1]) % (n + 1); }

static void typing_windows(uint64_t n, uint64_t lo, uint64_t hi, indent_range w[2])
{
    w[0] = (indent_range){lo > T + 1 ? lo - T - 1 : 0, lo};
    w[1] = (indent_range){hi, n - hi > T ? hi + T : n};
}

static void typing(gtext *g, bool guarded, const uint8_t *b, uint64_t n, uint64_t lo, uint64_t hi, unsigned width)
{
    if (guarded) {
        indent_range w[2];
        typing_windows(n, lo, hi, w);
        CHECK(gtext_allow(g, w, 2));
    }
    static uint8_t L[MAX_TEXT];
    memcpy(L, b, lo);
    memcpy(L + lo, b + hi, n - hi);
    uint64_t ln = n - (hi - lo);
    uint8_t out[INDENT_ENTER_MAX], want[INDENT_ENTER_MAX];
    memset(out, 0xa5, sizeof out);
    size_t len = 99, wlen = 0;
    indent_code rc = indent_on_enter(&g->text, lo, hi, out, &len);
    CHECK(rc == model_enter(L, ln, lo, want, &wlen));
    CHECK(len == wlen && memcmp(out, want, wlen) == 0);
    for (size_t i = rc == INDENT_OK ? len : 0; i < sizeof out; i++) CHECK(out[i] == 0xa5);
    uint64_t from = 7, wfrom = 0;
    bool consistent = false;
    rc = indent_on_close_brace(&g->text, lo, hi, (uint8_t)width, &from);
    CHECK(rc == model_brace(L, ln, lo, width, &wfrom, &consistent));
    CHECK(consistent && from == wfrom);
}

static void queries(gtext *g, bool guarded, const uint8_t *b, uint64_t n, uint64_t cursor,
                    uint64_t lo, uint64_t hi, unsigned width)
{
    typing(g, guarded, b, n, cursor, cursor, width);
    typing(g, guarded, b, n, lo, hi, width);

    if (guarded) CHECK(gtext_allow(g, &(indent_range){lo, hi}, 1));
    indent_pair pair;
    indent_pair want = model_match(b, cursor, lo, hi);
    CHECK(indent_bracket_match(&g->text, cursor, lo, hi, &pair) == INDENT_OK);
    CHECK(pair.source == want.source && pair.mate == want.mate);

    static indent_range got[MAX_TEXT + 1], exp[MAX_TEXT + 1];
    size_t count = 0, wcount = model_trailing(b, n, lo, hi, exp);
    CHECK(indent_trailing_ws(&g->text, lo, hi, got, MAX_TEXT + 1, &count) == INDENT_OK);
    CHECK(count == wcount && memcmp(got, exp, count * sizeof *got) == 0);
    size_t cap = width % (wcount + 1);
    CHECK(indent_trailing_ws(&g->text, lo, hi, got, cap, &count) == (wcount > cap ? INDENT_ERR_CAPACITY : INDENT_OK));
    CHECK(count == wcount);

    if (guarded) CHECK(gtext_allow(g, &(indent_range){0, n > INDENT_PREFIX_BYTES ? INDENT_PREFIX_BYTES : n}, 1));
    indent_style st = indent_detect(&g->text), ws = model_detect(b, n);
    CHECK(st.kind == ws.kind && st.width == ws.width);
    CHECK(g->bad == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < HEADER) return 0;
    static const uint8_t alphabet[] = " \t\r\n()[]{}x\"\\\xc3\xa9";
    static uint8_t b[MAX_TEXT];
    uint64_t n = size - HEADER > MAX_TEXT ? MAX_TEXT : size - HEADER;
    for (uint64_t i = 0; i < n; i++)
        b[i] = data[0] & 1u ? alphabet[data[HEADER + i] % (sizeof alphabet - 1)] : data[HEADER + i];
    uint64_t cursor = pick(data + 1, n), lo = pick(data + 3, n), hi = pick(data + 5, n);
    if (lo > hi) { uint64_t x = lo; lo = hi; hi = x; }
    unsigned width = 1u + data[7] % INDENT_MAX_WIDTH;
    uint64_t stride = (uint64_t)data[8] + 1;

    /* Straddled: chunk edges every stride bytes only. */
    static uint64_t cuts[MAX_TEXT + 16];
    size_t k = 0;
    for (uint64_t at = stride; at < n; at += stride) cuts[k++] = at;
    gtext g;
    CHECK(gtext_init(&g, b, (size_t)n, cuts, k));
    queries(&g, false, b, n, cursor, lo, hi, width);
    gtext_fini(&g);

    /* Guarded: plus every window edge the queries read up to. */
    k = 0;
    for (uint64_t at = stride; at < n; at += stride) cuts[k++] = at;
    indent_range w[4];
    typing_windows(n, cursor, cursor, w);
    typing_windows(n, lo, hi, w + 2);
    for (size_t i = 0; i < 4; i++) { cuts[k++] = w[i].lo; cuts[k++] = w[i].hi; }
    cuts[k++] = INDENT_PREFIX_BYTES;
    CHECK(gtext_init(&g, b, (size_t)n, cuts, k));
    queries(&g, true, b, n, cursor, lo, hi, width);
    gtext_fini(&g);

    /* Contract errors: no output. */
    uint8_t out[INDENT_ENTER_MAX];
    size_t len = 99;
    uint64_t from = 7;
    indent_pair pair;
    CHECK(gtext_init(&g, b, (size_t)n, NULL, 0));
    CHECK(indent_on_enter(&g.text, n + 1, n + 1, out, &len) == INDENT_ERR_RANGE && len == 0);
    if (lo < hi) {
        CHECK(indent_on_enter(&g.text, hi, lo, out, &len) == INDENT_ERR_RANGE && len == 0);
        CHECK(indent_on_close_brace(&g.text, hi, lo, (uint8_t)width, &from) == INDENT_ERR_RANGE && from == INDENT_NONE);
        CHECK(indent_bracket_match(&g.text, cursor, hi, lo, &pair) == INDENT_ERR_RANGE && pair.mate == INDENT_NONE);
    }
    CHECK(indent_on_close_brace(&g.text, cursor, cursor, 0, &from) == INDENT_ERR_ARGUMENT && from == INDENT_NONE);
    gtext_fini(&g);
    return 0;
}
