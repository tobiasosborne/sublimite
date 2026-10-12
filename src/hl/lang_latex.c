#include "hl/lang.h"

/* Upstream grammar entry point; static language storage, no allocation. */
extern const TSLanguage *tree_sitter_latex(void);

const TSLanguage *hl_language_latex(void)
{
    return tree_sitter_latex();
}
