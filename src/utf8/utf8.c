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

/* Indic_Conjunct_Break (GB9c), only the states the rule distinguishes. */
enum { INCB_NONE, INCB_CONSONANT, INCB_LINKER, INCB_EXTEND };

/* Grapheme_Cluster_Break plus Extended_Pictographic, from the generated
 * tables. Below U+0300 the classes are fixed (tests/utf8_test.c t_table_facts
 * checks it against the tables): CR, LF, Control = Cc + U+00AD, else Other. */
static enum gcb gcb_of(uint32_t cp)
{
    if (cp < 0x300) {
        if (cp >= 0x7F)
            return cp <= 0x9F || cp == 0xAD ? G_CONTROL : G_OTHER;
        return cp >= 0x20 ? G_OTHER : cp == '\r' ? G_CR : cp == '\n' ? G_LF : G_CONTROL;
    }
    if (cp == 0x200D)
        return G_ZWJ;
    if (IN(UCD_EXTEND, cp))
        return G_EXTEND;
    if (IN(UCD_SPACINGMARK, cp))
        return G_SPACING;
    if (IN(UCD_PREPEND, cp))
        return G_PREPEND;
    if (IN(UCD_CONTROL, cp))
        return G_CONTROL;
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

/* Explicit segmentation state: everything the rules remember about the
 * scalars already inside the cluster. Plain data, so a caller can suspend
 * and resume a cluster scan (P1.1d). */
typedef struct {
    uint8_t prev;       /* enum gcb of the last scalar */
    uint8_t pict;       /* inside ExtPict Extend* (GB11 left side) */
    uint8_t pict_zwj;   /* last scalar is a ZWJ that followed ExtPict Extend* */
    uint8_t ri_odd;     /* odd number of RI ending at the last scalar */
    uint8_t incb;       /* 0: no open conjunct; 1: Consonant [Extend Linker]*; 2: ... Linker ... */
} gstate;

static int incb_of(uint32_t cp, enum gcb g, const gstate *st)
{
    if (g == G_OTHER)                   /* lowest Consonant is U+0915; InCB=Extend reaches down to U+0300 */
        return cp >= 0x900 && IN(UCD_INCB_CONSONANT, cp) ? INCB_CONSONANT : INCB_NONE;
    if (st->incb && (g == G_EXTEND || g == G_ZWJ)) {
        if (IN(UCD_INCB_LINKER, cp))
            return INCB_LINKER;
        if (g == G_ZWJ || IN(UCD_INCB_EXTEND, cp))
            return INCB_EXTEND;
    }
    return INCB_NONE;
}

static void gstate_start(gstate *st, uint32_t cp)
{
    enum gcb g = gcb_of(cp);
    st->incb = 0;                       /* incb_of reads it: a cluster never starts inside a conjunct */
    st->prev = (uint8_t)g;
    st->pict = g == G_PICT;
    st->pict_zwj = 0;
    st->ri_odd = g == G_RI;
    st->incb = incb_of(cp, g, st) == INCB_CONSONANT;
}

/* Is there no break between the state's last scalar and cp (GB3..GB13)? If
 * so the state absorbs cp. If not, the state is left untouched. */
static int gstate_join(gstate *st, uint32_t cp)
{
    enum gcb prev = (enum gcb)st->prev, cur = gcb_of(cp);
    int ci = incb_of(cp, cur, st), join;
    if (prev == G_CR && cur == G_LF)                                 join = 1; /* GB3 */
    else if (prev == G_CR || prev == G_LF || prev == G_CONTROL)      join = 0; /* GB4 */
    else if (cur == G_CR || cur == G_LF || cur == G_CONTROL)         join = 0; /* GB5 */
    else if (prev == G_L && (cur == G_L || cur == G_V || cur == G_LV || cur == G_LVT)) join = 1; /* GB6 */
    else if ((prev == G_LV || prev == G_V) && (cur == G_V || cur == G_T)) join = 1; /* GB7 */
    else if ((prev == G_LVT || prev == G_T) && cur == G_T)           join = 1; /* GB8 */
    else if (cur == G_EXTEND || cur == G_ZWJ || cur == G_SPACING)    join = 1; /* GB9, GB9a */
    else if (prev == G_PREPEND)                                      join = 1; /* GB9b */
    else if (st->incb == 2 && ci == INCB_CONSONANT)                  join = 1; /* GB9c */
    else if (st->pict_zwj && cur == G_PICT)                          join = 1; /* GB11 */
    else if (st->ri_odd && cur == G_RI)                              join = 1; /* GB12, GB13 */
    else                                                             join = 0; /* GB999 */
    if (!join)
        return 0;
    st->pict_zwj = cur == G_ZWJ && st->pict;
    if (cur == G_PICT)
        st->pict = 1;
    else if (cur != G_EXTEND)
        st->pict = 0;
    st->ri_odd = cur == G_RI && !st->ri_odd;
    if (ci == INCB_CONSONANT)
        st->incb = 1;
    else if (st->incb && ci == INCB_LINKER)
        st->incb = 2;
    else if (!(st->incb && ci == INCB_EXTEND))
        st->incb = 0;
    st->prev = (uint8_t)cur;
    return 1;
}

size_t utf8_grapheme_next(const uint8_t *p, size_t n)
{
    if (n == 0)
        return 0;
    utf8_step s = utf8_decode(p, n);
    if (!s.valid)
        return 1;                                   /* invalid byte: own cluster */
    gstate st;
    gstate_start(&st, s.cp);
    size_t i = s.len;
    while (i < n) {
        utf8_step t = utf8_decode(p + i, n - i);
        if (!t.valid || !gstate_join(&st, t.cp))    /* invalid: Control (GB5) */
            break;
        i += t.len;
    }
    return i;
}

/* A break before the unit at q is certain whatever precedes it: the unit is
 * invalid (Control by definition) or a CR or Control (GB5); an LF after a
 * non-CR qualifies too. Forward segmentation can start there. */
static int safe_start(const uint8_t *base, size_t q, size_t end)
{
    utf8_step s = utf8_decode(base + q, end - q);
    if (!s.valid)
        return 1;
    enum gcb g = gcb_of(s.cp);
    if (g == G_CR || g == G_CONTROL)
        return 1;
    return g == G_LF && (q == 0 || base[q - 1] != '\r');
}

size_t utf8_grapheme_prev(const uint8_t *base, size_t off)
{
    if (off == 0)
        return 0;
    size_t q = utf8_prev(base, off);
    while (q > 0 && !safe_start(base, q, off))
        q = utf8_prev(base, q);
    size_t start = q;                               /* cluster containing byte off-1 */
    while (q < off) {
        start = q;
        q += utf8_grapheme_next(base + q, off - q);
    }
    return start;
}

/* ----------------------------------------------------------- cluster width */

static int cluster_width_slow(const uint8_t *p, size_t len)
{
    utf8_step s = utf8_decode(p, len);
    if (!s.valid)
        return 1;
    enum gcb g0 = gcb_of(s.cp);
    if (g0 == G_CR || g0 == G_LF || g0 == G_CONTROL)
        return 1;
    int w = 0, vs16 = 0, keycap = 0, zwj_seq = 0, prev_zwj = 0;
    uint32_t base = 0;
    enum gcb gb = G_OTHER;
    for (size_t i = 0; i < len; i += s.len) {
        s = utf8_decode(p + i, len - i);
        enum gcb g = gcb_of(s.cp);
        if (!w) {
            int sw = utf8_cell_width(s.cp);
            if (sw) {
                w = sw;
                base = s.cp;
                gb = g;
            }
        } else {
            vs16 |= s.cp == 0xFE0F;
            keycap |= s.cp == 0x20E3;
            zwj_seq |= prev_zwj && g == G_PICT;
        }
        prev_zwj = g == G_ZWJ;
    }
    if (!w)
        return 0;
    if (gb == G_L || gb == G_V || gb == G_T || gb == G_LV || gb == G_LVT)
        return 2;                                   /* a Hangul syllable block is two cells */
    if (w == 1) {
        int pict = gcb_of(base) == G_PICT;
        int kbase = base == '#' || base == '*' || (base >= '0' && base <= '9');
        if ((pict && (vs16 || zwj_seq)) || (kbase && keycap))
            return 2;
    }
    return w;
}

size_t utf8_cluster(const uint8_t *p, size_t n, int *width)
{
    if (n == 0) {
        *width = 0;
        return 0;
    }
    if (p[0] >= 0x20 && p[0] < 0x7F && (n == 1 || p[1] < 0x80)) {
        *width = 1;                                 /* printable ASCII before ASCII: always a break */
        return 1;
    }
    size_t len = utf8_grapheme_next(p, n);
    *width = cluster_width_slow(p, len);
    return len;
}

int utf8_cluster_width(const uint8_t *p, size_t n)
{
    int w;
    (void)utf8_cluster(p, n, &w);
    return w;
}
