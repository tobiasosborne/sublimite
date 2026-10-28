#ifndef EDIT_EDITOR_THEME_H
#define EDIT_EDITOR_THEME_H
#include <stdint.h>

/* Content shares the text edge. With a map, leave one cell before its first
 * column; without one the reserved outer margin supplies that cell. */
static inline findui_bounds editor_panel_bounds(const editor *e)
{
    uint32_t end = e->text_grid.dims.cols - (e->map_cols && e->text_grid.dims.cols ? 1u : 0u);
    return (findui_bounds){e->lay.gw < end ? e->lay.gw : end, end};
}

/* Estimated gutter numbers use half the configured foreground contrast.
 * Blend channels separately so carries cannot change neighbouring channels. */
static inline uint32_t editor_theme_estimated_gutter(uint32_t fg, uint32_t bg)
{
    uint32_t rb = ((fg & UINT32_C(0xff00ff)) + (bg & UINT32_C(0xff00ff))) >> 1;
    uint32_t g = ((fg & UINT32_C(0x00ff00)) + (bg & UINT32_C(0x00ff00))) >> 1;
    return (rb & UINT32_C(0xff00ff)) | (g & UINT32_C(0x00ff00));
}
#endif
