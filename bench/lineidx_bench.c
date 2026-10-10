/* P1.6d: G7 new mapping -> exact index, G7j fresh index request -> correct
 * 80x24 viewport submitted/presented to the null backend. ASCII fixtures.
 * Usage: [--file=PATH] [--reps=3..200] [--track] [--cold]
 *        [--timeout-ms=1..600000] | --self-check[=15|16|17|18]
 * --cold: manual root eviction + mincore verification BEFORE EVERY sample.
 * Warm-only gated success exits 3 (cold unvalidated). --track never claims gates.
 * --partial was removed: G7j always starts with nothing built. */
#include "lineidx/lineidx.h"
#include "layout/layout.h"
#include "../bench/harness.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const uint8_t *b; uint64_t n; } flat;
static size_t flat_span(void *ctx, uint64_t off, const uint8_t **p)
{
    const flat *f = ctx;
    if (off >= f->n) return 0;
    *p = f->b + off;
    return (size_t)(f->n - off);
}

typedef struct fixture { const char *path; uint64_t n; } fixture;

static const uint8_t *map_file(fixture *file)
{
    int fd = open(file->path, O_RDONLY);
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (uintmax_t)st.st_size > SIZE_MAX ||
        (file->n && file->n != (uint64_t)st.st_size)) { close(fd); return NULL; }
    file->n = (uint64_t)st.st_size;
    void *m = mmap(NULL, (size_t)file->n, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return m == MAP_FAILED ? NULL : m;
}

static void nap(long us) { nanosleep(&(struct timespec){0, us * 1000}, NULL); }
static void ignore_message(const work_msg *msg, void *ud) { (void)msg; (void)ud; }
static void drain(work_pool *p) { (void)work_mailbox_drain(p, ignore_message, NULL); }

static bool wait_index_observed(lineidx *x, work_pool *p, uint64_t deadline,
                                bool complete, const char *label,
                                void (*after_poll)(lineidx *, void *), void *ctx)
{
    for (;;) {
        drain(p);
        (void)lineidx_poll(x);
        bool built = lineidx_complete(x);
        if (after_poll) after_poll(x, ctx);
        bool building = lineidx_building(x);
        /* Worker completion can race the first poll. Stopped does not mean
         * its final published batch has already been applied on UI. */
        if (complete && !built && !building) {
            (void)lineidx_poll(x);
            built = lineidx_complete(x);
        }
        if (complete ? built : !building) return true;
        if ((complete && !building) || bench_now_ns() >= deadline) {
            fprintf(stderr, "%s: completion failure prefix=%zu/%zu complete=%d building=%d deadline_expired=%d\n",
                    label, lineidx_built_prefix(x), lineidx_chunk_count(x), built, building,
                    bench_now_ns() >= deadline);
            return false;
        }
        nap(50);
    }
}

static bool wait_index(lineidx *x, work_pool *p, uint64_t deadline,
                       bool complete, const char *label)
{
    return wait_index_observed(x, p, deadline, complete, label, NULL, NULL);
}

/* Do not enter a possibly blocking destructor/join after a timeout. Process
 * exit keeps every source alive until the OS stops all workers. */
static void fail_fast(const char *reason)
{
    fprintf(stderr, "lineidx_bench: ERROR %s (exit 2)\n", reason);
    fflush(NULL);
    _Exit(2);
}

static void index_destroy(lineidx *x, work_pool *p, uint64_t timeout)
{
    if (!wait_index(x, p, bench_now_ns() + timeout, false, "retirement"))
        fail_fast("worker retirement failed");
    lineidx_destroy(x);
    drain(p);
}

#define VIEW_COLS 80u
#define VIEW_ROWS 24u

typedef struct viewport {
    piece_tree *tree;
    layout engine;
    render_grid grid;
    render_backend backend;
    void *state;
    render_cell cells[VIEW_COLS * VIEW_ROWS];
    uint64_t dirty, rows[VIEW_ROWS];
    uint32_t used[VIEW_ROWS];
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_atlas_page page;
} viewport;

static void hold_map(void *ctx) { (void)ctx; }
static void ignore_timestamp(void *ctx, uint32_t frame, uint64_t ns)
{ (void)ctx; (void)frame; (void)ns; }

/* All open-time allocations precede the request. The mapping outlives this tree. */
static int viewport_init(viewport *v, const flat *f)
{
    memset(v, 0, sizeof *v);
    piece_allocator alloc = piece_default_allocator();
    piece_map_hooks hooks = {NULL, hold_map, hold_map};
    v->tree = piece_create(&alloc);
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(15);
    if (!v->tree || !atlas || piece_init_mapped(v->tree, f->b, (size_t)f->n, &hooks) != PIECE_OK) return -1;
    render_dims dims = {VIEW_COLS, VIEW_ROWS, atlas->cell.cell_w, atlas->cell.cell_h};
    if (render_grid_init(&v->grid, dims, v->cells, VIEW_COLS * VIEW_ROWS, &v->dirty, 1) != RENDER_OK) return -1;
    layout_ascii_glyphs(atlas, v->glyphs);
    v->page = (render_atlas_page){atlas->pixels, atlas->pixels_len,
        (size_t)atlas->cell.cell_w * 95, atlas->cell.cell_w * 95, atlas->cell.cell_h};
    v->grid.pages = &v->page; v->grid.page_count = 1;
    v->grid.glyphs = v->glyphs; v->grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    layout_config cfg = {.tab_width = 4, .fg = 0xdddddd, .bg = 0x101010, .slice_clusters = 500};
    if (layout_init(&v->engine, &v->grid, &cfg, v->rows, v->used) != LAYOUT_DONE ||
        render_null_backend(&v->backend) != RENDER_OK) return -1;
    v->state = calloc(1, v->backend.info.state_size);
    if (!v->state) return -1;
    render_config config = {.dims = dims, .max_width = VIEW_COLS * dims.cell_w,
        .max_height = VIEW_ROWS * dims.cell_h, .max_cells = VIEW_COLS * VIEW_ROWS,
        .max_glyphs = LAYOUT_ASCII_GLYPHS, .max_pages = 1, .max_atlas_bytes = atlas->pixels_len,
        .hooks = {ignore_timestamp, ignore_timestamp, NULL}};
    return render_backend_init(&v->backend, &config, v->state, v->backend.info.state_size);
}

static void viewport_free(viewport *v)
{
    render_backend_shutdown(&v->backend);
    free(v->state);
    if (v->tree) piece_destroy(v->tree);
}

static int viewport_submit(viewport *v, uint64_t byte, uint64_t line, uint64_t lines, uint64_t deadline)
{
    if (render_frame_begin(&v->grid, 1) != RENDER_OK ||
        layout_begin(&v->engine, v->tree, (layout_viewport){byte, line, 0, lines}) != LAYOUT_DONE) return -1;
    int result;
    do {
        if (bench_now_ns() >= deadline) return -1;
        result = layout_run(&v->engine);
    } while (result == LAYOUT_MORE);
    render_strip strip = {0, VIEW_ROWS};
    if (result != LAYOUT_DONE || layout_approximate(&v->engine) || v->rows[0] != byte ||
        render_backend_submit(&v->backend, &v->grid, &strip, 1) != RENDER_OK ||
        render_backend_present(&v->backend, 1) != RENDER_OK) return -1;
    return 0;
}

typedef struct jump_observation {
    size_t prefix;
    bool viewport_submitted;
    uint64_t scanned_bytes, submitted_ns;
} jump_observation;

typedef struct counted_source {
    const lineidx_src *src;
    _Atomic uint64_t bytes;
} counted_source;

static size_t counted_span(void *ctx, uint64_t off, const uint8_t **p)
{
    counted_source *counted = ctx;
    size_t count = counted->src->span(counted->src->ctx, off, p);
    if (count > LINEIDX_CHUNK) count = LINEIDX_CHUNK;
    atomic_fetch_add_explicit(&counted->bytes, count, memory_order_relaxed);
    return count;
}

/* A fresh, entirely unbuilt index; the request starts the bulk worker. There is
 * no worker seek API yet, so this path builds the full index before laying out.
 * This deliberately charges that extra work to the G7j end-to-end interval. */
static lineidx_result jump_request(lineidx *x, work_pool *p, uint64_t timeout, const lineidx_src *s,
                                  uint64_t target, viewport *v,
                                  jump_observation *seen)
{
    seen->prefix = lineidx_built_prefix(x);
    if (seen->prefix != 0 || lineidx_building(x)) return (lineidx_result){0, false};
    counted_source counted = {.src = s, .bytes = 0};
    lineidx_src worker_src = {&counted, s->len, counted_span, NULL};
    uint64_t deadline = bench_now_ns() + timeout;
    if (lineidx_build_start(x, p, &worker_src) != 0 || !wait_index(x, p, deadline, true, "G7j"))
        fail_fast("jump worker submission/completion failed");
    seen->scanned_bytes = atomic_load_explicit(&counted.bytes, memory_order_relaxed);
    lineidx_result answer = lineidx_line_to_byte(x, s, target);
    if (!answer.exact || seen->scanned_bytes != s->len ||
        viewport_submit(v, answer.value, target, lineidx_line_count(x).value, deadline) != 0)
        return (lineidx_result){0, false};
    seen->submitted_ns = bench_now_ns();
    seen->viewport_submitted = true;
    if (!wait_index(x, p, deadline, false, "G7j source retirement")) fail_fast("jump source retirement failed");
    return answer;
}

typedef struct oracle { uint64_t lines, target, byte; } oracle;

/* Scalar oracle: independent of lineidx and scan_count, outside every timer.
 * Count once; the second walk locates the 0-based target using that count. */
static int reference_metadata(const flat *f, const lineidx_src *source, oracle *out)
{
    (void)source;
    out->lines = 1;
    for (uint64_t i = 0; i < f->n; i++) out->lines += f->b[i] == '\n';
    out->target = (out->lines / 10) * 9 + (out->lines % 10) * 9 / 10;
    out->byte = 0;
    uint64_t line = 0;
    for (uint64_t i = 0; line < out->target && i < f->n; i++) {
        if (f->b[i] == '\n') { line++; out->byte = i + 1; }
    }
    return line == out->target ? 0 : -1;
}

static bool fixture_valid(uint64_t bytes, const oracle *o)
{
    return bytes == UINT64_C(1073741824) && o->lines == UINT64_C(8947842) &&
           o->target == UINT64_C(8053057) && o->byte < bytes;
}

static bool index_correct(lineidx *x, const oracle *expected)
{
    lineidx_result count = lineidx_line_count(x);
    return count.exact && count.value == expected->lines;
}

/* Independent ASCII cell oracle. Non-ASCII viewport fixtures are refused by
 * this bench rather than treating the layout's own grid as its reference. */
static bool viewport_correct(const viewport *v, const flat *f, const oracle *o)
{
    uint64_t pos = o->byte;
    for (uint32_t row = 0; row < VIEW_ROWS; row++) {
        uint64_t expected_row = o->target + row < o->lines ? pos : LAYOUT_VOID_ROW;
        if (v->rows[row] != expected_row) return false;
        uint64_t col = 0;
        uint32_t chars[VIEW_COLS] = {0};
        uint16_t attrs[VIEW_COLS] = {0};
        while (pos < f->n && f->b[pos] != '\n') {
            uint8_t ch = f->b[pos++];
            if (ch >= 0x80) return false;
            if (ch == '\r' && pos < f->n && f->b[pos] == '\n') continue;
            if (ch == '\t') { col += 4 - col % 4; continue; }
            if (col < VIEW_COLS && ch != ' ') {
                chars[col] = ch < 0x20 || ch == 0x7f ? '?' : ch;
                attrs[col] = ch < 0x20 || ch == 0x7f ? RENDER_ATTR_INVERSE : 0;
            }
            col++;
        }
        if (pos < f->n) pos++;
        for (uint32_t colno = 0; colno < VIEW_COLS; colno++) {
            const render_cell *cell = &v->cells[row * VIEW_COLS + colno];
            uint32_t slot = chars[colno] ? chars[colno] - 0x20 : RENDER_NO_SLOT;
            if (cell->glyph_index != chars[colno] || cell->atlas_slot != slot ||
                cell->attrs != attrs[colno] || cell->fg != 0xdddddd || cell->bg != 0x101010 || cell->reserved) return false;
        }
    }
    return true;
}

static int self_check_15(void)
{
    size_t n = 2u * LINEIDX_CHUNK;
    uint8_t *bytes = malloc(n);
    if (!bytes) return 2;
    memset(bytes, 'x', n);
    for (size_t i = 79; i < n; i += 80) bytes[i] = '\n';
    flat f = {bytes, n};
    lineidx_src src = {&f, n, flat_span, NULL};
    work_pool pool;
    if (work_pool_init(&pool, 1, 0) != 0) return 2;
    lineidx *x = lineidx_create(n);
    jump_observation seen = {0};
    viewport v;
    if (!x || viewport_init(&v, &f) != 0) fail_fast("viewport/index open setup failed");
    lineidx_result answer = jump_request(x, &pool, 1000000000ull, &src, 1400, &v, &seen);
    jump_observation reused = {0};
    bool rejects_built = !jump_request(x, &pool, 1000000000ull, &src, 1400, &v, &reused).exact;
    oracle expected = {n / 80 + 1, 1400, 112000};
    bool cells = viewport_correct(&v, &f, &expected);
    v.cells[0].glyph_index ^= 1;
    bool rejects_corrupt = !viewport_correct(&v, &f, &expected);
    int fail = !answer.exact || answer.value != 112000 || seen.prefix != 0 ||
        !seen.viewport_submitted || seen.scanned_bytes != n || !cells || !rejects_corrupt ||
        !rejects_built || seen.submitted_ns < v.backend.submitted_ns;
    printf("SELF_CHECK 15 %s fresh_prefix=%zu viewport_submitted=%d\n",
           fail ? "FAIL" : "PASS", seen.prefix, seen.viewport_submitted);
    index_destroy(x, &pool, 1000000000ull);
    viewport_free(&v);
    work_pool_shutdown(&pool);
    free(bytes);
    return fail;
}

static size_t no_newline_span(void *ctx, uint64_t off, const uint8_t **p)
{
    flat *f = ctx;
    if (off >= f->n) return 0;
    *p = (const uint8_t *)"xxxxxxxx";
    return 1;
}

static int self_check_16(void)
{
    const uint8_t data[] = "a\nbb\nc\n";
    flat f = {data, sizeof data - 1};
    lineidx_src faulty = {&f, f.n, no_newline_span, NULL};
    work_pool pool;
    if (work_pool_init(&pool, 1, 0) != 0) return 2;
    oracle o = {0};
    int result = reference_metadata(&f, &faulty, &o);
    bool independent = result == 0 && o.lines == 4 && o.target == 3 && o.byte == 7;
    bool rejects_tiny = !fixture_valid(f.n, &o);
    lineidx *x = lineidx_create(f.n);
    if (!x || lineidx_build_start(x, &pool, &faulty) != 0 ||
        !wait_index(x, &pool, bench_now_ns() + 1000000000ull, true, "oracle fault injection")) return 2;
    bool rejects_fault = !index_correct(x, &o);
    index_destroy(x, &pool, 1000000000ull);
    oracle wrong_manifest = {8947841, 8053057, 1};
    bool rejects_count = !fixture_valid(1073741824, &wrong_manifest);
    int fail = !independent || !rejects_tiny || !rejects_fault || !rejects_count;
    printf("SELF_CHECK 16 %s independent_oracle=%d rejects_tiny_fixture=%d\n",
           fail ? "FAIL" : "PASS", independent, rejects_tiny);
    work_pool_shutdown(&pool);
    return fail;
}

typedef struct options {
    const char *file;
    int reps;
    uint64_t timeout;
    bool cold, track;
} options;

static bool parse_uint(const char *text, uint64_t low, uint64_t high, uint64_t *value)
{
    if (*text < '0' || *text > '9') return false;
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed < low || parsed > high) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_options(int argc, char **argv, options *out)
{
    *out = (options){"/tmp/edit-corpus/log_1g.txt", 9, 30000000000ull, false, false};
    for (int i = 1; i < argc; i++) {
        uint64_t value = 0;
        if (strcmp(argv[i], "--cold") == 0) out->cold = true;
        else if (strcmp(argv[i], "--track") == 0) out->track = true;
        else if (strncmp(argv[i], "--file=", 7) == 0 && argv[i][7]) out->file = argv[i] + 7;
        else if (strncmp(argv[i], "--reps=", 7) == 0 && parse_uint(argv[i] + 7, 3, 200, &value))
            out->reps = (int)value;
        else if (strncmp(argv[i], "--timeout-ms=", 13) == 0 && parse_uint(argv[i] + 13, 1, 600000, &value))
            out->timeout = value * 1000000;
        else return false;
    }
    return true;
}

typedef struct blocked_source { _Atomic bool entered, release; } blocked_source;
static size_t blocked_span(void *ctx, uint64_t off, const uint8_t **p)
{
    blocked_source *blocked = ctx;
    if (off) return 0;
    atomic_store_explicit(&blocked->entered, true, memory_order_release);
    while (!atomic_load_explicit(&blocked->release, memory_order_acquire)) nap(50);
    *p = (const uint8_t *)"x";
    return 1;
}

/* Force the worker's final publication between the waiter's poll and state
 * query. Its completed result still requires one final UI adoption. */
static void finish_after_poll(lineidx *x, void *ctx)
{
    blocked_source *blocked = ctx;
    atomic_store_explicit(&blocked->release, true, memory_order_release);
    uint64_t deadline = bench_now_ns() + 1000000000ull;
    while (lineidx_building(x)) {
        if (bench_now_ns() >= deadline) fail_fast("late-publication self-check worker did not finish");
        nap(50);
    }
}

static int self_check_completion_race(void)
{
    work_pool pool;
    if (work_pool_init(&pool, 1, 0) != 0) return 2;
    lineidx *x = lineidx_create(1);
    blocked_source blocked = {.entered = false, .release = false};
    lineidx_src src = {&blocked, 1, blocked_span, NULL};
    if (!x || lineidx_build_start(x, &pool, &src) != 0) return 2;
    bool adopted = wait_index_observed(x, &pool, bench_now_ns() + 1000000000ull,
                                      true, "final-publication race", finish_after_poll, &blocked);
    /* A final poll here also makes cleanup safe in the red test. */
    (void)lineidx_poll(x);
    index_destroy(x, &pool, 1000000000ull);
    work_pool_shutdown(&pool);
    printf("SELF_CHECK 18 %s final_publication_adopted=%d\n", adopted ? "PASS" : "FAIL", adopted);
    return adopted ? 0 : 1;
}

static int self_check_18(void)
{
    const char *invalid[] = {"--partial=2", "--partial=nan", "--partial=inf",
        "--partial=-1", "--reps=3junk", "--reps=", "--reps=999999999999999999999999",
        "--reps=2", "--reps=201", "--reps=-3", "--reps=+3", "--reps= 3",
        "--timeout-ms=0", "--timeout-ms=600001", "--timeout-ms=nan", "--timeout-ms=3junk", "--file=", "--unknown"};
    bool rejects = true;
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        char *args[] = {(char *)"lineidx_bench", (char *)invalid[i]};
        options parsed;
        if (parse_options(2, args, &parsed)) rejects = false;
    }
    printf("SELF_CHECK 18 %s rejects_invalid_workloads=%d\n", rejects ? "PASS" : "FAIL", rejects);
    puts("SELF_CHECK 18 waiting for a stranded, unsubmitted index (must return a failure)");
    fflush(stdout);
    lineidx *x = lineidx_create(128);
    work_pool pool;
    if (!x || work_pool_init(&pool, 1, 0) != 0) return 2;
    bool waited = wait_index(x, &pool, bench_now_ns() + 1000000, true, "SELF_CHECK 18 unsubmitted");
    lineidx_destroy(x);
    x = lineidx_create(1);
    blocked_source blocked = {.entered = false, .release = false};
    lineidx_src src = {&blocked, 1, blocked_span, NULL};
    if (!x || lineidx_build_start(x, &pool, &src) != 0) return 2;
    uint64_t deadline = bench_now_ns() + 1000000000ull;
    while (!atomic_load_explicit(&blocked.entered, memory_order_acquire)) {
        if (bench_now_ns() >= deadline) fail_fast("self-check worker did not start");
        nap(50);
    }
    bool completes = wait_index(x, &pool, bench_now_ns() + 1000000, true, "SELF_CHECK 18 stranded worker");
    lineidx_build_cancel(x);
    bool retires = wait_index(x, &pool, bench_now_ns() + 1000000, false, "SELF_CHECK 18 stranded retirement");
    atomic_store_explicit(&blocked.release, true, memory_order_release);
    index_destroy(x, &pool, 1000000000ull);
    work_pool_shutdown(&pool);
    bool bounded = !waited && !completes && !retires;
    printf("SELF_CHECK 18 %s bounded_stranded_wait=%d active_deadline=%d retirement_deadline=%d\n",
           bounded ? "PASS" : "FAIL", bounded, !completes, !retires);
    return !rejects || !bounded;
}

typedef struct evidence { char power[16], load[32]; } evidence;

static evidence measurement_stamp(void)
{
    evidence stamp = {{0}, {0}};
    char status[32];
    bench_battery_status(status, sizeof status);
    snprintf(stamp.power, sizeof stamp.power, "%s", bench__tag_from_power(status));
    FILE *load = fopen("/proc/loadavg", "r");
    if (!load || fscanf(load, "%31s", stamp.load) != 1) strcpy(stamp.load, "unknown");
    if (load) fclose(load);
    return stamp;
}

static int report_stamped(FILE *output, const char *name, const bench_samples *samples,
                          uint64_t gate50, uint64_t gate99, const evidence *stamp)
{
    uint64_t p50 = bench_p50(samples), p99 = bench_p99(samples), lo = 0, hi = 0;
    bench_ci95(samples, 0.50, &lo, &hi);
    bool pass = samples->n && !samples->dropped && (!gate50 || p50 <= gate50) && (!gate99 || p99 <= gate99);
    fprintf(output, "BENCH name=%s n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] ns (M)%s load=%s "
                    "gate_p50=%llu gate_p99=%llu ns (G) pass=%d\n", name, samples->n,
            (unsigned long long)p50, (unsigned long long)p99, (unsigned long long)lo,
            (unsigned long long)hi, stamp->power, stamp->load,
            (unsigned long long)gate50, (unsigned long long)gate99, pass);
    return pass ? 0 : 1;
}

static int report_timing(FILE *output, const bench_samples *samples, bool jump,
                         bool cold, bool calibrated, bool track, const evidence *stamp)
{
    uint64_t gate50 = jump ? (cold ? 250000000ull : 30000000ull) :
                            (cold ? 600000000ull : 80000000ull);
    uint64_t gate99 = jump ? (cold ? 350000000ull : 50000000ull) :
                            (cold ? 800000000ull : 125000000ull);
    char name[96];
    snprintf(name, sizeof name, "%s%s_%s_%s", track || !calibrated ? "TRACK_" : "",
             jump ? "G7j_unindexed_viewport_90pct" : "G7_index_1g",
             calibrated ? "fixture" : "alternate", cold ? "cold" : "warm");
    if (stamp) return report_stamped(output, name, samples,
                                    calibrated && !track ? gate50 : 0, calibrated && !track ? gate99 : 0, stamp);
    /* Only synthetic self-checks use the unstamped harness report. */
    return bench_report_fp(output, name, samples,
                           calibrated && !track ? gate50 : 0, calibrated && !track ? gate99 : 0);
}

static int finish_run(int misses, bool cold, bool calibrated, bool track)
{
    if (misses) return 1;
    if (!track && calibrated && !cold) return 3; /* unvalidated manual cold gates */
    return 0;
}

static bool verify_eviction(fixture *file)
{
    const uint8_t *map = map_file(file);
    uint64_t n = file->n;
    long page_size = sysconf(_SC_PAGESIZE);
    if (!map || page_size <= 0) return false;
    size_t pages = (size_t)(n / (uint64_t)page_size + (n % (uint64_t)page_size != 0));
    unsigned char *resident = malloc(pages);
    bool ok = resident && mincore((void *)map, (size_t)n, resident) == 0;
    size_t present = 0;
    if (ok) for (size_t i = 0; i < pages; i++) present += (resident[i] & 1u) != 0;
    free(resident);
    munmap((void *)map, (size_t)n);
    if (!ok || present) {
        fprintf(stderr, "cold sample refused: resident_pages=%zu of %zu\n", present, pages);
        return false;
    }
    printf("# cold eviction verified: resident_pages=0 of %zu\n", pages);
    return true;
}

/* An untimed manual eviction handshake for EVERY cold sample, after the scalar
 * oracle/open setup. mincore never touches the mapping's data. No reference or
 * warm-up runs intervene between this verification and the timed request. */
static bool manual_cold_ready(fixture *file, uint64_t timeout, const char *row, int sample)
{
    printf("MANUAL %s sample=%d: externally sync + drop_caches (root), verify fincore; type EVICTED\n", row, sample);
    fflush(stdout);
    char response[32] = {0};
    size_t used = 0;
    uint64_t deadline = bench_now_ns() + timeout;
    while (used + 1 < sizeof response) {
        uint64_t now = bench_now_ns();
        if (now >= deadline) break;
        uint64_t remaining_ms = (deadline - now) / 1000000 + 1;
        int wait_ms = remaining_ms > INT_MAX ? INT_MAX : (int)remaining_ms;
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, wait_ms);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || read(STDIN_FILENO, response + used, 1) != 1) break;
        if (response[used++] == '\n') break;
    }
    if (strcmp(response, "EVICTED\n") != 0) {
        fputs("cold sample refused: manual eviction acknowledgement missing/expired\n", stderr);
        return false;
    }
    return verify_eviction(file);
}

static int self_check_17(void)
{
    /* Synthetic, untimed gate misses: no performance run or eviction needed. */
    uint64_t values[] = {801000000, 801000000, 801000000};
    bench_samples samples;
    bench_samples_init(&samples, values, 3); samples.n = 3;
    FILE *output = tmpfile();
    if (!output) return 2;
    bool index_miss = report_timing(output, &samples, false, true, true, false, NULL) != 0;
    for (size_t i = 0; i < 3; i++) values[i] = 351000000;
    bool jump_miss = report_timing(output, &samples, true, true, true, false, NULL) != 0;
    values[0] = 599000000; values[1] = 599000000; values[2] = 801000000;
    bool index_tail = report_timing(output, &samples, false, true, true, false, NULL) != 0;
    values[0] = 249000000; values[1] = 249000000; values[2] = 351000000;
    bool jump_tail = report_timing(output, &samples, true, true, true, false, NULL) != 0;
    char temp[] = "/tmp/edit-4w1.48-cold-XXXXXX";
    int fd = mkstemp(temp);
    if (fd < 0) return 2;
    bool written = write(fd, "x\n", 2) == 2;
    close(fd);
    fixture resident = {temp, 2};
    bool rejects_resident = written && !verify_eviction(&resident);
    unlink(temp);
    bool pending_fails = finish_run(0, false, true, false) != 0;
    int fail = !index_miss || !jump_miss || !index_tail || !jump_tail || !pending_fails || !rejects_resident;
    printf("SELF_CHECK 17 %s cold_index_miss=%d cold_jump_miss=%d unvalidated_cold_fails=%d\n",
           fail ? "FAIL" : "PASS", index_miss, jump_miss, pending_fails);
    fclose(output);
    return fail;
}

/* P1.6c additional rows. The P1.6d rows above remain unchanged. */
typedef struct cancel_source {
    uint8_t byte;
    _Atomic bool entered, resume;
    _Atomic unsigned calls;
} cancel_source;
static uint64_t worker_cpu_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) fail_fast("thread CPU clock failed");
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static size_t cancel_span(void *ctx, uint64_t off, const uint8_t **p)
{
    cancel_source *s = ctx;
    if (off >= LINEIDX_CHUNK) return 0;
    atomic_fetch_add_explicit(&s->calls, 1, memory_order_relaxed);
    atomic_store_explicit(&s->entered, true, memory_order_release);
    while (!atomic_load_explicit(&s->resume, memory_order_acquire)) nap(50);
    uint64_t begin = worker_cpu_now();
    while (worker_cpu_now() - begin < 2000000ull) { }
    *p = &s->byte;
    return 1;
}
static int cancellation_rows(work_pool *p, int reps, uint64_t timeout)
{
    uint64_t logical_buf[200], cpu_buf[200], cpu_max = 0;
    bench_samples logical, cpu;
    bench_samples_init(&logical, logical_buf, 200);
    bench_samples_init(&cpu, cpu_buf, 200);
    evidence stamp = measurement_stamp();
    for (int r = 0; r < reps; r++) {
        stamp = measurement_stamp();
        cancel_source source = {.byte = 'a'};
        lineidx_src src = {&source, LINEIDX_CHUNK, cancel_span, NULL};
        lineidx *x = lineidx_create(src.len);
        if (!x || lineidx_build_start_owned(x, p, &src, 0) != 0) fail_fast("G6c submit failed");
        uint64_t deadline = bench_now_ns() + timeout;
        while (!atomic_load_explicit(&source.entered, memory_order_acquire)) {
            if (bench_now_ns() >= deadline) fail_fast("G6c worker start timeout");
            nap(50);
        }
        uint64_t begin = bench_now_ns();
        lineidx_build_cancel(x);
        bench_add(&logical, bench_now_ns() - begin); /* logical return only */
        atomic_store_explicit(&source.resume, true, memory_order_release);
        if (!wait_index(x, p, deadline, false, "G6c physical completion")) fail_fast("G6c retirement failed");
        uint64_t slice = lineidx_cancel_cpu_ns(x);
        if (!slice || atomic_load(&source.calls) != 1u || lineidx_complete(x))
            fail_fast("G6c polling/publication suppression failed");
        bench_add(&cpu, slice);
        if (slice > cpu_max) cpu_max = slice;
        index_destroy(x, p, timeout);
    }
    int rc = report_stamped(stdout, "TRACK_G6c_lineidx_cancel_logical", &logical, 1000000, 5000000, &stamp);
    rc |= report_stamped(stdout, "TRACK_G6c_lineidx_cancel_cpu_slice", &cpu, 5000000, 5000000, &stamp);
    printf("# G6c worker_next_cpu_max=%llu ns (M)%s load=%s gate_max=5000000 ns (G) publication_suppression=PASS\n",
           (unsigned long long)cpu_max, stamp.power, stamp.load);
    return rc | (cpu_max > 5000000ull ? 1 : 0);
}
typedef struct seek_source { flat f; _Atomic uint64_t bytes; } seek_source;
static size_t seek_span(void *ctx, uint64_t off, const uint8_t **p)
{
    seek_source *s = ctx;
    if (off >= s->f.n) return 0;
    uint64_t k = s->f.n - off;
    if (k > 16384u) k = 16384u;
    *p = s->f.b + off;
    atomic_fetch_add_explicit(&s->bytes, k, memory_order_relaxed);
    return (size_t)k;
}
static int worker_seek_row(work_pool *p, fixture *file, const oracle *reference,
                            int reps, uint64_t timeout, bool calibrated, bool track)
{
    uint64_t buf[200]; bench_samples samples;
    bench_samples_init(&samples, buf, 200);
    evidence stamp = measurement_stamp();
    for (int r = 0; r < reps + 2; r++) {
        const uint8_t *m = map_file(file);
        if (!m) fail_fast("async G7j mapping failed");
        seek_source source = {.f = {m, file->n}};
        lineidx_src src = {&source, file->n, seek_span, NULL};
        lineidx *x = lineidx_create(file->n); viewport v;
        if (!x || viewport_init(&v, &source.f) != 0 || lineidx_built_prefix(x) != 0)
            fail_fast("async G7j fresh open setup failed");
        /* Prepare a controlled prefix outside the timer. Each call is a UI
         * slice; no worker can race ahead of this prefix. Always stop before
         * the target, even for an uncalibrated alternate fixture. */
        size_t prefix = lineidx_chunk_count(x) / 2u;
        uint64_t target_chunk = reference->byte ? (reference->byte - 1u) / LINEIDX_CHUNK : 0;
        if (prefix > target_chunk) prefix = (size_t)target_chunk;
        lineidx_src seed_src = {&source.f, file->n, flat_span, NULL};
        uint64_t seed_deadline = bench_now_ns() + timeout;
        while (lineidx_built_prefix(x) < prefix) {
            (void)lineidx_seek_line(x, &seed_src, UINT64_MAX, LINEIDX_CHUNK);
            if (bench_now_ns() >= seed_deadline) fail_fast("async G7j prefix setup timeout");
        }
        uint64_t prefix_bytes = (uint64_t)prefix * LINEIDX_CHUNK;
        if (lineidx_built_prefix(x) != prefix || (reference->byte && prefix_bytes >= reference->byte))
            fail_fast("async G7j prefix reached target before request");
        stamp = measurement_stamp();
        uint64_t begin = bench_now_ns(), deadline = begin + timeout;
        if (lineidx_seek_start_owned(x, p, &src, reference->target, 0) != 0)
            fail_fast("async G7j request failed");
        lineidx_result answer;
        while (!lineidx_seek_result(x, &answer)) {
            drain(p);
            if (bench_now_ns() >= deadline) fail_fast("async G7j completion timeout");
            nap(50);
        }
        if (!answer.exact || answer.value != reference->byte ||
            viewport_submit(&v, answer.value, reference->target, reference->lines, deadline) != 0)
            fail_fast("async G7j offset/viewport failed");
        uint64_t elapsed = bench_now_ns() - begin;
        if (r >= 2) bench_add(&samples, elapsed);
        uint64_t bytes = atomic_load(&source.bytes);
        uint64_t needed = reference->byte - prefix_bytes;
        if (bytes < needed || bytes - needed > LINEIDX_CHUNK ||
            (calibrated && bytes >= file->n - prefix_bytes) || !viewport_correct(&v, &source.f, reference))
            fail_fast("async G7j independent byte/cell oracle failed");
        printf("# async G7j sample=%d partial_prefix_chunks=%zu prefix_bytes=%llu scanned_bytes=%llu target_byte=%llu viewport_submitted=1 (M)%s load=%s\n",
               r, prefix, (unsigned long long)prefix_bytes, (unsigned long long)bytes,
               (unsigned long long)reference->byte, stamp.power, stamp.load);
        viewport_free(&v); index_destroy(x, p, timeout); munmap((void *)m, (size_t)file->n);
    }
    puts("# async partial G7j warm contract (G) p50=30 ms p99=50 ms; measured on the box as it is; null viewport backend");
    return report_stamped(stdout, "TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm", &samples,
                          calibrated && !track ? 30000000 : 0, calibrated && !track ? 50000000 : 0, &stamp);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-check") == 0) {
        int result = self_check_15();
        result |= self_check_16(); result |= self_check_17(); result |= self_check_18();
        result |= self_check_completion_race();
        return result;
    }
    if (argc == 2 && strcmp(argv[1], "--self-check=15") == 0) return self_check_15();
    if (argc == 2 && strcmp(argv[1], "--self-check=16") == 0) return self_check_16();
    if (argc == 2 && strcmp(argv[1], "--self-check=17") == 0) return self_check_17();
    if (argc == 2 && strcmp(argv[1], "--self-check=18") == 0) return self_check_18() | self_check_completion_race();
    if (argc == 2 && strcmp(argv[1], "--self-check=race") == 0) return self_check_completion_race();
    options parsed;
    if (!parse_options(argc, argv, &parsed)) {
        fputs("invalid workload: use --file=PATH --reps=3..200 --track --cold --timeout-ms=1..600000; --partial removed (fresh index required)\n", stderr);
        return 2;
    }
    int reps = parsed.reps, rc = 0;
    bool cold = parsed.cold, track = parsed.track;
    fixture file = {parsed.file, 0};
    work_pool pool;
    char st[32];
    printf("# lineidx_bench file=%s reps=%d fresh_index=1 cold=%d track=%d power=%s\n",
           file.path, reps, cold, track, bench_battery_status(st, sizeof st));
    if (work_pool_init(&pool, 1, 0) != 0) { fprintf(stderr, "pool init failed\n"); return 2; }

    uint64_t n = 0;
    const uint8_t *m0 = map_file(&file);
    n = file.n;
    if (!m0) { fprintf(stderr, "cannot map %s (positive representable unchanged size required)\n", file.path); return 2; }

    flat f0 = { m0, n };
    lineidx_src s0 = { &f0, n, flat_span, NULL };
    oracle reference;
    if (reference_metadata(&f0, &s0, &reference) != 0) return 2;
    bool gate_fixture = strcmp(file.path, "/tmp/edit-corpus/log_1g.txt") == 0;
    if (gate_fixture && !fixture_valid(n, &reference)) {
        fprintf(stderr, "fixture manifest mismatch: bytes=%llu lines=%llu target=%llu\n",
                (unsigned long long)n, (unsigned long long)reference.lines, (unsigned long long)reference.target);
        return 2;
    }
    if (!gate_fixture) puts("TRACK alternate fixture: G7/G7j limits are uncalibrated");
    uint64_t lc = reference.lines, target = reference.target, want = reference.byte;
    printf("# line_count=%llu target_line=%llu target_byte=%llu\n",
           (unsigned long long)lc, (unsigned long long)target, (unsigned long long)want);
    munmap((void *)m0, (size_t)n);

    uint64_t buf[256];
    bench_samples sm;

    evidence stamp = measurement_stamp();

    /* G7: new mapping -> exact index (mmap + create + job + completion). */
    bench_samples_init(&sm, buf, 256);
    for (int r = 0; r < reps + (cold ? 0 : 2); r++) {
        if (cold && !manual_cold_ready(&file, parsed.timeout, "G7", r)) return 2;
        stamp = measurement_stamp();
        printf("# G7 sample=%d power=%s load=%s\n", r, stamp.power, stamp.load);
        uint64_t t0 = bench_now_ns();
        const uint8_t *m = map_file(&file);
        if (!m) fail_fast("mapping failed or fixture size changed");
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        if (!x || lineidx_build_start(x, &pool, &s) != 0) fail_fast("index allocation or job submission failed");
        if (!wait_index(x, &pool, bench_now_ns() + parsed.timeout, true, "index build"))
            fail_fast("index build completion failed");
        uint64_t dt = bench_now_ns() - t0;
        if (!index_correct(x, &reference)) { fprintf(stderr, "G7: wrong line count\n"); return 2; }
        if (cold || r >= 2) bench_add(&sm, dt);            /* two warm-up reps (page cache) */
        index_destroy(x, &pool, parsed.timeout);
        munmap((void *)m, (size_t)n);
    }
    rc |= report_timing(stdout, &sm, false, cold, gate_fixture, track, &stamp);

    stamp = measurement_stamp();
    /* G7 build job alone (mapping faulted outside the timer): TRACK. */
    {
        const uint8_t *m = map_file(&file);
        if (!m) fail_fast("mapping failed or fixture size changed");
        volatile uint8_t sink = 0;
        for (uint64_t o = 0; o < n; o += 4096) sink ^= m[o];
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < reps; r++) {
            flat f = { m, n };
            lineidx_src s = { &f, n, flat_span, NULL };
            lineidx *x = lineidx_create(n);
            uint64_t t0 = bench_now_ns();
            if (!x || lineidx_build_start(x, &pool, &s) != 0) fail_fast("index allocation or job submission failed");
            if (!wait_index(x, &pool, bench_now_ns() + parsed.timeout, true, "index build"))
                fail_fast("index build completion failed");
            bench_add(&sm, bench_now_ns() - t0);
            if (!index_correct(x, &reference)) fail_fast("prefaulted build wrong count");
            index_destroy(x, &pool, parsed.timeout);
        }
        (void)report_stamped(stdout, "TRACK_build_prefaulted", &sm, 0, 0, &stamp);
        munmap((void *)m, (size_t)n);
    }
    if (!cold) puts("UNVALIDATED G7 cold (G) 600/800 ms; G7j cold (G) 250/350 ms: run --cold with manual verified drop_caches per sample");

    stamp = measurement_stamp();
    /* G7j: new mapping, zero built chunks. Request -> worker build -> exact
     * target offset -> sliced layout -> null submit + present. */
    bench_samples_init(&sm, buf, 256);
    for (int r = 0; r < reps + (cold ? 0 : 2); r++) {
        const uint8_t *m = map_file(&file);
        if (!m) fail_fast("mapping failed or fixture size changed");
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        jump_observation seen = {0};
        viewport v;
        if (!x || viewport_init(&v, &f) != 0) fail_fast("viewport/index open setup failed");
        if (cold && !manual_cold_ready(&file, parsed.timeout, "G7j", r)) return 2;
        stamp = measurement_stamp();
        uint64_t t0 = bench_now_ns();
        lineidx_result q = jump_request(x, &pool, parsed.timeout, &s, target, &v, &seen);
        size_t pre = seen.prefix;
        if (!q.exact || q.value != want) { fprintf(stderr, "G7j: wrong answer %llu want %llu exact=%d\n", (unsigned long long)q.value, (unsigned long long)want, q.exact); return 2; }
        uint64_t dt = seen.submitted_ns - t0;
        if (cold || r >= 2) bench_add(&sm, dt);
        printf("# G7j sample=%d fresh_prefix=%zu scanned_bytes=%llu viewport_submitted=%d power=%s load=%s\n",
               r, pre, (unsigned long long)seen.scanned_bytes, seen.viewport_submitted, stamp.power, stamp.load);
        if (!viewport_correct(&v, &f, &reference)) { fprintf(stderr, "G7j: wrong viewport cells (ASCII fixtures required)\n"); return 2; }
        viewport_free(&v);
        index_destroy(x, &pool, parsed.timeout);
        munmap((void *)m, (size_t)n);
    }
    rc |= report_timing(stdout, &sm, true, cold, gate_fixture, track, &stamp);

    stamp = measurement_stamp();
    /* Cancel acknowledgement of a running 1 GB build: TRACK (binding G6c bench is P1.8's). */
    {
        const uint8_t *m = map_file(&file);
        if (!m) fail_fast("mapping failed or fixture size changed");
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < reps; r++) {
            flat f = { m, n };
            lineidx_src s = { &f, n, flat_span, NULL };
            lineidx *x = lineidx_create(n);
            if (!x || lineidx_build_start(x, &pool, &s) != 0) fail_fast("index allocation or job submission failed");
            nap(10000);
            uint64_t t0 = bench_now_ns();
            lineidx_build_cancel(x);
            if (!wait_index(x, &pool, bench_now_ns() + parsed.timeout, false, "cancel retirement"))
                fail_fast("cancel retirement failed");
            bench_add(&sm, bench_now_ns() - t0);
            index_destroy(x, &pool, parsed.timeout);
        }
        (void)report_stamped(stdout, "TRACK_cancel_ack_gate_1ms_5ms", &sm, 0, 0, &stamp);
        munmap((void *)m, (size_t)n);
    }

    stamp = measurement_stamp();
    /* Queries and edits on a complete 1 GB index (mapping resident): TRACK. */
    {
        const uint8_t *m = map_file(&file);
        if (!m) fail_fast("mapping failed or fixture size changed");
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        if (!x || lineidx_build_start(x, &pool, &s) != 0) fail_fast("index allocation or job submission failed");
        if (!wait_index(x, &pool, bench_now_ns() + parsed.timeout, true, "index build"))
            fail_fast("index build completion failed");
        if (!index_correct(x, &reference)) fail_fast("query reference build wrong count");
        uint64_t rs = 88172645463325252ull;
        volatile uint64_t sink = 0;
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t ln = rs % lc;
            BENCH_TIME(&sm, sink += lineidx_line_to_byte(x, &s, ln).value);
        }
        (void)report_stamped(stdout, "TRACK_line_to_byte_exact", &sm, 0, 0, &stamp);
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t off = rs % n;
            BENCH_TIME(&sm, sink += lineidx_byte_to_line(x, &s, off).value);
        }
        (void)report_stamped(stdout, "TRACK_byte_to_line_exact", &sm, 0, 0, &stamp);
        /* typing: edit + refresh on a flat copy would need a mutable source; the
         * refresh rescan is one chunk, so time it on the unchanged mapping by
         * invalidating a chunk with a zero-length replace. */
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t off = rs % n;
            BENCH_TIME(&sm, { (void)lineidx_edit(x, off, 0, 0); (void)lineidx_refresh(x, &s); });
        }
        (void)report_stamped(stdout, "TRACK_edit_refresh_1chunk", &sm, 0, 0, &stamp);
        (void)sink;
        /* G10f memory: 16 B per 64 KiB chunk (+ summaries). */
        size_t mem = lineidx_mem_bytes(x);
        double per = (double)mem / (double)lineidx_chunk_count(x);
        int miss = per > 16.5;
        printf("BENCH name=%smem_bytes_per_chunk value=%.3f B (M)%s load=%s gate=16.5 (G) within_limit=%d (%zu B reported, %zu chunks)\n",
               track || !gate_fixture ? "TRACK_" : "", per, stamp.power, stamp.load, miss ? 0 : 1, mem, lineidx_chunk_count(x));
        if (!track && gate_fixture) rc |= miss;
        index_destroy(x, &pool, parsed.timeout);
        munmap((void *)m, (size_t)n);
    }

    rc |= cancellation_rows(&pool, reps, parsed.timeout);
    if (!cold) rc |= worker_seek_row(&pool, &file, &reference, reps, parsed.timeout, gate_fixture, track);
    work_pool_shutdown(&pool);
    int result = finish_run(rc, cold, gate_fixture, track);
    printf("lineidx_bench: %s\n", track || !gate_fixture ? "TRACK only; gates unvalidated" :
           result == 3 ? "UNVALIDATED cold gates (exit 3)" : result ? "MISS" : "cold rows met; warm unvalidated (null viewport backend)");
    return result;
}
