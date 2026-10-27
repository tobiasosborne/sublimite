/* edit-cxho.16: independent mode x action witnesses, through editor input.
 * Numbers (P): zero is success/absence/first index, one is a unit step or C
 * terminator; counts/lengths come from enums, arrays and fixture strings.
 * (E) Geometry reuses n95_find_in_files's 120 columns / 24 rows. The extra
 * resize cell witnesses a changed extent; no new capacity or time budget.
 * (E) EDITOR_IDLE_NS is the existing editor test settle watchdog, not a gate.
 * No production state is written except the existing headless drop seam.
 */
#include "editor/private.h"
#include "trace/trace.h"
#include "tree_fixture.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "modal_matrix_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
#define N(a) (sizeof(a) / sizeof *(a))
#define CS (PLAT_MOD_CTRL | PLAT_MOD_SHIFT)
typedef enum mode {
    NONE, FIND_FIELD, FIND_DOCUMENT, REPLACE, FIF_WHERE, GOTO, PALETTE,
    MENU, CLOSE_PROMPT, RELOAD_PROMPT, RESULTS, MAP_DRAG, TEXT_DRAG, MODE_COUNT
} mode;
typedef enum action {
    CLOSE, NEXT_MRU, PREV_MRU, PREV_TAB, NEXT_TAB, NEW, OPEN, SAVE, REOPEN,
    GO, FIND, COMMANDS, ALT, ESCAPE, CLICK, MIDDLE, TYPE, UNDO, RESIZE, DROP,
    ACTION_COUNT
} action;
typedef enum document { DOC_A, DOC_B, DOC_C, DOC_RESULTS, DOC_NEW, DOC_DROP, DOC_COUNT } document;
typedef enum focus { DOCUMENT, QUERY, WHERE, GO_FIELD, PALETTE_FIELD, MENU_FOCUS, CLOSE_FOCUS, RELOAD_FOCUS } focus;
typedef enum row_owner { ROW_NONE, ROW_FIND, ROW_GO, ROW_PALETTE, ROW_CLOSE, ROW_RELOAD } row_owner;
static const char *const mode_names[] = {
    "none", "find field", "find document", "replace field", "find in files Where",
    "go to line", "command palette", "menu open", "close/save prompt",
    "Reload/Keep prompt", "find results active", "minimap drag", "text drag"
};
static const char *const action_names[] = {
    "Ctrl+W", "Ctrl+Tab", "Ctrl+Shift+Tab", "Ctrl+PgUp", "Ctrl+PgDn",
    "Ctrl+N", "Ctrl+O then cancel", "Ctrl+S", "Ctrl+Shift+T", "Ctrl+G",
    "Ctrl+F", "Ctrl+Shift+P", "Alt", "Escape", "click text", "middle paste",
    "type x", "Ctrl+Z", "resize", "drop"
};
static const char initial[] = "alpha beta\n";
static const char edited[] = "xalpha beta\n";
/* (P) Query has no matches: incremental preview cannot change the fixture's
 * selection or PRIMARY, making paste and command prefill deterministic. */
static const char needle[] = "absent";
typedef struct fixture {
    tree_fixture tree;
    char paths[DOC_COUNT][PATH_MAX];
    char uri[PATH_MAX + sizeof "file://\r\n"];
    editor *e;
    render_backend backend;
    editor_buffer *buffers[DOC_COUNT];
    uint64_t ids[DOC_COUNT], revisions[DOC_COUNT];
} fixture;
typedef struct state {
    size_t tabs, active;
    document doc;
    uint64_t revision, b_revision;
    uint64_t cursor, anchor;
    bool check_selection, open_seen;
    bool modified, find, replace, fif, palette, close, stale, results;
    bool map_drag, text_drag;
    focus focused;
    row_owner painted;
    int menu; /* (P) -1 closed, -2 latched bar, otherwise menu index. */
    keys_action go;
    /* (P) all tested field inputs fit these literal-derived extents. Where
     * and path prompts retain the platform's established path bound. */
    char query[sizeof needle + sizeof "x"], palette_query[PALETTE_LABEL_CAP];
    char where[PATH_MAX], go_text[IPC_PATH_CAP];
    uint32_t cols, rows;
} state;

static plat_event key(uint32_t sym, uint16_t mods, const char *text)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods};
    if (text) { EDIT_ASSERT(strlen(text) <= PLAT_UTF8_MAX); ev.utf8_len = (uint8_t)strlen(text); memcpy(ev.utf8, text, ev.utf8_len); }
    return ev;
}
static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    do {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active && !editor_save_active(e) && !editor_clip_pending(e)) {
            T(!e->error && !s.error_cause && !s.journal_error && !e->find_error);
            return 0;
        }
        sched_yield();
    } while (trace_now_ns() < deadline);
    T(false); return 1;
}
static int send_event(editor *e, plat_event ev)
{
    T(editor_inject(e, &ev) == EDITOR_OK); return settle(e);
}
static int chord(editor *e, uint32_t sym, uint16_t mods)
{
    return send_event(e, key(sym, mods, NULL));
}
static int type_string(editor *e, const char *text)
{
    for (size_t i = 0; text[i]; i++) {
        char byte[] = {text[i], '\0'};
        T(send_event(e, key((uint32_t)(uint8_t)text[i], 0, byte)) == 0);
    }
    return 0;
}
static plat_event text_button(editor *e, uint16_t button, bool press)
{
    return (plat_event){.kind = PLAT_EV_BUTTON, .code = button, .press = press,
        /* Rightmost text cell is outside the File dropdown. */
        .x = (int32_t)((e->text_grid.dims.cols - 1u) * e->grid.dims.cell_w),
        .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h)};
}
static int click_text(editor *e)
{
    T(send_event(e, text_button(e, 1, true)) == 0);
    return send_event(e, text_button(e, 1, false));
}
static int alt(editor *e)
{
    T(chord(e, XKB_KEY_Alt_L, 0) == 0);
    plat_event release = key(XKB_KEY_Alt_L, PLAT_MOD_ALT, NULL); release.press = false;
    return send_event(e, release);
}
static int find_setup(editor *e, uint16_t mods, uint32_t sym)
{
    T(chord(e, sym, mods) == 0);
    T(send_event(e, key('a', 0, needle)) == 0);
    /* Select the field, so a letter replaces the query in every field row. */
    return chord(e, 'a', PLAT_MOD_CTRL);
}
static int fixture_open(fixture *f, mode m, action a)
{
    strcpy(f->tree.root, "/tmp/modal-matrix-XXXXXX"); T(mkdtemp(f->tree.root));
    static const char *const names[] = {"a", "b", "c", "results", "new", "drop"};
    for (size_t i = 0; i < N(names); i++) {
        T(tree_fixture_path(&f->tree, names[i], f->paths[i]) == 0);
        if (i != DOC_RESULTS && i != DOC_NEW) T(tree_fixture_file(&f->tree, names[i], initial, sizeof initial - 1u) == 0);
    }
    int n = snprintf(f->uri, sizeof f->uri, "file://%s\r\n", f->paths[DOC_DROP]);
    T(n > 0 && (size_t)n < sizeof f->uri);
    T(render_null_backend(&f->backend) == RENDER_OK);
    editor_config cfg = {.path = f->paths[DOC_A], .cols = 120, .rows = 1u + 3u + 20u, .wrap_mode = -1};
    T(editor_open(&f->e, &cfg, &f->backend) == EDITOR_OK);
    editor *e = f->e; T(settle(e) == 0);
    f->ids[DOC_A] = e->buffer->id; f->buffers[DOC_A] = e->buffer;
    T(editor_add_buffer(e, f->paths[DOC_B], NULL, 0, &f->ids[DOC_B]) == 0); T(settle(e) == 0);
    f->buffers[DOC_B] = e->buffer;
    T(send_event(e, key('x', 0, "x")) == 0);
    T(chord(e, 's', PLAT_MOD_CTRL) == 0); T(!tabs_active(&e->tabs)->modified);
    uint8_t bytes[sizeof edited - 1u];
    T(editor_length(e) == sizeof bytes && editor_read(e, 0, bytes, sizeof bytes) == 0);
    T(!memcmp(bytes, edited, sizeof bytes));
    /* Publish PRIMARY by a real selection; collapse without replacing it. */
    T(editor_set_cursor(e, 0) == 0); T(settle(e) == 0);
    T(chord(e, XKB_KEY_Right, PLAT_MOD_SHIFT) == 0);
    T(chord(e, 'c', PLAT_MOD_CTRL) == 0);
    T(editor_set_cursor(e, 0) == 0); T(settle(e) == 0);
    T(editor_add_buffer(e, f->paths[DOC_C], NULL, 0, &f->ids[DOC_C]) == 0); T(settle(e) == 0);
    f->buffers[DOC_C] = e->buffer;
    T(editor_close_tab(e, tabs_active_index(&e->tabs)) == 0); T(settle(e) == 0);
    T(e->buffer == f->buffers[DOC_B]);
    /* A save must clear a real dirty edit, rather than pass as a no-op. */
    if (a == SAVE && m != CLOSE_PROMPT && m != RELOAD_PROMPT && m != RESULTS)
        T(send_event(e, key('x', 0, "x")) == 0);
    switch (m) {
    case FIND_FIELD: case FIND_DOCUMENT: case GOTO: case PALETTE: case MENU:
        T(find_setup(e, PLAT_MOD_CTRL, 'f') == 0);
        if (m == FIND_DOCUMENT) T(click_text(e) == 0);
        if (m == GOTO) T(chord(e, 'g', PLAT_MOD_CTRL) == 0);
        if (m == PALETTE) T(chord(e, 'p', CS) == 0);
        if (m == MENU) T(chord(e, 'f', PLAT_MOD_ALT) == 0);
        break;
    case REPLACE: T(find_setup(e, PLAT_MOD_CTRL, 'h') == 0); break;
    case FIF_WHERE:
        T(find_setup(e, CS, 'f') == 0); T(chord(e, XKB_KEY_Tab, 0) == 0); break;
    case CLOSE_PROMPT: case RELOAD_PROMPT:
        T(send_event(e, key('x', 0, "x")) == 0);
        if (m == CLOSE_PROMPT) {
            T(find_setup(e, PLAT_MOD_CTRL, 'f') == 0);
            T(chord(e, 'w', PLAT_MOD_CTRL) == 0);
            T(editor_close_prompt_shown(e) && e->close_prompt.prompt_ns);
        }
        else {
            const char external[] = "external\n";
            T(tree_fixture_file(&f->tree, "b", external, sizeof external - 1u) == 0);
            uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
            while (!e->buffer->source_stale) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
            T(settle(e) == 0); T(e->buffer->prompt_ns);
        }
        break;
    case RESULTS:
        T(find_setup(e, CS, 'f') == 0); T(chord(e, XKB_KEY_Tab, 0) == 0);
        T(type_string(e, f->tree.root) == 0);
        T(chord(e, XKB_KEY_Return, 0) == 0);
        T(editor_fif_results_active(e) && e->fif.done_received);
        f->ids[DOC_RESULTS] = e->buffer->id; f->buffers[DOC_RESULTS] = e->buffer;
        break;
    case MAP_DRAG: {
        plat_event ev = text_button(e, 1, true);
        ev.x = (int32_t)(e->text_grid.dims.cols * e->grid.dims.cell_w);
        T(send_event(e, ev) == 0); T(e->map_drag); break;
    }
    case TEXT_DRAG: {
        /* A row inside the view remains inside when Alt adds its menu row. */
        plat_event ev = text_button(e, 1, true); ev.y += (int32_t)e->grid.dims.cell_h;
        T(send_event(e, ev) == 0); T(e->text_drag); break;
    }
    case NONE: break;
    case MODE_COUNT: T(false);
    }
    for (size_t i = 0; i < DOC_COUNT; i++) if (f->buffers[i]) f->revisions[i] = f->buffers[i]->revision;
    return 0;
}
static int fixture_close(fixture *f)
{
    if (f->e) editor_close(f->e);
    return *f->tree.root ? tree_fixture_remove(f->tree.root) : 0;
}
static focus state_focus(const state *s)
{
    if (s->close) return CLOSE_FOCUS;
    if (s->palette) return PALETTE_FIELD;
    if (s->menu != -1) return MENU_FOCUS;
    if (s->go) return GO_FIELD;
    if (s->stale) return RELOAD_FOCUS;
    return s->focused;
}
static row_owner expected_owner(const state *s)
{
    if (s->close) return ROW_CLOSE;
    if (s->go && !s->stale) return ROW_GO;
    if (s->stale) return ROW_RELOAD;
    if (s->palette) return ROW_PALETTE;
    return s->find ? ROW_FIND : ROW_NONE;
}
static bool grid_prefix(editor *e, uint32_t row, const char *text)
{
    for (size_t i = 0; text[i]; i++) {
        if (i >= e->grid.dims.cols || e->grid.cells[(size_t)row * e->grid.dims.cols + i].glyph_index != (uint32_t)(uint8_t)text[i]) return false;
    }
    return true;
}
static int snapshot(const fixture *f, state *s)
{
    editor *e = f->e; findui_state fs = findui_get_state(&e->find);
    *s = (state){.tabs = tabs_count(&e->tabs), .active = tabs_active_index(&e->tabs),
        .doc = DOC_NEW, .revision = e->buffer->revision, .b_revision = f->buffers[DOC_B]->revision,
        .modified = tabs_active(&e->tabs)->modified, .find = e->find_visible,
        .replace = fs.replace_mode, .fif = e->fif.mode, .palette = e->palette_open,
        .close = e->close_prompt.owner != 0, .stale = e->buffer->source_stale,
        .results = editor_fif_results_active(e), .map_drag = e->map_drag, .text_drag = e->text_drag,
        .focused = e->find_visible && e->find_focus == EDITOR_FIND_FIELD ?
            (e->fif.mode && e->fif.where_focus ? WHERE : QUERY) : DOCUMENT,
        .menu = e->menu.open >= 0 ? e->menu.open : e->menu.active ? -2 : -1,
        .go = e->go.active ? e->go.kind : KEYS_ACTION_NONE,
        .cols = e->grid.dims.cols, .rows = e->grid.dims.rows};
    s->cursor = e->v.state.selection.cursor; s->anchor = e->v.state.selection.anchor;
    for (size_t i = 0; i < DOC_COUNT; i++) if (f->ids[i] && f->ids[i] == e->buffer->id) s->doc = (document)i;
    if (!strcmp(e->buffer->path, f->paths[DOC_DROP])) s->doc = DOC_DROP;
    T(fs.query_length < sizeof s->query && e->palette->query_length < sizeof s->palette_query);
    if (fs.query_length) memcpy(s->query, fs.query, fs.query_length);
    memcpy(s->palette_query, e->palette->query, e->palette->query_length);
    if (s->fif) strcpy(s->where, e->fif.where);
    if (s->go) strcpy(s->go_text, e->go.digits);
    uint32_t last = e->grid.dims.rows - 1u;
    if (grid_prefix(e, last, "Save changes to ")) s->painted = ROW_CLOSE;
    else if (grid_prefix(e, last, "Go to line: :") || grid_prefix(e, last, "Open file: ") || grid_prefix(e, last, "Save as: ")) s->painted = ROW_GO;
    else if (grid_prefix(e, last, "Source changed: ")) s->painted = ROW_RELOAD;
    else if (e->panel_rows) {
        uint32_t first = e->grid.dims.rows - e->panel_rows;
        if (grid_prefix(e, first, "Command: ")) s->painted = ROW_PALETTE;
        else if (grid_prefix(e, first, "Find: ")) s->painted = ROW_FIND;
    }
    return 0;
}
static int precondition(mode m, state s)
{
    static const struct {
        bool find, replace, fif, palette, close, stale, results, map_drag, text_drag;
        focus field;
        keys_action go;
        int menu;
    } rows[] = {
        {false, false, false, false, false, false, false, false, false, DOCUMENT, KEYS_ACTION_NONE, -1},
        {true, false, false, false, false, false, false, false, false, QUERY, KEYS_ACTION_NONE, -1},
        {true, false, false, false, false, false, false, false, false, DOCUMENT, KEYS_ACTION_NONE, -1},
        {true, true, false, false, false, false, false, false, false, QUERY, KEYS_ACTION_NONE, -1},
        {true, false, true, false, false, false, false, false, false, WHERE, KEYS_ACTION_NONE, -1},
        {true, false, false, false, false, false, false, false, false, QUERY, KEYS_ACTION_GOTO_LINE, -1},
        {true, false, false, true, false, false, false, false, false, QUERY, KEYS_ACTION_NONE, -1},
        {true, false, false, false, false, false, false, false, false, QUERY, KEYS_ACTION_NONE, 0},
        {true, false, false, false, true, false, false, false, false, QUERY, KEYS_ACTION_NONE, -1},
        {false, false, false, false, false, true, false, false, false, DOCUMENT, KEYS_ACTION_NONE, -1},
        {false, false, false, false, false, false, true, false, false, DOCUMENT, KEYS_ACTION_NONE, -1},
        {false, false, false, false, false, false, false, true, false, DOCUMENT, KEYS_ACTION_NONE, -1},
        {false, false, false, false, false, false, false, false, true, DOCUMENT, KEYS_ACTION_NONE, -1}
    };
    T(N(rows) == MODE_COUNT);
    T(s.tabs == (size_t)DOC_C + (s.results ? 1u : 0u));
    T(s.active == (s.results ? (size_t)DOC_C : (size_t)DOC_B));
    T(s.doc == (s.results ? DOC_RESULTS : DOC_B));
    T(s.find == rows[m].find && s.replace == rows[m].replace && s.fif == rows[m].fif);
    T(s.palette == rows[m].palette && s.close == rows[m].close && s.stale == rows[m].stale);
    T(s.results == rows[m].results && s.map_drag == rows[m].map_drag && s.text_drag == rows[m].text_drag);
    T(s.focused == rows[m].field && s.go == rows[m].go && s.menu == rows[m].menu);
    T(s.painted == expected_owner(&s));
    if (s.find || s.results) T(!strcmp(s.query, needle));
    return 0;
}
static void hide_find(state *s)
{
    s->find = s->fif = false; s->focused = DOCUMENT;
    /* findui retains replace_mode while hidden. */
}
static void activate(const fixture *f, state *s, document d, size_t index)
{
    s->doc = d; s->active = index; s->revision = f->revisions[d];
    s->modified = false; s->stale = s->close = false; s->results = d == DOC_RESULTS;
    s->map_drag = s->text_drag = false;
    s->go = KEYS_ACTION_NONE; s->go_text[0] = '\0';
}
static void mutate(state *s)
{
    if (s->doc == DOC_RESULTS || s->stale) return;
    s->revision++; s->modified = true;
    if (s->doc == DOC_B) s->b_revision++;
}
/* Normative expected values. Required-answer prompts own keyboard commands;
 * a navigation prompt yields to global chords or a primary text click.
 * Global chords leave the menu/palette before dispatch. Fields retain undo. */
static state expected(const fixture *f, mode m, action a, state s)
{
    if (m == CLOSE_PROMPT && a == DROP) return s; /* preserve the unanswered prompt */
    if (m == CLOSE_PROMPT && a != ESCAPE && a != CLICK && a != MIDDLE && a != RESIZE && a != DROP) return s;
    if (m == RELOAD_PROMPT && a != CLOSE && a != NEXT_MRU && a != PREV_MRU &&
        a != PREV_TAB && a != NEXT_TAB && a != NEW && a != OPEN && a != SAVE && a != REOPEN && a != RESIZE && a != DROP) return s;
    if (m == GOTO && a > COMMANDS && a != ESCAPE && a != CLICK && a != MIDDLE && a != TYPE && a != RESIZE && a != DROP) return s;
    if (m == PALETTE && a == UNDO) return s; /* no palette query undo log */
    bool global = a <= COMMANDS || a == UNDO;
    if (m == GOTO && global) { s.go = KEYS_ACTION_NONE; s.go_text[0] = '\0'; }
    if (s.menu != -1 && global) s.menu = -1;
    if (s.palette && global) s.palette = false;
    switch (a) {
    case CLOSE:
        if (s.modified) s.close = true;
        else { s.tabs--; activate(f, &s, s.doc == DOC_RESULTS ? DOC_B : DOC_A, s.doc == DOC_RESULTS ? 1u : 0u); }
        break;
    case NEXT_MRU: case PREV_MRU: case PREV_TAB: case NEXT_TAB:
        activate(f, &s, s.doc == DOC_RESULTS && a != NEXT_TAB && a != PREV_MRU ? DOC_B : DOC_A,
            s.doc == DOC_RESULTS && a != NEXT_TAB && a != PREV_MRU ? 1u : 0u); break;
    case NEW: s.tabs++; activate(f, &s, DOC_NEW, s.tabs - 1u); break;
    case OPEN:
        /* Assert the intermediate prompt before the cancelling Escape too. */
        break;
    case SAVE: if (!s.results && !s.stale) s.modified = false; break;
    case REOPEN: s.tabs++; activate(f, &s, DOC_C, DOC_C); break;
    case GO: s.go = KEYS_ACTION_GOTO_LINE; break;
    case FIND:
        s.find = true; s.fif = s.replace = false; s.focused = QUERY;
        if (!*s.query) s.query[0] = '\0';
        break;
    case COMMANDS: s.palette = true; s.text_drag = false; break;
    case ALT:
        if (s.palette) break;
        s.menu = s.menu != -1 ? -1 : -2; break;
    case ESCAPE:
        if (s.close) s.close = false;
        else if (s.palette) s.palette = false;
        else if (s.menu >= 0) s.menu = -2;
        else if (s.menu == -2) s.menu = -1;
        else if (s.go) { s.go = KEYS_ACTION_NONE; s.go_text[0] = '\0'; }
        else if (s.find) hide_find(&s);
        break;
    case CLICK:
        if (s.close || s.palette || s.stale) { s.check_selection = true; break; } /* true modals own the mouse */
        s.menu = -1; s.go = KEYS_ACTION_NONE; s.go_text[0] = '\0';
        s.focused = DOCUMENT; s.map_drag = s.text_drag = false; break;
    case MIDDLE:
        if (s.close || s.palette || s.go || s.stale) break;
        s.menu = -1; s.focused = DOCUMENT; mutate(&s); break;
    case TYPE:
        if (s.palette) strcpy(s.palette_query, "x");
        else if (s.menu != -1) break;
        else if (s.go) break; /* x is not a line-number digit */
        else if (s.find && s.focused != DOCUMENT) {
            if (s.focused == WHERE) strcpy(s.where, "x"); else strcpy(s.query, "x");
        } else mutate(&s);
        break;
    case UNDO:
        if (s.find && s.focused != DOCUMENT) break;
        mutate(&s); break;
    case RESIZE: s.cols++; s.rows++; s.map_drag = s.text_drag = false; break;
    case DROP:
        s.tabs++; s.palette = false; s.menu = -1;
        activate(f, &s, DOC_DROP, s.tabs - 1u); break;
    case ACTION_COUNT: break;
    }
    if (a == OPEN) s.open_seen = true;
    if (!s.fif) s.where[0] = '\0';
    if (!s.go) s.go_text[0] = '\0';
    return s;
}
static int perform(fixture *f, action a, bool *open_seen)
{
    editor *e = f->e;
    static const struct { uint32_t sym; uint16_t mods; } chords[] = {
        {'w', PLAT_MOD_CTRL}, {XKB_KEY_Tab, PLAT_MOD_CTRL}, {XKB_KEY_Tab, CS},
        {XKB_KEY_Page_Up, PLAT_MOD_CTRL}, {XKB_KEY_Page_Down, PLAT_MOD_CTRL},
        {'n', PLAT_MOD_CTRL}, {'o', PLAT_MOD_CTRL}, {'s', PLAT_MOD_CTRL}, {'t', CS},
        {'g', PLAT_MOD_CTRL}, {'f', PLAT_MOD_CTRL}, {'p', CS}
    };
    if ((size_t)a < N(chords)) {
        T(chord(e, chords[a].sym, chords[a].mods) == 0);
        if (a == OPEN) {
            *open_seen = e->go.active && e->go.kind == KEYS_ACTION_OPEN_FILE;
            if (*open_seen) T(chord(e, XKB_KEY_Escape, 0) == 0);
        }
        return 0;
    }
    switch (a) {
    case ALT: return alt(e);
    case ESCAPE: return chord(e, XKB_KEY_Escape, 0);
    case CLICK: return click_text(e);
    case MIDDLE: return send_event(e, text_button(e, 2, true)); /* (P) X11 middle button */
    case TYPE: return send_event(e, key('x', 0, "x"));
    case UNDO: return chord(e, 'z', PLAT_MOD_CTRL);
    case RESIZE: return send_event(e, (plat_event){.kind = PLAT_EV_RESIZE,
        .w = (e->grid.dims.cols + 1u) * e->grid.dims.cell_w,
        .h = (e->grid.dims.rows + 1u) * e->grid.dims.cell_h});
    case DROP: {
        e->drop_data = (const uint8_t *)f->uri; e->drop_size = strlen(f->uri);
        int rc = send_event(e, (plat_event){.kind = PLAT_EV_DROP});
        e->drop_data = NULL; e->drop_size = 0; return rc;
    }
    default: T(false);
    }
    return 1;
}
static bool equal(state a, state b)
{
    return a.tabs == b.tabs && a.active == b.active && a.doc == b.doc &&
        a.revision == b.revision && a.b_revision == b.b_revision && a.modified == b.modified &&
        a.find == b.find && a.replace == b.replace && a.fif == b.fif &&
        a.palette == b.palette && a.close == b.close && a.stale == b.stale && a.results == b.results &&
        a.map_drag == b.map_drag && a.text_drag == b.text_drag && a.focused == b.focused &&
        state_focus(&a) == state_focus(&b) && expected_owner(&a) == b.painted && a.menu == b.menu && a.go == b.go &&
        !strcmp(a.query, b.query) && !strcmp(a.palette_query, b.palette_query) &&
        !strcmp(a.where, b.where) && !strcmp(a.go_text, b.go_text) && a.cols == b.cols && a.rows == b.rows &&
        a.open_seen == b.open_seen && (!a.check_selection || (a.cursor == b.cursor && a.anchor == b.anchor));
}
static void describe(const char *label, state s)
{
    fprintf(stderr, "%s tabs=%zu active=%zu doc=%d rev=%llu b_rev=%llu modified=%d find/replace/fif=%d/%d/%d focus=%d(field=%d) query='%s' palette=%d:'%s' menu=%d go=%d:'%s' close=%d stale=%d results=%d drag=%d/%d extent=%u/%u\n",
        label, s.tabs, s.active, (int)s.doc, (unsigned long long)s.revision,
        (unsigned long long)s.b_revision, s.modified, s.find, s.replace, s.fif,
        (int)state_focus(&s), (int)s.focused, s.query, s.palette, s.palette_query,
        s.menu, (int)s.go, s.go_text, s.close, s.stale, s.results, s.map_drag, s.text_drag, s.cols, s.rows);
    fprintf(stderr, "  where='%s' selection=%llu/%llu open_seen=%d row_owner=%d\n", s.where,
        (unsigned long long)s.anchor, (unsigned long long)s.cursor, s.open_seen, (int)s.painted);
}

/* XFAIL: policy violations only. Each row pins the observed state as well
 * as requiring a mismatch against the intended state. XPASS and any change
 * in the pinned failure are errors: this is not a blanket failure allowance. */
typedef enum observed {
    UNCHANGED, DISMISS_WITH_FIELD_FOCUS, PALETTE_CARET, PASTE_BEHIND_GO,
    PASTE_BEHIND_PALETTE, PASTE_BEHIND_MENU, CLOSE_CARET, PASTE_BEHIND_CLOSE,
    DROP_LOSES_CLOSE, LIVE_SEARCH_AFTER_CLOSE
} observed;
typedef struct expected_failure { mode m; action a; observed current; const char *reason; } expected_failure;
static const expected_failure xfails[] = {
    {GOTO, CLOSE, UNCHANGED, "edit-cxho.17: go-to swallows window Close chord"},
    {GOTO, NEXT_MRU, UNCHANGED, "edit-cxho.17: go-to swallows next MRU tab chord"},
    {GOTO, PREV_MRU, UNCHANGED, "edit-cxho.17: go-to swallows previous MRU tab chord"},
    {GOTO, PREV_TAB, UNCHANGED, "edit-cxho.17: go-to swallows previous adjacent tab chord"},
    {GOTO, NEXT_TAB, UNCHANGED, "edit-cxho.17: go-to swallows next adjacent tab chord"},
    {GOTO, NEW, UNCHANGED, "edit-cxho.17: go-to swallows New File chord"},
    {GOTO, OPEN, UNCHANGED, "edit-cxho.17: go-to swallows Open File chord; no open prompt appears"},
    {GOTO, SAVE, UNCHANGED, "edit-cxho.17: go-to swallows Save chord; dirty document stays dirty"},
    {GOTO, REOPEN, UNCHANGED, "edit-cxho.17: go-to swallows Reopen File chord"},
    {GOTO, FIND, UNCHANGED, "edit-cxho.17: go-to swallows Find chord"},
    {GOTO, COMMANDS, UNCHANGED, "edit-cxho.17: go-to swallows Command Palette chord"},
    {GOTO, CLICK, DISMISS_WITH_FIELD_FOCUS, "edit-cxho.17: click cancels go-to but leaves find field focus and caret unchanged"},
    {GOTO, MIDDLE, PASTE_BEHIND_GO, "edit-cxho.17: middle paste edits the document behind go-to"},
    {PALETTE, CLOSE, UNCHANGED, "edit-cxho.17: palette swallows window Close chord"},
    {PALETTE, NEXT_MRU, UNCHANGED, "edit-cxho.17: palette swallows next MRU tab chord"},
    {PALETTE, PREV_MRU, UNCHANGED, "edit-cxho.17: palette swallows previous MRU tab chord"},
    {PALETTE, PREV_TAB, UNCHANGED, "edit-cxho.17: palette swallows previous adjacent tab chord"},
    {PALETTE, NEXT_TAB, UNCHANGED, "edit-cxho.17: palette swallows next adjacent tab chord"},
    {PALETTE, NEW, UNCHANGED, "edit-cxho.17: palette swallows New File chord"},
    {PALETTE, OPEN, UNCHANGED, "edit-cxho.17: palette swallows Open File chord; no prompt appears"},
    {PALETTE, SAVE, UNCHANGED, "edit-cxho.17: palette swallows Save chord; dirty document stays dirty"},
    {PALETTE, REOPEN, UNCHANGED, "edit-cxho.17: palette swallows Reopen File chord"},
    {PALETTE, GO, UNCHANGED, "edit-cxho.17: palette swallows Go to Line chord"},
    {PALETTE, FIND, UNCHANGED, "edit-cxho.17: palette swallows Find chord"},
    {PALETTE, CLICK, PALETTE_CARET, "edit-cxho.17: primary click moves the document caret behind palette"},
    {PALETTE, MIDDLE, PASTE_BEHIND_PALETTE, "edit-cxho.17: middle paste edits document and closes palette"},
    {MENU, CLICK, DISMISS_WITH_FIELD_FOCUS, "edit-cxho.17: text click dismisses menu but retains find field focus"},
    {MENU, MIDDLE, PASTE_BEHIND_MENU, "edit-cxho.17: middle paste edits behind open menu without transferring focus"},
    {CLOSE_PROMPT, CLICK, CLOSE_CARET, "edit-cxho.17: primary click moves caret and find focus behind close prompt"},
    {CLOSE_PROMPT, MIDDLE, PASTE_BEHIND_CLOSE, "edit-cxho.17: middle paste edits document behind unanswered close prompt"},
    {CLOSE_PROMPT, DROP, DROP_LOSES_CLOSE, "edit-cxho.17: drop activates a new tab and silently cancels unanswered close prompt"},
    /* MODE_COUNT denotes the supplemental running-job witness, not a matrix row. */
    {MODE_COUNT, CLOSE, LIVE_SEARCH_AFTER_CLOSE, "edit-cxho.18: closing live Find Results leaves its search running"}
};
static const expected_failure *find_xfail(mode m, action a)
{
    for (size_t i = 0; i < N(xfails); i++) if (xfails[i].m == m && xfails[i].a == a) return &xfails[i];
    return NULL;
}
static state observed_state(const fixture *f, const expected_failure *xf, state before, state want)
{
    state current = before;
    switch (xf->current) {
    case UNCHANGED: current.check_selection = true; break;
    case DISMISS_WITH_FIELD_FOCUS:
        current = want; current.focused = before.focused;
        current.check_selection = true; current.cursor = before.cursor; current.anchor = before.anchor; break;
    case PALETTE_CARET:
        current.check_selection = true;
        current.cursor = current.anchor = sizeof "xalpha beta" - 1u; break;
    case PASTE_BEHIND_GO: case PASTE_BEHIND_PALETTE: case PASTE_BEHIND_MENU:
        mutate(&current); current.check_selection = true;
        current.cursor = current.anchor = sizeof "xalpha beta"; /* (P) line prefix plus copied x */
        if (xf->current == PASTE_BEHIND_PALETTE) current.palette = false;
        break;
    case CLOSE_CARET:
        current.focused = DOCUMENT; current.check_selection = true;
        current.cursor = current.anchor = sizeof "xxalpha beta" - 1u; break;
    case PASTE_BEHIND_CLOSE:
        mutate(&current); current.focused = DOCUMENT; current.check_selection = true;
        current.cursor = current.anchor = sizeof "xxalpha beta"; break;
    case DROP_LOSES_CLOSE: current = expected(f, NONE, DROP, before); break;
    case LIVE_SEARCH_AFTER_CLOSE: break;
    }
    return current;
}

typedef enum route { VIA_MENU, VIA_PALETTE } route;
static const struct command_case { action a; keys_action command; const char *label; } commands[] = {
    {CLOSE, KEYS_ACTION_CLOSE, "Close"}, {NEW, KEYS_ACTION_NEW_FILE, "New File"},
    {OPEN, KEYS_ACTION_OPEN_FILE, "Open File"}, {SAVE, KEYS_ACTION_SAVE, "Save"},
    {GO, KEYS_ACTION_GOTO_LINE, "Goto Line"}, {UNDO, KEYS_ACTION_UNDO, "Undo"}
};
static int run_command(fixture *f, route via, const struct command_case *c, bool *open_seen)
{
    editor *e = f->e;
    if (via == VIA_MENU) {
        size_t group = 0, item = 0;
        bool found = false;
        for (group = 0; group < MENU_COUNT && !found; group++) {
            const menu_def *def = &menu_table()[group];
            for (item = 0; item < def->count; item++) if (def->items[item] == c->command) { found = true; break; }
        }
        T(found); group--;
        T(chord(e, (uint32_t)(uint8_t)menu_table()[group].mnemonic, PLAT_MOD_ALT) == 0);
        for (size_t i = 0; i < item; i++) T(chord(e, XKB_KEY_Down, 0) == 0);
        T(e->menu.open == (int)group && e->menu.item == item);
    } else {
        T(chord(e, 'p', CS) == 0); T(e->palette_open);
        T(type_string(e, c->label) == 0);
        size_t selected = 0;
        while (selected < e->palette->count &&
            e->palette->rows[e->palette->matches[selected]].binding->action != c->command) selected++;
        T(selected < e->palette->count);
        for (size_t i = 0; i < selected; i++) T(chord(e, XKB_KEY_Down, 0) == 0);
    }
    T(chord(e, XKB_KEY_Return, 0) == 0);
    if (c->a == OPEN) {
        *open_seen = e->go.active && e->go.kind == KEYS_ACTION_OPEN_FILE;
        if (*open_seen) T(chord(e, XKB_KEY_Escape, 0) == 0);
    }
    return 0;
}
static int command_witnesses(size_t *passed, size_t *failed)
{
    puts("\n| Find focus | Command | Menu | Palette |\n|---|---|---|---|");
    const mode modes[] = {FIND_FIELD, FIND_DOCUMENT};
    for (size_t i = 0; i < N(modes); i++) for (size_t j = 0; j < N(commands); j++) {
        const struct command_case *c = &commands[j];
        printf("| %s | %s |", mode_names[modes[i]], c->label);
        for (route via = VIA_MENU; via <= VIA_PALETTE; via++) {
            fixture f = {0}; state before = {0}, actual = {0};
            int rc = fixture_open(&f, modes[i], c->a);
            if (!rc) rc = snapshot(&f, &before);
            if (!rc) rc = precondition(modes[i], before);
            state want = before;
            if (!rc) {
                /* Explicit menu/palette Undo targets the document even while
                 * the field owns the physical Ctrl+Z chord (landed decision). */
                if (c->a == UNDO) want.focused = DOCUMENT;
                want = expected(&f, modes[i], c->a, want);
                want.focused = before.focused;
                if (via == VIA_PALETTE) strcpy(want.palette_query, c->label);
            }
            bool open_seen = false;
            if (!rc) rc = run_command(&f, via, c, &open_seen);
            if (!rc) rc = snapshot(&f, &actual);
            actual.open_seen = open_seen;
            bool ok = !rc && equal(want, actual);
            if (!ok) {
                fprintf(stderr, "FAIL %s x %s %s rc=%d\n", mode_names[modes[i]], via == VIA_MENU ? "menu" : "palette", c->label, rc);
                describe("want", want); describe(" got", actual); (*failed)++;
            } else (*passed)++;
            if (fixture_close(&f)) { (*failed)++; ok = false; }
            printf(" %s |", ok ? "PASS" : "FAIL");
        }
        putchar('\n');
    }
    puts("\n| Find focus | click then type x |\n|---|---|");
    for (size_t i = 0; i < N(modes); i++) {
        fixture f = {0}; state before = {0}, actual = {0};
        int rc = fixture_open(&f, modes[i], TYPE);
        if (!rc) rc = snapshot(&f, &before);
        if (!rc) rc = precondition(modes[i], before);
        state want = before;
        if (!rc) { want = expected(&f, modes[i], CLICK, want); want = expected(&f, FIND_DOCUMENT, TYPE, want); }
        if (!rc) rc = click_text(f.e);
        if (!rc) rc = send_event(f.e, key('x', 0, "x"));
        if (!rc) rc = snapshot(&f, &actual);
        bool ok = !rc && equal(want, actual);
        if (!ok) { describe("want click/type", want); describe("got click/type", actual); (*failed)++; }
        else (*passed)++;
        if (fixture_close(&f)) { (*failed)++; ok = false; }
        printf("| %s | %s |\n", mode_names[modes[i]], ok ? "PASS" : "FAIL");
    }
    return 0;
}
static int lifecycle_witnesses(size_t *passed, size_t *xfailed, size_t *failed)
{
    puts("\n| Running Find Results | Outcome |\n|---|---|");
    const action actions[] = {CLOSE, ESCAPE};
    for (size_t i = 0; i < N(actions); i++) {
        fixture f = {0}; int rc = fixture_open(&f, RESULTS, actions[i]);
        /* (P) Two mailbox capacities of one-hit lines exceed the mailbox
         * and its single outbox, as in n95_find_in_files. Backpressure keeps
         * the worker alive without a sleep, duration or large fixture. */
        char dense[2u * WORK_MAILBOX_CAP * (sizeof "x\n" - 1u)];
        for (size_t at = 0; at < sizeof dense; at += sizeof "x\n" - 1u)
            memcpy(dense + at, "x\n", sizeof "x\n" - 1u);
        if (!rc) rc = tree_fixture_file(&f.tree, "dense", dense, sizeof dense);
        if (!rc) rc = find_setup(f.e, CS, 'f');
        if (!rc) rc = send_event(f.e, key('x', 0, "x"));
        if (!rc) rc = chord(f.e, XKB_KEY_Tab, 0);
        char where[PATH_MAX + sizeof ", dense"];
        if (!rc) {
            int n = snprintf(where, sizeof where, "%s, dense", f.tree.root);
            if (n < 0 || (size_t)n >= sizeof where) rc = 1;
        }
        if (!rc) rc = type_string(f.e, where);
        uint64_t full = rc ? 0 : atomic_load(&f.e->pool.dropped_full);
        /* Existing private dispatcher seam (as in n95): do not drain DONE
         * between starting the job and the tested command. Ordinary matrix
         * cells and command equivalence witnesses use editor_inject. */
        if (!rc) rc = editor_fif_start(f.e);
        if (!rc) {
            uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
            while (atomic_load(&f.e->pool.dropped_full) == full && trace_now_ns() < deadline) sched_yield();
            if (atomic_load(&f.e->pool.dropped_full) == full || !f.e->fif.running || !editor_fif_results_active(f.e)) rc = 1;
        }
        bool running = true;
        if (!rc) {
            plat_event ev = key(actions[i] == CLOSE ? (uint32_t)'w' : XKB_KEY_Escape,
                actions[i] == CLOSE ? PLAT_MOD_CTRL : 0, NULL);
            rc = editor_handle_key(f.e, &ev);
            running = f.e->fif.running;
        }
        const expected_failure *xf = find_xfail(MODE_COUNT, actions[i]);
        bool ok = !rc && (xf ? running : !running);
        if (!ok) {
            fprintf(stderr, "FAIL running results x %s rc=%d running=%d (want cancelled)\n", action_names[actions[i]], rc, running);
            (*failed)++;
        } else if (xf) (*xfailed)++;
        else (*passed)++;
        if (!rc) {
            if (actions[i] == CLOSE) {
                if (tabs_count(&f.e->tabs) != (size_t)DOC_C || f.e->buffer->id != f.ids[DOC_B]) rc = 1;
            } else if (!editor_fif_results_active(f.e) || f.e->find_visible) rc = 1;
            if (!rc) rc = settle(f.e);
            if (!rc && actions[i] == ESCAPE && !f.e->fif.done.cancelled) rc = 1;
        }
        if (fixture_close(&f)) rc = 1;
        if (rc) { (*failed)++; ok = false; }
        printf("| %s | %s |\n", action_names[actions[i]], ok ? (xf ? "XFAIL" : "PASS") : "FAIL");
    }
    return 0;
}

int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(N(mode_names) == MODE_COUNT && N(action_names) == ACTION_COUNT);
    size_t passed = 0, xfailed = 0, failed = 0;
    puts("| Mode / Action | Ctrl+W | Ctrl+Tab | Ctrl+Shift+Tab | Ctrl+PgUp | Ctrl+PgDn | Ctrl+N | Ctrl+O then cancel | Ctrl+S | Ctrl+Shift+T | Ctrl+G | Ctrl+F | Ctrl+Shift+P | Alt | Escape | click text | middle paste | type x | Ctrl+Z | resize | drop |");
    puts("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");
    for (mode m = NONE; m < MODE_COUNT; m++) {
        printf("| %s |", mode_names[m]); fflush(stdout);
        for (action a = CLOSE; a < ACTION_COUNT; a++) {
            fixture f = {0}; state before = {0}, actual = {0};
            int rc = fixture_open(&f, m, a);
            if (!rc) rc = snapshot(&f, &before);
            if (!rc) rc = precondition(m, before);
            state want = rc ? before : expected(&f, m, a, before);
            bool open_seen = false;
            if (!rc) rc = perform(&f, a, &open_seen);
            if (!rc) rc = snapshot(&f, &actual);
            actual.open_seen = open_seen;
            const expected_failure *xf = find_xfail(m, a);
            bool ideal = !rc && equal(want, actual);
            bool ok = !rc && (xf ? !ideal && equal(observed_state(&f, xf, before, want), actual) : ideal);
            if (!ok) {
                fprintf(stderr, "FAIL %s x %s rc=%d\n", mode_names[m], action_names[a], rc);
                describe("want", want); describe(" got", actual);
                if (xf) { fprintf(stderr, "%s: %s\n", ideal ? "XPASS" : "XFAIL changed", xf->reason); describe("pinned", observed_state(&f, xf, before, want)); }
                failed++;
            } else if (xf) xfailed++;
            else passed++;
            if (fixture_close(&f)) { failed++; ok = false; }
            printf(" %s |", ok ? (xf ? "XFAIL" : "PASS") : "FAIL"); fflush(stdout);
        }
        putchar('\n');
    }
    T(command_witnesses(&passed, &failed) == 0);
    T(lifecycle_witnesses(&passed, &xfailed, &failed) == 0);
    puts("\nXFAIL table (strict observed-state pins; XPASS fails):\n\n| Mode | Action | Reason |\n|---|---|---|");
    for (size_t i = 0; i < N(xfails); i++) printf("| %s | %s | %s |\n",
        xfails[i].m == MODE_COUNT ? "running results" : mode_names[xfails[i].m], action_names[xfails[i].a], xfails[i].reason);
    printf("modal_matrix_test: %zu PASS, %zu XFAIL, %zu FAIL (P: enum cross product plus command/click/lifecycle witnesses)\n", passed, xfailed, failed);
    fflush(stdout);
    return failed ? 1 : 0;
}
