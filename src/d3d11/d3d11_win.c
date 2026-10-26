/* W1.2: C COM backend; exclusive init worker, UI submission and completion.
 * Cells draw to a retained scene; a second pass applies exact Q8 scrolling.
 * The retained output repairs flip-buffer age before every presentation. */
#if defined(_WIN32)
#include "d3d11/d3d11.h"
#include "base/base.h"
#include "os/os.h"
#include "work/work.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <limits.h>
#include <string.h>

#define D3D11_POLL_MESSAGE UINT32_C(0x4433504c)
/* (G) Missing-completion watchdog follows GL_COMPLETION_TIMEOUT_NS. This is
 * an inherited recovery policy, not a frame-time performance assertion. */
#define D3D11_COMPLETION_NS UINT64_C(3000000000)
#define D3D11_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)
typedef struct d3d11_instance { uint32_t pos[4], glyph[4], color[4]; } d3d11_instance;
typedef struct d3d11_strip { render_strip rows; UINT first, count; } d3d11_strip;
typedef struct d3d11_constants { uint32_t surface[4], motion[4]; } d3d11_constants;
typedef struct d3d11_state {
    HWND window;
    bool force_warp, readback, warp, debug, failed, swapped, drawn;
    bool own_arena, full_frame, region_upload, latency_ready;
    edit_arena arena;
    HMODULE compiler;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain2 *swapchain;
    IDXGISwapChainMedia *media;
    HANDLE latency;
    ID3D11Texture2D *backbuffer, *scene, *target, *atlas, *region_texture, *staging;
    ID3D11RenderTargetView *scene_rtv, *target_rtv;
    ID3D11ShaderResourceView *scene_srv, *atlas_srv;
    ID3D11VertexShader *cell_vs, *copy_vs;
    ID3D11PixelShader *cell_ps, *copy_ps, *margin_ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *instances_vbo, *constants;
    ID3D11RasterizerState *rasterizer;
    ID3D11Query *query;
    d3d11_instance *instances;
    d3d11_strip *strips;
    size_t strip_count, instance_count;
    size_t *page_offsets;
    uint8_t *atlas_cache;
    uint32_t *region_pixels;
    render_pixels region;
    render_scroll scroll;
    render_dims dims;
    uint32_t surface_background, width, height, max_width, max_height, atlas_width, atlas_height;
    UINT pending_count;
    HRESULT present_error;
    d3d11_present_info present_info;
    uint64_t deadline;
} d3d11_state;

static bool d3d11_descriptor(const render_backend *b)
{
    return b && b->info.name && strcmp(b->info.name,"d3d11") == 0;
}
static int d3d11_fail(d3d11_state *s)
{ s->failed = true; return RENDER_ERR_DEVICE; }
static void d3d11_release(d3d11_state *s)
{
    if (s->context) {
        s->context->lpVtbl->ClearState(s->context);
        s->context->lpVtbl->Flush(s->context);
        /* Quiescent shutdown may wait. Drain the last device fence before
         * freeing the init arena; device loss/expiry releases native refs. */
        if (s->drawn && s->query) {
            uint64_t deadline = os_now_ns()+D3D11_COMPLETION_NS;
            for (;;) {
                BOOL ready = FALSE;
                HRESULT hr = s->context->lpVtbl->GetData(s->context,(ID3D11Asynchronous *)s->query,
                    &ready,sizeof ready,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (FAILED(hr) || (hr == S_OK && ready) || os_now_ns() >= deadline) break;
                Sleep(1);
            }
        }
    }
    if (s->swapchain) (void)s->swapchain->lpVtbl->SetFullscreenState(s->swapchain,FALSE,NULL);
    D3D11_RELEASE(s->query);
    D3D11_RELEASE(s->staging); D3D11_RELEASE(s->region_texture);
    D3D11_RELEASE(s->rasterizer); D3D11_RELEASE(s->constants);
    D3D11_RELEASE(s->instances_vbo); D3D11_RELEASE(s->layout);
    D3D11_RELEASE(s->cell_vs); D3D11_RELEASE(s->copy_vs);
    D3D11_RELEASE(s->cell_ps); D3D11_RELEASE(s->copy_ps); D3D11_RELEASE(s->margin_ps);
    D3D11_RELEASE(s->scene_rtv); D3D11_RELEASE(s->target_rtv);
    D3D11_RELEASE(s->scene_srv); D3D11_RELEASE(s->atlas_srv);
    D3D11_RELEASE(s->scene); D3D11_RELEASE(s->target); D3D11_RELEASE(s->atlas);
    D3D11_RELEASE(s->backbuffer); D3D11_RELEASE(s->media); D3D11_RELEASE(s->swapchain);
    if (s->latency) { CloseHandle(s->latency); s->latency = NULL; }
    D3D11_RELEASE(s->context); D3D11_RELEASE(s->device);
    if (s->compiler) { FreeLibrary(s->compiler); s->compiler = NULL; }
    if (s->own_arena) edit_arena_free(&s->arena);
}
static HRESULT d3d11_try_device(d3d11_state *s, D3D_DRIVER_TYPE driver)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(NULL,driver,NULL,flags,&level,1,D3D11_SDK_VERSION,
                                  &s->device,NULL,&s->context);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~(UINT)D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(NULL,driver,NULL,flags,&level,1,D3D11_SDK_VERSION,
                               &s->device,NULL,&s->context);
    }
    s->debug = SUCCEEDED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
    return hr;
}
static bool d3d11_native_init(d3d11_state *s)
{
    HRESULT hr = E_FAIL;
    if (!s->force_warp) hr = d3d11_try_device(s,D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(hr)) {
        D3D11_RELEASE(s->context); D3D11_RELEASE(s->device);
        hr = d3d11_try_device(s,D3D_DRIVER_TYPE_WARP); s->warp = true;
    }
    if (FAILED(hr)) return false;
    IDXGIDevice *dxgi = NULL; IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL; IDXGISwapChain1 *chain = NULL;
    hr = s->device->lpVtbl->QueryInterface(s->device,&IID_IDXGIDevice,(void **)&dxgi);
    if (SUCCEEDED(hr)) hr = dxgi->lpVtbl->GetAdapter(dxgi,&adapter);
    if (SUCCEEDED(hr)) hr = adapter->lpVtbl->GetParent(adapter,&IID_IDXGIFactory2,(void **)&factory);
    DXGI_SWAP_CHAIN_DESC1 desc = {.Width = s->width,.Height = s->height,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc = {1,0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,.BufferCount = 2,
        .Scaling = DXGI_SCALING_STRETCH,.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT};
    /* (P) Flip model requires >=2 buffers. MaxFrameLatency=1 and the common
     * adapter's single active frame independently bound the submission queue. */
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateSwapChainForHwnd(factory,(IUnknown *)s->device,
                                                s->window,&desc,NULL,NULL,&chain);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->MakeWindowAssociation(factory,s->window,DXGI_MWA_NO_ALT_ENTER);
    if (SUCCEEDED(hr)) hr = chain->lpVtbl->QueryInterface(chain,&IID_IDXGISwapChain2,(void **)&s->swapchain);
    if (SUCCEEDED(hr)) hr = s->swapchain->lpVtbl->SetMaximumFrameLatency(s->swapchain,1);
    if (SUCCEEDED(hr)) {
        s->latency = s->swapchain->lpVtbl->GetFrameLatencyWaitableObject(s->swapchain);
        if (!s->latency) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = s->swapchain->lpVtbl->GetBuffer(s->swapchain,0,&IID_ID3D11Texture2D,(void **)&s->backbuffer);
    if (SUCCEEDED(hr)) (void)s->swapchain->lpVtbl->QueryInterface(s->swapchain,&IID_IDXGISwapChainMedia,(void **)&s->media);
    D3D11_RELEASE(chain); D3D11_RELEASE(factory); D3D11_RELEASE(adapter); D3D11_RELEASE(dxgi);
    return SUCCEEDED(hr);
}
static bool d3d11_texture(d3d11_state *s, uint32_t w, uint32_t h, DXGI_FORMAT format,
                          UINT bind, ID3D11Texture2D **out)
{
    D3D11_TEXTURE2D_DESC desc = {.Width = w,.Height = h,.MipLevels = 1,.ArraySize = 1,
        .Format = format,.SampleDesc = {1,0},.Usage = D3D11_USAGE_DEFAULT,.BindFlags = bind};
    return SUCCEEDED(s->device->lpVtbl->CreateTexture2D(s->device,&desc,NULL,out));
}
static bool d3d11_buffer(d3d11_state *s, size_t bytes, UINT bind, ID3D11Buffer **out)
{
    if (!bytes || bytes > UINT_MAX) return false;
    D3D11_BUFFER_DESC desc = {.ByteWidth = (UINT)bytes,.Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = bind,.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    return SUCCEEDED(s->device->lpVtbl->CreateBuffer(s->device,&desc,NULL,out));
}
/* Integer R8 and /255 reproduce render.h in encoded sRGB, without hardware
 * sRGB conversion or alpha blending. Scene pixels are quantised before the
 * Q8 composition, exactly as raster_row_scrolled's two scalar samples. */
static bool d3d11_shaders(d3d11_state *s)
{
    const char *source =
        "cbuffer C:register(b0){uint4 surface;uint4 motion;}\n"
        "Texture2D<uint> atlas:register(t0);Texture2D<float4> scene:register(t1);\n"
        "struct I{uint4 pos:POSITION;uint4 glyph:TEXCOORD0;uint4 color:COLOR0;};\n"
        "struct V{float4 p:SV_Position;float2 local:TEXCOORD0;nointerpolation uint4 glyph:TEXCOORD1;"
        "nointerpolation uint4 color:TEXCOORD2;nointerpolation uint2 extent:TEXCOORD3;};\n"
        "V cell_vs(I i,uint v:SV_VertexID){V o;float2 q=float2(v&1,v>>1);"
        "o.local=q*i.pos.zw;float2 p=i.pos.xy+o.local;"
        "o.p=float4(p.x*2/surface.x-1,1-p.y*2/surface.y,0,1);"
        "o.glyph=i.glyph;o.color=i.color;o.extent=i.pos.zw;return o;}\n"
        "uint3 rgb(uint c){return uint3((c>>16)&255,(c>>8)&255,c&255);}\n"
        "float4 cell_ps(V i):SV_Target{uint attrs=i.color.z;if(attrs&32)discard;"
        "uint2 p=(uint2)i.local;uint a=0;if(p.x<i.glyph.z&&p.y<i.glyph.w){"
        "uint n=i.glyph.x+p.y*i.glyph.y+p.x;a=atlas.Load(int3(n%surface.z,n/surface.z,0));}"
        "if((attrs&4)&&p.y==i.extent.y-1)a=255;uint3 fg=rgb(i.color.x),bg=rgb(i.color.y);"
        "if(attrs&8){uint3 t=fg;fg=bg;bg=t;}uint3 c=(fg*a+bg*(255-a)+127)/255;"
        "return float4(float3(c)/255.0,1);}\n"
        "float4 copy_vs(uint v:SV_VertexID):SV_Position{float2 q=float2(v&1,v>>1);return float4(q.x*2-1,1-q.y*2,0,1);}\n"
        "float4 copy_ps(float4 p:SV_Position):SV_Target{uint2 xy=(uint2)p.xy;"
        "if(motion.w&&xy.x<motion.x&&xy.y>=motion.y&&xy.y<motion.z){"
        "uint whole=motion.w/256,f=motion.w%256;xy.y+=whole;"
        "uint3 a=(uint3)round(scene.Load(int3(xy,0)).rgb*255);"
        "uint3 b=(uint3)round(scene.Load(int3(xy+uint2(0,1),0)).rgb*255);"
        "return float4(float3((a*(256-f)+b*f+128)/256)/255.0,1);}"
        "return scene.Load(int3(xy,0));}\n"
        "float4 margin_ps(float4 p:SV_Position):SV_Target{return float4(float3(rgb(surface.w))/255.0,1); }\n";
    s->compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!s->compiler) return false;
    FARPROC proc = GetProcAddress(s->compiler,"D3DCompile");
    pD3DCompile compile = NULL;
    _Static_assert(sizeof proc == sizeof compile,"Windows function pointer size");
    memcpy(&compile,&proc,sizeof compile); if (!compile) return false;
    const char *entries[] = {"cell_vs","cell_ps","copy_vs","copy_ps","margin_ps"};
    for (size_t i = 0; i < 5; i++) {
        ID3DBlob *code = NULL, *errors = NULL;
        HRESULT hr = compile(source,strlen(source),"sublimite-d3d11",NULL,NULL,entries[i],
                             i == 0 || i == 2 ? "vs_5_0" : "ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        D3D11_RELEASE(errors);
        if (FAILED(hr)) { D3D11_RELEASE(code); return false; }
        const void *data = code->lpVtbl->GetBufferPointer(code);
        SIZE_T bytes = code->lpVtbl->GetBufferSize(code);
        if (i == 0) {
            hr = s->device->lpVtbl->CreateVertexShader(s->device,data,bytes,NULL,&s->cell_vs);
            D3D11_INPUT_ELEMENT_DESC attrs[] = {
                {"POSITION",0,DXGI_FORMAT_R32G32B32A32_UINT,0,0,D3D11_INPUT_PER_INSTANCE_DATA,1},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_UINT,0,16,D3D11_INPUT_PER_INSTANCE_DATA,1},
                {"COLOR",0,DXGI_FORMAT_R32G32B32A32_UINT,0,32,D3D11_INPUT_PER_INSTANCE_DATA,1}};
            if (SUCCEEDED(hr)) hr = s->device->lpVtbl->CreateInputLayout(s->device,attrs,3,data,bytes,&s->layout);
        } else if (i == 1) hr = s->device->lpVtbl->CreatePixelShader(s->device,data,bytes,NULL,&s->cell_ps);
        else if (i == 2) hr = s->device->lpVtbl->CreateVertexShader(s->device,data,bytes,NULL,&s->copy_vs);
        else if (i == 3) hr = s->device->lpVtbl->CreatePixelShader(s->device,data,bytes,NULL,&s->copy_ps);
        else hr = s->device->lpVtbl->CreatePixelShader(s->device,data,bytes,NULL,&s->margin_ps);
        D3D11_RELEASE(code); if (FAILED(hr)) return false;
    }
    return true;
}
static bool d3d11_resources(d3d11_state *s, const render_config *cfg)
{
    const UINT rtv = D3D11_BIND_RENDER_TARGET, srv = D3D11_BIND_SHADER_RESOURCE;
    if (!d3d11_texture(s,s->max_width,s->max_height*2u,DXGI_FORMAT_B8G8R8A8_UNORM,rtv|srv,&s->scene) ||
        !d3d11_texture(s,s->max_width,s->max_height,DXGI_FORMAT_B8G8R8A8_UNORM,rtv,&s->target) ||
        !d3d11_texture(s,s->atlas_width,s->atlas_height,DXGI_FORMAT_R8_UINT,srv,&s->atlas)) return false;
    if (FAILED(s->device->lpVtbl->CreateRenderTargetView(s->device,(ID3D11Resource *)s->scene,NULL,&s->scene_rtv)) ||
        FAILED(s->device->lpVtbl->CreateRenderTargetView(s->device,(ID3D11Resource *)s->target,NULL,&s->target_rtv)) ||
        FAILED(s->device->lpVtbl->CreateShaderResourceView(s->device,(ID3D11Resource *)s->scene,NULL,&s->scene_srv)) ||
        FAILED(s->device->lpVtbl->CreateShaderResourceView(s->device,(ID3D11Resource *)s->atlas,NULL,&s->atlas_srv))) return false;
    if (cfg->max_region_width && cfg->max_region_height &&
        !d3d11_texture(s,cfg->max_region_width,cfg->max_region_height,DXGI_FORMAT_B8G8R8A8_UNORM,0,&s->region_texture)) return false;
    if (s->readback) {
        D3D11_TEXTURE2D_DESC desc = {.Width = s->max_width,.Height = s->max_height,
            .MipLevels = 1,.ArraySize = 1,.Format = DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc = {1,0},
            .Usage = D3D11_USAGE_STAGING,.CPUAccessFlags = D3D11_CPU_ACCESS_READ};
        if (FAILED(s->device->lpVtbl->CreateTexture2D(s->device,&desc,NULL,&s->staging))) return false;
    }
    if (!d3d11_buffer(s,cfg->max_cells*sizeof(d3d11_instance),D3D11_BIND_VERTEX_BUFFER,&s->instances_vbo) ||
        !d3d11_buffer(s,sizeof(d3d11_constants),D3D11_BIND_CONSTANT_BUFFER,&s->constants)) return false;
    D3D11_RASTERIZER_DESC raster = {.FillMode = D3D11_FILL_SOLID,.CullMode = D3D11_CULL_NONE,
                                   .DepthClipEnable = TRUE,.ScissorEnable = TRUE};
    D3D11_QUERY_DESC query = {.Query = D3D11_QUERY_EVENT};
    return SUCCEEDED(s->device->lpVtbl->CreateRasterizerState(s->device,&raster,&s->rasterizer)) &&
        SUCCEEDED(s->device->lpVtbl->CreateQuery(s->device,&query,&s->query)) && d3d11_shaders(s);
}
static bool d3d11_reserve(d3d11_state *s, const render_config *cfg)
{
    /* Include alignment slack for each allocation; all products were bounded
     * by init's D3D limits before entering here. */
    size_t atlas_bytes = (size_t)s->atlas_width*s->atlas_height;
    size_t strips = cfg->max_cells/2u+1u;
    size_t sizes[] = {cfg->max_cells*sizeof(d3d11_instance),strips*sizeof(d3d11_strip),
        cfg->max_pages*sizeof(size_t),atlas_bytes,
        (size_t)cfg->max_region_width*cfg->max_region_height*sizeof(uint32_t)};
    size_t total = 0;
    for (size_t i = 0; i < sizeof sizes/sizeof sizes[0]; i++) {
        if (sizes[i] > SIZE_MAX-total-_Alignof(max_align_t)) return false;
        total += sizes[i]+_Alignof(max_align_t);
    }
    edit_arena *arena = cfg->arena;
    if (!arena) {
        if (edit_arena_init(&s->arena,total) != 0) return false;
        s->own_arena = true; arena = &s->arena;
    }
    s->instances = edit_arena_alloc(arena,sizes[0],_Alignof(d3d11_instance));
    s->strips = edit_arena_alloc(arena,sizes[1],_Alignof(d3d11_strip));
    s->page_offsets = edit_arena_alloc(arena,sizes[2],_Alignof(size_t));
    s->atlas_cache = edit_arena_alloc(arena,sizes[3],_Alignof(uint8_t));
    s->region_pixels = edit_arena_alloc(arena,sizes[4],_Alignof(uint32_t));
    if (!s->instances || !s->strips || !s->page_offsets || !s->atlas_cache || !s->region_pixels) return false;
    memset(s->atlas_cache,0,atlas_bytes); return true;
}
static int d3d11_init(render_backend *b, const render_config *cfg)
{
    d3d11_state *s = b->state;
    HWND window = s->window; bool warp = s->force_warp, readback = s->readback;
    memset(s,0,sizeof *s); s->window = window; s->force_warp = warp; s->readback = readback;
    if (!cfg->platform) return RENDER_ERR_UNSUPPORTED;
    if (!window || !IsWindow(window)) return RENDER_ERR_ARG;
    /* (P) D3D11 feature level 11 texture dimension = 16384; scene reserves an
     * extra max_height for any admissible overscan cell_h after resize. */
    const uint32_t limit = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    if (cfg->max_width > limit || cfg->max_height > limit/2u ||
        cfg->max_cells > UINT_MAX/sizeof(d3d11_instance) ||
        cfg->max_atlas_bytes > (size_t)limit*limit) return RENDER_ERR_CAPACITY;
    RECT client;
    if (!GetClientRect(window,&client) || client.right <= 0 || client.bottom <= 0) return RENDER_ERR_UNSUPPORTED;
    s->width = (uint32_t)client.right; s->height = (uint32_t)client.bottom;
    if (s->width > cfg->max_width || s->height > cfg->max_height) return RENDER_ERR_CAPACITY;
    s->max_width = cfg->max_width; s->max_height = cfg->max_height; s->dims = cfg->dims;
    s->atlas_width = cfg->max_atlas_bytes < limit ? (uint32_t)cfg->max_atlas_bytes : limit;
    if (!s->atlas_width) s->atlas_width = 1;
    s->atlas_height = (uint32_t)((cfg->max_atlas_bytes+s->atlas_width-1u)/s->atlas_width);
    if (!s->atlas_height) s->atlas_height = 1;
    edit_arena_mark_t mark = cfg->arena ? edit_arena_mark(cfg->arena) : 0;
    int rc = RENDER_ERR_INIT;
    if (!d3d11_reserve(s,cfg)) goto fail;
    rc = RENDER_ERR_UNSUPPORTED;
    if (!d3d11_native_init(s)) goto fail;
    rc = RENDER_ERR_INIT;
    if (!d3d11_resources(s,cfg)) goto fail;
    DXGI_FRAME_STATISTICS initial_stats = {0};
    (void)s->swapchain->lpVtbl->GetFrameStatistics(s->swapchain,&initial_stats);
    if (s->media) {
        DXGI_FRAME_STATISTICS_MEDIA initial_media = {0};
        (void)s->media->lpVtbl->GetFrameStatisticsMedia(s->media,&initial_media);
    }
    /* No diagnostic Present, including background_init. The first real frame
     * must prove matching statistics; a missing/unsupported observation is
     * terminal after the inherited completion watchdog, never endless BUSY. */
    return RENDER_OK;
fail:
    d3d11_release(s);
    if (cfg->arena) edit_arena_reset_to_mark(cfg->arena,mark);
    return rc;
}
static int d3d11_resize(render_backend *b, render_dims dims)
{
    d3d11_state *s = b->state;
    if (s->failed) return RENDER_ERR_DEVICE;
    RECT client;
    if (!GetClientRect(s->window,&client) || client.right <= 0 || client.bottom <= 0) return RENDER_ERR_BOUNDS;
    uint32_t w = (uint32_t)client.right, h = (uint32_t)client.bottom;
    if (w > s->max_width || h > s->max_height) return RENDER_ERR_CAPACITY;
    if (w != s->width || h != s->height) {
        s->context->lpVtbl->ClearState(s->context);
        D3D11_RELEASE(s->backbuffer);
        HRESULT hr = s->swapchain->lpVtbl->ResizeBuffers(s->swapchain,0,w,h,DXGI_FORMAT_UNKNOWN,
                                                    DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        if (FAILED(hr) || FAILED(s->swapchain->lpVtbl->GetBuffer(s->swapchain,0,&IID_ID3D11Texture2D,(void **)&s->backbuffer)))
            return d3d11_fail(s);
        s->width = w; s->height = h;
    }
    s->dims = dims; return RENDER_OK;
}
static int d3d11_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    d3d11_state *s = b->state;
    if (s->failed) return RENDER_ERR_DEVICE;
    if (g->scroll.origin_y_q8 && !g->full_frame) return RENDER_ERR_STRIPS;
    /* Descriptors are consumed into value-only instances and packed pixels;
     * not even an atlas pointer survives submit. No native calls/allocations. */
    size_t offset = 0;
    for (size_t page = 0; page < g->page_count; page++) {
        const render_atlas_page *p = &g->pages[page]; s->page_offsets[page] = offset;
        for (uint32_t y = 0; y < p->height; y++)
            memcpy(s->atlas_cache+offset+(size_t)y*p->width,p->pixels+(size_t)y*p->stride,p->width);
        offset += (size_t)p->width*p->height;
    }
    render_strip expanded = {0,g->dims.rows+1u};
    bool fractional = g->scroll.origin_y_q8 != 0;
    const render_strip *draw = fractional ? &expanded : strips;
    s->strip_count = fractional ? 1u : count;
    s->instance_count = 0;
    for (size_t i = 0; i < s->strip_count; i++) {
        UINT first = (UINT)s->instance_count;
        uint32_t end = draw[i].first_row+draw[i].row_count;
        for (uint32_t row = draw[i].first_row; row < end; row++) for (uint32_t col = 0; col < g->dims.cols; col++) {
            const render_cell *c = &g->cells[(size_t)row*g->dims.cols+col];
            d3d11_instance *instance = &s->instances[s->instance_count++];
            *instance = (d3d11_instance){.pos = {col*g->dims.cell_w,row*g->dims.cell_h,
                g->dims.cell_w*((c->attrs & RENDER_ATTR_WIDE_LEFT) ? 2u : 1u),g->dims.cell_h},
                .color = {c->fg,c->bg,c->attrs,0}};
            if (c->atlas_slot != RENDER_NO_SLOT) {
                const render_glyph *glyph = &g->glyphs[c->atlas_slot];
                const render_atlas_page *page = &g->pages[glyph->page];
                instance->glyph[0] = (uint32_t)(s->page_offsets[glyph->page]+(size_t)glyph->y*page->width+glyph->x);
                instance->glyph[1] = page->width; instance->glyph[2] = glyph->w; instance->glyph[3] = glyph->h;
            }
        }
        s->strips[i] = (d3d11_strip){draw[i],first,(UINT)s->instance_count-first};
    }
    s->region = g->region;
    if (g->region.pixels) {
        for (uint32_t y = 0; y < g->region.height; y++) for (uint32_t x = 0; x < g->region.width; x++)
            s->region_pixels[(size_t)y*g->region.width+x] =
                g->region.pixels[(size_t)y*g->region.stride+x] | UINT32_C(0xff000000);
        s->region.pixels = s->region_pixels; s->region.stride = s->region.width;
        s->region.capacity = (size_t)s->region.width*s->region.height;
    }
    s->region_upload = g->region.pixels && (g->region.dirty || g->full_frame || count);
    s->full_frame = g->full_frame; s->scroll = g->scroll;
    s->surface_background = g->surface_background; s->swapped = false; s->drawn = false;
    s->pending_count = 0; s->present_error = S_OK; s->deadline = 0;
    s->present_info = (d3d11_present_info){0};
    return RENDER_OK;
}
static bool d3d11_upload_buffer(d3d11_state *s, ID3D11Buffer *buffer, const void *data, size_t bytes)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(s->context->lpVtbl->Map(s->context,(ID3D11Resource *)buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
    memcpy(mapped.pData,data,bytes);
    s->context->lpVtbl->Unmap(s->context,(ID3D11Resource *)buffer,0); return true;
}
static void d3d11_background(uint32_t rgb, float *out)
{
    out[0] = (float)((rgb>>16)&255u)/255.0f; out[1] = (float)((rgb>>8)&255u)/255.0f;
    out[2] = (float)(rgb&255u)/255.0f; out[3] = 1.0f;
}
static bool d3d11_draw(d3d11_state *s)
{
    ID3D11DeviceContext *c = s->context;
    d3d11_constants constants = {.surface = {s->max_width,s->max_height*2u,s->atlas_width,s->surface_background},
        .motion = {s->scroll.cols*s->dims.cell_w,s->scroll.first_row*s->dims.cell_h,
            (s->scroll.first_row+s->scroll.rows)*s->dims.cell_h,(uint32_t)(-s->scroll.origin_y_q8)}};
    if (!d3d11_upload_buffer(s,s->constants,&constants,sizeof constants)) return false;
    if (s->instance_count && !d3d11_upload_buffer(s,s->instances_vbo,s->instances,
                                                s->instance_count*sizeof(d3d11_instance))) return false;
    ID3D11ShaderResourceView *empty[2] = {NULL,NULL};
    c->lpVtbl->PSSetShaderResources(c,0,2,empty);
    c->lpVtbl->UpdateSubresource(c,(ID3D11Resource *)s->atlas,0,NULL,s->atlas_cache,s->atlas_width,0);
    c->lpVtbl->RSSetState(c,s->rasterizer);
    c->lpVtbl->VSSetConstantBuffers(c,0,1,&s->constants);
    c->lpVtbl->PSSetConstantBuffers(c,0,1,&s->constants);
    float bg[4]; d3d11_background(s->surface_background,bg);
    if (s->full_frame) {
        c->lpVtbl->ClearRenderTargetView(c,s->scene_rtv,bg);
        c->lpVtbl->ClearRenderTargetView(c,s->target_rtv,bg);
    }
    c->lpVtbl->OMSetRenderTargets(c,1,&s->scene_rtv,NULL);
    D3D11_VIEWPORT viewport = {0,0,(float)s->max_width,(float)(s->max_height*2u),0,1};
    c->lpVtbl->RSSetViewports(c,1,&viewport);
    c->lpVtbl->IASetInputLayout(c,s->layout);
    c->lpVtbl->IASetPrimitiveTopology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->lpVtbl->VSSetShader(c,s->cell_vs,NULL,0); c->lpVtbl->PSSetShader(c,s->cell_ps,NULL,0);
    c->lpVtbl->PSSetShaderResources(c,0,1,&s->atlas_srv);
    UINT stride = sizeof(d3d11_instance), offset = 0;
    c->lpVtbl->IASetVertexBuffers(c,0,1,&s->instances_vbo,&stride,&offset);
    for (size_t i = 0; i < s->strip_count; i++) {
        const d3d11_strip *strip = &s->strips[i];
        D3D11_RECT clip = {0,(LONG)(strip->rows.first_row*s->dims.cell_h),
            (LONG)(s->dims.cols*s->dims.cell_w),(LONG)((strip->rows.first_row+strip->rows.row_count)*s->dims.cell_h)};
        c->lpVtbl->RSSetScissorRects(c,1,&clip);
        c->lpVtbl->DrawInstanced(c,4,strip->count,0,strip->first);
    }
    c->lpVtbl->OMSetRenderTargets(c,1,&s->target_rtv,NULL);
    c->lpVtbl->PSSetShaderResources(c,1,1,&s->scene_srv);
    c->lpVtbl->IASetInputLayout(c,NULL);
    c->lpVtbl->VSSetShader(c,s->copy_vs,NULL,0); c->lpVtbl->PSSetShader(c,s->copy_ps,NULL,0);
    viewport.Height = (float)s->max_height; c->lpVtbl->RSSetViewports(c,1,&viewport);
    for (size_t i = 0; i < s->strip_count; i++) {
        const render_strip *strip = &s->strips[i].rows;
        uint32_t end = (strip->first_row+strip->row_count)*s->dims.cell_h;
        uint32_t visible = s->dims.rows*s->dims.cell_h;
        if (end > visible) end = visible;
        D3D11_RECT clip = {0,(LONG)(strip->first_row*s->dims.cell_h),
            (LONG)(s->dims.cols*s->dims.cell_w),(LONG)end};
        c->lpVtbl->RSSetScissorRects(c,1,&clip); c->lpVtbl->Draw(c,4,0);
    }
    /* Like GL, surface margins are independent of cell strips. A theme/base
     * change with zero cell damage still repaints every native margin. */
    c->lpVtbl->PSSetShader(c,s->margin_ps,NULL,0);
    uint32_t grid_w = s->dims.cols*s->dims.cell_w, grid_h = s->dims.rows*s->dims.cell_h;
    if (s->width > grid_w) {
        D3D11_RECT clip = {(LONG)grid_w,0,(LONG)s->width,(LONG)s->height};
        c->lpVtbl->RSSetScissorRects(c,1,&clip); c->lpVtbl->Draw(c,4,0);
    }
    if (s->height > grid_h) {
        D3D11_RECT clip = {0,(LONG)grid_h,(LONG)(s->width < grid_w ? s->width : grid_w),(LONG)s->height};
        c->lpVtbl->RSSetScissorRects(c,1,&clip); c->lpVtbl->Draw(c,4,0);
    }
    c->lpVtbl->OMSetRenderTargets(c,0,NULL,NULL); c->lpVtbl->PSSetShaderResources(c,0,2,empty);
    if (s->region_upload) {
        D3D11_BOX box = {0,0,0,s->region.width,s->region.height,1};
        c->lpVtbl->UpdateSubresource(c,(ID3D11Resource *)s->region_texture,0,&box,s->region_pixels,
                                    s->region.width*sizeof(uint32_t),0);
        c->lpVtbl->CopySubresourceRegion(c,(ID3D11Resource *)s->target,0,s->region.x,s->region.y,0,
                                       (ID3D11Resource *)s->region_texture,0,&box);
    }
    D3D11_BOX visible = {0,0,0,s->width,s->height,1};
    c->lpVtbl->CopySubresourceRegion(c,(ID3D11Resource *)s->backbuffer,0,0,0,0,(ID3D11Resource *)s->target,0,&visible);
    if (s->staging) c->lpVtbl->CopySubresourceRegion(c,(ID3D11Resource *)s->staging,0,0,0,0,
                                                 (ID3D11Resource *)s->backbuffer,0,&visible);
    c->lpVtbl->End(c,(ID3D11Asynchronous *)s->query); c->lpVtbl->Flush(c);
    return SUCCEEDED(s->device->lpVtbl->GetDeviceRemovedReason(s->device));
}
static int d3d11_present(render_backend *b, uint32_t id)
{
    (void)id; d3d11_state *s = b->state;
    if (s->failed) return RENDER_ERR_DEVICE;
    if (s->swapped) return RENDER_OK;
    if (!s->deadline) s->deadline = os_now_ns()+D3D11_COMPLETION_NS;
    if (os_now_ns() >= s->deadline) return d3d11_fail(s);
    /* A zero-time poll only. No fence wait or frame-latency wait on UI. */
    if (!s->latency_ready) {
        DWORD ready = WaitForSingleObject(s->latency,0);
        if (ready == WAIT_TIMEOUT) { s->present_info.status = (int32_t)HRESULT_FROM_WIN32(ERROR_IO_PENDING); return RENDER_ERR_BUSY; }
        if (ready != WAIT_OBJECT_0) return d3d11_fail(s);
        s->latency_ready = true;
    }
    if (!s->drawn) {
        if (!d3d11_draw(s)) return d3d11_fail(s);
        s->drawn = true;
    }
    /* Poll the query across present retries, never spin/block on UI. Ordering
     * follows the T5 table literally: event query signalled, then Present. */
    BOOL ready = FALSE;
    HRESULT hr = s->context->lpVtbl->GetData(s->context,(ID3D11Asynchronous *)s->query,&ready,sizeof ready,D3D11_ASYNC_GETDATA_DONOTFLUSH);
    s->present_info.status = (int32_t)hr;
    if (hr == S_FALSE || (hr == S_OK && !ready)) return RENDER_ERR_BUSY;
    if (FAILED(hr)) return d3d11_fail(s);
    DXGI_PRESENT_PARAMETERS params = {0};
    hr = s->swapchain->lpVtbl->Present1(s->swapchain,1,DXGI_PRESENT_DO_NOT_WAIT,&params);
    s->present_info.status = (int32_t)hr;
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return RENDER_ERR_BUSY;
    if (FAILED(hr) || hr == DXGI_STATUS_OCCLUDED) return d3d11_fail(s);
    s->swapped = true; s->latency_ready = false;
    s->deadline = os_now_ns()+D3D11_COMPLETION_NS;
    s->present_error = s->swapchain->lpVtbl->GetLastPresentCount(s->swapchain,&s->pending_count);
    /* Present committed: latch subsequent failure for event, never invite a
     * duplicate retry of an already submitted frame. */
    return RENDER_OK;
}
static int d3d11_event(render_backend *b, const render_event *event)
{
    d3d11_state *s = b->state;
    if (event->work->kind != D3D11_POLL_MESSAGE) return RENDER_ERR_UNSUPPORTED;
    if (s->failed) return RENDER_ERR_DEVICE;
    if (!s->swapped) return s->deadline && os_now_ns() >= s->deadline ? d3d11_fail(s) : RENDER_OK;
    if (FAILED(s->present_error) || FAILED(s->device->lpVtbl->GetDeviceRemovedReason(s->device))) return d3d11_fail(s);
    if (!b->device_seen) {
        /* The query was observed before Present; observation now is the later
         * bound. Adapter clamps to T4 and delivers hooks here on UI. */
        int rc = render_backend_signal(b,RENDER_EVENT_DEVICE_DONE,event->frame_id,os_now_ns());
        if (rc != RENDER_OK) return rc;
    }
    if (!b->complete_seen) {
        if (!s->latency_ready) {
            DWORD ready = WaitForSingleObject(s->latency,0);
            if (ready == WAIT_OBJECT_0) s->latency_ready = true;
            else if (ready != WAIT_TIMEOUT) return d3d11_fail(s);
        }
        DXGI_FRAME_STATISTICS stats = {0};
        HRESULT hr = s->swapchain->lpVtbl->GetFrameStatistics(s->swapchain,&stats);
        if (s->media && (FAILED(hr) || !stats.PresentCount)) {
            DXGI_FRAME_STATISTICS_MEDIA media = {0};
            HRESULT media_hr = s->media->lpVtbl->GetFrameStatisticsMedia(s->media,&media);
            if (SUCCEEDED(media_hr) && media.PresentCount) {
                hr = media_hr; stats.PresentCount = media.PresentCount; stats.SyncQPCTime = media.SyncQPCTime;
            }
        }
        s->present_info.expected = s->pending_count; s->present_info.observed = stats.PresentCount;
        s->present_info.status = (int32_t)hr;
        s->present_info.sync_qpc = (uint64_t)stats.SyncQPCTime.QuadPart;
        if (SUCCEEDED(hr) && stats.PresentCount == s->pending_count && stats.PresentCount != 0) {
            s->present_info.statistics_matched = true;
            /* SyncQPCTime describes SyncRefreshCount, not necessarily this
             * frame's scanout. Use the monotonic observation of its exact
             * PresentCount, never mislabel a raw DXGI sync clock as T6. */
            return render_backend_signal(b,RENDER_EVENT_PRESENT_COMPLETE,event->frame_id,os_now_ns());
        }
        if (SUCCEEDED(hr) && stats.PresentCount > s->pending_count) return d3d11_fail(s);
        if (FAILED(hr) && hr != DXGI_ERROR_FRAME_STATISTICS_DISJOINT) return d3d11_fail(s);

    }
    return os_now_ns() >= s->deadline ? d3d11_fail(s) : RENDER_OK;
}
static void d3d11_shutdown(render_backend *b)
{ d3d11_release(b->state); }
int render_d3d11_backend(render_backend *b)
{
    if (!b) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    *b = (render_backend){.info = {"d3d11",sizeof(d3d11_state),_Alignof(d3d11_state),
        RENDER_CAP_GPU|RENDER_CAP_DEVICE_TIMING|RENDER_CAP_PRESENT_TIMING},
        .ops = {d3d11_init,d3d11_resize,d3d11_submit,d3d11_present,d3d11_event,d3d11_shutdown}};
    return RENDER_OK;
}
int render_d3d11_backend_for_window(render_backend *b, void *hwnd)
{
    if (!d3d11_descriptor(b) || !b->state || !hwnd) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    d3d11_state *s = b->state; s->window = hwnd; return RENDER_OK;
}
int d3d11_diagnostics(render_backend *b, bool force_warp, bool readback)
{
    if (!d3d11_descriptor(b) || !b->state) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    d3d11_state *s = b->state; s->force_warp = force_warp; s->readback = readback; return RENDER_OK;
}
int d3d11_poll_completions(render_backend *b)
{
    if (!d3d11_descriptor(b) || !b->initialized) return RENDER_ERR_STATE;
    d3d11_state *s = b->state;
    if (s->failed) return RENDER_ERR_DEVICE;
    if (!b->active) return RENDER_OK;
    work_msg poll = {.kind = D3D11_POLL_MESSAGE,.generation = b->active_frame};
    render_event event = {RENDER_EVENT_WORK,b->active_frame,0,&poll};
    return render_backend_event(b,&event);
}
int d3d11_present_stats(const render_backend *b, d3d11_present_info *out)
{
    if (!d3d11_descriptor(b) || !b->initialized || !out) return RENDER_ERR_ARG;
    *out = ((d3d11_state *)b->state)->present_info; return RENDER_OK;
}
int d3d11_notify_latency_ready(render_backend *b)
{
    if (!d3d11_descriptor(b) || !b->initialized) return RENDER_ERR_STATE;
    d3d11_state *s = b->state;
    if (s->failed) return RENDER_ERR_DEVICE;
    s->latency_ready = true; return RENDER_OK;
}
void *d3d11_latency_handle(const render_backend *b)
{ return d3d11_descriptor(b) && b->initialized ? ((d3d11_state *)b->state)->latency : NULL; }
void *d3d11_device(const render_backend *b)
{ return d3d11_descriptor(b) && b->initialized ? ((d3d11_state *)b->state)->device : NULL; }
bool d3d11_is_warp(const render_backend *b)
{ return d3d11_descriptor(b) && b->initialized && ((d3d11_state *)b->state)->warp; }
bool d3d11_debug_enabled(const render_backend *b)
{ return d3d11_descriptor(b) && b->initialized && ((d3d11_state *)b->state)->debug; }
int d3d11_read_pixels(render_backend *b, uint32_t *out, size_t capacity, size_t stride)
{
    if (!d3d11_descriptor(b) || !b->initialized || !out) return RENDER_ERR_ARG;
    d3d11_state *s = b->state;
    if (!s->staging || !s->swapped) return RENDER_ERR_STATE;
    if (stride < s->width || stride > SIZE_MAX/s->height || capacity < stride*s->height) return RENDER_ERR_CAPACITY;
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(s->context->lpVtbl->Map(s->context,(ID3D11Resource *)s->staging,0,D3D11_MAP_READ,0,&map))) return RENDER_ERR_DEVICE;
    for (uint32_t y = 0; y < s->height; y++)
        memcpy(out+(size_t)y*stride,(const uint8_t *)map.pData+(size_t)y*map.RowPitch,(size_t)s->width*sizeof(uint32_t));
    s->context->lpVtbl->Unmap(s->context,(ID3D11Resource *)s->staging,0);
    return RENDER_OK;
}
#endif /* _WIN32 */
