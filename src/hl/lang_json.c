#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_json(void);

const TSLanguage *hl_language_json(void)
{
    return tree_sitter_json();
}
