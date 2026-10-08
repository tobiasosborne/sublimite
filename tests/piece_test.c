/* Piece tree conformance suite. Build against any implementation of piece.h. */
#include "piece/piece.h"
#include "piece_model.h"
#include <pthread.h>
#include <stdio.h>

static int g_fail;
static const char *g_name = "";
#define CHECK(c) do { if (!(c)) { printf("FAIL [%s] %s:%d: %s\n", g_name, __FILE__, __LINE__, #c); g_fail = 1; return; } } while (0)

typedef struct { piece_allocator a; size_t live, peak, requested; long calls; } ca;
static void *ca_alloc(void *x, size_t n) { ca *c = x; void *p = malloc(n); if (!p) return NULL; c->live += n; c->requested += n; c->calls++; if (c->live > c->peak) c->peak = c->live; return p; }
static void ca_free(void *x, void *p, size_t n) { ca *c = x; c->live -= n; free(p); }
static piece_allocator ca_make(ca *c) { memset(c, 0, sizeof *c); piece_allocator a = { c, ca_alloc, ca_free }; return a; }

static uint64_t rng_s;
static uint64_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return rng_s; }

/* full comparison of tree against model */
static int same(const piece_tree *t, const pm_model *m) {
    if (piece_len(t) != m->n) return 0;
    if (piece_line_count(t) != pm_line_count(m)) return 0;
    uint8_t *b = malloc(m->n + 1);
    int ok = piece_read(t, 0, b, m->n) == 0 && (m->n == 0 || !memcmp(b, m->d, m->n));
    free(b);
    if (!ok) return 0;
    piece_iter it; const uint8_t *p; size_t n; size_t pos = 0;
    piece_iter_begin(&it, t, 0);
    while (piece_iter_next(&it, &p, &n)) {
        if (n == 0 || pos + n > m->n || memcmp(p, m->d + pos, n)) return 0;
        pos += n;
    }
    return pos == m->n;
}
static int lines_ok(const piece_tree *t, const pm_model *m, uint64_t samples) {
    uint64_t lc = pm_line_count(m);
    for (uint64_t i = 0; i < samples; i++) {
        uint64_t l = rnd() % (lc + 2), o = m->n ? rnd() % (m->n + 2) : rnd() % 3;
        if (piece_line_to_byte(t, l) != pm_line_to_byte(m, l)) return 0;
        if (piece_byte_to_line(t, o) != pm_byte_to_line(m, o)) return 0;
    }
    return 1;
}

static void t_empty(void) {
    g_name = "empty";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); CHECK(t);
    CHECK(piece_len(t) == 0 && piece_line_count(t) == 1);
    CHECK(piece_line_to_byte(t, 0) == 0 && piece_line_to_byte(t, 5) == 0 && piece_byte_to_line(t, 9) == 0);
    piece_iter it; const uint8_t *p; size_t n; piece_iter_begin(&it, t, 0);
    CHECK(!piece_iter_next(&it, &p, &n));
    uint8_t c; CHECK(piece_read(t, 0, &c, 1) != 0);
    CHECK(piece_read(t, 0, &c, 0) == 0);
    CHECK(piece_delete(t, 0, 1, NULL) != 0 && piece_insert(t, 1, (const uint8_t *)"x", 1) != 0);
    CHECK(piece_insert(t, 0, (const uint8_t *)"", 0) == 0 && piece_len(t) == 0);
    piece_destroy(t);
}

static void t_basic(void) {
    g_name = "insert/delete basic";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); pm_model m;
    const uint8_t *o = (const uint8_t *)"hello world\nsecond\r\nthird";
    CHECK(piece_init_copy(t, o, 25) == 0); pm_init(&m, o, 25);
    CHECK(same(t, &m) && lines_ok(t, &m, 200));
    CHECK(piece_line_count(t) == 3);
    CHECK(piece_line_to_byte(t, 1) == 12 && piece_line_to_byte(t, 2) == 20 && piece_line_to_byte(t, 3) == 25);
    CHECK(piece_byte_to_line(t, 11) == 0 && piece_byte_to_line(t, 12) == 1 && piece_byte_to_line(t, 20) == 2);
    const char *ins[3] = { "AAA", "BBB\n", "CCC" }; size_t at[3] = { 0, 10, 0 };
    for (int i = 0; i < 3; i++) {
        at[2] = m.n;
        size_t l = strlen(ins[i]);
        CHECK(piece_insert(t, at[i], (const uint8_t *)ins[i], l) == 0);
        pm_insert(&m, at[i], (const uint8_t *)ins[i], l);
        CHECK(same(t, &m) && lines_ok(t, &m, 200));
    }
    /* delete across piece boundaries */
    CHECK(piece_delete(t, 2, m.n - 4, NULL) == 0); pm_delete(&m, 2, m.n - 4);
    CHECK(same(t, &m) && lines_ok(t, &m, 100));
    CHECK(piece_delete(t, 0, m.n, NULL) == 0); pm_delete(&m, 0, m.n);
    CHECK(same(t, &m) && piece_len(t) == 0);
    CHECK(piece_delete(t, 0, 1, NULL) != 0);
    pm_free(&m); piece_destroy(t);
}

static void t_crlf(void) {
    g_name = "crlf";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); pm_model m;
    const uint8_t *o = (const uint8_t *)"a\r\nb\r\nc\n\r\n";
    CHECK(piece_init_copy(t, o, 10) == 0); pm_init(&m, o, 10);
    CHECK(piece_line_count(t) == 5);
    CHECK(piece_line_to_byte(t, 1) == 3 && piece_line_to_byte(t, 2) == 6);
    /* split a CRLF pair with an insert, then delete the LF only */
    CHECK(piece_insert(t, 2, (const uint8_t *)"X", 1) == 0); pm_insert(&m, 2, (const uint8_t *)"X", 1);
    CHECK(same(t, &m) && piece_line_count(t) == 5);
    CHECK(piece_delete(t, 3, 1, NULL) == 0); pm_delete(&m, 3, 1);
    CHECK(same(t, &m) && piece_line_count(t) == 4 && lines_ok(t, &m, 100));
    pm_free(&m); piece_destroy(t);
}

static void t_undo(void) {
    g_name = "delete-then-reinsert";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); pm_model m;
    uint8_t *o = malloc(100000); for (int i = 0; i < 100000; i++) o[i] = (uint8_t)(i % 7 == 0 ? '\n' : 'a' + i % 26);
    CHECK(piece_init_copy(t, o, 100000) == 0); pm_init(&m, o, 100000);
    for (int k = 0; k < 200; k++) {
        uint64_t off = rnd() % (m.n + 1), len = rnd() % 3000; if (len > m.n - off) len = m.n - off;
        uint8_t *save = malloc(len + 1);
        CHECK(piece_read(t, off, save, len) == 0);
        CHECK(piece_delete(t, off, len, NULL) == 0); pm_delete(&m, off, len);
        CHECK(same(t, &m));
        CHECK(piece_insert(t, off, save, len) == 0); pm_insert(&m, off, save, len);
        free(save);
        CHECK(same(t, &m));
    }
    CHECK(memcmp(m.d, o, 100000) == 0);
    free(o); pm_free(&m); piece_destroy(t);
}

typedef struct { int rc; int freed; } mapctx;
static void m_acq(void *x) { __atomic_add_fetch(&((mapctx *)x)->rc, 1, __ATOMIC_SEQ_CST); }
static void m_rel(void *x) { __atomic_sub_fetch(&((mapctx *)x)->rc, 1, __ATOMIC_SEQ_CST); }
static void t_mapped(void) {
    g_name = "mapped";
    piece_allocator a = piece_default_allocator();
    uint8_t *map = malloc(5000); for (int i = 0; i < 5000; i++) map[i] = (uint8_t)(i % 11 == 0 ? '\n' : 'q');
    pm_model m; pm_init(&m, map, 5000);
    mapctx mc = { 0, 0 }; piece_map_hooks h = { &mc, m_acq, m_rel };
    piece_tree *t = piece_create(&a);
    CHECK(piece_init_mapped(t, map, 5000, &h) == 0 && mc.rc >= 1);
    CHECK(same(t, &m));
    uint8_t save[1000]; CHECK(piece_read(t, 100, save, 1000) == 0);
    CHECK(piece_delete(t, 100, 1000, NULL) == 0); pm_delete(&m, 100, 1000);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    memset(map, 'Z', 5000); /* mapping changes behind the deleted range: only deleted bytes are protected */
    uint8_t *tail = malloc(m.n);
    memset(m.d, 'Z', m.n); /* remaining live bytes now come from changed mapping; skip content check */
    free(tail);
    CHECK(piece_insert(t, 100, save, 1000) == 0);
    uint8_t back[1000]; CHECK(piece_read(t, 100, back, 1000) == 0 && !memcmp(back, save, 1000));
    piece_destroy(t);
    CHECK(mc.rc >= 1); /* snapshot still holds the mapping */
    piece_snapshot_release(s);
    CHECK(mc.rc == 0);
    free(map); pm_free(&m);
}

static void t_snapshot(void) {
    g_name = "snapshot isolation";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); pm_model m, frozen;
    uint8_t *o = malloc(50000); for (int i = 0; i < 50000; i++) o[i] = (uint8_t)(i % 13 == 0 ? '\n' : 'a' + i % 26);
    CHECK(piece_init_copy(t, o, 50000) == 0); pm_init(&m, o, 50000);
    piece_snapshot *s1 = piece_snapshot_take(t); CHECK(s1);
    pm_init(&frozen, m.d, m.n);
    for (int i = 0; i < 2000; i++) {
        uint8_t b[300]; size_t l = rnd() % 300; for (size_t j = 0; j < l; j++) b[j] = (uint8_t)(rnd() % 5 ? 'x' : '\n');
        uint64_t off = rnd() % (m.n + 1);
        if (rnd() & 1) { CHECK(piece_insert(t, off, b, l) == 0); pm_insert(&m, off, b, l); }
        else { uint64_t d = rnd() % 400; if (d > m.n - off) d = m.n - off; CHECK(piece_delete(t, off, d, NULL) == 0); pm_delete(&m, off, d); }
    }
    CHECK(same(t, &m));
    CHECK(piece_snapshot_len(s1) == frozen.n && piece_snapshot_line_count(s1) == pm_line_count(&frozen));
    uint8_t *b = malloc(frozen.n); CHECK(piece_snapshot_read(s1, 0, b, frozen.n) == 0 && !memcmp(b, frozen.d, frozen.n)); free(b);
    for (int i = 0; i < 100; i++) {
        uint64_t l = rnd() % (pm_line_count(&frozen) + 1), off = rnd() % (frozen.n + 1);
        CHECK(piece_snapshot_line_to_byte(s1, l) == pm_line_to_byte(&frozen, l));
        CHECK(piece_snapshot_byte_to_line(s1, off) == pm_byte_to_line(&frozen, off));
    }
    piece_iter it; const uint8_t *p; size_t n, pos = 0; piece_iter_begin_snapshot(&it, s1, 0);
    while (piece_iter_next(&it, &p, &n)) { CHECK(memcmp(p, frozen.d + pos, n) == 0); pos += n; }
    CHECK(pos == frozen.n);
    piece_snapshot *s2 = piece_snapshot_retain(s1); piece_snapshot_release(s1);
    piece_destroy(t); /* snapshot outlives tree */
    CHECK(piece_snapshot_len(s2) == frozen.n);
    piece_snapshot_release(s2);
    free(o); pm_free(&m); pm_free(&frozen);
}

typedef struct { piece_snapshot *s; pm_model *m; int ok; } rdarg;
static void *reader(void *x) {
    rdarg *r = x; uint8_t *b = malloc(r->m->n + 1);
    for (int k = 0; k < 20; k++) {
        if (piece_snapshot_read(r->s, 0, b, r->m->n) || memcmp(b, r->m->d, r->m->n)) r->ok = 0;
        if (piece_snapshot_line_count(r->s) != pm_line_count(r->m)) r->ok = 0;
    }
    free(b); return NULL;
}
static void t_threads(void) {
    g_name = "snapshot from threads";
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a); pm_model fz;
    uint8_t *o = malloc(20000); for (int i = 0; i < 20000; i++) o[i] = (uint8_t)(i % 9 == 0 ? '\n' : 'a');
    CHECK(piece_init_copy(t, o, 20000) == 0); pm_init(&fz, o, 20000);
    piece_snapshot *s = piece_snapshot_take(t);
    pthread_t th[3]; rdarg ra[3];
    for (int i = 0; i < 3; i++) { ra[i] = (rdarg){ piece_snapshot_retain(s), &fz, 1 }; pthread_create(&th[i], NULL, reader, &ra[i]); }
    for (int i = 0; i < 500; i++) { piece_insert(t, rnd() % (piece_len(t) + 1), (const uint8_t *)"zz\n", 3); piece_delete(t, rnd() % (piece_len(t) - 4), 3, NULL); }
    for (int i = 0; i < 3; i++) { pthread_join(th[i], NULL); CHECK(ra[i].ok); piece_snapshot_release(ra[i].s); }
    piece_snapshot_release(s); piece_destroy(t); free(o); pm_free(&fz);
}

static int g11f_bound(const ca *c, const piece_tree *t, size_t orig, uint64_t typed, uint64_t deleted) {
    double bound = 1.25 * (double)orig + 65536.0 + 96.0 * (double)piece_piece_count(t) + 1.25 * (double)(typed + deleted) + 4096.0;
    if ((double)c->peak > bound) printf("  peak %zu > bound %.0f (pieces %llu typed %llu deleted %llu)\n", c->peak, bound, (unsigned long long)piece_piece_count(t), (unsigned long long)typed, (unsigned long long)deleted);
    return (double)c->peak <= bound;
}

static void t_random(void) {
    g_name = "random 1e5 ops + memory bound";
    rng_s = 0x9E3779B97F4A7C15ull;
    size_t N = 1 << 20;
    uint8_t *o = malloc(N); for (size_t i = 0; i < N; i++) o[i] = (uint8_t)((rnd() % 40 == 0) ? '\n' : (rnd() % 60 == 0 ? '\r' : 'a' + rnd() % 26));
    ca c; piece_allocator a = ca_make(&c);
    piece_tree *t = piece_create(&a); CHECK(t);
    pm_model m; CHECK(piece_init_copy(t, o, N) == 0); pm_init(&m, o, N);
    uint64_t typed = 0, deleted = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t r = rnd() % 10;
        if (r < 6 || m.n == 0) {
            uint8_t b[200]; size_t l = rnd() % 4 == 0 ? rnd() % 200 : 1 + rnd() % 3;
            for (size_t j = 0; j < l; j++) b[j] = (uint8_t)(rnd() % 20 == 0 ? '\n' : 'A' + rnd() % 26);
            uint64_t off = rnd() % 3 == 0 ? rnd() % (m.n + 1) : (m.n ? (uint64_t)(i * 7919) % (m.n + 1) : 0);
            CHECK(piece_insert(t, off, b, l) == 0); pm_insert(&m, off, b, l); typed += l;
        } else {
            uint64_t off = rnd() % m.n, l = rnd() % 8 == 0 ? rnd() % 500 : rnd() % 4;
            if (l > m.n - off) l = m.n - off;
            CHECK(piece_delete(t, off, l, NULL) == 0); pm_delete(&m, off, l); deleted += l;
        }
        CHECK(piece_len(t) == m.n);
        if (i % 1000 == 0) { CHECK(same(t, &m)); CHECK(lines_ok(t, &m, 3)); }
        else {
            uint64_t off = m.n ? rnd() % m.n : 0; size_t l = (size_t)(rnd() % 64); if (l > m.n - off) l = m.n - off;
            uint8_t b[64]; CHECK(piece_read(t, off, b, l) == 0 && !memcmp(b, m.d + off, l));
        }
    }
    CHECK(same(t, &m)); CHECK(lines_ok(t, &m, 60));
    CHECK(g11f_bound(&c, t, N, typed, deleted));
    printf("  random: pieces=%llu peak=%zu typed=%llu deleted=%llu\n", (unsigned long long)piece_piece_count(t), c.peak, (unsigned long long)typed, (unsigned long long)deleted);
    piece_destroy(t);
    CHECK(c.live == 0); /* no leaks through the hooks */
    free(o); pm_free(&m);
}

static void t_mapped_mem(void) {
    g_name = "mapped memory bound";
    size_t N = 1 << 22; uint8_t *o = calloc(N, 1);
    ca c; piece_allocator a = ca_make(&c);
    piece_tree *t = piece_create(&a); mapctx mc = {0,0}; piece_map_hooks h = { &mc, m_acq, m_rel };
    CHECK(piece_init_mapped(t, o, N, &h) == 0);
    CHECK(c.peak <= 32 * ((N + 65535) / 65536) + 2 * 1024 * 1024);
    CHECK(piece_len(t) == N);
    piece_destroy(t); CHECK(c.live == 0 && mc.rc == 0); free(o);
}


static void t_coalesce(void) {
    g_name = "coalesce";
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(piece_init_copy(t, (const uint8_t *)"0123456789", 10) == 0);
    for (int i = 0; i < 10000; i++) CHECK(piece_insert(t, 5 + (uint64_t)i, (const uint8_t *)"x", 1) == 0);
    CHECK(piece_len(t) == 10010 && piece_piece_count(t) <= 3);
    uint64_t pc0 = piece_piece_count(t);
    CHECK(piece_insert(t, 0, (const uint8_t *)"y", 1) == 0);      /* elsewhere breaks run */
    CHECK(piece_insert(t, 10006, (const uint8_t *)"z", 1) == 0); /* old cursor: not adjacent in add buffer */
    CHECK(piece_piece_count(t) >= pc0 + 2);
    piece_destroy(t);
    t = piece_create(&a);
    for (int i = 0; i < 10000; i++) CHECK(piece_insert(t, (uint64_t)i, (const uint8_t *)"q", 1) == 0);
    CHECK(piece_piece_count(t) <= 2 && piece_len(t) == 10000);
    piece_destroy(t);
}

static void t_ref(void) {
    g_name = "ref";
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    enum { N = 3000 };
    uint8_t o[N]; for (int i = 0; i < N; i++) o[i] = (uint8_t)('a' + i % 26 + (i % 7 == 0 ? -32 : 0));
    CHECK(piece_init_copy(t, o, N) == 0);
    pm_model m; pm_init(&m, o, N);
    for (int i = 0; i < 40; i++) {                     /* build mixed content */
        uint64_t off = rnd() % (m.n + 1); uint8_t b[9] = "ab\ncd\nef\n"; size_t l = 1 + rnd() % 8;
        CHECK(piece_insert(t, off, b, l) == 0); pm_insert(&m, off, b, l);
    }
    for (int it = 0; it < 300; it++) {
        uint64_t off = rnd() % (m.n + 1), l = rnd() % (it % 3 == 0 ? m.n - off + 1 : 200);
        if (l > m.n - off) l = m.n - off;
        uint8_t *save = malloc(l + 1); CHECK(piece_read(t, off, save, (size_t)l) == 0);
        piece_ref r; uint64_t pcnt0 = piece_piece_count(t); (void)pcnt0;
        CHECK(piece_delete(t, off, l, &r) == 0);
        CHECK(r.len == l && r.nspans <= PIECE_REF_SPANS && (l == 0 || r.nspans >= 1));
        pm_delete(&m, off, l); CHECK(same(t, &m));
        uint64_t off2 = rnd() % (m.n + 1);
        CHECK(piece_insert_ref(t, off2, &r) == 0); pm_insert(&m, off2, save, (size_t)l);
        CHECK(same(t, &m)); CHECK(lines_ok(t, &m, 20));
        free(save);
    }
    CHECK(piece_insert_ref(t, m.n + 1, &(piece_ref){0}) != 0);
    piece_ref bad = { 1, 1, {{ 1ull << 40, 1 }} };
    CHECK(piece_insert_ref(t, 0, &bad) != 0);
    pm_free(&m); piece_destroy(t);
}

int main(void) {
    rng_s = 12345;
    t_empty(); t_basic(); t_crlf(); t_undo(); t_mapped(); t_snapshot(); t_threads(); t_random(); t_mapped_mem(); t_coalesce(); t_ref();
    printf(g_fail ? "piece_test: FAILED\n" : "piece_test: all passed\n");
    return g_fail;
}
