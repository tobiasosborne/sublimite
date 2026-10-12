#ifndef EDIT_HL_LANG_H
#define EDIT_HL_LANG_H

#include <tree_sitter/api.h>

const TSLanguage *hl_language_c(void);
const TSLanguage *hl_language_rust(void);
const TSLanguage *hl_language_python(void);
const TSLanguage *hl_language_julia(void);

#endif
