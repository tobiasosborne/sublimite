#include "../tests/support/viewport_flat.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static uint64_t now_ns(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static size_t run(viewport_motion *m, viewport_flat *flat, const viewport_intent *in,
                  viewport_frame frame, uint64_t expect, uint64_t *step_ns, size_t capacity)
{
    viewport_motion_begin(m, in, frame, flat->length);
    viewport_port port = {flat, flat_query};
    viewport_answer out;
    viewport_progress progress;
    size_t steps = 0;
    do {
        size_t credit = UTF8_GRAPHEME_BUDGET, before = flat->work;
        uint64_t start = step_ns ? now_ns() : 0;
        progress = viewport_motion_step(m, &port, &credit, &out);
        if (step_ns) {
            uint64_t elapsed = now_ns() - start;
            assert(steps < capacity);
            step_ns[steps] = elapsed;
        }
        assert(flat->work - before <= UTF8_GRAPHEME_BUDGET - credit);
        ++steps;
    } while (progress == VP_MORE);
    assert(progress == VP_DONE && out.selection.caret == expect);
    return steps;
}

static void percentiles(const char *label, uint64_t *samples, size_t count)
{
    qsort(samples, count, sizeof *samples, compare);
    printf("%s (%zu samples) p50=%" PRIu64 " ns p99=%" PRIu64 " ns\n",
           label, count, samples[count / 2 - 1], samples[99 * count / 100 - 1]);
}

static void measure(viewport_motion *motion, viewport_flat *flat, const char *fixture,
                    viewport_action action, uint64_t expect)
{
    /* 1,000 motions give ten observations in the upper 1% tail. */
    enum { SAMPLES = 1000 };
    uint64_t motion_ns[SAMPLES];
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = {.serial = 1, .action = action,
        .selection = {flat->length, flat->length, UINT64_MAX}};
    size_t steps = run(motion, flat, &in, frame, expect, NULL, 0);
    uint64_t *step_ns = calloc(steps * SAMPLES, sizeof *step_ns);
    assert(step_ns);
    for (size_t i = 0; i < SAMPLES; ++i) {
        uint64_t start = now_ns();
        size_t complete = run(motion, flat, &in, frame, expect, NULL, 0);
        motion_ns[i] = now_ns() - start;
        assert(complete == steps);
    }
    for (size_t i = 0; i < SAMPLES; ++i)
        assert(run(motion, flat, &in, frame, expect, step_ns + i * steps, steps) == steps);
    printf("%s %s, %zu positive-credit steps per motion\n",
           fixture, action == VIEWPORT_LEFT ? "LEFT" : "WORD_LEFT", steps);
    percentiles("motion", motion_ns, SAMPLES);
    percentiles("positive-credit step", step_ns, steps * SAMPLES);
    free(step_ns);
}

static void benchmark(viewport_motion *motion, uint8_t *text, size_t len, size_t lf,
                      const char *name)
{
    const char tail[] = " abc";
    memset(text, 'x', len);
    if (lf < len)
        text[lf] = '\n';
    memcpy(text + len - (sizeof tail - 1), tail, sizeof tail - 1);
    viewport_flat flat = flat_open(text, len, 0);
    if (len == VIEWPORT_DISPLAY_MAX) {
        /* Feed an exactly D-byte certificate to exercise the conservative
         * contract bound. A29's independent oracle builds shorter real rows. */
        flat.rows = 0;
        flat_rows(&flat, 0, len, false, false, 0);
        flat.at[len].row = 0;
    }
    assert(flat.row[flat.rows - 1].end - flat.row[flat.rows - 1].byte == len - (lf < len ? lf + 1 : 0));
    measure(motion, &flat, name, VIEWPORT_LEFT, len - 1);
    measure(motion, &flat, name, VIEWPORT_WORD_LEFT, len - (sizeof tail - 2));
    flat_close(&flat);
}

int main(void)
{
    uint8_t text[VIEWPORT_DISPLAY_MAX];
    viewport_motion *motion = malloc(viewport_motion_size());
    assert(motion);
    /* Two 80-byte content rows separated by one LF, the requested 3,000
     * byte paragraph, and one certificate at the public A29 bound. */
    benchmark(motion, text, 2 * 80 + 1, 80, "80-byte lines");
    benchmark(motion, text, 3000, 3000, "3,000-byte paragraph");
    benchmark(motion, text, sizeof text, sizeof text, "65,540-byte certified row");
    free(motion);
    return 0;
}
