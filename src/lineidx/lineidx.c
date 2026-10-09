/* lineidx.c -- sparse line index (P1.6). Design: docs/decisions/P1.6.md. */
#include "lineidx/lineidx.h"
#include "scan/scan.h"

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
    size_t source_bytes;
    work_pool *pool;
    work_handle h;
    size_t n;
    size_t applied;                       /* UI thread */
    uint64_t *starts;                     /* n + 1 */
    uint64_t *res;                        /* immutable once its range is mailed */
    uint32_t generation;
    size_t received;                     /* UI: newly adopted since last poll */
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

/* Real line start after the last '\n' in [lo, hi), or UINT64_MAX. */
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
    return last == UINT64_MAX ? UINT64_MAX : last + 1;
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
    return work_handle_finished(j->pool, j->h);
}

static void job_free(lineidx_job *j)
{
    (void)work_mailbox_bind(j->pool, j->h, j->generation, NULL, NULL);
    if (j->src.release) j->src.release(j->src.ctx);
    free(j->starts);
    free(j->res);
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
    (void)work_mailbox_bind(j->pool, j->h, j->generation, NULL, NULL);
    x->job = NULL;
    j->next = x->retired;
    x->retired = j;
}

typedef struct result_range { uint64_t first, end; } result_range;
_Static_assert(sizeof(result_range) <= WORK_MSG_DATA, "line index range fits mailbox");

static bool publish_range(work_ctx *c, size_t first, size_t end)
{
    work_msg msg = {.kind = LINEIDX_MSG_PROGRESS, .generation = c->generation};
    result_range range = {first, end};
    memcpy(msg.data, &range, sizeof range);
    while (!work_should_stop(c)) {
        if (work_publish(c, &msg)) return true;
        nanosleep(&(struct timespec){0, 50000}, NULL);
    }
    return false;
}

/* Only a validated mailbox callback may read sealed worker result ranges.
 * No independent atomic prefix/result channel exists. The worker never writes
 * a mailed entry again; work retains callback ownership until it returns. */
static void apply_results(const work_msg *msg, void *ud)
{
    lineidx *x = ud;
    lineidx_job *j = x->job;
    if (!j || msg->kind != LINEIDX_MSG_PROGRESS || msg->generation != j->generation ||
        msg->slot_ != j->h.slot || msg->epoch_ != j->h.epoch) return;
    result_range range;
    memcpy(&range, msg->data, sizeof range);
    if (range.first != j->applied || range.end < range.first || range.end > j->n) return;
    for (size_t i = (size_t)range.first; i < (size_t)range.end; i++) {
        entry *e = &x->e[i];
        if (e->fl & FL_BUILT) continue;
        uint64_t r = j->res[i];
        e->nl = (uint32_t)(r & UINT32_MAX);
        e->fl = FL_BUILT | ((r & RES_NA) ? FL_NONASCII : 0);
        j->received++;
        x->dirty = true;
    }
    j->applied = (size_t)range.end;
}

static void build_fn(work_ctx *c)
{
    lineidx_job *j = c->arg;
    size_t sealed = 0;
    for (size_t i = 0; i < j->n; i++) {
        if ((i & 15u) == 0 && work_should_stop(c)) return;
        if (!(j->res[i] & RES_DONE)) {
            uint64_t nl, na;
            scan_range(&j->src, j->starts[i], j->starts[i + 1] - j->starts[i], &nl, &na);
            j->res[i] = RES_DONE | (na ? RES_NA : 0) | nl;
        }
        if (((i + 1u) & 15u) == 0 || i + 1u == j->n) {
            if (!publish_range(c, sealed, i + 1u)) return;
            sealed = i + 1u;
        }
    }
}

/* ---- lifecycle ---- */

lineidx *lineidx_create_reserved(uint64_t len, size_t extra_chunks)
{
    if (len > LINEIDX_MAX_LEN) return NULL;
    uint64_t chunks = len / LINEIDX_CHUNK + (len % LINEIDX_CHUNK != 0);
    if (chunks == 0) chunks = 1;
    if (chunks > SIZE_MAX - extra_chunks || chunks + extra_chunks > SIZE_MAX / sizeof(entry)) return NULL;
    size_t n = (size_t)chunks;
    lineidx *x = calloc(1, sizeof *x);
    if (!x) return NULL;
    x->cap = n + extra_chunks;
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

lineidx *lineidx_create(uint64_t len) { return lineidx_create_reserved(len, 64); }

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
    size_t bytes = x->cap * sizeof(entry);
    if (sizeof *x > SIZE_MAX - bytes) return SIZE_MAX;
    bytes += sizeof *x;
    size_t summaries = x->sb_cap * sizeof(uint64_t);
    if (summaries > SIZE_MAX - bytes) return SIZE_MAX;
    bytes += summaries;
    const lineidx_job *j = x->job ? x->job : x->retired;
    while (j) {
        size_t arrays = (j->n + 1) * sizeof(uint64_t) + j->n * sizeof(uint64_t);
        if (arrays > SIZE_MAX - sizeof *j) return SIZE_MAX;
        size_t scratch = sizeof *j + arrays;
        if (scratch > SIZE_MAX - bytes) return SIZE_MAX;
        bytes += scratch;
        if (j->source_bytes > SIZE_MAX - bytes) return SIZE_MAX;
        bytes += j->source_bytes;
        j = j == x->job ? x->retired : j->next;
    }
    return bytes;
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
        (void)work_mailbox_receive(j->pool, j->h, j->generation, apply_results, x);
        got = j->received;
        j->received = 0;
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

int lineidx_build_start_owned(lineidx *x, work_pool *pool, const lineidx_src *snap,
                             size_t source_bytes)
{
    if (snap->len != x->len || !snap->span) return -1;
    lineidx_build_cancel(x);
    if (x->retired) return -1;                    /* one scratch/source lease at a time */
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
    if (!j->starts || !j->res) { free(j->starts); free(j->res); free(j); return -1; }
    for (size_t i = 0; i < x->n; i++) {
        j->starts[i] = x->e[i].start;
        uint64_t r = 0;
        if (x->e[i].fl & FL_BUILT)
            r = RES_DONE | ((x->e[i].fl & FL_NONASCII) ? RES_NA : 0) | x->e[i].nl;
        j->res[i] = r;
    }
    j->starts[x->n] = x->len;
    j->src = *snap;
    j->source_bytes = source_bytes;
    j->pool = pool;
    j->generation = ++x->gen;
    work_job wj = { build_fn, j, j->generation, WORK_BULK };
    j->h = work_submit(pool, wj);
    if (j->h.epoch == 0) {
        j->src.release = NULL;                     /* caller keeps ownership */
        job_free(j);
        return -1;
    }
    x->job = j;
    if (work_mailbox_bind(pool, j->h, j->generation, apply_results, x) != 0) {
        /* Submit/bind are serialized UI calls, so a successful submission's
         * identity cannot change here. Preserve refusal ownership if misused. */
        work_cancel(pool, j->h);
        while (!job_done(j)) nanosleep(&(struct timespec){0, 100000}, NULL);
        x->job = NULL;
        j->src.release = NULL;
        job_free(j);
        return -1;
    }
    return 0;
}

int lineidx_build_start(lineidx *x, work_pool *pool, const lineidx_src *snap)
{
    return lineidx_build_start_owned(x, pool, snap, snap->release ? SIZE_MAX : 0);
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
    if (ins_len > LINEIDX_MAX_LEN - (x->len - del)) return -1;
    size_t c0 = find_chunk(x, off);
    size_t c1 = del ? find_chunk(x, off + del - 1) : c0;
    uint64_t s0 = x->e[c0].start;
    uint64_t e1 = (c1 + 1 < x->n ? x->e[c1 + 1].start : x->len);
    uint64_t newlen = (e1 - s0) - del + ins_len;
    uint64_t pieces = newlen / LINEIDX_CHUNK + (newlen % LINEIDX_CHUNK != 0);
    size_t kept = x->n - (c1 - c0 + 1);
    if (pieces > x->cap - kept) return -1;
    lineidx_build_cancel(x);                       /* preflight complete, now mutate */
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
        if (pieces > 1) {                          /* preflight guarantees storage */
            size_t k = (size_t)pieces - 1;
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
    x->dirty = true;
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
        x->n_edited--;
        break;                                    /* at most 64 KiB of content */
    }
    if (!done) x->n_edited = 0;
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
    uint64_t lo = est - pb > LINEIDX_CHUNK ? est - LINEIDX_CHUNK : pb;
    uint64_t aligned = align_back(cur, lo, est);
    if (aligned == UINT64_MAX) {
        /* Chunk starts need not be line starts. Recover the last proven line
         * anchor in the built prefix with at most one bounded chunk scan. */
        aligned = 0;
        for (size_t i = x->pfx_n; i > 0; i--) {
            if (!x->e[i - 1].nl) continue;
            uint64_t q = nth_newline(cur, x->e[i - 1].start, chunk_len(x, i - 1), x->e[i - 1].nl - 1u);
            if (q != UINT64_MAX) aligned = q + 1;
            break;
        }
    }
    return (lineidx_result){ aligned, false };
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
