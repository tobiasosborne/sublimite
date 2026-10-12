#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_toml(void);

const TSLanguage *hl_language_toml(void)
{
    return tree_sitter_toml();
}
