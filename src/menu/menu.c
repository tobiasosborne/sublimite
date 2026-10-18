#include "menu/menu.h"
#include "palette/palette.h"
#include <string.h>
#include "config/actions.inc"

/* Items follow Sublime's menus, flattened (no submenus or separators: the
 * visual pass is edit-b0j). Only actions with a palette command appear. */
static const keys_action file_items[] = {
    KEYS_ACTION_NEW_FILE, KEYS_ACTION_OPEN_FILE, KEYS_ACTION_REOPEN_FILE,
    KEYS_ACTION_SAVE, KEYS_ACTION_SAVE_AS, KEYS_ACTION_NEW_WINDOW,
    KEYS_ACTION_CLOSE_WINDOW, KEYS_ACTION_CLOSE, KEYS_ACTION_EXIT,
};
static const keys_action edit_items[] = {
    KEYS_ACTION_UNDO, KEYS_ACTION_REDO_OR_REPEAT, KEYS_ACTION_SOFT_UNDO, KEYS_ACTION_SOFT_REDO,
    KEYS_ACTION_CUT, KEYS_ACTION_COPY, KEYS_ACTION_PASTE, KEYS_ACTION_PASTE_AND_INDENT,
    KEYS_ACTION_PASTE_HISTORY, KEYS_ACTION_INDENT, KEYS_ACTION_UNINDENT,
    KEYS_ACTION_MOVE_LINE_UP, KEYS_ACTION_MOVE_LINE_DOWN, KEYS_ACTION_DUPLICATE_LINE,
    KEYS_ACTION_DELETE_LINE, KEYS_ACTION_JOIN_LINES, KEYS_ACTION_TOGGLE_COMMENT,
    KEYS_ACTION_INSERT_LINE_BEFORE, KEYS_ACTION_INSERT_LINE_AFTER, KEYS_ACTION_DELETE_TO_EOL,
    KEYS_ACTION_DELETE_TO_BOL, KEYS_ACTION_TRANSPOSE, KEYS_ACTION_CLOSE_TAG,
    KEYS_ACTION_UPPER_CASE, KEYS_ACTION_LOWER_CASE, KEYS_ACTION_WRAP_LINES,
    KEYS_ACTION_SORT_LINES, KEYS_ACTION_FOLD, KEYS_ACTION_UNFOLD, KEYS_ACTION_UNFOLD_ALL,
    KEYS_ACTION_AUTO_COMPLETE, KEYS_ACTION_TOGGLE_OVERWRITE,
};
static const keys_action selection_items[] = {
    KEYS_ACTION_SPLIT_SELECTION_LINES, KEYS_ACTION_ADD_NEXT_OCCURRENCE,
    KEYS_ACTION_SKIP_NEXT_OCCURRENCE, KEYS_ACTION_SELECT_ALL, KEYS_ACTION_SELECT_LINE,
    KEYS_ACTION_SELECT_SCOPE, KEYS_ACTION_SELECT_BRACKETS, KEYS_ACTION_SELECT_SMART,
};
static const keys_action find_items[] = {
    KEYS_ACTION_FIND, KEYS_ACTION_FIND_NEXT, KEYS_ACTION_FIND_PREVIOUS,
    KEYS_ACTION_INCREMENTAL_FIND, KEYS_ACTION_REPLACE, KEYS_ACTION_REPLACE_NEXT,
    KEYS_ACTION_FIND_UNDER, KEYS_ACTION_FIND_UNDER_PREVIOUS, KEYS_ACTION_FIND_ALL_UNDER,
    KEYS_ACTION_SLURP_FIND, KEYS_ACTION_SLURP_REPLACE, KEYS_ACTION_FIND_IN_FILES,
    KEYS_ACTION_NEXT_RESULT, KEYS_ACTION_PREVIOUS_RESULT, KEYS_ACTION_TOGGLE_REGEX,
    KEYS_ACTION_TOGGLE_CASE_SENSITIVE, KEYS_ACTION_TOGGLE_WHOLE_WORD,
};
static const keys_action view_items[] = {
    KEYS_ACTION_TOGGLE_SIDEBAR, KEYS_ACTION_TOGGLE_MENU, KEYS_ACTION_CONSOLE,
    KEYS_ACTION_FULL_SCREEN, KEYS_ACTION_DISTRACTION_FREE, KEYS_ACTION_NEW_PANE,
    KEYS_ACTION_CLOSE_PANE, KEYS_ACTION_SPELL_CHECK, KEYS_ACTION_NEXT_MISSPELLING,
    KEYS_ACTION_PREVIOUS_MISSPELLING, KEYS_ACTION_INLINE_DIFF,
};
static const keys_action goto_items[] = {
    KEYS_ACTION_GOTO_FILE, KEYS_ACTION_GOTO_SYMBOL, KEYS_ACTION_GOTO_PROJECT_SYMBOL,
    KEYS_ACTION_GOTO_DEFINITION, KEYS_ACTION_GOTO_REFERENCE, KEYS_ACTION_GOTO_LINE,
    KEYS_ACTION_GOTO_WORD, KEYS_ACTION_JUMP_BACK, KEYS_ACTION_JUMP_FORWARD,
    KEYS_ACTION_NEXT_VIEW, KEYS_ACTION_PREVIOUS_VIEW, KEYS_ACTION_NEXT_VIEW_STACK,
    KEYS_ACTION_PREVIOUS_VIEW_STACK, KEYS_ACTION_GOTO_BRACKET, KEYS_ACTION_NEXT_MODIFICATION,
    KEYS_ACTION_PREVIOUS_MODIFICATION, KEYS_ACTION_SHOW_AT_CENTER, KEYS_ACTION_TOGGLE_BOOKMARK,
    KEYS_ACTION_NEXT_BOOKMARK, KEYS_ACTION_PREVIOUS_BOOKMARK, KEYS_ACTION_CLEAR_BOOKMARKS,
    KEYS_ACTION_SELECT_BOOKMARKS,
};
static const keys_action tools_items[] = {
    KEYS_ACTION_COMMAND_PALETTE, KEYS_ACTION_BUILD, KEYS_ACTION_CANCEL_BUILD,
    KEYS_ACTION_RECORD_MACRO, KEYS_ACTION_RUN_MACRO, KEYS_ACTION_SHOW_SCOPE,
};
static const keys_action preferences_items[] = { KEYS_ACTION_FONT_SIZE };
static const keys_action help_items[] = { KEYS_ACTION_COMMAND_PALETTE };
#define MENU(t, m, items) {t, m, items, sizeof items / sizeof items[0]}
/* Sublime's mnemonics: F_ind and Prefere_nces (P is Sublime's Project). */
static const menu_def menus[] = {
    MENU("File", 'f', file_items), MENU("Edit", 'e', edit_items),
    MENU("Selection", 's', selection_items), MENU("Find", 'i', find_items),
    MENU("View", 'v', view_items), MENU("Goto", 'g', goto_items),
    MENU("Tools", 't', tools_items), MENU("Preferences", 'n', preferences_items),
    MENU("Help", 'h', help_items),
};
_Static_assert(sizeof menus / sizeof menus[0] == MENU_COUNT, "menu table follows MENU_COUNT");

const menu_def *menu_table(void) { return menus; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
int menu_mnemonic(uint32_t keysym)
{
    if (keysym >= 'A' && keysym <= 'Z') keysym += 'a' - 'A';
    if (keysym < 'a' || keysym > 'z') return -1;
    for (size_t m = 0; m < MENU_COUNT; m++) if ((uint32_t)menus[m].mnemonic == keysym) return (int)m;
    return -1;
}
size_t menu_mnemonic_offset(size_t menu)
{
    if (menu >= MENU_COUNT) return SIZE_MAX;
    const char *title = menus[menu].title;
    for (size_t i = 0; title[i]; i++) if (lower(title[i]) == menus[menu].mnemonic) return i;
    return SIZE_MAX;
}
uint32_t menu_title_column(size_t menu)
{
    uint32_t column = 0;
    /* (P) one padding cell either side of each title. */
    for (size_t m = 0; m < menu && m < MENU_COUNT; m++) column += (uint32_t)strlen(menus[m].title) + 2u;
    return column;
}
int menu_label(keys_action action, char *out, size_t capacity)
{
    for (size_t i = 0; i < sizeof action_names / sizeof action_names[0]; i++)
        if (action_names[i].action == action) return palette_label(action_names[i].name, out, capacity);
    if (out && capacity) out[0] = '\0';
    return -1;
}
const keys_binding *menu_command(keys_action action) { return palette_command(action); }
