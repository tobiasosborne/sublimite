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
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    if(size==0) return 0;
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
