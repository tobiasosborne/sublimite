#include "base/base.h"
#include "work/work.h"

#include <stdio.h>
#include <stdlib.h>

static bool guard_proven(void)
{
    /* Force one counted allocation at release optimization, as in law2_trace_test. */
    void *(*volatile allocate)(size_t) = malloc;
    base_malloc_guard_begin();
    void *p = allocate(1);
    size_t count = base_malloc_guard_end();
    bool active = base_malloc_guard_active();
    bool allocated = p != NULL;
    free(p);
    printf("work guard positive control: active=%d count=%zu\n", (int)active, count);
    return active && allocated && count == 1;
}

static void block_bulk(work_ctx *ctx)
{
    _Atomic bool *started = ctx->arg;
    atomic_store_explicit(started, true, memory_order_release);
    while (!work_should_stop(ctx)) os_sleep_ns(1000000); /* v1 work-test poll */
}

static void never_run(work_ctx *ctx)
{
    (void)ctx;
    ASSERT(false);
}

static void publish(work_ctx *ctx)
{
    work_msg msg = {.generation = ctx->generation};
    ASSERT(work_publish(ctx, &msg));
}

static void count_message(const work_msg *msg, void *arg)
{
    ASSERT(msg->generation != 0);
    (*(size_t *)arg)++;
}

static void finish(const work_pool *pool, work_handle h)
{
    /* The v1 work-test per-job hang bound and poll cadence. */
    uint64_t deadline = os_now_ns() + 5000000000ull;
    ASSERT(h.epoch);
    while (!work_handle_finished(pool, h)) {
        ASSERT(os_now_ns() < deadline);
        os_sleep_ns(1000000);
    }
}

int main(void)
{
    if (!guard_proven()) return EXIT_FAILURE;

    work_pool pool;
    ASSERT(work_pool_init_foreground(&pool, 1, 1) == 0);
    _Atomic bool started = false;
    work_handle blocked = work_submit(&pool, (work_job){block_bulk, &started, 0, WORK_BULK});
    ASSERT(blocked.epoch);
    uint64_t deadline = os_now_ns() + 5000000000ull; /* same v1 hang bound */
    while (!atomic_load_explicit(&started, memory_order_acquire)) {
        ASSERT(os_now_ns() < deadline);
        os_sleep_ns(1000000);
    }

    work_handle previous[2] = {{0}};
    base_malloc_guard_begin();
    /* Cycle one whole slot table's worth of submissions per worker. Each
     * cycle retires and drains before submitting the next lease in its slot. */
    for (uint32_t gen = 1; gen <= WORK_MAX_JOBS; gen++) {
        work_job jobs[] = {
            {never_run, NULL, gen, WORK_BULK},
            {publish, NULL, gen, WORK_RASTER},
            {publish, NULL, gen, WORK_FOREGROUND},
        };
        work_handle handles[sizeof jobs / sizeof jobs[0]];
        ASSERT(work_submit_batch(&pool, jobs, sizeof jobs / sizeof jobs[0], handles) == 0);
        work_cancel(&pool, handles[0]);
        work_cancel(&pool, handles[0]); /* repeated cancellation is harmless */
        ASSERT(work_handle_finished(&pool, handles[0]));
        finish(&pool, handles[1]);
        finish(&pool, handles[2]);

        if (gen > 1) {
            for (size_t i = 0; i < sizeof previous / sizeof previous[0]; i++) {
                ASSERT(previous[i].slot == handles[i + 1].slot);
                ASSERT(previous[i].epoch < handles[i + 1].epoch);
                work_cancel(&pool, previous[i]);
            }
        }
        previous[0] = handles[1];
        previous[1] = handles[2];

        size_t selected = 0, ordinary = 0;
        ASSERT(work_mailbox_bind(&pool, handles[2], gen, count_message, &selected) == 0);
        ASSERT(work_mailbox_receive_bounded(&pool, handles[2], gen, count_message,
                                                &ordinary, WORK_MAILBOX_CAP, 0) == 1);
        ASSERT(selected == 1 && ordinary == 0);
        ASSERT(work_mailbox_bind(&pool, handles[2], gen, NULL, NULL) == 0);
        if (gen % 2u == 0) work_cancel(&pool, handles[1]);
        size_t expected = gen % 2u;
        ASSERT(work_mailbox_drain_bounded(&pool, count_message, &ordinary, 1, 0) == expected);
        ASSERT(ordinary == expected && !work_mailbox_pending(&pool));
    }
    work_cancel(&pool, blocked);
    finish(&pool, blocked);
    size_t count = base_malloc_guard_end();
    work_pool_shutdown(&pool);

    bool active = base_malloc_guard_active();
    printf("work submit/cancel/drain/reuse Law-2 window: active=%d count=%zu\n", (int)active, count);
    if (!active || count != 0) return EXIT_FAILURE;
    puts("law2_work_test: PASS");
    return EXIT_SUCCESS;
}
