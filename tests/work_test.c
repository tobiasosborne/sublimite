#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

static work_pool pool;
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void sleep_ms(long ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }

static _Atomic int ran;
static _Atomic int started;

static void job_pub(work_ctx *c)
{
    uint32_t n = (uint32_t)(uintptr_t)c->arg;
    for (uint32_t i = 0; i < n; i++) {
        work_msg m = {0};
        m.kind = i;
        m.generation = c->generation;
        CHECK(work_publish(c, &m));
    }
    atomic_fetch_add(&ran, 1);
}

static void job_block(work_ctx *c)
{
    atomic_store(&started, 1);
    while (!work_should_stop(c))
        sleep_ms(1);
    atomic_fetch_add(&ran, 1);
}

static void job_spin_pub(work_ctx *c)
{
    atomic_store(&started, 1);
    while (!work_should_stop(c))
        sleep_ms(1);
    work_msg m = {0};
    CHECK(!work_publish(c, &m));   /* stale -> dropped */
}

static void job_noop(work_ctx *c) { (void)c; atomic_fetch_add(&ran, 1); }

typedef struct { uint32_t seen[300]; size_t n; } coll;
static void collect(const work_msg *m, void *ud)
{
    coll *c = ud;
    if (c->n < 300) c->seen[c->n++] = m->kind;
}

int main(void)
{
    trace_init();
    CHECK(work_pool_init(&pool, 1, 2) == 0);

    /* submit / run / publish / drain order */
    work_handle h = work_submit(&pool, (work_job){ job_pub, (void *)(uintptr_t)100, 7, WORK_BULK });
    CHECK(h.epoch != 0);
    struct pollfd pfd = { work_pool_eventfd(&pool), POLLIN, 0 };
    CHECK(poll(&pfd, 1, 2000) == 1);
    while (atomic_load(&ran) < 1) sleep_ms(1);
    coll c = {0};
    CHECK(work_mailbox_drain(&pool, collect, &c) == 100);
    for (uint32_t i = 0; i < 100; i++) CHECK(c.seen[i] == i);

    /* raster job */
    atomic_store(&ran, 0);
    h = work_submit(&pool, (work_job){ job_noop, NULL, 0, WORK_RASTER });
    CHECK(h.epoch != 0);
    while (atomic_load(&ran) < 1) sleep_ms(1);

    /* cancel of a queued job never runs it */
    atomic_store(&ran, 0); atomic_store(&started, 0);
    work_handle hb = work_submit(&pool, (work_job){ job_block, NULL, 0, WORK_BULK });
    while (!atomic_load(&started)) sleep_ms(1);
    work_handle hq = work_submit(&pool, (work_job){ job_noop, NULL, 0, WORK_BULK });
    work_cancel(&pool, hq);
    work_cancel(&pool, hb);          /* running: stops at next poll */
    sleep_ms(50);
    CHECK(atomic_load(&ran) == 1);   /* only job_block incremented */
    work_cancel(&pool, hq);          /* stale handle: harmless */

    /* running cancel + stale publication dropped */
    atomic_store(&started, 0);
    h = work_submit(&pool, (work_job){ job_spin_pub, NULL, 0, WORK_BULK });
    while (!atomic_load(&started)) sleep_ms(1);
    work_cancel(&pool, h);
    sleep_ms(30);
    c.n = 0;
    CHECK(work_mailbox_drain(&pool, collect, &c) == 0);
    CHECK(atomic_load(&pool.dropped_stale) >= 1);

    /* publication then cancel before drain: dropped at drain */
    atomic_store(&ran, 0);
    h = work_submit(&pool, (work_job){ job_pub, (void *)(uintptr_t)5, 0, WORK_BULK });
    while (atomic_load(&ran) < 1) sleep_ms(1);
    work_cancel(&pool, h);
    CHECK(work_mailbox_drain(&pool, collect, &c) == 0);

    /* finished slot reused before drain: A's messages must still arrive */
    atomic_store(&ran, 0);
    work_handle ha = work_submit(&pool, (work_job){ job_pub, (void *)(uintptr_t)3, 1, WORK_BULK });
    CHECK(ha.epoch != 0);
    while (atomic_load(&ran) < 1 || atomic_load(&pool.slots[ha.slot].busy)) sleep_ms(1);
    work_handle hb2 = work_submit(&pool, (work_job){ job_noop, NULL, 2, WORK_BULK });
    CHECK(hb2.epoch != 0);
    sleep_ms(20);
    c.n = 0;
    CHECK(work_mailbox_drain(&pool, collect, &c) == 3);
    for (uint32_t i = 0; i < 3 && i < c.n; i++) CHECK(c.seen[i] == i);
    sleep_ms(10);
    /* slot is reusable again after the drain */
    work_handle hc2 = work_submit(&pool, (work_job){ job_noop, NULL, 3, WORK_BULK });
    CHECK(hc2.epoch != 0 && hc2.slot == ha.slot);
    sleep_ms(20);

    /* shutdown with jobs queued and one running */
    atomic_store(&started, 0);
    (void)work_submit(&pool, (work_job){ job_block, NULL, 0, WORK_BULK });
    while (!atomic_load(&started)) sleep_ms(1);
    for (int i = 0; i < 10; i++)
        (void)work_submit(&pool, (work_job){ job_noop, NULL, 0, WORK_BULK });
    work_pool_shutdown(&pool);

    printf("work_test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
