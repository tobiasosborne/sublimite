#include "raster/raster.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "raster_scroll_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    /* Minimal surface: text and fixed sidebar columns; fixed chrome row and
     * visible text row; bottom overscan row. Two pixels per cell let the last
     * pixel straddle a row boundary under an exact half-pixel origin. */
    render_cell cells[] = {
        {.atlas_slot = RENDER_NO_SLOT, .bg = 0xff0000}, {.atlas_slot = RENDER_NO_SLOT, .bg = 0xff0000},
        {.atlas_slot = RENDER_NO_SLOT, .bg = 0}, {.atlas_slot = RENDER_NO_SLOT, .bg = 0x0000ff},
        {.atlas_slot = RENDER_NO_SLOT, .bg = 0xffffff}, {.atlas_slot = RENDER_NO_SLOT, .bg = 0}
    };
    raster_scene scene = {.dims = {2, 2, 1, 2}, .cells = cells};
    render_scroll plan = {.origin_y_q8 = -128, .first_row = 1, .rows = 1, .cols = 1};
    uint32_t pixels[4] = {0}; raster_palette palette = {0};
    CHECK(raster_row_scrolled(&scene, &plan, pixels, 2, 1, &palette, NULL, NULL));
    CHECK(pixels[0] == 0 && pixels[1] == 0x0000ff);
    CHECK(pixels[2] == 0x808080 && pixels[3] == 0x0000ff); /* (0*128+255*128+128)/256 per channel */
    uint32_t surface[8] = {0}, ink[] = {0x112233, 0x445566};
    render_pixels region = {.pixels = ink, .capacity = 2, .stride = 1,
        .x = scene.dims.cell_w, .y = scene.dims.cell_h, .width = 1, .height = 2};
    CHECK(raster_row_scrolled(&scene, &plan, surface, 2, 0, &palette, NULL, NULL));
    CHECK(raster_row_scrolled(&scene, &plan, surface + 4, 2, 1, &palette, NULL, NULL));
    raster_blit_pixels(&region, surface, 2, 0, region.height, 0);
    CHECK(surface[4] == 0 && surface[6] == 0x808080);
    CHECK(surface[5] == ink[0] && surface[7] == ink[1]);
    CHECK(surface[0] == 0xff0000 && surface[1] == 0xff0000);
    CHECK(raster_row_scrolled(&scene, &plan, pixels, 2, 0, &palette, NULL, NULL));
    for (size_t i = 0; i < sizeof pixels / sizeof pixels[0]; i++) CHECK(pixels[i] == 0xff0000);
    puts("raster_scroll_test: exact Q8 translated pixels, bottom overscan, fixed chrome/sidebar pixel region PASS");
    return 0;
}
