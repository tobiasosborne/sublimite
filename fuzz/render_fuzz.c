/* Pure dirty-row model, arbitrary dims/patterns/full flag/output capacity.
 * No external input interpretation, no allocation. */
#include "render/render.h"
#include "base/base.h"
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 5) return 0;
    uint32_t rows = 1u + ((uint32_t)data[0] | ((uint32_t)data[1] << 8)) % 257u;
    uint32_t cols = 1u + (uint32_t)data[2] % 8u;
    render_cell cells[257 * 8]; uint64_t dirty[5] = {0};
    render_grid grid;
    EDIT_ASSERT(render_grid_init(&grid, (render_dims){cols,rows,1,1}, cells,
        sizeof cells / sizeof cells[0], dirty, 5) == RENDER_OK);
    EDIT_ASSERT(render_frame_begin(&grid, 1) == RENDER_OK);
    bool model[257];
    for (uint32_t row = 0; row < rows; row++) {
        uint8_t value = data[4 + ((size_t)row % (size - 4))];
        model[row] = (value & 1u) != 0;
        if (model[row]) EDIT_ASSERT(render_mark_rows(&grid, row, 1) == RENDER_OK);
    }
    if (data[3] & 1u) {
        EDIT_ASSERT(render_mark_full(&grid) == RENDER_OK);
        for (uint32_t row = 0; row < rows; row++) model[row] = true;
    }
    /* Dirty-word padding deliberately arbitrary, never leaks extra rows. */
    if (rows % 64u) dirty[(rows - 1u) / 64u] |= UINT64_MAX << (rows % 64u);
    render_strip expected[129]; size_t needed = 0;
    for (uint32_t row = 0; row < rows; row++) {
        if (!model[row]) continue;
        if (row != 0 && model[row - 1u]) expected[needed - 1].row_count++;
        else expected[needed++] = (render_strip){row,1};
    }
    render_strip actual[129], original[129];
    memset(actual, 0x5a, sizeof actual); memcpy(original, actual, sizeof actual);
    size_t cap = (size_t)data[4] % 130;
    size_t n = SIZE_MAX;
    int rc = render_dirty_strips(&grid, actual, cap, &n);
    EDIT_ASSERT(n == needed);
    EDIT_ASSERT(rc == (cap < needed ? RENDER_ERR_CAPACITY : RENDER_OK));
    if (cap < needed) EDIT_ASSERT(memcmp(actual, original, sizeof actual) == 0);
    else {
        EDIT_ASSERT(memcmp(actual, expected, needed * sizeof actual[0]) == 0);
        EDIT_ASSERT(memcmp(actual + needed, original + needed, (129 - needed) * sizeof actual[0]) == 0);
    }
    n = SIZE_MAX;
    EDIT_ASSERT(render_dirty_strips(&grid, actual, 129, &n) == RENDER_OK && n == needed);
    EDIT_ASSERT(memcmp(actual, expected, needed * sizeof actual[0]) == 0);
    uint64_t before[5]; memcpy(before, dirty, sizeof dirty);
    EDIT_ASSERT(render_mark_rows(&grid, rows, 1) == RENDER_ERR_BOUNDS);
    EDIT_ASSERT(memcmp(before, dirty, sizeof dirty) == 0);
    return 0;
}
