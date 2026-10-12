#ifndef EDIT_HL_LANG_H
#define EDIT_HL_LANG_H

#include <tree_sitter/api.h>

const TSLanguage *hl_language_c(void);
const TSLanguage *hl_language_rust(void);
const TSLanguage *hl_language_python(void);
const TSLanguage *hl_language_julia(void);
const TSLanguage *hl_language_javascript(void);
const TSLanguage *hl_language_typescript(void);
const TSLanguage *hl_language_json(void);
const TSLanguage *hl_language_toml(void);
const TSLanguage *hl_language_yaml(void);
const TSLanguage *hl_language_markdown(void);
const TSLanguage *hl_language_markdown_inline(void);
const TSLanguage *hl_language_latex(void);
const TSLanguage *hl_language_bash(void);

#endif
