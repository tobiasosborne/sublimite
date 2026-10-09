#include "journal/journal.h"
#include <stdlib.h>
#include <string.h>

int journal_default_dir(char *out, size_t cap)
{
    if (!out || !cap) return JOURNAL_INVALID;
    const char *root = getenv("XDG_DATA_HOME"), *suffix = "/sublimite";
    if (!root || root[0] != '/') {
        root = getenv("HOME"); suffix = "/.local/share/sublimite";
    }
    if (!root || root[0] != '/') return JOURNAL_INVALID;
    size_t len = strlen(root);
    while (len && root[len - 1] == '/') len--;
    if (len >= cap || strlen(suffix) >= cap - len) return JOURNAL_INVALID;
    memcpy(out, root, len); strcpy(out + len, suffix); return JOURNAL_OK;
}
