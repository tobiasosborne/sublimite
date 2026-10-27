#ifndef EDIT_MINIMAP_H
#define EDIT_MINIMAP_H
#include "lineidx/lineidx.h"
#include "render/render.h"

/* Caller-owned standalone density strip. No allocation, locks, or workers.
 * All storage is caller-owned, including max_rows row summaries reserved at
 * open/resize. Only source callback/context identity survives a call; no
 * source bytes/grid/index pointers are retained. Context must identify one
 * buffer for its lifetime. Reinitialise or bind when recycling a context.
 * Small means <=64 KiB AND <=20000 lines. Space bytes are ASCII HT, LF, VT,
 * FF, CR and space; all other bytes (including UTF-8 bytes) count. Each line's
 * count saturates at 80 (the existing clipped line bound). Character columns
 * of the first line in each group are retained separately for miniature ink.
 * A row covers K=ceil(lines/height) consecutive lines;
 * density=floor(255*sum(clipped counts)/(80*row line count)). Extra rows blank.
 * Large files share <=256 representative nominal LINEIDX_CHUNK samples across
 * adjacent rows (also bounded by the chunk/active-row counts). Each sample
 * reads <=256 bytes. The first line after that boundary supplies the count;
 * a line longer than the sample is clipped. This is an approximation, flagged
 * sampled, and costs O(height*256 + width*height), independent of file size.
 */
#define MINIMAP_SMALL_BYTES UINT64_C(65536)
#define MINIMAP_SMALL_LINES UINT64_C(20000)
#define MINIMAP_LINE_CAP UINT64_C(80)
#define MINIMAP_SAMPLE_BYTES 256u
#define MINIMAP_MAX_SAMPLES 256u

enum { MINIMAP_OK = 0, MINIMAP_ERR_ARG = -1, MINIMAP_ERR_CAPACITY = -2,
       MINIMAP_ERR_STALE = -3, MINIMAP_ERR_SOURCE = -4 };

typedef struct minimap_input {
    lineidx_src source;       /* current bytes; span never blocks/allocates */
    uint64_t lines;           /* >=1, lineidx_line_count().value */
    uint64_t revision;        /* caller increments on EVERY mutation */
    bool index_ready;        /* complete && !building, len matches source */
} minimap_input;

typedef struct minimap_row {
    uint64_t first_line, line_count;
    uint64_t first_byte;      /* small: line start; large: representative sample */
    uint64_t ink;             /* small: sum of clipped counts; large: sample */
    uint8_t density;
    /* (G) existing clipped line bound: ceil(MINIMAP_LINE_CAP / 64)
     * machine words. Representative character columns; tabs expand to the
     * editor default four-column stops. Spaces retain their positions. */
    uint64_t marks[(MINIMAP_LINE_CAP + 63) / 64];
} minimap_row;

typedef struct minimap_style {
    uint32_t background, density, viewport, stale; /* opaque 0x00RRGGBB; stale is the pending edge bar */
} minimap_style;

typedef struct minimap {
    minimap_row *rows;
    size_t capacity;
    uint64_t revision, bytes, lines, lines_per_row;
    uint64_t painted_first, painted_end; /* retain the ready map's viewport during edits */
    uint32_t height, active_rows;
    bool initialized, filled, stale, sampled;
    const void *buffer_identity; /* optional stable token set by bind */
    lineidx_src identity; /* callback/context identity only; never dereferenced */
} minimap;

typedef struct minimap_target {
    uint64_t line, byte;
    bool exact;               /* exact byte at the returned line's start */
} minimap_target;

int minimap_init(minimap *m, minimap_row *rows, size_t max_rows);
/* Additive tab-switch binding for adapters that reuse one source context.
 * identity is an opaque stable buffer token; never dereferenced. Changing it
 * invalidates cached exact offsets. Bind before stale/fill/hit on a switch. */
int minimap_bind(minimap *m, const void *identity);
void minimap_fini(minimap *m); /* idempotent; releases no caller storage */
/* Query after any edit/index event and before using retained cells. Includes
 * stored stale flag; the flag stays set until a successful fresh fill. */
bool minimap_stale(const minimap *m, const minimap_input *input);
/* Worker: prepare into EXCLUSIVE state/rows using a retained immutable
 * snapshot-backed input. May fault on source bytes. No UI-owned tree/state.
 * Bind worker state to the same buffer token as UI when using minimap_bind.
 * input identity must be the SAME stable buffer context/callback as on UI;
 * a worker adapter must carry that identity (see prepare_source below).
 * Publish only via caller's work mailbox, retaining snapshot/job storage until
 * UI consumption. UI discards old buffer/revision/line-count/height results. */
int minimap_prepare(minimap *m, const minimap_input *input, uint32_t height);
/* Allows worker snapshot source to differ from the UI buffer identity. */
int minimap_prepare_source(minimap *m, const minimap_input *input,
                            const lineidx_src *snapshot_source, uint32_t height);
/* UI: copy prepared rows with identity/revision/bytes/lines/readiness checks.
 * Caller verifies prepared height still matches current grid, and keeps worker
 * rows immutable during publication. Row storage must not overlap. */
int minimap_publish(minimap *m, const minimap_input *input, const minimap *prepared);
/* UI: zero source reads, even on cache miss, edits, resize or index progress.
 * Fresh published summaries render normally; otherwise retain same-buffer,
 * same-height summaries and viewport, or display a pending scrollbar.
 * m->stale requests a worker preparation; use hit_approx while stale. */
int minimap_fill_cached(minimap *m, const minimap_input *input, render_grid *grid,
                        uint32_t first_col, uint32_t width,
                        uint64_t first_line, uint64_t visible_lines,
                        const minimap_style *style);
/* Fill [first_col, first_col+width) across all grid rows in a begun frame;
 * damages only rows whose strip cells change. viewport [first_line, first_line+visible_lines) is
 * clamped to the file, drawn as a translucent band with a one-cell perimeter.
 * Each column represents one character; marks are pixel-scale only when grid
 * cells are pixel-scale. Use minimap_pixels for an exact logical-pixel dump.
 * Matching revision/height reuses density summaries; every edit invalidates
 * them. Viewport/cell colours are checked on every fill. Legacy synchronous
 * fill may fault on dispersed mapped pages during regeneration: use only
 * with resident bytes on UI, or with an exclusive worker-owned grid. Use
 * fill_cached on UI for a source-read-free path.
 * Pending index: paint same-buffer retained density (if height matches),
 * or a current approximate scrollbar,
 * keeping its ready viewport and colours. Without retained density, draw a
 * current approximate scrollbar and a last-column pending bar in style.stale.
 * Return OK with m->stale=true, and NEVER read source.
 * Invalid args leave grid/state unchanged. Invalid span reports SOURCE,
 * leaves grid unchanged and invalidates summaries (stale=true).
 * Caller sets index_ready only for the same revision/bytes as source.
 */
int minimap_fill(minimap *m, const minimap_input *input, render_grid *grid,
                 uint32_t first_col, uint32_t width,
                 uint64_t first_line, uint64_t visible_lines,
                 const minimap_style *style);
/* Pending scrollbar geometry (no compatible summaries): whether summary row
 * r of rows paints the viewport [first_line, end_line) of a lines-line
 * source. minimap_fill's pending branch and the editor's grab hit share it
 * (edit-cxho.1), so the painted band and the grabbable band cannot drift. */
bool minimap_pending_band(uint64_t lines, uint32_t rows, uint32_t r,
                          uint64_t first_line, uint64_t end_line);
/* y is a strip row (pixel_y/cell_h), clamped for dragging outside the strip.
 * Small hits use exact cached line starts. Large hits resolve via index on
 * demand (one existing line_to_byte query, may scan one chunk); idx must model
 * input.source. Stale hits fail; output unchanged on failure. */
/* Source-read-free pixel dump of prepared summaries. Stale summaries retain
 * their painted viewport, as in fill; caller verifies source/height identity.
 * (G) perf target §2:
 * two logical pixels per summary line, a one-pixel-wide non-space mark on
 * the first scanline and a blank second scanline. Grouped/sampled summaries
 * retain their existing approximate navigation. height <= m->height*2.
 * Caller owns pixels; stride/capacity are in uint32_t elements. */
int minimap_pixels(const minimap *m, uint32_t *pixels, size_t capacity,
                   uint32_t width, uint32_t height, size_t stride,
                   uint64_t first_line, uint64_t visible_lines,
                   const minimap_style *style);
int minimap_hit(const minimap *m, const minimap_input *input, lineidx *idx,
                int64_t y, minimap_target *out);
/* Current byte-based navigation without index/cache/source reads. Row endpoints
 * map to byte 0 and EOF (height==1 maps to 0). line is only an estimate;
 * exact=false: caller must use byte-based scrolling until exact indexing. */
int minimap_hit_approx(const minimap_input *input, uint32_t height,
                       int64_t y, minimap_target *out);
int minimap_row_for_line(const minimap *m, uint64_t line, uint32_t *out);
#endif
