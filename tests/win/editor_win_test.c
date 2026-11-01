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
#include "test_isolation.h"
/* Same native call seam as win32_plat_test; no production fault switch. */
static volatile LONG fail_clip, synthetic_publication;
static BOOL publication_empty(void)
{ return InterlockedCompareExchange(&synthetic_publication,0,0) ? TRUE : EmptyClipboard(); }
static BOOL publication_close(void)
{ return InterlockedCompareExchange(&synthetic_publication,0,0) ? TRUE : CloseClipboard(); }
static HANDLE publish_entered, publish_release;
static HANDLE stalled_publish(UINT format,HANDLE memory)
{
    if(publish_entered) {
        SetEvent(publish_entered);
        WaitForSingleObject(publish_release,INFINITE);
    }
    if(InterlockedCompareExchange(&synthetic_publication,0,0)) {
        /* This API seam models a late native success. It owns/frees the test
         * HGLOBAL instead of transferring it to the OS clipboard. */
        GlobalFree(memory); return (HANDLE)(uintptr_t)1u;
    }
    return SetClipboardData(format,memory);
}
static BOOL cut_open(HWND window)
{
    if(InterlockedCompareExchange(&fail_clip,0,0)) return FALSE;
    return InterlockedCompareExchange(&synthetic_publication,0,0) ? TRUE : OpenClipboard(window);
}
#define EmptyClipboard publication_empty
#define CloseClipboard publication_close
#define SetClipboardData stalled_publish
#define OpenClipboard cut_open
#include "../../src/win32/win32_clip_win.c"
#undef OpenClipboard
#undef SetClipboardData
#undef EmptyClipboard
#undef CloseClipboard
#include <d3d11.h>
#include <dwmapi.h>
static bool fail_native_alloc;
static HRESULT WINAPI allocation_device(IDXGIAdapter *adapter,D3D_DRIVER_TYPE type,HMODULE module,
    UINT flags,const D3D_FEATURE_LEVEL *levels,UINT count,UINT sdk,ID3D11Device **device,
    D3D_FEATURE_LEVEL *level,ID3D11DeviceContext **context)
{
    if(fail_native_alloc) return E_OUTOFMEMORY;
    return D3D11CreateDevice(adapter,type,module,flags,levels,count,sdk,device,level,context);
}
/* A non-interactive station refuses visible windows. Only this renderer
 * seam exercises an undrawn visible-surface latency wait and offscreen draw;
 * it supplies no displayed-frame completion. */
static bool force_paint_visibility, force_cloaked;
static BOOL paint_visible(HWND window)
{ return force_paint_visibility ? TRUE : IsWindowVisible(window); }
static HRESULT WINAPI paint_attribute(HWND window,DWORD attribute,PVOID value,DWORD bytes)
{
    if(force_paint_visibility && attribute==DWMWA_CLOAKED && bytes==sizeof(DWORD)) {
        *(DWORD *)value=force_cloaked ? 1u : 0u; return S_OK;
    }
    return DwmGetWindowAttribute(window,attribute,value,bytes);
}
#define D3D11CreateDevice allocation_device
#define IsWindowVisible paint_visible
#define DwmGetWindowAttribute paint_attribute
#include "../../src/d3d11/d3d11_win.c"
#undef D3D11CreateDevice
#undef IsWindowVisible
#undef DwmGetWindowAttribute
static bool refuse_clear, crash_status, observe_status, status_guard_seen, snapshot_guard_seen;
static bool hold_save_fence;
static bool test_save_leased(const editor *e)
{ return hold_save_fence || editor_save_leased(e); }
static editor *cpu_editor;
static int snapshot_probe(render_backend *b,const render_grid *g,const render_strip *strips,size_t count)
{
    if(observe_status && cpu_editor) snapshot_guard_seen=cpu_editor->journal_paint==cpu_editor->buffer->id;
    int rc=render_backend_submit(b,g,strips,count);
    return rc;
}
static void status_probe(editor *e)
{
    cpu_editor=e;
    if(observe_status) status_guard_seen=e->journal_paint==e->buffer->id;
    if(crash_status) ExitProcess(73); /* (P) private child crash sentinel. */
    editor_status_overlay(e);
}
static void clear_guard(editor *e) { if(!refuse_clear) editor_journal_paint_clear(e); }
#define NATIVE_TURN_WORK UINT32_C(0x4d4f444c) /* Test message discriminator. */
static bool native_work_received;
static int native_work_event(render_backend *backend,const render_event *ev)
{
    if(ev->kind==RENDER_EVENT_WORK && ev->work && ev->work->kind==NATIVE_TURN_WORK) {
        native_work_received=true; return 0;
    }
    return render_backend_event(backend,ev);
}
#define editor_save_leased test_save_leased
#define render_backend_submit snapshot_probe
#define editor_status_overlay status_probe
#define editor_journal_paint_clear clear_guard
#define render_backend_event native_work_event
#include "../../src/editor/editor.c"
#undef render_backend_event
#undef editor_journal_paint_clear
#undef editor_status_overlay
#undef render_backend_submit
#undef editor_save_leased
/* Observe shutdown's actual mailbox transfer, independently of its handle. */
static _Atomic unsigned shutdown_sent;
static unsigned shutdown_received;
static bool observed_publish(work_ctx *ctx,const work_msg *msg)
{
    bool ok=work_publish(ctx,msg);
    if(ok && msg->kind==UINT32_C(0x45445344)) atomic_fetch_add(&shutdown_sent,1u);
    return ok;
}
typedef struct result_observer { void (*cb)(const work_msg *,void *); void *ctx; } result_observer;
static void observe_result(const work_msg *msg,void *ctx)
{
    result_observer *o=ctx;
    if(msg->kind==UINT32_C(0x45445344)) shutdown_received++;
    o->cb(msg,o->ctx);
}
static size_t observed_receive(work_pool *pool,work_handle handle,uint32_t generation,
    void (*cb)(const work_msg *,void *),void *ctx)
{
    result_observer observer={cb,ctx};
    return work_mailbox_receive(pool,handle,generation,observe_result,&observer);
}
#define work_publish observed_publish
#define work_mailbox_receive observed_receive
#include "../../src/editor/backend.c"
#undef work_publish
#undef work_mailbox_receive
#define T(x) do { if(!(x)) { fprintf(stderr,"editor_win:%d FAIL %s\n",__LINE__,#x); if(publish_release) SetEvent(publish_release); return 1; } } while(0)
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
/* Terminal publication and native control must survive a completely full FIFO. */
static int queue_cut_test(editor *e)
{
    T(settle(e)==0);
    e->v.state.selection=(view_selection){.anchor=0,.cursor=4};
    plat_event cut={.kind=PLAT_EV_KEY,.press=true,.keysym='x',.mods=PLAT_MOD_CTRL};
    InterlockedExchange(&fail_clip,1);
    T(editor_clip_action(e,KEYS_ACTION_CUT,&cut)==0);
    plat_event key={.kind=PLAT_EV_KEY,.press=false};
    for(size_t i=0;i<EDITOR_INPUT_CAP;i++) T(editor_inject(e,&key)==0);
    T(e->queue_count==EDITOR_INPUT_CAP);
    HWND window=plat_native_win32(&e->platform);
    uintptr_t token=(uintptr_t)GetPropW(window,L"SublimiteTestBarrier")+1u;
    T(PostMessageW(window,WM_APP+0x53,token,0));
    uint64_t end=os_now_ns()+WATCH_NS;
    while(editor_clip_busy(e) || (uintptr_t)GetPropW(window,L"SublimiteTestBarrier")!=token) {
        T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield();
    }
    T(editor_length(e)==5 && e->recovery_notice);
    InterlockedExchange(&fail_clip,0);
    T(settle(e)==0);
    puts("round3: full input FIFO still pumps clipboard terminal and native control PASS"); return 0;
}
static int deadline_test(editor *e)
{
    T(settle(e)==0);
    plat_event expose={.kind=PLAT_EV_EXPOSE};
    e->journal_metadata_pending=true;
    T(editor_inject(e,&expose)==0); T(queue_pick(e)==SIZE_MAX);
    e->win_unavailable=true; e->win_retry_ns=os_now_ns()-1u;
    (void)d3d11_set_suspended(e->backend,true);
    e->journal_metadata_pending=false;
    T(nonkey_dispatch(e,&e->queue[e->queue_head])==0); queue_remove(e,0);
    /* Native suspension wins over EXPOSE, including a station's display-off
     * notification racing startup. It owes no expired probe deadline. */
    bool suspended=win32_plat_suspended(&e->platform);
    T(e->win_unavailable==suspended);
    if(!suspended) T(!e->win_retry_ns);
    if(suspended) {
        T(windows_surface(e)==0 && !e->win_retry_ns);
        win32_plat *native=win32_private(&e->platform);
        printf("round4: deadline fixture suspended minimized=%d lock=%d disconnected=%d power=%d\n",
            native->minimized,native->session_locked,native->session_disconnected,native->power_off);
        /* Native seam: also exercise the awake state independently of station
         * power notifications, without changing the fixture's watchdog. */
        bool minimized=native->minimized, locked=native->session_locked;
        bool disconnected=native->session_disconnected, power=native->power_off;
        native->minimized=native->session_locked=native->session_disconnected=native->power_off=false;
        T(nonkey_dispatch(e,&expose)==0);
        T(!e->win_unavailable && !e->win_retry_ns);
        native->minimized=minimized; native->session_locked=locked;
        native->session_disconnected=disconnected; native->power_off=power;
    }
    T(settle(e)==0);
    e->win_retry_ns=os_now_ns()-1u; e->win_unavailable=false;
    T(wait_timeout(e,100,os_now_ns())>0); e->win_retry_ns=0;
    puts("round3: expose behind journal fence clears expired deadline PASS"); return 0;
}
static int idle_probe_test(editor *e)
{
    T(settle(e)==0);
    /* Seam-confirmed last frame with no occlusion/device/suspension since. */
    force_paint_visibility=true;
    win32_plat *platform=win32_private(&e->platform);
    bool power=platform->power_off; platform->power_off=false;
    e->win_surface_valid=true; e->win_unavailable=false;
    (void)d3d11_set_suspended(e->backend,false);
    e->win_frame_owed=false; e->win_retry_ns=0;
    T(windows_surface(e)==0); T(e->win_retry_ns==0);
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) T(windows_surface(e)==0 && !e->win_retry_ns);
    force_paint_visibility=false;
    T(editor_full_layout(e)==0 && settle(e)==0);
    e->win_frame_owed=true;
    /* No native event is delivered between forcing expiry and retry. */
    e->win_unavailable=true; (void)d3d11_set_suspended(e->backend,true);
    e->win_retry_ns=os_now_ns()-1u;
    T(windows_surface(e)==0 && !e->win_unavailable && !e->win_retry_ns);
    e->win_retry_delay_ns=D3D11_PARK_PROBE_NS;
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) {
        e->win_unavailable=true; (void)d3d11_set_suspended(e->backend,true);
        e->win_retry_ns=os_now_ns()-1u;
        T(windows_surface(e)==0 && e->win_retry_delay_ns<=D3D11_PARK_PROBE_MAX_NS);
    }
    T(e->win_retry_delay_ns==D3D11_PARK_PROBE_MAX_NS);
    e->win_frame_owed=false; T(windows_surface(e)==0 && e->win_retry_ns);
    platform->power_off=power;
    puts("round4: confirmed valid idle has zero deadlines; uncertain/owed frames retry without events PASS"); return 0;
}
static editor *drawing_editor;
static ID3D11DeviceContextVtbl *drawing_original;
static bool drawing_guard_seen;
static void WINAPI guarded_instances(ID3D11DeviceContext *context,UINT vertices,UINT instances,UINT first,UINT start)
{
    drawing_guard_seen=drawing_editor->journal_paint==drawing_editor->active_frame.source_id;
    context->lpVtbl=drawing_original;
    drawing_original->DrawInstanced(context,vertices,instances,first,start);
}
static int undrawn_guard_test(editor *e)
{
    T(settle(e)==0);
    HWND window=plat_native_win32(&e->platform);
    (void)ShowWindow(window,SW_SHOWNOACTIVATE);
    T(!IsIconic(window));
    /* PAINT only guards a recorded, recoverable tab. A pristine unnamed
     * fixture is deliberately absent from journal tab metadata. */
    uint64_t before=e->stats.mutations;
    T(PostMessageW(plat_native_win32(&e->platform),WM_APP+0x51,0,0));
    T(PostMessageW(plat_native_win32(&e->platform),WM_CHAR,'g',1));
    T(wait_mutation(e,before+1u)==0);
    win32_plat *platform=win32_private(&e->platform);
    platform->power_off=platform->session_locked=platform->session_disconnected=platform->minimized=false;
    e->buffer->painted=false; e->buffer->win_composed=false; e->buffer->win_draw_safe=false;
    T(!IsIconic(window));
    d3d11_state *native=e->backend->state;
    HANDLE saved=native->latency, blocked=CreateEventW(NULL,FALSE,FALSE,NULL); T(blocked);
    force_paint_visibility=true;
    native->latency=blocked; native->latency_ready=false;
    e->win_unavailable=false; T(d3d11_set_suspended(e->backend,false)==0);
    T(editor_full_layout(e)==0);
    while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
    T(editor_paint_prepare(e)==0);
    uint64_t end=os_now_ns()+WATCH_NS;
    observe_status=true; status_guard_seen=snapshot_guard_seen=false;
    while(e->dirty) {
        while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
        if(e->full_pending) T(editor_full_layout(e)==0);
        T(editor_paint_prepare(e)==0);
        T(submit(e)==0);
        if(e->dirty) T(editor_flush(e)==0);
        if(os_now_ns()>=end) fprintf(stderr,"submit blocked paint=%d full=%d layout=%d clip=%d active=%d ops=%zu action=%d extra=%d\n",e->paint_ready,e->full_pending,layout_busy(&e->lay),editor_clip_busy(e),e->backend->active,e->op_count,e->action,e->extra_rows);
        T(os_now_ns()<end);
    }
    observe_status=false; T(status_guard_seen && snapshot_guard_seen);
    T(e->backend->active && !native->drawn && !e->journal_paint);
    uint64_t records=journal_get_stats(e->journal).accepted_sequence;
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) {
        /* This oracle is guard growth, independently of the missing-latency
         * watchdog; slow native RPCs on an inactive station may outlast it. */
        native->deadline=os_now_ns()+WATCH_NS;
        T(present(e)==0); T(!e->journal_paint && e->backend->active);
    }
    T(journal_get_stats(e->journal).accepted_sequence==records);
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) {
        T(d3d11_discard_unpresented(e->backend)==0);
        e->win_unavailable=true;
        T(windows_retry(e)==0);
        while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
        T(editor_paint_prepare(e)==0);
        while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
        T(submit(e)==0);
        T(!e->dirty && e->backend->active && !e->journal_paint);
    }
    T(journal_get_stats(e->journal).accepted_sequence==records);
    /* New glyph/source work before any presentation requires a new guard. */
    T(d3d11_discard_unpresented(e->backend)==0);
    T(e->buffer->win_composed);
    T(editor_full_layout(e)==0);
    observe_status=true; status_guard_seen=snapshot_guard_seen=false;
    end=os_now_ns()+WATCH_NS;
    do {
        while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
        if(e->full_pending) T(editor_full_layout(e)==0);
        T(editor_paint_prepare(e)==0 && submit(e)==0);
        if(e->dirty) T(editor_flush(e)==0);
        T(os_now_ns()<end);
    } while(e->dirty);
    observe_status=false; T(status_guard_seen && snapshot_guard_seen);
    native->latency=saved; CloseHandle(blocked);
    T(editor_flush(e)==0); /* Return protected PAINT credit before native draw. */
    drawing_editor=e; drawing_guard_seen=false; e->buffer->win_draw_safe=false;
    drawing_original=native->context->lpVtbl;
    ID3D11DeviceContextVtbl guarded=*drawing_original; guarded.DrawInstanced=guarded_instances;
    native->context->lpVtbl=&guarded; native->latency_ready=true;
    native->deadline=os_now_ns()+WATCH_NS;
    ID3D11DeviceContext *instrumented=native->context;
    int draw_rc=present(e);
    if(native->context==instrumented) native->context->lpVtbl=drawing_original;
    T(draw_rc==0);
    T(drawing_guard_seen && !e->journal_paint);
    force_paint_visibility=false;
    T(settle(e)==0);
    plat_event undo={.kind=PLAT_EV_KEY,.press=true,.keysym='z',.mods=PLAT_MOD_CTRL};
    T(editor_inject(e,&undo)==0 && settle(e)==0);
    puts("round3: covered non-minimized undrawn latency waits leave no PAINT or journal growth PASS"); return 0;
}
static IDXGISwapChain2Vtbl *resize_original;
static HRESULT WINAPI no_resize_memory(IDXGISwapChain2 *chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags)
{
    (void)count; (void)width; (void)height; (void)format; (void)flags;
    chain->lpVtbl=resize_original;
    return E_OUTOFMEMORY;
}
static int resize_failure_test(editor *e)
{
    T(settle(e)==0);
    d3d11_state *native=e->backend->state;
    T(native->swapchain);
    resize_original=native->swapchain->lpVtbl;
    IDXGISwapChain2Vtbl failing=*resize_original; failing.ResizeBuffers=no_resize_memory;
    native->swapchain->lpVtbl=&failing;
    /* A smaller valid client rectangle still makes DXGI allocate buffers. */
    native->width+=e->grid.dims.cell_w;
    T(editor_resize_grid(e,e->grid.dims)==0);
    T(!native->failed && e->recovery_notice);
    T(editor_step(e,0)>=0 && settle(e)==0);
    puts("round3: ResizeBuffers allocation failure stays alive with notice PASS"); return 0;
}
static int allocation_failure_test(editor *e)
{
    T(settle(e)==0);
    render_config cfg=e->backend->config; void *state=e->backend->state;
    editor_backend_shutdown_sync(e,e->backend);
    fail_native_alloc=true;
    int rc=editor_backend_init_sync(e,cfg,state);
    fail_native_alloc=false;
    T(rc==0 && e->backend->initialized && d3d11_native_deferred(e->backend));
    T(editor_step(e,0)>=0 && e->win_unavailable && e->recovery_notice);
    e->win_frame_owed=true; e->win_retry_ns=os_now_ns()-1u;
    T(windows_surface(e)==0 && !d3d11_native_deferred(e->backend) && !d3d11_native_failed(e->backend));
    T(editor_full_layout(e)==0 && settle(e)==0);
    T(!e->error);
    puts("round3: native allocation failure preserves CPU editing, parks with notice, retries PASS"); return 0;
}
static int larger_surface_test(editor *e)
{
    T(settle(e)==0);
    HWND window=plat_native_win32(&e->platform);
    /* Reduce the reservation's admissible client bound to avoid a giant test
     * allocation; restored rectangle is one existing cell larger in each axis. */
    e->font.max_width=e->font.window_width; e->font.max_height=e->font.window_height;
    d3d11_state *native=e->backend->state;
    native->max_width=e->font.max_width; native->max_height=e->font.max_height;
    RECT rect={0,0,(LONG)(e->font.max_width+e->grid.dims.cell_w),(LONG)(e->font.max_height+e->grid.dims.cell_h)};
    T(AdjustWindowRectEx(&rect,(DWORD)GetWindowLongPtrW(window,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(window,GWL_EXSTYLE)));
    T(SetWindowPos(window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    T(settle(e)==0);
    RECT client; T(GetClientRect(window,&client));
    T((uint32_t)client.right<=e->font.max_width && (uint32_t)client.bottom<=e->font.max_height);
    T(!e->error && !native->failed);
    puts("round3: larger restored client constrained to reserved surface; editor stays alive PASS"); return 0;
}
typedef struct cut_replay { uint64_t deletes; bool open; } cut_replay;
static int cut_record(void *ctx,const journal_record *record)
{
    cut_replay *r=ctx;
    if(record->type==JOURNAL_GROUP_BEGIN) r->open=true;
    if(record->type==JOURNAL_GROUP_COMMIT) r->open=false;
    if(record->type==JOURNAL_DELETE && record->size==16) { uint64_t n; memcpy(&n,record->data+8,sizeof n); r->deletes+=n; }
    return 0;
}
static int close_large_test(editor *e)
{
    T(settle(e)==0);
    uint64_t total=editor_length(e); T(total>VIEW_MUTATION_BOUND);
    /* Exhaust the inherited protected-sync window, independently of cut
     * slice size. CLOSE must schedule the flush that returns this credit. */
    int credit;
    do { credit=journal_append_protected(e->journal,JOURNAL_PAINT,0,NULL,0); } while(!credit);
    T(credit==JOURNAL_BUSY);
    e->v.state.selection=(view_selection){.anchor=0,.cursor=total-1u};
    plat_event cut={.kind=PLAT_EV_KEY,.press=true,.keysym='x',.mods=PLAT_MOD_CTRL};
    T(editor_clip_action(e,KEYS_ACTION_CUT,&cut)==0);
    plat_event close={.kind=PLAT_EV_CLOSE}; T(editor_inject(e,&close)==0);
    uint64_t end=os_now_ns()+WATCH_NS; int rc;
    do {
        rc=editor_step(e,0); T(rc>=0);
        if(os_now_ns()>=end) {
            /* Permit failing-test cleanup to release its own native objects. */
            T(journal_flush(e->journal)==0); T(os_now_ns()<end);
        }
        os_yield();
    } while(rc!=EDITOR_CLOSED);
    T(editor_length(e)==1 && e->v.state.selection.cursor==0 && !e->explicit_group && !e->journal_group_open);
    T(OpenClipboard(NULL)); HANDLE data=GetClipboardData(CF_UNICODETEXT); T(data);
    WCHAR *text=GlobalLock(data); T(text && wcslen(text)==total-1u);
    for(size_t i=0;i<(size_t)total-1u;i++) T(text[i]=='a');
    GlobalUnlock(data); T(CloseClipboard());
    cut_replay replay={0}; journal_replay_result result;
    T(journal_replay_file(e->cfg.journal_path,cut_record,&replay,&result)==0);
    T(replay.deletes==total-1u && !replay.open);
    puts("round3: large cut closes only after selection/undo/journal commit; clipboard and replay exact PASS"); return 0;
}
static int cut_test(editor *e)
{
    T(test_isolation_query()==TEST_ISOLATION_STATION);
    T(settle(e)==0);
    e->v.state.selection=(view_selection){.anchor=0,.cursor=4};
    plat_event cut={.kind=PLAT_EV_KEY,.press=true,.keysym='x',.mods=PLAT_MOD_CTRL};
    InterlockedExchange(&fail_clip,1);
    T(editor_clip_action(e,KEYS_ACTION_CUT,&cut)==0);
    T(settle(e)==0);
    uint8_t bytes[5]; T(editor_length(e)==5 && editor_read(e,0,bytes,5)==0);
    T(!memcmp(bytes,"base\n",5) && e->v.state.selection.anchor==0 && e->v.state.selection.cursor==4);
    T(e->recovery_notice && strstr(e->recovery_notice,"clipboard"));
    InterlockedExchange(&fail_clip,0);
    T(editor_clip_action(e,KEYS_ACTION_CUT,&cut)==0); T(settle(e)==0);
    T(editor_length(e)==1);
    e->v.state.selection=(view_selection){.anchor=0,.cursor=1};
    InterlockedExchange(&fail_clip,1);
    T(editor_clip_action(e,KEYS_ACTION_CUT,&cut)==0);
    plat_event close={.kind=PLAT_EV_CLOSE}; T(editor_inject(e,&close)==0);
    uint64_t end=os_now_ns()+WATCH_NS;
    while(e->win_close_requested || editor_clip_pending(e)) { T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield(); }
    T(editor_length(e)==1 && e->recovery_notice && !editor_clip_pending(e));
    InterlockedExchange(&fail_clip,0);
    puts("round2: close waits for queued publication; failure cancels close with text intact PASS");
    puts("round2: identified clipboard failure preserves text/selection, success cuts PASS");
    return 0;
}
/* Native publication stays blocked until this fixture explicitly releases it. */
static int publication_start(editor *e)
{
    publish_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    publish_release=CreateEventW(NULL,TRUE,FALSE,NULL); T(publish_entered && publish_release);
    e->v.state.selection=(view_selection){.anchor=0,.cursor=4};
    plat_event cut={.kind=PLAT_EV_KEY,.press=true,.keysym='x',.mods=PLAT_MOD_CTRL};
    T(editor_inject(e,&cut)==0);
    uint64_t end=os_now_ns()+WATCH_NS; unsigned retries=0;
    while(WaitForSingleObject(publish_entered,0)!=WAIT_OBJECT_0) {
        T(editor_step(e,0)>=0);
        if(!editor_clip_busy(e) && !e->queue_count) {
            /* Another isolated suite may occupy native publication first.
             * An acquisition failure correctly keeps text; retry admission
             * until this fixture reaches its foreign-call gate. Do not
             * confuse that earlier native failure with a blocked handler. */
            T(e->recovery_notice && strstr(e->recovery_notice,"publication failed"));
            T(editor_inject(e,&cut)==0); retries++;
        }
        T(os_now_ns()<end); os_yield();
    }
    if(retries) printf("round4: publication fixture native acquisition retries=%u\n",retries);
    return 0;
}
typedef struct close_records { bool deleted,typed,ordered,late; } close_records;
static int close_record(void *ctx,const journal_record *record)
{
    close_records *seen=ctx;
    if(record->type==JOURNAL_DELETE) seen->deleted=true;
    if(record->type==JOURNAL_INSERT && record->size==sizeof(uint64_t)+1u) {
        if(record->data[sizeof(uint64_t)]=='q') { seen->typed=true; seen->ordered=seen->deleted; }
        if(record->data[sizeof(uint64_t)]=='r') seen->late=true;
    }
    return 0;
}
static int close_queue_test(editor *e,bool prompt,unsigned fence)
{
    T(settle(e)==0);
    if(fence==1u) e->journal_metadata_pending=true;
    else if(fence==2u) hold_save_fence=true;
    else T(publication_start(e)==0);
    plat_event key={.kind=PLAT_EV_KEY,.press=true,.keysym='q',.utf8_len=1,.utf8={'q'}};
    T(editor_inject(e,&key)==0);
    plat_event close={.kind=prompt ? PLAT_EV_KEY : PLAT_EV_CLOSE,.press=true,.keysym='w',.mods=PLAT_MOD_CTRL};
    T(editor_inject(e,&close)==0);
    if(!prompt) { key.keysym='r'; key.utf8[0]='r'; T(editor_inject(e,&key)==0); }
    if(fence) { T(queue_pick(e)==SIZE_MAX); e->journal_metadata_pending=false; hold_save_fence=false; }
    else SetEvent(publish_release);
    uint64_t end=os_now_ns()+WATCH_NS; int rc=0;
    do {
        rc=editor_step(e,0); T(rc>=0); T(os_now_ns()<end); os_yield();
    } while(prompt ? !editor_close_prompt_shown(e) : rc!=EDITOR_CLOSED);
    uint8_t bytes[6]; size_t length=fence ? 6u : 2u;
    T(editor_length(e)==length && editor_read(e,0,bytes,length)==0);
    T(!memcmp(bytes,fence ? "qbase\n" : "q\n",length));
    if(prompt) { T(!e->quit && editor_close_prompt_shown(e)); e->close_prompt=(editor_close_prompt){0}; }
    T(editor_flush(e)==0);
    close_records records={0}; journal_replay_result replay;
    T(journal_replay_file(e->cfg.journal_path,close_record,&records,&replay)==0);
    T(records.typed && !records.late && (fence || records.ordered));
    printf("round4: close boundary %s accepted typing exact, late input excluded, prompt=%d PASS\n",fence==2u ? "save fence" : fence ? "journal fence" : "cut",prompt);
    return 0;
}
static int stuck_publication_test(editor *e)
{
    /* Isolate the deliberately stuck SetClipboardData from unrelated native
     * clipboard acquisition failures. Real acquire/publish behavior remains
     * covered by close_queue_test, cut_test and win32_plat_test. */
    InterlockedExchange(&synthetic_publication,1);
    T(settle(e)==0 && publication_start(e)==0);
    plat_event key={.kind=PLAT_EV_KEY,.press=true,.keysym='q',.utf8_len=1,.utf8={'q'}};
    /* Preserve the selection while pending, then move to its end before
     * typing: the failed cut must leave every original byte intact. */
    T(editor_length(e)==5);
    e->blinking=false;
    T(editor_clip_deadline(e)>os_now_ns());
    T(wait_timeout(e,-1,os_now_ns())>1);
    plat_event right={.kind=PLAT_EV_KEY,.press=true,.keysym=0xff53};
    T(editor_inject(e,&right)==0 && editor_inject(e,&key)==0);
    uint64_t end=os_now_ns()+WATCH_NS;
    while(editor_clip_busy(e) || e->queue_count) {
        T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield();
    }
    uint8_t bytes[6]; T(editor_length(e)==6 && editor_read(e,0,bytes,6)==0 && !memcmp(bytes,"baseq\n",6));
    T(e->recovery_notice && strstr(e->recovery_notice,"text kept"));
    /* A late native success must be ignored after the expired operation. */
    SetEvent(publish_release);
    win32_clip *transport=win32_private(&e->platform)->clip;
    end=os_now_ns()+WATCH_NS;
    for(;;) {
        AcquireSRWLockShared(&transport->lock); unsigned used=transport->used; ReleaseSRWLockShared(&transport->lock);
        if(!used) break;
        T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield();
    }
    T(settle(e)==0);
    CloseHandle(publish_entered); CloseHandle(publish_release);
    T(editor_length(e)==6 && editor_read(e,0,bytes,6)==0 && !memcmp(bytes,"baseq\n",6));
    /* Repeat with no release at all: close and native teardown must detach. */
    publish_entered=NULL; publish_release=NULL;
    T(publication_start(e)==0);
    plat_event close={.kind=PLAT_EV_CLOSE}; T(editor_inject(e,&close)==0);
    int rc;
    end=os_now_ns()+WATCH_NS;
    do { rc=editor_step(e,0); T(rc>=0); T(os_now_ns()<end); os_yield(); } while(rc!=EDITOR_CLOSED);
    T(editor_length(e)==6);
    puts("round4: stuck publication expires, typing/close proceed, late deletion refused PASS"); return 0;
}
static int cloak_test(editor *e)
{
    T(settle(e)==0);
    force_paint_visibility=true; force_cloaked=true;
    e->win_unavailable=false; T(d3d11_set_suspended(e->backend,false)==0);
    e->backend->active=true; ((d3d11_state *)e->backend->state)->drawn=false;
    e->win_frame_owed=true;
    T(windows_surface(e)==0);
    T(e->win_unavailable && !e->backend->active && e->win_retry_ns);
    e->blinking=false;
    T(wait_timeout(e,100,os_now_ns())==100);
    uint64_t before=e->stats.poll_returns; T(editor_step(e,100)>=0);
    T(e->stats.poll_returns<=before+1u);
    force_cloaked=false; e->win_retry_ns=os_now_ns()-1u;
    T(windows_surface(e)==0 && !e->win_unavailable && e->dirty);
    force_paint_visibility=false; T(settle(e)==0);
    puts("round4: cloaked pending frame parks without polling; eventless uncloak retries PASS"); return 0;
}
/* Synthetic native commit/authentication seams, never displayed-frame evidence. */
static int committed_frame(render_backend *backend,uint32_t frame)
{ (void)backend; (void)frame; return RENDER_OK; }
static int uncertain_surface_test(editor *e)
{
    T(settle(e)==0);
    force_paint_visibility=true;
    d3d11_state *native=e->backend->state;
    win32_private(&e->platform)->power_off=false;
    T(d3d11_set_suspended(e->backend,false)==0);
    e->win_unavailable=false; e->win_surface_valid=false;
    int (*original)(render_backend *,uint32_t)=e->backend->ops.present;
    e->backend->ops.present=committed_frame;
    e->backend->active=true; e->backend->presented=false; native->drawn=true;
    int committed=present(e); bool premature=e->win_surface_valid;
    e->backend->ops.present=original;
    /* T4 acceptance cannot authenticate intact displayed content. */
    e->backend->active=false; e->backend->presented=false; native->drawn=false;
    force_paint_visibility=false;
    T(committed==0 && !premature);
    /* The same authenticated mailbox route used by the real renderer is
     * the only point that confirms validity (first PresentCount = 1). */
    uint64_t ns=os_now_ns(); e->backend->active=true; e->backend->presented=true;
    native->completion_queued=true; native->completion_ns=ns;
    native->present_info.statistics_matched=true; native->present_info.observed=1u;
    platform_complete(e,e->backend->active_frame,ns,1u);
    bool confirmed=e->win_surface_valid;
    e->backend->active=false; e->backend->presented=false;
    T(confirmed);
    e->win_frame_owed=false; e->win_unavailable=false; e->win_retry_ns=0;
    ((d3d11_state *)e->backend->state)->occluded=true;
    T(windows_surface(e)==0);
    T(e->win_unavailable && e->win_retry_ns);
    e->win_retry_ns=os_now_ns()-1u;
    T(windows_surface(e)==0 && !e->win_unavailable && e->dirty);
    T(settle(e)==0);
    puts("round4: uncertain surface retains recovery even with no frame previously owed PASS"); return 0;
}
static int maximum_grid_test(editor *e)
{
    T(e->max_cols*e->grid.dims.cell_w>=e->cfg.max_window_width);
    T(e->max_rows*e->grid.dims.cell_h>=e->cfg.max_window_height);
    T(editor_inject(e,&(plat_event){.kind=PLAT_EV_RESIZE,.w=e->cfg.max_window_width,.h=e->cfg.max_window_height})==0);
    T(settle(e)==0);
    T(e->grid.dims.cols*e->grid.dims.cell_w+e->grid.dims.cell_w>e->cfg.max_window_width);
    printf("round4: maximum reserve cols=%u rows=%u cell=%ux%u sizeof(hl_span)=%zu hit=%zu cell=%zu instance=%zu strip=%zu\n",
        e->max_cols,e->max_rows,e->grid.dims.cell_w,e->grid.dims.cell_h,
        sizeof(hl_span),sizeof(editor_hit),sizeof(render_cell),sizeof(d3d11_instance),sizeof(d3d11_strip));
    puts("round4: startup maximum client is covered by prepaid grid PASS"); return 0;
}
static int minimize_test(editor *e)
{
    T(settle(e)==0);
    HWND window=plat_native_win32(&e->platform);
    uint32_t width=e->font.window_width,height=e->font.window_height;
    for(unsigned drawn=0;drawn<2;drawn++) {
        if(drawn) {
            d3d11_state *native=e->backend->state;
            T(editor_full_layout(e)==0); T(settle(e)==0);
            /* Existing shim: a native draw awaiting completion. */
            T(d3d11_draw(native));
            e->backend->active=true; native->drawn=true;
        }
        (void)ShowWindow(window,SW_MINIMIZE); T(IsIconic(window));
        T(editor_step(e,0)>=0);
        T(e->font.window_width==width && e->font.window_height==height);
        T(PostMessageW(window,WM_CHAR,'x',1));
        /* Drawn snapshot may remain pinned until restore; CPU edits still apply. */
        uint64_t before=e->stats.mutations,end=os_now_ns()+WATCH_NS;
        while(e->stats.mutations==before) { T(editor_step(e,0)>=0); T(os_now_ns()<end); }
        T(editor_save_request(e,os_now_ns())==0);
        while(editor_save_active(e)) { T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield(); }
        T(!editor_tab(e,tabs_active_index(&e->tabs))->modified);
        (void)ShowWindow(window,SW_SHOWNOACTIVATE); T(!IsIconic(window));
        T(settle(e)==0);
    }
    puts("round2: real HWND minimize/restore with CPU and native frames stays alive PASS");
    return 0;
}
static void completed_job(work_ctx *ctx)
{ work_msg msg={.kind=JOURNAL_RETAIN_MESSAGE}; while(!work_publish(ctx,&msg)) os_yield(); }
static void native_completed_job(work_ctx *ctx)
{ work_msg msg={.kind=NATIVE_TURN_WORK}; while(!work_publish(ctx,&msg)) os_yield(); }
typedef struct native_turn_case {
    editor *e;
    WNDPROC original;
    HANDLE entered, done;
    work_handle job;
    uint32_t frame;
    uint64_t rejected;
    UINT finish;
    unsigned phase;
    bool save, clip, draw, drawn, in_loop, survived, progressed, exited, rescued;
    ID3D11DeviceContextVtbl *draw_original;
} native_turn_case;
static native_turn_case *native_turn;
static void WINAPI native_turn_draw(ID3D11DeviceContext *ctx,UINT vertices,UINT instances,UINT first,UINT start)
{
    native_turn_case *c=native_turn;
    c->drawn=c->drawn || (c->in_loop && c->e->win_modal_turn && c->e->frame.id>c->frame);
    c->draw_original->DrawInstanced(ctx,vertices,instances,first,start);
}
static LRESULT CALLBACK native_turn_window(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    native_turn_case *c=native_turn; editor *e=c->e;
    if(msg==WM_ENTERSIZEMOVE) { c->in_loop=true; SetEvent(c->entered); }
    if(msg==WM_EXITSIZEMOVE) c->in_loop=false;
    if((msg==WM_APP+0x61 || msg==WM_APP+0x62) &&
        (win32_private(&e->platform)->callback_depth || win32_private(&e->platform)->servicing)) return 0;
    if(msg==WM_APP+0x61) {
        RECT r; GetWindowRect(hwnd,&r);
        (void)SetWindowPos(hwnd,NULL,0,0,r.right-r.left-(int)e->grid.dims.cell_w,
            r.bottom-r.top-(int)e->grid.dims.cell_h,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
        c->job=work_submit(&e->pool,(work_job){native_completed_job,NULL,0,WORK_BULK});
        return 1;
    }
    if(msg==WM_APP+0x62) {
        if(c->draw && c->phase) { c->progressed=c->in_loop && c->drawn; return c->progressed; }
        if(editor_save_leased(e)) return 0;
        journal_stats js=journal_get_stats(e->journal);
        bool delivered=c->job.epoch && native_work_received;
        c->survived=c->survived || (c->in_loop && e->pump_stopped);
        bool ready=c->in_loop && editor_length(e)==6 && !e->queue_count &&
            e->frame.id>c->frame && !e->resize_pending &&
            e->grid.dims.cols==e->platform.width/e->grid.dims.cell_w &&
            e->grid.dims.rows==e->platform.height/e->grid.dims.cell_h &&
            delivered && js.accepted_sequence==js.durable_sequence && !e->op_count &&
            /* A clipboard timeout records one rejected publication (P:
             * editor_clip_poll), independently of FIFO admission. */
            !e->journal_metadata_pending && e->stats.rejected_commands==c->rejected+(c->clip ? 1u : 0u);
        if(c->clip) ready=ready && !editor_clip_busy(e) && e->recovery_notice &&
            strstr(e->recovery_notice,"text kept");
        if(c->draw && ready) {
            /* Same offscreen/native visibility seam as undrawn_guard_test.
             * Observe actual D3D drawing by the guarded modal editor turn;
             * no displayed completion is fabricated on this station. */
            force_paint_visibility=true;
            d3d11_state *s=e->backend->state; s->latency_ready=true;
            (void)d3d11_set_suspended(e->backend,false); e->win_unavailable=false;
            (void)editor_full_layout(e); c->phase=1; return 0;
        }
        if(c->save && ready) {
            if(!c->phase) {
                plat_event save={.kind=PLAT_EV_KEY,.press=true,.keysym='s',.mods=PLAT_MOD_CTRL};
                (void)editor_inject(e,&save); c->phase=1; return 0;
            }
            if(editor_save_active(e) || e->file_command!=KEYS_ACTION_NONE ||
                e->buffer->history_cursor!=e->buffer->saved_cursor || e->buffer->saved_lost) return 0;
            if(c->phase==1) { e->publish_owed=true; c->phase=2; return 0; }
            ready=!e->publish_owed && !e->publish;
        }
        c->progressed=ready;
        return c->progressed ? 1 : 0;
    }
    if(msg==WM_APP+0x63) return c->in_loop ? 1 : 0;
    return CallWindowProcW(c->original,hwnd,msg,wp,lp);
}
static void native_turn_producer(void *arg)
{
    native_turn_case *c=arg; HWND hwnd=plat_native_win32(&c->e->platform);
    if(WaitForSingleObject(c->entered,(DWORD)(WATCH_NS/UINT64_C(1000000)))==WAIT_OBJECT_0) {
        uint64_t start_end=os_now_ns()+WATCH_NS;
        while(!SendMessageW(hwnd,WM_APP+0x61,0,0) && os_now_ns()<start_end) os_sleep_ns(UINT64_C(1000000));
        (void)SendMessageW(hwnd,WM_UNICHAR,'q',1);
        /* (G) More native input than fits in the editor FIFO during a held
         * loop. Releases do not mutate text or consume the clipboard choice. */
        for(unsigned i=0;!c->clip && i<EDITOR_INPUT_CAP+1u;i++)
            (void)SendMessageW(hwnd,WM_KEYUP,VK_F12,1);
        uint64_t end=os_now_ns()+WATCH_NS;
        do {
            DWORD_PTR ready=0;
            if(SendMessageTimeoutW(hwnd,WM_APP+0x62,0,0,SMTO_ABORTIFHUNG,
                (UINT)(WATCH_NS/UINT64_C(1000000)),&ready) && ready) break;
            /* Existing 1 ms test wait cadence lets User32 generate WM_TIMER;
             * a continuous stream of sent probes can starve low-priority timers. */
            os_sleep_ns(UINT64_C(1000000));
        } while(os_now_ns()<end);
    }
    (void)PostMessageW(hwnd,c->finish,c->finish==WM_KEYDOWN ? VK_RETURN : 0,1);
    uint64_t end=os_now_ns()+WATCH_NS;
    do {
        DWORD_PTR active=1;
        if(SendMessageTimeoutW(hwnd,WM_APP+0x63,0,0,SMTO_ABORTIFHUNG,
            (UINT)(WATCH_NS/UINT64_C(1000000)),&active) && !active) { c->exited=true; break; }
        os_sleep_ns(UINT64_C(1000000));
    } while(os_now_ns()<end);
    if(!c->exited) { c->rescued=true; (void)PostMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,1); }
    SetEvent(c->done);
}
static int native_turn_test(editor *e,UINT finish,bool save,bool clip,bool draw)
{
    T(settle(e)==0);
    if(clip) {
        InterlockedExchange(&synthetic_publication,1);
        T(publication_start(e)==0);
        e->v.state.selection=(view_selection){.anchor=4,.cursor=4};
    }
    native_turn_case c={.e=e,.entered=CreateEventW(NULL,TRUE,FALSE,NULL),
        .done=CreateEventW(NULL,TRUE,FALSE,NULL),.finish=finish,.frame=e->frame.id,
        .rejected=e->stats.rejected_commands,.save=save,.clip=clip,.draw=draw};
    T(c.entered && c.done); native_turn=&c;
    native_work_received=false;
    d3d11_state *s=e->backend->state;
    ID3D11DeviceContext *context=s->context;
    c.draw_original=context->lpVtbl;
    ID3D11DeviceContextVtbl drawing=*c.draw_original; drawing.DrawInstanced=native_turn_draw;
    if(draw) context->lpVtbl=&drawing;
    HWND hwnd=plat_native_win32(&e->platform);
    c.original=(WNDPROC)SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)native_turn_window);
    os_thread thread; T(os_thread_start(&thread,native_turn_producer,&c)==OS_OK);
    T(PostMessageW(hwnd,WM_SYSCOMMAND,SC_MOVE,0));
    int rc=0;
    do { rc=editor_step(e,0); if(rc<0) break; os_yield(); }
    while(WaitForSingleObject(c.done,0)!=WAIT_OBJECT_0);
    T(os_thread_join(&thread)==OS_OK);
    if(draw && s->context==context && context->lpVtbl==&drawing) context->lpVtbl=c.draw_original;
    force_paint_visibility=false;
    SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)c.original); native_turn=NULL;
    CloseHandle(c.entered); CloseHandle(c.done);
    if(clip) {
        SetEvent(publish_release);
        T(settle(e)==0);
        /* The clipboard worker owns these gates until it has returned. */
        win32_clip *transport=win32_private(&e->platform)->clip;
        uint64_t end=os_now_ns()+WATCH_NS;
        for(;;) {
            AcquireSRWLockShared(&transport->lock); unsigned used=transport->used; ReleaseSRWLockShared(&transport->lock);
            if(!used) break;
            T(editor_step(e,0)>=0 && os_now_ns()<end); os_yield();
        }
        CloseHandle(publish_entered); CloseHandle(publish_release);
        publish_entered=publish_release=NULL;
        InterlockedExchange(&synthetic_publication,0);
    }
    printf("native modal observations: survived=%d progress=%d exited=%d rescue=%d save=%d clip=%d draw=%d rc=%d\n",
        c.survived,c.progressed,c.exited,c.rescued,save,clip,c.drawn,rc);
    if(!c.progressed) fprintf(stderr,"native modal pending: len=%llu queue=%zu resize=%d geometry=%u/%u client=%u/%u work=%d rejects=%llu/%llu clip=%d notice=%s\n",
        (unsigned long long)editor_length(e),e->queue_count,e->resize_pending,e->grid.dims.cols,e->grid.dims.rows,
        e->platform.width/e->grid.dims.cell_w,e->platform.height/e->grid.dims.cell_h,native_work_received,
        (unsigned long long)e->stats.rejected_commands,(unsigned long long)c.rejected,editor_clip_busy(e),
        e->recovery_notice ? e->recovery_notice : "none");
    T(rc>=0 && c.survived && c.progressed && c.exited && !c.rescued);
    if(!save && !clip) T(((samples *)e->cfg.hook_ctx)->allocations==0);
    if(finish!=WM_KEYDOWN) {
        uint64_t end=os_now_ns()+WATCH_NS;
        while(rc!=EDITOR_CLOSED && os_now_ns()<end) rc=editor_step(e,0);
        T(rc==EDITOR_CLOSED && !e->queue_count);
    }
    puts("native modal: editor pump stops survive; typing/work/resize/frame/journal advance; explicit finish exits PASS");
    return 0;
}
static int native_watchdog_test(editor *e)
{
    T(settle(e)==0);
    d3d11_state *s=e->backend->state;
    HANDLE saved=s->latency, blocked=CreateEventW(NULL,FALSE,FALSE,NULL); T(blocked);
    bool deferred=s->native_deferred, suspended=s->suspended, invisible=s->invisible;
    force_paint_visibility=true;
    s->native_deferred=s->suspended=s->invisible=false; s->last_observation=os_now_ns();
    /* Native latency seam: a retained CPU frame, with no native latency
     * credit, and an elapsed inherited completion window. No fake T4/T6. */
    s->latency=blocked; s->latency_ready=false; e->backend->active=true;
    HWND hwnd=plat_native_win32(&e->platform);
    SendMessageW(hwnd,WM_ENTERSIZEMOVE,0,0);
    s->deadline=os_now_ns()-1u;
    T(!d3d11_present_ready(e->backend)); T(!d3d11_recovery_required(e->backend));
    SendMessageW(hwnd,WM_EXITSIZEMOVE,0,0);
    T(!d3d11_present_ready(e->backend));
    T(!d3d11_recovery_required(e->backend) && s->deadline>os_now_ns());
    s->deadline=os_now_ns()-1u;
    T(!d3d11_present_ready(e->backend) && d3d11_recovery_required(e->backend));
    s->recovery=false; s->latency=saved; s->deadline=0; e->backend->active=false;
    s->native_deferred=deferred; s->suspended=suspended; s->invisible=invisible;
    force_paint_visibility=false;
    CloseHandle(blocked);
    puts("native modal: held latency is not lost; release rebases watchdog; ordinary expiry still recovers PASS");
    return 0;
}
static editor *lifecycle_owner;
static _Atomic bool lifecycle_exclusive;
static void lifecycle_shutdown_probe(render_backend *backend)
{
    atomic_store(&lifecycle_exclusive,backend!=lifecycle_owner->backend &&
        !lifecycle_owner->backend->initialized);
    d3d11_shutdown(backend);
}
static int lifecycle_test(editor *e)
{
    T(settle(e)==0);
    work_handle jobs[WORK_MAX_JOBS]; size_t count=0;
    while(count<WORK_MAX_JOBS) {
        work_handle h=work_submit(&e->pool,(work_job){completed_job,NULL,0,WORK_BULK});
        if(!h.epoch) break;
        jobs[count++]=h;
    }
    T(count>0);
    for(size_t at=0;at<count;at++) while(!work_handle_finished(&e->pool,jobs[at])) os_yield();
    render_config cfg=e->backend->config; void *state=e->backend->state;
    lifecycle_owner=e; e->backend->ops.shutdown=lifecycle_shutdown_probe;
    unsigned received=shutdown_received, sent=atomic_load(&shutdown_sent);
    editor_backend_shutdown_sync(e,e->backend);
    T(shutdown_received==received+1u && atomic_load(&shutdown_sent)==sent+1u);
    T(atomic_load(&lifecycle_exclusive));
    T(editor_backend_init_sync(e,cfg,state)==0);
    for(size_t at=0;at<count;at++) T(atomic_load(&e->pool.slots[jobs[at].slot].pending)==0);
    puts("round2: saturated ordinary pool recovery/close and completion drain PASS");
    return 0;
}
static int wait_test(void)
{
    editor_wait_set set; T(editor_wait_init(&set)==OS_OK);
    HANDLE event=CreateEventW(NULL,FALSE,TRUE,NULL); T(event);
    T(editor_wait_add_handle(&set,event,EDITOR_WAIT_D3D11)==OS_OK);
    uint32_t tag=0; size_t count=0;
    T(editor_wait_ready(&set,100,&tag,1,&count)==OS_OK);
    T(count==1 && tag==EDITOR_WAIT_D3D11);
    T(WaitForSingleObject(event,0)==WAIT_TIMEOUT); CloseHandle(event);
    puts("round2: blocking auto-reset readiness credit PASS"); return 0;
}
static void park(editor *e)
{
    e->win_unavailable=true;
    (void)d3d11_set_suspended(e->backend,true);
}
static int retry_test(editor *e)
{
    T(settle(e)==0);
    /* This oracle concerns an awake window; lifecycle registration can
     * deliver the station's display-off state during the preceding settle. */
    win32_private(&e->platform)->power_off=false;
    plat_event triggers[]={
        {.kind=PLAT_EV_FOCUS,.focused=true},
        {.kind=PLAT_EV_KEY,.press=true,.keysym='x',.utf8_len=1,.utf8={'x'}},
        {.kind=PLAT_EV_EXPOSE}
    };
    for(size_t at=0;at<sizeof triggers/sizeof triggers[0];at++) {
        park(e); T(editor_inject(e,&triggers[at])==0);
        T(!e->win_unavailable && !((d3d11_state *)e->backend->state)->suspended);
        T(settle(e)==0);
    }
    HWND window=plat_native_win32(&e->platform);
    UINT messages[]={WM_ACTIVATE,WM_WINDOWPOSCHANGED,WM_POWERBROADCAST,WIN32_OCCLUSION_MESSAGE};
    for(size_t at=0;at<sizeof messages/sizeof messages[0];at++) {
        park(e); WINDOWPOS position={.hwnd=window,.flags=SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER};
        if(at==1) T(SetWindowPos(window,NULL,0,0,0,0,position.flags|SWP_FRAMECHANGED|SWP_NOACTIVATE));
        else T(PostMessageW(window,messages[at],at==2 ? PBT_APMRESUMEAUTOMATIC : WA_ACTIVE,0));
        /* Pump exactly to the first callback, before a new Present can park. */
        plat_callbacks cb={.ud=e,.on_event=platform_event}; e->platform.quit=false;
        T(plat_run_for(&e->platform,&cb,100)==PLAT_OK);
        T(!e->win_unavailable);
        T(settle(e)==0);
    }
    park(e); e->win_retry_ns=os_now_ns()-1;
    void *device=d3d11_device(e->backend);
    T(windows_surface(e)==0 && !e->win_unavailable);
    T(d3d11_device(e->backend)==device); /* Probe does not rebuild. */
    (void)ShowWindow(window,SW_MINIMIZE); e->win_retry_ns=os_now_ns()-1;
    T(windows_surface(e)==0 && e->win_unavailable && e->win_retry_ns==0);
    (void)ShowWindow(window,SW_SHOWNOACTIVATE); T(settle(e)==0);
    /* Expired latency before drawing is also an unexplained recovery. */
    d3d11_state *native=e->backend->state;
    e->backend->active=true; native->drawn=false; native->swapped=false;
    native->recovery=true;
    T(windows_surface(e)==0 && !d3d11_recovery_required(e->backend));
    T(settle(e)==0);
    puts("round2: focus/input/activation/position/display and event-free probe retry; minimized probe sleeps PASS");
    return 0;
}
static int paint_discard_test(editor *e)
{
    T(settle(e)==0);
    e->buffer->painted=false;
    T(editor_journal_paint(e)==0 && e->journal_paint==e->buffer->id);
    e->backend->active=true; ((d3d11_state *)e->backend->state)->drawn=false;
    e->win_unavailable=false;
    (void)ShowWindow(plat_native_win32(&e->platform),SW_MINIMIZE);
    refuse_clear=true;
    T(windows_surface(e)==0);
    T(e->journal_paint && e->win_paint_clear_pending && !e->buffer->painted);
    refuse_clear=false; T(editor_step(e,0)>=0);
    T(!e->journal_paint && !e->win_paint_clear_pending && !e->buffer->painted);
    puts("round2: suspension learned after CPU submit clears first-paint guard PASS");
    return 0;
}
static int fallback_test(editor *e)
{
    T(settle(e)==0);
    d3d11_state *native=e->backend->state;
    HANDLE saved=native->latency, credit=CreateEventW(NULL,FALSE,TRUE,NULL); T(credit);
    native->latency=credit; native->latency_ready=false; native->swapped=false;
    e->win_unavailable=false; e->backend->active=true; e->backend->presented=true;
    e->queue_count=EDITOR_INPUT_CAP; e->journal_metadata_pending=true;
    for(size_t at=0;at<EDITOR_INPUT_CAP;at++) e->queue[at]=(plat_event){.kind=PLAT_EV_KEY,.press=true,.keysym='x'};
    T(queue_pick(e)==SIZE_MAX);
    T(pump(e,100)==0);
    T(native->latency_ready && WaitForSingleObject(credit,0)==WAIT_TIMEOUT);
    native->latency=saved; CloseHandle(credit); e->backend->active=false; e->backend->presented=false;
    e->queue_count=0; e->journal_metadata_pending=false;
    puts("round2: full input queue plus journal fence preserves fallback latency credit PASS"); return 0;
}
static int run_child_case(const char *mode,const char *root,DWORD expected)
{
    WCHAR self[OS_PATH_MAX],command[3u*OS_PATH_MAX];
    T(GetModuleFileNameW(NULL,self,OS_PATH_MAX)>0);
    if(root) {
        WCHAR wide[OS_PATH_MAX]; T(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root,-1,wide,OS_PATH_MAX)>0);
        T(swprintf(command,sizeof command/sizeof command[0],L"\"%ls\" %hs \"%ls\"",self,mode,wide)>0);
    } else T(swprintf(command,sizeof command/sizeof command[0],L"\"%ls\" %hs",self,mode)>0);
    STARTUPINFOW startup={.cb=sizeof startup,.dwFlags=STARTF_USESTDHANDLES,
        .hStdInput=GetStdHandle(STD_INPUT_HANDLE),.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE),.hStdError=GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION process={0}; T(CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process));
    /* (E) Existing WATCH per startup, test, teardown; no tighter load bound. */
    uint64_t watches=!strcmp(mode,"--crash-guard") ? 9u : 3u;
    DWORD result=WaitForSingleObject(process.hProcess,(DWORD)(watches*WATCH_NS/UINT64_C(1000000)));
    if(result!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
    DWORD code=0; T(GetExitCodeProcess(process.hProcess,&code));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    T(result==WAIT_OBJECT_0 && code==expected); return 0;
}
static int crash_guard_test(const editor_config *config,const char *root)
{
    for(unsigned cpu=0;cpu<2;cpu++) {
        char path[OS_PATH_MAX]; snprintf(path,sizeof path,"%s\\guard.journal",root);
        (void)os_unlink(path);
        T(run_child_case(cpu ? "--cpu-death" : "--latency-death",root,cpu ? 73u : 74u)==0);
        editor_config cfg=*config; cfg.journal_path=path; cfg.start_empty=true;
        render_backend backend={0}; T(render_null_backend(&backend)==0);
        editor *restored=NULL; T(editor_open(&restored,&cfg,&backend)==0);
        T(tabs_closed_count(&restored->tabs)==cpu);
        T(tabs_count(&restored->tabs)==(cpu ? 0u : 1u));
        if(cpu) T(restored->recovery_notice && strstr(restored->recovery_notice,"first paint"));
        editor_close(restored); (void)os_unlink(path);
    }
    puts("round4: CPU crash restores closed; healthy latency-wait death restores live PASS"); return 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    (void)SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    trace_init(); T(trace_thread_register()>=0);
    T(auto_source_test()==0);
    if(argc==2 && !strcmp(argv[1],"--wait")) return wait_test();
    char temp[OS_PATH_MAX],root[OS_PATH_MAX],journal_path[OS_PATH_MAX]; unsigned random;
    T(os_temp_dir(temp,sizeof temp)==OS_OK && os_random(&random,sizeof random)==OS_OK);
    snprintf(root,sizeof root,"%s\\sublimite-test-editor-path-%x",temp,random);
    if(argc==3) snprintf(root,sizeof root,"%s",argv[2]);
    else T(os_mkdir(root)==OS_OK);
    T(os_env_set("SUBLIMITE_PROFILE",root)==OS_OK);
    T(os_env_set("SUBLIMITE_TEST_INPUT","1")==OS_OK);
    render_backend b={0}; T(render_d3d11_backend(&b)==RENDER_OK);
    snprintf(journal_path,sizeof journal_path,"%s\\typing.journal",root);
    if(argc==3) snprintf(journal_path,sizeof journal_path,"%s\\guard.journal",root);
    char named_path[OS_PATH_MAX]; snprintf(named_path,sizeof named_path,"%s\\minimized.txt",root);
    os_file named=OS_FILE_INIT; size_t wrote;
    T(os_file_open(&named,named_path,OS_OPEN_WRITE|OS_OPEN_CREATE)==OS_OK);
    T(os_file_write(&named,"base\n",5,&wrote)==OS_OK && wrote==5); os_file_close(&named);
    samples measurements={0};
    editor_config cfg={.initial=(const uint8_t *)"base\n",.initial_len=5,.cols=80,.rows=24,
        .journal_path=journal_path,.platform_backend=PLAT_BACKEND_WIN32,.font_dpi=96,.font_points=12,
        .hook_ctx=&measurements,.on_ingress=ingress,.on_submit=submitted};
    if(argc==2 && (!strcmp(argv[1],"--minimize") || !strcmp(argv[1],"--modal-save"))) cfg.path=named_path;
    uint8_t *large=NULL;
    if(argc==2 && !strcmp(argv[1],"--close-large")) {
        cfg.initial_len=2u*VIEW_MUTATION_BOUND+1u; large=malloc(cfg.initial_len); T(large);
        memset(large,'a',cfg.initial_len); large[cfg.initial_len-1u]='\n'; cfg.initial=large;
    }
    if(argc==2 && !strcmp(argv[1],"--maximum")) {
        /* (P) Tobias's display from brief; reserve versus 60% startup width. */
        cfg.max_window_width=5120; cfg.max_window_height=1440;
        cfg.window_width=5120u*60u/100u; cfg.window_height=1440u*70u/100u;
    }
    editor *e=NULL; T(editor_open(&e,&cfg,&b)==0);
    HWND window=plat_native_win32(&e->platform);
    if(argc==3) {
        T(settle(e)==0);
        win32_plat *platform=win32_private(&e->platform);
        platform->power_off=platform->session_locked=platform->session_disconnected=platform->minimized=false;
        plat_event key={.kind=PLAT_EV_KEY,.press=true,.keysym='g',.utf8_len=1,.utf8={'g'}};
        T(editor_inject(e,&key)==0 && settle(e)==0);
        /* A changed input must re-arm even though the prior CPU submission
         * survived. Snapshot death waits until submit clears the marker. */
        e->buffer->painted=false;
        crash_status=!strcmp(argv[1],"--cpu-death");
        force_paint_visibility=true;
        d3d11_state *native=e->backend->state;
        native->latency=CreateEventW(NULL,FALSE,FALSE,NULL); T(native->latency);
        native->latency_ready=false;
        e->win_unavailable=false; T(d3d11_set_suspended(e->backend,false)==0);
        T(editor_full_layout(e)==0);
        uint64_t end=os_now_ns()+WATCH_NS;
        while(e->dirty) {
            while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
            if(e->full_pending) T(editor_full_layout(e)==0);
            T(editor_paint_prepare(e)==0 && submit(e)==0);
            if(e->dirty) T(editor_flush(e)==0);
            T(os_now_ns()<end);
        }
        T(!crash_status); /* CPU mode must have died after composition. */
        T(e->backend->active && !native->drawn && !e->journal_paint);
        T(present(e)==0 && e->backend->active && !native->drawn && !e->journal_paint);
        ExitProcess(74); /* (P) abrupt death inside the undrawn latency wait. */
    }
    if(argc==2) {
        /* On a station display-power notifications are real but unrelated to
         * these renderer seams. --deadline explicitly tests both states. */
        if(strcmp(argv[1],"--deadline")) {
            win32_plat *native=win32_private(&e->platform);
            native->power_off=native->session_locked=native->session_disconnected=native->minimized=false;
        }
        int rc=!strcmp(argv[1],"--crash-guard") ? crash_guard_test(&cfg,root) :
            !strcmp(argv[1],"--modal-turn") ? native_turn_test(e,WM_KEYDOWN,false,false,false) :
            !strcmp(argv[1],"--modal-close") ? native_turn_test(e,WM_CLOSE,false,false,false) :
            !strcmp(argv[1],"--modal-session") ? native_turn_test(e,WM_QUERYENDSESSION,false,false,false) :
            !strcmp(argv[1],"--modal-save") ? native_turn_test(e,WM_KEYDOWN,true,false,false) :
            !strcmp(argv[1],"--modal-clip") ? native_turn_test(e,WM_KEYDOWN,false,true,false) :
            !strcmp(argv[1],"--modal-draw") ? native_turn_test(e,WM_KEYDOWN,false,false,true) :
            !strcmp(argv[1],"--modal-clock") ? native_watchdog_test(e) :
            !strcmp(argv[1],"--close-queue") ? close_queue_test(e,false,false) :
            !strcmp(argv[1],"--close-fence") ? close_queue_test(e,false,1u) :
            !strcmp(argv[1],"--close-save") ? close_queue_test(e,false,2u) :
            !strcmp(argv[1],"--close-prompt") ? close_queue_test(e,true,false) :
            !strcmp(argv[1],"--stuck") ? stuck_publication_test(e) :
            !strcmp(argv[1],"--cloak") ? cloak_test(e) :
            !strcmp(argv[1],"--uncertain") ? uncertain_surface_test(e) :
            !strcmp(argv[1],"--maximum") ? maximum_grid_test(e) :
            !strcmp(argv[1],"--resizefail") ? resize_failure_test(e) :
            !strcmp(argv[1],"--allocfail") ? allocation_failure_test(e) :
            !strcmp(argv[1],"--queue") ? queue_cut_test(e) :
            !strcmp(argv[1],"--close-large") ? close_large_test(e) :
            !strcmp(argv[1],"--guard") ? undrawn_guard_test(e) :
            !strcmp(argv[1],"--deadline") ? deadline_test(e) :
            !strcmp(argv[1],"--idle") ? idle_probe_test(e) :
            !strcmp(argv[1],"--larger") ? larger_surface_test(e) :
            !strcmp(argv[1],"--cut") ? cut_test(e) :
            !strcmp(argv[1],"--minimize") ? minimize_test(e) :
            !strcmp(argv[1],"--retry") ? retry_test(e) :
            !strcmp(argv[1],"--paint") ? paint_discard_test(e) :
            !strcmp(argv[1],"--fallback") ? fallback_test(e) : lifecycle_test(e);
        editor_close(e);
        if(!rc && (!strcmp(argv[1],"--close-queue") || !strcmp(argv[1],"--close-fence") || !strcmp(argv[1],"--close-save"))) {
            bool fence=strcmp(argv[1],"--close-queue")!=0;
            b=(render_backend){0}; T(render_d3d11_backend(&b)==0);
            T(editor_open(&e,&cfg,&b)==0); T(settle(e)==0);
            uint8_t recovered[6]; size_t count=fence ? 6u : 2u;
            T(editor_length(e)==count && editor_read(e,0,recovered,count)==0);
            T(!memcmp(recovered,fence ? "qbase\n" : "q\n",count));
            puts("round4: close-boundary recovery bytes exact PASS"); editor_close(e);
        }
        if(!rc && !strcmp(argv[1],"--close-large")) {
            b=(render_backend){0}; T(render_d3d11_backend(&b)==0);
            T(editor_open(&e,&cfg,&b)==0); T(settle(e)==0);
            uint8_t last=0; T(editor_length(e)==1 && editor_read(e,0,&last,1)==0 && last=='\n');
            puts("round3: large cut recovery restores exactly the cut result PASS"); editor_close(e);
        }
        free(large); cleanup(root); return rc;
    }
    const char *round4[]={"--modal-turn","--modal-close","--modal-session","--modal-save","--modal-clip","--modal-draw","--modal-clock","--close-queue","--close-fence","--close-save","--close-prompt","--guard","--cloak","--uncertain","--stuck","--maximum","--crash-guard"};
    for(size_t at=0;at<sizeof round4/sizeof round4[0];at++) T(run_child_case(round4[at],NULL,0)==0);
    T(queue_cut_test(e)==0); T(deadline_test(e)==0); T(idle_probe_test(e)==0); T(undrawn_guard_test(e)==0);
    T(larger_surface_test(e)==0); T(allocation_failure_test(e)==0); T(resize_failure_test(e)==0);
    T(cut_test(e)==0); /* Restore the campaign's initial text afterward. */
    editor_close(e); (void)os_unlink(journal_path); b=(render_backend){0}; T(render_d3d11_backend(&b)==0);
    cfg.path=named_path;
    T(editor_open(&e,&cfg,&b)==0); window=plat_native_win32(&e->platform);
    T(minimize_test(e)==0); T(lifecycle_test(e)==0); T(retry_test(e)==0); T(fallback_test(e)==0); T(paint_discard_test(e)==0);
    editor_close(e); (void)os_unlink(journal_path); b=(render_backend){0}; T(render_d3d11_backend(&b)==0);
    cfg.path=NULL;
    T(editor_open(&e,&cfg,&b)==0); window=plat_native_win32(&e->platform);
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
    editor_close(e); (void)os_unlink(journal_path);
    cfg.initial_len=2u*VIEW_MUTATION_BOUND+1u; large=malloc(cfg.initial_len); T(large);
    memset(large,'a',cfg.initial_len); large[cfg.initial_len-1u]='\n'; cfg.initial=large;
    b=(render_backend){0}; T(render_d3d11_backend(&b)==0); T(editor_open(&e,&cfg,&b)==0);
    T(close_large_test(e)==0); editor_close(e);
    b=(render_backend){0}; T(render_d3d11_backend(&b)==0); T(editor_open(&e,&cfg,&b)==0); T(settle(e)==0);
    uint8_t last=0; T(editor_length(e)==1 && editor_read(e,0,&last,1)==0 && last=='\n');
    puts("round3: large cut recovery restores exactly the cut result PASS");
    editor_close(e); free(large); cleanup(root);
    return 0;
}
