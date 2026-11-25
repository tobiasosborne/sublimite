/* L2 file (bead L2-1): acquisition, fault registration, change
 * detection, the save file half, reload. Real files under build/;
 * fsync/dirsync/fault-register failures injected at the os seam with
 * -Wl,--wrap (Makefile LDFLAGS_file_test), each asserted to have fired.
 * Ported from v1 tests/file_test.c + file_os_cases.inc (harvest §1.6):
 * t_save_*, t_durability (kill at every save step), t_sigbus, EOL and
 * identity cases; v1's compiled-out wrap blocks are live here. */
#include "file/file.h"
#include "file/file_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

static const char *cur_test = "?";
static void check(bool ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "file_test.c:%d: FAIL %s (in %s)\n", line, expr, cur_test);
        exit(EXIT_FAILURE);
    }
}
static const char *only_test; /* argv[1]: run one test by name */
static int tests_run;
#define RUN(fn) do { if (only_test == NULL || strcmp(only_test, #fn) == 0) { cur_test = #fn; fn(); tests_run++; } } while (0)
#define CHECK(x) check((x), #x, __LINE__)

/* ---- os-seam injection -------------------------------------------------- */
static int fail_file_sync, fail_dir_sync, fail_register;
static int register_gate_r = -1; /* MF1: the UI holds a registration at the worker */
static _Atomic int register_blocked;
static unsigned calls_file_sync, calls_dir_sync, calls_register;

os_err __real_os_file_sync(os_file *f);
os_err __wrap_os_file_sync(os_file *f)
{
    calls_file_sync++;
    if (fail_file_sync > 0) { fail_file_sync--; return OS_E_IO; }
    return __real_os_file_sync(f);
}
os_err __real_os_dir_sync(os_dir *d);
os_err __wrap_os_dir_sync(os_dir *d)
{
    calls_dir_sync++;
    if (fail_dir_sync > 0) { fail_dir_sync--; return OS_E_IO; }
    return __real_os_dir_sync(d);
}
os_err __real_os_fault_register(const os_map *v, os_fault_id *o);
os_err __wrap_os_fault_register(const os_map *v, os_fault_id *o)
{
    calls_register++;
    if (register_gate_r >= 0) {
        atomic_store(&register_blocked, 1);
        char g;
        ssize_t n = read(register_gate_r, &g, 1);
        (void)n;
    }
    if (fail_register > 0) {
        fail_register--;
        *o = (os_fault_id)OS_FAULT_ID_INIT;
        return OS_E_NOSPC;
    }
    return __real_os_fault_register(v, o);
}
#define FIRED() CHECK(fail_file_sync == 0 && fail_dir_sync == 0 && fail_register == 0)

/* Round 2 seams (review wave1-L2-1-opus.md). Atomic: the bulk worker
 * reads them while the UI arms them. */
/* EIO / EISDIR: nothing renamed. AWAY: the temp moves to UNCERTAIN_AWAY
 * and EIO is returned, so neither name proves anything (a true
 * uncertain outcome). */
enum { RENAME_PASS = 0, RENAME_EIO, RENAME_EISDIR, RENAME_AWAY, RENAME_AWAY_BLOCK,
       RENAME_FOREIGN, RENAME_BACK };
#define UNCERTAIN_AWAY "uncertain.away"
static _Atomic int rename_inject;
static _Atomic unsigned calls_rename;
static char rename_foreign_path[4096];
static int rename_gate_r = -1;
static void put_file(const char *path, const void *bytes, size_t n);
os_err __real_os_rename_at(const os_dir *a, const char *b, const os_dir *c,
                           const char *d, os_rename_mode m);
os_err __wrap_os_rename_at(const os_dir *a, const char *b, const os_dir *c,
                           const char *d, os_rename_mode m)
{
    atomic_fetch_add(&calls_rename, 1);
    switch (atomic_exchange(&rename_inject, RENAME_PASS)) {
    case RENAME_EIO:
        return OS_E_IO; /* reported, but nothing renamed */
    case RENAME_EISDIR:
        return OS_E_ISDIR;
    case RENAME_AWAY_BLOCK: {
        char g;
        ssize_t n = read(rename_gate_r, &g, 1);
        (void)n;
    }
    /* fall through */
    case RENAME_AWAY:
        (void)__real_os_rename_at(a, b, c, UNCERTAIN_AWAY, OS_RENAME_REPLACE);
        return OS_E_IO;
    case RENAME_FOREIGN:
        put_file(rename_foreign_path, "foreign", 7); /* after the recheck */
        break;
    case RENAME_BACK: {
        /* Review R3-A: renamed for real, then a foreign rename moves the
         * destination back onto our exact temp name, and EIO is reported. */
        os_err e = __real_os_rename_at(a, b, c, d, m);
        if (e != OS_OK) return e;
        (void)__real_os_rename_at(c, d, a, b, OS_RENAME_REPLACE);
        return OS_E_IO;
    }
    default:
        break;
    }
    return __real_os_rename_at(a, b, c, d, m);
}
/* Round 3: a stat of a name that lies once (MF-D, X13), and a read-back
 * during which the file's times move without its bytes (X7). */
static _Atomic int stat_at_lie_on; /* lie on the Nth call from now (1-based) */
os_err __real_os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st);
os_err __wrap_os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st)
{
    os_err e = __real_os_stat_at(dir, name, flags, st);
    int n = atomic_load(&stat_at_lie_on);
    if (n > 0) {
        atomic_store(&stat_at_lie_on, n - 1);
        if (n == 1 && e == OS_OK) st->id.index[0] ^= 1u; /* another inode */
    }
    return e;
}
static char pread_touch_path[4096];
static _Atomic int pread_touch;
os_err __real_os_file_pread(os_file *f, void *buf, size_t n, uint64_t off, size_t *got);
os_err __wrap_os_file_pread(os_file *f, void *buf, size_t n, uint64_t off, size_t *got)
{
    os_err e = __real_os_file_pread(f, buf, n, off, got);
    if (atomic_exchange(&pread_touch, 0)) {
        /* (P) 1000000000 s since the epoch: a time no save writes. */
        struct timespec ts[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
        if (utimensat(AT_FDCWD, pread_touch_path, ts, 0) != 0) _exit(90);
    }
    return e;
}
/* The /proc/self/status read behind os_creation_mask, refused on demand
 * (the child "mask_fallback" arms it before the first read). */
static int openat_refuse_status;
int __real_openat(int dirfd, const char *path, int flags, ...);
int __wrap_openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (openat_refuse_status && strcmp(path, "/proc/self/status") == 0) {
        errno = EACCES;
        return -1;
    }
    return __real_openat(dirfd, path, flags, mode);
}
static _Atomic int poll_taint;
os_err __real_os_fault_poll(os_fault_id id, uint64_t *epoch);
os_err __wrap_os_fault_poll(os_fault_id id, uint64_t *epoch)
{
    os_err e = __real_os_fault_poll(id, epoch);
    if (e == OS_OK && atomic_load(&poll_taint)) *epoch = 1;
    return e;
}
static _Atomic uint64_t watch_gen;   /* fault id generation under watch */
static _Atomic uintptr_t watch_addr; /* mapping under watch */
static _Atomic unsigned watched_retires, watched_unmaps;
void __real_os_fault_retire(os_fault_id id);
void __wrap_os_fault_retire(os_fault_id id)
{
    if (id.gen != 0 && id.gen == atomic_load(&watch_gen)) atomic_fetch_add(&watched_retires, 1);
    __real_os_fault_retire(id);
}
void __real_os_unmap(os_map *m);
void __wrap_os_unmap(os_map *m)
{
    if (m->addr != NULL && (uintptr_t)m->addr == atomic_load(&watch_addr))
        atomic_fetch_add(&watched_unmaps, 1);
    __real_os_unmap(m);
}

/* ---- fixtures ----------------------------------------------------------- */
#define ROOT "build/file-test"

static void mkpath(const char *path) /* mkdir -p for a file path's parents */
{
    char buf[4096];
    CHECK(strlen(path) < sizeof buf);
    strcpy(buf, path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = 0; CHECK(mkdir(buf, 0700) == 0 || errno == EEXIST); *p = '/'; }
}
static void put_file(const char *path, const void *bytes, size_t n)
{
    mkpath(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, bytes, n) == (ssize_t)n);
    CHECK(close(fd) == 0);
}
static size_t get_file(const char *path, uint8_t *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    ssize_t n = read(fd, buf, cap);
    CHECK(n >= 0);
    CHECK(close(fd) == 0);
    return (size_t)n;
}
static bool file_eq(const char *path, const void *bytes, size_t n)
{
    static uint8_t buf[1 << 20];
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if ((uint64_t)st.st_size != n || n > sizeof buf) return false;
    return get_file(path, buf, sizeof buf) == n && memcmp(buf, bytes, n) == 0;
}
static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

/* ---- pool pump ---------------------------------------------------------- */
static file_msg last_msg;
static int last_kind_count[0x10000];
static void on_msg(const work_msg *m, void *ud)
{
    (void)ud;
    file_msg out;
    if (file_msg_decode(m, &out) == 0) {
        last_msg = out;
        if (out.kind < 0x10000u) last_kind_count[out.kind]++;
    }
}
static void pump_until(work_pool *p, bool (*cond)(void *), void *ud, const char *what, int line)
{
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (!cond(ud)) {
        work_mailbox_drain(p, on_msg, NULL);
        if (cond(ud)) break;
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    (void)what; (void)line;
}
#define PUMP_UNTIL(p, cond, ud) pump_until((p), (cond), (ud), #cond, __LINE__)
static bool open_settled(void *ud) { file *f = ud; return file_open_ready(f) || last_msg.kind == FILE_MSG_OPEN_FAILED; }
static bool prepared_or_done(void *ud)
{
    file *f = ud;
    file_save_phase ph = file_save_phase_of(f);
    return ph == FILE_SAVE_PREPARED || (ph == FILE_SAVE_IDLE && !file_save_busy(f));
}
static bool not_busy(void *ud) { return !file_save_busy((file *)ud); }
static bool reload_settled(void *ud)
{ (void)ud; return last_msg.kind == FILE_MSG_RELOAD_READY || last_msg.kind == FILE_MSG_RELOAD_FAILED; }

static _Alignas(64) work_pool pool;
static piece_reserve *reserve;

static void close_file(file *f)
{
    file_close_begin(f);
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (!file_close_ready(f)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    file_close_finish(f);
}

static void open_file(file *f, const char *path, uint64_t threshold)
{
    memset(f, 0, sizeof *f);
    memset(&last_msg, 0, sizeof last_msg);
    file_open_opts opts = { .copy_threshold = threshold, .generation = 7 };
    CHECK(file_open_begin(f, &pool, path, &opts) == FILE_OK);
    PUMP_UNTIL(&pool, open_settled, f);
    CHECK(file_open_ready(f));
}

static piece_tree *text_tree(const char *text)
{
    piece_tree *t;
    CHECK(piece_open_copy(reserve, (const uint8_t *)text, strlen(text), &t) == PIECE_OK);
    return t;
}
static piece_snapshot *snap_of(piece_tree *t)
{
    piece_snapshot *s;
    CHECK(piece_snapshot_take(t, &s) == PIECE_OK);
    return s;
}
static void view_eq(const piece_view *v, const char *text)
{
    size_t n = strlen(text);
    CHECK(piece_length(v) == n);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *p;
    size_t got, off = 0;
    while ((got = piece_iter_next(&it, &p)) != 0) {
        CHECK(off + got <= n && memcmp(p, text + off, got) == 0);
        off += got;
    }
    CHECK(off == n);
}

/* ---- pure pieces -------------------------------------------------------- */
static void t_prefix_scan(void)
{
    file_prefix_info in;
    file_prefix_scan((const uint8_t *)"", 0, &in);
    CHECK(in.kind == FILE_EOL_NONE && !in.has_bom);
    file_prefix_scan((const uint8_t *)"a\r\nb\nc\r", 7, &in);
    CHECK(in.crlf == 1 && in.lf == 1 && in.cr == 1 && in.kind == FILE_EOL_MIXED);
    file_prefix_scan((const uint8_t *)"\xef\xbb\xbf" "x\n", 5, &in);
    CHECK(in.has_bom && in.kind == FILE_EOL_LF && in.dominant == FILE_EOL_LF);
    file_prefix_scan((const uint8_t *)"a\r\nb\r\n", 6, &in);
    CHECK(in.kind == FILE_EOL_CRLF && in.dominant == FILE_EOL_CRLF);
}

static void t_crc(void)
{
    /* CRC-32/ISCSI of "123456789" is 0xE3069283 (the catalogue check). */
    uint32_t c = file_crc32c(0, (const uint8_t *)"1234", 4);
    c = file_crc32c(c, (const uint8_t *)"56789", 5);
    CHECK(c == UINT32_C(0xE3069283));
    CHECK(file_crc32c(0, NULL, 0) == 0);
}

static void t_expectation(void)
{
    const char *path = ROOT "/exp/a.txt";
    put_file(path, "abc", 3);
    file_expectation a, b;
    os_err oe = OS_OK;
    /* GAP 1: a missing path is ABSENT; a failed observation is distinct. */
    CHECK(file_expectation_observe_path(ROOT "/exp/missing.txt", &a, &oe) == FILE_OK);
    CHECK(a.presence == FILE_ABSENT);
    CHECK(file_expectation_observe_path(path, &b, &oe) == FILE_OK);
    CHECK(b.presence == FILE_PRESENT && b.stat.size == 3);
    CHECK(file_expectation_diff(&b, &b) == FILE_CHG_NONE);
    CHECK((file_expectation_diff(&a, &b) & FILE_CHG_GONE) != 0);
    CHECK(file_expectation_diff(&a, &a) == FILE_CHG_NONE);
    file_expectation c = b;
    c.stat.size = 4;
    CHECK((file_expectation_diff(&b, &c) & FILE_CHG_SIZE) != 0);
    c = b; c.stat.id.index[0]++;
    CHECK((file_expectation_diff(&b, &c) & FILE_CHG_INODE) != 0);
    c = b; c.stat.id.index[1]++;
    CHECK((file_expectation_diff(&b, &c) & FILE_CHG_INODE) != 0);
    c = b; c.stat.ctime_ns++;
    CHECK(file_expectation_diff(&b, &c) != FILE_CHG_NONE);
    /* A path through a regular file names nothing: ABSENT (the
     * missing-component policy). A leaf past NAME_MAX is a failed
     * observation instead: IO, and the output is untouched. */
    CHECK(file_expectation_observe_path(ROOT "/exp/a.txt/child", &a, &oe) == FILE_OK);
    CHECK(a.presence == FILE_ABSENT);
    char longpath[4096];
    strcpy(longpath, ROOT "/exp/");
    memset(longpath + strlen(longpath), 'x', 300);
    longpath[strlen(ROOT "/exp/") + 300] = '\0';
    file_expectation sentinel = b;
    CHECK(file_expectation_observe_path(longpath, &sentinel, &oe) == FILE_ERR_IO);
    CHECK(memcmp(&sentinel, &b, sizeof b) == 0);
}

/* ---- acquisition -------------------------------------------------------- */
static file f1;

static void t_open_copy(void)
{
    const char *path = ROOT "/open/copy.txt";
    put_file(path, "hello\r\nworld\n", 13);
    open_file(&f1, path, 0);
    CHECK(file_open_mode(&f1) == FILE_MODE_COPY);
    CHECK(file_size(&f1) == 13);
    size_t n;
    const uint8_t *px = file_prefix(&f1, &n);
    CHECK(n == 13 && memcmp(px, "hello\r\nworld\n", 13) == 0);
    CHECK(file_prefix_info_of(&f1)->kind == FILE_EOL_MIXED);
    char canon[4096];
    CHECK(realpath(path, canon) != NULL && strcmp(file_path(&f1), canon) == 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    view_eq(piece_tree_view(t), "hello\r\nworld\n");
    CHECK(piece_view_backing(piece_tree_view(t)) == NULL);
    piece_tree *again = NULL;
    CHECK(file_open_tree(&f1, reserve, &again) == FILE_ERR_STATE && again == NULL);
    piece_close(t);
    close_file(&f1);
}

static void t_open_mapped(void)
{
    const char *path = ROOT "/open/map.txt";
    put_file(path, "mapped bytes here", 17);
    open_file(&f1, path, 1); /* threshold 1: at-or-above maps */
    CHECK(file_open_mode(&f1) == FILE_MODE_MMAP);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    view_eq(piece_tree_view(t), "mapped bytes here");
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    CHECK(b != NULL && b->fault.gen != 0);
    os_fault_id fid = b->fault; /* a value copy outlives the backing */
    uint64_t epoch = 99;
    CHECK(os_fault_poll(fid, &epoch) == OS_OK && epoch == 0);
    /* The undo validator accepts the live backing, Law-2 clean. */
    CHECK(file_backing_validate(&f1, b));
    piece_close(t);
    close_file(&f1);
    /* Final release retired the registration exactly once. */
    CHECK(os_fault_poll(fid, &epoch) == OS_E_NOENT);
}

static void t_open_missing_and_dir(void)
{
    memset(&f1, 0, sizeof f1);
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_open_begin(&f1, &pool, ROOT "/open/nope.txt", NULL) == FILE_OK);
    PUMP_UNTIL(&pool, open_settled, &f1);
    CHECK(last_msg.kind == FILE_MSG_OPEN_FAILED && last_msg.status == FILE_ERR_IO);
    CHECK(last_msg.oserr == OS_E_NOENT);
    close_file(&f1);
    CHECK(mkdir(ROOT "/open", 0700) == 0 || errno == EEXIST);
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_open_begin(&f1, &pool, ROOT "/open", NULL) == FILE_OK);
    PUMP_UNTIL(&pool, open_settled, &f1);
    CHECK(last_msg.kind == FILE_MSG_OPEN_FAILED && last_msg.status == FILE_ERR_NOTREG);
    close_file(&f1);
}

static void t_open_nospc(void)
{
    /* Q12: registration exhaustion refuses the mapped open visibly and
     * disposes the unpublished map/fd; a later retry succeeds. */
    const char *path = ROOT "/open/nospc.txt";
    put_file(path, "needs a slot", 12);
    fail_register = 1;
    calls_register = 0;
    memset(&f1, 0, sizeof f1);
    memset(&last_msg, 0, sizeof last_msg);
    file_open_opts opts = { .copy_threshold = 1, .generation = 3 };
    CHECK(file_open_begin(&f1, &pool, path, &opts) == FILE_OK);
    PUMP_UNTIL(&pool, open_settled, &f1);
    CHECK(last_msg.kind == FILE_MSG_OPEN_FAILED && last_msg.status == FILE_ERR_NOSPC);
    CHECK(calls_register == 1);
    FIRED();
    close_file(&f1);
    open_file(&f1, path, 1);
    CHECK(file_open_mode(&f1) == FILE_MODE_MMAP);
    close_file(&f1);
}

/* ---- change detection --------------------------------------------------- */
static void t_change_in_place(void)
{
    /* Same-size in-place overwrite, no SIGBUS: identity/timestamps and
     * the validator must catch it, even with mtime restored (ctime). */
    const char *path = ROOT "/chg/inplace.txt";
    put_file(path, "AAAAAAAA", 8);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    CHECK(!file_check(&f1, NULL) && !file_changed(&f1));
    /* Overwrite in place, then restore the original mtime. */
    int fd = open(path, O_WRONLY);
    CHECK(fd >= 0 && write(fd, "BBBBBBBB", 8) == 8 && close(fd) == 0);
    struct timespec ts[2];
    ts[0].tv_sec = ts[1].tv_sec = (time_t)(file_baseline(&f1)->stat.mtime_ns / 1000000000);
    ts[0].tv_nsec = ts[1].tv_nsec = (long)(file_baseline(&f1)->stat.mtime_ns % 1000000000);
    CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
    uint32_t reasons = 0;
    CHECK(file_check(&f1, &reasons) && file_changed(&f1));
    CHECK(reasons != 0);
    CHECK(!file_backing_validate(&f1, b));
    /* Keep must refuse: the mapped bytes are no longer the baseline. */
    CHECK(file_resolve_keep(&f1) == FILE_ERR_CHANGED);
    piece_close(t);
    close_file(&f1);
}

static void t_change_replace_keep(void)
{
    /* Replacement by rename. A copy backing keeps any disk state. A
     * mapped one cannot: the rename moved our inode's ctime (its link
     * dropped), which no observation tells apart from an in-place
     * rewrite with mtime restored (round 2, MF6), so Keep refuses with
     * the baseline preserved and the caller reloads. */
    const char *path = ROOT "/chg/repl.txt";
    for (uint64_t threshold = 0; threshold <= 1; threshold++) {
        put_file(path, "original", 8);
        open_file(&f1, path, threshold);
        piece_tree *t;
        CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
        file_expectation before = *file_baseline(&f1);
        put_file(ROOT "/chg/repl.new", "replaced!", 9);
        CHECK(rename(ROOT "/chg/repl.new", path) == 0);
        CHECK(file_check(&f1, NULL));
        view_eq(piece_tree_view(t), "original"); /* our inode is intact */
        if (threshold == 0) {
            CHECK(file_resolve_keep(&f1) == FILE_OK);
            CHECK(!file_changed(&f1));
        } else {
            CHECK(file_resolve_keep(&f1) == FILE_ERR_CHANGED);
            CHECK(file_changed(&f1));
            CHECK(memcmp(file_baseline(&f1), &before, sizeof before) == 0);
        }
        piece_close(t);
        close_file(&f1);
    }
}

static void t_sigbus(void)
{
    /* Truncation under a mapping: the fault path (zeroes) is detected
     * as a change, and the zeroes are never adopted as text. */
    const char *path = ROOT "/chg/trunc.txt";
    static uint8_t big[20000];
    memset(big, 'z', sizeof big);
    put_file(path, big, sizeof big);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    CHECK(truncate(path, 100) == 0);
    /* Touch a byte past the new EOF: recovered as zero, epoch rises. */
    volatile uint8_t touch = ((const volatile uint8_t *)b->bytes)[15000];
    CHECK(touch == 0);
    uint64_t epoch = 0;
    CHECK(os_fault_poll(b->fault, &epoch) == OS_OK && epoch != 0);
    uint32_t reasons = 0;
    CHECK(file_check(&f1, &reasons));
    CHECK((reasons & (FILE_CHG_FAULTED | FILE_CHG_TRUNCATED | FILE_CHG_SIZE)) != 0);
    CHECK(!file_backing_validate(&f1, b));
    CHECK(file_resolve_keep(&f1) == FILE_ERR_CHANGED);
    piece_close(t);
    close_file(&f1);
}

static void t_reload(void)
{
    const char *path = ROOT "/chg/reload.txt";
    put_file(path, "version one", 11);
    open_file(&f1, path, 1);
    piece_tree *t1;
    CHECK(file_open_tree(&f1, reserve, &t1) == FILE_OK);
    /* Replace by rename (new inode): old readers keep their bytes. */
    put_file(ROOT "/chg/reload.new", "version two!", 12);
    CHECK(rename(ROOT "/chg/reload.new", path) == 0);
    CHECK(file_check(&f1, NULL));
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_reload_begin(&f1, 9) == FILE_OK);
    PUMP_UNTIL(&pool, reload_settled, &f1);
    CHECK(last_msg.kind == FILE_MSG_RELOAD_READY);
    piece_tree *t2;
    CHECK(file_reload_tree(&f1, reserve, &t2) == FILE_OK);
    view_eq(piece_tree_view(t2), "version two!");
    view_eq(piece_tree_view(t1), "version one"); /* old readers survive */
    CHECK(!file_changed(&f1));
    piece_close(t1);
    piece_close(t2);
    close_file(&f1);
}

static void t_reload_nospc(void)
{
    /* Q12 on reload: refused visibly, old tree and registration intact. */
    const char *path = ROOT "/chg/reload2.txt";
    put_file(path, "still here ok", 13);
    open_file(&f1, path, 1);
    piece_tree *t1;
    CHECK(file_open_tree(&f1, reserve, &t1) == FILE_OK);
    os_fault_id old_id = piece_view_backing(piece_tree_view(t1))->fault;
    put_file(ROOT "/chg/reload2.new", "changed on disk", 15);
    CHECK(rename(ROOT "/chg/reload2.new", path) == 0);
    fail_register = 1;
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_reload_begin(&f1, 9) == FILE_OK);
    PUMP_UNTIL(&pool, reload_settled, &f1);
    CHECK(last_msg.kind == FILE_MSG_RELOAD_FAILED && last_msg.status == FILE_ERR_NOSPC);
    FIRED();
    uint64_t epoch = 7;
    CHECK(os_fault_poll(old_id, &epoch) == OS_OK && epoch == 0);
    view_eq(piece_tree_view(t1), "still here ok");
    piece_close(t1);
    close_file(&f1);
}

static void t_watch(void)
{
    const char *path = ROOT "/chg/watch.txt";
    put_file(path, "watched", 7);
    open_file(&f1, path, 0);
    CHECK(file_watch_start(&f1) == FILE_OK);
    CHECK(file_watch_fd(&f1) >= 0);
    CHECK(!file_watch_poll(&f1));
    put_file(path, "watched!", 8);
    uint64_t deadline = os_now_ns() + UINT64_C(10000000000);
    bool seen = false;
    while (os_now_ns() < deadline && !(seen = file_watch_poll(&f1)))
        os_sleep_ns(UINT64_C(1000000));
    CHECK(seen && file_changed(&f1));
    file_watch_stop(&f1);
    CHECK(file_watch_fd(&f1) == -1);
    close_file(&f1);
}

/* ---- save: synchronous core and seam GAPs 2+3 --------------------------- */
static int steps_seen[8];
static void record_step(void *ctx, int step)
{ (void)ctx; if (step >= 1 && step < 8) steps_seen[step]++; }

static void t_save_sync(void)
{
    const char *path = ROOT "/save/sync.txt";
    put_file(path, "before", 6);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("after the save");
    piece_snapshot *s = snap_of(t);
    file_test_set_step_hook(NULL, record_step, NULL);
    memset(steps_seen, 0, sizeof steps_seen);
    /* Typing on the live tree after the snapshot is not in the save. */
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"XX", 2) == PIECE_OK);
    file_witness w;
    file_save_outcome out;
    CHECK(file_save_write(path, s, NULL, &exp, 0, &w, &out) == FILE_OK);
    CHECK(out.pub == FILE_PUB_VERIFIED);
    CHECK(file_eq(path, "after the save", 14));
    for (int step = 1; step <= 6; step++) CHECK(steps_seen[step] == 1);
    /* Witness names the installed inode, its size and S's CRC32C. */
    file_expectation now;
    CHECK(file_expectation_observe_path(path, &now, &oe) == FILE_OK);
    CHECK(w.volume == now.stat.id.volume && w.index_low == now.stat.id.index[0]);
    CHECK(w.index_high == now.stat.id.index[1] && w.size == 14);
    CHECK(w.content_crc32c == file_crc32c(0, (const uint8_t *)"after the save", 14));
    CHECK(out.installed.presence == FILE_PRESENT);
    CHECK(out.installed.stat.id.index[0] == now.stat.id.index[0]);
    view_eq(piece_snapshot_view(s), "after the save");
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_absent_race(void)
{
    /* Absent target + a file created after the absence check is never
     * overwritten: NOREPLACE gives EXIST -> CHANGED, PRE_RENAME. */
    const char *path = ROOT "/save/absent.txt";
    (void)unlink(path);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    CHECK(exp.presence == FILE_ABSENT);
    piece_tree *t = text_tree("ours");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    file_test_set_step_hook(NULL, NULL, NULL);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    CHECK(tmp.state == FILE_TEMP_SYNCED);
    put_file(path, "foreign", 7); /* created between check and rename */
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_CHANGED);
    CHECK(out.pub == FILE_PUB_PRE_RENAME);
    CHECK(file_eq(path, "foreign", 7));
    CHECK(tmp.state == FILE_TEMP_DONE);
    piece_snapshot_release(s);
    piece_close(t);
    /* And the plain absent case installs. */
    (void)unlink(path);
    t = text_tree("fresh");
    s = snap_of(t);
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    CHECK(file_save_write(path, s, NULL, &exp, 0, NULL, &out) == FILE_OK);
    CHECK(out.pub == FILE_PUB_VERIFIED && file_eq(path, "fresh", 5));
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_changed_refused(void)
{
    const char *path = ROOT "/save/changed.txt";
    put_file(path, "expected", 8);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    put_file(path, "someone else", 12);
    piece_tree *t = text_tree("mine");
    piece_snapshot *s = snap_of(t);
    file_save_outcome out;
    CHECK(file_save_write(path, s, NULL, &exp, 0, NULL, &out) == FILE_ERR_CHANGED);
    CHECK(out.pub == FILE_PUB_PRE_RENAME);
    CHECK(file_eq(path, "someone else", 12));
    /* FORCE publishes over the change. */
    CHECK(file_save_write(path, s, NULL, &exp, FILE_SAVE_FORCE, NULL, &out) == FILE_OK);
    CHECK(file_eq(path, "mine", 4));
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_fsync_fails(void)
{
    const char *path = ROOT "/save/fsync.txt";
    put_file(path, "stable", 6);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("new text");
    piece_snapshot *s = snap_of(t);
    fail_file_sync = 1;
    calls_file_sync = 0;
    file_save_outcome out;
    CHECK(file_save_write(path, s, NULL, &exp, 0, NULL, &out) == FILE_ERR_IO);
    CHECK(out.pub == FILE_PUB_PRE_RENAME);
    CHECK(calls_file_sync >= 1);
    FIRED();
    CHECK(file_eq(path, "stable", 6));
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_dirsync_retry(void)
{
    /* GAP 3: renamed but the directory barrier failed -> INSTALLED_
     * UNSYNCED; the barrier-only retry on the original handle verifies
     * without renaming again (the inode must not change). */
    const char *path = ROOT "/save/dir.txt";
    put_file(path, "old", 3);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("installed text");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    fail_dir_sync = 1;
    calls_dir_sync = 0;
    unsigned renames = atomic_load(&calls_rename);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_DIRSYNC);
    CHECK(out.pub == FILE_PUB_INSTALLED_UNSYNCED);
    CHECK(tmp.state == FILE_TEMP_INSTALLED_UNSYNCED);
    CHECK(file_eq(path, "installed text", 14));
    uint64_t inode = out.installed.stat.id.index[0];
    CHECK(file_temp_retry_barrier(&tmp, &out) == FILE_OK);
    CHECK(out.pub == FILE_PUB_VERIFIED);
    CHECK(out.installed.stat.id.index[0] == inode); /* no second rename */
    CHECK(atomic_load(&calls_rename) == renames + 1);
    CHECK(tmp.state == FILE_TEMP_HELD);
    file_temp_discard(&tmp);
    CHECK(calls_dir_sync == 2);
    FIRED();
    piece_snapshot_release(s);
    piece_close(t);
}

static void foreign_after_rename(void *ctx, int step)
{
    if (step == FILE_STEP_RENAMED) {
        const char *path = ctx;
        char alt[4096];
        strcpy(alt, path);
        strcat(alt, ".foreign");
        put_file(alt, "foreign!", 8);
        CHECK(rename(alt, path) == 0);
    }
}

static void t_save_foreign_after_rename(void)
{
    /* A23 T5 detection half: a foreign rename over our installed file
     * is caught at verification -> CONFLICT, never VERIFIED. */
    const char *path = ROOT "/save/foreign.txt";
    put_file(path, "before", 6);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("our save");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    file_test_set_step_hook(NULL, foreign_after_rename, (void *)path);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_CONFLICT);
    CHECK(out.pub == FILE_PUB_CONFLICT);
    file_test_set_step_hook(NULL, NULL, NULL);
    CHECK(file_eq(path, "foreign!", 8)); /* foreign file left alone */
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_temp_custody_discard(void)
{
    /* GAP 2 cleanup paths: discard after prepare removes the temp and
     * leaves the destination; custody survives a struct-copy move. */
    const char *path = ROOT "/save/custody.txt";
    put_file(path, "keepme", 6);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("discarded");
    piece_snapshot *s = snap_of(t);
    file_temp tmp, moved;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    moved = tmp;
    file_temp_forget(&tmp);
    /* The temp name exists beside the destination while in custody. */
    char tmp_path[4096];
    strcpy(tmp_path, ROOT "/save/");
    strcat(tmp_path, moved.tmp);
    CHECK(exists(tmp_path));
    file_temp_discard(&moved);
    CHECK(moved.state == FILE_TEMP_DONE);
    CHECK(!exists(tmp_path));
    CHECK(file_eq(path, "keepme", 6));
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_metadata_xattr(void)
{
    const char *path = ROOT "/save/meta.txt";
    put_file(path, "with attrs", 10);
    if (setxattr(path, "user.sublimite_test", "v1", 2, 0) != 0) return; /* fs without user xattrs */
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("new body");
    piece_snapshot *s = snap_of(t);
    file_save_outcome out;
    CHECK(file_save_write(path, s, NULL, &exp, 0, NULL, &out) == FILE_OK);
    char val[8] = {0};
    CHECK(getxattr(path, "user.sublimite_test", val, sizeof val) == 2);
    CHECK(memcmp(val, "v1", 2) == 0);
    piece_snapshot_release(s);
    piece_close(t);
}

static void t_save_symlink(void)
{
    /* Open through a symlink: the canonical target is saved and the
     * link itself survives. */
    const char *real = ROOT "/save/real.txt";
    const char *link = ROOT "/save/link.txt";
    put_file(real, "real one", 8);
    (void)unlink(link);
    CHECK(symlink("real.txt", link) == 0);
    open_file(&f1, link, 0);
    char canon[4096];
    CHECK(realpath(real, canon) != NULL && strcmp(file_path(&f1), canon) == 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 8, 0, (const uint8_t *)"!", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    PUMP_UNTIL(&pool, prepared_or_done, &f1);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
    CHECK(file_save_witness(&f1) != NULL && file_save_witness(&f1)->size == 9);
    CHECK(file_save_publish(&f1) == FILE_OK);
    PUMP_UNTIL(&pool, not_busy, &f1);
    CHECK(last_msg.kind == FILE_MSG_SAVE_DONE && last_msg.status == FILE_OK);
    CHECK(file_eq(real, "real one!", 9));
    struct stat st;
    CHECK(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
    CHECK(!file_changed(&f1));
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* ---- save: async pipeline, blocking, cancel, close ---------------------- */
static int gate_r = -1, gate_w = -1;
static void block_mid_write(void *ctx, int step)
{
    (void)ctx;
    if (step == FILE_STEP_MID_WRITE && gate_r >= 0) {
        char b;
        ssize_t n = read(gate_r, &b, 1);
        (void)n;
    }
}

static void t_save_async_cancel_blocked(void)
{
    /* Fault-spec integration shape: block a running job, cancel it,
     * release it; the snapshot's backing survives to physical
     * acknowledgement and the destination is untouched. */
    const char *path = ROOT "/save/cancel.txt";
    static uint8_t big[300000];
    memset(big, 'q', sizeof big);
    put_file(path, "small", 5);
    open_file(&f1, path, 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 5, big, sizeof big) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    int fds[2];
    CHECK(pipe(fds) == 0);
    gate_r = fds[0];
    gate_w = fds[1];
    file_test_set_step_hook(&f1, block_mid_write, NULL);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    /* Let the worker reach the gate, then cancel and release it. */
    os_sleep_ns(UINT64_C(50000000));
    file_save_abandon(&f1);
    CHECK(write(gate_w, "x", 1) == 1);
    PUMP_UNTIL(&pool, not_busy, &f1);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_IDLE);
    CHECK(file_eq(path, "small", 5));
    /* The snapshot still reads exactly; close retires cleanly. */
    CHECK(piece_length(piece_snapshot_view(s)) == sizeof big);
    piece_snapshot_release(s);
    piece_close(t);
    file_test_set_step_hook(&f1, NULL, NULL);
    close(gate_r);
    close(gate_w);
    gate_r = gate_w = -1;
    close_file(&f1);
}

static void t_close_while_saving(void)
{
    /* Two-phase close never spins: begin returns at once while a save
     * job is blocked; ready only after the job physically finishes. */
    const char *path = ROOT "/save/close.txt";
    static uint8_t big[200000];
    memset(big, 'w', sizeof big);
    put_file(path, "tiny", 4);
    open_file(&f1, path, 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 4, big, sizeof big) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    int fds[2];
    CHECK(pipe(fds) == 0);
    gate_r = fds[0];
    gate_w = fds[1];
    file_test_set_step_hook(&f1, block_mid_write, NULL);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    os_sleep_ns(UINT64_C(50000000));
    uint64_t before = os_now_ns();
    file_close_begin(&f1);
    CHECK(os_now_ns() - before < UINT64_C(1000000000)); /* (E) generous: begin must not wait */
    bool blocked_ready = file_close_ready(&f1);
    CHECK(write(gate_w, "x", 1) == 1);
    uint64_t deadline = os_now_ns() + UINT64_C(10000000000);
    while (!file_close_ready(&f1)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    (void)blocked_ready; /* may already be true if the job raced ahead */
    file_close_finish(&f1);
    CHECK(file_eq(path, "tiny", 4));
    piece_snapshot_release(s);
    piece_close(t);
    close(gate_r);
    close(gate_w);
    gate_r = gate_w = -1;
}

static void t_source_lease(void)
{
    /* The source lease survives file close and tree close; the final
     * release retires the registration exactly once (poll -> NOENT),
     * and the release path performs no unmap before that point: bytes
     * stay readable through the lease's snapshot. */
    const char *path = ROOT "/open/lease.txt";
    put_file(path, "leased bytes!", 13);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    piece_snapshot *s = snap_of(t);
    os_fault_id id = piece_view_backing(piece_tree_view(t))->fault;
    file_source *src;
    CHECK(file_source_acquire(&f1, &src) == FILE_OK);
    CHECK(file_source_stat(src).size == 13);
    os_err oe;
    CHECK(file_source_validate(src, &oe) == FILE_OK);
    piece_close(t);
    close_file(&f1);
    uint64_t epoch = 3;
    CHECK(os_fault_poll(id, &epoch) == OS_OK && epoch == 0); /* still live */
    view_eq(piece_snapshot_view(s), "leased bytes!");
    piece_snapshot_release(s);
    CHECK(os_fault_poll(id, &epoch) == OS_OK); /* lease still holds it */
    file_source_release(src);
    CHECK(os_fault_poll(id, &epoch) == OS_E_NOENT); /* retired once */
}

/* ---- round 2: one test per review finding (wave1-L2-1-opus.md) -------- */
static int count_fds(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    CHECK(d != NULL);
    while (readdir(d) != NULL) n++;
    CHECK(closedir(d) == 0);
    return n;
}
static int count_temps(const char *dir)
{
    int n = 0;
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strstr(e->d_name, ".tmp") != NULL) n++;
    CHECK(closedir(d) == 0);
    return n;
}
static void unlink_temps(const char *dir) /* fixture cleanup: our own temps only */
{
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    struct dirent *e;
    char p[4096];
    while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, ".tmp") == NULL) continue;
        CHECK(snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p);
        CHECK(unlink(p) == 0);
    }
    CHECK(closedir(d) == 0);
}
static _Atomic int hook_entered;
static int hook_target;
static void block_at_step(void *ctx, int step)
{
    (void)ctx;
    if (step == hook_target && gate_r >= 0) {
        atomic_store(&hook_entered, 1);
        char b;
        ssize_t n = read(gate_r, &b, 1);
        (void)n;
    }
}
static void open_gate(void)
{
    int fds[2];
    CHECK(pipe(fds) == 0);
    gate_r = fds[0];
    gate_w = fds[1];
    atomic_store(&hook_entered, 0);
}
static void close_gate(void)
{
    close(gate_r);
    close(gate_w);
    gate_r = gate_w = -1;
}
static void wait_entered(void)
{
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (!atomic_load(&hook_entered)) {
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
}
static bool open_failed(void *ud) { (void)ud; return last_msg.kind == FILE_MSG_OPEN_FAILED; }
static bool prefix_seen(void *ud) { return file_prefix_ready((file *)ud) || last_msg.kind == FILE_MSG_OPEN_FAILED; }
static bool save_done(void *ud) { return !file_save_busy((file *)ud) && last_msg.kind == FILE_MSG_SAVE_DONE; }
static void save_prepare(file *f, piece_snapshot *s, const char *dest,
                         const file_expectation *exp, unsigned flags)
{
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_begin(f, s, dest, exp, flags, 5) == FILE_OK);
    PUMP_UNTIL(&pool, prepared_or_done, f);
}
static void save_async(file *f, piece_snapshot *s, const char *dest,
                       const file_expectation *exp, unsigned flags)
{
    save_prepare(f, s, dest, exp, flags);
    CHECK(file_save_phase_of(f) == FILE_SAVE_PREPARED);
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_publish(f) == FILE_OK);
    PUMP_UNTIL(&pool, save_done, f);
}

/* MF1: the prefix the UI borrowed at PREFIX_READY dies with a failed
 * open: no dangling pointer, no race with the worker's cleanup. */
static void t_r2_prefix_after_failed_open(void)
{
    const char *path = ROOT "/r2/uaf.txt";
    put_file(path, "prefix bytes that will dangle", 29);
    int g[2];
    CHECK(pipe(g) == 0);
    register_gate_r = g[0];
    fail_register = 1;
    memset(&f1, 0, sizeof f1);
    memset(&last_msg, 0, sizeof last_msg);
    file_open_opts opts = { .copy_threshold = 1, .generation = 4 };
    CHECK(file_open_begin(&f1, &pool, path, &opts) == FILE_OK);
    PUMP_UNTIL(&pool, prefix_seen, &f1);
    CHECK(file_prefix_ready(&f1));
    size_t n = 0;
    const uint8_t *p = file_prefix(&f1, &n);
    CHECK(n == 29 && memcmp(p, "prefix bytes that will dangle", 29) == 0);
    CHECK(write(g[1], "x", 1) == 1); /* registration now refused: NOSPC */
    PUMP_UNTIL(&pool, open_failed, &f1);
    CHECK(last_msg.status == FILE_ERR_NOSPC);
    FIRED();
    p = file_prefix(&f1, &n);
    CHECK(!file_prefix_ready(&f1));
    CHECK(p == NULL && n == 0);
    register_gate_r = -1;
    close(g[0]);
    close(g[1]);
    close_file(&f1);
}

/* MF2: an uncertain rename keeps custody reachable and its evidence. The
 * temp moved to UNCERTAIN_AWAY and EIO came back: neither the temp name
 * nor the destination proves anything (round 3: a rename observed not to
 * have happened is PRE_RENAME instead, t_r3_rename_not_performed). */
static void t_r2_uncertain_rename(void)
{
    const char *dir = ROOT "/r2/unc";
    const char *path = ROOT "/r2/unc/u.txt";
    const char *away = ROOT "/r2/unc/" UNCERTAIN_AWAY;
    put_file(path, "old", 3);
    unlink_temps(dir);
    (void)unlink(away);
    int fds0 = count_fds();
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("uncertain");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    atomic_store(&rename_inject, RENAME_AWAY);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_IO);
    CHECK(out.pub == FILE_PUB_RENAME_UNCERTAIN);
    CHECK(tmp.state == FILE_TEMP_HELD);
    CHECK(file_eq(path, "old", 3));
    CHECK(file_eq(away, "uncertain", 9));
    file_temp_discard(&tmp);
    CHECK(tmp.state == FILE_TEMP_DONE);
    CHECK(count_fds() == fds0);
    CHECK(file_eq(away, "uncertain", 9)); /* no authority to unlink after it */
    CHECK(unlink(away) == 0);
    /* The asynchronous object: custody stays reachable for discard. */
    open_file(&f1, path, 0);
    fds0 = count_fds();
    atomic_store(&rename_inject, RENAME_AWAY);
    save_async(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(last_msg.status == FILE_ERR_IO && last_msg.mode == FILE_PUB_RENAME_UNCERTAIN);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_HELD); /* custody reachable */
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_ERR_BUSY);
    file_save_abandon(&f1);
    CHECK(!file_save_busy(&f1) && file_save_phase_of(&f1) == FILE_SAVE_IDLE);
    CHECK(count_fds() == fds0);
    CHECK(file_eq(away, "uncertain", 9));
    CHECK(unlink(away) == 0);
    file_expectation now = *file_baseline(&f1);
    save_async(&f1, s, file_path(&f1), &now, 0);
    CHECK(last_msg.status == FILE_OK && file_eq(path, "uncertain", 9));
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
    CHECK(count_temps(dir) == 0);
}

/* MF3: close completes even when the job's final message was dropped. */
static void fill_mailbox(work_ctx *ctx)
{
    work_msg m;
    memset(&m, 0, sizeof m);
    m.kind = 0x12345678u;
    while (work_publish(ctx, &m)) { }
}
static void wait_dropped(uint64_t before)
{
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (atomic_load(&pool.dropped_full) == before) {
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
}
static bool close_ready_cond(void *ud) { return file_close_ready((file *)ud); }
static void t_r2_close_after_dropped_result(void)
{
    const char *path = ROOT "/r2/hang.txt";
    put_file(path, "hang", 4);
    work_job j = { .fn = fill_mailbox, .arg = NULL, .generation = 0, .cls = WORK_BULK };
    uint64_t before = atomic_load(&pool.dropped_full);
    CHECK(work_submit(&pool, j).epoch != 0);
    memset(&f1, 0, sizeof f1);
    file_open_opts opts = { .copy_threshold = 0, .generation = 1 };
    CHECK(file_open_begin(&f1, &pool, path, &opts) == FILE_OK);
    wait_dropped(before); /* the open's final publish met a full mailbox */
    file_close_begin(&f1);
    PUMP_UNTIL(&pool, close_ready_cond, &f1);
    file_close_finish(&f1);
    /* The same for a reload result. */
    open_file(&f1, path, 0);
    before = atomic_load(&pool.dropped_full);
    CHECK(work_submit(&pool, j).epoch != 0);
    CHECK(file_reload_begin(&f1, 3) == FILE_OK);
    wait_dropped(before);
    file_close_begin(&f1);
    PUMP_UNTIL(&pool, close_ready_cond, &f1);
    file_close_finish(&f1);
    while (work_mailbox_pending(&pool)) work_mailbox_drain(&pool, on_msg, NULL);
}

/* MF4: abandon during publish settles the classified outcome. */
static void t_r2_abandon_while_publishing(void)
{
    const char *dir = ROOT "/r2/abpub";
    const char *path = ROOT "/r2/abpub/a.txt";
    put_file(path, "before", 6);
    unlink_temps(dir);
    open_file(&f1, path, 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"new ", 4) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
    open_gate();
    hook_target = FILE_STEP_DIR_SYNCED;
    file_test_set_step_hook(&f1, block_at_step, NULL);
    CHECK(file_save_publish(&f1) == FILE_OK);
    wait_entered();
    file_save_abandon(&f1);
    CHECK(write(gate_w, "x", 1) == 1);
    PUMP_UNTIL(&pool, not_busy, &f1);
    CHECK(file_save_outcome_of(&f1)->pub == FILE_PUB_VERIFIED);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_HELD);
    uint32_t reasons = 0;
    CHECK(!file_check(&f1, &reasons)); /* our own save is not foreign */
    CHECK(file_eq(path, "new before", 10));
    file_save_abandon(&f1);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_IDLE);
    file_test_set_step_hook(&f1, NULL, NULL);
    close_gate();
    /* An uncertain rename abandoned in flight keeps its evidence. */
    int g[2];
    CHECK(pipe(g) == 0);
    int fds0 = count_fds();
    save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
    rename_gate_r = g[0];
    unsigned renames = atomic_load(&calls_rename);
    atomic_store(&rename_inject, RENAME_AWAY_BLOCK);
    CHECK(file_save_publish(&f1) == FILE_OK);
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (atomic_load(&calls_rename) == renames) {
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    file_save_abandon(&f1);
    CHECK(write(g[1], "x", 1) == 1);
    PUMP_UNTIL(&pool, not_busy, &f1);
    CHECK(file_save_outcome_of(&f1)->pub == FILE_PUB_RENAME_UNCERTAIN);
    CHECK(exists(ROOT "/r2/abpub/" UNCERTAIN_AWAY)); /* evidence kept */
    file_save_abandon(&f1);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_IDLE);
    CHECK(count_fds() == fds0);
    CHECK(exists(ROOT "/r2/abpub/" UNCERTAIN_AWAY));
    CHECK(unlink(ROOT "/r2/abpub/" UNCERTAIN_AWAY) == 0);
    rename_gate_r = -1;
    close(g[0]);
    close(g[1]);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* MF5: our own verified save leaves the mapped original valid. */
static void t_r2_validate_after_own_save(void)
{
    const char *path = ROOT "/r2/val.txt";
    put_file(path, "mapped original", 15);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    CHECK(file_backing_validate(&f1, b));
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"X", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    save_async(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(file_backing_validate(&f1, b)); /* the old bytes are intact */
    CHECK(!file_check(&f1, NULL));
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* MF6: Keep never adopts foreign bytes in a mapped inode; the fd leg of
 * file_check sees ctime. */
static void restore_mtime(const char *p, const struct stat *st)
{
    struct timespec ts[2] = { st->st_atim, st->st_mtim };
    CHECK(utimensat(AT_FDCWD, p, ts, 0) == 0);
}
static void t_r2_keep_hardlink(void)
{
    const char *a = ROOT "/r2/hl/a.txt", *bpath = ROOT "/r2/hl/b.txt", *c = ROOT "/r2/hl/c.txt";
    put_file(a, "AAAAAAAAAA", 10);
    (void)unlink(bpath);
    CHECK(link(a, bpath) == 0);
    open_file(&f1, a, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *bk = piece_view_backing(piece_tree_view(t));
    put_file(c, "replacement", 11);
    CHECK(rename(c, a) == 0);
    CHECK(file_check(&f1, NULL));
    struct stat st;
    CHECK(stat(bpath, &st) == 0);
    int fd = open(bpath, O_WRONLY);
    CHECK(fd >= 0 && write(fd, "ZZZZZZZZZZ", 10) == 10 && close(fd) == 0);
    restore_mtime(bpath, &st);
    CHECK(file_resolve_keep(&f1) == FILE_ERR_CHANGED);
    CHECK(!file_backing_validate(&f1, bk));
    piece_close(t);
    close_file(&f1);
    /* fd leg: the path is gone (GONE only), our inode's ctime moved. */
    put_file(a, "AAAAAAAAAA", 10);
    (void)unlink(bpath);
    CHECK(link(a, bpath) == 0);
    open_file(&f1, a, 1);
    CHECK(unlink(a) == 0);
    uint32_t reasons = 0;
    CHECK(file_check(&f1, &reasons));
    CHECK((reasons & FILE_CHG_GONE) != 0);
    CHECK((reasons & FILE_CHG_METADATA) != 0);
    close_file(&f1);
}

/* MF7-MF9 run in a fresh process (file_test --child NAME ...): MF7 needs
 * the umask captured at that process's start; MF8/MF9 must not take the
 * suite down if they abort or block. */
static int run_child(const char *name, const char *arg, unsigned mask)
{
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        umask((mode_t)mask);
        char *argv[] = { (char *)"file_test", (char *)"--child", (char *)name,
                         (char *)arg, NULL };
        execv("/proc/self/exe", argv);
        _exit(127);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status)) {
        fprintf(stderr, "file_test.c: child %s died by signal %d\n", name,
                WIFSIGNALED(status) ? WTERMSIG(status) : -1);
        return -1;
    }
    return WEXITSTATUS(status);
}
static void abs_path(const char *rel, char *out, size_t cap)
{
    char cwd[2048];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    CHECK(snprintf(out, cap, "%s/%s", cwd, rel) < (int)cap);
}
static void t_r2_new_mode(void)
{
    char path[4096];
    abs_path(ROOT "/r2/mode/new.txt", path, sizeof path);
    mkpath(path);
    (void)unlink(path);
    struct stat st;
    CHECK(run_child("mode", path, 022) == 0);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 07777) == 0644);
    CHECK(unlink(path) == 0);
    CHECK(run_child("mode", path, 077) == 0);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 07777) == 0600);
    /* 1.28: an existing destination keeps its mode. */
    CHECK(chmod(path, 0640) == 0);
    CHECK(run_child("mode", path, 077) == 0);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 07777) == 0640);
}
static void t_r2_bad_names(void)
{
    char base[4096], p[4096];
    abs_path(ROOT "/r2/names", base, sizeof base);
    put_file(ROOT "/r2/names/x", "x", 1);
    CHECK(snprintf(p, sizeof p, "%s/", base) < (int)sizeof p);
    CHECK(run_child("name", p, 022) == 0);       /* trailing slash */
    size_t n = (size_t)snprintf(p, sizeof p, "%s/", base);
    memset(p + n, 'q', 300);
    p[n + 300] = '\0';
    CHECK(run_child("name", p, 022) == 0);       /* overlong leaf */
    CHECK(run_child("name", "", 022) == 0);       /* empty name */
    CHECK(run_child("open", "/", 022) == 0);     /* open of the root */
}
static void t_r2_fifo_destination(void)
{
    char p[4096];
    abs_path(ROOT "/r2/fifo/f", p, sizeof p);
    mkpath(p);
    CHECK(run_child("fifo", p, 022) == 0);
    CHECK(run_child("fifo_swapped", p, 022) == 0);
}

/* MF10: acceptance lines no round-1 test checked. */
static void t_r2_exist_race(void)
{
    const char *dir = ROOT "/r2/exist";
    const char *path = ROOT "/r2/exist/e.txt";
    mkpath(path);
    (void)unlink(path);
    unlink_temps(dir);
    int fds0 = count_fds();
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK && exp.presence == FILE_ABSENT);
    piece_tree *t = text_tree("ours");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    strcpy(rename_foreign_path, path);
    atomic_store(&rename_inject, RENAME_FOREIGN); /* between recheck and rename */
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_CHANGED);
    CHECK(out.pub == FILE_PUB_PRE_RENAME && out.oserr == OS_E_EXIST);
    CHECK(file_eq(path, "foreign", 7));
    file_temp_discard(&tmp);
    CHECK(count_temps(dir) == 0 && count_fds() == fds0);
    piece_snapshot_release(s);
    piece_close(t);
}
static void t_r2_cleanup_paths(void)
{
    const char *dir = ROOT "/r2/clean";
    const char *path = ROOT "/r2/clean/c.txt";
    put_file(path, "stable", 6);
    unlink_temps(dir);
    int fds0 = count_fds();
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("next text");
    piece_snapshot *s = snap_of(t);
    file_save_outcome out;
    /* temp fsync failure */
    fail_file_sync = 1;
    CHECK(file_save_write(path, s, NULL, &exp, 0, NULL, &out) == FILE_ERR_IO);
    FIRED();
    CHECK(count_temps(dir) == 0 && count_fds() == fds0);
    /* pre-rename CHANGED */
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    put_file(path, "changed", 7);
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_CHANGED && out.pub == FILE_PUB_PRE_RENAME);
    file_temp_discard(&tmp);
    CHECK(count_temps(dir) == 0 && count_fds() == fds0);
    /* async: abandon after the temp is synced, before its message */
    open_file(&f1, path, 0);
    open_gate();
    fds0 = count_fds();
    hook_target = FILE_STEP_FSYNCED;
    file_test_set_step_hook(&f1, block_at_step, NULL);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    wait_entered();
    file_save_abandon(&f1);
    CHECK(write(gate_w, "x", 1) == 1);
    PUMP_UNTIL(&pool, not_busy, &f1);
    file_save_abandon(&f1); /* settles a finished abandon */
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_IDLE);
    CHECK(count_temps(dir) == 0 && count_fds() == fds0);
    close_gate();
    /* async: cancel while writing */
    open_gate();
    fds0 = count_fds();
    hook_target = FILE_STEP_MID_WRITE;
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    wait_entered();
    file_save_abandon(&f1);
    CHECK(write(gate_w, "x", 1) == 1);
    PUMP_UNTIL(&pool, not_busy, &f1);
    file_save_abandon(&f1);
    CHECK(count_temps(dir) == 0 && count_fds() == fds0);
    close_gate();
    fds0 -= 2; /* the gate */
    file_test_set_step_hook(&f1, NULL, NULL);
    /* close while a synced temp is in custody */
    save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
    CHECK(count_temps(dir) == 1);
    close_file(&f1);
    CHECK(count_temps(dir) == 0);
    CHECK(file_eq(path, "changed", 7));
    piece_snapshot_release(s);
    piece_close(t);
}
static void t_r2_barrier_retry_async(void)
{
    const char *path = ROOT "/r2/retry/r.txt";
    put_file(path, "old", 3);
    open_file(&f1, path, 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 3, 0, (const uint8_t *)"+new", 4) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    unsigned renames = atomic_load(&calls_rename);
    fail_dir_sync = 1;
    save_async(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    FIRED();
    CHECK(last_msg.status == FILE_ERR_DIRSYNC && last_msg.mode == FILE_PUB_INSTALLED_UNSYNCED);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_UNSYNCED);
    uint64_t inode = file_save_outcome_of(&f1)->installed.stat.id.index[0];
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_retry_barrier(&f1) == FILE_OK);
    PUMP_UNTIL(&pool, save_done, &f1);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(atomic_load(&calls_rename) == renames + 1); /* never renamed again */
    CHECK(file_save_outcome_of(&f1)->installed.stat.id.index[0] == inode);
    CHECK(file_eq(path, "old+new", 7));
    /* VERIFIED adopted the installed baseline. */
    CHECK(file_baseline(&f1)->stat.id.index[0] == inode);
    CHECK(!file_check(&f1, NULL));
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}
static void t_r2_check_fault_poll(void)
{
    const char *path = ROOT "/r2/poll.txt";
    put_file(path, "pristine?", 9);
    open_file(&f1, path, 1);
    CHECK(!file_check(&f1, NULL));
    atomic_store(&poll_taint, 1); /* the fd stat stays identical */
    uint32_t reasons = 0;
    CHECK(file_check(&f1, &reasons));
    atomic_store(&poll_taint, 0);
    CHECK(reasons == FILE_CHG_FAULTED);
    close_file(&f1);
}
static void t_r2_source_validation(void)
{
    /* A same-size in-place overwrite of the mapped source (mtime
     * restored, so only ctime moves) refuses the prepare itself. */
    const char *path = ROOT "/r2/src.txt";
    put_file(path, "SOURCE-BYTES", 12);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)">", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    struct stat st;
    CHECK(stat(path, &st) == 0);
    int fd = open(path, O_WRONLY);
    CHECK(fd >= 0 && write(fd, "FOREIGN-BYTE", 12) == 12 && close(fd) == 0);
    restore_mtime(path, &st);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    save_prepare(&f1, s, file_path(&f1), &exp, FILE_SAVE_FORCE);
    CHECK(last_msg.kind == FILE_MSG_SAVE_DONE && last_msg.status == FILE_ERR_CHANGED);
    CHECK(file_eq(path, "FOREIGN-BYTE", 12));
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}
static void t_r2_mapped_cancel_lease(void)
{
    /* Fault-spec integration row on a MAPPED open: block the running
     * save, drop every other owner, cancel, release; the backing
     * survives to physical acknowledgement, then retires exactly once. */
    const char *path = ROOT "/r2/lease.txt";
    static uint8_t body[65536];
    memset(body, 'm', sizeof body);
    put_file(path, body, sizeof body);
    calls_register = 0;
    open_file(&f1, path, 1);
    CHECK(calls_register == 1 && file_open_mode(&f1) == FILE_MODE_MMAP);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    os_fault_id fid = b->fault;
    atomic_store(&watch_gen, fid.gen);
    atomic_store(&watch_addr, (uintptr_t)b->bytes);
    atomic_store(&watched_retires, 0);
    atomic_store(&watched_unmaps, 0);
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"!", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    open_gate();
    hook_target = FILE_STEP_MID_WRITE;
    file_test_set_step_hook(&f1, block_at_step, NULL);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    wait_entered();
    piece_snapshot_release(s);
    piece_close(t);
    file_close_begin(&f1); /* cancels the blocked job */
    CHECK(!file_close_ready(&f1));
    uint64_t epoch = 9;
    CHECK(os_fault_poll(fid, &epoch) == OS_OK && epoch == 0);
    CHECK(atomic_load(&watched_retires) == 0 && atomic_load(&watched_unmaps) == 0);
    CHECK(write(gate_w, "x", 1) == 1);
    PUMP_UNTIL(&pool, close_ready_cond, &f1);
    CHECK(atomic_load(&watched_retires) == 0 && atomic_load(&watched_unmaps) == 0);
    file_close_finish(&f1);
    CHECK(atomic_load(&watched_retires) == 1 && atomic_load(&watched_unmaps) == 1);
    CHECK(os_fault_poll(fid, &epoch) == OS_E_NOENT);
    CHECK(file_eq(path, body, sizeof body));
    atomic_store(&watch_gen, 0);
    atomic_store(&watch_addr, 0);
    close_gate();
}

/* ---- round 3 (re-check docs/reviews/wave1-L2-1-opus-recheck.md) -------- */

/* MF-B: an existing destination the owner cannot read keeps its observed
 * mode; the new-file default is only for a name with no destination. */
static void t_r3_unreadable_dest(void)
{
    const char *path = ROOT "/r3/noread.txt";
    piece_tree *t = text_tree("replacement");
    piece_snapshot *s = snap_of(t);
    for (unsigned flags = 0; flags <= FILE_SAVE_FORCE; flags += FILE_SAVE_FORCE) {
        put_file(path, "secret", 6);
        CHECK(chmod(path, 0060) == 0);
        file_expectation exp;
        os_err oe;
        CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
        file_save_outcome out;
        CHECK(file_save_write(path, s, NULL, &exp, flags, NULL, &out) == FILE_OK);
        struct stat st;
        CHECK(stat(path, &st) == 0 && (st.st_mode & 07777) == 0060);
        CHECK(chmod(path, 0600) == 0);
        CHECK(file_eq(path, "replacement", 11));
    }
    piece_snapshot_release(s);
    piece_close(t);
}

/* MF-D: file_temp_verify and file_temp_recover, each refusal class. */
static uint8_t r3_body[10000];
static void r3_install(const char *path, file_temp *tmp, piece_tree **t, piece_snapshot **s)
{
    for (size_t i = 0; i < sizeof r3_body; i++) r3_body[i] = (uint8_t)('a' + i % 26);
    (void)unlink(path);
    mkpath(path);
    CHECK(piece_open_copy(reserve, r3_body, sizeof r3_body, t) == PIECE_OK);
    *s = snap_of(*t);
    file_expectation absent;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &absent, &oe) == FILE_OK);
    memset(tmp, 0, sizeof *tmp);
    CHECK(file_temp_prepare(tmp, path, *s, NULL, &absent, 0, &oe) == FILE_OK);
    file_save_outcome out;
    CHECK(file_temp_publish(tmp, &out) == FILE_OK && tmp->state == FILE_TEMP_HELD);
}
static void t_r3_verify(void)
{
    const char *path = ROOT "/r3/verify.txt";
    file_temp tmp;
    piece_tree *t;
    piece_snapshot *s;
    r3_install(path, &tmp, &t, &s);
    file_expectation inst;
    uint32_t pcrc = 0;
    os_err oe = OS_OK;
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_OK);
    CHECK(pcrc == file_crc32c(0, r3_body, FILE_WIRE_PREFIX_BYTES));
    CHECK(inst.presence == FILE_PRESENT && inst.stat.size == sizeof r3_body);
    /* The name lies on its second look: not a stable observation (X13). */
    atomic_store(&stat_at_lie_on, 2);
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_ERR_CHANGED);
    CHECK(atomic_load(&stat_at_lie_on) == 0);
    /* The fd's times move during the read-back, bytes unchanged (X7). */
    strcpy(pread_touch_path, path);
    atomic_store(&pread_touch, 1);
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_ERR_CHANGED);
    CHECK(atomic_load(&pread_touch) == 0);
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_OK); /* stable again */
    /* Same inode and size, other bytes: only the content CRC sees it (X6). */
    int fd = open(path, O_WRONLY);
    CHECK(fd >= 0 && pwrite(fd, "Z", 1, 5000) == 1 && close(fd) == 0);
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_ERR_CHANGED);
    /* A symlink at the destination is a mismatch, not an I/O error. */
    CHECK(unlink(path) == 0 && symlink("elsewhere", path) == 0);
    CHECK(file_temp_verify(&tmp, &inst, &pcrc, &oe) == FILE_ERR_CHANGED);
    CHECK(tmp.state == FILE_TEMP_HELD); /* failure preserves custody */
    file_temp_discard(&tmp);
    CHECK(unlink(path) == 0);
    piece_snapshot_release(s);
    piece_close(t);
}
static void t_r3_recover(void)
{
    const char *path = ROOT "/r3/recover.txt";
    file_temp tmp;
    piece_tree *t;
    piece_snapshot *s;
    r3_install(path, &tmp, &t, &s);
    file_witness w = tmp.witness;
    file_temp_discard(&tmp);
    int fds0 = count_fds();
    file_temp rec;
    os_err oe = OS_OK;
    /* content mismatch */
    file_witness bad = w;
    bad.content_crc32c ^= 1u;
    memset(&rec, 0, sizeof rec);
    CHECK(file_temp_recover(&rec, path, &bad, &oe) == FILE_ERR_CHANGED);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    /* re-stat mismatch (X7 through recover) */
    strcpy(pread_touch_path, path);
    atomic_store(&pread_touch, 1);
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_ERR_CHANGED);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    /* the name moves between its two looks (X13 through recover) */
    atomic_store(&stat_at_lie_on, 2);
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_ERR_CHANGED);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    /* the real installed file: INSTALLED_UNSYNCED, then barrier-only retry */
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_OK);
    CHECK(rec.state == FILE_TEMP_INSTALLED_UNSYNCED);
    unsigned renames = atomic_load(&calls_rename);
    file_save_outcome out;
    CHECK(file_temp_retry_barrier(&rec, &out) == FILE_OK && out.pub == FILE_PUB_VERIFIED);
    CHECK(atomic_load(&calls_rename) == renames);
    file_temp_discard(&rec);
    CHECK(count_fds() == fds0 && file_eq(path, r3_body, sizeof r3_body));
    /* a symlink is what it is: not a regular file, not an I/O error */
    CHECK(unlink(path) == 0 && symlink("recover.target", path) == 0);
    put_file(ROOT "/r3/recover.target", r3_body, sizeof r3_body);
    oe = OS_OK;
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_ERR_NOTREG);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    CHECK(unlink(path) == 0 && unlink(ROOT "/r3/recover.target") == 0);
    piece_snapshot_release(s);
    piece_close(t);
}

/* MF-D / X9: from HELD, the retry syncs nothing until the destination is
 * proven to be the held fd; once it is, the barrier settles VERIFIED. */
static void t_r3_held_retry_proof(void)
{
    const char *path = ROOT "/r3/proof/p.txt";
    const char *away = ROOT "/r3/proof/" UNCERTAIN_AWAY;
    put_file(path, "old", 3);
    (void)unlink(away);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("proven later");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    atomic_store(&rename_inject, RENAME_AWAY);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_IO && tmp.state == FILE_TEMP_HELD);
    unsigned syncs = calls_dir_sync;
    CHECK(file_temp_retry_barrier(&tmp, &out) != FILE_OK);
    CHECK(out.pub == FILE_PUB_CONFLICT && calls_dir_sync == syncs); /* no proof, no sync */
    CHECK(tmp.state == FILE_TEMP_HELD && file_eq(path, "old", 3));
    CHECK(rename(away, path) == 0); /* the installation becomes visible */
    CHECK(file_temp_retry_barrier(&tmp, &out) == FILE_OK && out.pub == FILE_PUB_VERIFIED);
    CHECK(calls_dir_sync == syncs + 1);
    file_temp_discard(&tmp);
    piece_snapshot_release(s);
    piece_close(t);
}

/* Item 4a: a rename that failed without renaming is a BEFORE-rename
 * result, and its temp is removed (it is still ours and unpublished). */
static void t_r3_rename_not_performed(void)
{
    const char *dir = ROOT "/r3/unr";
    const char *path = ROOT "/r3/unr/u.txt";
    int injects[] = { RENAME_EIO, RENAME_EISDIR };
    os_err errs[] = { OS_E_IO, OS_E_ISDIR };
    piece_tree *t = text_tree("never installed");
    piece_snapshot *s = snap_of(t);
    for (size_t i = 0; i < 2; i++) {
        put_file(path, "old", 3);
        unlink_temps(dir);
        int fds0 = count_fds();
        file_expectation exp;
        os_err oe;
        CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
        file_temp tmp;
        memset(&tmp, 0, sizeof tmp);
        CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
        atomic_store(&rename_inject, injects[i]);
        file_save_outcome out;
        CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_IO);
        CHECK(out.pub == FILE_PUB_PRE_RENAME && out.oserr == errs[i]);
        CHECK(tmp.state == FILE_TEMP_DONE);
        CHECK(count_temps(dir) == 0 && count_fds() == fds0 && file_eq(path, "old", 3));
        /* asynchronous: SAVE_DONE leaves IDLE and no temp */
        open_file(&f1, path, 0);
        atomic_store(&rename_inject, injects[i]);
        save_async(&f1, s, file_path(&f1), file_baseline(&f1), 0);
        CHECK(last_msg.status == FILE_ERR_IO && last_msg.mode == FILE_PUB_PRE_RENAME);
        CHECK(file_save_phase_of(&f1) == FILE_SAVE_IDLE && count_temps(dir) == 0);
        CHECK(!file_check(&f1, NULL));
        close_file(&f1);
        CHECK(count_fds() == fds0);
    }
    piece_snapshot_release(s);
    piece_close(t);
}

/* Item 4b and X1: a displaced source that moved beyond our own rename is
 * CONFLICT; the barrier still runs, and no retry turns it into VERIFIED. */
static const char *r3_hook_link;
static int r3_hook_op;
enum { R3_GROW = 1, R3_RELINK, R3_TOUCH_DEST };
static void r3_renamed_hook(void *ctx, int step)
{
    (void)ctx;
    if (step != FILE_STEP_RENAMED || r3_hook_link == NULL) return;
    switch (r3_hook_op) {
    case R3_GROW: { /* the displaced inode grows through its other name */
        int fd = open(r3_hook_link, O_WRONLY | O_APPEND);
        if (fd < 0 || write(fd, "XX", 2) != 2 || close(fd) != 0) _exit(91);
        break;
    }
    case R3_RELINK: { /* a link comes back: ctime moves, the count does not fall */
        char c[4096];
        strcpy(c, r3_hook_link);
        strcat(c, ".relink");
        (void)unlink(c);
        if (link(r3_hook_link, c) != 0) _exit(92);
        break;
    }
    case R3_TOUCH_DEST: { /* our installed inode's mtime moves (X16) */
        struct timespec ts[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
        if (utimensat(AT_FDCWD, r3_hook_link, ts, 0) != 0) _exit(93);
        break;
    }
    }
    r3_hook_link = NULL;
}
static void r3_linked_pair(const char *a, const char *b)
{
    put_file(a, "AAAAAAAAAA", 10);
    (void)unlink(b);
    CHECK(link(a, b) == 0);
}
static void t_r3_source_conflict(void)
{
    const char *a = ROOT "/r3/sc/a.txt", *b = ROOT "/r3/sc/b.txt";
    for (int op = R3_GROW; op <= R3_RELINK; op++) {
        r3_linked_pair(a, b);
        open_file(&f1, a, 1);
        piece_tree *t;
        CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
        const piece_backing *bk = piece_view_backing(piece_tree_view(t));
        CHECK(piece_replace(t, 10, 0, (const uint8_t *)"+", 1) == PIECE_OK);
        piece_snapshot *s = snap_of(t);
        save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), 0);
        CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
        r3_hook_link = b;
        r3_hook_op = op;
        file_test_set_step_hook(&f1, r3_renamed_hook, NULL);
        unsigned syncs = calls_dir_sync;
        memset(&last_msg, 0, sizeof last_msg);
        CHECK(file_save_publish(&f1) == FILE_OK);
        PUMP_UNTIL(&pool, save_done, &f1);
        file_test_set_step_hook(&f1, NULL, NULL);
        CHECK(last_msg.status == FILE_ERR_CONFLICT && last_msg.mode == FILE_PUB_CONFLICT);
        CHECK(calls_dir_sync == syncs + 1); /* the barrier was not skipped */
        CHECK(file_save_phase_of(&f1) == FILE_SAVE_HELD);
        CHECK(!file_backing_validate(&f1, bk));
        memset(&last_msg, 0, sizeof last_msg);
        CHECK(file_save_retry_barrier(&f1) == FILE_OK);
        PUMP_UNTIL(&pool, save_done, &f1);
        CHECK(last_msg.mode == FILE_PUB_CONFLICT); /* never VERIFIED */
        CHECK(!file_backing_validate(&f1, bk));
        file_save_abandon(&f1);
        piece_snapshot_release(s);
        piece_close(t);
        close_file(&f1);
    }
}

/* X3: a source that changed BEFORE our rename (FORCE skips the recheck)
 * is never re-baselined by the rename pair. */
static void t_r3_rebaseline_needs_baseline(void)
{
    const char *path = ROOT "/r3/rb/a.txt";
    put_file(path, "BBBBBBBBBB", 10);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *bk = piece_view_backing(piece_tree_view(t));
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)">", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), FILE_SAVE_FORCE);
    CHECK(file_save_phase_of(&f1) == FILE_SAVE_PREPARED);
    struct stat st;
    CHECK(stat(path, &st) == 0);
    int fd = open(path, O_WRONLY);
    CHECK(fd >= 0 && write(fd, "CCCCCCCCCC", 10) == 10 && close(fd) == 0);
    restore_mtime(path, &st);
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_publish(&f1) == FILE_OK);
    PUMP_UNTIL(&pool, save_done, &f1);
    CHECK(last_msg.mode == FILE_PUB_VERIFIED); /* the destination is ours */
    CHECK(!file_backing_validate(&f1, bk));    /* the source is not */
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* Item 4c: a relative destination naming the file's own path adopts the
 * installed baseline like the canonical one. */
static void t_r3_relative_own_path(void)
{
    const char *rel = ROOT "/r3/rel.txt";
    put_file(rel, "rel", 3);
    open_file(&f1, rel, 0);
    CHECK(strcmp(file_path(&f1), rel) != 0); /* canonical differs from rel */
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 3, 0, (const uint8_t *)"+", 1) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    save_async(&f1, s, rel, file_baseline(&f1), 0);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(file_baseline(&f1)->stat.id.index[0] == file_save_outcome_of(&f1)->installed.stat.id.index[0]);
    CHECK(!file_check(&f1, NULL));
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* X14: a verified save to another path leaves this file's baseline. */
static void t_r3_saveas_keeps_baseline(void)
{
    const char *path = ROOT "/r3/mine.txt", *other = ROOT "/r3/other.txt";
    put_file(path, "mine", 4);
    (void)unlink(other);
    open_file(&f1, path, 0);
    file_expectation before = *file_baseline(&f1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    piece_snapshot *s = snap_of(t);
    file_expectation absent;
    os_err oe;
    CHECK(file_expectation_observe_path(other, &absent, &oe) == FILE_OK);
    save_async(&f1, s, other, &absent, 0);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(memcmp(file_baseline(&f1), &before, sizeof before) == 0);
    CHECK(!file_check(&f1, NULL));
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* X4, X5: decode accepts only the job the slot names: epoch and
 * generation both count (N8). */
static int r3_tamper_seen;
static void r3_tamper_cb(const work_msg *m, void *ud)
{
    (void)ud;
    if (m->kind == FILE_MSG_SAVE_PREPARED && !r3_tamper_seen) {
        file_msg out;
        work_msg x = *m;
        x.epoch_++;
        CHECK(file_msg_decode(&x, &out) == 1);
        x = *m;
        x.generation++;
        CHECK(file_msg_decode(&x, &out) == 1);
        r3_tamper_seen = 1;
    }
    on_msg(m, ud);
}
static void t_r3_decode_identity(void)
{
    const char *path = ROOT "/r3/decode.txt";
    put_file(path, "decode", 6);
    open_file(&f1, path, 0);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    piece_snapshot *s = snap_of(t);
    r3_tamper_seen = 0;
    memset(&last_msg, 0, sizeof last_msg);
    CHECK(file_save_begin(&f1, s, file_path(&f1), file_baseline(&f1), 0, 5) == FILE_OK);
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (file_save_phase_of(&f1) != FILE_SAVE_PREPARED) {
        work_mailbox_drain(&pool, r3_tamper_cb, NULL);
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    CHECK(r3_tamper_seen);
    file_save_abandon(&f1);
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* X8: discard unlinks the temp name only while it still names our fd. */
static void t_r3_unlink_identity(void)
{
    const char *dir = ROOT "/r3/own";
    const char *path = ROOT "/r3/own/o.txt";
    put_file(path, "dest", 4);
    unlink_temps(dir);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("ours");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    char tp[4096], fp[4096];
    CHECK(snprintf(tp, sizeof tp, "%s/%s", dir, tmp.tmp) < (int)sizeof tp);
    CHECK(snprintf(fp, sizeof fp, "%s/foreign.x", dir) < (int)sizeof fp);
    put_file(fp, "foreign", 7);
    CHECK(rename(fp, tp) == 0); /* someone reuses the temp name */
    file_temp_discard(&tmp);
    CHECK(file_eq(tp, "foreign", 7)); /* not ours: never unlinked */
    CHECK(unlink(tp) == 0);
    piece_snapshot_release(s);
    piece_close(t);
}

/* X15: close readiness waits for a reload job too. */
static void t_r3_close_waits_reload(void)
{
    const char *path = ROOT "/r3/reload.txt";
    put_file(path, "reloadable", 10);
    open_file(&f1, path, 1);
    int g[2];
    CHECK(pipe(g) == 0);
    atomic_store(&register_blocked, 0);
    register_gate_r = g[0];
    CHECK(file_reload_begin(&f1, 9) == FILE_OK);
    uint64_t deadline = os_now_ns() + UINT64_C(20000000000);
    while (!atomic_load(&register_blocked)) {
        CHECK(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    file_close_begin(&f1);
    CHECK(!file_close_ready(&f1)); /* the reload job still runs */
    CHECK(write(g[1], "x", 1) == 1);
    PUMP_UNTIL(&pool, close_ready_cond, &f1);
    file_close_finish(&f1);
    register_gate_r = -1;
    close(g[0]);
    close(g[1]);
}

/* X16: a foreign touch of our installed inode between rename and
 * verification is CONFLICT (N2: fd_stat's mtime is compared). */
static void t_r3_installed_touched(void)
{
    const char *path = ROOT "/r3/touch.txt";
    put_file(path, "before", 6);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("after!");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    r3_hook_link = path;
    r3_hook_op = R3_TOUCH_DEST;
    file_test_set_step_hook(NULL, r3_renamed_hook, NULL);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_CONFLICT && out.pub == FILE_PUB_CONFLICT);
    file_test_set_step_hook(NULL, NULL, NULL);
    file_temp_discard(&tmp);
    piece_snapshot_release(s);
    piece_close(t);
}

/* X18: the null-ctx validator refuses a backing file did not create. */
static void fake_acquire(void *ctx) { (void)ctx; }
static void fake_release(void *ctx) { (void)ctx; }
static void t_r3_null_ctx_validator(void)
{
    const char *path = ROOT "/r3/nullctx.txt";
    put_file(path, "null ctx", 8);
    open_file(&f1, path, 1);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    const piece_backing *b = piece_view_backing(piece_tree_view(t));
    CHECK(file_backing_validate(NULL, b));
    piece_backing forged = *b;
    forged.acquire = fake_acquire;
    forged.release = fake_release;
    CHECK(!file_backing_validate(NULL, &forged));
    piece_close(t);
    close_file(&f1);
}

/* Item 5: the creation mask comes from /proc/self/status; unreadable, it
 * is 077 (the private side). Fresh processes: the value is read once. */
static void t_r3_creation_mask(void)
{
    CHECK(run_child("mask_proc", "027", 027) == 0);
    CHECK(run_child("mask_fallback", "077", 022) == 0);
}

/* ---- round 4 (re-check docs/reviews/wave1-L2-1-opus-recheck2.md) ------ */

/* MF-E: a copy buffer's bytes are private, so a foreign change to its
 * original inode never refuses a save; Keep and the destination
 * expectation still decide what may be overwritten. */
static void r4_foreign_atomic_save(const char *path, const char *text)
{
    char tmp[4096];
    CHECK(snprintf(tmp, sizeof tmp, "%s.foreign", path) < (int)sizeof tmp);
    put_file(tmp, text, strlen(text));
    CHECK(rename(tmp, path) == 0);
}
static void t_r4_copy_after_foreign_save(void)
{
    const char *path = ROOT "/r4/copy.txt", *as = ROOT "/r4/copy-as.txt";
    put_file(path, "original", 8);
    (void)unlink(as);
    open_file(&f1, path, 0);
    CHECK(file_open_mode(&f1) == FILE_MODE_COPY);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"edited ", 7) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    /* Save, after a foreign atomic save and Keep. */
    r4_foreign_atomic_save(path, "foreign one");
    CHECK(file_check(&f1, NULL));
    CHECK(file_resolve_keep(&f1) == FILE_OK);
    save_async(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(file_eq(path, "edited original", 15));
    file_save_abandon(&f1);
    /* Save with FORCE, over a second foreign save nobody kept. */
    r4_foreign_atomic_save(path, "foreign two");
    save_async(&f1, s, file_path(&f1), file_baseline(&f1), FILE_SAVE_FORCE);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(file_eq(path, "edited original", 15));
    file_save_abandon(&f1);
    /* Save As to a fresh name. */
    r4_foreign_atomic_save(path, "foreign three");
    file_expectation absent;
    os_err oe;
    CHECK(file_expectation_observe_path(as, &absent, &oe) == FILE_OK && absent.presence == FILE_ABSENT);
    save_async(&f1, s, as, &absent, 0);
    CHECK(last_msg.status == FILE_OK && last_msg.mode == FILE_PUB_VERIFIED);
    CHECK(file_eq(as, "edited original", 15));
    file_save_abandon(&f1);
    /* The destination expectation still protects what is overwritten. */
    r4_foreign_atomic_save(path, "foreign four");
    save_prepare(&f1, s, file_path(&f1), file_baseline(&f1), 0);
    CHECK(last_msg.kind == FILE_MSG_SAVE_DONE && last_msg.status == FILE_ERR_CHANGED);
    CHECK(file_eq(path, "foreign four", 12));
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}
/* The control: a mapped buffer reads its bytes from the source, so after
 * the same foreign atomic save it still refuses (Keep, then Save As). */
static void t_r4_mapped_still_refuses(void)
{
    const char *path = ROOT "/r4/mapped.txt", *as = ROOT "/r4/mapped-as.txt";
    put_file(path, "mapped original", 15);
    (void)unlink(as);
    open_file(&f1, path, 1);
    CHECK(file_open_mode(&f1) == FILE_MODE_MMAP);
    piece_tree *t;
    CHECK(file_open_tree(&f1, reserve, &t) == FILE_OK);
    CHECK(piece_replace(t, 0, 0, (const uint8_t *)"edited ", 7) == PIECE_OK);
    piece_snapshot *s = snap_of(t);
    r4_foreign_atomic_save(path, "foreign");
    CHECK(file_resolve_keep(&f1) == FILE_ERR_CHANGED);
    file_expectation absent;
    os_err oe;
    CHECK(file_expectation_observe_path(as, &absent, &oe) == FILE_OK);
    save_prepare(&f1, s, as, &absent, 0);
    CHECK(last_msg.kind == FILE_MSG_SAVE_DONE && last_msg.status == FILE_ERR_CHANGED);
    CHECK(!exists(as));
    piece_snapshot_release(s);
    piece_close(t);
    close_file(&f1);
}

/* R3-A: "not renamed" needs the destination to still match its
 * expectation; a destination moved back onto our temp name by a foreign
 * rename is unknown, and nothing is unlinked. */
static void t_r4_rename_back(void)
{
    const char *dir = ROOT "/r4/back";
    const char *path = ROOT "/r4/back/b.txt";
    put_file(path, "old!", 4);
    unlink_temps(dir);
    file_expectation exp;
    os_err oe;
    CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
    piece_tree *t = text_tree("ours");
    piece_snapshot *s = snap_of(t);
    file_temp tmp;
    memset(&tmp, 0, sizeof tmp);
    CHECK(file_temp_prepare(&tmp, path, s, NULL, &exp, 0, &oe) == FILE_OK);
    char tp[4096];
    CHECK(snprintf(tp, sizeof tp, "%s/%s", dir, tmp.tmp) < (int)sizeof tp);
    atomic_store(&rename_inject, RENAME_BACK);
    file_save_outcome out;
    CHECK(file_temp_publish(&tmp, &out) == FILE_ERR_IO);
    CHECK(out.pub == FILE_PUB_RENAME_UNCERTAIN && tmp.state == FILE_TEMP_HELD);
    CHECK(file_eq(tp, "ours", 4)); /* nothing unlinked */
    CHECK(!exists(path));
    file_temp_discard(&tmp);
    CHECK(file_eq(tp, "ours", 4));
    CHECK(unlink(tp) == 0);
    piece_snapshot_release(s);
    piece_close(t);
}

/* R3-C: a restart that meets an unreadable or absent installed name is a
 * mismatch, not an I/O error repeated on every start. */
static void t_r4_recover_unreadable_absent(void)
{
    const char *path = ROOT "/r4/recover.txt";
    file_temp tmp;
    piece_tree *t;
    piece_snapshot *s;
    r3_install(path, &tmp, &t, &s);
    file_witness w = tmp.witness;
    file_temp_discard(&tmp);
    int fds0 = count_fds();
    file_temp rec;
    memset(&rec, 0, sizeof rec);
    os_err oe = OS_OK;
    CHECK(chmod(path, 0060) == 0);
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_ERR_CHANGED && oe == OS_E_ACCESS);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    CHECK(chmod(path, 0600) == 0 && unlink(path) == 0);
    oe = OS_OK;
    CHECK(file_temp_recover(&rec, path, &w, &oe) == FILE_ERR_CHANGED && oe == OS_E_NOENT);
    CHECK(rec.state == FILE_TEMP_EMPTY && count_fds() == fds0);
    piece_snapshot_release(s);
    piece_close(t);
}

/* ---- children (file_test --child NAME ARG) ------------------------------- */
static uint32_t child_result_perm;
static const char *child_dir;
static int child_wider;
static void child_temp_mode(void *ctx, int step)
{
    (void)ctx;
    if (step > FILE_STEP_FSYNCED) return;
    DIR *d = opendir(child_dir);
    if (d == NULL) { child_wider = 1; return; }
    struct dirent *e;
    char p[4096];
    while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, ".tmp") == NULL) continue;
        struct stat st;
        if (snprintf(p, sizeof p, "%s/%s", child_dir, e->d_name) >= (int)sizeof p ||
            stat(p, &st) != 0 || ((uint32_t)st.st_mode & 07777u & ~child_result_perm) != 0)
            child_wider = 1;
    }
    closedir(d);
}
static int child_main(const char *name, const char *arg)
{
    if (strcmp(name, "mask_proc") == 0 || strcmp(name, "mask_fallback") == 0) {
        /* Before anything in os can read the mask. */
        openat_refuse_status = strcmp(name, "mask_fallback") == 0;
        uint32_t want = (uint32_t)strtoul(arg, NULL, 8);
        return os_creation_mask() == want ? 0 : 10;
    }
    CHECK(piece_reserve_create(&reserve) == PIECE_OK);
    piece_tree *t = text_tree("child text");
    piece_snapshot *s = snap_of(t);
    file_expectation exp;
    os_err oe = OS_OK;
    file_save_outcome out;
    if (strcmp(name, "mode") == 0) {
        CHECK(file_expectation_observe_path(arg, &exp, &oe) == FILE_OK);
        mode_t m = umask(0); /* the test child's own probe of its mask */
        umask(m);
        struct stat st;
        child_result_perm = exp.presence == FILE_PRESENT
                                ? exp.stat.perm
                                : (uint32_t)(0666u & ~(uint32_t)m);
        (void)st;
        static char dirbuf[4096];
        strcpy(dirbuf, arg);
        *strrchr(dirbuf, '/') = '\0';
        child_dir = dirbuf;
        file_test_set_step_hook(NULL, child_temp_mode, NULL);
        CHECK(file_save_write(arg, s, NULL, &exp, 0, NULL, &out) == FILE_OK);
        return child_wider ? 3 : 0;
    }
    if (strcmp(name, "name") == 0) {
        memset(&exp, 0, sizeof exp);
        exp.presence = FILE_ABSENT;
        int rc = file_save_write(arg, s, NULL, &exp, 0, NULL, &out);
        if (rc == FILE_OK) return 4;
        if (out.pub != FILE_PUB_PRE_RENAME) return 5;
        return 0;
    }
    if (strcmp(name, "open") == 0) {
        CHECK(work_pool_init(&pool, 1, 0) == 0);
        memset(&f1, 0, sizeof f1);
        memset(&last_msg, 0, sizeof last_msg);
        CHECK(file_open_begin(&f1, &pool, arg, NULL) == FILE_OK);
        PUMP_UNTIL(&pool, open_settled, &f1);
        int ok = last_msg.kind == FILE_MSG_OPEN_FAILED && last_msg.status == FILE_ERR_NOTREG;
        close_file(&f1);
        work_pool_shutdown(&pool);
        return ok ? 0 : 6;
    }
    if (strcmp(name, "fifo") == 0 || strcmp(name, "fifo_swapped") == 0) {
        (void)unlink(arg);
        static char fdir[4096];
        strcpy(fdir, arg);
        *strrchr(fdir, '/') = '\0';
        unlink_temps(fdir); /* a killed earlier run may have left one */
        if (strcmp(name, "fifo_swapped") == 0) {
            put_file(arg, "regular", 7);
            CHECK(file_expectation_observe_path(arg, &exp, &oe) == FILE_OK);
            CHECK(unlink(arg) == 0);
            CHECK(mkfifo(arg, 0600) == 0);
        } else {
            CHECK(mkfifo(arg, 0600) == 0);
            CHECK(file_expectation_observe_path(arg, &exp, &oe) == FILE_OK);
        }
        alarm(10); /* (E) far above one stat and one open: a block dies here */
        int rc = file_save_write(arg, s, NULL, &exp, 0, NULL, &out);
        alarm(0);
        char dirbuf[4096];
        strcpy(dirbuf, arg);
        *strrchr(dirbuf, '/') = '\0';
        int temps = count_temps(dirbuf);
        CHECK(unlink(arg) == 0);
        if (rc == FILE_OK || temps != 0) return 7;
        return 0;
    }
    return 8;
}

/* ---- durability: SIGKILL at every save step ----------------------------- */
static int kill_pipe_w = -1, kill_pipe_r = -1;
static void kill_step(void *ctx, int step)
{
    (void)ctx;
    char b = (char)step;
    if (write(kill_pipe_w, &b, 1) != 1) _exit(3);
    char ack;
    if (read(kill_pipe_r, &ack, 1) != 1) _exit(4); /* parent may kill us here */
}

static void t_durability(void)
{
    for (int target = FILE_STEP_TEMP_CREATED; target <= FILE_STEP_DIR_SYNCED; target++) {
        char dir[4096], path[4096];
        int dn = snprintf(dir, sizeof dir, ROOT "/kill%d", target);
        CHECK(dn > 0 && (size_t)dn < sizeof dir);
        strcpy(path, dir);
        strcat(path, "/f.txt");
        put_file(path, "OLD-CONTENT", 11);
        int to_parent[2], to_child[2];
        CHECK(pipe(to_parent) == 0 && pipe(to_child) == 0);
        piece_tree *t = text_tree("NEW-CONTENT!");
        piece_snapshot *s = snap_of(t);
        file_expectation exp;
        os_err oe;
        CHECK(file_expectation_observe_path(path, &exp, &oe) == FILE_OK);
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            close(to_parent[0]);
            close(to_child[1]);
            kill_pipe_w = to_parent[1];
            kill_pipe_r = to_child[0];
            file_test_set_step_hook(NULL, kill_step, NULL);
            file_save_outcome out;
            int rc = file_save_write(path, s, NULL, &exp, 0, NULL, &out);
            _exit(rc == FILE_OK ? 0 : 2);
        }
        close(to_parent[1]);
        close(to_child[0]);
        bool killed = false;
        for (;;) {
            char b;
            ssize_t n = read(to_parent[0], &b, 1);
            if (n <= 0) break;
            if ((int)b == target) {
                CHECK(kill(pid, SIGKILL) == 0);
                killed = true;
                break;
            }
            CHECK(write(to_child[1], "a", 1) == 1);
        }
        int status = 0;
        CHECK(waitpid(pid, &status, 0) == pid);
        close(to_parent[0]);
        close(to_child[1]);
        CHECK(killed && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
        /* After a kill at any step the target holds complete old text
         * (before rename) or complete new text (at/after it), never a
         * torn or partial file. */
        if (target < FILE_STEP_RENAMED) CHECK(file_eq(path, "OLD-CONTENT", 11));
        else CHECK(file_eq(path, "NEW-CONTENT!", 12));
        piece_snapshot_release(s);
        piece_close(t);
    }
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--child") == 0) {
        cur_test = argv[2];
        return child_main(argv[2], argv[3]);
    }
    if (argc == 2) only_test = argv[1];
    CHECK(mkdir("build", 0700) == 0 || errno == EEXIST);
    CHECK(mkdir(ROOT, 0700) == 0 || errno == EEXIST);
    CHECK(work_pool_init(&pool, 1, 0) == 0);
    CHECK(piece_reserve_create(&reserve) == PIECE_OK);
    RUN(t_prefix_scan);
    RUN(t_crc);
    RUN(t_expectation);
    RUN(t_open_copy);
    RUN(t_open_mapped);
    RUN(t_open_missing_and_dir);
    RUN(t_open_nospc);
    RUN(t_change_in_place);
    RUN(t_change_replace_keep);
    RUN(t_sigbus);
    RUN(t_reload);
    RUN(t_reload_nospc);
    RUN(t_watch);
    RUN(t_save_sync);
    RUN(t_save_absent_race);
    RUN(t_save_changed_refused);
    RUN(t_save_fsync_fails);
    RUN(t_save_dirsync_retry);
    RUN(t_save_foreign_after_rename);
    RUN(t_temp_custody_discard);
    RUN(t_save_metadata_xattr);
    RUN(t_save_symlink);
    RUN(t_save_async_cancel_blocked);
    RUN(t_close_while_saving);
    RUN(t_source_lease);
    RUN(t_r2_prefix_after_failed_open);
    RUN(t_r2_uncertain_rename);
    RUN(t_r2_close_after_dropped_result);
    RUN(t_r2_abandon_while_publishing);
    RUN(t_r2_validate_after_own_save);
    RUN(t_r2_keep_hardlink);
    RUN(t_r2_new_mode);
    RUN(t_r2_bad_names);
    RUN(t_r2_fifo_destination);
    RUN(t_r2_exist_race);
    RUN(t_r2_cleanup_paths);
    RUN(t_r2_barrier_retry_async);
    RUN(t_r2_check_fault_poll);
    RUN(t_r2_source_validation);
    RUN(t_r2_mapped_cancel_lease);
    RUN(t_r3_unreadable_dest);
    RUN(t_r3_verify);
    RUN(t_r3_recover);
    RUN(t_r3_held_retry_proof);
    RUN(t_r3_rename_not_performed);
    RUN(t_r3_source_conflict);
    RUN(t_r3_rebaseline_needs_baseline);
    RUN(t_r3_relative_own_path);
    RUN(t_r3_saveas_keeps_baseline);
    RUN(t_r3_decode_identity);
    RUN(t_r3_unlink_identity);
    RUN(t_r3_close_waits_reload);
    RUN(t_r3_installed_touched);
    RUN(t_r3_null_ctx_validator);
    RUN(t_r3_creation_mask);
    RUN(t_r4_copy_after_foreign_save);
    RUN(t_r4_mapped_still_refuses);
    RUN(t_r4_rename_back);
    RUN(t_r4_recover_unreadable_absent);
    RUN(t_durability);
    FIRED();
    piece_reserve_destroy(reserve);
    work_pool_shutdown(&pool);
    CHECK(tests_run > 0); /* a misspelt selector runs nothing: fail closed */
    printf("file_test: ok (%d tests)\n", tests_run);
    return 0;
}


