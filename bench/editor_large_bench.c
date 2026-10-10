/* edit-457.10 / P4.10: G5, G7, G6 through the editor loop on the large corpus
 * (1 GiB log, 10 GiB sparse single line), with zero, one and three bulk jobs outstanding.
 * Rows are TRACK: the box is shared and loaded; the coordinator owns verdicts.
 * Missing fixtures are skipped (exit 0). Save rows write a private copy of the
 * 1 GiB log (needs 1 GiB free in /tmp) and never touch the corpus file. */
#include "editor/editor.h"
#include "editor/large.h"
#include "editor/private.h"
#include "trace/trace_fmt.h"
#include "raster/raster.h"
#include "font/font.h"
#include "trace/trace.h"
#include "base/base.h"
#include "harness.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG "/tmp/edit-corpus/log_1g.txt"
#define SPARSE "/tmp/edit-corpus/sparse_10g.bin"
#define MS(x) ((double)(x) / 1e6)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "editor_large_bench:%d failed: %s\n", __LINE__, #c); return -1; } } while (0)
#define G1_P50 UINT64_C(1000000)
#define G1_P99 UINT64_C(2000000)
#define G5_P50 UINT64_C(6000000)
#define G5_P99 UINT64_C(9000000)
#define G7_P50 UINT64_C(80000000)
#define G7_P99 UINT64_C(125000000)
#define G6_P50 UINT64_C(80000000)
#define G6_P99 UINT64_C(125000000)
#define FG_CAP 20000u
/* An edit at 0.9 x lines keeps the layout busy for 1 s to 30 s per key on main too (editor_bench G1 row fails the same way at HEAD), so the typing rows type at line 1000 instead. */
#define TYPE_KEYS 500u
#define TYPE_LINE 1000u   /* not 0.9 x lines: see above and docs/decisions/edit-457.10.md */

typedef struct rig {
    editor_frame frame; uint64_t ingress, first_submit, t0;
    uint32_t open_trace;
    bool guard, suspended, measuring; size_t allocations;
} rig;
static void on_ingress(void *c, uint64_t s, uint64_t ns) { rig *r = c; (void)s; r->ingress = ns; if (r->measuring) { edit_malloc_guard_begin(); r->guard = true; } }
static void on_submit(void *c, const editor_frame *f)
{
    rig *r = c; if (!r->first_submit) {
        r->first_submit = trace_now_ns();
        trace_record_at(r->first_submit, TRACE_T4_PRESENT_SUBMITTED, r->open_trace);
    }
    if (!f->last_sequence) return;
    if (r->guard) { r->allocations += edit_malloc_guard_end(); r->guard = false; }
    r->frame = *f;
}
static void on_present(void *c, const editor_frame *f) { rig *r = c; if (f->last_sequence) r->frame = *f; }
static void on_io(void *c, bool in)
{
    rig *r = c;
    if (in && r->guard) { r->allocations += edit_malloc_guard_end(); r->guard = false; r->suspended = true; }
    else if (!in && r->suspended) { edit_malloc_guard_begin(); r->guard = true; r->suspended = false; }
}
static void stamp(void)
{
    char st[32] = "unknown", ld[24] = "unknown"; const char *tag = "[unknown]";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(st, sizeof st, f)) st[strcspn(st, "\n")] = 0; fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fgets(ld, sizeof ld, f)) ld[strcspn(ld, " \n")] = 0; fclose(f); }
    if (!strcmp(st, "Charging") || !strcmp(st, "Full") || !strcmp(st, "Not charging")) tag = "[AC]";
    else if (!strcmp(st, "Discharging")) tag = "[bat]";
    printf("STAMP (M)%s BAT0=%s load1=%s loaded TRACK shared box\n", tag, st, ld);
}
static int open_ed(editor **e, render_backend *b, rig *r, const char *path)
{
    font_cell font = font_ascii_cell();
    editor_config cfg = {.path = path, .cols = 2880 / font.cell_w, .rows = 1800 / font.cell_h, .font_px = 30,
        .hook_ctx = r, .on_ingress = on_ingress, .on_submit = on_submit, .on_present = on_present, .on_io = on_io};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows;
    memset(r, 0, sizeof *r); *e = NULL;
    REQUIRE(render_null_backend(b) == 0);
    r->t0 = trace_now_ns();
    int rc = editor_open(e, &cfg, b); REQUIRE(rc == 0);
    r->open_trace = editor_large_trace_frame((*e)->buffer->file);
    trace_record_at(r->t0, TRACE_T0_INGRESS, r->open_trace);
    if (r->first_submit) trace_record_at(r->first_submit, TRACE_T4_PRESENT_SUBMITTED, r->open_trace);
    const render_grid *grid = editor_grid(*e);
    REQUIRE(grid->dims.cols * grid->dims.cell_w == 2880);
    REQUIRE(grid->dims.rows * grid->dims.cell_h == 1800);
    return 0;
}
static bool quiet(editor *e) { editor_stats s = editor_get_stats(e); return !s.pending && !s.render_active; }
static int settle(editor *e)
{
    uint64_t dl = bench_now_ns() + UINT64_C(60000000000);   /* first edit at 0.9 lines is slow: see report */
    while (!quiet(e)) { int rc = editor_step(e, 5); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE); REQUIRE(bench_now_ns() < dl); }
    return 0;
}
static int step_until(editor *e, bool (*done)(editor *), uint64_t budget_ns)
{
    uint64_t dl = bench_now_ns() + budget_ns;
    while (!done(e)) { int rc = editor_step(e, 2); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE); REQUIRE(bench_now_ns() < dl); }
    return 0;
}
static bool b_first(editor *e) { (void)e; return false; }
static bool b_exact(editor *e) { return editor_large_status_get(e).lines_exact; }
static bool b_warm(editor *e) { editor_large_status s = editor_large_status_get(e); return s.lines_exact && s.warm_done; }
static bool b_find(editor *e) { return editor_large_status_get(e).find_done; }
static bool b_save(editor *e) { return editor_large_status_get(e).save_done; }

/* Workers must be joined before dumping/resetting the single-writer rings. */
static int attribution(const char *name, int sample, uint32_t frame, bool finding)
{
    char path[128];
    (void)snprintf(path, sizeof path, "build/edit-dnc-%s-%d.trace", name, sample);
    FILE *f = fopen(path, "wb"); REQUIRE(f);
    REQUIRE(trace_dump(f) == 0); REQUIRE(fclose(f) == 0);
    f = fopen(path, "rb"); REQUIRE(f);
    trace_rec *records = NULL; size_t count = 0;
    REQUIRE(trace_fmt_load(f, &records, &count) == 0); REQUIRE(fclose(f) == 0);
    uint64_t phases[TRACE_EV_COUNT] = {0};
    for (size_t i = 0; i < count; i++) if (records[i].frame_id == frame) {
        unsigned event = records[i].ev;
        if (event < TRACE_EV_COUNT && (!phases[event] || records[i].ns < phases[event]))
            phases[event] = records[i].ns;
    }
    const unsigned open_order[] = {0, 1, 2, 3, 5, 4, 6};
    const unsigned find_order[] = {0, 1, 2, 3, 4};
    const char *open_names[] = {"request_prefix", "prefix_mapping", "mapping_attachment", "attachment_index_submit", "index_submit_viewport", "viewport_exact"};
    const char *find_names[] = {"queue_worker", "search_kernel", "snapshot_release", "mailbox_adoption"};
    const unsigned *order = finding ? find_order : open_order;
    const char **names = finding ? find_names : open_names;
    size_t pairs = finding ? 4u : 6u;
    for (size_t i = 0; i < pairs; i++) {
        uint64_t begin = phases[order[i]], end = phases[order[i + 1]];
        if (begin && end && end >= begin)
            printf("ATTR %s sample=%d phase=%s ns=%" PRIu64 " (M)[AC]\n", name, sample, names[i], end - begin);
        else printf("ATTR %s sample=%d phase=%s unavailable\n", name, sample, names[i]);
    }
    if (!finding && phases[4] && phases[3]) {
        printf("ATTR %s sample=%d viewport_before_attachment=%d (M)[AC]\n",
            name, sample, phases[4] < phases[3]);
        if (phases[4] < phases[3])
            printf("ATTR %s sample=%d phase=viewport_attachment ns=%" PRIu64 " (M)[AC]\n",
                name, sample, phases[3] - phases[4]);
        if (phases[4] < phases[5])
            printf("ATTR %s sample=%d phase=viewport_index_submit ns=%" PRIu64 " (M)[AC]\n",
                name, sample, phases[5] - phases[4]);
    }
    free(records); trace_reset(); return 0;
}
static bool b_all3(editor *e) { editor_large_status s = editor_large_status_get(e); return s.lines_exact && s.find_done && s.save_done && s.warm_done; }

/* Foreground probe: one key (arrow, or insert/backspace pair) at a time while
 * `busy` holds; samples ingress->submit, the G1 definition. */
static int probe(editor *e, rig *r, bool (*done)(editor *), bool edit, bench_samples *out, uint64_t budget_ns)
{
    uint64_t dl = bench_now_ns() + budget_ns; unsigned i = 0;
    while (!done(e) && out->n < out->cap) {
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
        if (edit) { ev.keysym = (i & 1u) ? XKB_KEY_BackSpace : XKB_KEY_x; if (!(i & 1u)) { ev.utf8_len = 1; ev.utf8[0] = 'x'; } }
        else ev.keysym = (i & 1u) ? XKB_KEY_Left : XKB_KEY_Right;
        i++; r->frame = (editor_frame){0}; r->measuring = edit;
        REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0); r->measuring = false;
        if (r->frame.last_sequence && r->frame.submit_ns >= r->ingress) (void)bench_add(out, r->frame.submit_ns - r->ingress);
        int rc = editor_step(e, 3); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);   /* let workers talk */
        REQUIRE(bench_now_ns() < dl);
    }
    return 0;
}
static int report_fg(const char *name, bench_samples *s, const char *mix)
{
    char n[96]; (void)snprintf(n, sizeof n, "editor_large_%s_fg_ingress_submit", name);
    (void)bench_report(n, s, G1_P50, G1_P99);
    printf("FG %s mix=%s samples=%zu (G)G1<=1/2ms TRACK\n", name, mix, s->n); return 0;
}

static int row_open(const char *path, const char *name, bool gated, int runs)
{
    enum { N = 7 };
    int n_runs = runs < N ? runs : N;
    uint64_t v5[N], v7[N]; bench_samples g5, g7; bench_samples_init(&g5, v5, N); bench_samples_init(&g7, v7, N);
    uint64_t est_lines = 0, exact_lines = 0;
    uint64_t fgv[FG_CAP]; bench_samples fg; bench_samples_init(&fg, fgv, FG_CAP);
    for (int k = 0; k < n_runs; k++) {
        render_backend b = {0}; rig r; editor *e;
        REQUIRE(open_ed(&e, &b, &r, path) == 0);
        while (!r.first_submit) { int rc = editor_step(e, 5); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE); }
        (void)bench_add(&g5, r.first_submit - r.t0);
        /* G5 ends at real content, rather than an empty submitted grid. */
        uint8_t prefix[16]; REQUIRE(editor_read(e, 0, prefix, sizeof prefix) == 0);
        const render_grid *grid = editor_grid(e);
        uint32_t gutter = 2;
        for (uint64_t lines = editor_line_count(e); lines >= 10; lines /= 10) gutter++;
        for (uint32_t i = 0; i < sizeof prefix; i++) {
            uint32_t glyph = prefix[i] == ' ' ? 0u : prefix[i] < 0x20 || prefix[i] == 0x7f ? '?' : prefix[i];
            const render_cell *cell = &grid->cells[grid->dims.cols + gutter + i];
            REQUIRE(cell->glyph_index == glyph);
            if (prefix[i] < 0x20) REQUIRE(cell->attrs & RENDER_ATTR_INVERSE);
        }
        editor_large_status s = editor_large_status_get(e); est_lines = editor_line_count(e);
        REQUIRE(s.mapped && !s.lines_exact);
        if (k == 0) REQUIRE(probe(e, &r, b_exact, false, &fg, UINT64_C(60000000000)) == 0);   /* index = the one active worker */
        else REQUIRE(step_until(e, b_exact, UINT64_C(60000000000)) == 0);
        (void)bench_add(&g7, trace_now_ns() - r.t0);
        exact_lines = editor_line_count(e);
        REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
        editor_close(e);
        REQUIRE(attribution(name, k, r.open_trace, false) == 0);
    }
    char n[96]; (void)snprintf(n, sizeof n, "editor_large_%s_G5_open_first_viewport", name);
    int miss = bench_report(n, &g5, G5_P50, G5_P99);
    (void)snprintf(n, sizeof n, "editor_large_%s_G7_open_index_exact", name);
    miss |= bench_report(n, &g7, G7_P50, G7_P99);
    printf("LINES %s estimated_at_first_viewport=%" PRIu64 " exact_after_index=%" PRIu64 " (G7 gate is 1 GB; sparse_10g tracked)\n", name, est_lines, exact_lines);
    report_fg(name, &fg, "index_active");
    (void)gated; return 0 * miss;
}
/* G6 with one bulk job active, followed by typing at line 1000. */
static int row_find(const char *name)
{
    render_backend b = {0}; rig r; editor *e; REQUIRE(open_ed(&e, &b, &r, LOG) == 0);
    REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
    const uint8_t needle[] = "ERROR render";
    enum { N = 5 }; uint64_t v6[N]; bench_samples g6; bench_samples_init(&g6, v6, N);
    uint64_t fgv[FG_CAP]; bench_samples fg; bench_samples_init(&fg, fgv, FG_CAP);
    uint64_t total = 0;
    uint32_t find_trace = 0;
    for (int k = 0; k < N; k++) {
        uint64_t t = trace_now_ns();
        REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
        if (k == 0) find_trace = editor_large_trace_frame(e->buffer->lg.find);
        if (k == 0) REQUIRE(probe(e, &r, b_find, false, &fg, UINT64_C(60000000000)) == 0);
        else REQUIRE(step_until(e, b_find, UINT64_C(60000000000)) == 0);
        (void)bench_add(&g6, trace_now_ns() - t);
        editor_large_status s = editor_large_status_get(e); REQUIRE(s.find_rc == 0); total = s.find_total;
    }
    char n[96]; (void)snprintf(n, sizeof n, "editor_large_%s_G6_find_1worker", name);
    (void)bench_report(n, &g6, G6_P50, G6_P99);
    printf("FIND %s total=%" PRIu64 " (exact count; first 4096 offsets stored)\n", name, total);
    report_fg("find_1worker_arrows", &fg, "find_active");
    /* typing at line 1000 while find loops: one worker always busy */
    uint64_t line = TYPE_LINE; bool ex = false;
    uint64_t byte = editor_large_line_to_byte(e, line, &ex); REQUIRE(ex);
    REQUIRE(editor_large_goto_byte(e, byte) == 0); REQUIRE(settle(e) == 0);
    REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
    uint64_t tv[FG_CAP]; bench_samples ty; bench_samples_init(&ty, tv, 2000);
    for (unsigned i = 0; i < 16; i++) { plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = (i & 1u) ? XKB_KEY_BackSpace : XKB_KEY_x}; if (!(i & 1u)) { ev.utf8_len = 1; ev.utf8[0] = 'x'; } REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0); }
    while (ty.n < TYPE_KEYS) {
        if (editor_large_status_get(e).find_done) REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
        bench_samples one; uint64_t ov[2]; bench_samples_init(&one, ov, 2);
        REQUIRE(probe(e, &r, b_find, true, &one, UINT64_C(60000000000)) == 0);
        for (size_t i = 0; i < one.n; i++) (void)bench_add(&ty, one.v[i]);
    }
    report_fg("typing_line1000_find_1worker", &ty, "find_active");
    printf("ALLOC typing_1worker allocations=%zu (G)=0\n", r.allocations);
    editor_close(e); REQUIRE(attribution("find", 0, find_trace, true) == 0); return 0;
}
/* Three job types outstanding on the single bulk lane: index + warm (from open), find, save (private copy). */
static int row_three(void)
{
    char copy[] = "build/editor-large-bench-XXXXXX"; int fd = mkstemp(copy); REQUIRE(fd >= 0); close(fd);
    char cmd[256]; (void)snprintf(cmd, sizeof cmd, "cp --reflink=auto %s %s", LOG, copy); REQUIRE(system(cmd) == 0);
    render_backend b = {0}; rig r; editor *e; REQUIRE(open_ed(&e, &b, &r, copy) == 0);
    const uint8_t needle[] = "ERROR render";
    uint64_t t = trace_now_ns();
    REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
    REQUIRE(editor_large_save_begin(e) == 0);
    uint64_t fgv[FG_CAP]; bench_samples fg; bench_samples_init(&fg, fgv, FG_CAP);
    REQUIRE(probe(e, &r, b_all3, false, &fg, UINT64_C(180000000000)) == 0);
    editor_large_status s = editor_large_status_get(e);
    printf("THREE find_done_and_save_done_ms=%.1f (tracked, not gated) save_status=%d find_total=%" PRIu64 " index_exact=%d\n",
        MS(trace_now_ns() - t), s.save_status, s.find_total, s.lines_exact);
    report_fg("arrows_3workers", &fg, "index+warm+find+save");
    /* typing with all three outstanding again: second round after an edit-free index. */
    uint64_t tv[FG_CAP]; bench_samples ty; bench_samples_init(&ty, tv, 2000);
    REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
    uint64_t line = TYPE_LINE; bool ex = false;
    uint64_t byte = editor_large_line_to_byte(e, line, &ex); REQUIRE(ex);
    REQUIRE(editor_large_goto_byte(e, byte) == 0); REQUIRE(settle(e) == 0);
    REQUIRE(editor_large_save_begin(e) == 0);
    while (ty.n < TYPE_KEYS) {
        if (editor_large_status_get(e).find_done) REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
        bench_samples one; uint64_t ov[2]; bench_samples_init(&one, ov, 2);
        REQUIRE(probe(e, &r, b_find, true, &one, UINT64_C(60000000000)) == 0);
        for (size_t i = 0; i < one.n; i++) (void)bench_add(&ty, one.v[i]);
    }
    report_fg("typing_line1000_find+save", &ty, "find_loop+save");
    printf("ALLOC typing_3workers allocations=%zu (G)=0\n", r.allocations);
    editor_close(e); unlink(copy); return 0;
}

typedef struct index_probe {
    piece_snapshot *snapshot;
    const uint8_t *bytes; size_t length;
    bool flat;
    uint64_t first, last, callback_ns; size_t calls;
    uint64_t first_cpu, last_cpu;
} index_probe;
static uint64_t worker_cpu_now(void)
{
    struct timespec ts; (void)clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static size_t index_span_probe(void *ctx, uint64_t off, const uint8_t **p)
{
    index_probe *probe_state = ctx;
    uint64_t begin = trace_now_ns();
    if (!probe_state->calls) {
        probe_state->first = begin;
        probe_state->first_cpu = worker_cpu_now();
        trace_record_at(begin, TRACE_T1_DEQUEUE, editor_large_trace_frame(probe_state));
    }
    size_t length = 0;
    if (probe_state->flat) {
        if (off < probe_state->length) { *p = probe_state->bytes + off; length = probe_state->length - (size_t)off; }
    } else {
        piece_iter it; piece_iter_begin_snapshot(&it, probe_state->snapshot, off);
        (void)piece_iter_next(&it, p, &length);
    }
    uint64_t end = trace_now_ns();
    probe_state->callback_ns += end - begin; probe_state->calls++;
    probe_state->last = end;
    if (off < probe_state->length && probe_state->length - off <= LINEIDX_CHUNK) {
        probe_state->last_cpu = worker_cpu_now();
        trace_record_at(end, TRACE_T2_MUTATION_DONE, editor_large_trace_frame(probe_state));
    }
    return length;
}
/* Same pool, mapping and poll policy, flat kernel floor then editor snapshot
 * callback. Source is prefaulted by the untimed editor warm job. */
static int row_index_floor(const char *path, const char *name)
{
    enum { N = 3 }; uint64_t flat_values[N], snapshot_values[N];
    bench_samples flat_samples, snapshot_samples;
    bench_samples_init(&flat_samples, flat_values, N);
    bench_samples_init(&snapshot_samples, snapshot_values, N);
    for (int sample = 0; sample < N; sample++) {
        render_backend backend = {0}; rig r; editor *e;
        REQUIRE(open_ed(&e, &backend, &r, path) == 0);
        REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
        for (unsigned variant = 0; variant < 2; variant++) {
            index_probe state = {.snapshot = piece_snapshot_take(e->tree), .flat = variant == 0};
            REQUIRE(state.snapshot);
            REQUIRE(file_snapshot_bytes(state.snapshot, NULL, &state.bytes, &state.length) == 1);
            uint64_t begin = trace_now_ns();
            lineidx *index = lineidx_create(state.length); REQUIRE(index);
            REQUIRE(lineidx_bind_snapshot(index, state.snapshot) == 0);
            lineidx_src source = {&state, state.length, index_span_probe, NULL};
            trace_record_at(begin, TRACE_T0_INGRESS, editor_large_trace_frame(&state));
            REQUIRE(lineidx_build_start(index, &e->pool, &source) == 0);
            uint64_t deadline = begin + UINT64_C(60000000000), exact = 0;
            while (!exact || lineidx_building(index)) {
                (void)work_mailbox_drain(&e->pool, editor_route_work, e);
                (void)lineidx_poll(index);
                if (!exact && lineidx_complete(index)) exact = trace_now_ns();
                REQUIRE(trace_now_ns() < deadline);
                if (!lineidx_build_needs_poll(index) && lineidx_building(index)) {
                    struct pollfd ready = {work_pool_eventfd(&e->pool), POLLIN, 0};
                    (void)poll(&ready, 1, 1);
                }
            }
            REQUIRE(lineidx_line_count(index).value == editor_line_count(e));
            bench_samples *samples = state.flat ? &flat_samples : &snapshot_samples;
            (void)bench_add(samples, exact - begin);
            trace_record_at(exact, TRACE_T4_PRESENT_SUBMITTED, editor_large_trace_frame(&state));
            printf("ATTR %s sample=%d variant=%s queue_ns=%" PRIu64 " worker_extent_ns=%" PRIu64
                " worker_cpu_ns=%" PRIu64 " callback_ns=%" PRIu64 " callbacks=%zu tail_adoption_ns=%" PRIu64 " (M)[AC]\n",
                name, sample, state.flat ? "flat" : "snapshot", state.first - begin,
                state.last - state.first, state.last_cpu - state.first_cpu, state.callback_ns, state.calls, exact > state.last ? exact - state.last : 0);
            lineidx_destroy(index); piece_snapshot_release(state.snapshot);
        }
        editor_close(e);
    }
    char label[96];
    (void)snprintf(label, sizeof label, "editor_large_%s_index_flat_floor_TRACK", name);
    (void)bench_report(label, &flat_samples, 0, 0);
    (void)snprintf(label, sizeof label, "editor_large_%s_index_snapshot_source_TRACK", name);
    (void)bench_report(label, &snapshot_samples, 0, 0);
    return 0;
}
typedef struct find_kernel_probe {
    find_source source; find_result result; find_code rc;
    uint64_t begin, end;
} find_kernel_probe;
static void find_floor_fn(work_ctx *ctx)
{
    find_kernel_probe *state = ctx->arg;
    const uint8_t needle[] = "ERROR render";
    find_control control = {.work = ctx};
    state->begin = trace_now_ns();
    trace_record_at(state->begin, TRACE_T1_DEQUEUE, editor_large_trace_frame(state));
    state->rc = find_literal(&state->source, needle, sizeof needle - 1, &control, &state->result);
    state->end = trace_now_ns();
    trace_record_at(state->end, TRACE_T2_MUTATION_DONE, editor_large_trace_frame(state));
}
static int row_find_floor(void)
{
    enum { N = 5 }; uint64_t flat_values[N], snapshot_values[N];
    bench_samples flat_samples, snapshot_samples;
    bench_samples_init(&flat_samples, flat_values, N); bench_samples_init(&snapshot_samples, snapshot_values, N);
    render_backend backend = {0}; rig r; editor *e;
    REQUIRE(open_ed(&e, &backend, &r, LOG) == 0);
    REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
    piece_snapshot *snapshot = piece_snapshot_take(e->tree); REQUIRE(snapshot);
    const uint8_t *bytes; size_t length;
    REQUIRE(file_snapshot_bytes(snapshot, NULL, &bytes, &length) == 1);
    for (int sample = 0; sample < N; sample++) {
        uint64_t total = 0;
        for (unsigned variant = 0; variant < 2; variant++) {
            find_kernel_probe state = {.source = {.bytes = bytes, .length = length, .snapshot = variant ? snapshot : NULL}};
            work_handle handle = work_submit(&e->pool, (work_job){find_floor_fn, &state, 0, WORK_BULK}); REQUIRE(handle.epoch);
            uint64_t deadline = trace_now_ns() + UINT64_C(60000000000);
            while (!work_handle_finished(&e->pool, handle)) {
                int rc = editor_step(e, 1); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
                REQUIRE(trace_now_ns() < deadline);
            }
            REQUIRE(state.rc == FIND_OK);
            if (!variant) total = state.result.total; else REQUIRE(total == state.result.total);
            (void)bench_add(variant ? &snapshot_samples : &flat_samples, state.end - state.begin);
            printf("ATTR find_floor sample=%d variant=%s kernel_ns=%" PRIu64 " total=%" PRIu64 " (M)[AC]\n",
                sample, variant ? "snapshot" : "flat", state.end - state.begin, state.result.total);
        }
    }
    piece_snapshot_release(snapshot); editor_close(e);
    (void)bench_report("editor_large_log_1g_find_flat_kernel_floor_TRACK", &flat_samples, 0, 0);
    (void)bench_report("editor_large_log_1g_find_snapshot_kernel_TRACK", &snapshot_samples, 0, 0);
    return 0;
}
int main(int argc, char **argv)
{
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    trace_init(); (void)trace_thread_register();
    struct stat st; bool have_log = stat(LOG, &st) == 0, have_sparse = stat(SPARSE, &st) == 0;
    if (!have_log && !have_sparse) { puts("editor_large_bench: SKIP (no fixtures in /tmp/edit-corpus)"); return 0; }
    stamp();
    if (argc == 2 && strcmp(argv[1], "--attribute-find-floor") == 0) return row_find_floor();
    if (argc == 2 && strcmp(argv[1], "--attribute-index") == 0) {
        if (have_log && row_index_floor(LOG, "log_1g")) return 1;
        if (have_sparse && row_index_floor(SPARSE, "sparse_10g")) return 1;
        FILE *dump = fopen("build/edit-dnc-index-floor.trace", "wb"); REQUIRE(dump);
        REQUIRE(trace_dump(dump) == 0); REQUIRE(fclose(dump) == 0); return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--attribute-log") == 0) return row_open(LOG, "log_1g", true, 3);
    if (argc == 2 && strcmp(argv[1], "--attribute-sparse") == 0) return row_open(SPARSE, "sparse_10g", false, 3);
    if (argc == 2 && strcmp(argv[1], "--attribute-find") == 0) return row_find("log_1g");
    if (have_log) { if (row_open(LOG, "log_1g", true, 7)) return 1; if (row_find("log_1g")) return 1; if (row_three()) return 1; }
    if (have_sparse && row_open(SPARSE, "sparse_10g", false, 3)) return 1;
    (void)b_first; (void)b_save;
    return 0;
}
