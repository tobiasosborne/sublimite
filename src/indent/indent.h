#ifndef EDIT_INDENT_H
#define EDIT_INDENT_H
#include "piece/piece.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INDENT_PREFIX_BYTES (64u * 1024u)
#define INDENT_TYPING_BYTES 8192u
#define INDENT_MAX_WIDTH 16u
#define INDENT_NONE UINT64_MAX

typedef enum indent_code {
    INDENT_OK = 0, INDENT_ERR_ARGUMENT, INDENT_ERR_RANGE, INDENT_ERR_CAPACITY,
    INDENT_ERR_LIMIT
} indent_code;
typedef struct indent_style { bool uses_tabs; uint8_t width; } indent_style;
typedef struct indent_range { uint64_t lo, hi; } indent_range;
typedef struct indent_edit {
    uint64_t lo, hi; /* replace [lo,hi) in the PRE-insertion tree */
    size_t length;
    uint8_t bytes[1];
} indent_edit;

/* Pure, synchronous, allocation-free. Tree calls: owning UI thread only.
 * No state/resources: no init/fini needed. Caller storage must not alias inputs.
 * All offsets/ranges are bytes, windows half-open and must fit the tree.
 * Required result pointers; NULL out permitted only with cap==0.
 * Enter/brace inspect at most INDENT_TYPING_BYTES in each direction.
 * If a line boundary is not found within that bound, return ERR_LIMIT with
 * cleared results, no mutation/output writes. Caller may defer the command;
 * successful queries retain exact indentation/EOL semantics.
 * On error scalar/results are cleared, except required size on CAPACITY. */

/* Insert LF/CRLF + exact leading SP/TAB before cursor on its current line.
 * No extra level after open delimiters. A cursor between CR and LF is RANGE.
 * Newline style: current line's terminator, else preceding line's terminator,
 * else LF. EOF after LF refers to the empty final line. On CAPACITY, *length
 * is required capacity and out is untouched. No partial insertion. */
indent_code indent_on_enter(const piece_tree *tree, uint64_t cursor_byte,
                            uint8_t *out, size_t cap, size_t *length);

/* Call BEFORE inserting '}'. Always returns the complete insertion edit.
 * If the entire current line (excluding CRLF) is SP/TAB, remove one visual
 * indent stop BEFORE the cursor (width 1..16). Tabs advance to width stops.
 * Replacement is always one byte, '}'; the suffix is preserved.
 * Otherwise edit is ordinary insertion at cursor. Apply delete+replacement
 * together in the same undo group. uses_tabs is advisory; existing bytes win. */
indent_code indent_on_close_brace(const piece_tree *tree, uint64_t cursor_byte,
                                  indent_style style, indent_edit *edit);

/* Select bracket at cursor, else immediately before cursor, within [lo,hi).
 * Balance only its own type ()/[]/{}; other types, quotes and comments are
 * ordinary bytes. Return INDENT_NONE if no visible source/mate. Never reads
 * text outside window; O(window bytes), fixed stack scratch, no allocations. */
indent_code indent_bracket_match(const piece_tree *tree, uint64_t cursor_byte,
                                 uint64_t lo, uint64_t hi, uint64_t *match);

/* Ascending SP/TAB runs ending at a visible LF (exclude CR in CRLF) or EOF.
 * Clip starts to lo. A line cut by hi is omitted unless hi==EOF; no text read
 * outside [lo,hi). Lone CR is ordinary text. *count is the required number of
 * ranges; stores first min(count,cap), returns CAPACITY if count>cap. */
indent_code indent_trailing_ws_ranges(const piece_tree *tree, uint64_t lo,
                                     uint64_t hi, indent_range *out, size_t cap,
                                     size_t *count);

/* Read at most first 64 KiB, no line-count/index queries, no allocations.
 * Complete nonblank indented lines vote: any leading tab votes tabs, otherwise
 * spaces. Ties choose spaces. Width is GCD of space-only leading lengths,
 * defaults to 4 if absent or >16. A cut final line is ignored. Snapshot may
 * be used on any thread and must remain alive until return. */
indent_code indent_detect(const piece_snapshot *snapshot, indent_style *style);
indent_code indent_detect_bytes(const uint8_t *bytes, size_t length,
                                indent_style *style);
#endif
