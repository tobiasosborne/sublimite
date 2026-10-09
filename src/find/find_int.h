/* Internal to src/find: shared metering/reader helpers. Not public API. */
#ifndef EDIT_FIND_INT_H
#define EDIT_FIND_INT_H
#include "find.h"
#include <string.h>

typedef struct meter {
    const find_control *control;
    unsigned units;
    bool stopped;
} meter;
static inline bool poll_stop(meter *m)
{
    const find_control *c=m->control;
    if (c && ((c->work && work_should_stop(c->work)) ||
        (c->cancel && atomic_load_explicit(c->cancel,memory_order_acquire)) ||
        (c->generation && atomic_load_explicit(c->generation,memory_order_acquire)!=c->expected_generation)))
        m->stopped=true;
    m->units=0;
    return m->stopped;
}
static inline bool step(meter *m)
{
    m->units++;
    return m->units>=FIND_POLL_UNITS ? poll_stop(m) : m->stopped;
}
static inline void clear_match(find_match *m)
{
    memset(m,0,sizeof *m);
    m->whole=(find_capture){FIND_UNSET,FIND_UNSET};
    for (size_t i=0;i<FIND_MAX_GROUPS;i++) m->captures[i]=m->whole;
}
static inline bool source_valid(const find_source *s)
{ return s && (s->snapshot || s->bytes || s->length==0); }
static inline uint64_t source_len(const find_source *s)
{ return s->snapshot ? piece_snapshot_len(s->snapshot) : (uint64_t)s->length; }
/* Lazy zero-copy snapshot reader, including verifications across pieces. */
typedef struct reader {
    const find_source *source;
    uint64_t len, base;
    const uint8_t *p;
    size_t n;
    piece_iter it;
} reader;
static inline reader reader_init(const find_source *s)
{
    reader r={0}; r.source=s; r.len=source_len(s); return r;
}
static inline uint8_t get_byte(reader *r,uint64_t off)
{
    if (!r->source->snapshot) return r->source->bytes[(size_t)off];
    if (!r->p || off<r->base || off-r->base>=(uint64_t)r->n) {
        piece_iter_begin_snapshot(&r->it,r->source->snapshot,off);
        r->base=off;
        (void)piece_iter_next(&r->it,&r->p,&r->n);
    }
    return r->p[(size_t)(off-r->base)];
}
/* Literal Two-Way has independent, monotonically advancing suffix/prefix
 * cursors. Seek once per cursor, then traverse each span at most once. Keep
 * the regex reader above rewindable for overlapping prefix retries/anchors. */
static inline uint8_t get_byte_forward(reader *r,uint64_t off,meter *m)
{
    if (!r->p) {
        if (step(m)) return 0;
        piece_iter_begin_snapshot(&r->it,r->source->snapshot,off);
        r->base=off;
        (void)piece_iter_next(&r->it,&r->p,&r->n);
    }
    while (off-r->base>=(uint64_t)r->n) {
        if (step(m)) return 0;
        r->base+=r->n;
        (void)piece_iter_next(&r->it,&r->p,&r->n);
    }
    return r->p[(size_t)(off-r->base)];
}
static inline find_code add_result(find_result *r,uint64_t off)
{
    if (r->total==UINT64_MAX) return FIND_ERR_LIMIT;
    if (r->stored<FIND_MAX_OFFSETS) r->offsets[r->stored++]=off;
    r->total++; return FIND_OK;
}
#endif
