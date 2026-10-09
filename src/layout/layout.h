/* layout (P3.1, edit-zzj.1): viewport of a piece tree/snapshot -> render.h cell grid.
 *
 * No wrap yet: one grid row per buffer line, clipped at the right edge, with a
 * horizontal scroll offset in columns. Tabs expand to the next multiple of
 * tab_width (columns count from the line start, the gutter excluded). Grapheme
 * clusters come from utf8_cluster; zero-width clusters are dropped, wide
 * clusters take a wide-left/wide-right pair, and a wide cluster that does not
 * fit (right edge) or is cut by the horizontal offset (left edge) becomes one
 * blank cell. Invalid bytes and control characters draw the '?' glyph with the
 * INVERSE attribute, width 1 (PRD 6.2). A trailing '\r' before '\n' is hidden.
 *
 * Work is sliced: layout_run does at most slice_clusters units (one unit per
 * cell-producing character, one per 256 skipped bytes) and returns
 * LAYOUT_MORE when work remains, so the editor loop can check input between
 * slices. A sliced run produces exactly the grid of a one-shot run.
 *
 * Thread: UI thread only. No allocation after layout_init; all storage is
 * caller-provided. The caller calls render_frame_begin on the grid before
 * layout_begin / layout_edit (damage marking needs a begun frame).
 *
 * Glyph slots: layout_ascii_glyphs() fills the first LAYOUT_ASCII_GLYPHS
 * entries of the grid's glyph table (slot = cp - 0x20, page 0 = the baked ASCII
 * page; space has no image and uses RENDER_NO_SLOT). Non-ASCII clusters ask the
 * layout_glyph_fn; without one (or on failure) they draw '?'.
 *
 * Long lines (perf 2.4): the scan of one line is bounded per slice and resumes
 * in the next. A horizontal offset above LAYOUT_MAX_HSCROLL columns is clamped
 * to it and layout_approximate() reports true; the checkpoint-based jump
 * (4 KiB column checkpoints for lines > 64 KiB) is a follow-up bead.
 */
#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "render/render.h"
#include "piece/piece.h"
#include "font/font.h"

enum {
    LAYOUT_DONE = 0, LAYOUT_MORE = 1,
    LAYOUT_RESET = 2,            /* edit touched the viewport start: call layout_begin again */
    LAYOUT_ERR_ARG = -1, LAYOUT_ERR_STATE = -2, LAYOUT_ERR_SOURCE = -3
};
#define LAYOUT_ASCII_GLYPHS 95u
#define LAYOUT_WIN 16384u
#define LAYOUT_MAX_HSCROLL 65536u
#define LAYOUT_LINE_UNKNOWN UINT64_MAX
#define LAYOUT_VOID_ROW UINT64_MAX

/* Map a cluster (bytes, display width 1 or 2) to a grid.glyphs slot, or
 * RENDER_NO_SLOT for a blank cell. Return 0 on success, nonzero to get '?'. */
typedef int (*layout_glyph_fn)(void *ctx, const uint8_t *cluster, size_t len,
                               uint32_t width, uint32_t *slot);

typedef struct layout_config {
    uint32_t tab_width;          /* 0 -> 4 */
    bool gutter;
    uint32_t fg, bg, gutter_fg, gutter_bg, cursor_fg, cursor_bg, sel_fg, sel_bg;
    uint32_t slice_clusters;     /* 0 = unlimited */
    layout_glyph_fn glyph;       /* may be NULL */
    void *glyph_ctx;
} layout_config;

typedef struct layout_viewport {
    uint64_t first_byte;         /* start of a line */
    uint64_t first_line;         /* its 0-based number, or LAYOUT_LINE_UNKNOWN (exact lookup) */
    uint32_t hscroll;            /* columns */
    uint64_t line_count;         /* total lines (gutter width), or 0 / LAYOUT_LINE_UNKNOWN to ask the
                                    buffer; the editor's line index should supply it: the P1.3 stub
                                    kernel's piece_line_count is a ~1 ms scan. layout_edit keeps it. */
} layout_viewport;

typedef struct layout {
    render_grid *grid;
    layout_config cfg;
    uint64_t *row_byte;          /* [rows] start byte of each row's line, LAYOUT_VOID_ROW past EOF */
    uint32_t *row_used;          /* [rows] cells possibly non-blank (cells beyond are blank) */
    const void *src; int is_snap;
    uint64_t total, line_count, first_byte, first_line, cursor, sel_lo, sel_hi;
    uint32_t hscroll, gw, text_cols, tab;
    bool approximate, marks;
    uint32_t row, row_end;       /* work range [row, row_end) */
    int phase;                   /* 0 start row, 1 text, 2 skip to newline */
    uint64_t pos;                /* current byte */
    uint64_t col;                /* absolute column in the line */
    uint32_t vis;                /* next text cell */
    uint64_t win_pos; uint32_t wi, win_len; bool win_eof;
    uint8_t win[LAYOUT_WIN];
} layout;

/* Fill out[0..LAYOUT_ASCII_GLYPHS) for atlas (page 0). */
void layout_ascii_glyphs(const font_ascii_atlas *a, render_glyph *out);

/* Bind to a grid (dims/cells already set up), clear every cell to blank
 * (cfg.fg on cfg.bg), zero row_used. row_byte: rows entries, row_used: rows. */
int layout_init(layout *l, render_grid *g, const layout_config *cfg,
                uint64_t *row_byte, uint32_t *row_used);

/* Full layout of a new viewport (marks the frame full). Scroll = call this. */
int layout_begin(layout *l, const piece_tree *t, layout_viewport vp);
int layout_begin_snapshot(layout *l, const piece_snapshot *s, layout_viewport vp);

/* Continue; LAYOUT_MORE means call again (after checking input). */
int layout_run(layout *l);

/* Re-lay out rows [first, first+count) only (e.g. cursor moved): marks them. */
int layout_relayout_rows(layout *l, uint32_t first, uint32_t count);

/* Tell layout the buffer changed (call AFTER the mutation, tree sources only):
 * bytes [off, off+old_len) were replaced by new_len bytes, old_nl / new_nl
 * newlines removed / inserted. Marks the affected rows (one row for a
 * newline-free edit, that row to the bottom otherwise, everything when the
 * gutter width or first line changes) and schedules their re-layout. */
int layout_edit(layout *l, uint64_t off, uint64_t old_len, uint64_t new_len,
                uint64_t old_nl, uint64_t new_nl);

/* Cursor (byte offset, UINT64_MAX = none) and selection [lo,hi) colours are
 * resolved while laying out; call layout_relayout_rows for rows that change. */
void layout_set_cursor(layout *l, uint64_t byte);
void layout_set_selection(layout *l, uint64_t lo, uint64_t hi);

bool layout_approximate(const layout *l);
uint32_t layout_gutter_width(const layout *l);
bool layout_busy(const layout *l);

#endif
