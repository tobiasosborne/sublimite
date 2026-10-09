#include "undo/undo.h"
#include <string.h>
#include <stdlib.h>
/* Independent full-copy history. Narrow mode has one-byte deletes and
 * therefore one span per edit (exact cap accounting). Wide mode includes
 * multi-byte deletes; it uses a cap larger than the entire input can fill.
 * Multi-span cap eviction and ref expansion also have dedicated unit tests. */
#define MAX_BYTES 1024
#define MAX_GROUPS 1024
typedef struct model_group { uint8_t before[MAX_BYTES],after[MAX_BYTES]; size_t bn,an,records; undo_state bs,as; } model_group;
typedef struct model {
    uint8_t bytes[MAX_BYTES]; size_t len,n,pos,cap; model_group g[MAX_GROUPS];
    int open,open_edit,burst; uint64_t tm,off,last_len; undo_kind kind; undo_state before;
} model;
static void must(int yes) { EDIT_ASSERT(yes); }
static void drop(model *m) {
    size_t records=0; for(size_t i=0;i<m->n;i++) records+=m->g[i].records;
    while(records>m->cap && m->n) {
        if(m->open && m->n==1) break;
        if(m->pos==0) { m->n=0; m->burst=0; break; }
        records-=m->g[0].records; memmove(m->g,m->g+1,(m->n-1)*sizeof *m->g);m->n--;m->pos--;if(!m->n) m->burst=0;
    }
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size) {
    if(size>2048) size=2048;
    int wide=size && (data[0]&1u);
    model *m=calloc(1,sizeof *m); must(m!=NULL);m->cap=wide?8192:32;
    piece_allocator a=piece_default_allocator();piece_tree *t=piece_create(&a);must(t!=NULL);
    undo_log u;must(undo_init(&u,t,8192)==0);must(undo_set_cap(&u,m->cap)==0);
    undo_state zero={{0}};
    for(size_t i=0;i+3<size;i+=4) {
        uint8_t op=data[i]%10; uint64_t tm=m->tm+(uint64_t)data[i+3]*3000000;
        undo_state before={{0}},after={{0}};before.bytes[0]=data[i+1];after.bytes[0]=data[i+2];
        if(op<4 && m->len<MAX_BYTES-8) {
            undo_kind kind=op==0?UNDO_BACKSPACE:op==1?UNDO_DELETE:UNDO_INSERT;
            if(kind!=UNDO_INSERT && !m->len) continue;
            size_t len=wide?1u+data[i+2]%8u:1u;
            if(kind!=UNDO_INSERT && len>m->len) len=m->len;
            uint64_t off=(uint64_t)data[i+1]%(kind==UNDO_INSERT?m->len+1:m->len-len+1);
            if(m->burst && data[i+2]&1u) {
                if(kind==UNDO_INSERT && m->off+m->last_len<=m->len) off=m->off+m->last_len;
                if(kind==UNDO_DELETE && m->off+len<=m->len) off=m->off;
                if(kind==UNDO_BACKSPACE && m->off>=len) off=m->off-len;
            }
            int join=m->open && m->open_edit;
            if(!m->open) join=m->burst && m->pos==m->n && m->n && kind==m->kind && tm>=m->tm && tm-m->tm<=UNDO_BURST_NS &&
                ((kind==UNDO_INSERT && off==m->off+m->last_len)||(kind==UNDO_BACKSPACE && off+len==m->off)||(kind==UNDO_DELETE && off==m->off));
            m->n=m->pos;
            if(!join) {
                must(m->n<MAX_GROUPS);model_group *g=&m->g[m->n++];memset(g,0,sizeof *g);g->bn=m->len;memcpy(g->before,m->bytes,m->len);g->bs=m->open?m->before:before;
            }
            model_group *g=&m->g[m->n-1];g->records++;
            if(kind==UNDO_INSERT) {
                uint8_t b[8];for(size_t j=0;j<len;j++) b[j]=(uint8_t)(data[i+2]+j);
                must(undo_insert(&u,off,b,len,tm,&before,&after)==0);
                memmove(m->bytes+off+len,m->bytes+off,m->len-(size_t)off);memcpy(m->bytes+off,b,len);m->len+=len;
            } else {
                must(undo_delete(&u,off,len,kind,tm,&before,&after)==0);
                memmove(m->bytes+off,m->bytes+off+len,m->len-(size_t)off-len);m->len-=len;
            }
            g->an=m->len;memcpy(g->after,m->bytes,m->len);g->as=after;m->pos=m->n;
            if(m->open) m->open_edit=1; m->burst=!m->open;m->off=off;m->kind=kind;m->tm=tm;m->last_len=len;drop(m);
        } else if(op==4 || op==5) {
            undo_change c;size_t n=1+data[i+1]%4;
            int rc=op==4?undo_undo(&u,n,&c):undo_redo(&u,n,&c);
            if(m->open) must(rc==UNDO_ERR_BUSY);
            else {
                must(rc==0);size_t count=0;undo_state expected=zero;
                while(count<n && (op==4?m->pos>0:m->pos<m->n)) {
                    model_group *g=op==4?&m->g[--m->pos]:&m->g[m->pos++];
                    m->len=op==4?g->bn:g->an;memcpy(m->bytes,op==4?g->before:g->after,m->len);expected=op==4?g->bs:g->as;count++;
                }
                must(c.groups==count && c.has_state==(count!=0));if(count) must(memcmp(&c.state,&expected,sizeof expected)==0);m->burst=0;
            }
        } else if(op==6) {
            if(!m->open) { must(undo_group_begin(&u,&before)==0);m->open=1;m->open_edit=0;m->before=before;m->burst=0; }
            else { must(undo_group_end(&u,&after)==0);if(m->open_edit) m->g[m->n-1].as=after;m->open=0;m->burst=0;drop(m); }
        } else if(op==7) {
            undo_break_burst(&u);m->burst=0;
        } else if(op==8) {
            size_t cap=wide?8192:1u+data[i+1]%32u;int rc=undo_set_cap(&u,cap);
            if(m->open) must(rc==UNDO_ERR_BUSY);else {must(rc==0);m->cap=cap;drop(m);m->burst=0;}
        } else if(op==9) { undo_clear(&u);m->n=m->pos=0;m->burst=m->open=0; }
        uint8_t b[MAX_BYTES];must(piece_len(t)==m->len && piece_read(t,0,b,m->len)==0 && memcmp(b,m->bytes,m->len)==0);
        undo_stats st=undo_get_stats(&u);must(st.live_bytes==st.records*64);
        if(!m->open) must(st.undo_groups==m->pos && st.redo_groups==m->n-m->pos);
    }
    undo_destroy(&u);piece_destroy(t);free(m);return 0;
}
