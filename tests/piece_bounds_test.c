/* Permanent uint64_t and snapshot-owner boundary regressions (P1.4d). */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __func__, __LINE__, #c); return 1; \
} } while (0)

#ifdef PIECE_TESTING
typedef struct { unsigned owners, acquired, released; } map_counts;
static void map_acquire(void *ctx) {
    map_counts *m = ctx; m->owners++; m->acquired++;
}
static void map_release(void *ctx) {
    map_counts *m = ctx; m->owners--; m->released++;
}
static int owner_boundary(void) {
    map_counts m = {0}; piece_map_hooks h = { &m, map_acquire, map_release };
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(t); CHECK(piece_init_mapped(t, (const uint8_t *)"x", 1, &h) == PIECE_OK);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    CHECK(piece_test_snapshot_owners(s) == 2);
    piece_test_snapshot_set_owners(s, UINT_MAX - 1);
    piece_snapshot *last = piece_snapshot_retain(s); CHECK(last == s);
    CHECK(piece_test_snapshot_owners(s) == UINT_MAX && m.owners == 3);
    unsigned acquired = m.acquired;
    CHECK(piece_snapshot_retain(s) == NULL);
    CHECK(piece_snapshot_take(t) == NULL);
    CHECK(piece_test_snapshot_owners(s) == UINT_MAX && m.acquired == acquired);
    piece_snapshot_release(last);
    CHECK(piece_test_snapshot_owners(s) == UINT_MAX - 1);
    last = piece_snapshot_take(t); CHECK(last == s);
    CHECK(piece_test_snapshot_owners(s) == UINT_MAX);
    piece_snapshot_release(last);
    piece_test_snapshot_set_owners(s, 2);
    piece_destroy(t);
    uint8_t b = 0; CHECK(piece_snapshot_read(s, 0, &b, 1) == PIECE_OK && b == 'x');
    piece_snapshot_release(s);
    CHECK(m.owners == 0 && m.acquired == m.released);
    puts("snapshot owner boundary: ok (retain/take refuse saturation; mapping balanced)");
    return 0;
}

static int state_equal(piece_test_state a, piece_test_state b) {
    return a.root == b.root && a.cached_snapshot == b.cached_snapshot &&
           a.len == b.len && a.root_len == b.root_len && a.add_len == b.add_len &&
           a.run_end == b.run_end && a.run_addend == b.run_addend &&
           a.run_valid == b.run_valid && a.cursor_valid == b.cursor_valid;
}
static piece_tree *repeated_tree(uint64_t len, const piece_allocator *a) {
    piece_tree *t = piece_create(a);
    if (!t) return NULL;
    if (piece_insert(t, 0, (const uint8_t *)"x", 1) ||
        piece_delete(t, 0, 1, NULL) || piece_test_repeat_byte(t, len)) {
        piece_destroy(t); return NULL;
    }
    return t;
}
static int insert_boundary(void) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = repeated_tree(UINT64_MAX - 2, &a); CHECK(t);
    CHECK(piece_insert(t, UINT64_MAX - 2, (const uint8_t *)"y", 1) == PIECE_OK);
    piece_test_state before = piece_test_get_state(t);
    CHECK(before.len == UINT64_MAX - 1 && before.root_len == before.len && before.cursor_valid);
    CHECK(piece_insert(t, UINT64_MAX - 1, (const uint8_t *)"zz", 2) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)));
    uint8_t b = 0;
    CHECK(piece_read(t, UINT64_MAX - 2, &b, 1) == PIECE_OK && b == 'y');
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    before = piece_test_get_state(t);
    CHECK(piece_insert(t, 0, (const uint8_t *)"zz", 2) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)));
    CHECK(piece_insert(t, UINT64_MAX - 1, (const uint8_t *)"z", 1) == PIECE_OK);
    CHECK(piece_len(t) == UINT64_MAX && piece_test_get_state(t).root_len == UINT64_MAX);
    CHECK(piece_snapshot_len(s) == UINT64_MAX - 1);
    CHECK(piece_read(t, UINT64_MAX - 1, &b, 1) == PIECE_OK && b == 'z');
    before = piece_test_get_state(t);
    CHECK(piece_insert(t, UINT64_MAX, (const uint8_t *)"q", 1) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)));
    CHECK(piece_insert(t, UINT64_MAX, NULL, 0) == PIECE_OK);
    CHECK(piece_read(t, UINT64_MAX, NULL, 0) == PIECE_OK);
    CHECK(piece_delete(t, UINT64_MAX, 0, NULL) == PIECE_OK);
    piece_snapshot_release(s); piece_destroy(t);
    puts("insert growth boundary: ok (cursor/COW/subtree sums; exact UINT64_MAX; refusal unchanged)");
    return 0;
}
static int ref_boundary(void) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = repeated_tree(UINT64_MAX - 1, &a); CHECK(t);
    piece_ref r = { .nspans = 2, .len = 2, .span = {{0, 1}, {0, 1}} };
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    piece_test_state before = piece_test_get_state(t);
    CHECK(piece_insert_ref(t, UINT64_MAX - 1, &r) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)));
    r.nspans = 1; r.len = 1;
    CHECK(piece_insert_ref(t, UINT64_MAX - 1, &r) == PIECE_OK);
    CHECK(piece_len(t) == UINT64_MAX && piece_test_get_state(t).root_len == UINT64_MAX);
    before = piece_test_get_state(t);
    CHECK(piece_insert_ref(t, 0, &r) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)));
    r.nspans = 0; r.len = 0;
    CHECK(piece_insert_ref(t, UINT64_MAX, &r) == PIECE_OK);
    CHECK(piece_snapshot_len(s) == UINT64_MAX - 1);
    piece_snapshot_release(s); piece_destroy(t);
    puts("reference growth boundary: ok (sum spans before mutation; exact UINT64_MAX)");
    return 0;
}
typedef struct { int refuse; size_t calls, live; } fail_allocator;
static void *test_alloc(void *ctx, size_t n) {
    fail_allocator *f = ctx; f->calls++;
    if (f->refuse) return NULL;
    void *p = malloc(n); if (p) f->live += n; return p;
}
static void test_free(void *ctx, void *p, size_t n) {
    fail_allocator *f = ctx; f->live -= n; free(p);
}
static int ref_sum_boundary(void) {
    fail_allocator f = {0}; piece_allocator a = { &f, test_alloc, test_free };
    piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(piece_insert(t, 0, (const uint8_t *)"x", 1) == PIECE_OK);
    uint64_t old_add = piece_test_set_add_length(t, UINT64_MAX / 2);
    piece_ref r = { .nspans = 3, .len = 0,
                   .span = {{0, UINT64_MAX / 2}, {0, UINT64_MAX / 2}, {0, UINT64_MAX / 2}} };
    piece_test_state before = piece_test_get_state(t);
    size_t calls = f.calls; f.refuse = 1;
    CHECK(piece_insert_ref(t, 0, &r) == PIECE_ERR_RANGE);
    CHECK(state_equal(before, piece_test_get_state(t)) && f.calls == calls);
    (void)piece_test_set_add_length(t, old_add);
    f.refuse = 0; piece_destroy(t); CHECK(!f.live);
    puts("reference span-sum overflow: ok (rejects before allocation; tree unchanged)");
    return 0;
}
static int wide_pointer_boundary(void) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = repeated_tree(UINT64_MAX - 1, &a); CHECK(t);
    CHECK(piece_insert(t, UINT64_MAX - 1, (const uint8_t *)"z", 1) == PIECE_OK);
    piece_ref r;
    CHECK(piece_delete(t, UINT64_MAX - 1, 1, &r) == PIECE_OK);
    CHECK(piece_len(t) == UINT64_MAX - 1 && r.len == 1);
    CHECK(piece_insert_ref(t, UINT64_MAX - 1, &r) == PIECE_OK);
    uint8_t b = 0; CHECK(piece_read(t, UINT64_MAX - 1, &b, 1) == PIECE_OK && b == 'z');
    piece_destroy(t);
    puts("uint64_t cursor offset: ok (subtract logical start before pointer addition)");
    return 0;
}
#endif

static int high_offsets(void) {
    const size_t chunk = 65536; const uint64_t copies = 65538;
    uint8_t *payload = malloc(chunk); CHECK(payload);
    for (size_t i = 0; i < chunk; i++) payload[i] = (uint8_t)('a' + i % 26);
    payload[chunk - 1] = '\n';
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(piece_insert(t, 0, payload, chunk) == PIECE_OK);
    piece_ref r; CHECK(piece_delete(t, 0, chunk, &r) == PIECE_OK && r.len == chunk);
    for (uint64_t i = 0; i < copies; i++) CHECK(piece_insert_ref(t, i * chunk, &r) == PIECE_OK);
    uint64_t total = copies * chunk;
    CHECK(piece_len(t) == total && piece_piece_count(t) == copies);
    CHECK(piece_line_count(t) == copies + 1);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    const uint64_t offsets[] = { 0, 65535, 65536, 65537, UINT64_C(4294967295),
                                UINT64_C(4294967296), UINT64_C(4294967297), total - 1, total };
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        uint64_t off = offsets[i]; uint8_t b[64], sb[64];
        size_t n = total - off < sizeof b ? (size_t)(total - off) : sizeof b;
        CHECK(piece_read(t, off, b, n) == PIECE_OK && piece_snapshot_read(s, off, sb, n) == PIECE_OK);
        for (size_t j = 0; j < n; j++) CHECK(b[j] == payload[(off + j) % chunk] && b[j] == sb[j]);
        CHECK(piece_byte_to_line(t, off) == off / chunk && piece_snapshot_byte_to_line(s, off) == off / chunk);
        CHECK(piece_line_to_byte(t, off / chunk) == off / chunk * chunk);
        CHECK(piece_snapshot_line_to_byte(s, off / chunk) == off / chunk * chunk);
        piece_iter it; const uint8_t *p; size_t span;
        piece_iter_begin(&it, t, off);
        int next = piece_iter_next(&it, &p, &span); CHECK(next == (off < total));
        if (next) CHECK(span == chunk - off % chunk && !memcmp(p, payload + off % chunk, span));
        piece_iter_begin_snapshot(&it, s, off);
        next = piece_iter_next(&it, &p, &span); CHECK(next == (off < total));
        if (next) CHECK(span == chunk - off % chunk && !memcmp(p, payload + off % chunk, span));
    }
    uint8_t b = 0;
    CHECK(piece_read(t, total, &b, 1) == PIECE_ERR_RANGE);
    CHECK(piece_read(t, UINT64_MAX, &b, 1) == PIECE_ERR_RANGE);
    CHECK(piece_read(t, 1, &b, SIZE_MAX) == PIECE_ERR_RANGE);
    CHECK(piece_snapshot_read(s, UINT64_MAX, NULL, 0) == PIECE_ERR_RANGE);
    CHECK(piece_delete(t, total, 1, NULL) == PIECE_ERR_RANGE);
    CHECK(piece_delete(t, total - 1, UINT64_MAX, NULL) == PIECE_ERR_RANGE);
    CHECK(piece_delete(t, UINT64_MAX, 0, NULL) == PIECE_ERR_RANGE);
    CHECK(piece_insert(t, UINT64_MAX, (const uint8_t *)"x", 1) == PIECE_ERR_RANGE);
    CHECK(piece_insert_ref(t, UINT64_MAX, &r) == PIECE_ERR_RANGE);
    CHECK(piece_read(t, total, NULL, 0) == PIECE_OK);
    CHECK(piece_delete(t, total, 0, NULL) == PIECE_OK);
    CHECK(piece_insert(t, total, NULL, 0) == PIECE_OK);
    CHECK(piece_line_to_byte(t, UINT64_MAX) == total && piece_snapshot_line_to_byte(s, UINT64_MAX) == total);
    CHECK(piece_byte_to_line(t, UINT64_MAX) == copies && piece_snapshot_byte_to_line(s, UINT64_MAX) == copies);
    piece_iter it; const uint8_t *p; size_t n;
    piece_iter_begin(&it, t, UINT64_MAX); CHECK(!piece_iter_next(&it, &p, &n));
    piece_iter_begin_snapshot(&it, s, UINT64_MAX); CHECK(!piece_iter_next(&it, &p, &n));
    uint64_t off = UINT64_C(4294967295);
    CHECK(piece_delete(t, off, 2, &r) == PIECE_OK && r.len == 2);
    CHECK(piece_insert_ref(t, off, &r) == PIECE_OK && piece_len(t) == total);
    uint8_t pair[2]; CHECK(piece_read(t, off, pair, 2) == PIECE_OK);
    CHECK(pair[0] == '\n' && pair[1] == payload[0]);
    CHECK(piece_insert(t, total, (const uint8_t *)"z", 1) == PIECE_OK);
    piece_destroy(t);
    CHECK(piece_snapshot_len(s) == total && piece_snapshot_line_count(s) == copies + 1);
    CHECK(piece_snapshot_read(s, UINT64_C(4294967296), &b, 1) == PIECE_OK && b == payload[0]);
    CHECK(piece_snapshot_read(s, total - 1, &b, 1) == PIECE_OK && b == '\n');
    piece_snapshot_release(s); free(payload);
    puts("uint64_t positions: ok (4295098368 logical bytes; 2^32/chunk/EOF/range; snapshot after destroy)");
    return 0;
}

int main(int argc, char **argv) {
    int bad = 0;
#ifdef PIECE_TESTING
    if (argc == 1 || !strcmp(argv[1], "--owners")) bad |= owner_boundary();
    if (argc == 1 || !strcmp(argv[1], "--growth")) {
        bad |= insert_boundary(); bad |= ref_boundary(); bad |= ref_sum_boundary();
    }
    if (argc == 1 || !strcmp(argv[1], "--wide")) bad |= wide_pointer_boundary();
#endif
    if (argc == 1 || !strcmp(argv[1], "--wide")) bad |= high_offsets();
    puts(bad ? "piece_bounds_test: FAILED" : "piece_bounds_test: ok");
    return bad;
}
