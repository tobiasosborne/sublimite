/* piece suite (v2-0fv.7, docs/decisions/v2-0fv.7.md §8). Every piece.h call
 * of the frozen sections is checked against the flat model
 * (tests/piece_model.h) through tests/piece_harness.h; the directed cases
 * also carry literal expectations, which check the model itself. The Lines
 * section has its own part at the end, checked against the truth of the
 * reference bytes.
 *
 * The implementation under test is the real tree (v2-0fv.17): libtext.a's
 * piece.c, linked through its PIECE_TEST_HOOKS variant
 * build/<cfg>/src/piece/piece_test_hooks.o for the height limit and the
 * live revision. The model is the reference only. Base refusal comes from
 * -Wl,--wrap=os_vm_reserve (LDFLAGS_piece_test). The shape, admission and
 * read-count checks that only a real tree has are tests/piece_shape_test.c. */
#include "piece_harness.h"

#include "indent/indent.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define CHECK PH_CHECK
#define SPAN(src, len) ((piece_span){(src), (len)})
#define ADD(a) (PIECE_SPAN_ADD | (uint64_t)(a))

static const uint8_t *str(const char *s) { return (const uint8_t *)s; }

/* ---- literal expectations ------------------------------------------------ */

static void expect_text(const piece_view *v, const char *want, const char *tag)
{
    size_t n = strlen(want);
    CHECK(piece_length(v) == n, "%s: length %llu, want %zu", tag, PH_U(piece_length(v)), n);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *p;
    size_t k, at = 0;
    bool same = true;
    while ((k = piece_iter_next(&it, &p)) != 0) {
        if (at + k > n || memcmp(p, want + at, k) != 0) same = false;
        at += k;
    }
    CHECK(same && at == n, "%s: bytes differ from \"%s\"", tag, want);
}

static void expect_spans(const piece_view *v, const piece_span *want, size_t n, const char *tag)
{
    ph_spans got = {0};
    ph_describe(v, 0, piece_length(v), false, &got);
    CHECK(got.n == n, "%s: %zu spans, want %zu", tag, got.n, n);
    for (size_t i = 0; i < got.n && i < n; i++)
        CHECK(ph_span_eq(got.s[i], want[i]), "%s: span %zu {%llx, %llu}, want {%llx, %llu}", tag, i,
              PH_U(got.s[i].src), PH_U(got.s[i].len), PH_U(want[i].src), PH_U(want[i].len));
    ph_spans_free(&got);
}

static uint8_t *read_all(const piece_view *v)
{
    uint64_t n = piece_length(v);
    uint8_t *b = malloc(n ? (size_t)n : 1u);
    ASSERT(b != NULL);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *p;
    size_t k;
    uint64_t at = 0;
    while ((k = piece_iter_next(&it, &p)) != 0) {
        ASSERT(at + k <= n);
        memcpy(b + at, p, k);
        at += k;
    }
    return b;
}

/* A reserve and a tree over a copy of s; false (and a failure) if refused. */
static bool open_text(piece_reserve **r, piece_tree **t, const uint8_t *s, size_t n)
{
    *r = NULL;
    *t = NULL;
    if (piece_reserve_create(r) != PIECE_OK) { CHECK(0, "piece_reserve_create refused"); return false; }
    if (piece_open_copy(*r, s, n, t) != PIECE_OK) {
        CHECK(0, "piece_open_copy refused");
        piece_reserve_destroy(*r);
        *r = NULL;
        return false;
    }
    return true;
}

static void close_text(piece_reserve *r, piece_tree *t)
{
    if (t) piece_close(t);
    if (r) piece_reserve_destroy(r);
}

/* ---- directed cases ------------------------------------------------------ */

static void t_open_copy(void)
{
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, str("hello\nworld"), 11)) return;
    const piece_view *v = piece_tree_view(t);
    expect_text(v, "hello\nworld", "open");
    CHECK(piece_byte_at(v, 5) == '\n' && piece_byte_at(v, 10) == 'd', "byte_at");
    expect_spans(v, (piece_span[]){SPAN(0, 11)}, 1, "open spans");
    CHECK(piece_add_length(t) == 0 && piece_view_backing(v) == NULL, "fresh tree");
    CHECK(piece_tree_view(t) == v, "the live view pointer is fixed");
    close_text(r, t);

    if (!open_text(&r, &t, NULL, 0)) return;
    expect_text(piece_tree_view(t), "", "empty open");
    expect_spans(piece_tree_view(t), NULL, 0, "empty spans");
    close_text(r, t);
}

static void t_replace(void)
{
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, str("hello\nworld"), 11)) return;
    const piece_view *v = piece_tree_view(t);
    CHECK(piece_replace(t, 5, 0, str("XY"), 2) == PIECE_OK, "insert");
    expect_text(v, "helloXY\nworld", "insert");
    expect_spans(v, (piece_span[]){SPAN(0, 5), SPAN(ADD(0), 2), SPAN(5, 6)}, 3, "insert spans");
    CHECK(piece_add_length(t) == 2, "ADD length after insert");
    /* Typing at the end of the run continues its coordinates: one span. */
    CHECK(piece_replace(t, 7, 0, str("Z"), 1) == PIECE_OK, "type");
    expect_spans(v, (piece_span[]){SPAN(0, 5), SPAN(ADD(0), 3), SPAN(5, 6)}, 3, "typed run");
    CHECK(piece_replace(t, 1, 2, NULL, 0) == PIECE_OK, "delete");
    expect_text(v, "hloXYZ\nworld", "delete");
    expect_spans(v, (piece_span[]){SPAN(0, 1), SPAN(3, 2), SPAN(ADD(0), 3), SPAN(5, 6)}, 4, "delete spans");
    CHECK(piece_replace(t, 0, 1, str("H"), 1) == PIECE_OK, "overwrite");
    expect_text(v, "HloXYZ\nworld", "overwrite");
    expect_spans(v, (piece_span[]){SPAN(ADD(3), 1), SPAN(3, 2), SPAN(ADD(0), 3), SPAN(5, 6)}, 4,
                 "overwrite spans");
    CHECK(piece_add_length(t) == 4, "ADD length after overwrite");
    /* Delete everything, then rebuild by reference from ORIGINAL. */
    CHECK(piece_replace(t, 0, 12, NULL, 0) == PIECE_OK, "delete all");
    expect_text(v, "", "delete all");
    CHECK(piece_replace_spans(t, 0, 0, (piece_span[]){SPAN(6, 5), SPAN(0, 6)}, NULL, 2) == PIECE_OK,
          "reinsert");
    expect_text(v, "worldhello\n", "reinsert");
    close_text(r, t);
}

/* Coordinates that continue each other merge into one span, so a delete and
 * its reinsertion by reference restore the description exactly. */
static void t_rejoin(void)
{
    uint8_t b[1000];
    ph_fill(b, sizeof b, 7);
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, b, sizeof b)) return;
    const piece_view *v = piece_tree_view(t);
    CHECK(piece_replace(t, 100, 100, NULL, 0) == PIECE_OK, "delete");
    expect_spans(v, (piece_span[]){SPAN(0, 100), SPAN(200, 800)}, 2, "cut");
    CHECK(piece_replace_spans(t, 100, 0, (piece_span[]){SPAN(100, 100)}, NULL, 1) == PIECE_OK, "reinsert");
    expect_spans(v, (piece_span[]){SPAN(0, 1000)}, 1, "rejoined");
    close_text(r, t);
}

static void t_capture(void)
{
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, str("abcdefgh"), 8)) return;
    const piece_view *v = piece_tree_view(t);
    piece_snapshot *s0;
    CHECK(piece_snapshot_take(t, &s0) == PIECE_OK, "take");
    piece_iter it;
    piece_iter_init(&it, v, 0);
    piece_span sp = {0};
    CHECK(piece_capture(t, 2, 3, &sp) == PIECE_OK, "capture");
    CHECK(ph_span_eq(sp, SPAN(ADD(0), 3)) && piece_add_length(t) == 3, "capture coordinates");
    expect_text(v, "abcdefgh", "capture leaves the text");
    const uint8_t *p;
    CHECK(piece_iter_next(&it, &p) >= 1 && p[0] == 'a', "capture leaves live iterators valid");
    piece_snapshot *s1;
    CHECK(piece_snapshot_take(t, &s1) == PIECE_OK && s1 == s0, "capture keeps the snapshot cache");
    CHECK(piece_replace_spans(t, 0, 0, &sp, NULL, 1) == PIECE_OK, "reinsert the literal");
    expect_text(v, "cdeabcdefgh", "literal reinserted");
    piece_snapshot_release(s0);
    piece_snapshot_release(s1);
    close_text(r, t);
}

static void t_snapshots(void)
{
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, str("one\ntwo\n"), 8)) return;
    piece_snapshot *a, *b, *c;
    CHECK(piece_snapshot_take(t, &a) == PIECE_OK, "take a");
    CHECK(piece_snapshot_take(t, &b) == PIECE_OK && b == a, "unchanged text: cache hit");
    CHECK(piece_replace(t, 0, 0, NULL, 0) == PIECE_OK, "empty edit");
    CHECK(piece_snapshot_take(t, &c) == PIECE_OK && c == a, "the empty edit keeps the cache");
    piece_snapshot_release(b);
    piece_snapshot_release(c);
    CHECK(piece_replace(t, 3, 1, str(" & "), 3) == PIECE_OK, "edit");
    expect_text(piece_snapshot_view(a), "one\ntwo\n", "a keeps its text");
    expect_text(piece_tree_view(t), "one & two\n", "live text");
    CHECK(piece_snapshot_take(t, &b) == PIECE_OK && b != a, "edit: fresh snapshot");
    piece_snapshot *a2 = piece_snapshot_retain(a);
    CHECK(a2 == a, "retain returns its argument");
    piece_close(t);
    expect_text(piece_snapshot_view(a), "one\ntwo\n", "a outlives its tree");
    expect_text(piece_snapshot_view(b), "one & two\n", "b outlives its tree");
    expect_spans(piece_snapshot_view(b), (piece_span[]){SPAN(0, 3), SPAN(ADD(0), 3), SPAN(4, 4)}, 3,
                 "b's description after close");
    piece_snapshot_release(a);
    piece_snapshot_release(a2);
    piece_snapshot_release(b);
    piece_reserve_destroy(r);
}

/* The private ADD boundary at the API: an abort lowers the ADD length, new
 * text reuses those coordinates, and a snapshot taken inside keeps its own
 * bytes at them. */
static void t_checkpoint(void)
{
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, str("0123456789"), 10)) return;
    const piece_view *v = piece_tree_view(t);
    CHECK(piece_replace(t, 10, 0, str("ab"), 2) == PIECE_OK, "before begin");
    ph_spans before = {0};
    ph_describe(v, 0, 12, false, &before);
    CHECK(piece_checkpoint_begin(t) == PIECE_OK, "begin");
    CHECK(piece_replace(t, 0, 0, str("cd"), 2) == PIECE_OK, "inside");
    piece_snapshot *in;
    CHECK(piece_snapshot_take(t, &in) == PIECE_OK, "take inside");
    size_t c0 = ph_calls();
    ph_refuse(true);
    piece_checkpoint_abort(t);
    ph_refuse(false);
    CHECK(ph_calls() == c0, "abort reserved storage");
    expect_text(v, "0123456789ab", "abort restores the text");
    CHECK(piece_add_length(t) == 2, "abort restores the ADD length");
    expect_spans(v, (piece_span[]){SPAN(0, 10), SPAN(ADD(0), 2)}, 2, "abort restores the description");
    CHECK(piece_replace(t, 0, 0, str("EF"), 2) == PIECE_OK, "after abort");
    expect_spans(v, (piece_span[]){SPAN(ADD(2), 2), SPAN(0, 10), SPAN(ADD(0), 2)}, 3, "ADD reused");
    expect_text(v, "EF0123456789ab", "new text at the reused coordinates");
    expect_text(piece_snapshot_view(in), "cd0123456789ab", "the inside snapshot keeps its bytes");
    /* Spans from before begin stay valid. */
    CHECK(piece_replace_spans(t, 0, 2, before.s, NULL, before.n) == PIECE_OK, "old spans");
    expect_text(v, "0123456789ab0123456789ab", "old spans after abort");
    /* Commit keeps the text and the cache. */
    piece_snapshot *s0, *s1;
    CHECK(piece_snapshot_take(t, &s0) == PIECE_OK, "take");
    CHECK(piece_checkpoint_begin(t) == PIECE_OK, "begin 2");
    c0 = ph_calls();
    ph_refuse(true);
    piece_checkpoint_commit(t);
    ph_refuse(false);
    CHECK(ph_calls() == c0, "commit reserved storage");
    CHECK(piece_snapshot_take(t, &s1) == PIECE_OK && s1 == s0, "begin and commit keep the cache");
    CHECK(piece_checkpoint_begin(t) == PIECE_OK, "begin 3");
    CHECK(piece_replace(t, 0, 24, NULL, 0) == PIECE_OK, "delete inside");
    piece_checkpoint_commit(t);
    expect_text(v, "", "commit keeps the edit");
    piece_snapshot_release(s0);
    piece_snapshot_release(s1);
    piece_snapshot_release(in);
    ph_spans_free(&before);
    close_text(r, t);
}

/* ---- the span-sequence inverse (signed 10(b), A13) ------------------------ */

typedef enum inv_kind { INV_LITERAL, INV_SPANS } inv_kind;

typedef struct inverse {
    inv_kind kind;
    ph_spans spans;
} inverse;

/* Undo's rule: a literal exactly when 16 x spans > length. */
static void inverse_delete(piece_tree *t, uint64_t off, uint64_t len, inverse *inv)
{
    const piece_view *v = piece_tree_view(t);
    uint64_t s = ph_count_spans(v, off, len);
    inv->spans = (ph_spans){0};
    if (s > len / 16u) {
        inv->kind = INV_LITERAL;
        piece_span sp;
        CHECK(piece_capture(t, off, len, &sp) == PIECE_OK, "capture literal");
        ph_spans_push(&inv->spans, sp, (piece_span_nl){PIECE_NL_UNKNOWN, PIECE_NL_UNKNOWN});
    } else {
        inv->kind = INV_SPANS;
        ph_describe(v, off, len, true, &inv->spans);
        CHECK(inv->spans.n == s, "described %zu spans, counted %llu", inv->spans.n, PH_U(s));
    }
    CHECK(piece_replace(t, off, len, NULL, 0) == PIECE_OK, "delete");
}

static void inverse_undo(piece_tree *t, uint64_t off, inverse *inv)
{
    CHECK(piece_replace_spans(t, off, 0, inv->spans.s, inv->spans.nl, inv->spans.n) == PIECE_OK, "undo");
    ph_spans_free(&inv->spans);
}

/* Deletes and undoes [off, off + len); checks the kind, the bytes copied into
 * ADD (min(len, 16 x spans) at most) and the exact restoration. */
static void inverse_case(piece_tree *t, uint64_t off, uint64_t len, inv_kind want, const char *tag)
{
    const piece_view *v = piece_tree_view(t);
    uint64_t n = piece_length(v), a0 = piece_add_length(t), s = ph_count_spans(v, off, len);
    ph_spans before = {0};
    ph_describe(v, 0, n, false, &before);
    uint8_t *bytes = read_all(v);

    inverse inv;
    inverse_delete(t, off, len, &inv);
    CHECK(inv.kind == want, "%s: kind %d, want %d (spans %llu, length %llu)", tag, (int)inv.kind, (int)want,
          PH_U(s), PH_U(len));
    uint64_t copied = piece_add_length(t) - a0, bound = len < 16u * s ? len : 16u * s;
    CHECK(copied <= bound, "%s: copied %llu bytes, bound %llu", tag, PH_U(copied), PH_U(bound));
    CHECK(inv.kind == INV_SPANS ? copied == 0 : copied == len, "%s: copied %llu", tag, PH_U(copied));
    CHECK(piece_length(v) == n - len, "%s: deleted length", tag);
    inverse_undo(t, off, &inv);
    uint8_t *after = read_all(v);
    CHECK(piece_length(v) == n && memcmp(after, bytes, (size_t)n) == 0, "%s: undo restores the bytes", tag);
    free(after);
    if (want == INV_SPANS) {
        ph_spans restored = {0};
        ph_describe(v, 0, n, false, &restored);
        bool same = restored.n == before.n;
        for (size_t i = 0; same && i < restored.n; i++) same = ph_span_eq(restored.s[i], before.s[i]);
        CHECK(same, "%s: a span inverse restores the coordinates", tag);
        ph_spans_free(&restored);
    }
    ph_spans_free(&before);
    free(bytes);
}

static void t_inverse(void)
{
    enum { N = 1 << 20 };
    uint8_t *b = malloc(N);
    ASSERT(b != NULL);
    ph_fill(b, N, 3);
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, b, N)) { free(b); return; }
    inverse_case(t, 1000, 1, INV_LITERAL, "one byte: literal");
    inverse_case(t, 100, 3u << 18, INV_SPANS, "768 KiB over three runs: spans");
    inverse_case(t, 5000, 15, INV_LITERAL, "16 x 1 > 15: literal");
    inverse_case(t, 6000, 16, INV_SPANS, "16 x 1 = 16: spans (tie)");
    inverse_case(t, 7000, 17, INV_SPANS, "16 x 1 < 17: spans");

    /* Nine long runs: 16 x 9 = 144 <= 90 KiB, spans. Nine one-byte runs:
     * 144 > 9, a literal. */
    piece_span rev[9];
    for (int i = 0; i < 9; i++) rev[i] = SPAN((uint64_t)(8 - i) * 10240u, 10240u);
    CHECK(piece_replace_spans(t, 0, piece_length(piece_tree_view(t)), rev, NULL, 9) == PIECE_OK, "nine runs");
    inverse_case(t, 0, 9u * 10240u, INV_SPANS, "nine long runs: spans");
    for (int i = 0; i < 9; i++) rev[i] = SPAN((uint64_t)(8 - i) * 2u, 1u);
    CHECK(piece_replace_spans(t, 0, 0, rev, NULL, 9) == PIECE_OK, "nine bytes");
    inverse_case(t, 0, 9, INV_LITERAL, "nine one-byte runs: literal");
    /* Mixed ORIGINAL and ADD runs. */
    CHECK(piece_replace(t, 4, 0, str("xyz"), 3) == PIECE_OK, "mixed");
    inverse_case(t, 2, 12, INV_LITERAL, "mixed, short: literal");
    inverse_case(t, 0, 10240u * 2u, INV_SPANS, "mixed, long: spans");
    close_text(r, t);

    /* A mapped ORIGINAL: a one-span delete and its undo copy nothing. */
    ph_mapping map;
    if (!ph_map_open(&map, b, N)) { CHECK(0, "mapped fixture"); free(b); return; }
    if (piece_reserve_create(&r) == PIECE_OK) {
        if (piece_open_mapped(r, &map.b, &t) == PIECE_OK) {
            inverse_case(t, 777, N - 1554u, INV_SPANS, "mapped, one span: spans");
            inverse_case(t, 4096, 1, INV_LITERAL, "mapped, one byte: literal");
            piece_close(t);
        } else {
            CHECK(0, "piece_open_mapped refused");
        }
        piece_reserve_destroy(r);
    } else {
        CHECK(0, "piece_reserve_create refused");
    }
    CHECK(atomic_load(&map.acquires) == atomic_load(&map.releases), "mapped fixture balanced");
    ph_map_unref(&map);
    free(b);
}

/* ---- refusal ----------------------------------------------------------------- */

static void t_nomem(void)
{
    piece_reserve *r = (piece_reserve *)&r;
    ph_refuse(true);
    piece_err e = piece_reserve_create(&r);
    ph_refuse(false);
    CHECK(e == PIECE_ERR_NOMEM && r == NULL, "reserve create under refusal: %d", (int)e);
    if (piece_reserve_create(&r) != PIECE_OK) { CHECK(0, "piece_reserve_create refused"); return; }
    piece_tree *t = (piece_tree *)&t;
    ph_refuse(true);
    e = piece_open_copy(r, str("0123456789"), 10, &t);
    ph_refuse(false);
    CHECK(e == PIECE_ERR_NOMEM && t == NULL, "open under refusal: %d", (int)e);

    /* A 1 MiB insert right after open needs 16 ADD chunks (P), more than the
     * top-up's one boundary chunk: refused while base refuses, done after. */
    if (piece_open_copy(r, NULL, 0, &t) != PIECE_OK) { CHECK(0, "open refused"); piece_reserve_destroy(r); return; }
    enum { M = 1 << 20 };
    uint8_t *b = malloc(M);
    ASSERT(b != NULL);
    ph_fill(b, M, 5);
    ph_refuse(true);
    e = piece_replace(t, 0, 0, b, M);
    ph_refuse(false);
    CHECK(e == PIECE_ERR_NOMEM, "1 MiB insert under refusal: %d", (int)e);
    CHECK(piece_length(piece_tree_view(t)) == 0 && piece_add_length(t) == 0, "refusal changed nothing");
    CHECK(piece_replace(t, 0, 0, b, M) == PIECE_OK, "the same insert once base grants");
    CHECK(piece_add_length(t) == M && piece_length(piece_tree_view(t)) == M, "after the insert");

    /* What allocates nothing succeeds under refusal and reserves nothing. */
    piece_snapshot *s, *s2;
    CHECK(piece_snapshot_take(t, &s) == PIECE_OK, "take");
    CHECK(piece_checkpoint_begin(t) == PIECE_OK, "begin");
    size_t c0 = ph_calls();
    ph_refuse(true);
    const piece_view *v = piece_tree_view(t);
    uint8_t first = piece_byte_at(v, 0);
    uint64_t spans = ph_count_spans(v, 0, M);
    e = piece_snapshot_take(t, &s2);
    piece_snapshot *s3 = piece_snapshot_retain(s);
    piece_snapshot_release(s3);
    piece_err empty = piece_replace(t, 7, 0, NULL, 0);
    (void)piece_reclaim(t, 16);
    piece_checkpoint_abort(t);
    ph_refuse(false);
    CHECK(first == b[0] && spans == 1, "reads under refusal");
    CHECK(e == PIECE_OK && s2 == s, "cached take under refusal");
    CHECK(empty == PIECE_OK, "empty edit under refusal");
    CHECK(ph_calls() == c0, "%llu reservations by calls that allocate nothing", PH_U(ph_calls() - c0));

    /* A refused top-up changes nothing; the next command admits itself. */
    ph_refuse(true);
    e = piece_replenish(t);
    ph_refuse(false);
    CHECK(e == PIECE_OK || e == PIECE_ERR_NOMEM, "replenish: %d", (int)e);
    CHECK(piece_length(v) == M && piece_add_length(t) == M, "refused top-up changed nothing");
    CHECK(piece_replenish(t) == PIECE_OK, "replenish once base grants");
    CHECK(piece_replace(t, 0, 1, str("Q"), 1) == PIECE_OK, "next command");
    piece_snapshot_release(s);
    piece_snapshot_release(s2);
    free(b);
    close_text(r, t);
}

/* ---- height limit: the half every implementation shares (§8) ------------- */

static void t_height(void)
{
    ph_state st;
    if (!ph_open(&st, NULL, 0, 0, NULL)) { ph_close(&st); return; }
    piece_test_set_height_limit(1);
    st.height_limited = true;
    ph_op_replace(&st, 0, 0, str("x"), 1, false);
    ph_spans seed = {0};
    ph_describe(piece_tree_view(st.t), 0, 1, true, &seed);
    /* (P) The real tree refuses at its 205th one-byte descriptor at limit 1
     * (25 + 15 x 12); 400 prepends pass that on any tree. */
    unsigned refused = 0;
    for (unsigned i = 0; i < 400; i++) {
        piece_err e = piece_replace_spans(st.t, 0, 0, seed.s, seed.nl, 1);
        if (e == PIECE_OK) {
            ASSERT(pm_replace_spans(st.m, 0, 0, seed.s, NULL, 1) == PIECE_OK);
        } else {
            CHECK(e == PIECE_ERR_RANGE, "prepend %u: %d", i, (int)e);
            refused++;
            ph_check_tree(&st, 2, "refused prepend changed nothing");
            /* Removing a whole leading descriptor never grows the root. */
            CHECK(piece_replace(st.t, 0, 1, NULL, 0) == PIECE_OK, "a fitting edit after a refusal");
            ASSERT(pm_replace(st.m, 0, 1, NULL, 0) == PIECE_OK);
        }
        if (i % 50 == 0) ph_check_tree(&st, 1, "prepend trace");
    }
    ph_check_tree(&st, 4, "after the trace");
    /* Reopening the saved text repacks it. */
    uint64_t n;
    uint8_t *saved = ph_bytes_model(pm_tree_view(st.m), &n);
    piece_tree *again;
    CHECK(piece_open_copy(st.r, saved, (size_t)n, &again) == PIECE_OK, "reopen");
    CHECK(piece_replace(again, 0, 0, str("y"), 1) == PIECE_OK, "an edit after reopening");
    piece_close(again);
    free(saved);
    piece_test_set_height_limit(PIECE_MAX_HEIGHT);
    printf("t_height: %u refusals over 400 prepends at limit 1\n", refused);
    ph_spans_free(&seed);
    ph_close(&st);
}

/* ---- a mapped ORIGINAL ---------------------------------------------------- */

static void t_mapped(void)
{
    enum { N = 3 * PIECE_CHUNK + 123 };
    uint8_t *b = malloc(N);
    ASSERT(b != NULL);
    ph_fill(b, N, 9);
    ph_mapping map;
    if (!ph_map_open(&map, b, N)) { CHECK(0, "mapped fixture"); free(b); return; }
    piece_reserve *r;
    piece_tree *t;
    if (piece_reserve_create(&r) != PIECE_OK) { CHECK(0, "reserve refused"); goto out; }
    if (piece_open_mapped(r, &map.b, &t) != PIECE_OK) { CHECK(0, "open_mapped refused"); piece_reserve_destroy(r); goto out; }
    CHECK(atomic_load(&map.acquires) == 1, "one owner per core: %d", atomic_load(&map.acquires));
    const piece_view *v = piece_tree_view(t);
    const piece_backing *bk = piece_view_backing(v);
    CHECK(bk && bk->bytes == map.b.bytes && bk->len == N && bk->fault.gen == map.id.gen, "backing");
    uint64_t epoch = 1;
    CHECK(bk && os_fault_poll(bk->fault, &epoch) == OS_OK && epoch == 0, "pristine backing");
    expect_spans(v, (piece_span[]){SPAN(0, N)}, 1, "mapped spans");
    CHECK(piece_byte_at(v, N - 1) == b[N - 1], "last byte");
    piece_snapshot *s;
    CHECK(piece_snapshot_take(t, &s) == PIECE_OK, "take");
    CHECK(piece_replace(t, 10, 2 * PIECE_CHUNK, str("--"), 2) == PIECE_OK, "edit");
    piece_close(t);
    CHECK(atomic_load(&map.releases) == 0, "a snapshot keeps the backing");
    CHECK(piece_view_backing(piece_snapshot_view(s)) != NULL, "snapshot backing after close");
    piece_snapshot_release(s);
    CHECK(atomic_load(&map.releases) == 1, "the last owner releases the backing once: %d",
          atomic_load(&map.releases));
    piece_reserve_destroy(r);
out:
    ph_map_unref(&map);
    free(b);
}

/* ---- threads: snapshot readers, worker releases after close --------------- */

typedef struct reader {
    piece_snapshot *s;
    const uint8_t *want;
    uint64_t n;
    atomic_int *bad;
} reader;

static void reader_run(void *arg)
{
    reader *rd = arg;
    const piece_view *v = piece_snapshot_view(rd->s);
    for (int round = 0; round < 40; round++) {
        piece_iter it;
        piece_iter_init(&it, v, 0);
        const uint8_t *p;
        size_t k;
        uint64_t at = 0;
        while ((k = piece_iter_next(&it, &p)) != 0) {
            if (at + k > rd->n || memcmp(p, rd->want + at, k) != 0) atomic_fetch_add(rd->bad, 1);
            at += k;
        }
        if (at != rd->n || piece_length(v) != rd->n) atomic_fetch_add(rd->bad, 1);
        piece_iter_init(&it, v, rd->n / 3);
        piece_span sp;
        while (piece_iter_next_span(&it, rd->n, &sp, NULL)) {}
        piece_snapshot *extra = piece_snapshot_retain(rd->s);
        piece_snapshot_release(extra);
    }
    piece_snapshot_release(rd->s);
}

static void t_threads(void)
{
    enum { N = 2 * PIECE_CHUNK + 77 };
    uint8_t *b = malloc(N);
    ASSERT(b != NULL);
    ph_fill(b, N, 13);
    ph_mapping map;
    if (!ph_map_open(&map, b, N)) { CHECK(0, "mapped fixture"); free(b); return; }
    piece_reserve *r = NULL;
    piece_tree *t = NULL;
    if (piece_reserve_create(&r) != PIECE_OK || piece_open_mapped(r, &map.b, &t) != PIECE_OK) {
        CHECK(0, "open refused");
        if (r) piece_reserve_destroy(r);
        ph_map_unref(&map);
        free(b);
        return;
    }
    for (int i = 0; i < 20; i++) CHECK(piece_replace(t, (uint64_t)i * 997u, 3, str("\nab"), 3) == PIECE_OK, "frag");
    piece_snapshot *s;
    CHECK(piece_snapshot_take(t, &s) == PIECE_OK, "take");
    /* The expected bytes, read on the UI before any reader starts. */
    uint64_t n = piece_length(piece_snapshot_view(s));
    uint8_t *want = read_all(piece_snapshot_view(s));
    atomic_int bad;
    atomic_init(&bad, 0);
    reader rd[2];
    os_thread th[2];
    for (int i = 0; i < 2; i++) {
        rd[i] = (reader){piece_snapshot_retain(s), want, n, &bad};
        CHECK(os_thread_start(&th[i], reader_run, &rd[i]) == OS_OK, "thread start");
    }
    /* The UI keeps editing, taking and aborting while the readers run. */
    for (int i = 0; i < 30; i++) {
        CHECK(piece_checkpoint_begin(t) == PIECE_OK, "begin");
        CHECK(piece_replace(t, 5, 1, str("xyz"), 3) == PIECE_OK, "edit");
        piece_snapshot *mid;
        CHECK(piece_snapshot_take(t, &mid) == PIECE_OK, "take inside");
        if (i % 2) piece_checkpoint_abort(t);
        else piece_checkpoint_commit(t);
        piece_snapshot_release(mid);
        (void)piece_reclaim(t, 8);
    }
    piece_snapshot_release(s);
    piece_close(t); /* the readers now hold the core's last owners */
    for (int i = 0; i < 2; i++) CHECK(os_thread_join(&th[i]) == OS_OK, "join");
    CHECK(atomic_load(&bad) == 0, "readers saw %d differences", atomic_load(&bad));
    CHECK(atomic_load(&map.acquires) == 1 && atomic_load(&map.releases) == 1,
          "backing acquired %d, released %d", atomic_load(&map.acquires), atomic_load(&map.releases));
    piece_reserve_destroy(r);
    ph_map_unref(&map);
    free(want);
    free(b);
}

/* ---- indent over a piece view (indent.h's indent_text) ------------------- */

static void pt_seek(void *it, uint64_t off) { piece_iter_seek(it, off); }
static size_t pt_next(void *it, const uint8_t **p) { return piece_iter_next(it, p); }

typedef struct flat {
    const uint8_t *b;
    uint64_t n, pos;
} flat;
static void flat_seek(void *f, uint64_t off) { ((flat *)f)->pos = off; }
static size_t flat_next(void *fv, const uint8_t **p)
{
    flat *f = fv;
    if (f->pos >= f->n) return 0;
    *p = f->b + f->pos;
    size_t k = (size_t)(f->n - f->pos < 3u ? f->n - f->pos : 3u);
    f->pos += k;
    return k;
}

static void t_indent(void)
{
    const char *text = "int f(void)\n{\n\tif (x) {\n\t\ty();\n\t}\n}\n";
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, NULL, 0)) return;
    /* Typed backwards: every byte is its own span, so every run is one byte. */
    for (size_t i = strlen(text); i-- > 0;)
        CHECK(piece_replace(t, 0, 0, str(text) + i, 1) == PIECE_OK, "type");
    CHECK(ph_count_spans(piece_tree_view(t), 0, strlen(text)) == strlen(text), "one-byte spans");
    piece_iter it;
    piece_iter_init(&it, piece_tree_view(t), 0);
    indent_text pt = {pt_seek, pt_next, &it, piece_length(piece_tree_view(t))};
    flat f = {str(text), strlen(text), 0};
    indent_text ft = {flat_seek, flat_next, &f, f.n};
    indent_style a = indent_detect(&pt), b = indent_detect(&ft);
    CHECK(a.kind == b.kind && a.eol == b.eol && a.width == b.width, "indent_detect over piece");
    for (uint64_t lo = 0; lo <= f.n; lo++) {
        uint64_t ls = lo;
        while (ls > 0 && text[ls - 1] != '\n') ls--;
        indent_range x = indent_on_enter(&pt, ls, lo), y = indent_on_enter(&ft, ls, lo);
        CHECK(x.lo == y.lo && x.hi == y.hi, "indent_on_enter at %llu", PH_U(lo));
    }
    close_text(r, t);
}

/* ---- misuse is the one ASSERT (A15) ----------------------------------------- */

/* The child dies by SIGABRT after writing the ASSERT message; its own setup
 * failing (a refused open) exits 3 instead, which fails the case. */
static bool dies_by_assert(void (*violate)(void), const char *what)
{
    int fd[2];
    if (pipe(fd) != 0) return false;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        close(fd[0]);
        if (dup2(fd[1], STDERR_FILENO) != STDERR_FILENO) _exit(2);
        violate();
        _exit(0);
    }
    close(fd[1]);
    char text[512];
    size_t got = 0;
    for (;;) {
        ssize_t k = read(fd[0], text + got, sizeof text - 1 - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += (size_t)k;
        if (got == sizeof text - 1) break;
    }
    text[got] = '\0';
    close(fd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    bool ok = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT && strstr(text, ": ASSERT failed: ") != NULL;
    if (!ok) fprintf(stderr, "%s: child status %d, stderr \"%s\"\n", what, status, text);
    return ok;
}

static piece_tree *child_tree(piece_reserve **r)
{
    piece_tree *t;
    if (piece_reserve_create(r) != PIECE_OK || piece_open_copy(*r, str("abc\ndef"), 7, &t) != PIECE_OK) _exit(3);
    return t;
}

static void v_nested_begin(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    if (piece_checkpoint_begin(t) != PIECE_OK) _exit(3);
    (void)piece_checkpoint_begin(t);
}
static void v_commit_none(void) { piece_reserve *r; piece_checkpoint_commit(child_tree(&r)); }
static void v_abort_none(void) { piece_reserve *r; piece_checkpoint_abort(child_tree(&r)); }
static void v_byte_past_end(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    (void)piece_byte_at(piece_tree_view(t), 7);
}
static void v_replace_past_end(void)
{
    piece_reserve *r;
    (void)piece_replace(child_tree(&r), 8, 0, str("x"), 1);
}
static void v_delete_past_end(void)
{
    piece_reserve *r;
    (void)piece_replace(child_tree(&r), 5, 3, NULL, 0);
}
static void v_add_span_past_add(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    (void)piece_replace_spans(t, 0, 0, (piece_span[]){SPAN(ADD(0), 1)}, NULL, 1);
}
static void v_orig_span_past_orig(void)
{
    piece_reserve *r;
    (void)piece_replace_spans(child_tree(&r), 0, 0, (piece_span[]){SPAN(5, 3)}, NULL, 1);
}
static void v_empty_span(void)
{
    piece_reserve *r;
    (void)piece_replace_spans(child_tree(&r), 0, 0, (piece_span[]){SPAN(1, 0)}, NULL, 1);
}
static void v_iter_after_edit(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    piece_iter it;
    piece_iter_init(&it, piece_tree_view(t), 0);
    if (piece_replace(t, 0, 1, NULL, 0) != PIECE_OK) _exit(3);
    const uint8_t *p;
    (void)piece_iter_next(&it, &p);
}
static void v_close_open_checkpoint(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    if (piece_checkpoint_begin(t) != PIECE_OK) _exit(3);
    piece_close(t);
}
static void v_destroy_busy_reserve(void)
{
    piece_reserve *r;
    (void)child_tree(&r);
    piece_reserve_destroy(r);
}
static void v_capture_empty(void)
{
    piece_reserve *r;
    piece_span sp;
    (void)piece_capture(child_tree(&r), 1, 0, &sp);
}

static void t_deaths(void)
{
    static const struct { void (*fn)(void); const char *what; } cases[] = {
        {v_nested_begin, "nested checkpoint begin"},
        {v_commit_none, "commit with no checkpoint"},
        {v_abort_none, "abort with no checkpoint"},
        {v_byte_past_end, "byte_at at the length"},
        {v_replace_past_end, "insert past the end"},
        {v_delete_past_end, "delete past the end"},
        {v_add_span_past_add, "ADD span past the ADD length"},
        {v_orig_span_past_orig, "ORIGINAL span past ORIGINAL"},
        {v_empty_span, "empty span"},
        {v_iter_after_edit, "live iterator after an edit"},
        {v_close_open_checkpoint, "close with a checkpoint open"},
        {v_destroy_busy_reserve, "destroy a reserve with an open tree"},
        {v_capture_empty, "empty capture"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        CHECK(dies_by_assert(cases[i].fn, cases[i].what), "%s is not an ASSERT", cases[i].what);
}

/* ==== Lines (draft section, docs/decisions/v2-0fv.7.md §9) ================
 * Kept apart from the frozen sections above. Which answers are exact depends
 * on the implementation's hidden shape, so these cases check the contract
 * against the truth of the reference bytes (soundness, private-copy
 * exactness, carried exactness, healing, completeness, faults, snapshot
 * stability) and never compare exactness flags with the model's. */

static uint8_t *lines_ops(size_t n, uint64_t seed)
{
    uint8_t *ops = malloc(n);
    ASSERT(ops != NULL);
    for (size_t i = 0; i < n; i++) ops[i] = (uint8_t)ph_rand(&seed);
    for (size_t i = 0; i < n; i += 9) /* refuse base on one operation in eight */
        if (ph_rand(&seed) % 8u) ops[i] &= 0x7f;
    return ops;
}

static void lines_check_all(ph_state *st, uint64_t seed, bool want_exact, const char *tag)
{
    ph_check_lines(piece_tree_view(st->t), pm_tree_view(st->m), seed, 3, want_exact, tag);
    for (unsigned i = 0; i < PH_SNAPS; i++)
        if (st->s[i]) ph_check_lines(piece_snapshot_view(st->s[i]), pm_snapshot_view(st->ms[i]), seed + i, 2,
                                     want_exact && !st->map, tag);
}

static void describe_single(const piece_view *v, uint64_t off, uint64_t end,
                            piece_span *span, piece_span_nl *nl)
{
    piece_iter it;
    piece_iter_init(&it, v, off);
    ASSERT(piece_iter_next_span(&it, end, span, nl) && span->len == end - off);
}

static piece_span_nl join_descriptions(const piece_view *left, const piece_view *right,
                                        uint64_t off, uint64_t cut, uint64_t end)
{
    piece_span a, b;
    piece_span_nl an, bn;
    describe_single(left, off, cut, &a, &an);
    describe_single(right, cut, end, &b, &bn);
    return piece_span_nl_join(a, an, b, bn);
}

static void t_lines_join(void)
{
    /* (P) Four source chunks cover a shared head, a shared tail, a
     * boundary seam and a shared interior chunk with a chunk left over. */
    static uint8_t bytes[4 * PIECE_CHUNK];
    ph_fill(bytes, sizeof bytes, 1);
    static const uint64_t ranges[][3] = {
        {1, 3, 7},
        {PIECE_CHUNK - 2, PIECE_CHUNK, PIECE_CHUNK + 3},
        {PIECE_CHUNK - 2, PIECE_CHUNK - 1, PIECE_CHUNK + 3},
        {PIECE_CHUNK - 2, PIECE_CHUNK + 1, PIECE_CHUNK + 3},
        {1, PIECE_CHUNK + 2, sizeof bytes - 1}
    };
    piece_reserve *r;
    piece_tree *t;
    if (!open_text(&r, &t, bytes, sizeof bytes)) return;
    const piece_view *v = piece_tree_view(t);
    for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        uint64_t off = ranges[i][0], cut = ranges[i][1], end = ranges[i][2];
        piece_span span;
        piece_span_nl fresh;
        describe_single(v, off, end, &span, &fresh);
        piece_span_nl got = join_descriptions(v, v, off, cut, end);
        CHECK(got.head == fresh.head && got.tail == fresh.tail, "joined counted edges, shape %zu", i);
        CHECK(piece_replace_spans(t, off, end - off, &span, &got, 1) == PIECE_OK,
              "joined provenance, shape %zu", i);
    }
    close_text(r, t);

    /* (P) A pre-adoption snapshot and the counted live view of its tree
     * provide independently described unknown and known edges. */
    ph_mapping map;
    ASSERT(ph_map_open(&map, bytes, sizeof bytes));
    ASSERT(piece_reserve_create(&r) == PIECE_OK);
    ASSERT(piece_open_mapped(r, &map.b, &t) == PIECE_OK);
    piece_snapshot *unresolved;
    ASSERT(piece_snapshot_take(t, &unresolved) == PIECE_OK);
    ASSERT(ph_lines_complete(t, &map, "join resolve"));
    v = piece_tree_view(t);
    const piece_view *old = piece_snapshot_view(unresolved);
    piece_span_nl left = join_descriptions(old, v, 1, 3, 7);
    piece_span_nl right = join_descriptions(v, old, 1, 3, 7);
    CHECK(left.head == PIECE_NL_UNKNOWN && left.tail == 0, "unknown left contribution");
    CHECK(right.head == PIECE_NL_UNKNOWN && right.tail == 0, "unknown right contribution");
    left = join_descriptions(old, v, PIECE_CHUNK - 2, PIECE_CHUNK, PIECE_CHUNK + 3);
    right = join_descriptions(v, old, PIECE_CHUNK - 2, PIECE_CHUNK, PIECE_CHUNK + 3);
    CHECK(left.head == PIECE_NL_UNKNOWN && left.tail != PIECE_NL_UNKNOWN, "unknown head only");
    CHECK(right.head != PIECE_NL_UNKNOWN && right.tail == PIECE_NL_UNKNOWN, "unknown tail only");
    piece_snapshot_release(unresolved);
    close_text(r, t);
    ph_map_unref(&map);
}

/* A private-copy tree is exact from open on, through every operation. */
static void t_lines_private(void)
{
    enum { N = 2 * PIECE_CHUNK + 999 };
    uint8_t *init = malloc(N), *scratch = malloc(PH_SCRATCH), *ops = lines_ops(9 * 150, 41);
    ASSERT(init != NULL && scratch != NULL);
    ph_fill(init, N, 21);
    ph_state st;
    if (ph_open(&st, init, N, 0, NULL)) {
        lines_check_all(&st, 1, true, "private open");
        ph_in in = {ops, 9 * 150};
        unsigned k = 0;
        while (ph_step(&st, &in, scratch)) lines_check_all(&st, k++, true, "private edit");
    }
    ph_close(&st);
    free(ops);
    free(scratch);
    free(init);
}

/* Mapped: sound throughout, with partial adoption and resolve between
 * operations; complete after every chunk is adopted and the sweep drained. */
static void t_lines_mapped(void)
{
    enum { N = 3 * PIECE_CHUNK + 4321 };
    uint8_t *init = malloc(N), *scratch = malloc(PH_SCRATCH), *ops = lines_ops(9 * 150, 43);
    ASSERT(init != NULL && scratch != NULL);
    ph_fill(init, N, 31);
    ph_mapping map;
    ph_state st;
    if (ph_open(&st, init, N, 1, &map)) {
        lines_check_all(&st, 2, false, "mapped open");
        uint32_t c1;
        CHECK(piece_lines_count(&map.b, 1, &c1, 1), "count chunk 1");
        CHECK(piece_lines_adopt(st.t, map.b.fault, 1, &c1, 1), "adopt chunk 1");
        ph_in in = {ops, 9 * 150};
        unsigned k = 0;
        while (ph_step(&st, &in, scratch)) {
            if (k % 10 == 0) {
                bool more;
                CHECK(piece_lines_resolve(st.t, 8, &more) == PIECE_OK, "resolve step");
            }
            lines_check_all(&st, k++, false, "mapped edit");
        }
        ph_op_end(&st, false);
        if (ph_lines_complete(st.t, &map, "mapped")) {
            ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 7, 16, true, "mapped complete");
            ph_op_take(&st, 0, false);
            if (st.s[0]) ph_check_lines(piece_snapshot_view(st.s[0]), pm_snapshot_view(st.ms[0]), 8, 8, true,
                                        "snapshot after completion");
        }
    }
    ph_close(&st);
    free(ops);
    free(scratch);
    free(init);
}

/* Undo with the records described before the delete keeps counted text
 * exact at once; without them (NULL, as journal replay) it heals by resolve. */
static void t_lines_carried(bool with_nl)
{
    const char *tag = with_nl ? "carried counts" : "NULL counts";
    enum { N = 4 * PIECE_CHUNK + 77 };
    uint8_t *init = malloc(N);
    ASSERT(init != NULL);
    ph_fill(init, N, 51);
    ph_mapping map;
    ph_state st;
    uint64_t rng = with_nl ? 5u : 6u;
    if (ph_open(&st, init, N, 1, &map) && ph_lines_complete(st.t, &map, tag)) {
        ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 1, 8, true, tag);
        for (int round = 0; round < 6; round++) {
            uint64_t len = piece_length(piece_tree_view(st.t));
            uint64_t off = ph_rand(&rng) % len, del = 1u + ph_rand(&rng) % (len - off < 3u * PIECE_CHUNK ? len - off : 3u * PIECE_CHUNK);
            ph_spans inv = {0};
            ph_describe(piece_tree_view(st.t), off, del, true, &inv);
            ph_op_replace(&st, off, del, NULL, 0, false);
            ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 2, 4, true, "after the delete");
            ph_op_replace_spans(&st, off, 0, &inv, with_nl, false);
            if (with_nl) {
                ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 3, 8, true, "undo with counts");
            } else {
                ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 3, 8, false, "undo without counts");
                if (ph_lines_complete(st.t, &map, tag))
                    ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 4, 8, true, "healed by resolve");
            }
            ph_spans_free(&inv);
        }
    }
    ph_close(&st);
    free(init);
}

/* Astra Q2.2: a counted slice A and its source-contiguous neighbour B added
 * without counts form one span whose head mixes knowledge. Described, deleted
 * and undone before resolve, then cut back to A: exact again after resolve. */
static void t_lines_mixed_end(void)
{
    enum { N = 2 * PIECE_CHUNK };
    uint8_t *init = malloc(N);
    ASSERT(init != NULL);
    ph_fill(init, N, 61);
    init[150] = '\n'; /* A holds a newline */
    ph_mapping map;
    ph_state st;
    if (ph_open(&st, init, N, 1, &map) && ph_lines_complete(st.t, &map, "mixed end")) {
        ph_op_replace(&st, PIECE_CHUNK, PIECE_CHUNK, NULL, 0, false); /* the known prefix ORIGINAL [0, C) */
        ph_spans a = {0}, b = {0}, ab = {0};
        ph_describe(piece_tree_view(st.t), 100, 100, true, &a); /* A = [100, 200), counted */
        ph_spans_push(&b, SPAN(200, 100), (piece_span_nl){PIECE_NL_UNKNOWN, PIECE_NL_UNKNOWN});
        ph_op_replace_spans(&st, PIECE_CHUNK, 0, &a, true, false);         /* A at EOF with its record */
        ph_op_replace_spans(&st, PIECE_CHUNK + 100u, 0, &b, false, false); /* B at EOF without counts */
        ph_describe(piece_tree_view(st.t), PIECE_CHUNK, 200, true, &ab);
        CHECK(ab.n == 1 && ph_span_eq(ab.s[0], SPAN(100, 200)), "A.B is one span");
        lines_check_all(&st, 1, false, "mixed: before the round trip");
        ph_op_replace(&st, PIECE_CHUNK, 200, NULL, 0, false);
        ph_op_replace_spans(&st, PIECE_CHUNK, 0, &ab, true, false); /* undo before resolve */
        lines_check_all(&st, 2, false, "mixed: after undo");
        ph_op_replace(&st, PIECE_CHUNK + 100u, 100, NULL, 0, false); /* cut away B */
        lines_check_all(&st, 3, false, "mixed: A alone, before resolve");
        if (ph_lines_complete(st.t, &map, "mixed end"))
            ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 4, 16, true, "mixed: exact after resolve");
        ph_spans_free(&a);
        ph_spans_free(&b);
        ph_spans_free(&ab);
    }
    ph_close(&st);
    free(init);
}

/* A snapshot keeps its answers while its backing is trusted. */
static void t_lines_snapshot_stable(void)
{
    enum { N = 2 * PIECE_CHUNK + 300 };
    uint8_t *init = malloc(N);
    ASSERT(init != NULL);
    ph_fill(init, N, 71);
    ph_mapping map;
    ph_state st;
    if (ph_open(&st, init, N, 1, &map)) {
        ph_op_take(&st, 0, false);
        const piece_view *sv = st.s[0] ? piece_snapshot_view(st.s[0]) : NULL;
        if (sv) {
            uint64_t c0, o0, b0, c1, o1, b1;
            bool e0 = piece_lines_before(sv, N, &c0), f0 = piece_lines_start(sv, 3, &o0, &b0);
            if (ph_lines_complete(st.t, &map, "snapshot stable")) {
                bool e1 = piece_lines_before(sv, N, &c1), f1 = piece_lines_start(sv, 3, &o1, &b1);
                CHECK(e0 == e1 && c0 == c1 && f0 == f1 && o0 == o1 && b0 == b1, "a snapshot's answers changed");
                ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 9, 8, true, "live after completion");
            }
        }
    }
    ph_close(&st);
    free(init);
}

/* Faults: a tainted batch is discarded at adoption, a disagreeing or foreign
 * one too; a nonzero epoch or a failing poll makes every dependent answer
 * inexact with lower bound 0, line 0 excepted. */
static void lines_fault_answers(ph_state *st, ph_mapping *map, ph_poll mode, const char *tag)
{
    const piece_view *v = piece_tree_view(st->t);
    uint64_t c = 7, o = 7, b = 7;
    uint32_t cnt;
    ph_poll_set(mode);
    bool r = piece_lines_before(v, piece_length(v), &c);
    bool s1 = piece_lines_start(v, 1, &o, &b);
    uint64_t o0 = 7, b0 = 7;
    bool s0 = piece_lines_start(v, 0, &o0, &b0);
    bool counted = piece_lines_count(&map->b, 0, &cnt, 1);
    ph_poll_set(PH_POLL_REAL);
    CHECK(!r && c == 0, "%s: rank (%d, %llu), want (false, 0)", tag, (int)r, PH_U(c));
    CHECK(!s1 && o == 0 && b == 0, "%s: select(1) (%d, %llu, %llu), want (false, 0, 0)", tag, (int)s1, PH_U(o), PH_U(b));
    CHECK(s0 && o0 == 0 && b0 == 0, "%s: select(0) must stay (true, 0, 0)", tag);
    CHECK(!counted, "%s: the counter must refuse", tag);
}

static void t_lines_faults(void)
{
    enum { N = 2 * PIECE_CHUNK + 5 };
    uint8_t *init = malloc(N);
    ASSERT(init != NULL);
    ph_fill(init, N, 81);
    ph_mapping map;
    ph_state st;
    if (ph_open(&st, init, N, 1, &map)) {
        uint32_t counts[3];
        CHECK(piece_lines_count(&map.b, 0, counts, 3), "count");
        ph_poll_set(PH_POLL_FAULTED);
        bool tainted = piece_lines_adopt(st.t, map.b.fault, 0, counts, 3);
        ph_poll_set(PH_POLL_ERROR);
        bool failed = piece_lines_adopt(st.t, map.b.fault, 0, counts, 3);
        ph_poll_set(PH_POLL_REAL);
        CHECK(!tainted && !failed, "a tainted batch was adopted");
        os_fault_id foreign = map.b.fault;
        foreign.gen++;
        CHECK(!piece_lines_adopt(st.t, foreign, 0, counts, 3), "a foreign batch was adopted");
        bool more;
        CHECK(piece_lines_resolve(st.t, 64, &more) == PIECE_OK, "resolve");
        ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 1, 4, false, "nothing adopted");
        CHECK(piece_lines_adopt(st.t, map.b.fault, 0, counts, 3), "a clean batch");
        CHECK(piece_lines_adopt(st.t, map.b.fault, 0, counts, 3), "an identical batch again");
        uint32_t other = counts[0] ? counts[0] - 1u : 1u;
        CHECK(!piece_lines_adopt(st.t, map.b.fault, 0, &other, 1), "a disagreeing batch was adopted");
        if (ph_lines_complete(st.t, &map, "faults")) {
            ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 2, 8, true, "before the fault");
            lines_fault_answers(&st, &map, PH_POLL_FAULTED, "nonzero epoch");
            lines_fault_answers(&st, &map, PH_POLL_ERROR, "failing poll");
            ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), 3, 8, true, "trusted again");
        }
    }
    ph_close(&st);
    free(init);
}

/* FAULTS for cuts and resolve (Muse's 7b test read): a poll that reports a
 * nonzero epoch or fails, around a cut of a counted mapped descriptor (an edit,
 * or a description with counts) or around a resolve scan, leaves the entry
 * unknown: no count read from possibly repaired pages is published. */
static void lines_fault_cut_resolve(ph_poll mode, const char *tag)
{
    enum { N = 2 * PIECE_CHUNK };
    uint8_t *init = malloc(N);
    ASSERT(init != NULL);
    for (size_t i = 0; i < N; i++) init[i] = i % 3u ? 'a' : '\n';
    ph_mapping map;
    ph_state st;
    if (ph_open(&st, init, N, 1, &map) && ph_lines_complete(st.t, &map, tag)) {
        const piece_view *v = piece_tree_view(st.t);
        uint64_t count;
        CHECK(piece_lines_before(v, piece_length(v), &count), "%s: exact before", tag);
        /* A cut of chunk 0's counted descriptor under the fault. */
        ph_poll_set(mode);
        ph_op_replace(&st, 100, 1, NULL, 0, false);
        ph_poll_set(PH_POLL_REAL);
        CHECK(!piece_lines_before(v, piece_length(v), &count), "%s: a faulted cut published counts", tag);
        ph_check_lines(v, pm_tree_view(st.m), 4, 4, false, tag);
        /* A description with counts that cuts chunk 1's descriptor. */
        piece_iter it;
        piece_span sp;
        piece_span_nl nl;
        ph_poll_set(mode);
        piece_iter_init(&it, v, PIECE_CHUNK + 10);
        bool described = piece_iter_next_span(&it, PIECE_CHUNK + 20, &sp, &nl);
        ph_poll_set(PH_POLL_REAL);
        CHECK(described && nl.head == PIECE_NL_UNKNOWN, "%s: a faulted describe counted", tag);
        piece_iter_init(&it, v, PIECE_CHUNK + 10);
        CHECK(piece_iter_next_span(&it, PIECE_CHUNK + 20, &sp, &nl) && nl.head != PIECE_NL_UNKNOWN,
              "%s: describe counts when trusted", tag);
        /* The resolve scan of chunk 0's unknown halves under the fault. */
        bool more = true;
        ph_poll_set(mode);
        CHECK(piece_lines_resolve(st.t, 64, &more) == PIECE_OK, "%s: faulted resolve", tag);
        ph_poll_set(PH_POLL_REAL);
        CHECK(!piece_lines_before(v, piece_length(v), &count), "%s: a faulted resolve published counts", tag);
        ph_check_lines(v, pm_tree_view(st.m), 5, 4, false, tag);
        /* Trusted again (the fault was simulated): an edit rewinds the sweep
         * past the entries it passed under the fault, and it completes. */
        ph_op_replace(&st, piece_length(v), 0, str("\n"), 1, false);
        if (ph_lines_complete(st.t, &map, tag))
            ph_check_lines(v, pm_tree_view(st.m), 6, 8, true, tag);
    }
    ph_close(&st);
    free(init);
}

static void t_lines_fault_cut_resolve(void)
{
    lines_fault_cut_resolve(PH_POLL_FAULTED, "cut and resolve, nonzero epoch");
    lines_fault_cut_resolve(PH_POLL_ERROR, "cut and resolve, failing poll");
}

/* B's debug check: a record that is not the true count of its bytes dies by
 * OS_SAN_ASSERT in the sanitizer lanes; the shipped build reads nothing. */
static void v_record_wrong_count(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r); /* "abc\ndef" */
    piece_iter it;
    piece_span sp;
    piece_span_nl nl;
    piece_iter_init(&it, piece_tree_view(t), 0);
    if (!piece_iter_next_span(&it, 4, &sp, &nl) || nl.head != 1) _exit(3);
    nl.head = 0;
    (void)piece_replace_spans(t, 7, 0, &sp, &nl, 1);
}
static void v_record_add_lineage(void)
{
    piece_reserve *r;
    piece_tree *t = child_tree(&r);
    piece_snapshot *in;
    if (piece_checkpoint_begin(t) != PIECE_OK || piece_replace(t, 7, 0, str("\n"), 1) != PIECE_OK ||
        piece_snapshot_take(t, &in) != PIECE_OK) _exit(3);
    piece_checkpoint_abort(t);
    if (piece_replace(t, 7, 0, str("y"), 1) != PIECE_OK) _exit(3); /* regrows ADD [0, 1) with 'y' */
    piece_iter it;
    piece_span sp;
    piece_span_nl nl;
    piece_iter_init(&it, piece_snapshot_view(in), 7);
    if (!piece_iter_next_span(&it, 8, &sp, &nl) || nl.head != 1) _exit(3);
    (void)piece_replace_spans(t, 0, 0, &sp, &nl, 1);
}

static void t_lines_deaths(void)
{
    bool want = OS_SAN_CHECKS != 0;
    CHECK(dies_by_assert(v_record_wrong_count, "wrong record count") == want,
          "a wrong record count: died %d, want %d", (int)!want, (int)want);
    CHECK(dies_by_assert(v_record_add_lineage, "ADD record after an abort") == want,
          "an ADD record across an abort: died %d, want %d", (int)!want, (int)want);
}

/* ---- the differential random walk ----------------------------------------- */

static void t_random(void)
{
    uint8_t *scratch = malloc(PH_SCRATCH);
    uint8_t ops[9 * 200]; /* 9 bytes per operation (ph_step) */
    uint8_t init[3 * PIECE_CHUNK];
    ASSERT(scratch != NULL);
    for (uint64_t seed = 1; seed <= 48; seed++) {
        uint64_t s = seed * 0x2545f4914f6cdd1du;
        for (size_t i = 0; i < sizeof ops; i++) ops[i] = (uint8_t)ph_rand(&s);
        for (size_t i = 0; i < sizeof ops; i += 9) /* refuse base on one operation in eight */
            if (ph_rand(&s) % 8u) ops[i] &= 0x7f;
        size_t n = (size_t)(ph_rand(&s) % sizeof init);
        ph_fill(init, n, (uint32_t)seed);
        ph_mapping map;
        ph_state st;
        unsigned mode = n > 0 && seed % 3 == 0 ? 1u : 0u;
        if (!ph_open(&st, init, n, mode, &map)) { ph_close(&st); break; }
        ph_in in = {ops, sizeof ops};
        int fails = ph_failures;
        while (ph_step(&st, &in, scratch) && ph_failures == fails) {}
        ph_check_snaps(&st, "final snapshots");
        ph_close(&st);
        if (ph_failures != fails) {
            fprintf(stderr, "t_random: seed %llu failed\n", PH_U(seed));
            break;
        }
    }
    free(scratch);
    printf("t_random: %lu calls returned OK, %lu NOMEM under refusal, %lu RANGE\n", ph_seen_ok, ph_seen_nomem,
           ph_seen_range);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    t_deaths(); /* forks: before any thread starts */
    t_lines_deaths();
    t_open_copy();
    t_replace();
    t_rejoin();
    t_capture();
    t_snapshots();
    t_checkpoint();
    t_inverse();
    t_nomem();
    t_height();
    t_mapped();
    t_indent();
    t_threads();
    t_random();
    /* Lines (draft section) */
    t_lines_join();
    t_lines_private();
    t_lines_mapped();
    t_lines_carried(true);
    t_lines_carried(false);
    t_lines_mixed_end();
    t_lines_snapshot_stable();
    t_lines_faults();
    t_lines_fault_cut_resolve();
    printf("Lines: %lu exact and %lu inexact answers checked\n", ph_lines_exact, ph_lines_inexact);
    CHECK(ph_lines_exact > 0 && ph_lines_inexact > 0, "the Lines cases must see both kinds of answer");
    if (ph_failures) {
        printf("piece_test: %d failures\n", ph_failures);
        return 1;
    }
    printf("piece_test: ok\n");
    return 0;
}
