/* L1 indent (docs/decisions/v2-0fv.12.md).
 *
 * Every scan is a forward walk over the spans of one window of the text.
 * No window contains the selection: the backward typing window ends at lo
 * and the forward one starts at hi, so the typing queries read the text
 * itself and need no logical-to-physical mapping. Offsets below lo are the
 * same in the text with and without the selection. */
#include "indent/indent.h"

#include <stdbool.h>
#include <string.h>

#define T ((uint64_t)INDENT_TYPING_BYTES)

/* A walk over the window [pos, end): the spans returned so far cover the
 * window up to pos, and the text's iterator stands at pos. */
typedef struct walk {
    const indent_text *t;
    uint64_t pos, end;
} walk;

static walk walk_open(const indent_text *t, uint64_t pos, uint64_t end)
{
    t->seek(t->iter, pos);
    return (walk){t, pos, end};
}

/* The next span of the window, clipped to it: on return it starts at
 * w->pos - n. 0 at the end of the window. */
static size_t walk_next(walk *w, const uint8_t **p)
{
    if (w->pos == w->end) return 0;
    size_t n = w->t->next(w->t->iter, p);
    if (n > w->end - w->pos) n = (size_t)(w->end - w->pos);
    w->pos += n;
    return n;
}

static uint8_t byte_at(const indent_text *t, uint64_t off)
{
    walk w = walk_open(t, off, off + 1);
    const uint8_t *p;
    return walk_next(&w, &p) ? p[0] : 0;
}

static void copy(const indent_text *t, uint64_t pos, uint8_t *out, size_t n)
{
    walk w = walk_open(t, pos, pos + n);
    const uint8_t *p;
    for (size_t m; (m = walk_next(&w, &p)); out += m) memcpy(out, p, m);
}

static bool ws(uint8_t c) { return c == ' ' || c == '\t'; }

static uint64_t back_bound(uint64_t lo) { return lo > T ? lo - T : 0; }
static uint64_t forward_end(const indent_text *t, uint64_t hi) { return t->length - hi > T ? hi + T : t->length; }

/* Shared typing preconditions. The cursor's neighbours in the text without
 * the selection are the bytes at lo - 1 and hi. */
static bool typing_range_ok(const indent_text *t, uint64_t lo, uint64_t hi)
{
    if (lo > hi || hi > t->length) return false;
    return !(lo > 0 && hi < t->length && byte_at(t, hi) == '\n' && byte_at(t, lo - 1) == '\r');
}

/* ---- Enter ----------------------------------------------------------------- */

/* One past the last LF of the backward window, else 0 when the window starts
 * the text, else INDENT_NONE. */
static uint64_t line_start(const indent_text *t, uint64_t lo)
{
    uint64_t bound = back_bound(lo), start = bound ? INDENT_NONE : 0;
    walk w = walk_open(t, bound, lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = n; i > 0; i--)
            if (p[i - 1] == '\n') {
                start = w.pos - n + i;
                break;
            }
    return start;
}

typedef enum term { TERM_LF, TERM_CRLF, TERM_EOF, TERM_UNKNOWN } term;

/* The cursor line's terminator, from the forward window. A LF at hi is
 * never the end of a CRLF: typing_range_ok refused that cursor. */
static term line_term(const indent_text *t, uint64_t hi)
{
    uint64_t end = forward_end(t, hi);
    walk w = walk_open(t, hi, end);
    const uint8_t *p;
    uint8_t prev = 0;
    for (size_t n; (n = walk_next(&w, &p)); prev = p[n - 1]) {
        const uint8_t *lf = memchr(p, '\n', n);
        if (lf) return (lf > p ? lf[-1] : prev) == '\r' ? TERM_CRLF : TERM_LF;
    }
    return end == t->length ? TERM_EOF : TERM_UNKNOWN;
}

indent_code indent_on_enter(const indent_text *t, uint64_t lo, uint64_t hi,
                            uint8_t out[INDENT_ENTER_MAX], size_t *length)
{
    *length = 0;
    if (!typing_range_ok(t, lo, hi)) return INDENT_ERR_RANGE;
    uint64_t start = line_start(t, lo);
    if (start == INDENT_NONE) return INDENT_ERR_LIMIT;
    size_t k = 0;
    switch (line_term(t, hi)) {
    case TERM_UNKNOWN: return INDENT_ERR_LIMIT;
    case TERM_LF: break;
    case TERM_CRLF: out[k++] = '\r'; break;
    case TERM_EOF:
        /* start >= 2 means a LF at start - 1; the byte before it names the
         * previous line's terminator. */
        if (start >= 2 && byte_at(t, start - 2) == '\r') out[k++] = '\r';
        break;
    }
    out[k++] = '\n';
    /* (P) lo - start <= T, so k <= INDENT_ENTER_MAX. */
    walk w = walk_open(t, start, lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));) {
        size_t i = 0;
        while (i < n && ws(p[i])) i++;
        memcpy(out + k, p, i);
        k += i;
        if (i < n) break;
    }
    *length = k;
    return INDENT_OK;
}

/* ---- '}' ------------------------------------------------------------------- */

/* What one typing window shows of the cursor line: a byte other than SP/TAB
 * (CONTENT), the line's edge after SP/TAB only (EDGE), or SP/TAB up to the
 * window's end short of the line's edge (UNKNOWN). */
typedef enum side { SIDE_CONTENT, SIDE_EDGE, SIDE_UNKNOWN } side;

/* The last byte of the backward window other than SP/TAB decides: a LF is
 * the edge and *start is one past it. Reaching the text start is the edge. */
static side before_side(const indent_text *t, uint64_t lo, uint64_t *start)
{
    uint64_t bound = back_bound(lo);
    side s = bound ? SIDE_UNKNOWN : SIDE_EDGE;
    *start = 0;
    walk w = walk_open(t, bound, lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));) {
        size_t i = n;
        while (i > 0 && ws(p[i - 1])) i--;
        if (i == 0) continue;
        s = p[i - 1] == '\n' ? SIDE_EDGE : SIDE_CONTENT;
        *start = w.pos - n + i;
    }
    return s;
}

typedef enum tail { TAIL_BLANK, TAIL_CR } tail;

/* The first byte of the forward window other than SP/TAB decides: a LF or a
 * CRLF is the edge, a lone CR or anything else is content. The text end is
 * the edge, unless it follows a lone CR. */
static side after_side(const indent_text *t, uint64_t hi)
{
    uint64_t end = forward_end(t, hi);
    tail st = TAIL_BLANK;
    walk w = walk_open(t, hi, end);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            switch (st) {
            case TAIL_CR: return p[i] == '\n' ? SIDE_EDGE : SIDE_CONTENT;
            case TAIL_BLANK:
                if (ws(p[i])) break;
                if (p[i] == '\n') return SIDE_EDGE;
                if (p[i] != '\r') return SIDE_CONTENT;
                st = TAIL_CR;
                break;
            }
        }
    if (end < t->length) return SIDE_UNKNOWN;
    return st == TAIL_CR ? SIDE_CONTENT : SIDE_EDGE;
}

indent_code indent_on_close_brace(const indent_text *t, uint64_t lo, uint64_t hi,
                                  uint8_t width, uint64_t *from)
{
    *from = INDENT_NONE;
    if (width == 0 || width > INDENT_MAX_WIDTH) return INDENT_ERR_ARGUMENT;
    if (!typing_range_ok(t, lo, hi)) return INDENT_ERR_RANGE;
    uint64_t start;
    side before = before_side(t, lo, &start);
    side after = before == SIDE_CONTENT ? SIDE_CONTENT : after_side(t, hi);
    if (before == SIDE_CONTENT || after == SIDE_CONTENT) {
        *from = lo;
        return INDENT_OK;
    }
    if (before == SIDE_UNKNOWN || after == SIDE_UNKNOWN) return INDENT_ERR_LIMIT;
    /* [start, lo) is SP/TAB only. stop is one past the last completed stop
     * (start if none), prev the one before it, column < width the columns
     * since stop. */
    uint64_t stop = start, prev = start;
    unsigned column = 0;
    walk w = walk_open(t, start, lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            column = p[i] == '\t' ? width : column + 1;
            if (column < width) continue;
            prev = stop;
            stop = w.pos - n + i + 1;
            column = 0;
        }
    *from = column ? stop : prev;
    return INDENT_OK;
}

/* ---- brackets ---------------------------------------------------------------- */

static uint8_t partner(uint8_t c)
{
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    default: return 0;
    }
}

static uint64_t mate_forward(const indent_text *t, uint64_t pos, uint64_t hi, uint8_t open, uint8_t close)
{
    uint64_t depth = 1;
    walk w = walk_open(t, pos, hi);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            if (p[i] == open) depth++;
            else if (p[i] == close && --depth == 0) return w.pos - n + i;
        }
    return INDENT_NONE;
}

/* Backward through copied blocks. The block is T bytes so that this, the
 * only stack scratch in the module, has the one size already justified. */
static uint64_t mate_backward(const indent_text *t, uint64_t lo, uint64_t end, uint8_t close, uint8_t open)
{
    uint8_t block[INDENT_TYPING_BYTES];
    uint64_t depth = 1;
    while (end > lo) {
        size_t n = end - lo > sizeof block ? sizeof block : (size_t)(end - lo);
        end -= n;
        copy(t, end, block, n);
        for (size_t i = n; i > 0; i--) {
            if (block[i - 1] == close) depth++;
            else if (block[i - 1] == open && --depth == 0) return end + i - 1;
        }
    }
    return INDENT_NONE;
}

indent_code indent_bracket_match(const indent_text *t, uint64_t cursor,
                                 uint64_t lo, uint64_t hi, indent_pair *pair)
{
    *pair = (indent_pair){INDENT_NONE, INDENT_NONE};
    if (cursor > t->length || lo > hi || hi > t->length) return INDENT_ERR_RANGE;
    uint64_t source = cursor;
    uint8_t c = 0, mate = 0;
    if (cursor >= lo && cursor < hi) mate = partner(c = byte_at(t, cursor));
    if (!mate && cursor > lo && cursor <= hi) mate = partner(c = byte_at(t, source = cursor - 1));
    if (!mate) return INDENT_OK;
    uint64_t found = c == '(' || c == '[' || c == '{' ? mate_forward(t, source + 1, hi, c, mate)
                                                      : mate_backward(t, lo, source, c, mate);
    if (found != INDENT_NONE) *pair = (indent_pair){source, found};
    return INDENT_OK;
}

/* ---- trailing whitespace ----------------------------------------------------- */

/* TEXT: no open run. RUN: an SP/TAB run started at `run` and reaches the
 * current byte. RUN_CR: that run is followed by one CR. */
typedef enum trail { TRAIL_TEXT, TRAIL_RUN, TRAIL_RUN_CR } trail;

static void emit(indent_range *out, size_t cap, size_t *count, uint64_t lo, uint64_t hi)
{
    if (*count < cap) out[*count] = (indent_range){lo, hi};
    ++*count;
}

indent_code indent_trailing_ws(const indent_text *t, uint64_t lo, uint64_t hi,
                               indent_range *out, size_t cap, size_t *count)
{
    *count = 0;
    if (lo > hi || hi > t->length) return INDENT_ERR_RANGE;
    trail st = TRAIL_TEXT;
    uint64_t run = 0;
    walk w = walk_open(t, lo, hi);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            uint64_t at = w.pos - n + i;
            uint8_t c = p[i];
            switch (st) {
            case TRAIL_TEXT:
                if (ws(c)) { st = TRAIL_RUN; run = at; }
                break;
            case TRAIL_RUN:
                if (c == '\n') { emit(out, cap, count, run, at); st = TRAIL_TEXT; }
                else if (c == '\r') st = TRAIL_RUN_CR;
                else if (!ws(c)) st = TRAIL_TEXT;
                break;
            case TRAIL_RUN_CR:
                if (c == '\n') { emit(out, cap, count, run, at - 1); st = TRAIL_TEXT; }
                else if (ws(c)) { st = TRAIL_RUN; run = at; } /* the lone CR was text */
                else st = TRAIL_TEXT;
                break;
            }
        }
    if (hi == t->length && st == TRAIL_RUN) emit(out, cap, count, run, hi);
    return *count > cap ? INDENT_ERR_CAPACITY : INDENT_OK;
}

/* ---- detection ------------------------------------------------------------------ */

/* Of the current line: LEAD while only SP/TAB were seen, BLANK after a CR
 * ended the leading run with nothing but SP/TAB/CR since, BODY once a byte
 * other than SP/TAB/CR was seen. */
typedef enum phase { PHASE_LEAD, PHASE_BLANK, PHASE_BODY } phase;

typedef struct votes { uint64_t tabs, spaces, gcd; } votes;

static uint64_t gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static void vote(votes *v, phase ph, uint64_t lead, indent_kind kind)
{
    switch (ph) {
    case PHASE_LEAD:
    case PHASE_BLANK: return;
    case PHASE_BODY: break;
    }
    if (!lead) return;
    switch (kind) {
    case INDENT_TABS: v->tabs++; break;
    case INDENT_SPACES: v->spaces++; v->gcd = gcd(v->gcd, lead); break;
    }
}

indent_style indent_detect(const indent_text *t)
{
    uint64_t end = t->length > INDENT_PREFIX_BYTES ? INDENT_PREFIX_BYTES : t->length;
    votes v = {0, 0, 0};
    /* The current line: its phase, the length of its leading SP/TAB run and
     * whether that run holds a tab. */
    phase ph = PHASE_LEAD;
    uint64_t lead = 0;
    indent_kind kind = INDENT_SPACES;
    walk w = walk_open(t, 0, end);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            uint8_t c = p[i];
            if (c == '\n') {
                vote(&v, ph, lead, kind);
                ph = PHASE_LEAD;
                lead = 0;
                kind = INDENT_SPACES;
                continue;
            }
            switch (ph) {
            case PHASE_LEAD:
                if (ws(c)) {
                    lead++;
                    if (c == '\t') kind = INDENT_TABS;
                } else {
                    ph = c == '\r' ? PHASE_BLANK : PHASE_BODY;
                }
                break;
            case PHASE_BLANK:
                if (!ws(c) && c != '\r') ph = PHASE_BODY;
                break;
            case PHASE_BODY: break;
            }
        }
    if (end == t->length) vote(&v, ph, lead, kind); /* the final line is complete */
    uint64_t width = v.gcd >= 1 && v.gcd <= INDENT_MAX_WIDTH ? v.gcd : INDENT_DEFAULT_WIDTH;
    return (indent_style){v.tabs > v.spaces ? INDENT_TABS : INDENT_SPACES, (uint8_t)width};
}
