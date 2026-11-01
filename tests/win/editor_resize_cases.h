/* Included by editor_win_test: private WARP readback and QPC stage timings.
 * Fixture dimensions come from Tobias's display and a half-size client. */
static WNDPROC resize_window_original;
static editor *resize_editor;
static HANDLE resize_loop_entered,resize_loop_done;
static int resize_inline_frame(editor *e,uint32_t width,uint32_t height);
static bool resize_loop_ok;
static uint64_t resize_setwindow_ns;
static LRESULT CALLBACK resize_test_window(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_APP+0x65) {
        if(win32_private(&resize_editor->platform)->servicing ||
            win32_private(&resize_editor->platform)->callback_depth) return 0;
        resize_loop_ok=win32_private(&resize_editor->platform)->modal_depth &&
            resize_inline_frame(resize_editor,3003,903)==0;
        return 1;
    }
    LRESULT result=CallWindowProcW(resize_window_original,hwnd,msg,wp,lp);
    if(msg==WM_ENTERSIZEMOVE && resize_loop_entered) SetEvent(resize_loop_entered);
    if(msg==WM_GETMINMAXINFO) {
        /* User32's default maximum tracking size excludes caption/borders.
         * Admit the requested full-display CLIENT in this offscreen fixture. */
        RECT outer,client; GetWindowRect(hwnd,&outer); GetClientRect(hwnd,&client);
        MINMAXINFO *limits=(void *)lp;
        limits->ptMaxTrackSize.x=(LONG)resize_editor->font.max_width+outer.right-outer.left-client.right;
        limits->ptMaxTrackSize.y=(LONG)resize_editor->font.max_height+outer.bottom-outer.top-client.bottom;
    }
    return result;
}
static int resize_client(editor *e,uint32_t width,uint32_t height)
{
    HWND hwnd=plat_native_win32(&e->platform);
    RECT outer,client; T(GetWindowRect(hwnd,&outer) && GetClientRect(hwnd,&client));
    uint64_t start=os_now_ns();
    BOOL changed=SetWindowPos(hwnd,NULL,0,0,(int)width+outer.right-outer.left-client.right,
        (int)height+outer.bottom-outer.top-client.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    resize_setwindow_ns=os_now_ns()-start; T(changed);
    T(GetClientRect(hwnd,&client));
    if(client.right!=(LONG)width || client.bottom!=(LONG)height)
        fprintf(stderr,"resize client requested=%ux%u got=%ldx%ld\n",width,height,client.right,client.bottom);
    T(client.right==(LONG)width && client.bottom==(LONG)height);
    return 0;
}
static int resize_erase(editor *e)
{
    HDC dc=CreateCompatibleDC(NULL); T(dc);
    BITMAPINFO info={.bmiHeader={.biSize=sizeof(BITMAPINFOHEADER),.biWidth=1,.biHeight=-1,
        .biPlanes=1,.biBitCount=32,.biCompression=BI_RGB}};
    void *pixels=NULL; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0); T(bitmap && pixels);
    HGDIOBJ old=SelectObject(dc,bitmap); *(uint32_t *)pixels=UINT32_C(0xdeadbeef);
    T(SendMessageW(plat_native_win32(&e->platform),WM_ERASEBKGND,(WPARAM)dc,0)==1);
    GdiFlush(); uint32_t actual=*(uint32_t *)pixels&UINT32_C(0xffffff);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    T(actual==e->layout_cfg.bg); return 0;
}
static int resize_staging(d3d11_state *s)
{
    if(s->staging) return 0;
    D3D11_TEXTURE2D_DESC desc={.Width=s->max_width,.Height=s->max_height,
        .MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},
        .Usage=D3D11_USAGE_STAGING,.CPUAccessFlags=D3D11_CPU_ACCESS_READ};
    T(SUCCEEDED(s->device->lpVtbl->CreateTexture2D(s->device,&desc,NULL,&s->staging)));
    return 0;
}
static int resize_readback(editor *e,bool map_check)
{
    d3d11_state *s=e->backend->state;
    D3D11_MAPPED_SUBRESOURCE read;
    T(SUCCEEDED(s->context->lpVtbl->Map(s->context,(ID3D11Resource *)s->staging,0,D3D11_MAP_READ,0,&read)));
    const render_pixels *r=&e->grid.region;
    size_t mismatches=0;
    uint32_t *linux_pixels=NULL;
    if(map_check) {
        linux_pixels=malloc((size_t)r->width*r->height*sizeof *linux_pixels); T(linux_pixels);
        render_pixels portable=*r; portable.x=portable.y=0;
        raster_blit_pixels(&portable,linux_pixels,r->width,0,r->height,UINT32_C(0xff000000));
    }
    if(map_check) for(uint32_t y=0;y<r->height;y++) {
        const uint32_t *row=(const void *)((const uint8_t *)read.pData+(size_t)(r->y+y)*read.RowPitch);
        for(uint32_t x=0;x<r->width;x++)
            mismatches+=row[r->x+x]!=linux_pixels[(size_t)y*r->width+x];
    }
    /* Newly exposed partial cells must be the editor's theme base. */
    const uint32_t *last=(const void *)((const uint8_t *)read.pData+(size_t)(s->height-1u)*read.RowPitch);
    uint32_t corner=last[s->width-1u];
    s->context->lpVtbl->Unmap(s->context,(ID3D11Resource *)s->staging,0);
    free(linux_pixels);
    if(mismatches) fprintf(stderr,"minimap mismatched pixels=%zu region=%ux%u stride=%zu reserve=%ux%u\n",
        mismatches,r->width,r->height,r->stride,e->backend->config.max_region_width,e->backend->config.max_region_height);
    T(!mismatches);
    if(s->height%e->grid.dims.cell_h || s->width%e->grid.dims.cell_w)
        T(corner==(e->layout_cfg.bg|UINT32_C(0xff000000)));
    return 0;
}
static IDXGISwapChain2Vtbl *resize_chain_original;
static bool resize_bad_interval;
static HRESULT WINAPI resize_occluded_present(IDXGISwapChain2 *chain,UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS *params)
{
    /* Real hidden Present cannot supply T4. Model a pending visible Present
     * after the real draw/readback, without fabricating successful completion. */
    HRESULT hr=resize_chain_original->Present1(chain,interval,flags,params);
    if(resize_editor->win_resize_turn && interval) resize_bad_interval=true;
    return hr==DXGI_STATUS_OCCLUDED ? DXGI_ERROR_WAS_STILL_DRAWING : hr;
}
static uint64_t resize_buffers_ns,resize_buffer_view_ns;
static ID3D11DeviceVtbl *resize_device_original;
static uint64_t resize_views_ns;
static unsigned resize_views;
static HRESULT WINAPI resize_timed_view(ID3D11Device *device,ID3D11Resource *resource,
    const D3D11_RENDER_TARGET_VIEW_DESC *desc,ID3D11RenderTargetView **view)
{
    uint64_t start=os_now_ns(); resize_views++;
    HRESULT hr=resize_device_original->CreateRenderTargetView(device,resource,desc,view);
    resize_views_ns+=os_now_ns()-start; return hr;
}
static HRESULT WINAPI resize_timed_buffers(IDXGISwapChain2 *chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags)
{
    uint64_t start=os_now_ns();
    HRESULT hr=resize_chain_original->ResizeBuffers(chain,count,width,height,format,flags);
    resize_buffers_ns+=os_now_ns()-start; return hr;
}
static HRESULT WINAPI resize_timed_buffer(IDXGISwapChain2 *chain,UINT index,REFIID iid,void **buffer)
{
    uint64_t start=os_now_ns();
    HRESULT hr=resize_chain_original->GetBuffer(chain,index,iid,buffer);
    resize_buffer_view_ns+=os_now_ns()-start; return hr;
}
static int resize_manual_frame(editor *e,uint32_t width,uint32_t height,bool profile)
{
    d3d11_state *s=e->backend->state;
    T(settle(e)==0);
    T(resize_client(e,width,height)==0);
    resize_chain_original=s->swapchain->lpVtbl;
    IDXGISwapChain2Vtbl timed=*resize_chain_original;
    timed.ResizeBuffers=resize_timed_buffers; timed.GetBuffer=resize_timed_buffer;
    s->swapchain->lpVtbl=&timed; resize_buffers_ns=resize_buffer_view_ns=0;
    resize_device_original=s->device->lpVtbl;
    ID3D11DeviceVtbl device_timed=*resize_device_original; device_timed.CreateRenderTargetView=resize_timed_view;
    s->device->lpVtbl=&device_timed; resize_views_ns=0; resize_views=0;
    uint64_t start=os_now_ns();
    T(editor_resize_now(e,width,height)==0);
    uint64_t geometry=os_now_ns()-start-resize_buffers_ns-resize_buffer_view_ns-resize_views_ns;
    s->device->lpVtbl=resize_device_original;
    s->swapchain->lpVtbl=resize_chain_original; start=os_now_ns();
    while(layout_busy(&e->lay)) T(layout_slice(e)>=0);
    T(editor_paint_prepare(e)==0);
    uint64_t rows=os_now_ns()-start; start=os_now_ns();
    T(editor_compose(e)==0);
    uint64_t compose=os_now_ns()-start; start=os_now_ns();
    size_t count=0; T(render_dirty_strips(&e->grid,e->strips,e->strip_cap,&count)==0);
    T(render_backend_submit(e->backend,&e->grid,e->strips,count)==0);
    uint64_t snapshot=os_now_ns()-start; start=os_now_ns();
    T(d3d11_draw(s));
    uint64_t draw=os_now_ns()-start; start=os_now_ns();
    DXGI_PRESENT_PARAMETERS params={0};
    HRESULT hr=s->swapchain->lpVtbl->Present1(s->swapchain,s->resize_frame ? 0u : 1u,DXGI_PRESENT_DO_NOT_WAIT,&params);
    uint64_t present_ns=os_now_ns()-start;
    if(profile) printf("resize-profile: (M)[AC, desktop Spiral] client=%ux%u ResizeBuffers_ns=%llu GetBuffer_ns=%llu target_views=%u target_views_ns=%llu geometry_ns=%llu rows_atlas_ns=%llu compose_ns=%llu snapshot_ns=%llu draw_ns=%llu present_ns=%llu hr=%08lx derivation=QPC stage end-start; geometry excludes native calls; hidden WARP; box in use\n",
        width,height,(unsigned long long)resize_buffers_ns,(unsigned long long)resize_buffer_view_ns,
        resize_views,(unsigned long long)resize_views_ns,
        (unsigned long long)geometry,(unsigned long long)rows,(unsigned long long)compose,
        (unsigned long long)snapshot,(unsigned long long)draw,(unsigned long long)present_ns,(unsigned long)hr);
    T(resize_readback(e,true)==0);
    if(profile) {
        printf("| %ux%u | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu | %llu |\n",
            width,height,(unsigned long long)resize_buffers_ns,(unsigned long long)resize_buffer_view_ns,
            (unsigned long long)resize_views_ns,(unsigned long long)geometry,(unsigned long long)rows,
            (unsigned long long)compose,(unsigned long long)snapshot,(unsigned long long)draw,(unsigned long long)present_ns);
        /* Back-to-back draw control: reinsert the removed clears using the
         * same snapshot, dimensions and device after the readback fence. */
        start=os_now_ns(); T(d3d11_draw(s)); uint64_t current_cpu=os_now_ns()-start;
        start=os_now_ns(); T(resize_readback(e,false)==0); uint64_t current_wait=os_now_ns()-start;
        float background[4]; d3d11_background(s->surface_background,background);
        start=os_now_ns();
        s->context->lpVtbl->ClearRenderTargetView(s->context,s->scene_rtv,background);
        s->context->lpVtbl->ClearRenderTargetView(s->context,s->target_rtv,background);
        T(d3d11_draw(s)); uint64_t legacy_cpu=os_now_ns()-start;
        start=os_now_ns(); T(resize_readback(e,false)==0); uint64_t legacy_wait=os_now_ns()-start;
        printf("resize-draw-pair: (M)[AC, desktop Spiral] client=%ux%u current_cpu_ns=%llu current_readback_wait_ns=%llu reserved_clears_cpu_ns=%llu reserved_clears_readback_wait_ns=%llu derivation=QPC end-start; same snapshot/device/dimensions; readback fence between variants; box in use\n",
            width,height,(unsigned long long)current_cpu,(unsigned long long)current_wait,
            (unsigned long long)legacy_cpu,(unsigned long long)legacy_wait);
    }
    /* Direct diagnostic draw never claimed T4/T5/T6; restore CPU parked mode. */
    e->backend->active=false; e->backend->full_required=true; e->dirty=false;
    return 0;
}
static int resize_band(editor *e)
{
    minimap_input input=editor_map_input(e->buffer); minimap_window w;
    T(input.index_ready && editor_map_window(e,&input,&w));
    uint64_t visible=e->v.config.rows;
    if(visible>input.lines) visible=input.lines;
    uint64_t expected=visible*MINIMAP_LINE_PX;
    if(expected>e->grid.region.height) expected=e->grid.region.height;
    T(w.box_height==expected && w.height==e->grid.region.height);
    T(e->grid.region.width==e->map_cols*e->grid.dims.cell_w);
    printf("minimap-size: client=%ux%u current=%ux%u band=%u visible=%llu total=%llu scale=%u PASS\n",
        e->platform.width,e->platform.height,e->grid.region.width,e->grid.region.height,w.box_height,
        (unsigned long long)visible,(unsigned long long)input.lines,w.scale);
    return 0;
}
static int resize_inline_frame(editor *e,uint32_t width,uint32_t height)
{
    d3d11_state *s=e->backend->state;
    uint64_t started=os_now_ns();
    T(resize_client(e,width,height)==0);
    T(s->width==width && s->height==height);
    /* Each continuation has bounded work. For this resident ASCII fixture,
     * one progress unit per cell/row plus prepare/submit/present bounds turns;
     * this is a liveness oracle, never a claim about refresh latency. */
    size_t turns=0,limit=(size_t)e->grid.dims.cols*(e->grid.dims.rows+1u)+3u;
    MSG message; HWND hwnd=plat_native_win32(&e->platform);
    while(!s->drawn && turns<limit && PeekMessageW(&message,hwnd,WIN32_RESIZE_MESSAGE,WIN32_RESIZE_MESSAGE,PM_REMOVE)) {
        DispatchMessageW(&message); turns++;
    }
    uint64_t ready_ns=os_now_ns()-started;
    if(s->width!=width || s->height!=height || !s->drawn || e->resize_pending)
        fprintf(stderr,"inline surface=%ux%u drawn=%d dirty=%d full=%d paint=%d layout=%d view=%d active=%d pending=%d unavailable=%d suspended=%d error=%d queue=%zu\n",
            s->width,s->height,s->drawn,e->dirty,e->full_pending,e->paint_ready,layout_busy(&e->lay),view_busy(&e->v),
            e->backend->active,e->resize_pending,e->win_unavailable,s->suspended,e->error,e->queue_count);
    T(s->width==width && s->height==height && s->drawn);
    T(e->grid.dims.cols==width/e->grid.dims.cell_w && e->grid.dims.rows==height/e->grid.dims.cell_h);
    T(s->dims.cols==e->grid.dims.cols && s->dims.rows==e->grid.dims.rows);
    T(!e->resize_pending && e->active_frame.geometry_generation==e->geometry_generation);
    T(resize_readback(e,true)==0 && resize_band(e)==0 && resize_erase(e)==0);
    DXGI_RGBA color; T(SUCCEEDED(s->swapchain->lpVtbl->GetBackgroundColor(s->swapchain,&color)));
    T((uint32_t)(color.r*255.0f+0.5f)==((e->layout_cfg.bg>>16)&255u));
    T((uint32_t)(color.g*255.0f+0.5f)==((e->layout_cfg.bg>>8)&255u));
    T((uint32_t)(color.b*255.0f+0.5f)==(e->layout_cfg.bg&255u));
    if(test_isolation_query()!=TEST_ISOLATION_LIVE)
        T(!e->backend->presented && !e->backend->device_seen && !e->backend->complete_seen);
    printf("resize-sync: client=%ux%u surface matches at SetWindowPos return; frame/map/margins/theme match after %zu queued continuations (bound=%zu) PASS\n",width,height,turns,limit);
    printf("resize-sync: (M)[AC, desktop Spiral] client=%ux%u SetWindowPos_ns=%llu drawn_ready_ns=%llu derivation=QPC end-start, before blocking readback; hidden WARP is not visible RTX 3090; box in use\n",
        width,height,(unsigned long long)resize_setwindow_ns,(unsigned long long)ready_ns);
    return 0;
}
static void resize_loop_producer(void *arg)
{
    HWND hwnd=arg;
    if(WaitForSingleObject(resize_loop_entered,(DWORD)(WATCH_NS/UINT64_C(1000000)))==WAIT_OBJECT_0) {
        DWORD_PTR result=0; uint64_t end=os_now_ns()+WATCH_NS;
        while(!result && os_now_ns()<end) {
            SendMessageTimeoutW(hwnd,WM_APP+0x65,0,0,SMTO_ABORTIFHUNG,(UINT)(WATCH_NS/UINT64_C(1000000)),&result);
            os_sleep_ns(UINT64_C(1000000));
        }
    }
    PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,1); SetEvent(resize_loop_done);
}
static int resize_live_loop(editor *e)
{
    LIVE_ONLY_NATIVE_LOOP("editor_win: real resize loop",0);
    HWND hwnd=plat_native_win32(&e->platform);
    resize_loop_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    resize_loop_done=CreateEventW(NULL,TRUE,FALSE,NULL); T(resize_loop_entered && resize_loop_done);
    os_thread producer; T(os_thread_start(&producer,resize_loop_producer,hwnd)==OS_OK);
    T(PostMessageW(hwnd,WM_SYSCOMMAND,SC_SIZE|WMSZ_RIGHT,0));
    uint64_t end=os_now_ns()+WATCH_NS;
    do { T(editor_step(e,0)>=0); T(os_now_ns()<end); }
    while(WaitForSingleObject(resize_loop_done,0)!=WAIT_OBJECT_0 || win32_private(&e->platform)->modal_depth);
    T(os_thread_join(&producer)==OS_OK && resize_loop_ok);
    CloseHandle(resize_loop_entered); CloseHandle(resize_loop_done); resize_loop_entered=resize_loop_done=NULL;
    return 0;
}
static int resize_notification_loop(editor *e)
{
    win32_plat *p=win32_private(&e->platform);
    plat_callbacks callbacks={.ud=e,.on_event=platform_event,.on_work=platform_work,
        .on_present_complete=platform_complete,.on_modal_turn=platform_modal_turn};
    notification_editor_send(e,&callbacks,WM_ENTERSIZEMOVE,0,0);
    T(p->modal_depth==1 && !p->callbacks);
    p->callbacks=&callbacks;
    bool running=e->win_step_running; e->win_step_running=true;
    int rc=resize_inline_frame(e,3003,903);
    e->win_step_running=running; p->callbacks=NULL;
    notification_editor_send(e,&callbacks,WM_EXITSIZEMOVE,0,0);
    T(!rc && !p->modal_depth);
    puts("resize-sync: programmatic resize between direct modal notifications PASS");
    return 0;
}
static int resize_cases(editor *e,const char *mode)
{
    bool live=!strcmp(mode,"--resize-live");
    if(live && test_isolation_query()!=TEST_ISOLATION_LIVE) return resize_live_loop(e);
    T(live || test_isolation_query()!=TEST_ISOLATION_LIVE);
    T(settle(e)==0);
    d3d11_state *s=e->backend->state;
    T(s->device && resize_staging(s)==0);
    HWND hwnd=plat_native_win32(&e->platform); resize_editor=e;
    resize_window_original=(WNDPROC)SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)resize_test_window);
    if(live || !strcmp(mode,"--resize-sync") || !strcmp(mode,"--resize-native-fail")) {
        force_paint_visibility=true; e->win_unavailable=false;
        T(d3d11_set_suspended(e->backend,false)==0);
        HANDLE latency=s->latency; s->latency=CreateEventW(NULL,FALSE,FALSE,NULL); T(s->latency);
        s->latency_ready=false;
        resize_chain_original=s->swapchain->lpVtbl;
        static IDXGISwapChain2Vtbl pending; pending=*resize_chain_original; pending.Present1=resize_occluded_present;
        s->swapchain->lpVtbl=&pending;
        T(resize_inline_frame(e,2561,721)==0);
        T(resize_inline_frame(e,2562,722)==0); /* sub-cell pixel resize */
        T(resize_inline_frame(e,5120,1440)==0);
        e->layout_cfg.bg=config_theme_color(CONFIG_LIGHT,"editor.background"); e->lay.cfg.bg=e->layout_cfg.bg;
        T(resize_inline_frame(e,5119,1439)==0);
        if(!live) T(resize_notification_loop(e)==0);
        T(resize_live_loop(e)==0); /* Hidden suites print the mandatory SKIP. */
        if(!strcmp(mode,"--resize-native-fail")) {
            resize_original=s->swapchain->lpVtbl; pending.ResizeBuffers=no_resize_memory;
            T(resize_client(e,2999,899)==0);
        }
        s->swapchain->lpVtbl=resize_chain_original;
        CloseHandle(s->latency); s->latency=latency;
        force_paint_visibility=false;
        SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)resize_window_original);
        T(!e->error && !resize_bad_interval);
        if(!strcmp(mode,"--resize-native-fail")) {
            T(e->recovery_notice && settle(e)==0);
            puts("resize-sync: native inline ResizeBuffers OOM defers rebuild, keeps editing alive PASS");
        }
        puts("resize-sync: programmatic size, unsignalled latency, old drawn frame, dark/light, no invented completions PASS");
        return 0;
    }
    bool profile=!strcmp(mode,"--resize-profile");
    if(profile) {
        puts("resize table (M)[AC, desktop Spiral], box in use; ns=QPC end-start; hidden WARP, not RTX 3090; Present status/timing on resize-profile lines");
        puts("| client | ResizeBuffers ns | GetBuffer ns | RTV recreation ns | geometry ns | rows/atlas ns | compose ns | CPU snapshot ns | draw enqueue ns | Present ns |");
        puts("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|");
    }
    /* Isolate stage costs from the native inline callback; --resize-sync
     * independently checks that callback, with no manual editor servicing. */
    win32_plat_on_resize(&e->platform,NULL,NULL);
    T(resize_manual_frame(e,3072,1008,profile)==0 && resize_band(e)==0);
    T(resize_manual_frame(e,5120,1440,profile)==0 && resize_band(e)==0);
    T(resize_manual_frame(e,2561,721,profile)==0 && resize_band(e)==0);
    T(resize_manual_frame(e,5119,1439,profile)==0 && resize_band(e)==0);
    if(!profile) {
        ShowWindow(hwnd,SW_MAXIMIZE); T(IsZoomed(hwnd)); T(settle(e)==0);
        RECT maximized; T(GetClientRect(hwnd,&maximized));
        T(resize_manual_frame(e,(uint32_t)maximized.right,(uint32_t)maximized.bottom,false)==0 && resize_band(e)==0);
        puts("minimap-size: maximized client PASS");
        ShowWindow(hwnd,SW_RESTORE); T(settle(e)==0);
        T(SendMessageW(hwnd,WM_DPICHANGED,MAKEWPARAM(144,144),0)==0); T(settle(e)==0);
        T(GetClientRect(hwnd,&maximized));
        T(resize_manual_frame(e,(uint32_t)maximized.right,(uint32_t)maximized.bottom,false)==0 && resize_band(e)==0);
        puts("minimap-size: 144 DPI metrics/atlas/region/band PASS");
        uint64_t id;
        T(editor_add_buffer(e,NULL,(const uint8_t *)"short\ntext\n",11,&id)==0 && settle(e)==0);
        T(resize_manual_frame(e,(uint32_t)maximized.right,(uint32_t)maximized.bottom,false)==0 && resize_band(e)==0);
        T(editor_add_buffer(e,NULL,NULL,0,&id)==0 && settle(e)==0);
        T(resize_manual_frame(e,(uint32_t)maximized.right,(uint32_t)maximized.bottom,false)==0 && resize_band(e)==0);
        puts("minimap-size: short and empty documents PASS");
    }
    puts("minimap-size: D3D readback equals portable Linux minimap pixels, startup/large/shrink/grow PASS");
    SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)resize_window_original);
    return 0;
}
