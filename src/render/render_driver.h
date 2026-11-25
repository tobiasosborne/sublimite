#ifndef RENDER_DRIVER_H
#define RENDER_DRIVER_H

/* Owner-side interface. The app uses render.h. */
#include <stdatomic.h>
#include "render/render.h"

/* Accepted packet, including its binding. Read-only through physical
 * retirement. Cells concatenate all complete rows in canonical run order.
 * Run i begins at cols times the sum of preceding runs' row_count. The
 * trailing overscan follows the last run with overscan_count cells when
 * overscan_dirty. Pad its absent columns with the surface background.
 * RGB on REPLACE is tightly packed RGB32 with stride region.width. KEEP
 * and REMOVE carry no RGB payload. Tile offsets index packed R8 rows with
 * stride delta.width and no padding; clip sampling to width and height.
 * sheet is present on the first full packet of its epoch. */
typedef struct render_packet {
    render_frame_id id;
    render_offer offer;
    render_binding binding;
    const render_sheet *sheet;
    render_rect damage, region;
    const render_strip *runs;
    const render_cell *cells;
    const uint32_t *region_rgb;
    const render_delta *deltas;
    const uint8_t *r8;
    size_t overscan_count, tiles;
} render_packet;

typedef struct render_scene render_scene;
typedef struct render_resident { uint32_t version, width, height, frame; } render_resident;

/* Owner borrow until next apply/quiescence. cells are dense including the
 * extra row. Redraw flags tag source rows with the current packet's frame.
 * Damage also includes margins, old/new clips and the independent fixed
 * region. region_rgb is meaningful for a nonempty region, tightly packed.
 * The atlas writer receives dense checked indices and tight bytes. Real
 * backends can install them directly on their device without a CPU shadow. */
typedef struct render_scene_view {
    const render_cell *cells;
    const render_resident *installed;
    const uint32_t *row_frames, *region_rgb;
    render_rect damage, region;
} render_scene_view;
typedef render_rc (*render_install)(void *, size_t slot, render_delta, const uint8_t *);

typedef struct render_driver_port {
    _Atomic(const render_packet *) command;
    const os_wake *wake;
    render_scene *scene;
    render_rc (*apply)(render_scene *, const render_packet *, render_install, void *, render_scene_view *);
} render_driver_port;

/* The owner drains wake, then takes the command exactly once. Hold its
 * pointer over MORE/WAIT and check physical lease finish before reuse. A
 * wake is a scheduling hint. The slot publishes with release/acquire. */
static inline const render_packet *render_driver_take(render_driver_port *p)
{
    return atomic_exchange_explicit(&p->command, NULL, memory_order_acquire);
}

static inline bool render_has_slot(render_slot_range r, uint32_t slot)
{
    return slot >= r.first && slot - r.first < r.count;
}
/* Dense destination of a slot already checked against the binding. */
static inline size_t render_atlas_index(const render_binding *b, uint32_t slot)
{
    return render_has_slot(b->sheet, slot) ? slot - b->sheet.first :
        (size_t)b->sheet.count + (slot - b->runtime.first);
}

/* Shared ink lookup. A right half samples its left owner with a cell
 * offset. A false result contributes zero ink and keeps the footprint.
 * Call with a checked cell index in the applied binding. */
static inline bool render_scene_ink(const render_scene_view *s, const render_binding *b,
                                    size_t index, size_t *slot, uint32_t *x)
{
    bool right = (s->cells[index].attrs & RENDER_ATTR_WIDE_RIGHT) != 0;
    render_cell owner = s->cells[index - (size_t)right];
    *x = right ? b->cell_width : 0;
    if (owner.atlas_slot == RENDER_NO_SLOT) return false;
    *slot = render_atlas_index(b, owner.atlas_slot);
    render_resident v = s->installed[*slot];
    return v.version == owner.glyph_index && *x < v.width;
}

typedef enum render_quiescence { RENDER_RETIRE_FRAME, RENDER_STOP_EPOCH } render_quiescence;

/* Query/open are setup controls. Open installs the owner's port; a terminal
 * open error must first release any partial native resources. Bind/consume/
 * poll run on the presentation owner. Bind runs once to completion for the
 * first packet's sheet, then consume applies the common scene before draw
 * and issues present once. Failure on an accepted command is LOST.
 *
 * Park an unpublished mailbox result with work_continue, retry publication
 * without repeating draw/issue and keep draining stale results. Quiesce is
 * a nonblocking UI control: RETIRE_FRAME waits for the packet's physical
 * leases, STOP_EPOCH also stops native use, unregisters and drains sources.
 * WAIT means the owner arranges a UI wake. Pool drivers check work_handle_finished
 * even without a message. Close follows STOP_EPOCH. A fresh epoch binds
 * again on the owner and may recreate native resources there.
 *
 * Authenticate swap/fence identity, native serial/window and clock before
 * forwarding. X11 accepts matching PIXMAP/MSC and displayed mode, Wayland
 * the original feedback object. Driver/context calls run on the owner.
 * Restore backing when swap-buffer age is unknown, including after discard.
 * GL R8 uses top-left source rows (account for upload orientation) and
 * unpack alignment 1. Match raster's integer RGB resolve, inverse after
 * styling, underline on the final local scanline. Per channel use
 * (fg*a+bg*(255-a)+127)/255. At q=-origin_y_q8 sample resolved text at
 * y+floor(q/256), and the successor only for nonzero f=q mod 256. Mix
 * ((256-f)*c0+f*c1+128)/256. Clip to the client, paint margins in background,
 * then fixed layers without the text transform. Disable filtering, sRGB
 * conversion, dithering and implicit blending. */
typedef struct render_driver_ops {
    render_backend_caps (*query_caps)(const render_setup *);
    render_rc (*open)(void **, base_arena *, const render_setup *, render_driver_port *);
    render_rc (*bind)(void *, const render_binding *, const render_sheet *);
    render_rc (*consume)(void *, const render_packet *);
    render_rc (*poll)(void *, render_notice *);
    render_rc (*quiesce)(void *, render_quiescence);
    void (*close)(void *);
} render_driver_ops;
#endif
