/* L2 journal runtime, recovery (L2-2b): validate, repair, plan, install
 * origins, apply, offer literals. journal.h "Recovery" states the rules;
 * docs/decisions/L2-2b-journal-runtime.md their derivation. Setup only:
 * it allocates (one base arena) and blocks on storage.
 *
 * Every read of the image goes through one window of one maximum record
 * (wire section 3's stated reader cost); frames borrow the window until
 * the next read. Each pass is a forward walk of the committed prefix
 * with journal_stream_next, so the passes and journal_stream_validate
 * apply the same rules. */
#include "journal/internal.h"

#include <string.h>

#define WINDOW ((size_t)JOURNAL_MAX_RECORD_BYTES)
/* (G) By-value restore reads go through one piece chunk of scratch: the
 * piece store's own unit (piece.h PIECE_CHUNK). */
#define SOURCE_CHUNK ((size_t)PIECE_CHUNK)
#define NONE UINT64_MAX

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

/* ---- the window ---- */
typedef struct window {
    os_file *fd;
    uint64_t file_len;
    uint8_t *buf;
    uint64_t at;
    size_t len;
} window;

static os_err win_fill(window *w, uint64_t off)
{
    size_t want = w->file_len - off < WINDOW ? (size_t)(w->file_len - off) : WINDOW;
    size_t got = 0;
    os_err e = journal_read_all(w->fd, w->buf, want, off, &got);
    if (e != OS_OK) return e;
    if (got != want) return OS_E_IO; /* the file shrank under recovery */
    w->at = off;
    w->len = got;
    return OS_OK;
}

static os_err win_at(window *w, uint64_t off, const uint8_t **p, size_t *n)
{
    if (off < w->at || off >= w->at + w->len) {
        os_err e = win_fill(w, off);
        if (e != OS_OK) return e;
    }
    *p = w->buf + (off - w->at);
    *n = w->len - (size_t)(off - w->at);
    return OS_OK;
}

/* One journal_stream_next at s's offset; a TRUNCATED against the window
 * edge refills at the record and tries once more. */
static journal_codec_err stream_step(window *w, journal_stream_state *s, journal_frame *f,
                                     os_err *io)
{
    uint64_t off = JOURNAL_PREAMBLE_BYTES + s->offset;
    const uint8_t *p;
    size_t n;
    *io = win_at(w, off, &p, &n);
    if (*io != OS_OK) return JOURNAL_CODEC_TRUNCATED;
    journal_codec_err e = journal_stream_next(s, p, n, f);
    if (e == JOURNAL_CODEC_TRUNCATED && off != w->at && w->at + w->len < w->file_len) {
        *io = win_fill(w, off);
        if (*io != OS_OK) return JOURNAL_CODEC_TRUNCATED;
        e = journal_stream_next(s, w->buf, w->len, f);
    }
    return e;
}

/* ---- tables (open addressing over nonzero u64 keys) ---- */
typedef struct gen_entry {
    uint64_t key;
    journal_wire_identity identity;
    uint32_t prefix_len, prefix_crc;
    char *path;
    uint64_t declared_at;    /* stream offset of the first declaration */
    uint64_t value_only_at;  /* stream offset VALUE_ONLY was gained, NONE */
    bool distrusted;         /* a conflicting redeclaration */
    enum { SRC_UNOPENED = 0, SRC_VERIFIED, SRC_FAILED } source;
    os_file fd;
} gen_entry;

typedef struct buf_entry {
    uint64_t id;
    journal_origin_kind origin;
    uint64_t boundary;       /* BASE sequence or covered sequence */
    uint64_t origin_at;      /* BASE record / checkpoint BEGIN offset */
    uint64_t origin_end;     /* checkpoint: offset past its COMMIT */
    uint64_t origin_seq;     /* checkpoint BEGIN sequence */
    uint64_t base_key, content_id, saved_id;
    uint32_t flags;
    uint64_t target_at, target_seq, view_at, view_seq; /* last ones, NONE */
    journal_buffer_verdict verdict;
    piece_tree *tree;
    uint64_t base_len;
    uint64_t group;          /* BEGIN offset of the group its piece
                                checkpoint is open in, NONE */
    size_t literal_count, literal_fill;
    journal_literal *literals;
} buf_entry;

typedef struct table {
    uint64_t *keys;
    uint32_t *slot;   /* entry index per key slot */
    size_t cap;       /* power of two */
    uint8_t *entries;
    size_t esize, count, ecap;
} table;

typedef struct rc {
    journal_recovery *out;
    base_arena *arena;
    window w;
    piece_reserve *reserve;
    journal_open_source_fn open_source;
    void *open_ctx;
    table gens, bufs;
    uint64_t tabs_at, tabs_seq;
    uint8_t *chunk;          /* SOURCE_CHUNK scratch */
    uint8_t *prefix;         /* JOURNAL_PREFIX_BYTES scratch */
    size_t *touched;         /* buffers with a piece checkpoint in the group */
    size_t touched_n, touched_cap;
} rc;

static void *arena_get(rc *c, size_t n, size_t align)
{
    return base_arena_alloc(c->arena, n ? n : 1, align);
}

static size_t hash64(uint64_t k)
{
    k ^= k >> 33;
    k *= UINT64_C(0xff51afd7ed558ccd);
    k ^= k >> 33;
    return (size_t)k;
}

static void *table_entry(table *t, size_t i) { return t->entries + i * t->esize; }

static void *table_get(table *t, uint64_t key)
{
    if (t->cap == 0) return NULL;
    for (size_t i = hash64(key) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        if (t->keys[i] == key) return table_entry(t, t->slot[i]);
        if (t->keys[i] == 0) return NULL;
    }
}

/* Grows by doubling from the arena; the old arrays stay behind, so the
 * arena pays at most twice the final size (P, geometric series). */
static bool table_grow(rc *c, table *t)
{
    size_t cap = t->cap ? t->cap * 2 : 16;
    uint64_t *keys = arena_get(c, cap * sizeof *keys, 8);
    uint32_t *slot = arena_get(c, cap * sizeof *slot, 4);
    if (!keys || !slot) return false;
    memset(keys, 0, cap * sizeof *keys);
    for (size_t i = 0; i < t->cap; i++) {
        if (t->keys[i] == 0) continue;
        size_t k = hash64(t->keys[i]) & (cap - 1);
        while (keys[k]) k = (k + 1) & (cap - 1);
        keys[k] = t->keys[i];
        slot[k] = t->slot[i];
    }
    t->keys = keys;
    t->slot = slot;
    t->cap = cap;
    if (t->count == t->ecap) {
        size_t ecap = t->ecap ? t->ecap * 2 : 8;
        uint8_t *e = arena_get(c, ecap * t->esize, 8);
        if (!e) return false;
        if (t->count) memcpy(e, t->entries, t->count * t->esize);
        t->entries = e;
        t->ecap = ecap;
    }
    return true;
}

/* The entry for key, created zeroed (*fresh) if absent; NULL on NOMEM. */
static void *table_put(rc *c, table *t, uint64_t key, bool *fresh)
{
    ASSERT(key != 0);
    *fresh = false;
    void *e = table_get(t, key);
    if (e) return e;
    if ((t->count + 1) * 2 > t->cap || t->count == t->ecap) {
        if (!table_grow(c, t)) return NULL;
    }
    size_t i = hash64(key) & (t->cap - 1);
    while (t->keys[i]) i = (i + 1) & (t->cap - 1);
    ASSERT(t->count < UINT32_MAX); /* records <= file bytes / 32 */
    t->keys[i] = key;
    t->slot[i] = (uint32_t)t->count;
    e = table_entry(t, t->count++);
    memset(e, 0, t->esize);
    *fresh = true;
    return e;
}

static buf_entry *buffer_of(rc *c, uint64_t id, bool *nomem)
{
    bool fresh;
    buf_entry *b = table_put(c, &c->bufs, id, &fresh);
    if (!b) {
        *nomem = true;
        return NULL;
    }
    if (fresh) {
        b->id = id;
        b->target_at = b->view_at = NONE;
        b->group = NONE;
    }
    return b;
}

static void hw(uint64_t *mark, uint64_t v)
{
    if (v > *mark) *mark = v;
}

/* ---- pass A: validate to the first failure, classify, repair ---- */

/* A torn append is the writer's last bytes: nothing complete follows it.
 * Any record that decodes (CRC, payload, PAD position) at its own
 * sequence anywhere after the failing header proves acknowledged bytes
 * may follow, so the tail is damage. Diagnostic only: nothing found here
 * is applied (wire section 8 forbids resynchronizing). */
static bool genuine_record_after(window *w, uint64_t fail_at)
{
    for (uint64_t off = fail_at + 1; off + JOURNAL_HEADER_BYTES <= w->file_len; off++) {
        const uint8_t *p;
        size_t n;
        if (win_at(w, off, &p, &n) != OS_OK) return true; /* unknown: keep it */
        if (n < JOURNAL_HEADER_BYTES || memcmp(p, "SJR1", 4) != 0) continue;
        uint64_t seq = rd64(p + 16);
        journal_frame f;
        if (journal_record_decode(p, n, off - JOURNAL_PREAMBLE_BYTES, seq ? seq : 1, &f) ==
            JOURNAL_CODEC_OK)
            return true;
    }
    return false;
}

static os_err copy_range(rc *c, os_file *to, uint64_t len)
{
    for (uint64_t off = 0; off < len;) {
        const uint8_t *p;
        size_t n;
        os_err e = win_at(&c->w, off, &p, &n);
        if (e != OS_OK) return e;
        if (n > len - off) n = (size_t)(len - off);
        e = journal_write_all(to, p, n, off);
        if (e != OS_OK) return e;
        off += n;
    }
    return OS_OK;
}

/* Damage: the whole file copied aside byte for byte and made durable,
 * then the valid prefix written to a fresh image that replaces the name.
 * At every instant the name holds a complete image. */
static bool set_aside_copy(rc *c, os_dir *dir, const char *name, os_file *image)
{
    os_file aside = OS_FILE_INIT, fresh = OS_FILE_INIT;
    char tmp[OS_NAME_MAX];
    os_err e = journal_create_sibling(dir, name, "aside", c->out->set_aside, &aside);
    if (e != OS_OK) {
        c->out->set_aside[0] = 0;
        return false;
    }
    e = copy_range(c, &aside, c->w.file_len);
    if (e == OS_OK) e = os_file_sync(&aside);
    os_file_close(&aside);
    if (e == OS_OK) e = os_dir_sync(dir);
    if (e != OS_OK) {
        (void)os_unlink_at(dir, c->out->set_aside);
        c->out->set_aside[0] = 0;
        return false;
    }
    e = journal_create_sibling(dir, name, "new", tmp, &fresh);
    if (e != OS_OK) return false;
    e = copy_range(c, &fresh, c->out->valid_bytes);
    if (e == OS_OK) e = os_file_sync(&fresh);
    if (e == OS_OK) e = os_rename_at(dir, tmp, dir, name, OS_RENAME_REPLACE);
    if (e != OS_OK) {
        (void)os_unlink_at(dir, tmp);
        os_file_close(&fresh);
        return false;
    }
    (void)os_dir_sync(dir); /* the copy aside is durable either way */
    os_file_close(image);
    *image = fresh;
    return true;
}

/* A refused preamble: the file is renamed aside, never truncated. */
static journal_err rename_aside(rc *c, os_dir *dir, const char *name)
{
    uint64_t v = os_wall_ns();
    for (uint32_t n = 0; n < 256u; n++) {
        journal_sibling_name(c->out->set_aside, name, "aside", v, n);
        os_err e = os_rename_at(dir, name, dir, c->out->set_aside, OS_RENAME_NOREPLACE);
        if (e == OS_OK) {
            (void)os_dir_sync(dir);
            return JOURNAL_OK;
        }
        if (e != OS_E_EXIST) break;
    }
    c->out->set_aside[0] = 0;
    return JOURNAL_ERR_IO;
}

/* ---- pass B: plan ---- */

/* A checkpoint group seen in pass B, adopted at its COMMIT if it wins. */
typedef struct pending_checkpoint {
    buf_entry *b;            /* NULL: none open */
    journal_checkpoint d;
    uint64_t begin_at, begin_seq;
} pending_checkpoint;

static bool plan_record(rc *c, const journal_frame *f, uint64_t at, const journal_stream_state *s,
                        bool *nomem, pending_checkpoint *pc)
{
    journal_recovery *out = c->out;
    if (f->buffer_id) hw(&out->buffer_high_water, f->buffer_id);
    switch (f->type) {
    case JOURNAL_REC_PAD:
    case JOURNAL_REC_GROUP_BEGIN:
        return true;
    case JOURNAL_REC_GROUP_COMMIT:
        if (pc->b) {
            /* The greatest boundary wins: a checkpoint at or after the
             * BASE's sequence describes the text including its effect. */
            buf_entry *b = pc->b;
            uint64_t covered = pc->d.covered_sequence;
            if (b->origin == JOURNAL_ORIGIN_NONE || b->boundary <= covered) {
                b->origin = JOURNAL_ORIGIN_CHECKPOINT;
                b->boundary = covered;
                b->origin_at = pc->begin_at;
                b->origin_seq = pc->begin_seq;
                b->origin_end = at + f->total_len;
                b->content_id = pc->d.content_id;
                b->saved_id = pc->d.saved_content_id;
                b->flags = pc->d.flags;
                b->base_key = 0;
            }
            pc->b = NULL;
        }
        return true;
    case JOURNAL_REC_GENERATION: {
        journal_generation g;
        if (journal_generation_decode(f, &g) != JOURNAL_CODEC_OK) return false;
        hw(&out->generation_high_water, g.key);
        bool fresh;
        gen_entry *e = table_put(c, &c->gens, g.key, &fresh);
        if (!e) {
            *nomem = true;
            return false;
        }
        if (fresh) {
            e->key = g.key;
            e->identity = g.identity;
            e->prefix_len = g.prefix_len;
            e->prefix_crc = g.prefix_crc32c;
            e->path = arena_get(c, g.location_len + 1, 1);
            if (!e->path) {
                *nomem = true;
                return false;
            }
            memcpy(e->path, g.location, g.location_len);
            e->path[g.location_len] = 0;
            e->declared_at = at;
            e->value_only_at = (g.flags & JOURNAL_GENERATION_VALUE_ONLY) ? at : NONE;
            e->fd = (os_file)OS_FILE_INIT;
            return true;
        }
        /* Redeclaration: identical in everything but VALUE_ONLY, which
         * may be gained and never lost (wire section 6). */
        bool same = journal_identity_same(&e->identity, &g.identity) &&
                    e->prefix_len == g.prefix_len && e->prefix_crc == g.prefix_crc32c &&
                    strlen(e->path) == g.location_len &&
                    memcmp(e->path, g.location, g.location_len) == 0 &&
                    !(e->value_only_at != NONE && !(g.flags & JOURNAL_GENERATION_VALUE_ONLY));
        if (!same) e->distrusted = true;
        if ((g.flags & JOURNAL_GENERATION_VALUE_ONLY) && e->value_only_at == NONE)
            e->value_only_at = at;
        return true;
    }
    case JOURNAL_REC_BASE: {
        journal_base base;
        if (journal_base_decode(f, &base) != JOURNAL_CODEC_OK) return false;
        buf_entry *b = buffer_of(c, f->buffer_id, nomem);
        if (!b) return false;
        bool wins = b->origin == JOURNAL_ORIGIN_NONE || b->origin == JOURNAL_ORIGIN_BASE ||
                    b->boundary < f->sequence;
        if (wins) {
            b->origin = JOURNAL_ORIGIN_BASE;
            b->boundary = f->sequence;
            b->origin_at = at;
            b->base_key = base.generation_key;
            b->content_id = base.content_id;
            b->saved_id = 0;
            b->flags = 0;
        }
        return true;
    }
    case JOURNAL_REC_CHECKPOINT_BY_VALUE: {
        buf_entry *b = buffer_of(c, f->buffer_id, nomem);
        if (!b || journal_checkpoint_decode(f, &pc->d) != JOURNAL_CODEC_OK) return false;
        pc->b = b;
        pc->begin_at = JOURNAL_PREAMBLE_BYTES + s->group_at;
        pc->begin_seq = s->group_sequence;
        return true;
    }
    case JOURNAL_REC_TARGET: {
        buf_entry *b = buffer_of(c, f->buffer_id, nomem);
        if (!b) return false;
        b->target_at = at;
        b->target_seq = f->sequence;
        return true;
    }
    case JOURNAL_REC_VIEW: {
        buf_entry *b = buffer_of(c, f->buffer_id, nomem);
        if (!b) return false;
        b->view_at = at;
        b->view_seq = f->sequence;
        return true;
    }
    case JOURNAL_REC_TABS: {
        c->tabs_at = at;
        c->tabs_seq = f->sequence;
        uint32_t count = rd32(f->payload);
        for (uint32_t i = 0; i < count; i++)
            hw(&out->buffer_high_water, rd64(f->payload + 8 + (size_t)i * JOURNAL_TABS_ENTRY_BYTES));
        return true;
    }
    case JOURNAL_REC_SAVE: {
        /* SAVE resolution is L2-3's; recovery keeps the old replay, which
         * the identity check makes safe. Keys it allocated still count. */
        journal_save sv;
        if (journal_save_decode(f, &sv) != JOURNAL_CODEC_OK) return false;
        switch (sv.phase) {
        case JOURNAL_SAVE_PREPARED: hw(&out->generation_high_water, sv.prepared.new_key); break;
        case JOURNAL_SAVE_FINISHED: hw(&out->generation_high_water, sv.finished.installed_key); break;
        case JOURNAL_SAVE_CANCELLED: break;
        }
        return true;
    }
    case JOURNAL_REC_INSERT:
    case JOURNAL_REC_DELETE:
    case JOURNAL_REC_RESTORE_BASE_RANGE:
        return buffer_of(c, f->buffer_id, nomem) != NULL;
    }
    return false;
}

/* ---- sources: open by declared location, verify, before and after ---- */

static bool stat_matches(os_file *fd, const journal_wire_identity *want)
{
    os_stat st;
    if (os_file_stat(fd, &st) != OS_OK || st.kind != OS_KIND_FILE) return false;
    journal_wire_identity now = journal_identity_of(&st);
    return journal_identity_same(&now, want);
}

static bool source_verified(rc *c, gen_entry *g)
{
    switch (g->source) {
    case SRC_VERIFIED: return true;
    case SRC_FAILED: return false;
    case SRC_UNOPENED: break;
    }
    g->source = SRC_FAILED;
    if (g->distrusted) return false;
    /* NONBLOCK: a FIFO renamed over the location must not hang recovery;
     * it then fails the kind check. */
    if (os_file_open(&g->fd, g->path, OS_OPEN_READ | OS_OPEN_NOFOLLOW | OS_OPEN_NONBLOCK) != OS_OK)
        return false;
    if (!stat_matches(&g->fd, &g->identity)) return false;
    size_t want = g->identity.size < JOURNAL_PREFIX_BYTES ? (size_t)g->identity.size
                                                          : JOURNAL_PREFIX_BYTES;
    size_t got = 0;
    if (journal_read_all(&g->fd, c->prefix, want, 0, &got) != OS_OK || got != want ||
        got != g->prefix_len || journal_crc32c(c->prefix, got) != g->prefix_crc)
        return false;
    if (!stat_matches(&g->fd, &g->identity)) return false;
    g->source = SRC_VERIFIED;
    return true;
}

/* The built-in opener: a private copy read in full. */
static piece_err open_copy(void *ctx, piece_reserve *r, os_file *fd, uint64_t size,
                           piece_tree **out)
{
    (void)ctx;
    if (size == 0) return piece_open_copy(r, NULL, 0, out);
    if (size > SIZE_MAX - 1) return PIECE_ERR_NOMEM;
    base_arena a;
    if (base_arena_init(&a, (size_t)size) != 0) return PIECE_ERR_NOMEM;
    uint8_t *bytes = base_arena_alloc(&a, (size_t)size, 16);
    ASSERT(bytes != NULL); /* reserved exactly above */
    size_t got = 0;
    piece_err pe = PIECE_ERR_NOMEM;
    if (journal_read_all(fd, bytes, (size_t)size, 0, &got) == OS_OK && got == size)
        pe = piece_open_copy(r, bytes, (size_t)size, out);
    else
        *out = NULL; /* a short read: the source changed; re-verification fails it */
    base_arena_free(&a);
    return pe == PIECE_OK && *out ? PIECE_OK : pe;
}

/* ---- install origins ---- */

typedef enum fail_kind { FAIL_SOURCE, FAIL_RECORD } fail_kind;

static void fail_buffer(buf_entry *b, fail_kind k)
{
    if (b->verdict != JOURNAL_BUFFER_RECOVERED) return;
    b->verdict = k == FAIL_SOURCE ? JOURNAL_BUFFER_UNRESOLVED_BASE : JOURNAL_BUFFER_INCONSISTENT;
    if (b->tree) {
        if (b->group != NONE) piece_checkpoint_abort(b->tree);
        piece_close(b->tree);
        b->tree = NULL;
    }
    b->group = NONE;
}

/* Raw walk from a record offset with a known first sequence: a span
 * pass A certified, re-read. */
static journal_codec_err raw_next(window *w, uint64_t *at, uint64_t *seq, journal_frame *f,
                                  os_err *io)
{
    const uint8_t *p;
    size_t n;
    *io = win_at(w, *at, &p, &n);
    if (*io != OS_OK) return JOURNAL_CODEC_TRUNCATED;
    journal_codec_err e = journal_record_decode(p, n, *at - JOURNAL_PREAMBLE_BYTES, *seq, f);
    if (e == JOURNAL_CODEC_TRUNCATED && *at != w->at) {
        *io = win_fill(w, *at);
        if (*io != OS_OK) return e;
        e = journal_record_decode(w->buf, w->len, *at - JOURNAL_PREAMBLE_BYTES, *seq, f);
    }
    if (e != JOURNAL_CODEC_OK) return e;
    *at += f->total_len;
    if (f->type != JOURNAL_REC_PAD) *seq = f->sequence + 1;
    return JOURNAL_CODEC_OK;
}

static journal_err install_checkpoint(rc *c, buf_entry *b)
{
    piece_err pe = piece_open_copy(c->reserve, NULL, 0, &b->tree);
    if (pe != PIECE_OK) return JOURNAL_ERR_NOMEM;
    uint64_t at = b->origin_at, seq = b->origin_seq;
    while (at < b->origin_end) {
        journal_frame f;
        os_err io;
        if (raw_next(&c->w, &at, &seq, &f, &io) != JOURNAL_CODEC_OK) {
            fail_buffer(b, FAIL_RECORD);
            return io == OS_OK ? JOURNAL_OK : JOURNAL_ERR_IO;
        }
        if (f.type != JOURNAL_REC_INSERT) continue;
        journal_insert ins;
        if (journal_insert_decode(&f, &ins) != JOURNAL_CODEC_OK) {
            fail_buffer(b, FAIL_RECORD);
            return JOURNAL_OK;
        }
        pe = piece_replace(b->tree, ins.destination, 0, ins.bytes, ins.len);
        if (pe == PIECE_ERR_NOMEM) return JOURNAL_ERR_NOMEM;
        if (pe != PIECE_OK) {
            fail_buffer(b, FAIL_RECORD);
            return JOURNAL_OK;
        }
    }
    return JOURNAL_OK;
}

static journal_err install_origins(rc *c)
{
    for (size_t i = 0; i < c->bufs.count; i++) {
        buf_entry *b = table_entry(&c->bufs, i);
        b->verdict = JOURNAL_BUFFER_RECOVERED;
        switch (b->origin) {
        case JOURNAL_ORIGIN_NONE:
            b->verdict = JOURNAL_BUFFER_INCONSISTENT;
            break;
        case JOURNAL_ORIGIN_CHECKPOINT: {
            journal_err e = install_checkpoint(c, b);
            if (e != JOURNAL_OK) return e;
            break;
        }
        case JOURNAL_ORIGIN_BASE: {
            if (b->base_key == 0) {
                if (piece_open_copy(c->reserve, NULL, 0, &b->tree) != PIECE_OK)
                    return JOURNAL_ERR_NOMEM;
                break;
            }
            gen_entry *g = table_get(&c->gens, b->base_key);
            if (!g || g->declared_at >= b->origin_at || g->distrusted) {
                b->verdict = JOURNAL_BUFFER_INCONSISTENT; /* used before declared */
                break;
            }
            if (!source_verified(c, g)) {
                b->verdict = JOURNAL_BUFFER_UNRESOLVED_BASE;
                break;
            }
            journal_open_source_fn opener = c->open_source ? c->open_source : open_copy;
            piece_err pe = opener(c->open_ctx, c->reserve, &g->fd, g->identity.size, &b->tree);
            if (pe == PIECE_ERR_NOMEM && b->tree == NULL && stat_matches(&g->fd, &g->identity))
                return JOURNAL_ERR_NOMEM;
            if (pe != PIECE_OK || !stat_matches(&g->fd, &g->identity) ||
                piece_length(piece_tree_view(b->tree)) != g->identity.size) {
                if (b->tree) piece_close(b->tree);
                b->tree = NULL;
                b->verdict = JOURNAL_BUFFER_UNRESOLVED_BASE;
                break;
            }
            b->base_len = g->identity.size;
            break;
        }
        }
    }
    return JOURNAL_OK;
}

/* ---- pass C: apply ---- */

/* A checkpoint group's INSERT slices are its descriptor's text, never
 * edits: whichever checkpoint they belong to, selected or superseded. */
static bool in_checkpoint(const journal_stream_state *s)
{
    return s->phase == JOURNAL_STREAM_CHECKPOINT || s->phase == JOURNAL_STREAM_CHECKPOINT_TARGET;
}

static journal_err touch(rc *c, size_t idx)
{
    if (c->touched_n == c->touched_cap) {
        size_t cap = c->touched_cap ? c->touched_cap * 2 : 8;
        size_t *t = arena_get(c, cap * sizeof *t, 8);
        if (!t) return JOURNAL_ERR_NOMEM;
        if (c->touched_n) memcpy(t, c->touched, c->touched_n * sizeof *t);
        c->touched = t;
        c->touched_cap = cap;
    }
    c->touched[c->touched_n++] = idx;
    return JOURNAL_OK;
}

/* A failure inside a group: no member takes effect on any tree it
 * touched, and none of them may take later records on that text. */
static void fail_group(rc *c, fail_kind k)
{
    for (size_t i = 0; i < c->touched_n; i++) fail_buffer(table_entry(&c->bufs, c->touched[i]), k);
    c->touched_n = 0;
}

static journal_err restore_by_value(rc *c, buf_entry *b, gen_entry *g, const journal_restore *rs,
                                    fail_kind *fk)
{
    if (!source_verified(c, g)) {
        *fk = FAIL_SOURCE;
        return JOURNAL_ERR_IO;
    }
    if (rs->source_offset + rs->len > g->identity.size) {
        *fk = FAIL_RECORD;
        return JOURNAL_ERR_IO;
    }
    for (uint64_t done = 0; done < rs->len;) {
        size_t n = rs->len - done < SOURCE_CHUNK ? (size_t)(rs->len - done) : SOURCE_CHUNK;
        size_t got = 0;
        if (journal_read_all(&g->fd, c->chunk, n, rs->source_offset + done, &got) != OS_OK ||
            got != n) {
            *fk = FAIL_SOURCE;
            return JOURNAL_ERR_IO;
        }
        piece_err pe = piece_replace(b->tree, rs->destination + done, 0, c->chunk, n);
        if (pe == PIECE_ERR_NOMEM) return JOURNAL_ERR_NOMEM;
        if (pe != PIECE_OK) {
            *fk = FAIL_RECORD;
            return JOURNAL_ERR_IO;
        }
        done += n;
    }
    if (!stat_matches(&g->fd, &g->identity)) {
        *fk = FAIL_SOURCE; /* changed across the read: the bytes are not G's */
        return JOURNAL_ERR_IO;
    }
    return JOURNAL_OK;
}

/* Applies one text record. JOURNAL_ERR_IO here means "this record cannot
 * apply" with *fk saying why; NOMEM is fatal to the recovery. */
static journal_err apply_text(rc *c, buf_entry *b, const journal_frame *f, uint64_t at,
                              fail_kind *fk)
{
    *fk = FAIL_RECORD;
    uint64_t len = piece_length(piece_tree_view(b->tree));
    piece_err pe = PIECE_OK;
    switch (f->type) {
    case JOURNAL_REC_INSERT: {
        journal_insert ins;
        if (journal_insert_decode(f, &ins) != JOURNAL_CODEC_OK || ins.destination > len ||
            (uint64_t)ins.len > PIECE_MAX_LENGTH - len)
            return JOURNAL_ERR_IO;
        pe = piece_replace(b->tree, ins.destination, 0, ins.bytes, ins.len);
        break;
    }
    case JOURNAL_REC_DELETE: {
        journal_delete d;
        if (journal_delete_decode(f, &d) != JOURNAL_CODEC_OK || d.destination > len ||
            d.len > len - d.destination)
            return JOURNAL_ERR_IO;
        pe = piece_replace(b->tree, d.destination, d.len, NULL, 0);
        break;
    }
    case JOURNAL_REC_RESTORE_BASE_RANGE: {
        journal_restore rs;
        if (journal_restore_decode(f, &rs) != JOURNAL_CODEC_OK || rs.destination > len ||
            rs.len > PIECE_MAX_LENGTH - len)
            return JOURNAL_ERR_IO;
        gen_entry *g = table_get(&c->gens, rs.generation_key);
        /* Declared before this use, trusted, and not yet VALUE_ONLY. */
        if (!g || g->declared_at >= at || g->distrusted || g->value_only_at < at)
            return JOURNAL_ERR_IO;
        if (b->origin == JOURNAL_ORIGIN_BASE && b->base_key == rs.generation_key) {
            /* The tree's ORIGINAL is this verified generation (Q3). */
            if (rs.source_offset + rs.len > b->base_len) return JOURNAL_ERR_IO;
            piece_span sp = {rs.source_offset, rs.len};
            pe = piece_replace_spans(b->tree, rs.destination, 0, &sp, NULL, 1);
            break;
        }
        return restore_by_value(c, b, g, &rs, fk);
    }
    case JOURNAL_REC_PAD:
    case JOURNAL_REC_GENERATION:
    case JOURNAL_REC_BASE:
    case JOURNAL_REC_TARGET:
    case JOURNAL_REC_GROUP_BEGIN:
    case JOURNAL_REC_GROUP_COMMIT:
    case JOURNAL_REC_CHECKPOINT_BY_VALUE:
    case JOURNAL_REC_SAVE:
    case JOURNAL_REC_VIEW:
    case JOURNAL_REC_TABS:
        return JOURNAL_OK;
    }
    if (pe == PIECE_ERR_NOMEM) return JOURNAL_ERR_NOMEM;
    return pe == PIECE_OK ? JOURNAL_OK : JOURNAL_ERR_IO;
}

static bool is_text(journal_record_type t)
{
    return t == JOURNAL_REC_INSERT || t == JOURNAL_REC_DELETE ||
           t == JOURNAL_REC_RESTORE_BASE_RANGE;
}

static journal_err apply_pass(rc *c)
{
    journal_stream_state s;
    journal_stream_init(&s);
    uint64_t group_at = NONE;
    /* Inside a group: applying its members, or one failed and every
     * buffer the rest of it touches fails too (whole or not at all). */
    enum { GROUP_APPLYING = 0, GROUP_FAILED } group = GROUP_APPLYING;
    fail_kind group_fail = FAIL_RECORD;
    uint64_t end = c->out->valid_bytes - JOURNAL_PREAMBLE_BYTES;
    while (s.offset < end) {
        uint64_t at = JOURNAL_PREAMBLE_BYTES + s.offset;
        journal_frame f;
        os_err io;
        if (stream_step(&c->w, &s, &f, &io) != JOURNAL_CODEC_OK) return JOURNAL_ERR_IO;
        if (f.type == JOURNAL_REC_GROUP_BEGIN) {
            group_at = at;
            group = GROUP_APPLYING;
            c->touched_n = 0;
            continue;
        }
        if (f.type == JOURNAL_REC_GROUP_COMMIT) {
            for (size_t i = 0; i < c->touched_n; i++) {
                buf_entry *b = table_entry(&c->bufs, c->touched[i]);
                if (b->tree && b->group == group_at) piece_checkpoint_commit(b->tree);
                b->group = NONE;
            }
            c->touched_n = 0;
            group_at = NONE;
            group = GROUP_APPLYING;
            continue;
        }
        if (!is_text(f.type)) continue;
        buf_entry *b = table_get(&c->bufs, f.buffer_id);
        ASSERT(b != NULL); /* planned in pass B from the same bytes */
        if (b->verdict != JOURNAL_BUFFER_RECOVERED || f.sequence <= b->boundary ||
            in_checkpoint(&s))
            continue;
        if (group == GROUP_FAILED) {
            fail_buffer(b, group_fail);
            continue;
        }
        if (group_at != NONE && b->group != group_at) {
            piece_err pe = piece_checkpoint_begin(b->tree);
            if (pe == PIECE_ERR_NOMEM) return JOURNAL_ERR_NOMEM;
            ASSERT(pe == PIECE_OK);
            b->group = group_at;
            if (touch(c, (size_t)((uint8_t *)b - c->bufs.entries) / c->bufs.esize) != JOURNAL_OK)
                return JOURNAL_ERR_NOMEM;
        }
        fail_kind fk;
        journal_err e = apply_text(c, b, &f, at, &fk);
        if (e == JOURNAL_ERR_NOMEM) return e;
        if (e == JOURNAL_OK) continue;
        if (group_at != NONE) {
            fail_group(c, fk);
            group = GROUP_FAILED;
            group_fail = fk;
        } else {
            fail_buffer(b, fk);
        }
    }
    return JOURNAL_OK;
}

/* ---- pass D: literals of the buffers that did not recover ---- */
static journal_err literal_pass(rc *c, bool fill)
{
    journal_stream_state s;
    journal_stream_init(&s);
    uint64_t end = c->out->valid_bytes - JOURNAL_PREAMBLE_BYTES;
    while (s.offset < end) {
        journal_frame f;
        os_err io;
        if (stream_step(&c->w, &s, &f, &io) != JOURNAL_CODEC_OK) return JOURNAL_ERR_IO;
        if (f.type != JOURNAL_REC_INSERT) continue;
        buf_entry *b = table_get(&c->bufs, f.buffer_id);
        ASSERT(b != NULL);
        if (b->verdict == JOURNAL_BUFFER_RECOVERED || f.sequence <= b->boundary ||
            in_checkpoint(&s))
            continue;
        if (!fill) {
            b->literal_count++;
            continue;
        }
        journal_insert ins;
        if (journal_insert_decode(&f, &ins) != JOURNAL_CODEC_OK) return JOURNAL_ERR_IO;
        uint8_t *copy = arena_get(c, ins.len, 1);
        if (!copy) return JOURNAL_ERR_NOMEM;
        memcpy(copy, ins.bytes, ins.len);
        b->literals[b->literal_fill++] = (journal_literal){
            .sequence = f.sequence, .destination = ins.destination, .bytes = copy, .len = ins.len};
    }
    return JOURNAL_OK;
}

/* ---- metadata: the last TARGET, VIEW and TABS, decoded once ---- */
static journal_err read_record(rc *c, uint64_t at, uint64_t seq, journal_frame *f)
{
    os_err io;
    uint64_t a = at, s = seq;
    return raw_next(&c->w, &a, &s, f, &io) == JOURNAL_CODEC_OK ? JOURNAL_OK : JOURNAL_ERR_IO;
}

static journal_err fill_outputs(rc *c)
{
    journal_recovery *out = c->out;
    out->buffer_count = c->bufs.count;
    out->buffers = arena_get(c, c->bufs.count * sizeof *out->buffers, 8);
    if (!out->buffers) return JOURNAL_ERR_NOMEM;
    for (size_t i = 0; i < c->bufs.count; i++) {
        buf_entry *b = table_entry(&c->bufs, i);
        journal_recovered_buffer *r = &out->buffers[i];
        *r = (journal_recovered_buffer){
            .id = b->id, .verdict = b->verdict, .tree = b->tree, .origin = b->origin,
            .base_key = b->origin == JOURNAL_ORIGIN_BASE ? b->base_key : 0,
            .origin_sequence = b->boundary, .content_id = b->content_id,
            .saved_content_id = b->saved_id, .checkpoint_flags = b->flags,
            .literals = b->literals, .literal_count = b->literal_fill};
        b->tree = NULL; /* owned by the output now */
        journal_frame f;
        if (b->target_at != NONE) {
            journal_target t;
            if (read_record(c, b->target_at, b->target_seq, &f) != JOURNAL_OK ||
                journal_target_decode(&f, &t) != JOURNAL_CODEC_OK)
                return JOURNAL_ERR_IO;
            uint8_t *path = arena_get(c, t.path_len, 1);
            if (!path) return JOURNAL_ERR_NOMEM;
            if (t.path_len) memcpy(path, t.path, t.path_len);
            t.path = path;
            r->target = t;
            r->has_target = true;
        }
        if (b->view_at != NONE) {
            journal_view v;
            if (read_record(c, b->view_at, b->view_seq, &f) != JOURNAL_OK ||
                journal_view_decode(&f, &v) != JOURNAL_CODEC_OK)
                return JOURNAL_ERR_IO;
            uint8_t *op = arena_get(c, v.opaque_len, 1);
            if (!op) return JOURNAL_ERR_NOMEM;
            if (v.opaque_len) memcpy(op, v.opaque, v.opaque_len);
            v.opaque = op;
            r->view = v;
            r->has_view = true;
        }
    }
    if (c->tabs_at != NONE) {
        journal_frame f;
        uint32_t count = 0, active = 0;
        if (read_record(c, c->tabs_at, c->tabs_seq, &f) != JOURNAL_OK) return JOURNAL_ERR_IO;
        count = rd32(f.payload);
        out->tabs = arena_get(c, (size_t)count * sizeof *out->tabs, 8);
        if (!out->tabs) return JOURNAL_ERR_NOMEM;
        if (journal_tabs_decode(&f, out->tabs, count, &count, &active) != JOURNAL_CODEC_OK)
            return JOURNAL_ERR_IO;
        out->has_tabs = true;
        out->tab_count = count;
        out->tab_active = active;
    }
    return JOURNAL_OK;
}

/* ---- the whole recovery ---- */

static void close_sources(rc *c)
{
    for (size_t i = 0; i < c->gens.count; i++) {
        gen_entry *g = table_entry(&c->gens, i);
        os_file_close(&g->fd);
    }
}

static void close_trees(rc *c)
{
    for (size_t i = 0; i < c->bufs.count; i++) {
        buf_entry *b = table_entry(&c->bufs, i);
        if (!b->tree) continue;
        if (b->group != NONE) piece_checkpoint_abort(b->tree);
        piece_close(b->tree);
        b->tree = NULL;
    }
}

static journal_err recover_image(rc *c, os_dir *dir, const char *name, os_file *image)
{
    journal_recovery *out = c->out;
    /* 1. The preamble, before anything else. */
    uint8_t pre[JOURNAL_PREAMBLE_BYTES];
    size_t got = 0;
    if (journal_read_all(image, pre, sizeof pre, 0, &got) != OS_OK) return JOURNAL_ERR_IO;
    journal_preamble p;
    journal_codec_err pe = journal_preamble_decode(pre, got, &p);
    if (pe != JOURNAL_CODEC_OK) {
        out->failure = pe;
        out->verdict = JOURNAL_IMAGE_REFUSED;
        os_file_close(image);
        return rename_aside(c, dir, name);
    }
    out->generation_high_water = p.generation_high_water;
    out->buffer_high_water = p.buffer_high_water;

    /* 2. The stream to its first failure. */
    journal_stream_state s;
    journal_stream_init(&s);
    journal_codec_err err = JOURNAL_CODEC_OK;
    while (JOURNAL_PREAMBLE_BYTES + s.offset < c->w.file_len) {
        journal_frame f;
        os_err io;
        err = stream_step(&c->w, &s, &f, &io);
        if (io != OS_OK) return JOURNAL_ERR_IO;
        if (err != JOURNAL_CODEC_OK) break;
    }
    out->valid_bytes = JOURNAL_PREAMBLE_BYTES + s.valid;
    out->last_sequence = s.last_sequence;
    uint64_t fail_at = JOURNAL_PREAMBLE_BYTES + s.offset; /* the failing record */
    if (err == JOURNAL_CODEC_OK && s.phase != JOURNAL_STREAM_OUTSIDE) err = JOURNAL_CODEC_TRUNCATED;
    out->failure = err;
    if (err == JOURNAL_CODEC_OK) {
        out->verdict = JOURNAL_IMAGE_CLEAN;
    } else if (err == JOURNAL_CODEC_TRUNCATED && !genuine_record_after(&c->w, fail_at)) {
        /* Torn tail: never acknowledged, repaired in place. */
        if (os_file_set_size(image, out->valid_bytes) != OS_OK || os_file_sync(image) != OS_OK)
            return JOURNAL_ERR_IO;
        out->verdict = JOURNAL_IMAGE_REPAIRED;
        c->w.file_len = out->valid_bytes;
        c->w.len = 0;
    } else if (set_aside_copy(c, dir, name, image)) {
        out->verdict = JOURNAL_IMAGE_SET_ASIDE;
        c->w.file_len = out->valid_bytes;
        c->w.len = 0;
    } else {
        out->verdict = JOURNAL_IMAGE_DAMAGED;
    }
    os_stat st;
    if (os_file_stat(image, &st) != OS_OK) return JOURNAL_ERR_IO;
    out->image = st.id;

    /* 3. Plan over the committed prefix. */
    journal_stream_init(&s);
    bool nomem = false;
    pending_checkpoint pc = {0};
    uint64_t end = out->valid_bytes - JOURNAL_PREAMBLE_BYTES;
    while (s.offset < end) {
        uint64_t at = JOURNAL_PREAMBLE_BYTES + s.offset;
        journal_frame f;
        os_err io;
        if (stream_step(&c->w, &s, &f, &io) != JOURNAL_CODEC_OK) return JOURNAL_ERR_IO;
        if (!plan_record(c, &f, at, &s, &nomem, &pc))
            return nomem ? JOURNAL_ERR_NOMEM : JOURNAL_ERR_IO;
    }

    /* 4, 5, 6. */
    journal_err e = install_origins(c);
    if (e == JOURNAL_OK) e = apply_pass(c);
    bool any_failed = false;
    for (size_t i = 0; e == JOURNAL_OK && i < c->bufs.count; i++)
        any_failed |= ((buf_entry *)table_entry(&c->bufs, i))->verdict != JOURNAL_BUFFER_RECOVERED;
    if (e == JOURNAL_OK && any_failed) {
        e = literal_pass(c, false);
        for (size_t i = 0; e == JOURNAL_OK && i < c->bufs.count; i++) {
            buf_entry *b = table_entry(&c->bufs, i);
            if (b->literal_count == 0) continue;
            b->literals = arena_get(c, b->literal_count * sizeof *b->literals, 8);
            if (!b->literals) e = JOURNAL_ERR_NOMEM;
        }
        if (e == JOURNAL_OK) e = literal_pass(c, true);
    }
    if (e == JOURNAL_OK) e = fill_outputs(c);
    return e;
}

journal_err journal_recover(journal_recovery *out, const os_dir *dir, const char *name,
                            piece_reserve *reserve, journal_open_source_fn open_source, void *ctx)
{
    ASSERT(out != NULL && dir != NULL && name != NULL && reserve != NULL);
    ASSERT(strlen(name) >= 1 && strlen(name) <= JOURNAL_NAME_MAX && strchr(name, '/') == NULL);
    *out = (journal_recovery){0};
    os_dir d = OS_DIR_INIT;
    if (os_dir_open_at(&d, dir, ".") != OS_OK) return JOURNAL_ERR_IO;
    os_file image = OS_FILE_INIT;
    os_err oe = os_file_open_at(&image, &d, name, OS_OPEN_RDWR | OS_OPEN_NOFOLLOW | OS_OPEN_NONBLOCK);
    if (oe == OS_E_NOENT) {
        out->verdict = JOURNAL_IMAGE_ABSENT;
        os_dir_close(&d);
        return JOURNAL_OK;
    }
    os_stat st;
    if (oe != OS_OK || os_file_stat(&image, &st) != OS_OK || st.kind != OS_KIND_FILE) {
        os_file_close(&image);
        os_dir_close(&d);
        return JOURNAL_ERR_IO;
    }
    out->file_bytes = st.size;
    /* (P) Storage: the window (one record) + the source chunk + the
     * prefix scratch; then tables and copies bounded by the file: every
     * record is at least 32 B, so at most file/32 records each add at
     * most one buffer entry and one key entry (under 256 B each, doubled
     * by table growth) -> 16 x file; literal, target, view and TABS
     * copies are each at most the record they come from -> 1 x file;
     * 32 x file covers both with slack. Reserved lazily (MAP_NORESERVE),
     * touched only as used. */
    if (st.size > (SIZE_MAX - 4u * WINDOW) / 32u) {
        os_file_close(&image);
        os_dir_close(&d);
        return JOURNAL_ERR_NOMEM;
    }
    size_t size = 4u * WINDOW + 32u * (size_t)st.size;
    if (base_arena_init(&out->arena_, size) != 0) {
        os_file_close(&image);
        os_dir_close(&d);
        return JOURNAL_ERR_NOMEM;
    }
    rc c = {.out = out, .arena = &out->arena_, .reserve = reserve,
            .open_source = open_source, .open_ctx = ctx, .tabs_at = NONE};
    c.gens.esize = sizeof(gen_entry);
    c.bufs.esize = sizeof(buf_entry);
    c.w = (window){.fd = &image, .file_len = st.size};
    c.w.buf = arena_get(&c, WINDOW, 64);
    c.chunk = arena_get(&c, SOURCE_CHUNK, 64);
    c.prefix = arena_get(&c, JOURNAL_PREFIX_BYTES, 64);
    ASSERT(c.w.buf && c.chunk && c.prefix); /* within the 4 x WINDOW above */
    c.w.at = 0;
    c.w.len = 0;
    journal_err e = recover_image(&c, &d, name, &image);
    close_sources(&c);
    if (e != JOURNAL_OK) {
        close_trees(&c);
        for (size_t i = 0; out->buffers && i < out->buffer_count; i++)
            if (out->buffers[i].tree) piece_close(out->buffers[i].tree);
        base_arena_free(&out->arena_);
        journal_image_verdict v = out->verdict;
        *out = (journal_recovery){0};
        out->verdict = v;
    }
    os_file_close(&image);
    os_dir_close(&d);
    return e;
}

void journal_recovery_release(journal_recovery *rec)
{
    ASSERT(rec != NULL);
    for (size_t i = 0; rec->buffers && i < rec->buffer_count; i++) {
        if (rec->buffers[i].tree) piece_close(rec->buffers[i].tree);
        rec->buffers[i].tree = NULL;
    }
    base_arena_free(&rec->arena_);
    rec->buffers = NULL;
    rec->buffer_count = 0;
    rec->tabs = NULL;
    rec->tab_count = 0;
}
