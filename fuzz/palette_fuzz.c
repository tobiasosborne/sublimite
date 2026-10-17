#include "palette/palette.h"
#include "base/base.h"
#include <string.h>

/* (P) query size follows the longest label; fuzz bytes are explicit lengths,
 * so embedded NUL/non-ASCII cannot escape the bound or match ASCII labels. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    palette_state p; palette_init(&p);
    size_t length = size < sizeof p.query ? size : sizeof p.query - 1u;
    char name[PALETTE_LABEL_CAP], label[PALETTE_LABEL_CAP];
    if (length) memcpy(name, data, length);
    name[length] = '\0';
    if (!palette_label(name, label, sizeof label)) EDIT_ASSERT(strlen(label) < sizeof label);
    EDIT_ASSERT(!palette_filter(&p, (const char *)data, length));
    EDIT_ASSERT(p.count <= PALETTE_ACTION_COUNT);
    size_t previous = 0;
    for (size_t i = 0; i < p.count; i++) {
        EDIT_ASSERT(p.matches[i] < PALETTE_ACTION_COUNT);
        const palette_row *row = &p.rows[p.matches[i]];
        size_t score;
        EDIT_ASSERT(row->binding && strlen(row->label) < sizeof row->label);
        EDIT_ASSERT(palette_match(row->label, (const char *)data, length, &score));
        EDIT_ASSERT(!i || score >= previous);
        EDIT_ASSERT(!i || score != previous || p.matches[i - 1u] < p.matches[i]);
        previous = score;
    }
    return 0;
}
