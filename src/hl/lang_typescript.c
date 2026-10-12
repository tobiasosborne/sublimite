#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_typescript(void);

const TSLanguage *hl_language_typescript(void)
{
    return tree_sitter_typescript();
}
