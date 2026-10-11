#include "hl/hl.h"
#include "base/base.h"
#include "hl_fixture.h"
#include <stdio.h>
#include <string.h>
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
static TSPoint point(const uint8_t *text, size_t length)
{
    TSPoint p = {0, 0};
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') { ++p.row; p.column = 0; } else ++p.column;
    }
    return p;
}
static int finish(hl_request *request, uint32_t generation)
{
    uint32_t next = HL_VIEWPORT_READY;
    while (true) {
        hl_step step = hl_parse_slice(request, (hl_slice_control){0});
        if (step == HL_DONE) break;
        if (step != HL_RESULT) continue;
        REQUIRE(request->pending.phase == next);
        work_msg message;
        REQUIRE(hl_result_encode(&message, generation, &request->pending));
        if (next == HL_VIEWPORT_READY) {
            REQUIRE(hl_viewport_ready(&message, generation,
                        request->viewport.start_byte, request->viewport.end_byte));
            REQUIRE(!hl_viewport_ready(&message, generation + 1u,
                        request->viewport.start_byte, request->viewport.end_byte));
            /* Backpressure retries the same result, without document parsing. */
            TSTree *old = request->worker->document_tree;
            REQUIRE(hl_parse_slice(request, (hl_slice_control){0}) == HL_RESULT);
            REQUIRE(request->worker->document_tree == old);
            next = HL_DOCUMENT_READY;
        } else {
            REQUIRE(!hl_viewport_ready(&message, generation,
                        request->viewport.start_byte, request->viewport.end_byte));
            next = HL_PARSE_FAILED; /* sentinel after the two publications */
        }
        hl_result_published(request);
    }
    REQUIRE(next == HL_PARSE_FAILED);
    return 0;
}
int main(void)
{
    const uint8_t text[] = "prefix\nviewport\nsuffix\n";
    const size_t length = sizeof text - 1u;
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator);
    REQUIRE(source && piece_init_copy(source, text, length) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source);
    REQUIRE(snapshot);
    hl_worker worker;
    REQUIRE(hl_worker_init(&worker, hl_fixture_language()));
    const uint32_t first = (uint32_t)(sizeof "prefix\n" - 1u);
    const uint32_t end = first + (uint32_t)(sizeof "viewport\n" - 1u);
    TSRange viewport = {point(text, first), point(text, end), first, end};
    hl_request request;
    /* UINT64_MAX is the disabled-expiry clock bound for conformance, not a
     * performance rate. Production must pass its measured refresh interval. */
    edit_malloc_guard_begin();
    bool initialized = hl_request_init(&request, &worker, snapshot, 1, viewport,
                                     UINT64_MAX, 0, NULL, 0);
    size_t allocations = edit_malloc_guard_end();
    REQUIRE(initialized && allocations == 0);
    REQUIRE(finish(&request, 1) == 0);
    TSNode root = ts_tree_root_node(worker.document_tree);
    REQUIRE(!ts_node_has_error(root));
    REQUIRE(ts_node_end_byte(root) == length);
    TSNode visible = ts_tree_root_node(worker.viewport_tree);
    REQUIRE(ts_node_end_byte(visible) == end);
    uint32_t range_count = 0;
    TSRange *ranges = ts_tree_included_ranges(worker.viewport_tree, &range_count);
    REQUIRE(range_count == 1 && ranges[0].start_byte == first && ranges[0].end_byte == end);
    free(ranges); /* runtime-owned query output, outside typing */
    TSTree *original = ts_tree_copy(worker.document_tree);
    const uint8_t insertion[] = "x\ny";
    REQUIRE(piece_insert(source, first, insertion, sizeof insertion - 1u) == PIECE_OK);
    piece_snapshot *edited = piece_snapshot_take(source);
    REQUIRE(edited);
    TSInputEdit edit = {first, first, first + (uint32_t)(sizeof insertion - 1u),
                       point(text, first), point(text, first), {2, 1}};
    TSRange changed_viewport = {viewport.start_point, {3, 0}, first,
                               end + (uint32_t)(sizeof insertion - 1u)};
    edit_malloc_guard_begin();
    initialized = hl_request_init(&request, &worker, edited, 2, changed_viewport,
                                 UINT64_MAX, 1, &edit, 1);
    allocations = edit_malloc_guard_end();
    REQUIRE(initialized && allocations == 0);
    /* Mutating/destroying the owner cannot change either worker snapshot. */
    REQUIRE(piece_delete(source, 0, piece_len(source), NULL) == PIECE_OK);
    piece_destroy(source);
    REQUIRE(finish(&request, 2) == 0);
    REQUIRE(!ts_node_has_error(ts_tree_root_node(worker.document_tree)));
    REQUIRE(ts_node_end_byte(ts_tree_root_node(worker.document_tree)) == length + sizeof insertion - 1u);
    REQUIRE(ts_node_end_byte(ts_tree_root_node(original)) == length);
    REQUIRE(!ts_node_has_changes(ts_tree_root_node(original)));
    REQUIRE(!hl_request_init(&request, &worker, edited, 3, changed_viewport, 0, 2, NULL, 0));
    /* Ordered chain: insert a newline, then delete a whole later line using
     * coordinates in the already-edited source. The intermediate length
     * differs from both endpoint lengths. */
    const uint8_t chain_text[] = "a\nb\nc";
    source = piece_create(&allocator);
    REQUIRE(source && piece_init_copy(source, chain_text, sizeof chain_text - 1u) == PIECE_OK);
    piece_snapshot *chain_before = piece_snapshot_take(source);
    REQUIRE(chain_before);
    TSRange chain_range = {{0, 0}, {2, 1}, 0, (uint32_t)(sizeof chain_text - 1u)};
    REQUIRE(hl_request_init(&request, &worker, chain_before, 3, chain_range,
                            UINT64_MAX, 0, NULL, 0));
    REQUIRE(finish(&request, 3) == 0);
    REQUIRE(piece_insert(source, 1, (const uint8_t *)"X\n", sizeof "X\n" - 1u) == PIECE_OK);
    REQUIRE(piece_delete(source, 4, sizeof "b\n" - 1u, NULL) == PIECE_OK);
    piece_snapshot *chain_after = piece_snapshot_take(source);
    REQUIRE(chain_after);
    const TSInputEdit chain[] = {
        {1, 1, 3, {0, 1}, {0, 1}, {1, 0}},
        {4, 6, 4, {2, 0}, {3, 0}, {2, 0}}
    };
    REQUIRE(hl_request_init(&request, &worker, chain_after, 4, chain_range,
                            UINT64_MAX, 3, chain, sizeof chain / sizeof chain[0]));
    REQUIRE(finish(&request, 4) == 0);
    REQUIRE(request.edit_next == sizeof chain / sizeof chain[0]);
    root = ts_tree_root_node(worker.document_tree);
    REQUIRE(!ts_node_has_error(root) && ts_node_end_byte(root) == sizeof chain_text - 1u);
    REQUIRE(ts_node_end_point(root).row == 2 && ts_node_end_point(root).column == 1);
    piece_snapshot_release(chain_before);
    piece_snapshot_release(chain_after);
    piece_destroy(source);
    ts_tree_delete(original);
    piece_snapshot_release(snapshot);
    piece_snapshot_release(edited);
    hl_worker_destroy(&worker);
    puts("hl_parse_test: PASS viewport-first, snapshot lifetime, incremental edit, typing guard");
    return 0;
}
