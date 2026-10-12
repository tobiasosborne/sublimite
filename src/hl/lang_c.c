#include "hl/lang.h"
const TSLanguage *tree_sitter_c(void);
const TSLanguage *hl_language_c(void) { return tree_sitter_c(); }
