#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A29: S is the nominal byte grid and D the conservative bound on one
 * display line, including any terminator. Layout certifies the aligned
 * seams. Viewport consumes the certificates without recomputing them. */
enum { VIEWPORT_GRID_BYTES = 32768, VIEWPORT_DISPLAY_MAX = 2 * VIEWPORT_GRID_BYTES + 4 };

typedef enum {
    VIEWPORT_RESTORE, VIEWPORT_REFLOW, VIEWPORT_FOLLOW, VIEWPORT_SEEK_BYTE,
    VIEWPORT_SEEK_LINE, VIEWPORT_SCROLL_Q8, VIEWPORT_LEFT, VIEWPORT_RIGHT,
    VIEWPORT_WORD_LEFT, VIEWPORT_WORD_RIGHT, VIEWPORT_UP, VIEWPORT_DOWN,
    VIEWPORT_PAGE_UP, VIEWPORT_PAGE_DOWN, VIEWPORT_HOME, VIEWPORT_END,
    VIEWPORT_DOC_HOME, VIEWPORT_DOC_END, VIEWPORT_SELECT_ALL, VIEWPORT_SELECT_WORD,
    VIEWPORT_SELECT_LINE
} viewport_action;

typedef struct viewport_anchor {
    uint64_t byte, hscroll_cols;
    uint32_t subrow_q8;
    bool visual_end;
} viewport_anchor;
/* The row owns [byte,next), with end its drawable byte boundary. At a
 * shared seam end == next, trailing affinity selects this row and leading
 * selects its successor. A hard terminator occupies [end,next), with
 * newline set for LF and CRLF. The terminal row has next == UINT64_MAX
 * and end at EOF, including an empty row after a final LF. Every other
 * row advances. Each byte start is a certified segmentation boundary.
 * line_start is the display-line start. approximate marks a synthetic
 * start and still describes exact geometry. continuation marks a soft
 * wrap. col, end_column and indent are cell counts. A clipped row may
 * have end_column == UINT64_MAX if its final column was not computed.
 * ordinal is publication-relative. exact_line is a zero-based logical
 * line rank, usable only when has_exact_line is true. */
typedef struct viewport_row_cert {
    uint64_t byte, end, next, line_start, col, end_column, indent, ordinal, exact_line;
    bool has_exact_line, newline, continuation, approximate;
} viewport_row_cert;
typedef struct viewport_key {
    uint64_t buffer, text, geometry;
} viewport_key;
/* Byte offsets within the bound snapshot. Either selection end is a
 * cluster boundary or the walker treats it as one. UINT64_MAX means
 * preferred_col is unset. Vertical movement latches a screen column. */
typedef struct viewport_selection {
    uint64_t caret, mark, preferred_col;
} viewport_selection;
typedef struct viewport_state {
    viewport_anchor top;
    bool wrap;
} viewport_state;
/* target is a byte for SEEK_BYTE and a zero-based line for SEEK_LINE.
 * REFLOW carries the issuer's selection as-is (E7): the issuer clears
 * preferred_col on a width change. SCROLL_Q8 uses signed pixels_q8.
 * extend retains the mark for movement. origin.top.byte is a leading
 * scroll anchor, and origin.top.visual_end is the caret's affinity. */
typedef struct viewport_intent {
    viewport_key key;
    uint64_t serial, target;
    viewport_action action;
    viewport_state origin;
    viewport_selection selection;
    int64_t pixels_q8;
    bool extend;
} viewport_intent;
typedef struct viewport_answer {
    viewport_key key;
    uint64_t intent_serial;
    viewport_anchor top;
    viewport_selection selection;
} viewport_answer;
typedef struct viewport_frame {
    uint32_t width_px, height_px, row_px, layout_rows;
    int64_t origin_y_q8;
} viewport_frame;

typedef struct viewport_motion viewport_motion;
/* DONE has a complete output. MORE keeps the cursors for another
 * scheduled work slice. WAIT is blocked on evidence only the driver
 * can obtain: call again when that evidence changes (E8). */
typedef enum { VP_DONE, VP_MORE, VP_WAIT } viewport_progress;
typedef enum { VP_WINDOW, VP_ROW, VP_NEIGHBOR, VP_COLUMN,
               VP_LOGICAL_RANK, VP_LOGICAL_START } viewport_query;
typedef struct viewport_request {
    viewport_query kind;
    union {
        struct { uint64_t byte; } window;
        struct { uint64_t byte; bool trailing; } row;
        struct { uint64_t byte; bool trailing; int64_t displacement; } neighbor;
        struct { uint64_t row_byte, screen_column; } column;
        struct { uint64_t byte; } logical_rank;
        struct { uint64_t line; } logical_start;
    } as;
} viewport_request;
typedef struct viewport_evidence {
    const uint8_t *bytes; size_t len; /* resident window starting at off */
    uint64_t off, point;             /* window/select offset and resolved column-to-byte point */
    viewport_row_cert row;
    /* VP_ROW only: the queried byte and affinity's column in cells from
     * the text area's left edge, including synthetic continuation indent. */
    uint64_t screen_column;
    uint64_t count; bool exact;      /* rank or select prefix count; select offset in off */
} viewport_evidence;
/* query receives an immutable request and a positive remaining grant.
 * On DONE it fills the fields below. Other fields are unused.
 *
 * VP_WINDOW: bytes, off and len describe a nonempty borrowed interval
 * inside the snapshot covering the requested byte (strictly below EOF).
 * It may begin before that byte. The bytes stay valid through consumption
 * in the current step, at least until the next query. The cursor retains
 * any needed bytes before yielding or issuing another query.
 * VP_ROW: row and screen_column for the requested byte and affinity.
 * VP_NEIGHBOR: row after the signed displacement from that byte and
 * affinity, clamped to the first or terminal row.
 * VP_COLUMN: point, the cluster boundary at or before the screen column
 * on the certified row identified by row_byte, clamped to its drawable
 * interval. The column is relative to the text area's left edge.
 * VP_LOGICAL_RANK: count is the LF count before byte (EOF allowed), with
 * exact indicating whether that rank is exact.
 * VP_LOGICAL_START: off and count give the exact logical start and rank
 * when exact is true (EOF and its actual rank past the last line). On
 * inexact evidence they describe the known prefix ending at off.
 *
 * Value evidence is copied. On MORE or WAIT the port supplies no usable
 * output and keeps any resumable service state for the same request.
 * MORE resumes on a work slice. WAIT resumes on a driver wake for new
 * evidence. The port debits its own work, including at least one credit
 * per delivered window byte. This byte debit includes retaining the
 * returned window in the motion cursor. For other work the port charges
 * one credit per byte operation, summary/directory visit or cell operated
 * on, or per bounded metadata answer. An indivisible indexed rank/select
 * is driver work under the signed S4 bound. Never debit below zero. */
typedef struct viewport_port {
    void *ctx;
    viewport_progress (*query)(void *, const viewport_request *,
                               size_t *credits, viewport_evidence *);
} viewport_port;
/* Returns the allocation size in bytes. Provide writable storage of at
 * least that size with max_align_t alignment. Prepay it before input. */
size_t viewport_motion_size(void);

/* Starts or replaces a motion in that storage. in is readable, frame has
 * the destination geometry prepared by viewport_plan, and length is the
 * bound snapshot's byte length (including zero). Selection ends are in
 * [0,length]. Copies the inputs. Clamps the incoming top for every action.
 * A REFLOW origin may carry a saved remainder from a taller row. Writes
 * only the cursor. Retire any outstanding port operation for a replaced
 * intent before reusing its provider state. */
void viewport_motion_begin(viewport_motion *, const viewport_intent *, viewport_frame, uint64_t length);

/* Advances a begun cursor. port, credits and out are valid for the call,
 * with stable snapshot/geometry and matching provider state until DONE
 * or cancellation. Cursor work debits one credit per inspected, copied
 * or decoded byte operation, or reverse-summary visit. It can prepay a
 * split scalar across grants and caps decoding at the S4 per-step limit.
 * Zero credit performs no work and returns MORE. Writes out only on
 * DONE. MORE keeps the cursor for another grant. WAIT leaves out
 * untouched and suspends at the same request until evidence changes.
 * In particular an inexact SEEK_LINE returns WAIT. The driver issues
 * at most one indexed attempt per new rank observation. */
viewport_progress viewport_motion_step(viewport_motion *, const viewport_port *,
                                       size_t *credits, viewport_answer *);

/* path is NULL or a NUL-terminated path. Returns the case-sensitive wrap
 * default for its basename. Unnamed and extensionless files, .md, .txt
 * and .tex wrap by default. */
bool viewport_wrap_default(const char *path);

/* Maps byte through [off,off+old_len) replaced by new_len bytes. The old
 * interval, replacement edge and resulting byte must fit uint64_t.
 * Bytes before off stay put, and surviving suffix bytes shift by the
 * length change. The right edge of a nonempty deletion always travels
 * with the suffix. Covered points and insertion ties map to off for
 * before-gravity and off+new_len for after-gravity. Returns the new byte. */
uint64_t viewport_rebase(uint64_t byte, uint64_t off, uint64_t old_len,
                         uint64_t new_len, bool after_gravity);

/* state and selection are writable installed values. in is the current
 * intent and the answer is a completed candidate. Returns false with
 * both outputs intact for a stale key or serial. On true installs both,
 * clearing preference and horizontal origin on a wrap toggle. */
bool viewport_accept(viewport_state *, viewport_selection *, const viewport_intent *,
                     const viewport_answer *);

/* Given clip dimensions in pixels and top with a normalized Q8
 * remainder, computes the intersecting rows and Q8 draw origin. Returns
 * false, leaving out untouched, if out is NULL, row height is zero or
 * cannot be indexed in uint32_t Q8, the remainder is out of range, or
 * the row count cannot fit. An empty clip has zero rows. Writes out
 * only on true. */
bool viewport_plan(viewport_anchor, uint32_t width_px, uint32_t height_px,
                   uint32_t row_px, viewport_frame *);

/* f is a planned frame and row and within_q8 are writable. y_q8 is
 * clip-local. Returns true and writes the row index and within-row Q8
 * coordinate for a point in the text clip, including a partial row.
 * Returns false without writing either output outside that domain. */
bool viewport_pixel_row(const viewport_frame *, int64_t y_q8, uint32_t *row, uint32_t *within_q8);

#endif
