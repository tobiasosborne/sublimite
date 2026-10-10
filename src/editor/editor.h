#ifndef EDIT_EDITOR_H
#define EDIT_EDITOR_H
#include "render/render.h"
#include "x11/plat.h"
#include "view/view.h"
#include "ipc/ipc.h"
#include "tabs/tabs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define EDITOR_SLICE_NS UINT64_C(500000)
#define EDITOR_BLINK_NS UINT64_C(500000000)
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
} editor_frame;
typedef struct editor_config {
    const char *path;                 /* existing file; NULL uses initial bytes */
    const uint8_t *initial;
    size_t initial_len;
    const char *journal_path;         /* new/empty file; NULL disables journaling */
    uint32_t cols, rows, font_px;      /* defaults: 120, 40, 15 */
    uint32_t max_cols, max_rows;       /* defaults: at least 360, 300 */
    size_t arena_bytes, history_keys; /* open-time bounds; zero selects defaults */
    size_t tab_capacity, closed_capacity; /* defaults: 128 live, 16 retained */
    bool start_empty;                 /* startup request supplies initial tabs */
    int wrap_mode;                    /* 0=file default, -1=off, +1=on */
    ipc_server *server;               /* borrowed; UI-owned, fini after close */
    bool (*allow_close)(void *, uint64_t id, bool modified);
    void *hook_ctx;
    void (*on_ingress)(void *, uint64_t sequence, uint64_t ns);
    void (*on_submit)(void *, const editor_frame *);
    void (*on_present)(void *, const editor_frame *);
    /* Instrumentation boundary for platform/present/completion IO, outside
     * our mutation/layout/submit slices. true enters IO, false leaves it.
     * Allows a process-wide guard to exclude XCB packets when a key spans
     * multiple turns. No key mutation/layout run/backend submit occurs inside
     * it. IPC open/setup and bounded index maintenance also run there. */
    void (*on_io)(void *, bool entering);
} editor_config;
typedef struct editor_stats {
    uint64_t input_sequence, submitted_sequence, presented_sequence;
    uint64_t mutations, journal_records, poll_returns, blinks, input_checks;
    uint64_t slices, longest_slice_ns;
    uint64_t unimplemented_actions, rejected_commands, minimap_fills, minimap_ns;
    size_t tabs, active_tab;
    bool minimap_stale;
    bool focused, blinking, cursor_visible, render_active, pending;
    int journal_error, error_cause; /* underlying module code on stopped loop */
} editor_stats;

/* UI-owned, no copies. The factory-filled backend is borrowed exclusively and
 * outlives editor_close. Open reserves memory, starts workers, opens/maps the
 * file and initialises the backend on a worker. No GL dependency in the loop.
 * Close is quiescent, blocking and releases workers before their storage. */
int editor_open(editor **out, const editor_config *config, render_backend *backend);
void editor_close(editor *e);
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
 * may allocate and rotate a complete current-state checkpoint before exit. */
int editor_flush(editor *e);
/* Allocating open path, UI thread; synchronous acceptance/partial-open rollback.
 * Requests and payloads are copied before return. token=0 for local startup.
 * Closed tabs retain their resources for reopen, but release wait membership. */
int editor_open_request(editor *e, const ipc_request *request, ipc_token token);
int editor_add_buffer(editor *e, const char *path, const uint8_t *bytes, size_t len,
                      uint64_t *id);
int editor_select_tab(editor *e, size_t index);
int editor_close_tab(editor *e, size_t index);
int editor_reopen_tab(editor *e);
const tabs_tab *editor_tab(const editor *e, size_t index);
/* Borrowed composed grid, inspect only after the loop settles. */
const render_grid *editor_grid(const editor *e);
#endif
