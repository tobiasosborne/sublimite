/* Windows execution of acknowledgment/manual-replay cases from the Linux
 * journal_ack_test. (P) Fixture sizes follow literals and the inherited cap.
 * Link creation has no frozen os.h entry, so those fixtures are explicit skips. */
#include "journal/journal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define main journal_replay_main
#include "../../tools/journal_replay.c"
#undef main
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_ack_test:%d FAIL %s native=%d\n",__LINE__,#c,os_native_error()); return 1; } } while(0)
#define O(c) T((c)==OS_OK)
static int ack(const char *source)
{
    char self[OS_PATH_MAX]; O(os_proc_self_path(self,sizeof self));
    const char *args[]={self,"--acknowledge",source,NULL}; os_proc child; O(os_proc_spawn(&child,args));
    os_proc_status st; O(os_proc_wait(&child,-1,&st)); T(st.how==OS_PROC_EXITED); return st.code;
}
static int named(char *out, size_t cap, const char *dir, const char *leaf)
{ int n=snprintf(out,cap,"%s/%s",dir,leaf); return n<0 || (size_t)n>=cap; }
static bool exists(const char *path) { os_stat st; return os_stat_path(path,OS_STAT_NOFOLLOW,&st)==OS_OK; }
int main(int argc, char **argv)
{
    if(argc==3) return journal_replay_main(argc,argv);
    T(argc==1); char temp[OS_PATH_MAX], root[OS_PATH_MAX]; O(os_temp_dir(temp,sizeof temp));
    int n=snprintf(root,sizeof root,"%s/sublimite-test-journal-ack-%" PRIu64,temp,os_proc_id()); T(n>0 && (size_t)n<sizeof root); O(os_mkdir(root));
    char source[OS_PATH_MAX], orphan[OS_PATH_MAX], target[OS_PATH_MAX], ordinary[OS_PATH_MAX];
    T(!named(source,sizeof source,root,".sublimite-journal-!1-1-1.set-aside-1"));
    T(!named(orphan,sizeof orphan,root,".sublimite-base-orphan")); T(!named(target,sizeof target,root,"resolved")); T(!named(ordinary,sizeof ordinary,root,"session-preserve"));
    work_pool pool; T(work_pool_init(&pool,1,0)==0); journal *j;
    T(journal_open(&j,source,&pool,NULL)==0); journal_base empty={.path=""};
    T(journal_set_base(j,1,&empty)==0); T(journal_insert(j,1,0,(const uint8_t *)"recovered",sizeof "recovered"-1u)==0); T(journal_flush(j)==0);
    journal_close(j); work_pool_shutdown(&pool);
    os_file f=OS_FILE_INIT; O(os_file_open(&f,orphan,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)); O(os_file_set_size(&f,JOURNAL_ARTIFACT_BYTES+1u)); os_file_close(&f);
    O(os_file_open(&f,target,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)); os_file_close(&f);
    FILE *out=tmpfile(); T(out); T(journal_manual_replay(source,target,0,true,out)==0); T(fclose(out)==0);
    T(journal_retire_directory(root,NULL)==0 && exists(orphan));
    os_dir guard=OS_DIR_INIT; O(os_dir_open(&guard,root)); O(os_dir_try_lock(&guard,OS_LOCK_EXCLUSIVE));
    T(ack(source)!=0 && exists(source)); T(journal_acknowledge(source)==JOURNAL_BUSY); os_dir_close(&guard);
    O(os_file_open(&f,source,OS_OPEN_READ)); O(os_file_try_lock(&f,OS_LOCK_EXCLUSIVE)); T(ack(source)!=0 && exists(source)); os_file_close(&f);
    O(os_file_open(&f,ordinary,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)); os_file_close(&f); T(ack(ordinary)!=0 && exists(ordinary)); O(os_unlink(ordinary));
    T(ack(source)==0 && !exists(source)); T(journal_retire_directory(root,NULL)==0 && !exists(orphan));
    O(os_file_open(&f,target,OS_OPEN_READ)); char bytes[sizeof "recovered"]; size_t got; O(os_file_read(&f,bytes,sizeof bytes,&got)); T(got==sizeof "recovered"-1u && !memcmp(bytes,"recovered",got)); os_file_close(&f);
    O(os_mkdir(source)); T(journal_acknowledge(source)==JOURNAL_BUSY && exists(source)); O(os_rmdir(source));
    O(os_file_open(&f,source,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)); O(os_file_write(&f,"torn",sizeof "torn"-1u,&got)); os_file_close(&f);
    T(journal_acknowledge(source)!=0 && exists(source));
    O(os_dir_open(&guard,root)); os_dir_iter it; O(os_dir_iter_open(&it,&guard)); const char *name; os_err e;
    while((e=os_dir_next(&it,&name))==OS_OK && name) O(os_unlink_at(&guard,name));
    O(e); os_dir_iter_close(&it); os_dir_close(&guard); O(os_rmdir(root));
    puts("journal_ack_test: SKIP hard-link/symlink fixtures: frozen os.h has no link-creation API");
    puts("journal_ack_test: explicit verified acknowledgement releases reserved source and orphan BASE cleanup"); return 0;
}
