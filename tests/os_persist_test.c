/* os seams for persistence (L2-0a, docs/decisions/L2-0a.md): the
 * directory-change watch and the fd-to-fd metadata copy. Real kernel events
 * and real user.* xattrs on a directory under build/; every failure path is
 * injected beneath os through -Wl,--wrap (Makefile OS_PERSIST_TEST_WRAPS),
 * and every injection is asserted to have fired. Law 2 for these calls is
 * tests/law2_os_persist_test.c (the guard is live only in release builds). */
#include "assert_death.h"
#include "os/os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static void check(bool ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "os_persist_test.c:%d: FAIL %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check((x), #x, __LINE__)
static void expect(os_err got, os_err want, const char *expression, int line)
{
    if (got != want) {
        fprintf(stderr, "os_persist_test.c:%d: FAIL %s -> %s (native %d), expected %s\n", line,
                expression, os_err_name(got), os_native_error(), os_err_name(want));
        exit(EXIT_FAILURE);
    }
}
#define EXPECT(call, error) expect((call), (error), #call, __LINE__)
#define OK(call) EXPECT(call, OS_OK)

/* (P) SI conversion. */
#define MS UINT64_C(1000000)

/* ---- injection ---------------------------------------------------------- */
enum { W_INIT, W_ADD, W_RM, W_READ, W_FCHOWN, W_FSTAT, W_LIST, W_GET, W_SET, W_REMOVE, W_COUNT };
static int inject_left[W_COUNT], inject_errno[W_COUNT], inject_fd[W_COUNT];
static unsigned calls[W_COUNT];
/* Fail the next `times` calls of w (on descriptor fd, or any when fd < 0). */
static void inject(int w, int e, int times, int fd)
{
    inject_left[w] = times;
    inject_errno[w] = e;
    inject_fd[w] = fd;
}
static bool take(int w, int fd)
{
    calls[w]++;
    if (inject_left[w] <= 0 || (inject_fd[w] >= 0 && fd != inject_fd[w])) return false;
    inject_left[w]--;
    errno = inject_errno[w];
    return true;
}
static void no_injection_left(int line)
{
    for (int w = 0; w < W_COUNT; w++)
        if (inject_left[w] != 0) {
            fprintf(stderr, "os_persist_test.c:%d: FAIL injection %d never fired\n", line, w);
            exit(EXIT_FAILURE);
        }
}
#define FIRED() no_injection_left(__LINE__)
static void reset_calls(void) { memset(calls, 0, sizeof calls); }

/* Special read modes: a crafted record instead of the kernel's. */
static int read_forge; /* 1: a header that claims more name bytes than were read. */
/* fstat of this descriptor reports uid + 1, once (forces the owner copy). */
static int fstat_bump_fd = -1;

int __real_inotify_init1(int);
int __real_inotify_add_watch(int, const char *, uint32_t);
int __real_inotify_rm_watch(int, int);
ssize_t __real_read(int, void *, size_t);
int __real_fchown(int, uid_t, gid_t);
int __real_fstat(int, struct stat *);
ssize_t __real_flistxattr(int, char *, size_t);
ssize_t __real_fgetxattr(int, const char *, void *, size_t);
int __real_fsetxattr(int, const char *, const void *, size_t, int);
int __real_fremovexattr(int, const char *);
int __wrap_inotify_init1(int);
int __wrap_inotify_add_watch(int, const char *, uint32_t);
int __wrap_inotify_rm_watch(int, int);
ssize_t __wrap_read(int, void *, size_t);
int __wrap_fchown(int, uid_t, gid_t);
int __wrap_fstat(int, struct stat *);
ssize_t __wrap_flistxattr(int, char *, size_t);
ssize_t __wrap_fgetxattr(int, const char *, void *, size_t);
int __wrap_fsetxattr(int, const char *, const void *, size_t, int);
int __wrap_fremovexattr(int, const char *);

int __wrap_inotify_init1(int f) { return take(W_INIT, -1) ? -1 : __real_inotify_init1(f); }
int __wrap_inotify_add_watch(int fd, const char *p, uint32_t m)
{
    return take(W_ADD, fd) ? -1 : __real_inotify_add_watch(fd, p, m);
}
int __wrap_inotify_rm_watch(int fd, int wd) { return take(W_RM, fd) ? -1 : __real_inotify_rm_watch(fd, wd); }
ssize_t __wrap_read(int fd, void *b, size_t n)
{
    if (take(W_READ, fd)) return -1;
    if (read_forge && inject_fd[W_READ] == fd) {
        read_forge = 0;
        struct inotify_event h;
        memset(&h, 0, sizeof h);
        h.wd = 1;
        h.mask = IN_CREATE;
        h.len = 64; /* but no name bytes follow */
        CHECK(n >= sizeof h);
        memcpy(b, &h, sizeof h);
        return (ssize_t)sizeof h;
    }
    return __real_read(fd, b, n);
}
int __wrap_fchown(int fd, uid_t u, gid_t g) { return take(W_FCHOWN, fd) ? -1 : __real_fchown(fd, u, g); }
int __wrap_fstat(int fd, struct stat *st)
{
    if (take(W_FSTAT, fd)) return -1;
    int rc = __real_fstat(fd, st);
    if (rc == 0 && fd == fstat_bump_fd) {
        fstat_bump_fd = -1;
        st->st_uid += 1;
    }
    return rc;
}
/* flistxattr mode: a list whose last name lacks its NUL. */
static int list_forge_fd = -1;
ssize_t __wrap_flistxattr(int fd, char *b, size_t n)
{
    if (take(W_LIST, fd)) return -1;
    if (fd == list_forge_fd) {
        list_forge_fd = -1;
        CHECK(n >= 6);
        memcpy(b, "user.x", 6);
        return 6;
    }
    return __real_flistxattr(fd, b, n);
}
ssize_t __wrap_fgetxattr(int fd, const char *k, void *b, size_t n)
{
    return take(W_GET, fd) ? -1 : __real_fgetxattr(fd, k, b, n);
}
int __wrap_fsetxattr(int fd, const char *k, const void *v, size_t n, int f)
{
    return take(W_SET, fd) ? -1 : __real_fsetxattr(fd, k, v, n, f);
}
int __wrap_fremovexattr(int fd, const char *k) { return take(W_REMOVE, fd) ? -1 : __real_fremovexattr(fd, k); }

/* ---- fixture ------------------------------------------------------------ */
static char root[OS_PATH_MAX];
static void path_of(char *out, const char *leaf)
{
    int n = snprintf(out, OS_PATH_MAX, "%s/%s", root, leaf);
    CHECK(n > 0 && n < (int)OS_PATH_MAX);
}
static void make_dir(const char *leaf)
{
    char p[OS_PATH_MAX];
    path_of(p, leaf);
    OK(os_mkdir(p));
}
static void make_file(const char *leaf, const char *bytes)
{
    char p[OS_PATH_MAX];
    path_of(p, leaf);
    int fd = open(p, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(fd >= 0);
    size_t n = strlen(bytes);
    if (n) CHECK(write(fd, bytes, n) == (ssize_t)n);
    CHECK(close(fd) == 0);
}
static void move(const char *from, const char *to)
{
    char a[OS_PATH_MAX], b[OS_PATH_MAX];
    path_of(a, from);
    path_of(b, to);
    OK(os_rename(a, b, OS_RENAME_REPLACE));
}
static void remove_leaf(const char *leaf)
{
    char p[OS_PATH_MAX];
    path_of(p, leaf);
    OK(os_unlink(p));
}

/* ---- the watch: drain and compare per id --------------------------------- */
typedef struct seen {
    os_watch_id id;
    os_watch_what what;
    char name[OS_NAME_MAX];
} seen;
#define SEEN_CAP 64
static seen got[SEEN_CAP];
static size_t got_n;

/* Reads until OS_E_AGAIN (events are queued by the syscall that caused
 * them, so they are all present when this runs). */
static void drain(os_watch *w)
{
    got_n = 0;
    for (;;) {
        os_watch_event ev[8];
        size_t n = 99;
        os_err e = os_watch_read(w, ev, 8, &n);
        if (e == OS_E_AGAIN) {
            CHECK(n == 0);
            return;
        }
        EXPECT(e, OS_OK);
        CHECK(n <= 8);
        for (size_t i = 0; i < n; i++) {
            CHECK(got_n < SEEN_CAP);
            got[got_n].id = ev[i].id;
            got[got_n].what = ev[i].what;
            CHECK(ev[i].name && strlen(ev[i].name) < OS_NAME_MAX);
            memcpy(got[got_n].name, ev[i].name, strlen(ev[i].name) + 1);
            got_n++;
        }
    }
}
/* The events drained for id, in order, must be exactly the list
 * {what, name}... terminated by 0. */
static const char *what_name(os_watch_what w)
{
    switch (w) {
    case OS_WATCH_CREATE: return "CREATE";
    case OS_WATCH_DELETE: return "DELETE";
    case OS_WATCH_MOVED_OUT: return "MOVED_OUT";
    case OS_WATCH_MOVED_IN: return "MOVED_IN";
    case OS_WATCH_MODIFY: return "MODIFY";
    case OS_WATCH_ATTRIB: return "ATTRIB";
    case OS_WATCH_ENDED: return "ENDED";
    }
    return "?";
}
static void dump(void)
{
    for (size_t i = 0; i < got_n; i++)
        fprintf(stderr, "  id %u %s \"%s\"\n", got[i].id, what_name(got[i].what), got[i].name);
}
static void expect_for(os_watch_id id, int line, ...)
{
    va_list ap;
    va_start(ap, line);
    size_t i = 0;
    for (;;) {
        int what = va_arg(ap, int);
        if (what == 0) break;
        const char *name = va_arg(ap, const char *);
        while (i < got_n && got[i].id != id) i++;
        if (i == got_n || (int)got[i].what != what || strcmp(got[i].name, name) != 0) {
            fprintf(stderr, "os_persist_test.c:%d: FAIL id %u expected %s \"%s\"; drained:\n", line,
                    id, what_name((os_watch_what)what), name);
            dump();
            exit(EXIT_FAILURE);
        }
        i++;
    }
    va_end(ap);
    for (; i < got_n; i++)
        if (got[i].id == id) {
            fprintf(stderr, "os_persist_test.c:%d: FAIL id %u has extra events; drained:\n", line, id);
            dump();
            exit(EXIT_FAILURE);
        }
}
#define EXPECT_FOR(id, ...) expect_for((id), __LINE__, __VA_ARGS__, 0)
#define NOTHING_FOR(id) expect_for((id), __LINE__, 0)

static bool fd_flag(int fd, int get, int bit)
{
    int f = fcntl(fd, get);
    return f >= 0 && (f & bit);
}

/* Create, modify, attribute change, rename both ways and over, delete, for
 * a watched file and its watched parent, plus the parent's own changes. */
static void case_watch_events(void)
{
    os_watch_slot slots[8];
    os_watch w = OS_WATCH_INIT;
    OK(os_watch_init(&w, slots, 8));
    CHECK(os_watch_fd(&w) >= 0);
    CHECK(fd_flag(os_watch_fd(&w), F_GETFD, FD_CLOEXEC));
    CHECK(fd_flag(os_watch_fd(&w), F_GETFL, O_NONBLOCK));
    size_t n = 1;
    os_watch_event ev[4];
    EXPECT(os_watch_read(&w, ev, 4, &n), OS_E_AGAIN);
    CHECK(n == 0);

    make_dir("d");
    char dpath[OS_PATH_MAX];
    path_of(dpath, "d");
    os_watch_id d = 99, top = 99;
    OK(os_watch_add(&w, dpath, &d));
    OK(os_watch_add(&w, root, &top));
    CHECK(d != top && d < 8 && top < 8);

    /* The descriptor joins the one wait set. */
    os_waitset ws = OS_WAITSET_INIT;
    OK(os_waitset_init(&ws));
    OK(os_waitset_add(&ws, os_watch_fd(&w), OS_WAIT_IN, 5));
    os_wait_event wev[2];
    OK(os_waitset_wait(&ws, 0, wev, 2, &n));
    CHECK(n == 0);

    make_file("d/f", "x");
    OK(os_waitset_wait(&ws, os_now_ns() + 1000 * MS, wev, 2, &n));
    CHECK(n == 1 && wev[0].tag == 5 && (wev[0].events & OS_WAIT_IN));
    drain(&w);
    /* write(2), then the writer's close. */
    EXPECT_FOR(d, OS_WATCH_CREATE, "f", OS_WATCH_MODIFY, "f", OS_WATCH_MODIFY, "f");
    NOTHING_FOR(top);
    OK(os_waitset_wait(&ws, 0, wev, 2, &n));
    CHECK(n == 0);

    char fpath[OS_PATH_MAX];
    path_of(fpath, "d/f");
    CHECK(chmod(fpath, 0640) == 0);
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_ATTRIB, "f");
    NOTHING_FOR(top);

    move("d/f", "d/g");
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_MOVED_OUT, "f", OS_WATCH_MOVED_IN, "g");

    move("d/g", "h"); /* out of the watched parent */
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_MOVED_OUT, "g");
    EXPECT_FOR(top, OS_WATCH_MOVED_IN, "h");
    move("h", "d/f"); /* and back in */
    drain(&w);
    EXPECT_FOR(top, OS_WATCH_MOVED_OUT, "h");
    EXPECT_FOR(d, OS_WATCH_MOVED_IN, "f");

    make_file("d/t", ""); /* the save shape: a temp renamed over the file */
    move("d/t", "d/f");
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_CREATE, "t", OS_WATCH_MODIFY, "t", OS_WATCH_MOVED_OUT, "t",
               OS_WATCH_MOVED_IN, "f");

    remove_leaf("d/f");
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_DELETE, "f");
    NOTHING_FOR(top);

    /* The parent itself: its attributes, then a rename of it. */
    CHECK(chmod(dpath, 0750) == 0);
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_ATTRIB, "");
    EXPECT_FOR(top, OS_WATCH_ATTRIB, "d");
    move("d", "d2");
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_MOVED_OUT, "");
    EXPECT_FOR(top, OS_WATCH_MOVED_OUT, "d", OS_WATCH_MOVED_IN, "d2");

    /* Holds: the same inode by another path is the same id; the kernel watch
     * survives the first removal and ends at the last. */
    char d2path[OS_PATH_MAX];
    path_of(d2path, "d2");
    os_watch_id again = 99;
    OK(os_watch_add(&w, d2path, &again));
    CHECK(again == d);
    reset_calls();
    os_watch_remove(&w, d);
    CHECK(calls[W_RM] == 0);
    make_file("d2/x", "");
    drain(&w);
    EXPECT_FOR(d, OS_WATCH_CREATE, "x", OS_WATCH_MODIFY, "x");
    os_watch_remove(&w, d);
    CHECK(calls[W_RM] == 1);
    make_file("d2/y", "");
    drain(&w); /* The kernel's end-of-watch record for d is discarded too. */
    CHECK(got_n == 0);

    /* The parent removed: its last events, then the watch ends. The holder
     * still removes its hold, which then makes no kernel call. */
    os_watch_id d2 = 99;
    OK(os_watch_add(&w, d2path, &d2));
    remove_leaf("d2/x");
    remove_leaf("d2/y");
    CHECK(rmdir(d2path) == 0);
    drain(&w);
    EXPECT_FOR(d2, OS_WATCH_DELETE, "x", OS_WATCH_DELETE, "y", OS_WATCH_DELETE, "",
               OS_WATCH_ENDED, "");
    EXPECT_FOR(top, OS_WATCH_DELETE, "d2");
    reset_calls();
    os_watch_remove(&w, d2);
    CHECK(calls[W_RM] == 0);

    /* Not a directory, and no such path. */
    make_file("plain", "");
    char ppath[OS_PATH_MAX], missing[OS_PATH_MAX];
    path_of(ppath, "plain");
    path_of(missing, "missing");
    os_watch_id bad = 99;
    EXPECT(os_watch_add(&w, ppath, &bad), OS_E_NOTDIR);
    EXPECT(os_watch_add(&w, missing, &bad), OS_E_NOENT);
    os_watch_remove(&w, top);
    os_waitset_remove(&ws, os_watch_fd(&w));
    os_waitset_close(&ws);
    os_watch_close(&w);
    CHECK(os_watch_fd(&w) == -1);
    os_watch_close(&w); /* idempotent */
    remove_leaf("plain");
}

/* Slot exhaustion withdraws the new kernel watch; a full caller array is
 * continued from the object without another read. */
static void case_watch_capacity(void)
{
    os_watch_slot slots[1];
    os_watch w = OS_WATCH_INIT;
    OK(os_watch_init(&w, slots, 1));
    make_dir("a");
    make_dir("b");
    char a[OS_PATH_MAX], b[OS_PATH_MAX];
    path_of(a, "a");
    path_of(b, "b");
    os_watch_id ia = 99, ia2 = 99, ib = 99;
    OK(os_watch_add(&w, a, &ia));
    OK(os_watch_add(&w, a, &ia2));
    CHECK(ia == 0 && ia2 == 0);
    reset_calls();
    EXPECT(os_watch_add(&w, b, &ib), OS_E_NOSPC);
    CHECK(calls[W_ADD] == 1 && calls[W_RM] == 1);
    make_file("b/x", "");
    drain(&w);
    CHECK(got_n == 0);

    make_file("a/1", "");
    make_file("a/2", "");
    reset_calls();
    os_watch_event ev[1];
    size_t n = 0;
    for (int i = 0; i < 4; i++) {
        OK(os_watch_read(&w, ev, 1, &n));
        CHECK(n == 1 && ev[0].id == ia);
    }
    CHECK(calls[W_READ] == 1); /* four records, one read(2) */
    EXPECT(os_watch_read(&w, ev, 1, &n), OS_E_AGAIN);
    CHECK(n == 0 && calls[W_READ] == 2);

    os_watch_remove(&w, ia);
    os_watch_remove(&w, ia);
    /* The slot is free again. */
    OK(os_watch_add(&w, b, &ib));
    CHECK(ib == 0);
    os_watch_remove(&w, ib);
    os_watch_close(&w);
    remove_leaf("a/1");
    remove_leaf("a/2");
    remove_leaf("b/x");
    OK(os_rmdir(a));
    OK(os_rmdir(b));
}

/* A real queue overflow: one more record than the kernel queues
 * (max_queued_events, read from /proc: M on this box, not assumed). */
static void case_watch_overflow(void)
{
    FILE *f = fopen("/proc/sys/fs/inotify/max_queued_events", "r");
    CHECK(f != NULL);
    unsigned long max = 0;
    CHECK(fscanf(f, "%lu", &max) == 1 && max > 0);
    fclose(f);
    printf("os_persist_test: max_queued_events %lu (M)\n", max);

    os_watch_slot slots[2];
    os_watch w = OS_WATCH_INIT;
    OK(os_watch_init(&w, slots, 2));
    make_dir("o");
    char o[OS_PATH_MAX], sub[OS_PATH_MAX];
    path_of(o, "o");
    path_of(sub, "o/n");
    os_watch_id io = 99;
    OK(os_watch_add(&w, o, &io));
    /* (P) Each mkdir/rmdir pair queues two records that differ from their
     * neighbours, so the kernel merges none (fs/notify/inotify:
     * inotify_merge compares only with the last queued record). */
    for (unsigned long i = 0; i < max / 2 + 1; i++) {
        CHECK(mkdir(sub, 0700) == 0);
        CHECK(rmdir(sub) == 0);
    }
    unsigned long before = 0, overflows = 0;
    for (;;) {
        os_watch_event ev[64];
        size_t n = 0;
        os_err e = os_watch_read(&w, ev, 64, &n);
        if (e == OS_E_AGAIN) break;
        CHECK(e == OS_OK || e == OS_E_OVERFLOW);
        for (size_t i = 0; i < n; i++) CHECK(ev[i].id == io && strcmp(ev[i].name, "n") == 0);
        if (!overflows) before += n;
        if (e == OS_E_OVERFLOW) overflows++;
    }
    printf("os_persist_test: %lu records, then the overflow\n", before);
    /* (P) fsnotify_insert_event queues the overflow record once the queue
     * holds max_events records. */
    CHECK(overflows == 1 && before == max);
    /* The watch still works. */
    CHECK(mkdir(sub, 0700) == 0);
    drain(&w);
    EXPECT_FOR(io, OS_WATCH_CREATE, "n");
    CHECK(rmdir(sub) == 0);
    os_watch_remove(&w, io);
    os_watch_close(&w);
    OK(os_rmdir(o));
}

static void case_watch_faults(void)
{
    os_watch_slot slots[2];
    os_watch w = OS_WATCH_INIT;
    inject(W_INIT, EMFILE, 1, -1);
    EXPECT(os_watch_init(&w, slots, 2), OS_E_IO);
    CHECK(os_native_error() == EMFILE && os_watch_fd(&w) == -1);
    FIRED();
    OK(os_watch_init(&w, slots, 2));
    int fd = os_watch_fd(&w);

    os_watch_id id = 99;
    inject(W_ADD, ENOSPC, 1, fd); /* the per-user watch limit */
    EXPECT(os_watch_add(&w, root, &id), OS_E_NOSPC);
    FIRED();
    OK(os_watch_add(&w, root, &id));

    os_watch_event ev[4];
    size_t n = 0;
    make_file("e", "");
    reset_calls();
    inject(W_READ, EINTR, 1, fd);
    OK(os_watch_read(&w, ev, 4, &n)); /* restarted */
    FIRED();
    CHECK(calls[W_READ] == 2 && n == 2 && ev[0].what == OS_WATCH_CREATE);

    inject(W_READ, EIO, 1, fd);
    EXPECT(os_watch_read(&w, ev, 4, &n), OS_E_IO);
    CHECK(os_native_error() == EIO && n == 0);
    FIRED();

    read_forge = 1;
    inject_fd[W_READ] = fd;
    EXPECT(os_watch_read(&w, ev, 4, &n), OS_E_IO);
    CHECK(os_native_error() == 0 && n == 0 && read_forge == 0);
    EXPECT(os_watch_read(&w, ev, 4, &n), OS_E_AGAIN); /* drained */

    /* A failed kernel removal leaves nothing for the holder to do. */
    reset_calls();
    inject(W_RM, EINVAL, 1, fd);
    os_watch_remove(&w, id);
    FIRED();
    CHECK(calls[W_RM] == 1);
    os_watch_close(&w);
    remove_leaf("e");
}

/* Caller bugs are ASSERTs (A15). */
static os_watch_slot death_slots[1];
static void remove_unheld(void)
{
    os_watch w = OS_WATCH_INIT;
    if (os_watch_init(&w, death_slots, 1) != OS_OK) _exit(3);
    os_watch_remove(&w, 0);
}
static void init_without_slots(void)
{
    os_watch w = OS_WATCH_INIT;
    (void)os_watch_init(&w, death_slots, 0);
}
static void case_watch_misuse(void)
{
    CHECK(assert_death(remove_unheld, "src/os/os_posix.c", "id < w->cap && w->slots[id].holds > 0"));
    CHECK(assert_death(init_without_slots, "src/os/os_posix.c", "slots && cap > 0"));
}

/* ---- metadata copy ------------------------------------------------------- */
static os_xattr_scratch scratch;

static int open_leaf(const char *leaf)
{
    char p[OS_PATH_MAX];
    path_of(p, leaf);
    int fd = open(p, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    CHECK(fd >= 0);
    return fd;
}
static void xset(int fd, const char *name, const void *v, size_t n)
{
    if (fsetxattr(fd, name, v, n, 0) != 0) {
        /* Fail closed (CLAUDE.md): this suite requires user xattrs under build/. */
        fprintf(stderr, "os_persist_test: FAIL fsetxattr %s under %s: %s; this lane "
                        "requires a filesystem with user.* xattrs\n", name, root, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/* fd's names, sorted into one canonical NUL-separated string. */
static size_t names_of(int fd, char *out, size_t cap)
{
    char raw[1024];
    ssize_t n = flistxattr(fd, raw, sizeof raw);
    CHECK(n >= 0);
    const char *v[32];
    size_t k = 0;
    for (size_t at = 0; at < (size_t)n; at += strlen(raw + at) + 1) {
        CHECK(k < 32);
        v[k++] = raw + at;
    }
    for (size_t i = 1; i < k; i++)
        for (size_t j = i; j > 0 && strcmp(v[j - 1], v[j]) > 0; j--) {
            const char *t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    size_t len = 0;
    for (size_t i = 0; i < k; i++) {
        size_t l = strlen(v[i]) + 1;
        CHECK(len + l <= cap);
        memcpy(out + len, v[i], l);
        len += l;
    }
    return len;
}
static bool value_is(int fd, const char *name, const void *v, size_t n)
{
    static char buf[OS_XATTR_MAX];
    ssize_t got_len = fgetxattr(fd, name, buf, sizeof buf);
    return got_len == (ssize_t)n && memcmp(buf, v, n) == 0;
}
static void clear_xattrs(int fd)
{
    char raw[1024];
    ssize_t n = flistxattr(fd, raw, sizeof raw);
    CHECK(n >= 0);
    for (size_t at = 0; at < (size_t)n; at += strlen(raw + at) + 1)
        CHECK(fremovexattr(fd, raw + at) == 0);
}

static void case_meta_roundtrip(void)
{
    int s = open_leaf("src"), d = open_leaf("dst");
    static char big[4000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (char)(i * 7u);
    xset(s, "user.a", "alpha", 5);
    xset(s, "user.empty", "", 0);
    xset(s, "user.big", big, sizeof big);
    xset(d, "user.a", "old", 3);       /* overwritten */
    xset(d, "user.stale", "x", 1);     /* inherited, absent on the source: removed */
    os_file src = os_file_from_fd(s), dst = os_file_from_fd(d);
    reset_calls();
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    CHECK(calls[W_FCHOWN] == 0); /* same owner: no privileged call */
    CHECK(calls[W_REMOVE] == 1 && calls[W_SET] == 3);
    char want[256], have[256];
    size_t wl = names_of(s, want, sizeof want), hl = names_of(d, have, sizeof have);
    CHECK(wl == hl && memcmp(want, have, wl) == 0);
    CHECK(value_is(d, "user.a", "alpha", 5));
    CHECK(value_is(d, "user.empty", "", 0));
    CHECK(value_is(d, "user.big", big, sizeof big));

    /* An empty source set empties the target. */
    clear_xattrs(s);
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    CHECK(names_of(d, have, sizeof have) == 0);
    CHECK(close(s) == 0 && close(d) == 0);
    remove_leaf("src");
    remove_leaf("dst");
}

static void case_meta_owner(void)
{
    int s = open_leaf("src"), d = open_leaf("dst");
    os_file src = os_file_from_fd(s), dst = os_file_from_fd(d);
    /* dst appears owned by someone else: os chowns it to src's owner. */
    reset_calls();
    fstat_bump_fd = d;
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    CHECK(fstat_bump_fd == -1 && calls[W_FCHOWN] == 1);
    struct stat a, b;
    CHECK(fstat(s, &a) == 0 && fstat(d, &b) == 0 && a.st_uid == b.st_uid && a.st_gid == b.st_gid);
    /* Refused: the caller learns it, and nothing further is attempted. */
    reset_calls();
    fstat_bump_fd = d;
    inject(W_FCHOWN, EPERM, 1, d);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_ACCESS);
    CHECK(os_native_error() == EPERM && calls[W_LIST] == 0);
    FIRED();
    inject(W_FSTAT, EIO, 1, s);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_IO);
    FIRED();
    CHECK(close(s) == 0 && close(d) == 0);
    remove_leaf("src");
    remove_leaf("dst");
}

static void case_meta_rules(void)
{
    int s = open_leaf("src"), d = open_leaf("dst");
    os_file src = os_file_from_fd(s), dst = os_file_from_fd(d);

    /* A label the target already holds identically: a refused set counts. */
    xset(s, "user.label", "L", 1);
    xset(d, "user.label", "L", 1);
    reset_calls();
    inject(W_SET, EPERM, 1, d);
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    FIRED();
    CHECK(calls[W_SET] == 1 && calls[W_GET] == 2);
    /* A different value: the set's refusal is the result. */
    xset(d, "user.label", "M", 1);
    inject(W_SET, EPERM, 1, d);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_ACCESS);
    CHECK(os_native_error() == EPERM);
    FIRED();
    CHECK(value_is(d, "user.label", "M", 1));

    /* An inherited name that cannot be removed. */
    xset(d, "user.stale", "x", 1);
    inject(W_REMOVE, EPERM, 1, d);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_ACCESS);
    FIRED();

    /* Unsupported is the empty set: both unsupported is nothing to copy... */
    reset_calls();
    inject(W_LIST, EOPNOTSUPP, 2, -1);
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    FIRED();
    CHECK(calls[W_SET] == 0 && calls[W_REMOVE] == 0);
    /* ...an unsupported source empties the target... */
    inject(W_LIST, EOPNOTSUPP, 1, s);
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    FIRED();
    char have[256];
    CHECK(names_of(d, have, sizeof have) == 0);
    /* ...and a target that cannot hold the source's names refuses. */
    inject(W_LIST, EOPNOTSUPP, 1, d);
    inject(W_SET, EOPNOTSUPP, 1, d);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_NOTSUP);
    FIRED();

    /* The source changed under the copy: a listed name is gone. */
    inject(W_GET, ENODATA, 1, s);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_NOENT);
    FIRED();
    /* A list past the kernel limit. */
    inject(W_LIST, E2BIG, 1, s);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_RANGE);
    FIRED();
    /* Other list and value errors are refused as themselves. */
    inject(W_LIST, EACCES, 1, d);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_ACCESS);
    FIRED();
    inject(W_GET, EIO, 1, s);
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_IO);
    CHECK(os_native_error() == EIO);
    FIRED();
    /* A malformed list. */
    list_forge_fd = s;
    EXPECT(os_file_copy_metadata(&dst, &src, &scratch), OS_E_IO);
    CHECK(os_native_error() == 0 && list_forge_fd == -1);
    /* Interrupted calls restart. */
    reset_calls();
    inject(W_SET, EINTR, 1, d);
    OK(os_file_copy_metadata(&dst, &src, &scratch));
    FIRED();
    CHECK(calls[W_SET] == 2 && value_is(d, "user.label", "L", 1));

    CHECK(close(s) == 0 && close(d) == 0);
    remove_leaf("src");
    remove_leaf("dst");
}

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    {"watch_events", case_watch_events},     {"watch_capacity", case_watch_capacity},
    {"watch_overflow", case_watch_overflow}, {"watch_faults", case_watch_faults},
    {"watch_misuse", case_watch_misuse},     {"meta_roundtrip", case_meta_roundtrip},
    {"meta_owner", case_meta_owner},         {"meta_rules", case_meta_rules},
};

int main(int argc, char **argv)
{
    char cwd[OS_PATH_MAX];
    OK(os_cwd(cwd, sizeof cwd));
    int n = snprintf(root, sizeof root, "%s/build/os-persist-XXXXXX", cwd);
    CHECK(n > 0 && (size_t)n < sizeof root);
    CHECK(mkdtemp(root) != NULL);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0) continue;
        printf("os_persist_test: %s\n", cases[i].name);
        fflush(stdout);
        cases[i].run();
        no_injection_left(__LINE__);
    }
    OK(os_rmdir(root));
    puts("os_persist_test: ok");
    return EXIT_SUCCESS;
}
