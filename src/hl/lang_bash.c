#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_bash(void);

const TSLanguage *hl_language_bash(void)
{
    return tree_sitter_bash();
}
