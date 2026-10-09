#include "journal/journal.h"
#include <fcntl.h>
#include <errno.h>
#include "file/file.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "journal_test:%d FAIL %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct model { uint8_t text[32768]; size_t len; uint64_t records; uint32_t views, tabs, windows; } model;
static uint64_t get64(const uint8_t *p) { uint64_t x = 0; for (unsigned i=0;i<8;i++) x |= (uint64_t)p[i] << (8*i); return x; }
static int apply(void *ctx, const journal_record *r)
{
    model *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        uint64_t off = get64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > sizeof m->text - m->len) return 1;
        memmove(m->text + (size_t)off + n, m->text + (size_t)off, m->len - (size_t)off);
        memcpy(m->text + (size_t)off, r->data + 8, n); m->len += n;
    } else if (r->type == JOURNAL_DELETE) {
        uint64_t off = get64(r->data), n = get64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        memmove(m->text + (size_t)off, m->text + (size_t)(off+n), m->len - (size_t)(off+n)); m->len -= (size_t)n;
    } else if (r->type == JOURNAL_VIEW) m->views++;
    else if (r->type == JOURNAL_TABS) m->tabs++;
    else if (r->type == JOURNAL_WINDOW) m->windows++;
    m->records++; return 0;
}
static int reject(void *ctx, const journal_record *r) { (void)ctx; (void)r; return 1; }
static void pause_ms(void) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static void other_message(work_ctx *c)
{ work_msg msg={.kind=42}; (void)work_publish(c,&msg); while(!work_should_stop(c)) pause_ms(); }
static void other_route(const work_msg *m, void *ctx)
{ if(m->kind==42) (*(unsigned *)ctx)++; }
static int apply_tree(void *ctx, const journal_record *r)
{ return r->type==JOURNAL_BASE?0:journal_apply_piece(ctx,r); }
static void blocked(work_ctx *c) { while (!work_should_stop(c)) pause_ms(); }
static void route(const work_msg *m, void *ctx) { (void)journal_receive(ctx, m); }
static int temp(char *p) { int fd = mkstemp(p); if (fd >= 0) close(fd); return fd; }

/* Volatile POSIX files and a separate simulated power-loss image. Only data
 * sync copies bytes, and only directory sync commits the journal's name. */
#define IMAGE_SIZE 300000u
typedef struct disk_image { ino_t inode; size_t size; uint8_t bytes[IMAGE_SIZE]; } disk_image;
typedef struct fault_disk {
    const char *path;
    disk_image image[8];
    size_t images;
    ino_t durable_name;
    unsigned writes, syncs, dirs, renames;
    unsigned fail_write, fail_sync, fail_dir;
    int fail_rename; /* 1 before, 2 after replacement */
    bool short_write;
    uint64_t retry_offset;
} fault_disk;
static ssize_t fault_write(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    fault_disk *d=ctx; d->writes++;
    if(d->writes==d->fail_write) { errno=EIO; return -1; }
    if(d->short_write && d->writes==1 && n>17) n=17;
    if(!d->retry_offset && d->writes>d->fail_write && d->fail_write) d->retry_offset=off;
    return pwrite(fd,p,n,(off_t)off);
}
static int fault_sync(void *ctx, int fd, bool directory)
{
    fault_disk *d=ctx;
    if(directory) {
        d->dirs++;
        if(d->dirs==d->fail_dir) { errno=EIO; return -1; }
        if(fsync(fd)) return -1;
        struct stat sb;
        if(!stat(d->path,&sb)) d->durable_name=sb.st_ino;
        return 0;
    }
    d->syncs++;
    if(d->syncs==d->fail_sync) { errno=EIO; return -1; }
    if(fdatasync(fd)) return -1;
    struct stat sb; if(fstat(fd,&sb) || sb.st_size<0 || (uint64_t)sb.st_size>IMAGE_SIZE) return -1;
    size_t index=0;
    while(index<d->images && d->image[index].inode!=sb.st_ino) index++;
    if(index==d->images) { if(index==8) return -1; d->images++; }
    disk_image *im=&d->image[index]; im->inode=sb.st_ino; im->size=(size_t)sb.st_size;
    return pread(fd,im->bytes,im->size,0)==(ssize_t)im->size?0:-1;
}
static int fault_rename(void *ctx, const char *from, const char *to)
{
    fault_disk *d=ctx; d->renames++;
    if(d->fail_rename==1) { errno=EIO; return -1; }
    if(rename(from,to)) return -1;
    if(d->fail_rename==2) { errno=EIO; return -1; }
    return 0;
}
static journal_io fault_io(fault_disk *d)
{ return (journal_io){.ctx=d,.write=fault_write,.sync=fault_sync,.rename=fault_rename}; }
static int durable_replay(fault_disk *d, model *m, journal_replay_result *rr)
{
    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name)
        return journal_replay_bytes(d->image[i].bytes,d->image[i].size,apply,m,rr);
    return journal_replay_bytes(NULL,0,apply,m,rr);
}
static int retry_test(void)
{
    char path[]="/tmp/journal-retry-XXXXXX"; CHECK(temp(path)>=0);
    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path; d->short_write=true; d->fail_write=2;
    journal_io io=fault_io(d); work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
    uint8_t *big=malloc(200000); CHECK(big); memset(big,'q',200000);
    CHECK(journal_insert(j,1,0,big,200000)==0);
    CHECK(journal_pump(j,1,true)==0);
    CHECK(journal_insert(j,1,200000,(const uint8_t *)"!",1)==0); /* second batch */
    CHECK(journal_flush(j)==JOURNAL_IO);
    journal_stats st=journal_get_stats(j);
    CHECK(st.accepted_sequence==2 && st.written_sequence==0 && st.durable_sequence==0);
    CHECK(journal_retry(j)==JOURNAL_OK);
    CHECK(journal_flush(j)==0);
    CHECK(d->retry_offset==17); /* resume the short write, never discard it */
    st=journal_get_stats(j); CHECK(st.accepted_sequence==2 && st.durable_sequence==2);
    journal_close(j);
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a); CHECK(tree);
    journal_replay_result rr; CHECK(journal_replay_file(path,apply_tree,tree,&rr)==0);
    uint8_t last; CHECK(piece_len(tree)==200001 && piece_read(tree,200000,&last,1)==0 && last=='!');
    piece_destroy(tree);
    /* A complete checkpoint can also resolve IO with undrained accepted bytes.
     * An unsuccessful replacement must retain the old acknowledged prefix. */
    int fd=open(path,O_WRONLY|O_TRUNC); CHECK(fd>=0); close(fd);
    memset(d,0,sizeof *d); d->path=path; io=fault_io(d);
    CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"a",1)==0 && journal_flush(j)==0);
    d->fail_write=d->writes+2;
    CHECK(journal_insert(j,1,1,big,200000)==0 && journal_pump(j,1,true)==0);
    CHECK(journal_insert(j,1,200001,(const uint8_t *)"!",1)==0 && journal_flush(j)==JOURNAL_IO);
    CHECK(journal_get_stats(j).accepted_sequence==3 && journal_get_stats(j).durable_sequence==1);
    journal_record invalid={99,1,0,NULL,0};
    CHECK(journal_rotate(j,&invalid,1)==JOURNAL_INVALID);
    model old={0}; CHECK(durable_replay(d,&old,&rr)==0 && rr.last_sequence==1 && old.len==1 && old.text[0]=='a');
    uint8_t first[9]={0}, last_op[9]={0}; first[8]='a'; last_op[8]='!';
    uint8_t *whole=calloc(1,200008); CHECK(whole); whole[0]=1; memcpy(whole+8,big,200000);
    uint64_t last_off=200001;
    for(unsigned k=0;k<8;k++) last_op[k]=(uint8_t)(last_off>>(k*8));
    journal_record complete[]={{JOURNAL_INSERT,1,0,first,9},{JOURNAL_INSERT,1,0,whole,200008},{JOURNAL_INSERT,1,0,last_op,9}};
    d->fail_write=0; CHECK(journal_rotate(j,complete,3)==0);
    CHECK(journal_get_stats(j).durable_sequence==3 && journal_get_stats(j).error==0); journal_close(j);
    tree=piece_create(&a); CHECK(tree && journal_replay_file(path,apply_tree,tree,&rr)==0);
    CHECK(piece_len(tree)==200002 && piece_read(tree,200001,&last,1)==0 && last=='!');
    CHECK(piece_read(tree,0,&last,1)==0 && last=='a'); piece_destroy(tree);
    fd=open(path,O_WRONLY|O_TRUNC); CHECK(fd>=0); close(fd);
    memset(d,0,sizeof *d); d->path=path; d->fail_sync=1; io=fault_io(d);
    CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"f",1)==0 && journal_flush(j)==JOURNAL_IO);
    CHECK(journal_retry(j)==0);
    CHECK(journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    unsigned wait=0;
    while(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) && wait++<2000) pause_ms();
    CHECK(wait<2000); work_mailbox_drain(&pool,route,j);
    CHECK(journal_get_stats(j).durable_sequence==1); /* retry preserves the force request */
    journal_close(j);
    free(whole); free(big); free(d); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: retry ok (short write + EIO, two batches, exact offset, checkpoint replacement)"); return 0;
}
static int barriers_test(void)
{
    char path[]="/tmp/journal-barrier-XXXXXX"; CHECK(temp(path)>=0);
    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path; d->fail_dir=1;
    journal_io io=fault_io(d); work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j=NULL; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==JOURNAL_IO && !j);
    d->fail_dir=0; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
    /* First complete record precedes a large record crossing 64 KiB. Lose
     * volatile bytes after the first data sync: exact surviving prefix is 1. */
    CHECK(journal_insert(j,1,0,(const uint8_t *)"a",1)==0);
    uint8_t big[70000]; memset(big,'b',sizeof big);
    CHECK(journal_insert(j,1,1,big,sizeof big)==0); d->fail_write=2;
    CHECK(journal_flush(j)==JOURNAL_IO);
    CHECK(journal_get_stats(j).durable_sequence==1);
    model m={0}; journal_replay_result rr; CHECK(durable_replay(d,&m,&rr)==0);
    CHECK(rr.corrupt && rr.last_sequence==1 && m.len==1 && m.text[0]=='a');
    CHECK(journal_retry(j)==0); CHECK(journal_flush(j)==0);
    /* Failed fdatasync must not advance acknowledgement; retry only syncs. */
    CHECK(journal_insert(j,1,70001,(const uint8_t *)"!",1)==0);
    d->fail_sync=d->syncs+1; CHECK(journal_flush(j)==JOURNAL_IO);
    CHECK(journal_get_stats(j).durable_sequence==2); unsigned writes=d->writes;
    CHECK(journal_retry(j)==0 && journal_flush(j)==0 && d->writes==writes);
    uint8_t op[11]={0}; memcpy(op+8,"new",3);
    journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    for(unsigned phase=0;phase<3;phase++) {
        d->fail_sync=0; d->fail_rename=phase<2?(int)phase+1:0;
        d->fail_dir=phase==2?d->dirs+2:0; /* next creation then post-rename barrier */
        CHECK(journal_rotate(j,&cp,1)==JOURNAL_IO);
        /* Durable name still selects the entire preceding checkpoint. */
        disk_image *old=NULL;
        for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) old=&d->image[i];
        CHECK(old); CHECK(journal_replay_bytes(old->bytes,old->size,NULL,NULL,&rr)==0 && rr.last_sequence==3);
        if(phase>=1) {
            m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==3 && !memcmp(m.text,"new",3));
            CHECK(journal_get_stats(j).error==JOURNAL_IO && journal_get_stats(j).durable_sequence==0);
            CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==JOURNAL_IO);
        }
        if(phase==2) {
            CHECK(journal_get_stats(j).durable_sequence==0);
            d->fail_dir=d->dirs+1;
            CHECK(journal_retry(j)==JOURNAL_IO); /* retry must redo the name barrier */
        }
        if(phase>=1) {
            d->fail_dir=0; CHECK(journal_retry(j)==0);
            CHECK(journal_get_stats(j).durable_sequence==1);
            CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==0 && journal_flush(j)==0);
            m=(model){0}; CHECK(durable_replay(d,&m,&rr)==0 && m.len==4 && !memcmp(m.text,"new!",4));
        }
        journal_close(j);
        /* Each phase gets a fresh inode and equivalent old checkpoint. */
        int fd=open(path,O_WRONLY|O_TRUNC); CHECK(fd>=0); close(fd);
        memset(d,0,sizeof *d); d->path=path; io=fault_io(d);
        CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
        CHECK(journal_insert(j,1,0,(const uint8_t *)"a",1)==0);
        CHECK(journal_insert(j,1,1,big,sizeof big)==0);
        CHECK(journal_insert(j,1,70001,(const uint8_t *)"!",1)==0); CHECK(journal_flush(j)==0);
    }
    CHECK(journal_rotate(j,&cp,1)==0); m=(model){0}; CHECK(durable_replay(d,&m,&rr)==0 && m.len==3 && !memcmp(m.text,"new",3));
    journal_close(j); work_pool_shutdown(&pool); free(d); unlink(path);
    puts("journal_test: barriers ok (durable image, creation, 64KiB straddle, sync/rename/dir failures)"); return 0;
}
static void base_payload(uint8_t *p, const journal_base *b)
{
    uint64_t values[]={b->size,b->mtime_ns,b->inode,b->device};
    for(unsigned i=0;i<4;i++) for(unsigned k=0;k<8;k++) p[i*8+k]=(uint8_t)(values[i]>>(k*8));
    for(unsigned k=0;k<4;k++) { p[32+k]=(uint8_t)(b->prefix_crc>>(k*8)); p[36+k]=(uint8_t)(b->prefix_len>>(k*8)); }
    strcpy((char *)p+40,b->path);
}
typedef struct save_restore { piece_tree *trees[2]; } save_restore;
static int load_save(void *ctx, const journal_record *r)
{
    save_restore *c=ctx; if(r->buffer_id<1 || r->buffer_id>2) return 1;
    piece_tree *t=c->trees[r->buffer_id-1];
    if(r->type==JOURNAL_BASE) {
        journal_base b; char path[4097]; if(journal_decode_base(r,&b,path,sizeof path) || journal_check_base(&b)) return 1;
        if(!*path) return 0;
        uint8_t bytes[32]; int fd=open(path,O_RDONLY); if(fd<0 || b.size>sizeof bytes) return 1;
        ssize_t n=read(fd,bytes,(size_t)b.size); close(fd);
        return n==(ssize_t)b.size?piece_init_copy(t,bytes,(size_t)b.size):1;
    }
    return r->type==JOURNAL_INSERT || r->type==JOURNAL_DELETE?journal_apply_piece(t,r):0;
}
static int verify_two_buffers(const char *path, const char *expected, const char *other)
{
    piece_allocator a=piece_default_allocator(); save_restore c={{piece_create(&a),piece_create(&a)}};
    CHECK(c.trees[0] && c.trees[1]); journal_replay_result rr; CHECK(journal_replay_file(path,load_save,&c,&rr)==0);
    uint8_t bytes[32]; size_t n=strlen(expected);
    CHECK(piece_len(c.trees[0])==n && piece_read(c.trees[0],0,bytes,n)==0 && !memcmp(bytes,expected,n));
    n=strlen(other);
    CHECK(piece_len(c.trees[1])==n && piece_read(c.trees[1],0,bytes,n)==0 && !memcmp(bytes,other,n));
    piece_destroy(c.trees[0]); piece_destroy(c.trees[1]); return 0;
}
static int verify_save(const char *path, const char *expected)
{ return verify_two_buffers(path,expected,"YZ"); }
typedef struct save_marker_check { const journal_base *base; uint64_t cutoff, phase; bool seen; } save_marker_check;
static int check_save_marker(void *ctx, const journal_record *r)
{
    save_marker_check *c=ctx;
    if(r->type!=JOURNAL_SAVE) return 0;
    if(c->seen || r->buffer_id!=1 || get64(r->data)!=c->cutoff || get64(r->data+8)!=c->phase) return 1;
    journal_record metadata=*r; metadata.type=JOURNAL_BASE; metadata.data+=16; metadata.size-=16;
    journal_base b; char path[4097];
    if(journal_decode_base(&metadata,&b,path,sizeof path)) return 1;
    const journal_base *expected=c->base;
    if(strcmp(b.path,expected->path) || b.size!=expected->size || b.mtime_ns!=expected->mtime_ns ||
       b.inode!=expected->inode || b.device!=expected->device || b.prefix_len!=expected->prefix_len ||
       b.prefix_crc!=expected->prefix_crc) return 1;
    c->seen=true; return 0;
}
static int verify_save_marker(const char *path, const journal_base *b, uint64_t cutoff, uint64_t phase)
{
    save_marker_check c={.base=b,.cutoff=cutoff,.phase=phase}; journal_replay_result rr;
    CHECK(journal_replay_file(path,check_save_marker,&c,&rr)==0 && c.seen && !rr.corrupt); return 0;
}
typedef struct save_message { unsigned done; int status; } save_message;
typedef struct save_pause { _Atomic bool entered, release; } save_pause;
static void pause_save(void *ctx, int step)
{
    save_pause *s=ctx;
    if(step!=FILE_STEP_FSYNCED) return;
    atomic_store_explicit(&s->entered,true,memory_order_release);
    while(!atomic_load_explicit(&s->release,memory_order_acquire)) pause_ms();
}
static void save_route(const work_msg *m, void *ctx)
{
    save_message *s=ctx; file_msg fm;
    if(!file_msg_decode(m,&fm) && fm.kind==FILE_MSG_SAVE_DONE) { s->done++; s->status=fm.status; }
}
static int save_test(void)
{
    char path[]="/tmp/journal-save-XXXXXX", target[]="/tmp/journal-target-XXXXXX";
    CHECK(temp(path)>=0 && temp(target)>=0);
    int fd=open(target,O_WRONLY); CHECK(fd>=0 && write(fd,"abcdef",6)==6); close(fd);
    journal_base b; CHECK(journal_capture_base(target,&b)==0);
    uint8_t bp[4137], empty[41]={0}, del[16]={0}, other[9]={0}, later[9]={0};
    base_payload(bp,&b); del[8]=3; other[8]='Y'; later[8]='X';
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,41+strlen(target)}, {JOURNAL_DELETE,1,0,del,16},
                         {JOURNAL_BASE,2,0,empty,41}, {JOURNAL_INSERT,2,0,other,9}};
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    CHECK(journal_set_base(j,1,&b)==0 && journal_delete(j,1,0,3)==0);
    CHECK(journal_set_base(j,2,&(journal_base){.path=""})==0 && journal_insert(j,2,0,(const uint8_t *)"Y",1)==0);
    journal_save save={0}; CHECK(journal_save_prepare(j,1,&b,cp,4,&save)==JOURNAL_OK);
    CHECK(save.prepared && save.sequence==4 && access(save.previous_path,F_OK)==0);
    CHECK(verify_save_marker(path,&b,4,1)==0);
    /* Crash after prepare, before replacement: generation is already valid. */
    piece_allocator before_allocator=piece_default_allocator();
    save_restore before={{piece_create(&before_allocator),piece_create(&before_allocator)}};
    journal_replay_result before_rr; CHECK(journal_replay_file(path,load_save,&before,&before_rr)==0);
    uint8_t before_text[3];
    CHECK(piece_len(before.trees[0])==3 && piece_read(before.trees[0],0,before_text,3)==0 && !memcmp(before_text,"def",3));
    CHECK(piece_len(before.trees[1])==1 && piece_read(before.trees[1],0,before_text,1)==0 && before_text[0]=='Y');
    piece_destroy(before.trees[0]); piece_destroy(before.trees[1]);
    save_message completion={0}; journal_set_message_handler(j,save_route,&completion);
    /* Compose with the real asynchronous file save entry point. */
    file *f; CHECK(file_open_begin(&pool,target,NULL,&f)==0 && file_open_ready(f));
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t && file_attach(f,t)==0);
    save_pause paused; atomic_init(&paused.entered,false); atomic_init(&paused.release,false);
    file_set_step_hook(f,pause_save,&paused);
    CHECK(piece_delete(t,0,3,NULL)==0); CHECK(file_save_begin(f,t,0,1)==0);
    unsigned wait=0;
    while(!atomic_load_explicit(&paused.entered,memory_order_acquire) && wait++<2000) pause_ms();
    CHECK(wait<2000 && file_save_busy(f));
    CHECK(piece_insert(t,0,(const uint8_t *)"X",1)==0);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"X",1)==0);
    CHECK(journal_insert(j,2,1,(const uint8_t *)"Z",1)==0);
    atomic_store_explicit(&paused.release,true,memory_order_release);
    CHECK(journal_flush(j)==0);
    CHECK(completion.done==1 && completion.status==FILE_OK);
    CHECK(journal_get_stats(j).durable_sequence==7);
    CHECK(verify_save(path,"Xdef")==0); /* crash after save, before checkpoint */
    CHECK(journal_capture_base(target,&b)==0 && b.size==3); base_payload(bp,&b);
    uint8_t other_now[10]={0}; memcpy(other_now+8,"YZ",2);
    journal_record next[]={{JOURNAL_BASE,1,0,bp,41+strlen(target)}, {JOURNAL_INSERT,1,0,later,9},
                           {JOURNAL_BASE,2,0,empty,41}, {JOURNAL_INSERT,2,0,other_now,10}};
    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path;
    journal_io io=fault_io(d); CHECK(journal_set_io(j,&io)==0);
    /* Seed the durable old-generation image at installation of the seam. */
    fd=open(path,O_RDWR); CHECK(fd>=0 && fault_sync(d,fd,false)==0); close(fd);
    struct stat saved_stat; CHECK(stat(path,&saved_stat)==0); d->durable_name=saved_stat.st_ino;
    /* Metadata must fit through the checkpoint path even if appends are FULL. */
    uint8_t *overflow=calloc(1,JOURNAL_DEFAULT_BATCH_BYTES); CHECK(overflow);
    CHECK(journal_insert(j,1,0,overflow,JOURNAL_DEFAULT_BATCH_BYTES)==JOURNAL_FULL); free(overflow);
    /* Failed marker data sync preserves the previous base and all edits. */
    d->fail_sync=d->syncs+1;
    CHECK(journal_save_finish(j,&save,&b,next,4)==JOURNAL_IO && save.prepared);
    CHECK(access(save.previous_path,F_OK)==0 && verify_save(path,"Xdef")==0);
    CHECK(journal_get_stats(j).error==JOURNAL_FULL); d->fail_sync=0;
    /* Both pre-rename and post-rename/directory-barrier failures preserve it. */
    d->fail_rename=1;
    CHECK(journal_save_finish(j,&save,&b,next,4)==JOURNAL_IO && save.prepared);
    CHECK(access(save.previous_path,F_OK)==0 && verify_save(path,"Xdef")==0);
    d->fail_rename=0; d->fail_dir=d->dirs+2;
    CHECK(journal_save_finish(j,&save,&b,next,4)==JOURNAL_IO && save.prepared);
    CHECK(access(save.previous_path,F_OK)==0 && verify_save(path,"Xdef")==0);
    /* Power loss selects the old checkpoint; ordinary restart may see new.
     * Both generations restore Xdef and the independent second buffer. */
    disk_image *old=NULL;
    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) old=&d->image[i];
    CHECK(old);
    save_restore recovered={{piece_create(&a),piece_create(&a)}}; journal_replay_result rr;
    CHECK(journal_replay_bytes(old->bytes,old->size,load_save,&recovered,&rr)==0);
    uint8_t recovered_text[4];
    CHECK(piece_len(recovered.trees[0])==4 && piece_read(recovered.trees[0],0,recovered_text,4)==0 && !memcmp(recovered_text,"Xdef",4));
    CHECK(piece_len(recovered.trees[1])==2 && piece_read(recovered.trees[1],0,recovered_text,2)==0 && !memcmp(recovered_text,"YZ",2));
    piece_destroy(recovered.trees[0]); piece_destroy(recovered.trees[1]);
    CHECK(journal_retry(j)==0); d->fail_dir=0;
    CHECK(journal_save_finish(j,&save,&b,next,4)==0);
    CHECK(!save.prepared && access(save.previous_path,F_OK)!=0 && verify_save(path,"Xdef")==0);
    CHECK(verify_save_marker(path,&b,4,2)==0);
    file_close(f); piece_destroy(t); journal_close(j); free(d); work_mailbox_drain(&pool,route,NULL);
    work_pool_shutdown(&pool); unlink(path); unlink(target);
    puts("journal_test: save ok (file_save_begin, retained generation, post-save edits, two buffers, marker/rename/dir failures)"); return 0;
}
static int worker_test(void)
{
    char path[]="/tmp/journal-worker-XXXXXX"; CHECK(temp(path)>=0); work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"x",1)==0 && journal_pump(j,1,true)==0);
    unsigned wait=0; while(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) && wait++<2000) pause_ms(); CHECK(wait<2000);
    for(unsigned i=0;i<20;i++) pause_ms();
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) CHECK(atomic_load(&pool.slots[i].busy)==0);
    CHECK(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: worker returns with undrained completion"); return 0;
}
static int save_names_test(void)
{
    char directory[]="/tmp/journal-save-names-XXXXXX"; CHECK(mkdtemp(directory));
    char path[512], target[512];
    CHECK(snprintf(path,sizeof path,"%s/journal",directory)>0);
    CHECK(snprintf(target,sizeof target,"%s/",directory)>0);
    size_t start=strlen(target); memset(target+start,'a',250); target[start+250]=0;
    int fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(fd>=0 && write(fd,"abc",3)==3); close(fd);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    journal_base b; CHECK(journal_capture_base(target,&b)==0);
    uint8_t bp[4137], del[16]={0}, text[11]={0}; base_payload(bp,&b);
    del[8]=3; memcpy(text+8,"new",3);
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,41+strlen(target)},
        {JOURNAL_DELETE,1,0,del,16},{JOURNAL_INSERT,1,0,text,11}};
    journal_save save={0};
    CHECK(journal_save_prepare(j,1,&b,cp,3,&save)==0);
    CHECK(verify_save_marker(path,&b,3,1)==0);
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a);
    CHECK(tree && piece_init_copy(tree,(const uint8_t *)"new",3)==0);
    piece_snapshot *snap=piece_snapshot_take(tree); CHECK(snap);
    file_save_args args={.path=target,.snap=snap};
    CHECK(file_save_write(&args)==FILE_OK); piece_snapshot_release(snap); piece_destroy(tree);
    CHECK(journal_capture_base(target,&b)==0); base_payload(bp,&b);
    CHECK(journal_save_finish(j,&save,&b,cp,1)==0);
    CHECK(verify_save_marker(path,&b,3,2)==0); /* cutoff belongs to PREPARED generation */
    CHECK(!save.prepared && access(save.previous_path,F_OK)!=0);
    journal_close(j); work_pool_shutdown(&pool); unlink(path); unlink(target); CHECK(rmdir(directory)==0);
    puts("journal_test: save transaction accepts a 250-byte base filename"); return 0;
}
static int save_shared_base_test(void)
{
    char path[]="/tmp/journal-shared-save-XXXXXX", target[]="/tmp/journal-shared-target-XXXXXX";
    CHECK(temp(path)>=0 && temp(target)>=0);
    int fd=open(target,O_WRONLY); CHECK(fd>=0 && write(fd,"abcdef",6)==6); close(fd);
    journal_base b; CHECK(journal_capture_base(target,&b)==0);
    uint8_t bp[4137], del[16]={0}, other[9]={0}; base_payload(bp,&b); del[8]=3; other[0]=6; other[8]='Z';
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,41+strlen(target)},{JOURNAL_DELETE,1,0,del,16},
        {JOURNAL_BASE,2,0,bp,41+strlen(target)},{JOURNAL_INSERT,2,0,other,9}};
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    journal_save save={0}; CHECK(journal_save_prepare(j,1,&b,cp,4,&save)==0);
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a);
    CHECK(tree && piece_init_copy(tree,(const uint8_t *)"def",3)==0);
    piece_snapshot *snap=piece_snapshot_take(tree); CHECK(snap);
    file_save_args args={.path=target,.snap=snap}; CHECK(file_save_write(&args)==FILE_OK);
    piece_snapshot_release(snap); piece_destroy(tree);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"X",1)==0);
    CHECK(journal_insert(j,2,7,(const uint8_t *)"Y",1)==0 && journal_flush(j)==0);
    CHECK(verify_two_buffers(path,"Xdef","abcdefZY")==0);
    CHECK(journal_capture_base(target,&b)==0); base_payload(bp,&b);
    uint8_t later[9]={0}, snapshot[16]={0};
    later[8]='X'; memcpy(snapshot+8,"abcdefZY",8);
    journal_record next[]={{JOURNAL_BASE,1,0,bp,41+strlen(target)},{JOURNAL_INSERT,1,0,later,9},
        {JOURNAL_BASE,2,0,bp,41+strlen(target)},{JOURNAL_DELETE,2,0,del,16},
        {JOURNAL_INSERT,2,0,snapshot,16}};
    CHECK(journal_save_finish(j,&save,&b,next,5)==0 && verify_two_buffers(path,"Xdef","abcdefZY")==0);
    journal_close(j); work_pool_shutdown(&pool); unlink(path); unlink(target);
    puts("journal_test: save retains a named base shared by two buffers"); return 0;
}
static int cadence_test(void)
{
    char path[]="/tmp/journal-cadence-test-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path;
    journal_io io=fault_io(d); journal *j;
    journal_options opts={.sync_bytes=8192,.sync_interval_ns=1000000000000ull};
    CHECK(journal_open_with_io(&j,path,&pool,&opts,&io)==0);
    uint8_t bytes[12000]; memset(bytes,'q',sizeof bytes);
    CHECK(journal_insert(j,1,0,bytes,sizeof bytes)==0);
    CHECK(journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    unsigned wait=0;
    while(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) && wait++<2000) pause_ms();
    CHECK(wait<2000); work_mailbox_drain(&pool,route,j);
    CHECK(journal_get_stats(j).syncs==1 && journal_get_stats(j).max_sync_bytes==8192);
    CHECK(journal_get_stats(j).written_sequence==1 && journal_get_stats(j).durable_sequence==0);
    CHECK(journal_flush(j)==0 && journal_get_stats(j).durable_sequence==1);
    uint8_t op[9]={0}; op[8]='x'; journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    CHECK(journal_rotate(j,&cp,1)==0); /* the configured cadence survives rotation */
    uint64_t syncs=journal_get_stats(j).syncs;
    CHECK(journal_insert(j,1,1,bytes,sizeof bytes)==0 && journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    wait=0;
    while(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) && wait++<2000) pause_ms();
    CHECK(wait<2000); work_mailbox_drain(&pool,route,j);
    CHECK(journal_get_stats(j).syncs==syncs+1 && journal_get_stats(j).last_sync_bytes==8192);
    CHECK(journal_flush(j)==0); journal_close(j);
    int fd=open(path,O_WRONLY|O_TRUNC); CHECK(fd>=0); close(fd);
    opts=(journal_options){.sync_bytes=131072,.sync_interval_ns=1000000};
    CHECK(journal_open_with_io(&j,path,&pool,&opts,&io)==0);
    uint64_t last=journal_get_stats(j).last_sync_ns;
    CHECK(journal_insert(j,1,0,(const uint8_t *)"t",1)==0);
    CHECK(journal_pump(j,last,false)==0);
    CHECK(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail));
    for(unsigned i=0;i<3;i++) pause_ms();
    CHECK(journal_pump(j,last+opts.sync_interval_ns,false)==0);
    wait=0;
    while(atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) && wait++<2000) pause_ms();
    CHECK(wait<2000); work_mailbox_drain(&pool,route,j);
    CHECK(journal_get_stats(j).durable_sequence==1 && journal_get_stats(j).syncs==1);
    journal_close(j); free(d); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: cadence parameter ok (byte boundary, timer, force, rotation)"); return 0;
}

static int full_test(void)
{
    char path[]="/tmp/journal-full-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal_options opts={.max_file_bytes=4096}; journal *j;
    CHECK(journal_open(&j,path,&pool,&opts)==0);
    CHECK(journal_insert(j,1,0,(const uint8_t *)"a",1)==0 && journal_flush(j)==0);
    CHECK(journal_insert(j,1,1,(const uint8_t *)"b",1)==JOURNAL_FULL);
    CHECK(journal_flush(j)==JOURNAL_FULL);
    journal_stats st=journal_get_stats(j);
    CHECK(st.error==JOURNAL_FULL && st.accepted_sequence==1 && st.durable_sequence==1);
    uint8_t op[10]={0}; memcpy(op+8,"ab",2);
    journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    CHECK(journal_rotate(j,&cp,1)==0 && journal_flush(j)==0);
    model m={0}; journal_replay_result rr;
    CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==2 && !memcmp(m.text,"ab",2));
    journal_close(j); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: FULL flush reports suspension; complete checkpoint clears it"); return 0;
}
static int paste_test(bool split)
{
    char path[]="/tmp/journal-paste-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    size_t n=split?1000000u:1048576u;
    uint8_t *bytes=malloc(n); CHECK(bytes); for(size_t i=0;i<n;i++) bytes[i]=(uint8_t)(i*17u);
    int rc=0; edit_malloc_guard_begin();
    for(size_t off=0;off<n && !rc;) {
        size_t k=split?262072u:n; if(k>n-off) k=n-off;
        rc=journal_insert(j,1,off,bytes+off,k); off+=k;
    }
    size_t allocations=edit_malloc_guard_end(); CHECK(rc==JOURNAL_OK && allocations==0);
    CHECK(journal_flush(j)==0);
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t);
    journal_replay_result rr; CHECK(journal_replay_file(path,apply_tree,t,&rr)==0);
    uint8_t chunk[4096]; CHECK(piece_len(t)==n);
    for(size_t off=0;off<n;off+=sizeof chunk) {
        size_t k=n-off; if(k>sizeof chunk) k=sizeof chunk;
        CHECK(piece_read(t,off,chunk,k)==0 && !memcmp(chunk,bytes+off,k));
    }
    /* Admission covers every split record before any sequence is accepted. */
    uint64_t accepted=journal_get_stats(j).accepted_sequence;
    CHECK(journal_insert(j,1,n,bytes,SIZE_MAX)==JOURNAL_INVALID);
    CHECK(journal_get_stats(j).accepted_sequence==accepted);
    CHECK(journal_insert(j,1,n,bytes,n)==0); accepted=journal_get_stats(j).accepted_sequence;
    CHECK(journal_insert(j,1,2*n,bytes,n)==JOURNAL_FULL);
    CHECK(journal_get_stats(j).accepted_sequence==accepted && journal_flush(j)==JOURNAL_FULL);
    journal_close(j); piece_destroy(t); free(bytes); work_pool_shutdown(&pool); unlink(path);
    printf("journal_test: %s paste accepted, replay bytes exact, allocations=0\n",split?"split 1 MB":"atomic 1 MiB"); return 0;
}
typedef struct checkpoint_verify { piece_tree *tree; size_t bytes, inserts; } checkpoint_verify;
static int checkpoint_visit(void *ctx, const journal_record *r)
{
    checkpoint_verify *c=ctx;
    if(r->type!=JOURNAL_INSERT) return 0;
    if(get64(r->data)!=c->bytes) return 1;
    for(size_t i=8;i<r->size;i++) if(r->data[i]!=(uint8_t)((c->bytes+i-8)*17u)) return 1;
    if(journal_apply_piece(c->tree,r)) return 1;
    c->bytes+=r->size-8; c->inserts++; return 0;
}
static int untitled_test(void)
{
    char path[]="/tmp/journal-untitled-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    size_t n=65u*1024u*1024u+1u, chunk=200000, count=(n+chunk-1)/chunk;
    uint8_t *data=malloc(n+8*count); journal_record *cp=calloc(count+4,sizeof *cp); CHECK(data && cp);
    uint8_t base[41]={0}; cp[0]=(journal_record){JOURNAL_BASE,1,0,base,sizeof base};
    size_t used=0;
    for(size_t i=0;i<count;i++) {
        size_t k=n-i*chunk; if(k>chunk) k=chunk;
        uint8_t *p=data+used; uint64_t off=i*chunk;
        for(unsigned bit=0;bit<8;bit++) p[bit]=(uint8_t)(off>>(bit*8));
        for(size_t x=0;x<k;x++) p[x+8]=(uint8_t)((off+x)*17u);
        cp[i+1]=(journal_record){JOURNAL_INSERT,1,0,p,k+8}; used+=k+8;
    }
    uint8_t view[32]={0}, tabs[24]={0}, window[8]={0}; tabs[0]=1; tabs[16]=1;
    cp[count+1]=(journal_record){JOURNAL_VIEW,1,0,view,sizeof view};
    cp[count+2]=(journal_record){JOURNAL_TABS,0,0,tabs,sizeof tabs};
    cp[count+3]=(journal_record){JOURNAL_WINDOW,0,0,window,sizeof window};
    int rc=0; while(!rc) rc=journal_append(j,JOURNAL_INSERT,1,cp[1].data,cp[1].size);
    CHECK(rc==JOURNAL_FULL); rc=journal_flush(j); CHECK(rc==0 || rc==JOURNAL_FULL);
    CHECK(journal_rotate(j,cp,count+4)==JOURNAL_OK);
    CHECK(journal_get_stats(j).error==0 && journal_flush(j)==0);
    piece_allocator a=piece_default_allocator(); checkpoint_verify c={.tree=piece_create(&a)}; CHECK(c.tree);
    journal_replay_result rr; CHECK(journal_replay_file(path,checkpoint_visit,&c,&rr)==0 && !rr.corrupt);
    CHECK(c.bytes==n && c.inserts==count && rr.records==count+4 && piece_len(c.tree)==n);
    uint8_t text[4096];
    for(size_t off=0;off<n;off+=sizeof text) {
        size_t k=n-off; if(k>sizeof text) k=sizeof text;
        CHECK(piece_read(c.tree,off,text,k)==0);
        for(size_t i=0;i<k;i++) CHECK(text[i]==(uint8_t)((off+i)*17u));
    }
    CHECK(journal_insert(j,1,n,(const uint8_t *)"!",1)==0 && journal_flush(j)==0);
    piece_destroy(c.tree); journal_close(j); free(cp); free(data); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: untitled checkpoint >64 MiB restores exact content and resumes appends"); return 0;
}
static int paths_test(void)
{
    char directory[]="/tmp/journal-paths-XXXXXX"; CHECK(mkdtemp(directory));
    char cwd[4097]; CHECK(getcwd(cwd,sizeof cwd));
    char path[512], basepath[512], alias[512], other[512];
    CHECK(snprintf(path,sizeof path,"%s/journal",directory)>0);
    CHECK(snprintf(basepath,sizeof basepath,"%s/notes",directory)>0);
    CHECK(snprintf(alias,sizeof alias,"%s/alias",directory)>0);
    CHECK(snprintf(other,sizeof other,"%s/other",directory)>0);
    int fd=open(basepath,O_WRONLY|O_CREAT,0600); CHECK(fd>=0 && write(fd,"abc",3)==3); close(fd);
    CHECK(symlink("notes",alias)==0 && mkdir(other,0700)==0 && chdir(directory)==0);
    journal_base b; CHECK(journal_capture_base("alias",&b)==0);
    CHECK(b.path[0]=='/' && !strcmp(b.path,basepath));
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,"journal",&pool,NULL)==0 && journal_set_base(j,1,&b)==0 && journal_flush(j)==0);
    CHECK(chdir(other)==0 && journal_check_base(&b)==0);
    uint8_t bp[4137], op[9]={0}; base_payload(bp,&b); op[0]=3; op[8]='!';
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,41+strlen(b.path)},{JOURNAL_INSERT,1,0,op,9}};
    CHECK(journal_rotate(j,cp,2)==0); journal_close(j);
    CHECK(access("journal",F_OK)!=0);
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t && piece_init_copy(t,(const uint8_t *)"abc",3)==0);
    journal_replay_result rr; CHECK(journal_replay_file(path,apply_tree,t,&rr)==0 && piece_len(t)==4);
    uint8_t text[4]; CHECK(piece_read(t,0,text,4)==0 && !memcmp(text,"abc!",4));
    piece_destroy(t); work_pool_shutdown(&pool); CHECK(chdir(cwd)==0);
    unlink(path); unlink(alias); unlink(basepath); CHECK(rmdir(other)==0 && rmdir(directory)==0);
    puts("journal_test: canonical base and journal paths survive cwd changes"); return 0;
}
static void repair_crc(uint8_t *p, size_t n)
{
    memset(p+12,0,4); uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n;i++) { crc^=p[i]; for(unsigned k=0;k<8;k++) crc=(crc>>1)^((crc&1u)?0x82f63b78u:0u); }
    crc=~crc; for(unsigned k=0;k<4;k++) p[12+k]=(uint8_t)(crc>>(k*8));
}
static int schema_test(bool parser)
{
    char path[]="/tmp/journal-schema-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
    journal *j; CHECK(journal_open(&j,path,&pool,NULL)==0);
    uint8_t tabs[16]={0}, window[8]={0};
    if(!parser) {
        CHECK(journal_append(j,JOURNAL_WINDOW,99,window,sizeof window)==JOURNAL_INVALID);
        CHECK(journal_append(j,JOURNAL_TABS,99,tabs,sizeof tabs)==JOURNAL_INVALID);
        CHECK(journal_get_stats(j).accepted_sequence==0 && journal_get_stats(j).error==0);
    } else {
        CHECK(journal_set_window(j,1,1)==0 && journal_set_tabs(j,NULL,0,0)==0 && journal_flush(j)==0);
        uint8_t bytes[4096]; int fd=open(path,O_RDONLY); CHECK(fd>=0 && read(fd,bytes,sizeof bytes)==sizeof bytes); close(fd);
        for(unsigned which=0;which<2;which++) {
            uint8_t copy[4096]; memcpy(copy,bytes,sizeof copy); size_t start=which?40:0, len=which?48:40;
            copy[start+24]=99; repair_crc(copy+start,len);
            model m={0}; journal_replay_result rr;
            CHECK(journal_replay_bytes(copy,sizeof copy,apply,&m,&rr)==0);
            CHECK(rr.corrupt && rr.valid_bytes==start && rr.records==which && rr.last_sequence==which);
        }
    }
    journal_close(j); work_pool_shutdown(&pool); unlink(path);
    printf("journal_test: session IDs rejected by %s\n",parser?"CRC-repaired parser":"writer"); return 0;
}
static ssize_t unreadable_base(void *ctx, int fd, uint8_t *p, size_t n, uint64_t off)
{ (void)ctx; (void)fd; (void)p; (void)n; (void)off; errno=EIO; return -1; }
static ssize_t short_base(void *ctx, int fd, uint8_t *p, size_t n, uint64_t off)
{ (void)ctx; return off?0:pread(fd,p,n>1?1:n,(off_t)off); }
static int base_read_test(void)
{
    char path[]="/tmp/journal-base-read-XXXXXX", basepath[]="/tmp/journal-unreadable-XXXXXX";
    CHECK(temp(path)>=0 && temp(basepath)>=0);
    int fd=open(basepath,O_WRONLY); CHECK(fd>=0 && write(fd,"abc",3)==3); close(fd);
    journal_base b; journal_io io={.read=unreadable_base};
    CHECK(journal_capture_base_with_io(basepath,&b,&io)==JOURNAL_BASE_CHANGED);
    io.read=short_base; CHECK(journal_capture_base_with_io(basepath,&b,&io)==JOURNAL_BASE_CHANGED);
    CHECK(journal_capture_base(basepath,&b)==0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j;
    CHECK(journal_open(&j,path,&pool,NULL)==0 && journal_set_base(j,1,&b)==0 && journal_flush(j)==0); journal_close(j);
    struct stat before,after; CHECK(stat(path,&before)==0);
    io.read=unreadable_base; model m={0}; journal_replay_result rr;
    CHECK(journal_replay_file_with_io(path,apply,&m,&rr,&io)==JOURNAL_BASE_CHANGED && m.records==0);
    CHECK(stat(path,&after)==0 && before.st_size==after.st_size);
    CHECK(unlink(basepath)==0 && journal_check_base(&b)==JOURNAL_BASE_CHANGED);
    work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: unreadable/short/missing bases are conflicts; replay leaves log intact"); return 0;
}
static int rotate_name_test(void)
{
    char directory[]="/tmp/journal-long-name-XXXXXX"; CHECK(mkdtemp(directory));
    char path[512]; CHECK(snprintf(path,sizeof path,"%s/",directory)>0);
    size_t start=strlen(path); memset(path+start,'j',255); path[start+255]=0;
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j;
    CHECK(journal_open(&j,path,&pool,NULL)==0 && journal_insert(j,1,0,(const uint8_t *)"a",1)==0 && journal_flush(j)==0);
    uint8_t op[9]={0}; op[8]='b'; journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    CHECK(journal_rotate(j,&cp,1)==JOURNAL_OK);
    model m={0}; journal_replay_result rr; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==1 && m.text[0]=='b');
    journal_close(j); work_pool_shutdown(&pool); unlink(path); CHECK(rmdir(directory)==0);
    puts("journal_test: 255-byte journal basename rotates with independent temporary name"); return 0;
}


static int open_options_test(void)
{
    char path[]="/tmp/journal-invalid-options-XXXXXX"; CHECK(temp(path)>=0 && unlink(path)==0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j=NULL;
    journal_options opts={.batch_bytes=8193};
    CHECK(journal_open(&j,path,&pool,&opts)==JOURNAL_INVALID && !j && access(path,F_OK)!=0);
    opts=(journal_options){.max_file_bytes=4097};
    CHECK(journal_open(&j,path,&pool,&opts)==JOURNAL_INVALID && !j && access(path,F_OK)!=0);
    opts=(journal_options){.sync_bytes=4097};
    CHECK(journal_open(&j,path,&pool,&opts)==JOURNAL_INVALID && !j && access(path,F_OK)!=0);
    work_pool_shutdown(&pool); puts("journal_test: invalid setup options create no journal"); return 0;
}
static int rotate_path_test(void)
{
    char root[]="/tmp/journal-long-path-XXXXXX"; CHECK(mkdtemp(root));
    char paths[18][4097]; strcpy(paths[0],root); size_t depth=0;
    while(strlen(paths[depth])<4093) {
        size_t len=strlen(paths[depth]), n=4093-len-1; if(n>245) n=245;
        CHECK(depth+1<18 && n>0); memcpy(paths[depth+1],paths[depth],len); paths[depth+1][len++]='/';
        memset(paths[depth+1]+len,'d',n); paths[depth+1][len+n]=0; depth++;
        CHECK(mkdir(paths[depth],0700)==0);
    }
    char path[4097]; size_t len=strlen(paths[depth]); CHECK(len==4093);
    memcpy(path,paths[depth],len); memcpy(path+len,"/j",3);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j;
    CHECK(journal_open(&j,path,&pool,NULL)==0 && journal_insert(j,1,0,(const uint8_t *)"a",1)==0 && journal_flush(j)==0);
    uint8_t op[9]={0}; op[8]='b'; journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    CHECK(journal_rotate(j,&cp,1)==JOURNAL_OK);
    model m={0}; journal_replay_result rr; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==1 && m.text[0]=='b');
    journal_close(j); work_pool_shutdown(&pool); unlink(path);
    do { CHECK(rmdir(paths[depth])==0); } while(depth--);
    puts("journal_test: 4095-byte journal path rotates via retained directory fd"); return 0;
}
static int directory_fd_test(void)
{
    char root[]="/tmp/journal-directory-fd-XXXXXX"; CHECK(mkdtemp(root));
    char path[512], renamed[512], newpath[512];
    CHECK(snprintf(path,sizeof path,"%s/log",root)>0 && snprintf(renamed,sizeof renamed,"%s-moved",root)>0);
    CHECK(snprintf(newpath,sizeof newpath,"%s/log",renamed)>0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j;
    CHECK(journal_open(&j,path,&pool,NULL)==0 && journal_insert(j,1,0,(const uint8_t *)"a",1)==0 && journal_flush(j)==0);
    CHECK(rename(root,renamed)==0);
    uint8_t op[9]={0}; op[8]='b'; journal_record cp={JOURNAL_INSERT,1,0,op,sizeof op};
    CHECK(journal_rotate(j,&cp,1)==0 && journal_flush(j)==0); journal_close(j);
    model m={0}; journal_replay_result rr;
    CHECK(journal_replay_file(newpath,apply,&m,&rr)==0 && m.len==1 && m.text[0]=='b');
    work_pool_shutdown(&pool); unlink(newpath); CHECK(rmdir(renamed)==0);
    puts("journal_test: journal rotation survives parent directory rename"); return 0;
}
/* Boundaries and byte snapshots come from the scripted mutations, before
 * encoding; expected prefix never depends on the parser's accepted count. */
static int exact_check(const uint8_t *bytes, size_t n, const size_t *starts,
                        const model *snapshots, bool flip, size_t changed)
{
    size_t count=0;
    while(count<8 && starts[count+1]<=n && (!flip || changed>=starts[count+1])) count++;
    size_t boundary=starts[count];
    if(count==8 && n==starts[9] && !flip) boundary=n;
    model actual={0}; journal_replay_result rr;
    CHECK(journal_replay_bytes(bytes,n,apply,&actual,&rr)==0);
    const model *expected=&snapshots[count];
    CHECK(rr.valid_bytes==boundary && rr.records==count && rr.last_sequence==count);
    CHECK(actual.records==count && actual.len==expected->len && !memcmp(actual.text,expected->text,actual.len));
    CHECK(rr.corrupt==(boundary!=n)); return 0;
}
static int exact_prefix_test(void)
{
    char path[]="/tmp/journal-exact-XXXXXX"; CHECK(temp(path)>=0);
    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0); journal *j;
    CHECK(journal_open(&j,path,&pool,NULL)==0);
    size_t starts[10]={0}; model *snapshots=calloc(9,sizeof *snapshots); CHECK(snapshots);
    uint8_t data[17000];
    for(size_t i=0;i<8;i++) {
        if(i%2==0) {
            memset(data,(int)('a'+i),sizeof data); CHECK(journal_insert(j,1,0,data,sizeof data)==0);
            snapshots[i+1].len=sizeof data; memcpy(snapshots[i+1].text,data,sizeof data);
            starts[i+1]=starts[i]+sizeof data+40;
        } else { CHECK(journal_delete(j,1,0,sizeof data)==0); starts[i+1]=starts[i]+48; }
        snapshots[i+1].records=i+1;
    }
    CHECK(journal_flush(j)==0); starts[9]=(size_t)journal_get_stats(j).file_bytes; journal_close(j);
    uint8_t *bytes=malloc(starts[9]), *copy=malloc(starts[9]); CHECK(bytes && copy);
    int fd=open(path,O_RDONLY); CHECK(fd>=0 && read(fd,bytes,starts[9])==(ssize_t)starts[9]); close(fd);
    CHECK(exact_check(bytes,starts[9],starts,snapshots,false,0)==0);
    for(size_t cut=0;cut<4096;cut++) CHECK(exact_check(bytes,cut,starts,snapshots,false,0)==0);
    for(size_t cut=65536-64;cut<65536+64;cut++) CHECK(exact_check(bytes,cut,starts,snapshots,false,0)==0);
    for(size_t i=0;i<9;i++) {
        size_t lo=starts[i]>32?starts[i]-32:0, hi=starts[i]+32; if(hi>starts[9]) hi=starts[9];
        for(size_t x=lo;x<hi;x++) {
            memcpy(copy,bytes,starts[9]); copy[x]^=1;
            CHECK(exact_check(copy,starts[9],starts,snapshots,true,x)==0);
            CHECK(exact_check(bytes,x,starts,snapshots,false,0)==0);
        }
    }
    /* File truncation agrees with the independent scripted boundary too. */
    for(size_t i=1;i<10;i++) {
        size_t cut=starts[i]-1, completed=i>8?8:i-1, boundary=starts[completed];
        fd=open(path,O_WRONLY|O_TRUNC); CHECK(fd>=0 && write(fd,bytes,cut)==(ssize_t)cut); close(fd);
        model actual={0}; journal_replay_result rr;
        CHECK(journal_replay_file(path,apply,&actual,&rr)==0 && rr.valid_bytes==boundary && rr.last_sequence==completed && rr.records==completed);
        CHECK(actual.len==snapshots[completed].len && !memcmp(actual.text,snapshots[completed].text,actual.len));
        struct stat sb; CHECK(stat(path,&sb)==0 && (size_t)sb.st_size==boundary);
    }
    free(copy); free(bytes); free(snapshots); work_pool_shutdown(&pool); unlink(path);
    puts("journal_test: exact scripted corruption/truncation prefix (first page, 64 KiB, PAD, byte contents)"); return 0;
}
int main(int argc, char **argv)
{
    if(argc==2) {
        if(!strcmp(argv[1],"--open-options")) return open_options_test();
        if(!strcmp(argv[1],"--rotate-path")) return rotate_path_test();
        if(!strcmp(argv[1],"--directory-fd")) return directory_fd_test();
        if(!strcmp(argv[1],"--exact-prefix")) return exact_prefix_test();
        if(!strcmp(argv[1],"--full")) return full_test();
        if(!strcmp(argv[1],"--paste-split")) return paste_test(true);
        if(!strcmp(argv[1],"--paste-atomic")) return paste_test(false);
        if(!strcmp(argv[1],"--untitled")) return untitled_test();
        if(!strcmp(argv[1],"--paths")) return paths_test();
        if(!strcmp(argv[1],"--schema-writer")) return schema_test(false);
        if(!strcmp(argv[1],"--schema-parser")) return schema_test(true);
        if(!strcmp(argv[1],"--base-read")) return base_read_test();
        if(!strcmp(argv[1],"--rotate-name")) return rotate_name_test();
        if(!strcmp(argv[1],"--retry")) return retry_test();
        if(!strcmp(argv[1],"--barriers")) return barriers_test();
        if(!strcmp(argv[1],"--save")) return save_test();
        if(!strcmp(argv[1],"--worker")) return worker_test();
        if(!strcmp(argv[1],"--cadence")) return cadence_test();
        if(!strcmp(argv[1],"--save-names")) return save_names_test();
        if(!strcmp(argv[1],"--save-shared")) return save_shared_base_test();
        return 2;
    }
    CHECK(open_options_test()==0 && rotate_path_test()==0 && directory_fd_test()==0 && exact_prefix_test()==0 && full_test()==0 && paste_test(true)==0 && paste_test(false)==0 && untitled_test()==0 &&
          paths_test()==0 && schema_test(false)==0 && schema_test(true)==0 && base_read_test()==0 && rotate_name_test()==0);
    CHECK(retry_test()==0 && barriers_test()==0 && save_test()==0 && worker_test()==0 &&
          cadence_test()==0 && save_names_test()==0 && save_shared_base_test()==0);

    char path[] = "/tmp/journal-test-XXXXXX", basepath[] = "/tmp/journal-base-XXXXXX";
    CHECK(temp(path) >= 0); CHECK(temp(basepath) >= 0);
    work_pool pool; CHECK(work_pool_init(&pool, 1, 1) == 0);
    journal *j = NULL; CHECK(journal_open(&j, path, &pool, NULL) == 0);
    journal_base empty = {.path=""}; CHECK(journal_set_base(j, 7, &empty) == 0);
    uint8_t big[4100]; memset(big, 'a', sizeof big);
    CHECK(journal_insert(j, 7, 0, big, sizeof big) == 0); /* straddles page */
    CHECK(journal_delete(j, 7, 1, 3) == 0);
    journal_view v = {17, 4, 200, 9}; CHECK(journal_set_view(j, 7, &v) == 0);
    uint64_t ids[] = {7, 19}; CHECK(journal_set_tabs(j, ids, 2, 1) == 0);
    CHECK(journal_set_window(j, 1200, 800) == 0);
    CHECK(journal_flush(j) == 0);
    edit_malloc_guard_begin();
    for (unsigned i=0;i<10000;i++) CHECK(journal_insert(j, 7, 0, (const uint8_t *)"x", 1) == JOURNAL_OK);
    CHECK(edit_malloc_guard_end() == 0);
    printf("journal_test: malloc_guard=%s append_allocations=0\n",edit_malloc_guard_active()?"active":"ASan-inactive (release run required)");
    /* All 10000 guarded appends succeeded. Now separately saturate the queue. */
    while(journal_insert(j,7,0,(const uint8_t *)"x",1)==0) {}
    /* FULL is sticky and accepted prefix is retained. */
    CHECK(journal_insert(j, 7, 0, (const uint8_t *)"x", 1) == JOURNAL_FULL);
    CHECK(journal_flush(j) == JOURNAL_FULL);
    journal_stats st = journal_get_stats(j); CHECK(st.durable_sequence == st.accepted_sequence);
    journal_close(j);
    model m = {0}; journal_replay_result rr;
    CHECK(journal_replay_file(path, apply, &m, &rr) == 0);
    CHECK(m.len > 4097 && m.views == 1 && m.tabs == 1 && m.windows == 1 && !rr.corrupt);
    int fd = open(path, O_RDONLY); CHECK(fd >= 0);
    size_t size = (size_t)st.file_bytes;
    uint8_t *bytes = malloc(size), *copy = malloc(size); CHECK(bytes && copy);
    CHECK(read(fd, bytes, size) == (ssize_t)size); close(fd);
    /* Smaller fixture ends at first batch; mutate every byte, including CRC/padding. */
    size_t small = 8192;
    CHECK(small > 4100);
    size_t record_start=0;
    for (size_t i=0;i<small;i++) {
        size_t record_len=(size_t)((uint32_t)bytes[record_start+4] | (uint32_t)bytes[record_start+5]<<8 | (uint32_t)bytes[record_start+6]<<16 | (uint32_t)bytes[record_start+7]<<24);
        if(i>=record_start+record_len) record_start+=record_len;
        memcpy(copy, bytes, small); copy[i] ^= 1; model a = {0};
        CHECK(journal_replay_bytes(copy, small, apply, &a, &rr) == 0);
        CHECK(rr.corrupt && rr.valid_bytes == record_start);
    }
    char tornpath[] = "/tmp/journal-torn-XXXXXX"; CHECK(temp(tornpath) >= 0);
    for (size_t n=0;n<small;n++) {
        model a = {0}; CHECK(journal_replay_bytes(bytes, n, apply, &a, &rr) == 0); size_t boundary=0; uint64_t seq=0;
        while(boundary+32<=n) {
            size_t len=(size_t)((uint32_t)bytes[boundary+4] | (uint32_t)bytes[boundary+5]<<8 | (uint32_t)bytes[boundary+6]<<16 | (uint32_t)bytes[boundary+7]<<24);
            if(len>n-boundary) break;
            if(bytes[boundary+8]) seq++;
            boundary+=len;
        }
        CHECK(rr.valid_bytes==boundary && rr.last_sequence==seq && rr.records==seq);
        fd=open(tornpath,O_WRONLY|O_TRUNC); CHECK(fd>=0); CHECK(write(fd,bytes,n)==(ssize_t)n); close(fd);
        uint64_t expected_valid=rr.valid_bytes;
        a=(model){0}; CHECK(journal_replay_file(tornpath,apply,&a,&rr)==0);
        CHECK(rr.valid_bytes==expected_valid); struct stat tornstat; CHECK(stat(tornpath,&tornstat)==0 && (uint64_t)tornstat.st_size==expected_valid);
    }
    unlink(tornpath);
    /* Actual truncation, callback failure does not truncate. */
    fd = open(path, O_RDWR); CHECK(fd >= 0); CHECK(ftruncate(fd, (off_t)(size-1)) == 0); close(fd);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0 && rr.corrupt);
    struct stat sb; CHECK(stat(path, &sb) == 0 && (uint64_t)sb.st_size == rr.valid_bytes);
    uint64_t prior = rr.valid_bytes;
    CHECK(journal_replay_file(path, reject, NULL, &rr) == JOURNAL_CALLBACK);
    CHECK(stat(path, &sb) == 0 && (uint64_t)sb.st_size == prior);
    free(copy); free(bytes);
    /* Base conflict, including same size mutation with restored mtime. */
    fd = open(basepath, O_WRONLY); CHECK(fd >= 0 && write(fd, "abc", 3) == 3); close(fd);
    journal_base base; CHECK(journal_capture_base(basepath, &base) == 0); CHECK(journal_check_base(&base) == 0);
    fd = open(basepath, O_WRONLY); CHECK(fd >= 0 && write(fd, "xyz", 3) == 3); close(fd);
    struct timespec times[2] = {{0,UTIME_OMIT},{(time_t)(base.mtime_ns/1000000000u),(long)(base.mtime_ns%1000000000u)}};
    CHECK(utimensat(AT_FDCWD, basepath, times, 0) == 0); CHECK(journal_check_base(&base) == JOURNAL_BASE_CHANGED);
    /* Saturation while worker falls behind; no later edits after gap. */
    fd = open(path, O_TRUNC | O_RDWR); CHECK(fd >= 0); close(fd);
    journal_options opts = {.batch_bytes=8192, .max_file_bytes=16384};
    CHECK(journal_open(&j, path, &pool, &opts) == 0);
    work_handle blocker = work_submit(&pool, (work_job){blocked, NULL, 0, WORK_BULK}); CHECK(blocker.epoch != 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_pump(j, 1, true) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == 0);
    CHECK(journal_insert(j, 7, 0, big, 4000) == JOURNAL_FULL);
    work_cancel(&pool, blocker); CHECK(journal_flush(j) == JOURNAL_FULL);
    st = journal_get_stats(j); CHECK(st.durable_sequence == 3 && st.file_bytes == 12288);
    /* Rotation removes pre-save operations, preserving a complete session. */
    uint8_t op[11] = {0}; memcpy(op+8, "new", 3);
    journal_record cp = {JOURNAL_INSERT, 7, 0, op, sizeof op};
    CHECK(journal_rotate(j, &cp, 1) == 0);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0); CHECK(m.len == 3 && memcmp(m.text, "new", 3) == 0);
    CHECK(journal_get_stats(j).error == 0);
    CHECK(journal_insert(j, 7, 3, (const uint8_t *)"!", 1) == 0); CHECK(journal_flush(j) == 0);
    journal_close(j);
    m = (model){0}; CHECK(journal_replay_file(path, apply, &m, &rr) == 0); CHECK(m.len == 4 && memcmp(m.text, "new!", 4) == 0);
    CHECK(journal_open(&j, path, &pool, NULL) == JOURNAL_INVALID);
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    opts=(journal_options){.batch_bytes=8192,.max_file_bytes=4096}; CHECK(journal_open(&j,path,&pool,&opts)==0);
    CHECK(journal_insert(j,7,0,(const uint8_t *)"x",1)==0); CHECK(journal_flush(j)==0);
    CHECK(journal_insert(j,7,1,(const uint8_t *)"y",1)==JOURNAL_FULL);
    CHECK(journal_flush(j)==JOURNAL_FULL); CHECK(journal_get_stats(j).file_bytes==4096);
    journal_record invalid={99,7,0,op,sizeof op};
    CHECK(journal_rotate(j,&invalid,1)==JOURNAL_INVALID);
    m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==1 && m.text[0]=='x');
    journal_close(j);
    /* Unread completion keeps its epoch alive while other jobs are queued.
     * Blocking operations preserve other modules' mailbox messages. */
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    CHECK(journal_open(&j,path,&pool,NULL)==0);
    unsigned other_count=0; journal_set_message_handler(j,other_route,&other_count);
    work_handle other_handle=work_submit(&pool,(work_job){other_message,NULL,0,WORK_RASTER});
    CHECK(other_handle.epoch!=0);
    CHECK(journal_insert(j,7,0,(const uint8_t *)"x",1)==0);
    CHECK(journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    CHECK(journal_pump(j,1,true)==0);
    unsigned waiting=0;
    while((atomic_load(&pool.mb[0].head)==atomic_load(&pool.mb[0].tail) || atomic_load(&pool.mb[1].head)==atomic_load(&pool.mb[1].tail)) && waiting++<2000) pause_ms();
    CHECK(waiting<2000);
    work_handle handles[WORK_MAX_JOBS]; size_t submitted=0;
    for(size_t i=0;i<WORK_MAX_JOBS;i++) { handles[i]=work_submit(&pool,(work_job){other_message,NULL,0,WORK_BULK}); if(handles[i].epoch) submitted++; }
    CHECK(submitted==WORK_MAX_JOBS-2); /* journal job still reserves one slot */
    for(size_t i=0;i<WORK_MAX_JOBS;i++) work_cancel(&pool,handles[i]);
    CHECK(journal_flush(j)==0 && other_count==1);
    work_cancel(&pool,other_handle);
    journal_close(j);
    /* A changed base stops file replay, with no journal truncation. */
    CHECK(journal_capture_base(basepath,&base)==0);
    fd=open(path,O_RDWR|O_TRUNC); CHECK(fd>=0); close(fd);
    CHECK(journal_open(&j,path,&pool,NULL)==0); CHECK(journal_set_base(j,7,&base)==0);
    CHECK(journal_insert(j,7,3,(const uint8_t *)"!",1)==0); CHECK(journal_flush(j)==0); journal_close(j);
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); CHECK(tree!=NULL);
    CHECK(piece_init_copy(tree,(const uint8_t *)"xyz",3)==0);
    CHECK(journal_replay_file(path,apply_tree,tree,&rr)==0);
    uint8_t restored[4]; CHECK(piece_len(tree)==4 && piece_read(tree,0,restored,sizeof restored)==0 && memcmp(restored,"xyz!",4)==0); piece_destroy(tree);
    CHECK(stat(path,&sb)==0); off_t before_size=sb.st_size;
    fd=open(basepath,O_WRONLY); CHECK(fd>=0 && write(fd,"123",3)==3); close(fd);
    m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==JOURNAL_BASE_CHANGED && m.records==0);
    CHECK(stat(path,&sb)==0 && sb.st_size==before_size);
    work_mailbox_drain(&pool, route, NULL);
    work_pool_shutdown(&pool); unlink(path); unlink(basepath);
    puts("journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)");
    return 0;
}
