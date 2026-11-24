/* utf8: decoder (inline in utf8.h, Unicode Table 3-7 range checks), bounded
 * prev walk-back, width and UAX #29 segmentation over the generated UCD 15.1
 * tables (utf8/tables.h); no hand tables. Harvested from v1 6fec2e8; what
 * changed and why: docs/decisions/v2-0fv.11.md. */
#include "utf8/utf8.h"
#include "utf8/tables.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------ prev, encode */

/* A valid unit starts at a non-continuation byte, which no earlier unit can
 * absorb (valid units contain only continuations after the lead; invalid
 * units are one byte). So the unit ending at off is either the valid
 * sequence whose lead is the nearest non-continuation within 4 bytes and
 * whose decode ends exactly at off, or the single byte off-1. */
static size_t utf8_prev(const uint8_t *base, size_t off)
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

/* Sorted, disjoint, non-empty ranges (tests/utf8_tables_test.c). Out-of-span
 * code points are rejected first; then a branchless lower-bound search (cmov).
 * Used for the grapheme property tables. */
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
 * the compiler folds every t[i].lo into an immediate, leaving a chain of
 * compare-and-branch that predicts well within a script run. v1 measured it
 * 3x faster than the branchless search on CJK runs (M, v1
 * docs/decisions/P1.1b.md); bench/utf8_bench.c tracks it in v2. */
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
_Static_assert(NELEM(UCD_ZERO_WIDTH) <= 1024 && NELEM(UCD_WIDE) <= 1024, "tree10 covers at most 1024 ranges (P: 2^10)");
#define IN_TREE(tab, cp) ((cp) >= (tab)[0].lo && (cp) <= (tab)[NELEM(tab) - 1].hi && tree10(tab, 0, NELEM(tab), cp))

static int utf8_cell_width_table(uint32_t cp)
{
    if (cp < 0x300)                 /* direct callers: keep the contract */
        return cp != 0xAD;
    /* Unshaped editor presentation: a skin tone modifies a base and never
     * owns a cell, including when typed without a base. */
    if (cp >= 0x1f3fbu && cp <= 0x1f3ffu)
        return 0;
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

/* Indic_Conjunct_Break of one scalar (GB9c), only the values the rule
 * distinguishes, and only when the left context makes it matter. */
enum { INCB_NONE, INCB_CONSONANT, INCB_LINKER, INCB_EXTEND };

/* GB11 left context: nothing, ExtPict Extend*, or ExtPict Extend* ZWJ. */
enum gb11 { GB11_NONE, GB11_PICT, GB11_ZWJ };

/* GB9c left context: nothing, Consonant [Extend]*, or Consonant
 * [Extend|Linker]* with at least one Linker. */
enum gb9c { GB9C_NONE, GB9C_CONSONANT, GB9C_LINKED };

/* Grapheme_Cluster_Break plus Extended_Pictographic, from the generated
 * tables. Below U+0300 the classes are fixed, so Latin text skips every
 * table: CR, LF, Control = Cc + U+00AD, Extended_Pictographic = © U+00A9 and
 * ® U+00AE (UCD_EXT_PICT[0] and [1], its only entries below U+0300), else
 * Other. tests/utf8_test.c t_table_facts pins those table facts, so a Unicode
 * version that adds a third sub-U+0300 ExtPict fails there; t_class_audit
 * checks this function against the tables for every code point. */
static enum gcb gcb_of(uint32_t cp)
{
    if (cp < 0x300) {
        if (cp >= 0x7F) {
            if (cp <= 0x9F || cp == 0xAD)
                return G_CONTROL;
            return cp == 0xA9 || cp == 0xAE ? G_PICT : G_OTHER;
        }
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

/* Everything the rules remember about the scalars already in the cluster.
 * Plain data, so a caller can suspend and resume a cluster scan. */
typedef utf8_gseg gstate;

static int incb_of(uint32_t cp, enum gcb g, const gstate *st)
{
    if (g == G_OTHER)                   /* lowest Consonant is U+0915; InCB=Extend reaches down to U+0300 */
        return cp >= 0x900 && IN(UCD_INCB_CONSONANT, cp) ? INCB_CONSONANT : INCB_NONE;
    if (st->incb != GB9C_NONE && (g == G_EXTEND || g == G_ZWJ)) {
        if (IN(UCD_INCB_LINKER, cp))
            return INCB_LINKER;
        if (g == G_ZWJ || IN(UCD_INCB_EXTEND, cp))
            return INCB_EXTEND;
    }
    return INCB_NONE;
}

static enum gb11 gb11_next(enum gb11 s, enum gcb cur)
{
    enum gb11 r = GB11_NONE;
    if (cur == G_PICT)
        return GB11_PICT;
    switch (s) {
    case GB11_PICT:
        r = cur == G_EXTEND ? GB11_PICT : cur == G_ZWJ ? GB11_ZWJ : GB11_NONE;
        break;
    case GB11_NONE:
    case GB11_ZWJ:
        break;
    }
    return r;
}

static enum gb9c gb9c_next(enum gb9c s, int ci)
{
    enum gb9c r = GB9C_NONE;
    if (ci == INCB_CONSONANT)
        return GB9C_CONSONANT;
    switch (s) {
    case GB9C_CONSONANT:
        r = ci == INCB_LINKER ? GB9C_LINKED : ci == INCB_EXTEND ? GB9C_CONSONANT : GB9C_NONE;
        break;
    case GB9C_LINKED:
        r = ci == INCB_LINKER || ci == INCB_EXTEND ? GB9C_LINKED : GB9C_NONE;
        break;
    case GB9C_NONE:
        break;
    }
    return r;
}

static void gstate_start(gstate *st, uint32_t cp)
{
    enum gcb g = gcb_of(cp);
    st->incb = GB9C_NONE;               /* incb_of reads it: a cluster never starts inside a conjunct */
    st->prev = (uint8_t)g;
    st->gb11 = (uint8_t)gb11_next(GB11_NONE, g);
    st->ri_odd = g == G_RI;
    st->incb = (uint8_t)gb9c_next(GB9C_NONE, incb_of(cp, g, st));   /* NONE or CONSONANT */
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
    else if (st->incb == GB9C_LINKED && ci == INCB_CONSONANT)        join = 1; /* GB9c */
    else if (st->gb11 == GB11_ZWJ && cur == G_PICT)                  join = 1; /* GB11 */
    else if (st->ri_odd && cur == G_RI)                              join = 1; /* GB12, GB13 */
    else                                                             join = 0; /* GB999 */
    if (!join)
        return 0;
    st->gb11 = (uint8_t)gb11_next((enum gb11)st->gb11, cur);
    st->ri_odd = cur == G_RI && !st->ri_odd;
    st->incb = (uint8_t)gb9c_next((enum gb9c)st->incb, ci);
    st->prev = (uint8_t)cur;
    return 1;
}

static size_t utf8_grapheme_next(const uint8_t *p, size_t n)
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

/* ---- resumable, budgeted segmentation ---- */

/* Is p[0..n), n >= 1, a proper prefix of a valid multi-byte sequence? Then the
 * bytes are not invalid, just not all here yet. */
static int partial_tail(const uint8_t *p, size_t n)
{
    uint8_t b = p[0];
    if (b < 0xC2 || b > 0xF4)
        return 0;
    size_t need = b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
    if (n >= need)
        return 0;
    if (n >= 2) {
        uint8_t lo = 0x80, hi = 0xBF;
        if (b == 0xE0) lo = 0xA0;
        else if (b == 0xED) hi = 0x9F;
        else if (b == 0xF0) lo = 0x90;
        else if (b == 0xF4) hi = 0x8F;
        if (p[1] < lo || p[1] > hi)
            return 0;
    }
    if (n >= 3 && (p[2] & 0xC0) != 0x80)
        return 0;
    return 1;
}

int utf8_grapheme_step(utf8_gseg *g, const uint8_t *p, size_t n, size_t budget, int eof, size_t *used)
{
    size_t i = 0;
    if (budget == 0)
        budget = 1;
    for (;;) {
        if (i >= n) {
            *used = i;
            if (!eof)
                return UTF8_G_MORE;
            utf8_gseg_init(g);
            return UTF8_G_END;
        }
        if (i >= budget) {
            *used = i;
            return UTF8_G_BUDGET;
        }
        utf8_step s = utf8_decode(p + i, n - i);
        if (!s.valid) {
            if (!eof && partial_tail(p + i, n - i)) {
                *used = i;
                return UTF8_G_MORE;
            }
            *used = i + !g->started;                /* fresh: a one-byte cluster; else GB5 break before it */
            utf8_gseg_init(g);
            return UTF8_G_END;
        }
        if (!g->started) {
            gstate_start(g, s.cp);
            g->started = 1;
        } else if (!gstate_join(g, s.cp)) {
            *used = i;
            utf8_gseg_init(g);
            return UTF8_G_END;
        }
        i += s.len;
    }
}

/* Below U+0300 the only zero-width scalar is soft hyphen (U+00AD). */
static inline int utf8_cell_width(uint32_t cp)
{
    if (cp < 0x300)
        return cp != 0xAD;
    return utf8_cell_width_table(cp);
}

/* ------------------------------------------------- emoji class, artwork */

int utf8_emoji_class(uint32_t cp)
{
    enum gcb g = gcb_of(cp);
    return g == G_PICT ? UTF8_EMOJI_PICT : g == G_RI ? UTF8_EMOJI_RI : 0;
}

/* utf8_artwork_state.phase */
enum art { ART_SEEK, ART_TEXT, ART_PICT };

int utf8_artwork_step(utf8_artwork_state *st, uint32_t cp)
{
    switch ((enum art)st->phase) {
    case ART_SEEK:
        if (utf8_cell_width(cp))
            st->phase = gcb_of(cp) == G_PICT ? ART_PICT : ART_TEXT;
        break;
    case ART_TEXT:
    case ART_PICT:
        break;
    }
    /* Without shaping a joined emoji is represented by its first emoji.
     * Segmentation and width still use the complete grapheme. */
    if (st->phase == ART_PICT && cp == 0x200du)
        return -1;
    if (cp == 0x200du || cp == 0xfe0eu || cp == 0xfe0fu || (cp >= 0x1f3fbu && cp <= 0x1f3ffu))
        return 0;
    return 1;
}

int utf8_cluster_emoji(const uint8_t *p, size_t len)
{
    for (size_t off = 0; off < len;) {
        utf8_step step = utf8_decode(p + off, len - off);
        if (step.valid && utf8_cell_width(step.cp))
            return utf8_emoji_class(step.cp);
        off += step.len;
    }
    return 0;
}

size_t utf8_cluster_artwork(const uint8_t *p, size_t len, uint8_t *out)
{
    size_t written = 0;
    utf8_artwork_state st = { ART_SEEK };
    for (size_t off = 0; off < len;) {
        utf8_step step = utf8_decode(p + off, len - off);
        if (step.valid) {
            int keep = utf8_artwork_step(&st, step.cp);
            if (keep < 0)
                break;
            if (!keep) {
                off += step.len;
                continue;
            }
        }
        /* out may be p: filtering only moves bytes towards the beginning. */
        memmove(out + written, p + off, step.len);
        written += step.len;
        off += step.len;
    }
    return written;
}

/* ----------------------------------------------------------- cluster width */

/* utf8_cseg.phase: the cluster-width automaton over the scalars of one
 * cluster, in order. ONE and TWO are final (the width is decided and the
 * rest of the cluster is only segmented). */
enum cw {
    CW_SEEK,        /* no scalar with nonzero width yet: width 0 if the cluster ends here */
    CW_PICT,        /* base is width-1 ExtPict, last scalar not ZWJ: waits for VS16 or ZWJ ExtPict */
    CW_PICT_ZWJ,    /* as CW_PICT, and the last scalar was ZWJ */
    CW_KEYCAP,      /* base is [0-9#*]: waits for U+20E3 */
    CW_ONE,         /* decided: 1 */
    CW_TWO,         /* decided: 2 */
};

static int cw_width(enum cw c)
{
    int w = 0;
    switch (c) {
    case CW_SEEK:
        break;
    case CW_PICT:
    case CW_PICT_ZWJ:
    case CW_KEYCAP:
    case CW_ONE:
        w = 1;
        break;
    case CW_TWO:
        w = 2;
        break;
    }
    return w;
}

/* Base class of the first nonzero-width scalar (sw = its width). */
static enum cw cw_base(uint32_t cp, enum gcb g, int sw)
{
    if (g == G_L || g == G_V || g == G_T || g == G_LV || g == G_LVT)
        return CW_TWO;                              /* a Hangul syllable block is two cells */
    if (sw == 2)
        return CW_TWO;
    if (g == G_PICT)
        return CW_PICT;
    if (cp == '#' || cp == '*' || (cp >= '0' && cp <= '9'))
        return CW_KEYCAP;
    return CW_ONE;
}

/* Fold the scalars of p[0..len) (whole units, segmented into one cluster, in
 * order and continuing the units already folded into c) into c->phase. A
 * cluster that holds an invalid byte, CR, LF or Control holds nothing else
 * (GB4, GB5; CR LF is the one pair and LF never follows a base), so meeting
 * one while seeking means it is the cluster's first unit: width 1. */
static void cseg_scan(utf8_cseg *c, const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len;) {
        utf8_step s = utf8_decode(p + i, len - i);
        i += s.len;
        enum gcb g = s.valid ? gcb_of(s.cp) : G_CONTROL;
        enum cw next = (enum cw)c->phase;
        switch ((enum cw)c->phase) {
        case CW_SEEK:
            if (g == G_CR || g == G_LF || g == G_CONTROL) {
                next = CW_ONE;
            } else {
                int sw = utf8_cell_width(s.cp);
                if (sw)
                    next = cw_base(s.cp, g, sw);
            }
            break;
        case CW_PICT:
        case CW_PICT_ZWJ:
            if (s.cp == 0xFE0F || (c->phase == CW_PICT_ZWJ && g == G_PICT))
                next = CW_TWO;
            else
                next = g == G_ZWJ ? CW_PICT_ZWJ : CW_PICT;
            break;
        case CW_KEYCAP:
            if (s.cp == 0x20E3)
                next = CW_TWO;
            break;
        case CW_ONE:
        case CW_TWO:
            return;
        }
        c->phase = (uint8_t)next;
    }
}

int utf8_cluster_step(utf8_cseg *c, const uint8_t *p, size_t n, size_t budget, int eof, size_t *used, int *width)
{
    if (!c->seg.started && n >= 2 && p[0] >= 0x20 && p[0] < 0x7F && p[1] < 0x80) {
        *used = 1;                                  /* printable ASCII before ASCII: always a break */
        *width = 1;
        return UTF8_G_END;
    }
    int r = utf8_grapheme_step(&c->seg, p, n, budget, eof, used);
    cseg_scan(c, p, *used);
    if (r == UTF8_G_END) {
        *width = cw_width((enum cw)c->phase);
        utf8_cseg_init(c);
    }
    return r;
}

#ifdef UTF8_TEST_HOOKS
size_t utf8_test_prev(const uint8_t *base, size_t off)
{
    return utf8_prev(base, off);
}

int utf8_test_cell_width(uint32_t cp)
{
    return utf8_cell_width(cp);
}
#endif
