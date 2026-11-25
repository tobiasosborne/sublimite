#include "undo/undo.h"

#include "base/base.h"

#include <limits.h>

/* (G) Signed inline capacity, and v1's per-call trim/reclaim work bound.
 * (P) Each external entry is one 16 B pair plus one 8 B count record. */
enum {
    UNDO_INLINE = 8,
    UNDO_RECLAIM_RECORDS = 16,
    UNDO_ENTRY_BYTES = sizeof(piece_span) + sizeof(piece_span_nl)
};

typedef enum undo_inverse_kind {
    UNDO_EMPTY, UNDO_LITERAL, UNDO_SPANS
} undo_inverse_kind;

typedef struct undo_extent {
    base_arena mem;
    size_t used, claims;
} undo_extent;

typedef struct undo_run {
    piece_span span;
    piece_span_nl nl;
} undo_run;

typedef struct undo_literal {
    piece_span copy;
    undo_run source; /* Single-run provenance for an implicit erase burst. */
    uint16_t cuts;  /* Nonzero for backspaces, at captured segment boundaries. */
} undo_literal;

_Static_assert(sizeof(piece_span) <= sizeof(uint16_t) * CHAR_BIT,
               "a cut bit fits at every position below the one-run literal threshold");

typedef struct undo_inverse {
    undo_inverse_kind kind;
    size_t count;
    union {
        undo_literal literal;
        struct {
            piece_span spans[UNDO_INLINE];
            piece_span_nl nl[UNDO_INLINE];
        } local;
        struct {
            undo_extent *extent;
            size_t offset;
        } external;
    } data;
} undo_inverse;

typedef struct undo_record {
    struct undo_record *prev, *next;
    uint64_t off, removed;
    piece_span inserted;
    undo_inverse inverse;
} undo_record;

typedef struct undo_group {
    struct undo_group *prev, *next;
    undo_record *first, *last;
    size_t bytes, records;
    undo_state before, after;
} undo_group;

typedef struct undo_slab undo_slab;
typedef struct undo_slot {
    undo_slab *slab;
    union {
        struct undo_slot *free_next;
        undo_group group;
        undo_record record;
    } value;
} undo_slot;

/* (G) v1 cfg.history_keys = 32,768 at oracle pin 6fec2e8, src/editor/buffers.c:256.
 * (P) A one-byte group charges two slots and the input byte. On LP64 the
 * slot is 264 B: 8 B owner + 48 B record prefix + 16 B inverse prefix +
 * eight 24 B entries. The resulting 529 B charge gives 17,334,272 B. */
#define UNDO_DEFAULT_CAP_BYTES (32768u * (2u * sizeof(undo_slot) + 1u))

size_t undo_default_cap_bytes(void)
{
    return UNDO_DEFAULT_CAP_BYTES;
}

struct undo_slab {
    undo_slab *prev, *next; /* Only slabs with space are linked. */
    base_arena mem;
    undo_slot *free;
    size_t live;
};

/* FAILED and REPLAY_FAILED: a call failed with the checkpoint open; only
 * the owner's undo_abort may follow (D1'). Each keeps the cleanup state of
 * its kind: pending forward records, or the replay views. */
typedef enum undo_phase {
    UNDO_IDLE, UNDO_OPEN, UNDO_FAILED, UNDO_REPLAYING, UNDO_PREPARED, UNDO_REPLAY_FAILED
} undo_phase;

typedef struct undo_replay {
    /* The committed cursor stays at its boundary while next walks the
     * unpublished mutations. */
    undo_group *group;
    undo_replay_direction direction;
    undo_record *next;
    piece_snapshot *view, *guard;
} undo_replay;

typedef struct undo_pending {
    bool join;
    undo_state before;
    undo_extent *append;
    size_t append_used;
    undo_group *target, *fresh;
    undo_record *first, *last;
    /* Only the last published record can be extended. Keep its draft
     * separate from the new records so abort never rewrites history. */
    undo_record *replaced;
    undo_record replacement;
    size_t records, charge, refund;
} undo_pending;

struct undo_log {
    base_arena self;
    piece_tree *tree;
    piece_validate_fn validate;
    void *ctx;
    size_t page, cap, storage, span_payload;
    undo_slab *available;
    undo_extent *append;
    undo_group *head, *tail, *applied;
    undo_group *retired_head, *retired_tail;
    size_t groups, applied_groups, evicted_groups;
    size_t bytes, applied_bytes, records, applied_records;
    uint64_t serial;
    undo_phase phase;
    union {
        undo_pending pending;
        undo_replay replay;
    };
};

static undo_err piece_result(piece_err err)
{
    switch (err) {
    case PIECE_OK: return UNDO_OK;
    case PIECE_ERR_NOMEM: return UNDO_ERR_NOMEM;
    case PIECE_ERR_RANGE: return UNDO_ERR_RANGE;
    }
    ASSERT(false);
    return UNDO_ERR_RANGE;
}

static bool trusted(const undo_log *u)
{
    const piece_backing *b = piece_view_backing(piece_tree_view(u->tree));
    if (!b) return true;
    uint64_t epoch;
    return u->validate(u->ctx, b) && os_fault_poll(b->fault, &epoch) == OS_OK && epoch == 0;
}

static bool extent_init(undo_log *u, base_arena *mem, size_t bytes)
{
    size_t padding = (u->page - bytes % u->page) % u->page;
    if (bytes > SIZE_MAX - padding || base_arena_init(mem, bytes + padding) != 0)
        return false;
    u->storage += mem->size;
    return true;
}

static void extent_free(undo_log *u, base_arena mem)
{
    u->storage -= mem.size;
    base_arena_free(&mem);
}

static void available_add(undo_log *u, undo_slab *s)
{
    s->prev = NULL;
    s->next = u->available;
    if (s->next) s->next->prev = s;
    u->available = s;
}

static void available_remove(undo_log *u, undo_slab *s)
{
    if (s->prev) s->prev->next = s->next;
    else u->available = s->next;
    if (s->next) s->next->prev = s->prev;
}

static bool slab_has_space(const undo_slab *s)
{
    return s->free || s->mem.size - s->mem.used >= sizeof(undo_slot);
}

static undo_slot *slot_take(undo_log *u)
{
    undo_slab *s = u->available;
    if (!s) {
        base_arena mem;
        /* One page is the OS's smallest independently releasable unit (P).
         * Include room for a slot even on a smaller page geometry. */
        size_t need = sizeof *s + _Alignof(undo_slot) - 1 + sizeof(undo_slot);
        if (!extent_init(u, &mem, need > u->page ? need : u->page)) return NULL;
        s = base_arena_alloc(&mem, sizeof *s, _Alignof(undo_slab));
        ASSERT(s != NULL);
        *s = (undo_slab){.mem = mem};
        available_add(u, s);
    }
    undo_slot *slot = s->free;
    if (slot) s->free = slot->value.free_next;
    else slot = base_arena_alloc(&s->mem, sizeof *slot, _Alignof(undo_slot));
    ASSERT(slot != NULL);
    s->live++;
    if (!slab_has_space(s)) available_remove(u, s);
    slot->slab = s;
    return slot;
}

static void slot_put(undo_log *u, void *member)
{
    undo_slot *slot = (undo_slot *)((unsigned char *)member - offsetof(undo_slot, value));
    undo_slab *s = slot->slab;
    bool available = slab_has_space(s);
    if (--s->live == 0) {
        if (available) available_remove(u, s);
        extent_free(u, s->mem);
        return;
    }
    if (!available) available_add(u, s);
    slot->value.free_next = s->free;
    s->free = slot;
}

static const piece_span *inverse_spans(const undo_inverse *v)
{
    if (v->count <= UNDO_INLINE) return v->data.local.spans;
    unsigned char *start = (unsigned char *)(v->data.external.extent + 1);
    return (piece_span *)(start + v->data.external.offset);
}

static const piece_span_nl *inverse_counts(const undo_inverse *v)
{
    if (v->count <= UNDO_INLINE) return v->data.local.nl;
    return (const piece_span_nl *)(inverse_spans(v) + v->count);
}

static bool store_spans(undo_log *u, undo_inverse *v)
{
    if (v->count <= UNDO_INLINE) return true;
    v->data.external.extent = NULL;
    if (v->count > (SIZE_MAX - sizeof(undo_extent)) / UNDO_ENTRY_BYTES) return false;
    const size_t bytes = v->count * UNDO_ENTRY_BYTES;
    undo_extent *s = u->append;
    if (!s || bytes > s->mem.size - sizeof *s - s->used * UNDO_ENTRY_BYTES) {
        base_arena mem;
        size_t need = sizeof *s + bytes;
        if (!extent_init(u, &mem, need <= u->page ? u->page : need)) return false;
        s = base_arena_alloc(&mem, sizeof *s, _Alignof(undo_extent));
        ASSERT(s != NULL);
        *s = (undo_extent){.mem = mem};
        if (need <= u->page) u->append = s;
    }
    v->data.external.extent = s;
    /* (P) Each vector's two parallel arrays together occupy 24n B,
     * keeping the next vector aligned to an 8 B boundary. */
    v->data.external.offset = s->used * UNDO_ENTRY_BYTES;
    s->used += v->count;
    s->claims++;
    u->span_payload += bytes;
    return true;
}

static void drop_spans(undo_log *u, const undo_inverse *v)
{
    if (v->count <= UNDO_INLINE || !v->data.external.extent) return;
    undo_extent *s = v->data.external.extent;
    u->span_payload -= v->count * UNDO_ENTRY_BYTES;
    if (--s->claims != 0) return;
    if (u->append == s) u->append = NULL;
    extent_free(u, s->mem);
}

static void retire(undo_log *u, undo_group *first, undo_group *last)
{
    last->next = NULL;
    if (u->retired_tail) u->retired_tail->next = first;
    else u->retired_head = first;
    u->retired_tail = last;
}

static bool failed(const undo_log *u)
{
    return u->phase == UNDO_FAILED || u->phase == UNDO_REPLAY_FAILED;
}

static size_t maintain_retired(undo_log *u, size_t record_budget)
{
    ASSERT(u && !failed(u));
    if (u->phase != UNDO_IDLE) return 0;
    size_t count = 0;
    while (u->retired_head && count < record_budget) {
        undo_group *g = u->retired_head;
        undo_record *r = g->first;
        g->first = r->next;
        drop_spans(u, &r->inverse);
        slot_put(u, r);
        count++;
        if (g->first) continue;
        u->retired_head = g->next;
        if (!u->retired_head) u->retired_tail = NULL;
        slot_put(u, g);
    }
    return count;
}

size_t undo_maintain(undo_log *u, size_t record_budget)
{
    return maintain_retired(u, record_budget);
}

static void clear_redo(undo_log *u)
{
    if (u->groups == u->applied_groups) return;
    undo_group *first = u->applied ? u->applied->next : u->head;
    retire(u, first, u->tail);
    u->tail = u->applied;
    if (u->tail) u->tail->next = NULL;
    else u->head = NULL;
    u->groups = u->applied_groups;
    u->records = u->applied_records;
    u->bytes = u->applied_bytes;
}

static void evict_prefix(undo_log *u, undo_group *last, size_t groups, size_t records, size_t bytes)
{
    undo_group *old = u->head;
    u->head = last->next;
    if (u->head) u->head->prev = NULL;
    else u->tail = NULL;
    if (u->applied == last) u->applied = NULL;
    u->groups -= groups;
    u->applied_groups -= groups;
    u->evicted_groups += groups;
    u->records -= records;
    u->applied_records -= records;
    u->bytes -= bytes;
    u->applied_bytes -= bytes;
    retire(u, old, last);
}

static void trim(undo_log *u)
{
    size_t visits = 0;
    while (u->bytes > u->cap && u->applied && visits < UNDO_RECLAIM_RECORDS) {
        undo_group *g = u->head;
        evict_prefix(u, g, 1, g->records, g->bytes);
        visits++;
    }
    /* The v1 bulk fallback bounds boundary visits on a large reduction. */
    if (u->bytes > u->cap && u->applied)
        evict_prefix(u, u->applied, u->applied_groups, u->applied_records, u->applied_bytes);
    if (u->bytes > u->cap) clear_redo(u);
}

undo_err undo_create(undo_log **out, piece_tree *tree, size_t cap_bytes,
                     piece_validate_fn validate, void *ctx)
{
    ASSERT(out && tree && cap_bytes != 0);
    ASSERT(!piece_view_backing(piece_tree_view(tree)) || validate);
    *out = NULL;
    undo_log initial = {.tree = tree, .validate = validate, .ctx = ctx,
                        .cap = cap_bytes, .page = os_page_size()};
    if (!extent_init(&initial, &initial.self, sizeof initial)) return UNDO_ERR_NOMEM;
    undo_log *u = base_arena_alloc(&initial.self, sizeof *u, _Alignof(undo_log));
    ASSERT(u != NULL);
    *u = initial;
    *out = u;
    return UNDO_OK;
}

static void release_replay_views(undo_log *u)
{
    if (u->replay.guard) piece_snapshot_release(u->replay.guard);
    if (u->replay.view) piece_snapshot_release(u->replay.view);
}

void undo_clear(undo_log *u)
{
    ASSERT(u && !failed(u) && u->phase != UNDO_PREPARED);
    switch (u->phase) {
    case UNDO_IDLE:
    case UNDO_FAILED:
    case UNDO_PREPARED:
    case UNDO_REPLAY_FAILED:
        break;
    case UNDO_OPEN:
    case UNDO_REPLAYING:
        undo_abort(u);
        break;
    }
    if (u->head) retire(u, u->head, u->tail);
    u->head = u->tail = u->applied = NULL;
    u->groups = u->applied_groups = u->records = u->applied_records = 0;
    u->bytes = u->applied_bytes = 0;
    (void)maintain_retired(u, SIZE_MAX);
}

void undo_destroy(undo_log *u)
{
    undo_clear(u);
    ASSERT(!u->append && !u->available && u->storage == u->self.size);
    base_arena self = u->self;
    base_arena_free(&self);
}

void undo_set_cap(undo_log *u, size_t cap_bytes)
{
    ASSERT(u && cap_bytes != 0 && u->phase == UNDO_IDLE);
    u->cap = cap_bytes;
    trim(u);
    (void)maintain_retired(u, UNDO_RECLAIM_RECORDS);
}

undo_history undo_get_history(const undo_log *u)
{
    ASSERT(u && !failed(u));
    return (undo_history){u->applied_groups, u->groups - u->applied_groups,
                          u->evicted_groups, u->serial};
}

undo_stats undo_get_stats(const undo_log *u)
{
    ASSERT(u && !failed(u));
    return (undo_stats){u->records, u->bytes, u->span_payload, u->storage};
}

undo_err undo_begin(undo_log *u, const undo_state *before, bool join)
{
    ASSERT(u && before && u->phase == UNDO_IDLE);
    (void)maintain_retired(u, UNDO_RECLAIM_RECORDS);
    undo_err e = piece_result(piece_checkpoint_begin(u->tree));
    if (e != UNDO_OK) return e;
    undo_pending *p = &u->pending;
    u->phase = UNDO_OPEN;
    p->join = join;
    p->before = *before;
    p->append = u->append;
    p->append_used = u->append ? u->append->used : 0;
    p->target = p->fresh = NULL;
    p->first = p->last = p->replaced = NULL;
    p->records = p->charge = p->refund = 0;
    return UNDO_OK;
}

static void discard_pending(undo_log *u)
{
    undo_pending *p = &u->pending;
    for (undo_record *r = p->first; r;) {
        undo_record *next = r->next;
        drop_spans(u, &r->inverse);
        slot_put(u, r);
        r = next;
    }
    if (p->fresh) slot_put(u, p->fresh);
    u->append = p->append;
    if (u->append) u->append->used = p->append_used;
}

void undo_abort(undo_log *u)
{
    ASSERT(u);
    switch (u->phase) {
    case UNDO_IDLE:
        return; /* the failed call opened nothing */
    case UNDO_OPEN:
    case UNDO_FAILED:
        piece_checkpoint_abort(u->tree);
        discard_pending(u);
        break;
    case UNDO_REPLAYING:
    case UNDO_PREPARED:
    case UNDO_REPLAY_FAILED:
        /* The guard is undo's own provisional pin: released before abort. */
        release_replay_views(u);
        piece_checkpoint_abort(u->tree);
        break;
    }
    u->phase = UNDO_IDLE;
}

static bool adjacent(const undo_record *r, uint64_t off, uint64_t del, size_t len)
{
    if (del == 0) return r->removed == 0 && r->off + r->inserted.len == off;
    return len == 0 && r->inserted.len == 0 && (off == r->off || off + del == r->off);
}

static undo_err prepare_inverse(undo_log *u, undo_record *r, bool remember_source)
{
    if (r->removed == 0) return UNDO_OK;
    const piece_view *view = piece_tree_view(u->tree);
    const uint64_t end = r->off + r->removed;
    piece_iter it;
    piece_span s;
    piece_iter_init(&it, view, r->off);
    uint64_t limit = r->removed / sizeof(piece_span), count = 0;
    while (piece_iter_next_span(&it, end, &s, NULL)) {
        if (++count > limit) {
            r->inverse.kind = UNDO_LITERAL;
            r->inverse.count = 1;
            undo_literal *literal = &r->inverse.data.literal;
            *literal = (undo_literal){0};
            if (remember_source && s.len == r->removed) {
                piece_iter_seek(&it, r->off);
                ASSERT(piece_iter_next_span(&it, end, &literal->source.span, &literal->source.nl));
            }
            return UNDO_OK;
        }
    }
    if (count > SIZE_MAX / UNDO_ENTRY_BYTES) return UNDO_ERR_NOMEM;
    undo_inverse *v = &r->inverse;
    v->kind = UNDO_SPANS;
    v->count = (size_t)count;
    if (!store_spans(u, v)) return UNDO_ERR_NOMEM;
    piece_span *spans = (piece_span *)inverse_spans(v);
    piece_span_nl *nl = (piece_span_nl *)inverse_counts(v);
    piece_iter_seek(&it, r->off);
    for (size_t i = 0; i < v->count; i++)
        ASSERT(piece_iter_next_span(&it, end, &spans[i], &nl[i]));
    return UNDO_OK;
}

static undo_err change_text(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes,
                            size_t len, piece_span *capture, piece_span *inserted)
{
    if (capture) {
        undo_err e = piece_result(piece_capture(u->tree, off, del, capture));
        if (e != UNDO_OK) return e;
    }
    if (inserted) *inserted = (piece_span){PIECE_SPAN_ADD | piece_add_length(u->tree), len};
    return piece_result(piece_replace(u->tree, off, del, bytes, len));
}

/* The burst already matches in document coordinates. Extend a deletion
 * only if its sources also concatenate to one canonical run. */
static bool erase_extension(undo_log *u, const undo_record *r, uint64_t off, uint64_t del,
                             undo_run *out)
{
    const undo_inverse *v = &r->inverse;
    undo_run old;
    switch (v->kind) {
    case UNDO_EMPTY:
        return false;
    case UNDO_LITERAL:
        old = v->data.literal.source;
        if (old.span.len == 0) return false;
        if (old.span.len + del < sizeof(piece_span)) {
            /* A forward append cannot extend the last capture once that
             * capture belongs at the beginning of a backspace literal. */
            if (v->data.literal.cuts && off == r->off) return false;
            if (v->data.literal.copy.src + v->data.literal.copy.len !=
                (PIECE_SPAN_ADD | piece_add_length(u->tree))) return false;
        }
        break;
    case UNDO_SPANS:
        if (v->count != 1) return false;
        old = (undo_run){v->data.local.spans[0], v->data.local.nl[0]};
        break;
    }

    piece_iter it;
    const uint64_t end = off + del;
    piece_iter_init(&it, piece_tree_view(u->tree), off);
    undo_run add;
    ASSERT(piece_iter_next_span(&it, end, &add.span, NULL));
    if (add.span.len != del) return false;
    bool backward = off < r->off;
    if (backward ? add.span.src + add.span.len != old.span.src
                 : old.span.src + old.span.len != add.span.src) return false;
    piece_iter_seek(&it, off);
    ASSERT(piece_iter_next_span(&it, end, &add.span, &add.nl));
    const undo_run left = backward ? add : old, right = backward ? old : add;
    *out = (undo_run){{left.span.src, left.span.len + right.span.len},
                      piece_span_nl_join(left.span, left.nl, right.span, right.nl)};
    return true;
}

static bool charge_fits(const undo_log *u, size_t current, uint64_t extra)
{
    const size_t room = SIZE_MAX - (u->applied_bytes - u->pending.refund);
    return current <= room && extra <= room - current;
}

static undo_err extend(undo_log *u, undo_record *r, uint64_t off, uint64_t del, const uint8_t *bytes,
                       size_t len, undo_run combined)
{
    undo_pending *p = &u->pending;
    const bool literal = del != 0 && combined.span.len < sizeof(piece_span);
    const size_t old = r->inverse.kind == UNDO_LITERAL ? (size_t)r->removed : 0;
    const bool published = p->target && r == p->target->last;
    if (published) {
        /* The old literal charge is already in the applied ledger. */
        p->refund = old;
        p->charge = old;
    }
    /* (P) One charge per short input write, even when the reference grows. */
    size_t charge = literal ? (size_t)combined.span.len : len < sizeof(piece_span) ? len : 0;
    if (!charge_fits(u, p->charge - old, charge)) return UNDO_ERR_NOMEM;
    piece_span capture;
    undo_err e = change_text(u, off, del, bytes, len, literal ? &capture : NULL, NULL);
    if (e != UNDO_OK) return e;
    if (published) {
        p->replaced = r;
        p->replacement = *r;
        r = &p->replacement;
    }

    if (del == 0) {
        r->inserted.len += len;
    } else {
        const bool backward = off < r->off;
        r->off = off;
        r->removed = combined.span.len;
        if (literal) {
            undo_literal *v = &r->inverse.data.literal;
            ASSERT(capture.src == v->copy.src + v->copy.len);
            /* Capture parts append in edit order. Remember a backspace
             * boundary to restore the parts in document order. */
            if (backward) v->cuts |= (uint16_t)(1u << v->copy.len);
            v->copy.len += capture.len;
            v->source = combined;
        } else {
            r->inverse.kind = UNDO_SPANS;
            r->inverse.data.local.spans[0] = combined.span;
            r->inverse.data.local.nl[0] = combined.nl;
        }
    }
    p->charge = p->charge - old + charge;
    return UNDO_OK;
}

static undo_record *pending_tail(undo_pending *p)
{
    if (p->last) return p->last;
    if (p->replaced) return &p->replacement;
    return p->target ? p->target->last : NULL;
}

static undo_err stage_record(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes, size_t len)
{
    undo_pending *p = &u->pending;
    undo_slot *slot = slot_take(u);
    if (!slot) return UNDO_ERR_NOMEM;
    undo_record *r = &slot->value.record;
    r->prev = p->last;
    r->next = NULL;
    r->off = off;
    r->removed = del;
    r->inverse.kind = UNDO_EMPTY;
    r->inverse.count = 0;
    if (p->last) p->last->next = r;
    else p->first = r;
    p->last = r;

    undo_err e = prepare_inverse(u, r, len == 0);
    if (e != UNDO_OK) return e;
    const undo_inverse *v = &r->inverse;
    uint64_t payload = v->count > UNDO_INLINE ? v->count * UNDO_ENTRY_BYTES : 0;
    if (v->kind == UNDO_LITERAL) payload += del;
    if (len < sizeof(piece_span)) payload += len;
    if (!charge_fits(u, p->charge, sizeof(undo_slot) + payload)) return UNDO_ERR_NOMEM;
    piece_span *capture = v->kind == UNDO_LITERAL ? &r->inverse.data.literal.copy : NULL;
    e = change_text(u, off, del, bytes, len, capture, &r->inserted);
    if (e != UNDO_OK) return e;
    p->charge += sizeof(undo_slot) + (size_t)payload;
    p->records++;
    return UNDO_OK;
}

static undo_err stage(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes, size_t len)
{
    undo_pending *p = &u->pending;
    if (!p->target && !p->fresh) {
        if (p->join && u->applied && adjacent(u->applied->last, off, del, len)) {
            p->target = u->applied;
        } else {
            undo_slot *slot = slot_take(u);
            if (!slot) return UNDO_ERR_NOMEM;
            p->fresh = &slot->value.group;
            p->charge = sizeof(undo_slot);
        }
    }
    undo_record *previous = pending_tail(p);
    if (previous && adjacent(previous, off, del, len)) {
        undo_run combined = {0};
        bool merge = del == 0
            ? previous->inserted.src + previous->inserted.len == (PIECE_SPAN_ADD | piece_add_length(u->tree))
            : erase_extension(u, previous, off, del, &combined);
        if (merge) return extend(u, previous, off, del, bytes, len, combined);
    }
    return stage_record(u, off, del, bytes, len);
}

undo_err undo_replace(undo_log *u, uint64_t off, uint64_t del, const uint8_t *bytes, size_t len)
{
    ASSERT(u && u->phase == UNDO_OPEN && (bytes || len == 0));
    const uint64_t n = piece_length(piece_tree_view(u->tree));
    ASSERT(off <= n && del <= n - off && len <= PIECE_MAX_LENGTH - (n - del));
    if (del == 0 && len == 0) return UNDO_OK;
    undo_err e = stage(u, off, del, bytes, len);
    if (e != UNDO_OK) u->phase = UNDO_FAILED;
    return e;
}

static void publish(undo_log *u, const undo_state *after)
{
    undo_pending *p = &u->pending;
    if (!p->first && !p->replaced) return;
    clear_redo(u);
    undo_group *g = p->target;
    if (!g) {
        g = p->fresh;
        *g = (undo_group){.prev = u->tail, .before = p->before};
        if (u->tail) u->tail->next = g;
        else u->head = g;
        u->tail = u->applied = g;
        u->applied_groups++;
        u->groups++;
        u->serial++;
    }
    if (p->replaced) *p->replaced = p->replacement;
    if (p->first) {
        p->first->prev = g->last;
        if (g->last) g->last->next = p->first;
        else g->first = p->first;
        g->last = p->last;
    }
    g->after = *after;
    g->records += p->records;
    u->records += p->records;
    u->applied_records += p->records;
    g->bytes = g->bytes - p->refund + p->charge;
    u->bytes = u->bytes - p->refund + p->charge;
    u->applied_bytes = u->applied_bytes - p->refund + p->charge;
    trim(u);
}

static const undo_state *replay_target(const undo_replay *replay)
{
    return replay->direction == UNDO_REPLAY_UNDO ? &replay->group->before : &replay->group->after;
}

static void publish_replay(undo_log *u)
{
    const undo_group *g = u->replay.group;
    switch (u->replay.direction) {
    case UNDO_REPLAY_UNDO:
        u->applied = g->prev;
        u->applied_groups--;
        u->applied_records -= g->records;
        u->applied_bytes -= g->bytes;
        break;
    case UNDO_REPLAY_REDO:
        u->applied = u->replay.group;
        u->applied_groups++;
        u->applied_records += g->records;
        u->applied_bytes += g->bytes;
        break;
    }
    release_replay_views(u);
}

undo_err undo_commit(undo_log *u, const undo_state *after)
{
    ASSERT(u && after && (u->phase == UNDO_OPEN ||
                          (u->phase == UNDO_PREPARED && after == replay_target(&u->replay))));
    if (!trusted(u)) {
        u->phase = u->phase == UNDO_PREPARED ? UNDO_REPLAY_FAILED : UNDO_FAILED;
        return UNDO_ERR_CONFLICT;
    }
    piece_checkpoint_commit(u->tree);
    if (u->phase == UNDO_PREPARED) publish_replay(u);
    else publish(u, after);
    u->phase = UNDO_IDLE;
    return UNDO_OK;
}

/* (P) Cuts are set only in a one-run literal below the pair width. Walk
 * its capture segments backwards, in document insertion order. */
static piece_span literal_part(const undo_literal *v, uint64_t *end)
{
    uint64_t start = *end - 1;
    while (start != 0 && !(v->cuts & (1u << start))) start--;
    piece_span part = {v->copy.src + start, *end - start};
    *end = start;
    return part;
}

static undo_err restore_literal(piece_tree *t, const undo_record *r)
{
    const undo_literal *v = &r->inverse.data.literal;
    if (v->cuts == 0)
        return piece_result(piece_replace_spans(t, r->off, r->inserted.len, &v->copy, NULL, 1));

    /* (P) Each nonempty capture contributes at least one byte. */
    piece_span spans[sizeof(piece_span) - 1];
    size_t count = 0;
    for (uint64_t end = v->copy.len; end != 0;)
        spans[count++] = literal_part(v, &end);
    return piece_result(piece_replace_spans(t, r->off, r->inserted.len, spans, NULL, count));
}

static undo_err apply(undo_log *u, const undo_record *r, undo_replay_direction direction)
{
    switch (direction) {
    case UNDO_REPLAY_UNDO: {
        const undo_inverse *v = &r->inverse;
        switch (v->kind) {
        case UNDO_EMPTY:
            return piece_result(piece_replace_spans(u->tree, r->off, r->inserted.len, NULL, NULL, 0));
        case UNDO_LITERAL:
            return restore_literal(u->tree, r);
        case UNDO_SPANS:
            return piece_result(piece_replace_spans(u->tree, r->off, r->inserted.len,
                                                    inverse_spans(v), inverse_counts(v), v->count));
        }
        break;
    }
    case UNDO_REPLAY_REDO:
        return piece_result(piece_replace_spans(u->tree, r->off, r->removed, &r->inserted,
                                                NULL, r->inserted.len != 0 ? 1u : 0u));
    }
    ASSERT(false);
    return UNDO_ERR_RANGE;
}

static undo_err begin_replay(undo_log *u, undo_group *g, undo_replay_direction direction)
{
    piece_snapshot *view = NULL;
    if (g->first != g->last) {
        undo_err e = piece_result(piece_snapshot_take(u->tree, &view));
        if (e != UNDO_OK) return e;
    }
    undo_err e = piece_result(piece_checkpoint_begin(u->tree));
    if (e != UNDO_OK) {
        if (view) piece_snapshot_release(view);
        return e;
    }
    u->replay = (undo_replay){.group = g, .direction = direction, .view = view,
                             .next = direction == UNDO_REPLAY_UNDO ? g->last : g->first};
    u->phase = UNDO_REPLAYING;
    return UNDO_OK;
}

static undo_err check_guard(undo_log *u)
{
    piece_snapshot *now;
    undo_err e = piece_result(piece_snapshot_take(u->tree, &now));
    if (e != UNDO_OK) return e;
    ASSERT(now == u->replay.guard);
    piece_snapshot_release(now);
    piece_snapshot_release(u->replay.guard);
    u->replay.guard = NULL;
    return UNDO_OK;
}

undo_err undo_replay_prepare(undo_log *u, undo_replay_direction direction,
                             uint64_t deadline, const undo_state **target)
{
    ASSERT(u && target && (direction == UNDO_REPLAY_UNDO || direction == UNDO_REPLAY_REDO));
    ASSERT(u->phase == UNDO_IDLE || (u->phase == UNDO_REPLAYING && u->replay.direction == direction));
    *target = NULL;
    undo_err e;
    undo_group *g;
    bool reverse = direction == UNDO_REPLAY_UNDO;
    if (u->phase == UNDO_REPLAYING) {
        e = check_guard(u);
        if (e != UNDO_OK) goto abort_group;
        g = u->replay.group;
    } else {
        g = reverse ? u->applied : (u->applied ? u->applied->next : u->head);
        if (!g) return UNDO_OK;
    }
    for (;;) {
        /* The sanitizer planner checks carried counts against source
         * bytes. Check the backing before it can recount and after apply. */
        if (!trusted(u)) {
            e = UNDO_ERR_CONFLICT;
            goto abort_group;
        }
        if (u->phase == UNDO_IDLE) {
            e = begin_replay(u, g, direction);
            if (e != UNDO_OK) goto abort_group;
        }
        undo_record *r = u->replay.next;
        e = apply(u, r, direction);
        if (e != UNDO_OK) goto abort_group;
        if (!trusted(u)) {
            e = UNDO_ERR_CONFLICT;
            goto abort_group;
        }
        u->replay.next = reverse ? r->prev : r->next;
        if (!u->replay.next) {
            u->phase = UNDO_PREPARED;
            *target = replay_target(&u->replay);
            return UNDO_OK;
        }
        if (deadline != OS_DEADLINE_NEVER && os_now_ns() >= deadline) {
            e = piece_result(piece_snapshot_take(u->tree, &u->replay.guard));
            if (e != UNDO_OK) goto abort_group;
            return UNDO_MORE;
        }
    }

abort_group:
    /* The checkpoint, if this group opened one, stays open for the owner. */
    if (u->phase == UNDO_REPLAYING) u->phase = UNDO_REPLAY_FAILED;
    return e;
}

void undo_splice_iter_init(undo_splice_iter *it, const undo_log *u)
{
    ASSERT(it && u && u->phase == UNDO_PREPARED);
    const undo_group *g = u->replay.group;
    *it = (undo_splice_iter){u, u->replay.direction == UNDO_REPLAY_UNDO ? g->last : g->first};
}

bool undo_splice_iter_next(undo_splice_iter *it, undo_prepared_splice *out)
{
    ASSERT(it && it->log && it->log->phase == UNDO_PREPARED && out);
    const undo_record *r = it->next;
    if (!r) return false;
    undo_replay_direction direction = it->log->replay.direction;
    uint64_t removed = r->removed;
    size_t count = r->inserted.len != 0 ? 1u : 0u;
    it->next = r->next;
    if (direction == UNDO_REPLAY_UNDO) {
        removed = r->inserted.len;
        count = r->inverse.count;
        if (r->inverse.kind == UNDO_LITERAL) {
            for (uint16_t cuts = r->inverse.data.literal.cuts; cuts; cuts &= (uint16_t)(cuts - 1u))
                count++;
        }
        it->next = r->prev;
    }
    *out = (undo_prepared_splice){r->off, removed, count, r, direction};
    return true;
}

piece_span undo_splice_span(const undo_prepared_splice *splice, size_t index)
{
    ASSERT(splice && index < splice->span_count);
    const undo_record *r = splice->record;
    if (splice->direction == UNDO_REPLAY_REDO) return r->inserted;
    const undo_inverse *v = &r->inverse;
    switch (v->kind) {
    case UNDO_EMPTY:
        break;
    case UNDO_LITERAL: {
        const undo_literal *literal = &v->data.literal;
        if (!literal->cuts) return literal->copy;
        uint64_t end = literal->copy.len;
        for (size_t i = 0; i < index; i++) (void)literal_part(literal, &end);
        return literal_part(literal, &end);
    }
    case UNDO_SPANS:
        return inverse_spans(v)[index];
    }
    ASSERT(false);
    return (piece_span){0};
}

piece_snapshot *undo_replay_snapshot(const undo_log *u)
{
    ASSERT(u && !failed(u));
    return u->phase == UNDO_REPLAYING || u->phase == UNDO_PREPARED ? u->replay.view : NULL;
}
