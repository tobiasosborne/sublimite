/* P2.5 CPU raster backend: SSE2 cell blit, MT4 strips on the work raster pool,
 * XShm present. Implements render.h's frozen backend interface (see
 * docs/decisions/P2.5.md). */
#ifndef EDIT_RASTER_H
#define EDIT_RASTER_H
#include "render/render.h"

/* Factory: fills b (zeroed or shut-down). init needs config.platform (X11 plat
 * with a window) and config.workers (pool with >= 1 WORK_RASTER worker). */
int render_cpu_backend(render_backend *b);

/* UST (us) and MSC of the latest matching-frame Present completion (UI only,
 * valid after that frame's PRESENT_COMPLETE event was routed). */
bool raster_last_present(const render_backend *b, uint64_t *ust, uint64_t *msc);

#define RASTER_JOBS 4u
/* Per-frame descriptor budget; larger init storage does not enlarge UI work.
 * Layout must bind a compact table within this budget (unused slots count). */
#define RASTER_FRAME_GLYPH_LIMIT 4096u

/* UI-only diagnostic snapshot. Worker timings arrive through work messages.
 * Times are CLOCK_MONOTONIC ns, durations are explicit; no allocation. */
typedef struct raster_metrics {
    uint32_t frame_id, jobs, present_kind, present_mode;
    uint32_t fence_sequence, present_sequence;
    uint64_t submit_ns, ready_ns, present_ns, issue_ns, server_ns;
    uint64_t strip_ns[RASTER_JOBS], queue_ns[RASTER_JOBS];
    uint64_t upload_issue_ns[RASTER_JOBS], upload_queued_ns;
} raster_metrics;
bool raster_frame_metrics(const render_backend *b, raster_metrics *out);

/* Kernel inputs: row-major cells for the whole grid, glyph table and page
 * descriptors (already validated by render_grid_validate). alpha_or is OR-ed
 * into every output pixel (0xff000000 for 32-bit ARGB windows). */
typedef struct raster_scene {
    render_dims dims;
    const render_cell *cells;
    const render_glyph *glyphs;
    size_t glyph_count;
    const render_atlas_page *pages;
    size_t page_count;
    uint32_t alpha_or;
} raster_scene;

/* Render one cell row (cell_h pixel rows) into dst, whose row 0 is the
 * first pixel row of the requested cell row; stride is in pixels. Callers of a
 * full surface pass surface + row * cell_h * stride. Pixels = 0x00RRGGBB | alpha_or.
 * Scalar is the reference; SSE2 must be pixel-exact equal. */
void raster_row_scalar(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row);
void raster_row_sse2(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row);
#define RASTER_PALETTE_SLOTS 64u
typedef struct raster_palette_entry {
    uint32_t fg, bg;
    bool valid;
    uint32_t pixels[256];
} raster_palette_entry;
typedef struct raster_palette {
    raster_palette_entry entries[RASTER_PALETTE_SLOTS];
    uint32_t rebuilds; /* reset per job; bounded misses fall back to direct SSE2 */
} raster_palette;
/* Exclusively owned per-job cache of exact SSE2 blend tables. Zero at init.
 * Tables depend on colours only, so atlas edits do not invalidate them. */
void raster_row_cached(const raster_scene *s, uint32_t *dst, size_t stride_px,
                       uint32_t row, raster_palette *palette);
/* Aligned 8/16-pixel cells: complete cache lines with streaming stores.
 * Other sizes/alignment use the regular SSE2 kernel. Includes the store fence. */
void raster_row_sse2_stream(const raster_scene *s, uint32_t *dst, size_t stride_px, uint32_t row);

/* Same row-local destination and exact pixels, with a stop check before each
 * cell and each <=256-pixel batch. False means interrupted: do not upload the
 * partial row. NULL stop completes the row. Cache belongs to the worker. */
typedef bool (*raster_stop_fn)(void *user);
bool raster_row_cached_cancellable(const raster_scene *s, uint32_t *dst,
    size_t stride_px, uint32_t row, raster_palette *palette,
    raster_stop_fn stop, void *user);

/* Visit dirty-row ordinals [lo,hi) of the strip list, in order. */
typedef void (*raster_row_fn)(void *user, uint32_t row);
void raster_for_rows(const render_strip *strips, size_t count, uint32_t lo, uint32_t hi,
                     raster_row_fn fn, void *user);
/* Ordinal range of job `job` of `njobs` over `total` rows (balanced, contiguous). */
void raster_partition(uint32_t total, uint32_t njobs, uint32_t job, uint32_t *lo, uint32_t *hi);
#endif
