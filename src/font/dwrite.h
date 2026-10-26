/* Internal worker raster seam; no COM state escapes a call. */
#ifndef EDIT_FONT_DWRITE_H
#define EDIT_FONT_DWRITE_H
#include "font/font.h"
#if defined(_WIN32)
int font_dwrite_raster(const font_t *f, uint32_t gid, edit_arena *arena,
                        font_bitmap *out, int unavailable);
/* Explicit reference backend for conformance and injected failure checks. */
int font_raster_glyph_stb(font_t *f, uint32_t cp, edit_arena *arena, font_bitmap *out);
#endif
#endif
