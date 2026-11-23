#include "base/base.h"
#include "find/find.h"
#include "find_fixture.h"

#include <stdio.h>

static bool record(void *user,uint64_t ordinal,find_capture range)
{
    (void)ordinal;
    (void)range;
    (*(size_t *)user)++;
    return true;
}

int main(void)
{
    /* (P) One folded word straddles a deliberately split snapshot. One
     * alphabetic byte on each side of the second copy rejects that copy. */
    const uint8_t text[]="Ab xaby";
    const uint8_t needle[]="ab";
    const uint8_t pattern[]="(ab)";
    const size_t cut=1;
    find_fixture f=find_fixture_snapshot(text,sizeof text-1,&cut,1);
    find_source source=find_fixture_source(f);
    find_literal_options options={.fold_ascii=true,.whole_word=true};
    _Alignas(max_align_t) uint8_t program[FIND_MAX_PROGRAM_BYTES];
    _Alignas(max_align_t) uint8_t scratch[FIND_MAX_SCRATCH_BYTES];
    uint8_t expansion[FIND_REGEX_FOLD_BUFFER_BYTES];
    find_regex *regex;
    find_result result;
    find_match match;
    size_t deliveries=0;
    find_visit visit={.prefix_capacity=FIND_MAX_OFFSETS,.wanted=FIND_UNSET,
                      .emit=record,.user=&deliveries};

    bool active=base_malloc_guard_active();
    ASSERT(active);
    base_malloc_guard_begin();
    ASSERT(find_literal(&source,needle,sizeof needle-1,options,NULL,&result)==FIND_OK);
    ASSERT(result.total==1 && result.offsets[0]==0);
    ASSERT(find_literal_next(&source,needle,sizeof needle-1,options,0,NULL,&match)==FIND_OK);
    ASSERT(match.matched && match.whole.end==sizeof needle-1);
    ASSERT(find_literal_visit(&source,needle,sizeof needle-1,options,NULL,&visit)==FIND_OK);
    ASSERT(visit.total==1 && deliveries==1);
    ASSERT(find_regex_compile(program,sizeof program,pattern,sizeof pattern-1,&regex,NULL)==FIND_OK);
    ASSERT(find_regex_compile_options(program,sizeof program,pattern,sizeof pattern-1,true,
                                     expansion,sizeof expansion,NULL,&regex,NULL)==FIND_OK);
    ASSERT(find_regex_search(&source,regex,scratch,sizeof scratch,NULL,&result)==FIND_OK);
    ASSERT(result.total==2);
    ASSERT(find_regex_next(&source,regex,0,scratch,sizeof scratch,NULL,&match)==FIND_OK);
    ASSERT(match.matched && match.whole.start==0);
    uint64_t budget=find_regex_work_budget(regex,sizeof text-1);
    ASSERT(find_regex_next_budget(&source,regex,0,scratch,sizeof scratch,NULL,&budget,&match)==FIND_OK);
    ASSERT(match.matched);
    ASSERT(find_regex_captures(&source,regex,0,scratch,sizeof scratch,NULL,&match)==FIND_OK);
    ASSERT(match.matched && match.captures[0].end==sizeof needle-1);
    ASSERT(find_regex_visit(&source,regex,scratch,sizeof scratch,NULL,&visit)==FIND_OK && visit.total==2);
    ASSERT(find_regex_word_visit(&source,regex,scratch,sizeof scratch,NULL,&visit)==FIND_OK && visit.total==1);
    ASSERT(find_regex_stream_begin(&source,regex,scratch,sizeof scratch,true)==FIND_OK);
    bool done=false;
    while (!done) ASSERT(find_regex_stream_step(scratch,NULL,&visit,&done)==FIND_OK);
    ASSERT(visit.total==1);
    size_t allocations=base_malloc_guard_end();

    find_fixture_release(f);
    printf("find allocation guard: active=%d count=%zu\n",active,allocations);
    return allocations!=0;
}
