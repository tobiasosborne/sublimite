/* Persistent 16-way B+ piece tree. See docs/decisions/P1.4-synthesis.md.
 * Compact leaf/branch pools, cursor path reuse, immutable chunked add storage,
 * atomic snapshot lifetime and lazy exact newline counts. No kernel threads. */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include "base/base.h"
#include "scan/scan.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define BPT_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BPT_ASAN 1
#endif
#endif
#ifdef BPT_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n) ASAN_POISON_MEMORY_REGION(p, n)
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION(p, n)
#else
#define POISON(p, n) ((void)0)
#define UNPOISON(p, n) ((void)0)
#endif

#define FAN 16u
#define CH_SHIFT 16
#define CH_SIZE ((uint64_t)1 << CH_SHIFT)
#define CH_MASK (CH_SIZE - 1)
#define TOP (1ull << 63)
#define UNK TOP
#define LEAF_SZ 272u
#define BRANCH_SZ 400u
#define MAXH 32

typedef struct node node;
struct node {
    atomic_uint rc;
    uint16_t cnt, leaf;
    node *nextfree; /* only used after the last owner releases the node */
    union {
        struct { uint32_t b[FAN], nl[FAN]; uint64_t x[FAN]; } l;
        struct { uint64_t b[FAN], nl[FAN]; node *ch[FAN]; } in;
    } v;
};
_Static_assert(offsetof(node, v) + sizeof(((node *)0)->v.l) == LEAF_SZ, "leaf size");
_Static_assert(sizeof(node) == BRANCH_SZ, "branch size");
typedef struct node_pool {
    node *fl; size_t nfree; _Atomic(node *) remote; atomic_size_t returned;
    void *slabs; size_t nslabs;
} node_pool;

typedef struct { uint64_t b, nl, x; } ent;

typedef struct retired { uint8_t **p; size_t cap; } retired;

typedef struct core {
    atomic_uint rc;
    piece_allocator a;
    piece_map_hooks mh; int has_mh;
    const uint8_t *orig; size_t orig_len; int orig_owned;
    _Atomic(uint8_t **) tbl; size_t tblcap, nch;
    retired ret[24]; int nret;
    node_pool pool[2]; /* leaf/snapshot and branch, owner allocates; any thread returns */
#ifdef PIECE_TESTING
    piece_test_stats stats;
#endif
} core;

struct piece_tree {
    core *c; node *root; int height; piece_snapshot *cached_snapshot;
    uint64_t add_len; int inited, mapped;
    int run_valid; uint64_t run_end, run_addend;
    uint64_t len;
    node *cursor[MAXH]; unsigned cursor_i[MAXH];
    unsigned cursor_depth, cursor_slot; uint64_t cursor_start; int cursor_valid;
};

struct piece_snapshot { atomic_uint rc; core *c; node *root; uint64_t len; int has_mh; };
_Static_assert(sizeof(struct piece_snapshot) <= LEAF_SZ, "snapshot header fits a node block");

static void *dm(void *x, size_t s) { (void)x; return malloc(s); }
static void df(void *x, void *p, size_t s) { (void)x; (void)s; free(p); }
piece_allocator piece_default_allocator(void) { piece_allocator a = { NULL, dm, df }; return a; }

/* ------------------------------------------------------------------ pool */
static inline uint64_t bytes(const node *n, unsigned i) { return n->leaf ? n->v.l.b[i] : n->v.in.b[i]; }
static inline void bytes_set(node *n, unsigned i, uint64_t v) {
    if (n->leaf) { EDIT_ASSERT(v <= CH_SIZE); n->v.l.b[i] = (uint32_t)v; }
    else n->v.in.b[i] = v;
}
static inline uint64_t nl_ld(const node *n, unsigned i) {
    if (!n->leaf) return __atomic_load_n(&n->v.in.nl[i], __ATOMIC_RELAXED);
    uint32_t v = __atomic_load_n(&n->v.l.nl[i], __ATOMIC_RELAXED);
    return (uint64_t)(v & UINT32_C(0x7fffffff)) | ((v & UINT32_C(0x80000000)) ? UNK : 0);
}
static inline void nl_st(node *n, unsigned i, uint64_t v) {
    if (!n->leaf) __atomic_store_n(&n->v.in.nl[i], v, __ATOMIC_RELAXED);
    else __atomic_store_n(&n->v.l.nl[i], (uint32_t)(v & ~UNK) | ((v & UNK) ? UINT32_C(0x80000000) : 0), __ATOMIC_RELAXED);
}
static inline uint64_t entry_x(const node *n, unsigned i) {
    return n->leaf ? n->v.l.x[i] : (uint64_t)(uintptr_t)n->v.in.ch[i];
}
static inline void entry_x_set(node *n, unsigned i, uint64_t x) {
    if (n->leaf) n->v.l.x[i] = x; else n->v.in.ch[i] = (node *)(uintptr_t)x;
}
static size_t slab_nodes(size_t k) { (void)k; return 4; }
static size_t slot_size(unsigned p) { return p == 0 ? LEAF_SZ : BRANCH_SZ; }
static int pool_slab(core *c, unsigned pi) {
    node_pool *p = &c->pool[pi]; size_t nn = slab_nodes(p->nslabs), slot = slot_size(pi);
    size_t sz = nn * slot + 16;
    uint8_t *raw = c->a.alloc(c->a.ctx, sz);
    if (!raw) return PIECE_ERR_NOMEM;
    *(void **)(void *)(raw + sz - 16) = p->slabs;
    p->slabs = raw; p->nslabs++;
    for (size_t i = nn; i-- > 0;) {
        node *n = (node *)(void *)(raw + i * slot);
        atomic_init(&n->rc, 0); n->nextfree = p->fl; p->fl = n; POISON((uint8_t *)n + 16, slot - 16);
    }
    p->nfree += nn; return 0;
}
static void pool_drain(core *c, unsigned pi) {
    node_pool *p = &c->pool[pi];
    node *h = atomic_exchange_explicit(&p->remote, NULL, memory_order_acquire);
    while (h) { node *nx = h->nextfree; h->nextfree = p->fl; p->fl = h; p->nfree++; h = nx; }
}
static int pool_reserve_class(core *c, unsigned pi, size_t n) {
    node_pool *p = &c->pool[pi];
    if (p->nfree >= n) return 0;
    pool_drain(c, pi);
    while (p->nfree < n) { int r = pool_slab(c, pi); if (r) return r; }
    return 0;
}
static node *pool_get(core *c, unsigned pi) {
    node_pool *p = &c->pool[pi];
    EDIT_ASSERT(p->fl); /* every mutation reserves before changing content */
    node *n = p->fl; p->fl = n->nextfree; p->nfree--;
    UNPOISON((uint8_t *)n + 16, slot_size(pi) - 16);
    return n;
}
static void pool_free(core *c, node *n, unsigned pi) { /* any thread */
    node_pool *p = &c->pool[pi];
    atomic_store_explicit(&n->rc, 0, memory_order_relaxed);
    POISON((uint8_t *)n + 16, slot_size(pi) - 16);
    node *h = atomic_load_explicit(&p->remote, memory_order_relaxed);
    do { n->nextfree = h; } while (!atomic_compare_exchange_weak_explicit(&p->remote, &h, n, memory_order_release, memory_order_relaxed));
    atomic_fetch_add_explicit(&p->returned, 1, memory_order_relaxed);
}
static node *node_new(core *c, int leaf) {
    node *n = pool_get(c, leaf ? 0u : 1u);
    atomic_init(&n->rc, 1); n->cnt = 0; n->leaf = (uint16_t)leaf;
    return n;
}
/* Reclaim wholly free slabs on the owner after a burst of releases. The core
 * reference is dropped after the remote push, so rc==1 also synchronizes with
 * the last snapshot releaser. No producer can then touch these free lists. */
static void pool_trim(core *c, int force) {
    if (atomic_load_explicit(&c->rc, memory_order_acquire) != 1) return;
    if (!force && atomic_load_explicit(&c->pool[0].returned, memory_order_relaxed) < 64 &&
        atomic_load_explicit(&c->pool[1].returned, memory_order_relaxed) < 64) return;
    for (unsigned pi = 0; pi < 2; pi++) {
        node_pool *p = &c->pool[pi]; void *slab = p->slabs;
        p->fl = NULL; p->nfree = 0; p->slabs = NULL; p->nslabs = 0;
        atomic_store_explicit(&p->remote, NULL, memory_order_relaxed);
        atomic_store_explicit(&p->returned, 0, memory_order_relaxed);
        size_t slot = slot_size(pi), nn = slab_nodes(0), sz = nn * slot + 16;
        while (slab) {
            uint8_t *raw = slab; void **tail = (void **)(void *)(raw + sz - 16); void *next = tail[0];
            int live = 0;
            for (size_t i = 0; i < nn; i++) {
                node *n = (node *)(void *)(raw + i * slot);
                if (atomic_load_explicit(&n->rc, memory_order_relaxed)) live = 1;
            }
            if (!live) { UNPOISON(raw, sz); c->a.free(c->a.ctx, raw, sz); }
            else {
                tail[0] = p->slabs; p->slabs = raw; p->nslabs++;
                for (size_t i = 0; i < nn; i++) {
                    node *n = (node *)(void *)(raw + i * slot);
                    if (!atomic_load_explicit(&n->rc, memory_order_relaxed)) {
                        n->nextfree = p->fl; p->fl = n; p->nfree++;
                    }
                }
            }
            slab = next;
        }
    }
}
/* A leaf root needs only its own possible COW/split. At greater heights a
 * delete visits two boundary paths and may own their merge neighbours. Small
 * fixed slabs keep these reservations inside the tiny-buffer G10f allowance. */
static int reserve_delete(piece_tree *t, int snapshot) {
    core *c = t->c; size_t h = (size_t)t->height;
    size_t leaves = h ? 4 : 2;
    size_t branches = h == 0 ? (t->root->cnt == FAN ? 1u : 0u) :
                      h == 1 ? (t->root->cnt == FAN ? 3u : 1u) : 4 * h - 1;
    int r = pool_reserve_class(c, 0, leaves + (snapshot ? 1u : 0u));
    return r ? r : pool_reserve_class(c, 1, branches);
}
static int reserve_insert(piece_tree *t, size_t np) {
    core *c = t->c; size_t h = (size_t)t->height, leaves, branches;
    if (!h && np <= (FAN - t->root->cnt) / 2u) {
        leaves = atomic_load_explicit(&t->root->rc, memory_order_acquire) > 1 ? 1u : 0u;
        branches = 0;
    } else {
        leaves = 2 * np + 1;
        branches = !h ? np + 1 : h == 1 && np == 1 ? (t->root->cnt == FAN ? 3u : 1u) : (2 * h + 3) * np + 1;
    }
    int r = pool_reserve_class(c, 0, leaves);
    return r ? r : pool_reserve_class(c, 1, branches);
}

static void node_unref(core *c, node *n) {
    if (atomic_fetch_sub_explicit(&n->rc, 1, memory_order_acq_rel) != 1) return;
    if (!n->leaf) for (unsigned i = 0; i < n->cnt; i++) node_unref(c, n->v.in.ch[i]);
    pool_free(c, n, n->leaf ? 0u : 1u);
}
static node *cow(core *c, node *n) {
    if (atomic_load_explicit(&n->rc, memory_order_acquire) == 1) return n;
    node *m = node_new(c, (int)n->leaf);
    m->cnt = n->cnt;
#ifdef PIECE_TESTING
    c->stats.pathcopy_bytes += n->leaf ? LEAF_SZ : BRANCH_SZ;
#endif
    for (unsigned i = 0; i < n->cnt; i++) {
        bytes_set(m, i, bytes(n, i)); nl_st(m, i, nl_ld(n, i)); entry_x_set(m, i, entry_x(n, i));
    }
    if (!n->leaf) for (unsigned i = 0; i < n->cnt; i++) atomic_fetch_add_explicit(&n->v.in.ch[i]->rc, 1, memory_order_relaxed);
    node_unref(c, n);
    return m;
}

/* ------------------------------------------------------------ core / add */
static void core_unref(core *c) {
    if (atomic_fetch_sub_explicit(&c->rc, 1, memory_order_acq_rel) != 1) return;
    piece_allocator a = c->a;
    uint8_t **tbl = atomic_load(&c->tbl);
    for (size_t i = 0; i < c->nch; i++) a.free(a.ctx, tbl[i], CH_SIZE);
    if (tbl) a.free(a.ctx, tbl, c->tblcap * sizeof *tbl);
    for (int i = 0; i < c->nret; i++) a.free(a.ctx, c->ret[i].p, c->ret[i].cap * sizeof(uint8_t *));
    if (c->orig_owned && c->orig_len) a.free(a.ctx, (void *)(uintptr_t)c->orig, c->orig_len);
    for (unsigned pi = 0; pi < 2; pi++) {
        node_pool *p = &c->pool[pi]; void *slab = p->slabs;
        for (size_t k = p->nslabs; k-- > 0;) {
            size_t sz = slab_nodes(k) * slot_size(pi) + 16;
            void *nx = *(void **)(void *)((uint8_t *)slab + sz - 16);
            UNPOISON(slab, sz); a.free(a.ctx, slab, sz); slab = nx;
        }
    }
    a.free(a.ctx, c, sizeof *c);
}

/* An unchanged tree shares one immutable snapshot header. Each public owner
 * still gets its own mapping acquire/release; the tree's cache is covered by
 * the tree's mapping owner. This bounds repeated takes without any edits. */
static void snapshot_drop(piece_snapshot *s, int external) {
    if (!s) return;
    core *c = s->c;
    if (external && s->has_mh) c->mh.release(c->mh.ctx);
    if (atomic_fetch_sub_explicit(&s->rc, 1, memory_order_acq_rel) != 1) return;
    node_unref(c, s->root); pool_free(c, (node *)(void *)s, 0); core_unref(c);
}
static void snapshot_uncache(piece_tree *t) {
    piece_snapshot *s = t->cached_snapshot; t->cached_snapshot = NULL;
    snapshot_drop(s, 0);
}

static void owner_trim(piece_tree *t, int force) {
    core *c = t->c;
    if (!force && atomic_load_explicit(&c->pool[0].returned, memory_order_relaxed) < 64 &&
        atomic_load_explicit(&c->pool[1].returned, memory_order_relaxed) < 64) return;
    if (t->cached_snapshot && atomic_load_explicit(&t->cached_snapshot->rc, memory_order_acquire) == 1)
        snapshot_uncache(t);
    pool_trim(c, force);
}

static inline const uint8_t *ent_data(const core *c, uint64_t x) {
    if (x & TOP) {
        uint64_t a = x & ~TOP;
        uint8_t **tbl = atomic_load_explicit((_Atomic(uint8_t **) *)(uintptr_t)&c->tbl, memory_order_acquire);
        return tbl[a >> CH_SHIFT] + (a & CH_MASK);
    }
    return c->orig + x;
}
static inline uint8_t *add_ptr(const core *c, uint64_t a) { return (uint8_t *)(uintptr_t)ent_data(c, a | TOP); }

static int add_reserve(piece_tree *t, uint64_t n) {
    core *c = t->c;
    if (n >= TOP || t->add_len > TOP - 1 - n) return PIECE_ERR_NOMEM;
    uint64_t need = (t->add_len + n + CH_MASK) >> CH_SHIFT;
    while (c->nch < need) {
        uint8_t **tbl = atomic_load(&c->tbl);
        if (c->nch == c->tblcap) {
            size_t nc = c->tblcap ? c->tblcap * 2 : 8;
            if (c->nret >= 24) return PIECE_ERR_NOMEM;
            uint8_t **nt = c->a.alloc(c->a.ctx, nc * sizeof *nt);
            if (!nt) return PIECE_ERR_NOMEM;
            if (c->nch) memcpy(nt, tbl, c->nch * sizeof *nt);
            atomic_store_explicit(&c->tbl, nt, memory_order_release);
            if (tbl) { c->ret[c->nret].p = tbl; c->ret[c->nret].cap = c->tblcap; c->nret++; }
            c->tblcap = nc; tbl = nt;
        }
        uint8_t *ch = c->a.alloc(c->a.ctx, CH_SIZE);
        if (!ch) return PIECE_ERR_NOMEM;
        tbl[c->nch++] = ch;
    }
    return 0;
}
/* append n bytes (space must be reserved) */
static void add_append(piece_tree *t, const uint8_t *p, size_t n) {
    while (n) {
        size_t room = (size_t)(CH_SIZE - (t->add_len & CH_MASK)), k = n < room ? n : room;
        memcpy(add_ptr(t->c, t->add_len), p, k);
        t->add_len += k; p += k; n -= k;
    }
}

static inline uint64_t nl_count(const uint8_t *p, size_t n) {
    if (n < 48) { uint64_t r = 0; for (size_t i = 0; i < n; i++) r += p[i] == '\n'; return r; }
    return scan_count(p, n).newlines;
}

/* ------------------------------------------------------------- entries */
static inline ent ent_get(const node *n, unsigned i) { ent e = { bytes(n, i), nl_ld(n, i), entry_x(n, i) }; return e; }
static inline void ent_put(node *n, unsigned i, const ent *e) { bytes_set(n, i, e->b); nl_st(n, i, e->nl); entry_x_set(n, i, e->x); }
static ent summ(const node *n, node *self) {
    ent e = { 0, 0, (uint64_t)(uintptr_t)self }; uint64_t unk = 0;
    for (unsigned i = 0; i < n->cnt; i++) { e.b += bytes(n, i); uint64_t v = nl_ld(n, i); unk |= v & UNK; e.nl += v & ~UNK; }
    e.nl |= unk;
    return e;
}
static void ent_split(const core *c, const ent *e, uint64_t s, ent *L, ent *R) {
    L->b = s; R->b = e->b - s; L->x = e->x; R->x = e->x + s;
    if (e->nl & UNK) { L->nl = R->nl = UNK; return; }
    if (!e->nl) { L->nl = R->nl = 0; return; }
    const uint8_t *p = ent_data(c, e->x);
    if (s <= e->b - s) { uint64_t l = nl_count(p, (size_t)s); L->nl = l; R->nl = e->nl - l; }
    else { uint64_t r = nl_count(p + s, (size_t)(e->b - s)); R->nl = r; L->nl = e->nl - r; }
}
static uint64_t ent_nl(const core *c, node *n, unsigned i);

/* exact newline count of entry i, resolving lazily; safe on shared nodes (relaxed atomics, idempotent) */
static uint64_t ent_nl(const core *c, node *n, unsigned i) {
    uint64_t v = nl_ld(n, i);
    if (!(v & UNK)) return v;
    uint64_t sum = 0;
    if (n->leaf) sum = scan_count(ent_data(c, entry_x(n, i)), (size_t)bytes(n, i)).newlines;
    else {
        node *ch = n->v.in.ch[i];
        for (unsigned j = 0; j < ch->cnt; j++) sum += ent_nl(c, ch, j);
    }
    nl_st(n, i, sum);
    return sum;
}

/* replace entries [i, i+ndel) by ins[0..nins); split into a new right sibling when > FAN */
static node *splice(core *c, node *n, unsigned i, unsigned ndel, const ent *ins, unsigned nins) {
    unsigned total = (unsigned)n->cnt - ndel + nins;
    if (total <= FAN) {
        size_t tail = (size_t)n->cnt - i - ndel;
        if (ndel != nins && tail) {
            if (n->leaf) {
                memmove(n->v.l.b + i + nins, n->v.l.b + i + ndel, tail * sizeof(uint32_t));
                memmove(n->v.l.nl + i + nins, n->v.l.nl + i + ndel, tail * sizeof(uint32_t));
                memmove(n->v.l.x + i + nins, n->v.l.x + i + ndel, tail * sizeof(uint64_t));
            } else {
                memmove(n->v.in.b + i + nins, n->v.in.b + i + ndel, tail * sizeof(uint64_t));
                memmove(n->v.in.nl + i + nins, n->v.in.nl + i + ndel, tail * sizeof(uint64_t));
                memmove(n->v.in.ch + i + nins, n->v.in.ch + i + ndel, tail * sizeof(node *));
            }
        }
        for (unsigned j = 0; j < nins; j++) ent_put(n, i + j, &ins[j]);
        n->cnt = (uint16_t)total; return NULL;
    }
    ent tmp[FAN + 4]; unsigned m = 0;
    for (unsigned j = 0; j < i; j++) tmp[m++] = ent_get(n, j);
    for (unsigned j = 0; j < nins; j++) tmp[m++] = ins[j];
    for (unsigned j = i + ndel; j < n->cnt; j++) tmp[m++] = ent_get(n, j);
    if (m <= FAN) { for (unsigned j = 0; j < m; j++) ent_put(n, j, &tmp[j]); n->cnt = (uint16_t)m; return NULL; }
    node *r = node_new(c, (int)n->leaf);
    unsigned l = (m + 1) / 2;
    for (unsigned j = 0; j < l; j++) ent_put(n, j, &tmp[j]);
    for (unsigned j = l; j < m; j++) ent_put(r, j - l, &tmp[j]);
    n->cnt = (uint16_t)l; r->cnt = (uint16_t)(m - l);
    return r;
}

/* --------------------------------------------------------------- insert */
static node *ins_rec(core *c, node *n, uint64_t off, const ent *ne) {
    if (n->leaf) {
        unsigned k = 0; uint64_t cum = 0; ent rep[3]; unsigned nr, ndel = 0, at;
        for (; k < n->cnt; k++) { if (off <= cum + bytes(n, k)) break; cum += bytes(n, k); }
        if (k == n->cnt || off == cum + bytes(n, k)) { at = k == n->cnt ? k : k + 1; rep[0] = *ne; nr = 1; }
        else if (off == cum) { at = k; rep[0] = *ne; nr = 1; }
        else {
            ent e = ent_get(n, k), L, R; ent_split(c, &e, off - cum, &L, &R);
            rep[0] = L; rep[1] = *ne; rep[2] = R; nr = 3; ndel = 1; at = k;
        }
        return splice(c, n, at, ndel, rep, nr);
    }
    unsigned i = 0; uint64_t cum = 0;
    while (i + 1 < n->cnt && off > cum + bytes(n, i)) { cum += bytes(n, i); i++; }
    node *ch = cow(c, n->v.in.ch[i]); n->v.in.ch[i] = ch;
    node *sib = ins_rec(c, ch, off - cum, ne);
    ent e[2]; e[0] = summ(ch, ch);
    if (!sib) { ent_put(n, i, &e[0]); return NULL; }
    e[1] = summ(sib, sib);
    return splice(c, n, i, 1, e, 2);
}
static void root_grow(piece_tree *t, node *sib) {
    core *c = t->c; node *r = node_new(c, 0); ent e[2];
    e[0] = summ(t->root, t->root); e[1] = summ(sib, sib);
    ent_put(r, 0, &e[0]); ent_put(r, 1, &e[1]); r->cnt = 2;
    t->root = r; t->height++;
}
static void ins_piece(piece_tree *t, uint64_t off, const ent *ne) {
    t->cursor_valid = 0;
#ifdef PIECE_TESTING
    t->c->stats.root_descents++;
#endif
    t->root = cow(t->c, t->root);
    node *sib = ins_rec(t->c, t->root, off, ne);
    if (sib) root_grow(t, sib);
}
/* The cursor cache borrows flat's bounded local lookup, retaining the B+ path
 * as well as the leaf. It is usable only while that entire path is exclusive.
 * Structural changes elsewhere and snapshot_take invalidate it. */
static void cursor_seek(piece_tree *t, uint64_t pos) {
    node *n = t->root; uint64_t base = 0; unsigned depth = 0;
    t->cursor_valid = 0;
#ifdef PIECE_TESTING
    t->c->stats.root_descents++;
#endif
    for (;;) {
        if (atomic_load_explicit(&n->rc, memory_order_acquire) != 1) return;
        t->cursor[depth] = n;
        if (n->leaf) break;
        unsigned i = 0;
        while (i + 1 < n->cnt && pos > base + bytes(n, i)) { base += bytes(n, i); i++; }
        t->cursor_i[depth] = i;
        EDIT_ASSERT(depth + 1 < MAXH); depth++; n = n->v.in.ch[i];
    }
    if (!n->cnt) return;
    unsigned k = 0; uint64_t start = base;
    while (k + 1 < n->cnt && pos > start + bytes(n, k)) { start += bytes(n, k); k++; }
    t->cursor_depth = depth; t->cursor_slot = k; t->cursor_start = start; t->cursor_valid = 1;
}
static void cursor_delta(piece_tree *t, uint64_t b, uint64_t nl, int remove) {
    for (unsigned d = 0; d < t->cursor_depth; d++) {
        node *n = t->cursor[d]; unsigned i = t->cursor_i[d];
        bytes_set(n, i, remove ? bytes(n, i) - b : bytes(n, i) + b);
        nl_st(n, i, remove ? nl_ld(n, i) - nl : nl_ld(n, i) + nl);
    }
}
static void cursor_relocate(piece_tree *t, uint64_t pos, uint64_t start) {
    node *leaf = t->cursor[t->cursor_depth];
    unsigned k = 0;
    while (k + 1 < leaf->cnt && pos > start + bytes(leaf, k)) { start += bytes(leaf, k); k++; }
    t->cursor_slot = k; t->cursor_start = start;
    if (!leaf->cnt) t->cursor_valid = 0;
}
static int cursor_extend(piece_tree *t, uint64_t off, const uint8_t *data, size_t len) {
    if (!t->cursor_valid || !t->run_valid || off != t->run_end || t->run_addend != t->add_len ||
        !(t->add_len & CH_MASK) || len > CH_SIZE - (t->add_len & CH_MASK)) return 0;
    node *leaf = t->cursor[t->cursor_depth]; unsigned k = t->cursor_slot;
    EDIT_ASSERT(t->cursor_start + bytes(leaf, k) == off);
    EDIT_ASSERT(entry_x(leaf, k) & TOP);
    uint64_t nl;
    if (len == 1) { nl = data[0] == '\n'; *add_ptr(t->c, t->add_len) = data[0]; }
    else { nl = nl_count(data, len); memcpy(add_ptr(t->c, t->add_len), data, len); }
    leaf->v.l.b[k] += (uint32_t)len;
    if (nl) nl_st(leaf, k, nl_ld(leaf, k) + nl);
    for (unsigned d = 0; d < t->cursor_depth; d++) {
        node *n = t->cursor[d]; unsigned i = t->cursor_i[d];
        n->v.in.b[i] += len;
        if (nl) nl_st(n, i, nl_ld(n, i) + nl);
    }
    t->inited = 1; t->len += len; t->add_len += len; t->run_end += len; t->run_addend += len;
#ifdef PIECE_TESTING
    t->c->stats.cursor_hits++;
#endif
    return 1;
}
/* Bounded leaf-local insertion after a backspace (the API breaks append runs
 * on every delete, so this must make a new piece rather than extend the old one). */
static int cursor_insert(piece_tree *t, uint64_t pos, const ent *e) {
    if (!t->cursor_valid) return 0;
    node *leaf = t->cursor[t->cursor_depth]; unsigned k = t->cursor_slot;
    if (pos == t->cursor_start + bytes(leaf, k) && leaf->cnt < FAN) {
        node *sib = splice(t->c, leaf, k + 1, 0, e, 1); EDIT_ASSERT(!sib);
        t->cursor_slot = k + 1; t->cursor_start = pos; cursor_delta(t, e->b, e->nl, 0);
    } else {
        uint64_t base = t->cursor_start, total = 0;
        for (unsigned i = 0; i < k; i++) base -= bytes(leaf, i);
        for (unsigned i = 0; i < leaf->cnt; i++) total += bytes(leaf, i);
        if (pos < base || pos > base + total || leaf->cnt + 2u > FAN) return 0;
        node *sib = ins_rec(t->c, leaf, pos - base, e); EDIT_ASSERT(!sib);
        cursor_delta(t, e->b, e->nl, 0); cursor_relocate(t, pos + e->b, base);
    }
#ifdef PIECE_TESTING
    t->c->stats.cursor_hits++;
#endif
    return 1;
}

/* --------------------------------------------------------------- delete */
static void merge_children(core *c, node *n, unsigned j) {   /* merge child j+1 into child j */
    node *a = cow(c, n->v.in.ch[j]); n->v.in.ch[j] = a;
    node *b = n->v.in.ch[j + 1];
    for (unsigned k = 0; k < b->cnt; k++) {
        ent e = ent_get(b, k); ent_put(a, a->cnt + k, &e);
        if (!b->leaf) atomic_fetch_add_explicit(&b->v.in.ch[k]->rc, 1, memory_order_relaxed);
    }
    a->cnt = (uint16_t)(a->cnt + b->cnt);
    node_unref(c, b);
    for (unsigned k = j + 1; k + 1 < n->cnt; k++) { ent e = ent_get(n, k + 1); ent_put(n, k, &e); }
    n->cnt--;
    ent e = summ(a, a); ent_put(n, j, &e);
}
static void maybe_merge(core *c, node *n, unsigned idx) {
    if (n->cnt < 2 || idx >= n->cnt) return;
    node *ch = n->v.in.ch[idx];
    if (ch->cnt >= 8) return;
    if (idx + 1 < n->cnt && ch->cnt + n->v.in.ch[idx + 1]->cnt <= 12) merge_children(c, n, idx);
    else if (idx > 0 && n->v.in.ch[idx - 1]->cnt + ch->cnt <= 12) merge_children(c, n, idx - 1);
}
static node *del_rec(core *c, node *n, uint64_t lo, uint64_t hi) {
    if (n->leaf) {
        unsigned k0 = 0, k1 = 0, k; uint64_t cum = 0, st0 = 0, st1 = 0, en1 = 0; int f0 = 0;
        for (k = 0; k < n->cnt; k++) {
            uint64_t e = cum + bytes(n, k);
            if (!f0 && lo < e) { k0 = k; st0 = cum; f0 = 1; }
            if (hi <= e) { k1 = k; st1 = cum; en1 = e; break; }
            cum = e;
        }
        ent rep[2] = {{0}}; unsigned nr = 0;
        ent e0 = ent_get(n, k0), e1 = ent_get(n, k1);
        if (k0 == k1) {
            ent L, R, M, R2; int hl = lo > st0, hr = hi < en1;
            if (hl && hr) {
                ent_split(c, &e0, lo - st0, &L, &M);          /* M = [lo, end) */
                ent_split(c, &M, hi - lo, &R2, &R);           /* R = [hi, end) */
                rep[nr++] = L; rep[nr++] = R;
            } else if (hl) { ent_split(c, &e0, lo - st0, &L, &M); rep[nr++] = L; }
            else if (hr) { ent_split(c, &e0, hi - st0, &M, &R); rep[nr++] = R; }
        } else {
            ent L, M, R, M2;
            if (lo > st0) { ent_split(c, &e0, lo - st0, &L, &M); rep[nr++] = L; }
            if (hi < en1) { ent_split(c, &e1, hi - st1, &M2, &R); rep[nr++] = R; }
        }
        return splice(c, n, k0, k1 - k0 + 1, rep, nr);
    }
    unsigned i0 = 0, i1 = 0, i; uint64_t cum = 0, c0 = 0, c1 = 0; int f0 = 0;
    for (i = 0; i < n->cnt; i++) {
        uint64_t e = cum + bytes(n, i);
        if (!f0 && lo < e) { i0 = i; c0 = cum; f0 = 1; }
        if (hi <= e) { i1 = i; c1 = cum; break; }
        cum = e;
    }
    ent rep[2] = {{0}}; unsigned nr = 0; node *csib = NULL;
    if (i0 == i1) {
        node *ch = n->v.in.ch[i0];
        if (lo == c0 && hi == c0 + bytes(n, i0)) node_unref(c, ch);
        else {
            ch = cow(c, ch); n->v.in.ch[i0] = ch;
            csib = del_rec(c, ch, lo - c0, hi - c0);
            rep[nr++] = summ(ch, ch);
            if (csib) rep[nr++] = summ(csib, csib);
        }
    } else {
        node *l = n->v.in.ch[i0];
        if (lo == c0) node_unref(c, l);
        else { l = cow(c, l); node *s = del_rec(c, l, lo - c0, bytes(n, i0)); (void)s; rep[nr++] = summ(l, l); }
        for (unsigned m = i0 + 1; m < i1; m++) node_unref(c, n->v.in.ch[m]);
        node *r = n->v.in.ch[i1];
        if (hi == c1 + bytes(n, i1)) node_unref(c, r);
        else { r = cow(c, r); node *s = del_rec(c, r, 0, hi - c1); (void)s; rep[nr++] = summ(r, r); }
    }
    node *sib = splice(c, n, i0, i1 - i0 + 1, rep, nr);
    if (!sib && nr) {
        maybe_merge(c, n, i0 + nr - 1);
        if (nr == 2) maybe_merge(c, n, i0);
    }
    return sib;
}

/* ----------------------------------------------------------------- walk */
typedef struct { node *leaf; uint64_t leaf_start, k, k_start; } walk;
static void walk_seek(walk *w, const node *root, uint64_t pos) {
    const node *n = root; uint64_t base = 0;
    while (!n->leaf) {
        unsigned i = 0;
        while (i + 1 < n->cnt && pos >= base + bytes(n, i)) { base += bytes(n, i); i++; }
        n = n->v.in.ch[i];
    }
    unsigned k = 0; uint64_t ks = base;
    while (k + 1 < n->cnt && pos >= ks + bytes(n, k)) { ks += bytes(n, k); k++; }
    w->leaf = (node *)(uintptr_t)n; w->k = k; w->k_start = ks;
    /* leaf_start: start of the leaf = base after loop above (recompute) */
    w->leaf_start = base;
}
/* next entry fragment at *pos; returns 0 at end */
static int walk_next(walk *w, const node *root, uint64_t total, uint64_t *pos, uint64_t *x, uint64_t *skip, uint64_t *n) {
    if (!w->leaf || w->k >= w->leaf->cnt) {
        if (*pos >= total) return 0;
        walk_seek(w, root, *pos);
    }
    node *l = w->leaf; unsigned k = (unsigned)w->k;
    *x = entry_x(l, k); *skip = *pos - w->k_start; *n = bytes(l, k) - *skip;
    *pos += *n; w->k_start += bytes(l, k); w->k++;
    return 1;
}
static uint64_t node_total(const node *n) { uint64_t s = 0; for (unsigned i = 0; i < n->cnt; i++) s += bytes(n, i); return s; }

static int read_range(const core *c, const node *root, uint64_t total, uint64_t off, uint8_t *dst, size_t len) {
    if (off > total || len > total - off) return PIECE_ERR_RANGE;
    walk w = { 0, 0, 0, 0 }; uint64_t pos = off, end = off + len, x, skip, n;
    while (pos < end && walk_next(&w, root, total, &pos, &x, &skip, &n)) {
        uint64_t take = n < end - (pos - n) ? n : end - (pos - n);
        memcpy(dst, ent_data(c, x) + skip, (size_t)take); dst += take;
        if (take < n) break;
    }
    return 0;
}

/* ------------------------------------------------------------ line maps */
static uint64_t tree_nl(const core *c, node *root) {
    uint64_t s = 0; for (unsigned i = 0; i < root->cnt; i++) s += ent_nl(c, root, i);
    return s;
}
/* Descend by newline count. Flagged (unresolved) internal entries are entered instead of resolved
 * up front, so only the prefix before the target is ever scanned; a fully consumed subtree gets its
 * exact count written back. Returns 1 and sets *res when the target lies inside n. */
static int l2b_rec(const core *c, node *n, uint64_t *need, uint64_t *base, uint64_t *res) {
    for (unsigned i = 0; i < n->cnt; i++) {
        uint64_t v = nl_ld(n, i);
        if (n->leaf) {
            if (v & UNK) v = ent_nl(c, n, i);
            if (v < *need) { *need -= v; *base += bytes(n, i); continue; }
            const uint8_t *d = ent_data(c, entry_x(n, i));
            const uint8_t *p = scan_find_nth_newline(d, (size_t)bytes(n, i), *need - 1);
            if (!p) return 0;
            *res = *base + (uint64_t)(p - d) + 1;
            return 1;
        }
        if (!(v & UNK) && v < *need) { *need -= v; *base += bytes(n, i); continue; }
        uint64_t need0 = *need;
        if (l2b_rec(c, n->v.in.ch[i], need, base, res)) return 1;
        nl_st(n, i, need0 - *need);               /* consumed whole: exact */
    }
    return 0;
}
static uint64_t l2b(const core *c, node *root, uint64_t total, uint64_t line) {
    if (line == 0) return 0;
    uint64_t need = line, base = 0, res = 0;
    return l2b_rec(c, root, &need, &base, &res) ? res : total;
}
static uint64_t b2l(const core *c, node *root, uint64_t total, uint64_t off) {
    if (off >= total) return tree_nl(c, root);
    uint64_t acc = 0; node *n = root;
    for (;;) {
        unsigned i = 0;
        while (i + 1 < n->cnt && off >= bytes(n, i)) { acc += ent_nl(c, n, i); off -= bytes(n, i); i++; }
        if (n->leaf) {
            const uint8_t *p = ent_data(c, entry_x(n, i));
            if (off == 0) return acc;
            if (!(nl_ld(n, i) & UNK) && off > bytes(n, i) / 2)
                return acc + nl_ld(n, i) - nl_count(p + off, (size_t)(bytes(n, i) - off));
            return acc + nl_count(p, (size_t)off);
        }
        n = n->v.in.ch[i];
    }
}

/* -------------------------------------------------------------- lifetime */
piece_tree *piece_create(const piece_allocator *a) {
    core *c = a->alloc(a->ctx, sizeof *c);
    if (!c) return NULL;
    memset(c, 0, sizeof *c); atomic_init(&c->rc, 1); c->a = *a;
    atomic_init(&c->tbl, NULL);
    for (unsigned pi = 0; pi < 2; pi++) { atomic_init(&c->pool[pi].remote, NULL); atomic_init(&c->pool[pi].returned, 0); }
    piece_tree *t = a->alloc(a->ctx, sizeof *t);
    if (!t) { core_unref(c); return NULL; }
    memset(t, 0, sizeof *t); t->c = c;
    if (pool_reserve_class(c, 0, 1) || !(t->root = node_new(c, 1))) { a->free(a->ctx, t, sizeof *t); core_unref(c); return NULL; }
    return t;
}
void piece_destroy(piece_tree *t) {
    if (!t) return;
    core *c = t->c;
    snapshot_uncache(t);
    node_unref(c, t->root);
    if (t->mapped && c->has_mh) c->mh.release(c->mh.ctx);
    c->a.free(c->a.ctx, t, sizeof *t);
    core_unref(c);
}

static int build(piece_tree *t, size_t len, int mapped) {
    core *c = t->c; uint64_t np = ((uint64_t)len + CH_MASK) >> CH_SHIFT;
    if (np == 0) return 0;
    uint64_t nleaf = (np + FAN - 1) / FAN, tot = 0, lv = nleaf;
    for (;;) { tot += lv; if (lv == 1) break; lv = (lv + FAN - 1) / FAN; }
    if (pool_reserve_class(c, 0, (size_t)nleaf + 2) ||
        pool_reserve_class(c, 1, (size_t)(tot - nleaf) + 2)) return PIECE_ERR_NOMEM;
    node **arr = c->a.alloc(c->a.ctx, (size_t)nleaf * sizeof *arr);
    if (!arr) return PIECE_ERR_NOMEM;
    uint64_t piece = 0; int h = 0;
    for (uint64_t li = 0; li < nleaf; li++) {
        node *l = node_new(c, 1);
        for (unsigned j = 0; j < FAN && piece < np; j++, piece++) {
            uint64_t o = piece << CH_SHIFT, bl = (uint64_t)len - o < CH_SIZE ? (uint64_t)len - o : CH_SIZE;
            ent e = { bl, mapped ? UNK : scan_count(c->orig + o, (size_t)bl).newlines, o };
            ent_put(l, j, &e); l->cnt++;
        }
        arr[li] = l;
    }
    uint64_t cnt = nleaf;
    while (cnt > 1) {
        uint64_t nn = (cnt + FAN - 1) / FAN;
        for (uint64_t g = 0; g < nn; g++) {
            node *p = node_new(c, 0);
            for (unsigned j = 0; j < FAN && g * FAN + j < cnt; j++) { node *ch = arr[g * FAN + j]; ent e = summ(ch, ch); ent_put(p, j, &e); p->cnt++; }
            arr[g] = p;
        }
        cnt = nn; h++;
    }
    node *old = t->root; t->root = arr[0]; t->height = h;
    node_unref(c, old);
    c->a.free(c->a.ctx, arr, (size_t)nleaf * sizeof *arr);
    return 0;
}
int piece_init_copy(piece_tree *t, const uint8_t *data, size_t len) {
    if (t->inited || t->root->cnt) return PIECE_ERR_RANGE;
    core *c = t->c;
    if (len) {
        uint8_t *o = c->a.alloc(c->a.ctx, len);
        if (!o) return PIECE_ERR_NOMEM;
        memcpy(o, data, len);
        c->orig = o; c->orig_len = len; c->orig_owned = 1;
        int r = build(t, len, 0);
        if (r) { c->orig = NULL; c->orig_len = 0; c->orig_owned = 0; c->a.free(c->a.ctx, o, len); return r; }
    }
    t->len = len; t->inited = 1; snapshot_uncache(t); return 0;
}
int piece_init_mapped(piece_tree *t, const uint8_t *mapped, size_t len, const piece_map_hooks *hooks) {
    if (t->inited || t->root->cnt) return PIECE_ERR_RANGE;
    core *c = t->c;
    if (len) {
        c->orig = mapped; c->orig_len = len;
        int r = build(t, len, 1);
        if (r) { c->orig = NULL; c->orig_len = 0; return r; }
    }
    if (hooks) { c->mh = *hooks; c->has_mh = 1; c->mh.acquire(c->mh.ctx); t->mapped = 1; }
    t->len = len; t->inited = 1; snapshot_uncache(t); return 0;
}

/* -------------------------------------------------------------- mutation */
uint64_t piece_len(const piece_tree *t) { owner_trim((piece_tree *)(uintptr_t)t, t->len == 0); return t->len; }

int piece_insert(piece_tree *t, uint64_t off, const uint8_t *data, size_t len) {
    core *c = t->c; uint64_t total = t->len;
    if (off > total) return PIECE_ERR_RANGE;
    if (len == 0) return 0;
    snapshot_uncache(t); owner_trim(t, t->len == 0);
    if (cursor_extend(t, off, data, len)) return 0;
    int r = add_reserve(t, len);
    if (r) return r;
    size_t np = (size_t)(((t->add_len & CH_MASK) + len + CH_MASK) >> CH_SHIFT);
    if ((r = reserve_insert(t, np))) return r;
    uint64_t cur = off; size_t rem = len;
    while (rem) {
        size_t room = (size_t)(CH_SIZE - (t->add_len & CH_MASK)), seg = rem < room ? rem : room;
        uint64_t a0 = t->add_len, nl = nl_count(data, seg);
        memcpy(add_ptr(c, a0), data, seg);
        if (t->run_valid && cur == t->run_end && t->run_addend == a0 && (a0 & CH_MASK)) {
            /* The preceding path may be shared with a snapshot: own it first. */
            node *n = cow(c, t->root); t->root = n; uint64_t rel = cur;
#ifdef PIECE_TESTING
            c->stats.root_descents++;
#endif
            t->cursor_valid = 0; unsigned depth = 0;
            while (!n->leaf) {
                unsigned i = 0;
                while (i + 1 < n->cnt && rel > bytes(n, i)) { rel -= bytes(n, i); i++; }
                t->cursor[depth] = n; t->cursor_i[depth] = i; depth++;
                n->v.in.ch[i] = cow(c, n->v.in.ch[i]); n = n->v.in.ch[i];
            }
            unsigned k = 0; uint64_t start = cur - rel;
            while (k + 1 < n->cnt && rel > bytes(n, k)) { rel -= bytes(n, k); start += bytes(n, k); k++; }
            t->cursor[depth] = n; t->cursor_depth = depth; t->cursor_slot = k; t->cursor_start = start; t->cursor_valid = 1;
            EDIT_ASSERT(cursor_extend(t, cur, data, seg));
            cur += seg; data += seg; rem -= seg; continue;
        }
        ent e = { seg, nl, a0 | TOP };
        if (!cursor_insert(t, cur, &e)) { ins_piece(t, cur, &e); if (len == 1) cursor_seek(t, cur + seg); }
        t->len += seg;
        t->add_len = a0 + seg;
        t->run_valid = 1; t->run_end = cur + seg; t->run_addend = t->add_len;
        cur += seg; data += seg; rem -= seg;
    }
    t->inited = 1; return 0;
}

/* collect add-buffer spans of [off, off+len); copies original fragments into the add buffer when r != NULL */
static unsigned collect(piece_tree *t, uint64_t off, uint64_t len, piece_ref *r, uint64_t *copy_bytes) {
    core *c = t->c; uint64_t total = t->len;
    walk w = { 0, 0, 0, 0 }; uint64_t pos = off, end = off + len, x, skip, n, cur = t->add_len, cb = 0;
    unsigned ns = 0; int have = 0; uint64_t pend = 0;
    while (pos < end && walk_next(&w, t->root, total, &pos, &x, &skip, &n)) {
        uint64_t used = n < end - (pos - n) ? n : end - (pos - n), ao;
        if (x & TOP) ao = (x & ~TOP) + skip;
        else {
            ao = cur;
            if (r) { const uint8_t *src = ent_data(c, x) + skip; uint64_t save = t->add_len; add_append(t, src, (size_t)used); (void)save; }
            cur += used; cb += used;
        }
        if (have && pend == ao) { if (r) r->span[ns - 1].len += used; }
        else {
            if (ns == PIECE_REF_SPANS) return PIECE_REF_SPANS + 1;
            if (r) { r->span[ns].add_off = ao; r->span[ns].len = used; }
            ns++;
        }
        pend = ao + used; have = 1;
    }
    *copy_bytes = cb;
    return ns;
}

static int cursor_delete(piece_tree *t, uint64_t off, uint64_t len, piece_ref *ref) {
    if (!t->cursor_valid) return 0;
    node *leaf = t->cursor[t->cursor_depth]; unsigned k = t->cursor_slot;
    uint64_t start = t->cursor_start, x = entry_x(leaf, k), bl = bytes(leaf, k);
    if (!(x & TOP) || off < start || off - start >= bl || len > bl - (off - start) ||
        (off > start && off + len < start + bl && leaf->cnt == FAN) ||
        (len == bl && leaf->cnt == 1 && t->cursor_depth)) return 0;
    if (ref) { ref->nspans = 1; ref->len = len; ref->span[0].add_off = (x & ~TOP) + off - start; ref->span[0].len = len; }
    uint64_t dn = nl_count(ent_data(t->c, x) + off - start, (size_t)len);
    if (off + len == start + bl && len < bl) {
        bytes_set(leaf, k, bl - len); nl_st(leaf, k, nl_ld(leaf, k) - dn);
    } else {
        uint64_t base = start;
        for (unsigned i = 0; i < k; i++) base -= bytes(leaf, i);
        node *sib = del_rec(t->c, leaf, off - base, off - base + len); EDIT_ASSERT(!sib);
        cursor_relocate(t, off, base);
    }
    cursor_delta(t, len, dn, 1); t->len -= len; t->run_valid = 0;
    pool_trim(t->c, t->len == 0);
#ifdef PIECE_TESTING
    t->c->stats.cursor_hits++;
#endif
    return 1;
}

int piece_delete(piece_tree *t, uint64_t off, uint64_t len, piece_ref *ref) {
    core *c = t->c; uint64_t total = t->len;
    if (off > total || len > total - off) return PIECE_ERR_RANGE;
    if (ref) { ref->nspans = 0; ref->len = 0; }
    if (len == 0) return 0;
    snapshot_uncache(t); owner_trim(t, t->len == 0);
    if (cursor_delete(t, off, len, ref)) return 0;
    int r = reserve_delete(t, 0);
    if (r) return r;
    piece_ref tmp; piece_ref *rr = ref ? ref : &tmp;
    uint64_t cb = 0;
    unsigned ns = collect(t, off, len, NULL, &cb);
    t->cursor_valid = 0;
    t->run_valid = 0;
    if (ns > PIECE_REF_SPANS) {
        if ((r = add_reserve(t, len))) return r;
        walk w = { 0, 0, 0, 0 }; uint64_t pos = off, end = off + len, x, skip, n, base = t->add_len;
        while (pos < end && walk_next(&w, t->root, total, &pos, &x, &skip, &n)) {
            uint64_t used = n < end - (pos - n) ? n : end - (pos - n);
            add_append(t, ent_data(c, x) + skip, (size_t)used);
        }
        rr->nspans = 1; rr->len = len; rr->span[0].add_off = base; rr->span[0].len = len;
    } else {
        if ((r = add_reserve(t, cb))) return r;
        rr->len = len;
        (void)collect(t, off, len, rr, &cb);
        rr->nspans = ns;
    }
    t->len -= len;
    if (off == 0 && len == total) {
        node *nr = node_new(c, 1);
        node_unref(c, t->root); t->root = nr; t->height = 0;
        pool_trim(c, 1); return 0;
    }
    t->root = cow(c, t->root);
    node *sib = del_rec(c, t->root, off, off + len);
    if (sib) root_grow(t, sib);
    while (!t->root->leaf && t->root->cnt == 1) {
        node *old = t->root; t->root = old->v.in.ch[0]; pool_free(c, old, 1); t->height--;
    }
    pool_trim(c, 0); return 0;
}

int piece_insert_ref(piece_tree *t, uint64_t off, const piece_ref *ref) {
    core *c = t->c; uint64_t total = t->len, nseg = 0;
    if (off > total) return PIECE_ERR_RANGE;
    if (ref->nspans > PIECE_REF_SPANS) return PIECE_ERR_RANGE;
    for (unsigned i = 0; i < ref->nspans; i++) {
        uint64_t a = ref->span[i].add_off, l = ref->span[i].len;
        if (a > t->add_len || l > t->add_len - a) return PIECE_ERR_RANGE;
        if (l) nseg += (((a & CH_MASK) + l + CH_MASK) >> CH_SHIFT);
    }
    if (!nseg) return 0;
    snapshot_uncache(t); owner_trim(t, t->len == 0);
    int r = reserve_insert(t, (size_t)nseg);
    if (r) return r;
    t->cursor_valid = 0; t->run_valid = 0;
    uint64_t cur = off;
    for (unsigned i = 0; i < ref->nspans; i++) {
        uint64_t a = ref->span[i].add_off, l = ref->span[i].len;
        while (l) {
            uint64_t room = CH_SIZE - (a & CH_MASK), n = l < room ? l : room;
            ent e = { n, nl_count(add_ptr(c, a), (size_t)n), a | TOP };
            ins_piece(t, cur, &e);
            t->len += n;
            cur += n; a += n; l -= n;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- queries */
static uint64_t count_pieces(const node *n) {
    if (n->leaf) return n->cnt;
    uint64_t s = 0; for (unsigned i = 0; i < n->cnt; i++) s += count_pieces(n->v.in.ch[i]);
    return s;
}
uint64_t piece_piece_count(const piece_tree *t) { owner_trim((piece_tree *)(uintptr_t)t, t->len == 0); return count_pieces(t->root); }
uint64_t piece_line_count(const piece_tree *t) { return tree_nl(t->c, t->root) + 1; }
int piece_read(const piece_tree *t, uint64_t off, uint8_t *dst, size_t len) {
    return read_range(t->c, t->root, node_total(t->root), off, dst, len);
}
uint64_t piece_line_to_byte(const piece_tree *t, uint64_t line) { return l2b(t->c, t->root, node_total(t->root), line); }
uint64_t piece_byte_to_line(const piece_tree *t, uint64_t off) { return b2l(t->c, t->root, node_total(t->root), off); }

/* -------------------------------------------------------------- snapshots */
piece_snapshot *piece_snapshot_take(piece_tree *t) {
    owner_trim(t, t->len == 0);
    t->cursor_valid = 0;
    if (t->cached_snapshot) return piece_snapshot_retain(t->cached_snapshot);
    core *c = t->c;
    /* Prepay the next edit's COW/merge reservation and the header slot. */
    if (reserve_delete(t, 1)) return NULL;
    piece_snapshot *s = (piece_snapshot *)(void *)pool_get(c, 0);
    atomic_init(&s->rc, 1); s->c = c; s->root = t->root; s->len = t->len;
    atomic_fetch_add_explicit(&t->root->rc, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->rc, 1, memory_order_relaxed);
    s->has_mh = t->mapped; t->cached_snapshot = s;
    return piece_snapshot_retain(s);
}
piece_snapshot *piece_snapshot_retain(piece_snapshot *s) {
    atomic_fetch_add_explicit(&s->rc, 1, memory_order_relaxed);
    if (s->has_mh) s->c->mh.acquire(s->c->mh.ctx);
    return s;
}
void piece_snapshot_release(piece_snapshot *s) { snapshot_drop(s, 1); }
uint64_t piece_snapshot_len(const piece_snapshot *s) { return s->len; }
uint64_t piece_snapshot_line_count(const piece_snapshot *s) { return tree_nl(s->c, s->root) + 1; }
int piece_snapshot_read(const piece_snapshot *s, uint64_t off, uint8_t *dst, size_t len) { return read_range(s->c, s->root, s->len, off, dst, len); }
uint64_t piece_snapshot_line_to_byte(const piece_snapshot *s, uint64_t line) { return l2b(s->c, s->root, s->len, line); }
uint64_t piece_snapshot_byte_to_line(const piece_snapshot *s, uint64_t off) { return b2l(s->c, s->root, s->len, off); }

/* --------------------------------------------------------------- iterator */
static void iter_begin(piece_iter *it, const void *src, int snap, uint64_t off, uint64_t total) {
    it->src = src; it->is_snap = snap; it->pos = off > total ? total : off;
    memset(it->priv, 0, sizeof it->priv);
}
void piece_iter_begin(piece_iter *it, const piece_tree *t, uint64_t off) { iter_begin(it, t, 0, off, node_total(t->root)); }
void piece_iter_begin_snapshot(piece_iter *it, const piece_snapshot *s, uint64_t off) { iter_begin(it, s, 1, off, s->len); }
int piece_iter_next(piece_iter *it, const uint8_t **p, size_t *n) {
    const core *c; const node *root; uint64_t total;
    if (it->is_snap) { const piece_snapshot *s = it->src; c = s->c; root = s->root; total = s->len; }
    else { const piece_tree *t = it->src; c = t->c; root = t->root; total = node_total(t->root); }
    walk w; w.leaf = (node *)(uintptr_t)it->priv[0]; w.leaf_start = it->priv[1]; w.k = it->priv[2]; w.k_start = it->priv[3];
    uint64_t x, skip, len;
    if (!walk_next(&w, root, total, &it->pos, &x, &skip, &len)) return 0;
    it->priv[0] = (uint64_t)(uintptr_t)w.leaf; it->priv[1] = w.leaf_start; it->priv[2] = w.k; it->priv[3] = w.k_start;
    *p = ent_data(c, x) + skip; *n = (size_t)len;
    return 1;
}

#ifdef PIECE_TESTING
piece_test_stats piece_test_get_stats(const piece_tree *t) {
    piece_test_stats s = t->c->stats; s.leaf_bytes = LEAF_SZ; s.branch_bytes = BRANCH_SZ; return s;
}
void piece_test_reset_stats(piece_tree *t) { memset(&t->c->stats, 0, sizeof t->c->stats); }
#endif
