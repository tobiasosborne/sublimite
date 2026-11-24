/* Linux conformance tests for the OS boundary. */
#include "os/os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
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
        int native = errno;
        fprintf(stderr, "os_test.c:%d: FAIL %s -> %d (native %d), expected %d\n",
                line, expression, got, native, want);
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

/* os has no environment setter (v2-0fv.20); fixtures set variables directly. */
static void env_set(const char *name, const char *value)
{
    CHECK((value ? setenv(name, value, 1) : unsetenv(name)) == 0);
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
    char missing[OS_PATH_MAX];
    join(missing, sizeof missing, work_root, "no-such-file");
    os_file f = OS_FILE_INIT;
    CHECK(!os_file_is_open(&f));
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));
    EXPECT(os_file_open_at(&f, &d, "no-such-file", OS_OPEN_READ), OS_E_NOENT);
    CHECK(errno == ENOENT);
    CHECK(!os_file_is_open(&f));
    os_file_close(&f); /* closing a handle that is not open is a no-op */
    os_stat st;
    EXPECT(os_stat_path(missing, 0, &st), OS_E_NOENT);
    /* A directory is not a file. */
    EXPECT(os_file_open_at(&f, &d, ".", OS_OPEN_READ), OS_E_ISDIR);
    /* Exactly one access mode. */
    EXPECT(os_file_open_at(&f, &d, "no-such-file", OS_OPEN_READ | OS_OPEN_WRITE | OS_OPEN_CREATE), OS_E_INVAL);
    EXPECT(os_file_open_at(&f, &d, "no-such-file", OS_OPEN_CREATE), OS_E_INVAL);
    EXPECT(os_file_open_at(&f, &d, "no-such-file", OS_OPEN_READ | OS_OPEN_TRUNC), OS_E_INVAL);
    os_dir_close(&d);
    CHECK(!os_dir_is_open(&d));
    EXPECT(os_dir_open(&d, missing), OS_E_NOENT);
}

/* ---- clocks ------------------------------------------------------------- */
static void case_clocks(void)
{
    uint64_t start = os_now_ns();
    CHECK(start > 0);
    os_sleep_ns(MS); /* Test one millisecond for a nonzero sleep. */
    CHECK(os_now_ns() - start >= MS);
}

/* ---- threads, mutex, cond, once ---------------------------------------- */
/* Each worker takes the lock once per increment. */
enum { WORKERS = 4, INCREMENTS = 10000 };

typedef struct counter_ctx {
    os_mutex mu;
    barrier ids;
    uint64_t value;
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
    barrier_destroy(&c.ids);
    os_mutex_destroy(&c.mu);

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
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, work_root));
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &dir, "files.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    CHECK(os_file_is_open(&f));
    CHECK(fcntl(f.fd, F_GETFD) == FD_CLOEXEC);
    os_file g = OS_FILE_INIT;
    EXPECT(os_file_open_at(&g, &dir, "files.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL), OS_E_EXIST);

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
    /* Signed time conversion is observable through the real stat call.
     * One second before the Unix epoch is -1 * 10^9 ns. */
    struct timespec times[] = {{0, UTIME_OMIT}, {-1, 0}};
    CHECK(futimens(f.fd, times) == 0);
    OK(os_file_stat(&f, &st));
    CHECK(st.mtime_ns == -1000 * (int64_t)MS);

    /* Sequential read from a fresh handle starts at 0. */
    OK(os_file_open_at(&g, &dir, "files.bin", OS_OPEN_READ));
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

    os_file_close(&f);

    /* Write-only open permits writes and rejects reads on that descriptor. */
    os_file wo = OS_FILE_INIT;
    OK(os_file_open_at(&wo, &dir, "files.bin", OS_OPEN_WRITE));
    OK(os_file_pwrite(&wo, "J", 1, 0, &done));
    CHECK(os_file_read(&wo, buf, sizeof buf, &got) != OS_OK);
    os_file_close(&wo);
    CHECK(content_at(&dir, "files.bin", "Jello"));

    /* Truncating open: an existing file is empty as soon as the open returns. */
    OK(os_file_open_at(&wo, &dir, "files.bin", OS_OPEN_WRITE | OS_OPEN_TRUNC));
    OK(os_file_stat(&wo, &st));
    CHECK(st.size == 0);
    OK(os_file_write(&wo, "new", 3, &done));
    os_file_close(&wo);
    CHECK(content_at(&dir, "files.bin", "new"));
    OK(os_unlink_at(&dir, "files.bin"));

    /* Simultaneous positioned I/O on one handle from four threads. */
    OK(os_file_open_at(&f, &dir, "pio.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
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
    OK(os_unlink_at(&dir, "pio.bin"));

    /* (P) Offset 2^32 catches 32-bit truncation and is page-aligned. A
     * positioned write beyond EOF creates a sparse hole. */
    OK(os_file_open_at(&f, &dir, "sparse.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
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
    OK(os_unlink_at(&dir, "sparse.bin"));
    os_dir_close(&dir);
}

/* ---- directory-relative operations ------------------------------------- */
static void case_dirs(void)
{
    char root[OS_PATH_MAX], sub_path[OS_PATH_MAX];
    join(root, sizeof root, work_root, "dirs");
    CHECK(mkdir(root, 0700) == 0);
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, root));
    CHECK(os_dir_is_open(&d));
    join(sub_path, sizeof sub_path, root, "sub");
    CHECK(mkdir(sub_path, 0700) == 0);
    os_dir sub = OS_DIR_INIT;
    OK(os_dir_open(&sub, sub_path));
    os_stat s1, s2;
    OK(os_stat_at(&d, ".", 0, &s1));
    OK(os_stat_path(root, 0, &s2));
    CHECK(s1.kind == OS_KIND_DIR && os_identity_equal(&s1.id, &s2.id));

    put_at(&d, "a", "AAAA");
    put_at(&d, "b", "BB");

    /* NOREPLACE onto an existing name fails and touches neither. */
    EXPECT(os_rename_at(&d, "a", &d, "b", OS_RENAME_NOREPLACE), OS_E_EXIST);
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
    CHECK(fchmod(rof.fd, 0444) == 0);
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
    CHECK(fchmod(roh.fd, 0444) == 0);
    os_file_close(&roh);
    OK(os_unlink_at(&d, "ro"));
    EXPECT(os_stat_at(&d, "ro", 0, &st), OS_E_NOENT);

    /* NOREPLACE onto a free name works; across directories too. */
    OK(os_rename_at(&d, "b", &sub, "moved", OS_RENAME_NOREPLACE));
    CHECK(content_at(&sub, "moved", "AAAA"));
    EXPECT(os_stat_at(&d, "b", 0, &st), OS_E_NOENT);
    OK(os_rename_at(&sub, "moved", &d, "back", OS_RENAME_REPLACE));
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
    os_dir parent = OS_DIR_INIT;
    OK(os_dir_open(&parent, work_root));
    OK(os_rename_at(&parent, "dirs", &parent, "dirs-moved", OS_RENAME_NOREPLACE));
    CHECK(content_at(&d, "back", "AAAA"));
    put_at(&d, "after-move", "x");
    char probe[OS_PATH_MAX];
    join(probe, sizeof probe, moved_root, "after-move");
    OK(os_stat_path(probe, 0, &st));
    OK(os_rename_at(&parent, "dirs-moved", &parent, "dirs", OS_RENAME_NOREPLACE));
    os_dir_close(&parent);

    OK(os_dir_sync(&d));

    /* Anonymous file: usable, and its name is gone before the call returns. */
    os_file anon = OS_FILE_INIT;
    os_err an = os_file_open_anon(&anon, &d);
    CHECK(an == OS_OK || an == OS_E_NOTSUP);
    if (an == OS_OK) {
        struct stat native;
        CHECK(fstat(anon.fd, &native) == 0 && native.st_nlink == 0);
        size_t done = 0, got = 0;
        OK(os_file_pwrite(&anon, "tmp", 3, 0, &done));
        OK(os_file_pread(&anon, buf, sizeof buf, 0, &got));
        CHECK(got == 3 && memcmp(buf, "tmp", 3) == 0);
        os_file_close(&anon);
    }

    OK(os_unlink_at(&d, "back"));
    OK(os_unlink_at(&d, "victim"));
    OK(os_unlink_at(&d, "after-move"));
    CHECK(rmdir(sub_path) == 0);
    os_dir_close(&d);
    CHECK(rmdir(root) == 0);
}

/* ---- locks -------------------------------------------------------------- */
static void case_locks(void)
{
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, work_root));
    os_file a = OS_FILE_INIT, b = OS_FILE_INIT;
    OK(os_file_open_at(&a, &dir, "lock.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0, got = 0;
    OK(os_file_pwrite(&a, "locked data", 11, 0, &done));
    OK(os_file_open_at(&b, &dir, "lock.bin", OS_OPEN_RDWR));

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
    OK(os_file_open_at(&c, &dir, "lock.bin", OS_OPEN_READ));
    EXPECT(os_file_try_lock(&c, OS_LOCK_EXCLUSIVE), OS_E_BUSY);
    os_file_unlock(&a);
    os_file_unlock(&b);
    OK(os_file_try_lock(&c, OS_LOCK_EXCLUSIVE));
    /* Closing the locking handle (no duplicates) releases the lock. */
    os_file_close(&c);
    OK(os_file_try_lock(&a, OS_LOCK_EXCLUSIVE));
    os_file_unlock(&a);

    os_file_close(&a);
    os_file_close(&b);
    OK(os_unlink_at(&dir, "lock.bin"));
    os_dir_close(&dir);
}

/* ---- mapping ------------------------------------------------------------ */
static void case_map(void)
{
    size_t ps = os_page_size();
    CHECK(ps > 0 && (ps & (ps - 1)) == 0);
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

    /* A nonzero, page-aligned offset and a partial last page: the
     * view starts at the offset's byte and ends at the file's last byte. */
    size_t big = 3 * ps + 100;
    unsigned char *pat = malloc(big);
    CHECK(pat != NULL);
    for (size_t i = 0; i < big; i++) pat[i] = (unsigned char)('a' + (i / ps) + (i % 7));
    OK(os_file_open_at(&f, &d, "offset", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_file_pwrite(&f, pat, big, 0, &done));
    CHECK(done == big);
    OK(os_map_file(&m, &f, ps, big - ps));
    CHECK(m.addr != NULL && m.len == big - ps);
    CHECK(memcmp(m.addr, pat + ps, big - ps) == 0);
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

    void *base = NULL;
    EXPECT(os_vm_reserve(&base, 1), OS_E_INVAL);
    CHECK(base == NULL);
    OK(os_vm_reserve(&base, 3 * ps));
    CHECK(base != NULL && ((uintptr_t)base % ps) == 0);
    os_vm_release(base, 3 * ps);
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
    CHECK(mkdir(dir_path, 0700) == 0);
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, dir_path));
    put_at(&d, file_name, "utf8");
    /* Path-based calls accept the same bytes. */
    os_stat st;
    OK(os_stat_path(file_path, 0, &st));
    CHECK(st.kind == OS_KIND_FILE && st.size == 4);
    CHECK(content_at(&d, file_name, "utf8"));
    OK(os_path_canonical(file_path, canon, sizeof canon));
    CHECK(os_path_is_absolute(canon));
    CHECK(ends_with(canon, file_name));
    char dir_tail[OS_PATH_MAX];
    join(dir_tail, sizeof dir_tail, dir_name, file_name);
    CHECK(ends_with(canon, dir_tail));
    EXPECT(os_path_canonical(file_path, canon, 4), OS_E_RANGE);
    /* Environment values are UTF-8 too. */
    char buf[16];
    env_set("OS_TEST_UTF8", file_name);
    EXPECT(os_env_get("OS_TEST_UTF8", buf, 4), OS_E_RANGE);
    char env[OS_PATH_MAX];
    OK(os_env_get("OS_TEST_UTF8", env, sizeof env));
    CHECK(strcmp(env, file_name) == 0);
    env_set("OS_TEST_UTF8", NULL);
    EXPECT(os_env_get("OS_TEST_UTF8", env, sizeof env), OS_E_NOENT);
    OK(os_unlink_at(&d, file_name));
    os_dir_close(&d);
    CHECK(rmdir(dir_path) == 0);
}

/* ---- path resolution and symlink semantics ----------------------------- */
static void case_paths(void)
{
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
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, work_root));
    put_at(&dir, "target", "data");
    CHECK(symlink("target", link) == 0);

    os_stat file_stat, followed, nofollow;
    OK(os_stat_path(abs, 0, &file_stat));
    OK(os_stat_path(link, 0, &followed));
    CHECK(os_identity_equal(&file_stat.id, &followed.id));
    CHECK(fcntl(dir.fd, F_GETFD) == FD_CLOEXEC);
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

    OK(os_unlink_at(&dir, "target"));
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
    os_dir dir = OS_DIR_INIT;
    if (os_dir_open(&dir, arg) != OS_OK) return 2;
    os_file f = OS_FILE_INIT;
    os_err rc = os_file_open_at(&f, &dir, "proc.lock", OS_OPEN_RDWR);
    os_dir_close(&dir);
    if (rc != OS_OK) return 2;
    uint64_t guard = os_now_ns() + HANG_GUARD_NS;
    while (os_file_try_lock(&f, OS_LOCK_EXCLUSIVE) != OS_OK) {
        if (os_now_ns() > guard) return 3;
        os_sleep_ns(MS);
    }
    for (;;) os_sleep_ns(1000 * MS);
}
static void case_proc(void)
{
    /* A lock held by a process is released when that process dies. */
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, work_root));
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &dir, "proc.lock", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    pid_t locker = fork_role(role_lock, work_root);
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
    OK(os_unlink_at(&dir, "proc.lock"));
    os_dir_close(&dir);
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
    CHECK(mkdir(config, 0700) == 0);
    CHECK(mkdir(data, 0700) == 0);
    CHECK(mkdir(runtime, 0700) == 0);
    CHECK(mkdir(home, 0700) == 0);
    env_set("XDG_CONFIG_HOME", config);
    env_set("XDG_DATA_HOME", data);
    env_set("XDG_RUNTIME_DIR", runtime);
    env_set("HOME", home);

    check_app_dir(OS_APP_CONFIG, config, EDIT_APP_ID);
    check_app_dir(OS_APP_DATA, data, EDIT_APP_ID);

    char trailing[OS_PATH_MAX], out[OS_PATH_MAX];
    join(trailing, sizeof trailing, config, "/");
    env_set("XDG_CONFIG_HOME", trailing);
    check_app_dir(OS_APP_CONFIG, config, EDIT_APP_ID);

    env_set("XDG_CONFIG_HOME", "relative");
    env_set("XDG_DATA_HOME", "");
    check_app_dir(OS_APP_CONFIG, home, ".config/" EDIT_APP_ID);
    check_app_dir(OS_APP_DATA, home, ".local/share/" EDIT_APP_ID);
    env_set("HOME", "relative");
    EXPECT(os_app_dir(OS_APP_CONFIG, out, sizeof out), OS_E_NOENT);
    env_set("HOME", NULL);
    EXPECT(os_app_dir(OS_APP_DATA, out, sizeof out), OS_E_NOENT);
    EXPECT(os_app_dir((os_app_dir_kind)0, out, sizeof out), OS_E_INVAL);

    /* Resolution creates no directories under any of the fixture roots. */
    CHECK(rmdir(home) == 0);
    CHECK(rmdir(runtime) == 0);
    CHECK(rmdir(data) == 0);
    CHECK(rmdir(config) == 0);
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
    {"locks", case_locks},
    {"map", case_map},
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
        CHECK(rmdir(work_root) == 0);
        alarm(0);
    }
    puts("os_test: ok");
    return EXIT_SUCCESS;
}
