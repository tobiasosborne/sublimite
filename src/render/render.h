#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "base/base.h"
#include "os/os.h"
#include "render/render_types.h"

typedef struct render render;
struct work_pool;
typedef struct plat_native_binding plat_native_binding;
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
 * wake is required and is signalled by the driver on UI progress. The
 * command wake in render_driver.h is separate and owned by the service.
 * recovery_slots is the positive directory inspection grant per accepted
 * input-free offer, when runtime slots are reserved. */
typedef struct render_setup {
    render_limits limits;
    render_driver driver;
    plat_native_binding *native;
    struct work_pool *workers;
    const os_wake *wake;
    size_t recovery_slots;
    void *directory_ctx;
    render_image_state (*directory)(void *, render_generation, uint32_t, render_image_id *);
} render_setup;

/* Slot-ordered views. The first ascii_count entries identify printable
 * ASCII directly in code order. A valid settled marker for each cell span
 * follows that range. Bind borrows these descriptors and pixels only for
 * its call and copies the checked sheet into prepaid candidate storage. */
#define RENDER_ASCII_COUNT (0x7eu - 0x20u + 1u)
typedef struct render_sheet {
    const render_tile_view *tiles;
    size_t count, ascii_count;
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
    RENDER_CLOSED, RENDER_OPENING, RENDER_UNBOUND, RENDER_IDLE_FULL, RENDER_IDLE,
    RENDER_ACCEPTED, RENDER_ISSUED, RENDER_RETIRING, RENDER_QUIESCING, RENDER_CLOSING
} render_state;
typedef enum render_device { RENDER_WAIT_FENCE, RENDER_DEVICE_DONE } render_device;
typedef enum render_presentation { RENDER_WAIT_DISPLAY, RENDER_PRESENTED, RENDER_DISCARDED } render_presentation;
typedef enum render_clock { RENDER_UNTIMED, RENDER_MONOTONIC, RENDER_SYNTHETIC } render_clock;
typedef enum render_notice_kind {
    RENDER_NOTICE_ISSUED, RENDER_NOTICE_DEVICE_DONE, RENDER_NOTICE_PRESENTED,
    RENDER_NOTICE_DISCARDED, RENDER_NOTICE_LOST
} render_notice_kind;

/* Platform/worker adapter authenticates cookie, surface/window, epoch/frame
 * and clock. A LOST with frame zero is a device/epoch disconnect from the live
 * owner, including between flights. All other evidence names the live flight.
 * Early evidence is private until issue. Null timings are synthetic. */
typedef struct render_notice {
    render_notice_kind kind;
    render_frame_id id;
    render_clock clock;
    uint64_t ns;
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
    render_rc rejected;
    render_need rejection;
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
/* Open prepays/touches storage. Set *out=NULL for first open. MORE/WAIT
 * returns an OPENING handle. Retry with the same inputs after progress or
 * close it. A terminal initial error restores the arena mark and clears
 * the handle. The arena interval belongs exclusively to the service. */
render_rc render_open(base_arena *, const render_setup *, render **);
/* A valid candidate is staged alongside the last accepted active binding.
 * Only its first accepted full packet adopts it. A rejected bind/submit
 * preserves active ancestry and tiles. A zero client waits for configure.
 * During loss, retry bind after quiescence wakes the UI. */
render_rc render_bind(render *, const render_binding *, const render_sheet *, render_need *);
/* At a UI boundary notify every settlement, reassignment or removal.
 * A matching active or staged generation records one FIFO node per slot.
 * Replacement of an owed tile preserves its ordinal. Non-INK unlinks it.
 * Watermark credit excludes an accepted prefix without a node walk. */
void render_image_changed(render *, render_generation, uint32_t slot);
/* One bounded structural validation and copy, with a single publication
 * stamp check and BUSY before staging. Sanitizer and RENDER_ASSERT_IDENTITIES
 * builds also verify current slot/version per changed ink cell. An in-range
 * stale image in release samples background on a residency mismatch.
 * One refusal may record diagnostics but spends no frame/input/credit.
 * Keep producer marks across refusals. Publish layout_present and the saved
 * pointer transform for this exact accepted candidate in the same UI step.
 * A first, rebind, tab or unknown-ancestry picture needs a full offer.
 *
 * Copy 20Z + 4U + L bytes for changed cells, region and tight R8 deltas.
 * Inspect at most N+1 FIFO candidates for N deltas. Read only declared
 * dimensions for the first candidate past the byte grant. An accepted
 * input-free offer also queues up to recovery_slots directory entries for
 * subsequent delivery. Pending schedules positive-grant repair. */
render_rc render_submit(render *, const render_offer *, const render_source *,
                        render_frame_id *out, render_need *);
/* Deliver authenticated notices on UI. Device and presentation independently
 * reach terminal states. T5=max(T4,fence), T6=max(T4,display) with a correlated
 * timestamp. Discard and uncorrelated presentation are untimed. Quiesce must
 * acknowledge physical retirement before reuse. No time is synthesized on loss.
 * Frame numbers increase across epochs and exhaustion retires the service. */
render_rc render_event(render *, const render_notice *);
/* WAIT uses the setup wake. Retry open, submit, bind or close as applicable.
 * Delivery/recovery at idle requests an input-free repair. */
bool render_pending(const render *, render_wake *);
/* Counts describe the last accepted frame and current delivery directory.
 * Rejected/rejection describe the most recent refusal independently.
 * stats(NULL) is CLOSED. */
void render_stats(const render *, render_counters *);
/* Retry MORE/WAIT until the owner is physically quiescent. Final OK releases
 * the arena interval and invalidates the handle. NULL is fine. */
render_rc render_close(render *);

/* XShm and GL factories are implemented by their respective backend modules.
 * Null owns a private R8 atlas and consumes on its owner. */
render_driver render_driver_null(void);
render_driver render_driver_xshm(void);
render_driver render_driver_gl33(void);
#endif
