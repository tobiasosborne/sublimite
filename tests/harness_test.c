/* tests/harness_test.c - checks for bench/harness.h (P0.4). */
#define main editor_bench_entry
#include "../bench/editor_bench.c"
#undef main

#include <stdlib.h>
#include "../bench/interaction.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void fill(bench_samples *s, uint64_t *buf, size_t cap, const uint64_t *vals, size_t n)
{
    size_t i;
    bench_samples_init(s, buf, cap);
    for (i = 0; i < n; i++)
        CHECK(bench_add(s, vals[i]) == 0);
}

static void test_percentiles_small(void)
{
    uint64_t buf[16];
    bench_samples s;
    uint64_t one[] = {42};
    uint64_t two[] = {7, 3};
    uint64_t eq[] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    uint64_t hundred[100];
    size_t i;

    fill(&s, buf, 16, one, 1);
    CHECK(bench_p50(&s) == 42);
    CHECK(bench_p99(&s) == 42);

    fill(&s, buf, 16, two, 2);
    CHECK(bench_p50(&s) == 3);   /* ceil(0.5*2)=1 -> smallest */
    CHECK(bench_p99(&s) == 7);   /* ceil(0.99*2)=2 -> largest */
    CHECK(bench_p(&s, 0.0) == 3);
    CHECK(bench_p(&s, 1.0) == 7);

    fill(&s, buf, 16, eq, 10);
    CHECK(bench_p50(&s) == 5);
    CHECK(bench_p99(&s) == 5);

    for (i = 0; i < 100; i++)
        hundred[i] = 100 - i;     /* descending, so sorting is exercised */
    bench_samples_init(&s, hundred, 100);
    s.n = 100;
    CHECK(bench_p50(&s) == 50);  /* rank 50 of 1..100 */
    CHECK(bench_p99(&s) == 99);  /* rank 99 */
    CHECK(bench_p(&s, 0.01) == 1);
}

/* §4 counterexample: 60 % at 20 ms, 40 % at 40 ms. Mean-based or
 * interpolated estimates would mislead; nearest-rank gives p50=20, p99=40. */
static void test_counterexample(void)
{
    uint64_t buf[10];
    bench_samples s;
    uint64_t vals[10];
    size_t i;
    for (i = 0; i < 10; i++)
        vals[i] = (i < 6) ? 20000000u : 40000000u;
    fill(&s, buf, 10, vals, 10);
    CHECK(bench_p50(&s) == 20000000u);
    CHECK(bench_p99(&s) == 40000000u);
}

static void test_empty_and_overflow(void)
{
    uint64_t buf[2];
    bench_samples s;
    bench_samples_init(&s, buf, 2);
    CHECK(bench_p50(&s) == 0);
    CHECK(bench_add(&s, 1) == 0);
    CHECK(bench_add(&s, 2) == 0);
    CHECK(bench_add(&s, 3) == -1);
    CHECK(s.n == 2);
    CHECK(s.dropped == 1);
}

/* Deterministic quasi-uniform sample on [0, 1e6): i*K mod M. The true
 * q-quantile is q*1e6; the 95 % CI must contain it. */
static void test_ci_uniform(void)
{
    enum { N = 10000 };
    static uint64_t buf[N];
    bench_samples s;
    const double qs[] = {0.01, 0.10, 0.50, 0.90, 0.99};
    size_t qi, i;
    bench_samples_init(&s, buf, N);
    for (i = 0; i < N; i++) {
        uint64_t v = ((uint64_t)i * 2654435761u) % 1000000u;
        CHECK(bench_add(&s, v) == 0);
    }
    for (qi = 0; qi < sizeof qs / sizeof qs[0]; qi++) {
        uint64_t lo = 0, hi = 0, p;
        double truth = qs[qi] * 1000000.0;
        bench_ci95(&s, qs[qi], &lo, &hi);
        p = bench_p(&s, qs[qi]);
        CHECK(lo <= hi);
        CHECK((double)lo <= truth && truth <= (double)hi);
        CHECK(lo <= p && p <= hi);
    }
    /* Tiny sets: CI must be well formed and bracket the sample. */
    {
        uint64_t tb[2], lo = 9, hi = 9;
        uint64_t v2[] = {4, 8};
        bench_samples t;
        fill(&t, tb, 2, v2, 2);
        bench_ci95(&t, 0.5, &lo, &hi);
        CHECK(lo >= 4 && hi <= 8 && lo <= hi);
    }
    {
        uint64_t tb[1], lo = 9, hi = 9;
        uint64_t v1[] = {42};
        bench_samples t;
        fill(&t, tb, 1, v1, 1);
        bench_ci95(&t, 0.99, &lo, &hi);
        CHECK(lo == 42 && hi == 42);
    }
}

static void test_report_and_gate(void)
{
    uint64_t buf[10];
    bench_samples s;
    uint64_t vals[10];
    char line[512];
    FILE *fp = tmpfile();
    size_t i;
    int rc;
    long len;

    CHECK(fp != NULL);
    if (fp == NULL)
        return;
    for (i = 0; i < 10; i++)
        vals[i] = (i < 6) ? 20000000u : 40000000u;
    fill(&s, buf, 10, vals, 10);

    /* gate met: p50=20ms<=25ms, p99=40ms<=50ms -> pass, rc 0 */
    rc = bench_report_fp(fp, "ctr_pass", &s, 25000000u, 50000000u);
    CHECK(rc == 0);
    /* gate missed on p99: 40ms > 30ms -> rc 1 */
    rc = bench_report_fp(fp, "ctr_miss", &s, 25000000u, 30000000u);
    CHECK(rc == 1);
    /* ungated (0 = not gated) -> rc 0 */
    rc = bench_report_fp(fp, "ctr_ungated", &s, 0, 0);
    CHECK(rc == 0);
    /* gate missed on p50: 20ms > 10ms -> rc 1 */
    rc = bench_report_fp(fp, "ctr_miss50", &s, 10000000u, 0);
    CHECK(rc == 1);

    /* dropped samples are a miss even when the percentiles pass */
    {
        uint64_t tb[2];
        bench_samples t;
        bench_samples_init(&t, tb, 2);
        (void)bench_add(&t, 1);
        (void)bench_add(&t, 1);
        (void)bench_add(&t, 1);
        rc = bench_report_fp(fp, "dropped", &t, 0, 0);
        CHECK(rc == 1);
    }

    rewind(fp);
    {
        int lines = 0;
        int found = 0;
        while (fgets(line, sizeof line, fp) != NULL) {
            lines++;
            if (strstr(line, "name=ctr_pass ") != NULL) {
                found = 1;
                CHECK(strstr(line, "BENCH name=ctr_pass n=10 p50=20000000 p99=40000000 ") != NULL);
                CHECK(strstr(line, "ci95=[") != NULL);
                CHECK(strstr(line, "gate_p50=25000000 gate_p99=50000000 pass=1 power=") != NULL);
                CHECK(strstr(line, "power=[bat]") != NULL || strstr(line, "power=[AC]") != NULL ||
                      strstr(line, "power=[unknown]") != NULL);
            }
        }
        CHECK(lines == 5);
        CHECK(found);
    }
    len = ftell(fp);
    CHECK(len > 0);
    fclose(fp);
}

static void test_battery(void)
{
    char st[32];
    const char *tag;
    bench_battery_status(st, sizeof st);
    CHECK(strcmp(st, "Discharging") == 0 || strcmp(st, "Charging") == 0 ||
          strcmp(st, "Full") == 0 || strcmp(st, "Not charging") == 0 ||
          strcmp(st, "unknown") == 0);
    tag = bench_evidence_tag();
    CHECK(strcmp(tag, "[bat]") == 0 || strcmp(tag, "[AC]") == 0 || strcmp(tag, "[unknown]") == 0);
    CHECK((strcmp(st, "Discharging") == 0) == (strcmp(tag, "[bat]") == 0));
}

static void test_power_status(void)
{
    CHECK(strcmp(bench__power_from_status("Discharging"), "Discharging") == 0);
    CHECK(strcmp(bench__power_from_status("Charging"), "Charging") == 0);
    CHECK(strcmp(bench__power_from_status("Not charging"), "Not charging") == 0);
    CHECK(strcmp(bench__power_from_status("Full"), "Full") == 0);
    CHECK(strcmp(bench__power_from_status("Unknown"), "unknown") == 0);
    CHECK(strcmp(bench__power_from_status(""), "unknown") == 0);
    CHECK(strcmp(bench__power_from_status("\n"), "unknown") == 0);
    CHECK(strcmp(bench__power_from_status(NULL), "unknown") == 0);
    CHECK(strcmp(bench__power_from_status("Discharging\n"), "Discharging") == 0);
    CHECK(strcmp(bench__power_from_status("Not charging\n"), "Not charging") == 0);
    CHECK(strcmp(bench__power_from_status("Full \r\n"), "Full") == 0);
    CHECK(strcmp(bench__power_from_status("Not charging \t\n"), "Not charging") == 0);
    CHECK(strcmp(bench__power_from_status("Not  charging"), "unknown") == 0);
    CHECK(strcmp(bench__tag_from_power("Discharging"), "[bat]") == 0);
    CHECK(strcmp(bench__tag_from_power("Charging"), "[AC]") == 0);
    CHECK(strcmp(bench__tag_from_power("Not charging"), "[AC]") == 0);
    CHECK(strcmp(bench__tag_from_power("Full"), "[AC]") == 0);
    CHECK(strcmp(bench__tag_from_power("unknown"), "[unknown]") == 0);
}

static void test_time_macro(void)
{
    uint64_t buf[4];
    bench_samples s;
    volatile int x = 0;
    bench_samples_init(&s, buf, 4);
    BENCH_TIME(&s, x = x + 1);
    BENCH_TIME(&s, x = x + 1, x = x + 1);
    CHECK(s.n == 2);
    CHECK(x == 3);
    bench_evict_caches_hint();
}

static void test_gate_verdict(void)
{
    uint64_t buf[64];
    bench_samples s;
    uint64_t v[5] = {1, 2, 3, 4, 5};
    size_t i;
    /* too few samples: refused, never PASS, even when every value is far under the gate */
    fill(&s, buf, 64, v, 5);
    CHECK(bench_judge(&s, 100, 100, 10) == BENCH_REFUSED);
    CHECK(bench_exit_code(BENCH_REFUSED, 0) != 0);
    CHECK(bench_exit_code(BENCH_REFUSED, 1) == 0);
    CHECK(bench_judge(&s, 100, 100, 5) == BENCH_PASS);
    CHECK(bench_exit_code(BENCH_PASS, 0) == 0);
    /* a miss exits non-zero by default; --track opts out */
    CHECK(bench_judge(&s, 1, 1, 5) == BENCH_MISS);
    CHECK(bench_exit_code(BENCH_MISS, 0) != 0);
    CHECK(bench_exit_code(BENCH_MISS, 1) == 0);
    /* a miss is a miss even with too few samples (refusal does not hide it) */
    CHECK(bench_judge(&s, 1, 1, 10) == BENCH_MISS);
    /* dropped samples are a miss */
    bench_samples_init(&s, buf, 2);
    for (i = 0; i < 3; i++)
        (void)bench_add(&s, 1);
    CHECK(bench_judge(&s, 100, 100, 1) == BENCH_MISS);
    /* unknown power never claims battery */
    CHECK(strcmp(bench__tag_from_power(bench__power_from_status("Unknown\n")), "[unknown]") == 0);
    {
        char line[256];
        fill(&s, buf, 64, v, 5);
        CHECK(bench_gate_line(line, sizeof line, "x", &s, 100, 100, 10, 0, "Unknown", "0.1") > 0);
        CHECK(strstr(line, "REFUSED") != NULL);
        CHECK(strstr(line, "[unknown]") != NULL);
        CHECK(strstr(line, "[bat]") == NULL);
        CHECK(strstr(line, "required_n=10") != NULL);
        CHECK(strstr(line, "ci95_p50=") != NULL && strstr(line, "ci95_p99=") != NULL);
    }
}

static void test_interaction_population(void)
{
    size_t n = 17;
    CHECK(bench_interaction_samples("10000", &n) == 0 && n == BENCH_INTERACTION_MIN_N);
    CHECK(bench_interaction_samples("20000", &n) == 0 && n == 20000);
    const char *invalid[] = {"0", "3", "64", "9999", "-10000", "+10000", "10000x", "", "999999999999999999999999999999"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        n = 17;
        CHECK(bench_interaction_samples(invalid[i], &n) != 0 && n == 17);
    }
    uint64_t values[BENCH_INTERACTION_MIN_N];
    bench_samples population;
    bench_samples_init(&population, values, BENCH_INTERACTION_MIN_N);
    for (size_t i = 1; i < BENCH_INTERACTION_MIN_N; i++) CHECK(bench_add(&population, 1) == 0);
    CHECK(bench_judge(&population, 100, 100, BENCH_INTERACTION_MIN_N) == BENCH_REFUSED);
    CHECK(bench_add(&population, 1) == 0);
    CHECK(bench_judge(&population, 100, 100, BENCH_INTERACTION_MIN_N) == BENCH_PASS);
}

static void test_editor_bench_honesty(void)
{
    stamp tag = power_stamp();
    render_backend b = {0};
    samples measured = {0};
    editor_config cfg = target_a_config();
    cfg.hook_ctx = &measured; cfg.on_ingress = ingress;
    cfg.on_submit = submitted; cfg.on_present = presented;
    editor *e = NULL;
    CHECK(render_null_backend(&b) == 0);
    CHECK(editor_open(&e, &cfg, &b) == 0);
    if (!e) return;
    CHECK(settle(e) == 0);
    uint32_t width = b.config.dims.cols * b.config.dims.cell_w;
    uint32_t height = b.config.dims.rows * b.config.dims.cell_h;
    printf("PROBE A runtime=%ux%u requested=2880x1800 (G)\n", width, height);
    CHECK(width == 2880 && height == 1800);
    plat_event ev = event(false);
    key_sample keys[3] = {{0}};
    measured.keys = keys; measured.count = 3; measured.first_sequence = 1;
    uint64_t injection = bench_now_ns(); keys[0].injected = injection;
    CHECK(editor_inject(e, &ev) == 0);
    struct timespec delay = {.tv_nsec = 20000000};
    CHECK(nanosleep(&delay, NULL) == 0);
    CHECK(settle(e) == 0);
    uint64_t reported = keys[0].t4 - keys[0].injected;
    uint64_t end_to_end = measured.frame.present_ns - injection;
    printf("PROBE G1 injected_wait=20000000 (G) reported=%" PRIu64 " injection_to_T4=%" PRIu64 " (M)%s\n",
           reported, end_to_end, tag.power);
    CHECK(reported >= UINT64_C(20000000));
    measured.frame = (editor_frame){0};
    keys[1].injected = bench_now_ns(); CHECK(editor_inject(e, &ev) == 0);
    keys[2].injected = bench_now_ns(); CHECK(editor_inject(e, &ev) == 0);
    CHECK(settle(e) == 0);
    printf("PROBE queued_keys first=%" PRIu64 " last=%" PRIu64 " dequeue_sequence=%" PRIu64 " (M)%s\n",
           measured.frame.first_sequence, measured.frame.last_sequence, measured.sequence, tag.power);
    CHECK(measured.completed == 3 && !measured.invalid);
    CHECK(keys[1].frame == keys[2].frame && keys[1].frame != keys[0].frame);
    CHECK(keys[1].t4 >= keys[1].injected && keys[2].t4 >= keys[2].injected);
    editor_close(e);
    int structural_rc = row_result(0, true, true);
    printf("PROBE structural_failure TRACK exit=%d expected=1 (G)\n", structural_rc);
    CHECK(structural_rc == 1);
    uint64_t value = 1; bench_samples tiny;
    bench_samples_init(&tiny, &value, 1); CHECK(bench_add(&tiny, 1) == 0);
    int refused = gate_row("editor_probe_insufficient", &tiny, 100, 100, false, &tag);
    printf("PROBE insufficient_samples exit=%d expected=3 (G)\n", refused);
    CHECK(refused == 3);
    CHECK(bench_merge_exit(0, 3) == 3);
    CHECK(bench_merge_exit(3, 0) == 3);
    CHECK(bench_merge_exit(3, 3) == 3);
    CHECK(bench_merge_exit(3, 1) == 1);
    CHECK(bench_merge_exit(1, 3) == 1);
}

typedef struct bench_delayed { bool ready; } bench_delayed;
static int bench_delayed_init(render_backend *b, const render_config *cfg)
{ (void)cfg; ((bench_delayed *)b->state)->ready = true; return 0; }
static int bench_delayed_resize(render_backend *b, render_dims dims)
{ (void)b; (void)dims; return 0; }
static int bench_delayed_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t n)
{ (void)b; (void)g; (void)strips; (void)n; return 0; }
static int bench_delayed_present(render_backend *b, uint32_t id)
{
    if (!((bench_delayed *)b->state)->ready) return RENDER_ERR_BUSY;
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, id, 0);
    return rc ? rc : render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, id, 0);
}
static int bench_delayed_event(render_backend *b, const render_event *ev)
{ (void)b; (void)ev; return RENDER_ERR_UNSUPPORTED; }
static void bench_delayed_close(render_backend *b) { (void)b; }
static void test_active_frame_samples(void)
{
    render_backend b = {.info = {"bench delayed probe", sizeof(bench_delayed), 16, RENDER_CAP_HEADLESS},
        .ops = {bench_delayed_init, bench_delayed_resize, bench_delayed_submit,
                bench_delayed_present, bench_delayed_event, bench_delayed_close}};
    samples measured = {0};
    editor_config cfg = {.cols = 32, .rows = 8, .hook_ctx = &measured,
        .on_ingress = ingress, .on_submit = submitted, .on_present = presented};
    editor *e = NULL; CHECK(editor_open(&e, &cfg, &b) == 0);
    if (!e) return;
    CHECK(settle(e) == 0);
    key_sample keys[4] = {{0}};
    measured.keys = keys; measured.count = 4; measured.first_sequence = 1;
    ((bench_delayed *)b.state)->ready = false;
    plat_event ev = event(false);
    keys[0].injected = bench_now_ns(); CHECK(editor_inject(e, &ev) == 0);
    for (unsigned i = 0; i < 100 && !b.active; i++) CHECK(editor_step(e, 0) >= 0);
    CHECK(b.active && keys[0].submit && !keys[0].t4);
    for (size_t i = 1; i < 4; i++) {
        keys[i].injected = bench_now_ns(); CHECK(editor_inject(e, &ev) == 0);
    }
    for (unsigned i = 0; i < 8; i++) CHECK(editor_step(e, 0) >= 0);
    CHECK(measured.completed == 0);
    struct timespec delay = {.tv_nsec = 20000000}; CHECK(nanosleep(&delay, NULL) == 0);
    ((bench_delayed *)b.state)->ready = true; CHECK(settle(e) == 0);
    CHECK(!measured.invalid && measured.completed == 4);
    for (size_t i = 0; i < 4; i++) CHECK(keys[i].t4 - keys[i].injected >= UINT64_C(20000000));
    CHECK(keys[0].frame != keys[1].frame && keys[1].frame == keys[2].frame && keys[2].frame == keys[3].frame);
    printf("PROBE active_frame keys=4 completed=%zu coalesced=3 (M)%s\n", measured.completed, bench_evidence_tag());
    /* A duplicate callback is structural, including under TRACK. */
    presented(&measured, &measured.frame);
    CHECK(measured.invalid && row_result(0, measured.invalid, true) == 1);
    editor_close(e);
}
static void test_structural_counts(void)
{
    samples measured = {.completed = 2};
    editor_stats before = {.mutations = 16, .journal_records = 16};
    editor_stats after = {.mutations = 18, .journal_records = 18};
    CHECK(!typing_structure(&measured, before, after, 2));
    after.mutations--;
    CHECK(row_result(0, typing_structure(&measured, before, after, 2), true) == 1);
    after.mutations++; after.journal_records--;
    CHECK(row_result(0, typing_structure(&measured, before, after, 2), true) == 1);
    after.journal_records++; after.journal_error = 1;
    CHECK(row_result(0, typing_structure(&measured, before, after, 2), true) == 1);
    after.journal_error = 0; measured.allocations = 1;
    CHECK(row_result(0, typing_structure(&measured, before, after, 2), true) == 1);
    measured.allocations = 0; measured.completed = 1;
    CHECK(row_result(0, typing_structure(&measured, before, after, 2), true) == 1);
    CHECK(row_result(1, false, true) == 0);
    CHECK(row_result(1, false, false) == 1);
    CHECK(row_result(3, false, false) == 3);
    puts("PROBE structural counts, allocation, missing/duplicate frame failures remain fatal in TRACK");
}

/* Exercise the real row schedules, rather than a duplicate of their constants.
 * Timing misses are allowed on the shared box; population loss is not. */
static void test_editor_row_populations(void)
{
    int output[2];
    int pipe_rc = pipe(output);
    CHECK(pipe_rc == 0);
    if (pipe_rc != 0) return;
    fflush(stdout);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child < 0) { close(output[0]); close(output[1]); return; }
    if (child == 0) {
        close(output[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0) _exit(2);
        close(output[1]);
        int tabs_rc = tab_row(false, false);
        int idle_rc = idle_row("null", false, false);
        fflush(stdout);
        _exit(tabs_rc < 0 || idle_rc < 0 ? 2 : 0);
    }
    close(output[1]);
    FILE *rows = fdopen(output[0], "r");
    CHECK(rows != NULL);
    size_t frames = 0, maps = 0, blinks = 0;
    if (rows) {
        char line[1024], name[128]; size_t n;
        while (fgets(line, sizeof line, rows)) {
            fputs(line, stdout);
            if (sscanf(line, "BENCH name=%127s n=%zu", name, &n) != 2) continue;
            if (!strcmp(name, "editor_null_100tabs_ingress_T5_G3")) frames = n;
            if (!strcmp(name, "editor_null_100tabs_minimap_inside_frame")) maps = n;
            if (!strncmp(name, "editor_null_G11_process_cpu", strlen("editor_null_G11_process_cpu")) && n > blinks)
                blinks = n;
        }
        fclose(rows);
    } else close(output[0]);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(frames >= BENCH_INTERACTION_MIN_N);
    CHECK(maps >= BENCH_INTERACTION_MIN_N);
    CHECK(blinks >= BENCH_INTERACTION_MIN_N);
}

int main(void)
{
    trace_init(); (void)trace_thread_register();
    test_editor_bench_honesty();
    test_active_frame_samples();
    test_structural_counts();
    test_gate_verdict();
    test_interaction_population();
    test_percentiles_small();
    test_counterexample();
    test_empty_and_overflow();
    test_ci_uniform();
    test_report_and_gate();
    test_battery();
    test_power_status();
    test_time_macro();
    test_editor_row_populations();
    printf("harness_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
