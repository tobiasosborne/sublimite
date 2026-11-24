#include "base/base.h"
#include "undo/undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void edit(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes, size_t len, bool join)
{
    const undo_state state = {0};
    ASSERT(undo_begin(u, &state, join) == UNDO_OK);
    ASSERT(undo_replace(u, off, del, bytes, len) == UNDO_OK);
    ASSERT(undo_commit(u, &state) == UNDO_OK);
}

static void replay(undo_log *u, undo_replay_direction direction, size_t spans)
{
    const undo_state *target;
    ASSERT(undo_replay_prepare(u, direction, OS_DEADLINE_NEVER, &target) == UNDO_OK && target);
    undo_splice_iter it;
    undo_prepared_splice splice;
    undo_splice_iter_init(&it, u);
    ASSERT(undo_splice_iter_next(&it, &splice) && splice.span_count == spans);
    for (size_t i = 0; i < spans; i++) ASSERT(undo_splice_span(&splice, i).len != 0);
    ASSERT(!undo_splice_iter_next(&it, &splice));
    ASSERT(undo_commit(u, target) == UNDO_OK);
}

int main(void)
{
    /* (P) The full delete crosses the eight-inline boundary with nine
     * eligible runs. The extra trailing byte keeps the final range at equality. */
    enum { cuts = 4, width = sizeof(piece_span) };
    uint8_t original[(cuts + 1) * width + 1], run[width];
    memset(original, 'o', sizeof original);
    memset(run, 'a', sizeof run);
    piece_reserve *r;
    piece_tree *t;
    undo_log *u;
    ASSERT(piece_reserve_create(&r) == PIECE_OK);
    ASSERT(piece_open_copy(r, original, sizeof original, &t) == PIECE_OK);
    ASSERT(undo_create(&u, t, SIZE_MAX, NULL, NULL) == UNDO_OK);

    base_malloc_guard_begin();
    /* (P) Two adjacent typed keys take the burst-extension path.
     * Remove both so the fragmented fixture retains its exact width. */
    const uint8_t key = 'x';
    edit(u, sizeof original, 0, &key, 1, false);
    edit(u, sizeof original + 1, 0, &key, 1, true);
    ASSERT(undo_get_stats(u).records == 1);
    edit(u, sizeof original, 2, NULL, 0, false);
    for (size_t i = 1; i <= cuts; i++) {
        edit(u, (i * 2 - 1) * width, 0, run, width, false);
    }
    /* (P) One trailing-byte deletion preserves all nine runs. */
    const uint64_t len = piece_length(piece_tree_view(t));
    edit(u, len - 1, 1, NULL, 0, false);
    edit(u, 0, len - 1, NULL, 0, false);
    replay(u, UNDO_REPLAY_UNDO, 2 * cuts + 1);
    replay(u, UNDO_REPLAY_REDO, 0);
    size_t calls = base_malloc_guard_end();

    ASSERT(undo_get_stats(u).span_payload_bytes != 0);
    bool active = base_malloc_guard_active();
    printf("law2_undo_test: active=%d count=%zu\n", (int)active, calls);
    undo_destroy(u);
    piece_close(t);
    piece_reserve_destroy(r);
    return active && calls == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
