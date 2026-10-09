#ifndef EDITOR_VIEW_H
#define EDITOR_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "piece/piece.h"
#include "utf8/utf8.h"

#define VIEW_SCAN_BOUND 4096u
#define VIEW_WINDOW 4096u
#define VIEW_MUTATION_BOUND 2048u
#define VIEW_SLICE_NS UINT64_C(200000)
#define VIEW_PREFERRED_UNSET UINT64_MAX
enum { VIEW_OK = 0, VIEW_MORE = 4, VIEW_ERR_BUSY = 5, VIEW_ERR_ARG = 6 };
typedef enum view_key {
    VIEW_LEFT, VIEW_RIGHT, VIEW_WORD_LEFT, VIEW_WORD_RIGHT,
    VIEW_UP, VIEW_DOWN, VIEW_PAGE_UP, VIEW_PAGE_DOWN,
    VIEW_HOME, VIEW_END, VIEW_DOC_HOME, VIEW_DOC_END,
    VIEW_SELECT_ALL, VIEW_SELECT_LINE, VIEW_SELECT_WORD,
    VIEW_BACKSPACE, VIEW_DELETE, VIEW_WORD_BACKSPACE, VIEW_WORD_DELETE,
    VIEW_TYPE
} view_key;

/* Pointer-free per-tab journal payload. A selection is separate so future
 * versions can store an array of these without changing its representation. */
typedef struct view_selection { uint64_t cursor, anchor, preferred_col; } view_selection;
typedef struct view_state {
    view_selection selection;
    uint64_t first_line, first_byte, hscroll;
    bool approximate;
    bool wrap; /* per-buffer flag; view_init remains wrap-off for old callers */
    bool visual_end; /* cursor affinity at a soft row end */
    uint64_t visual_byte; /* first visible visual row */
} view_state;

/* Optional exact checkpoint seed. Return 1 with a grapheme boundary and its
 * exact column on `line`, <= byte/column target; return 0 for no checkpoint.
 * No allocation; bounded work; never seed beyond the requested target.
 * col_target==UINT64_MAX means byte lookup, byte_target==UINT64_MAX column.
 * Caller invalidates its checkpoints AFTER each reported edit, before resume. */
typedef int (*view_checkpoint_fn)(void *ctx, const piece_tree *tree,
    uint64_t line, uint64_t byte_target, uint64_t col_target,
    uint64_t *byte, uint64_t *col);
typedef struct view_config {
    uint32_t tab_width, rows, cols; /* zero defaults: 4, 24, 80 */
    view_checkpoint_fn checkpoint;
    void *checkpoint_ctx;
} view_config;
typedef struct view_change {
    uint64_t offset, old_len, new_len;
    bool changed; /* also inspect on errors/MORE: a successful edit prefix */
} view_change;
struct undo_log;

/* Caller-owned runtime, not journal data. Scratch fields are private. */
typedef struct view {
    view_state state;
    view_config config;
    piece_tree *tree;
    uint64_t scanned; /* bytes consumed/examined in most recent call <= bound */
    bool busy, shift, edited, word_found;
    view_key key;
    unsigned phase, scan_mode;
    uint64_t origin, target, lo, hi, line, end, result, column;
    uint64_t scan_pos, cluster_start, scan_col, run_start;
    uint64_t win_pos;
    size_t win_len;
    int run_class, cluster_class;
    utf8_cseg cluster;
    struct layout *wrap_layout;
    bool wrap_target_soft;
    uint8_t win[VIEW_WINDOW + 4u];
    view_state before_state, completed_state;
    uint64_t completed_len;
    uint64_t deadline_ns, deadline_check_work, column_start, last_seed;
    struct { uint64_t byte, col, start; bool valid; } seeds[16];
    unsigned seed_next;
    uint32_t seed_tab;
    struct undo_log *undo;
    bool undo_group_open;
    view_state restore_state;
    bool cursor_before, anchor_before;
    /* Private cooperative line queries: no opaque exact-count calls. */
    struct { uint64_t byte, line; bool valid; } lines[32];
    struct { uint64_t target, result; unsigned kind; } queries[16];
    unsigned line_next, query_next, query_kind;
    uint64_t query_target, query_pos, query_line;
    uint64_t home_start, visual_column;
    uint32_t visual_indent;
    /* Owned TYPE bytes for a deferred small insertion. */
    uint8_t pending_text[VIEW_WINDOW];
    size_t pending_len;
    uint64_t bulk_offset, bulk_remaining, bulk_target;
    bool bulk_insert;
} view;

void view_init(view *v, piece_tree *tree, const view_config *config);
/* Commands/resumes allocate no libc storage. View decoding and line queries
 * use at most VIEW_SCAN_BOUND charged byte work per call. Deadlines are checked
 * between bounded chunks; scheduling/page faults and opaque piece/layout calls
 * are outside this work bound. Uncached P4 layout queries retain their own API.
 * MORE: keep the tree stable, process this call's change, invalidate affected
 * checkpoints, then call view_continue or view_cancel before another command.
 * TYPE consumes all input synchronously; no caller pointer is retained.
 * Selection capture above VIEW_MUTATION_BOUND is sliced. Small TYPE input is
 * copied into runtime scratch before an initial unchanged MORE; larger input
 * is inserted synchronously under the legacy API. Each continuation reports
 * ONLY its own successful replacement prefix in that call's tree coordinates.
 * Process changes on MORE/errors too; cancellation accepts any committed prefix.
 * During edited repair/capture MORE the visible selection/viewport is at byte 0.
 * Columns and segmentation are exact and resumable. Resume following before
 * drawing. Cancellation restores movement's prior completed state, or a visible
 * byte-zero fallback after edits, preserving the buffer's wrap flag.
 * Empty TYPE on unchanged completed state is a no-op. Legacy editor callers
 * that install a new repair target or change tree length still get bounded
 * repair/follow; use notify/restore for arbitrary external mutations. */
/* All APIs with a change output clear a nonnull output even on argument errors. */
int view_command(view *v, view_key key, bool shift, const uint8_t *text,
                 size_t len, view_change *change);
int view_continue(view *v, view_change *change);
void view_cancel(view *v);
bool view_busy(const view *v);
/* Optional mutation routing, attach before editing; log/tree must match and
 * neither may be busy. No ownership transfer. Each editing command is one
 * explicit undo group (including replacement and a committed error prefix).
 * Group completion waits for repair/follow or cancellation; before/after blobs
 * contain cursor then anchor as two uint64_t values. Detach only while idle;
 * direct edits after detaching require caller undo_clear before reattachment.
 * Replay/external edits require cancellation before mutation and normalized
 * state/checkpoints before commands; view_restore/view_notify_edit provide it. */
int view_set_undo(view *v, struct undo_log *log);
typedef enum view_affinity { VIEW_AFFINITY_BEFORE, VIEW_AFFINITY_AFTER } view_affinity;
/* After external mutation: cancel pending work BEFORE changing the tree and
 * invalidate external checkpoints. Supply the replacement tuple in coordinates
 * of the old tree; view still holds its old selection/viewport. Endpoints before
 * the range stay put; endpoints beyond it shift by the byte delta; an endpoint
 * in the removed range (or at an insertion) chooses its start/end by affinity.
 * The old nonempty range's right endpoint maps to the new right endpoint.
 * Repair joined clusters toward BEFORE/AFTER, reset preferred column, normalize
 * viewport, follow the cursor. MORE resumes through view_continue, cancellation
 * accepts external text with the byte-zero fallback. No mutation/undo recording
 * and no emitted change. Invalid calls clear a nonnull output and keep state.
 * Wrapped following inherits the layout-query limitation noted above. */
int view_notify_edit(view *v, const view_change *edit, view_affinity cursor_affinity,
                     view_affinity anchor_affinity, view_change *change);
/* Untrusted journal/undo state: clamp endpoints to EOF, repair forward to full
 * cluster boundaries, reset preferred column and validate/follow the viewport.
 * Restored byte offsets refer to the CURRENT tree (no rebasing). */
int view_restore(view *v, const view_state *state, view_change *change);
/* Tiny P4.1 defaults; NULL/empty = untitled. Config overrides are P6.4. */
bool view_wrap_default(const char *path);
/* Apply open-time defaults to both the buffer flag and its layout. Reserve
 * layout_wrap_init first for text/untitled buffers; begin after this call. */
int view_wrap_file(view *v, struct layout *context, const char *path);
/* Bind caller-owned layout query context; configured text width must match
 * layout's text area. Toggle resets preferred column and horizontal scroll. */
int view_set_wrap(view *v, bool enabled, struct layout *context);
#endif
