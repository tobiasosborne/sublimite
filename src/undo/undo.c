#include "undo/undo.h"
#include <string.h>
/* Two tag bits in each link keep both 16-byte cursor states in 64 bytes. */
#define INDEX UINT32_C(0x3fffffff)
#define START UINT32_C(0x40000000)
#define INSERT UINT32_C(0x80000000)
#define END UINT32_C(0x40000000)
#define KNOWN UINT32_C(0x80000000)
typedef struct undo_record {
    uint64_t off, len, add_off;
    uint32_t prev, next;
    undo_state before, after;
} undo_record;
_Static_assert(sizeof(undo_record)==64,"G10f: 64 B per span record");
static undo_record *record(const undo_log *u,uint32_t id) {
    return (undo_record *)(u->pool.base+((size_t)id-1)*u->pool.stride);
}
static uint32_t prev_id(const undo_record *r) { return r->prev&INDEX; }
static uint32_t next_id(const undo_record *r) { return r->next&INDEX; }
static void set_prev(undo_record *r,uint32_t id) { r->prev=(r->prev&~INDEX)|id; }
static void set_next(undo_record *r,uint32_t id) { r->next=(r->next&~INDEX)|id; }
static uint32_t take(undo_log *u) {
    void *p=edit_pool_get(&u->pool);
    if(!p) return 0;
    return (uint32_t)((size_t)((uint8_t *)p-u->pool.base)/u->pool.stride)+1;
}
static void put(undo_log *u,uint32_t id) { (void)edit_pool_put(&u->pool,record(u,id)); }
static void clear_redo(undo_log *u) {
    uint32_t id=u->cursor?next_id(record(u,u->cursor)):u->head;
    while(id) { uint32_t next=next_id(record(u,id));put(u,id);u->count--;id=next; }
    u->tail=u->cursor;
    if(u->cursor) set_next(record(u,u->cursor),0);else u->head=0;
}
void undo_clear(undo_log *u) {
    u->cursor=0;clear_redo(u);u->burst=u->open=u->partial=0;u->open_first=0;
}
int undo_init(undo_log *u,piece_tree *tree,size_t max_records) {
    memset(u,0,sizeof *u);
    if(!tree || !max_records || max_records>(size_t)INDEX-8) return UNDO_ERR_RANGE;
    if(edit_pool_init(&u->pool,sizeof(undo_record),8,max_records+8)!=0) return UNDO_ERR_NOMEM;
    u->tree=tree;u->cap=u->max_records=max_records;return 0;
}
void undo_destroy(undo_log *u) { edit_pool_free(&u->pool);memset(u,0,sizeof *u); }
void undo_break_burst(undo_log *u) { u->burst=0; }
/* During an explicit group its first record is protected. */
static void trim(undo_log *u) {
    while(u->count>u->cap && u->head) {
        if(u->open && u->head==u->open_first) break;
        if(!u->cursor) { clear_redo(u);u->burst=0;break; }
        uint32_t id=u->head;
        for(;;) {
            undo_record *r=record(u,id);uint32_t next=next_id(r);int end=(r->next&END)!=0;
            if(id==u->cursor) u->cursor=0;
            put(u,id);u->count--;id=next;
            if(end) break;
        }
        u->head=id;
        if(id) set_prev(record(u,id),0);else u->tail=0;
        if(!u->tail) u->burst=0;
    }
}
int undo_set_cap(undo_log *u,size_t records) {
    if(u->open || u->partial) return UNDO_ERR_BUSY;
    if(!records || records>u->max_records) return UNDO_ERR_RANGE;
    u->cap=records;u->burst=0;trim(u);return 0;
}
int undo_group_begin(undo_log *u,const undo_state *before) {
    if(u->open || u->partial) return UNDO_ERR_BUSY;
    if(!before) return UNDO_ERR_RANGE;
    u->open=1;u->open_first=0;u->open_before=*before;u->burst=0;return 0;
}
int undo_group_end(undo_log *u,const undo_state *after) {
    if(!u->open) return UNDO_ERR_BUSY;
    if(!after) return UNDO_ERR_RANGE;
    if(u->open_first) { record(u,u->open_first)->before=u->open_before;record(u,u->tail)->after=*after; }
    u->open=0;u->open_first=0;u->burst=0;trim(u);return 0;
}
static int joins(const undo_log *u,undo_kind kind,uint64_t off,uint64_t len,uint64_t tm) {
    if(u->open) return u->open_first!=0;
    if(!u->burst || !u->tail || u->cursor!=u->tail || kind!=u->last_kind || tm<u->last_time || tm-u->last_time>UNDO_BURST_NS) return 0;
    if(kind==UNDO_INSERT) return off==u->last_off+u->last_len;
    if(kind==UNDO_BACKSPACE) return len<=u->last_off && off==u->last_off-len;
    return off==u->last_off;
}
static int reserve(undo_log *u,uint32_t ids[PIECE_REF_SPANS]) {
    for(size_t i=0;i<PIECE_REF_SPANS;i++) {
        ids[i]=take(u);
        if(!ids[i]) { while(i) put(u,ids[--i]);return UNDO_ERR_NOMEM; }
    }
    return 0;
}
static void append_edit(undo_log *u,const uint32_t *ids,size_t n,const piece_ref *ref,
                        uint64_t off,uint64_t len,undo_kind kind,uint64_t tm,
                        const undo_state *before,const undo_state *after) {
    int join=joins(u,kind,off,len,tm);
    clear_redo(u);
    if(join) record(u,u->tail)->next&=~END;
    for(size_t i=0;i<n;i++) {
        undo_record *r=record(u,ids[i]);memset(r,0,sizeof *r);
        r->off=off;r->len=ref?ref->span[i].len:len;
        r->add_off=ref?ref->span[i].add_off:0;
        r->prev=u->tail|(kind==UNDO_INSERT?INSERT:0)|(!join && i==0?START:0);
        r->next=(i+1==n?END:0)|(ref?KNOWN:0);
        r->before=(u->open && !join)?u->open_before:*before;r->after=*after;
        if(u->tail) set_next(record(u,u->tail),ids[i]);else u->head=ids[i];
        u->tail=ids[i];u->count++;
    }
    u->cursor=u->tail;
    if(u->open && !u->open_first) u->open_first=ids[0];
    u->last_time=tm;u->last_off=off;u->last_len=len;u->last_kind=kind;u->burst=!u->open;
    if(!u->open) trim(u);
}
static int edit(undo_log *u,uint64_t off,const uint8_t *data,uint64_t len,
                undo_kind kind,uint64_t tm,const undo_state *before,const undo_state *after) {
    if(u->partial) return UNDO_ERR_BUSY;
    if(!before || !after || (kind==UNDO_INSERT && len && !data)) return UNDO_ERR_RANGE;
    uint64_t total=piece_len(u->tree);
    if(off>total || (kind!=UNDO_INSERT && len>total-off) || (kind==UNDO_INSERT && len>UINT64_MAX-total)) return UNDO_ERR_RANGE;
    if(!len) return 0;
    uint32_t ids[PIECE_REF_SPANS];int rc=reserve(u,ids);if(rc) return rc;
    piece_ref ref;size_t n=1;
    if(kind==UNDO_INSERT) rc=piece_insert(u->tree,off,data,(size_t)len);
    else { rc=piece_delete(u->tree,off,len,&ref);if(!rc) n=ref.nspans; }
    if(!rc) append_edit(u,ids,n,kind==UNDO_INSERT?NULL:&ref,off,len,kind,tm,before,after);
    for(size_t i=rc?0:n;i<PIECE_REF_SPANS;i++) put(u,ids[i]);
    return rc;
}
int undo_insert(undo_log *u,uint64_t off,const uint8_t *data,size_t len,uint64_t tm,const undo_state *before,const undo_state *after) {
    return edit(u,off,data,len,UNDO_INSERT,tm,before,after);
}
int undo_delete(undo_log *u,uint64_t off,uint64_t len,undo_kind kind,uint64_t tm,const undo_state *before,const undo_state *after) {
    if(kind!=UNDO_BACKSPACE && kind!=UNDO_DELETE) return UNDO_ERR_RANGE;
    return edit(u,off,NULL,len,kind,tm,before,after);
}
/* Capture an insertion's redo ref on its first undo. A ref may have become
 * fragmented by intervening deletes which piece flattened into fresh bytes.
 * Expand to span records only AFTER the atomic delete succeeds. */
static uint32_t capture(undo_log *u,uint32_t id,const piece_ref *ref,const uint32_t *ids) {
    undo_record old=*record(u,id);uint32_t first=id,tail=id;
    uint64_t off=old.off;
    for(uint32_t i=0;i<ref->nspans;i++) {
        uint32_t current=i?ids[i-1]:id;undo_record *r=record(u,current);
        *r=old;r->off=off;r->len=ref->span[i].len;r->add_off=ref->span[i].add_off;
        r->prev=INSERT|(i?tail:prev_id(&old))|(i==0?(old.prev&START):0);
        r->next=KNOWN|(i+1==ref->nspans?(old.next&(INDEX|END)):0);
        if(i) set_next(record(u,tail),current);
        tail=current;off+=r->len;
    }
    uint32_t next=next_id(&old);
    if(next) set_prev(record(u,next),tail);else u->tail=tail;
    u->count+=(size_t)ref->nspans-1;
    return first;
}
static void dirty(undo_change *c,uint64_t off,uint64_t end) {
    if(!c->records) { c->off=off;c->len=end-off;return; }
    uint64_t old_end=c->off+c->len;
    if(off<c->off) c->off=off;
    if(end<old_end) end=old_end;
    c->len=end-c->off;
}
static int replay(undo_log *u,size_t groups,undo_change *c,int direction) {
    if(!c) return UNDO_ERR_RANGE;
    memset(c,0,sizeof *c);
    if(u->open || (u->partial && u->partial!=direction)) return UNDO_ERR_BUSY;
    u->burst=0;
    while(c->groups<groups) {
        uint32_t id=direction<0?u->cursor:(u->cursor?next_id(record(u,u->cursor)):u->head);
        if(!id) break;
        undo_record *r=record(u,id);uint64_t before_len=piece_len(u->tree),off=r->off;
        int remove=((r->prev&INSERT)!=0)==(direction<0),rc;
        size_t applied=1;
        if(remove) {
            uint32_t ids[PIECE_REF_SPANS];int unknown=(r->next&KNOWN)==0;
            if(unknown) { rc=reserve(u,ids);if(rc) return rc; }
            piece_ref ref;rc=piece_delete(u->tree,r->off,r->len,&ref);
            if(unknown) {
                size_t used=0;
                if(!rc) { id=capture(u,id,&ref,ids);used=(size_t)ref.nspans-1;applied=ref.nspans; }
                for(size_t i=used;i<PIECE_REF_SPANS;i++) put(u,ids[i]);
            }
        } else {
            piece_ref ref={0};ref.nspans=1;ref.len=r->len;ref.span[0].add_off=r->add_off;ref.span[0].len=r->len;
            rc=piece_insert_ref(u->tree,r->off,&ref);
        }
        if(rc) return rc;
        r=record(u,id);uint64_t after_len=piece_len(u->tree);
        dirty(c,off,before_len>after_len?before_len:after_len);c->records+=applied;
        int complete=direction<0?(r->prev&START)!=0:(r->next&END)!=0;
        u->cursor=direction<0?prev_id(r):id;u->partial=complete?0:direction;
        if(complete) { c->groups++;c->has_state=1;c->state=direction<0?r->before:r->after;trim(u); }
    }
    return 0;
}
int undo_undo(undo_log *u,size_t groups,undo_change *c) { return replay(u,groups,c,-1); }
int undo_redo(undo_log *u,size_t groups,undo_change *c) { return replay(u,groups,c,1); }
undo_stats undo_get_stats(const undo_log *u) {
    undo_stats s={0};s.records=u->count;s.live_bytes=edit_pool_count_live(&u->pool)*u->pool.stride;s.reserved_bytes=u->pool.map_bytes;
    int applied=u->cursor!=0;
    for(uint32_t id=u->head;id;id=next_id(record(u,id))) {
        const undo_record *r=record(u,id);
        if(applied && (r->next&END)) s.undo_groups++;
        if(!applied && (r->prev&START)) s.redo_groups++;
        if(id==u->cursor) applied=0;
    }
    return s;
}
