/* indent test support, shared by tests/indent_test.c and fuzz/indent_fuzz.c.
 *
 * gtext: an indent_text whose spans are chunks of the text, each alone on
 *   its own read-write pages between PROT_NONE guard pages, alternately
 *   flush against the guard after it and the guard before it. gtext_allow
 *   turns every chunk outside the windows a query may read PROT_NONE, so a
 *   read outside them faults in every build.
 * model_*: the header contract restated over a flat logical byte array, by
 *   whole-line reasoning with no bound, no spans and no shared code with
 *   src/indent/indent.c. */
#ifndef TESTS_INDENT_MODEL_H
#define TESTS_INDENT_MODEL_H

#include "indent/indent.h"
#include "os/os.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef enum gt_access { GT_RW, GT_NONE } gt_access;
typedef struct gt_chunk {
    uint64_t off;
    size_t len, rw_len;
    uint8_t *rw, *data;
    gt_access access;
} gt_chunk;

typedef struct gtext {
    indent_text text;
    gt_chunk *chunk;
    size_t n, page, mapped;
    uint8_t *map;
    size_t ci;      /* iterator: next chunk */
    uint64_t pos;   /* iterator: next byte */
    unsigned bad;   /* contract violations seen by the adapter */
} gtext;

static void gt_seek(void *it, uint64_t off)
{
    gtext *g = it;
    if (off > g->text.length) { g->bad++; off = g->text.length; }
    size_t lo = 0, hi = g->n; /* first chunk ending after off */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g->chunk[mid].off + g->chunk[mid].len <= off) lo = mid + 1;
        else hi = mid;
    }
    g->ci = lo;
    g->pos = off;
}

static size_t gt_next(void *it, const uint8_t **p)
{
    gtext *g = it;
    if (g->ci == g->n) return 0;
    const gt_chunk *c = &g->chunk[g->ci++];
    size_t skip = (size_t)(g->pos - c->off);
    *p = c->data + skip;
    g->pos = c->off + c->len;
    return c->len - skip;
}

static int gt_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* Chunks of bytes[0, len) split at every cut in (0, len); cuts is sorted in
 * place. Returns false on a mapping failure. */
static bool gtext_init(gtext *g, const uint8_t *bytes, size_t len, uint64_t *cuts, size_t ncuts)
{
    memset(g, 0, sizeof *g);
    g->page = os_page_size();
    if (ncuts) qsort(cuts, ncuts, sizeof *cuts, gt_cmp);
    g->chunk = calloc(ncuts + 1, sizeof *g->chunk);
    if (!g->chunk) return false;
    uint64_t at = 0;
    for (size_t i = 0; i <= ncuts; i++) {
        uint64_t end = i < ncuts ? cuts[i] : len;
        if (end <= at || end > len) continue;
        g->chunk[g->n++] = (gt_chunk){.off = at, .len = (size_t)(end - at)};
        at = end;
    }
    size_t pages = g->n + 1;
    for (size_t i = 0; i < g->n; i++) {
        g->chunk[i].rw_len = (g->chunk[i].len + g->page - 1) / g->page * g->page;
        pages += g->chunk[i].rw_len / g->page;
    }
    g->mapped = pages * g->page;
    void *m = mmap(NULL, g->mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { free(g->chunk); return false; }
    g->map = m;
    uint8_t *cur = g->map + g->page;
    for (size_t i = 0; i < g->n; i++) {
        gt_chunk *c = &g->chunk[i];
        c->rw = cur;
        c->data = i % 2 == 0 ? c->rw + c->rw_len - c->len : c->rw;
        if (mprotect(c->rw, c->rw_len, PROT_READ | PROT_WRITE) != 0) {
            (void)munmap(g->map, g->mapped);
            free(g->chunk);
            return false;
        }
        memcpy(c->data, bytes + c->off, c->len);
        cur += c->rw_len + g->page;
    }
    g->text = (indent_text){gt_seek, gt_next, g, len};
    return true;
}

static void gtext_fini(gtext *g)
{
    (void)munmap(g->map, g->mapped);
    free(g->chunk);
}

/* Leave readable exactly the chunks inside one of the k windows [lo, hi).
 * Every window edge must be a chunk boundary; returns false otherwise or on
 * an mprotect failure. */
static bool gtext_allow(gtext *g, const indent_range *w, size_t k)
{
    for (size_t i = 0; i < g->n; i++) {
        const gt_chunk *c = &g->chunk[i];
        uint64_t a = c->off, b = c->off + c->len;
        bool inside = false;
        for (size_t j = 0; j < k; j++) {
            if (a >= w[j].lo && b <= w[j].hi) inside = true;
            else if (a < w[j].hi && b > w[j].lo) return false; /* straddles an edge */
        }
        gt_access want = inside ? GT_RW : GT_NONE;
        if (want == g->chunk[i].access) continue;
        if (mprotect(c->rw, c->rw_len, inside ? PROT_READ | PROT_WRITE : PROT_NONE) != 0) return false;
        g->chunk[i].access = want;
    }
    return true;
}

/* ---- reference models ----------------------------------------------------- */

#define MODEL_B ((uint64_t)INDENT_BLANK_BYTES)

static bool model_ws(uint8_t c) { return c == ' ' || c == '\t'; }

/* Start of the line holding c, and the offset of its LF (n if none). */
static uint64_t model_line_start(const uint8_t *L, uint64_t c)
{
    while (c > 0 && L[c - 1] != '\n') c--;
    return c;
}
static uint64_t model_line_end(const uint8_t *L, uint64_t n, uint64_t c)
{
    while (c < n && L[c] != '\n') c++;
    return c;
}

/* -- v1 (pin 6fec2e8), over the logical text L with the selection removed and
 *    the caret at c. v1 answered only when the caret was not between CR and LF
 *    and both line boundaries lay within its 8192-byte windows; the v1 model
 *    says whether it answered and, if so, what. */
#define MODEL_V1_T 8192u /* v1 INDENT_TYPING_BYTES, only to know when v1 answered */

static bool model_v1_answered(const uint8_t *L, uint64_t n, uint64_t c)
{
    if (c > 0 && c < n && L[c - 1] == '\r' && L[c] == '\n') return false;
    uint64_t s = model_line_start(L, c), e = model_line_end(L, n, c);
    if (s > 0 ? s - 1 + MODEL_V1_T < c : c > MODEL_V1_T) return false;
    if (e < n ? e - c >= MODEL_V1_T : n - c > MODEL_V1_T) return false;
    return true;
}

/* v1 Enter's leading bytes: the SP/TAB run from the line start up to c. */
static uint64_t model_v1_lead(const uint8_t *L, uint64_t c)
{
    uint64_t s = model_line_start(L, c), i = s;
    while (i < c && model_ws(L[i])) i++;
    return i - s;
}

/* Visual-column statement (v1 fuzz oracle): the stop before the cursor is
 * the last position whose column does not exceed the previous multiple of
 * width below the cursor's column. */
static uint64_t model_dedent(const uint8_t *L, uint64_t s, uint64_t c, unsigned width)
{
    uint64_t column = 0;
    for (uint64_t i = s; i < c; i++) column = L[i] == '\t' ? column + width - column % width : column + 1;
    uint64_t target = column ? (column - 1) / width * width : 0, keep = s, visual = 0;
    for (uint64_t i = s; i < c; i++) {
        visual = L[i] == '\t' ? visual + width - visual % width : visual + 1;
        if (visual <= target) keep = i + 1;
    }
    return keep;
}

/* v1 '}' over the whole line, terminator excluded. */
static uint64_t model_v1_brace(const uint8_t *L, uint64_t n, uint64_t c, unsigned width)
{
    uint64_t s = model_line_start(L, c), e = model_line_end(L, n, c), body = e;
    if (e < n && e > s && L[e - 1] == '\r') body--;
    for (uint64_t i = s; i < body; i++)
        if (!model_ws(L[i])) return c;
    return model_dedent(L, s, c, width);
}

/* -- v2, over the text p with the selection [lo, hi) and the caller's line
 *    start ls (header contract). */
static indent_range model_enter(const uint8_t *p, uint64_t ls, uint64_t lo)
{
    uint64_t k = 0;
    while (ls + k < lo && model_ws(p[ls + k])) k++;
    return (indent_range){ls, k > MODEL_B ? ls : ls + k};
}

static uint64_t model_brace(const uint8_t *p, uint64_t n, uint64_t ls, uint64_t lo, uint64_t hi, unsigned width)
{
    if (lo - ls > MODEL_B) return lo;
    for (uint64_t i = ls; i < lo; i++)
        if (!model_ws(p[i])) return lo;
    uint64_t q = hi;
    while (q < n && model_ws(p[q])) q++;
    if (q - hi > MODEL_B) return lo;
    bool edge = q == n || p[q] == '\n' || (p[q] == '\r' && q + 1 < n && p[q + 1] == '\n');
    return edge ? model_dedent(p, ls, lo, width) : lo;
}

static indent_pair model_match(const uint8_t *p, uint64_t cursor, uint64_t lo, uint64_t hi)
{
    indent_pair none = {INDENT_NONE, INDENT_NONE};
    static const char brackets[] = "()[]{}";
    uint64_t pos = cursor;
    if (!(pos >= lo && pos < hi && p[pos] && strchr(brackets, p[pos]))) {
        if (!(pos > lo && pos - 1 < hi && p[pos - 1] && strchr(brackets, p[pos - 1]))) return none;
        --pos;
    }
    uint8_t c = p[pos], open, close;
    if (c == '(' || c == ')') { open = '('; close = ')'; }
    else if (c == '[' || c == ']') { open = '['; close = ']'; }
    else { open = '{'; close = '}'; }
    uint64_t depth = 1;
    if (c == open) {
        for (uint64_t i = pos + 1; i < hi; ++i) {
            if (p[i] == open) ++depth;
            if (p[i] == close && --depth == 0) return (indent_pair){pos, i};
        }
    } else {
        for (uint64_t i = pos; i > lo;) {
            --i;
            if (p[i] == close) ++depth;
            if (p[i] == open && --depth == 0) return (indent_pair){pos, i};
        }
    }
    return none;
}

/* Line by line: every line ending at a LF inside [lo, hi), and the last line
 * when hi == n; its run is the SP/TAB before the terminator, clipped to lo. */
static size_t model_trailing(const uint8_t *p, uint64_t n, uint64_t lo, uint64_t hi, indent_range *out)
{
    size_t count = 0;
    uint64_t start = lo;
    for (uint64_t i = lo; i <= hi; ++i) {
        if (!((i < hi && p[i] == '\n') || (i == hi && hi == n))) continue;
        uint64_t end = i;
        if (i < hi && end > start && p[end - 1] == '\r') --end;
        uint64_t begin = end;
        while (begin > start && model_ws(p[begin - 1])) --begin;
        if (begin < end) out[count++] = (indent_range){begin, end};
        start = i + 1;
    }
    return count;
}

static uint64_t model_gcd(uint64_t a, uint64_t b)
{
    while (b) { uint64_t r = a % b; a = b; b = r; }
    return a;
}

static indent_style model_detect(const uint8_t *p, uint64_t n)
{
    uint64_t m = n > INDENT_PREFIX_BYTES ? INDENT_PREFIX_BYTES : n, tabs = 0, spaces = 0, g = 0, crlf = 0, lf = 0;
    for (uint64_t i = 0; i < m; i++)
        if (p[i] == '\n') {
            if (i > 0 && p[i - 1] == '\r') crlf++;
            else lf++;
        }
    for (uint64_t s = 0; s < m;) {
        uint64_t e = s;
        while (e < m && p[e] != '\n') e++;
        if (e == m && m < n) break; /* cut by the prefix */
        uint64_t lead = 0;
        bool tab = false, solid = false;
        for (; s + lead < e && model_ws(p[s + lead]); lead++)
            if (p[s + lead] == '\t') tab = true;
        for (uint64_t i = s; i < e; i++) solid = solid || !(model_ws(p[i]) || p[i] == '\r');
        if (lead && solid) {
            if (tab) tabs++;
            else { spaces++; g = model_gcd(g, lead); }
        }
        s = e + 1;
    }
    return (indent_style){tabs > spaces ? INDENT_TABS : INDENT_SPACES, crlf > lf ? INDENT_CRLF : INDENT_LF,
                          (uint8_t)(g >= 1 && g <= INDENT_MAX_WIDTH ? g : INDENT_DEFAULT_WIDTH)};
}

#endif
