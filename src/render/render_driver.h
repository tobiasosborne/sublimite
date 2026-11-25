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

typedef enum render_command_state {
    RENDER_COMMAND_EMPTY, RENDER_COMMAND_READY, RENDER_COMMAND_TAKEN
} render_command_state;

typedef struct render_driver_port {
    _Atomic render_command_state command;
    const render_packet *packet;
    const os_wake *wake, *ui_wake;
    render_scene *scene;
    render_rc (*apply)(render_scene *, const render_packet *, render_install, void *, render_scene_view *);
    render_feedback feedback; /* optional UI mailbox view, installed during open */
} render_driver_port;

/* The owner drains the command wake, then claims READY once. Acquisition
 * pins the packet before accessing it, including before bind/consume. UI
 * cancellation can retract READY, while TAKEN belongs to the owner. Keep
 * the claim across retries and until every worker using the packet has
 * physically finished. Release exactly once and make no later packet
 * reads. The UI wake requests a quiesce recheck and is not evidence. */
static inline const render_packet *render_driver_take(render_driver_port *p)
{
    render_command_state expected = RENDER_COMMAND_READY;
    if (!atomic_compare_exchange_strong_explicit(&p->command, &expected, RENDER_COMMAND_TAKEN,
                                                  memory_order_acquire, memory_order_relaxed)) return NULL;
    return p->packet;
}

static inline void render_driver_release(render_driver_port *p)
{
    const os_wake *wake = p->ui_wake;
    ASSERT(atomic_load_explicit(&p->command, memory_order_relaxed) == RENDER_COMMAND_TAKEN);
    atomic_store_explicit(&p->command, RENDER_COMMAND_EMPTY, memory_order_release);
    os_wake_signal(wake);
}

static inline bool render_driver_borrowed(const render_driver_port *p)
{
    return atomic_load_explicit(&p->command, memory_order_acquire) == RENDER_COMMAND_TAKEN;
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

/* Query/open are setup controls. A terminal open error releases any partial
 * native resources. MORE retries setup when runnable, WAIT after its UI wake.
 * Bind/consume/poll run on the presentation owner. Bind completes the first
 * packet's sheet before consume. Consume applies the common scene before
 * drawing and issues present once. Keep the claimed command and current
 * phase on MORE/WAIT. MORE resumes that phase when scheduled. WAIT resumes
 * on the relevant native readiness, worker/mailbox-space or cancellation wake.
 * Poll returns one typed notice on OK, MORE for runnable work, WAIT when no
 * notice is ready. Never retake, re-upload or reissue completed work. A
 * terminal failure after acceptance is LOST.
 *
 * Park an unpublished mailbox result with work_continue and retry only
 * publication. Preserve the leased argument until work_handle_finished,
 * including cancellation, parked continuations and completion without a
 * message. Recheck after wakes and drain stale results. Pool jobs check
 * work_should_stop at least every 5 ms CPU, using the bound in work.h.
 * Put command, cancellation and native readiness on the owner's root
 * wait set. Use cancellable native waits and a bounded disconnect or
 * missing-feedback path. After issue arm the platform contract's absolute
 * monotonic completion watchdog for missing device or display evidence.
 * A disconnect or expired obligation yields LOST. An elapsed deadline
 * cannot authorize reuse before physical quiescence.
 *
 * Quiesce is a nonblocking UI control. RETIRE_FRAME waits for physical
 * packet leases. STOP_EPOCH also halts native use, unregisters observers
 * and drains stale sources. WAIT arranges a UI wake. OK means the owner
 * has released the claim and stopped using the affected storage. Close
 * follows STOP_EPOCH and joins any remaining owner thread before freeing
 * its storage. A fresh epoch binds again on its owner.
 *
 * Authenticate the native cookie, epoch/frame, serial/window and clock.
 * ISSUED means successful native present submission. For XShm DEVICE_DONE,
 * observe the matched server device fence triggered after the drawable
 * uploads on the presentation connection. Raster job completion or source
 * reuse alone does not establish T5. For GL, observe the matched device sync.
 * X11 display evidence needs matching
 * PIXMAP/MSC and displayed mode, or a matched discard. A Wayland frame
 * callback only schedules work. The original swap presentation feedback
 * determines PRESENTED or DISCARDED. Encode notices with render.h's
 * clock/ns rules, including UNTIMED and zero for unconvertible presentation
 * times. Driver/context calls run on the owner.
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
