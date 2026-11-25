/* Real tree checks for the signed geometry, admission and physical ledger. */
#include "piece_harness.h"
#include "piece/piece_test.h"
#include <sched.h>

#define REQUIRE(c) do { if (!(c)) { \
    PH_CHECK(false, "%s:%d: %s", __func__, __LINE__, #c); return; \
} } while (0)

static void shape_limits(void)
{
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    const piece_view *v = piece_tree_view(t);

    /* (P) A leftmost split freezes 12 entries on the right. The new
     * root starts with two children and takes 15 more to overflow. */
    const unsigned first_split = 24u + 1u;
    const unsigned second_split = first_split + (16u - 1u) * 12u;
    const uint8_t byte = 'x';
    piece_test_set_height_limit(0);
    for (unsigned i = 1; i < first_split; i++)
        REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 0);
    REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_ERR_RANGE);
    REQUIRE(piece_length(v) == first_split - 1u);
    piece_test_set_height_limit(1);
    for (unsigned i = first_split; i < second_split; i++) {
        REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_OK);
        REQUIRE(piece_test_measure(t).height == 1);
    }
    REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_ERR_RANGE);
    REQUIRE(piece_add_length(t) == second_split - 1u);
    piece_test_set_height_limit(PIECE_MAX_HEIGHT);
    REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 2);

    piece_snapshot *before;
    REQUIRE(piece_snapshot_take(t, &before) == PIECE_OK);
    REQUIRE(piece_replace(t, 0, second_split - 1u, NULL, 0) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 0 && piece_byte_at(v, 0) == byte);
    REQUIRE(piece_length(piece_snapshot_view(before)) == second_split);
    piece_snapshot_release(before);
    piece_close(t);
    piece_reserve_destroy(r);
    printf("shape (P): height 1 at %u and height 2 at %u prepends\n", first_split, second_split);
}

static void packed_interior_delete(void)
{
    /* (P) Sixteen full leaves in the first branch, one full leaf in
     * the second. Splitting the first leaf therefore splits its parent. */
    const size_t chunks = (16u + 1u) * 24u;
    const size_t n = chunks * PIECE_CHUNK;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    ph_mapping map;
    REQUIRE(ph_map_open(&map, bytes, n));
    free(bytes);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_test_stats before = piece_test_measure(t);
    REQUIRE(before.height == 2 && before.root_entries == 2 && before.live_slots == 17u + 2u + 1u);
    REQUIRE(before.mapped_reads == 0);
    REQUIRE(piece_replace(t, 1, 1, NULL, 0) == PIECE_OK);
    piece_test_stats after = piece_test_measure(t);
    REQUIRE(after.height == before.height && after.root_entries == before.root_entries + 1u);
    REQUIRE(after.live_slots == before.live_slots + 2u && after.mapped_reads == 0);
    const piece_view *v = piece_tree_view(t);
    REQUIRE(piece_length(v) == n - 1u);
    piece_iter it;
    piece_span span;
    piece_iter_init(&it, v, 1);
    REQUIRE(piece_iter_next_span(&it, piece_length(v), &span, NULL));
    REQUIRE(span.src == 2 && span.len == n - 2u);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    printf("packed delete (P): %zu descriptors, root entries %u to %u, live slots %zu to %zu\n",
           chunks, before.root_entries, after.root_entries, before.live_slots, after.live_slots);
}

/* A20: v1's version_headers fixture (tests/piece_mem_test.c at the pin),
 * gated by v2's own exact derivation. Every number below is (P) from the
 * signed geometry (24-entry leaves, 16-child branches, 400 B slots, ten per
 * 4,096 B page) unless tagged otherwise. */
static void delete_at_limit(void)
{
    /* The signed counter-example at the root: 16 x 24 = 384 chunks pack
     * into 16 full leaves under one full root (P). Deleting one interior
     * byte of the first chunk splits its leaf and then the root, a root
     * increase with zero descriptors inserted: RANGE at limit 1 with
     * nothing changed, OK at height 2 once the limit allows it. */
    const size_t chunks = 16u * 24u;
    const size_t n = chunks * PIECE_CHUNK;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    ph_mapping map;
    REQUIRE(ph_map_open(&map, bytes, n));
    free(bytes);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_test_stats before = piece_test_measure(t);
    REQUIRE(before.height == 1 && before.root_entries == 16u);
    piece_test_set_height_limit(1);
    piece_err err = piece_replace(t, 1, 1, NULL, 0);
    piece_test_stats refused = piece_test_measure(t);
    uint64_t length = piece_length(piece_tree_view(t));
    /* A deletion that only trims an entry's end still fits. */
    piece_err fits = piece_replace(t, 0, 1, NULL, 0);
    piece_test_set_height_limit(PIECE_MAX_HEIGHT);
    REQUIRE(err == PIECE_ERR_RANGE && refused.revision == before.revision &&
            refused.live_slots == before.live_slots && length == n);
    REQUIRE(fits == PIECE_OK && piece_test_measure(t).height == 1);
    REQUIRE(piece_replace(t, 1, 1, NULL, 0) == PIECE_OK);
    piece_test_stats grown = piece_test_measure(t);
    REQUIRE(grown.height == 2 && grown.root_entries == 2u && grown.mapped_reads == 0);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    printf("delete at limit (P): interior delete in a full height-1 path refused at limit 1, "
           "height 2 above it\n");
}

static void version_headers(void)
{
    enum { PREPENDS = 10000, VERSIONS = 5000 };
    piece_reserve *r;
    piece_tree *t;
    piece_snapshot *snapshots[VERSIONS];
    const uint8_t byte = 'x';
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    for (unsigned i = 0; i < PREPENDS; i++)
        REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_OK);
    /* Left-spine prepends of 10,000 uncoalescable one-byte runs: a level
     * of capacity c needs 1 node up to c entries, then 1 + ceil((m - c) /
     * (c / 2)) (spec "Choosing the slot"). Leaves 1 + ceil(9,976 / 12) =
     * 833; branches 1 + ceil(817 / 8) = 104, 1 + ceil(88 / 8) = 12, root 1. */
    const size_t built_slots = 833u + 104u + 12u + 1u;
    piece_test_stats built = piece_test_measure(t);
    REQUIRE(built.height == 3 && built.live_slots == built_slots && built_slots == 950u);
    /* Each held version: a header, then a one-byte insert at 1 + i that
     * extends the leading ADD run and copies its leaf-to-root path. */
    for (unsigned i = 0; i < VERSIONS; i++) {
        REQUIRE(piece_snapshot_take(t, &snapshots[i]) == PIECE_OK);
        REQUIRE(piece_replace(t, 1u + i, 0, &byte, 1) == PIECE_OK);
    }
    REQUIRE(piece_replenish(t) == PIECE_OK);
    piece_test_stats s = piece_test_measure(t);
    REQUIRE(ph_count_spans(piece_tree_view(t), 0, PREPENDS + VERSIONS) == PREPENDS);
    REQUIRE(s.page == 4096u && s.slots_per_extent == 10u);

    /* Live slots, EXACT: 950 + 5,000 x (4 path slots + 1 header). */
    const size_t slots = 25950u;
    REQUIRE(built_slots + VERSIONS * (4u + 1u) == slots && s.live_slots == slots);
    /* Node and header extents: 25,950 / 10 = 2,595 full pages. */
    const size_t nodes = 2595u * 4096u;
    REQUIRE(s.extents == 2595u && nodes == 10629120u);
    /* ADD: 15,000 bytes in chunk 0; a chunk is 24 B header + 4 B total +
     * 128 x 2 B block prefixes + 65,536 B = 65,820 B, one mapping of
     * ceil(65,820 / 4,096) = 17 pages. */
    const size_t add = 17u * 4096u;
    REQUIRE(s.add_chunks == 1u && s.chunk_pages == 17u && add == 69632u);
    /* ADD directory: chunk 0 is its own root, no radix node. */
    const size_t directory = 0;
    REQUIRE(s.add_dirs == 0);
    /* Fixed descriptors, one base mapping each, each under a page: the
     * core, the tree, the process reserve. */
    const size_t core = 4096u, tree = 4096u, reserve_descriptor = 4096u;
    REQUIRE(s.core_pages == 1u && s.tree_pages == 1u && s.reserve_pages == 1u);
    /* Reserve target after replenish: W(3) = 1 + (4 x 3 + 3) + (2 x 4 + 3)
     * + 1 = 28 slots; the core's pages are full (25,950 mod 10 = 0), so the
     * reserve keeps ceil(28 / 10) = 3 whole extents and nothing else (D1':
     * no spare boundary). */
    const size_t reserve = 3u * 4096u;
    REQUIRE(s.free_extents == 3u && reserve == 12288u);
    const size_t total = nodes + add + directory + core + tree + reserve_descriptor + reserve;
    REQUIRE(total == 10723328u);
    /* (G) The signed outer bound, two potential extra pieces per insert:
     * 65,536 + 4,096 + 96 x 20,000 + 1.25 x 15,000 + 1,920 x 5,000. */
    const size_t outer = 65536u + 4096u + 96u * (PREPENDS + 2u * VERSIONS) +
                         5u * (PREPENDS + VERSIONS) / 4u + 1920u * VERSIONS;
    REQUIRE(outer == 11608382u && total <= outer);
    /* Comparison only (A20): v1's runtime formula at the actual 10,000
     * pieces; v2's node + ADD mappings are 0.47 % above it. */
    const size_t v1 = 65536u + 4096u + 96u * PREPENDS + 5u * (PREPENDS + VERSIONS) / 4u +
                      1920u * VERSIONS;
    printf("version_headers (P): slots=%zu nodes=%zu ADD=%zu directory=%zu core+tree+reserve "
           "descriptors=%zu reserve target=%zu total=%zu; outer (G)=%zu; v1 formula=%zu (comparison)\n",
           slots, nodes, add, directory, core + tree + reserve_descriptor, reserve, total, outer, v1);
    for (unsigned i = 0; i < VERSIONS; i++) {
        const piece_view *v = piece_snapshot_view(snapshots[i]);
        REQUIRE(piece_length(v) == PREPENDS + i && piece_byte_at(v, i) == byte);
        piece_snapshot_release(snapshots[i]);
    }
    piece_close(t);
    piece_reserve_destroy(r);
}

static void release_on_worker(void *ctx)
{
    piece_snapshot_release(ctx);
}

static void extents_follow_owners(void)
{
    piece_reserve *r;
    piece_tree *a, *b;
    piece_snapshot *hold;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &a) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &b) == PIECE_OK);
    REQUIRE(piece_snapshot_take(a, &hold) == PIECE_OK);
    piece_test_stats s = piece_test_measure(b);
    REQUIRE(piece_test_measure(a).extents == 1 && s.extents == 1 && s.free_extents == 0);
    piece_close(a);
    /* (P) b's empty root already occupies one of its page's slots. A
     * different core's partly free page is usable only after its final owner. */
    ph_refuse(true);
    bool funded = piece_test_fund_slots(b, s.slots_per_extent);
    ph_refuse(false);
    REQUIRE(!funded);

    os_thread worker;
    REQUIRE(os_thread_start(&worker, release_on_worker, hold) == OS_OK);
    REQUIRE(os_thread_join(&worker) == OS_OK);
    size_t calls = ph_calls();
    ph_refuse(true);
    funded = piece_test_fund_slots(b, s.slots_per_extent);
    ph_refuse(false);
    REQUIRE(funded && ph_calls() == calls);
    s = piece_test_measure(b);
    REQUIRE(s.extents == 1 && s.free_extents == 1);
    piece_close(b);
    piece_reserve_destroy(r);
}

static void add_directory_rollback(void)
{
    /* (P) The 65th chunk requires a second radix level (64 pointers
     * per node). One byte into it makes the checkpoint mark unaligned. */
    const size_t n = 64u * PIECE_CHUNK + 1u;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    bytes[n - 1] = '\n';
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    REQUIRE(piece_replace(t, 0, 0, bytes, n) == PIECE_OK);
    free(bytes);
    const piece_view *v = piece_tree_view(t);
    REQUIRE(piece_replenish(t) == PIECE_OK);

    piece_snapshot *before;
    REQUIRE(piece_snapshot_take(t, &before) == PIECE_OK);
    size_t calls = ph_calls();
    ph_refuse(true);
    piece_err err = piece_checkpoint_begin(t);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && ph_calls() == calls);
    const uint8_t lf = '\n';
    REQUIRE(piece_replace(t, n, 0, &lf, 1) == PIECE_OK);

    /* The inside header is funded by the replenished slots: no base call.
     * D1': it is provisional, read and released before the abort. */
    calls = ph_calls();
    ph_refuse(true);
    piece_snapshot *inside;
    err = piece_snapshot_take(t, &inside);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && ph_calls() == calls);
    const piece_view *in = piece_snapshot_view(inside);
    uint64_t count = 0;
    REQUIRE(piece_lines_before(in, piece_length(in), &count) && count == 2 && piece_byte_at(in, n) == '\n');
    piece_snapshot_release(inside);

    /* Abort restores the unaligned mark's tail and newline total without a
     * directory walk; regrowth reuses the restored tail in place. */
    calls = ph_calls();
    ph_refuse(true);
    piece_checkpoint_abort(t);
    const uint8_t letter = 'x';
    err = piece_replace(t, n, 0, &letter, 1);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && ph_calls() == calls);
    uint64_t rank = 0, saved_rank = 0;
    REQUIRE(piece_byte_at(v, n) == letter && piece_lines_before(v, n + 1u, &rank) && rank == 1);
    const piece_view *saved = piece_snapshot_view(before);
    REQUIRE(piece_length(saved) == n && piece_lines_before(saved, n, &saved_rank) && saved_rank == 1);
    piece_snapshot_release(before);
    piece_close(t);
    piece_reserve_destroy(r);
}

static void large_replay(void)
{
    /* (G) A19's 513 occupied chunks. One LF per chunk plus an EOF LF
     * distinguishes last-line selection and the short exact prefix. */
    const size_t chunks = 513;
    const size_t n = chunks * PIECE_CHUNK;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    for (size_t i = 0; i < n; i += PIECE_CHUNK) bytes[i] = '\n';
    bytes[n - 1] = '\n';
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, n, 1, &map));
    free(bytes);
    const piece_view *v = piece_tree_view(st.t);
    REQUIRE(ph_lines_complete(st.t, &map, "513-chunk adopt"));
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "before large delete");

    piece_iter it;
    piece_span span;
    piece_span_nl counts;
    piece_iter_init(&it, v, 0);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_iter_next_span(&it, n, &span, &counts) && span.len == n);
    piece_span extra;
    REQUIRE(!piece_iter_next_span(&it, n, &extra, NULL));
    REQUIRE(piece_replace(st.t, 0, n, NULL, 0) == PIECE_OK);
    REQUIRE(pm_replace(st.m, 0, n, NULL, 0) == PIECE_OK);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 0);
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "between large delete and undo");

    piece_snapshot *empty;
    REQUIRE(piece_snapshot_take(st.t, &empty) == PIECE_OK);
    /* (P) The signed batches at EOF allocate 24 leaves, two parents
     * and a root. All are new, since the snapshot pins the empty leaf. */
    const size_t claim = 24u + 2u + 1u;
    REQUIRE(piece_test_fund_slots(st.t, claim));
    size_t held = piece_test_measure(st.t).available_slots - (claim - 1u);
    for (size_t i = 0; i < held; i++) piece_test_hold_slot(st.t);
    REQUIRE(piece_test_measure(st.t).available_slots == claim - 1u);
    size_t calls = ph_calls();
    ph_refuse(true);
    piece_err err = piece_replace_spans(st.t, 0, 0, &span, &counts, 1);
    ph_refuse(false);
    REQUIRE(err == PIECE_ERR_NOMEM && ph_calls() > calls);
    REQUIRE(piece_length(v) == 0 && piece_add_length(st.t) == 0);
    REQUIRE(piece_test_measure(st.t).slots_claimed == claim);
    piece_snapshot *same;
    REQUIRE(piece_snapshot_take(st.t, &same) == PIECE_OK && same == empty);
    piece_snapshot_release(same);

    piece_test_release_slot(st.t);
    calls = ph_calls();
    piece_test_reset_reads(st.t);
    ph_refuse(true);
    err = piece_replace_spans(st.t, 0, 0, &span, &counts, 1);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && ph_calls() == calls);
    piece_test_stats s = piece_test_measure(st.t);
    REQUIRE(s.slots_claimed == claim && s.available_slots == 0 && s.mapped_reads == 0);
    REQUIRE(piece_length(piece_snapshot_view(empty)) == 0);
    while (--held) piece_test_release_slot(st.t);
    piece_snapshot_release(empty);
    REQUIRE(pm_replace_spans(st.m, 0, 0, &span, NULL, 1) == PIECE_OK);
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "after large undo");
    REQUIRE(piece_add_length(st.t) == 0);
    piece_test_reset_reads(st.t);
    uint64_t count, at;
    REQUIRE(piece_lines_before(v, n - 1u, &count) && count == chunks);
    s = piece_test_measure(st.t);
    REQUIRE(s.query_visits <= s.height + 1u && s.mapped_reads <= PIECE_CHUNK);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_lines_start(v, chunks + 1u, &at, &count) && at == n && count == chunks + 1u);
    s = piece_test_measure(st.t);
    REQUIRE(s.query_visits <= s.height + 1u && s.mapped_reads <= PIECE_CHUNK);
    ph_close(&st);
    printf("bulk replay (P): chunks=%zu exact new slots=%zu, one slot short refused\n", chunks, claim);
}

static void mapped_reads(void)
{
    const size_t half = PIECE_CHUNK / 2u;
    /* (P) Two nontrivial full chunks, so description and deletion each
     * perform two H-byte cuts. The source gap makes undo boundary-aligned. */
    static uint8_t bytes[2u * PIECE_CHUNK];
    for (size_t i = 0; i < sizeof bytes; i += 2) bytes[i] = '\n';
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, sizeof bytes, 1, &map));
    REQUIRE(ph_lines_complete(st.t, &map, "midpoint adopt"));
    const piece_view *v = piece_tree_view(st.t);
    piece_iter it;
    piece_span span;
    piece_span_nl counts;
    piece_iter_init(&it, v, half);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_iter_next_span(&it, half + PIECE_CHUNK, &span, &counts));
    size_t describe = piece_test_measure(st.t).mapped_reads;
    REQUIRE(describe == 2 * half && span.src == half && span.len == PIECE_CHUNK);
    REQUIRE(piece_replace(st.t, half, PIECE_CHUNK, NULL, 0) == PIECE_OK);
    size_t delete_reads = piece_test_measure(st.t).mapped_reads - describe;
    REQUIRE(delete_reads == 2 * half);
    REQUIRE(piece_replace_spans(st.t, half, 0, &span, &counts, 1) == PIECE_OK);
    size_t total = piece_test_measure(st.t).mapped_reads;
    REQUIRE(total == 4 * half && total <= 2u * PIECE_CHUNK); /* (G) A13. */
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "midpoint undo");

    /* Astra Q2.4. Two occurrences of the same chunk. The surviving
     * prefix and suffix could join at the deletion seam. Replacement
     * must insert before that join, or the third cut reads another H. */
    const piece_span duplicate[] = {{0, PIECE_CHUNK}, {0, PIECE_CHUNK}};
    REQUIRE(piece_replace_spans(st.t, 0, piece_length(v), duplicate, NULL, 2) == PIECE_OK);
    const uint8_t lf = '\n';
    piece_test_reset_reads(st.t);
    REQUIRE(piece_replace(st.t, half, PIECE_CHUNK, &lf, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 2 * half);

    piece_test_reset_reads(st.t);
    uint64_t rank, at;
    REQUIRE(piece_lines_before(v, half - 1, &rank) && rank == half / 2u);
    piece_test_stats s = piece_test_measure(st.t);
    REQUIRE(s.mapped_reads <= PIECE_CHUNK && s.query_visits <= s.height + 1u);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_lines_start(v, half / 2u, &at, &rank) && at == half - 1u && rank == half / 2u);
    s = piece_test_measure(st.t);
    REQUIRE(s.mapped_reads <= PIECE_CHUNK && s.query_visits <= s.height + 1u);
    ph_close(&st);
    printf("mapped reads (P): describe=%zu delete=%zu round trip=%zu\n",
           describe, delete_reads, total);
}

static void resolve_misses_and_slices(void)
{
    /* Astra's first trace uses two nearly whole source ends and a cut
     * in a third unresolved chunk. All three remain unknown on edit. */
    static uint8_t bytes[3u * PIECE_CHUNK];
    memset(bytes, '\n', sizeof bytes);
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, sizeof bytes, 1, &map));
    const piece_view *v = piece_tree_view(st.t);
    piece_span span = {1, 2u * PIECE_CHUNK - 2u};
    uint64_t dest = 2u * PIECE_CHUNK + PIECE_CHUNK / 2u;
    piece_test_reset_reads(st.t);
    REQUIRE(piece_replace_spans(st.t, dest, 0, &span, NULL, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 0);
    bool more;
    REQUIRE(piece_lines_resolve(st.t, 1, &more) == PIECE_OK && !more);
    REQUIRE(piece_test_measure(st.t).resolve_visits == 1); /* (P) One leaf root. */
    REQUIRE(pm_replace_spans(st.m, dest, 0, &span, NULL, 1) == PIECE_OK);
    REQUIRE(ph_lines_complete(st.t, &map, "eligible slices after miss"));
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "Astra source ends");

    /* The table already knows this chunk. A partial reinsertion with
     * a non-NULL UNKNOWN record must rewind a completed sweep. */
    piece_span small = {3, PIECE_CHUNK - 6u};
    piece_span_nl stale = {PIECE_NL_UNKNOWN, 0};
    REQUIRE(piece_replace_spans(st.t, 0, 0, &small, &stale, 1) == PIECE_OK);
    REQUIRE(pm_replace_spans(st.m, 0, 0, &small, NULL, 1) == PIECE_OK);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_lines_resolve(st.t, 1, &more) == PIECE_OK && !more);
    piece_test_stats s = piece_test_measure(st.t);
    REQUIRE(s.resolve_visits == 1 && s.mapped_reads == small.len && s.mapped_reads <= PIECE_CHUNK);
    ph_check_lines(v, pm_tree_view(st.m), 1, 2, true, "Astra sweep rewind");
    ph_close(&st);
}

static void resolve_rollback(void)
{
    /* A short final chunk is whole. Install its total, refine a pinned
     * root under admission pressure and restore that root on abort. */
    const uint8_t bytes[] = "a\n";
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, sizeof bytes - 1, 1, &map));
    const piece_view *v = piece_tree_view(st.t);
    piece_snapshot *old;
    REQUIRE(piece_snapshot_take(st.t, &old) == PIECE_OK);
    REQUIRE(piece_checkpoint_begin(st.t) == PIECE_OK);
    uint32_t count;
    REQUIRE(piece_lines_count(&map.b, 0, &count, 1) && count == 1);
    REQUIRE(piece_lines_adopt(st.t, map.b.fault, 0, &count, 1));
    size_t held = piece_test_measure(st.t).available_slots;
    REQUIRE(held != 0);
    for (size_t i = 0; i < held; i++) piece_test_hold_slot(st.t);

    bool more = true;
    uint64_t revision = piece_test_measure(st.t).revision;
    ph_refuse(true);
    piece_err status = piece_lines_resolve(st.t, 1, &more);
    /* D1': an inside take cannot hit the ordinary pin; with no free slot
     * the first inside header is refused and leaves *out NULL. */
    piece_snapshot *cached = old;
    piece_err take = piece_snapshot_take(st.t, &cached);
    ph_refuse(false);
    REQUIRE(status == PIECE_ERR_NOMEM && piece_test_measure(st.t).revision == revision);
    REQUIRE(take == PIECE_ERR_NOMEM && cached == NULL);
    uint64_t rank;
    REQUIRE(!piece_lines_before(v, piece_length(v), &rank) && rank == 0);

    while (held--) piece_test_release_slot(st.t);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_lines_resolve(st.t, 1, &more) == PIECE_OK && !more);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 0);
    REQUIRE(piece_lines_before(v, piece_length(v), &rank) && rank == 1);
    const piece_view *sv = piece_snapshot_view(old);
    REQUIRE(!piece_lines_before(sv, piece_length(sv), &rank) && rank == 0);
    piece_checkpoint_abort(st.t);
    REQUIRE(!piece_lines_before(v, piece_length(v), &rank) && rank == 0);
    REQUIRE(piece_lines_resolve(st.t, 1, &more) == PIECE_OK && !more);
    REQUIRE(piece_lines_before(v, piece_length(v), &rank) && rank == 1);
    piece_snapshot_release(old);
    ph_close(&st);
}

static void private_reference_reads(void)
{
    /* Private stores are counted from their 512 B block prefixes (piece.h
     * Lines, errata 1 and 2): at most 511 B per span end, the end's partial
     * block, and nothing at a chunk or block boundary (P). */
    static uint8_t bytes[2u * PIECE_CHUNK];
    ph_fill(bytes, sizeof bytes, 17);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, bytes, sizeof bytes, &t) == PIECE_OK);
    REQUIRE(piece_replace(t, sizeof bytes, 0, bytes, sizeof bytes) == PIECE_OK);
    const piece_view *v = piece_tree_view(t);
    /* One-chunk spans, ends at chunk offsets lo and hi: (lo mod 512) +
     * (hi mod 512) bytes (P): [1, 65,535): 1 + 511; [3, 65,535): 3 + 511.
     * Cross-chunk spans [3, 65,541): 3 B at the head end, the head piece
     * ending on the chunk boundary, 5 B at the tail end (P). */
    const struct { piece_span span; size_t reads; } cases[] = {
        {{1, PIECE_CHUNK - 2u}, 1u + 511u},
        {{PIECE_CHUNK + 3u, PIECE_CHUNK - 4u}, 3u + 511u},
        {{PIECE_SPAN_ADD + 1u, PIECE_CHUNK - 2u}, 1u + 511u},
        {{PIECE_SPAN_ADD + PIECE_CHUNK + 3u, PIECE_CHUNK - 4u}, 3u + 511u},
        {{3, PIECE_CHUNK + 2u}, 3u + 5u},
        {{PIECE_SPAN_ADD + 3u, PIECE_CHUNK + 2u}, 3u + 5u},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        piece_test_reset_reads(t);
        REQUIRE(piece_replace_spans(t, piece_length(v), 0, &cases[i].span, NULL, 1) == PIECE_OK);
        piece_test_stats s = piece_test_measure(t);
        REQUIRE(s.mapped_reads == 0 && s.private_reads == cases[i].reads && s.private_reads <= 2u * 511u);
    }
    piece_close(t);
    piece_reserve_destroy(r);
}

static void private_cut_reads(void)
{
    /* Erratum 2's counter-example: a counted private descriptor starting at
     * chunk offset 511, cut at offset 1,535, counts its left part
     * [511, 1,535) from the prefixes at both ends: 511 + 511 = 1,022 B (P),
     * the bound for a private cut. */
    static uint8_t bytes[PIECE_CHUNK];
    memset(bytes, '\n', sizeof bytes);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, bytes, sizeof bytes, &t) == PIECE_OK);
    REQUIRE(piece_replace(t, 0, 511, NULL, 0) == PIECE_OK); /* the descriptor is [511, 65,536) */
    piece_test_reset_reads(t);
    REQUIRE(piece_replace(t, 1535u - 511u, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    piece_test_stats s = piece_test_measure(t);
    REQUIRE(s.private_reads == 1022u && s.mapped_reads == 0);
    uint64_t rank;
    REQUIRE(piece_lines_before(piece_tree_view(t), 1024u, &rank) && rank == 1024u);
    piece_close(t);
    piece_reserve_destroy(r);
}

/* Leaves exactly `left` free slots in t's core and the reserve (base refused
 * by the caller around the edit), holding the rest; returns how many it held. */
static size_t leave_free(piece_tree *t, size_t left)
{
    size_t available = piece_test_measure(t).available_slots;
    if (available < left) {
        PH_CHECK(piece_test_fund_slots(t, left), "fund %zu slots", left);
        available = piece_test_measure(t).available_slots;
    }
    size_t held = available - left;
    for (size_t i = 0; i < held; i++) piece_test_hold_slot(t);
    PH_CHECK(piece_test_measure(t).available_slots == left, "left %zu free slots", left);
    return held;
}

static void release_held(piece_tree *t, size_t held)
{
    while (held--) piece_test_release_slot(t);
}

/* One edit refused with `left` free slots and base refused: NOMEM, and
 * nothing observable moved (length, ADD length, revision, cache). */
static void refused_edit(piece_tree *t, piece_snapshot *cached, uint64_t off, uint64_t del,
                         const uint8_t *bytes, size_t n, size_t left, size_t want_claim)
{
    const piece_view *v = piece_tree_view(t);
    uint64_t length = piece_length(v), add = piece_add_length(t);
    uint64_t revision = piece_test_measure(t).revision;
    size_t held = leave_free(t, left);
    ph_refuse(true);
    piece_err err = piece_replace(t, off, del, bytes, n);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    release_held(t, held);
    REQUIRE(err == PIECE_ERR_NOMEM && s.slots_claimed == want_claim);
    REQUIRE(piece_length(v) == length && piece_add_length(t) == add && s.revision == revision);
    piece_snapshot *same;
    REQUIRE(piece_snapshot_take(t, &same) == PIECE_OK && same == cached);
    piece_snapshot_release(same);
}

/* Admitted with exactly `left` free slots while base refuses: the scalar
 * claim asks base first (refused), then the exact plan fits. */
static void admitted_edit(piece_tree *t, uint64_t off, uint64_t del, const uint8_t *bytes,
                          size_t n, size_t left, size_t want_claim)
{
    size_t held = leave_free(t, left);
    ph_refuse(true);
    piece_err err = piece_replace(t, off, del, bytes, n);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    release_held(t, held);
    REQUIRE(err == PIECE_OK && s.slots_claimed == want_claim);
}

static void exact_claims(void)
{
    /* (P) 1,000 one-byte prepends give height 2 (the second root growth
     * is at 205). The leftmost leaf holds 13 + (1,000 - 25) mod 12 = 16
     * entries and every other leaf 12, so an edit of the first entry
     * splits and merges nothing. A snapshot pins the root: the edit copies
     * the leaf-to-root path, h + 1 = 3 slots (the exact claim), where the
     * scalar table asks 2h + 3 = 7 to insert and 4h + 3 = 11 to delete. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    const uint8_t byte = 'x', other = 'y';
    for (unsigned i = 0; i < 1000u; i++) REQUIRE(piece_replace(t, 0, 0, &byte, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 2);
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);

    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    refused_edit(t, pin, 0, 0, &other, 1, 2, 3);
    admitted_edit(t, 0, 0, &other, 1, 3, 3);
    REQUIRE(piece_byte_at(piece_tree_view(t), 0) == other);

    piece_snapshot *pin2;
    REQUIRE(piece_snapshot_take(t, &pin2) == PIECE_OK);
    refused_edit(t, pin2, 0, 1, NULL, 0, 2, 3);
    admitted_edit(t, 0, 1, NULL, 0, 3, 3);
    REQUIRE(piece_byte_at(piece_tree_view(t), 0) == byte && piece_length(piece_tree_view(t)) == 1000u);

    /* (P) Exclusive: no owner but the live tree, so deleting the first
     * entry copies nothing and needs zero slots, though 11 cannot be had. */
    piece_snapshot_release(pin);
    piece_snapshot_release(pin2);
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);
    admitted_edit(t, 0, 1, NULL, 0, 0, 0);
    REQUIRE(piece_length(piece_tree_view(t)) == 999u);
    piece_close(t);
    piece_reserve_destroy(r);
    printf("exact claims (P): pinned insert and delete 3 slots (2 refused), exclusive delete 0\n");
}

static void hundred_tabs(void)
{
    /* Spec "Empty-tab cost": each empty core owns one extent (its root);
     * the shared working supply for W(0) = 10 is one more extent:
     * 101 extent mappings x 4,096 B = 413,696 B (P; D1' deleted the spare
     * private boundary chunk). */
    enum { TABS = 100 };
    piece_reserve *r;
    piece_tree *tabs[TABS];
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    for (unsigned i = 0; i < TABS; i++) {
        REQUIRE(piece_open_copy(r, NULL, 0, &tabs[i]) == PIECE_OK);
        piece_test_stats s = piece_test_measure(tabs[i]);
        REQUIRE(s.extents == 1 && s.live_slots == 1 && s.free_extents == 0);
    }
    REQUIRE(piece_replenish(tabs[TABS - 1]) == PIECE_OK);
    piece_test_stats s = piece_test_measure(tabs[TABS - 1]);
    /* (P) The active core's own page has 9 free slots; W(0) - 9 = 1 more
     * slot needs one whole reserve extent. */
    REQUIRE(s.page == 4096u && s.free_extents == 1);
    size_t extents = 0;
    for (unsigned i = 0; i < TABS; i++) extents += piece_test_measure(tabs[i]).extents;
    size_t mapped = (extents + s.free_extents) * s.page;
    REQUIRE(extents == TABS && mapped == 413696u);
    /* Closing returns every extent; the reserve keeps the funded target. */
    for (unsigned i = 0; i + 1 < TABS; i++) piece_close(tabs[i]);
    REQUIRE(piece_test_measure(tabs[TABS - 1]).free_extents == 1);
    piece_close(tabs[TABS - 1]);
    piece_reserve_destroy(r);
    printf("hundred tabs (P): %zu extents + 1 reserve extent = %zu B\n",
           extents, mapped);
}

static void begin_is_constant(void)
{
    /* A7: begin shares the ADD directory; it walks no directory level and
     * reserves nothing, whatever ADD holds. (P) 64 x 65,536 + 1 bytes
     * occupy 65 chunks: 64 pointers per radix node, so a level-2 root over
     * a full level-1 node and one holding the 65th chunk, 3 nodes. */
    const size_t n = 64u * PIECE_CHUNK + 1u;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    piece_test_reset_reads(t);
    REQUIRE(piece_replace(t, 0, 0, bytes, n) == PIECE_OK);
    free(bytes);
    REQUIRE(piece_test_measure(t).add_chunks == 65u && piece_test_measure(t).add_dirs == 3u);
    size_t appended = piece_test_measure(t).dir_steps;
    REQUIRE(appended > 0); /* the counter is live */
    REQUIRE(piece_replenish(t) == PIECE_OK);
    piece_test_reset_reads(t);
    size_t calls = ph_calls();
    ph_refuse(true);
    piece_err err = piece_checkpoint_begin(t);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && ph_calls() == calls && piece_test_measure(t).dir_steps == 0);
    piece_checkpoint_abort(t);
    REQUIRE(piece_test_measure(t).dir_steps == 0);
    piece_close(t);
    piece_reserve_destroy(r);
}

static void lf_every_other(uint8_t *bytes, size_t n)
{
    memset(bytes, 'a', n);
    for (size_t i = 0; i < n; i += 2) bytes[i] = '\n';
}

static void undo_seam(void)
{
    /* Astra Q3.2 and Q1.1. One counted mapped chunk D. Q1.1: an ADD span
     * inserted at D's midpoint reads none of its source and cuts D once,
     * H = 32,768 B (P). Q3.2: describing that byte, deleting it (the
     * survivors are source-adjacent halves of D and join) and undoing it
     * cuts the joined descriptor again: H more, so the round trip reads
     * H <= 4H = 131,072 B (P, A13). */
    const size_t half = PIECE_CHUNK / 2u;
    static uint8_t bytes[PIECE_CHUNK];
    lf_every_other(bytes, sizeof bytes);
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, sizeof bytes, 1, &map));
    REQUIRE(ph_lines_complete(st.t, &map, "seam adopt"));
    const piece_view *v = piece_tree_view(st.t);
    /* A valid ADD span of this tree: one captured byte. */
    piece_span add, model_add;
    REQUIRE(piece_capture(st.t, 1, 1, &add) == PIECE_OK);
    REQUIRE(pm_capture(st.m, 1, 1, &model_add) == PIECE_OK && ph_span_eq(add, model_add));
    piece_test_reset_reads(st.t);
    REQUIRE(piece_replace_spans(st.t, half, 0, &add, NULL, 1) == PIECE_OK);
    REQUIRE(pm_replace_spans(st.m, half, 0, &add, NULL, 1) == PIECE_OK);
    piece_test_stats s = piece_test_measure(st.t);
    REQUIRE(s.mapped_reads == half && s.private_reads <= half);

    piece_iter it;
    piece_span span;
    piece_span_nl counts;
    piece_test_reset_reads(st.t);
    piece_iter_init(&it, v, half);
    REQUIRE(piece_iter_next_span(&it, half + 1u, &span, &counts) && span.src == add.src);
    REQUIRE(piece_replace(st.t, half, 1, NULL, 0) == PIECE_OK);
    REQUIRE(pm_replace(st.m, half, 1, NULL, 0) == PIECE_OK);
    size_t before_undo = piece_test_measure(st.t).mapped_reads;
    REQUIRE(before_undo == 0);
    /* (P) The deletion joined the halves: one descriptor again. */
    REQUIRE(ph_count_spans(v, 0, piece_length(v)) == 1);
    REQUIRE(piece_replace_spans(st.t, half, 0, &span, &counts, 1) == PIECE_OK);
    REQUIRE(pm_replace_spans(st.m, half, 0, &span, NULL, 1) == PIECE_OK);
    size_t total = piece_test_measure(st.t).mapped_reads;
    REQUIRE(total == half && total <= 2u * PIECE_CHUNK);
    ph_check_lines(v, pm_tree_view(st.m), 3, 4, true, "undo seam");
    ph_close(&st);
    printf("undo seam (P): midpoint span insert %zu, describe+delete+undo %zu mapped bytes\n",
           half, total);
}

static void asymmetric_cuts(void)
{
    /* A counted cut scans its shorter half (piece.h Lines, KNOWLEDGE). One
     * counted mapped chunk with an LF at every even offset (P):
     *  - describing [100, 200) with counts cuts at 100 (shorter: 100 B),
     *    then the remaining 65,436 B at 100 (shorter: 100 B): 200 B;
     *  - deleting [100, 101) cuts at 100 (100 B), then the suffix at 1
     *    (1 B): 101 B;
     *  - inserting one byte at 50 cuts [0, 100) at 50: 50 B. */
    static uint8_t bytes[PIECE_CHUNK];
    lf_every_other(bytes, sizeof bytes);
    ph_mapping map;
    ph_state st;
    REQUIRE(ph_open(&st, bytes, sizeof bytes, 1, &map));
    REQUIRE(ph_lines_complete(st.t, &map, "asymmetric adopt"));
    const piece_view *v = piece_tree_view(st.t);
    piece_iter it;
    piece_span span;
    piece_span_nl counts;
    piece_test_reset_reads(st.t);
    piece_iter_init(&it, v, 100);
    REQUIRE(piece_iter_next_span(&it, 200, &span, &counts) && counts.head == 50u);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 200u);
    piece_test_reset_reads(st.t);
    REQUIRE(piece_replace(st.t, 100, 1, NULL, 0) == PIECE_OK);
    REQUIRE(pm_replace(st.m, 100, 1, NULL, 0) == PIECE_OK);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 101u);
    const uint8_t x = 'x';
    piece_test_reset_reads(st.t);
    REQUIRE(piece_replace(st.t, 50, 0, &x, 1) == PIECE_OK);
    REQUIRE(pm_replace(st.m, 50, 0, &x, 1) == PIECE_OK);
    REQUIRE(piece_test_measure(st.t).mapped_reads == 50u);
    ph_check_lines(v, pm_tree_view(st.m), 5, 4, true, "asymmetric cuts");
    ph_close(&st);
}

static void duplicate_adoption(void)
{
    /* Astra Q2.5. 384 chunks pack into 16 full leaves under one root
     * (P, 384 / 24). Only the last chunk is adopted, so the first 15
     * leaves hold ineligible unknown entries. A slice of budget h + 1 = 2
     * visits the root and one leaf. An identical re-adoption before every
     * slice must not rewind the sweep: it ends in exactly 16 slices. */
    enum { CHUNKS = 384 };
    const size_t n = (size_t)CHUNKS * PIECE_CHUNK;
    uint8_t *bytes = calloc(n, 1);
    REQUIRE(bytes);
    bytes[n - 1] = '\n';
    ph_mapping map;
    REQUIRE(ph_map_open(&map, bytes, n));
    free(bytes);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 1 && piece_test_measure(t).root_entries == 16u);
    uint32_t last;
    REQUIRE(piece_lines_count(&map.b, CHUNKS - 1u, &last, 1) && last == 1);
    REQUIRE(piece_lines_adopt(t, map.b.fault, CHUNKS - 1u, &last, 1));
    bool more = true;
    unsigned slices = 0;
    while (more && slices < 2u * CHUNKS) {
        REQUIRE(piece_lines_adopt(t, map.b.fault, CHUNKS - 1u, &last, 1));
        REQUIRE(piece_lines_resolve(t, 2, &more) == PIECE_OK);
        REQUIRE(piece_test_measure(t).resolve_visits <= 2u);
        slices++;
    }
    REQUIRE(!more && slices == 16u);
    /* A batch that adds a chunk rewinds: chunk 0 becomes eligible. */
    uint32_t first;
    REQUIRE(piece_lines_count(&map.b, 0, &first, 1) && first == 0);
    REQUIRE(piece_lines_adopt(t, map.b.fault, 0, &first, 1));
    REQUIRE(piece_lines_resolve(t, 2, &more) == PIECE_OK && more);
    uint64_t rank;
    REQUIRE(piece_lines_before(piece_tree_view(t), PIECE_CHUNK, &rank) && rank == 0);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    printf("duplicate adoption (P): sweep done in %u slices of budget 2\n", slices);
}

static void typed_command_after_replenish(unsigned prepends, unsigned want_height)
{
    /* Admission invariant 3 (v2-0fv.17.md): after a successful replenish an
     * ordinary command, checkpoint begin on a cache miss, select-and-type
     * (a one-byte deletion and a one-byte insertion in a non-full ADD chunk)
     * and a take, is funded locally: W(h) covers begin 1, delete 4h + 3,
     * insert 2 min(31, h + 1) + 3 and take 1 (P). A snapshot pins the root so
     * every node on the path is shared. Base is refused and must not even
     * be asked. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    for (unsigned i = 0; i < prepends; i++)
        REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == want_height);
    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)"y", 1) == PIECE_OK); /* the cache is not the pin */
    REQUIRE(piece_replenish(t) == PIECE_OK);
    size_t calls = ph_calls();
    ph_refuse(true);
    piece_err begin = piece_checkpoint_begin(t);
    piece_err typed = piece_replace(t, prepends / 2u, 1, (const uint8_t *)"z", 1);
    piece_snapshot *taken = NULL;
    piece_err take = piece_snapshot_take(t, &taken);
    ph_refuse(false);
    REQUIRE(begin == PIECE_OK && typed == PIECE_OK && take == PIECE_OK && ph_calls() == calls);
    piece_checkpoint_commit(t);
    piece_snapshot_release(taken);
    piece_snapshot_release(pin);
    piece_close(t);
    piece_reserve_destroy(r);
}

static void admission_invariants(void)
{
    typed_command_after_replenish(10, 0);
    typed_command_after_replenish(1000, 2); /* (P) height 2 from the 205th prepend */
}

/* ---- Astra's 7b source review (docs/reviews/wave1-piece-7b-source-astra.md) --- */

/* A mapped ORIGINAL of `chunks` zero-filled chunks, chunk 0 holding an LF at
 * every even offset. */
static bool open_lf_mapping(ph_mapping *map, size_t chunks)
{
    size_t n = chunks * PIECE_CHUNK;
    uint8_t *bytes = calloc(n, 1);
    if (!bytes) return false;
    lf_every_other(bytes, PIECE_CHUNK);
    bool ok = ph_map_open(map, bytes, n);
    free(bytes);
    return ok;
}

static void astra_a_admission_after_retirement(void)
{
    /* MUST-FIX A. 169 = 7 x 24 + 1 chunks pack into 8 leaves under one
     * root: 9 of the first extent's 10 slots (P). Begin on a cache miss
     * takes the 10th (the header); abort queues that header, which still
     * holds the root. With base refused, an EOF insertion of one ORIGINAL
     * span into the last leaf (1 entry, no split) samples the root as
     * shared (2 copies) but needs 0 slots once funding has retired the
     * header: it must succeed. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 169));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_test_stats s = piece_test_measure(t);
    REQUIRE(s.live_slots == 9u && s.extents == 1u && s.available_slots == 1u);
    REQUIRE(piece_checkpoint_begin(t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 0);
    piece_checkpoint_abort(t);
    const uint64_t n = 169u * PIECE_CHUNK;
    const piece_span span = {0, 1};
    ph_refuse(true);
    piece_err err = piece_replace_spans(t, n, 0, &span, NULL, 1);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && piece_length(piece_tree_view(t)) == n + 1u);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
}

static void astra_b_add_allocation_drains(void)
{
    /* MUST-FIX B. A private one-byte ORIGINAL; begin at ADD mark 0, capture
     * the byte (one ADD chunk), abort: the chunk is queued for retirement.
     * With base capped at what is mapped now, a second capture needs one
     * chunk mapping, exactly what draining the queue returns. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, (const uint8_t *)"a", 1, &t) == PIECE_OK);
    piece_span got;
    REQUIRE(piece_checkpoint_begin(t) == PIECE_OK);
    REQUIRE(piece_capture(t, 0, 1, &got) == PIECE_OK);
    piece_checkpoint_abort(t);
    REQUIRE(piece_add_length(t) == 0);
    ph_cap_at_live();
    piece_err err = piece_capture(t, 0, 1, &got);
    ph_uncap();
    REQUIRE(err == PIECE_OK && got.src == PIECE_SPAN_ADD && piece_add_length(t) == 1u);
    piece_close(t);
    piece_reserve_destroy(r);
}

static void astra_d_resolve_exclusive_leaf(void)
{
    /* MUST-FIX D. 145 = 6 x 24 + 1 chunks: 7 leaves + root = 8 slots (P);
     * an old snapshot takes the 9th. With base refused, replacing the whole
     * text by a slice of chunk 0 (NULL counts) needs one new empty leaf
     * (the 10th) and nothing for the reference. The old snapshot owns the
     * old tree; the live leaf is exclusive. Resolving it needs no slot. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 145));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).live_slots == 8u);
    piece_snapshot *old;
    REQUIRE(piece_snapshot_take(t, &old) == PIECE_OK);
    const uint64_t n = 145u * PIECE_CHUNK;
    const piece_span slice = {1, 100};
    ph_refuse(true);
    piece_err err = piece_replace_spans(t, 0, n, &slice, NULL, 1);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    REQUIRE(err == PIECE_OK && s.live_slots == 10u && s.available_slots == 0);
    uint32_t count;
    REQUIRE(piece_lines_count(&map.b, 0, &count, 1) && piece_lines_adopt(t, map.b.fault, 0, &count, 1));
    bool more = true;
    ph_refuse(true);
    err = piece_lines_resolve(t, 1, &more);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && !more);
    uint64_t rank;
    /* (P) LFs at the even offsets 2 .. 100 of [1, 101): 50. */
    REQUIRE(piece_lines_before(piece_tree_view(t), 100, &rank) && rank == 50u);
    piece_snapshot_release(old);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
}

static void astra_e_resolve_never_retires(void)
{
    /* MUST-FIX E. 49 = 2 x 24 + 1 chunks: 3 leaves + root (P). Snapshot S0,
     * replace the whole text by a slice of chunk 0, release S0: its header
     * and the old multi-level root wait in the retirement queue. S1 pins
     * the new leaf. With no free slot and base refused, resolve (budget 1)
     * must answer NOMEM without visiting the queue; after piece_reclaim it
     * succeeds. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 49));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_snapshot *s0, *s1;
    REQUIRE(piece_snapshot_take(t, &s0) == PIECE_OK);
    const piece_span slice = {1, 100};
    REQUIRE(piece_replace_spans(t, 0, 49u * PIECE_CHUNK, &slice, NULL, 1) == PIECE_OK);
    piece_snapshot_release(s0);
    REQUIRE(piece_snapshot_take(t, &s1) == PIECE_OK);
    uint32_t count;
    REQUIRE(piece_lines_count(&map.b, 0, &count, 1) && piece_lines_adopt(t, map.b.fault, 0, &count, 1));
    size_t held = leave_free(t, 0);
    piece_test_reset_reads(t);
    bool more = true;
    ph_refuse(true);
    piece_err err = piece_lines_resolve(t, 1, &more);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    release_held(t, held);
    REQUIRE(err == PIECE_ERR_NOMEM && s.retire_steps == 0 && s.resolve_visits <= 1u);
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);
    ph_refuse(true);
    err = piece_lines_resolve(t, 1, &more);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && !more);
    piece_snapshot_release(s1);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
}

static void astra_f_owner_counts(void)
{
    /* MUST-FIX F. 2^32 - 2 more owners on the core (as if that many
     * snapshot headers lived), then one more header: the count must read
     * 2^32 + 1 (the tree, the shift, two headers), not wrap. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    piece_snapshot *a, *b;
    REQUIRE(piece_snapshot_take(t, &a) == PIECE_OK);
    const int64_t shift = (INT64_C(1) << 32) - 2;
    piece_test_shift_owners(t, NULL, shift);
    REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    REQUIRE(piece_snapshot_take(t, &b) == PIECE_OK);
    uint64_t owners = piece_test_measure(t).core_owners;
    piece_test_shift_owners(t, NULL, -shift);
    piece_snapshot_release(a);
    piece_snapshot_release(b);
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(owners == (UINT64_C(1) << 32) + 1u);
}

static void astra_f_snapshot_retains(void)
{
    /* MUST-FIX F, the snapshot's own count: 2^32 owners of one header,
     * then a retain. Run last: on a 32-bit count the retain ASSERTs. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    piece_snapshot *s;
    REQUIRE(piece_snapshot_take(t, &s) == PIECE_OK); /* cache + caller: 2 */
    const int64_t shift = (INT64_C(1) << 32) - 2;
    piece_test_shift_owners(t, s, shift);
    REQUIRE(piece_snapshot_retain(s) == s);
    uint64_t owners = piece_test_snapshot_owners(s);
    piece_snapshot_release(s);
    piece_test_shift_owners(t, s, -shift);
    piece_snapshot_release(s);
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(owners == (UINT64_C(1) << 32) + 1u);
}

static void astra_g_orphans_return(void)
{
    /* MUST-FIX G. A foreground leaf with holes: 20 rounds of (take, insert
     * at 0) occupy a header and a copied root each, 1 + 20 x 2 = 41 slots in
     * 5 extents (P); keeping every 5th snapshot keeps two slots per full
     * extent, so the core holds 4 x 8 + 9 = 41 free slots, at least
     * W(0) = 10. A second tree closes while a worker still owns its
     * snapshot; the worker's last release sends its extent to the reserve
     * inbox. Replenish (no slot deficit) must still return it to base. */
    enum { ROUNDS = 20 };
    piece_reserve *r;
    piece_tree *t, *other;
    piece_snapshot *keep[ROUNDS];
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    for (unsigned i = 0; i < ROUNDS; i++) {
        REQUIRE(piece_snapshot_take(t, &keep[i]) == PIECE_OK);
        REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    }
    REQUIRE(piece_test_measure(t).live_slots == 41u && piece_test_measure(t).height == 0);
    for (unsigned i = 0; i < ROUNDS; i++)
        if (i % 5u) {
            piece_snapshot_release(keep[i]);
            keep[i] = NULL;
        }
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);
    REQUIRE(piece_test_measure(t).core_free_slots == 41u);
    REQUIRE(piece_replenish(t) == PIECE_OK);

    piece_snapshot *held;
    REQUIRE(piece_open_copy(r, (const uint8_t *)"b", 1, &other) == PIECE_OK);
    REQUIRE(piece_snapshot_take(other, &held) == PIECE_OK);
    piece_close(other);
    os_thread worker;
    REQUIRE(os_thread_start(&worker, release_on_worker, held) == OS_OK);
    REQUIRE(os_thread_join(&worker) == OS_OK);
    REQUIRE(piece_test_measure(t).inbox_extents == 1u);
    REQUIRE(piece_replenish(t) == PIECE_OK);
    piece_test_stats s = piece_test_measure(t);
    for (unsigned i = 0; i < ROUNDS; i++)
        if (keep[i]) piece_snapshot_release(keep[i]);
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(s.inbox_extents == 0 && s.free_extents == 0);
}

static void node_refs_saturate(void)
{
    /* A24 on node reference counts, option b. 25 chunks pack into two
     * leaves under a root (P, 24 + 1): leaf 0 holds chunks 0..23, leaf 1
     * chunk 24. Leaf 0's count is set one below UINT32_MAX, standing in for
     * 2^32 - 2 references no test can build. Two path copies through leaf 1,
     * each under a pinning snapshot, copy the root and add a reference to
     * leaf 0 each: the count saturates at UINT32_MAX and sticks. Releasing
     * every real holder then never frees leaf 0 (one node leaked until the
     * core dies), never frees it early, and never aborts. */
    const size_t n = 25u * PIECE_CHUNK;
    uint8_t *bytes = malloc(n);
    REQUIRE(bytes);
    ph_fill(bytes, n, 5);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, bytes, n, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).height == 1 && piece_test_measure(t).live_slots == 3u);
    REQUIRE(piece_test_node_refs(t, 0) == 1u);
    piece_test_set_node_refs(t, 0, UINT32_MAX - 1u);
    const uint64_t in_leaf1 = 24u * PIECE_CHUNK + 7u;
    piece_snapshot *s1, *s2;
    REQUIRE(piece_snapshot_take(t, &s1) == PIECE_OK);
    REQUIRE(piece_replace(t, in_leaf1, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    REQUIRE(piece_test_node_refs(t, 0) == UINT32_MAX);
    REQUIRE(piece_snapshot_take(t, &s2) == PIECE_OK);
    REQUIRE(piece_replace(t, in_leaf1, 0, (const uint8_t *)"y", 1) == PIECE_OK);
    REQUIRE(piece_test_node_refs(t, 0) == UINT32_MAX);
    /* Every holder of leaf 0 lets go: the snapshots, then the live text. */
    REQUIRE(piece_byte_at(piece_snapshot_view(s1), 0) == bytes[0]);
    piece_snapshot_release(s1);
    piece_snapshot_release(s2);
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);
    REQUIRE(piece_test_node_refs(t, 0) == UINT32_MAX && piece_byte_at(piece_tree_view(t), 0) == bytes[0]);
    REQUIRE(piece_replace(t, 0, piece_length(piece_tree_view(t)), NULL, 0) == PIECE_OK);
    REQUIRE(piece_reclaim(t, SIZE_MAX) == false);
    /* (P) The new empty root leaf, and leaf 0, still occupied. */
    REQUIRE(piece_test_measure(t).live_slots == 2u);
    free(bytes);
    piece_close(t);
    piece_reserve_destroy(r);
    printf("node refs (P): saturated at UINT32_MAX, never freed while the core lives, no abort\n");
}

/* ---- Astra's 7b re-read (docs/reviews/wave1-piece-7b-source-astra-2.md) ----- */

static piece_snapshot *a1_held;

static void a1_worker_release(void)
{
    piece_snapshot_release(a1_held); /* the closed tree's last owner */
    a1_held = NULL;
}

/* A26's contract while another thread releases storage: a call may be
 * refused that an identical retry would not be; nothing latches. `first`
 * is either result; a refusal changed nothing; the retry succeeds. */
static bool race_contract(piece_err first, piece_err retry, uint64_t length_before, uint64_t length_refused,
                          uint64_t add_before, uint64_t add_refused)
{
    bool kept = first == PIECE_OK || (first == PIECE_ERR_NOMEM && length_refused == length_before &&
                                      add_refused == add_before);
    printf("race contract: first call %s, retry %s\n", first == PIECE_OK ? "admitted" : "refused",
           retry == PIECE_OK ? "admitted" : "refused");
    return kept && retry == PIECE_OK;
}

static void astra2_a1_release_during_refusal(void)
{
    /* A(1), as A26's contract. Foreground: a one-byte private tree, 9 free
     * slots in its extent.
     * A second one-byte tree closes while its snapshot is held. Base is
     * capped at what is mapped. Inserting 20 x 22 references to ORIGINAL
     * byte 0 at EOF needs exactly 19 leaf siblings + 1 branch root + 1
     * branch sibling + 1 top root = 22 slots, no path shared (P, Astra). The
     * held snapshot's last release lands inside base's refusal of the next
     * extent: it returns the closed core's mappings and its extent (9 + 10 =
     * 19 slots); the remainder, 3 slots, then fits one page of base. The
     * call during the race may be refused; the identical retry must not. */
    enum { SPANS = 20 * 22 };
    static piece_span spans[SPANS];
    for (size_t i = 0; i < SPANS; i++) spans[i] = (piece_span){0, 1};
    piece_reserve *r;
    piece_tree *t, *other;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, (const uint8_t *)"a", 1, &t) == PIECE_OK);
    REQUIRE(piece_open_copy(r, (const uint8_t *)"b", 1, &other) == PIECE_OK);
    REQUIRE(piece_snapshot_take(other, &a1_held) == PIECE_OK);
    piece_close(other);
    REQUIRE(piece_test_measure(t).available_slots == 9u);
    ph_cap_at_live();
    ph_on_refusal = a1_worker_release;
    piece_err first = piece_replace_spans(t, 1, 0, spans, NULL, SPANS);
    ph_on_refusal = NULL;
    uint64_t length = piece_length(piece_tree_view(t)), add = piece_add_length(t);
    piece_err retry = first == PIECE_OK ? PIECE_OK : piece_replace_spans(t, 1, 0, spans, NULL, SPANS);
    ph_uncap();
    if (a1_held) piece_snapshot_release(a1_held);
    a1_held = NULL;
    piece_test_stats s = piece_test_measure(t);
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(race_contract(first, retry, 1, length, 0, add) && s.slots_claimed == 22u);
}

static void astra2_a2_no_scalar_purchase(void)
{
    /* A(2). 169 mapped chunks: 8 leaves + root, 1 free slot (P). Base may
     * map exactly one ADD chunk (17 pages) more. A one-byte append at EOF
     * needs 0 slots (exclusive path, the last leaf has room) and the chunk;
     * buying an extent for the scalar claim 2 x 1 + 3 = 5 would leave 16
     * pages for 17. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 169));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 1u);
    ph_cap_at_live_plus(17u * 4096u);
    piece_err err = piece_replace(t, 169u * PIECE_CHUNK, 0, (const uint8_t *)"x", 1);
    ph_uncap();
    piece_test_stats s = piece_test_measure(t);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(err == PIECE_OK && s.slots_claimed == 0 && s.extents == 1u);
}

static void astra2_e_touched_nodes(void)
{
    /* E. 25 mapped chunks: two leaves under a root (P). A snapshot pins the
     * root; chunk 0 is adopted. Budget 2 enters the root and the first leaf;
     * the leaf is copied, then the root, whose copy adds a reference to both
     * leaves: 2 entered + 2 children updated = 4 touched nodes (P), within
     * 17 x 2 (piece.h). */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 25));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    uint32_t count;
    REQUIRE(piece_lines_count(&map.b, 0, &count, 1) && piece_lines_adopt(t, map.b.fault, 0, &count, 1));
    bool more;
    REQUIRE(piece_lines_resolve(t, 2, &more) == PIECE_OK);
    piece_test_stats s = piece_test_measure(t);
    piece_snapshot_release(pin);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(s.resolve_visits == 2u && s.resolve_touched == 4u && s.resolve_touched <= 17u * 2u);
}

static void astra2_h_discard_keeps_revision(void)
{
    /* H. 528 = 22 x 24 mapped chunks: 22 leaves, 2 branches, a root = 25
     * slots on 3 extents; a snapshot's header leaves 4 free (P). With base
     * refused, deleting the first byte copies root, left branch and first
     * leaf (exact plan 3), leaving 1 free slot and the right branch shared.
     * Chunks 0 and 384 are adopted (the first under each branch). Budget
     * 20 = root + left branch + 16 left leaves + right branch + its first
     * leaf (P): the first leaf is rewritten in place; the right leaf's copy
     * takes the last slot, its parent's copy cannot be funded, so that copy
     * is discarded. Only an in-place rewrite remains: the live revision and a
     * live iterator survive. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 528));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).live_slots == 25u && piece_test_measure(t).extents == 3u);
    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 4u);
    ph_refuse(true);
    piece_err err = piece_replace(t, 0, 1, NULL, 0);
    ph_refuse(false);
    REQUIRE(err == PIECE_OK && piece_test_measure(t).available_slots == 1u);
    uint32_t count;
    REQUIRE(piece_lines_count(&map.b, 0, &count, 1) && piece_lines_adopt(t, map.b.fault, 0, &count, 1));
    REQUIRE(piece_lines_count(&map.b, 384, &count, 1) && piece_lines_adopt(t, map.b.fault, 384, &count, 1));
    const piece_view *v = piece_tree_view(t);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    uint64_t revision = piece_test_measure(t).revision;
    bool more;
    ph_refuse(true);
    err = piece_lines_resolve(t, 20, &more);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    REQUIRE(err == PIECE_OK && s.resolve_visits == 20u && s.revision == revision);
    const uint8_t *bytes;
    REQUIRE(piece_iter_next(&it, &bytes) > 0); /* the live iterator is still valid */
    piece_snapshot_release(pin);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
}

/* ---- Astra's third read (docs/reviews/wave1-piece-7b-source-astra-3.md), A26 ---- */

static void astra3_i_release_after_refusal(void)
{
    /* I, as A26's contract. 169 mapped chunks, 1 free slot (P); a one-byte
     * EOF append needs 0 slots and one ADD chunk, 17 pages; base has 16
     * pages of headroom. A closed one-byte tree's snapshot is released inside
     * base's refusal of the chunk: its ORIGINAL and core pages return. The
     * call may be refused; the identical retry succeeds, nothing latched. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 169));
    piece_reserve *r;
    piece_tree *t, *other;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    REQUIRE(piece_open_copy(r, (const uint8_t *)"b", 1, &other) == PIECE_OK);
    REQUIRE(piece_snapshot_take(other, &a1_held) == PIECE_OK);
    piece_close(other);
    const uint64_t n = 169u * PIECE_CHUNK;
    ph_cap_at_live_plus(16u * 4096u);
    ph_on_refusal = a1_worker_release;
    piece_err first = piece_replace(t, n, 0, (const uint8_t *)"x", 1);
    ph_on_refusal = NULL;
    uint64_t length = piece_length(piece_tree_view(t)), add = piece_add_length(t);
    piece_err retry = first == PIECE_OK ? PIECE_OK : piece_replace(t, n, 0, (const uint8_t *)"x", 1);
    ph_uncap();
    if (a1_held) piece_snapshot_release(a1_held);
    a1_held = NULL;
    uint64_t final_length = piece_length(piece_tree_view(t));
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(race_contract(first, retry, n, length, 0, add) && final_length == n + 1u);
}

static void astra3_j_surplus_returned(void)
{
    /* J, A26 step 4. 169 mapped chunks pinned by a snapshot: its header
     * takes the extent's last slot; the EOF append needs 2 copies (root and
     * last leaf) and one ADD chunk (17 pages) (P). The reserve holds 2 whole
     * free extents (held and released slots) and base 16 pages of headroom.
     * Step 4 keeps ceil(2 / 10) = 1 extent and returns the other: 16 + 1 =
     * 17 pages for the chunk. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 169));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 0);
    REQUIRE(piece_test_fund_slots(t, 20u));
    REQUIRE(piece_test_measure(t).free_extents == 2u);
    ph_cap_at_live_plus(16u * 4096u);
    piece_err err = piece_replace(t, 169u * PIECE_CHUNK, 0, (const uint8_t *)"x", 1);
    ph_uncap();
    piece_test_stats s = piece_test_measure(t);
    piece_snapshot_release(pin);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(err == PIECE_OK && s.slots_claimed == 2u);
}

static void astra3_k1_funded_exact_drains_nothing(void)
{
    /* K, A26 step 2. 121 = 5 x 24 + 1 mapped chunks: 6 leaves + root = 7
     * slots (P). Capture one byte (a partial ADD tail), then begin and abort:
     * the queued header pins the root and takes a slot, 10 - 7 - 1 = 2 free.
     * A one-byte EOF append misses the scalar 2 x 1 + 3 = 5 but its exact
     * plan, 2 copies and no new ADD chunk, is funded: no drain. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 121));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_span got;
    REQUIRE(piece_capture(t, 0, 1, &got) == PIECE_OK);
    REQUIRE(piece_checkpoint_begin(t) == PIECE_OK);
    piece_checkpoint_abort(t);
    REQUIRE(piece_test_measure(t).available_slots == 2u);
    piece_test_reset_reads(t);
    ph_refuse(true);
    piece_err err = piece_replace(t, 121u * PIECE_CHUNK, 0, (const uint8_t *)"x", 1);
    ph_refuse(false);
    piece_test_stats s = piece_test_measure(t);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(err == PIECE_OK && s.retire_steps == 0 && s.slots_claimed == 2u);
}

static void astra3_k2_missing_extra_is_not_fast(void)
{
    /* K, A26 step 1. A one-byte private tree: 9 free slots cover the scalar
     * claim 2 x 0 + 3 = 3, but the first ADD chunk is missing, so this is not
     * the fast path: the exact plan (0 slots) admits it. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, (const uint8_t *)"a", 1, &t) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 9u);
    piece_err err = piece_replace(t, 1, 0, (const uint8_t *)"x", 1);
    piece_test_stats s = piece_test_measure(t);
    /* With the tail chunk present, the next key is the fast path. */
    piece_err next = piece_replace(t, 2, 0, (const uint8_t *)"y", 1);
    piece_test_stats fast = piece_test_measure(t);
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(err == PIECE_OK && s.slots_claimed == 0 && next == PIECE_OK && fast.slots_claimed == 3u);
}

static void astra4_empty_insert_keeps_no_add(void)
{
    /* Astra's admission read, step 1. An empty tree on a fresh reserve; base
     * capped at one ADD chunk (17 pages) more. Begin, insert one byte (the
     * chunk), delete it passing a non-NULL pointer with n == 0, abort: the
     * chunk must retire, so the same insert at quiescence fits again. A
     * deletion that prepares ADD storage from the pointer alone keeps a
     * reference nobody owns, and the chunk never returns. */
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    const uint8_t x = 'x';
    ph_cap_at_live_plus(17u * 4096u);
    piece_err begin = piece_checkpoint_begin(t);
    piece_err first = piece_replace(t, 0, 0, &x, 1);
    piece_err erase = piece_replace(t, 0, 1, &x, 0);
    if (begin == PIECE_OK) piece_checkpoint_abort(t);
    piece_err again = piece_replace(t, 0, 0, &x, 1);
    ph_uncap();
    uint64_t length = piece_length(piece_tree_view(t));
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(begin == PIECE_OK && first == PIECE_OK && erase == PIECE_OK && again == PIECE_OK && length == 1u);
}

/* A26 step 4, condition 2 (advisor's ruling on Astra's admission read, step
 * 4): a refusal returns every object the call took from base to base; base's
 * available bytes and the tree are as before the call. */
static void step4_capture_refusal_returns_chunk(void)
{
    /* A private ORIGINAL of exactly one chunk, captured whole: ADD's chunk 0
     * is full and is its own directory root (P). Capturing one more byte
     * needs a new chunk (17 pages) and a level-1 directory parent (1 page).
     * Base admits the chunk, not the parent: NOMEM, base and the tree as
     * before; one page more and the same call succeeds. */
    static uint8_t bytes[PIECE_CHUNK];
    ph_fill(bytes, sizeof bytes, 9);
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_copy(r, bytes, sizeof bytes, &t) == PIECE_OK);
    piece_span whole, got;
    REQUIRE(piece_capture(t, 0, PIECE_CHUNK, &whole) == PIECE_OK);
    ph_cap_at_live_plus(17u * 4096u);
    size_t live = atomic_load(&ph_live_bytes);
    uint64_t add = piece_add_length(t);
    piece_err err = piece_capture(t, 0, 1, &got);
    size_t live_after = atomic_load(&ph_live_bytes);
    uint64_t add_after = piece_add_length(t);
    ph_cap_at_live_plus(18u * 4096u);
    piece_err retry = piece_capture(t, 0, 1, &got);
    ph_uncap();
    piece_close(t);
    piece_reserve_destroy(r);
    REQUIRE(err == PIECE_ERR_NOMEM && live_after == live && add_after == add);
    REQUIRE(retry == PIECE_OK && got.src == (PIECE_SPAN_ADD | PIECE_CHUNK));
}

static void step4_edit_refusal_returns_extent(void)
{
    /* 169 mapped chunks pinned by a snapshot: no free slot, an empty
     * reserve; the EOF append needs 2 copies (one extent) and one ADD chunk
     * (P). Base admits the extent's page, not the chunk: NOMEM, and the
     * extent goes back to base, not into the reserve. */
    ph_mapping map;
    REQUIRE(open_lf_mapping(&map, 169));
    piece_reserve *r;
    piece_tree *t;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    REQUIRE(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_snapshot *pin;
    REQUIRE(piece_snapshot_take(t, &pin) == PIECE_OK);
    REQUIRE(piece_test_measure(t).available_slots == 0 && piece_test_measure(t).free_extents == 0);
    const uint64_t n = 169u * PIECE_CHUNK;
    ph_cap_at_live_plus(4096u);
    size_t live = atomic_load(&ph_live_bytes);
    piece_err err = piece_replace(t, n, 0, (const uint8_t *)"x", 1);
    size_t live_after = atomic_load(&ph_live_bytes);
    piece_test_stats s = piece_test_measure(t);
    uint64_t length = piece_length(piece_tree_view(t));
    ph_cap_at_live_plus(18u * 4096u);
    piece_err retry = piece_replace(t, n, 0, (const uint8_t *)"x", 1);
    ph_uncap();
    piece_snapshot_release(pin);
    piece_close(t);
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    REQUIRE(err == PIECE_ERR_NOMEM && live_after == live && s.free_extents == 0 && length == n);
    REQUIRE(retry == PIECE_OK);
}

int main(void)
{
    shape_limits();
    packed_interior_delete();
    delete_at_limit();
    version_headers();
    extents_follow_owners();
    add_directory_rollback();
    large_replay();
    mapped_reads();
    resolve_misses_and_slices();
    resolve_rollback();
    private_reference_reads();
    private_cut_reads();
    exact_claims();
    hundred_tabs();
    begin_is_constant();
    undo_seam();
    asymmetric_cuts();
    duplicate_adoption();
    admission_invariants();
    astra_a_admission_after_retirement();
    astra_b_add_allocation_drains();
    astra_d_resolve_exclusive_leaf();
    astra_e_resolve_never_retires();
    astra_g_orphans_return();
    astra2_a1_release_during_refusal();
    astra2_a2_no_scalar_purchase();
    astra2_e_touched_nodes();
    astra3_i_release_after_refusal();
    astra3_j_surplus_returned();
    astra3_k1_funded_exact_drains_nothing();
    astra3_k2_missing_extra_is_not_fast();
    astra4_empty_insert_keeps_no_add();
    step4_capture_refusal_returns_chunk();
    step4_edit_refusal_returns_extent();
    node_refs_saturate();
    astra_f_owner_counts();
    astra_f_snapshot_retains(); /* last: on a 32-bit count it ASSERTs */
    astra2_h_discard_keeps_revision(); /* before the fix its iterator ASSERTs */
    if (ph_failures) return 1;
    puts("piece_shape_test: ok");
    return 0;
}
