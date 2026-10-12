#include "hl/lang.h"
#include <stddef.h>
#include <string.h>
/* Optional archive members are extracted only by HL_OPTIN's explicit link
 * or another strong client. GNU weak references are supported by both project
 * compilers; querying an absent grammar allocates nothing. */
extern const TSLanguage *hl_language_julia(void) __attribute__((weak));
extern const TSLanguage *hl_language_typescript(void) __attribute__((weak));
extern const TSLanguage *hl_language_markdown(void) __attribute__((weak));
extern const TSLanguage *hl_language_markdown_inline(void) __attribute__((weak));
extern const TSLanguage *hl_language_latex(void) __attribute__((weak));
extern const TSLanguage *hl_language_bash(void) __attribute__((weak));
#define LANGUAGE(name) {#name, hl_language_##name}
typedef const TSLanguage *(*language_fn)(void);
static const struct { const char *name; language_fn fn; } languages[] = {
    LANGUAGE(c), LANGUAGE(rust), LANGUAGE(python), LANGUAGE(javascript),
    LANGUAGE(json), LANGUAGE(toml), LANGUAGE(yaml), LANGUAGE(julia),
    LANGUAGE(typescript), LANGUAGE(markdown), LANGUAGE(markdown_inline),
    LANGUAGE(latex), LANGUAGE(bash)
};
const char *hl_language_name(const TSLanguage *language)
{
    if (!language) return NULL;
    for (size_t i=0; i<sizeof languages/sizeof languages[0]; i++)
        if (languages[i].fn && languages[i].fn()==language) return languages[i].name;
    return NULL;
}
const TSLanguage *hl_language_path(const char *path)
{
    if (!path) return NULL;
    const char *extension = strrchr(path,'.');
    if (!extension || strchr(extension,'/')) return NULL;
    /* (P) Extension aliases identify the vendored grammar, no content scan. */
    static const struct { const char *extension, *language; } aliases[] = {
        {".c","c"}, {".h","c"}, {".rs","rust"}, {".py","python"},
        {".js","javascript"}, {".mjs","javascript"}, {".cjs","javascript"},
        {".jsx","javascript"}, {".json","json"}, {".toml","toml"},
        {".yaml","yaml"}, {".yml","yaml"}, {".jl","julia"},
        {".ts","typescript"}, {".md","markdown"}, {".markdown","markdown"},
        {".tex","latex"}, {".sh","bash"}, {".bash","bash"}
    };
    for (size_t i=0; i<sizeof aliases/sizeof aliases[0]; i++) if (!strcmp(extension,aliases[i].extension))
        for (size_t j=0; j<sizeof languages/sizeof languages[0]; j++)
            if (!strcmp(aliases[i].language,languages[j].name))
                return languages[j].fn ? languages[j].fn() :
                    (!strcmp(languages[j].name,"markdown") && hl_language_markdown_inline ? hl_language_markdown_inline() : NULL);
    return NULL;
}
