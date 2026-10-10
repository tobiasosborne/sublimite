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
enum { SCROLL_OK = 0, SCROLL_MORE = 1, SCROLL_ERR_ARG = -1, SCROLL_ERR_SOURCE = -2 };
typedef struct scroll_config { uint32_t rows, row_height, margin; } scroll_config;
typedef struct scroll_extent { uint64_t bytes, lines; bool exact; } scroll_extent;
typedef enum scroll_request { SCROLL_READY, SCROLL_RELATIVE, SCROLL_LINE, SCROLL_BYTE } scroll_request;
typedef struct scroll_state {
    scroll_config config;
    scroll_extent extent;
    uint64_t first_line, first_byte, subrow_q8;
    /* Private request/anchor fields. The byte anchor is always a line start. */
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
