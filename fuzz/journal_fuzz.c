#include "journal/journal.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

typedef struct fuzz_model { uint8_t bytes[4096]; size_t size, edits; piece_tree *tree; } fuzz_model;
static uint64_t read64(const uint8_t *p)
{ uint64_t v=0; for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
static int apply(void *ctx, const journal_record *r)
{
    fuzz_model *m=ctx;
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
static void no_visit(const uint8_t *data, size_t size)
{
    fuzz_model m={0}; journal_replay_result rr;
    int rc=journal_replay_bytes(data,size,apply,&m,&rr);
    EDIT_ASSERT(rc==0 || rc==JOURNAL_CALLBACK);
    EDIT_ASSERT(rr.valid_bytes<=size && m.size<=sizeof m.bytes);
    /* Deterministic replay of accepted prefix has identical contents. */
    fuzz_model prefix={0}; journal_replay_result pr;
    EDIT_ASSERT(journal_replay_bytes(data,(size_t)rr.valid_bytes,apply,&prefix,&pr)==0);
    EDIT_ASSERT(!pr.corrupt && m.size==prefix.size && memcmp(m.bytes,prefix.bytes,m.size)==0);
}
/* Repair CRC around arbitrary payloads so mutation reaches schema checks,
 * rather than spending all raw-byte coverage on magic/CRC rejection. */
static void structured_bytes(const uint8_t *data, size_t size)
{
    if(!size || size>16384) return;
    uint8_t wire[16416]={0}; size_t n=size-1, total=n+32;
    wire[0]='J'; wire[1]='N'; wire[2]='L'; wire[3]='1';
    for(unsigned k=0;k<4;k++) wire[4+k]=(uint8_t)((uint32_t)total>>(8*k));
    wire[8]=data[0]%8; wire[16]=1;
    wire[24]=(data[0]&16u)?99:(wire[8]==JOURNAL_TABS || wire[8]==JOURNAL_WINDOW?0:1);
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
typedef struct fuzz_io { unsigned writes, syncs; bool write_error, sync_error; } fuzz_io;
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
    if(f->sync_error && f->syncs==1) { errno=EIO; return -1; }
    return fdatasync(fd);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    no_visit(data,size);
    structured_bytes(data,size);
    if(!size || (data[0]&31u)!=0) return 0;
    char path[]="/tmp/journal-fuzz-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 0; close(fd);
    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return 0; }
    fuzz_io faults={.write_error=(data[0]&64u)!=0,.sync_error=(data[0]&128u)!=0};
    journal_io io={.ctx=&faults,.write=fuzz_write,.sync=fuzz_sync};
    journal *j=NULL; if(journal_open_with_io(&j,path,&pool,NULL,&io)) { work_pool_shutdown(&pool); unlink(path); return 0; }
    fuzz_model model={0}; journal_record operations[64]; uint8_t payloads[64][24]; size_t count=0, boundaries[65]={0};
    for(size_t i=1;i+2<size && count<64;i+=3) {
        bool del=model.size && (data[i]&1u);
        size_t off=data[i+1]%(del?model.size:model.size+1), n=del?1:1+data[i+2]%16;
        uint8_t *p=payloads[count]; memset(p,0,24);
        for(unsigned k=0;k<8;k++) p[k]=(uint8_t)((uint64_t)off>>(8*k));
        if(del) p[8]=1; else memset(p+8,data[i+2],n);
        journal_record r={(uint32_t)(del?JOURNAL_DELETE:JOURNAL_INSERT),1,count+1,p,del?16:n+8};
        EDIT_ASSERT(journal_append(j,r.type,r.buffer_id,r.data,r.size)==0);
        EDIT_ASSERT(apply(&model,&r)==0); operations[count]=r;
        boundaries[count+1]=boundaries[count]+JOURNAL_HEADER+r.size; count++;
    }
    int flush_rc=journal_flush(j);
    for(unsigned retries=0;flush_rc==JOURNAL_IO && retries<2;retries++) {
        EDIT_ASSERT(journal_get_stats(j).accepted_sequence==count);
        EDIT_ASSERT(journal_retry(j)==0); flush_rc=journal_flush(j);
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
