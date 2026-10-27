#include "editor/private.h"
#include "base/base.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <sched.h>
#include <stdio.h>
#include <string.h>

/* (P) zero is the C/API success, empty state and first byte/index; one is
 * the C string terminator or a single event. All byte offsets derive from
 * the fixture strings. Keysyms are protocol constants, not numeric budgets. */
#define T(x) do { if (!(x)) { fprintf(stderr, "config_find_test: FAIL %s\n", #x); return 1; } } while (0)

static int settle(editor *e)
{
    do {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        sched_yield();
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active ||
             findui_get_state(&e->find).searching || e->find_select_pending);
    return 0;
}
static int press(editor *e, uint32_t sym, uint16_t mods)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods};
    edit_malloc_guard_begin();
    T(editor_inject(e, &ev) == EDITOR_OK);
    T(settle(e) == 0);
    size_t allocations = edit_malloc_guard_end();
    T(!edit_malloc_guard_active() || !allocations);
    return 0;
}
static int find_next_override(bool visible)
{
    static const char text[] = "cat cat cat\n", query[] = "cat";
    static const char preferences[] = "[keymap]\nfind_next='Ctrl+J'\n";
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    /* (P) one minimap (eight columns) plus one tab slot (sixteen columns);
     * one tab row, one find row and one document row. */
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = 8u + 16u, .max_cols = 8u + 16u, .rows = 1u + 1u + 1u,
        .max_rows = 1u + 1u + 1u, .preferences_path = "build/edit-x8i-absent-config.toml"};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); T(settle(e) == 0);
    T(config_parse((const uint8_t *)preferences, sizeof preferences - 1u,
                   &e->preferences, NULL) == CONFIG_OK);
    T(press(e, XKB_KEY_f, PLAT_MOD_CTRL) == 0);
    T(findui_set_query(&e->find, (const uint8_t *)query, sizeof query - 1u) == FINDUI_OK);
    T(settle(e) == 0);
    if (!visible) T(press(e, XKB_KEY_Escape, 0) == 0);
    T(e->find_visible == visible);
    view_selection before = editor_view(e).selection;
    T(press(e, XKB_KEY_F3, 0) == 0);
    T(editor_view(e).selection.cursor == before.cursor &&
      editor_view(e).selection.anchor == before.anchor);
    T(press(e, XKB_KEY_j, PLAT_MOD_CTRL) == 0);
    T(editor_view(e).selection.anchor == 0 &&
      editor_view(e).selection.cursor == sizeof query - 1u);
    T(press(e, XKB_KEY_j, PLAT_MOD_CTRL) == 0);
    T(editor_view(e).selection.anchor == sizeof "cat " - 1u &&
      editor_view(e).selection.cursor == sizeof "cat cat" - 1u);
    before = editor_view(e).selection;
    T(press(e, XKB_KEY_F3, 0) == 0);
    T(editor_view(e).selection.cursor == before.cursor &&
      editor_view(e).selection.anchor == before.anchor);
    T(e->find_visible == visible);
    T(editor_get_stats(e).unimplemented_actions == 0);
    editor_close(e);
    puts(visible ? "config_find_test: find-next override with panel open passed" :
                   "config_find_test: find-next override with panel closed passed");
    return 0;
}
static int other_overrides(void)
{
    static const char text[] = "cat cat\n", query[] = "cat";
    static const char preferences[] = "[keymap]\nfind='Super+f'\nreplace='Super+h'\n"
        "find_previous='Super+p'\ntoggle_regex='Super+r'\n"
        "toggle_case_sensitive='Super+c'\ntoggle_whole_word='Super+w'\n"
        "select_all='Super+a'\npaste='Super+v'\ncancel='Super+Escape'\n";
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    /* (P) columns = minimap plus tab slot as above; rows = tab, document,
     * and the replace panel's query, replacement and status rows. */
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = 8u + 16u, .max_cols = 8u + 16u, .rows = 1u + 1u + 3u,
        .max_rows = 1u + 1u + 3u, .preferences_path = "build/edit-x8i-absent-config.toml"};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); T(settle(e) == 0);
    T(config_parse((const uint8_t *)preferences, sizeof preferences - 1u,
                   &e->preferences, NULL) == CONFIG_OK);
    T(press(e, XKB_KEY_f, PLAT_MOD_CTRL) == 0); T(!e->find_visible);
    T(press(e, XKB_KEY_Find, 0) == 0); T(!e->find_visible);
    T(press(e, XKB_KEY_F, PLAT_MOD_SUPER | PLAT_MOD_CAPS) == 0); T(e->find_visible);
    T(press(e, XKB_KEY_h, PLAT_MOD_CTRL) == 0); T(!findui_get_state(&e->find).replace_mode);
    T(press(e, XKB_KEY_h, PLAT_MOD_SUPER) == 0); T(findui_get_state(&e->find).replace_mode);
    T(findui_set_query(&e->find, (const uint8_t *)query, sizeof query - 1u) == FINDUI_OK);
    T(settle(e) == 0);
    T(press(e, XKB_KEY_r, PLAT_MOD_ALT) == 0); T(!findui_get_state(&e->find).options.regex);
    T(press(e, XKB_KEY_c, PLAT_MOD_ALT) == 0); T(!findui_get_state(&e->find).options.match_case);
    T(press(e, XKB_KEY_w, PLAT_MOD_ALT) == 0); T(!findui_get_state(&e->find).options.whole_word);
    T(press(e, XKB_KEY_r, PLAT_MOD_SUPER) == 0); T(findui_get_state(&e->find).options.regex);
    T(press(e, XKB_KEY_c, PLAT_MOD_SUPER) == 0); T(findui_get_state(&e->find).options.match_case);
    T(press(e, XKB_KEY_w, PLAT_MOD_SUPER) == 0); T(findui_get_state(&e->find).options.whole_word);
    /* edit-cxho.12: toggles select the origin preview; the disabled binding
     * must preserve that selection (P: fixture literal offsets). */
    view_selection preview = editor_view(e).selection;
    T(preview.cursor == sizeof query - 1u && preview.anchor == 0);
    T(press(e, XKB_KEY_F3, PLAT_MOD_SHIFT) == 0);
    T(editor_view(e).selection.cursor == preview.cursor && editor_view(e).selection.anchor == preview.anchor);
    T(press(e, XKB_KEY_p, PLAT_MOD_SUPER) == 0);
    T(editor_view(e).selection.anchor == sizeof "cat " - 1u &&
      editor_view(e).selection.cursor == sizeof "cat cat" - 1u);
    T(press(e, XKB_KEY_Left, 0) == 0); T(!e->find_field_all);
    view_selection before = editor_view(e).selection;
    T(press(e, XKB_KEY_a, PLAT_MOD_CTRL) == 0); T(e->find_field_all);
    T(press(e, XKB_KEY_v, PLAT_MOD_CTRL) == 0);
    T(editor_view(e).selection.anchor == before.anchor && editor_view(e).selection.cursor == before.cursor);
    T(press(e, XKB_KEY_Escape, 0) == 0); T(!e->find_visible);
    T(press(e, XKB_KEY_p, PLAT_MOD_SUPER) == 0);
    T(editor_view(e).selection.anchor == 0 && editor_view(e).selection.cursor == sizeof query - 1u);
    T(press(e, XKB_KEY_r, PLAT_MOD_SUPER) == 0); T(findui_get_state(&e->find).options.regex);
    T(editor_get_stats(e).unimplemented_actions == 0);
    editor_close(e);
    puts("config_find_test: open/replace/previous/toggle overrides and local editing passed");
    return 0;
}
static int replace_next_shortcuts(bool remapped)
{
    static const char text[]="cat cat cat", query[]="cat", replacement[]="fox";
    static const char preferences[]="[keymap]\nreplace_next='Ctrl+J'\n";
    render_backend backend={0}; T(render_null_backend(&backend)==RENDER_OK);
    /* (P) reuse the minimap/tab columns and tab/document/replace-panel rows
     * from other_overrides. Offsets and read sizes derive from the strings. */
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1u,
        .cols=8u+16u,.max_cols=8u+16u,.rows=1u+1u+3u,.max_rows=1u+1u+3u,
        .preferences_path="build/edit-g02-absent-config.toml"};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==EDITOR_OK); T(settle(e)==0);
    if (remapped) T(config_parse((const uint8_t *)preferences,sizeof preferences-1u,
                                &e->preferences,NULL)==CONFIG_OK);
    uint32_t sym=remapped ? XKB_KEY_j : XKB_KEY_H;
    uint16_t mods=remapped ? PLAT_MOD_CTRL : PLAT_MOD_CTRL|PLAT_MOD_SHIFT|PLAT_MOD_CAPS;
    /* Replace-next is harmless until the replacement panel owns the fields. */
    T(press(e,sym,mods)==0); T(!e->find_visible);
    T(press(e,XKB_KEY_f,PLAT_MOD_CTRL)==0);
    T(findui_set_query(&e->find,(const uint8_t *)query,sizeof query-1u)==FINDUI_OK);
    T(settle(e)==0); T(press(e,sym,mods)==0);
    T(editor_length(e)==sizeof text-1u);
    T(press(e,XKB_KEY_h,PLAT_MOD_CTRL)==0);
    T(findui_set_replacement(&e->find,(const uint8_t *)replacement,sizeof replacement-1u,
                             sizeof replacement-1u)==FINDUI_OK);
    uint8_t bytes[sizeof text-1u];
    if (remapped) {
        T(press(e,XKB_KEY_h,PLAT_MOD_CTRL|PLAT_MOD_SHIFT)==0);
        T(editor_read(e,0,bytes,sizeof bytes)==EDITOR_OK); T(!memcmp(bytes,text,sizeof bytes));
    }
    T(press(e,sym,mods)==0); T(e->find_visible && findui_get_state(&e->find).replace_mode);
    T(editor_read(e,0,bytes,sizeof bytes)==EDITOR_OK); T(!memcmp(bytes,"fox cat cat",sizeof bytes));
    T(press(e,XKB_KEY_Return,PLAT_MOD_CTRL)==0); T(e->find_visible);
    T(editor_read(e,0,bytes,sizeof bytes)==EDITOR_OK); T(!memcmp(bytes,"fox fox cat",sizeof bytes));
    T(press(e,XKB_KEY_Return,PLAT_MOD_CTRL|PLAT_MOD_ALT)==0); T(e->find_visible);
    T(editor_read(e,0,bytes,sizeof bytes)==EDITOR_OK); T(!memcmp(bytes,"fox fox fox",sizeof bytes));
    T(press(e,XKB_KEY_Escape,0)==0); T(!e->find_visible);
    T(press(e,XKB_KEY_z,PLAT_MOD_CTRL)==0);
    T(editor_read(e,0,bytes,sizeof bytes)==EDITOR_OK); T(!memcmp(bytes,"fox fox cat",sizeof bytes));
    T(editor_get_stats(e).unimplemented_actions==0);
    editor_close(e);
    puts(remapped ? "config_find_test: remapped replace-next and retained Enter shortcuts passed" :
                    "config_find_test: default replace-next and retained Enter shortcuts passed");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--g02-default")) return replace_next_shortcuts(false);
    if (argc > 1 && !strcmp(argv[1], "--g02-remapped")) return replace_next_shortcuts(true);
    if (argc > 1 && !strcmp(argv[1], "--closed")) return find_next_override(false);
    T(find_next_override(true) == 0);
    T(find_next_override(false) == 0);
    T(other_overrides() == 0);
    T(replace_next_shortcuts(false) == 0);
    T(replace_next_shortcuts(true) == 0);
    return 0;
}
