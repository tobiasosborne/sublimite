#include "journal/journal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAGIC 0x314c4e4au
#define SYNC_BYTES 65536u
#define SYNC_NS 1000000000ull
#define PAD 0u

typedef struct journal_batch {
    uint8_t *bytes;
    size_t used, sealed;
    uint64_t offset, sequence;
    bool force;
} journal_batch;
typedef struct journal_disk {
    int fd;
    uint64_t unsynced, written_sequence;
    journal_stats stats;
} journal_disk;
struct journal {
    edit_arena arena;
    work_pool *pool;
    work_handle handle;
    journal_batch batches[2];
    journal_disk disk;          /* exclusively worker owned while a job is active */
    journal_stats stats;        /* exclusively UI owned */
    uint32_t crc_table[256];
    size_t capacity;
    uint64_t limit, reserved;
    char *path;
    unsigned current, active_index;
    bool active;
    void (*message_handler)(const work_msg *, void *);
    void *message_ctx;
};
typedef struct journal_completion { journal_stats stats; } journal_completion;
/* A completion snapshot lives in the active arena batch after its data.
 * Only its address crosses the work mailbox; worker parks until consumption. */
static uint64_t clock_ns(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static void delay(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t u64(const uint8_t *p)
{ return (uint64_t)u32(p) | (uint64_t)u32(p+4)<<32; }
static void put32(uint8_t *p, uint32_t v)
{ for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put64(uint8_t *p, uint64_t v)
{ for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void crc_init(uint32_t *table)
{
    for(uint32_t i=0;i<256;i++) { uint32_t v=i; for(unsigned k=0;k<8;k++) v=(v>>1)^((v&1u)?0x82f63b78u:0u); table[i]=v; }
}
static uint32_t crc_data(const uint32_t *table, const uint8_t *p, size_t n)
{ uint32_t c=UINT32_MAX; for(size_t i=0;i<n;i++) c=table[(c^p[i])&255u]^(c>>8); return ~c; }
static uint32_t crc_record(const uint32_t *table, const uint8_t *p, size_t n)
{ uint32_t c=UINT32_MAX; for(size_t i=0;i<n;i++) { uint8_t v=(i>=12 && i<16)?0:p[i]; c=table[(c^v)&255u]^(c>>8); } return ~c; }
static size_t sealed_size(size_t used)
{ return (used+JOURNAL_HEADER+JOURNAL_PAGE-1u)&~(size_t)(JOURNAL_PAGE-1u); }
static void encode(uint8_t *p, uint32_t type, uint64_t id, uint64_t seq, size_t n, const uint32_t *table)
{
    put32(p,MAGIC); put32(p+4,(uint32_t)n); put32(p+8,type); put32(p+12,0);
    put64(p+16,seq); put64(p+24,id); put32(p+12,crc_record(table,p,n));
}
static void seal(journal_batch *b, const uint32_t *table)
{
    b->sealed=sealed_size(b->used); size_t n=b->sealed-b->used;
    memset(b->bytes+b->used,0,n); encode(b->bytes+b->used,PAD,0,0,n,table);
}
static bool payload_valid(uint32_t type, const uint8_t *p, size_t n)
{
    switch(type) {
    case JOURNAL_BASE:
        if(n<41 || n>4137 || p[n-1]!=0 || memchr(p+40,0,n-41)!=NULL || u32(p+36)>4096 || u32(p+36)>u64(p)) return false;
        if(p[40]==0 && (u64(p)!=0 || u64(p+8)!=0 || u64(p+16)!=0 || u64(p+24)!=0 || u32(p+32)!=0 || u32(p+36)!=0)) return false;
        return true;
    case JOURNAL_INSERT: return n>=8;
    case JOURNAL_DELETE: return n==16 && u64(p)<=UINT64_MAX-u64(p+8);
    case JOURNAL_VIEW: return n==32;
    case JOURNAL_TABS: return n>=16 && (n-16)%8==0 && u64(p)==(n-16)/8 && (u64(p)==0 ? u64(p+8)==0 : u64(p+8)<u64(p));
    case JOURNAL_WINDOW: return n==8;
    default: return false;
    }
}
static bool valid_record(const uint8_t *p, size_t n, uint64_t offset, uint64_t seq, const uint32_t *table)
{
    if(u32(p)!=MAGIC || u32(p+4)!=n || crc_record(table,p,n)!=u32(p+12)) return false;
    uint32_t type=u32(p+8);
    if(type==PAD) {
        if(n>JOURNAL_PAGE+JOURNAL_HEADER-1u || (offset+n)%JOURNAL_PAGE || u64(p+16) || u64(p+24)) return false;
        for(size_t i=32;i<n;i++) if(p[i]) return false;
        return true;
    }
    return seq!=UINT64_MAX && u64(p+16)==seq+1 && payload_valid(type,p+32,n-32);
}
static int visit_record(const uint8_t *p, size_t n, journal_visit visit, void *ctx, journal_replay_result *r)
{
    if(u32(p+8)!=PAD) {
        journal_record rec={u32(p+8),u64(p+24),u64(p+16),p+32,n-32};
        if(visit && visit(ctx,&rec)) return JOURNAL_CALLBACK;
        r->records++; r->last_sequence=rec.sequence;
    }
    r->valid_bytes+=n; return 0;
}
int journal_replay_bytes(const uint8_t *bytes, size_t size, journal_visit visit, void *ctx, journal_replay_result *result)
{
    if(!result || (!bytes && size)) return JOURNAL_INVALID;
    *result=(journal_replay_result){0}; uint32_t table[256]; crc_init(table); size_t off=0;
    while(off<size) {
        if(size-off<32) { result->corrupt=true; break; }
        size_t n=u32(bytes+off+4);
        if(n<32 || n>JOURNAL_MAX_RECORD || n>size-off || !valid_record(bytes+off,n,off,result->last_sequence,table)) { result->corrupt=true; break; }
        int rc=visit_record(bytes+off,n,visit,ctx,result); if(rc) return rc; off+=n;
    }
    return 0;
}
static int read_at(int fd, uint8_t *p, size_t n, uint64_t off, size_t *got)
{
    *got=0;
    while(*got<n) {
        ssize_t k=pread(fd,p+*got,n-*got,(off_t)(off+*got));
        if(k<0) { if(errno==EINTR) continue; return JOURNAL_IO; }
        if(k==0) break;
        *got+=(size_t)k;
    }
    return 0;
}
static int write_at(int fd, const uint8_t *p, size_t n, uint64_t off)
{
    size_t done=0;
    while(done<n) {
        ssize_t k=pwrite(fd,p+done,n-done,(off_t)(off+done));
        if(k<0 && errno==EINTR) continue;
        if(k<=0) return JOURNAL_IO;
        done+=(size_t)k;
    }
    return 0;
}
static int data_sync(int fd)
{ int rc; do { rc=fdatasync(fd); } while(rc<0 && errno==EINTR); return rc<0?JOURNAL_IO:0; }
int journal_replay_file(const char *path, journal_visit visit, void *ctx, journal_replay_result *result)
{
    if(!path || !result) return JOURNAL_INVALID;
    *result=(journal_replay_result){0}; int fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return JOURNAL_IO;
    edit_arena a; if(edit_arena_init(&a,JOURNAL_MAX_RECORD)) { close(fd); return JOURNAL_NOMEM; }
    uint8_t *p=edit_arena_alloc(&a,JOURNAL_MAX_RECORD,16); uint32_t table[256]; crc_init(table); int rc=0;
    for(;;) {
        size_t got=0; rc=read_at(fd,p,32,result->valid_bytes,&got); if(rc || !got) break;
        size_t n=got==32?u32(p+4):0;
        if(got!=32 || n<32 || n>JOURNAL_MAX_RECORD) { result->corrupt=true; break; }
        rc=read_at(fd,p+32,n-32,result->valid_bytes+32,&got); if(rc) break;
        if(got!=n-32 || !valid_record(p,n,result->valid_bytes,result->last_sequence,table)) { result->corrupt=true; break; }
        if(u32(p+8)==JOURNAL_BASE) {
            journal_record rec={JOURNAL_BASE,u64(p+24),u64(p+16),p+32,n-32};
            journal_base base; char basepath[4097];
            rc=journal_decode_base(&rec,&base,basepath,sizeof basepath);
            if(!rc) rc=journal_check_base(&base);
            if(rc) break;
        }
        rc=visit_record(p,n,visit,ctx,result); if(rc) break;
    }
    if(!rc && result->corrupt) { if(ftruncate(fd,(off_t)result->valid_bytes)) rc=JOURNAL_IO; else rc=data_sync(fd); }
    edit_arena_free(&a); close(fd); return rc;
}
static bool stat_equal(const struct stat *a, const struct stat *b)
{ return a->st_size==b->st_size && a->st_ino==b->st_ino && a->st_dev==b->st_dev && a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec && a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec; }
int journal_capture_base(const char *path, journal_base *base)
{
    if(!path || !base) return JOURNAL_INVALID;
    if(!*path) { *base=(journal_base){.path=path}; return 0; }
    int fd=open(path,O_RDONLY|O_CLOEXEC); if(fd<0) return JOURNAL_BASE_CHANGED;
    struct stat before,after,atpath; uint8_t prefix[4096]; uint32_t table[256]; size_t got=0; int rc=0;
    if(fstat(fd,&before) || !S_ISREG(before.st_mode) || before.st_size<0 || before.st_mtim.tv_sec<0) rc=JOURNAL_BASE_CHANGED;
    if(!rc) rc=read_at(fd,prefix,(size_t)(before.st_size<4096?before.st_size:4096),0,&got);
    if(!rc && (fstat(fd,&after) || stat(path,&atpath) || !stat_equal(&before,&after) || !stat_equal(&after,&atpath))) rc=JOURNAL_BASE_CHANGED;
    if(!rc) {
        crc_init(table); *base=(journal_base){(uint64_t)before.st_size,(uint64_t)before.st_mtim.tv_sec*1000000000u+(uint64_t)before.st_mtim.tv_nsec,(uint64_t)before.st_ino,(uint64_t)before.st_dev,crc_data(table,prefix,got),(uint32_t)got,path};
    }
    close(fd); return rc;
}
int journal_check_base(const journal_base *base)
{
    if(!base || !base->path) return JOURNAL_INVALID;
    journal_base now; int rc=journal_capture_base(base->path,&now); if(rc) return rc;
    return base->size==now.size && base->mtime_ns==now.mtime_ns && base->inode==now.inode && base->device==now.device && base->prefix_len==now.prefix_len && base->prefix_crc==now.prefix_crc?0:JOURNAL_BASE_CHANGED;
}
int journal_decode_base(const journal_record *r, journal_base *base, char *path, size_t cap)
{
    if(!r || !base || !path || !r->data || r->type!=JOURNAL_BASE || !payload_valid(r->type,r->data,r->size) || cap<r->size-40) return JOURNAL_INVALID;
    memcpy(path,r->data+40,r->size-40); *base=(journal_base){u64(r->data),u64(r->data+8),u64(r->data+16),u64(r->data+24),u32(r->data+32),u32(r->data+36),path}; return 0;
}
int journal_apply_piece(piece_tree *tree, const journal_record *r)
{
    if(!tree || !r || !r->data || !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
    int rc;
    if(r->type==JOURNAL_INSERT) rc=piece_insert(tree,u64(r->data),r->data+8,r->size-8);
    else if(r->type==JOURNAL_DELETE) rc=piece_delete(tree,u64(r->data),u64(r->data+8),NULL);
    else return JOURNAL_INVALID;
    return rc==PIECE_OK?0:rc==PIECE_ERR_NOMEM?JOURNAL_NOMEM:JOURNAL_INVALID;
}
/* Every batch ends on a page boundary. Record prefix at a sync boundary may
 * end earlier when a record straddles it. Never acknowledge the partial one. */
static uint64_t prefix_sequence(const journal_batch *b, size_t upto, uint64_t initial)
{
    size_t pos=0; uint64_t seq=initial;
    while(pos+32<=upto) { size_t n=u32(b->bytes+pos+4); if(n>upto-pos) break; if(u32(b->bytes+pos+8)!=PAD) seq=u64(b->bytes+pos+16); pos+=n; }
    return seq;
}
static int sync_disk(journal_disk *d, uint64_t sequence)
{
    int rc=data_sync(d->fd); if(rc) return rc;
    uint64_t now=clock_ns(); journal_stats *s=&d->stats;
    uint64_t interval=now-s->last_sync_ns;
    if(d->unsynced && interval>s->max_sync_interval_ns) s->max_sync_interval_ns=interval;
    s->last_sync_ns=now; s->last_sync_bytes=d->unsynced;
    if(d->unsynced>s->max_sync_bytes) s->max_sync_bytes=d->unsynced;
    s->syncs++; s->durable_sequence=sequence; d->unsynced=0; return 0;
}
static void worker(work_ctx *ctx)
{
    journal *j=ctx->arg; journal_batch *b=&j->batches[j->active_index]; journal_disk *d=&j->disk;
    size_t pos=0; int rc=0; uint64_t initial=d->written_sequence;
    while(pos<b->sealed && !work_should_stop(ctx)) {
        size_t n=b->sealed-pos; size_t room=(size_t)(SYNC_BYTES-d->unsynced); if(n>room) n=room;
        rc=write_at(d->fd,b->bytes+pos,n,b->offset+pos); if(rc) break;
        pos+=n; d->unsynced+=n; d->written_sequence=prefix_sequence(b,pos,initial);
        d->stats.written_sequence=d->written_sequence; d->stats.file_bytes=b->offset+pos;
        if(d->unsynced>=SYNC_BYTES || clock_ns()-d->stats.last_sync_ns>=SYNC_NS) { rc=sync_disk(d,d->written_sequence); if(rc) break; }
    }
    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=SYNC_NS))) rc=sync_disk(d,d->written_sequence);
    d->stats.error=rc;
    journal_completion *completion=(journal_completion *)(void *)(b->bytes+j->capacity);
    completion->stats=d->stats;
    work_msg msg={.kind=JOURNAL_MESSAGE}; uintptr_t address=(uintptr_t)completion; memcpy(msg.data,&address,sizeof address);
    while(!work_should_stop(ctx) && !work_publish(ctx,&msg)) delay();
    /* Reserve slot epoch until UI reads completion; prevents work_submit from
     * reusing the slot and invalidating its unread mailbox message. */
    while(!work_should_stop(ctx)) delay();
}
static int sync_parent(const char *path)
{
    char directory[4097]; strcpy(directory,path);
    char *slash=strrchr(directory,'/');
    if(!slash) strcpy(directory,".");
    else if(slash==directory) slash[1]=0;
    else *slash=0;
    int fd=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return JOURNAL_IO;
    int rc=fsync(fd)<0?JOURNAL_IO:0;
    close(fd); return rc;
}
int journal_open(journal **out, const char *path, work_pool *pool, const journal_options *options)
{
    if(!out || !path || !pool) return JOURNAL_INVALID;
    *out=NULL;
    size_t cap=options && options->batch_bytes?options->batch_bytes:262144u;
    uint64_t limit=options && options->max_file_bytes?options->max_file_bytes:67108864u;
    if(cap<8192 || cap>JOURNAL_MAX_RECORD*2u || cap%4096 || limit%4096 || limit<4096 || limit>INT64_MAX || strlen(path)>4096) return JOURNAL_INVALID;
    edit_arena a; if(edit_arena_init(&a,sizeof(journal)+2*(cap+4096)+8192)) return JOURNAL_NOMEM;
    journal *j=edit_arena_alloc(&a,sizeof *j,16); memset(j,0,sizeof *j); j->arena=a; j->capacity=cap; j->limit=limit; j->pool=pool;
    j->path=edit_arena_alloc(&j->arena,strlen(path)+1,1); strcpy(j->path,path); crc_init(j->crc_table);
    for(unsigned i=0;i<2;i++) { j->batches[i].bytes=edit_arena_alloc(&j->arena,cap+sizeof(journal_completion),4096); memset(j->batches[i].bytes,0,cap+sizeof(journal_completion)); }
    int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600); struct stat sb;
    if(fd<0 || fstat(fd,&sb) || !S_ISREG(sb.st_mode) || sb.st_size!=0) { int rc=fd<0?JOURNAL_IO:JOURNAL_INVALID; if(fd>=0) close(fd); edit_arena_free(&a); return rc; }
    int directory_rc=sync_parent(path);
    if(directory_rc) { close(fd); edit_arena_free(&a); return directory_rc; }
    j->disk.fd=fd; j->disk.stats.last_sync_ns=clock_ns(); j->stats=j->disk.stats; *out=j; return 0;
}
static int reserve_record(journal *j, size_t payload, uint8_t **p)
{
    if(!j || payload>JOURNAL_MAX_RECORD-32) return JOURNAL_INVALID;
    if(j->stats.error) return j->stats.error;
    journal_batch *b=&j->batches[j->current]; size_t n=payload+32;
    if(j->stats.accepted_sequence==UINT64_MAX || n>j->capacity-32 || b->used>j->capacity-32-n || j->reserved>j->limit || sealed_size(b->used+n)>j->limit-j->reserved) { j->stats.error=JOURNAL_FULL; return JOURNAL_FULL; }
    *p=b->bytes+b->used; return 0;
}
static void finish_record(journal *j, uint8_t *p, uint32_t type, uint64_t id, size_t payload)
{
    journal_batch *b=&j->batches[j->current]; j->stats.accepted_sequence++;
    encode(p,type,id,j->stats.accepted_sequence,payload+32,j->crc_table); b->used+=payload+32; b->sequence=j->stats.accepted_sequence;
}
int journal_append(journal *j, uint32_t type, uint64_t id, const uint8_t *data, size_t size)
{
    if((!data && size) || !payload_valid(type,data,size)) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,size,&p); if(rc) return rc;
    memcpy(p+32,data,size); finish_record(j,p,type,id,size); return 0;
}
int journal_insert(journal *j, uint64_t id, uint64_t off, const uint8_t *bytes, size_t size)
{
    if((!bytes && size) || size>JOURNAL_MAX_RECORD-40 || off>UINT64_MAX-size) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,size+8,&p); if(rc) return rc;
    put64(p+32,off); if(size) memcpy(p+40,bytes,size); finish_record(j,p,JOURNAL_INSERT,id,size+8); return 0;
}
int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len)
{ uint8_t data[16]; if(off>UINT64_MAX-len) return JOURNAL_INVALID; put64(data,off); put64(data+8,len); return journal_append(j,JOURNAL_DELETE,id,data,16); }
int journal_set_base(journal *j, uint64_t id, const journal_base *base)
{
    if(!base || !base->path || strlen(base->path)>4096 || base->prefix_len>4096 || base->prefix_len>base->size) return JOURNAL_INVALID;
    uint8_t data[4137]; put64(data,base->size); put64(data+8,base->mtime_ns); put64(data+16,base->inode); put64(data+24,base->device); put32(data+32,base->prefix_crc); put32(data+36,base->prefix_len);
    size_t n=strlen(base->path)+1; memcpy(data+40,base->path,n); return journal_append(j,JOURNAL_BASE,id,data,40+n);
}
int journal_set_view(journal *j, uint64_t id, const journal_view *v)
{ if(!v) return JOURNAL_INVALID; uint8_t data[32]; put64(data,v->cursor); put64(data+8,v->anchor); put64(data+16,v->scroll_byte); put64(data+24,v->scroll_x); return journal_append(j,JOURNAL_VIEW,id,data,32); }
int journal_set_tabs(journal *j, const uint64_t *ids, size_t count, uint64_t active)
{
    if((!ids && count) || count>(JOURNAL_MAX_RECORD-48)/8 || (count?active>=count:active!=0)) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,16+count*8,&p); if(rc) return rc;
    put64(p+32,count); put64(p+40,active); for(size_t i=0;i<count;i++) put64(p+48+i*8,ids[i]); finish_record(j,p,JOURNAL_TABS,0,16+count*8); return 0;
}
int journal_set_window(journal *j, uint32_t width, uint32_t height)
{ uint8_t data[8]; put32(data,width); put32(data+4,height); return journal_append(j,JOURNAL_WINDOW,0,data,8); }
int journal_pump(journal *j, uint64_t now, bool force)
{
    if(!j) return JOURNAL_INVALID;
    if(j->stats.error && j->stats.error!=JOURNAL_FULL) return j->stats.error;
    if(j->active) return JOURNAL_BUSY;
    journal_batch *b=&j->batches[j->current];
    uint64_t elapsed=now>=j->stats.last_sync_ns?now-j->stats.last_sync_ns:0;
    if(!force) {
        if(!b->used && (j->stats.written_sequence<=j->stats.durable_sequence || elapsed<SYNC_NS)) return 0;
        /* Coalesce a partial page until its time sync, avoiding a 4 KiB write
         * for every individual key when the timer runs at 5 ms. */
        if(b->used && b->used<JOURNAL_PAGE-JOURNAL_HEADER && elapsed<SYNC_NS) return 0;
    }
    b->sealed=0; b->force=force; b->offset=j->reserved;
    if(b->used) seal(b,j->crc_table);
    j->active_index=j->current;
    j->handle=work_submit(j->pool,(work_job){worker,j,0,WORK_BULK}); if(!j->handle.epoch) return JOURNAL_BUSY;
    j->active=true; j->reserved+=b->sealed; j->current^=1u; return 0;
}
bool journal_receive(journal *j, const work_msg *m)
{
    if(!j || !m || m->kind!=JOURNAL_MESSAGE || !j->active || m->slot_!=j->handle.slot || m->epoch_!=j->handle.epoch) return false;
    uintptr_t address; memcpy(&address,m->data,sizeof address);
    journal_batch *b=&j->batches[j->active_index];
    if(address!=(uintptr_t)(b->bytes+j->capacity)) return false;
    journal_completion *c=(journal_completion *)address;
    uint64_t accepted=j->stats.accepted_sequence; int sticky=j->stats.error;
    j->stats=c->stats; j->stats.accepted_sequence=accepted;
    if(!j->stats.error) j->stats.error=sticky;
    work_cancel(j->pool,j->handle);
    /* After publish the worker only polls its work_ctx; snapshot ownership
     * returns with mailbox acquire, without waiting for the worker's sleep. */
    b->used=0; b->sealed=0; j->active=false;
    return true;
}
journal_stats journal_get_stats(const journal *j) { return j?j->stats:(journal_stats){.error=JOURNAL_INVALID}; }
void journal_set_message_handler(journal *j, void (*handler)(const work_msg *, void *), void *ctx)
{
    if(j) { j->message_handler=handler; j->message_ctx=ctx; }
}
static void drain(const work_msg *m, void *ctx)
{
    journal *j=ctx;
    if(!journal_receive(j,m) && j->message_handler) j->message_handler(m,j->message_ctx);
}
int journal_flush(journal *j)
{
    if(!j) return JOURNAL_INVALID;
    if(j->stats.error && j->stats.error!=JOURNAL_FULL) return j->stats.error;
    for(;;) {
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return 0;
        work_mailbox_drain(j->pool,drain,j);
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return 0;
        int rc=journal_pump(j,clock_ns(),true); if(rc && rc!=JOURNAL_BUSY) return rc;
        if(j->stats.error && j->stats.error!=JOURNAL_FULL) return j->stats.error;
        delay();
    }
}
void journal_close(journal *j)
{
    if(!j) return;
    (void)journal_flush(j);
    if(j->active) { work_cancel(j->pool,j->handle); while(atomic_load_explicit(&j->pool->slots[j->handle.slot].busy,memory_order_acquire)) delay(); }
    close(j->disk.fd); edit_arena a=j->arena; edit_arena_free(&a);
}
int journal_rotate(journal *j, const journal_record *records, size_t count)
{
    if(!j || (!records && count)) return JOURNAL_INVALID;
    if(j->active || j->batches[j->current].used || j->stats.accepted_sequence!=j->stats.durable_sequence) return JOURNAL_BUSY;
    char tmp[4120], dir[4097]; size_t plen=strlen(j->path);
    memcpy(tmp,j->path,plen); memcpy(tmp+plen,".new-XXXXXX",12);
    int tfd=mkstemp(tmp); if(tfd<0) return JOURNAL_IO; close(tfd);
    strcpy(dir,j->path); char *slash=strrchr(dir,'/'); if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else strcpy(dir,".");
    int dfd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC); if(dfd<0) { unlink(tmp); return JOURNAL_IO; }
    journal *next=NULL; journal_options opt={j->capacity,j->limit}; int rc=journal_open(&next,tmp,j->pool,&opt);
    if(!rc) journal_set_message_handler(next,j->message_handler,j->message_ctx);
    for(size_t i=0;!rc && i<count;i++) {
        const journal_record *r=&records[i];
        if(r->size>JOURNAL_MAX_RECORD-32 || r->size+32>next->capacity-32) { rc=JOURNAL_INVALID; break; }
        if(next->batches[next->current].used>next->capacity-64-r->size) rc=journal_flush(next);
        if(!rc) rc=journal_append(next,r->type,r->buffer_id,r->data,r->size);
    }
    if(!rc && !count) rc=journal_pump(next,clock_ns(),true);
    if(!rc) rc=journal_flush(next);
    if(!rc && rename(tmp,j->path)) rc=JOURNAL_IO;
    if(!rc) {
        int oldfd=j->disk.fd;
        j->disk=next->disk; next->disk.fd=-1; j->stats=next->stats; j->reserved=next->reserved;
        for(unsigned i=0;i<2;i++) { j->batches[i].used=0; j->batches[i].sealed=0; }
        j->current=0;
        if(fsync(dfd)) { rc=JOURNAL_IO; j->stats.error=rc; }
        close(oldfd);
    }
    if(next) journal_close(next);
    close(dfd); unlink(tmp); return rc;
}
