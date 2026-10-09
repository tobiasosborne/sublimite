#include "journal/journal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAGIC 0x314c4e4au
#define PAD 0u

typedef struct journal_batch {
    uint8_t *bytes;
    size_t used, sealed, progress;
    uint64_t offset, sequence;
    bool force, checksummed;
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
    uint64_t limit, log_budget, reserved, sync_bytes, sync_interval_ns;
    char *path, *name;
    int directory_fd;
    unsigned current, active_index;
    bool active, failed, directory_pending;
    int append_error;
    journal_io io;
    void (*message_handler)(const work_msg *, void *);
    void *message_ctx;
};
typedef struct journal_completion { journal_stats stats; } journal_completion;
/* A completion snapshot lives in the active arena batch after its data.
 * Only its address crosses the work mailbox; publish transfers ownership. */
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
static void encode(uint8_t *p, uint32_t type, uint64_t id, uint64_t seq, size_t n)
{
    put32(p,MAGIC); put32(p+4,(uint32_t)n); put32(p+8,type); put32(p+12,0);
    put64(p+16,seq); put64(p+24,id);
}
static void seal(journal_batch *b)
{
    b->sealed=sealed_size(b->used); size_t n=b->sealed-b->used;
    memset(b->bytes+b->used,0,n); encode(b->bytes+b->used,PAD,0,0,n);
}
static bool payload_valid(uint32_t type, const uint8_t *p, size_t n)
{
    switch(type) {
    case JOURNAL_BASE:
        if(n<41 || n>4137 || p[n-1]!=0 || memchr(p+40,0,n-41)!=NULL || u32(p+36)>4096 || u32(p+36)>u64(p)) return false;
        if(p[40]!=0 && (p[40]!='/' || u32(p+36)!=(u64(p)<4096?(uint32_t)u64(p):4096u))) return false;
        if(p[40]==0 && (u64(p)!=0 || u64(p+8)!=0 || u64(p+16)!=0 || u64(p+24)!=0 || u32(p+32)!=0 || u32(p+36)!=0)) return false;
        return true;
    case JOURNAL_SAVE: return n>=57 && (u32(p+8)==1 || u32(p+8)==2) && u32(p+12)==0 && payload_valid(JOURNAL_BASE,p+16,n-16);
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
    return (!(type==JOURNAL_TABS || type==JOURNAL_WINDOW) || u64(p+24)==0) &&
        seq!=UINT64_MAX && u64(p+16)==seq+1 && payload_valid(type,p+32,n-32);
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
static int write_at(const journal_io *io, int fd, const uint8_t *p, size_t n, uint64_t off, size_t *done)
{
    *done=0;
    while(*done<n) {
        ssize_t k=io->write?io->write(io->ctx,fd,p+*done,n-*done,off+*done):pwrite(fd,p+*done,n-*done,(off_t)(off+*done));
        if(k<0 && errno==EINTR) continue;
        if(k<=0) return JOURNAL_IO;
        if((size_t)k>n-*done) return JOURNAL_IO;
        *done+=(size_t)k;
    }
    return 0;
}
static int data_sync(int fd)
{ int rc; do { rc=fdatasync(fd); } while(rc<0 && errno==EINTR); return rc<0?JOURNAL_IO:0; }
static int io_sync(const journal_io *io, int fd, bool directory)
{
    int rc;
    do { rc=io && io->sync?io->sync(io->ctx,fd,directory):(directory?fsync(fd):fdatasync(fd)); }
    while(rc<0 && errno==EINTR);
    return rc<0?JOURNAL_IO:0;
}
static int check_base_io(const journal_base *base, const journal_io *io);
int journal_replay_file_with_io(const char *path, journal_visit visit, void *ctx, journal_replay_result *result, const journal_io *io)
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
            if(!rc) rc=check_base_io(&base,io);
            if(rc) break;
        }
        rc=visit_record(p,n,visit,ctx,result); if(rc) break;
    }
    if(!rc && result->corrupt) { if(ftruncate(fd,(off_t)result->valid_bytes)) rc=JOURNAL_IO; else rc=data_sync(fd); }
    edit_arena_free(&a); close(fd); return rc;
}
int journal_replay_file(const char *path, journal_visit visit, void *ctx, journal_replay_result *result)
{ return journal_replay_file_with_io(path,visit,ctx,result,NULL); }
static bool stat_equal(const struct stat *a, const struct stat *b)
{ return a->st_size==b->st_size && a->st_ino==b->st_ino && a->st_dev==b->st_dev && a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec && a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec; }
int journal_capture_base_with_io(const char *path, journal_base *base, const journal_io *io)
{
    if(!path || !base) return JOURNAL_INVALID;
    if(!*path) { *base=(journal_base){.path=path}; return 0; }
    char canonical[4097];
    if(strlen(path)>4096 || !realpath(path,canonical)) return JOURNAL_BASE_CHANGED;
    int fd=open(canonical,O_RDONLY|O_CLOEXEC); if(fd<0) return JOURNAL_BASE_CHANGED;
    struct stat before,after,atpath; uint8_t prefix[4096]; uint32_t table[256]; size_t got=0; int rc=0;
    if(fstat(fd,&before) || !S_ISREG(before.st_mode) || before.st_size<0 || before.st_mtim.tv_sec<0) rc=JOURNAL_BASE_CHANGED;
    if(!rc) {
        uint64_t base_size=(uint64_t)before.st_size;
        size_t wanted=base_size<sizeof prefix?(size_t)base_size:sizeof prefix;
        if(!io || !io->read) {
            if(read_at(fd,prefix,wanted,0,&got) || got!=wanted) rc=JOURNAL_BASE_CHANGED;
        } else while(got<wanted) {
            ssize_t k=io->read(io->ctx,fd,prefix+got,wanted-got,got);
            if(k<0 && errno==EINTR) continue;
            if(k<=0 || (size_t)k>wanted-got) { rc=JOURNAL_BASE_CHANGED; break; }
            got+=(size_t)k;
        }
    }
    if(!rc && (fstat(fd,&after) || stat(canonical,&atpath) || !stat_equal(&before,&after) || !stat_equal(&after,&atpath))) rc=JOURNAL_BASE_CHANGED;
    if(!rc) {
        crc_init(table); *base=(journal_base){.size=(uint64_t)before.st_size,
            .mtime_ns=(uint64_t)before.st_mtim.tv_sec*1000000000u+(uint64_t)before.st_mtim.tv_nsec,
            .inode=(uint64_t)before.st_ino,.device=(uint64_t)before.st_dev,
            .prefix_crc=crc_data(table,prefix,got),.prefix_len=(uint32_t)got};
        strcpy(base->captured_path,canonical); base->path=base->captured_path;
    }
    close(fd); return rc;
}
int journal_capture_base(const char *path, journal_base *base)
{ return journal_capture_base_with_io(path,base,NULL); }
static int check_base_io(const journal_base *base, const journal_io *io)
{
    if(!base || !base->path) return JOURNAL_INVALID;
    journal_base now; int rc=journal_capture_base_with_io(base->path,&now,io); if(rc) return rc;
    return base->size==now.size && base->mtime_ns==now.mtime_ns && base->inode==now.inode && base->device==now.device && base->prefix_len==now.prefix_len && base->prefix_crc==now.prefix_crc?0:JOURNAL_BASE_CHANGED;
}
int journal_check_base(const journal_base *base)
{ return check_base_io(base,NULL); }
int journal_decode_base(const journal_record *r, journal_base *base, char *path, size_t cap)
{
    if(!r || !base || !path || !r->data || r->type!=JOURNAL_BASE || !payload_valid(r->type,r->data,r->size) || cap<r->size-40) return JOURNAL_INVALID;
    memcpy(path,r->data+40,r->size-40); *base=(journal_base){.size=u64(r->data),.mtime_ns=u64(r->data+8),.inode=u64(r->data+16),.device=u64(r->data+24),.prefix_crc=u32(r->data+32),.prefix_len=u32(r->data+36),.path=path}; return 0;
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
static int sync_disk(journal *j, uint64_t sequence)
{
    journal_disk *d=&j->disk;
    int rc=io_sync(&j->io,d->fd,false); if(rc) return rc;
    uint64_t now=clock_ns(); journal_stats *s=&d->stats;
    uint64_t interval=now-s->last_sync_ns;
    if(d->unsynced && interval>s->max_sync_interval_ns) s->max_sync_interval_ns=interval;
    s->last_sync_ns=now; s->last_sync_bytes=d->unsynced;
    if(d->unsynced>s->max_sync_bytes) s->max_sync_bytes=d->unsynced;
    s->syncs++; s->durable_sequence=sequence; d->unsynced=0; return 0;
}
/* Only the worker owns a sealed batch. Complete every wire CRC before any
 * write; retry reuses those exact bytes. Poll between bounded CRC slices. */
static bool checksum_batch(journal *j, journal_batch *b, work_ctx *ctx)
{
    if(b->checksummed) return true;
    for(size_t pos=0;pos<b->sealed;) {
        uint8_t *p=b->bytes+pos; size_t n=u32(p+4); put32(p+12,0);
        uint32_t c=UINT32_MAX;
        for(size_t off=0;off<n;) {
            size_t end=off+JOURNAL_PAGE; if(end>n) end=n;
            for(;off<end;off++) c=j->crc_table[(c^p[off])&255u]^(c>>8);
            if(work_should_stop(ctx)) return false;
        }
        put32(p+12,~c); pos+=n;
    }
    b->checksummed=true; return true;
}
static void worker(work_ctx *ctx)
{
    journal *j=ctx->arg; journal_batch *b=&j->batches[j->active_index]; journal_disk *d=&j->disk;
    if(!checksum_batch(j,b,ctx)) return;
    size_t pos=b->progress; int rc=0; uint64_t initial=d->written_sequence;
    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence);
    while(!rc && pos<b->sealed && !work_should_stop(ctx)) {
        size_t n=b->sealed-pos; uint64_t room=j->sync_bytes-d->unsynced; if(n>room) n=(size_t)room;
        size_t done=0; rc=write_at(&j->io,d->fd,b->bytes+pos,n,b->offset+pos,&done);
        pos+=done; b->progress=pos; d->unsynced+=done; d->written_sequence=prefix_sequence(b,pos,initial);
        d->stats.written_sequence=d->written_sequence; d->stats.file_bytes=b->offset+pos;
        if(rc) break;
        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence); if(rc) break; }
    }
    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence);
    d->stats.error=rc;
    journal_completion *completion=(journal_completion *)(void *)(b->bytes+j->capacity);
    completion->stats=d->stats;
    work_msg msg={.kind=JOURNAL_MESSAGE}; uintptr_t address=(uintptr_t)completion; memcpy(msg.data,&address,sizeof address);
    while(!work_should_stop(ctx) && !work_publish(ctx,&msg)) delay();
    /* work keeps an undrained message slot reserved (P1.8b). Return immediately. */
}
static int sync_parent(const char *path, const journal_io *io)
{
    char directory[4097]; strcpy(directory,path);
    char *slash=strrchr(directory,'/');
    if(!slash) strcpy(directory,".");
    else if(slash==directory) slash[1]=0;
    else *slash=0;
    int fd=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return JOURNAL_IO;
    int rc=io_sync(io,fd,true);
    close(fd); return rc;
}
/* Resolve the existing parent without following the journal leaf. All live
 * namespace operations use the retained directory fd, even after chdir. */
static int journal_path(const char *path, char *canonical, size_t *name_offset, int *dfd)
{
    if(!*path || strlen(path)>4096) return JOURNAL_INVALID;
    char directory[4097], parent[4097]; strcpy(directory,path);
    char *slash=strrchr(directory,'/'); const char *name=slash?path+(slash-directory)+1:path;
    if(!*name || !strcmp(name,".") || !strcmp(name,"..")) return JOURNAL_INVALID;
    if(!slash) strcpy(directory,"."); else if(slash==directory) slash[1]=0; else *slash=0;
    if(!realpath(directory,parent)) return JOURNAL_IO;
    size_t n=strlen(parent), leaf=strlen(name); bool root=n==1 && parent[0]=='/';
    if(n+(!root?1u:0u)+leaf>4096) return JOURNAL_INVALID;
    strcpy(canonical,parent); if(!root) canonical[n++]='/';
    strcpy(canonical+n,name); *name_offset=n;
    *dfd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC); return *dfd<0?JOURNAL_IO:0;
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
                           int fd, int dfd, work_pool *pool,
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
    j->directory_fd=dfd; crc_init(j->crc_table);
    for(unsigned i=0;i<2;i++) { j->batches[i].bytes=edit_arena_alloc(&j->arena,cap+sizeof(journal_completion),4096); memset(j->batches[i].bytes,0,cap+sizeof(journal_completion)); }
    int rc=io_sync(&j->io,dfd,true); if(rc) { edit_arena_free(&a); return rc; }
    j->disk.fd=fd; j->disk.stats.last_sync_ns=clock_ns();
    j->disk.stats.file_limit_bytes=limit; j->disk.stats.queue_bytes=2*cap;
    j->stats=j->disk.stats; *out=j; return 0;
}
int journal_open_with_io(journal **out, const char *path, work_pool *pool, const journal_options *options, const journal_io *io)
{
    if(!out || !path || !pool) return JOURNAL_INVALID;
    *out=NULL; if(!options_valid(options)) return JOURNAL_INVALID;
    char canonical[4097]; size_t name_offset=0; int dfd=-1;
    int rc=journal_path(path,canonical,&name_offset,&dfd); if(rc) return rc;
    int fd=openat(dfd,canonical+name_offset,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600); struct stat sb;
    if(fd<0 || fstat(fd,&sb) || !S_ISREG(sb.st_mode) || sb.st_size!=0) rc=fd<0?JOURNAL_IO:JOURNAL_INVALID;
    else rc=journal_create(out,canonical,name_offset,fd,dfd,pool,options,io);
    if(rc) { if(fd>=0) close(fd); close(dfd); } return rc;
}
int journal_open(journal **out, const char *path, work_pool *pool, const journal_options *options)
{ return journal_open_with_io(out,path,pool,options,NULL); }
int journal_set_io(journal *j, const journal_io *io)
{ if(!j) return JOURNAL_INVALID; if(j->active) return JOURNAL_BUSY; j->io=io?*io:(journal_io){0}; return 0; }
int journal_retry(journal *j)
{
    if(!j) return JOURNAL_INVALID;
    if(j->active) return JOURNAL_BUSY;
    if(j->stats.error!=JOURNAL_IO) return JOURNAL_INVALID;
    if(j->directory_pending) {
        int rc=io_sync(&j->io,j->directory_fd,true); if(rc) return rc;
        j->directory_pending=false; j->stats.durable_sequence=j->disk.stats.durable_sequence;
    }
    j->stats.error=j->append_error; j->disk.stats.error=0;
    return 0;
}
static int reserve_record(journal *j, size_t payload, uint8_t **p)
{
    if(!j || payload>JOURNAL_MAX_RECORD-32) return JOURNAL_INVALID;
    if(j->stats.error) return j->stats.error;
    journal_batch *b=&j->batches[j->current]; size_t n=payload+32;
    if(j->stats.accepted_sequence==UINT64_MAX || n>j->capacity-32 || b->used>j->capacity-32-n || j->reserved>j->limit || sealed_size(b->used+n)>j->limit-j->reserved) { j->stats.error=JOURNAL_FULL; j->append_error=JOURNAL_FULL; return JOURNAL_FULL; }
    *p=b->bytes+b->used; return 0;
}
static void finish_record(journal *j, uint8_t *p, uint32_t type, uint64_t id, size_t payload)
{
    journal_batch *b=&j->batches[j->current]; j->stats.accepted_sequence++;
    encode(p,type,id,j->stats.accepted_sequence,payload+32); b->used+=payload+32; b->sequence=j->stats.accepted_sequence;
}
int journal_append(journal *j, uint32_t type, uint64_t id, const uint8_t *data, size_t size)
{
    if((!data && size) || ((type==JOURNAL_TABS || type==JOURNAL_WINDOW) && id) || !payload_valid(type,data,size)) return JOURNAL_INVALID;
    uint8_t *p; int rc=reserve_record(j,size,&p); if(rc) return rc;
    memcpy(p+32,data,size); finish_record(j,p,type,id,size); return 0;
}
int journal_insert(journal *j, uint64_t id, uint64_t off, const uint8_t *bytes, size_t size)
{
    if(!j || (!bytes && size) || off>UINT64_MAX-size) return JOURNAL_INVALID;
    if(j->stats.error) return j->stats.error;
    size_t chunk=j->capacity-72; if(chunk>JOURNAL_MAX_RECORD-40) chunk=JOURNAL_MAX_RECORD-40;
    size_t records=size?1+(size-1)/chunk:1;
    if(records>(SIZE_MAX-size)/40) return JOURNAL_INVALID;
    size_t total=size+records*40; journal_batch *b=&j->batches[j->current];
    if(records>UINT64_MAX-j->stats.accepted_sequence || total>j->capacity-32 ||
       b->used>j->capacity-32-total || j->reserved>j->limit || sealed_size(b->used+total)>j->limit-j->reserved) {
        j->stats.error=JOURNAL_FULL; j->append_error=JOURNAL_FULL; return JOURNAL_FULL;
    }
    size_t copied=0;
    do {
        size_t n=size-copied; if(n>chunk) n=chunk;
        uint8_t *p=b->bytes+b->used; put64(p+32,off+copied);
        if(n) memcpy(p+40,bytes+copied,n);
        finish_record(j,p,JOURNAL_INSERT,id,n+8); copied+=n;
    } while(copied<size);
    return 0;
}
int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len)
{ uint8_t data[16]; if(off>UINT64_MAX-len) return JOURNAL_INVALID; put64(data,off); put64(data+8,len); return journal_append(j,JOURNAL_DELETE,id,data,16); }
static int base_data(const journal_base *base, uint8_t *data, size_t *size)
{
    if(!base || !base->path || strlen(base->path)>4096 || base->prefix_len>4096 || base->prefix_len>base->size) return JOURNAL_INVALID;
    put64(data,base->size); put64(data+8,base->mtime_ns); put64(data+16,base->inode); put64(data+24,base->device);
    put32(data+32,base->prefix_crc); put32(data+36,base->prefix_len);
    size_t n=strlen(base->path)+1; memcpy(data+40,base->path,n); *size=40+n;
    return payload_valid(JOURNAL_BASE,data,*size)?0:JOURNAL_INVALID;
}
int journal_set_base(journal *j, uint64_t id, const journal_base *base)
{
    uint8_t data[4137]; size_t n=0; int rc=base_data(base,data,&n);
    return rc?rc:journal_append(j,JOURNAL_BASE,id,data,n);
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
    unsigned index=j->failed?j->active_index:j->current;
    journal_batch *b=&j->batches[index];
    uint64_t elapsed=now>=j->stats.last_sync_ns?now-j->stats.last_sync_ns:0;
    if(!j->failed) {
        if(!force) {
            if(!b->used && (j->stats.written_sequence<=j->stats.durable_sequence || elapsed<j->sync_interval_ns)) return 0;
            if(b->used && b->used<JOURNAL_PAGE-JOURNAL_HEADER && elapsed<j->sync_interval_ns) return 0;
        }
        b->sealed=0; b->progress=0; b->offset=j->reserved;
        if(b->used) seal(b);
        b->checksummed=false;
    }
    b->force=force || (j->failed && b->force);

    j->active_index=index;
    j->handle=work_submit(j->pool,(work_job){worker,j,0,WORK_BULK}); if(!j->handle.epoch) return JOURNAL_BUSY;
    j->active=true;
    if(!j->failed) { j->reserved+=b->sealed; j->current^=1u; }
    return 0;
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
    j->failed=c->stats.error!=0;
    if(!j->failed) { b->used=0; b->sealed=0; b->progress=0; b->checksummed=false; }
    j->active=false;
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
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return j->stats.error;
        work_mailbox_drain(j->pool,drain,j);
        if(!j->active && !j->batches[j->current].used && j->stats.durable_sequence==j->stats.accepted_sequence) return j->stats.error;
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
    close(j->disk.fd); close(j->directory_fd); edit_arena a=j->arena; edit_arena_free(&a);
}
/* Exact transport size, with the same batch packing/PAD strategy as build.
 * Validate the entire checkpoint before creating anything or growing a limit. */
static int checkpoint_size(const journal *j, const journal_record *records, size_t count, uint64_t *bytes)
{
    size_t used=0; uint64_t total=0;
    for(size_t i=0;i<count;i++) {
        const journal_record *r=&records[i];
        if(!r->data || r->size>JOURNAL_MAX_RECORD-32 || r->size+32>j->capacity-32 ||
           ((r->type==JOURNAL_TABS || r->type==JOURNAL_WINDOW) && r->buffer_id) ||
           !payload_valid(r->type,r->data,r->size)) return JOURNAL_INVALID;
        if(used>j->capacity-64-r->size) { total+=sealed_size(used); used=0; }
        used+=r->size+32;
        if(total>INT64_MAX-j->capacity) return JOURNAL_FULL;
    }
    if(used) total+=sealed_size(used);
    if(total>(uint64_t)INT64_MAX-j->log_budget) return JOURNAL_FULL;
    *bytes=total; return 0;
}
static int checkpoint_temp(const journal *j, char *name, size_t cap)
{
    uint64_t nonce=clock_ns();
    for(unsigned i=0;i<64;i++) {
        int n=snprintf(name,cap,".edit-journal-%lx-%llx-%x",(unsigned long)getpid(),(unsigned long long)nonce,i);
        if(n<0 || (size_t)n>=cap) return -1;
        int fd=openat(j->directory_fd,name,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd>=0 || errno!=EEXIST) return fd;
    }
    return -1;
}
int journal_rotate(journal *j, const journal_record *records, size_t count)
{
    if(!j || (!records && count)) return JOURNAL_INVALID;
    if(j->active || (!j->failed && (j->batches[j->current].used || j->stats.accepted_sequence!=j->stats.durable_sequence))) return JOURNAL_BUSY;
    uint64_t checkpoint_bytes=0; int rc=checkpoint_size(j,records,count,&checkpoint_bytes); if(rc) return rc;
    char tmpname[80]; int tfd=checkpoint_temp(j,tmpname,sizeof tmpname); if(tfd<0) return JOURNAL_IO;
    int dfd=fcntl(j->directory_fd,F_DUPFD_CLOEXEC,0);
    if(dfd<0) { close(tfd); unlinkat(j->directory_fd,tmpname,0); return JOURNAL_IO; }
    journal *next=NULL; journal_options opt={.batch_bytes=j->capacity,.max_file_bytes=checkpoint_bytes+j->log_budget,
        .sync_bytes=j->sync_bytes,.sync_interval_ns=j->sync_interval_ns};
    rc=journal_create(&next,j->path,(size_t)(j->name-j->path),tfd,dfd,j->pool,&opt,&j->io);
    if(rc) { close(tfd); close(dfd); }
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
        char tmppath[4177]; size_t parent=(size_t)(j->name-j->path);
        memcpy(tmppath,j->path,parent); strcpy(tmppath+parent,tmpname);
        if(!(j->io.rename?j->io.rename(j->io.ctx,tmppath,j->path):renameat(j->directory_fd,tmpname,j->directory_fd,j->name))) renamed=true;
        else {
            /* An ambiguous error after replacement must never permit writes
             * to the now-unlinked old inode. Single-instance owns this name. */
            struct stat replacement, named;
            renamed=fstat(next->disk.fd,&replacement)==0 && fstatat(j->directory_fd,j->name,&named,AT_SYMLINK_NOFOLLOW)==0 &&
                replacement.st_dev==named.st_dev && replacement.st_ino==named.st_ino;
            rc=JOURNAL_IO;
        }
    }
    if(renamed) {
        int oldfd=j->disk.fd;
        j->disk=next->disk; next->disk.fd=-1; j->stats=next->stats; j->reserved=next->reserved;
        for(unsigned i=0;i<2;i++) { j->batches[i].used=0; j->batches[i].sealed=0; }
        j->current=0; j->failed=false; j->append_error=0;
        j->limit=next->limit; j->disk.stats.checkpoint_bytes=checkpoint_bytes; j->stats.checkpoint_bytes=checkpoint_bytes;
        j->directory_pending=true;
        if(rc || io_sync(&j->io,j->directory_fd,true)) {
            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0;
        } else j->directory_pending=false;
        close(oldfd);
    }
    if(next) journal_close(next);
    unlinkat(j->directory_fd,tmpname,0); return rc;
}


/* Retained generation is a hard link: file_save_* replaces the target inode
 * atomically, so the previous contents stay immutable under this pathname. */
int journal_save_prepare(journal *j, uint64_t id, const journal_base *previous,
                         const journal_record *checkpoint, size_t count, journal_save *save)
{
    if(!j || !previous || !previous->path || !checkpoint || !count || !save || save->prepared ||
       count>j->limit/JOURNAL_HEADER || count>SIZE_MAX/sizeof(journal_record)-1 ||
       (*previous->path && previous->path[0]!='/') || strlen(previous->path)>4096) return JOURNAL_INVALID;
    uint8_t original[4137]; size_t original_size=0;
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
        size_t n=(size_t)(strrchr(previous->path,'/')-previous->path)+1;
        const char name[]=".edit-base-XXXXXX";
        if(n>sizeof save->previous_path-sizeof name) return JOURNAL_INVALID;
        memcpy(save->previous_path,previous->path,n);
        memcpy(save->previous_path+n,name,sizeof name);
        int fd=mkstemp(save->previous_path); if(fd<0) return JOURNAL_IO; close(fd);
        if(unlink(save->previous_path) || linkat(AT_FDCWD,previous->path,AT_FDCWD,save->previous_path,AT_SYMLINK_FOLLOW)) return JOURNAL_IO;
        retained.path=save->previous_path;
        rc=journal_check_base(&retained); if(rc) return rc;
        fd=open(save->previous_path,O_RDONLY|O_CLOEXEC); if(fd<0) return JOURNAL_IO;
        rc=io_sync(&j->io,fd,false); close(fd);
        if(!rc) rc=sync_parent(save->previous_path,&j->io);
        if(rc) return rc;
    }
    uint8_t retained_data[4137], marker[4153]={0}; size_t retained_size=0;
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
    uint8_t marker[4153]={0}; size_t n=0; int rc=base_data(saved,marker+16,&n); if(rc) return rc;
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
        if(unlink(save->previous_path) && errno!=ENOENT) return JOURNAL_IO;
        rc=sync_parent(save->previous_path,&j->io); if(rc) return rc;
    }
    save->prepared=false; return 0;
}
