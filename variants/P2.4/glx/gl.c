#include "gl/gl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "x11/plat.h"
#include "work/work.h"
#include <GL/glcorearb.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <dlfcn.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/present.h>

#define GL_FUNCTIONS(M) \
 M(GetIntegerv,PFNGLGETINTEGERVPROC) M(GetStringi,PFNGLGETSTRINGIPROC) \
 M(GetError,PFNGLGETERRORPROC) M(CreateShader,PFNGLCREATESHADERPROC) \
 M(ShaderSource,PFNGLSHADERSOURCEPROC) M(CompileShader,PFNGLCOMPILESHADERPROC) \
 M(GetShaderiv,PFNGLGETSHADERIVPROC) M(DeleteShader,PFNGLDELETESHADERPROC) \
 M(CreateProgram,PFNGLCREATEPROGRAMPROC) M(AttachShader,PFNGLATTACHSHADERPROC) \
 M(LinkProgram,PFNGLLINKPROGRAMPROC) M(GetProgramiv,PFNGLGETPROGRAMIVPROC) \
 M(DeleteProgram,PFNGLDELETEPROGRAMPROC) M(UseProgram,PFNGLUSEPROGRAMPROC) \
 M(GetUniformLocation,PFNGLGETUNIFORMLOCATIONPROC) M(Uniform1i,PFNGLUNIFORM1IPROC) \
 M(Uniform1ui,PFNGLUNIFORM1UIPROC) M(Uniform2f,PFNGLUNIFORM2FPROC) \
 M(GenBuffers,PFNGLGENBUFFERSPROC) M(BindBuffer,PFNGLBINDBUFFERPROC) \
 M(BufferData,PFNGLBUFFERDATAPROC) M(BufferSubData,PFNGLBUFFERSUBDATAPROC) \
 M(MapBufferRange,PFNGLMAPBUFFERRANGEPROC) M(UnmapBuffer,PFNGLUNMAPBUFFERPROC) \
 M(DeleteBuffers,PFNGLDELETEBUFFERSPROC) M(GenVertexArrays,PFNGLGENVERTEXARRAYSPROC) \
 M(BindVertexArray,PFNGLBINDVERTEXARRAYPROC) M(DeleteVertexArrays,PFNGLDELETEVERTEXARRAYSPROC) \
 M(EnableVertexAttribArray,PFNGLENABLEVERTEXATTRIBARRAYPROC) \
 M(VertexAttribIPointer,PFNGLVERTEXATTRIBIPOINTERPROC) M(VertexAttribDivisor,PFNGLVERTEXATTRIBDIVISORPROC) \
 M(GenTextures,PFNGLGENTEXTURESPROC) M(BindTexture,PFNGLBINDTEXTUREPROC) \
 M(TexSubImage2D,PFNGLTEXSUBIMAGE2DPROC) M(TexImage2D,PFNGLTEXIMAGE2DPROC) \
 M(TexParameteri,PFNGLTEXPARAMETERIPROC) M(DeleteTextures,PFNGLDELETETEXTURESPROC) \
 M(GenFramebuffers,PFNGLGENFRAMEBUFFERSPROC) M(BindFramebuffer,PFNGLBINDFRAMEBUFFERPROC) \
 M(FramebufferTexture2D,PFNGLFRAMEBUFFERTEXTURE2DPROC) M(CheckFramebufferStatus,PFNGLCHECKFRAMEBUFFERSTATUSPROC) \
 M(DeleteFramebuffers,PFNGLDELETEFRAMEBUFFERSPROC) M(BlitFramebuffer,PFNGLBLITFRAMEBUFFERPROC) \
 M(Disable,PFNGLDISABLEPROC) M(Enable,PFNGLENABLEPROC) M(Viewport,PFNGLVIEWPORTPROC) \
 M(Scissor,PFNGLSCISSORPROC) M(ClearColor,PFNGLCLEARCOLORPROC) M(Clear,PFNGLCLEARPROC) \
 M(DrawArraysInstanced,PFNGLDRAWARRAYSINSTANCEDPROC) M(FenceSync,PFNGLFENCESYNCPROC) \
 M(ClientWaitSync,PFNGLCLIENTWAITSYNCPROC) M(DeleteSync,PFNGLDELETESYNCPROC) \
 M(Flush,PFNGLFLUSHPROC) M(Finish,PFNGLFINISHPROC) M(ReadPixels,PFNGLREADPIXELSPROC)

typedef struct gl_instance {
    uint32_t fg, bg, offset, stride, width, height, dx, underline;
} gl_instance;
typedef struct gl_page { uint32_t offset, width, height; bool valid; } gl_page;
typedef struct gl_state {
    void *lib_gl, *lib_x;
    Display *display;
    GLXContext context;
    GLXWindow window;
    __typeof__(&XOpenDisplay) open_display;
    __typeof__(&XCloseDisplay) close_display;
    __typeof__(&XFree) x_free;
    __typeof__(&XSync) x_sync;
    __typeof__(&XSetErrorHandler) x_error_handler;
    __typeof__(&glXGetProcAddressARB) get_proc;
    __typeof__(&glXGetFBConfigs) get_configs;
    __typeof__(&glXGetFBConfigAttrib) config_attrib;
    __typeof__(&glXCreateWindow) create_window;
    __typeof__(&glXDestroyWindow) destroy_window;
    __typeof__(&glXMakeContextCurrent) make_current;
    __typeof__(&glXGetCurrentContext) current_context;
    __typeof__(&glXDestroyContext) destroy_context;
    __typeof__(&glXSwapBuffers) swap;
    __typeof__(&glXQueryExtensionsString) extensions;
    PFNGLXCREATECONTEXTATTRIBSARBPROC create_context;
    PFNGLXGETSYNCVALUESOMLPROC sync_values;
    PFNGLXSWAPINTERVALEXTPROC swap_ext;
    PFNGLXSWAPINTERVALMESAPROC swap_mesa;
#define GL_DECLARE(n,t) t n;
    GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE
    PFNGLBUFFERSTORAGEPROC BufferStorage;
    edit_arena storage;
    gl_instance *instances, *mapped;
    render_strip *strips;
    gl_page *pages;
    uint8_t *atlas;
    size_t count, atlas_bytes, atlas_capacity;
    uint32_t atlas_width, atlas_shift;
    GLuint program, vao, vbo, atlas_texture, surface_texture, fbo;
    GLint u_cell, u_extent, u_first;
    GLsync fence;
    uint32_t next_serial, serial;
    uint64_t msc, completion_ns;
    bool atlas_upload, bound, persistent_requested, persistent_active, verified, completion, device_lost;
} gl_state;

static bool gl_has_extension(const char *list, const char *name)
{
    if (list == NULL) return false;
    size_t len = strlen(name);
    const char *p = list;
    while ((p = strstr(p, name)) != NULL) {
        if ((p == list || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) return true;
        p += len;
    }
    return false;
}
static int gl_ignore_x_error(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }
static int gl_bind(gl_state *s)
{
    if (s->bound && s->current_context && s->current_context()==s->context) return RENDER_OK;
    if (!s->make_current(s->display,s->window,s->window,s->context)) return RENDER_ERR_DEVICE;
    s->bound = true; return RENDER_OK;
}
static GLuint gl_shader(gl_state *s, GLenum type, const char *source)
{
    GLuint shader = s->CreateShader(type);
    if (!shader) return 0;
    s->ShaderSource(shader,1,&source,NULL); s->CompileShader(shader);
    GLint ok = 0; s->GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) { s->DeleteShader(shader); return 0; }
    return shader;
}
static int gl_program(gl_state *s)
{
    const char *vertex =
        "#version 330 core\n"
        "layout(location=0) in uvec4 a; layout(location=1) in uvec4 b;\n"
        "uniform vec2 cell,extent; uniform uint first;\n"
        "out vec2 local; flat out uvec4 ca; flat out uvec4 cb;\n"
        "void main(){ uint i=first+uint(gl_InstanceID); uint cols=uint(extent.x/cell.x);\n"
        "vec2 p=vec2(float(i%cols),float(i/cols))*cell;\n"
        "vec2 q=vec2(float(gl_VertexID&1),float((gl_VertexID>>1)&1));\n"
        "local=q*cell; vec2 v=(p+local)/extent;\n"
        "gl_Position=vec4(v.x*2.0-1.0,1.0-v.y*2.0,0,1); ca=a;cb=b;}\n";
    const char *fragment =
        "#version 330 core\n"
        "uniform sampler2D atlas; uniform uint atlas_mask,atlas_shift; uniform vec2 cell;\n"
        "in vec2 local; flat in uvec4 ca; flat in uvec4 cb; out vec4 colour;\n"
        "uvec3 rgb(uint c){return uvec3((c>>16)&255u,(c>>8)&255u,c&255u);}\n"
        "void main(){uvec2 p=uvec2(floor(local));uint x=p.x+cb.z;uint a=0u;\n"
        "if(x<cb.x && p.y<cb.y){uint t=ca.z+p.y*ca.w+x; a=uint(round(texelFetch(atlas,ivec2(int(t&atlas_mask),int(t>>atlas_shift)),0).r*255.0));}\n"
        "if(cb.w!=0u && p.y==uint(cell.y)-1u) a=255u;\n"
        "uvec3 c=(rgb(ca.x)*a+rgb(ca.y)*(255u-a)+127u)/255u;colour=vec4(vec3(c)/255.0,1);}\n";
    GLuint v = gl_shader(s,GL_VERTEX_SHADER,vertex), f = gl_shader(s,GL_FRAGMENT_SHADER,fragment);
    if (!v || !f) { if(v) s->DeleteShader(v); if(f) s->DeleteShader(f); return RENDER_ERR_INIT; }
    s->program=s->CreateProgram();
    s->AttachShader(s->program,v); s->AttachShader(s->program,f); s->LinkProgram(s->program);
    s->DeleteShader(v); s->DeleteShader(f);
    GLint ok=0; s->GetProgramiv(s->program,GL_LINK_STATUS,&ok);
    if (!ok) return RENDER_ERR_INIT;
    s->UseProgram(s->program); s->Uniform1i(s->GetUniformLocation(s->program,"atlas"),0);
    s->Uniform1ui(s->GetUniformLocation(s->program,"atlas_mask"),s->atlas_width-1u);
    s->Uniform1ui(s->GetUniformLocation(s->program,"atlas_shift"),s->atlas_shift);
    s->u_cell=s->GetUniformLocation(s->program,"cell");
    s->u_extent=s->GetUniformLocation(s->program,"extent");
    s->u_first=s->GetUniformLocation(s->program,"first");
    return RENDER_OK;
}
static void gl_shutdown(render_backend *b)
{
    gl_state *s=b->state;
    if (s->context && s->window && s->make_current && gl_bind(s)==RENDER_OK) {
        if (s->Finish) s->Finish();
        if (s->fence && s->DeleteSync) s->DeleteSync(s->fence);
        if (s->mapped && s->UnmapBuffer) { s->BindBuffer(GL_ARRAY_BUFFER,s->vbo); (void)s->UnmapBuffer(GL_ARRAY_BUFFER); }
        if (s->DeleteBuffers) { s->DeleteBuffers(1,&s->vbo); }
        if (s->DeleteTextures) { s->DeleteTextures(1,&s->atlas_texture); s->DeleteTextures(1,&s->surface_texture); }
        if (s->DeleteFramebuffers) s->DeleteFramebuffers(1,&s->fbo);
        if (s->DeleteVertexArrays) s->DeleteVertexArrays(1,&s->vao);
        if (s->program && s->DeleteProgram) s->DeleteProgram(s->program);
        (void)s->make_current(s->display,None,None,NULL); s->bound=false;
    }
    if (s->context && s->destroy_context) s->destroy_context(s->display,s->context);
    if (s->window && s->destroy_window) s->destroy_window(s->display,s->window);
    if (s->display && s->close_display) s->close_display(s->display);
    if (s->lib_gl) dlclose(s->lib_gl);
    if (s->lib_x) dlclose(s->lib_x);
    edit_arena_free(&s->storage); memset(s,0,sizeof *s);
}
/* Startup probe uses a separate Present event selection/special queue; the
 * platform's ordinary event queue and callbacks remain owned by the UI. */
#ifndef GL_READBACK_TEST
static int gl_verify_present(gl_state *s, plat *p)
{
    xcb_connection_t *c=p->conn;
    uint32_t eid=xcb_generate_id(c), stamp=0;
    xcb_special_event_t *queue=xcb_register_for_special_xge(c,&xcb_present_id,eid,&stamp);
    if (!queue) return RENDER_ERR_INIT;
    xcb_generic_error_t *error=xcb_request_check(c,xcb_present_select_input_checked(c,eid,p->win,XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY));
    if (error) { free(error); xcb_unregister_for_special_event(c,queue); return RENDER_ERR_UNSUPPORTED; }
    int64_t ust=0,msc=0,sbc=0;
    bool sync=s->sync_values(s->display,s->window,&ust,&msc,&sbc)!=0;
    int rc=RENDER_ERR_UNSUPPORTED;
    if (sync && sbc>=0 && sbc<UINT32_MAX) {
        s->serial=(uint32_t)sbc+1u;
        s->swap(s->display,s->window);
        s->Flush();
        uint64_t deadline=trace_now_ns()+UINT64_C(2000000000);
        struct pollfd fd={.fd=xcb_get_file_descriptor(c),.events=POLLIN};
        while (trace_now_ns()<deadline) {
            xcb_generic_event_t *e=xcb_poll_for_special_event(c,queue);
            if (!e) { (void)poll(&fd,1,10); continue; }
            xcb_present_complete_notify_event_t *complete=(void *)e;
            if (complete->event_type==XCB_PRESENT_COMPLETE_NOTIFY &&
                complete->kind==XCB_PRESENT_COMPLETE_KIND_PIXMAP && complete->serial==s->serial &&
                complete->window==p->win && complete->msc>0) {
                s->msc=complete->msc; s->verified=true; rc=RENDER_OK;
            }
            free(e); if (rc==RENDER_OK) break;
        }
    }
    xcb_present_select_input(c,eid,p->win,0); xcb_flush(c);
    xcb_unregister_for_special_event(c,queue);
    s->next_serial=s->serial+1u; return rc;
}
#endif
static int gl_init(render_backend *b, const render_config *c)
{
    gl_state *s=b->state; memset(s,0,sizeof *s);
    if (!c->platform || !c->platform->conn || !c->platform->present_ok ||
        !c->platform->height || c->platform->height>(uint32_t)INT_MAX) return RENDER_ERR_UNSUPPORTED;
    if (c->max_cells>(size_t)INT_MAX/sizeof(gl_instance) || c->max_atlas_bytes>INT_MAX ||
        c->max_pages>SIZE_MAX/sizeof(gl_page)) return RENDER_ERR_UNSUPPORTED;
    int result=RENDER_ERR_INIT;
    s->lib_x=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);
    s->lib_gl=dlopen("libGL.so.1",RTLD_NOW|RTLD_LOCAL);
    if (!s->lib_x || !s->lib_gl) goto fail;
#define X_LOAD(field,name) do { *(void **)(&s->field)=dlsym(s->lib_x,name); if(!s->field) goto fail; } while(0)
    X_LOAD(open_display,"XOpenDisplay"); X_LOAD(close_display,"XCloseDisplay");
    X_LOAD(x_free,"XFree"); X_LOAD(x_sync,"XSync"); X_LOAD(x_error_handler,"XSetErrorHandler");
    int (*threads_init)(void)=NULL; *(void **)(&threads_init)=dlsym(s->lib_x,"XInitThreads");
    if (!threads_init || !threads_init()) goto fail;
    s->display=s->open_display(NULL); if (!s->display) goto fail;
#define X_GL_LOAD(field,name) do { *(void **)(&s->field)=dlsym(s->lib_gl,name); if(!s->field) goto fail; } while(0)
    X_GL_LOAD(get_proc,"glXGetProcAddressARB"); X_GL_LOAD(get_configs,"glXGetFBConfigs");
    X_GL_LOAD(config_attrib,"glXGetFBConfigAttrib"); X_GL_LOAD(create_window,"glXCreateWindow");
    X_GL_LOAD(destroy_window,"glXDestroyWindow"); X_GL_LOAD(make_current,"glXMakeContextCurrent");
    X_GL_LOAD(current_context,"glXGetCurrentContext");
    X_GL_LOAD(destroy_context,"glXDestroyContext"); X_GL_LOAD(swap,"glXSwapBuffers");
    X_GL_LOAD(extensions,"glXQueryExtensionsString");
    const char *ext=s->extensions(s->display,DefaultScreen(s->display));
    result=RENDER_ERR_UNSUPPORTED;
    if (!gl_has_extension(ext,"GLX_ARB_create_context") ||
        !gl_has_extension(ext,"GLX_ARB_create_context_profile")) goto fail;
#ifndef GL_READBACK_TEST
    if (!gl_has_extension(ext,"GLX_OML_sync_control")) goto fail;
#endif
    s->create_context=(PFNGLXCREATECONTEXTATTRIBSARBPROC)s->get_proc((const GLubyte *)"glXCreateContextAttribsARB");
    s->sync_values=(PFNGLXGETSYNCVALUESOMLPROC)s->get_proc((const GLubyte *)"glXGetSyncValuesOML");
    if (!s->create_context || !s->sync_values) goto fail;
    int nconfigs=0; GLXFBConfig *configs=s->get_configs(s->display,DefaultScreen(s->display),&nconfigs);
    GLXFBConfig config=NULL;
    for (int i=0;i<nconfigs;i++) {
        int visual=0,db=0,type=0,drawable=0;
        (void)s->config_attrib(s->display,configs[i],GLX_VISUAL_ID,&visual);
        (void)s->config_attrib(s->display,configs[i],GLX_DOUBLEBUFFER,&db);
        (void)s->config_attrib(s->display,configs[i],GLX_RENDER_TYPE,&type);
        (void)s->config_attrib(s->display,configs[i],GLX_DRAWABLE_TYPE,&drawable);
        if ((uint32_t)visual==c->platform->visual && db && (type&GLX_RGBA_BIT) && (drawable&GLX_WINDOW_BIT)) { config=configs[i]; break; }
    }
    if (configs) s->x_free(configs);
    if (!config) goto fail;
    const int attributes[]={GLX_CONTEXT_MAJOR_VERSION_ARB,3,GLX_CONTEXT_MINOR_VERSION_ARB,3,
        GLX_CONTEXT_PROFILE_MASK_ARB,GLX_CONTEXT_CORE_PROFILE_BIT_ARB,None};
    /* GLX errors are asynchronous. Restore the process handler before handoff. */
    int (*previous)(Display *,XErrorEvent *)=s->x_error_handler(gl_ignore_x_error);
    s->context=s->create_context(s->display,config,NULL,True,attributes);
    s->window=s->create_window(s->display,config,c->platform->win,NULL);
    s->x_sync(s->display,False); (void)s->x_error_handler(previous);
    if (!s->context || !s->window || gl_bind(s)!=RENDER_OK) goto fail;
#define GL_RESOLVE(n,t) s->n=(t)s->get_proc((const GLubyte *)"gl" #n); if (!s->n) goto fail;
    GL_FUNCTIONS(GL_RESOLVE)
#undef GL_RESOLVE
    GLint major=0,minor=0,texture_limit=0;
    s->GetIntegerv(GL_MAJOR_VERSION,&major); s->GetIntegerv(GL_MINOR_VERSION,&minor);
    s->GetIntegerv(GL_MAX_TEXTURE_SIZE,&texture_limit);
    if (major<3 || (major==3 && minor<3) || c->max_width>(uint32_t)texture_limit || c->max_height>(uint32_t)texture_limit) goto fail;
    s->atlas_width=1u; s->atlas_shift=0u;
    while (s->atlas_width<2048u && s->atlas_width*2u<=(uint32_t)texture_limit) { s->atlas_width*=2u; s->atlas_shift++; }
    size_t atlas_height=(c->max_atlas_bytes+s->atlas_width-1u)/s->atlas_width;
    while (atlas_height>(size_t)texture_limit && s->atlas_width*2u<=(uint32_t)texture_limit) {
        s->atlas_width*=2u; s->atlas_shift++;
        atlas_height=(c->max_atlas_bytes+s->atlas_width-1u)/s->atlas_width;
    }
    if (!atlas_height) atlas_height=1;
    if (atlas_height>(size_t)texture_limit) goto fail;
    s->atlas_capacity=atlas_height*s->atlas_width;
    const char *mode=getenv("EDIT_GL_VBO");
    if (mode && strcmp(mode,"persistent") && strcmp(mode,"orphan")) { result=RENDER_ERR_ARG; goto fail; }
    s->persistent_requested=mode && !strcmp(mode,"persistent");
    GLint extensions=0; s->GetIntegerv(GL_NUM_EXTENSIONS,&extensions);
    bool buffer_storage=false;
    for (GLint i=0;i<extensions;i++) {
        const char *name=(const char *)s->GetStringi(GL_EXTENSIONS,(GLuint)i);
        if (name && !strcmp(name,"GL_ARB_buffer_storage")) buffer_storage=true;
    }
    if (s->persistent_requested && buffer_storage) {
        s->BufferStorage=(PFNGLBUFFERSTORAGEPROC)s->get_proc((const GLubyte *)"glBufferStorage");
        if (!s->BufferStorage) goto fail;
        s->persistent_active=true;
    }
    size_t total=c->max_cells*(sizeof(gl_instance)+sizeof(render_strip));
    if (c->max_pages>(SIZE_MAX-total-128u)/sizeof(gl_page)) goto fail;
    total+=c->max_pages*sizeof(gl_page)+128u;
    if (s->atlas_capacity>SIZE_MAX-total) goto fail;
    total+=s->atlas_capacity;
    if (edit_arena_init(&s->storage,total)!=0) { result=RENDER_ERR_INIT; goto fail; }
    s->instances=edit_arena_alloc(&s->storage,c->max_cells*sizeof *s->instances,_Alignof(gl_instance));
    s->strips=edit_arena_alloc(&s->storage,c->max_cells*sizeof *s->strips,_Alignof(render_strip));
    s->pages=edit_arena_alloc(&s->storage,c->max_pages*sizeof *s->pages,_Alignof(gl_page));
    s->atlas=edit_arena_alloc(&s->storage,s->atlas_capacity,1);
    if (!s->instances || !s->strips || (c->max_pages && !s->pages) || (c->max_atlas_bytes && !s->atlas)) goto fail;
    memset(s->instances,0,c->max_cells*sizeof *s->instances);
    if (c->max_pages) memset(s->pages,0,c->max_pages*sizeof *s->pages);
    memset(s->atlas,0,s->atlas_capacity);
    if (gl_program(s)!=RENDER_OK) { result=RENDER_ERR_INIT; goto fail; }
    s->GenVertexArrays(1,&s->vao); s->BindVertexArray(s->vao);
    s->GenBuffers(1,&s->vbo); s->BindBuffer(GL_ARRAY_BUFFER,s->vbo);
    GLsizeiptr instance_bytes=(GLsizeiptr)(c->max_cells*sizeof *s->instances);
    if (s->persistent_active) {
        GLbitfield flags=GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
        s->BufferStorage(GL_ARRAY_BUFFER,instance_bytes,s->instances,flags);
        s->mapped=s->MapBufferRange(GL_ARRAY_BUFFER,0,instance_bytes,flags);
        if (!s->mapped) goto fail;
    } else s->BufferData(GL_ARRAY_BUFFER,instance_bytes,s->instances,GL_STREAM_DRAW);
    for (GLuint i=0;i<2;i++) {
        s->EnableVertexAttribArray(i);
        s->VertexAttribIPointer(i,4,GL_UNSIGNED_INT,(GLsizei)sizeof(gl_instance),(void *)(uintptr_t)(i*16u));
        s->VertexAttribDivisor(i,1);
    }
    s->GenTextures(1,&s->atlas_texture); s->BindTexture(GL_TEXTURE_2D,s->atlas_texture);
    s->TexImage2D(GL_TEXTURE_2D,0,GL_R8,(GLsizei)s->atlas_width,(GLsizei)atlas_height,0,GL_RED,GL_UNSIGNED_BYTE,s->atlas);
    s->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); s->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    s->GenTextures(1,&s->surface_texture); s->BindTexture(GL_TEXTURE_2D,s->surface_texture);
    s->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)c->max_width,(GLsizei)c->max_height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    s->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); s->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    s->GenFramebuffers(1,&s->fbo); s->BindFramebuffer(GL_FRAMEBUFFER,s->fbo);
    s->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s->surface_texture,0);
    if (s->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto fail;
    s->Disable(GL_DITHER); s->Disable(GL_BLEND); s->Disable(GL_FRAMEBUFFER_SRGB);
    s->ClearColor(0,0,0,1); s->Clear(GL_COLOR_BUFFER_BIT);
    s->BindFramebuffer(GL_FRAMEBUFFER,0); s->Clear(GL_COLOR_BUFFER_BIT);
#ifndef GL_READBACK_TEST
    if (gl_has_extension(ext,"GLX_EXT_swap_control")) {
        s->swap_ext=(PFNGLXSWAPINTERVALEXTPROC)s->get_proc((const GLubyte *)"glXSwapIntervalEXT");
        if (!s->swap_ext) goto fail;
        s->swap_ext(s->display,s->window,1);
    } else if (gl_has_extension(ext,"GLX_MESA_swap_control")) {
        s->swap_mesa=(PFNGLXSWAPINTERVALMESAPROC)s->get_proc((const GLubyte *)"glXSwapIntervalMESA");
        if (!s->swap_mesa || s->swap_mesa(1)!=0) goto fail;
    } else goto fail;
    if (s->GetError()!=GL_NO_ERROR) goto fail;
    result=gl_verify_present(s,c->platform);
    if (result!=RENDER_OK) goto fail;
#else
    /* Compiled only by gl_test.c's readback copy. Never a production option;
     * the driver synthesises acknowledgement and labels displayed tests SKIP. */
    s->next_serial=1;
#endif
    s->Finish();
    if (!s->make_current(s->display,None,None,NULL)) { result=RENDER_ERR_INIT; goto fail; }
    s->bound=false;
    b->info.name=s->persistent_active?"glx-persistent":s->persistent_requested?"glx-orphan(fallback)":"glx-orphan";
    return RENDER_OK;
fail:
    gl_shutdown(b); return result;
}
static int gl_resize(render_backend *b, render_dims dims)
{
    (void)dims; return gl_bind(b->state);
}
static int gl_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    gl_state *s=b->state;
    /* CPU snapshot only: first UI context bind and every GL call belong to
     * present, keeping input->submit free of library allocation/round trips. */
    size_t offset=0; bool upload=false;
    for (size_t i=0;i<g->page_count;i++) {
        const render_atlas_page *p=&g->pages[i]; gl_page *cache=&s->pages[i];
        size_t len=(size_t)p->width*p->height;
        bool changed=!cache->valid || cache->offset!=offset || cache->width!=p->width || cache->height!=p->height;
        for (uint32_t row=0;row<p->height;row++) {
            uint8_t *dst=s->atlas+offset+(size_t)row*p->width;
            const uint8_t *src=p->pixels+(size_t)row*p->stride;
            if (changed || memcmp(dst,src,p->width)) { memcpy(dst,src,p->width); changed=true; }
        }
        if (changed) upload=true;
        *cache=(gl_page){(uint32_t)offset,p->width,p->height,true}; offset+=len;
    }
    s->atlas_upload=upload;
    s->atlas_bytes=offset; s->count=count; s->completion=false; s->completion_ns=0;
    if (count) memcpy(s->strips,strips,count*sizeof *strips);
    for (size_t strip=0;strip<count;strip++) {
        size_t first=(size_t)strips[strip].first_row*g->dims.cols;
        size_t end=first+(size_t)strips[strip].row_count*g->dims.cols;
        for (size_t i=first;i<end;i++) {
            const render_cell *c=&g->cells[i];
            const render_cell *image=c;
            uint32_t dx=0;
            if (c->attrs&RENDER_ATTR_WIDE_RIGHT) { image=c-1; dx=g->dims.cell_w; }
            uint32_t fg=c->fg,bg=c->bg;
            if (c->attrs&RENDER_ATTR_INVERSE) { uint32_t tmp=fg; fg=bg; bg=tmp; }
            gl_instance instance={.fg=fg,.bg=bg,.dx=dx,.underline=(c->attrs&RENDER_ATTR_UNDERLINE)!=0};
            if (image->atlas_slot!=RENDER_NO_SLOT) {
                const render_glyph *glyph=&g->glyphs[image->atlas_slot];
                gl_page p=s->pages[glyph->page];
                instance.offset=p.offset+glyph->y*p.width+glyph->x;
                instance.stride=p.width; instance.width=glyph->w; instance.height=glyph->h;
            }
            s->instances[i]=instance;
        }
    }
    return RENDER_OK;
}
static int gl_present(render_backend *b, uint32_t id)
{
    (void)id; gl_state *s=b->state;
    if (s->next_serial==0 || s->device_lost) return RENDER_ERR_DEVICE;
    int rc=gl_bind(s); if (rc!=RENDER_OK) return rc;
    s->BindTexture(GL_TEXTURE_2D,s->atlas_texture);
    if (s->atlas_upload) {
        s->TexSubImage2D(GL_TEXTURE_2D,0,0,0,(GLsizei)s->atlas_width,
            (GLsizei)((s->atlas_bytes+s->atlas_width-1u)/s->atlas_width),GL_RED,GL_UNSIGNED_BYTE,s->atlas);
        s->atlas_upload=false;
    }
    render_dims d=b->config.dims;
    GLsizei width=(GLsizei)(d.cols*d.cell_w),height=(GLsizei)(d.rows*d.cell_h);
    s->BindVertexArray(s->vao); s->UseProgram(s->program);
    s->BindBuffer(GL_ARRAY_BUFFER,s->vbo);
    GLsizeiptr bytes=(GLsizeiptr)(b->config.max_cells*sizeof(gl_instance));
    if (!s->persistent_active && s->count) s->BufferData(GL_ARRAY_BUFFER,bytes,NULL,GL_STREAM_DRAW);
    for (size_t i=0;i<s->count;i++) {
        size_t first=(size_t)s->strips[i].first_row*d.cols;
        size_t n=(size_t)s->strips[i].row_count*d.cols;
        if (s->mapped) memcpy(s->mapped+first,s->instances+first,n*sizeof(gl_instance));
        else s->BufferSubData(GL_ARRAY_BUFFER,(GLintptr)(first*sizeof(gl_instance)),(GLsizeiptr)(n*sizeof(gl_instance)),s->instances+first);
    }
    s->BindFramebuffer(GL_FRAMEBUFFER,s->fbo); s->Viewport(0,0,width,height);
    s->Uniform2f(s->u_cell,(float)d.cell_w,(float)d.cell_h);
    s->Uniform2f(s->u_extent,(float)width,(float)height);
    s->Enable(GL_SCISSOR_TEST);
    for (size_t i=0;i<s->count;i++) {
        render_strip strip=s->strips[i];
        size_t first=(size_t)strip.first_row*d.cols;
        for (GLuint a=0;a<2;a++) s->VertexAttribIPointer(a,4,GL_UNSIGNED_INT,(GLsizei)sizeof(gl_instance),
            (void *)(uintptr_t)(first*sizeof(gl_instance)+a*16u));
        s->Uniform1ui(s->u_first,(GLuint)first);
        s->Scissor(0,height-(GLint)((strip.first_row+strip.row_count)*d.cell_h),width,(GLsizei)(strip.row_count*d.cell_h));
        s->DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,(GLsizei)(strip.row_count*d.cols));
    }
    s->Disable(GL_SCISSOR_TEST);
    s->BindFramebuffer(GL_READ_FRAMEBUFFER,s->fbo); s->BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    GLint top=(GLint)b->config.platform->height;
    s->BlitFramebuffer(0,0,width,height,0,top-height,width,top,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    s->BindFramebuffer(GL_FRAMEBUFFER,s->fbo);
    if (s->GetError()!=GL_NO_ERROR) return RENDER_ERR_DEVICE;
    s->serial=s->next_serial++;
    s->swap(s->display,s->window);
    s->fence=s->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); s->Flush();
    if (!s->fence) { s->device_lost=true; return RENDER_ERR_DEVICE; }
    return RENDER_OK;
}
int gl_backend_poll(render_backend *b)
{
    if (!b || !b->initialized) return RENDER_ERR_STATE;
    gl_state *s=b->state;
    if (!b->active || !b->presented) return RENDER_OK;
    int rc_bind=gl_bind(s); if (rc_bind!=RENDER_OK) return rc_bind;
    if (s->fence) {
        GLenum result=s->ClientWaitSync(s->fence,0,0);
        if (result==GL_WAIT_FAILED) return RENDER_ERR_DEVICE;
        if (result==GL_ALREADY_SIGNALED || result==GL_CONDITION_SATISFIED) {
            s->DeleteSync(s->fence); s->fence=NULL;
            int rc=render_backend_signal(b,RENDER_EVENT_DEVICE_DONE,b->active_frame,trace_now_ns());
            if (rc!=RENDER_OK) return rc;
        }
    }
    if (s->completion && b->device_seen && !b->complete_seen) {
        uint64_t ns=s->completion_ns>b->device_ns?s->completion_ns:b->device_ns;
        return render_backend_signal(b,RENDER_EVENT_PRESENT_COMPLETE,b->active_frame,ns);
    }
    return RENDER_OK;
}
int gl_backend_present_complete(render_backend *b, uint32_t serial, uint64_t ust, uint64_t msc)
{
    (void)ust; /* UST is not assumed to be CLOCK_MONOTONIC. Timestamp observation. */
    if (!b || !b->initialized) return RENDER_ERR_STATE;
    gl_state *s=b->state;
    if (!b->active || !b->presented || serial!=s->serial || s->completion || msc==0) return RENDER_ERR_FRAME;
    s->completion=true; s->completion_ns=trace_now_ns(); s->msc=msc;
    return gl_backend_poll(b);
}
int gl_backend_query(const render_backend *b, gl_backend_status *out)
{
    if (!b || !out) return RENDER_ERR_ARG;
    if (!b->initialized) return RENDER_ERR_STATE;
    const gl_state *s=b->state;
    *out=(gl_backend_status){s->persistent_requested,s->persistent_active,s->verified,s->msc,s->serial};
    return RENDER_OK;
}
int gl_backend_read_pixels(render_backend *b, uint8_t *rgba, size_t bytes)
{
    if (!b || !b->initialized || !rgba) return RENDER_ERR_ARG;
    if (b->active) return RENDER_ERR_BUSY;
    render_dims d=b->config.dims; uint32_t w=d.cols*d.cell_w,h=d.rows*d.cell_h;
    if (bytes<(size_t)w*h*4u) return RENDER_ERR_CAPACITY;
    gl_state *s=b->state; int rc=gl_bind(s); if (rc!=RENDER_OK) return rc;
    s->BindFramebuffer(GL_READ_FRAMEBUFFER,s->fbo);
    s->ReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    return s->GetError()==GL_NO_ERROR?RENDER_OK:RENDER_ERR_DEVICE;
}
static int gl_event(render_backend *b, const render_event *event)
{
    if (event->kind!=RENDER_EVENT_WORK) return RENDER_ERR_UNSUPPORTED;
    if (event->work && event->work->kind==GL_POLL_MSG_KIND) return gl_backend_poll(b);
    if (!event->work) return RENDER_ERR_ARG;
    if (event->work->kind!=GL_PRESENT_MSG_KIND) return RENDER_ERR_UNSUPPORTED;
    gl_present_message native;
    memcpy(&native,event->work->data,sizeof native);
    return gl_backend_present_complete(b,native.serial,native.ust,native.msc);
}
int render_gl_backend(render_backend *b)
{
    if (!b) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    *b=(render_backend){.info={"glx",sizeof(gl_state),_Alignof(gl_state),
        RENDER_CAP_DEVICE_TIMING|RENDER_CAP_PRESENT_TIMING|RENDER_CAP_GPU},
        .ops={gl_init,gl_resize,gl_submit,gl_present,gl_event,gl_shutdown}};
    return RENDER_OK;
}
