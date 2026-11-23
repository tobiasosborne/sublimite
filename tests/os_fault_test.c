/* Mapped-file fault service conformance: the acceptance tables of
 * docs/decisions/wave1-os-fault-lease.md ("Implementation acceptance tests").
 *
 * The service is one process-global installation, so every case runs in a
 * fresh self-exec child. The suite links with -Wl,--wrap for the service's
 * native calls (tests/lanes.mk LDFLAGS_os_fault_test); each wrapper passes
 * through until a case arms it. Test code calls the __real_ entry points
 * itself, so the counters see only the service's calls. */
#include "os/os.h"

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>


static void expect(os_err got, os_err want, const char *expression, int line)
{
    if (got != want) {
        int native = os_native_error();
        fprintf(stderr, "%s -> %s (native %d), expected %s\n", expression, os_err_name(got), native,
                os_err_name(want));
        os_assert_fail(__FILE__, line, expression);
    }
}
#define EXPECT(call, error) expect((call), (error), #call, __LINE__)
#define OK(call) EXPECT(call, OS_OK)

/* The case guard is tests/os_test.c's (v1's per-case hang detector). */
#define CASE_GUARD_SECONDS 120u

static const char *work_root;

/* Defined in os_posix.c only when built with EDIT_TEST_HOOKS (A15); the suite
 * links that object ahead of libcore.a (tests/lanes.mk). */
os_err os_fault_test_next_gen(uint64_t next);

/* ---- native fault injection (-Wl,--wrap) -------------------------------- */
/* Switches and counters the signal handler touches are lock-free atomics
 * (checked in main). Handler-side wrappers call only write, read, _exit and
 * the real entry point, all async-signal-safe. */
void *__real_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int __real_sigaction(int sig, const struct sigaction *act, struct sigaction *old);
int __real_sem_init(sem_t *s, int pshared, unsigned value);
int __real_sem_destroy(sem_t *s);
int __real_sem_wait(sem_t *s);
int __real_sem_post(sem_t *s);
long __real_sysconf(int name);
long __real_syscall(long n, ...);

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int __wrap_sigaction(int sig, const struct sigaction *act, struct sigaction *old);
int __wrap_sem_init(sem_t *s, int pshared, unsigned value);
int __wrap_sem_destroy(sem_t *s);
int __wrap_sem_wait(sem_t *s);
int __wrap_sem_post(sem_t *s);
long __wrap_sysconf(int name);
long __wrap_syscall(long n, ...);

static _Atomic long page_override; /* Nonzero replaces sysconf(_SC_PAGESIZE). */
static _Atomic bool fail_sem_init, fail_query, fail_install, fail_remap, fail_send;
static _Atomic bool pause_remap, gate_wait, wait_eintr;
static _Atomic unsigned n_pagesize, n_sem_init, n_sem_destroy, n_query, n_install, n_remap, n_send, n_wait_entry, n_wait_done, n_post;

/* (P) The retire race holds at most two handlers at once, one per retiree. */
enum { PAUSE_TICKETS = 2 };
static _Atomic unsigned pause_next;
static int paused_pipe[2] = {-1, -1}, go_pipe[PAUSE_TICKETS][2], entered_pipe[2] = {-1, -1},
           wait_go_pipe[2] = {-1, -1};

/* Distinct exit codes name a broken fixture rather than a failed oracle. */
enum { FIXTURE_IO = 96, FIXTURE_TICKET = 95, FIXTURE_SYSCALL = 94 };

static void put_bytes(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) _exit(FIXTURE_IO);
        c += w;
        n -= (size_t)w;
    }
}
static void get_bytes(int fd, void *p, size_t n)
{
    char *c = p;
    while (n) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) _exit(FIXTURE_IO);
        c += r;
        n -= (size_t)r;
    }
}

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    /* Only recovery maps fixed, anonymous, read-only pages. */
    if ((flags & (MAP_FIXED | MAP_ANONYMOUS)) == (MAP_FIXED | MAP_ANONYMOUS) && prot == PROT_READ) {
        atomic_fetch_add(&n_remap, 1);
        if (atomic_load(&pause_remap)) {
            unsigned t = atomic_fetch_add(&pause_next, 1);
            if (t >= PAUSE_TICKETS) _exit(FIXTURE_TICKET);
            put_bytes(paused_pipe[1], &t, sizeof t);
            char go;
            get_bytes(go_pipe[t][0], &go, 1);
        }
        if (atomic_load(&fail_remap)) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
    }
    return __real_mmap(addr, len, prot, flags, fd, off);
}

/* The sanitizer runtimes import sigaction and sysconf themselves, and --wrap
 * redirects every input's references: their startup calls arrive here before
 * the runtime is initialized. These two wrappers therefore carry no sanitizer
 * instrumentation, and main zeroes the counters once a case starts. */
#if defined(__has_attribute)
#if __has_attribute(disable_sanitizer_instrumentation)
#define RUNTIME_SAFE __attribute__((disable_sanitizer_instrumentation))
#endif
#endif
#ifndef RUNTIME_SAFE
#define RUNTIME_SAFE
#endif

RUNTIME_SAFE int __wrap_sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    if (sig == SIGBUS && !act) {
        atomic_fetch_add(&n_query, 1);
        if (atomic_load(&fail_query)) { errno = EINVAL; return -1; }
    } else if (sig == SIGBUS && (act->sa_flags & SA_SIGINFO)) {
        atomic_fetch_add(&n_install, 1);
        if (atomic_load(&fail_install)) { errno = EINVAL; return -1; }
    }
    return __real_sigaction(sig, act, old);
}

RUNTIME_SAFE long __wrap_sysconf(int name)
{
    if (name == _SC_PAGESIZE) {
        atomic_fetch_add(&n_pagesize, 1);
        long o = atomic_load(&page_override);
        if (o) return o;
    }
    return __real_sysconf(name);
}

int __wrap_sem_init(sem_t *s, int pshared, unsigned value)
{
    atomic_fetch_add(&n_sem_init, 1);
    if (atomic_load(&fail_sem_init)) { errno = ENOSYS; return -1; }
    return __real_sem_init(s, pshared, value);
}
int __wrap_sem_destroy(sem_t *s)
{
    atomic_fetch_add(&n_sem_destroy, 1);
    return __real_sem_destroy(s);
}
int __wrap_sem_wait(sem_t *s)
{
    atomic_fetch_add(&n_wait_entry, 1);
    if (atomic_exchange(&wait_eintr, false)) { errno = EINTR; return -1; }
    if (atomic_load(&gate_wait)) {
        char c = 'w';
        put_bytes(entered_pipe[1], &c, 1);
        get_bytes(wait_go_pipe[0], &c, 1);
    }
    int r = __real_sem_wait(s);
    if (r == 0) atomic_fetch_add(&n_wait_done, 1);
    return r;
}
int __wrap_sem_post(sem_t *s)
{
    atomic_fetch_add(&n_post, 1);
    return __real_sem_post(s);
}

long __wrap_syscall(long n, ...)
{
    /* The service's only raw system call is the test trigger's
     * rt_tgsigqueueinfo, passed as four long-sized words (os_posix.c). */
    if (n != SYS_rt_tgsigqueueinfo) _exit(FIXTURE_SYSCALL);
    va_list ap;
    va_start(ap, n);
    long tgid = va_arg(ap, long), tid = va_arg(ap, long), sig = va_arg(ap, long);
    siginfo_t *si = va_arg(ap, siginfo_t *);
    va_end(ap);
    atomic_fetch_add(&n_send, 1);
    if (atomic_load(&fail_send)) { errno = EAGAIN; return -1; }
    return __real_syscall(n, tgid, tid, sig, si);
}

static void open_pipes(void)
{
    ASSERT(pipe(paused_pipe) == 0 && pipe(entered_pipe) == 0 && pipe(wait_go_pipe) == 0);
    for (int i = 0; i < PAUSE_TICKETS; i++) ASSERT(pipe(go_pipe[i]) == 0);
}
/* Waits for a handler to stop inside the recovery remap; returns its ticket. */
static unsigned await_pause(void)
{
    unsigned t = PAUSE_TICKETS;
    get_bytes(paused_pipe[0], &t, sizeof t);
    ASSERT(t < PAUSE_TICKETS);
    return t;
}
static void release_pause(unsigned t)
{
    char c = 'g';
    put_bytes(go_pipe[t][1], &c, 1);
}

/* ---- fixtures ------------------------------------------------------------- */
/* (P) 251 is prime, so the pattern has no power-of-two period and a page
 * shifted view cannot match; it is never zero, so recovery fill is visible. */
static unsigned char pattern_at(size_t i) { return (unsigned char)(1u + i % 251u); }

/* An open, already unlinked file of len pattern bytes: nothing is left
 * behind even when a child dies on purpose. */
static os_file pattern_file(const char *name, size_t len)
{
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &d, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    OK(os_unlink_at(&d, name));
    os_dir_close(&d);
    unsigned char *buf = malloc(len);
    ASSERT(buf != NULL);
    for (size_t i = 0; i < len; i++) buf[i] = pattern_at(i);
    size_t done = 0;
    OK(os_file_pwrite(&f, buf, len, 0, &done));
    ASSERT(done == len);
    free(buf);
    return f;
}
static os_map pattern_map(const char *name, size_t len)
{
    os_file f = pattern_file(name, len);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, len));
    os_file_close(&f);
    return m;
}
static os_map sub(const os_map *m, size_t off, size_t len)
{
    ASSERT(off < m->len && len <= m->len - off);
    os_map s = {(const unsigned char *)m->addr + off, len};
    return s;
}
static bool is_pattern(const os_map *m, size_t from, size_t to)
{
    const volatile unsigned char *v = m->addr;
    for (size_t i = from; i < to; i++) if (v[i] != pattern_at(i)) return false;
    return true;
}
static bool is_zero(const os_map *m, size_t from, size_t to)
{
    const volatile unsigned char *v = m->addr;
    for (size_t i = from; i < to; i++) if (v[i] != 0) return false;
    return true;
}

static const os_fault_id ABSENT = OS_FAULT_ID_INIT;
static bool absent(os_fault_id id) { return id.slot == ABSENT.slot && id.gen == ABSENT.gen; }
/* Any value no poll can legitimately write in these fixtures. */
#define UNTOUCHED UINT64_C(0x5a5a5a5a5a5a5a5a)
static uint64_t epoch_of(os_fault_id id)
{
    uint64_t e = UNTOUCHED;
    OK(os_fault_poll(id, &e));
    return e;
}
static void expect_gone(os_fault_id id)
{
    uint64_t e = UNTOUCHED;
    EXPECT(os_fault_poll(id, &e), OS_E_NOENT);
    ASSERT(e == UNTOUCHED);
}
static os_fault_id registered(const os_map *view)
{
    os_fault_id id = {UINT32_MAX, UINT64_MAX};
    OK(os_fault_register(view, &id));
    ASSERT(id.gen != 0 && id.slot < OS_FAULT_SLOTS);
    return id;
}

/* A real kernel-style SIGBUS to this thread with any code, sent by the test. */
static void send_bus(const void *addr, int code)
{
    siginfo_t si;
    memset(&si, 0, sizeof si);
    si.si_signo = SIGBUS;
    si.si_code = code;
    si.si_addr = (void *)(uintptr_t)addr;
    ASSERT(__real_syscall(SYS_rt_tgsigqueueinfo, (long)getpid(), (long)gettid(), (long)SIGBUS, &si) == 0);
}

static void set_prior(void (*handler)(int), void (*info)(int, siginfo_t *, void *), unsigned flags,
                      int masked)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    if (info) sa.sa_sigaction = info;
    else sa.sa_handler = handler;
    sa.sa_flags = (int)(flags | (info ? SA_SIGINFO : 0u));
    ASSERT(sigemptyset(&sa.sa_mask) == 0);
    if (masked) ASSERT(sigaddset(&sa.sa_mask, masked) == 0);
    ASSERT(__real_sigaction(SIGBUS, &sa, NULL) == 0);
}
static struct sigaction current_bus(void)
{
    struct sigaction sa;
    ASSERT(__real_sigaction(SIGBUS, NULL, &sa) == 0);
    return sa;
}
/* Handler, flags and every signal of the mask. */
static bool same_action(const struct sigaction *a, const struct sigaction *b)
{
    if (a->sa_sigaction != b->sa_sigaction || a->sa_flags != b->sa_flags) return false;
    for (int sig = 1; sig <= SIGRTMAX; sig++)
        if (sigismember(&a->sa_mask, sig) != sigismember(&b->sa_mask, sig)) return false;
    return true;
}

/* Prior-handler probes. Written from a signal handler, read after it. */
static volatile sig_atomic_t prior_calls, prior_sig, prior_code, prior_ctx_ok;
static void *volatile prior_addr;
static void prior_simple(int sig)
{
    prior_calls++;
    prior_sig = sig;
    errno = EIO; /* The service must restore the interrupted errno. */
}
static void prior_info(int sig, siginfo_t *si, void *uc)
{
    prior_calls++;
    prior_sig = sig;
    prior_code = si->si_code;
    prior_addr = si->si_addr;
    /* The interrupted context ran with SIGBUS unblocked: the original
     * ucontext arrived, not one built by the service. */
    prior_ctx_ok = uc && sigismember(&((ucontext_t *)uc)->uc_sigmask, SIGBUS) == 0;
    errno = EIO;
}

/* ---- single-thread public cases -------------------------------------------- */
static void case_stale(void)
{
    size_t ps = os_page_size();
    os_map a = pattern_map("a", 3 * ps), b = pattern_map("b", 3 * ps);
    os_fault_id ia = registered(&a);
    ASSERT(epoch_of(ia) == 0);
    OK(os_fault_test_trigger(&a, ps + 1));
    ASSERT(epoch_of(ia) == 1);

    os_fault_id saved = ia; /* A value copy outlives the registration. */
    OK(os_fault_retire(ia));
    expect_gone(saved); /* Before reuse: the slot is free. */
    os_fault_id ib = registered(&b);
    ASSERT(ib.slot == saved.slot && ib.gen != saved.gen);
    ASSERT(epoch_of(ib) == 0);
    expect_gone(saved); /* After reuse: never the new tenant's epoch. */
    EXPECT(os_fault_retire(saved), OS_E_NOENT);
    ASSERT(epoch_of(ib) == 0);

    OK(os_fault_test_trigger(&b, 1));
    ASSERT(epoch_of(ib) == 1);
    expect_gone(saved);
    OK(os_fault_retire(ib));
    expect_gone(ib);
    EXPECT(os_fault_retire(ib), OS_E_NOENT);
    os_unmap(&a);
    os_unmap(&b);
}

static void case_args(void)
{
    size_t ps = os_page_size();
    os_map v = pattern_map("v", 4 * ps);
    os_fault_id id = {7, 7};
    uint64_t e = UNTOUCHED;

    EXPECT(os_fault_register(&v, NULL), OS_E_INVAL);
    EXPECT(os_fault_register(NULL, &id), OS_E_INVAL);
    ASSERT(absent(id));
    os_map bad = {NULL, ps};
    id.slot = 7;
    EXPECT(os_fault_register(&bad, &id), OS_E_INVAL);
    ASSERT(absent(id));
    bad = sub(&v, 0, 0);
    EXPECT(os_fault_register(&bad, &id), OS_E_INVAL);
    bad = sub(&v, 1, ps);
    EXPECT(os_fault_register(&bad, &id), OS_E_INVAL);
    /* (P) Ends past the address space: the logical end, then only its page
     * rounding. Neither extent is mapped; validation rejects both. */
    bad.addr = (const void *)(UINTPTR_MAX - ps + 1);
    bad.len = ps;
    EXPECT(os_fault_register(&bad, &id), OS_E_INVAL);
    bad.addr = (const void *)(UINTPTR_MAX - 2 * ps + 1);
    bad.len = ps + 1;
    EXPECT(os_fault_register(&bad, &id), OS_E_INVAL);
    ASSERT(absent(id));

    os_fault_id ok = registered(&v);
    os_fault_id malformed[] = {{ok.slot, 0}, {OS_FAULT_SLOTS, ok.gen}, {UINT32_MAX, ok.gen}};
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; i++) {
        EXPECT(os_fault_poll(malformed[i], &e), OS_E_INVAL);
        EXPECT(os_fault_retire(malformed[i]), OS_E_INVAL);
    }
    ASSERT(e == UNTOUCHED);
    EXPECT(os_fault_poll(ok, NULL), OS_E_INVAL);
    os_fault_id other = {ok.slot + 1u, ok.gen};
    expect_gone(other);
    EXPECT(os_fault_retire(other), OS_E_NOENT);
    other.slot = ok.slot;
    other.gen = ok.gen + 1u;
    expect_gone(other);

    EXPECT(os_fault_test_trigger(NULL, 0), OS_E_INVAL);
    EXPECT(os_fault_test_trigger(&v, v.len), OS_E_INVAL);
    bad.addr = NULL;
    bad.len = ps;
    EXPECT(os_fault_test_trigger(&bad, 0), OS_E_INVAL);
    ASSERT(epoch_of(ok) == 0);
    OK(os_fault_retire(ok));

    /* Overlap is decided on page-rounded extents. */
    os_fault_id r1 = registered(&(os_map){v.addr, ps + 1}); /* rounds to [0, 2 pages) */
    os_map dup = sub(&v, 0, ps + 1), second = sub(&v, ps, ps), r2v = sub(&v, 2 * ps, ps),
           r3v = sub(&v, 3 * ps, ps);
    id.slot = 7;
    EXPECT(os_fault_register(&dup, &id), OS_E_EXIST);
    ASSERT(absent(id));
    EXPECT(os_fault_register(&second, &id), OS_E_EXIST);
    EXPECT(os_fault_register(&v, &id), OS_E_EXIST);
    os_fault_id r2 = registered(&r2v), r3 = registered(&r3v); /* adjacent */
    OK(os_fault_test_trigger(&r2v, 1));
    ASSERT(epoch_of(r1) == 0 && epoch_of(r2) == 1 && epoch_of(r3) == 0);
    ASSERT(is_pattern(&v, 0, 2 * ps) && is_zero(&v, 2 * ps, 3 * ps) && is_pattern(&v, 3 * ps, 4 * ps));
    OK(os_fault_retire(r1));
    OK(os_fault_retire(r2));
    OK(os_fault_retire(r3));
    os_unmap(&v);

    /* Distinct views of one file register and taint independently. */
    os_file f = pattern_file("same", 2 * ps);
    os_map m1 = {0}, m2 = {0};
    OK(os_map_file(&m1, &f, 0, 2 * ps));
    OK(os_map_file(&m2, &f, 0, 2 * ps));
    os_file_close(&f);
    os_fault_id i1 = registered(&m1), i2 = registered(&m2);
    OK(os_fault_test_trigger(&m1, 0));
    ASSERT(epoch_of(i1) == 1 && epoch_of(i2) == 0);
    ASSERT(is_zero(&m1, 0, 2 * ps) && is_pattern(&m2, 0, 2 * ps));
    OK(os_fault_retire(i1));
    OK(os_fault_retire(i2));
    os_unmap(&m1);
    os_unmap(&m2);
}

static void case_capacity(void)
{
    size_t ps = os_page_size();
    os_map v = pattern_map("cap", (OS_FAULT_SLOTS + 1) * ps);
    static os_fault_id ids[OS_FAULT_SLOTS];
    for (uint32_t i = 0; i < OS_FAULT_SLOTS; i++) {
        os_map one = sub(&v, i * ps, ps);
        ids[i] = registered(&one);
        ASSERT(ids[i].slot == i); /* The lowest free index. */
        ASSERT(i == 0 || ids[i].gen > ids[i - 1].gen);
    }
    os_map extra = sub(&v, OS_FAULT_SLOTS * ps, ps);
    os_fault_id x = {7, 7};
    EXPECT(os_fault_register(&extra, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    /* (P) An interior index: neither the lowest nor the last issued. */
    const uint32_t k = OS_FAULT_SLOTS / 2;
    OK(os_fault_retire(ids[k]));
    x = registered(&extra);
    ASSERT(x.slot == k && x.gen > ids[OS_FAULT_SLOTS - 1].gen);
    expect_gone(ids[k]);
    ids[k] = x;
    for (uint32_t i = 0; i < OS_FAULT_SLOTS; i++) OK(os_fault_retire(ids[i]));
    os_unmap(&v);
}

static void case_recover(void)
{
    /* (P) One page per role: prefix, earlier and later suffix start, then a
     * partial final page. */
    size_t ps = os_page_size(), len = 3 * ps + ps / 2;
    os_file f = pattern_file("recover", len);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, len));
    os_fault_id id = registered(&m);
    OK(os_fault_test_trigger(&m, 2 * ps));
    ASSERT(epoch_of(id) == 1);
    ASSERT(is_pattern(&m, 0, 2 * ps) && is_zero(&m, 2 * ps, len));
    OK(os_fault_test_trigger(&m, ps + ps / 2));
    ASSERT(epoch_of(id) == 2);
    ASSERT(is_pattern(&m, 0, ps) && is_zero(&m, ps, len));
    OK(os_fault_retire(id));
    os_unmap(&m);
    OK(os_map_file(&m, &f, 0, len)); /* Recovery never touched the file. */
    ASSERT(is_pattern(&m, 0, len));
    os_unmap(&m);
    os_file_close(&f);
}

static void case_truncate(void)
{
    /* A real kernel SIGBUS: the file shrinks under a registered view. */
    size_t ps = os_page_size();
    os_file f = pattern_file("truncate", 3 * ps);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, 3 * ps));
    os_fault_id id = registered(&m);
    OK(os_file_set_size(&f, ps));
    const volatile unsigned char *v = m.addr;
    ASSERT(v[ps + 1] == 0);
    ASSERT(epoch_of(id) == 1);
    ASSERT(is_pattern(&m, 0, ps) && is_zero(&m, ps, 3 * ps));
    ASSERT(epoch_of(id) == 1); /* The recovered suffix does not fault again. */
    OK(os_fault_retire(id));
    os_unmap(&m);
    os_file_close(&f);
}

/* ---- chaining: each child sets the prior disposition, then installs ------ */
static void install_service(void)
{
    static os_map reg;
    reg = pattern_map("reg", os_page_size());
    (void)registered(&reg);
}

/* A native fault in a view no registration covers. Surviving it fails. */
static void stray_native_fault(void)
{
    size_t ps = os_page_size();
    os_file f = pattern_file("stray", 2 * ps);
    os_map stray = {0};
    OK(os_map_file(&stray, &f, 0, 2 * ps));
    OK(os_file_set_size(&f, ps));
    const volatile unsigned char *v = stray.addr;
    (void)v[ps];
    fprintf(stderr, "os_fault_test.c: survived an unrepaired fault\n");
    exit(EXIT_FAILURE);
}

static void case_chain_default(void)
{
    set_prior(SIG_DFL, NULL, 0, 0);
    install_service();
    stray_native_fault();
}

/* A prior SIG_IGN is SIG_DFL: an unrepaired fault never returns to the
 * faulting instruction with nothing changed. */
static void case_chain_ignore(void)
{
    set_prior(SIG_IGN, NULL, 0, 0);
    install_service();
    stray_native_fault();
}

static void case_chain_simple(void)
{
    set_prior(prior_simple, NULL, 0, 0);
    install_service();
    size_t ps = os_page_size();
    os_map stray = pattern_map("stray", ps), mine = pattern_map("mine", 2 * ps);
    errno = EDOM;
    OK(os_fault_test_trigger(&stray, 1));
    ASSERT(errno == EDOM);
    ASSERT(prior_calls == 1 && prior_sig == SIGBUS);

    /* An unrelated code at a registered address goes to the prior handler
     * unrepaired; a repaired fault (either recovery code) does not. */
    os_fault_id id = registered(&mine);
    send_bus((const unsigned char *)mine.addr + 1, BUS_ADRALN);
    ASSERT(prior_calls == 2 && epoch_of(id) == 0 && is_pattern(&mine, 0, 2 * ps));
    send_bus((const unsigned char *)mine.addr + ps, BUS_OBJERR);
    ASSERT(prior_calls == 2 && epoch_of(id) == 1 && is_pattern(&mine, 0, ps) && is_zero(&mine, ps, 2 * ps));
    send_bus((const unsigned char *)mine.addr, BUS_ADRERR);
    ASSERT(prior_calls == 2 && epoch_of(id) == 2 && is_zero(&mine, 0, 2 * ps));
    OK(os_fault_retire(id));
}

static void case_chain_siginfo(void)
{
    set_prior(NULL, prior_info, 0, 0);
    install_service();
    os_map stray = pattern_map("stray", os_page_size());
    errno = EDOM;
    OK(os_fault_test_trigger(&stray, 1));
    ASSERT(errno == EDOM);
    ASSERT(prior_calls == 1 && prior_sig == SIGBUS && prior_code == BUS_ADRERR);
    ASSERT(prior_addr == (const unsigned char *)stray.addr + 1 && prior_ctx_ok);
}

/* The one ASSERT (A15): a fixed message on stderr, then death by SIGABRT. */
static void case_assert(void)
{
    os_dir d = OS_DIR_INIT;
    OK(os_dir_open(&d, work_root));
    os_file f = OS_FILE_INIT;
    OK(os_file_open_at(&f, &d, "stderr", OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL));
    ASSERT(dup2(os_file_fd(&f), STDERR_FILENO) == STDERR_FILENO);
    ASSERT(prior_calls == 1); /* No prior handler ran in this child. */
    exit(EXIT_FAILURE);
}

/* Generation exhaustion at full width (A15): NOSPC in every build, issued ids
 * keep working, and a free slot does not help. */
static void case_exhaustion(void)
{
    size_t ps = os_page_size();
    os_map v = pattern_map("gens", 4 * ps);
    os_map p0 = sub(&v, 0, ps), p1 = sub(&v, ps, ps), p2 = sub(&v, 2 * ps, ps), p3 = sub(&v, 3 * ps, ps);
    os_fault_id a = registered(&p0);
    EXPECT(os_fault_test_next_gen(a.gen), OS_E_INVAL); /* Forward only. */
    OK(os_fault_test_next_gen(UINT64_MAX - 1));
    os_fault_id b = registered(&p1), c = registered(&p2), x = {7, 7};
    ASSERT(b.gen == UINT64_MAX - 1 && c.gen == UINT64_MAX);
    EXPECT(os_fault_register(&p3, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    OK(os_fault_retire(b));
    x.slot = 7;
    EXPECT(os_fault_register(&p3, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    expect_gone(b);
    OK(os_fault_test_trigger(&p0, 1));
    OK(os_fault_test_trigger(&p2, 1));
    ASSERT(epoch_of(a) == 1 && epoch_of(c) == 1);
    OK(os_fault_retire(a));
    OK(os_fault_retire(c));
    os_unmap(&v);
}

/* ---- native errors ---------------------------------------------------------- */
static void expect_unavailable(void)
{
    /* A distinctive prior: handler, flag and mask must all survive. */
    set_prior(prior_simple, NULL, SA_RESTART, SIGUSR1);
    struct sigaction before = current_bus();
    os_map v = pattern_map("v", os_page_size());
    for (int round = 0; round < 2; round++) { /* (P) once to fail, once to prove it sticks */
        os_fault_id id = {7, 7};
        EXPECT(os_fault_register(&v, &id), OS_E_NOTSUP);
        ASSERT(absent(id));
    }
    struct sigaction after = current_bus();
    ASSERT(same_action(&before, &after));
    os_unmap(&v);
}
static void case_err_pagesize(void)
{
    atomic_store(&page_override, 3); /* (P) positive but not a power of two */
    expect_unavailable();
    ASSERT(atomic_load(&n_pagesize) == 1 && atomic_load(&n_install) == 0);
}
static void case_err_sem(void)
{
    atomic_store(&fail_sem_init, true);
    expect_unavailable();
    ASSERT(atomic_load(&n_sem_init) == 1 && atomic_load(&n_install) == 0);
}
static void case_err_query(void)
{
    atomic_store(&fail_query, true);
    expect_unavailable();
    ASSERT(atomic_load(&n_query) == 1 && atomic_load(&n_install) == 0);
    ASSERT(atomic_load(&n_sem_init) == 1 && atomic_load(&n_sem_destroy) == 1);
}
static void case_err_install(void)
{
    atomic_store(&fail_install, true);
    expect_unavailable();
    ASSERT(atomic_load(&n_install) == 1);
    ASSERT(atomic_load(&n_sem_init) == 1 && atomic_load(&n_sem_destroy) == 1);
}
static void case_err_remap(void)
{
    set_prior(NULL, prior_info, 0, 0);
    size_t ps = os_page_size();
    os_map m = pattern_map("m", 2 * ps);
    os_fault_id id = registered(&m);
    atomic_store(&fail_remap, true);
    OK(os_fault_test_trigger(&m, ps + 1));
    ASSERT(atomic_load(&n_remap) == 1);
    ASSERT(prior_calls == 1 && prior_addr == (const unsigned char *)m.addr + ps + 1);
    ASSERT(epoch_of(id) == 1 && is_pattern(&m, 0, 2 * ps)); /* Tainted, not repaired. */
    OK(os_fault_retire(id));
    os_unmap(&m);
}
static void case_err_remap_default(void)
{
    set_prior(SIG_DFL, NULL, 0, 0);
    size_t ps = os_page_size();
    os_map m = pattern_map("m", 2 * ps);
    (void)registered(&m);
    atomic_store(&fail_remap, true);
    (void)os_fault_test_trigger(&m, ps + 1); /* Unrepaired, no prior: death by SIGBUS. */
    fprintf(stderr, "os_fault_test.c: survived a failed repair\n");
    exit(EXIT_FAILURE);
}
static void case_err_send(void)
{
    os_map m = pattern_map("m", os_page_size());
    os_fault_id id = registered(&m);
    atomic_store(&fail_send, true);
    EXPECT(os_fault_test_trigger(&m, 1), OS_E_AGAIN);
    ASSERT(os_native_error() == EAGAIN);
    ASSERT(atomic_load(&n_send) == 1 && epoch_of(id) == 0);
    OK(os_fault_retire(id));
    os_unmap(&m);
}

/* ---- threaded cases (TSan lane) --------------------------------------------- */
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

typedef struct trigger_job { const os_map *view; size_t off; os_err rc; } trigger_job;
static void trigger_body(void *arg)
{
    trigger_job *j = arg;
    j->rc = os_fault_test_trigger(j->view, j->off);
}
typedef struct retire_job { os_fault_id id; os_err rc; _Atomic bool done; } retire_job;
static void retire_body(void *arg)
{
    retire_job *j = arg;
    j->rc = os_fault_retire(j->id);
    atomic_store(&j->done, true);
}
static void spin_until_gone(os_fault_id id)
{
    uint64_t e;
    while (os_fault_poll(id, &e) == OS_OK) os_yield();
}

typedef struct observer {
    os_fault_id id; /* A value copy: the observer never sees either map. */
    uint64_t want;
    barrier *start;
    _Atomic bool reused, bad, final_gone;
    _Atomic unsigned long hits;
} observer;
static void observe_body(void *arg)
{
    observer *o = arg;
    barrier_wait(o->start);
    for (;;) {
        bool after = atomic_load(&o->reused); /* Stored once reuse completed. */
        uint64_t e = UNTOUCHED;
        os_err rc = os_fault_poll(o->id, &e);
        bool gone = rc == OS_E_NOENT && e == UNTOUCHED;
        if (after) {
            atomic_store(&o->final_gone, gone);
            return;
        }
        if (rc == OS_OK && e == o->want) atomic_fetch_add(&o->hits, 1);
        else if (!gone) atomic_store(&o->bad, true);
    }
}

static void case_race_reuse(void)
{
    size_t ps = os_page_size();
    os_map a = pattern_map("a", ps), b = pattern_map("b", ps);
    os_fault_id ia = registered(&a);
    OK(os_fault_test_trigger(&a, 0)); /* A carries 1, B will read pristine 0. */
    barrier start;
    barrier_init(&start, 2);
    observer o = {.id = ia, .want = 1, .start = &start};
    os_thread t;
    OK(os_thread_start(&t, observe_body, &o));
    barrier_wait(&start);
    while (atomic_load(&o.hits) == 0 && !atomic_load(&o.bad)) os_yield(); /* polled while live */
    OK(os_fault_retire(ia));
    expect_gone(ia); /* After completed invalidation, while the slot is free. */
    os_fault_id ib = registered(&b);
    atomic_store(&o.reused, true); /* The observer's next poll follows the reuse. */
    OK(os_thread_join(&t));
    ASSERT(!atomic_load(&o.bad) && atomic_load(&o.final_gone));
    ASSERT(ib.slot == ia.slot && ib.gen != ia.gen);
    expect_gone(ia); /* After synchronized reuse: always NOENT. */
    ASSERT(epoch_of(ib) == 0);
    OK(os_fault_retire(ib));
    expect_gone(ia);
    os_unmap(&a);
    os_unmap(&b);
}

static void case_race_delivery(void)
{
    open_pipes();
    size_t ps = os_page_size();
    os_map m = pattern_map("m", 2 * ps);
    os_fault_id id = registered(&m);
    ASSERT(epoch_of(id) == 0);
    atomic_store(&pause_remap, true);
    trigger_job j = {&m, ps + 1, OS_E_IO};
    os_thread t;
    OK(os_thread_start(&t, trigger_body, &j));
    (void)await_pause();
    ASSERT(epoch_of(id) == 1);         /* The taint precedes the replacement... */
    ASSERT(is_pattern(&m, 0, 2 * ps)); /* ...which has not happened yet. */
    release_pause(0);
    OK(os_thread_join(&t));
    OK(j.rc);
    ASSERT(epoch_of(id) == 1 && is_pattern(&m, 0, ps) && is_zero(&m, ps, 2 * ps));
    OK(os_fault_retire(id));
    os_unmap(&m);
}

/* The delivering thread is ordered after the registration only by a relaxed
 * flag, which TSan treats as no synchronization: the handler's read of the
 * slot's base and length is ordered after register's writes solely by the
 * service's admission (acquire CAS on the release store of LIVE). */
typedef struct admit_job { const os_map *view; _Atomic bool *ready; os_err rc; } admit_job;
static void admit_body(void *arg)
{
    admit_job *j = arg;
    while (!atomic_load_explicit(j->ready, memory_order_relaxed)) os_yield();
    j->rc = os_fault_test_trigger(j->view, 1);
}
static void case_race_admission(void)
{
    os_map m = pattern_map("m", os_page_size());
    _Atomic bool ready = false;
    admit_job j = {&m, &ready, OS_E_IO};
    os_thread t;
    OK(os_thread_start(&t, admit_body, &j)); /* Started before the registration. */
    os_fault_id id = registered(&m);
    atomic_store_explicit(&ready, true, memory_order_relaxed);
    OK(os_thread_join(&t));
    OK(j.rc);
    ASSERT(epoch_of(id) == 1 && is_zero(&m, 0, m.len));
    OK(os_fault_retire(id));
    os_unmap(&m);
}

/* Astra's departure fixture (A18): a handler departs a LIVE slot, the owner
 * closes it with a zero count (no semaphore) and reuses it before joining the
 * handler's thread. Completion is announced by a relaxed flag, so the only
 * edge from the handler's reads of base and len to register's rewrite of
 * them is the departure on the control word, observed by the close. */
typedef struct depart_job { const os_map *view; _Atomic bool done; os_err rc; } depart_job;
static void depart_body(void *arg)
{
    depart_job *j = arg;
    j->rc = os_fault_test_trigger(j->view, 1);
    atomic_store_explicit(&j->done, true, memory_order_relaxed);
}
static void case_race_departure(void)
{
    size_t ps = os_page_size();
    os_map a = pattern_map("a", ps), b = pattern_map("b", ps);
    os_fault_id ia = registered(&a);
    depart_job j = {.view = &a, .rc = OS_E_IO};
    os_thread t;
    OK(os_thread_start(&t, depart_body, &j));
    while (!atomic_load_explicit(&j.done, memory_order_relaxed)) os_yield();
    OK(os_fault_retire(ia));         /* Zero count at close. */
    os_fault_id ib = registered(&b); /* Same slot, new base and len. */
    OK(os_thread_join(&t));
    OK(j.rc);
    ASSERT(ib.slot == ia.slot && epoch_of(ib) == 0 && is_zero(&a, 0, ps));
    ASSERT(atomic_load(&n_wait_entry) == 0 && atomic_load(&n_post) == 0);
    OK(os_fault_retire(ib));
    os_unmap(&a);
    os_unmap(&b);
}

typedef struct counts { unsigned entry, done, post; } counts;
static counts drain_counts(void)
{
    counts c = {atomic_load(&n_wait_entry), atomic_load(&n_wait_done), atomic_load(&n_post)};
    return c;
}
static void hold_handler(os_thread *t, trigger_job *j, const os_map *view, unsigned ticket)
{
    *j = (trigger_job){view, 1, OS_E_IO};
    OK(os_thread_start(t, trigger_body, j));
    ASSERT(await_pause() == ticket);
}
static void start_retire(os_thread *t, retire_job *r, os_fault_id id)
{
    r->id = id;
    r->rc = OS_E_IO;
    atomic_store(&r->done, false);
    OK(os_thread_start(t, retire_body, r));
}
static void spin_until_waiting(unsigned entries, const retire_job *r)
{
    while (atomic_load(&n_wait_entry) < entries && !atomic_load(&r->done)) os_yield();
    ASSERT(!atomic_load(&r->done));
}

static void case_race_retire(void)
{
    open_pipes();
    size_t ps = os_page_size();
    os_map a = pattern_map("a", ps), b = pattern_map("b", ps);
    os_thread t1, t2, r1, r2;
    trigger_job j1, j2;
    retire_job x1, x2;
    atomic_store(&pause_remap, true);

    /* 1. The final post lands after retire entered its wait; a second
     * retiree with no admitted handler waits behind it and consumes nothing. */
    os_fault_id ia = registered(&a), ib = registered(&b);
    counts c0 = drain_counts();
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a, 0);
    ASSERT(epoch_of(ia) == 1);
    atomic_store(&wait_eintr, true); /* The first wait is interrupted once. */
    start_retire(&r1, &x1, ia);
    spin_until_gone(ia); /* Invalidated while the drain is pending. */
    spin_until_waiting(c0.entry + 2, &x1);
    start_retire(&r2, &x2, ib);
    ASSERT(epoch_of(ib) == 0); /* r2 cannot close B before r1 frees A. */
    ASSERT(atomic_load(&n_post) == c0.post && !atomic_load(&x1.done));
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(j1.rc);
    OK(os_thread_join(&r1));
    OK(os_thread_join(&r2));
    OK(x1.rc);
    OK(x2.rc);
    counts c1 = drain_counts();
    ASSERT(c1.entry == c0.entry + 2 && c1.done == c0.done + 1 && c1.post == c0.post + 1);
    ASSERT(!atomic_load(&wait_eintr));
    expect_gone(ib);

    /* 2. The final post lands before retire calls the real wait: the
     * semaphore keeps it. */
    ia = registered(&a);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a, 0);
    atomic_store(&gate_wait, true);
    start_retire(&r1, &x1, ia);
    char c;
    get_bytes(entered_pipe[0], &c, 1);
    atomic_store(&gate_wait, false);
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(j1.rc);
    ASSERT(atomic_load(&n_post) == c1.post + 1 && !atomic_load(&x1.done));
    put_bytes(wait_go_pipe[1], &c, 1);
    OK(os_thread_join(&r1));
    OK(x1.rc);
    counts c2 = drain_counts();
    ASSERT(c2.entry == c1.entry + 1 && c2.done == c1.done + 1 && c2.post == c1.post + 1);

    /* 3. Both slots hold a handler. B's departure while B is still live
     * posts nothing, so retiree 1 cannot take it; then retiree 2 finds B
     * drained and consumes nothing. */
    ia = registered(&a);
    ib = registered(&b);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a, 0);
    hold_handler(&t2, &j2, &b, 1);
    start_retire(&r1, &x1, ia);
    spin_until_waiting(c2.entry + 1, &x1);
    start_retire(&r2, &x2, ib);
    release_pause(1);
    OK(os_thread_join(&t2));
    OK(j2.rc);
    ASSERT(atomic_load(&n_post) == c2.post && !atomic_load(&x1.done));
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(os_thread_join(&r1));
    OK(os_thread_join(&r2));
    OK(x1.rc);
    OK(x2.rc);
    counts c3 = drain_counts();
    ASSERT(c3.entry == c2.entry + 1 && c3.done == c2.done + 1 && c3.post == c2.post + 1);

    /* 4. Both retirees drain a positive count: one consumed post each. */
    ia = registered(&a);
    ib = registered(&b);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a, 0);
    hold_handler(&t2, &j2, &b, 1);
    start_retire(&r1, &x1, ia);
    spin_until_waiting(c3.entry + 1, &x1);
    start_retire(&r2, &x2, ib);
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(os_thread_join(&r1));
    OK(x1.rc);
    spin_until_waiting(c3.entry + 2, &x2);
    release_pause(1);
    OK(os_thread_join(&t2));
    OK(os_thread_join(&r2));
    OK(x2.rc);
    counts c4 = drain_counts();
    ASSERT(c4.entry == c3.entry + 2 && c4.done == c3.done + 2 && c4.post == c3.post + 2);

    /* Both mappings register and fault independently afterwards. */
    atomic_store(&pause_remap, false);
    ia = registered(&a);
    ib = registered(&b);
    OK(os_fault_test_trigger(&a, 0));
    ASSERT(epoch_of(ia) == 1 && epoch_of(ib) == 0);
    OK(os_fault_test_trigger(&b, 0));
    ASSERT(epoch_of(ia) == 1 && epoch_of(ib) == 1);
    OK(os_fault_retire(ia));
    OK(os_fault_retire(ib));
    ASSERT(drain_counts().post == c4.post); /* No handler was admitted at either close. */
    os_unmap(&a);
    os_unmap(&b);
}

/* (M) v1 tests/os_test.c case_fault ran four concurrent installers. */
enum { READERS = 4 };
typedef struct reader {
    os_fault_id id;
    barrier *start;
    _Atomic bool *tainted, *stop;
    bool monotonic, saw_taint_after_sync;
} reader;
static void reader_body(void *arg)
{
    reader *r = arg;
    uint64_t last = 0;
    r->monotonic = true;
    barrier_wait(r->start);
    for (;;) {
        bool synced = atomic_load(r->tainted); /* acquire: the trigger was joined */
        uint64_t e = UNTOUCHED;
        if (os_fault_poll(r->id, &e) != OS_OK || e < last) r->monotonic = false;
        last = e;
        if (synced) {
            r->saw_taint_after_sync = e == 1;
            break;
        }
        if (atomic_load(r->stop)) break;
    }
}
static void case_readers(void)
{
    size_t ps = os_page_size();
    os_map m = pattern_map("m", 2 * ps);
    os_fault_id id = registered(&m);
    barrier start;
    barrier_init(&start, READERS + 1);
    _Atomic bool tainted = false, stop = false;
    reader rs[READERS];
    os_thread ts[READERS];
    for (int i = 0; i < READERS; i++) {
        rs[i] = (reader){.id = id, .start = &start, .tainted = &tainted, .stop = &stop};
        OK(os_thread_start(&ts[i], reader_body, &rs[i]));
    }
    barrier_wait(&start);
    trigger_job j = {&m, ps, OS_E_IO};
    os_thread t;
    OK(os_thread_start(&t, trigger_body, &j));
    OK(os_thread_join(&t));
    OK(j.rc);
    atomic_store(&tainted, true);
    for (int i = 0; i < READERS; i++) {
        OK(os_thread_join(&ts[i]));
        ASSERT(rs[i].monotonic && rs[i].saw_taint_after_sync);
    }
    os_fault_id saved = id;
    OK(os_fault_retire(id));
    EXPECT(os_fault_retire(saved), OS_E_NOENT); /* Retired exactly once. */
    expect_gone(saved);
    os_unmap(&m);
}

typedef struct installer {
    barrier *start;
    os_map view;
    os_fault_id id;
    os_err reg, trig;
    uint64_t epoch;
} installer;
static void installer_body(void *arg)
{
    installer *c = arg;
    barrier_wait(c->start);
    c->id = (os_fault_id){7, 7};
    c->reg = os_fault_register(&c->view, &c->id);
    if (c->reg != OS_OK) return;
    c->trig = os_fault_test_trigger(&c->view, 1);
    c->epoch = UNTOUCHED;
    (void)os_fault_poll(c->id, &c->epoch);
}
static void case_install_race(void)
{
    /* (P) Capacity plus one disjoint request: exactly one must lose. */
    enum { RACERS = OS_FAULT_SLOTS + 1 };
    size_t ps = os_page_size();
    os_map v = pattern_map("race", RACERS * ps);
    static installer cs[RACERS];
    static os_thread ts[RACERS];
    barrier start;
    barrier_init(&start, RACERS);
    for (size_t i = 0; i < RACERS; i++) {
        cs[i] = (installer){.start = &start, .view = sub(&v, i * ps, ps)};
        OK(os_thread_start(&ts[i], installer_body, &cs[i]));
    }
    /* Join every racer before retiring any slot (v1 edit-nafu). */
    for (size_t i = 0; i < RACERS; i++) OK(os_thread_join(&ts[i]));
    ASSERT(atomic_load(&n_install) == 1);
    size_t wins = 0, loser = RACERS;
    uint64_t top = 0;
    static bool used[OS_FAULT_SLOTS];
    for (size_t i = 0; i < RACERS; i++) {
        if (cs[i].reg == OS_E_NOSPC) {
            ASSERT(loser == RACERS && absent(cs[i].id));
            loser = i;
            continue;
        }
        OK(cs[i].reg);
        OK(cs[i].trig);
        ASSERT(cs[i].epoch == 1 && is_zero(&v, i * ps, (i + 1) * ps));
        ASSERT(!used[cs[i].id.slot]);
        used[cs[i].id.slot] = true;
        for (size_t k = 0; k < i; k++) ASSERT(cs[k].reg != OS_OK || cs[k].id.gen != cs[i].id.gen);
        if (cs[i].id.gen > top) top = cs[i].id.gen;
        wins++;
    }
    ASSERT(wins == OS_FAULT_SLOTS && loser < RACERS);
    size_t first = loser == 0 ? 1 : 0;
    OK(os_fault_retire(cs[first].id));
    os_fault_id again = registered(&cs[loser].view);
    ASSERT(again.slot == cs[first].id.slot && again.gen > top);
    cs[first].id = again;
    for (size_t i = 0; i < RACERS; i++) if (i != loser) OK(os_fault_retire(cs[i].id));
    os_unmap(&v);
}

/* ---- driver --------------------------------------------------------------- */
typedef enum outcome { EXITS_ZERO, DIES_BY_SIGBUS, DIES_BY_SIGABRT } outcome;
static const struct {
    const char *name;
    void (*run)(void);
    outcome want;
    const char *stderr_has; /* The child's stderr file must contain this. */
} cases[] = {
    {"stale", case_stale, EXITS_ZERO, NULL},
    {"args", case_args, EXITS_ZERO, NULL},
    {"capacity", case_capacity, EXITS_ZERO, NULL},
    {"recover", case_recover, EXITS_ZERO, NULL},
    {"truncate", case_truncate, EXITS_ZERO, NULL},
    {"chain_default", case_chain_default, DIES_BY_SIGBUS, NULL},
    {"chain_ignore", case_chain_ignore, DIES_BY_SIGBUS, NULL},
    {"chain_simple", case_chain_simple, EXITS_ZERO, NULL},
    {"chain_siginfo", case_chain_siginfo, EXITS_ZERO, NULL},
    {"assert", case_assert, DIES_BY_SIGABRT, ": ASSERT failed: prior_calls == 1\n"},
    {"exhaustion", case_exhaustion, EXITS_ZERO, NULL},
    {"err_pagesize", case_err_pagesize, EXITS_ZERO, NULL},
    {"err_sem", case_err_sem, EXITS_ZERO, NULL},
    {"err_query", case_err_query, EXITS_ZERO, NULL},
    {"err_install", case_err_install, EXITS_ZERO, NULL},
    {"err_remap", case_err_remap, EXITS_ZERO, NULL},
    {"err_remap_default", case_err_remap_default, DIES_BY_SIGBUS, NULL},
    {"err_send", case_err_send, EXITS_ZERO, NULL},
    {"race_reuse", case_race_reuse, EXITS_ZERO, NULL},
    {"race_delivery", case_race_delivery, EXITS_ZERO, NULL},
    {"race_admission", case_race_admission, EXITS_ZERO, NULL},
    {"race_departure", case_race_departure, EXITS_ZERO, NULL},
    {"race_retire", case_race_retire, EXITS_ZERO, NULL},
    {"readers", case_readers, EXITS_ZERO, NULL},
    {"install_race", case_install_race, EXITS_ZERO, NULL},
};
enum { CASES = sizeof cases / sizeof cases[0] };

int main(int argc, char **argv)
{
    /* Handler-side switches and counters must be lock-free (AS-safe). */
    ASSERT(atomic_is_lock_free(&n_post) && atomic_is_lock_free(&fail_remap) &&
          atomic_is_lock_free(&pause_next));
    if (argc == 3 && strncmp(argv[1], "--case=", sizeof "--case=" - 1) == 0) {
        alarm(CASE_GUARD_SECONDS);
        work_root = argv[2];
        _Atomic unsigned *counters[] = {&n_pagesize, &n_sem_init, &n_sem_destroy, &n_query, &n_install,
                                        &n_remap, &n_send, &n_wait_entry, &n_wait_done, &n_post};
        for (size_t i = 0; i < sizeof counters / sizeof counters[0]; i++) atomic_store(counters[i], 0);
        for (size_t i = 0; i < CASES; i++) {
            if (strcmp(cases[i].name, argv[1] + sizeof "--case=" - 1) == 0) {
                cases[i].run();
                return EXIT_SUCCESS;
            }
        }
        return EXIT_FAILURE;
    }
    ASSERT(argc == 1);
    char exe[OS_PATH_MAX], cwd[OS_PATH_MAX];
    OK(os_proc_self_path(exe, sizeof exe));
    OK(os_cwd(cwd, sizeof cwd));
    for (size_t i = 0; i < CASES; i++) {
        char dir[OS_PATH_MAX], flag[64];
        int n = snprintf(dir, sizeof dir, "%s/build/os-fault-%s-XXXXXX", cwd, cases[i].name);
        ASSERT(n >= 0 && (size_t)n < sizeof dir);
        ASSERT(mkdtemp(dir) != NULL);
        n = snprintf(flag, sizeof flag, "--case=%s", cases[i].name);
        ASSERT(n >= 0 && (size_t)n < sizeof flag);
        printf("os_fault_test: %s\n", cases[i].name);
        fflush(stdout);
        const char *child[] = {exe, flag, dir, NULL};
        os_proc p;
        os_proc_status s = {OS_PROC_EXITED, -1};
        OK(os_proc_spawn(&p, child));
        OK(os_proc_wait(&p, -1, &s)); /* The child's alarm bounds the wait. */
        bool ok = false;
        switch (cases[i].want) {
        case EXITS_ZERO: ok = s.how == OS_PROC_EXITED && s.code == 0; break;
        case DIES_BY_SIGBUS: ok = s.how == OS_PROC_ABNORMAL && s.code == SIGBUS; break;
        case DIES_BY_SIGABRT: ok = s.how == OS_PROC_ABNORMAL && s.code == SIGABRT; break;
        }
        if (!ok) {
            fprintf(stderr, "os_fault_test: %s child %s %d\n", cases[i].name,
                    s.how == OS_PROC_EXITED ? "exited" : "died by signal", s.code);
            return EXIT_FAILURE;
        }
        if (cases[i].stderr_has) {
            char path[OS_PATH_MAX], text[512] = {0};
            n = snprintf(path, sizeof path, "%s/stderr", dir);
            ASSERT(n >= 0 && (size_t)n < sizeof path);
            os_file f = OS_FILE_INIT;
            size_t got = 0;
            OK(os_file_open(&f, path, OS_OPEN_READ));
            OK(os_file_pread(&f, text, sizeof text - 1, 0, &got));
            os_file_close(&f);
            fprintf(stderr, "os_fault_test: %s child wrote: %s", cases[i].name, text);
            ASSERT(strncmp(text, "tests/os_fault_test.c:", sizeof "tests/os_fault_test.c:" - 1) == 0);
            ASSERT(strstr(text, cases[i].stderr_has) != NULL);
            OK(os_unlink(path));
        }
        OK(os_rmdir(dir));
    }
    puts("os_fault_test: ok");
    return EXIT_SUCCESS;
}
