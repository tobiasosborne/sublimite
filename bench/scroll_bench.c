/* Warm G7j partial-prefix seek -> correct null-backend viewport, then G3z
 * work proxy. Default exits nonzero on a miss; --track records shared-box
 * observations without issuing a gate verdict. Never opens a display. */
#include "scroll/scroll.h"
#include "layout/layout.h"
#include "../bench/harness.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define COLS 80u
#define ROWS 24u
#define JUMPS 5u
#define STEPS 10000u
#define HALF_PERIOD_NS UINT64_C(4166666)
typedef struct source { const uint8_t *bytes; uint64_t size; } source;
static size_t span(void *ctx, uint64_t off, const uint8_t **out)
{
    source *f = ctx;
    if (off >= f->size) return 0;
    *out = f->bytes + off;
    return (size_t)(f->size - off);
}
typedef struct stamp { char power[32], load[32]; } stamp;
static stamp read_stamp(void)
{
    stamp s;
    bench_battery_status(s.power, sizeof s.power);
    memcpy(s.load, "unknown", 8);
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%31s", s.load) != 1) memcpy(s.load, "unknown", 8); fclose(f); }
    return s;
}
static void no_map_hook(void *ctx) { (void)ctx; }
static void no_timestamp(void *ctx, uint32_t id, uint64_t ns) { (void)ctx; (void)id; (void)ns; }
typedef struct viewport {
    piece_tree *tree;
    layout engine;
    render_grid grid;
    render_backend backend;
    void *backend_state;
    render_cell cells[ROWS * COLS];
    uint64_t dirty, row_bytes[ROWS];
    uint32_t row_used[ROWS], frame;
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_atlas_page page;
} viewport;
static int viewport_init(viewport *v, source *f)
{
    memset(v, 0, sizeof *v);
    piece_allocator alloc = piece_default_allocator();
    piece_map_hooks hooks = {NULL, no_map_hook, no_map_hook};
    v->tree = piece_create(&alloc);
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(15);
    if (!v->tree || !atlas || piece_init_mapped(v->tree, f->bytes, (size_t)f->size, &hooks) != PIECE_OK) return -1;
    render_dims dims = {COLS, ROWS, atlas->cell.cell_w, atlas->cell.cell_h};
    if (render_grid_init(&v->grid, dims, v->cells, ROWS * COLS, &v->dirty, 1) != RENDER_OK) return -1;
    layout_ascii_glyphs(atlas, v->glyphs);
    v->page = (render_atlas_page){atlas->pixels, atlas->pixels_len,
        (size_t)atlas->cell.cell_w * 95, atlas->cell.cell_w * 95, atlas->cell.cell_h};
    v->grid.pages = &v->page; v->grid.page_count = 1;
    v->grid.glyphs = v->glyphs; v->grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    layout_config cfg = {.tab_width = 4, .fg = 0xdddddd, .bg = 0x101010, .slice_clusters = 500};
    if (layout_init(&v->engine, &v->grid, &cfg, v->row_bytes, v->row_used) != LAYOUT_DONE ||
        render_null_backend(&v->backend) != RENDER_OK) return -1;
    v->backend_state = calloc(1, v->backend.info.state_size);
    if (!v->backend_state) return -1;
    render_config config = {.dims = dims, .max_width = COLS * dims.cell_w, .max_height = ROWS * dims.cell_h,
        .max_cells = ROWS * COLS, .max_glyphs = LAYOUT_ASCII_GLYPHS, .max_pages = 1,
        .max_atlas_bytes = atlas->pixels_len, .hooks = {no_timestamp, no_timestamp, NULL}};
    return render_backend_init(&v->backend, &config, v->backend_state, v->backend.info.state_size);
}
static void viewport_free(viewport *v)
{
    render_backend_shutdown(&v->backend);
    free(v->backend_state);
    if (v->tree) piece_destroy(v->tree);
}
static int produce_frame(viewport *v, const scroll_state *s, uint64_t lines)
{
    if (render_frame_begin(&v->grid, ++v->frame) != RENDER_OK ||
        layout_begin(&v->engine, v->tree, (layout_viewport){s->first_byte, s->first_line, 0, lines}) != LAYOUT_DONE) return -1;
    int rc;
    do { rc = layout_run(&v->engine); } while (rc == LAYOUT_MORE);
    render_strip strip = {0, ROWS};
    if (rc != LAYOUT_DONE || layout_approximate(&v->engine) || v->row_bytes[0] != s->first_byte ||
        render_backend_submit(&v->backend, &v->grid, &strip, 1) != RENDER_OK ||
        render_backend_present(&v->backend, v->frame) != RENDER_OK) return -1;
    return 0;
}
/* Scalar oracle, outside all timers. Also warms every mapped page. */
static uint64_t count_lines(const source *f)
{
    uint64_t lines = 1;
    for (uint64_t i = 0; i < f->size; i++) if (f->bytes[i] == '\n') lines++;
    return lines;
}
static uint64_t target_byte(const source *f, uint64_t line)
{
    if (!line) return 0;
    for (uint64_t i = 0; i < f->size; i++) if (f->bytes[i] == '\n' && --line == 0) return i + 1;
    return UINT64_MAX;
}
/* Independent oracle for all viewport row starts and ASCII cells. */
static bool correct_viewport(const viewport *v, const source *f, uint64_t byte)
{
    uint64_t pos = byte;
    for (uint32_t r = 0; r < ROWS; r++) {
        if (v->row_bytes[r] != pos) return false;
        uint32_t expected[COLS] = {0}; uint16_t attrs[COLS] = {0};
        uint64_t col = 0;
        while (pos < f->size && f->bytes[pos] != '\n') {
            uint8_t ch = f->bytes[pos++];
            if (ch >= 128) return false;
            if (ch == '\r' && pos < f->size && f->bytes[pos] == '\n') continue;
            if (ch == '\t') { col += 4 - col % 4; continue; }
            if (col < COLS && ch != ' ') {
                expected[col] = ch < 32 || ch == 127 ? '?' : ch;
                attrs[col] = ch < 32 || ch == 127 ? RENDER_ATTR_INVERSE : 0;
            }
            col++;
        }
        if (pos < f->size) pos++;
        for (uint32_t c = 0; c < COLS; c++) {
            const render_cell *cell = &v->cells[r * COLS + c];
            if (cell->glyph_index != expected[c] || cell->attrs != attrs[c] ||
                cell->atlas_slot != (expected[c] ? expected[c] - 32 : RENDER_NO_SLOT) ||
                cell->fg != 0xdddddd || cell->bg != 0x101010 || cell->reserved) return false;
        }
    }
    return true;
}
int main(int argc, char **argv)
{
    bool track = argc == 2 && strcmp(argv[1], "--track") == 0;
    if (argc > 1 && !track) { fprintf(stderr, "usage: scroll_bench [--track]\n"); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *path = "/tmp/edit-corpus/log_1g.txt";
    int fd = open(path, O_RDONLY); struct stat statbuf;
    if (fd < 0 || fstat(fd, &statbuf) || statbuf.st_size != (off_t)1073741824) {
        fprintf(stderr, "scroll_bench: corpus missing/wrong length\n"); if (fd >= 0) close(fd); return 2;
    }
    void *mapping = mmap(NULL, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return 2;
    source f = {mapping, (uint64_t)statbuf.st_size};
    lineidx_src src = {&f, f.size, span, NULL};
    uint64_t lines = count_lines(&f), target = (lines / 10) * 9 + (lines % 10) * 9 / 10;
    uint64_t expected = target_byte(&f, target);
    if (lines != 8947842 || target != 8053057 || expected == UINT64_MAX) return 2;
    viewport v;
    if (viewport_init(&v, &f)) { viewport_free(&v); munmap(mapping, (size_t)f.size); return 2; }
    printf("scroll_bench: TRACK=%d warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps\n", track);
    uint64_t jump_values[JUMPS]; stamp jump_stamps[JUMPS];
    bench_samples jumps; bench_samples_init(&jumps, jump_values, JUMPS);
    lineidx *index = NULL;
    for (unsigned i = 0; i < JUMPS; i++) {
        if (index) lineidx_destroy(index);
        index = lineidx_create(f.size);
        if (!index || lineidx_built_prefix(index) != 0) return 2;
        scroll_state s;
        if (scroll_init(&s, (scroll_config){ROWS, v.grid.dims.cell_h, 3}, (scroll_extent){f.size, f.size / 40 + 1, false})) return 2;
        jump_stamps[i] = read_stamp();
        uint64_t start = bench_now_ns();
        if (scroll_seek_line(&s, target)) return 2;
        /* Single synchronous partial-prefix request, stopping at the target
         * chunk. Its entire scan is charged. Editor integration must schedule
         * bounded work/cancellation; this benchmark does not invent an async API. */
        lineidx_result result = lineidx_seek_line(index, &src, target, f.size);
        if (!result.exact || scroll_resolve(&s, index, &src, 0) || produce_frame(&v, &s, lines)) return 2;
        uint64_t duration = bench_now_ns() - start;
        if (s.first_byte != expected || s.first_line != target || !correct_viewport(&v, &f, expected)) return 2;
        (void)bench_add(&jumps, duration);
        printf("JUMP (M)%s load1=%s power=%s sample=%u ns=%" PRIu64 " target=%" PRIu64 " byte=%" PRIu64 " prefix_chunks=%zu correct=1\n",
            bench__tag_from_power(jump_stamps[i].power), jump_stamps[i].load, jump_stamps[i].power,
            i + 1, duration, target, expected, lineidx_built_prefix(index));
    }
    uint64_t unsorted[JUMPS]; memcpy(unsorted, jump_values, sizeof unsorted);
    uint64_t p50 = bench_p50(&jumps), p99 = bench_p99(&jumps);
    bool jump_miss = p50 > 30000000 || p99 > 50000000;
    for (unsigned i = 0; i < JUMPS; i++) {
        if (unsorted[i] == p50) printf("BENCH G7j_warm p50_ns=%" PRIu64 " (M)%s load1=%s (G)<=30000000 TRACK=%d\n", p50, bench__tag_from_power(jump_stamps[i].power), jump_stamps[i].load, track);
        if (unsorted[i] == p99) printf("BENCH G7j_warm p99_ns=%" PRIu64 " (M)%s load1=%s (G)<=50000000 TRACK=%d\n", p99, bench__tag_from_power(jump_stamps[i].power), jump_stamps[i].load, track);
    }
    scroll_state s;
    if (scroll_init(&s, (scroll_config){ROWS, v.grid.dims.cell_h, 3}, (scroll_extent){f.size, lines, true}) ||
        scroll_seek_line(&s, target - 20000) || scroll_resolve(&s, index, &src, 0)) return 2;
    uint64_t work_values[STEPS]; bench_samples work; bench_samples_init(&work, work_values, STEPS);
    stamp cadence_stamp = read_stamp();
    uint64_t maximum_ns = 0; size_t slow = 0;
    for (unsigned i = 0; i < STEPS; i++) {
        int32_t delta = (i % 7u == 0) ? 256 : (int32_t)(i % 61u) + 1;
        if (i >= STEPS / 2) delta = -delta;
        uint64_t start = bench_now_ns();
        if (scroll_wheel(&s, delta) || scroll_resolve(&s, index, &src, 0) || produce_frame(&v, &s, lines)) return 2;
        uint64_t duration = bench_now_ns() - start;
        if (duration > maximum_ns) maximum_ns = duration;
        if (duration > HALF_PERIOD_NS) slow++;
        (void)bench_add(&work, duration);
    }
    render_stats stats;
    if (render_backend_stats(&v.backend, &stats) || stats.presented_frames != JUMPS + STEPS ||
        !correct_viewport(&v, &f, s.first_byte)) return 2;
    printf("BENCH G3z_work_proxy (M)%s load1=%s power=%s n=%u p50_ns=%" PRIu64 " p99_ns=%" PRIu64 " max_ns=%" PRIu64 " over_T_half=%zu (G)max_ns<=%" PRIu64 " (G)over_T_half=0 TRACK=%d\n",
        bench__tag_from_power(cadence_stamp.power), cadence_stamp.load, cadence_stamp.power, STEPS,
        bench_p50(&work), bench_p99(&work), maximum_ns, slow, HALF_PERIOD_NS, track);
    puts("G3z displayed refreshes: UNMEASURED; requires real display and editor wiring. Sub-row pixel origin is retained by scroll; null render currently draws integral rows.");
    lineidx_destroy(index); viewport_free(&v); munmap(mapping, (size_t)f.size);
    if (track) return 0;
    return jump_miss || slow != 0 ? 1 : 0;
}
