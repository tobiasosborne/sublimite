#include "undo/undo.h"
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Two tag bits in each link keep boundary states and group indexing in 64 B.
 * Only the first.before and last.after blobs of a group are public states.
 * In a multi-record group, first.after indexes the last/count, and last.before
 * indexes the first. Interior states are never used by replay. */
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
typedef struct group_index { uint32_t last, unused; uint64_t count; } group_index;
_Static_assert(sizeof(undo_record)==64,"G10f: 64 B per span record");
_Static_assert(sizeof(group_index)==UNDO_STATE_BYTES,"group index fits unused state");
static undo_record *record(const undo_log *u,uint32_t id) {
    return (undo_record *)(u->pool.base+((size_t)id-1)*u->pool.stride);
}
static uint32_t prev_id(const undo_record *r) { return r->prev&INDEX; }
static uint32_t next_id(const undo_record *r) { return r->next&INDEX; }
static void set_prev(undo_record *r,uint32_t id) { r->prev=(r->prev&~INDEX)|id; }
static void set_next(undo_record *r,uint32_t id) { r->next=(r->next&~INDEX)|id; }
static group_index group(const undo_log *u,uint32_t first) {
    const undo_record *r=record(u,first);group_index g={first,0,1};
    if(!(r->next&END)) memcpy(&g,&r->after,sizeof g);
    return g;
}
static uint32_t group_first(const undo_log *u,uint32_t last) {
    const undo_record *r=record(u,last);uint32_t first=last;
    if(!(r->prev&START)) memcpy(&first,&r->before,sizeof first);
    return first;
}
static void index_group(undo_log *u,uint32_t first,uint32_t last,size_t count) {
    if(first==last) return;
    group_index g={last,0,(uint64_t)count};
    memcpy(&record(u,first)->after,&g,sizeof g);
    memcpy(&record(u,last)->before,&first,sizeof first);
}

/* The base pool owns the lazy mapping. Undo uses it as a packed array, rather
 * than an intrusive free list: freeing a slot moves the last occupied record
 * into it and repairs O(1) links. Consequently no free-list pointer prevents
 * MADV_DONTNEED of wholly unused tail pages. Scratch is always a suffix. */
static size_t committed(const undo_log *u) {
    size_t bytes=u->pool.fresh*u->pool.stride;
    return bytes?(bytes+u->page_bytes-1)/u->page_bytes*u->page_bytes:0;
}
static void decommit(undo_log *u) {
    size_t bytes=committed(u);
    if(bytes<u->committed_bytes) {
        if(madvise(u->pool.base+bytes,u->committed_bytes-bytes,MADV_DONTNEED)==0)
            u->committed_bytes=bytes;
    }
}
static uint32_t take(undo_log *u) {
    if(u->pool.fresh==u->pool.capacity) return 0;
    uint32_t id=(uint32_t)++u->pool.fresh;u->pool.live++;
    memset(record(u,id),0,sizeof(undo_record));
    record(u,id)->prev=START;record(u,id)->next=END;
    size_t bytes=committed(u);if(bytes>u->committed_bytes) u->committed_bytes=bytes;
    return id;
}
static void release_scratch(undo_log *u,size_t n) {
    u->pool.fresh-=n;u->pool.live-=n;decommit(u);
}
static void relocate(undo_log *u,uint32_t old,uint32_t id) {
    undo_record *r=record(u,id);uint32_t prev=prev_id(r),next=next_id(r);
    if(prev) set_next(record(u,prev),id);
    if(next) set_prev(record(u,next),id);
    if((r->prev&START) && !(r->next&END)) {
        group_index g=group(u,id);
        memcpy(&record(u,g.last)->before,&id,sizeof id);
    } else if((r->next&END) && !(r->prev&START)) {
        uint32_t first=group_first(u,id);
        /* Reclaiming a retired group's first record zeroes this backlink. */
        if(first) {
            group_index g=group(u,first);g.last=id;
            memcpy(&record(u,first)->after,&g,sizeof g);
        }
    }
    if(u->head==old) u->head=id;
    if(u->tail==old) u->tail=id;
    if(u->cursor==old) u->cursor=id;
    if(u->open_first==old) u->open_first=id;
    if(u->replay_first==old) u->replay_first=id;
    if(u->retired_head==old) u->retired_head=id;
    if(u->retired_tail==old) u->retired_tail=id;
}
static void put_retired(undo_log *u,uint32_t id) {
    undo_record *r=record(u,id);
    if((r->prev&START) && !(r->next&END)) {
        uint32_t zero=0;group_index g=group(u,id);
        memcpy(&record(u,g.last)->before,&zero,sizeof zero);
    }
    uint32_t last=(uint32_t)u->pool.fresh;
    if(id!=last) { *r=*record(u,last);relocate(u,last,id); }
    u->pool.fresh--;u->pool.live--;u->retired_count--;
}
size_t undo_maintain(undo_log *u,size_t budget) {
    /* Saved capture handles and the pre-group packed suffix must stay fixed. */
    if(u->replay_checkpoint) return 0;
    size_t n=0;
    while(n<budget && u->retired_head) {
        uint32_t id=u->retired_head;
        u->retired_head=next_id(record(u,id));
        if(u->retired_head) set_prev(record(u,u->retired_head),0);else u->retired_tail=0;
        put_retired(u,id);n++;
    }
    decommit(u);return n;
}
static void retire(undo_log *u,uint32_t first,uint32_t last,size_t n) {
    if(!n) return;
    set_next(record(u,last),0);set_prev(record(u,first),u->retired_tail);
    if(u->retired_tail) set_next(record(u,u->retired_tail),first);else u->retired_head=first;
    u->retired_tail=last;u->retired_count+=n;u->count-=n;
}
static void clear_redo(undo_log *u) {
    uint32_t first=u->cursor?next_id(record(u,u->cursor)):u->head;
    if(!first) return;
    /* Applied record count is maintained across capture and replay. */
    retire(u,first,u->tail,u->count-u->applied_count);
    u->total_groups=u->applied_groups;
    u->tail=u->cursor;
    if(u->cursor) set_next(record(u,u->cursor),0);else u->head=0;
}
static void release_view(undo_log *u) {
    if(u->replay_view) piece_snapshot_release(u->replay_view);
    u->replay_view=NULL;u->replay_first=0;
}
static undo_record *backup(const undo_log *u,size_t i) {
    return (undo_record *)(u->replay_pool.base+i*u->replay_pool.stride);
}
static void reset_backup(undo_log *u) {
    u->replay_pool.fresh=u->replay_pool.live=0;
    if(u->replay_committed_bytes &&
       madvise(u->replay_pool.base,u->replay_committed_bytes,MADV_DONTNEED)==0)
        u->replay_committed_bytes=0;
}
static void save_capture(undo_log *u,uint32_t id) {
    /* Unknown insert records have no published ref. Reuse their zero add_off
     * word for the handle in a full 64 B backup; every other byte is preserved. */
    EDIT_ASSERT(!(record(u,id)->next&KNOWN) && record(u,id)->add_off==0);
    EDIT_ASSERT(u->replay_pool.fresh<u->replay_pool.capacity);
    undo_record *r=backup(u,u->replay_pool.fresh++);u->replay_pool.live++;
    *r=*record(u,id);r->add_off=id;
    size_t bytes=(u->replay_pool.fresh*u->replay_pool.stride+u->page_bytes-1)/u->page_bytes*u->page_bytes;
    if(bytes>u->replay_committed_bytes) u->replay_committed_bytes=bytes;
}
static void assert_replay_tree(const undo_log *u) {
    if(!u->replay_guard) return;
    /* The linked kernel caches one immutable header until a mutation. Holding
     * an owner prevents cache eviction/address reuse, even for an empty tree.
     * Retaking an unchanged cached snapshot allocates nothing. This assertion
     * deliberately also rejects an attempted direct mutation that invalidates
     * the cache before failing. No piece-private layout is inspected. */
    piece_snapshot *current=piece_snapshot_take(u->tree);
    EDIT_ASSERT(current && current==u->replay_guard);
    piece_snapshot_release(current);
}
static void release_guard(undo_log *u) {
    if(u->replay_guard) piece_snapshot_release(u->replay_guard);
    u->replay_guard=NULL;
}
static void abort_replay(undo_log *u) {
    if(!u->replay_checkpoint) return;
    assert_replay_tree(u);release_guard(u);
    piece_checkpoint_abort(u->replay_checkpoint);u->replay_checkpoint=NULL;
    /* Undo captures in reverse order, repairing the neighbor each expansion
     * changed. All extra span slots are the suffix appended since begin.
     * Maintenance/trim cannot run while checkpointed, so handles never move. */
    for(size_t n=u->replay_pool.fresh;n;n--) {
        undo_record old=*backup(u,n-1);uint32_t id=(uint32_t)old.add_off;old.add_off=0;
        *record(u,id)=old;
        uint32_t next=next_id(&old);if(next) set_prev(record(u,next),id);
    }
    record(u,u->replay_first)->after=u->replay_first_after;
    record(u,u->replay_last)->before=u->replay_last_before;
    record(u,u->replay_last)->after=u->replay_last_after;
    u->cursor=u->replay_cursor;u->tail=u->replay_tail;
    u->count=u->replay_count;u->applied_count=u->replay_applied;
    u->pool.fresh=u->pool.live=u->replay_fresh;
    u->partial=0;reset_backup(u);decommit(u);release_view(u);
}
static int begin_replay(undo_log *u,uint32_t id,int direction) {
    EDIT_ASSERT(!u->replay_checkpoint && !u->replay_pool.fresh);
    uint32_t first=direction<0?group_first(u,id):id;
    group_index g=group(u,first);
    /* A pre-group view remains necessary for normal in-group slice yields.
     * Take it before begin so it needs no intermediate-tail reservation. */
    if(g.count>1) {
        u->replay_view=piece_snapshot_take(u->tree);
        if(!u->replay_view) return UNDO_ERR_NOMEM;
    }
    int rc=piece_checkpoint_begin(u->tree,&u->replay_checkpoint);
    if(rc) { release_view(u);return rc; }
    u->replay_first=first;u->replay_last=g.last;
    u->replay_first_after=record(u,first)->after;
    u->replay_last_before=record(u,g.last)->before;
    u->replay_last_after=record(u,g.last)->after;
    u->replay_cursor=u->cursor;u->replay_tail=u->tail;
    u->replay_count=u->count;u->replay_applied=u->applied_count;u->replay_fresh=u->pool.fresh;
    u->partial=direction;return UNDO_OK;
}
static void commit_replay(undo_log *u) {
    EDIT_ASSERT(u->replay_checkpoint && !u->replay_guard);
    piece_checkpoint_commit(u->replay_checkpoint);u->replay_checkpoint=NULL;
    u->partial=0;reset_backup(u);release_view(u);
}
void undo_clear(undo_log *u) {
    abort_replay(u);
    release_view(u);
    u->head=u->tail=u->cursor=u->open_first=0;
    u->retired_head=u->retired_tail=0;
    u->count=u->applied_count=u->retired_count=0;
    u->total_groups=u->applied_groups=0;
    u->pool.fresh=u->pool.live=0;u->pool.free_head=NULL;
    u->burst=u->open=u->partial=0;decommit(u);
}
int undo_init(undo_log *u,piece_tree *tree,size_t max_records) {
    memset(u,0,sizeof *u);
    if(!tree || !max_records || max_records>((size_t)INDEX/PIECE_REF_SPANS)-8) return UNDO_ERR_RANGE;
    /* Every admitted unknown insertion can expand by seven records. Untouched
     * virtual slots do not contribute to the committed-memory bound. */
    if(edit_pool_init(&u->pool,sizeof(undo_record),8,(max_records+8)*PIECE_REF_SPANS)!=0) return UNDO_ERR_NOMEM;
    if(edit_pool_init(&u->replay_pool,sizeof(undo_record),8,max_records+8)!=0) {
        edit_pool_free(&u->pool);return UNDO_ERR_NOMEM;
    }
    long pg=sysconf(_SC_PAGESIZE);
    if(pg<=0) { edit_pool_free(&u->replay_pool);edit_pool_free(&u->pool);return UNDO_ERR_NOMEM; }
    u->page_bytes=(size_t)pg;
    u->tree=tree;u->cap=u->max_records=max_records;return 0;
}
void undo_destroy(undo_log *u) {
    abort_replay(u);release_view(u);edit_pool_free(&u->replay_pool);edit_pool_free(&u->pool);memset(u,0,sizeof *u);
}
void undo_break_burst(undo_log *u) { u->burst=0; }
/* Detach at most sixteen groups. Very large cap reductions may discard extra
 * oldest groups (all remaining closed history) to keep the operation bounded.
 * Physical reclamation is separately limited to UNDO_RECLAIM_RECORDS. */
static void trim(undo_log *u) {
    size_t work=0;
    if(u->partial) return;
    while(u->count>u->cap && u->head && work<UNDO_RECLAIM_RECORDS) {
        if(u->open && u->head==u->open_first) return;
        group_index g=group(u,u->head);
        if((size_t)g.count>u->applied_count) { clear_redo(u);u->burst=0;break; }
        uint32_t first=u->head,next=next_id(record(u,g.last));
        if(u->cursor==g.last) u->cursor=0;
        u->head=next;u->applied_count-=(size_t)g.count;
        if(next) set_prev(record(u,next),0);else u->tail=0;
        retire(u,first,g.last,(size_t)g.count);work++;
        u->total_groups--;u->applied_groups--;u->evicted_groups++;
    }
    if(u->count>u->cap && u->head && (!u->open || u->head!=u->open_first)) {
        uint32_t last=u->open_first?prev_id(record(u,u->open_first)):u->tail;
        size_t n=u->count;
        if(u->open_first) n-=(size_t)group(u,u->open_first).count;
        uint32_t first=u->head;u->head=u->open_first;
        if(u->head) set_prev(record(u,u->head),0);else u->tail=0;
        /* An open group is the applied suffix; otherwise all history goes. */
        u->applied_count=u->head?u->count-n:0;u->cursor=u->head?u->tail:0;
        retire(u,first,last,n);
        size_t retained=u->open_first?1u:0u;
        u->evicted_groups+=u->total_groups-retained;
        u->total_groups=u->applied_groups=retained;
    }
    if(!u->tail) u->burst=0;
}
int undo_set_cap(undo_log *u,size_t records) {
    if(u->open || u->partial) return UNDO_ERR_BUSY;
    if(!records || records>u->max_records) return UNDO_ERR_RANGE;
    u->cap=records;u->burst=0;trim(u);(void)undo_maintain(u,UNDO_RECLAIM_RECORDS);return 0;
}
int undo_group_begin(undo_log *u,const undo_state *before) {
    if(u->open || u->partial) return UNDO_ERR_BUSY;
    if(!before) return UNDO_ERR_RANGE;
    u->open=1;u->open_first=0;u->open_before=*before;u->burst=0;return 0;
}
int undo_group_end(undo_log *u,const undo_state *after) {
    if(!u->open) return UNDO_ERR_BUSY;
    if(!after) return UNDO_ERR_RANGE;
    if(u->open_first) record(u,u->tail)->after=*after;
    u->open=0;u->open_first=0;u->burst=0;trim(u);(void)undo_maintain(u,UNDO_RECLAIM_RECORDS);return 0;
}
static int joins(const undo_log *u,undo_kind kind,uint64_t off,uint64_t len,uint64_t tm) {
    if(u->open) return u->open_first!=0;
    if(!u->burst || !u->tail || u->cursor!=u->tail || kind!=u->last_kind || tm<u->last_time || tm-u->last_time>UNDO_BURST_NS) return 0;
    if(kind==UNDO_INSERT) return off==u->last_off+u->last_len;
    if(kind==UNDO_BACKSPACE) return len<=u->last_off && off==u->last_off-len;
    return off==u->last_off;
}
static int reserve(undo_log *u,uint32_t ids[PIECE_REF_SPANS],size_t n) {
    for(size_t i=0;i<n;i++) {
        ids[i]=take(u);
        if(!ids[i]) { release_scratch(u,i);return UNDO_ERR_NOMEM; }
    }
    return 0;
}
static void append_edit(undo_log *u,const uint32_t *ids,size_t n,const piece_ref *ref,
                        uint64_t off,uint64_t len,undo_kind kind,uint64_t tm,
                        const undo_state *before,const undo_state *after) {
    int join=joins(u,kind,off,len,tm);uint32_t first=ids[0];size_t count=n;
    clear_redo(u);
    if(!join) { u->total_groups++;u->applied_groups++;u->group_serial++; }
    if(join) {
        first=group_first(u,u->tail);count+=(size_t)group(u,first).count;
        record(u,u->tail)->next&=~END;
    }
    for(size_t i=0;i<n;i++) {
        undo_record *r=record(u,ids[i]);memset(r,0,sizeof *r);
        r->off=off;r->len=ref?ref->span[i].len:len;
        r->add_off=ref?ref->span[i].add_off:0;
        r->prev=u->tail|(kind==UNDO_INSERT?INSERT:0)|(!join && i==0?START:0);
        r->next=(i+1==n?END:0)|(ref?KNOWN:0);
        r->before=(u->open && !join)?u->open_before:*before;r->after=*after;
        if(u->tail) set_next(record(u,u->tail),ids[i]);else u->head=ids[i];
        u->tail=ids[i];u->count++;u->applied_count++;
    }
    index_group(u,first,u->tail,count);u->cursor=u->tail;
    if(u->open && !u->open_first) u->open_first=first;
    u->last_time=tm;u->last_off=off;u->last_len=len;u->last_kind=kind;u->burst=!u->open;
}
static int edit(undo_log *u,uint64_t off,const uint8_t *data,uint64_t len,
                undo_kind kind,uint64_t tm,const undo_state *before,const undo_state *after) {
    if(u->partial) return UNDO_ERR_BUSY;
    if(!before || !after || (kind==UNDO_INSERT && len && !data)) return UNDO_ERR_RANGE;
    uint64_t total=piece_len(u->tree);
    if(off>total || (kind!=UNDO_INSERT && len>total-off) || (kind==UNDO_INSERT && (len>UINT64_MAX-total || len>SIZE_MAX))) return UNDO_ERR_RANGE;
    if(!len) return 0;
    size_t needed=kind==UNDO_INSERT?1:PIECE_REF_SPANS;
    if(u->count+needed>u->max_records+8) return UNDO_ERR_NOMEM;
    /* One bounded maintenance pass per edit, even when the piece call fails. */
    (void)undo_maintain(u,UNDO_RECLAIM_RECORDS);
    uint32_t ids[PIECE_REF_SPANS];int rc=reserve(u,ids,needed);if(rc) return rc;
    piece_ref ref;size_t n=1;
    if(kind==UNDO_INSERT) rc=piece_insert(u->tree,off,data,(size_t)len);
    else { rc=piece_delete(u->tree,off,len,&ref);if(!rc) n=ref.nspans; }
    if(!rc) append_edit(u,ids,n,kind==UNDO_INSERT?NULL:&ref,off,len,kind,tm,before,after);
    release_scratch(u,rc?needed:needed-n);
    if(!rc && !u->open) trim(u);
    return rc;
}
int undo_insert(undo_log *u,uint64_t off,const uint8_t *data,size_t len,uint64_t tm,const undo_state *before,const undo_state *after) {
    return edit(u,off,data,len,UNDO_INSERT,tm,before,after);
}
int undo_delete(undo_log *u,uint64_t off,uint64_t len,undo_kind kind,uint64_t tm,const undo_state *before,const undo_state *after) {
    if(kind!=UNDO_BACKSPACE && kind!=UNDO_DELETE) return UNDO_ERR_RANGE;
    return edit(u,off,NULL,len,kind,tm,before,after);
}
/* Capture occurs only after a successful atomic piece_delete. The virtual
 * reserve covers expansion of every admitted unknown record, even inside a
 * protected group; seven scratch slots suffice because id supplies span zero. */
static uint32_t capture(undo_log *u,uint32_t id,const piece_ref *ref,const uint32_t *ids) {
    undo_record old=*record(u,id);uint32_t tail=id;
    uint32_t first=u->replay_first?u->replay_first:id;
    group_index g=group(u,first);undo_state after=record(u,g.last)->after;
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
    if(g.last==id) g.last=tail;
    record(u,g.last)->after=after;
    index_group(u,first,g.last,(size_t)g.count+ref->nspans-1);
    u->count+=(size_t)ref->nspans-1;u->applied_count+=(size_t)ref->nspans-1;
    return id;
}
static void dirty(undo_change *c,uint64_t off,uint64_t end) {
    if(!c->records) { c->off=off;c->len=end-off;return; }
    uint64_t old_end=c->off+c->len;
    if(off<c->off) c->off=off;
    if(end<old_end) end=old_end;
    c->len=end-c->off;
}
static uint64_t now_ns(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0) return UINT64_MAX;
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}
static int replay(undo_log *u,size_t groups,size_t budget,uint64_t deadline,undo_change *c,int direction) {
    if(!c) return UNDO_ERR_RANGE;
    memset(c,0,sizeof *c);
    assert_replay_tree(u);
    if(u->open || (u->partial && u->partial!=direction)) return UNDO_ERR_BUSY;
    u->burst=0;
    undo_change completed_change={0};
    while(c->groups<groups) {
        uint32_t id=direction<0?u->cursor:(u->cursor?next_id(record(u,u->cursor)):u->head);
        if(!id) break;
        if(c->operations>=budget || (deadline!=UINT64_MAX && now_ns()>=deadline)) {
            if(u->replay_checkpoint && !u->replay_guard) {
                u->replay_guard=piece_snapshot_take(u->tree);
                if(!u->replay_guard) { abort_replay(u);*c=completed_change;return UNDO_ERR_NOMEM; }
            }
            return UNDO_MORE;
        }
        release_guard(u);
        /* Compaction can change id, so resolve it again afterwards. */
        (void)undo_maintain(u,UNDO_RECLAIM_RECORDS);
        id=direction<0?u->cursor:(u->cursor?next_id(record(u,u->cursor)):u->head);
        if(!u->replay_checkpoint) {
            int rc=begin_replay(u,id,direction);
            if(rc) { *c=completed_change;return rc; }
        }
        undo_record *r=record(u,id);uint64_t before_len=piece_len(u->tree),off=r->off;
        int remove=((r->prev&INSERT)!=0)==(direction<0),rc;
        size_t applied=1;
        if(remove) {
            uint32_t ids[PIECE_REF_SPANS];int unknown=(r->next&KNOWN)==0;
            if(unknown) {
                rc=reserve(u,ids,PIECE_REF_SPANS-1);
                if(rc) { abort_replay(u);*c=completed_change;return rc; }
                /* A one-operation group has no possible failure after its
                 * successful capture: commit is void and allocation-free. */
                if(u->replay_view) save_capture(u,id);
            }
            piece_ref ref;rc=piece_delete(u->tree,r->off,r->len,&ref);
            if(unknown) {
                size_t used=0;
                if(!rc) { id=capture(u,id,&ref,ids);used=(size_t)ref.nspans-1;applied=ref.nspans; }
                release_scratch(u,PIECE_REF_SPANS-1-used);
            }
        } else {
            piece_ref ref={0};ref.nspans=1;ref.len=r->len;ref.span[0].add_off=r->add_off;ref.span[0].len=r->len;
            rc=piece_insert_ref(u->tree,r->off,&ref);
        }
        if(rc) { abort_replay(u);*c=completed_change;return rc; }
        r=record(u,id);uint64_t after_len=piece_len(u->tree);
        dirty(c,off,before_len>after_len?before_len:after_len);c->records+=applied;c->operations++;
        int complete=direction<0?(r->prev&START)!=0:(r->next&END)!=0;
        u->cursor=direction<0?prev_id(r):id;
        if(direction<0) u->applied_count-=applied;else u->applied_count+=applied;
        if(complete) {
            if(direction<0) u->applied_groups--;else u->applied_groups++;
            c->groups++;c->has_state=1;c->state=direction<0?r->before:r->after;
            commit_replay(u);trim(u);completed_change=*c;
        }
    }
    return 0;
}
int undo_undo_slice(undo_log *u,size_t groups,size_t budget,uint64_t deadline,undo_change *c) {
    return replay(u,groups,budget,deadline,c,-1);
}
int undo_redo_slice(undo_log *u,size_t groups,size_t budget,uint64_t deadline,undo_change *c) {
    return replay(u,groups,budget,deadline,c,1);
}
int undo_undo(undo_log *u,size_t groups,undo_change *c) { return undo_undo_slice(u,groups,SIZE_MAX,UINT64_MAX,c); }
int undo_redo(undo_log *u,size_t groups,undo_change *c) { return undo_redo_slice(u,groups,SIZE_MAX,UINT64_MAX,c); }
const piece_snapshot *undo_replay_snapshot(const undo_log *u) { return u->replay_view; }
undo_stats undo_get_stats(const undo_log *u) {
    undo_stats s={0};s.records=u->count;s.retired_records=u->retired_count;
    s.replay_records=u->replay_pool.live;
    s.live_bytes=u->pool.live*u->pool.stride+u->replay_pool.live*u->replay_pool.stride;
    s.reserved_bytes=u->pool.map_bytes+u->replay_pool.map_bytes;
    s.committed_bytes=u->committed_bytes+u->replay_committed_bytes;
    uint32_t cursor=u->replay_checkpoint?u->replay_cursor:u->cursor;
    if(u->replay_checkpoint && u->partial<0) cursor=group(u,u->replay_first).last;
    int applied=cursor!=0;
    for(uint32_t id=u->head;id;id=next_id(record(u,id))) {
        const undo_record *r=record(u,id);
        if(applied && (r->next&END)) s.undo_groups++;
        if(!applied && (r->prev&START)) s.redo_groups++;
        if(id==cursor) applied=0;
    }
    return s;
}

undo_history undo_get_history(const undo_log *u) {
    return (undo_history){u->applied_groups,u->total_groups-u->applied_groups,u->evicted_groups,u->group_serial};
}
