#ifndef EDIT_EDITOR_H
#define EDIT_EDITOR_H
#include "render/render.h"
#include "x11/plat.h"
#include "view/view.h"
#include "ipc/ipc.h"
#include "tabs/tabs.h"
#include "config/config.h"
#include "scroll/scroll.h"
#include "layout/layout.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define EDITOR_SLICE_NS UINT64_C(500000)
#define EDITOR_BLINK_NS UINT64_C(500000000)
/* (G) caret blink stops after 10 s idle: PRD.md section 6 ("Cursor blink stops
 * after 10 s idle") and gate G11, perf/01-perf-target.md section 0.2 (0 wakeups/s
 * after 10 s). Tests also reuse it as a settle watchdog: (E) for that use, no
 * gate bounds a settle (edit-dz0). */
#define EDITOR_IDLE_NS UINT64_C(10000000000)
#define EDITOR_INPUT_CAP 1024u
enum {
    EDITOR_OK = 0, EDITOR_MORE = 1, EDITOR_CLOSED = 2,
    EDITOR_ERR_ARG = -100, EDITOR_ERR_MEMORY = -101,
    EDITOR_ERR_INPUT_FULL = -102, EDITOR_ERR_IO = -103,
    EDITOR_ERR_HISTORY = -104, EDITOR_ERR_CAPACITY = -105
};
typedef struct editor editor;
typedef struct editor_frame {
    uint32_t id;
    uint64_t first_sequence, last_sequence;
    uint64_t ingress_ns, submit_ns, present_ns;
    scroll_frame_plan scroll; /* logical plan when !wrapped; wrap_seed otherwise */
    view_selection selection;
    layout_wrap_row wrap_seed;
    uint64_t source_id, source_generation, source_revision, geometry_generation;
    uint64_t sidebar_revision, sidebar_identity;
    bool wrapped;
} editor_frame;
typedef struct editor_refresh {
    uint32_t displayed_frame_id;
    uint64_t refresh_sequence, completion_ns;
    bool synthetic, opportunity_stream, metadata_valid;
    editor_frame frame; /* immutable submission, not the current viewport */
} editor_refresh;
typedef struct editor_config {
    const char *path;                 /* existing file; NULL uses initial bytes */
    const uint8_t *initial;
    size_t initial_len;
    const char *journal_path;         /* existing replays; session-* discovers orphans; NULL disables */
    /* Optional: the session lease journal_session_create already holds for
     * journal_path. Borrowed; open duplicates it, so the caller may close its
     * descriptor at any time after editor_open returns. */
    const int *session_lease;
    /* Optional nonblocking signalfd for termination, borrowed until close.
     * Caller blocks its signals before starting workers; the UI requests the
     * same clean close as a window close when this descriptor is readable. */
    const int *close_signal_fd;
    uint32_t cols, rows, font_px;      /* defaults: 120, 40, 15 */
    uint32_t max_cols, max_rows;       /* defaults: at least 360, 300 */
    size_t arena_bytes, history_keys; /* open-time bounds; zero selects defaults */
    size_t tab_capacity, closed_capacity; /* defaults: 128 live, 16 retained */
    bool start_empty;                 /* startup request supplies initial tabs */
    plat_backend platform_backend;      /* default X11; main selects from environment */
    bool raster_fallback;             /* raster paints while GPU init runs; no retry */
    int wrap_mode;                    /* 0=file default, -1=off, +1=on */
    ipc_server *server;               /* borrowed; UI-owned, fini after close */
    bool (*allow_close)(void *, uint64_t id, bool modified);
    void *hook_ctx;
    void (*on_ingress)(void *, uint64_t sequence, uint64_t ns);
    void (*on_submit)(void *, const editor_frame *);
    void (*on_present)(void *, const editor_frame *);
    /* UI only. Null completion is synthetic. Native completion alone has
     * opportunity_stream=false: repeats/drops need a display opportunity
     * producer calling editor_on_refresh, not a submit-derived timer. */
    void (*on_refresh)(void *, const editor_refresh *);
    /* Instrumentation boundary for platform/present/completion IO, outside
     * our mutation/layout/submit slices. true enters IO, false leaves it.
     * Allows a process-wide guard to exclude XCB packets when a key spans
     * multiple turns. No key mutation/layout run/backend submit occurs inside
     * it. IPC open/setup and bounded index maintenance also run there. */
    void (*on_io)(void *, bool entering);
    uint64_t copy_threshold;           /* 0 = file default; off-path test override */
    /* Display-derived startup: points use a true em. Zero dpi keeps the
     * legacy explicit pixel/baked test configuration; main queries X first.
     * Explicit points override file font.size; zero uses file/default points.
     * Pixel extents are optional. */
    double font_dpi, font_points;
    uint32_t window_width, window_height;
    /* Pixel extents above are a launch default (native startup's DPI policy):
     * a journalled WINDOW record restores over them. False = explicit size wins. */
    bool window_size_default;
    /* Reserve the display's pixel extents for resize/zoom, independent of
     * font size. Zero retains explicit-grid callers' existing reservation. */
    uint32_t max_window_width, max_window_height;
    bool load_preferences;            /* native startup enables XDG config */
    const char *preferences_path;     /* optional absolute setup/test override */
    /* Startup selection (native CLI): either non-NULL pointer overrides the
     * environment pair and config pair. Empty explicitly selects embedded.
     * Copied during open; file wins within the selected source. */
    const char *font_family, *font_file;
    /* edit-6e2: comma-separated font.fallbacks chain from the CLI. Non-NULL
     * (even empty) overrides EDIT_FONT_FALLBACKS and the config key. */
    const char *font_fallbacks;
} editor_config;
typedef struct editor_stats {
    uint64_t input_sequence, submitted_sequence, presented_sequence;
    uint64_t mutations, journal_records, poll_returns, blinks, input_checks;
    uint64_t slices, longest_slice_ns;
    uint64_t unimplemented_actions, rejected_commands, minimap_fills, minimap_ns;
    size_t tabs, active_tab;
    bool minimap_stale, source_stale, open_pending;
    bool focused, blinking, cursor_visible, render_active, pending;
    int journal_error, error_cause; /* underlying module code on stopped loop */
    uint64_t font_changes; /* adopted atlas generations, including failure fallback */
    int font_error;
    uint32_t font_fallback_skipped; /* edit-6e2: unresolvable chain entries, adopted version */
    bool font_pending;
    int backend_init_error;           /* failed GPU init, 0 if no fallback */
    uint64_t render_rejects;          /* edit-dzk1: submits the render adapter rejected */
    int render_reject_cause;          /* edit-dzk1: the last rejection's render code */
} editor_stats;

/* UI-owned, no copies. The factory-filled backend is borrowed exclusively and
 * outlives editor_close. Open reserves memory, starts workers, opens/maps the
 * file and initialises the backend on a worker. With raster_fallback, raster
 * (null for headless descriptors) paints while an independent GPU worker runs;
 * successful GPU adoption waits for bootstrap presentation/retirement. The loop routes native
 * completion callbacks and polls only an outstanding presented GPU frame.
 * Copy-mode files larger than the bounded prefix publish a read-only preview
 * while open_pending is true. Queued keys wait for whole-source adoption;
 * pending remains the frame/input-work flag, so a preview can settle.
 * Close is quiescent, blocking and releases workers before their storage. */
/* Queue a stale-source choice; processed off the typing path. Unsafe keep
 * leaves the source suspended; reload opens the current generation. */
int editor_source_resolve(editor *e, bool reload);
int editor_open(editor **out, const editor_config *config, render_backend *backend);
/* Setup only. NULL selects EGL; explicit gl/raster overrides for tests/benches.
 * Unknown names fail without changing the backend. Set raster_fallback at open
 * to paint on raster during EGL discovery and retain it after init failure.
 * No runtime retries. Strict no-fallback selection retains synchronous init. */
int editor_backend_select(render_backend *backend, const char *name);
void editor_close(editor *e);
/* Config-loader hook, UI-only copied request, allocation-free. file wins over
 * family; file#decimal selects a TTC face. Empty restores embedded DejaVu.
 * MORE means a previous family request is still pending: retry after stepping.
 * Selection and zoom share worker preparation and the frame-boundary swap;
 * an accepted request waits for retired Unicode jobs before version reuse. */
int editor_font_request(editor *e, const char *family, const char *file);
/* Fixed queue; stamp ingress when drained by step, not when injected. Native
 * events and injected events take the same command/layout/submit path.
 * CLOSE requests an immediate stop, even when command storage is full.
 * Shutdown flushes applied edits; commands still queued are not applied. */
int editor_inject(editor *e, const plat_event *event);
/* One UI turn, checking input between bounded layout/view continuations.
 * timeout < 0 may sleep indefinitely; 0 never sleeps. MORE means runnable
 * work remains; OK means wait for input/worker/timer; CLOSED means WM close.
 * Mutations reach page-cache recovery storage before yielding the turn or
 * acknowledging a frame. CLOSED acknowledges an explicit journal flush.
 * Submit callbacks precede present/completion handling and journal pumping.
 * Hooks must not re-enter the editor. Our typing code allocates nothing;
 * XCB's event/reply allocations occur in the platform/completion pump.
 * Mutation failures stop subsequent turns; flush/close accepts any reported
 * successful prefix, with no automatic rollback or partial replay retry. */
int editor_step(editor *e, int timeout_ms);
int editor_run(editor *e);
/* UI-only opportunity producer, including repeats and missing IDs (id=0).
 * Sequences strictly increase; timestamps are CLOCK_MONOTONIC and nondecreasing.
 * Reject stale/duplicate opportunities without changing backend ownership.
 * Unknown/retired IDs report metadata_valid=false. Hooks must not re-enter. */
int editor_on_refresh(editor *e, uint32_t displayed_frame_id,
                      uint64_t refresh_sequence, uint64_t completion_ns, bool synthetic);
/* Setup/test navigation; byte must be a grapheme boundary. */
int editor_set_cursor(editor *e, uint64_t byte);
/* An incomplete resident copy build is prioritized on the foreground worker;
 * returns EDITOR_MORE until publication. Mapping I/O and dirty-index repair
 * retain their background service; the caller retries after editor_step. */
int editor_jump_line(editor *e, uint64_t line);
uint64_t editor_length(const editor *e);
int editor_read(const editor *e, uint64_t off, uint8_t *dst, size_t len);
view_state editor_view(const editor *e);
editor_stats editor_get_stats(const editor *e);
bool editor_index_complete(editor *e);
uint64_t editor_line_count(const editor *e);
/* Blocking journal flush/checkpoint, outside the typing path. Failure recovery
 * may allocate and rotate a complete current-state checkpoint before exit.
 * edit-y315: an owed Save As checkpoint is written on a worker; the call waits
 * for it at most one journal tick (JOURNAL_DEFAULT_SYNC_NS) and returns
 * EDITOR_MORE if it is still running: the journal stays for recovery. */
int editor_flush(editor *e);
/* edit-ivv: editor_flush for exit paths (main's final flush, editor_close):
 * a JOURNAL_BUSY refusal from the shared directory guard is retried once
 * after one journal tick (JOURNAL_DEFAULT_SYNC_NS, the close_result bound). */
int editor_flush_settle(editor *e);
/* Allocating open path, UI thread; synchronous acceptance/partial-open rollback.
 * Requests and payloads are copied before return. token=0 for local startup.
 * Resolved paths already open activate their retained tab; origin coordinates
 * preserve its view. Non-origin coordinates reposition it. Independent wait
 * requests share that tab and finish when it closes.
 * Closed tabs retain their resources for reopen, but release wait membership. */
int editor_open_request(editor *e, const ipc_request *request, ipc_token token);
/* Existing resolved paths select their tab and return its retained ID. */
int editor_add_buffer(editor *e, const char *path, const uint8_t *bytes, size_t len,
                      uint64_t *id);
int editor_select_tab(editor *e, size_t index);
int editor_close_tab(editor *e, size_t index);
int editor_reopen_tab(editor *e);
const tabs_tab *editor_tab(const editor *e, size_t index);
/* Borrowed composed grid, inspect only after the loop settles. */
const render_grid *editor_grid(const editor *e);
/* Applied config, including font points/family for the DPI/font consumer.
 * Borrowed until the next UI turn; generation advances with the containing
 * frame, never by a delayed timer. NULL for a NULL editor. */
const config_settings *editor_preferences(const editor *e);
uint64_t editor_preferences_generation(const editor *e);
#endif
