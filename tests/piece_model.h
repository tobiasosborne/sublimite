/* Flat reference model of src/piece/piece.h (docs/decisions/v2-0fv.7.md §7).
 *
 * No pool, no tree. A text is a canonical array of coalesced spans: no
 * empty span, and no two neighbours whose coordinates continue each other.
 * Its bytes resolve through two stores: ORIGINAL (the private copy, or the
 * borrowed mapped view) and ADD (a directory of PIECE_CHUNK-byte chunks that
 * never move, so bytes a live iterator returned stay put until the next
 * edit). Every edit builds its result in new storage and installs it only
 * when nothing can fail any more. A snapshot copies the span array and the
 * ADD prefix, so no later append, and no abort that lowers the ADD length,
 * can change its bytes. A checkpoint keeps a copy of the span array and the
 * ADD length; abort swaps them back and allocates nothing.
 *
 * All storage comes from base_arena (os_vm_reserve underneath), so a test
 * that refuses os_vm_reserve refuses the model exactly as it refuses base.
 * Snapshot owners are atomic: snapshots may be read, retained and released
 * on any thread. The Lines section is not modelled yet; its functions
 * ASSERT.
 *
 * Two uses:
 *  - pm_*: the reference, called directly by tests/piece_test.c and
 *    fuzz/piece_fuzz.c next to the implementation under test;
 *  - with PIECE_MODEL_IMPL defined (tests/piece_model.c, exactly one TU),
 *    external definitions of every piece.h symbol on top of pm_*. A suite
 *    that links build/<cfg>/tests/piece_model.o ahead of libtext.a never
 *    selects the archive's piece.o; a reference that would is a
 *    duplicate-definition link error. */
#ifndef TESTS_PIECE_MODEL_H
#define TESTS_PIECE_MODEL_H

#include "piece/piece.h"
#include "base/base.h"
#include "os/os.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

/* ---- storage ------------------------------------------------------------ */

/* One base reservation per buffer; n == 0 reserves nothing. */
static inline bool pm_mem_get(base_arena *a, size_t n, void **p)
{
    *a = (base_arena){0};
    *p = NULL;
    if (n == 0) return true;
    if (base_arena_init(a, n) != 0) return false;
    *p = base_arena_alloc(a, n, 16);
    ASSERT(*p != NULL);
    return true;
}

static inline void pm_mem_put(base_arena *a)
{
    base_arena copy = *a; /* the handle may live inside the reservation */
    *a = (base_arena){0};
    if (copy.base) base_arena_free(&copy);
}

/* ---- objects ------------------------------------------------------------ */

typedef struct pm_reserve {
    atomic_size_t cores;
    base_arena self;
} pm_reserve;

typedef struct pm_core {
    atomic_size_t owners; /* the open tree, plus one per live snapshot */
    pm_reserve *reserve;
    const uint8_t *orig;
    uint64_t orig_len;
    bool mapped;
    piece_backing backing;
    base_arena orig_mem, self;
} pm_core;

typedef struct pm_text {
    piece_span *s;
    size_t n;
    uint64_t len;
    base_arena mem;
} pm_text;

struct pm_tree;
struct pm_snap;

/* Exactly one of tree, snap is set. */
typedef struct pm_view {
    const struct pm_tree *tree;
    const struct pm_snap *snap;
} pm_view;

typedef struct pm_snap {
    pm_view view;
    atomic_size_t owners;
    pm_core *core;
    pm_text text;
    uint8_t *add; /* flat copy of the tree's ADD [0, add_len) at take */
    uint64_t add_len;
    base_arena add_mem, self;
} pm_snap;

typedef struct pm_chunk {
    base_arena mem; /* PIECE_CHUNK bytes; mem.base is the data */
} pm_chunk;

typedef enum pm_cp { PM_CP_NONE, PM_CP_OPEN } pm_cp;

typedef struct pm_tree {
    pm_view view;
    pm_core *core;
    pm_reserve *reserve;
    pm_text text;
    pm_chunk *chunk; /* ADD directory */
    size_t nchunk, chunk_cap;
    base_arena dir_mem;
    uint64_t add_len;
    uint64_t rev; /* the live view's revision */
    pm_snap *cache;
    pm_cp cp;
    pm_text cp_text;
    uint64_t cp_add_len;
    base_arena self;
} pm_tree;

typedef struct pm_iter {
    const pm_view *v;
    uint64_t pos, rev;
    size_t si;   /* span holding pos */
    uint64_t so; /* offset of pos inside it */
} pm_iter;

/* ---- coordinates -------------------------------------------------------- */

static inline bool pm_is_add(uint64_t src) { return (src & PIECE_SPAN_ADD) != 0; }
static inline uint64_t pm_off(uint64_t src) { return src & ~PIECE_SPAN_ADD; }

static inline const pm_text *pm_view_text(const pm_view *v)
{
    return v->tree ? &v->tree->text : &v->snap->text;
}

static inline const pm_core *pm_view_core(const pm_view *v)
{
    return v->tree ? v->tree->core : v->snap->core;
}

/* Bytes at coordinate src, contiguous for *room bytes. */
static inline const uint8_t *pm_at(const pm_view *v, uint64_t src, uint64_t *room)
{
    uint64_t o = pm_off(src);
    if (!pm_is_add(src)) {
        const pm_core *c = pm_view_core(v);
        *room = c->orig_len - o;
        return c->orig + o;
    }
    if (v->snap) {
        *room = v->snap->add_len - o;
        return v->snap->add + o;
    }
    const pm_tree *t = v->tree;
    uint64_t k = o / PIECE_CHUNK, in = o % PIECE_CHUNK;
    *room = PIECE_CHUNK - in;
    return (const uint8_t *)t->chunk[k].mem.base + in;
}

static inline uint64_t pm_min(uint64_t a, uint64_t b) { return a < b ? a : b; }

/* Newlines of the coordinates [src, src + len), byte by byte. */
static inline uint64_t pm_count_nl(const pm_view *v, uint64_t src, uint64_t len)
{
    uint64_t n = 0;
    while (len) {
        uint64_t room;
        const uint8_t *p = pm_at(v, src, &room);
        uint64_t k = pm_min(room, len);
        for (uint64_t i = 0; i < k; i++) n += p[i] == '\n';
        src += k;
        len -= k;
    }
    return n;
}

/* ---- canonical span arrays ---------------------------------------------- */

typedef struct pm_build {
    pm_text t;
    size_t cap;
} pm_build;

static inline bool pm_build_init(pm_build *b, size_t cap)
{
    b->t = (pm_text){0};
    b->cap = cap;
    void *p;
    if (!pm_mem_get(&b->t.mem, cap * sizeof(piece_span), &p)) return false;
    b->t.s = p;
    return true;
}

static inline void pm_push(pm_build *b, piece_span sp)
{
    if (sp.len == 0) return;
    piece_span *last = b->t.n ? &b->t.s[b->t.n - 1] : NULL;
    if (last && last->src + last->len == sp.src) {
        last->len += sp.len; /* same store: the tag bit never carries */
    } else {
        ASSERT(b->t.n < b->cap);
        b->t.s[b->t.n++] = sp;
    }
    b->t.len += sp.len;
}

static inline void pm_text_free(pm_text *t)
{
    pm_mem_put(&t->mem);
    *t = (pm_text){0};
}

static inline bool pm_text_copy(const pm_text *from, pm_text *out)
{
    pm_build b;
    if (!pm_build_init(&b, from->n)) return false;
    for (size_t i = 0; i < from->n; i++) pm_push(&b, from->s[i]);
    *out = b.t;
    return true;
}

/* old[0, off) . ins . old[off + del, len) in new storage. */
static inline bool pm_splice(const pm_text *old, uint64_t off, uint64_t del,
                             const piece_span *ins, size_t nins, pm_text *out)
{
    pm_build b;
    if (!pm_build_init(&b, old->n + nins + 1u)) return false;
    uint64_t p = 0, end = off + del;
    for (size_t i = 0; i < old->n && p < off; p += old->s[i].len, i++) {
        piece_span s = old->s[i];
        s.len = pm_min(s.len, off - p);
        pm_push(&b, s);
    }
    for (size_t i = 0; i < nins; i++) pm_push(&b, ins[i]);
    p = 0;
    for (size_t i = 0; i < old->n; p += old->s[i].len, i++) {
        piece_span s = old->s[i];
        if (p + s.len <= end) continue;
        uint64_t skip = end > p ? end - p : 0;
        s.src += skip;
        s.len -= skip;
        pm_push(&b, s);
    }
    *out = b.t;
    return true;
}

/* ---- reserve, core, tree ------------------------------------------------ */

static inline piece_err pm_reserve_create(pm_reserve **out)
{
    ASSERT(out != NULL);
    *out = NULL;
    base_arena a;
    void *p;
    if (!pm_mem_get(&a, sizeof(pm_reserve), &p)) return PIECE_ERR_NOMEM;
    pm_reserve *r = p;
    atomic_init(&r->cores, 0);
    r->self = a;
    *out = r;
    return PIECE_OK;
}

static inline void pm_reserve_destroy(pm_reserve *r)
{
    ASSERT(r != NULL);
    ASSERT(atomic_load(&r->cores) == 0);
    pm_mem_put(&r->self);
}

static inline void pm_core_release(pm_core *c)
{
    if (atomic_fetch_sub(&c->owners, 1) != 1) return;
    if (c->mapped) c->backing.release(c->backing.ctx);
    else pm_mem_put(&c->orig_mem);
    pm_reserve *r = c->reserve;
    pm_mem_put(&c->self);
    atomic_fetch_sub(&r->cores, 1);
}

/* A tree over core c with one span covering ORIGINAL; frees c on failure. */
static inline piece_err pm_tree_new(pm_reserve *r, pm_core *c, pm_tree **out)
{
    base_arena a;
    void *p;
    pm_build b;
    if (!pm_mem_get(&a, sizeof(pm_tree), &p)) return PIECE_ERR_NOMEM;
    if (!pm_build_init(&b, c->orig_len ? 1u : 0u)) {
        pm_mem_put(&a);
        return PIECE_ERR_NOMEM;
    }
    pm_push(&b, (piece_span){0, c->orig_len});
    pm_tree *t = p;
    memset(t, 0, sizeof *t);
    t->self = a;
    t->core = c;
    t->reserve = r;
    t->text = b.t;
    t->cp = PM_CP_NONE;
    t->view.tree = t;
    *out = t;
    return PIECE_OK;
}

static inline piece_err pm_core_new(pm_reserve *r, pm_core **out)
{
    base_arena a;
    void *p;
    if (!pm_mem_get(&a, sizeof(pm_core), &p)) return PIECE_ERR_NOMEM;
    pm_core *c = p;
    memset(c, 0, sizeof *c);
    c->self = a;
    c->reserve = r;
    atomic_init(&c->owners, 1);
    *out = c;
    return PIECE_OK;
}

static inline piece_err pm_open_copy(pm_reserve *r, const uint8_t *bytes, size_t n, pm_tree **out)
{
    ASSERT(r != NULL && out != NULL);
    ASSERT(n == 0 || bytes != NULL);
    ASSERT(n <= PIECE_MAX_LENGTH);
    *out = NULL;
    pm_core *c;
    if (pm_core_new(r, &c) != PIECE_OK) return PIECE_ERR_NOMEM;
    void *p;
    if (!pm_mem_get(&c->orig_mem, n, &p)) {
        pm_mem_put(&c->self);
        return PIECE_ERR_NOMEM;
    }
    if (n) memcpy(p, bytes, n);
    c->orig = p;
    c->orig_len = n;
    if (pm_tree_new(r, c, out) != PIECE_OK) {
        pm_mem_put(&c->orig_mem);
        pm_mem_put(&c->self);
        return PIECE_ERR_NOMEM;
    }
    atomic_fetch_add(&r->cores, 1);
    return PIECE_OK;
}

static inline piece_err pm_open_mapped(pm_reserve *r, const piece_backing *b, pm_tree **out)
{
    ASSERT(r != NULL && b != NULL && out != NULL);
    ASSERT(b->bytes != NULL && b->acquire != NULL && b->release != NULL);
    ASSERT(b->fault.gen != 0);
    ASSERT(b->len > 0 && b->len <= PIECE_MAX_LENGTH);
    *out = NULL;
    pm_core *c;
    if (pm_core_new(r, &c) != PIECE_OK) return PIECE_ERR_NOMEM;
    c->orig = b->bytes;
    c->orig_len = b->len;
    c->mapped = true;
    c->backing = *b;
    if (pm_tree_new(r, c, out) != PIECE_OK) {
        pm_mem_put(&c->self);
        return PIECE_ERR_NOMEM;
    }
    b->acquire(b->ctx);
    atomic_fetch_add(&r->cores, 1);
    return PIECE_OK;
}

static inline void pm_snapshot_release(pm_snap *s);

static inline void pm_drop_cache(pm_tree *t)
{
    if (t->cache) pm_snapshot_release(t->cache);
    t->cache = NULL;
}

static inline void pm_close(pm_tree *t)
{
    ASSERT(t != NULL);
    ASSERT(t->cp == PM_CP_NONE);
    pm_drop_cache(t);
    pm_text_free(&t->text);
    for (size_t i = 0; i < t->nchunk; i++) pm_mem_put(&t->chunk[i].mem);
    pm_mem_put(&t->dir_mem);
    pm_core *c = t->core;
    pm_mem_put(&t->self);
    pm_core_release(c);
}

static inline const pm_view *pm_tree_view(const pm_tree *t) { return &t->view; }
static inline uint64_t pm_add_length(const pm_tree *t) { return t->add_len; }

/* ---- reading ------------------------------------------------------------ */

static inline uint64_t pm_length(const pm_view *v) { return pm_view_text(v)->len; }

static inline const piece_backing *pm_view_backing(const pm_view *v)
{
    const pm_core *c = pm_view_core(v);
    return c->mapped ? &c->backing : NULL;
}

static inline uint64_t pm_view_rev(const pm_view *v) { return v->tree ? v->tree->rev : 0; }

static inline void pm_iter_seek(pm_iter *it, uint64_t off)
{
    const pm_text *t = pm_view_text(it->v);
    ASSERT(off <= t->len);
    it->pos = off;
    it->rev = pm_view_rev(it->v);
    it->si = 0;
    uint64_t p = 0;
    while (it->si < t->n && p + t->s[it->si].len <= off) p += t->s[it->si++].len;
    it->so = off - p;
}

static inline void pm_iter_init(pm_iter *it, const pm_view *v, uint64_t off)
{
    ASSERT(it != NULL && v != NULL);
    it->v = v;
    pm_iter_seek(it, off);
}

static inline size_t pm_iter_next(pm_iter *it, const uint8_t **p)
{
    ASSERT(p != NULL);
    ASSERT(it->rev == pm_view_rev(it->v));
    const pm_text *t = pm_view_text(it->v);
    if (it->si >= t->n) return 0;
    piece_span s = t->s[it->si];
    uint64_t room;
    *p = pm_at(it->v, s.src + it->so, &room);
    uint64_t n = pm_min(room, s.len - it->so);
    it->pos += n;
    it->so += n;
    if (it->so == s.len) {
        it->si++;
        it->so = 0;
    }
    return (size_t)n;
}

/* The record of coordinates [src, src + len): head and tail per piece.h. A
 * mapped part read while the backing's epoch is nonzero is unknown. */
static inline uint32_t pm_nl_part(const pm_view *v, uint64_t src, uint64_t len)
{
    const pm_core *c = pm_view_core(v);
    uint64_t e0 = 0, e1 = 0;
    bool mapped = c->mapped && !pm_is_add(src);
    if (mapped && (os_fault_poll(c->backing.fault, &e0) != OS_OK || e0 != 0)) return PIECE_NL_UNKNOWN;
    uint64_t n = pm_count_nl(v, src, len);
    if (mapped && (os_fault_poll(c->backing.fault, &e1) != OS_OK || e1 != 0)) return PIECE_NL_UNKNOWN;
    return (uint32_t)n;
}

static inline piece_span_nl pm_record(const pm_view *v, piece_span s)
{
    uint64_t o = pm_off(s.src), tag = s.src & PIECE_SPAN_ADD;
    uint64_t i = o / PIECE_CHUNK, j = (o + s.len - 1u) / PIECE_CHUNK;
    if (i == j) return (piece_span_nl){pm_nl_part(v, s.src, s.len), 0};
    uint64_t head = (i + 1u) * PIECE_CHUNK - o, tail0 = j * PIECE_CHUNK;
    return (piece_span_nl){pm_nl_part(v, s.src, head),
                           pm_nl_part(v, tag | tail0, o + s.len - tail0)};
}

static inline bool pm_iter_next_span(pm_iter *it, uint64_t end, piece_span *out, piece_span_nl *nl)
{
    ASSERT(out != NULL);
    ASSERT(it->rev == pm_view_rev(it->v));
    const pm_text *t = pm_view_text(it->v);
    if (it->pos >= end || it->si >= t->n) return false;
    piece_span s = t->s[it->si];
    piece_span r = {s.src + it->so, pm_min(s.len - it->so, end - it->pos)};
    *out = r;
    if (nl) *nl = pm_record(it->v, r);
    pm_iter_seek(it, it->pos + r.len);
    return true;
}

static inline uint8_t pm_byte_at(const pm_view *v, uint64_t off)
{
    ASSERT(off < pm_length(v));
    pm_iter it;
    pm_iter_init(&it, v, off);
    const uint8_t *p;
    size_t n = pm_iter_next(&it, &p);
    ASSERT(n >= 1);
    return p[0];
}

/* ---- ADD store ------------------------------------------------------------ */

/* Makes room for n more ADD bytes without touching add_len; all or nothing. */
static inline bool pm_add_reserve(pm_tree *t, uint64_t n)
{
    uint64_t need = (t->add_len + n + PIECE_CHUNK - 1u) / PIECE_CHUNK;
    if (need <= t->nchunk) return true;
    size_t want = (size_t)need;
    pm_chunk *dir = t->chunk;
    base_arena dir_mem = t->dir_mem;
    size_t cap = t->chunk_cap;
    if (want > cap) {
        size_t ncap = cap ? cap : 8u;
        while (ncap < want) ncap *= 2u;
        void *p;
        if (!pm_mem_get(&dir_mem, ncap * sizeof(pm_chunk), &p)) return false;
        dir = p;
        if (t->nchunk) memcpy(dir, t->chunk, t->nchunk * sizeof(pm_chunk));
        cap = ncap;
    }
    size_t k = t->nchunk;
    for (; k < want; k++) {
        void *p;
        if (!pm_mem_get(&dir[k].mem, PIECE_CHUNK, &p)) break;
    }
    if (k < want) {
        for (size_t i = t->nchunk; i < k; i++) pm_mem_put(&dir[i].mem);
        if (dir != t->chunk) pm_mem_put(&dir_mem);
        return false;
    }
    if (dir != t->chunk) {
        base_arena old = t->dir_mem;
        t->chunk = dir;
        t->dir_mem = dir_mem;
        pm_mem_put(&old);
    }
    t->chunk_cap = cap;
    t->nchunk = want;
    return true;
}

/* Copies n bytes to ADD at add_len (room reserved). */
static inline void pm_add_write(pm_tree *t, const uint8_t *bytes, uint64_t n)
{
    while (n) {
        uint64_t o = t->add_len, in = o % PIECE_CHUNK, k = pm_min(PIECE_CHUNK - in, n);
        memcpy((uint8_t *)t->chunk[o / PIECE_CHUNK].mem.base + in, bytes, (size_t)k);
        t->add_len += k;
        bytes += k;
        n -= k;
    }
}

/* ---- editing ------------------------------------------------------------ */

static inline void pm_install(pm_tree *t, pm_text *nt)
{
    pm_text_free(&t->text);
    t->text = *nt;
    t->rev++;
    pm_drop_cache(t);
}

static inline piece_err pm_replace(pm_tree *t, uint64_t off, uint64_t del, const uint8_t *bytes, size_t n)
{
    ASSERT(t != NULL);
    uint64_t len = t->text.len;
    ASSERT(off <= len && del <= len - off);
    ASSERT(n == 0 || bytes != NULL);
    ASSERT(n <= PIECE_MAX_LENGTH && len - del <= PIECE_MAX_LENGTH - n);
    if (del == 0 && n == 0) return PIECE_OK;
    piece_span ins = {PIECE_SPAN_ADD | t->add_len, n};
    pm_text nt;
    if (!pm_splice(&t->text, off, del, &ins, n ? 1u : 0u, &nt)) return PIECE_ERR_NOMEM;
    if (!pm_add_reserve(t, n)) {
        pm_text_free(&nt);
        return PIECE_ERR_NOMEM;
    }
    pm_add_write(t, bytes, n);
    pm_install(t, &nt);
    return PIECE_OK;
}

static inline piece_err pm_replace_spans(pm_tree *t, uint64_t off, uint64_t del, const piece_span *spans,
                                         const piece_span_nl *nl, size_t count)
{
    ASSERT(t != NULL);
    uint64_t len = t->text.len;
    ASSERT(off <= len && del <= len - off);
    ASSERT(count == 0 || spans != NULL);
    uint64_t room = PIECE_MAX_LENGTH - (len - del);
    ASSERT(len - del <= PIECE_MAX_LENGTH);
    for (size_t i = 0; i < count; i++) {
        uint64_t o = pm_off(spans[i].src), l = spans[i].len;
        uint64_t store = pm_is_add(spans[i].src) ? t->add_len : t->core->orig_len;
        ASSERT(l >= 1 && o <= store && l <= store - o);
        ASSERT(l <= room);
        room -= l;
        if (nl) ASSERT((nl[i].head <= PIECE_CHUNK || nl[i].head == PIECE_NL_UNKNOWN) &&
                       (nl[i].tail <= PIECE_CHUNK || nl[i].tail == PIECE_NL_UNKNOWN));
    }
    if (del == 0 && count == 0) return PIECE_OK;
    pm_text nt;
    if (!pm_splice(&t->text, off, del, spans, count, &nt)) return PIECE_ERR_NOMEM;
    pm_install(t, &nt);
    return PIECE_OK;
}

static inline piece_err pm_capture(pm_tree *t, uint64_t off, uint64_t len, piece_span *out)
{
    ASSERT(t != NULL && out != NULL);
    ASSERT(len >= 1 && off <= t->text.len && len <= t->text.len - off);
    if (!pm_add_reserve(t, len)) return PIECE_ERR_NOMEM;
    uint64_t a = t->add_len;
    pm_iter it;
    pm_iter_init(&it, &t->view, off);
    uint64_t left = len;
    while (left) {
        const uint8_t *p;
        uint64_t k = pm_min(pm_iter_next(&it, &p), left);
        pm_add_write(t, p, k); /* sources lie below a, the destination at or above */
        left -= k;
    }
    *out = (piece_span){PIECE_SPAN_ADD | a, len};
    return PIECE_OK;
}

/* ---- snapshots ---------------------------------------------------------- */

static inline piece_err pm_snapshot_take(pm_tree *t, pm_snap **out)
{
    ASSERT(t != NULL && out != NULL);
    *out = NULL;
    if (t->cache) {
        atomic_fetch_add(&t->cache->owners, 1);
        *out = t->cache;
        return PIECE_OK;
    }
    base_arena self, add_mem;
    void *p, *q;
    pm_text text;
    if (!pm_mem_get(&self, sizeof(pm_snap), &p)) return PIECE_ERR_NOMEM;
    if (!pm_text_copy(&t->text, &text)) {
        pm_mem_put(&self);
        return PIECE_ERR_NOMEM;
    }
    if (!pm_mem_get(&add_mem, (size_t)t->add_len, &q)) {
        pm_text_free(&text);
        pm_mem_put(&self);
        return PIECE_ERR_NOMEM;
    }
    for (uint64_t o = 0; o < t->add_len;) {
        uint64_t k = pm_min(PIECE_CHUNK, t->add_len - o);
        memcpy((uint8_t *)q + o, t->chunk[o / PIECE_CHUNK].mem.base, (size_t)k);
        o += k;
    }
    pm_snap *s = p;
    memset(s, 0, sizeof *s);
    s->self = self;
    s->add_mem = add_mem;
    s->add = q;
    s->add_len = t->add_len;
    s->text = text;
    s->core = t->core;
    s->view.snap = s;
    atomic_init(&s->owners, 2); /* the caller and the cache */
    atomic_fetch_add(&t->core->owners, 1);
    t->cache = s;
    *out = s;
    return PIECE_OK;
}

static inline pm_snap *pm_snapshot_retain(pm_snap *s)
{
    ASSERT(s != NULL);
    size_t was = atomic_fetch_add(&s->owners, 1);
    ASSERT(was >= 1);
    return s;
}

static inline void pm_snapshot_release(pm_snap *s)
{
    ASSERT(s != NULL);
    size_t was = atomic_fetch_sub(&s->owners, 1);
    ASSERT(was >= 1);
    if (was != 1) return;
    pm_core *c = s->core;
    pm_text_free(&s->text);
    pm_mem_put(&s->add_mem);
    pm_mem_put(&s->self);
    pm_core_release(c);
}

static inline const pm_view *pm_snapshot_view(const pm_snap *s) { return &s->view; }

/* ---- checkpoint ----------------------------------------------------------- */

static inline piece_err pm_checkpoint_begin(pm_tree *t)
{
    ASSERT(t != NULL);
    ASSERT(t->cp == PM_CP_NONE);
    if (!pm_text_copy(&t->text, &t->cp_text)) return PIECE_ERR_NOMEM;
    t->cp_add_len = t->add_len;
    t->cp = PM_CP_OPEN;
    return PIECE_OK;
}

static inline void pm_checkpoint_commit(pm_tree *t)
{
    ASSERT(t != NULL);
    ASSERT(t->cp == PM_CP_OPEN);
    pm_text_free(&t->cp_text);
    t->cp = PM_CP_NONE;
}

static inline void pm_checkpoint_abort(pm_tree *t)
{
    ASSERT(t != NULL);
    ASSERT(t->cp == PM_CP_OPEN);
    pm_text_free(&t->text);
    t->text = t->cp_text;
    t->cp_text = (pm_text){0};
    t->add_len = t->cp_add_len;
    t->cp = PM_CP_NONE;
    t->rev++;
    pm_drop_cache(t);
}

/* ---- maintenance: the model holds no spare storage and queues nothing ---- */

static inline piece_err pm_replenish(pm_tree *t)
{
    ASSERT(t != NULL);
    return PIECE_OK;
}

static inline bool pm_reclaim(pm_tree *t, size_t budget)
{
    ASSERT(t != NULL);
    (void)budget;
    return false;
}

/* ---- piece.h on top of the model (one TU: tests/piece_model.c) ----------- */

#ifdef PIECE_MODEL_IMPL

#define PM_R(x) ((pm_reserve *)(void *)(x))
#define PM_T(x) ((pm_tree *)(void *)(x))
#define PM_CT(x) ((const pm_tree *)(const void *)(x))
#define PM_S(x) ((pm_snap *)(void *)(x))
#define PM_CS(x) ((const pm_snap *)(const void *)(x))
#define PM_V(x) ((const pm_view *)(const void *)(x))

_Static_assert(sizeof(pm_iter) <= sizeof(piece_iter), "pm_iter fits piece_iter");
_Static_assert(_Alignof(pm_iter) <= _Alignof(piece_iter), "pm_iter alignment");

piece_err piece_reserve_create(piece_reserve **out)
{
    ASSERT(out != NULL);
    pm_reserve *r;
    piece_err e = pm_reserve_create(&r);
    *out = (piece_reserve *)(void *)r;
    return e;
}
void piece_reserve_destroy(piece_reserve *r) { pm_reserve_destroy(PM_R(r)); }

piece_err piece_open_copy(piece_reserve *r, const uint8_t *bytes, size_t n, piece_tree **out)
{
    ASSERT(out != NULL);
    pm_tree *t;
    piece_err e = pm_open_copy(PM_R(r), bytes, n, &t);
    *out = (piece_tree *)(void *)t;
    return e;
}
piece_err piece_open_mapped(piece_reserve *r, const piece_backing *b, piece_tree **out)
{
    ASSERT(out != NULL);
    pm_tree *t;
    piece_err e = pm_open_mapped(PM_R(r), b, &t);
    *out = (piece_tree *)(void *)t;
    return e;
}
void piece_close(piece_tree *t) { pm_close(PM_T(t)); }
const piece_view *piece_tree_view(const piece_tree *t)
{
    return (const piece_view *)(const void *)pm_tree_view(PM_CT(t));
}
uint64_t piece_add_length(const piece_tree *t) { return pm_add_length(PM_CT(t)); }

uint64_t piece_length(const piece_view *v) { return pm_length(PM_V(v)); }
uint8_t piece_byte_at(const piece_view *v, uint64_t off) { return pm_byte_at(PM_V(v), off); }
const piece_backing *piece_view_backing(const piece_view *v) { return pm_view_backing(PM_V(v)); }

void piece_iter_init(piece_iter *it, const piece_view *v, uint64_t off)
{
    ASSERT(it != NULL);
    pm_iter m;
    pm_iter_init(&m, PM_V(v), off);
    memcpy(it, &m, sizeof m);
}
void piece_iter_seek(piece_iter *it, uint64_t off)
{
    pm_iter m;
    memcpy(&m, it, sizeof m);
    pm_iter_seek(&m, off);
    memcpy(it, &m, sizeof m);
}
size_t piece_iter_next(piece_iter *it, const uint8_t **p)
{
    pm_iter m;
    memcpy(&m, it, sizeof m);
    size_t n = pm_iter_next(&m, p);
    memcpy(it, &m, sizeof m);
    return n;
}
bool piece_iter_next_span(piece_iter *it, uint64_t end, piece_span *out, piece_span_nl *nl)
{
    pm_iter m;
    memcpy(&m, it, sizeof m);
    bool more = pm_iter_next_span(&m, end, out, nl);
    memcpy(it, &m, sizeof m);
    return more;
}

piece_err piece_replace(piece_tree *t, uint64_t off, uint64_t del, const uint8_t *bytes, size_t n)
{
    return pm_replace(PM_T(t), off, del, bytes, n);
}
piece_err piece_replace_spans(piece_tree *t, uint64_t off, uint64_t del, const piece_span *spans,
                              const piece_span_nl *nl, size_t count)
{
    return pm_replace_spans(PM_T(t), off, del, spans, nl, count);
}
piece_err piece_capture(piece_tree *t, uint64_t off, uint64_t len, piece_span *out)
{
    return pm_capture(PM_T(t), off, len, out);
}

piece_err piece_snapshot_take(piece_tree *t, piece_snapshot **out)
{
    ASSERT(out != NULL);
    pm_snap *s;
    piece_err e = pm_snapshot_take(PM_T(t), &s);
    *out = (piece_snapshot *)(void *)s;
    return e;
}
piece_snapshot *piece_snapshot_retain(piece_snapshot *s)
{
    return (piece_snapshot *)(void *)pm_snapshot_retain(PM_S(s));
}
void piece_snapshot_release(piece_snapshot *s) { pm_snapshot_release(PM_S(s)); }
const piece_view *piece_snapshot_view(const piece_snapshot *s)
{
    return (const piece_view *)(const void *)pm_snapshot_view(PM_CS(s));
}

piece_err piece_checkpoint_begin(piece_tree *t) { return pm_checkpoint_begin(PM_T(t)); }
void piece_checkpoint_commit(piece_tree *t) { pm_checkpoint_commit(PM_T(t)); }
void piece_checkpoint_abort(piece_tree *t) { pm_checkpoint_abort(PM_T(t)); }

piece_err piece_replenish(piece_tree *t) { return pm_replenish(PM_T(t)); }
bool piece_reclaim(piece_tree *t, size_t budget) { return pm_reclaim(PM_T(t), budget); }

/* Lines: a draft under review, not modelled (v2-0fv.7 §9). */
#define PM_LINES_NOT_MODELLED 0
bool piece_lines_before(const piece_view *v, uint64_t off, uint64_t *count)
{
    (void)v, (void)off, (void)count;
    ASSERT(PM_LINES_NOT_MODELLED);
    return false;
}
bool piece_lines_start(const piece_view *v, uint64_t line, uint64_t *off, uint64_t *before)
{
    (void)v, (void)line, (void)off, (void)before;
    ASSERT(PM_LINES_NOT_MODELLED);
    return false;
}
bool piece_lines_count(const piece_backing *b, uint64_t first, uint32_t *counts, size_t n)
{
    (void)b, (void)first, (void)counts, (void)n;
    ASSERT(PM_LINES_NOT_MODELLED);
    return false;
}
void piece_lines_adopt(piece_tree *t, uint64_t first, const uint32_t *counts, size_t n)
{
    (void)t, (void)first, (void)counts, (void)n;
    ASSERT(PM_LINES_NOT_MODELLED);
}
piece_err piece_lines_resolve(piece_tree *t, size_t budget, bool *more)
{
    (void)t, (void)budget, (void)more;
    ASSERT(PM_LINES_NOT_MODELLED);
    return PIECE_ERR_NOMEM;
}

size_t piece_app_id_len(void) { return strlen(os_app_id()); }

/* Test hook (v2-0fv.7 §8): the model has no shape, so it never refuses. */
void piece_test_set_height_limit(unsigned levels);
void piece_test_set_height_limit(unsigned levels) { ASSERT(levels <= PIECE_MAX_HEIGHT); }

#endif /* PIECE_MODEL_IMPL */

#endif
