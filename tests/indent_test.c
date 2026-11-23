/* indent: v1's cases, an exhaustive small-text sweep, a sweep of every
 * typing-bound edge and long bracket and prefix cases. Every query is
 * compared with the reference model in tests/indent_model.h and run twice:
 * guarded (every chunk outside the query's readable windows is PROT_NONE,
 * so a read outside them faults in every build) and straddled (spans cross
 * every window edge, so a missing clip gives a wrong answer). */
#include "indent_model.h"

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: FAIL ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define T MODEL_T
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

/* The readable windows of a typing query: T bytes plus the terminator
 * probe before the cursor, T bytes after the selection. */
static void typing_windows(uint64_t n, uint64_t lo, uint64_t hi, indent_range w[2])
{
    w[0] = (indent_range){lo > T + 1 ? lo - T - 1 : 0, lo};
    w[1] = (indent_range){hi, n - hi > T ? hi + T : n};
}

/* ---- differential checks --------------------------------------------------- */

static bool all_bytes(const uint8_t *p, size_t from, size_t to, uint8_t v)
{
    for (size_t i = from; i < to; i++)
        if (p[i] != v) return false;
    return true;
}

static void check_typing(gtext *g, const uint8_t *phys, uint64_t n, uint64_t lo, uint64_t hi,
                         mode m, const unsigned *widths, size_t nw, const char *tag)
{
    if (m == GUARDED) {
        indent_range w[2];
        typing_windows(n, lo, hi, w);
        allow(g, w, 2);
    }
    uint64_t ln = n - (hi - lo);
    uint8_t *L = malloc(ln + 1);
    if (!L) exit(1);
    memcpy(L, phys, lo);
    memcpy(L + lo, phys + hi, n - hi);

    uint8_t out[INDENT_ENTER_MAX], want[INDENT_ENTER_MAX];
    memset(out, 0xa5, sizeof out);
    size_t len = 99, wlen = 0;
    indent_code rc = indent_on_enter(&g->text, lo, hi, out, &len);
    indent_code wrc = model_enter(L, ln, lo, want, &wlen);
    bool untouched = all_bytes(out, rc == INDENT_OK ? len : 0, sizeof out, 0xa5);
    CHECK(rc == wrc && len == wlen && memcmp(out, want, wlen) == 0 && untouched,
          "%s enter n=%llu lo=%llu hi=%llu: rc %d want %d, length %zu want %zu, tail untouched %d",
          tag, U(n), U(lo), U(hi), rc, wrc, len, wlen, untouched);
    for (size_t i = 0; i < nw; i++) {
        uint64_t from = 7, wfrom = 0;
        bool consistent = false;
        rc = indent_on_close_brace(&g->text, lo, hi, (uint8_t)widths[i], &from);
        wrc = model_brace(L, ln, lo, widths[i], &wfrom, &consistent);
        CHECK(consistent, "%s model brace verdict contradicts its whole line n=%llu lo=%llu", tag, U(n), U(lo));
        CHECK(rc == wrc && from == wfrom, "%s brace n=%llu lo=%llu hi=%llu width=%u: rc %d want %d, from %llu want %llu",
              tag, U(n), U(lo), U(hi), widths[i], rc, wrc, U(from), U(wfrom));
    }
    CHECK(g->bad == 0, "%s adapter saw a seek past the text", tag);
    free(L);
}

static void check_bracket(gtext *g, const uint8_t *phys, uint64_t cursor, uint64_t lo, uint64_t hi,
                          mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){lo, hi}, 1);
    indent_pair pair = {7, 7}, want = model_match(phys, cursor, lo, hi);
    indent_code rc = indent_bracket_match(&g->text, cursor, lo, hi, &pair);
    CHECK(rc == INDENT_OK && pair.source == want.source && pair.mate == want.mate,
          "%s bracket cursor=%llu [%llu,%llu): rc %d source %llu want %llu, mate %llu want %llu",
          tag, U(cursor), U(lo), U(hi), rc, U(pair.source), U(want.source), U(pair.mate), U(want.mate));
}

static void check_trailing(gtext *g, const uint8_t *phys, uint64_t n, uint64_t lo, uint64_t hi,
                           mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){lo, hi}, 1);
    size_t room = (size_t)(hi - lo) + 1;
    indent_range *got = calloc(room, sizeof *got), *want = calloc(room, sizeof *want);
    if (!got || !want) exit(1);
    size_t count = 99, wcount = model_trailing(phys, n, lo, hi, want);
    indent_code rc = indent_trailing_ws(&g->text, lo, hi, got, room, &count);
    CHECK(rc == INDENT_OK && count == wcount && memcmp(got, want, wcount * sizeof *got) == 0,
          "%s trailing [%llu,%llu): rc %d count %zu want %zu", tag, U(lo), U(hi), rc, count, wcount);
    if (wcount > 0) {
        memset(got, 0, room * sizeof *got);
        rc = indent_trailing_ws(&g->text, lo, hi, got, wcount - 1, &count);
        CHECK(rc == INDENT_ERR_CAPACITY && count == wcount
              && memcmp(got, want, (wcount - 1) * sizeof *got) == 0 && got[wcount - 1].hi == 0,
              "%s trailing capacity [%llu,%llu): rc %d count %zu want %zu", tag, U(lo), U(hi), rc, count, wcount);
    }
    free(got);
    free(want);
}

static void check_detect(gtext *g, const uint8_t *phys, uint64_t n, mode m, const char *tag)
{
    if (m == GUARDED) allow(g, &(indent_range){0, n > INDENT_PREFIX_BYTES ? INDENT_PREFIX_BYTES : n}, 1);
    indent_style got = indent_detect(&g->text), want = model_detect(phys, n);
    CHECK(got.kind == want.kind && got.width == want.width, "%s detect n=%llu: %d/%u want %d/%u",
          tag, U(n), got.kind, got.width, want.kind, want.width);
}

/* ---- guard positive control ------------------------------------------------- */

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

/* ---- v1 cases (tests/indent_test.c at the pin, on the v2 API) --------------- */

static void enter_case(const char *s, uint64_t lo, uint64_t hi, const char *want)
{
    for (int bytes = 0; bytes < 2; bytes++) {
        gtext g;
    text_of(&g, s, bytes);
        uint8_t out[INDENT_ENTER_MAX];
        size_t n = 99;
        indent_code rc = indent_on_enter(&g.text, lo, hi, out, &n);
        CHECK(rc == INDENT_OK && n == strlen(want) && memcmp(out, want, n) == 0,
              "enter \"%s\" [%llu,%llu): rc %d length %zu", s, U(lo), U(hi), rc, n);
        gtext_fini(&g);
    }
}

static void brace_case(const char *s, uint64_t cursor, unsigned width, uint64_t want)
{
    for (int bytes = 0; bytes < 2; bytes++) {
        gtext g;
    text_of(&g, s, bytes);
        uint64_t from = 7;
        indent_code rc = indent_on_close_brace(&g.text, cursor, cursor, (uint8_t)width, &from);
        CHECK(rc == INDENT_OK && from == want, "brace \"%s\" at %llu width %u: rc %d from %llu want %llu",
              s, U(cursor), width, rc, U(from), U(want));
        gtext_fini(&g);
    }
}

static void match_case(const indent_text *t, uint64_t cursor, uint64_t lo, uint64_t hi, uint64_t want)
{
    indent_pair p = {0, 0};
    indent_code rc = indent_bracket_match(t, cursor, lo, hi, &p);
    CHECK(rc == INDENT_OK && p.mate == want && (want == INDENT_NONE) == (p.source == INDENT_NONE),
          "match cursor=%llu [%llu,%llu): rc %d mate %llu want %llu", U(cursor), U(lo), U(hi), rc, U(p.mate), U(want));
}

static void v1_cases(void)
{
    enter_case("", 0, 0, "\n"); enter_case("  a", 3, 3, "\n  ");
    enter_case("\t  a\n", 4, 4, "\n\t  "); enter_case("\t  a\n", 5, 5, "\n");
    enter_case("    abc", 2, 2, "\n  "); enter_case("  {", 3, 3, "\n  ");
    enter_case("  x\r\n\ty", 7, 7, "\r\n\t"); enter_case("  x\r\n", 3, 3, "\r\n  ");
    enter_case("  x\r\n", 5, 5, "\r\n"); enter_case("\tfoo\r\n", 2, 2, "\r\n\t");
    /* Selections: the omitted bytes are not part of the line. */
    enter_case("    XX\n", 4, 6, "\n    "); enter_case("  x\r\n\ty", 6, 7, "\r\n\t");

    gtext g;
    text_of(&g, "a\r\n", false);
    uint8_t out[INDENT_ENTER_MAX];
    size_t n = 99;
    uint64_t from = 7;
    CHECK(indent_on_enter(&g.text, 2, 2, out, &n) == INDENT_ERR_RANGE && n == 0, "enter between CR and LF");
    CHECK(indent_on_enter(&g.text, 4, 4, out, &n) == INDENT_ERR_RANGE && n == 0, "enter past the end");
    CHECK(indent_on_enter(&g.text, 2, 1, out, &n) == INDENT_ERR_RANGE && n == 0, "enter lo > hi");
    CHECK(indent_on_close_brace(&g.text, 2, 2, 4, &from) == INDENT_ERR_RANGE && from == INDENT_NONE, "brace CR|LF");
    CHECK(indent_on_close_brace(&g.text, 0, 4, 4, &from) == INDENT_ERR_RANGE && from == INDENT_NONE, "brace hi > n");
    CHECK(indent_on_close_brace(&g.text, 0, 0, 0, &from) == INDENT_ERR_ARGUMENT && from == INDENT_NONE, "width 0");
    CHECK(indent_on_close_brace(&g.text, 0, 0, INDENT_MAX_WIDTH + 1, &from) == INDENT_ERR_ARGUMENT, "width 17");
    CHECK(indent_on_close_brace(&g.text, 0, 0, INDENT_MAX_WIDTH, &from) == INDENT_OK && from == 0, "width 16");
    gtext_fini(&g);

    brace_case("", 0, 4, 0); brace_case("        ", 8, 4, 4);
    brace_case("      \r\n", 6, 4, 4); brace_case("\t\t", 2, 4, 1);
    brace_case(" \t  ", 4, 4, 2); brace_case(" \t\t", 3, 4, 2);
    brace_case("  \t ", 4, 4, 3); brace_case("\t  ", 1, 4, 0);
    brace_case("  x", 2, 4, 2); brace_case("\n   ", 4, 2, 3);
    brace_case("  \t", 3, 3, 0); /* tab columns use the caller's width */

    const char bracket_text[] = "\xc3\xa9{([])}x";
    for (int bytes = 0; bytes < 2; bytes++) {
        text_of(&g, bracket_text, bytes);
        match_case(&g.text, 2, 2, 8, 7); match_case(&g.text, 8, 2, 8, 2);
        match_case(&g.text, 3, 2, 8, 6); match_case(&g.text, 6, 2, 8, 3);
        match_case(&g.text, 4, 4, 6, 5); match_case(&g.text, 6, 4, 6, 4);
        match_case(&g.text, 2, 2, 7, INDENT_NONE); match_case(&g.text, 7, 3, 8, INDENT_NONE);
        match_case(&g.text, 0, 0, 9, INDENT_NONE); match_case(&g.text, 9, 0, 9, INDENT_NONE);
        match_case(&g.text, 2, 2, 2, INDENT_NONE);
        indent_pair p;
        CHECK(indent_bracket_match(&g.text, 10, 0, 9, &p) == INDENT_ERR_RANGE && p.source == INDENT_NONE, "cursor > n");
        CHECK(indent_bracket_match(&g.text, 2, 8, 2, &p) == INDENT_ERR_RANGE && p.mate == INDENT_NONE, "lo > hi");
        CHECK(indent_bracket_match(&g.text, 2, 0, 10, &p) == INDENT_ERR_RANGE, "hi > n");
        gtext_fini(&g);
    }
    text_of(&g, "\"{\"}\r\n", false);
    match_case(&g.text, 1, 0, 6, 3);
    gtext_fini(&g);

    const char *ws = "x \t\r\n  \nq\r \nx  ";
    for (int bytes = 0; bytes < 2; bytes++) {
        text_of(&g, ws, bytes);
        indent_range r[8];
        size_t c = 0;
        CHECK(indent_trailing_ws(&g.text, 0, 15, r, 8, &c) == INDENT_OK && c == 4
              && r[0].lo == 1 && r[0].hi == 3 && r[1].lo == 5 && r[1].hi == 7
              && r[2].lo == 10 && r[2].hi == 11 && r[3].lo == 13 && r[3].hi == 15, "trailing v1 text: %zu", c);
        CHECK(indent_trailing_ws(&g.text, 2, 5, r, 8, &c) == INDENT_OK && c == 1 && r[0].lo == 2 && r[0].hi == 3,
              "trailing clipped start");
        CHECK(indent_trailing_ws(&g.text, 0, 4, r, 8, &c) == INDENT_OK && c == 0, "trailing cut run");
        CHECK(indent_trailing_ws(&g.text, 0, 15, r, 1, &c) == INDENT_ERR_CAPACITY && c == 4, "trailing capacity");
        CHECK(indent_trailing_ws(&g.text, 0, 15, NULL, 0, &c) == INDENT_ERR_CAPACITY && c == 4, "trailing count only");
        CHECK(indent_trailing_ws(&g.text, 8, 3, r, 8, &c) == INDENT_ERR_RANGE && c == 0, "trailing lo > hi");
        gtext_fini(&g);
    }

    struct { const char *s; indent_kind kind; unsigned width; } det[] = {
        {"", INDENT_SPACES, 4}, {"x\n \t \r\n", INDENT_SPACES, 4},
        {"  a\r\n    b\r\n", INDENT_SPACES, 2}, {"    a\n        b", INDENT_SPACES, 4},
        {"\ta\n\t b\n  c", INDENT_TABS, 2}, {"\ta\n  b", INDENT_SPACES, 2},
        {"   a\n      b", INDENT_SPACES, 3}, {" a", INDENT_SPACES, 1},
        /* v2: "nonblank" means a byte other than SP, TAB, CR (v1 counted "  \r " as content). */
        {"\t\r \n  a\n", INDENT_SPACES, 2},
    };
    for (size_t i = 0; i < sizeof det / sizeof det[0]; i++) {
        text_of(&g, det[i].s, true);
        indent_style st = indent_detect(&g.text);
        CHECK(st.kind == det[i].kind && st.width == det[i].width, "detect \"%s\": %d/%u", det[i].s, st.kind, st.width);
        gtext_fini(&g);
    }
}

/* ---- ERR_LIMIT: no writes, and only when the answer needs unread bytes ------ */

static void limit_cases(void)
{
    size_t n = 4 * T;
    uint8_t *spaces = malloc(n), *solid = malloc(n);
    if (!spaces || !solid) exit(1);
    memset(spaces, ' ', n);
    memset(solid, 'x', n);
    uint64_t cuts[] = {T, 3 * T};
    gtext g;
    build(&g, spaces, n, cuts, 2);
    uint8_t out[INDENT_ENTER_MAX];
    for (int side = 0; side < 2; side++) {
        uint64_t c = side ? n : 0;
        /* Only the typing window on the open side is readable. */
        allow(&g, &(indent_range){side ? 3 * T : 0, side ? n : T}, 1);
        memset(out, 0xa5, sizeof out);
        size_t len = 99;
        uint64_t from = 7;
        CHECK(indent_on_enter(&g.text, c, c, out, &len) == INDENT_ERR_LIMIT && len == 0
              && all_bytes(out, 0, sizeof out, 0xa5), "enter on a 4T blank line at %llu: limit, no writes", U(c));
        CHECK(indent_on_close_brace(&g.text, c, c, 4, &from) == INDENT_ERR_LIMIT && from == INDENT_NONE,
              "brace on a 4T blank line at %llu: limit", U(c));
    }
    /* A giant selection is never read: [T, 3T) stays PROT_NONE. */
    uint8_t *sel = malloc(n);
    if (!sel) exit(1);
    memset(sel, ' ', n);
    sel[n - 1] = '\n';
    gtext_fini(&g);
    build(&g, sel, n, cuts, 2);
    allow(&g, (indent_range[]){{0, T}, {3 * T, n}}, 2);
    size_t len = 99;
    uint64_t from = 7;
    CHECK(indent_on_enter(&g.text, T, 3 * T, out, &len) == INDENT_OK && len == T + 1 && out[0] == '\n'
          && all_bytes(out, 1, T + 1, ' '), "enter over a giant selection: length %zu", len);
    CHECK(indent_on_close_brace(&g.text, T, 3 * T, 4, &from) == INDENT_OK && from == T - 4,
          "brace over a giant selection: from %llu", U(from));
    gtext_fini(&g);

    /* v1 returned LIMIT here; content inside the windows decides the answer. */
    build(&g, solid, n, NULL, 0);
    for (int side = 0; side < 2; side++) {
        uint64_t c = side ? n : 0;
        CHECK(indent_on_close_brace(&g.text, c, c, 4, &from) == INDENT_OK && from == c,
              "brace on a 4T solid line at %llu: from %llu", U(c), U(from));
        CHECK(indent_on_enter(&g.text, c, c, out, &len) == INDENT_ERR_LIMIT, "enter on a 4T solid line still limits");
    }
    gtext_fini(&g);
    free(spaces);
    free(solid);
    free(sel);
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
                        check_typing(g, b, n, lo, hi, m, widths, 4, "small");
                        check_trailing(g, b, n, lo, hi, m, "small");
                        for (uint64_t c = 0; c <= n; c++) check_bracket(g, b, c, lo, hi, m, "small");
                        queries += 3 + n;
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

/* ---- typing-bound edges ------------------------------------------------------- */

/* The cursor line is head + fill1 * k1 | selection | fill2 * k2 + tail. The k
 * values put the previous LF, the text start, the line's LF, its CR and the
 * text end one byte inside and one byte outside each typing window (P). */
static void bound_sweep(void)
{
    static const char *heads[] = {"", "\n", "\r\n", "x\n"};
    static const char *tails[] = {"", "\n", "\r\n", "x"};
    static const uint64_t k1s[] = {2, T - 1, T, T + 1};
    static const uint64_t k2s[] = {2, T - 2, T - 1, T, T + 1};
    static const uint64_t sels[] = {0, 1, T + 1};
    static const uint8_t fills[] = {' ', 'x', '\t'};
    static const unsigned widths[] = {3, 4}; /* (P) one width divides T, one does not */
    size_t cases = 0;
    uint8_t *b = malloc(4 * T);
    if (!b) exit(1);
    for (size_t h = 0; h < 4; h++)
    for (size_t i1 = 0; i1 < 4; i1++)
    for (size_t f1 = 0; f1 < 3; f1++)
    for (size_t s = 0; s < 3; s++)
    for (size_t i2 = 0; i2 < 5; i2++)
    for (size_t f2 = 0; f2 < 3; f2++)
    for (size_t t = 0; t < 4; t++) {
        uint64_t n = 0;
        memcpy(b, heads[h], strlen(heads[h]));
        n += strlen(heads[h]);
        memset(b + n, fills[f1], k1s[i1]);
        n += k1s[i1];
        uint64_t lo = n;
        for (uint64_t j = 0; j < sels[s]; j++) b[n++] = j % 2 ? 'x' : '\n';
        uint64_t hi = n;
        memset(b + n, fills[f2], k2s[i2]);
        n += k2s[i2];
        memcpy(b + n, tails[t], strlen(tails[t]));
        n += strlen(tails[t]);

        indent_range w[2];
        typing_windows(n, lo, hi, w);
        uint64_t edges[] = {w[0].lo, w[0].hi, w[1].lo, w[1].hi};
        uint64_t cuts[12];
        size_t k = 0;
        for (size_t e = 0; e < 4; e++) cuts[k++] = edges[e];
        cuts[k++] = (w[0].lo + w[0].hi) / 2;
        cuts[k++] = (w[1].lo + w[1].hi) / 2;
        gtext g;
        build(&g, b, n, cuts, k);
        check_typing(&g, b, n, lo, hi, GUARDED, widths, 2, "bound");
        gtext_fini(&g);
        k = 0;
        for (size_t e = 0; e < 4; e++) {
            if (edges[e] > 0) cuts[k++] = edges[e] - 1;
            cuts[k++] = edges[e] + 1;
        }
        build(&g, b, n, cuts, k);
        check_typing(&g, b, n, lo, hi, STRADDLED, widths, 2, "bound");
        gtext_fini(&g);
        cases++;
    }
    free(b);
    printf("indent_test: bound sweep: %zu cursor lines around INDENT_TYPING_BYTES=%u, guarded and straddled\n",
           cases, INDENT_TYPING_BYTES);
}

/* ---- long bracket windows and the detection prefix ----------------------------- */

static void long_cases(void)
{
    /* Brackets on both sides of the backward copy blocks' edges, which lie at
     * multiples of T below the source (P). */
    uint64_t n = 3 * T + 5;
    uint8_t *b = malloc(n);
    if (!b) exit(1);
    memset(b, 'x', n);
    uint64_t at[] = {0, 1, 2, 4, T - 1, T, T + 1, T + 4, 2 * T, 2 * T + 3, 3 * T, 3 * T + 3, 3 * T + 4};
    const char kinds[] = "{({[{]}}(){})";
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) b[at[i]] = (uint8_t)kinds[i];
    for (int win = 0; win < 2; win++) {
        uint64_t lo = win ? 1 : 0, hi = win ? n - 1 : n;
        uint64_t cuts[] = {lo, hi, T / 2, 2 * T + 1};
        gtext guarded, straddled;
        build(&guarded, b, n, cuts, 4);
        uint64_t scuts[] = {lo + 1, hi - 1, T + 2};
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
    n = INDENT_PREFIX_BYTES + T;
    b = malloc(n);
    if (!b) exit(1);
    memset(b, 'x', n);
    memcpy(b, "  a\n    b\n", 10);
    b[INDENT_PREFIX_BYTES - 8] = '\n';
    memcpy(b + INDENT_PREFIX_BYTES - 7, "\t\t\t\t\t\t\t", 7); /* cut line: would vote tabs */
    b[INDENT_PREFIX_BYTES + 2] = '\n';
    uint64_t cut = INDENT_PREFIX_BYTES;
    gtext g;
    build(&g, b, n, &cut, 1);
    check_detect(&g, b, n, GUARDED, "prefix");
    indent_style st = indent_detect(&g.text);
    CHECK(st.kind == INDENT_SPACES && st.width == 2, "prefix detect %d/%u", st.kind, st.width);
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
    v1_cases();
    limit_cases();
    small_sweep();
    bound_sweep();
    long_cases();
    if (failures) {
        fprintf(stderr, "indent_test: %d FAILURES\n", failures);
        return 1;
    }
    puts("indent_test: all passed");
    return 0;
}
