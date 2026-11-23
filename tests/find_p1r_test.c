/* (G) Dense byte counts, regex retries and cancellation from v1 P1.10c.
 * Observe callbacks and interpose work polling on the linked public API. */
#include "find_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "find_p1r_test:%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

typedef struct probe { size_t polls, cancel_at; } probe;

bool __real_work_should_stop(const work_ctx *ctx);
bool __wrap_work_should_stop(const work_ctx *ctx)
{
    if (!ctx->arg) return __real_work_should_stop(ctx);
    probe *p=ctx->arg;
    p->polls++;
    return p->cancel_at && p->polls>=p->cancel_at;
}

typedef struct masks {
    uint64_t next, calls;
    bool valid;
    find_visit *visit;
} masks;

static bool no_range(void *user,uint64_t ordinal,find_capture range)
{
    (void)user;
    (void)ordinal;
    (void)range;
    return false;
}

static bool dense_mask(void *user,uint64_t mask,uint64_t base,uint64_t ordinal)
{
    masks *m=user;
    m->valid=m->valid && base==m->next && ordinal==m->next && mask==UINT64_MAX;
    /* (P) Each full mask contains every start in one uint64_t. */
    uint64_t span=sizeof mask * 8;
    m->next+=span;
    m->calls++;
    m->visit->visible+=span;
    return m->valid;
}

static int dense_count(void)
{
    /* (G) v1 P1R7 fills enough full masks to overflow the result cache. */
    uint8_t text[65536];
    for (size_t i=0;i<sizeof text;i++) text[i]=i%2 ? 'A' : 'a';
    find_source source={text,sizeof text,NULL};
    find_literal_options fold={.fold_ascii=true};
    const uint8_t byte='a';
    find_result result;
    CHECK(find_literal(&source,&byte,sizeof byte,fold,NULL,&result)==FIND_OK);
    CHECK(result.total==sizeof text && result.stored==FIND_MAX_OFFSETS);
    for (size_t i=0;i<result.stored;i++) CHECK(result.offsets[i]==i);

    find_visit visit={.wanted=FIND_UNSET,.emit=no_range,.emit_mask=dense_mask};
    masks m={.valid=true,.visit=&visit};
    visit.user=&m;
    CHECK(find_literal_visit(&source,&byte,sizeof byte,fold,NULL,&visit)==FIND_OK);
    CHECK(m.valid && m.calls==sizeof text/(sizeof(uint64_t)*8));
    CHECK(visit.total==sizeof text && visit.visible==sizeof text);
    puts("find_p1r_test: dense byte masks preserve ranks past the result cap");
    return 0;
}

static int regex_retry_bound(void)
{
    _Alignas(max_align_t) uint8_t program[FIND_MAX_PROGRAM_BYTES], memory[FIND_MAX_SCRATCH_BYTES];
    uint8_t text[4096]; memset(text,'a',sizeof text);
    const char *patterns[]={"a*b","aa*b","(a*)b"};
    for (size_t i=0;i<sizeof patterns/sizeof patterns[0];i++) {
        find_regex *regex;
        CHECK(find_regex_compile(program,sizeof program,(const uint8_t *)patterns[i],
                                 strlen(patterns[i]),&regex,NULL)==FIND_OK);
        size_t previous=0;
        for (size_t n=FIND_POLL_UNITS;n<=sizeof text;n*=2) {
            probe p={0}; work_ctx context={.arg=&p};
            find_control control={.work=&context}; find_source source={text,n,NULL};
            find_result result;
            CHECK(find_regex_search(&source,regex,memory,sizeof memory,&control,&result)==FIND_ERR_LIMIT);
            CHECK(!result.total && !result.stored);
            /* (G) v1 P1R9 doubles length, allowing four end/batch polls. */
            CHECK(!previous || p.polls<=2*previous+4);
            previous=p.polls;
            find_match match;
            CHECK(find_regex_next(&source,regex,0,memory,sizeof memory,&control,&match)==FIND_ERR_LIMIT);
            CHECK(!match.matched && match.whole.start==FIND_UNSET && match.groups==0);
            find_visit visit={.wanted=FIND_UNSET,.emit=no_range};
            CHECK(find_regex_stream_begin(&source,regex,memory,sizeof memory,false)==FIND_OK);
            bool done=false;
            find_code code;
            do {
                code=find_regex_stream_step(memory,NULL,&visit,&done);
            } while (code==FIND_OK && !done);
            CHECK(code==FIND_ERR_LIMIT && !done && !visit.total && !visit.visible);
            p=(probe){.cancel_at=2}; /* (P) Pass the entry poll, stop inside work. */
            CHECK(find_regex_search(&source,regex,memory,sizeof memory,&control,&result)==FIND_CANCELLED);
            CHECK(!result.total && !result.stored);
        }
    }
    puts("find_p1r_test: sync and continuation share bounded regex retries");
    return 0;
}

static int rejected_overlap_is_linear(void)
{
    /* (P) Double both text and needle lengths. Every alignment compares
     * equal by fold and fails a word boundary. A fresh scan at each
     * rejected start would cause roughly four times as many polls. */
    uint8_t text[16*FIND_POLL_UNITS], needle[sizeof text/4];
    for (size_t i=0;i<sizeof text;i++) text[i]=i%2 ? 'A' : 'a';
    memset(needle,'a',sizeof needle);
    find_literal_options options={.fold_ascii=true,.whole_word=true};
    for (unsigned force=0;force<=1;force++) {
        size_t previous=0;
        for (size_t n=sizeof text/2;n<=sizeof text;n*=2) {
            probe p={0}; work_ctx context={.arg=&p};
            find_control control={.work=&context};
            find_source source={text,n,NULL}; find_result result;
            find_code code=force ?
                find_test_literal_two_way(&source,needle,n/4,options,&control,&result) :
                find_literal(&source,needle,n/4,options,&control,&result);
            CHECK(code==FIND_OK && !result.total && !result.stored);
            /* (G) Same twofold bound and poll-rounding allowance as P1R9. */
            CHECK(!previous || p.polls<=2*previous+4);
            printf("find_p1r_test: word rejection force_tw=%u bytes=%zu polls=%zu\n",force,n,p.polls);
            previous=p.polls;
        }
    }
    return 0;
}

typedef struct log {
    /* (P) At most two prefix, four window and one wanted range. */
    uint64_t ordinals[2+4+1];
    find_capture ranges[2+4+1];
    size_t len;
} log;

static bool save(void *user,uint64_t ordinal,find_capture range)
{
    log *out=user;
    if (out->len==sizeof out->ordinals/sizeof out->ordinals[0]) return false;
    out->ordinals[out->len]=ordinal;
    out->ranges[out->len++]=range;
    return true;
}

static int regex_visit_equivalence(void)
{
    _Alignas(max_align_t) uint8_t program[FIND_MAX_PROGRAM_BYTES], memory[FIND_MAX_SCRATCH_BYTES];
    /* (P) Repeat the v1 rejected-overlap fixture with both word and
     * punctuation bytes. Multiple polling quanta force continuation yields. */
    uint8_t text[2*FIND_POLL_UNITS];
    const uint8_t unit[]="xa a a !";
    for (size_t i=0;i<sizeof text;i++) text[i]=unit[i%(sizeof unit-1)];
    const uint8_t *patterns[]={
        (const uint8_t *)"a a", (const uint8_t *)"(a+)|!", (const uint8_t *)""
    };
    /* (P) Cut inside the first rejected prefix and first accepted match. */
    const size_t cuts[]={2,4,5};
    find_fixture f=find_fixture_snapshot(text,sizeof text,cuts,sizeof cuts/sizeof cuts[0]);
    find_source source=find_fixture_source(f);
    for (size_t i=0;i<sizeof patterns/sizeof patterns[0];i++) {
        find_regex *regex;
        CHECK(find_regex_compile(program,sizeof program,patterns[i],
                                 strlen((const char *)patterns[i]),&regex,NULL)==FIND_OK);
        size_t stream_bytes=find_regex_stream_scratch_bytes(regex);
        CHECK(stream_bytes && stream_bytes<=sizeof memory);
        for (unsigned word=0;word<=1;word++) {
            log expected={0}, actual={0};
            find_visit sync={.prefix_capacity=2,.visible_capacity=4,.wanted=29,
                .window_start=sizeof text/2,.window_end=sizeof text/2+sizeof unit,
                .emit=save,.user=&expected};
            find_visit stream=sync;
            stream.user=&actual;
            work_ctx context={0}; probe p={0}; context.arg=&p;
            find_control control={.work=&context};
            find_code code=word ? find_regex_word_visit(&source,regex,memory,sizeof memory,&control,&sync)
                                : find_regex_visit(&source,regex,memory,sizeof memory,&control,&sync);
            CHECK(code==FIND_OK && sync.total>0 && p.polls>1);
            size_t request_polls=p.polls;
            if (!word) {
                find_result result;
                CHECK(find_regex_search(&source,regex,memory,sizeof memory,NULL,&result)==FIND_OK);
                CHECK(result.total==sync.total);
                for (size_t j=0;j<expected.len;j++)
                    CHECK(result.offsets[expected.ordinals[j]]==expected.ranges[j].start);
            }
            CHECK(find_regex_stream_begin(&source,regex,memory,stream_bytes-1,word!=0)==FIND_ERR_MEMORY);
            CHECK(find_regex_stream_begin(&source,regex,memory,stream_bytes,word!=0)==FIND_OK);
            bool done=false;
            size_t steps=0;
            while (!done) {
                CHECK(find_regex_stream_step(memory,NULL,&stream,&done)==FIND_OK);
                steps++;
            }
            CHECK(steps>1 && stream.total==sync.total && stream.visible==sync.visible);
            CHECK(actual.len==expected.len);
            for (size_t j=0;j<actual.len;j++) {
                CHECK(actual.ordinals[j]==expected.ordinals[j]);
                CHECK(actual.ranges[j].start==expected.ranges[j].start &&
                      actual.ranges[j].end==expected.ranges[j].end);
            }
            /* Observable cancellation discards accumulated counts. */
            CHECK(find_regex_stream_begin(&source,regex,memory,stream_bytes,word!=0)==FIND_OK);
            actual=(log){0}; stream.total=stream.visible=0;
            CHECK(find_regex_stream_step(memory,NULL,&stream,&done)==FIND_OK && !done && stream.total);
            p=(probe){.cancel_at=1};
            CHECK(find_regex_stream_step(memory,&control,&stream,&done)==FIND_CANCELLED);
            CHECK(!done && !stream.total && !stream.visible);
            if (word) {
                for (size_t cancel_at=1;cancel_at<=request_polls;cancel_at++) {
                    expected=(log){0};
                    p=(probe){.cancel_at=cancel_at};
                    CHECK(find_regex_word_visit(&source,regex,memory,sizeof memory,&control,&sync)==FIND_CANCELLED);
                    CHECK(!sync.total && !sync.visible);
                }
            }
        }
    }
    find_fixture_release(f);
    puts("find_p1r_test: regex result, visit and resumed visit agree");
    return 0;
}

int main(void)
{
    CHECK(dense_count()==0);
    CHECK(regex_retry_bound()==0);
    CHECK(rejected_overlap_is_linear()==0);
    CHECK(regex_visit_equivalence()==0);
    return 0;
}
