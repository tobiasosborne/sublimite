/* P2.0 frozen cell-grid contract; changes require a new bead. */
#ifndef EDIT_RENDER_H
#define EDIT_RENDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct plat;
struct work_pool;
struct work_msg;
struct edit_arena;

enum {
    RENDER_OK = 0, RENDER_ERR_ARG = -1, RENDER_ERR_BOUNDS = -2,
    RENDER_ERR_CAPACITY = -3, RENDER_ERR_STATE = -4, RENDER_ERR_FRAME = -5,
    RENDER_ERR_STRIPS = -6, RENDER_ERR_CELL = -7, RENDER_ERR_BUSY = -8,
    RENDER_ERR_INIT = -9, RENDER_ERR_UNSUPPORTED = -10, RENDER_ERR_DEVICE = -11
};
#define RENDER_NO_SLOT UINT32_MAX
#define RENDER_ATTR_BOLD      UINT16_C(0x001)
#define RENDER_ATTR_ITALIC    UINT16_C(0x002)
#define RENDER_ATTR_UNDERLINE UINT16_C(0x004)
#define RENDER_ATTR_INVERSE   UINT16_C(0x008)
#define RENDER_ATTR_WIDE_LEFT UINT16_C(0x010)
#define RENDER_ATTR_WIDE_RIGHT UINT16_C(0x020)
#define RENDER_ATTR_CURSOR    UINT16_C(0x040)
#define RENDER_ATTR_SELECTION UINT16_C(0x080)
#define RENDER_ATTR_MASK      UINT16_C(0x0ff)

/* Colours are opaque sRGB 0x00RRGGBB (high byte must be zero). Coverage is
 * linear R8; blend each channel as (fg*a + bg*(255-a) + 127)/255.
 * glyph_index is caller-defined glyph/cluster identity (not a codepoint).
 * atlas_slot indexes grid.glyphs; RENDER_NO_SLOT means background only and
 * requires glyph_index=0. reserved must be zero. 20 bytes, no pointers. */
typedef struct render_cell {
    uint32_t glyph_index, atlas_slot, fg, bg;
    uint16_t attrs, reserved;
} render_cell;
_Static_assert(sizeof(render_cell) == 20, "frozen render_cell size");

typedef struct render_dims {
    uint32_t cols, rows, cell_w, cell_h;
} render_dims;

/* General R8 page, including the font_ascii_atlas's 95-cell-wide single row
 * and FONT_ATLAS_PAGE_DIM square runtime pages from font_atlas_alloc.
 * pixels_len >= stride*(height-1)+width; stride >= width; nonzero dimensions.
 * Page index is independent of font's page numbering; layout maps it here.
 * Pixel storage stays alive and referenced glyph rectangles stay immutable
 * until that frame's T5 hook (or quiescent shutdown). Appending disjoint glyph
 * rectangles is allowed; never modify bytes a pending frame may sample. A
 * backend may copy/upload pixels earlier but caller uses T5 as the reuse bound. */
typedef struct render_atlas_page {
    const uint8_t *pixels;
    size_t pixels_len, stride;
    uint32_t width, height;
} render_atlas_page;

/* Top-left, pixel (not normalised UV) rectangle of a precomposed cell image.
 * font_place_in_cell applies metrics/bearings before packing; zero-width
 * combining marks merge into that image before submit. A wide glyph's image
 * may span two cells. Backend derives UVs using the selected page dimensions.
 * Bold/italic select distinct images in layout; backends do not synthesise.
 * Empty glyphs use RENDER_NO_SLOT, rather than zero-sized rectangles. */
typedef struct render_glyph {
    uint32_t glyph_index, page, x, y, w, h;
} render_glyph;

typedef struct render_strip {
    uint32_t first_row, row_count; /* full-width, half-open row run */
} render_strip;

typedef struct render_grid {
    render_dims dims;
    render_cell *cells;          /* exactly cols*rows active cells, row-major */
    size_t cell_capacity;
    uint64_t *dirty;             /* bit r%64 in dirty[r/64]; padding ignored */
    size_t dirty_word_capacity;
    const render_atlas_page *pages;
    size_t page_count;
    const render_glyph *glyphs;
    size_t glyph_count;
    uint32_t frame_id;           /* trace's uint32_t; 0 reserved for startup */
    bool full_frame, begun;
} render_grid;

/* UI thread only (pure read-only helpers may also operate on an exclusively
 * owned worker snapshot). No allocation. Nonzero dims, pixel extents <=
 * INT32_MAX. Capacities are in elements. Caller owns ALL referenced storage.
 * Init does not clear cells; initialise every cell before first submit.
 * Begin requires a strictly increasing nonzero ID, clears damage, preserves
 * cells/atlas bindings. No ID wrap; restart a quiescent grid/backend at UINT32_MAX.
 * Mark full after scroll/resize/tab switch/expose; partial edits mark rows.
 * Failure leaves state/output unchanged, except strips' out_count below. */
int render_grid_init(render_grid *g, render_dims dims, render_cell *cells,
                     size_t cell_capacity, uint64_t *dirty, size_t dirty_capacity);
int render_frame_begin(render_grid *g, uint32_t frame_id);
int render_mark_rows(render_grid *g, uint32_t first_row, uint32_t row_count);
int render_mark_full(render_grid *g);
/* Minimal contiguous dirty row runs, or one full-height run when full_frame.
 * out_count is required; it receives required count even on ERR_CAPACITY.
 * NULL output with capacity 0 is a sizing query (OK only when count is 0).
 * Output is untouched on error; capacity ceil(rows/2) always suffices.
 * Pure: never clears damage. Zero damage submits/presents a valid no-op frame. */
int render_dirty_strips(const render_grid *g, render_strip *out, size_t capacity,
                        size_t *out_count);
/* Checks dimensions/storage, all atlas rectangles and ALL active cells.
 * Wide-left must be followed within the same row by wide-right; continuation
 * must have no glyph and matching colours/non-width attrs. No orphan halves.
 * Layout clips wide characters at viewport edges using blank/replacement cells.
 * Render all backgrounds, then glyphs, then underlines (last pixel row).
 * Inverse swaps fg/bg. Cursor/selection colours are resolved by layout; those
 * bits are semantic only. A continuation never draws a second glyph. Any
 * change to a slot/image used by retained cells must damage their rows too. */
int render_grid_validate(const render_grid *g);

/* T5/T6 hooks receive CLOCK_MONOTONIC ns and the ORIGINAL frame ID, once each.
 * Default hooks below call trace_record_at (ns!=0), or trace_record (ns==0).
 * Register every recording thread with trace_thread_register before use.
 * Hooks must not allocate, block, re-enter render, or mutate UI state.
 * The common backend adapter delivers hooks on UI, asynchronously in event()
 * for real backends. Raw trace hooks may also be used on a registered worker;
 * worker completions/state changes must still cross a src/work mailbox.
 * Correlate X UST/device clocks to CLOCK_MONOTONIC before supplying timestamps.
 * Notifications observed inside present are held until it succeeds; immediate
 * T6 timestamps are also clamped to T4 (null backend).
 * T5 is max(device fence completion, T4 present submission), NOT XShm source
 * reuse. T6 is matching-frame PresentCompleteNotify (separate from T5). */
typedef void (*render_timestamp_hook)(void *user, uint32_t frame_id, uint64_t ns);
typedef struct render_hooks {
    render_timestamp_hook device_done, present_complete;
    void *user;
} render_hooks;
void render_trace_device_done(void *user, uint32_t frame_id, uint64_t ns);
void render_trace_present_complete(void *user, uint32_t frame_id, uint64_t ns);

#define RENDER_CAP_DEVICE_TIMING UINT32_C(1)
#define RENDER_CAP_PRESENT_TIMING UINT32_C(2)
#define RENDER_CAP_HEADLESS UINT32_C(4)
#define RENDER_CAP_GPU UINT32_C(8)
#define RENDER_CAP_RASTER_POOL UINT32_C(16)

typedef struct render_backend_info {
    const char *name;            /* immutable, factory lifetime */
    size_t state_size, state_align; /* caller allocates before init */
    uint32_t capabilities;
} render_backend_info;

typedef struct render_config {
    render_dims dims;
    uint32_t max_width, max_height; /* init-reserved surface pixel extents */
    size_t max_cells, max_glyphs, max_pages; /* init-time snapshot capacities */
    size_t max_atlas_bytes;      /* resource bound: sum of page pixels_len */
    struct edit_arena *arena;    /* backend allocation at init only; may be NULL */
    struct plat *platform;      /* borrowed; NULL only for headless backend */
    struct work_pool *workers;  /* borrowed; CPU MT4 jobs use WORK_RASTER */
    render_hooks hooks;          /* NULL functions use trace defaults */
} render_config;

enum render_event_kind {
    RENDER_EVENT_WORK, RENDER_EVENT_DEVICE_DONE, RENDER_EVENT_PRESENT_COMPLETE
};
typedef struct render_event {
    enum render_event_kind kind;
    uint32_t frame_id;
    uint64_t ns;                /* 0 = observe now; not raw UST */
    const struct work_msg *work; /* WORK only, borrowed for call duration */
} render_event;

struct render_backend;
/* Low-level implementor interface; clients use common wrappers below so
 * validation, queue depth, stats and tracing are identical for every backend.
 * All entries required; factory fills an instance, no global vtable needed.
 * ops receive b->state and may signal via render_backend_signal on UI.
 * submit must not signal before it returns; present/event may signal on UI.
 * event handles routed work messages; frame fence/Present events are handled
 * by the common adapter. Return UNSUPPORTED for unrecognised work messages. */
typedef struct render_backend_ops {
    int (*init)(struct render_backend *b, const render_config *config);
    int (*resize)(struct render_backend *b, render_dims dims);
    int (*submit)(struct render_backend *b, const render_grid *g,
                  const render_strip *strips, size_t count);
    int (*present)(struct render_backend *b, uint32_t frame_id);
    int (*event)(struct render_backend *b, const render_event *event);
    void (*shutdown)(struct render_backend *b);
} render_backend_ops;

/* Adapter counters saturate at UINT64_MAX. Cells count damaged rows*cols,
 * including continuation/background cells, not glyph instances. */
typedef struct render_stats {
    uint64_t submitted_frames, presented_frames, submitted_cells, submitted_strips;
} render_stats;

typedef struct render_backend {
    render_backend_info info;
    render_backend_ops ops;
    void *state;
    render_config config;
    render_stats stats;
    /* Adapter-owned fields. Caller/backends must not edit these. */
    uint32_t last_frame, active_frame;
    uint64_t submitted_ns, device_ns, complete_ns;
    bool initialized, active, presented, device_seen, complete_seen, t5_sent, t6_sent;
    bool full_required;
} render_backend;

/* Factory/query may run on UI or init worker, on exclusive storage. Factory
 * requires a zeroed or shut-down instance, never a live backend. */
int render_null_backend(render_backend *b);
int render_backend_query(const render_backend *b, render_backend_info *out);
/* INIT: worker only; may block/dlopen/create GL context, never UI. state is
 * caller-owned, aligned to info.state_align, >=info.state_size bytes. Exclusive
 * backend/config/arena access until result is published through a work mailbox.
 * Failed init must clean up everything it acquired and leave adapter uninitialised;
 * caller can select CPU factory + fresh state after any GL init error or --cpu.
 * Backends unable to deliver fence and matching-frame completion must fail init
 * with UNSUPPORTED rather than leave the one-frame slot permanently busy.
 * Initialisation is the only allocation opportunity. GL transfers/detaches its
 * context before init returns, and binds it to UI for submit/present.
 * QUIESCENT SHUTDOWN: UI only, after input has stopped; may block. It can
 * work_cancel owned jobs (UI-only in src/work), then drain/join those jobs and
 * release the UI-bound GL context. Caller stops submissions first and keeps
 * state/hooks/platform/workers alive until backend joins/cancels its jobs and
 * drains fences. Shutdown discards pending callbacks and is idempotent.
 * RESIZE/SUBMIT/PRESENT/EVENT/STATS/SIGNAL: UI only after init handoff.
 * Resize only while idle, within max_cells/max_width/max_height; first
 * subsequent frame must be full. Caller updates grid dims/storage separately.
 * Submit requires grid dims == configured dims and EXACT canonical strips.
 * Success finishes reading or COPIES grid cells, strips, glyph table AND page
 * descriptors into init-reserved backend storage before returning. No borrowed
 * grid/metadata pointers survive submit. R8 pixel storage alone may be borrowed
 * through T5 under the page immutability rule above (avoids copying all atlas
 * pages on every key). Thus UI may begin/fill N+1 cells/metadata immediately
 * after submit returns, while CPU workers get the backend-owned immutable
 * snapshot and immutable cached atlas pixels. Pool workers publish results through src/work, UI routes messages
 * into event(); workers never modify adapter/UI state. A backend may read only
 * dirty cells for rendering, but must preserve undamaged pixels/buffer age.
 * Queue depth is one until both T5 and T6 are delivered: subsequent submit
 * returns BUSY, never queues. UI can coalesce edits into its next grid meanwhile.
 * IDs strictly increase on successful submit; rejected frames are retryable.
 * present may return BUSY while MT4 work is unfinished; UI retries on work event.
 * Successful present records T4 in src/trace BEFORE delivering any hooks. It
 * submits exactly once. Backend errors retain the frame for retry or shutdown;
 * failed submit retains no caller pointers/jobs and does not consume an ID.
 * Zero-damage frames still produce T4/T5/T6 (real backends acknowledge retained
 * content through their present path). No printf or allocation on these calls.
 * Frame/device/present notifications carry the frame ID, not only a fence slot.
 * Stale/duplicate signals return FRAME/STATE and never release a newer frame. */
int render_backend_init(render_backend *b, const render_config *config,
                        void *state, size_t state_bytes);
int render_backend_resize(render_backend *b, render_dims dims);
int render_backend_submit(render_backend *b, const render_grid *g,
                          const render_strip *strips, size_t count);
int render_backend_present(render_backend *b, uint32_t frame_id);
int render_backend_event(render_backend *b, const render_event *event);
int render_backend_signal(render_backend *b, enum render_event_kind kind,
                         uint32_t frame_id, uint64_t ns);
int render_backend_stats(const render_backend *b, render_stats *out);
void render_backend_shutdown(render_backend *b);
#endif
