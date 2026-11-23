/* indent latency, tracked numbers only (CLAUDE.md law 1): every timing
 * outcome exits 0. A wrong answer from a timed call is a correctness
 * failure and exits 1. Rows follow v1 bench/indent_bench.c; its corpus
 * files are replaced by a generated C-like text, and each typing row runs
 * over one span (a flat buffer) and over one-byte spans (P: the most
 * fragmented text a span iterator can present). The worst_blank rows read
 * the most a typing query can read; their per-byte cost derives
 * INDENT_BLANK_BYTES (docs/decisions/v2-0fv.12.md). */
#include "indent/indent.h"
#include "os/os.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v1 bench sample counts and viewport rows. */
enum { TYPING_SAMPLES = 20000, DETECT_SAMPLES = 2000, VIEW_ROWS = 300 };
/* Worst-case typing line: "\n", B blanks, the caret, B blanks, "\n". '}'
 * reads both blank parts and the LF, 2B + 1 bytes; Enter at the line end
 * reads B + 1 of its 2B blanks (P). */
#define WORST_BYTES (2u * INDENT_BLANK_BYTES + 2u)

typedef struct source {
    const uint8_t *bytes;
    uint64_t len, pos;
    size_t span; /* 0: the rest of the text */
} source;

static void src_seek(void *it, uint64_t off) { ((source *)it)->pos = off; }

static size_t src_next(void *it, const uint8_t **p)
{
    source *s = it;
    if (s->pos == s->len) return 0;
    uint64_t n = s->len - s->pos;
    if (s->span && n > s->span) n = s->span;
    *p = s->bytes + s->pos;
    s->pos += n;
    return (size_t)n;
}

static indent_text text_of(source *s) { return (indent_text){src_seek, src_next, s, s->len}; }

static void stamp(const char *when)
{
    char power[32] = "unknown", load[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = '\0';
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(f);
    }
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0 || strcmp(power, "Not charging") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s at %s\n", tag, power, load, when);
}

static uint64_t ns[TYPING_SAMPLES];

static void report(const char *name, size_t n)
{
    printf("%-34s p50=%8" PRIu64 " ns p99=%8" PRIu64 " ns  (%zu samples)\n", name,
           trace_fmt_pct(ns, n, 50), trace_fmt_pct(ns, n, 99), n);
}

/* Nested blocks of C-like lines, four spaces per level, so every line end
 * is an Enter cursor with a known leading run. */
static size_t generate(uint8_t *out, size_t cap, uint64_t *ends, size_t *nends)
{
    static const char *words[] = {"int x = f(a[i], b);", "if (y) {", "}", "return (z + 1) * 2;",
                                  "for (k = 0; k < n; k++) {", "}", "g(\"{\");"};
    size_t len = 0, depth = 1, w = 0;
    *nends = 0;
    while (len + 128 < cap) {
        const char *s = words[w++ % (sizeof words / sizeof words[0])];
        if (s[0] == '}' && depth > 1) depth--;
        for (size_t i = 0; i < 4 * depth; i++) out[len++] = ' ';
        memcpy(out + len, s, strlen(s));
        len += strlen(s);
        if (*nends < VIEW_ROWS) ends[(*nends)++] = len;
        out[len++] = '\n';
        if (s[strlen(s) - 1] == '{') depth++;
    }
    return len;
}

int main(void)
{
    static uint8_t code[2u * INDENT_PREFIX_BYTES], worst[WORST_BYTES];
    uint64_t ends[VIEW_ROWS];
    size_t nends;
    uint64_t len = generate(code, sizeof code, ends, &nends);
    const uint64_t blank = INDENT_BLANK_BYTES, caret = 1 + blank;
    memset(worst, ' ', sizeof worst);
    worst[0] = '\n';
    worst[sizeof worst - 1] = '\n';
    uint64_t brackets[VIEW_ROWS * 16];
    size_t nb = 0, view = (size_t)ends[nends - 1];
    for (size_t i = 0; i < view && nb < sizeof brackets / sizeof brackets[0]; i++)
        if (strchr("()[]{}", code[i])) brackets[nb++] = i;
    int bad = nends != VIEW_ROWS || nb == 0;
    /* Line starts of the Enter cursors: what the caller hands in. */
    uint64_t starts[VIEW_ROWS];
    for (size_t r = 0; r < nends; r++) {
        uint64_t s = ends[r];
        while (s > 0 && code[s - 1] != '\n') s--;
        starts[r] = s;
    }

    stamp("start");
    static const size_t spans[] = {0, 1};
    for (size_t sp = 0; sp < 2 && !bad; sp++) {
        source s = {code, len, 0, spans[sp]};
        indent_text t = text_of(&s);
        for (size_t i = 0; i < TYPING_SAMPLES; i++) {
            size_t r = i % VIEW_ROWS;
            uint64_t t0 = os_now_ns();
            indent_range run = indent_on_enter(&t, starts[r], ends[r]);
            ns[i] = os_now_ns() - t0;
            bad |= run.lo != starts[r] || run.hi == run.lo || code[run.hi - 1] != ' ';
        }
        report(sp ? "enter viewport_rows=300 span=1" : "enter viewport_rows=300 flat", TYPING_SAMPLES);
        for (size_t i = 0; i < TYPING_SAMPLES; i++) {
            size_t r = i % VIEW_ROWS;
            uint64_t t0 = os_now_ns();
            uint64_t from = indent_on_close_brace(&t, starts[r], ends[r], ends[r], 4);
            ns[i] = os_now_ns() - t0;
            bad |= from != ends[r];
        }
        report(sp ? "brace viewport_rows=300 span=1" : "brace viewport_rows=300 flat", TYPING_SAMPLES);
        for (size_t i = 0; i < TYPING_SAMPLES; i++) {
            uint64_t t0 = os_now_ns();
            indent_pair pair = indent_bracket_match(&t, brackets[i % nb], 0, view);
            ns[i] = os_now_ns() - t0;
            bad |= pair.mate != INDENT_NONE && pair.mate >= view;
        }
        report(sp ? "bracket viewport_rows=300 span=1" : "bracket viewport_rows=300 flat", TYPING_SAMPLES);
    }
    for (size_t sp = 0; sp < 2 && !bad; sp++) {
        source s = {worst, sizeof worst, 0, spans[sp]};
        indent_text t = text_of(&s);
        for (size_t i = 0; i < TYPING_SAMPLES; i++) {
            uint64_t t0 = os_now_ns();
            uint64_t from = indent_on_close_brace(&t, 1, caret, caret, 4);
            ns[i] = os_now_ns() - t0;
            bad |= from != caret - 4;
        }
        report(sp ? "worst_blank brace 2B+1 bytes span=1" : "worst_blank brace 2B+1 bytes flat", TYPING_SAMPLES);
        for (size_t i = 0; i < TYPING_SAMPLES; i++) {
            uint64_t t0 = os_now_ns();
            indent_range run = indent_on_enter(&t, 1, sizeof worst - 1);
            ns[i] = os_now_ns() - t0;
            bad |= run.hi != 1; /* more than B blanks: degraded */
        }
        report(sp ? "worst_blank enter B+1 bytes span=1" : "worst_blank enter B+1 bytes flat", TYPING_SAMPLES);
    }
    if (!bad) {
        source s = {code, len, 0, 0};
        indent_text t = text_of(&s);
        for (size_t i = 0; i < DETECT_SAMPLES; i++) {
            uint64_t t0 = os_now_ns();
            indent_style st = indent_detect(&t);
            ns[i] = os_now_ns() - t0;
            bad |= st.kind != INDENT_SPACES || st.eol != INDENT_LF || st.width != 4;
        }
        report("detect prefix=64KiB flat", DETECT_SAMPLES);
    }
    stamp("end");
    if (bad) {
        puts("indent_bench: FAIL a timed call returned a wrong answer");
        return 1;
    }
    return 0;
}
