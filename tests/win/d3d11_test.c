/* Real-window GPU conformance. Geometry and blend cases follow raster_test's
 * blend_exact_test; every R8 coverage, wide halves and Q8 overscan are added.
 * Fixture sizes describe test data, not performance budgets. */
#include "d3d11/d3d11.h"
#include "raster/raster.h"
#include "base/base.h"
#include "os/os.h"
#include "trace/trace.h"
#include "work/work.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#define T(c) do { if (!(c)) { fprintf(stderr,"d3d11_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
/* (G) Liveness watchdog only, same missing-completion window as GL; no
 * assertion on render speed or the panel's refresh period. */
#define WATCHDOG_NS UINT64_C(3000000000)
/* Frozen render_config requires a borrowed non-NULL platform identity. This
 * test-only opaque implementation has no W1.1 layout dependency. */
struct plat { HWND window; };
typedef struct stamps {
    uint32_t id, t5_count, t6_count, stats_frames;
    uint64_t t5, t6, ui;
    bool bad;
    HWND minimize;
} stamps;
static void done(void *user, uint32_t id, uint64_t ns)
{
    stamps *p = user;
    if (id != p->id || ns < p->t5 || os_thread_self() != p->ui) p->bad = true;
    p->t5 = ns; p->t5_count++;
}
static void complete(void *user, uint32_t id, uint64_t ns)
{
    stamps *p = user;
    if (id != p->id || ns < p->t6 || os_thread_self() != p->ui) p->bad = true;
    p->t6 = ns; p->t6_count++;
}
static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m,NULL,0,0,PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}
static int client_size(HWND window, uint32_t w, uint32_t h)
{
    RECT rect = {0,0,(LONG)w,(LONG)h};
    T(AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE));
    T(SetWindowPos(window,NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,
                   SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    pump();
    RECT client; T(GetClientRect(window,&client));
    T(client.right == (LONG)w && client.bottom == (LONG)h);
    return 0;
}
typedef struct init_args { render_backend *b; render_config config; void *state; int rc; bool ready; } init_args;
static void init_worker(work_ctx *ctx)
{
    init_args *a = ctx->arg;
    int rc = render_backend_init(a->b,&a->config,a->state,a->b->info.state_size);
    if (rc == RENDER_OK && a->b->finish_init) rc = a->b->finish_init(a->b);
    work_msg result = {.kind = 1,.generation = ctx->generation};
    memcpy(result.data,&rc,sizeof rc);
    while (!work_should_stop(ctx) && !work_publish(ctx,&result)) os_yield();
}
static void init_receive(const work_msg *msg, void *arg)
{
    init_args *a = arg;
    memcpy(&a->rc,msg->data,sizeof a->rc); a->ready = true;
}
static int initialize(init_args *a)
{
    work_pool *pool = a->config.workers;
    a->ready = false; a->rc = RENDER_ERR_INIT;
    work_handle job = work_submit(pool,(work_job){init_worker,a,1,WORK_BULK}); T(job.epoch);
    while (!a->ready || !work_handle_finished(pool,job)) {
        pump(); (void)work_mailbox_drain(pool,init_receive,a); Sleep(1);
    }
    return 0;
}
static int debug_messages(ID3D11InfoQueue *queue)
{
    if (!queue) return 0;
    UINT64 count = queue->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(queue);
    for (UINT64 i = 0; i < count; i++) {
        SIZE_T size = 0;
        T(SUCCEEDED(queue->lpVtbl->GetMessage(queue,i,NULL,&size)));
        D3D11_MESSAGE *m = malloc(size); T(m);
        T(SUCCEEDED(queue->lpVtbl->GetMessage(queue,i,m,&size)));
        bool bad = m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING;
        if (bad) fprintf(stderr,"D3D11 debug: %s\n",m->pDescription);
        free(m); T(!bad);
    }
    queue->lpVtbl->ClearStoredMessages(queue); return 0;
}
static int debug_live_children(ID3D11Debug *debugger, ID3D11InfoQueue *queue)
{
    if (!debugger) return 0;
    queue->lpVtbl->ClearStoredMessages(queue);
    T(SUCCEEDED(debugger->lpVtbl->ReportLiveDeviceObjects(debugger,
        D3D11_RLDO_DETAIL)));
    UINT64 count = queue->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(queue);
    for (UINT64 i = 0; i < count; i++) {
        SIZE_T size = 0;
        T(SUCCEEDED(queue->lpVtbl->GetMessage(queue,i,NULL,&size)));
        D3D11_MESSAGE *m = malloc(size); T(m);
        T(SUCCEEDED(queue->lpVtbl->GetMessage(queue,i,m,&size)));
        /* Queue/debugger deliberately keep the device alive for this census.
         * Ignore that one device; every external device-child ref must be gone. */
        bool child = strstr(m->pDescription,"Live ID3D11") &&
                     !strstr(m->pDescription,"Live ID3D11Device at") &&
                     !strstr(m->pDescription,"Refcount: 0");
        if (child) fprintf(stderr,"D3D11 live child: %s\n",m->pDescription);
        free(m); T(!child);
    }
    queue->lpVtbl->ClearStoredMessages(queue); return 0;
}
static int frame(render_backend *b, render_grid *grid, stamps *p,
                 const uint32_t *want, uint32_t w, uint32_t h, bool mutate)
{
    render_strip strips[8]; size_t count = 0;
    T(render_dirty_strips(grid,strips,8,&count) == RENDER_OK);
    p->id = grid->frame_id;
    uint32_t n5 = p->t5_count, n6 = p->t6_count;
    /* Alternate event-loop ownership of the auto-reset readiness signal. */
    if (p->id%2u && WaitForSingleObject(d3d11_latency_handle(b),0) == WAIT_OBJECT_0)
        T(d3d11_notify_latency_ready(b) == RENDER_OK);
    edit_malloc_guard_begin();
    int rc = render_backend_submit(b,grid,strips,count);
    T(rc == RENDER_OK);
    T(p->t5_count == n5 && p->t6_count == n6);
    T(render_backend_submit(b,grid,strips,count) == RENDER_ERR_BUSY);
    /* All caller metadata and RGB pixels may be reused immediately. Atlas
     * pixels are intentionally kept immutable until T5, as render.h requires. */
    if (mutate) {
        memset(grid->cells,0,grid->cell_capacity*sizeof *grid->cells);
        memset((void *)grid->glyphs,0,grid->glyph_count*sizeof *grid->glyphs);
        memset((void *)grid->pages,0,grid->page_count*sizeof *grid->pages);
        if (grid->region.pixels) memset((void *)grid->region.pixels,0,grid->region.capacity*sizeof(uint32_t));
    }
    if (p->minimize) {
        HWND window = p->minimize;
        (void)ShowWindow(window,SW_MINIMIZE); pump(); T(IsIconic(window));
        T(render_backend_present(b,p->id) == RENDER_ERR_BUSY);
        T(d3d11_poll_completions(b) == RENDER_OK && !d3d11_recovery_required(b));
        T(p->t5_count == n5 && p->t6_count == n6);
        (void)ShowWindow(window,SW_SHOWNOACTIVATE); pump(); T(!IsIconic(window));
        p->minimize = NULL;
    }
    uint64_t deadline = os_now_ns() + WATCHDOG_NS;
    do {
        rc = render_backend_present(b,p->id);
        if (rc == RENDER_ERR_BUSY) { pump(); Sleep(1); }
        if (os_now_ns() >= deadline) {
            d3d11_present_info info; T(d3d11_present_stats(b,&info) == RENDER_OK);
            fprintf(stderr,"present timeout frame=%u rc=%d status=%08x visible=%d iconic=%d\n",
                p->id,rc,(unsigned)info.status,IsWindowVisible(((struct plat *)b->config.platform)->window),
                IsIconic(((struct plat *)b->config.platform)->window));
            ID3D11Device *native = d3d11_device(b);
            fprintf(stderr,"device removal=%08x\n",(unsigned)native->lpVtbl->GetDeviceRemovedReason(native));
            ID3D11InfoQueue *messages = NULL;
            if (SUCCEEDED(native->lpVtbl->QueryInterface(native,&IID_ID3D11InfoQueue,(void **)&messages))) {
                (void)debug_messages(messages); messages->lpVtbl->Release(messages);
            }
            return 1;
        }
    } while (rc == RENDER_ERR_BUSY);
    T(rc == RENDER_OK);
    T(render_backend_present(b,p->id) == RENDER_ERR_STATE);
    uint32_t got[136*80];
    T(d3d11_read_pixels(b,got,136*80,w) == RENDER_OK);
    /* (P) Integer shader blend has the same rounded /255 as scalar: exact
     * RGB equality, stricter than the +/-1 LSB allowed for float blending. */
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        size_t i = (size_t)y*w+x;
        if (got[i] != want[i]) {
            fprintf(stderr,"frame=%u pixel=%u,%u got=%08x want=%08x\n",p->id,x,y,got[i],want[i]);
            return 1;
        }
    }
    while (b->active) {
        rc = d3d11_poll_completions(b);
        if (rc != RENDER_OK || os_now_ns() >= deadline) {
            d3d11_present_info info;
            T(d3d11_present_stats(b,&info) == RENDER_OK);
            fprintf(stderr,"stats expected=%u observed=%u hr=%08x qpc=%llu\n",info.expected,info.observed,(unsigned)info.status,(unsigned long long)info.sync_qpc);
            HDESK desktop = OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
            char name[256] = {0}; DWORD needed = 0;
            if (desktop) {
                (void)GetUserObjectInformationA(desktop,UOI_NAME,name,sizeof name,&needed);
                CloseDesktop(desktop);
            }
            fprintf(stderr,"input desktop=%s err=%lu\n",name,GetLastError());
            return 1;
        }
        if (b->active) { pump(); Sleep(1); }
    }
    size_t allocations = edit_malloc_guard_end();
    T(!edit_malloc_guard_active() || allocations == 0);
    T(p->t5_count == n5+1 && p->t6_count == n6+1 && !p->bad);
    T(p->t5 >= b->submitted_ns && p->t6 >= p->t5);
    d3d11_present_info info; T(d3d11_present_stats(b,&info) == RENDER_OK);
    T(b->info.capabilities & RENDER_CAP_PRESENT_TIMING);
    T(info.expected && info.observed == info.expected && info.sync_qpc && info.statistics_matched);
    p->stats_frames++;
    T(d3d11_poll_completions(b) == RENDER_OK);
    T(p->t5_count == n5+1 && p->t6_count == n6+1);
    render_event stale = {RENDER_EVENT_DEVICE_DONE,p->id,os_now_ns(),NULL};
    T(render_backend_event(b,&stale) == RENDER_ERR_FRAME);
    return 0;
}
static void oracle(render_grid *g, uint32_t *out, uint32_t w, uint32_t h)
{
    for (size_t i = 0; i < (size_t)w*h; i++) out[i] = g->surface_background | UINT32_C(0xff000000);
    raster_scene scene = {g->dims,g->cells,g->glyphs,g->glyph_count,g->pages,g->page_count,UINT32_C(0xff000000)};
    raster_palette palette = {0};
    for (uint32_t row = 0; row < g->dims.rows; row++)
        (void)raster_row_scrolled(&scene,&g->scroll,out+(size_t)row*g->dims.cell_h*w,w,row,&palette,NULL,NULL);
    raster_blit_pixels(&g->region,out,w,0,g->region.height,UINT32_C(0xff000000));
}
static void fixtures(render_cell *cells, render_glyph *glyphs, render_atlas_page *pages, uint8_t *a, uint8_t *z)
{
    /* Two padded pages and different page shapes; all byte coverage values. */
    for (uint32_t y = 0; y < 16; y++) for (uint32_t x = 0; x < 16; x++) a[y*19+x] = (uint8_t)(y*16+x);
    for (size_t i = 0; i < 32*16; i++) z[i] = (uint8_t)((i*37u)&255u);
    pages[0] = (render_atlas_page){a,19*16,19,16,16};
    pages[1] = (render_atlas_page){z,32*16,32,32,16};
    glyphs[0] = (render_glyph){1,0,0,0,8,16};
    glyphs[1] = (render_glyph){2,0,8,0,8,16};
    glyphs[2] = (render_glyph){3,1,3,2,16,12};
    glyphs[3] = (render_glyph){4,1,23,4,3,5};
    for (size_t i = 0; i < 16*5; i++) cells[i] = (render_cell){(uint32_t)(i%2+1),(uint32_t)(i%2),
        i%3 ? 0xfedcba : 0xffffff,i%3 ? 0x213547 : 0,(uint16_t)(i%4 == 0 ? RENDER_ATTR_INVERSE : i%4 == 1 ? RENDER_ATTR_UNDERLINE : 0),0};
    cells[2] = (render_cell){3,2,0x123456,0xfedcba,RENDER_ATTR_WIDE_LEFT|RENDER_ATTR_UNDERLINE,0};
    cells[3] = (render_cell){0,RENDER_NO_SLOT,0x123456,0xfedcba,RENDER_ATTR_WIDE_RIGHT|RENDER_ATTR_UNDERLINE,0};
    cells[4] = (render_cell){4,3,0xffffff,0x776655,RENDER_ATTR_BOLD|RENDER_ATTR_ITALIC|RENDER_ATTR_CURSOR|RENDER_ATTR_SELECTION,0};
    cells[5] = (render_cell){0,RENDER_NO_SLOT,0,0x998877,0,0};
}
typedef struct atlas_append {
    uint8_t *page;
    _Atomic bool ready, stop;
} atlas_append;
static void append_pixels(void *arg)
{
    atlas_append *a = arg;
    do {
        for (size_t row = 0; row < 16; row++) a->page[row*32+31]++;
        atomic_store_explicit(&a->ready,true,memory_order_release);
    } while (!atomic_load_explicit(&a->stop,memory_order_acquire));
}
static int run_device(HWND window, bool warp, bool background)
{
    T(client_size(window,130,67) == 0);
    render_backend b = {0}; T(render_d3d11_backend(&b) == RENDER_OK);
    render_backend_info info; T(render_backend_query(&b,&info) == RENDER_OK);
    void *state = calloc(1,info.state_size); T(state); b.state = state;
    T(render_d3d11_backend_for_window(&b,window) == RENDER_OK);
    T(d3d11_diagnostics(&b,warp,true) == RENDER_OK);
    stamps p = {.ui = os_thread_self()};
    render_config config = {.dims = {16,4,8,16},.max_width = 136,.max_height = 80,
        .max_cells = 16*6,.max_glyphs = 4,.max_pages = 2,.max_atlas_bytes = 19*16+32*16,
        .max_region_width = 4,.max_region_height = 3,
        .hooks = {done,complete,&p},.background_init = background};
    edit_arena pool_arena;
    T(edit_arena_init(&pool_arena,sizeof(work_pool)+_Alignof(work_pool)) == 0);
    work_pool *pool = edit_arena_alloc(&pool_arena,sizeof *pool,_Alignof(work_pool)); T(pool);
    T(work_pool_init(pool,1,0) == 0); config.workers = pool;
    init_args init = {.b = &b,.config = config,.state = state,.rc = RENDER_ERR_INIT};
    T(initialize(&init) == 0);
    T(init.rc == RENDER_ERR_UNSUPPORTED && !b.initialized && b.state == NULL);
    b.state = state;
    T(render_d3d11_backend_for_window(&b,window) == RENDER_OK);
    T(d3d11_diagnostics(&b,warp,true) == RENDER_OK);
    struct plat platform = {window}; init.config.platform = &platform;
    /* Ordinary init must roll back native acquisitions when no diagnostic
     * presentation can be displayed; retry on this same HWND must succeed. */
    bool init_background = init.config.background_init;
    init.config.background_init = false;
    (void)ShowWindow(window,SW_HIDE); pump();
    T(initialize(&init) == 0);
    T(init.rc == RENDER_ERR_UNSUPPORTED && !b.initialized && b.state == NULL);
    (void)ShowWindow(window,SW_SHOWNOACTIVATE); pump();
    b.state = state;
    T(render_d3d11_backend_for_window(&b,window) == RENDER_OK);
    T(d3d11_diagnostics(&b,warp,true) == RENDER_OK);
    init.config.background_init = init_background;
    /* UI dispatches while the init worker owns the descriptor; publication
     * and adoption both cross the work mailbox, including the failure above. */
    T(initialize(&init) == 0);
    if (init.rc != RENDER_OK) fprintf(stderr,"D3D11 init rc=%d native=%lu\n",init.rc,GetLastError());
    T(init.rc == RENDER_OK);
    bool actual_warp = d3d11_is_warp(&b), debug = d3d11_debug_enabled(&b);
    T(!warp || actual_warp);
    T(d3d11_latency_handle(&b));
    ID3D11Device *device = d3d11_device(&b); ID3D11InfoQueue *queue = NULL; ID3D11Debug *debugger = NULL;
    if (debug) T(SUCCEEDED(device->lpVtbl->QueryInterface(device,&IID_ID3D11InfoQueue,(void **)&queue)));
    if (debug) T(SUCCEEDED(device->lpVtbl->QueryInterface(device,&IID_ID3D11Debug,(void **)&debugger)));
    T(p.t5_count == 0 && p.t6_count == 0);
    T(((b.info.capabilities & RENDER_CAP_PRESENT_TIMING) != 0) == !background);
    T(d3d11_diagnostics(&b,true,true) == RENDER_ERR_STATE);
    render_cell cells[16*5]; render_glyph glyphs[4]; render_atlas_page pages[2];
    uint8_t a[19*16], z[32*16]; uint64_t dirty = 0;
    fixtures(cells,glyphs,pages,a,z);
    render_grid g; T(render_grid_init(&g,config.dims,cells,16*5,&dirty,1) == RENDER_OK);
    g.pages = pages; g.page_count = 2; g.glyphs = glyphs; g.glyph_count = 4;
    g.surface_background = 0x192837;
    uint32_t want[136*80], ink[15];
    for (size_t i = 0; i < 15; i++) ink[i] = (uint32_t)i*0x10203u;
    g.region = (render_pixels){ink,15,5,3,17,4,3,true};
    T(render_frame_begin(&g,1) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    oracle(&g,want,130,67);
    atlas_append append = {.page = z}; os_thread writer;
    T(os_thread_start(&writer,append_pixels,&append) == OS_OK);
    while (!atomic_load_explicit(&append.ready,memory_order_acquire)) os_yield();
    int first_frame = frame(&b,&g,&p,want,130,67,true);
    atomic_store_explicit(&append.stop,true,memory_order_release);
    T(os_thread_join(&writer) == OS_OK); T(first_frame == 0);
    fixtures(cells,glyphs,pages,a,z);
    for (size_t i = 0; i < 15; i++) ink[i] = (uint32_t)i*0x10203u;
    /* A changed row, atlas and retained untouched rows through flip rotation. */
    T(render_frame_begin(&g,2) == RENDER_OK);
    cells[16].bg = 0xaabbcc; cells[48].bg = 0x665544;
    T(render_mark_rows(&g,1,1) == RENDER_OK && render_mark_rows(&g,3,1) == RENDER_OK);
    oracle(&g,want,130,67); T(frame(&b,&g,&p,want,130,67,false) == 0);
    T(render_frame_begin(&g,3) == RENDER_OK); /* zero damage still T4/T5/T6 */
    T(frame(&b,&g,&p,want,130,67,false) == 0);
    T(render_frame_begin(&g,4) == RENDER_OK);
    ink[0] = 0xabcdef; g.region.dirty = true; g.surface_background = 0x654321;
    oracle(&g,want,130,67); T(frame(&b,&g,&p,want,130,67,false) == 0);
    /* Fractional text moves, fixed chrome/sidebar/region does not. */
    T(render_frame_begin(&g,5) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    g.scroll = (render_scroll){-128,1,3,12};
    oracle(&g,want,130,67); T(frame(&b,&g,&p,want,130,67,false) == 0);
    T(render_frame_begin(&g,6) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    g.scroll.origin_y_q8 = -4095; /* cell_h*256-1 exercises final overscan pixel */
    oracle(&g,want,130,67); T(frame(&b,&g,&p,want,130,67,false) == 0);
    T(client_size(window,122,51) == 0);
    render_dims small = {8,3,8,16};
    T(render_backend_resize(&b,small) == RENDER_OK);
    T(render_grid_init(&g,small,cells,16*5,&dirty,1) == RENDER_OK);
    g.pages = pages; g.page_count = 2; g.glyphs = glyphs; g.glyph_count = 4; g.surface_background = 0x574839;
    /* Rebuild pairs for the new row width. */
    for (size_t i = 0; i < 8*3; i++) cells[i] = (render_cell){1,0,0xfefefe,0x010203,0,0};
    T(render_frame_begin(&g,7) == RENDER_OK);
    T(render_backend_submit(&b,&g,NULL,0) == RENDER_ERR_STRIPS);
    T(render_mark_full(&g) == RENDER_OK);
    oracle(&g,want,122,51); T(frame(&b,&g,&p,want,122,51,false) == 0);
    /* Independent region changes must restore the entire old and new bounds. */
    uint32_t next = 8;
    for (unsigned step = 0; step < 6; step++) {
        T(render_frame_begin(&g,next++) == RENDER_OK);
        if (step == 0) g.region = (render_pixels){ink,15,5,5,7,4,3,false};
        if (step == 1) { g.region.x = 20; g.region.y = 17; g.region.dirty = true; }
        if (step == 2) { g.region.width = 2; g.region.height = 1; g.region.dirty = false; }
        if (step == 3) g.region.pixels = NULL;
        if (step == 4) { g.region.pixels = ink; g.region.x = 3; g.region.y = 3; }
        if (step == 5) {
            cells[0].bg = 0x897654; T(render_mark_rows(&g,0,1) == RENDER_OK);
        }
        oracle(&g,want,122,51); T(frame(&b,&g,&p,want,122,51,false) == 0);
    }
    puts("d3d11_test: region enable/move/shrink/disable/re-enable/clean-over-damage exact PASS");
    T(render_frame_begin(&g,next++) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    a[0] ^= 255u; /* Previous T5 retired; referenced coverage may now change. */
    p.minimize = window;
    oracle(&g,want,122,51); T(frame(&b,&g,&p,want,122,51,false) == 0);
    puts("d3d11_test: minimize/restore before Present, post-T5 atlas change exact PASS");
    T(debug_messages(queue) == 0);
    render_stats stats; T(render_backend_stats(&b,&stats) == RENDER_OK);
    T(stats.submitted_frames == p.stats_frames && stats.presented_frames == p.stats_frames);
    /* Quiescent shutdown discards pending T5/T6 and releases native refs. */
    T(render_frame_begin(&g,next) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    render_strip full = {0,small.rows};
    T(render_backend_submit(&b,&g,&full,1) == RENDER_OK);
    int present_rc;
    uint64_t deadline = os_now_ns()+WATCHDOG_NS;
    do {
        present_rc = render_backend_present(&b,next);
        if (os_now_ns() >= deadline) {
            d3d11_present_info pending; T(d3d11_present_stats(&b,&pending) == RENDER_OK);
            fprintf(stderr,"shutdown present rc=%d status=%08x\n",present_rc,(unsigned)pending.status);
            return 1;
        }
        if (present_rc == RENDER_ERR_BUSY) { pump(); Sleep(1); }
    } while (present_rc == RENDER_ERR_BUSY);
    T(present_rc == RENDER_OK && b.active);
    render_backend_shutdown(&b); render_backend_shutdown(&b);
    T(p.t5_count == p.stats_frames && p.t6_count == p.stats_frames);
    T(debug_messages(queue) == 0);
    T(debug_live_children(debugger,queue) == 0);
    /* Retained diagnostics keep the OLD device alive across replacement. */
    memset(state,0,info.state_size); b.state = state;
    T(render_d3d11_backend_for_window(&b,window) == RENDER_OK);
    T(d3d11_diagnostics(&b,warp,true) == RENDER_OK);
    init.config.dims = small;
    T(initialize(&init) == 0 && init.rc == RENDER_OK);
    T(render_frame_begin(&g,next+1u) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    oracle(&g,want,122,51); T(frame(&b,&g,&p,want,122,51,false) == 0);
    render_backend_shutdown(&b);
    T(debug_messages(queue) == 0 && debug_live_children(debugger,queue) == 0);
    puts("d3d11_test: same-HWND shutdown/re-init with old diagnostic device retained PASS");
    if (debugger) debugger->lpVtbl->Release(debugger);
    if (queue) queue->lpVtbl->Release(queue);
    free(state); work_pool_shutdown(pool); edit_arena_free(&pool_arena);
    printf("d3d11_test: %s%s pixels=exact frames=%u T5/T6=%u/%u monotone UI resize shutdown-pending PASS T6=DXGI-statistics:%u debug=%s guard=%s init=%s present-timing=validated\n",
        warp ? "WARP" : "hardware", !warp && actual_warp ? " unavailable (WARP fallback)" : "",
        p.stats_frames,p.t5_count,p.t6_count,p.stats_frames,debug ? "enabled, no live children" : "SDK layers unavailable",edit_malloc_guard_active() ? "zero allocations" : "sanitizer owns malloc", background ? "background" : "ordinary");
    return 0;
}
int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    /* Keep the authorised display test observable while it runs; restore
     * this thread's execution request on exit. No persistent power-policy change. */
    EXECUTION_STATE execution = SetThreadExecutionState(ES_CONTINUOUS|ES_DISPLAY_REQUIRED); T(execution);
    /* The binding worker brief authorises this small nonactivating window. */
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {.lpfnWndProc = DefWindowProcW,.hInstance = instance,.lpszClassName = L"sublimite-d3d11-test"};
    T(RegisterClassW(&cls));
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"sublimite D3D11 conformance",
        WS_OVERLAPPEDWINDOW,50,50,160,120,NULL,NULL,instance,NULL);
    T(window); ShowWindow(window,SW_SHOWNOACTIVATE);
    T(SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)); pump();
    bool ordinary = argc > 1 && strcmp(argv[1],"--ordinary") == 0;
    int rc = run_device(window,argc > 1 && strcmp(argv[1],"--warp") == 0,!ordinary);
    if (!rc && argc == 1) rc = run_device(window,true,true);
    if (!rc && argc == 1) rc = run_device(window,false,false);
    if (!rc && argc == 1) rc = run_device(window,true,false);
    DestroyWindow(window); UnregisterClassW(cls.lpszClassName,instance);
    (void)SetThreadExecutionState(execution);
    return rc;
}
