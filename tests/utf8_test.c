/* utf8: every fast path against an independent slow reference.
 *
 * References live here, not in utf8.c: a bit-pattern decoder, a binary-search
 * width over the tables, a stateless UAX #29 oracle (pairwise rules over left
 * context, its own GCB classifier with no below-U+0300 shortcut) and the
 * cluster-width rule as utf8.h states it. utf8.c's segmenter, classifier and
 * width automaton share code with each other, so only these references can
 * catch a shared mistake (the ©/® misclassification was one: v1 ran its
 * cluster width "slow path" through the same gcb_of as the fast path).
 *
 * Inputs handed to the API under test sit flush against PROT_NONE guard pages
 * (one side or the other), so a read outside [p, p + n) faults in every lane,
 * the gcc -O2 release build included. A positive control proves the guards
 * can fail before any case runs. Run from the repository root (reads
 * vendor/ucd/GraphemeBreakTest.txt). */
#include "utf8_fixture.h"
#include "utf8/tables.h"
#include "os/os.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
/* Stop a loop after its first failure so one bug does not print a million lines. */
#define CHECK_BREAK(c) if (!(c)) { CHECK(c); break; }
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------------------------------------------------- guard slots */

/* Each slot: [PROT_NONE page][RW SLOT_CAP bytes][PROT_NONE page]. put() copies
 * flush against the right guard, put_begin() flush against the left one.
 * SLOT_CAP (P): the largest input placed is t_random's 1 << 16 bytes. */
#define SLOT_CAP ((size_t)1 << 16)
enum { S_ONE, S_AUX, S_VIEW, S_TEXT, SLOTS };   /* transient, second transient, chunk view, subject text */
static struct { uint8_t *mapping, *begin, *end; size_t mapped; } slot[SLOTS];

static bool slots_init(void)
{
    size_t page = os_page_size(), span = (SLOT_CAP + page - 1) / page * page;
    for (int i = 0; i < SLOTS; i++) {
        size_t mapped = span + 2 * page;
        void *m = mmap(NULL, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) return false;
        slot[i].mapping = m;
        slot[i].mapped = mapped;
        slot[i].begin = slot[i].mapping + page;
        slot[i].end = slot[i].begin + span;
        if (mprotect(slot[i].begin, span, PROT_READ | PROT_WRITE) != 0) return false;
    }
    return true;
}

static uint8_t *put(int s, const void *src, size_t n)
{
    if (n > SLOT_CAP) { printf("FAIL put %zu > SLOT_CAP\n", n); exit(1); }
    uint8_t *p = slot[s].end - n;
    if (n) memmove(p, src, n);
    return p;
}

static uint8_t *put_begin(int s, const void *src, size_t n)
{
    if (n > SLOT_CAP) { printf("FAIL put %zu > SLOT_CAP\n", n); exit(1); }
    if (n) memmove(slot[s].begin, src, n);
    return slot[s].begin;
}

enum { PROBE_FAULTED = 3 };
static void on_segv(int sig)
{
    (void)sig;
    _exit(PROBE_FAULTED);
}

/* Exit status of a child that reads *addr: PROBE_FAULTED or 0. */
static int probe(const volatile uint8_t *addr)
{
    fflush(stdout);
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

/* Positive control: begin-1 and end fault, begin and end-1 do not. */
static bool slots_armed(void)
{
    bool armed = true;
    for (int i = 0; i < SLOTS; i++) {
        int below = probe(slot[i].begin - 1), first = probe(slot[i].begin);
        int last = probe(slot[i].end - 1), above = probe(slot[i].end);
        bool ok = below == PROBE_FAULTED && first == 0 && last == 0 && above == PROBE_FAULTED;
        printf("utf8_test: slot %d [%p PROT_NONE][%p RW %zu][%p PROT_NONE]; read begin-1=%s begin=%s end-1=%s end=%s\n",
               i, (void *)slot[i].mapping, (void *)slot[i].begin, (size_t)(slot[i].end - slot[i].begin),
               (void *)slot[i].end, below == PROBE_FAULTED ? "SIGSEGV" : "ok", first == PROBE_FAULTED ? "SIGSEGV" : "ok",
               last == PROBE_FAULTED ? "SIGSEGV" : "ok", above == PROBE_FAULTED ? "SIGSEGV" : "ok");
        armed = armed && ok;
    }
    return armed;
}

static utf8_step dec(const char *s, size_t n) { return utf8_decode(put(S_ONE, s, n), n); }

/* ------------------------------------------------- independent references */

/* Bit patterns + scalar range checks (a different formulation from the
 * Table 3-7 byte ranges in utf8.h). */
static utf8_step ref_decode(const uint8_t *p, size_t n)
{
    utf8_step bad = { p[0], 1, 0 };
    uint8_t b = p[0];
    size_t len;
    uint32_t cp, min;
    if (b < 0x80) { utf8_step s = { b, 1, 1 }; return s; }
    if ((b & 0xE0) == 0xC0) { len = 2; cp = b & 0x1Fu; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0Fu; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07u; min = 0x10000; }
    else return bad;
    if (n < len) return bad;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return bad;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return bad;
    utf8_step s = { cp, (uint8_t)len, 1 };
    return s;
}

/* Plain binary search; no span pre-check, no decision tree, no shortcut. */
static int ref_in(const utf8_range *t, size_t n, uint32_t cp)
{
    for (size_t lo = 0, hi = n; lo < hi;) {
        size_t m = lo + (hi - lo) / 2;
        if (cp < t[m].lo) hi = m;
        else if (cp > t[m].hi) lo = m + 1;
        else return 1;
    }
    return 0;
}
#define RIN(tab, cp) ref_in(tab, NELEM(tab), cp)

static int ref_width(uint32_t cp)
{
    /* (G) Editor presentation policy: skin-tone modifiers have no own cell. */
    if (cp >= 0x1f3fbu && cp <= 0x1f3ffu) return 0;
    if (RIN(UCD_ZERO_WIDTH, cp)) return 0;
    if (RIN(UCD_WIDE, cp)) return 2;
    return 1;
}

static size_t enc_len(uint32_t cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }

/* UAX #29 oracle: break decision between unit i-1 and i by scanning left for
 * context (no running state). Invalid bytes are Control. Same shape as
 * fuzz/utf8_fuzz.c. */
enum { R_OTHER, R_CR, R_LF, R_CTL, R_EXT, R_ZWJ, R_SPC, R_PRE, R_RI, R_L, R_V, R_T, R_LV, R_LVT, R_PICT };
static int ref_class(int valid, uint32_t cp)
{
    if (!valid) return R_CTL;
    if (cp == '\r') return R_CR;
    if (cp == '\n') return R_LF;
    if (RIN(UCD_CONTROL, cp)) return R_CTL;
    if (cp == 0x200D) return R_ZWJ;
    if (RIN(UCD_EXTEND, cp)) return R_EXT;
    if (RIN(UCD_SPACINGMARK, cp)) return R_SPC;
    if (RIN(UCD_PREPEND, cp)) return R_PRE;
    if (RIN(UCD_REGIONAL_INDICATOR, cp)) return R_RI;
    if (RIN(UCD_HANGUL_L, cp)) return R_L;
    if (RIN(UCD_HANGUL_V, cp)) return R_V;
    if (RIN(UCD_HANGUL_T, cp)) return R_T;
    if (RIN(UCD_HANGUL_LV, cp)) return R_LV;
    if (RIN(UCD_HANGUL_LVT, cp)) return R_LVT;
    if (RIN(UCD_EXT_PICT, cp)) return R_PICT;
    return R_OTHER;
}
static int ref_incb(int valid, uint32_t cp)       /* 1 Consonant, 2 Linker, 3 Extend */
{
    if (!valid) return 0;
    if (RIN(UCD_INCB_CONSONANT, cp)) return 1;
    if (RIN(UCD_INCB_LINKER, cp)) return 2;
    if (RIN(UCD_INCB_EXTEND, cp)) return 3;
    return 0;
}
/* c[], ib[]: classes of units 0..m-1. Returns 1 if there is a break before unit i (1 <= i < m). */
static int ref_break(const int *c, const int *ib, int i)
{
    int a = c[i - 1], b = c[i];
    if (a == R_CR && b == R_LF) return 0;
    if (a == R_CR || a == R_LF || a == R_CTL) return 1;
    if (b == R_CR || b == R_LF || b == R_CTL) return 1;
    if (a == R_L && (b == R_L || b == R_V || b == R_LV || b == R_LVT)) return 0;
    if ((a == R_LV || a == R_V) && (b == R_V || b == R_T)) return 0;
    if ((a == R_LVT || a == R_T) && b == R_T) return 0;
    if (b == R_EXT || b == R_ZWJ || b == R_SPC) return 0;
    if (a == R_PRE) return 0;
    if (ib[i] == 1) {                                   /* GB9c */
        int j = i - 1, linker = 0;
        while (j >= 0 && (ib[j] == 2 || ib[j] == 3)) { linker |= ib[j] == 2; j--; }
        if (j >= 0 && ib[j] == 1 && linker) return 0;
    }
    if (a == R_ZWJ && b == R_PICT) {                    /* GB11 */
        int j = i - 2;
        while (j >= 0 && c[j] == R_EXT) j--;
        if (j >= 0 && c[j] == R_PICT) return 0;
    }
    if (a == R_RI && b == R_RI) {                       /* GB12, GB13 */
        int k = 0, j = i - 1;
        while (j >= 0 && c[j] == R_RI) { k++; j--; }
        if (k % 2 == 1) return 0;
    }
    return 1;
}

/* The cluster-width rule exactly as utf8.h states it, over the units of one
 * cluster u[0..m) with their oracle classes c[0..m). */
static int ref_cluster_width(const utf8_step *u, const int *c, int m)
{
    if (m == 0) return 0;
    if (!u[0].valid || c[0] == R_CR || c[0] == R_LF || c[0] == R_CTL) return 1;
    int k = 0;
    while (k < m && ref_width(u[k].cp) == 0) k++;
    if (k == m) return 0;
    int b = c[k], w = ref_width(u[k].cp);
    if (b == R_L || b == R_V || b == R_T || b == R_LV || b == R_LVT) return 2;
    if (w != 1) return w;
    uint32_t base = u[k].cp;
    int kbase = base == '#' || base == '*' || (base >= '0' && base <= '9');
    for (int j = k + 1; j < m; j++) {
        if (b == R_PICT && (u[j].cp == 0xFE0F || (c[j - 1] == R_ZWJ && c[j] == R_PICT))) return 2;
        if (kbase && u[j].cp == 0x20E3) return 2;
    }
    return 1;
}

/* ---------------------------------------------------------------- decode */
#define VALID(s, n, c, l) do { utf8_step t_ = dec(s, n); CHECK(t_.valid == 1 && t_.cp == (c) && t_.len == (l)); } while (0)
#define INV(s) do { utf8_step t_ = dec(s, sizeof(s) - 1); \
    CHECK(t_.valid == 0 && t_.len == 1 && t_.cp == (uint8_t)(s)[0]); } while (0)

static void t_decode_cases(void)
{
    VALID("A", 1, 'A', 1);
    VALID("\x00", 1, 0, 1);
    VALID("\x7F", 1, 0x7F, 1);
    VALID("\xC2\x80", 2, 0x80, 2);
    VALID("\xC3\xA9", 2, 0xE9, 2);
    VALID("\xDF\xBF", 2, 0x7FF, 2);
    VALID("\xE0\xA0\x80", 3, 0x800, 3);
    VALID("\xE2\x82\xAC", 3, 0x20AC, 3);
    VALID("\xED\x9F\xBF", 3, 0xD7FF, 3);
    VALID("\xEE\x80\x80", 3, 0xE000, 3);
    VALID("\xEF\xBF\xBF", 3, 0xFFFF, 3);
    VALID("\xF0\x90\x80\x80", 4, 0x10000, 4);
    VALID("\xF0\x9F\x98\x80", 4, 0x1F600, 4);
    VALID("\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 4);
    VALID("\xC3\xA9\xA9", 3, 0xE9, 2);           /* trailing stray continuation is the next unit */
    INV("\x80"); INV("\xBF");                     /* stray continuations */
    INV("\xC0\x80"); INV("\xC1\xBF");             /* C0/C1 overlong leads */
    INV("\xE0\x80\x80"); INV("\xE0\x9F\xBF");     /* E0 overlong */
    INV("\xED\xA0\x80"); INV("\xED\xBF\xBF");     /* surrogates */
    INV("\xF0\x80\x80\x80"); INV("\xF0\x8F\xBF\xBF"); /* F0 overlong */
    INV("\xF4\x90\x80\x80"); INV("\xF5\x80\x80\x80"); /* > U+10FFFF */
    INV("\xF8\x88\x80\x80\x80"); INV("\xFC\x84\x80\x80\x80\x80"); /* 5/6-byte forms */
    INV("\xFE"); INV("\xFF");
    INV("\xC3"); INV("\xE2\x82"); INV("\xF0\x9F\x98");  /* truncated by n */
    INV("\xC3\x28"); INV("\xE2\x28\xA1"); INV("\xE2\x82\x28"); INV("\xF0\x9F\x28\x80"); INV("\xF0\x9F\x98\x28");
    { utf8_step t = dec("\xC3\xA9", 1); CHECK(!t.valid && t.len == 1 && t.cp == 0xC3); } /* n truncates even if p[1] is fine */
}

/* Every lead x every second byte x boundary values for bytes 3/4, at every
 * n = 1..4, against the reference, flush against the guard. */
static void t_decode_exhaustive(void)
{
    static const uint8_t R[] = { 0x00, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xFF };
    long bad = 0;
    for (unsigned a = 0; a < 256; a++)
        for (unsigned b = 0; b < 256; b++)
            for (unsigned c = 0; c < NELEM(R); c++)
                for (unsigned d = 0; d < NELEM(R); d++) {
                    uint8_t src[4] = { (uint8_t)a, (uint8_t)b, R[c], R[d] };
                    for (size_t n = 1; n <= 4; n++) {
                        utf8_step x = utf8_decode(put(S_ONE, src, n), n), y = ref_decode(src, n);
                        if (x.cp != y.cp || x.len != y.len || x.valid != y.valid) {
                            if (bad++ < 4) printf("decode %02X %02X %02X %02X n=%zu: got {%X,%u,%u} want {%X,%u,%u}\n",
                                                  a, b, R[c], R[d], n, x.cp, x.len, x.valid, y.cp, y.len, y.valid);
                        }
                    }
                }
    CHECK(bad == 0);
    /* Table 3-7 second-byte windows, stated directly. */
    for (unsigned b = 0; b < 256; b++) {
        uint8_t e0[3] = { 0xE0, (uint8_t)b, 0x80 }, ed[3] = { 0xED, (uint8_t)b, 0x80 };
        uint8_t f0[4] = { 0xF0, (uint8_t)b, 0x80, 0x80 }, f4[4] = { 0xF4, (uint8_t)b, 0x80, 0x80 };
        uint8_t e1[3] = { 0xE1, (uint8_t)b, 0x80 }, f1[4] = { 0xF1, (uint8_t)b, 0x80, 0x80 };
        CHECK(utf8_decode(put(S_ONE, e0, 3), 3).valid == (b >= 0xA0 && b <= 0xBF));
        CHECK(utf8_decode(put(S_ONE, ed, 3), 3).valid == (b >= 0x80 && b <= 0x9F));
        CHECK(utf8_decode(put(S_ONE, f0, 4), 4).valid == (b >= 0x90 && b <= 0xBF));
        CHECK(utf8_decode(put(S_ONE, f4, 4), 4).valid == (b >= 0x80 && b <= 0x8F));
        CHECK(utf8_decode(put(S_ONE, e1, 3), 3).valid == (b >= 0x80 && b <= 0xBF));
        CHECK(utf8_decode(put(S_ONE, f1, 4), 4).valid == (b >= 0x80 && b <= 0xBF));
    }
    /* Lead classes: only 00..7F and C2..F4 can start a valid unit. */
    for (unsigned a = 0x80; a < 256; a++) {
        uint8_t src[4] = { (uint8_t)a, 0, 0, 0 };
        int any = 0;
        for (unsigned b1 = 0x80; b1 < 0xC0 && !any; b1++) {
            src[1] = (uint8_t)b1; src[2] = 0x80; src[3] = 0x80;
            any |= utf8_decode(put(S_ONE, src, 4), 4).valid;
        }
        CHECK(any == (a >= 0xC2 && a <= 0xF4));
    }
}

/* ------------------------------------------------- scalars: encode, prev */
static void t_all_scalars(void)
{
    static const char *ctx[] = { "", "a", "\x80", "\xE1\x80", "\xF0\x9F\x98", "\xC3\xA9", "\xFF", "\xF0\x9F\x98\x80" };
    uint8_t buf[16];
    size_t bounds[16];
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        uint8_t b[4];
        size_t l = utf8_encode(cp, b);
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            CHECK_BREAK(l == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
            uint8_t h[3] = { (uint8_t)(0xE0 | (cp >> 12)), (uint8_t)(0x80 | ((cp >> 6) & 63)), (uint8_t)(0x80 | (cp & 63)) };
            CHECK_BREAK(!utf8_decode(put(S_ONE, h, 3), 3).valid);   /* hand-encoded surrogate */
            continue;
        }
        CHECK_BREAK(l == enc_len(cp));
        utf8_step s = utf8_decode(put(S_ONE, b, l), l);
        CHECK_BREAK(s.valid && s.cp == cp && s.len == l);
        CHECK_BREAK(utf8_test_prev(put_begin(S_ONE, b, l), l) == 0);
        /* truncation at every shorter length: invalid single byte */
        int tr_ok = 1;
        for (size_t k = 1; k < l; k++) {
            utf8_step t = utf8_decode(put(S_ONE, b, k), k);
            tr_ok &= !t.valid && t.len == 1 && t.cp == b[0];
        }
        CHECK_BREAK(tr_ok);
        /* forward/prev consistency with valid and invalid left context; prev
         * runs flush against the left guard (reads below base fault) and
         * the right one (reads at or past off fault). */
        int pv_ok = 1;
        for (size_t c = 0; c < NELEM(ctx); c++) {
            size_t cl = strlen(ctx[c]), n = cl + l, nb = 0, off = 0;
            memcpy(buf, ctx[c], cl);
            memcpy(buf + cl, b, l);
            while (off < n) { bounds[nb++] = off; off += utf8_decode(buf + off, n - off).len; }
            pv_ok &= nb >= 1 && bounds[nb - 1] == cl;    /* scalar is its own unit */
            const uint8_t *left = put_begin(S_ONE, buf, n);
            for (size_t k = nb, o = n; k > 0; k--) { o = utf8_test_prev(left, o); pv_ok &= o == bounds[k - 1]; }
            const uint8_t *right = put(S_AUX, buf, n);
            pv_ok &= utf8_test_prev(right, n) == bounds[nb - 1];
        }
        CHECK_BREAK(pv_ok);
    }
    uint8_t b[4];
    CHECK(utf8_encode(0x110000, b) == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
    CHECK(utf8_encode(0xFFFFFFFFu, b) == 3 && b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
    CHECK(utf8_encode(0xD800, b) == 3 && b[0] == 0xEF);
    CHECK(utf8_test_prev(put(S_ONE, "abc", 3), 0) == 0);
    CHECK(utf8_test_prev(put(S_ONE, "\x80\x80\x80\x80\x80", 5), 5) == 4);         /* 4+ continuations */
    CHECK(utf8_test_prev(put(S_ONE, "\xE2\x82\xAC\x80", 4), 4) == 3);            /* valid unit + stray */
    CHECK(utf8_test_prev(put(S_ONE, "\xF0\x9F\x98\x80\x80", 5), 5) == 4);
    CHECK(utf8_test_prev(put(S_ONE, "\xF0\x9F\x98\x80", 4), 4) == 0);
    CHECK(utf8_test_prev(put(S_ONE, "\xE0\x80\x80", 3), 3) == 2);                /* overlong: 3 units */
}

/* ----------------------------------------------------------------- width */
static void t_width(void)
{
    long bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        int w = utf8_test_cell_width(cp), r = ref_width(cp);
        if (w != r && bad++ < 4) printf("width U+%04X: got %d want %d\n", cp, w, r);
    }
    CHECK(bad == 0);
    CHECK(utf8_test_cell_width(0x110000) == 1 && utf8_test_cell_width(0xFFFFFFFFu) == 1);
    /* Spot checks, each re-derived from tables.h. */
    CHECK(utf8_test_cell_width('a') == 1);
    CHECK(utf8_test_cell_width(0x07) == 1);       /* controls: 1, drawn as placeholders */
    CHECK(utf8_test_cell_width(0x1B) == 1);
    CHECK(utf8_test_cell_width(0x7F) == 1);
    CHECK(utf8_test_cell_width(0xA9) == 1);       /* ©: ExtPict, not Emoji_Presentation */
    CHECK(utf8_test_cell_width(0xAE) == 1);       /* ® likewise */
    CHECK(utf8_test_cell_width(0xE9) == 1);
    CHECK(utf8_test_cell_width(0x00AD) == 0);     /* soft hyphen is Cf */
    CHECK(utf8_test_cell_width(0x0301) == 0);
    CHECK(utf8_test_cell_width(0x0600) == 0);     /* Arabic number sign (Cf, Prepend) */
    CHECK(utf8_test_cell_width(0x0915) == 1);
    CHECK(utf8_test_cell_width(0x093E) == 0);     /* Mc is zero-width in these tables */
    CHECK(utf8_test_cell_width(0x0E01) == 1);
    CHECK(utf8_test_cell_width(0x0E31) == 0);
    CHECK(utf8_test_cell_width(0x115F) == 0);     /* Hangul choseong filler: DI wins over W */
    CHECK(utf8_test_cell_width(0x1161) == 1);     /* medial jamo: EAW N */
    CHECK(utf8_test_cell_width(0x200B) == 0);
    CHECK(utf8_test_cell_width(0x200D) == 0);
    CHECK(utf8_test_cell_width(0x2603) == 1);     /* snowman: text presentation */
    CHECK(utf8_test_cell_width(0x2FFC) == 2);     /* new in 15.1 */
    CHECK(utf8_test_cell_width(0x3000) == 2);
    CHECK(utf8_test_cell_width(0x3042) == 2);
    CHECK(utf8_test_cell_width(0x3099) == 0);     /* Mn and W: zero wins */
    CHECK(utf8_test_cell_width(0x302A) == 0);
    CHECK(utf8_test_cell_width(0x303F) == 1);
    CHECK(utf8_test_cell_width(0x31EF) == 2);     /* new in 15.1 */
    CHECK(utf8_test_cell_width(0x3164) == 0);     /* Hangul filler: DI */
    CHECK(utf8_test_cell_width(0x4DC0) == 1);
    CHECK(utf8_test_cell_width(0x4E2D) == 2);
    CHECK(utf8_test_cell_width(0xAC00) == 2);
    CHECK(utf8_test_cell_width(0xFE0F) == 0);
    CHECK(utf8_test_cell_width(0xFF21) == 2);
    CHECK(utf8_test_cell_width(0x1AFF4) == 1);    /* unassigned gap in Kana Ext-B */
    CHECK(utf8_test_cell_width(0x1F1E6) == 2);    /* regional indicator: Emoji_Presentation */
    for (uint32_t cp = 0x1F3FB; cp <= 0x1F3FF; cp++) CHECK(utf8_test_cell_width(cp) == 0);
    CHECK(utf8_test_cell_width(0x1F600) == 2);
    CHECK(utf8_test_cell_width(0x20000) == 2);
    CHECK(utf8_test_cell_width(0x2FFFE) == 1);
    CHECK(utf8_test_cell_width(0x2FFFD) == 2);        /* unassigned, EAW=W: tables, not a blanket 1 */
    CHECK(utf8_test_cell_width(0xE01F0) == 0);        /* unassigned, Default_Ignorable */
    CHECK(utf8_test_cell_width(0x3FFFD) == 2);
    CHECK(utf8_test_cell_width(0xE0001) == 0);
    CHECK(utf8_test_cell_width(0xE0100) == 0);
}

/* -------------------------------------------------------------- grapheme */
static size_t G(const char *s, size_t n) { return first_grapheme(put(S_ONE, s, n), n); }
#define GS(s) G(s, sizeof(s) - 1)

static void t_grapheme(void)
{
    CHECK(G("", 0) == 0);
    CHECK(GS("\xE2\x80\x8B\xCC\x81") == 3);                 /* GB4: ZWSP (Cf, Control) / Extend */
    CHECK(GS("\xD8\x80\xE2\x80\x8B") == 2);                 /* GB5: Prepend / ZWSP */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 9);  /* GB9c: KA VIRAMA KA */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8D\xE0\xA4\x95") == 12); /* KA VIRAMA ZWJ KA */
    CHECK(GS("\xE0\xA4\x95\xE0\xA4\x95") == 3);           /* no linker: break */
    CHECK(GS("a\xE0\xA5\x8D\xE0\xA4\x95") == 4);           /* linker without Consonant start: break */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D" "a") == 6);        /* virama extends, then break */
    CHECK(GS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 15); /* chained */
    CHECK(GS("\xE2\x80\x8D\xE0\xA5\x8D\xE0\xA4\xA4") == 6);   /* cluster starting at ZWJ never opens a conjunct (fuzz find) */
    CHECK(GS("\xE0\xA5\x8D\xE0\xA4\x95") == 3);
    CHECK(GS("a") == 1);
    CHECK(GS("ab") == 1);
    CHECK(GS("e\xCC\x81x") == 3);                    /* e + combining acute */
    CHECK(GS("\xC3\xA9x") == 2);                     /* precomposed */
    CHECK(GS("e\xCC\x81\xCC\xA3x") == 5);            /* two marks */
    CHECK(GS("e\xCC") == 1);                         /* truncated mark is an invalid unit */
    CHECK(GS("e\xFF") == 1);
    CHECK(GS("\xFF" "a") == 1);
    CHECK(GS("\xFF\x80") == 1);
    CHECK(GS("\xFF\xCC\x81") == 1);                  /* invalid byte never takes extenders */
    CHECK(GS("\r\n") == 2);                          /* GB3 */
    CHECK(GS("\r\nx") == 2);
    CHECK(GS("\n\r") == 1);
    CHECK(GS("\ra") == 1);
    CHECK(GS("a\r") == 1);                           /* GB5 */
    CHECK(GS("\x01\xCC\x81") == 1);                  /* GB4: control takes no marks */
    CHECK(GS("\n\xCC\x81") == 1);
    CHECK(GS("a\xE2\x80\xA8") == 1);                 /* U+2028 is Control */
    CHECK(GS("a\xE2\x80\x8D") == 4);                 /* GB9: ZWJ extends any base */
    CHECK(GS("a\xE2\x80\x8D\xF0\x9F\x98\x80") == 4); /* GB11 needs ExtPict before ZWJ */
    CHECK(GS("a\xE2\x80\x8C") == 4);                 /* ZWNJ is Extend */
    CHECK(GS("\xE0\xA4\x95\xE0\xA4\xBE" "x") == 6);  /* GB9a: KA + AA (SpacingMark) */
    CHECK(GS("\xD8\x80\xD8\xA8x") == 4);             /* GB9b: Prepend U+0600 + BEH */
    CHECK(GS("\xD8\x80\n") == 2);                    /* GB5 beats GB9b */
    /* Hangul GB6-8 */
    CHECK(GS("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8x") == 9); /* L V T */
    CHECK(GS("\xE1\x84\x80\xE1\x84\x80") == 6);              /* L L */
    CHECK(GS("\xEA\xB0\x80\xE1\x86\xA8x") == 6);             /* LV T */
    CHECK(GS("\xEA\xB0\x80\xE1\x85\xA1x") == 6);             /* LV V */
    CHECK(GS("\xEA\xB0\x81\xE1\x86\xA8x") == 6);             /* LVT T */
    CHECK(GS("\xEA\xB0\x81\xE1\x85\xA1") == 3);              /* LVT / V */
    CHECK(GS("\xE1\x86\xA8\xE1\x85\xA1") == 3);              /* T / V */
    CHECK(GS("\xEA\xB0\x80\xEA\xB0\x80") == 3);              /* LV / LV */
    /* emoji */
    {
        const char fam[] = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7x";
        CHECK(GS(fam) == sizeof fam - 2);            /* man ZWJ woman ZWJ girl */
    }
    CHECK(GS("\xE2\x9D\xA4\xEF\xB8\x8Fx") == 6);     /* heart + VS16 */
    CHECK(GS("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBDx") == 8);  /* thumbs up + skin tone (Extend) */
    CHECK(GS("\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88x") == 14); /* rainbow flag */
    CHECK(GS("\xF0\x9F\x8F\x83\xE2\x80\x8D\xE2\x99\x80\xEF\xB8\x8Fx") == 13);     /* runner ZWJ female sign VS16 */
    CHECK(GS("\xF0\x9F\x98\x80\xE2\x80\x8D") == 7);  /* dangling ZWJ */
    CHECK(GS("\xF0\x9F\x98\x80\xF0\x9F\x98\x80") == 4);   /* no ZWJ: break */
    /* regional indicators GB12/13 */
    CHECK(GS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7") == 8);
    CHECK(GS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB") == 8);
    CHECK(GS("\xF0\x9F\x87\xBA\xCC\x81\xF0\x9F\x87\xB8") == 6);  /* mark ends the RI run */
    CHECK(GS("\xF0\x9F\x87\xBA") == 4);
    /* ZWSP is Control in GCB */
    CHECK(GS("a\xE2\x80\x8B") == 1);
}

static size_t seg_chunked(const uint8_t *src, size_t n, size_t split, size_t budget, size_t *ends);

/* Full byte-boundary vectors from the unmodified Unicode 15.1 conformance file
 * (vendor/ucd/GraphemeBreakTest.txt, 1187 cases). Runs from the repo root. */
static void t_grapheme_conformance(void)
{
    FILE *f = fopen("vendor/ucd/GraphemeBreakTest.txt", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[4096];
    size_t cases = 0, bad = 0, lineno = 0, chunk_bad = 0, copyright = 0;
    uint8_t bytes[1024];
    size_t ends[1025];
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        uint8_t want[1025] = {0}, got[1025] = {0};
        size_t n = 0;
        int boundary = 0, have = 0;
        for (char *tok = strtok(line, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (strcmp(tok, "\xC3\xB7") == 0) { boundary = 1; if (!have) want[0] = 1; else want[n] = 1; }
            else if (strcmp(tok, "\xC3\x97") == 0) boundary = 0;
            else {
                char *end;
                unsigned long cp = strtoul(tok, &end, 16);
                CHECK(*end == '\0' && cp <= 0x10FFFF && n + 4 <= sizeof bytes);
                if (*end || cp > 0x10FFFF || n + 4 > sizeof bytes) break;
                copyright += cp == 0xA9 || cp == 0xAE;
                want[n] = (uint8_t)boundary;
                n += utf8_encode((uint32_t)cp, bytes + n);
                have = 1;
            }
        }
        if (!have) continue;
        cases++;
        want[n] = 1;                                    /* GB2: break at end */
        want[0] = 1;                                    /* GB1 */
        const uint8_t *exact = put(S_TEXT, bytes, n);
        got[0] = 1;
        for (size_t off = 0; off < n;) {
            size_t step = first_grapheme(exact + off, n - off);
            CHECK(step > 0 && step <= n - off);
            if (!step || step > n - off) break;
            off += step;
            got[off] = 1;
        }
        {   /* resumable API, every split and several budgets: identical boundaries */
            static const size_t budgets[] = { 0, 2, 4096 };
            for (size_t k = 0; k < NELEM(budgets); k++)
                for (size_t split = 0; split <= n; split++) {
                    uint8_t gs[1025] = {0};
                    size_t nc = seg_chunked(bytes, n, split, budgets[k], ends);
                    gs[0] = 1;
                    for (size_t j = 0; j < nc; j++) gs[ends[j]] = 1;
                    if (memcmp(want, gs, n + 1)) { chunk_bad++; break; }
                }
        }
        if (memcmp(want, got, n + 1)) {
            if (bad < 8) printf("GraphemeBreakTest boundary mismatch at line %zu\n", lineno);
            bad++;
        }
    }
    CHECK(!ferror(f));
    fclose(f);
    CHECK(cases == 1187);
    CHECK(bad == 0 && chunk_bad == 0);
    printf("GraphemeBreakTest: %zu cases, %zu mismatches, %zu chunked mismatches; "
           "%zu scalars are U+00A9/U+00AE (the corpus does not exercise them)\n", cases, bad, chunk_bad, copyright);
}

/* ------------------------------------------------- resumable step */

/* Segment src[0..n) with the step API. The first call sees only src[0..split)
 * (eof = 0) and each later call sees everything (or, after MORE, the rest), so
 * a split inside a multi-byte unit must come back as MORE, never as a boundary.
 * Each call gets a guarded copy of exactly what is available. */
static size_t seg_chunked(const uint8_t *src, size_t n, size_t split, size_t budget, size_t *ends)
{
    utf8_gseg g;
    utf8_gseg_init(&g);
    size_t pos = 0, avail = split < n ? split : n, nc = 0, guard = 0;
    while (pos < n && guard++ < 4 * n + 16) {
        size_t m = avail - pos, used = 99;
        const uint8_t *v = put(S_VIEW, src + pos, m);
        int eof = avail == n;
        int r = utf8_grapheme_step(&g, v, m, budget, eof, &used);
        CHECK(used <= m);
        if (used > m) break;
        pos += used;
        if (r == UTF8_G_END) {
            ends[nc++] = pos;
        } else if (r == UTF8_G_MORE) {
            CHECK(!eof);
            avail = n;
        } else {
            CHECK(r == UTF8_G_BUDGET && used >= 1 && used <= (budget ? budget : 1) + 3);
        }
    }
    CHECK(pos == n);
    return nc;
}

static size_t seg_oneshot(const uint8_t *src, size_t n, size_t *ends)
{
    const uint8_t *p = put(S_AUX, src, n);
    size_t nc = 0;
    for (size_t off = 0; off < n;) {
        off += first_grapheme(p + off, n - off);
        ends[nc++] = off;
    }
    return nc;
}

/* Every split position x several budgets must give the one-shot boundaries. */
static int chunked_equals(const uint8_t *src, size_t n)
{
    size_t *a = malloc((n + 1) * sizeof *a), *b = malloc((n + 1) * sizeof *b);
    size_t na = seg_oneshot(src, n, a);
    static const size_t budgets[] = { 0, 1, 2, 3, 5, 4096 };
    int ok = 1;
    for (size_t k = 0; k < NELEM(budgets) && ok; k++)
        for (size_t split = 0; split <= n && ok; split++) {
            size_t nb = seg_chunked(src, n, split, budgets[k], b);
            ok = nb == na && !memcmp(a, b, na * sizeof *a);
        }
    free(a);
    free(b);
    return ok;
}

static uint8_t *chain(const char *head, const char *unit, size_t reps, size_t *n)
{
    size_t hl = strlen(head), ul = strlen(unit);
    uint8_t *b = malloc(hl + ul * reps + 8);
    memcpy(b, head, hl);
    for (size_t i = 0; i < reps; i++) memcpy(b + hl + i * ul, unit, ul);
    *n = hl + ul * reps;
    return b;
}

/* Drive step over a guarded copy: returns calls, total in *len. */
static size_t drive_budget(const uint8_t *src, size_t n, size_t budget, size_t *len, size_t *first_used, int *first_r)
{
    const uint8_t *b = put(S_TEXT, src, n);
    utf8_gseg g;
    utf8_gseg_init(&g);
    size_t pos = 0, calls = 0;
    int r;
    do {
        size_t used = 0;
        r = utf8_grapheme_step(&g, b + pos, n - pos, budget, 1, &used);
        if (!calls) { *first_used = used; *first_r = r; }
        pos += used;
        calls++;
    } while (r == UTF8_G_BUDGET && calls < n + 2);
    CHECK(r == UTF8_G_END);
    *len = pos;
    return calls;
}

static void t_step(void)
{
    size_t used = 7, n;
    utf8_gseg g;
    utf8_gseg_init(&g);
    CHECK(utf8_grapheme_step(&g, put(S_ONE, "", 0), 0, 10, 1, &used) == UTF8_G_END && used == 0);
    utf8_gseg_init(&g);
    CHECK(utf8_grapheme_step(&g, put(S_ONE, "", 0), 0, 10, 0, &used) == UTF8_G_MORE && used == 0);
    /* a truncated tail is never a boundary: 'a' + first byte of U+0301 */
    {
        const uint8_t *t = put(S_ONE, "a\xCC", 2);
        utf8_gseg_init(&g);
        CHECK(utf8_grapheme_step(&g, t, 2, 100, 0, &used) == UTF8_G_MORE && used == 1);
        const uint8_t *u = put(S_AUX, "\xCC\x81", 2);
        CHECK(utf8_grapheme_step(&g, u, 2, 100, 1, &used) == UTF8_G_END && used == 2);
        utf8_gseg_init(&g);                           /* at eof the same tail is an invalid byte: break before it */
        CHECK(utf8_grapheme_step(&g, put(S_ONE, "a\xCC", 2), 2, 100, 1, &used) == UTF8_G_END && used == 1);
    }
    {   /* fresh invalid byte: own cluster; E0 80 is invalid even without eof (not a prefix) */
        const uint8_t *t = put(S_ONE, "\xE0\x80", 2);
        utf8_gseg_init(&g);
        CHECK(utf8_grapheme_step(&g, t, 2, 100, 0, &used) == UTF8_G_END && used == 1);
        const uint8_t *e = put(S_AUX, "\xE2\x82", 2);
        utf8_gseg_init(&g);
        CHECK(utf8_grapheme_step(&g, e, 2, 100, 0, &used) == UTF8_G_MORE && used == 0);
    }
    /* budget stops a 49,153-byte cluster early, and the pieces add up */
    {
        uint8_t *b = chain("a", "\xCC\x81", 24576, &n);
        CHECK(n == 49153 && first_grapheme(put(S_TEXT, b, n), n) == n);
        size_t len, fu; int fr;
        size_t calls = drive_budget(b, n, UTF8_GRAPHEME_BUDGET, &len, &fu, &fr);
        /* (P) the first call takes 'a' + 2048 marks = 4097 B (the unit that
         * crosses the budget is taken whole), each later call 2048 marks =
         * 4096 B: 4097 + 11 * 4096 = 49153, so exactly 12 calls. */
        CHECK(fr == UTF8_G_BUDGET && fu == UTF8_GRAPHEME_BUDGET + 1);
        CHECK(len == n && calls == 12);
        calls = drive_budget(b, n, 1, &len, &fu, &fr);
        CHECK(len == n && fu == 1 && fr == UTF8_G_BUDGET);
        free(b);
    }
    {   /* Prepend chain then a letter; ZWJ emoji chain; © ZWJ chain */
        size_t len, fu; int fr;
        uint8_t *b = chain("", "\xD8\x80", 20000, &n);
        b[n++] = 'a';
        CHECK(first_grapheme(put(S_TEXT, b, n), n) == n);
        CHECK(drive_budget(b, n, UTF8_GRAPHEME_BUDGET, &len, &fu, &fr) > 1 && len == n && fr == UTF8_G_BUDGET);
        free(b);
        b = chain("\xF0\x9F\x91\xA8", "\xE2\x80\x8D\xF0\x9F\x91\xA9", 6000, &n);
        CHECK(first_grapheme(put(S_TEXT, b, n), n) == n);
        CHECK(drive_budget(b, n, UTF8_GRAPHEME_BUDGET, &len, &fu, &fr) > 1 && len == n && fr == UTF8_G_BUDGET);
        free(b);
    }
    /* chunked == unchunked for hand-made tricky sequences */
    {
        static const char *cases[] = {
            "a\xCC\x81" "b", "\r\n\r", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
            "\xF0\x9F\x87\xA6\xF0\x9F\x87\xA7\xF0\x9F\x87\xA8", "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95",
            "\xD8\x80\xD8\x80" "a" "\xE1\x84\x80\xE1\x85\xA1", "a\xE2\x82", "\x80\xCC\x81\xFF", "\xF0\x9F\x98",
        };
        for (size_t i = 0; i < NELEM(cases); i++)
            CHECK(chunked_equals((const uint8_t *)cases[i], strlen(cases[i])));
    }
}

/* ---------------------------------------------- ©/® (UCD_EXT_PICT below U+0300) */

/* Red test (a): every Extended_Pictographic code point classifies as PICT,
 * and nothing else does; RI likewise. Exhaustive over the code space. */
static void t_pict_audit(void)
{
    long bad = 0;
    size_t listed = 0;
    for (size_t i = 0; i < NELEM(UCD_EXT_PICT); i++)
        for (uint32_t cp = UCD_EXT_PICT[i].lo; cp <= UCD_EXT_PICT[i].hi; cp++) {
            listed++;
            if (utf8_emoji_class(cp) != UTF8_EMOJI_PICT && bad++ < 8)
                printf("pict audit: U+%04X in UCD_EXT_PICT[%zu] has emoji class %d\n", cp, i, utf8_emoji_class(cp));
        }
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        int want = RIN(UCD_EXT_PICT, cp) ? UTF8_EMOJI_PICT : RIN(UCD_REGIONAL_INDICATOR, cp) ? UTF8_EMOJI_RI : 0;
        if (utf8_emoji_class(cp) != want && bad++ < 8)
            printf("emoji class U+%04X: got %d want %d\n", cp, utf8_emoji_class(cp), want);
    }
    CHECK(bad == 0);
    printf("pict audit: %zu UCD_EXT_PICT code points, %ld misclassified\n", listed, bad);
}

/* Red test (b): GB11 with © or ® on either side of the ZWJ, one-shot, every
 * split and budget, and backward. U+1F600 is the partner ExtPict. */
static void t_gb11_copyright(void)
{
    static const struct { const char *s; size_t first; } cases[] = {
        { "\xC2\xA9\xE2\x80\x8D\xF0\x9F\x98\x80", 9 },             /* © ZWJ 😀 */
        { "\xC2\xAE\xE2\x80\x8D\xF0\x9F\x98\x80", 9 },             /* ® ZWJ 😀 */
        { "\xF0\x9F\x98\x80\xE2\x80\x8D\xC2\xA9", 9 },             /* 😀 ZWJ © */
        { "\xF0\x9F\x98\x80\xE2\x80\x8D\xC2\xAE", 9 },             /* 😀 ZWJ ® */
        { "\xC2\xA9\xE2\x80\x8D\xC2\xAE" "x", 7 },                 /* © ZWJ ® | x */
        { "\xC2\xA9\xEF\xB8\x8F\xE2\x80\x8D\xC2\xA9", 10 },        /* © VS16 ZWJ © (Extend keeps GB11 open) */
        { "\xC2\xA9\xCC\x81\xE2\x80\x8D\xF0\x9F\x98\x80", 11 },    /* © U+0301 ZWJ 😀 */
        { "a\xE2\x80\x8D\xC2\xA9", 4 },                            /* a ZWJ | ©: GB11 needs ExtPict before ZWJ */
        { "\xC2\xA9\xC2\xA9", 2 },                                 /* © | ©: no ZWJ, GB999 */
        { "\xC2\xA9\xE2\x80\x8D" "a", 5 },                         /* © ZWJ | a */
    };
    for (size_t i = 0; i < NELEM(cases); i++) {
        size_t n = strlen(cases[i].s);
        const uint8_t *p = put(S_TEXT, cases[i].s, n);
        size_t g = first_grapheme(p, n);
        if (g != cases[i].first) printf("gb11 case %zu: first cluster %zu bytes, want %zu\n", i, g, cases[i].first);
        CHECK(g == cases[i].first);
        CHECK(utf8_grapheme_prev(p, cases[i].first) == 0);
        CHECK(utf8_grapheme_prev(put_begin(S_AUX, cases[i].s, cases[i].first), cases[i].first) == 0);
        CHECK(chunked_equals((const uint8_t *)cases[i].s, n));
    }
}

/* Red test (c): © and ® take one cell alone and two with VS16; VS15 does not
 * widen; both directions, so moving them into UCD_WIDE would fail too. */
static int cw(const char *s, size_t n) { return first_cluster_width(put(S_ONE, s, n), n); }
#define CWS(s) cw(s, sizeof(s) - 1)
static int cluster_step_ok(const uint8_t *src, size_t n, size_t budget, size_t chunk);

static void t_copyright_width(void)
{
    static const struct { const char *s; int w; } cases[] = {
        { "\xC2\xA9", 1 }, { "\xC2\xAE", 1 },                                     /* bare: text presentation */
        { "\xC2\xA9\xEF\xB8\x8F", 2 }, { "\xC2\xAE\xEF\xB8\x8F", 2 },             /* + VS16: emoji presentation */
        { "\xC2\xA9\xEF\xB8\x8E", 1 }, { "\xC2\xAE\xEF\xB8\x8E", 1 },             /* + VS15 */
        { "\xC2\xA9\xE2\x80\x8D\xF0\x9F\x98\x80", 2 },                            /* © ZWJ 😀 */
        { "\xC2\xA9\xE2\x80\x8D\xC2\xAE", 2 },                                    /* © ZWJ ®: width-1 ExtPict ZWJ ExtPict */
        { "\xC2\xA9\xCC\x81", 1 },                                                /* © + mark */
    };
    for (size_t i = 0; i < NELEM(cases); i++) {
        size_t n = strlen(cases[i].s);
        int w = cw(cases[i].s, n);
        if (w != cases[i].w) printf("copyright width case %zu: %d want %d\n", i, w, cases[i].w);
        CHECK(w == cases[i].w);
        CHECK(utf8_cluster_emoji(put(S_ONE, cases[i].s, n), n) == UTF8_EMOJI_PICT);
        static const size_t budgets[] = { 0, 1, 2, 4096 };
        for (size_t k = 0; k < NELEM(budgets); k++)
            for (size_t chunk = 1; chunk <= n + 1; chunk++)
                CHECK_BREAK(cluster_step_ok((const uint8_t *)cases[i].s, n, budgets[k], chunk));
    }
    /* Artwork: © is the base, VS16 is dropped, a ZWJ ends the projection. */
    uint8_t out[16];
    const uint8_t *p = put(S_ONE, "\xC2\xA9\xEF\xB8\x8F", 5);
    CHECK(utf8_cluster_artwork(p, 5, out) == 2 && out[0] == 0xC2 && out[1] == 0xA9);
    p = put(S_ONE, "\xC2\xAE\xE2\x80\x8D\xF0\x9F\x98\x80", 9);
    CHECK(utf8_cluster_artwork(p, 9, out) == 2 && out[0] == 0xC2 && out[1] == 0xAE);
}

/* Every code point C: segment C followed by VS16 and check length and width
 * against ref_cluster_width, through first_cluster and through
 * utf8_cluster_step fed C first and VS16 after a refill. */
static void t_vs16_every_cp(void)
{
    long bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF && bad < 8; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        uint8_t b[8];
        size_t l = utf8_encode(cp, b);
        memcpy(b + l, "\xEF\xB8\x8F", 3);
        size_t n = l + 3;
        utf8_step u[2] = { { cp, (uint8_t)l, 1 }, { 0xFE0F, 3, 1 } };
        int c[2] = { ref_class(1, cp), R_EXT }, ib[2] = { ref_incb(1, cp), ref_incb(1, 0xFE0F) };
        int m = ref_break(c, ib, 1) ? 1 : 2;
        size_t want_len = m == 1 ? l : n;
        int want_w = ref_cluster_width(u, c, m), w = -1;
        size_t len = first_cluster(put(S_ONE, b, n), n, &w);
        utf8_cseg cs;
        utf8_cseg_init(&cs);
        size_t used = 0, pos = 0;
        int sw = -1, r = utf8_cluster_step(&cs, put(S_AUX, b, l), l, UTF8_GRAPHEME_BUDGET, 0, &used, &sw);
        pos += used;
        if (r != UTF8_G_END) {
            r = utf8_cluster_step(&cs, put(S_AUX, b + pos, n - pos), n - pos, UTF8_GRAPHEME_BUDGET, 1, &used, &sw);
            pos += used;
        }
        if (len != want_len || w != want_w || r != UTF8_G_END || pos != want_len || sw != want_w) {
            bad++;
            printf("VS16 U+%04X: cluster %zu/%d step %zu/%d want %zu/%d\n", cp, len, w, pos, sw, want_len, want_w);
        }
    }
    CHECK(bad == 0);
}

/* Every code point C: the GCB class utf8.c assigns, observed through
 * segmentation of probe sequences around C, against the oracle. A probe's
 * outcome is a mask: bit j set iff there is a break before unit j. The test
 * first proves the probes separate every pair of GCB classes under every InCB
 * value (so a classifier that puts any code point in the wrong class changes
 * some probe's mask), keeps a greedy subset that still separates them all,
 * and runs that subset for every code point. */
/* PX marks C in a probe, PEND ends it; (P) PMAXU = the longest probe (5 units). */
enum { PX = -1, PEND = -2, PMAXU = 5, NCLASS = R_PICT + 1, NINCB = 4 };
static const int64_t probes[][PMAXU] = {
    { PX, 'a', PEND }, { 'a', PX, PEND }, { PX, 0x301, PEND }, { PX, '\n', PEND }, { '\r', PX, PEND },
    { 0x1F600, 0x200D, PX, PEND }, { 0x1F600, PX, 0x200D, 0x1F600, PEND }, { 0x1F600, PX, 0x1F600, PEND },
    { PX, PX, PX, PEND }, { 0x1F1E6, PX, PEND }, { 0x1100, PX, PEND }, { PX, 0x1161, PEND }, { PX, 0x11A8, PEND },
    { 0x915, 0x94D, PX, PEND }, { PX, 0x94D, 0x915, PEND }, { 0x915, PX, 0x915, PEND }, { 0x915, PX, 0x94D, 0x915, PEND },
    { 0x600, PX, PEND },
};
enum { NPROBE = NELEM(probes) };

static unsigned probe_oracle(size_t pi, int xc, int xi)
{
    int c[PMAXU], ib[PMAXU], m = 0;
    for (; m < PMAXU && probes[pi][m] != PEND; m++) {
        int x = probes[pi][m] == PX;
        c[m] = x ? xc : ref_class(1, (uint32_t)probes[pi][m]);
        ib[m] = x ? xi : ref_incb(1, (uint32_t)probes[pi][m]);
    }
    unsigned mask = 0;
    for (int j = 1; j < m; j++)
        if (ref_break(c, ib, j)) mask |= 1u << j;
    return mask;
}

static unsigned probe_utf8(size_t pi, uint32_t cp)
{
    uint8_t b[PMAXU * 4];
    size_t pos[PMAXU + 1], n = 0;
    int m = 0;
    for (; m < PMAXU && probes[pi][m] != PEND; m++) {
        pos[m] = n;
        n += utf8_encode(probes[pi][m] == PX ? cp : (uint32_t)probes[pi][m], b + n);
    }
    pos[m] = n;
    unsigned mask = 0;
    for (size_t off = first_grapheme(b, n); off < n; off += first_grapheme(b + off, n - off)) {
        int j = 1;
        while (j < m && pos[j] < off) j++;
        if (j == m || pos[j] != off) return ~0u;        /* not a unit boundary */
        mask |= 1u << j;
    }
    return mask;
}

static void t_class_audit(void)
{
    static unsigned want[NCLASS][NINCB][NPROBE];
    for (int xc = 0; xc < NCLASS; xc++)
        for (int xi = 0; xi < NINCB; xi++)
            for (size_t pi = 0; pi < NPROBE; pi++) want[xc][xi][pi] = probe_oracle(pi, xc, xi);
    /* Pairs the probes must tell apart: two classes under one InCB value (a
     * wrong GCB class), and two InCB values of a class whose InCB the rules
     * read (Other: Consonant or not; Extend: none, Linker or Extend). */
    enum { MAXPAIR = NCLASS * NCLASS * NINCB };
    static int pa[MAXPAIR][2], pb[MAXPAIR][2];
    static bool done[MAXPAIR];
    int np = 0;
    for (int x = 0; x < NCLASS; x++) for (int y = x + 1; y < NCLASS; y++) for (int i = 0; i < NINCB; i++) {
        pa[np][0] = x; pa[np][1] = i; pb[np][0] = y; pb[np][1] = i; np++;
    }
    static const int incb_pairs[][3] = { { R_OTHER, 0, 1 }, { R_EXT, 0, 2 }, { R_EXT, 0, 3 }, { R_EXT, 2, 3 } };
    for (size_t k = 0; k < NELEM(incb_pairs); k++) {
        pa[np][0] = incb_pairs[k][0]; pa[np][1] = incb_pairs[k][1];
        pb[np][0] = incb_pairs[k][0]; pb[np][1] = incb_pairs[k][2]; np++;
    }
#define DIFFERS(q, pi) (want[pa[q][0]][pa[q][1]][pi] != want[pb[q][0]][pb[q][1]][pi])
    size_t use[NPROBE], nuse = 0;
    for (;;) {                                          /* greedy cover */
        size_t best = NPROBE, best_n = 0;
        for (size_t pi = 0; pi < NPROBE; pi++) {
            size_t k = 0;
            for (int q = 0; q < np; q++) k += !done[q] && DIFFERS(q, pi);
            if (k > best_n) { best = pi; best_n = k; }
        }
        if (best == NPROBE) break;
        use[nuse++] = best;
        for (int q = 0; q < np; q++) done[q] |= DIFFERS(q, best);
    }
#undef DIFFERS
    int alike = 0;
    for (int q = 0; q < np; q++)
        if (!done[q] && alike++ < 4)
            printf("class audit: no probe tells class %d/InCB %d from class %d/InCB %d\n", pa[q][0], pa[q][1], pb[q][0], pb[q][1]);
    CHECK(alike == 0);
    long bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF && bad < 8; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        int xc = ref_class(1, cp), xi = ref_incb(1, cp);
        for (size_t k = 0; k < nuse; k++) {
            unsigned got = probe_utf8(use[k], cp);
            if (got != want[xc][xi][use[k]]) {
                bad++;
                printf("class audit U+%04X (oracle class %d incb %d): probe %zu breaks %#x, oracle %#x\n",
                       cp, xc, xi, use[k], got, want[xc][xi][use[k]]);
                break;
            }
        }
    }
    CHECK(bad == 0);
    printf("class audit: every code point, %zu of %d probes separate all %d class and InCB pairs\n", nuse, (int)NPROBE, np);
}

/* ---------------------------------------------- grapheme_prev, cluster width */
static void t_grapheme_prev_cases(void)
{
    /* Flush right (reads at or past off fault) and flush left (reads below base fault). */
    static const struct { const char *s; size_t n, off, want; } c[] = {
        { "", 0, 0, 0 }, { "ab", 2, 2, 1 }, { "e\xCC\x81", 3, 3, 0 }, { "e\xCC\x81", 3, 1, 0 },
        { "\r\n", 2, 2, 0 }, { "\n\n", 2, 2, 1 },
        { "a\xE2\x80\x8B\xCC\x81", 6, 6, 4 }, { "a\xE2\x80\x8B\xCC\x81", 6, 4, 1 },       /* ZWSP / Extend */
        { "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95", 9, 9, 0 },                              /* GB9c: one cluster */
        { "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB", 12, 12, 8 },                /* RI RI | RI */
        { "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB", 12, 8, 0 },
        { "x\xFF\xCC\x81", 4, 4, 2 },                                                     /* U+0301 after an invalid byte: GB4 break */
        { "x\xFF\xCC\x81", 4, 2, 1 },
        { "x\xC2\xA9\xE2\x80\x8D\xC2\xAE", 8, 8, 1 },                                     /* x | © ZWJ ® */
    };
    for (size_t i = 0; i < NELEM(c); i++) {
        CHECK(utf8_grapheme_prev(put(S_ONE, c[i].s, c[i].off), c[i].off) == c[i].want);
        CHECK(utf8_grapheme_prev(put_begin(S_ONE, c[i].s, c[i].off), c[i].off) == c[i].want);
    }
}

static void t_cluster_width(void)
{
    CHECK(CWS("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB") == 2); /* (G) one unshaped grapheme, base's wide cell pair */
    CHECK(CWS("\xE2\x80\x8D") == 0);
    CHECK(CWS("\xEF\xB8\x8E") == 0);
    CHECK(CWS("\xEF\xB8\x8F") == 0);
    for (uint32_t cp = 0x1F3FB; cp <= 0x1F3FF; cp++) {
        uint8_t b[4];
        size_t l = utf8_encode(cp, b);
        CHECK(first_cluster_width(put(S_ONE, b, l), l) == 0);
    }
    CHECK(cw("", 0) == 0);
    CHECK(CWS("a") == 1);
    CHECK(CWS("e\xCC\x81") == 1);                          /* base + mark */
    CHECK(CWS("\xCC\x81") == 0);                           /* lone mark */
    CHECK(CWS("\xD8\x80\xD8\xA8") == 1);                   /* Prepend U+0600 + BEH: not 0 */
    CHECK(CWS("\xD8\x80") == 0);
    CHECK(CWS("\xE0\xB5\x8E") == 1);                       /* U+0D4E Prepend, visible */
    CHECK(CWS("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == 2); /* L V T: 2, not 3 */
    CHECK(CWS("\xE1\x84\x80") == 2);
    CHECK(CWS("\xEA\xB0\x80") == 2);                       /* LV (EAW W anyway) */
    CHECK(CWS("\xE1\x85\xA1\xE1\x86\xA8") == 2);           /* orphan V T */
    CHECK(CWS("\xE1\x85\x9F") == 0);                       /* U+115F alone: zero width wins */
    CHECK(CWS("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95") == 1); /* Devanagari conjunct */
    CHECK(CWS("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7") == 2); /* family, not 6 */
    CHECK(CWS("\xE2\x9D\xA4\xE2\x80\x8D\xE2\x9D\xA4") == 2);  /* narrow pict ZWJ narrow pict */
    CHECK(CWS("\xE2\x9D\xA4") == 1);
    CHECK(CWS("\xE2\x9D\xA4\xEF\xB8\x8F") == 2);           /* heart + VS16 */
    CHECK(CWS("\xE2\x9D\xA4\xEF\xB8\x8E") == 1);           /* heart + VS15 */
    CHECK(CWS("1\xEF\xB8\x8F\xE2\x83\xA3") == 2);          /* keycap */
    CHECK(CWS("#\xE2\x83\xA3") == 2);
    CHECK(CWS("1") == 1);
    CHECK(CWS("a\xEF\xB8\x8F") == 1);                      /* VS16 does not widen a letter */
    CHECK(CWS("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == 2);   /* thumbs up + skin tone */
    CHECK(CWS("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8") == 2);   /* flag */
    CHECK(CWS("\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88") == 2); /* rainbow flag */
    CHECK(CWS("\xE4\xB8\x80\xCC\x81") == 2);               /* wide + mark */
    CHECK(CWS("\xFF") == 1);                               /* invalid byte: 1 (not AD-zero) */
    CHECK(CWS("\xAD") == 1);
    CHECK(CWS("\xC2\xAD") == 1);                           /* U+00AD is a Control cluster: placeholder */
    CHECK(CWS("\r\n") == 1);
    CHECK(CWS("\t") == 1);
    CHECK(CWS("\xE2\x80\x8B") == 1);                       /* ZWSP (Control) */
    /* cluster length agrees with grapheme_next, ASCII fast path with the slow path */
    for (uint32_t a = 0; a < 0x80; a++)
        for (uint32_t b = 0; b < 0x80; b++) {
            uint8_t s[2] = { (uint8_t)a, (uint8_t)b };
            const uint8_t *h = put(S_ONE, s, 2);
            int w;
            size_t l = first_cluster(h, 2, &w);
            CHECK_BREAK(l == first_grapheme(h, 2) && w == 1 && (l == 2) == (a == '\r' && b == '\n'));
        }
}

/* -------------------------------------------- random buffers */
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void t_random(void)
{
    enum { CAP = 1 << 16 };
    uint8_t *buf = malloc(CAP);
    size_t *bounds = malloc((CAP + 1) * sizeof *bounds);
    /* (E) 240 rounds, 60 per generator: v1's coverage count, not a bound; seeded, so reproducible. */
    for (int round = 0; round < 240; round++) {
        size_t n = 1 + rnd() % CAP;
        int gen = round % 4;
        for (size_t i = 0; i < n; i++) {
            uint32_t r = rnd() % 8;
            if (gen == 0)       /* ASCII / continuation / 2-, 3-, 4-byte leads */
                buf[i] = r < 3 ? (uint8_t)(rnd() & 0x7F) : r < 5 ? (uint8_t)(0x80 | (rnd() & 63)) : r == 5 ? (uint8_t)(0xC0 | (rnd() & 63))
                       : r == 6 ? (uint8_t)(0xE0 | (rnd() & 31)) : (uint8_t)(0xF0 | (rnd() & 15));
            else if (gen == 1)  /* continuation-heavy mix */
                buf[i] = r < 3 ? (uint8_t)(rnd() & 0x7F) : r < 6 ? (uint8_t)(0x80 + rnd() % 0x80) : (uint8_t)(0xC0 + rnd() % 0x40);
            else
                buf[i] = (uint8_t)rnd();
        }
        if (gen == 3) { size_t i = 0; while (i + 4 <= n) { uint32_t c = rnd() % 0x110000; i += utf8_encode(c, buf + i); if (rnd() % 16 == 0) buf[i - 1] ^= 0x40; } }
        const uint8_t *h = put(S_TEXT, buf, n);
        size_t off = 0, nb = 0;
        while (off < n) {
            utf8_step s = utf8_decode(h + off, n - off);
            if (!(s.len >= 1 && s.len <= 4 && off + s.len <= n)) { CHECK(0); break; }
            bounds[nb++] = off;
            off += s.len;
        }
        bounds[nb] = n;
        CHECK(off == n);
        size_t o = n;
        for (size_t k = nb; k > 0; k--) { o = utf8_test_prev(h, o); if (o != bounds[k - 1]) { CHECK(o == bounds[k - 1]); break; } }
        /* grapheme boundaries advance, never overrun, and land on unit boundaries */
        size_t bi = 0;
        for (off = 0; off < n;) {
            size_t g = first_grapheme(h + off, n - off);
            if (g == 0 || off + g > n) { CHECK(g >= 1 && off + g <= n); break; }
            off += g;
            while (bi < nb && bounds[bi] < off) bi++;
            if (bounds[bi] != off) { CHECK(bounds[bi] == off); break; }
        }
    }
    free(bounds);
    free(buf);
}

/* Random property soup against the oracle: segmentation, prev, cluster width. */
static void t_oracle_random(void)
{
    static const uint32_t pool[] = { 'a', 'b', ' ', 0x7F, 0xA0, 0xA9, 0xAD, 0xAE, 0xE9, 0x301, 0x308, 0x600, 0x605, 0x903,
        0x915, 0x939, 0x94D, 0x9CD, 0x995, 0xACD, 0x200B, 0x200C, 0x200D, 0x2028, 0x2764, 0xFE0E, 0xFE0F, 0x20E3, 0x1100,
        0x1160, 0x11A8, 0xAC00, 0xAC01, 0x1F600, 0x1F3FD, 0x1F468, 0x1F1E6, 0x1F1E7, 0x1F1E8, 0xE0020, 0xE007F, 0x4E00,
        0xD4E, '\r', '\n', '\t', '1', '#' };
    /* (E) 20000 rounds of up to 40 units: v1's coverage count, not a bound; seeded. */
    for (int round = 0; round < 20000; round++) {
        uint8_t buf[160];
        int c[48], ib[48];
        utf8_step u[48];
        size_t pos[49];
        size_t n = 0;
        int m = 0, units = 1 + (int)(rnd() % 40);
        for (int i = 0; i < units; i++) {
            if (rnd() % 12 == 0)
                buf[n++] = (uint8_t)(0x80 | rnd());        /* stray continuation or lead byte */
            else
                n += utf8_encode(pool[rnd() % NELEM(pool)], buf + n);
        }
        for (size_t o = 0; o < n && m < 48;) {             /* units as the decoder sees them */
            utf8_step s = utf8_decode(buf + o, n - o);
            pos[m] = o;
            u[m] = s;
            c[m] = ref_class(s.valid, s.cp);
            ib[m] = ref_incb(s.valid, s.cp);
            o += s.len;
            m++;
        }
        pos[m] = n;
        const uint8_t *exact = put(S_TEXT, buf, n);
        int i = 0;
        for (size_t off = 0; off < n;) {
            size_t g = first_grapheme(exact + off, n - off);
            int j = i + 1;                                  /* ref: extend until the next break */
            while (j < m && !ref_break(c, ib, j)) j++;
            CHECK_BREAK(g == pos[j] - off);
            CHECK_BREAK(utf8_grapheme_prev(exact, off + g) == off);
            int w;
            CHECK_BREAK(first_cluster(exact + off, n - off, &w) == g);
            CHECK_BREAK(w == ref_cluster_width(u + i, c + i, j - i));
            for (int k = i + 1; k < j; k++)                 /* unit ends inside the cluster: its start */
                CHECK_BREAK(utf8_grapheme_prev(exact, pos[k]) == off);
            off += g;
            i = j;
        }
    }
}

/* Fast paths in utf8.c rely on these table facts; fail loudly if a table
 * regeneration breaks them. */
static void t_table_facts(void)
{
    /* utf8_test_cell_width: below U+0300 only U+00AD is zero width, nothing is wide. */
    CHECK(UCD_ZERO_WIDTH[0].lo == 0xAD && UCD_ZERO_WIDTH[0].hi == 0xAD && UCD_ZERO_WIDTH[1].lo >= 0x300);
    CHECK(UCD_WIDE[0].lo >= 0x1100);
    /* gcb_of below U+0300: Control = Cc + U+00AD; no Extend, SpacingMark, Prepend, RI, Hangul. */
    CHECK(UCD_EXTEND[0].lo >= 0x300 && UCD_SPACINGMARK[0].lo >= 0x300 && UCD_PREPEND[0].lo >= 0x300);
    CHECK(UCD_HANGUL_L[0].lo >= 0x300 && UCD_REGIONAL_INDICATOR[0].lo >= 0x300);
    CHECK(UCD_CONTROL[0].lo == 0 && UCD_INCB_CONSONANT[0].lo >= 0x900 && UCD_INCB_LINKER[0].lo >= 0x900);
    for (uint32_t cp = 0; cp < 0x300; cp++) {
        int ctl = cp == '\r' || cp == '\n' ? 0 : RIN(UCD_CONTROL, cp);
        CHECK_BREAK(ctl == (cp < 0x20 && cp != '\r' && cp != '\n') + (cp >= 0x7F && cp <= 0x9F) + (cp == 0xAD));
        CHECK_BREAK(!RIN(UCD_EXTEND, cp) && !RIN(UCD_SPACINGMARK, cp) && !RIN(UCD_PREPEND, cp));
    }
    /* gcb_of below U+0300, Extended_Pictographic: exactly © and ®. A Unicode
     * version that adds a third fails here before it can regress GB11. */
    CHECK(UCD_EXT_PICT[0].lo == 0xA9 && UCD_EXT_PICT[0].hi == 0xA9);
    CHECK(UCD_EXT_PICT[1].lo == 0xAE && UCD_EXT_PICT[1].hi == 0xAE);
    CHECK(UCD_EXT_PICT[2].lo >= 0x300);
}

/* ---- utf8_cluster_step == first_cluster at every budget and split. */
static int cluster_step_ok(const uint8_t *src, size_t n, size_t budget, size_t chunk)
{
    size_t off = 0;
    while (off < n) {
        int wantw;
        size_t want = first_cluster(put(S_AUX, src + off, n - off), n - off, &wantw);
        utf8_cseg c;
        utf8_cseg_init(&c);
        size_t pos = 0, avail = chunk < n - off ? chunk : n - off;
        int w = -1, r, eof;
        unsigned guard = 0;
        do {
            size_t used = 0;
            eof = avail == n - off;
            const uint8_t *v = put(S_VIEW, src + off + pos, avail - pos);
            r = utf8_cluster_step(&c, v, avail - pos, budget, eof, &used, &w);
            if (used > (budget ? budget : 1) + 3)
                return 0;
            pos += used;
            if (r == UTF8_G_MORE) {
                if (eof)
                    return 0;
                avail = avail + chunk < n - off ? avail + chunk : n - off;
            }
        } while (r != UTF8_G_END && guard++ < 4 * n + 16);
        if (r != UTF8_G_END || pos != want || w != wantw)
            return 0;
        off += want;
    }
    return 1;
}

static void t_cluster_step(void)
{
    static const char *samples[] = {
        "abc", "e\xCC\x81x", "\xE2\x9D\xA4\xEF\xB8\x8F", "\xE2\x9D\xA4\xEF\xB8\x8E", "1\xEF\xB8\x8F\xE2\x83\xA3",
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", "\xD8\x80\x61", "\xD8\x80\xE2\x9D\xA4\xEF\xB8\x8F",
        "\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xAF", "\xE4\xB8\x80\xCC\x81",
        "\r\n\t\xFF\x80\xE2\x82", "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", "\xCC\x81\xCC\x81", "#\xE2\x83\xA3*\xEF\xB8\x8F\xE2\x83\xA3",
        "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", "\xE2\x9D\xA4\xE2\x80\x8D\xF0\x9F\x94\xA5x",
        "\xC2\xA9\xEF\xB8\x8F\xC2\xAE\xE2\x80\x8D\xC2\xA9\x61\xC2\xAE",
    };
    static const size_t budgets[] = { 0, 1, 2, 3, 5, 4096 };
    for (size_t i = 0; i < NELEM(samples); i++) {
        size_t n = strlen(samples[i]);
        for (size_t k = 0; k < NELEM(budgets); k++)
            for (size_t chunk = 1; chunk <= n + 1; chunk++)
                CHECK_BREAK(cluster_step_ok((const uint8_t *)samples[i], n, budgets[k], chunk));
    }
    /* random byte soup over an interesting alphabet */
    static const char *alpha[] = { "a", "\xCC\x81", "\xE2\x80\x8D", "\xEF\xB8\x8F", "\xEF\xB8\x8E", "\xE2\x83\xA3", "\xF0\x9F\x91\xA8",
                                   "\xE2\x9D\xA4", "\xD8\x80", "\xE1\x84\x80", "\xE1\x85\xA1", "1", "#", "\xF0\x9F\x87\xAF",
                                   "\xE4\xB8\x80", "\n", "\xFF", "\xE0\xA4\x95", "\xE0\xA5\x8D", "\xF0\x9F\x8F\xBD",
                                   "\xC2\xA9", "\xC2\xAE" };
    /* (E) 3000 draws: v1's coverage count, not a bound; seeded. */
    for (int it = 0; it < 3000; it++) {
        uint8_t buf[64];
        size_t n = 0, cnt = 1 + rnd() % 10;
        for (size_t j = 0; j < cnt; j++) {
            const char *a = alpha[rnd() % NELEM(alpha)];
            size_t l = strlen(a);
            memcpy(buf + n, a, l);
            n += l;
        }
        size_t budget = rnd() % 7, chunk = 1 + rnd() % 9;
        CHECK_BREAK(cluster_step_ok(buf, n, budget, chunk));
    }
    /* long clusters: result equals the one-shot, calls bounded by the budget */
    const struct { const char *h, *u; size_t reps; } L[] = {
        { "a", "\xCC\x81", 24576 }, { "", "\xD8\x80", 20000 },
        { "\xF0\x9F\x91\xA8", "\xE2\x80\x8D\xF0\x9F\x91\xA9", 5000 }, { "\xE2\x9D\xA4", "\xCC\x81", 3000 },
        { "\xC2\xA9", "\xE2\x80\x8D\xC2\xAE", 5000 },
    };
    for (size_t i = 0; i < NELEM(L); i++) {
        size_t n;
        uint8_t *src = chain(L[i].h, L[i].u, L[i].reps, &n);
        const uint8_t *b = put(S_TEXT, src, n);
        int ww;
        size_t want = first_cluster(b, n, &ww);
        utf8_cseg c;
        utf8_cseg_init(&c);
        size_t pos = 0, calls = 0, used;
        int w = -1, r;
        do {
            r = utf8_cluster_step(&c, b + pos, n - pos, UTF8_GRAPHEME_BUDGET, 1, &used, &w);
            CHECK(used <= UTF8_GRAPHEME_BUDGET + 3);
            pos += used;
            calls++;
        } while (r == UTF8_G_BUDGET && calls < n);
        CHECK(r == UTF8_G_END && pos == want && w == ww);
        CHECK(want == n && calls > 1);
        free(src);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);                   /* a fault keeps the lines before it */
    if (!slots_init()) {
        printf("utf8_test: FAIL guard mapping\n");
        return 1;
    }
    if (!slots_armed()) {
        printf("utf8_test: FAIL guard pages not armed; overread checks would be inert\n");
        return 1;
    }
    t_table_facts();
    t_pict_audit();
    t_gb11_copyright();
    t_copyright_width();
    t_vs16_every_cp();
    t_class_audit();
    t_decode_cases();
    t_decode_exhaustive();
    t_all_scalars();
    t_width();
    t_grapheme();
    t_grapheme_conformance();
    t_grapheme_prev_cases();
    t_step();
    t_cluster_width();
    t_cluster_step();
    t_oracle_random();
    t_random();
    for (int i = 0; i < SLOTS; i++) CHECK(munmap(slot[i].mapping, slot[i].mapped) == 0);
    printf(fails ? "utf8_test: %d FAILURES\n" : "utf8_test: all passed\n", fails);
    return fails != 0;
}
