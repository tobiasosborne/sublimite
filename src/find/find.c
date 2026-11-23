/* Tagged Thompson byte regex. (P) Two tags per group record start/end,
 * the class bitset covers the uint8_t alphabet, and FIND_NONE is outside
 * FIND_MAX_STATES. The frozen cases and capture ordering come from v1. */
#include "literal.h"

#define FIND_MAGIC UINT32_C(0x66696e64) /* (P) ASCII "find" */
#define FIND_NONE UINT16_MAX

typedef enum state_kind { S_BYTE, S_JUMP, S_SPLIT, S_SAVE, S_BOL, S_EOL, S_MATCH } state_kind;
typedef struct state {
    state_kind kind;
    uint16_t out, other, tag;
    uint8_t cls[32];
} state;
struct find_regex {
    uint32_t magic;
    uint16_t count, start, groups, prefix_len;
    uint8_t prefix[FIND_MAX_STATES];
    state states[FIND_MAX_STATES];
};
/* Compiler: closed Thompson fragments, with a conservative common-prefix
 * calculation. Prefix filtering is sound even for nullable branches/loops. */
typedef struct fragment {
    uint16_t start,end,len;
    bool fixed;
    uint8_t prefix[FIND_MAX_STATES];
} fragment;
typedef struct parser {
    find_regex *re;
    const uint8_t *p;
    size_t n,pos,error;
    unsigned depth;
    find_code code;
} parser;
static void fail(parser *p,find_code code,size_t at)
{ if (p->code==FIND_OK) { p->code=code; p->error=at; } }
static uint16_t emit(parser *p,state_kind kind)
{
    if (p->code!=FIND_OK) return 0;
    if (p->re->count==FIND_MAX_STATES) { fail(p,FIND_ERR_LIMIT,p->pos); return 0; }
    uint16_t i=p->re->count++;
    p->re->states[i].kind=kind;
    p->re->states[i].out=FIND_NONE; p->re->states[i].other=FIND_NONE;
    return i;
}
static fragment blank(parser *p)
{
    fragment f={0}; f.start=emit(p,S_JUMP); f.end=f.start; f.fixed=true; return f;
}
static void class_set(uint8_t *cls,uint8_t c)
{ cls[c/8u]|=(uint8_t)(1u<<(c%8u)); }
static bool class_has(const uint8_t *cls,uint8_t c)
{ return (cls[c/8u] & (uint8_t)(1u<<(c%8u)))!=0; }
static uint8_t class_byte(parser *p)
{
    if (p->pos==p->n) { fail(p,FIND_ERR_SYNTAX,p->pos); return 0; }
    uint8_t c=p->p[p->pos++];
    if (c=='\\') {
        if (p->pos==p->n) { fail(p,FIND_ERR_SYNTAX,p->pos); return 0; }
        c=p->p[p->pos++];
    } else if (c=='[' && p->pos<p->n &&
               (p->p[p->pos]==':' || p->p[p->pos]=='.' || p->p[p->pos]=='='))
        fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
    return c;
}
static void parse_class(parser *p,uint8_t *cls)
{
    bool negate=false, any=false;
    if (p->pos<p->n && p->p[p->pos]=='^') { negate=true; p->pos++; }
    while (p->pos<p->n && p->p[p->pos]!=']' && p->code==FIND_OK) {
        uint8_t lo=class_byte(p); any=true;
        if (p->pos+1<p->n && p->p[p->pos]=='-' && p->p[p->pos+1]!=']') {
            p->pos++; uint8_t hi=class_byte(p);
            if (hi<lo) { fail(p,FIND_ERR_SYNTAX,p->pos); break; }
            for (unsigned c=lo;c<=(unsigned)hi;c++) class_set(cls,(uint8_t)c);
        } else class_set(cls,lo);
    }
    if (!any || p->pos==p->n) fail(p,FIND_ERR_SYNTAX,p->pos);
    else p->pos++;
    if (negate) for (size_t i=0;i<32;i++) cls[i]=(uint8_t)~cls[i];
}
static fragment parse_expr(parser *p);
static fragment parse_atom(parser *p)
{
    fragment f={0}; bool anchor=false;
    uint8_t c=p->p[p->pos++];
    if (c=='(') {
        if (++p->depth>FIND_MAX_DEPTH || p->re->groups==FIND_MAX_GROUPS) {
            fail(p,FIND_ERR_LIMIT,p->pos-1); return f;
        }
        uint16_t g=p->re->groups++;
        uint16_t begin=emit(p,S_SAVE); p->re->states[begin].tag=(uint16_t)(2u*g);
        f=parse_expr(p);
        if (p->pos==p->n || p->p[p->pos]!=')') fail(p,FIND_ERR_SYNTAX,p->pos);
        else p->pos++;
        uint16_t end=emit(p,S_SAVE); p->re->states[end].tag=(uint16_t)(2u*g+1u);
        p->re->states[begin].out=f.start; p->re->states[f.end].out=end;
        f.start=begin; f.end=emit(p,S_JUMP);
        p->re->states[end].out=f.end; p->depth--;
    } else {
        state_kind kind=S_BYTE;
        if (c=='^' || c=='$') { kind=c=='^'?S_BOL:S_EOL; anchor=true; }
        else if (c=='*' || c=='+' || c=='?') fail(p,FIND_ERR_SYNTAX,p->pos-1);
        else if (c=='{' || c=='}') fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
        f.start=emit(p,kind); f.end=emit(p,S_JUMP);
        p->re->states[f.start].out=f.end;
        uint8_t *cls=p->re->states[f.start].cls;
        if (anchor) f.fixed=true;
        else if (c=='[') parse_class(p,cls);
        else if (c=='.') {
            memset(cls,255,32); cls['\n'/8u]&=(uint8_t)~(1u<<('\n'%8u));
        } else {
            if (c=='\\') {
                if (p->pos==p->n) fail(p,FIND_ERR_SYNTAX,p->pos);
                else {
                    c=p->p[p->pos++];
                    if (c>='0' && c<='9') fail(p,FIND_ERR_UNSUPPORTED,p->pos-1);
                }
            }
            class_set(cls,c); f.len=1; f.prefix[0]=c; f.fixed=true;
        }
        /* Singleton classes can supply a literal prefix too. */
        if (!anchor && !f.fixed) {
            unsigned count=0; uint8_t only=0;
            for (unsigned v=0;v<256;v++) if (class_has(cls,(uint8_t)v)) { count++; only=(uint8_t)v; }
            if (count==1) { f.fixed=true; f.len=1; f.prefix[0]=only; }
        }
    }
    if (p->code==FIND_OK && p->pos<p->n &&
        (p->p[p->pos]=='*' || p->p[p->pos]=='+' || p->p[p->pos]=='?')) {
        uint8_t q=p->p[p->pos++];
        if (anchor) { fail(p,FIND_ERR_SYNTAX,p->pos-1); return f; }
        uint16_t split=emit(p,S_SPLIT), end=emit(p,S_JUMP);
        p->re->states[split].out=f.start; p->re->states[split].other=end;
        p->re->states[f.end].out=q=='?'?end:split;
        if (q!='+') { f.start=split; f.len=0; }
        f.end=end; f.fixed=false;
        if (p->pos<p->n && (p->p[p->pos]=='*' || p->p[p->pos]=='+' || p->p[p->pos]=='?'))
            fail(p,FIND_ERR_SYNTAX,p->pos);
    }
    return f;
}
static fragment parse_concat(parser *p)
{
    fragment f=blank(p);
    while (p->pos<p->n && p->p[p->pos]!='|' && p->p[p->pos]!=')' && p->code==FIND_OK) {
        fragment right=parse_atom(p);
        p->re->states[f.end].out=right.start; f.end=right.end;
        if (f.fixed) {
            size_t room=FIND_MAX_STATES-(size_t)f.len;
            size_t add=right.len<room?right.len:room;
            memcpy(f.prefix+f.len,right.prefix,add); f.len=(uint16_t)((size_t)f.len+add);
        }
        f.fixed=f.fixed && right.fixed;
    }
    return f;
}
static fragment parse_expr(parser *p)
{
    fragment f=parse_concat(p);
    while (p->pos<p->n && p->p[p->pos]=='|' && p->code==FIND_OK) {
        p->pos++; fragment right=parse_concat(p);
        uint16_t split=emit(p,S_SPLIT), end=emit(p,S_JUMP);
        p->re->states[split].out=f.start; p->re->states[split].other=right.start;
        p->re->states[f.end].out=end; p->re->states[right.end].out=end;
        size_t common=0;
        while (common<f.len && common<right.len && f.prefix[common]==right.prefix[common]) common++;
        f.fixed=f.fixed && right.fixed && f.len==right.len && common==f.len;
        f.len=(uint16_t)common; f.start=split; f.end=end;
    }
    return f;
}
size_t find_regex_bytes(void) { return sizeof(find_regex); }
find_code find_regex_compile(void *memory,size_t size,const uint8_t *pattern,size_t n,
                             find_regex **out,size_t *error_offset)
{
    if (error_offset) *error_offset=0;
    if (!out) return FIND_ERR_ARGUMENT;
    *out=NULL;
    if (!memory || (uintptr_t)memory%_Alignof(max_align_t)!=0 || (!pattern && n)) return FIND_ERR_ARGUMENT;
    if (size<sizeof(find_regex)) return FIND_ERR_MEMORY;
    if (n>FIND_MAX_PATTERN) { if (error_offset) *error_offset=FIND_MAX_PATTERN; return FIND_ERR_LIMIT; }
    find_regex *re=memory; memset(re,0,sizeof *re);
    parser p={re,pattern,n,0,0,0,FIND_OK}; fragment f=parse_expr(&p);
    if (p.code==FIND_OK && p.pos<n) fail(&p,FIND_ERR_SYNTAX,p.pos);
    if (p.code!=FIND_OK) { if (error_offset) *error_offset=p.error; return p.code; }
    re->states[f.end].kind=S_MATCH;
    re->start=f.start; re->prefix_len=f.len; memcpy(re->prefix,f.prefix,f.len);
    re->magic=FIND_MAGIC; *out=re; return FIND_OK;
}
const uint8_t *find_regex_prefix(const find_regex *re,size_t *len)
{ if (len) *len=re?re->prefix_len:0; return re?re->prefix:NULL; }
size_t find_regex_group_count(const find_regex *re) { return re?re->groups:0; }

/* Ordered, tagged Thompson simulation. Deduplicate each state at each boundary
 * on its first DFS visit. Earlier branches and consuming loop arms win ties. */
typedef struct thread {
    uint16_t state;
    uint64_t tags[2*FIND_MAX_GROUPS];
} thread;
typedef struct scratch {
    thread lists[2][FIND_MAX_STATES];
    thread stack[2*FIND_MAX_STATES+1];
    bool seen[FIND_MAX_STATES];
} scratch;
size_t find_regex_scratch_bytes(const find_regex *re) { return re?sizeof(scratch):0; }
/* (G) v1's retry ceiling: 64*(states+1)*(source_bytes+1) visits/candidates,
 * with saturation. The whole request shares this credit, including overlap
 * retries. Exhaustion returns LIMIT and clears the public counts. */
typedef struct regex_meter {
    meter poll;
    uint64_t remaining;
    bool limited;
} regex_meter;
uint64_t find_regex_work_budget(const find_regex *re,uint64_t length)
{
    if (!re || re->magic!=FIND_MAGIC) return 0;
    uint64_t scale=64*((uint64_t)re->count+1);
    return length>=UINT64_MAX/scale ? UINT64_MAX : (length+1)*scale;
}
static regex_meter regex_meter_init(const find_source *source,const find_regex *re,
                                   const find_control *control,uint64_t length)
{
    return (regex_meter){.poll = meter_init(source, control),
                         .remaining = find_regex_work_budget(re, length)};
}
static bool regex_step(regex_meter *m)
{
    if (!m->remaining) { m->limited=true; return true; }
    m->remaining--;
    return step(&m->poll);
}
static bool regex_stopped(const regex_meter *m)
{ return m->limited || m->poll.stopped; }
static size_t closure(reader *r,const find_regex *re,scratch *s,thread seed,
                      uint64_t pos,thread *dest,size_t count,regex_meter *m,find_match *best)
{
    size_t top=0; s->stack[top++]=seed;
    while (top && !regex_stopped(m)) {
        thread t=s->stack[--top];
        if (regex_step(m)) break;
        if (s->seen[t.state]) continue;
        s->seen[t.state]=true; const state *st=&re->states[t.state];
        switch (st->kind) {
        case S_BYTE: dest[count++]=t; break;
        case S_MATCH:
            if (!best->matched || pos>best->whole.end) {
                best->matched=true; best->whole.end=pos;
                for (size_t g=0;g<re->groups;g++) best->captures[g]=(find_capture){t.tags[2*g],t.tags[2*g+1]};
            }
            break;
        case S_SPLIT: {
            thread second=t; second.state=st->other; s->stack[top++]=second;
            t.state=st->out; s->stack[top++]=t; break;
        }
        case S_SAVE:
            t.tags[st->tag]=pos; t.state=st->out; s->stack[top++]=t; break;
        case S_BOL:
            if (pos==0 || get_byte(r,pos-1)=='\n') { t.state=st->out; s->stack[top++]=t; }
            break;
        case S_EOL:
            if (pos==r->len || get_byte(r,pos)=='\n') { t.state=st->out; s->stack[top++]=t; }
            break;
        case S_JUMP: t.state=st->out; s->stack[top++]=t; break;
        }
    }
    return count;
}
static size_t start_candidate(reader *r,const find_regex *re,scratch *s,
                              uint64_t off,regex_meter *m,find_match *hit)
{
    clear_match(hit); hit->groups=re->groups;
    thread seed={0}; seed.state=re->start;
    for (size_t i=0;i<2*FIND_MAX_GROUPS;i++) seed.tags[i]=FIND_UNSET;
    memset(s->seen,0,sizeof s->seen);
    return closure(r,re,s,seed,off,s->lists[0],0,m,hit);
}

static size_t advance_candidate(reader *r,const find_regex *re,scratch *s,
                                uint64_t pos,unsigned current,size_t count,
                                regex_meter *m,find_match *hit)
{
    uint8_t byte=get_byte(r,pos);
    size_t next_count=0;
    memset(s->seen,0,sizeof s->seen);
    for (size_t i=0;i<count && !regex_stopped(m);i++) {
        thread t=s->lists[current][i];
        if (regex_step(m)) break;
        const state *st=&re->states[t.state];
        if (class_has(st->cls,byte)) {
            t.state=st->out;
            next_count=closure(r,re,s,t,pos+1,s->lists[1u-current],next_count,m,hit);
        }
    }
    return next_count;
}

static void anchored(reader *r,const find_regex *re,uint64_t off,scratch *s,regex_meter *m,
                     find_match *hit)
{
    size_t count=start_candidate(r,re,s,off,m,hit);
    unsigned current=0;
    for (uint64_t pos=off;count && pos<r->len && !regex_stopped(m);pos++) {
        count=advance_candidate(r,re,s,pos,current,count,m,hit);
        current=1u-current;
    }
    if (hit->matched) hit->whole.start=off;
}
/* Candidate starts come from the literal kernel (rare-byte filter + verify,
 * Two-Way fallback) when the program has a mandatory literal prefix. */
static bool regex_seek(reader *r,const find_regex *re,uint64_t off,scratch *s,regex_meter *m,
                       find_lit *lit,find_match *hit)
{
    if (re->prefix_len) {
        uint64_t at=0;
        for (uint64_t i=off;;) {
            if (find_lit_seek(lit,r->source,r->len,i,&at)<=0) return false;
            anchored(r,re,at,s,m,hit);
            if (hit->matched) return true;
            if (regex_stopped(m) || at==r->len) return false;
            if (regex_step(m)) return false;
            i=at+1;
        }
    }
    for (uint64_t i=off;;i++) {
        if (regex_step(m)) break;
        anchored(r,re,i,s,m,hit);
        if (hit->matched) return true;
        if (regex_stopped(m) || i==r->len) break;
    }
    return false;
}
static find_code regex_arguments(const find_source *source,const find_regex *re,void *memory,size_t size)
{
    if (!source_valid(source) || !re || re->magic!=FIND_MAGIC || !memory ||
        (uintptr_t)memory%_Alignof(max_align_t)!=0) return FIND_ERR_ARGUMENT;
    return size<sizeof(scratch)?FIND_ERR_MEMORY:FIND_OK;
}
static find_code regex_one(const find_source *source,const find_regex *re,uint64_t off,
                           void *memory,size_t size,const find_control *control,find_match *match,bool scan,
                           uint64_t *budget)
{
    if (!match) return FIND_ERR_ARGUMENT;
    clear_match(match);
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    if (off>source_len(source)) return FIND_ERR_ARGUMENT;
    reader r=reader_init(source); regex_meter m=regex_meter_init(source,re,control,r.len); match->groups=re->groups;
    if (budget) m.remaining=*budget;
    if (!poll_stop(&m.poll)) {
        if (scan) {
            find_lit lit;
            if (!re->prefix_len || find_lit_init(&lit,source,re->prefix,re->prefix_len,(find_literal_options){0},&m.poll,false))
                (void)regex_seek(&r,re,off,memory,&m,&lit,match);
        }
        else anchored(&r,re,off,memory,&m,match);
    }
    if (budget) *budget=m.remaining;
    if (poll_stop(&m.poll)) { clear_match(match); return FIND_CANCELLED; }
    if (m.limited) { clear_match(match); return FIND_ERR_LIMIT; }
    return FIND_OK;
}
find_code find_regex_captures(const find_source *source,const find_regex *re,uint64_t off,
                              void *memory,size_t size,const find_control *control,find_match *match)
{ return regex_one(source,re,off,memory,size,control,match,false,NULL); }
find_code find_regex_next(const find_source *source,const find_regex *re,uint64_t off,
                          void *memory,size_t size,const find_control *control,find_match *match)
{ return regex_one(source,re,off,memory,size,control,match,true,NULL); }
find_code find_regex_next_budget(const find_source *source,const find_regex *re,uint64_t off,
                                void *memory,size_t size,const find_control *control,
                                uint64_t *budget,find_match *match)
{
    if (!budget) { if (match) clear_match(match); return FIND_ERR_ARGUMENT; }
    return regex_one(source,re,off,memory,size,control,match,true,budget);
}
static bool regex_word_boundary(reader *r,find_capture range,regex_meter *m)
{
    bool accepted=true;
    if (range.start) {
        if (regex_step(m)) return false;
        if (ascii_word(get_byte(r,range.start-1))) accepted=false;
    }
    if (range.end<r->len) {
        if (regex_step(m)) return false;
        if (ascii_word(get_byte(r,range.end))) accepted=false;
    }
    return accepted;
}
static find_code regex_search_run(const find_source *source,const find_regex *re,
                                 void *memory,size_t size,const find_control *control,
                                 find_sink *sink,bool whole_word)
{
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    reader r=reader_init(source), boundaries=reader_init(source);
    regex_meter m=regex_meter_init(source,re,control,r.len);
    find_lit lit;
    bool ready=!poll_stop(&m.poll);
    if (ready && re->prefix_len)
        ready=find_lit_init(&lit,source,re->prefix,re->prefix_len,(find_literal_options){0},&m.poll,false);
    if (ready) for (uint64_t off=0;;) {
        find_match hit;
        if (!regex_seek(&r,re,off,memory,&m,&lit,&hit)) break;
        bool accepted=!whole_word || regex_word_boundary(&boundaries,hit.whole,&m);
        if (regex_stopped(&m)) break;
        if (accepted) {
            code=find_sink_add(sink,&m.poll,hit.whole);
            if (code!=FIND_OK) break;
        }
        if (regex_step(&m)) break;
        off=accepted ? hit.whole.end : hit.whole.start;
        if (off==hit.whole.start) {
            if (off==r.len) break;
            off++;
        }
    }
    if (m.limited) code=FIND_ERR_LIMIT;
    return find_sink_finish(sink,&m.poll,code);
}

find_code find_regex_search(const find_source *source,const find_regex *re,void *memory,size_t size,
                            const find_control *control,find_result *result)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->total=0; result->stored=0;
    find_sink sink={.result=result};
    return regex_search_run(source,re,memory,size,control,&sink,false);
}
static find_code regex_visit_run(const find_source *source,const find_regex *re,void *memory,size_t size,
                                 const find_control *control,find_visit *visit,bool whole_word)
{
    if (!visit) return FIND_ERR_ARGUMENT;
    visit->total=0; visit->visible=0;
    if (!visit->emit) return FIND_ERR_ARGUMENT;
    find_sink sink={.visit=visit};
    return regex_search_run(source,re,memory,size,control,&sink,whole_word);
}
find_code find_regex_visit(const find_source *source,const find_regex *re,void *memory,size_t size,
                           const find_control *control,find_visit *visit)
{ return regex_visit_run(source,re,memory,size,control,visit,false); }
find_code find_regex_word_visit(const find_source *source,const find_regex *re,void *memory,size_t size,
                                const find_control *control,find_visit *visit)
{ return regex_visit_run(source,re,memory,size,control,visit,true); }

/* The continuation preserves one candidate's lists and endpoint across
 * yields. Its prefix cursor preserves overlaps from rejected candidates. */
typedef enum stream_phase { STREAM_SEEK, STREAM_MATCH, STREAM_DONE } stream_phase;

typedef struct regex_stream {
    scratch nfa;
    find_source source;
    const find_regex *re;
    reader bytes, prefix, boundaries;
    regex_meter m;
    find_match hit;
    uint64_t candidate, pos, prefix_pos, total;
    size_t count, prefix_matched, failure[FIND_MAX_STATES];
    unsigned current;
    stream_phase phase;
    find_code code;
    bool whole_word;
} regex_stream;

_Static_assert(sizeof(find_regex) <= FIND_MAX_PROGRAM_BYTES, "prepaid regex program");
_Static_assert(sizeof(regex_stream) <= FIND_MAX_SCRATCH_BYTES, "prepaid regex continuation");

size_t find_regex_stream_scratch_bytes(const find_regex *re)
{
    return re ? sizeof(regex_stream) : 0;
}

find_code find_regex_stream_begin(const find_source *source,const find_regex *re,
                                  void *memory,size_t size,bool whole_word)
{
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    if (size<sizeof(regex_stream)) return FIND_ERR_MEMORY;
    regex_stream *s=memory;
    memset(s,0,sizeof *s);
    s->source=*source;
    s->re=re;
    s->whole_word=whole_word;
    s->bytes=reader_init(&s->source);
    s->prefix=reader_init(&s->source);
    s->boundaries=reader_init(&s->source);
    s->m=regex_meter_init(&s->source,re,NULL,s->bytes.len);
    for (size_t i=1;i<re->prefix_len;i++) {
        size_t k=s->failure[i-1];
        while (k && re->prefix[k]!=re->prefix[i]) k=s->failure[k-1];
        s->failure[i]=k+(re->prefix[k]==re->prefix[i]);
    }
    return FIND_OK;
}

find_code find_regex_stream_step(void *memory,const find_control *control,find_visit *visit,bool *done)
{
    if (!done) return FIND_ERR_ARGUMENT;
    *done=false;
    if (!memory || (uintptr_t)memory%_Alignof(max_align_t)!=0 || !visit || !visit->emit)
        return FIND_ERR_ARGUMENT;
    regex_stream *s=memory;
    const find_regex *re=s->re;
    if (!s->total) visit->visible=0;
    find_sink sink={.visit=visit,.total=s->total};
    s->m.poll.control=control;
    uint64_t initial=s->m.remaining;
    size_t prefix_units=0;
    if (s->code!=FIND_OK || poll_stop(&s->m.poll)) goto finish;
    while (initial-s->m.remaining+prefix_units<FIND_POLL_UNITS && !regex_stopped(&s->m)) {
        switch (s->phase) {
        case STREAM_SEEK:
            if (re->prefix_len) {
                bool found=false;
                while (s->prefix_pos<s->bytes.len) {
                    if (step(&s->m.poll)) goto finish;
                    uint8_t byte=get_byte(&s->prefix,s->prefix_pos++);
                    prefix_units++;
                    while (s->prefix_matched && re->prefix[s->prefix_matched]!=byte) {
                        if (step(&s->m.poll)) goto finish;
                        s->prefix_matched=s->failure[s->prefix_matched-1];
                        prefix_units++;
                    }
                    if (re->prefix[s->prefix_matched]==byte) s->prefix_matched++;
                    if (s->prefix_matched==re->prefix_len) {
                        s->candidate=s->prefix_pos-re->prefix_len;
                        s->prefix_matched=s->failure[s->prefix_matched-1];
                        found=true;
                        break;
                    }
                    if (initial-s->m.remaining+prefix_units>=FIND_POLL_UNITS) goto finish;
                }
                if (!found) { s->phase=STREAM_DONE; goto finish; }
            }
            if (!re->prefix_len && regex_step(&s->m)) goto finish;
            s->count=start_candidate(&s->bytes,re,&s->nfa,s->candidate,&s->m,&s->hit);
            s->current=0;
            s->pos=s->candidate;
            s->phase=STREAM_MATCH;
            break;
        case STREAM_MATCH: {
            if (s->count && s->pos<s->bytes.len) {
                s->count=advance_candidate(&s->bytes,re,&s->nfa,s->pos,s->current,s->count,&s->m,&s->hit);
                s->current=1u-s->current;
                s->pos++;
                break;
            }
            s->hit.whole.start=s->candidate;
            bool accepted=s->hit.matched && (!s->whole_word ||
                regex_word_boundary(&s->boundaries,s->hit.whole,&s->m));
            if (regex_stopped(&s->m)) goto finish;
            if (accepted) {
                s->code=find_sink_add(&sink,&s->m.poll,s->hit.whole);
                if (s->code!=FIND_OK) goto finish;
            }
            /* Match the synchronous budget, including retirement of an EOF
             * candidate. A rejected word or empty match advances one byte. */
            if ((re->prefix_len || s->hit.matched) && regex_step(&s->m)) goto finish;
            uint64_t next=accepted?s->hit.whole.end:s->candidate;
            if (next==s->candidate) {
                if (next==s->bytes.len) { s->phase=STREAM_DONE; goto finish; }
                next++;
            }
            s->candidate=next;
            if (accepted) {
                s->prefix_pos=next;
                s->prefix_matched=0;
            }
            s->phase=STREAM_SEEK;
            break;
        }
        case STREAM_DONE:
            goto finish;
        }
    }
finish:
    if (s->m.limited) s->code=FIND_ERR_LIMIT;
    s->code=find_sink_finish(&sink,&s->m.poll,s->code);
    s->m.poll.control=NULL;
    s->total=visit->total;
    *done=s->code==FIND_OK && s->phase==STREAM_DONE;
    return s->code;
}
