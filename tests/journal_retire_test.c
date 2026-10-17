/* (M)[AC] fixture count follows the 32 accumulated sessions in edit-btj.
 * (P) structural integers: zero origin/empty, one buffer/worker, two outcomes;
 * paths use journal_base's schema capacity, wire bytes use JOURNAL_* constants.
 * Time conversion: (P) nanoseconds per second = 1000000000. */
#include "journal/journal.h"
#include "editor/private.h"
#include "trace/trace.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#define T(x) do { if (!(x)) { fprintf(stderr,"journal_retire_test:%d FAIL %s\n",__LINE__,#x); return 1; } } while (0)
#define FIXTURE_SESSIONS 32u /* (M)[AC] edit-btj profile count */
static int fixture(const char *path, const char *basepath, bool edits)
{
    work_pool pool; T(work_pool_init(&pool,1,0)==0);
    journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    journal_base base; T(journal_capture_base(basepath,&base)==0);
    T(journal_set_base(j,1,&base)==0);
    if(edits) T(journal_insert(j,1,0,(const uint8_t *)"unsaved",strlen("unsaved"))==0);
    T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool); return 0;
}
/* edit-28u: an empty session (no BASE, no edits): only the closed tab order. */
static int empty_fixture(const char *path)
{
    work_pool pool; T(work_pool_init(&pool,1,0)==0);
    journal *j; T(journal_open(&j,path,&pool,NULL)==0);
    T(journal_set_tabs(j,NULL,0,0)==0);
    T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool); return 0;
}
static int discovery(const char *root, size_t *count, uint64_t *ns)
{
    struct timespec begin,end; T(clock_gettime(CLOCK_MONOTONIC,&begin)==0);
    DIR *dir=opendir(root); T(dir); *count=0; struct dirent *ent;
    /* Same name filter and open/fstat work as startup discovery. */
    while((ent=readdir(dir))) {
        if(strncmp(ent->d_name,"session-",strlen("session-")) || strchr(ent->d_name,'.')) continue;
        int fd=openat(dirfd(dir),ent->d_name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        T(fd>=0); struct stat st; T(fstat(fd,&st)==0); close(fd);
        if(S_ISREG(st.st_mode) && st.st_size) {
            char lock[sizeof ((journal_base *)0)->captured_path];
            T(snprintf(lock,sizeof lock,".%s.lock",ent->d_name)>0);
            fd=openat(dirfd(dir),lock,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
            T(fd>=0 && flock(fd,LOCK_EX|LOCK_NB)==0); close(fd); (*count)++;
        }
    }
    T(closedir(dir)==0); T(clock_gettime(CLOCK_MONOTONIC,&end)==0);
    *ns=(uint64_t)(end.tv_sec-begin.tv_sec)*UINT64_C(1000000000)+(uint64_t)end.tv_nsec-(uint64_t)begin.tv_nsec;
    return 0;
}
static int cleanup(const char *root)
{
    DIR *dir=opendir(root); T(dir); struct dirent *ent;
    while((ent=readdir(dir))) if(strcmp(ent->d_name,".") && strcmp(ent->d_name,"..")) T(unlinkat(dirfd(dir),ent->d_name,0)==0);
    T(closedir(dir)==0); T(rmdir(root)==0); return 0;
}
static int retirement(void)
{
    char root[]="build/retire-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], clean[sizeof base], dirty[sizeof base], next[sizeof base];
    T(snprintf(base,sizeof base,"%s/base",root)>0);
    int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); T(write(fd,"base",strlen("base"))==(ssize_t)strlen("base")); close(fd);
    T(snprintf(clean,sizeof clean,"%s/session-clean",root)>0);
    T(snprintf(dirty,sizeof dirty,"%s/session-dirty",root)>0);
    T(snprintf(next,sizeof next,"%s/session-next",root)>0);
    T(fixture(clean,base,false)==0); T(fixture(dirty,base,true)==0);
    struct stat before,after; T(stat(dirty,&before)==0);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config config={.journal_path=next,.start_empty=true}; editor *e=NULL;
    T(editor_open(&e,&config,&backend)==0);
    T(access(clean,F_OK)<0); /* red: discovery only recovered newest */
    /* Dirty source may be migrated by normal recovery; force direct housekeeping
     * below to verify its inode/size/timestamps, independent of that migration. */
    editor_close(e); T(cleanup(root)==0);
    T(mkdtemp(strcpy(root,"build/retire-XXXXXX")));
    T(snprintf(base,sizeof base,"%s/base",root)>0); fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    T(snprintf(dirty,sizeof dirty,"%s/session-dirty",root)>0); T(fixture(dirty,base,true)==0);
    T(stat(dirty,&before)==0); T(journal_retire_directory(root,NULL)==0); T(stat(dirty,&after)==0);
    T(before.st_ino==after.st_ino && before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec);
    T(cleanup(root)==0); return 0;
}
static int protection(void)
{
    char root[]="build/protection-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], path[sizeof base], clean[sizeof base], lock[sizeof base];
    T(snprintf(base,sizeof base,"%s/base",root)>0); int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    T(snprintf(path,sizeof path,"%s/session-dirty.retired",root)>0); T(fixture(path,base,true)==0);
    T(snprintf(clean,sizeof clean,"%s/session-clean.retired",root)>0); T(fixture(clean,base,false)==0);
    struct stat before,after; T(stat(path,&before)==0);
    T(journal_retire_directory(root,NULL)==0); T(access(clean,F_OK)<0);
    T(stat(path,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    T(snprintf(clean,sizeof clean,"%s/session-corrupt",root)>0); T(fixture(clean,base,false)==0);
    fd=open(clean,O_WRONLY|O_APPEND); T(fd>=0); T(write(fd,"torn",strlen("torn"))==(ssize_t)strlen("torn")); close(fd);
    T(stat(clean,&before)==0); T(journal_retire_directory(root,NULL)==0);
    T(stat(clean,&after)==0 && before.st_size==after.st_size);
    T(snprintf(clean,sizeof clean,"%s/session-live",root)>0); T(fixture(clean,base,false)==0);
    T(snprintf(lock,sizeof lock,"%s/.session-live.lock",root)>0); fd=open(lock,O_CREAT|O_RDWR,0600); T(fd>=0 && flock(fd,LOCK_EX|LOCK_NB)==0);
    T(journal_retire_directory(root,NULL)==0 && access(clean,F_OK)==0); close(fd);
    T(snprintf(clean,sizeof clean,"%s/session-mismatch",root)>0); T(fixture(clean,base,false)==0);
    fd=open(base,O_WRONLY); T(fd>=0); T(write(fd,"changed",strlen("changed"))==(ssize_t)strlen("changed")); close(fd);
    T(journal_retire_directory(root,NULL)==0 && access(clean,F_OK)==0);
    T(cleanup(root)==0); return 0;
}
/* edit-28u: a discoverable session holding only a BASE (a checkpointed
 * clean tab set) is hot-exit state and stays; a BASE-free one is retired. */
static int clean_tab_set(void)
{
    char root[]="build/tabset-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], tabset[sizeof base], empty[sizeof base];
    T(snprintf(base,sizeof base,"%s/base",root)>0); int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    T(snprintf(tabset,sizeof tabset,"%s/session-tabset",root)>0); T(fixture(tabset,base,false)==0);
    T(snprintf(empty,sizeof empty,"%s/session-empty",root)>0); T(empty_fixture(empty)==0);
    T(journal_retire_directory(root,NULL)==0);
    T(access(tabset,F_OK)==0); /* red: retired as clean, losing the tab set */
    T(access(empty,F_OK)<0);
    T(cleanup(root)==0); return 0;
}
static int artifacts(void)
{
    char root[]="build/artifacts-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], old[sizeof base], newer[sizeof base], dirty[sizeof base];
    T(snprintf(base,sizeof base,"%s/.sublimite-base-pinned",root)>0);
    int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); T(write(fd,"base",strlen("base"))==(ssize_t)strlen("base")); close(fd);
    T(snprintf(dirty,sizeof dirty,"%s/session-dirty.set-aside-fixture",root)>0); T(fixture(dirty,base,true)==0);
    struct stat before,after; T(stat(dirty,&before)==0);
    T(snprintf(old,sizeof old,"%s/.sublimite-base-old",root)>0); fd=open(old,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); T(ftruncate(fd,(off_t)JOURNAL_ARTIFACT_BYTES)==0);
    /* (P) epoch timestamp makes this eligible artifact older than fixtures
     * created at the present realtime, without inventing an expiry duration. */
    const struct timespec epoch[2]={{0,0},{0,0}}; T(futimens(fd,epoch)==0); close(fd);
    T(snprintf(newer,sizeof newer,"%s/.sublimite-base-new",root)>0); fd=open(newer,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); T(write(fd,"base",strlen("base"))==(ssize_t)strlen("base")); close(fd);
    T(journal_retire_directory(root,NULL)==0);
    T(access(old,F_OK)<0 && access(newer,F_OK)==0 && access(base,F_OK)==0);
    T(stat(dirty,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    T(cleanup(root)==0);
    T(mkdtemp(strcpy(root,"build/artifacts-XXXXXX")));
    T(snprintf(base,sizeof base,"%s/base",root)>0); fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    T(snprintf(old,sizeof old,"%s/session-old.set-aside-fixture",root)>0); T(fixture(old,base,false)==0);
    fd=open(old,O_WRONLY); T(fd>=0); T(futimens(fd,epoch)==0); close(fd);
    T(snprintf(dirty,sizeof dirty,"%s/session-dirty.set-aside-fixture",root)>0); T(fixture(dirty,base,true)==0); T(stat(dirty,&before)==0);
    /* (M)[AC] cap / sealed-page size supplies the pressure-fixture count. */
    for(size_t i=0;i<JOURNAL_ARTIFACT_BYTES/JOURNAL_PAGE;i++) {
        T(snprintf(newer,sizeof newer,"%s/session-%zu.set-aside-fixture",root,i)>0); T(fixture(newer,base,false)==0);
    }
    T(journal_retire_directory(root,NULL)==0 && access(old,F_OK)<0);
    T(stat(dirty,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    T(cleanup(root)==0); return 0;
}
static int custom_reference(void)
{
    char root[]="build/custom-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], path[sizeof base];
    T(snprintf(path,sizeof path,"%s/custom-journal",root)>0);
    int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    /* (M)[AC] use the observed profile count as the candidate-name fixture.
     * Choose an actual readdir ordering with the journal before its BASE;
     * filesystem iteration order is not assumed to follow creation order. */
    for(unsigned i=0;i<FIXTURE_SESSIONS;i++) {
        T(snprintf(base,sizeof base,"%s/.sublimite-base-%u",root,i)>0);
        fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0);
        /* (E) measured cap + one byte creates pressure without a new budget. */
        T(ftruncate(fd,(off_t)JOURNAL_ARTIFACT_BYTES+1)==0); close(fd);
    }
    DIR *dir=opendir(root); T(dir); struct dirent *entry; bool seen=false,chosen=false;
    while((entry=readdir(dir))) {
        if(!strcmp(entry->d_name,"custom-journal")) seen=true;
        else if(seen && !strncmp(entry->d_name,".sublimite-base-",strlen(".sublimite-base-"))) {
            T(snprintf(base,sizeof base,"%s/%s",root,entry->d_name)>0); chosen=true; break;
        }
    }
    T(closedir(dir)==0 && chosen);
    T(snprintf(path,sizeof path,"%s/custom-journal",root)>0); T(fixture(path,base,true)==0);
    struct stat before,after; T(stat(path,&before)==0);
    T(journal_retire_directory(root,NULL)==0 && access(base,F_OK)==0);
    T(stat(path,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    T(cleanup(root)==0); return 0;
}
static int save_metadata(void)
{
    char root[]="build/save-marker-XXXXXX"; T(mkdtemp(root));
    char basepath[sizeof ((journal_base *)0)->captured_path], finished[sizeof basepath], pending[sizeof basepath];
    T(snprintf(basepath,sizeof basepath,"%s/base",root)>0); int fd=open(basepath,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    /* edit-28u: discoverable BASE journals are clean tab sets and stay;
     * dotted .retired names (never discovered, not capped) keep checking that FINISHED alone is not an edit. */
    T(snprintf(finished,sizeof finished,"%s/session-finished.retired",root)>0);
    T(snprintf(pending,sizeof pending,"%s/session-pending.retired",root)>0);
    /* (P) wire SAVE phases: PREPARED=1, FINISHED=2 (journal_save_finish).
     * SAVE prefix is sequence64 + phase32 + reserved32; all start zero. */
    for(unsigned phase=1;phase<=2;phase++) {
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
        T(journal_open(&j,phase==1?pending:finished,&pool,NULL)==0);
        journal_base base; T(journal_capture_base(basepath,&base)==0); T(journal_set_base(j,1,&base)==0);
        const size_t prefix=sizeof(uint64_t)+sizeof(uint32_t)+sizeof(uint32_t);
        uint8_t marker[JOURNAL_BASE_MAX+sizeof(uint64_t)+sizeof(uint32_t)+sizeof(uint32_t)]={0};
        marker[sizeof(uint64_t)]=(uint8_t)phase; size_t size;
        T(journal_encode_base(&base,marker+prefix,sizeof marker-prefix,&size)==0);
        T(journal_append(j,JOURNAL_SAVE,1,marker,prefix+size)==0);
        T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool);
    }
    T(journal_retire_directory(root,NULL)==0);
    T(access(finished,F_OK)<0 && access(pending,F_OK)==0);
    T(cleanup(root)==0); return 0;
}
static int targets(void)
{
    char root[]="build/targets-XXXXXX"; T(mkdtemp(root));
    char basepath[sizeof ((journal_base *)0)->captured_path], other[sizeof basepath], path[sizeof basepath];
    T(snprintf(basepath,sizeof basepath,"%s/base",root)>0); T(snprintf(other,sizeof other,"%s/other",root)>0);
    int fd=open(basepath,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    fd=open(other,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    /* (P) cases derive from matching target, differing target, edited log. */
    enum { MATCHING, DIFFERING, EDITED, TARGET_CASES };
    for(unsigned which=MATCHING;which<TARGET_CASES;which++) {
        T(snprintf(path,sizeof path,"%s/session-%u.retired",root,which)>0); /* edit-28u, as save_metadata */
        work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
        T(journal_open(&j,path,&pool,NULL)==0); journal_base base;
        T(journal_capture_base(basepath,&base)==0); T(journal_set_base(j,1,&base)==0);
        char target[sizeof basepath]; T(realpath(which==DIFFERING?other:basepath,target));
        T(journal_append(j,JOURNAL_TARGET,1,(const uint8_t *)target,strlen(target)+1)==0);
        if(which==EDITED) T(journal_insert(j,1,0,(const uint8_t *)"unsaved",strlen("unsaved"))==0);
        T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool);
    }
    T(journal_retire_directory(root,NULL)==0);
    for(unsigned which=MATCHING;which<TARGET_CASES;which++) {
        T(snprintf(path,sizeof path,"%s/session-%u.retired",root,which)>0);
        T((access(path,F_OK)==0)==(which!=MATCHING));
    }
    T(cleanup(root)==0); return 0;
}
static int manual(void)
{
    char root[]="build/manual-XXXXXX"; T(mkdtemp(root));
    char base[sizeof ((journal_base *)0)->captured_path], path[sizeof base];
    T(snprintf(base,sizeof base,"%s/base",root)>0); int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0);
    T(write(fd,"base",strlen("base"))==(ssize_t)strlen("base")); close(fd);
    T(snprintf(path,sizeof path,"%s/session-dirty.set-aside-fixture",root)>0); T(fixture(path,base,true)==0);
    FILE *output=tmpfile(); T(output); struct stat before,after; T(stat(path,&before)==0);
    T(journal_manual_replay(path,base,0,false,output)==0);
    char bytes[sizeof "unsavedbase"]={0}; fd=open(base,O_RDONLY); T(fd>=0);
    T(read(fd,bytes,sizeof bytes)==(ssize_t)strlen("base") && !strcmp(bytes,"base")); close(fd);
    /* (P) zero allowed file-growth bytes forces the temp write to fail. The
     * original file and edited journal must survive the temp-unlink path. */
    pid_t child=fork(); T(child>=0);
    if(!child) {
        const struct rlimit limit={0,0};
        if(signal(SIGXFSZ,SIG_IGN)==SIG_ERR || setrlimit(RLIMIT_FSIZE,&limit)) _exit(JOURNAL_IO);
        _exit(journal_manual_replay(path,base,0,true,output)==JOURNAL_IO?0:JOURNAL_INVALID);
    }
    int status; T(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    T(stat(path,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    memset(bytes,0,sizeof bytes); fd=open(base,O_RDONLY); T(fd>=0);
    T(read(fd,bytes,sizeof bytes)==(ssize_t)strlen("base") && !strcmp(bytes,"base")); close(fd);
    T(journal_manual_replay(path,path,0,true,output)==JOURNAL_INVALID);
    T(journal_manual_replay(path,base,0,true,output)==0);
    memset(bytes,0,sizeof bytes); fd=open(base,O_RDONLY); T(fd>=0);
    T(read(fd,bytes,sizeof bytes)==(ssize_t)strlen("unsavedbase") && !strcmp(bytes,"unsavedbase")); close(fd);
    T(stat(path,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    /* (P) distinct IDs distinguish the two BASE owners in a multi-buffer
     * journal. Auto selection must refuse ambiguity; explicit ID selects only
     * that owner's edits, leaving the other owner's marker out of the result. */
    work_pool multi_pool; T(work_pool_init(&multi_pool,1,0)==0); journal *multi_log;
    char multi[sizeof base]; T(snprintf(multi,sizeof multi,"%s/session-multi.set-aside-fixture",root)>0);
    T(journal_open(&multi_log,multi,&multi_pool,NULL)==0); journal_base multi_base;
    T(journal_capture_base(base,&multi_base)==0);
    T(journal_set_base(multi_log,1,&multi_base)==0 && journal_set_base(multi_log,2,&multi_base)==0);
    T(journal_insert(multi_log,1,0,(const uint8_t *)"one",strlen("one"))==0);
    T(journal_insert(multi_log,2,0,(const uint8_t *)"two",strlen("two"))==0);
    T(journal_flush(multi_log)==0); journal_close(multi_log); work_pool_shutdown(&multi_pool);
    T(journal_manual_replay(multi,base,0,true,output)==JOURNAL_INVALID);
    T(journal_manual_replay(multi,base,2,true,output)==0);
    char multi_bytes[sizeof "twounsavedbase"]={0};
    fd=open(base,O_RDONLY); T(fd>=0);
    T(read(fd,multi_bytes,sizeof multi_bytes)==(ssize_t)strlen("twounsavedbase"));
    T(!strcmp(multi_bytes,"twounsavedbase")); close(fd);
    /* Invalid ranges and corrupt tails must never partly overwrite the target
     * or truncate the edited source. Replaying onto empty bytes fails ranges. */
    fd=open(base,O_WRONLY|O_TRUNC); T(fd>=0); close(fd);
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    char invalid[sizeof base]; T(snprintf(invalid,sizeof invalid,"%s/session-range.set-aside-fixture",root)>0);
    T(journal_open(&j,invalid,&pool,NULL)==0); journal_base identity; T(journal_capture_base(base,&identity)==0);
    T(journal_set_base(j,1,&identity)==0); T(journal_delete(j,1,0,strlen("base"))==0);
    T(journal_flush(j)==0); journal_close(j); work_pool_shutdown(&pool);
    T(journal_manual_replay(invalid,base,0,true,output)!=0); T(stat(base,&after)==0 && after.st_size==0);
    fd=open(path,O_WRONLY|O_APPEND); T(fd>=0); T(write(fd,"torn",strlen("torn"))==(ssize_t)strlen("torn")); close(fd);
    T(stat(path,&before)==0); T(journal_manual_replay(path,base,0,true,output)!=0);
    T(stat(path,&after)==0 && before.st_size==after.st_size); T(stat(base,&after)==0 && after.st_size==0);
    fclose(output); T(cleanup(root)==0); return 0;
}
static int measurement(void)
{
    char power[sizeof "Not charging"]="unknown"; FILE *f=fopen("/sys/class/power_supply/BAT0/status","r");
    T(f); T(fgets(power,sizeof power,f)); fclose(f); power[strcspn(power,"\n")]=0;
    double load1=0; f=fopen("/proc/loadavg","r"); T(f); T(fscanf(f,"%lf",&load1)==1); fclose(f);
    char root[]="build/discovery-XXXXXX"; T(mkdtemp(root)); char base[sizeof ((journal_base *)0)->captured_path], path[sizeof base];
    T(snprintf(base,sizeof base,"%s/base",root)>0); int fd=open(base,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0);
    T(write(fd,"base",strlen("base"))==(ssize_t)strlen("base")); close(fd);
    for(unsigned i=0;i<FIXTURE_SESSIONS;i++) { T(snprintf(path,sizeof path,"%s/session-%u",root,i)>0); T(empty_fixture(path)==0); }
    size_t count; uint64_t before,after; T(discovery(root,&count,&before)==0 && count==FIXTURE_SESSIONS);
    struct stat st; T(stat(path,&st)==0);
    T(journal_retire_directory(root,NULL)==0);
    T(discovery(root,&count,&after)==0 && count==0);
    printf("(M)[%s] BAT0=%s empty_fixture_count=%u journal_bytes=%lld base_bytes=%zu discovery_before_ns=%llu discovery_after_ns=%llu load1=%g\n",
        !strcmp(power,"Discharging")?"bat":"AC",power,FIXTURE_SESSIONS,(long long)st.st_size,strlen("base"),(unsigned long long)before,(unsigned long long)after,load1);
    T(cleanup(root)==0); return 0;
}
/* edit-cxo: a second instance's startup housekeeping runs in the window
 * between the first instance creating its session journal and its editor
 * starting. The placeholder must survive, its lease must already be held, and
 * the owner's editor must start on it. */
static int startup_window(void)
{
    char root[]="build/window-XXXXXX"; T(mkdtemp(root));
    char path[sizeof ((journal_base *)0)->captured_path], lock[sizeof path]; int lease=-1;
    T(journal_session_create(root,path,sizeof path,&lease)==0);
    const char *leaf=strrchr(path,'/'); T(leaf && !strncmp(leaf+1,"session-",strlen("session-")) && !strchr(leaf+1,'.'));
    T(journal_retire_directory(root,NULL)==0);
    T(access(path,F_OK)==0); /* red: retired as an empty clean placeholder */
    T(snprintf(lock,sizeof lock,"%s/.%s.lock",root,leaf+1)>0);
    int other=open(lock,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600); T(other>=0);
    T(flock(other,LOCK_EX|LOCK_NB)!=0); close(other); /* red: lease free for others */
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config config={.journal_path=path,.start_empty=true,.session_lease=&lease}; editor *e=NULL;
    T(editor_open(&e,&config,&backend)==0);
    T(close(lease)==0); /* the editor holds its own duplicate of the lease */
    other=open(lock,O_RDWR|O_CLOEXEC|O_NOFOLLOW); T(other>=0);
    T(flock(other,LOCK_EX|LOCK_NB)!=0);
    editor_close(e); T(flock(other,LOCK_EX|LOCK_NB)==0); close(other);
    /* A lease that does not guard this journal is refused. */
    char stray[sizeof path]; int wrong=-1;
    T(journal_session_create(root,stray,sizeof stray,&wrong)==0);
    char second[sizeof path]; int held=-1; T(journal_session_create(root,second,sizeof second,&held)==0);
    config.journal_path=second; config.session_lease=&wrong; e=NULL;
    int rc=editor_open(&e,&config,&backend); if(e) editor_close(e); T(rc!=0);
    T(close(wrong)==0); T(close(held)==0);
    T(cleanup(root)==0); return 0;
}
/* edit-cxo/oo3: lease files whose journal is gone are swept; a lease that
 * still guards a journal (or a dotted variant) or is held stays. A failed
 * launch through main leaves nothing (wayland_startup_test's rmdir). */
static int orphan_leases(void)
{
    char root[]="build/leases-XXXXXX"; T(mkdtemp(root));
    char orphan[sizeof ((journal_base *)0)->captured_path], guarded[sizeof orphan], aside[sizeof orphan], held[sizeof orphan], jpath[sizeof orphan];
    T(snprintf(orphan,sizeof orphan,"%s/.session-gone.lock",root)>0);
    T(snprintf(guarded,sizeof guarded,"%s/.session-live.lock",root)>0);
    T(snprintf(aside,sizeof aside,"%s/.session-aside.lock",root)>0);
    T(snprintf(held,sizeof held,"%s/.session-held.lock",root)>0);
    const char *locks[]={orphan,guarded,aside,held};
    for(size_t i=0;i<sizeof locks/sizeof *locks;i++) { int fd=open(locks[i],O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd); }
    /* Corrupt (non-JNL1) bytes: retirement must preserve this journal. */
    T(snprintf(jpath,sizeof jpath,"%s/session-live",root)>0); int fd=open(jpath,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0);
    T(write(fd,"x",strlen("x"))==(ssize_t)strlen("x")); close(fd);
    T(snprintf(jpath,sizeof jpath,"%s/session-aside.set-aside-fixture",root)>0); fd=open(jpath,O_CREAT|O_EXCL|O_WRONLY,0600); T(fd>=0); close(fd);
    int lease=open(held,O_RDWR|O_CLOEXEC); T(lease>=0); T(flock(lease,LOCK_EX|LOCK_NB)==0);
    (void)journal_retire_directory(root,NULL);
    T(access(orphan,F_OK)<0); /* red: lease files were never removed */
    T(access(guarded,F_OK)==0 && access(aside,F_OK)==0 && access(held,F_OK)==0);
    close(lease);
    /* The owner releases its own lease file once its empty journal is gone. */
    char path[sizeof orphan]; T(journal_session_create(root,path,sizeof path,&lease)==0);
    T(journal_session_release(path,lease)==JOURNAL_BUSY); T(unlink(path)==0);
    T(journal_session_release(path,lease)==JOURNAL_OK); close(lease);
    T(cleanup(root)==0); return 0;
}
/* edit-52o: pause at the actual rename seam, with a complete page-sized
 * checkpoint. (P) Pipe endpoints are read/write; SIGKILL drops every lease.
 * No guessed duration: the pipe establishes that the temp is visible. */
static int rotation_pause(void *ctx, const char *from, const char *to)
{
    (void)to; int fd=*(int *)ctx; size_t size=strlen(from)+1u;
    if(write(fd,from,size)!=(ssize_t)size) return -1;
    for(;;) pause();
}
static int rotation_names(const char *root, size_t *asides, bool *locks)
{
    DIR *dir=opendir(root); T(dir); struct dirent *entry; *asides=0; *locks=false;
    while((entry=readdir(dir))) {
        if(!strncmp(entry->d_name,"..sublimite-journal-",strlen("..sublimite-journal-"))) *locks=true;
        if(!strncmp(entry->d_name,".sublimite-journal-",strlen(".sublimite-journal-")) &&
           strstr(entry->d_name,".set-aside-")) (*asides)++;
    }
    T(closedir(dir)==0); return 0;
}
/* (P) Cases derive from the sweep's existing clean/missing-original outcomes
 * and the two failed states admitted by journal_rotate: UI append and worker.
 * Reject the original append or lose its writeback so accepted checkpoint
 * bytes exist only in temp. */
typedef enum rotation_case {
    ROTATION_CLEAN, ROTATION_MISSING, ROTATION_APPEND_FAILED, ROTATION_WORKER_FAILED,
    ROTATION_UNFLAGGED_FAILED
} rotation_case;
static ssize_t rotation_reject_write(void *ctx, int fd, const uint8_t *bytes, size_t size, uint64_t off)
{
    (void)ctx; (void)fd; (void)bytes; (void)size; (void)off;
    errno=EIO; return -1;
}
static int rotation_lose_writeback(void *ctx, int fd, bool directory)
{
    (void)ctx;
    if(directory) return fsync(fd);
    /* (P) A consumed writeback error can lose all unsynced bytes. Truncate
     * to the empty durable prefix to exercise worker failure independently
     * of append_failed; metadata is captured only after this damage. */
    if(ftruncate(fd,0)) return -1;
    errno=EIO; return -1;
}
static int rotation_checkpoint(void *ctx, const journal_record *record)
{
    bool *found=ctx;
    if(record->type!=JOURNAL_INSERT || record->buffer_id!=1 ||
       record->size!=sizeof(uint64_t)+strlen("checkpoint") ||
       memcmp(record->data+sizeof(uint64_t),"checkpoint",strlen("checkpoint"))) return JOURNAL_INVALID;
    *found=true; return JOURNAL_OK;
}
static int rotation_temps(rotation_case which)
{
    char root[]="build/rotation-XXXXXX"; T(mkdtemp(root));
    char path[sizeof ((journal_base *)0)->captured_path], temp[sizeof path]={0}, stray[sizeof path];
    int result=0, channel[2]={-1,-1}; pid_t child=-1; bool locks=false; size_t asides=0;
#define R(x) do { if(!(x)) { fprintf(stderr,"journal_retire_test:%d FAIL %s\n",__LINE__,#x); result=1; goto done; } } while(0)
    R(snprintf(path,sizeof path,"%s/session-owner",root)>0); R(pipe(channel)==0);
    child=fork(); R(child>=0);
    if(!child) {
        close(channel[0]); work_pool pool;
        if(work_pool_init(&pool,1,0)) _exit(EXIT_FAILURE);
        journal_io io={.ctx=&channel[1],.rename=rotation_pause}; journal *j=NULL;
        if(journal_open_with_io(&j,path,&pool,NULL,&io)) _exit(EXIT_FAILURE);
        if(which==ROTATION_APPEND_FAILED || which==ROTATION_WORKER_FAILED || which==ROTATION_UNFLAGGED_FAILED) {
            journal_io broken=which!=ROTATION_WORKER_FAILED?
                (journal_io){.append_write=rotation_reject_write}:(journal_io){.sync=rotation_lose_writeback};
            int appended=which!=ROTATION_WORKER_FAILED?JOURNAL_IO:JOURNAL_OK;
            if(journal_set_io(j,&broken) ||
               journal_insert(j,1,0,(const uint8_t *)"checkpoint",strlen("checkpoint"))!=appended)
                _exit(EXIT_FAILURE);
            if(which==ROTATION_WORKER_FAILED && journal_flush(j)!=JOURNAL_IO) _exit(EXIT_FAILURE);
            journal_stats stats=journal_get_stats(j);
            if(stats.accepted_sequence!=1 || stats.durable_sequence || stats.error!=JOURNAL_IO ||
               journal_set_io(j,&io)) _exit(EXIT_FAILURE);
        }
        /* (P) INSERT = offset64 followed by the literal checkpoint bytes. */
        uint8_t payload[sizeof(uint64_t)+sizeof "checkpoint"-1u]={0};
        memcpy(payload+sizeof(uint64_t),"checkpoint",strlen("checkpoint"));
        journal_record cp={JOURNAL_INSERT,1,0,payload,sizeof payload};
        (void)journal_rotate(j,&cp,1); _exit(EXIT_FAILURE);
    }
    close(channel[1]); channel[1]=-1;
    size_t got=0;
    while(got<sizeof temp) {
        ssize_t n=read(channel[0],temp+got,sizeof temp-got); R(n>0); got+=(size_t)n;
        if(temp[got-1u]==0) break;
    }
    R(got && got<sizeof temp && temp[got-1u]==0);
    struct stat before,after; R(stat(temp,&before)==0 && before.st_size==(off_t)JOURNAL_PAGE);
    if(which==ROTATION_APPEND_FAILED || which==ROTATION_WORKER_FAILED || which==ROTATION_UNFLAGGED_FAILED)
        R(stat(path,&after)==0 && after.st_size==0);
    /* The journal API itself must protect its temp, even without an editor's
     * outer session lease. Housekeeping may skip the leased directory. */
    int retired=journal_retire_directory(root,NULL); R(retired==0 || retired==JOURNAL_BUSY);
    R(stat(temp,&after)==0 && before.st_ino==after.st_ino && before.st_size==after.st_size);
    R(access(path,F_OK)==0);
    R(rotation_names(root,&asides,&locks)==0);
    if(locks || asides) { fprintf(stderr,"rotation_temps: live temp gained a lock or set-aside\n"); result=1; }
    R(kill(child,SIGKILL)==0); int status;
    R(waitpid(child,&status,0)==child); child=-1; R(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    if(which==ROTATION_UNFLAGGED_FAILED) {
        /* (P) Remove the sole state character to reproduce the older build's
         * basename, retaining the actual failed checkpoint and original.
         * Moving strlen(flag) bytes also copies the trailing NUL. */
        char old[sizeof temp]; strcpy(old,temp);
        char *flag=strstr(old,".sublimite-journal-"); R(flag);
        flag+=strlen(".sublimite-journal-"); R(*flag=='!');
        memmove(flag,flag+1,strlen(flag));
        R(rename(temp,old)==0); strcpy(temp,old);
    }
    const char *leaf=strrchr(temp,'/'); R(leaf);
    R(snprintf(stray,sizeof stray,"%s/.%s.lock",root,leaf+1)>0);
    int fd=open(stray,O_CREAT|O_WRONLY|O_CLOEXEC,0600); R(fd>=0); close(fd);
    if(which==ROTATION_MISSING) R(unlink(path)==0);
    bool preserve=which!=ROTATION_CLEAN;
    /* A launch runs housekeeping before recovery. An absent original makes
     * the temp the possible sole copy; preserve its inode under set-aside. */
    R(journal_retire_directory(root,NULL)==0 && access(temp,F_OK)<0 && access(stray,F_OK)<0);
    R(rotation_names(root,&asides,&locks)==0 && !locks);
    if(asides!=(preserve?1u:0u)) {
        fprintf(stderr,"rotation_temps: case=%d set-asides=%zu expected=%u\n",which,asides,preserve?1u:0u);
        result=1; goto done;
    }
    if(preserve) {
        DIR *dir=opendir(root); R(dir); struct dirent *entry; bool found=false;
        while((entry=readdir(dir))) if(strstr(entry->d_name,".set-aside-")) {
            if(fstatat(dirfd(dir),entry->d_name,&after,AT_SYMLINK_NOFOLLOW)==0 &&
               before.st_ino==after.st_ino && before.st_size==after.st_size) {
                char aside[sizeof path]; bool checkpoint=false; journal_replay_result replayed;
                if(snprintf(aside,sizeof aside,"%s/%s",root,entry->d_name)>0 &&
                   journal_scan_file(aside,rotation_checkpoint,&checkpoint,&replayed)==JOURNAL_OK &&
                   checkpoint && !replayed.corrupt && replayed.records==1) found=true;
            }
        }
        closedir(dir); R(found);
        R(journal_retire_directory(root,NULL)==0);
        R(rotation_names(root,&asides,&locks)==0 && !locks && asides==1);
    }
done:
    if(child>0) { (void)kill(child,SIGKILL); (void)waitpid(child,NULL,0); }
    for(size_t i=0;i<sizeof channel/sizeof *channel;i++) if(channel[i]>=0) close(channel[i]);
    if(cleanup(root)) result=1;
#undef R
    if(!result) printf("journal_retire_test: rotation case=%d %s\n",which,which==ROTATION_CLEAN?"swept":"set aside and pinned");
    return result;
}
/* Historical names contain no original-journal identity. From directory
 * state alone these are possible sole copies, so keep them once, without
 * scanning even the deliberately incomplete JNL1 content. */
static int legacy_rotation_temp(void)
{
    char root[]="build/legacy-rotation-XXXXXX"; T(mkdtemp(root));
    char temp[sizeof ((journal_base *)0)->captured_path]; int result=0;
    T(snprintf(temp,sizeof temp,"%s/.sublimite-journal-a-b-c",root)>0);
    int fd=open(temp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600); T(fd>=0);
    uint8_t bytes[JOURNAL_PAGE]={0}; memcpy(bytes,"JNL1",strlen("JNL1"));
    bool written=write(fd,bytes,sizeof bytes)==(ssize_t)sizeof bytes; close(fd);
    bool locks=false; size_t asides=0;
    char lock[sizeof temp]; T(snprintf(lock,sizeof lock,"%s/.session-previous.lock",root)>0);
    int owner=open(lock,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600); T(owner>=0 && flock(owner,LOCK_EX|LOCK_NB)==0);
    if(journal_retire_directory(root,NULL) || access(temp,F_OK) ||
       rotation_names(root,&asides,&locks) || locks || asides) {
        fprintf(stderr,"legacy_rotation_temp: live owner's temp changed\n"); result=1;
    }
    close(owner);
    /* A fresh launch already holds its destination lease; it must not pin a
     * prior writer's temp merely by being the launch doing the sweep. */
    char current[sizeof temp]; int destination=-1;
    T(journal_session_create(root,current,sizeof current,&destination)==0);
    if(!written || journal_retire_directory(root,current) || access(temp,F_OK)==0 ||
       rotation_names(root,&asides,&locks) || locks || asides!=1) {
        fprintf(stderr,"legacy_rotation_temp: not set aside without a temp lock\n"); result=1;
    }
    if(!result && (journal_retire_directory(root,NULL) || rotation_names(root,&asides,&locks) || locks || asides!=1)) {
        fprintf(stderr,"legacy_rotation_temp: set-aside changed on next launch\n"); result=1;
    }
    close(destination);
    if(cleanup(root)) result=1;
    return result;
}
int main(void)
{
    trace_init(); T(trace_thread_register()>=0);
    int rotations=0;
    for(rotation_case which=ROTATION_CLEAN;which<=ROTATION_UNFLAGGED_FAILED;which++) rotations|=rotation_temps(which);
    rotations|=legacy_rotation_temp(); T(rotations==0);
    T(retirement()==0); T(protection()==0); T(clean_tab_set()==0); T(artifacts()==0); T(save_metadata()==0); T(custom_reference()==0); T(targets()==0); T(manual()==0); T(startup_window()==0); T(orphan_leases()==0); T(measurement()==0);
    puts("journal_retire_test: passed"); return 0;
}
