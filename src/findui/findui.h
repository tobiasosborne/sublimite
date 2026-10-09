#ifndef EDIT_FINDUI_H
#define EDIT_FINDUI_H

#include "base/base.h"
#include "find/find.h"
#include "render/render.h"
#include "undo/undo.h"

#define FINDUI_QUERY_BYTES FIND_MAX_PATTERN
#define FINDUI_NO_INDEX UINT64_MAX
#define FINDUI_MSG_ONE UINT32_C(0x46490101)
#define FINDUI_MSG_TWO UINT32_C(0x46490102)
#define FINDUI_MSG_DONE UINT32_C(0x46490103)

typedef enum findui_code {
    FINDUI_OK = 0, FINDUI_MORE, FINDUI_ERR_ARGUMENT, FINDUI_ERR_MEMORY,
    FINDUI_ERR_BUSY, FINDUI_ERR_LIMIT, FINDUI_ERR_STALE, FINDUI_ERR_FIND,
    FINDUI_ERR_UNDO
} findui_code;

typedef struct findui_options { bool regex, match_case, whole_word; } findui_options;
typedef struct findui_range { uint64_t start, end; } findui_range;
typedef struct findui_panel { void *private_; } findui_panel;

/* Invoked only by the bulk worker, immediately before every find call and
 * boundary-byte read. Optional tracing/test hook; must not mutate UI state,
 * allocate, or block. generation identifies the query/source/window request. */
typedef void (*findui_scan_hook)(void *user, uint32_t generation);
typedef struct findui_config {
    edit_arena *arena;
    work_pool *workers;
    size_t match_capacity, visible_capacity;
    findui_scan_hook scan_hook;
    void *hook_user;
} findui_config;

typedef struct findui_state {
    const uint8_t *query, *replacement;
    size_t query_length, query_cursor, replacement_length, replacement_cursor;
    findui_options options;
    uint32_t generation;
    uint64_t revision, match_index, match_count, cancel_requests;
    findui_range selected;
    size_t cached_matches, visible_matches, error_offset;
    find_code search_error;
    int undo_error;
    bool open, replace_mode, replacement_focus, searching, complete;
    bool cache_overflow, visible_overflow, replacing;
} findui_state;

/* UI-only, no allocation after init; use exclusive arena storage that outlives
 * disposal. No copies of an initialized panel. Init requires zeroed panel.
 * The shared pool's owner drains it and routes each message to accept(); this
 * module never steals another module's messages or shuts down the pool. */
findui_code findui_init(findui_panel *panel, const findui_config *config);
findui_code findui_dispose(findui_panel *panel); /* MORE until cancelled jobs return */
findui_code findui_service(findui_panel *panel); /* reap + retry deferred submission */
bool findui_accept(findui_panel *panel, const work_msg *message);
findui_state findui_get_state(const findui_panel *panel);

/* Retains snapshot; source change invalidates ALL old results, including when
 * the length stays the same. revision is the host's monotonically changing
 * document identity/version. Window is half-open; results keep absolute byte
 * offsets. A changed window restarts on the worker, so late windows work even
 * when the first-match cache fills. Empty query means no matches, also regex. */
findui_code findui_set_source(findui_panel *panel, piece_snapshot *snapshot,
                             uint64_t revision);
findui_code findui_set_window(findui_panel *panel, uint64_t start, uint64_t end);
findui_code findui_show(findui_panel *panel, bool open, bool replace_mode);
findui_code findui_set_options(findui_panel *panel, findui_options options);
findui_code findui_set_query(findui_panel *panel, const uint8_t *bytes, size_t length);
findui_code findui_edit_query(findui_panel *panel, size_t at, size_t erase,
                            const uint8_t *bytes, size_t length);
findui_code findui_set_query_cursor(findui_panel *panel, size_t at);
findui_code findui_set_replacement(findui_panel *panel, const uint8_t *bytes,
                                  size_t length, size_t cursor);
findui_code findui_focus_replacement(findui_panel *panel, bool focus);

/* +/-1. Wrap requires a completed count; MORE means an uncached selection is
 * being fetched by a fresh worker generation. Empty results return OK/unset.
 * Highlights never read document bytes. Copies clipped, sorted ranges; an
 * empty match is a caret range. *count is required size; LIMIT means output
 * capacity or init-time visible capacity was exceeded (no partial output). */
findui_code findui_next(findui_panel *panel, int direction, findui_range *selected);
findui_code findui_highlights(const findui_panel *panel, uint64_t start, uint64_t end,
                             findui_range *ranges, size_t capacity, size_t *count);

/* Literal replacement bytes, including in regex mode. Host must bind the
 * SAME tree/version used to take the source snapshot; revision detects stale
 * results without scanning the live tree. record_limit is the host-guaranteed
 * retained undo capacity (see integration contract). Admission conservatively
 * budgets eight delete spans plus one insert per match and existing records.
 * Begin gathers only worker-produced ranges, applies nothing, opens one group.
 * Step applies right-to-left up to match_budget/deadline_ns (0=no deadline).
 * MORE keeps the group open: host defers other document edits/replay. A failure
 * closes the successful prefix as one undo group and reports the undo error.
 * Cancel closes the prefix too. One operation can exceed a time slice under
 * the frozen undo/piece API. No automatic replacement scan on the UI thread. */
findui_code findui_replace_one(findui_panel *panel, undo_log *undo,
                              uint64_t revision, size_t record_limit,
                              uint64_t time_ns, const undo_state *before,
                              const undo_state *after);
findui_code findui_replace_all(findui_panel *panel, undo_log *undo,
                              uint64_t revision, size_t record_limit,
                              uint64_t time_ns, const undo_state *before,
                              const undo_state *after);
findui_code findui_replace_step(findui_panel *panel, size_t match_budget,
                               uint64_t deadline_ns, size_t *replaced);
findui_code findui_replace_cancel(findui_panel *panel);

typedef struct findui_style {
    uint32_t foreground, background, accent, cursor_foreground, cursor_background;
    /* 95 slots for ASCII 32..126 in the grid's existing glyph table. Missing
     * glyphs may use RENDER_NO_SLOT. Their glyph_index comes from that table. */
    const uint32_t *ascii_slots;
    size_t ascii_slot_count;
} findui_style;
/* Paint only [first_row,first_row+row_count), mark those rows. Begun grid and
 * valid glyph table required. 1 row=find; 2 adds replacement/status; 3+ adds
 * key hints/blank rows. Query cursor scrolls horizontally; non-ASCII/control
 * bytes display as \\xHH. Host resolves ASCII slots/font outside this module. */
findui_code findui_render(const findui_panel *panel, render_grid *grid,
                         uint32_t first_row, uint32_t row_count,
                         const findui_style *style);

#endif
