/* utf8 (P1.1b best-of). Decoder (inline in utf8.h): variant 3's Table 3-7
 * range checks (shared with variant 1). prev: variant 3's bounded walk-back.
 * Bulk skip: variant 2's utf8_ascii_run, widened to 32 bytes/step. Width and grapheme: rewritten over
 * the generated UCD 15.1 tables (utf8/tables.h); no hand tables.
 * Rationale and numbers: docs/decisions/P1.1b.md. */
#include "utf8/utf8.h"
#include "utf8/tables.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------ prev, encode */

/* A valid unit starts at a non-continuation byte, which no earlier unit can
 * absorb (valid units contain only continuations after the lead; invalid
 * units are one byte). So the unit ending at off is either the valid
 * sequence whose lead is the nearest non-continuation within 4 bytes and
 * whose decode ends exactly at off, or the single byte off-1. */
size_t utf8_prev(const uint8_t *base, size_t off)
{
    if (off == 0)
        return 0;
    size_t j = off - 1;
    if (base[j] < 0x80)
        return j;
    size_t lim = off > 4 ? off - 4 : 0;
    while (j > lim && (base[j] & 0xC0) == 0x80)
        j--;
    if (j < off - 1) {
        utf8_step s = utf8_decode(base + j, off - j);
        if (s.valid && (size_t)s.len == off - j)
            return j;
    }
    return off - 1;
}

size_t utf8_encode(uint32_t cp, uint8_t out[4])
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | (cp >> 18));
    out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

/* ----------------------------------------------------------------- width */

/* Sorted, disjoint ranges (tests/utf8_tables_test.c). Out-of-span code points
 * are rejected first; then a branchless lower-bound search (cmov). Used for
 * the grapheme property tables; 4-ary and 8-ary variants measured slower. */
static inline int in_tab(const utf8_range *t, size_t n, uint32_t cp)
{
    if (cp < t[0].lo || cp > t[n - 1].hi)
        return 0;
    const utf8_range *b = t;
    while (n > 1) {                 /* last range with lo <= cp */
        size_t half = n / 2;
        b = b[half].lo <= cp ? b + half : b;
        n -= half;
    }
    return cp <= b->hi;
}
#define IN(tab, cp) in_tab(tab, NELEM(tab), cp)

/* Width lookups are the hot table path (CJK and emoji text hit both tables
 * for every scalar), so they use a decision tree instead of a search loop:
 * treeK splits [lo, lo+n) at lo+n/2 and recurses into tree(K-1). Called with
 * a static const table and constant lo/n, always_inline unrolls it fully and
 * the compiler folds every t[i].lo into an immediate (checked: gcc 13 and
 * clang 18 emit no table loads), leaving a chain of compare-and-branch that
 * predicts perfectly within a script run. ~2.5k instructions for both
 * tables; 3x faster than the branchless search on CJK runs (decision doc). */
#define UTF8_AI static inline __attribute__((always_inline))
UTF8_AI int tree0(const utf8_range *t, size_t lo, size_t n, uint32_t cp)
{
    (void)n;
    return cp <= t[lo].hi;
}
#define UTF8_TREE_LEVEL(k, j)                                               \
    UTF8_AI int tree##k(const utf8_range *t, size_t lo, size_t n, uint32_t cp) \
    {                                                                       \
        if (n > 1 && cp >= t[lo + n / 2].lo)                                \
            return tree##j(t, lo + n / 2, n - n / 2, cp);                   \
        return tree##j(t, lo, n / 2, cp);                                   \
    }
UTF8_TREE_LEVEL(1, 0)
UTF8_TREE_LEVEL(2, 1)
UTF8_TREE_LEVEL(3, 2)
UTF8_TREE_LEVEL(4, 3)
UTF8_TREE_LEVEL(5, 4)
UTF8_TREE_LEVEL(6, 5)
UTF8_TREE_LEVEL(7, 6)
UTF8_TREE_LEVEL(8, 7)
UTF8_TREE_LEVEL(9, 8)
UTF8_TREE_LEVEL(10, 9)
_Static_assert(NELEM(UCD_ZERO_WIDTH) <= 1024 && NELEM(UCD_WIDE) <= 1024, "tree10 covers at most 1024 ranges");
#define IN_TREE(tab, cp) ((cp) >= (tab)[0].lo && (cp) <= (tab)[NELEM(tab) - 1].hi && tree10(tab, 0, NELEM(tab), cp))

int utf8_cell_width_table(uint32_t cp)
{
    if (cp < 0x300)                 /* direct callers: keep the contract */
        return cp != 0xAD;
    if (IN_TREE(UCD_ZERO_WIDTH, cp)) /* zero wins: U+115F, 302A..302F, 3099.. are in both */
        return 0;
    if (IN_TREE(UCD_WIDE, cp))
        return 2;
    return 1;
}

/* -------------------------------------------------------------- grapheme */

enum gcb {
    G_OTHER, G_CR, G_LF, G_CONTROL, G_EXTEND, G_ZWJ, G_SPACING, G_PREPEND,
    G_RI, G_L, G_V, G_T, G_LV, G_LVT, G_PICT,
};

/* Grapheme_Cluster_Break plus Extended_Pictographic. Control = Cc + Zl + Zp
 * (stable sets, not a table); Cf code points that UAX #29 also calls Control
 * fall through to Other here (tables.h has no Control table yet). */
static enum gcb gcb_of(uint32_t cp)
{
    if (cp < 0x7F)
        return cp >= 0x20 ? G_OTHER : cp == '\r' ? G_CR : cp == '\n' ? G_LF : G_CONTROL;
    if (cp <= 0x9F || cp == 0x2028 || cp == 0x2029)
        return G_CONTROL;
    if (cp == 0x200D)
        return G_ZWJ;
    if (IN(UCD_EXTEND, cp))
        return G_EXTEND;
    if (IN(UCD_SPACINGMARK, cp))
        return G_SPACING;
    if (IN(UCD_PREPEND, cp))
        return G_PREPEND;
    if (IN(UCD_REGIONAL_INDICATOR, cp))
        return G_RI;
    if (IN(UCD_HANGUL_LV, cp))
        return G_LV;
    if (IN(UCD_HANGUL_LVT, cp))
        return G_LVT;
    if (IN(UCD_HANGUL_L, cp))
        return G_L;
    if (IN(UCD_HANGUL_V, cp))
        return G_V;
    if (IN(UCD_HANGUL_T, cp))
        return G_T;
    if (IN(UCD_EXT_PICT, cp))
        return G_PICT;
    return G_OTHER;
}

size_t utf8_grapheme_next(const uint8_t *p, size_t n)
{
    if (n == 0)
        return 0;
    utf8_step s = utf8_decode(p, n);
    if (!s.valid)
        return 1;                                   /* invalid byte: own cluster */
    enum gcb prev = gcb_of(s.cp);
    int pict = prev == G_PICT;                      /* inside ExtPict Extend* */
    int pict_zwj = 0;                               /* prev is a ZWJ that followed ExtPict Extend* */
    int ri_odd = prev == G_RI;                      /* odd count of RI ending at prev */
    size_t i = s.len;
    while (i < n) {
        utf8_step t = utf8_decode(p + i, n - i);
        if (!t.valid)
            break;                                  /* treated as Control (GB5) */
        enum gcb cur = gcb_of(t.cp);
        int join;
        if (prev == G_CR && cur == G_LF)                                 join = 1; /* GB3 */
        else if (prev == G_CR || prev == G_LF || prev == G_CONTROL)      join = 0; /* GB4 */
        else if (cur == G_CR || cur == G_LF || cur == G_CONTROL)         join = 0; /* GB5 */
        else if (prev == G_L && (cur == G_L || cur == G_V || cur == G_LV || cur == G_LVT)) join = 1; /* GB6 */
        else if ((prev == G_LV || prev == G_V) && (cur == G_V || cur == G_T)) join = 1; /* GB7 */
        else if ((prev == G_LVT || prev == G_T) && cur == G_T)           join = 1; /* GB8 */
        else if (cur == G_EXTEND || cur == G_ZWJ || cur == G_SPACING)    join = 1; /* GB9, GB9a */
        else if (prev == G_PREPEND)                                      join = 1; /* GB9b */
        else if (pict_zwj && cur == G_PICT)                              join = 1; /* GB11 */
        else if (ri_odd && cur == G_RI)                                  join = 1; /* GB12, GB13 */
        else                                                             join = 0; /* GB999 */
        if (!join)
            break;
        pict_zwj = cur == G_ZWJ && pict;
        if (cur == G_PICT)
            pict = 1;
        else if (cur != G_EXTEND)
            pict = 0;
        ri_odd = cur == G_RI && !ri_odd;
        prev = cur;
        i += t.len;
    }
    return i;
}
