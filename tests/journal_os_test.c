/* Portable journal integration on real files (NTFS under Windows %TEMP%).
 * (P) Counts/sizes below derive from literal payloads, wire fields and existing
 * journal constants. Hang guards reuse the journal's existing sync interval;
 * they are test escapes, not latency claims. */
#include "journal/journal.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define JOURNAL_TEST_SANITIZER 1
#else
#define JOURNAL_TEST_SANITIZER 0
#endif
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_os_test:%d FAIL %s (native=%d)\n",__LINE__,#c,os_native_error()); return 1; } } while(0)
#define O(c) T((c)==OS_OK)
typedef struct image { uint8_t bytes[JOURNAL_PAGE]; size_t len; uint64_t paint; } image;
static uint64_t get64(const uint8_t *p)
{ uint64_t v=0; for(size_t i=0;i<sizeof v;i++) v|=(uint64_t)p[i]<<(i*8u); return v; }
static int visit(void *ctx, const journal_record *r)
{
    image *m=ctx;
    if(r->type==JOURNAL_PAINT) m->paint=r->buffer_id;
    if(r->type!=JOURNAL_INSERT && r->type!=JOURNAL_DELETE) return 0;
    uint64_t off=get64(r->data); if(off>m->len) return 1;
    if(r->type==JOURNAL_INSERT) {
        size_t n=r->size-sizeof(uint64_t); if(n>sizeof m->bytes-m->len) return 1;
        memmove(m->bytes+(size_t)off+n,m->bytes+(size_t)off,m->len-(size_t)off);
        memcpy(m->bytes+(size_t)off,r->data+sizeof(uint64_t),n); m->len+=n;
    } else {
        uint64_t n=get64(r->data+sizeof(uint64_t)); if(n>m->len-(size_t)off) return 1;
        memmove(m->bytes+(size_t)off,m->bytes+(size_t)(off+n),m->len-(size_t)(off+n)); m->len-=(size_t)n;
    }
    return 0;
}
static int join(char *out, size_t cap, const char *dir, const char *leaf)
{ int n=snprintf(out,cap,"%s/%s",dir,leaf); return n<0 || (size_t)n>=cap; }
static int create(const char *path, const char *bytes)
{
    os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL));
    size_t n; O(os_file_write(&f,bytes,strlen(bytes),&n)); T(n==strlen(bytes)); os_file_close(&f); return 0;
}
static int expect(const char *path, const char *text, bool readonly)
{
    image m={0}; journal_replay_result rr;
    T((readonly?journal_scan_file(path,visit,&m,&rr):journal_replay_file(path,visit,&m,&rr))==0);
    T(!rr.corrupt && m.len==strlen(text) && !memcmp(m.bytes,text,m.len)); return 0;
}
static int transport(const char *dir)
{
    char path[OS_PATH_MAX]; T(!join(path,sizeof path,dir,"transport"));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,path,&pool,NULL)==0);
    journal *rejected=NULL; T(journal_open(&rejected,dir,&pool,NULL)==JOURNAL_INVALID && !rejected);
    FILE *output=tmpfile(); T(output);
    T(journal_manual_replay(path,dir,1,false,output)==JOURNAL_INVALID); T(fclose(output)==0);
    T(JOURNAL_TEST_SANITIZER || edit_malloc_guard_active());
    if(edit_malloc_guard_active()) {
        void *(*volatile alloc)(size_t)=malloc;
        edit_malloc_guard_begin(); void *probe=alloc(JOURNAL_HEADER);
        size_t seen=edit_malloc_guard_end(); free(probe); T(seen>=1);
    }
    edit_malloc_guard_begin();
    int rc=0; const char text[]="typing";
    for(size_t i=0;i<sizeof text-1u;i++) if(journal_insert(j,1,i,(const uint8_t *)text+i,1)) rc=1;
    size_t allocations=edit_malloc_guard_end(); T(!rc && !allocations);
    printf("journal_os_test: malloc_guard=%s append_allocations=%zu\n",edit_malloc_guard_active()?"active":"sanitizer-inactive",allocations);
    T(expect(path,text,true)==0); T(journal_get_stats(j).durable_sequence==0);
    T(journal_flush(j)==0);
    os_file old=OS_FILE_INIT; O(os_file_open(&old,path,OS_OPEN_READ));
    uint8_t insert[sizeof(uint64_t)+sizeof "checkpoint"-1u]={0}; memcpy(insert+sizeof(uint64_t),"checkpoint",sizeof "checkpoint"-1u);
    journal_record checkpoint={.type=JOURNAL_INSERT,.buffer_id=1,.data=insert,.size=sizeof insert};
    os_dir guard=OS_DIR_INIT; O(os_dir_open(&guard,dir)); O(os_dir_try_lock(&guard,OS_LOCK_EXCLUSIVE));
    T(journal_rotate(j,&checkpoint,1)==JOURNAL_BUSY); T(expect(path,text,true)==0);
    os_dir_unlock(&guard); T(journal_rotate(j,&checkpoint,1)==0);
    T(expect(path,"checkpoint",true)==0);
    os_stat st; O(os_file_stat(&old,&st)); T(st.size>=JOURNAL_HEADER+sizeof(uint64_t)+1u);
    uint8_t first[JOURNAL_HEADER+sizeof(uint64_t)+1u]; size_t got;
    O(os_file_pread(&old,first,sizeof first,0,&got)); T(got==sizeof first && first[sizeof first-1u]=='t');
    os_file_close(&old); os_dir_close(&guard); journal_close(j); work_pool_shutdown(&pool);
    O(os_unlink(path)); puts("journal_os_test: page cache, directory contention, rotation and open generations passed"); return 0;
}
static int counting_append(const char *dir)
{
    char path[OS_PATH_MAX]; T(!join(path,sizeof path,dir,"counting"));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,path,&pool,NULL)==0);
    /* (P) Reuse the existing journal_test counting campaign, unchanged. */
    edit_malloc_guard_begin(); int rc=0;
    for(unsigned i=0;i<10000;i++) if(journal_insert(j,1,0,(const uint8_t *)"x",1)) rc=1;
    size_t allocations=edit_malloc_guard_end(); T(!rc && !allocations);
    T(journal_get_stats(j).accepted_sequence==10000); T(journal_flush(j)==0);
    journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    printf("journal_os_test: guarded_appends=10000 append_allocations=%zu\n",allocations); return 0;
}
typedef struct fault { os_file handle; int error; size_t short_bytes; unsigned attempts; bool zero; } fault;
static ssize_t append_hook(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    (void)fd; fault *f=ctx; f->attempts++;
    if(f->error) { errno=f->error; return -1; }
    if(f->zero) return 0;
    if(f->short_bytes && n>f->short_bytes) n=f->short_bytes;
    size_t done=0;
    if(os_file_pwrite_once(&f->handle,p,n,off,&done)!=OS_OK) { errno=EIO; return -1; }
    return (ssize_t)done;
}
static int append_errors(const char *dir)
{
    const int errors[]={EAGAIN,ENOSPC,EIO,EINTR,0,0};
    for(size_t i=0;i<sizeof errors/sizeof errors[0];i++) {
        char path[OS_PATH_MAX]; T(!join(path,sizeof path,dir,"errors"));
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
        T(journal_open(&j,path,&pool,NULL)==0);
        fault f={.handle=OS_FILE_INIT,.error=errors[i],.zero=i==4,.short_bytes=i==5?JOURNAL_HEADER:0};
        O(os_file_open(&f.handle,path,OS_OPEN_RDWR));
        journal_io io={.ctx=&f,.append_write=append_hook}; T(journal_set_io(j,&io)==0);
        edit_malloc_guard_begin(); int rc=journal_insert(j,1,0,(const uint8_t *)"x",1);
        size_t allocations=edit_malloc_guard_end();
        T(rc==JOURNAL_IO && allocations==0 && f.attempts==1);
        T(journal_get_stats(j).append_errno==(errors[i]?errors[i]:EIO));
        T(journal_insert(j,1,1,(const uint8_t *)"y",1)==JOURNAL_IO && f.attempts==1);
        T(journal_set_io(j,NULL)==0); T(journal_flush(j)==JOURNAL_IO); T(journal_retry(j)==0);
        T(journal_flush(j)==0); T(expect(path,"x",true)==0);
        os_file_close(&f.handle); journal_close(j); work_pool_shutdown(&pool); O(os_unlink(path));
    }
    puts("journal_os_test: EINTR/errors/zero/short writes are single-attempt sticky IO without allocations"); return 0;
}
static int leases(const char *dir)
{
    char path[OS_PATH_MAX], lock[OS_PATH_MAX]; os_file lease=OS_FILE_INIT, duplicate=OS_FILE_INIT, other=OS_FILE_INIT;
    T(journal_session_create_file(dir,path,sizeof path,&lease)==0);
    const char *leaf=strrchr(path,'/'); T(leaf);
    int n=snprintf(lock,sizeof lock,"%s/.%s.lock",dir,leaf+1); T(n>0 && (size_t)n<sizeof lock);
    O(os_file_dup(&duplicate,&lease)); os_file_close(&lease);
    O(os_file_open(&other,lock,OS_OPEN_RDWR)); T(os_file_try_lock(&other,OS_LOCK_EXCLUSIVE)==OS_E_BUSY);
    T(journal_session_release_file(path,&duplicate)==JOURNAL_BUSY);
    T(journal_retire_directory(dir,NULL)==0); os_stat st; O(os_stat_path(path,0,&st));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    journal_close_discard_empty(j); work_pool_shutdown(&pool);
    T(journal_session_release_file(path,&duplicate)==0); T(os_stat_path(lock,0,&st)==OS_E_NOENT);
    os_file_close(&other); os_file_close(&duplicate);
    puts("journal_os_test: session lease dup lifetime, live retirement pin and empty release passed"); return 0;
}
static int retention_and_manual(const char *dir)
{
    char path[OS_PATH_MAX], source[OS_PATH_MAX]; T(!join(path,sizeof path,dir,"retain-log")); T(!join(source,sizeof source,dir,"original"));
    T(create(source,"base")==0); journal_base base; T(journal_capture_base(source,&base)==0);
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    T(journal_capture_base(dir,&base)==JOURNAL_BASE_CHANGED); T(journal_capture_base(source,&base)==0);
    journal_retain retained={0}; work_handle h; T(journal_retain_start(j,&retained,&base,&h)==0);
    uint64_t end=os_now_ns()+UINT64_C(10000000000); /* tests/os_test.c HANG_GUARD_NS */
    while(!work_handle_finished(&pool,h)) { T(os_now_ns()<end); os_sleep_ns(1000000); }
    T(retained.error==0); T(journal_check_base(&retained.copy)==0);
    os_file f=OS_FILE_INIT; O(os_file_open(&f,retained.path,OS_OPEN_READ));
    char bytes[sizeof "base"-1u]; size_t got; O(os_file_read(&f,bytes,sizeof bytes,&got)); T(got==sizeof bytes && !memcmp(bytes,"base",sizeof bytes)); os_file_close(&f);
    T(journal_set_base(j,1,&base)==0); T(journal_insert(j,1,strlen("base"),(const uint8_t *)"!",1)==0);
    T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool);
    FILE *output=tmpfile(); T(output); T(journal_manual_replay(path,source,1,false,output)==0);
    T(journal_manual_replay(path,source,1,true,output)==0); T(fclose(output)==0);
    O(os_file_open(&f,source,OS_OPEN_READ)); char replaced[sizeof "base!"-1u];
    O(os_file_read(&f,replaced,sizeof replaced,&got)); T(got==sizeof replaced && !memcmp(replaced,"base!",sizeof replaced)); os_file_close(&f);
    T(journal_check_base(&base)==JOURNAL_BASE_CHANGED);
    O(os_unlink(retained.path)); O(os_unlink(source)); O(os_unlink(path));
    puts("journal_os_test: retained clone/copy, BASE conflict and durable manual replay passed"); return 0;
}
static int sweep_and_ack(const char *dir)
{
    char path[OS_PATH_MAX], tmp[OS_PATH_MAX], aside[OS_PATH_MAX];
    T(!join(path,sizeof path,dir,"session-source")); work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,path,&pool,NULL)==0); T(journal_insert(j,1,0,(const uint8_t *)"saved",strlen("saved"))==0); T(journal_flush(j)==0);
    os_stat st; O(os_stat_path(path,0,&st));
    /* (P) Literal owner is hex("session-source"); source generation is held metadata. */
    char leaf[OS_NAME_MAX]; int n=snprintf(leaf,sizeof leaf,".sublimite-journal-+from-%jx-%jx-%jx-%jx-1-1-0-73657373696f6e2d736f75726365",
        (uintmax_t)st.id.volume,(uintmax_t)st.id.index[0],(uintmax_t)(st.ctime_ns/1000000000),(uintmax_t)(st.ctime_ns%1000000000));
    T(n>0 && (size_t)n<sizeof leaf); T(!join(tmp,sizeof tmp,dir,leaf)); T(create(tmp,"incomplete")==0);
    T(journal_retire_directory(dir,path)==0); T(os_stat_path(tmp,0,&st)==OS_E_NOENT);
    T(!join(tmp,sizeof tmp,dir,".sublimite-copy-abcdef")); T(create(tmp,"partial")==0);
    T(journal_retire_directory(dir,path)==0); T(os_stat_path(tmp,0,&st)==OS_E_NOENT);
    journal_close(j); work_pool_shutdown(&pool);
    T(!join(aside,sizeof aside,dir,".sublimite-journal-1-1-0.set-aside-1")); O(os_rename(path,aside,OS_RENAME_NOREPLACE));
    T(journal_acknowledge(aside)==0); T(os_stat_path(aside,0,&st)==OS_E_NOENT);
    puts("journal_os_test: clean rotation sweep, dead staging copy and pinned-file acknowledgment passed"); return 0;
}
static int cleanup(const char *dir)
{
    os_dir d=OS_DIR_INIT; O(os_dir_open(&d,dir)); os_dir_iter it; O(os_dir_iter_open(&it,&d));
    const char *name; os_err e;
    while((e=os_dir_next(&it,&name))==OS_OK && name) O(os_unlink_at(&d,name));
    O(e); os_dir_iter_close(&it); os_dir_close(&d); O(os_rmdir(dir)); return 0;
}
int main(void)
{
    char temp[OS_PATH_MAX], root[OS_PATH_MAX]; O(os_temp_dir(temp,sizeof temp));
    int n=snprintf(root,sizeof root,"%s/sublimite-test-journal-%" PRIu64 "-%" PRIu64,temp,os_proc_id(),os_wall_ns()); T(n>0 && (size_t)n<sizeof root); O(os_mkdir(root));
#if defined(_WIN32)
    for(char *p=root;*p;p++) if(*p=='\\') *p='/';
#endif
    int rc=transport(root);
    if(!rc) rc=counting_append(root);
    if(!rc) rc=append_errors(root);
    if(!rc) rc=leases(root);
    if(!rc) rc=retention_and_manual(root);
    if(!rc) rc=sweep_and_ack(root);
    int cleaned=cleanup(root); T(!rc && !cleaned);
    puts("journal_os_test: all portable journal cases passed"); return 0;
}
