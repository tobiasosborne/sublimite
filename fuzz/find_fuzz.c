/* Frozen P1.10a: independent endpoint-set regex model, no NFA/shared parser.
 * Generated grammar plus arbitrary compile inputs, binary literal differential,
 * snapshot parity and pagination. Keep subjects small for exhaustive closure. */
#include "find/find.h"
#include "base/base.h"
#include <string.h>
#include <stdlib.h>

typedef enum model_kind { M_LITERAL, M_CLASS, M_DOT, M_BOL, M_EOL, M_EMPTY,
                          M_CAT, M_ALT, M_STAR, M_PLUS, M_OPT } model_kind;
typedef struct node { model_kind kind; int left,right; uint8_t byte,cls; } node;
typedef struct model {
    node nodes[32]; size_t used;
    const uint8_t *input; size_t input_n,at;
    uint8_t pattern[512]; size_t pn;
    const uint8_t *text; size_t tn;
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
static void put(model *m,uint8_t c) { EDIT_ASSERT(m->pn<sizeof m->pattern); m->pattern[m->pn++]=c; }
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
    case M_LITERAL: if(pos<m->tn && m->text[pos]==n->byte) out=bit<<1; break;
    case M_CLASS:
        if(pos<m->tn) {
            uint8_t c=m->text[pos]; bool yes=n->cls==0?(c>='a' && c<='c'):n->cls==1?(c!='a'):n->cls==2?(c>=0x80):(c>='-' && c<='0');
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
/* P1.10b gap: the small endpoint-set subjects cannot enter the long-needle
 * snapshot verifier. Generate bounded periodic/critical-prefix subjects and
 * compare against an independent KMP model, also from arbitrary next offsets. */
static uint64_t long_model(const uint8_t *h,size_t hn,const uint8_t *n,size_t nn,
                          const size_t *prefix,size_t from,find_result *out)
{
    out->total=0; out->stored=0;
    size_t matched=0;
    for (size_t i=from;i<hn;i++) {
        while (matched && h[i]!=n[matched]) matched=prefix[matched-1];
        if (h[i]==n[matched]) matched++;
        if (matched==nn) {
            if (out->stored<FIND_MAX_OFFSETS) out->offsets[out->stored++]=i+1-nn;
            out->total++; matched=0; /* non-overlapping */
        }
    }
    return out->stored?out->offsets[0]:FIND_UNSET;
}
static void long_snapshot_op(const uint8_t *data,size_t size)
{
    if (size<8 || (data[0]&63u)!=0) return;
    uint8_t h[16448],n[8192]; size_t prefix[8192];
    size_t nn=2049+(((size_t)data[1]<<8)|data[2])%6144,hn=2*nn+64;
    size_t period=1+data[3]%7u;
    for (size_t i=0;i<hn;i++) h[i]=(uint8_t)(data[(4+i%period)%size]%4u);
    for (size_t i=0;i<nn;i++) n[i]=(uint8_t)(data[(4+i%period)%size]%4u);
    if (data[4]%4u) {
        size_t at=data[4]%4u==1?0:data[4]%4u==2?nn/2:nn-1;
        n[at]=0xff;
    }
    if (data[5]&1u) memcpy(h+17,n,nn);
    if (data[6]&1u) memcpy(h+nn+33,n,nn);
    prefix[0]=0;
    for (size_t i=1;i<nn;i++) {
        size_t matched=prefix[i-1];
        while (matched && n[i]!=n[matched]) matched=prefix[matched-1];
        if (n[i]==n[matched]) matched++;
        prefix[i]=matched;
    }
    find_result want,got;
    (void)long_model(h,hn,n,nn,prefix,0,&want);
    find_source flat={h,hn,NULL};
    EDIT_ASSERT(find_literal(&flat,n,nn,NULL,&got)==FIND_OK);
    EDIT_ASSERT(got.total==want.total && got.stored==want.stored);
    EDIT_ASSERT(memcmp(got.offsets,want.offsets,want.stored*sizeof want.offsets[0])==0);
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); EDIT_ASSERT(tree);
    size_t chunk=16+data[7]%16u;
    for (size_t end=hn;end;) {
        size_t lo=end>chunk?end-chunk:0;
        EDIT_ASSERT(piece_insert(tree,0,h+lo,end-lo)==PIECE_OK); end=lo;
    }
    piece_snapshot *snapshot=piece_snapshot_take(tree); EDIT_ASSERT(snapshot); piece_destroy(tree);
    find_source source={NULL,0,snapshot};
    EDIT_ASSERT(find_literal(&source,n,nn,NULL,&got)==FIND_OK);
    EDIT_ASSERT(got.total==want.total && got.stored==want.stored);
    EDIT_ASSERT(memcmp(got.offsets,want.offsets,want.stored*sizeof want.offsets[0])==0);
    size_t from=(size_t)data[size-1]*(hn+1)/256;
    uint64_t next=long_model(h,hn,n,nn,prefix,from,&want); find_match match;
    EDIT_ASSERT(find_literal_next(&source,n,nn,from,NULL,&match)==FIND_OK);
    EDIT_ASSERT(match.matched==(next!=FIND_UNSET));
    if (match.matched) EDIT_ASSERT(match.whole.start==next && match.whole.end==next+nn);
    atomic_bool stop; atomic_init(&stop,true); find_control control={0}; control.cancel=&stop;
    EDIT_ASSERT(find_literal(&source,n,nn,&control,&got)==FIND_CANCELLED && got.total==0 && got.stored==0);
    EDIT_ASSERT(find_literal_next(&source,n,nn,from,&control,&match)==FIND_CANCELLED && !match.matched && match.groups==0 && match.whole.start==FIND_UNSET && match.whole.end==FIND_UNSET);
    piece_snapshot_release(snapshot);
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    if(size==0) return 0;
    long_snapshot_op(data,size);
    size_t hn=(size_t)(data[0]%33u); if(hn>size-1) hn=size-1;
    find_source source={data+1,hn,NULL};
    const uint8_t *needle=data+1+hn; size_t nn=size-1-hn; if(nn>40) nn=40;
    find_result r; EDIT_ASSERT(find_literal(&source,needle,nn,NULL,&r)==FIND_OK);
    uint64_t count=0;
    if(nn) for(size_t i=0;i+nn<=hn;) {
        size_t k=0; while(k<nn && source.bytes[i+k]==needle[k]) k++;
        if(k==nn) { EDIT_ASSERT(r.offsets[count]==i); count++; i+=nn; } else i++;
    }
    EDIT_ASSERT(r.total==count && r.stored==count);
    model m={0}; m.input=needle; m.input_n=size-1-hn; m.text=source.bytes; m.tn=hn;
    (void)generate(&m,2); encode(&m,0);
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,512*1024)==0);
    void *memory=edit_arena_alloc(&arena,find_regex_bytes(),_Alignof(max_align_t));
    find_regex *re=NULL; size_t error;
    find_code code=find_regex_compile(memory,find_regex_bytes(),m.pattern,m.pn,&re,&error);
    EDIT_ASSERT(code==FIND_OK || code==FIND_ERR_LIMIT);
    if(code==FIND_OK) {
        size_t sn=find_regex_scratch_bytes(re); void *scratch=edit_arena_alloc(&arena,sn,_Alignof(max_align_t)); EDIT_ASSERT(scratch);
        EDIT_ASSERT(find_regex_search(&source,re,scratch,sn,NULL,&r)==FIND_OK);
        count=0; size_t off=0,start=0,end=0;
        for(;;) {
            bool found=model_next(&m,off,&start,&end); find_match hit;
            EDIT_ASSERT(find_regex_next(&source,re,off,scratch,sn,NULL,&hit)==FIND_OK && hit.matched==found);
            if(!found) break;
            EDIT_ASSERT(hit.whole.start==start && hit.whole.end==end && r.offsets[count]==start);
            find_match anchored;
            EDIT_ASSERT(find_regex_captures(&source,re,start,scratch,sn,NULL,&anchored)==FIND_OK);
            EDIT_ASSERT(anchored.matched && anchored.whole.end==end);
            count++; off=end;
            if(start==end) { if(off==hn) break; off++; }
        }
        EDIT_ASSERT(r.total==count && r.stored==count);
        /* Same bytes in many pieces: matches/anchors may cross every boundary. */
        if((data[0]&15u)==0) {
            piece_allocator allocator=piece_default_allocator(); piece_tree *t=piece_create(&allocator); EDIT_ASSERT(t);
            for(size_t i=0;i<hn;i++) EDIT_ASSERT(piece_insert(t,0,source.bytes+hn-1-i,1)==PIECE_OK);
            piece_snapshot *snap=piece_snapshot_take(t); EDIT_ASSERT(snap); piece_destroy(t);
            find_source ss={NULL,0,snap}; find_result other;
            EDIT_ASSERT(find_regex_search(&ss,re,scratch,sn,NULL,&other)==FIND_OK && other.total==r.total && other.stored==r.stored);
            for(size_t i=0;i<r.stored;i++) EDIT_ASSERT(other.offsets[i]==r.offsets[i]);
            EDIT_ASSERT(find_literal(&ss,needle,nn,NULL,&other)==FIND_OK);
            find_result bytes_result; EDIT_ASSERT(find_literal(&source,needle,nn,NULL,&bytes_result)==FIND_OK);
            EDIT_ASSERT(other.total==bytes_result.total && other.stored==bytes_result.stored);
            for(size_t i=0;i<other.stored;i++) EDIT_ASSERT(other.offsets[i]==bytes_result.offsets[i]);
            piece_snapshot_release(snap);
        }
    }
    /* Arbitrary syntax: errors are bounded, successful programs are searchable. */
    size_t raw_n=size>FIND_MAX_PATTERN?FIND_MAX_PATTERN:size;
    code=find_regex_compile(memory,find_regex_bytes(),data,raw_n,&re,&error);
    EDIT_ASSERT(error<=raw_n && (code==FIND_OK || re==NULL));
    if(code==FIND_OK) {
        edit_arena_reset_to_mark(&arena,find_regex_bytes());
        size_t sn=find_regex_scratch_bytes(re); void *scratch=edit_arena_alloc(&arena,sn,_Alignof(max_align_t)); EDIT_ASSERT(scratch);
        EDIT_ASSERT(find_regex_search(&source,re,scratch,sn,NULL,&r)==FIND_OK);
        EDIT_ASSERT(r.stored<=FIND_MAX_OFFSETS && r.total<=hn+1);
        for(size_t i=0;i<r.stored;i++) EDIT_ASSERT(r.offsets[i]<=hn && (i==0 || r.offsets[i]>r.offsets[i-1]));
    }
    edit_arena_free(&arena); return 0;
}
