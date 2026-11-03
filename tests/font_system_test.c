/* edit-b6ai.3: follow fontconfig's `monospace` while the Omarchy palette is
 * active. A fake fonts.conf (FONTCONFIG_FILE) maps monospace to faces that are
 * installed under ~/.local/share/fonts (Go Mono, DejaVu Sans Mono), the way
 * `omarchy font set` prepends a strong family. Rewrites are atomic renames,
 * the way omarchy-font-set / editors replace files. Counts below are
 * identities (one rewrite = one selection), never timing budgets. */
#include "editor/private.h"
#include "base/base.h"
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "font_system_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb"); T(f);
    T(fputs(text, f) >= 0); T(fclose(f) == 0); return 0;
}
/* fonts.conf as a user file: the system configuration plus one strong
 * prepend rule for monospace, as omarchy-font-set writes it. */
static int fonts_conf(char *out, size_t cap, const char *family)
{
    int n = snprintf(out, cap,
        "<?xml version=\"1.0\"?><!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n<fontconfig>\n"
        "<include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
        "<match target=\"pattern\"><test name=\"family\" qual=\"any\"><string>monospace</string></test>\n"
        "<edit name=\"family\" mode=\"prepend_first\" binding=\"strong\"><string>%s</string></edit></match>\n"
        "</fontconfig>\n", family);
    T(n > 0 && (size_t)n < cap); return 0;
}
/* Atomic replacement: write a sibling, rename over. */
static int replace_file(const char *path, const char *text)
{
    char tmp[CONFIG_TEXT_CAP]; T(snprintf(tmp, sizeof tmp, "%s.tmp", path) > 0);
    T(write_file(tmp, text) == 0); T(rename(tmp, path) == 0); return 0;
}
static int set_family(const char *path, const char *family)
{
    char text[2048]; T(fonts_conf(text, sizeof text, family) == 0);
    return replace_file(path, text);
}

/* ---- config level ---- */
static int tick_until_fonts(config_runtime *r, uint64_t above)
{
    while (config_runtime_fonts_generation(r) <= above) {
        (void)config_runtime_tick(r); usleep(1000);
    }
    return 0;
}
static int readable(int fd)
{
    struct pollfd p = {fd, POLLIN, 0}; return poll(&p, 1, 0) > 0;
}
static int config_level(const char *root, const char *colors)
{
    char path[CONFIG_TEXT_CAP];
    T(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    T(config_fontconf_path(path, sizeof path) == CONFIG_OK);
    char want[CONFIG_TEXT_CAP]; T(snprintf(want, sizeof want, "%s/fontconfig/fonts.conf", root) > 0);
    T(!strcmp(path, want) && config_fontconf_path(path, 4) == CONFIG_ERR_LIMIT);
    T(setenv("XDG_CONFIG_HOME", "relative", 1) == 0 && setenv("HOME", root, 1) == 0);
    T(config_fontconf_path(path, sizeof path) == CONFIG_OK);
    T(snprintf(want, sizeof want, "%s/.config/fontconfig/fonts.conf", root) > 0 && !strcmp(path, want));

    char cfg[CONFIG_TEXT_CAP], dir[CONFIG_TEXT_CAP], conf[CONFIG_TEXT_CAP];
    T(snprintf(cfg, sizeof cfg, "%s/config.toml", root) > 0);
    T(snprintf(dir, sizeof dir, "%s/fontconfig", root) > 0);
    T(snprintf(conf, sizeof conf, "%s/fontconfig/fonts.conf", root) > 0);
    T(write_file(cfg, "") == 0);
    T(write_file(colors, "background = '#1a1b26'\nforeground = '#a9b1d6'\n") == 0);
    config_runtime *r = NULL;
    /* The fontconfig directory does not exist yet (this box before the first
     * `omarchy font set`): the watch sits on its parent until it appears. */
    T(config_runtime_open_follow(&r, cfg, colors, conf) == CONFIG_OK);
    while (config_runtime_busy(r)) { (void)config_runtime_tick(r); usleep(1000); }
    for (int i = 0; i < 4; i++) (void)config_runtime_tick(r);
    T(config_runtime_system_font(r));
    uint64_t seen = config_runtime_fonts_generation(r), generation = config_runtime_generation(r);
    int fd = config_runtime_watchfd(r); T(fd >= 0);
    T(!readable(fd)); /* idle: nothing to read, no timer */
    T(mkdir(dir, 0700) == 0);
    T(set_family(conf, "Go Mono") == 0);
    T(tick_until_fonts(r, seen) == 0);
    seen = config_runtime_fonts_generation(r);
    for (int i = 0; i < 8; i++) { (void)config_runtime_tick(r); usleep(1000); }
    seen = config_runtime_fonts_generation(r);
    T(set_family(conf, "DejaVu Sans Mono") == 0); /* rename over the file */
    T(tick_until_fonts(r, seen) == 0);
    for (int i = 0; i < 8; i++) { (void)config_runtime_tick(r); usleep(1000); }
    T(config_runtime_generation(r) == generation); /* fonts do not bump settings */
    T(!readable(fd));
    seen = config_runtime_fonts_generation(r);
    for (int i = 0; i < 16; i++) { (void)config_runtime_tick(r); usleep(1000); }
    T(config_runtime_fonts_generation(r) == seen);
    /* pinned theme: no system font */
    T(write_file(cfg, "theme = 'dark'\n") == 0);
    while (config_runtime_system_font(r)) { (void)config_runtime_tick(r); usleep(1000); }
    config_runtime_close(r);
    /* no fonts path: never follows; no palette: never follows */
    T(write_file(cfg, "") == 0);
    T(config_runtime_open_follow(&r, cfg, colors, NULL) == CONFIG_OK);
    while (config_runtime_busy(r)) { (void)config_runtime_tick(r); usleep(1000); }
    T(!config_runtime_system_font(r)); config_runtime_close(r);
    char absent[CONFIG_TEXT_CAP]; T(snprintf(absent, sizeof absent, "%s/absent/colors.toml", root) > 0);
    T(config_runtime_open_follow(&r, cfg, absent, conf) == CONFIG_OK);
    while (config_runtime_busy(r)) { (void)config_runtime_tick(r); usleep(1000); }
    for (int i = 0; i < 4; i++) (void)config_runtime_tick(r);
    T(!config_runtime_system_font(r)); config_runtime_close(r);
    T(unlink(conf) == 0 && rmdir(dir) == 0 && unlink(cfg) == 0);
    puts("font_system_test: fontconfig path, watch on a not-yet-existing directory, atomic rewrite notices, idle fd silent, pin/no-palette/no-path do not follow");
    return 0;
}

/* ---- editor level ---- */
typedef struct guard { bool active; size_t allocations, submits; } guard;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    (void)sequence; (void)ns; guard *g = ctx;
    if (!g->active) { edit_malloc_guard_begin(); g->active = true; }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    (void)frame; guard *g = ctx;
    if (g->active) { g->allocations += edit_malloc_guard_end(); g->active = false; }
    g->submits++;
}
static int settle(editor *e)
{
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active) T(editor_step(e, 0) >= 0);
    return 0;
}
static int adopted(editor *e)
{
    while (editor_get_stats(e).font_pending) { T(editor_step(e, 0) >= 0); sched_yield(); }
    return settle(e);
}
static int type(editor *e, guard *g)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'a', .utf8_len = 1, .utf8 = {'a'}};
    size_t submits = g->submits;
    T(editor_inject(e, &ev) == 0); T(settle(e) == 0);
    T(g->submits > submits && !g->active && g->allocations == 0);
    return 0;
}
static const char *current_file(const editor *e) { return e->font.atlas[e->font.current].selection.file; }
static int wait_changes(editor *e, uint64_t above)
{
    while (e->stats.font_changes <= above) { T(editor_step(e, 0) >= 0); sched_yield(); }
    return adopted(e);
}
/* Step until nothing more arrives for a while: counts events, not time. */
static int quiet(editor *e)
{
    for (int i = 0; i < 200; i++) { T(editor_step(e, 0) >= 0); usleep(500); }
    return adopted(e);
}
typedef struct fixture {
    char root[CONFIG_TEXT_CAP], cfg[CONFIG_TEXT_CAP], colors[CONFIG_TEXT_CAP], conf[CONFIG_TEXT_CAP], dir[CONFIG_TEXT_CAP];
} fixture;
static int open_editor(editor **e, guard *g, const fixture *f, const char *colors, const char *fontconf,
                       const char *family, const char *font_file)
{
    static render_backend backend; /* outlives the editor opened on it; one at a time */
    T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 32, .rows = 8, .max_cols = 64, .max_rows = 16,
        .preferences_path = f->cfg, .palette_path = colors, .fontconf_path = fontconf,
        .font_family = family, .font_file = font_file,
        .hook_ctx = g, .on_ingress = ingress, .on_submit = submitted};
    T(editor_open(e, &cfg, &backend) == 0); return settle(*e);
}
static int editor_level(fixture *f)
{
    guard g = {0}; editor *e = NULL;
    T(setenv("FONTCONFIG_FILE", f->conf, 1) == 0);
    T(unsetenv("EDIT_FONT_FAMILY") == 0 && unsetenv("EDIT_FONT_FILE") == 0);
    T(set_family(f->conf, "Go Mono") == 0);
    T(write_file(f->cfg, "") == 0);

    /* 1. Following: the default family is fontconfig's monospace. */
    T(open_editor(&e, &g, f, f->colors, f->conf, NULL, NULL) == 0);
    T(adopted(e) == 0);
    T(e->stats.font_error == FONT_OK && e->stats.font_changes == 1u);
    T(!strcmp(e->font.selection.family, "monospace"));
    T(strstr(current_file(e), "Go-Mono") != NULL);
    T(type(e, &g) == 0); /* typing path unchanged: mallocs = 0 */
    /* 2. Live: omarchy-font-set rewrites fonts.conf (atomic rename). */
    uint64_t changes = e->stats.font_changes;
    T(set_family(f->conf, "DejaVu Sans Mono") == 0);
    T(wait_changes(e, changes) == 0);
    T(strstr(current_file(e), "DejaVuSansMono") != NULL && e->stats.font_error == FONT_OK);
    T(type(e, &g) == 0);
    /* Idle after the switch: no further selections and a silent descriptor. */
    changes = e->stats.font_changes;
    T(quiet(e) == 0 && e->stats.font_changes == changes);
    int fd = config_runtime_watchfd(e->preferences_runtime);
    T(fd >= 0 && !readable(fd));
    /* 3. An explicit config family wins and ignores fonts.conf rewrites. */
    T(write_file(f->cfg, "[font]\nfamily = 'Go Mono'\n") == 0);
    T(wait_changes(e, changes) == 0);
    T(strstr(current_file(e), "Go-Mono") != NULL);
    changes = e->stats.font_changes;
    T(set_family(f->conf, "Liberation Mono") == 0);
    T(quiet(e) == 0 && e->stats.font_changes == changes && strstr(current_file(e), "Go-Mono") != NULL);
    /* Removing the explicit family returns to following. */
    T(write_file(f->cfg, "") == 0);
    T(wait_changes(e, changes) == 0 && strstr(current_file(e), "LiberationMono-Regular") != NULL);
    /* theme = "dark" pins the palette: no system font, embedded face. */
    changes = e->stats.font_changes;
    T(write_file(f->cfg, "theme = 'dark'\n") == 0);
    T(wait_changes(e, changes) == 0 && !e->font.selection.family[0] && !current_file(e)[0]);
    changes = e->stats.font_changes;
    T(set_family(f->conf, "Go Mono") == 0);
    T(quiet(e) == 0 && e->stats.font_changes == changes);
    T(type(e, &g) == 0);
    editor_close(e);

    /* 4. Explicit sources win at startup: config file, env, CLI. */
    T(write_file(f->cfg, "[font]\nfile = 'vendor/DejaVuSansMono.ttf#0'\n") == 0);
    T(open_editor(&e, &g, f, f->colors, f->conf, NULL, NULL) == 0 && adopted(e) == 0);
    T(!e->font.selection.family[0] && strstr(current_file(e), "vendor/DejaVuSansMono.ttf") != NULL);
    editor_close(e);
    T(write_file(f->cfg, "") == 0);
    T(setenv("EDIT_FONT_FAMILY", "Go Mono", 1) == 0);
    T(set_family(f->conf, "DejaVu Sans Mono") == 0);
    T(open_editor(&e, &g, f, f->colors, f->conf, NULL, NULL) == 0 && adopted(e) == 0);
    T(!strcmp(e->font.selection.family, "Go Mono") && strstr(current_file(e), "Go-Mono") != NULL);
    changes = e->stats.font_changes;
    T(set_family(f->conf, "Liberation Mono") == 0);
    T(quiet(e) == 0 && e->stats.font_changes == changes);
    editor_close(e);
    T(unsetenv("EDIT_FONT_FAMILY") == 0);
    T(open_editor(&e, &g, f, f->colors, f->conf, "Go Mono", NULL) == 0 && adopted(e) == 0);
    T(!strcmp(e->font.selection.family, "Go Mono")); editor_close(e);
    /* An explicitly empty CLI pair selects the embedded face, even following. */
    T(open_editor(&e, &g, f, f->colors, f->conf, "", "") == 0 && adopted(e) == 0);
    T(e->stats.font_changes == 0 && !e->font.selection.family[0]);
    changes = e->stats.font_changes;
    T(set_family(f->conf, "DejaVu Sans Mono") == 0);
    T(quiet(e) == 0 && e->stats.font_changes == changes);
    editor_close(e);

    /* 5. Outside Omarchy (no colors.toml) the default stays the embedded face. */
    char absent[CONFIG_TEXT_CAP]; T(snprintf(absent, sizeof absent, "%s/absent/colors.toml", f->root) > 0);
    T(open_editor(&e, &g, f, absent, f->conf, NULL, NULL) == 0 && adopted(e) == 0);
    T(e->stats.font_changes == 0 && !e->font.selection.family[0] && !current_file(e)[0]);
    T(set_family(f->conf, "Go Mono") == 0);
    T(quiet(e) == 0 && e->stats.font_changes == 0);
    T(type(e, &g) == 0); editor_close(e);
    /* ... and without a fonts path (the test default) even with a palette. */
    T(open_editor(&e, &g, f, f->colors, NULL, NULL, NULL) == 0 && adopted(e) == 0);
    T(e->stats.font_changes == 0 && !e->font.selection.family[0]);
    editor_close(e);

    /* 6. A monospace that fontconfig cannot resolve to our cell grid falls
     * back to the embedded face and logs once; the editor keeps working. */
    T(set_family(f->conf, "Noto Sans") == 0); /* proportional: rejected by the cell grid */
    T(open_editor(&e, &g, f, f->colors, f->conf, NULL, NULL) == 0 && adopted(e) == 0);
    T(e->stats.font_error != FONT_OK && type(e, &g) == 0);
    editor_close(e);
    puts("font_system_test: follows monospace, live atomic rewrite re-selects, config/env/CLI win, pin and no-Omarchy keep the embedded face, typing mallocs=0");
    return 0;
}
int main(void)
{
    fixture f; strcpy(f.root, "/tmp/sublimite-font-system-XXXXXX");
    T(mkdtemp(f.root));
    /* Omarchy's layout: colors.toml in current/theme/. The palette watches sit
     * on the private current/ directory; a colors.toml directly in a shared
     * directory would also watch that directory's neighbours (theme.name is
     * <dir>.name) and a busy /tmp would wake the idle descriptor. */
    char state[CONFIG_TEXT_CAP], theme[CONFIG_TEXT_CAP];
    T(snprintf(state, sizeof state, "%s/current", f.root) > 0 && snprintf(theme, sizeof theme, "%s/theme", state) > 0);
    T(mkdir(state, 0700) == 0 && mkdir(theme, 0700) == 0);
    T(snprintf(f.cfg, sizeof f.cfg, "%s/config.toml", f.root) > 0);
    T(snprintf(f.colors, sizeof f.colors, "%s/colors.toml", theme) > 0);
    T(snprintf(f.dir, sizeof f.dir, "%s/fontconfig", f.root) > 0);
    T(snprintf(f.conf, sizeof f.conf, "%s/fontconfig/fonts.conf", f.root) > 0);
    T(write_file(f.colors, "background = '#1a1b26'\nforeground = '#a9b1d6'\n") == 0);
    if (config_level(f.root, f.colors)) return 1;
    T(mkdir(f.dir, 0700) == 0);
    if (editor_level(&f)) return 1;
    T(unlink(f.conf) == 0 && rmdir(f.dir) == 0 && unlink(f.cfg) == 0 && unlink(f.colors) == 0);
    T(rmdir(theme) == 0 && rmdir(state) == 0 && rmdir(f.root) == 0);
    return 0;
}
