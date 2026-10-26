#include "palette/palette.h"
#include "base/base.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <stdio.h>
#include <string.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "palette_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
/* (P) capacities come from the action table/longest config action name;
 * zero is absence/first index, one advances an index or removes a NUL. */
static int labels_and_filter(void)
{
    char label[PALETTE_LABEL_CAP]; palette_state p;
    T(palette_label("save_as", label, sizeof label) == 0);
    T(!strcmp(label, "Save As"));
    T(palette_label("doc_home", label, sizeof label) == 0);
    T(!strcmp(label, "Document Home"));
    T(palette_label("toggle_regex", label, sizeof label) == 0);
    T(!strcmp(label, "Toggle Regex"));
    T(palette_label("save_as", label, sizeof "Save As" - 1u) != 0);
    palette_init(&p);
    T(p.count <= PALETTE_ACTION_COUNT && p.count != 0);
    T(palette_filter(&p, "SaV", sizeof "SaV" - 1u) == 0);
    T(p.count == sizeof "ab" - 1u); /* (P) Save and Save As in actions.inc */
    T(p.rows[p.matches[0]].binding->action == KEYS_ACTION_SAVE);
    T(p.rows[p.matches[1]].binding->action == KEYS_ACTION_SAVE_AS);
    size_t score, loose;
    T(palette_match("Save As", "svas", sizeof "svas" - 1u, &score));
    T(palette_match("Save As", "sa", sizeof "sa" - 1u, &score));
    T(palette_match("Select All", "sa", sizeof "sa" - 1u, &loose));
    T(score < loose);
    T(palette_match("s---sa", "sa", sizeof "sa" - 1u, &score));
    T(score == 0); /* (P) tightest span is the final contiguous sa */
    T(!palette_match("Save", "vs", sizeof "vs" - 1u, &score));
    T(palette_filter(&p, "sa", sizeof "sa" - 1u) == 0);
    size_t previous = 0;
    for (size_t i = 0; i < p.count; i++) {
        const palette_row *row = &p.rows[p.matches[i]];
        T(palette_match(row->label, "sa", sizeof "sa" - 1u, &score));
        T(!i || score >= previous);
        if (i && score == previous) T(p.matches[i - 1u] < p.matches[i]);
        previous = score;
    }
    T(palette_filter(&p, "", 0) == 0);
    for (size_t i = 0; i < p.count; i++) {
        keys_action action = p.rows[p.matches[i]].binding->action;
        T(action != KEYS_ACTION_INSERT && action != KEYS_ACTION_SELECT_TAB &&
          action != KEYS_ACTION_SCROLL_LINES && action != KEYS_ACTION_FOLD_LEVEL);
    }
    return 0;
}
static int chords(void)
{
    char text[KEYS_CHORD_FORMAT_CAP]; size_t count;
    const keys_binding *table = keys_defaults(&count), *save = NULL, *upper = NULL;
    for (size_t i = 0; i < count; i++) {
        T(keys_chord_format(&table[i], text, sizeof text) == 0 && text[0]);
        if (table[i].action == KEYS_ACTION_SAVE && table[i].keysym == XKB_KEY_s) save = &table[i];
        if (table[i].action == KEYS_ACTION_UPPER_CASE) upper = &table[i];
    }
    T(save && upper);
    T(keys_chord_format(NULL, text, sizeof text) != 0);
    T(keys_chord_format(save, text, sizeof text) == 0 && !strcmp(text, "Ctrl+S"));
    keys_binding plus = {.modifiers = PLAT_MOD_CTRL, .keysym = XKB_KEY_plus};
    T(keys_chord_format(&plus, text, sizeof text) == 0 && !strcmp(text, "Ctrl++"));
    T(keys_chord_format(upper, text, sizeof text) == 0 && !strcmp(text, "Ctrl+K Ctrl+U"));
    T(keys_chord_format(save, text, sizeof "Ctrl+S" - 1u) != 0);
    config_settings settings; config_error error;
    const char config[] = "[keymap]\nsave='Alt+s'\nsave_as='Ctrl+Shift+w'\n";
    T(config_parse((const uint8_t *)config, sizeof config - 1u, &settings, &error) == CONFIG_OK);
    const keys_binding *live = keys_binding_for_command(save, settings.bindings, settings.binding_count);
    T(live && keys_chord_format(live, text, sizeof text) == 0 && !strcmp(text, "Alt+S"));
    keys_binding shadow = {.keysym = XKB_KEY_k, .modifiers = PLAT_MOD_CTRL, .action = KEYS_ACTION_SAVE};
    T(!keys_binding_for_command(upper, &shadow, sizeof shadow / sizeof shadow));
    edit_malloc_guard_begin();
    palette_state p; palette_init(&p);
    T(palette_filter(&p, "sav", sizeof "sav" - 1u) == 0);
    T(keys_chord_format(save, text, sizeof text) == 0);
    T(keys_binding_for_command(save, settings.bindings, settings.binding_count));
    T(edit_malloc_guard_end() == 0); /* (P) typing-path allocation law */
    return 0;
}
int main(void)
{
    T(labels_and_filter() == 0); T(chords() == 0);
    puts("palette_test: labels, tightness/order, exclusions, live chords and allocation guard passed");
    return 0;
}
