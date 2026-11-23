#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static work_pool pool;
static _Atomic int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); atomic_fetch_add(&fails, 1); } } while (0)

/* Wrap only work's calls into os. Successful calls use the real boundary,
 * including the sanitizer runtime's pthread interception. The UI thread
 * owns the fixture state. Workers only read the live mutex marker. */
static int fault_stage, acquisitions_seen, started_threads, joined_threads;
static int wake_fd;
static bool mutex_live, cond_live[3];

os_err __real_os_wake_init(os_wake *w);
os_err __real_os_mutex_init(os_mutex *m);
void __real_os_mutex_destroy(os_mutex *m);
void __real_os_mutex_lock(os_mutex *m);
os_err __real_os_cond_init(os_cond *c);
void __real_os_cond_destroy(os_cond *c);
os_err __real_os_thread_start(os_thread *t, void (*fn)(void *), void *arg);
os_err __real_os_thread_join(os_thread *t);

static bool fail_acquisition(void)
{
    return fault_stage && ++acquisitions_seen == fault_stage;
}

os_err __wrap_os_wake_init(os_wake *w)
{
    if (fail_acquisition()) return OS_E_IO;
    os_err rc = __real_os_wake_init(w);
    if (fault_stage && rc == OS_OK) wake_fd = os_wake_fd(w);
    return rc;
}

os_err __wrap_os_mutex_init(os_mutex *m)
{
    if (fail_acquisition()) return OS_E_IO;
    os_err rc = __real_os_mutex_init(m);
    if (m == &pool.mu) mutex_live = rc == OS_OK;
    return rc;
}

void __wrap_os_mutex_destroy(os_mutex *m)
{
    if (m == &pool.mu) {
        BASE_ASSERT(mutex_live);
        mutex_live = false;
    }
    __real_os_mutex_destroy(m);
}

void __wrap_os_mutex_lock(os_mutex *m)
{
    /* mutex_live changes only before worker creation or after all joins. */
    if (m == &pool.mu) BASE_ASSERT(mutex_live);
    __real_os_mutex_lock(m);
}

static bool *condition_live(const os_cond *c)
{
    if (c == &pool.cv[0]) return &cond_live[0];
    if (c == &pool.cv[1]) return &cond_live[1];
    if (c == &pool.foreground_cv) return &cond_live[2];
    return NULL;
}

os_err __wrap_os_cond_init(os_cond *c)
{
    if (fail_acquisition()) return OS_E_IO;
    os_err rc = __real_os_cond_init(c);
    bool *live = condition_live(c);
    if (live) *live = rc == OS_OK;
    return rc;
}

void __wrap_os_cond_destroy(os_cond *c)
{
    bool *live = condition_live(c);
    if (live) {
        BASE_ASSERT(*live);
        *live = false;
    }
    __real_os_cond_destroy(c);
}

os_err __wrap_os_thread_start(os_thread *t, void (*fn)(void *), void *arg)
{
    if (fail_acquisition()) return OS_E_IO;
    os_err rc = __real_os_thread_start(t, fn, arg);
    if (fault_stage && rc == OS_OK) started_threads++;
    return rc;
}

os_err __wrap_os_thread_join(os_thread *t)
{
    os_err rc = __real_os_thread_join(t);
    if (fault_stage && rc == OS_OK) joined_threads++;
    return rc;
}

static void sleep_ms(long ms)
{
    os_sleep_ns((uint64_t)ms * UINT64_C(1000000));
}

static bool wait_value(_Atomic uint32_t *v, uint32_t value)
{
    uint64_t end = os_now_ns() + 5000000000ull;
    while (atomic_load_explicit(v, memory_order_acquire) != value) {
        if (os_now_ns() >= end) { CHECK(false); return false; }
        sleep_ms(1);
    }
    return true;
}
static bool init_pool(uint32_t raster)
{
    int rc = work_pool_init(&pool, 1, raster);
    CHECK(rc == 0);
    return rc == 0;
}
static void wait_finished(work_handle h)
{
    /* Same hang bound as the v1 per-job wait. */
    uint64_t end = os_now_ns() + 5000000000ull;
    while (!work_handle_finished(&pool, h)) {
        BASE_ASSERT(os_now_ns() < end);
        sleep_ms(1);
    }
}
typedef struct fixture {
    _Atomic uint32_t started, ran;
    _Atomic uint64_t stopped_ns;
    uint32_t n;
} fixture;
static void job_pub(work_ctx *c)
{
    fixture *f = c->arg;
    for (uint32_t i = 0; i < f->n; i++) {
        work_msg m = {.kind = i, .generation = c->generation};
        CHECK(work_publish(c, &m));
    }
    atomic_fetch_add_explicit(&f->ran, 1, memory_order_release);
}
static void job_block(work_ctx *c)
{
    fixture *f = c->arg;
    atomic_store_explicit(&f->started, 1, memory_order_release);
    while (!work_should_stop(c)) sleep_ms(1);
    atomic_store(&f->stopped_ns, work_cancel_time_ns(c));
    atomic_fetch_add_explicit(&f->ran, 1, memory_order_release);
}
static void job_noop(work_ctx *c)
{
    fixture *f = c->arg;
    atomic_fetch_add_explicit(&f->ran, 1, memory_order_release);
}
typedef struct collection { size_t n; uint32_t next; } collection;
static void collect(const work_msg *m, void *ud)
{
    collection *c = ud;
    CHECK(m->kind == c->next++);
    c->n++;
}
static void discard(const work_msg *m, void *ud) { (void)m; (void)ud; }
static void result(const char *name, int before)
{
    printf("work_test: %s %s\n", name, atomic_load(&fails) == before ? "ok" : "FAIL");
}

typedef struct recursive_collection { size_t calls, nested; bool entered; } recursive_collection;
static void recursive_cb(const work_msg *m, void *ud)
{
    recursive_collection *c = ud;
    (void)m;
    c->calls++;
    if (!c->entered) {
        c->entered = true;
        c->nested = work_mailbox_drain(&pool, recursive_cb, c);
    }
}
static void test_recursive(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture f = {.n = 2};
    work_handle h = work_submit(&pool, (work_job){job_pub, &f, 1, WORK_BULK});
    CHECK(h.epoch);
    wait_finished(h);
    recursive_collection c = {0};
    size_t n = work_mailbox_drain_bounded(&pool, recursive_cb, &c, 2, 0);
    CHECK(c.calls == 2 && c.nested == 0 && n == 2);
    CHECK(atomic_load(&pool.slots[h.slot].pending) == 0);
    printf("work_test: section1 callbacks=%zu nested=%zu delivered=%zu pending=%u\n", c.calls, c.nested, n, atomic_load(&pool.slots[h.slot].pending));
    work_pool_shutdown(&pool);
    result("section1 recursive", before);
}
static void test_alignment(void)
{
    int before = atomic_load(&fails);
    work_pool *heap = aligned_alloc(_Alignof(work_pool), sizeof *heap);
    CHECK(heap && (uintptr_t)heap % _Alignof(work_pool) == 0);
    if (heap) {
        CHECK(work_pool_init(heap, 1, 0) == 0);
        work_pool_shutdown(heap);
        free(heap);
    }
    result("section2 alignment", before);
}
static void test_epoch(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture f = {0};
    work_handle old = work_submit(&pool, (work_job){job_noop, &f, 0, WORK_BULK});
    CHECK(old.epoch == 2);
    wait_finished(old);
    atomic_store(&pool.slots[old.slot].epoch, UINT32_MAX);
    work_handle cycle = work_submit(&pool, (work_job){job_noop, &f, 0, WORK_BULK});
    CHECK(cycle.epoch);
    wait_finished(cycle);
    work_handle replacement = work_submit(&pool, (work_job){job_block, &f, 0, WORK_BULK});
    CHECK(replacement.epoch);
    (void)wait_value(&f.started, 1);
    work_cancel(&pool, old);
    bool aliased = old.slot == replacement.slot && old.epoch == replacement.epoch;
    CHECK(!aliased);
    CHECK(atomic_load(&pool.slots[replacement.slot].epoch) == replacement.epoch);
    printf("work_test: section3 old=%u replacement=%u alias=%d\n", old.epoch, replacement.epoch, aliased ? 1 : 0);
    work_cancel(&pool, replacement);
    wait_finished(replacement);
    /* Also cross the last valid submission and cancellation boundaries. */
    atomic_store(&pool.slots[1].epoch, UINT32_MAX - 2u);
    atomic_store(&f.started, 0);
    work_handle last = work_submit(&pool, (work_job){job_block, &f, 0, WORK_BULK});
    CHECK(last.epoch);
    (void)wait_value(&f.started, 1);
    work_cancel(&pool, last);
    wait_finished(last);
    work_pool_shutdown(&pool);
    CHECK(atomic_load(&pool.slots[old.slot].epoch) == UINT32_MAX);
    if (init_pool(0)) {
        for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) atomic_store(&pool.slots[i].epoch, UINT32_MAX);
        work_cancel(&pool, (work_handle){0, UINT32_MAX});
        CHECK(atomic_load(&pool.slots[0].epoch) == UINT32_MAX);
        CHECK(work_submit(&pool, (work_job){job_noop, &f, 0, WORK_BULK}).epoch == 0);
        work_pool_shutdown(&pool);
        for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) CHECK(atomic_load(&pool.slots[i].epoch) == UINT32_MAX);
    }
    result("section3 epoch", before);
}
static void test_shutdown(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture active = {0}, queued = {0};
    work_handle h = work_submit(&pool, (work_job){job_block, &active, 0, WORK_BULK});
    CHECK(h.epoch);
    (void)wait_value(&active.started, 1);
    work_handle q = work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
    CHECK(q.epoch);
    work_pool_shutdown(&pool);
    CHECK(atomic_load(&queued.ran) == 0);
    CHECK(atomic_load(&pool.slots[q.slot].busy) == 0);
    CHECK(pool.queue[0].count == 0 && pool.queue[1].count == 0);
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) CHECK(atomic_load(&pool.slots[i].busy) == 0);
    printf("work_test: section4 queued_busy=%u queue_count=%u\n", atomic_load(&pool.slots[q.slot].busy), pool.queue[0].count);
    result("section4 shutdown", before);
}
static void test_closed_submit(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    work_pool_shutdown(&pool);
    uint32_t epoch = atomic_load(&pool.slots[0].epoch);
    fixture f = {0};
    work_handle h = work_submit(&pool, (work_job){job_noop, &f, 0, WORK_BULK});
    CHECK(h.epoch == 0);
    CHECK(atomic_load(&pool.slots[0].epoch) == epoch && pool.slots[0].job.fn == NULL);
    work_pool_shutdown(&pool);
    result("section5 closed_submit", before);
}
static void check_init_errors(int (*init)(work_pool *, uint32_t, uint32_t),
                              int conditions)
{
    /* Wake + mutex + configured conditions + bulk/two raster/optional foreground. */
    int synchronization = 2 + conditions;
    int acquisitions = synchronization + 3 + (conditions == 3);
    for (int stage = 1; stage <= acquisitions; stage++) {
        fault_stage = stage;
        acquisitions_seen = 0;
        started_threads = 0;
        joined_threads = 0;
        wake_fd = -1;
        int rc = init(&pool, 1, 2);
        fault_stage = 0;
        CHECK(rc == -1 && acquisitions_seen == stage);
        if (rc == 0) work_pool_shutdown(&pool);
        CHECK(work_pool_wake(&pool) == NULL && !mutex_live);
        CHECK(!cond_live[0] && !cond_live[1] && !cond_live[2]);
        CHECK(started_threads == (stage > synchronization ? stage - synchronization - 1 : 0));
        CHECK(joined_threads == started_threads && !pool.foreground_started);
        CHECK(stage == 1 || (wake_fd >= 0 && fcntl(wake_fd, F_GETFD) == -1 && errno == EBADF));
        work_pool_shutdown(&pool);
    }
    CHECK(init(&pool, 0, 0) == -1 && work_pool_wake(&pool) == NULL);
    work_pool_shutdown(&pool);
    CHECK(init(&pool, 1, WORK_MAX_RASTER + 1u) == -1 && work_pool_wake(&pool) == NULL);
    work_pool_shutdown(&pool);
    CHECK(init(&pool, 1, 2) == 0);
    work_pool_shutdown(&pool);
}

static void test_init_errors(void)
{
    int before = atomic_load(&fails);
    check_init_errors(work_pool_init, 2);
    result("section6 init_errors", before);
}

static void test_churn(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(2)) return;
    fixture active = {0}, queued = {0}, raster = {0};
    work_handle h = work_submit(&pool, (work_job){job_block, &active, 0, WORK_BULK});
    CHECK(h.epoch);
    (void)wait_value(&active.started, 1);
    for (uint32_t i = 0; i < 3u * WORK_MAX_JOBS; i++) {
        work_handle q = work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
        CHECK(q.epoch);
        work_cancel(&pool, q);
    }
    CHECK(pool.queue[0].count == 0);
    work_handle r = work_submit(&pool, (work_job){job_noop, &raster, 0, WORK_RASTER});
    CHECK(r.epoch);
    if (r.epoch) (void)wait_value(&raster.ran, 1);
    printf("work_test: section7 cancelled_queue=%u raster_epoch=%u\n", pool.queue[0].count, r.epoch);
    /* Uncancelled bulk reservations must also leave foreground capacity. */
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        (void)work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
    r = work_submit(&pool, (work_job){job_noop, &raster, 0, WORK_RASTER});
    CHECK(r.epoch);
    printf("work_test: section7 saturated_bulk=%u raster_epoch=%u\n", pool.queue[0].count, r.epoch);
    work_pool_shutdown(&pool);
    CHECK(atomic_load(&queued.ran) == 0);
    result("section7 churn", before);
}
static void slow_collect(const work_msg *m, void *ud)
{
    collect(m, ud);
    /* Deliberately slow to establish a deterministic slice regression. */
    sleep_ms(1);
}
static void test_slice(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture f = {.n = WORK_MAILBOX_CAP};
    work_handle h = work_submit(&pool, (work_job){job_pub, &f, 0, WORK_BULK});
    CHECK(h.epoch);
    wait_finished(h);
    collection c = {0};
    size_t n = work_mailbox_drain(&pool, slow_collect, &c);
    /* Descheduling before the first callback can also exhaust the slice. */
    CHECK(n <= 1 && n == c.n && work_mailbox_pending(&pool));
    CHECK(os_wake_wait(work_pool_wake(&pool), 0) == OS_OK);
    while (c.n < f.n) {
        size_t got = work_mailbox_drain_bounded(&pool, collect, &c, WORK_DRAIN_MAX_MESSAGES, 0);
        if (!got) { CHECK(false); break; }
    }
    CHECK(c.n == f.n && atomic_load(&pool.slots[h.slot].pending) == 0);
    work_pool_shutdown(&pool);
    result("section8 slice", before);
}
static void test_shutdown_stamp(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture f = {0};
    work_handle h = work_submit(&pool, (work_job){job_block, &f, 0, WORK_BULK});
    CHECK(h.epoch);
    (void)wait_value(&f.started, 1);
    work_pool_shutdown(&pool);
    CHECK(atomic_load(&f.stopped_ns) != 0);
    printf("work_test: section18 timestamp_nonzero=%d\n", atomic_load(&f.stopped_ns) != 0 ? 1 : 0);
    result("section18 shutdown_stamp", before);
}

static void job_stale_publish(work_ctx *c)
{
    fixture *f = c->arg;
    atomic_store_explicit(&f->started, 1, memory_order_release);
    while (!work_should_stop(c)) sleep_ms(1);
    work_msg m = {0};
    CHECK(!work_publish(c, &m));
}
typedef struct submit_collection { fixture *arg; work_handle submitted; } submit_collection;
static void submit_from_callback(const work_msg *m, void *ud)
{
    submit_collection *c = ud;
    c->submitted = work_submit(&pool, (work_job){job_noop, c->arg, 0, WORK_BULK});
    CHECK(c->submitted.epoch && c->submitted.slot != m->slot_);
}
static void test_reservations(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture f = {.n = 3}, noop = {0};
    CHECK(work_submit(&pool, (work_job){job_noop, &noop, 0, WORK_RASTER}).epoch == 0);
    work_handle a = work_submit(&pool, (work_job){job_pub, &f, 0, WORK_BULK});
    CHECK(a.epoch); wait_finished(a);
    work_handle b = work_submit(&pool, (work_job){job_noop, &noop, 0, WORK_BULK});
    CHECK(b.epoch && b.slot != a.slot); wait_finished(b);
    collection c = {0};
    CHECK(work_mailbox_drain_bounded(&pool, collect, &c, 3, 0) == 3 && c.n == 3);
    work_handle reuse = work_submit(&pool, (work_job){job_pub, &f, 0, WORK_BULK});
    CHECK(reuse.epoch && reuse.slot == a.slot); wait_finished(reuse);
    work_cancel(&pool, a); /* old submission must not invalidate reuse */
    CHECK(atomic_load(&pool.slots[reuse.slot].epoch) == reuse.epoch);
    work_cancel(&pool, reuse); /* suppress already-published messages */
    CHECK(work_mailbox_drain_bounded(&pool, discard, NULL, 3, 0) == 0);
    CHECK(atomic_load(&pool.slots[reuse.slot].pending) == 0);
    atomic_store(&f.started, 0);
    reuse = work_submit(&pool, (work_job){job_stale_publish, &f, 0, WORK_BULK});
    CHECK(reuse.epoch); (void)wait_value(&f.started, 1);
    work_cancel(&pool, reuse); wait_finished(reuse);
    CHECK(atomic_load(&pool.dropped_stale) == 4);
    f.n = 1;
    reuse = work_submit(&pool, (work_job){job_pub, &f, 0, WORK_BULK});
    CHECK(reuse.epoch); wait_finished(reuse);
    submit_collection sc = {.arg = &noop};
    CHECK(work_mailbox_drain_bounded(&pool, submit_from_callback, &sc, 1, 0) == 1);
    wait_finished(sc.submitted);
    work_pool_shutdown(&pool);
    result("reservation_and_cancel_contracts", before);
}
typedef struct generation_collection { uint32_t generation; size_t n; } generation_collection;
static void generation_cb(const work_msg *m, void *ud)
{
    generation_collection *c = ud;
    c->generation = m->generation;
    c->n++;
}
static void test_budget_controls(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(2)) return;
    fixture bulk = {.n = WORK_MAILBOX_CAP}, raster = {.n = 1};
    work_handle b = work_submit(&pool, (work_job){job_pub, &bulk, 11, WORK_BULK});
    work_handle r = work_submit(&pool, (work_job){job_pub, &raster, 22, WORK_RASTER});
    CHECK(b.epoch && r.epoch); wait_finished(b); wait_finished(r);
    generation_collection c = {0};
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 0, 0) == 0);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 1) == 0);
    CHECK(work_mailbox_pending(&pool));

    CHECK(os_wake_wait(work_pool_wake(&pool), 0) == OS_OK);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 0) == 1 && c.generation == 11);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 0) == 1 && c.generation == 22);
    work_cancel(&pool, b);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 0) == 0);
    CHECK(atomic_load(&pool.slots[b.slot].pending) == WORK_MAILBOX_CAP - 2u);
    CHECK(os_wake_wait(work_pool_wake(&pool), 0) == OS_OK);
    work_pool_shutdown(&pool);
    /* Dropping stale messages after shutdown must still release reservations. */
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    CHECK(atomic_load(&pool.slots[b.slot].pending) == 0 && c.n == 2);
    result("section8 explicit_budget_fairness_shutdown_drain", before);
}
static void test_cancel_fifo(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture blocker = {0}, queued[5] = {{0}};
    work_handle block = work_submit(&pool, (work_job){job_block, &blocker, 0, WORK_BULK});
    CHECK(block.epoch); (void)wait_value(&blocker.started, 1);
    /* Queue head near ring wrap tests in-place removal at first/middle/last. */
    os_mutex_lock(&pool.mu);
    pool.queue[0].head = WORK_MAX_JOBS - 2u;
    os_mutex_unlock(&pool.mu);
    work_handle h[5];
    for (size_t i = 0; i < 5; i++) {
        queued[i].n = 1;
        h[i] = work_submit(&pool, (work_job){job_pub, &queued[i], (uint32_t)i, WORK_BULK});
        CHECK(h[i].epoch);
    }
    work_cancel(&pool, h[0]); work_cancel(&pool, h[2]); work_cancel(&pool, h[4]);
    CHECK(pool.queue[0].count == 2);
    CHECK(pool.queue[0].q[pool.queue[0].head] == h[1].slot);
    CHECK(pool.queue[0].q[(pool.queue[0].head + 1u) % WORK_MAX_JOBS] == h[3].slot);
    work_cancel(&pool, block); wait_finished(block); wait_finished(h[1]); wait_finished(h[3]);
    generation_collection c = {0};
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 0) == 1 && c.generation == 1);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &c, 1, 0) == 1 && c.generation == 3);
    CHECK(atomic_load(&queued[0].ran) == 0 && atomic_load(&queued[2].ran) == 0 && atomic_load(&queued[4].ran) == 0);
    work_pool_shutdown(&pool);
    result("section7 cancelled_queue_fifo_wrap", before);
}

typedef struct stream_fixture { uint32_t n, id; } stream_fixture;
typedef struct stream_collection { uint32_t next[3], total; } stream_collection;
static uint8_t payload_byte(uint32_t id, uint32_t seq, size_t byte)
{
    return (uint8_t)((id * 53u + seq * 17u + (uint32_t)byte * 7u) & 255u);
}
static void job_stream(work_ctx *ctx)
{
    const stream_fixture *f = ctx->arg;
    for (uint32_t seq = 0; seq < f->n; seq++) {
        work_msg m = {.kind = seq, .generation = f->id};
        for (size_t byte = 0; byte < sizeof m.data; byte++) m.data[byte] = payload_byte(f->id, seq, byte);
        while (!work_publish(ctx, &m)) {
            if (work_should_stop(ctx)) return;
            os_yield();
        }
    }
}
static void stream_collect(const work_msg *m, void *ud)
{
    stream_collection *c = ud;
    CHECK(m->generation >= 1 && m->generation <= 3);
    if (m->generation < 1 || m->generation > 3) return;
    uint32_t id = m->generation;
    CHECK(m->kind == c->next[id - 1u]++);
    bool intact = true;
    for (size_t byte = 0; byte < sizeof m->data; byte++)
        if (m->data[byte] != payload_byte(id, m->kind, byte)) intact = false;
    CHECK(intact);
    c->total++;
}
static void test_live_ordering(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(2)) return;
    /* No started/ran/busy/completion load before consuming payload. Each job
     * has distinct immutable input; only mailbox release/acquire publishes its
     * writes to stream_collect. Start both raster workers and a bulk producer. */
    stream_fixture fixtures[3] = {{8192, 1}, {8192, 2}, {8192, 3}};
    work_handle handles[3];
    for (size_t i = 0; i < 3; i++) {
        handles[i] = work_submit(&pool, (work_job){job_stream, &fixtures[i], fixtures[i].id, i == 0 ? WORK_BULK : WORK_RASTER});
        CHECK(handles[i].epoch);
    }
    stream_collection c = {0};
    uint64_t end = os_now_ns() + 20000000000ull;
    while (c.total < 3u * fixtures[0].n && os_now_ns() < end && atomic_load(&fails) == before) {
        (void)work_mailbox_drain(&pool, stream_collect, &c);
        if (!work_mailbox_pending(&pool))
            (void)os_wake_wait(work_pool_wake(&pool), 10000000);
    }
    CHECK(c.total == 3u * fixtures[0].n);
    for (size_t i = 0; i < 3; i++) CHECK(c.next[i] == fixtures[i].n);
    /* Joining only after integrity checks cannot hide a mailbox race. */
    work_pool_shutdown(&pool);
    for (size_t i = 0; i < 3; i++) CHECK(atomic_load(&pool.slots[handles[i].slot].pending) == 0);
    printf("work_test: section15 live_integrity=%u producers=3\n", c.total);
    result("section15 live_ordering", before);
}
typedef struct full_fixture { _Atomic uint32_t attempted; } full_fixture;
static void job_full(work_ctx *c)
{
    full_fixture *f = c->arg;
    for (uint32_t i = 0; i < WORK_MAILBOX_CAP + 1u; i++) {
        work_msg m = {.kind = i};
        bool ok = work_publish(c, &m);
        CHECK(ok == (i < WORK_MAILBOX_CAP));
    }
    /* Scheduling only: relaxed so it supplies no payload visibility edge. */
    atomic_store_explicit(&f->attempted, 1, memory_order_relaxed);
}
static void test_full_wrap(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    uint32_t origin = UINT32_MAX - WORK_MAILBOX_CAP / 2u;
    atomic_store(&pool.mb[0].head, origin);
    atomic_store(&pool.mb[0].tail, origin);
    full_fixture f = {0};
    work_handle h = work_submit(&pool, (work_job){job_full, &f, 0, WORK_BULK});
    CHECK(h.epoch);
    uint64_t end = os_now_ns() + 5000000000ull;
    while (!atomic_load_explicit(&f.attempted, memory_order_relaxed) && os_now_ns() < end) os_yield();
    CHECK(atomic_load_explicit(&f.attempted, memory_order_relaxed));
    CHECK(atomic_load_explicit(&pool.dropped_full, memory_order_relaxed) == 1);
    collection c = {0};
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, collect, &c);
    CHECK(c.n == WORK_MAILBOX_CAP);
    CHECK(atomic_load(&pool.mb[0].head) == origin + WORK_MAILBOX_CAP);
    wait_finished(h);
    CHECK(atomic_load(&pool.slots[h.slot].pending) == 0);
    work_pool_shutdown(&pool);
    result("section15 full_wrap", before);
}
static uint32_t random_step(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}
static void job_maybe_pub(work_ctx *c)
{
    fixture *f = c->arg;
    for (uint32_t i = 0; i < f->n; i++) {
        work_msg m = {.kind = i, .generation = c->generation};
        if (!work_publish(c, &m)) return; /* cancellation/fullness are legal */
    }
}
static void test_state_machine(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(2)) return;
    /* Stable arg storage across reuse; random dequeue/cancel/drain interleavings
     * exercise both classes and slot reservation transitions under TSan. */
    fixture fixtures[WORK_MAX_JOBS] = {0};
    work_handle handles[WORK_MAX_JOBS] = {{0}};
    uint32_t rng = 0x81238u;
    for (uint32_t step = 0; step < 4096u; step++) {
        uint32_t index = random_step(&rng) % WORK_MAX_JOBS;
        work_handle h = handles[index];
        if (h.epoch && (random_step(&rng) & 1u)) work_cancel(&pool, h);
        if (!h.epoch || (!atomic_load_explicit(&pool.slots[h.slot].busy, memory_order_acquire) &&
                        !atomic_load_explicit(&pool.slots[h.slot].pending, memory_order_acquire))) {
            fixtures[index].n = random_step(&rng) % 4u;
            handles[index] = work_submit(&pool, (work_job){job_maybe_pub, &fixtures[index], step, (random_step(&rng) & 1u) ? WORK_BULK : WORK_RASTER});
        }
        (void)work_mailbox_drain_bounded(&pool, discard, NULL, random_step(&rng) % 8u, 0);
        if (!(step % 32u)) os_yield();
    }
    work_pool_shutdown(&pool);
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) {
        CHECK(atomic_load(&pool.slots[i].busy) == 0);
        CHECK(atomic_load(&pool.slots[i].pending) == 0);
    }
    result("section15 state_machine", before);
}
static void job_trace(work_ctx *c)
{
    trace_record_at(c->generation, TRACE_T1_DEQUEUE, c->generation);
}

static void test_trace_pool_lifetimes(void)
{
    int before = atomic_load(&fails);
    trace_reset();
    /* One more pool lifetime than the total number of physical slots. */
    const uint32_t lifetimes = TRACE_MAX_THREADS + 1u;
    for (uint32_t i = 1; i <= lifetimes; i++) {
        if (!init_pool(0)) return;
        work_handle h = work_submit(&pool, (work_job){job_trace, NULL, i, WORK_BULK});
        CHECK(h.epoch != 0);
        wait_finished(h);
        work_pool_shutdown(&pool);
    }

    char path[] = "build/work-trace-XXXXXX";
    int fd = mkstemp(path);
    BASE_ASSERT(fd >= 0 && os_unlink(path) == OS_OK);
    os_file file = os_file_from_fd(fd);
    BASE_ASSERT(trace_dump(&file) == 0);
    trace_loaded loaded;
    BASE_ASSERT(trace_fmt_load_dump(&file, &loaded) == 0);
    size_t seen = 0;
    for (size_t i = 0; i < loaded.nrecs; i++) {
        const trace_rec *rec = &loaded.recs[i];
        CHECK(rec->ev == TRACE_T1_DEQUEUE);
        CHECK(rec->frame_id == i + 1 && rec->ns == i + 1);
        seen++;
    }
    printf("work_test: trace pool lifetimes recorded=%zu/%u\n", seen, lifetimes);
    CHECK(seen == lifetimes);
    trace_fmt_dump_free(&loaded);
    os_file_close(&file);
    result("trace_pool_lifetimes", before);
}

static void test_selective_receive(void);
static void test_atomic_batch(void);
static void test_receive_slices(void);
static void test_receive_routes(void);
static void test_finished_leases(void);
static void test_batch_contracts(void);
static void test_dormant_receive_wrap(void);
static void test_foreground_progress(void);
static void test_foreground_contracts(void);
static void test_foreground_cancel_shutdown(void);
static void test_foreground_mailbox(void);
static void test_foreground_init_errors(void);
static void test_continuations(void);
static void test_foreground_priority(void);
static bool init_foreground_pool(uint32_t raster);
int main(int argc, char **argv)
{
    struct test { const char *name; void (*fn)(void); } tests[] = {
        {"1", test_recursive}, {"2", test_alignment}, {"3", test_epoch},
        {"4", test_shutdown}, {"5", test_closed_submit}, {"6", test_init_errors},
        {"7", test_churn}, {"8", test_slice}, {"15", test_live_ordering},
        {"15", test_full_wrap}, {"15", test_state_machine},
        {"18", test_shutdown_stamp},
        {"basic", test_reservations}, {"8", test_budget_controls}, {"7", test_cancel_fifo},
        {"receive", test_selective_receive}, {"batch", test_atomic_batch},
        {"receive", test_receive_slices}, {"receive", test_receive_routes},
        {"receive", test_finished_leases}, {"batch", test_batch_contracts},
        {"receive", test_dormant_receive_wrap},
        {"foreground", test_foreground_progress},
        {"foreground", test_foreground_contracts},
        {"foreground", test_foreground_cancel_shutdown},
        {"foreground", test_foreground_mailbox},
        {"foreground", test_foreground_init_errors},
        {"continue", test_continuations},
        {"priority", test_foreground_priority},
        {"trace", test_trace_pool_lifetimes}
    };
    bool ran = false;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (argc == 1 || (argc == 2 && strcmp(argv[1], tests[i].name) == 0)) {
            tests[i].fn();
            ran = true;
        }
    }
    CHECK(ran);
    printf("work_test: %s\n", atomic_load(&fails) ? "FAIL" : "ok");
    return atomic_load(&fails) ? 1 : 0;
}

typedef struct continuation_fixture {
    fixture pause;
    _Atomic uint32_t release;
    uint32_t steps;
    bool yield_ok;
    uint32_t workers[3];
    fixture *reply;
} continuation_fixture;

static void job_continue(work_ctx *c)
{
    continuation_fixture *f = c->arg;
    if (f->steps < 3) f->workers[f->steps] = c->worker;
    f->steps++;
    if (f->steps == 2 && f->reply) CHECK(atomic_load(&f->reply->ran) == 1);
    if (f->steps == 1) {
        atomic_store(&f->pause.started, 1);
        while (!atomic_load(&f->release) && !work_should_stop(c)) sleep_ms(1);
    }
    if (f->steps < 3) f->yield_ok = work_continue(c);
}
static void job_ordered_continue(work_ctx *c)
{
    continuation_fixture *f = c->arg;
    work_msg msg = {.kind = f->steps, .generation = c->generation};
    CHECK(work_publish(c, &msg));
    job_continue(c);
}
typedef struct ordered_delivery { uint32_t next; } ordered_delivery;
static void collect_ordered(const work_msg *msg, void *ctx)
{
    ordered_delivery *seen = ctx;
    CHECK(msg->kind == seen->next++);
}

static void test_continuations(void)
{
    int before = atomic_load(&fails);
    for (uint32_t cls = 0; cls < 3; cls++) {
        if (!init_foreground_pool(1)) return;
        continuation_fixture f = {0}; fixture reply = {0};
        work_handle h = work_submit(&pool, (work_job){job_continue, &f, 100, (work_class)cls});
        CHECK(h.epoch); (void)wait_value(&f.pause.started, 1);
        work_handle next = work_submit(&pool, (work_job){job_noop, &reply, 101, (work_class)cls});
        CHECK(next.epoch);
        f.reply = &reply;
        atomic_store(&f.release, 1);
        wait_finished(h); wait_finished(next);
        CHECK(f.steps == 3 && f.yield_ok && atomic_load(&reply.ran) == 1);
        CHECK(atomic_load(&pool.slots[h.slot].epoch) == h.epoch);
        work_pool_shutdown(&pool);
    }
    result("continuation_same_lease_all_classes", before);
}

static void test_foreground_priority(void)
{
    int before = atomic_load(&fails);
    if (!init_foreground_pool(1)) return;
    fixture blocker = {0}, reply = {.n = 2};
    work_handle bulk = work_submit(&pool, (work_job){job_block, &blocker, 10, WORK_BULK});
    CHECK(bulk.epoch); (void)wait_value(&blocker.started, 1);
    work_handle queued = work_submit(&pool, (work_job){job_pub, &reply, 11, WORK_BULK});
    CHECK(queued.epoch);
    int rc = work_prioritize(&pool, queued);
    CHECK(rc == 0);
    if (rc == 0) {
        wait_finished(queued);
        CHECK(!work_handle_finished(&pool, bulk));
        CHECK(atomic_load(&reply.ran) == 1);
        CHECK(pool.slots[queued.slot].job.cls == WORK_FOREGROUND);
        CHECK(atomic_load(&pool.slots[queued.slot].epoch) == queued.epoch);
        CHECK(work_prioritize(&pool, queued) == 0); /* idempotent completion */
    }
    work_cancel(&pool, bulk); wait_finished(bulk);
    wait_finished(queued);
    CHECK(work_prioritize(&pool, bulk) == -1); /* cancelled identity */
    collection got = {0};
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, collect, &got);
    CHECK(got.n == 2);
    work_pool_shutdown(&pool);
    CHECK(work_prioritize(&pool, queued) == -1);

    /* The running invocation remains on its owner until return; only its
     * next continuation moves, with the same identity and argument. */
    if (!init_foreground_pool(0)) return;
    continuation_fixture f = {0};
    work_handle running = work_submit(&pool, (work_job){job_continue, &f, 12, WORK_BULK});
    CHECK(running.epoch); (void)wait_value(&f.pause.started, 1);
    CHECK(work_prioritize(&pool, running) == 0);
    atomic_store(&f.release, 1);
    wait_finished(running);
    CHECK(f.steps == 3 && f.yield_ok);
    CHECK(f.workers[0] == 0 && f.workers[1] == WORK_FOREGROUND_WORKER &&
          f.workers[2] == WORK_FOREGROUND_WORKER);
    CHECK(pool.slots[running.slot].job.cls == WORK_FOREGROUND);
    work_pool_shutdown(&pool);

    if (!init_foreground_pool(0)) return;
    fixture foreground_blocker = {0}, marker = {0};
    work_handle held = work_submit(&pool, (work_job){job_block, &foreground_blocker, 1, WORK_FOREGROUND});
    CHECK(held.epoch); (void)wait_value(&foreground_blocker.started, 1);
    continuation_fixture stream = {0};
    work_handle moving = work_submit(&pool, (work_job){job_ordered_continue, &stream, 2, WORK_BULK});
    CHECK(moving.epoch); (void)wait_value(&stream.pause.started, 1);
    CHECK(work_prioritize(&pool, moving) == 0);
    atomic_store(&stream.release, 1);
    /* Keep foreground physically paused until the continuation is queued. */
    uint64_t deadline = os_now_ns() + 2000000000ull;
    bool continuation_queued = false;
    while (!continuation_queued && os_now_ns() < deadline) {
        os_mutex_lock(&pool.mu);
        for (uint32_t i = 0; i < pool.foreground_queue.count; i++)
            continuation_queued = continuation_queued || pool.foreground_queue.q[(pool.foreground_queue.head + i) % WORK_MAX_JOBS] == moving.slot;
        os_mutex_unlock(&pool.mu);
        if (!continuation_queued) sleep_ms(1);
    }
    CHECK(continuation_queued);
    work_handle mark = work_submit(&pool, (work_job){job_noop, &marker, 3, WORK_FOREGROUND});
    CHECK(mark.epoch);
    work_cancel(&pool, held); wait_finished(held); wait_finished(mark);
    /* Visit the destination mailbox first: old delivery must still precede
     * any message from the migrated invocation. */
    pool.slots[moving.slot].receive_next = pool.n_workers;
    ordered_delivery seen = {0};
    while (seen.next < 3 && os_now_ns() < deadline) {
        (void)work_mailbox_receive(&pool, moving, 2, collect_ordered, &seen);
        sleep_ms(1);
    }
    CHECK(seen.next == 3);
    wait_finished(moving);
    work_pool_shutdown(&pool);
    result("foreground_priority_same_lease_queued_running", before);
}

typedef struct selective_collection { work_handle h; uint32_t generation, next; size_t n; } selective_collection;
static void collect_selected(const work_msg *m, void *arg)
{
    selective_collection *s = arg;
    CHECK(m->slot_ == s->h.slot && m->epoch_ == s->h.epoch);
    CHECK(m->generation == s->generation);
    CHECK(m->kind == s->next++);
    s->n++;
}
static void test_selective_receive(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture foreign = {.n = 3}, selected = {.n = 2};
    work_handle a = work_submit(&pool, (work_job){job_pub, &foreign, 10, WORK_BULK});
    CHECK(a.epoch); wait_finished(a);
    work_handle b = work_submit(&pool, (work_job){job_pub, &selected, 11, WORK_BULK});
    CHECK(b.epoch); wait_finished(b);
    selective_collection received = {.h = b, .generation = 11};
    (void)work_mailbox_receive_bounded(&pool, b, 11, collect_selected, &received, WORK_MAILBOX_CAP, 0);
    CHECK(received.n == 2);
    CHECK(atomic_load(&pool.slots[a.slot].pending) == 3);
    CHECK(atomic_load(&pool.slots[b.slot].pending) == 0);
    collection others = {0};
    (void)work_mailbox_drain_bounded(&pool, collect, &others, WORK_MAILBOX_CAP, 0);
    CHECK(others.n == 3);
    printf("work_test: receive selected=%zu foreign=%zu\n", received.n, others.n);
    work_pool_shutdown(&pool);
    result("selective_receive", before);
}
static void test_atomic_batch(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture blocked = {0}, queued = {0}, batch = {0};
    work_handle blocker = work_submit(&pool, (work_job){job_block, &blocked, 0, WORK_BULK});
    CHECK(blocker.epoch); (void)wait_value(&blocked.started, 1);
    work_handle fill[WORK_MAX_JOBS - 3u];
    for (size_t i = 0; i < WORK_MAX_JOBS - 3u; i++) {
        fill[i] = work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
        CHECK(fill[i].epoch);
    }
    work_job jobs[4]; work_handle handles[4], untouched[4];
    for (size_t i = 0; i < 4; i++) {
        jobs[i] = (work_job){job_noop, &batch, 22, WORK_BULK};
        handles[i] = (work_handle){UINT32_MAX, UINT32_MAX};
    }
    memcpy(untouched, handles, sizeof handles);
    uint32_t count_before = pool.queue[WORK_BULK].count;
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    CHECK(memcmp(handles, untouched, sizeof handles) == 0);
    CHECK(pool.queue[WORK_BULK].count == count_before);
    CHECK(atomic_load(&pool.slots[WORK_MAX_JOBS - 2u].epoch) == 1);
    CHECK(atomic_load(&pool.slots[WORK_MAX_JOBS - 1u].epoch) == 1);
    printf("work_test: batch fail_at=3 queue_delta=%u handles_unchanged=%d\n",
        pool.queue[WORK_BULK].count - count_before, memcmp(handles, untouched, sizeof handles) == 0);
    for (size_t i = 0; i < WORK_MAX_JOBS - 3u; i++) work_cancel(&pool, fill[i]);
    work_cancel(&pool, blocker); wait_finished(blocker);
    work_pool_shutdown(&pool);
    CHECK(atomic_load(&batch.ran) == 0);
    result("atomic_batch", before);
}

static void test_receive_slices(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    uint32_t origin = UINT32_MAX - 2u;
    atomic_store(&pool.mb[0].head, origin);
    atomic_store(&pool.mb[0].tail, origin);
    fixture foreign = {.n = 129}, target = {.n = 3};
    work_handle a = work_submit(&pool, (work_job){job_pub, &foreign, 30, WORK_BULK});
    CHECK(a.epoch); wait_finished(a);
    work_handle b = work_submit(&pool, (work_job){job_pub, &target, 31, WORK_BULK});
    CHECK(b.epoch); wait_finished(b);
    selective_collection received = {.h = b, .generation = 31};
    CHECK(work_mailbox_receive_bounded(&pool, b, 32, collect_selected, &received, 130, 0) == 0);
    CHECK(atomic_load(&pool.slots[b.slot].pending) == 3);
    CHECK(work_mailbox_receive_bounded(&pool, b, 31, collect_selected, &received, 0, 0) == 0);
    CHECK(work_mailbox_receive_bounded(&pool, b, 31, collect_selected, &received, WORK_MAILBOX_CAP, os_now_ns()) == 0);
    for (size_t i = 0; i < 160 && received.n < 3; i++)
        (void)work_mailbox_receive_bounded(&pool, b, 31, collect_selected, &received, 1, 0);
    CHECK(received.n == 3);
    CHECK(atomic_load(&pool.mb[0].head) == origin); /* foreign head still owns capacity */
    CHECK(atomic_load(&pool.slots[a.slot].pending) == 129);

    CHECK(os_wake_wait(work_pool_wake(&pool), 0) == OS_OK);
    collection others = {0};
    while (work_mailbox_pending(&pool))
        (void)work_mailbox_drain_bounded(&pool, collect, &others, 1, 0);
    CHECK(others.n == 129);
    CHECK(atomic_load(&pool.mb[0].head) == origin + 132u);
    CHECK(atomic_load(&pool.slots[a.slot].pending) == 0);
    work_pool_shutdown(&pool);
    result("receive_slices_generation_wrap_continuation", before);
}

typedef struct bound_collection { selective_collection selected; size_t nested; } bound_collection;
static void bound_cb(const work_msg *m, void *arg)
{
    bound_collection *bound = arg;
    collect_selected(m, &bound->selected);
    CHECK(atomic_load(&pool.slots[m->slot_].pending) > 0);
    bound->nested += work_mailbox_receive_bounded(&pool, bound->selected.h,
        bound->selected.generation, discard, NULL, WORK_MAILBOX_CAP, 0);
    bound->nested += work_mailbox_drain_bounded(&pool, discard, NULL, WORK_MAILBOX_CAP, 0);
}
static void test_receive_routes(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(1)) return;
    fixture target = {.n = 3}, foreign = {.n = 2};
    work_handle a = work_submit(&pool, (work_job){job_pub, &target, 41, WORK_BULK});
    CHECK(a.epoch); wait_finished(a);
    work_handle b = work_submit(&pool, (work_job){job_pub, &foreign, 42, WORK_RASTER});
    CHECK(b.epoch); wait_finished(b);
    bound_collection bound = {.selected = {.h = a, .generation = 41}};
    CHECK(work_mailbox_bind(&pool, a, 40, bound_cb, &bound) == -1);
    CHECK(work_mailbox_bind(&pool, a, 41, bound_cb, &bound) == 0);
    selective_collection others = {.h = b, .generation = 42};
    CHECK(work_mailbox_drain_bounded(&pool, collect_selected, &others, WORK_MAILBOX_CAP, 0) == 5);
    CHECK(bound.selected.n == 3 && bound.nested == 0 && others.n == 2);
    work_handle reuse = work_submit(&pool, (work_job){job_pub, &target, 43, WORK_BULK});
    CHECK(reuse.epoch && reuse.slot == a.slot); wait_finished(reuse);
    bound.selected = (selective_collection){.h = reuse, .generation = 43};
    CHECK(work_mailbox_bind(&pool, reuse, 43, bound_cb, &bound) == 0);
    CHECK(work_mailbox_bind(&pool, a, 41, NULL, NULL) == 0); /* cannot erase replacement binding */
    CHECK(work_mailbox_receive_bounded(&pool, reuse, 43, discard, NULL, WORK_MAILBOX_CAP, 0) == 3);
    CHECK(bound.selected.n == 3);
    work_handle cancelled = work_submit(&pool, (work_job){job_pub, &target, 44, WORK_BULK});
    CHECK(cancelled.epoch); wait_finished(cancelled);
    bound.selected = (selective_collection){.h = cancelled, .generation = 44};
    CHECK(work_mailbox_bind(&pool, cancelled, 44, bound_cb, &bound) == 0);
    work_cancel(&pool, cancelled);
    CHECK(work_mailbox_receive_bounded(&pool, cancelled, 44, bound_cb, &bound, WORK_MAILBOX_CAP, 0) == 0);
    CHECK(bound.selected.n == 0 && atomic_load(&pool.slots[cancelled.slot].pending) == 0);
    CHECK(work_mailbox_bind(&pool, cancelled, 44, NULL, NULL) == 0);
    work_handle shutdown = work_submit(&pool, (work_job){job_pub, &target, 45, WORK_BULK});
    CHECK(shutdown.epoch); wait_finished(shutdown);
    work_pool_shutdown(&pool);
    CHECK(work_mailbox_receive_bounded(&pool, shutdown, 45, discard, NULL, WORK_MAILBOX_CAP, 0) == 0);
    CHECK(!work_mailbox_pending(&pool));
    result("receive_binding_cancel_shutdown_reentrancy", before);
}

static void test_finished_leases(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    fixture first = {0}, second = {0}, never_run = {0};
    work_handle done = work_submit(&pool, (work_job){job_noop, &first, 1, WORK_BULK});
    CHECK(done.epoch); wait_finished(done);
    CHECK(work_handle_finished(&pool, done));
    work_handle live = work_submit(&pool, (work_job){job_block, &second, 2, WORK_BULK});
    CHECK(live.slot == done.slot && live.epoch == done.epoch + 1u);
    (void)wait_value(&second.started, 1);
    CHECK(work_handle_finished(&pool, done)); /* epoch+1 is replacement, not cancellation */
    CHECK(!work_handle_finished(&pool, live));
    work_handle queued = work_submit(&pool, (work_job){job_noop, &never_run, 3, WORK_BULK});
    CHECK(queued.epoch && !work_handle_finished(&pool, queued));
    work_cancel(&pool, queued);
    CHECK(work_handle_finished(&pool, queued) && atomic_load(&never_run.ran) == 0);
    work_cancel(&pool, live); wait_finished(live);
    CHECK(work_handle_finished(&pool, live));
    work_pool_shutdown(&pool);
    CHECK(work_handle_finished(&pool, live));
    result("physical_lease_completion_reuse", before);
}

typedef struct batch_visibility {
    const work_handle *handles;
    size_t count;
    _Atomic uint32_t ran;
} batch_visibility;
static void job_batch_visible(work_ctx *c)
{
    batch_visibility *v = c->arg;
    /* The first dequeued job must see all outputs and immutable job inputs.
     * This is the only bulk worker, so no later job can finish/reuse yet. */
    for (size_t i = 0; i < v->count; i++) {
        work_handle h = v->handles[i];
        CHECK(h.epoch != 0 && h.slot < WORK_MAX_JOBS);
        if (h.slot >= WORK_MAX_JOBS) continue;
        CHECK(c->pool->slots[h.slot].job.arg == v && c->pool->slots[h.slot].job.generation == 51);
        for (size_t j = 0; j < i; j++) CHECK(v->handles[j].slot != h.slot);
    }
    atomic_fetch_add_explicit(&v->ran, 1, memory_order_release);
}
static void test_batch_contracts(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    work_handle handles[WORK_MAX_JOBS];
    work_job jobs[WORK_MAX_JOBS];
    fixture pub = {.n = 1};
    /* Undrained completions, even from physically finished jobs, own slots. */
    for (uint32_t i = 0; i < WORK_MAX_JOBS - 2u; i++) {
        work_handle h = work_submit(&pool, (work_job){job_pub, &pub, 50, WORK_BULK});
        CHECK(h.epoch); wait_finished(h);
    }
    batch_visibility visible = {.handles = handles, .count = WORK_MAX_JOBS};
    for (size_t i = 0; i < WORK_MAX_JOBS; i++) {
        jobs[i] = (work_job){job_batch_visible, &visible, 51, WORK_BULK};
        handles[i] = (work_handle){UINT32_MAX, UINT32_MAX};
    }
    work_handle saved[WORK_MAX_JOBS]; memcpy(saved, handles, sizeof saved);
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    CHECK(memcmp(saved, handles, sizeof saved) == 0 && atomic_load(&visible.ran) == 0);
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    jobs[3].fn = NULL;
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    CHECK(memcmp(saved, handles, sizeof saved) == 0);
    jobs[3].fn = job_batch_visible;
    jobs[3].cls = WORK_RASTER;
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    jobs[3].cls = (work_class)42;
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    jobs[3].cls = WORK_BULK;
    CHECK(work_submit_batch(&pool, NULL, 4, handles) == -1);
    CHECK(work_submit_batch(&pool, jobs, 4, NULL) == -1);
    CHECK(work_submit_batch(&pool, jobs, WORK_MAX_JOBS + 1u, handles) == -1);
    CHECK(work_submit_batch(NULL, NULL, 0, NULL) == 0);
    CHECK(work_submit_batch(&pool, jobs, WORK_MAX_JOBS, handles) == 0);
    (void)wait_value(&visible.ran, WORK_MAX_JOBS);
    for (size_t i = 0; i < WORK_MAX_JOBS; i++) {
        wait_finished(handles[i]);
        CHECK(work_handle_finished(&pool, handles[i]));
    }
    work_pool_shutdown(&pool);
    memcpy(saved, handles, sizeof saved);
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    CHECK(memcmp(saved, handles, sizeof saved) == 0);
    result("batch_pending_invalid_full_publication_shutdown", before);

    if (!init_pool(0)) return;
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) atomic_store(&pool.slots[i].epoch, UINT32_MAX - 1u);
    CHECK(work_submit_batch(&pool, jobs, 4, handles) == -1);
    CHECK(memcmp(saved, handles, sizeof saved) == 0 && pool.queue[0].count == 0);
    work_pool_shutdown(&pool);
    result("batch_retired_identities", before);

    if (!init_pool(1)) return;
    fixture blocked = {0}, cancelled = {0}, raster = {.n = 1};
    work_handle blocker = work_submit(&pool, (work_job){job_block, &blocked, 0, WORK_BULK});
    CHECK(blocker.epoch); (void)wait_value(&blocked.started, 1);
    size_t shared = WORK_MAX_JOBS - WORK_RASTER_RESERVE;
    for (size_t i = 0; i < shared - 1; i++) jobs[i] = (work_job){job_noop, &cancelled, 52, WORK_BULK};
    jobs[shared - 1] = (work_job){job_pub, &raster, 53, WORK_RASTER};
    CHECK(work_submit_batch(&pool, jobs, shared, handles) == 0);
    wait_finished(handles[shared - 1]);
    CHECK(handles[shared - 1].slot >= shared);
    for (size_t i = 0; i < shared - 1; i++) work_cancel(&pool, handles[i]);
    CHECK(atomic_load(&cancelled.ran) == 0);
    work_cancel(&pool, blocker); wait_finished(blocker);
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    work_pool_shutdown(&pool);
    result("batch_mixed_foreground_reserve_cancellation", before);
}

typedef struct receive_wrap_fixture { _Atomic uint32_t phase, ready; } receive_wrap_fixture;
static void job_receive_wrap(work_ctx *c)
{
    receive_wrap_fixture *f = c->arg;
    for (uint32_t stage = 0; stage < 3; stage++) {
        while (atomic_load_explicit(&f->phase, memory_order_acquire) != stage)
            if (work_should_stop(c)) return;
        uint32_t count = stage == 0 ? 2u : 3u;
        for (uint32_t i = 0; i < count; i++) {
            work_msg m = {.kind = i, .generation = stage == 2 ? c->generation : c->generation + 1u};
            CHECK(work_publish(c, &m));
        }
        atomic_store_explicit(&f->ready, stage + 1u, memory_order_release);
    }
}
static void test_dormant_receive_wrap(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(0)) return;
    receive_wrap_fixture f = {0};
    work_handle h = work_submit(&pool, (work_job){job_receive_wrap, &f, 71, WORK_BULK});
    CHECK(h.epoch); (void)wait_value(&f.ready, 1);
    selective_collection selected = {.h = h, .generation = 71};
    CHECK(work_mailbox_receive_bounded(&pool, h, 71, collect_selected, &selected, 2, 0) == 0);
    (void)work_mailbox_drain_bounded(&pool, discard, NULL, WORK_MAILBOX_CAP, 0);
    /* Compress a dormant selector's nearly-full counter cycle. The ring is
     * empty and its producer is paused, just as in the existing wrap fixture. */
    uint32_t origin = UINT32_MAX - 2u;
    atomic_store(&pool.mb[0].head, origin); atomic_store(&pool.mb[0].tail, origin);
    atomic_store_explicit(&f.phase, 1, memory_order_release);
    (void)wait_value(&f.ready, 2);
    (void)work_mailbox_drain_bounded(&pool, discard, NULL, WORK_MAILBOX_CAP, 0);
    CHECK(atomic_load(&pool.mb[0].head) == 0);
    atomic_store_explicit(&f.phase, 2, memory_order_release);
    wait_finished(h);
    CHECK(work_mailbox_receive_bounded(&pool, h, 71, collect_selected, &selected, WORK_MAILBOX_CAP, 0) == 3);
    CHECK(selected.n == 3);
    work_pool_shutdown(&pool);
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    result("dormant_receive_cursor_wrap_order", before);
}

static bool init_foreground_pool(uint32_t raster)
{
    int rc = work_pool_init_foreground(&pool, 1, raster);
    CHECK(rc == 0);
    return rc == 0;
}

/* One completion byte is the only way out of a blocking bulk invocation. */
typedef struct blocking_io {
    int read_fd;
    _Atomic uint32_t started;
} blocking_io;

static void job_foreground_bulk(work_ctx *c)
{
    blocking_io *io = c->arg;
    uint8_t byte;
    ssize_t n;
    atomic_store_explicit(&io->started, 1, memory_order_release);
    do {
        n = read(io->read_fd, &byte, sizeof byte);
    } while (n < 0 && errno == EINTR);
    CHECK(n == sizeof byte);
}

static void job_foreground_reply(work_ctx *c)
{
    work_msg m = {.generation = c->generation};
    CHECK(work_publish(c, &m));
}

static void test_foreground_progress(void)
{
    int before = atomic_load(&fails);
    if (!init_foreground_pool(0)) return;
    int io[2];
    if (pipe(io) != 0) {
        CHECK(false);
        work_pool_shutdown(&pool);
        return;
    }
    blocking_io blocked = {.read_fd = io[0]};
    fixture queued = {0};
    work_handle bulk = work_submit(&pool, (work_job){job_foreground_bulk, &blocked, 0, WORK_BULK});
    CHECK(bulk.epoch && wait_value(&blocked.started, 1));
    work_handle next = work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
    CHECK(next.epoch);
    work_cancel(&pool, bulk);
    CHECK(!work_handle_finished(&pool, bulk));

    work_handle fg = work_submit(&pool, (work_job){job_foreground_reply, NULL, 84, WORK_FOREGROUND});
    CHECK(fg.epoch);
    /* v1 foreground fixture watchdog bounds the wait for one publication. */
    CHECK(os_wake_wait(work_pool_wake(&pool), 2000000000) == OS_OK);
    selective_collection seen = {.h = fg, .generation = 84};
    CHECK(work_mailbox_receive_bounded(&pool, fg, 84, collect_selected, &seen, WORK_MAILBOX_CAP, 0) == 1);
    CHECK(seen.n == 1 && !work_handle_finished(&pool, bulk));
    CHECK(atomic_load(&queued.ran) == 0);

    /* EOF also releases a blocked read if the completion write fails. */
    uint8_t byte = 1;
    CHECK(write(io[1], &byte, sizeof byte) == sizeof byte);
    CHECK(close(io[1]) == 0);
    wait_finished(bulk);
    wait_finished(next);
    wait_finished(fg);
    work_pool_shutdown(&pool);
    CHECK(close(io[0]) == 0);
    result("foreground_progress_during_blocking_bulk_io", before);
}

static void test_foreground_contracts(void)
{
    int before = atomic_load(&fails);
    if (!init_pool(1)) return;
    fixture unused = {0};
    work_job unavailable = {job_noop, &unused, 0, WORK_FOREGROUND};
    work_handle unchanged = {UINT32_MAX, UINT32_MAX};
    CHECK(work_submit(&pool, unavailable).epoch == 0);
    CHECK(work_submit_batch(&pool, &unavailable, 1, &unchanged) == -1);
    CHECK(unchanged.slot == UINT32_MAX && unchanged.epoch == UINT32_MAX);
    work_pool_shutdown(&pool);
    if (!init_foreground_pool(1)) return;
    CHECK(pool.n_workers == 2 && pool.foreground_started);
    fixture blocked = {0}, queued = {0}, pub = {.n = 1};
    work_handle blocker = work_submit(&pool, (work_job){job_block, &blocked, 0, WORK_BULK});
    CHECK(blocker.epoch); (void)wait_value(&blocked.started, 1);
    uint32_t shared = WORK_MAX_JOBS - WORK_RASTER_RESERVE - WORK_FOREGROUND_RESERVE;
    work_handle filled[WORK_MAX_JOBS] = {{0}};
    for (uint32_t i = 1; i < shared; i++) {
        filled[i] = work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK});
        CHECK(filled[i].epoch != 0 && filled[i].slot < shared);
    }
    CHECK(work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_BULK}).epoch == 0);
    /* A mixed atomic batch must still fit both independent reserves with
     * every shared slot occupied. Undrained replies keep those leases owned. */
    work_job jobs[WORK_RASTER_RESERVE + WORK_FOREGROUND_RESERVE];
    work_handle handles[WORK_RASTER_RESERVE + WORK_FOREGROUND_RESERVE];
    for (uint32_t i = 0; i < WORK_RASTER_RESERVE + WORK_FOREGROUND_RESERVE; i++)
        jobs[i] = (work_job){job_pub, &pub, 90, i < WORK_FOREGROUND_RESERVE ? WORK_FOREGROUND : WORK_RASTER};
    CHECK(work_submit_batch(&pool, jobs, WORK_RASTER_RESERVE + WORK_FOREGROUND_RESERVE, handles) == 0);
    for (uint32_t i = 0; i < WORK_RASTER_RESERVE + WORK_FOREGROUND_RESERVE; i++) {
        CHECK(handles[i].epoch);
        wait_finished(handles[i]);
        CHECK(atomic_load(&pool.slots[handles[i].slot].pending) == 1);
        if (i < WORK_FOREGROUND_RESERVE)
            CHECK(handles[i].slot >= shared && handles[i].slot < shared + WORK_FOREGROUND_RESERVE);
        else
            CHECK(handles[i].slot >= shared + WORK_FOREGROUND_RESERVE);
    }
    CHECK(work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_FOREGROUND}).epoch == 0);
    CHECK(work_submit(&pool, (work_job){job_noop, &queued, 0, WORK_RASTER}).epoch == 0);
    for (uint32_t i = 0; i < WORK_FOREGROUND_RESERVE; i++) {
        CHECK(work_mailbox_receive_bounded(&pool, handles[i], 90, discard, NULL, WORK_MAILBOX_CAP, 0) == 1);
        CHECK(atomic_load(&pool.slots[handles[i].slot].pending) == 0);
    }
    CHECK(atomic_load(&pool.slots[handles[WORK_FOREGROUND_RESERVE].slot].pending) == 1);
    work_job refused[2] = {{job_noop, &queued, 0, WORK_FOREGROUND}, {job_noop, &queued, 0, WORK_BULK}};
    work_handle outputs[2] = {{UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}};
    CHECK(work_submit_batch(&pool, refused, 2, outputs) == -1);
    CHECK(outputs[0].epoch == UINT32_MAX && outputs[1].epoch == UINT32_MAX);
    CHECK(pool.foreground_queue.count == 0);
    work_handle again = work_submit(&pool, (work_job){job_pub, &pub, 91, WORK_FOREGROUND});
    CHECK(again.epoch); wait_finished(again);
    CHECK(again.slot == handles[0].slot && again.epoch != handles[0].epoch);
    work_cancel(&pool, handles[0]); /* old foreground lease cannot cancel reuse */
    CHECK(atomic_load(&pool.slots[again.slot].epoch) == again.epoch);
    work_cancel(&pool, again);
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, discard, NULL);
    for (uint32_t i = 1; i < shared; i++) work_cancel(&pool, filled[i]);
    work_cancel(&pool, blocker); wait_finished(blocker);
    CHECK(atomic_load(&queued.ran) == 0);
    work_pool_shutdown(&pool);
    result("foreground_capacity_batch_lease", before);
}

static void test_foreground_cancel_shutdown(void)
{
    int before = atomic_load(&fails);
    if (!init_foreground_pool(0)) return;
    CHECK(work_submit(&pool, (work_job){job_noop, NULL, 0, WORK_RASTER}).epoch == 0);
    fixture blocked = {0}, queued[5] = {{0}};
    work_handle blocker = work_submit(&pool, (work_job){job_block, &blocked, 0, WORK_FOREGROUND});
    CHECK(blocker.epoch); (void)wait_value(&blocked.started, 1);
    os_mutex_lock(&pool.mu);
    pool.foreground_queue.head = WORK_MAX_JOBS - 2u;
    os_mutex_unlock(&pool.mu);
    work_handle handles[5];
    for (uint32_t i = 0; i < 5; i++) {
        queued[i].n = 1;
        handles[i] = work_submit(&pool, (work_job){job_pub, &queued[i], i, WORK_FOREGROUND});
        CHECK(handles[i].epoch);
    }
    work_cancel(&pool, handles[0]); work_cancel(&pool, handles[2]); work_cancel(&pool, handles[4]);
    CHECK(pool.foreground_queue.count == 2);
    CHECK(pool.foreground_queue.q[pool.foreground_queue.head] == handles[1].slot);
    CHECK(pool.foreground_queue.q[(pool.foreground_queue.head + 1u) % WORK_MAX_JOBS] == handles[3].slot);
    CHECK(work_handle_finished(&pool, handles[0]) && work_handle_finished(&pool, handles[2]));
    CHECK(!work_handle_finished(&pool, blocker));
    work_cancel(&pool, blocker); wait_finished(blocker);
    wait_finished(handles[1]); wait_finished(handles[3]);
    generation_collection seen = {0};
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &seen, 1, 0) == 1 && seen.generation == 1);
    CHECK(work_mailbox_drain_bounded(&pool, generation_cb, &seen, 1, 0) == 1 && seen.generation == 3);
    CHECK(atomic_load(&queued[0].ran) == 0 && atomic_load(&queued[2].ran) == 0 && atomic_load(&queued[4].ran) == 0);
    fixture pending = {.n = 2}, never_run = {.n = 1};
    work_handle published = work_submit(&pool, (work_job){job_pub, &pending, 94, WORK_FOREGROUND});
    CHECK(published.epoch); wait_finished(published);
    atomic_store(&blocked.started, 0);
    blocker = work_submit(&pool, (work_job){job_stale_publish, &blocked, 95, WORK_FOREGROUND});
    CHECK(blocker.epoch); (void)wait_value(&blocked.started, 1);
    work_handle dropped = work_submit(&pool, (work_job){job_pub, &never_run, 96, WORK_FOREGROUND});
    CHECK(dropped.epoch);
    work_pool_shutdown(&pool);
    CHECK(work_handle_finished(&pool, blocker) && work_handle_finished(&pool, dropped));
    CHECK(pool.foreground_queue.count == 0 && !pool.foreground_started);
    CHECK(atomic_load(&never_run.ran) == 0 && atomic_load(&pool.slots[dropped.slot].busy) == 0);
    CHECK(work_mailbox_pending(&pool));
    CHECK(work_mailbox_drain_bounded(&pool, discard, NULL, WORK_MAILBOX_CAP, 0) == 0);
    CHECK(!work_mailbox_pending(&pool) && atomic_load(&pool.slots[published.slot].pending) == 0);
    work_pool_shutdown(&pool);
    result("foreground_fifo_cancel_shutdown_stale_drain", before);
}

static void test_foreground_mailbox(void)
{
    int before = atomic_load(&fails);
    if (!init_foreground_pool(WORK_MAX_RASTER)) return;
    CHECK(pool.n_workers == WORK_MAX_WORKERS);
    uint32_t origin = UINT32_MAX - 2u;
    atomic_store(&pool.foreground_mb.head, origin);
    atomic_store(&pool.foreground_mb.tail, origin);
    fixture foreign = {.n = 3}, selected = {.n = 2};
    work_handle a = work_submit(&pool, (work_job){job_pub, &foreign, 97, WORK_FOREGROUND});
    CHECK(a.epoch); wait_finished(a);
    work_handle b = work_submit(&pool, (work_job){job_pub, &selected, 98, WORK_FOREGROUND});
    CHECK(b.epoch); wait_finished(b);
    bound_collection bound = {.selected = {.h = b, .generation = 98}};
    CHECK(work_mailbox_bind(&pool, b, 98, bound_cb, &bound) == 0);
    for (uint32_t i = 0; i < 5; i++)
        (void)work_mailbox_receive_bounded(&pool, b, 98, discard, NULL, 1, 0);
    CHECK(bound.selected.n == 2 && bound.nested == 0);
    CHECK(atomic_load(&pool.foreground_mb.head) == origin);
    CHECK(atomic_load(&pool.slots[a.slot].pending) == 3);
    collection others = {0};
    while (work_mailbox_pending(&pool)) (void)work_mailbox_drain(&pool, collect, &others);
    CHECK(others.n == 3 && atomic_load(&pool.foreground_mb.head) == origin + 5u);
    work_pool_shutdown(&pool);

    if (!init_foreground_pool(1)) return;
    /* Live payload checks precede all job-completion acquires, exercising the
     * separate foreground SPSC mailbox alongside bulk and raster traffic. */
    stream_fixture fixtures[3] = {{2048, 1}, {2048, 2}, {2048, 3}};
    work_class classes[3] = {WORK_BULK, WORK_RASTER, WORK_FOREGROUND};
    for (uint32_t i = 0; i < 3; i++)
        CHECK(work_submit(&pool, (work_job){job_stream, &fixtures[i], fixtures[i].id, classes[i]}).epoch != 0);
    stream_collection seen = {0};
    uint64_t deadline = os_now_ns() + 20000000000ull;
    while (seen.total < 3u * fixtures[0].n && os_now_ns() < deadline && atomic_load(&fails) == before)
        (void)work_mailbox_drain(&pool, stream_collect, &seen);
    CHECK(seen.total == 3u * fixtures[0].n);
    work_pool_shutdown(&pool);
    CHECK(!work_mailbox_pending(&pool));
    result("foreground_mailbox_wrap_bind_live_integrity", before);
}

static void test_foreground_init_errors(void)
{
    int before = atomic_load(&fails);
    check_init_errors(work_pool_init_foreground, 3);
    result("foreground_init_failure_cleanup", before);
}
