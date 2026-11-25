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
                  viewport_frame frame, uint64_t *step_ns, size_t capacity)
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
    assert(out.selection.caret == 0);
    return steps;
}

static void percentiles(const char *label, uint64_t *samples, size_t count)
{
    qsort(samples, count, sizeof *samples, compare);
    printf("%s (%zu samples) p50=%" PRIu64 " ns p99=%" PRIu64 " ns\n",
           label, count, samples[count / 2 - 1], samples[99 * count / 100 - 1]);
}

int main(void)
{
    /* 1,000 motions give ten observations in the upper 1% tail.
     * The flat-port fixture has one A29 seam and a one-byte suffix. */
    enum { SAMPLES = 1000 };
    uint64_t motion_ns[SAMPLES];
    uint8_t text[FLAT_S + 1];
    memset(text, 'a', sizeof text);
    viewport_flat flat = flat_open(text, sizeof text, 0);
    viewport_motion *motion = malloc(viewport_motion_size());
    assert(motion);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = {.serial = 1, .action = VIEWPORT_WORD_LEFT,
        .selection = {sizeof text, sizeof text, UINT64_MAX}};
    /* Warm up and size the samples from the steps in the fixed workload. */
    size_t steps = run(motion, &flat, &in, frame, NULL, 0);
    uint64_t *step_ns = calloc(steps * SAMPLES, sizeof *step_ns);
    assert(step_ns);
    for (size_t i = 0; i < SAMPLES; ++i) {
        uint64_t start = now_ns();
        size_t complete = run(motion, &flat, &in, frame, NULL, 0);
        motion_ns[i] = now_ns() - start;
        assert(complete == steps);
    }
    for (size_t i = 0; i < SAMPLES; ++i)
        assert(run(motion, &flat, &in, frame, step_ns + i * steps, steps) == steps);
    printf("viewport word-left over S+1 bytes, metered flat port, %zu steps per motion\n", steps);
    percentiles("motion", motion_ns, SAMPLES);
    percentiles("positive-credit step", step_ns, steps * SAMPLES);
    free(step_ns);
    free(motion);
    flat_close(&flat);
    return 0;
}
