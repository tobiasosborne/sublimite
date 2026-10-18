#ifndef EDIT_MENU_H
#define EDIT_MENU_H
#include "keys/keys.h"

/* edit-yey: Sublime-style menu bar data. Static, immutable tables; every
 * item is an action from config/actions.inc dispatched as the palette's
 * default command (palette_command). No allocation, I/O or mutable state.
 * (P) the bead's title list: File Edit Selection Find View Goto Tools
 * Preferences Help. */
enum { MENU_COUNT = 9 };
typedef struct menu_def {
    const char *title;
    char mnemonic;            /* lowercase ASCII letter of title, unique */
    const keys_action *items;
    size_t count;
} menu_def;
const menu_def *menu_table(void); /* MENU_COUNT entries, display order */
/* Menu index for an ASCII letter keysym of either case, else -1. */
int menu_mnemonic(uint32_t keysym);
/* Offset of the mnemonic letter inside its title (underlined cell). */
size_t menu_mnemonic_offset(size_t menu);
/* First cell of a title's slot on the bar: each slot is one padding cell,
 * the title, one padding cell, laid out left to right from column zero. */
uint32_t menu_title_column(size_t menu);
/* The palette's label for an action; nonzero on capacity/unknown action. */
int menu_label(keys_action, char *, size_t);
/* The default-argument command the item dispatches (palette_command). */
const keys_binding *menu_command(keys_action);
#endif
