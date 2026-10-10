#ifndef EDIT_EDITOR_THEME_H
#define EDIT_EDITOR_THEME_H
#include <stdint.h>

/* Estimated gutter numbers use half the configured foreground contrast.
 * Blend channels separately so carries cannot change neighbouring channels. */
static inline uint32_t editor_theme_estimated_gutter(uint32_t fg, uint32_t bg)
{
    uint32_t rb = ((fg & UINT32_C(0xff00ff)) + (bg & UINT32_C(0xff00ff))) >> 1;
    uint32_t g = ((fg & UINT32_C(0x00ff00)) + (bg & UINT32_C(0x00ff00))) >> 1;
    return (rb & UINT32_C(0xff00ff)) | (g & UINT32_C(0x00ff00));
}
#endif
