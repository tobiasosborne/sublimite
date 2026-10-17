#ifndef EDIT_PALETTE_H
#define EDIT_PALETTE_H
#include "config/config.h"

/* (P) actions.inc contains every intent from LEFT through the enum's end;
 * palette.c statically checks that count against the actual table. NONE and
 * CHORD are input routing sentinels. CONFIG_NAME_CAP already fits the longest
 * action name with its keymap prefix, and every expanded label below. */
enum { PALETTE_ACTION_COUNT = KEYS_ACTION_COUNT - KEYS_ACTION_LEFT };
#define PALETTE_LABEL_CAP CONFIG_NAME_CAP
typedef struct palette_row {
    char label[PALETTE_LABEL_CAP];
    const keys_binding *binding; /* immutable default command, never sorted */
} palette_row;
typedef struct palette_state {
    palette_row rows[PALETTE_ACTION_COUNT];
    size_t matches[PALETTE_ACTION_COUNT], scores[PALETTE_ACTION_COUNT];
    size_t count, selected, first, query_length;
    char query[PALETTE_LABEL_CAP]; /* (P) a longer query cannot match any label */
} palette_state;
/* No allocation, locale, I/O or mutable global state. Return zero on success;
 * label/filter capacity or argument refusals return nonzero. */
int palette_label(const char *name, char *out, size_t capacity);
bool palette_match(const char *label, const char *query, size_t length, size_t *score);
void palette_init(palette_state *);
int palette_filter(palette_state *, const char *query, size_t length);
#endif
