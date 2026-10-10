/* Stateful work leases: all lanes, continuation, cancellation and transport. */
#include "work/work.h"
#include "trace/trace.h"
#include <string.h>
#include <time.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

typedef struct fuzz_job {
    work_handle handle;
    uint32_t id, generation, limit, sent, received;
    bool cancelled;
} fuzz_job;
typedef struct fuzz_state {
    work_pool pool;
    fuzz_job jobs[WORK_MAX_JOBS];
    size_t count;
} fuzz_state;

static void job_step(work_ctx *ctx)
{
    fuzz_job *job = ctx->arg;
    if (work_should_stop(ctx)) return;
    work_msg msg = {.kind = job->id, .generation = ctx->generation};
    memcpy(msg.data, &job->sent, sizeof job->sent);
    if (work_publish(ctx, &msg)) job->sent++;
    if (job->sent < job->limit) (void)work_continue(ctx);
}

static void receive(const work_msg *msg, void *arg)
{
    fuzz_state *state = arg;
    REQUIRE(msg->kind < state->count);
    fuzz_job *job = &state->jobs[msg->kind];
    uint32_t sequence;
    memcpy(&sequence, msg->data, sizeof sequence);
    REQUIRE(!job->cancelled);
    REQUIRE(msg->slot_ == job->handle.slot && msg->epoch_ == job->handle.epoch);
    REQUIRE(msg->generation == job->generation && sequence == job->received);
    job->received++;
}

static work_job prepare(fuzz_state *state, uint8_t value)
{
    fuzz_job *job = &state->jobs[state->count];
    job->id = (uint32_t)state->count;
    job->generation = 100u + (uint32_t)state->count;
    job->limit = 1u + value % 12u;
    state->count++;
    return (work_job){job_step, job, job->generation, (work_class)(value % 3u)};
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size) return 0;
    fuzz_state state = {0};
    REQUIRE(work_pool_init_foreground(&state.pool, 1, 2) == 0);
    /* One input owns every argument until physical completion. No global
     * pool or persistent handles survive the input's shutdown. */
    for (size_t i = 0; i + 1 < size && i < 256; i += 2) {
        uint8_t op = data[i] % 8u, value = data[i + 1];
        size_t selected = state.count ? value % state.count : 0;
        if (op == 0 && state.count < WORK_MAX_JOBS) {
            size_t next = state.count;
            work_job job = prepare(&state, value);
            state.jobs[next].handle = work_submit(&state.pool, job);
        } else if (op == 1 && state.count) {
            work_cancel(&state.pool, state.jobs[selected].handle);
            state.jobs[selected].cancelled = true;
        } else if (op == 2 && state.count + 3 <= WORK_MAX_JOBS) {
            size_t first = state.count;
            work_job jobs[3]; work_handle handles[3] = {{0}};
            for (uint8_t j = 0; j < 3; j++) jobs[j] = prepare(&state, (uint8_t)(value + j));
            int rc = work_submit_batch(&state.pool, jobs, 3, handles);
            for (size_t j = 0; j < 3; j++) {
                state.jobs[first + j].handle = handles[j];
                REQUIRE(rc == 0 ? handles[j].epoch != 0 : handles[j].epoch == 0);
            }
        } else if (op == 3 && state.count) {
            fuzz_job *job = &state.jobs[selected];
            (void)work_mailbox_receive_bounded(&state.pool, job->handle, job->generation,
                                             receive, &state, value % 65u, 0);
        } else if (op == 4) {
            (void)work_mailbox_drain_bounded(&state.pool, receive, &state, value % 65u, 0);
        } else if (op == 7 && state.count) {
            (void)work_prioritize(&state.pool, state.jobs[selected].handle);
        } else if (state.count) {
            fuzz_job *job = &state.jobs[selected];
            (void)work_mailbox_bind(&state.pool, job->handle, job->generation,
                                   op == 5 ? receive : NULL, &state);
        }
    }
    uint64_t deadline = trace_now_ns() + 2000000000ull;
    for (;;) {
        (void)work_mailbox_drain_bounded(&state.pool, receive, &state, 64, 0);
        bool finished = true;
        for (size_t i = 0; i < state.count; i++)
            finished = finished && work_handle_finished(&state.pool, state.jobs[i].handle);
        if (finished && !work_mailbox_pending(&state.pool)) break;
        REQUIRE(trace_now_ns() < deadline);
        nanosleep(&(struct timespec){0, 10000}, NULL);
    }
    for (size_t i = 0; i < state.count; i++) {
        fuzz_job *job = &state.jobs[i];
        if (job->handle.epoch && !job->cancelled)
            REQUIRE(job->sent == job->limit && job->received == job->limit);
    }
    work_pool_shutdown(&state.pool);
    REQUIRE(!work_mailbox_pending(&state.pool));
    return 0;
}
