/* L1 piece: the persistent piece tree behind src/piece/piece.h (v2-0fv.17;
 * design, invariants and evidence in docs/decisions/v2-0fv.17.md; geometry
 * and admission in docs/decisions/wave1-piece-pool.md).
 *
 * Ownership. Only the UI changes node and directory reference counts. A
 * snapshot's last release, on any thread, pushes the header onto its core's
 * retirement inbox (one compare-and-swap) and then drops its core owner; the
 * UI drains the inbox. The core's last owner, on whatever thread, frees the
 * ADD mappings and hands every extent to the reserve's inbox as one chain:
 * nodes and headers need no walk, they leave with their pages.
 *
 * Storage. Leaves, branches and snapshot headers share one 400 B slot class
 * in core-owned page extents; ADD chunks, ADD directory nodes and the private
 * ORIGINAL copy are separate base mappings. Every call that can need slots
 * claims them from the current shape before it changes anything. */
#include "piece/piece.h"

#include <stdatomic.h>
#include <string.h>

#include "base/base.h"
#include "scan/scan.h"

#ifdef PIECE_TEST_HOOKS
#include "piece/piece_test.h"
#endif

#define PIECE_LEAF 24u
#define PIECE_BRANCH 16u
#define PIECE_BATCH (PIECE_LEAF - 2u)
#define PIECE_SLOT 400u
#define PIECE_LEVELS (PIECE_MAX_HEIGHT + 1u)
#define PIECE_MASK (PIECE_CHUNK - 1u)
#define PIECE_UNKNOWN PIECE_SPAN_ADD
#define PIECE_PREFIX 512u
#define PIECE_PREFIXES (PIECE_CHUNK / PIECE_PREFIX)
/* Six index bits per level. Eight directory levels address more than the
 * 47 chunk-index bits in the ADD coordinate. The root grows on demand. */
#define PIECE_DIR_BITS 6u
#define PIECE_DIR_FAN (1u << PIECE_DIR_BITS)

/* Owners of a core (its tree, one per live snapshot header) and of a
 * snapshot header (each retain). 64 bits cannot wrap: every owner occupies
 * storage of its own, so 2^64 owners cannot exist in a 2^47- or 2^56-byte
 * address space (P); the ASSERTs at each increment state that bound. */
typedef uint64_t piece_owners;

typedef struct piece_node piece_node;
typedef struct piece_add piece_add;
typedef struct piece_core piece_core;
typedef struct piece_extent piece_extent;

typedef struct piece_entry {
    uint64_t bytes, nl;
    union {
        uint64_t src;
        piece_node *child;
    } as;
} piece_entry;

typedef struct piece_leaf_entry {
    uint64_t src;
    uint32_t bytes, nl;
} piece_leaf_entry;

/* The 16 B prefix (x86-64: offset 0 refs, 4 count, 6 height, 7 padding,
 * 8 state) is signed with the 400 B slot. refs saturates (PIECE_REFS_STUCK). */
struct piece_node {
    uint32_t refs;
    uint16_t count;
    uint8_t height;
    union {
        uint64_t add_high;
        piece_node *retired;
    } state;
    union {
        piece_leaf_entry leaf[PIECE_LEAF];
        piece_entry branch[PIECE_BRANCH];
    } entries;
};

struct piece_add {
    size_t refs;
    piece_add *retired;
    unsigned level, count;
};

typedef struct piece_dir {
    piece_add head;
    piece_add *child[PIECE_DIR_FAN];
} piece_dir;

typedef struct piece_chunk {
    piece_add head;
    uint32_t total;
    uint16_t prefix[PIECE_PREFIXES];
    uint8_t bytes[PIECE_CHUNK];
} piece_chunk;

struct piece_extent {
    base_pool pool;
    piece_extent *prev, *next;
};

struct piece_reserve {
    _Atomic(piece_extent *) inbox;
    atomic_size_t cores;
    piece_extent *free;
    size_t free_extents, slots_per_extent, page, target_extents;
    piece_chunk *boundary;
    piece_add *dirs;
    size_t free_dirs;
};

struct piece_view {
    piece_core *core;
    piece_node *root;
    piece_add *add;
    uint64_t length, revision;
};

struct piece_snapshot {
    piece_view view;
    _Atomic(piece_owners) owners;
    piece_snapshot *retired;
};

struct piece_core {
    piece_reserve *reserve;
    _Atomic(piece_owners) owners;
    _Atomic(piece_snapshot *) inbox;
    piece_snapshot *retired;
    piece_node *nodes;
    piece_add *dirs;
    piece_extent *full, *partial;
    size_t free_slots;

    piece_backing backing;
    base_arena original;
    const uint8_t *bytes;
    size_t length;
    uint32_t *totals;
    const uint16_t *prefix;

#ifdef PIECE_TEST_HOOKS
    atomic_size_t mapped_reads, private_reads, query_visits;
    size_t resolve_visits, slots_claimed, live_slots, extents, add_chunks, add_dirs;
    /* Every slot drawn since the last claim fits that claim: live slots
     * stay at most claim_base + claim_limit (frees count as credit). */
    size_t claim_base, claim_limit;
    size_t retire_steps; /* retirement steps since piece_test_reset_reads */
    size_t resolve_touched; /* the last resolve call's touched nodes */
    void **held_slots;
#endif
};

/* The checkpoint lifecycle (piece.h Checkpoint; spec "Private ADD
 * boundary"). SHARED: open, and the restore view still shares ADD's tail
 * chunk with the live tree. PRIVATE: an inside take could observe bytes past
 * an unaligned mark, so the restore view now owns a private copy of the
 * chunk's prefix; abort appends into that copy, never into bytes a
 * surviving inside snapshot reads. */
typedef enum piece_cp {
    PIECE_CP_NONE,
    PIECE_CP_SHARED,
    PIECE_CP_PRIVATE
} piece_cp;

struct piece_tree {
    piece_view view;
    piece_snapshot *cached;
    piece_cp cp;
    piece_snapshot *checkpoint; /* the restore view; set exactly when cp != NONE */
    piece_add *restore_add;
    piece_chunk *tail, *restore_tail;
    uint64_t add_length, add_mark, resolve_offset;
    uint32_t mark_count;
};

/* Storage a command needs beside slots, admitted with them as one
 * requirement (A24): ADD chunks and directory nodes for appended bytes, or
 * the private ADD boundary of an open checkpoint. */
typedef enum piece_extra {
    PIECE_EXTRA_NONE,
    PIECE_EXTRA_ADD,
    PIECE_EXTRA_BOUNDARY
} piece_extra;

static bool piece_extra_local(const piece_tree *t, piece_extra extra, uint64_t add_bytes);
static piece_err piece_admit_fixed(piece_tree *t, size_t slots, piece_extra extra, uint64_t add_bytes,
                                   piece_add **dir);

_Static_assert(sizeof(piece_node) == PIECE_SLOT, "common slot size");
_Static_assert(offsetof(piece_node, entries) + sizeof(((piece_node *)0)->entries.leaf)
               == PIECE_SLOT, "leaf fills its slot");
_Static_assert(offsetof(piece_node, entries) + sizeof(((piece_node *)0)->entries.branch)
               == PIECE_SLOT, "branch fills its slot");
_Static_assert(sizeof(piece_snapshot) <= PIECE_SLOT && _Alignof(piece_snapshot) <= 16
               && _Alignof(piece_node) <= 16 && PIECE_SLOT % 16 == 0, "slot alignment");
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2 &&
               sizeof(piece_owners) == sizeof(unsigned long long),
               "lock-free retirement and 64-bit owner counts");

#ifdef PIECE_TEST_HOOKS
static unsigned piece_height_limit = PIECE_MAX_HEIGHT;
void piece_test_set_height_limit(unsigned levels)
{
    ASSERT(levels <= PIECE_MAX_HEIGHT);
    piece_height_limit = levels;
}
/* ADD directory levels walked or copied, process-wide (A7's begin check). */
static atomic_size_t piece_dir_steps;
#define PIECE_DIR_STEP() atomic_fetch_add_explicit(&piece_dir_steps, 1, memory_order_relaxed)
#else
#define piece_height_limit PIECE_MAX_HEIGHT
#define PIECE_DIR_STEP() ((void)0)
#endif

static uint64_t piece_min(uint64_t a, uint64_t b) { return a < b ? a : b; }

static uint64_t piece_chunks(uint64_t n)
{
    return n / PIECE_CHUNK + (n % PIECE_CHUNK != 0);
}

static bool piece_mapped(const piece_core *c) { return c->backing.bytes != NULL; }

static bool piece_pristine(os_fault_id id)
{
    uint64_t epoch;
    return os_fault_poll(id, &epoch) == OS_OK && epoch == 0;
}

static bool piece_trusted(const piece_core *c)
{
    return !piece_mapped(c) || piece_pristine(c->backing.fault);
}

static uint32_t piece_scan(const uint8_t *p, size_t n)
{
    return (uint32_t)scan_count(p, n).newlines;
}

/* Fixed allocations keep their native base reservation shape. The caller
 * knows the object size when freeing it, including on the final worker. */
static void *piece_memory(size_t size)
{
    base_arena a;
    return base_arena_init(&a, size) == 0 ? a.base : NULL;
}

static void piece_memory_free(void *p, size_t size)
{
    base_arena a = { .base = p, .size = size };
    base_arena_free(&a);
}

static void piece_extent_push(piece_extent **list, piece_extent *p)
{
    p->prev = NULL;
    p->next = *list;
    if (*list) (*list)->prev = p;
    *list = p;
}

static void piece_extent_remove(piece_extent **list, piece_extent *p)
{
    if (p->prev) p->prev->next = p->next;
    else *list = p->next;
    if (p->next) p->next->prev = p->prev;
}

static void piece_reserve_inbox(piece_reserve *r)
{
    piece_extent *list = atomic_exchange_explicit(&r->inbox, NULL, memory_order_acquire);
    while (list) {
        piece_extent *next = list->next;
        piece_extent_push(&r->free, list);
        r->free_extents++;
        list = next;
    }
}

static bool piece_extent_new(piece_reserve *r)
{
    base_pool pool;
    if (base_pool_init(&pool, PIECE_SLOT, 16, r->slots_per_extent) != 0) return false;
    piece_extent *p = (void *)(pool.base + r->slots_per_extent * PIECE_SLOT);
    p->pool = pool;
    piece_extent_push(&r->free, p);
    r->free_extents++;
    return true;
}

static void piece_extent_trim(piece_reserve *r, size_t keep)
{
    while (r->free_extents > keep) {
        piece_extent *p = r->free;
        piece_extent_remove(&r->free, p);
        r->free_extents--;
        base_pool pool = p->pool;
        base_pool_free(&pool);
    }
}

static void *piece_slot_get(piece_core *c)
{
    piece_reserve *r = c->reserve;
    if (!c->partial) {
        piece_extent *p = r->free;
        ASSERT(p != NULL);
        piece_extent_remove(&r->free, p);
        r->free_extents--;
        piece_extent_push(&c->partial, p);
        c->free_slots += r->slots_per_extent;
#ifdef PIECE_TEST_HOOKS
        c->extents++;
#endif
    }
    piece_extent *p = c->partial;
    void *slot = base_pool_get(&p->pool);
    ASSERT(slot != NULL);
    c->free_slots--;
    if (p->pool.live == r->slots_per_extent) {
        piece_extent_remove(&c->partial, p);
        piece_extent_push(&c->full, p);
    }
#ifdef PIECE_TEST_HOOKS
    c->live_slots++;
    ASSERT(c->live_slots <= c->claim_base + c->claim_limit); /* an under-claim */
#endif
    return slot;
}

static void piece_slot_put(piece_core *c, void *slot)
{
    piece_reserve *r = c->reserve;
    uintptr_t page = (uintptr_t)slot & ~(uintptr_t)(r->page - 1);
    piece_extent *p = (void *)(page + r->slots_per_extent * PIECE_SLOT);
    bool was_full = p->pool.live == r->slots_per_extent;
    base_pool_put(&p->pool, slot);
    c->free_slots++;
#ifdef PIECE_TEST_HOOKS
    c->live_slots--;
#endif
    if (was_full) {
        piece_extent_remove(&c->full, p);
        piece_extent_push(&c->partial, p);
    }
    if (p->pool.live == 0) {
        piece_extent_remove(&c->partial, p);
        c->free_slots -= r->slots_per_extent;
        piece_extent_push(&r->free, p);
        r->free_extents++;
#ifdef PIECE_TEST_HOOKS
        c->extents--;
#endif
    }
}

static piece_node *piece_node_new(piece_core *c, unsigned height)
{
    piece_node *n = piece_slot_get(c);
    n->refs = 1;
    n->count = 0;
    n->height = (uint8_t)height;
    n->state.add_high = 0;
    return n;
}

/* A node's references saturate (A24, node reference counts). The bound on
 * them is installed memory, not structure or address space: 2^32 references
 * need about 2^32 x 400 B = 1.7 TB of live slots (P), well inside a 2^47-byte
 * space, so an ASSERT would be wrong (A15). A 64-bit count does not fit the
 * signed 16 B prefix: count (5 bits), height (5 bits) and add_high (an ADD
 * offset below 2^56, 56 bits) leave 62 bits beside 64 (P). So the count
 * sticks at UINT32_MAX: a stuck node is never freed while its core lives
 * (its slot returns with the core's extents at the last release), a leak
 * of one node when about 1.7 TB is in use, never an early free and never an
 * abort. A stuck node reads as shared everywhere (refs > 1), so it is
 * copied, never written in place. */
#define PIECE_REFS_STUCK UINT32_MAX

static void piece_node_get(piece_node *n)
{
    if (n->refs != PIECE_REFS_STUCK) n->refs++;
}

/* Drops one reference; true when the node is now unreferenced. */
static bool piece_node_unref(piece_node *n)
{
    ASSERT(n->refs > 0);
    if (n->refs == PIECE_REFS_STUCK) return false;
    return --n->refs == 0;
}

static void piece_node_queue(piece_core *c, piece_node *n)
{
    if (piece_node_unref(n)) {
        n->state.retired = c->nodes;
        c->nodes = n;
    }
}

static void piece_node_drop(piece_core *c, piece_node *n)
{
    if (!piece_node_unref(n)) return;
    if (n->height)
        for (unsigned i = 0; i < n->count; i++)
            piece_node_drop(c, n->entries.branch[i].as.child);
    piece_slot_put(c, n);
}

static piece_node *piece_node_copy(piece_core *c, const piece_node *n)
{
    piece_node *copy = piece_node_new(c, n->height);
    copy->count = n->count;
    copy->state.add_high = n->state.add_high;
    if (n->height) {
        memcpy(copy->entries.branch, n->entries.branch, n->count * sizeof n->entries.branch[0]);
        for (unsigned i = 0; i < n->count; i++) piece_node_get(copy->entries.branch[i].as.child);
    } else {
        memcpy(copy->entries.leaf, n->entries.leaf, n->count * sizeof n->entries.leaf[0]);
    }
    return copy;
}

static piece_node *piece_node_own(piece_core *c, piece_node *n)
{
    if (n->refs == 1) return n;
    piece_node *copy = piece_node_copy(c, n);
    (void)piece_node_unref(n); /* refs > 1: n stays referenced */
    return copy;
}

static void piece_add_get(piece_add *a)
{
    if (a) a->refs++;
}

static void piece_add_queue(piece_core *c, piece_add *a)
{
    if (a && --a->refs == 0) {
        a->retired = c->dirs;
        c->dirs = a;
    }
}

static void piece_add_free(piece_core *c, piece_add *a)
{
#ifdef PIECE_TEST_HOOKS
    if (a->level) c->add_dirs--;
    else c->add_chunks--;
#else
    (void)c;
#endif
    piece_memory_free(a, a->level ? sizeof(piece_dir) : sizeof(piece_chunk));
}

static void piece_add_drop(piece_core *c, piece_add *a)
{
    if (!a || --a->refs != 0) return;
    if (a->level) {
        piece_dir *d = (piece_dir *)a;
        for (unsigned i = 0; i < a->count; i++) piece_add_drop(c, d->child[i]);
    }
    piece_add_free(c, a);
}

/* The UI drains the retirement queue (piece_reclaim, admission of edits and
 * companion storage, close; never resolve, A24). One step drops one
 * reference: a branch keeps its remaining children as the continuation, so
 * a large revision retires in bounded increments. Returns the steps done. */
static size_t piece_retire(piece_core *c, size_t budget)
{
    size_t done = 0;
    while (done < budget) {
        if (c->nodes) {
            piece_node *n = c->nodes;
            if (n->height && n->count)
                piece_node_queue(c, n->entries.branch[--n->count].as.child);
            else {
                c->nodes = n->state.retired;
                piece_slot_put(c, n);
            }
        } else if (c->dirs) {
            piece_add *a = c->dirs;
            if (a->level && a->count) piece_add_queue(c, ((piece_dir *)a)->child[--a->count]);
            else {
                c->dirs = a->retired;
                piece_add_free(c, a);
            }
        } else {
            if (!c->retired)
                c->retired = atomic_exchange_explicit(&c->inbox, NULL, memory_order_acquire);
            piece_snapshot *s = c->retired;
            if (!s) break;
            if (s->view.root) {
                piece_node_queue(c, s->view.root);
                s->view.root = NULL;
            } else if (s->view.add) {
                piece_add_queue(c, s->view.add);
                s->view.add = NULL;
            } else {
                c->retired = s->retired;
                piece_slot_put(c, s);
            }
        }
        done++;
    }
#ifdef PIECE_TEST_HOOKS
    c->retire_steps += done;
#endif
    return done;
}

static bool piece_retire_pending(const piece_core *c)
{
    return c->nodes || c->dirs || c->retired ||
           atomic_load_explicit(&c->inbox, memory_order_acquire) != NULL;
}

/* The last owner's walk. Nodes and headers leave with their extents, so
 * no node is visited and no slot returned one by one; only ADD chunks and
 * directory nodes, each its own base mapping, are freed: those that queued
 * retirement and every surviving snapshot's view still names. */
static void piece_core_final(piece_core *c)
{
    piece_snapshot *s = c->retired;
    while (s) {
        piece_add_queue(c, s->view.add);
        s = s->retired;
    }
    s = atomic_exchange_explicit(&c->inbox, NULL, memory_order_acquire);
    while (s) {
        piece_add_queue(c, s->view.add);
        s = s->retired;
    }
    while (c->dirs) {
        piece_add *a = c->dirs;
        if (a->level && a->count) piece_add_queue(c, ((piece_dir *)a)->child[--a->count]);
        else {
            c->dirs = a->retired;
            piece_add_free(c, a);
        }
    }
}

static void piece_core_drop(piece_core *c)
{
    if (atomic_fetch_sub_explicit(&c->owners, 1, memory_order_acq_rel) != 1) return;
    piece_reserve *r = c->reserve;
    piece_core_final(c);
    if (piece_mapped(c)) c->backing.release(c->backing.ctx);
    base_arena_free(&c->original);

    piece_extent *head = c->full ? c->full : c->partial;
    if (head) {
        piece_extent *tail = head;
        while (tail->next) tail = tail->next;
        if (c->full && c->partial) {
            tail->next = c->partial;
            tail = c->partial;
            while (tail->next) tail = tail->next;
        }
        for (piece_extent *p = head; p; p = p->next) base_pool_reset(&p->pool);
        piece_extent *inbox = atomic_load_explicit(&r->inbox, memory_order_relaxed);
        do { tail->next = inbox; }
        while (!atomic_compare_exchange_weak_explicit(&r->inbox, &inbox, head,
                                                      memory_order_release, memory_order_relaxed));
    }

    piece_memory_free(c, sizeof *c);
    atomic_fetch_sub_explicit(&r->cores, 1, memory_order_release);
}

static size_t piece_available(const piece_core *c)
{
    return c->free_slots + c->reserve->free_extents * c->reserve->slots_per_extent;
}

/* Funds `slots` free slots for c: its own, then the reserve's whole
 * extents (inbox first), then, with `retire`, its retirement queue, then
 * base. False only after base refused the remainder; with `retire` the queue
 * is then empty (drained before base and again after the refusal, for
 * releases that arrived meanwhile). */
static bool piece_fund(piece_core *c, size_t slots, bool retire)
{
    if (slots <= c->free_slots) return true;
    piece_reserve *r = c->reserve;
    piece_reserve_inbox(r);
    if (slots <= piece_available(c)) return true;
    if (retire)
        while (piece_retire(c, 1))
            if (slots <= piece_available(c)) return true;
    piece_reserve_inbox(r);
    while (slots > piece_available(c))
        if (!piece_extent_new(r)) {
            if (retire) (void)piece_retire(c, SIZE_MAX);
            piece_reserve_inbox(r);
            return slots <= piece_available(c);
        }
    return true;
}

static void piece_claim_note(piece_core *c, size_t slots)
{
#ifdef PIECE_TEST_HOOKS
    c->slots_claimed = slots;
    c->claim_base = c->live_slots;
    c->claim_limit = slots;
#else
    (void)c;
    (void)slots;
#endif
}

/* A26 step 1's slot half: true when the core's free slots and the reserve's
 * whole extents (its inbox taken first) cover `slots`. Neither drains
 * retirement nor asks base. */
static bool piece_local(piece_core *c, size_t slots)
{
    if (slots <= c->free_slots) return true;
    piece_reserve_inbox(c->reserve);
    return slots <= piece_available(c);
}

/* Resolve's claim: never walks the retirement queue (A24), so one call
 * visits at most its budget in nodes. */
static bool piece_claim_unretired(piece_core *c, size_t slots)
{
    piece_claim_note(c, slots);
    return piece_fund(c, slots, false);
}

/* spare: draw the reserve's prepaid boundary chunk or directory node first
 * (the private boundary exchange, spec "Private ADD boundary"). */
static piece_add *piece_add_new(piece_core *c, unsigned level, bool spare)
{
    piece_reserve *r = c->reserve;
    piece_add *a;
    if (spare && !level && r->boundary) {
        a = &r->boundary->head;
        r->boundary = NULL;
    } else if (spare && level && r->dirs) {
        a = r->dirs;
        r->dirs = a->retired;
        r->free_dirs--;
    } else {
        a = piece_memory(level ? sizeof(piece_dir) : sizeof(piece_chunk));
        if (!a) return NULL; /* the command's admission decides (piece_admit) */
    }
    *a = (piece_add){ .refs = 1, .level = level };
    if (!level) {
        piece_chunk *ch = (piece_chunk *)a;
        ch->total = 0;
        ch->prefix[0] = 0;
    }
#ifdef PIECE_TEST_HOOKS
    if (level) c->add_dirs++;
    else c->add_chunks++;
#endif
    return a;
}

static unsigned piece_dir_index(uint64_t index, unsigned level)
{
    return (unsigned)(index >> ((level - 1u) * PIECE_DIR_BITS)) & (PIECE_DIR_FAN - 1u);
}

static piece_chunk *piece_chunk_at(piece_add *a, uint64_t index)
{
    ASSERT(a != NULL);
    while (a->level) {
        PIECE_DIR_STEP();
        unsigned i = piece_dir_index(index, a->level);
        ASSERT(i < a->count && ((piece_dir *)a)->child[i]);
        a = ((piece_dir *)a)->child[i];
    }
    return (piece_chunk *)a;
}

/* Replace one path, retaining untouched children. Append admission may
 * reuse the private part of its unpublished directory. A shared ancestor
 * forces copies below it. The caller owns the supplied chunk on failure. */
static piece_add *piece_dir_put(piece_core *c, piece_add *old, unsigned level,
                                uint64_t index, piece_add *chunk, bool spare, bool exclusive)
{
    if (!level) {
        piece_add_get(chunk);
        return chunk;
    }
    PIECE_DIR_STEP();
    unsigned i = piece_dir_index(index, level);
    ASSERT(!old || old->level == level);
    piece_dir *src = (piece_dir *)old;
    piece_add *child = old && i < old->count ? src->child[i] : NULL;
    bool reuse = exclusive && old && old->refs == 1;
    piece_add *new_child = piece_dir_put(c, child, level - 1, index, chunk, spare, reuse);
    if (!new_child) return NULL;

    ASSERT(i <= (old ? old->count : 0));
    unsigned count = old && i < old->count ? old->count : i + 1;
    if (reuse) {
        piece_add_drop(c, child);
        src->child[i] = new_child;
        old->count = count;
        piece_add_get(old);
        return old;
    }

    piece_dir *dst = (piece_dir *)piece_add_new(c, level, spare);
    if (!dst) {
        piece_add_drop(c, new_child);
        return NULL;
    }
    dst->head.count = count;
    for (unsigned j = 0; j < count; j++) {
        if (j == i) dst->child[j] = new_child;
        else {
            dst->child[j] = src->child[j];
            piece_add_get(dst->child[j]);
        }
    }
    return &dst->head;
}

static piece_add *piece_dir_append(piece_core *c, piece_add *a, uint64_t index)
{
    piece_add *chunk = piece_add_new(c, 0, false);
    if (!chunk) return NULL;
    piece_add *next = NULL;
    if (a && index >> (a->level * PIECE_DIR_BITS)) {
        /* The old root is a completely full first child. The new
         * hierarchical right edge is the only other child. */
        piece_add *child = piece_dir_put(c, NULL, a->level, index, chunk, false, false);
        if (child) {
            piece_dir *top = (piece_dir *)piece_add_new(c, a->level + 1, false);
            if (top) {
                top->child[0] = a;
                top->child[1] = child;
                top->head.count = 2;
                piece_add_get(a);
                next = &top->head;
            } else piece_add_drop(c, child);
        }
    } else {
        next = piece_dir_put(c, a, a ? a->level : 0, index, chunk, false, true);
    }
    piece_add_drop(c, chunk);
    return next;
}

static bool piece_add_prepare(piece_tree *t, uint64_t n, piece_add **out)
{
    uint64_t mark = t->add_length;
    ASSERT(n < PIECE_SPAN_ADD && mark < PIECE_SPAN_ADD - n);
    uint64_t first = piece_chunks(mark);
    uint64_t end = piece_chunks(mark + n);
    piece_add *a = t->view.add;
    piece_add_get(a);
    for (uint64_t i = first; i < end; i++) {
        piece_add *next = piece_dir_append(t->view.core, a, i);
        piece_add_drop(t->view.core, a);
        if (!next) return false;
        a = next;
    }
    *out = a;
    return true;
}

static void piece_add_publish(piece_tree *t, piece_add *a, uint64_t n)
{
    piece_add_drop(t->view.core, t->view.add);
    t->view.add = a;
    t->add_length += n;
    t->tail = piece_chunk_at(a, (t->add_length - 1) / PIECE_CHUNK);
}

static uint32_t piece_add_write(piece_add *a, uint64_t start, const uint8_t *bytes, size_t n)
{
    piece_chunk *ch = piece_chunk_at(a, start / PIECE_CHUNK);
    size_t off = (size_t)(start & PIECE_MASK);
    ASSERT(n <= PIECE_CHUNK - off);
    uint32_t total = 0;
    while (n) {
        size_t take = (size_t)piece_min(n, PIECE_PREFIX - off % PIECE_PREFIX);
        memcpy(ch->bytes + off, bytes, take);
        uint32_t count = piece_scan(ch->bytes + off, take);
        total += count;
        ch->total += count;
        off += take;
        bytes += take;
        n -= take;
        if (off < PIECE_CHUNK && off % PIECE_PREFIX == 0)
            ch->prefix[off / PIECE_PREFIX] = (uint16_t)ch->total;
    }
    return total;
}

static bool piece_private_boundary(piece_tree *t)
{
    uint64_t mark = t->add_mark;
    size_t prefix = (size_t)(mark & PIECE_MASK);
    piece_core *c = t->view.core;
    const piece_chunk *old = t->restore_tail;
    piece_chunk *copy = (piece_chunk *)piece_add_new(c, 0, true);
    if (!copy) return false;
    memcpy(copy->bytes, old->bytes, prefix);
    memcpy(copy->prefix, old->prefix, (prefix / PIECE_PREFIX + 1) * sizeof copy->prefix[0]);
    copy->total = t->mark_count;

    piece_add *root = piece_dir_put(c, t->restore_add, t->restore_add->level,
                                    mark / PIECE_CHUNK, &copy->head, true, false);
    piece_add_drop(c, &copy->head);
    if (!root) return false;
    piece_add_drop(c, t->restore_add);
    t->restore_add = root;
    t->restore_tail = copy;
    t->cp = PIECE_CP_PRIVATE;
    return true;
}

/* The reserve's spare private boundary: one chunk and `levels` directory
 * nodes, what the boundary exchange copies (piece_private_boundary). The
 * missing ones come from base in a fixed order; a refusal returns the ones
 * this call took to base (A26 step 4). */
static bool piece_spares_make(piece_reserve *r, unsigned levels)
{
    bool chunk = !r->boundary;
    if (chunk && !(r->boundary = piece_memory(sizeof(piece_chunk)))) return false;
    size_t taken = 0;
    while (r->free_dirs < levels) {
        piece_add *dir = piece_memory(sizeof(piece_dir));
        if (!dir) {
            for (; taken; taken--) {
                piece_add *back = r->dirs;
                r->dirs = back->retired;
                r->free_dirs--;
                piece_memory_free(back, sizeof(piece_dir));
            }
            if (chunk) {
                piece_memory_free(r->boundary, sizeof(piece_chunk));
                r->boundary = NULL;
            }
            return false;
        }
        dir->retired = r->dirs;
        r->dirs = dir;
        r->free_dirs++;
        taken++;
    }
    return true;
}

static const uint8_t *piece_data(const piece_view *v, uint64_t src)
{
    if (!(src & PIECE_SPAN_ADD)) return v->core->bytes + src;
    uint64_t off = src & ~PIECE_SPAN_ADD;
    return piece_chunk_at(v->add, off / PIECE_CHUNK)->bytes + (off & PIECE_MASK);
}

static void piece_note_read(const piece_view *v, uint64_t src, size_t n)
{
#ifdef PIECE_TEST_HOOKS
    atomic_size_t *counter = !(src & PIECE_SPAN_ADD) && piece_mapped(v->core)
                             ? &v->core->mapped_reads : &v->core->private_reads;
    atomic_fetch_add_explicit(counter, n, memory_order_relaxed);
#else
    (void)v;
    (void)src;
    (void)n;
#endif
}

static uint32_t piece_read_count(const piece_view *v, uint64_t src, size_t n)
{
    if (!n) return 0;
    piece_note_read(v, src, n);
    return piece_scan(piece_data(v, src), n);
}

static bool piece_source_trusted(const piece_view *v, uint64_t src)
{
    return (src & PIECE_SPAN_ADD) || piece_trusted(v->core);
}

/* Half-open source chunk. Its last occupied byte determines whether a
 * truncated final chunk is a whole chunk. */
static size_t piece_original_chunk(const piece_core *c, uint64_t start)
{
    return (size_t)piece_min(PIECE_CHUNK, c->length - start);
}

/* Newlines in [0, at) of the private chunk that starts at coordinate
 * start: its 512 B block prefix plus at most 511 bytes read. A chunk total
 * is read only where it is final: at the end of a private ORIGINAL chunk
 * (immutable), or at the end of a full ADD chunk (no append reaches a full
 * chunk again; an abort rewrites only past an unaligned mark, and a view
 * that saw past it got the private boundary). Every other field read was
 * written before any byte the view can name, so any view on any thread may
 * call this while the UI appends. */
static uint32_t piece_private_prefix(const piece_view *v, uint64_t start, size_t at)
{
    uint64_t off = start & ~PIECE_SPAN_ADD;
    size_t block = at / PIECE_PREFIX;
    size_t partial = at % PIECE_PREFIX;
    uint64_t base = start + block * PIECE_PREFIX;
    if (start & PIECE_SPAN_ADD) {
        const piece_chunk *ch = piece_chunk_at(v->add, off / PIECE_CHUNK);
        if (at == PIECE_CHUNK) return ch->total;
        return ch->prefix[block] + piece_read_count(v, base, partial);
    }
    const piece_core *c = v->core;
    if (at == piece_original_chunk(c, off)) return c->totals[off / PIECE_CHUNK];
    return c->prefix[off / PIECE_PREFIX + block] + piece_read_count(v, base, partial);
}

/* Newlines in a private run [src, src + n) inside one chunk: at most two
 * partial blocks, 2 x 511 B, read. */
static uint32_t piece_private_count(const piece_view *v, uint64_t src, size_t n)
{
    size_t lo = (size_t)(src & PIECE_MASK);
    size_t hi = lo + n;
    uint64_t start = src - lo;
    if (!lo) return piece_private_prefix(v, start, hi);
    if (lo / PIECE_PREFIX == hi / PIECE_PREFIX) return piece_read_count(v, src, n);
    return piece_private_prefix(v, start, hi) - piece_private_prefix(v, start, lo);
}

static bool piece_private_source(const piece_view *v, uint64_t src)
{
    return (src & PIECE_SPAN_ADD) || !piece_mapped(v->core);
}

static piece_entry piece_entry_at(const piece_node *n, unsigned i)
{
    if (n->height) return n->entries.branch[i];
    piece_leaf_entry e = n->entries.leaf[i];
    return (piece_entry){ .bytes = e.bytes, .nl = e.nl == PIECE_NL_UNKNOWN ? PIECE_UNKNOWN : e.nl,
                          .as.src = e.src };
}

static void piece_entry_set(piece_node *n, unsigned i, piece_entry e)
{
    if (n->height) n->entries.branch[i] = e;
    else n->entries.leaf[i] = (piece_leaf_entry){ e.as.src, (uint32_t)e.bytes,
                         e.nl == PIECE_UNKNOWN ? PIECE_NL_UNKNOWN : (uint32_t)e.nl };
}

static void piece_add_high(piece_node *n)
{
    uint64_t high = 0;
    for (unsigned i = 0; i < n->count; i++) {
        piece_entry e = piece_entry_at(n, i);
        uint64_t end = n->height ? e.as.child->state.add_high :
                       (e.as.src & PIECE_SPAN_ADD) ? (e.as.src & ~PIECE_SPAN_ADD) + e.bytes : 0;
        if (end > high) high = end;
    }
    n->state.add_high = high;
}

static piece_entry piece_summary(piece_node *n)
{
    piece_entry e = { .as.child = n };
    for (unsigned i = 0; i < n->count; i++) {
        piece_entry child = piece_entry_at(n, i);
        e.bytes += child.bytes;
        /* Sums fit below the flag bit, so OR of flags commutes with addition. */
        e.nl = ((e.nl & ~PIECE_UNKNOWN) + (child.nl & ~PIECE_UNKNOWN)) |
               ((e.nl | child.nl) & PIECE_UNKNOWN);
    }
    return e;
}

static unsigned piece_capacity(const piece_node *n)
{
    return n->height ? PIECE_BRANCH : PIECE_LEAF;
}

static void piece_original_index(piece_core *c, const uint8_t *input, uint8_t *copy,
                                 uint16_t *prefix, size_t n)
{
    size_t off = 0;
    while (off < n) {
        uint32_t total = 0;
        size_t bytes = piece_original_chunk(c, off);
        for (size_t at = 0; at < bytes; at += PIECE_PREFIX) {
            size_t len = (size_t)piece_min(PIECE_PREFIX, bytes - at);
            prefix[(off + at) / PIECE_PREFIX] = (uint16_t)total;
            memcpy(copy + off + at, input + off + at, len);
            total += piece_scan(copy + off + at, len);
        }
        c->totals[off / PIECE_CHUNK] = total;
        off += bytes;
    }
}

static bool piece_original_setup(piece_core *c, const uint8_t *input, size_t n, bool mapped)
{
    c->bytes = input;
    c->length = n;
    if (!n) return true;
    size_t total_bytes = (size_t)piece_chunks(n) * sizeof(uint32_t);
    size_t prefix_bytes = (n / PIECE_PREFIX + (n % PIECE_PREFIX != 0)) * sizeof(uint16_t);
    size_t allocation = total_bytes + (mapped ? 0 : prefix_bytes + n);
    if (base_arena_init(&c->original, allocation) != 0) return false;
    c->totals = base_arena_alloc(&c->original, total_bytes, _Alignof(uint32_t));
    if (mapped) {
        memset(c->totals, 0xff, total_bytes);
    } else {
        uint16_t *prefix = base_arena_alloc(&c->original, prefix_bytes, _Alignof(uint16_t));
        uint8_t *copy = base_arena_alloc(&c->original, n, _Alignof(uint8_t));
        ASSERT(prefix && copy);
        piece_original_index(c, input, copy, prefix, n);
        c->bytes = copy;
        c->prefix = prefix;
    }
    return true;
}

static piece_node *piece_build(piece_core *c, uint64_t first, uint64_t count,
                                unsigned height, uint64_t capacity)
{
    piece_node *n = piece_node_new(c, height);
    if (!height) {
        for (unsigned i = 0; i < count; i++) {
            uint64_t src = (first + i) * PIECE_CHUNK;
            uint32_t lfs = c->totals[first + i];
            piece_entry e = { .as.src = src, .bytes = piece_original_chunk(c, src),
                              .nl = lfs == PIECE_NL_UNKNOWN ? PIECE_UNKNOWN : lfs };
            piece_entry_set(n, i, e);
            n->count++;
        }
    } else {
        uint64_t stride = capacity / PIECE_BRANCH;
        for (uint64_t off = 0; off < count; off += stride) {
            piece_node *child = piece_build(c, first + off, piece_min(count - off, stride),
                                            height - 1, stride);
            piece_entry_set(n, n->count++, piece_summary(child));
        }
    }
    return n;
}

piece_err piece_reserve_create(piece_reserve **out)
{
    ASSERT(out != NULL);
    *out = NULL;
    piece_reserve *r = piece_memory(sizeof *r);
    if (!r) return PIECE_ERR_NOMEM;
    *r = (piece_reserve){ .page = os_page_size() };
    atomic_init(&r->inbox, NULL);
    atomic_init(&r->cores, 0);
    ASSERT(r->page >= sizeof(piece_extent) + PIECE_SLOT && !(r->page & (r->page - 1)));
    r->slots_per_extent = (r->page - sizeof(piece_extent)) / PIECE_SLOT;
    ASSERT((r->slots_per_extent * PIECE_SLOT) % _Alignof(piece_extent) == 0);
    *out = r;
    return PIECE_OK;
}

void piece_reserve_destroy(piece_reserve *r)
{
    ASSERT(r != NULL && atomic_load_explicit(&r->cores, memory_order_acquire) == 0);
    piece_reserve_inbox(r);
    piece_extent_trim(r, 0);
    piece_memory_free(r->boundary, sizeof *r->boundary);
    while (r->dirs) {
        piece_add *next = r->dirs->retired;
        piece_memory_free(r->dirs, sizeof(piece_dir));
        r->dirs = next;
    }
    piece_memory_free(r, sizeof *r);
}

static piece_err piece_open(piece_reserve *r, const uint8_t *bytes, size_t n,
                            const piece_backing *backing, piece_tree **out)
{
    ASSERT(r && out && n <= PIECE_MAX_LENGTH && (!n || bytes));
    *out = NULL;
    piece_core *c = piece_memory(sizeof *c);
    if (!c) return PIECE_ERR_NOMEM;
    *c = (piece_core){ .reserve = r };
    atomic_init(&c->owners, 1);
    atomic_init(&c->inbox, NULL);
#ifdef PIECE_TEST_HOOKS
    atomic_init(&c->mapped_reads, 0);
    atomic_init(&c->private_reads, 0);
    atomic_init(&c->query_visits, 0);
#endif
    atomic_fetch_add_explicit(&r->cores, 1, memory_order_relaxed);

    piece_tree *t = piece_memory(sizeof *t);
    if (!t || !piece_original_setup(c, bytes, n, backing != NULL)) {
        piece_memory_free(t, sizeof *t);
        piece_core_drop(c);
        return PIECE_ERR_NOMEM;
    }
    uint64_t pieces = piece_chunks(n);
    uint64_t levels = (pieces + PIECE_LEAF - 1) / PIECE_LEAF;
    if (!levels) levels = 1;
    size_t slots = (size_t)levels;
    unsigned height = 0;
    uint64_t capacity = PIECE_LEAF;
    while (levels > 1) {
        levels = (levels + PIECE_BRANCH - 1) / PIECE_BRANCH;
        slots += (size_t)levels;
        capacity *= PIECE_BRANCH;
        height++;
    }
    piece_claim_note(c, slots);
    if (!piece_fund(c, slots, true)) { /* exact: the packed count, nothing to drain */
        piece_memory_free(t, sizeof *t);
        piece_core_drop(c);
        return PIECE_ERR_NOMEM;
    }
    if (backing) {
        c->backing = *backing;
        backing->acquire(backing->ctx);
    }
    *t = (piece_tree){ .view = { .core = c, .length = n,
                                 .root = piece_build(c, 0, pieces, height, capacity) } };
    *out = t;
    return PIECE_OK;
}

piece_err piece_open_copy(piece_reserve *r, const uint8_t *bytes, size_t n, piece_tree **out)
{
    return piece_open(r, bytes, n, NULL, out);
}

piece_err piece_open_mapped(piece_reserve *r, const piece_backing *b, piece_tree **out)
{
    ASSERT(b && b->bytes && b->len && b->acquire && b->release && b->fault.gen &&
           b->fault.slot < OS_FAULT_SLOTS);
    return piece_open(r, b->bytes, b->len, b, out);
}

const piece_view *piece_tree_view(const piece_tree *t)
{
    ASSERT(t);
    return &t->view;
}

uint64_t piece_add_length(const piece_tree *t)
{
    ASSERT(t);
    return t->add_length;
}

/* A cache reference is an ordinary snapshot owner. Cached takes hit even
 * after capture, which can extend ADD without changing any visible byte. */
piece_snapshot *piece_snapshot_retain(piece_snapshot *s)
{
    ASSERT(s);
    piece_owners old = atomic_fetch_add_explicit(&s->owners, 1, memory_order_relaxed);
    ASSERT(old && old != UINT64_MAX); /* owned; 2^64 owners cannot exist (piece_owners) */
    return s;
}

void piece_snapshot_release(piece_snapshot *s)
{
    ASSERT(s);
    piece_owners old = atomic_fetch_sub_explicit(&s->owners, 1, memory_order_acq_rel);
    ASSERT(old);
    if (old != 1) return;
    piece_core *c = s->view.core;
    piece_snapshot *head = atomic_load_explicit(&c->inbox, memory_order_relaxed);
    do { s->retired = head; }
    while (!atomic_compare_exchange_weak_explicit(&c->inbox, &head, s,
                                                  memory_order_release, memory_order_relaxed));
    piece_core_drop(c);
}

static void piece_uncache(piece_tree *t)
{
    if (t->cached) {
        piece_snapshot_release(t->cached);
        t->cached = NULL;
    }
}

static void piece_publish(piece_tree *t)
{
    t->view.revision++;
    t->resolve_offset = 0;
    piece_uncache(t);
}

piece_err piece_snapshot_take(piece_tree *t, piece_snapshot **out)
{
    ASSERT(t && out);
    *out = NULL;
    if (t->cached) {
        *out = piece_snapshot_retain(t->cached);
        return PIECE_OK;
    }
    piece_core *c = t->view.core;
    bool boundary = false;
    switch (t->cp) {
    case PIECE_CP_SHARED:
        boundary = (t->add_mark & PIECE_MASK) && t->view.root->state.add_high > t->add_mark;
        break;
    case PIECE_CP_NONE:
    case PIECE_CP_PRIVATE:
        break;
    }
    /* One header, exact (A26): the fast path when the slot and any boundary
     * storage are local, else steps 2-4. */
    piece_extra extra = boundary ? PIECE_EXTRA_BOUNDARY : PIECE_EXTRA_NONE;
    if (piece_local(c, 1) && piece_extra_local(t, extra, 0)) {
        bool made = !boundary || piece_private_boundary(t); /* from the reserve's spares */
        ASSERT(made);
        piece_claim_note(c, 1);
    } else {
        piece_err err = piece_admit_fixed(t, 1, extra, 0, NULL);
        if (err != PIECE_OK) return err;
    }

    piece_snapshot *s = piece_slot_get(c);
    s->view = t->view;
    s->retired = NULL;
    atomic_init(&s->owners, 2);
    piece_node_get(s->view.root);
    piece_add_get(s->view.add);
    piece_owners core_owners = atomic_fetch_add_explicit(&c->owners, 1, memory_order_relaxed);
    ASSERT(core_owners != UINT64_MAX); /* 2^64 owners cannot exist (piece_owners) */
    t->cached = s;
    *out = s;
    return PIECE_OK;
}

const piece_view *piece_snapshot_view(const piece_snapshot *s)
{
    ASSERT(s);
    return &s->view;
}

piece_err piece_checkpoint_begin(piece_tree *t)
{
    ASSERT(t && t->cp == PIECE_CP_NONE);
    piece_snapshot *s;
    piece_err err = piece_snapshot_take(t, &s);
    if (err != PIECE_OK) return err;
    t->cp = PIECE_CP_SHARED;
    t->checkpoint = s;
    t->add_mark = t->add_length;
    t->restore_add = t->view.add;
    piece_add_get(t->restore_add);
    t->restore_tail = t->tail;
    t->mark_count = (t->add_mark & PIECE_MASK) ? t->tail->total : 0;
    return PIECE_OK;
}

void piece_checkpoint_commit(piece_tree *t)
{
    ASSERT(t && t->cp != PIECE_CP_NONE);
    piece_snapshot_release(t->checkpoint);
    t->checkpoint = NULL;
    t->cp = PIECE_CP_NONE;
    piece_add_queue(t->view.core, t->restore_add);
    t->restore_add = NULL;
    t->restore_tail = NULL;
}

void piece_checkpoint_abort(piece_tree *t)
{
    ASSERT(t && t->cp != PIECE_CP_NONE);
    piece_core *c = t->view.core;
    const piece_view *saved = &t->checkpoint->view;
    piece_node_get(saved->root);
    piece_node_queue(c, t->view.root);
    piece_add_queue(c, t->view.add);
    t->view.root = saved->root;
    t->view.length = saved->length;
    t->view.add = t->restore_add;
    t->add_length = t->add_mark;
    t->tail = t->restore_tail;
    if (t->add_mark & PIECE_MASK) t->tail->total = t->mark_count;
    t->restore_add = NULL;
    t->restore_tail = NULL;
    piece_publish(t);
    piece_snapshot_release(t->checkpoint);
    t->checkpoint = NULL;
    t->cp = PIECE_CP_NONE;
}

static unsigned piece_insert_claim(unsigned height)
{
    return 2u * height + 3u;
}

static unsigned piece_delete_claim(unsigned height)
{
    return 4u * height + 3u;
}

piece_err piece_replenish(piece_tree *t)
{
    ASSERT(t);
    piece_core *c = t->view.core;
    piece_reserve *r = c->reserve;
    unsigned height = t->view.root->height;
    unsigned next = height < PIECE_MAX_HEIGHT ? height + 1u : height;
    size_t target = 1u + piece_delete_claim(height) + piece_insert_claim(next) + 1u;
    /* Extents of a core whose last owner let go after its close wait in the
     * inbox; take them even with no slot deficit, so the trim below returns
     * them to base (Astra 7b-G). */
    piece_reserve_inbox(r);
    unsigned levels = t->view.add ? t->view.add->level : 0;
    if (!piece_fund(c, target, true) ||
        (!piece_spares_make(r, levels) && (!piece_retire(c, SIZE_MAX) || !piece_spares_make(r, levels))))
        return PIECE_ERR_NOMEM;
    size_t shortfall = target > c->free_slots ? target - c->free_slots : 0;
    r->target_extents = (shortfall + r->slots_per_extent - 1) / r->slots_per_extent;
    piece_extent_trim(r, r->target_extents);
    return PIECE_OK;
}

bool piece_reclaim(piece_tree *t, size_t budget)
{
    ASSERT(t);
    (void)piece_retire(t->view.core, budget);
    return piece_retire_pending(t->view.core);
}

void piece_close(piece_tree *t)
{
    ASSERT(t && t->cp == PIECE_CP_NONE);
    piece_core *c = t->view.core;
    piece_reserve *r = c->reserve;
    piece_uncache(t);
    piece_node_queue(c, t->view.root);
    piece_add_queue(c, t->view.add);
    piece_memory_free(t, sizeof *t);
    /* With snapshots alive, return what only this tree held now. As the
     * last owner (no other can appear), the final walk frees the rest
     * without visiting a node. */
    if (atomic_load_explicit(&c->owners, memory_order_acquire) != 1) (void)piece_retire(c, SIZE_MAX);
    piece_core_drop(c);
    piece_reserve_inbox(r);
    /* Preserve only an already funded foreground allowance after closing. */
    piece_extent_trim(r, r->target_extents);
}

uint64_t piece_length(const piece_view *v)
{
    ASSERT(v);
    return v->length;
}

const piece_backing *piece_view_backing(const piece_view *v)
{
    ASSERT(v);
    return piece_mapped(v->core) ? &v->core->backing : NULL;
}

/* Cuts e at `at`. A counted entry counts one part and derives the other
 * by subtraction: a private one counts its left part from block prefixes
 * (at most 2 x 511 B read); a mapped one scans its shorter half (at most
 * PIECE_CHUNK / 2 = 32,768 B, piece.h Lines) and keeps the result only if
 * the closing poll reports epoch 0 and the count fits. An unknown entry's
 * halves stay unknown and nothing is read. */
static void piece_cut(const piece_view *v, piece_entry e, uint64_t at,
                      piece_entry *left, piece_entry *right)
{
    ASSERT(at && at < e.bytes);
    *left = e;
    left->bytes = at;
    *right = e;
    right->bytes -= at;
    right->as.src += at;
    if (e.nl == PIECE_UNKNOWN) return;

    bool private_source = piece_private_source(v, e.as.src);
    bool count_left = private_source || left->bytes <= right->bytes;
    piece_entry *counted = count_left ? left : right;
    piece_entry *derived = count_left ? right : left;
    uint32_t n = 0;
    if (e.nl)
        n = private_source ? piece_private_count(v, counted->as.src, (size_t)counted->bytes)
                           : piece_read_count(v, counted->as.src, (size_t)counted->bytes);
    if (!piece_source_trusted(v, e.as.src) || n > e.nl || e.nl - n > derived->bytes) {
        left->nl = right->nl = PIECE_UNKNOWN;
    } else {
        counted->nl = n;
        derived->nl = e.nl - n;
    }
}

/* Insertion ties go left, so the next batch stays in an already owned
 * path. Reads go right at an entry end, to the byte at that position. */
static unsigned piece_locate(const piece_node *n, uint64_t *off, bool left_tie)
{
    unsigned i = 0;
    while (i + 1 < n->count) {
        uint64_t bytes = piece_entry_at(n, i).bytes;
        if (*off < bytes || (left_tie && *off == bytes)) break;
        *off -= bytes;
        i++;
    }
    return i;
}

typedef struct piece_cursor {
    const piece_view *view;
    uint64_t off, revision;
    const piece_node *node[PIECE_LEVELS]; /* indexed by node height */
    uint8_t index[PIECE_LEVELS];
    uint64_t skip;
} piece_cursor;

_Static_assert(sizeof(piece_cursor) <= sizeof(piece_iter), "frozen iterator storage");

static piece_cursor piece_cursor_load(const piece_iter *it)
{
    piece_cursor c;
    memcpy(&c, it, sizeof c);
    return c;
}

static void piece_cursor_save(piece_iter *it, const piece_cursor *c)
{
    memcpy(it, c, sizeof *c);
}

static void piece_cursor_seek(piece_cursor *c, const piece_view *v, uint64_t off)
{
    ASSERT(v && off <= v->length);
    c->view = v;
    c->off = off;
    c->revision = v->revision;
    if (off == v->length) return;
    const piece_node *n = v->root;
    for (;;) {
        unsigned i = piece_locate(n, &off, false);
        c->node[n->height] = n;
        c->index[n->height] = (uint8_t)i;
        if (!n->height) break;
        n = n->entries.branch[i].as.child;
    }
    c->skip = off;
}

void piece_iter_init(piece_iter *it, const piece_view *v, uint64_t off)
{
    ASSERT(it);
    piece_cursor c = {0};
    piece_cursor_seek(&c, v, off);
    piece_cursor_save(it, &c);
}

void piece_iter_seek(piece_iter *it, uint64_t off)
{
    ASSERT(it);
    piece_cursor c = piece_cursor_load(it);
    piece_cursor_seek(&c, c.view, off);
    piece_cursor_save(it, &c);
}

static piece_entry piece_cursor_entry(const piece_cursor *c)
{
    return piece_entry_at(c->node[0], c->index[0]);
}

static void piece_cursor_advance(piece_cursor *c, uint64_t n)
{
    c->off += n;
    if (c->off == c->view->length) return;
    c->skip += n;
    if (c->skip < piece_cursor_entry(c).bytes) return;
    c->skip = 0;
    unsigned h = 0;
    const piece_node *node = c->node[0];
    while (++c->index[h] == node->count) {
        h++;
        ASSERT(h <= c->view->root->height);
        node = c->node[h];
    }
    while (h) {
        node = node->entries.branch[c->index[h]].as.child;
        h--;
        c->node[h] = node;
        c->index[h] = 0;
    }
}

size_t piece_iter_next(piece_iter *it, const uint8_t **p)
{
    ASSERT(it && p);
    piece_cursor c = piece_cursor_load(it);
    ASSERT(c.revision == c.view->revision);
    if (c.off == c.view->length) return 0;
    piece_entry e = piece_cursor_entry(&c);
    uint64_t n = e.bytes - c.skip;
    *p = piece_data(c.view, e.as.src + c.skip);
    piece_cursor_advance(&c, n);
    piece_cursor_save(it, &c);
    return (size_t)n;
}

static uint32_t piece_clipped_count(const piece_cursor *c, piece_entry e, uint64_t n)
{
    piece_entry discarded;
    if (c->skip) piece_cut(c->view, e, c->skip, &discarded, &e);
    if (n < e.bytes) piece_cut(c->view, e, n, &e, &discarded);
    return e.nl == PIECE_UNKNOWN ? PIECE_NL_UNKNOWN : (uint32_t)e.nl;
}

bool piece_iter_next_span(piece_iter *it, uint64_t end, piece_span *out, piece_span_nl *nl)
{
    ASSERT(it && out);
    piece_cursor c = piece_cursor_load(it);
    ASSERT(c.revision == c.view->revision);
    end = piece_min(end, c.view->length);
    if (c.off >= end) return false;

    uint64_t src = piece_cursor_entry(&c).as.src + c.skip;
    uint64_t length = 0;
    uint64_t first_chunk = src / PIECE_CHUNK;
    uint64_t chunk = first_chunk;
    uint32_t head = 0, total = 0;
    do {
        piece_entry e = piece_cursor_entry(&c);
        uint64_t start = e.as.src + c.skip;
        if (start != src + length) break;
        uint64_t n = piece_min(e.bytes - c.skip, end - c.off);
        if (nl) {
            if (start / PIECE_CHUNK != chunk) {
                if (chunk == first_chunk) head = total;
                total = 0;
                chunk = start / PIECE_CHUNK;
            }
            uint32_t count = piece_clipped_count(&c, e, n);
            total = total == PIECE_NL_UNKNOWN || count == PIECE_NL_UNKNOWN
                    ? PIECE_NL_UNKNOWN : total + count;
        }
        length += n;
        piece_cursor_advance(&c, n);
    } while (c.off < end);
    *out = (piece_span){ src, length };
    if (nl) *nl = chunk == first_chunk ? (piece_span_nl){ total, 0 }
                                       : (piece_span_nl){ head, total };
    piece_cursor_save(it, &c);
    return true;
}

uint8_t piece_byte_at(const piece_view *v, uint64_t off)
{
    ASSERT(v && off < v->length);
    piece_cursor c;
    piece_cursor_seek(&c, v, off);
    uint64_t src = piece_cursor_entry(&c).as.src + c.skip;
    piece_note_read(v, src, 1);
    return *piece_data(v, src);
}

static void piece_query_visit(const piece_view *v)
{
#ifdef PIECE_TEST_HOOKS
    atomic_fetch_add_explicit(&v->core->query_visits, 1, memory_order_relaxed);
#else
    (void)v;
#endif
}

bool piece_lines_before(const piece_view *v, uint64_t off, uint64_t *count)
{
    ASSERT(v && count && off <= v->length);
    uint64_t sum = 0;
    bool exact = true;
    const piece_node *n = v->root;
    while (off && n) {
        piece_query_visit(v);
        const piece_node *next = NULL;
        for (unsigned i = 0; i < n->count && off; i++) {
            piece_entry e = piece_entry_at(n, i);
            if (off >= e.bytes) {
                sum += e.nl & ~PIECE_UNKNOWN;
                exact &= !(e.nl & PIECE_UNKNOWN);
                off -= e.bytes;
            } else {
                if (n->height) next = e.as.child;
                else if (piece_private_source(v, e.as.src))
                    sum += piece_private_count(v, e.as.src, (size_t)off);
                else sum += piece_read_count(v, e.as.src, (size_t)off);
                break;
            }
        }
        n = next;
    }
    if (!piece_trusted(v->core)) {
        *count = 0;
        return false;
    }
    *count = sum;
    return exact;
}

bool piece_lines_start(const piece_view *v, uint64_t line, uint64_t *off, uint64_t *before)
{
    ASSERT(v && off && before);
    *off = *before = 0;
    if (!line) return true;

    bool exact = true;
    const piece_node *n = v->root;
    while (n) {
        piece_query_visit(v);
        const piece_node *next = NULL;
        for (unsigned i = 0; i < n->count; i++) {
            piece_entry e = piece_entry_at(n, i);
            if (!(e.nl & PIECE_UNKNOWN) && line > e.nl) {
                line -= e.nl;
                *before += e.nl;
                *off += e.bytes;
            } else if (n->height) {
                next = e.as.child;
                break;
            } else if (e.nl == PIECE_UNKNOWN) {
                exact = false;
                break;
            } else {
                /* One pass to the line-th LF of this entry (1 <= line <= its
                 * count). If the bytes hold fewer (a fault or an unsignalled
                 * overwrite), the entry blocks: *off stays at its start,
                 * *before the exact count before it (FAULTS decides). */
                const uint8_t *bytes = piece_data(v, e.as.src);
                const uint8_t *lf = scan_find_nth_newline(bytes, (size_t)e.bytes, line - 1u);
                piece_note_read(v, e.as.src, lf ? (size_t)(lf - bytes) + 1u : (size_t)e.bytes);
                if (lf) {
                    *off += (uint64_t)(lf - bytes) + 1u;
                    *before += line;
                } else exact = false;
                break;
            }
        }
        n = next;
    }
    if (!piece_trusted(v->core)) {
        *off = *before = 0;
        return false;
    }
    return exact;
}

bool piece_lines_count(const piece_backing *b, uint64_t first, uint32_t *counts, size_t n)
{
    ASSERT(b && counts && b->bytes);
    uint64_t chunks = piece_chunks(b->len);
    ASSERT(first <= chunks && n <= chunks - first);
    for (size_t i = 0; i < n; i++) {
        uint64_t start = (first + i) * PIECE_CHUNK;
        counts[i] = piece_scan(b->bytes + start, (size_t)piece_min(b->len - start, PIECE_CHUNK));
    }
    return piece_pristine(b->fault);
}

bool piece_lines_adopt(piece_tree *t, os_fault_id source, uint64_t first,
                       const uint32_t *counts, size_t n)
{
    ASSERT(t && (counts || n == 0));
    piece_core *c = t->view.core;
    uint64_t chunks = piece_chunks(c->length);
    ASSERT(piece_mapped(c) && first <= chunks && n <= chunks - first);
    bool disagree = false, new_chunk = false;
    for (size_t i = 0; i < n; i++) {
        ASSERT(counts[i] <= piece_original_chunk(c, (first + i) * PIECE_CHUNK));
        uint32_t old = c->totals[first + i];
        new_chunk |= old == PIECE_NL_UNKNOWN;
        disagree |= old != PIECE_NL_UNKNOWN && old != counts[i];
    }
    if (disagree || c->backing.fault.slot != source.slot || c->backing.fault.gen != source.gen ||
        !piece_pristine(source))
        return false;
    if (new_chunk) {
        memcpy(c->totals + first, counts, n * sizeof *counts);
        t->resolve_offset = 0;
    }
    return true;
}

/* Run the deletion geometry against bounded shadow nodes only after the
 * scalar upper bound was refused. The shadow uses the same splices and
 * merges and never reads source bytes. The signed bound counts every
 * possible copy even when the live node is exclusive. */
#define PIECE_DELETE_SCRATCH (4u * PIECE_MAX_HEIGHT + 3u)

typedef struct piece_shadow_node {
    piece_node node;
    bool copied_shared;
} piece_shadow_node;

typedef struct piece_delete_plan {
    piece_shadow_node shadow[PIECE_DELETE_SCRATCH];
    size_t used, peak;
    int64_t delta;
} piece_delete_plan;

static bool piece_shadow_holds(const piece_delete_plan *plan, const piece_node *node)
{
    uintptr_t offset = (uintptr_t)node - (uintptr_t)plan->shadow;
    return offset < plan->used * sizeof plan->shadow[0];
}

static piece_shadow_node *piece_shadow_of(piece_delete_plan *plan, piece_node *node)
{
    uintptr_t offset = (uintptr_t)node - (uintptr_t)plan->shadow;
    if (offset >= plan->used * sizeof plan->shadow[0]) return NULL;
    return &plan->shadow[offset / sizeof plan->shadow[0]];
}

static piece_node *piece_shadow_new(piece_delete_plan *plan, unsigned height,
                                    bool paid, bool copied_shared)
{
    ASSERT(plan->used < PIECE_DELETE_SCRATCH);
    if (paid) {
        plan->delta++;
        if (plan->delta > 0 && (uint64_t)plan->delta > plan->peak)
            plan->peak = (size_t)plan->delta;
    }
    piece_shadow_node *s = &plan->shadow[plan->used++];
    s->copied_shared = copied_shared;
    s->node.refs = 1;
    s->node.count = 0;
    s->node.height = (uint8_t)height;
    s->node.state.add_high = 0;
    return &s->node;
}

static piece_node *piece_edit_node_new(piece_core *c, piece_delete_plan *plan, unsigned height)
{
    return plan ? piece_shadow_new(plan, height, true, false) : piece_node_new(c, height);
}

static piece_node *piece_edit_node_own(piece_core *c, piece_delete_plan *plan, piece_node *n,
                                       bool inherited)
{
    if (!plan) return piece_node_own(c, n);
    if (piece_shadow_of(plan, n)) return n;
    bool shared = inherited || n->refs > 1;
    piece_node *copy = piece_shadow_new(plan, n->height, shared, shared);
    memcpy(copy, n, sizeof *copy);
    copy->refs = 1;
    return copy;
}

static bool piece_edit_inherited(piece_delete_plan *plan, piece_node *n)
{
    if (!plan) return false;
    piece_shadow_node *s = piece_shadow_of(plan, n);
    return s ? s->copied_shared : n->refs > 1;
}

static void piece_edit_node_drop(piece_core *c, piece_delete_plan *plan, piece_node *n,
                                 bool inherited, bool transfer_children)
{
    if (!plan) {
        piece_node_drop(c, n);
        return;
    }
    piece_shadow_node *s = piece_shadow_of(plan, n);
    if (!s && (inherited || n->refs > 1)) return;
    plan->delta--;
    if (n->height && !transfer_children) {
        for (unsigned i = 0; i < n->count; i++)
            piece_edit_node_drop(c, plan, n->entries.branch[i].as.child,
                                 s && s->copied_shared, false);
    }
}

/* Replace a contiguous run of entries. The caller transfers child ownership
 * through this function. Only the root and propagated siblings can grow. */
static piece_node *piece_splice(piece_core *c, piece_delete_plan *plan,
                                 piece_node *n, unsigned first,
                                 unsigned remove, const piece_entry *insert, unsigned count)
{
    unsigned total = n->count - remove + count;
    unsigned cap = piece_capacity(n);
    unsigned end = first + remove;
    unsigned tail = n->count - end;
    if (total <= cap) {
        if (tail && remove != count) {
            if (n->height)
                memmove(n->entries.branch + first + count, n->entries.branch + end,
                        tail * sizeof n->entries.branch[0]);
            else
                memmove(n->entries.leaf + first + count, n->entries.leaf + end,
                        tail * sizeof n->entries.leaf[0]);
        }
        for (unsigned i = 0; i < count; i++) piece_entry_set(n, first + i, insert[i]);
        n->count = (uint16_t)total;
        piece_add_high(n);
        return NULL;
    }

    ASSERT(total <= 2 * cap);
    piece_entry entries[2 * PIECE_LEAF];
    for (unsigned i = 0; i < first; i++) entries[i] = piece_entry_at(n, i);
    for (unsigned i = 0; i < count; i++) entries[first + i] = insert[i];
    for (unsigned i = 0; i < tail; i++) entries[first + count + i] = piece_entry_at(n, end + i);
    unsigned mid = (total + 1) / 2;
    piece_node *right = piece_edit_node_new(c, plan, n->height);
    n->count = (uint16_t)mid;
    right->count = (uint16_t)(total - mid);
    for (unsigned i = 0; i < mid; i++) piece_entry_set(n, i, entries[i]);
    for (unsigned i = mid; i < total; i++) piece_entry_set(right, i - mid, entries[i]);
    piece_add_high(n);
    piece_add_high(right);
    return right;
}

static piece_node *piece_root_grow(piece_core *c, piece_delete_plan *plan,
                                    piece_node *left, piece_node *right)
{
    ASSERT(left->height < piece_height_limit);
    piece_node *root = piece_edit_node_new(c, plan, left->height + 1u);
    root->count = 2;
    piece_entry_set(root, 0, piece_summary(left));
    piece_entry_set(root, 1, piece_summary(right));
    piece_add_high(root);
    return root;
}

static bool piece_mergeable(piece_entry left, piece_entry right)
{
    return left.as.src + left.bytes == right.as.src &&
           left.as.src / PIECE_CHUNK == right.as.src / PIECE_CHUNK &&
           (left.nl == PIECE_UNKNOWN) == (right.nl == PIECE_UNKNOWN);
}

static void piece_join_entries(piece_entry *left, piece_entry right)
{
    ASSERT(piece_mergeable(*left, right));
    left->bytes += right.bytes;
    if (left->nl != PIECE_UNKNOWN) left->nl += right.nl;
}

static void piece_merge_children(piece_core *c, piece_delete_plan *plan,
                                  piece_node *parent, unsigned i)
{
    piece_node *left = parent->entries.branch[i].as.child;
    piece_node *right = parent->entries.branch[i + 1].as.child;
    bool inherited = piece_edit_inherited(plan, parent);
    left = piece_edit_node_own(c, plan, left, inherited);
    for (unsigned j = 0; j < right->count; j++) {
        piece_entry e = piece_entry_at(right, j);
        if (left->height && !plan) piece_node_get(e.as.child);
        piece_entry_set(left, left->count++, e);
    }
    piece_add_high(left);
    piece_edit_node_drop(c, plan, right, inherited, true);
    piece_entry replacement = piece_summary(left);
    piece_node *split = piece_splice(c, plan, parent, i, 2, &replacement, 1);
    ASSERT(!split);
}

static void piece_merge_near(piece_core *c, piece_delete_plan *plan,
                              piece_node *parent, unsigned i)
{
    if (i >= parent->count) return;
    piece_node *child = parent->entries.branch[i].as.child;
    unsigned capacity = piece_capacity(child);
    if (child->count >= capacity / 2) return;
    if (i + 1 < parent->count &&
        child->count + parent->entries.branch[i + 1].as.child->count <= capacity)
        piece_merge_children(c, plan, parent, i);
    else if (i && child->count + parent->entries.branch[i - 1].as.child->count <= capacity)
        piece_merge_children(c, plan, parent, i - 1);
}

static void piece_delete_cut(const piece_view *v, const piece_delete_plan *plan,
                             piece_entry e, uint64_t off, piece_entry *left, piece_entry *right)
{
    if (!plan) {
        piece_cut(v, e, off, left, right);
        return;
    }
    *left = (piece_entry){ .as.src = e.as.src, .bytes = off, .nl = PIECE_UNKNOWN };
    *right = (piece_entry){ .as.src = e.as.src + off, .bytes = e.bytes - off, .nl = PIECE_UNKNOWN };
}

static piece_node *piece_delete_rec(const piece_view *v, piece_delete_plan *plan, piece_node *n,
                                    uint64_t lo, uint64_t hi, bool join)
{
    piece_core *c = v->core;
    unsigned first = piece_locate(n, &lo, false);
    unsigned last = piece_locate(n, &hi, true);
    unsigned count = 0;
    piece_entry keep[2] = {0};
    piece_entry left = piece_entry_at(n, first), right = piece_entry_at(n, last);
    if (!n->height) {
        piece_entry discard;
        if (lo) {
            piece_entry suffix;
            piece_delete_cut(v, plan, left, lo, &keep[count++], &suffix);
            if (first == last) {
                right = suffix;
                hi -= lo;
            }
        }
        bool has_right = hi < right.bytes;
        if (has_right) {
            piece_delete_cut(v, plan, right, hi, &discard, &keep[count]);
            count++;
        }
        unsigned remove = last - first + 1;
        if (join && (lo || first) && (has_right || last + 1 < n->count)) {
            piece_entry a = lo ? keep[0] : piece_entry_at(n, first - 1);
            piece_entry b = has_right ? keep[count - 1] : piece_entry_at(n, last + 1);
            if (piece_mergeable(a, b)) {
                piece_join_entries(&a, b);
                first -= !lo;
                remove += !lo;
                remove += !has_right;
                keep[0] = a;
                count = 1;
            }
        }
        return piece_splice(c, plan, n, first, remove, keep, count);
    }
    bool inherited = piece_edit_inherited(plan, n);
    if (first == last) {
        piece_node *child = left.as.child;
        if (!lo && hi == left.bytes) {
            piece_edit_node_drop(c, plan, child, inherited, false);
        } else {
            child = piece_edit_node_own(c, plan, child, inherited);
            piece_node *sibling = piece_delete_rec(v, plan, child, lo, hi, join);
            keep[count++] = piece_summary(child);
            if (sibling) keep[count++] = piece_summary(sibling);
        }
    } else {
        if (lo) {
            piece_node *child = piece_edit_node_own(c, plan, left.as.child, inherited);
            piece_node *split = piece_delete_rec(v, plan, child, lo, left.bytes, join);
            ASSERT(!split);
            keep[count++] = piece_summary(child);
        } else piece_edit_node_drop(c, plan, left.as.child, inherited, false);
        for (unsigned i = first + 1; i < last; i++)
            piece_edit_node_drop(c, plan, n->entries.branch[i].as.child, inherited, false);
        if (hi < right.bytes) {
            piece_node *child = piece_edit_node_own(c, plan, right.as.child, inherited);
            piece_node *split = piece_delete_rec(v, plan, child, 0, hi, join);
            ASSERT(!split);
            keep[count++] = piece_summary(child);
        } else piece_edit_node_drop(c, plan, right.as.child, inherited, false);
    }
    piece_node *sibling = piece_splice(c, plan, n, first, last - first + 1, keep, count);
    if (!sibling) {
        if (count) piece_merge_near(c, plan, n, first + count - 1);
        if (count == 2) piece_merge_near(c, plan, n, first);
    }
    return sibling;
}

static bool piece_delete_grows(const piece_node *n, uint64_t off, uint64_t len)
{
    while (n->count == piece_capacity(n)) {
        unsigned i = piece_locate(n, &off, false);
        piece_entry e = piece_entry_at(n, i);
        if (len >= e.bytes - off) return false;
        if (!n->height) return off != 0;
        n = e.as.child;
    }
    return false;
}

/* Takes ownership of root. Promotion first retains the only child so
 * dropping the old root transfers an owner even after a shared descent. */
static piece_node *piece_delete_apply(const piece_view *v, piece_delete_plan *plan,
                                      piece_node *root, uint64_t off, uint64_t len,
                                      bool join, bool inherited)
{
    piece_core *c = v->core;
    if (len == v->length) {
        if (!inherited && root->refs == 1 && !root->height) {
            root = piece_edit_node_own(c, plan, root, false);
            root->count = 0;
            root->state.add_high = 0;
            return root;
        }
        piece_edit_node_drop(c, plan, root, inherited, false);
        return piece_edit_node_new(c, plan, 0);
    }
    root = piece_edit_node_own(c, plan, root, inherited);
    piece_node *sibling = piece_delete_rec(v, plan, root, off, off + len, join);
    if (sibling) return piece_root_grow(c, plan, root, sibling);
    inherited = false;
    while (root->height && root->count == 1) {
        piece_node *child = root->entries.branch[0].as.child;
        if (!plan) piece_node_get(child);
        bool child_inherited = inherited || piece_edit_inherited(plan, root);
        piece_edit_node_drop(c, plan, root, inherited, true);
        inherited = child_inherited;
        root = child;
    }
    return root;
}

static size_t piece_delete_exact(const piece_view *v, uint64_t off, uint64_t len, bool compound)
{
    piece_delete_plan plan;
    plan.used = plan.peak = 0;
    plan.delta = 0;
    (void)piece_delete_apply(v, &plan, v->root, off, len, false, compound);
    return plan.peak;
}

typedef struct piece_gap {
    unsigned count, index;
} piece_gap;

typedef struct piece_frontier {
    piece_gap gap[PIECE_LEVELS];
    size_t copies;
    unsigned height;
    bool cut, extend;
} piece_frontier;

/* With a shadow (a planned deletion's result) a node is shared exactly when
 * it is not one of the shadow's copies (piece_plan_replace). */
static piece_frontier piece_frontier_at(const piece_node *n, uint64_t off, uint64_t append_src,
                                        const piece_delete_plan *shadow)
{
    piece_frontier f = { .height = n->height };
    bool shared = false;
    for (;;) {
        unsigned i = piece_locate(n, &off, true);
        shared |= shadow ? !piece_shadow_holds(shadow, n) : n->refs > 1;
        f.copies += shared;
        f.gap[n->height] = (piece_gap){ n->count, i };
        if (!n->height) {
            if (!n->count) return f;
            piece_entry e = piece_entry_at(n, i);
            f.cut = off && off < e.bytes;
            if (off == e.bytes) {
                f.gap[0].index++;
                f.extend = append_src && e.as.src + e.bytes == append_src &&
                           e.as.src / PIECE_CHUNK == append_src / PIECE_CHUNK;
            }
            return f;
        }
        n = n->entries.branch[i].as.child;
    }
}

/* Same left-tie rule and upper midpoint as splice. A sibling freezes one
 * half of the frontier and adds just one parent entry. */
static bool piece_simulate(piece_frontier f, uint64_t segments, size_t *claim)
{
    size_t slots = f.copies;
    while (segments) {
        unsigned n = (unsigned)piece_min(segments, PIECE_BATCH);
        segments -= n;
        unsigned total = f.gap[0].count + n + f.cut;
        unsigned at = f.gap[0].index + n + f.cut;
        f.cut = false;
        if (total <= PIECE_LEAF) {
            f.gap[0] = (piece_gap){ total, at };
            continue;
        }
        slots++;
        unsigned mid = (total + 1) / 2;
        bool right = at > mid;
        f.gap[0] = right ? (piece_gap){ total - mid, at - mid } : (piece_gap){ mid, at };
        unsigned level = 1;
        for (; level <= f.height; level++) {
            piece_gap *parent = &f.gap[level];
            parent->count++;
            parent->index += right;
            if (parent->count <= PIECE_BRANCH) break;
            slots++;
            mid = (parent->count + 1) / 2;
            right = parent->index >= mid;
            if (right) {
                parent->count -= mid;
                parent->index -= mid;
            } else parent->count = mid;
        }
        if (level > f.height) {
            if (f.height >= piece_height_limit) return false;
            f.gap[++f.height] = (piece_gap){ 2, right };
            slots++;
        }
    }
    *claim = slots;
    return true;
}

typedef struct piece_insert_path {
    piece_node *node[PIECE_LEVELS];
    unsigned index[PIECE_LEVELS];
    unsigned height;
    uint64_t cut;
} piece_insert_path;

static piece_insert_path piece_insert_path_open(const piece_view *v, uint64_t off)
{
    piece_core *c = v->core;
    piece_insert_path path = { .height = v->root->height };
    piece_node *n = piece_node_own(c, v->root);
    for (;;) {
        unsigned i = piece_locate(n, &off, true);
        path.node[n->height] = n;
        path.index[n->height] = i;
        if (!n->height) {
            if (n->count && off == piece_entry_at(n, i).bytes) path.index[0]++;
            else path.cut = off;
            return path;
        }
        piece_node *child = piece_node_own(c, n->entries.branch[i].as.child);
        n->entries.branch[i].as.child = child;
        n = child;
    }
}

/* On a split, the half no longer at the gap gets its final summary. The
 * active half may change again, so higher ancestors are refreshed only
 * when a split reaches them or the whole batch stream finishes. */
static void piece_insert_batch(const piece_view *v, piece_insert_path *path,
                                const piece_entry *entries, unsigned count,
                                piece_entry extension)
{
    piece_core *c = v->core;
    piece_node *left = path->node[0];
    unsigned at = path->index[0];
    piece_node *right;
    if (path->cut) {
        piece_entry parts[PIECE_LEAF];
        piece_cut(v, piece_entry_at(left, at), path->cut, &parts[0], &parts[count + 1]);
        memcpy(parts + 1, entries, count * sizeof entries[0]);
        right = piece_splice(c, NULL, left, at, 1, parts, count + 2);
        path->cut = 0;
        at++;
    } else {
        if (extension.bytes) {
            piece_entry previous = piece_entry_at(left, at - 1);
            piece_join_entries(&previous, extension);
            piece_entry_set(left, at - 1, previous);
        }
        right = piece_splice(c, NULL, left, at, 0, entries, count);
    }
    at += count;
    bool right_active = at > left->count;
    ASSERT(!right_active || right);
    path->node[0] = right_active ? right : left;
    path->index[0] = right_active ? at - left->count : at;

    for (unsigned h = 1; right; h++) {
        if (h > path->height) {
            path->node[h] = piece_root_grow(c, NULL, left, right);
            path->index[h] = right_active;
            path->height = h;
            break;
        }
        piece_entry children[] = { piece_summary(left), piece_summary(right) };
        left = path->node[h];
        unsigned child = path->index[h];
        right = piece_splice(c, NULL, left, child, 1, children, 2);
        child += right_active;
        right_active = child >= left->count;
        ASSERT(!right_active || right);
        path->node[h] = right_active ? right : left;
        path->index[h] = right_active ? child - left->count : child;
    }
}

typedef struct piece_input {
    const piece_span *spans;
    const piece_span_nl *records;
    const uint8_t *bytes;
    uint64_t used, segments, length;
    size_t count, at;
} piece_input;

static uint64_t piece_span_entries(piece_span span)
{
    return piece_chunks((span.src & PIECE_MASK) + span.len);
}

static uint64_t piece_reference_count(const piece_view *v, piece_span span, uint64_t used, size_t n,
                                       const piece_span_nl *record)
{
    uint64_t src = span.src + used;
    if (piece_private_source(v, src)) return piece_private_count(v, src, n);
    uint64_t chunk = src / PIECE_CHUNK;
    uint32_t total = v->core->totals[chunk];
    if ((src & PIECE_MASK) == 0 && n == piece_original_chunk(v->core, src) &&
        total != PIECE_NL_UNKNOWN)
        return total;
    uint32_t carry = PIECE_NL_UNKNOWN;
    if (record) {
        if (!used) carry = record->head;
        else if (used + n == span.len) carry = record->tail;
    }
    return carry == PIECE_NL_UNKNOWN ? PIECE_UNKNOWN : carry;
}

static piece_entry piece_feed(piece_input *input, const piece_view *v)
{
    piece_span span = input->spans[input->at];
    uint64_t src = span.src + input->used;
    size_t n = (size_t)piece_min(span.len - input->used, PIECE_CHUNK - (src & PIECE_MASK));
    uint64_t count;
    if (input->bytes) {
        count = piece_add_write(v->add, src & ~PIECE_SPAN_ADD, input->bytes, n);
        input->bytes += n;
    } else {
        const piece_span_nl *record = input->records ? &input->records[input->at] : NULL;
        count = piece_reference_count(v, span, input->used, n, record);
    }
    input->used += n;
    if (input->used == span.len) {
        input->used = 0;
        input->at++;
    }
    return (piece_entry){ .as.src = src, .bytes = n, .nl = count };
}

/* A count in one chunk: the sum of two parts, unknown if either is. Both
 * parts lie in one chunk, so the sum is at most PIECE_CHUNK. */
static uint32_t piece_nl_add(uint32_t a, uint32_t b)
{
    return a == PIECE_NL_UNKNOWN || b == PIECE_NL_UNKNOWN ? PIECE_NL_UNKNOWN : a + b;
}

/* The union's record, chunk by chunk on the PIECE_CHUNK grid (the store tag
 * is chunk aligned, so tagged coordinates keep it). The union's head is its
 * part in its first chunk, its tail its part in its last chunk (0 when that
 * is the same chunk). Each edge sums what each input holds in that chunk:
 * the left span always owns part of the first chunk (its own head); the
 * right span adds its head there exactly when it starts in that chunk. In
 * the last chunk the right span owns its tail (its head when it occupies one
 * chunk); the left span adds its tail exactly when it ends in that chunk,
 * which is when the right span starts inside it. Pure, O(1). */
piece_span_nl piece_span_nl_join(piece_span left, piece_span_nl left_nl,
                                 piece_span right, piece_span_nl right_nl)
{
    ASSERT(left.len && right.len && !((left.src ^ right.src) & PIECE_SPAN_ADD));
    ASSERT(right.src > left.src && right.src - left.src == left.len &&
           right.len - 1u <= UINT64_MAX - right.src);
    uint64_t first = left.src / PIECE_CHUNK;
    uint64_t left_last = (right.src - 1u) / PIECE_CHUNK;
    uint64_t right_first = right.src / PIECE_CHUNK;
    uint64_t last = (right.src + right.len - 1u) / PIECE_CHUNK;
    uint32_t head = left_nl.head;
    if (right_first == first) head = piece_nl_add(head, right_nl.head);
    if (last == first) return (piece_span_nl){ head, 0 };
    uint32_t tail = right_first == last ? right_nl.head : right_nl.tail;
    if (left_last == last) tail = piece_nl_add(tail, left_nl.tail);
    return (piece_span_nl){ head, tail };
}

/* This sanitizer-only check scans carried source ends to catch invalid
 * provenance. Diagnostic reads do not enter the production read ledger. */
static bool piece_record_check(const piece_view *v, piece_span s, piece_span_nl record)
{
    size_t first = (size_t)piece_min(s.len, PIECE_CHUNK - (s.src & PIECE_MASK));
    bool good = true;
    if (record.head != PIECE_NL_UNKNOWN) {
        uint32_t count = piece_scan(piece_data(v, s.src), first);
        bool trusted = piece_source_trusted(v, s.src);
        good = !trusted || count == record.head;
    }
    /* A one-chunk span has no tail part: a known tail must be 0, and
     * UNKNOWN claims nothing (piece.h checks known counts only). */
    if (first == s.len) return good && (record.tail == 0 || record.tail == PIECE_NL_UNKNOWN);
    if (record.tail != PIECE_NL_UNKNOWN) {
        uint64_t end = s.src + s.len;
        uint64_t last = (end - 1) & ~(uint64_t)PIECE_MASK;
        uint32_t count = piece_scan(piece_data(v, last), (size_t)(end - last));
        bool trusted = piece_source_trusted(v, last);
        good &= !trusted || count == record.tail;
    }
    return good;
}

static piece_node *piece_insert_apply(piece_view v, uint64_t off, piece_input input, bool extend)
{
    piece_insert_path path = piece_insert_path_open(&v, off);
    piece_entry extension = {0};
    if (extend) extension = piece_feed(&input, &v);
    do {
        piece_entry batch[PIECE_BATCH];
        unsigned count = 0;
        while (count < PIECE_BATCH && input.at < input.count)
            batch[count++] = piece_feed(&input, &v);
        piece_insert_batch(&v, &path, batch, count, extension);
        extension.bytes = 0;
    } while (input.at < input.count);

    for (unsigned h = 1; h <= path.height; h++) {
        piece_entry_set(path.node[h], path.index[h], piece_summary(path.node[h - 1]));
        piece_add_high(path.node[h]);
    }
    return path.node[path.height];
}

/* ---- Admission (piece.h STORAGE; A26) ----------------------------------
 *
 * Exact at quiescence; a sequence, not a loop: at most two plans and one
 * base request per call.
 * 1. Fast path: the scalar claim (begin, a deletion, a one-chunk insertion of
 *    copied bytes, a take) and the companion storage (room in the ADD tail
 *    chunk, the reserve's spare boundary) are both local: proceed, no plan, no
 *    base call. The typing path after a successful replenish.
 * 2. Otherwise the exact plan; nothing missing: proceed, no drain.
 * 3. Otherwise every local source once: drain this core's retirement queue,
 *    take the reserve and its inbox, plan again (the drain can remove the
 *    sharing that made copies necessary); nothing missing: proceed.
 * 4. Return to base every whole free extent beyond the exact slot need, then
 *    ask base once for exactly the remainder: granted, proceed; refused,
 *    PIECE_ERR_NOMEM with nothing changed.
 * A release on another thread can land just after base refused: that call
 * may be refused where a retry would not be. Nothing latches. */

typedef enum piece_plan_kind {
    PIECE_PLAN_FIXED,   /* a given slot count (takes, captures) */
    PIECE_PLAN_DELETE,  /* a deletion, run on shadow nodes */
    PIECE_PLAN_INSERT,  /* an insertion: its frontier simulation */
    PIECE_PLAN_REPLACE  /* a deletion, then an insertion simulated on the
                           deletion's shadow result */
} piece_plan_kind;

typedef struct piece_plan {
    piece_plan_kind kind;
    const piece_view *view; /* the shape planned on */
    uint64_t off, del;
    const piece_input *input;
    piece_frontier gap; /* INSERT: the frontier of the last plan */
    size_t slots;       /* FIXED: given; else the last plan's count */
} piece_plan;

/* REPLACE: the slots a compound replace holds at once. The deletion runs on
 * shadow nodes as it will run for real, its root owned twice (the staged
 * root keeps the old one alive); after it, every original node under the new
 * root is shared and every shadow node exclusive, so the insertion's frontier
 * is simulated on the shadow result with exactly the sharing the real
 * insertion meets. The need is the larger of the deletion's peak and its
 * final balance plus the insertion. */
static piece_err piece_plan_replace(piece_plan *p, uint64_t append_src)
{
    piece_delete_plan shadow;
    shadow.used = shadow.peak = 0;
    shadow.delta = 0;
    piece_node *root = piece_delete_apply(p->view, &shadow, p->view->root, p->off, p->del, false, true);
    piece_frontier gap = piece_frontier_at(root, p->off, append_src, &shadow);
    size_t insert;
    if (!piece_simulate(gap, p->input->segments - gap.extend, &insert)) return PIECE_ERR_RANGE;
    ASSERT(shadow.delta >= 0); /* originals are shared: only shadow nodes leave */
    size_t after = (size_t)shadow.delta + insert;
    p->slots = shadow.peak > after ? shadow.peak : after;
    return PIECE_OK;
}

static piece_err piece_plan_make(piece_plan *p, piece_core *c)
{
    piece_err err = PIECE_OK;
    uint64_t append_src = p->input && p->input->bytes ? p->input->spans[0].src : 0;
    switch (p->kind) {
    case PIECE_PLAN_FIXED:
        break;
    case PIECE_PLAN_DELETE:
        p->slots = piece_delete_exact(p->view, p->off, p->del, false);
        break;
    case PIECE_PLAN_INSERT:
        p->gap = piece_frontier_at(p->view->root, p->off, append_src, NULL);
        if (!piece_simulate(p->gap, p->input->segments - p->gap.extend, &p->slots)) err = PIECE_ERR_RANGE;
        break;
    case PIECE_PLAN_REPLACE:
        err = piece_plan_replace(p, append_src);
        break;
    }
#ifdef PIECE_TEST_HOOKS
    c->slots_claimed = p->slots;
#else
    (void)c;
#endif
    return err;
}

/* Companion storage already present: room in the ADD tail chunk, or the
 * reserve's spare boundary chunk and a directory node per level. */
static bool piece_extra_local(const piece_tree *t, piece_extra extra, uint64_t add_bytes)
{
    switch (extra) {
    case PIECE_EXTRA_NONE:
        return true;
    case PIECE_EXTRA_ADD:
        return piece_chunks(t->add_length) == piece_chunks(t->add_length + add_bytes);
    case PIECE_EXTRA_BOUNDARY: {
        const piece_reserve *r = t->view.core->reserve;
        return r->boundary && r->free_dirs >= t->restore_add->level;
    }
    }
    return false;
}

static bool piece_extra_make(piece_tree *t, piece_extra extra, uint64_t add_bytes, piece_add **dir)
{
    switch (extra) {
    case PIECE_EXTRA_NONE:
        return true;
    case PIECE_EXTRA_ADD:
        return piece_add_prepare(t, add_bytes, dir);
    case PIECE_EXTRA_BOUNDARY: {
        if (!piece_spares_make(t->view.core->reserve, t->restore_add->level)) return false;
        bool made = piece_private_boundary(t); /* from the spares: no base call */
        ASSERT(made);
        return true;
    }
    }
    return false;
}

static bool piece_plan_covered(const piece_tree *t, const piece_plan *plan, piece_extra extra,
                               uint64_t add_bytes)
{
    return plan->slots <= piece_available(t->view.core) && piece_extra_local(t, extra, add_bytes);
}

/* A26 steps 2-4 (step 1 is each caller's fast path). */
static piece_err piece_admit(piece_tree *t, piece_plan *plan, piece_extra extra, uint64_t add_bytes,
                             piece_add **dir)
{
    piece_core *c = t->view.core;
    piece_reserve *r = c->reserve;
    piece_err err = piece_plan_make(plan, c);
    if (err != PIECE_OK) return err;
    size_t bought = 0;
    bool granted = true;
    if (!piece_plan_covered(t, plan, extra, add_bytes)) {
        (void)piece_retire(c, SIZE_MAX);
        piece_reserve_inbox(r);
        err = piece_plan_make(plan, c);
        if (err != PIECE_OK) return err;
        if (!piece_plan_covered(t, plan, extra, add_bytes)) {
            /* Step 4's list, from this plan: the missing extents, then the
             * companion objects (piece_extra_make), each once. */
            size_t missing = plan->slots > c->free_slots ? plan->slots - c->free_slots : 0;
            piece_extent_trim(r, (missing + r->slots_per_extent - 1) / r->slots_per_extent);
            while (granted && plan->slots > piece_available(c))
                if ((granted = piece_extent_new(r))) bought++;
        }
    }
    if (!granted || !piece_extra_make(t, extra, add_bytes, dir)) {
        /* Every object this call took goes back to base: the extents it
         * pushed on the reserve's free list (piece_extent_new pushes at the
         * head), the companion objects (their own cleanup). */
        piece_extent_trim(r, r->free_extents - bought);
        return PIECE_ERR_NOMEM;
    }
    piece_claim_note(c, plan->slots);
    return PIECE_OK;
}

static piece_err piece_admit_fixed(piece_tree *t, size_t slots, piece_extra extra, uint64_t add_bytes,
                                   piece_add **dir)
{
    piece_plan plan = { .kind = PIECE_PLAN_FIXED, .slots = slots };
    return piece_admit(t, &plan, extra, add_bytes, dir);
}

static piece_err piece_edit(piece_tree *t, uint64_t off, uint64_t del, piece_input input)
{
    piece_view view = t->view;
    piece_core *c = view.core;
    bool compound = del && input.length;
    if (!del && !input.length) return PIECE_OK;
    unsigned height = view.root->height;
    if (del && height >= piece_height_limit && piece_delete_grows(view.root, off, del))
        return PIECE_ERR_RANGE;
    /* Companion storage from what is inserted, not from the pointer: a
     * deletion may pass bytes with n == 0 and must prepare (and so hold) no
     * ADD reference (Astra's admission read, step 1). */
    bool copies = input.bytes && input.length;
    piece_extra extra = copies ? PIECE_EXTRA_ADD : PIECE_EXTRA_NONE;
    piece_add *dir = view.add;

    /* 1. The scalar claims: 4h + 3 to delete; 2h' + 3 to insert one chunk of
     * copied bytes, h' the height the deletion can leave (piece.h Editing). */
    bool scalar = !input.length || (input.bytes && input.segments == 1);
    unsigned grown = del && height < PIECE_MAX_HEIGHT ? height + 1u : height;
    size_t claim = (del ? piece_delete_claim(height) : 0) + (input.length ? piece_insert_claim(grown) : 0);
    bool fast = scalar && piece_local(c, claim) && piece_extra_local(t, extra, input.length);
    piece_plan plan = { .kind = compound ? PIECE_PLAN_REPLACE : del ? PIECE_PLAN_DELETE : PIECE_PLAN_INSERT,
                        .view = &view, .off = off, .del = del, .input = &input };
    if (fast) {
        piece_claim_note(c, claim);
        bool made = !copies || piece_add_prepare(t, input.length, &dir); /* within the tail chunk */
        ASSERT(made);
    } else {
        piece_err err = piece_admit(t, &plan, extra, input.length, &dir);
        if (err != PIECE_OK) return err;
    }

    if (!input.length) {
        t->view.root = piece_delete_apply(&view, NULL, view.root, off, del, fast, false);
    } else {
        if (compound) {
            piece_node_get(view.root);
            view.root = piece_delete_apply(&view, NULL, view.root, off, del, false, false);
        }
        piece_frontier gap = plan.gap;
        if (fast || compound) {
            size_t unused;
            gap = piece_frontier_at(view.root, off, input.bytes ? input.spans[0].src : 0, NULL);
            /* Planned compound edits checked the height on the shadow. */
            if (fast && view.root->height >= piece_height_limit &&
                !piece_simulate(gap, input.segments - gap.extend, &unused)) {
                if (input.bytes) piece_add_drop(c, dir);
                if (compound) piece_node_drop(c, view.root);
                return PIECE_ERR_RANGE;
            }
        }
        view.add = dir;
        view.root = piece_insert_apply(view, off, input, gap.extend);
        if (input.bytes) piece_add_publish(t, view.add, input.length);
        if (compound) piece_node_drop(c, t->view.root);
        t->view.root = view.root;
    }
    t->view.length = view.length - del + input.length;
    piece_publish(t);
    return PIECE_OK;
}

piece_err piece_replace(piece_tree *t, uint64_t off, uint64_t del, const uint8_t *bytes, size_t n)
{
    ASSERT(t && (!n || bytes) && off <= t->view.length && del <= t->view.length - off);
    ASSERT(n <= PIECE_MAX_LENGTH - (t->view.length - del));
    piece_span span = { .src = PIECE_SPAN_ADD | t->add_length, .len = n };
    piece_input input = { .spans = &span, .bytes = bytes, .length = n,
                          .segments = piece_span_entries(span), .count = n != 0 };
    return piece_edit(t, off, del, input);
}

piece_err piece_replace_spans(piece_tree *t, uint64_t off, uint64_t del, const piece_span *spans,
                              const piece_span_nl *nl, size_t count)
{
    ASSERT(t && (!count || spans) && off <= t->view.length && del <= t->view.length - off);
    piece_input input = { .spans = spans, .records = nl, .count = count };
    uint64_t remaining = PIECE_MAX_LENGTH - (t->view.length - del);
    for (size_t i = 0; i < count; i++) {
        piece_span s = spans[i];
        uint64_t store = s.src & PIECE_SPAN_ADD ? t->add_length : t->view.core->length;
        uint64_t start = s.src & ~PIECE_SPAN_ADD;
        ASSERT(s.len && start < store && s.len <= store - start && s.len <= remaining);
        remaining -= s.len;
        input.length += s.len;
        input.segments += piece_span_entries(s);
        if (OS_SAN_CHECKS && nl) {
            bool valid = piece_record_check(&t->view, s, nl[i]);
            OS_SAN_ASSERT(valid); /* Recounting mapped source ends is diagnostic only. */
        }
    }
    return piece_edit(t, off, del, input);
}

piece_err piece_capture(piece_tree *t, uint64_t off, uint64_t len, piece_span *out)
{
    ASSERT(t && out && len && off <= t->view.length && len <= t->view.length - off);
    piece_add *add;
    if (piece_extra_local(t, PIECE_EXTRA_ADD, len)) {
        bool made = piece_add_prepare(t, len, &add); /* within the tail chunk: no base */
        ASSERT(made);
    } else {
        piece_err err = piece_admit_fixed(t, 0, PIECE_EXTRA_ADD, len, &add);
        if (err != PIECE_OK) return err;
    }
    piece_cursor cursor;
    piece_cursor_seek(&cursor, &t->view, off);
    uint64_t start = t->add_length;
    uint64_t written = 0;
    while (written < len) {
        piece_entry e = piece_cursor_entry(&cursor);
        uint64_t src = e.as.src + cursor.skip;
        size_t n = (size_t)piece_min(e.bytes - cursor.skip, len - written);
        n = (size_t)piece_min(n, PIECE_CHUNK - ((start + written) & PIECE_MASK));
        piece_note_read(&t->view, src, n);
        (void)piece_add_write(add, start + written, piece_data(&t->view, src), n);
        piece_cursor_advance(&cursor, n);
        written += n;
    }
    piece_add_publish(t, add, len);
    *out = (piece_span){ PIECE_SPAN_ADD | start, len };
    return PIECE_OK;
}

/* One resolve call (piece.h Lines; A24 on Astra 7b-D and -E). The sweep
 * walks the live tree in document order from the cursor, one budget unit
 * per node entered. Ownership is decided on the visited path: a node is
 * shared when it or an ancestor on that path has another owner (a copied
 * parent shares its children). An entry under exclusive nodes only is
 * rewritten in place and needs no slot; a shared path is copied up to the
 * first exclusive ancestor, one claimed slot per copy, funded from free
 * slots, the reserve and base, never from the retirement queue. When a copy
 * cannot be funded the sweep stops there and keeps what it attached before;
 * the cursor stays at or before the first entry it could not publish. */
typedef struct piece_sweep {
    const piece_view *view;
    size_t budget;
    uint64_t offset;  /* entries ending at or before it are done */
    size_t rewrites;  /* leaves rewritten, in place or by copy */
    bool copied;      /* a node of the live tree was replaced by a copy */
    bool stopped;     /* a copy could not be funded */
} piece_sweep;

/* Returns the node to install for n: n itself (unchanged, or rewritten in
 * place) or a copy of n holding the rewrites. */
static piece_node *piece_resolve_node(piece_sweep *sweep, piece_node *n, uint64_t start,
                                      bool shared)
{
    piece_core *c = sweep->view->core;
    ASSERT(sweep->budget);
    sweep->budget--;
#ifdef PIECE_TEST_HOOKS
    c->resolve_visits++;
    c->resolve_touched++;
#endif
    shared = shared || n->refs > 1;
    if (!n->height) {
        uint32_t counts[PIECE_LEAF];
        uint32_t changes = 0;
        for (unsigned i = 0; i < n->count; i++) {
            piece_entry e = piece_entry_at(n, i);
            start += e.bytes;
            if (start <= sweep->offset || e.nl != PIECE_UNKNOWN) continue;
            uint64_t chunk = e.as.src / PIECE_CHUNK;
            uint32_t total = c->totals[chunk];
            if (total == PIECE_NL_UNKNOWN) continue;
            if (!(e.as.src & PIECE_MASK) && e.bytes == piece_original_chunk(c, e.as.src))
                counts[i] = total;
            else counts[i] = piece_read_count(sweep->view, e.as.src, (size_t)e.bytes);
            changes |= UINT32_C(1) << i;
        }
        if (!changes || !piece_trusted(c)) {
            sweep->offset = start;
            return n;
        }
        piece_node *dst = n;
        if (shared) {
            if (!piece_claim_unretired(c, 1)) {
                sweep->stopped = true;
                return n;
            }
            dst = piece_node_copy(c, n);
            sweep->copied = true;
        }
        for (unsigned i = 0; i < dst->count; i++)
            if (changes & (UINT32_C(1) << i)) dst->entries.leaf[i].nl = counts[i];
        sweep->rewrites++;
        sweep->offset = start;
        return dst;
    }

    piece_node *dst = n;
    for (unsigned i = 0; i < n->count; i++) {
        piece_entry e = piece_entry_at(n, i);
        uint64_t end = start + e.bytes;
        if (end > sweep->offset) {
            if (!(e.nl & PIECE_UNKNOWN)) {
                sweep->offset = end;
            } else {
                if (!sweep->budget || sweep->stopped) break;
                /* The whole sweep state before the child: a discarded copy
                 * restores all of it at once (Astra 7b re-read H). */
                piece_sweep before = *sweep;
                size_t rewrites = sweep->rewrites;
                piece_node *child = e.as.child;
                piece_node *got = piece_resolve_node(sweep, child, start, shared);
                if (got != child) {
                    /* A copy below a shared n needs n copied too; an
                     * exclusive n takes it in place, dropping its own
                     * reference to the shared original. */
                    if (dst == n && shared) {
                        if (!piece_claim_unretired(c, 1)) {
                            /* Discard the child's copy: the sweep is as it
                             * was before the child (cursor, rewrites, the
                             * copied flag), now stopped. */
                            piece_node_drop(c, got);
                            before.stopped = true;
                            *sweep = before;
                            break;
                        }
                        dst = piece_node_copy(c, n);
#ifdef PIECE_TEST_HOOKS
                        c->resolve_touched += n->count; /* the copy's child references */
#endif
                        sweep->copied = true;
                    }
                    piece_node_drop(c, dst->entries.branch[i].as.child);
                    piece_entry_set(dst, i, piece_summary(got));
                } else if (sweep->rewrites != rewrites) {
                    piece_entry_set(dst, i, piece_summary(child)); /* rewritten in place */
                }
                if (sweep->offset < end) break;
            }
        }
        start = end;
    }
    return dst;
}

piece_err piece_lines_resolve(piece_tree *t, size_t budget, bool *more)
{
    ASSERT(t && more);
    const piece_view *v = &t->view;
    if (!piece_mapped(v->core) || t->resolve_offset == v->length) {
        *more = false;
        return PIECE_OK;
    }
    if (!budget) {
        *more = true;
        return PIECE_OK;
    }
    piece_core *c = v->core;
#ifdef PIECE_TEST_HOOKS
    c->resolve_visits = 0;
    c->resolve_touched = 0;
#endif
    piece_sweep sweep = { .view = v, .budget = budget, .offset = t->resolve_offset };
    piece_node *root = piece_resolve_node(&sweep, v->root, 0, false);
    /* A copy that could not be funded before anything was rewritten:
     * nothing changed, cursor included. */
    if (sweep.stopped && !sweep.rewrites) return PIECE_ERR_NOMEM;
    if (root != v->root) {
        piece_node_drop(c, v->root);
        t->view.root = root;
    }
    /* A replaced node (the root, or one under an exclusive root) may be
     * named by a live iterator or the snapshot cache: end both. */
    if (sweep.copied) piece_publish(t);
    t->resolve_offset = sweep.offset;
    *more = sweep.offset < v->length;
    return PIECE_OK;
}

#ifdef PIECE_TEST_HOOKS
void piece_test_reset_reads(piece_tree *t)
{
    piece_core *c = t->view.core;
    atomic_store_explicit(&c->mapped_reads, 0, memory_order_relaxed);
    atomic_store_explicit(&c->private_reads, 0, memory_order_relaxed);
    atomic_store_explicit(&c->query_visits, 0, memory_order_relaxed);
    atomic_store_explicit(&piece_dir_steps, 0, memory_order_relaxed);
    c->resolve_visits = 0;
    c->retire_steps = 0;
}

static size_t piece_test_inbox_extents(piece_reserve *r)
{
    size_t n = 0;
    for (piece_extent *p = atomic_load_explicit(&r->inbox, memory_order_acquire); p; p = p->next) n++;
    return n;
}

uint64_t piece_test_snapshot_owners(const piece_snapshot *s)
{
    return atomic_load_explicit(&s->owners, memory_order_relaxed);
}

/* The leaf of t's live tree that holds offset off. */
static piece_node *piece_test_leaf_at(const piece_tree *t, uint64_t off)
{
    piece_node *n = t->view.root;
    while (n->height) n = n->entries.branch[piece_locate(n, &off, false)].as.child;
    return n;
}

uint32_t piece_test_node_refs(const piece_tree *t, uint64_t off)
{
    return piece_test_leaf_at(t, off)->refs;
}

void piece_test_set_node_refs(piece_tree *t, uint64_t off, uint32_t refs)
{
    piece_test_leaf_at(t, off)->refs = refs;
}

void piece_test_shift_owners(piece_tree *t, piece_snapshot *s, int64_t delta)
{
    _Atomic(piece_owners) *count = s ? &s->owners : &t->view.core->owners;
    atomic_fetch_add_explicit(count, (piece_owners)delta, memory_order_relaxed);
}

static size_t piece_round_pages(size_t bytes, size_t page)
{
    return (bytes + page - 1) / page;
}

piece_test_stats piece_test_measure(const piece_tree *t)
{
    const piece_core *c = t->view.core;
    const piece_reserve *r = c->reserve;
    return (piece_test_stats){
        .height = t->view.root->height, .root_entries = t->view.root->count,
        .revision = t->view.revision,
        .slots_claimed = c->slots_claimed, .live_slots = c->live_slots,
        .available_slots = piece_available(c), .slots_per_extent = r->slots_per_extent,
        .extents = c->extents, .free_extents = r->free_extents,
        .page = r->page, .chunk_pages = piece_round_pages(sizeof(piece_chunk), r->page),
        .dir_pages = piece_round_pages(sizeof(piece_dir), r->page),
        .core_pages = piece_round_pages(sizeof(piece_core), r->page),
        .tree_pages = piece_round_pages(sizeof(piece_tree), r->page),
        .reserve_pages = piece_round_pages(sizeof(piece_reserve), r->page),
        .add_chunks = c->add_chunks, .add_dirs = c->add_dirs,
        .spare_chunks = r->boundary != NULL, .spare_dirs = r->free_dirs,
        .mapped_reads = atomic_load_explicit(&c->mapped_reads, memory_order_relaxed),
        .private_reads = atomic_load_explicit(&c->private_reads, memory_order_relaxed),
        .query_visits = atomic_load_explicit(&c->query_visits, memory_order_relaxed),
        .resolve_visits = c->resolve_visits,
        .dir_steps = atomic_load_explicit(&piece_dir_steps, memory_order_relaxed),
        .core_free_slots = c->free_slots, .retire_steps = c->retire_steps,
        .resolve_touched = c->resolve_touched,
        .inbox_extents = piece_test_inbox_extents(c->reserve),
        .core_owners = atomic_load_explicit(&c->owners, memory_order_relaxed)
    };
}

bool piece_test_fund_slots(piece_tree *t, size_t slots)
{
    return piece_fund(t->view.core, slots, true);
}

void piece_test_hold_slot(piece_tree *t)
{
    piece_core *c = t->view.core;
    c->claim_limit = SIZE_MAX - c->claim_base; /* a test draw, outside any claim */
    void **slot = piece_slot_get(c);
    *slot = c->held_slots;
    c->held_slots = slot;
}

void piece_test_release_slot(piece_tree *t)
{
    piece_core *c = t->view.core;
    void **slot = c->held_slots;
    ASSERT(slot);
    c->held_slots = *slot;
    piece_slot_put(c, slot);
}
#endif
