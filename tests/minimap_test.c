#include "minimap/minimap.h"
#include "base/base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "minimap_test:%d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct flat { const uint8_t *p; size_t n, fragment, calls; } flat;
static size_t span(void *ctx, uint64_t off, const uint8_t **p)
{
    flat *f = ctx; f->calls++;
    if (off >= f->n) return 0;
    size_t n = f->n - (size_t)off;
    if (f->fragment && n > f->fragment) n = f->fragment;
    *p = f->p + off; return n;
}
static bool space(uint8_t c) { return c == ' ' || (c >= 9 && c <= 13); }
static void reference(const uint8_t *p, size_t n, uint32_t h, minimap_row *rows)
{
    uint64_t lines = 1;
    for (size_t i = 0; i < n; i++) lines += p[i] == '\n';
    uint64_t k = (lines + h - 1) / h, line = 0, ink = 0, start = 0;
    memset(rows, 0, h * sizeof *rows);
    for (size_t i = 0; i <= n; i++) {
        if (i == n || p[i] == '\n') {
            minimap_row *r = &rows[line / k];
            if (!r->line_count) { r->first_line = line; r->first_byte = start; }
            r->line_count++; r->ink += ink < 80 ? ink : 80;
            line++; ink = 0; start = i + 1;
        } else if (!space(p[i])) ink++;
    }
    for (uint32_t i = 0; i < h; i++) if (rows[i].line_count)
        rows[i].density = (uint8_t)(rows[i].ink * 255 / (rows[i].line_count * 80));
}
static const minimap_style style = {0x101010, 0xffffff, 0x224466, 0x552200, false};
static void small_case(const uint8_t *p, size_t n, uint32_t h, size_t frag)
{
    render_cell cells[64 * 7]; uint64_t dirty[1]; minimap_row rows[64], ref[64];
    render_grid g; minimap m; flat f = {p, n, frag, 0};
    reference(p, n, h, ref);
    uint64_t lines = 0; for (uint32_t i = 0; i < h; i++) lines += ref[i].line_count;
    minimap_input in = {{&f, n, span, NULL}, lines, 1, true};
    CHECK(minimap_init(&m, rows, 64) == 0);
    CHECK(render_grid_init(&g, (render_dims){7,h,1,1}, cells, 64*7, dirty, 1) == 0);
    memset(cells, 0xa5, sizeof cells); CHECK(render_frame_begin(&g, 1) == 0);
    edit_malloc_guard_begin();
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 1, 1, &style) == 0);
    size_t allocs = edit_malloc_guard_end(); CHECK(allocs == 0);
    CHECK(!minimap_stale(&m, &in) && !m.sampled);
    size_t cached_calls = f.calls;
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 1, 1, &style) == 0 && f.calls == cached_calls);
    for (uint32_t r = 0; r < h; r++) {
        CHECK(rows[r].density == ref[r].density && rows[r].ink == ref[r].ink);
        CHECK(rows[r].line_count == ref[r].line_count);
        CHECK(rows[r].first_line == ref[r].first_line && rows[r].first_byte == ref[r].first_byte);
        CHECK(cells[r*7].bg == 0xa5a5a5a5u);
        for (uint32_t c = 3; c < 7; c++) {
            CHECK(cells[r*7+c].atlas_slot == RENDER_NO_SLOT && cells[r*7+c].glyph_index == 0);
            CHECK(cells[r*7+c].reserved == 0 && cells[r*7+c].attrs == 0);
        }
        if (ref[r].line_count) {
            minimap_target hit; uint32_t mapped;
            CHECK(minimap_hit(&m, &in, NULL, r, &hit) == 0 && hit.exact);
            CHECK(hit.line == ref[r].first_line && hit.byte == ref[r].first_byte);
            CHECK(minimap_row_for_line(&m, hit.line, &mapped) == 0 && mapped == r);
        }
    }
    CHECK((dirty[0] & ((UINT64_C(1) << h) - 1)) == ((UINT64_C(1) << h) - 1));
    minimap_target hit; CHECK(minimap_hit(&m, &in, NULL, -100, &hit) == 0 && hit.line == 0);
    CHECK(minimap_hit(&m, &in, NULL, 1000, &hit) == 0 && hit.line < lines);
    in.revision++; CHECK(minimap_stale(&m, &in));
    CHECK(minimap_hit(&m, &in, NULL, 0, &hit) == MINIMAP_ERR_STALE);
    render_cell previous[64 * 7]; memcpy(previous, cells, sizeof previous);
    in.index_ready = false; size_t calls = f.calls;
    CHECK(render_frame_begin(&g, g.frame_id + 1u) == 0);
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 0, 1, &style) == 0 && m.stale);
    CHECK(f.calls == calls);
    /* edit-g11: pending edits retain every prior cell, including viewport;
     * zero changed cells entails zero extra dirty rows. Geometry is inherited
     * from this fixture; no frame-count or timing threshold is introduced. */
    CHECK(memcmp(previous, cells, sizeof previous) == 0 && dirty[0] == 0);
    /* A shared grid can contain another tab's image. The pending map must
     * reconstruct this buffer's ready image rather than keep foreign cells. */
    for (uint32_t r = 0; r < h; r++) memset(&cells[r*7+3], 0, 4 * sizeof(render_cell));
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 0, 1, &style) == 0 && f.calls == calls);
    CHECK(memcmp(previous, cells, sizeof previous) == 0);
    in.index_ready = true; CHECK(minimap_stale(&m, &in));
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 0, 1, &style) == 0 && !minimap_stale(&m, &in));
    minimap_fini(&m); minimap_fini(&m);
}
static uint32_t model_blend(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t out = 0;
    for (uint32_t c = 0; c < 3; c++) {
        uint32_t x = (a >> (8*c)) & 255, y = (b >> (8*c)) & 255;
        out |= ((x*(255-t)+y*t+127)/255) << (8*c);
    }
    return out;
}
/* (G) positions come directly from "ab   cd": its non-space columns.
 * The pixel-cell fixture uses the requested one-pixel marks/edge. Tab stops
 * use the editor default (G) four columns, from editor/open.c. */
static void character_marks(void)
{
    static const uint8_t text[] = "ab   cd\na\tb\nabcdefghijklmnop";
    enum { COLS = sizeof "abcdefghijklmnop" - 1, ROWS = 3 };
    render_cell cells[ROWS * COLS] = {0}; uint64_t dirty[1];
    minimap_row rows[ROWS]; minimap m; render_grid g;
    flat f = {text, sizeof text - 1, 1, 0};
    minimap_input in = {{&f, f.n, span, NULL}, ROWS, 1, true};
    CHECK(minimap_init(&m, rows, ROWS) == 0);
    CHECK(render_grid_init(&g, (render_dims){COLS, ROWS, 1, 1},
                           cells, ROWS * COLS, dirty, 1) == 0);
    CHECK(render_frame_begin(&g, 1) == 0);
    edit_malloc_guard_begin();
    CHECK(minimap_fill(&m, &in, &g, 0, COLS, 0, 0, &style) == 0);
    CHECK(edit_malloc_guard_end() == 0);
    for (uint32_t c = 0; c < COLS; c++) {
        bool marked = c == 0 || c == 1 || c == 5 || c == 6;
        CHECK((cells[c].bg != style.background) == marked);
        CHECK((cells[COLS + c].bg != style.background) == (c == 0 || c == 4));
    }
    CHECK(render_frame_begin(&g, 2) == 0);
    CHECK(minimap_fill(&m, &in, &g, 0, 7, 0, 0, &style) == 0);
    CHECK(cells[2 * COLS + 6].bg != style.background);
    puts("edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok");
}
/* (G) four summary rows, each covering one fixture line. Viewport is the
 * middle pair; its perimeter is one pixel, with translucent interior. */
static void viewport_geometry(void)
{
    static const uint8_t text[] = "ab   cd\nab   cd\nab   cd\nab   cd";
    enum { COLS = sizeof "ab   cd" - 1, ROWS = 4 };
    render_cell cells[ROWS * COLS] = {0}; uint64_t dirty[1];
    minimap_row rows[ROWS]; minimap m; render_grid g;
    flat f = {text, sizeof text - 1, 0, 0};
    minimap_input in = {{&f, f.n, span, NULL}, ROWS, 1, true};
    CHECK(minimap_init(&m, rows, ROWS) == 0);
    CHECK(render_grid_init(&g, (render_dims){COLS, ROWS, 1, 1},
                           cells, ROWS * COLS, dirty, 1) == 0);
    CHECK(render_frame_begin(&g, 1) == 0);
    CHECK(minimap_fill(&m, &in, &g, 0, COLS, 1, 2, &style) == 0);
    /* (E) interior coverage = existing 128/4 = 32; edge = 3*32 = 96,
     * each normalised by the R8 maximum 255. The edge is three times
     * the wash, both below half opacity so document ink remains visible. */
    CHECK(cells[COLS + 3].bg == model_blend(style.background, style.viewport, 96));
    CHECK(cells[3].bg == style.background && cells[3 * COLS + 3].bg == style.background);
    CHECK(cells[COLS].bg != cells[COLS + 3].bg);
    puts("edit-8m3: translucent viewport perimeter and half-open geometry: ok");
}
/* (G) logical line spacing from perf/01-perf-target.md: two pixels.
 * Physical geometry is inspected separately from legacy cell backgrounds. */
static void pixel_geometry(void)
{
    static const uint8_t text[] = "ab   cd\nab   cd\nab   cd\nab   cd";
    enum { WIDTH = sizeof "ab   cd" - 1, LINES = 4, HEIGHT = LINES * 2,
           STRIDE = WIDTH + 1 };
    uint32_t pixels[HEIGHT * STRIDE];
    for (size_t i = 0; i < HEIGHT * STRIDE; i++) pixels[i] = UINT32_MAX;
    minimap_row rows[LINES]; minimap m;
    flat f = {text, sizeof text - 1, 1, 0};
    minimap_input in = {{&f, f.n, span, NULL}, LINES, 1, true};
    CHECK(minimap_init(&m, rows, LINES) == 0);
    CHECK(minimap_prepare(&m, &in, LINES) == 0);
    size_t calls = f.calls;
    edit_malloc_guard_begin();
    CHECK(minimap_pixels(&m, pixels, HEIGHT * STRIDE, WIDTH, HEIGHT, STRIDE,
                         1, 2, &style) == 0);
    CHECK(edit_malloc_guard_end() == 0 && f.calls == calls);
    /* The middle pair of lines occupies scanlines [2,6). */
    CHECK(pixels[2 * STRIDE + 3] == model_blend(style.background, style.viewport, 96));
    CHECK(pixels[3 * STRIDE + 3] == model_blend(style.background, style.viewport, 32));
    CHECK(pixels[4 * STRIDE + 1] == model_blend(style.density, style.viewport, 32));
    CHECK(pixels[3 * STRIDE] == model_blend(style.background, style.viewport, 96));
    CHECK(pixels[5 * STRIDE + 3] == model_blend(style.background, style.viewport, 96));
    CHECK(pixels[6 * STRIDE + 3] == style.background);
    for (uint32_t x = 0; x < WIDTH; x++) {
        CHECK(pixels[x] == (x == 0 || x == 1 || x == 5 || x == 6
                           ? style.density : style.background));
        CHECK(pixels[STRIDE + x] == style.background);
    }
    for (uint32_t y = 0; y < HEIGHT; y++) CHECK(pixels[y * STRIDE + WIDTH] == UINT32_MAX);
    CHECK(minimap_pixels(&m, pixels, WIDTH - 1, WIDTH, HEIGHT, STRIDE,
                         0, 0, &style) == MINIMAP_ERR_CAPACITY);
    CHECK(minimap_pixels(&m, pixels, HEIGHT * STRIDE, WIDTH, HEIGHT, STRIDE,
                         UINT64_MAX, UINT64_MAX, &style) == 0);
    CHECK(pixels[3 * STRIDE + 3] == style.background);
    /* edit-d5o: pending compose must reproduce the ready viewport, even
     * if navigation changed while indexing. No reads or allocations. */
    uint32_t retained[HEIGHT * STRIDE];
    CHECK(minimap_pixels(&m, retained, HEIGHT * STRIDE, WIDTH, HEIGHT, STRIDE,
                         1, 2, &style) == 0);
    m.painted_first = 1; m.painted_end = 3; /* fixture's viewport [1,1+2). */
    m.stale = true;
    calls = f.calls; edit_malloc_guard_begin();
    CHECK(minimap_pixels(&m, pixels, HEIGHT * STRIDE, WIDTH, HEIGHT, STRIDE,
                         0, 0, &style) == 0);
    CHECK(edit_malloc_guard_end() == 0 && f.calls == calls);
    for (uint32_t y = 0; y < HEIGHT; y++)
        CHECK(!memcmp(pixels + y * STRIDE, retained + y * STRIDE, WIDTH * sizeof *pixels));
    puts("edit-8m3: pixel marks, line spacing, viewport wash/edge, stride, source/allocation guards: ok");
}
/* Optional review artifact: rendered by the real minimap pixel API.
 * Width (G) comes from perf §2.11; height derives from fixture line count. */
static void dump_map(void)
{
    const char *path = getenv("MINIMAP_DUMP_PPM");
    if (!path) return;
    static const uint8_t text[] =
        "/* miniature document: whitespace and indentation survive */\n"
        "#include <stdint.h>\n\n"
        "static uint64_t sum(const uint8_t *bytes, size_t length)\n"
        "{\n"
        "    uint64_t result = 0;\n"
        "    for (size_t index = 0; index < length; index++) {\n"
        "        if (bytes[index] != ' ') {\n"
        "            result += bytes[index];\n"
        "        }\n"
        "    }\n"
        "    return result;\n"
        "}\n\n"
        "int main(void)\n"
        "{\n"
        "    const uint8_t message[] = \"ab   cd\";\n"
        "    return (int)sum(message, sizeof message);\n"
        "}\n";
    enum { WIDTH = 120, MAX_LINES = sizeof text, MAX_HEIGHT = MAX_LINES * 2 };
    minimap_row rows[MAX_LINES]; uint32_t pixels[WIDTH * MAX_HEIGHT]; minimap m;
    flat f = {text, sizeof text - 1, 0, 0}; uint32_t lines = 1;
    for (size_t i = 0; i < f.n; i++) lines += text[i] == '\n';
    minimap_input in = {{&f, f.n, span, NULL}, lines, 1, true};
    const minimap_style colors = {0x20242c, 0x78808e, 0xe5e9f0, 0xbf853d, false};
    CHECK(minimap_init(&m, rows, MAX_LINES) == 0);
    CHECK(minimap_prepare(&m, &in, lines) == 0);
    /* Viewport is the loop body: derive its extent from fixture substrings. */
    uint64_t first = 0, visible = 0;
    const uint8_t *begin = (const uint8_t *)strstr((const char *)text, "    for");
    const uint8_t *end = (const uint8_t *)strstr((const char *)text, "    return");
    CHECK(begin && end);
    for (const uint8_t *p = text; p < begin; p++) first += *p == '\n';
    for (const uint8_t *p = begin; p < end; p++) visible += *p == '\n';
    CHECK(minimap_pixels(&m, pixels, WIDTH * MAX_HEIGHT, WIDTH, lines * 2,
                         WIDTH, first, visible, &colors) == 0);
    FILE *out = fopen(path, "wb"); CHECK(out);
    CHECK(fprintf(out, "P6\n%u %u\n255\n", WIDTH, lines * 2) > 0);
    for (size_t i = 0; i < (size_t)WIDTH * lines * 2; i++) {
        uint8_t rgb[] = {(uint8_t)(pixels[i] >> 16), (uint8_t)(pixels[i] >> 8), (uint8_t)pixels[i]};
        CHECK(fwrite(rgb, sizeof rgb, 1, out) == 1);
    }
    CHECK(fclose(out) == 0);
}
static void large_and_errors(void)
{
    size_t n = 131089; uint8_t *bytes = malloc(n); CHECK(bytes);
    for (size_t i = 0; i < n; i++) bytes[i] = i % 101 == 100 ? '\n' : i % 5 == 0 ? ' ' : 'x';
    flat f = {bytes,n,7,0}; lineidx_src src = {&f,n,span,NULL};
    lineidx *idx = lineidx_create(n); CHECK(idx);
    render_cell cells[90], saved[90]; uint64_t dirty[1]; minimap_row rows[10]; minimap m;
    render_grid g; CHECK(render_grid_init(&g,(render_dims){9,10,1,180},cells,90,dirty,1) == 0);
    CHECK(render_frame_begin(&g,1) == 0); CHECK(minimap_init(&m,rows,10) == 0);
    memset(cells,0,sizeof cells);
    minimap_input in = {src, lineidx_line_count(idx).value, 8, lineidx_complete(idx)};
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && m.stale && f.calls == 0);
    /* Index setup resumes the bounded lineidx API between UI slices. */
    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(idx); slice++)
        (void)lineidx_seek_line(idx,&src,UINT64_MAX,n);
    CHECK(lineidx_complete(idx));
    in.lines = lineidx_line_count(idx).value; in.index_ready = true; f.calls = 0;
    edit_malloc_guard_begin();
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale && m.sampled);
    CHECK(edit_malloc_guard_end() == 0);
    /* edit-cxho.9: rows are lines 0..9 (the window at line 0), each read up
     * to its newline: (P) 10 lines of 101 bytes in 7-byte spans, plus the
     * snap at byte 0 which reads nothing: <= 10 * ceil(101/7) = 150 calls. */
    CHECK(f.calls <= 10*15);
    for (uint32_t r = 0; r < 10; r++) {
        size_t first = (size_t)r * 101u, end = first + 80u; /* summary prefix */
        CHECK(rows[r].first_byte == first && rows[r].first_line == r && rows[r].line_count == 1);
        uint64_t ink = 0;
        for (size_t i = first; i < end && bytes[i] != '\n'; i++) if (!space(bytes[i])) ink++;
        CHECK(rows[r].density == ink*255/80);
        minimap_target target; uint32_t back;
        CHECK(minimap_hit(&m,&in,idx,r,&target) == 0 && target.exact);
        CHECK(target.byte == (r ? lineidx_line_to_byte(idx,&src,target.line).value : 0));
        uint64_t want = 0;
        for (size_t i = 0; i < target.byte; i++) want += bytes[i] == '\n';
        CHECK(want == target.line);
        CHECK(minimap_row_for_line(&m,target.line,&back) == 0 && back == r);
        uint32_t bg = first < end && bytes[first] != '\n' && !space(bytes[first])
                    ? style.density : style.background;
        if (r == 0) bg = model_blend(bg,style.viewport,96);
        CHECK(cells[r*9+6].bg == bg);
    }
    memcpy(saved,cells,sizeof cells); minimap before = m;
    CHECK(minimap_fill(&m,&in,&g,8,2,0,1,&style) == MINIMAP_ERR_ARG);
    CHECK(memcmp(saved,cells,sizeof cells) == 0 && memcmp(&before,&m,sizeof m) == 0);
    g.dims.cell_h = 0;
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == MINIMAP_ERR_ARG);
    CHECK(memcmp(saved,cells,sizeof cells) == 0); g.dims.cell_h = 180;
    m.capacity = 9;
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == MINIMAP_ERR_CAPACITY); m.capacity = 10;
    /* Same-size real edit, refreshed index: revision still invalidates strip. */
    CHECK(lineidx_edit(idx,1,1,1) == 0); bytes[1] = ' ';
    in.revision++; in.index_ready = false;
    CHECK(minimap_stale(&m,&in));
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && m.stale);
    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(idx); slice++)
        (void)lineidx_refresh(idx,&src);
    CHECK(lineidx_complete(idx));
    in.lines = lineidx_line_count(idx).value; in.index_ready = true;
    CHECK(minimap_stale(&m,&in));
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale);
    /* (P) 80-byte prefix: 16 spaces at i % 5 == 0 plus the edited byte 1. */
    CHECK(rows[0].density == 63*255/80);
    /* Broken source must not partially modify the grid. */
    memcpy(saved,cells,sizeof cells); f.n = 0; in.revision++;
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == MINIMAP_ERR_SOURCE && m.stale);
    CHECK(memcmp(saved,cells,sizeof cells) == 0);
    minimap_fini(&m); lineidx_destroy(idx); free(bytes);
}
static void exact_edit(void)
{
    uint8_t bytes[] = "abc\ndef\n"; flat f = {bytes,sizeof bytes-1,0,0};
    minimap_input in = {{&f,f.n,span,NULL},3,1,true};
    minimap_row rows[4],ref[4]; render_cell cells[8]; uint64_t dirty[1]; minimap m; render_grid g;
    CHECK(render_grid_init(&g,(render_dims){2,4,1,1},cells,8,dirty,1) == 0);
    CHECK(render_frame_begin(&g,1) == 0 && minimap_init(&m,rows,4) == 0);
    CHECK(minimap_fill(&m,&in,&g,0,2,0,UINT64_MAX,&style) == 0);
    bytes[1] = ' '; in.revision++;
    CHECK(minimap_stale(&m,&in));
    CHECK(minimap_fill(&m,&in,&g,0,2,0,UINT64_MAX,&style) == 0);
    reference(bytes,f.n,4,ref); CHECK(!minimap_stale(&m,&in));
    for (size_t i = 0; i < 4; i++) CHECK(rows[i].density == ref[i].density);
    minimap_fini(&m);
}
typedef struct virtual_source { uint8_t block[256]; size_t calls; } virtual_source;
static size_t virtual_span(void *ctx, uint64_t off, const uint8_t **p)
{
    virtual_source *f = ctx; f->calls++;
    size_t phase = (size_t)(off%256); *p = f->block+phase; return 256-phase;
}
static void sample_cap_and_resize(void)
{
    virtual_source f = {{0},0};
    for (size_t i = 0; i < sizeof f.block; i++) f.block[i] = i%64 == 63 ? '\n' : 'x';
    minimap_input in = {{&f,UINT64_C(1073741824),virtual_span,NULL},UINT64_C(16777217),1,true};
    render_cell cells[1800*2]; uint64_t dirty[29]; minimap_row rows[1800]; minimap m; render_grid g;
    CHECK(minimap_init(&m,rows,1800) == 0);
    CHECK(render_grid_init(&g,(render_dims){2,1800,1,1},cells,3600,dirty,29) == 0);
    CHECK(render_frame_begin(&g,1) == 0);
    edit_malloc_guard_begin();
    CHECK(minimap_fill(&m,&in,&g,0,2,0,100,&style) == 0);
    /* edit-cxho.9: the window at line 0 reads only its 1800 lines of 64
     * bytes, in spans that end at each 256-byte block: (P) 1800*64/256 =
     * 450 calls, independent of the 1 GiB length. */
    CHECK(edit_malloc_guard_end() == 0 && f.calls == 1800u*64u/256u && m.active_rows == 1800);
    for (uint32_t r = 0; r < m.active_rows; r++)
        CHECK(rows[r].first_byte == (uint64_t)r*64 && rows[r].density == 63*255/80);
    size_t calls = f.calls;
    CHECK(minimap_fill(&m,&in,&g,0,1,1000,50,&style) == 0 && f.calls == calls);
    /* Changed height invalidates cache even with an unchanged revision. */
    g.dims.rows = 129; CHECK(render_frame_begin(&g,2) == 0);
    CHECK(minimap_fill(&m,&in,&g,0,2,0,0,&style) == 0);
    CHECK(f.calls > calls && m.height == 129);
    in.index_ready = false; g.dims.rows = 1800;
    CHECK(render_frame_begin(&g,3) == 0);
    calls = f.calls;
    CHECK(minimap_fill(&m,&in,&g,0,2,0,0,&style) == 0 && m.stale && f.calls == calls);
    /* Pending resize cannot retain a differently shaped map. The existing
     * fallback density remains, with only the last strip column as its bar. */
    CHECK(cells[0].bg == model_blend(style.background,style.density,64));
    CHECK(cells[1].bg == style.stale);
    minimap_fini(&m);
}
static void no_hook(void *user, uint32_t frame, uint64_t ns)
{
    (void)user; (void)frame; (void)ns;
}
static void cache_identity(void)
{
    flat a = {(const uint8_t *)"aaa\n",4,0,0};
    flat b = {(const uint8_t *)"\nxxx",4,0,0};
    minimap_input in = {{&a,4,span,NULL},2,1,true};
    minimap_row rows[2]; minimap m; render_grid g;
    render_cell cells[4] = {0}; uint64_t dirty[1]; minimap_target hit;
    for (size_t i=0;i<4;i++) cells[i].atlas_slot=RENDER_NO_SLOT;
    CHECK(minimap_init(&m,rows,2) == 0);
    CHECK(render_grid_init(&g,(render_dims){2,2,1,1},cells,4,dirty,1) == 0);
    CHECK(render_frame_begin(&g,1) == 0);
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    in.source.ctx = &b;
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    CHECK(minimap_hit(&m,&in,NULL,1,&hit) == 0 && hit.exact && hit.byte == 1);
    /* Adapter context recycling uses explicit stable buffer binding. */
    CHECK(minimap_bind(&m,&b) == 0);
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    b.p=(const uint8_t *)"aaa\n";
    CHECK(minimap_bind(&m,&a) == 0 && minimap_stale(&m,&in));
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    CHECK(minimap_hit(&m,&in,NULL,1,&hit) == 0 && hit.exact && hit.byte == 4);
    puts("review 5: equal-size tab switch exact byte=1: ok");
    render_backend backend = {0}; render_backend_info info;
    CHECK(render_null_backend(&backend) == 0 && render_backend_query(&backend,&info) == 0);
    void *state=calloc(1,info.state_size?info.state_size:1); CHECK(state);
    render_config config={.dims=g.dims,.max_width=2,.max_height=2,.max_cells=4,
                          .hooks={no_hook,no_hook,NULL}};
    CHECK(render_backend_init(&backend,&config,state,info.state_size) == 0);
    render_strip initial[2]; size_t initial_count;
    CHECK(render_mark_full(&g) == 0 && render_dirty_strips(&g,initial,2,&initial_count) == 0);
    CHECK(render_backend_submit(&backend,&g,initial,initial_count) == 0);
    CHECK(render_backend_present(&backend,1) == 0);
    CHECK(render_frame_begin(&g,2) == 0 && render_mark_rows(&g,0,1) == 0);
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    render_strip strips[2]; size_t count;
    CHECK(render_dirty_strips(&g,strips,2,&count) == 0);
    CHECK(count == 1 && strips[0].first_row == 0 && strips[0].row_count == 1);
    CHECK(render_backend_submit(&backend,&g,strips,count) == 0 && render_backend_present(&backend,2) == 0);
    render_stats stats; CHECK(render_backend_stats(&backend,&stats) == 0 && stats.submitted_cells == 6);
    CHECK(render_frame_begin(&g,3) == 0);
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0);
    CHECK(render_dirty_strips(&g,strips,2,&count) == 0 && count == 0);
    CHECK(render_backend_submit(&backend,&g,strips,count) == 0 && render_backend_present(&backend,3) == 0);
    CHECK(render_backend_stats(&backend,&stats) == 0 && stats.submitted_cells == 6);
    render_backend_shutdown(&backend); free(state);
    puts("review 7: cached fill preserves typing/blink damage: ok");
}
static void pending_navigation(void)
{
    flat f = {NULL,1000000,0,0};
    minimap_input in = {{&f,f.n,span,NULL},1,1,false};
    minimap_row rows[10]; minimap m; render_grid g;
    render_cell cells[20] = {0}; uint64_t dirty[1];
    CHECK(minimap_init(&m,rows,10) == 0);
    CHECK(render_grid_init(&g,(render_dims){2,10,1,1},cells,20,dirty,1) == 0);
    CHECK(render_frame_begin(&g,1) == 0);
    CHECK(minimap_fill(&m,&in,&g,1,1,0,1,&style) == 0 && f.calls == 0);
    CHECK(cells[1].bg != model_blend(style.background,style.stale,128));
    minimap_target hit;
    CHECK(minimap_hit_approx(&in,10,-100,&hit) == 0 && hit.byte == 0 && !hit.exact);
    CHECK(minimap_hit_approx(&in,10,5,&hit) == 0 && hit.byte == 555555 && !hit.exact);
    CHECK(minimap_hit_approx(&in,10,100,&hit) == 0 && hit.byte == in.source.len);
    in.revision++; in.source.len=UINT64_MAX;
    CHECK(minimap_hit_approx(&in,10,9,&hit) == 0 && hit.byte == UINT64_MAX);
    CHECK(f.calls == 0);
    puts("review 8: pending index paints current byte scrollbar and drag works: ok");
}
/* edit-cxho.1: an unfilled map has no summaries, so fill paints the pending
 * scrollbar. (P) a one-line source in a strip of more rows than lines owns
 * one row, as minimap_prepare's one-line-per-row grouping gives it; the band
 * may light only that row. 100 rows is the bead's fixture (any rows > lines
 * shows it); width 4 reuses the strip widths above. Occupied columns are
 * ceil(64 * 4 / 255) = 2, the pending ink coverage minimap_fill uses. The
 * three-line case puts the one-line viewport on the middle line. */
static void pending_one_line(void)
{
    enum { rows = 100, width = 4, occupied = 2 };
    flat f = {(const uint8_t *)"x", 1, 0, 0};
    minimap_input in = {{&f,f.n,span,NULL},1,1,false};
    minimap_row summaries[rows]; minimap m; render_grid g;
    render_cell cells[rows * width]; uint64_t dirty[2];
    CHECK(minimap_init(&m,summaries,rows) == 0 && !m.filled);
    CHECK(render_grid_init(&g,(render_dims){width,rows,1,1},cells,rows*width,dirty,2) == 0);
    CHECK(render_frame_begin(&g,1) == 0);
    CHECK(minimap_fill(&m,&in,&g,0,width,0,1,&style) == 0 && m.stale && !m.filled && f.calls == 0);
    uint32_t ink = model_blend(style.background,style.density,64);
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c + 1 < width; c++) {
            uint32_t plain = c < occupied ? ink : style.background;
            uint32_t want = r ? plain : c == 0 ? model_blend(plain,style.viewport,96)
                                      : model_blend(plain,style.viewport,32);
            if (cells[r*width+c].bg != want)
                fprintf(stderr, "row %u col %u bg %06x want %06x\n", r, c, cells[r*width+c].bg, want);
            CHECK(cells[r*width+c].bg == want);
        }
        CHECK(cells[r*width+width-1].bg == style.stale); /* the pending edge bar stays */
    }
    /* The editor's grab hit (paint.c map_grab) uses this same predicate. */
    for (uint32_t r = 0; r < rows; r++) {
        CHECK(minimap_pending_band(1,rows,r,0,1) == (r == 0));
        CHECK(minimap_pending_band(3,rows,r,1,2) == (r == 1));
    }
    minimap_fini(&m);
    puts("edit-cxho.1: pending one-line map lights only its own row: ok");
}
static void deferred_samples(void)
{
    virtual_source f = {{0},0}; memset(f.block,'x',sizeof f.block);
    minimap_input in = {{&f,UINT64_C(1073741824),virtual_span,NULL},100000,1,true};
    minimap_row rows[10]; minimap m; render_grid g;
    render_cell cells[20] = {0}; uint64_t dirty[1];
    CHECK(minimap_init(&m,rows,10) == 0);
    CHECK(render_grid_init(&g,(render_dims){2,10,1,1},cells,20,dirty,1) == 0);
    CHECK(render_frame_begin(&g,1) == 0);
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0);
    CHECK(f.calls == 0 && m.stale);
    minimap_row worker_rows[10]; minimap prepared;
    CHECK(minimap_init(&prepared,worker_rows,10) == 0);
    virtual_source worker = f; minimap_input old = in;
    lineidx_src snapshot_source = {&worker,in.source.len,virtual_span,NULL};
    CHECK(minimap_prepare_source(&prepared,&in,&snapshot_source,10) == 0 && worker.calls > 0);
    in.revision++;
    CHECK(minimap_publish(&m,&in,&prepared) == MINIMAP_ERR_STALE);
    in = old;
    CHECK(minimap_publish(&m,&in,&prepared) == 0 && m.rows == rows);
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0 && !m.stale && f.calls == 0);
    render_cell previous[20]; memcpy(previous,cells,sizeof previous);
    in.revision++;
    CHECK(render_frame_begin(&g,g.frame_id + 1u) == 0);
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0 && m.stale && f.calls == 0);
    CHECK(memcmp(previous,cells,sizeof previous) == 0 && dirty[0] == 0);
    in = old;
    CHECK(minimap_publish(&m,&in,&prepared) == 0);
    g.dims.rows = 9;
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0 && m.stale && f.calls == 0);
    /* Read-protected sample pages prove UI fill cannot dereference them after
     * a worker preparation, or on a cache miss after an edit. */
    size_t n=131072;
    uint8_t *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n);
    flat mapped={p,n,0,0}; in=(minimap_input){{&mapped,n,span,NULL},1000,1,true};
    CHECK(minimap_bind(&m,&mapped) == 0 && minimap_bind(&prepared,&mapped) == 0);
    CHECK(minimap_prepare(&prepared,&in,9) == 0);
    CHECK(minimap_publish(&m,&in,&prepared) == 0);
    CHECK(mprotect(p,n,PROT_NONE) == 0); mapped.calls=0;
    edit_malloc_guard_begin();
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0 && !m.stale);
    in.revision++;
    CHECK(minimap_fill_cached(&m,&in,&g,1,1,0,1,&style) == 0 && m.stale);
    CHECK(edit_malloc_guard_end() == 0 && mapped.calls == 0);
    CHECK(munmap(p,n) == 0);
    puts("review 9: UI deferred fill reads no dispersed samples: ok");
}
/* edit-cxho.8: fixed-scale window mapping. Strip H = 1410 px and v = 45
 * lines are the research repro (Sonnet 2026-10-27): ~705 summary rows of
 * 2 px (M) and a 45-line text view (M), so the box is 45 * 2 = 90 px (P).
 * Line counts are test inputs spanning tiny to 10^7, not budgets. */
static uint64_t ceil_div(uint64_t a, uint64_t b) { return a / b + (a % b != 0); }
static bool window_equal(const minimap_window *a, const minimap_window *b)
{
    return a->first_line == b->first_line && a->range == b->range && a->above == b->above
        && a->phase == b->phase && a->rows == b->rows && a->box_top == b->box_top
        && a->box_height == b->box_height && a->travel == b->travel
        && a->scale == b->scale && a->height == b->height && a->scrolls == b->scrolls;
}
static void window_invariants(uint64_t n, uint64_t v, uint64_t f, uint32_t s,
                              uint32_t h, const minimap_window *w)
{
    uint64_t r = n > v ? n - v : 0, fc = f < r ? f : r;
    uint64_t b = v > h / s ? h : v * s;
    CHECK(w->box_height == b && w->box_top + w->box_height <= h);
    CHECK(w->range == r && w->scale == s && w->height == h);
    CHECK(w->travel == (r > (h - b) / s ? h - b : r * s));
    CHECK(w->scrolls == (r > (h - b) / s));
    CHECK(w->box_top <= w->travel && w->phase < s);
    /* The viewport's first line is drawn exactly at the box top. */
    CHECK(w->first_line <= fc && fc - w->first_line == w->above);
    CHECK((uint64_t)w->above * s - w->phase == w->box_top);
    CHECK(w->above == ceil_div(w->box_top, s));
    uint64_t slots = ceil_div((uint64_t)h + w->phase, s);
    uint64_t left = n - w->first_line;
    CHECK(w->rows == (slots < left ? slots : left));
    if (!w->scrolls) CHECK(w->first_line == 0 && w->phase == 0 && w->box_top == fc * s);
    else if (n <= UINT64_MAX / s) /* never shows past EOF */
        CHECK(w->first_line * s + w->phase + h <= n * s);
}
static void window_mapping(void)
{
    static const uint64_t sizes[] = {0, 1, 44, 45, 46, 100, 660, 661, 704, 705,
        706, 1000, 20000, 200000, 1000000, 10000000};
    const uint32_t s = MINIMAP_LINE_PX, h = 1410; const uint64_t v = 45;
    CHECK(MINIMAP_LINE_PX == 2u);
    edit_malloc_guard_begin();
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        uint64_t n = sizes[i], r = n > v ? n - v : 0;
        minimap_window w, prev = {0};
        uint64_t stride = r > 4000 ? r / 4000 : 1;
        for (uint64_t f = 0;; f = f + stride > r && f < r ? r : f + stride) {
            CHECK(minimap_window_lines(n, v, f, s, h, &w) == 0);
            window_invariants(n, v, f, s, h, &w);
            CHECK(w.box_height == 90); /* constant across N */
            CHECK(n * s > h || !w.scrolls); /* small documents do not scroll */
            if (f) CHECK(w.box_top >= prev.box_top);
            if (f == 0) CHECK(w.box_top == 0 && w.first_line == 0);
            if (f == r) CHECK(w.box_top == (w.scrolls ? h - 90u : r * s));
            /* f -> top -> f: exact when lines are no denser than pixels. */
            uint64_t back = minimap_window_seek(&w, w.box_top, 0);
            uint64_t step = w.travel ? ceil_div(r, w.travel) : r;
            CHECK(back <= f && f - back < (step ? step : 1));
            if (r <= w.travel) CHECK(back == f);
            prev = w;
            if (f == r) break;
        }
        /* top -> f -> top: the drag is 1:1 when lines are denser than px. */
        for (uint32_t t = 0; t <= w.travel; t++) {
            uint64_t line = minimap_window_seek(&w, t, 0);
            minimap_window again;
            CHECK(line <= r && minimap_window_lines(n, v, line, s, h, &again) == 0);
            if (r >= w.travel) CHECK(again.box_top == t);
            else CHECK(again.box_top >= t && again.box_top - t <= ceil_div(w.travel, r));
        }
        CHECK(minimap_window_seek(&w, -100000, 0) == 0);
        CHECK(minimap_window_seek(&w, 100000, 0) == r);
        if (n >= 20000) CHECK(w.scrolls && w.rows == 705);
    }
    /* Grab: inside keeps the offset; outside centres the box on the pointer. */
    minimap_window w; int64_t off = -1;
    CHECK(minimap_window_lines(20000, v, 10000, s, h, &w) == 0 && w.scrolls);
    CHECK(minimap_window_grab(&w, (int64_t)w.box_top + 5, &off) && off == 5);
    uint64_t grabbed = minimap_window_seek(&w, (int64_t)w.box_top + 5, off);
    CHECK(grabbed <= 10000 && 10000 - grabbed < ceil_div(w.range, w.travel));
    CHECK(!minimap_window_grab(&w, (int64_t)w.box_top - 1, &off) && off == 45);
    CHECK(!minimap_window_grab(&w, (int64_t)w.box_top + 90, &off) && off == 45);
    CHECK(minimap_window_grab(&w, (int64_t)w.box_top + 89, &off) && off == 89);
    uint64_t warp = minimap_window_seek(&w, 700, 45);
    minimap_window c; CHECK(minimap_window_lines(20000, v, warp, s, h, &c) == 0);
    CHECK(c.box_top == 700 - 45); /* the click lands at the box centre */
    CHECK(!minimap_window_grab(NULL, 0, &off) && !minimap_window_grab(&w, 0, NULL));
    CHECK(minimap_window_seek(NULL, 0, 0) == 0);
    /* Degenerate inputs clamp. */
    minimap_window keep = w;
    CHECK(minimap_window_lines(100, v, 0, 0, h, &w) == MINIMAP_ERR_ARG);
    CHECK(window_equal(&keep, &w));
    CHECK(minimap_window_lines(100, v, 0, s, h, NULL) == MINIMAP_ERR_ARG);
    CHECK(minimap_window_bytes(100, v, 0, 0, h, &w) == MINIMAP_ERR_ARG);
    CHECK(minimap_window_lines(0, v, 7, s, h, &w) == 0 && w.rows == 0);
    window_invariants(0, v, 7, s, h, &w);
    CHECK(minimap_window_lines(10, v, 3, s, h, &w) == 0 && w.box_top == 0 && w.rows == 10);
    window_invariants(10, v, 3, s, h, &w);
    CHECK(minimap_window_lines(1000, v, 500, s, 50, &w) == 0); /* H < box */
    window_invariants(1000, v, 500, s, 50, &w);
    CHECK(w.box_height == 50 && w.box_top == 0 && w.first_line == 500 && w.travel == 0);
    CHECK(minimap_window_seek(&w, 30, 0) == 0);
    CHECK(minimap_window_lines(1000, v, 500, s, 0, &w) == 0);
    window_invariants(1000, v, 500, s, 0, &w);
    CHECK(w.box_height == 0 && w.rows == 0);
    CHECK(minimap_window_lines(1000, v, 5000, s, h, &w) == 0);
    CHECK(minimap_window_lines(1000, v, 955, s, h, &c) == 0 && window_equal(&w, &c));
    CHECK(minimap_window_lines(UINT64_MAX, UINT64_MAX / 3, UINT64_MAX / 2, 7, UINT32_MAX, &w) == 0);
    window_invariants(UINT64_MAX, UINT64_MAX / 3, UINT64_MAX / 2, 7, UINT32_MAX, &w);
    CHECK(minimap_window_lines(UINT64_MAX, 1, UINT64_MAX - 2, s, h, &w) == 0);
    window_invariants(UINT64_MAX, 1, UINT64_MAX - 2, s, h, &w);
    /* floor((R - 1) * 1408 / R) = 1407 = h - 3 (P) */
    CHECK(w.box_top == h - 3u && minimap_window_seek(&w, h, 0) == UINT64_MAX - 1);
    /* Byte fraction while indexing: a thumb over [0, bytes]. 10^9 bytes is
     * a test input (a GB-class file), not a budget. */
    const uint64_t bytes = UINT64_C(1000000000);
    for (uint64_t b = 0; b <= bytes; b += bytes / 997) {
        CHECK(minimap_window_bytes(bytes, v, b, s, h, &w) == 0);
        CHECK(w.first_line == MINIMAP_LINE_UNKNOWN && w.scrolls && w.range == bytes);
        CHECK(w.box_height == 90 && w.travel == h - 90u);
        CHECK(w.box_top == b * (h - 90u) / bytes);
        CHECK(w.above == ceil_div(w.box_top, s) && w.above * s - w.phase == w.box_top);
        CHECK(w.rows == ceil_div(h + w.phase, s));
        uint64_t back = minimap_window_seek(&w, w.box_top, 0);
        CHECK(back <= b && b - back < ceil_div(bytes, w.travel));
    }
    CHECK(minimap_window_bytes(bytes, v, bytes * 2, s, h, &w) == 0 && w.box_top == h - 90u);
    for (uint32_t t = 0; t <= h - 90u; t++) {
        minimap_window again;
        CHECK(minimap_window_bytes(bytes, v, minimap_window_seek(&w, t, 0), s, h, &again) == 0);
        CHECK(again.box_top == t);
    }
    CHECK(minimap_window_bytes(0, v, 5, s, h, &w) == 0 && w.box_top == 0);
    CHECK(minimap_window_seek(&w, 900, 0) == 0);
    CHECK(edit_malloc_guard_end() == 0);
    puts("edit-cxho.8: fixed-scale window, constant box, round trip, clamps: ok");
}
/* edit-cxho.9: miniature of the window's real lines. Strip H = 1410 px,
 * v = 45 lines and 705 summary rows are the research repro (M) used by
 * edit-cxho.8; 200000 lines / window at 150000 and the 640943-byte file
 * (Tobias's CONTENTS.md size, (M)) are the bead's fixtures, not budgets. */
typedef struct counted {
    const uint8_t *p; size_t n, fragment, calls;
    uint64_t delivered, lo, hi;
} counted;
static size_t counted_span(void *ctx, uint64_t off, const uint8_t **p)
{
    counted *f = ctx; f->calls++;
    if (off >= f->n) return 0;
    size_t n = f->n - (size_t)off;
    if (f->fragment && n > f->fragment) n = f->fragment;
    f->delivered += n;
    if (off < f->lo) f->lo = off;
    if (off + n > f->hi) f->hi = off + n;
    *p = f->p + off; return n;
}
/* Independent model of one line's marks: tabs to four-column stops,
 * columns clipped at MINIMAP_LINE_CAP. */
static void model_marks(const uint8_t *p, size_t n, size_t start, uint64_t marks[2])
{
    marks[0] = marks[1] = 0; uint32_t col = 0;
    for (size_t i = start; i < n && p[i] != '\n' && col < MINIMAP_LINE_CAP; i++) {
        if (p[i] == '\t') { col += 4u - col % 4u; continue; }
        if (!space(p[i])) marks[col / 64u] |= UINT64_C(1) << (col % 64u);
        col++;
    }
}
/* Line j: indent j % 13 (neighbours always differ), then 1..40 bytes of 'x'
 * with a space wherever (k + j) % 6 == 0: <= 12 + 40 = 52 bytes (P), so
 * every line fits the summary width. No trailing newline. */
static size_t region_lines(uint8_t *buf, size_t lines, uint64_t *starts)
{
    size_t n = 0;
    for (size_t j = 0; j < lines; j++) {
        starts[j] = n;
        for (size_t k = 0; k < j % 13u; k++) buf[n++] = ' ';
        size_t len = 1 + (j / 13u * 7u + j) % 40u;
        for (size_t k = 0; k < len; k++) buf[n++] = (k + j) % 6u == 0 ? ' ' : 'x';
        if (j + 1 < lines) buf[n++] = '\n';
    }
    starts[lines] = n + 1; /* one past the last line's (absent) newline */
    return n;
}
static void check_rows(const minimap *m, const uint8_t *p, size_t n, const uint64_t *starts,
                       uint64_t top, uint64_t label, uint32_t count)
{
    CHECK(m->active_rows == count);
    for (uint32_t i = 0; i < count; i++) {
        const minimap_row *r = &m->rows[i]; uint64_t want[2];
        model_marks(p, n, (size_t)starts[top + i], want);
        if (r->first_byte != starts[top + i] || r->marks[0] != want[0] || r->marks[1] != want[1])
            fprintf(stderr, "row %u: byte %llu want %llu (line %llu)\n", i,
                    (unsigned long long)r->first_byte, (unsigned long long)starts[top + i],
                    (unsigned long long)(top + i));
        CHECK(r->first_byte == starts[top + i] && r->line_count == 1);
        CHECK(r->first_line == label + i);
        CHECK(r->marks[0] == want[0] && r->marks[1] == want[1]);
    }
}
static void window_lines_exact(void)
{
    enum { LINES = 200000, ROWS = 705, H = 1410, V = 45, AT = 150000, FRAG = 16 };
    uint8_t *buf = malloc((size_t)LINES * 53u); uint64_t *starts = malloc((LINES + 1u) * sizeof *starts);
    minimap_row *rows = malloc(ROWS * sizeof *rows); CHECK(buf && starts && rows);
    size_t n = region_lines(buf, LINES, starts);
    counted f = {buf, n, FRAG, 0, 0, UINT64_MAX, 0};
    minimap_input in = {{&f, n, counted_span, NULL}, LINES, 7, true};
    minimap_anchor a = {.first_line = AT, .first_byte = starts[AT]};
    CHECK(minimap_window_lines(LINES, V, AT, 2, H, &a.window) == 0 && a.window.scrolls);
    minimap m; CHECK(minimap_init(&m, rows, ROWS) == 0);
    edit_malloc_guard_begin();
    int rc = minimap_prepare_window(&m, &in, NULL, &a, ROWS);
    CHECK(edit_malloc_guard_end() == 0);
    CHECK(rc == MINIMAP_OK && m.filled && !m.stale && m.windowed && m.window_exact && m.sampled);
    uint32_t count = a.window.rows < ROWS ? a.window.rows : ROWS;
    check_rows(&m, buf, n, starts, a.window.first_line, a.window.first_line, count);
    CHECK(m.rows[a.window.above].first_line == AT); /* the box's row is the viewport's line */
    CHECK(!minimap_stale(&m, &in) && !minimap_anchor_moved(&m, &a));
    /* (P) bounded reads: walk_back = above lines, every line <= 52 bytes. */
    uint64_t walk = AT - a.window.first_line, longest = 52;
    uint64_t bound = (walk + 1) * (longest + MINIMAP_LINE_CAP + 2) + count * (longest + 1) + FRAG;
    printf("edit-cxho.9 (M): window %u rows, walk back %llu, delivered %llu bytes of %zu, bound %llu, span calls %zu\n",
           count, (unsigned long long)walk, (unsigned long long)f.delivered, n,
           (unsigned long long)bound, f.calls);
    CHECK(f.delivered <= bound);
    CHECK(f.lo + MINIMAP_LINE_CAP + 1 >= starts[a.window.first_line]);
    CHECK(f.hi <= starts[a.window.first_line + count] + FRAG);
    /* The window moves with the viewport: a different anchor is a move. */
    minimap_anchor b = a; b.first_line++; CHECK(minimap_anchor_moved(&m, &b));
    /* Worker source and UI identity, as minimap_prepare_source. */
    counted g = f; g.delivered = 0; lineidx_src snap = {&g, n, counted_span, NULL};
    minimap_row wrows[ROWS]; minimap w; CHECK(minimap_init(&w, wrows, ROWS) == 0);
    CHECK(minimap_prepare_window(&w, &in, &snap, &a, ROWS) == 0 && g.delivered > 0);
    CHECK(w.identity.ctx == &f && !minimap_stale(&w, &in));
    /* The legacy whole-map prepare rows are each a real line too: the
     * window at line 0 (no sampled groups). */
    CHECK(minimap_prepare(&m, &in, ROWS) == 0);
    check_rows(&m, buf, n, starts, 0, 0, ROWS);
    minimap_target hit; uint32_t back;
    CHECK(minimap_hit(&m, &in, NULL, 300, &hit) == 0 && hit.exact && hit.line == 300 && hit.byte == starts[300]);
    CHECK(minimap_row_for_line(&m, 300, &back) == 0 && back == 300);
    minimap_fini(&m); free(rows); free(starts); free(buf);
    puts("edit-cxho.9: window at line 150000: row i is line first+i, bounded reads: ok");
}
/* A 640943-byte file with markdown-like line lengths 0..119 (some longer
 * than the summary width): ten 64 KiB chunks. No two adjacent rows may be
 * identical copies where their lines differ. */
static void no_stripes(void)
{
    enum { N = 640943, ROWS = 705, H = 1410, V = 45 };
    uint8_t *buf = malloc(N); uint64_t *starts = malloc((N + 2u) * sizeof *starts);
    minimap_row *rows = malloc(ROWS * sizeof *rows); CHECK(buf && starts && rows);
    size_t lines = 0, n = 0;
    while (n < N) {
        starts[lines] = n;
        size_t len = (lines * 37u) % 120u;
        for (size_t k = 0; k < len && n < N; k++) buf[n++] = k % 9u == 4u ? ' ' : (uint8_t)('a' + (lines + k) % 26u);
        if (n < N) buf[n++] = '\n';
        lines++;
    }
    if (buf[N - 1] == '\n') starts[lines++] = N;
    starts[lines] = N + 1;
    CHECK(N / LINEIDX_CHUNK + 1 == 10);
    flat f = {buf, N, 0, 0};
    minimap_input in = {{&f, N, span, NULL}, lines, 1, true};
    minimap m; CHECK(minimap_init(&m, rows, ROWS) == 0);
    CHECK(minimap_prepare(&m, &in, ROWS) == 0);
    for (int pass = 0; pass < 2; pass++) {
        uint64_t top = m.rows[0].first_line;
        check_rows(&m, buf, N, starts, top, top, m.active_rows);
        for (uint32_t i = 0; i + 1 < m.active_rows; i++) {
            uint64_t x[2], y[2];
            model_marks(buf, N, (size_t)starts[top + i], x); model_marks(buf, N, (size_t)starts[top + i + 1], y);
            if (x[0] != y[0] || x[1] != y[1])
                CHECK(m.rows[i].marks[0] != m.rows[i + 1].marks[0] || m.rows[i].marks[1] != m.rows[i + 1].marks[1]);
        }
        minimap_anchor a = {.first_line = lines / 2, .first_byte = starts[lines / 2]};
        CHECK(minimap_window_lines(lines, V, lines / 2, 2, H, &a.window) == 0);
        CHECK(minimap_prepare_window(&m, &in, NULL, &a, ROWS) == 0);
        CHECK(m.rows[0].first_line == a.window.first_line && m.active_rows == a.window.rows);
    }
    minimap_fini(&m); free(rows); free(starts); free(buf);
    puts("edit-cxho.9: 640943-byte file (10 chunks): adjacent rows are their own lines, no stripes: ok");
}
/* Index building: the byte-fraction window anchors mid-line, snaps to the
 * line start and walks `above` lines back; rows carry the caller's label. */
static void window_unindexed(void)
{
    enum { LINES = 200000, ROWS = 705, H = 1410, V = 45, AT = 150000, LABEL = 149000 };
    uint8_t *buf = malloc((size_t)LINES * 53u); uint64_t *starts = malloc((LINES + 1u) * sizeof *starts);
    minimap_row *rows = malloc(ROWS * sizeof *rows); CHECK(buf && starts && rows);
    size_t n = region_lines(buf, LINES, starts);
    flat f = {buf, n, 5, 0};
    minimap_input in = {{&f, n, span, NULL}, 123, 3, false}; /* lines: a stale estimate */
    minimap_anchor a = {.first_line = LABEL, .first_byte = starts[AT] + 3};
    CHECK(buf[a.first_byte - 1] != '\n');
    CHECK(minimap_window_bytes(n, V, a.first_byte, 2, H, &a.window) == 0 && a.window.above > 0);
    minimap m; CHECK(minimap_init(&m, rows, ROWS) == 0);
    CHECK(minimap_prepare_window(&m, &in, NULL, &a, ROWS) == 0);
    CHECK(m.windowed && !m.window_exact && m.filled);
    uint32_t count = a.window.rows < ROWS ? a.window.rows : ROWS;
    check_rows(&m, buf, n, starts, AT - a.window.above, LABEL - a.window.above, count);
    CHECK(m.rows[a.window.above].first_byte == starts[AT]); /* snapped */
    CHECK(!minimap_stale(&m, &in) && !minimap_anchor_moved(&m, &a));
    in.lines = 124; CHECK(!minimap_stale(&m, &in)); /* the estimate may move */
    in.revision++; CHECK(minimap_stale(&m, &in)); in.revision--;
    in.index_ready = true; in.lines = LINES; CHECK(minimap_stale(&m, &in)); /* exact replaces it */
    in.index_ready = false;
    /* Anchor on line 0's newline (byte 1): snaps to byte 0, no lines above. */
    a.first_byte = 1; a.first_line = 0; CHECK(buf[1] == '\n');
    CHECK(minimap_window_bytes(n, V, 1, 2, H, &a.window) == 0 && a.window.above == 0);
    CHECK(minimap_prepare_window(&m, &in, NULL, &a, ROWS) == 0);
    count = a.window.rows < ROWS ? a.window.rows : ROWS;
    check_rows(&m, buf, n, starts, 0, 0, count);
    /* UI fill paints the fresh unindexed window without reading the source. */
    render_cell cells[ROWS * 2]; uint64_t dirty[(ROWS + 63) / 64]; render_grid g;
    CHECK(render_grid_init(&g, (render_dims){2, ROWS, 1, 1}, cells, ROWS * 2, dirty, (ROWS + 63) / 64) == 0);
    CHECK(render_frame_begin(&g, 1) == 0);
    size_t calls = f.calls;
    CHECK(minimap_fill_cached(&m, &in, &g, 0, 2, 0, V, &style) == 0 && !m.stale && f.calls == calls);
    minimap_fini(&m); free(rows); free(starts); free(buf);
    puts("edit-cxho.9: unindexed byte anchor snaps to line starts, walks above lines back: ok");
}
/* A line longer than MINIMAP_SCAN_BYTES ends the walk: bounded reads. */
static void window_long_line(void)
{
    size_t big = (size_t)MINIMAP_SCAN_BYTES + 10u, n = 2 + big + 1 + 2;
    uint8_t *buf = malloc(n); CHECK(buf);
    buf[0] = 'a'; buf[1] = '\n'; memset(buf + 2, 'x', big); buf[2 + big] = '\n';
    buf[3 + big] = 'b'; buf[4 + big] = '\n';
    counted f = {buf, n, 16, 0, 0, UINT64_MAX, 0};
    minimap_input in = {{&f, n, counted_span, NULL}, 4, 1, true};
    minimap_row rows[8]; minimap m; CHECK(minimap_init(&m, rows, 8) == 0);
    minimap_anchor a = {.first_line = 0, .first_byte = 0};
    CHECK(minimap_window_lines(4, 2, 0, 1, 8, &a.window) == 0 && a.window.rows == 4);
    CHECK(minimap_prepare_window(&m, &in, NULL, &a, 8) == 0);
    CHECK(m.active_rows == 2 && rows[1].first_byte == 2 && rows[1].marks[0] == UINT64_MAX);
    CHECK(rows[2].line_count == 0);
    CHECK(f.hi <= 2 + MINIMAP_SCAN_BYTES + 16);
    minimap_fini(&m); free(buf);
    puts("edit-cxho.9: a line over MINIMAP_SCAN_BYTES ends the walk: ok");
}
/* edit-cxho.10 (P): a 4-line source "a\nbb\n\nc" prepared as one row per
 * line (window at line 0, scale 1, 8 rows), painted at scale 2 with line 0
 * placed at y = 3, so line L's marks sit on scanline 3 + 2L; the box is
 * 4 px at y = 5 (lines 1 and 2). Strip 4 px wide, 12 px tall. Pending: the
 * same frame for a 2-line source inks only scanlines 3..6. */
static void window_paint(void)
{
    enum { W = 4, H = 12 };
    static const uint8_t src[] = "a\nbb\n\nc";
    flat f = {src, sizeof src - 1, 0, 0};
    minimap_input in = {{&f, sizeof src - 1, span, NULL}, 4, 1, true};
    minimap_row rows[8]; minimap m; CHECK(minimap_init(&m, rows, 8) == 0);
    minimap_anchor a = {0};
    CHECK(minimap_window_lines(4, 0, 0, 1, 8, &a.window) == 0);
    CHECK(minimap_prepare_window(&m, &in, NULL, &a, 8) == 0 && m.active_rows == 4);
    uint32_t px[W * H];
    minimap_frame fr = {.line_y = 3, .line = 0, .lines = 4, .box_top = 5, .box_height = 4, .scale = 2};
    CHECK(minimap_pixels_window(&m, px, W * H, W, H, W, &fr, &style) == 0);
    uint32_t bg = style.background, ink = style.density;
    for (uint32_t y = 0; y < H; y++) for (uint32_t x = 0; x < W; x++) {
        bool band = y >= 5 && y < 9, edge = band && (y == 5 || y == 8 || x == 0 || x == W - 1);
        bool mark = (y == 3 && x == 0) || (y == 5 && x < 2) || (y == 9 && x == 0);
        uint32_t got = px[y * W + x];
        if (edge) CHECK(got != bg && got != ink && got != px[6 * W + 1]);
        else if (band) CHECK(got == px[6 * W + 1] && got != bg);
        else CHECK(got == (mark ? ink : bg));
    }
    CHECK(px[5 * W + 1] != px[5 * W + 2]);          /* marked edge pixel vs plain edge pixel */
    /* Rows far outside the strip are skipped; a label offset moves them. */
    fr.line = 2; fr.line_y = 0; /* line 2 at y 0: line 3 at y 2, lines 0/1 above */
    fr.box_height = 0;
    CHECK(minimap_pixels_window(&m, px, W * H, W, H, W, &fr, &style) == 0);
    CHECK(px[0] == bg && px[2 * W] == ink && px[3 * W] == bg);
    /* Pending: placeholder ink only on lines 0..1 (scanlines 3..6). */
    minimap_frame pend = {.line_y = 3, .line = 0, .lines = 2, .box_top = 100, .box_height = 4, .scale = 2, .pending = true};
    CHECK(minimap_pixels_window(NULL, px, W * H, W, H, W, &pend, &style) == 0);
    for (uint32_t y = 0; y < H; y++) {
        CHECK(px[y * W + W - 1] == style.stale);
        CHECK((px[y * W] != bg) == (y >= 3 && y < 7));
    }
    CHECK(minimap_pixels_window(NULL, px, W * H, W, H, W, &fr, &style) == MINIMAP_ERR_ARG);
    fr.scale = 0; CHECK(minimap_pixels_window(&m, px, W * H, W, H, W, &fr, &style) == MINIMAP_ERR_ARG);
    fr.scale = 2; CHECK(minimap_pixels_window(&m, px, W * H - 1, W, H, W, &fr, &style) == MINIMAP_ERR_CAPACITY);
    minimap_fini(&m);
    puts("edit-cxho.10: window-geometry pixels, box from the caller, bounded pending ink: ok");
}
int main(void)
{
    window_paint();
    window_mapping();
    window_lines_exact(); no_stripes(); window_unindexed(); window_long_line();
    character_marks(); viewport_geometry(); pixel_geometry(); dump_map();
    deferred_samples();
    pending_navigation(); pending_one_line();
    cache_identity();
    uint8_t mixed[600];
    for (size_t i = 0; i < sizeof mixed; i++) mixed[i] = i%99 == 98 ? '\n' : i%7 == 0 ? '\t' : i%11 == 0 ? ' ' : i%3 == 0 ? 0xff : 'x';
    static const uint8_t text[] = "  ab\t c\r\n\nxyz\n\xc3\xa9 \v\f\nlast";
    for (uint32_t h = 1; h <= 32; h++) {
        small_case(mixed, sizeof mixed, h, 17);
        small_case(mixed, sizeof mixed, h, 0);
        small_case(text, sizeof text - 1, h, 1);
        small_case(text, sizeof text - 1, h, 0);
        small_case((const uint8_t *)"", 0, h, 1);
        small_case((const uint8_t *)"\n\n", 2, h, 1);
    }
    large_and_errors(); exact_edit(); sample_cap_and_resize();
    puts("minimap_test: density, hit round trip, stale, malloc guard: ok"); return 0;
}
