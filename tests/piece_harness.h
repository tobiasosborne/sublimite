/* piece differential harness, shared by tests/piece_test.c and
 * fuzz/piece_fuzz.c (one TU each).
 *
 * The implementation under test (IUT, piece_*) and the flat reference
 * (pm_*, tests/piece_model.h) run the same operations side by side and are
 * compared through observables only: length, bytes, description, carried
 * counts (soundness), ADD length, snapshot identity and results.
 *
 * Base refusal: the binary links with -Wl,--wrap=os_vm_reserve, the one
 * reservation path under base (Makefile, LDFLAGS_piece_test). Refusal is
 * armed only around an IUT call, so the reference is never refused.
 *
 * Every use of piece_span and piece_span_nl goes through ph_spans, so a
 * change to the carried-count record stays local to it. */
#ifndef TESTS_PIECE_HARNESS_H
#define TESTS_PIECE_HARNESS_H

#include "piece_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- failures ----------------------------------------------------------- */

static int ph_failures;
#define PH_CHECK(c, ...) do { if (!(c)) { ph_failures++; \
    fprintf(stderr, "%s:%d: FAIL %s: ", __FILE__, __LINE__, #c); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define PH_U(x) ((unsigned long long)(x))

/* ---- base refusal --------------------------------------------------------- */

os_err __real_os_vm_reserve(void **out, size_t len);
os_err __wrap_os_vm_reserve(void **out, size_t len);

static atomic_bool ph_refusing;
static atomic_size_t ph_reserve_calls; /* every call, granted or refused */

os_err __wrap_os_vm_reserve(void **out, size_t len)
{
    atomic_fetch_add(&ph_reserve_calls, 1);
    if (atomic_load(&ph_refusing)) return OS_E_NOMEM;
    return __real_os_vm_reserve(out, len);
}

static inline void ph_refuse(bool on) { atomic_store(&ph_refusing, on); }
static inline size_t ph_calls(void) { return atomic_load(&ph_reserve_calls); }

/* ---- a mapped ORIGINAL over a memfd (no filesystem trace) ---------------- */

typedef struct ph_mapping {
    os_file f;
    os_map map;
    os_fault_id id;
    atomic_int owners;   /* the test's own reference plus piece's */
    atomic_int acquires; /* piece's calls only */
    atomic_int releases;
    piece_backing b;
} ph_mapping;

static inline void ph_map_unref(ph_mapping *mp)
{
    if (atomic_fetch_sub(&mp->owners, 1) != 1) return;
    os_fault_retire(mp->id);
    os_unmap(&mp->map);
    close(mp->f.fd);
}

static void ph_map_acquire(void *ctx)
{
    ph_mapping *mp = ctx;
    atomic_fetch_add(&mp->acquires, 1);
    atomic_fetch_add(&mp->owners, 1);
}

static void ph_map_release(void *ctx)
{
    ph_mapping *mp = ctx;
    atomic_fetch_add(&mp->releases, 1);
    ph_map_unref(mp);
}

/* n > 0. The test holds one owner until ph_map_unref. */
static inline bool ph_map_open(ph_mapping *mp, const uint8_t *bytes, size_t n)
{
    memset(mp, 0, sizeof *mp);
    int fd = memfd_create("piece_test", MFD_CLOEXEC);
    if (fd < 0) return false;
    for (size_t done = 0; done < n;) {
        ssize_t k = write(fd, bytes + done, n - done);
        if (k <= 0) { close(fd); return false; }
        done += (size_t)k;
    }
    mp->f.fd = fd;
    if (os_map_file(&mp->map, &mp->f, 0, n) != OS_OK) { close(fd); return false; }
    if (os_fault_register(&mp->map, &mp->id) != OS_OK) { os_unmap(&mp->map); close(fd); return false; }
    atomic_init(&mp->owners, 1);
    atomic_init(&mp->acquires, 0);
    atomic_init(&mp->releases, 0);
    mp->b = (piece_backing){mp->map.addr, n, mp->id, mp, ph_map_acquire, ph_map_release};
    return true;
}

/* ---- span lists: the one place piece_span and piece_span_nl are used ----- */

typedef struct ph_spans {
    piece_span *s;
    piece_span_nl *nl;
    size_t n, cap;
    uint64_t len;
} ph_spans;

static inline void ph_spans_clear(ph_spans *v) { v->n = 0; v->len = 0; }

static inline void ph_spans_free(ph_spans *v)
{
    free(v->s);
    free(v->nl);
    *v = (ph_spans){0};
}

static inline void ph_spans_push(ph_spans *v, piece_span s, piece_span_nl nl)
{
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 16;
        v->s = realloc(v->s, v->cap * sizeof *v->s);
        v->nl = realloc(v->nl, v->cap * sizeof *v->nl);
        ASSERT(v->s != NULL && v->nl != NULL);
    }
    v->s[v->n] = s;
    v->nl[v->n] = nl;
    v->n++;
    v->len += s.len;
}

/* Describes [off, off + len) of an IUT view; with_nl asks for records. */
static inline void ph_describe(const piece_view *v, uint64_t off, uint64_t len, bool with_nl, ph_spans *out)
{
    ph_spans_clear(out);
    piece_iter it;
    piece_iter_init(&it, v, off);
    piece_span s;
    piece_span_nl nl = {PIECE_NL_UNKNOWN, PIECE_NL_UNKNOWN};
    while (piece_iter_next_span(&it, off + len, &s, with_nl ? &nl : NULL)) ph_spans_push(out, s, nl);
}

static inline void ph_describe_model(const pm_view *v, uint64_t off, uint64_t len, ph_spans *out)
{
    ph_spans_clear(out);
    pm_iter it;
    pm_iter_init(&it, v, off);
    piece_span s;
    piece_span_nl nl;
    while (pm_iter_next_span(&it, off + len, &s, &nl)) ph_spans_push(out, s, nl);
}

/* Spans of [off, off + len) without records: reads no byte. */
static inline uint64_t ph_count_spans(const piece_view *v, uint64_t off, uint64_t len)
{
    piece_iter it;
    piece_iter_init(&it, v, off);
    piece_span s;
    uint64_t n = 0;
    while (piece_iter_next_span(&it, off + len, &s, NULL)) n++;
    return n;
}

static inline bool ph_span_eq(piece_span a, piece_span b) { return a.src == b.src && a.len == b.len; }

/* Same spans; every known IUT count equals the reference's true count. */
static inline void ph_spans_check(const ph_spans *got, const ph_spans *want, const char *tag)
{
    PH_CHECK(got->n == want->n, "%s: %llu spans, want %llu", tag, PH_U(got->n), PH_U(want->n));
    for (size_t i = 0; i < got->n && i < want->n; i++) {
        PH_CHECK(ph_span_eq(got->s[i], want->s[i]), "%s: span %llu is {%llx, %llu}, want {%llx, %llu}", tag,
                 PH_U(i), PH_U(got->s[i].src), PH_U(got->s[i].len), PH_U(want->s[i].src), PH_U(want->s[i].len));
        if (i + 1 < got->n)
            PH_CHECK(got->s[i].src + got->s[i].len != got->s[i + 1].src, "%s: spans %llu and %llu mergeable",
                     tag, PH_U(i), PH_U(i + 1));
        piece_span_nl g = got->nl[i], w = want->nl[i];
        if (g.head != PIECE_NL_UNKNOWN && w.head != PIECE_NL_UNKNOWN)
            PH_CHECK(g.head == w.head, "%s: span %llu head count %u, true %u", tag, PH_U(i), g.head, w.head);
        if (g.tail != PIECE_NL_UNKNOWN && w.tail != PIECE_NL_UNKNOWN)
            PH_CHECK(g.tail == w.tail, "%s: span %llu tail count %u, true %u", tag, PH_U(i), g.tail, w.tail);
    }
}

/* ---- comparing views ---------------------------------------------------- */

static inline uint8_t *ph_bytes_model(const pm_view *m, uint64_t *n)
{
    *n = pm_length(m);
    uint8_t *b = malloc(*n ? (size_t)*n : 1u);
    ASSERT(b != NULL);
    pm_iter it;
    pm_iter_init(&it, m, 0);
    const uint8_t *p;
    size_t k;
    uint64_t at = 0;
    while ((k = pm_iter_next(&it, &p)) != 0) {
        memcpy(b + at, p, k);
        at += k;
    }
    ASSERT(at == *n);
    return b;
}

static inline uint64_t ph_rand(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

/* Compares an IUT view with its reference view. probes: random offsets;
 * records: also check the carried counts of the whole description. */
static inline void ph_check_view(const piece_view *v, const pm_view *m, uint64_t seed, unsigned probes,
                                 bool records, const char *tag)
{
    uint64_t n = piece_length(v), mn;
    PH_CHECK(n == pm_length(m), "%s: length %llu, want %llu", tag, PH_U(n), PH_U(pm_length(m)));
    if (n != pm_length(m)) return;
    uint8_t *want = ph_bytes_model(m, &mn);
    ph_spans got = {0}, ref = {0};
    ph_describe(v, 0, n, records, &got);
    ph_describe_model(m, 0, n, &ref);
    if (!records)
        for (size_t i = 0; i < ref.n; i++) ref.nl[i] = (piece_span_nl){PIECE_NL_UNKNOWN, PIECE_NL_UNKNOWN};
    ph_spans_check(&got, &ref, tag);

    /* Bytes, run by run; each run lies inside one span of the description. */
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *p;
    size_t k;
    uint64_t at = 0, span_end = 0;
    size_t si = 0;
    bool ok = true;
    while ((k = piece_iter_next(&it, &p)) != 0) {
        if (at + k > n) { ok = false; break; }
        while (si < ref.n && span_end + ref.s[si].len <= at) span_end += ref.s[si++].len;
        PH_CHECK(si < ref.n && at + k <= span_end + ref.s[si].len, "%s: run [%llu, %llu) crosses a span", tag,
                 PH_U(at), PH_U(at + k));
        if (memcmp(p, want + at, k) != 0) ok = false;
        at += k;
    }
    PH_CHECK(ok && at == n, "%s: bytes differ (read %llu of %llu)", tag, PH_U(at), PH_U(n));

    for (unsigned i = 0; i < probes && n; i++) {
        uint64_t off = ph_rand(&seed) % (n + 1u);
        if (off < n) PH_CHECK(piece_byte_at(v, off) == want[off], "%s: byte_at(%llu)", tag, PH_U(off));
        uint64_t len = ph_rand(&seed) % (n - off + 1u);
        if (len > 4u * PIECE_CHUNK) len = 4u * PIECE_CHUNK; /* bounds the reference's byte-wise counting */
        ph_describe(v, off, len, true, &got);
        ph_describe_model(m, off, len, &ref);
        ph_spans_check(&got, &ref, tag);
        piece_iter_seek(&it, off);
        uint64_t q = off;
        while ((k = piece_iter_next(&it, &p)) != 0 && q < n) {
            if (q + k > n || memcmp(p, want + q, k) != 0) { ok = false; break; }
            q += k;
        }
        PH_CHECK(ok && q == n, "%s: bytes from %llu differ", tag, PH_U(off));
    }

    const piece_backing *b = piece_view_backing(v), *mb = pm_view_backing(m);
    PH_CHECK((b == NULL) == (mb == NULL), "%s: backing presence", tag);
    if (b && mb)
        PH_CHECK(b->bytes == mb->bytes && b->len == mb->len && b->fault.slot == mb->fault.slot &&
                 b->fault.gen == mb->fault.gen && b->ctx == mb->ctx && b->acquire == mb->acquire &&
                 b->release == mb->release, "%s: backing fields", tag);
    ph_spans_free(&got);
    ph_spans_free(&ref);
    free(want);
}

/* ---- the differential state machine ------------------------------------- */

#define PH_SNAPS 6u
#define PH_LISTS 4u
/* (P) Large enough to cross an ADD chunk boundary in one call. */
#define PH_LARGE (PIECE_CHUNK + PIECE_CHUNK / 2u)

typedef enum ph_cp { PH_CP_NONE, PH_CP_OPEN } ph_cp;

typedef struct ph_state {
    piece_reserve *r;
    pm_reserve *mr;
    piece_tree *t;
    pm_tree *m;
    ph_mapping *map;
    piece_snapshot *s[PH_SNAPS];
    pm_snap *ms[PH_SNAPS];
    bool snap_inside[PH_SNAPS]; /* taken while the checkpoint is open */
    bool snap_foreign[PH_SNAPS]; /* taken inside an aborted checkpoint: no span source */
    ph_spans list[PH_LISTS];
    bool list_inside[PH_LISTS]; /* obtained while the checkpoint is open */
    bool list_foreign[PH_LISTS]; /* names ADD the tree no longer holds */
    int mapped_sides; /* trees opened over the mapped backing, IUT and reference */
    ph_cp cp;
    piece_snapshot *last; /* the previous take, and whether the text moved since */
    pm_snap *mlast;
    bool moved;
    bool height_limited; /* a test hook lowered the limit: RANGE is allowed */
    uint64_t seed;
    unsigned steps;
} ph_state;

/* Carried counts of the whole text are checked every PH_RECORD_EVERY steps:
 * the reference counts byte by byte. */
#define PH_RECORD_EVERY 16u

static inline void ph_check_tree(ph_state *st, unsigned probes, const char *tag)
{
    PH_CHECK(piece_add_length(st->t) == pm_add_length(st->m), "%s: ADD length %llu, want %llu", tag,
             PH_U(piece_add_length(st->t)), PH_U(pm_add_length(st->m)));
    bool records = st->steps++ % PH_RECORD_EVERY == 0;
    ph_check_view(piece_tree_view(st->t), pm_tree_view(st->m), ph_rand(&st->seed), probes, records, tag);
}

static inline void ph_check_snaps(ph_state *st, const char *tag)
{
    for (unsigned i = 0; i < PH_SNAPS; i++)
        if (st->s[i]) ph_check_view(piece_snapshot_view(st->s[i]), pm_snapshot_view(st->ms[i]),
                                    ph_rand(&st->seed), 2, true, tag);
}

/* Opens both sides: mode 0 copy, 1 mapped (n > 0). False if the fixture failed. */
static inline bool ph_open(ph_state *st, const uint8_t *bytes, size_t n, unsigned mode, ph_mapping *map)
{
    memset(st, 0, sizeof *st);
    st->seed = 0x9e3779b97f4a7c15u ^ n;
    if (piece_reserve_create(&st->r) != PIECE_OK) { PH_CHECK(0, "piece_reserve_create refused"); return false; }
    ASSERT(pm_reserve_create(&st->mr) == PIECE_OK);
    piece_err e;
    if (mode == 1 && n > 0) {
        if (!ph_map_open(map, bytes, n)) { PH_CHECK(0, "mapped fixture"); return false; }
        st->map = map;
        e = piece_open_mapped(st->r, &map->b, &st->t);
        ASSERT(pm_open_mapped(st->mr, &map->b, &st->m) == PIECE_OK);
        st->mapped_sides = e == PIECE_OK ? 2 : 1;
    } else {
        e = piece_open_copy(st->r, bytes, n, &st->t);
        ASSERT(pm_open_copy(st->mr, bytes, n, &st->m) == PIECE_OK);
    }
    PH_CHECK(e == PIECE_OK, "open refused (%d)", (int)e);
    if (e != PIECE_OK) {
        st->t = NULL;
        return false;
    }
    return true;
}

static inline void ph_close(ph_state *st)
{
    if (st->cp == PH_CP_OPEN) {
        if (st->t) piece_checkpoint_abort(st->t);
        pm_checkpoint_abort(st->m);
    }
    for (unsigned i = 0; i < PH_SNAPS; i++) {
        if (st->s[i]) piece_snapshot_release(st->s[i]);
        if (st->ms[i]) pm_snapshot_release(st->ms[i]);
    }
    if (st->t) piece_close(st->t);
    if (st->m) pm_close(st->m);
    for (unsigned i = 0; i < PH_LISTS; i++) ph_spans_free(&st->list[i]);
    if (st->r) piece_reserve_destroy(st->r);
    if (st->mr) pm_reserve_destroy(st->mr);
    if (st->map) {
        /* One owner per core, released by the core's last owner. */
        PH_CHECK(atomic_load(&st->map->acquires) == st->mapped_sides &&
                 atomic_load(&st->map->releases) == st->mapped_sides,
                 "backing acquired %d, released %d times, want %d", atomic_load(&st->map->acquires),
                 atomic_load(&st->map->releases), st->mapped_sides);
        ph_map_unref(st->map);
    }
    memset(st, 0, sizeof *st);
}

/* Results seen by the IUT calls that can fail, for the suite's summary. */
static unsigned long ph_seen_ok, ph_seen_nomem, ph_seen_range;

/* An error is allowed only for the reason the contract gives. */
static inline void ph_expect_err(const ph_state *st, piece_err e, bool refused, const char *op)
{
    switch (e) {
    case PIECE_OK: ph_seen_ok++; break;
    case PIECE_ERR_NOMEM: ph_seen_nomem++; break;
    case PIECE_ERR_RANGE: ph_seen_range++; break;
    }
    PH_CHECK(e == PIECE_OK || (e == PIECE_ERR_NOMEM && refused) || (e == PIECE_ERR_RANGE && st->height_limited),
             "%s returned %d (refusing %d)", op, (int)e, (int)refused);
}

static inline void ph_moved(ph_state *st) { st->moved = true; }

static inline void ph_op_replace(ph_state *st, uint64_t off, uint64_t del, const uint8_t *bytes, size_t n,
                                 bool refuse)
{
    ph_refuse(refuse);
    piece_err e = piece_replace(st->t, off, del, bytes, n);
    ph_refuse(false);
    ph_expect_err(st, e, refuse, "replace");
    if (e != PIECE_OK) return;
    PH_CHECK(pm_replace(st->m, off, del, bytes, n) == PIECE_OK, "model replace");
    if (del || n) ph_moved(st);
}

static inline void ph_op_replace_spans(ph_state *st, uint64_t off, uint64_t del, const ph_spans *l,
                                       bool with_nl, bool refuse)
{
    ph_refuse(refuse);
    piece_err e = piece_replace_spans(st->t, off, del, l->s, with_nl ? l->nl : NULL, l->n);
    ph_refuse(false);
    ph_expect_err(st, e, refuse, "replace_spans");
    if (e != PIECE_OK) return;
    PH_CHECK(pm_replace_spans(st->m, off, del, l->s, NULL, l->n) == PIECE_OK, "model replace_spans");
    if (del || l->n) ph_moved(st);
}

static inline void ph_op_capture(ph_state *st, uint64_t off, uint64_t len, unsigned li, bool refuse)
{
    piece_span got = {0, 0}, want = {0, 0};
    uint64_t a = piece_add_length(st->t);
    ph_refuse(refuse);
    piece_err e = piece_capture(st->t, off, len, &got);
    ph_refuse(false);
    ph_expect_err(st, e, refuse, "capture");
    if (e != PIECE_OK) return;
    PH_CHECK(pm_capture(st->m, off, len, &want) == PIECE_OK, "model capture");
    PH_CHECK(ph_span_eq(got, want) && got.src == (PIECE_SPAN_ADD | a), "capture span {%llx, %llu}",
             PH_U(got.src), PH_U(got.len));
    ph_spans_clear(&st->list[li]);
    ph_spans_push(&st->list[li], got, (piece_span_nl){PIECE_NL_UNKNOWN, PIECE_NL_UNKNOWN});
    st->list_inside[li] = st->cp == PH_CP_OPEN;
    st->list_foreign[li] = false;
}

/* Describes [off, off + len) of the live view (src < 0) or of a snapshot. */
static inline void ph_op_describe(ph_state *st, int src, uint64_t off, uint64_t len, unsigned li)
{
    const piece_view *v = src < 0 ? piece_tree_view(st->t) : piece_snapshot_view(st->s[src]);
    const pm_view *mv = src < 0 ? pm_tree_view(st->m) : pm_snapshot_view(st->ms[src]);
    ph_spans ref = {0};
    size_t c0 = ph_calls();
    ph_describe(v, off, len, true, &st->list[li]);
    PH_CHECK(ph_calls() == c0, "describe reserved storage");
    ph_describe_model(mv, off, len, &ref);
    ph_spans_check(&st->list[li], &ref, "describe");
    ph_spans_free(&ref);
    st->list_inside[li] = st->cp == PH_CP_OPEN;
    st->list_foreign[li] = src >= 0 && st->snap_foreign[src];
}

static inline void ph_op_take(ph_state *st, unsigned slot, bool refuse)
{
    if (st->s[slot]) {
        piece_snapshot_release(st->s[slot]);
        pm_snapshot_release(st->ms[slot]);
        st->s[slot] = NULL;
        st->ms[slot] = NULL;
    }
    bool hit = st->last && !st->moved;
    size_t c0 = ph_calls();
    piece_snapshot *s;
    ph_refuse(refuse);
    piece_err e = piece_snapshot_take(st->t, &s);
    ph_refuse(false);
    if (hit) {
        PH_CHECK(e == PIECE_OK && s == st->last, "cache hit returned %d, %s", (int)e,
                 s == st->last ? "same" : "another snapshot");
        PH_CHECK(ph_calls() == c0, "cache hit reserved storage");
    } else {
        ph_expect_err(st, e, refuse, "snapshot_take");
    }
    if (e != PIECE_OK) {
        PH_CHECK(s == NULL, "failed take left *out set");
        return;
    }
    pm_snap *ms;
    PH_CHECK(pm_snapshot_take(st->m, &ms) == PIECE_OK, "model take");
    if (!hit)
        for (unsigned i = 0; i < PH_SNAPS; i++)
            PH_CHECK(st->s[i] != s, "a fresh take returned live snapshot %u", i);
    st->s[slot] = s;
    st->ms[slot] = ms;
    st->snap_inside[slot] = st->cp == PH_CP_OPEN;
    st->snap_foreign[slot] = false;
    st->last = s;
    st->mlast = ms;
    st->moved = false;
}

static inline void ph_op_release(ph_state *st, unsigned slot)
{
    if (!st->s[slot]) return;
    size_t c0 = ph_calls();
    piece_snapshot_release(st->s[slot]);
    pm_snapshot_release(st->ms[slot]);
    PH_CHECK(ph_calls() == c0, "release reserved storage");
    st->s[slot] = NULL;
    st->ms[slot] = NULL;
}

static inline void ph_op_retain(ph_state *st, unsigned from, unsigned to)
{
    if (!st->s[from] || from == to) return;
    ph_op_release(st, to);
    size_t c0 = ph_calls();
    st->s[to] = piece_snapshot_retain(st->s[from]);
    st->ms[to] = pm_snapshot_retain(st->ms[from]);
    st->snap_inside[to] = st->snap_inside[from];
    st->snap_foreign[to] = st->snap_foreign[from];
    PH_CHECK(st->s[to] == st->s[from] && ph_calls() == c0, "retain");
}

static inline void ph_op_begin(ph_state *st, bool refuse)
{
    if (st->cp == PH_CP_OPEN) return;
    ph_refuse(refuse);
    piece_err e = piece_checkpoint_begin(st->t);
    ph_refuse(false);
    ph_expect_err(st, e, refuse, "checkpoint_begin");
    if (e != PIECE_OK) return;
    PH_CHECK(pm_checkpoint_begin(st->m) == PIECE_OK, "model begin");
    st->cp = PH_CP_OPEN;
    for (unsigned i = 0; i < PH_LISTS; i++) st->list_inside[i] = false;
}

/* Commit and abort run under refusal: they must allocate nothing. */
static inline void ph_op_end(ph_state *st, bool abort)
{
    if (st->cp != PH_CP_OPEN) return;
    size_t c0 = ph_calls();
    ph_refuse(true);
    if (abort) piece_checkpoint_abort(st->t);
    else piece_checkpoint_commit(st->t);
    ph_refuse(false);
    PH_CHECK(ph_calls() == c0, "checkpoint %s reserved storage", abort ? "abort" : "commit");
    if (abort) pm_checkpoint_abort(st->m);
    else pm_checkpoint_commit(st->m);
    st->cp = PH_CP_NONE;
    for (unsigned i = 0; i < PH_LISTS; i++) {
        if (abort && st->list_inside[i]) ph_spans_clear(&st->list[i]); /* spans minted inside are invalid */
        st->list_inside[i] = false;
    }
    for (unsigned i = 0; i < PH_SNAPS; i++) {
        if (abort && st->snap_inside[i]) st->snap_foreign[i] = true;
        st->snap_inside[i] = false;
    }
    if (abort) ph_moved(st);
}

static inline void ph_op_maintain(ph_state *st, bool refuse, size_t budget)
{
    ph_refuse(refuse);
    piece_err e = piece_replenish(st->t);
    ph_refuse(false);
    ph_expect_err(st, e, refuse, "replenish");
    size_t c0 = ph_calls();
    ph_refuse(true);
    (void)piece_reclaim(st->t, budget);
    ph_refuse(false);
    PH_CHECK(ph_calls() == c0, "reclaim reserved storage");
}

/* ---- byte-coded operations (the fuzzer's input and the random test) ----- */

typedef struct ph_in {
    const uint8_t *p;
    size_t n;
} ph_in;

static inline uint8_t ph_u8(ph_in *in)
{
    if (!in->n) return 0;
    in->n--;
    return *in->p++;
}

static inline uint32_t ph_u32(ph_in *in)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | ph_u8(in);
    return v;
}

/* Bytes the model and the tree must keep apart: newlines, CR, letters. */
static inline void ph_fill(uint8_t *b, size_t n, uint32_t seed)
{
    static const uint8_t alpha[] = "ab\n\r xyz\n";
    uint64_t s = seed | 1u;
    for (size_t i = 0; i < n; i++) b[i] = alpha[ph_rand(&s) % (sizeof alpha - 1u)];
}

enum {
    PH_OP_INSERT, PH_OP_DELETE, PH_OP_REPLACE, PH_OP_LARGE, PH_OP_DESCRIBE, PH_OP_SPANS, PH_OP_CAPTURE,
    PH_OP_TAKE, PH_OP_RELEASE, PH_OP_RETAIN, PH_OP_BEGIN, PH_OP_COMMIT, PH_OP_ABORT, PH_OP_MAINTAIN,
    PH_OP_CHECK, PH_OP_COUNT
};

/* One operation from in; returns false when the input is exhausted. */
static inline bool ph_step(ph_state *st, ph_in *in, uint8_t *scratch)
{
    if (!in->n) return false;
    uint8_t code = ph_u8(in);
    bool refuse = (code & 0x80) != 0;
    unsigned op = (unsigned)(code & 0x7f) % PH_OP_COUNT;
    uint32_t a = ph_u32(in), b = ph_u32(in);
    uint64_t len = piece_length(piece_tree_view(st->t));
    uint64_t off = a % (len + 1u), rest = len - off;
    unsigned slot = (b >> 24) % PH_SNAPS, li = (b >> 16) % PH_LISTS;
    switch (op) {
    case PH_OP_INSERT: {
        size_t n = 1u + b % 512u;
        ph_fill(scratch, n, b);
        ph_op_replace(st, off, 0, scratch, n, refuse);
        break;
    }
    case PH_OP_DELETE:
        ph_op_replace(st, off, rest ? b % (rest + 1u) : 0, NULL, 0, refuse);
        break;
    case PH_OP_REPLACE: {
        size_t n = b % 64u;
        ph_fill(scratch, n, a);
        ph_op_replace(st, off, rest ? (b >> 8) % (rest + 1u) : 0, scratch, n, refuse);
        break;
    }
    case PH_OP_LARGE: { /* one in four crosses an ADD chunk boundary in one call */
        size_t n = b % 4u == 0 ? PH_LARGE : 1u + b % 4096u;
        ph_fill(scratch, n, b);
        ph_op_replace(st, off, 0, scratch, n, refuse);
        break;
    }
    case PH_OP_DESCRIBE: {
        int src = -1;
        if (b & 1u && st->s[slot]) src = (int)slot;
        uint64_t vl = src < 0 ? len : piece_length(piece_snapshot_view(st->s[slot]));
        uint64_t o = a % (vl + 1u);
        ph_op_describe(st, src, o, (b >> 1) % (vl - o + 1u), li);
        break;
    }
    case PH_OP_SPANS:
        if (!st->list_foreign[li] && st->list[li].len <= PIECE_MAX_LENGTH - len)
            ph_op_replace_spans(st, off, rest ? (b >> 1) % (rest + 1u) : 0, &st->list[li], b & 1u, refuse);
        break;
    case PH_OP_CAPTURE:
        if (rest) ph_op_capture(st, off, 1u + b % (rest < 70000u ? rest : 70000u), li, refuse);
        break;
    case PH_OP_TAKE:
        ph_op_take(st, slot, refuse);
        break;
    case PH_OP_RELEASE:
        ph_op_release(st, slot);
        break;
    case PH_OP_RETAIN:
        ph_op_retain(st, slot, (unsigned)a % PH_SNAPS);
        break;
    case PH_OP_BEGIN:
        ph_op_begin(st, refuse);
        break;
    case PH_OP_COMMIT:
        ph_op_end(st, false);
        break;
    case PH_OP_ABORT:
        ph_op_end(st, true);
        break;
    case PH_OP_MAINTAIN:
        ph_op_maintain(st, refuse, b % 64u);
        break;
    case PH_OP_CHECK:
        ph_check_snaps(st, "snapshots");
        break;
    default:
        ASSERT(op < PH_OP_COUNT);
    }
    ph_check_tree(st, 1, "tree");
    return true;
}

/* scratch holds at least PH_LARGE bytes. */
#define PH_SCRATCH PH_LARGE

/* ---- Lines (draft section): fault polls and the truth ------------------- */

/* -Wl,--wrap=os_fault_poll: a test arms a simulated fault (a nonzero epoch)
 * or a failing poll around a call under test, never around the reference. */
typedef enum ph_poll { PH_POLL_REAL, PH_POLL_FAULTED, PH_POLL_ERROR } ph_poll;
static atomic_int ph_poll_mode;

os_err __real_os_fault_poll(os_fault_id id, uint64_t *epoch);
os_err __wrap_os_fault_poll(os_fault_id id, uint64_t *epoch);

os_err __wrap_os_fault_poll(os_fault_id id, uint64_t *epoch)
{
    ph_poll mode = (ph_poll)atomic_load(&ph_poll_mode);
    switch (mode) {
    case PH_POLL_REAL:
        return __real_os_fault_poll(id, epoch);
    case PH_POLL_FAULTED: {
        os_err e = __real_os_fault_poll(id, epoch);
        if (e == OS_OK) *epoch += 1u;
        return e;
    }
    case PH_POLL_ERROR:
        return OS_E_NOENT;
    }
    return OS_E_NOENT;
}

static inline void ph_poll_set(ph_poll m) { atomic_store(&ph_poll_mode, (int)m); }

/* Newline positions of the reference text: the truth for rank and select. */
typedef struct ph_lines_truth {
    uint8_t *bytes;
    uint64_t n, total;
    uint64_t *pos; /* offsets of the newlines, ascending */
} ph_lines_truth;

static inline void ph_truth_init(ph_lines_truth *tr, const pm_view *m)
{
    tr->bytes = ph_bytes_model(m, &tr->n);
    tr->total = 0;
    for (uint64_t i = 0; i < tr->n; i++) tr->total += tr->bytes[i] == '\n';
    tr->pos = malloc((size_t)(tr->total ? tr->total : 1u) * sizeof *tr->pos);
    ASSERT(tr->pos != NULL);
    for (uint64_t i = 0, k = 0; i < tr->n; i++)
        if (tr->bytes[i] == '\n') tr->pos[k++] = i;
}

static inline void ph_truth_free(ph_lines_truth *tr)
{
    free(tr->bytes);
    free(tr->pos);
}

static inline uint64_t ph_truth_rank(const ph_lines_truth *tr, uint64_t off)
{
    uint64_t lo = 0, hi = tr->total; /* newlines before off */
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2u;
        if (tr->pos[mid] < off) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

static inline uint64_t ph_truth_start(const ph_lines_truth *tr, uint64_t line)
{
    if (line == 0) return 0;
    return line <= tr->total ? tr->pos[line - 1u] + 1u : tr->n;
}

/* Answers seen, for the suite's summary: both kinds must occur. */
static unsigned long ph_lines_exact, ph_lines_inexact;

/* Checks rank and select on v against the truth: an exact answer is true,
 * an inexact one is a sound bound; want_exact also demands exactness. */
static inline void ph_check_lines(const piece_view *v, const pm_view *m, uint64_t seed, unsigned probes,
                                  bool want_exact, const char *tag)
{
    ph_lines_truth tr;
    ph_truth_init(&tr, m);
    PH_CHECK(piece_length(v) == tr.n, "%s: length", tag);
    for (unsigned i = 0; i < probes + 2u; i++) {
        uint64_t off = i == 0 ? tr.n : i == 1 ? 0 : ph_rand(&seed) % (tr.n + 1u), count = UINT64_MAX;
        bool exact = piece_lines_before(v, off, &count);
        uint64_t truth = ph_truth_rank(&tr, off);
        if (exact) ph_lines_exact++;
        else ph_lines_inexact++;
        if (exact) PH_CHECK(count == truth, "%s: rank(%llu) %llu, true %llu", tag, PH_U(off), PH_U(count), PH_U(truth));
        else PH_CHECK(count <= truth, "%s: rank(%llu) bound %llu above %llu", tag, PH_U(off), PH_U(count), PH_U(truth));
        if (want_exact) PH_CHECK(exact, "%s: rank(%llu) inexact", tag, PH_U(off));

        uint64_t line = i == 0 ? tr.total : i == 1 ? tr.total + 1u : ph_rand(&seed) % (tr.total + 3u);
        uint64_t at = UINT64_MAX, before = UINT64_MAX, want = ph_truth_start(&tr, line);
        exact = piece_lines_start(v, line, &at, &before);
        if (exact) ph_lines_exact++;
        else ph_lines_inexact++;
        if (exact) {
            PH_CHECK(at == want && before == (line <= tr.total ? line : tr.total), "%s: select(%llu) (%llu, %llu), "
                     "true (%llu, %llu)", tag, PH_U(line), PH_U(at), PH_U(before), PH_U(want),
                     PH_U(line <= tr.total ? line : tr.total));
        } else {
            PH_CHECK(at <= tr.n && before == ph_truth_rank(&tr, at) && before < line && (line > tr.total || want > at),
                     "%s: select(%llu) inexact (%llu, %llu) unsound, true start %llu", tag, PH_U(line), PH_U(at),
                     PH_U(before), PH_U(want));
        }
        if (want_exact) PH_CHECK(exact, "%s: select(%llu) inexact", tag, PH_U(line));
    }
    ph_truth_free(&tr);
}

/* Counts and adopts every chunk of the mapped ORIGINAL, then drains the
 * resolve sweep; false (with a failure) if that does not finish. */
static inline bool ph_lines_complete(piece_tree *t, const ph_mapping *map, const char *tag)
{
    uint64_t chunks = (map->b.len + PIECE_CHUNK - 1u) / PIECE_CHUNK;
    uint32_t counts[16];
    for (uint64_t first = 0; first < chunks; first += 16u) {
        size_t n = (size_t)(chunks - first < 16u ? chunks - first : 16u);
        PH_CHECK(piece_lines_count(&map->b, first, counts, n), "%s: count chunks %llu", tag, PH_U(first));
        PH_CHECK(piece_lines_adopt(t, map->b.fault, first, counts, n), "%s: adopt chunks %llu", tag, PH_U(first));
    }
    /* A guard, not a budget (P): a tree of d descriptors has fewer than 2d
     * nodes (a leaf holds one or more, a branch two or more children), these
     * fixtures stay under 32,000 descriptors, so one pass of the sweep takes
     * fewer than 64,000 visits: 1,000 calls of budget 64. */
    for (unsigned call = 0; call < 1000u; call++) {
        bool more = true;
        piece_err e = piece_lines_resolve(t, 64, &more);
        PH_CHECK(e == PIECE_OK, "%s: resolve %d", tag, (int)e);
        if (e != PIECE_OK) return false;
        if (!more) return true;
    }
    PH_CHECK(0, "%s: the resolve sweep did not finish", tag);
    return false;
}

#endif
