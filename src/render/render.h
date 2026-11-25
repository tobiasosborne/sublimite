#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "base/base.h"
#include "os/os.h"
#include "render/render_types.h"

typedef struct render render;
struct work_pool;
struct plat_native;
struct render_driver_ops;
typedef struct render_driver { const struct render_driver_ops *ops; } render_driver;

typedef enum render_rc {
    RENDER_OK, RENDER_MORE, RENDER_WAIT, RENDER_BUSY, RENDER_STALE, RENDER_INVALID,
    RENDER_CAPACITY, RENDER_UNSUPPORTED, RENDER_LOST
} render_rc;

typedef enum render_field {
    RENDER_FIELD_NONE, RENDER_FIELD_BINDING, RENDER_FIELD_STAMP, RENDER_FIELD_WIDTH,
    RENDER_FIELD_HEIGHT, RENDER_FIELD_COLS, RENDER_FIELD_ROWS, RENDER_FIELD_CELL_WIDTH,
    RENDER_FIELD_CELL_HEIGHT, RENDER_FIELD_CELLS, RENDER_FIELD_TARGET,
    RENDER_FIELD_SHEET, RENDER_FIELD_RUNTIME, RENDER_FIELD_ATLAS, RENDER_FIELD_RUNS,
    RENDER_FIELD_OVERSCAN, RENDER_FIELD_CELL, RENDER_FIELD_REGION_WIDTH,
    RENDER_FIELD_REGION_HEIGHT, RENDER_FIELD_REGION_PIXELS, RENDER_FIELD_REGION,
    RENDER_FIELD_TILE, RENDER_FIELD_TILE_DESCRIPTORS, RENDER_FIELD_TILE_BYTES,
    RENDER_FIELD_EPOCH
} render_field;
typedef struct render_need { render_field field; size_t required, supported; } render_need;

/* Hard prepaid maxima. target_bytes includes actual target pitch. Atlas
 * sizes are also checked as slot count times the largest double-cell tile.
 * Backend caps project stricter native limits onto these common fields. */
typedef struct render_limits {
    uint32_t client_width, client_height, cols, rows, cell_width, cell_height;
    size_t cells, target_bytes, sheet_slots, runtime_slots, atlas_bytes;
    size_t region_width, region_height, region_pixels, tile_descriptors, tile_bytes;
} render_limits;
typedef struct render_backend_caps { render_limits limits; bool synthetic; } render_backend_caps;

typedef enum render_image_state {
    RENDER_IMAGE_VACANT, RENDER_IMAGE_QUEUED, RENDER_IMAGE_EMPTY, RENDER_IMAGE_INK
} render_image_state;
typedef struct render_cell_span { const render_cell *cells; size_t count; } render_cell_span;

/* UI-owned for one non-reentrant submit. Each callback is a bounded,
 * constant-time descriptor/metadata read. current returns the live stamp
 * for this candidate, including its ancestry and association. Producer data
 * remains immutable until the call returns. Only get_tile lends R8 bytes. */
typedef struct render_source {
    void *ctx;
    render_stamp (*current)(void *ctx);
    render_rc (*get_run)(void *ctx, size_t index, render_strip *out);
    render_rc (*get_row)(void *ctx, uint32_t source_row, render_cell_span *out);
    render_rc (*get_region)(void *ctx, render_region_view *out);
    render_image_state (*get_identity)(void *ctx, uint32_t slot, render_image_id *out);
    render_rc (*get_tile)(void *ctx, render_image_id expected, render_tile_view *out);
} render_source;

/* The persistent UI identity reader is used by image_changed and recovery.
 * It reads the same directory as the offer adapter. It never lends pixels.
 * The caller retains ctx, native binding, wake and workers through close.
 * recovery_slots is a positive per-pump inspection grant if runtime slots
 * are reserved. Owner-thread operations that can wait use wake. */
typedef struct render_setup {
    render_limits limits;
    render_driver driver;
    const struct plat_native *native;
    struct work_pool *workers;
    const os_wake *wake;
    size_t recovery_slots;
    void *directory_ctx;
    render_image_state (*directory)(void *, render_generation, uint32_t, render_image_id *);
} render_setup;

/* Exactly one checked view for each sheet slot, in slot order. The two
 * marker IDs designate settled artwork for each span. Bind copies the sheet
 * into backend storage. Keep it alive and immutable through any retry. */
typedef struct render_sheet {
    const render_tile_view *tiles;
    size_t count;
    render_image_id marker[RENDER_TILE_MAX_SPAN];
} render_sheet;

typedef enum render_mode { RENDER_PATCH, RENDER_FULL } render_mode;
/* Full-width sorted, disjoint, maximal row runs. The extra row (which may
 * supply fewer than cols cells) is independent of visible runs. FULL needs
 * one complete visible run, enabled overscan and explicit region disposition.
 * layer_damage is a conservative physical rectangle (empty is all zeros).
 * Replace/move/remove adds old and new region extents. Origin/clip changes
 * add both text clips. A background change invalidates the whole client.
 * Tile grants may be zero; a repair can submit zero runs and no new input. */
typedef struct render_offer {
    render_stamp stamp;
    render_mode mode;
    uint32_t cols, rows, background;
    size_t run_count, tile_descriptors, tile_bytes;
    bool overscan_dirty;
    render_region_mode region;
    render_rect layer_damage;
    render_scroll scroll;
} render_offer;

typedef enum render_state {
    RENDER_CLOSED, RENDER_OPENING, RENDER_UNBOUND, RENDER_BINDING, RENDER_IDLE_FULL,
    RENDER_IDLE, RENDER_ACCEPTED, RENDER_ISSUED, RENDER_QUIESCING, RENDER_CLOSING
} render_state;
typedef enum render_device { RENDER_WAIT_FENCE, RENDER_DEVICE_DONE } render_device;
typedef enum render_presentation {
    RENDER_WAIT_DISPLAY, RENDER_PRESENTED, RENDER_DISCARDED
} render_presentation;
typedef enum render_clock { RENDER_UNTIMED, RENDER_MONOTONIC, RENDER_SYNTHETIC } render_clock;
typedef enum render_notice_kind {
    RENDER_PUMP, RENDER_NOTICE_ISSUED, RENDER_NOTICE_DEVICE_DONE, RENDER_NOTICE_PRESENTED,
    RENDER_NOTICE_DISCARDED, RENDER_NOTICE_RELEASED, RENDER_NOTICE_LOST
} render_notice_kind;

/* Null oracle reads. Each sample owns its original cell footprint and the
 * coverage it would fetch (NULL for a version mismatch or inkless cell).
 * Wide right reads the left image at x_offset. Extents are for the whole
 * packed image, with reads clipped to extents. Borrow through the next
 * driver operation or close. Output damage conservatively bounds redraw. */
typedef struct render_sample {
    render_cell cell;
    const uint8_t *coverage;
    uint32_t width, height, x_offset;
} render_sample;
typedef struct render_draw_report {
    size_t scan_cells, drawn_cells, drawn_rows, copied_tile_bytes;
    uint64_t scan_ns; /* null-only elapsed R-A scan, before drawing */
    render_rect damage, region;
    const render_sample *samples; /* row-major, includes reserved overscan */
    const uint32_t *region_rgb;  /* tight, current region */
} render_draw_report;

/* Platform/worker adapter authenticates native cookie, surface/window,
 * epoch/frame and clock before forwarding. RELEASED proves physical
 * completion of every job using the packet. Early terminal evidence is
 * retained privately until issue succeeds. Null timestamps are synthetic. */
typedef struct render_notice {
    render_notice_kind kind;
    render_frame_id id;
    render_clock clock;
    uint64_t ns;
    render_draw_report draw; /* ISSUED only */
} render_notice;

typedef struct render_wake {
    const os_wake *wake;
    bool runnable, waiting, recovery, delivery;
} render_wake;
typedef struct render_counters {
    render_state state;
    render_device device;
    render_presentation presentation;
    render_clock issue_clock, device_clock, presentation_clock;
    render_frame_id accepted;
    render_stamp stamp;
    uint64_t t4, t5, t6, watermark;
    size_t queued, recovery_inspections;
    size_t rows, runs, cells, region_pixels, tile_inspections, tiles, tile_bytes;
    render_draw_report draw;
} render_counters;

/* Setup and all service functions are serialized by the UI owner. No call
 * may reenter the service from a source or driver callback. Inputs are
 * call-scoped except setup lifetimes and a bind in progress. Error details
 * are returned through need, which may be NULL.
 *
 * Capacity is established off the input path. For V=cols*(rows+overscan)
 * and T=2*cell_width*cell_height, reserve V cells and ceil(rows/2) run
 * records, max region_pixels RGB32 elements, tile_bytes payload bytes and
 * tile_descriptors records. A binding with runtime slots requires room for
 * one T-byte tile and descriptor. Driver resources are separate. Use the
 * actual target stride, and include both banks if reprovision overlaps. */
render_rc render_measure(const render_binding *, const render_limits *,
                         const render_backend_caps *, render_need *);
/* Open prepays and touches both packet and backend storage. MORE/WAIT gives
 * a handle advanced by PUMP. A failed initial open restores the arena mark.
 * The arena interval belongs exclusively to this service through close. */
render_rc render_open(base_arena *, const render_setup *, render **);
/* Bind only at idle or unbound. Retry the same binding and sheet on
 * MORE/WAIT from the driver. A zero client suspends admission and returns
 * WAIT until the caller supplies a drawable binding. Success starts a
 * fresh epoch and requires a complete next frame. */
render_rc render_bind(render *, const render_binding *, const render_sheet *, render_need *);
/* Notify at each UI settlement, replacement or removal. A generation
 * mismatch or non-runtime slot is ignored. An owed replacement keeps its
 * FIFO ordinal. Vacant/QUEUED/EMPTY unlinks it. Delivery credit uses the
 * accepted watermark, so committing a prefix never visits old nodes.
 * PUMP checks at most recovery_slots directory indices per idle step,
 * once per epoch. Notifications deduplicate with replay and with credit.
 * The front end schedules positive-grant repairs while delivery is owed. */
void render_image_changed(render *, render_generation, uint32_t slot);
/* One bounded check/validate/copy/commit. BUSY precedes staging. Any
 * refusal preserves the prior scene, stamps, output ID and delivery queue.
 * The UI may immediately reuse source cells, descriptors, RGB and R8 bytes
 * on success. The sealed backing survives until both terminal axes and
 * physical release, or quiescence on LOST. Apply layout_present for the
 * exact accepted publication in this same UI step. The caller keeps its
 * pending change marks across refusals and supplies full initialization
 * for a new tab, binding or unknown ancestor. It pairs pointer events
 * with the accepted publication and its saved transform/geometry serial.
 * The last accepted frame is the damage base, even after discard.
 *
 * Copy 20Z + 4U + L bytes for Z changed cells, U changed region elements
 * and L tight R8 bytes, plus runs and selected descriptors. Validate only
 * the indicated rows and tiles. Inspect at most N+1 FIFO nodes for N
 * copied tiles. The grant may leave owed cells background-only until a
 * tile-only repair. A stable zero-tile edit needs no directory sweep.
 * Source current is read before prep and again at seal. Its paint serial
 * covers eviction and regeneration of even inherited rows. */
render_rc render_submit(render *, const render_offer *, const render_source *,
                        render_frame_id *out, render_need *);
/* PUMP advances opening, quiescence, bounded directory recovery or one
 * driver step. Re-drive while runnable. On loss stop admission, quiesce,
 * discard credits, then bind and rebuild with a full current picture.
 * T5=max(T4,fence), T6=max(T4,matched display); discard is untimed.
 * A matching presented result with no correlated time is also untimed.
 * Frame callbacks and ordinary wakes only allow progress. Device done
 * alone and presentation alone cannot retire a frame. Physically released
 * work may still wait on either. ID exhaustion forces quiescence before
 * reuse. In QUIESCING/CLOSING, late evidence cannot release resources. */
render_rc render_event(render *, const render_notice *);
/* A waiting service needs its wake. A runnable bind or close is retried
 * with its original arguments. Delivery means a repair can be offered. */
bool render_pending(const render *, render_wake *);
/* Counts refer to the last accepted frame, except delivery/recovery fields
 * which describe the current epoch. Refusals are reported through need/rc.
 * Keep the service live for borrowed draw records. stats(NULL) is CLOSED. */
void render_stats(const render *, render_counters *);
/* Stop admission and retire physical work. Retry on MORE/WAIT. Final OK
 * releases the arena interval and invalidates the handle. NULL is fine. */
render_rc render_close(render *);

/* XShm and GL factories are implemented by their respective backend modules.
 * Null owns a private R8 atlas and runs only when pumped. */
render_driver render_driver_null(void);
render_driver render_driver_xshm(void);
render_driver render_driver_gl33(void);
#endif
