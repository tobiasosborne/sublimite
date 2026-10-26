#ifndef EDIT_EDITOR_PRIVATE_H
#define EDIT_EDITOR_PRIVATE_H
#include "editor/editor.h"
#include "editor/wait.h"
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
#include "palette/palette.h"
#include "menu/menu.h"
#include "fif/fif.h"
#include "editor/large_private.h"
#include "editor/runtime.h"
#include "scroll/scroll.h"
#include "editor/highlight.h"
#include <limits.h>
#define EDITOR_STAGE_BYTES (512u * 1024u)
#define EDITOR_STAGE_OPS (2u * EDITOR_INPUT_CAP + 8u)
#define EDITOR_PIECE_BINS (sizeof(size_t) * 8u)
/* (P) existing expanded find panel: query, extra field, hints. Palette reuses
 * its rows with the query first and actions in all remaining rows. */
#define EDITOR_FIND_PANEL_ROWS (1u + 1u + 1u)
typedef struct editor_piece_storage {
    edit_arena *arena;
    os_mutex mutex;
    void *free[EDITOR_PIECE_BINS];
    size_t size[EDITOR_PIECE_BINS], classes;
    size_t requests, fail_request; /* per-buffer allocator fault-test seam */
    bool ready;
} editor_piece_storage;
typedef struct editor_delta { uint64_t off, old, add; undo_state before, after; } editor_delta;
typedef struct editor_soft_delta { view_selection before, after; uint64_t serial; bool text; } editor_soft_delta;
typedef struct editor_jop { uint64_t off, len; size_t at; bool insert, commit; uint64_t id; size_t progress; piece_tree *tree; } editor_jop;
typedef struct editor_session editor_session;
/* edit-ze59: worker checkpoint (session.c) and in-flight Save As (commands.c). */
typedef struct editor_publish editor_publish;
typedef struct editor_save_as editor_save_as;
#define EDITOR_PUBLISH_MESSAGE UINT32_C(0x45505542) /* ASCII "EPUB": wake-up only */
/* edit-28u: identity of one on-disk file generation, as journal_check_base
 * compares it (journal_base without its path). valid=false binds nothing. */
typedef struct editor_base_id {
    uint64_t size, mtime_ns, inode, device, ctime_ns;
    uint32_t prefix_crc, prefix_len;
    bool valid;
} editor_base_id;
/* edit-f3i: a reference text (the saved revision's snapshot) indexed by the
 * address of its bytes. Piece spans point into immutable original/mapping or
 * add-buffer memory that the retained snapshot keeps alive, so equal
 * addresses are equal bytes; off is the span's offset in the reference text.
 * Sorted by at; adjacent spans contiguous in memory and text are merged. */
typedef struct editor_ref_span { uintptr_t at; uint64_t len, off; } editor_ref_span;
typedef struct editor_ref_map { editor_ref_span *spans; size_t count; } editor_ref_map;
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
    file_path_watch target_watch;
    file_source *target_source;
    bool target_rebound;
    size_t session_slot;
    uint64_t save_revision;
    size_t save_cursor;
    bool save_tracking;
    journal_base base;
    /* edit-28u: the tree's ORIGINAL holds exactly the bytes of this file
     * generation. Set once in editor_buffer_prepare, before the buffer is
     * published; save workers read it. A checkpoint references base bytes
     * instead of copying them only while BASE is this same generation. */
    editor_base_id origin;
    /* edit-28u: at revision saved_text_revision the whole text equals the
     * bytes of this generation (a completed save of that revision). */
    editor_base_id saved_text;
    uint64_t saved_text_revision;
    /* edit-f3i: saved_snapshot is the snapshot whose bytes the saved_text
     * generation holds (the text written by that save), retained so later
     * checkpoints encode against it; saved_map indexes it (built lazily by
     * the UI checkpoint). save_snapshot is the in-flight save's snapshot,
     * from request until the save ends; a completed save moves it to
     * saved_snapshot. Both are replaced together with saved_text. */
    piece_snapshot *save_snapshot, *saved_snapshot;
    editor_ref_map saved_map;
    /* edit-fi2: private copy of BASE, made off the UI thread after the first
     * modification (retain.c). retain_of is the BASE identity it copies;
     * once READY, checkpoints write BASE = retain.copy + TARGET = path, with
     * the text's differences from BASE (the copy holds BASE's bytes), so
     * recovery survives an external rewrite of the original. retain_used:
     * the checkpoint being built references the copy. */
    journal_retain retain;
    work_handle retain_handle;
    work_pool *retain_pool;
    editor_base_id retain_of;
    uint64_t retain_failed_revision; /* edit-fi0p F9: retry only after a later mutation */
    uint8_t retain_state;
    bool retain_used, retain_building;
    journal_view_state journal_view_state;
    bool journal_view_valid;
    lineidx *index;
    editor_delta *history;
    editor_soft_delta *soft_history;
    size_t soft_head, soft_count, soft_cursor;
    size_t soft_base_redo; /* completed text undos preceding retained selection records */
    size_t history_head, history_count, history_cursor, history_cap;
    uint64_t id, lines, revision, history_serial, source_generation;
    bool last_repeat;
    bool overwrite, mark_valid;
    bool last_key_valid;
    plat_event last_key;
    const keys_binding *last_binding;
    view_selection mark;
    size_t saved_cursor;
    bool saved_lost, undo_ready, index_dirty, retired;
    bool painted; /* edit-dzk1: one of its frames presented in this launch */
    /* edit-qct: closed-tab stand-in for a deferred session journal (restore.c):
     * inert, never recorded in this launch's journal or session tree. */
    bool placeholder;
    struct editor_restore *restore;                   /* placeholder's source, lease and program */
    struct editor_restore_buffer *restore_apply;      /* journalled edits to apply once attached */
    size_t find_leases, hl_leases;
    /* edit-n95: read_only refuses every user edit (input.c checks; a refused
     * edit is a no-op, never an error). derived marks a buffer rebuilt from
     * other state (Find Results): no journal BASE, no checkpoint/TABS/VIEW
     * record, no session leaf, no save; it does not survive hot exit. */
    bool read_only, derived;
    indent_style style;
    indent_style detected_style;
    file_eol paste_eol; /* edit-bml: detected in-memory ending survives edits */
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
    /* edit-ci6i: a refused map snapshot defers the take until this tick
     * (0: none due); consecutive refusals, a notice from the second on. */
    uint64_t map_retry_ns;
    uint32_t map_refusals;
    view_state initial;
    scroll_state scroll;
    scroll_state scroll_display;
    /* edit-d50 fix: an edit straddled the displayed top. Its byte was moved
     * onto the edit but its line label is unknown until view normalization
     * or the resident resolver certifies one (editor_scroll_follow). */
    bool scroll_display_stale;
    scroll_resolver scroll_resolver;
    scroll_resolver scroll_follow_resolver;
    scroll_resident scroll_resident;
    piece_snapshot *scroll_snapshot;
    uint64_t scroll_revision, scroll_cursor;
    size_t scroll_prefix;
    bool scroll_ready, scroll_pending, scroll_following;
    /* edit-pd6: page keys (signed count) and wheel delta (plat_event.dy
     * units, saturated to its int32 range) that arrived while a byte jump
     * was unresolved; scrollctl applies them once it resolves. */
    int64_t scroll_queued_pages;
    int32_t scroll_queued_wheel;
    int64_t scroll_queued_pixels; /* edit-o20 scroll_lines, Q8 pixels, saturated */
    /* edit-bt1: a refused piece snapshot defers the slice until this tick
     * (0: none due); consecutive refusals, a notice from the second on. */
    uint64_t scroll_retry_ns;
    uint32_t scroll_refusals;
    char path[IPC_PATH_CAP], save_path[IPC_PATH_CAP];
    ipc_token wait_token;
} editor_buffer;
typedef struct editor_hit { uint64_t before, after; uint32_t within, width; } editor_hit;
/* edit-n95: find in files (src/editor/fif.c). One job is current; a replaced
 * job retires (search cancelled, mail still drained, stale generations
 * ignored) until fif_job_finished and fif_job_dispose succeed. */
typedef struct editor_fif_slot { fif_job job; work_handle search; uint32_t generation; bool live; } editor_fif_slot;
typedef struct editor_fif {
    edit_arena arena;
    editor_fif_slot current, retiring;
    uint32_t generation, received, assembled, last_file, files, visited;
    uint64_t results_id, last_line;
    /* Per assembled hit (capacity FIF_DEFAULT_HITS, the engine's default):
     * results-buffer byte of its line start/end (the LF), of its marked match
     * and the mark length, and the header line start when it opens a file. */
    uint64_t *line_start, *line_end, *mark, *header;
    uint32_t *mark_length;
    fif_done done;
    char where[PATH_MAX];
    size_t where_length, where_cursor;
    bool mode, where_focus, where_all, running, done_received, summary_written;
    bool full; /* edit-5vz: an append was refused; assembly stopped */
    bool ranges_owned; /* e->find_ranges currently hold results marks */
} editor_fif;
typedef struct editor_wait {
    ipc_token token;
    size_t remaining;
    /* (P) distinct tabs per request: IPC paths plus optional stdin/untitled. */
    uint64_t ids[IPC_MAX_PATHS + 1u];
} editor_wait;
typedef struct editor_clip editor_clip;
/* A decimal digit carries at least one bit. The uint64_t bit width is a
 * conservative bound on decimal input length, plus a NUL for rendering. */
typedef struct editor_goto {
    /* (P) path capacity includes NUL and matches buffer/IPC storage. */
    char digits[IPC_PATH_CAP];
    keys_action kind;
    size_t length, cursor;
    uint64_t line, owner, generation;
    view_state original;
    scroll_state original_scroll; /* displayed 3kk top before the prompt */
    bool active, selected, valid, accept;
} editor_goto;
/* edit-2ye3: Save / Don't save / Cancel for a user close of a modified tab.
 * Embedded storage; owner 0 = no prompt. prompt_ns/serial follow the
 * source-change prompt: set when the frame showing it is submitted, so only
 * keys dispatched after that answer it. saving: the tab whose save (or Save
 * As) decides the close; it closes once that save leaves it unmodified. */
typedef struct editor_close_prompt {
    uint64_t owner, prompt_ns, prompt_serial, saving;
} editor_close_prompt;
typedef enum editor_action { EDITOR_ACTION_NONE, EDITOR_ACTION_GOTO, EDITOR_ACTION_MOVE, EDITOR_ACTION_DELETE, EDITOR_ACTION_REPAIR, EDITOR_ACTION_REPLAY, EDITOR_ACTION_REPLAY_WORK, EDITOR_ACTION_CLIP } editor_action;
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
    char fallbacks[FONT_CHAIN_TEXT_MAX]; /* edit-6e2: chain snapshot for this version */
    uint32_t fallback_skipped;           /* worker-written, read at adoption */
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
    char fallbacks[FONT_CHAIN_TEXT_MAX]; /* edit-6e2: UI chain request, '\n'-joined */
    uint32_t chain_logged_generation;    /* worker-only: last generation whose skips were logged */
    uint32_t requested_generation, prepared_generation, applied_generation;
    uint32_t window_width, window_height, max_width, max_height;
    unsigned current, preparing;
    bool legacy, received;
    _Atomic bool *logged;
    int result;
    work_handle handle;
    uint64_t worker;
} editor_font_size;
struct editor {
    editor_clip *clipboard;
    editor_config cfg;
    editor_highlight hl;
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
    char preferences_font_family[FONT_FALLBACK_PATH_MAX];
    char preferences_font_file[FONT_FALLBACK_PATH_MAX];
    bool preferences_font_selection_override, preferences_font_selection_pending;
    char preferences_font_fallbacks[FONT_CHAIN_TEXT_MAX]; /* edit-6e2: effective chain */
    bool preferences_font_fallbacks_override; /* CLI or env chain wins over the config key */
    int preferences_watchfd;
    findui_panel find;
    palette_state *palette;
    bool palette_open;
    uint64_t palette_owner, palette_generation, palette_revision;
    /* edit-yey: menu bar, embedded (no allocation). The bar is shown while
     * pinned (menu.visible / Toggle Menu), active (keyboard focus after a
     * bare Alt press-and-release, mnemonic or title click) or with a
     * drop-down open; open is the menu index or negative. A held Alt only
     * arms; it never changes the layout. */
    struct editor_menu {
        bool pinned, config_visible, armed, was_active, active;
        int open;
        size_t title, item, first;
    } menu;
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
    editor_fif fif;
    bool open_byte_column; /* edit-n95: position_buffer reads col as a one-based byte column */
    editor_goto go;
    editor_close_prompt close_prompt;
    keys_action file_command;
    char file_command_path[IPC_PATH_CAP];
    uint64_t file_command_owner, file_command_generation;
    editor_wait waits[IPC_MAX_CLIENTS];
    editor_wait_set poll_set;
    bool poll_ready;
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
    /* edit-fi2: largest BASE retained on first modification (retain.c);
     * zero disables retention (tests of the set-aside fallback). */
    uint64_t retain_limit;
    uint64_t retain_retry; /* stats.mutations + 1 at a failed reference checkpoint; 0 none */
    /* edit-ze59: retention publication and Save As's replacing checkpoint run
     * on a worker that leases the journal (publish, non-NULL until adopted);
     * publish_owed: a Save As rebind awaits its checkpoint; save_as: the
     * target write in flight. */
    editor_publish *publish;
    editor_save_as *save_as;
    bool publish_owed;
    uint64_t publish_deadline_ns; /* edit-y315: exit wait on publish; 0 not waiting */
    /* (P) A tab title plus the Save As refusal suffix and its NUL. */
    char file_command_notice[TABS_TITLE_BYTES + sizeof " is open and modified in another tab"];
    uint64_t recovery_notice_mutations;
    bool recovery_notice_shown, recovery_retry, recovery_skip;
    char recovery_aside[IPC_PATH_CAP];
    size_t recovery_deferred; /* edit-fm4: discovered sessions left pending (too large to replay at startup) */
    /* edit-qct: placeholders hold tab IDs that never reach the journal;
     * restore_active counts placeholders with a scan or open in flight;
     * restore_prepare is the program editor_buffer_prepare applies. */
    size_t recovery_placeholder_ids, restore_active;
    struct editor_restore_buffer *restore_prepare;
    int restore_fault; /* edit-601 test seam: a nonzero journal code fails the next restore scan with it */
    work_pool pool, journal_pool, startup_pool;
    editor_backend_init startup;
    render_backend retired_backend;
    bool startup_pool_ready, startup_presented;
    bool journal_pool_ready;
    uint8_t journal_scratch[JOURNAL_INSERT_SLICE_BYTES];
    uint8_t *journal_tabs, *journal_tabs_cached;
    size_t journal_tabs_size;
    uint32_t journal_width, journal_height;
    bool journal_metadata_pending, journal_group_open;
    /* edit-dzk1: buffer id of the last PAINT record appended (0: none pending);
     * render_dropped: a rejected frame was dropped, the next one is full. */
    uint64_t journal_paint;
    bool render_dropped;
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
    bool edit_selection_valid;
    bool selection_command, soft_replaying;
    bool replay_anchor_trusted;
    plat_event command_key;
    const keys_binding *command_binding;
    uint64_t command_sequence;
    size_t soft_target;
    view_selection edit_selection;
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
    uint64_t checkpoint_retry_ns; /* edit-dr9: busy close retries on the journal tick */
    /* edit-ivv: a checkpoint the shared directory guard refused (JOURNAL_BUSY)
     * keeps its caller's state; the caller retries once editor_checkpoint_due.
     * busy_due_ns is that tick (0 none); busy_refusals counts consecutive
     * refusals (a second one has outlasted the tick: notice). A startup
     * recovery whose checkpoint was refused owes one (busy_checkpoint) and
     * retires its source journal (busy_source, lease held) only after it.
     * A refused Save As checkpoint stays publish_owed instead (edit-ze59,
     * edit-e3z5). */
    uint64_t busy_due_ns;
    uint32_t busy_refusals;
    bool busy_checkpoint;
    int busy_source_lease;
    char busy_source[IPC_PATH_CAP];
    bool pool_ready, tabs_ready, has_platform, pump_stopped;
    uint32_t tab_rows, map_cols; /* tab_rows: every row above the text (menu bar + tab strip) */
    uint32_t menu_rows; /* edit-yey: the bar's row (0/1); the tab strip is row menu_rows */
    tabs_strip strip;
    size_t drag_tab;
    uint64_t drag_id, map_drag_id, map_drag_generation;
    int64_t map_drag_offset; /* edit-2mhd: pointer pixels below the captured viewport top */
    bool map_drag, paint_ready, caret_only;
    bool glyph_only; /* glyph pixels plus caret damage; retain minimap/chrome cells */
    bool text_drag;
    uint64_t text_drag_id, text_drag_generation, click_ns, click_byte;
    uint64_t click_id, click_generation;
    uint32_t click_button;
    plat_event text_pointer;
    int64_t text_drag_pixels;
    bool text_drag_rehit;
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
    /* Kill top derives its reserve from the existing synchronous view window.
     * Sublime's yank reads only the top entry; history cycling needs a panel. */
    uint8_t kill_bytes[VIEW_WINDOW];
    size_t kill_len;
    uint64_t kill_at, kill_buffer;
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
/* edit-5u2: true iff b is the buffer whose save is in flight (edit-9u3 keeps
 * its leaf after eviction). state, if non-NULL, receives its published view. */
bool editor_session_retains(const editor *e, const editor_buffer *b, view_state *state);
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
/* edit-qct (restore.c): lazy restore of deferred session journals. */
#define EDITOR_RESTORE_MSG UINT32_C(0x45524301) /* message kind tag, like EDITOR_LARGE_MSG_* */
typedef struct editor_restore editor_restore;
typedef struct editor_restore_buffer editor_restore_buffer;
int editor_restore_defer(editor *e, const char *directory, const char *source, int lease);
int editor_restore_start(editor *e, editor_buffer *b);
bool editor_restore_receive(editor *e, const work_msg *msg);
int editor_restore_service(editor *e);
int editor_restore_apply(editor_buffer *b);
int editor_restore_reserve(const editor_restore_buffer *rb, size_t *bytes);
void editor_restore_free(editor_restore *r);
int editor_install_buffer(editor *e, editor_buffer *b, uint64_t *id);
void editor_unpoll_buffer(editor *e, editor_buffer *b);
size_t editor_tab_title(const char *path, const char **title);
bool editor_same_path(const char *path, const char *other);
/* Internal cross-file helpers; UI only. */
int editor_clip_init(editor *e);
void editor_clip_stop(editor *e);
void editor_clip_close(editor *e);
void editor_clip_selection(editor *e);
int editor_clip_action(editor *e, keys_action action, const plat_event *ev);
int editor_clip_find_paste(editor *e);
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

void editor_backend_shutdown_sync(editor *, render_backend *);
int editor_backend_init_sync(editor *e, render_config config, void *state);
int editor_backend_start_probe(editor *e, render_backend backend, render_config config, void *state);
int editor_backend_poll_probe(editor *e);
void editor_backend_close_probe(editor *e);
bool editor_activation_busy(const editor *e);
void editor_font_bad_config(editor *e);
/* edit-6e2: editor_font_request plus the '\n'-joined fallback chain (NULL keeps it). */
int editor_font_request_chain(editor *e, const char *family, const char *file, const char *fallbacks);
int editor_begin_frame(editor *e);
int editor_full_layout(editor *e);
int editor_refresh_cursor(editor *e, uint64_t old_cursor);
int editor_refresh_placed(editor *e, uint64_t old_cursor);
void editor_seed_view_lines(editor *e);
void editor_large_relabel_top(editor *e);
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
/* edit-ivv: JOURNAL_BUSY retry on the journal tick (checkpoint.c). */
bool editor_checkpoint_due(const editor *e);
void editor_checkpoint_owed(editor *e);
void editor_recovery_owed_done(editor *e); /* recovery.c: retire the deferred source */
/* edit-3f2k: the save graph's TABS rank of live tab `index`: the live tabs
 * before it that have a session leaf (editor_tab_recorded). With index =
 * tabs_count, the recorded count. Session publication and save admission use
 * this one mapping. edit-uzrb: pending-open tabs have no complete tree and no
 * leaf, so they are skipped like placeholders and derived tabs; the UI
 * checkpoint's own TABS record (recorded_tabs) still keeps them. */
static inline bool editor_tab_recorded(const editor_buffer *b)
{ return !b->pending_open && !b->placeholder && !b->derived; }
size_t editor_recorded_rank(const editor *e, size_t index);
/* edit-fi2 (retain.c): BASE retention after the first modification. */
enum { EDITOR_RETAIN_IDLE, EDITOR_RETAIN_RUNNING, EDITOR_RETAIN_READY, EDITOR_RETAIN_FAILED };
bool editor_retain_matches(const editor_buffer *b, const journal_base *base);
bool editor_retain_unprotected(const editor_buffer *b);
void editor_retain_tick(editor *e);
void editor_retain_checkpointed(editor *e);
void editor_retain_stop(editor_buffer *b);
void editor_retain_quiesce(editor *e);
/* edit-ze59: complete checkpoint on the bulk worker from the published graph.
 * start: UI, journal quiescent, refuses (EDITOR_ERR_*) when a leaf is stale or
 * the pool is full; poll: true once the worker finished (rc its result);
 * published: the UI bookkeeping editor_checkpoint does after its rotation. */
int editor_publish_start(editor *e);
bool editor_publish_poll(editor *e, int *rc);
void editor_checkpoint_published(editor *e, int rc);
/* edit-y315: exit paths only. Adopts the running worker checkpoint, waiting at
 * most one journal tick from the first wait on it; EDITOR_MORE if it is still
 * running past that deadline, else its result (0 when none ran). */
int editor_publish_wait(editor *e);
void editor_save_as_tick(editor *e);
void editor_save_as_close(editor *e);
bool editor_save_as_runnable(const editor *e);
int editor_journal_metadata(editor *e);
/* Journal an applied action without waiting for a frame or durability credit. */
int editor_journal_changed(editor *e);
/* edit-dzk1: PAINT bookkeeping (checkpoint.c). paint names e->buffer while it
 * has not presented in this launch, zero otherwise; BUSY means retry before
 * submitting. painted records that buffer id presented; paint_clear is the
 * clean-close path (no paint is in flight). */
int editor_journal_paint(editor *e);
void editor_journal_painted(editor *e, uint64_t id);
void editor_journal_paint_clear(editor *e);
void editor_encode_view(view_state state, uint8_t out[JOURNAL_VIEW_STATE_BYTES]);
/* edit-28u: checkpoint text encoding against BASE (docs/decisions/edit-28u.md). */
editor_base_id editor_base_id_of(const journal_base *base);
bool editor_base_id_matches(const editor_base_id *id, const journal_base *base);
/* Emit the INSERT/DELETE records that turn BASE's base_size bytes into the
 * iterated text. With original_is_base, ORIGINAL spans are BASE bytes at the
 * same offsets and are kept in place (no bytes written); every other span is
 * written as INSERT slices of at most JOURNAL_INSERT_SLICE_BYTES. op receives
 * (JOURNAL_DELETE, off, len, NULL) or (JOURNAL_INSERT, off, len, bytes). */
/* True when a checkpoint of a modified text may reference BASE bytes instead
 * of writing the whole text: the tree's original is BASE's generation and
 * BASE is larger than FILE_PREFIX_MAX (smaller texts stay self-contained). */
bool editor_checkpoint_references(const editor_base_id *origin, const journal_base *base);
typedef int (*editor_text_op)(void *ctx, uint32_t type, uint64_t off, uint64_t len, const uint8_t *bytes);
/* edit-f3i: with ref (and !original_is_base), spans found in the reference
 * map are BASE bytes at the mapped offsets: BASE is the generation holding
 * exactly the reference text (a completed save of that snapshot). */
int editor_text_ops(piece_iter *it, uint64_t base_size, bool original_is_base, const editor_ref_map *ref,
                    editor_text_op op, void *ctx);
int editor_ref_map_build(editor_ref_map *m, const piece_snapshot *s);
void editor_ref_map_free(editor_ref_map *m);
/* edit-f3i: install s (retained by the caller, may be NULL) as b's saved
 * reference, releasing the previous one and its map. */
void editor_saved_reference(editor_buffer *b, piece_snapshot *s);
/* edit-28u/f3i: checkpoint record builder shared by editor_checkpoint and the
 * save worker. Two passes over the same unmutated state: the sizing pass
 * (records == NULL) writes each record into one scratch record and counts;
 * editor_records_fill then allocates one record array and one payload arena
 * of exactly that size for the fill pass. No per-record allocation. */
typedef struct editor_records {
    journal_record *records;
    size_t count, capacity;
    uint8_t *payload, *scratch;
    size_t used, bytes;
    int error;
    edit_arena arena;
    bool arena_ready;
} editor_records;
typedef struct editor_records_sink { editor_records *records; uint64_t id; } editor_records_sink;
int editor_records_begin(editor_records *c);
uint8_t *editor_records_add(editor_records *c, uint32_t type, uint64_t id, size_t size);
/* editor_text_op over an editor_records_sink. */
int editor_records_text(void *ctx, uint32_t type, uint64_t off, uint64_t len, const uint8_t *bytes);
int editor_records_fill(editor_records *c);
/* (P) the fill pass revisits the sizing pass's state: equal counts. */
int editor_records_check(const editor_records *c);
void editor_records_free(editor_records *c);
int editor_resize_now(editor *e, uint32_t width, uint32_t height);
bool editor_event_control(const editor *e, const plat_event *ev);
int editor_handle_key(editor *e, const plat_event *ev);
int editor_dispatch_action(editor *, const plat_event *, const keys_binding *, keys_result);
int editor_palette_show(editor *);
int editor_palette_close(editor *);
int editor_palette_sync(editor *);
int editor_palette_key(editor *, const plat_event *);
int editor_palette_paint(editor *);
/* edit-yey menu bar (menubar.c). key/pointer return true when consumed,
 * with the result in *rc. Paint runs at the end of every composition. */
bool editor_menu_visible(const editor *);
bool editor_menu_key(editor *, const plat_event *, int *rc);
bool editor_menu_pointer(editor *, const plat_event *, int *rc);
int editor_menu_toggle(editor *);
int editor_menu_configure(editor *, bool visible);
int editor_menu_reset(editor *);
int editor_menu_paint(editor *);
int editor_handle_wheel(editor *e, const plat_event *ev);
int editor_scroll_init(editor *e, editor_buffer *b);
int editor_scroll_retire(editor_buffer *b);
void editor_scroll_close(editor_buffer *b);
int editor_scroll_slice(editor *e);
int editor_scroll_follow(editor *e);
bool editor_scroll_place(editor *e, uint64_t byte, uint64_t line, bool approximate);
int editor_scroll_seek(editor *e, uint64_t byte);
int editor_scroll_edit(editor *e, uint64_t off, uint64_t old, uint64_t add, uint64_t old_nl, uint64_t new_nl);
int editor_scroll_resize(editor *e);
void editor_scroll_mirror(editor *e);
bool editor_scroll_waiting(const editor *e);
/* edit-pd6: navigation arriving during an unresolved byte jump. */
bool editor_scroll_jumping(const editor *e);
void editor_scroll_queue(editor *e, int64_t pages, int32_t wheel);
void editor_scroll_queue_pixels(editor *e, int64_t pixels_q8);
void editor_scroll_cancel_jump(editor *e);
void editor_refresh_complete(void *ctx, uint32_t id, uint64_t ns);
void editor_refresh_flush(editor *e);
int editor_continue_action(editor *e);
void editor_goto_overlay(editor *e);
int editor_goto_cancel(editor *e);
/* edit-2ye3 (buffers.c): user close (prompts for a modified tab), the close
 * that drops a modified tab outright when the closed pool is all modified
 * (Don't save), the post-save close and the overlay (paint.c). */
int editor_close_request(editor *e);
int editor_close_discard(editor *e, size_t index);
int editor_close_answer_discard(editor *e);
bool editor_close_prompt_shown(const editor *e);
bool editor_close_ready(const editor *e);
int editor_close_service(editor *e);
void editor_close_overlay(editor *e);
int editor_file_command_service(editor *e);
int editor_register_journal(editor *e, editor_buffer *b, uint64_t id);
/* editor_add_buffer without path reuse: journal recovery only (edit-dlfix). */
int editor_add_recovered_buffer(editor *e, const char *path, uint64_t *id);
int editor_buffer_prepare(editor *e, const char *path, const uint8_t *bytes,
                          size_t len, editor_buffer **out);
void editor_buffer_destroy(editor_buffer *b);
int editor_adopt_open(editor *e, editor_buffer *b);
int editor_check_sources(editor *e);
/* Save As peers share its owed worker checkpoint (edit-9fzc). */
int editor_reload_saved_peer(editor *e, editor_buffer *b, size_t tab_index);
/* edit-e3z5: why a reload (buffers.c reload_source) left the buffer in
 * place. REFUSED is JOURNAL_BUSY (retry on the journal tick); DEFERRED waits
 * for a save on the file or staged journal ops (transient); NO_SLOT needs a
 * free buffer slot (a user action); FAILED is any other rollback
 * (acquisition or checkpoint). */
typedef enum editor_reload_outcome {
    EDITOR_RELOAD_DONE, EDITOR_RELOAD_FAILED, EDITOR_RELOAD_REFUSED,
    EDITOR_RELOAD_DEFERRED, EDITOR_RELOAD_NO_SLOT
} editor_reload_outcome;
int editor_activate(editor *e);
int editor_cycle_tab(editor *e, bool reverse);
int editor_compose(editor *e);
int editor_paint_prepare(editor *e);
int editor_font_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot);
bool editor_font_event(editor *e, const work_msg *msg);
void editor_font_track(editor *e, uint32_t slot, uint32_t row);
int editor_pointer(editor *e, const plat_event *ev);
bool editor_pointer_scrolling(const editor *e);
int editor_pointer_tick(editor *e);
int editor_hl_init(editor *e, size_t cells);
void editor_hl_close(editor *e);
void editor_hl_service(editor *e);
void editor_hl_request(editor *e);
void editor_hl_paint(editor *e, uint64_t byte, uint32_t *fg);
int editor_map_service(editor *e);
uint64_t editor_map_retry_ns(const editor *e); /* edit-ci6i: the due retry tick, 0 if none */
bool editor_map_receive(editor *e, const work_msg *msg);
int editor_chrome_pointer(editor *e, const plat_event *ev);
int editor_wheel_continue(editor *e);
int editor_text_geometry(editor *e);
bool editor_wheel_ready(const editor *e);
int editor_scroll_resolve(editor *e, scroll_state *s, scroll_resolver *r);
bool editor_scroll_deferred(const editor_buffer *b); /* edit-bt1/ci6i: a refused install waits for its tick */
void editor_hit_record(void *ctx, uint32_t row, uint32_t column, uint32_t count,
    uint32_t skip, uint32_t width, uint64_t before, uint64_t after);
int editor_find_init(editor *e);
void editor_find_close(editor *e);
int editor_find_service(editor *e);
bool editor_find_key(editor *e, const plat_event *ev);
bool editor_find_action(editor *e, const plat_event *ev, const keys_binding *binding);
int editor_find_paste(editor *e, const uint8_t *bytes, size_t length, bool replacement);
int editor_find_paint(editor *e);
int editor_find_replace_continue(editor *e);
/* edit-n95 (fif.c): find in files panel mode, job lifecycle, results tab. */
int editor_fif_init(editor *e);
void editor_fif_close(editor *e);
bool editor_fif_receive(editor *e, const work_msg *msg);
int editor_fif_service(editor *e);
bool editor_fif_runnable(const editor *e);
bool editor_fif_pending(const editor *e);
void editor_fif_show(editor *e);
int editor_fif_start(editor *e);
bool editor_fif_cancel(editor *e);
bool editor_fif_key(editor *e, const plat_event *ev);
void editor_fif_paint(editor *e, uint32_t first_row, uint32_t rows);
int editor_fif_paste(editor *e, const uint8_t *bytes, size_t length);
int editor_fif_open_at_caret(editor *e);
int editor_fif_step(editor *e, int direction);
bool editor_fif_results_active(const editor *e);
/* buffers.c: replace the Find Results tab's buffer with an empty derived
 * read-only one (or open the tab), and activate it. */
int editor_results_open(editor *e, uint64_t *id);
/* input.c: the owner's append into the active read-only results buffer. */
int editor_results_append(editor *e, const uint8_t *bytes, size_t length);
static inline bool editor_read_only(const editor *e) { return e->buffer && e->buffer->read_only; }
findui_mutation_result editor_find_delete(void *ctx, undo_log *undo, uint64_t off, uint64_t len,
    uint64_t when, const undo_state *before, const undo_state *after);
findui_mutation_result editor_find_insert(void *ctx, undo_log *undo, uint64_t off, const uint8_t *bytes,
    size_t len, uint64_t when, const undo_state *before, const undo_state *after);
void editor_find_replace_finish(editor *e);
int editor_poll_sources(editor *e);
int editor_preferences_poll(editor *e);
int editor_preferences_apply(editor *e);
int editor_preferences_font_init(editor *e);
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
