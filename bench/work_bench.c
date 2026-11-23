/* Synthetic submit, cancellation, retirement and foreground delivery. */
#include "work/work.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The v1 work bench uses this sample count and a two-second fixture watchdog.
 * Each trial publishes one ordinary result and one cancellable start token. */
enum { TRIALS = 1000 };
#define WAIT_NS INT64_C(2000000000)

typedef struct samples {
    uint64_t submit[TRIALS];
    uint64_t start[TRIALS];
    uint64_t drain[TRIALS];
    uint64_t cancel[TRIALS];
    uint64_t retire[TRIALS];
    uint64_t foreground[TRIALS];
} samples;

static void publish(work_ctx *ctx)
{
    uint64_t start = os_now_ns();
    work_msg msg = {0};
    memcpy(msg.data, &start, sizeof start);
    (void)work_publish(ctx, &msg);
}

static void hold_bulk(work_ctx *ctx)
{
    publish(ctx);
    while (!work_should_stop(ctx)) os_yield();
}

static void capture_start(const work_msg *msg, void *arg)
{
    memcpy(arg, msg->data, sizeof(uint64_t));
}

static bool finish(const work_pool *p, work_handle h)
{
    uint64_t deadline = os_now_ns() + WAIT_NS;
    while (!work_handle_finished(p, h)) {
        if (os_now_ns() >= deadline) return false;
        os_yield();
    }
    return true;
}

static bool trial(work_pool *p, samples *s, size_t i)
{
    uint64_t t0 = os_now_ns();
    work_handle job = work_submit(p, (work_job){publish, NULL, 0, WORK_BULK});
    s->submit[i] = os_now_ns() - t0;
    if (!job.epoch || os_wake_wait(work_pool_wake(p), WAIT_NS) != OS_OK) return false;
    uint64_t start = 0;
    uint64_t drain = os_now_ns();
    if (work_mailbox_drain_bounded(p, capture_start, &start, 1, 0) != 1) return false;
    s->drain[i] = os_now_ns() - drain;
    s->start[i] = start - t0;
    if (!finish(p, job)) return false;

    work_handle bulk = work_submit(p, (work_job){hold_bulk, NULL, 0, WORK_BULK});
    if (!bulk.epoch || os_wake_wait(work_pool_wake(p), WAIT_NS) != OS_OK) return false;
    /* Consume the start token so the next wake comes from foreground. */
    if (work_mailbox_drain_bounded(p, capture_start, &start, 1, 0) != 1) return false;
    t0 = os_now_ns();
    work_handle fg = work_submit(p, (work_job){publish, NULL, 0, WORK_FOREGROUND});
    if (!fg.epoch || os_wake_wait(work_pool_wake(p), WAIT_NS) != OS_OK) return false;
    if (work_mailbox_drain_bounded(p, capture_start, &start, 1, 0) != 1) return false;
    s->foreground[i] = os_now_ns() - t0;
    if (!finish(p, fg)) return false;

    t0 = os_now_ns();
    work_cancel(p, bulk);
    s->cancel[i] = os_now_ns() - t0;
    if (!finish(p, bulk)) return false;
    s->retire[i] = os_now_ns() - t0;
    return true;
}

static void stamp(void)
{
    char power[32] = "unknown";
    char load[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = '\0';
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(f);
    }
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s at start, %d trials\n", tag, power, load, TRIALS);
}

static void report(const char *name, uint64_t *values)
{
    uint64_t p50 = trace_fmt_pct(values, TRIALS, 50);
    uint64_t p99 = trace_fmt_pct(values, TRIALS, 99);
    printf("%s p50=%" PRIu64 " ns p99=%" PRIu64 " ns\n", name, p50, p99);
}

int main(void)
{
    stamp();
    work_pool pool;
    if (work_pool_init_foreground(&pool, 1, 0) != 0) {
        puts("work_bench: pool unavailable");
        return 0;
    }
    samples s;
    size_t n = 0;
    while (n < TRIALS && trial(&pool, &s, n)) n++;
    work_pool_shutdown(&pool);
    if (n != TRIALS) {
        printf("work_bench: incomplete samples (%zu/%d)\n", n, TRIALS);
        return 0;
    }
    report("work_submit_call", s.submit);
    report("work_submit_to_start", s.start);
    report("work_drain_one", s.drain);
    report("work_cancel_call", s.cancel);
    report("work_cancel_to_observed_retirement", s.retire);
    report("work_foreground_delivery_while_bulk_held", s.foreground);
    return 0;
}
