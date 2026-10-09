/* P3.6: identical exec/first-frame interface for every isolated variant. */
#include "../variants/P3.6/common/protocol.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec ts; if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static uint64_t endpoint(uint64_t map, uint64_t input, uint64_t cpu)
{
    if (!map || !input || !cpu) return 0;
    uint64_t v = map > input ? map : input; return cpu > v ? cpu : v;
}
static int compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static uint64_t percentile(uint64_t *values, size_t n, unsigned percent)
{
    qsort(values, n, sizeof *values, compare);
    return values[((n * percent + 99u) / 100u) - 1u];
}
static int selftest(void)
{
    uint64_t v[200]; for (size_t i = 0; i < 200; i++) v[i] = 200u - i;
    if (endpoint(10, 30, 20) != 30 || endpoint(40, 30, 20) != 40 || endpoint(10, 30, 50) != 50 || endpoint(0, 30, 50) != 0 ||
        percentile(v, 200, 50) != 100 || percentile(v, 200, 99) != 198) return 1;
    puts("linkorder_bench: selftest passed (all endpoint prerequisites, nearest-rank p50/p99)"); return 0;
}
static int stamp(char *power, size_t cap, double *load)
{
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r"); if (!f) return -1;
    char status[64]; bool ok = fgets(status, sizeof status, f) != NULL; (void)fclose(f); if (!ok) return -1;
    status[strcspn(status, "\n")] = '\0';
    (void)snprintf(power, cap, "%s", strcmp(status, "Discharging") == 0 ? "bat" :
        (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0 || strcmp(status, "Not charging") == 0) ? "AC" : "unknown");
    f = fopen("/proc/loadavg", "r"); if (!f) return -1;
    int rc = fscanf(f, "%lf", load) == 1 ? 0 : -1; (void)fclose(f); return rc;
}
static int smaps(pid_t child, uint64_t *text_kb, uint64_t *rss_kb)
{
    char path[80]; (void)snprintf(path, sizeof path, "/proc/%ld/smaps", (long)child);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char line[4096]; bool text = false;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi; char perms[5]; unsigned long kb;
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) == 3)
            text = perms[2] == 'x' && strstr(line, "/variants/P3.6/") != NULL;
        else if (sscanf(line, "Rss: %lu kB", &kb) == 1) { *rss_kb += kb; if (text) *text_kb += kb; }
    }
    return fclose(f);
}
static void cleanup(const char *root)
{
    char dir[4096]; (void)snprintf(dir, sizeof dir, "%s/sublimite", root);
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d))) {
            if (ent->d_name[0] == '.') continue;
            char path[8192]; (void)snprintf(path, sizeof path, "%s/%s", dir, ent->d_name); (void)unlink(path);
        }
        (void)closedir(d); (void)rmdir(dir);
    }
    (void)rmdir(root);
}
static int launch(const char *variant, size_t iteration, const char *file, const char *profile_binary, FILE *csv,
                  uint64_t *latency, uint64_t *text_pages, uint64_t *rss)
{
    char power[16]; double load;
    if (stamp(power, sizeof power, &load)) return -1;
    char binary[4096], logpath[4096];
    (void)snprintf(binary, sizeof binary, "%s", profile_binary ? profile_binary : "");
    if (!profile_binary) (void)snprintf(binary, sizeof binary, "variants/P3.6/%s/sublimite", variant);
    (void)snprintf(logpath, sizeof logpath, "variants/P3.6/%s/launch.log", variant);
    char root[] = "/tmp/linkorder-launch-XXXXXX"; if (!mkdtemp(root)) return -1;
    int fd = memfd_create("linkorder-bench", 0), ack[2];
    if (fd < 0 || ftruncate(fd, (off_t)sizeof(linkorder_bench_state)) || pipe(ack)) return -1;
    linkorder_bench_state *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) return -1;
    memset(s, 0, sizeof *s);
    atomic_init(&s->exec_ns, 0); atomic_init(&s->map_ns, 0); atomic_init(&s->input_ns, 0); atomic_init(&s->cpu_ns, 0);
    atomic_init(&s->jobs, 0); atomic_init(&s->ready, 0);
    int logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0600);
    int tracefd = profile_binary ? open("variants/P3.6/b/functions.bin", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600) : -1;
    if (logfd < 0 || (profile_binary && tracefd < 0)) return -1;
    pid_t child = fork(); if (child < 0) return -1;
    if (!child) {
        if (dup2(fd, LINKORDER_BENCH_FD) < 0 || dup2(ack[0], LINKORDER_ACK_FD) < 0 || dup2(logfd, STDERR_FILENO) < 0 ||
            (profile_binary && dup2(tracefd, LINKORDER_PROFILE_FD) < 0)) _exit(125);
        (void)close(ack[1]);
        if (setenv("DISPLAY", ":99", 1) || setenv("EDIT_DISPLAY", ":99", 1) || unsetenv("EDIT_ALLOW_REAL_DISPLAY") ||
            setenv("XDG_DATA_HOME", root, 1) || unsetenv("EDIT_TRACE_DUMP")) _exit(125);
        atomic_store_explicit(&s->exec_ns, now_ns(), memory_order_release);
        execl(binary, binary, file, (char *)NULL); _exit(127);
    }
    (void)close(ack[0]); (void)close(logfd); if (tracefd >= 0) (void)close(tracefd);
    uint64_t deadline = now_ns() + UINT64_C(30000000000); int status = 0; bool reaped = false;
    while (!atomic_load_explicit(&s->ready, memory_order_acquire) && now_ns() < deadline) {
        if (waitpid(child, &status, WNOHANG) == child) { reaped = true; break; }
        struct timespec pause = {0, 500000}; (void)nanosleep(&pause, NULL);
    }
    bool stopped = false;
    while (!reaped && atomic_load_explicit(&s->ready, memory_order_acquire) && now_ns() < deadline) {
        pid_t waited = waitpid(child, &status, WNOHANG | WUNTRACED);
        if (waited == child) { stopped = WIFSTOPPED(status); reaped = !stopped; break; }
        struct timespec pause = {0, 500000}; (void)nanosleep(&pause, NULL);
    }
    uint64_t text_kb = 0, rss_kb = 0;
    uint64_t map = atomic_load_explicit(&s->map_ns, memory_order_acquire), input = atomic_load_explicit(&s->input_ns, memory_order_acquire);
    uint64_t cpu = atomic_load_explicit(&s->cpu_ns, memory_order_acquire), start = atomic_load_explicit(&s->exec_ns, memory_order_acquire);
    uint64_t end = endpoint(map, input, cpu);
    bool valid = stopped && !reaped && atomic_load_explicit(&s->ready, memory_order_acquire) && !s->error && !s->advice_rc && end > start &&
        atomic_load_explicit(&s->jobs, memory_order_acquire) == 4 && s->text_pages > 0 && smaps(child, &text_kb, &rss_kb) == 0;
    if (valid) {
        *latency = end - start; long ps = sysconf(_SC_PAGESIZE);
        *text_pages = text_kb * 1024u / (uint64_t)ps; *rss = rss_kb;
        (void)fprintf(csv, "%s,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s,%.2f\n", variant, iteration,
            (unsigned long long)start, (unsigned long long)*latency, (unsigned long long)(map - start),
            (unsigned long long)(input - start), (unsigned long long)(cpu - start), (unsigned long long)*text_pages,
            (unsigned long long)s->resident_pages, (unsigned long long)s->text_pages, (unsigned long long)rss_kb, power, load);
        (void)fflush(csv);
    }
    if (!reaped) {
        if (valid) { (void)kill(child, SIGCONT); char value = 'x'; ssize_t sent; do { sent = write(ack[1], &value, 1); } while (sent < 0 && errno == EINTR); if (sent != 1) { valid = false; (void)kill(child, SIGKILL); } }
        else (void)kill(child, SIGKILL);
        (void)waitpid(child, &status, 0);
    }
    if (!valid || !WIFEXITED(status) || WEXITSTATUS(status))
        (void)fprintf(stderr, "linkorder_bench: FAIL variant=%s launch=%zu ready=%u error=%d jobs=%u status=%d; see %s\n",
            variant, iteration, atomic_load_explicit(&s->ready, memory_order_acquire), s->error, atomic_load_explicit(&s->jobs, memory_order_acquire), status, logpath);
    (void)close(ack[1]); (void)munmap(s, sizeof *s); (void)close(fd); cleanup(root);
    return valid && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0) return selftest();
    bool train = argc == 3 && strcmp(argv[1], "--train") == 0;
    bool run = argc == 1 || (argc == 3 && strcmp(argv[1], "--run") == 0);
    if (!train && !run) { fprintf(stderr, "usage: linkorder_bench --selftest | --train binary | --run launches-per-variant\n"); return 2; }
    char *tail = NULL; unsigned long count = run ? (argc == 1 ? 200 : strtoul(argv[2], &tail, 10)) : 1;
    if (run && ((argc != 1 && (!tail || *tail)) || count == 0 || count > 200)) return 2;
    uint64_t values[3][200], pages[3][200], rss[3][200];
    FILE *csv = fopen(train ? "variants/P3.6/b/training.csv" : "variants/P3.6/launches.csv", "w"); if (!csv) return 1;
    (void)fprintf(csv, "variant,launch,exec_ns,g4a_ns,map_ns,input_ns,cpu_ns,text_rss_pages,mincore_resident_pages,text_segment_pages,total_rss_kb,power,load1\n");
    for (size_t i = 0; i < count; i++) {
        for (size_t v = 0; v < (train ? 1u : 3u); v++) {
            const char *variant = train ? "b" : v == 0 ? "a" : v == 1 ? "b" : "c";
            if (launch(variant, i + 1, "/tmp/edit-corpus/ascii_code.c", train ? argv[2] : NULL, csv, &values[v][i], &pages[v][i], &rss[v][i])) { (void)fclose(csv); return 1; }
        }
        if ((i + 1) % 25 == 0) { (void)fprintf(stderr, "linkorder_bench: %zu interleaved A/B/C rounds complete\n", i + 1); }
    }
    if (fclose(csv)) return 1;
    if (!train) for (size_t v = 0; v < 3; v++) {
        char power[16]; double load; if (stamp(power, sizeof power, &load)) return 1;
        uint64_t p50 = percentile(values[v], count, 50), p99 = percentile(values[v], count, 99);
        printf("BENCH variant=%c row=G4a_warm p50=%.3f p99=%.3f unit=ms n=%lu gate_A=25/40(G) status=TRACK evidence=(M)[%s] load1=%.2f text_rss_pages_p50=%llu text_rss_pages_p99=%llu total_rss_kb_p50=%llu total_rss_kb_p99=%llu\n",
            (int)('a' + (int)v), (double)p50 / 1e6, (double)p99 / 1e6, count, power, load,
            (unsigned long long)percentile(pages[v], count, 50), (unsigned long long)percentile(pages[v], count, 99),
            (unsigned long long)percentile(rss[v], count, 50), (unsigned long long)percentile(rss[v], count, 99));
    }
    return 0;
}
