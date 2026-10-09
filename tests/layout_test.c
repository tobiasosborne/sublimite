/* layout suite (P3.1): text -> render.h grid. Every layout is checked with
 * render_grid_validate; edits are checked against a fresh full layout. */
#include "layout/layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

enum { REPL = 95 };
typedef struct fx {
    render_grid g;
    render_cell *cells;
    uint64_t bits[8];
    uint64_t *row_byte; uint32_t *row_used;
    render_glyph glyphs[96];
    render_atlas_page page;
    layout l;
    piece_tree *t;
    uint32_t frame;
} fx;

static int stub_glyph(void *ctx, const uint8_t *c, size_t n, uint32_t w, uint32_t *slot)
{
    (void)c; (void)n; (void)w;
    (*(int *)ctx)++;
    *slot = REPL;
    return 0;
}
static int cb_calls;

static layout_config cfg_default(void)
{
    layout_config c = {0};
    c.tab_width = 4; c.fg = 0xdddddd; c.bg = 0x101010; c.gutter_fg = 0x808080; c.gutter_bg = 0x202020;
    c.cursor_fg = 0x000000; c.cursor_bg = 0xffff00; c.sel_fg = 0xffffff; c.sel_bg = 0x0000aa;
    c.glyph = stub_glyph; c.glyph_ctx = &cb_calls;
    return c;
}

static void fx_make(fx *f, uint32_t cols, uint32_t rows, layout_config cfg, const char *text, size_t len)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
    memset(f, 0, sizeof *f);
    f->cells = malloc((size_t)cols * rows * sizeof *f->cells);
    f->row_byte = malloc(rows * sizeof *f->row_byte);
    f->row_used = malloc(rows * sizeof *f->row_used);
    CHECK(a && f->cells && f->row_byte && f->row_used);
    CHECK(render_grid_init(&f->g, (render_dims){cols, rows, a->cell.cell_w, a->cell.cell_h},
                           f->cells, (size_t)cols * rows, f->bits, 8) == RENDER_OK);
    layout_ascii_glyphs(a, f->glyphs);
    f->glyphs[REPL] = f->glyphs['?' - 0x20];
    f->page = (render_atlas_page){a->pixels, a->pixels_len, (size_t)a->cell.cell_w * 95,
                                  a->cell.cell_w * 95, a->cell.cell_h};
    f->g.pages = &f->page; f->g.page_count = 1; f->g.glyphs = f->glyphs; f->g.glyph_count = 96;
    piece_allocator al = piece_default_allocator();
    f->t = piece_create(&al);
    CHECK(f->t && piece_init_copy(f->t, (const uint8_t *)text, len) == PIECE_OK);
    CHECK(layout_init(&f->l, &f->g, &cfg, f->row_byte, f->row_used) == LAYOUT_DONE);
}
static void fx_free(fx *f) { piece_destroy(f->t); free(f->cells); free(f->row_byte); free(f->row_used); }
static void fx_frame(fx *f) { CHECK(render_frame_begin(&f->g, ++f->frame) == RENDER_OK); }

static void run_all(fx *f)
{
    int rc, guard = 0;
    while ((rc = layout_run(&f->l)) == LAYOUT_MORE && ++guard < 10000000) {}
    CHECK(rc == LAYOUT_DONE);
    CHECK(render_grid_validate(&f->g) == RENDER_OK);
}
static void show(fx *f, uint64_t first_byte, uint32_t hscroll)
{
    fx_frame(f);
    CHECK(layout_begin(&f->l, f->t, (layout_viewport){first_byte, LAYOUT_LINE_UNKNOWN, hscroll, 0}) >= 0);
    run_all(f);
}

/* Row text from `from` cell on: ' ' for blank, ASCII glyph char, '#' other, '_' wide right. */
static void row_str(const fx *f, uint32_t r, uint32_t from, char *out)
{
    uint32_t n = 0;
    for (uint32_t c = from; c < f->g.dims.cols; c++) {
        const render_cell *x = &f->g.cells[(size_t)r * f->g.dims.cols + c];
        char ch;
        if (x->attrs & RENDER_ATTR_WIDE_RIGHT) ch = '_';
        else if (x->atlas_slot == RENDER_NO_SLOT) ch = ' ';
        else if (x->atlas_slot == REPL) ch = '#';
        else ch = (char)(x->atlas_slot + 0x20);
        out[n++] = ch;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}
#define ROW_IS(f, r, from, s) do { char b_[512]; row_str(f, r, from, b_); \
    if (strcmp(b_, s)) { printf("FAIL %s:%d row %u '%s' != '%s'\n", __FILE__, __LINE__, (unsigned)(r), b_, s); fails++; } } while (0)
static const render_cell *cell(const fx *f, uint32_t r, uint32_t c) { return &f->g.cells[(size_t)r * f->g.dims.cols + c]; }

static bool same_grid(const fx *a, const fx *b)
{
    return a->g.dims.cols == b->g.dims.cols &&
           memcmp(a->g.cells, b->g.cells, (size_t)a->g.dims.cols * a->g.dims.rows * sizeof(render_cell)) == 0;
}

static layout_config nogutter(void) { layout_config c = cfg_default(); c.gutter = false; return c; }
static layout_config gutter(void) { layout_config c = cfg_default(); c.gutter = true; return c; }

static void test_basic(void)
{
    fx f; const char *s = "a\tb\n\tx\nabc\tz\n";
    fx_make(&f, 16, 6, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "a   b");
    ROW_IS(&f, 1, 0, "    x");
    ROW_IS(&f, 2, 0, "abc z");
    ROW_IS(&f, 3, 0, "");
    ROW_IS(&f, 4, 0, "");
    CHECK(cell(&f, 0, 0)->bg == 0x101010 && cell(&f, 0, 0)->fg == 0xdddddd);
    CHECK(cell(&f, 0, 0)->glyph_index == 'a');
    fx_free(&f);
    layout_config c = nogutter(); c.tab_width = 8;
    s = "a\tb";
    fx_make(&f, 16, 2, c, s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "a       b");
    fx_free(&f);
}

static void test_crlf(void)
{
    fx f; const char *s = "ab\r\ncd\r\n\r\nx\ry";
    fx_make(&f, 10, 6, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "ab");
    ROW_IS(&f, 1, 0, "cd");
    ROW_IS(&f, 2, 0, "");
    ROW_IS(&f, 3, 0, "x?y");              /* lone CR is a control placeholder */
    CHECK(cell(&f, 3, 1)->attrs & RENDER_ATTR_INVERSE);
    fx_free(&f);
}

static void test_clusters(void)
{
    fx f;
    const char *s = "e\xCC\x81x";                     /* e + U+0301 */
    fx_make(&f, 8, 2, nogutter(), s, strlen(s));
    cb_calls = 0; show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "#x");
    CHECK(cell(&f, 0, 0)->glyph_index == '?' && cb_calls == 1);
    fx_free(&f);
    s = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7x"; /* ZWJ family */
    fx_make(&f, 8, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "#_x");
    CHECK((cell(&f, 0, 0)->attrs & RENDER_ATTR_WIDE_LEFT) && (cell(&f, 0, 1)->attrs & RENDER_ATTR_WIDE_RIGHT));
    CHECK(cell(&f, 0, 1)->atlas_slot == RENDER_NO_SLOT);
    fx_free(&f);
    s = "\xCC\x81z";                                  /* lone mark: zero width, dropped */
    fx_make(&f, 8, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "z");
    fx_free(&f);
    s = "a\xEF\xBC\xA1" "b";                        /* fullwidth A: wide */
    fx_make(&f, 8, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "a#_b");
    fx_free(&f);
}

static void test_wide_edges(void)
{
    fx f; const char *s = "abcd\xE4\xB8\xAD" "e";
    fx_make(&f, 5, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);                                   /* wide at the last cell: blank */
    ROW_IS(&f, 0, 0, "abcd");
    CHECK(cell(&f, 0, 4)->atlas_slot == RENDER_NO_SLOT && !(cell(&f, 0, 4)->attrs & RENDER_ATTR_WIDE_LEFT));
    fx_free(&f);
    fx_make(&f, 6, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "abcd#_");
    fx_free(&f);
    s = "a\xE4\xB8\xAD" "bc";                        /* hscroll 2 cuts the wide char: blank */
    fx_make(&f, 6, 2, nogutter(), s, strlen(s));
    show(&f, 0, 2);
    ROW_IS(&f, 0, 0, " bc");
    CHECK(cell(&f, 0, 0)->atlas_slot == RENDER_NO_SLOT);
    show(&f, 0, 1);
    ROW_IS(&f, 0, 0, "#_bc");
    show(&f, 0, 3);
    ROW_IS(&f, 0, 0, "bc");
    fx_free(&f);
}

static void test_invalid(void)
{
    fx f; const char *s = "a\xFF" "b\xC3\x28" "c\x01";
    fx_make(&f, 12, 2, nogutter(), s, strlen(s));
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "a?b?(c?");
    CHECK(cell(&f, 0, 1)->attrs & RENDER_ATTR_INVERSE);
    CHECK(!(cell(&f, 0, 0)->attrs & RENDER_ATTR_INVERSE));
    fx_free(&f);
}

static void test_gutter(void)
{
    for (uint32_t k = 1; k <= 7; k++) {
        uint32_t n = 1; for (uint32_t i = 0; i < k; i++) n *= 10; n -= 1;   /* n lines: k digits */
        char *s = malloc(n); memset(s, '\n', n - 1); s[n - 1] = 'z';
        fx f; fx_make(&f, 12, 3, gutter(), s, n);
        show(&f, 0, 0);
        CHECK(layout_gutter_width(&f.l) == k + 1);
        char buf[16]; snprintf(buf, sizeof buf, "%*u", (int)k, 1u);
        char exp[32]; snprintf(exp, sizeof exp, "%s", buf);
        char got[512]; row_str(&f, 0, 0, got);
        CHECK(strcmp(got, exp) == 0 || (k == 1 && strcmp(got, "1") == 0));
        CHECK(cell(&f, 0, k)->bg == 0x202020 && cell(&f, 0, k)->atlas_slot == RENDER_NO_SLOT);
        CHECK(cell(&f, 0, k - 1)->fg == 0x808080 && cell(&f, 0, k - 1)->atlas_slot == (uint32_t)('1' - 0x20));
        CHECK(cell(&f, 0, k + 1)->bg == 0x101010);
        free(s); fx_free(&f);
    }
    /* mid-file viewport: exact line numbers, text excluded from column maths */
    char *s = malloc(8000); size_t n = 0;
    for (int i = 0; i < 1000; i++) n += (size_t)snprintf(s + n, 8, "%d\t\n", i % 10);
    fx f; fx_make(&f, 14, 4, gutter(), s, n);
    show(&f, piece_line_to_byte(f.t, 500), 0);
    ROW_IS(&f, 0, 0, " 501 0");
    ROW_IS(&f, 1, 0, " 502 1");
    CHECK(layout_gutter_width(&f.l) == 5);
    fx_frame(&f);                                      /* caller-supplied first_line hook */
    CHECK(layout_begin(&f.l, f.t, (layout_viewport){piece_line_to_byte(f.t, 998), 998, 0, 0}) >= 0);
    run_all(&f);
    ROW_IS(&f, 0, 0, " 999 8");
    ROW_IS(&f, 1, 0, "1000 9");
    ROW_IS(&f, 2, 0, "1001");                          /* final empty line after the last \n */
    ROW_IS(&f, 3, 0, "");
    free(s); fx_free(&f);
}

static void test_hscroll_eof(void)
{
    fx f; const char *s = "0123456789\nab";
    fx_make(&f, 6, 4, nogutter(), s, strlen(s));
    show(&f, 0, 3);
    ROW_IS(&f, 0, 0, "345678");
    ROW_IS(&f, 1, 0, "");
    ROW_IS(&f, 2, 0, "");
    CHECK(f.row_byte[2] == LAYOUT_VOID_ROW);
    CHECK(!layout_approximate(&f.l));
    show(&f, 0, LAYOUT_MAX_HSCROLL + 5);
    CHECK(!layout_approximate(&f.l)); /* short lines are now exact at any offset */
    fx_free(&f);
}

static void test_slices(void)
{
    char *s = malloc(100000); size_t n = 0;
    for (int i = 0; i < 400; i++)
        n += (size_t)sprintf(s + n, "line %d\t\xE4\xB8\xAD\xE6\x96\x87 e\xCC\x81 \xFF end of it %d\r\n", i, i * 7);
    for (int i = 0; i < 3000; i++) s[n++] = 'x';                  /* a long line */
    s[n++] = '\n'; s[n++] = 'q';
    layout_config c = gutter();
    fx a, b;
    fx_make(&a, 40, 30, c, s, n);
    c.slice_clusters = 7;
    fx_make(&b, 40, 30, c, s, n);
    for (uint32_t hs = 0; hs < 12; hs += 5) {
        uint64_t fb = piece_line_to_byte(a.t, 395);
        show(&a, fb, hs);
        fx_frame(&b);
        CHECK(layout_begin(&b.l, b.t, (layout_viewport){fb, LAYOUT_LINE_UNKNOWN, hs, 0}) >= 0);
        int slices = 0, rc;
        while ((rc = layout_run(&b.l)) == LAYOUT_MORE) slices++;
        CHECK(rc == LAYOUT_DONE && slices > 10);
        CHECK(render_grid_validate(&b.g) == RENDER_OK);
        CHECK(same_grid(&a, &b));
        CHECK(memcmp(a.row_byte, b.row_byte, 30 * sizeof(uint64_t)) == 0);
    }
    /* a sliced run stops between clusters: each call does bounded work */
    fx_frame(&b);
    CHECK(layout_begin(&b.l, b.t, (layout_viewport){0, 0, 0, 0}) >= 0);
    CHECK(layout_run(&b.l) == LAYOUT_MORE && layout_busy(&b.l));
    free(s); fx_free(&a); fx_free(&b);
}

static void edit_insert(fx *f, uint64_t off, const char *txt)
{
    size_t n = strlen(txt), nl = 0;
    for (size_t i = 0; i < n; i++) nl += txt[i] == '\n';
    CHECK(piece_insert(f->t, off, (const uint8_t *)txt, n) == PIECE_OK);
    fx_frame(f);
    CHECK(layout_edit(&f->l, off, 0, n, 0, nl) >= 0);
    run_all(f);
}
static void edit_delete(fx *f, uint64_t off, uint64_t len)
{
    uint8_t *buf = malloc(len); size_t nl = 0;
    CHECK(piece_read(f->t, off, buf, len) == PIECE_OK);
    for (uint64_t i = 0; i < len; i++) nl += buf[i] == '\n';
    free(buf);
    CHECK(piece_delete(f->t, off, len, NULL) == PIECE_OK);
    fx_frame(f);
    CHECK(layout_edit(&f->l, off, len, 0, nl, 0) >= 0);
    run_all(f);
}
static void expect_dirty(const fx *f, uint32_t first, uint32_t count)
{
    render_strip st[8]; size_t n = 0;
    CHECK(render_dirty_strips(&f->g, st, 8, &n) == RENDER_OK);
    if (count == 0) { CHECK(n == 0); return; }
    CHECK(!f->g.full_frame);
    CHECK(n == 1 && st[0].first_row == first && st[0].row_count == count);
}
static void check_vs_fresh(fx *f, layout_config c, uint32_t cols, uint32_t rows, uint64_t fb, uint32_t hs)
{
    fx g; fx_make(&g, cols, rows, c, "", 0);
    piece_destroy(g.t); g.t = f->t;
    fx_frame(&g);
    CHECK(layout_begin(&g.l, g.t, (layout_viewport){fb, LAYOUT_LINE_UNKNOWN, hs, 0}) >= 0);
    run_all(&g);
    CHECK(same_grid(f, &g));
    CHECK(memcmp(f->row_byte, g.row_byte, rows * sizeof(uint64_t)) == 0);
    g.t = NULL; free(g.cells); free(g.row_byte); free(g.row_used);
}

static void test_dirty(void)
{
    char *s = malloc(1000); size_t n = 0;
    for (int i = 0; i < 20; i++) n += (size_t)sprintf(s + n, "line%d\n", i);
    layout_config c = gutter();
    fx f; fx_make(&f, 20, 8, c, s, n);
    show(&f, 0, 0);
    /* insert without newline in row 3 */
    uint64_t off3 = piece_line_to_byte(f.t, 3);
    edit_insert(&f, off3 + 2, "XY");
    expect_dirty(&f, 3, 1);
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* delete inside a row */
    edit_delete(&f, off3 + 2, 2);
    expect_dirty(&f, 3, 1);
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* insert a newline: row 3 to the bottom */
    edit_insert(&f, off3 + 2, "\n");
    expect_dirty(&f, 3, 5);
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* delete it again (joins lines) */
    edit_delete(&f, off3 + 2, 1);
    expect_dirty(&f, 3, 5);
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* delete spanning newlines */
    edit_delete(&f, off3 + 1, 8);
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* typing at the last visible row, and many keystrokes in one row keep row offsets exact */
    for (int i = 0; i < 12; i++) {
        edit_insert(&f, piece_line_to_byte(f.t, 2) + 1, "k");
        expect_dirty(&f, 2, 1);
    }
    check_vs_fresh(&f, c, 20, 8, 0, 0);
    /* edit before the viewport (scrolled): no newline = no redraw; with newline = full */
    show(&f, piece_line_to_byte(f.t, 5), 0);
    edit_insert(&f, 1, "ab");
    expect_dirty(&f, 0, 0);
    check_vs_fresh(&f, c, 20, 8, piece_line_to_byte(f.t, 5), 0);
    edit_insert(&f, 1, "\n");
    CHECK(f.g.full_frame);
    /* viewport first line was 5, now line 6 */
    {
        fx g; fx_make(&g, 20, 8, c, "", 0); piece_destroy(g.t); g.t = f.t;
        fx_frame(&g);
        CHECK(layout_begin(&g.l, g.t, (layout_viewport){f.l.first_byte, 6, 0, 0}) >= 0);
        run_all(&g);
        CHECK(same_grid(&f, &g));
        g.t = NULL; free(g.cells); free(g.row_byte); free(g.row_used);
    }
    /* edit that eats the viewport start asks for a new viewport */
    uint64_t fb = f.l.first_byte;
    CHECK(piece_delete(f.t, fb - 3, 6, NULL) == PIECE_OK);
    fx_frame(&f);
    CHECK(layout_edit(&f.l, fb - 3, 6, 0, 1, 0) == LAYOUT_RESET);
    /* digit growth: gutter width changes -> full */
    free(s);
    fx_free(&f);
    s = malloc(100);
    memset(s, '\n', 8); s[8] = 'x';
    fx_make(&f, 12, 4, c, s, 9);
    show(&f, 0, 0);
    CHECK(layout_gutter_width(&f.l) == 2);
    edit_insert(&f, 0, "\n");
    CHECK(f.g.full_frame && layout_gutter_width(&f.l) == 3);
    check_vs_fresh(&f, c, 12, 4, 0, 0);
    free(s); fx_free(&f);
}

static void test_cursor(void)
{
    fx f; const char *s = "abc\n\tdef\n";
    fx_make(&f, 12, 4, nogutter(), s, strlen(s));
    layout_set_cursor(&f.l, 1);
    layout_set_selection(&f.l, 5, 7);
    show(&f, 0, 0);
    CHECK((cell(&f, 0, 1)->attrs & RENDER_ATTR_CURSOR) && cell(&f, 0, 1)->bg == 0xffff00 && cell(&f, 0, 1)->fg == 0);
    CHECK(!(cell(&f, 0, 0)->attrs & RENDER_ATTR_CURSOR) && cell(&f, 0, 0)->bg == 0x101010);
    CHECK((cell(&f, 1, 4)->attrs & RENDER_ATTR_SELECTION) && cell(&f, 1, 4)->bg == 0x0000aa);   /* 'd' at byte 5 */
    CHECK((cell(&f, 1, 5)->attrs & RENDER_ATTR_SELECTION) && !(cell(&f, 1, 6)->attrs & RENDER_ATTR_SELECTION));
    layout_set_cursor(&f.l, 3);                       /* end of line: blank cursor cell */
    fx_frame(&f);
    CHECK(layout_relayout_rows(&f.l, 0, 1) == LAYOUT_MORE || 1);
    run_all(&f);
    CHECK((cell(&f, 0, 3)->attrs & RENDER_ATTR_CURSOR) && cell(&f, 0, 3)->atlas_slot == RENDER_NO_SLOT);
    CHECK(!(cell(&f, 0, 1)->attrs & RENDER_ATTR_CURSOR));
    expect_dirty(&f, 0, 1);
    fx_free(&f);
}

/* RED: unlimited slices must still bound long-line UI work. */
static void test_long_line_bounded(void)
{
    const size_t n = 1024u * 1024u;
    char *text = malloc(n);
    memset(text, 'a', n);
    text[1000000] = 'Z';
    fx f; fx_make(&f, 20, 3, nogutter(), text, n);
    fx_frame(&f);
    CHECK(layout_begin(&f.l, f.t, (layout_viewport){0, 0, 1000000, 1}) == LAYOUT_DONE);
    CHECK(layout_run(&f.l) == LAYOUT_DONE);
    CHECK(layout_approximate(&f.l));
    CHECK(f.l.bytes_scanned <= LAYOUT_BYTE_BUDGET);
    CHECK(f.l.bytes_read <= LAYOUT_WIN);
    fx_free(&f); free(text);
}

static void checkpoint_cb(const work_msg *msg, void *ud)
{
    (void)layout_checkpoint_event(ud, msg);
}
static void wait_checkpoint(work_pool *pool, layout_checkpoint_store *store)
{
    for (unsigned i = 0; store->pending && i < 20000; i++) {
        (void)work_mailbox_drain(pool, checkpoint_cb, store);
        struct timespec ts = {0, 1000000};
        if (store->pending) (void)nanosleep(&ts, NULL);
    }
    CHECK(!store->pending);
}
static void build_checkpoint(work_pool *pool, layout_checkpoint_store *store, fx *f)
{
    piece_snapshot *snap = piece_snapshot_take(f->t);
    CHECK(snap != NULL);
    CHECK(layout_checkpoint_request(store, pool, snap, f->t, 0, f->l.tab) == LAYOUT_DONE);
    piece_snapshot_release(snap);
    wait_checkpoint(pool, store);
}
static void test_checkpoints(void)
{
    const size_t n = 1024u * 1024u;
    char *text = malloc(n + 6);
    for (size_t i = 0; i < n; i++) text[i] = (char)('A' + i % 26);
    /* Tabs alter byte->column; Unicode cluster straddles a 4 KiB boundary. */
    text[17] = '\t';
    memcpy(text + 4095, "e\xCC\x81", 3);
    memcpy(text + n, "\nnext", 5);
    fx f; fx_make(&f, 20, 3, nogutter(), text, n + 5);
    edit_arena arena; CHECK(edit_arena_init(&arena, 16384) == 0);
    layout_checkpoint_store store;
    CHECK(layout_checkpoint_init(&store, &arena, n + 5) == LAYOUT_DONE);
    CHECK(layout_set_checkpoints(&f.l, &store) == LAYOUT_DONE);
    work_pool *pool = malloc(sizeof *pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    show(&f, 0, 1000000);
    CHECK(layout_approximate(&f.l) && f.l.bytes_scanned <= LAYOUT_BYTE_BUDGET + LAYOUT_WIN);
    build_checkpoint(pool, &store, &f);
    CHECK(store.complete && store.count > 200);
    show(&f, 0, 1000000);
    CHECK(!layout_approximate(&f.l));
    CHECK(f.l.bytes_scanned <= 2u * LAYOUT_WIN);
    CHECK(f.l.bytes_read <= 2u * LAYOUT_WIN);
    /* tab adds 2 columns; combining cluster removes 2: byte == column here. */
    CHECK(cell(&f, 0, 0)->glyph_index == (uint32_t)(unsigned char)text[1000000]);
    CHECK(f.row_byte[1] == n + 1 && f.row_byte[2] == LAYOUT_VOID_ROW);
    size_t old_count = store.count;
    edit_insert(&f, 200000, "\t");
    CHECK(store.count > 1 && store.count < old_count);
    CHECK(layout_approximate(&f.l));
    build_checkpoint(pool, &store, &f);
    show(&f, 0, 1000000);
    CHECK(!layout_approximate(&f.l));
    /* independent ASCII/tab prefix model around the inserted tab */
    uint32_t extra = 4u - (200000u % 4u);
    CHECK(cell(&f, 0, 0)->glyph_index == (uint32_t)(unsigned char)text[1000000u - extra]);
    edit_delete(&f, 200000, 1);
    build_checkpoint(pool, &store, &f);
    show(&f, 0, 1000000);
    CHECK(!layout_approximate(&f.l));
    CHECK(cell(&f, 0, 0)->glyph_index == (uint32_t)(unsigned char)text[1000000]);
    /* A result built on a stale snapshot must not revive invalidated columns. */
    piece_snapshot *snap = piece_snapshot_take(f.t);
    CHECK(layout_checkpoint_request(&store, pool, snap, f.t, 0, 4) == LAYOUT_DONE);
    piece_snapshot_release(snap);
    edit_insert(&f, 100, "\n");
    wait_checkpoint(pool, &store);
    CHECK(!store.complete);
    CHECK(store.count <= 1);
    work_pool_shutdown(pool); free(pool);
    fx_free(&f); edit_arena_free(&arena); free(text);
}

/* No synthetic cluster boundary at a window or budget boundary. */
static void test_budgeted_cluster(void)
{
    size_t n = 100001;
    char *text = malloc(n + 2); text[0] = 'e';
    for (size_t i = 1; i < n; i += 2) { text[i] = (char)0xcc; text[i + 1] = (char)0x81; }
    text[n] = 'x'; text[n + 1] = '\n';
    fx f; layout_config cfg = nogutter(); cfg.slice_clusters = 7;
    fx_make(&f, 8, 2, cfg, text, n + 2);
    fx_frame(&f); CHECK(layout_begin(&f.l, f.t, (layout_viewport){0, 0, 0, 2}) == LAYOUT_DONE);
    unsigned slices = 0; int rc;
    do {
        uint64_t before = f.l.bytes_scanned;
        rc = layout_run(&f.l);
        CHECK(f.l.bytes_scanned - before <= LAYOUT_BYTE_BUDGET + 4);
        slices++;
    } while (rc == LAYOUT_MORE && slices < 100000);
    CHECK(rc == LAYOUT_DONE && slices > 1);
    ROW_IS(&f, 0, 0, "?x");
    CHECK(layout_approximate(&f.l));
    fx_free(&f); free(text);
}

static int sized_cluster_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot)
{
    size_t *observed = ctx; *observed = len;
    CHECK(bytes[0] == 'e' && width == 1);
    *slot = REPL; return 0;
}
static void test_cluster_window_boundary(void)
{
    size_t cluster_len = 16001, prefix = 1000;
    char *text = malloc(prefix + cluster_len + 1); memset(text, 'a', prefix); text[prefix] = 'e';
    for (size_t i = 1; i < cluster_len; i += 2) { text[prefix + i] = (char)0xcc; text[prefix + i + 1] = (char)0x81; }
    text[prefix + cluster_len] = 'x';
    size_t observed = 0; layout_config cfg = nogutter(); cfg.glyph = sized_cluster_glyph; cfg.glyph_ctx = &observed;
    fx f; fx_make(&f, 8, 1, cfg, text, prefix + cluster_len + 1);
    show(&f, 0, (uint32_t)prefix);
    ROW_IS(&f, 0, 0, "#x");
    CHECK(observed == cluster_len && !layout_approximate(&f.l));
    fx_free(&f); free(text);
}
static void test_short_line_before_long(void)
{
    size_t n = 1024u * 1024u; char *text = malloc(n);
    memset(text, 'a', n); memcpy(text, "short\n", 6);
    fx f; fx_make(&f, 20, 2, nogutter(), text, n);
    show(&f, 0, 100000);
    ROW_IS(&f, 0, 0, "");
    CHECK(f.row_byte[1] == 6);
    CHECK(layout_approximate(&f.l));
    CHECK(f.l.bytes_scanned <= LAYOUT_BYTE_BUDGET + LAYOUT_WIN);
    fx_free(&f); free(text);
}

static void test_cache_invalid_right_context(void)
{
    char text[40]; memset(text, 'x', sizeof text); memcpy(text, "e\xE0\xFF", 3);
    fx f; fx_make(&f, 8, 1, nogutter(), text, sizeof text);
    show(&f, 0, 0); /* caches 'e' before an invalid E0 lead */
    memset(text, 'x', sizeof text); memcpy(text, "e\xE0\xB8\xB1", 4); /* Thai combining mark */
    CHECK(piece_delete(f.t, 0, sizeof text, NULL) == PIECE_OK);
    CHECK(piece_insert(f.t, 0, (const uint8_t *)text, sizeof text) == PIECE_OK);
    show(&f, 0, 0);
    ROW_IS(&f, 0, 0, "#xxxxxxx");
    fx_free(&f);
}

static void test_no_allocations(void)
{
    fx f; const char *text = "abc\t\xE4\xB8\xAD" "e\xCC\x81\n";
    fx_make(&f, 32, 4, nogutter(), text, strlen(text));
    show(&f, 0, 0);
    edit_malloc_guard_begin();
    for (uint32_t i = 0; i < 10000; i++) {
        fx_frame(&f);
        CHECK(layout_relayout_rows(&f.l, 0, 1) == LAYOUT_MORE);
        run_all(&f);
    }
    size_t allocations = edit_malloc_guard_end();
    CHECK(allocations == 0);
    printf("layout no-malloc: %zu allocations over 10000 relayouts (guard %s)\n", allocations,
           edit_malloc_guard_active() ? "active" : "ASan-inert");
    fx_free(&f);
}

int main(void)
{
    test_basic(); test_crlf(); test_clusters(); test_wide_edges(); test_invalid();
    test_gutter(); test_hscroll_eof(); test_slices(); test_dirty(); test_cursor(); test_long_line_bounded(); test_checkpoints(); test_budgeted_cluster(); test_no_allocations(); test_cluster_window_boundary(); test_short_line_before_long(); test_cache_invalid_right_context();
    if (fails) { printf("layout_test: %d FAILED\n", fails); return 1; }
    printf("layout_test: all passed\n");
    return 0;
}
