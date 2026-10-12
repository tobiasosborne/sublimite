#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_javascript(void);

const TSLanguage *hl_language_javascript(void)
{
    return tree_sitter_javascript();
}
