#include "palette/palette.h"
#include <string.h>
#include "config/actions.inc"

_Static_assert(PALETTE_ACTION_COUNT == sizeof action_names / sizeof action_names[0],
    "palette storage follows the action table");
_Static_assert(sizeof "Delete To Beginning Of Line" <= PALETTE_LABEL_CAP,
    "longest expanded label fits the config name capacity");
/* (P) spelling expansions only; other labels capitalize underscore-separated
 * words. These names fit the existing config action-name storage bound. */
static const struct { const char *name, *label; } labels[] = {
    {"doc_home", "Document Home"}, {"doc_end", "Document End"},
    {"delete_to_bol", "Delete To Beginning Of Line"},
    {"delete_to_eol", "Delete To End Of Line"},
    {"xml_tag_snippet", "XML Tag Snippet"}
};
static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}
int palette_label(const char *name, char *out, size_t capacity)
{
    if (!name || !out || !capacity) return -1;
    const char *expanded = NULL;
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++)
        if (!strcmp(name, labels[i].name)) { expanded = labels[i].label; break; }
    const char *source = expanded ? expanded : name;
    size_t n = strlen(source);
    if (n >= capacity) { out[0] = '\0'; return -1; }
    bool word = true;
    for (size_t i = 0; i < n; i++) {
        char c = source[i];
        if (!expanded && c == '_') { out[i] = ' '; word = true; }
        else {
            out[i] = !expanded && word && c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
            word = false;
        }
    }
    out[n] = '\0'; return 0;
}
bool palette_match(const char *label, const char *query, size_t length, size_t *score)
{
    if (!label || (!query && length) || !score) return false;
    if (!length) { *score = 0; return true; }
    size_t best = SIZE_MAX;
    /* (P) for each possible first match, greedy following matches minimize
     * its end. Minimum span minus query length counts only unmatched gaps. */
    for (size_t start = 0; label[start]; start++) {
        if (lower((unsigned char)label[start]) != lower((unsigned char)query[0])) continue;
        size_t q = 0, end = start;
        while (label[end] && q < length) {
            if (lower((unsigned char)label[end]) == lower((unsigned char)query[q])) q++;
            end++;
        }
        if (q == length && end - start - length < best) best = end - start - length;
    }
    if (best == SIZE_MAX) return false;
    *score = best; return true;
}
static bool default_arguments(const keys_binding *binding)
{
    /* Numeric zero can still be a required payload (tab/group indices).
     * Other nonzero values/behavior flags are chord variants, not defaults.
     * FONT_SIZE's zero is accepted by its dispatcher as reset. */
    switch (binding->action) {
    case KEYS_ACTION_INSERT: case KEYS_ACTION_SELECT_TAB:
    case KEYS_ACTION_FOCUS_GROUP: case KEYS_ACTION_MOVE_TO_GROUP:
    case KEYS_ACTION_SET_LAYOUT: case KEYS_ACTION_FOLD_LEVEL:
    case KEYS_ACTION_COLUMN_SELECTION: case KEYS_ACTION_SCROLL_LINES:
    case KEYS_ACTION_FOCUS_NEIGHBOR_GROUP: case KEYS_ACTION_MOVE_NEIGHBOR_GROUP:
        return false;
    default: break;
    }
    return !binding->args.value &&
        !(binding->args.flags & (uint16_t)~(KEYS_ARG_UNIMPLEMENTED | KEYS_ARG_SINGLE_SELECTION));
}
const keys_binding *palette_command(keys_action action)
{
    size_t count; const keys_binding *table = keys_defaults(&count);
    for (size_t j = 0; j < count; j++)
        if (table[j].action == action && default_arguments(&table[j])) return &table[j];
    table = keys_unbound(&count); /* edit-yey: strokeless commands */
    for (size_t j = 0; j < count; j++) if (table[j].action == action) return &table[j];
    return NULL;
}
void palette_init(palette_state *p)
{
    if (!p) return;
    memset(p, 0, sizeof *p);
    for (size_t i = 0; i < PALETTE_ACTION_COUNT; i++) {
        if (palette_label(action_names[i].name, p->rows[i].label, sizeof p->rows[i].label)) continue;
        p->rows[i].binding = palette_command(action_names[i].action);
    }
    (void)palette_filter(p, "", 0);
}
int palette_filter(palette_state *p, const char *query, size_t length)
{
    if (!p || (!query && length) || length >= sizeof p->query) return -1;
    if (length) memmove(p->query, query, length);
    p->query[length] = '\0'; p->query_length = length;
    p->count = p->selected = p->first = 0;
    for (size_t i = 0; i < PALETTE_ACTION_COUNT; i++) {
        size_t score;
        if (!p->rows[i].binding || !palette_match(p->rows[i].label, p->query, length, &score)) continue;
        size_t at = p->count;
        while (at && p->scores[at - 1u] > score) {
            p->scores[at] = p->scores[at - 1u]; p->matches[at] = p->matches[at - 1u]; at--;
        }
        p->scores[at] = score; p->matches[at] = i; p->count++;
    }
    return 0;
}
