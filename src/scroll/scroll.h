/* P3.4: caller-owned, deterministic vertical scrolling. No allocation/globals. */
#ifndef EDITOR_SCROLL_H
#define EDITOR_SCROLL_H
#include <stdbool.h>
#include <stdint.h>
#include "view/view.h"
#include "lineidx/lineidx.h"

#define SCROLL_PIXEL_UNIT 256u
#define SCROLL_WHEEL_UNIT 256u /* same units as plat_event.dy / xi2_result.dy */
#define SCROLL_CORE_ROWS 3u
#define SCROLL_SCAN_BUDGET 65536u
enum { SCROLL_OK = 0, SCROLL_MORE = 1, SCROLL_EOF = 2, SCROLL_ERR_ARG = -1, SCROLL_ERR_SOURCE = -2 };
typedef struct scroll_config { uint32_t rows, row_height, margin; } scroll_config;
typedef struct scroll_extent { uint64_t bytes, lines; bool exact; } scroll_extent;
typedef enum scroll_request { SCROLL_READY, SCROLL_RELATIVE, SCROLL_LINE, SCROLL_BYTE } scroll_request;
typedef struct scroll_state {
    scroll_config config;
    scroll_extent extent;
    uint64_t first_line, first_byte, subrow_q8;
    /* Private request/anchor fields. Logical adapters anchor at a line start;
     * scroll_visual anchors at an exact visual-row boundary instead. */
    uint64_t anchor_line, target_byte, pending_rows;
    scroll_request request;
    bool approximate, pending_up;
} scroll_state;
typedef enum scroll_key { SCROLL_PAGE_UP, SCROLL_PAGE_DOWN, SCROLL_HOME, SCROLL_END } scroll_key;

/* Pure state transitions; errors leave state/output unchanged. rows/height
 * must be nonzero; lines >= 1; bytes <= LINEIDX_MAX_LEN. Heights <= 2^24-1.
 * Unknown counts do not clamp a deep target to a density estimate. */
int scroll_init(scroll_state *s, scroll_config config, scroll_extent extent);
int scroll_resize(scroll_state *s, scroll_config config);
int scroll_set_extent(scroll_state *s, scroll_extent extent);
int scroll_pixels(scroll_state *s, int64_t delta_q8);
/* Both core and XI2 use three rows per notch; XI2 retains sub-notch precision.
 * Do not round, divide by 256, or feed emulated core events a second time. */
int scroll_wheel(scroll_state *s, int32_t delta);
int scroll_seek_line(scroll_state *s, uint64_t line);
int scroll_seek_byte(scroll_state *s, uint64_t byte);
/* Map to view_command motions AND move viewport one page/document boundary.
 * shift selection is the caller's view_command argument. No cursor mutation. */
int scroll_key_motion(scroll_state *s, scroll_key key, view_key *motion);
/* Call after completed edits/motions only, with the cursor's row ordinal in
 * the same coordinate system as first_line. Never call after pure scrolling.
 * Margins shrink to (rows-1)/2. Fully visible rows, including sub-row clipping. */
int scroll_follow(scroll_state *s, uint64_t cursor_line);

/* Renderer handoff. Reserve config.rows + 1 layout rows at setup, including
 * cells, row metadata and wrap plans; visible surface geometry stays unchanged.
 * The renderer adds origin_y_q8 to every text/gutter/cursor/selection row,
 * clips to [0, clip_width) x [0, clip_height), and leaves fixed chrome/sidebar
 * outside this transform. Damage uses this same transform, rounding outward
 * to pixels AFTER clipping. full_damage includes sub-row-only movement.
 * previous is the last submitted plan, NULL for the first frame. */
typedef struct scroll_frame_plan {
    uint64_t first_byte, first_row;
    int64_t origin_y_q8;
    uint32_t clip_width, clip_height, row_height, layout_rows;
    bool full_damage;
} scroll_frame_plan;
int scroll_plan_frame(const scroll_state *s, uint32_t width, uint32_t height,
                      const scroll_frame_plan *previous, scroll_frame_plan *out);
/* y_q8 is viewport-local; rejects clipped coordinates. Applies the exact
 * inverse drawing transform, including the bottom overscan row. */
int scroll_frame_hit(const scroll_frame_plan *p, int64_t y_q8,
                     uint32_t *row, uint64_t *within_q8);

/* Wrapped adapter: ordinals count VISUAL rows, never logical lines. The host
 * supplies a stable snapshot of its wrap geometry (width/tab/gutter/indent)
 * and a row count (an estimate when exact=false). One cluster produces at most
 * one row, so rows <= bytes+1. Unknown counts never limit deep navigation.
 * row_at and locate read bounded resident metadata or return MORE; they must
 * never scan/fault/allocate unboundedly on UI. A lazy producer retains its own
 * continuation and publishes through work mailboxes. Returned descriptors are
 * exact, usable to seed layout_begin_visual; viewport seeds use leading
 * affinity. locate honors trailing affinity at a soft boundary. The source
 * row_at past EOF returns SCROLL_EOF with the exact last row; this publishes
 * the exact extent and clips the full viewport. locate at bytes returns the
 * last row (OK). The source lease outlives all calls; a reflow/edit changes
 * generation and rebinds it.
 * No logical index/resident adapter may be called on viewport below. */
typedef struct scroll_visual_row {
    uint64_t ordinal, byte, end, line_start, line, column;
    uint64_t next, end_column, last;
    uint32_t indent;
    bool newline, continuation;
} scroll_visual_row;
typedef struct scroll_visual_source {
    void *ctx;
    uint64_t bytes, rows, generation;
    int (*row_at)(void *ctx, uint64_t ordinal, scroll_visual_row *out);
    int (*locate)(void *ctx, uint64_t byte, bool trailing, scroll_visual_row *out);
    bool exact;
} scroll_visual_source;
typedef struct scroll_visual {
    scroll_state viewport;
    scroll_visual_source source;
    scroll_visual_row first;
    uint64_t reflow_byte, reflow_rows;
    bool reflow;
} scroll_visual;
typedef struct scroll_visual_resolver {
    scroll_visual before, next;
    uint64_t cursor_byte;
    uint32_t phase;
    bool active, following, trailing;
} scroll_visual_resolver;
/* Initial seed is exact visual row zero, obtained outside the typing path. */
int scroll_visual_init(scroll_visual *s, scroll_config config,
                       const scroll_visual_source *source, scroll_visual_row first);
int scroll_visual_wheel(scroll_visual *s, int32_t delta);
int scroll_visual_pixels(scroll_visual *s, int64_t delta_q8);
int scroll_visual_key_motion(scroll_visual *s, scroll_key key, view_key *motion);
/* Reflow preserves the byte anchor, maps it with leading affinity in the new
 * geometry, then clips at EOF. Rebind after an edit with a host-transformed
 * first.byte. Source retirement belongs to the host, outside this adapter. */
int scroll_visual_resize(scroll_visual *s, scroll_config config,
                         const scroll_visual_source *source);
/* Zero-initialized caller-owned resolver. <= min(query_budget,256) callbacks
 * per slice; zero chooses 2. Absolute deadline, zero selects entry+0.5 ms.
 * Callback MORE and errors preserve s; a changed intent/source cancels old
 * progress. Input must be checked between slices. Publish/render only OK. */
int scroll_visual_resolve_slice(scroll_visual *s, scroll_visual_resolver *r,
                                uint32_t query_budget, uint64_t deadline_ns);
int scroll_visual_follow_slice(scroll_visual *s, scroll_visual_resolver *r,
                               uint64_t cursor_byte, bool trailing,
                               uint32_t query_budget, uint64_t deadline_ns);

/* Caller-owned continuation, zero-initialized at setup. Private fields; no
 * allocation. A new state/source/index cancels the previous pending operation.
 * Source bytes and index must remain unchanged between slices (publication is
 * allowed); zero this object after a source edit or cancellation. */
typedef struct scroll_resolver {
    scroll_state before, next;
    lineidx *index;
    lineidx_src source;
    uint64_t cursor, pos, end, start, left, spent, count, block_end, nth;
    uint64_t query_byte, query_line, slot, walk_rows;
    uint32_t phase, follow_phase, scan_kind;
    bool active, following, coarse, query_exact, query_active, walk_up;
} scroll_resolver;
/* The complete operation shares <= budget bytes, <= 256 source callbacks,
 * and an absolute CLOCK_MONOTONIC deadline (0: entry + 0.5 ms). budget=0
 * chooses the default 64 KiB slice; positive budgets are literal. Each callback
 * must be resident/nonblocking and bounded; bytes returned are capped at 4 KiB.
 * MORE leaves s unchanged, retaining progress only in resolver/index. Repeat
 * after checking input. Errors discard the continuation and preserve s. */
int scroll_resolve_slice(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                         const lineidx_src *source, uint64_t budget, uint64_t deadline_ns);
int scroll_follow_cursor_slice(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                               const lineidx_src *source, uint64_t cursor_byte,
                               uint64_t budget, uint64_t deadline_ns);
/* Compatibility one-slice calls. For fragmented sources or multi-stage
 * operations use the caller-owned continuation API to ensure progress. */
int scroll_resolve(scroll_state *s, lineidx *index, const lineidx_src *source,
                   uint64_t budget);
int scroll_follow_cursor(scroll_state *s, lineidx *index,
                         const lineidx_src *source, uint64_t cursor_byte);
/* Resident source bridge: immutable snapshot bytes are copied on WORK_BULK
 * into two fixed caller-owned windows and adopted ONLY through work mailboxes.
 * Init/close are setup/maintenance operations. Keep source.ctx/backing alive
 * until close returns OK; release ownership remains with the caller. Never
 * use a raw mapped source with the foreground slice API. A missing window
 * keeps navigation pending (MORE); failures preserve the viewport. */
#define SCROLL_RESIDENT_WINDOWS 2u
#define SCROLL_RESIDENT_MSG UINT32_C(0x5343524c)
struct scroll_resident;
typedef struct scroll_resident_window {
    struct scroll_resident *owner;
    work_handle handle;
    uint64_t start;
    size_t length, filled;
    uint32_t id, generation, state;
    int error;
    uint8_t bytes[SCROLL_SCAN_BUDGET];
} scroll_resident_window;
typedef struct scroll_resident {
    work_pool *pool;
    lineidx_src snapshot;
    scroll_resident_window windows[SCROLL_RESIDENT_WINDOWS];
    uint32_t generation, replacement;
    bool pending, closing;
} scroll_resident;
int scroll_resident_init(scroll_resident *r, work_pool *pool, const lineidx_src *snapshot);
/* Bounded mailbox adoption; called by the resident slice wrappers too. */
void scroll_resident_poll(scroll_resident *r);
int scroll_resident_close(scroll_resident *r);
int scroll_resolve_resident(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                            scroll_resident *resident, uint64_t budget, uint64_t deadline_ns);
int scroll_follow_cursor_resident(scroll_state *s, scroll_resolver *resolver, lineidx *index,
                                  scroll_resident *resident, uint64_t cursor_byte,
                                  uint64_t budget, uint64_t deadline_ns);
#endif
