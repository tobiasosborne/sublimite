/* STUB reference implementation from P1.3 (variants/P1.3/stub). Replaced by the P1.4 best-of. Slow by design. */
/* Deliberately simple reference implementation of piece.h: flat piece array,
 * O(n) operations, snapshot = O(pieces) copy. Proves the suite runs. */
#include "piece/piece.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define CH_SHIFT 16
#define CH_SIZE ((uint64_t)1 << CH_SHIFT)

typedef struct { uint64_t off, len; uint8_t add; } pc;

typedef struct core {           /* shared: add chunks + mapping */
    atomic_int rc;
    piece_allocator a;
    piece_map_hooks mh; int has_mh;
    uint8_t *orig; size_t orig_len; int orig_owned;
    uint8_t **ch; size_t nch, chcap; /* tree-only mutation */
    uint64_t add_len;
} core;

struct piece_tree { core *c; pc *p; size_t n, cap; piece_allocator a; int edited; int run; uint64_t run_end, run_add; };
struct piece_snapshot {
    atomic_int rc; core *c; piece_allocator a;
    pc *p; size_t n; uint8_t **ch; size_t nch;
};

static void *dm(void *x, size_t s) { (void)x; return malloc(s); }
static void df(void *x, void *p, size_t s) { (void)x; (void)s; free(p); }
piece_allocator piece_default_allocator(void) {
    piece_allocator a = { NULL, dm, df }; return a;
}

static void core_unref(core *c) {
    if (atomic_fetch_sub(&c->rc, 1) != 1) return;
    piece_allocator a = c->a;
    for (size_t i = 0; i < c->nch; i++) a.free(a.ctx, c->ch[i], CH_SIZE);
    if (c->ch) a.free(a.ctx, c->ch, c->chcap * sizeof *c->ch);
    if (c->orig_owned && c->orig_len) a.free(a.ctx, c->orig, c->orig_len);
    if (c->has_mh) c->mh.release(c->mh.ctx);
    a.free(a.ctx, c, sizeof *c);
}

piece_tree *piece_create(const piece_allocator *a) {
    piece_tree *t = a->alloc(a->ctx, sizeof *t);
    if (!t) return NULL;
    core *c = a->alloc(a->ctx, sizeof *c);
    if (!c) { a->free(a->ctx, t, sizeof *t); return NULL; }
    memset(c, 0, sizeof *c); memset(t, 0, sizeof *t);
    atomic_init(&c->rc, 1); c->a = *a; t->c = c; t->a = *a;
    return t;
}
void piece_destroy(piece_tree *t) {
    if (!t) return;
    if (t->p) t->a.free(t->a.ctx, t->p, t->cap * sizeof *t->p);
    core_unref(t->c);
    t->a.free(t->a.ctx, t, sizeof *t);
}

static int pgrow(piece_tree *t, size_t extra) {
    if (t->n + extra <= t->cap) return 0;
    size_t nc = t->cap ? t->cap : 8;
    while (nc < t->n + extra) nc *= 2;
    pc *np = t->a.alloc(t->a.ctx, nc * sizeof *np);
    if (!np) return PIECE_ERR_NOMEM;
    if (t->n) memcpy(np, t->p, t->n * sizeof *np);
    if (t->p) t->a.free(t->a.ctx, t->p, t->cap * sizeof *t->p);
    t->p = np; t->cap = nc; return 0;
}

int piece_init_copy(piece_tree *t, const uint8_t *d, size_t len) {
    if (t->edited || t->n) return PIECE_ERR_RANGE;
    if (len) {
        uint8_t *o = t->a.alloc(t->a.ctx, len);
        if (!o) return PIECE_ERR_NOMEM;
        if (pgrow(t, 1)) { t->a.free(t->a.ctx, o, len); return PIECE_ERR_NOMEM; }
        memcpy(o, d, len);
        t->c->orig = o; t->c->orig_len = len; t->c->orig_owned = 1;
        t->p[0] = (pc){ 0, len, 0 }; t->n = 1;
    }
    return 0;
}
int piece_init_mapped(piece_tree *t, const uint8_t *m, size_t len, const piece_map_hooks *h) {
    if (t->edited || t->n) return PIECE_ERR_RANGE;
    if (len && pgrow(t, 1)) return PIECE_ERR_NOMEM;
    if (h) { t->c->mh = *h; t->c->has_mh = 1; h->acquire(h->ctx); }
    t->c->orig = (uint8_t *)(uintptr_t)m; t->c->orig_len = len;
    if (len) { t->p[0] = (pc){ 0, len, 0 }; t->n = 1; }
    return 0;
}

/* append bytes to add buffer; returns start offset */
static int add_append(piece_tree *t, const uint8_t *d, size_t len, uint64_t *start) {
    core *c = t->c;
    uint64_t need_end = c->add_len + len;
    size_t want = (size_t)((need_end + CH_SIZE - 1) >> CH_SHIFT);
    if (want > c->chcap) {
        size_t nc = c->chcap ? c->chcap * 2 : 4;
        while (nc < want) nc *= 2;
        uint8_t **n = t->a.alloc(t->a.ctx, nc * sizeof *n);
        if (!n) return PIECE_ERR_NOMEM;
        if (c->nch) memcpy(n, c->ch, c->nch * sizeof *n);
        if (c->ch) t->a.free(t->a.ctx, c->ch, c->chcap * sizeof *n);
        c->ch = n; c->chcap = nc;
    }
    while (c->nch < want) {
        uint8_t *b = t->a.alloc(t->a.ctx, CH_SIZE);
        if (!b) return PIECE_ERR_NOMEM;
        c->ch[c->nch++] = b;
    }
    *start = c->add_len;
    uint64_t pos = c->add_len; size_t done = 0;
    while (done < len) {
        size_t in = (size_t)(pos & (CH_SIZE - 1)), k = (size_t)CH_SIZE - in;
        if (k > len - done) k = len - done;
        memcpy(c->ch[pos >> CH_SHIFT] + in, d + done, k);
        pos += k; done += k;
    }
    c->add_len = need_end;
    return 0;
}

static const uint8_t *pdata(const core *c, uint8_t *const *ch, const pc *p, uint64_t skip) {
    if (!p->add) return c->orig + p->off + skip;
    uint64_t o = p->off + skip;
    return ch[o >> CH_SHIFT] + (o & (CH_SIZE - 1));
}

/* split so a piece boundary exists at off; returns index of piece starting at off */
static size_t split_at(piece_tree *t, uint64_t off) {
    uint64_t pos = 0;
    for (size_t i = 0; i < t->n; i++) {
        if (off == pos) return i;
        if (off < pos + t->p[i].len) {
            uint64_t k = off - pos;
            memmove(t->p + i + 2, t->p + i + 1, (t->n - i - 1) * sizeof *t->p);
            t->p[i + 1] = (pc){ t->p[i].off + k, t->p[i].len - k, t->p[i].add };
            t->p[i].len = k; t->n++;
            return i + 1;
        }
        pos += t->p[i].len;
    }
    return t->n;
}

uint64_t piece_len(const piece_tree *t) {
    uint64_t s = 0; for (size_t i = 0; i < t->n; i++) s += t->p[i].len; return s;
}
uint64_t piece_piece_count(const piece_tree *t) { return t->n; }

int piece_insert(piece_tree *t, uint64_t off, const uint8_t *d, size_t len) {
    if (off > piece_len(t)) return PIECE_ERR_RANGE;
    if (!len) return 0;
    size_t chunks = (size_t)((len + CH_SIZE - 1) >> CH_SHIFT) + 1;
    if (pgrow(t, chunks + 1)) return PIECE_ERR_NOMEM;
    uint64_t start;
    if (add_append(t, d, len, &start)) return PIECE_ERR_NOMEM;
    t->edited = 1;
    if (t->run && t->run_end == off && t->run_add == start && (start & (CH_SIZE - 1)) &&
        len <= CH_SIZE - (start & (CH_SIZE - 1))) {
        uint64_t pos = 0;
        for (size_t i = 0; i < t->n; i++) {
            pos += t->p[i].len;
            if (pos == off) {
                if (t->p[i].add && t->p[i].off + t->p[i].len == start) {
                    t->p[i].len += len;
                    t->run_end = off + len; t->run_add = start + len;
                    return 0;
                }
                break;
            }
            if (pos > off) break;
        }
    }
    t->run = 0;
    size_t at = split_at(t, off);
    /* pieces must not span chunk boundaries */
    pc tmp[1 << 8]; size_t k = 0;
    uint64_t o = start, rem = len;
    pc *buf = tmp;
    if (chunks > 256) {
        buf = t->a.alloc(t->a.ctx, chunks * sizeof *buf);
        if (!buf) return PIECE_ERR_NOMEM; /* add bytes orphaned; content unchanged */
    }
    while (rem) {
        uint64_t take = CH_SIZE - (o & (CH_SIZE - 1));
        if (take > rem) take = rem;
        buf[k++] = (pc){ o, take, 1 }; o += take; rem -= take;
    }
    if (pgrow(t, k)) { if (buf != tmp) t->a.free(t->a.ctx, buf, chunks * sizeof *buf); return PIECE_ERR_NOMEM; }
    memmove(t->p + at + k, t->p + at, (t->n - at) * sizeof *t->p);
    memcpy(t->p + at, buf, k * sizeof *buf);
    t->n += k;
    if (buf != tmp) t->a.free(t->a.ctx, buf, chunks * sizeof *buf);
    t->run = 1; t->run_end = off + len; t->run_add = start + len;
    return 0;
}

static int ref_push(piece_ref *r, uint64_t o, uint64_t l) {
    if (r->nspans && r->span[r->nspans - 1].add_off + r->span[r->nspans - 1].len == o) {
        r->span[r->nspans - 1].len += l;
    } else {
        if (r->nspans >= PIECE_REF_SPANS) return 1;
        r->span[r->nspans].add_off = o; r->span[r->nspans].len = l; r->nspans++;
    }
    r->len += l;
    return 0;
}
static int rd(const core *c, uint8_t *const *ch, const pc *p, size_t n, uint64_t off, uint8_t *dst, size_t len);

int piece_delete(piece_tree *t, uint64_t off, uint64_t len, piece_ref *ref) {
    uint64_t total = piece_len(t);
    if (off > total || len > total - off) return PIECE_ERR_RANGE;
    piece_ref local; if (!ref) ref = &local;
    memset(ref, 0, sizeof *ref);
    if (!len) return 0;
    t->edited = 1; t->run = 0;
    /* pass 1: estimate span count (conservative) */
    uint64_t pos = 0; size_t est = 0; int prev_orig = 0;
    for (size_t i = 0; i < t->n; i++) {
        uint64_t s = pos, e = pos + t->p[i].len;
        pos = e;
        if (e <= off) continue;
        if (s >= off + len) break;
        if (t->p[i].add) { est++; prev_orig = 0; }
        else if (!prev_orig) { est++; prev_orig = 1; }
    }
    if (est > PIECE_REF_SPANS) {
        uint8_t *tmp = t->a.alloc(t->a.ctx, (size_t)len);
        if (!tmp) return PIECE_ERR_NOMEM;
        uint64_t st;
        if (rd(t->c, t->c->ch, t->p, t->n, off, tmp, (size_t)len) ||
            add_append(t, tmp, (size_t)len, &st)) {
            t->a.free(t->a.ctx, tmp, (size_t)len); return PIECE_ERR_NOMEM;
        }
        t->a.free(t->a.ctx, tmp, (size_t)len);
        ref_push(ref, st, len);
    } else {
        pos = 0;
        for (size_t i = 0; i < t->n; i++) {
            uint64_t s = pos, e = pos + t->p[i].len;
            pos = e;
            if (e <= off) continue;
            if (s >= off + len) break;
            uint64_t a = s > off ? s : off, b = e < off + len ? e : off + len, st;
            if (!t->p[i].add) {
                if (add_append(t, t->c->orig + t->p[i].off + (a - s), (size_t)(b - a), &st))
                    return PIECE_ERR_NOMEM;
            } else st = t->p[i].off + (a - s);
            if (ref_push(ref, st, b - a)) return PIECE_ERR_NOMEM; /* unreachable */
        }
    }
    if (pgrow(t, 2)) return PIECE_ERR_NOMEM;
    size_t a = split_at(t, off);
    size_t b = split_at(t, off + len);
    memmove(t->p + a, t->p + b, (t->n - b) * sizeof *t->p);
    t->n -= b - a;
    return 0;
}

int piece_insert_ref(piece_tree *t, uint64_t off, const piece_ref *ref) {
    if (off > piece_len(t)) return PIECE_ERR_RANGE;
    if (ref->nspans > PIECE_REF_SPANS) return PIECE_ERR_RANGE;
    size_t k = 0;
    for (uint32_t i = 0; i < ref->nspans; i++) {
        uint64_t o = ref->span[i].add_off, l = ref->span[i].len;
        if (o > t->c->add_len || l > t->c->add_len - o) return PIECE_ERR_RANGE;
        k += (size_t)((l + CH_SIZE - 1) >> CH_SHIFT) + 1;
    }
    if (!k) return 0;
    if (pgrow(t, k + 1)) return PIECE_ERR_NOMEM;
    t->edited = 1; t->run = 0;
    size_t at = split_at(t, off);
    for (uint32_t i = 0; i < ref->nspans; i++) {
        uint64_t o = ref->span[i].add_off, rem = ref->span[i].len;
        while (rem) {
            uint64_t take = CH_SIZE - (o & (CH_SIZE - 1));
            if (take > rem) take = rem;
            memmove(t->p + at + 1, t->p + at, (t->n - at) * sizeof *t->p);
            t->p[at++] = (pc){ o, take, 1 }; t->n++;
            o += take; rem -= take;
        }
    }
    return 0;
}

/* shared read helpers over (core, ch, pieces) */
static int rd(const core *c, uint8_t *const *ch, const pc *p, size_t n, uint64_t off, uint8_t *dst, size_t len) {
    uint64_t pos = 0;
    for (size_t i = 0; i < n && len; i++) {
        uint64_t e = pos + p[i].len;
        if (off < e) {
            uint64_t skip = off - pos, avail = p[i].len - skip;
            while (avail && len) {
                const uint8_t *src = pdata(c, ch, &p[i], skip);
                uint64_t k = avail;
                if (p[i].add) { uint64_t room = CH_SIZE - ((p[i].off + skip) & (CH_SIZE - 1)); if (k > room) k = room; }
                if (k > len) k = len;
                memcpy(dst, src, (size_t)k);
                dst += k; len -= (size_t)k; skip += k; avail -= k; off += k;
            }
        }
        pos = e;
    }
    return len ? PIECE_ERR_RANGE : 0;
}
static uint64_t total_of(const pc *p, size_t n) { uint64_t s = 0; for (size_t i = 0; i < n; i++) s += p[i].len; return s; }
static int rdchk(const core *c, uint8_t *const *ch, const pc *p, size_t n, uint64_t off, uint8_t *dst, size_t len) {
    uint64_t tot = total_of(p, n);
    if (off > tot || len > tot - off) return PIECE_ERR_RANGE;
    return rd(c, ch, p, n, off, dst, len);
}
static uint64_t nl_before(const core *c, uint8_t *const *ch, const pc *p, size_t n, uint64_t off) {
    uint64_t pos = 0, cnt = 0;
    for (size_t i = 0; i < n && pos < off; i++) {
        uint64_t k = p[i].len; if (k > off - pos) k = off - pos;
        for (uint64_t j = 0; j < k;) {
            uint64_t o = p[i].off + j; uint64_t run = k - j;
            if (p[i].add) { uint64_t room = CH_SIZE - (o & (CH_SIZE - 1)); if (run > room) run = room; }
            const uint8_t *q = pdata(c, ch, &p[i], j);
            for (uint64_t x = 0; x < run; x++) cnt += q[x] == '\n';
            j += run;
        }
        pos += p[i].len;
    }
    return cnt;
}
static uint64_t l2b(const core *c, uint8_t *const *ch, const pc *p, size_t n, uint64_t line) {
    if (line == 0) return 0;
    uint64_t pos = 0, cnt = 0;
    for (size_t i = 0; i < n; i++) {
        for (uint64_t j = 0; j < p[i].len; j++) {
            if (*pdata(c, ch, &p[i], j) == '\n' && ++cnt == line) return pos + j + 1;
        }
        pos += p[i].len;
    }
    return pos;
}

int piece_read(const piece_tree *t, uint64_t off, uint8_t *dst, size_t len) {
    return rdchk(t->c, t->c->ch, t->p, t->n, off, dst, len);
}
uint64_t piece_line_count(const piece_tree *t) { return nl_before(t->c, t->c->ch, t->p, t->n, piece_len(t)) + 1; }
uint64_t piece_line_to_byte(const piece_tree *t, uint64_t line) { return l2b(t->c, t->c->ch, t->p, t->n, line); }
uint64_t piece_byte_to_line(const piece_tree *t, uint64_t off) { return nl_before(t->c, t->c->ch, t->p, t->n, off); }

piece_snapshot *piece_snapshot_take(piece_tree *t) {
    piece_snapshot *s = t->a.alloc(t->a.ctx, sizeof *s);
    if (!s) return NULL;
    memset(s, 0, sizeof *s);
    if (t->n) { s->p = t->a.alloc(t->a.ctx, t->n * sizeof *s->p); if (!s->p) goto fail; memcpy(s->p, t->p, t->n * sizeof *s->p); }
    if (t->c->nch) { s->ch = t->a.alloc(t->a.ctx, t->c->nch * sizeof *s->ch); if (!s->ch) goto fail; memcpy(s->ch, t->c->ch, t->c->nch * sizeof *s->ch); }
    s->n = t->n; s->nch = t->c->nch; s->a = t->a; s->c = t->c;
    atomic_init(&s->rc, 1); atomic_fetch_add(&t->c->rc, 1);
    if (t->c->has_mh) t->c->mh.acquire(t->c->mh.ctx);
    return s;
fail:
    if (s->p) t->a.free(t->a.ctx, s->p, t->n * sizeof *s->p);
    t->a.free(t->a.ctx, s, sizeof *s);
    return NULL;
}
piece_snapshot *piece_snapshot_retain(piece_snapshot *s) { atomic_fetch_add(&s->rc, 1); return s; }
void piece_snapshot_release(piece_snapshot *s) {
    if (!s || atomic_fetch_sub(&s->rc, 1) != 1) return;
    piece_allocator a = s->a; core *c = s->c;
    if (c->has_mh) c->mh.release(c->mh.ctx);
    if (s->p) a.free(a.ctx, s->p, s->n * sizeof *s->p);
    if (s->ch) a.free(a.ctx, s->ch, s->nch * sizeof *s->ch);
    a.free(a.ctx, s, sizeof *s);
    core_unref(c);
}
uint64_t piece_snapshot_len(const piece_snapshot *s) { return total_of(s->p, s->n); }
uint64_t piece_snapshot_line_count(const piece_snapshot *s) { return nl_before(s->c, s->ch, s->p, s->n, total_of(s->p, s->n)) + 1; }
int piece_snapshot_read(const piece_snapshot *s, uint64_t off, uint8_t *dst, size_t len) { return rdchk(s->c, s->ch, s->p, s->n, off, dst, len); }
uint64_t piece_snapshot_line_to_byte(const piece_snapshot *s, uint64_t l) { return l2b(s->c, s->ch, s->p, s->n, l); }
uint64_t piece_snapshot_byte_to_line(const piece_snapshot *s, uint64_t o) { return nl_before(s->c, s->ch, s->p, s->n, o); }

/* iterator: priv[0] = piece index, priv[1] = skip within piece */
static void it_seek(piece_iter *it, const pc *p, size_t n, uint64_t off) {
    uint64_t pos = 0; size_t i = 0;
    while (i < n && off >= pos + p[i].len) { pos += p[i].len; i++; }
    it->priv[0] = i; it->priv[1] = i < n ? off - pos : 0; it->pos = off;
}
void piece_iter_begin(piece_iter *it, const piece_tree *t, uint64_t off) {
    it->src = t; it->is_snap = 0; it_seek(it, t->p, t->n, off);
}
void piece_iter_begin_snapshot(piece_iter *it, const piece_snapshot *s, uint64_t off) {
    it->src = s; it->is_snap = 1; it_seek(it, s->p, s->n, off);
}
int piece_iter_next(piece_iter *it, const uint8_t **out, size_t *n) {
    const core *c; uint8_t *const *ch; const pc *p; size_t np;
    if (it->is_snap) { const piece_snapshot *s = it->src; c = s->c; ch = s->ch; p = s->p; np = s->n; }
    else { const piece_tree *t = it->src; c = t->c; ch = t->c->ch; p = t->p; np = t->n; }
    if (it->priv[0] >= np) return 0;
    const pc *q = &p[it->priv[0]];
    uint64_t skip = it->priv[1], k = q->len - skip;
    *out = pdata(c, ch, q, skip); *n = (size_t)k;
    it->pos += k; it->priv[0]++; it->priv[1] = 0;
    return 1;
}
