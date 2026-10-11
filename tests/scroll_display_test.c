/* Pure §8 displayed-identity regression; no display or timing measurement.
 * All fixture counts derive from SCROLL_DISPLAY_REFRESHES (G3z). Synthetic
 * timestamps are ordinal ticks (G fixture), not measured nanoseconds. */
#include "../bench/scroll_display.h"
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "scroll_display_test:%d: FAIL %s\n", __LINE__, #x); return 1; } } while (0)

static scroll_display_observation observation(uint64_t sequence)
{
    /* (G) one fractional Q8 unit per input; fixed geometry uses one visible
     * row plus scroll's required overscan row. Identifiers are ordinal ticks. */
    scroll_display_frame f = {.id = (uint32_t)sequence,
        .first_input = sequence, .last_input = sequence,
        .source_generation = sequence, .geometry_generation = sequence,
        .sidebar_revision = sequence, .sidebar_identity = sequence,
        .submit_ns = sequence * SCROLL_PIXEL_UNIT,
        .plan = {.first_byte = sequence, .first_row = sequence,
            .origin_y_q8 = -1, .clip_width = 1, .clip_height = 1,
            .row_height = 1, .layout_rows = 1u + 1u, .full_damage = true}};
    return (scroll_display_observation){.sequence = sequence,
        .opportunity_ns = f.submit_ns + SCROLL_PIXEL_UNIT,
        .completion_ns = f.submit_ns + 1u, .expected = f, .displayed = f,
        .supported = true, .synthetic = true, .completed = true,
        .metadata_available = true, .text_pixels_match = true, .sidebar_pixels_match = true};
}

static int repeated_stream(void)
{
    scroll_display_history h = {0};
    scroll_display_observation previous = {0};
    uint64_t submitted = 0, presented = 0;
    for (uint64_t seq = 1; seq <= SCROLL_DISPLAY_REFRESHES; seq++) {
        scroll_display_observation o = observation(seq);
        submitted++; presented++; /* Old proxy still counts every frame. */
        /* (G) review counterexample: repeat every second refresh, hence
         * SCROLL_DISPLAY_REFRESHES / 2 misses despite unchanged counters. */
        if (seq % (1u + 1u) == 0) o.displayed = previous.displayed;
        scroll_display_observe(&h, &o);
        previous = o;
    }
    CHECK(submitted == SCROLL_DISPLAY_REFRESHES && presented == submitted);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_MISS);
    CHECK(h.misses == SCROLL_DISPLAY_REFRESHES / (1u + 1u));
    puts("scroll_display_test: every-second displayed repeat rejected with unchanged submit/present counts PASS");
    return 0;
}

static int healthy_stream(void)
{
    scroll_display_history h = {0};
    for (uint64_t seq = 1; seq <= SCROLL_DISPLAY_REFRESHES; seq++) {
        scroll_display_observation o = observation(seq);
        scroll_display_observe(&h, &o);
    }
    CHECK(h.observations == SCROLL_DISPLAY_REFRESHES && h.misses == 0);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_UNAVAILABLE);
    /* Exercise eligibility logic only: changing this fixture flag does not
     * create physical evidence, and this test never reports a real G3z pass. */
    h.synthetic = false;
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_PASS);
    h.fractional = false;
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_REFUSED);
    h.fractional = true; h.observations--;
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_REFUSED);
    puts("scroll_display_test: healthy synthetic stream unavailable; population/fractional eligibility PASS");
    return 0;
}

static int metadata_and_completion(void)
{
    /* Each selected field is independently corrupted after a good refresh.
     * (G) mutation population is sizeof cases / sizeof cases[0], derived from
     * the observer contract's individual fields, not a performance sample. */
    enum mutation {
        FRAME_ID, INPUT_FIRST, INPUT_LAST, SOURCE, GEOMETRY, SIDEBAR_REVISION,
        SIDEBAR_IDENTITY, SUBMIT_TIME, FIRST_BYTE, FIRST_ROW, ORIGIN, WIDTH,
        HEIGHT, ROW_HEIGHT, LAYOUT_ROWS, DAMAGE, WRAPPED, WRAP_ORDINAL,
        WRAP_BYTE, WRAP_END, WRAP_LINE_START, WRAP_LINE, WRAP_COLUMN,
        WRAP_NEXT, WRAP_END_COLUMN, WRAP_LAST, WRAP_INDENT, WRAP_NEWLINE,
        WRAP_CONTINUATION, NO_METADATA, NO_COMPLETION, TEXT_PIXELS,
        SIDEBAR_PIXELS, COMPLETION_BEFORE_SUBMIT, COMPLETION_AFTER_REFRESH,
        REUSED_COMPLETION, REUSED_SEQUENCE, REVERSED_OPPORTUNITY, NO_SEQUENCE,
        NO_ID, INVALID_INPUT, INVALID_PLAN
    };
    const enum mutation cases[] = {
        FRAME_ID, INPUT_FIRST, INPUT_LAST, SOURCE, GEOMETRY, SIDEBAR_REVISION,
        SIDEBAR_IDENTITY, SUBMIT_TIME, FIRST_BYTE, FIRST_ROW, ORIGIN, WIDTH,
        HEIGHT, ROW_HEIGHT, LAYOUT_ROWS, DAMAGE, WRAPPED, WRAP_ORDINAL,
        WRAP_BYTE, WRAP_END, WRAP_LINE_START, WRAP_LINE, WRAP_COLUMN,
        WRAP_NEXT, WRAP_END_COLUMN, WRAP_LAST, WRAP_INDENT, WRAP_NEWLINE,
        WRAP_CONTINUATION, NO_METADATA, NO_COMPLETION, TEXT_PIXELS,
        SIDEBAR_PIXELS, COMPLETION_BEFORE_SUBMIT, COMPLETION_AFTER_REFRESH,
        REUSED_COMPLETION, REUSED_SEQUENCE, REVERSED_OPPORTUNITY, NO_SEQUENCE,
        NO_ID, INVALID_INPUT, INVALID_PLAN
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        scroll_display_history h = {0};
        scroll_display_observation first = observation(1);
        scroll_display_observe(&h, &first);
        scroll_display_observation o = observation(first.sequence + 1u);
        switch (cases[i]) {
        case FRAME_ID: o.displayed.id++; break;
        case INPUT_FIRST: o.displayed.first_input++; break;
        case INPUT_LAST: o.displayed.last_input++; break;
        case SOURCE: o.displayed.source_generation++; break;
        case GEOMETRY: o.displayed.geometry_generation++; break;
        case SIDEBAR_REVISION: o.displayed.sidebar_revision++; break;
        case SIDEBAR_IDENTITY: o.displayed.sidebar_identity++; break;
        case SUBMIT_TIME: o.displayed.submit_ns++; break;
        case FIRST_BYTE: o.displayed.plan.first_byte++; break;
        case FIRST_ROW: o.displayed.plan.first_row++; break;
        case ORIGIN: o.displayed.plan.origin_y_q8 = 0; break;
        case WIDTH: o.displayed.plan.clip_width++; break;
        case HEIGHT: o.displayed.plan.clip_height++; break;
        case ROW_HEIGHT: o.displayed.plan.row_height++; break;
        case LAYOUT_ROWS: o.displayed.plan.layout_rows++; break;
        case DAMAGE: o.displayed.plan.full_damage = false; break;
        case WRAPPED: o.displayed.wrapped = true; break;
        case WRAP_ORDINAL: o.displayed.wrap_seed.ordinal++; break;
        case WRAP_BYTE: o.displayed.wrap_seed.byte++; break;
        case WRAP_END: o.displayed.wrap_seed.end++; break;
        case WRAP_LINE_START: o.displayed.wrap_seed.line_start++; break;
        case WRAP_LINE: o.displayed.wrap_seed.line++; break;
        case WRAP_COLUMN: o.displayed.wrap_seed.column++; break;
        case WRAP_NEXT: o.displayed.wrap_seed.next++; break;
        case WRAP_END_COLUMN: o.displayed.wrap_seed.end_column++; break;
        case WRAP_LAST: o.displayed.wrap_seed.last++; break;
        case WRAP_INDENT: o.displayed.wrap_seed.indent++; break;
        case WRAP_NEWLINE: o.displayed.wrap_seed.newline = true; break;
        case WRAP_CONTINUATION: o.displayed.wrap_seed.continuation = true; break;
        case NO_METADATA: o.metadata_available = false; break;
        case NO_COMPLETION: o.completed = false; break;
        case TEXT_PIXELS: o.text_pixels_match = false; break;
        case SIDEBAR_PIXELS: o.sidebar_pixels_match = false; break;
        case COMPLETION_BEFORE_SUBMIT: o.completion_ns = o.expected.submit_ns - 1u; break;
        case COMPLETION_AFTER_REFRESH: o.completion_ns = o.opportunity_ns + 1u; break;
        case REUSED_COMPLETION: o.completion_ns = first.completion_ns; break;
        case REUSED_SEQUENCE: o.sequence = first.sequence; break;
        case REVERSED_OPPORTUNITY: o.opportunity_ns = first.opportunity_ns; break;
        case NO_SEQUENCE: o.sequence = 0; break;
        case NO_ID: o.expected.id = 0; o.displayed.id = 0; break;
        case INVALID_INPUT: o.expected.first_input = o.expected.last_input + 1u; o.displayed = o.expected; break;
        case INVALID_PLAN: o.expected.plan.layout_rows = 0; o.displayed = o.expected; break;
        }
        scroll_display_observe(&h, &o);
        CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_MISS);
        CHECK(h.misses == 1);
    }
    puts("scroll_display_test: immutable plan/wrap/input/source/sidebar and actual completion corruption rejected PASS");
    return 0;
}

static int missing_opportunities(void)
{
    scroll_display_history h = {0};
    scroll_display_observation first = observation(1);
    scroll_display_observe(&h, &first);
    /* Review's every-second drop also occurs as an absent callback: skipped
     * sequence count = next - previous - 1, independently of CPU timings. */
    scroll_display_observation next = observation(first.sequence + 1u + 1u);
    scroll_display_observe(&h, &next);
    CHECK(h.misses == next.sequence - first.sequence - 1u);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_MISS);
    h = (scroll_display_history){0};
    scroll_display_observation unsupported = {0};
    scroll_display_observe(&h, &unsupported);
    CHECK(h.observations == 0 && h.unsupported);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_UNAVAILABLE);
    puts("scroll_display_test: absent refresh callback counted; unsupported hook unavailable PASS");
    return 0;
}

static int integer_origin_is_not_fractional(void)
{
    scroll_display_history h = {0};
    scroll_display_observation o = observation(1);
    /* (G) one whole pixel in a two-pixel row: valid subrow scrolling, but
     * it does not test preservation of a subpixel Q8 renderer origin. */
    o.expected.plan.origin_y_q8 = -(int64_t)SCROLL_PIXEL_UNIT;
    o.expected.plan.row_height = 1u + 1u;
    o.expected.plan.clip_height = o.expected.plan.row_height;
    o.displayed = o.expected;
    scroll_display_observe(&h, &o);
    CHECK(h.misses == 0 && !h.fractional);
    puts("scroll_display_test: whole-pixel translation cannot qualify subpixel coverage PASS");
    return 0;
}

typedef struct driver_fixture {
    uint64_t calls, stop_after;
    bool repeat, synthetic;
    scroll_display_observation previous;
} driver_fixture;
static bool next_refresh(void *ctx, scroll_display_observation *out)
{
    driver_fixture *f = ctx;
    if (f->calls == f->stop_after) return false;
    *out = observation(++f->calls);
    out->synthetic = f->synthetic;
    if (f->repeat && f->calls % (1u + 1u) == 0) out->displayed = f->previous.displayed;
    f->previous = *out;
    return true;
}
static int driver_and_report(void)
{
    driver_fixture fixture = {.stop_after = SCROLL_DISPLAY_REFRESHES, .repeat = true, .synthetic = true};
    scroll_display_history h = scroll_display_run(next_refresh, &fixture);
    CHECK(fixture.calls == SCROLL_DISPLAY_REFRESHES);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_MISS);
    fixture = (driver_fixture){.stop_after = SCROLL_DISPLAY_REFRESHES, .synthetic = true};
    h = scroll_display_run(next_refresh, &fixture);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_UNAVAILABLE);
    fixture = (driver_fixture){.stop_after = SCROLL_DISPLAY_REFRESHES - 1u};
    h = scroll_display_run(next_refresh, &fixture);
    CHECK(scroll_display_judge(&h) == SCROLL_DISPLAY_REFUSED);
    h = scroll_display_run(NULL, NULL);
    CHECK(h.unsupported && h.observations == 0);
    FILE *f = tmpfile(); CHECK(f != NULL);
    scroll_display_report(f, &h);
    rewind(f);
    /* (G) report storage derives from the exact longest format above plus
     * sizeof's terminator; the fixture here has no measured digits. */
    char output[sizeof "G3z displayed_verdict=UNAVAILABLE refreshes=18446744073709551615 required_refreshes=18446744073709551615 misses=18446744073709551615 synthetic=yes fractional=yes derivation=(G)perf/01-perf-target.md:G3z;misses=sequence_gaps+invalid_displayed_opportunities;allowed_misses=zero\n"];
    size_t got = fread(output, 1, sizeof output - 1u, f);
    fclose(f); output[got] = '\0';
    CHECK(strstr(output, "displayed_verdict=UNAVAILABLE") != NULL);
    CHECK(strstr(output, "derivation=(G)perf/01-perf-target.md:G3z") != NULL);
    CHECK(strstr(output, "displayed_verdict=PASS") == NULL);
    puts("scroll_display_test: observer driver and evidence-labelled unavailable report PASS");
    return 0;
}

int main(void)
{
    return repeated_stream() || healthy_stream() || metadata_and_completion() ||
        missing_opportunities() || integer_origin_is_not_fractional() || driver_and_report();
}
