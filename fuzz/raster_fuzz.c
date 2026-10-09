/* P2.5: arbitrary dims/cells/attrs/atlas bytes -> SSE2 kernel == scalar
 * reference, with an overrun canary around the surface; plus partition
 * coverage. Cells are made valid by construction (wide pairs matched). */
#include "raster/raster.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct rd { const uint8_t *p; size_t n, i; } rd;
static uint32_t take(rd *r) { return r->n ? r->p[r->i++ % r->n] : 0; }

typedef struct stop_state {uint32_t checks, limit;} stop_state;
static bool stop_batch(void *u)
{
    stop_state *s = u;
    return ++s->checks >= s->limit;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8) return 0;
    rd r = {data, size, 0};
    render_dims d = {1 + take(&r) % 40, 1 + take(&r) % 3, 1 + take(&r) % 32, 1 + take(&r) % 32};
    if (take(&r) & 1u) d.cell_w = (take(&r) & 1u) ? 8u : 16u;
    /* Cross 256-pixel cancellation batches as well as ordinary font sizes. */
    if ((take(&r) & 15u) == 0) d.cell_w = 255u + take(&r);
    size_t n = (size_t)d.cols * d.rows;
    render_cell cells[120];
    uint8_t page0[32 * 32];
    render_atlas_page pg = {page0, sizeof page0, 32, 32, 32};
    render_glyph gl[8];
    for (size_t i = 0; i < sizeof page0; i++) page0[i] = data[(8 + i) % size] ^ (uint8_t)(i * (take(&r) & 1u));
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t w = 1 + take(&r) % 32, h = 1 + take(&r) % 32;
        gl[i] = (render_glyph){i, 0, take(&r) % (33 - w), take(&r) % (33 - h), w, h};
    }
    for (uint32_t row = 0; row < d.rows; row++)
        for (uint32_t c = 0; c < d.cols; c++) {
            render_cell *cell = &cells[(size_t)row * d.cols + c];
            if (c > 0 && (cell - 1)->attrs & RENDER_ATTR_WIDE_LEFT) {
                render_cell *l = cell - 1;
                *cell = (render_cell){0, RENDER_NO_SLOT, l->fg, l->bg,
                    (uint16_t)((l->attrs & ~RENDER_ATTR_WIDE_LEFT) | RENDER_ATTR_WIDE_RIGHT), 0};
                continue;
            }
            uint32_t b = take(&r) | (take(&r) << 8);
            uint16_t at = (uint16_t)(b & (RENDER_ATTR_BOLD | RENDER_ATTR_ITALIC | RENDER_ATTR_UNDERLINE |
                                           RENDER_ATTR_INVERSE | RENDER_ATTR_CURSOR | RENDER_ATTR_SELECTION));
            uint32_t slot = take(&r) % 9;
            uint32_t fg = take(&r) | take(&r) << 8 | take(&r) << 16, bg = take(&r) | take(&r) << 8 | take(&r) << 16;
            *cell = (render_cell){slot == 8 ? 0 : slot, slot == 8 ? RENDER_NO_SLOT : slot, fg, bg, at, 0};
            if (c + 1 < d.cols && (b & 0x8000u)) cell->attrs |= RENDER_ATTR_WIDE_LEFT;
        }
    raster_scene s = {d, cells, gl, 8, &pg, 1, (take(&r) & 1u) ? 0xff000000u : 0u};
    size_t stride = (size_t)d.cols * d.cell_w + take(&r) % 4;
    if (take(&r) & 1u) stride = (stride + 15u) & ~(size_t)15u;
    size_t px = stride * d.cell_h;
    uint32_t *a = malloc(px * 4), *b2 = malloc(px * 4);
    if (!a || !b2) abort();
    raster_palette palette = {0};
    for (uint32_t row = 0; row < d.rows; row++) {
        memset(a, 0xa5, px * 4); memset(b2, 0xa5, px * 4);
        raster_row_scalar(&s, a, stride, row);
        raster_row_sse2(&s, b2, stride, row);
        if (memcmp(a, b2, px * 4) != 0) abort();
        memset(b2, 0xa5, px * 4);
        raster_row_sse2_stream(&s, b2, stride, row);
        if (memcmp(a, b2, px * 4) != 0) abort();
        memset(b2, 0xa5, px * 4);
        raster_row_cached(&s, b2, stride, row, &palette);
        if (memcmp(a, b2, px * 4) != 0 || palette.rebuilds > RASTER_PALETTE_SLOTS) abort();
        memset(b2, 0xa5, px * 4);
        if (!raster_row_cached_cancellable(&s,b2,stride,row,&palette,NULL,NULL) ||
            memcmp(a,b2,px*4) != 0) abort();
        memset(b2,0xa5,px*4);
        stop_state stop = {0,1u+take(&r)%32u};
        bool done = raster_row_cached_cancellable(&s,b2,stride,row,&palette,stop_batch,&stop);
        if (done) { if (memcmp(a,b2,px*4) != 0) abort(); }
        else {
            if (stop.checks != stop.limit) abort();
            /* Independent work bound: between checks at most 256 pixel stores.
             * Guard padding must stay untouched even on an interrupted row. */
            size_t written = 0;
            for (size_t i=0;i<px;i++) if (b2[i] != 0xa5a5a5a5u) written++;
            if (written > (size_t)(stop.checks-1u)*256u) abort();
            for (uint32_t y=0;y<d.cell_h;y++)
                for (size_t x=(size_t)d.cols*d.cell_w;x<stride;x++)
                    if (b2[(size_t)y*stride+x] != 0xa5a5a5a5u) abort();
        }
    }
    free(a); free(b2);
    (void)n;
    /* partition: every ordinal exactly once */
    uint32_t total = take(&r) % 200, njobs = 1 + take(&r) % 8, prev = 0;
    for (uint32_t j = 0; j < njobs; j++) {
        uint32_t lo, hi;
        raster_partition(total, njobs, j, &lo, &hi);
        if (lo != prev || hi < lo) abort();
        prev = hi;
    }
    if (prev != total) abort();
    return 0;
}
