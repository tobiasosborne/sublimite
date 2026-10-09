#include "undo/undo.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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
#define must(yes) EDIT_ASSERT(yes)
static void drop(model *m) {
    /* Explicit groups defer cap trimming until end, including old history. */
    if(m->open) return;
    size_t records=0; for(size_t i=0;i<m->n;i++) records+=m->g[i].records;
    size_t work=0;
    while(records>m->cap && m->n && work<UNDO_RECLAIM_RECORDS) {
        if(m->open && m->n==1) break;
        if(m->pos==0) { m->n=0; m->burst=0; break; }
        records-=m->g[0].records; memmove(m->g,m->g+1,(m->n-1)*sizeof *m->g);m->n--;m->pos--;work++;if(!m->n) m->burst=0;
    }
    if(records>m->cap && m->n && (!m->open || m->n>1)) {
        if(m->open) { m->g[0]=m->g[m->n-1];m->n=m->pos=1; }
        else { m->n=m->pos=0;m->burst=0; }
    }
}
typedef struct fuzz_allocator { piece_allocator backing; size_t calls,fail; int persistent; } fuzz_allocator;
static void *fuzz_alloc(void *ctx,size_t n) {
    fuzz_allocator *f=ctx;f->calls++;
    if(f->fail && (f->persistent?f->calls>=f->fail:f->calls==f->fail)) return NULL;
    return f->backing.alloc(f->backing.ctx,n);
}
static void fuzz_free(void *ctx,void *p,size_t n) {
    fuzz_allocator *f=ctx;f->backing.free(f->backing.ctx,p,n);
}
static void completed(model *m,int reverse,size_t count,undo_state *expected) {
    for(size_t j=0;j<count;j++) {
        must(reverse?m->pos>0:m->pos<m->n);
        model_group *g=reverse?&m->g[--m->pos]:&m->g[m->pos++];
        m->len=reverse?g->bn:g->an;memcpy(m->bytes,reverse?g->before:g->after,m->len);
        *expected=reverse?g->bs:g->as;
    }
}
static void fuzz_replay(undo_log *u,model *m,fuzz_allocator *f,int reverse,size_t requested,uint8_t seed,uint8_t work) {
    size_t remaining=requested,rounds=0;int inject=1;
    while(remaining) {
        uint8_t before[MAX_BYTES],after[MAX_BYTES];size_t bn=(size_t)piece_len(u->tree);
        must(bn<=MAX_BYTES && piece_read(u->tree,0,before,bn)==0);
        f->calls=0;f->fail=inject?1u+seed%8u:0;f->persistent=(seed&16u)!=0;inject=0;
        undo_change c;size_t budget=1u+work%8u;
        int rc=reverse?undo_undo_slice(u,remaining,budget,UINT64_MAX,&c):undo_redo_slice(u,remaining,budget,UINT64_MAX,&c);
        f->fail=0;
        must(rc==0 || rc==UNDO_MORE || rc==UNDO_ERR_NOMEM);
        must(c.operations<=budget && c.records<=PIECE_REF_SPANS*c.operations && c.groups<=remaining);
        size_t an=(size_t)piece_len(u->tree);must(an<=MAX_BYTES && piece_read(u->tree,0,after,an)==0);
        if(!c.records) must(bn==an && memcmp(before,after,bn)==0 && c.len==0);
        for(size_t j=0;j<(bn<an?bn:an);j++) if(before[j]!=after[j]) must(j>=c.off && j<c.off+c.len);
        if(bn!=an) must(c.records && c.off+c.len>=(bn>an?bn:an));
        undo_state expected={{0}};completed(m,reverse,c.groups,&expected);
        must(c.has_state==(c.groups!=0));if(c.has_state) must(memcmp(&c.state,&expected,sizeof expected)==0);
        remaining-=c.groups;
        if(u->partial) {
            const piece_snapshot *view=undo_replay_snapshot(u);uint8_t stable[MAX_BYTES];
            must(view && piece_snapshot_len(view)==m->len && piece_snapshot_read(view,0,stable,m->len)==0 && memcmp(stable,m->bytes,m->len)==0);
        }
        if(rc==UNDO_ERR_NOMEM && (seed&16u)) {
            f->calls=0;f->fail=1;f->persistent=1;
            for(size_t retry=0;retry<3;retry++) {
                undo_change failed;
                int retry_rc=reverse?undo_undo_slice(u,remaining,budget,UINT64_MAX,&failed):undo_redo_slice(u,remaining,budget,UINT64_MAX,&failed);
                must(retry_rc==UNDO_ERR_NOMEM && failed.records==0 && failed.groups==0 && !failed.has_state);
                uint8_t unchanged[MAX_BYTES];must(piece_len(u->tree)==an && piece_read(u->tree,0,unchanged,an)==0 && memcmp(unchanged,after,an)==0);
            }
            f->fail=0;
        }
        must(++rounds<=8192);if(rc==0) break;
    }
    m->burst=0;
}
/* The reviewed near-capacity fragmentation script, with varied noise bytes.
 * A healthy allocator must never strand expansion inside its two-op group. */
static void fragmented_capacity(uint8_t seed) {
    piece_allocator a=piece_default_allocator();piece_tree *t=piece_create(&a);must(t!=NULL);
    undo_log u;must(undo_init(&u,t,12)==0);undo_state st={{0}};undo_change c;
    must(piece_init_copy(t,(const uint8_t *)"0123456789",10)==0);
    must(undo_group_begin(&u,&st)==0);
    must(undo_insert(&u,10,(const uint8_t *)"Z",1,0,&st,&st)==0);
    must(undo_insert(&u,5,(const uint8_t *)"abcdefghij",10,1,&st,&st)==0);
    must(undo_group_end(&u,&st)==0);
    for(size_t j=0;j<9;j++) must(undo_insert(&u,0,&seed,1,j+2,&st,&st)==0);
    must(undo_delete(&u,0,17,UNDO_DELETE,20,&st,&st)==0);
    must(undo_undo(&u,10,&c)==0 && c.groups==10);
    int rc=undo_undo_slice(&u,1,1,UINT64_MAX,&c);must(rc==UNDO_MORE && c.operations==1);
    must(undo_undo_slice(&u,1,1,UINT64_MAX,&c)==0 && c.groups==1);
    uint8_t text[10];must(piece_len(t)==10 && piece_read(t,0,text,10)==0 && memcmp(text,"0123456789",10)==0);
    undo_destroy(&u);piece_destroy(t);
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size) {
    if(size>2048) size=2048;
    int wide=size && (data[0]&1u);
    if(size>4 && (data[0]&240u)==240u) fragmented_capacity(data[1]);
    model *m=calloc(1,sizeof *m); must(m!=NULL);m->cap=wide?8192:32;
    fuzz_allocator allocator={piece_default_allocator(),0,0,0};
    piece_allocator a={&allocator,fuzz_alloc,fuzz_free};piece_tree *t=piece_create(&a);must(t!=NULL);
    undo_log u;must(undo_init(&u,t,wide?8192:32)==0);must(undo_set_cap(&u,m->cap)==0);
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
            uint8_t inserted[8];for(size_t j=0;j<len;j++) inserted[j]=(uint8_t)(data[i+2]+j);
            int edit_rc=kind==UNDO_INSERT?undo_insert(&u,off,inserted,len,tm,&before,&after):
                undo_delete(&u,off,len,kind,tm,&before,&after);
            if(edit_rc) {
                must(edit_rc==UNDO_ERR_NOMEM);
                uint8_t unchanged[MAX_BYTES];
                must(piece_len(t)==m->len && piece_read(t,0,unchanged,m->len)==0 && memcmp(unchanged,m->bytes,m->len)==0);
                continue;
            }
            m->n=m->pos;
            if(!join) {
                must(m->n<MAX_GROUPS);model_group *g=&m->g[m->n++];memset(g,0,sizeof *g);g->bn=m->len;memcpy(g->before,m->bytes,m->len);g->bs=m->open?m->before:before;
            }
            model_group *g=&m->g[m->n-1];g->records++;
            if(kind==UNDO_INSERT) {
                memmove(m->bytes+off+len,m->bytes+off,m->len-(size_t)off);memcpy(m->bytes+off,inserted,len);m->len+=len;
            } else {
                memmove(m->bytes+off,m->bytes+off+len,m->len-(size_t)off-len);m->len-=len;
            }
            g->an=m->len;memcpy(g->after,m->bytes,m->len);g->as=after;m->pos=m->n;
            if(m->open) m->open_edit=1; m->burst=!m->open;m->off=off;m->kind=kind;m->tm=tm;m->last_len=len;drop(m);
        } else if(op==4 || op==5) {
            size_t n=1u+data[i+1]%4u;
            if(m->open) {
                undo_change c;must((op==4?undo_undo(&u,n,&c):undo_redo(&u,n,&c))==UNDO_ERR_BUSY);
            } else fuzz_replay(&u,m,&allocator,op==4,n,data[i+2],data[i+3]);
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
        undo_stats st=undo_get_stats(&u);must(st.live_bytes==(st.records+st.retired_records)*64);
        must(st.committed_bytes>=st.live_bytes && st.committed_bytes-st.live_bytes<u.page_bytes);
        if(!m->open) {
            if(st.undo_groups!=m->pos || st.redo_groups!=m->n-m->pos)
                fprintf(stderr,"history mismatch at=%zu op=%u cap=%zu undo=%zu/%zu redo=%zu/%zu records=%zu retired=%zu\n",
                    i,(unsigned)op,m->cap,st.undo_groups,m->pos,st.redo_groups,m->n-m->pos,st.records,st.retired_records);
            must(st.undo_groups==m->pos && st.redo_groups==m->n-m->pos);
        }
    }
    undo_destroy(&u);piece_destroy(t);free(m);return 0;
}
