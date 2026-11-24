/* (G) v1 find_fuzz dimensions: endpoint-set regex model and a separate
 * KMP model for long fragmented sources. Small subjects fit in one uint64_t
 * endpoint set. The long operation crosses the inherited literal window. */
#include "../tests/find_fixture.h"
#include "base/base.h"
#include <string.h>
#include <stdlib.h>

static uint8_t lower_ascii(uint8_t byte)
{
    if (byte >= 'A' && byte <= 'Z') byte=(uint8_t)(byte-'A'+'a');
    return byte;
}

static bool literal_equal(uint8_t a,uint8_t b,find_literal_options options)
{
    return options.fold_ascii ? lower_ascii(a)==lower_ascii(b) : a==b;
}

typedef enum model_kind { M_LITERAL, M_CLASS, M_DOT, M_BOL, M_EOL, M_EMPTY,
                          M_CAT, M_ALT, M_STAR, M_PLUS, M_OPT } model_kind;
typedef struct node { model_kind kind; int left,right; uint8_t byte,cls; } node;
typedef struct model {
    node nodes[32]; size_t used;
    const uint8_t *input; size_t input_n,at;
    uint8_t pattern[512]; size_t pn;
    const uint8_t *text; size_t tn;
    bool fold;
    uint64_t memo[32][33]; bool known[32][33];
} model;
static uint8_t take(model *m)
{ return m->at<m->input_n ? m->input[m->at++] : 0; }
static int generate(model *m,unsigned depth)
{
    int index=(int)m->used++; node *n=&m->nodes[index];
    uint8_t k=take(m);
    n->kind=(model_kind)(depth ? k%11u : k%6u);
    if (n->kind==M_LITERAL) n->byte=take(m);
    if (n->kind==M_CLASS) n->cls=(uint8_t)(take(m)%4u);
    if (n->kind>=M_CAT) n->left=generate(m,depth-1);
    if (n->kind==M_CAT || n->kind==M_ALT) n->right=generate(m,depth-1);
    return index;
}
static void put(model *m,uint8_t c) { ASSERT(m->pn<sizeof m->pattern); m->pattern[m->pn++]=c; }
static void encode(model *m,int index)
{
    const node *n=&m->nodes[index];
    switch(n->kind) {
    case M_LITERAL:
        if (strchr("\\.^$[]()|*+?{}",n->byte)!=NULL && n->byte!=0) put(m,'\\');
        put(m,n->byte); break;
    case M_CLASS:
        put(m,'[');
        if(n->cls==0) { put(m,'a'); put(m,'-'); put(m,'c'); }
        else if(n->cls==1) { put(m,'^'); put(m,'a'); }
        else if(n->cls==2) { put(m,0x80); put(m,'-'); put(m,0xff); }
        else { put(m,'-'); put(m,'-'); put(m,'0'); }
        put(m,']'); break;
    case M_DOT: put(m,'.'); break;
    case M_BOL: put(m,'^'); break;
    case M_EOL: put(m,'$'); break;
    case M_EMPTY: put(m,'('); put(m,')'); break;
    case M_CAT: case M_ALT:
        put(m,'('); encode(m,n->left);
        if(n->kind==M_ALT) put(m,'|');
        encode(m,n->right); put(m,')'); break;
    case M_STAR: case M_PLUS: case M_OPT:
        /* Group permits quantifying even a zero-width anchor expression. */
        put(m,'('); encode(m,n->left); put(m,')');
        put(m,n->kind==M_STAR?'*':n->kind==M_PLUS?'+':'?'); break;
    }
}
static uint64_t endpoints(model *m,int index,size_t pos)
{
    if(m->known[index][pos]) return m->memo[index][pos];
    const node *n=&m->nodes[index]; uint64_t out=0,bit=UINT64_C(1)<<pos;
    switch(n->kind) {
    case M_LITERAL: if(pos<m->tn && literal_equal(m->text[pos],n->byte,(find_literal_options){.fold_ascii=m->fold})) out=bit<<1; break;
    case M_CLASS:
        if(pos<m->tn) {
            uint8_t c=m->fold?lower_ascii(m->text[pos]):m->text[pos]; bool yes=n->cls==0?(c>='a' && c<='c'):n->cls==1?(c!='a'):n->cls==2?(c>=0x80):(c>='-' && c<='0');
            if(yes) out=bit<<1;
        }
        break;
    case M_DOT: if(pos<m->tn && m->text[pos]!='\n') out=bit<<1; break;
    case M_BOL: if(pos==0 || m->text[pos-1]=='\n') out=bit; break;
    case M_EOL: if(pos==m->tn || m->text[pos]=='\n') out=bit; break;
    case M_EMPTY: out=bit; break;
    case M_ALT: out=endpoints(m,n->left,pos)|endpoints(m,n->right,pos); break;
    case M_CAT: {
        uint64_t left=endpoints(m,n->left,pos);
        for(size_t i=pos;i<=m->tn;i++) if(left&(UINT64_C(1)<<i)) out|=endpoints(m,n->right,i);
        break;
    }
    case M_OPT: out=bit|endpoints(m,n->left,pos); break;
    case M_STAR: case M_PLUS:
        out=n->kind==M_STAR?bit:endpoints(m,n->left,pos);
        for (;;) {
            uint64_t expanded=out;
            for(size_t i=pos;i<=m->tn;i++) if(out&(UINT64_C(1)<<i)) expanded|=endpoints(m,n->left,i);
            if(expanded==out) break;
            out=expanded;
        }
        break;
    }
    m->known[index][pos]=true; m->memo[index][pos]=out; return out;
}
static bool model_next(model *m,size_t off,size_t *start,size_t *end)
{
    for(size_t i=off;i<=m->tn;i++) {
        uint64_t set=endpoints(m,0,i);
        if(set) {
            *start=i; *end=i;
            for(size_t j=i;j<=m->tn;j++) if(set&(UINT64_C(1)<<j)) *end=j;
            return true;
        }
    }
    return false;
}
typedef struct word_results {
    /* Same boundary domain as model.memo: subject bytes plus its end. */
    find_capture ranges[33];
    bool seen[33];
    size_t count;
} word_results;
static bool model_word_byte(uint8_t byte)
{
    return (byte>='a' && byte<='z') || (byte>='A' && byte<='Z') ||
           (byte>='0' && byte<='9') || byte=='_';
}
static bool word_emit(void *user,uint64_t ordinal,find_capture range)
{
    word_results *expected=user;
    ASSERT(ordinal<expected->count && !expected->seen[ordinal]);
    ASSERT(range.start==expected->ranges[ordinal].start && range.end==expected->ranges[ordinal].end);
    expected->seen[ordinal]=true;
    return true;
}
static void word_visit_model(model *m,const find_source *source,const find_regex *re,
                             void *scratch,size_t scratch_size)
{
    word_results expected={0};
    for (size_t off=0;;) {
        size_t start=0,end=0;
        if (!model_next(m,off,&start,&end)) break;
        bool accepted=(!start || !model_word_byte(m->text[start-1])) &&
                      (end==m->tn || !model_word_byte(m->text[end]));
        if (accepted) expected.ranges[expected.count++]=(find_capture){start,end};
        off=accepted ? end : start;
        if (!accepted || start==end) { if (off==m->tn) break; off++; }
    }
    /* Minimal independent caches; request the middle ordinal and the subject's
     * latter half so bounded output is checked against discarded matches. */
    find_visit visit={.prefix_capacity=1,.visible_capacity=1,.wanted=expected.count/2,
                      .window_start=m->tn/2,.window_end=m->tn,.emit=word_emit,.user=&expected};
    for (unsigned streaming=0;streaming<=1;streaming++) {
        memset(expected.seen,0,sizeof expected.seen);
        if (streaming) {
            ASSERT(find_regex_stream_begin(source,re,scratch,scratch_size,true)==FIND_OK);
            bool done=false;
            while (!done) ASSERT(find_regex_stream_step(scratch,NULL,&visit,&done)==FIND_OK);
        } else {
            ASSERT(find_regex_word_visit(source,re,scratch,scratch_size,NULL,&visit)==FIND_OK);
        }
        ASSERT(visit.total==expected.count);
        uint64_t visible=0;
        for (size_t i=0;i<expected.count;i++) {
            find_capture range=expected.ranges[i];
            bool shown=range.start==range.end ? range.start>=visit.window_start && range.start<=visit.window_end
                        : visit.window_start<visit.window_end && range.start<visit.window_end && range.end>visit.window_start;
            bool wanted=i<visit.prefix_capacity || i==visit.wanted || (shown && visible<visit.visible_capacity);
            ASSERT(expected.seen[i]==wanted);
            visible+=shown;
        }
        ASSERT(visit.visible==visible);
    }
}
/* P1.10b gap: the small endpoint-set subjects cannot enter the long-needle
 * snapshot verifier. Generate bounded periodic/critical-prefix subjects and
 * compare against an independent KMP model, also from arbitrary next offsets. */
static uint64_t long_model(const uint8_t *h,size_t hn,const uint8_t *n,size_t nn,
                          const size_t *prefix,size_t from,find_literal_options options,find_fixture_result *out)
{
    out->total=0; out->stored=0;
    size_t matched=0;
    for (size_t i=from;i<hn;i++) {
        while (matched && !literal_equal(h[i],n[matched],options)) matched=prefix[matched-1];
        if (literal_equal(h[i],n[matched],options)) matched++;
        if (matched==nn) {
            size_t at=i+1-nn;
            bool word=(!at || !model_word_byte(h[at-1])) &&
                      (i+1==hn || !model_word_byte(h[i+1]));
            if (options.whole_word && !word) {
                matched=prefix[matched-1];
            } else {
                if (out->stored<FIND_FIXTURE_OFFSETS) out->offsets[out->stored++]=at;
                out->total++;
                matched=0; /* non-overlapping */
            }
        }
    }
    return out->stored?out->offsets[0]:FIND_UNSET;
}
static void long_snapshot_op(const uint8_t *data,size_t size,find_literal_options options)
{
    if (size<8 || (data[0]&63u)!=0) return;
    uint8_t h[16448],n[8192]; size_t prefix[8192];
    size_t nn=2049+(((size_t)data[1]<<8)|data[2])%6144,hn=2*nn+64;
    size_t period=1+data[3]%7u;
    const uint8_t alphabet[]={'a','A','b','B',0,'!',0xff,'_'};
    for (size_t i=0;i<hn;i++) h[i]=alphabet[data[(4+i%period)%size]%sizeof alphabet];
    for (size_t i=0;i<nn;i++) n[i]=alphabet[data[(4+i%period)%size]%sizeof alphabet];
    if (data[4]%4u) {
        size_t at=data[4]%4u==1?0:data[4]%4u==2?nn/2:nn-1;
        n[at]=0xff;
    }
    if (data[5]&1u) memcpy(h+17,n,nn);
    if (data[6]&1u) memcpy(h+nn+33,n,nn);
    prefix[0]=0;
    for (size_t i=1;i<nn;i++) {
        size_t matched=prefix[i-1];
        while (matched && !literal_equal(n[i],n[matched],options)) matched=prefix[matched-1];
        if (literal_equal(n[i],n[matched],options)) matched++;
        prefix[i]=matched;
    }
    find_fixture_result want,got;
    (void)long_model(h,hn,n,nn,prefix,0,options,&want);
    find_source flat={h,hn,NULL};
    ASSERT(find_fixture_literal(&flat,n,nn,options,NULL,&got)==FIND_OK);
    ASSERT(got.total==want.total && got.stored==want.stored);
    ASSERT(memcmp(got.offsets,want.offsets,want.stored*sizeof want.offsets[0])==0);
    piece_reserve *reserve;
    piece_tree *tree;
    ASSERT(piece_reserve_create(&reserve)==PIECE_OK);
    ASSERT(piece_open_copy(reserve,NULL,0,&tree)==PIECE_OK);
    size_t chunk=16+data[7]%16u;
    for (size_t end=hn;end;) {
        size_t lo=end>chunk?end-chunk:0;
        ASSERT(piece_replace(tree,0,0,h+lo,end-lo)==PIECE_OK); end=lo;
    }
    piece_snapshot *snapshot;
    ASSERT(piece_snapshot_take(tree,&snapshot)==PIECE_OK);
    piece_close(tree);
    find_source source={.view=piece_snapshot_view(snapshot)};
    ASSERT(find_fixture_literal(&source,n,nn,options,NULL,&got)==FIND_OK);
    ASSERT(got.total==want.total && got.stored==want.stored);
    ASSERT(memcmp(got.offsets,want.offsets,want.stored*sizeof want.offsets[0])==0);
    size_t from=(size_t)data[size-1]*(hn+1)/256;
    uint64_t next=long_model(h,hn,n,nn,prefix,from,options,&want); find_match match;
    ASSERT(find_literal_next(&source,n,nn,options,from,NULL,&match)==FIND_OK);
    ASSERT(match.matched==(next!=FIND_UNSET));
    if (match.matched) ASSERT(match.whole.start==next && match.whole.end==next+nn);
    atomic_bool stop; atomic_init(&stop,true); find_control control={0}; control.cancel=&stop;
    ASSERT(find_fixture_literal(&source,n,nn,options,&control,&got)==FIND_CANCELLED && got.total==0 && got.stored==0);
    ASSERT(find_literal_next(&source,n,nn,options,from,&control,&match)==FIND_CANCELLED && !match.matched && match.groups==0 && match.whole.start==FIND_UNSET && match.whole.end==FIND_UNSET);
    piece_snapshot_release(snapshot);
    piece_reserve_destroy(reserve);
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    if(size==0) return 0;
    /* (P) Two high bits select fold and whole-word independently. The low
     * six bits retain the short length and long-operation selector. */
    find_literal_options options={.fold_ascii=(data[0]&0x80u)!=0,.whole_word=(data[0]&0x40u)!=0};
    long_snapshot_op(data,size,options);
    size_t hn=(data[0]&63u)%33u; if(hn>size-1) hn=size-1;
    find_source source={data+1,hn,NULL};
    const uint8_t *needle=data+1+hn; size_t nn=size-1-hn; if(nn>40) nn=40;
    find_fixture_result r; ASSERT(find_fixture_literal(&source,needle,nn,options,NULL,&r)==FIND_OK);
    uint64_t count=0;
    if(nn) for(size_t i=0;i+nn<=hn;) {
        size_t k=0; while(k<nn && literal_equal(source.bytes[i+k],needle[k],options)) k++;
        bool word=(!i || !model_word_byte(source.bytes[i-1])) &&
                  (i+nn==hn || !model_word_byte(source.bytes[i+nn]));
        if(k==nn && (!options.whole_word || word)) {
            ASSERT(r.offsets[count]==i); count++; i+=nn;
        } else i++;
    }
    ASSERT(r.total==count && r.stored==count);
    model m={0}; m.input=needle; m.input_n=size-1-hn; m.text=source.bytes; m.tn=hn;
    m.fold=options.fold_ascii;
    (void)generate(&m,2); encode(&m,0);
    base_arena arena; ASSERT(base_arena_init(&arena,512*1024)==0);
    void *memory=base_arena_alloc(&arena,find_regex_bytes(),_Alignof(max_align_t));
    find_regex *re=NULL; size_t error;
    uint8_t expansion[FIND_REGEX_FOLD_BUFFER_BYTES];
    find_code code=find_regex_compile_options(memory,find_regex_bytes(),m.pattern,m.pn,m.fold,
                                               expansion,sizeof expansion,NULL,&re,&error);
    ASSERT(code==FIND_OK || code==FIND_ERR_LIMIT);
    if(code==FIND_OK) {
        size_t sn=find_regex_stream_scratch_bytes(re); void *scratch=base_arena_alloc(&arena,sn,_Alignof(max_align_t)); ASSERT(scratch);
        ASSERT(find_fixture_regex(&source,re,scratch,sn,NULL,&r)==FIND_OK);
        count=0; size_t off=0,start=0,end=0;
        for(;;) {
            bool found=model_next(&m,off,&start,&end); find_match hit;
            ASSERT(find_fixture_regex_next(&source,re,off,scratch,sn,NULL,&hit)==FIND_OK && hit.matched==found);
            if(!found) break;
            ASSERT(hit.whole.start==start && hit.whole.end==end && r.offsets[count]==start);
            find_match anchored;
            ASSERT(find_regex_captures(&source,re,start,scratch,sn,NULL,&anchored)==FIND_OK);
            ASSERT(anchored.matched && anchored.whole.end==end);
            count++; off=end;
            if(start==end) { if(off==hn) break; off++; }
        }
        ASSERT(r.total==count && r.stored==count);
        word_visit_model(&m,&source,re,scratch,sn);
        /* Same bytes in many pieces: matches/anchors may cross every boundary. */
        if((data[0]&15u)==0 || options.fold_ascii || options.whole_word) {
            piece_reserve *reserve;
            piece_tree *t;
            piece_snapshot *snap;
            ASSERT(piece_reserve_create(&reserve)==PIECE_OK);
            ASSERT(piece_open_copy(reserve,NULL,0,&t)==PIECE_OK);
            for(size_t i=hn;i>0;i--) ASSERT(piece_replace(t,0,0,source.bytes+i-1,1)==PIECE_OK);
            ASSERT(piece_snapshot_take(t,&snap)==PIECE_OK);
            piece_close(t);
            find_source ss={.view=piece_snapshot_view(snap)}; find_fixture_result other;
            ASSERT(find_fixture_regex(&ss,re,scratch,sn,NULL,&other)==FIND_OK && other.total==r.total && other.stored==r.stored);
            for(size_t i=0;i<r.stored;i++) ASSERT(other.offsets[i]==r.offsets[i]);
            word_visit_model(&m,&ss,re,scratch,sn);
            ASSERT(find_fixture_literal(&ss,needle,nn,options,NULL,&other)==FIND_OK);
            find_fixture_result bytes_result; ASSERT(find_fixture_literal(&source,needle,nn,options,NULL,&bytes_result)==FIND_OK);
            ASSERT(other.total==bytes_result.total && other.stored==bytes_result.stored);
            for(size_t i=0;i<other.stored;i++) ASSERT(other.offsets[i]==bytes_result.offsets[i]);
            piece_snapshot_release(snap);
            piece_reserve_destroy(reserve);
        }
    }
    /* Arbitrary syntax: errors are bounded, successful programs are searchable. */
    size_t raw_n=size>FIND_MAX_PATTERN?FIND_MAX_PATTERN:size;
    code=find_fixture_compile(memory,find_regex_bytes(),data,raw_n,&re,&error);
    ASSERT(error<=raw_n && (code==FIND_OK || re==NULL));
    if(code==FIND_OK) {
        base_arena_reset_to_mark(&arena,find_regex_bytes());
        size_t sn=find_regex_scratch_bytes(re); void *scratch=base_arena_alloc(&arena,sn,_Alignof(max_align_t)); ASSERT(scratch);
        ASSERT(find_fixture_regex(&source,re,scratch,sn,NULL,&r)==FIND_OK);
        ASSERT(r.stored<=FIND_FIXTURE_OFFSETS && r.total<=hn+1);
        for(size_t i=0;i<r.stored;i++) ASSERT(r.offsets[i]<=hn && (i==0 || r.offsets[i]>r.offsets[i-1]));
    }
    base_arena_free(&arena); return 0;
}
