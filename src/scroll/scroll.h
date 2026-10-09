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

/* Explicit lineidx/source adapter. No allocation; budget <= 65536. A nonzero
 * seek budget publishes a bounded prefix via lineidx_seek_line (UI only).
 * Resolve each pending event before another transition. Approximate relative
 * scrolling walks physical newlines from the byte anchor. Idle publication
 * corrects ONLY its line label, preserving bytes/pixels and cursor placement.
 * A fresh extent is adopted here without clamping/moving the existing anchor.
 * Source must match index and remain stable throughout the call. */
int scroll_resolve(scroll_state *s, lineidx *index, const lineidx_src *source,
                   uint64_t budget);
/* Post-motion/edit policy for callers holding a cursor BYTE. Physical row
 * counting near the viewport avoids mixing different density estimates.
 * Far cursors use a bounded line-start seed and physical margin backtracking.
 * Call only after resolve, with request == SCROLL_READY. MORE means a bounded
 * scan could not prove the cursor's line/margins: retain the pending motion
 * and retry after index publication. MORE preserves the physical viewport.
 * A new pure scroll cancels that caller-owned pending-follow intent. */
int scroll_follow_cursor(scroll_state *s, lineidx *index,
                         const lineidx_src *source, uint64_t cursor_byte);
#endif
