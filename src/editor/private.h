#ifndef EDIT_EDITOR_PRIVATE_H
#define EDIT_EDITOR_PRIVATE_H
#include "editor/editor.h"
#include "base/base.h"
#include "file/file.h"
#include "layout/layout.h"
#include "lineidx/lineidx.h"
#include "undo/undo.h"
#include "journal/journal.h"
#include "work/work.h"
#include "keys/keys.h"
#include "indent/indent.h"
#include "minimap/minimap.h"
#define EDITOR_STAGE_BYTES (512u * 1024u)
#define EDITOR_STAGE_OPS (2u * EDITOR_INPUT_CAP + 8u)
typedef struct editor_delta { uint64_t off, old, add; } editor_delta;
typedef struct editor_jop { uint64_t off, len; size_t at; bool insert; uint64_t id; } editor_jop;
typedef struct editor_buffer {
    edit_arena arena;
    piece_tree *tree;
    undo_log undo;
    file *file;
    journal_base base;
    lineidx *index;
    editor_delta *history;
    size_t history_head, history_count, history_cursor, history_cap;
    uint64_t id, lines, revision;
    size_t saved_cursor;
    bool saved_lost, undo_ready, index_dirty, retired;
    indent_style style;
    minimap map;
    minimap_row *map_rows;
    view_state initial;
    char path[IPC_PATH_CAP];
    ipc_token wait_token;
} editor_buffer;
typedef struct editor_wait { ipc_token token; size_t remaining; } editor_wait;
typedef enum editor_action { EDITOR_ACTION_NONE, EDITOR_ACTION_MOVE, EDITOR_ACTION_DELETE, EDITOR_ACTION_REPAIR, EDITOR_ACTION_REPLAY } editor_action;
struct editor {
    editor_config cfg;
    editor_stats stats;
    edit_arena arena;
    piece_tree *tree;
    undo_log *undo;
    editor_buffer *buffer, *empty;
    editor_buffer **buffers;
    size_t buffer_capacity;
    tabs_set tabs;
    keys_state keys;
    editor_wait waits[IPC_MAX_CLIENTS];
    int poll_fd;
    file *opening;
    int open_error;
    view v;
    journal *journal;
    work_pool pool;
    plat platform;
    render_backend *backend;
    render_grid grid;
    render_grid text_grid, map_grid;
    layout lay;
    layout_config layout_cfg;
    render_atlas_page page;
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_strip *strips;
    uint64_t *row_byte;
    uint32_t *row_used;
    size_t strip_cap;
    uint32_t max_cols, max_rows;
    plat_event queue[EDITOR_INPUT_CAP];
    size_t queue_head, queue_count;
    editor_delta edit_delta;
    editor_jop *ops;
    uint8_t *stage;
    size_t op_count, stage_used;
    editor_action action;
    undo_kind delete_kind;
    view_selection old_selection;
    view_selection replay_selection;
    uint64_t old_cursor, key_ns, last_input, next_blink;
    editor_frame frame, active_frame;
    uint32_t extra_first, extra_end, resize_w, resize_h;
    bool dirty, extra_rows, resize_pending, full_pending;
    bool focused, visible, blinking, quit;
    bool pool_ready, tabs_ready, has_platform, pump_stopped;
    uint32_t tab_rows, map_cols;
    tabs_strip strip;
    size_t drag_tab;
    bool map_drag, paint_ready;
    uint8_t *indent_bytes;
    indent_range *ws_ranges;
    size_t ws_count;
    uint64_t bracket_source, bracket_mate;
    int error;
};
/* Internal cross-file helpers; UI only. */
int editor_begin_frame(editor *e);
int editor_full_layout(editor *e);
int editor_refresh_cursor(editor *e, uint64_t old_cursor);
void editor_restart_blink(editor *e, uint64_t now);
void editor_route_work(const work_msg *msg, void *ctx);
void editor_journal_staged(editor *e);
int editor_checkpoint(editor *e);
int editor_handle_key(editor *e, const plat_event *ev);
int editor_continue_action(editor *e);
int editor_register_journal(editor *e, editor_buffer *b, uint64_t id);
int editor_buffer_prepare(editor *e, const char *path, const uint8_t *bytes,
                          size_t len, editor_buffer **out);
void editor_buffer_destroy(editor_buffer *b);
int editor_activate(editor *e);
int editor_cycle_tab(editor *e, bool reverse);
int editor_compose(editor *e);
int editor_paint_prepare(editor *e);
int editor_pointer(editor *e, const plat_event *ev);
int editor_poll_sources(editor *e);
void editor_retire_buffers(editor *e);
lineidx_src editor_source(editor_buffer *b);
minimap_input editor_map_input(editor_buffer *b);
void editor_modified(editor *e);
#endif
