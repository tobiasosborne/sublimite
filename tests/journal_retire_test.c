/* (M)[AC] fixture count follows the 32 accumulated sessions in edit-btj.
 * (P) structural integers: zero origin/empty, one buffer/worker, two outcomes;
 * paths use journal_base's schema capacity, wire bytes use JOURNAL_* constants.
 * Time conversion: (P) nanoseconds per second = 1000000000. */
#include "journal/journal.h"
#include "editor/private.h"
#include "trace/trace.h"
#include <dirent.h>
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
    T(snprintf(finished,sizeof finished,"%s/session-finished",root)>0);
    T(snprintf(pending,sizeof pending,"%s/session-pending",root)>0);
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
        T(snprintf(path,sizeof path,"%s/session-%u",root,which)>0);
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
        T(snprintf(path,sizeof path,"%s/session-%u",root,which)>0);
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
    for(unsigned i=0;i<FIXTURE_SESSIONS;i++) { T(snprintf(path,sizeof path,"%s/session-%u",root,i)>0); T(fixture(path,base,false)==0); }
    size_t count; uint64_t before,after; T(discovery(root,&count,&before)==0 && count==FIXTURE_SESSIONS);
    struct stat st; T(stat(path,&st)==0);
    T(journal_retire_directory(root,NULL)==0);
    T(discovery(root,&count,&after)==0 && count==0);
    printf("(M)[%s] BAT0=%s empty_fixture_count=%u journal_bytes=%lld base_bytes=%zu discovery_before_ns=%llu discovery_after_ns=%llu load1=%g\n",
        !strcmp(power,"Discharging")?"bat":"AC",power,FIXTURE_SESSIONS,(long long)st.st_size,strlen("base"),(unsigned long long)before,(unsigned long long)after,load1);
    T(cleanup(root)==0); return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register()>=0); T(retirement()==0); T(protection()==0); T(artifacts()==0); T(save_metadata()==0); T(custom_reference()==0); T(targets()==0); T(manual()==0); T(measurement()==0);
    puts("journal_retire_test: passed"); return 0;
}
