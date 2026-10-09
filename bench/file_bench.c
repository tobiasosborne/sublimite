/* P1.7f: endpoint benches. --self-check never measures latency. */
#include "file/file.h"
#include "base/base.h"
#include "trace/trace.h"
#include "layout/layout.h"
#include "raster/raster.h"
#include "x11/plat.h"
#include "harness.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define SAMPLES 100u
#define COLS 142u
#define ROWS 47u
#define CELLS (COLS * ROWS)

typedef struct collector {
    file *f;
    uint32_t kind, generation;
    int seen, bad, failed, prefix, opened, prepared, replaced;
    file_msg msg;
    render_backend *backend;
} collector;
typedef struct busy_jobs {
    _Atomic int started, release;
    work_handle handles[3];
    size_t count;
} busy_jobs;
typedef struct view {
    plat platform;
    render_backend backend;
    void *state;
    render_grid grid;
    render_cell cells[CELLS], expected[CELLS];
    uint64_t dirty, row_byte[ROWS];
    uint32_t row_used[ROWS];
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_atlas_page page;
    layout lay;
    uint32_t serial;
    collector *pending;
} view;

static void pause_briefly(void)
{
    struct timespec ts = {0, 100000};
    (void)nanosleep(&ts, NULL);
}
static int prefix_correct(const uint8_t *got, size_t n, const uint8_t *want, size_t expected)
{
    return n == expected && (n == 0 || (got && want && memcmp(got, want, n) == 0));
}
static int prepare_fresh(piece_tree *t, uint32_t sample)
{
    uint8_t byte = (uint8_t)('A' + sample % 26u);
    if (piece_delete(t, 0, 1, NULL) != 0) return 1;
    return piece_insert(t, 0, &byte, 1) != 0;
}
static int completion_correct(const file_msg *m, file *f, uint32_t generation, uint64_t size)
{
    return m && m->kind == FILE_MSG_SAVE_DONE && m->f == f &&
        m->generation == generation && m->status == FILE_OK && m->size == size;
}
static int limits_ok(const bench_samples *s, uint64_t p50, uint64_t p99)
{
    return s->n != 0 && s->dropped == 0 && (p50 == 0 || bench_p50(s) <= p50) && (p99 == 0 || bench_p99(s) <= p99);
}
static int fixture(const char *path, uint64_t size, int sparse, uint8_t *prefix, size_t *len, int quiet);
static int source_spans_cold(const piece_tree *tree, uint64_t size, int quiet);
static int self_check(unsigned section)
{
    if (section == 26u) {
        const uint8_t good[] = "first viewport\n", bad[] = "blank viewport\n";
        if (prefix_correct(NULL, 0, good, sizeof good) ||
            prefix_correct(good, sizeof good - 1u, good, sizeof good) ||
            prefix_correct(bad, sizeof bad, good, sizeof good) ||
            !prefix_correct(good, sizeof good, good, sizeof good)) {
            puts("SELF_CHECK section=26 FAIL blank/short/wrong prefix accepted"); return 1;
        }
        char path[] = "/tmp/edit-file-fixture-XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) return 1;
        int rc = ftruncate(fd, 65536) != 0;
        uint8_t prefix[65536]; size_t n = 0;
        if (!rc) rc = fixture(path, 65536, 1, prefix, &n, 1) || n != 65536;
        if (!rc) rc = !fixture(path, 65537, 0, prefix, &n, 1);
        uint8_t zero = 0;
        if (!rc) rc = pwrite(fd, &zero, 1, 65535) != 1;
        if (!rc) rc = !fixture(path, 65536, 1, prefix, &n, 1);
        close(fd); unlink(path);
        if (!rc) rc = !fixture(path, 65536, 0, prefix, &n, 1);
        if (rc) { puts("SELF_CHECK section=26 FAIL missing/wrong-sized/non-sparse fixture accepted"); return 1; }
    } else if (section == 27u) {
        piece_allocator a = piece_default_allocator();
        piece_tree *t = piece_create(&a);
        EDIT_ASSERT(t && piece_init_copy(t, (const uint8_t *)"ab", 2) == 0);
        for (uint32_t i = 1; i <= 3; i++) {
            piece_snapshot *before = piece_snapshot_take(t);
            EDIT_ASSERT(before);
            EDIT_ASSERT(prepare_fresh(t, i) == 0);
            piece_snapshot *after = piece_snapshot_take(t);
            uint8_t oldbyte = 0, newbyte = 0;
            EDIT_ASSERT(after && piece_snapshot_read(before, 0, &oldbyte, 1) == 0);
            EDIT_ASSERT(piece_snapshot_read(after, 0, &newbyte, 1) == 0);
            int fresh = before != after && oldbyte != newbyte;
            piece_snapshot_release(before); piece_snapshot_release(after);
            if (!fresh) { piece_destroy(t); puts("SELF_CHECK section=27 FAIL unchanged snapshot reused"); return 1; }
        }
        piece_destroy(t);
    } else if (section == 28u) {
        /* A completion must match identity, generation, status AND byte count. */
        file *f = NULL;
        file_msg m = { FILE_MSG_SAVE_DONE, 7, f, FILE_ERR_IO, 0, 100, ENOSPC };
        if (completion_correct(&m, f, 7, 100)) {
            puts("SELF_CHECK section=28 FAIL failed save accepted"); return 1;
        }
        m.status = FILE_OK; m.err_no = 0;
        if (!completion_correct(&m, f, 7, 100)) return 1;
        m.generation = 6;
        if (completion_correct(&m, f, 7, 100)) return 1;
        m.generation = 7; m.f = (file *)(void *)&m;
        if (completion_correct(&m, f, 7, 100)) return 1;
        m.f = f; m.size = 99;
        if (completion_correct(&m, f, 7, 100)) return 1;
        m.size = 100; m.kind = FILE_MSG_OPEN_READY;
        if (completion_correct(&m, f, 7, 100)) return 1;
        uint64_t times[] = {11000000, 11000000, 51000000};
        bench_samples s; bench_samples_init(&s, times, 3); s.n = 3;
        if (limits_ok(&s, 10000000, 50000000)) return 1;
        times[0] = times[1] = 1000000; times[2] = 51000000;
        if (limits_ok(&s, 10000000, 50000000)) return 1;
        times[2] = 49000000;
        if (!limits_ok(&s, 10000000, 50000000)) return 1;
        s.dropped = 1;
        if (limits_ok(&s, 10000000, 50000000)) return 1;
        s.dropped = 0; s.n = 0;
        if (limits_ok(&s, 10000000, 50000000)) return 1;
        piece_allocator a = piece_default_allocator();
        piece_tree *tree = piece_create(&a);
        EDIT_ASSERT(tree && piece_init_copy(tree, (const uint8_t *)"materialised", 12) == 0);
        int incorrectly_cold = source_spans_cold(tree, 12, 1) == 0;
        piece_destroy(tree);
        if (incorrectly_cold) return 1;
    } else return 2;
    printf("SELF_CHECK section=%u PASS\n", section);
    return 0;
}

static void collect(const work_msg *wm, void *ud)
{
    collector *c = ud;
    if (c->backend) {
        render_event ev = { .kind = RENDER_EVENT_WORK, .frame_id = c->backend->active_frame, .work = wm };
        (void)render_backend_event(c->backend, &ev);
    }
    /* Decode internal notifications too: SAVE_PREPARED authorizes/enqueues
     * commit on UI, and REPLACED installs the new identity before SAVE_DONE. */
    file_msg m = {0};
    int decoded = file_msg_decode(wm, &m);
    if (!c->f || m.f != c->f || m.generation != c->generation) return;
    if (decoded != FILE_OK) {
        if (wm->kind == FILE_MSG_SAVE_PREPARED && ++c->prepared != 1) c->bad = 1;
        if (wm->kind == FILE_MSG_REPLACED && (++c->replaced != 1 || c->prepared != 1)) c->bad = 1;
        return;
    }
    if (m.kind == FILE_MSG_PREFIX_READY && (++c->prefix != 1 || m.status != FILE_OK)) c->bad = 1;
    if (m.kind == FILE_MSG_OPEN_READY && (++c->opened != 1 || c->prefix != 1)) c->bad = 1;
    if (m.kind == FILE_MSG_OPEN_FAILED) { c->failed = 1; c->msg = m; }
    if (m.kind == FILE_MSG_SAVE_DONE && m.status == FILE_OK &&
        (c->prepared != 1 || c->replaced != 1)) c->bad = 1;
    if (m.kind == c->kind) {
        if (c->seen) c->bad = 1;
        c->seen++; c->msg = m;
    }
}
static int await_message(work_pool *pool, collector *c)
{
    uint64_t start = bench_now_ns();
    while (!c->seen && !c->bad && !c->failed && bench_now_ns() - start < 60000000000ull) {
        (void)work_mailbox_drain(pool, collect, c);
        if (c->kind == FILE_MSG_SAVE_DONE) (void)file_save_busy(c->f);
        if (!c->seen) pause_briefly();
    }
    return c->seen == 1 && !c->bad && !c->failed ? 0 : 1;
}
/* Physical retirement is required before a new trial, but is outside the
 * G8d interval, which ends at decoded durable SAVE_DONE receipt. */
static int finish_save(work_pool *pool, collector *c)
{
    uint64_t start = bench_now_ns();
    while (file_save_busy(c->f) && bench_now_ns() - start < 60000000000ull) {
        (void)work_mailbox_drain(pool, collect, c);
        pause_briefly();
    }
    return file_save_busy(c->f) || c->bad;
}
static void bulk_busy(work_ctx *ctx)
{
    busy_jobs *b = ctx->arg;
    atomic_fetch_add(&b->started, 1);
    /* Touch a private working set, rather than sleeping and calling it busy. */
    volatile uint64_t scratch[1024] = {0};
    uint64_t k = 0;
    while (!atomic_load(&b->release) && !work_should_stop(ctx)) {
        for (size_t i = 0; i < 1024; i++) scratch[i] = scratch[i] + ++k;
    }
}
static void stop_busy(work_pool *pool, busy_jobs *b);
static int start_busy(work_pool *pool, busy_jobs *b, unsigned jobs)
{
    memset(b, 0, sizeof *b);
    for (unsigned i = 0; i < jobs; i++) {
        b->handles[i] = work_submit(pool, (work_job){bulk_busy, b, 0, WORK_BULK});
        if (!b->handles[i].epoch) { stop_busy(pool, b); return 1; }
        b->count++;
    }
    if (jobs) {
        uint64_t start = bench_now_ns();
        while (!atomic_load(&b->started) && bench_now_ns() - start < 5000000000ull) pause_briefly();
        if (!atomic_load(&b->started)) { stop_busy(pool, b); return 1; }
    }
    return 0;
}
static void stop_busy(work_pool *pool, busy_jobs *b)
{
    atomic_store(&b->release, 1);
    /* Drain before the next trial. busy/pending are documented work.h fields. */
    for (size_t i = 0; i < b->count; i++)
        while (atomic_load(&pool->slots[b->handles[i].slot].busy)) pause_briefly();
}
static int report(const char *name, const bench_samples *s, uint64_t p50, uint64_t p99, int track)
{
    char power[32]; double load = 0;
    bench_battery_status(power, sizeof power);
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", &load) != 1) load = 0; fclose(f); }
    printf("%s %s (M)%s load1=%.2f n=%zu p50=%.3f ms p99=%.3f ms gate=%.0f/%.0f ms (G)\n",
           track ? "TRACK" : "GATE", name, bench__tag_from_power(power), load,
           s->n, (double)bench_p50(s) / 1e6, (double)bench_p99(s) / 1e6,
           (double)p50 / 1e6, (double)p99 / 1e6);
    if (!s->n || s->dropped) return 1;
    return track ? 0 : !limits_ok(s, p50, p99);
}
static void present_done(void *ud, uint32_t serial, uint64_t ust, uint64_t msc)
{
    view *v = ud; (void)ust; (void)msc;
    render_event ev = { .kind = RENDER_EVENT_PRESENT_COMPLETE, .frame_id = serial };
    (void)render_backend_event(&v->backend, &ev);
}
static void ignore_event(void *ud, const plat_event *event) { (void)ud; (void)event; }
static void ignore_timer(void *ud) { (void)ud; }
static void pump_view(view *v, work_pool *pool)
{
    collector c = {.backend = &v->backend};
    (void)work_mailbox_drain(pool, collect, v->pending ? v->pending : &c);
    plat_callbacks cb = {.ud = v, .on_event = ignore_event, .on_blink = ignore_timer,
        .on_work = ignore_timer, .on_present_complete = present_done};
    (void)plat_run_for(&v->platform, &cb, 0);
}
static int finish_frame(view *v, work_pool *pool)
{
    uint64_t start = bench_now_ns();
    while (v->backend.active && bench_now_ns() - start < 5000000000ull) { pump_view(v, pool); pause_briefly(); }
    return v->backend.active ? 1 : 0;
}
typedef struct backend_init_job {
    view *v;
    render_config config;
    size_t bytes;
    int rc;
    _Atomic int done;
} backend_init_job;
static void init_backend(work_ctx *ctx)
{
    backend_init_job *job = ctx->arg;
    job->rc = render_backend_init(&job->v->backend, &job->config, job->v->state, job->bytes);
    atomic_store(&job->done, 1);
}
static int view_init(view *v, work_pool *pool)
{
    memset(v, 0, sizeof *v);
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(30);
    if (!atlas) return 1;
    /* Target A: 142x47 cells at 18x36 px. Baked 30px glyph images
     * fit inside these cells; atlas stride remains its own 16px metric. */
    render_dims dims = {COLS, ROWS, 18, 36};
    v->page = (render_atlas_page){atlas->pixels, atlas->pixels_len,
        (size_t)atlas->cell.cell_w * LAYOUT_ASCII_GLYPHS,
        atlas->cell.cell_w * LAYOUT_ASCII_GLYPHS, atlas->cell.cell_h};
    layout_ascii_glyphs(atlas, v->glyphs);
    if (render_grid_init(&v->grid, dims, v->cells, CELLS, &v->dirty, 1) != 0) return 1;
    v->grid.pages = &v->page; v->grid.page_count = 1;
    v->grid.glyphs = v->glyphs; v->grid.glyph_count = LAYOUT_ASCII_GLYPHS;
    layout_config cfg = {.tab_width = 4, .fg = 0xffffff, .slice_clusters = 500};
    if (layout_init(&v->lay, &v->grid, &cfg, v->row_byte, v->row_used) != 0) return 1;
    plat_config pc = {.title = "file bench TRACK", .width = COLS * dims.cell_w,
        .height = ROWS * dims.cell_h, .work_eventfd = work_pool_eventfd(pool)};
    if (plat_init(&v->platform, &pc) != 0) return 1;
    plat_map(&v->platform);
    if (render_cpu_backend(&v->backend) != 0) return 1;
    render_backend_info info;
    if (render_backend_query(&v->backend, &info) != 0) return 1;
    if (posix_memalign(&v->state, info.state_align, info.state_size) != 0) return 1;
    render_config config = {.dims = dims, .max_width = pc.width, .max_height = pc.height,
        .max_cells = CELLS, .max_glyphs = LAYOUT_ASCII_GLYPHS, .max_pages = 1,
        .max_atlas_bytes = atlas->pixels_len, .platform = &v->platform, .workers = pool};
    /* Frozen render.h requires backend init on a worker. */
    backend_init_job job = {.v = v, .config = config, .bytes = info.state_size};
    work_handle h = work_submit(pool, (work_job){init_backend, &job, 0, WORK_BULK});
    if (!h.epoch) return 1;
    while (!atomic_load(&job.done) || atomic_load(&pool->slots[h.slot].busy)) pause_briefly();
    return job.rc != 0;
}
static int layout_prefix(view *v, const uint8_t *bytes, size_t n)
{
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    if (!t) return 1;
    int rc = piece_init_mapped(t, bytes, n, NULL);
    if (rc == 0) rc = render_frame_begin(&v->grid, ++v->serial);
    layout_viewport vp = {.first_byte = 0, .first_line = 0, .line_count = 1};
    if (rc == 0) rc = layout_begin(&v->lay, t, vp);
    if (rc == 0) { do { rc = layout_run(&v->lay); } while (rc == LAYOUT_MORE); }
    piece_destroy(t);
    return rc != LAYOUT_DONE;
}
static int submit_view(view *v, work_pool *pool)
{
    render_strip strips[ROWS]; size_t count;
    if (render_dirty_strips(&v->grid, strips, ROWS, &count) != 0 ||
        render_backend_submit(&v->backend, &v->grid, strips, count) != 0) return 1;
    uint64_t start = bench_now_ns(); int rc;
    do {
        pump_view(v, pool);
        rc = render_backend_present(&v->backend, v->grid.frame_id);
        if (rc == RENDER_ERR_BUSY) pause_briefly();
    } while (rc == RENDER_ERR_BUSY && bench_now_ns() - start < 5000000000ull);
    if (rc != 0) fprintf(stderr, "FAIL viewport present rc=%d frame=%u\n", rc, v->grid.frame_id);
    return rc != 0;
}
static int read_exact(int fd, uint8_t *bytes, size_t n, off_t off)
{
    size_t done = 0;
    while (done < n) {
        ssize_t got = pread(fd, bytes + done, n - done, off + (off_t)done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return 1;
        done += (size_t)got;
    }
    return 0;
}
static int fixture(const char *path, uint64_t size, int sparse, uint8_t *prefix, size_t *len, int quiet)
{
    int fd = open(path, O_RDONLY); struct stat st;
    int bad = fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size != size;
    *len = size < FILE_PREFIX_MAX ? (size_t)size : FILE_PREFIX_MAX;
    if (!bad) bad = read_exact(fd, prefix, *len, 0);
    if (!bad && sparse) {
        /* Require an all-hole fixture; reject an incorrectly-sized or allocated file. */
        errno = 0; off_t data = lseek(fd, 0, SEEK_DATA);
        bad = st.st_blocks != 0 || data != -1 || errno != ENXIO;
        for (size_t i = 0; i < *len; i++) if (prefix[i]) bad = 1;
        uint8_t tail = 1;
        if (read_exact(fd, &tail, 1, (off_t)(size - 1)) || tail != 0) bad = 1;
    }
    if (fd >= 0) close(fd);
    if (bad && !quiet) fprintf(stderr, "FAIL required fixture %s (size/content/sparsity)\n", path);
    return bad;
}
/* Manual cold mode: attempt eviction, then VERIFY every page with mincore.
 * A hint alone is never evidence. Existing mappings/other readers may prevent
 * eviction, in which case the required scenario fails without a sample. */
static int source_cache(const char *path, uint64_t size, int cold)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0 || size == 0 || size > SIZE_MAX) { if (fd >= 0) close(fd); return 1; }
    size_t n = (size_t)size;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) { close(fd); return 1; }
    size_t pages = (n + (size_t)page_size - 1u) / (size_t)page_size;
    unsigned char *resident = malloc(pages);
    void *mapping = mmap(NULL, n, PROT_READ, MAP_SHARED, fd, 0);
    int rc = !resident || mapping == MAP_FAILED;
    if (!rc && cold) {
        rc = fsync(fd) != 0 || madvise(mapping, n, MADV_DONTNEED) != 0 ||
             posix_fadvise(fd, 0, (off_t)n, POSIX_FADV_DONTNEED) != 0;
    }
    if (!rc) rc = mincore(mapping, n, resident) != 0;
    if (!rc) for (size_t i = 0; i < pages; i++) {
        if (((resident[i] & 1u) != 0) == (cold != 0)) { rc = 1; break; }
    }
    if (mapping != MAP_FAILED) munmap(mapping, n);
    free(resident); close(fd);
    if (rc) fprintf(stderr, "FAIL required %s source residency %s\n", cold ? "cold" : "warm", path);
    return rc;
}
/* Cache checks on a pathname alone are insufficient if attach/snapshot
 * materialises anonymous copies. Check the actual large source spans too.
 * The generated save history inserts only 1/2-byte ADD pieces; spans >2 bytes
 * cover the base. Require majority coverage and reject ANY resident base page. */
static int source_spans_cold(const piece_tree *tree, uint64_t size, int quiet)
{
    long system_page = sysconf(_SC_PAGESIZE);
    if (system_page <= 0) return 1;
    size_t page = (size_t)system_page;
    piece_iter iter; piece_iter_begin(&iter, tree, 0);
    const uint8_t *bytes; size_t n; uint64_t checked = 0;
    while (piece_iter_next(&iter, &bytes, &n)) {
        if (n <= 2) continue;
        uintptr_t address = (uintptr_t)bytes;
        size_t head = (size_t)(address % page);
        if (n > SIZE_MAX - head) return 1;
        size_t length = n + head, pages = length / page + (length % page != 0);
        unsigned char *resident = malloc(pages);
        if (!resident) return 1;
        int rc = mincore((void *)(address - head), length, resident) != 0;
        if (!rc) for (size_t i = 0; i < pages; i++) if (resident[i] & 1u) { rc = 1; break; }
        free(resident);
        if (rc) { if (!quiet) fprintf(stderr, "FAIL cold source: actual base spans are resident/materialised\n"); return 1; }
        checked += n;
    }
    return checked < size / 2u;
}
static int open_row(work_pool *pool, view *v, const char *name, const char *path, uint64_t size,
                    int sparse, unsigned jobs, unsigned samples, int track, int cold)
{
    printf("BEGIN G5_%s_%s busy%u samples=%u\n", cold ? "cold" : "warm", name, jobs, samples);
    uint8_t *expected = malloc(FILE_PREFIX_MAX); size_t n = 0;
    uint64_t *prefix_times = calloc(samples, sizeof(uint64_t)), *times = calloc(samples, sizeof(uint64_t));
    if (!expected || !times || !prefix_times) return 1;
    int rc = fixture(path, size, sparse, expected, &n, 0);
    if (!rc) rc = layout_prefix(v, expected, n);
    if (rc) { free(expected); free(times); free(prefix_times); return 1; }
    memcpy(v->expected, v->cells, sizeof v->cells);
    bench_samples prefix, endpoint;
    bench_samples_init(&prefix, prefix_times, samples); bench_samples_init(&endpoint, times, samples);
    for (unsigned i = 0; i < samples && !rc; i++) {
        if (source_cache(path, cold ? size : n, cold)) { rc = 1; break; }
        busy_jobs busy;
        if (start_busy(pool, &busy, jobs)) { rc = 1; break; }
        file *f = NULL; file_open_opts opts = {.generation = i + 1u};
        uint64_t start = bench_now_ns();
        int opened = file_open_begin(pool, path, &opts, &f);
        collector c = {.f = f, .kind = FILE_MSG_PREFIX_READY, .generation = opts.generation,
                       .backend = &v->backend};
        v->pending = &c;
        rc = opened != FILE_OK;
        if (!rc) rc = await_message(pool, &c) || c.msg.status != FILE_OK ||
                      !file_prefix_ready(f) || c.msg.size != n;
        uint64_t acquired = bench_now_ns();
        size_t got = 0; const uint8_t *bytes = f ? file_prefix(f, &got) : NULL;
        if (!rc) rc = file_size(f) != size || !prefix_correct(bytes, got, expected, n);
        if (!rc) rc = layout_prefix(v, bytes, got);
        if (!rc) rc = memcmp(v->expected, v->cells, sizeof v->cells) != 0 || render_grid_validate(&v->grid) != 0;
        if (!rc) rc = submit_view(v, pool);
        uint64_t submitted = bench_now_ns();
        stop_busy(pool, &busy);
        if (!rc) { (void)bench_add(&prefix, acquired - start); (void)bench_add(&endpoint, submitted - start); }
        if (finish_frame(v, pool)) rc = 1;
        rc |= c.bad || c.failed;
        v->pending = NULL;
        if (f) file_close(f);
        collector drain = {0}; (void)work_mailbox_drain(pool, collect, &drain);
        if ((i + 1u) % 10u == 0) printf("PROGRESS G5_%s busy%u done=%u/%u\n", name, jobs, i + 1u, samples);
    }
    char row[128]; snprintf(row, sizeof row, "primitive_prefix_%s_busy%u", name, jobs);
    rc |= report(row, &prefix, 0, 0, 1);
    snprintf(row, sizeof row, "G5_%s_%s_busy%u", cold ? "cold" : "warm", name, jobs);
    rc |= report(row, &endpoint, cold ? 10000000 : 6000000, cold ? 110000000 : 9000000, track);
    free(expected); free(times); free(prefix_times); return rc;
}
static int output_correct(const char *path, const piece_snapshot *s)
{
    int fd = open(path, O_RDONLY); struct stat st;
    uint64_t n = piece_snapshot_len(s);
    if (fd < 0) return 1;
    int rc = fstat(fd, &st) != 0 || st.st_size < 0 || (uint64_t)st.st_size != n;
    uint8_t disk[65536], want[65536];
    for (uint64_t off = 0; off < n && !rc; off += sizeof disk) {
        size_t part = n - off < sizeof disk ? (size_t)(n - off) : sizeof disk;
        rc = read_exact(fd, disk, part, (off_t)off) || piece_snapshot_read(s, off, want, part) != 0 || memcmp(disk, want, part) != 0;
    }
    close(fd); return rc;
}
static int make_target(const char *dst, uint64_t n)
{
    /* A deterministic generated fixture; no shell cp, no unchecked writes. */
    int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0640);
    if (fd < 0) return 1;
    uint8_t block[65536];
    for (size_t i = 0; i < sizeof block; i++) block[i] = i % 61u == 60u ? '\n' : (uint8_t)('a' + i % 26u);
    int rc = 0;
    for (uint64_t off = 0; off < n && !rc; off += sizeof block) {
        size_t part = n - off < sizeof block ? (size_t)(n - off) : sizeof block, done = 0;
        while (done < part) {
            ssize_t got = write(fd, block + done, part - done);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) { rc = 1; break; }
            done += (size_t)got;
        }
    }
    if (close(fd) != 0) rc = 1;
    return rc;
}
static int saving_shown(view *v, work_pool *pool)
{
    /* G8s ends after a real status row is submitted. Preserve the document
     * viewport and damage only this row after the initial full frame. No
     * temporary tree/layout/allocation is needed for the indicator. */
    if (render_frame_begin(&v->grid, ++v->serial) != 0) return 1;
    size_t first = (ROWS - 1u) * COLS;
    for (size_t i = 0; i < COLS; i++)
        v->cells[first + i] = (render_cell){.atlas_slot = RENDER_NO_SLOT, .fg = 0xffffff};
    for (size_t i = 0; i < 6; i++) {
        uint32_t cp = (uint32_t)"saving"[i];
        v->cells[first + i] = (render_cell){.glyph_index = cp, .atlas_slot = cp - 0x20u, .fg = 0xffffff};
    }
    if (v->backend.full_required) {
        if (render_mark_full(&v->grid) != 0) return 1;
    } else if (render_mark_rows(&v->grid, ROWS - 1u, 1) != 0) return 1;
    for (size_t i = 0; i < 6; i++) if (v->cells[first + i].glyph_index != (uint32_t)"saving"[i]) return 1;
    return submit_view(v, pool);
}
static int save_row(work_pool *pool, view *v, uint64_t size, unsigned jobs, unsigned samples,
                    int fresh, int track, int cold)
{
    printf("BEGIN save bytes=%llu busy%u fresh=%d cold=%d samples=%u\n",
           (unsigned long long)size, jobs, fresh, cold, samples);
    struct statvfs space;
    if (statvfs("/tmp", &space) || (uint64_t)space.f_bavail * space.f_frsize < size * 3u + (64u << 20)) {
        fprintf(stderr, "FAIL required save fixture: insufficient space\n"); return 1;
    }
    char dir[] = "/tmp/edit-file-bench-XXXXXX", path[128], source[128];
    if (!mkdtemp(dir)) return 1;
    snprintf(path, sizeof path, "%s/target", dir);
    snprintf(source, sizeof source, "%s/original", dir);
    int rc = make_target(path, size);
    if (!rc) rc = link(path, source) != 0;
    file *f = NULL; piece_tree *t = NULL;
    uint64_t *at = calloc(samples, sizeof(uint64_t)), *dt = calloc(samples, sizeof(uint64_t));
    if (!at || !dt) rc = 1;
    if (!rc) rc = file_open_begin(pool, path, NULL, &f) != FILE_OK;
    if (!rc) {
        collector c = {.f = f, .kind = FILE_MSG_OPEN_READY};
        rc = await_message(pool, &c) || c.msg.status != FILE_OK || c.msg.size != size ||
             !file_open_ready(f) || !file_prefix_ready(f);
    }
    piece_allocator a = piece_default_allocator();
    if (!rc) { t = piece_create(&a); rc = !t || file_attach(f, t) != FILE_OK; }
    /* A fragmented history, not the original 20 tiny edits. */
    if (!rc) for (uint64_t i = 0; i < 4096; i++) {
        uint64_t off = (i * 997u) % piece_len(t);
        if (piece_insert(t, off, (const uint8_t *)"xy", 2) != 0) { rc = 1; break; }
    }
    bench_samples ack, durable;
    bench_samples_init(&ack, at, samples); bench_samples_init(&durable, dt, samples);
    for (unsigned i = 0; i < samples && !rc; i++) {
        if (fresh && prepare_fresh(t, i + 1u)) { rc = 1; break; }
        if (source_cache(source, size, cold) || (cold && source_spans_cold(t, size, 0))) { rc = 1; break; }
        busy_jobs busy; if (start_busy(pool, &busy, jobs)) { rc = 1; break; }
        uint32_t generation = i + 1u;
        collector c = {.f = f, .kind = FILE_MSG_SAVE_DONE, .generation = generation, .backend = &v->backend};
        v->pending = &c;
        uint64_t start = bench_now_ns();
        rc = file_save_begin(f, t, 0, generation) != FILE_OK;
        uint64_t enqueued = bench_now_ns();
        if (!rc) rc = saving_shown(v, pool);
        uint64_t shown = bench_now_ns();
        stop_busy(pool, &busy);
        if (!rc) rc = await_message(pool, &c) || !completion_correct(&c.msg, f, generation, piece_len(t));
        uint64_t complete = bench_now_ns();
        if (!rc) rc = finish_save(pool, &c);
        if (!rc) {
            piece_snapshot *snap = piece_snapshot_take(t);
            rc = !snap || output_correct(path, snap);
            if (snap) piece_snapshot_release(snap);
        }
        if (!rc) {
            (void)bench_add(&ack, fresh ? shown - start : enqueued - start);
            (void)bench_add(&durable, complete - start);
        }
        if (finish_frame(v, pool)) rc = 1;
        v->pending = NULL;
        if ((i + 1u) % 10u == 0) printf("PROGRESS save busy%u done=%u/%u\n", jobs, i + 1u, samples);
    }
    char name[128];
    snprintf(name, sizeof name, "%s_%s_busy%u", fresh ? "G8s_fresh_fragmented_status" : "primitive_unchanged_save_enqueue", size > (1u << 20) ? "1GiB" : "1MiB", jobs);
    rc |= report(name, &ack, fresh ? 2000000 : 0, fresh ? 5000000 : 0, fresh ? track : 1);
    if (jobs == 0 && fresh) {
        snprintf(name, sizeof name, "G8d_%s_%s_source", size > (1u << 20) ? "1GiB" : "1MiB", cold ? "cold" : "warm");
        rc |= report(name, &durable, size > (1u << 20) ? (cold ? 2200000000 : 1500000000) : 10000000,
                     size > (1u << 20) ? (cold ? 3000000000 : 2500000000) : 50000000, track);
    }
    if (rc) fprintf(stderr, "FAIL %s: missing/failed completion, output, endpoint, or gate\n", name);
    if (t) piece_destroy(t);
    if (f) file_close(f);
    collector drain = {.backend = &v->backend}; (void)work_mailbox_drain(pool, collect, &drain);
    unlink(path); unlink(source);
    /* Leave unexpected temp files visible as a correctness failure. */
    if (rmdir(dir) != 0) { fprintf(stderr, "FAIL cleanup %s\n", dir); rc = 1; }
    free(at); free(dt); return rc;
}
int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--self-check") == 0) return self_check((unsigned)strtoul(argv[2], NULL, 10));
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) return 2;
    int track = 0, cold = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--track") == 0) track = 1;
        else if (strcmp(argv[i], "--cold") == 0) cold = 1;
        else { fprintf(stderr, "usage: %s [--track] [--cold] | --self-check 26|27|28\n", argv[0]); return 2; }
    }
    char power[32]; bench_battery_status(power, sizeof power);
    FILE *load = fopen("/proc/loadavg", "r"); double load1 = 0;
    if (load) { if (fscanf(load, "%lf", &load1) != 1) load1 = 0; fclose(load); }
    printf("file_bench power=%s %s load1=%.2f mode=%s; real CPU raster submit on DISPLAY=%s\n",
           power, bench__tag_from_power(power), load1, track ? "TRACK" : "GATE", getenv("DISPLAY") ? getenv("DISPLAY") : "unset");
    trace_init();
    work_pool pool; if (work_pool_init(&pool, 1, 4) != 0) return 2;
    view *v = calloc(1, sizeof *v); int rc = !v || view_init(v, &pool);
    if (rc) fprintf(stderr, "FAIL required viewport renderer initialization\n");
    const char *paths[] = {"/tmp/edit-corpus/sparse_10g.bin", "/tmp/edit-corpus/oneline_1g.txt", "/tmp/edit-corpus/log_1g.txt", "/tmp/edit-corpus/unicode.txt"};
    const char *names[] = {"sparse10g", "oneline1g", "log1g", "unicode_copy"};
    const uint64_t sizes[] = {10ull << 30, 1ull << 30, 1ull << 30, 1ull << 20};
    const unsigned conditions[] = {0, 1, 3};
    /* Default gate run requires BOTH suites. --cold is the separately invoked
     * manual cold suite; TRACK records warm data without claiming cold coverage. */
    unsigned suites = !track && !cold ? 2u : 1u;
    for (unsigned suite = 0; suite < suites && v && v->backend.initialized; suite++) {
        int is_cold = cold || suite == 1u;
        for (size_t j = 0; j < 3; j++) {
            for (size_t i = 0; i < 4; i++)
                rc |= open_row(&pool, v, names[i], paths[i], sizes[i], i == 0,
                               conditions[j], SAMPLES, track, is_cold);
            if (!is_cold) rc |= save_row(&pool, v, 1u << 20, conditions[j], SAMPLES, 1, track, 0);
        }
        if (!is_cold) rc |= save_row(&pool, v, 1u << 20, 0, SAMPLES, 0, track, 0);
        rc |= save_row(&pool, v, 1ull << 30, 0, track ? 10u : SAMPLES, 1, track, is_cold);
    }
    if (track && !cold)
        printf("MANUAL cold rows unmeasured: --cold evicts + verifies residency before EACH sample; G8d cold 2200/3000 ms (G); see docs/decisions/P1.7f.md\n");
    if (v) { render_backend_shutdown(&v->backend); free(v->state); if (v->platform.conn) plat_shutdown(&v->platform); free(v); }
    work_pool_shutdown(&pool); return rc;
}
