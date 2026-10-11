#include "hl/hl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "file/file.h"
#include "hl_fixture.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

typedef struct logical_clock { uint64_t tick; bool cancel; } logical_clock;
static uint64_t tick(void *payload) { return ++((logical_clock *)payload)->tick; }
static bool cancel(void *payload) { return ((logical_clock *)payload)->cancel; }
static void collect(const work_msg *message, void *payload)
{
    uint32_t *phase = payload;
    hl_result result;
    EDIT_ASSERT(hl_result_decode(message, &result));
    EDIT_ASSERT(result.phase == *phase + 1u);
    *phase = result.phase;
}
static void file_collect(const work_msg *message, void *payload)
{
    (void)payload;
    file_msg result;
    if (!file_msg_decode(message, &result)) EDIT_ASSERT(result.status == FILE_OK);
}
static int faulted_snapshot(void)
{
    char path[] = "/tmp/hl-mapped-XXXXXX";
    int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    uint8_t text[2u * sizeof ((hl_request *)0)->input];
    memset(text, 'a', sizeof text);
    REQUIRE(write(fd, text, sizeof text) == (ssize_t)sizeof text);
    REQUIRE(close(fd) == 0);
    work_pool pool;
    REQUIRE(work_pool_init(&pool, 1, 0) == 0);
    /* A threshold of one byte forces this nonempty fixture through mmap. */
    file_open_opts options = {.copy_threshold = 1};
    file *opened = NULL;
    REQUIRE(file_open_begin(&pool, path, &options, &opened) == FILE_OK);
    while (!file_open_ready(opened))
        (void)work_mailbox_drain(&pool, file_collect, NULL);
    REQUIRE(file_open_mode(opened) == FILE_MODE_MMAP);
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator);
    REQUIRE(source && file_attach(opened, source) == FILE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source);
    hl_worker worker;
    REQUIRE(snapshot && hl_worker_init(&worker, hl_fixture_language()));
    hl_request request;
    TSRange range = {{0, 0}, {0, (uint32_t)sizeof text}, 0, (uint32_t)sizeof text};
    REQUIRE(hl_request_init(&request, &worker, snapshot, 1, range, UINT64_MAX, 0, NULL, 0));
    REQUIRE(truncate(path, 0) == 0);
    REQUIRE(hl_parse_slice(&request, (hl_slice_control){0}) == HL_RESULT);
    REQUIRE(request.pending.phase == HL_PARSE_FAILED && !worker.viewport_tree);
    REQUIRE(file_snapshot_faulted(snapshot));
    hl_result_published(&request);
    hl_worker_destroy(&worker);
    piece_snapshot_release(snapshot);
    piece_destroy(source);
    file_close(opened);
    work_pool_shutdown(&pool);
    REQUIRE(unlink(path) == 0);
    return 0;
}
static int sliced(hl_request *request)
{
    logical_clock clock = {0};
    hl_slice_control control = {&clock, tick, cancel};
    size_t slices = 0, yielded = 0;
    uint32_t phase = 0;
    while (true) {
        hl_step step = hl_parse_slice(request, control);
        if (step == HL_DONE) break;
        ++slices;
        /* Fixture has at most a shift + reduction per source unit. A bound
         * of four slices per byte plus preparation/publication states is a
         * progress watchdog derived from that work, not a latency gate. */
        REQUIRE(slices <= request->length * 4u + sizeof *request);
        if (step == HL_MORE && request->yielded) ++yielded;
        if (step == HL_RESULT) {
            REQUIRE(request->pending.phase == phase + 1u);
            phase = request->pending.phase;
            hl_result_published(request);
        }
    }
    REQUIRE(phase == HL_DOCUMENT_READY && yielded > 0);
    return 0;
}
int main(void)
{
    /* Cross the input cache boundary, with two whole pages of fixture bytes.
     * The logical clock advances one ns each check; refresh=one tick forces
     * expiry at each callback without using a guessed physical display rate. */
    uint8_t text[2u * sizeof ((hl_request *)0)->input];
    memset(text, 'a', sizeof text);
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator);
    REQUIRE(source && piece_init_copy(source, text, sizeof text) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source);
    REQUIRE(snapshot);
    hl_worker worker;
    REQUIRE(hl_worker_init(&worker, hl_fixture_language()));
    hl_request request;
    const uint32_t length = (uint32_t)sizeof text;
    TSRange range = {{0, 0}, {0, length}, 0, length};
    REQUIRE(hl_request_init(&request, &worker, snapshot, 1, range, 1, 0, NULL, 0));
    REQUIRE(sliced(&request) == 0);
    REQUIRE(!ts_node_has_error(ts_tree_root_node(worker.document_tree)));
    TSTree *completed = worker.document_tree;
    /* Cancel while a parse is suspended, then restart on a different range. */
    REQUIRE(hl_request_init(&request, &worker, snapshot, 2, range, 1, 0, NULL, 0));
    logical_clock clock = {0};
    hl_slice_control control = {&clock, tick, cancel};
    while (!request.yielded) REQUIRE(hl_parse_slice(&request, control) == HL_MORE);
    clock.cancel = true;
    REQUIRE(hl_parse_slice(&request, control) == HL_DONE);
    REQUIRE(worker.document_tree == completed && worker.document_generation == 1);
    REQUIRE(hl_request_init(&request, &worker, snapshot, 3, range, 1, 999, NULL, 0));
    REQUIRE(sliced(&request) == 0); /* missing base: fresh parse */

    /* Matching prior generation plus an invalid edit chain must fail closed. */
    TSInputEdit bad = {length + 1u, length + 1u, length + 1u, {0, length + 1u},
                       {0, length + 1u}, {0, length + 1u}};
    REQUIRE(hl_request_init(&request, &worker, snapshot, 4, range, UINT64_MAX, 3, &bad, 1));
    REQUIRE(hl_parse_slice(&request, (hl_slice_control){0}) == HL_RESULT);
    REQUIRE(request.pending.phase == HL_PARSE_FAILED);
    REQUIRE(worker.document_generation == 3);
    hl_result_published(&request);
    REQUIRE(hl_parse_slice(&request, (hl_slice_control){0}) == HL_DONE);

    /* Actual pool integration publishes viewport then document on one lease. */
    work_pool pool;
    REQUIRE(work_pool_init(&pool, 1, 0) == 0);
    REQUIRE(hl_request_init(&request, &worker, snapshot, 5, range,
                           UINT64_MAX, 0, NULL, 0));
    work_handle handle = work_submit(&pool, (work_job){hl_worker_run, &request, 5, WORK_BULK});
    REQUIRE(handle.epoch);
    uint32_t phase = 0;
    while (!work_handle_finished(&pool, handle) || work_mailbox_pending(&pool)) {
        (void)work_mailbox_receive(&pool, handle, 5, collect, &phase);
        struct pollfd fd = {work_pool_eventfd(&pool), POLLIN, 0};
        /* poll timeout zero is the nonblocking progress-pump control. */
        (void)poll(&fd, 1, 0);
    }
    REQUIRE(phase == HL_DOCUMENT_READY);
    work_pool_shutdown(&pool);
    hl_request_discard(&request);
    hl_worker_destroy(&worker);
    piece_snapshot_release(snapshot);
    piece_destroy(source);

    /* An empty viewport/document is still a completed parse/publication. */
    source = piece_create(&allocator);
    REQUIRE(source);
    snapshot = piece_snapshot_take(source);
    REQUIRE(snapshot && hl_worker_init(&worker, hl_fixture_language()));
    range = (TSRange){{0, 0}, {0, 0}, 0, 0};
    REQUIRE(hl_request_init(&request, &worker, snapshot, 1, range, UINT64_MAX, 0, NULL, 0));
    REQUIRE(hl_parse_slice(&request, (hl_slice_control){0}) == HL_RESULT);
    REQUIRE(request.pending.phase == HL_VIEWPORT_READY);
    hl_result_published(&request);
    REQUIRE(hl_parse_slice(&request, (hl_slice_control){0}) == HL_RESULT);
    REQUIRE(request.pending.phase == HL_DOCUMENT_READY);
    hl_result_published(&request);
    hl_worker_destroy(&worker);
    piece_snapshot_release(snapshot);
    piece_destroy(source);
    REQUIRE(faulted_snapshot() == 0);
    puts("hl_worker_test: PASS slice resume, cancellation, restart, failure, pool ordering, empty/faulted parse");
    return 0;
}
