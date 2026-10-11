#ifndef EDIT_EDITOR_PRIVATE_H
#define EDIT_EDITOR_PRIVATE_H
#include "editor/editor.h"
#include "font/selection.h"
#include "base/base.h"
#include "file/file.h"
#include "layout/layout.h"
#include "lineidx/lineidx.h"
#include "undo/undo.h"
#include "journal/journal.h"
#include "work/work.h"
#include "keys/keys.h"
#include "savectl/savectl.h"
#include "indent/indent.h"
#include "minimap/minimap.h"
#include "findui/findui.h"
#include "editor/large_private.h"
#include "editor/runtime.h"
#include "scroll/scroll.h"
#define EDITOR_STAGE_BYTES (512u * 1024u)
#define EDITOR_STAGE_OPS (2u * EDITOR_INPUT_CAP + 8u)
#define EDITOR_PIECE_BINS (sizeof(size_t) * 8u)
typedef struct editor_piece_storage {
    edit_arena *arena;
    pthread_mutex_t mutex;
    void *free[EDITOR_PIECE_BINS];
    size_t size[EDITOR_PIECE_BINS], classes;
    size_t requests, fail_request; /* per-buffer allocator fault-test seam */
    bool ready;
} editor_piece_storage;
typedef struct editor_delta { uint64_t off, old, add; undo_state before, after; } editor_delta;
typedef struct editor_jop { uint64_t off, len; size_t at; bool insert; uint64_t id; size_t progress; piece_tree *tree; } editor_jop;
typedef struct editor_session editor_session;
typedef struct editor_buffer {
    edit_arena arena;
    editor_piece_storage piece_storage;
    work_pool *pool;
    editor_large lg;
    piece_tree *tree, *preview;
    bool pending_open, source_stale, watch_pending;
    int watch_fd;
    uint64_t prompt_ns, prompt_serial, prompt_generation;
    int operation_error, operation_errno, save_error, save_errno;
    uint32_t operation_kind, operation_generation;
    undo_log undo;
    file *file;
    savectl *save;
    size_t session_slot;
    uint64_t save_revision;
    size_t save_cursor;
    bool save_tracking;
    journal_base base;
    journal_view_state journal_view_state;
    bool journal_view_valid;
    lineidx *index;
    editor_delta *history;
    size_t history_head, history_count, history_cursor, history_cap;
    uint64_t id, lines, revision, history_serial, source_generation;
    bool last_repeat;
    size_t saved_cursor;
    bool saved_lost, undo_ready, index_dirty, retired;
    size_t find_leases;
    indent_style style;
    indent_style detected_style;
    bool config_wrap;
    minimap map;
    minimap_row *map_rows, *map_worker_rows;
    minimap map_worker;
    minimap_input map_job_input;
    piece_snapshot *map_snapshot;
    work_handle map_handle;
    uint32_t map_job_height;
    int map_job_error;
    bool map_job_sealed, map_received;
    bool map_sync_fill; /* file-backed source: compose prepares the first map (edit-73j landing) */
    view_state initial;
    scroll_state scroll;
    scroll_state scroll_display;
    scroll_resolver scroll_resolver;
    scroll_resolver scroll_follow_resolver;
    scroll_resident scroll_resident;
    piece_snapshot *scroll_snapshot;
    uint64_t scroll_revision, scroll_cursor;
    size_t scroll_prefix;
    bool scroll_ready, scroll_pending, scroll_following;
    char path[IPC_PATH_CAP], save_path[IPC_PATH_CAP];
    ipc_token wait_token;
} editor_buffer;
typedef struct editor_hit { uint64_t before, after; uint32_t within, width; } editor_hit;
typedef struct editor_wait { ipc_token token; size_t remaining; } editor_wait;
typedef struct editor_clip editor_clip;
typedef enum editor_action { EDITOR_ACTION_NONE, EDITOR_ACTION_MOVE, EDITOR_ACTION_DELETE, EDITOR_ACTION_REPAIR, EDITOR_ACTION_REPLAY, EDITOR_ACTION_REPLAY_WORK, EDITOR_ACTION_CLIP } editor_action;
typedef struct editor_backend_init {
    render_backend backend;
    render_config config;
    void *state;
    work_handle handle;
    int result;
    bool received, pending, finishing;
} editor_backend_init;
typedef struct editor_font_atlas {
    font_ascii_atlas atlas;
    font_metric metrics[LAYOUT_ASCII_GLYPHS];
    uint8_t *pixels;
    font_async *fonts;
    font_selection selection; /* primary bytes live as long as this version */
} editor_font_atlas;
typedef struct editor_font_size {
    font_t face;
    font_cell max_cell;
    edit_arena storage, scratch;
    editor_font_atlas atlas[2]; /* current frame + worker replacement */
    size_t capacity;
    double dpi, points, default_points;
    uint32_t requested_px, prepared_px, applied_px, default_px;
    font_selection selection; /* copied UI request; workers use a version snapshot */
    uint32_t requested_generation, prepared_generation, applied_generation;
    uint32_t window_width, window_height, max_width, max_height;
    unsigned current, preparing;
    bool legacy, received;
    _Atomic bool *logged;
    int result;
    work_handle handle;
    pthread_t worker;
} editor_font_size;
struct editor {
    editor_clip *clipboard;
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
    config_runtime *preferences_runtime;
    config_settings preferences;
    uint64_t preferences_generation;
    double preferences_font_override; /* explicit startup points (native env) win over file */
    int preferences_watchfd;
    findui_panel find;
    uint64_t find_id, find_source_generation, find_buffer_revision, find_revision, find_replace_end;
    uint32_t panel_rows;
    int find_direction, find_error, find_replace_request;
    bool find_select_pending, find_replace_active, find_visible, find_navigated, find_field_all;
    uint8_t find_edit[FINDUI_QUERY_BYTES];
    uint32_t find_ascii[LAYOUT_ASCII_GLYPHS];
    findui_range *find_ranges;
    size_t find_range_count;
    uint64_t find_window_start, find_window_end;
    edit_arena find_arena;
    editor_wait waits[IPC_MAX_CLIENTS];
    int poll_fd;
    file *opening;
    int open_error;
    view v;
    journal *journal;
    editor_session *session;
    int session_lock;
    bool recovering, startup_restored, save_flushing, recover_existing, session_lock_owned;
    /* edit-btj: startup recovery notice, shown on the status row until the
     * first mutation; recovery_aside names the set-aside source journal. */
    const char *recovery_notice;
    uint64_t recovery_notice_mutations;
    bool recovery_notice_shown, recovery_retry, recovery_skip;
    char recovery_aside[IPC_PATH_CAP];
    work_pool pool, journal_pool, startup_pool;
    editor_backend_init startup;
    render_backend retired_backend;
    bool startup_pool_ready, startup_presented;
    bool journal_pool_ready;
    uint8_t journal_scratch[JOURNAL_INSERT_SLICE_BYTES];
    uint8_t *journal_tabs, *journal_tabs_cached;
    size_t journal_tabs_size;
    uint32_t journal_width, journal_height;
    bool journal_metadata_pending;
    plat platform;
    render_backend *backend;
    render_grid grid;
    render_grid text_grid, map_grid;
    uint32_t *map_pixels, *map_scratch;
    size_t map_pixel_capacity;
    uint32_t map_capacity;
    layout lay;
    layout_config layout_cfg;
    font_async *fonts; /* alias of the adopted font-size version */
    uint64_t *font_rows;
    size_t font_row_words, font_row_slots;
    editor_font_size font;
    _Atomic bool font_logged;
    render_strip *strips;
    editor_hit *hits;
    uint64_t *row_byte;
    uint32_t *row_used;
    size_t strip_cap;
    uint32_t max_cols, max_rows;
    plat_event queue[EDITOR_INPUT_CAP];
    uint64_t queue_owner[EDITOR_INPUT_CAP], queue_generation[EDITOR_INPUT_CAP];
    size_t queue_head, queue_count;
    size_t index_cursor; /* round-robin dirty-buffer maintenance */
    editor_delta edit_delta, replay_delta;
    uint64_t replay_old_nl, replay_before_len;
    bool replay_redo;
    bool explicit_group;
    unsigned source_choice;
    uint64_t source_choice_id, source_choice_generation;
    uint64_t event_serial, dispatch_serial, queue_serial[EDITOR_INPUT_CAP];
    editor_jop *ops;
    uint8_t *stage;
    size_t op_count, stage_used;
    editor_action action;
    undo_kind delete_kind;
    view_selection old_selection;
    view_selection replay_selection;
    uint64_t old_cursor, key_ns, edit_time_ns, last_input, next_blink;
    uint64_t repair_line_byte, repair_line_number;
    editor_frame frame, active_frame;
    uint32_t unimplemented_frame; /* Notice belongs to this frame generation. */
    scroll_frame_plan scroll_plan, submitted_plan;
    bool submitted_plan_valid;
    uint64_t geometry_generation;
    /* One backend slot plus the preceding submission for repeat observation. */
    editor_frame refresh_frames[2];
    size_t refresh_next;
    uint64_t refresh_sequence, refresh_ns, completion_ns;
    uint32_t completion_id;
    uint32_t extra_first, extra_end, resize_w, resize_h;
    bool dirty, extra_rows, resize_pending, full_pending;
    bool repair_line_valid;
    bool focused, visible, blinking, quit;
    bool journal_waiting;
    bool pool_ready, tabs_ready, has_platform, pump_stopped;
    uint32_t tab_rows, map_cols;
    tabs_strip strip;
    size_t drag_tab;
    uint64_t drag_id, map_drag_id, map_drag_generation;
    bool map_drag, paint_ready, caret_only;
    bool glyph_only; /* glyph pixels plus caret damage; retain minimap/chrome cells */
    bool text_drag;
    uint64_t text_drag_id, text_drag_generation, click_ns, click_byte;
    uint32_t click_row, click_col, click_count, text_drag_mode;
    uint64_t text_drag_lo, text_drag_hi;
    bool pointer_unit_pending, pointer_unit_initial;
    scroll_state wheel;
    scroll_resolver wheel_resolver;
    bool wheel_pending, find_scroll, visual_seed_valid;
    layout_wrap_row visual_seed;
    int64_t wrap_wheel_rows;
    int raster_poll_fd;
    uint8_t *indent_bytes;
    indent_range *ws_ranges;
    size_t ws_count;
    uint64_t bracket_source, bracket_mate;
    int error;
};
/* Session publication uses prepaid persistent metadata; worker requests pin it. */
int editor_session_init(editor *e);
void editor_session_close(editor *e);
void editor_session_publish(editor *e, editor_buffer *b);
void editor_session_metadata(editor *e);
void editor_session_forget(editor *e, editor_buffer *b);
int editor_save_setup(editor *e, editor_buffer *b);
int editor_save_request(editor *e, uint64_t request_ns);
void editor_save_tick(editor *e);
bool editor_save_leased(const editor *e);
bool editor_save_active(const editor *e);
void editor_save_overlay(editor *e);
void editor_save_submitted(editor *e, uint64_t submitted_ns);
int editor_session_restore(editor *e);
/* edit-btj: rename a recovery journal to "<path>.set-aside-<ns>" without
 * replacing anything; out receives the new name ("" on failure). Startup only. */
int editor_set_aside(const char *path, char *out, size_t cap);
/* Internal cross-file helpers; UI only. */
int editor_clip_init(editor *e);
void editor_clip_stop(editor *e);
void editor_clip_close(editor *e);
void editor_clip_selection(editor *e);
int editor_clip_action(editor *e, keys_action action, const plat_event *ev);
int editor_clip_event(editor *e, const plat_event *ev);
int editor_clip_poll(editor *e);
bool editor_clip_busy(const editor *e);
bool editor_clip_pending(const editor *e);
bool editor_clip_runnable(const editor *e);
bool editor_clip_holds(const editor *e, const editor_buffer *b);
int editor_clip_continue(editor *e);
int editor_replace_begin(editor *e, size_t len);
int editor_replace_delete(editor *e, uint64_t off, uint64_t len);
int editor_replace_insert(editor *e, uint64_t off, const uint8_t *bytes, size_t len);
int editor_replace_end(editor *e, uint64_t target);

int editor_backend_init_sync(editor *e, render_config config, void *state);
int editor_backend_start_probe(editor *e, render_backend backend, render_config config, void *state);
int editor_backend_poll_probe(editor *e);
void editor_backend_close_probe(editor *e);
bool editor_activation_busy(const editor *e);
void editor_font_bad_config(editor *e);
int editor_begin_frame(editor *e);
int editor_full_layout(editor *e);
int editor_refresh_cursor(editor *e, uint64_t old_cursor);
int editor_refresh_placed(editor *e, uint64_t old_cursor);
void editor_seed_view_lines(editor *e);
bool editor_line_start_before(editor *e, uint64_t end, uint64_t *start);
void editor_restart_blink(editor *e, uint64_t now);
void editor_route_work(const work_msg *msg, void *ctx);
int editor_font_init(editor *e, const font_ascii_atlas **atlas);
int editor_font_zoom(editor *e, int value);
bool editor_font_receive(editor *e, const work_msg *msg);
int editor_font_poll(editor *e);
bool editor_font_pending(const editor *e);
int editor_resize_grid(editor *e, render_dims dims);
void editor_journal_staged(editor *e);
int editor_checkpoint(editor *e);
int editor_journal_metadata(editor *e);
/* Journal an applied action without waiting for a frame or durability credit. */
int editor_journal_changed(editor *e);
void editor_encode_view(view_state state, uint8_t out[JOURNAL_VIEW_STATE_BYTES]);
int editor_resize_now(editor *e, uint32_t width, uint32_t height);
bool editor_event_control(const editor *e, const plat_event *ev);
int editor_handle_key(editor *e, const plat_event *ev);
int editor_handle_wheel(editor *e, const plat_event *ev);
int editor_scroll_init(editor *e, editor_buffer *b);
int editor_scroll_retire(editor_buffer *b);
void editor_scroll_close(editor_buffer *b);
int editor_scroll_slice(editor *e);
int editor_scroll_follow(editor *e);
bool editor_scroll_place(editor *e, uint64_t byte, uint64_t line, bool approximate);
int editor_scroll_seek(editor *e, uint64_t byte);
int editor_scroll_edit(editor *e, uint64_t off, uint64_t old, uint64_t add);
int editor_scroll_resize(editor *e);
void editor_scroll_mirror(editor *e);
bool editor_scroll_waiting(const editor *e);
void editor_refresh_complete(void *ctx, uint32_t id, uint64_t ns);
void editor_refresh_flush(editor *e);
int editor_continue_action(editor *e);
int editor_register_journal(editor *e, editor_buffer *b, uint64_t id);
int editor_buffer_prepare(editor *e, const char *path, const uint8_t *bytes,
                          size_t len, editor_buffer **out);
void editor_buffer_destroy(editor_buffer *b);
int editor_adopt_open(editor *e, editor_buffer *b);
int editor_check_sources(editor *e);
int editor_activate(editor *e);
int editor_cycle_tab(editor *e, bool reverse);
int editor_compose(editor *e);
int editor_paint_prepare(editor *e);
int editor_font_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot);
bool editor_font_event(editor *e, const work_msg *msg);
void editor_font_track(editor *e, uint32_t slot, uint32_t row);
int editor_pointer(editor *e, const plat_event *ev);
int editor_map_service(editor *e);
bool editor_map_receive(editor *e, const work_msg *msg);
int editor_chrome_pointer(editor *e, const plat_event *ev);
int editor_wheel_continue(editor *e);
int editor_text_geometry(editor *e);
bool editor_wheel_ready(const editor *e);
int editor_scroll_resolve(editor *e, scroll_state *s, scroll_resolver *r);
void editor_hit_record(void *ctx, uint32_t row, uint32_t column, uint32_t count,
    uint32_t skip, uint32_t width, uint64_t before, uint64_t after);
int editor_find_init(editor *e);
void editor_find_close(editor *e);
int editor_find_service(editor *e);
bool editor_find_key(editor *e, const plat_event *ev);
int editor_find_paint(editor *e);
int editor_find_replace_continue(editor *e);
findui_mutation_result editor_find_delete(void *ctx, undo_log *undo, uint64_t off, uint64_t len,
    uint64_t when, const undo_state *before, const undo_state *after);
findui_mutation_result editor_find_insert(void *ctx, undo_log *undo, uint64_t off, const uint8_t *bytes,
    size_t len, uint64_t when, const undo_state *before, const undo_state *after);
void editor_find_replace_finish(editor *e);
int editor_poll_sources(editor *e);
int editor_preferences_poll(editor *e);
int editor_preferences_apply(editor *e);
void editor_preferences_buffer(editor *e, editor_buffer *b);
void editor_retire_buffers(editor *e);
lineidx_src editor_source(editor_buffer *b);
minimap_input editor_map_input(editor_buffer *b);
void editor_modified(editor *e);
/* large.c: UI-owned per-buffer state, mailbox-only worker results. */
void editor_large_init(editor_buffer *b);
void editor_large_start(editor *e, editor_buffer *b);
void editor_large_close(editor_buffer *b);
void editor_large_keep(editor_buffer *b);
void editor_large_index_progress(editor *e);
void editor_large_file_msg(editor *e, const file_msg *fm);
bool editor_large_receive(editor *e, const work_msg *msg);
#endif
