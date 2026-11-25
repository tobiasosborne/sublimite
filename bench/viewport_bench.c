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

int main(void)
{
    /* 1,000 samples give ten observations in the upper 1% tail.
     * The flat-port fixture has one A29 seam and a one-byte suffix. */
    enum { SAMPLES = 1000 };
    uint64_t samples[SAMPLES];
    uint8_t text[FLAT_S + 1];
    memset(text, 'a', sizeof text);
    viewport_flat flat = flat_open(text, sizeof text, 0);
    viewport_motion *motion = malloc(viewport_motion_size());
    assert(motion);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = {.serial = 1, .action = VIEWPORT_WORD_LEFT,
        .selection = {sizeof text, sizeof text, UINT64_MAX}};
    for (size_t i = 0; i < SAMPLES; ++i) {
        uint64_t start = now_ns();
        viewport_answer out = flat_motion(motion, &flat, in, frame, UTF8_GRAPHEME_BUDGET);
        samples[i] = now_ns() - start;
        assert(out.selection.caret == 0);
    }
    qsort(samples, SAMPLES, sizeof *samples, compare);
    printf("viewport word-left over S+1 bytes, flat port: p50=%" PRIu64
           " ns p99=%" PRIu64 " ns\n", samples[SAMPLES / 2 - 1], samples[99 * SAMPLES / 100 - 1]);
    free(motion);
    flat_close(&flat);
    return 0;
}
