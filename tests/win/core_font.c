#if defined(_WIN32)
/* The layout fixture uses the production baked atlases, without linking the
 * font runtime (whose discovery/raster platform port belongs to W1/W2). */
#include "font/font.h"
#include "font/atlas_ascii.h"
#include "font/atlas_ascii_15.h"
static const font_ascii_atlas atlases[2] = {
    {15u, {FONT_ATLAS_CELL_W_15, FONT_ATLAS_CELL_H_15, FONT_ATLAS_ASCENT_15,
            FONT_ATLAS_CELL_H_15 - FONT_ATLAS_ASCENT_15},
        font_atlas_metrics_15, font_atlas_pixels_15, sizeof font_atlas_pixels_15},
    {30u, {FONT_ATLAS_CELL_W, FONT_ATLAS_CELL_H, FONT_ATLAS_ASCENT,
            FONT_ATLAS_CELL_H - FONT_ATLAS_ASCENT},
        font_atlas_metrics, font_atlas_pixels, sizeof font_atlas_pixels}
};
const font_ascii_atlas *font_ascii_atlas_for_px(uint32_t px)
{
    for (size_t i = 0; i < sizeof atlases / sizeof atlases[0]; i++)
        if (atlases[i].px == px) return &atlases[i];
    return NULL;
}
#endif
