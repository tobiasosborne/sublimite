/* file_test.c -- P1.7: open, EOL, change detection, durable save. */
#include "file/file.h"
#include "file/file_test.h"
#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"
#include "journal/journal.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static work_pool pool;
static char dir[256];

static void sleep_ms(long ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }

static void path_of(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", dir, name); }

static void write_file(const char *name, const uint8_t *d, size_t n)
{
    char p[512]; path_of(p, sizeof p, name);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    CHECK(fd >= 0);
    size_t off = 0;
    while (off < n) { ssize_t w = write(fd, d + off, n - off); if (w <= 0) break; off += (size_t)w; }
    close(fd);
}

static size_t read_file(const char *name, uint8_t *buf, size_t cap)
{
    char p[512]; path_of(p, sizeof p, name);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return (size_t)-1;
    size_t off = 0; ssize_t r;
    while (off < cap && (r = read(fd, buf + off, cap - off)) > 0) off += (size_t)r;
    close(fd);
    return off;
}

static void fill(uint8_t *b, size_t n, unsigned seed)
{
    uint32_t x = 2463534242u + seed;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        b[i] = (x % 23u == 0) ? '\n' : (uint8_t)('a' + x % 26u);
    }
}

/* ---- mailbox collection ---- */
typedef struct { file_msg m[16]; size_t n; } coll;
static void on_msg(const work_msg *wm, void *ud)
{
    coll *c = ud; file_msg m;
    if (file_msg_decode(wm, &m) == 0 && c->n < 16) c->m[c->n++] = m;
}
/* Wait up to 5 s for a message of `kind`. */
static int wait_msg(uint32_t kind, file_msg *out)
{
    for (int i = 0; i < 5000; i++) {
        coll c = {0};
        (void)work_mailbox_drain(&pool, on_msg, &c);
        for (size_t k = 0; k < c.n; k++) if (c.m[k].kind == kind) { *out = c.m[k]; return 1; }
        sleep_ms(1);
    }
    return 0;
}

static _Atomic int blocker_go;
static _Atomic int blocker_started;
static void blocker(work_ctx *c)
{
    atomic_store(&blocker_started, 1);
    while (!atomic_load(&blocker_go) && !work_should_stop(c)) sleep_ms(1);
}
static work_handle start_blocker(void)
{
    atomic_store(&blocker_go, 0); atomic_store(&blocker_started, 0);
    work_handle h = work_submit(&pool, (work_job){ blocker, NULL, 0, WORK_BULK });
    while (!atomic_load(&blocker_started)) sleep_ms(1);
    return h;
}

static piece_tree *new_tree(void)
{
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    CHECK(t != NULL);
    return t;
}

static int tree_equals(piece_tree *t, const uint8_t *d, size_t n)
{
    if (piece_len(t) != n) return 0;
    uint8_t *b = malloc(n ? n : 1);
    int ok = piece_read(t, 0, b, n) == 0 && (n == 0 || memcmp(b, d, n) == 0);
    free(b);
    return ok;
}

/* ---- EOL ---- */
static file_prefix_info scan(const char *s)
{
    file_prefix_info i; file_prefix_scan((const uint8_t *)s, strlen(s), &i); return i;
}

static void t_eol(void)
{
    file_prefix_info i;
    i = scan(""); CHECK(i.kind == FILE_EOL_NONE && i.dominant == FILE_EOL_LF && !i.has_bom);
    i = scan("abc"); CHECK(i.kind == FILE_EOL_NONE);
    i = scan("a\nb\nc"); CHECK(i.kind == FILE_EOL_LF && i.lf == 2 && i.crlf == 0);
    i = scan("a\r\nb\r\n"); CHECK(i.kind == FILE_EOL_CRLF && i.crlf == 2 && i.lf == 0 && i.dominant == FILE_EOL_CRLF);
    i = scan("a\r\nb\nc\r\n"); CHECK(i.kind == FILE_EOL_MIXED && i.crlf == 2 && i.lf == 1 && i.dominant == FILE_EOL_CRLF);
    i = scan("a\nb\nc\r\n"); CHECK(i.kind == FILE_EOL_MIXED && i.dominant == FILE_EOL_LF);
    i = scan("a\rb\r"); CHECK(i.cr == 2 && i.kind == FILE_EOL_NONE);
    i = scan("\n\n\r\n"); CHECK(i.lf == 2 && i.crlf == 1);
    i = scan("\xEF\xBB\xBFhi\n"); CHECK(i.has_bom && i.kind == FILE_EOL_LF);
    i = scan("\xEF\xBB"); CHECK(!i.has_bom);
}

/* ---- open ---- */
static void t_open_small_copy(void)
{
    uint8_t d[100]; fill(d, sizeof d, 1);
    write_file("small.txt", d, sizeof d);
    char p[512]; path_of(p, sizeof p, "small.txt");
    file *f = NULL;
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0 && f);
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    size_t n = 0; const uint8_t *pre = file_prefix(f, &n);
    CHECK(n == sizeof d && memcmp(pre, d, n) == 0);
    CHECK(file_size(f) == sizeof d && file_open_mode(f) == FILE_MODE_COPY);
    CHECK(file_open_ready(f) == 1);          /* whole file in the prefix: no job */
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == 0);
    CHECK(tree_equals(t, d, sizeof d));
    CHECK(file_attach(f, t) == FILE_ERR_STATE);
    piece_destroy(t); file_close(f);

    uint8_t e[1]; write_file("empty.txt", e, 0);
    path_of(p, sizeof p, "empty.txt");
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    CHECK(file_size(f) == 0 && file_open_ready(f));
    t = new_tree(); CHECK(file_attach(f, t) == 0 && piece_len(t) == 0);
    piece_destroy(t); file_close(f);

    path_of(p, sizeof p, "nonexistent");
    CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_OPEN_FAILED, &m) && m.status == FILE_ERR_IO && m.err_no == ENOENT);
    file_close(f);
    CHECK(file_open_begin(&pool, dir, NULL, &f) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_OPEN_FAILED, &m) && m.status == FILE_ERR_NOTREG);
    file_close(f);
}

static void t_prefix_before_copy(void)
{
    size_t n = 3u << 20;
    uint8_t *d = malloc(n); fill(d, n, 2);
    write_file("big.txt", d, n);
    char p[512]; path_of(p, sizeof p, "big.txt");
    work_handle bh = start_blocker();        /* the single worker is now busy */
    file *f = NULL;
    file_open_opts o = { 1ull << 40, 7 };
    CHECK(file_open_begin(&pool, p, &o, &f) == 0);
    file_msg m; CHECK(wait_msg(FILE_MSG_PREFIX_READY, &m));
    size_t pn = 0; const uint8_t *pre = file_prefix(f, &pn);
    CHECK(pn == FILE_PREFIX_MAX && memcmp(pre, d, pn) == 0);   /* published ... */
    CHECK(file_open_ready(f) == 0);                           /* ... before the copy ran */
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == FILE_ERR_STATE);
    atomic_store(&blocker_go, 1);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    CHECK(m.f == f && m.status == 0 && m.mode == FILE_MODE_COPY && m.size == n && m.generation == 7);
    CHECK(file_open_ready(f));
    CHECK(file_attach(f, t) == 0 && tree_equals(t, d, n));
    piece_destroy(t); file_close(f); free(d);
    work_cancel(&pool, bh);
}

static void t_mmap_threshold(void)
{
    size_t n = 200000;
    uint8_t *d = malloc(n); fill(d, n, 3);
    write_file("thr.txt", d, n);
    char p[512]; path_of(p, sizeof p, "thr.txt");
    file *f; file_msg m;
    file_open_opts o = { n, 1 };                    /* size == threshold -> mmap */
    CHECK(file_open_begin(&pool, p, &o, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.mode == FILE_MODE_MMAP);
    CHECK(file_open_mode(f) == FILE_MODE_MMAP);
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == 0 && tree_equals(t, d, n));
    piece_snapshot *s = piece_snapshot_take(t);
    piece_destroy(t); file_close(f);                 /* snapshot keeps the mapping alive */
    uint8_t *b = malloc(n);
    CHECK(piece_snapshot_read(s, 0, b, n) == 0 && memcmp(b, d, n) == 0);
    piece_snapshot_release(s); free(b);

    o.copy_threshold = n + 1;                        /* size < threshold -> copy */
    CHECK(file_open_begin(&pool, p, &o, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.mode == FILE_MODE_COPY);
    CHECK(file_open_ready(f) && file_open_mode(f) == FILE_MODE_COPY);
    file_close(f);
    CHECK(file_default_copy_threshold() >= (1ull << 20) && file_default_copy_threshold() <= (256ull << 20));

    /* close right after begin (job probably queued/running) must be safe */
    for (int i = 0; i < 20; i++) {
        o.copy_threshold = (i & 1) ? n : (1ull << 40);
        CHECK(file_open_begin(&pool, p, &o, &f) == 0);
        file_close(f);
    }
    free(d);
}

/* ---- change detection ---- */
static void t_change(void)
{
    uint8_t d[5000]; fill(d, sizeof d, 4);
    char p[512]; path_of(p, sizeof p, "chg.txt");
    file *f; uint32_t r; file_msg m;
    write_file("chg.txt", d, sizeof d);
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    CHECK(file_check(f, &r) == 0 && r == FILE_CHG_NONE && !file_changed(f));

    struct timespec ts[2] = { { 1000000000, 0 }, { 1000000000, 0 } };   /* touch: mtime only */
    CHECK(utimensat(AT_FDCWD, p, ts, 0) == 0);
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_MTIME) && !(r & FILE_CHG_SIZE) && file_changed(f));
    CHECK(file_resolve_keep(f) == 0 && !file_changed(f));
    CHECK(file_check(f, &r) == 0);

    CHECK(truncate(p, 100) == 0);                                        /* truncate */
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_SIZE));
    CHECK(file_resolve_keep(f) == 0);

    char tmp[512]; path_of(tmp, sizeof tmp, "chg.new");                  /* replace by rename */
    write_file("chg.new", d, 100);
    CHECK(utimensat(AT_FDCWD, tmp, ts, 0) == 0);
    CHECK(utimensat(AT_FDCWD, p, ts, 0) == 0);
    CHECK(file_resolve_keep(f) == 0);
    CHECK(rename(tmp, p) == 0);
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_INODE));
    CHECK(file_resolve_keep(f) == 0 && !file_changed(f));

    CHECK(unlink(p) == 0);                                               /* delete */
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_GONE));
    file_close(f);

    /* inotify */
    write_file("chg.txt", d, sizeof d);
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    int fd = file_watch_start(f);
    CHECK(fd >= 0);
    CHECK(file_watch_poll(f) == 0);
    int wfd = open(p, O_WRONLY | O_APPEND); CHECK(write(wfd, "x", 1) == 1); close(wfd);
    struct pollfd pf = { fd, POLLIN, 0 };
    CHECK(poll(&pf, 1, 2000) == 1);
    (void)file_watch_poll(f);
    for (unsigned i = 0; i < 5000 && !file_changed(f); i++) {
        coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c); sleep_ms(1);
    }
    CHECK(file_changed(f));
    file_close(f);

    /* mapped inode truncated in place */
    size_t n = 100000; uint8_t *big = malloc(n); fill(big, n, 5);
    write_file("chg.txt", big, n);
    file_open_opts o = { 1, 0 };
    CHECK(file_open_begin(&pool, p, &o, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    CHECK(file_open_mode(f) == FILE_MODE_MMAP);
    CHECK(truncate(p, 10) == 0);
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_TRUNCATED));
    file_close(f); free(big);
}

/* ---- save ---- */
static void t_save_roundtrip(void)
{
    size_t n = 300000; uint8_t *d = malloc(n); fill(d, n, 6);
    write_file("sv.txt", d, n);
    char p[512]; path_of(p, sizeof p, "sv.txt");
    chmod(p, 0640);
    for (int mmapmode = 0; mmapmode < 2; mmapmode++) {
        file *f; file_msg m;
        file_open_opts o = { mmapmode ? 1u : (1ull << 40), 9 };
        CHECK(file_open_begin(&pool, p, &o, &f) == 0);
        if (!file_open_ready(f)) CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
        piece_tree *t = new_tree();
        CHECK(file_attach(f, t) == 0);
        CHECK(piece_insert(t, 10, (const uint8_t *)"HELLO", 5) == 0);
        CHECK(piece_delete(t, 1000, 500, NULL) == 0);
        size_t en = n + 5 - 500;
        uint8_t *e = malloc(en);
        CHECK(piece_read(t, 0, e, en) == 0);
        CHECK(file_save_begin(f, t, 0, 11) == 0);
        CHECK(file_save_busy(f));
        CHECK(file_save_begin(f, t, 0, 12) == FILE_ERR_BUSY);
        CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m));
        CHECK(m.status == 0 && m.generation == 11 && m.size == en);
        uint8_t *g = malloc(en + 10);
        CHECK(read_file("sv.txt", g, en + 10) == en && memcmp(g, e, en) == 0);
        struct stat st; CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0640);
        CHECK(file_check(f, NULL) == 0);       /* our own save is not a change */
        /* second save of an unchanged tree after more edits works */
        CHECK(piece_insert(t, 0, (const uint8_t *)"Z", 1) == 0);
        CHECK(file_save_begin(f, t, 0, 13) == 0 && wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == 0);
        CHECK(read_file("sv.txt", g, en + 10) == en + 1 && g[0] == 'Z');
        free(g);
        piece_destroy(t); file_close(f);
        /* restore for the next mode */
        write_file("sv.txt", d, n); chmod(p, 0640);
        free(e);
    }
    free(d);
}

static void t_save_isolation(void)
{
    size_t n = 200000; uint8_t *d = malloc(n); fill(d, n, 7);
    write_file("iso.txt", d, n);
    char p[512]; path_of(p, sizeof p, "iso.txt");
    file *f; CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    file_msg m; if (!file_open_ready(f)) CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == 0);
    work_handle bh = start_blocker();
    CHECK(file_save_begin(f, t, 0, 1) == 0);               /* ack while the worker is busy */
    CHECK(piece_insert(t, 0, (const uint8_t *)"EDITED", 6) == 0);   /* edit after the ack */
    CHECK(piece_delete(t, 100, 1000, NULL) == 0);
    atomic_store(&blocker_go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == 0);
    uint8_t *g = malloc(n + 10);
    CHECK(read_file("iso.txt", g, n + 10) == n && memcmp(g, d, n) == 0);
    free(g); piece_destroy(t); file_close(f); free(d);
    work_cancel(&pool, bh);
}

static void t_save_changed(void)
{
    uint8_t d[4000]; fill(d, sizeof d, 8);
    write_file("sc.txt", d, sizeof d);
    char p[512]; path_of(p, sizeof p, "sc.txt");
    file *f; file_msg m;
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == 0);
    CHECK(piece_insert(t, 0, (const uint8_t *)"mine", 4) == 0);
    write_file("sc.txt", d, 3999);                           /* someone else changes it */
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    CHECK(file_changed(f));
    uint8_t g[5000];
    CHECK(read_file("sc.txt", g, sizeof g) == 3999);         /* untouched */
    CHECK(file_save_begin(f, t, FILE_SAVE_FORCE, 2) == 0);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == 0);
    CHECK(read_file("sc.txt", g, sizeof g) == 4004 && memcmp(g, "mine", 4) == 0);
    CHECK(!file_changed(f));

    /* change that lands after the begin-time check is caught before rename */
    work_handle bh = start_blocker();
    CHECK(file_save_begin(f, t, 0, 3) == 0);
    write_file("sc.txt", d, 10);
    atomic_store(&blocker_go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    CHECK(read_file("sc.txt", g, sizeof g) == 10);
    piece_destroy(t); file_close(f);
    work_cancel(&pool, bh);
}

/* ---- durability: kill between steps ---- */
static void kill_hook(void *ctx, int step)
{
    if (step == (int)(intptr_t)ctx) kill(getpid(), SIGKILL);
}

static int count_temps(const char *prefix)
{
    DIR *d = opendir(dir); struct dirent *e; int n = 0;
    while (d && (e = readdir(d))) if (strncmp(e->d_name, prefix, strlen(prefix)) == 0) n++;
    if (d) closedir(d);
    return n;
}

static void t_durability(void)
{
    size_t n = 1u << 20; uint8_t *oldc = malloc(n), *newc = malloc(n + 64);
    fill(oldc, n, 10); fill(newc, n + 64, 11);
    char p[512]; path_of(p, sizeof p, "dur.txt");
    uint8_t *g = malloc(n + 128);
    for (int step = FILE_STEP_TEMP_CREATED; step <= FILE_STEP_DIR_SYNCED; step++) {
        write_file("dur.txt", oldc, n);
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            piece_tree *t = new_tree();
            if (piece_init_copy(t, newc, n + 64) != 0) _exit(90);
            piece_snapshot *s = piece_snapshot_take(t);
            file_save_args a = {0};
            a.path = p; a.snap = s; a.mode = 0644;
            a.step = kill_hook; a.step_ctx = (void *)(intptr_t)step;
            int rc = file_save_write(&a);
            _exit(rc == 0 ? 0 : 91);               /* reached only if the kill never fired */
        }
        int ws = 0; CHECK(waitpid(pid, &ws, 0) == pid);
        CHECK(WIFSIGNALED(ws) && WTERMSIG(ws) == SIGKILL);
        size_t got = read_file("dur.txt", g, n + 128);
        int is_old = got == n && memcmp(g, oldc, n) == 0;
        int is_new = got == n + 64 && memcmp(g, newc, n + 64) == 0;
        CHECK(is_old || is_new);                   /* never partial */
        if (step < FILE_STEP_RENAMED) CHECK(is_old); else CHECK(is_new);
        if (step < FILE_STEP_RENAMED) CHECK(count_temps(".dur.txt.edit-") >= 1);   /* stray temp documented */
        /* after the rename the temp name is gone (it became the target) */
        else CHECK(count_temps(".dur.txt.edit-") == 0);
        /* clean stray temps from earlier iterations so counts stay meaningful */
        DIR *d = opendir(dir); struct dirent *e;
        while (d && (e = readdir(d))) if (strncmp(e->d_name, ".dur.txt.edit-", 14) == 0) {
            char q[600]; snprintf(q, sizeof q, "%s/%s", dir, e->d_name); unlink(q);
        }
        if (d) closedir(d);
    }
    /* temps never carry the target's own name; a normal save afterwards works */
    piece_tree *t = new_tree(); CHECK(piece_init_copy(t, newc, n + 64) == 0);
    piece_snapshot *s = piece_snapshot_take(t);
    file_save_args a = {0}; a.path = p; a.snap = s;
    CHECK(file_save_write(&a) == 0 && a.written == n + 64);
    CHECK(count_temps(".dur.txt.edit-") == 0);
    piece_snapshot_release(s); piece_destroy(t);
    free(oldc); free(newc); free(g);
}

static void *release_later(void *arg)
{
    (void)arg; sleep_ms(50); atomic_store(&blocker_go, 1); return NULL;
}

static void t_save_cancel(void)
{
    size_t n = 200000; uint8_t *d = malloc(n); fill(d, n, 12);
    write_file("can.txt", d, n);
    char p[512]; path_of(p, sizeof p, "can.txt");
    file *f; CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
    file_msg m; if (!file_open_ready(f)) CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == 0);
    CHECK(piece_insert(t, 0, (const uint8_t *)"X", 1) == 0);
    work_handle bh = start_blocker();
    CHECK(file_save_begin(f, t, 0, 1) == 0);
    pthread_t th; pthread_create(&th, NULL, release_later, NULL);
    file_close(f);              /* cancels the queued save (waits for the worker to pop it) */
    pthread_join(th, NULL);
    sleep_ms(20);
    uint8_t *g = malloc(n + 10);
    CHECK(read_file("can.txt", g, n + 10) == n && memcmp(g, d, n) == 0);
    CHECK(count_temps(".can.txt.edit-") == 0);
    free(g); piece_destroy(t); free(d);
    work_cancel(&pool, bh);
}


/* ---- P1.7b: SIGBUS on external truncation of a mapped original ---- */
typedef struct { piece_snapshot *s; uint8_t *buf; size_t n; int rc; _Atomic int done; } rd_arg;
static void rd_job(work_ctx *c)
{
    rd_arg *a = c->arg;
    a->rc = piece_snapshot_read(a->s, 0, a->buf, a->n);
    atomic_store(&a->done, 1);
}

static void sigbus_case(int use_worker)
{
    size_t n = 4u << 20, keep = 8192;
    uint8_t *d = malloc(n); fill(d, n, 11);
    write_file("sb.txt", d, n);
    char p[512]; path_of(p, sizeof p, "sb.txt");
    file *f; file_msg m;
    file_open_opts o = { 1, 0 };
    CHECK(file_open_begin(&pool, p, &o, &f) == 0);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    CHECK(file_open_mode(f) == FILE_MODE_MMAP);
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == 0);
    piece_snapshot *s = piece_snapshot_take(t);
    CHECK(truncate(p, (off_t)keep) == 0);                 /* external truncation */
    uint8_t *b = malloc(n);
    if (use_worker) {
        rd_arg a = { s, b, n, -1, 0 };
        work_handle h = work_submit(&pool, (work_job){ rd_job, &a, 0, WORK_BULK });
        for (int i = 0; i < 10000 && !atomic_load(&a.done); i++) sleep_ms(1);
        CHECK(atomic_load(&a.done));
        (void)h;
        CHECK(a.rc == 0);
    } else {
        CHECK(piece_read(t, 0, b, n) == 0);               /* UI thread read */
    }
    CHECK(memcmp(b, d, keep) == 0);                       /* surviving bytes intact */
    int z = 1; for (size_t i = keep; i < n; i++) if (b[i]) { z = 0; break; }
    CHECK(z);                                             /* tail reads as zeros */
    CHECK(file_changed(f));
    uint32_t r = 0;
    CHECK(file_check(f, &r) == 1 && (r & FILE_CHG_TRUNCATED));
    piece_snapshot_release(s); piece_destroy(t); file_close(f);
    free(b); free(d);
}

/* A SIGBUS outside our mappings must still be fatal (handler chains to default). */
static void sigbus_foreign(void)
{
    char p[512]; path_of(p, sizeof p, "fx.txt");
    uint8_t d[8192]; fill(d, sizeof d, 12);
    write_file("fx.txt", d, sizeof d);
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(p, O_RDWR);
        { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { (void)dup2(dn, 2); close(dn); } }
        uint8_t *a = mmap(NULL, 8192, PROT_READ, MAP_SHARED, fd, 0);
        if (fd < 0 || a == MAP_FAILED || ftruncate(fd, 0) != 0) _exit(90);
        volatile uint8_t x = a[4096]; (void)x;
        _exit(91);
    }
    int st = 0; CHECK(waitpid(pid, &st, 0) == pid);
    /* SIGBUS, or ASan's abort report of it; never a clean/recovered exit */
    CHECK(WIFSIGNALED(st) || (WIFEXITED(st) && WEXITSTATUS(st) != 0 && WEXITSTATUS(st) != 90 && WEXITSTATUS(st) != 91));
}

static void t_sigbus(void)
{
    sigbus_foreign();                                   /* before any handler use: baseline */
    sigbus_case(0);
    sigbus_case(1);
    sigbus_foreign();                                   /* after: still chained */
}

/* ---- P1.7c review regressions (kept separate from the kill tests) ---- */
#ifdef FILE_TEST_WRAP
static _Atomic unsigned sysconf_calls, worker_watch_calls;
static unsigned wrong_dir_syncs;
static int check_dir_sync;
static struct stat expected_dir;
static pthread_t ui_thread;
long __real_sysconf(int name);
long __wrap_sysconf(int name);
long __wrap_sysconf(int name)
{
    atomic_fetch_add(&sysconf_calls, 1);
    return __real_sysconf(name);
}
int __real_inotify_add_watch(int fd, const char *path, uint32_t mask);
int __wrap_inotify_add_watch(int fd, const char *path, uint32_t mask);
int __wrap_inotify_add_watch(int fd, const char *path, uint32_t mask)
{
    if (!pthread_equal(pthread_self(), ui_thread)) atomic_fetch_add(&worker_watch_calls, 1);
    return __real_inotify_add_watch(fd, path, mask);
}
int __real_inotify_rm_watch(int fd, int wd);
int __wrap_inotify_rm_watch(int fd, int wd);
int __wrap_inotify_rm_watch(int fd, int wd)
{
    if (!pthread_equal(pthread_self(), ui_thread)) atomic_fetch_add(&worker_watch_calls, 1);
    return __real_inotify_rm_watch(fd, wd);
}
int __real_fsync(int fd);
int __wrap_fsync(int fd);
int __wrap_fsync(int fd)
{
    struct stat st;
    if (check_dir_sync && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
        (st.st_dev != expected_dir.st_dev || st.st_ino != expected_dir.st_ino)) wrong_dir_syncs++;
    return __real_fsync(fd);
}
#endif

typedef struct { _Atomic int arrived, go; int at; } review_pause;
static void review_pause_hook(void *ctx, int step)
{
    review_pause *b = ctx;
    if (step != b->at) return;
    atomic_store(&b->arrived, 1);
    while (!atomic_load(&b->go)) sleep_ms(1);
}
static void review_wait_pause(review_pause *b)
{
    for (int i = 0; i < 5000 && !atomic_load(&b->arrived); i++) {
        coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c); sleep_ms(1);
    }
    CHECK(atomic_load(&b->arrived));
}
static void review_wait_save(file *f)
{
    for (int i = 0; i < 5000 && file_save_busy(f); i++) sleep_ms(1);
    CHECK(!file_save_busy(f));
}
static file *review_map(const char *name, piece_tree **t, size_t n)
{
    uint8_t *d = malloc(n); memset(d, 'x', n);
    for (size_t i = 1; i < n; i += 2) d[i] = '\n';
    write_file(name, d, n); free(d);
    char p[512]; path_of(p, sizeof p, name);
    file *f = NULL; file_msg m;
    file_open_opts o = { 1, 0 };
    CHECK(file_open_begin(&pool, p, &o, &f) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    *t = new_tree(); CHECK(file_attach(f, *t) == FILE_OK);
    return f;
}

static void t_review_ui_publication(void)
{
    size_t n = 2u << 20;
    uint8_t *d = malloc(n); memset(d, 'x', n);
    write_file("publication.txt", d, n); free(d);
    char p[512]; path_of(p, sizeof p, "publication.txt");
    for (unsigned j = 0; j < 32; j++) {
        file *f = NULL; file_open_opts o = {1, j};
        CHECK(file_open_begin(&pool, p, &o, &f) == FILE_OK);
        coll seen = {0};
        while (!file_open_ready(f)) {
            (void)file_check(f, NULL); (void)file_changed(f); (void)file_errno(f);
            (void)work_mailbox_drain(&pool, on_msg, &seen);
        }
        CHECK(seen.n && seen.m[seen.n-1].kind == FILE_MSG_OPEN_READY && seen.m[seen.n-1].status == FILE_OK);
        CHECK(file_errno(f) == 0);
        file_close(f);
    }
    file *f = NULL; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == FILE_OK);
    review_pause b = { .at = FILE_STEP_FSYNCED };
    file_set_step_hook(f, review_pause_hook, &b);
    CHECK(file_save_begin(f, t, 0, 33) == FILE_OK);
    review_wait_pause(&b);
    CHECK(file_watch_start(f) >= 0);
    atomic_store(&b.go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK);
    review_wait_save(f);
    CHECK(file_watch_poll(f) == 0);
#ifdef FILE_TEST_WRAP
    CHECK(atomic_load(&worker_watch_calls) == 0);
#endif
    /* Identity is installed on the UI even while the namespace sync is still
     * pending: observing our replacement must not raise an external change. */
    review_pause after = { .at = FILE_STEP_RENAMED };
    file_set_step_hook(f, review_pause_hook, &after);
    CHECK(file_save_begin(f, t, 0, 34) == FILE_OK);
    review_wait_pause(&after);
    coll replaced = {0}; (void)work_mailbox_drain(&pool, on_msg, &replaced);
    CHECK(file_check(f, NULL) == 0 && !file_changed(f));
    atomic_store(&after.go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK);
    review_wait_save(f);
    piece_destroy(t); file_close(f);
}

static void t_review_late_source_fault(void)
{
    size_t n = 1u << 20;
    piece_tree *t; file *f = review_map("late.txt", &t, n);
    char p[512]; path_of(p, sizeof p, "late.txt");
    int oldfd = open(p, O_RDWR); CHECK(oldfd >= 0);
    file_msg m;
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK);
    review_wait_save(f);
    review_pause b = { .at = FILE_STEP_TEMP_CREATED };
    file_set_step_hook(f, review_pause_hook, &b);
    CHECK(file_save_begin(f, t, 0, 2) == FILE_OK);
    review_wait_pause(&b);
    CHECK(ftruncate(oldfd, 4096) == 0);
    uint8_t byte = 1;
    CHECK(piece_read(t, 4096, &byte, 1) == 0 && byte == 0);
    CHECK(file_check(f, NULL) == 1);
    atomic_store(&b.go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    uint8_t *got = malloc(n);
    CHECK(read_file("late.txt", got, n) == n);
    CHECK(got[8192] == 'x' && got[8193] == '\n');
    CHECK(file_changed(f));
    free(got); close(oldfd); piece_destroy(t); file_close(f);
}

static void t_review_keep_counts(void)
{
    size_t n = 65536;
    piece_tree *t; file *f = review_map("counts.txt", &t, n);
    piece_snapshot *s = piece_snapshot_take(t);
    CHECK(piece_line_count(t) == n / 2 + 1);
    CHECK(piece_snapshot_line_count(s) == n / 2 + 1);
    uint8_t *d = malloc(n); memset(d, 'x', n);
    write_file("counts.txt", d, n);
    CHECK(file_check(f, NULL) == 1);
    /* Accepting this backing would bless stale counts in tree AND snapshot.
     * Until piece supports rebasing, keep must refuse the compromised source. */
    CHECK(file_resolve_keep(f) == FILE_ERR_CHANGED);
    CHECK(file_changed(f));
    CHECK(tree_equals(t, d, n));
    CHECK(piece_line_count(t) == n / 2 + 1); /* witness the frozen-contract conflict */
    piece_snapshot_release(s); piece_destroy(t); file_close(f); free(d);
}

static void t_review_late_source_identity(void)
{
    piece_tree *t; file *f = review_map("source-id.txt", &t, 16384);
    char p[512]; path_of(p, sizeof p, "source-id.txt");
    int oldfd = open(p, O_RDWR); CHECK(oldfd >= 0);
    file_msg m;
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK); review_wait_save(f);
    review_pause b = { .at = FILE_STEP_FSYNCED };
    file_set_step_hook(f, review_pause_hook, &b);
    CHECK(file_save_begin(f, t, 0, 2) == FILE_OK); review_wait_pause(&b);
    CHECK(pwrite(oldfd, "Q", 1, 10) == 1);
    struct timespec ts[2] = { { 1100000000, 0 }, { 1100000000, 0 } };
    CHECK(futimens(oldfd, ts) == 0);
    /* No UI check and no SIGBUS: validate the snapshot's inode itself. */
    atomic_store(&b.go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    uint8_t got[16384]; CHECK(read_file("source-id.txt", got, sizeof got) == sizeof got);
    CHECK(got[10] == 'x');
    close(oldfd); piece_destroy(t); file_close(f);
}

static void t_review_late_sticky_change(void)
{
    write_file("sticky-save.txt", (const uint8_t *)"saved\n", 6);
    char p[512]; path_of(p, sizeof p, "sticky-save.txt");
    file *f = NULL; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == FILE_OK);
    struct stat st; CHECK(stat(p, &st) == 0);
    review_pause b = { .at = FILE_STEP_FSYNCED };
    file_set_step_hook(f, review_pause_hook, &b);
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK); review_wait_pause(&b);
    struct timespec ts[2] = { st.st_atim, { 1100000000, 0 } };
    CHECK(utimensat(AT_FDCWD, p, ts, 0) == 0);
    CHECK(file_check(f, NULL) == 1);
    ts[1] = st.st_mtim; CHECK(utimensat(AT_FDCWD, p, ts, 0) == 0);
    CHECK(file_resolve_keep(f) == FILE_OK); /* clearing changed cannot revive this job */
    atomic_store(&b.go, 1);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    CHECK(stat(p, &st) == 0); /* target was not replaced */
    piece_destroy(t); file_close(f);
}

static void t_review_late_force_fault(void)
{
    piece_tree *t; file *f = review_map("force-fault.txt", &t, 16384);
    char p[512]; path_of(p, sizeof p, "force-fault.txt");
    CHECK(truncate(p, 4096) == 0);
    uint8_t byte = 1;
    CHECK(piece_read(t, 8192, &byte, 1) == 0 && byte == 0);
    CHECK(file_check(f, NULL) == 1);
    review_pause b = { .at = FILE_STEP_TEMP_CREATED };
    file_set_step_hook(f, review_pause_hook, &b);
    CHECK(file_save_begin(f, t, FILE_SAVE_FORCE, 1) == FILE_OK); review_wait_pause(&b);
    /* This generation was already faulted at acknowledgement. Another fault
     * must still invalidate it, even though the old boolean stays true. */
    CHECK(piece_read(t, 4096, &byte, 1) == 0 && byte == 0);
    atomic_store(&b.go, 1);
    file_msg m; CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    struct stat st; CHECK(stat(p, &st) == 0 && st.st_size == 4096);
    piece_destroy(t); file_close(f);
}

typedef struct { char from[512], to[512]; int at, replacement; } review_dir_move;
static void review_move_hook(void *ctx, int step)
{
    review_dir_move *m = ctx;
    if (step != m->at) return;
    CHECK(rename(m->from, m->to) == 0);
    /* A pathname reopen would now encounter this unrelated namespace. */
    if (m->replacement) CHECK(mkdir(m->from, 0700) == 0);
    else CHECK(symlink("/dev/null", m->from) == 0);
}
static void t_review_directory_barrier(void)
{
    for (int scenario = 0; scenario < 3; scenario++) {
        int at = scenario == 0 ? FILE_STEP_TEMP_CREATED : FILE_STEP_RENAMED;
        review_dir_move m = { .at = at, .replacement = scenario == 2 };
        snprintf(m.from, sizeof m.from, "%s/parent-%d", dir, scenario);
        snprintf(m.to, sizeof m.to, "%s/moved-%d", dir, scenario);
        CHECK(mkdir(m.from, 0700) == 0);
#ifdef FILE_TEST_WRAP
        CHECK(stat(m.from, &expected_dir) == 0);
        check_dir_sync = 1; wrong_dir_syncs = 0;
#endif
        char target[600]; snprintf(target, sizeof target, "%s/target", m.from);
        int fd = open(target, O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0); close(fd);
        piece_tree *t = new_tree();
        CHECK(piece_init_copy(t, (const uint8_t *)"saved\n", 6) == 0);
        piece_snapshot *s = piece_snapshot_take(t);
        file_save_args a = {0}; a.path = target; a.snap = s;
        a.step = review_move_hook; a.step_ctx = &m;
        CHECK(file_save_write(&a) == FILE_OK);
#ifdef FILE_TEST_WRAP
        check_dir_sync = 0;
        CHECK(wrong_dir_syncs == 0);
#endif
        snprintf(target, sizeof target, "%s/target", m.to);
        uint8_t bytes[8] = {0}; fd = open(target, O_RDONLY); CHECK(fd >= 0);
        CHECK(read(fd, bytes, sizeof bytes) == 6 && memcmp(bytes, "saved\n", 6) == 0);
        close(fd); piece_snapshot_release(s); piece_destroy(t);
    }
}

static void t_review_old_completions(void)
{
    write_file("pending.txt", (const uint8_t *)"old\n", 4);
    char p[512]; path_of(p, sizeof p, "pending.txt");
    file *f = NULL; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == FILE_OK);
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK);
    /* Completion ownership now returns exclusively through mailbox decode;
     * a second save before receipt stays BUSY. Close still drops the old one. */
    sleep_ms(30);
    CHECK(file_save_begin(f, t, 0, 2) == FILE_ERR_BUSY);
    file_close(f);
    coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c);
    CHECK(c.n == 0); /* no freed file pointer reaches a completion callback */
    piece_destroy(t);
}

static void t_review_handler_page_size(void)
{
    piece_tree *t; file *f = review_map("page-size.txt", &t, 16384);
    char p[512]; path_of(p, sizeof p, "page-size.txt");
    CHECK(truncate(p, 4096) == 0);
#ifdef FILE_TEST_WRAP
    unsigned before = atomic_load(&sysconf_calls);
#endif
    uint8_t byte = 1;
    CHECK(piece_read(t, 8192, &byte, 1) == 0 && byte == 0);
#ifdef FILE_TEST_WRAP
    CHECK(atomic_load(&sysconf_calls) == before);
#endif
    piece_destroy(t); file_close(f);
}

static volatile sig_atomic_t review_bus_calls, review_bus_mask_ok;
static void review_bus_one(int sig)
{
    sigset_t mask; (void)sigprocmask(SIG_SETMASK, NULL, &mask);
    review_bus_mask_ok = sig == SIGBUS && sigismember(&mask, SIGUSR1) == 1
                        && sigismember(&mask, SIGBUS) == 1;
    review_bus_calls++;
}
static void review_bus_info(int sig, siginfo_t *si, void *uc)
{
    review_bus_one(sig);
    if (!si || si->si_signo != SIGBUS || !uc) review_bus_mask_ok = 0;
}
static void review_bus_nodefer(int sig)
{
    sigset_t mask; (void)sigprocmask(SIG_SETMASK, NULL, &mask);
    review_bus_mask_ok = sig == SIGBUS && sigismember(&mask, SIGUSR1) == 1
                        && sigismember(&mask, SIGBUS) == 0;
    review_bus_calls++;
    if (review_bus_calls == 1) (void)raise(SIGBUS);
}
static void review_signal_setup(const char *mode)
{
    struct sigaction sa = {0}; sigemptyset(&sa.sa_mask); sigaddset(&sa.sa_mask, SIGUSR1);
    if (strcmp(mode, "ignore") == 0) { sa.sa_handler = SIG_IGN; sa.sa_flags = SA_SIGINFO; }
    else if (strcmp(mode, "default") == 0) sa.sa_handler = SIG_DFL;
    else if (strcmp(mode, "info") == 0) { sa.sa_sigaction = review_bus_info; sa.sa_flags = SA_SIGINFO; }
    else if (strcmp(mode, "nodefer") == 0) { sa.sa_handler = review_bus_nodefer; sa.sa_flags = SA_NODEFER; }
    else { sa.sa_handler = review_bus_one; if (strcmp(mode, "reset") == 0) sa.sa_flags = (int)SA_RESETHAND; }
    CHECK(sigaction(SIGBUS, &sa, NULL) == 0);
}
static void t_review_signal_child(const char *mode)
{
    piece_tree *t; file *f = review_map("chain.txt", &t, 16384);
    CHECK(raise(SIGBUS) == 0);
    if (strcmp(mode, "default") == 0) _exit(91); /* default must never return */
    if (strcmp(mode, "ignore") != 0)
        CHECK(review_bus_calls == (strcmp(mode, "nodefer") == 0 ? 2 : 1) && review_bus_mask_ok);
    char p[512]; path_of(p, sizeof p, "chain.txt");
    CHECK(truncate(p, 4096) == 0);
    uint8_t byte = 1; CHECK(piece_read(t, 8192, &byte, 1) == 0 && byte == 0);
    if (strcmp(mode, "reset") == 0) { CHECK(raise(SIGBUS) == 0); _exit(92); }
    piece_destroy(t); file_close(f);
}
static void t_review_signal_chaining(void)
{
    const char *modes[] = { "ignore", "default", "mask", "info", "reset", "nodefer" };
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        pid_t pid = fork(); CHECK(pid >= 0);
        if (pid == 0) {
            (void)setenv("FT_SIGNAL", modes[i], 1);
            (void)setenv("FT_CASE", "signal_child", 1);
            execl("/proc/self/exe", "file_test", (char *)NULL); _exit(90);
        }
        int st = 0; CHECK(waitpid(pid, &st, 0) == pid);
        int fatal = strcmp(modes[i], "default") == 0 || strcmp(modes[i], "reset") == 0;
        int ok = fatal ? WIFSIGNALED(st) && WTERMSIG(st) == SIGBUS
                       : WIFEXITED(st) && WEXITSTATUS(st) == 0;
        if (!ok) fprintf(stderr, "signal chain %s: FAIL status=%d\n", modes[i], st);
        CHECK(ok);
    }
}

static void review_run(const char *name, void (*fn)(void))
{
    int before = fails; fn();
    fprintf(stderr, "%s: %s\n", name, before == fails ? "ok" : "FAIL");
}

static void t_review_registry(void)
{
    int ok = file_test_guard_registry();
    CHECK((ok & 1) != 0);
    CHECK((ok & 2) != 0);
}

/* P1.7d: syscall oracles, enabled in the separately linked wrapped suite.
 * Counters, not wall-clock gate measurements, identify foreground work. */
#ifdef FILE_REVIEW_WRAP
static pthread_t latency_ui;
static _Atomic unsigned latency_io, latency_reads, latency_writes;
static _Atomic int latency_count, latency_write_mode;
static int latency_watch_fd = -1;
static unsigned latency_watch_reads;
static _Atomic int latency_temp_error;
static _Atomic int latency_pause_commit, latency_commit_arrived, latency_commit_go;
static _Atomic int latency_status_done, latency_status_immediate;
static void latency_note(void)
{
    if (atomic_load(&latency_count) && pthread_equal(pthread_self(), latency_ui))
        atomic_fetch_add(&latency_io, 1);
}
char *__real_realpath(const char *, char *);
char *__wrap_realpath(const char *p, char *out)
{ latency_note(); return __real_realpath(p, out); }
int __real_open(const char *, int, ...);
int __wrap_open(const char *p, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    latency_note(); return __real_open(p, flags, mode);
}
int __real_openat(int, const char *, int, ...);
int __wrap_openat(int fd, const char *p, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    latency_note();
    int error = atomic_load(&latency_temp_error);
    if (error && (flags & O_CREAT)) { errno = error; return -1; }
    return __real_openat(fd, p, flags, mode);
}
int __real_stat(const char *, struct stat *);
int __wrap_stat(const char *p, struct stat *st)
{ latency_note(); return __real_stat(p, st); }
int __real_fstat(int, struct stat *);
int __wrap_fstat(int fd, struct stat *st)
{ latency_note(); return __real_fstat(fd, st); }
ssize_t __real_pread(int, void *, size_t, off_t);
ssize_t __wrap_pread(int fd, void *p, size_t n, off_t off)
{
    latency_note();
    if (atomic_load(&latency_count) && pthread_equal(pthread_self(), latency_ui))
        atomic_fetch_add(&latency_reads, 1);
    return __real_pread(fd, p, n, off);
}
ssize_t __real_write(int, const void *, size_t);
ssize_t __wrap_write(int fd, const void *p, size_t n)
{
    int mode = atomic_load(&latency_write_mode);
    if (mode) {
        unsigned count = atomic_fetch_add(&latency_writes, 1);
        /* Finite fallbacks make the old infinite-retry bug safely testable. */
        if (mode == 1 && count == 0) return 0;
        if (mode == 2 && count < 8) { errno = EINTR; return -1; }
        if (mode == 1 || mode == 2 || mode == 4) { errno = ENOSPC; return -1; }
        if (mode == 3 && n > 7) n = 7;
    }
    return __real_write(fd, p, n);
}
ssize_t __real_read(int, void *, size_t);
ssize_t __wrap_read(int fd, void *p, size_t n)
{
    if (fd == latency_watch_fd) {
        if (++latency_watch_reads <= 1024 && n >= sizeof(struct inotify_event)) {
            struct inotify_event event = { .mask = latency_watch_reads & 1u ? IN_MODIFY : IN_ATTRIB };
            memcpy(p, &event, sizeof event); return (ssize_t)sizeof event;
        }
        errno = EAGAIN; return -1;
    }
    return __real_read(fd, p, n);
}
int __real_renameat(int, const char *, int, const char *);
int __wrap_renameat(int oldfd, const char *old, int newfd, const char *name)
{
    latency_note();
    if (atomic_load(&latency_pause_commit) && !pthread_equal(pthread_self(), latency_ui)) {
        atomic_store(&latency_commit_arrived, 1);
        while (!atomic_load(&latency_commit_go)) sleep_ms(1);
    }
    return __real_renameat(oldfd, old, newfd, name);
}
static void *latency_release_status(void *ctx)
{
    (void)ctx; sleep_ms(50);
    atomic_store(&latency_status_immediate, atomic_load(&latency_status_done));
    atomic_store(&latency_commit_go, 1); return NULL;
}
static void latency_begin(void)
{
    latency_ui = pthread_self(); atomic_store(&latency_io, 0);
    atomic_store(&latency_reads, 0); atomic_store(&latency_count, 1);
}
static unsigned latency_end(void)
{ atomic_store(&latency_count, 0); return atomic_load(&latency_io); }
#endif

static void t_latency_open(void)
{
    uint8_t bytes[8192]; memset(bytes, '\n', sizeof bytes);
    write_file("latency-open.txt", bytes, sizeof bytes);
    char p[512]; path_of(p, sizeof p, "latency-open.txt");
    file *f = NULL;
#ifdef FILE_REVIEW_WRAP
    latency_begin();
#endif
    CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK && f);
#ifdef FILE_REVIEW_WRAP
    CHECK(latency_end() == 0);
#endif
    /* Completing physical I/O alone must not mutate installed UI state. */
    for (unsigned i = 0; i < 5000; i++) {
        unsigned busy = 0;
        for (uint32_t k = 0; k < WORK_MAX_JOBS; k++) busy += atomic_load(&pool.slots[k].busy) != 0;
        if (!busy) break;
        sleep_ms(1);
    }
    size_t prefix_len = 99;
    CHECK(!file_prefix_ready(f) && !file_open_ready(f));
    CHECK(file_prefix(f, &prefix_len) == NULL && prefix_len == 0 && file_size(f) == 0);
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    file_close(f);
}

static void latency_alarm(int sig) { (void)sig; _exit(99); }
static void t_latency_fifo(void)
{
    char p[512]; path_of(p, sizeof p, "latency.fifo");
    CHECK(mkfifo(p, 0600) == 0);
    /* Fresh child has its own pool: a hung blocking open cannot strand ours. */
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) {
        (void)signal(SIGALRM, latency_alarm); alarm(2);
        work_pool child; if (work_pool_init(&child, 1, 0)) _exit(90);
        file *f = NULL; int rc = file_open_begin(&child, p, NULL, &f);
        if (rc == FILE_ERR_NOTREG) _exit(0);
        if (rc != FILE_OK) _exit(91);
        coll c = {0};
        for (;;) {
            (void)work_mailbox_drain(&child, on_msg, &c);
            if (c.n) _exit(c.m[c.n-1].status == FILE_ERR_NOTREG ? 0 : 92);
            sleep_ms(1);
        }
    }
    int status = 0; CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    file *f = NULL; int rc = file_open_begin(&pool, "/dev/null", NULL, &f);
    if (rc == FILE_OK) {
        file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_FAILED, &m) && m.status == FILE_ERR_NOTREG);
        file_close(f);
    } else CHECK(rc == FILE_ERR_NOTREG);
}

static file *latency_file(const char *name, piece_tree **tree)
{
    char p[512]; path_of(p, sizeof p, name);
    write_file(name, (const uint8_t *)"original\n", 9);
    file *f = NULL; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    if (!file_open_ready(f)) { file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m)); }
    *tree = new_tree(); CHECK(file_attach(f, *tree) == FILE_OK);
    return f;
}
static void t_latency_save(void)
{
    piece_tree *t; file *f = latency_file("latency-save.txt", &t);
    work_handle h = start_blocker();
#ifdef FILE_REVIEW_WRAP
    latency_begin();
#endif
    CHECK(file_save_begin(f, t, 0, 13) == FILE_OK);
    (void)file_changed(f); (void)file_save_busy(f); (void)file_errno(f);
#ifdef FILE_REVIEW_WRAP
    CHECK(latency_end() == 0);
#endif
    atomic_store(&blocker_go, 1);
    file_msg m; CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK);
#ifdef FILE_REVIEW_WRAP
    review_wait_save(f);
    atomic_store(&latency_commit_arrived, 0); atomic_store(&latency_commit_go, 0);
    atomic_store(&latency_status_done, 0); atomic_store(&latency_status_immediate, 0);
    atomic_store(&latency_pause_commit, 1);
    CHECK(file_save_begin(f, t, 0, 14) == FILE_OK);
    for (unsigned i = 0; i < 5000 && !atomic_load(&latency_commit_arrived); i++) {
        coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c); sleep_ms(1);
    }
    CHECK(atomic_load(&latency_commit_arrived));
    pthread_t release; CHECK(pthread_create(&release, NULL, latency_release_status, NULL) == 0);
    latency_begin();
    CHECK(!file_changed(f)); CHECK(file_save_busy(f)); (void)file_errno(f);
    CHECK(latency_end() == 0); atomic_store(&latency_status_done, 1);
    pthread_join(release, NULL); CHECK(atomic_load(&latency_status_immediate));
    atomic_store(&latency_pause_commit, 0);
    CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_OK);
#endif
    piece_destroy(t); file_close(f); work_cancel(&pool, h);
}

static int latency_journal_sync(void *ctx, int fd, bool directory)
{
    unsigned *calls = ctx; (void)directory;
    if (pthread_equal(pthread_self(), pool.threads[0]) == 0) (*calls)++;
    return fsync(fd);
}
static void t_latency_journal(void)
{
    char p[512]; path_of(p, sizeof p, "latency.journal");
    unsigned calls = 0;
    journal_io io = { .ctx = &calls, .sync = latency_journal_sync };
    journal *j = NULL; CHECK(journal_open_with_io(&j, p, &pool, NULL, &io) == JOURNAL_OK);
    /* An untitled checkpoint is sufficient to isolate preparation barriers. */
    uint8_t base[41] = {0};
    journal_base previous = { .path = "" };
    journal_record checkpoint = { JOURNAL_BASE, 1, 0, base, sizeof base };
    journal_save save = {0}; calls = 0;
    CHECK(journal_save_prepare(j, 1, &previous, &checkpoint, 1, &save) == JOURNAL_OK);
    CHECK(calls == 0); /* existing transaction is synchronous: proposal oracle */
    journal_close(j);
}

static _Atomic int latency_close_done, latency_close_was_immediate;
static void *latency_release_close(void *arg)
{
    (void)arg; sleep_ms(50);
    atomic_store(&latency_close_was_immediate, atomic_load(&latency_close_done));
    atomic_store(&blocker_go, 1); return NULL;
}
static void t_latency_close(void)
{
    piece_tree *t; file *f = latency_file("latency-close.txt", &t);
    work_handle h = start_blocker(); CHECK(file_save_begin(f, t, 0, 15) == FILE_OK);
    atomic_store(&latency_close_done, 0); atomic_store(&latency_close_was_immediate, 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, latency_release_close, NULL) == 0);
    file_close(f); atomic_store(&latency_close_done, 1); pthread_join(thread, NULL);
    CHECK(atomic_load(&latency_close_was_immediate));
    piece_destroy(t); work_cancel(&pool, h);
}

typedef struct latency_alloc { size_t bulk_bytes, limit; } latency_alloc;
static void *latency_alloc_fn(void *ctx, size_t n)
{
    latency_alloc *a = ctx; if (n >= a->limit) a->bulk_bytes += n; return malloc(n);
}
static void latency_free_fn(void *ctx, void *p, size_t n)
{ (void)ctx; (void)n; free(p); }
static void t_latency_attach(void)
{
    size_t n = 4u << 20; uint8_t *bytes = malloc(n); CHECK(bytes);
    memset(bytes, '\n', n); write_file("latency-attach.txt", bytes, n);
    char p[512]; path_of(p, sizeof p, "latency-attach.txt");
    file *f = NULL; file_open_opts o = { n+1, 16 }; file_msg m;
    CHECK(file_open_begin(&pool, p, &o, &f) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
    latency_alloc counter = { .limit = n };
    piece_allocator allocator = { &counter, latency_alloc_fn, latency_free_fn };
    piece_tree *t = piece_create(&allocator); CHECK(t);
    CHECK(file_attach(f, t) == FILE_OK);
    CHECK(counter.bulk_bytes == 0);
    CHECK(tree_equals(t, bytes, n));
    piece_snapshot *s = piece_snapshot_take(t); piece_destroy(t); file_close(f);
    uint8_t tail[16]; CHECK(piece_snapshot_read(s, n-sizeof tail, tail, sizeof tail) == PIECE_OK);
    CHECK(memcmp(tail, bytes+n-sizeof tail, sizeof tail) == 0);
    piece_snapshot_release(s); free(bytes);
}

static _Atomic int latency_saturated;
static void latency_saturate(work_ctx *ctx)
{
    work_msg m = { .kind = 123 };
    for (unsigned i = 0; i < WORK_MAILBOX_CAP; i++) CHECK(work_publish(ctx, &m));
    atomic_fetch_add(&latency_saturated, 1);
}
static void latency_fill_mailbox(void)
{
    coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c);
    atomic_store(&latency_saturated, 0);
    for (uint32_t i = 0; i < pool.n_workers; i++) {
        work_class cls = i < pool.n_bulk ? WORK_BULK : WORK_RASTER;
        work_handle h = work_submit(&pool, (work_job){latency_saturate, NULL, 0, cls});
        CHECK(h.epoch);
    }
    while ((uint32_t)atomic_load(&latency_saturated) < pool.n_workers) sleep_ms(1);
}
static void t_latency_terminal(void)
{
    size_t n = 2u << 20; uint8_t *bytes = malloc(n); CHECK(bytes); memset(bytes, 'x', n);
    write_file("latency-terminal.txt", bytes, n); free(bytes);
    char p[512]; path_of(p, sizeof p, "latency-terminal.txt");
    latency_fill_mailbox();
    file *f = NULL; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    sleep_ms(30); /* let the old worker drop its terminal on the full mailbox */
    file_msg m; CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    file_close(f);
    latency_fill_mailbox();
    path_of(p, sizeof p, "latency-terminal-missing");
    CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    sleep_ms(30);
    CHECK(wait_msg(FILE_MSG_OPEN_FAILED, &m) && m.status == FILE_ERR_IO && m.err_no == ENOENT);
    file_close(f);
    piece_tree *t; f = latency_file("latency-terminal-save.txt", &t);
    for (unsigned i = 0; i < 2; i++) {
        latency_fill_mailbox();
#ifdef FILE_REVIEW_WRAP
        atomic_store(&latency_temp_error, i ? ENOSPC : 0);
#endif
        CHECK(file_save_begin(f, t, 0, 24+i) == FILE_OK);
        sleep_ms(30);
        CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m));
#ifdef FILE_REVIEW_WRAP
        CHECK(m.status == (i ? FILE_ERR_IO : FILE_OK)); atomic_store(&latency_temp_error, 0);
#endif
    }
    piece_destroy(t); file_close(f);
}

#ifdef FILE_REVIEW_WRAP
static int latency_write_stop(void *ctx)
{ (void)ctx; return atomic_load(&latency_writes) >= 3; }
#endif
static void t_latency_writes(void)
{
#ifdef FILE_REVIEW_WRAP
    char p[512]; path_of(p, sizeof p, "latency-writes.txt");
    piece_tree *t = new_tree(); uint8_t bytes[64]; memset(bytes, 'w', sizeof bytes);
    CHECK(piece_init_copy(t, bytes, sizeof bytes) == PIECE_OK);
    piece_snapshot *s = piece_snapshot_take(t);
    for (int mode = 1; mode <= 4; mode++) {
        file_save_args a = { .path = p, .snap = s };
        if (mode == 2) a.stop = latency_write_stop;
        atomic_store(&latency_writes, 0); atomic_store(&latency_write_mode, mode);
        int rc = file_save_write(&a); atomic_store(&latency_write_mode, 0);
        if (mode == 1) CHECK(rc == FILE_ERR_IO && a.err_no == EIO && atomic_load(&latency_writes) == 1);
        if (mode == 2) CHECK(rc == FILE_ERR_CANCELLED && atomic_load(&latency_writes) == 3);
        if (mode == 3) {
            uint8_t got[64]; CHECK(rc == FILE_OK && a.written == sizeof bytes);
            CHECK(read_file("latency-writes.txt", got, sizeof got) == sizeof got && !memcmp(got, bytes, sizeof got));
        }
        if (mode == 4) CHECK(rc == FILE_ERR_IO && a.err_no == ENOSPC);
    }
    piece_snapshot_release(s); piece_destroy(t);
#endif
}
static void t_latency_errno(void)
{
#ifdef FILE_REVIEW_WRAP
    piece_tree *t; file *f = latency_file("latency-errno.txt", &t);
    for (unsigned i = 0; i < 3; i++) {
        int error = i ? EACCES : ENOSPC; atomic_store(&latency_temp_error, error);
        if (i == 2) {
            error = ENOSPC; atomic_store(&latency_temp_error, 0);
            atomic_store(&latency_write_mode, 4);
        }
        CHECK(file_save_begin(f, t, 0, 31+i) == FILE_OK);
        file_msg m; CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_IO && m.err_no == error);
        CHECK(file_errno(f) == error); atomic_store(&latency_temp_error, 0);
        atomic_store(&latency_write_mode, 0);
    }
    piece_destroy(t); file_close(f);
#endif
}
static void t_latency_watch(void)
{
    piece_tree *t; file *f = latency_file("latency-watch.txt", &t);
    int fd = file_watch_start(f); CHECK(fd >= 0);
#ifdef FILE_REVIEW_WRAP
    latency_watch_fd = fd; latency_watch_reads = 0; latency_begin();
#endif
    (void)file_watch_poll(f);
#ifdef FILE_REVIEW_WRAP
    CHECK(latency_end() == 0); CHECK(latency_watch_reads <= 1);
    latency_watch_fd = -1;
#endif
    piece_destroy(t); file_close(f);
}

static void t_latency_prefix_boundaries(void)
{
    size_t n = 3u * 16384u; uint8_t *bytes = malloc(n); CHECK(bytes);
    memset(bytes, 'x', n); bytes[0] = 0xEF; bytes[1] = 0xBB; bytes[2] = 0xBF;
    for (size_t i = 16384u; i < n; i += 16384u) { bytes[i-1] = '\r'; bytes[i] = '\n'; }
    bytes[n-1] = '\r'; bytes[27] = '\n';
    write_file("latency-prefix-boundary.txt", bytes, n);
    char p[512]; path_of(p, sizeof p, "latency-prefix-boundary.txt");
    file *f = NULL; file_msg m; CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_OK);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.status == FILE_OK);
    file_prefix_info whole; file_prefix_scan(bytes, n, &whole);
    const file_prefix_info *sliced = file_prefix_info_of(f);
    CHECK(sliced->lf == whole.lf && sliced->crlf == whole.crlf && sliced->cr == whole.cr);
    CHECK(sliced->kind == whole.kind && sliced->dominant == whole.dominant && sliced->has_bom == whole.has_bom);
    file_close(f); free(bytes);
}

static void t_latency_commit_invalidation(void)
{
    piece_tree *t; file *f = latency_file("latency-commit-invalidation.txt", &t);
    char p[512]; path_of(p, sizeof p, "latency-commit-invalidation.txt");
    struct stat before; CHECK(stat(p, &before) == 0);
    review_pause pause = { .at = FILE_STEP_FSYNCED };
    file_set_step_hook(f, review_pause_hook, &pause);
    CHECK(file_save_begin(f, t, 0, 131) == FILE_OK); review_wait_pause(&pause);
    atomic_store(&blocker_go, 0); atomic_store(&blocker_started, 0);
    work_handle h = work_submit(&pool, (work_job){blocker, NULL, 0, WORK_BULK}); CHECK(h.epoch);
    atomic_store(&pause.go, 1);
    for (unsigned i = 0; i < 5000 && !atomic_load(&blocker_started); i++) sleep_ms(1);
    CHECK(atomic_load(&blocker_started));
    coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c); /* authorizes queued commit */
    struct timespec times[2] = { before.st_atim, { 1100000000, 0 } };
    CHECK(utimensat(AT_FDCWD, p, times, 0) == 0); CHECK(file_check(f, NULL) == 1);
    times[1] = before.st_mtim; CHECK(utimensat(AT_FDCWD, p, times, 0) == 0);
    CHECK(file_resolve_keep(f) == FILE_OK);
    atomic_store(&blocker_go, 1);
    file_msg m; CHECK(wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED);
    struct stat after; CHECK(stat(p, &after) == 0 && after.st_ino == before.st_ino);
    CHECK(count_temps(".latency-commit-invalidation.txt.edit-") == 0);
    piece_destroy(t); file_close(f); work_cancel(&pool, h);
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/edit-file-test-XXXXXX", base ? base : "/tmp");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    trace_init();
    const char *signal_mode = getenv("FT_SIGNAL");
    if (signal_mode) review_signal_setup(signal_mode);
#ifdef FILE_TEST_WRAP
    ui_thread = pthread_self();
#endif
    EDIT_ASSERT(work_pool_init(&pool, 1, 1) == 0);

    const char *review_case = getenv("FT_CASE");
    if (review_case) {
        if (strcmp(review_case, "signal_child") == 0 && signal_mode) t_review_signal_child(signal_mode);
        else if (strcmp(review_case, "1") == 0) review_run("finding1", t_review_registry);
        else if (strcmp(review_case, "2") == 0) review_run("finding2", t_review_signal_chaining);
        else if (strcmp(review_case, "3") == 0) review_run("finding3", t_review_ui_publication);
        else if (strcmp(review_case, "4") == 0) {
            review_run("finding4", t_review_late_source_fault);
            review_run("finding4 source identity", t_review_late_source_identity);
            review_run("finding4 invalidated generation", t_review_late_sticky_change);
            review_run("finding4 forced late fault", t_review_late_force_fault);
        }
        else if (strcmp(review_case, "4force") == 0) review_run("finding4 forced late fault", t_review_late_force_fault);
        else if (strcmp(review_case, "5") == 0) review_run("finding5", t_review_keep_counts);
        else if (strcmp(review_case, "7") == 0) review_run("finding7", t_review_directory_barrier);
        else if (strcmp(review_case, "8") == 0) review_run("finding8", t_review_old_completions);
        else if (strcmp(review_case, "9") == 0) review_run("finding9", t_review_handler_page_size);
        else if (strcmp(review_case, "11") == 0) {
            review_run("finding11", t_latency_open);
            review_run("finding11 scan boundaries", t_latency_prefix_boundaries);
        }
        else if (strcmp(review_case, "12") == 0) review_run("finding12", t_latency_fifo);
        else if (strcmp(review_case, "13") == 0) {
            review_run("finding13", t_latency_save);
            review_run("finding13 queued commit invalidation", t_latency_commit_invalidation);
        }
        else if (strcmp(review_case, "14") == 0) review_run("finding14", t_latency_journal);
        else if (strcmp(review_case, "15") == 0) review_run("finding15", t_latency_close);
        else if (strcmp(review_case, "16") == 0) review_run("finding16 copy", t_latency_attach);
        else if (strcmp(review_case, "24") == 0) review_run("finding24", t_latency_terminal);
        else if (strcmp(review_case, "25") == 0) review_run("finding25", t_latency_writes);
        else if (strcmp(review_case, "31") == 0) review_run("finding31", t_latency_errno);
        else if (strcmp(review_case, "32") == 0) review_run("finding32", t_latency_watch);
        else CHECK(0);
        goto finish;
    }

    if (getenv("FT_V")) {
        fprintf(stderr, "run eol\n");
    }
    t_eol();
    if (getenv("FT_V")) {
        fprintf(stderr, "run open_small_copy\n");
    }
    t_open_small_copy();
    if (getenv("FT_V")) {
        fprintf(stderr, "run prefix_before_copy\n");
    }
    t_prefix_before_copy();
    if (getenv("FT_V")) {
        fprintf(stderr, "run mmap_threshold\n");
    }
    t_mmap_threshold();
    if (getenv("FT_V")) {
        fprintf(stderr, "run change\n");
    }
    t_change();
    if (getenv("FT_V")) {
        fprintf(stderr, "run save_roundtrip\n");
    }
    t_save_roundtrip();
    if (getenv("FT_V")) {
        fprintf(stderr, "run save_isolation\n");
    }
    t_save_isolation();
    if (getenv("FT_V")) {
        fprintf(stderr, "run save_changed\n");
    }
    t_save_changed();
    if (getenv("FT_V")) {
        fprintf(stderr, "run save_cancel\n");
    }
    t_save_cancel();
    if (getenv("FT_V")) {
        fprintf(stderr, "run durability\n");
    }
    t_durability();

    if (getenv("FT_V")) {
        fprintf(stderr, "run sigbus\n");
    }
    t_sigbus();

    review_run("finding1", t_review_registry);
    review_run("finding2", t_review_signal_chaining);
    review_run("finding3", t_review_ui_publication);
    review_run("finding4", t_review_late_source_fault);
    review_run("finding4 source identity", t_review_late_source_identity);
    review_run("finding4 invalidated generation", t_review_late_sticky_change);
    review_run("finding4 forced late fault", t_review_late_force_fault);
    review_run("finding5", t_review_keep_counts);
    review_run("finding7", t_review_directory_barrier);
    review_run("finding8", t_review_old_completions);
    review_run("finding9", t_review_handler_page_size);
    review_run("finding11", t_latency_open);
    review_run("finding11 scan boundaries", t_latency_prefix_boundaries);
    review_run("finding12", t_latency_fifo);
    review_run("finding13", t_latency_save);
    review_run("finding13 queued commit invalidation", t_latency_commit_invalidation);
    review_run("finding16 copy", t_latency_attach);
    review_run("finding24", t_latency_terminal);
    review_run("finding25", t_latency_writes);
    review_run("finding31", t_latency_errno);
    review_run("finding32", t_latency_watch);

finish:
    work_pool_shutdown(&pool);
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");
    if (fails) { fprintf(stderr, "file_test: %d FAILED\n", fails); return 1; }
    printf("file_test: ok\n");
    return 0;
}
