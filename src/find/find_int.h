/* Internal to find. Every mapped read is covered by the request's polls. */
#ifndef EDIT_FIND_INT_H
#define EDIT_FIND_INT_H
#include "find.h"
#include "os/os.h"
#include <string.h>

typedef struct meter {
    const find_control *control;
    const piece_backing *backing;
    unsigned units;
    bool stopped;
} meter;

static inline meter meter_init(const find_source *source, const find_control *control)
{
    return (meter){.control = control,
                   .backing = source->view ? piece_view_backing(source->view) : NULL};
}

static inline bool poll_fault(meter *m)
{
    uint64_t epoch;
    if (m->backing && !m->stopped &&
        (os_fault_poll(m->backing->fault, &epoch) != OS_OK || epoch != 0))
        m->stopped = true;
    return m->stopped;
}

static inline bool poll_stop(meter *m)
{
    const find_control *c = m->control;
    if (!poll_fault(m) && c &&
        ((c->work && work_should_stop(c->work)) ||
         (c->cancel && atomic_load_explicit(c->cancel, memory_order_acquire)) ||
         (c->generation && atomic_load_explicit(c->generation, memory_order_acquire) !=
                           c->expected_generation)))
        m->stopped = true;
    m->units = 0;
    return m->stopped;
}

static inline bool step(meter *m)
{
    m->units++;
    return m->units >= FIND_POLL_UNITS ? poll_stop(m) : m->stopped;
}

static inline void clear_match(find_match *match)
{
    memset(match, 0, sizeof *match);
    match->whole = (find_capture){FIND_UNSET, FIND_UNSET};
    for (size_t i = 0; i < FIND_MAX_GROUPS; i++) match->captures[i] = match->whole;
}

static inline bool source_valid(const find_source *s)
{
    return s && (s->view ? !s->bytes && !s->length : s->bytes || !s->length);
}

static inline uint64_t source_len(const find_source *s)
{
    return s->view ? piece_length(s->view) : (uint64_t)s->length;
}

static inline bool ascii_letter(uint8_t byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static inline uint8_t ascii_fold(uint8_t byte)
{
    return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte;
}

static inline bool ascii_word(uint8_t byte)
{
    return ascii_letter(byte) || (byte >= '0' && byte <= '9') || byte == '_';
}

/* Cached zero-copy run. Regex may seek backwards for anchors and retries. */
typedef struct reader {
    const find_source *source;
    uint64_t len, base;
    const uint8_t *p;
    size_t n;
    piece_iter it;
} reader;

static inline reader reader_init(const find_source *s)
{
    return (reader){.source = s, .len = source_len(s)};
}

static inline void reader_seek(reader *r, uint64_t off)
{
    piece_iter_init(&r->it, r->source->view, off);
    r->base = off;
    r->n = piece_iter_next(&r->it, &r->p);
}

static inline void reader_advance(reader *r)
{
    r->base += r->n;
    r->n = piece_iter_next(&r->it, &r->p);
}

static inline uint8_t get_byte(reader *r, uint64_t off)
{
    if (!r->source->view) return r->source->bytes[(size_t)off];
    if (!r->n || off < r->base || off - r->base > r->n) reader_seek(r, off);
    else if (off - r->base == r->n) reader_advance(r);
    return r->p[(size_t)(off - r->base)];
}

/* Two-Way's left and right comparisons, and literal word-boundary reads,
 * each advance monotonically on their own cursor. Walking even a skipped
 * run is charged, so highly fragmented text cannot delay cancellation. */
static inline uint8_t get_byte_forward(reader *r, uint64_t off, meter *m)
{
    if (!r->source->view) return r->source->bytes[(size_t)off];
    if (!r->n) {
        if (step(m)) return 0;
        reader_seek(r, off);
    }
    while (off - r->base >= r->n) {
        if (step(m)) return 0;
        reader_advance(r);
    }
    return r->p[(size_t)(off - r->base)];
}

typedef struct find_sink {
    find_visit *visit;
    uint64_t total;
} find_sink;

static inline bool find_visit_intersects(const find_visit *v, find_capture range)
{
    if (range.start == range.end)
        return range.start >= v->window_start && range.start <= v->window_end;
    return v->window_start < v->window_end &&
           range.start < v->window_end && range.end > v->window_start;
}

static inline find_code find_sink_add(find_sink *sink, meter *m, find_capture range)
{
    if (sink->total == UINT64_MAX) return FIND_ERR_LIMIT;
    find_visit *v = sink->visit;
    bool shown = find_visit_intersects(v, range);
    bool selected = sink->total < v->prefix_capacity || sink->total == v->wanted ||
                    (shown && v->visible < v->visible_capacity);
    v->visible += shown;
    if (selected && (poll_fault(m) || !v->emit(v->user, sink->total, range)))
        return FIND_CANCELLED;
    sink->total++;
    return FIND_OK;
}

static inline find_code find_sink_finish(find_sink *sink, meter *m, find_code code)
{
    if (poll_stop(m)) code = FIND_CANCELLED;
    sink->visit->total = code == FIND_OK ? sink->total : 0;
    if (code != FIND_OK) sink->visit->visible = 0;
    return code;
}

#endif
