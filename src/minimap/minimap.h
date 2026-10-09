#ifndef EDIT_MINIMAP_H
#define EDIT_MINIMAP_H
#include "lineidx/lineidx.h"
#include "render/render.h"

/* UI-thread-owned standalone density strip. No allocation, locks, or workers.
 * All storage is caller-owned, including max_rows row summaries reserved at
 * open/resize. No pointer to source/grid/index survives a call.
 * Small means <=64 KiB AND <=20000 lines. Space bytes are ASCII HT, LF, VT,
 * FF, CR and space; all other bytes (including UTF-8 bytes) count. Each line's
 * count saturates at 80. A row covers K=ceil(lines/height) consecutive lines;
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
} minimap_row;

typedef struct minimap_style {
    uint32_t background, density, viewport, stale; /* opaque 0x00RRGGBB */
} minimap_style;

typedef struct minimap {
    minimap_row *rows;
    size_t capacity;
    uint64_t revision, bytes, lines, lines_per_row;
    uint32_t height, active_rows;
    bool initialized, filled, stale, sampled;
} minimap;

typedef struct minimap_target {
    uint64_t line, byte;
    bool exact;               /* exact byte at the returned line's start */
} minimap_target;

int minimap_init(minimap *m, minimap_row *rows, size_t max_rows);
void minimap_fini(minimap *m); /* idempotent; releases no caller storage */
/* Query after any edit/index event and before using retained cells. Includes
 * stored stale flag; the flag stays set until a successful fresh fill. */
bool minimap_stale(const minimap *m, const minimap_input *input);
/* Fill [first_col, first_col+width) across all grid rows in a begun frame;
 * damages those rows. viewport [first_line, first_line+visible_lines) is
 * clamped to the file, drawn as a band across the full strip width.
 * Matching revision/height reuses density summaries; every edit invalidates
 * them. Viewport/cell colours and row damage are produced on every fill.
 * Pending index: paint retained density (if height matches), or blank,
 * with stale tint; return OK with m->stale=true, and NEVER read source.
 * Invalid args leave grid/state unchanged. Invalid span reports SOURCE,
 * leaves grid unchanged and invalidates summaries (stale=true).
 * Caller sets index_ready only for the same revision/bytes as source.
 */
int minimap_fill(minimap *m, const minimap_input *input, render_grid *grid,
                 uint32_t first_col, uint32_t width,
                 uint64_t first_line, uint64_t visible_lines,
                 const minimap_style *style);
/* y is a strip row (pixel_y/cell_h), clamped for dragging outside the strip.
 * Small hits use exact cached line starts. Large hits resolve via index on
 * demand (one existing line_to_byte query, may scan one chunk); idx must model
 * input.source. Stale hits fail; output unchanged on failure. */
int minimap_hit(const minimap *m, const minimap_input *input, lineidx *idx,
                int64_t y, minimap_target *out);
int minimap_row_for_line(const minimap *m, uint64_t line, uint32_t *out);
#endif
