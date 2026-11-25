#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
typedef struct viewport_row_cert {
    uint64_t byte, end, next, line_start, col, end_column, indent, ordinal, exact_line;
    bool has_exact_line, newline, continuation, approximate;
} viewport_row_cert;
typedef struct viewport_key {
    uint64_t buffer, text, geometry;
} viewport_key;
typedef struct viewport_selection {
    uint64_t caret, mark, preferred_col;
} viewport_selection;
typedef struct viewport_state {
    viewport_anchor top;
    bool wrap;
} viewport_state;
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
typedef enum { VP_DONE, VP_MORE } viewport_progress;
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
typedef struct viewport_port {
    void *ctx;
    viewport_progress (*query)(void *, const viewport_request *,
                               size_t *credits, viewport_evidence *);
} viewport_port;
size_t viewport_motion_size(void);
void viewport_motion_begin(viewport_motion *, const viewport_intent *, viewport_frame, uint64_t length);
viewport_progress viewport_motion_step(viewport_motion *, const viewport_port *,
                                       size_t *credits, viewport_answer *);
bool viewport_wrap_default(const char *path);
uint64_t viewport_rebase(uint64_t byte, uint64_t off, uint64_t old_len,
                         uint64_t new_len, bool after_gravity);
bool viewport_accept(viewport_state *, viewport_selection *, const viewport_intent *,
                     const viewport_answer *);
bool viewport_plan(viewport_anchor, uint32_t width_px, uint32_t height_px,
                   uint32_t row_px, viewport_frame *);
bool viewport_pixel_row(const viewport_frame *, int64_t y_q8, uint32_t *row, uint32_t *within_q8);

#endif
