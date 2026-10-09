/* Persistent 16-way B+ piece tree. See docs/decisions/P1.4-synthesis.md.
 * Compact leaf/branch pools, cursor path reuse, immutable chunked add storage,
 * atomic snapshot lifetime and lazy exact newline counts. No kernel threads. */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include "base/base.h"
#include "scan/scan.h"
#include <limits.h>
#include <pthread.h>
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
#define SNAPSHOT_SZ 80u
#define MAXH 32
#define NL_BLOCK 512u
#define NL_BLOCKS (65536u / NL_BLOCK)
#define REF_BATCH (FAN - 2u)
#define TRIM_QUERY 3

typedef struct node node;
struct node {
    atomic_uint rc;
    uint16_t cnt, leaf;
    union { node *nextfree; uint64_t add_high; }; /* free link or live ADD high-water */
    union {
        struct { uint32_t b[FAN], nl[FAN]; uint64_t x[FAN]; } l;
        struct { uint64_t b[FAN], nl[FAN]; node *ch[FAN]; } in;
    } v;
};
_Static_assert(offsetof(node, v) + sizeof(((node *)0)->v.l) == LEAF_SZ, "leaf size");
_Static_assert(sizeof(node) == BRANCH_SZ, "branch size");
/* Occupancy and all lists share pool_mu, including worker returns. Slabs
 * retain the existing two slots + 16-byte tail: free-slot words hold queue
 * links, and the tagged tail holds all-list links and two occupancy bits. */
#define SLAB_OVERHEAD 16u
#define FREE_HEADER 32u
#define SLAB_TAG ((uintptr_t)TOP)
typedef struct slab_tail { uintptr_t prev_used; node *next; } slab_tail;
_Static_assert(sizeof(slab_tail) == SLAB_OVERHEAD, "slab tail");
_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "64-bit slab tags");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "slab marker follows the x86-64 little-endian node header"
#endif
typedef struct node_pool {
    node *slabs, *partial, *empty;
    size_t nslabs, nfree; atomic_size_t returned;
} node_pool;

typedef struct { uint64_t b, nl, x; } ent;

/* Prefixes are published as append crosses each block boundary. The mutable
 * total belongs to the owning writer; snapshot byte/line queries do not read
 * it. Rollback restores it together with the saved ADD boundary. */
typedef struct add_chunk {
    atomic_uint rc; uint32_t nl_total; uint16_t nl_prefix[NL_BLOCKS]; uint8_t data[65536];
} add_chunk;
typedef struct retired { add_chunk **p; size_t cap; } retired;
/* Append-only within one view. Only checkpoint rollback forks the directory;
 * snapshots retain their original view and its immutable published prefix. */
typedef struct add_store {
    atomic_uint rc;
    _Atomic(add_chunk **) tbl;
    size_t cap, nch;
    retired ret[24]; int nret, sealed;
    pthread_mutex_t mu;
    piece_snapshot *snapshots;
    atomic_uint_fast64_t live_high;
} add_store;

typedef struct orig_store {
    atomic_uint rc; const uint8_t *data; size_t len; int owned, has_mh;
    piece_map_hooks mh;
} orig_store;
typedef struct core {
    atomic_uint rc; atomic_uint_fast64_t external;
    piece_allocator a;
    piece_map_hooks mh; int has_mh;
    const uint8_t *orig; size_t orig_len; int orig_owned;
    add_store *add; orig_store *original;
    pthread_mutex_t pool_mu;
    node_pool pool[3]; /* leaves, branches, compact snapshot headers */
#ifdef PIECE_TESTING
    piece_test_stats stats;
#endif
} core;

struct piece_tree {
    core *c; node *root; int height; piece_snapshot *cached_snapshot;
    piece_checkpoint *checkpoint;
    uint64_t add_len, deleted_original, fallback_add; int inited, mapped;
    int run_valid; uint64_t run_end, run_addend;
    uint64_t len;
    node *cursor[MAXH]; unsigned cursor_i[MAXH];
    unsigned cursor_depth, cursor_slot; uint64_t cursor_start; int cursor_valid;
};

struct piece_snapshot {
    atomic_uint rc; core *c; node *root; uint64_t len; int has_mh;
    add_store *add; uint64_t add_len;
    piece_snapshot *prev, *next; orig_store *original;
};
struct piece_checkpoint {
    piece_tree *tree; piece_snapshot *snapshot; add_store *restore;
    piece_tree saved; int boundary_private;
};
_Static_assert(sizeof(struct piece_snapshot) <= SNAPSHOT_SZ, "snapshot header fits its compact block");

/* Public mutations reject growth before touching storage. Every internal
 * length update is consequently bounded by that preflight (or a reserved add
 * extent). Keep the invariant checked at the update, including subtree sums.
 * These branches are never taken for a valid tree. */
static inline uint64_t length_add(uint64_t a, uint64_t b) {
    uint64_t sum;
    int overflow = __builtin_add_overflow(a, b, &sum);
    EDIT_ASSERT(!overflow);
    return sum;
}
static inline uint64_t chunk_count(uint64_t len) {
    return (len >> CH_SHIFT) + ((len & CH_MASK) != 0);
}

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
static size_t slot_size(unsigned p) { return p == 0 ? LEAF_SZ : p == 1 ? BRANCH_SZ : SNAPSHOT_SZ; }
static node *slab_slot(node *s, size_t slot, unsigned i) {
    return (node *)(void *)((uint8_t *)s + i * slot);
}
static slab_tail *slab_end(node *s, size_t slot) {
    return (slab_tail *)(void *)((uint8_t *)s + 2 * slot);
}
/* The next slot's kind (or snapshot padding, initialized to zero) never has
 * its high bit set. The tail's tagged first word always does. Read only that
 * immutable halfword, not the neighbouring atomic reference count. This
 * identifies either slot without extra live-node/slab bytes or a lookup. */
static node *slot_slab(node *n, size_t slot) {
    uint16_t marker;
    memcpy(&marker, (uint8_t *)n + slot + offsetof(node, leaf), sizeof marker);
    return marker & UINT16_C(0x8000) ? (node *)(void *)((uint8_t *)n - slot) : n;
}
static unsigned slab_used(const slab_tail *tail) { return (unsigned)(tail->prev_used & 3u); }
static node *slab_prev(const slab_tail *tail) { return (node *)(tail->prev_used & ~(SLAB_TAG | (uintptr_t)3)); }
static void slab_set_prev(slab_tail *tail, node *prev) {
    tail->prev_used = SLAB_TAG | (uintptr_t)prev | slab_used(tail);
}
/* Free nodes keep next at offset eight and prev at offset sixteen. The first
 * eight bytes remain a normal rc/count/kind header for slot_slab and probes. */
static node *queue_prev(const node *n) {
    node *prev; memcpy(&prev, (const uint8_t *)n + 16, sizeof prev); return prev;
}
static void queue_set_prev(node *n, node *prev) { memcpy((uint8_t *)n + 16, &prev, sizeof prev); }
static void slab_enqueue(node **head, node *n) {
    queue_set_prev(n, NULL); n->nextfree = *head;
    if (*head) queue_set_prev(*head, n);
    *head = n;
}
static void slab_dequeue(node **head, node *n) {
    node *prev = queue_prev(n), *next = n->nextfree;
    if (prev) prev->nextfree = next; else *head = next;
    if (next) queue_set_prev(next, prev);
}
static int pool_grow(core *c, unsigned pi) {
    node_pool *p = &c->pool[pi]; size_t slot = slot_size(pi), sz = 2 * slot + SLAB_OVERHEAD;
    node *s = c->a.alloc(c->a.ctx, sz);
    if (!s) return PIECE_ERR_NOMEM;
    EDIT_ASSERT(!((uintptr_t)s & (SLAB_TAG | (uintptr_t)15)));
    slab_tail *tail = slab_end(s, slot); tail->prev_used = SLAB_TAG; tail->next = p->slabs;
    if (p->slabs) slab_set_prev(slab_end(p->slabs, slot), s);
    p->slabs = s; p->nslabs++;
    for (unsigned i = 0; i < 2; i++) {
        node *n = slab_slot(s, slot, i);
        atomic_init(&n->rc, 0); n->cnt = 0; n->leaf = 0;
        POISON((uint8_t *)n + FREE_HEADER, slot - FREE_HEADER);
    }
    slab_enqueue(&p->empty, s); p->nfree += 2; return 0;
}
static int pool_reserve_class(core *c, unsigned pi, size_t n) {
    node_pool *p = &c->pool[pi];
    while (p->nfree < n) { int r = pool_grow(c, pi); if (r) return r; }
    return 0;
}
static node *pool_get(core *c, unsigned pi) {
    node_pool *p = &c->pool[pi]; size_t slot = slot_size(pi);
    node *n = p->partial ? p->partial : p->empty;
    EDIT_ASSERT(n); /* every mutation reserves before changing content */
    node *s = slot_slab(n, slot); slab_tail *tail = slab_end(s, slot);
    unsigned used = slab_used(tail), i = n == s ? 0u : 1u;
    if (used) slab_dequeue(&p->partial, n);
    else {
        slab_dequeue(&p->empty, n); slab_enqueue(&p->partial, slab_slot(s, slot, 1));
    }
    EDIT_ASSERT(!(used & (1u << i)));
    tail->prev_used |= (uintptr_t)1 << i; p->nfree--;
    UNPOISON((uint8_t *)n + FREE_HEADER, slot - FREE_HEADER);
    return n;
}
static void pool_free(core *c, node *n, unsigned pi) { /* any thread */
    pthread_mutex_lock(&c->pool_mu);
#ifdef PIECE_TESTING
    c->stats.pool_returns++;
#endif
    node_pool *p = &c->pool[pi]; size_t slot = slot_size(pi);
    node *s = slot_slab(n, slot); slab_tail *tail = slab_end(s, slot);
    unsigned used = slab_used(tail), i = n == s ? 0u : 1u;
    EDIT_ASSERT(used & (1u << i));
    atomic_store_explicit(&n->rc, 0, memory_order_relaxed);
    POISON((uint8_t *)n + FREE_HEADER, slot - FREE_HEADER);
    tail->prev_used &= ~((uintptr_t)1 << i); p->nfree++;
    if (used == 3) slab_enqueue(&p->partial, n);
    else {
        slab_dequeue(&p->partial, slab_slot(s, slot, 1u - i)); slab_enqueue(&p->empty, s);
    }
    atomic_fetch_add_explicit(&p->returned, 1, memory_order_relaxed);
    pthread_mutex_unlock(&c->pool_mu);
}
static node *node_new(core *c, int leaf) {
    node *n = pool_get(c, leaf ? 0u : 1u);
    atomic_init(&n->rc, 1); n->cnt = 0; n->leaf = (uint16_t)leaf; n->add_high = 0;
    return n;
}
/* Only completed slabs are queued: work follows reclaimed storage, never
 * the live pool. Reservations hold pool_mu until their operation finishes.
 * Forced rollback/final-owner cleanup drains the same queue with pinned roots. */
static void pool_trim_locked(core *c, int force) {
    if ((!force || force == TRIM_QUERY) && atomic_load_explicit(&c->pool[0].returned, memory_order_relaxed) < 64 &&
        atomic_load_explicit(&c->pool[1].returned, memory_order_relaxed) < 64) return;
    /* Queries/takes consume a fixed batch even if workers queued a large
     * retired version. Mutation/release cleanup follows its reclaimed work. */
    size_t budget = force == TRIM_QUERY ? 64 : SIZE_MAX;
    for (unsigned pi = 0; pi < 3; pi++) {
        node_pool *p = &c->pool[pi];
        size_t sz = 2 * slot_size(pi) + SLAB_OVERHEAD;
        while (p->empty && budget) {
#ifdef PIECE_TESTING
            c->stats.slabs_scanned++;
#endif
            node *s = p->empty; slab_dequeue(&p->empty, s);
            size_t slot = slot_size(pi); slab_tail *tail = slab_end(s, slot); node *prev = slab_prev(tail);
            if (prev) slab_end(prev, slot)->next = tail->next; else p->slabs = tail->next;
            if (tail->next) slab_set_prev(slab_end(tail->next, slot), prev);
            p->nslabs--; p->nfree -= 2;
            UNPOISON(s, sz); c->a.free(c->a.ctx, s, sz);
            budget--;
        }
    }
    /* Preserve the wakeup while a bounded drain still has queued work. */
    if (!c->pool[0].empty && !c->pool[1].empty && !c->pool[2].empty)
        for (unsigned pi = 0; pi < 3; pi++) atomic_store_explicit(&c->pool[pi].returned, 0, memory_order_relaxed);
}
static void pool_trim(core *c, int force) {
    pthread_mutex_lock(&c->pool_mu);
    pool_trim_locked(c, force);
    pthread_mutex_unlock(&c->pool_mu);
}
static int pool_done(core *c, int result) {
    pthread_mutex_unlock(&c->pool_mu); return result;
}
/* A leaf root needs only its own possible COW/split. At greater heights a
 * delete visits two boundary paths and may own their merge neighbours. Small
 * fixed slabs keep these reservations inside the tiny-buffer G10f allowance. */
static int reserve_delete(piece_tree *t, int snapshot) {
    pthread_mutex_lock(&t->c->pool_mu);
    core *c = t->c; size_t h = (size_t)t->height;
    size_t leaves = h ? 4 : 2;
    size_t branches = h == 0 ? (t->root->cnt == FAN ? 1u : 0u) :
                      h == 1 ? (t->root->cnt == FAN ? 3u : 1u) : 4 * h - 1;
    int r = pool_reserve_class(c, 0, leaves);
    if (!r) r = pool_reserve_class(c, 1, branches);
    if (!r && snapshot) r = pool_reserve_class(c, 2, 1);
    return r;
}
static int reserve_insert(piece_tree *t, size_t np) {
    pthread_mutex_lock(&t->c->pool_mu);
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

static void node_unref_locked(core *c, node *n) {
    if (atomic_fetch_sub_explicit(&n->rc, 1, memory_order_acq_rel) != 1) return;
    if (!n->leaf) for (unsigned i = 0; i < n->cnt; i++) node_unref_locked(c, n->v.in.ch[i]);
    pool_free(c, n, n->leaf ? 0u : 1u);
}
static void node_unref(core *c, node *n) {
    pthread_mutex_lock(&c->pool_mu); node_unref_locked(c, n); pthread_mutex_unlock(&c->pool_mu);
}
static node *cow(core *c, node *n) {
    if (atomic_load_explicit(&n->rc, memory_order_acquire) == 1) return n;
    node *m = node_new(c, (int)n->leaf);
    m->cnt = n->cnt; m->add_high = n->add_high;
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
static orig_store *orig_store_new(core *c, const uint8_t *data, size_t len, int owned,
                                  const piece_map_hooks *hooks) {
    orig_store *v = c->a.alloc(c->a.ctx, sizeof *v); if (!v) return NULL;
    memset(v, 0, sizeof *v); atomic_init(&v->rc, 1); v->data = data; v->len = len; v->owned = owned;
    if (hooks) { v->mh = *hooks; v->has_mh = 1; }
    return v;
}
static void orig_store_unref(core *c, orig_store *v) {
    if (!v || atomic_fetch_sub_explicit(&v->rc, 1, memory_order_acq_rel) != 1) return;
    if (v->owned && v->len) c->a.free(c->a.ctx, (void *)(uintptr_t)v->data, v->len);
    c->a.free(c->a.ctx, v, sizeof *v);
}
static void orig_store_install(core *c, orig_store *v) {
    orig_store *old = c->original; c->original = v;
    c->orig = v->data; c->orig_len = v->len; c->orig_owned = v->owned;
    c->mh = v->mh; c->has_mh = v->has_mh; orig_store_unref(c, old);
}
static add_store *add_store_new(core *c) {
    add_store *v = c->a.alloc(c->a.ctx, sizeof *v);
    if (!v) return NULL;
    memset(v, 0, sizeof *v); atomic_init(&v->rc, 1);
    atomic_init(&v->tbl, NULL); atomic_init(&v->live_high, 0);
    if (pthread_mutex_init(&v->mu, NULL)) { c->a.free(c->a.ctx, v, sizeof *v); return NULL; }
    return v;
}
static void chunk_unref(core *c, add_chunk *ch) {
    if (atomic_fetch_sub_explicit(&ch->rc, 1, memory_order_acq_rel) == 1)
        c->a.free(c->a.ctx, ch, sizeof *ch);
}
static void add_store_unref(core *c, add_store *v) {
    if (!v || atomic_fetch_sub_explicit(&v->rc, 1, memory_order_acq_rel) != 1) return;
    add_chunk **tbl = atomic_load_explicit(&v->tbl, memory_order_relaxed);
    for (size_t i = 0; i < v->nch; i++) chunk_unref(c, tbl[i]);
    if (tbl) c->a.free(c->a.ctx, tbl, v->cap * sizeof *tbl);
    for (int i = 0; i < v->nret; i++) c->a.free(c->a.ctx, v->ret[i].p, v->ret[i].cap * sizeof *tbl);
    pthread_mutex_destroy(&v->mu); c->a.free(c->a.ctx, v, sizeof *v);
}
/* Called under the view lock. A sealed view has no writer; release can reclaim
 * transaction-only chunks immediately, even while older snapshots survive. */
static void add_store_prune(core *c, add_store *v) {
    if (!v->sealed) return;
    uint64_t high = 0;
    for (piece_snapshot *s = v->snapshots; s; s = s->next)
        if (s->add_len > high) high = s->add_len;
    atomic_store_explicit(&v->live_high, high, memory_order_release);
    size_t keep = (size_t)chunk_count(high);
    add_chunk **tbl = atomic_load_explicit(&v->tbl, memory_order_relaxed);
    while (v->nch > keep) chunk_unref(c, tbl[--v->nch]);
}
static add_store *add_store_prefix(core *c, uint64_t len) {
    add_store *v = add_store_new(c); if (!v) return NULL;
    size_t n = (size_t)chunk_count(len);
    if (n) {
        add_chunk **tbl = c->a.alloc(c->a.ctx, n * sizeof *tbl);
        if (!tbl) { add_store_unref(c, v); return NULL; }
        add_chunk **src = atomic_load_explicit(&c->add->tbl, memory_order_acquire);
        for (size_t i = 0; i < n; i++) { tbl[i] = src[i]; atomic_fetch_add_explicit(&tbl[i]->rc, 1, memory_order_relaxed); }
        atomic_store_explicit(&v->tbl, tbl, memory_order_relaxed); v->cap = n; v->nch = n;
    }
    return v;
}
static inline uint64_t nl_count(const uint8_t *p, size_t n);
static uint64_t chunk_prefix(const add_chunk *ch, size_t off) {
    size_t block = off / NL_BLOCK, edge = off % NL_BLOCK;
    if (block == NL_BLOCKS) return ch->nl_total;
    return ch->nl_prefix[block] + (edge ? nl_count(ch->data + block * NL_BLOCK, edge) : 0);
}
/* Prepay a writable saved boundary before publishing the first snapshot
 * that can observe transaction bytes in it. Abort itself only swaps storage;
 * the restored edit path requires no checkpoint-specific allocation/copy. */
static int checkpoint_snapshot_storage(piece_tree *t) {
    piece_checkpoint *cp = t->checkpoint; uint64_t mark = cp->saved.add_len;
    if (cp->boundary_private || !(mark & CH_MASK) || t->root->add_high <= mark) return 0;
    core *c = t->c; add_chunk *ch = c->a.alloc(c->a.ctx, sizeof *ch);
    if (!ch) return PIECE_ERR_NOMEM;
    atomic_init(&ch->rc, 1);
    add_chunk **tbl = atomic_load_explicit(&cp->restore->tbl, memory_order_acquire);
    size_t i = (size_t)(mark >> CH_SHIFT);
    memcpy(ch->data, tbl[i]->data, (size_t)(mark & CH_MASK));
    memcpy(ch->nl_prefix, tbl[i]->nl_prefix, ((size_t)(mark & CH_MASK) / NL_BLOCK + 1) * sizeof ch->nl_prefix[0]);
    ch->nl_total = (uint32_t)chunk_prefix(tbl[i], (size_t)(mark & CH_MASK));
    add_chunk *old = tbl[i]; tbl[i] = ch; chunk_unref(c, old);
    cp->boundary_private = 1; return 0;
}
static void core_unref(core *c) {
    pthread_mutex_lock(&c->pool_mu);
    unsigned owners = atomic_fetch_sub_explicit(&c->rc, 1, memory_order_acq_rel);
    if (owners == 2) pool_trim_locked(c, 1);
    pthread_mutex_unlock(&c->pool_mu);
    if (owners != 1) return;
    piece_allocator a = c->a;
    add_store_unref(c, c->add);
    orig_store_unref(c, c->original);
    for (unsigned pi = 0; pi < 3; pi++) {
        node_pool *p = &c->pool[pi]; node *slab = p->slabs;
        while (slab) {
            size_t sz = 2 * slot_size(pi) + SLAB_OVERHEAD;
            node *next = slab_end(slab, slot_size(pi))->next;
            UNPOISON(slab, sz); a.free(a.ctx, slab, sz); slab = next;
        }
    }
    pthread_mutex_destroy(&c->pool_mu); a.free(a.ctx, c, sizeof *c);
}

/* An unchanged tree shares one immutable snapshot header. Each public owner
 * still gets its own mapping acquire/release; the tree's cache is covered by
 * the tree's mapping owner. This bounds repeated takes without any edits. */
static void snapshot_drop(piece_snapshot *s, int external) {
    if (!s) return;
    core *c = s->c;
    if (external && s->has_mh) s->original->mh.release(s->original->mh.ctx);
    pthread_mutex_lock(&c->pool_mu);
    int no_external = external && atomic_fetch_sub_explicit(&c->external, 1, memory_order_acq_rel) == 1;
    if (atomic_fetch_sub_explicit(&s->rc, 1, memory_order_acq_rel) != 1) {
        if (no_external) pool_trim_locked(c, 2);
        pthread_mutex_unlock(&c->pool_mu); return;
    }
    add_store *v = s->add;
    pthread_mutex_lock(&v->mu);
    if (s->prev) s->prev->next = s->next; else v->snapshots = s->next;
    if (s->next) s->next->prev = s->prev;
    add_store_prune(c, v);
    pthread_mutex_unlock(&v->mu);
    orig_store_unref(c, s->original);
    node_unref(c, s->root); pool_free(c, (node *)(void *)s, 2);
    add_store_unref(c, v);
    if (no_external) pool_trim_locked(c, 2);
    pthread_mutex_unlock(&c->pool_mu); core_unref(c);
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
    pool_trim(c, TRIM_QUERY);
}

static inline const uint8_t *view_data(const uint8_t *original, const add_store *v, uint64_t x) {
    if (x & TOP) {
        uint64_t a = x & ~TOP;
        add_chunk **tbl = atomic_load_explicit(&v->tbl, memory_order_acquire);
        return tbl[a >> CH_SHIFT]->data + (a & CH_MASK);
    }
    return original + x;
}
static inline const uint8_t *ent_data(const core *c, uint64_t x) { return view_data(c->orig, c->add, x); }
static inline uint8_t *add_ptr(const core *c, uint64_t a) { return (uint8_t *)(uintptr_t)ent_data(c, a | TOP); }

static int add_reserve(piece_tree *t, uint64_t n) {
    core *c = t->c; add_store *v = c->add;
    if (n >= TOP || t->add_len > TOP - 1 - n) return PIECE_ERR_NOMEM;
    uint64_t need = chunk_count(length_add(t->add_len, n));
    while (v->nch < need) {
        add_chunk **tbl = atomic_load(&v->tbl);
        if (v->nch == v->cap) {
            size_t nc = v->cap ? v->cap * 2 : 8;
            if (v->nret >= 24) return PIECE_ERR_NOMEM;
            add_chunk **nt = c->a.alloc(c->a.ctx, nc * sizeof *nt);
            if (!nt) return PIECE_ERR_NOMEM;
            if (v->nch) memcpy(nt, tbl, v->nch * sizeof *nt);
            atomic_store_explicit(&v->tbl, nt, memory_order_release);
            if (tbl) { v->ret[v->nret].p = tbl; v->ret[v->nret].cap = v->cap; v->nret++; }
            v->cap = nc; tbl = nt;
        }
        add_chunk *ch = c->a.alloc(c->a.ctx, sizeof *ch);
        if (!ch) return PIECE_ERR_NOMEM;
        atomic_init(&ch->rc, 1); ch->nl_total = 0; ch->nl_prefix[0] = 0; tbl[v->nch++] = ch;
    }
    return 0;
}
/* append n bytes (space must be reserved) */
static uint64_t add_write(core *c, uint64_t a, const uint8_t *p, size_t n);
static void add_append(piece_tree *t, const uint8_t *p, size_t n) {
    while (n) {
        size_t room = (size_t)(CH_SIZE - (t->add_len & CH_MASK)), k = n < room ? n : room;
        (void)add_write(t->c, t->add_len, p, k);
        t->add_len = length_add(t->add_len, k); p += k; n -= k;
    }
}

static inline uint64_t nl_count(const uint8_t *p, size_t n) {
    if (n < 48) { uint64_t r = 0; for (size_t i = 0; i < n; i++) r += p[i] == '\n'; return r; }
    return scan_count(p, n).newlines;
}
/* Copy and index new bytes once. No checkpoint state, allocation or old-byte
 * recount is needed on an ordinary append, including one-byte cursor typing. */
static uint64_t add_write(core *c, uint64_t a, const uint8_t *p, size_t n) {
    add_chunk **tbl = atomic_load_explicit(&c->add->tbl, memory_order_acquire);
    add_chunk *ch = tbl[a >> CH_SHIFT]; size_t off = (size_t)(a & CH_MASK);
    uint64_t total = 0;
    while (n) {
        size_t room = NL_BLOCK - off % NL_BLOCK, take = n < room ? n : room;
        uint64_t count = nl_count(p, take);
        memcpy(ch->data + off, p, take); ch->nl_total += (uint32_t)count; total += count;
        off += take; p += take; n -= take;
        /* The full-chunk total may be 65536; earlier prefixes fit in u16. */
        if (!(off % NL_BLOCK) && off < CH_SIZE) ch->nl_prefix[off / NL_BLOCK] = (uint16_t)ch->nl_total;
    }
    return total;
}
/* One chunk fragment: full blocks use cached counts; each endpoint recounts
 * at most NL_BLOCK-1 bytes. A same-block fragment scans just its own bytes. */
static uint64_t add_nl(const core *c, uint64_t a, size_t n) {
    add_chunk **tbl = atomic_load_explicit(&c->add->tbl, memory_order_acquire);
    const add_chunk *ch = tbl[a >> CH_SHIFT]; size_t lo = (size_t)(a & CH_MASK), hi = lo + n;
    EDIT_ASSERT(hi <= CH_SIZE);
    size_t recounted = lo / NL_BLOCK == hi / NL_BLOCK ? n : lo % NL_BLOCK + hi % NL_BLOCK;
#ifdef PIECE_TESTING
    ((core *)(uintptr_t)c)->stats.ref_recount_bytes += recounted;
#else
    (void)recounted;
#endif
    if (lo / NL_BLOCK == hi / NL_BLOCK) return nl_count(ch->data + lo, n);
    return chunk_prefix(ch, hi) - chunk_prefix(ch, lo);
}

/* ------------------------------------------------------------- entries */
static inline ent ent_get(const node *n, unsigned i) { ent e = { bytes(n, i), nl_ld(n, i), entry_x(n, i) }; return e; }
static inline void ent_put(node *n, unsigned i, const ent *e) { bytes_set(n, i, e->b); nl_st(n, i, e->nl); entry_x_set(n, i, e->x); }
static ent summ(const node *n, node *self) {
    ent e = { 0, 0, (uint64_t)(uintptr_t)self }; uint64_t unk = 0;
    for (unsigned i = 0; i < n->cnt; i++) { e.b = length_add(e.b, bytes(n, i)); uint64_t v = nl_ld(n, i); unk |= v & UNK; e.nl += v & ~UNK; }
    e.nl |= unk;
    return e;
}
static void ent_split(const core *c, const ent *e, uint64_t s, ent *L, ent *R) {
    L->b = s; R->b = e->b - s; L->x = e->x; R->x = e->x + s;
    if (e->nl & UNK) { L->nl = R->nl = UNK; return; }
    if (!e->nl) { L->nl = R->nl = 0; return; }
    const uint8_t *p = ent_data(c, e->x);
    if (s <= e->b - s) {
        uint64_t l = e->x & TOP ? add_nl(c, e->x & ~TOP, (size_t)s) : nl_count(p, (size_t)s);
        L->nl = l; R->nl = e->nl - l;
    } else {
        uint64_t r = e->x & TOP ? add_nl(c, (e->x & ~TOP) + s, (size_t)(e->b - s)) : nl_count(p + s, (size_t)(e->b - s));
        R->nl = r; L->nl = e->nl - r;
    }
}
static uint64_t ent_nl(const core *c, const add_store *view, const uint8_t *original, node *n, unsigned i);

/* exact newline count of entry i, resolving lazily; safe on shared nodes (relaxed atomics, idempotent) */
static uint64_t ent_nl(const core *c, const add_store *view, const uint8_t *original, node *n, unsigned i) {
    uint64_t v = nl_ld(n, i);
    if (!(v & UNK)) return v;
    uint64_t sum = 0;
    if (n->leaf) sum = scan_count(view_data(original, view, entry_x(n, i)), (size_t)bytes(n, i)).newlines;
    else {
        node *ch = n->v.in.ch[i];
        for (unsigned j = 0; j < ch->cnt; j++) sum += ent_nl(c, view, original, ch, j);
    }
    nl_st(n, i, sum);
    return sum;
}

/* Snapshot storage ownership is bounded by the referenced ADD prefix,
 * rather than all historical/transaction appends. No change in node size. */
static void node_add_high(node *n) {
    uint64_t high = 0;
    for (unsigned i = 0; i < n->cnt; i++) {
        uint64_t end = 0;
        if (n->leaf) {
            uint64_t x = entry_x(n, i);
            if (x & TOP) end = length_add(x & ~TOP, bytes(n, i));
        } else end = n->v.in.ch[i]->add_high;
        if (end > high) high = end;
    }
    n->add_high = high;
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
        n->cnt = (uint16_t)total; node_add_high(n); return NULL;
    }
    EDIT_ASSERT(total <= 2 * FAN);
    ent tmp[2 * FAN]; unsigned m = 0;
    for (unsigned j = 0; j < i; j++) tmp[m++] = ent_get(n, j);
    for (unsigned j = 0; j < nins; j++) tmp[m++] = ins[j];
    for (unsigned j = i + ndel; j < n->cnt; j++) tmp[m++] = ent_get(n, j);
    if (m <= FAN) { for (unsigned j = 0; j < m; j++) ent_put(n, j, &tmp[j]); n->cnt = (uint16_t)m; node_add_high(n); return NULL; }
    node *r = node_new(c, (int)n->leaf);
    unsigned l = (m + 1) / 2;
    for (unsigned j = 0; j < l; j++) ent_put(n, j, &tmp[j]);
    for (unsigned j = l; j < m; j++) ent_put(r, j - l, &tmp[j]);
    n->cnt = (uint16_t)l; r->cnt = (uint16_t)(m - l); node_add_high(n); node_add_high(r);
    return r;
}

/* --------------------------------------------------------------- insert */
static node *ins_batch_rec(core *c, node *n, uint64_t off, const ent *ne, unsigned nne) {
    EDIT_ASSERT(nne && nne <= REF_BATCH);
    if (n->leaf) {
        unsigned k = 0; uint64_t cum = 0; ent rep[FAN]; unsigned nr = 0, ndel = 0, at;
        for (; k < n->cnt; k++) { if (off <= cum + bytes(n, k)) break; cum += bytes(n, k); }
        if (k == n->cnt || off == cum + bytes(n, k)) {
            at = k == n->cnt ? k : k + 1;
            for (unsigned j = 0; j < nne; j++) rep[nr++] = ne[j];
        } else if (off == cum) {
            at = k;
            for (unsigned j = 0; j < nne; j++) rep[nr++] = ne[j];
        }
        else {
            ent e = ent_get(n, k), L, R; ent_split(c, &e, off - cum, &L, &R);
            rep[nr++] = L;
            for (unsigned j = 0; j < nne; j++) rep[nr++] = ne[j];
            rep[nr++] = R; ndel = 1; at = k;
        }
        return splice(c, n, at, ndel, rep, nr);
    }
    unsigned i = 0; uint64_t cum = 0;
    while (i + 1 < n->cnt && off > cum + bytes(n, i)) { cum += bytes(n, i); i++; }
    node *ch = cow(c, n->v.in.ch[i]); n->v.in.ch[i] = ch;
    node *sib = ins_batch_rec(c, ch, off - cum, ne, nne);
    ent e[2]; e[0] = summ(ch, ch);
    if (!sib) { ent_put(n, i, &e[0]); node_add_high(n); return NULL; }
    e[1] = summ(sib, sib);
    return splice(c, n, i, 1, e, 2);
}
static node *ins_rec(core *c, node *n, uint64_t off, const ent *ne) {
    return ins_batch_rec(c, n, off, ne, 1);
}
static void root_grow(piece_tree *t, node *sib) {
    core *c = t->c; node *r = node_new(c, 0); ent e[2];
    e[0] = summ(t->root, t->root); e[1] = summ(sib, sib);
    ent_put(r, 0, &e[0]); ent_put(r, 1, &e[1]); r->cnt = 2; node_add_high(r);
    t->root = r; t->height++;
}
static void ins_batch(piece_tree *t, uint64_t off, const ent *ne, unsigned nne) {
    t->cursor_valid = 0;
#ifdef PIECE_TESTING
    t->c->stats.root_descents++;
#endif
    t->root = cow(t->c, t->root);
    node *sib = ins_batch_rec(t->c, t->root, off, ne, nne);
    if (sib) root_grow(t, sib);
}
static void ins_piece(piece_tree *t, uint64_t off, const ent *ne) { ins_batch(t, off, ne, 1); }
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
        bytes_set(n, i, remove ? bytes(n, i) - b : length_add(bytes(n, i), b));
        nl_st(n, i, remove ? nl_ld(n, i) - nl : nl_ld(n, i) + nl);
    }
    for (unsigned d = t->cursor_depth; d-- > 0;) node_add_high(t->cursor[d]);
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
    uint64_t nl = add_write(t->c, t->add_len, data, len);
    leaf->add_high = length_add(t->add_len, len);
    bytes_set(leaf, k, length_add(bytes(leaf, k), len));
    if (nl) nl_st(leaf, k, nl_ld(leaf, k) + nl);
    for (unsigned d = 0; d < t->cursor_depth; d++) {
        node *n = t->cursor[d]; unsigned i = t->cursor_i[d];
        n->add_high = leaf->add_high;
        n->v.in.b[i] = length_add(n->v.in.b[i], len);
        if (nl) nl_st(n, i, nl_ld(n, i) + nl);
    }
    t->inited = 1; t->len = length_add(t->len, len); t->add_len = length_add(t->add_len, len);
    t->run_end = length_add(t->run_end, len); t->run_addend = length_add(t->run_addend, len);
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
        for (unsigned i = 0; i < leaf->cnt; i++) total = length_add(total, bytes(leaf, i));
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
    a->cnt = (uint16_t)(a->cnt + b->cnt); node_add_high(a);
    node_unref(c, b);
    for (unsigned k = j + 1; k + 1 < n->cnt; k++) { ent e = ent_get(n, k + 1); ent_put(n, k, &e); }
    n->cnt--;
    ent e = summ(a, a); ent_put(n, j, &e); node_add_high(n);
}
static void maybe_merge(core *c, node *n, unsigned idx) {
    if (n->cnt < 2 || idx >= n->cnt) return;
    node *ch = n->v.in.ch[idx];
    if (ch->cnt >= 8) return;
    if (idx + 1 < n->cnt && ch->cnt + n->v.in.ch[idx + 1]->cnt <= FAN) merge_children(c, n, idx);
    else if (idx > 0 && n->v.in.ch[idx - 1]->cnt + ch->cnt <= FAN) merge_children(c, n, idx - 1);
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
    node_add_high(n); return sib;
}

/* ----------------------------------------------------------------- walk */
typedef struct { node *leaf; uint64_t leaf_start, k, k_start; } walk;
static void walk_visit(const core *c) {
    (void)c;
#ifdef PIECE_TESTING
    __atomic_fetch_add(&((core *)(uintptr_t)c)->stats.walk_nodes, 1, __ATOMIC_RELAXED);
#endif
}
static void walk_seek(const core *c, walk *w, const node *root, uint64_t pos) {
    const node *n = root; uint64_t base = 0;
    while (!n->leaf) {
        walk_visit(c);
        unsigned i = 0;
        while (i + 1 < n->cnt && pos >= base + bytes(n, i)) { base += bytes(n, i); i++; }
        n = n->v.in.ch[i];
    }
    walk_visit(c);
    unsigned k = 0; uint64_t ks = base;
    while (k + 1 < n->cnt && pos >= ks + bytes(n, k)) { ks += bytes(n, k); k++; }
    w->leaf = (node *)(uintptr_t)n; w->k = k; w->k_start = ks;
    /* leaf_start: start of the leaf = base after loop above (recompute) */
    w->leaf_start = base;
}
/* next entry fragment at *pos; returns 0 at end */
static int walk_next(const core *c, walk *w, const node *root, uint64_t total, uint64_t *pos, uint64_t *x, uint64_t *skip, uint64_t *n) {
    if (!w->leaf || w->k >= w->leaf->cnt) {
        if (*pos >= total) return 0;
        walk_seek(c, w, root, *pos);
    }
    node *l = w->leaf; unsigned k = (unsigned)w->k;
    *x = entry_x(l, k); *skip = *pos - w->k_start; *n = bytes(l, k) - *skip;
    *pos += *n; w->k_start += bytes(l, k); w->k++;
    return 1;
}
static uint64_t node_total(const node *n) { uint64_t s = 0; for (unsigned i = 0; i < n->cnt; i++) s = length_add(s, bytes(n, i)); return s; }

/* Call-local traversal for ranges. Each ancestor is entered once; advancing
 * leaves climbs the saved path and descends only the next intersected subtree.
 * The public iterator keeps its frozen four-word scratch representation. */
typedef struct range_walk {
    walk entry; const node *path[MAXH]; unsigned index[MAXH], depth;
} range_walk;
static void range_seek(const core *c, range_walk *w, const node *root, uint64_t pos) {
    const node *n = root; uint64_t base = 0; w->depth = 0;
    walk_visit(c);
    while (!n->leaf) {
        unsigned i = 0;
        while (i + 1 < n->cnt && pos >= base + bytes(n, i)) { base += bytes(n, i); i++; }
        EDIT_ASSERT(w->depth < MAXH);
        w->path[w->depth] = n; w->index[w->depth++] = i;
        n = n->v.in.ch[i]; walk_visit(c);
    }
    walk *e = &w->entry; e->leaf = (node *)(uintptr_t)n; e->leaf_start = base;
    e->k = 0; e->k_start = base;
    while (e->k + 1 < n->cnt && pos >= e->k_start + bytes(n, (unsigned)e->k)) {
        e->k_start += bytes(n, (unsigned)e->k); e->k++;
    }
}
static int range_advance(const core *c, range_walk *w) {
    while (w->depth) {
        unsigned d = w->depth - 1; const node *parent = w->path[d];
        if (w->index[d] + 1 == parent->cnt) { w->depth--; continue; }
        const node *n = parent->v.in.ch[++w->index[d]]; walk_visit(c);
        while (!n->leaf) {
            EDIT_ASSERT(w->depth < MAXH);
            w->path[w->depth] = n; w->index[w->depth++] = 0;
            n = n->v.in.ch[0]; walk_visit(c);
        }
        w->entry.leaf = (node *)(uintptr_t)n; w->entry.k = 0;
        w->entry.leaf_start = w->entry.k_start;
        return 1;
    }
    return 0;
}
static int range_next(const core *c, range_walk *w, const node *root, uint64_t total,
                      uint64_t *pos, uint64_t *x, uint64_t *skip, uint64_t *n) {
    if (*pos >= total) return 0;
    walk *e = &w->entry;
    if (!e->leaf) range_seek(c, w, root, *pos);
    else if (e->k == e->leaf->cnt && !range_advance(c, w)) return 0;
    unsigned k = (unsigned)e->k;
    *x = entry_x(e->leaf, k); *skip = *pos - e->k_start; *n = bytes(e->leaf, k) - *skip;
    *pos += *n; e->k_start += bytes(e->leaf, k); e->k++;
    return 1;
}

static int read_range(const core *c, const add_store *v, const uint8_t *original, const node *root, uint64_t total, uint64_t off, uint8_t *dst, size_t len) {
    if (off > total || len > total - off) return PIECE_ERR_RANGE;
    range_walk w = {0}; uint64_t pos = off, end = off + len, x, skip, n;
    while (pos < end && range_next(c, &w, root, total, &pos, &x, &skip, &n)) {
        uint64_t take = n < end - (pos - n) ? n : end - (pos - n);
        memcpy(dst, view_data(original, v, x) + skip, (size_t)take); dst += take;
        if (take < n) break;
    }
    return 0;
}

/* ------------------------------------------------------------ line maps */
static uint64_t tree_nl(const core *c, const add_store *v, const uint8_t *original, node *root) {
    uint64_t s = 0; for (unsigned i = 0; i < root->cnt; i++) s += ent_nl(c, v, original, root, i);
    return s;
}
/* Descend by newline count. Flagged (unresolved) internal entries are entered instead of resolved
 * up front, so only the prefix before the target is ever scanned; a fully consumed subtree gets its
 * exact count written back. Returns 1 and sets *res when the target lies inside n. */
static int l2b_rec(const core *c, const add_store *view, const uint8_t *original, node *n, uint64_t *need, uint64_t *base, uint64_t *res) {
    for (unsigned i = 0; i < n->cnt; i++) {
        uint64_t v = nl_ld(n, i);
        if (n->leaf) {
            if (v & UNK) v = ent_nl(c, view, original, n, i);
            if (v < *need) { *need -= v; *base += bytes(n, i); continue; }
            const uint8_t *d = view_data(original, view, entry_x(n, i));
            const uint8_t *p = scan_find_nth_newline(d, (size_t)bytes(n, i), *need - 1);
            if (!p) return 0;
            *res = *base + (uint64_t)(p - d) + 1;
            return 1;
        }
        if (!(v & UNK) && v < *need) { *need -= v; *base += bytes(n, i); continue; }
        uint64_t need0 = *need;
        if (l2b_rec(c, view, original, n->v.in.ch[i], need, base, res)) return 1;
        nl_st(n, i, need0 - *need);               /* consumed whole: exact */
    }
    return 0;
}
static uint64_t l2b(const core *c, const add_store *view, const uint8_t *original, node *root, uint64_t total, uint64_t line) {
    if (line == 0) return 0;
    uint64_t need = line, base = 0, res = 0;
    return l2b_rec(c, view, original, root, &need, &base, &res) ? res : total;
}
static uint64_t b2l(const core *c, const add_store *view, const uint8_t *original, node *root, uint64_t total, uint64_t off) {
    if (off >= total) return tree_nl(c, view, original, root);
    uint64_t acc = 0; node *n = root;
    for (;;) {
        unsigned i = 0;
        while (i + 1 < n->cnt && off >= bytes(n, i)) { acc += ent_nl(c, view, original, n, i); off -= bytes(n, i); i++; }
        if (n->leaf) {
            const uint8_t *p = view_data(original, view, entry_x(n, i));
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
    memset(c, 0, sizeof *c); atomic_init(&c->rc, 1); atomic_init(&c->external, 0); c->a = *a;
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr)) { a->free(a->ctx, c, sizeof *c); return NULL; }
    int lock_error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!lock_error) lock_error = pthread_mutex_init(&c->pool_mu, &attr);
    pthread_mutexattr_destroy(&attr);
    if (lock_error) { a->free(a->ctx, c, sizeof *c); return NULL; }
    c->add = add_store_new(c);
    if (!c->add) { pthread_mutex_destroy(&c->pool_mu); a->free(a->ctx, c, sizeof *c); return NULL; }
    c->original = orig_store_new(c, NULL, 0, 0, NULL);
    if (!c->original) { core_unref(c); return NULL; }
    for (unsigned pi = 0; pi < 3; pi++) atomic_init(&c->pool[pi].returned, 0);
    piece_tree *t = a->alloc(a->ctx, sizeof *t);
    if (!t) { core_unref(c); return NULL; }
    memset(t, 0, sizeof *t); t->c = c;
    pthread_mutex_lock(&c->pool_mu);
    int reserve_error = pool_reserve_class(c, 0, 1);
    if (!reserve_error) t->root = node_new(c, 1);
    pthread_mutex_unlock(&c->pool_mu);
    if (reserve_error) { a->free(a->ctx, t, sizeof *t); core_unref(c); return NULL; }
    return t;
}
void piece_destroy(piece_tree *t) {
    if (!t) return;
    core *c = t->c;
    snapshot_uncache(t);
    node_unref(c, t->root);
    if (t->mapped && c->has_mh) c->mh.release(c->mh.ctx);
    orig_store_unref(c, c->original); c->original = NULL;
    add_store *v = c->add; c->add = NULL;
    pthread_mutex_lock(&v->mu); v->sealed = 1; add_store_prune(c, v); pthread_mutex_unlock(&v->mu);
    add_store_unref(c, v);
    c->a.free(c->a.ctx, t, sizeof *t);
    core_unref(c);
}

static int build(piece_tree *t, size_t len, int mapped) {
    core *c = t->c; uint64_t np = chunk_count(len);
    if (np == 0) return 0;
    uint64_t nleaf = (np + FAN - 1) / FAN, tot = 0, lv = nleaf;
    for (;;) { tot += lv; if (lv == 1) break; lv = (lv + FAN - 1) / FAN; }
    pthread_mutex_lock(&c->pool_mu);
    if (pool_reserve_class(c, 0, (size_t)nleaf + 2) ||
        pool_reserve_class(c, 1, (size_t)(tot - nleaf) + 2)) { pool_trim_locked(c, 2); return pool_done(c, PIECE_ERR_NOMEM); }
    node **arr = c->a.alloc(c->a.ctx, (size_t)nleaf * sizeof *arr);
    if (!arr) { pool_trim_locked(c, 2); return pool_done(c, PIECE_ERR_NOMEM); }
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
    return pool_done(c, 0);
}
int piece_init_copy(piece_tree *t, const uint8_t *data, size_t len) {
    if (t->inited || t->root->cnt) return PIECE_ERR_RANGE;
    core *c = t->c;
    if (len) {
        uint8_t *o = c->a.alloc(c->a.ctx, len); if (!o) return PIECE_ERR_NOMEM;
        memcpy(o, data, len);
        orig_store *v = orig_store_new(c, o, len, 1, NULL);
        if (!v) { c->a.free(c->a.ctx, o, len); return PIECE_ERR_NOMEM; }
        c->orig = o; c->orig_len = len; c->orig_owned = 1;
        int r = build(t, len, 0);
        if (r) {
            c->orig = c->original->data; c->orig_len = c->original->len; c->orig_owned = c->original->owned;
            orig_store_unref(c, v); return r;
        }
        orig_store_install(c, v);
    }
    t->len = len; t->inited = 1; snapshot_uncache(t); return 0;
}
int piece_init_mapped(piece_tree *t, const uint8_t *mapped, size_t len, const piece_map_hooks *hooks) {
    if (t->inited || t->root->cnt) return PIECE_ERR_RANGE;
    core *c = t->c;
    orig_store *v = orig_store_new(c, mapped, len, 0, hooks); if (!v) return PIECE_ERR_NOMEM;
    c->orig = mapped; c->orig_len = len;
    int r = build(t, len, 1);
    if (r) {
        c->orig = c->original->data; c->orig_len = c->original->len;
        orig_store_unref(c, v); return r;
    }
    orig_store_install(c, v);
    if (hooks) { c->mh.acquire(c->mh.ctx); t->mapped = 1; }
    t->len = len; t->inited = 1; snapshot_uncache(t); return 0;
}

/* -------------------------------------------------------------- mutation */
uint64_t piece_len(const piece_tree *t) { owner_trim((piece_tree *)(uintptr_t)t, t->len == 0); return t->len; }

int piece_insert(piece_tree *t, uint64_t off, const uint8_t *data, size_t len) {
    core *c = t->c; uint64_t total = t->len;
    if (off > total || len > UINT64_MAX - total) return PIECE_ERR_RANGE;
    if (len == 0) return 0;
    snapshot_uncache(t); owner_trim(t, t->len == 0);
    if (cursor_extend(t, off, data, len)) return 0;
    int r = add_reserve(t, len);
    if (r) return r;
    size_t np = (size_t)chunk_count(length_add(t->add_len & CH_MASK, len));
    if ((r = reserve_insert(t, np))) return pool_done(c, r);
    uint64_t cur = off; size_t rem = len;
    while (rem) {
        size_t room = (size_t)(CH_SIZE - (t->add_len & CH_MASK)), seg = rem < room ? rem : room;
        uint64_t a0 = t->add_len;
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
            cur = length_add(cur, seg); data += seg; rem -= seg; continue;
        }
        ent e = { seg, add_write(c, a0, data, seg), a0 | TOP };
        if (!cursor_insert(t, cur, &e)) { ins_piece(t, cur, &e); if (len == 1) cursor_seek(t, cur + seg); }
        t->len = length_add(t->len, seg);
        t->add_len = length_add(a0, seg);
        t->run_valid = 1; t->run_end = length_add(cur, seg); t->run_addend = t->add_len;
        cur = length_add(cur, seg); data += seg; rem -= seg;
    }
    /* Bulk reservations can be large. Reclaim their unused slabs on this
     * bulk call, so a later small edit/query never inherits their cleanup. */
    if (np > 1) pool_trim_locked(c, 2);
    t->inited = 1; return pool_done(c, 0);
}

/* collect add-buffer spans of [off, off+len); copies original fragments into the add buffer when r != NULL */
static unsigned collect(piece_tree *t, uint64_t off, uint64_t len, piece_ref *r, uint64_t *copy_bytes) {
    core *c = t->c; uint64_t total = t->len;
    range_walk w = {0}; uint64_t pos = off, end = off + len, x, skip, n, cur = t->add_len, cb = 0;
    unsigned ns = 0; int have = 0; uint64_t pend = 0;
    while (pos < end && range_next(c, &w, t->root, total, &pos, &x, &skip, &n)) {
        uint64_t used = n < end - (pos - n) ? n : end - (pos - n), ao;
        if (x & TOP) ao = (x & ~TOP) + skip;
        else {
            ao = cur;
            if (r) { const uint8_t *src = ent_data(c, x) + skip; uint64_t save = t->add_len; add_append(t, src, (size_t)used); (void)save; }
            cur = length_add(cur, used); cb = length_add(cb, used);
        }
        if (have && pend == ao) { if (r) r->span[ns - 1].len = length_add(r->span[ns - 1].len, used); }
        else {
            if (ns == PIECE_REF_SPANS && r) return PIECE_REF_SPANS + 1;
            if (r) { r->span[ns].add_off = ao; r->span[ns].len = used; }
            if (ns <= PIECE_REF_SPANS) ns++;
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
        (len == bl && leaf->cnt <= FAN / 2 && t->cursor_depth)) return 0;
    if (ref) { ref->nspans = 1; ref->len = len; ref->span[0].add_off = (x & ~TOP) + off - start; ref->span[0].len = len; }
    uint64_t dn = nl_count(ent_data(t->c, x) + (off - start), (size_t)len);
    if (off + len == start + bl && len < bl) {
        bytes_set(leaf, k, bl - len); nl_st(leaf, k, nl_ld(leaf, k) - dn);
    } else {
        uint64_t base = start;
        for (unsigned i = 0; i < k; i++) base -= bytes(leaf, i);
        node *sib = del_rec(t->c, leaf, off - base, off - base + len); EDIT_ASSERT(!sib);
        cursor_relocate(t, off, base);
    }
    node_add_high(leaf); cursor_delta(t, len, dn, 1); t->len -= len; t->run_valid = 0;
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
    if (r) return pool_done(c, r);
    piece_ref tmp; piece_ref *rr = ref ? ref : &tmp;
    uint64_t cb = 0;
    unsigned ns = collect(t, off, len, NULL, &cb);
    t->cursor_valid = 0;
    t->run_valid = 0;
    if (!ref && ns > PIECE_REF_SPANS) {
        if ((r = add_reserve(t, cb))) return pool_done(c, r);
        range_walk w = {0}; uint64_t pos = off, end = off + len, x, skip, n;
        while (pos < end && range_next(c, &w, t->root, total, &pos, &x, &skip, &n)) {
            uint64_t used = n < end - (pos - n) ? n : end - (pos - n);
            if (!(x & TOP)) add_append(t, ent_data(c, x) + skip, (size_t)used);
        }
    } else if (ns > PIECE_REF_SPANS) {
        if ((r = add_reserve(t, len))) return pool_done(c, r);
        range_walk w = {0}; uint64_t pos = off, end = off + len, x, skip, n, base = t->add_len;
        while (pos < end && range_next(c, &w, t->root, total, &pos, &x, &skip, &n)) {
            uint64_t used = n < end - (pos - n) ? n : end - (pos - n);
            add_append(t, ent_data(c, x) + skip, (size_t)used);
        }
        rr->nspans = 1; rr->len = len; rr->span[0].add_off = base; rr->span[0].len = len;
    } else {
        if ((r = add_reserve(t, cb))) return pool_done(c, r);
        rr->len = len;
        (void)collect(t, off, len, rr, &cb);
        rr->nspans = ns;
    }
    t->deleted_original = length_add(t->deleted_original, cb);
    if (ref && ns > PIECE_REF_SPANS) t->fallback_add = length_add(t->fallback_add, len - cb);
    t->len -= len;
    if (off == 0 && len == total) {
        node *nr = node_new(c, 1);
        node_unref(c, t->root); t->root = nr; t->height = 0;
        pool_trim(c, 1); return pool_done(c, 0);
    }
    t->root = cow(c, t->root);
    node *sib = del_rec(c, t->root, off, off + len);
    if (sib) root_grow(t, sib);
    while (!t->root->leaf && t->root->cnt == 1) {
        node *old = t->root; t->root = old->v.in.ch[0]; pool_free(c, old, 1); t->height--;
    }
    pool_trim(c, 0); return pool_done(c, 0);
}

int piece_insert_ref(piece_tree *t, uint64_t off, const piece_ref *ref) {
    core *c = t->c; uint64_t total = t->len, nseg = 0, growth = 0;
    if (off > total) return PIECE_ERR_RANGE;
    if (ref->nspans > PIECE_REF_SPANS) return PIECE_ERR_RANGE;
    for (unsigned i = 0; i < ref->nspans; i++) {
        uint64_t a = ref->span[i].add_off, l = ref->span[i].len;
        if (a > t->add_len || l > t->add_len - a) return PIECE_ERR_RANGE;
        if (__builtin_add_overflow(growth, l, &growth)) return PIECE_ERR_RANGE;
        if (l) nseg = length_add(nseg, chunk_count(length_add(a & CH_MASK, l)));
    }
    if (growth > UINT64_MAX - total) return PIECE_ERR_RANGE;
    if (!nseg) return 0;
    snapshot_uncache(t); owner_trim(t, t->len == 0);
    int r = reserve_insert(t, (size_t)nseg);
    if (r) return pool_done(c, r);
    t->cursor_valid = 0; t->run_valid = 0;
    uint64_t cur = off, batch_bytes = 0; ent batch[REF_BATCH]; unsigned nbatch = 0;
    for (unsigned i = 0; i < ref->nspans; i++) {
        uint64_t a = ref->span[i].add_off, l = ref->span[i].len;
        while (l) {
            uint64_t room = CH_SIZE - (a & CH_MASK), n = l < room ? l : room;
            batch[nbatch++] = (ent){ n, add_nl(c, a, (size_t)n), a | TOP };
            batch_bytes = length_add(batch_bytes, n); a = length_add(a, n); l -= n;
            if (nbatch == REF_BATCH) {
                ins_batch(t, cur, batch, nbatch);
                t->len = length_add(t->len, batch_bytes); cur = length_add(cur, batch_bytes);
                nbatch = 0; batch_bytes = 0;
            }
        }
    }
    if (nbatch) { ins_batch(t, cur, batch, nbatch); t->len = length_add(t->len, batch_bytes); }
    if (nseg > 1) pool_trim_locked(c, 2);
    return pool_done(c, 0);
}

/* ----------------------------------------------------------- checkpoints */
int piece_checkpoint_begin(piece_tree *t, piece_checkpoint **out) {
    if (out) *out = NULL;
    if (!t || !out || t->checkpoint) return PIECE_ERR_RANGE;
    core *c = t->c;
    piece_checkpoint *cp = c->a.alloc(c->a.ctx, sizeof *cp);
    if (!cp) return PIECE_ERR_NOMEM;
    cp->tree = t; cp->saved = *t; cp->boundary_private = 0;
    cp->restore = add_store_prefix(c, t->add_len);
    if (!cp->restore) { c->a.free(c->a.ctx, cp, sizeof *cp); return PIECE_ERR_NOMEM; }
    cp->snapshot = piece_snapshot_take(t);
    if (!cp->snapshot) {
        add_store_unref(c, cp->restore); t->cursor_valid = cp->saved.cursor_valid;
        c->a.free(c->a.ctx, cp, sizeof *cp);
        return PIECE_ERR_NOMEM;
    }
    t->checkpoint = cp; *out = cp; return PIECE_OK;
}
void piece_checkpoint_commit(piece_checkpoint *cp) {
    piece_tree *t = cp->tree; core *c = t->c;
    t->checkpoint = NULL;
    piece_snapshot_release(cp->snapshot); add_store_unref(c, cp->restore);
    c->a.free(c->a.ctx, cp, sizeof *cp);
}
void piece_checkpoint_abort(piece_checkpoint *cp) {
    piece_tree *t = cp->tree; core *c = t->c;
    add_store *old = c->add;
    /* Transfer the retained root back before dropping either live root owner. */
    node *root = cp->snapshot->root;
    atomic_fetch_add_explicit(&root->rc, 1, memory_order_relaxed);
    snapshot_uncache(t); node_unref(c, t->root);
    *t = cp->saved; t->root = root; t->cached_snapshot = NULL; t->checkpoint = NULL;
    c->add = cp->restore;
    if (c->original != cp->snapshot->original) {
        if (c->has_mh) c->mh.release(c->mh.ctx);
        orig_store *v = cp->snapshot->original;
        atomic_fetch_add_explicit(&v->rc, 1, memory_order_relaxed);
        if (v->has_mh) v->mh.acquire(v->mh.ctx);
        orig_store_install(c, v);
    }
    piece_snapshot_release(cp->snapshot);
    /* An unchanged/intermediate snapshot may still share all or part of the
     * saved cursor path. Its publication invalidation must survive rollback. */
    if (t->cursor_valid) for (unsigned d = 0; d <= t->cursor_depth; d++)
        if (atomic_load_explicit(&t->cursor[d]->rc, memory_order_acquire) != 1) { t->cursor_valid = 0; break; }
    pthread_mutex_lock(&old->mu);
    uint64_t high = 0;
    for (piece_snapshot *s = old->snapshots; s; s = s->next) if (s->add_len > high) high = s->add_len;
    /* If all transaction observers died, reuse the original saved boundary
     * and return the prepaid copy too, before pruning the abandoned view. */
    if (cp->boundary_private && high <= t->add_len) {
        size_t i = (size_t)(t->add_len >> CH_SHIFT);
        add_chunk **dst = atomic_load_explicit(&c->add->tbl, memory_order_relaxed);
        add_chunk **src = atomic_load_explicit(&old->tbl, memory_order_relaxed);
        add_chunk *backup = dst[i]; dst[i] = src[i]; atomic_fetch_add_explicit(&dst[i]->rc, 1, memory_order_relaxed);
        chunk_unref(c, backup);
    }
    old->sealed = 1; add_store_prune(c, old); pthread_mutex_unlock(&old->mu);
    add_store_unref(c, old);
    /* Restore the ordinary append cache now, never on a later inactive edit.
     * A surviving transaction snapshot has its prepaid separate boundary. */
    if (t->add_len & CH_MASK) {
        add_chunk **tbl = atomic_load_explicit(&c->add->tbl, memory_order_relaxed);
        add_chunk *tail = tbl[t->add_len >> CH_SHIFT];
        tail->nl_total = (uint32_t)chunk_prefix(tail, (size_t)(t->add_len & CH_MASK));
    }
    c->a.free(c->a.ctx, cp, sizeof *cp); pool_trim(c, 2);
}

/* --------------------------------------------------------------- queries */
static uint64_t count_pieces(const node *n) {
    if (n->leaf) return n->cnt;
    uint64_t s = 0; for (unsigned i = 0; i < n->cnt; i++) s += count_pieces(n->v.in.ch[i]);
    return s;
}
uint64_t piece_piece_count(const piece_tree *t) { owner_trim((piece_tree *)(uintptr_t)t, t->len == 0); return count_pieces(t->root); }
uint64_t piece_line_count(const piece_tree *t) { return tree_nl(t->c, t->c->add, t->c->orig, t->root) + 1; }
int piece_read(const piece_tree *t, uint64_t off, uint8_t *dst, size_t len) {
    return read_range(t->c, t->c->add, t->c->orig, t->root, node_total(t->root), off, dst, len);
}
uint64_t piece_line_to_byte(const piece_tree *t, uint64_t line) { return l2b(t->c, t->c->add, t->c->orig, t->root, node_total(t->root), line); }
uint64_t piece_byte_to_line(const piece_tree *t, uint64_t off) { return b2l(t->c, t->c->add, t->c->orig, t->root, node_total(t->root), off); }

/* -------------------------------------------------------------- snapshots */
piece_snapshot *piece_snapshot_take(piece_tree *t) {
    owner_trim(t, t->len == 0);
    t->cursor_valid = 0;
    if (t->cached_snapshot) return piece_snapshot_retain(t->cached_snapshot);
    core *c = t->c;
    /* Prepay the next edit's COW/merge reservation and the header slot. */
    if (reserve_delete(t, 1)) { pool_done(c, 0); return NULL; }
    if (t->checkpoint && checkpoint_snapshot_storage(t)) { pool_done(c, 0); return NULL; }
    piece_snapshot *s = (piece_snapshot *)(void *)pool_get(c, 2);
    atomic_init(&s->rc, 1); s->c = c; s->root = t->root; s->len = t->len;
    atomic_fetch_add_explicit(&t->root->rc, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->rc, 1, memory_order_relaxed);
    s->original = c->original; atomic_fetch_add_explicit(&s->original->rc, 1, memory_order_relaxed);
    s->has_mh = t->mapped; s->add = c->add; s->add_len = t->root->add_high;
    atomic_fetch_add_explicit(&s->add->rc, 1, memory_order_relaxed);
    pthread_mutex_lock(&s->add->mu);
    s->prev = NULL; s->next = s->add->snapshots;
    if (s->next) s->next->prev = s;
    s->add->snapshots = s;
    pthread_mutex_unlock(&s->add->mu);
    t->cached_snapshot = s;
    piece_snapshot *out = piece_snapshot_retain(s); pool_done(c, 0); return out;
}
piece_snapshot *piece_snapshot_retain(piece_snapshot *s) {
    /* The caller already owns s (or its tree cache owns it). Refusing the
     * increment at the cap leaves every existing owner, including the cache,
     * intact. Acquire a mapping owner only after the CAS succeeds. */
    unsigned owners = atomic_load_explicit(&s->rc, memory_order_relaxed);
    do {
        if (owners == UINT_MAX) return NULL;
    } while (!atomic_compare_exchange_weak_explicit(&s->rc, &owners, owners + 1,
                                                   memory_order_relaxed, memory_order_relaxed));
    atomic_fetch_add_explicit(&s->c->external, 1, memory_order_relaxed);
    if (s->has_mh) s->original->mh.acquire(s->original->mh.ctx);
    return s;
}
void piece_snapshot_release(piece_snapshot *s) { snapshot_drop(s, 1); }
uint64_t piece_snapshot_len(const piece_snapshot *s) { return s->len; }
uint64_t piece_snapshot_line_count(const piece_snapshot *s) { return tree_nl(s->c, s->add, s->original->data, s->root) + 1; }
int piece_snapshot_read(const piece_snapshot *s, uint64_t off, uint8_t *dst, size_t len) { return read_range(s->c, s->add, s->original->data, s->root, s->len, off, dst, len); }
uint64_t piece_snapshot_line_to_byte(const piece_snapshot *s, uint64_t line) { return l2b(s->c, s->add, s->original->data, s->root, s->len, line); }
uint64_t piece_snapshot_byte_to_line(const piece_snapshot *s, uint64_t off) { return b2l(s->c, s->add, s->original->data, s->root, s->len, off); }

/* --------------------------------------------------------------- iterator */
static void iter_begin(piece_iter *it, const void *src, int snap, uint64_t off, uint64_t total) {
    it->src = src; it->is_snap = snap; it->pos = off > total ? total : off;
    memset(it->priv, 0, sizeof it->priv);
}
void piece_iter_begin(piece_iter *it, const piece_tree *t, uint64_t off) { iter_begin(it, t, 0, off, node_total(t->root)); }
void piece_iter_begin_snapshot(piece_iter *it, const piece_snapshot *s, uint64_t off) { iter_begin(it, s, 1, off, s->len); }
int piece_iter_next(piece_iter *it, const uint8_t **p, size_t *n) {
    const core *c; const uint8_t *original; const add_store *v; const node *root; uint64_t total;
    if (it->is_snap) { const piece_snapshot *s = it->src; c = s->c; original = s->original->data; v = s->add; root = s->root; total = s->len; }
    else { const piece_tree *t = it->src; c = t->c; original = t->c->orig; v = t->c->add; root = t->root; total = node_total(t->root); }
    walk w; w.leaf = (node *)(uintptr_t)it->priv[0]; w.leaf_start = it->priv[1]; w.k = it->priv[2]; w.k_start = it->priv[3];
    uint64_t x, skip, len;
    if (!walk_next(c, &w, root, total, &it->pos, &x, &skip, &len)) return 0;
    it->priv[0] = (uint64_t)(uintptr_t)w.leaf; it->priv[1] = w.leaf_start; it->priv[2] = w.k; it->priv[3] = w.k_start;
    *p = view_data(original, v, x) + skip; *n = (size_t)len;
    return 1;
}

#ifdef PIECE_TESTING
piece_test_stats piece_test_get_stats(const piece_tree *t) {
    core *c = t->c; pthread_mutex_lock(&c->pool_mu);
    piece_test_stats s = {0};
    s.root_descents = c->stats.root_descents; s.cursor_hits = c->stats.cursor_hits;
    s.pathcopy_bytes = c->stats.pathcopy_bytes; s.gap_deletes = c->stats.gap_deletes;
    s.slabs_scanned = c->stats.slabs_scanned; s.pool_returns = c->stats.pool_returns;
    s.walk_nodes = __atomic_load_n(&c->stats.walk_nodes, __ATOMIC_RELAXED);
    s.ref_recount_bytes = c->stats.ref_recount_bytes;
    s.leaf_bytes = LEAF_SZ; s.branch_bytes = BRANCH_SZ; s.snapshot_bytes = SNAPSHOT_SZ;
    s.slab_overhead = SLAB_OVERHEAD; s.height = (unsigned)t->height;
    pthread_mutex_unlock(&c->pool_mu); return s;
}
piece_test_memory piece_test_get_memory(const piece_tree *t) {
    core *c = t->c; piece_test_memory m = {0};
    m.deleted_original = t->deleted_original; m.fallback_add = t->fallback_add;
    pthread_mutex_lock(&c->pool_mu);
    for (unsigned pi = 0; pi < 3; pi++) {
        size_t slot = slot_size(pi);
        for (node *slab = c->pool[pi].slabs; slab; slab = slab_end(slab, slot)->next) {
            m.slabs[pi]++;
            for (unsigned i = 0; i < 2; i++) {
                node *n = slab_slot(slab, slot, i);
                if (!atomic_load_explicit(&n->rc, memory_order_relaxed)) continue;
                m.live[pi]++;
                if (!pi) { m.leaf_pieces += n->cnt; if (n->cnt < FAN / 2) m.underfull_leaves++; }
            }
        }
    }
    pthread_mutex_unlock(&c->pool_mu); return m;
}
void piece_test_reset_stats(piece_tree *t) { memset(&t->c->stats, 0, sizeof t->c->stats); }
void piece_test_snapshot_set_owners(piece_snapshot *s, unsigned owners) {
    atomic_store_explicit(&s->rc, owners, memory_order_relaxed);
}
unsigned piece_test_snapshot_owners(const piece_snapshot *s) {
    return atomic_load_explicit(&s->rc, memory_order_relaxed);
}
/* Full subtrees hold 16^(height+1) one-byte pieces. The partial rightmost
 * path uses base-16 digits, so even UINT64_MAX costs only a few dozen nodes.
 * Shared children are ordinary refcounted nodes, with exact byte/newline sums. */
static node *test_repeat_partial(core *c, node *const full[15], unsigned height, uint64_t len) {
    node *n = node_new(c, height == 0);
    if (!height) {
        while (len--) { ent e = { 1, 0, TOP }; ent_put(n, n->cnt++, &e); }
    } else {
        uint64_t width = UINT64_C(1) << (4 * height);
        while (len >= width) {
            node *ch = full[height - 1];
            atomic_fetch_add_explicit(&ch->rc, 1, memory_order_relaxed);
            ent e = { width, 0, (uint64_t)(uintptr_t)ch }; ent_put(n, n->cnt++, &e);
            len -= width;
        }
        if (len) {
            node *ch = test_repeat_partial(c, full, height - 1, len);
            ent e = { len, 0, (uint64_t)(uintptr_t)ch }; ent_put(n, n->cnt++, &e);
        }
    }
    node_add_high(n); return n;
}
int piece_test_repeat_byte(piece_tree *t, uint64_t len) {
    if (t->len || !t->add_len || !len || *add_ptr(t->c, 0) == '\n') return PIECE_ERR_RANGE;
    core *c = t->c;
    if (pool_reserve_class(c, 0, 2) || pool_reserve_class(c, 1, 30)) return PIECE_ERR_NOMEM;
    node *full[15];
    for (unsigned h = 0; h < 15; h++) {
        full[h] = node_new(c, h == 0);
        for (unsigned i = 0; i < FAN; i++) {
            ent e = { 1, 0, TOP };
            if (h) {
                node *ch = full[h - 1];
                atomic_fetch_add_explicit(&ch->rc, 1, memory_order_relaxed);
                e.b = UINT64_C(1) << (4 * h); e.x = (uint64_t)(uintptr_t)ch;
            }
            ent_put(full[h], i, &e); full[h]->cnt++;
        }
        node_add_high(full[h]);
    }
    node *root = test_repeat_partial(c, full, 15, len);
    for (unsigned h = 15; h-- > 0;) node_unref(c, full[h]);
    snapshot_uncache(t); node_unref(c, t->root);
    int height = 15;
    while (!root->leaf && root->cnt == 1) {
        node *old = root; root = old->v.in.ch[0]; pool_free(c, old, 1); height--;
    }
    t->root = root; t->height = height; t->len = len; t->inited = 1;
    t->cursor_valid = 0; t->run_valid = 0;
    return PIECE_OK;
}
uint64_t piece_test_set_add_length(piece_tree *t, uint64_t len) {
    uint64_t old = t->add_len; t->add_len = len; return old;
}
piece_test_state piece_test_get_state(const piece_tree *t) {
    piece_test_state s = { t->root, t->cached_snapshot, t->len, node_total(t->root),
                          t->add_len, t->run_end, t->run_addend, t->run_valid, t->cursor_valid };
    return s;
}
#endif
