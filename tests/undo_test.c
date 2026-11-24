#include "undo/undo.h"
#include "piece_harness.h"
#include "assert_death.h"

/* (G) A13's large mapped fixture, with each cut just inside a different
 * boundary chunk. The model gate uses ADD growth for the bytes copied.
 * Real tree (7b, v2-0fv.17): ORIGINAL bytes read across describe + delete +
 * undo, setup and verification excluded, are at most 2 * PIECE_CHUNK (P, A13
 * and v2-0fv.7 section 6), counted by the piece test hooks; midpoint_reads
 * attains the bound through undo. */
#define LARGE_LEN (513u * PIECE_CHUNK)

/* (P, A20) Eight inline entries at 24 B, tag/count at 16 B, record
 * prefix at 48 B, slab owner at 8 B. Group and record each use a slot. */
enum { SLOT_BYTES = 264, GROUP_RECORD_BYTES = 2 * SLOT_BYTES, ENTRY_BYTES = 24 };

static const undo_state idle;
static const undo_state first = {{'a'}};
static const undo_state last = {{'z'}};

typedef struct fixture {
    piece_reserve *reserve;
    piece_tree *tree;
    undo_log *log;
} fixture;

static const ph_spans *expect_replay;
static bool fail_replay;
static size_t fail_replay_step, joined_descriptions;
static uint64_t fail_at_add_mark;
static ph_poll late_forward, late_replay;

piece_span_nl __real_piece_span_nl_join(piece_span, piece_span_nl, piece_span, piece_span_nl);
piece_span_nl __wrap_piece_span_nl_join(piece_span left, piece_span_nl ln,
                                        piece_span right, piece_span_nl rn)
{
    joined_descriptions++;
    return __real_piece_span_nl_join(left, ln, right, rn);
}

piece_err __real_piece_replace(piece_tree *, uint64_t, uint64_t, const uint8_t *, size_t);
piece_err __wrap_piece_replace(piece_tree *t, uint64_t off, uint64_t del,
                              const uint8_t *bytes, size_t len)
{
    if (fail_at_add_mark) {
        ASSERT(piece_add_length(t) == fail_at_add_mark);
        return PIECE_ERR_NOMEM;
    }
    piece_err e = __real_piece_replace(t, off, del, bytes, len);
    if (e == PIECE_OK && late_forward != PH_POLL_REAL) ph_poll_set(late_forward);
    return e;
}

piece_err __real_piece_replace_spans(piece_tree *, uint64_t, uint64_t,
                                    const piece_span *, const piece_span_nl *, size_t);
piece_err __wrap_piece_replace_spans(piece_tree *t, uint64_t off, uint64_t del,
                                    const piece_span *spans, const piece_span_nl *nl, size_t count)
{
    if (fail_replay || (fail_replay_step != 0 && --fail_replay_step == 0))
        return PIECE_ERR_NOMEM; /* Exactly one atomic step fails. */
    if (expect_replay && count != 0) {
        ASSERT(count == expect_replay->n && nl != NULL);
        for (size_t i = 0; i < count; i++) {
            ASSERT(ph_span_eq(spans[i], expect_replay->s[i]));
            ASSERT(nl[i].head == expect_replay->nl[i].head);
            ASSERT(nl[i].tail == expect_replay->nl[i].tail);
        }
        expect_replay = NULL;
    }
    piece_err e = __real_piece_replace_spans(t, off, del, spans, nl, count);
    if (e == PIECE_OK && late_replay != PH_POLL_REAL) ph_poll_set(late_replay);
    return e;
}

static undo_err replace_cmd(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes, size_t len,
                            bool join, const undo_state *before, const undo_state *after)
{
    undo_err e = undo_begin(u, before, join);
    if (e != UNDO_OK) return e;
    e = undo_replace(u, off, del, bytes, len);
    if (e != UNDO_OK) {
        undo_abort(u);
        return e;
    }
    return undo_commit(u, after);
}

static undo_err insert_cmd(undo_log *u, uint64_t off, const uint8_t *bytes, size_t len,
                            bool join, const undo_state *before, const undo_state *after)
{
    return replace_cmd(u, off, 0, bytes, len, join, before, after);
}

static undo_err erase_cmd(undo_log *u, uint64_t off, uint64_t len, bool join,
                           const undo_state *before, const undo_state *after)
{
    return replace_cmd(u, off, len, NULL, 0, join, before, after);
}

static fixture open_copy(const uint8_t *bytes, size_t n)
{
    fixture f;
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_copy(f.reserve, bytes, n, &f.tree) == PIECE_OK);
    ASSERT(undo_create(&f.log, f.tree, SIZE_MAX, NULL, NULL) == UNDO_OK);
    return f;
}

static void close_fixture(fixture f)
{
    undo_destroy(f.log);
    piece_close(f.tree);
    piece_reserve_destroy(f.reserve);
}

static void text_eq(const piece_view *v, const char *s)
{
    ASSERT(piece_length(v) == strlen(s));
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *bytes;
    size_t n;
    while ((n = piece_iter_next(&it, &bytes)) != 0) {
        ASSERT(memcmp(bytes, s, n) == 0);
        s += n;
    }
}

static bool trusted(void *ctx, const piece_backing *b)
{
    ASSERT(b != NULL);
    return *(bool *)ctx;
}

static void one_span_mapped(void)
{
    static uint8_t sample[LARGE_LEN];
    const uint64_t off = 1, len = sizeof sample - 2; /* (P) trim a byte at each end */
    sample[off] = '\n';
    sample[off + len - 1] = '\n';
    ph_mapping mp;
    ASSERT(ph_map_open(&mp, sample, sizeof sample));
    fixture f;
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_mapped(f.reserve, &mp.b, &f.tree) == PIECE_OK);
    bool trust = true;
    ASSERT(undo_create(&f.log, f.tree, SIZE_MAX, trusted, &trust) == UNDO_OK);

    const piece_view *v = piece_tree_view(f.tree);
    ASSERT(ph_lines_complete(f.tree, &mp, "undo resolve"));
    pm_reserve *mr;
    pm_tree *model;
    ASSERT(pm_reserve_create(&mr) == PIECE_OK);
    ASSERT(pm_open_copy(mr, sample, sizeof sample, &model) == PIECE_OK);
    /* (P) The seam, EOF probes in the helper and one sampled probe
     * compare both rank and select against the reference bytes. */
    ph_check_lines(v, pm_tree_view(model), off, 1, true, "before mapped delete");
    ph_spans original = {0}, restored = {0};
    ph_describe(v, off, len, true, &original);
    ASSERT(original.n == 1); /* (P) contiguous mapped offsets */
    const uint64_t add = piece_add_length(f.tree);
    piece_test_reset_reads(f.tree);
    ASSERT(erase_cmd(f.log, off, len, false, &first, &last) == UNDO_OK);
    size_t mapped_read = piece_test_measure(f.tree).mapped_reads;
    ASSERT(piece_add_length(f.tree) == add);
    ASSERT(pm_checkpoint_begin(model) == PIECE_OK);
    ASSERT(pm_replace(model, off, len, NULL, 0) == PIECE_OK);
    ph_check_lines(v, pm_tree_view(model), off, 1, true, "after mapped delete");
    undo_change c;
    /* A known identity change must stop before the planner can recount
     * the old mapped record in a sanitizer build. */
    expect_replay = &original;
    trust = false;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_ERR_CONFLICT);
    ASSERT(expect_replay == &original);
    ASSERT(c.len == 0 && undo_get_history(f.log).undo_groups == 1);
    ASSERT(piece_length(v) == sizeof sample - len);
    trust = true;
    expect_replay = &original;
    piece_test_reset_reads(f.tree);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && expect_replay == NULL);
    mapped_read += piece_test_measure(f.tree).mapped_reads;
    /* (P) Each cut lies one byte inside its chunk: the shorter halves are
     * 1 B at each end, for describe and for delete; the undo cuts nothing.
     * (G) A13: at most 2 x 65,536. */
    ASSERT(mapped_read == 4u && mapped_read <= 2u * PIECE_CHUNK);
    ASSERT(piece_add_length(f.tree) == add && piece_length(v) == sizeof sample);
    pm_checkpoint_abort(model);
    ph_check_lines(v, pm_tree_view(model), off, 1, true, "after mapped undo");
    pm_close(model);
    pm_reserve_destroy(mr);
    ph_describe(v, off, len, true, &restored);
    ASSERT(restored.n == original.n && ph_span_eq(restored.s[0], original.s[0]));
    ASSERT(restored.nl[0].head == original.nl[0].head && restored.nl[0].tail == original.nl[0].tail);

    /* (P) Two edit records permit a yield before the ORIGINAL restore. */
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, off, len, NULL, 0) == UNDO_OK);
    ASSERT(undo_replace(f.log, off, 0, (const uint8_t *)"x", 1) == UNDO_OK);
    ASSERT(undo_commit(f.log, &last) == UNDO_OK);
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE);
    ASSERT(piece_length(v) == sizeof sample - len);
    trust = false;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_ERR_CONFLICT && c.len == 0);
    ASSERT(piece_length(v) == sizeof sample - len + 1 && piece_byte_at(v, off) == 'x');
    trust = true;
    expect_replay = &original;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && expect_replay == NULL);

    /* (G) Asymmetry: one mapped byte becomes an immutable ADD literal. */
    const uint64_t literal_at = piece_add_length(f.tree);
    ASSERT(erase_cmd(f.log, off, 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == literal_at + 1);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    ph_describe(v, off, 1, true, &restored);
    ASSERT(restored.n == 1 && restored.s[0].src == (PIECE_SPAN_ADD | literal_at));
    ASSERT(restored.nl[0].head == 1 && piece_byte_at(v, off) == '\n');
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == literal_at + 1);
    ph_spans_free(&original);
    ph_spans_free(&restored);
    close_fixture(f);
    ASSERT(atomic_load(&mp.owners) == 1); /* (P) only the fixture owns the mapping */
    ph_map_unref(&mp);
}

static void threshold(void)
{
    /* (G) 16 B per coalesced pair. Test one below, equal, and one above. */
    const uint8_t sample[sizeof(piece_span) + 1] = "\n0123456789abcdef";
    for (size_t n = sizeof(piece_span) - 1; n <= sizeof sample; n++) {
        fixture f = open_copy(sample, n);
        undo_change c;
        ASSERT(erase_cmd(f.log, 0, n, false, &idle, &idle) == UNDO_OK);
        uint64_t copied = n < sizeof(piece_span) ? n : 0;
        ASSERT(piece_add_length(f.tree) == copied);
        ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
        const piece_view *v = piece_tree_view(f.tree);
        piece_iter it;
        piece_iter_init(&it, v, 0);
        piece_span s;
        piece_span_nl nl;
        ASSERT(piece_iter_next_span(&it, n, &s, &nl));
        ASSERT(s.src == (copied ? PIECE_SPAN_ADD : 0) && s.len == n && nl.head == 1);
        ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
        ASSERT(piece_add_length(f.tree) == copied);
        close_fixture(f);
    }
}

static void external_store(void)
{
    /* (P) Four inserts of one full pair-width each between five original
     * runs gives nine eligible runs. The source switches are intentional. */
    enum { splits = 4, run = sizeof(piece_span), spans = splits * 2 + 1 };
    uint8_t sample[(splits + 1) * run], inserted[run];
    memset(sample, 'o', sizeof sample);
    memset(inserted, 'a', sizeof inserted);
    sample[0] = inserted[0] = '\n';
    fixture f = open_copy(sample, sizeof sample);
    const piece_view *v = piece_tree_view(f.tree);
    for (size_t i = 1; i <= splits; i++) {
        ASSERT(insert_cmd(f.log, (i * 2 - 1) * run, inserted, run, false, &idle, &idle) == UNDO_OK);
    }
    const uint64_t len = piece_length(v), add = piece_add_length(f.tree);
    ph_spans desc = {0}, restored = {0};
    ph_describe(v, 0, len, true, &desc);
    ASSERT(desc.n == spans);
    undo_clear(f.log);
    const size_t payload = spans * ENTRY_BYTES;
    undo_set_cap(f.log, GROUP_RECORD_BYTES + payload);
    ASSERT(erase_cmd(f.log, 0, len, false, &idle, &idle) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == add);
    undo_stats charged = undo_get_stats(f.log);
    ASSERT(charged.span_payload_bytes == payload && charged.history_bytes == GROUP_RECORD_BYTES + payload);
    undo_change c;
    expect_replay = &desc;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && expect_replay == NULL);
    ph_describe(v, 0, len, true, &restored);
    ASSERT(restored.n == desc.n);
    for (size_t i = 0; i < desc.n; i++) {
        ASSERT(ph_span_eq(desc.s[i], restored.s[i]));
        ASSERT(desc.nl[i].head == restored.nl[i].head && desc.nl[i].tail == restored.nl[i].tail);
    }
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    /* (P) The same nine runs can share a segment for two edits. Branching
     * discards redo's claim, while the new claim survives retirement. */
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    undo_stats old = undo_get_stats(f.log);
    fail_at_add_mark = add;
    ASSERT(erase_cmd(f.log, 0, len, false, &idle, &idle) == UNDO_ERR_NOMEM);
    fail_at_add_mark = 0;
    ASSERT(undo_get_history(f.log).redo_groups == 1);
    ASSERT(undo_get_stats(f.log).storage_bytes == old.storage_bytes);
    ASSERT(undo_get_stats(f.log).span_payload_bytes == old.span_payload_bytes);
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    expect_replay = &desc;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && expect_replay == NULL);
    ASSERT(erase_cmd(f.log, 0, len, false, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).redo_groups == 0);
    (void)undo_maintain(f.log, SIZE_MAX);
    ASSERT(undo_get_stats(f.log).span_payload_bytes == old.span_payload_bytes);
    ASSERT(undo_get_stats(f.log).storage_bytes == old.storage_bytes);
    expect_replay = &desc;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && expect_replay == NULL);
    undo_clear(f.log);
    ASSERT(undo_get_stats(f.log).span_payload_bytes == 0);
    /* (P, A20) One byte below the external-vector group charge. */
    undo_set_cap(f.log, GROUP_RECORD_BYTES + payload - 1);
    size_t evicted = undo_get_history(f.log).evicted_groups;
    ASSERT(erase_cmd(f.log, 0, len, false, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_stats(f.log).history_bytes == 0);
    ASSERT(undo_get_history(f.log).evicted_groups == evicted + 1);
    ph_spans_free(&desc);
    ph_spans_free(&restored);
    close_fixture(f);
}

static void typing_record(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    /* (P) Carry the burst one byte past the literal threshold. Each
     * keystroke retains one already-appended byte for redo. */
    const uint8_t key = 'x';
    const size_t keys = sizeof(piece_span) + 1;
    ASSERT(insert_cmd(f.log, 0, &key, 1, false, &first, &idle) == UNDO_OK);
    const size_t initial = GROUP_RECORD_BYTES + 1; /* (P) Two slots plus one byte. */
    ASSERT(undo_get_stats(f.log).history_bytes == initial);
    fail_at_add_mark = 1;
    ASSERT(insert_cmd(f.log, 1, &key, 1, true, &idle, &last) == UNDO_ERR_NOMEM);
    fail_at_add_mark = 0;
    ASSERT(undo_get_stats(f.log).history_bytes == initial);
    ASSERT(piece_length(v) == 1 && piece_add_length(f.tree) == 1);
    for (size_t i = 1; i < keys; i++) {
        ASSERT(insert_cmd(f.log, i, &key, 1, true, &idle, &last) == UNDO_OK);
        undo_stats s = undo_get_stats(f.log);
        ASSERT(s.records == 1);
        ASSERT(s.history_bytes == initial + i);
    }
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_OK);
    ASSERT(c.groups == 1 && c.state.bytes[0] == 'a');
    text_eq(v, "");
    ASSERT(undo_redo(f.log, 1, 0, &c) == UNDO_OK);
    ASSERT(c.groups == 1 && c.state.bytes[0] == 'z');
    ASSERT(piece_length(v) == keys && piece_add_length(f.tree) == keys);
    for (size_t i = 0; i < keys; i++) ASSERT(piece_byte_at(v, i) == key);
    close_fixture(f);
}

static void typing_abort_reuse(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    ASSERT(insert_cmd(f.log, 0, (const uint8_t *)"a", 1, false, &idle, &idle) == UNDO_OK);
    /* (P) The abandoned adjacent write can be seen in its retained
     * snapshot. A retry regrows ADD and extends only the published text. */
    ASSERT(undo_begin(f.log, &idle, true) == UNDO_OK);
    ASSERT(undo_replace(f.log, 1, 0, (const uint8_t *)"x", 1) == UNDO_OK);
    piece_snapshot *inside;
    ASSERT(piece_snapshot_take(f.tree, &inside) == PIECE_OK);
    ASSERT(undo_get_stats(f.log).history_bytes == GROUP_RECORD_BYTES + 1);
    undo_abort(f.log);
    text_eq(v, "a");
    ASSERT(piece_add_length(f.tree) == 1);
    ASSERT(insert_cmd(f.log, 1, (const uint8_t *)"b", 1, true, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 1 && undo_get_stats(f.log).records == 1);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_OK && c.groups == 1);
    text_eq(v, "");
    ASSERT(undo_redo(f.log, 1, 0, &c) == UNDO_OK);
    text_eq(v, "ab");
    text_eq(piece_snapshot_view(inside), "ax");
    piece_snapshot_release(inside);
    close_fixture(f);
}

static void typing_add_gap(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    ASSERT(insert_cmd(f.log, 0, (const uint8_t *)"a", 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(insert_cmd(f.log, 1, (const uint8_t *)"x", 1, false, &idle, &idle) == UNDO_OK);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_OK);
    /* (P) Undoing the branch does not rewind its one ADD byte.
     * The caller can join the applied group, but its tail's ADD run ends
     * before the new write. Replay must avoid the abandoned branch byte. */
    ASSERT(insert_cmd(f.log, 1, (const uint8_t *)"b", 1, true, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 1 && undo_get_history(f.log).redo_groups == 0);
    ASSERT(undo_get_stats(f.log).records == 2);
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE && c.groups == 0);
    text_eq(v, "a");
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_OK && c.groups == 1);
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    text_eq(v, "ab");
    close_fixture(f);
}

static void burst_and_cap(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    /* (G) The caller joins the first two keys and starts a new group for the third. */
    ASSERT(insert_cmd(f.log, 0, (const uint8_t *)"a", 1, false, &first, &idle) == UNDO_OK);
    ASSERT(insert_cmd(f.log, 1, (const uint8_t *)"b", 1, true, &idle, &last) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 1);
    size_t old_bytes = undo_get_stats(f.log).history_bytes;
    ASSERT(insert_cmd(f.log, 2, (const uint8_t *)"c", 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 2);
    undo_set_cap(f.log, undo_get_stats(f.log).history_bytes - old_bytes);
    undo_history h = undo_get_history(f.log);
    ASSERT(h.undo_groups == 1 && h.evicted_groups == 1);
    undo_change c;
    ASSERT(undo_undo(f.log, 2, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1);
    text_eq(v, "ab");
    ASSERT(insert_cmd(f.log, 2, NULL, 0, false, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).redo_groups == 1);
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    text_eq(v, "abc");
    close_fixture(f);
}

static void erase_bursts(void)
{
    /* (P) Two backward-adjacent erases of different lengths and two
     * forward erases at one offset. Each direction forms one record. */
    const uint8_t original[] = "abcde";
    fixture f = open_copy(original, sizeof original - 1);
    const piece_view *v = piece_tree_view(f.tree);
    undo_change c;
    ASSERT(erase_cmd(f.log, 3, 2, false, &first, &idle) == UNDO_OK);
    ASSERT(erase_cmd(f.log, 2, 1, true, &idle, &last) == UNDO_OK);
    ASSERT(erase_cmd(f.log, 0, 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(erase_cmd(f.log, 0, 1, true, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 2 && undo_get_stats(f.log).records == 2);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1);
    text_eq(v, "ab");
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1 && c.state.bytes[0] == 'a');
    text_eq(v, "abcde");
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.state.bytes[0] == 'z');
    text_eq(v, "ab");
    close_fixture(f);
}

static void erase_reverses_direction(void)
{
    /* (P) First capture b, then prepend a, then append c in document
     * order. The last append must stay separate from the reverse cuts. */
    const uint8_t original[] = "abcd";
    fixture f = open_copy(original, sizeof original - 1);
    ASSERT(undo_begin(f.log, &idle, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 1, 1, NULL, 0) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 1, NULL, 0) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 1, NULL, 0) == UNDO_OK);
    ASSERT(undo_commit(f.log, &idle) == UNDO_OK);
    ASSERT(undo_get_stats(f.log).records == 2 && undo_get_history(f.log).undo_groups == 1);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE);
    text_eq(piece_tree_view(f.tree), "cd");
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_OK && c.groups == 1);
    text_eq(piece_tree_view(f.tree), "abcd");
    close_fixture(f);
}

static void erase_threshold(void)
{
    /* (P) One byte past a pair-width, with three bytes before the
     * source chunk boundary. Exercise counted joins on both sides. */
    static uint8_t original[PIECE_CHUNK + sizeof(piece_span)];
    const uint64_t off = PIECE_CHUNK - 3, len = sizeof(piece_span) + 1;
    original[off] = original[PIECE_CHUNK] = original[off + len - 1] = '\n';
    for (unsigned backward = 0; backward <= 1; backward++) {
        fixture f = open_copy(original, sizeof original);
        const piece_view *v = piece_tree_view(f.tree);
        ph_spans want = {0};
        ph_describe(v, off, len, true, &want);
        ASSERT(want.n == 1 && want.nl[0].head == 1 && want.nl[0].tail == 2);
        size_t joins_before = joined_descriptions;
        for (uint64_t n = 1; n <= len; n++) {
            uint64_t at = backward ? off + len - n : off;
            ASSERT(erase_cmd(f.log, at, 1, n != 1, &idle, &idle) == UNDO_OK);
            undo_stats stats = undo_get_stats(f.log);
            ASSERT(stats.records == 1);
            size_t charge = n < sizeof(piece_span) ? (size_t)n : 0;
            ASSERT(stats.history_bytes == GROUP_RECORD_BYTES + charge);
            ASSERT(piece_add_length(f.tree) == (charge ? charge : sizeof(piece_span) - 1));
        }
        ASSERT(joined_descriptions == joins_before + len - 1);
        undo_change c;
        expect_replay = &want;
        ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1 && expect_replay == NULL);
        ASSERT(piece_length(v) == sizeof original);
        for (uint64_t i = off; i < off + len; i++) ASSERT(piece_byte_at(v, i) == original[i]);
        ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1);
        ASSERT(piece_length(v) == sizeof original - len);
        ph_spans_free(&want);
        close_fixture(f);
    }
}

static void erase_source_gap(void)
{
    /* (P) Removing the middle byte makes two formerly nonadjacent source
     * offsets consecutive in the document. Their burst needs two records. */
    const uint8_t original[] = "a!b";
    fixture f = open_copy(original, sizeof original - 1);
    const piece_view *v = piece_tree_view(f.tree);
    ASSERT(erase_cmd(f.log, 1, 1, false, &idle, &idle) == UNDO_OK);
    undo_clear(f.log);
    ASSERT(erase_cmd(f.log, 0, 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(erase_cmd(f.log, 0, 1, true, &idle, &idle) == UNDO_OK);
    ASSERT(undo_get_history(f.log).undo_groups == 1 && undo_get_stats(f.log).records == 2);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    text_eq(v, "ab");
    close_fixture(f);
}

static void reclaim_and_retry(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    size_t base = undo_get_stats(f.log).storage_bytes, before = ph_calls();
    ASSERT(undo_begin(f.log, &idle, false) == UNDO_OK);
    ph_refuse(true);
    ASSERT(undo_replace(f.log, 0, 0, (const uint8_t *)"x", 1) == UNDO_ERR_NOMEM);
    undo_abort(f.log);
    ph_refuse(false);
    ASSERT(ph_calls() > before && undo_get_stats(f.log).storage_bytes == base);
    text_eq(v, "");

    /* (P) More groups than v1's bound of 16 boundary visits plus its
     * record budget. A tiny cap forces the bulk detach and leaves retirements. */
    enum { separate = 2 * 16 };
    for (size_t i = 0; i < separate; i++) {
        ASSERT(insert_cmd(f.log, i, (const uint8_t *)"x", 1, false, &idle, &idle) == UNDO_OK);
    }
    undo_set_cap(f.log, 1);
    ASSERT(undo_get_history(f.log).undo_groups == 0);
    ASSERT(undo_get_history(f.log).evicted_groups == separate);
    ASSERT(piece_length(v) == separate);
    ASSERT(undo_get_stats(f.log).storage_bytes > base);
    ASSERT(undo_maintain(f.log, SIZE_MAX) != 0);
    ASSERT(undo_get_stats(f.log).storage_bytes == base);
    close_fixture(f);
}

static void sliced_abort(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    /* (P) Start at ADD mark C-1. The two-byte literal then straddles C.
     * A later slice failure rolls back the group. An independently retained
     * inside snapshot survives, and a retry still uses the same literal. */
    static uint8_t pad[PIECE_CHUNK - 3];
    ASSERT(insert_cmd(f.log, 0, pad, sizeof pad, false, &idle, &idle) == UNDO_OK);
    ASSERT(replace_cmd(f.log, 0, sizeof pad, (const uint8_t *)"a\n", 2, false, &idle, &idle) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == PIECE_CHUNK - 1);
    undo_clear(f.log);
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    /* (P) Force a refusal after capture grew the in-checkpoint ADD across C.
     * Rollback must discard both the tentative history and those ADD bytes. */
    fail_at_add_mark = PIECE_CHUNK + 1;
    ASSERT(undo_replace(f.log, 0, 2, (const uint8_t *)"b", 1) == UNDO_ERR_NOMEM);
    undo_abort(f.log);
    fail_at_add_mark = 0;
    ASSERT(piece_add_length(f.tree) == PIECE_CHUNK - 1);
    ASSERT(undo_get_history(f.log).undo_groups == 0);
    text_eq(v, "a\n");
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 2, (const uint8_t *)"b", 1) == UNDO_OK);
    ASSERT(undo_replace(f.log, 1, 0, (const uint8_t *)"c", 1) == UNDO_OK);
    ASSERT(undo_commit(f.log, &last) == UNDO_OK);
    const uint64_t add = piece_add_length(f.tree);
    undo_history h = undo_get_history(f.log);
    undo_stats s = undo_get_stats(f.log);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE);
    ASSERT(c.groups == 0 && c.len != 0 && !c.has_state);
    text_eq(v, "b");
    piece_snapshot *pre = piece_snapshot_retain(undo_replay_snapshot(f.log));
    text_eq(piece_snapshot_view(pre), "bc");
    piece_snapshot *inside;
    ASSERT(piece_snapshot_take(f.tree, &inside) == PIECE_OK);
    fail_replay = true;
    size_t calls = ph_calls();
    ph_refuse(true); /* Abort also succeeds with total base refusal. */
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_ERR_NOMEM);
    ph_refuse(false);
    fail_replay = false;
    ASSERT(ph_calls() == calls);
    ASSERT(c.groups == 0 && c.len == 0 && !c.has_state);
    ASSERT(undo_replay_snapshot(f.log) == NULL);
    ASSERT(piece_add_length(f.tree) == add);
    undo_history after = undo_get_history(f.log);
    ASSERT(after.undo_groups == h.undo_groups && after.redo_groups == h.redo_groups);
    ASSERT(after.group_serial == h.group_serial && after.evicted_groups == h.evicted_groups);
    ASSERT(undo_get_stats(f.log).history_bytes == s.history_bytes);
    text_eq(v, "bc");
    text_eq(piece_snapshot_view(inside), "b");
    text_eq(piece_snapshot_view(pre), "bc");
    piece_snapshot_release(pre);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1 && c.state.bytes[0] == 'a');
    text_eq(v, "a\n");
    ph_spans restore = {0};
    ph_describe(v, 0, 2, true, &restore);
    ASSERT(restore.n == 1 && restore.s[0].src == (PIECE_SPAN_ADD | (PIECE_CHUNK - 1)));
    ASSERT(restore.nl[0].head == 0 && restore.nl[0].tail == 1);
    ASSERT(undo_redo(f.log, 1, 0, &c) == UNDO_MORE);
    text_eq(v, "b");
    fail_replay = true;
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_ERR_NOMEM && c.len == 0);
    fail_replay = false;
    text_eq(v, "a\n");
    ASSERT(undo_get_history(f.log).redo_groups == 1);
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1 && c.state.bytes[0] == 'z');
    ASSERT(piece_add_length(f.tree) == add);
    text_eq(v, "bc");
    ph_spans_free(&restore);
    piece_snapshot_release(inside);
    close_fixture(f);
}

static void cap_boundary(void)
{
    /* (P, A20) The independent first-key charge tests exact admission
     * and eviction at a cap one byte smaller. */
    const size_t key_bytes = GROUP_RECORD_BYTES + 1;
    for (size_t cap = key_bytes - 1; cap <= key_bytes; cap++) {
        fixture f = open_copy(NULL, 0);
        undo_set_cap(f.log, cap);
        ASSERT(insert_cmd(f.log, 0, (const uint8_t *)"x", 1, false, &idle, &idle) == UNDO_OK);
        ASSERT(undo_get_stats(f.log).history_bytes == (cap == key_bytes ? key_bytes : 0));
        ASSERT(undo_get_history(f.log).evicted_groups == (cap == key_bytes ? 0u : 1u));
        text_eq(piece_tree_view(f.tree), "x");
        close_fixture(f);
    }
}

static void abandoned_add_lineage(void)
{
    /* (P) Equal-width, zero-LF strings make stale ADD reuse pass both the
     * bounds and count checks. Undo owns both sides of the command here. */
    const uint8_t lower[sizeof(piece_span)] = "aaaaaaaaaaaaaaaa";
    const uint8_t upper[sizeof(piece_span)] = "BBBBBBBBBBBBBBBB";
    fixture f = open_copy(lower, sizeof lower);
    const piece_view *v = piece_tree_view(f.tree);
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, sizeof lower, 0, upper, sizeof upper) == UNDO_OK);
    ASSERT(undo_replace(f.log, sizeof lower, sizeof upper, NULL, 0) == UNDO_OK);
    /* (G) Also take a literal under the same owned checkpoint. */
    ASSERT(undo_replace(f.log, 0, 1, NULL, 0) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == sizeof upper + 1);
    ASSERT(undo_get_stats(f.log).records == 0);
    undo_abort(f.log);
    ASSERT(piece_add_length(f.tree) == 0 && undo_get_history(f.log).undo_groups == 0);
    text_eq(v, "aaaaaaaaaaaaaaaa");

    ASSERT(insert_cmd(f.log, sizeof lower, lower, sizeof lower, false, &first, &last) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == sizeof upper);
    undo_change c;
    ASSERT(undo_undo(f.log, 2, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1);
    text_eq(v, "aaaaaaaaaaaaaaaa");
    close_fixture(f);
}

static void abandoned_command_keeps_redo(void)
{
    fixture f = open_copy(NULL, 0);
    ASSERT(insert_cmd(f.log, 0, (const uint8_t *)"a", 1, false, &idle, &idle) == UNDO_OK);
    ASSERT(insert_cmd(f.log, 1, (const uint8_t *)"b", 1, false, &idle, &idle) == UNDO_OK);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    /* (P) The current two groups exactly fill the cap. An unpublished
     * command cannot branch or evict either one, even if it would be larger. */
    undo_set_cap(f.log, 2 * (GROUP_RECORD_BYTES + 1));
    undo_history h = undo_get_history(f.log);
    ASSERT(undo_begin(f.log, &idle, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 1, 0, (const uint8_t *)"xyz", 3) == UNDO_OK);
    ASSERT(undo_get_history(f.log).redo_groups == 1);
    undo_abort(f.log);
    undo_history restored = undo_get_history(f.log);
    ASSERT(restored.undo_groups == 1 && restored.redo_groups == 1);
    ASSERT(restored.evicted_groups == h.evicted_groups && restored.group_serial == h.group_serial);
    ASSERT(undo_redo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    text_eq(piece_tree_view(f.tree), "ab");
    close_fixture(f);
}

static void mapped_without_validator(void)
{
    ph_mapping mp;
    const uint8_t sample[] = "x";
    fixture f;
    ASSERT(ph_map_open(&mp, sample, sizeof sample - 1));
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_mapped(f.reserve, &mp.b, &f.tree) == PIECE_OK);
    if (undo_create(&f.log, f.tree, SIZE_MAX, NULL, NULL) == UNDO_OK) close_fixture(f);
}

static void wrote_during_yield(void)
{
    fixture f = open_copy(NULL, 0);
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 0, (const uint8_t *)"ab", 2) == UNDO_OK);
    /* (P) An insertion at the front keeps two separate replay mutations. */
    ASSERT(undo_replace(f.log, 0, 0, (const uint8_t *)"c", 1) == UNDO_OK);
    ASSERT(undo_commit(f.log, &last) == UNDO_OK);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE);
    text_eq(piece_tree_view(f.tree), "ab");
    /* Deliberate contract violation. The bytes remain identical. */
    ASSERT(piece_replace(f.tree, 0, 1, (const uint8_t *)"a", 1) == PIECE_OK);
    (void)undo_undo(f.log, 1, 0, &c);
}

static void later_group_refusal(void)
{
    fixture f = open_copy(NULL, 0);
    const piece_view *v = piece_tree_view(f.tree);
    /* (P) Two records in the older group and one in the newest. The
     * third mutation fails after the older group has begun to rewind. */
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 0, (const uint8_t *)"b", 1) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 0, (const uint8_t *)"a", 1) == UNDO_OK);
    ASSERT(undo_commit(f.log, &idle) == UNDO_OK);
    ASSERT(insert_cmd(f.log, 2, (const uint8_t *)"c", 1, false, &last, &idle) == UNDO_OK);
    const uint64_t add = piece_add_length(f.tree);
    fail_replay_step = 3;
    undo_change c;
    ASSERT(undo_undo(f.log, 2, OS_DEADLINE_NEVER, &c) == UNDO_ERR_NOMEM);
    ASSERT(fail_replay_step == 0);
    ASSERT(c.groups == 1 && c.has_state && c.state.bytes[0] == 'z');
    ASSERT(c.off == 2 && c.len == 1);
    text_eq(v, "ab");
    ASSERT(piece_add_length(f.tree) == add);
    undo_history h = undo_get_history(f.log);
    ASSERT(h.undo_groups == 1 && h.redo_groups == 1);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    text_eq(v, "");
    close_fixture(f);
}

static void check_rank_eof(const piece_view *v)
{
    uint64_t rank, start, before;
    /* (P) The single LF at offset one starts line one at offset two. */
    ASSERT(piece_lines_before(v, piece_length(v), &rank) && rank == 1);
    ASSERT(piece_lines_start(v, 1, &start, &before) && start == 2 && before == 1);
}

static void late_fault_rollback(void)
{
    const uint8_t sample[] = "a\nb";
    ph_mapping mp;
    ASSERT(ph_map_open(&mp, sample, sizeof sample - 1));
    fixture f;
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_mapped(f.reserve, &mp.b, &f.tree) == PIECE_OK);
    bool trust = true;
    ASSERT(undo_create(&f.log, f.tree, SIZE_MAX, trusted, &trust) == UNDO_OK);
    ASSERT(ph_lines_complete(f.tree, &mp, "late fault resolve"));
    const piece_view *v = piece_tree_view(f.tree);

    /* (P) Capture and replacement append one byte each. Inject a late
     * fault after the wrapped piece write succeeds, before publication. */
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    late_forward = PH_POLL_FAULTED;
    ASSERT(undo_replace(f.log, 0, 1, (const uint8_t *)"x", 1) == UNDO_OK);
    ASSERT(piece_add_length(f.tree) == 2 && undo_get_stats(f.log).records == 0);
    text_eq(v, "x\nb");
    ASSERT(undo_commit(f.log, &last) == UNDO_ERR_CONFLICT);
    late_forward = PH_POLL_REAL;
    ph_poll_set(PH_POLL_REAL);
    text_eq(v, "a\nb");
    check_rank_eof(v);
    ASSERT(piece_add_length(f.tree) == 0 && undo_get_history(f.log).group_serial == 0);

    /* (P) Yield after removing the second edit, then inject a failed poll
     * after the resumed splice. The whole replay group must rewind. */
    ASSERT(undo_begin(f.log, &first, false) == UNDO_OK);
    ASSERT(undo_replace(f.log, 0, 1, (const uint8_t *)"x", 1) == UNDO_OK);
    ASSERT(undo_replace(f.log, sizeof sample - 1, 0, (const uint8_t *)"y", 1) == UNDO_OK);
    ASSERT(undo_commit(f.log, &last) == UNDO_OK);
    const uint64_t add = piece_add_length(f.tree);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_MORE);
    text_eq(v, "x\nb");
    late_replay = PH_POLL_ERROR;
    ASSERT(undo_undo(f.log, 1, 0, &c) == UNDO_ERR_CONFLICT);
    late_replay = PH_POLL_REAL;
    ph_poll_set(PH_POLL_REAL);
    ASSERT(c.groups == 0 && c.len == 0 && !c.has_state);
    ASSERT(undo_replay_snapshot(f.log) == NULL);
    ASSERT(piece_add_length(f.tree) == add);
    undo_history h = undo_get_history(f.log);
    ASSERT(h.undo_groups == 1 && h.redo_groups == 0);
    text_eq(v, "x\nby");
    check_rank_eof(v);
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK && c.groups == 1);
    text_eq(v, "a\nb");
    check_rank_eof(v);
    close_fixture(f);
    ph_map_unref(&mp);
}

/* The A13 bound attained through undo (real tree): two counted mapped chunks
 * with an LF at every even offset; erase [H, C + H), H = 32,768. Undo
 * describes it with counts (two H-byte cuts), the deletion cuts the same
 * two places (2 x H), and the undo reinserts at a seam whose survivors have a
 * C-byte source gap, so nothing is cut: 4H = 131,072 B (P). */
static void midpoint_reads(void)
{
    static uint8_t sample[2u * PIECE_CHUNK];
    for (size_t i = 0; i < sizeof sample; i++) sample[i] = i % 2u ? 'a' : '\n';
    const uint64_t half = PIECE_CHUNK / 2u;
    ph_mapping mp;
    ASSERT(ph_map_open(&mp, sample, sizeof sample));
    fixture f;
    ASSERT(piece_reserve_create(&f.reserve) == PIECE_OK);
    ASSERT(piece_open_mapped(f.reserve, &mp.b, &f.tree) == PIECE_OK);
    bool trust = true;
    ASSERT(undo_create(&f.log, f.tree, SIZE_MAX, trusted, &trust) == UNDO_OK);
    ASSERT(ph_lines_complete(f.tree, &mp, "midpoint resolve"));
    const piece_view *v = piece_tree_view(f.tree);
    piece_test_reset_reads(f.tree);
    ASSERT(erase_cmd(f.log, half, PIECE_CHUNK, false, &first, &last) == UNDO_OK);
    undo_change c;
    ASSERT(undo_undo(f.log, 1, OS_DEADLINE_NEVER, &c) == UNDO_OK);
    size_t mapped_read = piece_test_measure(f.tree).mapped_reads;
    ASSERT(mapped_read == 4u * half && mapped_read <= 2u * PIECE_CHUNK);
    uint64_t rank;
    ASSERT(piece_length(v) == sizeof sample && piece_lines_before(v, sizeof sample, &rank) &&
           rank == sizeof sample / 2u);
    close_fixture(f);
    ph_map_unref(&mp);
    printf("undo mapped reads (P): describe + delete + undo %zu B <= 131,072 B (G, A13)\n", mapped_read);
}

int main(void)
{
    ASSERT(assert_death(mapped_without_validator, "src/undo/undo.c",
                        "!piece_view_backing(piece_tree_view(tree)) || validate"));
    ASSERT(assert_death(wrote_during_yield, "src/undo/undo.c", "now == u->replay.guard"));
    one_span_mapped();
    midpoint_reads();
    late_fault_rollback();
    threshold();
    external_store();
    typing_record();
    typing_abort_reuse();
    typing_add_gap();
    burst_and_cap();
    erase_bursts();
    erase_reverses_direction();
    erase_threshold();
    erase_source_gap();
    reclaim_and_retry();
    sliced_abort();
    cap_boundary();
    abandoned_add_lineage();
    abandoned_command_keeps_redo();
    later_group_refusal();
    ASSERT(ph_failures == 0);
    puts("undo_test: PASS");
    return EXIT_SUCCESS;
}
