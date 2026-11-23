/* indent: v1's cases, ASSERT preconditions, the degraded answers, an
 * exhaustive small-text sweep, a sweep of every INDENT_BLANK_BYTES edge and
 * long bracket and prefix cases. Every query is compared with the v2 model
 * in tests/indent_model.h. Every typing case that v1 answered is also
 * compared with the v1 whole-line model, which pins v1 behaviour for
 * ordinary lines. Each query runs twice: guarded (every chunk outside the
 * bytes the query may read is PROT_NONE, so a read outside them faults in
 * every build) and straddled (spans cross every window edge, so a missing
 * clip gives a wrong answer). */
#include "indent_model.h"

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
static size_t v1_pinned;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: FAIL ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define B MODEL_B
#define U(x) ((unsigned long long)(x))

typedef enum mode { GUARDED, STRADDLED } mode;

/* ---- fixtures -------------------------------------------------------------- */

static void build(gtext *g, const uint8_t *bytes, size_t n, uint64_t *cuts, size_t k)
{
    if (!gtext_init(g, bytes, n, cuts, k)) {
        fprintf(stderr, "indent_test: FAIL fixture mapping (%zu bytes, %zu cuts)\n", n, k);
        exit(1);
    }
}

/* Every byte its own chunk: every window edge is a chunk edge. */
static void build_bytes(gtext *g, const uint8_t *bytes, size_t n)
{
    uint64_t *cuts = malloc((n + 1) * sizeof *cuts);
    if (!cuts) exit(1);
    size_t k = 0;
    for (size_t i = 1; i < n; i++) cuts[k++] = i;
    build(g, bytes, n, cuts, k);
    free(cuts);
}

static void allow(gtext *g, const indent_range *w, size_t k)
{
    if (!gtext_allow(g, w, k)) {
        fprintf(stderr, "indent_test: FAIL fixture protection (window edge inside a chunk)\n");
        exit(1);
    }
}

/* In place: the text's iterator points at the gtext itself. */
static void text_of(gtext *g, const char *s, bool bytes)
{
    size_t n = strlen(s);
    if (bytes) build_bytes(g, (const uint8_t *)s, n);
    else build(g, (const uint8_t *)s, n, NULL, 0);
}

static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }

/* The bytes a typing query may read (header contract). The precondition
 * check reads the byte before ls unless ls is 0 or lo. */
static uint64_t pre(uint64_t ls, uint64_t lo) { return ls > 0 && ls < lo ? ls - 1 : ls; }
static indent_range enter_window(uint64_t ls, uint64_t lo)
{
    return (indent_range){pre(ls, lo), min64(lo, ls + B + 1)};
}
static void brace_windows(uint64_t n, uint64_t ls, uint64_t lo, uint64_t hi, indent_range w[2])
{
    w[0] = (indent_range){pre(ls, lo), lo - ls > B ? ls : lo};
    w[1] = (indent_range){hi, min64(n, hi + B + 2)};
}

/* ---- differential checks --------------------------------------------------- */

static void check_typing(gtext *g, const uint8_t *p, uint64_t n, uint64_t ls, uint64_t lo, uint64_t hi,
                         mode m, const unsigned *widths, size_t nw, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){enter_window(ls, lo).lo, enter_window(ls, lo).hi}, 1);
    indent_range run = indent_on_enter(&g->text, ls, lo), want = model_enter(p, ls, lo);
    CHECK(run.lo == want.lo && run.hi == want.hi, "%s enter n=%llu ls=%llu lo=%llu: [%llu,%llu) want [%llu,%llu)",
          tag, U(n), U(ls), U(lo), U(run.lo), U(run.hi), U(want.lo), U(want.hi));
    if (m == GUARDED) {
        indent_range w[2];
        brace_windows(n, ls, lo, hi, w);
        allow(g, w, 2);
    }
    uint64_t from[INDENT_MAX_WIDTH + 1];
    for (size_t i = 0; i < nw; i++) {
        from[i] = indent_on_close_brace(&g->text, ls, lo, hi, (uint8_t)widths[i]);
        uint64_t wf = model_brace(p, n, ls, lo, hi, widths[i]);
        CHECK(from[i] == wf, "%s brace n=%llu ls=%llu lo=%llu hi=%llu width=%u: from %llu want %llu",
              tag, U(n), U(ls), U(lo), U(hi), widths[i], U(from[i]), U(wf));
    }
    CHECK(g->bad == 0, "%s adapter saw a seek past the text", tag);

    /* v1 pin: with the true line start, every case v1 answered keeps v1's answer. */
    if (ls != model_line_start(p, lo)) return;
    uint64_t ln = n - (hi - lo);
    uint8_t *L = malloc(ln + 1);
    if (!L) exit(1);
    memcpy(L, p, lo);
    memcpy(L + lo, p + hi, n - hi);
    if (model_v1_answered(L, ln, lo)) {
        v1_pinned++;
        CHECK(run.hi - run.lo == model_v1_lead(L, lo), "%s v1 enter lead n=%llu lo=%llu hi=%llu", tag, U(n), U(lo), U(hi));
        for (size_t i = 0; i < nw; i++)
            CHECK(from[i] == model_v1_brace(L, ln, lo, widths[i]), "%s v1 brace n=%llu lo=%llu hi=%llu width=%u",
                  tag, U(n), U(lo), U(hi), widths[i]);
    }
    free(L);
}

static void check_bracket(gtext *g, const uint8_t *p, uint64_t cursor, uint64_t lo, uint64_t hi,
                          mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){lo, hi}, 1);
    indent_pair pair = indent_bracket_match(&g->text, cursor, lo, hi), want = model_match(p, cursor, lo, hi);
    CHECK(pair.source == want.source && pair.mate == want.mate,
          "%s bracket cursor=%llu [%llu,%llu): source %llu want %llu, mate %llu want %llu",
          tag, U(cursor), U(lo), U(hi), U(pair.source), U(want.source), U(pair.mate), U(want.mate));
}

static void check_trailing(gtext *g, const uint8_t *p, uint64_t n, uint64_t lo, uint64_t hi,
                           mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){lo, hi}, 1);
    size_t room = (size_t)(hi - lo) + 1;
    indent_range *got = calloc(room, sizeof *got), *want = calloc(room, sizeof *want);
    if (!got || !want) exit(1);
    size_t wcount = model_trailing(p, n, lo, hi, want);
    size_t count = indent_trailing_ws(&g->text, lo, hi, got, room);
    CHECK(count == wcount && memcmp(got, want, wcount * sizeof *got) == 0,
          "%s trailing [%llu,%llu): count %zu want %zu", tag, U(lo), U(hi), count, wcount);
    if (wcount > 0) {
        memset(got, 0, room * sizeof *got);
        count = indent_trailing_ws(&g->text, lo, hi, got, wcount - 1);
        CHECK(count == wcount && memcmp(got, want, (wcount - 1) * sizeof *got) == 0 && got[wcount - 1].hi == 0,
              "%s trailing capacity [%llu,%llu): count %zu want %zu", tag, U(lo), U(hi), count, wcount);
    }
    free(got);
    free(want);
}

static void check_detect(gtext *g, const uint8_t *p, uint64_t n, mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){0, min64(n, INDENT_PREFIX_BYTES)}, 1);
    indent_style got = indent_detect(&g->text), want = model_detect(p, n);
    CHECK(got.kind == want.kind && got.eol == want.eol && got.width == want.width,
          "%s detect n=%llu: %d/%d/%u want %d/%d/%u", tag, U(n), got.kind, got.eol, got.width,
          want.kind, want.eol, want.width);
}

/* ---- forked probes: guard positive control and ASSERT preconditions -------- */

enum { PROBE_FAULTED = 3 };
static void on_segv(int sig)
{
    (void)sig;
    _exit(PROBE_FAULTED);
}

static int probe(const volatile uint8_t *addr)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_segv;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGSEGV, &sa, NULL) != 0) _exit(1);
        (void)*addr;
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static const char *word(int r) { return r == PROBE_FAULTED ? "SIGSEGV" : r == 0 ? "ok" : "error"; }

/* Chunks 0 and 3 are outside the window and must fault; chunk 1 (flush
 * left) faults one byte before its data, chunk 2 (flush right) one byte
 * after; the data of both reads. Without this the guarded runs prove nothing. */
static bool guard_armed(void)
{
    gtext g;
    text_of(&g, "abcd", true);
    allow(&g, &(indent_range){1, 3}, 1);
    const gt_chunk *c = g.chunk;
    int out0 = probe(c[0].data), in1 = probe(c[1].data), before1 = probe(c[1].data - 1);
    int in2 = probe(c[2].data), after2 = probe(c[2].data + c[2].len), out3 = probe(c[3].data);
    printf("indent_test: guard page=%zu: chunk0 (outside)=%s chunk1=%s chunk1-1=%s chunk2=%s chunk2+len=%s "
           "chunk3 (outside)=%s\n", g.page, word(out0), word(in1), word(before1), word(in2), word(after2), word(out3));
    gtext_fini(&g);
    return out0 == PROBE_FAULTED && in1 == 0 && before1 == PROBE_FAULTED && in2 == 0
        && after2 == PROBE_FAULTED && out3 == PROBE_FAULTED;
}

typedef enum call { C_ENTER, C_BRACE, C_BRACKET, C_TRAILING } call;
typedef struct bad_call { call c; uint64_t a, b, d; unsigned width; const char *what; } bad_call;

/* The call must die by the always-on ASSERT (SIGABRT); its message goes to /dev/null. */
static bool dies(const indent_text *t, bad_call k)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        FILE *null = freopen("/dev/null", "w", stderr);
        (void)null;
        switch (k.c) {
        case C_ENTER: (void)indent_on_enter(t, k.a, k.b); break;
        case C_BRACE: (void)indent_on_close_brace(t, k.a, k.b, k.d, (uint8_t)k.width); break;
        case C_BRACKET: (void)indent_bracket_match(t, k.a, k.b, k.d); break;
        case C_TRAILING: (void)indent_trailing_ws(t, k.a, k.b, NULL, 0); break;
        }
        _exit(0);
    }
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void assert_cases(void)
{
    gtext g;
    text_of(&g, "ab\ncd", false); /* length 5, line starts 0 and 3 */
    static const bad_call bad[] = {
        {C_ENTER, 2, 1, 0, 0, "enter line_start > lo"},
        {C_ENTER, 3, 6, 0, 0, "enter lo past the text"},
        {C_ENTER, 1, 2, 0, 0, "enter line_start not after a LF"},
        {C_BRACE, 3, 4, 3, 4, "brace hi < lo"},
        {C_BRACE, 3, 4, 6, 4, "brace hi past the text"},
        {C_BRACE, 4, 5, 5, 4, "brace line_start not after a LF"},
        {C_BRACE, 3, 4, 4, 0, "brace width 0"},
        {C_BRACE, 3, 4, 4, INDENT_MAX_WIDTH + 1, "brace width 17"},
        {C_BRACKET, 6, 0, 5, 0, "bracket cursor past the text"},
        {C_BRACKET, 2, 3, 2, 0, "bracket lo > hi"},
        {C_BRACKET, 2, 0, 6, 0, "bracket hi past the text"},
        {C_TRAILING, 3, 2, 0, 0, "trailing lo > hi"},
        {C_TRAILING, 0, 6, 0, 0, "trailing hi past the text"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(dies(&g.text, bad[i]), "ASSERT did not fire: %s", bad[i].what);
    /* The boundaries themselves are legal. */
    CHECK(indent_on_close_brace(&g.text, 3, 5, 5, INDENT_MAX_WIDTH) == 5, "width 16, caret at the end");
    CHECK(indent_on_enter(&g.text, 5, 5).lo == 5, "line_start == lo == length");
    gtext_fini(&g);
    printf("indent_test: %zu precondition violations die by ASSERT\n", sizeof bad / sizeof bad[0]);
}

/* ---- v1 cases (tests/indent_test.c at the pin, on the v2 API) --------------- */

static void enter_case(const char *s, uint64_t lo, uint64_t want_lead)
{
    for (int bytes = 0; bytes < 2; bytes++) {
        gtext g;
        text_of(&g, s, bytes);
        uint64_t ls = model_line_start((const uint8_t *)s, lo);
        indent_range r = indent_on_enter(&g.text, ls, lo);
        CHECK(r.lo == ls && r.hi - r.lo == want_lead, "enter \"%s\" at %llu: [%llu,%llu) want %llu blanks",
              s, U(lo), U(r.lo), U(r.hi), U(want_lead));
        gtext_fini(&g);
    }
}

static void brace_case(const char *s, uint64_t cursor, unsigned width, uint64_t want)
{
    for (int bytes = 0; bytes < 2; bytes++) {
        gtext g;
        text_of(&g, s, bytes);
        uint64_t ls = model_line_start((const uint8_t *)s, cursor);
        uint64_t from = indent_on_close_brace(&g.text, ls, cursor, cursor, (uint8_t)width);
        CHECK(from == want, "brace \"%s\" at %llu width %u: from %llu want %llu", s, U(cursor), width, U(from), U(want));
        gtext_fini(&g);
    }
}

static void match_case(const indent_text *t, uint64_t cursor, uint64_t lo, uint64_t hi, uint64_t want)
{
    indent_pair p = indent_bracket_match(t, cursor, lo, hi);
    CHECK(p.mate == want && (want == INDENT_NONE) == (p.source == INDENT_NONE),
          "match cursor=%llu [%llu,%llu): mate %llu want %llu", U(cursor), U(lo), U(hi), U(p.mate), U(want));
}

static void v1_cases(void)
{
    /* v1 expected "\n" + these bytes (or "\r\n" + them); the line break is now the buffer's. */
    enter_case("", 0, 0); enter_case("  a", 3, 2);
    enter_case("\t  a\n", 4, 3); enter_case("\t  a\n", 5, 0);
    enter_case("    abc", 2, 2); enter_case("  {", 3, 2);
    enter_case("  x\r\n\ty", 7, 1); enter_case("  x\r\n", 3, 2);
    enter_case("  x\r\n", 5, 0); enter_case("\tfoo\r\n", 2, 1);

    brace_case("", 0, 4, 0); brace_case("        ", 8, 4, 4);
    brace_case("      \r\n", 6, 4, 4); brace_case("\t\t", 2, 4, 1);
    brace_case(" \t  ", 4, 4, 2); brace_case(" \t\t", 3, 4, 2);
    brace_case("  \t ", 4, 4, 3); brace_case("\t  ", 1, 4, 0);
    brace_case("  x", 2, 4, 2); brace_case("\n   ", 4, 2, 3);
    brace_case("  \t", 3, 3, 0); /* tab columns use the caller's width */

    const char bracket_text[] = "\xc3\xa9{([])}x";
    gtext g;
    for (int bytes = 0; bytes < 2; bytes++) {
        text_of(&g, bracket_text, bytes);
        match_case(&g.text, 2, 2, 8, 7); match_case(&g.text, 8, 2, 8, 2);
        match_case(&g.text, 3, 2, 8, 6); match_case(&g.text, 6, 2, 8, 3);
        match_case(&g.text, 4, 4, 6, 5); match_case(&g.text, 6, 4, 6, 4);
        match_case(&g.text, 2, 2, 7, INDENT_NONE); match_case(&g.text, 7, 3, 8, INDENT_NONE);
        match_case(&g.text, 0, 0, 9, INDENT_NONE); match_case(&g.text, 9, 0, 9, INDENT_NONE);
        match_case(&g.text, 2, 2, 2, INDENT_NONE);
        gtext_fini(&g);
    }
    text_of(&g, "\"{\"}\r\n", false);
    match_case(&g.text, 1, 0, 6, 3);
    gtext_fini(&g);

    const char *ws = "x \t\r\n  \nq\r \nx  ";
    for (int bytes = 0; bytes < 2; bytes++) {
        text_of(&g, ws, bytes);
        indent_range r[8];
        size_t c = indent_trailing_ws(&g.text, 0, 15, r, 8);
        CHECK(c == 4 && r[0].lo == 1 && r[0].hi == 3 && r[1].lo == 5 && r[1].hi == 7
              && r[2].lo == 10 && r[2].hi == 11 && r[3].lo == 13 && r[3].hi == 15, "trailing v1 text: %zu", c);
        c = indent_trailing_ws(&g.text, 2, 5, r, 8);
        CHECK(c == 1 && r[0].lo == 2 && r[0].hi == 3, "trailing clipped start");
        CHECK(indent_trailing_ws(&g.text, 0, 4, r, 8) == 0, "trailing cut run");
        CHECK(indent_trailing_ws(&g.text, 0, 15, r, 1) == 4, "trailing capacity");
        CHECK(indent_trailing_ws(&g.text, 0, 15, NULL, 0) == 4, "trailing count only");
        gtext_fini(&g);
    }

    struct { const char *s; indent_kind kind; indent_eol eol; unsigned width; } det[] = {
        {"", INDENT_SPACES, INDENT_LF, 4}, {"x\n \t \r\n", INDENT_SPACES, INDENT_LF, 4},
        {"  a\r\n    b\r\n", INDENT_SPACES, INDENT_CRLF, 2}, {"    a\n        b", INDENT_SPACES, INDENT_LF, 4},
        {"\ta\n\t b\n  c", INDENT_TABS, INDENT_LF, 2}, {"\ta\n  b", INDENT_SPACES, INDENT_LF, 2},
        {"   a\n      b", INDENT_SPACES, INDENT_LF, 3}, {" a", INDENT_SPACES, INDENT_LF, 1},
        /* v2: "nonblank" means a byte other than SP, TAB, CR (v1 counted "  \r " as content). */
        {"\t\r \n  a\n", INDENT_SPACES, INDENT_LF, 2},
        /* v2: the line break votes; a tie chooses LF. */
        {"a\r\nb\r\nc\n", INDENT_SPACES, INDENT_CRLF, 4}, {"a\r\nb\n", INDENT_SPACES, INDENT_LF, 4},
        {"\r\n", INDENT_SPACES, INDENT_CRLF, 4},
    };
    for (size_t i = 0; i < sizeof det / sizeof det[0]; i++) {
        text_of(&g, det[i].s, true);
        indent_style st = indent_detect(&g.text);
        CHECK(st.kind == det[i].kind && st.eol == det[i].eol && st.width == det[i].width,
              "detect \"%s\": %d/%d/%u", det[i].s, st.kind, st.eol, st.width);
        gtext_fini(&g);
    }
}

/* ---- v1's ERR_LIMIT and ERR_RANGE cases now have defined answers ------------ */

static void defined_cases(void)
{
    size_t n = 4 * B;
    uint8_t *blank = malloc(n), *solid = malloc(n);
    if (!blank || !solid) exit(1);
    memset(blank, ' ', n);
    memset(solid, 'x', n);
    memcpy(solid, "    ", 4);
    uint64_t cuts[] = {5, B, B + 1, B + 2, 3 * B};
    gtext g;
    build(&g, blank, n, cuts, 5);
    /* A blank line longer than the bound: Enter and '}' read at most B + 1
     * bytes and take the degraded answer; nothing past [0, B + 1) is readable. */
    allow(&g, &(indent_range){0, B + 1}, 1);
    indent_range r = indent_on_enter(&g.text, 0, n);
    CHECK(r.lo == 0 && r.hi == 0, "enter after a 4B blank run: degraded empty run, got [%llu,%llu)", U(r.lo), U(r.hi));
    CHECK(indent_on_close_brace(&g.text, 0, n, n, 4) == n, "brace after a 4B blank run: plain insertion");
    allow(&g, &(indent_range){0, B + 1}, 1);
    r = indent_on_enter(&g.text, 0, B + 1);
    CHECK(r.hi == 0, "enter after B + 1 blanks: degraded");
    allow(&g, &(indent_range){0, B}, 1);
    r = indent_on_enter(&g.text, 0, B);
    CHECK(r.lo == 0 && r.hi == B, "enter after exactly B blanks: the full run, got %llu", U(r.hi));
    /* Caret at the line start of a 4B blank line (v1: LIMIT). */
    allow(&g, &(indent_range){0, B + 2}, 1);
    r = indent_on_enter(&g.text, 0, 0);
    CHECK(r.lo == 0 && r.hi == 0, "enter at the start of a long blank line: empty run");
    CHECK(indent_on_close_brace(&g.text, 0, 0, 0, 4) == 0, "brace at the start of a long blank line: degraded plain");
    gtext_fini(&g);

    /* F1: Enter far into a long solid line keeps the line's indent (v1: LIMIT, a rejected keystroke). */
    build(&g, solid, n, cuts, 5);
    allow(&g, &(indent_range){0, 5}, 1);
    r = indent_on_enter(&g.text, 0, n);
    CHECK(r.lo == 0 && r.hi == 4, "enter at the end of a 4B solid line: [%llu,%llu) want [0,4)", U(r.lo), U(r.hi));
    CHECK(indent_on_close_brace(&g.text, 0, n, n, 4) == n, "brace at the end of a 4B solid line: plain");
    gtext_fini(&g);

    /* A giant selection is never read. */
    blank[n - 1] = '\n';
    build(&g, blank, n, cuts, 5);
    allow(&g, (indent_range[]){{0, B}, {3 * B, n}}, 2);
    r = indent_on_enter(&g.text, 0, B);
    CHECK(r.hi == B, "enter before a giant selection");
    CHECK(indent_on_close_brace(&g.text, 0, B, 3 * B, 4) == B - 4, "brace over a giant selection");
    gtext_fini(&g);

    /* Caret between CR and LF (v1: ERR_RANGE). Input can put it there, e.g.
     * pasting "\r" before a LF, so it is an answer, not an assertion. */
    text_of(&g, "  x\r\n", true);
    r = indent_on_enter(&g.text, 0, 4);
    CHECK(r.lo == 0 && r.hi == 2, "enter between CR and LF copies the line's run");
    CHECK(indent_on_close_brace(&g.text, 0, 4, 4, 4) == 4, "brace between CR and LF: plain (x and CR are text)");
    gtext_fini(&g);
    text_of(&g, "    \r\n", true);
    CHECK(indent_on_close_brace(&g.text, 0, 5, 5, 4) == 5, "brace after a blank run and a CR: plain");
    gtext_fini(&g);

    /* Unknown line start: the caller passes lo. */
    text_of(&g, "\n        ", true);
    r = indent_on_enter(&g.text, 9, 9);
    CHECK(r.lo == 9 && r.hi == 9, "enter with an unknown line start: bare line break");
    CHECK(indent_on_close_brace(&g.text, 9, 9, 9, 4) == 9, "brace with an unknown line start: plain");
    gtext_fini(&g);
    free(blank);
    free(solid);
}

/* ---- exhaustive small texts ------------------------------------------------- */

/* Every byte class the module distinguishes: SP, TAB, CR, LF, an ordinary
 * byte, an opener and its closer. Length 4 holds "x\r\n " and a bracket pair
 * around a CRLF (P). */
static const uint8_t ALPHA[] = {' ', '\t', '\r', '\n', 'x', '{', '}'};
enum { SMALL_MAX = 4 };

static void small_sweep(void)
{
    /* (P) widths 1..4 put a tab stop before, inside and after every line of <= 4 bytes. */
    static const unsigned widths[] = {1, 2, 3, 4};
    size_t texts = 0, queries = 0;
    for (size_t n = 0; n <= SMALL_MAX; n++) {
        size_t total = 1;
        for (size_t i = 0; i < n; i++) total *= sizeof ALPHA;
        for (size_t code = 0; code < total; code++) {
            uint8_t b[SMALL_MAX + 1];
            for (size_t i = 0, v = code; i < n; i++, v /= sizeof ALPHA) b[i] = ALPHA[v % sizeof ALPHA];
            gtext guarded, whole;
            build_bytes(&guarded, b, n);
            build(&whole, b, n, NULL, 0);
            for (int pass = 0; pass < 2; pass++) {
                gtext *g = pass == 0 ? &guarded : &whole;
                mode m = pass == 0 ? GUARDED : STRADDLED;
                for (uint64_t lo = 0; lo <= n; lo++)
                    for (uint64_t hi = lo; hi <= n; hi++) {
                        check_typing(g, b, n, model_line_start(b, lo), lo, hi, m, widths, 4, "small");
                        check_typing(g, b, n, lo, lo, hi, m, widths, 4, "small unknown-start");
                        check_trailing(g, b, n, lo, hi, m, "small");
                        for (uint64_t c = 0; c <= n; c++) check_bracket(g, b, c, lo, hi, m, "small");
                        queries += 2 * 5 + 1 + n + 1;
                    }
                check_detect(g, b, n, m, "small");
            }
            gtext_fini(&guarded);
            gtext_fini(&whole);
            texts++;
        }
    }
    printf("indent_test: small sweep: %zu texts over 7 byte classes, length <= %d, %zu query calls, guarded and straddled\n",
           texts, SMALL_MAX, 2 * queries);
}

/* ---- INDENT_BLANK_BYTES edges ------------------------------------------------ */

/* The cursor line is head + fill1 * k1 | selection | fill2 * k2 + tail. The k
 * values put a blank run one byte inside and one byte past the bound on each
 * side of the caret (P); the tails cover LF, CRLF, a lone CR, text and the end. */
static void bound_sweep(void)
{
    static const char *heads[] = {"", "\n", "\r\n", "x\n"};
    static const char *tails[] = {"", "\n", "\r\n", "x", "\r", "\r\r"};
    static const uint64_t ks[] = {0, 2, B, B + 1};
    static const uint64_t sels[] = {0, 1, B + 2};
    static const uint8_t fills[] = {' ', '\t', 'x'};
    static const unsigned widths[] = {3, 4}; /* (P) one width divides B, one does not */
    size_t cases = 0;
    uint8_t *b = malloc(4 * B);
    if (!b) exit(1);
    for (size_t h = 0; h < 4; h++)
    for (size_t i1 = 0; i1 < 4; i1++)
    for (size_t f1 = 0; f1 < 3; f1++)
    for (size_t s = 0; s < 3; s++)
    for (size_t i2 = 0; i2 < 4; i2++)
    for (size_t f2 = 0; f2 < 3; f2++)
    for (size_t t = 0; t < 6; t++) {
        uint64_t n = 0;
        memcpy(b, heads[h], strlen(heads[h]));
        n += strlen(heads[h]);
        uint64_t ls = n;
        memset(b + n, fills[f1], ks[i1]);
        n += ks[i1];
        uint64_t lo = n;
        for (uint64_t j = 0; j < sels[s]; j++) b[n++] = j % 2 ? 'x' : '\n';
        uint64_t hi = n;
        memset(b + n, fills[f2], ks[i2]);
        n += ks[i2];
        memcpy(b + n, tails[t], strlen(tails[t]));
        n += strlen(tails[t]);

        for (int unknown = 0; unknown < 2; unknown++) {
            uint64_t start = unknown ? lo : ls;
            indent_range w[3];
            w[0] = enter_window(start, lo);
            brace_windows(n, start, lo, hi, w + 1);
            uint64_t cuts[16];
            size_t k = 0;
            for (size_t e = 0; e < 3; e++) { cuts[k++] = w[e].lo; cuts[k++] = w[e].hi; }
            cuts[k++] = (w[0].lo + w[0].hi) / 2;
            cuts[k++] = (w[2].lo + w[2].hi) / 2;
            gtext g;
            build(&g, b, n, cuts, k);
            check_typing(&g, b, n, start, lo, hi, GUARDED, widths, 2, "bound");
            gtext_fini(&g);
            k = 0;
            for (size_t e = 0; e < 3; e++)
                for (int side = 0; side < 2; side++) {
                    uint64_t edge = side ? w[e].hi : w[e].lo;
                    if (edge > 0) cuts[k++] = edge - 1;
                    cuts[k++] = edge + 1;
                }
            build(&g, b, n, cuts, k);
            check_typing(&g, b, n, start, lo, hi, STRADDLED, widths, 2, "bound");
            gtext_fini(&g);
        }
        cases++;
    }
    free(b);
    printf("indent_test: bound sweep: %zu cursor lines around INDENT_BLANK_BYTES=%u, true and unknown line start, "
           "guarded and straddled\n", cases, INDENT_BLANK_BYTES);
}

/* ---- long bracket windows and the detection prefix ----------------------------- */

static void long_cases(void)
{
    /* Brackets on both sides of the backward copy blocks' edges, which lie at
     * multiples of the block size below the source (P). */
    uint64_t blk = INDENT_BLANK_BYTES, n = 3 * blk + 5;
    uint8_t *b = malloc(n);
    if (!b) exit(1);
    memset(b, 'x', n);
    uint64_t at[] = {0, 1, 2, 4, blk - 1, blk, blk + 1, blk + 4, 2 * blk, 2 * blk + 3, 3 * blk, 3 * blk + 3, 3 * blk + 4};
    const char kinds[] = "{({[{]}}(){})";
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) b[at[i]] = (uint8_t)kinds[i];
    for (int win = 0; win < 2; win++) {
        uint64_t lo = win ? 1 : 0, hi = win ? n - 1 : n;
        uint64_t cuts[] = {lo, hi, blk / 2, 2 * blk + 1};
        gtext guarded, straddled;
        build(&guarded, b, n, cuts, 4);
        uint64_t scuts[] = {lo + 1, hi - 1, blk + 2};
        build(&straddled, b, n, scuts, 3);
        for (size_t i = 0; i < sizeof at / sizeof at[0]; i++)
            for (uint64_t c = at[i]; c <= at[i] + 1; c++) {
                check_bracket(&guarded, b, c, lo, hi, GUARDED, "long");
                check_bracket(&straddled, b, c, lo, hi, STRADDLED, "long");
            }
        check_trailing(&guarded, b, n, lo, hi, GUARDED, "long");
        gtext_fini(&guarded);
        gtext_fini(&straddled);
    }
    free(b);

    /* Detection reads only the prefix; the line it cuts does not vote. */
    n = INDENT_PREFIX_BYTES + blk;
    b = malloc(n);
    if (!b) exit(1);
    memset(b, 'x', n);
    memcpy(b, "  a\r\n    b\r\n", 12);
    b[INDENT_PREFIX_BYTES - 8] = '\n';
    memcpy(b + INDENT_PREFIX_BYTES - 7, "\t\t\t\t\t\t\t", 7); /* cut line: would vote tabs */
    for (uint64_t i = INDENT_PREFIX_BYTES + 2; i + 1 < n; i += 4) memcpy(b + i, "\t\n", 2); /* unread: LF lines */
    uint64_t cut = INDENT_PREFIX_BYTES;
    gtext g;
    build(&g, b, n, &cut, 1);
    check_detect(&g, b, n, GUARDED, "prefix");
    indent_style st = indent_detect(&g.text);
    CHECK(st.kind == INDENT_SPACES && st.eol == INDENT_CRLF && st.width == 2, "prefix detect %d/%d/%u",
          st.kind, st.eol, st.width);
    gtext_fini(&g);
    uint64_t scut = INDENT_PREFIX_BYTES - 1;
    build(&g, b, n, &scut, 1);
    check_detect(&g, b, n, STRADDLED, "prefix");
    gtext_fini(&g);
    free(b);
}

int main(void)
{
    if (!guard_armed()) {
        fprintf(stderr, "indent_test: FAIL guard pages not armed; guarded runs would be inert\n");
        return 1;
    }
    assert_cases();
    v1_cases();
    defined_cases();
    small_sweep();
    bound_sweep();
    long_cases();
    printf("indent_test: %zu typing cases v1 answered keep v1's answer\n", v1_pinned);
    if (failures) {
        fprintf(stderr, "indent_test: %d FAILURES\n", failures);
        return 1;
    }
    puts("indent_test: all passed");
    return 0;
}
