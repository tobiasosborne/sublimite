#include "editor/editor.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "font/font.h"
#include "trace/trace.h"
#include "trace/trace_fmt.h"
#include "base/base.h"
#include "harness.h"
#include "work/work.h"
#include "scan/scan.h"
#include "find/find.h"
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define G1_P50 UINT64_C(1000000)
#define G1_P99 UINT64_C(2000000)
#define G11_P50 UINT64_C(100000)
#define G11_P99 UINT64_C(200000)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "editor_bench:%d failed: %s\n", __LINE__, #c); return -1; } } while (0)
typedef struct stamp { char status[32], load[24]; const char *power; } stamp;
static stamp power_stamp(void)
{
    stamp s = {.status = "unknown", .load = "unknown", .power = "[unknown]"};
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(s.status, sizeof s.status, f)) s.status[strcspn(s.status, "\n")] = 0; fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fgets(s.load, sizeof s.load, f)) s.load[strcspn(s.load, " \n")] = 0; fclose(f); }
    if (!strcmp(s.status, "Charging") || !strcmp(s.status, "Full") || !strcmp(s.status, "Not charging")) s.power = "[AC]";
    else if (!strcmp(s.status, "Discharging")) s.power = "[bat]";
    printf("STAMP (M)%s BAT0=%s load1=%s shared box\n", s.power, s.status, s.load);
    return s;
}
typedef struct key_sample {
    uint64_t injected, dequeued, submit, t4;
    uint32_t frame;
} key_sample;
typedef struct samples {
    editor_frame frame;
    uint64_t ingress, sequence;
    bool guard, suspended, measuring;
    size_t allocations;
    key_sample *keys;
    size_t count, completed;
    uint64_t first_sequence;
    bool invalid;
} samples;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    samples *s = ctx; s->ingress = ns; s->sequence = sequence;
    if (s->keys) {
        if (sequence < s->first_sequence || sequence - s->first_sequence >= s->count) s->invalid = true;
        else {
            key_sample *k = &s->keys[sequence - s->first_sequence];
            if (!k->injected || k->dequeued || ns < k->injected) s->invalid = true;
            k->dequeued = ns;
        }
    }
    if (s->measuring && !s->guard && !s->suspended) { edit_malloc_guard_begin(); s->guard = true; }
}
static void submitted(void *ctx, const editor_frame *f)
{
    samples *s = ctx;
    if (!f->last_sequence) return;
    if (s->guard) { s->allocations += edit_malloc_guard_end(); s->guard = false; }
    s->frame = *f;
    if (s->keys) {
        for (uint64_t seq = f->first_sequence; seq <= f->last_sequence; seq++) {
            if (seq < s->first_sequence) continue;
            if (seq - s->first_sequence >= s->count) { s->invalid = true; break; }
            key_sample *k = &s->keys[seq - s->first_sequence];
            if (k->submit || !k->dequeued || f->submit_ns < k->dequeued) s->invalid = true;
            k->submit = f->submit_ns; k->frame = f->id;
        }
    }
}
static void presented(void *ctx, const editor_frame *f)
{
    samples *s = ctx; if (f->last_sequence) s->frame = *f;
    if (s->keys && f->last_sequence) {
        for (uint64_t seq = f->first_sequence; seq <= f->last_sequence; seq++) {
            if (seq < s->first_sequence) continue;
            if (seq - s->first_sequence >= s->count) { s->invalid = true; break; }
            key_sample *k = &s->keys[seq - s->first_sequence];
            if (k->t4 || k->frame != f->id || f->present_ns < k->submit) s->invalid = true;
            else { k->t4 = f->present_ns; s->completed++; }
        }
    }
}
static void io_boundary(void *ctx, bool entering)
{
    samples *s = ctx;
    if (entering && s->guard) {
        s->allocations += edit_malloc_guard_end(); s->guard = false; s->suspended = true;
    } else if (!entering && s->suspended) {
        edit_malloc_guard_begin(); s->guard = true; s->suspended = false;
    }
}
static bool settled(editor_stats s, const render_backend *b)
{
    return !s.pending && (!s.render_active || b->presented);
}
static int settle_self_check(void)
{
    render_backend b = {.active = true, .presented = true};
    editor_stats s = {.render_active = true};
    REQUIRE(settled(s, &b));
    s.pending = true; REQUIRE(!settled(s, &b));
    s.pending = false; b.presented = false; REQUIRE(!settled(s, &b));
    puts("editor_bench: settle permits undamaged submitted frame with delayed Present completion");
    return 0;
}
static int settle(editor *e)
{
    uint64_t deadline = bench_now_ns() + UINT64_C(5000000000);
    for (;;) {
        editor_stats before = editor_get_stats(e);
        if (settled(before, e->backend)) return 0;
        int rc = editor_step(e, 20); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (settled(s, e->backend)) return 0;
        if (bench_now_ns() >= deadline) {
            fprintf(stderr, "SETTLE deadline: view_busy=%d layout_busy=%d view_query_byte=%" PRIu64 " layout_row=%u phase=%u mutations=%" PRIu64 " slice_max_ns=%" PRIu64 "\n",
                view_busy(&e->v), layout_busy(&e->lay), e->v.query_pos,
                e->lay.row, e->lay.phase, s.mutations, s.longest_slice_ns);
            return -1;
        }
    }
}
/* Rows ending at T5 explicitly drain the retained backend slot. The main settle endpoint is damage submitted (T4). */
static int settle_ready(editor *e)
{
    uint64_t deadline = bench_now_ns() + UINT64_C(5000000000);
    e->caret_only = false;
    do {
        int rc = editor_step(e, 20); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
        if (bench_now_ns() >= deadline) {
            fprintf(stderr, "SETTLE deadline: view_busy=%d layout_busy=%d view_query_byte=%" PRIu64 " layout_row=%u phase=%u mutations=%" PRIu64 " slice_max_ns=%" PRIu64 "\n",
                view_busy(&e->v), layout_busy(&e->lay), e->v.query_pos,
                e->lay.row, e->lay.phase, s.mutations, s.longest_slice_ns);
            return -1;
        }
    } while (true);
}
static plat_event event(bool back)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = back ? XKB_KEY_BackSpace : XKB_KEY_x};
    if (!back) { ev.utf8_len = 1; ev.utf8[0] = 'x'; } return ev;
}
static uint64_t process_ns(void)
{
    struct timespec t; (void)clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static int observe_quiet(editor *e, uint64_t *background)
{
    uint64_t before = editor_get_stats(e).poll_returns;
    uint64_t deadline = bench_now_ns() + UINT64_C(1000000000);
    while (bench_now_ns() < deadline) {
        uint64_t left = deadline - bench_now_ns();
        if (left > UINT64_C(1000000000)) break;
        int ms = (int)((left + UINT64_C(999999)) / UINT64_C(1000000));
        int rc = editor_step(e, ms); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
    }
    uint64_t returns = editor_get_stats(e).poll_returns - before;
    *background = returns ? returns - 1 : 0;
    return 0;
}
static int row_backend(render_backend *b, const char *name)
{
    return !strcmp(name, "null") ? render_null_backend(b) : editor_backend_select(b, name);
}
static const char *row_label(const render_backend *b, const char *requested)
{
    if (!strcmp(requested, "gl") && !(b->info.capabilities & RENDER_CAP_GPU)) return "gl_fallback_raster";
    return requested;
}
/* Kept in one place so benchmark regressions exercise the row policy. */
static editor_config target_a_config(void)
{
    font_cell font = font_ascii_cell();
    editor_config cfg = {.cols = 2880 / font.cell_w, .rows = 1800 / font.cell_h, .font_px = font_ascii_px()};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows;
    return cfg;
}
static int row_result(int timing, bool structural, bool track)
{
    return structural ? 1 : track ? 0 : timing;
}
static int gate_row(const char *name, const bench_samples *s, uint64_t p50, uint64_t p99,
                    bool track, const stamp *tag)
{
    return bench_gate_report(name, s, p50, p99, BENCH_INTERACTION_MIN_N, track, tag->status, tag->load);
}
/* Called only after editor_close has joined every recording worker. */
static void typing_trace(const char *label, const char *power)
{
    const char *dump = getenv("EDIT_TRACE_DUMP");
    if (!dump) return;
    FILE *out = fopen(dump, "wb");
    if (!out) return;
    int rc = trace_dump(out); fclose(out); if (rc) return;
    FILE *in = fopen(dump, "rb"); if (!in) return;
    trace_rec *recs = NULL; size_t n = 0;
    rc = trace_fmt_load(in, &recs, &n); fclose(in); if (rc) return;
    const enum trace_ev from[] = {TRACE_T1_DEQUEUE, TRACE_T2_MUTATION_DONE, TRACE_T3_RENDER_DONE};
    const enum trace_ev to[] = {TRACE_T2_MUTATION_DONE, TRACE_T3_RENDER_DONE, TRACE_T4_PRESENT_SUBMITTED};
    const char *names[] = {"T2-T1", "T3-T2", "T4-T3"};
    for (size_t i = 0; i < sizeof from / sizeof from[0]; i++) {
        uint64_t *values = NULL; size_t count = 0;
        if (!trace_fmt_latencies(recs, n, from[i], to[i], &values, &count)) {
            uint64_t p50 = trace_fmt_pct(values, count, 50), p99 = trace_fmt_pct(values, count, 99);
            printf("TRACE %s %s n=%zu p50_ns=%" PRIu64 " p99_ns=%" PRIu64 " (M)%s\n", label, names[i], count, p50, p99, power);
        }
        free(values);
    }
    free(recs);
}
static int idle_row(const char *requested, bool track, bool require_gl)
{
    stamp tag = power_stamp();
    render_backend b = {0}; REQUIRE(row_backend(&b, requested) == 0);
    /* G11 uses an asserted A viewport. Process CPU time includes every
     * worker and is an upper bound on summed per-thread running time. */
    editor_config cfg = target_a_config(); cfg.raster_fallback = true;
    editor *e = NULL; REQUIRE(editor_open(&e, &cfg, &b) == 0);
    REQUIRE(!require_gl || strcmp(requested, "gl") || (b.info.capabilities & RENDER_CAP_GPU));
    REQUIRE(b.config.dims.cols * b.config.dims.cell_w == 2880);
    REQUIRE(b.config.dims.rows * b.config.dims.cell_h == 1800);
    printf("TARGET A=%ux%u font_px=%u (M)%s\n", b.config.dims.cols * b.config.dims.cell_w,
           b.config.dims.rows * b.config.dims.cell_h, cfg.font_px, tag.power);
    const char *label = row_label(&b, requested);
    printf("BACKEND requested=%s actual=%s init_error=%d\n", requested, b.info.name, editor_get_stats(e).backend_init_error);
    REQUIRE(settle(e) == 0);
    REQUIRE(editor_grid(e)->dims.cols * editor_grid(e)->dims.cell_w == 2880);
    REQUIRE(editor_grid(e)->dims.rows * editor_grid(e)->dims.cell_h == 1800);
    uint64_t values[32]; bench_samples cpu; bench_samples_init(&cpu, values, 32);
    uint64_t worker_jobs = 0, inline_frames = 0;
    uint64_t wake_start = editor_get_stats(e).poll_returns, wall = bench_now_ns();
    bool measuring = !b.active;
    while (editor_get_stats(e).blinking) {
        uint64_t blink_count = editor_get_stats(e).blinks, start = process_ns();
        int rc = editor_step(e, -1); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        REQUIRE(settle(e) == 0);
        if (!measuring && !b.active && !editor_get_stats(e).pending && !editor_get_stats(e).blinks) {
            /* Setup completion is outside the blink window. No separate
             * Present wait or completion deadline when nothing is damaged. */
            wake_start = editor_get_stats(e).poll_returns; wall = bench_now_ns(); measuring = true;
        }
        if (editor_get_stats(e).blinks > blink_count) {
            REQUIRE(measuring);
            (void)bench_add(&cpu, process_ns() - start);
            raster_metrics m;
            if (raster_frame_metrics(&b, &m)) {
                worker_jobs += m.jobs + m.completion_jobs;
                inline_frames += m.inline_cells != 0;
            }
        }
        REQUIRE(bench_now_ns() - wall < UINT64_C(15000000000));
    }
    editor_stats s = editor_get_stats(e);
    uint64_t wakes = s.poll_returns - wake_start;
    uint64_t elapsed = bench_now_ns() - wall;
    double rate = (double)wakes * 1e9 / (double)elapsed;
    char name[80]; (void)snprintf(name, sizeof name, "editor_%s_G11_process_cpu", label);
    int miss = gate_row(name, &cpu, G11_P50, G11_P99, track, &tag);
    /* A bounded observation adds exactly one external test-deadline timeout.
     * Subtract that known timeout, retaining all earlier poll returns. */
    uint64_t quiet; REQUIRE(observe_quiet(e, &quiet) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    REQUIRE(editor_inject(e, &focus) == 0); REQUIRE(settle(e) == 0);
    uint64_t unfocused; REQUIRE(observe_quiet(e, &unfocused) == 0);
    bool structural = wakes > 20 || quiet || unfocused || worker_jobs;
    printf("G11 %s (M)%s load1=%s blinks=%" PRIu64 " poll_returns=%" PRIu64 " wakeups/s=%.3f idle=%" PRIu64 " unfocused=%" PRIu64 " (G)<=2/s,0,0 mode=%s structural=%s\n",
        label, tag.power, tag.load, s.blinks, wakes, rate, quiet, unfocused, track ? "TRACK" : "GATE", structural ? "FAIL" : "OK");
    /* The fixed ten-second blink policy permits nineteen blinks plus its
     * terminal timeout. The observation starts after setup, so a raw rate
     * can exceed 2 slightly solely from that truncated first interval. */
    printf("G11 %s structural worker_jobs=%" PRIu64 " inline_frames=%" PRIu64 "\n", label, worker_jobs, inline_frames);
    editor_close(e); return row_result(miss, structural, track);
}
/* Real bounded kernels share the editor backend's public worker pool. Their
 * immutable corpus mapping and output spool are setup-only reservations. */
typedef struct bulk_job {
    const uint8_t *bytes;
    size_t length, offset;
    unsigned kind;
    int fd;
    _Atomic uint64_t chunks, max_cpu;
    _Atomic bool failed;
    uint64_t checksum;
} bulk_job;
typedef struct bulk_mix {
    bulk_job jobs[3];
    work_handle handles[3];
    work_pool *pool;
    const uint8_t *mapping;
    size_t length, count;
    int fd;
} bulk_mix;
static uint64_t thread_ns(void)
{
    struct timespec t;
    (void)clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void bulk_chunk(work_ctx *ctx)
{
    bulk_job *job = ctx->arg;
    if (work_should_stop(ctx)) return;
    uint64_t start = thread_ns();
    size_t n = job->length - job->offset;
    if (n > 65536) n = 65536;
    const uint8_t *p = job->bytes + job->offset;
    bool failed = false;
    if (job->kind == 0) {
        scan_counts c = scan_count(p, n); job->checksum += c.newlines;
    } else if (job->kind == 1) {
        find_source source = {.bytes = p, .length = n};
        find_control control = {.work = ctx}; find_result result;
        find_code rc = find_literal(&source, (const uint8_t *)"ERROR", 5, &control, &result);
        if (rc == FIND_CANCELLED) return;
        failed = rc != FIND_OK; job->checksum += result.total;
    } else {
        /* Bounded save-write kernel; reuse a fixed-size spool, never the corpus.
         * Durability/rename latency is outside this foreground contention row. */
        failed = pwrite(job->fd, p, n, (off_t)(job->offset % (16u * 1024u * 1024u))) != (ssize_t)n;
    }
    uint64_t cpu = thread_ns() - start;
    if (cpu > atomic_load(&job->max_cpu)) atomic_store(&job->max_cpu, cpu);
    if (failed) { atomic_store(&job->failed, true); return; }
    job->offset += n;
    if (job->offset == job->length) job->offset = 0;
    atomic_fetch_add(&job->chunks, 1);
    (void)work_continue(ctx);
}
static int bulk_start(bulk_mix *mix, render_backend *b, size_t count)
{
    *mix = (bulk_mix){.pool = b->config.workers, .fd = -1};
    REQUIRE(mix->pool != NULL && count <= 3);
    int fd = open("/tmp/edit-corpus/log_1g.txt", O_RDONLY); REQUIRE(fd >= 0);
    struct stat st; int rc = fstat(fd, &st);
    if (rc != 0 || st.st_size <= 0 || (uintmax_t)st.st_size > SIZE_MAX) { close(fd); return -1; }
    mix->length = (size_t)st.st_size;
    void *mapping = mmap(NULL, mix->length, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    REQUIRE(mapping != MAP_FAILED); mix->mapping = mapping;
    char path[] = "build/editor-bulk-XXXXXX";
    mix->fd = mkstemp(path); REQUIRE(mix->fd >= 0); REQUIRE(unlink(path) == 0);
    work_job jobs[3];
    for (size_t i = 0; i < count; i++) {
        mix->jobs[i] = (bulk_job){.bytes = mix->mapping, .length = mix->length, .kind = (unsigned)i, .fd = mix->fd};
        jobs[i] = (work_job){.fn = bulk_chunk, .arg = &mix->jobs[i], .cls = WORK_BULK};
    }
    REQUIRE(work_submit_batch(mix->pool, jobs, count, mix->handles) == 0);
    mix->count = count;
    return 0;
}
static void bulk_stop(bulk_mix *mix)
{
    for (size_t i = 0; i < mix->count; i++) work_cancel(mix->pool, mix->handles[i]);
    /* Arguments and mapping outlive physical cancellation acknowledgement. */
    for (size_t i = 0; i < mix->count; i++) {
        while (!work_handle_finished(mix->pool, mix->handles[i])) {
            struct timespec pause = {.tv_nsec = 100000}; (void)nanosleep(&pause, NULL);
        }
    }
    if (mix->mapping) (void)munmap((void *)mix->mapping, mix->length);
    if (mix->fd >= 0) close(mix->fd);
    *mix = (bulk_mix){.fd = -1};
}
static bool typing_structure(const samples *measured, editor_stats before, editor_stats after, size_t count)
{
    return measured->invalid || measured->completed != count || measured->allocations != 0 ||
        measured->guard || measured->suspended || after.journal_error != 0 ||
        after.mutations - before.mutations != count || after.journal_records - before.journal_records != count;
}
#define TYPING_REQUIRE(c) do { if (!(c)) { fprintf(stderr, "editor_bench:%d failed: %s\n", __LINE__, #c); goto cleanup; } } while (0)
static int typing_row(const char *requested, size_t count, bool track, bool require_gl,
                      unsigned scenario, uint64_t ingress_delay, const char *path, bool partial)
{
    trace_reset();
    const char *workload = scenario == 0 ? "serial" : scenario == 1 ? "queued_active_frame" :
                           scenario == 2 ? "active_index" : "queued_index_find_save";
    int result = -1;
    uint64_t *values = NULL;
    key_sample *keys = NULL;
    bulk_mix mix = {.fd = -1};
    editor *e = NULL;
    const char *label = requested;
    bool journal_created = false;
    samples measured = {0};
    stamp tag = power_stamp();
    char journal_path[] = "build/editor-bench-XXXXXX";
    render_backend b = {0}; TYPING_REQUIRE(row_backend(&b, requested) == 0);
    int fd = mkstemp(journal_path); TYPING_REQUIRE(fd >= 0); close(fd); journal_created = true;
    editor_config cfg = target_a_config();
    cfg.path = path; cfg.journal_path = journal_path;
    cfg.hook_ctx = &measured; cfg.on_ingress = ingress; cfg.on_submit = submitted;
    cfg.on_present = presented; cfg.on_io = io_boundary; cfg.raster_fallback = true;
    TYPING_REQUIRE(editor_open(&e, &cfg, &b) == 0);
    TYPING_REQUIRE(!require_gl || strcmp(requested, "gl") || (b.info.capabilities & RENDER_CAP_GPU));
    TYPING_REQUIRE(b.config.dims.cols * b.config.dims.cell_w == 2880);
    TYPING_REQUIRE(b.config.dims.rows * b.config.dims.cell_h == 1800);
    label = row_label(&b, requested);
    printf("BACKEND requested=%s actual=%s init_error=%d TARGET A=%ux%u font_px=%u (M)%s\n",
           requested, b.info.name, editor_get_stats(e).backend_init_error,
           b.config.dims.cols * b.config.dims.cell_w, b.config.dims.rows * b.config.dims.cell_h, cfg.font_px, tag.power);
    TYPING_REQUIRE(settle(e) == 0);
    /* No indexing warmup barrier: the contention fixtures retain background
     * work and a byte-positioned viewport through the public editor API. */
    TYPING_REQUIRE(editor_set_cursor(e, editor_length(e) * 9 / 10) == 0); TYPING_REQUIRE(settle(e) == 0);
    printf("POSITION %s byte=%" PRIu64 " index=%s workload=%s edits=alternating_insert_backspace\n",
           label, editor_view(e).selection.cursor, editor_index_complete(e) ? "published" : "progressing", workload);
    if (partial) {
        /* Setup only: destroy joins any index worker before replacing it. */
        lineidx_destroy(e->buffer->index);
        e->buffer->index = lineidx_create(editor_length(e));
        TYPING_REQUIRE(e->buffer->index != NULL);
        e->buffer->index_dirty = false;
        printf("INDEX %s deliberately_unbuilt before warmup; viewport anchor retained\n", label);
    }
    for (unsigned i = 0; i < 16; i++) {
        plat_event ev = event((i & 1u) != 0); TYPING_REQUIRE(editor_inject(e, &ev) == 0); TYPING_REQUIRE(settle(e) == 0);
    }
    values = malloc(3 * count * sizeof *values);
    keys = calloc(count, sizeof *keys); TYPING_REQUIRE(values && keys);
    bench_samples submit, t4, dequeue;
    bench_samples_init(&submit, values, count); bench_samples_init(&t4, values + count, count);
    bench_samples_init(&dequeue, values + 2 * count, count);
    if (scenario >= 2) TYPING_REQUIRE(bulk_start(&mix, &b, scenario == 2 ? 1u : 3u) == 0);
    uint64_t progress_before[3] = {0};
    for (size_t i = 0; i < mix.count; i++) progress_before[i] = atomic_load(&mix.jobs[i].chunks);
    editor_stats before = editor_get_stats(e);
    measured.keys = keys; measured.count = count; measured.first_sequence = before.input_sequence + 1;
    measured.measuring = true;
    size_t arrivals_active = 0, coalesced = 0;
    size_t batch = scenario == 0 ? 1u : 8u;
    for (size_t i = 0; i < count;) {
        size_t end = count - i > batch ? i + batch : count;
        for (size_t k = i; k < end; k++) {
            plat_event ev = event((k & 1u) != 0);
            keys[k].injected = bench_now_ns();
            if (editor_get_stats(e).render_active) arrivals_active++;
            TYPING_REQUIRE(editor_inject(e, &ev) == 0);
            /* The first key starts a frame; the rest arrive before its fence
             * and matching completion have drained. Null is a reference and
             * has no asynchronous active-frame interval. */
            if (k == i && batch > 1) {
                uint64_t deadline = bench_now_ns() + UINT64_C(5000000000);
                do {
                    TYPING_REQUIRE(editor_step(e, 0) >= 0);
                    TYPING_REQUIRE(bench_now_ns() < deadline);
                } while (!editor_get_stats(e).render_active && !keys[k].t4);
            }
        }
        if (ingress_delay) {
            struct timespec delay = {.tv_sec = (time_t)(ingress_delay / UINT64_C(1000000000)),
                .tv_nsec = (long)(ingress_delay % UINT64_C(1000000000))};
            TYPING_REQUIRE(nanosleep(&delay, NULL) == 0);
        }
        TYPING_REQUIRE(settle(e) == 0);
        TYPING_REQUIRE(!measured.invalid && measured.completed == end && !measured.guard && !measured.suspended);
        i = end;
    }
    measured.measuring = false;
    for (size_t i = 0; i < count; i++) {
        TYPING_REQUIRE(keys[i].t4 >= keys[i].submit && keys[i].submit >= keys[i].dequeued && keys[i].dequeued >= keys[i].injected);
        TYPING_REQUIRE(bench_add(&submit, keys[i].submit - keys[i].injected) == 0);
        TYPING_REQUIRE(bench_add(&t4, keys[i].t4 - keys[i].injected) == 0);
        TYPING_REQUIRE(bench_add(&dequeue, keys[i].submit - keys[i].dequeued) == 0);
        if (i && keys[i].frame == keys[i - 1].frame) coalesced++;
    }
    editor_stats after = editor_get_stats(e);
    bool structural = typing_structure(&measured, before, after, count);
    if (scenario >= 1 && strcmp(requested, "null") && count > 1 && !arrivals_active) structural = true;
    for (size_t i = 0; i < mix.count; i++) {
        uint64_t progress = atomic_load(&mix.jobs[i].chunks) - progress_before[i];
        uint64_t cpu = atomic_load(&mix.jobs[i].max_cpu);
        printf("BULK %s kernel=%s chunks=%" PRIu64 " max_chunk_cpu_ns=%" PRIu64 " (M)%s shared_editor_pool=1\n",
               workload, i == 0 ? "index_scan" : i == 1 ? "find_literal" : "save_write", progress, cpu, tag.power);
        structural |= progress == 0 || atomic_load(&mix.jobs[i].failed) || cpu > UINT64_C(5000000);
    }
    bulk_stop(&mix);
    char name[128];
    (void)snprintf(name, sizeof name, "editor_%s_%s_injection_submit", label, workload);
    /* Submit-return and dequeue diagnostics are descriptive. G1 ends at T4. */
    (void)gate_row(name, &submit, 0, 0, true, &tag);
    (void)snprintf(name, sizeof name, "editor_%s_%s_dequeue_submit", label, workload);
    (void)gate_row(name, &dequeue, 0, 0, true, &tag);
    (void)snprintf(name, sizeof name, "editor_%s_%s_injection_T4_G1", label, workload);
    int timing = gate_row(name, &t4, G1_P50, G1_P99, track, &tag);
    printf("G1 %s workload=%s keys=%zu completed=%zu arrivals_active=%zu coalesced=%zu allocations=%zu mutations=%" PRIu64
           " journal=%" PRIu64 " structural=%s mode=%s (M)%s\n", label, workload, count, measured.completed,
           arrivals_active, coalesced, measured.allocations, after.mutations - before.mutations,
           after.journal_records - before.journal_records, structural ? "FAIL" : "OK", track ? "TRACK" : "GATE", tag.power);
    TYPING_REQUIRE(editor_flush(e) == 0);
    result = row_result(timing, structural, track);
cleanup:
    measured.measuring = false;
    if (measured.guard) { measured.allocations += edit_malloc_guard_end(); measured.guard = false; }
    measured.suspended = false;
    bulk_stop(&mix);
    if (e) { editor_close(e); typing_trace(label, tag.power); }
    if (journal_created && unlink(journal_path) != 0) result = -1;
    free(keys); free(values);
    return result;
}
#undef TYPING_REQUIRE
static int tab_row(bool raster, bool track)
{
    stamp tag = power_stamp();
    render_backend b = {0}; REQUIRE((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    char text[32768]; size_t len = 0; const char line[] = "    int f() { return 42; }  \n";
    for (unsigned i = 0; i < 512; i++) { memcpy(text + len, line, sizeof line - 1); len += sizeof line - 1; }
    font_cell fc = font_ascii_atlas_for_px(15)->cell; samples measured = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = len,
        .cols = 2880 / fc.cell_w, .rows = 1800 / fc.cell_h, .font_px = 15, .arena_bytes = 4u * 1024u * 1024u,
        .history_keys = 128, .hook_ctx = &measured, .on_ingress = ingress,
        .on_submit = submitted, .on_present = presented, .on_io = io_boundary};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows;
    editor *e = NULL; REQUIRE(editor_open(&e, &cfg, &b) == 0);
    for (unsigned i = 1; i < 100; i++) { uint64_t id; REQUIRE(editor_add_buffer(e, NULL, cfg.initial, len, &id) == 0); }
    REQUIRE(settle_ready(e) == 0);
    REQUIRE(editor_grid(e)->dims.cols * editor_grid(e)->dims.cell_w == 2880);
    REQUIRE(editor_grid(e)->dims.rows * editor_grid(e)->dims.cell_h == 1800);
    uint64_t frame_values[200], map_values[200]; bench_samples frames, maps;
    bench_samples_init(&frames, frame_values, 200); bench_samples_init(&maps, map_values, 200);
    measured.measuring = true;
    for (unsigned i = 0; i < 200; i++) {
        uint64_t fills = editor_get_stats(e).minimap_fills;
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Tab, .mods = PLAT_MOD_CTRL};
        REQUIRE(editor_inject(e, &ev) == 0);
        ev.keysym = XKB_KEY_Control_L; ev.press = false; ev.mods = 0;
        REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle_ready(e) == 0);
        REQUIRE(measured.frame.present_ns >= measured.ingress && !measured.guard && !measured.suspended);
        /* G3 ends at compositor-ready T5, after the XShm fence, rather
         * than the T4 upload submission measured by the legacy key row. */
        REQUIRE(b.t5_sent && b.active_frame == measured.frame.id);
        uint64_t ready = b.device_ns > b.submitted_ns ? b.device_ns : b.submitted_ns;
        REQUIRE(ready >= measured.ingress);
        REQUIRE(editor_get_stats(e).minimap_fills > fills && !editor_get_stats(e).minimap_stale);
        (void)bench_add(&frames, ready - measured.ingress);
        (void)bench_add(&maps, editor_get_stats(e).minimap_ns);
    }
    measured.measuring = false;
    char name[96]; (void)snprintf(name, sizeof name, "editor_%s_100tabs_ingress_T5_G3", raster ? "raster" : "null");
    int miss = gate_row(name, &frames, UINT64_C(5000000), UINT64_C(5560000), track, &tag);
    (void)snprintf(name, sizeof name, "editor_%s_100tabs_minimap_inside_frame", raster ? "raster" : "null");
    miss = bench_merge_exit(miss, gate_row(name, &maps, 0, UINT64_C(500000), track, &tag));
    printf("G3 %s (M)%s load1=%s tabs=100 A=2880x1800 switches=200 allocations=%zu minimap_fills=%" PRIu64 " stale=%d mode=%s\n",
        raster ? "raster" : "null", tag.power, tag.load, measured.allocations,
        editor_get_stats(e).minimap_fills, editor_get_stats(e).minimap_stale, track ? "TRACK" : "GATE");
    bool structural = measured.allocations != 0; editor_close(e); return row_result(miss, structural, track);
}
static int ipc_row(bool track)
{
    stamp tag = power_stamp(); char relative[] = "build/editor-ipc-bench-XXXXXX", runtime[4096];
    REQUIRE(mkdtemp(relative) != NULL); REQUIRE(realpath(relative, runtime) != NULL);
    REQUIRE(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
    char path[8192], arg[16384]; (void)snprintf(path, sizeof path, "%s/file.txt", runtime);
    REQUIRE(strlen(path) + 5 < sizeof arg); strcpy(arg, path); strcat(arg, ":2:2");
    FILE *f = fopen(path, "w"); REQUIRE(f != NULL); REQUIRE(fputs("abc\ndef\n", f) >= 0); REQUIRE(fclose(f) == 0);
    ipc_server server = {0}; REQUIRE(ipc_server_init(&server, runtime) == IPC_OK);
    render_backend b = {0}; REQUIRE(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8, .server = &server, .start_empty = true,
        .arena_bytes = 4u * 1024u * 1024u, .history_keys = 128};
    editor *e = NULL; REQUIRE(editor_open(&e, &cfg, &b) == 0); REQUIRE(settle(e) == 0);
    uint64_t values[64]; bench_samples handoff; bench_samples_init(&handoff, values, 64);
    for (unsigned i = 0; i < 64; i++) {
        int pipefd[2]; REQUIRE(pipe(pipefd) == 0); pid_t child = fork(); REQUIRE(child >= 0);
        if (!child) {
            close(pipefd[0]); uint64_t start = bench_now_ns();
            if (write(pipefd[1], &start, sizeof start) != (ssize_t)sizeof start) _exit(126);
            close(pipefd[1]); execl("build/sublimite", "sublimite", arg, (char *)NULL); _exit(127);
        }
        close(pipefd[1]); uint64_t start = 0; REQUIRE(read(pipefd[0], &start, sizeof start) == (ssize_t)sizeof start); close(pipefd[0]);
        int status = 0; pid_t reaped = 0; uint64_t deadline = start + UINT64_C(30000000000);
        while (!reaped) {
            REQUIRE(editor_step(e, 1) >= 0); reaped = waitpid(child, &status, WNOHANG);
            REQUIRE(reaped >= 0 && bench_now_ns() < deadline);
        }
        uint64_t elapsed = bench_now_ns() - start;
        REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        REQUIRE(editor_get_stats(e).tabs == (size_t)i + 1 && editor_view(e).selection.cursor == 5);
        (void)bench_add(&handoff, elapsed); REQUIRE(settle(e) == 0);
    }
    int miss = gate_row("editor_second_invocation_exec_open_ACK_exit", &handoff, 0, UINT64_C(10000000), track, &tag);
    printf("IPC (M)%s load1=%s invocations=64 opened_tabs=%zu includes=exec,parse,open,ACK,exit,reap mode=%s\n",
        tag.power, tag.load, editor_get_stats(e).tabs, track ? "TRACK" : "GATE");
    editor_close(e); ipc_server_fini(&server); unlink(path);
    char lock[8192]; (void)snprintf(lock, sizeof lock, "%s/sublimite-%lu.lock", runtime, (unsigned long)getuid());
    unlink(lock); REQUIRE(rmdir(runtime) == 0); return miss;
}
int main(int argc, char **argv)
{
    bool track = false, idle = true, typing = true, p4 = true, ipc_only = false, require_gl = false, partial = false; size_t count = 10000;
    uint64_t ingress_delay = 0; unsigned scenarios = 4;
    const char *path = "/tmp/edit-corpus/log_1g.txt";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--self-check")) return settle_self_check();
        if (!strcmp(argv[i], "--track")) track = true;
        else if (!strcmp(argv[i], "--require-gl")) require_gl = true;
        else if (!strcmp(argv[i], "--partial-index")) partial = true;
        else if (!strcmp(argv[i], "--no-idle")) idle = false;
        else if (!strcmp(argv[i], "--idle-only")) { typing = false; p4 = false; }
        else if (!strcmp(argv[i], "--p4-only")) { typing = false; idle = false; }
        else if (!strcmp(argv[i], "--no-p4")) p4 = false;
        else if (!strcmp(argv[i], "--ipc-only")) { typing = false; idle = false; p4 = false; ipc_only = true; }
        else if (!strncmp(argv[i], "--file=", 7)) path = argv[i] + 7;
        else if (!strcmp(argv[i], "--serial-only")) scenarios = 1;
        else if (!strncmp(argv[i], "--ingress-delay-ms=", 19)) {
            char *end = NULL; errno = 0;
            unsigned long long ms = strtoull(argv[i] + 19, &end, 10);
            if (errno || end == argv[i] + 19 || *end || ms > 1000) return 2;
            ingress_delay = (uint64_t)ms * UINT64_C(1000000);
        }
        else if (!strncmp(argv[i], "--keys=", 7)) count = (size_t)strtoull(argv[i] + 7, NULL, 10);
        else { fprintf(stderr, "usage: editor_bench [--track] [--require-gl] [--partial-index] [--no-idle|--idle-only|--p4-only|--ipc-only] [--no-p4] [--keys=N] [--serial-only] [--ingress-delay-ms=N] [--file=PATH]\n"); return 2; }
    }
    const char *override = getenv("EDIT_BACKEND");
    if (override && strcmp(override, "gl") && strcmp(override, "raster")) return 2;
    if (require_gl && override && strcmp(override, "gl")) return 2;
    if (!count || count > 100000 || ingress_delay > UINT64_C(1000000000)) return 2;
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    trace_init(); (void)trace_thread_register();
    int rows = 0;
    const char *names[] = {"null", "raster", "gl"};
    if (typing) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *name = names[i];
        if (override && strcmp(name, "null") && strcmp(name, override)) continue;
        for (unsigned scenario = 0; scenario < scenarios; scenario++) {
            int rc = typing_row(name, count, track, require_gl, scenario, ingress_delay, path, partial);
            if (rc < 0) return 1;
            rows = bench_merge_exit(rows, rc);
        }
    }
    if (idle) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *name = names[i];
        if (override && strcmp(name, "null") && strcmp(name, override)) continue;
        int rc = idle_row(name, track, require_gl); if (rc < 0) return 1; rows = bench_merge_exit(rows, rc);
    }
    int f = 0, g = 0, h = 0;
    if (p4) { f = tab_row(false, track); if (f < 0) return 1; g = tab_row(true, track); if (g < 0) return 1; h = ipc_row(track); if (h < 0) return 1; }
    if (ipc_only) { h = ipc_row(track); if (h < 0) return 1; }
    return bench_merge_exit(rows, bench_merge_exit(f, bench_merge_exit(g, h)));
}
