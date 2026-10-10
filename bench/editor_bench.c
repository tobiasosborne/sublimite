#include "editor/editor.h"
#include "raster/raster.h"
#include "font/font.h"
#include "trace/trace.h"
#include "base/base.h"
#include "harness.h"
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
    printf("STAMP (M)%s BAT0=%s load1=%s TRACK shared box\n", s.power, s.status, s.load);
    return s;
}
typedef struct samples {
    editor_frame frame;
    uint64_t ingress, sequence;
    bool guard, suspended, measuring;
    size_t allocations;
} samples;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    samples *s = ctx; s->ingress = ns; s->sequence = sequence;
    if (s->measuring) { edit_malloc_guard_begin(); s->guard = true; }
}
static void submitted(void *ctx, const editor_frame *f)
{
    samples *s = ctx;
    if (!f->last_sequence) return;
    if (s->guard) { s->allocations += edit_malloc_guard_end(); s->guard = false; }
    s->frame = *f;
}
static void presented(void *ctx, const editor_frame *f)
{
    samples *s = ctx; if (f->last_sequence) s->frame = *f;
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
static int settle(editor *e)
{
    uint64_t deadline = bench_now_ns() + UINT64_C(5000000000);
    for (;;) {
        editor_stats before = editor_get_stats(e);
        if (!before.pending && !before.render_active) return 0;
        int rc = editor_step(e, 20); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
        REQUIRE(bench_now_ns() < deadline);
    }
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
static int idle_row(const char *requested, bool track, bool require_gl)
{
    stamp tag = power_stamp();
    render_backend b = {0}; REQUIRE(row_backend(&b, requested) == 0);
    /* G11 uses the same A viewport as G1. Process CPU time includes every
     * worker and is an upper bound on summed per-thread running time. */
    font_cell font = font_ascii_cell();
    editor_config cfg = {.cols = 2880 / font.cell_w, .rows = 1800 / font.cell_h};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows; cfg.raster_fallback = true;
    editor *e = NULL; REQUIRE(editor_open(&e, &cfg, &b) == 0);
    REQUIRE(!require_gl || strcmp(requested, "gl") || (b.info.capabilities & RENDER_CAP_GPU));
    const char *label = row_label(&b, requested);
    printf("BACKEND requested=%s actual=%s init_error=%d\n", requested, b.info.name, editor_get_stats(e).backend_init_error);
    REQUIRE(settle(e) == 0);
    uint64_t values[32]; bench_samples cpu; bench_samples_init(&cpu, values, 32);
    uint64_t wake_start = editor_get_stats(e).poll_returns, wall = bench_now_ns();
    while (editor_get_stats(e).blinking) {
        uint64_t blink_count = editor_get_stats(e).blinks, start = process_ns();
        int rc = editor_step(e, -1); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        REQUIRE(settle(e) == 0);
        if (editor_get_stats(e).blinks > blink_count) (void)bench_add(&cpu, process_ns() - start);
        REQUIRE(bench_now_ns() - wall < UINT64_C(15000000000));
    }
    editor_stats s = editor_get_stats(e);
    uint64_t wakes = s.poll_returns - wake_start;
    uint64_t elapsed = bench_now_ns() - wall;
    double rate = (double)wakes * 1e9 / (double)elapsed;
    char name[80]; (void)snprintf(name, sizeof name, "editor_%s_G11_process_cpu", label);
    int miss = bench_report(name, &cpu, G11_P50, G11_P99);
    /* A bounded observation adds exactly one external test-deadline timeout.
     * Subtract that known timeout, retaining all earlier poll returns. */
    uint64_t quiet; REQUIRE(observe_quiet(e, &quiet) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    REQUIRE(editor_inject(e, &focus) == 0); REQUIRE(settle(e) == 0);
    uint64_t unfocused; REQUIRE(observe_quiet(e, &unfocused) == 0);
    printf("G11 %s (M)%s load1=%s blinks=%" PRIu64 " poll_returns=%" PRIu64 " wakeups/s=%.3f idle=%" PRIu64 " unfocused=%" PRIu64 " (G)<=2/s,0,0 mode=%s\n",
        label, tag.power, tag.load, s.blinks, wakes, rate, quiet, unfocused, track ? "TRACK" : "GATE");
    /* The fixed ten-second blink policy permits nineteen blinks plus its
     * terminal timeout. The observation starts after setup, so a raw rate
     * can exceed 2 slightly solely from that truncated first interval. */
    miss |= wakes > 20 || quiet || unfocused;
    editor_close(e); return track ? 0 : miss;
}
static int typing_row(const char *requested, size_t count, bool track, bool require_gl)
{
    stamp tag = power_stamp();
    render_backend b = {0}; REQUIRE(row_backend(&b, requested) == 0);
    char journal_path[] = "/tmp/editor-bench-XXXXXX"; int fd = mkstemp(journal_path); REQUIRE(fd >= 0); close(fd);
    samples measured = {0}; font_cell font = font_ascii_cell();
    editor_config cfg = {.path = "/tmp/edit-corpus/log_1g.txt", .journal_path = journal_path,
        .cols = 2880 / font.cell_w, .rows = 1800 / font.cell_h,
        .hook_ctx = &measured, .on_ingress = ingress, .on_submit = submitted, .on_present = presented, .on_io = io_boundary};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows; cfg.raster_fallback = true;
    editor *e = NULL; REQUIRE(editor_open(&e, &cfg, &b) == 0);
    REQUIRE(!require_gl || strcmp(requested, "gl") || (b.info.capabilities & RENDER_CAP_GPU));
    const char *label = row_label(&b, requested);
    printf("BACKEND requested=%s actual=%s init_error=%d\n", requested, b.info.name, editor_get_stats(e).backend_init_error);
    REQUIRE(settle(e) == 0);
    uint64_t deadline = bench_now_ns() + UINT64_C(5000000000);
    while (!editor_index_complete(e) && bench_now_ns() < deadline) REQUIRE(editor_step(e, 20) >= 0);
    uint64_t line = editor_line_count(e) * 9 / 10;
    bool exact = editor_index_complete(e);
    REQUIRE((exact ? editor_jump_line(e, line) : editor_set_cursor(e, editor_length(e) * 9 / 10)) == 0);
    REQUIRE(settle(e) == 0);
    printf("POSITION %s lines=%" PRIu64 " target_line=%" PRIu64 " byte=%" PRIu64 " index=%s A=2880x1800 workload=alternating_insert_backspace\n",
        label, editor_line_count(e), line, editor_view(e).selection.cursor, exact ? "published" : "byte_fallback");
    for (unsigned i = 0; i < 16; i++) { plat_event ev = event((i & 1u) != 0); REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0); }
    uint64_t *values = malloc(2 * count * sizeof *values); REQUIRE(values != NULL);
    bench_samples submit, t4; bench_samples_init(&submit, values, count); bench_samples_init(&t4, values + count, count);
    measured.measuring = true;
    for (size_t i = 0; i < count; i++) {
        plat_event ev = event((i & 1u) != 0); measured.frame = (editor_frame){0};
        REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0);
        REQUIRE(measured.frame.first_sequence == measured.sequence && measured.frame.last_sequence == measured.sequence);
        REQUIRE(measured.frame.submit_ns >= measured.ingress && measured.frame.present_ns >= measured.frame.submit_ns);
        REQUIRE(!measured.guard && !measured.suspended);
        (void)bench_add(&submit, measured.frame.submit_ns - measured.ingress);
        (void)bench_add(&t4, measured.frame.present_ns - measured.ingress);
    }
    measured.measuring = false;
    char name[80]; (void)snprintf(name, sizeof name, "editor_%s_ingress_submit_return%s", label, strcmp(requested, "null") ? "_G1" : "_TRACK");
    int miss = bench_report(name, &submit, G1_P50, G1_P99);
    (void)snprintf(name, sizeof name, "editor_%s_ingress_T4_present%s", label, strcmp(requested, "null") ? "_G1" : "_TRACK");
    miss |= bench_report(name, &t4, G1_P50, G1_P99);
    editor_stats s = editor_get_stats(e);
    printf("G1 %s (M)%s load1=%s keys=%zu allocations=%zu mutations=%" PRIu64 " journal=%" PRIu64 " slice_max_ns=%" PRIu64 " mode=%s\n",
        label, tag.power, tag.load, count, measured.allocations, s.mutations, s.journal_records, s.longest_slice_ns, track || !strcmp(requested, "null") ? "TRACK" : "GATE");
    REQUIRE(measured.allocations == 0);
    miss |= s.journal_error != 0;
    REQUIRE(editor_flush(e) == 0); editor_close(e); unlink(journal_path); free(values);
    return track || !strcmp(requested, "null") ? 0 : miss;
}
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
    REQUIRE(settle(e) == 0);
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
        REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0);
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
    int miss = bench_report(name, &frames, UINT64_C(5000000), UINT64_C(5560000));
    (void)snprintf(name, sizeof name, "editor_%s_100tabs_minimap_inside_frame", raster ? "raster" : "null");
    miss |= bench_report(name, &maps, 0, UINT64_C(500000));
    printf("G3 %s (M)%s load1=%s tabs=100 A=2880x1800 switches=200 allocations=%zu minimap_fills=%" PRIu64 " stale=%d mode=%s\n",
        raster ? "raster" : "null", tag.power, tag.load, measured.allocations,
        editor_get_stats(e).minimap_fills, editor_get_stats(e).minimap_stale, track ? "TRACK" : "GATE");
    miss |= measured.allocations != 0; editor_close(e); return track ? 0 : miss;
}
static int ipc_row(bool track)
{
    stamp tag = power_stamp(); char runtime[] = "/tmp/editor-ipc-bench-XXXXXX";
    REQUIRE(mkdtemp(runtime) != NULL); REQUIRE(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
    char path[4096], arg[4096]; (void)snprintf(path, sizeof path, "%s/file.txt", runtime);
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
    int miss = bench_report("editor_second_invocation_exec_open_ACK_exit", &handoff, 0, UINT64_C(10000000));
    printf("IPC (M)%s load1=%s invocations=64 opened_tabs=%zu includes=exec,parse,open,ACK,exit,reap mode=%s\n",
        tag.power, tag.load, editor_get_stats(e).tabs, track ? "TRACK" : "GATE");
    editor_close(e); ipc_server_fini(&server); unlink(path);
    char lock[4096]; (void)snprintf(lock, sizeof lock, "%s/sublimite-%lu.lock", runtime, (unsigned long)getuid());
    unlink(lock); REQUIRE(rmdir(runtime) == 0); return track ? 0 : miss;
}
int main(int argc, char **argv)
{
    bool track = false, idle = true, typing = true, p4 = true, ipc_only = false, require_gl = false; size_t count = 10000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--track")) track = true;
        else if (!strcmp(argv[i], "--require-gl")) require_gl = true;
        else if (!strcmp(argv[i], "--no-idle")) idle = false;
        else if (!strcmp(argv[i], "--idle-only")) { typing = false; p4 = false; }
        else if (!strcmp(argv[i], "--p4-only")) { typing = false; idle = false; }
        else if (!strcmp(argv[i], "--no-p4")) p4 = false;
        else if (!strcmp(argv[i], "--ipc-only")) { typing = false; idle = false; p4 = false; ipc_only = true; }
        else if (!strncmp(argv[i], "--keys=", 7)) count = (size_t)strtoull(argv[i] + 7, NULL, 10);
        else { fprintf(stderr, "usage: editor_bench [--track] [--require-gl] [--no-idle|--idle-only|--p4-only|--ipc-only] [--no-p4] [--keys=N]\n"); return 2; }
    }
    const char *override = getenv("EDIT_BACKEND");
    if (override && strcmp(override, "gl") && strcmp(override, "raster")) return 2;
    if (require_gl && override && strcmp(override, "gl")) return 2;
    if (!count || count > 100000) return 2;
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    trace_init(); (void)trace_thread_register();
    int rows = 0;
    const char *names[] = {"null", "raster", "gl"};
    if (typing) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *name = names[i];
        if (override && strcmp(name, "null") && strcmp(name, override)) continue;
        int rc = typing_row(name, count, track, require_gl); if (rc < 0) return 1; rows |= rc;
    }
    if (idle) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *name = names[i];
        if (override && strcmp(name, "null") && strcmp(name, override)) continue;
        int rc = idle_row(name, track, require_gl); if (rc < 0) return 1; rows |= rc;
    }
    int f = 0, g = 0, h = 0;
    if (p4) { f = tab_row(false, track); if (f < 0) return 1; g = tab_row(true, track); if (g < 0) return 1; h = ipc_row(track); if (h < 0) return 1; }
    if (ipc_only) { h = ipc_row(track); if (h < 0) return 1; }
    return rows || f || g || h;
}
