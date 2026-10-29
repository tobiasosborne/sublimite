#include "journal/private.h"
#include "file/file.h"
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#if defined(__x86_64__)
#include <nmmintrin.h>
#endif

#define MAGIC 0x314c4e4au
#define PAD 0u
#define RETENTION_MESSAGE 0x4a52544eu

typedef struct journal_retention journal_retention;
typedef struct journal_crc {
    uint32_t table[256];
    bool hardware;
} journal_crc;

typedef struct journal_batch {
    uint8_t *bytes;
    size_t used, sealed, progress, cached;
    uint64_t offset, sequence;
    bool force, pad_failed;
} journal_batch;
typedef struct journal_disk {
    os_file fd, worker_fd;
    uint64_t unsynced, written_sequence;
    journal_stats stats;
    bool writeback_failed;    /* only a fresh complete checkpoint can repair */
} journal_disk;
struct journal {
    edit_arena arena;
    work_pool *pool;
    work_handle handle;
    journal_batch batches[2];
    journal_disk disk;          /* UI fd and worker transport have separate ownership */
    journal_stats stats;        /* exclusively UI owned */
    journal_crc crc_table;
    size_t capacity;
    uint64_t limit, log_budget, reserved, sync_bytes, sync_interval_ns;
    char *path, *name;
    os_dir directory_fd;
    unsigned current, active_index;
    bool active, failed, directory_pending, recovery_pending;
    bool append_failed, append_pending;
    uint64_t pending_sequence;
    int append_error;
    journal_io io;
    void (*message_handler)(const work_msg *, void *);
    void *message_ctx;
    journal_retention *retention; /* immutable result adopted through mailbox */
    work_handle retention_handle;
};
typedef struct journal_completion { journal_stats stats; } journal_completion;
/* A completion snapshot lives in the active arena batch after its data.
 * Only its address crosses the work mailbox; publish transfers ownership. */
static uint64_t clock_ns(void)
{ return os_now_ns(); }
static void delay(void) { os_sleep_ns(1000000); }
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t u64(const uint8_t *p)
{ return (uint64_t)u32(p) | (uint64_t)u32(p+4)<<32; }
static void put32(uint8_t *p, uint32_t v)
{ for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put64(uint8_t *p, uint64_t v)
{ for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void crc_init(journal_crc *crc)
{
    for(uint32_t i=0;i<256;i++) { uint32_t v=i; for(unsigned k=0;k<8;k++) v=(v>>1)^((v&1u)?0x82f63b78u:0u); crc->table[i]=v; }
    crc->hardware=false;
#if defined(__x86_64__) && !defined(EDIT_JOURNAL_PORTABLE_CRC)
    /* Setup only; dispatch state belongs to this encoder/parser instance. */
    __builtin_cpu_init();
    crc->hardware=__builtin_cpu_supports("sse4.2")!=0;
#endif
}
#if defined(__x86_64__)
__attribute__((target("sse4.2")))
static uint32_t crc_hardware(uint32_t c, const uint8_t *p, size_t n)
{
    while(n>=8) {
        uint64_t word; memcpy(&word,p,sizeof word);
        c=(uint32_t)_mm_crc32_u64(c,word); p+=8; n-=8;
    }
    while(n) { c=_mm_crc32_u8(c,*p++); n--; }
    return c;
}
#endif
static uint32_t crc_update(const journal_crc *crc, uint32_t c, const uint8_t *p, size_t n)
{
#if defined(__x86_64__)
    if(crc->hardware) return crc_hardware(c,p,n);
#endif
    for(size_t i=0;i<n;i++) c=crc->table[(c^p[i])&255u]^(c>>8);
    return c;
}
static uint32_t crc_data(const journal_crc *crc, const uint8_t *p, size_t n)
{ return ~crc_update(crc,UINT32_MAX,p,n); }
static uint32_t crc_record(const journal_crc *crc, const uint8_t *p, size_t n)
{
    const uint8_t zero[4]={0};
    uint32_t c=crc_update(crc,UINT32_MAX,p,12);
    c=crc_update(crc,c,zero,sizeof zero);
    return ~crc_update(crc,c,p+16,n-16);
}
static size_t sealed_size(size_t used)
{ return (used+JOURNAL_HEADER+JOURNAL_PAGE-1u)&~(size_t)(JOURNAL_PAGE-1u); }
static void encode(uint8_t *p, uint32_t type, uint64_t id, uint64_t seq, size_t n)
{
    put32(p,MAGIC); put32(p+4,(uint32_t)n); put32(p+8,type); put32(p+12,0);
    put64(p+16,seq); put64(p+24,id);
}
static void seal(journal *j, journal_batch *b)
{
    b->sealed=sealed_size(b->used); size_t n=b->sealed-b->used;
    memset(b->bytes+b->used,0,n); encode(b->bytes+b->used,PAD,0,0,n);
    put32(b->bytes+b->used+12,crc_record(&j->crc_table,b->bytes+b->used,n));
}
static bool payload_valid(uint32_t type, const uint8_t *p, size_t n)
{
    switch(type) {
    case JOURNAL_BASE: {
        bool v2=n>=40 && (u32(p+36)&JOURNAL_BASE_V2)!=0;
        size_t tail=v2?8u:0u;
        uint32_t prefix=n>=40?u32(p+36)&~JOURNAL_BASE_V2:0;
        if(n<41+tail || n>4137+tail || p[n-tail-1]!=0 ||
           memchr(p+40,0,n-tail-41)!=NULL || prefix>4096 || prefix>u64(p)) return false;
        if(p[40]!=0 && (!os_path_is_absolute((const char *)p+40) || prefix!=(u64(p)<4096?(uint32_t)u64(p):4096u))) return false;
        if(p[40]==0 && (v2 || u64(p)!=0 || u64(p+8)!=0 || u64(p+16)!=0 || u64(p+24)!=0 || u32(p+32)!=0 || prefix!=0)) return false;
        return true;
    }
    case JOURNAL_SAVE: return n>=57 && (u32(p+8)==1 || u32(p+8)==2) && u32(p+12)==0 && payload_valid(JOURNAL_BASE,p+16,n-16);
    case JOURNAL_GROUP_BEGIN: case JOURNAL_GROUP_COMMIT: case JOURNAL_PAINT: return n==0;
    case JOURNAL_INSERT: return n>=8;
    case JOURNAL_DELETE: return n==16 && u64(p)<=UINT64_MAX-u64(p+8);
    case JOURNAL_TARGET: return n>=1 && n<=4097 && p[n-1]==0 && memchr(p,0,n-1)==NULL;
    case JOURNAL_VIEW: return n==32 || (n==JOURNAL_VIEW_STATE_BYTES &&
        !(u64(p+56) & ~(JOURNAL_VIEW_WRAP | JOURNAL_VIEW_APPROXIMATE | JOURNAL_VIEW_VISUAL_END)));
    case JOURNAL_TABS: return n>=16 && (n-16)%8==0 && u64(p)==(n-16)/8 && (u64(p)==0 ? u64(p+8)==0 : u64(p+8)<u64(p));
    case JOURNAL_WINDOW: return n==JOURNAL_WINDOW_LEGACY_BYTES ||
        (n==JOURNAL_WINDOW_BYTES && !(u32(p+8) & ~JOURNAL_WINDOW_CHOSEN));
    default: return false;
    }
}
static bool valid_record(const uint8_t *p, size_t n, uint64_t offset, uint64_t seq, const journal_crc *table)
{
    if(u32(p)!=MAGIC || u32(p+4)!=n || crc_record(table,p,n)!=u32(p+12)) return false;
    uint32_t type=u32(p+8);
    if(type==PAD) {
        if(n>JOURNAL_PAGE+JOURNAL_HEADER-1u || (offset+n)%JOURNAL_PAGE || u64(p+16) || u64(p+24)) return false;
        for(size_t i=32;i<n;i++) if(p[i]) return false;
        return true;
    }
    return (!(type==JOURNAL_TABS || type==JOURNAL_WINDOW || type==JOURNAL_GROUP_BEGIN || type==JOURNAL_GROUP_COMMIT) || u64(p+24)==0) &&
        seq!=UINT64_MAX && u64(p+16)==seq+1 && payload_valid(type,p+32,n-32);
}
static int visit_record(const uint8_t *p, size_t n, journal_visit visit, void *ctx, journal_replay_result *r)
{
    if(u32(p+8)!=PAD) {
        journal_record rec={u32(p+8),u64(p+24),u64(p+16),p+32,n-32};
        if(visit && rec.type!=JOURNAL_GROUP_BEGIN && rec.type!=JOURNAL_GROUP_COMMIT && visit(ctx,&rec)) return JOURNAL_CALLBACK;
        r->records++; r->last_sequence=rec.sequence;
    }
    r->valid_bytes+=n; return 0;
}
static int read_at(os_file fd, uint8_t *p, size_t n, uint64_t off, size_t *got)
{
    *got=0;
    while(*got<n) {
        size_t k=0;
        if(os_file_pread(&fd,p+*got,n-*got,off+*got,&k)!=OS_OK) return JOURNAL_IO;
        if(k==0) break;
        *got+=k;
    }
    return 0;
}
static int write_at(const journal_io *io, os_file fd, const uint8_t *p, size_t n, uint64_t off, size_t *done)
{
    *done=0;
    while(*done<n) {
        size_t k=0;
        if(io->write) {
            ssize_t result=io->write(io->ctx,journal_hook_file(&fd),p+*done,n-*done,off+*done);
            if(result<0 && errno==EINTR) continue;
            if(result<=0) return JOURNAL_IO;
            k=(size_t)result;
        } else if(os_file_pwrite(&fd,p+*done,n-*done,off+*done,&k)!=OS_OK) return JOURNAL_IO;
        if(!k || k>n-*done) return JOURNAL_IO;
        *done+=k;
    }
    return 0;
}
static int data_sync(os_file fd)
{ return os_file_sync_data(&fd)==OS_OK?0:JOURNAL_IO; }
static int io_sync(const journal_io *io, os_file fd)
{
    if(!io || !io->sync) return data_sync(fd);
    int rc;
    do { rc=io->sync(io->ctx,journal_hook_file(&fd),false); } while(rc<0 && errno==EINTR);
    return rc<0?JOURNAL_IO:0;
}
static int directory_sync(const journal_io *io, os_dir *dir)
{
    if(!io || !io->sync) return os_dir_sync(dir)==OS_OK?0:JOURNAL_IO;
    int rc;
    do { rc=io->sync(io->ctx,journal_hook_dir(dir),true); } while(rc<0 && errno==EINTR);
    return rc<0?JOURNAL_IO:0;
}
static int metadata_sync(const journal_io *io, os_file fd)
{
    if(!io->metadata_sync) return os_file_sync(&fd)==OS_OK?0:JOURNAL_IO;
    int rc;
    do { rc=io->metadata_sync(io->ctx,journal_hook_file(&fd)); } while(rc<0 && errno==EINTR);
    return rc<0?JOURNAL_IO:0;
}
static int check_base_io(const journal_base *base, const journal_io *io);
/* Replay is off the typing path. Validate ahead to COMMIT, then reread that
 * span for delivery: streaming memory remains one existing record arena,
 * independent of group length. Byte-image replay borrows the caller's bytes. */
typedef struct replay_source {
    const uint8_t *bytes;
    size_t size;
    os_file fd;
    uint8_t *scratch;
    const journal_io *io;
    bool readonly; /* edit-rfq scan: no BASE validation, no truncation */
} replay_source;
static int replay_read(replay_source *source, uint64_t off, uint64_t seq,
                       const journal_crc *table, const uint8_t **out, size_t *size)
{
    const uint8_t *p;
    size_t n;
    *size=0;
    if(source->bytes || !os_file_is_open(&source->fd)) {
        if(off==source->size) return 0;
        if(off>source->size || source->size-(size_t)off<JOURNAL_HEADER) return JOURNAL_INVALID;
        p=source->bytes+(size_t)off; n=u32(p+4);
        if(n>source->size-(size_t)off) return JOURNAL_INVALID;
    } else {
        size_t got;
        int rc=read_at(source->fd,source->scratch,JOURNAL_HEADER,off,&got);
        if(rc || !got) return rc;
        if(got!=JOURNAL_HEADER) return JOURNAL_INVALID;
        p=source->scratch; n=u32(p+4);
        if(n<JOURNAL_HEADER || n>JOURNAL_MAX_RECORD) return JOURNAL_INVALID;
        rc=read_at(source->fd,source->scratch+JOURNAL_HEADER,n-JOURNAL_HEADER,off+JOURNAL_HEADER,&got);
        if(rc) return rc;
        if(got!=n-JOURNAL_HEADER) return JOURNAL_INVALID;
    }
    if(n<JOURNAL_HEADER || n>JOURNAL_MAX_RECORD || !valid_record(p,n,off,seq,table)) return JOURNAL_INVALID;
    *out=p; *size=n; return 0;
}
static int replay_deliver(replay_source *source, const uint8_t *p, size_t n,
                          journal_visit visit, void *ctx, journal_replay_result *result)
{
    if(os_file_is_open(&source->fd) && !source->readonly && u32(p+8)==JOURNAL_BASE) {
        journal_record rec={JOURNAL_BASE,u64(p+24),u64(p+16),p+JOURNAL_HEADER,n-JOURNAL_HEADER};
        journal_base base; char path[sizeof base.captured_path];
        int rc=journal_decode_base(&rec,&base,path,sizeof path);
        if(!rc) rc=check_base_io(&base,source->io);
        if(rc) return rc;
    }
    return visit_record(p,n,visit,ctx,result);
}
static int replay_source_records(replay_source *source, journal_visit visit, void *ctx,
                                  journal_replay_result *result)
{
    *result=(journal_replay_result){0};
    journal_replay_result scan={0}; journal_crc table; crc_init(&table);
    bool group=false;
    for(;;) {
        const uint8_t *p=NULL; size_t n;
        int rc=replay_read(source,scan.valid_bytes,scan.last_sequence,&table,&p,&n);
        if(rc==JOURNAL_INVALID || (!rc && !n && group)) { result->corrupt=true; break; }
        if(rc || !n) return rc;
        uint32_t type=u32(p+8);
        if((type==JOURNAL_GROUP_BEGIN && group) || (type==JOURNAL_GROUP_COMMIT && !group)) {
            result->corrupt=true; break;
        }
        if(type==JOURNAL_GROUP_BEGIN) group=true;
        (void)visit_record(p,n,NULL,NULL,&scan);
        if(!group) {
            rc=replay_deliver(source,p,n,visit,ctx,result); if(rc) return rc;
        } else if(type==JOURNAL_GROUP_COMMIT) {
            /* No callback has seen any byte in this span before validation of
             * its final commit. Revalidate rereads before delivering them. */
            while(result->valid_bytes<scan.valid_bytes) {
                rc=replay_read(source,result->valid_bytes,result->last_sequence,&table,&p,&n);
                if(rc || !n) return rc?rc:JOURNAL_IO;
                rc=replay_deliver(source,p,n,visit,ctx,result); if(rc) return rc;
            }
            group=false;
        }
    }
    return 0;
}
int journal_replay_bytes(const uint8_t *bytes, size_t size, journal_visit visit, void *ctx, journal_replay_result *result)
{
    if(!result || (!bytes && size)) return JOURNAL_INVALID;
    replay_source source={.bytes=bytes,.size=size,.fd=OS_FILE_INIT};
    return replay_source_records(&source,visit,ctx,result);
}
static int replay_file_mode(const char *path, journal_visit visit, void *ctx, journal_replay_result *result, const journal_io *io, bool readonly)
{
    if(!path || !result) return JOURNAL_INVALID;
    *result=(journal_replay_result){0}; os_file fd=OS_FILE_INIT;
    if(os_file_open(&fd,path,(readonly?OS_OPEN_READ:OS_OPEN_RDWR)|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK)!=OS_OK) return JOURNAL_IO;
    edit_arena a; if(edit_arena_init(&a,JOURNAL_MAX_RECORD)) { os_file_close(&fd); return JOURNAL_NOMEM; }
    replay_source source={.fd=fd,.scratch=edit_arena_alloc(&a,JOURNAL_MAX_RECORD,16),.io=io,.readonly=readonly};
    int rc=replay_source_records(&source,visit,ctx,result);
    if(!readonly && !rc && result->corrupt) { if(os_file_set_size(&fd,result->valid_bytes)) rc=JOURNAL_IO; else rc=data_sync(fd); }
    edit_arena_free(&a); os_file_close(&fd); return rc;
}
int journal_scan_open(os_file input, journal_visit visit, void *ctx, journal_replay_result *result)
{
    *result=(journal_replay_result){0};
    edit_arena a; if(edit_arena_init(&a,JOURNAL_MAX_RECORD)) return JOURNAL_NOMEM;
    replay_source source={.fd=input,.scratch=edit_arena_alloc(&a,JOURNAL_MAX_RECORD,16),.readonly=true};
    int rc=replay_source_records(&source,visit,ctx,result);
    edit_arena_free(&a); return rc;
}
int journal_scan_file(const char *path, journal_visit visit, void *ctx, journal_replay_result *result)
{ return replay_file_mode(path,visit,ctx,result,NULL,true); }
int journal_replay_file_with_io(const char *path, journal_visit visit, void *ctx, journal_replay_result *result, const journal_io *io)
{ return replay_file_mode(path,visit,ctx,result,io,false); }
int journal_replay_file(const char *path, journal_visit visit, void *ctx, journal_replay_result *result)
{ return journal_replay_file_with_io(path,visit,ctx,result,NULL); }
static bool stat_equal(const os_stat *a, const os_stat *b)
{ return journal_same_stat(a,b); }
static int capture_open(os_file fd, const os_dir *dir, const char *name,
                        const char *canonical, journal_base *base, const journal_io *io)
{
    os_stat before,after,atpath; uint8_t prefix[4096]; journal_crc table; size_t got=0; int rc=0;
    if(os_file_stat(&fd,&before) || before.kind!=OS_KIND_FILE || before.mtime_ns<0 || before.id.index[1]!=0) rc=JOURNAL_BASE_CHANGED;
    /* The existing BASE wire identity holds volume + a 64-bit file index.
     * Refuse an unrepresentable identity rather than discard its high bits. */
    if(!rc) {
        uint64_t base_size=(uint64_t)before.size;
        size_t wanted=base_size<sizeof prefix?(size_t)base_size:sizeof prefix;
        if(!io || !io->read) {
            if(read_at(fd,prefix,wanted,0,&got) || got!=wanted) rc=JOURNAL_BASE_CHANGED;
        } else while(got<wanted) {
            ssize_t k=io->read(io->ctx,journal_hook_file(&fd),prefix+got,wanted-got,got);
            if(k<0 && errno==EINTR) continue;
            if(k<=0 || (size_t)k>wanted-got) { rc=JOURNAL_BASE_CHANGED; break; }
            got+=(size_t)k;
        }
    }
    if(!rc && (os_file_stat(&fd,&after) || (dir?os_stat_at(dir,name,0,&atpath):os_stat_path(canonical,0,&atpath)) || !stat_equal(&before,&after) || !stat_equal(&after,&atpath))) rc=JOURNAL_BASE_CHANGED;
    if(!rc) {
        crc_init(&table); *base=(journal_base){.size=(uint64_t)before.size,
            .mtime_ns=(uint64_t)before.mtime_ns,
            .inode=(uint64_t)before.id.index[0],.device=(uint64_t)before.id.volume,
            .ctime_ns=(uint64_t)before.ctime_ns,
            .prefix_crc=crc_data(&table,prefix,got),.prefix_len=(uint32_t)got};
        strcpy(base->captured_path,canonical); base->path=base->captured_path;
    }
    return rc;
}
int journal_capture_base_with_io(const char *path, journal_base *base, const journal_io *io)
{
    if(!path || !base) return JOURNAL_INVALID;
    if(!*path) { *base=(journal_base){.path=path}; return 0; }
    char canonical[4097];
    if(strlen(path)>4096 || !journal_canonical(path,canonical,sizeof canonical)) return JOURNAL_BASE_CHANGED;
    os_file fd=OS_FILE_INIT; if(os_file_open(&fd,canonical,OS_OPEN_READ|OS_OPEN_NONBLOCK)!=OS_OK) return JOURNAL_BASE_CHANGED;
    int rc=capture_open(fd,NULL,NULL,canonical,base,io);
    os_file_close(&fd); return rc;
}
int journal_base_from_prefix(const char *path, const file_id *identity,
                             const uint8_t *prefix, size_t size, journal_base *base)
{
    if(!path || !os_path_is_absolute(path) || strlen(path)>4096 || !identity || !base ||
       !identity->exists || identity->type!=0100000u /* legacy file_id.type: POSIX regular-file tag */ || (!prefix && size)) return JOURNAL_INVALID;
    size_t n=identity->size<4096?(size_t)identity->size:4096u;
    if(size<n) return JOURNAL_INVALID;
    journal_crc crc; crc_init(&crc);
    *base=(journal_base){.size=identity->size,.mtime_ns=identity->mtime_ns,
        .ctime_ns=identity->ctime_ns,.inode=identity->ino,.device=identity->dev,
        .prefix_crc=crc_data(&crc,prefix,n),.prefix_len=(uint32_t)n};
    strcpy(base->captured_path,path); journal_slashes(base->captured_path); base->path=base->captured_path;
    return 0;
}
int journal_capture_base(const char *path, journal_base *base)
{ return journal_capture_base_with_io(path,base,NULL); }
static int check_base_io(const journal_base *base, const journal_io *io)
{
    if(!base || !base->path) return JOURNAL_INVALID;
    journal_base now; int rc=journal_capture_base_with_io(base->path,&now,io); if(rc) return rc;
    return base->size==now.size && base->mtime_ns==now.mtime_ns && base->ctime_ns==now.ctime_ns && base->inode==now.inode && base->device==now.device && base->prefix_len==now.prefix_len && base->prefix_crc==now.prefix_crc?0:JOURNAL_BASE_CHANGED;
}
int journal_check_base(const journal_base *base)
{ return check_base_io(base,NULL); }
int journal_decode_base(const journal_record *r, journal_base *base, char *path, size_t cap)
{
    if(!r || !base || !path || !r->data || r->type!=JOURNAL_BASE || !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
    bool v2=(u32(r->data+36)&JOURNAL_BASE_V2)!=0;
    size_t path_size=r->size-40-(v2?8u:0u);
    if(cap<path_size) return JOURNAL_INVALID;
    memcpy(path,r->data+40,path_size);
    *base=(journal_base){.size=u64(r->data),.mtime_ns=u64(r->data+8),.inode=u64(r->data+16),.device=u64(r->data+24),.prefix_crc=u32(r->data+32),.prefix_len=u32(r->data+36)&~JOURNAL_BASE_V2,.path=path,
        .ctime_ns=v2?u64(r->data+r->size-8):0};
    return 0;
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
static size_t prefix_bytes(const journal_batch *b, size_t upto)
{
    size_t pos=0;
    while(pos+32<=upto) {
        size_t n=u32(b->bytes+pos+4);
        if(n>upto-pos) break;
        pos+=n;
    }
    return pos;
}
static int sync_disk(journal *j, uint64_t sequence, uint64_t complete_bytes)
{
    journal_disk *d=&j->disk;
    int rc=io_sync(&j->io,d->worker_fd);
    if(rc) { d->writeback_failed=true; return rc; }
    uint64_t now=clock_ns(); journal_stats *s=&d->stats;
    uint64_t interval=now-s->last_sync_ns;
    if(d->unsynced && interval>s->max_sync_interval_ns) s->max_sync_interval_ns=interval;
    s->last_sync_ns=now; s->last_sync_bytes=d->unsynced;
    if(d->unsynced>s->max_sync_bytes) s->max_sync_bytes=d->unsynced;
    s->syncs++; s->durable_sequence=sequence;
    if(complete_bytes>s->durable_bytes) s->durable_bytes=complete_bytes;
    d->unsynced=0; return 0;
}
static void worker(work_ctx *ctx)
{
    journal *j=ctx->arg; journal_batch *b=&j->batches[j->active_index]; journal_disk *d=&j->disk;
    size_t pos=b->progress; int rc=0; uint64_t initial=d->written_sequence;
    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence,d->stats.file_bytes);
    while(!rc && pos<b->sealed && !work_should_stop(ctx)) {
        size_t n=b->sealed-pos; uint64_t room=j->sync_bytes-d->unsynced; if(n>room) n=(size_t)room;
        size_t done=0;
        /* Account the UI-written prefix at the existing sync boundaries.
         * Only a failed/short UI write (or its PAD) needs another data write. */
        if(pos<b->cached) { if(n>b->cached-pos) n=b->cached-pos; done=n; }
        else rc=write_at(&j->io,d->worker_fd,b->bytes+pos,n,b->offset+pos,&done);
        pos+=done; b->progress=pos; d->unsynced+=done; d->written_sequence=prefix_sequence(b,pos,initial);
        d->stats.written_sequence=d->written_sequence; d->stats.file_bytes=b->offset+pos;
        if(rc) break;
        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos)); if(rc) break; }
    }
    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos));
    d->stats.error=rc;
    journal_completion *completion=(journal_completion *)(void *)(b->bytes+j->capacity);
    completion->stats=d->stats;
    work_msg msg={.kind=JOURNAL_MESSAGE}; uintptr_t address=(uintptr_t)completion; memcpy(msg.data,&address,sizeof address);
    while(!work_should_stop(ctx) && !work_publish(ctx,&msg)) delay();
    /* work keeps an undrained message slot reserved (P1.8b). Return immediately. */
}
static int sync_parent(const char *path, const journal_io *io)
{
    char directory[4097]; strcpy(directory,path); journal_slashes(directory);
    char *slash=strrchr(directory,'/');
    journal_parent(directory,slash);
    os_dir fd=OS_DIR_INIT;
    if(os_dir_open(&fd,directory)!=OS_OK) return JOURNAL_IO;
    int rc=directory_sync(io,&fd);
    os_dir_close(&fd); return rc;
}
/* Resolve the existing parent without following the journal leaf. All live
 * namespace operations use the retained directory fd, even after chdir. */
static int journal_path(const char *path, char *canonical, size_t *name_offset, os_dir *dfd)
{
    if(!*path || strlen(path)>4096) return JOURNAL_INVALID;
    char directory[4097], parent[4097]; strcpy(directory,path); journal_slashes(directory);
    char *slash=strrchr(directory,'/'); const char *name=slash?path+(slash-directory)+1:path;
    if(!*name || !strcmp(name,".") || !strcmp(name,"..")) return JOURNAL_INVALID;
    journal_parent(directory,slash);
    if(!journal_canonical(directory,parent,sizeof parent)) return JOURNAL_IO;
    size_t n=strlen(parent), leaf=strlen(name); bool root=n>0 && parent[n-1]=='/';
    if(n+(!root?1u:0u)+leaf>4096) return JOURNAL_INVALID;
    strcpy(canonical,parent); if(!root) canonical[n++]='/';
    strcpy(canonical+n,name); *name_offset=n;
    return os_dir_open(dfd,parent)==OS_OK?0:JOURNAL_IO;
}
static bool options_valid(const journal_options *options)
{
    size_t cap=options && options->batch_bytes?options->batch_bytes:JOURNAL_DEFAULT_BATCH_BYTES;
    uint64_t limit=options && options->max_file_bytes?options->max_file_bytes:JOURNAL_DEFAULT_FILE_BYTES;
    uint64_t sync_bytes=options && options->sync_bytes?options->sync_bytes:JOURNAL_DEFAULT_SYNC_BYTES;
    return cap>=8192 && cap<=JOURNAL_MAX_RECORD*2u && cap%4096==0 &&
        limit>=4096 && limit<=(uint64_t)INT64_MAX && limit%4096==0 &&
        sync_bytes>=JOURNAL_PAGE && sync_bytes<=(uint64_t)INT64_MAX && sync_bytes%JOURNAL_PAGE==0;
}
/* Takes fd/directory ownership only on success. Used for live and temporary
 * journals, avoiding temporary full-path length restrictions. */
static int journal_create(journal **out, const char *path, size_t name_offset,
                           os_file fd, const char *transport_name, os_dir dfd, work_pool *pool,
                           const journal_options *options, const journal_io *io)
{
    size_t cap=options && options->batch_bytes?options->batch_bytes:JOURNAL_DEFAULT_BATCH_BYTES;
    uint64_t limit=options && options->max_file_bytes?options->max_file_bytes:JOURNAL_DEFAULT_FILE_BYTES;
    uint64_t sync_bytes=options && options->sync_bytes?options->sync_bytes:JOURNAL_DEFAULT_SYNC_BYTES;
    uint64_t sync_interval_ns=options && options->sync_interval_ns?options->sync_interval_ns:JOURNAL_DEFAULT_SYNC_NS;
    if(!options_valid(options)) return JOURNAL_INVALID;
    edit_arena a; if(edit_arena_init(&a,sizeof(journal)+2*(cap+4096)+8192)) return JOURNAL_NOMEM;
    journal *j=edit_arena_alloc(&a,sizeof *j,16); memset(j,0,sizeof *j); j->arena=a; j->capacity=cap;
    j->limit=limit; j->log_budget=limit; j->pool=pool; if(io) j->io=*io;
    j->sync_bytes=sync_bytes; j->sync_interval_ns=sync_interval_ns;
    j->path=edit_arena_alloc(&j->arena,strlen(path)+1,1); strcpy(j->path,path); j->name=j->path+name_offset;
    j->directory_fd=dfd; crc_init(&j->crc_table);
    for(unsigned i=0;i<2;i++) { j->batches[i].bytes=edit_arena_alloc(&j->arena,cap+sizeof(journal_completion),4096); memset(j->batches[i].bytes,0,cap+sizeof(journal_completion)); }
    int rc=directory_sync(&j->io,&dfd); if(rc) { edit_arena_free(&a); return rc; }
    os_file worker_fd=OS_FILE_INIT;
#if defined(_WIN32)
    /* DuplicateHandle shares the synchronous file object, so independently
     * reopen the pinned generation and verify it before any worker can run. */
    os_stat ui,worker_stat;
    if(os_file_open_at(&worker_fd,&dfd,transport_name,OS_OPEN_RDWR|OS_OPEN_NOFOLLOW)!=OS_OK ||
       os_file_stat(&fd,&ui) || os_file_stat(&worker_fd,&worker_stat) ||
       !journal_same_id(&ui,&worker_stat)) rc=JOURNAL_IO;
#else
    (void)transport_name;
    if(os_file_dup(&worker_fd,&fd)!=OS_OK) rc=JOURNAL_IO;
#endif
    if(rc) { os_file_close(&worker_fd); edit_arena_free(&a); return rc; }
    j->disk.fd=fd; j->disk.worker_fd=worker_fd; j->disk.stats.last_sync_ns=clock_ns();
    j->disk.stats.file_limit_bytes=limit; j->disk.stats.queue_bytes=2*cap;
    j->stats=j->disk.stats; *out=j; return 0;
}
int journal_open_with_io(journal **out, const char *path, work_pool *pool, const journal_options *options, const journal_io *io)
{
    if(!out || !path || !pool) return JOURNAL_INVALID;
    *out=NULL; if(!options_valid(options)) return JOURNAL_INVALID;
    char canonical[4097]; size_t name_offset=0; os_dir dfd=OS_DIR_INIT;
    int rc=journal_path(path,canonical,&name_offset,&dfd); if(rc) return rc;
    os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dfd,canonical+name_offset,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_NOFOLLOW); os_stat sb;
    if(opened!=OS_OK || os_file_stat(&fd,&sb) || sb.kind!=OS_KIND_FILE || sb.size!=0) rc=opened!=OS_OK?JOURNAL_IO:JOURNAL_INVALID;
    else if(os_file_set_mode(&fd,0600)) rc=JOURNAL_IO;
    else rc=journal_create(out,canonical,name_offset,fd,canonical+name_offset,dfd,pool,options,io);
    if(rc) { os_file_close(&fd); os_dir_close(&dfd); } return rc;
}
int journal_open(journal **out, const char *path, work_pool *pool, const journal_options *options)
{ return journal_open_with_io(out,path,pool,options,NULL); }
int journal_open_checkpoint(journal **out, const char *path, work_pool *pool, const journal_options *options)
{
    if(!out || !path || !pool || !options_valid(options)) return JOURNAL_INVALID;
    *out=NULL;
    char canonical[4097]; size_t name_offset=0; os_dir dfd=OS_DIR_INIT;
    int rc=journal_path(path,canonical,&name_offset,&dfd); if(rc) return rc;
    os_file fd=OS_FILE_INIT; os_err opened=os_file_open_at(&fd,&dfd,canonical+name_offset,OS_OPEN_RDWR|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK); os_stat sb;
    if(opened!=OS_OK || os_file_stat(&fd,&sb) || sb.kind!=OS_KIND_FILE || sb.size<=0) rc=opened!=OS_OK?JOURNAL_IO:JOURNAL_INVALID;
    else rc=journal_create(out,canonical,name_offset,fd,canonical+name_offset,dfd,pool,options,NULL);
    if(rc) { os_file_close(&fd); os_dir_close(&dfd); }
    else (*out)->recovery_pending=true;
    return rc;
}
int journal_set_io(journal *j, const journal_io *io)
{ if(!j) return JOURNAL_INVALID; if(j->active || j->retention) return JOURNAL_BUSY; j->io=io?*io:(journal_io){0}; return 0; }
int journal_retry(journal *j)
{
    if(!j) return JOURNAL_INVALID;
    if(j->active) return JOURNAL_BUSY;
    if(j->stats.error!=JOURNAL_IO) return JOURNAL_INVALID;
    /* A consumed writeback error can leave clean, lost pages anywhere in the
     * unsynced generation, including batches already returned to the UI. */
    if(j->disk.writeback_failed) return JOURNAL_IO;
    if(j->directory_pending) {
        int rc=directory_sync(&j->io,&j->directory_fd); if(rc) return rc;
        j->directory_pending=false; j->stats.durable_sequence=j->disk.stats.durable_sequence;
        j->stats.durable_bytes=j->disk.stats.durable_bytes;
    }
    j->stats.error=j->append_error; j->disk.stats.error=0;
    j->append_failed=false; j->stats.append_errno=0;
    return 0;
}
static int reserve_record(journal *j, size_t payload, uint8_t **p)
{
    if(!j || payload>JOURNAL_MAX_RECORD-32) return JOURNAL_INVALID;
    if(j->recovery_pending) return JOURNAL_BUSY;
    if(j->stats.error) return j->stats.error;
    if(j->append_pending) return JOURNAL_BUSY; /* retry must first close the wire gap */
    journal_batch *b=&j->batches[j->current]; size_t n=payload+32;
    if(j->stats.accepted_sequence==UINT64_MAX || n>j->capacity-32 || b->used>j->capacity-32-n || j->reserved>j->limit || sealed_size(b->used+n)>j->limit-j->reserved) { j->stats.error=JOURNAL_FULL; j->append_error=JOURNAL_FULL; return JOURNAL_FULL; }
    *p=b->bytes+b->used; return 0;
}
/* Exactly one bounded syscall attempt, never an EINTR/short-write retry on UI.
 * Preserve its exact prefix; immutable queued bytes let the worker finish it. */
static int cache_write(journal *j, journal_batch *b, size_t start, size_t n)
{
    size_t done=0;
    int error=EIO;
    os_err e=OS_OK;
    if(j->io.append_write) {
        ssize_t k=j->io.append_write(j->io.ctx,journal_hook_file(&j->disk.fd),b->bytes+start,n,j->reserved+start);
        if(k<0) { error=errno; e=OS_E_IO; }
        else done=(size_t)k;
    } else {
        e=os_file_pwrite_once(&j->disk.fd,b->bytes+start,n,j->reserved+start,&done);
        if(e!=OS_OK) error=os_native_error();
    }
    if(e==OS_OK && done>0 && done<=n) {
        b->cached=start+done;
        uint64_t end=j->reserved+b->cached;
        if(end>j->stats.file_bytes) j->stats.file_bytes=end;
    }
    if(e==OS_OK && done==n) return 0;
    j->append_failed=true; j->append_pending=true; j->pending_sequence=b->sequence;
    j->stats.error=JOURNAL_IO; j->stats.append_errno=error; return JOURNAL_IO;
}
static int finish_record(journal *j, uint8_t *p, uint32_t type, uint64_t id, size_t payload)
{
    journal_batch *b=&j->batches[j->current]; size_t start=b->used; j->stats.accepted_sequence++;
    encode(p,type,id,j->stats.accepted_sequence,payload+32); b->used+=payload+32; b->sequence=j->stats.accepted_sequence;
    put32(p+12,crc_record(&j->crc_table,p,payload+32));
    if(j->append_pending) { j->pending_sequence=b->sequence; return JOURNAL_IO; }
    /* A pump whose work_submit was BUSY may already have cached a PAD here. */
    b->cached=start;
    int rc=cache_write(j,b,start,payload+32);
    if(!rc) j->stats.written_sequence=b->sequence;
    return rc;
}
int journal_append(journal *j, uint32_t type, uint64_t id, const uint8_t *data, size_t size)
{
    if((!data && size) || ((type==JOURNAL_TABS || type==JOURNAL_WINDOW || type==JOURNAL_GROUP_BEGIN || type==JOURNAL_GROUP_COMMIT) && id) || !payload_valid(type,data,size)) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,size,&p); if(rc) return rc;
    if(size) memcpy(p+32,data,size);
    return finish_record(j,p,type,id,size);
}
int journal_insert(journal *j, uint64_t id, uint64_t off, const uint8_t *bytes, size_t size)
{
    if(!j || (!bytes && size) || off>UINT64_MAX-size) return JOURNAL_INVALID;
    if(j->recovery_pending) return JOURNAL_BUSY;
    if(j->stats.error) return j->stats.error;
    if(j->append_pending) return JOURNAL_BUSY;
    size_t chunk=j->capacity-72; if(chunk>JOURNAL_MAX_RECORD-40) chunk=JOURNAL_MAX_RECORD-40;
    size_t records=size?1+(size-1)/chunk:1;
    if(records>(SIZE_MAX-size)/40) return JOURNAL_INVALID;
    size_t total=size+records*40; journal_batch *b=&j->batches[j->current];
    if(records>UINT64_MAX-j->stats.accepted_sequence || total>j->capacity-32 ||
       b->used>j->capacity-32-total || j->reserved>j->limit || sealed_size(b->used+total)>j->limit-j->reserved) {
        j->stats.error=JOURNAL_FULL; j->append_error=JOURNAL_FULL; return JOURNAL_FULL;
    }
    size_t copied=0; int rc=0;
    do {
        size_t n=size-copied; if(n>chunk) n=chunk;
        uint8_t *p=b->bytes+b->used; put64(p+32,off+copied);
        if(n) memcpy(p+40,bytes+copied,n);
        int finished=finish_record(j,p,JOURNAL_INSERT,id,n+8); if(finished) rc=finished;
        copied+=n;
    } while(copied<size);
    return rc;
}
int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len)
{ uint8_t data[16]; if(off>UINT64_MAX-len) return JOURNAL_INVALID; put64(data,off); put64(data+8,len); return journal_append(j,JOURNAL_DELETE,id,data,16); }
int journal_append_protected(journal *j, uint32_t type, uint64_t id, const uint8_t *data, size_t size)
{
    uint8_t *record;
    int rc=reserve_record(j,size,&record); if(rc) return rc;
    uint64_t end=j->reserved+sealed_size(j->batches[j->current].used+size+32);
    if(end>j->stats.durable_bytes && end-j->stats.durable_bytes>j->sync_bytes) return JOURNAL_BUSY;
    return journal_append(j,type,id,data,size);
}
int journal_delete_protected(journal *j, uint64_t id, uint64_t off, uint64_t len)
{
    if(off>UINT64_MAX-len) return JOURNAL_INVALID;
    uint8_t data[16]; put64(data,off); put64(data+8,len);
    return journal_append_protected(j,JOURNAL_DELETE,id,data,sizeof data);
}
int journal_insert_step(journal *j, uint64_t id, uint64_t off,
                        const uint8_t *bytes, size_t size, size_t *progress)
{
    if(!j || !progress || *progress>size || (!bytes && size) ||
       off>UINT64_MAX-size) return JOURNAL_INVALID;
    if(*progress==size) return 0;
    size_t n=size-*progress;
    if(n>JOURNAL_INSERT_SLICE_BYTES) n=JOURNAL_INSERT_SLICE_BYTES;
    if(n>j->capacity-72) n=j->capacity-72;
    if(n>j->sync_bytes-72) n=(size_t)(j->sync_bytes-72);
    uint8_t *record;
    int preflight=reserve_record(j,n+8,&record); if(preflight) return preflight;
    /* Reserve the eventual PAD too. Credit is returned only by a received
     * durability fence at a COMPLETE record boundary, never by worker-accounted
     * bytes or a partial CRC-protected record. This remains conservative when
     * the kernel sync also covers newer UI writes in the other batch. */
    journal_batch *b=&j->batches[j->current];
    uint64_t end=j->reserved+sealed_size(b->used+n+40);
    if(end>j->stats.durable_bytes && end-j->stats.durable_bytes>j->sync_bytes)
        return JOURNAL_BUSY;
    uint64_t accepted=j->stats.accepted_sequence;
    int rc=journal_insert(j,id,off+*progress,bytes+*progress,n);
    /* IO after encoding still owns this record in the fixed queue. Sticky IO,
     * FULL or a prior transport gap accept nothing; never skip those bytes. */
    if(j->stats.accepted_sequence!=accepted) *progress+=n;
    return rc?rc:(*progress<size?JOURNAL_BUSY:JOURNAL_OK);
}
int journal_encode_base(const journal_base *base, uint8_t *data, size_t cap, size_t *size)
{
    if(!base || !base->path || !data || !size || strlen(base->path)>4096 || base->prefix_len>4096 || base->prefix_len>base->size) return JOURNAL_INVALID;
    size_t n=strlen(base->path)+1, tail=*base->path?8u:0u;
    if(cap<40+n+tail) return JOURNAL_INVALID;
    put64(data,base->size); put64(data+8,base->mtime_ns); put64(data+16,base->inode); put64(data+24,base->device);
    put32(data+32,base->prefix_crc); put32(data+36,base->prefix_len|(tail?JOURNAL_BASE_V2:0));
    memcpy(data+40,base->path,n);
    if(tail) put64(data+40+n,base->ctime_ns);
    *size=40+n+tail;
    return payload_valid(JOURNAL_BASE,data,*size)?0:JOURNAL_INVALID;
}
static int base_data(const journal_base *base, uint8_t *data, size_t *size)
{ return journal_encode_base(base,data,JOURNAL_BASE_MAX,size); }

int journal_set_base(journal *j, uint64_t id, const journal_base *base)
{
    uint8_t data[JOURNAL_BASE_MAX]; size_t n=0; int rc=base_data(base,data,&n);
    return rc?rc:journal_append(j,JOURNAL_BASE,id,data,n);
}
int journal_set_view(journal *j, uint64_t id, const journal_view *v)
{ if(!v) return JOURNAL_INVALID; uint8_t data[32]; put64(data,v->cursor); put64(data+8,v->anchor); put64(data+16,v->scroll_byte); put64(data+24,v->scroll_x); return journal_append(j,JOURNAL_VIEW,id,data,32); }
void journal_encode_view_state(const journal_view_state *s, uint8_t out[JOURNAL_VIEW_STATE_BYTES])
{
    put64(out,s->position.cursor); put64(out+8,s->position.anchor);
    put64(out+16,s->position.scroll_byte); put64(out+24,s->position.scroll_x);
    put64(out+32,s->preferred_col); put64(out+40,s->first_line);
    put64(out+48,s->visual_byte); put64(out+56,s->flags);
}
int journal_decode_view_state(const journal_record *r, journal_view_state *out)
{
    if(!r || !out || r->type!=JOURNAL_VIEW || !r->data || !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
    const uint8_t *p=r->data;
    *out=(journal_view_state){.position={u64(p),u64(p+8),u64(p+16),u64(p+24)},
        .preferred_col=UINT64_MAX,.visual_byte=u64(p+16)};
    if(r->size==JOURNAL_VIEW_STATE_BYTES) {
        out->preferred_col=u64(p+32); out->first_line=u64(p+40);
        out->visual_byte=u64(p+48); out->flags=u64(p+56);
    }
    return JOURNAL_OK;
}
int journal_set_tabs(journal *j, const uint64_t *ids, size_t count, uint64_t active)
{
    if((!ids && count) || count>(JOURNAL_MAX_RECORD-48)/8 || (count?active>=count:active!=0)) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,16+count*8,&p); if(rc) return rc;
    put64(p+32,count); put64(p+40,active); for(size_t i=0;i<count;i++) put64(p+48+i*8,ids[i]); return finish_record(j,p,JOURNAL_TABS,0,16+count*8);
}
void journal_encode_window(const journal_window *w, uint8_t out[JOURNAL_WINDOW_BYTES])
{ put32(out,w->width); put32(out+4,w->height); put32(out+8,w->flags); }
int journal_decode_window(const journal_record *r, journal_window *out)
{
    if(!r || !out || r->type!=JOURNAL_WINDOW || !r->data || !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
    *out=(journal_window){u32(r->data),u32(r->data+4),r->size==JOURNAL_WINDOW_BYTES?u32(r->data+8):0};
    return JOURNAL_OK;
}
int journal_set_window(journal *j, uint32_t width, uint32_t height)
{
    uint8_t data[JOURNAL_WINDOW_BYTES]; journal_window w={width,height,JOURNAL_WINDOW_CHOSEN};
    journal_encode_window(&w,data); return journal_append(j,JOURNAL_WINDOW,0,data,sizeof data);
}
int journal_pump(journal *j, uint64_t now, bool force)
{
    if(j && j->recovery_pending) return JOURNAL_BUSY;
    if(!j) return JOURNAL_INVALID;
    if(j->stats.error && j->stats.error!=JOURNAL_FULL && (!j->append_failed || j->failed || j->directory_pending)) return j->stats.error;
    if(j->active) return JOURNAL_BUSY;
    unsigned index=j->failed?j->active_index:j->current;
    journal_batch *b=&j->batches[index];
    int append_rc=0;
    uint64_t elapsed=now>=j->stats.last_sync_ns?now-j->stats.last_sync_ns:0;
    if(!j->failed) {
        if(!force && !j->append_pending) {
            if(!b->used && (j->stats.written_sequence<=j->stats.durable_sequence || elapsed<j->sync_interval_ns)) return 0;
            if(b->used && b->used<JOURNAL_PAGE-JOURNAL_HEADER && elapsed<j->sync_interval_ns) return 0;
        }
        b->sealed=0; b->progress=0; b->offset=j->reserved;
        if(b->used) {
            seal(j,b);
            /* A page gap would hide successful appends in the next batch on
             * SIGKILL. Cache PAD before transferring this batch to the worker. */
            if(b->cached==b->used && !b->pad_failed) {
                append_rc=cache_write(j,b,b->used,b->sealed-b->used);
                b->pad_failed=append_rc!=0;
            } else if(b->pad_failed) append_rc=JOURNAL_IO;
        }
    }
    b->force=force || (j->failed && b->force);

    j->active_index=index;
    j->handle=work_submit(j->pool,(work_job){worker,j,0,WORK_BULK});
    if(!j->handle.epoch) return append_rc?append_rc:JOURNAL_BUSY;
    j->active=true;
    if(!j->failed) { j->reserved+=b->sealed; j->current^=1u; }
    return append_rc;
}
static bool retention_receive(journal *j, const work_msg *m);
bool journal_receive(journal *j, const work_msg *m)
{
    if(j && m && retention_receive(j,m)) return true;
    if(!j || !m || m->kind!=JOURNAL_MESSAGE || !j->active || m->slot_!=j->handle.slot || m->epoch_!=j->handle.epoch) return false;
    uintptr_t address; memcpy(&address,m->data,sizeof address);
    journal_batch *b=&j->batches[j->active_index];
    if(address!=(uintptr_t)(b->bytes+j->capacity)) return false;
    journal_completion *c=(journal_completion *)address;
    uint64_t accepted=j->stats.accepted_sequence, written=j->stats.written_sequence, file_bytes=j->stats.file_bytes;
    int sticky=j->stats.error, append_errno=j->stats.append_errno;
    j->stats=c->stats; j->stats.accepted_sequence=accepted;
    if(written>j->stats.written_sequence) j->stats.written_sequence=written;
    if(file_bytes>j->stats.file_bytes) j->stats.file_bytes=file_bytes;
    j->stats.append_errno=append_errno;
    if(!j->stats.error) j->stats.error=sticky;
    j->failed=c->stats.error!=0;
    if(!j->failed) {
        if(b->sequence>=j->pending_sequence) j->append_pending=false;
        b->used=0; b->sealed=0; b->progress=0; b->cached=0;
        b->pad_failed=false;
    }
    j->active=false;
    return true;
}
bool journal_in_flight(const journal *j) { return j && j->active; }
journal_stats journal_get_stats(const journal *j)
{
    if(!j) return (journal_stats){.error=JOURNAL_INVALID};
    journal_stats s=j->stats; s.pending_bytes=j->batches[0].used+j->batches[1].used;
    s.unprotected_bytes=s.file_bytes>s.durable_bytes?s.file_bytes-s.durable_bytes:0;
    return s;
}
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
    if(j->stats.error && j->stats.error!=JOURNAL_FULL && (!j->append_failed || j->failed || j->directory_pending)) return j->stats.error;
    for(;;) {
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return j->stats.error;
        work_mailbox_drain(j->pool,drain,j);
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return j->stats.error;
        int rc=journal_pump(j,clock_ns(),true);
        if(rc && rc!=JOURNAL_BUSY && !(rc==JOURNAL_IO && j->append_failed && !j->failed && !j->directory_pending)) return rc;
        if(j->stats.error && j->stats.error!=JOURNAL_FULL && (!j->append_failed || j->failed || j->directory_pending)) return j->stats.error;
        delay();
    }
}
static void close_journal(journal *j, bool discard_empty)
{
    if(!j) return;
    (void)journal_flush(j);
    if(j->active) { work_cancel(j->pool,j->handle); while(atomic_load_explicit(&j->pool->slots[j->handle.slot].busy,memory_order_acquire)) delay(); }
    if(discard_empty) {
        os_stat owned,named;
        if(!os_file_stat(&j->disk.fd,&owned) && owned.kind==OS_KIND_FILE && !owned.size &&
           !os_stat_at(&j->directory_fd,j->name,OS_STAT_NOFOLLOW,&named) &&
           journal_same_id(&owned,&named) &&
           !os_unlink_at(&j->directory_fd,j->name)) (void)directory_sync(&j->io,&j->directory_fd);
    }
    os_file_close(&j->disk.worker_fd); os_file_close(&j->disk.fd); os_dir_close(&j->directory_fd); edit_arena a=j->arena; edit_arena_free(&a);
}
void journal_close(journal *j) { close_journal(j,false); }
void journal_close_discard_empty(journal *j) { close_journal(j,true); }
/* Exact transport size, with the same batch packing/PAD strategy as build.
 * Validate the entire checkpoint before creating anything or growing a limit. */
static int checkpoint_size(const journal *j, const journal_record *records, size_t count, uint64_t *bytes)
{
    size_t used=0; uint64_t total=0;
    for(size_t i=0;i<count;i++) {
        const journal_record *r=&records[i];
        if(!r->data || r->size>JOURNAL_MAX_RECORD-32 || r->size+32>j->capacity-32 ||
           ((r->type==JOURNAL_TABS || r->type==JOURNAL_WINDOW || r->type==JOURNAL_GROUP_BEGIN || r->type==JOURNAL_GROUP_COMMIT) && r->buffer_id) ||
           !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
        if(used>j->capacity-64-r->size) { total+=sealed_size(used); used=0; }
        used+=r->size+32;
        if(total>INT64_MAX-j->capacity) return JOURNAL_FULL;
    }
    if(used) total+=sealed_size(used);
    if(total>(uint64_t)INT64_MAX-j->log_budget) return JOURNAL_FULL;
    *bytes=total; return 0;
}
static int checkpoint_temp(const journal *j, char *name, size_t cap, os_file *out)
{
    os_stat source; if(os_file_stat(&j->disk.fd,&source)) return -1;
    /* edit-dr9: failed originals may lack accepted edits in this checkpoint.
     * Explicit clean/failed flags survive owner death; old unflagged names
     * cannot prove a clean start. Journal wire bytes remain unchanged. */
    const char *state=(j->failed || j->append_failed)?"!":"+";
    uint64_t nonce=clock_ns();
    for(unsigned i=0;i<64;i++) {
        /* edit-52o (P): original device/inode/ctime identify a surviving copy using
         * directory metadata alone; PID/nonce/attempt retain name uniqueness.
         * The inherited collision ceiling above is unchanged. */
        int n=snprintf(name,cap,".sublimite-journal-%sfrom-%jx-%jx-%jx-%jx-%lx-%llx-%x-",
            state,(uintmax_t)source.id.volume,(uintmax_t)source.id.index[0],
            (uintmax_t)(source.ctime_ns/1000000000),(uintmax_t)(source.ctime_ns%1000000000),
            (unsigned long)os_proc_id(),(unsigned long long)nonce,i);
        if(n<0 || (size_t)n>=cap) return -1;
        /* (P) Each original-basename byte takes two hex digits. Exact names
         * prevent a temp/new session reusing the old inode within a timestamp
         * tick from proving its own redundant copy. Very long names retain
         * the independent legacy spelling, conservatively set aside if killed. */
        size_t length=strlen(j->name), prefix=(size_t)n;
        /* (P) Reserve the set-aside literal, a time_t bit count (a safe
         * upper bound on decimal seconds including sign), and nine decimal
         * nanosecond digits. A killed temp must still fit its aside name. */
        size_t aside=sizeof ".set-aside-"-1u+sizeof(time_t)*CHAR_BIT+sizeof "999999999"-1u;
        if(source.id.index[1]!=0 || cap<=aside || prefix>=cap-aside || length>(cap-aside-prefix-1u)/2u) {
            n=snprintf(name,cap,".sublimite-journal-%s%lx-%llx-%x",state,(unsigned long)os_proc_id(),(unsigned long long)nonce,i);
            if(n<0 || (size_t)n>=cap) return -1;
        } else {
            static const char hex[]="0123456789abcdef";
            for(size_t k=0;k<length;k++) {
                unsigned byte=(unsigned char)j->name[k];
                name[prefix+2u*k]=hex[byte/16u]; name[prefix+2u*k+1u]=hex[byte%16u];
            }
            name[prefix+2u*length]=0;
        }
        os_err e=os_file_open_at(out,&j->directory_fd,name,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL|OS_OPEN_NOFOLLOW);
        if(e==OS_OK && os_file_set_mode(out,0600)!=OS_OK) {
            os_file_close(out); (void)os_unlink_at(&j->directory_fd,name); return -1;
        }
        if(e==OS_OK) return 0;
        if(e!=OS_E_EXIST) return -1;
    }
    return -1;
}
int journal_rotate(journal *j, const journal_record *records, size_t count)
{
    if(!j || (!records && count)) return JOURNAL_INVALID;
    if(j->active || (!j->failed && !j->append_failed && (j->batches[j->current].used || j->stats.accepted_sequence!=j->stats.durable_sequence))) return JOURNAL_BUSY;
    uint64_t checkpoint_bytes=0; int rc=checkpoint_size(j,records,count,&checkpoint_bytes); if(rc) return rc;
    /* edit-52o: the directory inode is the rotation owner's lease. Take it
     * before publishing any temp name, keep it through rename/sync/cleanup.
     * Retirement's existing exclusive nonblocking flock proves owner death
     * without PID reuse, timers, extra files, or a create-before-lock window.
     * A separate open description avoids changing the retained fd's locks.
     * edit-dr9: UI checkpoint callers must retry instead of waiting for an
     * inventory already in flight. No temp exists before this admission. */
    os_dir guard=OS_DIR_INIT;
    if(os_dir_open_at(&guard,&j->directory_fd,".")!=OS_OK) return JOURNAL_IO;
    os_err locked=os_dir_try_lock(&guard,OS_LOCK_SHARED);
    if(locked!=OS_OK) {
        rc=locked==OS_E_BUSY?JOURNAL_BUSY:JOURNAL_IO;
        os_dir_close(&guard); return rc;
    }
    /* (P) Generated names are ASCII: NTFS permits 255 UTF-16 units,
     * each of these bytes consumes one unit. OS_NAME_MAX is a UTF-8 storage
     * bound for arbitrary input, not a generated-component budget. */
#if defined(_WIN32)
    char tmpname[255u+1u];
#else
    char tmpname[NAME_MAX+1u];
#endif
    os_file tfd=OS_FILE_INIT;
    if(checkpoint_temp(j,tmpname,sizeof tmpname,&tfd)) { os_dir_close(&guard); return JOURNAL_IO; }
    os_dir dfd=OS_DIR_INIT;
    if(os_dir_open_at(&dfd,&j->directory_fd,".")!=OS_OK) {
        os_unlink_at(&j->directory_fd,tmpname); os_file_close(&tfd); os_dir_close(&guard); return JOURNAL_IO;
    }
    journal *next=NULL; journal_options opt={.batch_bytes=j->capacity,.max_file_bytes=checkpoint_bytes+j->log_budget,
        .sync_bytes=j->sync_bytes,.sync_interval_ns=j->sync_interval_ns};
    rc=journal_create(&next,j->path,(size_t)(j->name-j->path),tfd,tmpname,dfd,j->pool,&opt,&j->io);
    if(rc) { os_file_close(&tfd); os_dir_close(&dfd); }
    if(!rc) journal_set_message_handler(next,j->message_handler,j->message_ctx);
    for(size_t i=0;!rc && i<count;i++) {
        const journal_record *r=&records[i];
        if(r->size>JOURNAL_MAX_RECORD-32 || r->size+32>next->capacity-32) { rc=JOURNAL_INVALID; break; }
        if(next->batches[next->current].used>next->capacity-64-r->size) rc=journal_flush(next);
        if(!rc) rc=journal_append(next,r->type,r->buffer_id,r->data,r->size);
    }
    if(!rc && !count) rc=journal_pump(next,clock_ns(),true);
    if(!rc) rc=journal_flush(next);
    bool renamed=false;
    if(!rc) {
        /* (P) Existing full-path capacity plus an independent temp basename. */
        char tmppath[sizeof ((journal_base *)0)->captured_path+sizeof tmpname]; size_t parent=(size_t)(j->name-j->path);
        memcpy(tmppath,j->path,parent); strcpy(tmppath+parent,tmpname);
        if(j->io.rename?j->io.rename(j->io.ctx,tmppath,j->path)==0:
           os_rename_at(&j->directory_fd,tmpname,&j->directory_fd,j->name,OS_RENAME_REPLACE)==OS_OK) renamed=true;
        else {
            /* An ambiguous error after replacement must never permit writes
             * to the now-unlinked old inode. Single-instance owns this name. */
            os_stat replacement, named;
            renamed=os_file_stat(&next->disk.fd,&replacement)==0 && os_stat_at(&j->directory_fd,j->name,OS_STAT_NOFOLLOW,&named)==0 &&
                journal_same_id(&replacement,&named);
            rc=JOURNAL_IO;
        }
    }
    if(renamed) {
        if(j->io.rotation_point) j->io.rotation_point(j->io.ctx,JOURNAL_ROTATION_REPLACED);
        j->recovery_pending=false;
        os_file oldfd=j->disk.fd, oldworker=j->disk.worker_fd;
        j->disk=next->disk; next->disk.fd=(os_file)OS_FILE_INIT; next->disk.worker_fd=(os_file)OS_FILE_INIT; j->stats=next->stats; j->reserved=next->reserved;
        for(unsigned i=0;i<2;i++) {
            j->batches[i].used=0; j->batches[i].sealed=0; j->batches[i].cached=0;
            j->batches[i].progress=0; j->batches[i].pad_failed=false;
        }
        j->current=0; j->failed=false; j->append_error=0;
        j->append_failed=false; j->append_pending=false; j->pending_sequence=0;
        j->limit=next->limit; j->disk.stats.checkpoint_bytes=checkpoint_bytes; j->stats.checkpoint_bytes=checkpoint_bytes;
        j->directory_pending=true;
        if(rc || directory_sync(&j->io,&j->directory_fd)) {
            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0; j->stats.durable_bytes=0;
        } else j->directory_pending=false;
        if(j->io.rotation_point) j->io.rotation_point(j->io.ctx,JOURNAL_ROTATION_ADOPTED);
        os_file_close(&oldworker); os_file_close(&oldfd);
    }
    if(next) journal_close(next);
    if(renamed && j->io.rotation_point) j->io.rotation_point(j->io.ctx,JOURNAL_ROTATION_CLEANUP);
    os_unlink_at(&j->directory_fd,tmpname); os_dir_close(&guard); return rc;
}


struct journal_retention {
    const journal_base *previous;
    const char *path;
    const os_dir *directory;
    const char *name;
    const journal_io *io;
    os_file fd;
    int error;
    journal_base base;
    bool received; /* exclusively UI owned; no shared polling flag */
};
static bool retention_receive(journal *j, const work_msg *m)
{
    if(m->kind!=RETENTION_MESSAGE || !j->retention ||
       m->slot_!=j->retention_handle.slot || m->epoch_!=j->retention_handle.epoch) return false;
    uintptr_t address; memcpy(&address,m->data,sizeof address);
    if(address!=(uintptr_t)j->retention) return false;
    j->retention->received=true;
    return true;
}
/* Bounded copy: no whole-file mapping/allocation or bulk I/O on the owner
 * thread; cancellation is checked before every chunk. */
static int retain_copy(work_ctx *ctx, journal_retention *r, os_file source)
{
    uint8_t bytes[65536]; uint64_t off=0;
    if(os_file_set_size(&r->fd,0)) return JOURNAL_IO;
    while(off<r->previous->size) {
        if(work_should_stop(ctx)) return JOURNAL_IO;
        size_t n=sizeof bytes, got=0, written=0;
        if(r->previous->size-off<n) n=(size_t)(r->previous->size-off);
        if(read_at(source,bytes,n,off,&got) || got!=n) return JOURNAL_BASE_CHANGED;
        int rc=write_at(r->io,r->fd,bytes,n,off,&written); if(rc) return rc;
        off+=n;
    }
    return 0;
}
/* The absolute BASE path must still resolve through the leased parent. */
static bool retention_parent(const os_dir *dir, const char *path)
{
    char parent[sizeof ((journal_base *)0)->captured_path]; strcpy(parent,path);
    journal_parent(parent,strrchr(parent,'/'));
    os_stat held,named;
    return os_stat_at(dir,".",0,&held)==OS_OK && os_stat_path(parent,0,&named)==OS_OK &&
           journal_same_id(&held,&named);
}
static int capture_retained(journal_retention *r, journal_base *base)
{
    if(!retention_parent(r->directory,r->path)) return JOURNAL_BASE_CHANGED;
    int rc=capture_open(r->fd,r->directory,r->name,r->path,base,r->io);
    if(!rc && !retention_parent(r->directory,r->path)) rc=JOURNAL_BASE_CHANGED;
    return rc;
}
static int retain_generation(work_ctx *ctx, journal_retention *r)
{
    os_file source=OS_FILE_INIT;
    if(os_file_open(&source,r->previous->path,OS_OPEN_READ|OS_OPEN_NOFOLLOW)!=OS_OK) return JOURNAL_BASE_CHANGED;
    os_stat before,after,named; const journal_base *p=r->previous; int rc=0;
    if(os_file_stat(&source,&before) || before.kind!=OS_KIND_FILE || before.mtime_ns<0 ||
       (uint64_t)before.size!=p->size || (uint64_t)before.id.index[0]!=p->inode || (uint64_t)before.id.volume!=p->device ||
       (uint64_t)before.mtime_ns!=p->mtime_ns ||
       (uint64_t)before.ctime_ns!=p->ctime_ns) rc=JOURNAL_BASE_CHANGED;
    /* The frozen clone wrapper retries EINTR without a work epoch. Copy in
     * bounded chunks so every interruption boundary remains cancellable. */
    if(!rc) rc=retain_copy(ctx,r,source);
    /* The retained inode has its own identity. Check its expected bytes, and
     * detect source changes throughout clone/copy using size/mtime/ctime and
     * pathname identity, including changes outside the small BASE prefix. */
    if(!rc) rc=capture_retained(r,&r->base);
    if(!rc && (r->base.size!=p->size || r->base.prefix_len!=p->prefix_len || r->base.prefix_crc!=p->prefix_crc ||
               (r->base.inode==p->inode && r->base.device==p->device))) rc=JOURNAL_BASE_CHANGED;
    if(!rc && (os_file_stat(&source,&after) || os_stat_path(p->path,0,&named) || !stat_equal(&before,&after) || !stat_equal(&after,&named))) rc=JOURNAL_BASE_CHANGED;
    os_file_close(&source);
    if(!rc) rc=metadata_sync(r->io,r->fd);
    if(!rc) rc=directory_sync(r->io,(os_dir *)r->directory);
    return rc;
}
static void retention_worker(work_ctx *ctx)
{
    journal_retention *r=ctx->arg;
    r->error=retain_generation(ctx,r);
    work_msg msg={.kind=RETENTION_MESSAGE}; uintptr_t address=(uintptr_t)r;
    memcpy(msg.data,&address,sizeof address);
    while(!work_should_stop(ctx) && !work_publish(ctx,&msg)) delay();
    /* No result storage is touched after work_publish transfers ownership. */
}
static int retain_base(journal *j, const journal_base *previous, char *path, journal_base *base)
{
    char parent[sizeof ((journal_base *)0)->captured_path]; strcpy(parent,path);
    char *name=strrchr(path,'/')+1; journal_parent(parent,strrchr(parent,'/'));
    os_dir guard=OS_DIR_INIT;
    if(os_dir_open(&guard,parent)!=OS_OK) return JOURNAL_IO;
    os_err locked=os_dir_try_lock(&guard,OS_LOCK_SHARED);
    if(locked!=OS_OK) { os_dir_close(&guard); return locked==OS_E_BUSY?JOURNAL_BUSY:JOURNAL_IO; }
    os_file fd=OS_FILE_INIT;
    if(journal_temp_at(&fd,&guard,name,0)!=OS_OK) { os_dir_close(&guard); return JOURNAL_IO; }
    journal_retention r={.previous=previous,.path=path,.directory=&guard,.name=name,.io=&j->io,.fd=fd};
    j->retention=&r;
    do {
        j->retention_handle=work_submit(j->pool,(work_job){retention_worker,&r,0,WORK_BULK});
        if(!j->retention_handle.epoch) { work_mailbox_drain(j->pool,drain,j); delay(); }
    } while(!j->retention_handle.epoch);
    while(!r.received) {
        work_mailbox_drain(j->pool,drain,j); delay();
    }
    j->retention=NULL;
    os_file_close(&fd);
    if(r.error) (void)os_unlink_at(&guard,name);
    os_dir_close(&guard);
    if(r.error) { path[0]=0; return r.error; }
    *base=r.base; base->path=path;
    return 0;
}
/* Recovery artifacts belong beside the journal, whose lifetime and access
 * already protect the recovery session. For a maximal journal parent use its
 * nearest existing ancestor that can encode the private leaf. */
static void retention_name(const journal *j, char out[4097])
{
    size_t n=(size_t)(j->name-j->path);
    const char name[]=".sublimite-base-XXXXXX";
    while(n>4097-sizeof name-1u) {
        n--; while(n>1 && j->path[n-1]!='/') n--;
    }
    memcpy(out,j->path,n);
    memcpy(out+n,name,sizeof name);
}
/* edit-fi2: the asynchronous form of retain_base. The worker creates the
 * temp itself, so the UI does no namespace I/O. */
static void retain_async_worker(work_ctx *ctx)
{
    journal_retain *r=ctx->arg;
    /* edit-7cj8: an incomplete copy is never a journal BASE. Its distinct
     * staging name is swept after owner death regardless of the artifact cap.
     * The shared directory lease starts before creation, so housekeeping's
     * exclusive lease cannot race a live copy or its durable promotion. */
    _Static_assert(sizeof ".sublimite-copy-"==sizeof ".sublimite-base-", "equal retention prefix lengths");
    char pending[sizeof r->path], directory[sizeof r->path];
    strcpy(pending,r->path);
    char *leaf=strrchr(pending,'/')+1;
    size_t parent=(size_t)(leaf-pending);
    memcpy(leaf,".sublimite-copy-",sizeof ".sublimite-copy-"-1u);
    memcpy(directory,pending,parent); directory[parent]=0;
    r->error=0;
    os_dir guard=OS_DIR_INIT; os_file fd=OS_FILE_INIT;
    bool promoted=false;
    if(os_dir_open(&guard,directory)!=OS_OK) r->error=JOURNAL_IO;
    else {
        os_err locked=os_dir_try_lock(&guard,OS_LOCK_SHARED);
        if(locked!=OS_OK) r->error=locked==OS_E_BUSY?JOURNAL_BUSY:JOURNAL_IO;
    }
    if(!r->error && r->io->retention_locked) r->io->retention_locked(r->io->ctx,&guard);
    if(!r->error && journal_temp_at(&fd,&guard,leaf,0)!=OS_OK) r->error=JOURNAL_IO;
    if(os_file_is_open(&fd)) {
        journal_retention t={.previous=&r->source,.path=pending,.directory=&guard,.name=leaf,.io=r->io,.fd=fd};
        r->error=retain_generation(ctx,&t);
        if(!r->error) {
            strcpy(r->path,pending);
            memcpy(r->path+parent,".sublimite-base-",sizeof ".sublimite-base-"-1u);
            if(os_rename_at(&guard,leaf,&guard,r->path+parent,OS_RENAME_NOREPLACE)) r->error=JOURNAL_IO;
            else {
                promoted=true;
                /* Rename advances ctime. Capture and persist that final
                 * identity before the caller can reference the copy. */
                t.path=r->path; t.name=r->path+parent;
                r->error=capture_retained(&t,&r->copy);
                if(!r->error) r->error=metadata_sync(r->io,fd);
                if(!r->error) r->error=directory_sync(r->io,&guard);
            }
        }
        os_file_close(&fd);
        if(r->error) {
            (void)os_unlink_at(&guard,leaf);
            if(promoted) (void)os_unlink_at(&guard,r->path+parent);
        }
    }
    os_dir_close(&guard);
    work_msg msg={.kind=JOURNAL_RETAIN_MESSAGE};
    (void)work_publish(ctx,&msg); /* wake-up only; a full mailbox loses nothing */
}
int journal_retain_start(journal *j, journal_retain *r, const journal_base *source, work_handle *handle)
{
    if(!j || !r || !source || !source->path || !os_path_is_absolute(source->path) || strlen(source->path)>4096 || !handle) return JOURNAL_INVALID;
    *r=(journal_retain){.source=*source,.io=&j->io,.error=JOURNAL_BUSY}; /* until the job reports */
    strcpy(r->source.captured_path,source->path); r->source.path=r->source.captured_path;
    retention_name(j,r->path);
    *handle=work_submit(j->pool,(work_job){retain_async_worker,r,0,WORK_BULK});
    return handle->epoch ? JOURNAL_OK : JOURNAL_BUSY;
}
/* A private reflink/copy freezes recovery data even when the old inode remains
 * writable through an external descriptor or another hard-link pathname. */
int journal_save_prepare(journal *j, uint64_t id, const journal_base *previous,
                         const journal_record *checkpoint, size_t count, journal_save *save)
{
    if(!j || !previous || !previous->path || !checkpoint || !count || !save || save->prepared ||
       count>j->limit/JOURNAL_HEADER || count>SIZE_MAX/sizeof(journal_record)-1 ||
       (*previous->path && !os_path_is_absolute(previous->path)) || strlen(previous->path)>4096) return JOURNAL_INVALID;
    uint8_t original[JOURNAL_BASE_MAX]; size_t original_size=0;
    int rc=base_data(previous,original,&original_size); if(rc) return rc;
    size_t found=count;
    for(size_t i=0;i<count;i++) if(checkpoint[i].type==JOURNAL_BASE && checkpoint[i].buffer_id==id) {
        if(found!=count || checkpoint[i].size!=original_size || !checkpoint[i].data ||
           memcmp(checkpoint[i].data,original,original_size)) return JOURNAL_INVALID;
        found=i;
    }
    if(found==count) return JOURNAL_INVALID;
    rc=journal_flush(j); if(rc && rc!=JOURNAL_FULL) return rc;
    rc=journal_check_base(previous); if(rc) return rc;
    *save=(journal_save){.buffer_id=id,.sequence=count};
    journal_base retained=*previous;
    if(*previous->path) {
        /* Recovery artifacts belong beside the journal, whose lifetime and
         * access already protect the recovery session. The worker copies in
         * bounded chunks, including across filesystems. */
        retention_name(j,save->previous_path);
        rc=retain_base(j,previous,save->previous_path,&retained);
        if(rc) return rc;
    }
    uint8_t retained_data[JOURNAL_BASE_MAX], marker[JOURNAL_BASE_MAX+16]={0}; size_t retained_size=0;
    rc=base_data(&retained,retained_data,&retained_size); if(rc) return rc;
    put64(marker,save->sequence); put32(marker+8,1); memcpy(marker+16,original,original_size);
    edit_arena a;
    if(edit_arena_init(&a,(count+1)*sizeof(journal_record)+16)) return JOURNAL_NOMEM;
    journal_record *records=edit_arena_alloc(&a,(count+1)*sizeof *records,16);
    memcpy(records,checkpoint,count*sizeof *records);
    /* Every buffer referencing this named identity loses its source on save.
     * Preserve them all, even when only one buffer's snapshot is being saved. */
    for(size_t i=0;i<count;i++) if(records[i].type==JOURNAL_BASE &&
        records[i].size==original_size && records[i].data &&
        !memcmp(records[i].data,original,original_size)) {
        records[i].data=retained_data; records[i].size=retained_size;
    }
    records[count]=(journal_record){JOURNAL_SAVE,id,0,marker,16+original_size};
    rc=journal_rotate(j,records,count+1); edit_arena_free(&a);
    /* A failed directory barrier can leave this new checkpoint installed.
     * Keep the retained generation on every ambiguous failure. */
    if(!rc) save->prepared=true;
    return rc;
}
int journal_save_finish(journal *j, journal_save *save, const journal_base *saved,
                        const journal_record *checkpoint, size_t count)
{
    if(!j || !save || !save->prepared || !saved || !saved->path || !*saved->path ||
       !checkpoint || !count || count>j->limit/JOURNAL_HEADER ||
       count>SIZE_MAX/sizeof(journal_record)-1) return JOURNAL_INVALID;
    uint8_t marker[JOURNAL_BASE_MAX+16]={0}; size_t n=0; int rc=base_data(saved,marker+16,&n); if(rc) return rc;
    bool found=false;
    for(size_t i=0;i<count;i++) if(checkpoint[i].type==JOURNAL_BASE) {
        journal_base b; char path[4097]; rc=journal_decode_base(&checkpoint[i],&b,path,sizeof path); if(rc) return rc;
        if(*save->previous_path && !strcmp(path,save->previous_path)) return JOURNAL_INVALID;
        if(checkpoint[i].buffer_id==save->buffer_id) {
            if(found || checkpoint[i].size!=n || memcmp(checkpoint[i].data,marker+16,n)) return JOURNAL_INVALID;
            found=true;
        }
    }
    if(!found) return JOURNAL_INVALID;
    rc=journal_check_base(saved); if(rc) return rc;
    rc=journal_flush(j); if(rc && rc!=JOURNAL_FULL) return rc;
    put64(marker,save->sequence); put32(marker+8,2);
    /* Persist the new identity in the replacement checkpoint before its
     * rename. This also works when append is FULL, just like plain rotation. */
    edit_arena a;
    if(edit_arena_init(&a,(count+1)*sizeof(journal_record)+16)) return JOURNAL_NOMEM;
    journal_record *records=edit_arena_alloc(&a,(count+1)*sizeof *records,16);
    memcpy(records,checkpoint,count*sizeof *records);
    records[count]=(journal_record){JOURNAL_SAVE,save->buffer_id,0,marker,n+16};
    rc=journal_rotate(j,records,count+1); edit_arena_free(&a);
    if(rc) return rc;
    /* The replacement checkpoint and its name are now durable. */
    if(*save->previous_path) {
        os_err removed=os_unlink(save->previous_path);
        if(removed!=OS_OK && removed!=OS_E_NOENT) return JOURNAL_IO;
        rc=sync_parent(save->previous_path,&j->io); if(rc) return rc;
    }
    save->prepared=false; return 0;
}

int journal_save_cancel(journal *j, journal_save *save, const journal_record *checkpoint, size_t count)
{
    if(!j || !save || (!save->prepared && !*save->previous_path) || !checkpoint || !count) return JOURNAL_INVALID;
    for(size_t i=0;i<count;i++) if(checkpoint[i].type==JOURNAL_BASE) {
        journal_base base; char path[4097];
        int rc=journal_decode_base(&checkpoint[i],&base,path,sizeof path); if(rc) return rc;
        if(*save->previous_path && !strcmp(path,save->previous_path)) return JOURNAL_INVALID;
        rc=journal_check_base(&base); if(rc) return rc;
    }
    int rc=journal_flush(j);
    if(rc && rc!=JOURNAL_IO && rc!=JOURNAL_FULL) return rc;
    if(rc==JOURNAL_IO) {
        /* An ambiguous PREPARE rename can leave an unacknowledged namespace
         * barrier. Repair it off path before rotating; true writeback loss
         * remains IO and is repaired by the fresh CURRENT generation below. */
        int retry=journal_retry(j);
        if(retry && retry!=JOURNAL_IO && retry!=JOURNAL_FULL) return retry;
    }
    rc=journal_rotate(j,checkpoint,count); if(rc) return rc;
    if(*save->previous_path) {
        os_err removed=os_unlink(save->previous_path);
        if(removed!=OS_OK && removed!=OS_E_NOENT) return JOURNAL_IO;
        rc=sync_parent(save->previous_path,&j->io); if(rc) return rc;
    }
    save->prepared=false; save->previous_path[0]=0;
    return 0;
}
