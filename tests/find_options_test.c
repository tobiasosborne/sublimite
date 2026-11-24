#include "find_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "find_options_test:%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static bool ascii_word(uint8_t byte)
{
    return byte == '_' || (byte >= '0' && byte <= '9') ||
           (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

static uint8_t ascii_lower(uint8_t byte)
{
    return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte - 'A' + 'a') : byte;
}

static bool equal_bytes(const uint8_t *text, const uint8_t *needle, size_t n, bool fold)
{
    for (size_t i = 0; i < n; i++)
        if (fold ? ascii_lower(text[i]) != ascii_lower(needle[i]) : text[i] != needle[i])
            return false;
    return true;
}

static find_fixture_result oracle(const uint8_t *text, size_t len, const uint8_t *needle, size_t n,
                          find_literal_options options)
{
    find_fixture_result result = {0};
    if (!n || n > len) return result;
    for (size_t i = 0; i <= len - n;) {
        bool word = (!i || !ascii_word(text[i - 1])) &&
                    (i + n == len || !ascii_word(text[i + n]));
        if ((!options.whole_word || word) && equal_bytes(text + i, needle, n, options.fold_ascii)) {
            result.offsets[result.stored++] = i;
            result.total++;
            i += n;
        } else i++;
    }
    return result;
}

typedef struct visits {
    find_capture ranges[FIND_FIXTURE_OFFSETS];
    uint64_t ordinals[FIND_FIXTURE_OFFSETS];
    size_t count;
} visits;

static bool capture(void *data, uint64_t ordinal, find_capture range)
{
    visits *log = data;
    log->ordinals[log->count] = ordinal;
    log->ranges[log->count++] = range;
    return true;
}

static int check_options(const find_source *source, const uint8_t *text, size_t len,
                         const uint8_t *needle, size_t n, find_literal_options options)
{
    find_fixture_result want = oracle(text, len, needle, n, options), got;
    CHECK(find_fixture_literal(source, needle, n, options, NULL, &got) == FIND_OK);
    if (got.total != want.total)
        fprintf(stderr, "fold=%d word=%d len=%zu needle=%zu actual=%llu expected=%llu\n",
                options.fold_ascii, options.whole_word, len, n,
                (unsigned long long)got.total, (unsigned long long)want.total);
    CHECK(got.total == want.total && got.stored == want.stored);
    CHECK(!memcmp(got.offsets, want.offsets, got.stored * sizeof got.offsets[0]));

    /* (P) Retain the first match, the last, and one intersection with the
     * middle third. Several selection rules may name the same ordinal. */
    visits log = {0};
    find_visit visit = {.prefix_capacity = 1, .visible_capacity = 1,
        .wanted = want.total ? want.total - 1 : FIND_UNSET,
        .window_start = len / 3, .window_end = len - len / 3,
        .emit = capture, .user = &log};
    CHECK(find_literal_visit(source, needle, n, options, NULL, &visit) == FIND_OK);
    CHECK(visit.total == want.total);
    uint64_t from = 0, visible = 0;
    size_t delivered = 0;
    for (size_t i = 0; i < want.stored; i++) {
        uint64_t at = want.offsets[i];
        find_match match;
        CHECK(find_literal_next(source, needle, n, options, from, NULL, &match) == FIND_OK);
        CHECK(match.matched && match.whole.start == at && match.whole.end == at + n);
        from = match.whole.end;
        bool shown = visit.window_start < visit.window_end &&
                     at < visit.window_end && at + n > visit.window_start;
        if (i < visit.prefix_capacity || i == visit.wanted ||
            (shown && visible < visit.visible_capacity)) {
            CHECK(delivered < log.count && log.ordinals[delivered] == i);
            CHECK(log.ranges[delivered].start == at && log.ranges[delivered].end == at + n);
            delivered++;
        }
        visible += shown;
    }
    find_match match;
    CHECK(find_literal_next(source, needle, n, options, from, NULL, &match) == FIND_OK);
    CHECK(!match.matched && match.whole.start == FIND_UNSET);
    CHECK(delivered == log.count && visible == visit.visible);
    return 0;
}

static int fixture(const uint8_t *text, size_t len, const uint8_t *needle, size_t n,
                   size_t folded_total, size_t word_total)
{
    find_literal_options folded = {.fold_ascii = true};
    find_literal_options word = {.fold_ascii = true, .whole_word = true};
    CHECK(oracle(text, len, needle, n, folded).total == folded_total);
    CHECK(oracle(text, len, needle, n, word).total == word_total);

    piece_reserve *reserve;
    piece_tree *tree;
    piece_snapshot *snapshot;
    CHECK(piece_reserve_create(&reserve) == PIECE_OK);
    CHECK(piece_open_copy(reserve, NULL, 0, &tree) == PIECE_OK);
    /* (P) Inserting each byte at the front in reverse gives adjacent logical
     * bytes descending ADD coordinates, so no two spans can coalesce. */
    for (size_t i = len; i > 0; i--)
        CHECK(piece_replace(tree, 0, 0, text + i - 1, 1) == PIECE_OK);
    CHECK(piece_snapshot_take(tree, &snapshot) == PIECE_OK);
    piece_iter it;
    piece_iter_init(&it, piece_snapshot_view(snapshot), 0);
    const uint8_t *p;
    for (size_t i = 0; i < len; i++)
        CHECK(piece_iter_next(&it, &p) == 1 && *p == text[i]);
    CHECK(piece_iter_next(&it, &p) == 0);

    const find_source sources[] = {
        {.bytes = text, .length = len}, {.view = piece_tree_view(tree)},
        {.view = piece_snapshot_view(snapshot)}
    };
    int failed = 0;
    for (size_t v = 0; v < sizeof sources / sizeof sources[0]; v++)
        for (unsigned fold = 0; fold <= 1; fold++)
            for (unsigned whole = 0; whole <= 1; whole++)
                failed |= check_options(&sources[v], text, len, needle, n,
                                        (find_literal_options){fold != 0, whole != 0});
    piece_snapshot_release(snapshot);
    piece_close(tree);
    piece_reserve_destroy(reserve);
    return failed;
}

int main(void)
{
    /* (G) v1 findui_test.c toggles and whole_word_count_worker. The first
     * binary query has right word neighbours. Reject the first overlapping
     * "a a" and accept the second at the space-delimited tail. */
    const uint8_t binary[] = {'a', 0, 'A', 0, 'a', 'a', ' '};
    const uint8_t binary_needle[] = {'a', 0};
    int failed = fixture(binary, sizeof binary, binary_needle, sizeof binary_needle, 2, 0);
    failed |= fixture(binary, sizeof binary, binary_needle, 1, 4, 2);
    const uint8_t overlap[] = "xa a a";
    failed |= fixture(overlap, sizeof overlap - 1, (const uint8_t *)"a a", 3, 1, 1);
    /* (P) Fold leaves the high byte alone. The first two 'ab' candidates
     * have word neighbours on both sides. The third reaches EOF. */
    const uint8_t mixed[] = "_Ab9 1aB_ \xff" "AB";
    failed |= fixture(mixed, sizeof mixed - 1, (const uint8_t *)"ab", 2, 3, 1);
    /* (P) The empty literal is never a match, even with word checking. */
    failed |= fixture(mixed, sizeof mixed - 1, NULL, 0, 0, 0);
    if (failed) return 1;
    puts("find_options_test: fold/word oracle and literal result/visit/next agree");
    return 0;
}
