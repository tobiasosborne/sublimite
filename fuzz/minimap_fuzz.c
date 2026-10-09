/* Random line-length vectors, whitespace, source fragmentation and strips. */
#include "minimap/minimap.h"
#include <stdlib.h>
#include <string.h>
#define MUST(c) do { if (!(c)) __builtin_trap(); } while (0)
typedef struct flat { uint8_t *p; size_t n, fragment; } flat;
static size_t span(void *ctx, uint64_t off, const uint8_t **p)
{
    flat *f = ctx;
    if (off >= f->n) return 0;
    size_t n = f->n - (size_t)off;
    if (n > f->fragment) n = f->fragment;
    *p = f->p + off; return n;
}
static bool space(uint8_t c) { return c == ' ' || (c >= 9 && c <= 13); }
static uint64_t sample_model(const uint8_t *p, size_t n, size_t start)
{
    size_t end = start + MINIMAP_SAMPLE_BYTES;
    if (end > n) end = n;
    size_t begin = start;
    if (start) {
        while (begin < end && p[begin] != '\n') begin++;
        if (begin < end) begin++; else begin = start;
    }
    uint64_t ink = 0;
    for (size_t i = begin; i < end && p[i] != '\n'; i++) ink += !space(p[i]);
    return ink > 80 ? 80 : ink;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 5) return 0;
    uint32_t h = (uint32_t)data[0] * 2u + 1, w = data[1] % 8u + 1;
    size_t count = (size - 4) / 2; if (count > 512) count = 512;
    uint64_t starts[513], inks[513]; size_t n = 0;
    uint8_t *bytes = malloc(1049600); MUST(bytes);
    for (size_t j = 0; j < count; j++) {
        size_t len = ((size_t)data[4+j*2] * 256 + data[5+j*2]) % 2049;
        starts[j] = n; inks[j] = 0;
        for (size_t k = 0; k < len; k++) {
            uint8_t c = k % (data[2] % 17u + 1u) == 0 ? ' ' : (uint8_t)(128u + (k % 128));
            bytes[n++] = c; inks[j] += !space(c);
        }
        if (inks[j] > 80) inks[j] = 80;
        if (j + 1 < count || (data[3]&1)) bytes[n++] = '\n';
    }
    size_t lines = count;
    if (count == 0 || (data[3]&1)) { starts[lines] = n; inks[lines++] = 0; }
    flat f = {bytes,n,(size_t)(data[2] % 64u + 1u)};
    minimap_input in = {{&f,n,span,NULL},lines,1,true};
    render_cell cells[512*10]; uint64_t dirty[8]; minimap_row rows[512]; minimap m; render_grid g;
    MUST(minimap_init(&m,rows,512) == 0);
    MUST(render_grid_init(&g,(render_dims){w+2,h,1,1},cells,512*10,dirty,8) == 0);
    MUST(render_frame_begin(&g,1) == 0);
    memset(cells,0x42,sizeof cells);
    minimap_style style = {0,0xffffff,0x002244,0x440000};
    MUST(minimap_fill(&m,&in,&g,2,w,data[3],data[2],&style) == 0);
    uint64_t k = (lines + h - 1) / h;
    flat fast = {bytes,n,n ? n : 1};
    minimap_input hit_input = in;
    lineidx_src fast_src = in.source; fast_src.ctx = &fast;
    lineidx *idx = NULL;
    if (m.sampled) {
        idx = lineidx_create(n); MUST(idx);
        (void)lineidx_seek_line(idx,&fast_src,UINT64_MAX,n); MUST(lineidx_complete(idx));
    }
    for (uint32_t r = 0; r < h; r++) {
        uint64_t first = (uint64_t)r*k;
        if (first >= lines) { MUST(rows[r].line_count == 0 && rows[r].density == 0); continue; }
        uint64_t end = first+k; if (end > lines) end = lines;
        uint64_t ink = 0, den;
        if (!m.sampled) {
            for (uint64_t j = first; j < end; j++) ink += inks[j];
            den = ink*255/(80*(end-first));
            MUST(rows[r].first_byte == starts[first]);
        } else {
            size_t chunks = n/LINEIDX_CHUNK + (n%LINEIDX_CHUNK != 0);
            size_t active = (lines+k-1)/k;
            size_t samples = active < MINIMAP_MAX_SAMPLES ? active : MINIMAP_MAX_SAMPLES;
            if (chunks < samples) samples = chunks;
            size_t group = samples*r/active;
            size_t boundary = chunks*group/samples*LINEIDX_CHUNK;
            MUST(rows[r].first_byte == boundary);
            ink = sample_model(bytes,n,boundary); den = ink*255/80;
        }
        MUST(rows[r].density == den && rows[r].ink == ink && rows[r].first_line == first);
        minimap_target target; uint32_t back;
        MUST(minimap_hit(&m,&hit_input,idx,r,&target) == 0 && target.exact);
        MUST(target.line == first && target.byte == starts[first]);
        MUST(minimap_row_for_line(&m,target.line,&back) == 0 && back == r);
        MUST(cells[r*(w+2)].bg == 0x42424242);
    }
    in.revision++; MUST(minimap_stale(&m,&in));
    in.index_ready = false;
    MUST(minimap_fill(&m,&in,&g,2,w,0,0,&style) == 0 && m.stale);
    minimap_target target = {0}; MUST(minimap_hit(&m,&in,idx,0,&target) == MINIMAP_ERR_STALE);
    in.index_ready = true;
    MUST(minimap_fill(&m,&in,&g,2,w,0,0,&style) == 0 && !minimap_stale(&m,&in));
    minimap_row worker_rows[512]; minimap worker;
    MUST(minimap_init(&worker,worker_rows,512) == 0);
    MUST(minimap_prepare_source(&worker,&in,&fast_src,h) == 0);
    in.revision++; MUST(minimap_publish(&m,&in,&worker) == MINIMAP_ERR_STALE);
    MUST(minimap_fill_cached(&m,&in,&g,2,w,0,1,&style) == 0 && m.stale);
    MUST(minimap_hit_approx(&in,h,(int64_t)data[3]-128,&target) == 0);
    MUST(!target.exact && target.byte <= n && target.line < lines);
    in.revision--; MUST(minimap_publish(&m,&in,&worker) == 0);
    MUST(minimap_fill_cached(&m,&in,&g,2,w,0,1,&style) == 0 && !m.stale);
    /* Identity-only switch, with equal bytes/revision/count, cannot hit cache. */
    hit_input = in; hit_input.source.ctx = &fast;
    MUST(minimap_stale(&m,&hit_input));
    MUST(minimap_hit(&m,&hit_input,idx,0,&target) == MINIMAP_ERR_STALE);
    minimap_fini(&worker); minimap_fini(&m); lineidx_destroy(idx); free(bytes); return 0;
}
