/* Same executable protocol for all P4.14 variants; no variant timing loop.
 * Starts warm servers once; launches A/B/C in strict round-robin order. */
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

#define ZYGOTE_N 200u
typedef struct zygote_variant {
    char id, executable[256], runtime[256];
    pid_t server;
    int output;
    zygote_record warm;
    uint64_t map[ZYGOTE_N], ready[ZYGOTE_N], present[ZYGOTE_N], complete[ZYGOTE_N];
    uint64_t max_rss, max_hwm, last_minor, last_major;
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
    int fds[2]; if (pipe2(fds, O_CLOEXEC)) return -1;
    v->server = fork();
    if (!v->server) {
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(fds[0]); close(fds[1]);
        execl(v->executable, v->executable, "--serve", v->runtime, (char *)NULL); _exit(127);
    }
    close(fds[1]); v->output = fds[0];
    return v->server > 0 && !zygote_read(v->output, &v->warm, sizeof v->warm) && !v->warm.result ? 0 : -1;
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
        if (v->id == 'a') execl(v->executable, v->executable, "--normal", (char *)NULL);
        else execl(v->executable, v->executable, "--launch", v->runtime, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    int rc = child > 0 ? zygote_read(fds[0], start, sizeof *start) : -1;
    if (!rc) rc = zygote_read(v->id == 'a' ? fds[0] : v->output, record, sizeof *record);
    close(fds[0]);
    if (rc && child > 0) (void)kill(child, SIGTERM);
    if (child > 0 && zygote_wait(child)) rc = -1;
    if (rc || record->result || !record->mapped_verified || !record->unmapped_verified ||
        record->map_ns < *start || record->present_ns < record->map_ns ||
        record->complete_ns < record->present_ns || record->ready_ns < *start ||
        record->ready_ns > record->present_ns || !record->ready_frame ||
        record->ready_frame != record->present_frame || record->ready_frame != record->complete_frame) return -1;
    return 0;
}
static void zygote_cleanup(zygote_variant *v)
{
    if (v->server > 0) {
        /* The private experiment server has no user data; kill on failed runs. */
        (void)kill(v->server, SIGTERM); (void)waitpid(v->server, NULL, 0);
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
    printf("BENCH variant=%c name=%s n=%zu p50_ns=%" PRIu64 " p99_ns=%" PRIu64 " evidence=(M)%s power=%s load1=%s verdict=TRACK\n",
        v->id, name, n, values[i50], values[i99], s->tag, s->power, s->load);
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
static void zygote_idle_memory(const zygote_variant *v, const zygote_stamp *s)
{
    char path[64], line[256];
    (void)snprintf(path, sizeof path, "/proc/%ld/smaps_rollup", (long)v->server);
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned long long value;
            if (sscanf(line, "Rss: %llu kB", &value) == 1 || sscanf(line, "Pss: %llu kB", &value) == 1 ||
                sscanf(line, "Private_Dirty: %llu kB", &value) == 1 || sscanf(line, "Private_Clean: %llu kB", &value) == 1) {
                line[strcspn(line, "\n")] = 0;
                printf("MEMORY variant=%c %s evidence=(M)%s power=%s load1=%s\n", v->id, line, s->tag, s->power, s->load);
            }
        }
        fclose(f);
    }
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        /* Makefile bench discovery uses no args; share the independent build. */
        execl("tools/zygote_bench.sh", "tools/zygote_bench.sh", "--run", (char *)NULL);
        perror("zygote build runner"); return 2;
    }
    bool self = argc == 2 && !strcmp(argv[1], "--self-check");
    if (!self && (argc != 2 || strcmp(argv[1], "--run"))) {
        fprintf(stderr, "usage: zygote_bench --self-check|--run\n"); return 2;
    }
    if (!getenv("DISPLAY") || strcmp(getenv("DISPLAY"), ":99") ||
        !getenv("EDIT_DISPLAY") || strcmp(getenv("EDIT_DISPLAY"), ":99")) return 2;
    zygote_stamp stamp;
    if (zygote_stamp_read(&stamp)) return 1;
    zygote_variant variants[3] = {0};
    int result = 1;
    for (size_t i = 0; i < 3; i++) {
        zygote_variant *v = &variants[i]; v->id = (char)('a' + (int)i); v->output = -1;
        (void)snprintf(v->executable, sizeof v->executable, "variants/P4.14/%c/launch", v->id);
        (void)snprintf(v->runtime, sizeof v->runtime, "/tmp/zygote-%c-XXXXXX", v->id);
        if (access(v->executable, X_OK)) {
            fprintf(stderr, "zygote contract: FAIL missing executable %s\n", v->executable); goto done;
        }
    }
    for (size_t i = 1; i < 3; i++) {
        if (zygote_server_start(&variants[i])) { fprintf(stderr, "zygote contract: FAIL warm server %c\n", variants[i].id); goto done; }
        zygote_record *r = &variants[i].warm;
        printf("WARM variant=%c gl_result=%d rss_kib=%" PRIu64 " hwm_kib=%" PRIu64 " minor=%" PRIu64 " major=%" PRIu64 " evidence=(M)%s power=%s load1=%s\n",
            variants[i].id, r->gl_result, r->rss_kib, r->hwm_kib, r->minor_faults, r->major_faults, stamp.tag, stamp.power, stamp.load);
        variants[i].last_minor = r->minor_faults; variants[i].last_major = r->major_faults;
    }
    size_t n = self ? 2 : ZYGOTE_N;
    for (size_t round = 0; round < n; round++) {
        for (size_t i = 0; i < 3; i++) {
            zygote_variant *v = &variants[i]; zygote_record r = {0}; uint64_t start;
            if (zygote_stamp_read(&stamp) || zygote_launch(v, &start, &r)) {
                fprintf(stderr, "zygote contract: FAIL launch variant=%c round=%zu\n", v->id, round); goto done;
            }
            uint64_t endpoint = r.ready_ns > r.map_ns ? r.ready_ns : r.map_ns;
            v->map[round] = r.map_ns - start; v->ready[round] = endpoint - start;
            v->present[round] = r.present_ns - start; v->complete[round] = r.complete_ns - start;
            if (r.rss_kib > v->max_rss) v->max_rss = r.rss_kib;
            if (r.hwm_kib > v->max_hwm) v->max_hwm = r.hwm_kib;
            if (!self) printf("SAMPLE round=%zu variant=%c start_ns=%" PRIu64 " map_ns=%" PRIu64 " ready_map_ns=%" PRIu64 " present_ns=%" PRIu64 " complete_ns=%" PRIu64 " minor_delta=%" PRIu64 " major_delta=%" PRIu64 " rss_kib=%" PRIu64 " gl_result=%d evidence=(M)%s power=%s load1=%s\n",
                round, v->id, start, v->map[round], v->ready[round], v->present[round], v->complete[round],
                r.minor_faults - v->last_minor, r.major_faults - v->last_major, r.rss_kib, r.gl_result, stamp.tag, stamp.power, stamp.load);
            if (i) { v->last_minor = r.minor_faults; v->last_major = r.major_faults; }
        }
    }
    if (!self) {
        uint64_t before[2] = {zygote_ticks(variants[1].server), zygote_ticks(variants[2].server)};
        uint64_t switches[2] = {zygote_switches(variants[1].server), zygote_switches(variants[2].server)};
        if (zygote_stamp_read(&stamp)) goto done;
        uint64_t begin = zygote_now();
        struct timespec delay = {12, 0};
        while (nanosleep(&delay, &delay) && errno == EINTR) { }
        uint64_t elapsed = zygote_now() - begin;
        for (size_t i = 1; i < 3; i++) {
            uint64_t after = zygote_ticks(variants[i].server);
            printf("IDLE variant=%c elapsed_ns=%" PRIu64 " cpu_ticks=%" PRIu64 " tick_hz=%ld context_switch_delta=%" PRIu64 " evidence=(M)%s power=%s load1=%s\n",
                variants[i].id, elapsed, after - before[i - 1], sysconf(_SC_CLK_TCK), zygote_switches(variants[i].server) - switches[i - 1], stamp.tag, stamp.power, stamp.load);
            zygote_idle_memory(&variants[i], &stamp);
        }
        for (size_t i = 0; i < 3; i++) {
            zygote_variant *v = &variants[i]; struct stat st;
            if (stat(v->executable, &st)) goto done;
            zygote_row(v, "exec_map_requested", v->map, n, &stamp);
            zygote_row(v, "exec_ready_and_map", v->ready, n, &stamp);
            zygote_row(v, "exec_first_present_request", v->present, n, &stamp);
            zygote_row(v, "exec_first_present_complete", v->complete, n, &stamp);
            printf("SIZE variant=%c binary_bytes=%jd max_rss_kib=%" PRIu64 " max_hwm_kib=%" PRIu64 " evidence=(M)%s power=%s load1=%s\n",
                v->id, (intmax_t)st.st_size, v->max_rss, v->max_hwm, stamp.tag, stamp.power, stamp.load);
        }
    }
    result = 0;
done:
    for (size_t i = 1; i < 3; i++) {
        zygote_cleanup(&variants[i]);
        if (variants[i].server > 0 && access(variants[i].runtime, F_OK) == 0) {
            fprintf(stderr, "zygote contract: FAIL cleanup private runtime variant=%c\n", variants[i].id);
            result = 1;
        }
    }
    if (!result) puts("zygote contract: PASS A/B/C exec, IPC wait, real map/present completion, unmap, repeated warm launch");
    return result;
}
