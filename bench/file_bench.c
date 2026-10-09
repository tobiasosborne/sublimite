/* file_bench.c -- P1.7: G5 (prefix publish), G8s (save ack), G8d (TRACK). */
#include "file/file.h"
#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"
#include "harness.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <time.h>

#define N 400
static work_pool pool;
static uint64_t buf[N];

static void ms_row(const char *name, const bench_samples *s, const char *gate)
{
    printf("  %s: p50=%.3f ms p99=%.3f ms (%s)\n", name,
           (double)bench_p50(s) / 1e6, (double)bench_p99(s) / 1e6, gate);
}

static void drain_cb(const work_msg *m, void *ud) { (void)m; (void)ud; }
static void drain(void) { (void)work_mailbox_drain(&pool, drain_cb, NULL); }

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static int open_row(const char *name, const char *path, uint64_t thr)
{
    bench_samples s; bench_samples_init(&s, buf, N);
    file_open_opts o = { thr, 0 };
    if (!exists(path)) { printf("SKIP %s (missing %s)\n", name, path); return 0; }
    { file *w; if (file_open_begin(&pool, path, &o, &w) == 0) file_close(w); }   /* warm */
    for (int i = 0; i < N; i++) {
        file *f = NULL;
        uint64_t t0 = bench_now_ns();
        int rc = file_open_begin(&pool, path, &o, &f);
        uint64_t t1 = bench_now_ns();
        if (rc != 0) { printf("open failed %d\n", rc); return 1; }
        size_t n; (void)file_prefix(f, &n);
        (void)bench_add(&s, t1 - t0);
        file_close(f);
        drain();
    }
    return bench_report(name, &s, 6000000ull, 9000000ull);
}

int main(void)
{
    char st[32]; int rc = 0;
    bench_battery_status(st, sizeof st);
    printf("power=%s (evidence %s); other workers may be loading this box: indicative only\n", st, bench_evidence_tag());
    trace_init();
    EDIT_ASSERT(work_pool_init(&pool, 1, 0) == 0);

    /* sparse 10 GiB fixture (no disk cost) */
    const char *sp = "/tmp/edit-corpus/sparse_10g.bin";
    if (!exists(sp)) {
        int fd = open(sp, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) { if (ftruncate(fd, 10ll << 30) != 0) perror("ftruncate"); close(fd); }
    }
    printf("G5 open -> bounded prefix published to the UI (viewport frame ~1.0 ms (E) added by later beads)\n");
    rc |= open_row("G5_open_prefix_sparse10g", sp, 0);
    rc |= open_row("G5_open_prefix_oneline1g", "/tmp/edit-corpus/oneline_1g.txt", 0);
    rc |= open_row("G5_open_prefix_log1g", "/tmp/edit-corpus/log_1g.txt", 0);
    rc |= open_row("G5_open_prefix_small_copy", "/tmp/edit-corpus/unicode.txt", 0);

    /* G8s: save ack on a lightly edited tree */
    {
        const char *src = "/tmp/edit-corpus/log_1g.txt";
        char tp[] = "/tmp/edit-bench-XXXXXX";
        if (mkdtemp(tp) == NULL) { perror("mkdtemp"); return 2; }
        char big[300]; snprintf(big, sizeof big, "%s/b.txt", tp);
        /* 1 MB file: first MiB of the corpus log, or generated */
        int fd = open(big, O_WRONLY | O_CREAT, 0644);
        static uint8_t blk[1 << 20];
        int sfd = exists(src) ? open(src, O_RDONLY) : -1;
        if (sfd >= 0) { if (pread(sfd, blk, sizeof blk, 0) < 0) perror("pread"); close(sfd); }
        else for (size_t i = 0; i < sizeof blk; i++) blk[i] = (i % 61 == 60) ? '\n' : (uint8_t)('a' + (i % 26));
        if (write(fd, blk, sizeof blk) < 0) perror("write");
        close(fd);

        file *f; file_open_opts o = {0, 0};
        EDIT_ASSERT(file_open_begin(&pool, big, &o, &f) == 0);
        piece_allocator a = piece_default_allocator();
        piece_tree *t = piece_create(&a);
        EDIT_ASSERT(file_attach(f, t) == 0);
        for (int i = 0; i < 20; i++) EDIT_ASSERT(piece_insert(t, (uint64_t)i * 997, (const uint8_t *)"xy", 2) == 0);

        bench_samples ack, comp;
        static uint64_t b2[64];
        bench_samples_init(&ack, buf, N);
        bench_samples_init(&comp, b2, 64);
        for (int i = 0; i < N; i++) {
            uint64_t t0 = bench_now_ns();
            int r = file_save_begin(f, t, 0, 0);
            uint64_t t1 = bench_now_ns();
            if (r != 0) { printf("save_begin %d\n", r); return 1; }
            (void)bench_add(&ack, t1 - t0);
            while (file_save_busy(f)) { struct timespec ts = { 0, 100000 }; nanosleep(&ts, NULL); }
            if (i < 64) (void)bench_add(&comp, bench_now_ns() - t0);
            drain();
        }
        rc |= bench_report("G8s_save_ack", &ack, 2000000ull, 5000000ull);
        printf("TRACK G8d_save_complete_1MB (gate 10/50 ms (G); disk state, other workers)\n");
        ms_row("G8d_1MB", &comp, "TRACK");
        piece_destroy(t); file_close(f);
        char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", tp); if (system(cmd)) {}
    }

    /* G8d 1 GB: TRACK, only if space allows */
    {
        struct statvfs sv;
        const char *src = "/tmp/edit-corpus/log_1g.txt";
        if (statvfs("/tmp", &sv) != 0 || (uint64_t)sv.f_bavail * sv.f_frsize < (3ull << 30) || !exists(src)) {
            printf("SKIP G8d_save_1GB (needs 3 GiB free in /tmp and %s)\n", src);
        } else {
            char tp[] = "/tmp/edit-bench-XXXXXX";
            if (mkdtemp(tp) == NULL) return 2;
            char dst[300], cmd[700]; snprintf(dst, sizeof dst, "%s/g.txt", tp);
            snprintf(cmd, sizeof cmd, "cp '%s' '%s'", src, dst); if (system(cmd)) return 2;
            file *f; file_open_opts o = {0, 0};
            EDIT_ASSERT(file_open_begin(&pool, dst, &o, &f) == 0);
            piece_allocator a = piece_default_allocator();
            piece_tree *t = piece_create(&a);
            file_msg m; (void)m;
            for (int w = 0; w < 600 && !file_open_ready(f); w++) { drain(); struct timespec ts = { 0, 10000000 }; nanosleep(&ts, NULL); }
            if (file_attach(f, t) != 0) { printf("SKIP G8d_save_1GB (attach)\n"); }
            else {
                uint64_t t0 = bench_now_ns();
                EDIT_ASSERT(file_save_begin(f, t, 0, 0) == 0);
                while (file_save_busy(f)) { struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); }
                printf("TRACK G8d_save_1GB: %.0f ms (gate 1500/2500 ms (G); kernel stub copy included)\n", (double)(bench_now_ns() - t0) / 1e6);
            }
            piece_destroy(t); file_close(f);
            snprintf(cmd, sizeof cmd, "rm -rf '%s'", tp); if (system(cmd)) {}
        }
    }
    work_pool_shutdown(&pool);
    return rc;
}
