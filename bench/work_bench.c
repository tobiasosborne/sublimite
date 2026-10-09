/* G6c caller acknowledgement, worker CPU slices, and physical cleanup. */
#include "work/work.h"
#include "trace/trace.h"
#include "harness.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUF_SZ (1ull << 30)
#define SLICE (64u * 1024u)
#define TRIALS 1000u
#define WAIT_NS 2000000000ull
#define RUN_NS 60000000000ull
#define CPU_GATE_NS 5000000ull

typedef struct scan_state {
    unsigned char *buf;
    _Atomic uint64_t started, done, start_ns;
    uint64_t max_slice, sink;
    int published_after_cancel;
} scan_state;

static uint64_t clock_ns(clockid_t id)
{
    struct timespec ts;
    if (clock_gettime(id, &ts) != 0) { perror("clock_gettime"); exit(2); }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Caller-side request to return, independent of worker observation. */
static uint64_t logical_elapsed(uint64_t request, uint64_t returned)
{
    return returned - request;
}

static int cancel_miss(const bench_samples *logical, uint64_t cpu_max)
{
    return logical->n == 0 || logical->dropped != 0 || cpu_max > CPU_GATE_NS
        || bench_p50(logical) > 1000000 || bench_p99(logical) > 5000000;
}

static int self_check_cancel(void)
{
    uint64_t v[1];
    bench_samples samples;
    bench_samples_init(&samples, v, 1);
    (void)bench_add(&samples, logical_elapsed(100, 20000100));
    int delayed_return = cancel_miss(&samples, 100);
    v[0] = 100;
    int long_cpu = cancel_miss(&samples, 6000000);
    v[0] = 1000000;
    int boundary = !cancel_miss(&samples, CPU_GATE_NS);
    v[0] = 1000001;
    int median_miss = cancel_miss(&samples, 100);
    int pass = delayed_return && long_cpu && boundary && median_miss;
    printf("work_bench self-check §10: %s delayed return rejected=%d CPU slice rejected=%d\n",
           pass ? "PASS" : "FAIL", delayed_return, long_cpu);
    return pass ? 0 : 1;
}

static int wait_value(_Atomic uint64_t *value, uint64_t deadline, const char *cause)
{
    while (atomic_load(value) == 0) {
        if (bench_now_ns() >= deadline) {
            fprintf(stderr, "work_bench FAIL: %s timeout (failed sample)\n", cause);
            return 1;
        }
        struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
    }
    return 0;
}

static int valid_handle(work_handle h, const char *cause)
{
    if (h.epoch == 0 || h.slot >= WORK_MAX_JOBS) {
        fprintf(stderr, "work_bench FAIL: %s invalid handle (failed sample)\n", cause);
        return 0;
    }
    return 1;
}

/* The parent never joins a potentially broken pool. Its deadline covers even
 * init, cancel, drain and shutdown; SIGKILL retires the isolated fixture. */
static int supervise(int (*run)(void), uint64_t budget, const char *cause)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (pid == 0) { int rc = run(); fflush(NULL); _exit(rc); }
    uint64_t deadline = bench_now_ns() + budget;
    for (;;) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
        if (result < 0 && errno != EINTR) { perror("waitpid"); return 2; }
        if (bench_now_ns() >= deadline) {
            fprintf(stderr, "work_bench FAIL: %s deadline; terminating fixture (no shutdown join)\n", cause);
            (void)kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            return 2;
        }
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
}

static int retire(work_pool *pool, work_handle handle, uint64_t deadline);

static int missing_events(void)
{
    _Atomic uint64_t value = 0;
    const char *causes[] = {"scan start", "cancel observation", "stamp start"};
    for (size_t i = 0; i < sizeof causes / sizeof causes[0]; i++)
        if (wait_value(&value, bench_now_ns() + 1000000, causes[i]) == 0) return 1;
    work_pool pool = {0};
    atomic_store(&pool.slots[0].busy, 1);
    if (retire(&pool, (work_handle){0, 1}, bench_now_ns() + 1000000) == 0) return 1;
    atomic_store(&value, 1);
    if (wait_value(&value, bench_now_ns(), "already signalled") != 0) return 1;
    return !valid_handle((work_handle){0, 0}, "refused submission")
        && valid_handle((work_handle){0, 1}, "valid submission") ? 0 : 1;
}

static int stuck_cleanup(void) { for (;;) pause(); return 0; }

static int self_check_deadlines(void)
{
    int missing = supervise(missing_events, 1000000000, "self-check missing events");
    int cleanup = supervise(stuck_cleanup, 10000000, "self-check stuck cleanup");
    int pass = missing == 0 && cleanup == 2;
    printf("work_bench self-check §11: %s missing waits/refusal bounded=%d cleanup bounded=%d\n",
           pass ? "PASS" : "FAIL", missing == 0, cleanup == 2);
    return pass ? 0 : 1;
}

static void scan_job(work_ctx *ctx)
{
    scan_state *state = ctx->arg;
    uint64_t acc = 0, previous = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    work_msg msg = {0};
    /* Exercise stale queued publication as well as publication after stop. */
    if (!work_publish(ctx, &msg)) { atomic_store(&state->started, 2); return; }
    atomic_store(&state->started, 1);
    for (;;) {
        for (size_t off = 0; off + SLICE <= BUF_SZ; off += SLICE) {
            bool stop = work_should_stop(ctx);
            uint64_t cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID);
            if (cpu - previous > state->max_slice) state->max_slice = cpu - previous;
            previous = cpu;
            if (stop) {
                state->published_after_cancel = work_publish(ctx, &msg) ? 1 : 0;
                state->sink = acc;
                atomic_store(&state->done, 1);
                return;
            }
            const uint64_t *words = (const uint64_t *)(state->buf + off);
            for (size_t i = 0; i < SLICE / 8; i++) acc += words[i];
        }
    }
}

static void stamp_job(work_ctx *ctx)
{
    scan_state *state = ctx->arg;
    atomic_store(&state->start_ns, bench_now_ns());
}

static void delivered(const work_msg *msg, void *ud)
{
    (void)msg;
    unsigned *count = ud;
    (*count)++;
}

static int retire(work_pool *pool, work_handle handle, uint64_t deadline)
{
    while (atomic_load(&pool->slots[handle.slot].busy)) {
        if (bench_now_ns() >= deadline) {
            fprintf(stderr, "work_bench FAIL: slot retirement timeout\n");
            return 1;
        }
        struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
    }
    return 0;
}

static int run_bench(void)
{
    char power[32];
    bench_battery_status(power, sizeof power);
    FILE *load_file = fopen("/proc/loadavg", "r");
    double load = -1.0;
    if (load_file) { if (fscanf(load_file, "%lf", &load) != 1) load = -1.0; fclose(load_file); }
    const char *tag = bench__tag_from_power(power);
    printf("TRACK work fixture (M)%s load1=%.2f power=%s; synthetic worker only\n", tag, load, power);
    work_pool pool;
    scan_state state = {0};
    uint64_t ackbuf[TRIALS], cpubuf[TRIALS], wallbuf[TRIALS], stbuf[TRIALS];
    bench_samples ack, cpu, wall, start;
    bench_samples_init(&ack, ackbuf, TRIALS);
    bench_samples_init(&cpu, cpubuf, TRIALS);
    bench_samples_init(&wall, wallbuf, TRIALS);
    bench_samples_init(&start, stbuf, TRIALS);
    state.buf = mmap(NULL, BUF_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (state.buf == MAP_FAILED) { perror("mmap"); return 2; }
    for (size_t i = 0; i < BUF_SZ; i += 4096) state.buf[i] = (unsigned char)i;
    trace_init();
    if (work_pool_init(&pool, 1, 0) != 0) { fprintf(stderr, "work_bench FAIL: pool init\n"); return 2; }
    uint64_t cpu_max = 0, rng = 88172645463325252ull;
    for (unsigned trial = 0; trial < TRIALS; trial++) {
        atomic_store(&state.started, 0); atomic_store(&state.done, 0);
        state.max_slice = 0;
        work_handle handle = work_submit(&pool, (work_job){scan_job, &state, 0, WORK_BULK});
        if (!valid_handle(handle, "scan submission")) return 2;
        if (wait_value(&state.started, bench_now_ns() + WAIT_NS, "scan start")) return 2;
        if (atomic_load(&state.started) != 1) { fprintf(stderr, "work_bench FAIL: initial publish refused\n"); return 2; }
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        struct timespec ts = {0, (long)(rng % 3000000u)};
        nanosleep(&ts, NULL);
        uint64_t request = bench_now_ns();
        work_cancel(&pool, handle);
        uint64_t returned = bench_now_ns();
        if (wait_value(&state.done, bench_now_ns() + WAIT_NS, "cancel observation")) return 2;
        if (retire(&pool, handle, bench_now_ns() + WAIT_NS)) return 2;
        unsigned count = 0;
        (void)work_mailbox_drain(&pool, delivered, &count);
        if (count || state.published_after_cancel) { fprintf(stderr, "work_bench FAIL: cancelled publication delivered/accepted\n"); return 2; }
        /* Every completed polling interval is bounded, including the one
         * which first observes cancellation. UI descheduling is not CPU time. */
        uint64_t slice = state.max_slice;
        if (slice > cpu_max) cpu_max = slice;
        (void)bench_add(&ack, logical_elapsed(request, returned));
        (void)bench_add(&cpu, slice);
        uint64_t finished = bench_now_ns();
        (void)bench_add(&wall, finished - request);
    }
    for (unsigned trial = 0; trial < TRIALS; trial++) {
        struct timespec ts = {0, 200000}; nanosleep(&ts, NULL);
        atomic_store(&state.start_ns, 0);
        uint64_t request = bench_now_ns();
        work_handle handle = work_submit(&pool, (work_job){stamp_job, &state, 0, WORK_BULK});
        if (!valid_handle(handle, "stamp submission")) return 2;
        if (wait_value(&state.start_ns, bench_now_ns() + WAIT_NS, "stamp start")) return 2;
        if (retire(&pool, handle, bench_now_ns() + WAIT_NS)) return 2;
        uint64_t stamp = atomic_load(&state.start_ns);
        (void)bench_add(&start, stamp > request ? stamp - request : 0);
    }
    work_pool_shutdown(&pool);
    (void)munmap(state.buf, BUF_SZ);
    printf("TRACK (M)%s load1=%.2f ", tag, load);
    int rc = bench_report("work_cancel_logical", &ack, 1000000, 5000000);
    printf("TRACK (M)%s load1=%.2f ", tag, load);
    rc |= bench_report("work_cancel_cpu_slice", &cpu, CPU_GATE_NS, CPU_GATE_NS);
    rc |= cancel_miss(&ack, cpu_max);
    printf("TRACK (M)%s load1=%.2f work_cancel_cpu_max=%llu ns gate_max=%llu (G) publication_suppression=PASS\n",
           tag, load, (unsigned long long)cpu_max, (unsigned long long)CPU_GATE_NS);
    printf("TRACK (M)%s load1=%.2f ", tag, load);
    rc |= bench_report("work_cancel_physical_TRACK", &wall, 0, 0);
    printf("TRACK (M)%s load1=%.2f ", tag, load);
    rc |= bench_report("work_submit_start_TRACK", &start, 0, 0);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-check-cancel") == 0) return self_check_cancel();
    if (argc == 2 && strcmp(argv[1], "--self-check-deadlines") == 0) return self_check_deadlines();
    return supervise(run_bench, RUN_NS, "benchmark including cleanup");
}
