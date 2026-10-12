#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_markdown_inline(void);

const TSLanguage *hl_language_markdown_inline(void)
{
    return tree_sitter_markdown_inline();
}
