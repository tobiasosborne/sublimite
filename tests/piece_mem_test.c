/* P1.4e allocator ownership regressions; inspect live bytes before cleanup
 * queries. The perf G10f edit term funds deleted ORIGINAL bytes only. */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __func__, __LINE__, #c); return 1; \
} } while (0)
typedef struct mem_allocator {
    atomic_size_t live, peak, calls;
    size_t remaining; int armed;
} mem_allocator;
static void *mem_alloc(void *ctx, size_t n) {
    mem_allocator *m = ctx; atomic_fetch_add(&m->calls, 1);
    if (m->armed) { if (!m->remaining) return NULL; m->remaining--; }
    void *p = malloc(n); if (!p) return NULL;
    size_t live = atomic_fetch_add(&m->live, n) + n, peak = atomic_load(&m->peak);
    while (peak < live && !atomic_compare_exchange_weak(&m->peak, &peak, live)) {}
    return p;
}
static void mem_free(void *ctx, void *p, size_t n) {
    mem_allocator *m = ctx; atomic_fetch_sub(&m->live, n); free(p);
}
static piece_allocator mem_hooks(mem_allocator *m) { return (piece_allocator){m, mem_alloc, mem_free}; }
static double bound(uint64_t original, uint64_t pieces, uint64_t typed, uint64_t deleted_original) {
    return 1.25 * (double)original + 65536.0 + 4096.0 + 96.0 * (double)pieces +
           1.25 * ((double)typed + (double)deleted_original);
}
static int thin_leaves(void) {
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0));
    for (unsigned i = 0; i < 10000; i++) CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    for (unsigned i = 0; i < 15; i++) CHECK(!piece_delete(t, 0, 1, NULL));
    for (uint64_t k = 1; k <= 1248; k++) {
        CHECK(!piece_insert(t, k + 1, (const uint8_t *)"x", 1));
        CHECK(!piece_delete(t, k + 1, 1, NULL));
        for (unsigned i = 0; i < 7; i++) CHECK(!piece_delete(t, k, 1, NULL));
    }
    uint64_t pieces = piece_piece_count(t); double limit = bound(0, pieces, 11248, 0);
    printf("thin leaves: live=%zu bound=%.2f pieces=%llu\n", atomic_load(&m.live), limit, (unsigned long long)pieces);
#ifdef PIECE_TESTING
    piece_test_memory st = piece_test_get_memory(t);
    printf("thin layout: leaf slabs=%zu live=%zu underfull=%zu entries=%zu; branch slabs=%zu live=%zu\n",
           st.slabs[0], st.live[0], st.underfull_leaves, st.leaf_pieces, st.slabs[1], st.live[1]);
#endif
    CHECK((double)atomic_load(&m.live) <= limit);
    piece_destroy(t); CHECK(!atomic_load(&m.live)); puts("thin leaves: ok"); return 0;
}
static int version_headers(void) {
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0));
    for (unsigned i = 0; i < 10000; i++) CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    piece_snapshot *snapshots[5000];
    for (unsigned i = 0; i < 5000; i++) {
        snapshots[i] = piece_snapshot_take(t); CHECK(snapshots[i]);
        CHECK(!piece_insert(t, 1u + i, (const uint8_t *)"x", 1));
    }
    double limit = bound(0, piece_piece_count(t), 15000, 0) + 1920.0 * 5000.0;
    printf("version headers: live=%zu bound=%.2f\n", atomic_load(&m.live), limit);
    CHECK((double)atomic_load(&m.live) <= limit);
    for (unsigned i = 0; i < 5000; i++) {
        uint8_t b; CHECK(piece_snapshot_len(snapshots[i]) == 10000u + i);
        CHECK(!piece_snapshot_read(snapshots[i], i, &b, 1) && b == 'x');
        piece_snapshot_release(snapshots[i]);
    }
    piece_destroy(t); CHECK(!atomic_load(&m.live)); puts("version headers: ok"); return 0;
}
#ifdef PIECE_TESTING
/* The primary perf formula funds actual live snapshot paths. The frozen
 * header's constant 1920 cannot describe deeper copies; --depth-contract
 * keeps that distinct contract defect reproducible without masking it. */
static int deeper_snapshots(int fixed_contract) {
    for (unsigned height = 1; height <= 15; height++) {
        mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
        CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1)); CHECK(!piece_delete(t, 0, 1, NULL));
        uint64_t len = (UINT64_C(1) << (4 * height)) + 1;
        CHECK(!piece_test_repeat_byte(t, len));
        piece_snapshot *s = piece_snapshot_take(t); CHECK(s); piece_test_reset_stats(t);
        size_t before = atomic_load(&m.live);
        CHECK(!piece_insert(t, 1, (const uint8_t *)"x", 1));
        piece_test_stats st = piece_test_get_stats(t);
        uint64_t path = (uint64_t)st.leaf_bytes + (uint64_t)st.height * st.branch_bytes;
        CHECK(st.height == height && st.pathcopy_bytes == path);
        /* Each class has two slots per slab, with 16 bytes of bookkeeping.
         * A class's trailing spare slot is explicitly counted too. */
        double actual_path_funding = (double)path + (double)st.snapshot_bytes +
                                     8.0 * (double)(height + 2) + (double)st.leaf_bytes +
                                     (double)st.branch_bytes + (double)st.snapshot_bytes + 48.0;
        CHECK((double)atomic_load(&m.live) <= (double)before + actual_path_funding);
        uint8_t b; CHECK(piece_snapshot_len(s) == len && !piece_snapshot_read(s, 0, &b, 1) && b == 'x');
        printf("deep snapshot: height=%u path=%llu header=%zu slab_B_per_slot=8\n", st.height,
               (unsigned long long)st.pathcopy_bytes, st.snapshot_bytes);
        if (fixed_contract) CHECK(st.pathcopy_bytes + st.snapshot_bytes + 8u * (height + 2u) <= 1920);
        piece_snapshot_release(s); piece_destroy(t); CHECK(!atomic_load(&m.live));
    }
    puts("deeper snapshots: ok (actual paths/headers/slab overhead; checked uint64_t depth)"); return 0;
}
#endif
static int overflow_delete(int requested) {
    const size_t block = 65536; uint8_t *data = malloc(block); CHECK(data); memset(data, 'x', block);
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0)); piece_ref refs[9];
    for (unsigned i = 0; i < 9; i++) {
        CHECK(!piece_insert(t, 0, data, block)); CHECK(!piece_delete(t, 0, block, &refs[i]));
    }
    for (unsigned i = 9; i-- > 0;) CHECK(!piece_insert_ref(t, piece_len(t), &refs[i]));
    piece_ref all; CHECK(!piece_delete(t, 0, piece_len(t), requested ? &all : NULL));
    double limit = bound(0, 0, 9 * block, 0) + (requested ? 64.0 : 0.0);
    printf("overflow delete (%s ref): live=%zu bound=%.2f\n", requested ? "requested" : "no", atomic_load(&m.live), limit);
#ifdef PIECE_TESTING
    piece_test_memory st = piece_test_get_memory(t);
    printf("delete accounting: original=%llu fallback_ADD=%llu\n", (unsigned long long)st.deleted_original,
           (unsigned long long)st.fallback_add);
    CHECK(!st.deleted_original && st.fallback_add == (requested ? 9 * block : 0));
#endif
    CHECK((double)atomic_load(&m.live) <= limit);
    if (requested) {
        CHECK(all.len == 9 * block && all.nspans == 1);
        CHECK(!piece_insert_ref(t, 0, &all));
        uint8_t b; CHECK(!piece_read(t, 8 * block, &b, 1) && b == 'x');
    }
    piece_destroy(t); free(data); CHECK(!atomic_load(&m.live)); puts("overflow delete: ok"); return 0;
}
static void *release_snapshots(void *ctx) {
    piece_snapshot **snapshots = ctx;
    for (unsigned i = 0; i < 32; i++) piece_snapshot_release(snapshots[i]);
    return NULL;
}
static int final_release(int worker, int cached) {
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0)); CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    piece_snapshot *snapshots[32];
    for (unsigned i = 0; i < 32; i++) {
        snapshots[i] = piece_snapshot_take(t); CHECK(snapshots[i]);
        CHECK(!piece_insert(t, i + 1u, (const uint8_t *)"x", 1));
    }
    if (cached) { piece_snapshot *current = piece_snapshot_take(t); CHECK(current); piece_snapshot_release(current); }
    if (worker) {
        pthread_t th; CHECK(!pthread_create(&th, NULL, release_snapshots, snapshots)); CHECK(!pthread_join(th, NULL));
    } else (void)release_snapshots(snapshots);
    double limit = bound(0, 1, 33, 0);
    printf("final release (%s%s): live=%zu bound=%.2f\n", worker ? "worker" : "owner", cached ? ", current cache" : "", atomic_load(&m.live), limit);
    CHECK((double)atomic_load(&m.live) <= limit); /* no piece query has run */
    piece_destroy(t); CHECK(!atomic_load(&m.live)); puts("final release: ok"); return 0;
}
static int checkpoint_old_owner(void) {
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0)); CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    piece_snapshot *before = piece_snapshot_take(t); CHECK(before);
    uint8_t *large = malloc(2u << 20); CHECK(large); memset(large, 'z', 2u << 20);
    double limit = bound(0, 1, 1, 0);
    for (unsigned i = 0; i < 8; i++) {
        piece_checkpoint *cp = NULL; CHECK(!piece_checkpoint_begin(t, &cp));
        CHECK(!piece_insert(t, 0, large, 2u << 20));
        size_t calls = atomic_load(&m.calls); m.armed = 1; m.remaining = 0;
        piece_checkpoint_abort(cp); m.armed = 0; CHECK(atomic_load(&m.calls) == calls);
        printf("abort with earlier owner: live=%zu bound=%.2f\n", atomic_load(&m.live), limit);
        CHECK((double)atomic_load(&m.live) <= limit); /* before any tree query */
        uint8_t b; CHECK(!piece_snapshot_read(before, 0, &b, 1) && b == 'x');
    }
    piece_snapshot_release(before); piece_destroy(t); free(large); CHECK(!atomic_load(&m.live));
    puts("abort with earlier owner: ok (transaction reservations reclaimed immediately)"); return 0;
}
static int destroy_old_owner(void) {
    mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(!piece_init_copy(t, NULL, 0)); CHECK(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    piece_snapshot *before = piece_snapshot_take(t); CHECK(before);
    uint8_t *large = malloc(2u << 20); CHECK(large); memset(large, 'z', 2u << 20);
    CHECK(!piece_insert(t, 1, large, 2u << 20)); piece_destroy(t);
    double limit = bound(0, 1, 1, 0);
    printf("destroy with earlier owner: live=%zu bound=%.2f\n", atomic_load(&m.live), limit);
    CHECK((double)atomic_load(&m.live) <= limit);
    uint8_t b; CHECK(!piece_snapshot_read(before, 0, &b, 1) && b == 'x');
    piece_snapshot_release(before); free(large); CHECK(!atomic_load(&m.live));
    puts("destroy with earlier owner: ok (only snapshot-referenced ADD prefix survives)"); return 0;
}
static int init_failures(void) {
    /* Mapped initialization never reads the fixture at initialization. The
     * one-byte pointer models a large immutable mapping for allocation-only
     * probes; no mapped content query is made. */
    uint8_t byte = 'o'; const size_t mapped_len = (size_t)1 << 30;
    for (unsigned mode = 0; mode < 3; mode++) {
        size_t requests = 0;
        mem_allocator healthy = {0}; piece_allocator ha = mem_hooks(&healthy);
        piece_tree *ht = piece_create(&ha); CHECK(ht);
        if (mode) {
            size_t before = atomic_load(&healthy.calls);
            CHECK(!(mode == 1 ? piece_init_copy(ht, &byte, 1) : piece_init_mapped(ht, &byte, mapped_len, NULL)));
            requests = atomic_load(&healthy.calls) - before;
        } else requests = atomic_load(&healthy.calls);
        piece_destroy(ht); CHECK(!atomic_load(&healthy.live));
        for (size_t point = 0; point <= requests; point++) {
            mem_allocator m = {0}; piece_allocator a = mem_hooks(&m); piece_tree *t = NULL;
            if (mode) { t = piece_create(&a); CHECK(t); }
            size_t baseline = atomic_load(&m.live); m.armed = 1; m.remaining = point;
            int rc = 0;
            if (!mode) t = piece_create(&a);
            else rc = mode == 1 ? piece_init_copy(t, &byte, 1) : piece_init_mapped(t, &byte, mapped_len, NULL);
            m.armed = 0;
            if (!mode && !t) CHECK(!atomic_load(&m.live));
            if (mode && rc) {
                CHECK(rc == PIECE_ERR_NOMEM);
                if (atomic_load(&m.live) != baseline)
                    printf("init rollback mode=%u point=%zu live=%zu baseline=%zu\n", mode, point, atomic_load(&m.live), baseline);
                CHECK(atomic_load(&m.live) == baseline); /* before any cleanup query */
                CHECK(!piece_init_copy(t, NULL, 0));
                CHECK((double)atomic_load(&m.live) <= bound(0, 0, 0, 0));
            }
            if (t) piece_destroy(t);
            CHECK(!atomic_load(&m.live));
        }
    }
    puts("initialization failure sweep: ok (create/copy/mapped; immediate rollback)"); return 0;
}
int main(int argc, char **argv) {
    int bad = 0;
    if (argc == 1 || !strcmp(argv[1], "--thin")) bad |= thin_leaves();
    if (argc == 1 || !strcmp(argv[1], "--versions")) bad |= version_headers();
#ifdef PIECE_TESTING
    if (argc == 1 || !strcmp(argv[1], "--depth")) bad |= deeper_snapshots(0);
    if (argc > 1 && !strcmp(argv[1], "--depth-contract")) bad |= deeper_snapshots(1);
#endif
    if (argc == 1 || !strcmp(argv[1], "--overflow")) bad |= overflow_delete(0);
    if (argc > 1 && !strcmp(argv[1], "--requested")) bad |= overflow_delete(1);
    if (argc == 1 || !strcmp(argv[1], "--release")) { bad |= final_release(0, 0); bad |= final_release(1, 0); bad |= final_release(1, 1); }
    if (argc == 1 || !strcmp(argv[1], "--init")) bad |= init_failures();
    if (argc == 1 || !strcmp(argv[1], "--checkpoint-owners")) bad |= checkpoint_old_owner();
    if (argc == 1 || !strcmp(argv[1], "--destroy-owners")) bad |= destroy_old_owner();
    puts(bad ? "piece_mem_test: FAILED" : "piece_mem_test: ok"); return bad;
}
