#include "find/find.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "find_timeout_test:%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

typedef struct job {
    os_mutex mutex;
    os_cond changed;
    bool entered_search, resume;
    size_t polls;
    find_source source;
    find_code code;
    find_result result;
} job;

bool __real_work_should_stop(const work_ctx *ctx);
bool __wrap_work_should_stop(const work_ctx *ctx)
{
    job *j = ctx->arg;
    /* (P) The first poll is before search. Hold the second, in preprocessing
     * or scanning, until the owner has cancelled the real worker epoch. */
    if (++j->polls == 2) {
        os_mutex_lock(&j->mutex);
        j->entered_search = true;
        os_cond_signal(&j->changed);
        while (!j->resume) os_cond_wait(&j->changed, &j->mutex);
        os_mutex_unlock(&j->mutex);
    }
    return __real_work_should_stop(ctx);
}

static void run_find(work_ctx *ctx)
{
    const uint8_t needle[] = "aba";
    const find_control control = {.work = ctx};
    job *j = ctx->arg;
    j->code = find_literal(&j->source, needle, sizeof needle - 1, (find_literal_options){0},
                           &control, &j->result);
}

int main(void)
{
    /* (G) v1 find_test's outer 30 s fail-stop for a lost start or shutdown.
     * The test uses a handshake for ordering and no delay for correctness. */
    alarm(30);
    uint8_t text[FIND_POLL_UNITS];
    memset(text, 'a', sizeof text);
    job j = {.source = {text, sizeof text, NULL}};
    CHECK(os_mutex_init(&j.mutex) == OS_OK && os_cond_init(&j.changed) == OS_OK);
    work_pool pool;
    /* (P) One worker and one job for the cancellation interleaving. */
    CHECK(work_pool_init(&pool, 1, 0) == 0);
    work_job request = {run_find, &j, 0, WORK_BULK};
    work_handle h = work_submit(&pool, request);
    CHECK(h.epoch != 0);
    os_mutex_lock(&j.mutex);
    while (!j.entered_search) os_cond_wait(&j.changed, &j.mutex);
    work_cancel(&pool, h);
    CHECK(!work_handle_finished(&pool, h));
    j.resume = true;
    os_cond_signal(&j.changed);
    os_mutex_unlock(&j.mutex);
    work_pool_shutdown(&pool);
    CHECK(j.polls >= 2 && j.code == FIND_CANCELLED && !j.result.total && !j.result.stored);
    /* Losing a submission is diagnosed before waiting for a start. */
    work_handle refused = work_submit(&pool, request);
    CHECK(!refused.epoch && work_handle_finished(&pool, refused));
    os_cond_destroy(&j.changed);
    os_mutex_destroy(&j.mutex);
    alarm(0);
    puts("find_timeout_test: real work epoch cancelled inside search, refused submit finished");
    return 0;
}
