/* L2 file bench (bead L2-1; ported from v1 bench/file_bench.c without
 * its layout/raster/x11 includes). Prints p50/p99 observations; exits 0.
 * Open is measured end to end (submit through tree build) in copy and
 * mapped mode; save through the synchronous core (write + sync + rename
 * + dirsync + verify), which is the worker's path. */
#include "file/file.h"
#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* (P) The smallest sample count with integral ranks at p50 and p99 and
 * a separate maximum. */
enum { SAMPLES = 100, CONTENT = 1 << 20 };

static _Alignas(64) work_pool pool;
static piece_reserve *reserve;

static void stamp(void)
{
    /* (P) Longest named Linux power status plus newline and terminator. */
    char power[sizeof "Not charging\n"] = "unknown";
    FILE *fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (fp) {
        if (fgets(power, sizeof power, fp)) power[strcspn(power, "\n")] = '\0';
        fclose(fp);
    }
    double load = 0;
    fp = fopen("/proc/loadavg", "r");
    bool have_load = fp && fscanf(fp, "%lf", &load) == 1;
    if (fp) fclose(fp);
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0 ||
        strcmp(power, "Not charging") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s ", tag, power);
    if (have_load) printf("load1=%.2f\n", load);
    else puts("load1=unknown");
}

static void report(const char *label, uint64_t times[SAMPLES])
{
    const uint64_t median = trace_fmt_pct(times, SAMPLES, 50);
    const uint64_t tail = trace_fmt_pct(times, SAMPLES, 99);
    printf("%s p50=%llu ns p99=%llu ns (n=%u)\n", label,
           (unsigned long long)median, (unsigned long long)tail, (unsigned)SAMPLES);
}

static void on_msg(const work_msg *m, void *ud)
{
    (void)ud;
    file_msg out;
    (void)file_msg_decode(m, &out);
}

static bool open_once(const char *path, uint64_t threshold)
{
    static file f;
    memset(&f, 0, sizeof f);
    file_open_opts opts = { .copy_threshold = threshold, .generation = 1 };
    if (file_open_begin(&f, &pool, path, &opts) != FILE_OK) return false;
    uint64_t deadline = os_now_ns() + UINT64_C(10000000000);
    while (!file_open_ready(&f)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        if (os_now_ns() >= deadline) return false;
        os_sleep_ns(UINT64_C(50000));
    }
    piece_tree *t;
    if (file_open_tree(&f, reserve, &t) != FILE_OK) return false;
    piece_close(t);
    file_close_begin(&f);
    while (!file_close_ready(&f)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        os_sleep_ns(UINT64_C(50000));
    }
    file_close_finish(&f);
    return true;
}

int main(void)
{
    stamp();
    (void)mkdir("build", 0700);
    (void)mkdir("build/file-bench", 0700);
    const char *path = "build/file-bench/f.txt";
    static uint8_t content[CONTENT];
    for (size_t i = 0; i < CONTENT; i++)
        content[i] = (uint8_t)('a' + (i / 64) % 26);
    for (size_t i = 63; i < CONTENT; i += 64) content[i] = '\n';
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || write(fd, content, CONTENT) != CONTENT || close(fd) != 0) {
        puts("file_bench p50=n/a p99=n/a (note: fixture unavailable)");
        return EXIT_SUCCESS;
    }
    if (work_pool_init(&pool, 1, 0) != 0 ||
        piece_reserve_create(&reserve) != PIECE_OK) {
        puts("file_bench p50=n/a p99=n/a (note: pool/reserve unavailable)");
        return EXIT_SUCCESS;
    }

    static uint64_t times[SAMPLES];
    for (int i = 0; i < SAMPLES; i++) {
        uint64_t t0 = os_now_ns();
        bool ok = open_once(path, 0);
        times[i] = os_now_ns() - t0;
        if (!ok) { puts("file_bench (note: copy open failed)"); return EXIT_SUCCESS; }
    }
    report("file_bench open_copy_1MiB", times);
    for (int i = 0; i < SAMPLES; i++) {
        uint64_t t0 = os_now_ns();
        bool ok = open_once(path, 1);
        times[i] = os_now_ns() - t0;
        if (!ok) { puts("file_bench (note: mapped open failed)"); return EXIT_SUCCESS; }
    }
    report("file_bench open_mmap_1MiB", times);

    piece_tree *t;
    if (piece_open_copy(reserve, content, CONTENT, &t) != PIECE_OK)
        return EXIT_SUCCESS;
    piece_snapshot *snap;
    if (piece_snapshot_take(t, &snap) != PIECE_OK) return EXIT_SUCCESS;
    for (int i = 0; i < SAMPLES; i++) {
        file_expectation exp;
        os_err oe;
        if (file_expectation_observe_path(path, &exp, &oe) != FILE_OK)
            return EXIT_SUCCESS;
        file_save_outcome out;
        uint64_t t0 = os_now_ns();
        int rc = file_save_write(path, snap, &exp, 0, NULL, &out);
        times[i] = os_now_ns() - t0;
        if (rc != FILE_OK) { puts("file_bench (note: save failed)"); break; }
        if (i == SAMPLES - 1) report("file_bench save_1MiB", times);
    }
    piece_snapshot_release(snap);
    piece_close(t);
    piece_reserve_destroy(reserve);
    work_pool_shutdown(&pool);
    return EXIT_SUCCESS;
}
