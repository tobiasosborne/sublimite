/* lineidx.c -- sparse line index (P1.6). Design: docs/decisions/P1.6.md. */
#include "lineidx/lineidx.h"
#include "scan/scan.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FL_BUILT    1u
#define FL_NONASCII 2u
#define FL_EDITED   4u
#define SB_SHIFT    6u                    /* one summary per 64 chunks (4 MiB) */
#define SB_N        (1u << SB_SHIFT)
#define RES_DONE    (1ull << 63)
#define RES_NA      (1ull << 32)
#define DEFAULT_BPL 40u                   /* bytes per line before any evidence */

typedef struct entry { uint64_t start; uint32_t nl; uint32_t fl; } entry;
_Static_assert(sizeof(entry) == 16, "16 B per chunk");

typedef struct lineidx_job {
    struct lineidx_job *next;             /* retired list */
    lineidx_src src;
    work_pool *pool;
    work_handle h;
    size_t n;
    size_t applied;                       /* UI thread */
    uint64_t *starts;                     /* n + 1 */
    _Atomic uint64_t *res;                /* n: RES_DONE | RES_NA | nl */
    _Atomic uint64_t done_n;              /* prefix of chunks whose res is final */
    _Atomic uint32_t fn_done;
} lineidx_job;

struct lineidx {
    entry *e;
    size_t n, cap;
    uint64_t len;
    size_t n_edited;
    bool dirty;                           /* derived state stale */
    size_t pfx_n;                         /* leading built chunks */
    uint64_t pfx_lines;                   /* newlines in them */
    uint64_t *sb;                         /* sb[k] = newlines in chunks [0, 64k), k*64 <= pfx_n */
    size_t sb_cap;
    lineidx_job *job, *retired;
    uint32_t gen;
};

size_t lineidx_entry_bytes(void) { return sizeof(entry); }

/* ---- content helpers ---- */

static void scan_range(const lineidx_src *s, uint64_t start, uint64_t len, uint64_t *nl, uint64_t *na)
{
    uint64_t pos = start, end = start + len, a = 0, b = 0;
    while (pos < end) {
        const uint8_t *p;
        size_t k = s->span(s->ctx, pos, &p);
        if (k == 0) break;
        if (k > end - pos) k = (size_t)(end - pos);
        scan_counts c = scan_count(p, k);
        a += c.newlines; b += c.nonascii;
        pos += k;
    }
    *nl = a; *na = b;
}

/* Absolute offset of the k-th (0-based) newline in [start, start+len), or UINT64_MAX. */
static uint64_t nth_newline(const lineidx_src *s, uint64_t start, uint64_t len, uint64_t k)
{
    uint64_t pos = start, end = start + len;
    while (pos < end) {
        const uint8_t *p;
        size_t m = s->span(s->ctx, pos, &p);
        if (m == 0) break;
        if (m > end - pos) m = (size_t)(end - pos);
        uint64_t c = scan_count(p, m).newlines;
        if (k < c) {
            const uint8_t *q = scan_find_nth_newline(p, m, k);
            return q ? pos + (uint64_t)(q - p) : UINT64_MAX;
        }
        k -= c;
        pos += m;
    }
    return UINT64_MAX;
}

/* Start of the line containing the last '\n' in [lo, hi), else hi. */
static uint64_t align_back(const lineidx_src *s, uint64_t lo, uint64_t hi)
{
    uint64_t pos = lo, last = UINT64_MAX;
    while (pos < hi) {
        const uint8_t *p;
        size_t m = s->span(s->ctx, pos, &p);
        if (m == 0) break;
        if (m > hi - pos) m = (size_t)(hi - pos);
        const uint8_t *q = memrchr(p, '\n', m);
        if (q) last = pos + (uint64_t)(q - p);
        pos += m;
    }
    return last == UINT64_MAX ? hi : last + 1;
}

static uint64_t chunk_len(const lineidx *x, size_t i)
{
    return (i + 1 < x->n ? x->e[i + 1].start : x->len) - x->e[i].start;
}

/* ---- derived state ---- */

static void derive(lineidx *x)
{
    if (!x->dirty) return;
    size_t i = 0;
    uint64_t cum = 0;
    while (i < x->n && (x->e[i].fl & FL_BUILT)) {
        if ((i & (SB_N - 1)) == 0) x->sb[i >> SB_SHIFT] = cum;
        cum += x->e[i].nl;
        i++;
    }
    x->pfx_n = i;
    x->pfx_lines = cum;
    x->dirty = false;
}

static uint64_t pfx_bytes(const lineidx *x) { return x->pfx_n < x->n ? x->e[x->pfx_n].start : x->len; }

/* newlines in chunks [0, c), c <= pfx_n */
static uint64_t cum_lines(const lineidx *x, size_t c)
{
    size_t k = c >> SB_SHIFT;
    uint64_t s = x->sb[k];
    for (size_t i = k << SB_SHIFT; i < c; i++) s += x->e[i].nl;
    return s;
}

/* ---- jobs ---- */

static bool job_done(const lineidx_job *j)
{
    if (atomic_load_explicit(&((lineidx_job *)j)->fn_done, memory_order_acquire)) return true;
    const work_pool *p = j->pool;
    if (p->efd < 0) return true;                        /* pool shut down: workers joined */
    const work_slot *s = &p->slots[j->h.slot];
    if (atomic_load_explicit(&s->busy, memory_order_acquire) == 0) return true;
    return atomic_load_explicit(&s->epoch, memory_order_acquire) >= j->h.epoch + 2u;
}

static void job_free(lineidx_job *j)
{
    if (j->src.release) j->src.release(j->src.ctx);
    free(j->starts);
    free((void *)j->res);
    free(j);
}

static void reap(lineidx *x)
{
    lineidx_job **pp = &x->retired;
    while (*pp) {
        lineidx_job *j = *pp;
        if (job_done(j)) { *pp = j->next; job_free(j); }
        else pp = &j->next;
    }
}

static void retire_job(lineidx *x)
{
    lineidx_job *j = x->job;
    if (!j) return;
    x->job = NULL;
    j->next = x->retired;
    x->retired = j;
}

static void build_fn(work_ctx *c)
{
    lineidx_job *j = c->arg;
    size_t i = 0;
    for (; i < j->n; i++) {
        if ((i & 15u) == 0) {
            atomic_store_explicit(&j->done_n, i, memory_order_release);
            if (work_should_stop(c)) goto out;
            if ((i & 63u) == 0 && i > 0) {
                work_msg m = {0};
                m.kind = LINEIDX_MSG_PROGRESS;
                m.generation = c->generation;
                (void)work_publish(c, &m);
            }
        }
        if (atomic_load_explicit(&j->res[i], memory_order_relaxed) & RES_DONE) continue;
        uint64_t nl, na;
        scan_range(&j->src, j->starts[i], j->starts[i + 1] - j->starts[i], &nl, &na);
        atomic_store_explicit(&j->res[i], RES_DONE | (na ? RES_NA : 0) | nl, memory_order_relaxed);
    }
    atomic_store_explicit(&j->done_n, j->n, memory_order_release);
    {
        work_msg m = {0};
        m.kind = LINEIDX_MSG_PROGRESS;
        m.generation = c->generation;
        (void)work_publish(c, &m);
    }
out:
    atomic_store_explicit(&j->fn_done, 1, memory_order_release);
}

/* ---- lifecycle ---- */

lineidx *lineidx_create(uint64_t len)
{
    size_t n = (size_t)((len + LINEIDX_CHUNK - 1) / LINEIDX_CHUNK);
    if (n == 0) n = 1;
    lineidx *x = calloc(1, sizeof *x);
    if (!x) return NULL;
    x->cap = 2 * n + 64;
    x->e = malloc(x->cap * sizeof(entry));
    x->sb_cap = (x->cap >> SB_SHIFT) + 2;
    x->sb = malloc(x->sb_cap * sizeof(uint64_t));
    if (!x->e || !x->sb) { free(x->e); free(x->sb); free(x); return NULL; }
    x->n = n;
    x->len = len;
    for (size_t i = 0; i < n; i++) {
        x->e[i].start = (uint64_t)i * LINEIDX_CHUNK;
        x->e[i].nl = 0;
        x->e[i].fl = len == 0 ? FL_BUILT : 0;
    }
    x->dirty = true;
    derive(x);
    return x;
}

void lineidx_destroy(lineidx *x)
{
    if (!x) return;
    lineidx_build_cancel(x);
    while (x->retired) {
        reap(x);
        if (x->retired) nanosleep(&(struct timespec){0, 100000}, NULL);
    }
    free(x->e);
    free(x->sb);
    free(x);
}

uint64_t lineidx_len(const lineidx *x) { return x->len; }
size_t lineidx_chunk_count(const lineidx *x) { return x->n; }
size_t lineidx_built_prefix(lineidx *x) { derive(x); return x->pfx_n; }
bool lineidx_complete(lineidx *x) { derive(x); return x->pfx_n == x->n; }
bool lineidx_chunk_nonascii(const lineidx *x, size_t i) { return i < x->n && (x->e[i].fl & FL_NONASCII); }
bool lineidx_any_nonascii(const lineidx *x)
{
    for (size_t i = 0; i < x->n; i++) if (x->e[i].fl & FL_NONASCII) return true;
    return false;
}
size_t lineidx_mem_bytes(const lineidx *x)
{
    return x->n * sizeof(entry) + ((x->n >> SB_SHIFT) + 1) * sizeof(uint64_t);
}

bool lineidx_building(lineidx *x)
{
    reap(x);
    if (x->retired) return true;
    return x->job && !job_done(x->job);
}

size_t lineidx_poll(lineidx *x)
{
    lineidx_job *j = x->job;
    size_t got = 0;
    if (j) {
        size_t d = (size_t)atomic_load_explicit(&j->done_n, memory_order_acquire);
        for (size_t i = j->applied; i < d && i < x->n; i++) {
            entry *e = &x->e[i];
            if (e->fl & FL_BUILT) continue;
            uint64_t r = atomic_load_explicit(&j->res[i], memory_order_relaxed);
            e->nl = (uint32_t)(r & 0xFFFFFFFFu);
            e->fl = FL_BUILT | ((r & RES_NA) ? FL_NONASCII : 0);
            got++;
        }
        if (d > j->applied) j->applied = d;
        if (got) x->dirty = true;
        if (j->applied >= j->n) retire_job(x);
    }
    reap(x);
    return got;
}

void lineidx_build_cancel(lineidx *x)
{
    if (!x) return;
    lineidx_poll(x);
    if (x->job) {
        work_cancel(x->job->pool, x->job->h);
        retire_job(x);
    }
    reap(x);
}

int lineidx_build_start(lineidx *x, work_pool *pool, const lineidx_src *snap)
{
    lineidx_build_cancel(x);
    derive(x);
    if (x->pfx_n == x->n) {                       /* nothing left to do */
        if (snap->release) snap->release(snap->ctx);
        return 0;
    }
    lineidx_job *j = calloc(1, sizeof *j);
    if (!j) return -1;
    j->n = x->n;
    j->starts = malloc((x->n + 1) * sizeof(uint64_t));
    j->res = malloc(x->n * sizeof(uint64_t));
    if (!j->starts || !j->res) { free(j->starts); free((void *)j->res); free(j); return -1; }
    for (size_t i = 0; i < x->n; i++) {
        j->starts[i] = x->e[i].start;
        uint64_t r = 0;
        if (x->e[i].fl & FL_BUILT)
            r = RES_DONE | ((x->e[i].fl & FL_NONASCII) ? RES_NA : 0) | x->e[i].nl;
        atomic_init(&j->res[i], r);
    }
    j->starts[x->n] = x->len;
    atomic_init(&j->done_n, 0);
    atomic_init(&j->fn_done, 0);
    j->src = *snap;
    j->pool = pool;
    work_job wj = { build_fn, j, ++x->gen, WORK_BULK };
    j->h = work_submit(pool, wj);
    if (j->h.epoch == 0) {
        j->src.release = NULL;                     /* caller keeps ownership */
        job_free(j);
        return -1;
    }
    x->job = j;
    return 0;
}

/* ---- edits ---- */

static size_t find_chunk(const lineidx *x, uint64_t off)   /* last c with start <= off */
{
    size_t lo = 0, hi = x->n;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (x->e[mid].start <= off) lo = mid; else hi = mid;
    }
    return lo;
}

int lineidx_edit(lineidx *x, uint64_t off, uint64_t del, uint64_t ins_len)
{
    if (off > x->len || del > x->len - off) return -1;
    lineidx_build_cancel(x);                       /* keeps finished work, then stops the job */
    size_t c0 = find_chunk(x, off);
    size_t c1 = del ? find_chunk(x, off + del - 1) : c0;
    uint64_t s0 = x->e[c0].start;
    uint64_t e1 = (c1 + 1 < x->n ? x->e[c1 + 1].start : x->len);
    uint64_t newlen = (e1 - s0) - del + ins_len;
    for (size_t i = c0; i <= c1; i++) if (x->e[i].fl & FL_EDITED) x->n_edited--;
    /* drop c0+1..c1 */
    if (c1 > c0) {
        memmove(&x->e[c0 + 1], &x->e[c1 + 1], (x->n - c1 - 1) * sizeof(entry));
        x->n -= c1 - c0;
    }
    /* shift later starts */
    for (size_t i = c0 + 1; i < x->n; i++) x->e[i].start = x->e[i].start - del + ins_len;
    x->len = x->len - del + ins_len;
    entry *e = &x->e[c0];
    e->start = s0;
    e->nl = 0;
    if (newlen == 0) {
        if (x->n > 1) {
            memmove(&x->e[c0], &x->e[c0 + 1], (x->n - c0 - 1) * sizeof(entry));
            x->n--;
        } else {
            e->fl = FL_BUILT;                        /* empty content: exactly zero lines breaks */
        }
    } else {
        e->fl = FL_EDITED;
        x->n_edited++;
        if (newlen > 2ull * LINEIDX_CHUNK) {          /* split into 64 KiB pieces if the table has room */
            size_t k = (size_t)((newlen - 1) / LINEIDX_CHUNK);   /* extra entries */
            if (x->n + k <= x->cap) {
                memmove(&x->e[c0 + 1 + k], &x->e[c0 + 1], (x->n - c0 - 1) * sizeof(entry));
                for (size_t i = 1; i <= k; i++) {
                    x->e[c0 + i].start = s0 + (uint64_t)i * LINEIDX_CHUNK;
                    x->e[c0 + i].nl = 0;
                    x->e[c0 + i].fl = FL_EDITED;
                }
                x->n += k;
                x->n_edited += k;
            }
        }
    }
    x->dirty = true;
    if (x->n >> SB_SHIFT >= x->sb_cap) return -1;     /* cannot happen: n <= cap */
    return 0;
}

size_t lineidx_refresh(lineidx *x, const lineidx_src *cur)
{
    size_t done = 0;
    if (x->n_edited == 0 || cur->len != x->len) return 0;
    for (size_t i = 0; i < x->n; i++) {
        entry *e = &x->e[i];
        if (!(e->fl & FL_EDITED)) continue;
        uint64_t nl, na;
        scan_range(cur, e->start, chunk_len(x, i), &nl, &na);
        e->nl = (uint32_t)nl;
        e->fl = FL_BUILT | (na ? FL_NONASCII : 0);
        done++;
    }
    x->n_edited = 0;
    x->dirty = true;
    return done;
}

/* ---- queries ---- */

static uint64_t density(const lineidx *x)       /* bytes per line, >= 1 */
{
    uint64_t pb = pfx_bytes(x);
    if (x->pfx_n > 0 && x->pfx_lines > 0) {
        uint64_t d = pb / x->pfx_lines;
        return d ? d : 1;
    }
    return DEFAULT_BPL;
}

lineidx_result lineidx_line_count(lineidx *x)
{
    derive(x);
    if (x->pfx_n == x->n) return (lineidx_result){ x->pfx_lines + 1, true };
    uint64_t pb = pfx_bytes(x);
    return (lineidx_result){ x->pfx_lines + 1 + (x->len - pb) / density(x), false };
}

static lineidx_result l2b_derived(lineidx *x, const lineidx_src *cur, uint64_t line)
{
    if (line == 0) return (lineidx_result){ 0, true };
    if (line <= x->pfx_lines) {
        size_t lo = 0, hi = ((x->pfx_n - 1) >> SB_SHIFT) + 1;     /* largest k with sb[k] < line */
        while (hi - lo > 1) {
            size_t mid = lo + (hi - lo) / 2;
            if (x->sb[mid] < line) lo = mid; else hi = mid;
        }
        size_t c = lo << SB_SHIFT;
        uint64_t cum = x->sb[lo];
        while (c < x->pfx_n && cum + x->e[c].nl < line) { cum += x->e[c].nl; c++; }
        uint64_t q = nth_newline(cur, x->e[c].start, chunk_len(x, c), line - cum - 1);
        return (lineidx_result){ q == UINT64_MAX ? x->len : q + 1, q != UINT64_MAX };
    }
    if (x->pfx_n == x->n) return (lineidx_result){ x->len, true };   /* past the last line */
    uint64_t pb = pfx_bytes(x);
    uint64_t d = density(x);
    uint64_t want = line - x->pfx_lines;
    uint64_t est = (want > (x->len - pb) / d) ? x->len : pb + want * d;
    if (est >= x->len) return (lineidx_result){ x->len, false };
    uint64_t lo = est > pb + LINEIDX_CHUNK ? est - LINEIDX_CHUNK : pb;
    return (lineidx_result){ align_back(cur, lo, est), false };
}

lineidx_result lineidx_line_to_byte(lineidx *x, const lineidx_src *cur, uint64_t line)
{
    derive(x);
    return l2b_derived(x, cur, line);
}

lineidx_result lineidx_byte_to_line(lineidx *x, const lineidx_src *cur, uint64_t off)
{
    derive(x);
    if (off > x->len) off = x->len;
    size_t c = find_chunk(x, off);
    if (c < x->pfx_n) {
        uint64_t nl, na;
        scan_range(cur, x->e[c].start, off - x->e[c].start, &nl, &na);
        return (lineidx_result){ cum_lines(x, c) + nl, true };
    }
    uint64_t pb = pfx_bytes(x);
    if (off <= pb) return (lineidx_result){ x->pfx_lines, true };
    return (lineidx_result){ x->pfx_lines + (off - pb) / density(x), false };
}

lineidx_result lineidx_seek_line(lineidx *x, const lineidx_src *cur, uint64_t line, uint64_t budget)
{
    derive(x);
    if (line > x->pfx_lines && x->pfx_n < x->n && cur->len == x->len) {
        size_t c = x->pfx_n;
        uint64_t cum = x->pfx_lines, spent = 0;
        while (c < x->n && cum < line && spent < budget) {
            entry *e = &x->e[c];
            if (!(e->fl & FL_BUILT)) {
                uint64_t len = chunk_len(x, c), nl, na;
                scan_range(cur, e->start, len, &nl, &na);
                if (e->fl & FL_EDITED) x->n_edited--;
                e->nl = (uint32_t)nl;
                e->fl = FL_BUILT | (na ? FL_NONASCII : 0);
                spent += len;
            }
            cum += e->nl;
            c++;
        }
        x->dirty = true;
        derive(x);
    }
    return l2b_derived(x, cur, line);
}
