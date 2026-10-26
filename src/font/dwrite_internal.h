#ifndef EDIT_FONT_DWRITE_INTERNAL_H
#define EDIT_FONT_DWRITE_INTERNAL_H
#if defined(_WIN32)
#define CONST_VTABLE
#include <windows.h>
#include <dwrite_2.h>
typedef struct font_dwrite {
    HMODULE library;
    IDWriteFactory2 *factory;
    int uninitialize;
} font_dwrite;
int font_dwrite_open(font_dwrite *dw, const char *library);
void font_dwrite_close(font_dwrite *dw);
int font_dwrite_path(IDWriteFont *font, char *path, size_t cap, uint32_t *index);
#endif
#endif
