/* White-box transport ownership. Real FlushFileBuffers calls run concurrently
 * with one allocation-counted UI append. Positions distinguish independent
 * synchronous file objects from DuplicateHandle aliases. No latency claim. */
#include "journal/private.h"
#include <windows.h>
#include <stdio.h>
static os_file observed=OS_FILE_INIT;
static _Atomic bool contend, entered, released;
static os_err observe_sync(os_file *file);
#define os_file_sync_data observe_sync
#include "../../src/journal/journal.c"
#undef os_file_sync_data
static os_err observe_sync(os_file *file)
{
    observed=*file;
    os_err e=os_file_sync_data(file);
    if(atomic_load(&contend)) {
        atomic_store(&entered,true);
        while(!atomic_load(&released) && e==OS_OK) e=os_file_sync_data(file);
    }
    return e;
}
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_transport_test:%d FAIL %s\n",__LINE__,#c); return 1; } } while(0)
static int native_ready(const char *path)
{
    os_file f=OS_FILE_INIT; T(os_file_open(&f,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    os_file_close(&f); return 0;
}
static int native_holder(const char *path, const char *ready, const char *kind)
{
    if(!strcmp(kind,"lease")) {
        os_file first=OS_FILE_INIT, duplicate=OS_FILE_INIT;
        T(os_file_open(&first,path,OS_OPEN_RDWR)==OS_OK); T(os_file_try_lock(&first,OS_LOCK_EXCLUSIVE)==OS_OK);
        T(os_file_dup(&duplicate,&first)==OS_OK); os_file_close(&first);
        T(native_ready(ready)==0); for(;;) os_sleep_ns(1000000);
    }
    wchar_t wide[OS_PATH_MAX]; T(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,(int)(sizeof wide/sizeof *wide))>0);
    bool pending=!strcmp(kind,"pending");
    HANDLE held=CreateFileW(wide,GENERIC_READ|(pending?DELETE:0),
        FILE_SHARE_READ|FILE_SHARE_WRITE|(pending?FILE_SHARE_DELETE:0),NULL,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,NULL);
    T(held!=INVALID_HANDLE_VALUE);
    if(pending) {
        FILE_DISPOSITION_INFO disposition={.DeleteFile=TRUE};
        T(SetFileInformationByHandle(held,FileDispositionInfo,&disposition,sizeof disposition));
        FILE_STANDARD_INFO information;
        T(GetFileInformationByHandleEx(held,FileStandardInfo,&information,sizeof information) && information.DeletePending);
    }
    T(native_ready(ready)==0);
    for(;;) os_sleep_ns(1000000);
}
static int spawn_holder(const char *path, const char *ready, const char *kind, os_proc *child)
{
    char self[OS_PATH_MAX]; T(os_proc_self_path(self,sizeof self)==OS_OK);
    const char *args[]={self,"--native-holder",path,ready,kind,NULL}; T(os_proc_spawn(child,args)==OS_OK);
    uint64_t end=os_now_ns()+UINT64_C(10000000000); /* os_test hang escape */
    for(;;) {
        os_stat st; if(os_stat_path(ready,0,&st)==OS_OK) return 0;
        T(os_now_ns()<end); os_proc_status status; T(os_proc_wait(child,0,&status)==OS_E_TIMEDOUT); os_sleep_ns(1000000);
    }
}
static int kill_holder(os_proc *child, const char *ready)
{
    T(os_proc_kill(child)==OS_OK); os_proc_status status; T(os_proc_wait(child,-1,&status)==OS_OK && status.how==OS_PROC_ABNORMAL);
    T(os_unlink(ready)==OS_OK); return 0;
}
static int native_states(const char *root)
{
    char path[OS_PATH_MAX], ready[OS_PATH_MAX];
    int n=snprintf(path,sizeof path,"%s/native",root); T(n>0 && (size_t)n<sizeof path);
    n=snprintf(ready,sizeof ready,"%s/ready",root); T(n>0 && (size_t)n<sizeof ready);
    work_pool pool; T(work_pool_init(&pool,1,0)==0);
    const char *kinds[]={"denial","pending"};
    uint8_t payload[11]={0}; memcpy(payload+8,"new",3); journal_record cp={JOURNAL_INSERT,1,0,payload,sizeof payload};
    for(size_t k=0;k<sizeof kinds/sizeof kinds[0];k++) {
        journal *j; T(journal_open(&j,path,&pool,NULL)==0); T(journal_insert(j,1,0,(const uint8_t *)"old",3)==0 && journal_flush(j)==0);
        os_stat original; T(os_file_stat(&j->disk.fd,&original)==OS_OK);
        os_proc child; T(spawn_holder(path,ready,kinds[k],&child)==0);
        int rotated=journal_rotate(j,&cp,1);

        os_stat owned; bool same_generation=os_file_stat(&j->disk.fd,&owned)==OS_OK && journal_same_id(&original,&owned);
        uint8_t old[JOURNAL_HEADER+8u+3u]; size_t got;
        bool old_bytes=os_file_pread(&j->disk.fd,old,sizeof old,0,&got)==OS_OK && got==sizeof old && !memcmp(old+JOURNAL_HEADER+8u,"old",3);
        os_stat named; bool same_name=os_stat_path(path,0,&named)==OS_OK && journal_same_id(&original,&named);
        T(kill_holder(&child,ready)==0);
        T(rotated==JOURNAL_IO && same_generation && old_bytes);
        if(k==0) T(same_name);
        if(k==0) { T(journal_rotate(j,&cp,1)==0); journal_close(j); T(os_unlink(path)==OS_OK); }
        else {
            /* Legacy delete-pending persists until all original handles close. */
            T(journal_rotate(j,&cp,1)==JOURNAL_IO); journal_close(j);
            T(journal_open(&j,path,&pool,NULL)==0); T(journal_rotate(j,&cp,1)==0); journal_close(j); T(os_unlink(path)==OS_OK);
        }
    }
    os_file f=OS_FILE_INIT; T(os_file_open(&f,path,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    os_proc child; T(spawn_holder(path,ready,"lease",&child)==0);
    T(os_file_try_lock(&f,OS_LOCK_EXCLUSIVE)==OS_E_BUSY); T(kill_holder(&child,ready)==0);
    T(os_file_try_lock(&f,OS_LOCK_EXCLUSIVE)==OS_OK); os_file_close(&f); T(os_unlink(path)==OS_OK);
    work_pool_shutdown(&pool);
    puts("journal_transport_test: separate-process sharing denial, legacy delete-pending, old-generation bytes/retry and last-duplicate death lock release passed"); return 0;
}
int main(int argc, char **argv)
{
    if(argc==5 && !strcmp(argv[1],"--native-holder")) return native_holder(argv[2],argv[3],argv[4]);
    T(argc==1);
    char temp[OS_PATH_MAX], root[OS_PATH_MAX], path[OS_PATH_MAX];
    T(os_temp_dir(temp,sizeof temp)==OS_OK);
    int n=snprintf(root,sizeof root,"%s/sublimite-test-transport-%llu",temp,(unsigned long long)os_proc_id()); T(n>0 && (size_t)n<sizeof root);
    T(os_mkdir(root)==OS_OK); n=snprintf(path,sizeof path,"%s/journal",root); T(n>0 && (size_t)n<sizeof path);
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,path,&pool,NULL)==0); T(journal_insert(j,1,0,(const uint8_t *)"a",1)==0); T(journal_flush(j)==0);
    T(observed.handle!=j->disk.fd.handle);
    LARGE_INTEGER zero={.QuadPart=0}, moved={.QuadPart=JOURNAL_HEADER}, position;
    T(SetFilePointerEx(j->disk.fd.handle,zero,NULL,FILE_BEGIN));
    T(SetFilePointerEx(observed.handle,moved,NULL,FILE_BEGIN));
    T(SetFilePointerEx(j->disk.fd.handle,zero,&position,FILE_CURRENT) && position.QuadPart==0);
    atomic_store(&contend,true);
    T(journal_insert(j,1,1,(const uint8_t *)"b",1)==0); T(journal_pump(j,os_now_ns(),true)==0);
    uint64_t end=os_now_ns()+UINT64_C(10000000000); /* os_test hang guard */
    while(!atomic_load(&entered)) { T(os_now_ns()<end); os_yield(); }
    edit_malloc_guard_begin(); int rc=journal_insert(j,1,2,(const uint8_t *)"c",1); size_t allocations=edit_malloc_guard_end();
    atomic_store(&released,true); T(rc==0 && allocations==0); T(journal_flush(j)==0);
    T(journal_get_stats(j).durable_sequence==3);
    journal_close(j); work_pool_shutdown(&pool); T(os_unlink(path)==OS_OK); T(native_states(root)==0); T(os_rmdir(root)==OS_OK);
    puts("journal_transport_test: independent synchronous file objects, concurrent append/FlushFileBuffers, allocations=0 passed"); return 0;
}
