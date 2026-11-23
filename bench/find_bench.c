/* Find latency on warm, generated text. Timing observations are tracked
 * under CLAUDE.md law 1. A wrong result fails the benchmark self-check. */
#include "find/find.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (E) Repeat short log-like records enough to amortize timer overhead.
 * (P) One sample over a quarter quantum of records plus one sees both cases.
 * (G) Use scan_bench's inherited sample count and percentile convention. */
enum { RECORDS = 1u << 17, SELF_CHECK_RECORDS = FIND_POLL_UNITS / 4 + 1, SAMPLES = 20 };
static const uint8_t record[] = "Info  worker entered case Echo, work_state=ready code=ok\n";

typedef struct row {
    const char *name;
    const char *needle;
    find_literal_options options;
    bool adversarial;
} row;

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
    if (!strcmp(power, "Discharging")) tag = "bat";
    if (!strcmp(power, "Charging") || !strcmp(power, "Full") || !strcmp(power, "Not charging"))
        tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s at %s\n", tag, power, load, when);
}

static uint8_t lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + 'a') : c;
}

static bool word(uint8_t c)
{
    return (lower(c) >= 'a' && lower(c) <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static find_result reference(const find_source *source, const row *r, size_t n)
{
    find_result result = {0};
    for (size_t i = 0; i + n <= source->length;) {
        const uint8_t *p = source->bytes + i;
        size_t k = 0;
        while (k < n) {
            uint8_t needle = (uint8_t)r->needle[k];
            if (r->options.fold_ascii ? lower(p[k]) != lower(needle) : p[k] != needle) break;
            k++;
        }
        bool boundary = !r->options.whole_word ||
            ((!i || !word(p[-1])) && (i + n == source->length || !word(p[n])));
        if (k == n && boundary) {
            if (result.stored < FIND_MAX_OFFSETS) result.offsets[result.stored++] = i;
            result.total++;
            i += n;
        } else i++;
    }
    return result;
}

static void fill(uint8_t *text, size_t length, bool adversarial)
{
    if (adversarial) {
        /* (P) Periodic false positives pass the rare 'b' and common 'a'
         * ranks, but no run before 'b' reaches the requested length. */
        const uint8_t period[] = "aAaab";
        for (size_t i = 0; i < length; i++) text[i] = period[i % (sizeof period - 1)];
        return;
    }
    size_t width = sizeof record - 1;
    /* (E) Half and quarter polling quanta express sparse mixed-case hits
     * without a dependency on machine-specific corpus files. */
    for (size_t i = 0; i < length / width; i++) {
        uint8_t *start = text + i * width;
        memcpy(start, record, width);
        if (i % (FIND_POLL_UNITS / 2) == 0) memcpy(start, "ERROR", sizeof "ERROR" - 1);
        else if (i % (FIND_POLL_UNITS / 4) == 0) memcpy(start, "ErRoR", sizeof "ErRoR" - 1);
    }
}

int main(int argc,char **argv)
{
    /* (P) The only option switches to the smallest generated test run. */
    bool self_check = argc == 2 && !strcmp(argv[1], "--self-check");
    if (argc > 1 && !self_check) {
        fprintf(stderr, "usage: %s [--self-check]\n", argv[0]);
        return 1;
    }
    const row rows[] = {
        {"newline", "\n", {0}, false},
        {"fold_dense_byte", "e", {.fold_ascii=true}, false},
        {"sparse_exact", "ERROR", {0}, false},
        {"sparse_fold", "ERROR", {.fold_ascii=true}, false},
        {"sparse_fold_word", "ERROR", {.fold_ascii=true,.whole_word=true}, false},
        {"periodic_exact_miss", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", {0}, true},
        {"periodic_fold_miss", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", {.fold_ascii=true}, true}
    };
    size_t samples = self_check ? 1 : SAMPLES;
    size_t length = (self_check ? SELF_CHECK_RECORDS : RECORDS) * (sizeof record - 1);
    uint8_t *text = malloc(length);
    if (!text) return 1;
    find_source source = {text, length, NULL};
    stamp("start");
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const row *r = &rows[i];
        fill(text, length, r->adversarial);
        size_t n = strlen(r->needle);
        find_result want = reference(&source, r, n), got;
        uint64_t ns[SAMPLES];
        for (size_t j = 0; j < samples; j++) {
            uint64_t start = os_now_ns();
            find_code code = find_literal(&source, (const uint8_t *)r->needle, n,
                                          r->options, NULL, &got);
            ns[j] = os_now_ns() - start;
            if (code != FIND_OK || got.total != want.total || got.stored != want.stored ||
                memcmp(got.offsets, want.offsets, want.stored * sizeof want.offsets[0])) {
                fprintf(stderr, "find_bench: wrong result in %s\n", r->name);
                free(text);
                return 1;
            }
        }
        uint64_t p50 = trace_fmt_pct(ns, samples, 50), p99 = trace_fmt_pct(ns, samples, 99);
        printf("%-24s p50=%" PRIu64 " ns p99=%" PRIu64 " ns bytes=%zu samples=%zu hits=%" PRIu64 "\n",
               r->name, p50, p99, length, samples, want.total);
    }
    stamp("end");
    free(text);
    return 0;
}
