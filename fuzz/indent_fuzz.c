/* indent fuzzer: every query against the v2 model in tests/indent_model.h,
 * and every typing case v1 answered against the v1 whole-line model. Each
 * runs once guarded (bytes outside what the query may read on PROT_NONE
 * pages) and once with spans that straddle those edges. A mismatch or an
 * out-of-window read crashes the fuzzer.
 *
 * Input: [flags][cursor 2][lo 2][hi 2][width][stride][text...]
 *   flags bit 0: map text bytes onto the bytes the module distinguishes
 *   width: 1 + width % INDENT_MAX_WIDTH; stride: chunk length - 1 */
#include "../tests/indent_model.h"

#include <stdio.h>

#define B MODEL_B
enum { HEADER = 9 };
/* (P) a blank run past the bound on both sides of a selection, plus a CRLF. */
#define MAX_TEXT (3u * INDENT_BLANK_BYTES + 8u)

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "indent_fuzz: %s:%d: %s\n", __FILE__, __LINE__, #c); \
    __builtin_trap(); } } while (0)

static uint64_t pick(const uint8_t *d, uint64_t n) { return ((uint64_t)d[0] << 8 | d[1]) % (n + 1); }
static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t pre(uint64_t ls, uint64_t lo) { return ls > 0 && ls < lo ? ls - 1 : ls; }

/* What the typing queries may read: [0] Enter, [1] and [2] '}'. */
static void typing_windows(uint64_t n, uint64_t ls, uint64_t lo, uint64_t hi, indent_range w[3])
{
    w[0] = (indent_range){pre(ls, lo), min64(lo, ls + B + 1)};
    w[1] = (indent_range){pre(ls, lo), lo - ls > B ? ls : lo};
    w[2] = (indent_range){hi, min64(n, hi + B + 2)};
}

static void typing(gtext *g, bool guarded, const uint8_t *b, uint64_t n, uint64_t ls, uint64_t lo, uint64_t hi,
                   unsigned width)
{
    indent_range w[3];
    typing_windows(n, ls, lo, hi, w);
    if (guarded) CHECK(gtext_allow(g, w, 1));
    indent_range run = indent_on_enter(&g->text, ls, lo), want = model_enter(b, ls, lo);
    CHECK(run.lo == want.lo && run.hi == want.hi);
    if (guarded) CHECK(gtext_allow(g, w + 1, 2));
    uint64_t from = indent_on_close_brace(&g->text, ls, lo, hi, (uint8_t)width);
    CHECK(from == model_brace(b, n, ls, lo, hi, width));

    if (ls != model_line_start(b, lo)) return;
    static uint8_t L[MAX_TEXT];
    memcpy(L, b, lo);
    memcpy(L + lo, b + hi, n - hi);
    uint64_t ln = n - (hi - lo);
    if (!model_v1_answered(L, ln, lo)) return;
    CHECK(run.hi - run.lo == model_v1_lead(L, lo));
    CHECK(from == model_v1_brace(L, ln, lo, width));
}

static void queries(gtext *g, bool guarded, const uint8_t *b, uint64_t n, uint64_t cursor,
                    uint64_t lo, uint64_t hi, unsigned width)
{
    typing(g, guarded, b, n, model_line_start(b, cursor), cursor, cursor, width);
    typing(g, guarded, b, n, model_line_start(b, lo), lo, hi, width);
    typing(g, guarded, b, n, lo, lo, hi, width); /* unknown line start */

    if (guarded) CHECK(gtext_allow(g, &(indent_range){lo, hi}, 1));
    indent_pair pair = indent_bracket_match(&g->text, cursor, lo, hi), want = model_match(b, cursor, lo, hi);
    CHECK(pair.source == want.source && pair.mate == want.mate);

    static indent_range got[MAX_TEXT + 1], exp[MAX_TEXT + 1];
    size_t wcount = model_trailing(b, n, lo, hi, exp);
    size_t count = indent_trailing_ws(&g->text, lo, hi, got, MAX_TEXT + 1);
    CHECK(count == wcount && memcmp(got, exp, count * sizeof *got) == 0);
    CHECK(indent_trailing_ws(&g->text, lo, hi, got, width % (wcount + 1)) == wcount);

    if (guarded) CHECK(gtext_allow(g, &(indent_range){0, min64(n, INDENT_PREFIX_BYTES)}, 1));
    indent_style st = indent_detect(&g->text), ws = model_detect(b, n);
    CHECK(st.kind == ws.kind && st.eol == ws.eol && st.width == ws.width);
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
    static uint64_t cuts[MAX_TEXT + 32];
    size_t k = 0;
    for (uint64_t at = stride; at < n; at += stride) cuts[k++] = at;
    gtext g;
    CHECK(gtext_init(&g, b, (size_t)n, cuts, k));
    queries(&g, false, b, n, cursor, lo, hi, width);
    gtext_fini(&g);

    /* Guarded: plus every window edge the queries read up to. */
    k = 0;
    for (uint64_t at = stride; at < n; at += stride) cuts[k++] = at;
    indent_range w[9];
    typing_windows(n, model_line_start(b, cursor), cursor, cursor, w);
    typing_windows(n, model_line_start(b, lo), lo, hi, w + 3);
    typing_windows(n, lo, lo, hi, w + 6);
    for (size_t i = 0; i < 9; i++) { cuts[k++] = w[i].lo; cuts[k++] = w[i].hi; }
    cuts[k++] = lo;
    cuts[k++] = hi;
    cuts[k++] = INDENT_PREFIX_BYTES;
    CHECK(gtext_init(&g, b, (size_t)n, cuts, k));
    queries(&g, true, b, n, cursor, lo, hi, width);
    gtext_fini(&g);
    return 0;
}
