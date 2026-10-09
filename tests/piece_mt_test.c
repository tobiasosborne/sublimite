/* Synthesis regressions plus snapshot readers/releases concurrent with edits. */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include "base/base.h"
#include "piece_model.h"
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "piece_mt_test:%d: %s\n", __LINE__, #c); return 1; } } while (0)

#ifdef PIECE_TEST_WRAP_THREADS
static atomic_uint thread_creates;
int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg) {
    atomic_fetch_add_explicit(&thread_creates, 1, memory_order_relaxed);
    return __real_pthread_create(thread, attr, start, arg);
}
static int kernel_threads_test(void) {
    size_t n = 32u << 20; uint8_t *map = malloc(n); REQUIRE(map);
    memset(map, 'a', n); map[1234] = '\n'; map[n - 1] = '\n';
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    REQUIRE(!piece_init_mapped(t, map, n, NULL));
    unsigned before = atomic_load(&thread_creates);
    REQUIRE(piece_line_count(t) == 3 && piece_line_to_byte(t, 1) == 1235);
    unsigned created = atomic_load(&thread_creates) - before;
    piece_destroy(t); free(map);
    printf("kernel cold count: threads_created=%u\n", created);
    if (created) puts("FAIL threads: piece queries must count without creating workers");
    return created != 0;
}
#endif

#ifdef PIECE_TESTING
typedef struct {
    pthread_barrier_t *barrier;
    piece_snapshot *snapshot, *retained;
} owner_race_job;
static void *owner_race_retain(void *ctx) {
    owner_race_job *j = ctx;
    (void)pthread_barrier_wait(j->barrier);
    j->retained = piece_snapshot_retain(j->snapshot);
    return NULL;
}
static int owner_race_test(void) {
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    REQUIRE(!piece_init_copy(t, (const uint8_t *)"x", 1));
    piece_snapshot *s = piece_snapshot_take(t); REQUIRE(s);
    piece_test_snapshot_set_owners(s, UINT_MAX - 1);
    pthread_barrier_t barrier; REQUIRE(!pthread_barrier_init(&barrier, NULL, 5));
    pthread_t threads[4]; owner_race_job jobs[4];
    for (unsigned i = 0; i < 4; i++) {
        jobs[i] = (owner_race_job){ &barrier, s, NULL };
        REQUIRE(!pthread_create(&threads[i], NULL, owner_race_retain, &jobs[i]));
    }
    (void)pthread_barrier_wait(&barrier);
    unsigned retained = 0;
    for (unsigned i = 0; i < 4; i++) {
        REQUIRE(!pthread_join(threads[i], NULL)); retained += jobs[i].retained != NULL;
    }
    REQUIRE(retained == 1 && piece_test_snapshot_owners(s) == UINT_MAX);
    for (unsigned i = 0; i < 4; i++) if (jobs[i].retained) piece_snapshot_release(jobs[i].retained);
    REQUIRE(piece_test_snapshot_owners(s) == UINT_MAX - 1);
    piece_test_snapshot_set_owners(s, 2);
    REQUIRE(!pthread_barrier_destroy(&barrier));
    piece_destroy(t);
    uint8_t b = 0; REQUIRE(!piece_snapshot_read(s, 0, &b, 1) && b == 'x');
    piece_snapshot_release(s);
    puts("snapshot owner race: ok (one winner for the final slot; no wrap)");
    return 0;
}
static int cursor_test(void) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    uint8_t *o = calloc(2u << 20, 1); REQUIRE(t && o);
    REQUIRE(!piece_init_copy(t, o, 2u << 20));
    REQUIRE(!piece_insert(t, 777777, (const uint8_t *)"x", 1));
    piece_test_reset_stats(t);
    for (uint64_t i = 1; i <= 1000; i++) REQUIRE(!piece_insert(t, 777777 + i, (const uint8_t *)"x", 1));
    piece_test_stats s = piece_test_get_stats(t);
    printf("cursor: root_descents=%llu hits=%llu\n", (unsigned long long)s.root_descents, (unsigned long long)s.cursor_hits);
    int bad = s.root_descents != 0 || s.cursor_hits != 1000;
    piece_snapshot *snap = piece_snapshot_take(t); REQUIRE(snap);
    REQUIRE(!piece_insert(t, 778778, (const uint8_t *)"\n", 1));
    REQUIRE(piece_snapshot_len(snap) == (2u << 20) + 1001);
    uint8_t b; REQUIRE(!piece_snapshot_read(snap, 778777, &b, 1) && b == 'x');
    piece_snapshot_release(snap); piece_destroy(t); free(o);
    if (bad) puts("FAIL cursor: cached typing must avoid root descent");
    return bad;
}
static int compact_test(void) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    uint8_t *o = calloc(2u << 20, 1); REQUIRE(t && o);
    REQUIRE(!piece_init_copy(t, o, 2u << 20));
    piece_snapshot *s = piece_snapshot_take(t); REQUIRE(s);
    piece_test_reset_stats(t);
    REQUIRE(!piece_insert(t, 12345, (const uint8_t *)"\n", 1));
    piece_test_stats st = piece_test_get_stats(t);
    printf("compact: leaf=%zu branch=%zu pathcopy=%llu\n", st.leaf_bytes, st.branch_bytes, (unsigned long long)st.pathcopy_bytes);
    int bad = st.leaf_bytes > 288 || st.branch_bytes > 400 || st.pathcopy_bytes > 720;
    REQUIRE(piece_snapshot_len(s) == (2u << 20) && piece_snapshot_line_count(s) == 1);
    piece_snapshot_release(s); piece_destroy(t); free(o);
    if (bad) puts("FAIL compact: snapshot path copy must fit compact node classes");
    return bad;
}
#ifdef PIECE_GAP_TRIAL
static int gap_test(void) {
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    REQUIRE(!piece_init_copy(t, (const uint8_t *)"0123456789", 10));
    REQUIRE(!piece_insert(t, 5, (const uint8_t *)"abcdefgh\nijklmno", 16));
    uint64_t before = piece_piece_count(t);
    piece_test_reset_stats(t);
    REQUIRE(!piece_delete(t, 9, 4, NULL));
    piece_test_stats st = piece_test_get_stats(t);
    printf("gap: local_deletes=%llu pieces=%llu->%llu\n", (unsigned long long)st.gap_deletes,
           (unsigned long long)before, (unsigned long long)piece_piece_count(t));
    int bad = st.gap_deletes != 1 || piece_piece_count(t) != before;
    uint8_t got[22]; REQUIRE(!piece_read(t, 0, got, sizeof got));
    REQUIRE(!memcmp(got, "01234abcd\nijklmno56789", sizeof got));
    piece_destroy(t);
    if (bad) puts("FAIL gap: interior delete must reuse the edit window");
    return bad;
}
#endif
#endif

typedef struct mt_ctx {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    struct mt_job *head;
    int done;
    atomic_uint bad, checked, owners;
} mt_ctx;
typedef struct mt_job {
    struct mt_job *next;
    piece_snapshot *s;
    pm_model model;
} mt_job;
static void map_acquire(void *p) { mt_ctx *c = p; atomic_fetch_add(&c->owners, 1); }
static void map_release(void *p) { mt_ctx *c = p; atomic_fetch_sub(&c->owners, 1); }
static int snapshot_same(piece_snapshot *s, const pm_model *m) {
    uint8_t *b = malloc(m->n + 1);
    if (!b) return 0;
    int ok = piece_snapshot_len(s) == m->n && !piece_snapshot_read(s, 0, b, m->n) && !memcmp(b, m->d, m->n);
    free(b);
    if (piece_snapshot_line_count(s) != pm_line_count(m)) ok = 0;
    piece_iter it; const uint8_t *p; size_t n, pos = 0;
    piece_iter_begin_snapshot(&it, s, 0);
    while (piece_iter_next(&it, &p, &n)) {
        if (!n || pos + n > m->n || memcmp(p, m->d + pos, n)) { ok = 0; break; }
        pos += n;
    }
    if (pos != m->n) ok = 0;
    for (uint64_t l = 0, lc = pm_line_count(m); l < lc + 1; l += lc / 8 + 1)
        if (piece_snapshot_line_to_byte(s, l) != pm_line_to_byte(m, l)) ok = 0;
    for (size_t off = 0; off <= m->n; off += m->n / 8 + 1)
        if (piece_snapshot_byte_to_line(s, off) != pm_byte_to_line(m, off)) ok = 0;
    return ok;
}
static void *mt_reader(void *p) {
    mt_ctx *c = p;
    for (;;) {
        pthread_mutex_lock(&c->mu);
        while (!c->head && !c->done) pthread_cond_wait(&c->cv, &c->mu);
        mt_job *j = c->head;
        if (j) c->head = j->next;
        pthread_mutex_unlock(&c->mu);
        if (!j) break;
        if (!snapshot_same(j->s, &j->model)) atomic_fetch_add(&c->bad, 1);
        piece_snapshot_release(j->s); pm_free(&j->model); free(j);
        atomic_fetch_add(&c->checked, 1);
    }
    return NULL;
}
static uint64_t mt_rand(uint64_t *r) { *r ^= *r << 13; *r ^= *r >> 7; *r ^= *r << 17; return *r; }
static int mt_test(void) {
    mt_ctx c = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };
    atomic_init(&c.bad, 0); atomic_init(&c.checked, 0); atomic_init(&c.owners, 0);
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    size_t len = 2u << 20; uint8_t *map = malloc(len); REQUIRE(map);
    for (size_t i = 0; i < len; i++) map[i] = (uint8_t)(i % 113 == 0 ? '\n' : 'a' + i % 26);
    piece_map_hooks h = { &c, map_acquire, map_release };
    REQUIRE(!piece_init_mapped(t, map, len, &h));
    pthread_t th[4];
    for (unsigned i = 0; i < 4; i++) REQUIRE(!pthread_create(&th[i], NULL, mt_reader, &c));
    /* Many owners read the same unresolved nodes and release on worker threads. */
    piece_snapshot *shared = piece_snapshot_take(t); REQUIRE(shared);
    for (unsigned i = 0; i < 8; i++) {
        mt_job *j = malloc(sizeof *j); REQUIRE(j); j->s = piece_snapshot_retain(shared); pm_init(&j->model, map, len);
        pthread_mutex_lock(&c.mu); j->next = c.head; c.head = j; pthread_cond_signal(&c.cv); pthread_mutex_unlock(&c.mu);
    }
    piece_snapshot_release(shared);
    uint64_t r = UINT64_C(88172645463325252);
    for (unsigned i = 0; i < 3000; i++) {
        uint64_t off = mt_rand(&r) % (piece_len(t) + 1);
        if (i % 3) {
            /* Cross several 64 KiB chunks and grow/retire the add directory
             * while old snapshots still read it on the other threads. */
            uint8_t b[1023]; size_t n = (size_t)(mt_rand(&r) % sizeof b) + 1;
            for (size_t k = 0; k < n; k++) b[k] = (uint8_t)(k % 7 == 0 ? '\n' : 'x');
            REQUIRE(!piece_insert(t, off, b, n));
        } else {
            uint64_t n = mt_rand(&r) % 100;
            if (n > piece_len(t) - off) n = piece_len(t) - off;
            piece_ref ref; REQUIRE(!piece_delete(t, off, n, &ref));
            if (i % 2) REQUIRE(!piece_insert_ref(t, off, &ref));
        }
        if (i % 97 == 0) {
            mt_job *j = malloc(sizeof *j); REQUIRE(j);
            size_t n = (size_t)piece_len(t); uint8_t *copy = malloc(n + 1); REQUIRE(copy);
            REQUIRE(!piece_read(t, 0, copy, n)); pm_init(&j->model, copy, n); free(copy);
            j->s = piece_snapshot_take(t); REQUIRE(j->s);
            pthread_mutex_lock(&c.mu); j->next = c.head; c.head = j; pthread_cond_signal(&c.cv); pthread_mutex_unlock(&c.mu);
        }
        if (i % 83 == 0) (void)piece_line_count(t);
    }
    /* Queue four final owners while holding the mailbox lock. They cannot
     * start their reads until after the tree has actually been destroyed. */
    piece_snapshot *final = piece_snapshot_take(t); REQUIRE(final);
    size_t final_len = (size_t)piece_len(t); uint8_t *copy = malloc(final_len + 1); REQUIRE(copy);
    REQUIRE(!piece_read(t, 0, copy, final_len));
    pthread_mutex_lock(&c.mu);
    for (unsigned i = 0; i < 4; i++) {
        mt_job *j = malloc(sizeof *j); REQUIRE(j);
        j->s = piece_snapshot_retain(final); pm_init(&j->model, copy, final_len);
        j->next = c.head; c.head = j;
    }
    free(copy); piece_snapshot_release(final);
    piece_destroy(t);
    c.done = 1; pthread_cond_broadcast(&c.cv); pthread_mutex_unlock(&c.mu);
    for (unsigned i = 0; i < 4; i++) REQUIRE(!pthread_join(th[i], NULL));
    unsigned bad = atomic_load(&c.bad), checked = atomic_load(&c.checked);
    REQUIRE(atomic_load(&c.owners) == 0); free(map);
    pthread_cond_destroy(&c.cv); pthread_mutex_destroy(&c.mu);
    printf("piece_mt_test: %s (%u snapshots checked, mapping owners=0)\n", bad ? "FAILED" : "ok", checked);
    return bad != 0;
}
static int checkpoint_unchanged_snapshot_test(void) {
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    for (size_t i = 0; i < 4; i++) REQUIRE(!piece_insert(t, i, (const uint8_t *)"tail" + i, 1));
    piece_checkpoint *cp = NULL; REQUIRE(!piece_checkpoint_begin(t, &cp));
    piece_snapshot *s = piece_snapshot_take(t); REQUIRE(s);
    piece_checkpoint_abort(cp);
    REQUIRE(!piece_insert(t, 4, (const uint8_t *)"\n", 1));
    REQUIRE(piece_snapshot_len(s) == 4 && piece_snapshot_line_count(s) == 1);
    piece_iter it; const uint8_t *p; size_t n;
    piece_iter_begin_snapshot(&it, s, 0); REQUIRE(piece_iter_next(&it, &p, &n) && n == 4 && !memcmp(p, "tail", 4));
    piece_snapshot_release(s); piece_destroy(t);
    puts("checkpoint unchanged snapshot: ok (abort restores run without reusing a shared cursor)"); return 0;
}
static int checkpoint_initialization_test(void) {
    for (unsigned mode = 0; mode < 2; mode++) {
        mt_ctx maps = {0}; atomic_init(&maps.owners, 0);
        piece_map_hooks h = { &maps, map_acquire, map_release };
        piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
        piece_checkpoint *cp = NULL; REQUIRE(!piece_checkpoint_begin(t, &cp));
        const uint8_t *old = (const uint8_t *)"old\ntext";
        REQUIRE(!(mode ? piece_init_mapped(t, old, 8, &h) : piece_init_copy(t, old, 8)));
        piece_snapshot *inside = piece_snapshot_take(t); REQUIRE(inside);
        piece_checkpoint_abort(cp); REQUIRE(piece_len(t) == 0);
        REQUIRE(atomic_load(&maps.owners) == (mode ? 1u : 0u));
        REQUIRE(!piece_init_copy(t, (const uint8_t *)"new text", 8));
        uint8_t b[8]; REQUIRE(!piece_snapshot_read(inside, 0, b, sizeof b) && !memcmp(b, old, sizeof b));
        REQUIRE(piece_snapshot_line_count(inside) == 2);
        piece_destroy(t);
        REQUIRE(!piece_snapshot_read(inside, 0, b, sizeof b) && !memcmp(b, old, sizeof b));
        piece_snapshot_release(inside); REQUIRE(!atomic_load(&maps.owners));
    }
    puts("checkpoint initialization: ok (abort restores eligibility; inside original/mapping stays valid)"); return 0;
}
/* Workers read transaction tails while abort restores the owning tree and
 * subsequent edits reuse the same logical offsets. The mapping owners remain
 * balanced across both kinds of checkpoint outcome and tree destruction. */
static int checkpoint_mt_test(void) {
    mt_ctx c = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };
    atomic_init(&c.bad, 0); atomic_init(&c.checked, 0); atomic_init(&c.owners, 0);
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    uint8_t original[1024]; memset(original, 'o', sizeof original); original[17] = '\n';
    piece_map_hooks h = { &c, map_acquire, map_release };
    REQUIRE(!piece_init_mapped(t, original, sizeof original, &h));
    pthread_t th[4];
    for (unsigned i = 0; i < 4; i++) REQUIRE(!pthread_create(&th[i], NULL, mt_reader, &c));
    uint8_t *payload = malloc(131091); REQUIRE(payload);
    /* An append run straddles the saved mark in a partially used add chunk. */
    REQUIRE(!piece_insert(t, sizeof original, (const uint8_t *)"tail", 4));
    for (unsigned i = 0; i < 192; i++) {
        REQUIRE(!piece_insert(t, piece_len(t), (const uint8_t *)"+", 1));
        uint64_t before_len = piece_len(t), before_pieces = piece_piece_count(t);
        piece_checkpoint *cp = NULL; REQUIRE(!piece_checkpoint_begin(t, &cp));
        memset(payload, (int)('a' + i % 26), 131091); payload[65537] = '\n';
        REQUIRE(!piece_insert(t, before_len, payload, 131091));
        mt_job *j = malloc(sizeof *j); REQUIRE(j);
        size_t n = (size_t)piece_len(t); uint8_t *copy = malloc(n); REQUIRE(copy);
        REQUIRE(!piece_read(t, 0, copy, n)); pm_init(&j->model, copy, n); free(copy);
        j->s = piece_snapshot_take(t); REQUIRE(j->s);
        pthread_mutex_lock(&c.mu); j->next = c.head; c.head = j; pthread_cond_signal(&c.cv); pthread_mutex_unlock(&c.mu);
        if (i % 3) {
            piece_checkpoint_abort(cp);
            REQUIRE(piece_len(t) == before_len && piece_piece_count(t) == before_pieces);
            REQUIRE(!piece_insert(t, before_len, (const uint8_t *)"!", 1));
            REQUIRE(piece_piece_count(t) == before_pieces);
        } else {
            piece_checkpoint_commit(cp);
            REQUIRE(!piece_delete(t, before_len, 131091, NULL));
        }
    }
    piece_destroy(t); free(payload);
    pthread_mutex_lock(&c.mu); c.done = 1; pthread_cond_broadcast(&c.cv); pthread_mutex_unlock(&c.mu);
    for (unsigned i = 0; i < 4; i++) REQUIRE(!pthread_join(th[i], NULL));
    REQUIRE(!atomic_load(&c.bad) && atomic_load(&c.checked) == 192 && !atomic_load(&c.owners));
    pthread_cond_destroy(&c.cv); pthread_mutex_destroy(&c.mu);
    puts("checkpoint snapshot threads: ok (abort/reuse/commit/destroy; mapping owners=0)");
    return 0;
}
/* Frozen lifetime edge cases, absent from the old bptree variant's tests. */
static int init_lifetime_test(void) {
    mt_ctx c = {0}; atomic_init(&c.owners, 0);
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
    piece_map_hooks h = { &c, map_acquire, map_release };
    REQUIRE(!piece_init_mapped(t, NULL, 0, &h));
    int bad = atomic_load(&c.owners) != 1;
    piece_snapshot *s = piece_snapshot_take(t); REQUIRE(s);
    if (atomic_load(&c.owners) != 2) bad = 1;
    piece_destroy(t); piece_snapshot_release(s); REQUIRE(atomic_load(&c.owners) == 0);
    t = piece_create(&a); REQUIRE(t);
    REQUIRE(!piece_insert(t, 0, (const uint8_t *)"x", 1)); REQUIRE(!piece_delete(t, 0, 1, NULL));
    if (piece_init_copy(t, (const uint8_t *)"q", 1) != PIECE_ERR_RANGE) bad = 1;
    piece_destroy(t);
    puts(bad ? "FAIL lifetime: empty mapping owners / init after edit" : "lifetime: ok");
    return bad;
}
typedef struct fail_allocator { size_t live, remaining; int armed; } fail_allocator;
static void *fail_alloc(void *p, size_t n) {
    fail_allocator *f = p;
    if (f->armed) { if (!f->remaining) return NULL; f->remaining--; }
    void *q = malloc(n); if (q) f->live += n; return q;
}
static void fail_free(void *p, void *q, size_t n) { fail_allocator *f = p; f->live -= n; free(q); }
static int failure_test(void) {
    unsigned failed = 0;
    uint8_t *orig = malloc(300000), *large = malloc(2u << 20); REQUIRE(orig && large);
    memset(orig, 'o', 300000); memset(large, '\n', 2u << 20);
    for (unsigned mode = 0; mode < 3; mode++) {
        int finished = 0;
        for (size_t point = 0; point < 1024 && !finished; point++) {
            fail_allocator f = {0}; piece_allocator a = { &f, fail_alloc, fail_free };
            piece_tree *t = piece_create(&a); REQUIRE(t); REQUIRE(!piece_init_copy(t, orig, 300000));
            piece_ref ref = {0};
            if (mode == 2) { REQUIRE(!piece_insert(t, 50000, large, 2u << 20)); REQUIRE(!piece_delete(t, 50000, 2u << 20, &ref));
                for (unsigned q = 1; q < PIECE_REF_SPANS; q++) ref.span[q] = ref.span[0];
                ref.nspans = PIECE_REF_SPANS; ref.len *= PIECE_REF_SPANS;
            }
            piece_snapshot *snap = piece_snapshot_take(t); REQUIRE(snap);
            f.armed = 1; f.remaining = point;
            int rc = mode == 0 ? piece_insert(t, 50000, large, 2u << 20) :
                     mode == 1 ? piece_delete(t, 100, 299800, &ref) : piece_insert_ref(t, 50000, &ref);
            f.armed = 0;
            if (rc == PIECE_ERR_NOMEM) {
                uint8_t *b = malloc(300000); REQUIRE(b); REQUIRE(piece_len(t) == 300000);
                REQUIRE(!piece_read(t, 0, b, 300000) && !memcmp(b, orig, 300000)); free(b); failed++;
            } else { REQUIRE(rc == 0); finished = 1; }
            REQUIRE(piece_snapshot_len(snap) == 300000 && piece_snapshot_line_count(snap) == 1);
            piece_snapshot_release(snap); piece_destroy(t); REQUIRE(f.live == 0);
        }
        REQUIRE(finished);
    }
    free(orig); free(large); REQUIRE(failed > 0);
    printf("failure atomicity: ok (%u injected allocation failures)\n", failed); return 0;
}
static int small_memory_test(void) {
    fail_allocator f = {0}; piece_allocator a = { &f, fail_alloc, fail_free };
    piece_tree *t = piece_create(&a); REQUIRE(t);
    REQUIRE(!piece_init_copy(t, NULL, 0)); REQUIRE(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    double bound = 65536.0 + 4096.0 + 96.0 + 1.25;
    printf("small memory: live=%zu bound=%.0f\n", f.live, bound);
    int bad = (double)f.live > bound;
    piece_snapshot *snap = piece_snapshot_take(t); REQUIRE(snap);
    REQUIRE(!piece_insert(t, 1, (const uint8_t *)"x", 1));
    bound = 65536.0 + 4096.0 + 96.0 + 2.5 + 1920.0;
    if ((double)f.live > bound) bad = 1;
    piece_snapshot_release(snap); piece_destroy(t); REQUIRE(!f.live);
    t = piece_create(&a); REQUIRE(t); REQUIRE(!piece_init_copy(t, NULL, 0));
    for (unsigned i = 0; i < 10000; i++) REQUIRE(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    REQUIRE(!piece_delete(t, 0, 9900, NULL));
    uint64_t pc = piece_piece_count(t);
    bound = 65536.0 + 4096.0 + 96.0 * (double)pc + 1.25 * 19900.0;
    printf("trim memory: live=%zu bound=%.0f pieces=%llu\n", f.live, bound, (unsigned long long)pc);
    if ((double)f.live > bound) bad = 1;
    REQUIRE(!piece_delete(t, 0, 100, NULL));
    bound = 65536.0 + 4096.0 + 1.25 * 20000.0;
    printf("empty memory: live=%zu bound=%.0f\n", f.live, bound);
    if ((double)f.live > bound) bad = 1;
    piece_destroy(t); REQUIRE(!f.live);
    puts(bad ? "FAIL memory: tiny buffers / retired slabs must meet G10f" : "small/trim memory: ok");
    return bad;
}
static int repeat_snapshot_test(void) {
    fail_allocator f = {0}; piece_allocator a = { &f, fail_alloc, fail_free };
    piece_tree *t = piece_create(&a); REQUIRE(t); REQUIRE(!piece_init_copy(t, NULL, 0));
    piece_snapshot *s[100];
    for (unsigned i = 0; i < 100; i++) { s[i] = piece_snapshot_take(t); REQUIRE(s[i]); }
    REQUIRE(!piece_insert(t, 0, (const uint8_t *)"x", 1));
    double bound = 65536.0 + 4096.0 + 96.0 + 1.25 + 1920.0;
    printf("repeated snapshots: live=%zu bound=%.0f\n", f.live, bound);
    int bad = (double)f.live > bound;
    for (unsigned i = 0; i < 100; i++) { REQUIRE(piece_snapshot_len(s[i]) == 0); piece_snapshot_release(s[i]); }
    piece_destroy(t); REQUIRE(!f.live);
    mt_ctx c = {0}; atomic_init(&c.owners, 0); a = piece_default_allocator(); t = piece_create(&a); REQUIRE(t);
    piece_map_hooks h = { &c, map_acquire, map_release }; REQUIRE(!piece_init_mapped(t, NULL, 0, &h));
    for (unsigned i = 0; i < 100; i++) { s[i] = piece_snapshot_take(t); REQUIRE(s[i]); }
    if (atomic_load(&c.owners) != 101) bad = 1;
    for (unsigned i = 0; i < 100; i++) piece_snapshot_release(s[i]);
    piece_destroy(t); REQUIRE(atomic_load(&c.owners) == 0);
    puts(bad ? "FAIL repeated snapshots: bounded headers / per-owner mapping hooks" : "repeated snapshots: ok"); return bad;
}
static void *arena_hook_alloc(void *p, size_t n) { return edit_arena_alloc(p, n, 16); }
static void arena_hook_free(void *p, void *q, size_t n) { (void)p; (void)q; (void)n; }
static int typing_allocator_test(void) {
    edit_arena arena; REQUIRE(!edit_arena_init(&arena, 64u << 20));
    piece_allocator a = { &arena, arena_hook_alloc, arena_hook_free }; piece_tree *t = piece_create(&a); REQUIRE(t);
    uint8_t orig[4096]; memset(orig, 'a', sizeof orig); REQUIRE(!piece_init_copy(t, orig, sizeof orig));
    REQUIRE(!piece_insert(t, 2048, (const uint8_t *)"x", 1)); /* warm SIMD/add storage */
    uint64_t cursor = 2049; int rc = 0; edit_malloc_guard_begin();
    for (unsigned i = 0; i < 10000; i++) {
        if (i % 19 == 0) { rc |= piece_delete(t, cursor - 1, 1, NULL); cursor--; }
        else { rc |= piece_insert(t, cursor, (const uint8_t *)"x", 1); cursor++; }
        if (i % 97 == 0) { piece_snapshot *s = piece_snapshot_take(t); if (!s) rc = 1; else piece_snapshot_release(s); }
    }
    size_t count = edit_malloc_guard_end(); REQUIRE(!rc && count == 0);
    printf("typing allocator: malloc_calls=%zu guard=%s\n", count, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
    piece_destroy(t); edit_arena_free(&arena); return 0;
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
#if defined(PIECE_TESTING) && defined(PIECE_GAP_TRIAL)
    /* Reproduce the rejected prototype's behavior independently of the
     * production-only lifetime and pool regressions added later. */
    if (argc == 2 && !strcmp(argv[1], "--gap-trial")) return gap_test();
#endif
    int bad = 0;
#ifdef PIECE_TEST_WRAP_THREADS
    bad |= kernel_threads_test();
#endif
#ifdef PIECE_TESTING
    bad |= owner_race_test(); bad |= cursor_test(); bad |= compact_test();
#ifdef PIECE_GAP_TRIAL
    bad |= gap_test();
#endif
#endif
    bad |= init_lifetime_test(); bad |= failure_test(); bad |= small_memory_test(); bad |= repeat_snapshot_test(); bad |= typing_allocator_test();
    bad |= mt_test(); bad |= checkpoint_mt_test(); bad |= checkpoint_unchanged_snapshot_test(); bad |= checkpoint_initialization_test();
    return bad;
}
