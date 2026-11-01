/* Proves tests/win/editor_fixture.h end to end, hidden and without focus:
 * private profile -> typing -> completion -> abrupt process death -> restart
 * -> exact recovered bytes, on both backends the fixture offers:
 *   --null   headless null backend, editor_inject input, SIMULATED present
 *   --d3d11  production D3D11 window, posted WM_CHAR input, CPU-submit completion
 * Consumers (hot exit, save/reload, commands, large files, fonts) build on the
 * same calls; this suite owns no editor logic of its own. */
#include "editor_fixture.h"
#include "trace/trace.h"

#define R(x) do { if(!(x)) { fprintf(stderr,"editor_fixture_test:%d FAIL %s\n",__LINE__,#x); return 1; } } while(0)
#define CHILD_DEATH 75u /* (P) private abrupt-death sentinel, as editor_win_test's 73/74 */

static void make_config(editor_config *cfg, const char *journal)
{
    *cfg=(editor_config){.initial=(const uint8_t *)"base\n",.initial_len=5,.cols=80,.rows=24,
        .journal_path=journal,.platform_backend=PLAT_BACKEND_WIN32,.font_dpi=96,.font_points=12};
}
/* Child: type, wait for completion, wait until the journal holds the last
 * byte, then die without editor_close. */
static int child_type(const char *root, bool native)
{
    efx_profile profile; char journal[OS_PATH_MAX]; editor_config cfg; render_backend b; editor *e=NULL;
    R(efx_profile_adopt(&profile,root)==0);
    R(efx_profile_path(&profile,native ? "d3d11.journal" : "null.journal",journal,sizeof journal)==0);
    make_config(&cfg,journal);
    R((native ? efx_open_d3d11(&e,&cfg,&b) : efx_open_null(&e,&cfg,&b))==0 && efx_settle(e)==0);
    R(efx_type(e,"ab")==0);
    R((native ? efx_wait_submitted(e) : efx_wait_presented(e))==0);
    R(efx_wait_journal_insert(journal,'b'));
    ExitProcess(CHILD_DEATH);
}
static int restart(const efx_profile *profile, const char *name, bool native)
{
    char journal[OS_PATH_MAX]; editor_config cfg; render_backend b; editor *e=NULL;
    R(efx_profile_path(profile,name,journal,sizeof journal)==0);
    make_config(&cfg,journal);
    /* Oracle independent of the editor: the journal holds both inserts. */
    R(efx_journal_has_insert(journal,'a') && efx_journal_has_insert(journal,'b'));
    R(!efx_journal_has_insert(journal,'Z')); /* negative control: absent byte */
    int rc=(native ? efx_open_d3d11(&e,&cfg,&b) : efx_open_null(&e,&cfg,&b)) || efx_settle(e) ||
        efx_expect_bytes(e,"abbase\n",7);
    if(!rc) {
        /* Negative controls: the oracle must detect a missing, a wrong and a short byte. */
        fputs("editor_fixture_test: (expected) oracle self-check failures follow\n",stderr);
        void (*hook)(void)=efx_fail_hook; efx_fail_hook=NULL;
        rc=!(efx_expect_bytes(e,"abase\n",6) && efx_expect_bytes(e,"abXbase\n",8) &&
            efx_expect_bytes(e,"abbase",6) && efx_expect_bytes(e,"abbase\n\n",8) &&
            efx_expect_bytes(e,"abbasX\n",7));
        efx_fail_hook=hook;
    }
    if(e) editor_close(e);
    return rc;
}
static int parent(void)
{
    efx_profile profile;
    R(efx_profile_create(&profile,"editor-fixture")==0);
    int rc=efx_child_run("--null",profile.root,1u,CHILD_DEATH) || /* one whole-child phase */
        efx_child_run("--d3d11",profile.root,1u,CHILD_DEATH) ||
        restart(&profile,"null.journal",false) || restart(&profile,"d3d11.journal",true);
    if(!rc) {
        /* Watchdog controls: a wrong expected exit code, and a child that never
         * exits within its window, must both fail the helper. */
        fputs("editor_fixture_test: (expected) child-watchdog failures follow\n",stderr);
        rc=!(efx_child_run("--null",profile.root,1u,CHILD_DEATH+1u)!=0);
    }
    /* Cleanup is confined to sublimite-test-*: a foreign name is refused. */
    efx_profile foreign={.owner=true}; snprintf(foreign.root,sizeof foreign.root,"%s","C:\\no-such\\other-dir");
    R(!efx_owned_name(foreign.root));
    char gone[OS_PATH_MAX]; snprintf(gone,sizeof gone,"%s",profile.root);
    efx_profile_destroy(&profile);
    os_stat st; R(os_stat_path(gone,OS_STAT_NOFOLLOW,&st)!=OS_OK);
    if(rc) return 1;
    puts("editor_fixture_test: private profile, typing (inject and posted), completion, abrupt kill, restart, exact bytes PASS");
    puts("SKIP (needs live desktop: real present timing; null-backend completion is simulated, D3D11 completion is a CPU submit)");
    return 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    (void)SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    trace_init(); R(trace_thread_register()>=0);
    if(argc==3 && !strcmp(argv[1],"--null")) return child_type(argv[2],false);
    if(argc==3 && !strcmp(argv[1],"--d3d11")) return child_type(argv[2],true);
    return parent();
}
