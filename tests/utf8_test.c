/* utf8 suite (P1.1b best-of): union of the three P1.1 variant suites, with
 * width expectations re-derived from the generated UCD 15.1 tables, plus
 * exhaustive decoder classes, truncation at every length, forward/prev
 * consistency over every scalar, and UAX #29 cluster cases. */
#include "utf8/utf8.h"
#include "utf8/tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
/* Stop a loop after its first failure so one bug does not print a million lines. */
#define CHECK_BREAK(c) if (!(c)) { CHECK(c); break; }
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* Exact-size heap window: bytes are placed at the end of an 8-byte heap block,
 * so any read past p[n-1] is an ASan heap-buffer-overflow. */
enum { TAIL = 8 };
static uint8_t *tail4;
static const uint8_t *place(const uint8_t *src, size_t n)
{
    memcpy(tail4 + TAIL - n, src, n);
    return tail4 + TAIL - n;
}
static utf8_step dec(const char *s, size_t n) { return utf8_decode(place((const uint8_t *)s, n), n); }

/* ---- independent reference decoder: bit patterns + scalar range checks
 * (a different formulation from the Table 3-7 byte ranges in utf8.c). */
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

static int in_tab(const utf8_range *t, size_t n, uint32_t cp)
{
    for (size_t lo = 0, hi = n; lo < hi;) {
        size_t m = lo + (hi - lo) / 2;
        if (cp < t[m].lo) hi = m;
        else if (cp > t[m].hi) lo = m + 1;
        else return 1;
    }
    return 0;
}
static int ref_width(uint32_t cp)
{
    if (in_tab(UCD_ZERO_WIDTH, NELEM(UCD_ZERO_WIDTH), cp)) return 0;
    if (in_tab(UCD_WIDE, NELEM(UCD_WIDE), cp)) return 2;
    return 1;
}

static size_t enc_len(uint32_t cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }


/* ---- independent grapheme oracle: UAX #29 break decision between unit i-1 and
 * i by scanning left for context (no running state), over the generated
 * property tables. Invalid bytes are Control. Shared shape with fuzz/utf8_fuzz.c. */
enum { R_OTHER, R_CR, R_LF, R_CTL, R_EXT, R_ZWJ, R_SPC, R_PRE, R_RI, R_L, R_V, R_T, R_LV, R_LVT, R_PICT };
static int ref_in(const utf8_range *t, size_t n, uint32_t cp)
{
    for (size_t i = 0; i < n; i++) if (cp >= t[i].lo && cp <= t[i].hi) return 1;
    return 0;
}
#define RIN(tab, cp) ref_in(tab, NELEM(tab), cp)
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
static int ref_incb(int valid, uint32_t cp)       /* 1 Consonant, 2 Linker, 3 Extend */
{
    if (!valid) return 0;
    if (RIN(UCD_INCB_CONSONANT, cp)) return 1;
    if (RIN(UCD_INCB_LINKER, cp)) return 2;
    if (RIN(UCD_INCB_EXTEND, cp)) return 3;
    return 0;
}
/* c[], ib[]: classes of units 0..m-1. Returns 1 if there is a break before unit i (1 <= i < m). */
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
    if (ib[i] == 1) {                                   /* GB9c */
        int j = i - 1, linker = 0;
        while (j >= 0 && (ib[j] == 2 || ib[j] == 3)) { linker |= ib[j] == 2; j--; }
        if (j >= 0 && ib[j] == 1 && linker) return 0;
    }
    if (a == R_ZWJ && b == R_PICT) {                    /* GB11 */
        int j = i - 2;
        while (j >= 0 && c[j] == R_EXT) j--;
        if (j >= 0 && c[j] == R_PICT) return 0;
    }
    if (a == R_RI && b == R_RI) {                       /* GB12, GB13 */
        int k = 0, j = i - 1;
        while (j >= 0 && c[j] == R_RI) { k++; j--; }
        if (k % 2 == 1) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- decode */
#define VALID(s, n, c, l) do { utf8_step t_ = dec(s, n); CHECK(t_.valid == 1 && t_.cp == (c) && t_.len == (l)); } while (0)
#define INV(s) do { utf8_step t_ = dec(s, sizeof(s) - 1); \
    CHECK(t_.valid == 0 && t_.len == 1 && t_.cp == (uint8_t)(s)[0]); } while (0)

static void t_decode_cases(void)
{
    VALID("A", 1, 'A', 1);
    VALID("\x00", 1, 0, 1);
    VALID("\x7F", 1, 0x7F, 1);
    VALID("\xC2\x80", 2, 0x80, 2);
    VALID("\xC3\xA9", 2, 0xE9, 2);
    VALID("\xDF\xBF", 2, 0x7FF, 2);
    VALID("\xE0\xA0\x80", 3, 0x800, 3);
    VALID("\xE2\x82\xAC", 3, 0x20AC, 3);
    VALID("\xED\x9F\xBF", 3, 0xD7FF, 3);
    VALID("\xEE\x80\x80", 3, 0xE000, 3);
    VALID("\xEF\xBF\xBF", 3, 0xFFFF, 3);
    VALID("\xF0\x90\x80\x80", 4, 0x10000, 4);
    VALID("\xF0\x9F\x98\x80", 4, 0x1F600, 4);
    VALID("\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 4);
    VALID("\xC3\xA9\xA9", 3, 0xE9, 2);           /* trailing stray continuation is the next unit */
    INV("\x80"); INV("\xBF");                     /* stray continuations */
    INV("\xC0\x80"); INV("\xC1\xBF");             /* C0/C1 overlong leads */
    INV("\xE0\x80\x80"); INV("\xE0\x9F\xBF");     /* E0 overlong */
    INV("\xED\xA0\x80"); INV("\xED\xBF\xBF");     /* surrogates */
    INV("\xF0\x80\x80\x80"); INV("\xF0\x8F\xBF\xBF"); /* F0 overlong */
    INV("\xF4\x90\x80\x80"); INV("\xF5\x80\x80\x80"); /* > U+10FFFF */
    INV("\xF8\x88\x80\x80\x80"); INV("\xFC\x84\x80\x80\x80\x80"); /* 5/6-byte forms */
    INV("\xFE"); INV("\xFF");
    INV("\xC3"); INV("\xE2\x82"); INV("\xF0\x9F\x98");  /* truncated by n */
    INV("\xC3\x28"); INV("\xE2\x28\xA1"); INV("\xE2\x82\x28"); INV("\xF0\x9F\x28\x80"); INV("\xF0\x9F\x98\x28");
    { utf8_step t = dec("\xC3\xA9", 1); CHECK(!t.valid && t.len == 1 && t.cp == 0xC3); } /* n truncates even if p[1] is fine */
}

/* Every lead x every second byte x boundary values for bytes 3/4, at every
 * n = 1..4, against the reference, with exact-size placement. */
static void t_decode_exhaustive(void)
{
    static const uint8_t R[] = { 0x00, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xFF };
    long bad = 0;
    for (unsigned a = 0; a < 256; a++)
        for (unsigned b = 0; b < 256; b++)
            for (unsigned c = 0; c < NELEM(R); c++)
                for (unsigned d = 0; d < NELEM(R); d++) {
                    uint8_t src[4] = { (uint8_t)a, (uint8_t)b, R[c], R[d] };
                    for (size_t n = 1; n <= 4; n++) {
                        utf8_step x = utf8_decode(place(src, n), n), y = ref_decode(src, n);
                        if (x.cp != y.cp || x.len != y.len || x.valid != y.valid) {
                            if (bad++ < 4) printf("decode %02X %02X %02X %02X n=%zu: got {%X,%u,%u} want {%X,%u,%u}\n",
                                                  a, b, R[c], R[d], n, x.cp, x.len, x.valid, y.cp, y.len, y.valid);
                        }
                    }
                }
    CHECK(bad == 0);
    /* Table 3-7 second-byte windows, stated directly. */
    for (unsigned b = 0; b < 256; b++) {
        uint8_t e0[3] = { 0xE0, (uint8_t)b, 0x80 }, ed[3] = { 0xED, (uint8_t)b, 0x80 };
        uint8_t f0[4] = { 0xF0, (uint8_t)b, 0x80, 0x80 }, f4[4] = { 0xF4, (uint8_t)b, 0x80, 0x80 };
        uint8_t e1[3] = { 0xE1, (uint8_t)b, 0x80 }, f1[4] = { 0xF1, (uint8_t)b, 0x80, 0x80 };
        CHECK(utf8_decode(place(e0, 3), 3).valid == (b >= 0xA0 && b <= 0xBF));
        CHECK(utf8_decode(place(ed, 3), 3).valid == (b >= 0x80 && b <= 0x9F));
        CHECK(utf8_decode(place(f0, 4), 4).valid == (b >= 0x90 && b <= 0xBF));
        CHECK(utf8_decode(place(f4, 4), 4).valid == (b >= 0x80 && b <= 0x8F));
        CHECK(utf8_decode(place(e1, 3), 3).valid == (b >= 0x80 && b <= 0xBF));
        CHECK(utf8_decode(place(f1, 4), 4).valid == (b >= 0x80 && b <= 0xBF));
    }
    /* Lead classes: only 00..7F and C2..F4 can start a valid unit. */
    for (unsigned a = 0x80; a < 256; a++) {
        uint8_t src[4] = { (uint8_t)a, 0, 0, 0 };
        int any = 0;
        for (unsigned b1 = 0x80; b1 < 0xC0 && !any; b1++) {
            src[1] = (uint8_t)b1; src[2] = 0x80; src[3] = 0x80;
            any |= utf8_decode(place(src, 4), 4).valid;
        }
        CHECK(any == (a >= 0xC2 && a <= 0xF4));
    }
}

/* ------------------------------------------------- scalars: encode, prev */
static void t_all_scalars(void)
{
    static const char *ctx[] = { "", "a", "\x80", "\xE1\x80", "\xF0\x9F\x98", "\xC3\xA9", "\xFF", "\xF0\x9F\x98\x80" };
    uint8_t buf[16];
    size_t bounds[16];
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        uint8_t b[4];
        size_t l = utf8_encode(cp, b);
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            CHECK_BREAK(l == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
            uint8_t h[3] = { (uint8_t)(0xE0 | (cp >> 12)), (uint8_t)(0x80 | ((cp >> 6) & 63)), (uint8_t)(0x80 | (cp & 63)) };
            CHECK_BREAK(!utf8_decode(place(h, 3), 3).valid);   /* hand-encoded surrogate */
            continue;
        }
        CHECK_BREAK(l == enc_len(cp));
        utf8_step s = utf8_decode(place(b, l), l);
        CHECK_BREAK(s.valid && s.cp == cp && s.len == l);
        CHECK_BREAK(utf8_prev(b, l) == 0);
        /* truncation at every shorter length: invalid single byte */
        int tr_ok = 1;
        for (size_t k = 1; k < l; k++) {
            utf8_step t = utf8_decode(place(b, k), k);
            tr_ok &= !t.valid && t.len == 1 && t.cp == b[0];
        }
        CHECK_BREAK(tr_ok);
        /* forward/prev consistency with valid and invalid left context */
        int pv_ok = 1;
        for (size_t c = 0; c < NELEM(ctx); c++) {
            size_t cl = strlen(ctx[c]), n = cl + l, nb = 0, off = 0;
            memcpy(buf, ctx[c], cl);
            memcpy(buf + cl, b, l);
            while (off < n) { bounds[nb++] = off; off += utf8_decode(buf + off, n - off).len; }
            pv_ok &= nb >= 1 && bounds[nb - 1] == cl;    /* scalar is its own unit */
            for (size_t k = nb, o = n; k > 0; k--) { o = utf8_prev(buf, o); pv_ok &= o == bounds[k - 1]; }
        }
        CHECK_BREAK(pv_ok);
    }
    uint8_t b[4];
    CHECK(utf8_encode(0x110000, b) == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
    CHECK(utf8_encode(0xFFFFFFFFu, b) == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
    CHECK(utf8_encode(0xD800, b) == 3 && b[0] == 0xEF);
    CHECK(utf8_prev((const uint8_t *)"abc", 0) == 0);
    CHECK(utf8_prev((const uint8_t *)"\x80\x80\x80\x80\x80", 5) == 4);         /* 4+ continuations */
    CHECK(utf8_prev((const uint8_t *)"\xE2\x82\xAC\x80", 4) == 3);            /* valid unit + stray */
    CHECK(utf8_prev((const uint8_t *)"\xF0\x9F\x98\x80\x80", 5) == 4);
    CHECK(utf8_prev((const uint8_t *)"\xF0\x9F\x98\x80", 4) == 0);
    CHECK(utf8_prev((const uint8_t *)"\xE0\x80\x80", 3) == 2);                /* overlong: 3 units */
}

/* ----------------------------------------------------------------- width */
static void t_width(void)
{
    long bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        int w = utf8_cell_width(cp), wt = utf8_cell_width_table(cp), r = ref_width(cp);
        if ((w != r || wt != r) && bad++ < 4) printf("width U+%04X: got %d/%d want %d\n", cp, w, wt, r);
    }
    CHECK(bad == 0);
    CHECK(utf8_cell_width(0x110000) == 1 && utf8_cell_width(0xFFFFFFFFu) == 1 && utf8_cell_width_table(0xFFFFFFFFu) == 1);
    /* Spot checks: union of the variants', each re-derived from tables.h. */
    CHECK(utf8_cell_width('a') == 1);
    CHECK(utf8_cell_width(0x07) == 1);       /* controls: 1, drawn as placeholders */
    CHECK(utf8_cell_width(0x1B) == 1);
    CHECK(utf8_cell_width(0x7F) == 1);
    CHECK(utf8_cell_width(0xE9) == 1);
    CHECK(utf8_cell_width(0x00AD) == 0);     /* soft hyphen is Cf: all three variants said 1 */
    CHECK(utf8_cell_width(0x0301) == 0);
    CHECK(utf8_cell_width(0x0600) == 0);     /* Arabic number sign (Cf, Prepend): v1/v3 said 1 */
    CHECK(utf8_cell_width(0x0915) == 1);
    CHECK(utf8_cell_width(0x093E) == 0);     /* Mc is zero-width in the P1.1a tables */
    CHECK(utf8_cell_width(0x0E01) == 1);
    CHECK(utf8_cell_width(0x0E31) == 0);
    CHECK(utf8_cell_width(0x115F) == 0);     /* Hangul choseong filler: DI wins over W; variants said 2 */
    CHECK(utf8_cell_width(0x1161) == 1);     /* medial jamo: EAW N; v2/v3 said 0 */
    CHECK(utf8_cell_width(0x200B) == 0);
    CHECK(utf8_cell_width(0x200D) == 0);
    CHECK(utf8_cell_width(0x2603) == 1);     /* snowman: text presentation */
    CHECK(utf8_cell_width(0x2FFC) == 2);     /* new in 15.1: v2/v3 said 1 */
    CHECK(utf8_cell_width(0x3000) == 2);
    CHECK(utf8_cell_width(0x3042) == 2);
    CHECK(utf8_cell_width(0x3099) == 0);     /* Mn and W: zero wins */
    CHECK(utf8_cell_width(0x302A) == 0);
    CHECK(utf8_cell_width(0x303F) == 1);
    CHECK(utf8_cell_width(0x31EF) == 2);     /* new in 15.1: all variants said 1 */
    CHECK(utf8_cell_width(0x3164) == 0);     /* Hangul filler: DI; variants said 2 */
    CHECK(utf8_cell_width(0x4DC0) == 1);
    CHECK(utf8_cell_width(0x4E2D) == 2);
    CHECK(utf8_cell_width(0xAC00) == 2);
    CHECK(utf8_cell_width(0xFE0F) == 0);
    CHECK(utf8_cell_width(0xFF21) == 2);
    CHECK(utf8_cell_width(0x1AFF4) == 1);    /* unassigned gap in Kana Ext-B: variants said 2 */
    CHECK(utf8_cell_width(0x1F1E6) == 2);    /* regional indicator: Emoji_Presentation; v3 asserted 1 */
    CHECK(utf8_cell_width(0x1F3FB) == 2);
    CHECK(utf8_cell_width(0x1F600) == 2);
    CHECK(utf8_cell_width(0x20000) == 2);
    CHECK(utf8_cell_width(0x2FFFE) == 1);
    CHECK(utf8_cell_width(0x2FFFD) == 2);        /* unassigned, EAW=W: tables, not a blanket 1 */
    CHECK(utf8_cell_width(0xE01F0) == 0);        /* unassigned, Default_Ignorable */
    CHECK(utf8_cell_width(0x3FFFD) == 2);
    CHECK(utf8_cell_width(0xE0001) == 0);
    CHECK(utf8_cell_width(0xE0100) == 0);
}

/* -------------------------------------------------------------- grapheme */
static size_t G(const char *s, size_t n)
{
    uint8_t *h = malloc(n ? n : 1);                  /* exact size for ASan */
    memcpy(h, s, n);
    size_t g = utf8_grapheme_next(h, n);
    free(h);
    return g;
}
#define GS(s) G(s, sizeof(s) - 1)

static void t_grapheme(void)
{
    CHECK(G("", 0) == 0);
    CHECK(GS("\xE2\x80\x8B\xCC\x81") == 3);                 /* GB4: ZWSP (Cf, Control) / Extend */
    CHECK(GS("\xD8\x80\xE2\x80\x8B") == 2);                 /* GB5: Prepend / ZWSP */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 9);  /* GB9c: KA VIRAMA KA */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8D\xE0\xA4\x95") == 12); /* KA VIRAMA ZWJ KA */
    CHECK(GS("\xE0\xA4\x95\xE0\xA4\x95") == 3);           /* no linker: break */
    CHECK(GS("a\xE0\xA5\x8D\xE0\xA4\x95") == 4);           /* linker without Consonant start: break */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D" "a") == 6);        /* virama extends, then break */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 15); /* chained */
    CHECK(GS("\xE2\x80\x8D\xE0\xA5\x8D\xE0\xA4\xA4") == 6);   /* cluster starting at ZWJ never opens a conjunct (fuzz find) */
    CHECK(GS("\xE0\xA5\x8D\xE0\xA4\x95") == 3);
    CHECK(GS("a") == 1);
    CHECK(GS("ab") == 1);
    CHECK(GS("e\xCC\x81x") == 3);                    /* e + combining acute */
    CHECK(GS("\xC3\xA9x") == 2);                     /* precomposed */
    CHECK(GS("e\xCC\x81\xCC\xA3x") == 5);            /* two marks */
    CHECK(GS("e\xCC") == 1);                         /* truncated mark is an invalid unit */
    CHECK(GS("e\xFF") == 1);
    CHECK(GS("\xFF" "a") == 1);
    CHECK(GS("\xFF\x80") == 1);
    CHECK(GS("\xFF\xCC\x81") == 1);                  /* invalid byte never takes extenders */
    CHECK(GS("\r\n") == 2);                          /* GB3 */
    CHECK(GS("\r\nx") == 2);
    CHECK(GS("\n\r") == 1);
    CHECK(GS("\ra") == 1);
    CHECK(GS("a\r") == 1);                           /* GB5 */
    CHECK(GS("\x01\xCC\x81") == 1);                  /* GB4: control takes no marks */
    CHECK(GS("\n\xCC\x81") == 1);
    CHECK(GS("a\xE2\x80\xA8") == 1);                 /* U+2028 is Control */
    CHECK(GS("a\xE2\x80\x8D") == 4);                 /* GB9: ZWJ extends any base */
    CHECK(GS("a\xE2\x80\x8D\xF0\x9F\x98\x80") == 4); /* GB11 needs ExtPict before ZWJ */
    CHECK(GS("a\xE2\x80\x8C") == 4);                 /* ZWNJ is Extend */
    CHECK(GS("\xE0\xA4\x95\xE0\xA4\xBE" "x") == 6);  /* GB9a: KA + AA (SpacingMark) */
    CHECK(GS("\xD8\x80\xD8\xA8x") == 4);             /* GB9b: Prepend U+0600 + BEH */
    CHECK(GS("\xD8\x80\n") == 2);                    /* GB5 beats GB9b */
    /* Hangul GB6-8 */
    CHECK(GS("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8x") == 9); /* L V T */
    CHECK(GS("\xE1\x84\x80\xE1\x84\x80") == 6);              /* L L */
    CHECK(GS("\xEA\xB0\x80\xE1\x86\xA8x") == 6);             /* LV T */
    CHECK(GS("\xEA\xB0\x80\xE1\x85\xA1x") == 6);             /* LV V */
    CHECK(GS("\xEA\xB0\x81\xE1\x86\xA8x") == 6);             /* LVT T */
    CHECK(GS("\xEA\xB0\x81\xE1\x85\xA1") == 3);              /* LVT / V */
    CHECK(GS("\xE1\x86\xA8\xE1\x85\xA1") == 3);              /* T / V */
    CHECK(GS("\xEA\xB0\x80\xEA\xB0\x80") == 3);              /* LV / LV */
    /* emoji */
    {
        const char fam[] = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7x";
        CHECK(GS(fam) == sizeof fam - 2);            /* man ZWJ woman ZWJ girl */
    }
    CHECK(GS("\xE2\x9D\xA4\xEF\xB8\x8Fx") == 6);     /* heart + VS16 */
    CHECK(GS("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBDx") == 8);  /* thumbs up + skin tone (Extend) */
    CHECK(GS("\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88x") == 14); /* rainbow flag */
    CHECK(GS("\xF0\x9F\x8F\x83\xE2\x80\x8D\xE2\x99\x80\xEF\xB8\x8Fx") == 13);     /* runner ZWJ female sign VS16 (v2 split it) */
    CHECK(GS("\xF0\x9F\x98\x80\xE2\x80\x8D") == 7);  /* dangling ZWJ */
    CHECK(GS("\xF0\x9F\x98\x80\xF0\x9F\x98\x80") == 4);   /* no ZWJ: break */
    /* regional indicators GB12/13 */
    CHECK(GS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7") == 8);
    CHECK(GS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB") == 8);
    CHECK(GS("\xF0\x9F\x87\xBA\xCC\x81\xF0\x9F\x87\xB8") == 6);  /* mark ends the RI run */
    CHECK(GS("\xF0\x9F\x87\xBA") == 4);
    /* ZWSP is Control in GCB (v2 joined it to the base) */
    CHECK(GS("a\xE2\x80\x8B") == 1);
}

/* Full byte-boundary vectors from the unmodified Unicode 15.1 conformance file
 * (vendor/ucd/GraphemeBreakTest.txt, 1187 cases). Runs from the repo root. */
static void t_grapheme_conformance(void)
{
    FILE *f = fopen("vendor/ucd/GraphemeBreakTest.txt", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[4096];
    size_t cases = 0, bad = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        uint8_t *bytes = malloc(1024);                  /* heap: ASan sees overreads of the string */
        uint8_t want[1025] = {0}, got[1025] = {0};
        size_t n = 0;
        int boundary = 0, have = 0;
        for (char *tok = strtok(line, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (strcmp(tok, "\xC3\xB7") == 0) { boundary = 1; if (!have) want[0] = 1; else want[n] = 1; }
            else if (strcmp(tok, "\xC3\x97") == 0) boundary = 0;
            else {
                char *end;
                unsigned long cp = strtoul(tok, &end, 16);
                CHECK(*end == '\0' && cp <= 0x10FFFF && n + 4 <= 1024);
                if (*end || cp > 0x10FFFF || n + 4 > 1024) break;
                want[n] = (uint8_t)boundary;
                n += utf8_encode((uint32_t)cp, bytes + n);
                have = 1;
            }
        }
        if (!have) { free(bytes); continue; }
        cases++;
        want[n] = 1;                                    /* GB2: break at end */
        want[0] = 1;                                    /* GB1 */
        uint8_t *exact = malloc(n);
        memcpy(exact, bytes, n);
        got[0] = 1;
        for (size_t off = 0; off < n;) {
            size_t step = utf8_grapheme_next(exact + off, n - off);
            CHECK(step > 0 && step <= n - off);
            if (!step || step > n - off) break;
            off += step;
            got[off] = 1;
        }
        if (memcmp(want, got, n + 1)) {
            if (bad < 8) printf("GraphemeBreakTest boundary mismatch at line %zu\n", lineno);
            bad++;
        }
        free(exact);
        free(bytes);
    }
    CHECK(!ferror(f));
    fclose(f);
    CHECK(cases == 1187);
    CHECK(bad == 0);
    printf("GraphemeBreakTest: %zu cases, %zu mismatches\n", cases, bad);
}

/* ------------------------------------------------------------ ascii_run */
static void t_ascii_run(void)
{
    for (size_t n = 0; n <= 80; n++) {
        uint8_t *h = malloc(n ? n : 1);
        memset(h, 'a', n);
        CHECK_BREAK(utf8_ascii_run(h, n) == n);
        for (size_t k = 0; k < n; k++) {
            h[k] = 0x80 | (uint8_t)k;
            size_t r = utf8_ascii_run(h, n);
            h[k] = 'a';
            if (r != k) { CHECK(r == k); break; }
        }
        for (size_t s = 1; s < 8 && s <= n; s++) CHECK(utf8_ascii_run(h + s, n - s) == n - s); /* misaligned */
        free(h);
    }
}

/* -------------------------------------------- random buffers (union of variant generators) */
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void t_random(void)
{
    enum { CAP = 1 << 16 };
    uint8_t *buf = malloc(CAP);
    size_t *bounds = malloc((CAP + 1) * sizeof *bounds);
    for (int round = 0; round < 240; round++) {
        size_t n = 1 + rnd() % CAP;
        int gen = round % 4;
        for (size_t i = 0; i < n; i++) {
            uint32_t r = rnd() % 8;
            if (gen == 0)       /* v1 mix: ASCII / continuation / 2-, 3-, 4-byte leads */
                buf[i] = r < 3 ? (uint8_t)(rnd() & 0x7F) : r < 5 ? (uint8_t)(0x80 | (rnd() & 63)) : r == 5 ? (uint8_t)(0xC0 | (rnd() & 63))
                       : r == 6 ? (uint8_t)(0xE0 | (rnd() & 31)) : (uint8_t)(0xF0 | (rnd() & 15));
            else if (gen == 1)  /* v2/v3 mix */
                buf[i] = r < 3 ? (uint8_t)(rnd() & 0x7F) : r < 6 ? (uint8_t)(0x80 + rnd() % 0x80) : (uint8_t)(0xC0 + rnd() % 0x40);
            else
                buf[i] = (uint8_t)rnd();
        }
        if (gen == 3) { size_t i = 0; while (i + 4 <= n) { uint32_t c = rnd() % 0x110000; i += utf8_encode(c, buf + i); if (rnd() % 16 == 0) buf[i - 1] ^= 0x40; } }
        uint8_t *h = malloc(n);                      /* exact-size copy for ASan */
        memcpy(h, buf, n);
        size_t off = 0, nb = 0;
        while (off < n) {
            utf8_step s = utf8_decode(h + off, n - off);
            if (!(s.len >= 1 && s.len <= 4 && off + s.len <= n)) { CHECK(0); break; }
            bounds[nb++] = off;
            off += s.len;
        }
        bounds[nb] = n;
        CHECK(off == n);
        size_t o = n;
        for (size_t k = nb; k > 0; k--) { o = utf8_prev(h, o); if (o != bounds[k - 1]) { CHECK(o == bounds[k - 1]); break; } }
        /* grapheme boundaries advance, never overrun, and land on unit boundaries */
        size_t bi = 0;
        for (off = 0; off < n;) {
            size_t g = utf8_grapheme_next(h + off, n - off);
            if (g == 0 || off + g > n) { CHECK(g >= 1 && off + g <= n); break; }
            off += g;
            while (bi < nb && bounds[bi] < off) bi++;
            if (bounds[bi] != off) { CHECK(bounds[bi] == off); break; }
        }
        /* ascii_run agrees with a byte loop at a few starts */
        for (int k = 0; k < 8; k++) {
            size_t st = rnd() % n, r = st;
            while (r < n && h[r] < 0x80) r++;
            CHECK(utf8_ascii_run(h + st, n - st) == r - st);
        }
        free(h);
    }
    free(bounds);
    free(buf);
}


/* ---- grapheme_prev, cluster width, property oracle ---- */
static void t_grapheme_prev_cases(void)
{
    uint8_t *h = malloc(32);
    CHECK(utf8_grapheme_prev(h, 0) == 0);
    memcpy(h, "ab", 2);
    CHECK(utf8_grapheme_prev(h, 2) == 1);
    memcpy(h, "e\xCC\x81", 3);
    CHECK(utf8_grapheme_prev(h, 3) == 0);
    CHECK(utf8_grapheme_prev(h, 1) == 0);
    memcpy(h, "\r\n", 2);
    CHECK(utf8_grapheme_prev(h, 2) == 0);
    memcpy(h, "\n\n", 2);
    CHECK(utf8_grapheme_prev(h, 2) == 1);
    memcpy(h, "a\xE2\x80\x8B\xCC\x81", 6);                /* ZWSP / Extend */
    CHECK(utf8_grapheme_prev(h, 6) == 4);
    CHECK(utf8_grapheme_prev(h, 4) == 1);
    memcpy(h, "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95", 9);  /* GB9c: one cluster */
    CHECK(utf8_grapheme_prev(h, 9) == 0);
    memcpy(h, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB", 12); /* RI RI | RI */
    CHECK(utf8_grapheme_prev(h, 12) == 8);
    CHECK(utf8_grapheme_prev(h, 8) == 0);
    memcpy(h, "x\xFF\xCC\x81", 4);                       /* invalid byte then orphan mark */
    CHECK(utf8_grapheme_prev(h, 4) == 2);                  /* U+0301 after an invalid byte: GB4 break */
    CHECK(utf8_grapheme_prev(h, 2) == 1);
    free(h);
}

static int cw(const char *s, size_t n)
{
    uint8_t *h = malloc(n ? n : 1);
    memcpy(h, s, n);
    int w = utf8_cluster_width(h, n);
    free(h);
    return w;
}
#define CWS(s) cw(s, sizeof(s) - 1)

static void t_cluster_width(void)
{
    CHECK(cw("", 0) == 0);
    CHECK(CWS("a") == 1);
    CHECK(CWS("e\xCC\x81") == 1);                          /* base + mark */
    CHECK(CWS("\xCC\x81") == 0);                           /* lone mark */
    CHECK(CWS("\xD8\x80\xD8\xA8") == 1);                   /* Prepend U+0600 + BEH: not 0 */
    CHECK(CWS("\xD8\x80") == 0);
    CHECK(CWS("\xE0\xB5\x8E") == 1);                       /* U+0D4E Prepend, visible */
    CHECK(CWS("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == 2); /* L V T: 2, not 3 */
    CHECK(CWS("\xE1\x84\x80") == 2);
    CHECK(CWS("\xEA\xB0\x80") == 2);                       /* LV (EAW W anyway) */
    CHECK(CWS("\xE1\x85\xA1\xE1\x86\xA8") == 2);           /* orphan V T */
    CHECK(CWS("\xE1\x85\x9F") == 0);                       /* U+115F alone: zero width wins */
    CHECK(CWS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 1); /* Devanagari conjunct */
    CHECK(CWS("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7") == 2); /* family, not 6 */
    CHECK(CWS("\xE2\x9D\xA4\xE2\x80\x8D\xE2\x9D\xA4") == 2);  /* narrow pict ZWJ narrow pict */
    CHECK(CWS("\xE2\x9D\xA4") == 1);
    CHECK(CWS("\xE2\x9D\xA4\xEF\xB8\x8F") == 2);           /* heart + VS16 */
    CHECK(CWS("\xE2\x9D\xA4\xEF\xB8\x8E") == 1);           /* heart + VS15 */
    CHECK(CWS("1\xEF\xB8\x8F\xE2\x83\xA3") == 2);          /* keycap */
    CHECK(CWS("#\xE2\x83\xA3") == 2);
    CHECK(CWS("1") == 1);
    CHECK(CWS("a\xEF\xB8\x8F") == 1);                      /* VS16 does not widen a letter */
    CHECK(CWS("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == 2);   /* thumbs up + skin tone */
    CHECK(CWS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8") == 2);   /* flag */
    CHECK(CWS("\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88") == 2); /* rainbow flag */
    CHECK(CWS("\xE4\xB8\x80\xCC\x81") == 2);               /* wide + mark */
    CHECK(CWS("\xFF") == 1);                               /* invalid byte: 1 (not AD-zero) */
    CHECK(CWS("\xAD") == 1);
    CHECK(CWS("\xC2\xAD") == 1);                           /* U+00AD is a Control cluster: placeholder */
    CHECK(CWS("\r\n") == 1);
    CHECK(CWS("\t") == 1);
    CHECK(CWS("\xE2\x80\x8B") == 1);                       /* ZWSP (Control) */
    /* cluster length agrees with grapheme_next, ASCII fast path with the slow path */
    uint8_t *h = malloc(8);
    for (uint32_t a = 0; a < 0x80; a++)
        for (uint32_t b = 0; b < 0x80; b++) {
            h[0] = (uint8_t)a; h[1] = (uint8_t)b;
            int w;
            size_t l = utf8_cluster(h, 2, &w);
            CHECK_BREAK(l == utf8_grapheme_next(h, 2) && w >= 0 && w <= 2 && (l == 2) == (a == '\r' && b == '\n'));
        }
    free(h);
}

/* Random property soup against the oracle: segmentation, prev, cluster width bounds. */
static void t_oracle_random(void)
{
    static const uint32_t pool[] = { 'a', 'b', ' ', 0x7F, 0xA0, 0xAD, 0xE9, 0x301, 0x308, 0x600, 0x605, 0x903, 0x915, 0x939,
        0x94D, 0x9CD, 0x995, 0xACD, 0x200B, 0x200C, 0x200D, 0x2028, 0x2764, 0xFE0F, 0x20E3, 0x1100, 0x1160, 0x11A8, 0xAC00,
        0xAC01, 0x1F600, 0x1F3FD, 0x1F468, 0x1F1E6, 0x1F1E7, 0x1F1E8, 0xE0020, 0xE007F, 0x4E00, 0xD4E, '\r', '\n', '\t', '1' };
    for (int round = 0; round < 20000; round++) {
        uint8_t *buf = malloc(160);
        int c[48], ib[48];
        size_t pos[49];
        size_t n = 0;
        int m = 0, units = 1 + (int)(rnd() % 40);
        for (int i = 0; i < units; i++) {
            if (rnd() % 12 == 0)
                buf[n++] = (uint8_t)(0x80 | rnd());        /* stray continuation or lead byte */
            else
                n += utf8_encode(pool[rnd() % NELEM(pool)], buf + n);
        }
        for (size_t o = 0; o < n && m < 48;) {             /* units as the decoder sees them */
            utf8_step s = utf8_decode(buf + o, n - o);
            pos[m] = o;
            c[m] = ref_class(s.valid, s.cp);
            ib[m] = ref_incb(s.valid, s.cp);
            o += s.len;
            m++;
        }
        pos[m] = n;
        uint8_t *exact = malloc(n);
        memcpy(exact, buf, n);
        int i = 0;
        for (size_t off = 0; off < n;) {
            size_t g = utf8_grapheme_next(exact + off, n - off);
            int j = i + 1;                                  /* ref: extend until the next break */
            while (j < m && !ref_break(c, ib, j)) j++;
            CHECK_BREAK(g == pos[j] - off);
            CHECK_BREAK(utf8_grapheme_prev(exact, off + g) == off);
            int w;
            CHECK_BREAK(utf8_cluster(exact + off, n - off, &w) == g && w >= 0 && w <= 2);
            for (int k = i + 1; k < j; k++)                 /* unit ends inside the cluster: its start */
                CHECK_BREAK(utf8_grapheme_prev(exact, pos[k]) == off);
            off += g;
            i = j;
        }
        free(exact);
        free(buf);
    }
}

/* Fast paths in utf8.c rely on these table facts; fail loudly if a table
 * regeneration breaks them. */
static void t_table_facts(void)
{
    CHECK(UCD_ZERO_WIDTH[0].lo == 0xAD && UCD_ZERO_WIDTH[0].hi == 0xAD && UCD_ZERO_WIDTH[1].lo >= 0x300);
    CHECK(UCD_WIDE[0].lo >= 0x1100);
    CHECK(UCD_EXTEND[0].lo >= 0x300 && UCD_SPACINGMARK[0].lo >= 0x300 && UCD_PREPEND[0].lo >= 0x300);
    CHECK(UCD_CONTROL[0].lo == 0 && UCD_INCB_CONSONANT[0].lo >= 0x900 && UCD_INCB_LINKER[0].lo >= 0x900);
    for (uint32_t cp = 0; cp < 0x300; cp++) {              /* gcb_of fast path below U+0300 */
        int ctl = cp == '\r' || cp == '\n' ? 0 : (ref_in(UCD_CONTROL, NELEM(UCD_CONTROL), cp));
        CHECK_BREAK(ctl == (cp < 0x20 && cp != '\r' && cp != '\n') + (cp >= 0x7F && cp <= 0x9F) + (cp == 0xAD));
        CHECK_BREAK(!ref_in(UCD_EXTEND, NELEM(UCD_EXTEND), cp) && !ref_in(UCD_SPACINGMARK, NELEM(UCD_SPACINGMARK), cp));
        CHECK_BREAK(!ref_in(UCD_PREPEND, NELEM(UCD_PREPEND), cp));
    }
    CHECK(UCD_EXT_PICT[0].lo >= 0x80 && UCD_HANGUL_L[0].lo >= 0x300 && UCD_REGIONAL_INDICATOR[0].lo >= 0x300);
}

int main(void)
{
    tail4 = malloc(TAIL);
    t_table_facts();
    t_decode_cases();
    t_decode_exhaustive();
    t_all_scalars();
    t_width();
    t_grapheme();
    t_grapheme_conformance();
    t_grapheme_prev_cases();
    t_cluster_width();
    t_oracle_random();
    t_ascii_run();
    t_random();
    free(tail4);
    printf(fails ? "utf8_test: %d FAILURES\n" : "utf8_test: all passed\n", fails);
    return fails != 0;
}
