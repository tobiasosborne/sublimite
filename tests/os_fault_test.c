/* Mapped-file fault service conformance: the acceptance tables of
 * docs/decisions/wave1-os-fault-lease.md, under A15 and A18 (v2-0fv.16).
 *
 * Every fault is real: the file shrinks under the view and a read faults.
 * Two cases also send SIGBUS themselves, for what a truncation cannot
 * produce: a device I/O error (BUS_OBJERR) and a sigqueue (SI_QUEUE).
 *
 * The service is one process-global installation, so every case runs in a
 * fresh self-exec child. The suite links with -Wl,--wrap for the recovery
 * remap and the drain semaphore (tests/lanes.mk LDFLAGS_os_fault_test); each
 * wrapper passes through until a case arms it. */
#include "os/os.h"

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

extern char **environ;


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
static size_t page; /* os_page_size(), read once so signal handlers may use it. */

/* The OS_TEST_HOOKS variant of os_posix.c defines it (A15); the suite links
 * that object ahead of libcore.a (tests/lanes.mk). */
void os_fault_test_next_gen(uint64_t next);

/* ---- native fault injection (-Wl,--wrap) -------------------------------- */
/* Switches and counters the signal handler touches are lock-free atomics
 * (checked in main). Handler-side wrappers call only write, read, _exit and
 * the real entry point, all async-signal-safe. */
void *__real_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int __real_sem_wait(sem_t *s);
int __real_sem_post(sem_t *s);

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int __wrap_sem_wait(sem_t *s);
int __wrap_sem_post(sem_t *s);

static _Atomic bool fail_remap, pause_remap, gate_wait, wait_eintr;
static _Atomic unsigned n_remap, n_wait_entry, n_wait_done, n_post;

/* (P) The retire race holds at most two handlers at once, one per retiree. */
enum { PAUSE_TICKETS = 2 };
static _Atomic unsigned pause_next;
static int paused_pipe[2] = {-1, -1}, go_pipe[PAUSE_TICKETS][2], entered_pipe[2] = {-1, -1},
           wait_go_pipe[2] = {-1, -1};

/* Distinct exit codes name a broken fixture rather than a failed oracle. */
enum { FIXTURE_IO = 96, FIXTURE_TICKET = 95 };

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
/* A view of len pattern bytes whose file then shrinks to size: a read at or
 * past the first page beyond size faults. */
static os_map shrunk_map(const char *name, size_t len, size_t size)
{
    os_file f = pattern_file(name, len);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, len));
    if (size < len) OK(os_file_set_size(&f, size));
    os_file_close(&f);
    return m;
}
static os_map pattern_map(const char *name, size_t len) { return shrunk_map(name, len, len); }
static os_map sub(const os_map *m, size_t off, size_t len)
{
    ASSERT(off < m->len && len <= m->len - off);
    os_map s = {(const unsigned char *)m->addr + off, len};
    return s;
}
static unsigned char touch(const os_map *m, size_t off)
{
    const volatile unsigned char *v = m->addr;
    return v[off];
}
static bool is_pattern(const os_map *m, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++) if (touch(m, i) != pattern_at(i)) return false;
    return true;
}
static bool is_zero(const os_map *m, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++) if (touch(m, i) != 0) return false;
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

/* What a truncation cannot produce: SIGBUS with another code, to this
 * thread, at a chosen address. */
static void send_bus(const void *addr, int code)
{
    siginfo_t si;
    memset(&si, 0, sizeof si);
    si.si_signo = SIGBUS;
    si.si_code = code;
    si.si_addr = (void *)(uintptr_t)addr;
    ASSERT(syscall(SYS_rt_tgsigqueueinfo, (long)getpid(), (long)gettid(), (long)SIGBUS, &si) == 0);
}

static void set_prior(void (*handler)(int), void (*info)(int, siginfo_t *, void *))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    if (info) sa.sa_sigaction = info;
    else sa.sa_handler = handler;
    sa.sa_flags = info ? SA_SIGINFO : 0;
    ASSERT(sigemptyset(&sa.sa_mask) == 0);
    ASSERT(sigaction(SIGBUS, &sa, NULL) == 0);
}

/* Prior-handler probes. Written from a signal handler, read after it. Each
 * repairs the faulting page itself (bypassing the wrapped remap) so that a
 * real fault does not repeat when it returns. */
static volatile sig_atomic_t prior_calls, prior_sig, prior_code, prior_ctx_ok;
static void *volatile prior_addr, *volatile stray_page;
static void repair(void *at)
{
    void *from = (void *)((uintptr_t)at & ~(uintptr_t)(page - 1u));
    if (__real_mmap(from, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        _exit(FIXTURE_IO);
}
static void prior_simple(int sig)
{
    prior_calls++;
    prior_sig = sig;
    repair(stray_page);
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
    if (si->si_code == BUS_ADRERR) repair(si->si_addr);
    errno = EIO;
}

/* ---- single-thread cases -------------------------------------------------- */
static void case_stale(void)
{
    os_map a = shrunk_map("a", 2 * page, page), b = shrunk_map("b", 2 * page, page);
    os_fault_id ia = registered(&a);
    ASSERT(epoch_of(ia) == 0);
    ASSERT(touch(&a, page) == 0);
    ASSERT(epoch_of(ia) == 1);

    os_fault_id saved = ia; /* A value copy outlives the registration. */
    os_fault_retire(ia);
    expect_gone(saved); /* Before reuse: the slot is free. */
    os_fault_id ib = registered(&b);
    ASSERT(ib.slot == saved.slot && ib.gen != saved.gen);
    ASSERT(epoch_of(ib) == 0);
    expect_gone(saved); /* After reuse: never the new tenant's epoch. */

    ASSERT(touch(&b, page + 1) == 0);
    ASSERT(epoch_of(ib) == 1);
    expect_gone(saved);
    os_fault_retire(ib);
    expect_gone(ib);
    os_unmap(&a);
    os_unmap(&b);
}

static void case_extents(void)
{
    /* Page-rounded extents side by side register and repair independently. */
    os_map v = shrunk_map("v", 4 * page, 2 * page);
    os_map r1v = sub(&v, 0, page + 1), r2v = sub(&v, 2 * page, page), r3v = sub(&v, 3 * page, page);
    os_fault_id r1 = registered(&r1v), r2 = registered(&r2v), r3 = registered(&r3v);
    ASSERT(touch(&v, 2 * page) == 0);
    ASSERT(epoch_of(r1) == 0 && epoch_of(r2) == 1 && epoch_of(r3) == 0);
    ASSERT(is_pattern(&v, 0, 2 * page) && is_zero(&v, 2 * page, 3 * page));
    ASSERT(touch(&v, 3 * page + 1) == 0);
    ASSERT(epoch_of(r1) == 0 && epoch_of(r2) == 1 && epoch_of(r3) == 1);
    os_fault_retire(r1);
    os_fault_retire(r2);
    os_fault_retire(r3);
    os_unmap(&v);

    /* Distinct views of one file register and taint independently. */
    os_file f = pattern_file("same", 2 * page);
    os_map m1 = {0}, m2 = {0};
    OK(os_map_file(&m1, &f, 0, 2 * page));
    OK(os_map_file(&m2, &f, 0, 2 * page));
    OK(os_file_set_size(&f, 0));
    os_file_close(&f);
    os_fault_id i1 = registered(&m1), i2 = registered(&m2);
    ASSERT(touch(&m1, 0) == 0);
    ASSERT(epoch_of(i1) == 1 && epoch_of(i2) == 0 && is_zero(&m1, 0, 2 * page));
    os_fault_retire(i1);
    os_fault_retire(i2);
    os_unmap(&m1);
    os_unmap(&m2);
}

static void case_capacity(void)
{
    os_map v = pattern_map("cap", (OS_FAULT_SLOTS + 1) * page);
    static os_fault_id ids[OS_FAULT_SLOTS];
    for (uint32_t i = 0; i < OS_FAULT_SLOTS; i++) {
        os_map one = sub(&v, i * page, page);
        ids[i] = registered(&one);
        ASSERT(ids[i].slot == i); /* The lowest free index. */
        ASSERT(i == 0 || ids[i].gen > ids[i - 1].gen);
    }
    os_map extra = sub(&v, OS_FAULT_SLOTS * page, page);
    os_fault_id x = {7, 7};
    EXPECT(os_fault_register(&extra, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    /* (P) An interior index: neither the lowest nor the last issued. */
    const uint32_t k = OS_FAULT_SLOTS / 2;
    os_fault_retire(ids[k]);
    x = registered(&extra);
    ASSERT(x.slot == k && x.gen > ids[OS_FAULT_SLOTS - 1].gen);
    expect_gone(ids[k]);
    ids[k] = x;
    for (uint32_t i = 0; i < OS_FAULT_SLOTS; i++) os_fault_retire(ids[i]);
    os_unmap(&v);
}

static void case_recover(void)
{
    /* (P) One page per role: prefix, earlier and later suffix start, then a
     * partial final page. The file shrinks twice: later, then earlier. */
    size_t len = 3 * page + page / 2;
    os_file f = pattern_file("recover", len);
    os_map m = {0};
    OK(os_map_file(&m, &f, 0, len));
    os_fault_id id = registered(&m);
    OK(os_file_set_size(&f, 2 * page));
    ASSERT(touch(&m, 2 * page + 1) == 0);
    ASSERT(epoch_of(id) == 1);
    ASSERT(is_pattern(&m, 0, 2 * page) && is_zero(&m, 2 * page, len));
    OK(os_file_set_size(&f, page));
    ASSERT(touch(&m, page + page / 2) == 0);
    ASSERT(epoch_of(id) == 2);
    ASSERT(is_pattern(&m, 0, page) && is_zero(&m, page, len));
    ASSERT(epoch_of(id) == 2); /* The recovered suffix does not fault again. */
    os_fault_retire(id);
    os_unmap(&m);
    OK(os_map_file(&m, &f, 0, page)); /* The surviving file prefix is intact. */
    ASSERT(is_pattern(&m, 0, page));
    os_unmap(&m);
    os_file_close(&f);
}

/* ---- chaining: each child sets the prior disposition, then installs ------ */
static void install_service(void)
{
    static os_map reg;
    reg = pattern_map("reg", page);
    (void)registered(&reg);
}

/* A native fault in a view no registration covers. Surviving it fails. */
static void stray_native_fault(void)
{
    os_map stray = shrunk_map("stray", 2 * page, page);
    (void)touch(&stray, page);
    fprintf(stderr, "os_fault_test.c: survived an unrepaired fault\n");
    exit(EXIT_FAILURE);
}

static void case_chain_default(void)
{
    set_prior(SIG_DFL, NULL);
    install_service();
    stray_native_fault();
}

/* A prior SIG_IGN is SIG_DFL: an unrepaired fault never returns to the
 * faulting instruction with nothing changed. */
static void case_chain_ignore(void)
{
    set_prior(SIG_IGN, NULL);
    install_service();
    stray_native_fault();
}

static void case_chain_simple(void)
{
    set_prior(prior_simple, NULL);
    install_service();
    os_map stray = shrunk_map("stray", 2 * page, page), mine = pattern_map("mine", 2 * page);
    stray_page = (unsigned char *)(uintptr_t)stray.addr + page;
    errno = EDOM;
    ASSERT(touch(&stray, page) == 0);
    ASSERT(errno == EDOM);
    ASSERT(prior_calls == 1 && prior_sig == SIGBUS);

    /* A sigqueue at a registered address goes to the prior handler
     * unrepaired; a device error (BUS_OBJERR) is repaired and does not. */
    os_fault_id id = registered(&mine);
    send_bus((const unsigned char *)mine.addr + 1, SI_QUEUE);
    ASSERT(prior_calls == 2 && epoch_of(id) == 0 && is_pattern(&mine, 0, 2 * page));
    send_bus((const unsigned char *)mine.addr + page, BUS_OBJERR);
    ASSERT(prior_calls == 2 && epoch_of(id) == 1);
    ASSERT(is_pattern(&mine, 0, page) && is_zero(&mine, page, 2 * page));
    os_fault_retire(id);
}

static void case_chain_siginfo(void)
{
    set_prior(NULL, prior_info);
    install_service();
    os_map stray = shrunk_map("stray", 2 * page, page);
    errno = EDOM;
    ASSERT(touch(&stray, page + 1) == 0);
    ASSERT(errno == EDOM);
    ASSERT(prior_calls == 1 && prior_sig == SIGBUS && prior_code == BUS_ADRERR);
    ASSERT(prior_addr == (const unsigned char *)stray.addr + page + 1 && prior_ctx_ok);
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
    os_map v = shrunk_map("gens", 4 * page, 0);
    os_map p0 = sub(&v, 0, page), p1 = sub(&v, page, page), p2 = sub(&v, 2 * page, page),
           p3 = sub(&v, 3 * page, page);
    os_fault_id a = registered(&p0);
    os_fault_test_next_gen(UINT64_MAX - 1);
    os_fault_id b = registered(&p1), c = registered(&p2), x = {7, 7};
    ASSERT(b.gen == UINT64_MAX - 1 && c.gen == UINT64_MAX);
    EXPECT(os_fault_register(&p3, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    os_fault_retire(b);
    x.slot = 7;
    EXPECT(os_fault_register(&p3, &x), OS_E_NOSPC);
    ASSERT(absent(x));
    expect_gone(b);
    ASSERT(touch(&p0, 1) == 0 && touch(&p2, 1) == 0);
    ASSERT(epoch_of(a) == 1 && epoch_of(c) == 1);
    os_fault_retire(a);
    os_fault_retire(c);
    os_unmap(&v);
}

/* The recovery remap fails (ENOMEM: the map-count limit). */
static void case_err_remap(void)
{
    set_prior(NULL, prior_info);
    os_map m = shrunk_map("m", 2 * page, page);
    os_fault_id id = registered(&m);
    atomic_store(&fail_remap, true);
    ASSERT(touch(&m, page + 1) == 0); /* The prior handler repaired it. */
    ASSERT(atomic_load(&n_remap) == 1);
    ASSERT(prior_calls == 1 && prior_addr == (const unsigned char *)m.addr + page + 1);
    ASSERT(epoch_of(id) == 1 && is_pattern(&m, 0, page)); /* Tainted all the same. */
    os_fault_retire(id);
    os_unmap(&m);
}
static void case_err_remap_default(void)
{
    set_prior(SIG_DFL, NULL);
    os_map m = shrunk_map("m", 2 * page, page);
    (void)registered(&m);
    atomic_store(&fail_remap, true);
    (void)touch(&m, page + 1); /* Unrepaired, no prior: death by SIGBUS. */
    fprintf(stderr, "os_fault_test.c: survived a failed repair\n");
    exit(EXIT_FAILURE);
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

/* A thread that reads one byte of a view, faulting if it lies past the end. */
typedef struct fault_job { const os_map *view; size_t off; unsigned got; } fault_job;
static void fault_body(void *arg)
{
    fault_job *j = arg;
    j->got = touch(j->view, j->off);
}
typedef struct retire_job { os_fault_id id; _Atomic bool done; } retire_job;
static void retire_body(void *arg)
{
    retire_job *j = arg;
    os_fault_retire(j->id);
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
    os_map a = shrunk_map("a", 2 * page, page), b = pattern_map("b", page);
    os_fault_id ia = registered(&a);
    ASSERT(touch(&a, page) == 0); /* A carries 1, B will read pristine 0. */
    barrier start;
    barrier_init(&start, 2);
    observer o = {.id = ia, .want = 1, .start = &start};
    os_thread t;
    OK(os_thread_start(&t, observe_body, &o));
    barrier_wait(&start);
    while (atomic_load(&o.hits) == 0 && !atomic_load(&o.bad)) os_yield(); /* polled while live */
    os_fault_retire(ia);
    expect_gone(ia); /* After completed invalidation, while the slot is free. */
    os_fault_id ib = registered(&b);
    atomic_store(&o.reused, true); /* The observer's next poll follows the reuse. */
    OK(os_thread_join(&t));
    ASSERT(!atomic_load(&o.bad) && atomic_load(&o.final_gone));
    ASSERT(ib.slot == ia.slot && ib.gen != ia.gen);
    expect_gone(ia); /* After synchronized reuse: always NOENT. */
    ASSERT(epoch_of(ib) == 0);
    os_fault_retire(ib);
    expect_gone(ia);
    os_unmap(&a);
    os_unmap(&b);
}

static void case_race_delivery(void)
{
    open_pipes();
    os_map m = shrunk_map("m", 2 * page, page);
    os_fault_id id = registered(&m);
    ASSERT(epoch_of(id) == 0);
    atomic_store(&pause_remap, true);
    fault_job j = {&m, page + 1, 1};
    os_thread t;
    OK(os_thread_start(&t, fault_body, &j));
    (void)await_pause();
    /* The handler waits inside the remap call, before the replacement: the
     * taint is already visible. */
    ASSERT(epoch_of(id) == 1 && is_pattern(&m, 0, page));
    release_pause(0);
    OK(os_thread_join(&t));
    ASSERT(j.got == 0);
    ASSERT(epoch_of(id) == 1 && is_pattern(&m, 0, page) && is_zero(&m, page, 2 * page));
    os_fault_retire(id);
    os_unmap(&m);
}

/* The faulting thread is ordered after the registration only by a relaxed
 * flag, which TSan treats as no synchronization: the handler's read of the
 * slot's base and length is ordered after register's writes solely by the
 * service's admission CAS on the store of LIVE. */
typedef struct admit_job { const os_map *view; _Atomic bool *ready; unsigned got; } admit_job;
static void admit_body(void *arg)
{
    admit_job *j = arg;
    while (!atomic_load_explicit(j->ready, memory_order_relaxed)) os_yield();
    j->got = touch(j->view, 1);
}
static void case_race_admission(void)
{
    os_map m = shrunk_map("m", page, 0);
    _Atomic bool ready = false;
    admit_job j = {&m, &ready, 1};
    os_thread t;
    OK(os_thread_start(&t, admit_body, &j)); /* Started before the registration. */
    os_fault_id id = registered(&m);
    atomic_store_explicit(&ready, true, memory_order_relaxed);
    OK(os_thread_join(&t));
    ASSERT(j.got == 0 && epoch_of(id) == 1 && is_zero(&m, 0, m.len));
    os_fault_retire(id);
    os_unmap(&m);
}

/* Astra's departure fixture (A18): a handler departs a LIVE slot, the owner
 * closes it with a zero count (no semaphore) and reuses it before joining the
 * handler's thread. Completion is announced by a relaxed flag, so the only
 * edge from the handler's reads of base and len to register's rewrite of
 * them is the departure on the control word, observed by the close. */
typedef struct depart_job { const os_map *view; _Atomic bool done; unsigned got; } depart_job;
static void depart_body(void *arg)
{
    depart_job *j = arg;
    j->got = touch(j->view, 1);
    atomic_store_explicit(&j->done, true, memory_order_relaxed);
}
static void case_race_departure(void)
{
    os_map a = shrunk_map("a", page, 0), b = pattern_map("b", page);
    os_fault_id ia = registered(&a);
    depart_job j = {.view = &a, .got = 1};
    os_thread t;
    OK(os_thread_start(&t, depart_body, &j));
    while (!atomic_load_explicit(&j.done, memory_order_relaxed)) os_yield();
    os_fault_retire(ia);             /* Zero count at close. */
    os_fault_id ib = registered(&b); /* Same slot, new base and len. */
    OK(os_thread_join(&t));
    ASSERT(j.got == 0);
    ASSERT(ib.slot == ia.slot && epoch_of(ib) == 0 && is_zero(&a, 0, page));
    ASSERT(atomic_load(&n_wait_entry) == 0 && atomic_load(&n_post) == 0);
    os_fault_retire(ib);
    os_unmap(&a);
    os_unmap(&b);
}

typedef struct counts { unsigned entry, done, post; } counts;
static counts drain_counts(void)
{
    counts c = {atomic_load(&n_wait_entry), atomic_load(&n_wait_done), atomic_load(&n_post)};
    return c;
}
/* A fresh view whose second page faults, so each held handler is a real one. */
static os_map faulting(const char *name) { return shrunk_map(name, 2 * page, page); }
static void hold_handler(os_thread *t, fault_job *j, const os_map *view, unsigned ticket)
{
    *j = (fault_job){view, page, 1};
    OK(os_thread_start(t, fault_body, j));
    ASSERT(await_pause() == ticket);
}
static void start_retire(os_thread *t, retire_job *r, os_fault_id id)
{
    r->id = id;
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
    os_thread t1, t2, r1, r2;
    fault_job j1, j2;
    retire_job x1, x2;
    atomic_store(&pause_remap, true);

    /* 1. The final post lands after retire entered its wait; a second
     * retiree with no admitted handler waits behind it and consumes nothing. */
    os_map a1 = faulting("a1"), b1 = pattern_map("b1", page);
    os_fault_id ia = registered(&a1), ib = registered(&b1);
    counts c0 = drain_counts();
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a1, 0);
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
    ASSERT(j1.got == 0);
    OK(os_thread_join(&r1));
    OK(os_thread_join(&r2));
    counts c1 = drain_counts();
    ASSERT(c1.entry == c0.entry + 2 && c1.done == c0.done + 1 && c1.post == c0.post + 1);
    ASSERT(!atomic_load(&wait_eintr));
    expect_gone(ib);

    /* 2. The final post lands before retire calls the real wait: the
     * semaphore keeps it. */
    os_map a2 = faulting("a2");
    ia = registered(&a2);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a2, 0);
    atomic_store(&gate_wait, true);
    start_retire(&r1, &x1, ia);
    char c;
    get_bytes(entered_pipe[0], &c, 1);
    atomic_store(&gate_wait, false);
    release_pause(0);
    OK(os_thread_join(&t1));
    ASSERT(atomic_load(&n_post) == c1.post + 1 && !atomic_load(&x1.done));
    put_bytes(wait_go_pipe[1], &c, 1);
    OK(os_thread_join(&r1));
    counts c2 = drain_counts();
    ASSERT(c2.entry == c1.entry + 1 && c2.done == c1.done + 1 && c2.post == c1.post + 1);

    /* 3. Both slots hold a handler. B's departure while B is still live
     * posts nothing, so retiree 1 cannot take it; then retiree 2 finds B
     * drained and consumes nothing. */
    os_map a3 = faulting("a3"), b3 = faulting("b3");
    ia = registered(&a3);
    ib = registered(&b3);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a3, 0);
    hold_handler(&t2, &j2, &b3, 1);
    start_retire(&r1, &x1, ia);
    spin_until_waiting(c2.entry + 1, &x1);
    start_retire(&r2, &x2, ib);
    release_pause(1);
    OK(os_thread_join(&t2));
    ASSERT(atomic_load(&n_post) == c2.post && !atomic_load(&x1.done));
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(os_thread_join(&r1));
    OK(os_thread_join(&r2));
    counts c3 = drain_counts();
    ASSERT(c3.entry == c2.entry + 1 && c3.done == c2.done + 1 && c3.post == c2.post + 1);

    /* 4. Both retirees drain a positive count: one consumed post each. */
    os_map a4 = faulting("a4"), b4 = faulting("b4");
    ia = registered(&a4);
    ib = registered(&b4);
    atomic_store(&pause_next, 0);
    hold_handler(&t1, &j1, &a4, 0);
    hold_handler(&t2, &j2, &b4, 1);
    start_retire(&r1, &x1, ia);
    spin_until_waiting(c3.entry + 1, &x1);
    start_retire(&r2, &x2, ib);
    release_pause(0);
    OK(os_thread_join(&t1));
    OK(os_thread_join(&r1));
    spin_until_waiting(c3.entry + 2, &x2);
    release_pause(1);
    OK(os_thread_join(&t2));
    OK(os_thread_join(&r2));
    counts c4 = drain_counts();
    ASSERT(c4.entry == c3.entry + 2 && c4.done == c3.done + 2 && c4.post == c3.post + 2);

    /* Two more mappings register and fault independently afterwards. */
    atomic_store(&pause_remap, false);
    os_map a5 = faulting("a5"), b5 = faulting("b5");
    ia = registered(&a5);
    ib = registered(&b5);
    ASSERT(touch(&a5, page) == 0);
    ASSERT(epoch_of(ia) == 1 && epoch_of(ib) == 0);
    ASSERT(touch(&b5, page) == 0);
    ASSERT(epoch_of(ia) == 1 && epoch_of(ib) == 1);
    os_fault_retire(ia);
    os_fault_retire(ib);
    ASSERT(drain_counts().post == c4.post); /* No handler was admitted at either close. */
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
        bool synced = atomic_load(r->tainted); /* The fault's thread was joined. */
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
    os_map m = shrunk_map("m", 2 * page, page);
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
    fault_job j = {&m, page, 1};
    os_thread t;
    OK(os_thread_start(&t, fault_body, &j));
    OK(os_thread_join(&t));
    ASSERT(j.got == 0);
    atomic_store(&tainted, true);
    for (int i = 0; i < READERS; i++) {
        OK(os_thread_join(&ts[i]));
        ASSERT(rs[i].monotonic && rs[i].saw_taint_after_sync);
    }
    os_fault_id saved = id;
    os_fault_retire(id);
    expect_gone(saved);
    os_unmap(&m);
}

typedef struct installer {
    barrier *start;
    os_map view;
    os_fault_id id;
    os_err reg;
    unsigned got;
    uint64_t epoch;
} installer;
static void installer_body(void *arg)
{
    installer *c = arg;
    barrier_wait(c->start);
    c->id = (os_fault_id){7, 7};
    c->reg = os_fault_register(&c->view, &c->id);
    if (c->reg != OS_OK) return;
    c->got = touch(&c->view, 1);
    c->epoch = UNTOUCHED;
    (void)os_fault_poll(c->id, &c->epoch);
}
static void case_install_race(void)
{
    /* A prior handler shows how many installations ran: after two, the
     * second's prior would be the service itself, and an unrepaired fault
     * would recurse instead of reaching prior_info exactly once. */
    set_prior(NULL, prior_info);
    /* (P) Capacity plus one disjoint request: exactly one must lose. */
    enum { RACERS = OS_FAULT_SLOTS + 1 };
    os_map v = shrunk_map("race", RACERS * page, 0);
    static installer cs[RACERS];
    static os_thread ts[RACERS];
    barrier start;
    barrier_init(&start, RACERS);
    for (size_t i = 0; i < RACERS; i++) {
        cs[i] = (installer){.start = &start, .view = sub(&v, i * page, page), .got = 1};
        OK(os_thread_start(&ts[i], installer_body, &cs[i]));
    }
    /* Join every racer before retiring any slot (v1 edit-nafu). */
    for (size_t i = 0; i < RACERS; i++) OK(os_thread_join(&ts[i]));
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
        ASSERT(cs[i].got == 0 && cs[i].epoch == 1 && is_zero(&v, i * page, (i + 1) * page));
        ASSERT(!used[cs[i].id.slot]);
        used[cs[i].id.slot] = true;
        for (size_t k = 0; k < i; k++) ASSERT(cs[k].reg != OS_OK || cs[k].id.gen != cs[i].id.gen);
        if (cs[i].id.gen > top) top = cs[i].id.gen;
        wins++;
    }
    ASSERT(wins == OS_FAULT_SLOTS && loser < RACERS && prior_calls == 0);
    ASSERT(touch(&cs[loser].view, 1) == 0); /* Unregistered: the prior repairs it. */
    ASSERT(prior_calls == 1 && prior_code == BUS_ADRERR);
    size_t first = loser == 0 ? 1 : 0;
    os_fault_retire(cs[first].id);
    os_fault_id again = registered(&cs[loser].view);
    ASSERT(again.slot == cs[first].id.slot && again.gen > top);
    cs[first].id = again;
    for (size_t i = 0; i < RACERS; i++) if (i != loser) os_fault_retire(cs[i].id);
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
    {"extents", case_extents, EXITS_ZERO, NULL},
    {"capacity", case_capacity, EXITS_ZERO, NULL},
    {"recover", case_recover, EXITS_ZERO, NULL},
    {"chain_default", case_chain_default, DIES_BY_SIGBUS, NULL},
    {"chain_ignore", case_chain_ignore, DIES_BY_SIGBUS, NULL},
    {"chain_simple", case_chain_simple, EXITS_ZERO, NULL},
    {"chain_siginfo", case_chain_siginfo, EXITS_ZERO, NULL},
    {"assert", case_assert, DIES_BY_SIGABRT, ": ASSERT failed: prior_calls == 1\n"},
    {"exhaustion", case_exhaustion, EXITS_ZERO, NULL},
    {"err_remap", case_err_remap, EXITS_ZERO, NULL},
    {"err_remap_default", case_err_remap_default, DIES_BY_SIGBUS, NULL},
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
    page = os_page_size();
    if (argc == 3 && strncmp(argv[1], "--case=", sizeof "--case=" - 1) == 0) {
        alarm(CASE_GUARD_SECONDS);
        work_root = argv[2];
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
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof exe);
    ASSERT(exe_len > 0 && (size_t)exe_len < sizeof exe);
    exe[exe_len] = '\0';
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
        /* os spawns no processes since v2-0fv.13, so the harness does: the
         * child inherits only the standard streams, as os_proc_spawn did. */
        const char *child[] = {exe, flag, dir, NULL};
        posix_spawn_file_actions_t fa;
        pid_t pid = -1;
        ASSERT(posix_spawn_file_actions_init(&fa) == 0);
        ASSERT(posix_spawn_file_actions_addclosefrom_np(&fa, 3) == 0);
        int rc = posix_spawn(&pid, exe, &fa, NULL, (char *const *)(uintptr_t)child, environ);
        (void)posix_spawn_file_actions_destroy(&fa);
        ASSERT(rc == 0);
        int status = 0;
        pid_t reaped;
        do reaped = waitpid(pid, &status, 0); /* The child's alarm bounds the wait. */
        while (reaped < 0 && errno == EINTR);
        ASSERT(reaped == pid);
        bool exited = WIFEXITED(status);
        int code = exited ? WEXITSTATUS(status) : WIFSIGNALED(status) ? WTERMSIG(status) : -1;
        bool ok = false;
        switch (cases[i].want) {
        case EXITS_ZERO: ok = exited && code == 0; break;
        case DIES_BY_SIGBUS: ok = !exited && code == SIGBUS; break;
        case DIES_BY_SIGABRT: ok = !exited && code == SIGABRT; break;
        }
        if (!ok) {
            fprintf(stderr, "os_fault_test: %s child %s %d\n", cases[i].name,
                    exited ? "exited" : "died by signal", code);
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
