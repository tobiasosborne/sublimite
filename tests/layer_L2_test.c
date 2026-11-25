/* check-layers L2: linked --whole-archive against lower layers only. */
#include <stdio.h>

#include "journal/journal.h"

int main(void)
{
    size_t want = JOURNAL_PAGE_BYTES - JOURNAL_PREAMBLE_BYTES;
    size_t got = journal_pad_length(0);
    if (got != want) {
        fprintf(stderr, "layer_L2_test: journal_pad_length(0) returned %zu, want %zu\n", got, want);
        return 1;
    }
    return 0;
}
