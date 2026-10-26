/* Journal-only recovery oracles, shared by Linux and native Windows/NTFS.
 * White-box OS shims inject transport failures using the real owned handle;
 * production has no syscall indirection. Constants are wire sizes, literal
 * lengths, or the existing journal_test exact-prefix/copy fixtures (P). */
#include "journal/private.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
typedef enum safety_mode { SAFE, SHORT_WRITE, ZERO_WRITE, WRITE_ERROR, DATA_ERROR,
    META_ERROR, DIRECTORY_ERROR, RENAME_ERROR, AMBIGUOUS_RENAME } safety_mode;
typedef struct safety_fault {
    safety_mode mode;
    unsigned writes, syncs, metadata, directories;
    bool published, lose;
    uint8_t durable[131089]; /* inherited multi-chunk retention fixture */
    size_t durable_size;
} safety_fault;
static safety_fault *safety;
static os_err safety_write(os_file *, const void *, size_t, uint64_t, size_t *);
static os_err safety_data(os_file *);
static os_err safety_metadata(os_file *);
static os_err safety_directory(os_dir *);
#define os_file_pwrite safety_write
#define os_file_sync_data safety_data
#define os_file_sync safety_metadata
#define os_dir_sync safety_directory
#include "../src/journal/journal.c"
#undef os_file_pwrite
#undef os_file_sync_data
#undef os_file_sync
#undef os_dir_sync
static os_err safety_write(os_file *f, const void *p, size_t n, uint64_t off, size_t *done)
{
    if(safety) {
        safety->writes++;
        if(safety->mode==ZERO_WRITE) { *done=0; return OS_OK; }
        if(safety->mode==WRITE_ERROR) { *done=0; return OS_E_IO; }
        if(safety->mode==SHORT_WRITE && n>1) n=(n+1u)/2u;
    }
    return os_file_pwrite(f,p,n,off,done);
}
static os_err safety_data(os_file *f)
{
    if(safety) {
        safety->syncs++;
        if(safety->mode==DATA_ERROR) {
            if(safety->lose) (void)os_file_set_size(f,0);
            return OS_E_IO;
        }
    }
    os_err e=os_file_sync_data(f);
    if(safety && !e) {
        size_t got=0;
        e=os_file_pread(f,safety->durable,sizeof safety->durable,0,&got);
        safety->durable_size=got;
    }
    return e;
}
static os_err safety_metadata(os_file *f)
{
    if(safety) { safety->metadata++; if(safety->mode==META_ERROR) return OS_E_IO; }
    return os_file_sync(f);
}
static os_err safety_directory(os_dir *d)
{
    if(safety) {
        safety->directories++;
        if(safety->mode==DIRECTORY_ERROR && safety->published) return OS_E_IO;
    }
    return os_dir_sync(d);
}
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_safety_test:%d FAIL %s native=%d\n",__LINE__,#c,os_native_error()); return 1; } } while(0)
#define O(c) T((c)==OS_OK)
static int path_join(char *out, size_t cap, const char *dir, const char *name)
{ int n=snprintf(out,cap,"%s/%s",dir,name); return n<0 || (size_t)n>=cap; }
static uint64_t ref64(const uint8_t *p)
{ uint64_t v=0; for(size_t i=0;i<8;i++) v+=(uint64_t)p[i]<<(i*8u); return v; }
typedef struct safety_image { uint8_t text[32768]; size_t len; uint64_t paint, save_phase; } safety_image;
static int safety_visit(void *ctx, const journal_record *r)
{
    safety_image *m=ctx;
    if(r->type==JOURNAL_BASE) {
        journal_base b; char path[OS_PATH_MAX];
        if(journal_decode_base(r,&b,path,sizeof path)) return 1;
        m->len=0;
        if(*path) {
            os_file f=OS_FILE_INIT; if(os_file_open(&f,path,OS_OPEN_READ)!=OS_OK) return 1;
            size_t got=0; os_err e=os_file_read(&f,m->text,sizeof m->text,&got); os_file_close(&f);
            if(e!=OS_OK || got!=b.size) return 1;
            m->len=got;
        }
    } else if(r->type==JOURNAL_PAINT) m->paint=r->buffer_id;
    else if(r->type==JOURNAL_SAVE) m->save_phase=r->data[8];
    else if(r->type==JOURNAL_INSERT || r->type==JOURNAL_DELETE) {
        uint64_t off=ref64(r->data); if(off>m->len) return 1;
        if(r->type==JOURNAL_DELETE) {
            uint64_t n=ref64(r->data+8); if(n>m->len-off) return 1;
            memmove(m->text+(size_t)off,m->text+(size_t)(off+n),m->len-(size_t)(off+n)); m->len-=(size_t)n;
        } else {
            size_t n=r->size-8u; if(n>sizeof m->text-m->len) return 1;
            memmove(m->text+(size_t)off+n,m->text+(size_t)off,m->len-(size_t)off);
            memcpy(m->text+(size_t)off,r->data+8,n); m->len+=n;
        }
    }
    return 0;
}
static int safety_expect(const char *path, const char *want, uint64_t paint, uint64_t phase)
{
    safety_image m={0}; journal_replay_result rr;
    T(journal_replay_file(path,safety_visit,&m,&rr)==0 && !rr.corrupt);
    T(m.len==strlen(want) && !memcmp(m.text,want,m.len) && m.paint==paint && m.save_phase==phase); return 0;
}
static int safety_create(const char *path, const char *bytes)
{
    os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL));
    size_t done; O(os_file_write(&f,bytes,strlen(bytes),&done)); T(done==strlen(bytes)); os_file_close(&f); return 0;
}
static int safety_rename(void *ctx, const char *from, const char *to)
{
    safety_fault *f=ctx;
    if(f->mode==RENAME_ERROR) { errno=EIO; return -1; }
    if(os_rename(from,to,OS_RENAME_REPLACE)!=OS_OK) return -1;
    f->published=true;
    if(f->mode==AMBIGUOUS_RENAME) { errno=EIO; return -1; }
    return 0;
}
static void safety_drain(const work_msg *m, void *ctx) { (void)journal_receive(ctx,m); }
static int transport_faults(const char *root)
{
    const safety_mode modes[]={SHORT_WRITE,ZERO_WRITE,WRITE_ERROR,DATA_ERROR};
    for(size_t k=0;k<sizeof modes/sizeof modes[0];k++) {
        char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"transport-fault"));
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
        T(journal_insert(j,1,0,(const uint8_t *)"bytes",5)==0);
        /* Force the worker's existing missing-tail repair path. */
        j->batches[j->current].cached=0;
        safety_fault f={.mode=modes[k]}; safety=&f;
        int rc=journal_flush(j);
        if(modes[k]==SHORT_WRITE) { T(rc==0 && f.writes>1 && f.syncs>0); }
        else {
            T(rc==JOURNAL_IO); f.mode=SAFE;
            if(modes[k]==DATA_ERROR) {
                T(journal_retry(j)==JOURNAL_IO);
                uint8_t p[13]={0}; memcpy(p+8,"bytes",5);
                journal_record cp={JOURNAL_INSERT,1,0,p,sizeof p}; T(journal_rotate(j,&cp,1)==0);
            } else { T(journal_retry(j)==0); T(journal_flush(j)==0); }
        }
        T(safety_expect(path,"bytes",0,0)==0); safety=NULL;
        journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    }
    puts("journal_safety_test: worker short/zero/error writes and data-fence failures passed"); return 0;
}
static int consumed_writeback(const char *root)
{
    char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"writeback"));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    journal_options opt={.sync_interval_ns=UINT64_C(1000000000000)}; /* journal_test cadence fixture */
    T(journal_open(&j,path,&pool,&opt)==0);
    uint8_t text[JOURNAL_PAGE+1u]; memset(text,'a',sizeof text);
    T(journal_insert(j,1,0,text,sizeof text)==0); T(journal_pump(j,journal_get_stats(j).last_sync_ns,false)==0);
    while(!work_handle_finished(&pool,j->handle)) os_yield();
    work_mailbox_drain(&pool,safety_drain,j); T(!j->active && journal_get_stats(j).durable_sequence==0);
    safety_fault f={.mode=DATA_ERROR,.lose=true}; safety=&f;
    T(journal_insert(j,1,sizeof text,(const uint8_t *)"!",1)==0); T(journal_flush(j)==JOURNAL_IO);
    f.mode=SAFE; T(journal_retry(j)==JOURNAL_IO && journal_flush(j)==JOURNAL_IO);
    T(journal_insert(j,1,0,text,1)==JOURNAL_IO);
    uint8_t cpbytes[8u+sizeof text+1u]={0}; memcpy(cpbytes+8,text,sizeof text); cpbytes[sizeof cpbytes-1u]='!';
    journal_record cp={JOURNAL_INSERT,1,0,cpbytes,sizeof cpbytes}; T(journal_rotate(j,&cp,1)==0);
    safety_image m={0}; journal_replay_result rr;
    T(journal_replay_bytes(f.durable,f.durable_size,safety_visit,&m,&rr)==0 && !rr.corrupt);
    T(m.len==sizeof text+1u && !memcmp(m.text,cpbytes+8,m.len));
    T(journal_get_stats(j).durable_sequence==1); safety=NULL;
    journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    puts("journal_safety_test: consumed writeback loss includes a released unsynced batch; fresh checkpoint required"); return 0;
}
static int publication_faults(const char *root)
{
    const safety_mode modes[]={DATA_ERROR,RENAME_ERROR,AMBIGUOUS_RENAME,DIRECTORY_ERROR};
    for(size_t k=0;k<sizeof modes/sizeof modes[0];k++) {
        char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"publication"));
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
        T(journal_insert(j,1,0,(const uint8_t *)"old",3)==0 && journal_flush(j)==0);
        os_file old=OS_FILE_INIT; O(os_file_open(&old,path,OS_OPEN_READ)); os_stat before; O(os_file_stat(&old,&before));
        safety_fault f={.mode=modes[k]}; safety=&f;
        journal_io io={.ctx=&f,.rename=safety_rename}; T(journal_set_io(j,&io)==0);
        uint8_t payload[11]={0}; memcpy(payload+8,"new",3); journal_record cp={JOURNAL_INSERT,1,0,payload,sizeof payload};
        T(journal_rotate(j,&cp,1)==JOURNAL_IO); f.mode=SAFE;
        bool installed=modes[k]==AMBIGUOUS_RENAME || modes[k]==DIRECTORY_ERROR;
        os_stat named,owned; O(os_stat_path(path,0,&named)); O(os_file_stat(&j->disk.fd,&owned));
        T(journal_same_id(&owned,&named)); T(journal_same_id(&before,&named)!=installed);
        T(safety_expect(path,installed?"new":"old",0,0)==0);
        safety_image image={0}; journal_replay_result rr; T(journal_scan_open(old,safety_visit,&image,&rr)==0);
        T(!rr.corrupt && image.len==3 && !memcmp(image.text,"old",3));
        if(installed) T(journal_retry(j)==0 && !j->directory_pending);
        T(journal_insert(j,1,3,(const uint8_t *)"!",1)==0 && journal_flush(j)==0);
        T(safety_expect(path,installed?"new!":"old!",0,0)==0);
        os_file_close(&old); safety=NULL; journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    }
    puts("journal_safety_test: pre-publication/ambiguous rename/directory barrier identity, old-handle bytes and retry passed"); return 0;
}
static int retained_transactions(const char *root)
{
    const safety_mode modes[]={SAFE,SHORT_WRITE,ZERO_WRITE,WRITE_ERROR,META_ERROR,DATA_ERROR,RENAME_ERROR,AMBIGUOUS_RENAME,DIRECTORY_ERROR};
    for(size_t k=0;k<sizeof modes/sizeof modes[0];k++) for(unsigned finish=0;finish<(modes[k]==SAFE?2u:1u);finish++) {
        char path[OS_PATH_MAX], target[OS_PATH_MAX], fresh[OS_PATH_MAX];
        T(!path_join(path,sizeof path,root,"save-log")); T(!path_join(target,sizeof target,root,"target")); T(!path_join(fresh,sizeof fresh,root,"fresh"));
        T(safety_create(target,"base")==0); journal_base base; T(journal_capture_base(target,&base)==0);
        uint8_t data[JOURNAL_BASE_MAX]; size_t size; T(base_data(&base,data,&size)==0);
        uint8_t edit[9]={4,0,0,0,0,0,0,0,'!'};
        journal_record cp[]={{JOURNAL_BASE,1,0,data,size},{JOURNAL_INSERT,1,0,edit,sizeof edit}};
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
        safety_fault f={.mode=modes[k]}; safety=&f; journal_io io={.ctx=&f,.rename=safety_rename}; T(journal_set_io(j,&io)==0);
        journal_save save={0}; int rc=journal_save_prepare(j,1,&base,cp,2,&save);
        bool success=modes[k]==SAFE || modes[k]==SHORT_WRITE;
        T(rc==(success?0:JOURNAL_IO)); f.mode=SAFE;
        if(success) {
            T(save.prepared && f.metadata>0 && f.directories>0);
            T(safety_expect(path,"base!",0,1)==0);
            journal_base retained; T(journal_capture_base(save.previous_path,&retained)==0 && retained.inode!=base.inode);
            if(finish) {
                T(safety_create(fresh,"base!")==0); O(os_rename(fresh,target,OS_RENAME_REPLACE));
                T(journal_capture_base(target,&base)==0); T(base_data(&base,data,&size)==0); cp[0].size=size;
                edit[0]=5; edit[8]='?';
                /* A failed finish must keep the token and retained BASE. */
                f.mode=RENAME_ERROR; T(journal_save_finish(j,&save,&base,cp,2)==JOURNAL_IO);
                T(save.prepared && journal_check_base(&retained)==0); f.mode=SAFE;
                T(journal_save_finish(j,&save,&base,cp,2)==0 && !save.prepared);
                T(safety_expect(path,"base!?",0,2)==0);
            } else { T(journal_save_cancel(j,&save,cp,2)==0 && !save.prepared); T(safety_expect(path,"base!",0,0)==0); }
        } else if(*save.previous_path) {
            os_stat st;
            if(os_stat_path(save.previous_path,0,&st)==OS_OK) {
                /* Ambiguous prepare retains the only old BASE until cancel's
                 * replacement checkpoint and namespace fence succeed. */
                T(journal_save_cancel(j,&save,cp,2)==0);
            }
        }
        if(*save.previous_path) { os_stat st; T(os_stat_path(save.previous_path,0,&st)==OS_E_NOENT); }
        safety=NULL; journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path)); O(os_unlink(target));
    }
    puts("journal_safety_test: retained BASE prepare/finish/cancel, short/zero/error/full-sync/publication failures and retry passed"); return 0;
}
static int retained_copy_bytes(const char *root)
{
    char path[OS_PATH_MAX], target[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"copy-log")); T(!path_join(target,sizeof target,root,"copy-source"));
    uint8_t expected[131089], actual[sizeof expected]; /* journal_test retained_copy_test = 2 * 65536 + 17 */
    for(size_t i=0;i<sizeof expected;i++) expected[i]=(uint8_t)(i%251u);
    os_file f=OS_FILE_INIT; O(os_file_open(&f,target,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL)); size_t done;
    O(os_file_write(&f,expected,sizeof expected,&done)); T(done==sizeof expected); os_file_close(&f);
    journal_base base; T(journal_capture_base(target,&base)==0); uint8_t data[JOURNAL_BASE_MAX]; size_t size; T(base_data(&base,data,&size)==0);
    journal_record cp={JOURNAL_BASE,1,0,data,size};
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    safety_fault fault={.mode=SHORT_WRITE}; safety=&fault; journal_save save={0};
    T(journal_save_prepare(j,1,&base,&cp,1,&save)==0 && save.prepared && fault.writes>2 && fault.metadata>0);
    O(os_file_open(&f,save.previous_path,OS_OPEN_READ)); O(os_file_read(&f,actual,sizeof actual,&done)); os_file_close(&f);
    T(done==sizeof expected && !memcmp(actual,expected,sizeof expected));
    T(journal_save_cancel(j,&save,&cp,1)==0); safety=NULL;
    journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path)); O(os_unlink(target));
    puts("journal_safety_test: short-write multi-chunk retained save copies every byte and cancellation retires it only after publication"); return 0;
}
/* Independent bitwise CRC32C and little-endian wire construction; no encoder,
 * parser, CRC table, or PAD helper supplies the expected transport. */
static void ref_integer(uint8_t *p, uint64_t v, size_t bytes)
{ for(size_t i=0;i<bytes;i++) { p[i]=(uint8_t)v; v>>=8; } }
static uint32_t ref_crc(const uint8_t *p, size_t n)
{
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n;i++) { crc^=p[i]; for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1u)?UINT32_C(0x82f63b78):0u); }
    return ~crc;
}
static size_t ref_record(uint8_t *out, uint32_t type, uint64_t id, uint64_t sequence, const uint8_t *data, size_t size)
{
    size_t n=32u+size; memset(out,0,n); memcpy(out,"JNL1",4);
    ref_integer(out+4,n,4); ref_integer(out+8,type,4); ref_integer(out+16,sequence,8); ref_integer(out+24,id,8);
    if(size && data) memcpy(out+32,data,size);
    ref_integer(out+12,ref_crc(out,n),4); return n;
}
typedef struct wire_expectation { const journal_record *records; size_t count, next; } wire_expectation;
static int wire_visit(void *ctx, const journal_record *r)
{
    wire_expectation *w=ctx;
    while(w->next<w->count && (w->records[w->next].type==9 || w->records[w->next].type==10)) w->next++;
    if(w->next==w->count) return 1;
    const journal_record *want=&w->records[w->next++];
    return r->sequence!=w->next || r->type!=want->type || r->buffer_id!=want->buffer_id || r->size!=want->size || memcmp(r->data,want->data,r->size);
}
static int wire_fixtures(const char *root, bool negative)
{
    T(ref_crc((const uint8_t *)"123456789",9)==UINT32_C(0xe3069283)); /* CRC32C published check vector */
    static const uint8_t empty[41]={0};
#if defined(_WIN32)
    static const uint8_t named[59]={[0]=1,[8]=2,[16]=3,[24]=4,[32]=5,[36]=1,[39]=0x80,
        [40]='C',[41]=':',[42]='/',[43]='f',[44]='i',[45]='x',[46]='t',[47]='u',[48]='r',[49]='e',[51]=6};
#else
    static const uint8_t named[57]={[0]=1,[8]=2,[16]=3,[24]=4,[32]=5,[36]=1,[39]=0x80,
        [40]='/',[41]='f',[42]='i',[43]='x',[44]='t',[45]='u',[46]='r',[47]='e',[49]=6};
#endif
    static const uint8_t view[32]={[0]=79,[8]=13,[16]=3,[17]=16,[24]=27};
    static const uint8_t full_view[64]={[0]=79,[8]=13,[16]=3,[17]=16,[24]=27,[32]=19,[40]=29,[48]=39,[56]=7};
    static const uint8_t tabs[40]={[0]=3,[8]=2,[16]=1,[24]=19,[32]=15,[33]=1};
    static const uint8_t window[12]={0x5d,5,0,0,0x8f,3,0,0,1,0,0,0};
    static const uint8_t save[57]={[0]=2,[8]=1}, insert[9]={[8]='z'}, marker[1]={0};
    journal_record records[]={
        {1,1,0,empty,sizeof empty},{1,19,0,named,sizeof named},
        {4,1,0,view,sizeof view},{4,19,0,full_view,sizeof full_view},{5,0,0,tabs,sizeof tabs},
        {6,0,0,window,8},{6,0,0,window,sizeof window},{7,1,0,save,sizeof save},
        {11,19,0,marker,0},{9,0,0,marker,0},{2,1,0,insert,sizeof insert},{10,0,0,marker,0},{11,0,0,marker,0}};
    size_t count=sizeof records/sizeof records[0], used=0; uint8_t expected[JOURNAL_PAGE]={0}, actual[JOURNAL_PAGE];
    for(size_t i=0;i<count;i++) used+=ref_record(expected+used,records[i].type,records[i].buffer_id,i+1u,records[i].data,records[i].size);
    T(used+32u<=sizeof expected); (void)ref_record(expected+used,0,0,0,NULL,sizeof expected-used-32u);
    if(negative) expected[12]^=1;
    journal_replay_result rr; wire_expectation w={records,count,0};
    T(journal_replay_bytes(expected,sizeof expected,wire_visit,&w,&rr)==0 && !rr.corrupt && w.next==count && rr.records==count);
    char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"wire")); work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,path,&pool,NULL)==0); T(journal_rotate(j,records,count)==0);
    os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_READ)); size_t got; O(os_file_read(&f,actual,sizeof actual,&got));
    T(got==sizeof expected && !memcmp(actual,expected,sizeof expected)); os_file_close(&f);
    journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    puts("journal_safety_test: fixed legacy/current BASE, SAVE, VIEW, TABS, WINDOW, group and PAINT wire/CRC/PAD fixtures passed"); return 0;
}
static int torn_transports(const char *root)
{
    /* Exact-prefix fixture inherited from journal_test: 17000-byte edit. */
    uint8_t bytes[32768], large[8u+17000u]={0}; memset(large+8,'x',sizeof large-8u);
    uint8_t before[11]={0}; memcpy(before+8,"old",3); uint8_t del[16]={[8]=3};
    size_t prefix=ref_record(bytes,2,1,1,before,sizeof before), used=prefix;
    used+=ref_record(bytes+used,9,0,2,NULL,0); used+=ref_record(bytes+used,3,1,3,del,sizeof del);
    used+=ref_record(bytes+used,2,1,4,large,sizeof large); size_t commit=used;
    used+=ref_record(bytes+used,10,0,5,NULL,0); size_t end=used;
    for(size_t cut=0;cut<=end;cut++) {
        safety_image m={0}; journal_replay_result rr; T(journal_replay_bytes(bytes,cut,safety_visit,&m,&rr)==0);
        size_t boundary=cut<prefix?0:cut<end?prefix:end;
        T(rr.valid_bytes==boundary && rr.corrupt==(boundary!=cut));
        T(m.len==(cut<prefix?0u:cut<end?3u:17000u));
        if(m.len) T(!memcmp(m.text,cut<end?(const uint8_t *)"old":large+8,m.len));
    }
    char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"torn"));
    for(size_t changed=commit;changed<end;changed++) {
        bytes[changed]^=1;
        os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_TRUNC)); size_t done;
        O(os_file_write(&f,bytes,end,&done)); T(done==end); os_file_close(&f);
        safety_image m={0}; journal_replay_result rr; T(journal_replay_file(path,safety_visit,&m,&rr)==0);
        os_stat st; O(os_stat_path(path,0,&st));
        T(rr.corrupt && rr.valid_bytes==prefix && (uint64_t)st.size==prefix && m.len==3 && !memcmp(m.text,"old",3));
        bytes[changed]^=1;
    }
    O(os_unlink(path));
    puts("journal_safety_test: every byte cut of a large grouped edit and every damaged COMMIT byte restores/truncates exactly the pre-group prefix"); return 0;
}
static int paint_checkpoints(const char *root)
{
    char path[OS_PATH_MAX]; T(!path_join(path,sizeof path,root,"paint"));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    T(journal_insert(j,1,0,(const uint8_t *)"text",4)==0);
    T(journal_append(j,JOURNAL_PAINT,19,NULL,0)==0 && journal_flush(j)==0);
    T(safety_expect(path,"text",19,0)==0);
    uint8_t p[12]={0}, marker[1]={0}; memcpy(p+8,"text",4);
    journal_record cp[]={{JOURNAL_INSERT,1,0,p,sizeof p},{JOURNAL_PAINT,19,0,marker,0}};
    T(journal_rotate(j,cp,2)==0 && safety_expect(path,"text",19,0)==0);
    T(journal_append(j,JOURNAL_PAINT,0,NULL,0)==0 && journal_flush(j)==0);
    T(safety_expect(path,"text",0,0)==0); cp[1].buffer_id=0;
    T(journal_rotate(j,cp,2)==0 && safety_expect(path,"text",0,0)==0);
    /* Every torn byte of a clear marker preserves the previous guard/text. */
    cp[1].buffer_id=19; T(journal_rotate(j,cp,2)==0);
    uint64_t boundary=j->batches[j->current].offset; /* next marker follows the sealed checkpoint page */
    T(boundary==JOURNAL_PAGE); journal_close(j); work_pool_shutdown(&pool);
    uint8_t record[32]; ref_record(record,11,0,3,NULL,0);
    for(size_t cut=0;cut<sizeof record;cut++) {
        os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_RDWR)); O(os_file_set_size(&f,boundary));
        size_t done; O(os_file_pwrite(&f,record,cut,boundary,&done)); T(done==cut); os_file_close(&f);
        safety_image m={0}; journal_replay_result rr; T(journal_replay_file(path,safety_visit,&m,&rr)==0);
        T(m.paint==19 && m.len==4 && !memcmp(m.text,"text",4) && rr.valid_bytes==boundary && rr.corrupt==(cut!=0));
    }
    O(os_unlink(path));
    puts("journal_safety_test: PAINT append/clear/last-wins/checkpoint/torn-clear preserves text and pending guard passed"); return 0;
}
int main(int argc, char **argv)
{
    bool negative=argc==2 && !strcmp(argv[1],"--negative-control"); T(argc==1 || negative);
    char temp[OS_PATH_MAX], root[OS_PATH_MAX]; O(os_temp_dir(temp,sizeof temp));
    int n=snprintf(root,sizeof root,"%s/sublimite-test-journal-safety-%" PRIu64 "-%" PRIu64,temp,os_proc_id(),os_wall_ns()); T(n>0 && (size_t)n<sizeof root);
    journal_slashes(root); O(os_mkdir(root));
    int rc=wire_fixtures(root,negative);
    if(!rc) rc=transport_faults(root);
    if(!rc) rc=consumed_writeback(root);
    if(!rc) rc=publication_faults(root);
    if(!rc) rc=retained_transactions(root);
    if(!rc) rc=retained_copy_bytes(root);
    if(!rc) rc=torn_transports(root);
    if(!rc) rc=paint_checkpoints(root);
    if(!rc || negative) O(os_rmdir(root));
    if(rc) return rc;
    puts("journal_safety_test: all journal-only safety oracles passed (process/cache/file fence; no power-loss claim)"); return 0;
}
