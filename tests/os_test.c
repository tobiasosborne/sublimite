/* os_test.c - conformance suite for src/os/os.h (edit-177r.2, W0.2, round 2).
 *
 * Uses only os.h, C11 and base.h so the same file builds as a Windows
 * executable in W0.3. Every case states the semantics both implementations
 * must share. Process cases re-execute this binary with a role argument
 * (--child=...) instead of fork, which Windows does not have.
 *
 * Run: build/tests/os_test (release) or build/san/tests/os_test (ASan/UBSan).
 * The driver runs every case in its own supervised child process
 * (--case=<name> <work dir>), so a hung case is killed and reported instead of
 * hanging the suite, and each case starts with a fresh process (the fault
 * case needs that: it races the service's first installation).
 * OS_TEST_CASE=<name> runs one case; an unknown name is an error.
 * OS_TEST_DATA_REPARSE=<path> names a Windows data file that carries reparse
 * metadata (W0.3 creates one with `compact /c /exe:lzx`); unset, that case
 * reports that it had no fixture.
 * Exit status 0 = every selected case ran and passed.
 *
 * Timeouts below are hang guards, not measurements or budgets: a case that
 * hits one has failed, and none bounds a timing the suite asserts. */
#include "os/os.h"
#include "base/base.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif
/* The same condition src/base/malloc_guard.c compiles its guard out under. */
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer) || \
    defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define OS_TEST_SANITIZER 1
#else
#define OS_TEST_SANITIZER 0
#endif

static _Atomic int failures;
static const char *current_case = "";

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            atomic_fetch_add(&failures, 1);                                   \
            fprintf(stderr, "%s:%d: [%s] FAIL %s\n", __FILE__, __LINE__,      \
                    current_case, #x);                                        \
        }                                                                     \
    } while (0)
/* An os call that must succeed; prints the error and native code if not. */
#define OK(call)                                                              \
    do {                                                                      \
        os_err ok_rc_ = (call);                                               \
        if (ok_rc_ != OS_OK) {                                                \
            atomic_fetch_add(&failures, 1);                                   \
            fprintf(stderr, "%s:%d: [%s] FAIL %s -> %s (native %d)\n",        \
                    __FILE__, __LINE__, current_case, #call,                  \
                    os_err_name(ok_rc_), os_native_error());                  \
        }                                                                     \
    } while (0)
#define EXPECT(call, want)                                                    \
    do {                                                                      \
        os_err ex_rc_ = (call);                                               \
        if (ex_rc_ != (want)) {                                               \
            atomic_fetch_add(&failures, 1);                                   \
            fprintf(stderr, "%s:%d: [%s] FAIL %s -> %s, want %s\n",           \
                    __FILE__, __LINE__, current_case, #call,                  \
                    os_err_name(ex_rc_), os_err_name(want));                  \
        }                                                                     \
    } while (0)

/* (E) Hang guard for one wait inside a case: process start and lock handoff
 * on this box take milliseconds (M: the proc case prints its spawn-to-exit
 * time); 10 s only stops a broken implementation from hanging the case. */
#define HANG_GUARD_NS (10ull * 1000000000ull)
/* (E) Hang guard for one whole case under the supervisor. The slowest case
 * measured 632 ms (M: the sanitizer build's fault case, which spawns a child
 * that crashes; the driver prints every case's time) [AC, desktop Spiral,
 * load1 about 12]. 120 s is about 190 times that, so load does not trip it
 * and only a hang does. */
#define CASE_GUARD_NS (120ull * 1000000000ull)
#define MS (1000000ull)

static char work_root[OS_PATH_MAX];   /* per-run scratch directory */

static void join(char *out, size_t cap, const char *a, const char *b)
{
    int n = snprintf(out, cap, "%s%c%s", a, OS_PATH_SEP, b);
    EDIT_ASSERT(n > 0 && (size_t)n < cap);
}

static bool ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && memcmp(s + a - b, tail, b) == 0;
}

/* Creates name in dir with exactly the given contents. */
static void put_at(const os_dir *dir, const char *name, const char *text)
{
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, dir, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0, n = strlen(text);
    if (n) OK(os_file_pwrite(&f, text, n, 0, &done));
    CHECK(done == n);
    os_file_close(&f);
}

static void put_path(const char *path, const void *bytes, size_t n)
{
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0;
    if (n) OK(os_file_pwrite(&f, bytes, n, 0, &done));
    CHECK(done == n);
    os_file_close(&f);
}

/* Whole-content read of an open handle into buf (NUL-terminated). */
static size_t slurp(os_file *f, char *buf, size_t cap)
{
    size_t total = 0, got = 0;
    do {
        got = 0;
        if (os_file_pread(f, buf + total, cap - 1 - total, total, &got) != OS_OK) break;
        total += got;
    } while (got && total < cap - 1);
    buf[total] = 0;
    return total;
}

static bool content_at(const os_dir *dir, const char *name, const char *want)
{
    os_file f = OS_FILE_INIT;
    if (os_file_open_at(&f, dir, name, OS_OPEN_READ) != OS_OK) return false;
    char buf[256];
    slurp(&f, buf, sizeof buf);
    os_file_close(&f);
    return strcmp(buf, want) == 0;
}

static bool content_path(const char *path, const char *want)
{
    os_file f = OS_FILE_INIT;
    if (os_file_open(&f, path, OS_OPEN_READ) != OS_OK) return false;
    char buf[256];
    slurp(&f, buf, sizeof buf);
    os_file_close(&f);
    return strcmp(buf, want) == 0;
}

/* A reusable barrier from the contract's own mutex and condition. */
typedef struct barrier {
    os_mutex mu;
    os_cond cv;
    int need, arrived, generation;
} barrier;
static void barrier_init(barrier *b, int need)
{
    memset(b, 0, sizeof *b);
    OK(os_mutex_init(&b->mu));
    OK(os_cond_init(&b->cv));
    b->need = need;
}
static void barrier_wait(barrier *b)
{
    os_mutex_lock(&b->mu);
    int gen = b->generation;
    if (++b->arrived == b->need) {
        b->arrived = 0;
        b->generation++;
        os_cond_broadcast(&b->cv);
    } else {
        while (gen == b->generation) os_cond_wait(&b->cv, &b->mu);
    }
    os_mutex_unlock(&b->mu);
}
static void barrier_destroy(barrier *b)
{
    os_cond_destroy(&b->cv);
    os_mutex_destroy(&b->mu);
}

/* ---- errors, limits ----------------------------------------------------- */
static void case_errors(void)
{
    CHECK(strcmp(os_err_name(OS_OK), "OS_OK") == 0);
    CHECK(strcmp(os_err_name(OS_E_NOENT), "OS_E_NOENT") == 0);
    CHECK(strcmp(os_err_name(OS_E_IO), "OS_E_IO") == 0);
    CHECK(strcmp(os_err_name(OS_E_INTR), "OS_E_INTR") == 0);
    CHECK(strcmp(os_err_name((os_err)999), "OS_E_?") == 0);
    char missing[OS_PATH_MAX];
    join(missing, sizeof missing, work_root, "no-such-file");
    os_file f = OS_FILE_INIT;
    CHECK(!os_file_is_open(&f));
    EXPECT(os_file_open(&f, missing, OS_OPEN_READ), OS_E_NOENT);
    CHECK(os_native_error() != 0);
    CHECK(!os_file_is_open(&f));
    os_file_close(&f); /* closing a handle that is not open is a no-op */
    os_stat st;
    EXPECT(os_stat_path(missing, 0, &st), OS_E_NOENT);
    /* A directory is not a file. */
    EXPECT(os_file_open(&f, work_root, OS_OPEN_READ), OS_E_ISDIR);
    /* Exactly one access mode. */
    EXPECT(os_file_open(&f, missing, OS_OPEN_READ | OS_OPEN_WRITE | OS_OPEN_CREATE), OS_E_INVAL);
    EXPECT(os_file_open(&f, missing, OS_OPEN_CREATE), OS_E_INVAL);
    os_dir d = OS_DIR_INIT;
    CHECK(!os_dir_is_open(&d));
    EXPECT(os_dir_open(&d, missing), OS_E_NOENT);
    /* Timestamps are signed: pre-epoch times survive (os.h). */
    st.mtime_ns = -1000000000;
    st.ctime_ns = -1;
    CHECK(st.mtime_ns < 0 && st.ctime_ns < 0);
}

/* ---- clocks ------------------------------------------------------------- */
/* Smallest nonzero step clock() is seen to move by within a spin of at least
 * span_ns of monotonic time (and at least 64 steps, or the hang guard). */
static uint64_t min_step(uint64_t (*clock)(void), uint64_t span_ns)
{
    uint64_t best = UINT64_MAX, prev = clock(), steps = 0;
    uint64_t start = os_now_ns(), guard = start + HANG_GUARD_NS;
    for (;;) {
        uint64_t now = os_now_ns();
        if (now > guard || (steps >= 64 && now - start >= span_ns)) break;
        uint64_t c = clock();
        if (c != prev) {
            CHECK(c > prev);
            if (c - prev < best) best = c - prev;
            prev = c;
            steps++;
        }
    }
    return best;
}

static void case_clocks(void)
{
    uint64_t a = os_now_ns(), b = os_now_ns();
    CHECK(a > 0 && b >= a);
    os_sleep_ns(2 * MS);
    uint64_t c = os_now_ns();
    CHECK(c - b >= 2 * MS);                 /* sleeps at least the request */
    /* (P) 2020-01-01T00:00:00Z = 1577836800 s since the Unix epoch. */
    CHECK(os_wall_ns() > 1577836800ull * 1000000000ull);

    /* Thread CPU time advances under work... */
    uint64_t cpu0 = os_thread_cpu_ns(), guard = os_now_ns() + HANG_GUARD_NS;
    volatile uint64_t spin = 0;
    while (os_thread_cpu_ns() == cpu0 && os_now_ns() < guard) spin++;
    CHECK(os_thread_cpu_ns() > cpu0);
    /* ...and not while sleeping. (P) 50 ms is above Windows' default
     * 15.625 ms scheduler tick (1/64 s), so a tick-quantised thread clock
     * cannot charge a whole sleep to the thread. */
    uint64_t sleep_ns = 50 * MS;
    uint64_t cpu1 = os_thread_cpu_ns(), w1 = os_now_ns();
    os_sleep_ns(sleep_ns);
    uint64_t cpu2 = os_thread_cpu_ns(), w2 = os_now_ns();
    CHECK(w2 - w1 >= sleep_ns);
    CHECK(cpu2 - cpu1 < sleep_ns);

    /* The reported CPU-clock resolution is never finer than what is seen.
     * (P) the spin spans 40 ms, more than two 15.625 ms Windows ticks. */
    uint64_t res = os_thread_cpu_resolution_ns();
    CHECK(res > 0);
    uint64_t cpu_step = min_step(os_thread_cpu_ns, 40 * MS);
    CHECK(cpu_step != UINT64_MAX && cpu_step >= res);

    /* The slice clock: resolves far below the smallest slice a module
     * enforces, (M) lineidx's UI_CPU_NS = 500000 ns (src/lineidx/lineidx.c:20),
     * and advances at least as much as the CPU clock over a busy interval
     * (slack: one CPU-clock step, for a tick-quantised CPU clock). */
    uint64_t slice_step = min_step(os_slice_ns, 2 * MS);
    CHECK(slice_step != UINT64_MAX && slice_step < 500000ull);
    uint64_t s0 = os_slice_ns(), c0 = os_thread_cpu_ns(), end = os_now_ns() + 20 * MS;
    while (os_now_ns() < end) spin++;
    uint64_t c1 = os_thread_cpu_ns(), s1 = os_slice_ns();
    CHECK(s1 >= s0 && c1 >= c0);
    CHECK(s1 - s0 + res >= c1 - c0);
    printf("clocks: cpu resolution %llu ns (reported), smallest cpu step %llu ns, "
           "smallest slice step %llu ns (M)\n", (unsigned long long)res,
           (unsigned long long)cpu_step, (unsigned long long)slice_step);
    os_yield();
}

/* ---- threads, mutex, cond, once ---------------------------------------- */
typedef struct counter_ctx {
    os_mutex mu;
    os_rmutex rmu;
    barrier ids;
    uint64_t value, rvalue;
    uint64_t self[4];
    _Atomic int next;
} counter_ctx;

static void count_body(void *arg)
{
    counter_ctx *c = arg;
    int me = atomic_fetch_add(&c->next, 1);
    c->self[me] = os_thread_self();
    /* Stay alive until every worker's ID is recorded and compared: IDs are
     * unique only among live threads. */
    barrier_wait(&c->ids);
    barrier_wait(&c->ids);
    for (int i = 0; i < 10000; i++) {
        os_mutex_lock(&c->mu);
        c->value++;
        os_mutex_unlock(&c->mu);
        os_rmutex_lock(&c->rmu);
        os_rmutex_lock(&c->rmu);                 /* nested acquisition */
        c->rvalue++;
        os_rmutex_unlock(&c->rmu);
        os_rmutex_unlock(&c->rmu);
    }
}

typedef struct handshake {
    os_mutex mu;
    os_cond cv;
    int stage;
} handshake;

static void handshake_body(void *arg)
{
    handshake *h = arg;
    os_mutex_lock(&h->mu);
    while (h->stage != 1) os_cond_wait(&h->cv, &h->mu);
    h->stage = 2;
    os_cond_broadcast(&h->cv);
    os_mutex_unlock(&h->mu);
}

typedef struct rheld { os_rmutex *m; _Atomic int got; } rheld;
static void rheld_body(void *arg)
{
    rheld *r = arg;
    os_rmutex_lock(r->m);
    atomic_store(&r->got, 1);
    os_rmutex_unlock(r->m);
}

static os_once once_flag = OS_ONCE_INIT;
static _Atomic int once_runs;
static void once_body(void) { atomic_fetch_add(&once_runs, 1); os_sleep_ns(MS); }
static void once_thread(void *arg) { (void)arg; os_once_run(&once_flag, once_body); }

static os_mutex static_mutex = OS_MUTEX_INIT;

static void case_threads(void)
{
    counter_ctx c;
    memset(&c, 0, sizeof c);
    OK(os_mutex_init(&c.mu));
    OK(os_rmutex_init(&c.rmu));
    barrier_init(&c.ids, 5);
    os_thread t[4];
    for (int i = 0; i < 4; i++) OK(os_thread_start(&t[i], count_body, &c));
    barrier_wait(&c.ids);                        /* all four IDs recorded */
    uint64_t me = os_thread_self();
    CHECK(me != 0);
    for (int i = 0; i < 4; i++) {
        CHECK(c.self[i] != 0 && c.self[i] != me);
        for (int j = 0; j < i; j++) CHECK(c.self[i] != c.self[j]);
    }
    barrier_wait(&c.ids);                        /* release them */
    for (int i = 0; i < 4; i++) OK(os_thread_join(&t[i]));
    CHECK(c.value == 40000);
    CHECK(c.rvalue == 40000);
    barrier_destroy(&c.ids);
    os_mutex_destroy(&c.mu);

    /* A recursive mutex is released only by the last unlock. */
    rheld r = {&c.rmu, 0};
    os_rmutex_lock(&c.rmu);
    os_rmutex_lock(&c.rmu);
    os_rmutex_lock(&c.rmu);
    os_thread rt;
    OK(os_thread_start(&rt, rheld_body, &r));
    os_rmutex_unlock(&c.rmu);
    os_rmutex_unlock(&c.rmu);
    os_sleep_ns(10 * MS);
    CHECK(atomic_load(&r.got) == 0);             /* still held once */
    os_rmutex_unlock(&c.rmu);
    OK(os_thread_join(&rt));
    CHECK(atomic_load(&r.got) == 1);
    os_rmutex_destroy(&c.rmu);

    handshake h;
    memset(&h, 0, sizeof h);
    OK(os_mutex_init(&h.mu));
    OK(os_cond_init(&h.cv));
    os_thread w;
    OK(os_thread_start(&w, handshake_body, &h));
    os_mutex_lock(&h.mu);
    h.stage = 1;
    os_cond_signal(&h.cv);
    while (h.stage != 2) os_cond_wait(&h.cv, &h.mu);
    os_mutex_unlock(&h.mu);
    OK(os_thread_join(&w));
    os_cond_destroy(&h.cv);
    os_mutex_destroy(&h.mu);

    os_thread o[4];
    for (int i = 0; i < 4; i++) OK(os_thread_start(&o[i], once_thread, NULL));
    os_once_run(&once_flag, once_body);
    for (int i = 0; i < 4; i++) OK(os_thread_join(&o[i]));
    CHECK(atomic_load(&once_runs) == 1);

    os_mutex_lock(&static_mutex);   /* static initialiser works */
    os_mutex_unlock(&static_mutex);
}

/* ---- wake handles ------------------------------------------------------- */
typedef struct wake_ctx { os_wake *w; uint64_t delay; } wake_ctx;
static void wake_later(void *arg)
{
    wake_ctx *c = arg;
    os_sleep_ns(c->delay);
    os_wake_signal(c->w);
}

static void case_wake(void)
{
    os_wake w;
    OK(os_wake_init(&w));
    EXPECT(os_wake_wait(&w, 0), OS_E_TIMEDOUT);           /* starts clear */
    uint64_t t0 = os_now_ns();
    EXPECT(os_wake_wait(&w, (int64_t)(5 * MS)), OS_E_TIMEDOUT);
    CHECK(os_now_ns() - t0 >= 5 * MS);                    /* waited the timeout */
    os_wake_signal(&w);
    os_wake_signal(&w);                                   /* coalesces */
    OK(os_wake_wait(&w, 0));
    OK(os_wake_wait(&w, 0));                              /* wait does not drain */
    os_wake_drain(&w);
    EXPECT(os_wake_wait(&w, 0), OS_E_TIMEDOUT);           /* one drain clears all */
    os_wake_drain(&w);                                    /* drain when clear: no-op */
    EXPECT(os_wake_wait(&w, 0), OS_E_TIMEDOUT);
    /* Another thread wakes a blocked waiter. */
    wake_ctx c = {&w, 2 * MS};
    os_thread t;
    OK(os_thread_start(&t, wake_later, &c));
    OK(os_wake_wait(&w, -1));
    OK(os_thread_join(&t));
    os_wake_drain(&w);
    os_wake_close(&w);
}

/* ---- no allocation on calls the typing path uses (law 2) --------------- */
typedef struct contend_ctx {
    os_mutex *m;
    os_rmutex *rm;
    os_cond *cv;
    _Atomic int stop;
    _Atomic uint64_t rounds;
    int ticks;
} contend_ctx;
/* Hammers the locks the guarded loop also takes, so it runs contended. */
static void contend_body(void *arg)
{
    contend_ctx *c = arg;
    while (!atomic_load(&c->stop)) {
        os_mutex_lock(c->m);
        os_mutex_unlock(c->m);
        os_rmutex_lock(c->rm);
        os_rmutex_unlock(c->rm);
        atomic_fetch_add(&c->rounds, 1);
    }
}
/* Waits on the condition so signal/broadcast have a waiter to wake. */
static void waiter_body(void *arg)
{
    contend_ctx *c = arg;
    os_mutex_lock(c->m);
    while (!atomic_load(&c->stop)) os_cond_wait(c->cv, c->m);
    os_mutex_unlock(c->m);
}

static void case_noalloc(void)
{
    /* In a release build the counting guard must be live; a missing guard is
     * a failure, not a skip (a Windows release build needs one too). */
    if (!OS_TEST_SANITIZER) CHECK(edit_malloc_guard_active());
    if (edit_malloc_guard_active()) {
        /* Positive control: the guard sees an allocation. */
        void *(*volatile alloc)(size_t) = malloc;
        edit_malloc_guard_begin();
        void *p = alloc(32);
        size_t seen = edit_malloc_guard_end();
        free(p);
        CHECK(seen >= 1);
    } else if (OS_TEST_SANITIZER) {
        puts("noalloc: sanitizer build, allocator guard compiled out; counts not checked");
    }

    os_wake w;
    OK(os_wake_init(&w));
    os_mutex m;
    OK(os_mutex_init(&m));
    os_rmutex rm;
    OK(os_rmutex_init(&rm));
    os_cond cv;
    OK(os_cond_init(&cv));
    os_mutex cold_m;
    OK(os_mutex_init(&cold_m));
    os_rmutex cold_rm;
    OK(os_rmutex_init(&cold_rm));
    os_cond cold_cv;
    OK(os_cond_init(&cold_cv));
    char path[OS_PATH_MAX];
    join(path, sizeof path, work_root, "noalloc.bin");
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    contend_ctx cc = {&m, &rm, &cv, 0, 0, 0};
    os_thread contender, waiter;
    OK(os_thread_start(&contender, contend_body, &cc));
    OK(os_thread_start(&waiter, waiter_body, &cc));
    while (atomic_load(&cc.rounds) == 0) os_yield();  /* contender running */

    char rec[64];
    memset(rec, 'r', sizeof rec);
    edit_malloc_guard_begin();
    /* Cold: first use of objects initialised outside the guard. */
    os_mutex_lock(&cold_m);
    os_mutex_unlock(&cold_m);
    os_rmutex_lock(&cold_rm);
    os_rmutex_lock(&cold_rm);
    os_rmutex_unlock(&cold_rm);
    os_rmutex_unlock(&cold_rm);
    os_cond_signal(&cold_cv);
    os_cond_broadcast(&cold_cv);
    uint64_t sum = 0;
    size_t done = 0, got = 0;
    bool io_ok = true;
    for (int i = 0; i < 1000; i++) {
        sum += os_now_ns() + os_thread_cpu_ns() + os_slice_ns() + os_thread_self();
        os_mutex_lock(&m);                 /* contended */
        os_cond_signal(&cv);               /* a waiter is (often) parked */
        os_mutex_unlock(&m);
        os_cond_broadcast(&cv);
        os_rmutex_lock(&rm);
        os_rmutex_lock(&rm);
        os_rmutex_unlock(&rm);
        os_rmutex_unlock(&rm);
        os_wake_signal(&w);
        os_wake_drain(&w);
        uint64_t off = (uint64_t)i * sizeof rec;
        io_ok &= os_file_pwrite_once(&f, rec, sizeof rec, off, &done) == OS_OK && done == sizeof rec;
        io_ok &= os_file_pwrite(&f, rec, sizeof rec, off, &done) == OS_OK && done == sizeof rec;
        io_ok &= os_file_pread(&f, rec, sizeof rec, off, &got) == OS_OK && got == sizeof rec;
    }
    os_yield();
    size_t mallocs = edit_malloc_guard_end();
    atomic_store(&cc.stop, 1);
    os_mutex_lock(&m);
    os_cond_broadcast(&cv);
    os_mutex_unlock(&m);
    OK(os_thread_join(&contender));
    OK(os_thread_join(&waiter));
    CHECK(sum != 0);
    CHECK(io_ok);
    if (edit_malloc_guard_active()) CHECK(mallocs == 0);
    os_file_close(&f);
    OK(os_unlink(path));
    os_cond_destroy(&cold_cv);
    os_rmutex_destroy(&cold_rm);
    os_mutex_destroy(&cold_m);
    os_cond_destroy(&cv);
    os_rmutex_destroy(&rm);
    os_mutex_destroy(&m);
    os_wake_close(&w);
}

/* ---- file handles ------------------------------------------------------- */
typedef struct pio_ctx { os_file *f; int id; bool ok; } pio_ctx;
/* Each thread owns one 4 KiB block of a shared handle and checks it reads
 * back exactly what it wrote while the others write theirs. */
static void pio_body(void *arg)
{
    pio_ctx *c = arg;
    unsigned char out[4096], in[4096];
    c->ok = true;
    for (int round = 0; round < 64; round++) {
        memset(out, (int)(c->id * 64 + round) & 0xff, sizeof out);
        uint64_t off = (uint64_t)c->id * sizeof out;
        size_t done = 0, got = 0;
        if (os_file_pwrite(c->f, out, sizeof out, off, &done) != OS_OK || done != sizeof out) c->ok = false;
        if (os_file_pread(c->f, in, sizeof in, off, &got) != OS_OK || got != sizeof in) c->ok = false;
        if (memcmp(in, out, sizeof in) != 0) c->ok = false;
    }
}

static void case_files(void)
{
    char path[OS_PATH_MAX];
    join(path, sizeof path, work_root, "files.bin");
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    CHECK(os_file_is_open(&f));
    os_file g = OS_FILE_INIT;
    EXPECT(os_file_open(&g, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL), OS_E_EXIST);

    size_t done = 0, got = 0;
    OK(os_file_write(&f, "hello ", 6, &done));
    CHECK(done == 6);
    OK(os_file_write(&f, "world", 5, &done));
    CHECK(done == 5);
    OK(os_file_sync_data(&f));
    OK(os_file_sync(&f));
    os_stat st;
    OK(os_file_stat(&f, &st));
    CHECK(st.kind == OS_KIND_FILE && st.size == 11);
    /* (P) 2020-01-01T00:00:00Z = 1577836800 s since the Unix epoch. */
    CHECK(st.mtime_ns > (int64_t)1577836800 * 1000000000);
    OK(os_file_pwrite(&f, "W", 1, 6, &done));
    char buf[64] = {0};
    OK(os_file_pread(&f, buf, sizeof buf, 0, &got));
    CHECK(got == 11 && memcmp(buf, "hello World", 11) == 0);
    OK(os_file_pread(&f, buf, sizeof buf, 11, &got));
    CHECK(got == 0);                                     /* end of file */
    /* The single-attempt write: one call, whole record here. */
    OK(os_file_pwrite_once(&f, "w", 1, 6, &done));
    CHECK(done == 1);
    OK(os_file_pread(&f, buf, 11, 0, &got));
    CHECK(got == 11 && memcmp(buf, "hello world", 11) == 0);
    OK(os_file_set_size(&f, 16));                        /* grow reads zeros */
    OK(os_file_pread(&f, buf, sizeof buf, 0, &got));
    CHECK(got == 16 && buf[11] == 0 && buf[15] == 0);
    OK(os_file_set_size(&f, 5));
    OK(os_file_stat(&f, &st));
    CHECK(st.size == 5);
    OK(os_file_set_mode(&f, 0600));
    OK(os_file_stat(&f, &st));
    CHECK((st.perm & 0200) != 0);

    /* Sequential read from a fresh handle starts at 0. */
    OK(os_file_open(&g, path, OS_OPEN_READ));
    memset(buf, 0, sizeof buf);
    OK(os_file_read(&g, buf, 3, &got));
    CHECK(got == 3 && memcmp(buf, "hel", 3) == 0);
    OK(os_file_read(&g, buf, sizeof buf, &got));
    CHECK(got == 2 && memcmp(buf, "lo", 2) == 0);
    OK(os_file_read(&g, buf, sizeof buf, &got));
    CHECK(got == 0);
    /* Same file, same identity, through handle and path. */
    os_stat a, b;
    OK(os_file_stat(&f, &a));
    OK(os_stat_path(path, 0, &b));
    CHECK(os_identity_equal(&a.id, &b.id));
    os_file_close(&g);

    /* A duplicate reads the same file. */
    os_file d = OS_FILE_INIT;
    OK(os_file_dup(&d, &f));
    OK(os_file_pread(&d, buf, sizeof buf, 0, &got));
    CHECK(got == 5 && memcmp(buf, "hello", 5) == 0);
    os_file_close(&d);
    CHECK(!os_file_is_open(&d));

    /* Clone is a whole-file copy where supported. */
    char clone_path[OS_PATH_MAX];
    join(clone_path, sizeof clone_path, work_root, "files.clone");
    os_file c = OS_FILE_INIT;
    OK(os_file_open(&c, clone_path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    os_err cl = os_file_clone(&c, &f);
    CHECK(cl == OS_OK || cl == OS_E_NOTSUP);
    if (cl == OS_OK) {
        OK(os_file_pread(&c, buf, sizeof buf, 0, &got));
        CHECK(got == 5 && memcmp(buf, "hello", 5) == 0);
    }
    printf("files: clone %s\n", os_err_name(cl));
    os_file_close(&c);
    os_file_close(&f);
    OK(os_unlink(clone_path));

    /* Write-only open: writes work, reads through it fail, and it needs no
     * read permission (where the platform has a read bit and we are not
     * the superuser). */
    os_file wo = OS_FILE_INIT;
    OK(os_file_open(&wo, path, OS_OPEN_WRITE));
    OK(os_file_pwrite(&wo, "J", 1, 0, &done));
    CHECK(os_file_read(&wo, buf, sizeof buf, &got) != OS_OK);
    OK(os_file_set_mode(&wo, 0200));
    OK(os_file_stat(&wo, &st));
    os_file_close(&wo);
    OK(os_file_open(&wo, path, OS_OPEN_WRITE));
    os_file_close(&wo);
    if ((st.perm & 0400) == 0 && st.uid != 0) {
        os_file ro = OS_FILE_INIT;
        EXPECT(os_file_open(&ro, path, OS_OPEN_READ), OS_E_ACCESS);
    }
    OK(os_file_open(&wo, path, OS_OPEN_WRITE));
    OK(os_file_set_mode(&wo, 0600));
    os_file_close(&wo);
    CHECK(content_path(path, "Jello"));

    /* Truncating open: an existing file is empty as soon as the open returns. */
    OK(os_file_open(&wo, path, OS_OPEN_WRITE | OS_OPEN_TRUNC));
    OK(os_file_stat(&wo, &st));
    CHECK(st.size == 0);
    OK(os_file_write(&wo, "new", 3, &done));
    os_file_close(&wo);
    CHECK(content_path(path, "new"));

    /* stdio bridge (the trace-dump path): truncate-create, hand to FILE. */
    OK(os_file_open(&wo, path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC));
    FILE *fp = NULL;
    OK(os_file_to_stdio(&wo, "wb", &fp));
    CHECK(fp != NULL && !os_file_is_open(&wo));
    if (fp) {
        CHECK(fputs("via stdio", fp) >= 0);
        CHECK(fclose(fp) == 0);
    }
    CHECK(content_path(path, "via stdio"));
    OK(os_unlink(path));
    EXPECT(os_unlink(path), OS_E_NOENT);

    /* Simultaneous positioned I/O on one handle from four threads. */
    join(path, sizeof path, work_root, "pio.bin");
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    pio_ctx pc[4];
    os_thread pt[4];
    for (int i = 0; i < 4; i++) {
        pc[i] = (pio_ctx){&f, i, false};
        OK(os_thread_start(&pt[i], pio_body, &pc[i]));
    }
    for (int i = 0; i < 4; i++) {
        OK(os_thread_join(&pt[i]));
        CHECK(pc[i].ok);
    }
    os_file_close(&f);
    OK(os_unlink(path));

    /* Offsets beyond 4 GiB on a sparse file are exact, and a mapping can
     * start there. (P) 2^32 is a multiple of every map granularity (4 KiB
     * pages, Windows' 64 KiB allocation granularity). */
    join(path, sizeof path, work_root, "sparse.bin");
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    os_err sp = os_file_set_sparse(&f);
    CHECK(sp == OS_OK || sp == OS_E_NOTSUP);
    printf("files: sparse %s\n", os_err_name(sp));
    if (sp == OS_OK) {
        const uint64_t far = 1ull << 32;
        OK(os_file_pwrite(&f, "low", 3, 0, &done));
        OK(os_file_pwrite(&f, "far", 3, far + 3, &done));
        CHECK(done == 3);
        OK(os_file_stat(&f, &st));
        CHECK(st.size == far + 6);
        memset(buf, 'x', sizeof buf);
        OK(os_file_pread(&f, buf, 6, far, &got));
        CHECK(got == 6 && memcmp(buf, "\0\0\0far", 6) == 0);
        OK(os_file_pread(&f, buf, 3, 0, &got));      /* no 32-bit wrap onto 0 */
        CHECK(got == 3 && memcmp(buf, "low", 3) == 0);
        os_map m = {0};
        OK(os_map_file(&m, &f, far, 6));
        if (m.addr) {
            const volatile char *v = m.addr;
            CHECK(v[0] == 0 && v[3] == 'f' && v[5] == 'r');
            os_unmap(&m);
        }
    }
    os_file_close(&f);
    OK(os_unlink(path));
}

/* ---- directory-relative operations ------------------------------------- */
static void case_dirs(void)
{
    char root[OS_PATH_MAX], sub_path[OS_PATH_MAX];
    join(root, sizeof root, work_root, "dirs");
    OK(os_mkdir(root));
    EXPECT(os_mkdir(root), OS_E_EXIST);
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, root));
    CHECK(os_dir_is_open(&d));
    OK(os_mkdir_at(&d, "sub"));
    EXPECT(os_mkdir_at(&d, "sub"), OS_E_EXIST);
    os_dir sub = OS_DIR_INIT;
    OK(os_dir_open_at(&sub, &d, "sub"));
    os_dir self = OS_DIR_INIT;
    OK(os_dir_open_at(&self, &d, "."));                  /* "." is the directory */
    os_stat s1, s2;
    OK(os_stat_at(&d, ".", 0, &s1));
    OK(os_stat_path(root, 0, &s2));
    CHECK(s1.kind == OS_KIND_DIR && os_identity_equal(&s1.id, &s2.id));
    os_dir_close(&self);

    put_at(&d, "a", "AAAA");
    put_at(&d, "b", "BB");

    /* NOREPLACE onto an existing name fails and touches neither. */
    EXPECT(os_rename_at(&d, "a", &d, "b", OS_RENAME_NOREPLACE), OS_E_EXIST);
    CHECK(content_at(&d, "a", "AAAA") && content_at(&d, "b", "BB"));
    char pa[OS_PATH_MAX], pb[OS_PATH_MAX];
    join(pa, sizeof pa, root, "a");
    join(pb, sizeof pb, root, "b");
    EXPECT(os_rename(pa, pb, OS_RENAME_NOREPLACE), OS_E_EXIST);
    CHECK(content_at(&d, "a", "AAAA") && content_at(&d, "b", "BB"));

    /* REPLACE over a target another handle holds open: the rename succeeds,
     * the open handle keeps the replaced bytes, the name has the new ones. */
    os_file held = OS_FILE_INIT;
    OK(os_file_open_at(&held, &d, "b", OS_OPEN_READ));
    os_stat held_before, a_before;
    OK(os_file_stat(&held, &held_before));
    OK(os_stat_at(&d, "a", OS_STAT_NOFOLLOW, &a_before));
    OK(os_rename_at(&d, "a", &d, "b", OS_RENAME_REPLACE));
    char buf[64];
    slurp(&held, buf, sizeof buf);
    CHECK(strcmp(buf, "BB") == 0);
    CHECK(content_at(&d, "b", "AAAA"));
    os_stat st;
    EXPECT(os_stat_at(&d, "a", OS_STAT_NOFOLLOW, &st), OS_E_NOENT);
    OK(os_stat_at(&d, "b", OS_STAT_NOFOLLOW, &st));
    CHECK(os_identity_equal(&st.id, &a_before.id));
    CHECK(!os_identity_equal(&st.id, &held_before.id));
    os_file_close(&held);

    /* REPLACE over a READ-ONLY (0444) target that is open and mapped:
     * succeeds as on Linux, where only the directory's permissions count. */
    put_at(&d, "ro", "old");
    put_at(&d, "ro-new", "new");
    os_file rof = OS_FILE_INIT;
    OK(os_file_open_at(&rof, &d, "ro", OS_OPEN_RDWR));
    os_map rom = {0};
    OK(os_map_file(&rom, &rof, 0, 3));
    OK(os_file_set_mode(&rof, 0444));
    OK(os_file_stat(&rof, &st));
    CHECK((st.perm & 0222) == 0);
    OK(os_rename_at(&d, "ro-new", &d, "ro", OS_RENAME_REPLACE));
    CHECK(content_at(&d, "ro", "new"));
    slurp(&rof, buf, sizeof buf);
    CHECK(strcmp(buf, "old") == 0);
    if (rom.addr) CHECK(memcmp(rom.addr, "old", 3) == 0);
    os_unmap(&rom);
    os_file_close(&rof);
    /* Unlink of a read-only file works too. */
    os_file roh = OS_FILE_INIT;
    OK(os_file_open_at(&roh, &d, "ro", OS_OPEN_READ));
    OK(os_file_set_mode(&roh, 0444));
    os_file_close(&roh);
    OK(os_unlink_at(&d, "ro"));
    EXPECT(os_stat_at(&d, "ro", 0, &st), OS_E_NOENT);

    /* NOREPLACE onto a free name works; across directories too. */
    OK(os_rename_at(&d, "b", &sub, "moved", OS_RENAME_NOREPLACE));
    CHECK(content_at(&sub, "moved", "AAAA"));
    EXPECT(os_stat_at(&d, "b", 0, &st), OS_E_NOENT);
    join(sub_path, sizeof sub_path, root, "sub");
    char moved[OS_PATH_MAX], back[OS_PATH_MAX];
    join(moved, sizeof moved, sub_path, "moved");
    join(back, sizeof back, root, "back");
    OK(os_rename(moved, back, OS_RENAME_REPLACE));
    CHECK(content_at(&d, "back", "AAAA"));

    /* Unlink of an open file: the name goes at once (and is reusable), the
     * open handle still reads the data. */
    put_at(&d, "victim", "still here");
    os_file v = OS_FILE_INIT;
    OK(os_file_open_at(&v, &d, "victim", OS_OPEN_READ));
    OK(os_unlink_at(&d, "victim"));
    EXPECT(os_stat_at(&d, "victim", OS_STAT_NOFOLLOW, &st), OS_E_NOENT);
    EXPECT(os_unlink_at(&d, "victim"), OS_E_NOENT);
    slurp(&v, buf, sizeof buf);
    CHECK(strcmp(buf, "still here") == 0);
    put_at(&d, "victim", "new");                          /* name reusable at once */
    CHECK(content_at(&d, "victim", "new"));
    slurp(&v, buf, sizeof buf);
    CHECK(strcmp(buf, "still here") == 0);
    os_file_close(&v);

    /* *_at stays pinned to the directory object, not its path: rename the
     * directory away and the handle still reaches its entries. */
    char moved_root[OS_PATH_MAX];
    join(moved_root, sizeof moved_root, work_root, "dirs-moved");
    os_dir_close(&sub);
    OK(os_rename(root, moved_root, OS_RENAME_NOREPLACE));
    CHECK(content_at(&d, "back", "AAAA"));
    put_at(&d, "after-move", "x");
    char probe[OS_PATH_MAX];
    join(probe, sizeof probe, moved_root, "after-move");
    OK(os_stat_path(probe, 0, &st));
    OK(os_rename(moved_root, root, OS_RENAME_NOREPLACE));

    OK(os_dir_sync(&d));    /* Windows: documented no-op, still OS_OK */

    /* Anonymous file: usable, and its name is gone before the call returns. */
    os_file anon = OS_FILE_INIT;
    os_err an = os_file_open_anon(&anon, &d);
    CHECK(an == OS_OK || an == OS_E_NOTSUP);
    printf("dirs: anon %s\n", os_err_name(an));
    if (an == OS_OK) {
        os_dir_iter it;
        OK(os_dir_iter_open(&it, &d));
        const char *name;
        int n = 0;
        while (os_dir_next(&it, &name) == OS_OK && name) n++;
        os_dir_iter_close(&it);
        CHECK(n == 4);  /* sub, back, victim, after-move */
        size_t done = 0, got = 0;
        OK(os_file_pwrite(&anon, "tmp", 3, 0, &done));
        OK(os_file_pread(&anon, buf, sizeof buf, 0, &got));
        CHECK(got == 3 && memcmp(buf, "tmp", 3) == 0);
        os_file_close(&anon);
    }

    OK(os_unlink_at(&d, "back"));
    OK(os_unlink_at(&d, "victim"));
    OK(os_unlink_at(&d, "after-move"));
    EXPECT(os_rmdir(root), OS_E_EXIST);                  /* not empty */
    OK(os_rmdir(sub_path));
    os_dir_close(&d);
    OK(os_rmdir(root));
}

/* ---- directory listing -------------------------------------------------- */
static int list_names(os_dir_iter *it, const char *const *want, int nwant, int *seen)
{
    const char *name;
    int n = 0;
    for (int i = 0; i < nwant; i++) seen[i] = 0;
    for (;;) {
        os_err rc = os_dir_next(it, &name);
        CHECK(rc == OS_OK);
        if (rc != OS_OK || !name) break;
        CHECK(strcmp(name, ".") != 0 && strcmp(name, "..") != 0);
        for (int i = 0; i < nwant; i++)
            if (strcmp(name, want[i]) == 0) seen[i]++;
        n++;
    }
    return n;
}

static void case_listing(void)
{
    char root[OS_PATH_MAX];
    join(root, sizeof root, work_root, "list");
    OK(os_mkdir(root));
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, root));
    const char *const want[] = {"one", "two", "three", "dir"};
    put_at(&d, "one", "1");
    put_at(&d, "two", "2");
    put_at(&d, "three", "3");
    OK(os_mkdir_at(&d, "dir"));
    /* A held directory lock adds no entry. */
    OK(os_dir_try_lock(&d, OS_LOCK_EXCLUSIVE));
    os_dir_iter it;
    OK(os_dir_iter_open(&it, &d));
    int seen[4];
    CHECK(list_names(&it, want, 4, seen) == 4);
    for (int i = 0; i < 4; i++) CHECK(seen[i] == 1);
    /* The directory handle stays usable while iterating. */
    os_stat st;
    OK(os_stat_at(&d, "one", 0, &st));
    os_dir_rewind(&it);
    CHECK(list_names(&it, want, 4, seen) == 4);
    for (int i = 0; i < 4; i++) CHECK(seen[i] == 1);
    os_dir_iter_close(&it);
    os_dir_unlock(&d);
    OK(os_unlink_at(&d, "one"));
    OK(os_unlink_at(&d, "two"));
    OK(os_unlink_at(&d, "three"));
    char sub[OS_PATH_MAX];
    join(sub, sizeof sub, root, "dir");
    OK(os_rmdir(sub));
    OK(os_dir_iter_open(&it, &d));
    CHECK(list_names(&it, want, 4, seen) == 0);
    os_dir_iter_close(&it);
    os_dir_close(&d);
    OK(os_rmdir(root));
}

/* ---- locks -------------------------------------------------------------- */
static void case_locks(void)
{
    char path[OS_PATH_MAX];
    join(path, sizeof path, work_root, "lock.bin");
    os_file a = OS_FILE_INIT, b = OS_FILE_INIT;
    OK(os_file_open(&a, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0, got = 0;
    OK(os_file_pwrite(&a, "locked data", 11, 0, &done));
    OK(os_file_open(&b, path, OS_OPEN_RDWR));

    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    EXPECT(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    EXPECT(os_file_try_lock(&b, OS_LOCK_SHARED), OS_E_BUSY);
    /* Advisory: the other handle still reads and writes the data. */
    char buf[32] = {0};
    OK(os_file_pread(&b, buf, sizeof buf, 0, &got));
    CHECK(got == 11 && memcmp(buf, "locked data", 11) == 0);
    OK(os_file_pwrite(&b, "L", 1, 0, &done));
    os_file_unlock(&a);
    OK(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE));
    EXPECT(os_file_try_lock(&a, OS_LOCK_SHARED), OS_E_BUSY);
    os_file_unlock(&b);
    /* Shared locks coexist and exclude an exclusive one. */
    OK(os_file_try_lock(&a, OS_LOCK_SHARED));
    OK(os_file_try_lock(&b, OS_LOCK_SHARED));
    os_file c = OS_FILE_INIT;
    OK(os_file_open(&c, path, OS_OPEN_READ));
    EXPECT(os_file_try_lock(&c, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_file_unlock(&a);
    os_file_unlock(&b);
    OK(os_file_try_lock(&c, OS_LOCK_EXCLUSIVE));
    /* Closing the locking handle (no duplicates) releases the lock. */
    os_file_close(&c);
    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&a);

    /* Duplicates share the lock: closing one of them keeps it; the last
     * close releases it (both close orders). */
    os_file dup = OS_FILE_INIT;
    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    OK(os_file_dup(&dup, &a));
    os_file_close(&a);
    EXPECT(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_file_close(&dup);
    OK(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&b);
    OK(os_file_open(&a, path, OS_OPEN_RDWR));
    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    OK(os_file_dup(&dup, &a));
    os_file_close(&dup);
    EXPECT(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_file_close(&a);
    OK(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&b);
    /* A duplicate may unlock it. */
    OK(os_file_open(&a, path, OS_OPEN_RDWR));
    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    OK(os_file_dup(&dup, &a));
    os_file_unlock(&dup);
    OK(os_file_try_lock(&b, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&b);
    os_file_close(&dup);
    os_file_close(&a);

    /* Session-lease handoff (src/editor/recovery.c:754): the creator locks,
     * the editor duplicates, the creator closes its copy; a third party is
     * excluded throughout, and admitted once the editor closes. */
    char lease[OS_PATH_MAX];
    join(lease, sizeof lease, work_root, "lease.lock");
    os_file creator = OS_FILE_INIT, editor = OS_FILE_INIT, third = OS_FILE_INIT;
    OK(os_file_open(&creator, lease, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_try_lock(&creator, OS_LOCK_EXCLUSIVE));
    OK(os_file_open(&third, lease, OS_OPEN_RDWR));
    EXPECT(os_file_try_lock(&third, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    OK(os_file_dup(&editor, &creator));
    EXPECT(os_file_try_lock(&third, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_file_close(&creator);
    EXPECT(os_file_try_lock(&third, OS_LOCK_SHARED), OS_E_BUSY);
    os_stat held, named;
    OK(os_file_stat(&editor, &held));
    OK(os_stat_path(lease, 0, &named));
    CHECK(os_identity_equal(&held.id, &named.id));
    os_file_close(&editor);
    OK(os_file_try_lock(&third, OS_LOCK_EXCLUSIVE));
    os_file_close(&third);
    OK(os_unlink(lease));

    /* Duplicates share the sequential position. */
    OK(os_file_open(&a, path, OS_OPEN_RDWR | OS_OPEN_TRUNC));
    OK(os_file_dup(&dup, &a));
    OK(os_file_write(&a, "ab", 2, &done));
    OK(os_file_write(&dup, "cd", 2, &done));
    os_file_close(&dup);
    memset(buf, 0, sizeof buf);
    OK(os_file_pread(&b, buf, sizeof buf, 0, &got));
    CHECK(got == 4 && memcmp(buf, "abcd", 4) == 0);
    os_file_close(&a);
    os_file_close(&b);
    OK(os_unlink(path));

    /* Directory locks: same semantics between two directory handles. */
    os_dir d1 = OS_DIR_INIT, d2 = OS_DIR_INIT;
    OK(os_dir_open(&d1, work_root));
    OK(os_dir_open(&d2, work_root));
    OK(os_dir_try_lock(&d1, OS_LOCK_SHARED));
    OK(os_dir_try_lock(&d2, OS_LOCK_SHARED));
    os_dir_unlock(&d2);
    EXPECT(os_dir_try_lock(&d2, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_dir_unlock(&d1);
    OK(os_dir_try_lock(&d2, OS_LOCK_EXCLUSIVE));
    EXPECT(os_dir_try_lock(&d1, OS_LOCK_SHARED), OS_E_BUSY);
    os_dir_close(&d2);                                    /* close releases */
    OK(os_dir_try_lock(&d1, OS_LOCK_EXCLUSIVE));
    os_dir_unlock(&d1);
    os_dir_close(&d1);

    /* A directory lock is keyed by the directory object, not its path: the
     * locked directory renamed away stays locked, and a new directory at
     * the old path is a different, unlocked object. */
    char p[OS_PATH_MAX], p2[OS_PATH_MAX];
    join(p, sizeof p, work_root, "lockdir");
    join(p2, sizeof p2, work_root, "lockdir-moved");
    OK(os_mkdir(p));
    os_dir l1 = OS_DIR_INIT, l3 = OS_DIR_INIT, l4 = OS_DIR_INIT;
    OK(os_dir_open(&l1, p));
    OK(os_dir_try_lock(&l1, OS_LOCK_EXCLUSIVE));
    OK(os_rename(p, p2, OS_RENAME_NOREPLACE));
    OK(os_mkdir(p));
    OK(os_dir_open(&l3, p));
    OK(os_dir_try_lock(&l3, OS_LOCK_EXCLUSIVE));          /* a different directory */
    OK(os_dir_open(&l4, p2));
    EXPECT(os_dir_try_lock(&l4, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_dir_close(&l1);
    OK(os_dir_try_lock(&l4, OS_LOCK_EXCLUSIVE));
    os_dir_close(&l4);
    os_dir_close(&l3);
    OK(os_rmdir(p2));

    /* A user's entry with any name, the old reserved name included, is
     * never hidden, touched or removed by locking, and an empty directory
     * held open and locked can still be removed, its name gone at once. */
    os_dir q = OS_DIR_INIT;
    OK(os_dir_open(&q, p));
    put_at(&q, ".sublimite-dirlock", "user data");
    OK(os_dir_try_lock(&q, OS_LOCK_EXCLUSIVE));
    os_dir_iter it;
    OK(os_dir_iter_open(&it, &q));
    const char *want[] = {".sublimite-dirlock"};
    int seen[1];
    CHECK(list_names(&it, want, 1, seen) == 1 && seen[0] == 1);
    os_dir_iter_close(&it);
    CHECK(content_at(&q, ".sublimite-dirlock", "user data"));
    EXPECT(os_rmdir(p), OS_E_EXIST);
    CHECK(content_at(&q, ".sublimite-dirlock", "user data"));
    os_dir_unlock(&q);
    CHECK(content_at(&q, ".sublimite-dirlock", "user data"));
    OK(os_unlink_at(&q, ".sublimite-dirlock"));
    OK(os_dir_try_lock(&q, OS_LOCK_EXCLUSIVE));
    OK(os_rmdir(p));
    os_stat st;
    EXPECT(os_stat_path(p, 0, &st), OS_E_NOENT);
    os_dir_close(&q);
}

/* ---- mapping ------------------------------------------------------------ */
static void case_map(void)
{
    size_t ps = os_page_size(), gran = os_map_granularity();
    CHECK(ps >= 4096 && (ps & (ps - 1)) == 0);
    CHECK(gran >= ps && gran % ps == 0);
    CHECK(os_phys_mem_bytes() > 0);

    /* Rename-replace over a mapped target keeps the view on the old bytes. */
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));
    size_t len = 3 * ps;
    unsigned char *page = malloc(len);
    EDIT_ASSERT(page);
    memset(page, 'A', len);
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &d, "mapped", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0;
    OK(os_file_pwrite(&f, page, len, 0, &done));
    CHECK(done == len);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, len));
    CHECK(m.addr != NULL && m.len == len);
    os_map_prefetch(&m, 0, len);
    const volatile unsigned char *v = m.addr;
    CHECK(v[0] == 'A' && v[len - 1] == 'A');
    put_at(&d, "replacement", "new");
    OK(os_rename_at(&d, "replacement", &d, "mapped", OS_RENAME_REPLACE));
    CHECK(v[0] == 'A' && v[len - 1] == 'A');
    CHECK(content_at(&d, "mapped", "new"));
    os_file_close(&f);                     /* view outlives the handle */
    CHECK(v[ps] == 'A');
    os_unmap(&m);
    OK(os_unlink_at(&d, "mapped"));

    /* A nonzero, granularity-aligned offset and a partial last page: the
     * view starts at the offset's byte and ends at the file's last byte. */
    size_t big = 3 * gran + 100;
    unsigned char *pat = malloc(big);
    EDIT_ASSERT(pat);
    for (size_t i = 0; i < big; i++) pat[i] = (unsigned char)('a' + (i / gran) + (i % 7));
    OK(os_file_open_at(&f, &d, "offset", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_pwrite(&f, pat, big, 0, &done));
    CHECK(done == big);
    OK(os_map_file(&m, &f, gran, big - gran));
    CHECK(m.len == big - gran);
    if (m.addr) {
        v = m.addr;
        CHECK(memcmp((const void *)m.addr, pat + gran, big - gran) == 0);
        CHECK(v[0] == pat[gran] && v[m.len - 1] == pat[big - 1]);
        os_unmap(&m);
    }
    EXPECT(os_map_file(&m, &f, ps == gran ? 1 : ps, ps), OS_E_INVAL);  /* misaligned */
    os_file_close(&f);
    OK(os_unlink_at(&d, "offset"));
    free(pat);

    /* Truncation by another handle while mapped and registered. */
    OK(os_file_open_at(&f, &d, "shrink", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_pwrite(&f, page, len, 0, &done));
    OK(os_map_file(&m, &f, 0, len));
    v = m.addr;
    int slot = -1;
    OK(os_fault_register(&m, &slot));
    CHECK(slot >= 0 && slot < OS_FAULT_SLOTS);
    uint64_t e0 = os_fault_epoch(slot);
    CHECK(v[2 * ps + 1] == 'A');
    CHECK(os_fault_epoch(slot) == e0);     /* no fault, no recovery */
    os_file other = OS_FILE_INIT;
    OK(os_file_open_at(&other, &d, "shrink", OS_OPEN_RDWR));
    os_err tr = os_file_set_size(&other, ps);
    CHECK(tr == OS_OK || tr == OS_E_BUSY);
    printf("map: truncate while mapped -> %s\n", os_err_name(tr));
    if (tr == OS_OK) {
        /* Reads past the new end fault and are recovered as zeros; the epoch
         * changes; bytes before the faulting page keep the file content. */
        CHECK(v[2 * ps + 1] == 0);
        uint64_t e1 = os_fault_epoch(slot);
        CHECK(e1 > e0);
        CHECK(v[len - 1] == 0);
        CHECK(v[0] == 'A' && v[ps - 1] == 'A');
    } else {
        /* Windows: shrinking a mapped file is refused; the view is intact. */
        CHECK(v[2 * ps + 1] == 'A' && v[len - 1] == 'A');
        CHECK(os_fault_epoch(slot) == e0);
    }
    os_fault_retire(slot);
    os_unmap(&m);
    os_file_close(&other);

    /* A range past the end of the file (it shrank after the caller sized
     * the view): Linux maps it and a registered view recovers the pages
     * past the end; Windows refuses with OS_E_RANGE. */
    OK(os_file_set_size(&f, ps));
    os_err past = os_map_file(&m, &f, 0, 3 * ps);
    CHECK(past == OS_OK || past == OS_E_RANGE);
    printf("map: range past end of file -> %s\n", os_err_name(past));
    if (past == OS_OK) {
        CHECK(m.len == 3 * ps);
        OK(os_fault_register(&m, &slot));
        v = m.addr;
        uint64_t p0 = os_fault_epoch(slot);
        CHECK(v[0] == 'A' && v[ps - 1] == 'A');
        CHECK(v[ps + 1] == 0);
        CHECK(os_fault_epoch(slot) > p0);
        os_fault_retire(slot);
        os_unmap(&m);
    }
    os_file_close(&f);
    OK(os_unlink_at(&d, "shrink"));
    os_dir_close(&d);
    free(page);

    /* Anonymous address space. */
    void *base = NULL;
    OK(os_vm_reserve(&base, 4 * ps));
    CHECK(base != NULL && ((uintptr_t)base % ps) == 0);
    OK(os_vm_commit(base, 4 * ps));
    unsigned char *b = base;
    memset(b, 0x5a, 4 * ps);
    OK(os_vm_decommit(b + ps, ps));
    OK(os_vm_commit(b + ps, ps));
    CHECK(b[ps] == 0 && b[2 * ps - 1] == 0);             /* recommitted pages read zero */
    CHECK(b[0] == 0x5a && b[2 * ps] == 0x5a);            /* neighbours untouched */
    os_vm_release(base, 4 * ps);
}

/* ---- fault service ------------------------------------------------------ */
/* Maps a fresh file of pages * page size of 'A' under name. */
static void map_pages(const os_dir *d, const char *name, size_t pages, os_map *m)
{
    size_t ps = os_page_size(), len = pages * ps, done = 0;
    unsigned char *buf = malloc(len);
    EDIT_ASSERT(buf);
    memset(buf, 'A', len);
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, d, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_pwrite(&f, buf, len, 0, &done));
    memset(m, 0, sizeof *m);
    OK(os_map_file(m, &f, 0, len));
    os_file_close(&f);
    free(buf);
}

static bool all_bytes(const volatile unsigned char *p, size_t n, unsigned char want)
{
    for (size_t i = 0; i < n; i++) if (p[i] != want) return false;
    return true;
}

typedef struct install_ctx {
    barrier *start;
    os_map *m;
    int slot;
    os_err reg, trig;
    bool zero, prefix;
    uint64_t e0, e1;
} install_ctx;
/* Released together at the barrier: first registration races installation. */
static void install_body(void *arg)
{
    install_ctx *c = arg;
    size_t ps = os_page_size();
    barrier_wait(c->start);
    c->reg = os_fault_register(c->m, &c->slot);
    if (c->reg != OS_OK) return;
    c->e0 = os_fault_epoch(c->slot);
    c->trig = os_fault_test_trigger(c->m, ps + 1);
    c->e1 = os_fault_epoch(c->slot);
    const volatile unsigned char *v = c->m->addr;
    c->prefix = all_bytes(v, ps, 'A');
    c->zero = all_bytes(v + ps, c->m->len - ps, 0);
}

static void case_fault(void)
{
    size_t ps = os_page_size();
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));

    /* This case runs in a fresh process: four threads make the first
     * registrations at once, racing the service's one installation. Each
     * then takes a native fault in its own view and is recovered. */
    enum { INSTALLERS = 4 };
    os_map im[INSTALLERS];
    install_ctx ic[INSTALLERS];
    os_thread it[INSTALLERS];
    barrier start;
    barrier_init(&start, INSTALLERS);
    for (int i = 0; i < INSTALLERS; i++) {
        char name[32];
        snprintf(name, sizeof name, "install-%d", i);
        map_pages(&d, name, 3, &im[i]);
        ic[i] = (install_ctx){&start, &im[i], -1, OS_E_IO, OS_E_IO, false, false, 0, 0};
        OK(os_thread_start(&it[i], install_body, &ic[i]));
    }
    /* edit-nafu: join every installer before retiring any slot. Retiring
     * slot 0 while installer 3 still sits between the barrier and its
     * registration lets it legally reuse slot 0, which the uniqueness check
     * below then reports as a duplicate (seen under parallel make check). */
    for (int i = 0; i < INSTALLERS; i++) OK(os_thread_join(&it[i]));
    for (int i = 0; i < INSTALLERS; i++) {
        CHECK(ic[i].reg == OS_OK && ic[i].trig == OS_OK);
        CHECK(ic[i].e1 == ic[i].e0 + 1);
        CHECK(ic[i].prefix && ic[i].zero);
        for (int j = 0; j < i; j++) CHECK(ic[i].slot != ic[j].slot);
        os_fault_retire(ic[i].slot);
        os_unmap(&im[i]);
        char name[32];
        snprintf(name, sizeof name, "install-%d", i);
        OK(os_unlink_at(&d, name));
    }
    barrier_destroy(&start);

    /* Recovery from the middle of a view, twice: each fault zero-fills from
     * its page to the end, the prefix keeps reading the file, the epoch
     * counts each recovery. */
    os_map m;
    map_pages(&d, "recover", 4, &m);
    const volatile unsigned char *v = m.addr;
    int slot = -1;
    OK(os_fault_register(&m, &slot));
    uint64_t e0 = os_fault_epoch(slot);
    EXPECT(os_fault_test_trigger(&m, m.len), OS_E_INVAL);
    OK(os_fault_test_trigger(&m, 2 * ps + 5));
    CHECK(os_fault_epoch(slot) == e0 + 1);
    CHECK(all_bytes(v, 2 * ps, 'A'));
    CHECK(all_bytes(v + 2 * ps, 2 * ps, 0));
    OK(os_fault_test_trigger(&m, ps + 1));
    CHECK(os_fault_epoch(slot) == e0 + 2);
    CHECK(all_bytes(v, ps, 'A'));
    CHECK(all_bytes(v + ps, 3 * ps, 0));
    /* Cleanup after recovery: retire, unmap the split view, map again: the
     * file itself was never touched. */
    os_fault_retire(slot);
    os_unmap(&m);
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &d, "recover", OS_OPEN_READ));
    OK(os_map_file(&m, &f, 0, 4 * ps));
    if (m.addr) CHECK(all_bytes(m.addr, 4 * ps, 'A'));
    os_file_close(&f);

    /* Capacity: OS_FAULT_SLOTS registrations, then OS_E_NOSPC; retired
     * slots are reusable. */
    static int slots[OS_FAULT_SLOTS];
    int n = 0;
    for (; n < OS_FAULT_SLOTS; n++) {
        if (os_fault_register(&m, &slots[n]) != OS_OK) break;
    }
    CHECK(n == OS_FAULT_SLOTS);
    int extra = -1;
    EXPECT(os_fault_register(&m, &extra), OS_E_NOSPC);
    CHECK(extra == -1);
    for (int i = 0; i < n; i++) os_fault_retire(slots[i]);
    OK(os_fault_register(&m, &extra));
    os_fault_retire(extra);
    os_unmap(&m);
    OK(os_unlink_at(&d, "recover"));
    os_dir_close(&d);

    /* A fault in no registered view is passed on, not swallowed: a child
     * that takes one must die (the default action, or the sanitizer's
     * handler, which exits nonzero). */
    char exe[OS_PATH_MAX];
    OK(os_proc_self_path(exe, sizeof exe));
    const char *argv[] = {exe, "--child=stray-fault", work_root, NULL};
    os_proc p;
    os_proc_status s = {0, 0};
    puts("fault: a child now takes an unregistered fault on purpose (a crash report may follow)");
    fflush(stdout);
    OK(os_proc_spawn(&p, argv));
    OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    CHECK(!(s.how == OS_PROC_EXITED && s.code == 0));
    printf("fault: stray fault child -> %s %d\n",
           s.how == OS_PROC_EXITED ? "exited" : "abnormal", s.code);
}

/* ---- random ------------------------------------------------------------- */
static void case_random(void)
{
    unsigned char a[32], b[32], zero[32] = {0};
    OK(os_random(a, sizeof a));
    OK(os_random(b, sizeof b));
    /* (P) Two equal 256-bit draws, or an all-zero one, have probability 2^-256. */
    CHECK(memcmp(a, b, sizeof a) != 0);
    CHECK(memcmp(a, zero, sizeof a) != 0);
    OK(os_random(a, 0));
}

/* ---- UTF-8 paths -------------------------------------------------------- */
static void case_utf8(void)
{
    /* Latin-1 range, Greek, CJK and a 4-byte (UTF-16 surrogate pair) emoji. */
    const char *dir_name = "verzeichnis-\xc3\xa4\xc3\xb6\xc3\xbc";              /* äöü */
    const char *file_name = "gr\xc3\xbc\xc3\x9f" "e-\xce\xa9-\xe6\x97\xa5\xe6\x9c\xac"
                            "-\xf0\x9f\x99\x82.txt";                            /* grüße-Ω-日本-🙂.txt */
    char dir_path[OS_PATH_MAX], file_path[OS_PATH_MAX], canon[OS_PATH_MAX];
    join(dir_path, sizeof dir_path, work_root, dir_name);
    join(file_path, sizeof file_path, dir_path, file_name);
    OK(os_mkdir(dir_path));
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, dir_path));
    put_at(&d, file_name, "utf8");
    /* The listing returns the name byte for byte. */
    os_dir_iter it;
    OK(os_dir_iter_open(&it, &d));
    const char *name;
    int found = 0;
    while (os_dir_next(&it, &name) == OS_OK && name) found += strcmp(name, file_name) == 0;
    os_dir_iter_close(&it);
    CHECK(found == 1);
    /* Path-based calls accept the same bytes. */
    os_stat st;
    OK(os_stat_path(file_path, 0, &st));
    CHECK(st.kind == OS_KIND_FILE && st.size == 4);
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, file_path, OS_OPEN_READ));
    char buf[16];
    slurp(&f, buf, sizeof buf);
    CHECK(strcmp(buf, "utf8") == 0);
    os_file_close(&f);
    OK(os_path_canonical(file_path, canon, sizeof canon));
    CHECK(os_path_is_absolute(canon));
    CHECK(ends_with(canon, file_name));
    char dir_tail[OS_PATH_MAX];
    snprintf(dir_tail, sizeof dir_tail, "%s%c%s", dir_name, OS_PATH_SEP, file_name);
    CHECK(ends_with(canon, dir_tail));
    EXPECT(os_path_canonical(file_path, canon, 4), OS_E_RANGE);
    /* Environment values are UTF-8 too. */
    OK(os_env_set("OS_TEST_UTF8", file_name));
    EXPECT(os_env_get("OS_TEST_UTF8", buf, 4), OS_E_RANGE);
    char env[OS_PATH_MAX];
    OK(os_env_get("OS_TEST_UTF8", env, sizeof env));
    CHECK(strcmp(env, file_name) == 0);
    OK(os_env_set("OS_TEST_UTF8", NULL));
    EXPECT(os_env_get("OS_TEST_UTF8", env, sizeof env), OS_E_NOENT);
    OK(os_unlink(file_path));
    os_dir_close(&d);
    OK(os_rmdir(dir_path));
}

/* ---- relative and long paths -------------------------------------------- */
/* UTF-16 code units of a UTF-8 string (4-byte sequences are surrogate pairs). */
static size_t utf16_units(const char *s)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xc0) == 0x80) continue;
        n += (*p >= 0xf0) ? 2 : 1;
    }
    return n;
}
static bool is_sep(char c) { return c == '/' || c == OS_PATH_SEP; }
/* Length of the root of an absolute path: "/" (POSIX), "C:\" (drive) or
 * "\\server\share\" (UNC). */
static size_t root_len(const char *p)
{
    if (p[0] && p[1] == ':' && is_sep(p[2])) return 3;
    if (is_sep(p[0]) && is_sep(p[1])) {
        size_t i = 2, seps = 0;
        while (p[i] && seps < 2) if (is_sep(p[i++])) seps++;
        return i;
    }
    return is_sep(p[0]) ? 1 : 0;
}
/* abs as a path relative to the current directory, through the root
 * ("../../tmp/x"), or false when they do not share a root. */
static bool relative_to_cwd(const char *abs, char *out, size_t cap)
{
    char cwd[OS_PATH_MAX];
    if (os_cwd(cwd, sizeof cwd) != OS_OK) return false;
    size_t ra = root_len(abs), rc = root_len(cwd);
    if (!ra || ra != rc || strncmp(abs, cwd, ra) != 0) return false;
    size_t depth = 0;
    for (size_t i = rc; cwd[i]; i++)
        if (is_sep(cwd[i]) && cwd[i + 1] && !is_sep(cwd[i + 1])) depth++;
    if (cwd[rc]) depth++;                 /* the first component after the root */
    size_t at = 0;
    out[0] = 0;
    for (size_t i = 0; i < depth; i++) {
        int k = snprintf(out + at, cap - at, "..%c", OS_PATH_SEP);
        if (k < 0 || (size_t)k >= cap - at) return false;
        at += (size_t)k;
    }
    int k = snprintf(out + at, cap - at, "%s", abs + ra);
    return k >= 0 && (size_t)k < cap - at;
}

static void case_paths(void)
{
    /* Relative paths resolve against the current directory. */
    char abs_file[OS_PATH_MAX], rel_dir[OS_PATH_MAX], rel_file[OS_PATH_MAX], rel2[OS_PATH_MAX];
    join(abs_file, sizeof abs_file, work_root, "rel.txt");
    if (relative_to_cwd(work_root, rel_dir, sizeof rel_dir)) {
        CHECK(!os_path_is_absolute(rel_dir));
        join(rel_file, sizeof rel_file, rel_dir, "rel.txt");
        join(rel2, sizeof rel2, rel_dir, "rel2.txt");
        put_path(rel_file, "relative", 8);
        os_stat a, b;
        OK(os_stat_path(rel_file, 0, &a));
        OK(os_stat_path(abs_file, 0, &b));
        CHECK(os_identity_equal(&a.id, &b.id) && a.size == 8);
        char c1[OS_PATH_MAX], c2[OS_PATH_MAX];
        OK(os_path_canonical(rel_file, c1, sizeof c1));
        OK(os_path_canonical(abs_file, c2, sizeof c2));
        CHECK(strcmp(c1, c2) == 0 && os_path_is_absolute(c1));
        os_dir d = OS_DIR_INIT;
        OK(os_dir_open(&d, rel_dir));
        CHECK(content_at(&d, "rel.txt", "relative"));
        os_dir_close(&d);
        OK(os_rename(rel_file, rel2, OS_RENAME_NOREPLACE));
        CHECK(content_path(rel2, "relative"));
        OK(os_unlink(rel2));
        printf("paths: relative %s\n", rel_dir);
    } else {
        printf("paths: relative skipped (work dir and cwd share no root)\n");
    }

    /* Nested Unicode directories whose full path exceeds Windows' MAX_PATH
     * (P: 260 UTF-16 units). */
    const char *level = "ebene-\xc3\xa4\xc3\xb6\xc3\xbc-\xce\xa9-\xe6\x97\xa5-"
                        "abcdefghijklmnopqrstuvwxyz0123456789";
    enum { LEVELS = 6 };
    char dirs[LEVELS][OS_PATH_MAX];
    const char *parent = work_root;
    for (int i = 0; i < LEVELS; i++) {
        char name[128];
        snprintf(name, sizeof name, "%d-%s", i, level);
        join(dirs[i], sizeof dirs[i], parent, name);
        OK(os_mkdir(dirs[i]));
        parent = dirs[i];
    }
    const char *leaf_name = "blatt-\xf0\x9f\x99\x82.txt";
    char leaf[OS_PATH_MAX];
    join(leaf, sizeof leaf, dirs[LEVELS - 1], leaf_name);
    CHECK(utf16_units(leaf) > 260);
    put_path(leaf, "deep", 4);
    os_stat st;
    OK(os_stat_path(leaf, 0, &st));
    CHECK(st.kind == OS_KIND_FILE && st.size == 4);
    CHECK(content_path(leaf, "deep"));
    char canon[OS_PATH_MAX];
    OK(os_path_canonical(leaf, canon, sizeof canon));
    CHECK(ends_with(canon, leaf_name) && utf16_units(canon) > 260);
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, dirs[LEVELS - 1]));
    os_dir_iter it;
    OK(os_dir_iter_open(&it, &d));
    const char *name;
    int found = 0;
    while (os_dir_next(&it, &name) == OS_OK && name) found += strcmp(name, leaf_name) == 0;
    os_dir_iter_close(&it);
    CHECK(found == 1);
    os_dir_close(&d);
    printf("paths: long path %zu UTF-16 units\n", utf16_units(leaf));
    OK(os_unlink(leaf));
    for (int i = LEVELS - 1; i >= 0; i--) OK(os_rmdir(dirs[i]));
}

/* ---- well-known directories -------------------------------------------- */
static void case_known_dirs(void)
{
    char tmp[OS_PATH_MAX], cfg[OS_PATH_MAX], data[OS_PATH_MAX], cwd[OS_PATH_MAX];
    OK(os_temp_dir(tmp, sizeof tmp));
    CHECK(os_path_is_absolute(tmp));
    CHECK(tmp[strlen(tmp) - 1] != OS_PATH_SEP || strlen(tmp) == 1);
    os_stat st;
    OK(os_stat_path(tmp, 0, &st));
    CHECK(st.kind == OS_KIND_DIR);
    char tail[16];
    snprintf(tail, sizeof tail, "%csublimite", OS_PATH_SEP);
    OK(os_app_dir(OS_APP_CONFIG, cfg, sizeof cfg));
    OK(os_app_dir(OS_APP_DATA, data, sizeof data));
    CHECK(os_path_is_absolute(cfg) && ends_with(cfg, tail));
    CHECK(os_path_is_absolute(data) && ends_with(data, tail));
    EXPECT(os_app_dir(OS_APP_CONFIG, cfg, 4), OS_E_RANGE);
    printf("known_dirs: temp=%s config=%s data=%s\n", tmp, cfg, data);
    OK(os_cwd(cwd, sizeof cwd));
    CHECK(os_path_is_absolute(cwd));
    CHECK(!os_path_is_absolute("relative"));
    CHECK(!os_path_is_absolute(""));
}

/* ---- reparse-backed data files (Windows fixture) ----------------------- */
static void case_reparse(void)
{
    char path[OS_PATH_MAX];
    if (os_env_get("OS_TEST_DATA_REPARSE", path, sizeof path) != OS_OK) {
        puts("reparse: no fixture (OS_TEST_DATA_REPARSE unset; W0.3 supplies a WOF file)");
        return;
    }
    /* An ordinary data file carrying reparse metadata is a FILE, followed
     * or not, and opens and reads with NOFOLLOW. */
    os_stat a, b;
    OK(os_stat_path(path, 0, &a));
    OK(os_stat_path(path, OS_STAT_NOFOLLOW, &b));
    CHECK(a.kind == OS_KIND_FILE && b.kind == OS_KIND_FILE);
    CHECK(os_identity_equal(&a.id, &b.id));
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_READ | OS_OPEN_NOFOLLOW));
    char buf[64];
    size_t got = 0;
    OK(os_file_pread(&f, buf, sizeof buf, 0, &got));
    CHECK(got > 0 || a.size == 0);
    os_file_close(&f);
    printf("reparse: %s is a file of %llu bytes\n", path, (unsigned long long)a.size);
}

/* ---- processes ---------------------------------------------------------- */
/* Arguments the child must receive byte for byte (CommandLineToArgvW
 * quoting corners, and non-ASCII). */
static const char *const tricky_args[] = {
    "",
    "two words",
    "quote\"inside",
    "trailing\\",
    "trailing two\\\\",
    "back\\\"slash quote",
    " lead and trail ",
    "tab\there",
    ("gr\xc3\xbc\xc3\x9f" "e-\xce\xa9-\xe6\x97\xa5\xe6\x9c\xac-\xf0\x9f\x99\x82"),
};
enum { TRICKY = sizeof tricky_args / sizeof tricky_args[0] };

static int run_child(const char *role, int argc, char **argv)
{
    const char *arg = argc >= 3 ? argv[2] : "";
    if (strcmp(role, "exit7") == 0) return 7;
    if (strcmp(role, "sleep") == 0) {
        for (;;) os_sleep_ns(1000 * MS);
    }
    if (strcmp(role, "lock") == 0) {
        /* Takes an exclusive lock on arg, then sleeps until killed. */
        os_file f = OS_FILE_INIT;
        if (os_file_open(&f, arg, OS_OPEN_RDWR) != OS_OK) return 2;
        uint64_t guard = os_now_ns() + HANG_GUARD_NS;
        while (os_file_try_lock(&f, OS_LOCK_EXCLUSIVE) != OS_OK) {
            if (os_now_ns() > guard) return 3;
            os_sleep_ns(MS);
        }
        for (;;) os_sleep_ns(1000 * MS);
    }
    if (strcmp(role, "args") == 0) {
        /* argv[2..] must equal tricky_args exactly. */
        if (argc != 2 + TRICKY) return 10;
        for (int i = 0; i < TRICKY; i++)
            if (strcmp(argv[2 + i], tricky_args[i]) != 0) return 11 + i;
        return 0;
    }
    if (strcmp(role, "fdscan") == 0) {
        /* Exit 1 if this process holds a handle to the file at arg. Linux
         * looks in its descriptor table; elsewhere (no /proc) the parent's
         * delete check covers the same question. */
        os_stat want;
        if (os_stat_path(arg, 0, &want) != OS_OK) return 2;
        os_dir fds = OS_DIR_INIT;
        if (os_dir_open(&fds, "/proc/self/fd") != OS_OK) return 0;
        os_dir_iter it;
        if (os_dir_iter_open(&it, &fds) != OS_OK) return 3;
        const char *name;
        int found = 0;
        while (os_dir_next(&it, &name) == OS_OK && name) {
            os_stat st;
            if (os_stat_at(&fds, name, 0, &st) == OS_OK && os_identity_equal(&st.id, &want.id))
                found = 1;
        }
        os_dir_iter_close(&it);
        os_dir_close(&fds);
        return found;
    }
    if (strcmp(role, "stray-fault") == 0) {
        /* Installs the service (one registered view), then takes a fault in
         * a view that is not registered. Surviving it is the failure. */
        char exe[OS_PATH_MAX];
        if (os_proc_self_path(exe, sizeof exe) != OS_OK) return 2;
        os_file f = OS_FILE_INIT;
        if (os_file_open(&f, exe, OS_OPEN_READ) != OS_OK) return 3;
        os_map reg = {0}, stray = {0};
        int slot = -1;
        if (os_map_file(&reg, &f, 0, os_page_size()) != OS_OK) return 4;
        if (os_map_file(&stray, &f, 0, os_page_size()) != OS_OK) return 5;
        if (os_fault_register(&reg, &slot) != OS_OK) return 6;
        (void)os_fault_test_trigger(&stray, 1);
        return 0;
    }
    return 99;
}

static void case_proc(void)
{
    char exe[OS_PATH_MAX];
    OK(os_proc_self_path(exe, sizeof exe));
    CHECK(os_path_is_absolute(exe));
    os_stat st;
    OK(os_stat_path(exe, 0, &st));
    CHECK(st.kind == OS_KIND_FILE);
    CHECK(os_proc_id() != 0);

    os_proc p;
    os_proc_status s = {0, 0};
    const char *exit7[] = {exe, "--child=exit7", NULL};
    uint64_t t0 = os_now_ns();
    OK(os_proc_spawn(&p, exit7));
    OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    CHECK(s.how == OS_PROC_EXITED && s.code == 7);
    printf("proc: spawn-to-exit %.3f ms (M)\n", (double)(os_now_ns() - t0) / 1e6);

    const char *sleeper[] = {exe, "--child=sleep", NULL};
    OK(os_proc_spawn(&p, sleeper));
    EXPECT(os_proc_wait(&p, 0, &s), OS_E_TIMEDOUT);
    EXPECT(os_proc_wait(&p, (int64_t)(5 * MS), &s), OS_E_TIMEDOUT);
    OK(os_proc_kill(&p));
    OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    CHECK(s.how == OS_PROC_ABNORMAL);

    /* argv reaches the child byte for byte as UTF-8. */
    const char *args[2 + TRICKY + 1];
    args[0] = exe;
    args[1] = "--child=args";
    for (int i = 0; i < TRICKY; i++) args[2 + i] = tricky_args[i];
    args[2 + TRICKY] = NULL;
    OK(os_proc_spawn(&p, args));
    OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    CHECK(s.how == OS_PROC_EXITED && s.code == 0);
    if (s.code) printf("proc: args child exit %d (10 = argc, 11 + i = argument i)\n", s.code);

    /* No handle reaches the child except the standard streams, not even
     * one opened outside os.h without close-on-exec (C stdio here). */
    char sentinel[OS_PATH_MAX];
    join(sentinel, sizeof sentinel, work_root, "sentinel");
    FILE *leak = fopen(sentinel, "w");
    CHECK(leak != NULL);
    if (leak) {
        const char *scan[] = {exe, "--child=fdscan", sentinel, NULL};
        OK(os_proc_spawn(&p, scan));
        OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
        CHECK(s.how == OS_PROC_EXITED && s.code == 0);
        OK(os_proc_spawn(&p, sleeper));
        fclose(leak);
        /* Windows: an inherited CRT handle (no FILE_SHARE_DELETE) would make
         * this fail OS_E_BUSY while the child lives. */
        OK(os_unlink(sentinel));
        OK(os_proc_kill(&p));
        OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    }

    /* A lock held by a process is released when that process dies. */
    char path[OS_PATH_MAX];
    join(path, sizeof path, work_root, "proc.lock");
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    const char *locker[] = {exe, "--child=lock", path, NULL};
    OK(os_proc_spawn(&p, locker));
    uint64_t guard = os_now_ns() + HANG_GUARD_NS;
    bool child_holds = false;
    while (!child_holds && os_now_ns() < guard) {
        os_err rc = os_file_try_lock(&f, OS_LOCK_EXCLUSIVE);
        if (rc == OS_E_BUSY) child_holds = true;
        else { if (rc == OS_OK) os_file_unlock(&f); os_sleep_ns(MS); }
    }
    CHECK(child_holds);
    OK(os_proc_kill(&p));
    OK(os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s));
    CHECK(s.how == OS_PROC_ABNORMAL);
    bool released = false;
    guard = os_now_ns() + HANG_GUARD_NS;
    while (!released && os_now_ns() < guard) {
        if (os_file_try_lock(&f, OS_LOCK_EXCLUSIVE) == OS_OK) released = true;
        else os_sleep_ns(MS);
    }
    CHECK(released);
    os_file_unlock(&f);
    os_file_close(&f);
    OK(os_unlink(path));
}

/* ---- driver ------------------------------------------------------------- */
typedef struct test_case { const char *name; void (*fn)(void); } test_case;
static const test_case cases[] = {
    {"errors", case_errors},     {"clocks", case_clocks},   {"threads", case_threads},
    {"wake", case_wake},         {"noalloc", case_noalloc}, {"files", case_files},
    {"dirs", case_dirs},         {"listing", case_listing}, {"locks", case_locks},
    {"map", case_map},           {"fault", case_fault},     {"random", case_random},
    {"utf8", case_utf8},         {"paths", case_paths},     {"known_dirs", case_known_dirs},
    {"reparse", case_reparse},   {"proc", case_proc},
};
enum { NCASES = sizeof cases / sizeof cases[0] };

static const test_case *find_case(const char *name)
{
    for (int i = 0; i < NCASES; i++)
        if (strcmp(cases[i].name, name) == 0) return &cases[i];
    return NULL;
}

/* One case, in this (child) process. */
static int run_case(const test_case *c)
{
    current_case = c->name;
    c->fn();
    int f = atomic_load(&failures);
    printf("%s: %s\n", c->name, f ? "FAIL" : "ok");
    fflush(stdout);
    return f ? 1 : 0;
}

/* Runs one case in a supervised child; true if it passed. */
static bool supervise(const char *exe, const test_case *c)
{
    char sel[64];
    snprintf(sel, sizeof sel, "--case=%s", c->name);
    const char *argv[] = {exe, sel, work_root, NULL};
    os_proc p;
    os_proc_status s = {0, 0};
    fflush(stdout);
    uint64_t t0 = os_now_ns();
    if (os_proc_spawn(&p, argv) != OS_OK) {
        printf("%s: FAIL (cannot spawn)\n", c->name);
        return false;
    }
    os_err w = os_proc_wait(&p, (int64_t)CASE_GUARD_NS, &s);
    if (w == OS_E_TIMEDOUT) {
        (void)os_proc_kill(&p);
        (void)os_proc_wait(&p, (int64_t)HANG_GUARD_NS, &s);
        printf("%s: FAIL (hung: killed after the case guard)\n", c->name);
        return false;
    }
    if (w != OS_OK) {
        printf("%s: FAIL (wait %s)\n", c->name, os_err_name(w));
        return false;
    }
    double ms = (double)(os_now_ns() - t0) / 1e6;
    if (s.how != OS_PROC_EXITED) {
        printf("%s: FAIL (died abnormally, code %d)\n", c->name, s.code);
        return false;
    }
    printf("%s: %.1f ms (M)\n", c->name, ms);
    return s.code == 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strncmp(argv[1], "--child=", 8) == 0)
        return run_child(argv[1] + 8, argc, argv);
    if (argc >= 3 && strncmp(argv[1], "--case=", 7) == 0) {
        const test_case *c = find_case(argv[1] + 7);
        if (!c) { fprintf(stderr, "os_test: unknown case %s\n", argv[1] + 7); return 2; }
        int n = snprintf(work_root, sizeof work_root, "%s", argv[2]);
        if (n < 0 || (size_t)n >= sizeof work_root) return 2;
        return run_case(c);
    }

    char exe[OS_PATH_MAX];
    if (os_proc_self_path(exe, sizeof exe) != OS_OK) { fputs("os_test: no self path\n", stderr); return 1; }
    char only[64];
    const test_case *one = NULL;
    os_err sel = os_env_get("OS_TEST_CASE", only, sizeof only);
    if (sel == OS_E_RANGE || (sel == OS_OK && !(one = find_case(only)))) {
        fprintf(stderr, "os_test: unknown OS_TEST_CASE (cases:");
        for (int i = 0; i < NCASES; i++) fprintf(stderr, " %s", cases[i].name);
        fputs(")\n", stderr);
        return 2;
    }

    char tmp[OS_PATH_MAX], leaf[96];
    if (os_temp_dir(tmp, sizeof tmp) != OS_OK) { fputs("os_test: no temp dir\n", stderr); return 1; }
    snprintf(leaf, sizeof leaf, "os_test-%llu-%llu", (unsigned long long)os_proc_id(),
             (unsigned long long)os_wall_ns());
    join(work_root, sizeof work_root, tmp, leaf);
    if (os_mkdir(work_root) != OS_OK) { fputs("os_test: cannot create work dir\n", stderr); return 1; }

    int ran = 0, failed = 0;
    for (int i = 0; i < NCASES; i++) {
        if (one && one != &cases[i]) continue;
        ran++;
        if (!supervise(exe, &cases[i])) failed++;
    }
    os_err rm = os_rmdir(work_root);
    if (rm != OS_OK) { fprintf(stderr, "os_test: work dir not empty: %s\n", os_err_name(rm)); failed++; }
    if (ran == 0) { fputs("os_test: no case ran\n", stderr); failed++; }
    printf("os_test: %d cases, %d failed\n", ran, failed);
    return failed ? 1 : 0;
}
