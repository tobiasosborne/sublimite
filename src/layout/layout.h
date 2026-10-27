/* layout (P3.1, edit-zzj.1): viewport of a piece tree/snapshot -> render.h cell grid.
 *
 * P4.1 adds opt-in wrapping through layout_wrap_init/layout_set_wrap; the
 * original description below applies to the default wrap-off path.
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
 * Long lines: an absent column index uses a bounded byte-position estimate.
 * Caller-reserved checkpoints are built from snapshots on src/work, then
 * adopted by layout_checkpoint_event on UI. No allocation in layout_run.
 * Grapheme segmentation resumes across windows/slices using utf8_cluster_step.
 * Clusters exceeding LAYOUT_WIN bytes keep exact width/segmentation but draw
 * '?' (approximate=true), since the glyph callback requires contiguous bytes.
 */
#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "render/render.h"
#include "piece/piece.h"
#include "font/font.h"
#include "utf8/utf8.h"
#include "base/base.h"
#include "work/work.h"

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

/* Map a cluster's artwork key (display width 1 or 2 from the complete source
 * cluster) to a grid.glyphs slot, or RENDER_NO_SLOT for a blank cell. The key
 * is utf8_cluster_artwork's projection, except that an emoji cluster
 * (utf8_cluster_emoji) is passed whole so the font can select a GSUB
 * ligature (edit-v16). Return 0 on success, nonzero to get '?'. */
typedef int (*layout_glyph_fn)(void *ctx, const uint8_t *cluster, size_t len,
                               uint32_t width, uint32_t *slot);
/* Optional viewport decorations, before cursor/selection colours. UI-owned
 * context must remain stable during a run; callback allocates nothing. */
typedef void (*layout_paint_fn)(void *ctx, uint64_t byte, uint32_t *fg,
                               uint32_t *bg, uint16_t *attrs);

/* Optional prepared hit metadata. Called only for rendered rows, bounded by
 * viewport cells. Cluster columns [column,column+count) have immutable byte
 * boundaries; skip/width locate a clipped cell within its cluster. Empty
 * cells/gutter use equal boundaries. Never reads source in the consumer. */
typedef void (*layout_hit_fn)(void *ctx, uint32_t row, uint32_t column,
    uint32_t count, uint32_t skip, uint32_t width, uint64_t before, uint64_t after);
typedef struct layout_config {
    uint32_t tab_width;          /* 0 -> 4 */
    bool gutter;
    uint32_t fg, bg, gutter_fg, gutter_bg, cursor_fg, cursor_bg, sel_fg, sel_bg;
    uint32_t slice_clusters;     /* 0 = unlimited */
    layout_glyph_fn glyph;       /* may be NULL */
    void *glyph_ctx;
    layout_hit_fn hit;
    void *hit_ctx;
} layout_config;

typedef struct layout_viewport {
    uint64_t first_byte;         /* start of a line */
    uint64_t first_line;         /* its 0-based number, or LAYOUT_LINE_UNKNOWN (exact lookup) */
    uint64_t hscroll;            /* absolute document columns (ignored with wrap on) */
    uint64_t line_count;         /* total lines (gutter width), or 0 / LAYOUT_LINE_UNKNOWN to ask the
                                    buffer; the editor's line index should supply it: the P1.3 stub
                                    kernel's piece_line_count is a ~1 ms scan. layout_edit keeps it. */
} layout_viewport;

#define LAYOUT_CHECKPOINT_STRIDE 4096u
#define LAYOUT_LONG_LINE 65536u
#define LAYOUT_BYTE_BUDGET 65536u
#define LAYOUT_CHECKPOINT_MSG UINT32_C(0x4c435031)
#define LAYOUT_CLUSTER_CACHE 512u

typedef struct layout_checkpoint { uint64_t byte, column; } layout_checkpoint;
/* One cached line. Reserve at open time; storage may not be freed until the
 * pool is shut down or the final event has been delivered. Fields private.
 * A worker writes only the unpublished suffix; UI reads the valid prefix. */
typedef struct layout_checkpoint_store {
    layout_checkpoint *entries;
    size_t capacity, count;
    const void *source;
    uint64_t start, end, columns;
    uint32_t tab, generation;
    bool complete, pending, newline;
    struct {
        struct layout_checkpoint_store *store;
        piece_snapshot *snapshot;
        const void *source;
        uint64_t start, pos, column;
        size_t count;
        uint32_t tab, generation;
    } job;
} layout_checkpoint_store;

typedef struct layout_cluster_cache_entry {
    uint64_t key[4];
    uint32_t stamp;
    uint8_t len, width, valid, key_len;
} layout_cluster_cache_entry;

/* INIT/open-time only: arena reserves 16 B per 4 KiB plus one sentinel.
 * max_line_bytes is a capacity bound; exhaustion leaves layout approximate. */
int layout_checkpoint_init(layout_checkpoint_store *s, edit_arena *arena, uint64_t max_line_bytes);
/* UI: submit a lazy build, retaining snapshot. source is the identity passed
 * to layout_begin[_snapshot], start is a line start. Snapshot creation is the
 * caller's job (outside layout_run); request itself allocates nothing. A store
 * permits one in-flight request; ERR_STATE means drain the preceding event.
 * Re-request after edits resumes from its last valid checkpoint. */
int layout_checkpoint_request(layout_checkpoint_store *s, work_pool *pool,
                              piece_snapshot *snapshot, const void *source,
                              uint64_t start, uint32_t tab_width);
/* UI mailbox router: true if the message belongs to s (even a stale result).
 * Does not mark/render: caller starts a new frame and relayouts after adoption.
 * All messages must be routed; do not externally cancel the owned job. */
bool layout_checkpoint_event(layout_checkpoint_store *s, const work_msg *msg);
/* UI: invalidate checkpoints at/after an edit; called by layout_edit for the
 * attached store. Strictly earlier checkpoints survive. No allocation. */
void layout_checkpoint_invalidate(layout_checkpoint_store *s, uint64_t off,
                                  uint64_t old_len, uint64_t new_len,
                                  uint64_t old_nl, uint64_t new_nl);

/* Wrapped row descriptor; caller-visible for viewport/motion adapters. Columns
 * count from logical line start; indent is synthetic on continuation rows. */
typedef struct layout_wrap_row {
    uint64_t start, end, next, line_start, line, column, end_column, last;
    uint32_t indent;
    bool newline, continuation, approximate;
} layout_wrap_row;

typedef struct layout {
    render_grid *grid;
    layout_config cfg;
    uint64_t *row_byte;          /* [rows] start byte of each row's line, LAYOUT_VOID_ROW past EOF */
    uint32_t *row_used;          /* [rows] cells possibly non-blank (cells beyond are blank) */
    const void *src; int is_snap;
    uint64_t total, line_count, first_byte, first_line, cursor, sel_lo, sel_hi;
    uint64_t hscroll;
    uint32_t gw, text_cols, tab;
    bool approximate, marks, hit_cr;
    /* Host-supplied density totals remain estimates through mutation. Default
     * false preserves exact standalone layout accounting; the editor sets
     * this before begin/edit and clears it on exact index publication. */
    bool line_count_estimated;
    bool first_line_valid;       /* logical byte anchor survives edits */
    layout_paint_fn paint;
    void *paint_ctx;
    uint32_t row, row_end;       /* work range [row, row_end) */
    int phase;                   /* 0 start row, 1 text, 2 skip to newline */
    uint64_t pos;                /* current byte */
    uint64_t col;                /* absolute column in the line */
    uint32_t vis;                /* next text cell */
    uint64_t win_pos; uint32_t wi, win_len; bool win_eof;
    layout_checkpoint_store *checkpoints;
    /* edit-1vun probe, optional (NULL by layout_init): counts the piece line
     * queries (piece_byte_to_line/piece_line_to_byte) a visual-row query
     * makes; on an unindexed mapping they resolve lazy newline counts from
     * byte 0 on the UI thread. Tests point it at a counter. */
    uint64_t *line_query_probe;
    uint64_t bytes_scanned, bytes_read, cache_hits, cache_misses; /* per begin/relayout;
        consumed decode/tail bytes and physical source-read bytes respectively */
    uint64_t row_scanned, cluster_at, cluster_len;
    uint32_t cluster_saved;
    bool cluster_active, row_indexed;
    utf8_cseg cluster_seg;
    layout_cluster_cache_entry cluster_cache[LAYOUT_CLUSTER_CACHE];
    uint8_t cluster_bytes[LAYOUT_WIN];
    /* P4.1 private wrap state. Reserved once with layout_wrap_init. */
    layout_wrap_row *wrap_rows, *wrap_plan;
    bool wrap, wrap_planning, wrap_leading, wrap_sink, wrap_cr, wrap_cursor_end;
    uint32_t wrap_first, wrap_after, wrap_fill, wrap_break_vis, wrap_written;
    uint64_t wrap_break_byte, wrap_break_col, wrap_break_last, wrap_last;
    uint32_t wrap_indent, wrap_capacity;
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

/* Attach a caller-owned cache for one line (NULL detaches); UI, idle only.
 * Keep the store alive for the layout lifetime. */
int layout_set_checkpoints(layout *l, layout_checkpoint_store *s);
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
void layout_set_paint(layout *l, layout_paint_fn paint, void *ctx);

bool layout_approximate(const layout *l);
uint32_t layout_gutter_width(const layout *l);
bool layout_busy(const layout *l);

/* Open-time reserve: two descriptors per viewport row; no typing allocation.
 * Init preserves wrap-off. Toggle only while idle; begin again after toggle or
 * resize. Existing begin/viewport callers keep their original semantics. */
int layout_wrap_init(layout *l, edit_arena *arena);
int layout_set_wrap(layout *l, bool enabled);
/* Trailing affinity places a soft-boundary cursor on the preceding row.
 * Ordinary layout_set_cursor keeps its existing leading-affinity behavior. */
void layout_set_cursor_visual(layout *l, uint64_t byte, bool trailing);
/* Start at a previously obtained visual row (logical line fields included).
 * Width/tab/gutter must match when using a descriptor from a preceding frame. */
int layout_begin_visual(layout *l, const piece_tree *t, layout_viewport vp,
                        const layout_wrap_row *first);
/* Query a visual row containing a cluster-boundary byte, or an adjacent row.
 * direction -1/0/+1. Work bounded by LAYOUT_BYTE_BUDGET; exact published column
 * checkpoints seed deep fallback. approximate is explicit if no wrap boundary
 * index exists near a deep byte. No allocation; no grid mutation. */
int layout_visual_row(const layout *l, const piece_tree *t, uint64_t byte,
                      int direction, layout_wrap_row *out, bool *approximate);

/* Query after a mutation before layout_edit: ignore stale row/checkpoint data. */
int layout_visual_row_fresh(const layout *l, const piece_tree *t, uint64_t byte,
                            layout_wrap_row *out, bool *approximate);
/* edit-cxho.25: the wrapped top after a mutation (before relayout) that
 * keeps `caret` (its fresh row) on screen below `top`. *moved is false when
 * the caret's row is within the rows below `top`; up to max_scroll rows
 * below them *new_top is the row rows-1 above the caret's (minimal scroll);
 * farther it is the caret's row. At most rows-1+max_scroll row queries. */
int layout_wrap_follow_edit(const layout *l, const piece_tree *t, uint64_t top, const layout_wrap_row *caret,
                            uint32_t max_scroll, layout_wrap_row *new_top, bool *moved, bool *approximate);
/* Same bounded visual query using a caller-certified logical line start and
 * ordinal (which may be estimated). Avoids whole-source piece line queries;
 * deep unindexed wrap phase retains the existing explicit approximation. */
int layout_visual_row_anchored(const layout *l, const piece_tree *t, uint64_t byte,
    uint64_t line_start, uint64_t line, layout_wrap_row *out, bool *approximate);
#endif
