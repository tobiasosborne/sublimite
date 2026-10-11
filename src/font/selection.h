#ifndef EDIT_FONT_SELECTION_H
#define EDIT_FONT_SELECTION_H
#include "font/font.h"
/* Printable ASCII count is last - first + inclusive endpoint. */
#define FONT_SELECTION_GLYPHS (0x7eu - 0x20u + 1u)
typedef struct font_selection {
    char family[FONT_FALLBACK_PATH_MAX], file[FONT_FALLBACK_PATH_MAX];
    uint32_t index;
    font_t face;
    edit_arena files;
    int result;
    uint64_t atlas_raster_ns, glyph_max_ns; /* worker diagnostics */
} font_selection;
/* UI-only copied request. file wins; optional trailing #decimal selects TTC
 * face. Empty strings restore the embedded face. No allocation or I/O.
 * Reuse only after previous physical completion/drain AND backend retirement. */
int font_selection_parse(font_selection *s, const char *family, const char *file);
/* Worker-only; missing exact family returns MISSING (fontconfig substitution
 * must not silently select a different installed family). */
int font_selection_resolve(const char *pattern, char *file, size_t cap, uint32_t *index);
int font_monospace(const font_t *face);
/* Worker-only load and admission. No atlas owner: the editor's size lifecycle
 * builds ASCII and lends this primary to the async Unicode owner. */
int font_selection_load(font_selection *s, uint32_t px, bool em,
                        font_cell max_cell, work_ctx *ctx);
/* Quiescent shutdown or exclusive worker reuse only. */
void font_selection_free(font_selection *s);
#endif
