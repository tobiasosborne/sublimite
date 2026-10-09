#ifndef EDIT_TABS_H
#define EDIT_TABS_H
#include "base/base.h"
#include "piece/piece.h"
#include "undo/undo.h"
#include "view/view.h"
#include "render/render.h"

#define TABS_TITLE_BYTES 256u
#define TABS_PATH_BYTES 4096u
#define TABS_NONE UINT32_MAX
#define TABS_NO_ID UINT64_C(0)
enum { TABS_OK = 0, TABS_ERR_ARG = -1, TABS_ERR_CAPACITY = -2,
       TABS_ERR_EMPTY = -3, TABS_ERR_RANGE = -4, TABS_ERR_INIT = -5 };

/* Borrowed resources: caller keeps them alive while open OR retained closed.
 * Tabs never mutate/destroy a tree or log. title/path are copied at open/rename;
 * byte lengths exclude NUL, must be below the fixed capacities. Arbitrary title
 * bytes are supported; path is opaque. Embedded NULs are rejected. */
typedef struct tabs_desc {
    piece_tree *buffer;
    undo_log *undo;
    view_state state;
    const char *title, *path;
    size_t title_len, path_len;
    bool modified;
} tabs_desc;
typedef struct tabs_tab {
    piece_tree *buffer;
    undo_log *undo;
    view_state state;
    uint64_t id;
    uint32_t mru_prev, mru_next;
    size_t title_len, path_len, closed_index;
    bool modified;
    char title[TABS_TITLE_BYTES], path[TABS_PATH_BYTES];
    /* Private title cache, built only on open/rename: cluster byte lengths
     * and packed 2-bit widths (3 = replacement with width 1). */
    uint8_t title_spans[TABS_TITLE_BYTES], title_widths[TABS_TITLE_BYTES/4u];
    uint16_t title_clusters, title_cells;
} tabs_tab;

/* Caller-owned control block; private fields. No copying after init. Stable
 * tabs_tab addresses until that record is evicted; indices change on reorder.
 * All APIs UI thread only. Initialise once (zero-init not required); fini after
 * retiring every retained borrowed resource. fini frees only tab storage. */
typedef struct tabs_set {
    edit_arena arena;
    tabs_tab *slots;
    uint32_t *order, *closed, *free_slots;
    size_t capacity, closed_capacity, count, closed_count, free_count;
    uint32_t active, mru_head, mru_tail;
    uint64_t next_id;
    bool cycling;
} tabs_set;
int tabs_init(tabs_set *s, size_t capacity, size_t closed_capacity);
void tabs_fini(tabs_set *s);
size_t tabs_owned_bytes(const tabs_set *s); /* control block + page-rounded arena */
size_t tabs_count(const tabs_set *s);
size_t tabs_closed_count(const tabs_set *s);
size_t tabs_active_index(const tabs_set *s); /* SIZE_MAX when empty */
const tabs_tab *tabs_at(const tabs_set *s, size_t index);
const tabs_tab *tabs_closed_at(const tabs_set *s, size_t depth); /* 0 = newest */
const tabs_tab *tabs_active(const tabs_set *s);

/* live is the loop's view_state: save outgoing, load incoming. Required, must
 * not alias tab storage. First open loads initial state; last close zeros live
 * (preferred column unset). Open appends and activates. Error leaves state and
 * outputs unchanged. No allocations or I/O in any API except init/fini. */
int tabs_open(tabs_set *s, const tabs_desc *d, view_state *live, uint64_t *id);
int tabs_select(tabs_set *s, size_t index, view_state *live);
/* Close commits any held cycle, saves live, then chooses MRU survivor if active
 * closed. evicted is required: id==0 means nothing evicted; otherwise the caller
 * may retire its resources after frame/worker references are quiescent. At zero
 * closed capacity the closed record is returned immediately. Modified close
 * policy (save/discard/cancel) belongs to caller, before calling close. */
int tabs_close(tabs_set *s, size_t index, view_state *live, tabs_tab *evicted);
int tabs_reopen(tabs_set *s, view_state *live, uint64_t *id);
int tabs_reorder(tabs_set *s, size_t from, size_t to); /* final destination index */
/* Key repeats call step; reverse for Ctrl+Shift+Tab. MRU order stays frozen
 * while held, wraps, and release promotes the final selection once. Other
 * structural/select operations commit a pending cycle on successful calls. */
int tabs_mru_step(tabs_set *s, bool reverse, view_state *live);
void tabs_mru_release(tabs_set *s);
int tabs_set_modified(tabs_set *s, size_t index, bool modified);
int tabs_rename(tabs_set *s, size_t index, const char *title, size_t title_len,
                const char *path, size_t path_len);

/* Resident, precomposed grapheme lookup. Must do no allocation/I/O, return a
 * slot in grid.glyphs whose glyph_index matches *identity; wide image spans
 * two cells. False means background only. Callback cannot mutate set/grid.
 * Invalid/control bytes are looked up as U+FFFD. Orphan zero-width clusters
 * are skipped. Width/segmentation are from utf8_cluster, never split clusters. */
typedef bool (*tabs_glyph_fn)(void *ctx, const uint8_t *text, size_t len,
                             int width, uint32_t *identity, uint32_t *slot);
typedef struct tabs_strip {
    uint32_t row, first_col, col_count, tab_cols;
    size_t first_tab;
    uint32_t fg, bg, active_fg, active_bg;
    tabs_glyph_fn glyph;
    void *glyph_ctx;
} tabs_strip;
/* Uniform tab slots, space padding, final separator '|', modified '*', and
 * truncated-title ellipsis U+2026. At tiny widths marker has priority. Caller
 * chooses first_tab to keep active visible and binds resident atlas before
 * render. Fills/damages only the requested row range in a begun frame; no
 * layout or frame submission. Requires a valid initialised render_grid. */
int tabs_strip_render(const tabs_set *s, render_grid *g, const tabs_strip *strip);
size_t tabs_strip_hit(const tabs_set *s, const tabs_strip *strip, uint32_t col);
#endif
