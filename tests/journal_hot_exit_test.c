/* Self-reexec crash cases: SIGKILL on Linux, TerminateProcess on Windows.
 * (P) Cases are the protocol boundaries named below. Payload sizes are literal
 * lengths or wire constants. Watchdog copies os_test's existing case guard;
 * no timing/performance assertion is made. No inherited handles/shared VM. */
#include "journal/journal.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_hot_exit_test:%d FAIL %s native=%d\n",__LINE__,#c,os_native_error()); return 1; } } while(0)
#define O(c) T((c)==OS_OK)
typedef enum phase { CACHE, DURABLE, BEGIN, DELETE, BEFORE_COMMIT, COMMIT, PAINT,
    ROTATE_CLEAN, ROTATE_FAILED, ROTATE_MISSING, COPY_PARTIAL, PHASE_END } phase;
typedef struct crash { char ready[OS_PATH_MAX]; os_file copy; } crash;
static int notify(const char *path)
{
    os_file f=OS_FILE_INIT; O(os_file_open(&f,path,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL));
    size_t n; O(os_file_write(&f,"ready",sizeof "ready"-1u,&n)); T(n==sizeof "ready"-1u);
    os_file_close(&f); return 0;
}
static void park(void) { for(;;) os_sleep_ns(1000000); }
static int paused_rename(void *ctx, const char *from, const char *to)
{
    (void)from; (void)to; crash *c=ctx;
    if(notify(c->ready)) return -1;
    park(); return -1;
}
static ssize_t fail_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{ (void)ctx; (void)fd; (void)p; (void)n; (void)off; errno=EIO; return -1; }
static ssize_t copy_pause(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{
    (void)fd; crash *c=ctx;
    /* The async staging file is created by the worker. Find and independently
     * open it in the same directory; its held directory lease pins the name. */
    char parent[OS_PATH_MAX]; strcpy(parent,c->ready); char *slash=strrchr(parent,'/'); if(!slash) return -1; *slash=0;
    os_dir d=OS_DIR_INIT; if(os_dir_open(&d,parent)!=OS_OK) return -1;
    os_dir_iter it; if(os_dir_iter_open(&it,&d)!=OS_OK) { os_dir_close(&d); return -1; }
    const char *name; os_err e;
    while((e=os_dir_next(&it,&name))==OS_OK && name) {
        if(!strncmp(name,".sublimite-copy-",sizeof ".sublimite-copy-"-1u)) {
            e=os_file_open_at(&c->copy,&d,name,OS_OPEN_RDWR); break;
        }
    }
    os_dir_iter_close(&it); os_dir_close(&d);
    if(e!=OS_OK || !os_file_is_open(&c->copy)) return -1;
    size_t done=0;
    if(os_file_pwrite(&c->copy,p,n,off,&done)!=OS_OK || done!=n) return -1;
    if(notify(c->ready)) return -1;
    park(); return -1;
}
static int writer(const char *path, const char *ready, phase at)
{
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    crash c={.copy=OS_FILE_INIT}; strcpy(c.ready,ready);
    if(at==COPY_PARTIAL) {
        char source[OS_PATH_MAX]; int n=snprintf(source,sizeof source,"%s.base",path); T(n>0 && (size_t)n<sizeof source);
        os_file f=OS_FILE_INIT; O(os_file_open(&f,source,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL));
        /* (P) Existing copy loop's stack chunk plus one byte requires a
         * partial first transfer, and stays below JOURNAL_ARTIFACT_BYTES. */
        O(os_file_set_size(&f,JOURNAL_DEFAULT_SYNC_BYTES+1u)); os_file_close(&f);
        journal_base base; T(journal_capture_base(source,&base)==0);
        journal_io io={.ctx=&c,.write=copy_pause}; T(journal_set_io(j,&io)==0);
        journal_retain retained; work_handle h; T(journal_retain_start(j,&retained,&base,&h)==0);
        park(); return 1;
    }
    T(journal_insert(j,1,0,(const uint8_t *)"before",sizeof "before"-1u)==0);
    if(at==CACHE) { T(notify(ready)==0); park(); }
    T(journal_flush(j)==0);
    if(at>=ROTATE_CLEAN && at<=ROTATE_MISSING) {
        journal_io io={.ctx=&c,.rename=paused_rename};
        if(at==ROTATE_FAILED) {
            io.append_write=fail_append; T(journal_set_io(j,&io)==0);
            T(journal_insert(j,1,sizeof "before"-1u,(const uint8_t *)"!",1)==JOURNAL_IO);
            io.append_write=NULL;
        }
        T(journal_set_io(j,&io)==0);
        uint8_t bytes[sizeof(uint64_t)+sizeof "checkpoint"-1u]={0}; memcpy(bytes+sizeof(uint64_t),"checkpoint",sizeof "checkpoint"-1u);
        journal_record r={.type=JOURNAL_INSERT,.buffer_id=1,.data=bytes,.size=sizeof bytes};
        T(journal_rotate(j,&r,1)==0); return 1; /* paused rename must not return */
    }
    if(at==DURABLE) { T(notify(ready)==0); park(); }
    if(at==PAINT) { T(journal_append(j,JOURNAL_PAINT,1,NULL,0)==0); T(notify(ready)==0); park(); }
    T(journal_append(j,JOURNAL_GROUP_BEGIN,0,NULL,0)==0);
    if(at==BEGIN) { T(notify(ready)==0); park(); }
    T(journal_delete(j,1,0,sizeof "before"-1u)==0);
    if(at==DELETE) { T(notify(ready)==0); park(); }
    T(journal_flush(j)==0); /* PAD across a batch boundary within the action */
    T(journal_insert(j,1,0,(const uint8_t *)"after",sizeof "after"-1u)==0);
    if(at==BEFORE_COMMIT) { T(notify(ready)==0); park(); }
    T(journal_append(j,JOURNAL_GROUP_COMMIT,0,NULL,0)==0);
    T(notify(ready)==0); park(); return 1;
}
typedef struct model { uint8_t text[JOURNAL_PAGE]; size_t len; uint64_t paint; } model;
static uint64_t get64(const uint8_t *p)
{ uint64_t v=0; for(size_t i=0;i<sizeof v;i++) v|=(uint64_t)p[i]<<(i*8u); return v; }
static int replay(void *ctx, const journal_record *r)
{
    model *m=ctx;
    if(r->type==JOURNAL_PAINT) { m->paint=r->buffer_id; return 0; }
    if(r->type!=JOURNAL_INSERT && r->type!=JOURNAL_DELETE) return 1; /* group markers never delivered */
    uint64_t off=get64(r->data); if(off>m->len) return 1;
    if(r->type==JOURNAL_DELETE) {
        uint64_t n=get64(r->data+sizeof(uint64_t)); if(n>m->len-(size_t)off) return 1;
        memmove(m->text+(size_t)off,m->text+(size_t)(off+n),m->len-(size_t)(off+n)); m->len-=(size_t)n;
    } else {
        size_t n=r->size-sizeof(uint64_t); if(n>sizeof m->text-m->len) return 1;
        memmove(m->text+(size_t)off+n,m->text+(size_t)off,m->len-(size_t)off);
        memcpy(m->text+(size_t)off,r->data+sizeof(uint64_t),n); m->len+=n;
    }
    return 0;
}
static int entries(const char *dir, const char *prefix, size_t *count, char *last, size_t cap)
{
    *count=0; os_dir d=OS_DIR_INIT; O(os_dir_open(&d,dir)); os_dir_iter it; O(os_dir_iter_open(&it,&d));
    const char *name; os_err e;
    while((e=os_dir_next(&it,&name))==OS_OK && name) if(!strncmp(name,prefix,strlen(prefix))) {
        (*count)++; int n=snprintf(last,cap,"%s/%s",dir,name); T(n>0 && (size_t)n<cap);
    }
    O(e); os_dir_iter_close(&it); os_dir_close(&d); return 0;
}
static int cleanup(const char *dir)
{
    os_dir d=OS_DIR_INIT; O(os_dir_open(&d,dir)); os_dir_iter it; O(os_dir_iter_open(&it,&d));
    const char *name; os_err e;
    while((e=os_dir_next(&it,&name))==OS_OK && name) O(os_unlink_at(&d,name));
    O(e); os_dir_iter_close(&it); os_dir_close(&d); O(os_rmdir(dir)); return 0;
}
static int trial(const char *self, const char *dir, phase at)
{
    char path[OS_PATH_MAX], ready[OS_PATH_MAX], role[sizeof "--writer=4294967295"];
    int n=snprintf(path,sizeof path,"%s/session-owner",dir); T(n>0 && (size_t)n<sizeof path);
    n=snprintf(ready,sizeof ready,"%s/ready",dir); T(n>0 && (size_t)n<sizeof ready);
    n=snprintf(role,sizeof role,"--writer=%u",(unsigned)at); T(n>0 && (size_t)n<sizeof role);
    const char *args[]={self,role,path,ready,NULL}; os_proc child; O(os_proc_spawn(&child,args));
    bool reached=false; uint64_t end=os_now_ns()+UINT64_C(10000000000); /* existing os_test case guard */
    while(os_now_ns()<end) {
        os_stat st;
        if(os_stat_path(ready,0,&st)==OS_OK && st.size==sizeof "ready"-1u) { reached=true; break; }
        os_proc_status status;
        os_err e=os_proc_wait(&child,0,&status);
        if(e==OS_OK) { fprintf(stderr,"journal_hot_exit_test: child exited code=%d phase=%d\n",status.code,at); return 1; }
        T(e==OS_E_TIMEDOUT); os_sleep_ns(1000000);
    }
    if(!reached) { (void)os_proc_kill(&child); os_proc_status status; (void)os_proc_wait(&child,-1,&status); T(reached); }
    if(at>=ROTATE_CLEAN) T(journal_retire_directory(dir,path)==JOURNAL_BUSY);
    char tmp[OS_PATH_MAX]; size_t count=0;
    if(at>=ROTATE_CLEAN && at<=ROTATE_MISSING) {
        T(entries(dir,".sublimite-journal-",&count,tmp,sizeof tmp)==0 && count==1);
        if(at==ROTATE_MISSING) O(os_unlink(path));
    }
    O(os_proc_kill(&child)); os_proc_status status; O(os_proc_wait(&child,-1,&status)); T(status.how==OS_PROC_ABNORMAL);
    if(at>=ROTATE_CLEAN) {
        T(journal_retire_directory(dir,path)==0);
        T(entries(dir,".sublimite-journal-",&count,tmp,sizeof tmp)==0);
        if(at==ROTATE_CLEAN || at==COPY_PARTIAL) T(count==0);
        else {
            T(count==1 && strstr(tmp,".set-aside-")); model m={0}; journal_replay_result rr;
            T(journal_scan_file(tmp,replay,&m,&rr)==0 && !rr.corrupt);
            T(m.len==sizeof "checkpoint"-1u && !memcmp(m.text,"checkpoint",m.len));
        }
        if(at==COPY_PARTIAL) T(entries(dir,".sublimite-copy-",&count,tmp,sizeof tmp)==0 && count==0);
    } else {
        model m={0}; journal_replay_result rr; T(journal_replay_file(path,replay,&m,&rr)==0);
        const char *want=at==COMMIT?"after":"before";
        T(m.len==strlen(want) && !memcmp(m.text,want,m.len));
        T(rr.corrupt==(at==BEGIN || at==DELETE || at==BEFORE_COMMIT)); T(m.paint==(at==PAINT?1u:0u));
    }
    T(cleanup(dir)==0); return 0;
}
int main(int argc, char **argv)
{
    if(argc==4 && !strncmp(argv[1],"--writer=",sizeof "--writer="-1u)) {
        unsigned at; T(sscanf(argv[1],"--writer=%u",&at)==1 && at<PHASE_END);
        return writer(argv[2],argv[3],(phase)at);
    }
    T(argc==1); char self[OS_PATH_MAX], temp[OS_PATH_MAX], root[OS_PATH_MAX]; O(os_proc_self_path(self,sizeof self)); O(os_temp_dir(temp,sizeof temp));
    for(phase at=CACHE;at<PHASE_END;at++) {
        int n=snprintf(root,sizeof root,"%s/sublimite-test-journal-kill-%" PRIu64 "-%u",temp,os_proc_id(),(unsigned)at); T(n>0 && (size_t)n<sizeof root);
#if defined(_WIN32)
        for(char *p=root;*p;p++) if(*p=='\\') *p='/';
#endif
        O(os_mkdir(root)); T(trial(self,root,at)==0);
    }
    puts("journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed"); return 0;
}
