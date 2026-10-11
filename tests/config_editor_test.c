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
} probe;
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
    T(font_preferences_test(path) == 0);
    T(unlink(path) == 0 && rmdir(root) == 0);
    puts("config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed");
    return 0;
}
