/* Local stream endpoint and wait set (v2-0fv.13): the disciplines os owns,
 * fault-injected once. The kernel calls under os are wrapped at link time
 * (Makefile OS_ENDPOINT_TEST_WRAPS) so a case can make the next call fail
 * with a chosen errno before it reaches the kernel. */
#include "os/os.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>

static void check(bool ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "os_endpoint_test.c:%d: FAIL %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check((x), #x, __LINE__)
static void expect(os_err got, os_err want, const char *expression, int line)
{
    if (got != want) {
        fprintf(stderr, "os_endpoint_test.c:%d: FAIL %s -> %d (native %d), expected %d\n", line,
                expression, got, errno, want);
        exit(EXIT_FAILURE);
    }
}
#define EXPECT(call, error) expect((call), (error), #call, __LINE__)
#define OK(call) EXPECT(call, OS_OK)

/* (P) SI conversion. */
#define MS UINT64_C(1000000)

/* ---- injection ---------------------------------------------------------- */
enum { W_RECV, W_SEND, W_ACCEPT4, W_CONNECT, W_BIND, W_EPOLL_WAIT, W_SETTIME, W_COUNT };
static int inject_left[W_COUNT], inject_errno[W_COUNT];
static unsigned calls[W_COUNT];
static void inject(int w, int e, int times) { inject_left[w] = times; inject_errno[w] = e; }
static bool take(int w)
{
    calls[w]++;
    if (inject_left[w] <= 0) return false;
    inject_left[w]--;
    errno = inject_errno[w];
    return true;
}
ssize_t __real_recv(int, void *, size_t, int);
ssize_t __real_send(int, const void *, size_t, int);
int __real_accept4(int, struct sockaddr *, socklen_t *, int);
int __real_connect(int, const struct sockaddr *, socklen_t);
int __real_bind(int, const struct sockaddr *, socklen_t);
int __real_epoll_wait(int, struct epoll_event *, int, int);
int __real_timerfd_settime(int, int, const struct itimerspec *, struct itimerspec *);
ssize_t __wrap_recv(int, void *, size_t, int);
ssize_t __wrap_send(int, const void *, size_t, int);
int __wrap_accept4(int, struct sockaddr *, socklen_t *, int);
int __wrap_connect(int, const struct sockaddr *, socklen_t);
int __wrap_bind(int, const struct sockaddr *, socklen_t);
int __wrap_epoll_wait(int, struct epoll_event *, int, int);
int __wrap_timerfd_settime(int, int, const struct itimerspec *, struct itimerspec *);
ssize_t __wrap_recv(int fd, void *b, size_t n, int f) { return take(W_RECV) ? -1 : __real_recv(fd, b, n, f); }
ssize_t __wrap_send(int fd, const void *b, size_t n, int f) { return take(W_SEND) ? -1 : __real_send(fd, b, n, f); }
int __wrap_accept4(int fd, struct sockaddr *a, socklen_t *l, int f) { return take(W_ACCEPT4) ? -1 : __real_accept4(fd, a, l, f); }
int __wrap_connect(int fd, const struct sockaddr *a, socklen_t l) { return take(W_CONNECT) ? -1 : __real_connect(fd, a, l); }
int __wrap_bind(int fd, const struct sockaddr *a, socklen_t l) { return take(W_BIND) ? -1 : __real_bind(fd, a, l); }
int __wrap_epoll_wait(int fd, struct epoll_event *e, int n, int ms) { return take(W_EPOLL_WAIT) ? -1 : __real_epoll_wait(fd, e, n, ms); }
int __wrap_timerfd_settime(int fd, int fl, const struct itimerspec *v, struct itimerspec *o)
{
    return take(W_SETTIME) ? -1 : __real_timerfd_settime(fd, fl, v, o);
}
static void no_injection_left(void)
{
    for (int w = 0; w < W_COUNT; w++) CHECK(inject_left[w] == 0);
}

/* ---- fixture ------------------------------------------------------------ */
static char root[OS_PATH_MAX];
static unsigned name_seq;
/* A process-unique abstract name. */
static void abstract_name(char *out, size_t cap)
{
    int n = snprintf(out, cap, "os-endpoint-test-%ld-%u", (long)getpid(), name_seq++);
    CHECK(n > 0 && (size_t)n < cap);
}
static void listen_abstract(os_listener *l, char *name, size_t cap)
{
    abstract_name(name, cap);
    OK(os_listen_local(l, NULL, name, 4, NULL));
}
static void connected(os_listener *l, os_stream *client, os_stream *server)
{
    char name[64];
    listen_abstract(l, name, sizeof name);
    OK(os_connect_local(client, NULL, name));
    OK(os_listener_accept(l, server));
}

/* ---- EINTR is restarted, once, inside os -------------------------------- */
static void case_eintr(void)
{
    os_listener l = OS_LISTENER_INIT;
    os_stream c = OS_STREAM_INIT, s = OS_STREAM_INIT;
    char name[64];
    abstract_name(name, sizeof name);
    inject(W_BIND, EINTR, 1);
    OK(os_listen_local(&l, NULL, name, 4, NULL));
    inject(W_CONNECT, EINTR, 1);
    OK(os_connect_local(&c, NULL, name));
    inject(W_ACCEPT4, EINTR, 1);
    OK(os_listener_accept(&l, &s));

    size_t n = 0;
    inject(W_SEND, EINTR, 1);
    OK(os_stream_write(&c, "abc", 3, &n));
    CHECK(n == 3);
    char buf[8];
    inject(W_RECV, EINTR, 1);
    OK(os_stream_peek(&s, buf, 1, &n));
    CHECK(n == 1 && buf[0] == 'a');
    inject(W_RECV, EINTR, 1);
    OK(os_stream_read(&s, buf, sizeof buf, &n));
    CHECK(n == 3 && memcmp(buf, "abc", 3) == 0);

    /* A wait interrupted before the member is ready still sees it, and an
     * interrupted finite wait still lasts until its deadline. */
    os_waitset ws = OS_WAITSET_INIT;
    OK(os_waitset_init(&ws));
    OK(os_waitset_add(&ws, os_stream_fd(&s), OS_WAIT_IN, 7));
    OK(os_stream_write(&c, "x", 1, &n));
    os_wait_event ev[4];
    inject(W_EPOLL_WAIT, EINTR, 1);
    OK(os_waitset_wait(&ws, os_now_ns() + 1000 * MS, ev, 4, &n));
    CHECK(n == 1 && ev[0].tag == 7 && (ev[0].events & OS_WAIT_IN));
    OK(os_stream_read(&s, buf, sizeof buf, &n));
    uint64_t deadline = os_now_ns() + 30 * MS;
    inject(W_EPOLL_WAIT, EINTR, 1);
    OK(os_waitset_wait(&ws, deadline, ev, 4, &n));
    CHECK(n == 0 && os_now_ns() >= deadline);
    no_injection_left();

    os_waitset_close(&ws);
    os_stream_close(&c);
    os_stream_close(&s);
    os_listener_close(&l);
}

/* ---- a closed peer is an error, never SIGPIPE --------------------------- */
static volatile sig_atomic_t sigpipes;
static void on_sigpipe(int sig) { (void)sig; sigpipes++; }
static void case_sigpipe(void)
{
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigpipe;
    CHECK(sigaction(SIGPIPE, &sa, &old) == 0);
    os_stream a = OS_STREAM_INIT, b = OS_STREAM_INIT;
    OK(os_stream_pair(&a, &b));
    os_stream_close(&b);
    size_t n = 7;
    EXPECT(os_stream_write(&a, "x", 1, &n), OS_E_IO);
    CHECK(n == 0 && errno == EPIPE);
    CHECK(sigpipes == 0);
    /* The same through a listener-accepted stream. */
    os_listener l = OS_LISTENER_INIT;
    os_stream c = OS_STREAM_INIT, s = OS_STREAM_INIT;
    connected(&l, &c, &s);
    os_stream_close(&s);
    EXPECT(os_stream_write(&c, "x", 1, &n), OS_E_IO);
    CHECK(sigpipes == 0);
    os_stream_close(&c);
    os_listener_close(&l);
    os_stream_close(&a);
    CHECK(sigaction(SIGPIPE, &old, NULL) == 0);
}

/* ---- every descriptor os makes is close-on-exec; streams nonblocking ---- */
/* (E) More descriptors than this test opens at once (a dozen). */
#define FD_SCAN 1024
static void open_fds(bool out[FD_SCAN])
{
    memset(out, 0, FD_SCAN * sizeof out[0]);
    DIR *d = opendir("/proc/self/fd");
    CHECK(d != NULL);
    int self = dirfd(d);
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.') continue;
        long fd = strtol(e->d_name, NULL, 10);
        if (fd != self && fd >= 0 && fd < FD_SCAN) out[fd] = true;
    }
    closedir(d);
}
static bool cloexec(int fd) { int f = fcntl(fd, F_GETFD); return f >= 0 && (f & FD_CLOEXEC); }
static bool nonblocking(int fd) { int f = fcntl(fd, F_GETFL); return f >= 0 && (f & O_NONBLOCK); }
static void case_cloexec(void)
{
    static bool before[FD_SCAN], after[FD_SCAN];
    open_fds(before);
    os_listener l = OS_LISTENER_INIT;
    os_stream c = OS_STREAM_INIT, s = OS_STREAM_INIT, a = OS_STREAM_INIT, b = OS_STREAM_INIT;
    connected(&l, &c, &s);
    OK(os_stream_pair(&a, &b));
    os_waitset ws = OS_WAITSET_INIT;
    OK(os_waitset_init(&ws));
    /* A plain descriptor pair is fixed on adoption. */
    int raw[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, raw) == 0);
    CHECK(!cloexec(raw[0]) && !nonblocking(raw[0]));
    os_stream adopted = OS_STREAM_INIT;
    OK(os_stream_adopt(&adopted, raw[0]));
    CHECK(cloexec(raw[0]) && nonblocking(raw[0]));
    close(raw[1]);
    open_fds(after);
    unsigned fresh = 0;
    for (int fd = 0; fd < FD_SCAN; fd++) {
        if (!after[fd] || before[fd]) continue;
        fresh++;
        if (!cloexec(fd)) fprintf(stderr, "fd %d is not close-on-exec\n", fd);
        CHECK(cloexec(fd));
    }
    /* (P) listener, two connected ends, two pair ends, epoll and timerfd,
     * the adopted end. */
    CHECK(fresh == 8);
    const os_stream *streams[] = {&c, &s, &a, &b, &adopted};
    for (size_t i = 0; i < sizeof streams / sizeof streams[0]; i++) CHECK(nonblocking(os_stream_fd(streams[i])));
    CHECK(nonblocking(os_listener_fd(&l)));
    /* Not a socket: refused before any flag changes. */
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    os_stream bad = OS_STREAM_INIT;
    EXPECT(os_stream_adopt(&bad, pipefd[0]), OS_E_INVAL);
    CHECK(!nonblocking(pipefd[0]));
    close(pipefd[0]);
    close(pipefd[1]);
    os_stream_close(&adopted);
    os_waitset_close(&ws);
    os_stream_close(&a);
    os_stream_close(&b);
    os_stream_close(&c);
    os_stream_close(&s);
    os_listener_close(&l);
}

/* ---- error mapping ------------------------------------------------------ */
static void case_errors(void)
{
    char name[64], longname[200];
    os_listener l = OS_LISTENER_INIT, l2 = OS_LISTENER_INIT;
    os_stream c = OS_STREAM_INIT, s = OS_STREAM_INIT;
    abstract_name(name, sizeof name);
    EXPECT(os_connect_local(&c, NULL, name), OS_E_REFUSED);
    CHECK(os_stream_fd(&c) == -1);
    OK(os_listen_local(&l, NULL, name, 1, NULL));
    EXPECT(os_listen_local(&l2, NULL, name, 1, NULL), OS_E_EXIST);
    CHECK(os_listener_fd(&l2) == -1);
    EXPECT(os_listener_accept(&l, &s), OS_E_AGAIN);
    EXPECT(os_listen_local(&l2, NULL, "", 1, NULL), OS_E_INVAL);
    EXPECT(os_listen_local(&l2, NULL, name, 0, NULL), OS_E_INVAL);
    memset(longname, 'n', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;
    EXPECT(os_listen_local(&l2, NULL, longname, 1, NULL), OS_E_NAMETOOLONG);
    EXPECT(os_connect_local(&c, NULL, longname), OS_E_NAMETOOLONG);

    OK(os_connect_local(&c, NULL, name));
    OK(os_listener_accept(&l, &s));
    uint32_t uid = 0;
    OK(os_stream_peer_uid(&s, &uid));
    CHECK(uid == os_user_id() && uid == (uint32_t)getuid());
    OK(os_stream_peer_uid(&c, &uid));
    CHECK(uid == os_user_id());
    char buf[4];
    size_t n = 9;
    EXPECT(os_stream_read(&s, buf, sizeof buf, &n), OS_E_AGAIN);
    CHECK(n == 0);
    EXPECT(os_stream_peek(&s, buf, 1, &n), OS_E_AGAIN);
    inject(W_RECV, ECONNRESET, 1);
    EXPECT(os_stream_read(&s, buf, sizeof buf, &n), OS_E_IO);
    CHECK(errno == ECONNRESET);
    inject(W_SEND, EAGAIN, 1);
    EXPECT(os_stream_write(&c, "x", 1, &n), OS_E_AGAIN);
    os_stream failed = {123};
    inject(W_ACCEPT4, EMFILE, 1);
    EXPECT(os_listener_accept(&l, &failed), OS_E_IO);
    CHECK(os_stream_fd(&failed) == -1);
    os_stream_close(&c);
    os_stream_close(&s);
    os_listener_close(&l);
    no_injection_left();

    /* Directory names: absent, stale, live, owner-only, bound identity. */
    os_dir dir = OS_DIR_INIT;
    OK(os_dir_open(&dir, root));
    EXPECT(os_connect_local(&c, &dir, "absent.sock"), OS_E_NOENT);
    EXPECT(os_connect_local(&c, &dir, "a/b"), OS_E_INVAL);
    mode_t mask = umask(0);
    os_stat bound;
    OK(os_listen_local(&l, &dir, "live.sock", 2, &bound));
    umask(mask);
    char path[OS_PATH_MAX];
    CHECK(snprintf(path, sizeof path, "%s/live.sock", root) > 0);
    struct stat st;
    CHECK(lstat(path, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);
    CHECK(bound.kind == OS_KIND_SOCKET && bound.id.volume == (uint64_t)st.st_dev &&
          bound.id.index[0] == (uint64_t)st.st_ino && bound.uid == os_user_id());
    os_stat by_name;
    OK(os_stat_at(&dir, "live.sock", OS_STAT_NOFOLLOW, &by_name));
    CHECK(by_name.kind == OS_KIND_SOCKET && os_identity_equal(&by_name.id, &bound.id));
    OK(os_connect_local(&c, &dir, "live.sock"));
    os_stream_close(&c);
    EXPECT(os_listen_local(&l2, &dir, "live.sock", 2, NULL), OS_E_EXIST);
    os_listener_close(&l);
    /* The name outlives its listener: a stale socket refuses. */
    EXPECT(os_connect_local(&c, &dir, "live.sock"), OS_E_REFUSED);
    OK(os_unlink_at(&dir, "live.sock"));
    /* A failed bind leaves nothing behind. */
    inject(W_BIND, EACCES, 1);
    EXPECT(os_listen_local(&l, &dir, "denied.sock", 2, NULL), OS_E_ACCESS);
    CHECK(os_listener_fd(&l) == -1);
    CHECK(snprintf(path, sizeof path, "%s/denied.sock", root) > 0);
    CHECK(lstat(path, &st) != 0);
    /* (P) "/proc/self/fd/<fd>/" plus a 100-byte leaf exceeds sun_path's 108. */
    char leaf[101];
    memset(leaf, 'l', sizeof leaf - 1);
    leaf[sizeof leaf - 1] = 0;
    EXPECT(os_listen_local(&l, &dir, leaf, 2, NULL), OS_E_NAMETOOLONG);
    os_dir_close(&dir);
    no_injection_left();
}

/* ---- the wait set ------------------------------------------------------- */
static void case_waitset(void)
{
    os_waitset ws = OS_WAITSET_INIT, outer = OS_WAITSET_INIT;
    os_wait_event ev[8];
    size_t n = 9;
    EXPECT(os_waitset_wait(&ws, 0, ev, 8, &n), OS_E_INVAL); /* closed */
    OK(os_waitset_init(&ws));
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 0);
    os_stream a = OS_STREAM_INIT, b = OS_STREAM_INIT;
    OK(os_stream_pair(&a, &b));
    EXPECT(os_waitset_add(&ws, os_stream_fd(&a), OS_WAIT_IN, OS_WAIT_ALARM), OS_E_INVAL);
    EXPECT(os_waitset_add(&ws, os_stream_fd(&a), OS_WAIT_HUP, 1), OS_E_INVAL);
    OK(os_waitset_add(&ws, os_stream_fd(&a), OS_WAIT_IN, 1));
    EXPECT(os_waitset_add(&ws, os_stream_fd(&a), OS_WAIT_IN, 1), OS_E_EXIST);

    /* No early return: an idle finite wait lasts until its deadline. */
    uint64_t deadline = os_now_ns() + 20 * MS;
    OK(os_waitset_wait(&ws, deadline, ev, 8, &n));
    CHECK(n == 0 && os_now_ns() >= deadline);

    /* Readiness is reported and the bytes stay for their owner. */
    size_t done = 0;
    OK(os_stream_write(&b, "hello", 5, &done));
    OK(os_waitset_wait(&ws, OS_DEADLINE_NEVER, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == 1 && ev[0].events == OS_WAIT_IN);
    char buf[8];
    OK(os_stream_read(&a, buf, sizeof buf, &done));
    CHECK(done == 5);
    OK(os_waitset_modify(&ws, os_stream_fd(&a), OS_WAIT_IN | OS_WAIT_OUT, 2));
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == 2 && ev[0].events == OS_WAIT_OUT);

    /* The alarm: one-shot, reported once, then disarmed. */
    unsigned settime = calls[W_SETTIME];
    OK(os_waitset_alarm(&ws, os_now_ns() + 15 * MS));
    OK(os_waitset_modify(&ws, os_stream_fd(&a), OS_WAIT_IN, 2));
    uint64_t start = os_now_ns();
    OK(os_waitset_wait(&ws, OS_DEADLINE_NEVER, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == OS_WAIT_ALARM && os_now_ns() - start >= 10 * MS);
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 0);
    CHECK(calls[W_SETTIME] == settime + 1);
    /* Re-arming the armed deadline makes no call; a past one fires at once;
     * NEVER disarms. */
    uint64_t later = os_now_ns() + 10000 * MS;
    OK(os_waitset_alarm(&ws, later));
    OK(os_waitset_alarm(&ws, later));
    CHECK(calls[W_SETTIME] == settime + 2);
    OK(os_waitset_alarm(&ws, 0));
    OK(os_waitset_wait(&ws, os_now_ns() + 1000 * MS, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == OS_WAIT_ALARM);
    OK(os_waitset_alarm(&ws, os_now_ns() + 5 * MS));
    OK(os_waitset_alarm(&ws, OS_DEADLINE_NEVER));
    deadline = os_now_ns() + 25 * MS;
    OK(os_waitset_wait(&ws, deadline, ev, 8, &n));
    CHECK(n == 0);
    /* A failed arm keeps the previous alarm. */
    inject(W_SETTIME, ENOMEM, 1);
    EXPECT(os_waitset_alarm(&ws, 0), OS_E_NOMEM);
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 0);

    /* Nesting: the inner set's alarm alone wakes the outer set, and the inner
     * owner then polls it. */
    OK(os_waitset_init(&outer));
    OK(os_waitset_add(&outer, os_waitset_fd(&ws), OS_WAIT_IN, 40));
    OK(os_waitset_alarm(&ws, os_now_ns() + 15 * MS));
    OK(os_waitset_wait(&outer, os_now_ns() + 2000 * MS, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == 40);
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == OS_WAIT_ALARM);
    OK(os_waitset_wait(&outer, 0, ev, 8, &n));
    CHECK(n == 0);
    /* A member of the inner set also propagates. */
    OK(os_stream_write(&b, "z", 1, &done));
    OK(os_waitset_wait(&outer, os_now_ns() + 2000 * MS, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == 40);

    /* Hang-up is reported without being requested. */
    os_stream_close(&b);
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 1 && ev[0].tag == 2 && (ev[0].events & OS_WAIT_HUP));
    os_waitset_remove(&ws, os_stream_fd(&a));
    OK(os_stream_read(&a, buf, sizeof buf, &done));
    CHECK(done == 1);
    OK(os_stream_read(&a, buf, sizeof buf, &done));
    CHECK(done == 0); /* end of stream */
    OK(os_waitset_wait(&ws, 0, ev, 8, &n));
    CHECK(n == 0);
    EXPECT(os_waitset_modify(&ws, os_stream_fd(&a), OS_WAIT_IN, 2), OS_E_NOENT);
    os_stream_close(&a);
    os_waitset_close(&outer);
    os_waitset_close(&ws);
    os_waitset_close(&ws); /* idempotent */
    no_injection_left();
}

static const struct { const char *name; void (*run)(void); } cases[] = {
    {"eintr", case_eintr},     {"sigpipe", case_sigpipe}, {"cloexec", case_cloexec},
    {"errors", case_errors},   {"waitset", case_waitset},
};

int main(int argc, char **argv)
{
    char cwd[OS_PATH_MAX];
    OK(os_cwd(cwd, sizeof cwd));
    int n = snprintf(root, sizeof root, "%s/build/os-endpoint-XXXXXX", cwd);
    CHECK(n > 0 && (size_t)n < sizeof root);
    CHECK(mkdtemp(root) != NULL);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0) continue;
        /* (E) A hang guard far above the waits here (at most 2 s each). */
        alarm(60);
        cases[i].run();
        alarm(0);
        printf("os_endpoint_test: %s ok\n", cases[i].name);
        fflush(stdout);
    }
    CHECK(rmdir(root) == 0);
    puts("os_endpoint_test: ok");
    return EXIT_SUCCESS;
}
