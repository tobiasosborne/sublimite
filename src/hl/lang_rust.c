#include "hl/lang.h"
const TSLanguage *tree_sitter_rust(void);
const TSLanguage *hl_language_rust(void) { return tree_sitter_rust(); }
