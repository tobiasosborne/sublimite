/* Same executable protocol for all P4.14 variants; no variant timing loop.
 * Starts warm servers once; interleaves A/B/C, then separately launches D GL exec. */
#include "../variants/P4.14/protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ZYGOTE_N 10000u
typedef struct zygote_variant {
    char id, executable[256], runtime[256];
    pid_t server;
    int output;
    bool normal, real, measured_native, runtime_created;
    zygote_record warm;
    uint64_t map[ZYGOTE_N], ready[ZYGOTE_N], present[ZYGOTE_N], device[ZYGOTE_N], complete[ZYGOTE_N];
    uint64_t max_rss, max_hwm, last_minor, last_major, last_complete_ns;
} zygote_variant;
typedef struct zygote_stamp { char power[32], load[32]; const char *tag; } zygote_stamp;
static uint64_t zygote_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static int zygote_stamp_read(zygote_stamp *s)
{
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!f) return -1;
    if (!fgets(s->power, sizeof s->power, f)) { fclose(f); return -1; }
    fclose(f); s->power[strcspn(s->power, "\n")] = 0;
    s->tag = !strcmp(s->power, "Discharging") ? "[bat]" : "[AC]";
    f = fopen("/proc/loadavg", "r"); if (!f) return -1;
    int rc = fscanf(f, "%31s", s->load); fclose(f);
    return rc == 1 ? 0 : -1;
}
static int zygote_read(int fd, void *data, size_t size)
{
    uint8_t *p = data;
    uint64_t deadline = zygote_now() + UINT64_C(30000000000);
    while (size) {
        struct pollfd pollfd = {fd, POLLIN, 0};
        int rc = poll(&pollfd, 1, 1000);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0 || zygote_now() > deadline) return -1;
        if (!rc) continue;
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        size -= (size_t)n; p += (size_t)n;
    }
    return 0;
}
static int zygote_wait(pid_t child)
{
    int status;
    while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) return -1; }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
static int zygote_server_start(zygote_variant *v)
{
    if (!mkdtemp(v->runtime)) return -1;
    v->runtime_created = true;
    int fds[2]; if (pipe2(fds, O_CLOEXEC)) return -1;
    v->server = fork();
    if (!v->server) {
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(fds[0]); close(fds[1]);
        execl(v->executable, v->executable, "--serve", v->runtime,
              v->real ? "--real-display" : (char *)NULL, (char *)NULL); _exit(127);
    }
    close(fds[1]); v->output = fds[0];
    return v->server > 0 && !zygote_read(v->output, &v->warm, sizeof v->warm) && !v->warm.result ? 0 : -1;
}
/* CPU submit/buffer readiness precedes present. Device fence follows present,
 * independently correlated with the original frame and matching Present. */
static bool zygote_record_valid(const zygote_record *r, uint64_t start, bool require_native)
{
    if (r->result || !r->mapped_verified || !r->unmapped_verified ||
        r->map_ns < start || r->present_ns < r->map_ns ||
        r->ready_ns < start || r->ready_ns > r->present_ns ||
        r->device_ns < r->present_ns || r->complete_ns < r->device_ns ||
        !r->ready_frame || r->ready_frame != r->present_frame ||
        r->ready_frame != r->device_frame || r->ready_frame != r->complete_frame) return false;
    if (require_native && !r->native_gl) return false;
    return !r->native_gl || (!r->gl_result && r->displayed_msc && r->device[0] && r->driver[0]);
}
static int zygote_launch(zygote_variant *v, uint64_t *start, zygote_record *record)
{
    int fds[2]; if (pipe2(fds, O_CLOEXEC)) return -1;
    pid_t child = fork();
    if (!child) {
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(fds[0]); close(fds[1]);
        uint64_t t0 = zygote_now();
        if (write(STDOUT_FILENO, &t0, sizeof t0) != (ssize_t)sizeof t0) _exit(126);
        if (v->normal) execl(v->executable, v->executable, "--normal",
                              v->real ? "--real-display" : (char *)NULL, (char *)NULL);
        else execl(v->executable, v->executable, "--launch", v->runtime,
                   v->real ? "--real-display" : (char *)NULL, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    int rc = child > 0 ? zygote_read(fds[0], start, sizeof *start) : -1;
    if (!rc) rc = zygote_read(v->normal ? fds[0] : v->output, record, sizeof *record);
    close(fds[0]);
    if (rc && child > 0) (void)kill(child, SIGTERM);
    if (child > 0 && zygote_wait(child)) rc = -1;
    if (rc || !zygote_record_valid(record, *start, v->real && (v->id == 'b' || v->id == 'd'))) {
        fprintf(stderr, "zygote launch rejected: variant=%c result=%d gl_result=%d native_gl=%d\n",
                v->id, record->result, record->gl_result, record->native_gl);
        return -1;
    }
    return 0;
}
static void zygote_cleanup(zygote_variant *v)
{
    if (v->server > 0) {
        /* The private experiment server has no user data; kill on failed runs. */
        (void)kill(v->server, SIGTERM); (void)waitpid(v->server, NULL, 0);
    }
    if (v->runtime_created) {
        if (v->output >= 0) close(v->output);
        char path[512];
        int n = snprintf(path, sizeof path, "%s/sublimite-%lu.sock", v->runtime, (unsigned long)getuid());
        if (n > 0 && (size_t)n < sizeof path) {
            (void)unlink(path);
            n = snprintf(path, sizeof path, "%s/sublimite-%lu.lock", v->runtime, (unsigned long)getuid());
            if (n > 0 && (size_t)n < sizeof path) (void)unlink(path);
        }
        (void)rmdir(v->runtime);
    }
}
static int zygote_compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static void zygote_row(zygote_variant *v, const char *name, uint64_t *values, size_t n, const zygote_stamp *s)
{
    qsort(values, n, sizeof *values, zygote_compare);
    size_t i50 = (n + 1) / 2 - 1, i99 = (99 * n + 99) / 100 - 1;
    printf("BENCH variant=%c backend=%s name=%s n=%zu p50_ns=%" PRIu64 " p99_ns=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
        v->id, v->measured_native ? "native_gl" : (v->id == 'b' || v->id == 'd') ? "cpu_fallback" : "cpu", name, n, values[i50], values[i99], s->tag, s->power, s->load);
}
static uint64_t zygote_ticks(pid_t pid)
{
    char path[64], line[4096];
    (void)snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    FILE *f = fopen(path, "r"); if (!f) return UINT64_MAX;
    char *got = fgets(line, sizeof line, f); fclose(f);
    if (!got) return UINT64_MAX;
    char *p = strrchr(line, ')'); if (!p) return UINT64_MAX;
    p += 2; uint64_t ticks = 0;
    for (unsigned field = 3; field <= 15; field++) {
        char *end = strchr(p, ' '); if (!end) return UINT64_MAX;
        *end = 0;
        if (field == 14 || field == 15) ticks += strtoull(p, NULL, 10);
        p = end + 1;
    }
    return ticks;
}
/* All-thread scheduling counts. Zero deltas bound idle wakeups; nonzero
 * deltas are context switches, not a claim of one switch per wakeup. */
static uint64_t zygote_switches(pid_t pid)
{
    char pattern[80], line[256];
    (void)snprintf(pattern, sizeof pattern, "/proc/%ld/task/*/status", (long)pid);
    glob_t paths = {0};
    if (glob(pattern, 0, NULL, &paths)) { globfree(&paths); return UINT64_MAX; }
    uint64_t total = 0;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        FILE *f = fopen(paths.gl_pathv[i], "r");
        if (!f) { globfree(&paths); return UINT64_MAX; }
        while (fgets(line, sizeof line, f)) {
            unsigned long long value;
            if (sscanf(line, "voluntary_ctxt_switches: %llu", &value) == 1 ||
                sscanf(line, "nonvoluntary_ctxt_switches: %llu", &value) == 1) total += (uint64_t)value;
        }
        fclose(f);
    }
    globfree(&paths); return total;
}
static int zygote_idle_memory(const zygote_variant *v, const zygote_stamp *s)
{
    char path[64], line[256];
    (void)snprintf(path, sizeof path, "/proc/%ld/smaps_rollup", (long)v->server);
    FILE *f = fopen(path, "r");
    bool rss_seen = false;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned long long value;
            if (sscanf(line, "Rss: %llu kB", &value) == 1 || sscanf(line, "Pss: %llu kB", &value) == 1 ||
                sscanf(line, "Private_Dirty: %llu kB", &value) == 1 || sscanf(line, "Private_Clean: %llu kB", &value) == 1) {
                if (!strncmp(line, "Rss:", 4)) rss_seen = true;
                line[strcspn(line, "\n")] = 0;
                printf("MEMORY variant=%c %s evidence=(M)%s power=%s load1=%s\n", v->id, line, s->tag, s->power, s->load);
            }
        }
        fclose(f);
    }
    return rss_seen ? 0 : -1;
}
static const char *zygote_backend_name(const zygote_record *r, char id)
{
    return r->native_gl ? "native_gl" : (id == 'b' || id == 'd') ? "cpu_fallback" : "cpu";
}
static void zygote_identity(const zygote_record *r, char id, const zygote_stamp *s)
{
    printf("DEVICE variant=%c backend=%s gl_result=%d device=\"%s\" driver=\"%s\" evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
           id, zygote_backend_name(r, id), r->gl_result, r->device, r->driver, s->tag, s->power, s->load);
    if (!r->native_gl && (id == 'b' || id == 'd'))
        printf("NATIVE_GL variant=%c status=SKIP reason=Xvfb_native_Present_unsupported fallback=CPU\n", id);
}
static int zygote_observe(zygote_variant *v, size_t round, zygote_stamp *stamp)
{
    zygote_record r = {0}; uint64_t start = 0;
    if (zygote_stamp_read(stamp) || zygote_launch(v, &start, &r)) {
        fprintf(stderr, "zygote contract: FAIL launch variant=%c round=%zu\n", v->id, round); return -1;
    }
    v->last_complete_ns = r.complete_ns;
    v->measured_native = r.native_gl;
    uint64_t endpoint = r.ready_ns > r.map_ns ? r.ready_ns : r.map_ns;
    v->map[round] = r.map_ns - start; v->ready[round] = endpoint - start;
    v->present[round] = r.present_ns - start; v->device[round] = r.device_ns - start;
    v->complete[round] = r.complete_ns - start;
    if (r.rss_kib > v->max_rss) v->max_rss = r.rss_kib;
    if (r.hwm_kib > v->max_hwm) v->max_hwm = r.hwm_kib;
    if (!round) zygote_identity(&r, v->id, stamp);
    printf("SAMPLE round=%zu variant=%c backend=%s start_ns=%" PRIu64 " map_ns=%" PRIu64 " ready_map_ns=%" PRIu64 " present_ns=%" PRIu64 " device_ns=%" PRIu64 " complete_ns=%" PRIu64 " ready_frame=%u present_frame=%u device_frame=%u complete_frame=%u displayed_msc=%" PRIu64 " cols=%u rows=%u width=%u height=%u minor_delta=%" PRIu64 " major_delta=%" PRIu64 " rss_kib=%" PRIu64 " gl_result=%d evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
           round, v->id, zygote_backend_name(&r, v->id), start, v->map[round], v->ready[round],
           v->present[round], v->device[round], v->complete[round], r.ready_frame, r.present_frame,
           r.device_frame, r.complete_frame, r.displayed_msc, r.cols, r.rows, r.width, r.height, r.minor_faults - v->last_minor,
           r.major_faults - v->last_major, r.rss_kib, r.gl_result, stamp->tag, stamp->power, stamp->load);
    if (!v->normal) { v->last_minor = r.minor_faults; v->last_major = r.major_faults; }
    return 0;
}
static int zygote_after_idle(zygote_variant *v, zygote_stamp *stamp)
{
    zygote_record r = {0}; uint64_t start = 0;
    if (zygote_stamp_read(stamp) || zygote_launch(v, &start, &r)) return -1;
    printf("AFTER_IDLE variant=%c backend=%s idle_ns=%" PRIu64 " map_ns=%" PRIu64 " present_ns=%" PRIu64 " device_ns=%" PRIu64 " complete_ns=%" PRIu64 " frame=%u displayed_msc=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
           v->id, zygote_backend_name(&r, v->id), start - v->last_complete_ns,
           r.map_ns - start, r.present_ns - start, r.device_ns - start, r.complete_ns - start,
           r.complete_frame, r.displayed_msc, stamp->tag, stamp->power, stamp->load);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        /* Makefile bench discovery uses no args; share the independent build. */
        execl("tools/zygote_bench.sh", "tools/zygote_bench.sh", "--run", (char *)NULL);
        perror("zygote build runner"); return 2;
    }
    bool self = true, selected = false, real = false, count_set = false;
    size_t n = 200;
    for (int arg = 1; arg < argc; arg++) {
        if (!strcmp(argv[arg], "--real-display")) real = true;
        else if (!strcmp(argv[arg], "--run") || !strcmp(argv[arg], "--self-check")) {
            if (selected) { fprintf(stderr, "choose one mode\n"); return 2; }
            self = !strcmp(argv[arg], "--self-check"); selected = true;
        } else if (!strcmp(argv[arg], "--launches")) {
            if (++arg == argc) { fprintf(stderr, "--launches requires N\n"); return 2; }
            const char *value = argv[arg];
            if (!*value || strspn(value, "0123456789") != strlen(value) || strlen(value) > 5) {
                fprintf(stderr, "--launches must be an integer in 1..10000\n"); return 2;
            }
            unsigned long parsed = strtoul(value, NULL, 10);
            if (!parsed || parsed > ZYGOTE_N) {
                fprintf(stderr, "--launches must be an integer in 1..10000\n"); return 2;
            }
            n = (size_t)parsed; count_set = true;
        } else {
            fprintf(stderr, "usage: zygote_bench [--self-check|--run] [--launches N] [--real-display]\n"); return 2;
        }
    }
    if (real && (!getenv("EDIT_ALLOW_REAL_DISPLAY") || strcmp(getenv("EDIT_ALLOW_REAL_DISPLAY"), "1"))) {
        fprintf(stderr, "--real-display requires EDIT_ALLOW_REAL_DISPLAY=1\n"); return 2;
    }
    if (!zygote_display_check(real)) { fprintf(stderr, "zygote: display refused\n"); return 2; }
    if (self && !count_set) n = 2;
    zygote_stamp stamp;
    if (zygote_stamp_read(&stamp)) return 1;
    printf("ESTIMATE duration_s=%zu launches_per_variant=%zu order=ABC_then_D display=%s real_display=%d evidence=(E)%s power=%s load1=%s includes=service_startup_launches_idle excludes=build conservative_not_deadline=1\n",
           10 + n * 2 + (self ? 0u : 12u), n, getenv("DISPLAY"), real, stamp.tag, stamp.power, stamp.load);
    printf("CONDITIONS swap_interval=%s vbo=%s viewport=%s workload=empty_document cadence=back_to_back effects=caller_record_required\n",
           getenv("EDIT_GL_SWAP_INTERVAL") ? getenv("EDIT_GL_SWAP_INTERVAL") : "0",
           getenv("EDIT_GL_VBO") ? getenv("EDIT_GL_VBO") : "orphan", real ? "display_root_cells" : "120x40_cells");
    puts("COVERAGE on_glass=SKIP compositor_timing=SKIP owned_graphics_peak=SKIP gpu_power=SKIP focused_blink_G11=SKIP cold_cache=SKIP");
    fflush(stdout);
    zygote_variant *variants = calloc(4, sizeof *variants);
    if (!variants) return 1;
    int result = 1;
    for (size_t i = 0; i < 4; i++) {
        zygote_variant *v = &variants[i]; v->id = (char)('a' + (int)i); v->output = -1;
        v->normal = i == 0 || i == 3; v->real = real;
        (void)snprintf(v->executable, sizeof v->executable, "variants/P4.14/%c/launch", v->id);
        (void)snprintf(v->runtime, sizeof v->runtime, "/tmp/zygote-%c-XXXXXX", v->id);
        if (access(v->executable, X_OK)) {
            fprintf(stderr, "zygote contract: FAIL missing executable %s\n", v->executable); goto done;
        }
    }
    for (size_t i = 1; i < 3; i++) {
        if (zygote_stamp_read(&stamp)) goto done;
        uint64_t begin = zygote_now();
        if (zygote_server_start(&variants[i])) {
            fprintf(stderr, "zygote contract: FAIL warm server %c result=%d gl_result=%d\n",
                    variants[i].id, variants[i].warm.result, variants[i].warm.gl_result); goto done;
        }
        zygote_record *r = &variants[i].warm;
        if (real && i == 1 && !r->native_gl) { fprintf(stderr, "zygote: native GL required\n"); goto done; }
        printf("WARM variant=%c backend=%s gl_result=%d init_ns=%" PRIu64 " service_fork_exec_ready_ns=%" PRIu64 " rss_kib=%" PRIu64 " hwm_kib=%" PRIu64 " minor=%" PRIu64 " major=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
               variants[i].id, zygote_backend_name(r, variants[i].id), r->gl_result, r->init_ns,
               zygote_now() - begin, r->rss_kib, r->hwm_kib, r->minor_faults, r->major_faults, stamp.tag, stamp.power, stamp.load);
        variants[i].last_minor = r->minor_faults; variants[i].last_major = r->major_faults;
    }
    /* D is kept outside A/B/C pairing to separate fresh GL choice from warming. */
    for (size_t round = 0; round < n; round++)
        for (size_t i = 0; i < 3; i++)
            if (zygote_observe(&variants[i], round, &stamp)) goto done;
    for (size_t round = 0; round < n; round++)
        if (zygote_observe(&variants[3], round, &stamp)) goto done;
    if (!self) {
        uint64_t before[2] = {zygote_ticks(variants[1].server), zygote_ticks(variants[2].server)};
        uint64_t switches[2] = {zygote_switches(variants[1].server), zygote_switches(variants[2].server)};
        if (before[0] == UINT64_MAX || before[1] == UINT64_MAX ||
            switches[0] == UINT64_MAX || switches[1] == UINT64_MAX || zygote_stamp_read(&stamp)) goto done;
        uint64_t begin = zygote_now();
        struct timespec delay = {12, 0};
        while (nanosleep(&delay, &delay) && errno == EINTR) { }
        uint64_t elapsed = zygote_now() - begin;
        for (size_t i = 1; i < 3; i++) {
            uint64_t after = zygote_ticks(variants[i].server), contexts = zygote_switches(variants[i].server);
            if (after == UINT64_MAX || contexts == UINT64_MAX) goto done;
            printf("IDLE variant=%c elapsed_ns=%" PRIu64 " cpu_ticks=%" PRIu64 " tick_hz=%ld context_switch_delta=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
                   variants[i].id, elapsed, after - before[i - 1], sysconf(_SC_CLK_TCK), contexts - switches[i - 1], stamp.tag, stamp.power, stamp.load);
        }
    }
    if (zygote_stamp_read(&stamp)) goto done;
    for (size_t i = 1; i < 3; i++)
        if (zygote_idle_memory(&variants[i], &stamp)) { fprintf(stderr, "zygote: idle RSS unavailable\n"); goto done; }
    if (!self)
        for (size_t i = 1; i < 3; i++)
            if (zygote_after_idle(&variants[i], &stamp)) goto done;
    if (zygote_stamp_read(&stamp)) goto done;
    for (size_t i = 0; i < 4; i++) {
        zygote_variant *v = &variants[i]; struct stat st;
        if (stat(v->executable, &st)) goto done;
        zygote_row(v, "exec_map_requested", v->map, n, &stamp);
        zygote_row(v, v->measured_native ? "exec_cpu_submit_ready_and_map" : "exec_ready_and_map", v->ready, n, &stamp);
        zygote_row(v, "exec_first_present_request", v->present, n, &stamp);
        zygote_row(v, "exec_device_done", v->device, n, &stamp);
        zygote_row(v, "exec_first_present_complete", v->complete, n, &stamp);
        printf("SIZE variant=%c binary_bytes=%jd max_rss_kib=%" PRIu64 " max_hwm_kib=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
               v->id, (intmax_t)st.st_size, v->max_rss, v->max_hwm, stamp.tag, stamp.power, stamp.load);
    }
    result = 0;
done:
    for (size_t i = 1; i < 3; i++) {
        zygote_cleanup(&variants[i]);
        if (variants[i].runtime_created && access(variants[i].runtime, F_OK) == 0) {
            fprintf(stderr, "zygote contract: FAIL cleanup private runtime variant=%c\n", variants[i].id);
            result = 1;
        }
    }
    free(variants);
    if (!result) puts("zygote contract: PASS A/B/C interleaved, D normal GL exec, IPC wait, matching readiness/fence/Present, map/unmap, cleanup");
    return result;
}
