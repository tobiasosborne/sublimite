/* White-box regressions: compile the C backend here; production has no hooks. */
#include <windows.h>
#include <dwmapi.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
static _Atomic uint64_t flush_thread;
static bool empty_statistics;
static HRESULT WINAPI test_dwm_flush(void);
#define DwmFlush test_dwm_flush
#include "../../src/d3d11/d3d11_win.c"
#undef DwmFlush
static HRESULT WINAPI test_dwm_flush(void)
{
    atomic_store_explicit(&flush_thread,os_thread_self(),memory_order_release);
    return S_OK;
}
#include <stdio.h>
#include <stdlib.h>
#define T(c) do { if (!(c)) { fprintf(stderr,"d3d11_win_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
static int disabled_scroll(void)
{
    render_config cfg = {.dims = {2,2,1,1},.max_cells = 4,.max_pages = 1,
        .max_glyphs = 1,.max_region_width = 1,.max_region_height = 1};
    d3d11_state s = {.atlas_width = 1,.atlas_height = 1};
    T(d3d11_reserve(&s,&cfg));
    render_cell *cells = calloc(cfg.max_cells,sizeof *cells); T(cells);
    for (size_t i = 0; i < cfg.max_cells; i++) cells[i].atlas_slot = RENDER_NO_SLOT;
    uint64_t dirty = 0;
    render_grid g; T(render_grid_init(&g,cfg.dims,cells,cfg.max_cells,&dirty,1) == RENDER_OK);
    render_backend b = {.state = &s,.config = cfg};
    g.scroll = (render_scroll){INT64_MIN,UINT32_MAX,UINT32_MAX,0};
    T(render_frame_begin(&g,1) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    render_strip full = {0,2}, partial = {1,1};
    T(d3d11_submit(&b,&g,&full,1) == RENDER_OK);
    T(s.instance_count == cfg.max_cells && s.scroll.origin_y_q8 == 0);
    T(render_frame_begin(&g,2) == RENDER_OK);
    T(d3d11_submit(&b,&g,&partial,1) == RENDER_OK && s.instance_count == 2);
    T(d3d11_submit(&b,&g,NULL,0) == RENDER_OK && s.instance_count == 0);
    free(cells); d3d11_release(&s);
    puts("d3d11_win_test: disabled-scroll exact-capacity full/partial/zero INT64_MIN PASS");
    return 0;
}
typedef struct append_writer {
    uint8_t *pixels;
    _Atomic bool ready, stop;
} append_writer;
static void append_page(void *arg)
{
    append_writer *w = arg;
    do {
        for (size_t i = 4; i < 8; i++) w->pixels[i]++;
        atomic_store_explicit(&w->ready,true,memory_order_release);
    } while (!atomic_load_explicit(&w->stop,memory_order_acquire));
}
static int atlas_rectangles(void)
{
    render_config cfg = {.dims = {1,1,2,2},.max_cells = 1,.max_pages = 1,
        .max_glyphs = 1,.max_region_width = 1,.max_region_height = 1};
    d3d11_state s = {.atlas_width = 8,.atlas_height = 1};
    T(d3d11_reserve(&s,&cfg));
    uint8_t pixels[8] = {255,128,77,77,77,77,77,77};
    render_atlas_page page = {pixels,sizeof pixels,8,8,1};
    render_glyph glyph = {1,0,0,0,2,1};
    render_cell cell = {1,0,0xffffff,0,0,0};
    uint64_t dirty = 0; render_grid g;
    T(render_grid_init(&g,cfg.dims,&cell,1,&dirty,1) == RENDER_OK);
    g.pages = &page; g.page_count = 1; g.glyphs = &glyph; g.glyph_count = 1;
    T(render_frame_begin(&g,1) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    append_writer writer = {.pixels = pixels}; os_thread thread;
    T(os_thread_start(&thread,append_page,&writer) == OS_OK);
    while (!atomic_load_explicit(&writer.ready,memory_order_acquire)) os_yield();
    render_backend b = {.state = &s,.config = cfg}; render_strip full = {0,1};
    int rc = d3d11_submit(&b,&g,&full,1);
    atomic_store_explicit(&writer.stop,true,memory_order_release);
    T(os_thread_join(&thread) == OS_OK); T(rc == RENDER_OK);
    T(s.atlas_cache[0] == 255 && s.atlas_cache[1] == 128);
    /* Untouched cache canaries prove no disjoint writer bytes were copied. */
    T(s.atlas_cache[2] == 0 && s.atlas_cache[3] == 0);
    for (size_t i = 4; i < 8; i++) T(s.atlas_cache[i] == 0);
    /* Metadata is reusable on return; only value snapshots remain. */
    glyph = (render_glyph){0}; page = (render_atlas_page){0};
    T(s.instances[0].glyph[2] == 2 && s.atlas_cache[1] == 128);
    d3d11_release(&s);
    puts("d3d11_win_test: concurrent disjoint atlas append/cache canaries PASS");
    return 0;
}
static int timing_capability(void)
{
    render_backend b = {0}; T(render_d3d11_backend(&b) == RENDER_OK);
    T(!(b.info.capabilities & RENDER_CAP_PRESENT_TIMING));
    puts("d3d11_win_test: present timing withheld until validated PASS");
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_stats(IDXGISwapChain2 *chain, DXGI_FRAME_STATISTICS *out)
{
    (void)chain; *out = (DXGI_FRAME_STATISTICS){0};
    if (!empty_statistics && atomic_load_explicit(&flush_thread,memory_order_acquire)) {
        out->PresentCount = 7; out->SyncQPCTime.QuadPart = 123;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fake_removed(ID3D11Device *device)
{ (void)device; return S_OK; }
static void hook_count(void *arg, uint32_t id, uint64_t ns)
{ if (id == 41 && ns) (*(unsigned *)arg)++; }
static int statistics_progress(void)
{
    render_config cfg = {.max_cells = 1,.max_pages = 1,.max_glyphs = 1,
        .max_region_width = 1,.max_region_height = 1};
    d3d11_state s = {.atlas_width = 1,.atlas_height = 1};
    T(d3d11_reserve(&s,&cfg));
    IDXGISwapChain2Vtbl cv = {.GetFrameStatistics = fake_stats}; IDXGISwapChain2 chain = {&cv};
    ID3D11DeviceVtbl dv = {.GetDeviceRemovedReason = fake_removed}; ID3D11Device device = {&dv};
    s.swapchain = &chain; s.device = &device; s.swapped = true; s.latency_ready = true;
    s.query_ready = true; s.pending_count = 7; s.deadline = os_now_ns()+D3D11_COMPLETION_NS;
    unsigned hooks = 0;
    render_backend b = {.state = &s,.initialized = true,.active = true,.presented = true,.active_frame = 41,
        .config = {.hooks = {hook_count,hook_count,&hooks}}};
    work_msg msg = {.kind = D3D11_POLL_MESSAGE}; render_event event = {RENDER_EVENT_WORK,41,0,&msg};
    atomic_store(&flush_thread,0);
    while (b.active && os_now_ns() < s.deadline) {
        T(d3d11_event(&b,&event) == RENDER_OK); Sleep(1);
    }
    T(!b.active && hooks == 2);
    T(atomic_load(&flush_thread) != os_thread_self());
    T(s.present_info.statistics_matched && s.present_info.expected == 7);
    s.swapchain = NULL; s.device = NULL; d3d11_release(&s);
    puts("d3d11_win_test: stale statistics require worker DwmFlush, original ID hooks once PASS");
    return 0;
}
static int delayed_retry(void)
{
    HANDLE latency = CreateEventW(NULL,FALSE,FALSE,NULL); T(latency);
    d3d11_state s = {.latency = latency,.deadline = 1};
    render_backend b = {.state = &s};
    T(d3d11_present(&b,1) == RENDER_ERR_BUSY);
    T(!s.failed);
    work_msg msg = {.kind = D3D11_POLL_MESSAGE}; render_event event = {RENDER_EVENT_WORK,1,0,&msg};
    T(d3d11_event(&b,&event) == RENDER_OK && !s.failed);
    CloseHandle(latency);
    puts("d3d11_win_test: delayed BUSY retry and pre-Present poll preserve healthy device PASS");
    return 0;
}
static bool texture_released, flush_after_release, chain_ordered;
static void STDMETHODCALLTYPE fake_clear(ID3D11DeviceContext *context) { (void)context; }
static void STDMETHODCALLTYPE fake_flush(ID3D11DeviceContext *context)
{ (void)context; if (texture_released) flush_after_release = true; }
static ULONG STDMETHODCALLTYPE fake_texture_release(ID3D11Texture2D *texture)
{ (void)texture; texture_released = true; return 0; }
static ULONG STDMETHODCALLTYPE fake_chain_release(IDXGISwapChain2 *chain)
{ (void)chain; chain_ordered = texture_released && flush_after_release; return 0; }
static ULONG STDMETHODCALLTYPE fake_context_release(ID3D11DeviceContext *context)
{ (void)context; return 0; }
static HRESULT STDMETHODCALLTYPE fake_fullscreen(IDXGISwapChain2 *chain, BOOL state, IDXGIOutput *output)
{ (void)chain; (void)state; (void)output; return S_OK; }
static int teardown_order(void)
{
    ID3D11Texture2DVtbl tv = {.Release = fake_texture_release}; ID3D11Texture2D texture = {&tv};
    IDXGISwapChain2Vtbl cv = {.Release = fake_chain_release,.SetFullscreenState = fake_fullscreen};
    IDXGISwapChain2 chain = {&cv};
    ID3D11DeviceContextVtbl xv = {.Release = fake_context_release,.ClearState = fake_clear,.Flush = fake_flush};
    ID3D11DeviceContext context = {&xv};
    d3d11_state s = {.backbuffer = &texture,.swapchain = &chain,.context = &context};
    d3d11_release(&s);
    T(chain_ordered);
    puts("d3d11_win_test: release dependent resources, flush, release swapchain PASS");
    return 0;
}
static bool present_committed;
static HRESULT STDMETHODCALLTYPE fake_query(ID3D11DeviceContext *context, ID3D11Asynchronous *query,
    void *data, UINT bytes, UINT flags)
{
    (void)context; (void)query; (void)bytes; (void)flags;
    *(BOOL *)data = present_committed ? TRUE : FALSE;
    return present_committed ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE fake_present(IDXGISwapChain2 *chain, UINT interval, UINT flags,
    const DXGI_PRESENT_PARAMETERS *params)
{ (void)chain; (void)interval; (void)flags; (void)params; present_committed = true; return S_OK; }
static HRESULT STDMETHODCALLTYPE fake_count(IDXGISwapChain2 *chain, UINT *count)
{ (void)chain; *count = 7; return S_OK; }
static int query_waits_for_present(void)
{
    ID3D11DeviceContextVtbl xv = {.GetData = fake_query}; ID3D11DeviceContext context = {&xv};
    IDXGISwapChain2Vtbl cv = {.Present1 = fake_present,.GetLastPresentCount = fake_count};
    IDXGISwapChain2 chain = {&cv};
    d3d11_state s = {.context = &context,.swapchain = &chain,.drawn = true,.latency_ready = true,.resume_pending = true};
    render_backend b = {.state = &s};
    T(d3d11_present(&b,41) == RENDER_OK && present_committed && s.swapped);
    T(!s.resume_pending);
    puts("d3d11_win_test: pending GPU query cannot prevent native Present progress PASS");
    return 0;
}
static void blocked_progress(work_ctx *ctx)
{ (void)WaitForSingleObject(ctx->arg,INFINITE); }
static int statistics_recovery(void)
{
    render_config cfg = {.max_cells = 1,.max_pages = 1,.max_glyphs = 1,
        .max_region_width = 1,.max_region_height = 1};
    for (unsigned blocked = 0; blocked < 2; blocked++) {
        d3d11_state s = {.atlas_width = 1,.atlas_height = 1}; T(d3d11_reserve(&s,&cfg));
        IDXGISwapChain2Vtbl cv = {.GetFrameStatistics = fake_stats}; IDXGISwapChain2 chain = {&cv};
        ID3D11DeviceVtbl dv = {.GetDeviceRemovedReason = fake_removed}; ID3D11Device device = {&dv};
        s.swapchain = &chain; s.device = &device; s.swapped = true; s.latency_ready = true;
        s.query_ready = true; s.pending_count = 7; s.deadline = 1; s.last_observation = os_now_ns();
        unsigned hooks = 0;
        render_backend b = {.state = &s,.initialized = true,.active = true,.presented = true,.active_frame = 41,
            .config = {.hooks = {hook_count,hook_count,&hooks}}};
        T(render_d3d11_backend(&(render_backend){0}) == RENDER_OK);
        b.info.name = "d3d11"; b.info.capabilities = RENDER_CAP_PRESENT_TIMING;
        work_msg msg = {.kind = D3D11_POLL_MESSAGE}; render_event event = {RENDER_EVENT_WORK,41,0,&msg};
        HANDLE gate = CreateEventW(NULL,TRUE,FALSE,NULL); T(gate);
        if (blocked) {
            s.sync_job = work_submit(s.sync_pool,(work_job){blocked_progress,gate,41,WORK_BULK});
            T(s.sync_job.epoch);
        }
        empty_statistics = true;
        int rc = RENDER_OK;
        uint64_t limit = os_now_ns()+D3D11_COMPLETION_NS;
        do { rc = d3d11_event(&b,&event); if (rc == RENDER_OK) Sleep(1); }
        while (rc == RENDER_OK && os_now_ns() < limit);
        SetEvent(gate);
        T(rc == RENDER_ERR_UNSUPPORTED && d3d11_recovery_required(&b) && !s.failed);
        T(b.active && hooks == 1 && !b.complete_seen);
        T(!(b.info.capabilities & RENDER_CAP_PRESENT_TIMING));
        s.swapchain = NULL; s.device = NULL; d3d11_release(&s); CloseHandle(gate);
    }
    empty_statistics = false;
    puts("d3d11_win_test: missing statistics/blocked progress request rebuild, no fabricated T6 PASS");
    return 0;
}
static int readiness_recovery(void)
{
    HANDLE latency = CreateEventW(NULL,FALSE,FALSE,NULL); T(latency);
    d3d11_state s = {.latency = latency,.deadline = 1,.last_observation = os_now_ns()};
    render_backend b = {.state = &s,.initialized = true,.info = {.name = "d3d11"}};
    int rc = d3d11_present(&b,41); CloseHandle(latency);
    T(rc == RENDER_ERR_UNSUPPORTED && d3d11_recovery_required(&b) && !s.failed);
    puts("d3d11_win_test: continuously observed missing latency requests healthy-device rebuild PASS");
    return 0;
}
int main(void) {
    if (disabled_scroll() || atlas_rectangles()) return 1;
    if (timing_capability()) return 1;
    if (statistics_progress()) return 1;
    if (delayed_retry()) return 1;
    if (teardown_order()) return 1;
    if (query_waits_for_present()) return 1;
    if (statistics_recovery()) return 1;
    return readiness_recovery();
}
