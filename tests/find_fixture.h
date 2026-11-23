#ifndef TESTS_FIND_FIXTURE_H
#define TESTS_FIND_FIXTURE_H

#include "find/find.h"

/* Own the reserve through the snapshot's last release. Reverse insertion
 * keeps each requested run separate by reversing its ADD coordinates. */
typedef struct find_fixture {
    piece_reserve *reserve;
    piece_snapshot *snapshot;
} find_fixture;

static inline find_fixture find_fixture_snapshot(const uint8_t *bytes, size_t length,
                                                const size_t *cuts, size_t count)
{
    find_fixture f;
    piece_tree *tree;
    size_t first = count ? cuts[0] : length;
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_copy(f.reserve, bytes, first, &tree) == PIECE_OK);
    for (size_t i = count; i > 0; i--) {
        size_t end = i == count ? length : cuts[i];
        ASSERT(piece_replace(tree, first, 0, bytes + cuts[i - 1], end - cuts[i - 1]) == PIECE_OK);
    }
    ASSERT(piece_snapshot_take(tree, &f.snapshot) == PIECE_OK);
    piece_close(tree);
    return f;
}

static inline find_source find_fixture_source(find_fixture f)
{
    return (find_source){.view = piece_snapshot_view(f.snapshot)};
}

static inline size_t find_fixture_spans(find_fixture f)
{
    piece_iter it;
    piece_iter_init(&it, piece_snapshot_view(f.snapshot), 0);
    const uint8_t *bytes;
    size_t count = 0;
    while (piece_iter_next(&it, &bytes)) count++;
    return count;
}

static inline void find_fixture_release(find_fixture f)
{
    piece_snapshot_release(f.snapshot);
    piece_reserve_destroy(f.reserve);
}

/* Test variant of literal.c. Forced Two-Way applies only to this invocation,
 * with the same public source, options, cancellation and result contract. */
find_code find_test_literal_two_way(const find_source *source, const uint8_t *needle,
                                   size_t n, find_literal_options options,
                                   const find_control *control, find_result *result);

#endif
