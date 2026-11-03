/* edit-b6ai.2: Omarchy colors.toml -> sublimite theme tokens, and the live
 * switch. Fixtures inline two stock themes byte for byte (M: copied from
 * /usr/share/omarchy/themes/{tokyo-night,flexoki-light}/colors.toml on the
 * monolith box, 2026-11-03). Ratios are WCAG 2.2 contrast (P): relative
 * luminance of linearised sRGB, (L1 + 0.05) / (L2 + 0.05). The live switch
 * replays omarchy-theme-set L266-296 in a temporary directory: stage
 * next-theme, rm -rf theme, mv next-theme theme, echo name > theme.name. */
#include "config/config.h"
#include "os/os.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define T(x) do { if (!(x)) { fprintf(stderr, "config_palette_test:%d: FAIL %s\n", __LINE__, #x); return 1; } } while (0)

static const char tokyo[] =
    "mode = \"dark\"\n\naccent = \"#7aa2f7\"\nselection = \"#292e42\"\nmuted = \"#414868\"\n\n"
    "background = \"#1a1b26\"\ndark_background = \"#13141c\"\ndarker_background = \"#0e0e14\"\n"
    "lighter_background = \"#24283b\"\n\nforeground = \"#a9b1d6\"\ndark_foreground = \"#565f89\"\n"
    "light_foreground = \"#b4bee6\"\nbright_foreground = \"#c0caf5\"\n\nred = \"#f7768e\"\n"
    "yellow = \"#e0af68\"\norange = \"#eb927b\"\ngreen = \"#9ece6a\"\ncyan = \"#449dab\"\n"
    "blue = \"#7aa2f7\"\nmagenta = \"#ad8ee6\"\nbrown = \"#75493d\"\n\nbright_red = \"#ff7a93\"\n"
    "bright_yellow = \"#ff9e64\"\nbright_green = \"#b9f27c\"\nbright_cyan = \"#0db9d7\"\n"
    "bright_blue = \"#7da6ff\"\nbright_magenta = \"#bb9af7\"\n";
static const char flexoki[] =
    "mode = \"light\"\n\naccent = \"#205EA6\"\nselection = \"#CECDC3\"\nmuted = \"#B7B5AC\"\n\n"
    "background = \"#FFFCF0\"\ndark_background = \"#f2efe4\"\ndarker_background = \"#e5e2d8\"\n"
    "lighter_background = \"#E6E4D9\"\n\nforeground = \"#100F0F\"\ndark_foreground = \"#878580\"\n"
    "light_foreground = \"#403E3C\"\nbright_foreground = \"#100F0F\"\n\nred = \"#D14D41\"\n"
    "yellow = \"#D0A215\"\norange = \"#d0772b\"\ngreen = \"#879A39\"\ncyan = \"#3AA99F\"\n"
    "blue = \"#205EA6\"\nmagenta = \"#CE5D97\"\nbrown = \"#683b15\"\n\nbright_red = \"#D14D41\"\n"
    "bright_yellow = \"#D0A215\"\nbright_green = \"#879A39\"\nbright_cyan = \"#3AA99F\"\n"
    "bright_blue = \"#4385BE\"\nbright_magenta = \"#CE5D97\"\n";

static double channel(uint32_t v)
{
    double c = (double)v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double luminance(uint32_t rgb)
{
    return 0.2126 * channel(rgb >> 16 & 0xffu) + 0.7152 * channel(rgb >> 8 & 0xffu) + 0.0722 * channel(rgb & 0xffu);
}
static double ratio(uint32_t a, uint32_t b)
{
    double x = luminance(a), y = luminance(b);
    return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}
/* Omarchy's mix (omarchy-theme-color mix_color): int(a*(1-t) + b*t + 0.5). */
static uint32_t mix(uint32_t a, uint32_t b, unsigned percent)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift <= 16u; shift += 8u) {
        double x = (double)(a >> shift & 0xffu), y = (double)(b >> shift & 0xffu), t = percent / 100.0;
        out |= (uint32_t)(x * (1.0 - t) + y * t + 0.5) << shift;
    }
    return out;
}
static uint32_t c(const config_palette *p, const char *name) { return config_palette_color(p, name); }
static double builtin_ratio(config_theme t, const char *token, const char *against)
{
    return ratio(config_theme_color(t, token), config_theme_color(t, against));
}
/* The ladder rule: the first whole percent toward foreground at which the
 * token reaches the built-in theme's ratio for the same token. */
static int derived(const config_palette *p, config_theme mode, const char *token, uint32_t source,
                   const char *against)
{
    uint32_t fg = c(p, "editor.foreground"), bg = c(p, against);
    double target = builtin_ratio(mode, token, against);
    unsigned t = 0;
    while (t < 100u && ratio(mix(source, fg, t), bg) < target) t++;
    T(c(p, token) == mix(source, fg, t));
    T(t == 100u || ratio(c(p, token), bg) >= target);
    return 0;
}
static int same_palette(const config_palette *a, const config_palette *b)
{
    T(a->mode == b->mode);
    for (size_t i = 0; i < CONFIG_THEME_TOKENS; i++) T(a->rgb[i] == b->rgb[i]);
    return 0;
}
static int is_builtin(const config_palette *p, config_theme t)
{
    config_palette b; config_palette_builtin(t, &b); return same_palette(p, &b);
}
static int parse(const char *text, bool marker, config_theme fallback, config_palette *out)
{
    config_error error = {0};
    int rc = config_palette_parse((const uint8_t *)text, strlen(text), marker, fallback, out, &error);
    return rc;
}

static int builtin_test(void)
{
    for (int t = CONFIG_DARK; t <= CONFIG_LIGHT; t++) {
        config_palette p; config_palette_builtin((config_theme)t, &p);
        size_t count; const config_token *tokens = config_theme_tokens((config_theme)t, &count);
        T(count == CONFIG_THEME_TOKENS && p.mode == (config_theme)t);
        for (size_t i = 0; i < count; i++) T(c(&p, tokens[i].name) == tokens[i].rgb);
        T(c(&p, "no.such.token") == 0 && c(&p, NULL) == 0);
    }
    return 0;
}
static int map_test(void)
{
    config_palette p;
    T(parse(tokyo, false, CONFIG_LIGHT, &p) == CONFIG_OK && p.mode == CONFIG_DARK);
    T(c(&p, "editor.background") == 0x1a1b26 && c(&p, "editor.foreground") == 0xa9b1d6);
    T(c(&p, "gutter.background") == 0x1a1b26 && c(&p, "chrome.background") == 0x1a1b26);
    T(c(&p, "tab.active.background") == 0x1a1b26 && c(&p, "tab.active.foreground") == 0xa9b1d6);
    T(c(&p, "caret.background") == 0x7aa2f7 && c(&p, "caret.foreground") == 0x1a1b26); /* caret on accent */
    T(c(&p, "selection.background") == 0x292e42 && c(&p, "selection.foreground") == 0xc0caf5);
    T(c(&p, "warning.foreground") == 0xe0af68 && c(&p, "error.foreground") == 0xf7768e);
    T(c(&p, "error.background") == mix(0x1a1b26, 0xf7768e, 15u));
    T(c(&p, "bracket.background") == mix(0x1a1b26, 0x7aa2f7, 20u));
    T(c(&p, "panel.background") == 0x24283b); /* dark: lighter_background */
    T(c(&p, "hover.background") == mix(0x1a1b26, 0xa9b1d6, 6u));
    T(c(&p, "tab.hover.foreground") == 0xa9b1d6);
    T(c(&p, "syntax.keyword") == 0xbb9af7 && c(&p, "syntax.string") == 0x9ece6a);
    T(c(&p, "syntax.number") == 0xeb927b && c(&p, "syntax.constant") == 0xff9e64);
    T(c(&p, "syntax.function") == 0x7aa2f7 && c(&p, "syntax.type") == 0xe0af68);
    T(c(&p, "syntax.variable") == 0xa9b1d6 && c(&p, "syntax.operator") == 0x7da6ff);
    T(c(&p, "syntax.punctuation") == 0xb4bee6 && c(&p, "syntax.attribute") == 0x449dab);
    /* muted #414868 is below the built-in ladder in tokyo-night; the derived
     * tokens move toward foreground and meet the built-in ratios. */
    T(ratio(0x414868, 0x1a1b26) < builtin_ratio(CONFIG_DARK, "syntax.comment", "editor.background"));
    T(derived(&p, CONFIG_DARK, "syntax.comment", 0x414868, "editor.background") == 0);
    T(derived(&p, CONFIG_DARK, "gutter.foreground", 0x414868, "editor.background") == 0);
    T(derived(&p, CONFIG_DARK, "minimap.foreground", 0x414868, "editor.background") == 0);
    T(derived(&p, CONFIG_DARK, "chrome.foreground", 0x565f89, "chrome.background") == 0);
    T(c(&p, "syntax.comment") != 0x414868);
    double comment = ratio(c(&p, "syntax.comment"), 0x1a1b26), gutter = ratio(c(&p, "gutter.foreground"), 0x1a1b26);
    T(ratio(0xa9b1d6, 0x1a1b26) > comment && comment > gutter && gutter >= ratio(c(&p, "minimap.foreground"), 0x1a1b26));
    printf("config_palette_test: tokyo-night comment %.2f:1 gutter %.2f:1 minimap %.2f:1 chrome %.2f:1 "
           "(built-in dark %.2f %.2f %.2f %.2f)\n", comment, gutter, ratio(c(&p, "minimap.foreground"), 0x1a1b26),
           ratio(c(&p, "chrome.foreground"), 0x1a1b26),
           builtin_ratio(CONFIG_DARK, "syntax.comment", "editor.background"),
           builtin_ratio(CONFIG_DARK, "gutter.foreground", "editor.background"),
           builtin_ratio(CONFIG_DARK, "minimap.foreground", "editor.background"),
           builtin_ratio(CONFIG_DARK, "chrome.foreground", "chrome.background"));

    T(parse(flexoki, false, CONFIG_DARK, &p) == CONFIG_OK && p.mode == CONFIG_LIGHT);
    T(c(&p, "editor.background") == 0xfffcf0 && c(&p, "editor.foreground") == 0x100f0f);
    T(c(&p, "caret.background") == 0x205ea6 && c(&p, "caret.foreground") == 0xfffcf0);
    T(c(&p, "selection.background") == 0xcecdc3 && c(&p, "selection.foreground") == 0x100f0f);
    T(c(&p, "panel.background") == 0xf2efe4); /* light: dark_background */
    T(c(&p, "syntax.operator") == 0x4385be && c(&p, "syntax.punctuation") == 0x403e3c);
    T(ratio(0xb7b5ac, 0xfffcf0) < builtin_ratio(CONFIG_LIGHT, "syntax.comment", "editor.background"));
    T(derived(&p, CONFIG_LIGHT, "syntax.comment", 0xb7b5ac, "editor.background") == 0);
    T(derived(&p, CONFIG_LIGHT, "gutter.foreground", 0xb7b5ac, "editor.background") == 0);
    T(derived(&p, CONFIG_LIGHT, "minimap.foreground", 0xb7b5ac, "editor.background") == 0);
    T(derived(&p, CONFIG_LIGHT, "chrome.foreground", 0x878580, "chrome.background") == 0);
    comment = ratio(c(&p, "syntax.comment"), 0xfffcf0); gutter = ratio(c(&p, "gutter.foreground"), 0xfffcf0);
    T(comment > gutter);
    printf("config_palette_test: flexoki-light comment %.2f:1 gutter %.2f:1 minimap %.2f:1 chrome %.2f:1 "
           "(built-in light %.2f %.2f %.2f %.2f)\n", comment, gutter, ratio(c(&p, "minimap.foreground"), 0xfffcf0),
           ratio(c(&p, "chrome.foreground"), 0xfffcf0),
           builtin_ratio(CONFIG_LIGHT, "syntax.comment", "editor.background"),
           builtin_ratio(CONFIG_LIGHT, "gutter.foreground", "editor.background"),
           builtin_ratio(CONFIG_LIGHT, "minimap.foreground", "editor.background"),
           builtin_ratio(CONFIG_LIGHT, "chrome.foreground", "chrome.background"));
    return 0;
}
/* The shell resolver's fallbacks (omarchy-theme-color resolve_theme_colors)
 * and mode precedence: mode, theme_type, light.mode marker, luminance > 382. */
static int fallback_test(void)
{
    config_palette p, light; config_palette_builtin(CONFIG_LIGHT, &light);
    T(parse("background = '#ffffff'\nforeground = '#000000'\nyellow = '#aa8800'\n", false, CONFIG_DARK, &p) == CONFIG_OK);
    T(p.mode == CONFIG_LIGHT); /* (P) 255 * 3 = 765 > 382 */
    T(c(&p, "syntax.number") == 0xaa8800); /* orange <- yellow */
    T(c(&p, "syntax.constant") == mix(0xaa8800, 0xffffff, 20u)); /* bright_yellow <- yellow + 20% white */
    T(c(&p, "selection.foreground") == 0x000000); /* <- bright_foreground <- foreground */
    T(c(&p, "panel.background") == mix(0xffffff, 0x000000, 25u)); /* dark_background */
    T(c(&p, "error.foreground") == c(&light, "error.foreground")); /* no red: built-in token */
    T(c(&p, "caret.background") == c(&light, "caret.background")); /* no accent */
    /* Legacy short names and ANSI aliases. */
    T(parse("bg = '#101010'\nfg = '#e0e0e0'\ncolor1 = '#ff0000'\ncolor8 = '#505050'\ncolor15 = '#fafafa'\n",
            false, CONFIG_LIGHT, &p) == CONFIG_OK);
    T(p.mode == CONFIG_DARK && c(&p, "editor.background") == 0x101010 && c(&p, "error.foreground") == 0xff0000);
    T(c(&p, "selection.foreground") == 0xfafafa && c(&p, "selection.background") == 0x505050);
    T(c(&p, "minimap.foreground") == 0x505050 || ratio(0x505050, 0x101010) < builtin_ratio(CONFIG_DARK, "minimap.foreground", "editor.background"));
    T(parse("theme_type = 'light'\nbackground = '#000000'\nforeground = '#ffffff'\n", false, CONFIG_DARK, &p) == CONFIG_OK);
    T(p.mode == CONFIG_LIGHT);
    T(parse("background = '#000000'\nforeground = '#ffffff'\n", true, CONFIG_DARK, &p) == CONFIG_OK && p.mode == CONFIG_LIGHT);
    T(parse("mode = 'dark'\nbackground = '#ffffff'\nforeground = '#000000'\n", true, CONFIG_LIGHT, &p) == CONFIG_OK);
    T(p.mode == CONFIG_DARK);
    /* Unknown keys, gradients and tables are ignored. */
    T(parse("hyprland_active_border = 'rgba(26a269ee) rgba(2ec27eee) 45deg'\nbackground='#1a1b26'\n"
            "foreground='#a9b1d6'\n[extra]\nred='#00ff00'\n", false, CONFIG_LIGHT, &p) == CONFIG_OK);
    T(p.mode == CONFIG_DARK && c(&p, "error.foreground") == config_theme_color(CONFIG_DARK, "error.foreground"));
    return 0;
}
static int malformed_test(void)
{
    config_palette p; config_error error;
    /* TOML syntax error after the mode key: built-in palette of that mode. */
    T(parse("mode = \"light\"\nbackground = #fffcf0\n", false, CONFIG_DARK, &p) != CONFIG_OK);
    T(is_builtin(&p, CONFIG_LIGHT) == 0);
    /* No background: value error; detected mode from the mode key. */
    T(parse("mode = 'light'\nforeground = '#000000'\n", false, CONFIG_DARK, &p) == CONFIG_ERR_VALUE);
    T(is_builtin(&p, CONFIG_LIGHT) == 0);
    /* Bad colour spelling counts as absent: falls back to the caller's mode. */
    T(parse("background = 'nothex'\nforeground = '#000000'\n", false, CONFIG_DARK, &p) == CONFIG_ERR_VALUE);
    T(is_builtin(&p, CONFIG_DARK) == 0);
    T(parse("", false, CONFIG_LIGHT, &p) == CONFIG_ERR_VALUE && is_builtin(&p, CONFIG_LIGHT) == 0);
    T(parse("background = '#12345'\nforeground='#000000'\n", false, CONFIG_DARK, &p) == CONFIG_ERR_VALUE);
    T(config_palette_parse(NULL, 0, false, CONFIG_DARK, &p, &error) == CONFIG_ERR_VALUE && is_builtin(&p, CONFIG_DARK) == 0);
    T(config_palette_parse((const uint8_t *)tokyo, strlen(tokyo), false, CONFIG_DARK, NULL, &error) == CONFIG_ERR_ARG);
    return 0;
}
static int settings_test(void)
{
    config_settings s; config_error error;
    config_defaults(&s); T(!s.theme_pinned && s.theme == CONFIG_DARK); /* default follows the system */
    T(config_parse((const uint8_t *)"theme='system'\n", strlen("theme='system'\n"), &s, &error) == CONFIG_OK);
    T(!s.theme_pinned && s.theme == CONFIG_DARK);
    T(config_parse((const uint8_t *)"theme='light'\n", strlen("theme='light'\n"), &s, &error) == CONFIG_OK);
    T(s.theme_pinned && s.theme == CONFIG_LIGHT);
    T(config_parse((const uint8_t *)"theme='dark'\n", strlen("theme='dark'\n"), &s, &error) == CONFIG_OK);
    T(s.theme_pinned && s.theme == CONFIG_DARK);
    T(config_parse((const uint8_t *)"theme='omarchy'\n", strlen("theme='omarchy'\n"), &s, &error) == CONFIG_ERR_VALUE);
    return 0;
}

#if !defined(_WIN32)
static int write_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    T(write(fd, text, strlen(text)) == (ssize_t)strlen(text)); T(close(fd) == 0); return 0;
}
static void settle(config_runtime *r)
{
    /* Drain every queued notification, then the reload it caused. */
    for (;;) {
        (void)config_runtime_tick(r);
        while (config_runtime_busy(r)) {
            (void)os_wake_wait(config_runtime_wake(r), INT64_C(1000000));
            (void)config_runtime_tick(r);
        }
        struct pollfd notice = {config_runtime_watchfd(r), POLLIN, 0};
        if (notice.fd < 0 || poll(&notice, 1, 0) != 1) return;
    }
}
static int wait_notice(config_runtime *r)
{
    struct pollfd notice = {config_runtime_watchfd(r), POLLIN, 0};
    T(poll(&notice, 1, -1) == 1); settle(r); return 0;
}
typedef struct paths { char state[512], current[512], theme[512], next[512], colors[512], next_colors[512], name[512]; } paths;
/* omarchy-theme-set L266-296, operation for operation (theme-system.md 3.1). */
static int theme_set(const paths *p, const char *colors, const char *name)
{
    T(mkdir(p->next, 0700) == 0);
    if (colors) T(write_file(p->next_colors, colors) == 0);
    if (colors) T(unlink(p->colors) == 0 || access(p->colors, F_OK) != 0);
    else (void)unlink(p->colors);
    (void)rmdir(p->theme);
    T(rename(p->next, p->theme) == 0);
    int fd = open(p->name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    T(write(fd, name, strlen(name)) == (ssize_t)strlen(name)); T(write(fd, "\n", 1) == 1); T(close(fd) == 0);
    return 0;
}
static int count_notices(const char *log)
{
    FILE *f = fopen(log, "r"); if (!f) return -1;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, f)) if (strstr(line, "colors.toml")) n++;
    fclose(f); return n;
}
static int runtime_test(void)
{
    char root[] = "/tmp/sublimite-palette-XXXXXX"; T(mkdtemp(root));
    char cfgdir[600], cfg[700], log[600]; paths p;
    T(snprintf(cfgdir, sizeof cfgdir, "%s/config", root) > 0 && mkdir(cfgdir, 0700) == 0);
    T(snprintf(cfg, sizeof cfg, "%s/config.toml", cfgdir) > 0);
    T(snprintf(p.state, sizeof p.state, "%s/omarchy", root) > 0 && mkdir(p.state, 0700) == 0);
    T(snprintf(p.current, sizeof p.current, "%s/current", p.state) > 0 && mkdir(p.current, 0700) == 0);
    T(snprintf(p.theme, sizeof p.theme, "%s/theme", p.current) > 0 && mkdir(p.theme, 0700) == 0);
    T(snprintf(p.next, sizeof p.next, "%s/next-theme", p.current) > 0);
    T(snprintf(p.colors, sizeof p.colors, "%s/colors.toml", p.theme) > 0);
    T(snprintf(p.next_colors, sizeof p.next_colors, "%s/colors.toml", p.next) > 0);
    T(snprintf(p.name, sizeof p.name, "%s/theme.name", p.current) > 0);
    T(snprintf(log, sizeof log, "%s/stderr.log", root) > 0);
    T(write_file(p.colors, tokyo) == 0 && write_file(p.name, "tokyo-night\n") == 0);

    /* The XDG_STATE_HOME lookup names the stock layout. */
    char path[CONFIG_TEXT_CAP];
    T(setenv("XDG_STATE_HOME", root, 1) == 0 && config_palette_path(path, sizeof path) == CONFIG_OK);
    char expected[700]; T(snprintf(expected, sizeof expected, "%s/omarchy/current/theme/colors.toml", root) > 0);
    T(!strcmp(path, expected));
    T(setenv("HOME", root, 1) == 0 && setenv("XDG_STATE_HOME", "relative", 1) == 0);
    T(config_palette_path(path, sizeof path) == CONFIG_OK);
    T(snprintf(expected, sizeof expected, "%s/.local/state/omarchy/current/theme/colors.toml", root) > 0);
    T(!strcmp(path, expected)); T(config_palette_path(path, 4) == CONFIG_ERR_LIMIT);

    config_palette want_tokyo, want_flexoki;
    T(parse(tokyo, false, CONFIG_DARK, &want_tokyo) == 0 && parse(flexoki, false, CONFIG_DARK, &want_flexoki) == 0);

    /* Capture worker notices: one line per malformed episode. */
    fflush(stderr); int saved = dup(2); T(saved >= 0);
    int logfd = open(log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(logfd >= 0 && dup2(logfd, 2) == 2);
    close(logfd);

    config_runtime *r = NULL; T(config_runtime_open_palette(&r, cfg, p.colors) == CONFIG_OK); settle(r);
    T(config_runtime_palette_error(r) == CONFIG_OK && same_palette(config_runtime_palette(r), &want_tokyo) == 0);
    T(config_runtime_generation(r) == 1u); /* (P) one publication: built-in -> tokyo-night */
    T(config_runtime_watchfd(r) >= 0);

    /* Switch: delete/rename/write order; exactly one publication. */
    uint64_t g = config_runtime_generation(r);
    T(theme_set(&p, flexoki, "flexoki-light") == 0);
    while (config_runtime_palette(r)->mode != CONFIG_LIGHT) T(wait_notice(r) == 0);
    settle(r);
    T(same_palette(config_runtime_palette(r), &want_flexoki) == 0 && config_runtime_generation(r) == g + 1u);
    /* Re-apply (omarchy-theme-refresh): same palette, no publication. */
    T(theme_set(&p, flexoki, "flexoki-light") == 0); T(wait_notice(r) == 0); settle(r);
    T(config_runtime_generation(r) == g + 1u && config_runtime_palette_error(r) == CONFIG_OK);
    /* theme/ absent (between rm and mv, or removed): the last palette stays. */
    T(unlink(p.colors) == 0 && rmdir(p.theme) == 0); T(write_file(p.name, "gone\n") == 0);
    T(wait_notice(r) == 0); settle(r);
    T(same_palette(config_runtime_palette(r), &want_flexoki) == 0 && config_runtime_generation(r) == g + 1u);
    T(mkdir(p.theme, 0700) == 0);
    /* Malformed: built-in palette of the detected mode, one notice per episode. */
    T(theme_set(&p, "mode = \"light\"\nbackground = #fff\n", "broken") == 0);
    while (config_runtime_palette_error(r) == CONFIG_OK) T(wait_notice(r) == 0);
    settle(r);
    T(is_builtin(config_runtime_palette(r), CONFIG_LIGHT) == 0);
    T(theme_set(&p, "mode = \"light\"\nbackground = #fff\n", "broken") == 0); T(wait_notice(r) == 0); settle(r);
    T(is_builtin(config_runtime_palette(r), CONFIG_LIGHT) == 0 && config_runtime_palette_error(r) != CONFIG_OK);
    /* Back to tokyo-night. */
    T(theme_set(&p, tokyo, "tokyo-night") == 0);
    while (config_runtime_palette_error(r) != CONFIG_OK) T(wait_notice(r) == 0);
    settle(r); T(same_palette(config_runtime_palette(r), &want_tokyo) == 0);
    /* theme = "light" pins the built-in palette; "system" follows again. */
    T(write_file(cfg, "theme = 'light'\n") == 0);
    while (!config_runtime_current(r)->theme_pinned) T(wait_notice(r) == 0);
    settle(r); T(is_builtin(config_runtime_palette(r), CONFIG_LIGHT) == 0);
    T(write_file(cfg, "theme = 'system'\n") == 0);
    while (config_runtime_current(r)->theme_pinned) T(wait_notice(r) == 0);
    settle(r); T(same_palette(config_runtime_palette(r), &want_tokyo) == 0);
    /* Zero idle wakeups: nothing is readable once the switch has settled. */
    struct pollfd idle[2] = {{config_runtime_watchfd(r), POLLIN, 0}, {config_runtime_eventfd(r), POLLIN, 0}};
    T(poll(idle, 2, 0) == 0);
    config_runtime_close(r);

    fflush(stderr); T(dup2(saved, 2) == 2); close(saved);
    T(count_notices(log) == 1);

    /* No Omarchy state (missing parent): today's built-in palette, no
     * publication, no notice; NULL palette path is the same. */
    char absent[700]; T(snprintf(absent, sizeof absent, "%s/none/current/theme/colors.toml", root) > 0);
    r = NULL; T(config_runtime_open_palette(&r, cfg, absent) == CONFIG_OK); settle(r);
    T(config_runtime_palette_error(r) == CONFIG_OK && config_runtime_generation(r) == 0u);
    T(is_builtin(config_runtime_palette(r), CONFIG_DARK) == 0); config_runtime_close(r);
    r = NULL; T(config_runtime_open(&r, cfg) == CONFIG_OK); settle(r);
    T(config_runtime_generation(r) == 0u && is_builtin(config_runtime_palette(r), CONFIG_DARK) == 0);
    config_runtime_close(r);
    /* Present parent, absent theme/ at startup: built-in, then follows the
     * first theme set. */
    T(unlink(p.colors) == 0 && rmdir(p.theme) == 0);
    r = NULL; T(config_runtime_open_palette(&r, cfg, p.colors) == CONFIG_OK); settle(r);
    T(is_builtin(config_runtime_palette(r), CONFIG_DARK) == 0);
    T(theme_set(&p, flexoki, "flexoki-light") == 0);
    while (config_runtime_palette(r)->mode != CONFIG_LIGHT) T(wait_notice(r) == 0);
    settle(r); T(same_palette(config_runtime_palette(r), &want_flexoki) == 0);
    config_runtime_close(r);

    char cmd[700]; T(snprintf(cmd, sizeof cmd, "rm -rf '%s'", root) > 0); T(system(cmd) == 0);
    return 0;
}
#endif

int main(void)
{
    T(builtin_test() == 0);
    T(map_test() == 0);
    T(fallback_test() == 0);
    T(malformed_test() == 0);
    T(settings_test() == 0);
#if !defined(_WIN32)
    T(runtime_test() == 0);
#endif
    puts("config_palette_test: built-in tables, tokyo-night and flexoki-light mapping and ladder, resolver fallbacks, "
         "malformed files, theme pin, live delete/rename/write switch, absent state passed");
    return 0;
}
