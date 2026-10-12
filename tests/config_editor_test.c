#include "editor/private.h"
#include "base/base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>

#define T(x) do { if (!(x)) { fprintf(stderr, "config_editor_test: FAIL %s\n", #x); return 1; } } while (0)
typedef struct probe {
    editor *owner;
    uint64_t expected_generation, seen_generation;
    uint32_t expected_frame;
    bool failed;
    bool check_scroll, expected_wrap;
    uint64_t expected_byte, expected_line, expected_geometry;
    uint32_t expected_tab;
    size_t line_bytes;
} probe;
/* (P) Zero denotes the first row/column or absence; one advances a byte,
 * row, generation or frame. These are identities, not timing budgets. */
static int check_scroll_frame(const probe *p, const editor_frame *frame)
{
    editor *e = p->owner; const editor_buffer *b = e->buffer;
    view_state state = editor_view(e);
    T(frame->geometry_generation == p->expected_geometry);
    T(frame->wrapped == p->expected_wrap && state.wrap == p->expected_wrap);
    T(state.first_byte == p->expected_byte && state.first_line == p->expected_line);
    T(state.visual_byte == p->expected_byte && frame->scroll.first_byte == p->expected_byte);
    T(e->lay.first_byte == p->expected_byte && e->row_byte[0] == p->expected_byte);
    T(e->v.config.tab_width == p->expected_tab && e->lay.tab == p->expected_tab);
    T(!state.approximate && !e->lay.approximate && !e->lay.line_count_estimated);
    T(b->lg.lines_exact && !b->lg.top_estimated);
    T(e->lay.cfg.gutter_fg == e->layout_cfg.gutter_fg);
    size_t first_cell = (size_t)e->tab_rows * e->grid.dims.cols +
        layout_gutter_width(&e->lay) + p->expected_tab;
    /* (P) After the leading tab, this line's repeated ASCII identity begins. */
    T(e->grid.cells[first_cell].glyph_index == (uint32_t)'a' + p->expected_line);
    if (p->expected_wrap) {
        T(frame->wrap_seed.start == p->expected_byte && frame->wrap_seed.line == p->expected_line);
        T(!frame->wrap_seed.approximate);
    } else {
        T(b->scroll.first_byte == p->expected_byte && b->scroll_display.first_byte == p->expected_byte);
        T(b->scroll.first_line == p->expected_line && b->scroll_display.first_line == p->expected_line);
        T(!b->scroll.approximate && !b->scroll_display.approximate && !b->scroll_pending);
        T(b->scroll.config.rows == e->v.config.rows);
        T(frame->scroll.first_row == p->expected_line);
        for (uint32_t row = 0; row < e->v.config.rows; row++) {
            T(e->row_byte[row] == p->expected_byte + row * p->line_bytes);
            size_t cell = (size_t)(e->tab_rows + row) * e->grid.dims.cols +
                layout_gutter_width(&e->lay) + p->expected_tab;
            /* (P) Fixture line identity uses successive contiguous ASCII letters. */
            T(e->grid.cells[cell].glyph_index == (uint32_t)'a' + p->expected_line + row);
        }
    }
    return 0;
}
static void io(void *ctx, bool entering)
{
    probe *p = ctx;
    if (entering || !p->owner || !p->owner->preferences_runtime) return;
    editor *e = p->owner;
    uint64_t generation = config_runtime_generation(e->preferences_runtime);
    if (generation > e->preferences_generation && generation != p->expected_generation) {
        p->expected_generation = generation;
        /* (G) bead's one frame generation: adoption belongs to the already
         * begun frame, or exactly its successor when the editor is idle. */
        p->expected_frame = e->grid.frame_id + (e->dirty ? 0u : 1u);
    }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    probe *p = ctx; uint64_t generation = editor_preferences_generation(p->owner);
    if (generation != p->seen_generation) {
        if (generation != p->expected_generation || frame->id != p->expected_frame) p->failed = true;
        if (p->check_scroll && check_scroll_frame(p, frame)) p->failed = true;
        p->seen_generation = generation;
    }
}
static int settle(editor *e)
{
    do {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        sched_yield();
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int wait_theme(editor *e, config_theme theme, uint64_t after)
{
    while (editor_preferences_generation(e) <= after || editor_preferences(e)->theme != theme) {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE); sched_yield();
    }
    return settle(e);
}
static int write_config(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    T(write(fd, text, strlen(text)) == (ssize_t)strlen(text)); T(close(fd) == 0); return 0;
}
static int font_preferences_test(const char *path)
{
    T(write_config(path, "[font]\nsize=12.5\n") == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    editor_config cfg = {.cols = 24, .rows = 3, .max_cols = 24, .max_rows = 3,
        .font_dpi = 96.0, .preferences_path = path};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK);
    T(e->cfg.font_points == 12.5 && e->font.default_points == 12.5);
    T(e->font.applied_px == editor_runtime_em_px(cfg.font_dpi, 12.5));
    T(settle(e) == 0);
    uint64_t before = editor_preferences_generation(e);
    T(write_config(path, "[font]\nsize=14\n") == 0);
    T(wait_theme(e, CONFIG_DARK, before) == 0);
    T(e->cfg.font_points == 14.0 && e->font.default_points == 14.0);
    T(e->font.applied_px == editor_runtime_em_px(cfg.font_dpi, 14.0));
    T(editor_font_zoom(e, 1) == 0); T(settle(e) == 0);
    T(editor_font_zoom(e, 0) == 0); T(settle(e) == 0);
    T(e->font.points == 14.0);
    before = editor_preferences_generation(e);
    T(unlink(path) == 0); T(wait_theme(e, CONFIG_DARK, before) == 0);
    T(e->cfg.font_points == 0.0 && e->font.default_points == EDITOR_FONT_DEFAULT_PT);
    T(e->font.applied_px == editor_runtime_em_px(cfg.font_dpi, EDITOR_FONT_DEFAULT_PT));
    editor_close(e);

    /* Native main passes EDIT_FONT_SIZE_PT as explicit startup font_points. */
    cfg.font_points = 11.0;
    T(write_config(path, "[font]\nsize=12.5\n") == 0);
    T(render_null_backend(&backend) == RENDER_OK);
    T(editor_open(&e, &cfg, &backend) == EDITOR_OK); T(settle(e) == 0);
    T(editor_preferences(e)->font_points == 12.5 && e->cfg.font_points == 11.0);
    T(e->font.default_points == 11.0);
    before = editor_preferences_generation(e);
    T(write_config(path, "[font]\nsize=14\n") == 0);
    T(wait_theme(e, CONFIG_DARK, before) == 0);
    T(editor_preferences(e)->font_points == 14.0 && e->cfg.font_points == 11.0);
    T(e->font.default_points == 11.0);
    T(e->font.applied_px == editor_runtime_em_px(cfg.font_dpi, 11.0));
    editor_close(e);
    puts("config_editor_test: DPI startup, size reload/reset/deletion and explicit environment precedence passed");
    return 0;
}
static int reload_scroll(editor *e, probe *p, const char *path, bool wrap, uint32_t tab)
{
    uint64_t before = editor_preferences_generation(e);
    p->expected_byte = editor_view(e).first_byte;
    p->expected_line = editor_view(e).first_line;
    p->expected_geometry = e->geometry_generation + 1u; /* (P) successor geometry identity */
    p->expected_wrap = wrap; p->expected_tab = tab; p->check_scroll = true;
    char text[CONFIG_TEXT_CAP];
    T(snprintf(text, sizeof text, "wrap=%s\ntabs=%u\n", wrap ? "true" : "false", tab) > 0);
    T(write_config(path, text) == 0 && wait_theme(e, CONFIG_DARK, before) == 0);
    T(p->seen_generation == editor_preferences_generation(e) && !p->failed);
    T(check_scroll_frame(p, &e->active_frame) == 0);
    p->check_scroll = false;
    return 0;
}
static int scroll_preferences_test(const char *root, const char *path)
{
    /* (P) Same geometry as main: minimap + tab slot, chrome + text rows.
     * A tab followed by a surface-width run must wrap in the narrower text
     * area. (E) Two surfaces plus a wheel notch of logical lines keep both
     * the scroll target and the following viewport away from EOF. */
    enum { cols = 8u + 16u, rows = 1u + 2u,
        lines = rows + SCROLL_CORE_ROWS + rows,
        line_bytes = cols + sizeof("\t\n") - 1u };
    /* (P) One terminating NUL lets the existing file writer use strlen. */
    char bytes[lines * line_bytes + 1u], file_path[CONFIG_TEXT_CAP];
    T(lines <= 'z' - 'a' + 1);
    for (size_t line = 0; line < lines; line++) {
        char *start = bytes + line * line_bytes;
        start[0] = '\t'; memset(start + 1u, (int)((unsigned char)'a' + line), cols);
        start[line_bytes - 1u] = '\n';
    }
    bytes[sizeof bytes - 1u] = '\0';
    T(snprintf(file_path, sizeof file_path, "%s/scroll.txt", root) > 0);
    T(write_config(file_path, bytes) == 0);
    /* (P) Tab widths reuse main's explicit narrow/wide config cases. */
    T(write_config(path, "wrap=true\ntabs=2\n") == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    probe p = {.line_bytes = line_bytes};
    editor_config cfg = {.path = file_path, .cols = cols, .rows = rows,
        .max_cols = cols, .max_rows = rows, .preferences_path = path,
        .hook_ctx = &p, .on_io = io, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); p.owner = e;
    T(wait_theme(e, CONFIG_DARK, 0) == 0);
    T(editor_view(e).wrap && e->lay.wrap_rows[0].next < line_bytes);
    /* (P) Ceil(wheel units / rows per notch) advances a visual row without
     * skipping every continuation when a line wraps to a full notch. */
    plat_event wheel = {.kind = PLAT_EV_WHEEL,
        .dy = (int32_t)((SCROLL_WHEEL_UNIT + SCROLL_CORE_ROWS - 1u) / SCROLL_CORE_ROWS)};
    /* (P) Source bytes bound possible visual-row starts; no time/retry budget. */
    for (size_t step = 0; step < sizeof bytes &&
        (!editor_view(e).first_byte || editor_view(e).visual_byte == editor_view(e).first_byte); step++) {
        T(editor_inject(e, &wheel) == EDITOR_OK && settle(e) == 0);
    }
    T(editor_view(e).first_byte > 0 && editor_view(e).visual_byte > editor_view(e).first_byte);
    T(e->buffer->scroll.first_byte != editor_view(e).first_byte);
    int result = reload_scroll(e, &p, path, false, e->lay.tab);
    /* (P) Both explicit tab-width values below come from main's config cases. */
    if (!result) result = reload_scroll(e, &p, path, false, 8u);
    if (!result) result = reload_scroll(e, &p, path, true, e->lay.tab);
    if (!result) result = reload_scroll(e, &p, path, true, 2u);
    if (!result) result = reload_scroll(e, &p, path, false, e->lay.tab);
    editor_close(e); T(unlink(file_path) == 0); T(result == 0);
    puts("config_editor_test: scrolled file wrap off/on and tab-width reloads resync the first submitted frame passed");
    return 0;
}
int main(void)
{
    char root[] = "/tmp/sublimite-config-editor-XXXXXX", path[CONFIG_TEXT_CAP]; T(mkdtemp(root));
    T(snprintf(path, sizeof path, "%s/config.toml", root) > 0);
    T(write_config(path, "theme='light'\nwrap=false\ntabs=8\n[font]\nsize=12.5\nfamily='Mono'\n[keymap]\nselect_all='Ctrl+J'\n") == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    probe p = {0};
    /* (P) fixture geometry: one minimap (eight columns) plus one tab slot
     * (sixteen columns); one chrome row plus two text rows. */
    editor_config cfg = {.initial = (const uint8_t *)"hello\n", .initial_len = sizeof("hello\n") - 1u,
        .cols = 8u + 16u, .rows = 1u + 2u, .max_cols = 8u + 16u, .max_rows = 1u + 2u,
        .preferences_path = path, .hook_ctx = &p, .on_io = io, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); p.owner = e;
    T(wait_theme(e, CONFIG_LIGHT, 0) == 0);
    T(!p.failed && p.seen_generation == editor_preferences_generation(e));
    T(!editor_view(e).wrap && e->v.config.tab_width == 8u && e->lay.tab == 8u);
    T(editor_preferences(e)->font_points == 12.5 && !strcmp(editor_preferences(e)->font_family, "Mono"));
    T(editor_grid(e)->cells[e->grid.dims.cols].bg == config_theme_color(CONFIG_LIGHT, "editor.background"));
    uint64_t before = editor_preferences_generation(e);
    T(write_config(path, "theme='dark'\nwrap=true\n[tabs]\nwidth=2\nspaces=true\n[keymap]\nselect_all='Ctrl+J'\n") == 0);
    T(wait_theme(e, CONFIG_DARK, before) == 0);
    T(!p.failed && p.seen_generation == editor_preferences_generation(e));
    T(editor_view(e).wrap && e->v.config.tab_width == 2u && e->lay.tab == 2u && !e->buffer->style.uses_tabs);
    T(editor_preferences(e)->font_points == 0); /* removed font setting returns consumer default */
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'j', .mods = PLAT_MOD_CTRL};
    edit_malloc_guard_begin(); T(editor_inject(e, &key) == EDITOR_OK); T(settle(e) == 0);
    size_t allocations = edit_malloc_guard_end(); T(!edit_malloc_guard_active() || !allocations);
    view_state selected = editor_view(e); T(selected.selection.cursor == editor_length(e) && selected.selection.anchor == 0);
    uint64_t tab; T(editor_add_buffer(e, NULL, (const uint8_t *)"other\n", sizeof("other\n") - 1u, &tab) == 0);
    T(settle(e) == 0); T(editor_view(e).wrap && e->v.config.tab_width == 2u);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(editor_view(e).wrap && e->v.config.tab_width == 2u);
    before = editor_preferences_generation(e);
    T(write_config(path, "theme='light'\n") == 0); T(wait_theme(e, CONFIG_LIGHT, before) == 0);
    T(e->v.config.tab_width == e->buffer->detected_style.width && e->v.config.tab_width == 4u);
    T(!p.failed); editor_close(e);
    T(scroll_preferences_test(root, path) == 0);
    T(font_preferences_test(path) == 0);
    T(unlink(path) == 0 && rmdir(root) == 0);
    puts("config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed");
    return 0;
}
