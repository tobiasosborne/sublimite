#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_markdown(void);

const TSLanguage *hl_language_markdown(void)
{
    return tree_sitter_markdown();
}
