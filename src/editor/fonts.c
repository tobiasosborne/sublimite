#include "editor/private.h"

void editor_font_track(editor *e, uint32_t slot, uint32_t row)
{
    if (slot == RENDER_NO_SLOT || row >= e->grid.dims.rows) return;
    e->font_rows[(size_t)slot * e->font_row_words + row / 64u] |= UINT64_C(1) << (row % 64u);
}

int editor_font_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot)
{
    editor *e = ctx;
    int rc = font_async_glyph(e->fonts, bytes, len, width, slot);
    (void)font_async_bind(e->fonts, &e->text_grid);
    (void)font_async_bind(e->fonts, &e->grid);
    if (rc == FONT_OK && e->lay.row < e->text_grid.dims.rows)
        editor_font_track(e, *slot, e->lay.row + e->tab_rows);
    return rc;
}

bool editor_font_event(editor *e, const work_msg *msg)
{
    if (msg->kind != FONT_ASYNC_MSG_KIND) return false;
    uint32_t slot = font_async_event(e->fonts, msg);
    if (slot == RENDER_NO_SLOT) return true;
    (void)font_async_bind(e->fonts, &e->text_grid);
    (void)font_async_bind(e->fonts, &e->grid);
    /* Pixel-only changes need damage even when cell metadata is unchanged.
     * Slots are stable across adoption, so this touches only visible users of
     * the completed image, without restarting layout or delaying queued input. */
    render_grid *grids[] = {&e->grid, &e->text_grid};
    bool begun = false;
    for (size_t i = 0; i < sizeof grids / sizeof grids[0]; i++) {
        render_grid *g = grids[i];
        for (uint32_t row = 0; row < g->dims.rows; row++) {
            uint32_t full_row = i ? row + e->tab_rows : row;
            uint64_t mask = UINT64_C(1) << (full_row % 64u);
            if (!(e->font_rows[(size_t)slot * e->font_row_words + full_row / 64u] & mask)) continue;
            bool changed = false;
            for (uint32_t col = 0; col < g->dims.cols; col++) {
                render_cell *cell = &g->cells[(size_t)row * g->dims.cols + col];
                if (cell->atlas_slot != slot) continue;
                if (!begun) {
                    int rc = editor_begin_frame(e);
                    if (rc) { e->error = rc; return true; }
                    begun = true;
                }
                if (!g->glyphs[slot].w) { cell->atlas_slot = RENDER_NO_SLOT; cell->glyph_index = 0; }
                changed = true;
            }
            if (changed) {
                int rc = render_mark_rows(g, row, 1);
                if (rc) e->error = rc;
            }
        }
    }
    for (size_t word = 0; word < e->font_row_words; word++)
        e->font_rows[(size_t)slot * e->font_row_words + word] = 0;
    return true;
}
