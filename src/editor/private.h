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
#define EDITOR_STAGE_BYTES (512u * 1024u)
#define EDITOR_STAGE_OPS (2u * EDITOR_INPUT_CAP + 8u)
typedef struct editor_delta { uint64_t off, old, add; } editor_delta;
typedef struct editor_jop { uint64_t off, len; size_t at; bool insert; } editor_jop;
typedef enum editor_action { EDITOR_ACTION_NONE, EDITOR_ACTION_MOVE, EDITOR_ACTION_DELETE, EDITOR_ACTION_REPAIR, EDITOR_ACTION_REPLAY } editor_action;
struct editor {
    editor_config cfg;
    editor_stats stats;
    edit_arena arena;
    piece_tree *tree;
    undo_log undo;
    view v;
    file *file;
    journal *journal;
    lineidx *index;
    work_pool pool;
    plat platform;
    render_backend *backend;
    render_grid grid;
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
    editor_delta *history, edit_delta;
    size_t history_head, history_count, history_cursor, history_cap;
    editor_jop *ops;
    uint8_t *stage;
    size_t op_count, stage_used;
    editor_action action;
    undo_kind delete_kind;
    view_selection old_selection;
    view_selection replay_selection;
    uint64_t old_cursor, key_ns, lines, last_input, next_blink;
    editor_frame frame, active_frame;
    uint32_t extra_first, extra_end, resize_w, resize_h;
    bool dirty, extra_rows, resize_pending, index_dirty, full_pending;
    bool focused, visible, blinking, quit, crlf;
    bool pool_ready, undo_ready, has_platform, pump_stopped;
    int error;
};
/* Internal cross-file helpers; UI only. */
int editor_begin_frame(editor *e);
int editor_full_layout(editor *e);
int editor_refresh_cursor(editor *e, uint64_t old_cursor);
void editor_restart_blink(editor *e, uint64_t now);
void editor_route_work(const work_msg *msg, void *ctx);
void editor_journal_staged(editor *e);
int editor_handle_key(editor *e, const plat_event *ev);
int editor_continue_action(editor *e);
#endif
