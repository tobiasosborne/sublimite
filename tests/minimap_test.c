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
static const minimap_style style = {0x101010, 0xffffff, 0x224466, 0x552200};
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
    in.index_ready = false; size_t calls = f.calls;
    CHECK(minimap_fill(&m, &in, &g, 3, 4, 0, 1, &style) == 0 && m.stale);
    CHECK(f.calls == calls);
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
    (void)lineidx_seek_line(idx,&src,UINT64_MAX,n); CHECK(lineidx_complete(idx));
    in.lines = lineidx_line_count(idx).value; in.index_ready = true; f.calls = 0;
    edit_malloc_guard_begin();
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale && m.sampled);
    CHECK(edit_malloc_guard_end() == 0);
    CHECK(f.calls <= 10*37); /* <= ceil(256/7) source calls per row */
    for (uint32_t r = 0; r < 10; r++) {
        size_t start = (size_t)rows[r].first_byte, end = start+256;
        if (end > n) end = n;
        size_t first = start;
        if (start) { while (first < end && bytes[first] != '\n') first++; if (first < end) first++; else first = start; }
        uint64_t ink = 0;
        for (size_t i = first; i < end && bytes[i] != '\n'; i++) if (!space(bytes[i])) ink++;
        if (ink > 80) ink = 80;
        CHECK(rows[r].density == ink*255/80);
        minimap_target target; uint32_t back;
        CHECK(minimap_hit(&m,&in,idx,r,&target) == 0 && target.exact);
        CHECK(target.byte == (r ? lineidx_line_to_byte(idx,&src,target.line).value : 0));
        uint64_t want = 0;
        for (size_t i = 0; i < target.byte; i++) want += bytes[i] == '\n';
        CHECK(want == target.line);
        CHECK(minimap_row_for_line(&m,target.line,&back) == 0 && back == r);
        uint32_t bg = model_blend(style.background,style.density,rows[r].density);
        if (r == 0) bg = model_blend(bg,style.viewport,128);
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
    (void)lineidx_refresh(idx,&src); CHECK(lineidx_complete(idx));
    in.lines = lineidx_line_count(idx).value; in.index_ready = true;
    CHECK(minimap_stale(&m,&in));
    CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale);
    CHECK(rows[0].density == 79*255/80);
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
    CHECK(edit_malloc_guard_end() == 0 && f.calls == MINIMAP_MAX_SAMPLES);
    for (uint32_t r = 0; r < m.active_rows; r++) {
        uint64_t group = (uint64_t)r*256/m.active_rows;
        uint64_t boundary = UINT64_C(16384)*group/256*LINEIDX_CHUNK;
        CHECK(rows[r].first_byte == boundary && rows[r].density == 63*255/80);
    }
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
    CHECK(cells[0].bg == model_blend(model_blend(style.background,style.density,64),style.stale,128));
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
int main(void)
{
    deferred_samples();
    pending_navigation();
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
