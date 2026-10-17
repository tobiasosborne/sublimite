#include "journal/journal.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

typedef struct fuzz_model { uint8_t bytes[4096]; size_t size, edits; piece_tree *tree;
    journal_view views[2]; journal_view_state full_views[2]; uint64_t view_ids[2], tab_ids[3], active, tab_count;
    uint32_t width, height; unsigned view_count, tabs, windows;
} fuzz_model;
static uint64_t read64(const uint8_t *p)
{ uint64_t v=0; for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
static int apply(void *ctx, const journal_record *r)
{
    fuzz_model *m=ctx;
    if(r->type==JOURNAL_VIEW) {
        if(m->view_count>=2) return 1;
        size_t i=m->view_count++;
        m->views[i]=(journal_view){read64(r->data),read64(r->data+8),read64(r->data+16),read64(r->data+24)};
        EDIT_ASSERT(journal_decode_view_state(r,&m->full_views[i])==JOURNAL_OK);
        m->view_ids[i]=r->buffer_id; return 0;
    }
    if(r->type==JOURNAL_TABS) {
        uint64_t count=read64(r->data); if(count>3) return 1;
        m->tab_count=count; m->active=read64(r->data+8); memset(m->tab_ids,0,sizeof m->tab_ids);
        for(size_t i=0;i<count;i++) m->tab_ids[i]=read64(r->data+16+8*i);
        m->tabs++; return 0;
    }
    if(r->type==JOURNAL_WINDOW) {
        m->width=(uint32_t)r->data[0] | (uint32_t)r->data[1]<<8 | (uint32_t)r->data[2]<<16 | (uint32_t)r->data[3]<<24;
        m->height=(uint32_t)r->data[4] | (uint32_t)r->data[5]<<8 | (uint32_t)r->data[6]<<16 | (uint32_t)r->data[7]<<24;
        m->windows++; return 0;
    }
    if(r->type!=JOURNAL_INSERT && r->type!=JOURNAL_DELETE) return 0;
    uint64_t off=read64(r->data);
    if(off>m->size) return 1;
    if(r->type==JOURNAL_INSERT) {
        size_t n=r->size-8; if(n>sizeof m->bytes-m->size) return 1;
        if(m->tree) EDIT_ASSERT(journal_apply_piece(m->tree,r)==0);
        memmove(m->bytes+(size_t)off+n,m->bytes+(size_t)off,m->size-(size_t)off);
        memcpy(m->bytes+(size_t)off,r->data+8,n); m->size+=n;
    } else {
        uint64_t n=read64(r->data+8); if(n>m->size-off) return 1;
        if(m->tree) EDIT_ASSERT(journal_apply_piece(m->tree,r)==0);
        memmove(m->bytes+(size_t)off,m->bytes+(size_t)(off+n),m->size-(size_t)(off+n)); m->size-=(size_t)n;
    }
    m->edits++; return 0;
}
static bool session_equal(const fuzz_model *a, const fuzz_model *b)
{
    return a->view_count==b->view_count && a->tabs==b->tabs && a->windows==b->windows &&
        !memcmp(a->views,b->views,sizeof a->views) &&
        !memcmp(a->full_views,b->full_views,sizeof a->full_views) && !memcmp(a->view_ids,b->view_ids,sizeof a->view_ids) &&
        !memcmp(a->tab_ids,b->tab_ids,sizeof a->tab_ids) && a->active==b->active && a->tab_count==b->tab_count &&
        a->width==b->width && a->height==b->height;
}
static void no_visit(const uint8_t *data, size_t size)
{
    fuzz_model m={0}; journal_replay_result rr;
    int rc=journal_replay_bytes(data,size,apply,&m,&rr);
    EDIT_ASSERT(rc==0 || rc==JOURNAL_CALLBACK);
    EDIT_ASSERT(rr.valid_bytes<=size && m.size<=sizeof m.bytes);
    /* A callback failure inside a committed group is a fatal restore error:
     * the caller discards partial callback state, as the replay API requires.
     * Only successful replay can be compared to its accepted wire prefix. */
    if(rc==JOURNAL_CALLBACK) return;
    /* Deterministic replay of accepted prefix has identical contents. */
    fuzz_model prefix={0}; journal_replay_result pr;
    EDIT_ASSERT(journal_replay_bytes(data,(size_t)rr.valid_bytes,apply,&prefix,&pr)==0);
    EDIT_ASSERT(!pr.corrupt && m.size==prefix.size && memcmp(m.bytes,prefix.bytes,m.size)==0 && session_equal(&m,&prefix));
}
/* Repair CRC around arbitrary payloads so mutation reaches schema checks,
 * rather than spending all raw-byte coverage on magic/CRC rejection. */
static void structured_bytes(const uint8_t *data, size_t size)
{
    if(!size || size>16384) return;
    uint8_t wire[16416]={0}; size_t n=size-1, total=n+32;
    wire[0]='J'; wire[1]='N'; wire[2]='L'; wire[3]='1';
    for(unsigned k=0;k<4;k++) wire[4+k]=(uint8_t)((uint32_t)total>>(8*k));
    /* (P) modulo the last wire enum plus the PAD value exercises every type. */
    wire[8]=(uint8_t)(data[0]%(JOURNAL_GROUP_COMMIT+1u)); wire[16]=1;
    wire[24]=(data[0]&16u)?99:(wire[8]==JOURNAL_TABS || wire[8]==JOURNAL_WINDOW || wire[8]==JOURNAL_GROUP_BEGIN || wire[8]==JOURNAL_GROUP_COMMIT?0:1);
    memcpy(wire+32,data+1,n);
    uint32_t crc=UINT32_MAX;
    for(size_t k=0;k<total;k++) {
        crc^=wire[k];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1u)?0x82f63b78u:0u);
    }
    crc=~crc;
    for(unsigned k=0;k<4;k++) wire[12+k]=(uint8_t)(crc>>(8*k));
    no_visit(wire,total);
}
typedef struct fuzz_io { unsigned writes, syncs, appends; bool write_error, sync_error; int append_error; } fuzz_io;
static ssize_t fuzz_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    fuzz_io *f=ctx; f->appends++;
    if(f->write_error && f->appends==1) {
        if(f->append_error) { errno=f->append_error; return -1; }
        if(n>17) n=17;
    }
    return pwrite(fd,p,n,(off_t)off);
}
static ssize_t fuzz_write(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    fuzz_io *f=ctx; f->writes++;
    if(f->write_error && f->writes==1 && n>17) n=17;
    if(f->write_error && f->writes==2) { errno=EIO; return -1; }
    return pwrite(fd,p,n,(off_t)off);
}
static int fuzz_sync(void *ctx, int fd, bool directory)
{
    fuzz_io *f=ctx;
    if(directory) return fsync(fd);
    f->syncs++;
    if(f->sync_error && f->syncs==1) {
        /* Consumed writeback EIO may leave clean lost pages. */
        if(ftruncate(fd,0)) return -1;
        errno=EIO; return -1;
    }
    return fdatasync(fd);
}

static void write64(uint8_t *p, uint64_t value)
{ for(unsigned k=0;k<8;k++) p[k]=(uint8_t)(value>>(k*8)); }
static void fuzz_sessions(const uint8_t *data, size_t size)
{
    if(size<5 || (data[0]&15u)!=7u) return;
    char path[]="/tmp/journal-fuzz-session-XXXXXX"; int fd=mkstemp(path); if(fd<0) return; close(fd);
    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return; }
    journal *j; if(journal_open(&j,path,&pool,NULL)) { work_pool_shutdown(&pool); unlink(path); return; }
    /* Expectations come directly from the generated session, before encoding.
     * Never infer the expected fields from the parser's record count. */
    fuzz_model expected={.views={{(uint64_t)data[1]+513,7,(uint64_t)data[2]*4096+1,23},
        {3,(uint64_t)data[3]+619,(uint64_t)data[4]*4096+9,41}},
        .view_ids={101,503},.tab_ids={101,503,909},.active=1u+data[4]%2u,.tab_count=3,
        .width=701u+data[1],.height=503u+data[2],.view_count=2,.tabs=1,.windows=1};
    /* (G, wire schema) full VIEW uses eight uint64_t fields; fuzz both lengths
     * and every extended field, including all combinations of defined flags. */
    uint8_t views[2][JOURNAL_VIEW_STATE_BYTES], tabs[40], window[JOURNAL_WINDOW_BYTES];
    size_t view_size=(data[1]&1u)?JOURNAL_VIEW_STATE_BYTES:sizeof(journal_view);
    for(unsigned i=0;i<2;i++) {
        expected.full_views[i]=(journal_view_state){.position=expected.views[i],
            .preferred_col=UINT64_MAX,.visual_byte=expected.views[i].scroll_byte};
        if(view_size==JOURNAL_VIEW_STATE_BYTES) {
            expected.full_views[i].preferred_col=data[i+1];
            expected.full_views[i].first_line=data[i+2];
            expected.full_views[i].visual_byte=data[i+3];
            expected.full_views[i].flags=data[(i+4u)%size] &
                (JOURNAL_VIEW_WRAP | JOURNAL_VIEW_APPROXIMATE | JOURNAL_VIEW_VISUAL_END);
        }
        journal_encode_view_state(&expected.full_views[i],views[i]);
        EDIT_ASSERT(journal_append(j,JOURNAL_VIEW,expected.view_ids[i],views[i],view_size)==0);
    }
    EDIT_ASSERT(journal_set_tabs(j,expected.tab_ids,3,expected.active)==0 && journal_set_window(j,expected.width,expected.height)==0);
    EDIT_ASSERT(journal_flush(j)==0);
    fuzz_model actual={0}; journal_replay_result rr;
    EDIT_ASSERT(journal_replay_file(path,apply,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected));
    write64(tabs,3); write64(tabs+8,expected.active);
    for(unsigned i=0;i<3;i++) write64(tabs+16+8*i,expected.tab_ids[i]);
    /* WINDOW v2 (12 bytes, chosen flag) and legacy (8 bytes) both replay. */
    journal_window w={expected.width,expected.height,JOURNAL_WINDOW_CHOSEN}, decoded;
    journal_encode_window(&w,window);
    size_t window_size=(data[1]&2u)?JOURNAL_WINDOW_BYTES:JOURNAL_WINDOW_LEGACY_BYTES;
    journal_record window_record={JOURNAL_WINDOW,0,0,window,window_size};
    EDIT_ASSERT(journal_decode_window(&window_record,&decoded)==0 && decoded.width==w.width && decoded.height==w.height &&
        decoded.flags==(window_size==JOURNAL_WINDOW_BYTES?JOURNAL_WINDOW_CHOSEN:0u));
    journal_record cp[]={{JOURNAL_VIEW,101,0,views[0],view_size},{JOURNAL_VIEW,503,0,views[1],view_size},
        {JOURNAL_TABS,0,0,tabs,40},window_record};
    EDIT_ASSERT(journal_rotate(j,cp,4)==0); journal_close(j);
    actual=(fuzz_model){0};
    EDIT_ASSERT(journal_replay_file(path,apply,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected));
    work_pool_shutdown(&pool); unlink(path);
}
typedef struct slice_model { const uint8_t *bytes; size_t size, copied; } slice_model;
static int apply_slice(void *ctx, const journal_record *r)
{
    slice_model *m=ctx;
    if(r->type!=JOURNAL_INSERT || r->buffer_id!=41 || r->size<8) return 1;
    size_t n=r->size-8;
    if(read64(r->data)!=m->copied || n>m->size-m->copied ||
       memcmp(r->data+8,m->bytes+m->copied,n)) return 1;
    m->copied+=n; return 0;
}
static void fuzz_slices(const uint8_t *data, size_t size)
{
    if(size<3 || (data[0]&15u)!=3u) return;
    uint8_t bytes[65536], wire[73728];
    size_t length=1+(size_t)data[1]*256+data[2];
    for(size_t i=0;i<length;i++) bytes[i]=data[i%size];
    char path[]="/tmp/journal-fuzz-slices-XXXXXX"; int fd=mkstemp(path); if(fd<0) return; close(fd);
    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return; }
    journal *j; if(journal_open(&j,path,&pool,NULL)) { work_pool_shutdown(&pool); unlink(path); return; }
    size_t progress=0;
    while(progress<length) {
        size_t before=progress;
        int rc=journal_insert_step(j,41,0,bytes,length,&progress);
        EDIT_ASSERT(rc==(progress<length?JOURNAL_BUSY:JOURNAL_OK));
        EDIT_ASSERT(progress>=before && progress-before<=JOURNAL_INSERT_SLICE_BYTES);
        EDIT_ASSERT(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
        if(progress==before) { EDIT_ASSERT(journal_flush(j)==0); continue; }
        fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
        ssize_t n=read(fd,wire,sizeof wire); close(fd); EDIT_ASSERT(n>=0);
        slice_model m={bytes,progress,0}; journal_replay_result rr;
        EDIT_ASSERT(journal_replay_bytes(wire,(size_t)n,apply_slice,&m,&rr)==0 && !rr.corrupt && m.copied==progress);
        /* A caller can abandon the unaccepted tail after any protected step. */
        if((data[0]&128u) && progress>=length/2) break;
    }
    EDIT_ASSERT(journal_flush(j)==0 && journal_get_stats(j).unprotected_bytes==0);
    journal_close(j); work_pool_shutdown(&pool); unlink(path);
}
/* edit-pgr: the input chooses a valid group's payload and torn-byte cutoff.
 * (P) Two pages cover the existing model's one-page payload, record headers
 * and final PAD; expected bytes come from the input, never the parser. */
static void fuzz_groups(const uint8_t *data, size_t size)
{
    if(!size) return;
    char path[]="/tmp/journal-fuzz-group-XXXXXX"; int fd=mkstemp(path); if(fd<0) return; close(fd);
    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return; }
    journal *j; if(journal_open(&j,path,&pool,NULL)) { work_pool_shutdown(&pool); unlink(path); return; }
    size_t len=size<sizeof((fuzz_model *)0)->bytes?size:sizeof((fuzz_model *)0)->bytes;
    EDIT_ASSERT(journal_insert(j,1,0,(const uint8_t *)"x",strlen("x"))==0);
    size_t legacy=(size_t)journal_get_stats(j).file_bytes;
    EDIT_ASSERT(journal_append(j,JOURNAL_GROUP_BEGIN,0,NULL,0)==0);
    EDIT_ASSERT(journal_delete(j,1,0,strlen("x"))==0);
    EDIT_ASSERT(journal_insert(j,1,0,data,len)==0);
    size_t commit=(size_t)journal_get_stats(j).file_bytes;
    EDIT_ASSERT(journal_append(j,JOURNAL_GROUP_COMMIT,0,NULL,0)==0);
    size_t committed=(size_t)journal_get_stats(j).file_bytes;
    journal_close(j);
    uint8_t bytes[JOURNAL_PAGE+JOURNAL_PAGE]; fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
    ssize_t got=read(fd,bytes,sizeof bytes); close(fd); EDIT_ASSERT(got>0);
    size_t cut=((size_t)data[0]*len+data[size-1])%((size_t)got+1u);
    fuzz_model actual={0}; journal_replay_result rr;
    EDIT_ASSERT(journal_replay_bytes(bytes,cut,apply,&actual,&rr)==0);
    size_t want=cut<legacy?0:cut<committed?strlen("x"):len;
    const uint8_t *expected=cut<committed?(const uint8_t *)"x":data;
    EDIT_ASSERT(actual.size==want && !memcmp(actual.bytes,expected,want));
    EDIT_ASSERT(rr.valid_bytes<=cut);
    /* Corrupt a byte inside the committed group's COMMIT header. */
    bytes[commit+data[0]%JOURNAL_HEADER]^=1u;
    actual=(fuzz_model){0};
    EDIT_ASSERT(journal_replay_bytes(bytes,(size_t)got,apply,&actual,&rr)==0);
    EDIT_ASSERT(rr.corrupt && rr.valid_bytes==legacy && actual.size==strlen("x") && actual.bytes[0]=='x');
    work_pool_shutdown(&pool); unlink(path);
}
/* edit-52o: directory-state oracles for rotation names, with arbitrary small
 * content. (P) Cases come from the protocol: surviving/missing/reused source,
 * legacy, malformed, already set aside, live, failed rotation whose temp may
 * exceed its original, older unflagged names. No content parser is the oracle.
 * Dispatch reuses a wire operation tag, rather than adding a fuzz budget. */
static void fuzz_retirement(const uint8_t *data, size_t size)
{
    if(!size || data[0]!=(uint8_t)JOURNAL_TARGET) return;
    enum { SURVIVING, MISSING, REUSED, LEGACY, MALFORMED, ASIDE, LIVE, OTHER_NAME, MAY_EXCEED, UNFLAGGED, CASES };
    unsigned which=size>1?data[1]%CASES:SURVIVING;
    char root[]="build/journal-fuzz-retire-XXXXXX"; if(!mkdtemp(root)) return;
    DIR *dir=opendir(root); EDIT_ASSERT(dir); int dfd=dirfd(dir);
    int source=openat(dfd,"original",O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600); EDIT_ASSERT(source>=0);
    struct stat st; EDIT_ASSERT(fstat(source,&st)==0); close(source);
    char temp[sizeof ((struct dirent *)0)->d_name];
    /* (P) ASCII basenames in hex: original=6f726967696e616c, other=6f74686572. */
    int n=snprintf(temp,sizeof temp,".sublimite-journal-%sfrom-%jx-%jx-%jx-%jx-%jx-%zx-0-%s%s",
        which==MAY_EXCEED?"!":which==UNFLAGGED?"":"+",(uintmax_t)st.st_dev,(uintmax_t)st.st_ino,
        (uintmax_t)st.st_ctim.tv_sec^(which==REUSED?UINTMAX_C(1):UINTMAX_C(0)),
        (uintmax_t)st.st_ctim.tv_nsec,(uintmax_t)getpid(),size,
        which==OTHER_NAME?"6f74686572":"6f726967696e616c",which==ASIDE?".set-aside-0":"");
    EDIT_ASSERT(n>0 && (size_t)n<sizeof temp);
    if(which==LEGACY) strcpy(temp,".sublimite-journal-a-b-c");
    if(which==MALFORMED) {
        /* (P) Each spelling violates a distinct grammar rule: empty field,
         * overflow, missing component, extra component, trailing suffix,
         * signed field, or invalid set-aside stamp. uintmax_t is uint64_t
         * on the supported Linux target; the overflow literal has an extra
         * hex digit beyond that type's all-ones representation. */
        static const char *bad[]={".sublimite-journal--b-c", ".sublimite-journal-fffffffffffffffff-b-c",
            ".sublimite-journal-a-b", ".sublimite-journal-a-b-c-d", ".sublimite-journal-a-b-c!",
            ".sublimite-journal-+a-b-c", ".sublimite-journal-a-b-c.set-aside-x",
            /* (P) Invalid owner bytes: NUL=00, slash=2f, dot=2e; odd hex,
             * empty and uppercase spellings violate the encoder grammar. */
            ".sublimite-journal-from-0-0-0-0-0-0-0-00", ".sublimite-journal-from-0-0-0-0-0-0-0-2f",
            ".sublimite-journal-from-0-0-0-0-0-0-0-2e", ".sublimite-journal-from-0-0-0-0-0-0-0-2e2e",
            ".sublimite-journal-from-0-0-0-0-0-0-0-f", ".sublimite-journal-from-0-0-0-0-0-0-0-",
            ".sublimite-journal-from-0-0-0-0-0-0-0-6F"};
        strcpy(temp,bad[size>2?data[2]%(sizeof bad/sizeof *bad):0]);
    }
    if(which==MISSING) EDIT_ASSERT(unlinkat(dfd,"original",0)==0);
    int fd=openat(dfd,temp,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600); EDIT_ASSERT(fd>=0);
    EDIT_ASSERT(write(fd,data,size)==(ssize_t)size); struct stat before; EDIT_ASSERT(fstat(fd,&before)==0);
    if(which==LIVE) EDIT_ASSERT(flock(fd,LOCK_EX|LOCK_NB)==0);
    EDIT_ASSERT(journal_retire_directory(root,NULL)==0);
    bool keep=which==MALFORMED || which==ASIDE || which==LIVE;
    EDIT_ASSERT((fstatat(dfd,temp,&st,AT_SYMLINK_NOFOLLOW)==0)==keep);
    close(fd);
    bool preserve=which==MISSING || which==REUSED || which==LEGACY || which==OTHER_NAME || which==MAY_EXCEED || which==UNFLAGGED || keep;
    size_t copies=0; struct dirent *entry; rewinddir(dir);
    while((entry=readdir(dir))) {
        EDIT_ASSERT(strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-"))!=0);
        if(!strncmp(entry->d_name,".sublimite-journal-",strlen(".sublimite-journal-"))) {
            EDIT_ASSERT(fstatat(dfd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)==0);
            EDIT_ASSERT(st.st_ino==before.st_ino && st.st_size==before.st_size); copies++;
        }
    }
    EDIT_ASSERT(copies==(preserve?1u:0u));
    rewinddir(dir);
    while((entry=readdir(dir))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) EDIT_ASSERT(unlinkat(dfd,entry->d_name,0)==0);
    closedir(dir); EDIT_ASSERT(rmdir(root)==0);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_retirement(data,size);
    fuzz_groups(data,size);
    no_visit(data,size);
    structured_bytes(data,size);
    fuzz_sessions(data,size);
    fuzz_slices(data,size);
    if(!size || (data[0]&31u)!=0) return 0;
    char path[]="/tmp/journal-fuzz-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 0; close(fd);
    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return 0; }
    fuzz_io faults={.write_error=(data[0]&64u)!=0,.sync_error=(data[0]&128u)!=0};
    const int errors[]={0,EAGAIN,ENOSPC,EIO}; faults.append_error=errors[size>1?data[1]%4:0];
    journal_io io={.ctx=&faults,.write=fuzz_write,.sync=fuzz_sync,.append_write=fuzz_append};
    journal *j=NULL; if(journal_open_with_io(&j,path,&pool,NULL,&io)) { work_pool_shutdown(&pool); unlink(path); return 0; }
    fuzz_model model={0}; journal_record operations[64]; uint8_t payloads[64][24]; size_t count=0, boundaries[65]={0};
    for(size_t i=1;i+2<size && count<64;i+=3) {
        bool del=model.size && (data[i]&1u);
        size_t off=data[i+1]%(del?model.size:model.size+1), n=del?1:1+data[i+2]%16;
        uint8_t *p=payloads[count]; memset(p,0,24);
        for(unsigned k=0;k<8;k++) p[k]=(uint8_t)((uint64_t)off>>(8*k));
        if(del) p[8]=1; else memset(p+8,data[i+2],n);
        journal_record r={(uint32_t)(del?JOURNAL_DELETE:JOURNAL_INSERT),1,count+1,p,del?16:n+8};
        int append_rc=journal_append(j,r.type,r.buffer_id,r.data,r.size);
        EDIT_ASSERT(append_rc==0 || append_rc==JOURNAL_IO);
        EDIT_ASSERT(apply(&model,&r)==0); operations[count]=r;
        boundaries[count+1]=boundaries[count]+JOURNAL_HEADER+r.size; count++;
        EDIT_ASSERT(journal_get_stats(j).accepted_sequence==count);
        if(append_rc==JOURNAL_IO) break; /* retained op, sticky suspension forbids later offsets */
        /* Before pump/flush, every successful wire record must already replay. */
        uint8_t cached[8192]; fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
        ssize_t cache_size=read(fd,cached,sizeof cached); close(fd); EDIT_ASSERT(cache_size>=0);
        fuzz_model prefix={0}; journal_replay_result cr;
        EDIT_ASSERT(journal_replay_bytes(cached,(size_t)cache_size,apply,&prefix,&cr)==0 && !cr.corrupt);
        EDIT_ASSERT(cr.last_sequence==count && prefix.edits==count && prefix.size==model.size && !memcmp(prefix.bytes,model.bytes,model.size));
    }
    int flush_rc=journal_flush(j);
    for(unsigned retries=0;flush_rc==JOURNAL_IO && retries<2;retries++) {
        EDIT_ASSERT(journal_get_stats(j).accepted_sequence==count);
        int retry_rc=journal_retry(j);
        if(retry_rc==JOURNAL_IO) {
            faults.sync_error=false;
            EDIT_ASSERT(journal_rotate(j,operations,count)==0);
        } else EDIT_ASSERT(retry_rc==0);
        flush_rc=journal_flush(j);
    }
    EDIT_ASSERT(flush_rc==0 && journal_get_stats(j).durable_sequence==count); journal_close(j);
    uint8_t bytes[8192]; fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
    ssize_t got=read(fd,bytes,sizeof bytes); EDIT_ASSERT(got>0 || count==0); close(fd);
    piece_allocator alloc=piece_default_allocator(); fuzz_model actual={.tree=piece_create(&alloc)};
    EDIT_ASSERT(actual.tree!=NULL);
    journal_replay_result rr; EDIT_ASSERT(journal_replay_bytes(bytes,(size_t)got,apply,&actual,&rr)==0);
    EDIT_ASSERT(!rr.corrupt && model.size==actual.size && model.edits==actual.edits && memcmp(model.bytes,actual.bytes,actual.size)==0);
    uint8_t text[4096]; EDIT_ASSERT(piece_len(actual.tree)==actual.size && piece_read(actual.tree,0,text,actual.size)==0 && memcmp(text,actual.bytes,actual.size)==0);
    piece_destroy(actual.tree);
    if(got>0 && size>3) {
        size_t changed=((size_t)data[1]*256+data[2])%(size_t)got;
        size_t expected_count=0;
        while(expected_count<count && boundaries[expected_count+1]<=changed) expected_count++;
        bytes[changed]^=(uint8_t)(1u<<(data[3]&7u));
        no_visit(bytes,(size_t)got);
        fuzz_model broken={0}, expected={0};
        EDIT_ASSERT(journal_replay_bytes(bytes,(size_t)got,apply,&broken,&rr)==0);
        EDIT_ASSERT(rr.corrupt && rr.valid_bytes==boundaries[expected_count] &&
                    rr.last_sequence==expected_count && rr.records==expected_count && broken.edits==expected_count);
        for(size_t i=0;i<expected_count;i++) EDIT_ASSERT(apply(&expected,&operations[i])==0);
        EDIT_ASSERT(broken.size==expected.size && memcmp(broken.bytes,expected.bytes,broken.size)==0);
        bytes[changed]^=(uint8_t)(1u<<(data[3]&7u));
        size_t cut=changed, completed=0;
        while(completed<count && boundaries[completed+1]<=cut) completed++;
        expected=(fuzz_model){0}; broken=(fuzz_model){0};
        EDIT_ASSERT(journal_replay_bytes(bytes,cut,apply,&broken,&rr)==0);
        EDIT_ASSERT(rr.valid_bytes==boundaries[completed] && rr.last_sequence==completed && rr.records==completed);
        for(size_t i=0;i<completed;i++) EDIT_ASSERT(apply(&expected,&operations[i])==0);
        EDIT_ASSERT(broken.edits==completed && broken.size==expected.size && !memcmp(broken.bytes,expected.bytes,broken.size));
    }
    work_pool_shutdown(&pool); unlink(path); return 0;
}
