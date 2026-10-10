/* A missing submission must not strand the frozen cancellation fixture.
 * Include its unchanged body with submission deliberately suppressed. */
#include "work/work.h"
static work_handle find_timeout_submit(work_pool *pool,work_job submitted)
{
    (void)pool; (void)submitted;
    return (work_handle){0,1};
}
#define work_submit find_timeout_submit
#define main find_frozen_test_entry
#include "find_test.c"
#undef main
#undef work_submit
static void normal_exit_marker(void)
{
    char path[80];
    (void)snprintf(path,sizeof path,"build/find-exit-%ld",(long)getppid());
    FILE *file=fopen(path,"w");
    if (file) { (void)fputc('1',file); (void)fclose(file); }
}
static int normal_fixture(void *unused)
{
    (void)unused;
    return atexit(normal_exit_marker)==0 ? 0 : 1;
}
static int missing_start_fixture(void *unused)
{
    (void)unused;
    return cancellation();
}
int main(void)
{
    /* Successful fixtures must run normal exit hooks, including LSan when the
     * coordinator enables it. Only timed-out fixtures skip those hooks. */
    char path[80];
    (void)snprintf(path,sizeof path,"build/find-exit-%ld",(long)getpid());
    (void)unlink(path);
    CHECK(find_fixture_supervise(normal_fixture,NULL,UINT64_C(1000000000),"normal exit")==0);
    FILE *file=fopen(path,"r");
    CHECK(file!=NULL);
    CHECK(fgetc(file)=='1');
    CHECK(fclose(file)==0 && unlink(path)==0);
    int code=find_fixture_supervise(missing_start_fixture,NULL,UINT64_C(100000000),
                                    "frozen cancellation missing start");
    CHECK(code==2);
    puts("find_timeout_test: ok (normal exit hooks; frozen missing start terminated with live storage)");
    return 0;
}
