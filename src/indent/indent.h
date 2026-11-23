/* L1 indent: indentation queries over a read-only text.
 *
 * Pure and synchronous: no allocation, no state between calls, no imports
 * but byte functions. The text is reached only through an indent_text, a
 * forward span iterator, so the module depends on no text store: a piece
 * tree, a piece snapshot or a flat buffer each adapt in a few lines
 * (docs/decisions/v2-0fv.12.md). Every query reads only the bytes its
 * contract names; a span may extend past them into memory never touched. */
#ifndef INDENT_INDENT_H
#define INDENT_INDENT_H

#include <stddef.h>
#include <stdint.h>

/* (G) Enter and '}' read at most this many bytes on each side of the
 * cursor, plus the one byte before the line start that names the previous
 * line's terminator (v1 decision P4.5b, carried; the cost is a tracked
 * number in bench/indent_bench.c). */
#define INDENT_TYPING_BYTES 8192u
/* (P) Enter output: a CRLF, then a leading run that lies inside the
 * backward typing window. */
#define INDENT_ENTER_MAX (INDENT_TYPING_BYTES + 2u)
/* (G) indent_detect reads at most this prefix (v1 decision P4.2, carried). */
#define INDENT_PREFIX_BYTES (64u * 1024u)
/* Product behaviour (v1 P4.2): indent widths are 1..INDENT_MAX_WIDTH, and
 * detection falls back to INDENT_DEFAULT_WIDTH. */
#define INDENT_MAX_WIDTH 16u
#define INDENT_DEFAULT_WIDTH 4u
#define INDENT_NONE UINT64_MAX

typedef enum indent_code {
    INDENT_OK = 0,
    INDENT_ERR_ARGUMENT, /* width outside 1..INDENT_MAX_WIDTH */
    INDENT_ERR_RANGE,    /* lo > hi, an offset past the text, or a cursor between CR and LF */
    INDENT_ERR_CAPACITY, /* more trailing-whitespace runs than the caller's capacity */
    INDENT_ERR_LIMIT,    /* the answer depends on bytes past INDENT_TYPING_BYTES */
} indent_code;

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
typedef struct indent_style {
    indent_kind kind;
    uint8_t width; /* 1..INDENT_MAX_WIDTH */
} indent_style;

typedef struct indent_range { uint64_t lo, hi; } indent_range;

/* Both typing queries see the text with the selection [lo, hi) removed and
 * the cursor at lo (lo == hi: no selection). They never read the removed
 * bytes and do not charge them to the typing bound; every offset they
 * return is at most lo, so it is the same before and after the deletion.
 * Apply the deletion and the returned edit in one undo group, and only
 * after INDENT_OK. A cursor between the CR and LF of a CRLF is ERR_RANGE. */

/* The bytes to insert for Enter: the cursor line's terminator (LF or CRLF;
 * an unterminated line copies the previous line's, else LF), then the
 * line's leading SP/TAB run up to the cursor. No extra level after an
 * opening delimiter. Writes out[0 .. *length), *length <= INDENT_ENTER_MAX.
 * ERR_LIMIT when the line start is not within INDENT_TYPING_BYTES before
 * the cursor or its end not within INDENT_TYPING_BYTES after it. On error
 * *length is 0 and out is untouched. out must not alias the text. */
indent_code indent_on_enter(const indent_text *text, uint64_t lo, uint64_t hi,
                            uint8_t out[INDENT_ENTER_MAX], size_t *length);

/* Call before inserting '}': replacing [*from, lo) with '}' is the whole
 * edit. When the cursor line, terminator excluded, holds only SP/TAB, *from
 * is the indent stop before the cursor (a stop every `width` columns; a tab
 * advances to the next stop); otherwise *from == lo. ERR_LIMIT only when
 * neither typing window shows a byte other than SP/TAB on the cursor line
 * and one of them ends before reaching the line's end: then and only then
 * does the answer depend on unread bytes. On error *from is INDENT_NONE. */
indent_code indent_on_close_brace(const indent_text *text, uint64_t lo, uint64_t hi,
                                  uint8_t width, uint64_t *from);

typedef struct indent_pair { uint64_t source, mate; } indent_pair;

/* The bracket at the cursor, else the one just before it, and its mate,
 * both inside the window [lo, hi). Only the source's own type of ()/[]/{}
 * balances; other bytes, quotes and comments included, are ordinary. With
 * no source bracket or no mate in the window both fields are INDENT_NONE.
 * Reads only [lo, hi), O(hi - lo), fixed stack scratch.
 * ERR_RANGE: cursor > length, lo > hi or hi > length. */
indent_code indent_bracket_match(const indent_text *text, uint64_t cursor,
                                 uint64_t lo, uint64_t hi, indent_pair *pair);

/* The SP/TAB runs that end a line inside [lo, hi), ascending: each ends at
 * a LF (before the CR of a CRLF) or, when hi == length, at the end of the
 * text. Starts are clipped to lo. A run cut by hi < length is omitted: the
 * byte that would end it is unread. A lone CR is ordinary text. Stores the
 * first min(*count, cap) runs (out may be NULL when cap is 0) and sets
 * *count to the total; ERR_CAPACITY when that exceeds cap. Reads only
 * [lo, hi).
 * ERR_RANGE: lo > hi or hi > length. */
indent_code indent_trailing_ws(const indent_text *text, uint64_t lo, uint64_t hi,
                               indent_range *out, size_t cap, size_t *count);

/* The style of the first INDENT_PREFIX_BYTES. Every complete line with a
 * nonempty leading SP/TAB run and a byte other than SP, TAB or CR votes:
 * tabs when the run holds a tab, else spaces; a tie chooses spaces. The
 * width is the GCD of the space-only run lengths, or INDENT_DEFAULT_WIDTH
 * when there are none or the GCD exceeds INDENT_MAX_WIDTH. A line cut by
 * the prefix does not vote. A deterministic heuristic, not a language
 * inference: a one-space continuation line makes the width 1. */
indent_style indent_detect(const indent_text *text);

#endif
