/* Differential incremental edit parsing against a fresh parse of the same
 * immutable piece snapshot. Input splits into original and replacement bytes;
 * the first selector bytes choose deletion/insertion and viewport boundaries. */
#include "hl/hl.h"
#include "base/base.h"
#include "../tests/hl_fixture.h"
#include <string.h>

static TSPoint point(const uint8_t *bytes, size_t length)
{
    TSPoint result = {0, 0};
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] == '\n') { ++result.row; result.column = 0; }
        else ++result.column;
    }
    return result;
}
static void finish(hl_request *request)
{
    uint32_t phase = 0;
    for (;;) {
        hl_step step = hl_parse_slice(request, (hl_slice_control){0});
        if (step == HL_DONE) break;
        if (step != HL_RESULT) continue;
        EDIT_ASSERT(request->pending.phase == phase + 1u);
        phase = request->pending.phase;
        hl_result_published(request);
    }
    EDIT_ASSERT(phase == HL_DOCUMENT_READY);
}
static void same_tree(TSTree *incremental, TSTree *fresh)
{
    TSTreeCursor a = ts_tree_cursor_new(ts_tree_root_node(incremental));
    TSTreeCursor b = ts_tree_cursor_new(ts_tree_root_node(fresh));
    for (;;) {
        TSNode x = ts_tree_cursor_current_node(&a), y = ts_tree_cursor_current_node(&b);
        EDIT_ASSERT(ts_node_symbol(x) == ts_node_symbol(y));
        EDIT_ASSERT(ts_node_start_byte(x) == ts_node_start_byte(y));
        EDIT_ASSERT(ts_node_end_byte(x) == ts_node_end_byte(y));
        TSPoint xp = ts_node_end_point(x), yp = ts_node_end_point(y);
        EDIT_ASSERT(xp.row == yp.row && xp.column == yp.column);
        xp = ts_node_start_point(x); yp = ts_node_start_point(y);
        EDIT_ASSERT(xp.row == yp.row && xp.column == yp.column);
        bool ac = ts_tree_cursor_goto_first_child(&a);
        bool bc = ts_tree_cursor_goto_first_child(&b);
        EDIT_ASSERT(ac == bc);
        if (ac) continue;
        for (;;) {
            ac = ts_tree_cursor_goto_next_sibling(&a);
            bc = ts_tree_cursor_goto_next_sibling(&b);
            EDIT_ASSERT(ac == bc);
            if (ac) break;
            ac = ts_tree_cursor_goto_parent(&a);
            bc = ts_tree_cursor_goto_parent(&b);
            EDIT_ASSERT(ac == bc);
            if (!ac) goto done;
        }
    }
done:
    ts_tree_cursor_delete(&a);
    ts_tree_cursor_delete(&b);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Three selector fields precede the bytes; TS byte/point ABI is uint32. */
    if (size < 3u || size > UINT32_MAX) return 0;
    size_t old_length = (size - 3u) / 2u;
    const uint8_t *original = data + 3u;
    const uint8_t *insertion = original + old_length;
    size_t inserted = size - 3u - old_length;
    size_t start = data[0] % (old_length + 1u);
    size_t removed = data[1] % (old_length - start + 1u);
    size_t new_length = old_length - removed + inserted;
    uint8_t *model = malloc(new_length + 1u);
    EDIT_ASSERT(model);
    memcpy(model, original, start);
    memcpy(model + start, insertion, inserted);
    memcpy(model + start + inserted, original + start + removed, old_length - start - removed);
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator);
    EDIT_ASSERT(source && piece_init_copy(source, original, old_length) == PIECE_OK);
    piece_snapshot *before = piece_snapshot_take(source);
    EDIT_ASSERT(before);
    hl_worker worker;
    EDIT_ASSERT(hl_worker_init(&worker, hl_fixture_language()));
    hl_request request;
    TSRange range = {{0, 0}, point(original, old_length), 0, (uint32_t)old_length};
    /* UINT64_MAX selects no expiry for differential conformance; it is not
     * a production refresh rate or a performance claim. */
    EDIT_ASSERT(hl_request_init(&request, &worker, before, 1, range,
                               UINT64_MAX, 0, NULL, 0));
    finish(&request);
    TSInputEdit edit = {(uint32_t)start, (uint32_t)(start + removed),
        (uint32_t)(start + inserted), point(original, start),
        point(original, start + removed), point(model, start + inserted)};
    EDIT_ASSERT(piece_delete(source, start, removed, NULL) == PIECE_OK);
    EDIT_ASSERT(piece_insert(source, start, insertion, inserted) == PIECE_OK);
    piece_snapshot *after = piece_snapshot_take(source);
    EDIT_ASSERT(after);
    piece_destroy(source); /* both snapshots must survive their owner */
    size_t first = data[2] % (new_length + 1u);
    range = (TSRange){point(model, first), point(model, new_length),
                      (uint32_t)first, (uint32_t)new_length};
    EDIT_ASSERT(hl_request_init(&request, &worker, after, 2, range,
                               UINT64_MAX, 1, &edit, 1));
    finish(&request);
    TSParser *reference = ts_parser_new();
    EDIT_ASSERT(ts_parser_set_language(reference, hl_fixture_language()));
    TSTree *fresh = ts_parser_parse_string(reference, NULL, (const char *)model, (uint32_t)new_length);
    EDIT_ASSERT(fresh);
    same_tree(worker.document_tree, fresh);
    ts_tree_delete(fresh);
    ts_parser_delete(reference);
    hl_worker_destroy(&worker);
    piece_snapshot_release(before);
    piece_snapshot_release(after);
    free(model);
    return 0;
}
