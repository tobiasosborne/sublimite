#include "base/base.h"
#include "piece/piece.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t initial[] = "a\n";
    const uint8_t newline = '\n';
    const uint8_t letter = 'x';
    piece_reserve *r;
    piece_tree *t;
    piece_err err = piece_reserve_create(&r);
    ASSERT(err == PIECE_OK);
    err = piece_open_copy(r, initial, sizeof initial - 1, &t);
    ASSERT(err == PIECE_OK);
    err = piece_replenish(t);
    ASSERT(err == PIECE_OK);
    const piece_view *v = piece_tree_view(t);

    /* Cold ADD admission, a provisional take read through the span reader
     * and released, abort and in-place regrowth, all inside the release
     * guard (D1'). The original text has two bytes and one LF. */
    base_malloc_guard_begin();
    err = piece_replace(t, piece_length(v), 0, &letter, 1);
    ASSERT(err == PIECE_OK);
    err = piece_checkpoint_begin(t);
    ASSERT(err == PIECE_OK);
    err = piece_replace(t, piece_length(v), 0, &newline, 1);
    ASSERT(err == PIECE_OK);
    piece_snapshot *inside;
    err = piece_snapshot_take(t, &inside);
    ASSERT(err == PIECE_OK);
    uint64_t count;
    const piece_view *saved = piece_snapshot_view(inside);
    bool exact = piece_lines_before(saved, piece_length(saved), &count);
    uint8_t add[2];
    size_t written = 0;
    piece_read_result read = piece_snapshot_read_span(inside, (piece_span){PIECE_SPAN_ADD, 2}, 0, add,
                                                      sizeof add, &written, NULL, NULL);
    bool isolated = exact && count == 2 && piece_byte_at(saved, piece_length(saved) - 1) == newline &&
                    read == PIECE_READ_OK && written == 2 && add[0] == letter && add[1] == newline;
    piece_snapshot_release(inside);
    piece_checkpoint_abort(t);
    err = piece_replace(t, piece_length(v), 0, &letter, 1);
    ASSERT(err == PIECE_OK);
    err = piece_replace(t, piece_length(v) - 1, 1, NULL, 0);
    ASSERT(err == PIECE_OK);
    /* The live text is "a\nx" again, and ADD [1, 2) was regrown in place
     * with a byte the text no longer shows. */
    piece_snapshot *now;
    err = piece_snapshot_take(t, &now);
    ASSERT(err == PIECE_OK);
    read = piece_snapshot_read_span(now, (piece_span){PIECE_SPAN_ADD | 1u, 1}, 0, add, sizeof add, &written,
                                    NULL, NULL);
    bool regrown = read == PIECE_READ_OK && written == 1 && add[0] == letter && piece_add_length(t) == 2 &&
                   piece_length(v) == 3 && piece_lines_before(v, 3, &count) && count == 1 &&
                   piece_byte_at(v, 2) == letter;
    piece_snapshot_release(now);
    size_t allocations = base_malloc_guard_end();

    bool active = base_malloc_guard_active();
    printf("piece Law-2 window (G): active=%d count=%zu\n", (int)active, allocations);
    piece_close(t);
    piece_reserve_destroy(r);
    return !active || allocations != 0 || !isolated || !regrown;
}
