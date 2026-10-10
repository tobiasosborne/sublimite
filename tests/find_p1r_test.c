/* P1.10c: counted production kernels; probes are caller-owned, no clocks. */
#include "find/find.h"
#include "find/visit.h"
#include "base/base.h"
#include <immintrin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while (0)
typedef struct find_p1r_probe { size_t polls, popcounts, enumerated, cancel_at; } find_p1r_probe;
static bool find_p1r_stop(const work_ctx *context)
{
    find_p1r_probe *probe=context->arg;
    probe->polls++;
    return probe->cancel_at && probe->polls>=probe->cancel_at;
}
/* Replace the shared meter's work poll in this private production copy. */
#define work_should_stop find_p1r_stop
#include "find/literal.h"
#undef work_should_stop
static int find_p1r_popcount(uint64_t bits,meter *m)
{
    find_p1r_probe *probe=m->control->work->arg;
    probe->popcounts++;
    return __builtin_popcountll(bits);
}
static int find_p1r_ctz(uint64_t bits,meter *m)
{
    find_p1r_probe *probe=m->control->work->arg;
    probe->enumerated++;
    return __builtin_ctzll(bits);
}
#define __builtin_popcountll(bits) find_p1r_popcount((bits),m)
#define __builtin_ctzll(bits) find_p1r_ctz((bits),m)
#define find_lit_init find_p1r_lit_init
#define find_lit_seek find_p1r_lit_seek
#define find_literal_mode find_p1r_literal_mode
#define find_literal find_p1r_literal
#define find_literal_next find_p1r_literal_next
#define find_literal_visit find_p1r_literal_visit
#include "../src/find/literal.c"
#undef __builtin_popcountll
#undef __builtin_ctzll
#undef find_lit_init
#undef find_lit_seek
#undef find_literal_mode
#undef find_literal
#undef find_literal_next
#undef find_literal_visit

#define find_lit_init find_p1r_lit_init
#define find_lit_seek find_p1r_lit_seek
#define find_regex_bytes find_p1r_regex_bytes
#define find_regex_compile find_p1r_regex_compile
#define find_regex_prefix find_p1r_regex_prefix
#define find_regex_group_count find_p1r_regex_group_count
#define find_regex_scratch_bytes find_p1r_regex_scratch_bytes
#define find_regex_captures find_p1r_regex_captures
#define find_regex_next find_p1r_regex_next
#define find_regex_search find_p1r_regex_search
#define find_regex_visit find_p1r_regex_visit
#define find_regex_work_budget find_p1r_regex_work_budget
#define find_regex_next_budget find_p1r_regex_next_budget
#include "../src/find/find.c"
#undef find_lit_init
#undef find_lit_seek
#undef find_regex_bytes
#undef find_regex_compile
#undef find_regex_prefix
#undef find_regex_group_count
#undef find_regex_scratch_bytes
#undef find_regex_captures
#undef find_regex_next
#undef find_regex_search
#undef find_regex_visit
#undef find_regex_work_budget
#undef find_regex_next_budget

static int dense_count(void)
{
    uint8_t text[65536]; memset(text,'a',sizeof text);
    find_p1r_probe probe={0}; work_ctx context={.arg=&probe};
    find_control control={.work=&context}; find_source source={text,sizeof text,NULL};
    find_result result;
    /* Reference the probe even before production uses popcount. */
    meter unused={&control,0,false,NULL}; (void)find_p1r_popcount(0,&unused); probe.popcounts=0;
    CHECK(find_p1r_literal(&source,(const uint8_t *)"a",1,&control,&result)==FIND_OK);
    CHECK(result.total==sizeof text && result.stored==FIND_MAX_OFFSETS);
    for (size_t i=0;i<result.stored;i++) CHECK(result.offsets[i]==i);
    printf("P1R7 dense masks: popcounts=%zu enumerated=%zu discarded_masks=%zu\n",
           probe.popcounts,probe.enumerated,(sizeof text-FIND_MAX_OFFSETS)/64);
    CHECK(probe.popcounts>=(sizeof text-FIND_MAX_OFFSETS)/64 && probe.enumerated==FIND_MAX_OFFSETS);
    puts("P1R7: PASS bounded enumeration + fixed-work popcount");
    return 0;
}
static int regex_retry_bound(void)
{
    _Alignas(max_align_t) uint8_t program[FIND_MAX_PROGRAM_BYTES], memory[FIND_MAX_SCRATCH_BYTES];
    uint8_t text[4096]; memset(text,'a',sizeof text);
    const char *patterns[]={"a*b","aa*b","(a*)b"};
    for (size_t p=0;p<sizeof patterns/sizeof patterns[0];p++) {
        find_regex *regex=NULL;
        CHECK(find_p1r_regex_compile(program,sizeof program,(const uint8_t *)patterns[p],strlen(patterns[p]),&regex,NULL)==FIND_OK);
        size_t previous=0;
        for (size_t n=1024;n<=sizeof text;n*=2) {
            find_p1r_probe probe={0}; work_ctx context={.arg=&probe};
            find_control control={.work=&context}; find_source source={text,n,NULL};
            find_result result;
            find_code code=find_p1r_regex_search(&source,regex,memory,sizeof memory,&control,&result);
            printf("P1R9 pattern=%s N=%zu polls=%zu code=%d\n",patterns[p],n,probe.polls,(int)code);
            CHECK(code==FIND_ERR_LIMIT && result.total==0 && result.stored==0);
            CHECK(!previous || probe.polls<=2*previous+4);
            previous=probe.polls;
            probe=(find_p1r_probe){0};
            find_match match;
            CHECK(find_p1r_regex_next(&source,regex,0,memory,sizeof memory,&control,&match)==FIND_ERR_LIMIT);
            CHECK(!match.matched && match.whole.start==FIND_UNSET && match.groups==0);
            probe=(find_p1r_probe){.cancel_at=2};
            CHECK(find_p1r_regex_search(&source,regex,memory,sizeof memory,&control,&result)==FIND_CANCELLED);
            CHECK(!result.total && !result.stored);
        }
    }
    puts("P1R9: PASS linear retry budget, limit clearing, cancellation");
    return 0;
}
int main(void)
{
    CHECK(dense_count()==0);
    CHECK(regex_retry_bound()==0);
    puts("find_p1r_test: ok"); return 0;
}
