/* Linux conformance tests for the OS boundary. */
#include "os/os.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void check(bool ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "os_test.c:%d: FAIL %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

static void expect(os_err got, os_err want, const char *expression, int line)
{
    if (got != want) {
        int native = os_native_error();
        fprintf(stderr, "os_test.c:%d: FAIL %s -> %s (native %d), expected %s\n",
                line, expression, os_err_name(got), native, os_err_name(want));
        exit(EXIT_FAILURE);
    }
}
#define EXPECT(call, error) expect((call), (error), #call, __LINE__)
#define OK(call) EXPECT(call, OS_OK)

/* (P) SI conversion. The 10 s wait guard is inherited from v1 to bound
 * child startup and lock handoffs, which require only startup and one I/O. */
#define MS UINT64_C(1000000)
#define HANG_GUARD_NS (10 * 1000 * MS)
/* The v1 case guard is longer than each child wait and bounds deadlocks. */
#define CASE_GUARD_SECONDS 120u

static char work_root[OS_PATH_MAX];

static void join(char *out, size_t cap, const char *a, const char *b)
{
    int n = snprintf(out, cap, "%s/%s", a, b);
    CHECK(n >= 0 && (size_t)n < cap);
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
static void slurp(os_file *f, char *buf, size_t cap)
{
    size_t total = 0, got = 0;
    do {
        got = 0;
        OK(os_file_pread(f, buf + total, cap - 1 - total, total, &got));
        total += got;
    } while (got && total < cap - 1);
    buf[total] = 0;
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
    CHECK(strcmp(os_err_name((os_err)-1), "OS_E_?") == 0);
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
    EXPECT(os_file_open(&f, missing, OS_OPEN_READ | OS_OPEN_TRUNC), OS_E_INVAL);
}

/* ---- clocks ------------------------------------------------------------- */
static void case_clocks(void)
{
    uint64_t start = os_now_ns();
    CHECK(start > 0);
    os_sleep_ns(MS); /* Test one millisecond for a nonzero sleep. */
    CHECK(os_now_ns() - start >= MS);
    /* 2020-01-01T00:00:00Z is 1577836800 seconds from the Unix epoch. */
    CHECK(os_wall_ns() > 1577836800ull * (1000 * MS));

    uint64_t before = os_thread_cpu_ns(), slice = os_slice_ns();
    uint64_t resolution = os_thread_cpu_resolution_ns();
    CHECK(before > 0 && resolution > 0);
    CHECK(slice >= before);
    uint64_t now;
    do {
        now = os_thread_cpu_ns();
        CHECK(os_now_ns() - start < HANG_GUARD_NS);
    } while (now == slice);
    CHECK(now > slice && now - slice >= resolution);
    os_yield();
}

/* ---- threads, mutex, cond, once ---------------------------------------- */
/* Four workers contend for the pool locks. Each takes both locks once per
 * iteration, so each final counter must be WORKERS * INCREMENTS. */
enum { WORKERS = 4, INCREMENTS = 10000 };

typedef struct counter_ctx {
    os_mutex mu;
    os_rmutex rmu;
    barrier ids;
    uint64_t value, rvalue;
    uint64_t self[WORKERS];
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
    for (int i = 0; i < INCREMENTS; i++) {
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

typedef enum handshake_stage { IDLE, REQUESTED, ACKNOWLEDGED } handshake_stage;
typedef struct handshake {
    os_mutex mu;
    os_cond cv;
    handshake_stage stage;
} handshake;

static void handshake_body(void *arg)
{
    handshake *h = arg;
    os_mutex_lock(&h->mu);
    while (h->stage != REQUESTED) os_cond_wait(&h->cv, &h->mu);
    h->stage = ACKNOWLEDGED;
    os_cond_broadcast(&h->cv);
    os_mutex_unlock(&h->mu);
}

typedef enum acquire_stage { WAITING_TO_START, ACQUIRING, ACQUIRED } acquire_stage;
typedef struct rheld { os_rmutex *m; _Atomic acquire_stage stage; } rheld;
static void rheld_body(void *arg)
{
    rheld *r = arg;
    atomic_store(&r->stage, ACQUIRING);
    os_rmutex_lock(r->m);
    atomic_store(&r->stage, ACQUIRED);
    os_rmutex_unlock(r->m);
}

static os_once once_ctl = OS_ONCE_INIT;
static _Atomic int once_runs;
static void once_body(void) { atomic_fetch_add(&once_runs, 1); os_sleep_ns(MS); }
static void once_thread(void *arg) { (void)arg; os_once_run(&once_ctl, once_body); }

static os_mutex static_mutex = OS_MUTEX_INIT;

static void case_threads(void)
{
    counter_ctx c;
    memset(&c, 0, sizeof c);
    OK(os_mutex_init(&c.mu));
    OK(os_rmutex_init(&c.rmu));
    barrier_init(&c.ids, WORKERS + 1);
    os_thread t[WORKERS];
    for (int i = 0; i < WORKERS; i++) OK(os_thread_start(&t[i], count_body, &c));
    barrier_wait(&c.ids);                        /* all four IDs recorded */
    uint64_t me = os_thread_self();
    CHECK(me != 0);
    for (int i = 0; i < WORKERS; i++) {
        CHECK(c.self[i] != 0 && c.self[i] != me);
        for (int j = 0; j < i; j++) CHECK(c.self[i] != c.self[j]);
    }
    barrier_wait(&c.ids);                        /* release them */
    for (int i = 0; i < WORKERS; i++) OK(os_thread_join(&t[i]));
    CHECK(c.value == WORKERS * INCREMENTS);
    CHECK(c.rvalue == WORKERS * INCREMENTS);
    barrier_destroy(&c.ids);
    os_mutex_destroy(&c.mu);

    /* A recursive mutex is released only by the last unlock. */
    rheld r = { .m = &c.rmu };
    os_rmutex_lock(&c.rmu);
    os_rmutex_lock(&c.rmu);
    os_rmutex_lock(&c.rmu);
    os_thread rt;
    OK(os_thread_start(&rt, rheld_body, &r));
    os_rmutex_unlock(&c.rmu);
    os_rmutex_unlock(&c.rmu);
    acquire_stage stage;
    do {
        stage = atomic_load(&r.stage);
    } while (stage == WAITING_TO_START);
    CHECK(stage == ACQUIRING);
    os_rmutex_unlock(&c.rmu);
    OK(os_thread_join(&rt));
    CHECK(atomic_load(&r.stage) == ACQUIRED);
    os_rmutex_destroy(&c.rmu);

    handshake h;
    memset(&h, 0, sizeof h);
    OK(os_mutex_init(&h.mu));
    OK(os_cond_init(&h.cv));
    os_thread w;
    OK(os_thread_start(&w, handshake_body, &h));
    os_mutex_lock(&h.mu);
    h.stage = REQUESTED;
    os_cond_signal(&h.cv);
    while (h.stage != ACKNOWLEDGED) os_cond_wait(&h.cv, &h.mu);
    os_mutex_unlock(&h.mu);
    OK(os_thread_join(&w));
    os_cond_destroy(&h.cv);
    os_mutex_destroy(&h.mu);

    os_thread o[WORKERS];
    for (int i = 0; i < WORKERS; i++) OK(os_thread_start(&o[i], once_thread, NULL));
    os_once_run(&once_ctl, once_body);
    for (int i = 0; i < WORKERS; i++) OK(os_thread_join(&o[i]));
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

/* The owner waits for a wake through a one-member wait set (v2-0fv.13). */
static bool wake_ready(os_waitset *ws, uint64_t deadline)
{
    os_wait_event ev;
    size_t n = 0;
    OK(os_waitset_wait(ws, deadline, &ev, 1, &n));
    CHECK(n == 0 || (ev.tag == 1 && ev.events == OS_WAIT_IN));
    return n == 1;
}

static void case_wake(void)
{
    os_wake w;
    OK(os_wake_init(&w));
    os_waitset ws = OS_WAITSET_INIT;
    OK(os_waitset_init(&ws));
    OK(os_waitset_add(&ws, os_wake_fd(&w), OS_WAIT_IN, 1));
    CHECK(!wake_ready(&ws, 0));                           /* starts clear */
    uint64_t t0 = os_now_ns();
    CHECK(!wake_ready(&ws, t0 + MS));
    CHECK(os_now_ns() - t0 >= MS);                        /* waited the timeout */
    os_wake_signal(&w);
    os_wake_signal(&w);                                   /* coalesces */
    CHECK(wake_ready(&ws, 0));
    CHECK(wake_ready(&ws, 0));                            /* waiting does not drain */
    os_wake_drain(&w);
    CHECK(!wake_ready(&ws, 0));                           /* one drain clears all */
    os_wake_drain(&w);                                    /* drain when clear: no-op */
    CHECK(!wake_ready(&ws, 0));
    /* Another thread wakes a blocked waiter. */
    wake_ctx c = {&w, MS}; /* Delay to exercise the wait or a pending signal. */
    os_thread t;
    OK(os_thread_start(&t, wake_later, &c));
    CHECK(wake_ready(&ws, OS_DEADLINE_NEVER));
    OK(os_thread_join(&t));
    os_wake_drain(&w);
    os_waitset_remove(&ws, os_wake_fd(&w));
    os_wake_close(&w);
    CHECK(os_wake_fd(&w) == -1);
    EXPECT(os_waitset_add(&ws, os_wake_fd(&w), OS_WAIT_IN, 1), OS_E_INVAL); /* a closed wake */
    os_waitset_close(&ws);
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
    CHECK(fcntl(os_file_fd(&f), F_GETFD) == FD_CLOEXEC);
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

    /* Signed time conversion is observable through the real stat call.
     * One second before the Unix epoch is -1 * 10^9 ns. */
    struct timespec times[] = {{0, UTIME_OMIT}, {-1, 0}};
    CHECK(futimens(os_file_fd(&f), times) == 0);
    OK(os_file_stat(&f, &st));
    CHECK(st.mtime_ns == -1000 * (int64_t)MS);

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
    os_file_close(&c);
    os_file_close(&f);
    OK(os_unlink(clone_path));

    /* Write-only open permits writes and rejects reads on that descriptor. */
    os_file wo = OS_FILE_INIT;
    OK(os_file_open(&wo, path, OS_OPEN_WRITE));
    OK(os_file_pwrite(&wo, "J", 1, 0, &done));
    CHECK(os_file_read(&wo, buf, sizeof buf, &got) != OS_OK);
    OK(os_file_set_mode(&wo, 0200));
    OK(os_file_stat(&wo, &st));
    os_file_close(&wo);
    OK(os_file_open(&wo, path, OS_OPEN_WRITE));
    CHECK(st.perm == 0200);
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
    CHECK(fputs("via stdio", fp) >= 0);
    CHECK(fclose(fp) == 0);
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

    /* (P) Offset 2^32 catches 32-bit truncation and is page-aligned. A
     * positioned write beyond EOF creates a sparse hole. */
    join(path, sizeof path, work_root, "sparse.bin");
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
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
    CHECK(m.addr != NULL);
    const char *v = m.addr;
    CHECK(v[0] == 0 && v[3] == 'f' && v[5] == 'r');
    os_unmap(&m);
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
    CHECK(rom.addr != NULL && memcmp(rom.addr, "old", 3) == 0);
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

    OK(os_dir_sync(&d));

    /* Anonymous file: usable, and its name is gone before the call returns. */
    os_file anon = OS_FILE_INIT;
    os_err an = os_file_open_anon(&anon, &d);
    CHECK(an == OS_OK || an == OS_E_NOTSUP);
    if (an == OS_OK) {
        os_dir_iter it;
        OK(os_dir_iter_open(&it, &d));
        const char *name;
        int n = 0;
        do {
            OK(os_dir_next(&it, &name));
            n += name != NULL;
        } while (name);
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
    /* A second iterator starts at the beginning while the first is at EOF. */
    os_dir_iter other;
    OK(os_dir_iter_open(&other, &d));
    CHECK(list_names(&other, want, 4, seen) == 4);
    os_dir_iter_close(&other);

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

    /* A user's entry is visible and untouched by locking. An empty directory
     * held open and locked can still be removed, its name gone at once. */
    os_dir q = OS_DIR_INIT;
    OK(os_dir_open(&q, p));
    put_at(&q, ".dirlock", "user data");
    OK(os_dir_try_lock(&q, OS_LOCK_EXCLUSIVE));
    os_dir_iter it;
    OK(os_dir_iter_open(&it, &q));
    const char *want[] = {".dirlock"};
    int seen[1];
    CHECK(list_names(&it, want, 1, seen) == 1 && seen[0] == 1);
    os_dir_iter_close(&it);
    CHECK(content_at(&q, ".dirlock", "user data"));
    EXPECT(os_rmdir(p), OS_E_EXIST);
    CHECK(content_at(&q, ".dirlock", "user data"));
    os_dir_unlock(&q);
    CHECK(content_at(&q, ".dirlock", "user data"));
    OK(os_unlink_at(&q, ".dirlock"));
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
    CHECK(ps > 0 && (ps & (ps - 1)) == 0);
    CHECK(gran == ps);
    CHECK(os_phys_mem_bytes() > 0);

    /* Rename-replace over a mapped target keeps the view on the old bytes. */
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));
    size_t len = 3 * ps;
    unsigned char *page = malloc(len);
    CHECK(page != NULL);
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
    CHECK(pat != NULL);
    for (size_t i = 0; i < big; i++) pat[i] = (unsigned char)('a' + (i / gran) + (i % 7));
    OK(os_file_open_at(&f, &d, "offset", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_pwrite(&f, pat, big, 0, &done));
    CHECK(done == big);
    OK(os_map_file(&m, &f, gran, big - gran));
    CHECK(m.addr != NULL && m.len == big - gran);
    CHECK(memcmp(m.addr, pat + gran, big - gran) == 0);
    os_unmap(&m);
    CHECK(m.addr == NULL && m.len == 0);
    EXPECT(os_map_file(&m, &f, 1, ps), OS_E_INVAL); /* Misaligned offset. */
    EXPECT(os_map_file(&m, &f, 0, SIZE_MAX), OS_E_INVAL); /* Beyond signed off_t. */
    CHECK(m.addr == NULL && m.len == 0);

    /* Linux allows a view sized before the file shrank. Only access the
     * first page, which still has backing after the truncate. */
    OK(os_file_set_size(&f, ps));
    OK(os_map_file(&m, &f, 0, 2 * ps));
    CHECK(m.addr != NULL && m.len == 2 * ps);
    CHECK(memcmp(m.addr, pat, ps) == 0);
    os_unmap(&m);
    os_file_close(&f);
    OK(os_unlink_at(&d, "offset"));
    free(pat);

    os_dir_close(&d);
    free(page);

    /* Three pages give the discarded page a neighbour on each side. */
    void *base = NULL;
    EXPECT(os_vm_reserve(&base, 1), OS_E_INVAL);
    CHECK(base == NULL);
    OK(os_vm_reserve(&base, 3 * ps));
    CHECK(base != NULL && ((uintptr_t)base % ps) == 0);
    OK(os_vm_commit(base, 3 * ps));
    unsigned char *b = base;
    memset(b, 0x5a, 3 * ps);
    EXPECT(os_vm_decommit(b + 1, ps), OS_E_INVAL);
    OK(os_vm_decommit(b + ps, ps));
    OK(os_vm_commit(b + ps, ps));
    CHECK(b[ps] == 0 && b[2 * ps - 1] == 0);             /* recommitted pages read zero */
    CHECK(b[0] == 0x5a && b[2 * ps] == 0x5a);            /* neighbours untouched */
    os_vm_release(base, 3 * ps);
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
    /* Latin-1 range, Greek, CJK and a four-byte emoji. */
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
    do {
        OK(os_dir_next(&it, &name));
        if (name) found += strcmp(name, file_name) == 0;
    } while (name);
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
    join(dir_tail, sizeof dir_tail, dir_name, file_name);
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

/* ---- path resolution and symlink semantics ----------------------------- */
static void case_paths(void)
{
    /* Linux's HOST_NAME_MAX excludes the terminating NUL. */
    char hostname[HOST_NAME_MAX + 1];
    OK(os_host_name(hostname, sizeof hostname));
    CHECK(hostname[0] != '\0' && memchr(hostname, '\0', sizeof hostname) != NULL);
    EXPECT(os_host_name(hostname, 1), OS_E_RANGE);

    char cwd[OS_PATH_MAX], relative[OS_PATH_MAX], abs[OS_PATH_MAX], link[OS_PATH_MAX];
    OK(os_cwd(cwd, sizeof cwd));
    size_t prefix = strlen(cwd);
    CHECK(strncmp(work_root, cwd, prefix) == 0 && work_root[prefix] == '/');
    join(relative, sizeof relative, work_root + prefix + 1, "target");
    join(abs, sizeof abs, work_root, "target");
    join(link, sizeof link, work_root, "link");

    CHECK(os_path_is_absolute(abs));
    CHECK(!os_path_is_absolute(relative));
    CHECK(!os_path_is_absolute(""));
    const char payload[] = "data";
    put_path(relative, payload, sizeof payload - 1);
    CHECK(symlink("target", link) == 0);

    os_stat file_stat, followed, nofollow;
    OK(os_stat_path(abs, 0, &file_stat));
    OK(os_stat_path(link, 0, &followed));
    CHECK(os_identity_equal(&file_stat.id, &followed.id));
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, work_root));
    CHECK(fcntl(os_dir_fd(&dir), F_GETFD) == FD_CLOEXEC);
    OK(os_stat_at(&dir, "link", OS_STAT_NOFOLLOW, &nofollow));
    CHECK(nofollow.kind == OS_KIND_LINK);
    CHECK(!os_identity_equal(&file_stat.id, &nofollow.id));

    os_file file = OS_FILE_INIT;
    EXPECT(os_file_open_at(&file, &dir, "link", OS_OPEN_READ | OS_OPEN_NOFOLLOW), OS_E_LOOP);
    CHECK(!os_file_is_open(&file));
    EXPECT(os_file_open_at(&file, &dir, "/target", OS_OPEN_READ), OS_E_INVAL);
    char canonical[OS_PATH_MAX], target[OS_PATH_MAX];
    OK(os_path_canonical(link, canonical, sizeof canonical));
    OK(os_path_canonical(relative, target, sizeof target));
    CHECK(os_path_is_absolute(canonical) && strcmp(canonical, target) == 0);

    OK(os_unlink(relative));
    EXPECT(os_stat_path(link, 0, &followed), OS_E_NOENT);
    EXPECT(os_file_open_at(&file, &dir, "link", OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_WRITE),
           OS_E_EXIST);
    OK(os_unlink_at(&dir, "link"));
    os_dir_close(&dir);
}

/* ---- process identity ------------------------------------------------- */
/* os spawns no processes since v2-0fv.13, so the harness forks: the child runs
 * one role and exits with its result. */
static pid_t fork_role(int (*role)(const char *), const char *arg)
{
    fflush(NULL);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        /* Bound child lifetime even when a failed parent case exits early. */
        alarm(CASE_GUARD_SECONDS);
        _exit(role(arg));
    }
    return pid;
}
static int reap(pid_t pid)
{
    int status = 0;
    pid_t r;
    do r = waitpid(pid, &status, 0); while (r < 0 && errno == EINTR);
    CHECK(r == pid);
    return status;
}
/* Takes an exclusive lock on arg, then sleeps until killed. */
static int role_lock(const char *arg)
{
    os_file f = OS_FILE_INIT;
    if (os_file_open(&f, arg, OS_OPEN_RDWR) != OS_OK) return 2;
    uint64_t guard = os_now_ns() + HANG_GUARD_NS;
    while (os_file_try_lock(&f, OS_LOCK_EXCLUSIVE) != OS_OK) {
        if (os_now_ns() > guard) return 3;
        os_sleep_ns(MS);
    }
    for (;;) os_sleep_ns(1000 * MS);
}
static int role_fdlimit(const char *arg)
{
    (void)arg;
    struct rlimit limit;
    CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
    /* Lower soft by one to exercise an actual raise in the child only. */
    CHECK(limit.rlim_cur > STDERR_FILENO + 1);
    limit.rlim_cur--;
    CHECK(setrlimit(RLIMIT_NOFILE, &limit) == 0);
    uint64_t soft = 0;
    OK(os_fd_limit_raise(&soft));
    CHECK(soft == (uint64_t)limit.rlim_max);
    CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
    CHECK(limit.rlim_cur == limit.rlim_max);
    OK(os_fd_limit_raise(NULL));
    return 0;
}

static void case_proc(void)
{
    char exe[OS_PATH_MAX];
    OK(os_proc_self_path(exe, sizeof exe));
    CHECK(os_path_is_absolute(exe));
    os_stat st;
    OK(os_stat_path(exe, 0, &st));
    CHECK(st.kind == OS_KIND_FILE);
    CHECK(os_proc_id() == (uint64_t)getpid());

    /* A lock held by a process is released when that process dies. */
    char path[OS_PATH_MAX];
    join(path, sizeof path, work_root, "proc.lock");
    os_file f = OS_FILE_INIT;
    OK(os_file_open(&f, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    pid_t locker = fork_role(role_lock, path);
    uint64_t guard = os_now_ns() + HANG_GUARD_NS;
    bool child_holds = false;
    while (!child_holds && os_now_ns() < guard) {
        os_err rc = os_file_try_lock(&f, OS_LOCK_EXCLUSIVE);
        if (rc == OS_E_BUSY) child_holds = true;
        else { if (rc == OS_OK) os_file_unlock(&f); os_sleep_ns(MS); }
    }
    CHECK(child_holds);
    CHECK(kill(locker, SIGKILL) == 0);
    int status = reap(locker);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    OK(os_file_try_lock(&f, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&f);
    os_file_close(&f);
    OK(os_unlink(path));

    status = reap(fork_role(role_fdlimit, NULL));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void check_app_dir(os_app_dir_kind kind, const char *root, const char *suffix)
{
    char actual[OS_PATH_MAX], expected[OS_PATH_MAX];
    join(expected, sizeof expected, root, suffix);
    OK(os_app_dir(kind, actual, sizeof actual));
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "os_test: namespace FAIL (got leaf %s, expected " EDIT_APP_ID ")\n",
                strrchr(actual, '/') + 1);
        exit(EXIT_FAILURE);
    }
    size_t bytes = strlen(expected) + 1; /* Include the terminating NUL. */
    OK(os_app_dir(kind, actual, bytes));
    EXPECT(os_app_dir(kind, actual, bytes - 1), OS_E_RANGE);
}

static void case_known_dirs(void)
{
    char config[OS_PATH_MAX], data[OS_PATH_MAX], runtime[OS_PATH_MAX], home[OS_PATH_MAX];
    join(config, sizeof config, work_root, "config");
    join(data, sizeof data, work_root, "data");
    join(runtime, sizeof runtime, work_root, "runtime");
    join(home, sizeof home, work_root, "home");
    OK(os_mkdir(config));
    OK(os_mkdir(data));
    OK(os_mkdir(runtime));
    OK(os_mkdir(home));
    OK(os_env_set("XDG_CONFIG_HOME", config));
    OK(os_env_set("XDG_DATA_HOME", data));
    OK(os_env_set("XDG_RUNTIME_DIR", runtime));
    OK(os_env_set("HOME", home));

    check_app_dir(OS_APP_CONFIG, config, EDIT_APP_ID);
    check_app_dir(OS_APP_DATA, data, EDIT_APP_ID);

    char trailing[OS_PATH_MAX], out[OS_PATH_MAX];
    join(trailing, sizeof trailing, config, "/");
    OK(os_env_set("XDG_CONFIG_HOME", trailing));
    check_app_dir(OS_APP_CONFIG, config, EDIT_APP_ID);

    OK(os_env_set("XDG_CONFIG_HOME", "relative"));
    OK(os_env_set("XDG_DATA_HOME", ""));
    check_app_dir(OS_APP_CONFIG, home, ".config/" EDIT_APP_ID);
    check_app_dir(OS_APP_DATA, home, ".local/share/" EDIT_APP_ID);
    OK(os_env_set("HOME", "relative"));
    EXPECT(os_app_dir(OS_APP_CONFIG, out, sizeof out), OS_E_NOENT);
    OK(os_env_set("HOME", NULL));
    EXPECT(os_app_dir(OS_APP_DATA, out, sizeof out), OS_E_NOENT);
    EXPECT(os_app_dir((os_app_dir_kind)0, out, sizeof out), OS_E_INVAL);

    OK(os_env_set("TMPDIR", trailing));
    OK(os_temp_dir(out, sizeof out));
    CHECK(strcmp(out, config) == 0);
    OK(os_env_set("TMPDIR", "relative"));
    OK(os_temp_dir(out, sizeof out));
    CHECK(strcmp(out, "/tmp") == 0);

    /* Resolution creates no directories under any of the fixture roots. */
    OK(os_rmdir(home));
    OK(os_rmdir(runtime));
    OK(os_rmdir(data));
    OK(os_rmdir(config));
    puts("os_test: namespace ok");
}

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    {"errors", case_errors},
    {"clocks", case_clocks},
    {"threads", case_threads},
    {"wake", case_wake},
    {"files", case_files},
    {"dirs", case_dirs},
    {"listing", case_listing},
    {"locks", case_locks},
    {"map", case_map},
    {"random", case_random},
    {"utf8", case_utf8},
    {"paths", case_paths},
    {"proc", case_proc},
    /* Last, since it changes this test process's environment. */
    {"known_dirs", case_known_dirs},
};

int main(int argc, char **argv)
{
    (void)argv;
    CHECK(argc == 1);
    char cwd[OS_PATH_MAX];
    OK(os_cwd(cwd, sizeof cwd));
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        /* v1's 120 s kill guard per case is a hang detector. SIGALRM also
         * covers a stuck thread or a timed wait that lost its deadline. */
        alarm(CASE_GUARD_SECONDS);
        const char *name = cases[i].name;
        printf("os_test: %s\n", name);
        fflush(stdout);
        char leaf[sizeof "build/os-known_dirs-XXXXXX"];
        int n = snprintf(leaf, sizeof leaf, "build/os-%s-XXXXXX", name);
        CHECK(n >= 0 && (size_t)n < sizeof leaf);
        join(work_root, sizeof work_root, cwd, leaf);
        CHECK(mkdtemp(work_root) != NULL);
        cases[i].run();
        OK(os_rmdir(work_root));
        alarm(0);
    }
    puts("os_test: ok");
    return EXIT_SUCCESS;
}
