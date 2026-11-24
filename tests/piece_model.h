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
 * on any thread.
 *
 * Lines: a mapped ORIGINAL's chunk table lives in the core; the model knows
 * a chunk's text once its count is adopted and published by
 * piece_lines_resolve, and private storage always. Knowledge is per chunk,
 * so the model is at least as exact as the contract asks (never less sound):
 * a snapshot keeps the publication it was taken at, every count it reports
 * is recounted from the bytes, and every mapped scan sits between two polls.
 * With records, piece_replace_spans runs the provenance debug check.
 *
 * The reference: pm_* is called directly, through tests/piece_harness.h, by
 * tests/piece_test.c, tests/undo_test.c and fuzz/piece_fuzz.c next to the
 * real tree (v2-0fv.17). Until 7b it also stood in for the tree through a
 * wrapper TU defining every piece.h symbol; 7b deleted that (A24 item 3). */
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

/* A chunk table entry: seq 0 is absent; otherwise the adoption that set it.
 * The UI writes each entry once; snapshot readers on other threads load it. */
typedef struct pm_line_chunk {
    atomic_uint_least32_t count;
    atomic_uint_least64_t seq;
} pm_line_chunk;

typedef struct pm_core {
    atomic_size_t owners; /* the open tree, plus one per live snapshot */
    pm_reserve *reserve;
    const uint8_t *orig;
    uint64_t orig_len;
    bool mapped;
    piece_backing backing;
    pm_line_chunk *table; /* mapped only: one entry per ORIGINAL chunk */
    uint64_t nchunks;
    uint64_t adopt_seq; /* UI only: the last adoption */
    base_arena orig_mem, table_mem, self;
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
    uint64_t lines_seq; /* the publication it was taken at */
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
    uint64_t lines_seq; /* the last adoption piece_lines_resolve published */
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

/* Is chunk k of the mapped ORIGINAL known to view v? Private storage always is. */
static inline bool pm_chunk_known(const pm_view *v, uint64_t k)
{
    const pm_core *c = pm_view_core(v);
    if (!c->mapped) return true;
    uint64_t seq = atomic_load(&c->table[k].seq);
    return seq != 0 && seq <= (v->tree ? v->tree->lines_seq : v->snap->lines_seq);
}

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
    pm_mem_put(&c->table_mem);
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
    c->nchunks = (b->len + PIECE_CHUNK - 1u) / PIECE_CHUNK;
    void *tp;
    if (!pm_mem_get(&c->table_mem, (size_t)c->nchunks * sizeof(pm_line_chunk), &tp)) {
        pm_mem_put(&c->self);
        return PIECE_ERR_NOMEM;
    }
    c->table = tp;
    for (uint64_t k = 0; k < c->nchunks; k++) {
        atomic_init(&c->table[k].count, 0);
        atomic_init(&c->table[k].seq, 0);
    }
    if (pm_tree_new(r, c, out) != PIECE_OK) {
        pm_mem_put(&c->table_mem);
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
 * mapped part is unknown when its chunk is unknown to the view, or when a
 * poll after its scan fails or shows a fault (one closing poll, piece.h FAULTS). */
static inline uint32_t pm_nl_part(const pm_view *v, uint64_t src, uint64_t len)
{
    const pm_core *c = pm_view_core(v);
    uint64_t e1 = 0;
    bool mapped = c->mapped && !pm_is_add(src);
    if (mapped && !pm_chunk_known(v, pm_off(src) / PIECE_CHUNK)) return PIECE_NL_UNKNOWN;
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
    /* The provenance debug check (piece.h): sanitizer lanes only, because it
     * reads every carried end piece, up to 2 x PIECE_CHUNK per span, which
     * the shipped build must not. A poll showing a fault skips it. */
    if (OS_SAN_CHECKS && nl) {
        for (size_t i = 0; i < count; i++) {
            uint64_t o = pm_off(spans[i].src), tag = spans[i].src & PIECE_SPAN_ADD;
            uint64_t ci = o / PIECE_CHUNK, cj = (o + spans[i].len - 1u) / PIECE_CHUNK;
            bool mapped = t->core->mapped && !tag;
            uint64_t e1 = 1;
            uint64_t head_len = ci == cj ? spans[i].len : (ci + 1u) * PIECE_CHUNK - o;
            uint64_t head = pm_count_nl(&t->view, spans[i].src, head_len);
            uint64_t tail = ci == cj ? 0 : pm_count_nl(&t->view, tag | (cj * PIECE_CHUNK), o + spans[i].len - cj * PIECE_CHUNK);
            if (mapped && (os_fault_poll(t->core->backing.fault, &e1) != OS_OK || e1 != 0)) continue;
            OS_SAN_ASSERT(nl[i].head == PIECE_NL_UNKNOWN || nl[i].head == head);
            OS_SAN_ASSERT(nl[i].tail == PIECE_NL_UNKNOWN || nl[i].tail == tail);
        }
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
        const uint8_t *p = NULL; /* set whenever left > 0 (gcc -O2 cannot see it) */
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
    s->lines_seq = t->lines_seq;
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

/* ---- Lines --------------------------------------------------------------- */

/* The one poll reports epoch 0 (mapped only; private storage needs none). */
static inline bool pm_poll_ok(const pm_core *c)
{
    uint64_t e = 1;
    return !c->mapped || (os_fault_poll(c->backing.fault, &e) == OS_OK && e == 0);
}

static inline bool pm_lines_before(const pm_view *v, uint64_t off, uint64_t *count)
{
    ASSERT(count != NULL && off <= pm_length(v));
    const pm_core *c = pm_view_core(v);
    *count = 0;
    if (!pm_poll_ok(c)) return false;
    const pm_text *t = pm_view_text(v);
    uint64_t pos = 0, n = 0;
    bool exact = true;
    for (size_t i = 0; i < t->n && pos < off; i++) {
        piece_span s = t->s[i];
        uint64_t len = pm_min(s.len, off - pos);
        uint64_t o = pm_off(s.src);
        while (len) {
            uint64_t part = pm_is_add(s.src) || !c->mapped ? len : pm_min(len, PIECE_CHUNK - o % PIECE_CHUNK);
            if (pm_is_add(s.src) || pm_chunk_known(v, o / PIECE_CHUNK))
                n += pm_count_nl(v, (s.src & PIECE_SPAN_ADD) | o, part);
            else
                exact = false;
            o += part;
            pos += part;
            len -= part;
        }
    }
    if (!pm_poll_ok(c)) return false;
    *count = n;
    return exact;
}

static inline bool pm_lines_start(const pm_view *v, uint64_t line, uint64_t *off, uint64_t *before)
{
    ASSERT(off != NULL && before != NULL);
    *off = 0;
    *before = 0;
    if (line == 0) return true;
    const pm_core *c = pm_view_core(v);
    if (!pm_poll_ok(c)) return false;
    const pm_text *t = pm_view_text(v);
    uint64_t pos = 0, n = 0, at = UINT64_MAX;
    bool exact = true;
    for (size_t i = 0; i < t->n && at == UINT64_MAX; i++) {
        piece_span s = t->s[i];
        uint64_t len = s.len, o = pm_off(s.src);
        while (len && at == UINT64_MAX) {
            uint64_t part = pm_is_add(s.src) || !c->mapped ? len : pm_min(len, PIECE_CHUNK - o % PIECE_CHUNK);
            if (!pm_is_add(s.src) && !pm_chunk_known(v, o / PIECE_CHUNK)) {
                at = pos; /* the first unknown part blocks: n < line here */
                exact = false;
                break;
            }
            for (uint64_t k = 0; k < part; k++) {
                uint64_t room;
                const uint8_t *p = pm_at(v, (s.src & PIECE_SPAN_ADD) | (o + k), &room);
                if (*p == '\n' && ++n == line) {
                    at = pos + k + 1u;
                    break;
                }
            }
            o += part;
            pos += part;
            len -= part;
        }
    }
    if (!pm_poll_ok(c)) return false;
    *off = at == UINT64_MAX ? pm_length(v) : at;
    *before = n;
    return exact;
}

static inline bool pm_lines_count(const piece_backing *b, uint64_t first, uint32_t *counts, size_t n)
{
    ASSERT(b != NULL && counts != NULL);
    uint64_t chunks = (b->len + PIECE_CHUNK - 1u) / PIECE_CHUNK;
    ASSERT(first <= chunks && n <= chunks - first);
    for (size_t i = 0; i < n; i++) {
        uint64_t lo = (first + i) * PIECE_CHUNK, hi = pm_min(lo + PIECE_CHUNK, b->len);
        uint32_t k = 0;
        for (uint64_t at = lo; at < hi; at++) k += b->bytes[at] == '\n';
        counts[i] = k;
    }
    uint64_t e = 1;
    return os_fault_poll(b->fault, &e) == OS_OK && e == 0;
}

static inline bool pm_lines_adopt(pm_tree *t, os_fault_id source, uint64_t first, const uint32_t *counts, size_t n)
{
    ASSERT(t != NULL && (n == 0 || counts != NULL));
    pm_core *c = t->core;
    ASSERT(c->mapped);
    ASSERT(first <= c->nchunks && n <= c->nchunks - first);
    for (size_t i = 0; i < n; i++) {
        uint64_t lo = (first + i) * PIECE_CHUNK;
        ASSERT(counts[i] <= pm_min(PIECE_CHUNK, c->orig_len - lo));
    }
    if (source.slot != c->backing.fault.slot || source.gen != c->backing.fault.gen) return false;
    if (!pm_poll_ok(c)) return false;
    bool adds = false;
    for (size_t i = 0; i < n; i++) {
        pm_line_chunk *e = &c->table[first + i];
        if (atomic_load(&e->seq) == 0) adds = true;
        else if (atomic_load(&e->count) != counts[i]) return false;
    }
    if (!adds) return true;
    uint64_t seq = ++c->adopt_seq;
    for (size_t i = 0; i < n; i++) {
        pm_line_chunk *e = &c->table[first + i];
        if (atomic_load(&e->seq) != 0) continue;
        atomic_store(&e->count, counts[i]);
        atomic_store(&e->seq, seq);
    }
    return true;
}

/* Publishing new knowledge stands in for the tree's root replacement: it
 * invalidates the snapshot cache and ends the live view's revision. */
static inline piece_err pm_lines_resolve(pm_tree *t, size_t budget, bool *more)
{
    ASSERT(t != NULL && more != NULL);
    (void)budget;
    if (t->lines_seq < t->core->adopt_seq) {
        t->lines_seq = t->core->adopt_seq;
        t->rev++;
        pm_drop_cache(t);
    }
    *more = false;
    return PIECE_OK;
}

#endif
