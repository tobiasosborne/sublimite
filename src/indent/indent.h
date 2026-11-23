/* L1 indent: indentation queries over a read-only text.
 *
 * Pure, synchronous and total: no allocation, no state between calls, no
 * error codes. The text is reached only through an indent_text, a forward
 * span iterator, so the module depends on no text store
 * (docs/decisions/v2-0fv.12.md). Indent never searches for line
 * boundaries: a typing query is handed the start of the cursor line by the
 * caller, who has the line index and the painted row. Each query reads
 * only the bytes its comment names. A span may extend past them into
 * memory that is never touched.
 *
 * Preconditions are stated per call and checked with ASSERT (os.h, A15).
 * No input can violate them; only a caller bug can. */
#ifndef INDENT_INDENT_H
#define INDENT_INDENT_H

#include <stddef.h>
#include <stdint.h>

/* (G)/(M) Most SP/TAB bytes one typing part reads. The worst query is '}'
 * on a blank line, which reads 2B + 1 bytes; the worst text is one-byte
 * spans, one iterator call per byte. Measured p99 there is 19.6 ns/byte,
 * (M)[AC] load1 4.3, bench/indent_bench.c. So 2B + 1 bytes fit the 0.5 ms
 * UI slice (G, v1 perf/01-perf-target.md, G1 row) for B <= 12,755. B is the
 * largest power of two below that. A part that reaches it takes the
 * degraded answer stated at each query; only pathological lines get there. */
#define INDENT_BLANK_BYTES 8192u
/* Product behaviour (v1 P4.2), with an (E) rationale: a text's style shows
 * in its first screens. 64 KiB is about 1,600 lines of 40 bytes, and the
 * cost per open stays constant however large the file. */
#define INDENT_PREFIX_BYTES (64u * 1024u)
/* Product behaviour (v1 P4.2): widths are 1..INDENT_MAX_WIDTH, and
 * detection falls back to INDENT_DEFAULT_WIDTH. */
#define INDENT_MAX_WIDTH 16u
#define INDENT_DEFAULT_WIDTH 4u
#define INDENT_NONE UINT64_MAX

/* A read-only text of `length` bytes. seek(iter, off), off <= length,
 * positions the iterator at byte off. Each next(iter, &p) then returns the
 * length n >= 1 of the following span and points p at those bytes, or
 * returns 0 at the end of the text. The bytes stay valid and unchanged
 * until the indent call returns. Calls on one indent_text must not overlap:
 * a query moves its iterator. */
typedef struct indent_text {
    void (*seek)(void *iter, uint64_t off);
    size_t (*next)(void *iter, const uint8_t **p);
    void *iter;
    uint64_t length;
} indent_text;

typedef enum indent_kind { INDENT_SPACES, INDENT_TABS } indent_kind;
typedef enum indent_eol { INDENT_LF, INDENT_CRLF } indent_eol;
typedef struct indent_style {
    indent_kind kind;
    indent_eol eol;
    uint8_t width; /* 1..INDENT_MAX_WIDTH */
} indent_style;

typedef struct indent_range { uint64_t lo, hi; } indent_range;

/* Typing queries. The caret is at lo, and [lo, hi) is the selection that
 * the keystroke replaces. line_start is the start of the line holding lo:
 * 0 or one past a LF, with no LF in [line_start, lo). A caller that does
 * not know it passes lo, and gets the answer for an empty leading run.
 * Every returned offset is at most lo, so it is the same before and after
 * the selection is deleted. Neither query reads inside the selection.
 * ASSERT: line_start <= lo <= hi <= length; line_start is 0, lo, or one
 * past a LF (a check that also reads the byte before line_start). */

/* Enter: the cursor line's leading SP/TAB run up to the caret, as the range
 * [line_start, run.hi) of the text. The caller deletes [lo, hi), then
 * inserts the buffer's line break (indent_style.eol) and the run's bytes at
 * lo, by reference or by copy. No extra level after an opening delimiter.
 * Reads [line_start, run.hi] clipped to lo, at most INDENT_BLANK_BYTES + 1
 * bytes. Degraded: a run of more than INDENT_BLANK_BYTES blanks is returned
 * empty, so Enter inserts a bare line break. */
indent_range indent_on_enter(const indent_text *text, uint64_t line_start, uint64_t lo);

/* '}', called before it is inserted: returns from <= lo, and replacing
 * [from, lo) with '}' is the whole edit. When the cursor line holds only
 * SP/TAB around the caret, from [line_start, lo) and from hi to its LF,
 * CRLF or the text end, from is the indent stop before the caret: a stop
 * every `width` columns, and a tab advances to the next stop. Otherwise
 * from == lo. A lone CR is text.
 * Reads [line_start, lo) up to its first byte other than SP/TAB, then from
 * hi up to and including the first byte other than SP/TAB, plus one more
 * after a CR. Degraded: a part with more than INDENT_BLANK_BYTES blanks
 * counts as text and is not read past that, so from == lo.
 * ASSERT: 1 <= width <= INDENT_MAX_WIDTH (config validates user widths). */
uint64_t indent_on_close_brace(const indent_text *text, uint64_t line_start,
                               uint64_t lo, uint64_t hi, uint8_t width);

typedef struct indent_pair { uint64_t source, mate; } indent_pair;

/* The bracket at the cursor, else the one just before it, and its mate,
 * both inside the window [lo, hi). Only the source's own type of ()/[]/{}
 * balances; other bytes, quotes and comments included, are ordinary. With
 * no source bracket or no mate in the window, both fields are INDENT_NONE.
 * Reads only [lo, hi), O(hi - lo), with a fixed stack scratch.
 * ASSERT: lo <= hi <= length, cursor <= length. */
indent_pair indent_bracket_match(const indent_text *text, uint64_t cursor,
                                 uint64_t lo, uint64_t hi);

/* The SP/TAB runs that end a line inside [lo, hi), ascending: each ends at
 * a LF (before the CR of a CRLF) or, when hi == length, at the end of the
 * text. Starts are clipped to lo. A run cut by hi < length is omitted,
 * because the byte that would end it is unread. A lone CR is ordinary text.
 * Stores the first min(count, cap) runs (out may be NULL when cap is 0) and
 * returns count. Reads only [lo, hi). ASSERT: lo <= hi <= length. */
size_t indent_trailing_ws(const indent_text *text, uint64_t lo, uint64_t hi,
                          indent_range *out, size_t cap);

/* The style of the first INDENT_PREFIX_BYTES. A line is complete when it
 * ends at a LF inside the prefix, or when it is the last line and the
 * prefix is the whole text; a line cut by the prefix is ignored.
 * - Indent: a complete line with a nonempty leading SP/TAB run and some
 *   byte other than SP, TAB or CR votes tabs when the run holds a tab,
 *   else spaces. A tie chooses spaces.
 * - Width: the GCD of the space-only run lengths, or INDENT_DEFAULT_WIDTH
 *   when there are none or the GCD exceeds INDENT_MAX_WIDTH.
 * - Line break: every LF in the prefix votes CRLF when a CR precedes it,
 *   else LF. A tie, or no LF at all, chooses LF.
 * A deterministic heuristic, not a language inference: a one-space
 * continuation line makes the width 1. */
indent_style indent_detect(const indent_text *text);

#endif
