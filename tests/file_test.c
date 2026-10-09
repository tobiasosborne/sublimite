/* file_test.c -- P1.7: open, EOL, change detection, durable save. */
#include "file/file.h"
#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
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
    CHECK(file_size(f) == 0 && file_open_ready(f));
    t = new_tree(); CHECK(file_attach(f, t) == 0 && piece_len(t) == 0);
    piece_destroy(t); file_close(f);

    path_of(p, sizeof p, "nonexistent");
    CHECK(file_open_begin(&pool, p, NULL, &f) == FILE_ERR_IO && f == NULL);
    CHECK(file_open_begin(&pool, dir, NULL, &f) == FILE_ERR_NOTREG);
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
    size_t pn = 0; const uint8_t *pre = file_prefix(f, &pn);
    CHECK(pn == FILE_PREFIX_MAX && memcmp(pre, d, pn) == 0);   /* published ... */
    CHECK(file_open_ready(f) == 0);                           /* ... before the copy ran */
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == FILE_ERR_STATE);
    atomic_store(&blocker_go, 1);
    file_msg m;
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
    CHECK(file_open_begin(&pool, p, &o, &f) == 0 && file_open_mode(f) == FILE_MODE_MMAP);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m) && m.mode == FILE_MODE_MMAP);
    piece_tree *t = new_tree();
    CHECK(file_attach(f, t) == 0 && tree_equals(t, d, n));
    piece_snapshot *s = piece_snapshot_take(t);
    piece_destroy(t); file_close(f);                 /* snapshot keeps the mapping alive */
    uint8_t *b = malloc(n);
    CHECK(piece_snapshot_read(s, 0, b, n) == 0 && memcmp(b, d, n) == 0);
    piece_snapshot_release(s); free(b);

    o.copy_threshold = n + 1;                        /* size < threshold -> copy */
    CHECK(file_open_begin(&pool, p, &o, &f) == 0 && file_open_mode(f) == FILE_MODE_COPY);
    CHECK(file_open_ready(f));   /* small copy: no job, no message */
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
    file *f; uint32_t r;
    write_file("chg.txt", d, sizeof d);
    CHECK(file_open_begin(&pool, p, NULL, &f) == 0);
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
    int fd = file_watch_start(f);
    CHECK(fd >= 0);
    CHECK(file_watch_poll(f) == 0);
    int wfd = open(p, O_WRONLY | O_APPEND); CHECK(write(wfd, "x", 1) == 1); close(wfd);
    struct pollfd pf = { fd, POLLIN, 0 };
    CHECK(poll(&pf, 1, 2000) == 1);
    CHECK(file_watch_poll(f) == 1 && file_changed(f));
    file_close(f);

    /* mapped inode truncated in place */
    size_t n = 100000; uint8_t *big = malloc(n); fill(big, n, 5);
    write_file("chg.txt", big, n);
    file_open_opts o = { 1, 0 }; file_msg m;
    CHECK(file_open_begin(&pool, p, &o, &f) == 0 && file_open_mode(f) == FILE_MODE_MMAP);
    CHECK(wait_msg(FILE_MSG_OPEN_READY, &m));
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
    piece_tree *t = new_tree(); CHECK(file_attach(f, t) == 0);
    CHECK(piece_insert(t, 0, (const uint8_t *)"mine", 4) == 0);
    write_file("sc.txt", d, 3999);                           /* someone else changes it */
    CHECK(file_save_begin(f, t, 0, 1) == FILE_ERR_CHANGED);
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

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/edit-file-test-XXXXXX", base ? base : "/tmp");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    trace_init();
    EDIT_ASSERT(work_pool_init(&pool, 1, 0) == 0);

    if (getenv("FT_V")) fprintf(stderr, "run eol\n"); t_eol();
    if (getenv("FT_V")) fprintf(stderr, "run open_small_copy\n"); t_open_small_copy();
    if (getenv("FT_V")) fprintf(stderr, "run prefix_before_copy\n"); t_prefix_before_copy();
    if (getenv("FT_V")) fprintf(stderr, "run mmap_threshold\n"); t_mmap_threshold();
    if (getenv("FT_V")) fprintf(stderr, "run change\n"); t_change();
    if (getenv("FT_V")) fprintf(stderr, "run save_roundtrip\n"); t_save_roundtrip();
    if (getenv("FT_V")) fprintf(stderr, "run save_isolation\n"); t_save_isolation();
    if (getenv("FT_V")) fprintf(stderr, "run save_changed\n"); t_save_changed();
    if (getenv("FT_V")) fprintf(stderr, "run save_cancel\n"); t_save_cancel();
    if (getenv("FT_V")) fprintf(stderr, "run durability\n"); t_durability();

    work_pool_shutdown(&pool);
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");
    if (fails) { fprintf(stderr, "file_test: %d FAILED\n", fails); return 1; }
    printf("file_test: ok\n");
    return 0;
}
