#ifndef EDIT_EDITOR_H
#define EDIT_EDITOR_H
#include "render/render.h"
#include "x11/plat.h"
#include "view/view.h"
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
    EDITOR_ERR_HISTORY = -104
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
    void *hook_ctx;
    void (*on_ingress)(void *, uint64_t sequence, uint64_t ns);
    void (*on_submit)(void *, const editor_frame *);
    void (*on_present)(void *, const editor_frame *);
    /* Instrumentation boundary for platform/present/completion IO, outside
     * our mutation/layout/submit slices. true enters IO, false leaves it.
     * Allows a process-wide guard to exclude XCB packets when a key spans
     * multiple turns. No text mutation/layout/submit occurs inside it. */
    void (*on_io)(void *, bool entering);
} editor_config;
typedef struct editor_stats {
    uint64_t input_sequence, submitted_sequence, presented_sequence;
    uint64_t mutations, journal_records, poll_returns, blinks, input_checks;
    uint64_t slices, longest_slice_ns;
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
 * events and injected events take the same command/layout/submit path. */
int editor_inject(editor *e, const plat_event *event);
/* One UI turn, checking input between bounded layout/view continuations.
 * timeout < 0 may sleep indefinitely; 0 never sleeps. MORE means runnable
 * work remains; OK means wait for input/worker/timer; CLOSED means WM close.
 * Submit callbacks precede present/completion handling and journal pumping.
 * Hooks must not re-enter the editor. Our typing code allocates nothing;
 * XCB's event/reply allocations occur in the platform/completion pump.
 * Mutation failures stop subsequent turns; flush/close accepts any reported
 * successful prefix, with no automatic rollback or partial replay retry. */
int editor_step(editor *e, int timeout_ms);
int editor_run(editor *e);
/* Setup/test navigation; byte must be a grapheme boundary. */
int editor_set_cursor(editor *e, uint64_t byte);
int editor_jump_line(editor *e, uint64_t line);
uint64_t editor_length(const editor *e);
int editor_read(const editor *e, uint64_t off, uint8_t *dst, size_t len);
view_state editor_view(const editor *e);
editor_stats editor_get_stats(const editor *e);
bool editor_index_complete(editor *e);
uint64_t editor_line_count(const editor *e);
/* Blocking journal flush, outside the typing path. */
int editor_flush(editor *e);
#endif
