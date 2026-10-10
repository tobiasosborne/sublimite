/* Sparse line index. See docs/decisions/P1.6{b,c}.md. */
#include "lineidx/lineidx.h"
#include "scan/scan.h"
#include "file/file.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FL_BUILT 1u
#define FL_NONASCII 2u
#define FL_EDITED 4u
#define RES_DONE (1u << 31)
#define RES_NA (1u << 30)
#define RES_COUNT ((1u << 17) - 1u)
#define BLOCK_MAX 512u
#define POLL_CHUNKS 64u
#define PUBLISH_CHUNKS 16u
#define SPAN_MAX 256u
#define SCAN_BLOCK 16384u
#define UI_CPU_NS 500000ull
#define MSG_SEEK (LINEIDX_MSG_PROGRESS + 1u)
#define DEFAULT_BPL 40u

_Static_assert(LINEIDX_CHUNK == (uint32_t)UINT16_MAX + 1u, "length-minus-one fits 16 bits");
_Static_assert(LINEIDX_CHUNK <= RES_COUNT, "newline count includes a full LF chunk");

/* Relative lengths avoid an absolute-offset update of the suffix. Leaves are
 * linked within bounded blocks; the block treap holds incremental summaries.
 * IDs are private fixed-pool indices, with zero reserved as null. */
typedef struct entry { uint32_t next, len, nl, fl; } entry;
_Static_assert(sizeof(entry) == 16, "16 B per chunk");
typedef struct block {
    uint64_t own_bytes, own_lines, bytes, lines;
    uint32_t left, right, parent, head, tail, count, total, priority;
    uint32_t edited, nonascii, unbuilt;
    uint16_t own_edited, own_nonascii, own_unbuilt;
} block;

typedef struct lineidx_job {
    struct lineidx_job *next;
    lineidx_src src;
    file_backing *backing; /* borrowed from the index until physical completion */
    size_t source_bytes;
    work_pool *pool;
    work_handle h;
    size_t n, applied, available;
    uint16_t *lengths; /* length minus one; empty source is handled separately */
    uint32_t *res;     /* 17-bit count (including 65536), nonascii, built */
    uint32_t generation, apply_block, apply_entry;
    bool seeking;
    uint64_t target;
    size_t cursor, sealed;                /* worker continuation */
    uint64_t chunk_start, scan_pos, scan_nl, scan_na, cumulative, answer;
    bool seek_pending;
    uint64_t cpu_begin, cancel_cpu_ns; /* worker; read only after work completion */
} lineidx_job;
struct lineidx {
    entry *e;
    block *b;
    size_t n, cap, block_cap;
    uint32_t root, garbage, free_entry, free_block, free_blocks, sequence;
    size_t free_entries;
    uint64_t len;
    file_backing *backing;
    bool faulted;
    bool backing_bound;
    bool dirty;
    size_t pfx_n;
    uint64_t pfx_lines, pfx_bytes;
    lineidx_job *job, *retired;
    uint32_t gen;
    uint64_t seek_pos, seek_nl, seek_na, seek_last, seek_line, seek_value;
    bool seek_cached, async_ready;
    uint64_t query_start, query_end, query_pos, query_nl, query_line, query_last;
    bool query_active;
    uint64_t async_value;
    uint32_t refresh_id;
    uint64_t refresh_pos, refresh_nl, refresh_na;
    uint64_t foreground_work, scanned_bytes;
    uint64_t last_cancel_cpu_ns;
};

size_t lineidx_entry_bytes(void) { return sizeof(entry); }
uint64_t lineidx_foreground_work(const lineidx *x) { return x->foreground_work; }
uint64_t lineidx_scanned_bytes(const lineidx *x) { return x->scanned_bytes; }
static uint64_t now_ns(clockid_t clock)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ---- summarized fixed-pool chunk rope (UI only) ---- */
static void pull(lineidx *x, uint32_t id)
{
    block *v = &x->b[id], *l = &x->b[v->left], *r = &x->b[v->right];
    x->foreground_work++;
    v->bytes = l->bytes + v->own_bytes + r->bytes;
    v->lines = l->lines + v->own_lines + r->lines;
    v->total = l->total + v->count + r->total;
    v->edited = l->edited + v->own_edited + r->edited;
    v->nonascii = l->nonascii + v->own_nonascii + r->nonascii;
    v->unbuilt = l->unbuilt + v->own_unbuilt + r->unbuilt;
    if (v->left) l->parent = id;
    if (v->right) r->parent = id;
}
static void recount(lineidx *x, uint32_t id)
{
    block *v = &x->b[id];
    v->own_bytes = v->own_lines = 0;
    v->own_edited = v->own_nonascii = v->own_unbuilt = 0;
    for (uint32_t i = v->head; i; i = x->e[i].next) {
        const entry *e = &x->e[i];
        x->foreground_work++;
        v->own_bytes += e->len; v->own_lines += e->nl;
        if (e->fl & FL_EDITED) v->own_edited++;
        if (e->fl & FL_NONASCII) v->own_nonascii++;
        if (!(e->fl & FL_BUILT)) v->own_unbuilt++;
    }
    pull(x, id);
}
static void update_up(lineidx *x, uint32_t id)
{
    while (id) { pull(x, id); id = x->b[id].parent; }
    x->dirty = true;
}
static uint32_t merge(lineidx *x, uint32_t a, uint32_t b)
{
    if (!a || !b) {
        uint32_t id = a ? a : b;
        if (id) x->b[id].parent = 0;
        return id;
    }
    uint32_t root;
    if (x->b[a].priority < x->b[b].priority) {
        x->b[a].right = merge(x, x->b[a].right, b);
        pull(x, a); root = a;
    } else {
        x->b[b].left = merge(x, a, x->b[b].left);
        pull(x, b); root = b;
    }
    x->b[root].parent = 0;
    return root;
}
static void release_block(lineidx *x, uint32_t id)
{
    memset(&x->b[id], 0, sizeof x->b[id]);
    x->b[id].left = x->free_block;
    x->free_block = id; x->free_blocks++;
}
/* Retire a detached block in O(tree height), linking all of its entries into
 * the free pool in O(1). No sweep of a deleted range is needed. */
static bool reclaim_one(lineidx *x)
{
    uint32_t id = x->garbage;
    if (!id) return false;
    block *v = &x->b[id];
    x->garbage = merge(x, v->left, v->right);
    x->e[v->tail].next = x->free_entry;
    x->free_entry = v->head; x->free_entries += v->count;
    release_block(x, id);
    return true;
}
static uint32_t take_block(lineidx *x)
{
    if (!x->free_block && !reclaim_one(x)) return 0;
    uint32_t id = x->free_block;
    x->free_block = x->b[id].left; x->free_blocks--;
    memset(&x->b[id], 0, sizeof x->b[id]);
    uint32_t z = (x->sequence += 0x9e3779b9u);
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    x->b[id].priority = z ^ (z >> 16);
    return id;
}
static uint32_t take_entry(lineidx *x)
{
    if (!x->free_entry && !reclaim_one(x)) return 0;
    uint32_t id = x->free_entry;
    x->free_entry = x->e[id].next; x->free_entries--;
    memset(&x->e[id], 0, sizeof x->e[id]);
    return id;
}
static void split(lineidx *x, uint32_t root, uint32_t n, uint32_t *a, uint32_t *b)
{
    if (!root) { *a = *b = 0; return; }
    block *v = &x->b[root];
    uint32_t left = x->b[v->left].total;
    if (n < left) {
        split(x, v->left, n, a, &v->left);
        pull(x, root); *b = root;
    } else if (n > left + v->count) {
        split(x, v->right, n - left - v->count, &v->right, b);
        pull(x, root); *a = root;
    } else if (n == left) {
        *a = v->left; v->left = 0; pull(x, root); *b = root;
    } else if (n == left + v->count) {
        *b = v->right; v->right = 0; pull(x, root); *a = root;
    } else {
        uint32_t other = take_block(x), k = n - left;
        block *w = &x->b[other];
        uint32_t tail = v->head;
        for (uint32_t i = 1; i < k; i++) { x->foreground_work++; tail = x->e[tail].next; }
        uint32_t old_left = v->left, old_right = v->right;
        w->head = x->e[tail].next; w->tail = v->tail; w->count = v->count - k;
        x->e[tail].next = 0;
        v->tail = tail; v->count = k; v->left = v->right = 0;
        recount(x, root); recount(x, other);
        *a = merge(x, old_left, root); *b = merge(x, other, old_right);
    }
    if (*a) x->b[*a].parent = 0;
    if (*b) x->b[*b].parent = 0;
}
static uint32_t edge_block(const lineidx *x, uint32_t root, bool last)
{
    while (root && (last ? x->b[root].right : x->b[root].left))
        root = last ? x->b[root].right : x->b[root].left;
    return root;
}
/* Adjacent blocks coalesce after each edit. Thus live block capacity scales
 * with chunks / (BLOCK_MAX/2), rather than the number of edits. */
static uint32_t concat(lineidx *x, uint32_t a, uint32_t b)
{
    if (!a || !b) return merge(x, a, b);
    uint32_t last = edge_block(x, a, true), first = edge_block(x, b, false);
    if (x->b[last].count + x->b[first].count > BLOCK_MAX) return merge(x, a, b);
    uint32_t left, right, one, two;
    split(x, a, x->b[a].total - x->b[last].count, &left, &one);
    split(x, b, x->b[first].count, &two, &right);
    x->e[x->b[one].tail].next = x->b[two].head;
    x->b[one].tail = x->b[two].tail;
    x->b[one].count += x->b[two].count;
    recount(x, one); release_block(x, two);
    return merge(x, merge(x, left, one), right);
}
typedef struct location { uint32_t block, id, ordinal; uint64_t byte, lines; } location;
static location locate(lineidx *x, uint32_t ordinal)
{
    location at = {0};
    uint32_t root = x->root, k = ordinal;
    while (root) {
        block *v = &x->b[root], *l = &x->b[v->left];
        x->foreground_work++;
        if (k < l->total) { root = v->left; continue; }
        at.byte += l->bytes; at.lines += l->lines; at.ordinal += l->total;
        k -= l->total;
        if (k < v->count) {
            at.block = root; at.id = v->head;
            while (k--) {
                x->foreground_work++;
                at.byte += x->e[at.id].len; at.lines += x->e[at.id].nl;
                at.id = x->e[at.id].next; at.ordinal++;
            }
            return at;
        }
        k -= v->count; at.byte += v->own_bytes; at.lines += v->own_lines;
        at.ordinal += v->count; root = v->right;
    }
    return at;
}
static location locate_byte(lineidx *x, uint64_t off)
{
    if (off == x->len) return locate(x, (uint32_t)x->n - 1u);
    location at = {0};
    uint32_t root = x->root;
    while (root) {
        block *v = &x->b[root], *l = &x->b[v->left];
        x->foreground_work++;
        if (off < at.byte + l->bytes) { root = v->left; continue; }
        at.byte += l->bytes; at.lines += l->lines; at.ordinal += l->total;
        if (off < at.byte + v->own_bytes) {
            at.block = root; at.id = v->head;
            while (off >= at.byte + x->e[at.id].len) {
                x->foreground_work++;
                at.byte += x->e[at.id].len; at.lines += x->e[at.id].nl;
                at.id = x->e[at.id].next; at.ordinal++;
            }
            return at;
        }
        at.byte += v->own_bytes; at.lines += v->own_lines; at.ordinal += v->count;
        root = v->right;
    }
    return at;
}
static uint32_t first_flag(lineidx *x, bool edited)
{
    uint32_t root = x->root, ordinal = 0;
    while (root) {
        block *v = &x->b[root], *l = &x->b[v->left];
        x->foreground_work++;
        if (edited ? l->edited : l->unbuilt) { root = v->left; continue; }
        ordinal += l->total;
        if (edited ? v->own_edited : v->own_unbuilt) {
            for (uint32_t id = v->head; id; id = x->e[id].next, ordinal++) {
                x->foreground_work++;
                if (edited ? (x->e[id].fl & FL_EDITED) != 0 : !(x->e[id].fl & FL_BUILT)) return ordinal;
            }
        }
        ordinal += v->count; root = v->right;
    }
    return (uint32_t)x->n;
}
static void derive(lineidx *x)
{
    if (!x->dirty) return;
    if (!x->b[x->root].unbuilt) {
        x->pfx_n = x->n; x->pfx_bytes = x->len; x->pfx_lines = x->b[x->root].lines;
    } else {
        location at = locate(x, first_flag(x, false));
        x->pfx_n = at.ordinal; x->pfx_bytes = at.byte; x->pfx_lines = at.lines;
    }
    x->dirty = false;
}
static void set_built(lineidx *x, location at, uint32_t nl, bool na)
{
    entry *e = &x->e[at.id]; block *v = &x->b[at.block];
    v->own_lines = v->own_lines - e->nl + nl;
    if (e->fl & FL_EDITED) v->own_edited--;
    if (e->fl & FL_NONASCII) v->own_nonascii--;
    if (!(e->fl & FL_BUILT)) v->own_unbuilt--;
    if (na) v->own_nonascii++;
    e->nl = nl; e->fl = FL_BUILT | (na ? FL_NONASCII : 0);
    update_up(x, at.block);
}
static uint32_t successor(const lineidx *x, uint32_t id)
{
    if (x->b[id].right) return edge_block(x, x->b[id].right, false);
    uint32_t parent = x->b[id].parent;
    while (parent && x->b[parent].right == id) { id = parent; parent = x->b[id].parent; }
    return parent;
}

/* ---- content helpers ---- */
static void scan_range(const lineidx_src *s, uint64_t start, uint64_t len, uint64_t *nl, uint64_t *na)
{
    uint64_t pos = start, end = start + len, a = 0, b = 0;
    while (pos < end) {
        const uint8_t *p;
        size_t k = s->span(s->ctx, pos, &p);
        if (!k) break;
        if (k > end - pos) k = (size_t)(end - pos);
        scan_counts c = scan_count(p, k); a += c.newlines; b += c.nonascii; pos += k;
    }
    *nl = a; *na = b;
}
static uint64_t nth_newline(const lineidx_src *s, uint64_t start, uint64_t len, uint64_t k)
{
    uint64_t pos = start, end = start + len;
    while (pos < end) {
        const uint8_t *p;
        size_t m = s->span(s->ctx, pos, &p);
        if (!m) break;
        if (m > end - pos) m = (size_t)(end - pos);
        uint64_t count = scan_count(p, m).newlines;
        if (k < count) {
            const uint8_t *q = scan_find_nth_newline(p, m, k);
            return q ? pos + (uint64_t)(q - p) : UINT64_MAX;
        }
        k -= count; pos += m;
    }
    return UINT64_MAX;
}
static uint64_t align_back(const lineidx_src *s, uint64_t lo, uint64_t hi)
{
    uint64_t pos = lo, last = 0;
    while (pos < hi) {
        const uint8_t *p;
        size_t m = s->span(s->ctx, pos, &p);
        if (!m) break;
        if (m > hi - pos) m = (size_t)(hi - pos);
        const uint8_t *q = memrchr(p, '\n', m);
        if (q) last = pos + (uint64_t)(q - p) + 1u;
        pos += m;
    }
    return last;
}
static location locate_line(lineidx *x, uint64_t line)
{
    location at = {0}; uint32_t root = x->root;
    while (root) {
        block *v = &x->b[root], *l = &x->b[v->left];
        x->foreground_work++;
        if (line <= at.lines + l->lines) { root = v->left; continue; }
        at.byte += l->bytes; at.lines += l->lines; at.ordinal += l->total;
        if (line <= at.lines + v->own_lines) {
            at.block = root; at.id = v->head;
            while (at.lines + x->e[at.id].nl < line) {
                x->foreground_work++;
                at.byte += x->e[at.id].len; at.lines += x->e[at.id].nl;
                at.id = x->e[at.id].next; at.ordinal++;
            }
            return at;
        }
        at.byte += v->own_bytes; at.lines += v->own_lines; at.ordinal += v->count;
        root = v->right;
    }
    return at;
}

/* ---- sealed worker ranges; UI callbacks only stage availability ---- */
static bool job_done(const lineidx_job *j) { return work_handle_finished(j->pool, j->h); }
static void job_free(lineidx_job *j)
{
    (void)work_mailbox_bind(j->pool, j->h, j->generation, NULL, NULL);
    if (j->src.release) j->src.release(j->src.ctx);
    free(j->lengths); free(j->res); free(j);
}
static void reap(lineidx *x)
{
    if (x->retired && job_done(x->retired)) {
        lineidx_job *j = x->retired;
        x->last_cancel_cpu_ns = j->cancel_cpu_ns;
        x->retired = j->next; job_free(j);
    }
}
static void retire_job(lineidx *x)
{
    lineidx_job *j = x->job;
    if (!j) return;
    (void)work_mailbox_bind(j->pool, j->h, j->generation, NULL, NULL);
    x->job = NULL; j->next = x->retired; x->retired = j;
}
typedef struct result_range { uint64_t first, end; } result_range;
static bool worker_stop(work_ctx *c)
{
    lineidx_job *j = c->arg;
    if (!work_should_stop(c) && !file_backing_faulted(j->backing)) return false;
    if (!j->cancel_cpu_ns && j->cpu_begin)
        j->cancel_cpu_ns = now_ns(CLOCK_THREAD_CPUTIME_ID) - j->cpu_begin;
    return true;
}
/* Backpressure retains progress and yields the execution resource. */
static bool publish(work_ctx *c, uint32_t kind, const void *data, size_t size)
{
    if (worker_stop(c)) return false;
    work_msg msg = {.kind = kind, .generation = c->generation};
    memcpy(msg.data, data, size);
    return work_publish(c, &msg);
}
static bool publish_range(work_ctx *c, size_t first, size_t end)
{
    if (first == end) return !worker_stop(c);
    result_range range = {first, end};
    return publish(c, LINEIDX_MSG_PROGRESS, &range, sizeof range);
}
_Static_assert(sizeof(result_range) <= WORK_MSG_DATA, "line index range fits mailbox");
static void receive_results(const work_msg *msg, void *ud)
{
    lineidx *x = ud; lineidx_job *j = x->job;
    if (!j || msg->generation != j->generation || j->generation != x->gen ||
        msg->slot_ != j->h.slot || msg->epoch_ != j->h.epoch) return;
    if (file_backing_faulted(j->backing)) return;
    if (msg->kind == LINEIDX_MSG_PROGRESS) {
        result_range range; memcpy(&range, msg->data, sizeof range);
        if (range.first != j->available || range.end < range.first || range.end > j->n) return;
        j->available = (size_t)range.end;
    } else if (msg->kind == MSG_SEEK && j->seeking) {
        uint64_t value; memcpy(&value, msg->data, sizeof value);
        if (value <= x->len) { x->async_value = value; x->async_ready = true; }
    }
}
/* One bounded invocation, preserving partial chunk/seek counts. Cancellation
 * surrounds each callback and 16 KiB scanner block. CPU checkpoints belong to
 * the current worker thread, since a continuation may move between workers. */
static void build_fn(work_ctx *c)
{
    lineidx_job *j = c->arg;
    j->cpu_begin = now_ns(CLOCK_THREAD_CPUTIME_ID);
    uint64_t deadline = j->cpu_begin + 4000000ull;
    size_t bytes = 0, spans = 0, chunks = 0;
    if (worker_stop(c)) return;
    if (j->cursor > j->sealed) {
        if (!publish_range(c, j->sealed, j->cursor)) goto yield;
        j->sealed = j->cursor;
    }
    if (j->seek_pending) goto seek_result;
    while (j->cursor < j->n) {
        if (worker_stop(c)) return;
        size_t i = j->cursor;
        uint32_t r = j->res[i];
        uint64_t count = r & RES_COUNT;
        uint64_t end = j->chunk_start + (j->src.len ? (uint64_t)j->lengths[i] + 1u : 0);
        bool scan = !(r & RES_DONE) || (j->seeking && j->target <= j->cumulative + count);
        if (scan) {
            if (j->scan_pos < j->chunk_start) j->scan_pos = j->chunk_start;
            while (j->scan_pos < end) {
                if (worker_stop(c)) return;
                const uint8_t *p;
                j->cpu_begin = now_ns(CLOCK_THREAD_CPUTIME_ID);
                size_t k = j->src.span(j->src.ctx, j->scan_pos, &p);
                if (worker_stop(c) || !k) return;
                if (k > end - j->scan_pos) k = (size_t)(end - j->scan_pos);
                spans++;
                while (k) {
                    size_t batch = k > SCAN_BLOCK ? SCAN_BLOCK : k;
                    scan_counts counts = scan_count(p, batch);
                    bool found = j->seeking && j->cumulative + j->scan_nl < j->target &&
                        counts.newlines >= j->target - j->cumulative - j->scan_nl;
                    if (found) {
                        const uint8_t *q = scan_find_nth_newline(p, batch,
                            j->target - j->cumulative - j->scan_nl - 1u);
                        j->answer = j->scan_pos + (uint64_t)(q - p) + 1u;
                    }
                    j->scan_nl += counts.newlines; j->scan_na += counts.nonascii;
                    j->scan_pos += batch; bytes += batch;
                    if (worker_stop(c)) return;
                    if (found) { j->seek_pending = true; goto publish_results; }
                    p += batch; k -= batch;
                    if (bytes >= 16u * LINEIDX_CHUNK || now_ns(CLOCK_THREAD_CPUTIME_ID) >= deadline)
                        goto publish_results;
                }
                if (spans >= 64u) goto publish_results;
            }
            count = j->scan_nl;
            j->res[i] = RES_DONE | (j->scan_na ? RES_NA : 0) | (uint32_t)count;
            j->scan_nl = j->scan_na = 0;
        }
        j->chunk_start = end; j->scan_pos = end;
        j->cumulative += count; j->cursor++; chunks++;
        if (chunks >= PUBLISH_CHUNKS || now_ns(CLOCK_THREAD_CPUTIME_ID) >= deadline) break;
    }
    if (j->seeking && j->cursor == j->n) {
        j->answer = j->src.len; j->seek_pending = true;
    }
publish_results:
    if (!publish_range(c, j->sealed, j->cursor)) goto yield;
    j->sealed = j->cursor;
    if (j->seek_pending) goto seek_result;
    if (j->cursor < j->n) goto yield;
    return;
seek_result:
    if (!publish(c, MSG_SEEK, &j->answer, sizeof j->answer)) goto yield;
    return;
yield:
    if (!worker_stop(c)) (void)work_continue(c);
}

/* ---- lifecycle ---- */
lineidx *lineidx_create_reserved(uint64_t len, size_t extra_chunks)
{
    if (len > LINEIDX_MAX_LEN) return NULL;
    uint64_t chunks = len / LINEIDX_CHUNK + (len % LINEIDX_CHUNK != 0);
    if (!chunks) chunks = 1;
    if (chunks > SIZE_MAX - extra_chunks || chunks + extra_chunks >= UINT32_MAX) return NULL;
    size_t cap = (size_t)chunks + extra_chunks;
    lineidx *x = calloc(1, sizeof *x);
    if (!x) return NULL;
    x->cap = cap; x->block_cap = cap / (BLOCK_MAX / 2u) + 5u;
    x->e = calloc(cap + 1u, sizeof *x->e);
    x->b = calloc(x->block_cap + 1u, sizeof *x->b);
    if (!x->e || !x->b) { free(x->e); free(x->b); free(x); return NULL; }
    for (size_t i = 1; i <= cap; i++) x->e[i].next = i < cap ? (uint32_t)i + 1u : 0;
    x->free_entry = 1; x->free_entries = cap;
    for (size_t i = 1; i <= x->block_cap; i++) x->b[i].left = i < x->block_cap ? (uint32_t)i + 1u : 0;
    x->free_block = 1; x->free_blocks = (uint32_t)x->block_cap;
    x->n = (size_t)chunks; x->len = len;
    uint64_t remaining = len;
    for (size_t i = 0; i < x->n;) {
        uint32_t id = take_block(x); block *v = &x->b[id];
        for (uint32_t k = 0; k < BLOCK_MAX && i < x->n; k++, i++) {
            uint32_t e = take_entry(x);
            uint32_t bytes = (uint32_t)(remaining > LINEIDX_CHUNK ? LINEIDX_CHUNK : remaining);
            x->e[e].len = bytes; x->e[e].fl = len == 0 ? FL_BUILT : 0;
            remaining -= bytes;
            if (v->tail) x->e[v->tail].next = e; else v->head = e;
            v->tail = e; v->count++;
        }
        recount(x, id); x->root = merge(x, x->root, id);
    }
    x->dirty = true; derive(x); return x;
}
lineidx *lineidx_create(uint64_t len) { return lineidx_create_reserved(len, 64); }
void lineidx_build_cancel(lineidx *x)
{
    if (!x) return;
    if (x->job) {
        lineidx_job *j = x->job;
        work_cancel(j->pool, j->h); x->gen++; retire_job(x);
    }
    x->async_ready = false;
}
static bool invalidate_backing(lineidx *x)
{
    if (!x->faulted && file_backing_faulted(x->backing)) {
        x->faulted = true;
        lineidx_build_cancel(x);
        x->seek_cached = false; x->query_active = false;
    }
    return x->faulted;
}
int lineidx_bind_snapshot(lineidx *x, const piece_snapshot *snapshot)
{
    if (!x || !snapshot || piece_snapshot_len(snapshot) != x->len ||
        x->backing_bound || x->job || x->retired ||
        (x->len && x->b[x->root].unbuilt != x->n)) return -1;
    x->backing = file_snapshot_backing(snapshot);
    x->backing_bound = true;
    file_backing_acquire(x->backing);
    return 0;
}
void lineidx_destroy(lineidx *x)
{
    if (!x) return;
    lineidx_build_cancel(x);
    while (x->retired) {
        reap(x);
        if (x->retired) nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    file_backing_release(x->backing);
    free(x->e); free(x->b); free(x);
}
uint64_t lineidx_len(const lineidx *x) { return x->len; }
uint64_t lineidx_cancel_cpu_ns(const lineidx *x)
{
    const lineidx_job *j = x->retired ? x->retired : x->job;
    return j && job_done(j) ? j->cancel_cpu_ns : x->last_cancel_cpu_ns;
}
size_t lineidx_chunk_count(const lineidx *x) { return x->n; }
size_t lineidx_built_prefix(lineidx *x) { if (invalidate_backing(x)) return 0; derive(x); return x->pfx_n; }
bool lineidx_complete(lineidx *x) { return !invalidate_backing(x) && x->b[x->root].unbuilt == 0; }
bool lineidx_any_nonascii(const lineidx *x) { return !file_backing_faulted(x->backing) && x->b[x->root].nonascii != 0; }
bool lineidx_chunk_nonascii(const lineidx *x, size_t ordinal)
{
    if (file_backing_faulted(x->backing) || ordinal >= x->n) return false;
    uint32_t root = x->root, k = (uint32_t)ordinal;
    while (root) {
        const block *v = &x->b[root]; uint32_t left = x->b[v->left].total;
        if (k < left) { root = v->left; continue; }
        k -= left;
        if (k < v->count) {
            uint32_t id = v->head;
            while (k--) id = x->e[id].next;
            return (x->e[id].fl & FL_NONASCII) != 0;
        }
        k -= v->count; root = v->right;
    }
    return false;
}
size_t lineidx_mem_bytes(const lineidx *x)
{
    size_t bytes = sizeof *x + (x->cap + 1u) * sizeof *x->e + (x->block_cap + 1u) * sizeof *x->b;
    const lineidx_job *j = x->job ? x->job : x->retired;
    while (j) {
        size_t scratch = sizeof *j + j->n * (sizeof *j->lengths + sizeof *j->res);
        if (scratch > SIZE_MAX - bytes) return SIZE_MAX;
        bytes += scratch;
        if (j->source_bytes > SIZE_MAX - bytes) return SIZE_MAX;
        bytes += j->source_bytes; j = j == x->job ? x->retired : j->next;
    }
    return bytes;
}
bool lineidx_building(lineidx *x)
{
    (void)invalidate_backing(x);
    reap(x);
    if (x->retired) return true;
    return x->job && (!job_done(x->job) || x->job->applied < x->job->available);
}
size_t lineidx_poll(lineidx *x)
{
    (void)invalidate_backing(x);
    lineidx_job *j = x->job;
    size_t got = 0;
    uint64_t deadline = now_ns(CLOCK_MONOTONIC) + UI_CPU_NS;
    uint64_t cpu_deadline = now_ns(CLOCK_THREAD_CPUTIME_ID) + UI_CPU_NS;
    if (j) {
        (void)work_mailbox_receive_bounded(j->pool, j->h, j->generation, receive_results, x, 4, deadline);
        while (j->applied < j->available && got < POLL_CHUNKS && now_ns(CLOCK_THREAD_CPUTIME_ID) < cpu_deadline) {
            if (invalidate_backing(x)) { j = NULL; break; }
            location at = {.block = j->apply_block, .id = j->apply_entry};
            entry *e = &x->e[at.id];
            if (!(e->fl & FL_BUILT)) {
                uint32_t r = j->res[j->applied];
                set_built(x, at, r & RES_COUNT, (r & RES_NA) != 0); got++;
            }
            j->applied++; j->apply_entry = e->next;
            if (!j->apply_entry && j->applied < j->n) {
                j->apply_block = successor(x, j->apply_block);
                j->apply_entry = x->b[j->apply_block].head;
            }
            /* Built entries also consume this slice's traversal budget. */
            if ((j->applied & (POLL_CHUNKS - 1u)) == 0 || now_ns(CLOCK_THREAD_CPUTIME_ID) >= cpu_deadline) break;
        }
        if (j && (j->applied == j->n || (j->seeking && x->async_ready && j->applied == j->available)) && job_done(j)) retire_job(x);
    }
    reap(x); return got;
}
static int start_job(lineidx *x, work_pool *pool, const lineidx_src *snap,
                       size_t source_bytes, bool seeking, uint64_t target, bool foreground)
{
    if (foreground && !pool->foreground_enabled) return -1;
    if (!snap || snap->len != x->len || !snap->span || invalidate_backing(x)) return -1;
    lineidx_build_cancel(x); reap(x);
    if (x->retired) return -1;
    x->last_cancel_cpu_ns = 0;
    derive(x);
    if (!seeking && lineidx_complete(x)) {
        if (snap->release) snap->release(snap->ctx);
        return 0;
    }
    lineidx_job *j = calloc(1, sizeof *j);
    if (!j) return -1;
    j->n = x->n; j->lengths = malloc(j->n * sizeof *j->lengths); j->res = malloc(j->n * sizeof *j->res);
    if (!j->lengths || !j->res) { free(j->lengths); free(j->res); free(j); return -1; }
    uint32_t leaf = edge_block(x, x->root, false); size_t i = 0;
    while (leaf) {
        for (uint32_t id = x->b[leaf].head; id; id = x->e[id].next) {
            const entry *e = &x->e[id];
            j->lengths[i] = (uint16_t)(e->len ? e->len - 1u : 0);
            j->res[i++] = (e->fl & FL_BUILT) ? RES_DONE | ((e->fl & FL_NONASCII) ? RES_NA : 0) | e->nl : 0;
        }
        leaf = successor(x, leaf);
    }
    j->src = *snap; j->source_bytes = source_bytes;
    j->pool = pool; j->generation = ++x->gen; j->seeking = seeking; j->target = target;
    j->backing = x->backing;
    j->seek_pending = seeking && target == 0;
    j->apply_block = edge_block(x, x->root, false); j->apply_entry = x->b[j->apply_block].head;
    j->h = work_submit(pool, (work_job){build_fn, j, j->generation, foreground ? WORK_FOREGROUND : WORK_BULK});
    if (!j->h.epoch) { j->src.release = NULL; job_free(j); return -1; }
    x->job = j;
    if (work_mailbox_bind(pool, j->h, j->generation, receive_results, x) != 0) {
        work_cancel(pool, j->h);
        while (!job_done(j)) nanosleep(&(struct timespec){0, 100000}, NULL);
        x->job = NULL; j->src.release = NULL; job_free(j); return -1;
    }
    return 0;
}
int lineidx_build_start_owned(lineidx *x, work_pool *pool, const lineidx_src *snap, size_t source_bytes)
{ return start_job(x, pool, snap, source_bytes, false, 0, false); }
int lineidx_build_start(lineidx *x, work_pool *pool, const lineidx_src *snap)
{ return lineidx_build_start_owned(x, pool, snap, snap->release ? SIZE_MAX : 0); }
int lineidx_build_start_foreground(lineidx *x, work_pool *pool, const lineidx_src *snap)
{ return start_job(x, pool, snap, snap->release ? SIZE_MAX : 0, false, 0, true); }
int lineidx_build_prioritize(lineidx *x)
{
    if (!x || !x->job) return -1;
    return work_prioritize(x->job->pool, x->job->h);
}
int lineidx_seek_start_owned(lineidx *x, work_pool *pool, const lineidx_src *snap, uint64_t line, size_t source_bytes)
{ return start_job(x, pool, snap, source_bytes, true, line, false); }
bool lineidx_seek_result(lineidx *x, lineidx_result *result)
{
    (void)lineidx_poll(x);
    if (invalidate_backing(x) || !x->async_ready) return false;
    if (result) *result = (lineidx_result){x->async_value, true};
    return true;
}

/* ---- bounded foreground edits and edited-range lookup ---- */
int lineidx_edit(lineidx *x, uint64_t off, uint64_t del, uint64_t ins_len)
{
    if (off > x->len || del > x->len - off || ins_len > LINEIDX_MAX_LEN - (x->len - del)) return -1;
    location first = locate_byte(x, off), last = del ? locate_byte(x, off + del - 1u) : first;
    uint64_t end = last.byte + x->e[last.id].len;
    uint64_t newlen = end - first.byte - del + ins_len;
    uint64_t pieces = newlen / LINEIDX_CHUNK + (newlen % LINEIDX_CHUNK != 0);
    uint32_t removed = last.ordinal - first.ordinal + 1u;
    size_t kept = x->n - removed;
    /* Admission is bounded too: a large replacement needs a rebuilt index on
     * the allocating path. Refusal precedes cancellation or model mutation. */
    if (pieces > x->cap - kept || pieces > LINEIDX_EDIT_MAX_CHUNKS) return -1;
    while (x->free_blocks < 3 && reclaim_one(x)) { }
    if (x->free_blocks < 3) return -1;
    lineidx_build_cancel(x);
    uint32_t a, b, removed_tree, c;
    split(x, x->root, first.ordinal, &a, &b);
    split(x, b, removed, &removed_tree, &c);
    x->garbage = merge(x, x->garbage, removed_tree);
    uint32_t replacement = 0;
    if (pieces || !kept) {
        replacement = take_block(x); block *v = &x->b[replacement];
        uint64_t remaining = newlen;
        uint32_t count = pieces ? (uint32_t)pieces : 1u;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t id = take_entry(x);
            uint32_t len = (uint32_t)(remaining > LINEIDX_CHUNK ? LINEIDX_CHUNK : remaining);
            x->e[id].len = len; x->e[id].fl = newlen ? FL_EDITED : FL_BUILT;
            remaining -= len;
            if (v->tail) x->e[v->tail].next = id; else v->head = id;
            v->tail = id; v->count++; x->foreground_work++;
        }
        recount(x, replacement);
    }
    x->root = concat(x, concat(x, a, replacement), c);
    x->n = x->b[x->root].total; x->len = x->len - del + ins_len; x->dirty = true;
    x->seek_pos = x->seek_nl = x->seek_na = x->seek_last = 0; x->seek_cached = false;
    x->query_active = false;
    x->refresh_id = 0;
    return 0;
}
size_t lineidx_refresh(lineidx *x, const lineidx_src *cur)
{
    if (invalidate_backing(x)) return 0;
    if (!x->b[x->root].edited || cur->len != x->len) return 0;
    location at = locate(x, first_flag(x, true));
    if (x->refresh_id != at.id) {
        x->refresh_id = at.id; x->refresh_pos = at.byte; x->refresh_nl = x->refresh_na = 0;
    }
    uint64_t end = at.byte + x->e[at.id].len;
    uint64_t deadline = now_ns(CLOCK_THREAD_CPUTIME_ID) + UI_CPU_NS;
    for (unsigned spans = 0; spans < SPAN_MAX && x->refresh_pos < end; spans++) {
        const uint8_t *p; size_t k = cur->span(cur->ctx, x->refresh_pos, &p);
        if (!k) return 0;
        if (k > end - x->refresh_pos) k = (size_t)(end - x->refresh_pos);
        scan_counts counts = scan_count(p, k);
        x->refresh_nl += counts.newlines; x->refresh_na += counts.nonascii; x->refresh_pos += k;
        x->scanned_bytes += k;
        if (now_ns(CLOCK_THREAD_CPUTIME_ID) >= deadline) break;
    }
    if (x->refresh_pos != end) return 0;
    if (invalidate_backing(x)) return 0;
    set_built(x, at, (uint32_t)x->refresh_nl, x->refresh_na != 0); x->refresh_id = 0; return 1;
}

/* ---- queries ---- */
static uint64_t density(const lineidx *x)
{
    if (x->pfx_lines) { uint64_t d = x->pfx_bytes / x->pfx_lines; return d ? d : 1; }
    return DEFAULT_BPL;
}
static lineidx_result line_count_impl(lineidx *x)
{
    derive(x);
    if (x->pfx_n == x->n) return (lineidx_result){x->pfx_lines + 1u, true};
    return (lineidx_result){x->pfx_lines + 1u + (x->len - x->pfx_bytes) / density(x), false};
}
static lineidx_result line_to_byte_impl(lineidx *x, const lineidx_src *cur, uint64_t line)
{
    derive(x);
    if (!line) return (lineidx_result){0, true};
    if (line <= x->pfx_lines) {
        location at = locate_line(x, line);
        uint64_t q = nth_newline(cur, at.byte, x->e[at.id].len, line - at.lines - 1u);
        return (lineidx_result){q == UINT64_MAX ? x->len : q + 1u, q != UINT64_MAX};
    }
    if (x->pfx_n == x->n) return (lineidx_result){x->len, true};
    uint64_t pb = x->pfx_bytes, d = density(x), want = line - x->pfx_lines;
    uint64_t est = want > (x->len - pb) / d ? x->len : pb + want * d;
    uint64_t lo = est - pb > LINEIDX_CHUNK ? est - LINEIDX_CHUNK : pb;
    uint64_t anchor = align_back(cur, lo, est);
    if (!anchor && x->pfx_lines) {
        location at = locate_line(x, x->pfx_lines);
        uint64_t q = nth_newline(cur, at.byte, x->e[at.id].len, x->pfx_lines - at.lines - 1u);
        if (q != UINT64_MAX) anchor = q + 1u;
    }
    return (lineidx_result){anchor, false};
}
static lineidx_result byte_to_line_impl(lineidx *x, const lineidx_src *cur, uint64_t off)
{
    derive(x); if (off > x->len) off = x->len;
    location at = locate_byte(x, off);
    if (at.ordinal < x->pfx_n) {
        uint64_t nl, na; scan_range(cur, at.byte, off - at.byte, &nl, &na);
        return (lineidx_result){at.lines + nl, true};
    }
    if (off <= x->pfx_bytes) return (lineidx_result){x->pfx_lines, true};
    return (lineidx_result){x->pfx_lines + (off - x->pfx_bytes) / density(x), false};
}
static lineidx_result seek_known(lineidx *x, const lineidx_src *cur, location at,
                                  uint64_t line, uint64_t len, uint64_t budget)
{
    uint64_t end = at.byte + len;
    if (!x->query_active || x->query_start != at.byte || x->query_end != end || x->query_line != line) {
        x->query_start = x->query_pos = at.byte; x->query_end = end;
        x->query_nl = at.lines; x->query_line = line; x->query_last = 0;
        x->query_active = true;
    }
    uint64_t deadline = now_ns(CLOCK_THREAD_CPUTIME_ID) + UI_CPU_NS;
    for (unsigned spans = 0; spans < SPAN_MAX && budget && x->query_pos < end; spans++) {
        uint64_t pos = x->query_pos, cum = x->query_nl;
        const uint8_t *p; size_t k = cur->span(cur->ctx, pos, &p);
        if (!k) break;
        if (k > end - pos) k = (size_t)(end - pos);
        if (k > budget) k = (size_t)budget;
        if (k > SCAN_BLOCK) k = SCAN_BLOCK;
        scan_counts counts = scan_count(p, k);
        x->scanned_bytes += k;
        if (cum < line && counts.newlines >= line - cum) {
            const uint8_t *q = scan_find_nth_newline(p, k, line - cum - 1u);
            x->seek_value = pos + (uint64_t)(q - p) + 1u;
            x->seek_line = line; x->seek_cached = true;
            x->query_active = false;
            return (lineidx_result){x->seek_value, true};
        }
        const uint8_t *last = memrchr(p, '\n', k);
        if (last) x->query_last = pos + (uint64_t)(last - p) + 1u;
        x->query_nl += counts.newlines; x->query_pos += k; budget -= k;
        if (now_ns(CLOCK_THREAD_CPUTIME_ID) >= deadline) break;
    }
    return (lineidx_result){x->query_last, false};
}
static lineidx_result seek_line_impl(lineidx *x, const lineidx_src *cur, uint64_t line, uint64_t budget)
{
    derive(x);
    if (!line) return (lineidx_result){0, true};
    if (x->seek_cached && x->seek_line == line) return (lineidx_result){x->seek_value, true};
    if (cur->len != x->len) return (lineidx_result){0, false};
    if (line <= x->pfx_lines) {
        location at = locate_line(x, line);
        return seek_known(x, cur, at, line, x->e[at.id].len, LINEIDX_CHUNK);
    }
    if (x->pfx_n == x->n) return (lineidx_result){x->len, true};
    if (x->seek_pos < x->pfx_bytes) { x->seek_pos = x->pfx_bytes; x->seek_nl = x->seek_na = 0; }
    if (budget > LINEIDX_CHUNK) budget = LINEIDX_CHUNK;
    if (line <= x->pfx_lines + x->seek_nl) {
        location at = locate(x, (uint32_t)x->pfx_n);
        return seek_known(x, cur, at, line, x->seek_pos - at.byte, budget);
    }
    uint64_t deadline = now_ns(CLOCK_THREAD_CPUTIME_ID) + UI_CPU_NS;
    for (unsigned spans = 0; spans < SPAN_MAX && budget && x->pfx_n < x->n; spans++) {
        location at = locate(x, (uint32_t)x->pfx_n);
        uint64_t end = at.byte + x->e[at.id].len;
        const uint8_t *p; size_t k = cur->span(cur->ctx, x->seek_pos, &p);
        if (!k) break;
        if (k > end - x->seek_pos) k = (size_t)(end - x->seek_pos);
        if (k > budget) k = (size_t)budget;
        if (k > SCAN_BLOCK) k = SCAN_BLOCK;
        scan_counts counts = scan_count(p, k);
        uint64_t cum = x->pfx_lines + x->seek_nl;
        bool found = cum < line && counts.newlines >= line - cum;
        uint64_t answer = 0;
        if (found) {
            const uint8_t *q = scan_find_nth_newline(p, k, line - cum - 1u);
            answer = x->seek_pos + (uint64_t)(q - p) + 1u;
        }
        const uint8_t *last = memrchr(p, '\n', k);
        if (last) x->seek_last = x->seek_pos + (uint64_t)(last - p) + 1u;
        x->seek_nl += counts.newlines; x->seek_na += counts.nonascii;
        x->seek_pos += k; budget -= k; x->scanned_bytes += k;
        if (x->seek_pos == end) {
            set_built(x, at, (uint32_t)x->seek_nl, x->seek_na != 0);
            x->seek_nl = x->seek_na = 0; derive(x);
            /* Refresh or worker adoption can have built following chunks.
             * Their counts are already in the derived prefix. */
            x->seek_pos = x->pfx_bytes;
        }
        if (found) {
            x->seek_cached = true; x->seek_line = line; x->seek_value = answer;
            return (lineidx_result){answer, true};
        }
        if (line <= x->pfx_lines) {
            location known = locate_line(x, line);
            return seek_known(x, cur, known, line, x->e[known.id].len, budget);
        }
        if (now_ns(CLOCK_THREAD_CPUTIME_ID) >= deadline) break;
    }
    if (x->pfx_n == x->n) return (lineidx_result){x->len, true};
    return (lineidx_result){x->seek_last, false};
}

/* A signal may arrive inside an indivisible source callback/scanner. Recheck
 * after computation as well as before it; no faulted answer escapes as exact. */
lineidx_result lineidx_line_count(lineidx *x)
{
    if (invalidate_backing(x)) return (lineidx_result){0, false};
    lineidx_result result = line_count_impl(x);
    return invalidate_backing(x) ? (lineidx_result){0, false} : result;
}
lineidx_result lineidx_line_to_byte(lineidx *x, const lineidx_src *cur, uint64_t line)
{
    if (invalidate_backing(x)) return (lineidx_result){0, false};
    lineidx_result result = line_to_byte_impl(x, cur, line);
    return invalidate_backing(x) ? (lineidx_result){0, false} : result;
}
lineidx_result lineidx_byte_to_line(lineidx *x, const lineidx_src *cur, uint64_t off)
{
    if (invalidate_backing(x)) return (lineidx_result){0, false};
    lineidx_result result = byte_to_line_impl(x, cur, off);
    return invalidate_backing(x) ? (lineidx_result){0, false} : result;
}
lineidx_result lineidx_seek_line(lineidx *x, const lineidx_src *cur, uint64_t line, uint64_t budget)
{
    if (invalidate_backing(x)) return (lineidx_result){0, false};
    lineidx_result result = seek_line_impl(x, cur, line, budget);
    return invalidate_backing(x) ? (lineidx_result){0, false} : result;
}
