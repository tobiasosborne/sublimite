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
static volatile LONG fail_clip;
static BOOL cut_open(HWND window)
{ return InterlockedCompareExchange(&fail_clip,0,0) ? FALSE : OpenClipboard(window); }
#define OpenClipboard cut_open
#include "../../src/win32/win32_clip_win.c"
#undef OpenClipboard
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
static bool force_paint_visibility;
static BOOL paint_visible(HWND window)
{ return force_paint_visibility ? TRUE : IsWindowVisible(window); }
static HRESULT WINAPI paint_attribute(HWND window,DWORD attribute,PVOID value,DWORD bytes)
{
    if(force_paint_visibility && attribute==DWMWA_CLOAKED && bytes==sizeof(DWORD)) {
        *(DWORD *)value=0; return S_OK;
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
static bool refuse_clear;
static void clear_guard(editor *e) { if(!refuse_clear) editor_journal_paint_clear(e); }
#define editor_journal_paint_clear clear_guard
#include "../../src/editor/editor.c"
#undef editor_journal_paint_clear
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
    T(!e->win_unavailable && !e->win_retry_ns);
    T(settle(e)==0);
    e->win_retry_ns=os_now_ns()-1u; e->win_unavailable=false;
    T(wait_timeout(e,100,os_now_ns())>0); e->win_retry_ns=0;
    puts("round3: expose behind journal fence clears expired deadline PASS"); return 0;
}
static int idle_probe_test(editor *e)
{
    T(settle(e)==0);
    /* A previously presented image remains valid when no new frame is owed. */
    e->win_unavailable=true; (void)d3d11_set_suspended(e->backend,true);
    e->win_frame_owed=false; e->win_retry_ns=0;
    T(windows_surface(e)==0); T(e->win_retry_ns==0);
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) T(windows_surface(e)==0 && !e->win_retry_ns);
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
    e->win_frame_owed=false; T(windows_surface(e)==0 && !e->win_retry_ns);
    puts("round3: valid covered idle frame has zero probe deadlines; owed frame retries without events PASS"); return 0;
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
    while(e->dirty) {
        while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
        if(e->full_pending) T(editor_full_layout(e)==0);
        T(editor_paint_prepare(e)==0);
        T(submit(e)==0);
        if(e->dirty) T(editor_flush(e)==0);
        if(os_now_ns()>=end) fprintf(stderr,"submit blocked paint=%d full=%d layout=%d clip=%d active=%d ops=%zu action=%d extra=%d\n",e->paint_ready,e->full_pending,layout_busy(&e->lay),editor_clip_busy(e),e->backend->active,e->op_count,e->action,e->extra_rows);
        T(os_now_ns()<end);
    }
    T(e->backend->active && !native->drawn && !e->journal_paint);
    uint64_t records=journal_get_stats(e->journal).accepted_sequence;
    for(unsigned i=0;i<WORK_MAX_JOBS;i++) {
        /* This oracle is guard growth, independently of the missing-latency
         * watchdog; slow native RPCs on an inactive station may outlast it. */
        native->deadline=os_now_ns()+WATCH_NS;
        T(present(e)==0); T(!e->journal_paint && e->backend->active);
    }
    T(journal_get_stats(e->journal).accepted_sequence==records);
    native->latency=saved; CloseHandle(blocked);
    drawing_editor=e; drawing_guard_seen=false; e->buffer->win_draw_safe=false;
    drawing_original=native->context->lpVtbl;
    ID3D11DeviceContextVtbl guarded=*drawing_original; guarded.DrawInstanced=guarded_instances;
    native->context->lpVtbl=&guarded; native->latency_ready=true;
    native->deadline=os_now_ns()+WATCH_NS;
    T(present(e)==0);
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
    while(e->quit) { T(editor_step(e,0)>=0); T(os_now_ns()<end); os_yield(); }
    T(editor_length(e)==1 && e->recovery_notice && !editor_clip_pending(e));
    InterlockedExchange(&fail_clip,0);
    puts("round2: close waits for queued publication; failure cancels close with text intact PASS");
    puts("round2: identified clipboard failure preserves text/selection, success cuts PASS");
    return 0;
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
    T(os_mkdir(root)==OS_OK && os_env_set("SUBLIMITE_PROFILE",root)==OS_OK);
    T(os_env_set("SUBLIMITE_TEST_INPUT","1")==OS_OK);
    render_backend b={0}; T(render_d3d11_backend(&b)==RENDER_OK);
    snprintf(journal_path,sizeof journal_path,"%s\\typing.journal",root);
    char named_path[OS_PATH_MAX]; snprintf(named_path,sizeof named_path,"%s\\minimized.txt",root);
    os_file named=OS_FILE_INIT; size_t wrote;
    T(os_file_open(&named,named_path,OS_OPEN_WRITE|OS_OPEN_CREATE)==OS_OK);
    T(os_file_write(&named,"base\n",5,&wrote)==OS_OK && wrote==5); os_file_close(&named);
    samples measurements={0};
    editor_config cfg={.initial=(const uint8_t *)"base\n",.initial_len=5,.cols=80,.rows=24,
        .journal_path=journal_path,.platform_backend=PLAT_BACKEND_WIN32,.font_dpi=96,.font_points=12,
        .hook_ctx=&measurements,.on_ingress=ingress,.on_submit=submitted};
    if(argc==2 && !strcmp(argv[1],"--minimize")) cfg.path=named_path;
    uint8_t *large=NULL;
    if(argc==2 && !strcmp(argv[1],"--close-large")) {
        cfg.initial_len=2u*VIEW_MUTATION_BOUND+1u; large=malloc(cfg.initial_len); T(large);
        memset(large,'a',cfg.initial_len); large[cfg.initial_len-1u]='\n'; cfg.initial=large;
    }
    editor *e=NULL; T(editor_open(&e,&cfg,&b)==0);
    HWND window=plat_native_win32(&e->platform);
    if(argc==2) {
        int rc=!strcmp(argv[1],"--resizefail") ? resize_failure_test(e) :
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
        if(!rc && !strcmp(argv[1],"--close-large")) {
            b=(render_backend){0}; T(render_d3d11_backend(&b)==0);
            T(editor_open(&e,&cfg,&b)==0); T(settle(e)==0);
            uint8_t last=0; T(editor_length(e)==1 && editor_read(e,0,&last,1)==0 && last=='\n');
            puts("round3: large cut recovery restores exactly the cut result PASS"); editor_close(e);
        }
        free(large); cleanup(root); return rc;
    }
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
