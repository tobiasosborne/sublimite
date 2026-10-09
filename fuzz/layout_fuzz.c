/* libFuzzer target for src/layout (P3.1): input = [viewport/config header][file bytes].
 * Properties: layout never reads out of bounds, always completes (sliced or
 * one-shot, identical grids), and the grid passes render_grid_validate; a
 * random newline-free or newline edit followed by layout_edit matches a fresh
 * full layout. */
#include "layout/layout.h"
#include <stdlib.h>
#include <string.h>
#include <poll.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int glyph_cb(void *ctx, const uint8_t *c, size_t n, uint32_t w, uint32_t *slot)
{
    (void)ctx; (void)c; (void)n; *slot = w == 2 ? 95u : 31u;
    return (c[0] & 0x1f) == 7;               /* sometimes fail: placeholder path */
}

static layout lay[2];

static void checkpoint_message(const work_msg *msg, void *ud)
{
    (void)layout_checkpoint_event(ud, msg);
}
static void checkpoint_build(work_pool *pool, layout_checkpoint_store *store, piece_tree *tree, uint32_t tab)
{
    piece_snapshot *snapshot = piece_snapshot_take(tree);
    if (!snapshot || layout_checkpoint_request(store, pool, snapshot, tree, 0, tab) != LAYOUT_DONE) __builtin_trap();
    piece_snapshot_release(snapshot);
    struct pollfd fd = {work_pool_eventfd(pool), POLLIN, 0};
    unsigned guard = 0;
    while (store->pending) {
        (void)poll(&fd, 1, 100);
        (void)work_mailbox_drain(pool, checkpoint_message, store);
        if (++guard > 1000) __builtin_trap();
    }
    if (!store->complete) __builtin_trap();
}

/* Independent naive forward column/cell model. Uses the one-shot UTF-8
 * oracle, no layout state, checkpoints, cache, or budgeted segmentation. */
static void naive_line(const uint8_t *text, size_t len, render_cell *cells, uint32_t cols,
                       uint32_t tab, uint32_t hs)
{
    render_cell blank = {0, RENDER_NO_SLOT, 0xffffff, 0, 0, 0};
    for (uint32_t i = 0; i < cols; i++) cells[i] = blank;
    uint64_t column = 0; uint32_t vis = 0;
    for (size_t p = 0; p < len && vis < cols;) {
        const uint8_t *bytes = text + p;
        if (*bytes == '\n') break;
        if (*bytes == '\r' && p + 1 < len && bytes[1] == '\n') { p++; continue; }
        if (*bytes == '\t') {
            uint32_t width = tab - (uint32_t)(column % tab);
            for (uint32_t j = 0; j < width && vis < cols; j++) if (column + j >= hs) cells[vis++] = blank;
            column += width; p++; continue;
        }
        utf8_step first = utf8_decode(bytes, len - p);
        bool invalid = !first.valid || *bytes < 0x20 || *bytes == 0x7f;
        int width = 1; size_t n = invalid ? 1 : utf8_cluster(bytes, len - p, &width);
        uint64_t after = column + (uint32_t)width;
        if (width && after > hs) {
            if (width == 2 && (column < hs || vis + 2 > cols)) cells[vis++] = blank;
            else {
                uint32_t slot = '?' - 0x20u, glyph = '?';
                if (!invalid && n == 1 && *bytes < 0x80) {
                    glyph = *bytes == ' ' ? 0 : *bytes;
                    slot = *bytes == ' ' ? RENDER_NO_SLOT : *bytes - 0x20u;
                } else if (!invalid) {
                    uint32_t selected;
                    if (glyph_cb(NULL, bytes, n, (uint32_t)width, &selected) == 0) slot = selected;
                }
                uint16_t attrs = invalid ? RENDER_ATTR_INVERSE : 0;
                if (width == 2) attrs |= RENDER_ATTR_WIDE_LEFT;
                cells[vis++] = (render_cell){glyph, slot, 0xffffff, 0, attrs, 0};
                if (width == 2) cells[vis++] = (render_cell){0, RENDER_NO_SLOT, 0xffffff, 0, RENDER_ATTR_WIDE_RIGHT, 0};
            }
        }
        column = after; p += n;
    }
}

static uint64_t naive_columns(const uint8_t *text, size_t len, uint32_t tab)
{
    uint64_t col = 0;
    for (size_t p = 0; p < len;) {
        if (text[p] == '\t') { col += tab - col % tab; p++; }
        else { int width; size_t n = utf8_cluster(text + p, len - p, &width); col += (uint32_t)width; p += n; }
    }
    return col;
}

static void fuzz_long_line(const uint8_t *data, size_t size)
{
    size_t capacity = 196608, target = 65537u + (size_t)data[1] * 400u;
    uint8_t *text = malloc(capacity);
    if (!text) __builtin_trap();
    static const char *patterns[] = {"abc", "\t", "\xE4\xB8\xAD", "e\xCC\x81", "\xFF", "\x01",
        "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5", "\xE2\x80\x8D", ("\xD8\x80" "a"), "1\xEF\xB8\x8F\xE2\x83\xA3"};
    size_t len = 0, index = 8;
    while (len < target) {
        const char *pattern = patterns[data[index % size] % 10u]; size_t n = strlen(pattern);
        memcpy(text + len, pattern, n); len += n; index++;
    }
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    if (!tree || piece_init_copy(tree, text, len) != PIECE_OK) __builtin_trap();
    work_pool *pool = malloc(sizeof *pool);
    edit_arena arena;
    if (!pool || work_pool_init(pool, 1, 0) != 0 || edit_arena_init(&arena, 2048) != 0) __builtin_trap();
    layout_checkpoint_store store;
    if (layout_checkpoint_init(&store, &arena, capacity) != LAYOUT_DONE) __builtin_trap();
    uint32_t cols = 1u + data[0] % 40u, tab = 1u + data[2] % 8u;
    uint64_t columns = naive_columns(text, len, tab);
    uint64_t pick = (uint64_t)data[6] * 257u + data[7];
    uint32_t hs = (uint32_t)(columns > 65536 ? 65536 + pick % (columns - 65536 + cols) : pick % (columns + cols));
    render_cell actual[40], expected[40]; uint64_t bits[1], rb[1]; uint32_t ru[1];
    render_grid grid; const font_ascii_atlas *atlas = font_ascii_atlas_for_px(15);
    render_glyph glyphs[96]; layout_ascii_glyphs(atlas, glyphs); glyphs[95] = glyphs[31];
    render_atlas_page page = {atlas->pixels, atlas->pixels_len, (size_t)atlas->cell.cell_w * 95,
                             atlas->cell.cell_w * 95, atlas->cell.cell_h};
    if (render_grid_init(&grid, (render_dims){cols, 1, atlas->cell.cell_w, atlas->cell.cell_h},
                         actual, cols, bits, 1) != RENDER_OK) __builtin_trap();
    grid.pages = &page; grid.page_count = 1; grid.glyphs = glyphs; grid.glyph_count = 96;
    layout_config cfg = {0}; cfg.tab_width = tab; cfg.fg = 0xffffff; cfg.glyph = glyph_cb;
    cfg.slice_clusters = 1u + data[3] % 17u;
    layout *l = malloc(sizeof *l);
    if (!l || layout_init(l, &grid, &cfg, rb, ru) != LAYOUT_DONE || layout_set_checkpoints(l, &store) != LAYOUT_DONE)
        __builtin_trap();
    uint32_t frame = 0;
    for (unsigned mutation = 0; mutation < 3; mutation++) {
        checkpoint_build(pool, &store, tree, tab);
        if (render_frame_begin(&grid, ++frame) != RENDER_OK ||
            layout_begin(l, tree, (layout_viewport){0, 0, hs, 1}) != LAYOUT_DONE) __builtin_trap();
        int rc; unsigned guard = 0;
        while ((rc = layout_run(l)) == LAYOUT_MORE) if (++guard > 100000) __builtin_trap();
        naive_line(text, len, expected, cols, tab, hs);
        if (rc != LAYOUT_DONE || render_grid_validate(&grid) != RENDER_OK ||
            memcmp(actual, expected, (size_t)cols * sizeof *actual) != 0) __builtin_trap();
        /* A pathological whole-line cluster may exceed contiguous glyph scratch;
         * otherwise a published index must resolve approximation. */
        if (l->cluster_len <= LAYOUT_WIN && layout_approximate(l)) __builtin_trap();
        if (mutation == 2) break;
        size_t off = ((size_t)data[7] * 257u + mutation * 4095u) % (len + 1);
        const char *insert = patterns[data[mutation + 2] % 10u]; size_t n = strlen(insert);
        if (piece_insert(tree, off, (const uint8_t *)insert, n) != PIECE_OK) __builtin_trap();
        memmove(text + off + n, text + off, len - off); memcpy(text + off, insert, n); len += n;
        if (render_frame_begin(&grid, ++frame) != RENDER_OK || layout_edit(l, off, 0, n, 0, 0) < 0) __builtin_trap();
        for (size_t i = 0; i < store.count; i++) if (store.entries[i].byte >= off) __builtin_trap();
        /* Supersede partial redraw with the next published-index full layout. */
    }
    if(layout_wrap_init(l,&arena)!=0 || layout_set_wrap(l,true)!=0) __builtin_trap();
    size_t target_byte=len/2, boundary=0;
    while(boundary<target_byte) { int width; boundary+=utf8_cluster(text+boundary,len-boundary,&width); }
    layout_wrap_row seed; bool approximate;
    if(layout_visual_row(l,tree,boundary,0,&seed,&approximate)!=0) __builtin_trap();
    size_t certified=0;
    while(certified<seed.start) { int width; certified+=utf8_cluster(text+certified,len-certified,&width); }
    if(certified!=seed.start) __builtin_trap();
    if(render_frame_begin(&grid,++frame)!=0 || layout_begin_visual(l,tree,(layout_viewport){0,0,0,1},&seed)!=0) __builtin_trap();
    int wrap_result; unsigned wrap_guard=0;
    do {
        uint64_t before=l->bytes_scanned; wrap_result=layout_run(l);
        if(l->bytes_scanned-before>LAYOUT_BYTE_BUDGET+4 || ++wrap_guard>100000) __builtin_trap();
    } while(wrap_result==LAYOUT_MORE);
    if(wrap_result!=LAYOUT_DONE || render_grid_validate(&grid)!=0) __builtin_trap();
    work_pool_shutdown(pool); free(pool); free(l); edit_arena_free(&arena); piece_destroy(tree); free(text);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8) return 0;
    if (size > 8 && (data[5] & 0x80u)) { fuzz_long_line(data, size); return 0; }
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
    edit_arena wrap_arena[2];
    bool wrapping=(data[4]&2u)!=0;
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
        if(edit_arena_init(&wrap_arena[k],8192)!=0 || layout_wrap_init(&lay[k],&wrap_arena[k])!=0 || layout_set_wrap(&lay[k],wrapping)!=0) __builtin_trap();
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
    if (memcmp(cells[0], cells[1], n * sizeof **cells) != 0 || memcmp(rb[0],rb[1],rows*sizeof **rb)!=0) __builtin_trap();
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
        if (!(ins & 0x80) && (memcmp(cells[0], cells[1], n * sizeof **cells) != 0 || memcmp(rb[0],rb[1],rows*sizeof **rb)!=0)) __builtin_trap();
        if (render_grid_validate(&g[1]) != RENDER_OK) __builtin_trap();
    }
    if((ins&0x40u) && piece_len(t)>first_byte) {
        uint64_t remaining=piece_len(t)-first_byte;
        uint64_t remove=1u+pick%5u; if(remove>remaining) remove=remaining;
        uint64_t off_delete=first_byte+(pick*37u)%(remaining-remove+1u);
        uint8_t removed[5]; if(piece_read(t,off_delete,removed,(size_t)remove)!=0) __builtin_trap();
        uint64_t old_newlines=0;
        for(size_t i=0;i<remove;i++) old_newlines+=removed[i]=='\n';
        if(piece_delete(t,off_delete,remove,NULL)!=0 || render_frame_begin(&g[0],++fr)!=0 ||
           layout_edit(&lay[0],off_delete,remove,0,old_newlines,0)<0) __builtin_trap();
        unsigned guard=0;
        while((rc=layout_run(&lay[0]))==LAYOUT_MORE) if(++guard>100000) __builtin_trap();
        if(rc!=0 || render_grid_validate(&g[0])!=0 || render_frame_begin(&g[1],++fr)!=0 ||
           layout_begin(&lay[1],t,(layout_viewport){first_byte,LAYOUT_LINE_UNKNOWN,hs,0})<0) __builtin_trap();
        while((rc=layout_run(&lay[1]))==LAYOUT_MORE) {}
        if(rc!=0 || (!(ins&0x80u) && (memcmp(cells[0],cells[1],n*sizeof **cells)!=0 || memcmp(rb[0],rb[1],rows*sizeof **rb)!=0))) __builtin_trap();
    }
    /* Resize and toggle back and forth, compare sliced vs one-shot output. */
    for(unsigned pass=0;pass<3;pass++) {
        uint32_t width=1u+(data[pass]+pass)%cols;
        for(int k=0;k<2;k++) {
            g[k].dims.cols=width;
            /* Existing wrap-off callers initialize repacked storage. Wrapped
             * begin must handle its own changed stride/stale cell extents. */
            if(pass%2u!=0) for(uint32_t r=0;r<rows;r++) ru[k][r]=width;
            if(layout_set_wrap(&lay[k],pass%2u==0)!=0 || render_frame_begin(&g[k],++fr)!=0 ||
               layout_begin(&lay[k],t,(layout_viewport){first_byte,LAYOUT_LINE_UNKNOWN,hs,0})<0) __builtin_trap();
            unsigned guard=0; int result;
            while((result=layout_run(&lay[k]))==LAYOUT_MORE) if(++guard>100000) __builtin_trap();
            if(result!=LAYOUT_DONE || render_grid_validate(&g[k])!=RENDER_OK) __builtin_trap();
        }
        if(memcmp(cells[0],cells[1],(size_t)width*rows*sizeof **cells)!=0 || memcmp(rb[0],rb[1],rows*sizeof **rb)!=0) __builtin_trap();
    }
    for (int k = 0; k < 2; k++) { edit_arena_free(&wrap_arena[k]); free(cells[k]); }
    piece_destroy(t);
    return 0;
}
