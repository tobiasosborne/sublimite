#include "config/config.h"
#include "base/base.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sched.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define T(x) do { if (!(x)) { fprintf(stderr, "config_test: FAIL %s\n", #x); return 1; } } while (0)
typedef struct oracle { size_t visited; } oracle;
static int visit(void *ctx, const char *path, const config_value *v)
{
    oracle *o = ctx; o->visited++;
    if (!strcmp(path, "test.min")) T(v->type == CONFIG_INTEGER && v->integer == INT64_MIN);
    else if (!strcmp(path, "test.max")) T(v->type == CONFIG_INTEGER && v->integer == INT64_MAX);
    else if (!strcmp(path, "test.float")) T(v->type == CONFIG_FLOAT && v->number == 125.0);
    else if (!strcmp(path, "test.bool")) T(v->type == CONFIG_BOOLEAN && !v->boolean);
    else if (!strcmp(path, "test.array")) {
        T(v->type == CONFIG_STRINGS && v->count == 2u); /* (P) fixture has two elements */
        T(!strcmp(v->strings[0], "a\n\xe2\x98\x83") && !strcmp(v->strings[1], "b\\c"));
    } else if (!strcmp(path, "test.empty")) T(v->type == CONFIG_STRINGS && !v->count);
    else T(false);
    return 0;
}
static int parser_test(void)
{
    /* Decimal extrema come from int64_t's range; float is 1.25 * 10^2.
     * Unicode escape input encodes the snowman scalar, literal string keeps
     * its backslash. Counts derive from these named fixture assignments. */
    const char source[] = "# comment\r\n[test]\r\nmin = -9223372036854775808\n"
        "max = +9_223_372_036_854_775_807\nfloat = 1.2_5e+2\nbool = false\n"
        "array = [\n # comment\n\"a\\n\\u2603\", 'b\\c',\n]\nempty = []";
    oracle o = {0}; config_error error;
    T(config_toml_parse((const uint8_t *)source, sizeof source - 1u, visit, &o, &error) == CONFIG_OK);
    T(o.visited == 6u); /* (P) min/max/float/bool/array/empty */
    const char *bad[] = {
        "a=1\na=2", "[a]\n[a]", "a=1\n[a]", "a=1\na.b=2", "a.b=1\na=2",
        "a.b=1\n[a]", "[a]\nb.c=1\n[a.b]",
        "[a.b]\nx=1\n[a]\nb.c=2",
        "a = 9223372036854775808", "a = -9223372036854775809", "a=1e99999", "a=1e-99999",
        "a=01", "a=1_", "a=1__2", "a=.5", "a=1.", "a=1e", "a=+", "a=--1",
        "a=truex", "a=False", "a='unterminated", "a=\"\\q\"", "a=\"\\u0000\"",
        "a=\"\\uD800\"", "a=\"\\U00110000\"", "a=\"\\u123\"", "a=\"raw\nline\"",
        "a=[1]", "a=['a' 'b']", "a=[['a']]", "a=['a'", "a={b=1}", "[[a]]",
        "a=0x10", "a=nan", "a=1979-05-27", "[a] junk", "a=1 junk", "a=1\r", "a..b=1"
    };
    config_settings settings; config_defaults(&settings);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_settings before = settings;
        T(config_parse((const uint8_t *)bad[i], strlen(bad[i]), &settings, &error) != CONFIG_OK);
        T(error.code && error.offset <= strlen(bad[i]));
        T(!memcmp(&before, &settings, sizeof settings));
    }
    const uint8_t invalid[] = {'a', '=', '\'', 0xff, '\''};
    T(config_parse(invalid, sizeof invalid, &settings, &error) == CONFIG_ERR_SYNTAX);
    const uint8_t nul[] = {'#', 0};
    T(config_parse(nul, sizeof nul, &settings, &error) == CONFIG_ERR_SYNTAX);
    T(config_parse(NULL, 0, &settings, &error) == CONFIG_OK);
    T(config_parse(NULL, 1, &settings, &error) == CONFIG_ERR_ARG); /* (P) nonempty NULL span */
    T(config_parse(invalid, CONFIG_SOURCE_CAP + 1u, &settings, &error) == CONFIG_ERR_LIMIT);
    char oversized[CONFIG_TEXT_CAP + sizeof("a=''\n")];
    strcpy(oversized, "a='"); memset(oversized + strlen("a='"), 'x', CONFIG_TEXT_CAP);
    strcpy(oversized + strlen("a='") + CONFIG_TEXT_CAP, "'\n");
    T(config_parse((const uint8_t *)oversized, strlen(oversized), &settings, &error) == CONFIG_ERR_LIMIT);
    strcpy(oversized + strlen("a='") + CONFIG_TEXT_CAP - 1u, "'\n");
    T(config_parse((const uint8_t *)oversized, strlen(oversized), &settings, &error) == CONFIG_OK);
    char long_key[CONFIG_NAME_CAP + sizeof("=true")]; memset(long_key, 'k', CONFIG_NAME_CAP);
    strcpy(long_key + CONFIG_NAME_CAP, "=true");
    T(config_parse((const uint8_t *)long_key, strlen(long_key), &settings, &error) == CONFIG_ERR_LIMIT);
    char too_many[CONFIG_TEXT_CAP]; size_t written = 0;
    for (size_t i = 0; i <= CONFIG_ENTRY_CAP; i++) {
        int added = snprintf(too_many + written, sizeof too_many - written, "k%zu=true\n", i);
        T(added > 0 && (size_t)added < sizeof too_many - written); written += (size_t)added;
    }
    T(config_parse((const uint8_t *)too_many, written, &settings, &error) == CONFIG_ERR_LIMIT);
    /* (P) every byte position and every possible octet in a representative
     * settings document; no invented fuzz duration/iteration budget. */
    const char seed[] = "theme='light'\n[font]\nsize=12.5\nfamily='Mono'\n";
    uint8_t mutated[sizeof seed - 1u];
    for (size_t at = 0; at < sizeof mutated; at++) for (unsigned byte = 0; byte <= UINT8_MAX; byte++) {
        memcpy(mutated, seed, sizeof mutated); mutated[at] = (uint8_t)byte;
        config_settings before = settings;
        int rc = config_parse(mutated, sizeof mutated, &settings, &error);
        T(error.code == rc && error.offset <= sizeof mutated);
        if (rc) T(!memcmp(&before, &settings, sizeof settings));
    }
    puts("config_test: TOML types, malformed input and exhaustive single-byte mutations passed");
    return 0;
}
static int write_config(const char *path, const char *source)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    T(write(fd, source, strlen(source)) == (ssize_t)strlen(source)); T(close(fd) == 0); return 0;
}
static void wait_runtime(config_runtime *r)
{
    /* Blocking test synchronization; production never blocks. No artificial
     * time budget. Finished leases can lag a delivered mailbox by scheduling,
     * so yield in that case instead of waiting for a nonexistent message. */
    do {
        struct pollfd fds[] = {{config_runtime_eventfd(r), POLLIN, 0}, {config_runtime_watchfd(r), POLLIN, 0}};
        (void)poll(fds, sizeof fds / sizeof fds[0], 0);
        (void)config_runtime_tick(r);
        sched_yield();
    } while (config_runtime_busy(r));
}
static int runtime_test(void)
{
    char root[] = "/tmp/sublimite-config-XXXXXX", path[CONFIG_TEXT_CAP], other[CONFIG_TEXT_CAP];
    T(mkdtemp(root)); T(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    T(config_path(path, sizeof path) == CONFIG_OK);
    T(config_path(path, sizeof root) == CONFIG_ERR_LIMIT);
    T(setenv("HOME", root, 1) == 0 && setenv("XDG_CONFIG_HOME", "relative", 1) == 0);
    T(config_path(path, sizeof path) == CONFIG_OK);
    T(snprintf(other, sizeof other, "%s/.config/sublimite/config.toml", root) > 0);
    T(!strcmp(path, other)); T(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    T(config_path(path, sizeof path) == CONFIG_OK);
    config_runtime *r = NULL; T(config_runtime_open(&r, NULL) == CONFIG_OK); wait_runtime(r);
    T(config_runtime_error(r).code == CONFIG_OK && config_runtime_watchfd(r) >= 0);
    T(write_config(path, "theme='light'\n[font]\nsize=14\n") == 0);
    struct pollfd notice = {config_runtime_watchfd(r), POLLIN, 0}; T(poll(&notice, 1, -1) == 1);
    (void)config_runtime_tick(r); wait_runtime(r);
    T(config_runtime_current(r)->theme == CONFIG_LIGHT && config_runtime_current(r)->font_points == 14.0);
    uint64_t generation = config_runtime_generation(r);
    T(write_config(path, "theme='broken'\n") == 0);
    T(poll(&notice, 1, -1) == 1); (void)config_runtime_tick(r); wait_runtime(r);
    T(config_runtime_error(r).code == CONFIG_ERR_VALUE);
    T(config_runtime_generation(r) == generation && config_runtime_current(r)->theme == CONFIG_LIGHT);
    T(snprintf(other, sizeof other, "%s/sublimite/other.toml", root) > 0);
    config_runtime *peer = NULL; T(config_runtime_open(&peer, other) == CONFIG_OK); wait_runtime(peer);
    T(config_runtime_watchfd(peer) == config_runtime_watchfd(r));
    config_runtime_close(peer); /* stopping a shared parent member must keep r */
    T(write_config(other, "theme='dark'\nwrap=false\n") == 0); T(rename(other, path) == 0);
    T(poll(&notice, 1, -1) == 1); (void)config_runtime_tick(r); wait_runtime(r);
    T(config_runtime_error(r).code == CONFIG_OK && config_runtime_current(r)->theme == CONFIG_DARK);
    T(config_runtime_current(r)->wrap_set && !config_runtime_current(r)->wrap);
    T(config_runtime_generation(r) == generation + 1u); /* (P) one valid replacement publication */
    T(write_config(path, "whitespace.trailing='all'\n") == 0);
    T(poll(&notice, 1, -1) == 1); (void)config_runtime_tick(r); wait_runtime(r);
    T(config_runtime_current(r)->trailing == CONFIG_TRAILING_ALL);
    T(config_runtime_error(r).code == CONFIG_OK);
    T(write_config(path, "theme='light'\nwhitespace.trailing='invalid'\n") == 0);
    T(poll(&notice, 1, -1) == 1); (void)config_runtime_tick(r); wait_runtime(r);
    T(config_runtime_error(r).code == CONFIG_ERR_VALUE);
    T(config_runtime_current(r)->trailing == CONFIG_TRAILING_NONE);
    T(config_runtime_current(r)->theme == CONFIG_LIGHT);
    T(unlink(path) == 0); T(poll(&notice, 1, -1) == 1); (void)config_runtime_tick(r); wait_runtime(r);
    T(!config_runtime_current(r)->wrap_set); /* absent file restores defaults */
    T(!config_runtime_current(r)->trailing_warning && config_runtime_error(r).code == CONFIG_OK);
    config_runtime_close(r);
    T(snprintf(path, sizeof path, "%s/sublimite", root) > 0); T(rmdir(path) == 0); T(rmdir(root) == 0);
    puts("config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed");
    return 0;
}
static int trailing_test(void)
{
    /* (P) Cases enumerate the bead's supported spellings, an unknown value,
     * and a wrong TOML type. Zero denotes absence, one removes the NUL. */
    const struct { const char *source; config_trailing mode; bool warning; } cases[] = {
        {"", CONFIG_TRAILING_NONE, false},
        {"whitespace.trailing='none'", CONFIG_TRAILING_NONE, false},
        {"whitespace.trailing='all'", CONFIG_TRAILING_ALL, false},
        {"[whitespace]\ntrailing='selection'", CONFIG_TRAILING_SELECTION, false},
        {"whitespace.trailing='invalid'", CONFIG_TRAILING_NONE, true},
        {"whitespace.trailing=true", CONFIG_TRAILING_NONE, true}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        config_settings settings; config_defaults(&settings); config_error error;
        T(settings.trailing == CONFIG_TRAILING_NONE && !settings.trailing_warning);
        T(config_parse((const uint8_t *)cases[i].source, strlen(cases[i].source), &settings, &error) == CONFIG_OK);
        T(settings.trailing == cases[i].mode && settings.trailing_warning == cases[i].warning);
    }
    puts("config_test: trailing whitespace defaults, values and invalid fallback passed");
    return 0;
}
int main(void)
{
    T(trailing_test() == 0);
    T(parser_test() == 0);
    const char source[] = "theme = 'light'\nwrap = true\ntabs = 8\n"
        "[font]\nsize = 12.5\nfamily = \"DejaVu Sans Mono\"\nfile='vendor/DejaVuSansMono.ttf#0'\n"
        "[keymap]\nredo = \"Ctrl+Y\"\n";
    /* (P) fixture: fractional points, tab width supplied by the user; literals
     * are parser inputs, never a latency/memory planning budget. */
    config_settings settings; config_defaults(&settings); config_error error;
    T(config_parse((const uint8_t *)source, sizeof source - 1u, &settings, &error) == CONFIG_OK);
    T(settings.font_points == 12.5 && settings.tab_width == 8u);
    T(settings.theme == CONFIG_LIGHT && settings.wrap_set && settings.wrap);
    T(!strcmp(settings.font_family, "DejaVu Sans Mono"));
    T(!strcmp(settings.font_file, "vendor/DejaVuSansMono.ttf#0")); /* (P) fixture path/face */
    keys_state state = {0}; const keys_binding *binding;
    T(config_keys_lookup(&settings, &state, 'Y', PLAT_MOD_CTRL, &binding) == KEYS_MATCH);
    T(binding->action == KEYS_ACTION_REDO);
    T(config_keys_lookup(&settings, &state, 'z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, &binding) == KEYS_NONE);
    T(config_keys_lookup(&settings, &state, 'Y', PLAT_MOD_CTRL | PLAT_MOD_CAPS, &binding) == KEYS_MATCH);
    T(config_keys_lookup(&settings, &state, 'y', PLAT_MOD_CTRL | PLAT_MOD_ALTGR, &binding) == KEYS_NONE);
    config_settings before = settings;
    const char bad[] = "[font]\nsize = -1\n";
    T(config_parse((const uint8_t *)bad, sizeof bad - 1u, &settings, &error) == CONFIG_ERR_VALUE);
    T(!memcmp(&before, &settings, sizeof settings));
    const char alternatives[] = "[keymap]\nundo=['Ctrl+U','Ctrl+Shift+!']\nredo='Ctrl++'\n";
    config_settings alternatives_settings;
    T(config_parse((const uint8_t *)alternatives, sizeof alternatives - 1u, &alternatives_settings, &error) == CONFIG_OK);
    T(config_keys_lookup(&alternatives_settings, &state, 'u', PLAT_MOD_CTRL, &binding) == KEYS_MATCH);
    T(binding->action == KEYS_ACTION_UNDO);
    T(config_keys_lookup(&alternatives_settings, &state, '!', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, &binding) == KEYS_MATCH);
    T(binding->action == KEYS_ACTION_UNDO);
    T(config_keys_lookup(&alternatives_settings, &state, '+', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, &binding) == KEYS_MATCH);
    T(binding->action == KEYS_ACTION_REDO);
    const char duplicate[] = "[keymap]\nredo='Ctrl+Y'\nundo='Ctrl+Y'\n";
    T(config_parse((const uint8_t *)duplicate, sizeof duplicate - 1u, &settings, &error) == CONFIG_ERR_VALUE);
    size_t dark_count, light_count;
    const config_token *dark = config_theme_tokens(CONFIG_DARK, &dark_count);
    const config_token *light = config_theme_tokens(CONFIG_LIGHT, &light_count);
    T(dark_count == light_count && dark_count);
    for (size_t i = 0; i < dark_count; i++) T(!strcmp(dark[i].name, light[i].name));
    edit_malloc_guard_begin();
    T(config_keys_lookup(&settings, &state, 'y', PLAT_MOD_CTRL, &binding) == KEYS_MATCH);
    size_t allocations = edit_malloc_guard_end();
    T(!edit_malloc_guard_active() || !allocations);
    T(runtime_test() == 0);
    puts("config_test: parser, settings and keymap passed");
    return 0;
}
