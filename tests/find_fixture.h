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

/* Collect the same bounded prefix and total through the public visitors.
 * The inherited fixture capacity also drives the dense-match tests. */
#define FIND_FIXTURE_OFFSETS 4096u
typedef struct find_fixture_result {
    uint64_t total;
    size_t stored;
    uint64_t offsets[FIND_FIXTURE_OFFSETS];
} find_fixture_result;

static inline bool find_fixture_emit(void *user, uint64_t ordinal, find_capture range)
{
    find_fixture_result *result = user;
    ASSERT(ordinal < FIND_FIXTURE_OFFSETS && ordinal == result->stored);
    result->offsets[result->stored++] = range.start;
    return true;
}

static inline bool find_fixture_emit_mask(void *user, uint64_t mask, uint64_t base, uint64_t ordinal)
{
    find_fixture_result *result = user;
    ASSERT(ordinal >= result->stored);
    while (mask && result->stored < FIND_FIXTURE_OFFSETS) {
        result->offsets[result->stored++] = base + (unsigned)__builtin_ctzll(mask);
        mask &= mask - 1;
    }
    return true;
}

/* The find test object forces Two-Way for this visitor call only. */
find_code find_test_literal_visit_two_way(const find_source *source, const uint8_t *needle,
                                         size_t n, find_literal_options options,
                                         const find_control *control, find_visit *visit);
typedef find_code (*find_fixture_literal_fn)(const find_source *, const uint8_t *, size_t,
    find_literal_options, const find_control *, find_visit *);

static inline find_code find_fixture_literal_run(find_fixture_literal_fn run,
    const find_source *source, const uint8_t *needle, size_t n,
    find_literal_options options, const find_control *control, find_fixture_result *result)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->stored = 0;
    find_visit visit = {.prefix_capacity = FIND_FIXTURE_OFFSETS, .wanted = FIND_UNSET,
                        .emit = find_fixture_emit, .emit_mask = find_fixture_emit_mask, .user = result};
    find_code code = run(source, needle, n, options, control, &visit);
    result->total = visit.total;
    if (code != FIND_OK) result->stored = 0;
    return code;
}

static inline find_code find_fixture_literal(const find_source *source, const uint8_t *needle,
    size_t n, find_literal_options options, const find_control *control, find_fixture_result *result)
{
    return find_fixture_literal_run(find_literal_visit, source, needle, n, options, control, result);
}

static inline find_code find_fixture_literal_two_way(const find_source *source, const uint8_t *needle,
    size_t n, find_literal_options options, const find_control *control, find_fixture_result *result)
{
    return find_fixture_literal_run(find_test_literal_visit_two_way, source, needle, n, options, control, result);
}

static inline find_code find_fixture_regex(const find_source *source, const find_regex *regex,
    void *scratch, size_t size, const find_control *control, find_fixture_result *result)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->stored = 0;
    find_visit visit = {.prefix_capacity = FIND_FIXTURE_OFFSETS, .wanted = FIND_UNSET,
                        .emit = find_fixture_emit, .user = result};
    find_code code = find_regex_visit(source, regex, scratch, size, control, &visit);
    result->total = visit.total;
    if (code != FIND_OK) result->stored = 0;
    return code;
}

static inline find_code find_fixture_compile(void *program, size_t size, const uint8_t *query,
    size_t length, find_regex **out, size_t *error)
{
    return find_regex_compile_options(program, size, query, length, false, NULL, 0, NULL, out, error);
}

static inline find_code find_fixture_regex_next(const find_source *source, const find_regex *regex,
    uint64_t off, void *scratch, size_t size, const find_control *control, find_match *match)
{
    uint64_t length = source ? source->length : 0;
    if (source && source->view) length = piece_length(source->view);
    uint64_t budget = find_regex_work_budget(regex, length);
    return find_regex_next_budget(source, regex, off, scratch, size, control, &budget, match);
}

#endif
