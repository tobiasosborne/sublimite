#include "editor/runtime.h"
#include "base/base.h"
#include <math.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    double xft = editor_runtime_xft_dpi((const char *)data, size);
    EDIT_ASSERT(isfinite(xft) && xft >= 0);
    uint32_t dimensions[4] = {0}; /* width/height in pixels, width/height in mm */
    size_t take = size < sizeof dimensions ? size : sizeof dimensions;
    memcpy(dimensions, data, take);
    double dpi = editor_runtime_dpi(xft, dimensions[0], dimensions[1], dimensions[2], dimensions[3]);
    EDIT_ASSERT(isfinite(dpi) && dpi > 0);
    uint32_t em = editor_runtime_em_px(dpi, EDITOR_FONT_DEFAULT_PT);
    EDIT_ASSERT(em >= 1u && em <= EDITOR_FONT_MAX_PX); /* positive whole em to prepaid atlas bound */
    uint32_t width, height;
    editor_runtime_window(dimensions[0], dimensions[1], &width, &height);
    EDIT_ASSERT(width <= dimensions[0] && height <= dimensions[1]);
    double arbitrary = 0;
    if (size >= sizeof arbitrary) memcpy(&arbitrary, data, sizeof arbitrary);
    em = editor_runtime_em_px(dpi, arbitrary);
    EDIT_ASSERT(!em || (em >= 1u && em <= EDITOR_FONT_MAX_PX));
    return 0;
}
