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

/* Count work, independent of clock speed and shared-machine load. */
static piece_tree *fragments(size_t n) {
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    if (!t) return NULL;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = (uint8_t)(i % 251);
        if (piece_insert(t, 0, &b, 1)) { piece_destroy(t); return NULL; }
    }
    return t;
}
static int trim_work(void) {
    const size_t sizes[] = {16384, 65536};
    for (size_t j = 0; j < sizeof sizes / sizeof sizes[0]; j++) {
        piece_tree *t = fragments(sizes[j]); CHECK(t);
        piece_snapshot *s = piece_snapshot_take(t); CHECK(s); piece_snapshot_release(s);
        for (unsigned i = 0; i < 10; i++) {
            piece_test_reset_stats(t); CHECK(!piece_delete(t, 0, 1024, NULL));
            piece_test_stats st = piece_test_get_stats(t);
            uint64_t limit = st.pool_returns + 16;
            printf("trim work: pieces=%zu delete=%u slabs=%llu returns=%llu bound=%llu\n", sizes[j], i,
                   (unsigned long long)st.slabs_scanned, (unsigned long long)st.pool_returns,
                   (unsigned long long)limit);
            CHECK(st.slabs_scanned <= limit);
            piece_test_reset_stats(t); CHECK(piece_len(t) == sizes[j] - (i + 1u) * 1024u);
            s = piece_snapshot_take(t); CHECK(s);
            st = piece_test_get_stats(t); CHECK(st.slabs_scanned <= st.pool_returns + 16);
            piece_test_reset_stats(t); piece_snapshot_release(s);
            st = piece_test_get_stats(t); CHECK(st.slabs_scanned <= st.pool_returns + 16);
        }
        piece_destroy(t);
    }
    /* A released old root can queue a tree-sized amount of dead storage
     * while another snapshot stays live. Queries/takes drain a fixed batch. */
    for (unsigned remaining = 0; remaining <= 8; remaining += 8) {
        piece_tree *t = fragments(65536); CHECK(t);
        piece_snapshot *old = piece_snapshot_take(t); CHECK(old);
        CHECK(!piece_delete(t, 0, 65536 - remaining, NULL));
        piece_snapshot *current = piece_snapshot_take(t); CHECK(current);
        piece_snapshot_release(old);
        piece_test_reset_stats(t); CHECK(piece_len(t) == remaining);
        piece_test_stats st = piece_test_get_stats(t);
        printf("trim work: pending returns length=%u slabs=%llu bound=64\n", remaining, (unsigned long long)st.slabs_scanned);
        CHECK(st.slabs_scanned <= 64);
        piece_test_reset_stats(t); piece_snapshot *next = piece_snapshot_take(t); CHECK(next);
        st = piece_test_get_stats(t);
        printf("trim work: pending returns take slabs=%llu bound=64\n", (unsigned long long)st.slabs_scanned);
        CHECK(st.slabs_scanned <= 64);
        piece_snapshot_release(next); piece_snapshot_release(current); piece_destroy(t);
    }
    puts("foreground slab work: ok (reclaimed storage only, independent of live pool)"); return 0;
}
static int range_work(void) {
    const size_t n = 65536; piece_tree *t = fragments(n); CHECK(t);
    uint8_t *out = malloc(n); CHECK(out);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    piece_test_memory m = piece_test_get_memory(t);
    uint64_t limit = m.live[0] + m.live[1];
    piece_test_reset_stats(t); CHECK(!piece_read(t, 0, out, n));
    for (size_t i = 0; i < n; i++) CHECK(out[i] == (uint8_t)((n - 1 - i) % 251));
    piece_test_stats st = piece_test_get_stats(t);
    printf("range work: tree nodes=%llu bound=%llu height=%u\n",
           (unsigned long long)st.walk_nodes, (unsigned long long)limit, st.height);
    CHECK(st.walk_nodes <= limit);
    piece_test_reset_stats(t); CHECK(!piece_snapshot_read(s, 13, out, n - 26));
    for (size_t i = 0; i < n - 26; i++) CHECK(out[i] == (uint8_t)((n - 14 - i) % 251));
    st = piece_test_get_stats(t);
    printf("range work: snapshot nodes=%llu bound=%llu\n", (unsigned long long)st.walk_nodes, (unsigned long long)limit);
    CHECK(st.walk_nodes <= limit);
    piece_test_reset_stats(t); piece_ref r; CHECK(!piece_delete(t, 13, n - 26, &r));
    st = piece_test_get_stats(t);
    printf("range work: collect/fallback nodes=%llu bound=%llu\n", (unsigned long long)st.walk_nodes,
           (unsigned long long)(3 * limit));
    CHECK(st.walk_nodes <= 3 * limit);
    CHECK(!piece_insert_ref(t, 13, &r)); CHECK(!piece_read(t, 0, out, n));
    for (size_t i = 0; i < n; i++) CHECK(out[i] == (uint8_t)((n - 1 - i) % 251));
    piece_snapshot_release(s); piece_destroy(t); free(out);
    puts("fragmented range work: ok (tree/snapshot/content/collection/fallback)"); return 0;
}
static int ref_work(void) {
    const size_t n = 1u << 20;
    uint8_t *data = malloc(n), *out = malloc(n); CHECK(data && out);
    uint64_t lines = 1;
    for (size_t i = 0; i < n; i++) { data[i] = i % 31 == 0 ? '\n' : (uint8_t)(i % 251); lines += data[i] == '\n'; }
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, (const uint8_t *)"..", 2));
    CHECK(!piece_insert(t, 1, data, n)); piece_ref r; CHECK(!piece_delete(t, 1, n, &r));
    for (unsigned i = 0; i < 4; i++) {
        piece_test_reset_stats(t); CHECK(!piece_insert_ref(t, 1, &r));
        piece_test_stats st = piece_test_get_stats(t);
        printf("reference work: replay=%u bytes=%llu bound=510 descents=%llu\n", i,
               (unsigned long long)st.ref_recount_bytes, (unsigned long long)st.root_descents);
        CHECK(st.ref_recount_bytes <= 510);
        CHECK(!st.ref_recount_bytes); /* aligned full chunks use only prefixes */
        CHECK(st.root_descents <= 2); /* two bounded batches for sixteen chunks */
        CHECK(piece_line_count(t) == lines); CHECK(!piece_read(t, 1, out, n) && !memcmp(data, out, n));
        CHECK(!piece_delete(t, 1, n, NULL));
    }
    /* Partial endpoints and overlapping/noncontiguous spans use cached block
     * prefixes too; every span has at most two short recounts. */
    piece_ref partial = { .nspans = 3, .len = n - 40 + 400,
                         .span = {{13, n - 40}, {250, 300}, {65530, 100}} };
    piece_test_reset_stats(t); CHECK(!piece_insert_ref(t, 1, &partial));
    piece_test_stats st = piece_test_get_stats(t);
    printf("reference work: partial bytes=%llu bound=1530\n", (unsigned long long)st.ref_recount_bytes);
    CHECK(st.ref_recount_bytes <= 3 * 510);
    CHECK(!piece_read(t, 1, out, n - 40)); CHECK(!memcmp(out, data + 13, n - 40));
    CHECK(!piece_read(t, n - 39, out, 300) && !memcmp(out, data + 250, 300));
    CHECK(!piece_read(t, n + 261, out, 100) && !memcmp(out, data + 65530, 100));
    uint64_t expected = 1;
    for (unsigned i = 0; i < partial.nspans; i++)
        for (uint64_t k = 0; k < partial.span[i].len; k++) expected += data[partial.span[i].add_off + k] == '\n';
    CHECK(piece_line_count(t) == expected);
    piece_destroy(t);
    /* Force bulk insertion through full leaves and several branch levels,
     * with COW and an unaligned ADD chunk start. */
    t = fragments(4096); CHECK(t);
    CHECK(!piece_insert(t, 2048, data, n)); CHECK(!piece_delete(t, 2048, n, &r));
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    CHECK(!piece_insert_ref(t, 2048, &r)); CHECK(!piece_read(t, 2048, out, n) && !memcmp(out, data, n));
    CHECK(!piece_snapshot_read(s, 0, out, 4096));
    for (size_t i = 0; i < 4096; i++) CHECK(out[i] == (uint8_t)((4095 - i) % 251));
    CHECK(!piece_delete(t, 2048, n, NULL)); CHECK(!piece_read(t, 0, out, 4096));
    for (size_t i = 0; i < 4096; i++) CHECK(out[i] == (uint8_t)((4095 - i) % 251));
    piece_snapshot_release(s); piece_destroy(t);
    /* The full-chunk count needs seventeen bits even though every interior
     * prefix fits in sixteen. Check both ends of that representation. */
    memset(data, '\n', 65536); t = piece_create(&a); CHECK(t);
    CHECK(!piece_insert(t, 0, data, 65536)); CHECK(!piece_delete(t, 0, 65536, &r));
    CHECK(!piece_insert_ref(t, 0, &r) && piece_line_count(t) == 65537);
    piece_test_reset_stats(t); CHECK(!piece_insert_ref(t, 32769, &r));
    CHECK(piece_line_count(t) == 131073 && piece_test_get_stats(t).ref_recount_bytes <= 1022);
    CHECK(!piece_delete(t, 32769, 65536, NULL) && piece_line_count(t) == 65537);
    CHECK(!piece_delete(t, 0, 65536, NULL));
    partial.nspans = 1; partial.len = 64513; partial.span[0].add_off = 511; partial.span[0].len = 64513;
    CHECK(!piece_insert_ref(t, 0, &partial) && piece_line_count(t) == 64514);
    piece_destroy(t); free(data); free(out);
    puts("large reference work: ok (bounded prefix edges; bulk replay; exact bytes/newlines)"); return 0;
}
static int ref_cache_rollback(void) {
    uint8_t before[250], inside[777], after[521], out[1027];
    for (size_t i = 0; i < sizeof before; i++) before[i] = i % 3 == 0 ? '\n' : 'b';
    for (size_t i = 0; i < sizeof inside; i++) inside[i] = i % 11 == 0 ? '\n' : 'i';
    for (size_t i = 0; i < sizeof after; i++) after[i] = i % 17 == 0 ? '\n' : 'a';
    for (unsigned pinned = 0; pinned < 2; pinned++) {
        piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); CHECK(t);
        CHECK(!piece_insert(t, 0, before, sizeof before));
        piece_checkpoint *cp; CHECK(!piece_checkpoint_begin(t, &cp));
        CHECK(!piece_insert(t, sizeof before, inside, sizeof inside));
        piece_snapshot *s = pinned ? piece_snapshot_take(t) : NULL; CHECK(!pinned || s);
        piece_checkpoint_abort(cp);
        /* Exercise both cursor typing and bulk writes through saved partial
         * and complete block boundaries after restoration. */
        CHECK(!piece_insert(t, sizeof before, after, 1));
        CHECK(!piece_insert(t, sizeof before + 1, after + 1, sizeof after - 1));
        uint64_t lines = 1;
        for (size_t i = 0; i < sizeof before; i++) lines += before[i] == '\n';
        for (size_t i = 0; i < sizeof after; i++) lines += after[i] == '\n';
        piece_ref r; CHECK(!piece_delete(t, 0, sizeof before + sizeof after, &r));
        CHECK(!piece_insert_ref(t, 0, &r)); CHECK(piece_line_count(t) == lines);
        CHECK(!piece_read(t, 0, out, sizeof before + sizeof after));
        CHECK(!memcmp(out, before, sizeof before) && !memcmp(out + sizeof before, after, sizeof after));
        piece_destroy(t);
        if (s) {
            CHECK(!piece_snapshot_read(s, 0, out, sizeof out));
            CHECK(!memcmp(out, before, sizeof before) && !memcmp(out + sizeof before, inside, sizeof inside));
            piece_snapshot_release(s);
        }
    }
    puts("reference prefix rollback: ok (saved partial block; pinned/unpinned transaction snapshot)"); return 0;
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
    if (argc == 1 || !strcmp(argv[1], "--trim-work")) bad |= trim_work();
    if (argc == 1 || !strcmp(argv[1], "--range-work")) bad |= range_work();
    if (argc == 1 || !strcmp(argv[1], "--ref-work")) { bad |= ref_work(); bad |= ref_cache_rollback(); }
#endif
    if (argc == 1 || !strcmp(argv[1], "--wide")) bad |= high_offsets();
    puts(bad ? "piece_bounds_test: FAILED" : "piece_bounds_test: ok");
    return bad;
}
