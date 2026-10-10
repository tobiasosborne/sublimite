/* Warm G7j partial-prefix seek -> correct null-backend viewport, then G3z
 * work proxy. Default exits nonzero on a miss or when a verdict is REFUSED (too few samples); --track records shared-box
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
#define JUMPS BENCH_INTERACTION_MIN_N
_Static_assert(JUMPS >= BENCH_INTERACTION_MIN_N, "G7j needs qualified samples");
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
/* Keep continuation/adoption/waiting inside the end-to-end request timer. */
static int resolve_viewport(scroll_state *s, lineidx *index, const lineidx_src *src, uint64_t deadline)
{
    scroll_resolver resolver = {0};
    int rc;
    do {
        rc = scroll_resolve_slice(s, &resolver, index, src, 0, 0);
        if (bench_now_ns() >= deadline) return -1;
    } while (rc == SCROLL_MORE);
    return rc;
}
/* The supplied log has fewer than 10^7 lines. Extend it by its own prefix
 * in private storage, preserving the log's line width rather than shortening
 * the seek. The binding target then scans more than a GiB of real log bytes. */
static int jump_fixture(char *path, size_t capacity)
{
    const char template[]="build/edit-scroll-jump-XXXXXX";
    if (capacity<sizeof template) return -1;
    memcpy(path,template,sizeof template);
    int source_fd=open("/tmp/edit-corpus/log_1g.txt",O_RDONLY|O_CLOEXEC);
    struct stat metadata;
    if (source_fd<0 || fstat(source_fd,&metadata) || metadata.st_size!=(off_t)1073741824) {
        if (source_fd>=0) close(source_fd);
        return -1;
    }
    int out=mkstemp(path);
    if (out<0) { close(source_fd); return -1; }
    uint8_t block[65536]; uint64_t total=UINT64_C(1073741824)+UINT64_C(134217728);
    for (uint64_t off=0;off<total;) {
        ssize_t got=pread(source_fd,block,sizeof block,(off_t)(off%UINT64_C(1073741824)));
        if (got<=0 || write(out,block,(size_t)got)!=got) { close(out); close(source_fd); (void)unlink(path); return -1; }
        off+=(uint64_t)got;
    }
    close(source_fd);
    if (fsync(out)) { close(out); (void)unlink(path); return -1; }
    return out;
}
int main(int argc, char **argv)
{
    bool track=false, probe=false, cold=false, indexed_guard=false;
    for (int arg=1;arg<argc;arg++) {
        if (!strcmp(argv[arg],"--track")) track=true;
        else if (!strcmp(argv[arg],"--probe")) probe=true;
        else if (!strcmp(argv[arg],"--cold")) cold=true;
        else if (!strcmp(argv[arg],"--indexed-guard")) indexed_guard=true;
        else return 2;
    }
    size_t population=probe ? 1u : JUMPS;
    setvbuf(stdout, NULL, _IOLBF, 0);
    char private_path[128]; int fd=jump_fixture(private_path,sizeof private_path); struct stat statbuf;
    if (fd<0 || fstat(fd,&statbuf)) {
        fputs("scroll_bench: UNAVAILABLE binding line-10^7 fixture\n",stderr); if (fd>=0) close(fd); return 2;
    }
    (void)unlink(private_path);
    void *mapping = mmap(NULL, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) return 2;
    source f = {mapping, (uint64_t)statbuf.st_size};
    lineidx_src src = {&f, f.size, span, NULL};
    uint64_t lines = count_lines(&f), target = UINT64_C(10000000);
    uint64_t expected = target_byte(&f, target);
    if (lines<=target || expected==UINT64_MAX) { fputs("scroll_bench: UNAVAILABLE target line 10^7\n",stderr); return 2; }
    printf("WORKLOAD scroll bytes=%" PRIu64 " target_line=%" PRIu64 " target_byte=%" PRIu64 " mapping=NEW indexed_guard=%d cold=%d samples=%zu (G)\n",f.size,target,expected,indexed_guard,cold,population);
    viewport v;
    if (viewport_init(&v, &f)) { viewport_free(&v); munmap(mapping, (size_t)f.size); return 2; }
    printf("scroll_bench: TRACK=%d warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps\n", track);
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    if (!pool || work_pool_init(pool, 1, 0)) return 2;
    uint64_t jump_values[JUMPS]; stamp jump_stamp = read_stamp();
    bench_samples jumps; bench_samples_init(&jumps, jump_values, JUMPS);
    lineidx *index = NULL;
    for (size_t i = 0; i < population; i++) {
        if (!indexed_guard || !index) {
            if (index) lineidx_destroy(index);
            index=lineidx_create(f.size);
            if (munmap(mapping,(size_t)f.size)) return 2;
            mapping=mmap(NULL,(size_t)f.size,PROT_READ,MAP_PRIVATE,fd,0);
            if (mapping==MAP_FAILED) return 2;
            f.bytes=mapping;
            piece_destroy(v.tree);
            piece_allocator allocator=piece_default_allocator(); v.tree=piece_create(&allocator);
            if (!v.tree || piece_init_mapped(v.tree,f.bytes,(size_t)f.size,NULL)) return 2;
        }
        if (cold && !indexed_guard) {
            if (madvise(mapping,(size_t)f.size,MADV_DONTNEED) || posix_fadvise(fd,0,(off_t)f.size,POSIX_FADV_DONTNEED)) return 2;
            size_t pages=((size_t)f.size+4095u)/4096u; unsigned char *resident=malloc(pages);
            if (!resident) return 2;
            bool unavailable=mincore(mapping,(size_t)f.size,resident)!=0;
            if (!unavailable) for (size_t page=0;page<pages;page++) if (resident[page]&1u) { unavailable=true; break; }
            free(resident);
            if (unavailable) { fputs("scroll_bench: UNAVAILABLE cold eviction not verified\n",stderr); return 2; }
        }
        if (!index) return 2;
        scroll_state s;
        if (scroll_init(&s, (scroll_config){ROWS, v.grid.dims.cell_h, 3}, (scroll_extent){f.size, f.size / 40 + 1, false})) return 2;
        uint64_t start = bench_now_ns();
        if (scroll_seek_line(&s, target)) return 2;
        uint64_t deadline = start + UINT64_C(10000000000);
        if (!indexed_guard || i == 0) {
            /* Every binding sample starts with a fresh unindexed source. */
            if (lineidx_seek_start_owned(index, pool, &src, target, 0)) return 2;
            lineidx_result result = {0, false};
            while (!lineidx_seek_result(index, &result)) {
                if (bench_now_ns() >= deadline) { lineidx_build_cancel(index); return 2; }
                (void)nanosleep(&(struct timespec){0, 100000}, NULL);
            }
            if (!result.exact || result.value != expected) return 2;
        }
        if (resolve_viewport(&s, index, &src, deadline) || produce_frame(&v, &s, lines)) return 2;
        uint64_t duration = bench_now_ns() - start;
        if (s.first_byte != expected || s.first_line != target || !correct_viewport(&v, &f, expected)) return 2;
        if (bench_add(&jumps, duration)) return 2;
        if (probe) printf("PROBE unindexed_owned_seek correct=1 ns=%" PRIu64 " (M)%s\n",duration,bench__tag_from_power(jump_stamp.power));
    }
    uint64_t g50=indexed_guard ? UINT64_C(1000000) : cold ? UINT64_C(250000000) : UINT64_C(30000000);
    uint64_t g99=indexed_guard ? UINT64_C(5000000) : cold ? UINT64_C(350000000) : UINT64_C(50000000);
    const char *jump_name=indexed_guard ? "scroll_indexed_null_frame_regression" : cold ? "G7j_cold_unindexed_line_10000000_null_frame" : "G7j_warm_unindexed_line_10000000_new_mapping_null_frame";
    bench_verdict_kind jump_verdict=bench_judge(&jumps,g50,g99,BENCH_INTERACTION_MIN_N);
    (void)bench_gate_report(jump_name,&jumps,g50,g99,BENCH_INTERACTION_MIN_N,track,jump_stamp.power,jump_stamp.load);
    scroll_state s;
    if (scroll_init(&s, (scroll_config){ROWS, v.grid.dims.cell_h, 3}, (scroll_extent){f.size, lines, true}) ||
        scroll_seek_line(&s, target - 20000) || resolve_viewport(&s, index, &src, bench_now_ns() + UINT64_C(10000000000))) return 2;
    uint64_t work_values[STEPS]; bench_samples work; bench_samples_init(&work, work_values, STEPS);
    stamp cadence_stamp = read_stamp();
    uint64_t maximum_ns = 0; size_t slow = 0;
    for (unsigned i = 0; i < STEPS; i++) {
        int32_t delta = (i % 7u == 0) ? 256 : (int32_t)(i % 61u) + 1;
        if (i >= STEPS / 2) delta = -delta;
        uint64_t start = bench_now_ns();
        if (scroll_wheel(&s, delta) || resolve_viewport(&s, index, &src, start + UINT64_C(10000000000)) || produce_frame(&v, &s, lines)) return 2;
        uint64_t duration = bench_now_ns() - start;
        if (duration > maximum_ns) maximum_ns = duration;
        if (duration > HALF_PERIOD_NS) slow++;
        (void)bench_add(&work, duration);
    }
    render_stats stats;
    if (render_backend_stats(&v.backend, &stats) || stats.presented_frames != population + STEPS ||
        !correct_viewport(&v, &f, s.first_byte)) return 2;
    printf("BENCH G3z_work_proxy (M)%s load1=%s power=%s n=%u p50_ns=%" PRIu64 " p99_ns=%" PRIu64 " max_ns=%" PRIu64 " over_T_half=%zu (G)max_ns<=%" PRIu64 " (G)over_T_half=0 TRACK=%d\n",
        bench__tag_from_power(cadence_stamp.power), cadence_stamp.load, cadence_stamp.power, STEPS,
        bench_p50(&work), bench_p99(&work), maximum_ns, slow, HALF_PERIOD_NS, track);
    int work_rc = bench_gate_report("G3z_null_work_proxy", &work, HALF_PERIOD_NS, HALF_PERIOD_NS,
                                    BENCH_INTERACTION_MIN_N, track, cadence_stamp.power, cadence_stamp.load);
    puts("G3z displayed_verdict=UNAVAILABLE: work_proxy is a CPU regression comparison plus synthetic null completion, not displayed cadence. Requires editor on_refresh(displayed frame ID, refresh sequence, completion time, immutable scroll plan and sidebar identity); see docs/decisions/P3.4.md. Renderer origin/clip wiring remains external.");
    lineidx_destroy(index);
    /* A cancelled seek must never become adoptable, including a fast worker
     * completion racing cancellation. Outside the descriptive sample timer. */
    index = lineidx_create(f.size);
    if (!index || lineidx_seek_start_owned(index, pool, &src, target, 0)) return 2;
    lineidx_build_cancel(index);
    lineidx_result cancelled;
    if (lineidx_seek_result(index, &cancelled)) return 2;
    lineidx_destroy(index);
    work_pool_shutdown(pool); free(pool); viewport_free(&v); munmap(mapping, (size_t)f.size); close(fd);
    {
        int rc = bench_merge_exit(bench_exit_code(jump_verdict, track), work_rc);
        if (!track && slow != 0) rc = 1;
        if (population<BENCH_INTERACTION_MIN_N) rc=bench_merge_exit(rc,3);
        return rc;
    }
}
