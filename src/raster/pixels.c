#include "raster/raster.h"

void raster_blit_pixels(const render_pixels *region, uint32_t *dst,
                        size_t stride, uint32_t first, uint32_t end,
                        uint32_t alpha_or)
{
    if (!region->pixels) return;
    for (uint32_t y = first; y < end; y++) {
        const uint32_t *src = region->pixels + (size_t)y * region->stride;
        uint32_t *row = dst + (size_t)(region->y + y) * stride + region->x;
        for (uint32_t x = 0; x < region->width; x++) row[x] = src[x] | alpha_or;
    }
}
