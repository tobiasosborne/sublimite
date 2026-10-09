/* Grid-side TRACK only. Other concurrent workers make these indicative. */
#include "render/render.h"
#include "font/font.h"
#include "base/base.h"
#include "harness.h"
#include <inttypes.h>
#include <stdio.h>

#define RENDER_BENCH_SAMPLES 10000u
#define RENDER_BENCH_WARMUP 32u

static int run_case(uint32_t px, bool typing)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(px);
    if (a == NULL) return 1;
    render_dims dims = {2880u / a->cell.cell_w, 1800u / a->cell.cell_h,
                        a->cell.cell_w, a->cell.cell_h};
    size_t cells_n = (size_t)dims.cols * dims.rows;
    size_t words_n = ((size_t)dims.rows + 63) / 64;
    size_t strips_n = ((size_t)dims.rows + 1) / 2;
    edit_arena arena;
    if (edit_arena_init(&arena, cells_n * sizeof(render_cell) + 4096) != 0) return 1;
    render_cell *cells = edit_arena_alloc(&arena, cells_n * sizeof *cells, _Alignof(render_cell));
    uint64_t *bits = edit_arena_alloc(&arena, words_n * sizeof *bits, _Alignof(uint64_t));
    render_strip *strips = edit_arena_alloc(&arena, strips_n * sizeof *strips, _Alignof(render_strip));
    render_grid grid;
    if (render_grid_init(&grid, dims, cells, cells_n, bits, words_n) != RENDER_OK) {
        fprintf(stderr, "render_bench: grid init failed\n"); edit_arena_free(&arena); return 1;
    }
    render_atlas_page page = {a->pixels,a->pixels_len,(size_t)dims.cell_w * 95,
                              dims.cell_w * 95,dims.cell_h};
    render_glyph glyphs[95];
    for (uint32_t slot = 0; slot < 95; slot++)
        glyphs[slot] = (render_glyph){32 + slot,0,slot * dims.cell_w,0,dims.cell_w,dims.cell_h};
    grid.pages = &page; grid.page_count = 1; grid.glyphs = glyphs; grid.glyph_count = 95;
    /* Touch ALL storage before sampling, also establishes retained typing cells. */
    for (size_t i = 0; i < cells_n; i++) cells[i] = (render_cell){0, RENDER_NO_SLOT, 0xffffff, 0, 0, 0};
    uint64_t values[RENDER_BENCH_SAMPLES]; bench_samples samples;
    bench_samples_init(&samples, values, RENDER_BENCH_SAMPLES);
    uint64_t checksum = 0;
    for (uint32_t iteration = 0; iteration < RENDER_BENCH_SAMPLES + RENDER_BENCH_WARMUP; iteration++) {
        uint32_t first_row = typing ? dims.rows / 2 : 0;
        uint32_t rows = typing ? 1 : dims.rows;
        size_t first = (size_t)first_row * dims.cols;
        size_t end = first + (size_t)rows * dims.cols;
        size_t count = 0;
        uint64_t start = bench_now_ns();
        int rc = render_frame_begin(&grid, iteration + 1);
        for (size_t i = first; i < end; i++) {
            cells[i] = (render_cell){32u + (uint32_t)(i % 95), (uint32_t)(i % 95),
                UINT32_C(0xffffff), iteration, (uint16_t)(iteration & RENDER_ATTR_UNDERLINE), 0};
        }
        /* Mark every written row, then compute runs rather than bypass via full flag. */
        if (rc == RENDER_OK) rc = render_mark_rows(&grid, first_row, rows);
        if (rc == RENDER_OK) rc = render_dirty_strips(&grid, strips, strips_n, &count);
        uint64_t elapsed = bench_now_ns() - start;
        if (rc != RENDER_OK || count != 1 || strips[0].first_row != first_row || strips[0].row_count != rows) {
            fprintf(stderr, "render_bench: fill/strips failed\n"); edit_arena_free(&arena); return 1;
        }
        checksum += cells[end - 1].bg + cells[first].atlas_slot + strips[0].row_count;
        if (iteration >= RENDER_BENCH_WARMUP) (void)bench_add(&samples, elapsed);
    }
    char name[64];
    (void)snprintf(name, sizeof name, "render_%u_px_%s_TRACK", px, typing ? "typing_row" : "full_grid");
    printf("TRACK px=%u grid=%ux%u cell=%ux%u written_cells=%zu bytes=%zu checksum=%" PRIu64 " tag=(M)%s loaded\n",
           px, dims.cols, dims.rows, dims.cell_w, dims.cell_h,
           typing ? (size_t)dims.cols : cells_n,
           (typing ? (size_t)dims.cols : cells_n) * sizeof(render_cell), checksum, bench_evidence_tag());
    (void)bench_report(name, &samples, 0, 0); /* TRACK: timing never fails */
    edit_arena_free(&arena);
    return 0;
}

int main(void)
{
    char status[32]; bench_battery_status(status, sizeof status);
    printf("power=%s %s; (M) indicative under concurrent builds\n", status, bench_evidence_tag());
    puts("G3 full frame 5.0 / 5.56 ms (G), perf s0.2; this bench measures only the grid side");
    if (run_case(15, false) || run_case(15, true) || run_case(30, false) || run_case(30, true)) return 1;
    return 0;
}
