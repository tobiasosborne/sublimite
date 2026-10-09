#ifndef EDITOR_VIEW_H
#define EDITOR_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "piece/piece.h"
#include "utf8/utf8.h"

#define VIEW_SCAN_BOUND 65536u
#define VIEW_WINDOW 4096u
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
} view;

void view_init(view *v, piece_tree *tree, const view_config *config);
/* All commands/resumes do zero libc allocations, at most VIEW_SCAN_BOUND
 * byte work in view (piece line queries/mutations follow frozen piece API).
 * MORE: keep tree/input stable and call view_continue, or view_cancel before
 * another command. TYPE consumes its data synchronously; no pointer retained.
 * During a post-edit boundary repair MORE the visible selection is at byte 0.
 * Column fallback stops after 64 KiB and sets state.approximate; movement
 * segmentation itself is exact and resumable, never split/truncate a cluster. */
int view_command(view *v, view_key key, bool shift, const uint8_t *text,
                 size_t len, view_change *change);
int view_continue(view *v, view_change *change);
void view_cancel(view *v);
bool view_busy(const view *v);
/* Tiny P4.1 defaults; NULL/empty = untitled. Config overrides are P6.4. */
bool view_wrap_default(const char *path);
/* Apply open-time defaults to both the buffer flag and its layout. Reserve
 * layout_wrap_init first for text/untitled buffers; begin after this call. */
int view_wrap_file(view *v, struct layout *context, const char *path);
/* Bind caller-owned layout query context; configured text width must match
 * layout's text area. Toggle resets preferred column and horizontal scroll. */
int view_set_wrap(view *v, bool enabled, struct layout *context);
#endif
