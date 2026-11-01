/* Production Win32 -> editor -> D3D11 path on an occluded private station. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wtsapi32.h>
#include "editor/private.h"
#include "d3d11/d3d11.h"
#include "win32/private.h"
#include "trace/trace.h"
#define T(x) do { if(!(x)) { fprintf(stderr,"editor_win:%d FAIL %s\n",__LINE__,#x); return 1; } } while(0)
/* Counts match Linux allocation_case: alternating insertion/backspace. */
#define KEYS 10000u
#define WATCH_NS UINT64_C(5000000000)
typedef struct samples {
    uint64_t ingress, costs[KEYS];
    size_t count, allocations;
    bool active;
} samples;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    samples *s=ctx; (void)sequence; s->ingress=ns;
    if(!s->active) { edit_malloc_guard_begin(); s->active=true; }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    samples *s=ctx;
    if(s->active) {
        s->allocations+=edit_malloc_guard_end(); s->active=false;
        if(s->count<KEYS) s->costs[s->count++]=frame->submit_ns-s->ingress;
    }
}
static int compare(const void *a, const void *b)
{ uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return (x>y)-(x<y); }
static int settle(editor *e)
{
    uint64_t end=os_now_ns()+WATCH_NS;
    HWND window=plat_native_win32(&e->platform);
    uintptr_t token=(uintptr_t)GetPropW(window,L"SublimiteTestBarrier")+1u;
    T(PostMessageW(window,WM_APP+0x53,token,0));
    while((uintptr_t)GetPropW(window,L"SublimiteTestBarrier")!=token) {
        T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield();
    }
    do {
        int rc=editor_step(e,0); T(rc>=0);
        editor_stats state=editor_get_stats(e);
        if(!state.pending && !state.render_active && !state.font_pending) return 0;
        os_yield();
    } while(os_now_ns()<end);
    fprintf(stderr,"settle timeout dirty=%d active=%d queue=%zu\n",e->dirty,e->backend->active,e->queue_count);
    return 1;
}
static int wait_mutation(editor *e, uint64_t target)
{
    uint64_t end=os_now_ns()+WATCH_NS;
    do { T(editor_step(e,0)>=0); if(editor_get_stats(e).mutations>=target) return settle(e); os_yield(); } while(os_now_ns()<end);
    fprintf(stderr,"mutation timeout target=%llu actual=%llu\n",(unsigned long long)target,(unsigned long long)editor_get_stats(e).mutations);
    return 1;
}
static void cleanup(const char *path)
{
    os_dir dir=OS_DIR_INIT; os_dir_iter it;
    if(os_dir_open(&dir,path)!=OS_OK) return;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) {
            char child[OS_PATH_MAX]; snprintf(child,sizeof child,"%s\\%s",path,name);
            os_stat st;
            if(os_stat_path(child,OS_STAT_NOFOLLOW,&st)==OS_OK && st.kind==OS_KIND_DIR) cleanup(child);
            else (void)os_unlink(child);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); (void)os_rmdir(path);
}
typedef struct auto_source { editor_wait_set set; plat platform; bool seen; } auto_source;
static void auto_source_ready(void *ctx)
{
    auto_source *source=ctx; uint32_t tag=0; size_t count=0;
    source->seen=editor_wait_ready(&source->set,100,&tag,1,&count)==OS_OK &&
        count==1 && tag==EDITOR_WAIT_D3D11;
    plat_quit(&source->platform);
}
static int auto_source_test(void)
{
    auto_source source={0}; T(editor_wait_init(&source.set)==OS_OK);
    HANDLE event=CreateEventW(NULL,FALSE,TRUE,NULL); T(event);
    T(editor_wait_add_handle(&source.set,event,EDITOR_WAIT_D3D11)==OS_OK);
    plat_config cfg={.width=80,.height=80,.work_sources=editor_wait_handles,
        .work_sources_ctx=&source.set,.work_consumed=editor_wait_consumed};
    T(plat_init_backend(&source.platform,&cfg,PLAT_BACKEND_WIN32)==PLAT_OK);
    plat_callbacks cb={.ud=&source,.on_work=auto_source_ready};
    T(plat_run_for(&source.platform,&cb,(int)(WATCH_NS/UINT64_C(1000000)))==PLAT_OK);
    T(source.seen && WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    plat_shutdown(&source.platform); editor_wait_close(&source.set); CloseHandle(event);
    puts("editor_win: auto-reset readiness survives native wait until owner dispatch PASS");
    return 0;
}
int main(void)
{
    (void)SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    trace_init(); T(trace_thread_register()>=0);
    T(auto_source_test()==0);
    char temp[OS_PATH_MAX],root[OS_PATH_MAX],journal_path[OS_PATH_MAX]; unsigned random;
    T(os_temp_dir(temp,sizeof temp)==OS_OK && os_random(&random,sizeof random)==OS_OK);
    snprintf(root,sizeof root,"%s\\sublimite-test-editor-path-%x",temp,random);
    T(os_mkdir(root)==OS_OK && os_env_set("SUBLIMITE_PROFILE",root)==OS_OK);
    T(os_env_set("SUBLIMITE_TEST_INPUT","1")==OS_OK);
    render_backend b={0}; T(render_d3d11_backend(&b)==RENDER_OK);
    snprintf(journal_path,sizeof journal_path,"%s\\typing.journal",root);
    samples measurements={0};
    editor_config cfg={.initial=(const uint8_t *)"base\n",.initial_len=5,.cols=80,.rows=24,
        .journal_path=journal_path,.platform_backend=PLAT_BACKEND_WIN32,.font_dpi=96,.font_points=12,
        .hook_ctx=&measurements,.on_ingress=ingress,.on_submit=submitted};
    editor *e=NULL; T(editor_open(&e,&cfg,&b)==0);
    HWND window=plat_native_win32(&e->platform);
    T(PostMessageW(window,WM_SIZE,SIZE_MINIMIZED,MAKELPARAM(640,480)));
    T(settle(e)==0); T(!b.active && e->win_unavailable);
    uint64_t before=editor_get_stats(e).poll_returns;
    T(editor_step(e,100)>=0);
    T(editor_get_stats(e).poll_returns==before+1u);
    puts("editor_win: minimized/occluded idle has only test-deadline wake PASS");
    /* Pointer-bearing WM_DPICHANGED uses a synchronous bounded SendMessage:
     * Windows rejects posting system messages that carry a pointer. */
    RECT rect={0,0,640,480}; DWORD_PTR sent;
    T(SendMessageTimeoutW(window,WM_DPICHANGED,MAKEWPARAM(144,144),(LPARAM)&rect,
        SMTO_ABORTIFHUNG,(UINT)(WATCH_NS/UINT64_C(1000000)),&sent));
    T(settle(e)==0);
    T(e->font.dpi==144.0 && e->font.applied_px==editor_runtime_em_px(144,12));
    T(PostMessageW(window,WM_SIZE,SIZE_RESTORED,MAKELPARAM(640,480)));
    T(PostMessageW(window,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0)); T(settle(e)==0);
    T(win32_plat_suspended(&e->platform) && e->win_unavailable);
    T(PostMessageW(window,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0)); T(settle(e)==0);
    T(!win32_private(&e->platform)->session_locked);
    T(PostMessageW(window,WM_POWERBROADCAST,PBT_APMSUSPEND,0)); T(settle(e)==0);
    T(win32_plat_suspended(&e->platform) && e->win_unavailable);
    T(PostMessageW(window,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0)); T(settle(e)==0);
    T(!win32_plat_suspended(&e->platform));
    struct { GUID guid; DWORD length, state; } display={GUID_CONSOLE_DISPLAY_STATE,sizeof(DWORD),0};
    T(SendMessageTimeoutW(window,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,(LPARAM)&display,
        SMTO_ABORTIFHUNG,(UINT)(WATCH_NS/UINT64_C(1000000)),&sent));
    T(settle(e)==0 && win32_private(&e->platform)->power_off);
    display.state=1;
    T(SendMessageTimeoutW(window,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,(LPARAM)&display,
        SMTO_ABORTIFHUNG,(UINT)(WATCH_NS/UINT64_C(1000000)),&sent));
    T(settle(e)==0 && !win32_private(&e->platform)->power_off);
    T(PostMessageW(window,WM_SIZE,SIZE_MINIMIZED,MAKELPARAM(640,480))); T(settle(e)==0);
    puts("editor_win: DPI metrics, minimize/restore, session lock/unlock, power suspend/resume PASS");
    /* Positive control: the release guard must observe an actual allocation. */
    edit_malloc_guard_begin();
    void *(*volatile allocate)(size_t)=malloc; void *control=allocate(1);
    size_t positive=edit_malloc_guard_end(); free(control);
    if(edit_malloc_guard_active()) T(positive>0);
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
    T(edit_malloc_guard_active());
#endif
    /* Warm both real native key paths before starting the sample campaign. */
    uint64_t warm=editor_get_stats(e).mutations;
    for(unsigned i=0;i<2;i++) {
        T(PostMessageW(window,i ? WM_KEYDOWN : WM_CHAR,i ? VK_BACK : 'x',1));
        if(i) T(PostMessageW(window,WM_KEYUP,VK_BACK,1));
        T(wait_mutation(e,warm+i+1u)==0);
    }
    measurements=(samples){0};
    uint64_t mutations=editor_get_stats(e).mutations;
    uint64_t records=editor_get_stats(e).journal_records;
    for(unsigned i=0;i<KEYS;i++) {
        /* Include native message decoding as well as mutation/layout/D3D11.
         * ingress must not reset this earlier counting interval. */
        edit_malloc_guard_begin(); measurements.active=true;
        if(i%2u) {
            T(PostMessageW(window,WM_KEYDOWN,VK_BACK,1)); T(PostMessageW(window,WM_KEYUP,VK_BACK,1));
        } else T(PostMessageW(window,WM_CHAR,'x',1));
        T(wait_mutation(e,mutations+i+1u)==0);
    }
    T(measurements.count==KEYS && !measurements.active && measurements.allocations==0);
    T(editor_get_stats(e).mutations==mutations+KEYS && editor_length(e)==5);
    T(editor_get_stats(e).journal_records==records+KEYS && !editor_get_stats(e).journal_error);
    qsort(measurements.costs,KEYS,sizeof measurements.costs[0],compare);
    printf("editor_win: (M)[AC, desktop Spiral] keys=%u journal_records=keys mallocs=%zu guard=%s p50_ns=%llu p99_ns=%llu "
        "derivation=submit_ns-ingress_ns; nearest-rank=ceil(N*pct/100); N=Linux allocation_case; "
        "surface=occluded (CPU submit, no T4/T6); Linux has no percentile G1 assertion\n",
        KEYS,measurements.allocations,edit_malloc_guard_active()?"active":"ASan-inert",
        (unsigned long long)measurements.costs[(KEYS*50u+99u)/100u-1u],
        (unsigned long long)measurements.costs[(KEYS*99u+99u)/100u-1u]);
    /* (G) Linux bench/editor_bench.c G1_P50/G1_P99, derived in perf §0.2.
     * These apply at native T4, which an occluded surface cannot establish. */
    puts("SKIP (needs live desktop: native T4 G1; Linux limits (G) p50=1000000 ns/p99=2000000 ns, perf/01-perf-target.md section 0.2)");
    puts("SKIP (needs live desktop: N edits give N frames)");
    editor_close(e); cleanup(root);
    return 0;
}
