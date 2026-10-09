/* Regex half of find (frozen P1.10a semantics). Literal search lives in
 * literal.c; the regex literal prefix is searched with that kernel. */
#include "literal.h"

#define FIND_MAGIC UINT32_C(0x66696e64)
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
static size_t closure(reader *r,const find_regex *re,scratch *s,thread seed,
                      uint64_t pos,thread *dest,size_t count,meter *m,find_match *best)
{
    size_t top=0; s->stack[top++]=seed;
    while (top && !m->stopped) {
        thread t=s->stack[--top];
        if (step(m)) break;
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
static void anchored(reader *r,const find_regex *re,uint64_t off,scratch *s,meter *m,find_match *hit)
{
    clear_match(hit); hit->groups=re->groups;
    thread seed={0}; seed.state=re->start;
    for (size_t i=0;i<2*FIND_MAX_GROUPS;i++) seed.tags[i]=FIND_UNSET;
    memset(s->seen,0,sizeof s->seen);
    size_t count=closure(r,re,s,seed,off,s->lists[0],0,m,hit);
    unsigned current=0; uint64_t pos=off;
    while (count && pos<r->len && !m->stopped) {
        uint8_t c=get_byte(r,pos); size_t next_count=0;
        memset(s->seen,0,sizeof s->seen);
        for (size_t i=0;i<count && !m->stopped;i++) {
            thread t=s->lists[current][i]; const state *st=&re->states[t.state];
            if (step(m)) break;
            if (class_has(st->cls,c)) {
                t.state=st->out;
                next_count=closure(r,re,s,t,pos+1,s->lists[1u-current],next_count,m,hit);
            }
        }
        count=next_count; current=1u-current; pos++;
    }
    if (hit->matched) hit->whole.start=off;
}
/* Candidate starts come from the literal kernel (rare-byte filter + verify,
 * Two-Way fallback) when the program has a mandatory literal prefix. */
static bool regex_seek(reader *r,const find_regex *re,uint64_t off,scratch *s,meter *m,find_match *hit)
{
    if (re->prefix_len) {
        find_lit lit; uint64_t at=0;
        if (!find_lit_init(&lit,re->prefix,re->prefix_len,m,false)) return false;
        for (uint64_t i=off;;) {
            if (find_lit_seek(&lit,r->source,r->len,i,&at)<=0) return false;
            anchored(r,re,at,s,m,hit);
            if (hit->matched) return true;
            if (m->stopped || at==r->len) return false;
            if (step(m)) return false;
            i=at+1;
        }
    }
    for (uint64_t i=off;;i++) {
        if (step(m)) break;
        anchored(r,re,i,s,m,hit);
        if (hit->matched) return true;
        if (m->stopped || i==r->len) break;
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
                           void *memory,size_t size,const find_control *control,find_match *match,bool scan)
{
    if (!match) return FIND_ERR_ARGUMENT;
    clear_match(match);
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    if (off>source_len(source)) return FIND_ERR_ARGUMENT;
    meter m={control,0,false}; reader r=reader_init(source); match->groups=re->groups;
    if (!poll_stop(&m)) {
        if (scan) (void)regex_seek(&r,re,off,memory,&m,match);
        else anchored(&r,re,off,memory,&m,match);
    }
    if (poll_stop(&m)) { clear_match(match); return FIND_CANCELLED; }
    return FIND_OK;
}
find_code find_regex_captures(const find_source *source,const find_regex *re,uint64_t off,
                              void *memory,size_t size,const find_control *control,find_match *match)
{ return regex_one(source,re,off,memory,size,control,match,false); }
find_code find_regex_next(const find_source *source,const find_regex *re,uint64_t off,
                          void *memory,size_t size,const find_control *control,find_match *match)
{ return regex_one(source,re,off,memory,size,control,match,true); }
find_code find_regex_search(const find_source *source,const find_regex *re,void *memory,size_t size,
                            const find_control *control,find_result *result)
{
    if (!result) return FIND_ERR_ARGUMENT;
    result->total=0; result->stored=0;
    find_code code=regex_arguments(source,re,memory,size);
    if (code!=FIND_OK) return code;
    meter m={control,0,false}; reader r=reader_init(source); uint64_t off=0;
    if (!poll_stop(&m)) for (;;) {
        find_match hit; clear_match(&hit); hit.groups=re->groups;
        if (!regex_seek(&r,re,off,memory,&m,&hit)) break;
        code=add_result(result,hit.whole.start);
        if (code!=FIND_OK) break;
        if (step(&m)) break;
        off=hit.whole.end;
        if (off==hit.whole.start) { if (off==r.len) break; off++; }
    }
    if (poll_stop(&m)) code=FIND_CANCELLED;
    if (code!=FIND_OK) { result->total=0; result->stored=0; }
    return code;
}
