#ifndef UTF8_UTF8_H
#define UTF8_UTF8_H

/* utf8: strict UTF-8 decode/encode, cell width, UAX #29 grapheme clusters.
 *
 * Bytes are never rejected: an ill-formed byte decodes as an invalid unit of
 * length 1 carrying the byte value, so every buffer segments into units and
 * round-trips byte-exactly. Width and grapheme properties come from the
 * generated Unicode 15.1 tables in utf8/tables.h (tools/ucdgen.py; see
 * docs/decisions/v2-0fv.11.md for the regeneration obligation). No
 * allocation, no globals, no imports beyond memset/memmove, safe on arbitrary
 * bytes. Byte offsets are size_t because every call works within one
 * contiguous span; callers (piece, layout) hand in the spans. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One decoded unit. valid=1: cp is a Unicode scalar, len 1..4.
 * valid=0: cp is the first byte (0x80..0xFF), len 1. */
typedef struct {
    uint32_t cp;
    uint8_t len;
    uint8_t valid;
} utf8_step;

/* Decode the unit at p[0..n), n >= 1. Strict (Unicode Table 3-7): C0, C1 and
 * F5..FF leads, overlongs (E0 80..9F, F0 80..8F), surrogates (ED A0..BF),
 * > U+10FFFF (F4 90..BF), stray continuations and truncated sequences are all
 * invalid: {cp = p[0], len = 1, valid = 0}. Never reads beyond p[n-1].
 * Inline on purpose: v1 measured the out-of-line form ~2x slower on CJK runs
 * (M, v1 docs/decisions/P1.1b.md): the struct return made len depend on the
 * loaded bytes and serialised decode loops; inlined, len comes from the
 * predicted branch. */
static inline utf8_step utf8_decode(const uint8_t *p, size_t n)
{
    utf8_step s = { p[0], 1, 0 };
    uint32_t b = p[0];
    if (b < 0x80) {
        s.valid = 1;
        return s;
    }
    if (b < 0xC2 || b > 0xF4)               /* continuation, C0/C1 overlong, F5..FF */
        return s;
    if (b < 0xE0) {
        if (n < 2 || (p[1] & 0xC0) != 0x80)
            return s;
        s.cp = ((b & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        s.len = 2;
        s.valid = 1;
        return s;
    }
    if (b < 0xF0) {
        if (n < 3)
            return s;
        uint8_t lo = b == 0xE0 ? 0xA0 : 0x80, hi = b == 0xED ? 0x9F : 0xBF;
        if (p[1] < lo || p[1] > hi || (p[2] & 0xC0) != 0x80)
            return s;
        s.cp = ((b & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        s.len = 3;
        s.valid = 1;
        return s;
    }
    if (n < 4)
        return s;
    uint8_t lo = b == 0xF0 ? 0x90 : 0x80, hi = b == 0xF4 ? 0x8F : 0xBF;
    if (p[1] < lo || p[1] > hi || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
        return s;
    s.cp = ((b & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    s.len = 4;
    s.valid = 1;
    return s;
}

/* Start offset of the unit that ends at off, consistent with forward
 * segmentation by utf8_decode (off must be a unit boundary of base[0..off)).
 * Reads only base[off-4 .. off-1]. off == 0 returns 0. */
size_t utf8_prev(const uint8_t *base, size_t off);

/* Encode a scalar into out; returns 1..4. Surrogates and cp > U+10FFFF encode
 * U+FFFD (EF BF BD). */
size_t utf8_encode(uint32_t cp, uint8_t out[4]);

/* Out-of-line table path of utf8_cell_width; same result for every cp. */
int utf8_cell_width_table(uint32_t cp);

/* Terminal cells for one scalar: 0 for Mn/Me/Mc/Cf, Default_Ignorable,
 * variation selectors and emoji skin-tone modifiers; 2 for East Asian
 * Wide/Fullwidth and Emoji_Presentation (zero wins where both apply); 1
 * otherwise, including controls and anything > U+10FFFF (the renderer draws
 * controls as placeholders). Below U+0300 the only table entry is U+00AD (Cf);
 * tests/utf8_test.c checks that and every code point. © U+00A9 and ® U+00AE
 * are Extended_Pictographic but not Emoji_Presentation: width 1 alone, 2 with
 * VS16 (utf8_cluster). Unassigned code points follow the same tables, not a
 * blanket 1: U+2FFFD (EAW=W) is 2 and U+E01F0 (Default_Ignorable) is 0. This
 * is the width of a scalar; invalid bytes are not scalars and need width 1
 * from the caller (utf8_cluster does it); clusters use utf8_cluster_width. */
static inline int utf8_cell_width(uint32_t cp)
{
    if (cp < 0x300)
        return cp != 0xAD;
    return utf8_cell_width_table(cp);
}

/* Byte length of the extended grapheme cluster at p[0..n) per UAX #29
 * (Unicode 15.1) rules GB3..GB13 including GB9c (Indic conjuncts), with the
 * real Grapheme_Cluster_Break=Control table. An invalid byte is a one-byte
 * cluster and acts as Control to its neighbours (a break before and after it).
 * Returns 0 iff n == 0; otherwise 1..n and always a unit boundary. Passes all
 * 1187 cases of vendor/ucd/GraphemeBreakTest.txt (tests/utf8_test.c). Cost is
 * linear in the cluster length, which is unbounded (a base plus a million
 * marks is one cluster); UI-thread callers use utf8_grapheme_step. */
size_t utf8_grapheme_next(const uint8_t *p, size_t n);

/* Start of the grapheme cluster that contains byte off-1 of base[0..off),
 * segmenting forward from the nearest certain break (invalid byte, CR,
 * Control, or LF after a non-CR) at or before it, or from base[0]. When off
 * is a cluster boundary (the usual cursor-left case) this is the start of the
 * cluster ending at off, i.e. exactly the cluster utf8_grapheme_next would
 * have stepped over. off must be a unit boundary of base[0..off), and base[0]
 * a cluster start. Reads only base[0..off-1]. off == 0 returns 0. Unbounded
 * like utf8_grapheme_next. */
size_t utf8_grapheme_prev(const uint8_t *base, size_t off);

/* ---- Resumable, budgeted segmentation ----
 * A cluster is unbounded ('a' + 24,576 x U+0301 is one 49,153-byte cluster),
 * so a UI-thread caller uses the step API: it consumes at most `budget`
 * bytes (rounded up to the unit that crosses it), and utf8_gseg carries
 * everything the rules remember, so the next call continues the same cluster
 * at p + *used. Segmentation of a chunked input is identical to the one-shot
 * result for every chunking and budget (tests and fuzz check it), and a short
 * n never manufactures a boundary: a truncated multi-byte tail is reported as
 * UTF8_G_MORE, not decoded as invalid bytes.
 *
 * Usage: utf8_gseg g; utf8_gseg_init(&g); then, from the cluster start p:
 *   loop { r = utf8_grapheme_step(&g, p + len, n - len, budget, eof, &used);
 *          len += used; if (r == UTF8_G_END) break; if (r == UTF8_G_MORE) ...refill... }
 * On UTF8_G_END the state is reset (ready for the next cluster) and len is the
 * cluster length; the next cluster starts at p + len.
 * Fields are private; invariants in docs/decisions/v2-0fv.11.md. */
typedef struct {
    uint8_t prev;       /* Grapheme_Cluster_Break class of the last scalar */
    uint8_t gb11;       /* GB11 automaton: none / ExtPict Extend* / ... ZWJ */
    uint8_t ri_odd;     /* the trailing Regional_Indicator run has odd length */
    uint8_t incb;       /* GB9c automaton: none / Consonant [Extend|Linker]* / ... Linker ... */
    uint8_t started;    /* 0: no scalar yet and every other field is 0 */
} utf8_gseg;

enum {
    UTF8_G_END = 0,     /* the cluster ended; *used bytes are in it (0 only for n == 0 at eof) */
    UTF8_G_BUDGET = 1,  /* budget spent mid-cluster; call again at p + *used */
    UTF8_G_MORE = 2     /* input exhausted (or a truncated multi-byte tail) with eof == 0:
                           append bytes and call again at p + *used; the tail is NOT consumed */
};

/* Default budget, bytes per call (G). Derivation: the slowest chain costs
 * t ns/byte (M, bench/utf8_bench.c *_total rows); one call then costs about
 * 4096 * t, which must stay well inside the 0.5 ms UI slice (G, v1
 * perf/01-perf-target.md G1 row: "UI slices <= 0.5 ms"). v2 numbers and
 * the resulting margin: docs/decisions/v2-0fv.11.md. */
#define UTF8_GRAPHEME_BUDGET 4096u

static inline void utf8_gseg_init(utf8_gseg *g) { memset(g, 0, sizeof *g); }

/* Continue the cluster in g over p[0..n). budget 0 is treated as 1 (every
 * call with n > 0 makes progress). eof != 0: p+n is the end of the text, so a
 * truncated tail is invalid bytes and n exhausted means END. An invalid byte
 * is its own cluster (fresh g) or ends the current one without being consumed. */
int utf8_grapheme_step(utf8_gseg *g, const uint8_t *p, size_t n, size_t budget, int eof, size_t *used);

/* 1 iff UAX #29 puts a cluster break between units x and y (x directly
 * before y) for EVERY left context of x, so forward segmentation may start
 * at y with fresh state. Conservative: 0 when x is Prepend, Extend or ZWJ
 * (GB9b, GB9c, GB11 context), x and y are both RI (GB12/13), the pair is
 * CR LF, y is Extend/ZWJ/SpacingMark, or a Hangul GB6-8 pair. Invalid units
 * act as Control (a break on both sides), as in utf8_grapheme_next. */
int utf8_break_certain(utf8_step x, utf8_step y);

/* Terminal cells of the first cluster of p[0..n), and its byte length
 * (== utf8_grapheme_next). Rule (layout uses this, not a sum of scalar widths):
 *   - invalid byte, CR, LF, Control (incl. tab): 1 (the renderer draws a
 *     placeholder; tab stops are the layout's business);
 *   - otherwise take the first scalar with utf8_cell_width > 0 (so Prepend,
 *     Cf and marks in front of a base are skipped); none: 0;
 *   - if that scalar is Hangul (L, V, T, LV, LVT) the cluster is 2, however
 *     many jamo follow (an orphan V or T block is also 2);
 *   - if the base has width 1 and is Extended_Pictographic (© and ® included)
 *     and the cluster contains VS16 (U+FE0F) or a ZWJ followed by an
 *     Extended_Pictographic scalar, or the base is [0-9#*] and the cluster
 *     contains U+20E3: 2;
 *   - else the base width (flags, modifier sequences: the base's 2).
 * VS15 (U+FE0E) does not narrow a wide base. Always 0..2. */
size_t utf8_cluster(const uint8_t *p, size_t n, int *width);

/* Width part of utf8_cluster for the cluster at p[0..n). */
int utf8_cluster_width(const uint8_t *p, size_t n);

/* Unshaped artwork key for one already segmented cluster. Copy to out,
 * omitting U+200D, U+FE0E/U+FE0F and U+1F3FB..U+1F3FF. If the first
 * nonzero-width scalar is Extended_Pictographic, stop at its first ZWJ so
 * later joined emoji cannot overlay or replace the first emoji's artwork.
 * Other combining marks remain. Return bytes written, at most len; out must
 * hold len bytes and may equal p (otherwise the spans must be disjoint).
 * Does not change the source cluster, segmentation or cell width. Linear in
 * len, no allocation: UI callers must bound len by their existing scratch. */
size_t utf8_cluster_artwork(const uint8_t *p, size_t len, uint8_t *out);

/* The projection above as a scalar step, for consumers that walk a cluster
 * themselves (the font's fallback path). Zero-initialise st per cluster;
 * feed valid scalars in order. Returns 1 keep, 0 omit, -1 stop (this scalar
 * and the rest of the cluster are omitted). */
typedef struct utf8_artwork_state {
    uint8_t phase;      /* private: 0 no base yet, then text base or pictographic base */
} utf8_artwork_state;
int utf8_artwork_step(utf8_artwork_state *st, uint32_t cp);

/* UAX #29 emoji properties used by segmentation. */
enum { UTF8_EMOJI_PICT = 1, UTF8_EMOJI_RI = 2 };
int utf8_emoji_class(uint32_t cp);   /* PICT, RI or 0 */

/* Emoji class of the cluster's first nonzero-width scalar (0 if none, or not
 * an emoji). Such clusters reach the glyph lookup whole, so the font can try
 * a GSUB ligature before applying utf8_artwork_step itself. */
int utf8_cluster_emoji(const uint8_t *p, size_t len);

/* ---- Budgeted cluster + width ----
 * utf8_cluster / utf8_cluster_width are unbounded (they run utf8_grapheme_next
 * and then a linear width scan), so layout on a UI slice uses this: the
 * grapheme step API with the cluster-width rule folded in, consuming the span
 * each call completes. The width is decided from the first scalar with a
 * nonzero width and then only the VS16 / ZWJ+ExtPict / keycap followers
 * matter; once the answer cannot change the rest of the cluster is segmented
 * but not scanned. Result identical to utf8_cluster on completion for every
 * budget and every chunking (tests and fuzz check it).
 * Usage as utf8_grapheme_step: init once, call at p + len until UTF8_G_END
 * (refill on UTF8_G_MORE); *width is written only on UTF8_G_END.
 * Fields are private; invariants in docs/decisions/v2-0fv.11.md. */
typedef struct {
    utf8_gseg seg;
    uint8_t phase;      /* width automaton: seek base / base waits for a follower / decided */
} utf8_cseg;

static inline void utf8_cseg_init(utf8_cseg *c) { memset(c, 0, sizeof *c); }

/* Like utf8_grapheme_step (same budget, eof and *used contract) and, on
 * UTF8_G_END, *width = the cluster width, 0..2, as utf8_cluster reports it. */
int utf8_cluster_step(utf8_cseg *c, const uint8_t *p, size_t n, size_t budget, int eof, size_t *used, int *width);

#endif
