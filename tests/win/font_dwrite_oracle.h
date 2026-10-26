#ifndef EDIT_TEST_DWRITE_ORACLE_H
#define EDIT_TEST_DWRITE_ORACLE_H
/* Exact oracle for the analytic integer-edge fixture, not a claim that two
 * general-purpose antialiasing filters produce identical arbitrary glyphs. */
static int font_test_artwork_equal(const font_t *f, const font_bitmap *a, const font_bitmap *b)
{
    if (a->w != b->w || a->h != b->h || memcmp(&a->m, &b->m, sizeof a->m) ||
        memcmp(a->pixels, b->pixels, (size_t)a->w * a->h)) return 0;
    /* Independently place expected samples in baseline coordinates. The cell
     * size is the existing UI bitmap cap, sufficient for this small fixture. */
    uint8_t expected[64 * 64] = {0}, actual[sizeof expected] = {0};
    for (uint32_t y = 0; y < a->h; y++) for (uint32_t x = 0; x < a->w; x++) {
        int64_t cx = (int64_t)a->m.bearing_x + x;
        int64_t cy = (int64_t)f->cell.ascent + a->m.bearing_y + y;
        if (cx >= 0 && cx < 64 && cy >= 0 && cy < 64)
            expected[(size_t)cy * 64u + (size_t)cx] = a->pixels[(size_t)y * a->w + x];
    }
    return font_place_in_cell(f, b, actual, 64, 64) == FONT_OK &&
           !memcmp(expected, actual, sizeof expected);
}
#endif
