#include "menu/menu.h"
#include "palette/palette.h"
#include "config/config.h"
#include "base/base.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <stdio.h>
#include <string.h>
#include "config/actions.inc"

#define T(c) do { if (!(c)) { fprintf(stderr, "menu_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
/* (P) MENU_COUNT is the brief's title list; zero is the first index or
 * absence, one removes a string NUL, two is a title's padding cells. */
static const char *const titles[] = {"File", "Edit", "Selection", "Find", "View",
    "Goto", "Tools", "Preferences", "Help"};
static bool in_actions(keys_action action)
{
    for (size_t i = 0; i < sizeof action_names / sizeof action_names[0]; i++)
        if (action_names[i].action == action) return true;
    return false;
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static int table_integrity(void)
{
    T(MENU_COUNT == sizeof titles / sizeof titles[0]);
    const menu_def *menus = menu_table();
    bool seen_save_as = false, seen_toggle = false;
    uint32_t column = 0;
    for (size_t m = 0; m < MENU_COUNT; m++) {
        T(!strcmp(menus[m].title, titles[m]));
        T(menus[m].count != 0 && menus[m].items);
        /* Mnemonic: a letter of its own title, unique among titles. */
        size_t at = menu_mnemonic_offset(m);
        T(at < strlen(menus[m].title));
        T(lower(menus[m].title[at]) == menus[m].mnemonic);
        for (size_t n = 0; n < m; n++) T(menus[n].mnemonic != menus[m].mnemonic);
        T(menu_mnemonic((uint32_t)(unsigned char)menus[m].mnemonic) == (int)m);
        T(menu_mnemonic((uint32_t)(unsigned char)menus[m].mnemonic - ('a' - 'A')) == (int)m);
        T(menu_title_column(m) == column);
        column += (uint32_t)strlen(menus[m].title) + 2u;
        for (size_t i = 0; i < menus[m].count; i++) {
            keys_action action = menus[m].items[i];
            T(action != KEYS_ACTION_NONE && action != KEYS_ACTION_CHORD && action < KEYS_ACTION_COUNT);
            T(in_actions(action)); /* no action exists only in the menu */
            const keys_binding *command = menu_command(action);
            if (!command) fprintf(stderr, "menu_test: %s item %zu (action %d) has no command\n", menus[m].title, i, (int)action);
            T(command && command->action == action);
            T(command == palette_command(action)); /* the palette's own command */
            char label[PALETTE_LABEL_CAP];
            T(menu_label(action, label, sizeof label) == 0 && label[0]);
            if (m == 0 && action == KEYS_ACTION_SAVE_AS) seen_save_as = true;
            if (!strcmp(menus[m].title, "View") && action == KEYS_ACTION_TOGGLE_MENU) seen_toggle = true;
        }
    }
    T(seen_save_as && seen_toggle);
    T(menu_mnemonic('x') == -1 && menu_mnemonic(XKB_KEY_Return) == -1);
    T(menu_mnemonic('i') == 3 && menu_mnemonic('n') == 7); /* Sublime: F_ind, Prefere_nces */
    return 0;
}
static int labels_and_commands(void)
{
    char label[PALETTE_LABEL_CAP];
    T(menu_label(KEYS_ACTION_SAVE_AS, label, sizeof label) == 0 && !strcmp(label, "Save As"));
    T(menu_label(KEYS_ACTION_DOC_HOME, label, sizeof label) == 0 && !strcmp(label, "Document Home"));
    T(menu_label(KEYS_ACTION_TOGGLE_MENU, label, sizeof label) == 0 && !strcmp(label, "Toggle Menu"));
    T(menu_label(KEYS_ACTION_SAVE_AS, label, sizeof "Save As" - 1u) != 0);
    T(menu_label(KEYS_ACTION_NONE, label, sizeof label) != 0);
    /* Toggle Menu has no Sublime default stroke: unbound, still a command. */
    const keys_binding *toggle = menu_command(KEYS_ACTION_TOGGLE_MENU);
    T(toggle && !keys_binding_for_command(toggle, NULL, 0));
    char chord[KEYS_CHORD_FORMAT_CAP];
    const keys_binding *save_as = menu_command(KEYS_ACTION_SAVE_AS);
    T(keys_chord_format(keys_binding_for_command(save_as, NULL, 0), chord, sizeof chord) == 0);
    T(!strcmp(chord, "Ctrl+Shift+S"));
    config_settings settings; config_error error;
    const char config[] = "[keymap]\ntoggle_menu='Ctrl+Alt+m'\n";
    T(config_parse((const uint8_t *)config, sizeof config - 1u, &settings, &error) == CONFIG_OK);
    const keys_binding *live = keys_binding_for_command(toggle, settings.bindings, settings.binding_count);
    T(live && keys_chord_format(live, chord, sizeof chord) == 0 && !strcmp(chord, "Ctrl+Alt+M"));
    keys_state state = {0}; const keys_binding *hit = NULL;
    T(config_keys_lookup(&settings, &state, 'm', PLAT_MOD_CTRL | PLAT_MOD_ALT, &hit) == KEYS_MATCH);
    T(hit && hit->action == KEYS_ACTION_TOGGLE_MENU);
    /* The palette lists the unbound toggle too (the menu adds no action). */
    palette_state p; palette_init(&p);
    T(palette_filter(&p, "toggle menu", sizeof "toggle menu" - 1u) == 0 && p.count);
    T(p.rows[p.matches[0]].binding == toggle);
    edit_malloc_guard_begin();
    for (size_t m = 0; m < MENU_COUNT; m++)
        for (size_t i = 0; i < menu_table()[m].count; i++) {
            T(menu_label(menu_table()[m].items[i], label, sizeof label) == 0);
            T(menu_command(menu_table()[m].items[i]));
        }
    T(menu_mnemonic('f') == 0);
    T(edit_malloc_guard_end() == 0); /* (P) typing-path allocation law */
    return 0;
}
int main(void)
{
    T(table_integrity() == 0);
    T(labels_and_commands() == 0);
    puts("menu_test: table integrity, unique mnemonics, labels, commands and malloc guard passed");
    return 0;
}
