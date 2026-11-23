/* L1 indent (docs/decisions/v2-0fv.12.md).
 *
 * Every read is a forward walk over the spans of one window of the text.
 * The typing queries are handed the cursor line's start and read only the
 * blank run from it and the bytes from the end of the selection, so their
 * cost is the length of the indentation, never a search for a line edge. */
#include "indent/indent.h"
#include "os/os.h"

#include <stdbool.h>
#include <string.h>

#define B ((uint64_t)INDENT_BLANK_BYTES)

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

static void typing_preconditions(const indent_text *t, uint64_t ls, uint64_t lo, uint64_t hi)
{
    ASSERT(ls <= lo && lo <= hi && hi <= t->length);
    if (ls != 0 && ls != lo) {
        uint8_t before = byte_at(t, ls - 1);
        ASSERT(before == '\n');
    }
}

/* ---- Enter ----------------------------------------------------------------- */

indent_range indent_on_enter(const indent_text *t, uint64_t ls, uint64_t lo)
{
    typing_preconditions(t, ls, lo, lo);
    /* Reading B + 1 bytes is enough to tell a run longer than B. */
    uint64_t run = ls;
    walk w = walk_open(t, ls, lo - ls > B ? ls + B + 1 : lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));) {
        size_t i = 0;
        while (i < n && ws(p[i])) i++;
        run += i;
        if (i < n) break;
    }
    return (indent_range){ls, run - ls > B ? ls : run};
}

/* ---- '}' ------------------------------------------------------------------- */

/* BLANK: only SP/TAB were seen. CR: they were followed by one CR, whose
 * successor decides. */
typedef enum tail { TAIL_BLANK, TAIL_CR } tail;

/* True when the bytes from hi are at most B blanks, then a LF, a CRLF or
 * the end of the text. Within B + 2 bytes the answer is always decided, so
 * a walk that runs out has reached the end of the text. */
static bool blank_tail(const indent_text *t, uint64_t hi)
{
    uint64_t blanks = 0;
    tail st = TAIL_BLANK;
    walk w = walk_open(t, hi, t->length - hi > B + 2 ? hi + B + 2 : t->length);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            switch (st) {
            case TAIL_CR: return p[i] == '\n';
            case TAIL_BLANK:
                if (ws(p[i])) {
                    if (++blanks > B) return false;
                    break;
                }
                if (p[i] == '\n') return true;
                if (p[i] != '\r') return false;
                st = TAIL_CR;
                break;
            }
        }
    return st == TAIL_BLANK; /* a lone CR at the end is text */
}

uint64_t indent_on_close_brace(const indent_text *t, uint64_t ls, uint64_t lo, uint64_t hi, uint8_t width)
{
    ASSERT(width >= 1 && width <= INDENT_MAX_WIDTH);
    typing_preconditions(t, ls, lo, hi);
    if (lo - ls > B) return lo; /* degraded: more than B blanks count as text */
    /* Over the SP/TAB run [ls, x): stop is one past the last completed
     * indent stop (ls if none), prev the one before it, and column < width
     * the columns since stop. */
    uint64_t stop = ls, prev = ls;
    unsigned column = 0;
    walk w = walk_open(t, ls, lo);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; i++) {
            if (!ws(p[i])) return lo;
            column = p[i] == '\t' ? width : column + 1;
            if (column < width) continue;
            prev = stop;
            stop = w.pos - n + i + 1;
            column = 0;
        }
    if (!blank_tail(t, hi)) return lo;
    return column ? stop : prev;
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

/* Backward through copied blocks: a forward iterator cannot run in reverse.
 * (E) One seek per block. The block reuses the module's one bound, so the
 * stack scratch is INDENT_BLANK_BYTES and introduces no new number. */
static uint64_t mate_backward(const indent_text *t, uint64_t lo, uint64_t end, uint8_t close, uint8_t open)
{
    uint8_t block[INDENT_BLANK_BYTES];
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

indent_pair indent_bracket_match(const indent_text *t, uint64_t cursor, uint64_t lo, uint64_t hi)
{
    ASSERT(cursor <= t->length && lo <= hi && hi <= t->length);
    indent_pair none = {INDENT_NONE, INDENT_NONE};
    uint64_t source = cursor;
    uint8_t c = 0, mate = 0;
    if (cursor >= lo && cursor < hi) mate = partner(c = byte_at(t, cursor));
    if (!mate && cursor > lo && cursor <= hi) mate = partner(c = byte_at(t, source = cursor - 1));
    if (!mate) return none;
    uint64_t found = c == '(' || c == '[' || c == '{' ? mate_forward(t, source + 1, hi, c, mate)
                                                      : mate_backward(t, lo, source, c, mate);
    return found == INDENT_NONE ? none : (indent_pair){source, found};
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

size_t indent_trailing_ws(const indent_text *t, uint64_t lo, uint64_t hi, indent_range *out, size_t cap)
{
    ASSERT(lo <= hi && hi <= t->length);
    size_t count = 0;
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
                if (c == '\n') { emit(out, cap, &count, run, at); st = TRAIL_TEXT; }
                else if (c == '\r') st = TRAIL_RUN_CR;
                else if (!ws(c)) st = TRAIL_TEXT;
                break;
            case TRAIL_RUN_CR:
                if (c == '\n') { emit(out, cap, &count, run, at - 1); st = TRAIL_TEXT; }
                else if (ws(c)) { st = TRAIL_RUN; run = at; } /* the lone CR was text */
                else st = TRAIL_TEXT;
                break;
            }
        }
    if (hi == t->length && st == TRAIL_RUN) emit(out, cap, &count, run, hi);
    return count;
}

/* ---- detection ------------------------------------------------------------------ */

/* Of the current line: LEAD while only SP/TAB were seen, BLANK after a CR
 * ended the leading run with nothing but SP/TAB/CR since, BODY once a byte
 * other than SP/TAB/CR was seen. */
typedef enum phase { PHASE_LEAD, PHASE_BLANK, PHASE_BODY } phase;

typedef struct votes { uint64_t tabs, spaces, gcd, crlf, lf; } votes;

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
    votes v = {0, 0, 0, 0, 0};
    /* The current line: its phase, the length of its leading SP/TAB run and
     * whether that run holds a tab; prev is the byte before the current one. */
    phase ph = PHASE_LEAD;
    uint64_t lead = 0;
    indent_kind kind = INDENT_SPACES;
    uint8_t prev = 0;
    walk w = walk_open(t, 0, end);
    const uint8_t *p;
    for (size_t n; (n = walk_next(&w, &p));)
        for (size_t i = 0; i < n; prev = p[i++]) {
            uint8_t c = p[i];
            if (c == '\n') {
                if (prev == '\r') v.crlf++;
                else v.lf++;
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
    return (indent_style){v.tabs > v.spaces ? INDENT_TABS : INDENT_SPACES,
                          v.crlf > v.lf ? INDENT_CRLF : INDENT_LF, (uint8_t)width};
}
