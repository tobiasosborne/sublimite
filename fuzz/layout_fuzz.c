/* libFuzzer target for src/layout (P3.1): input = [viewport/config header][file bytes].
 * Properties: layout never reads out of bounds, always completes (sliced or
 * one-shot, identical grids), and the grid passes render_grid_validate; a
 * random newline-free or newline edit followed by layout_edit matches a fresh
 * full layout. */
#include "layout/layout.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int glyph_cb(void *ctx, const uint8_t *c, size_t n, uint32_t w, uint32_t *slot)
{
    (void)ctx; (void)c; (void)n; *slot = w == 2 ? 95u : 31u;
    return (c[0] & 0x1f) == 7;               /* sometimes fail: placeholder path */
}

static layout lay[2];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8) return 0;
    uint32_t cols = 1u + data[0] % 40u, rows = 1u + data[1] % 12u, tab = data[2] % 9u;
    uint32_t hs = (uint32_t)data[3] * (data[4] & 1u ? 1u : 3000u);
    bool gutter = data[5] & 1u;
    uint32_t slice = (data[5] >> 1) % 20u;
    uint64_t pick = data[6], ins = data[7];
    const uint8_t *text = data + 8; size_t len = size - 8;
    const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
    render_glyph glyphs[96];
    layout_ascii_glyphs(a, glyphs);
    glyphs[95] = (render_glyph){0x3f, 0, 0, 0, a->cell.cell_w, a->cell.cell_h};
    glyphs[95].glyph_index = glyphs['?' - 0x20].glyph_index;
    /* slot 31 is '?' itself; 95 duplicates it as the "wide" image */
    render_atlas_page page = {a->pixels, a->pixels_len, (size_t)a->cell.cell_w * 95, a->cell.cell_w * 95, a->cell.cell_h};
    piece_allocator al = piece_default_allocator();
    piece_tree *t = piece_create(&al);
    if (!t || piece_init_copy(t, text, len) != PIECE_OK) { if (t) piece_destroy(t); return 0; }
    size_t n = (size_t)cols * rows;
    render_cell *cells[2];
    uint64_t bits[2][1], rb[2][12];
    uint32_t ru[2][12];
    render_grid g[2];
    layout_config cfg = {0};
    cfg.tab_width = tab; cfg.gutter = gutter; cfg.fg = 0xffffff; cfg.bg = 0; cfg.gutter_fg = 0x808080;
    cfg.gutter_bg = 0x101010; cfg.cursor_fg = 1; cfg.cursor_bg = 2; cfg.sel_fg = 3; cfg.sel_bg = 4;
    cfg.glyph = glyph_cb;
    for (int k = 0; k < 2; k++) {
        cells[k] = malloc(n * sizeof **cells);
        bits[k][0] = 0;
        if (!cells[k] || render_grid_init(&g[k], (render_dims){cols, rows, a->cell.cell_w, a->cell.cell_h},
                                          cells[k], n, bits[k], 1) != RENDER_OK) __builtin_trap();
        g[k].pages = &page; g[k].page_count = 1; g[k].glyphs = glyphs; g[k].glyph_count = 96;
        cfg.slice_clusters = k ? slice : 0;
        if (layout_init(&lay[k], &g[k], &cfg, rb[k], ru[k]) != LAYOUT_DONE) __builtin_trap();
        if (ins & 0x80) {                    /* marks are absolute byte offsets: edit check runs without them */
            layout_set_cursor(&lay[k], pick % (len + 1));
            layout_set_selection(&lay[k], pick % (len + 1), (pick * 7) % (len + 2));
        }
    }
    uint64_t first_byte = 0;                 /* a real line start */
    if (len) first_byte = piece_line_to_byte(t, pick % piece_line_count(t));
    uint32_t fr = 0;
    for (int k = 0; k < 2; k++) {
        if (render_frame_begin(&g[k], ++fr) != RENDER_OK) __builtin_trap();
        if (layout_begin(&lay[k], t, (layout_viewport){first_byte, LAYOUT_LINE_UNKNOWN, hs, 0}) < 0) __builtin_trap();
        int rc, guard = 0;
        while ((rc = layout_run(&lay[k])) == LAYOUT_MORE) if (++guard > 100000) __builtin_trap();
        if (rc != LAYOUT_DONE || render_grid_validate(&g[k]) != RENDER_OK) __builtin_trap();
    }
    if (memcmp(cells[0], cells[1], n * sizeof **cells) != 0) __builtin_trap();
    /* one edit through layout_edit vs a fresh layout */
    static const char *snip[] = { "x", "\n", "\xE4\xB8\xAD", "\t", "ab\ncd", "\xCC\x81" };
    const char *s = snip[ins % 6]; size_t sl = strlen(s), nl = 0;
    for (size_t i = 0; i < sl; i++) nl += s[i] == '\n';
    uint64_t off = len ? first_byte + (pick * 31) % (len - first_byte + 1) : 0;
    if (piece_insert(t, off, (const uint8_t *)s, sl) != PIECE_OK) __builtin_trap();
    if (render_frame_begin(&g[0], ++fr) != RENDER_OK) __builtin_trap();
    int rc = layout_edit(&lay[0], off, 0, sl, 0, nl);
    if (rc >= 0) {
        int guard = 0;
        while ((rc = layout_run(&lay[0])) == LAYOUT_MORE) if (++guard > 100000) __builtin_trap();
        if (rc != LAYOUT_DONE || render_grid_validate(&g[0]) != RENDER_OK) __builtin_trap();
        if (render_frame_begin(&g[1], ++fr) != RENDER_OK) __builtin_trap();
        if (layout_begin(&lay[1], t, (layout_viewport){first_byte, LAYOUT_LINE_UNKNOWN, hs, 0}) < 0) __builtin_trap();
        while ((rc = layout_run(&lay[1])) == LAYOUT_MORE) {}
        if (!(ins & 0x80) && memcmp(cells[0], cells[1], n * sizeof **cells) != 0) __builtin_trap();
        if (render_grid_validate(&g[1]) != RENDER_OK) __builtin_trap();
    }
    for (int k = 0; k < 2; k++) free(cells[k]);
    piece_destroy(t);
    return 0;
}
