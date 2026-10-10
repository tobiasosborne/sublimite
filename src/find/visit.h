/* Internal integration API; the frozen public find.h remains unchanged.
 * Count the whole source, deliver only a prefix, a bounded visible window,
 * and one requested ordinal. The callback runs synchronously on the worker;
 * false cancels the search. Inputs/results stay caller-owned. */
#ifndef EDIT_FIND_VISIT_H
#define EDIT_FIND_VISIT_H
#include "find.h"
typedef struct find_visit {
    size_t prefix_capacity, visible_capacity;
    uint64_t wanted, window_start, window_end;
    bool (*emit)(void *user,uint64_t ordinal,find_capture range);
    void *user;
    uint64_t total, visible;
} find_visit;
static inline bool find_visit_intersects(const find_visit *v,find_capture range)
{
    return range.start==range.end ? range.start>=v->window_start && range.start<=v->window_end
        : v->window_start<v->window_end && range.start<v->window_end && range.end>v->window_start;
}
static inline bool find_visit_add(find_visit *v,uint64_t ordinal,find_capture range)
{
    if (!v) return true;
    bool shown=find_visit_intersects(v,range);
    bool wanted=ordinal<v->prefix_capacity || ordinal==v->wanted ||
                (shown && v->visible<v->visible_capacity);
    if (shown) v->visible++;
    return !wanted || v->emit(v->user,ordinal,range);
}
static inline uint64_t find_visit_low_bits(uint64_t n)
{ return n>=64 ? UINT64_MAX : (UINT64_C(1)<<n)-1; }
static inline uint64_t find_visit_first_bits(uint64_t mask,uint64_t n)
{
    uint64_t keep=0;
    while (mask && n) { uint64_t bit=mask & (~mask+1); keep|=bit; mask^=bit; n--; }
    return keep;
}
/* Rank/select a SIMD mask. Retire discarded offsets by popcount, including
 * an arbitrarily late requested ordinal/window; never enumerate their gap. */
static inline bool find_visit_mask(find_visit *v,uint64_t mask,uint64_t base,uint64_t ordinal)
{
    if (!v) return true;
    uint64_t selected=ordinal<v->prefix_capacity ? find_visit_first_bits(mask,v->prefix_capacity-ordinal) : 0;
    if (v->wanted>=ordinal && v->wanted-ordinal<64)
        selected|=find_visit_first_bits(mask,v->wanted-ordinal+1) & ~find_visit_first_bits(mask,v->wanted-ordinal);
    uint64_t lo=v->window_start>base ? v->window_start-base : 0;
    uint64_t hi=v->window_end>base ? v->window_end-base : 0;
    uint64_t visible=mask & find_visit_low_bits(hi) & ~find_visit_low_bits(lo);
    if (v->visible<v->visible_capacity) selected|=find_visit_first_bits(visible,v->visible_capacity-v->visible);
    v->visible+=(unsigned)__builtin_popcountll(visible);
    while (selected) {
        unsigned bit=(unsigned)__builtin_ctzll(selected);
        uint64_t index=ordinal+(unsigned)__builtin_popcountll(mask & find_visit_low_bits(bit));
        if (!v->emit(v->user,index,(find_capture){base+bit,base+bit+1})) return false;
        selected&=selected-1;
    }
    return true;
}
find_code find_literal_visit(const find_source *source,const uint8_t *needle,size_t n,
                             const find_control *control,find_visit *visit);
find_code find_regex_visit(const find_source *source,const find_regex *regex,void *scratch,
                           size_t size,const find_control *control,find_visit *visit);
/* Filtered consumers share a request budget across overlapping retries,
 * including successful but rejected candidates. */
uint64_t find_regex_work_budget(const find_regex *regex,uint64_t length);
find_code find_regex_next_budget(const find_source *source,const find_regex *regex,uint64_t off,
                                void *scratch,size_t size,const find_control *control,
                                uint64_t *budget,find_match *match);
#endif
