/* layout bench (P3.1): viewport -> cell grid. Gate (G): 300 rows x 360 cols full
 * layout p50/p99 <= 150 us on ascii_code.c and log_1g.txt. unicode.txt,
 * malformed.txt, the 120-row screen and the one-row typing relayout are TRACK.
 * Args: [corpus-dir [name-filter]] (default /tmp/edit-corpus). Indicative under concurrent builds. */
#include "layout/layout.h"
#include "harness.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SAMPLES 2000u
#define STARTS 64u
#define GATE_NS 150000u

static int stub_glyph(void *ctx, const uint8_t *c, size_t n, uint32_t w, uint32_t *slot)
{
    (void)ctx; (void)c; (void)n; (void)w;
    *slot = '?' - 0x20u;
    return 0;
}
static void hold(void *c) { (void)c; }
static const piece_map_hooks hooks = { NULL, hold, hold };

static const char *only;
static int run(const char *dir, const char *file, uint32_t cols, uint32_t rows, bool gated, bool typing)
{
    if (only && !strstr(file, only)) return 0;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) { fprintf(stderr, "layout_bench: cannot open %s\n", path); return 1; }
    const uint8_t *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) return 1;
    for (off_t o = 0; o < st.st_size; o += 4096) (void)*(volatile const uint8_t *)(map + o);  /* warm page cache */
    piece_allocator al = piece_default_allocator();
    piece_tree *t = piece_create(&al);
    if (!t || piece_init_mapped(t, map, (size_t)st.st_size, &hooks) != PIECE_OK) return 1;

    const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
    render_cell *cells = malloc((size_t)cols * rows * sizeof *cells);
    uint64_t bits[8] = {0}, *row_byte = malloc(rows * sizeof *row_byte);
    uint32_t *row_used = malloc(rows * sizeof *row_used);
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_grid g;
    layout_config cfg = {0};
    static layout l;                         /* 16 KiB window: keep off the stack */
    if (!cells || !row_byte || !row_used) return 1;
    if (render_grid_init(&g, (render_dims){cols, rows, a->cell.cell_w, a->cell.cell_h}, cells,
                         (size_t)cols * rows, bits, 8) != RENDER_OK) return 1;
    layout_ascii_glyphs(a, glyphs);
    render_atlas_page page = {a->pixels, a->pixels_len, (size_t)a->cell.cell_w * 95, a->cell.cell_w * 95, a->cell.cell_h};
    g.pages = &page; g.page_count = 1; g.glyphs = glyphs; g.glyph_count = LAYOUT_ASCII_GLYPHS;
    cfg.tab_width = 4; cfg.gutter = true; cfg.fg = 0xdddddd; cfg.bg = 0x101010;
    cfg.gutter_fg = 0x808080; cfg.gutter_bg = 0x202020; cfg.glyph = stub_glyph;
    if (layout_init(&l, &g, &cfg, row_byte, row_used) != LAYOUT_DONE) return 1;

    uint64_t lines = piece_line_count(t), starts[STARTS], first_line[STARTS];
    /* the P1.3 stub kernel needs ~1 s per line lookup on a 1 GiB file: fewer starts there */
    uint32_t nstarts = st.st_size > (off_t)64 << 20 ? 4u : STARTS;
    for (uint32_t i = 0; i < nstarts; i++) {
        first_line[i] = (lines > 2u * rows) ? (lines / nstarts) * i : 0;
        starts[i] = piece_line_to_byte(t, first_line[i]);
    }
    static uint64_t vals[SAMPLES];
    bench_samples s; bench_samples_init(&s, vals, SAMPLES);
    uint32_t frame = 0; uint64_t sum = 0;
    for (uint32_t it = 0; it < SAMPLES + 64; it++) {
        uint32_t k = it % nstarts;
        if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
        uint64_t t0 = bench_now_ns();
        int rc = layout_begin(&l, t, (layout_viewport){starts[k], first_line[k], 0, lines});
        if (rc >= 0) rc = layout_run(&l);
        uint64_t dt = bench_now_ns() - t0;
        if (rc != LAYOUT_DONE) { fprintf(stderr, "layout_bench: rc=%d\n", rc); return 1; }
        if (!typing && it >= 64) (void)bench_add(&s, dt);
        sum += row_byte[rows - 1];
        if (typing) {
            uint64_t row = rows / 2, off = row_byte[row] + 1;
            if (it < 64 || row_byte[row] == LAYOUT_VOID_ROW) continue;
            uint8_t ch = 'x';
            if (piece_insert(t, off, &ch, 1) != PIECE_OK) return 1;
            if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
            t0 = bench_now_ns();
            rc = layout_edit(&l, off, 0, 1, 0, 0);
            if (rc >= 0) rc = layout_run(&l);
            dt = bench_now_ns() - t0;
            if (rc != LAYOUT_DONE) return 1;
            (void)bench_add(&s, dt);
            if (piece_delete(t, off, 1, NULL) != PIECE_OK) return 1;
            if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
            if (layout_edit(&l, off, 1, 0, 0, 0) < 0 || layout_run(&l) != LAYOUT_DONE) return 1;
        }
    }
    if (render_grid_validate(&g) != RENDER_OK && !typing) {
        fprintf(stderr, "layout_bench: grid invalid\n");
        return 1;
    }
    char name[96];
    snprintf(name, sizeof name, "layout_%s_%ux%u%s%s", file, cols, rows, typing ? "_typing_row" : "", gated ? "" : "_TRACK");
    int miss = bench_report(name, &s, gated ? GATE_NS : 0, gated ? GATE_NS : 0);
    if (sum == 1) puts("");
    piece_destroy(t); munmap((void *)map, (size_t)st.st_size); close(fd);
    free(cells); free(row_byte); free(row_used);
    return miss;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/edit-corpus";
    only = argc > 2 ? argv[2] : NULL;      /* optional file-name filter */
    setvbuf(stdout, NULL, _IOLBF, 0);
    char status[32]; bench_battery_status(status, sizeof status);
    printf("power=%s %s; (M) indicative under concurrent builds\n", status, bench_evidence_tag());
    printf("G: 300-row x 360-col full layout <= 150 us p50 and p99 (PLAN P3.1)\n");
    int miss = 0;
    miss |= run(dir, "ascii_code.c", 360, 300, true, false);
    miss |= run(dir, "log_1g.txt", 360, 300, true, false);
    (void)run(dir, "unicode.txt", 360, 300, false, false);
    (void)run(dir, "malformed.txt", 360, 300, false, false);
    (void)run(dir, "ascii_code.c", 360, 120, false, false);
    (void)run(dir, "log_1g.txt", 360, 120, false, false);
    (void)run(dir, "ascii_code.c", 360, 120, false, true);
    return miss;
}
