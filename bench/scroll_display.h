#ifndef EDIT_BENCH_SCROLL_DISPLAY_H
#define EDIT_BENCH_SCROLL_DISPLAY_H
/* edit-ovu: consumer contract for edit-3kk's displayed-refresh observer.
 * Caller-owned, no allocation. Synthetic observations test the detector only.
 * Required population comes from perf/01-perf-target.md G3z / review §8:
 * (G) 10000 refreshes, zero missed displayed identities. No timing budget. */
#include "scroll/scroll.h"
#include <stdio.h>

#define SCROLL_DISPLAY_REFRESHES UINT64_C(10000)
typedef struct scroll_display_frame {
    uint32_t id;
    uint64_t first_input, last_input, source_generation, geometry_generation;
    uint64_t sidebar_revision, sidebar_identity, submit_ns;
    scroll_frame_plan plan;
    scroll_visual_row wrap_seed;
    bool wrapped;
} scroll_display_frame;
typedef struct scroll_display_observation {
    uint64_t sequence, opportunity_ns, completion_ns;
    scroll_display_frame expected, displayed;
    /* expected comes from the input/source oracle; displayed is the immutable
     * record looked up by the backend's ACTUAL completed ID. Pixel checks use
     * captured output, including the lower edge/sidebar, not submitted cells.
     * unsupported is a terminal capability observation, never a fake refresh. */
    bool supported, synthetic, completed, metadata_available;
    bool text_pixels_match, sidebar_pixels_match;
} scroll_display_observation;
typedef struct scroll_display_history {
    uint64_t observations, misses, last_sequence, last_opportunity, last_completion;
    uint32_t last_id;
    bool synthetic, unsupported, fractional, seen;
} scroll_display_history;
typedef enum scroll_display_verdict {
    SCROLL_DISPLAY_PASS, SCROLL_DISPLAY_MISS,
    SCROLL_DISPLAY_UNAVAILABLE, SCROLL_DISPLAY_REFUSED
} scroll_display_verdict;
static inline bool scroll_display_plan_equal(const scroll_frame_plan *a,
                                              const scroll_frame_plan *b)
{
    return a->first_byte == b->first_byte && a->first_row == b->first_row &&
        a->origin_y_q8 == b->origin_y_q8 && a->clip_width == b->clip_width &&
        a->clip_height == b->clip_height && a->row_height == b->row_height &&
        a->layout_rows == b->layout_rows && a->full_damage == b->full_damage;
}
static inline bool scroll_display_wrap_equal(const scroll_visual_row *a,
                                              const scroll_visual_row *b)
{
    return a->ordinal == b->ordinal && a->byte == b->byte && a->end == b->end &&
        a->line_start == b->line_start && a->line == b->line && a->column == b->column &&
        a->next == b->next && a->end_column == b->end_column && a->last == b->last &&
        a->indent == b->indent && a->newline == b->newline && a->continuation == b->continuation;
}
static inline bool scroll_display_frame_equal(const scroll_display_frame *a,
                                               const scroll_display_frame *b)
{
    return a->id == b->id && a->first_input == b->first_input && a->last_input == b->last_input &&
        a->source_generation == b->source_generation && a->geometry_generation == b->geometry_generation &&
        a->sidebar_revision == b->sidebar_revision && a->sidebar_identity == b->sidebar_identity &&
        a->submit_ns == b->submit_ns && a->wrapped == b->wrapped &&
        scroll_display_plan_equal(&a->plan, &b->plan) && scroll_display_wrap_equal(&a->wrap_seed, &b->wrap_seed);
}
static inline bool scroll_display_plan_valid(const scroll_frame_plan *p)
{
    if (!p->clip_width || !p->clip_height || !p->row_height || !p->layout_rows ||
        p->origin_y_q8 > 0 || p->origin_y_q8 <= -(int64_t)p->row_height * SCROLL_PIXEL_UNIT)
        return false;
    /* (P) translated bottom Q8 coordinate must fit reserved row coverage.
     * Both products widen before multiplication; factors are public u32. */
    uint64_t bottom = (uint64_t)p->clip_height * SCROLL_PIXEL_UNIT + (uint64_t)(-p->origin_y_q8);
    uint64_t row_q8 = (uint64_t)p->row_height * SCROLL_PIXEL_UNIT;
    return (bottom - 1u) / row_q8 < p->layout_rows;
}
/* Call once per refresh opportunity, not once per submit or completion. A
 * missing completion remains an observation. MSC gaps also count omitted
 * callbacks. Caller supplies the first opportunity as the stream baseline;
 * early termination cannot qualify the required population. */
static inline void scroll_display_observe(scroll_display_history *h,
                                          const scroll_display_observation *o)
{
    h->synthetic |= o->synthetic;
    if (!o->supported) { h->unsupported = true; return; }
    h->observations++;
    if (h->seen && o->sequence > h->last_sequence)
        h->misses += o->sequence - h->last_sequence - 1u;
    bool good = o->sequence != 0 && o->opportunity_ns != 0 && o->completed &&
        o->metadata_available && o->text_pixels_match && o->sidebar_pixels_match &&
        o->expected.id != 0 && o->expected.first_input != 0 &&
        o->expected.first_input <= o->expected.last_input && o->expected.submit_ns != 0 &&
        o->completion_ns >= o->expected.submit_ns && o->completion_ns <= o->opportunity_ns &&
        scroll_display_frame_equal(&o->expected, &o->displayed) &&
        scroll_display_plan_valid(&o->expected.plan);
    if (h->seen)
        good = good && o->sequence > h->last_sequence && o->opportunity_ns > h->last_opportunity &&
            o->completion_ns > h->last_completion && o->displayed.id != h->last_id;
    if (!good) h->misses++;
    else h->fractional |= o->displayed.plan.origin_y_q8 % (int64_t)SCROLL_PIXEL_UNIT != 0;
    if (o->sequence > h->last_sequence) h->last_sequence = o->sequence;
    if (o->opportunity_ns > h->last_opportunity) h->last_opportunity = o->opportunity_ns;
    if (o->completed && o->metadata_available) {
        if (o->completion_ns > h->last_completion) h->last_completion = o->completion_ns;
        h->last_id = o->displayed.id;
    }
    h->seen = true;
}
static inline scroll_display_verdict scroll_display_judge(const scroll_display_history *h)
{
    if (h->misses) return SCROLL_DISPLAY_MISS;
    if (h->unsupported || h->synthetic) return SCROLL_DISPLAY_UNAVAILABLE;
    if (h->observations < SCROLL_DISPLAY_REFRESHES || !h->fractional) return SCROLL_DISPLAY_REFUSED;
    return SCROLL_DISPLAY_PASS;
}
static inline const char *scroll_display_verdict_name(scroll_display_verdict verdict)
{
    switch (verdict) {
    case SCROLL_DISPLAY_PASS: return "PASS";
    case SCROLL_DISPLAY_MISS: return "MISS";
    case SCROLL_DISPLAY_UNAVAILABLE: return "UNAVAILABLE";
    case SCROLL_DISPLAY_REFUSED: return "REFUSED";
    }
    return "UNAVAILABLE";
}
/* Run a future editor driver: next waits off the typing path for its refresh
 * observer, uses a separately maintained oracle and captured-output checks,
 * and returns false on terminal unsupported/error/end. Storage is fixed.
 * The driver must propagate unsupported capability instead of supplying
 * synthetic completion as physical. This consumer adds no editor frame wait. */
typedef bool (*scroll_display_next)(void *ctx, scroll_display_observation *out);
static inline scroll_display_history scroll_display_run(scroll_display_next next, void *ctx)
{
    scroll_display_history h = {0};
    if (!next) { h.unsupported = true; return h; }
    while (h.observations < SCROLL_DISPLAY_REFRESHES && !h.unsupported) {
        scroll_display_observation o = {0};
        if (!next(ctx, &o)) break;
        scroll_display_observe(&h, &o);
    }
    return h;
}
static inline void scroll_display_report(FILE *out, const scroll_display_history *h)
{
    fprintf(out, "G3z displayed_verdict=%s refreshes=%llu required_refreshes=%llu misses=%llu synthetic=%s fractional=%s "
        "derivation=(G)perf/01-perf-target.md:G3z;misses=sequence_gaps+invalid_displayed_opportunities;allowed_misses=zero\n",
        scroll_display_verdict_name(scroll_display_judge(h)),
        (unsigned long long)h->observations, (unsigned long long)SCROLL_DISPLAY_REFRESHES,
        (unsigned long long)h->misses, h->synthetic ? "yes" : "no", h->fractional ? "yes" : "no");
}
#endif
