/* EGL/Mesa DRI3 renderer: loaded only in init's worker. */
#include "gl/gl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "work/work.h"
#include "x11/plat.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <dlfcn.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>
#include <xcb/present.h>
#include <xcb/xcbext.h>
#include <poll.h>

typedef struct gl_instance { uint32_t pos[4], glyph[4], color[4]; } gl_instance;
typedef struct gl_strip { render_strip rows; size_t first, count; } gl_strip;
enum gl_upload { GL_UPLOAD_LEGACY, GL_UPLOAD_SUBDATA, GL_UPLOAD_ORPHAN, GL_UPLOAD_PERSISTENT };
#define GL_CELL_RING 3u
typedef struct gl_cell_slot { render_cell *cells; GLsync fence; } gl_cell_slot;
typedef struct gl_state {
    void *egl_lib, *gl_lib;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    bool egl_live, bound, own_arena, persistent, requested_persistent;
    edit_arena arena;
    enum gl_upload upload;
    render_cell *cells;
    uint32_t (*images)[4];
    size_t cell_bytes, image_bytes, image_count;
    GLuint image_vbo, image_texture;
    GLint first_uniform, cols_uniform, cell_uniform;
    gl_cell_slot ring[GL_CELL_RING];
    uint32_t ring_next, ring_current, ring_last;
    bool ring_has_last, leased, ring_poisoned;
    gl_instance *instances;
    gl_strip *strips;
    size_t strip_count, instance_count, vbo_bytes;
    uint8_t *atlas_cache, *atlas_dirty, *glyph_seen;
    size_t *page_offsets;
    uint32_t atlas_width, atlas_height;
    GLuint vao, vbo, program, atlas_texture, target_texture, fbo;
    GLint surface_uniform, atlas_uniform, shift_uniform;
    GLsync fence;
    void *mapped;
    uint32_t swap_serial, pending_serial;
    uint64_t msc;
    bool present_notice, present_verified, complete_selected;
    uint32_t present_eid, present_stamp;
    uint64_t notice_msc;
    xcb_special_event_t *present_special;
    render_dims dims;
    char device_name[256];
    uint32_t max_width, max_height;
    struct plat *platform;
    PFNEGLRELEASETHREADPROC eReleaseThread;
    PFNEGLGETPROCADDRESSPROC eGetProcAddress;
    PFNEGLQUERYSTRINGPROC eQueryString;
    PFNEGLINITIALIZEPROC eInitialize;
    PFNEGLTERMINATEPROC eTerminate;
    PFNEGLBINDAPIPROC eBindAPI;
    PFNEGLCHOOSECONFIGPROC eChooseConfig;
    PFNEGLGETCONFIGATTRIBPROC eGetConfigAttrib;
    PFNEGLCREATECONTEXTPROC eCreateContext;
    PFNEGLDESTROYCONTEXTPROC eDestroyContext;
    PFNEGLCREATEPLATFORMWINDOWSURFACEPROC eCreatePlatformWindowSurface;
    PFNEGLDESTROYSURFACEPROC eDestroySurface;
    PFNEGLMAKECURRENTPROC eMakeCurrent;
    PFNEGLSWAPINTERVALPROC eSwapInterval;
    PFNEGLSWAPBUFFERSPROC eSwapBuffers;
    PFNEGLGETPLATFORMDISPLAYPROC eGetPlatformDisplay;
    PFNGLGETSTRINGPROC gGetString;
    PFNGLGETSTRINGIPROC gGetStringi;
    PFNGLGETINTEGERVPROC gGetIntegerv;
    PFNGLGETERRORPROC gGetError;
    PFNGLGENVERTEXARRAYSPROC gGenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC gBindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC gDeleteVertexArrays;
    PFNGLGENBUFFERSPROC gGenBuffers;
    PFNGLBINDBUFFERPROC gBindBuffer;
    PFNGLBUFFERDATAPROC gBufferData;
    PFNGLBUFFERSUBDATAPROC gBufferSubData;
    PFNGLDELETEBUFFERSPROC gDeleteBuffers;
    PFNGLMAPBUFFERRANGEPROC gMapBufferRange;
    PFNGLUNMAPBUFFERPROC gUnmapBuffer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC gEnableVertexAttribArray;
    PFNGLVERTEXATTRIBIPOINTERPROC gVertexAttribIPointer;
    PFNGLVERTEXATTRIBDIVISORPROC gVertexAttribDivisor;
    PFNGLCREATESHADERPROC gCreateShader;
    PFNGLSHADERSOURCEPROC gShaderSource;
    PFNGLCOMPILESHADERPROC gCompileShader;
    PFNGLGETSHADERIVPROC gGetShaderiv;
    PFNGLDELETESHADERPROC gDeleteShader;
    PFNGLCREATEPROGRAMPROC gCreateProgram;
    PFNGLATTACHSHADERPROC gAttachShader;
    PFNGLLINKPROGRAMPROC gLinkProgram;
    PFNGLGETPROGRAMIVPROC gGetProgramiv;
    PFNGLDELETEPROGRAMPROC gDeleteProgram;
    PFNGLUSEPROGRAMPROC gUseProgram;
    PFNGLGETUNIFORMLOCATIONPROC gGetUniformLocation;
    PFNGLUNIFORM2FPROC gUniform2f;
    PFNGLUNIFORM1IPROC gUniform1i;
    PFNGLGENTEXTURESPROC gGenTextures;
    PFNGLBINDTEXTUREPROC gBindTexture;
    PFNGLTEXIMAGE2DPROC gTexImage2D;
    PFNGLTEXSUBIMAGE2DPROC gTexSubImage2D;
    PFNGLTEXPARAMETERIPROC gTexParameteri;
    PFNGLDELETETEXTURESPROC gDeleteTextures;
    PFNGLACTIVETEXTUREPROC gActiveTexture;
    PFNGLPIXELSTOREIPROC gPixelStorei;
    PFNGLGENFRAMEBUFFERSPROC gGenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC gBindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC gFramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC gCheckFramebufferStatus;
    PFNGLDELETEFRAMEBUFFERSPROC gDeleteFramebuffers;
    PFNGLVIEWPORTPROC gViewport;
    PFNGLSCISSORPROC gScissor;
    PFNGLENABLEPROC gEnable;
    PFNGLDISABLEPROC gDisable;
    PFNGLCLEARCOLORPROC gClearColor;
    PFNGLCLEARPROC gClear;
    PFNGLDRAWARRAYSINSTANCEDPROC gDrawArraysInstanced;
    PFNGLBLITFRAMEBUFFERPROC gBlitFramebuffer;
    PFNGLFENCESYNCPROC gFenceSync;
    PFNGLCLIENTWAITSYNCPROC gClientWaitSync;
    PFNGLDELETESYNCPROC gDeleteSync;
    PFNGLFLUSHPROC gFlush;
    PFNGLFINISHPROC gFinish;
    PFNGLREADPIXELSPROC gReadPixels;
    PFNGLBUFFERSTORAGEPROC gBufferStorage;
    PFNGLTEXBUFFERPROC gTexBuffer;
    PFNGLUNIFORM1UIPROC gUniform1ui;
    PFNGLUNIFORM2UIPROC gUniform2ui;
} gl_state;
static int gl_select_upload(gl_state *s);

static bool gl_extension(const char *list, const char *name)
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
static bool gl_symbol(void *lib, const char *name, void *target, size_t bytes)
{
    void *symbol = dlsym(lib, name);
    if (symbol == NULL || bytes != sizeof symbol) return false;
    memcpy(target, &symbol, bytes); return true;
}
static bool gl_proc(gl_state *s, const char *name, void *target, size_t bytes)
{
    __eglMustCastToProperFunctionPointerType proc = s->eGetProcAddress(name);
    if (proc == NULL) return gl_symbol(s->gl_lib, name, target, bytes);
    if (bytes != sizeof proc) return false;
    memcpy(target, &proc, bytes); return true;
}
static bool gl_load(gl_state *s)
{
    const char *egl_name=getenv("EDIT_GL_EGL_LIBRARY"), *gl_name=getenv("EDIT_GL_GL_LIBRARY");
    /* EGLDisplay handles are process-lifetime identities even after terminate.
     * Keep the loader/vendor cache rooted; still close both acquired handles. */
    s->egl_lib = dlopen(egl_name!=NULL ? egl_name : "libEGL.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    s->gl_lib = dlopen(gl_name!=NULL ? gl_name : "libGL.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (s->egl_lib == NULL || s->gl_lib == NULL) return false;
    if (!gl_symbol(s->egl_lib,"eglReleaseThread",&s->eReleaseThread,sizeof s->eReleaseThread)) return false;
    if (!gl_symbol(s->egl_lib,"eglGetProcAddress",&s->eGetProcAddress,sizeof s->eGetProcAddress)) return false;
    if (!gl_symbol(s->egl_lib,"eglQueryString",&s->eQueryString,sizeof s->eQueryString)) return false;
    if (!gl_symbol(s->egl_lib,"eglInitialize",&s->eInitialize,sizeof s->eInitialize)) return false;
    if (!gl_symbol(s->egl_lib,"eglTerminate",&s->eTerminate,sizeof s->eTerminate)) return false;
    if (!gl_symbol(s->egl_lib,"eglBindAPI",&s->eBindAPI,sizeof s->eBindAPI)) return false;
    if (!gl_symbol(s->egl_lib,"eglChooseConfig",&s->eChooseConfig,sizeof s->eChooseConfig)) return false;
    if (!gl_symbol(s->egl_lib,"eglGetConfigAttrib",&s->eGetConfigAttrib,sizeof s->eGetConfigAttrib)) return false;
    if (!gl_symbol(s->egl_lib,"eglCreateContext",&s->eCreateContext,sizeof s->eCreateContext)) return false;
    if (!gl_symbol(s->egl_lib,"eglDestroyContext",&s->eDestroyContext,sizeof s->eDestroyContext)) return false;
    if (!gl_symbol(s->egl_lib,"eglCreatePlatformWindowSurface",&s->eCreatePlatformWindowSurface,sizeof s->eCreatePlatformWindowSurface)) return false;
    if (!gl_symbol(s->egl_lib,"eglDestroySurface",&s->eDestroySurface,sizeof s->eDestroySurface)) return false;
    if (!gl_symbol(s->egl_lib,"eglMakeCurrent",&s->eMakeCurrent,sizeof s->eMakeCurrent)) return false;
    if (!gl_symbol(s->egl_lib,"eglSwapInterval",&s->eSwapInterval,sizeof s->eSwapInterval)) return false;
    if (!gl_symbol(s->egl_lib,"eglSwapBuffers",&s->eSwapBuffers,sizeof s->eSwapBuffers)) return false;
    if (!gl_symbol(s->egl_lib,"eglGetPlatformDisplay",&s->eGetPlatformDisplay,sizeof s->eGetPlatformDisplay)) return false;
    return true;
}
static bool gl_load_api(gl_state *s)
{
    if (!gl_proc(s,"glGetString",&s->gGetString,sizeof s->gGetString)) return false;
    if (!gl_proc(s,"glGetStringi",&s->gGetStringi,sizeof s->gGetStringi)) return false;
    if (!gl_proc(s,"glGetIntegerv",&s->gGetIntegerv,sizeof s->gGetIntegerv)) return false;
    if (!gl_proc(s,"glGetError",&s->gGetError,sizeof s->gGetError)) return false;
    if (!gl_proc(s,"glGenVertexArrays",&s->gGenVertexArrays,sizeof s->gGenVertexArrays)) return false;
    if (!gl_proc(s,"glBindVertexArray",&s->gBindVertexArray,sizeof s->gBindVertexArray)) return false;
    if (!gl_proc(s,"glDeleteVertexArrays",&s->gDeleteVertexArrays,sizeof s->gDeleteVertexArrays)) return false;
    if (!gl_proc(s,"glGenBuffers",&s->gGenBuffers,sizeof s->gGenBuffers)) return false;
    if (!gl_proc(s,"glBindBuffer",&s->gBindBuffer,sizeof s->gBindBuffer)) return false;
    if (!gl_proc(s,"glBufferData",&s->gBufferData,sizeof s->gBufferData)) return false;
    if (!gl_proc(s,"glBufferSubData",&s->gBufferSubData,sizeof s->gBufferSubData)) return false;
    if (!gl_proc(s,"glDeleteBuffers",&s->gDeleteBuffers,sizeof s->gDeleteBuffers)) return false;
    if (!gl_proc(s,"glMapBufferRange",&s->gMapBufferRange,sizeof s->gMapBufferRange)) return false;
    if (!gl_proc(s,"glUnmapBuffer",&s->gUnmapBuffer,sizeof s->gUnmapBuffer)) return false;
    if (!gl_proc(s,"glEnableVertexAttribArray",&s->gEnableVertexAttribArray,sizeof s->gEnableVertexAttribArray)) return false;
    if (!gl_proc(s,"glVertexAttribIPointer",&s->gVertexAttribIPointer,sizeof s->gVertexAttribIPointer)) return false;
    if (!gl_proc(s,"glVertexAttribDivisor",&s->gVertexAttribDivisor,sizeof s->gVertexAttribDivisor)) return false;
    if (!gl_proc(s,"glCreateShader",&s->gCreateShader,sizeof s->gCreateShader)) return false;
    if (!gl_proc(s,"glShaderSource",&s->gShaderSource,sizeof s->gShaderSource)) return false;
    if (!gl_proc(s,"glCompileShader",&s->gCompileShader,sizeof s->gCompileShader)) return false;
    if (!gl_proc(s,"glGetShaderiv",&s->gGetShaderiv,sizeof s->gGetShaderiv)) return false;
    if (!gl_proc(s,"glDeleteShader",&s->gDeleteShader,sizeof s->gDeleteShader)) return false;
    if (!gl_proc(s,"glCreateProgram",&s->gCreateProgram,sizeof s->gCreateProgram)) return false;
    if (!gl_proc(s,"glAttachShader",&s->gAttachShader,sizeof s->gAttachShader)) return false;
    if (!gl_proc(s,"glLinkProgram",&s->gLinkProgram,sizeof s->gLinkProgram)) return false;
    if (!gl_proc(s,"glGetProgramiv",&s->gGetProgramiv,sizeof s->gGetProgramiv)) return false;
    if (!gl_proc(s,"glDeleteProgram",&s->gDeleteProgram,sizeof s->gDeleteProgram)) return false;
    if (!gl_proc(s,"glUseProgram",&s->gUseProgram,sizeof s->gUseProgram)) return false;
    if (!gl_proc(s,"glGetUniformLocation",&s->gGetUniformLocation,sizeof s->gGetUniformLocation)) return false;
    if (!gl_proc(s,"glUniform2f",&s->gUniform2f,sizeof s->gUniform2f)) return false;
    if (!gl_proc(s,"glUniform1i",&s->gUniform1i,sizeof s->gUniform1i)) return false;
    if (!gl_proc(s,"glGenTextures",&s->gGenTextures,sizeof s->gGenTextures)) return false;
    if (!gl_proc(s,"glBindTexture",&s->gBindTexture,sizeof s->gBindTexture)) return false;
    if (!gl_proc(s,"glTexImage2D",&s->gTexImage2D,sizeof s->gTexImage2D)) return false;
    if (!gl_proc(s,"glTexSubImage2D",&s->gTexSubImage2D,sizeof s->gTexSubImage2D)) return false;
    if (!gl_proc(s,"glTexParameteri",&s->gTexParameteri,sizeof s->gTexParameteri)) return false;
    if (!gl_proc(s,"glDeleteTextures",&s->gDeleteTextures,sizeof s->gDeleteTextures)) return false;
    if (!gl_proc(s,"glActiveTexture",&s->gActiveTexture,sizeof s->gActiveTexture)) return false;
    if (!gl_proc(s,"glPixelStorei",&s->gPixelStorei,sizeof s->gPixelStorei)) return false;
    if (!gl_proc(s,"glGenFramebuffers",&s->gGenFramebuffers,sizeof s->gGenFramebuffers)) return false;
    if (!gl_proc(s,"glBindFramebuffer",&s->gBindFramebuffer,sizeof s->gBindFramebuffer)) return false;
    if (!gl_proc(s,"glFramebufferTexture2D",&s->gFramebufferTexture2D,sizeof s->gFramebufferTexture2D)) return false;
    if (!gl_proc(s,"glCheckFramebufferStatus",&s->gCheckFramebufferStatus,sizeof s->gCheckFramebufferStatus)) return false;
    if (!gl_proc(s,"glDeleteFramebuffers",&s->gDeleteFramebuffers,sizeof s->gDeleteFramebuffers)) return false;
    if (!gl_proc(s,"glViewport",&s->gViewport,sizeof s->gViewport)) return false;
    if (!gl_proc(s,"glScissor",&s->gScissor,sizeof s->gScissor)) return false;
    if (!gl_proc(s,"glEnable",&s->gEnable,sizeof s->gEnable)) return false;
    if (!gl_proc(s,"glDisable",&s->gDisable,sizeof s->gDisable)) return false;
    if (!gl_proc(s,"glClearColor",&s->gClearColor,sizeof s->gClearColor)) return false;
    if (!gl_proc(s,"glClear",&s->gClear,sizeof s->gClear)) return false;
    if (!gl_proc(s,"glDrawArraysInstanced",&s->gDrawArraysInstanced,sizeof s->gDrawArraysInstanced)) return false;
    if (!gl_proc(s,"glBlitFramebuffer",&s->gBlitFramebuffer,sizeof s->gBlitFramebuffer)) return false;
    if (!gl_proc(s,"glFenceSync",&s->gFenceSync,sizeof s->gFenceSync)) return false;
    if (!gl_proc(s,"glClientWaitSync",&s->gClientWaitSync,sizeof s->gClientWaitSync)) return false;
    if (!gl_proc(s,"glDeleteSync",&s->gDeleteSync,sizeof s->gDeleteSync)) return false;
    if (!gl_proc(s,"glFlush",&s->gFlush,sizeof s->gFlush)) return false;
    if (!gl_proc(s,"glFinish",&s->gFinish,sizeof s->gFinish)) return false;
    if (!gl_proc(s,"glReadPixels",&s->gReadPixels,sizeof s->gReadPixels)) return false;
    if (!gl_proc(s,"glTexBuffer",&s->gTexBuffer,sizeof s->gTexBuffer)) return false;
    if (!gl_proc(s,"glUniform1ui",&s->gUniform1ui,sizeof s->gUniform1ui)) return false;
    if (!gl_proc(s,"glUniform2ui",&s->gUniform2ui,sizeof s->gUniform2ui)) return false;
    (void)gl_proc(s, "glBufferStorage", &s->gBufferStorage, sizeof s->gBufferStorage);
    return true;
}
/* Integer R8 and integer arithmetic implement the frozen sRGB blend exactly. */
static GLuint gl_shader(gl_state *s, GLenum type, const char *source)
{
    GLuint shader = s->gCreateShader(type); if (shader == 0) return 0;
    s->gShaderSource(shader, 1, &source, NULL); s->gCompileShader(shader);
    GLint ok = 0; s->gGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { s->gDeleteShader(shader); return 0; }
    return shader;
}
static bool gl_program_init(gl_state *s)
{
    const char *vertex =
        "#version 330 core\n"
        "layout(location=0) in uvec4 position;\n"
        "layout(location=1) in uvec4 glyph;\n"
        "layout(location=2) in uvec4 colors;\n"
        "uniform vec2 surface_size; out vec2 local;\n"
        "flat out uvec4 image; flat out uint attrs; flat out uvec2 extent; flat out ivec3 delta; flat out ivec3 base;\n"
        "ivec3 rgb(uint c){return ivec3(c>>16,(c>>8)&255u,c&255u); }\n"
        "void main(){ vec2 q=vec2(gl_VertexID&1,gl_VertexID>>1);\n"
        "local=q*vec2(position.zw); vec2 p=vec2(position.xy)+local;\n"
        "gl_Position=vec4(p.x*2.0/surface_size.x-1.0,1.0-p.y*2.0/surface_size.y,0,1);\n"
        "image=glyph; attrs=colors.z; extent=position.zw; ivec3 fg=rgb(colors.x),bg=rgb(colors.y); if((attrs&8u)!=0u){ivec3 t=fg;fg=bg;bg=t;} delta=fg-bg; base=bg*255+127; }\n";
    if (s->upload!=GL_UPLOAD_LEGACY) vertex=
        "#version 330 core\n"
        "layout(location=0) in uvec4 cell; layout(location=1) in uint bits;\n"
        "uniform usamplerBuffer images; uniform uint first_cell; uniform uint cols; uniform uvec2 cell_size;\n"
        "uniform vec2 surface_size; out vec2 local;\n"
        "flat out uvec4 image; flat out uint attrs; flat out uvec2 extent; flat out ivec3 delta; flat out ivec3 base;\n"
        "ivec3 rgb(uint c){return ivec3(c>>16,(c>>8)&255u,c&255u); }\n"
        "void main(){uint i=first_cell+uint(gl_InstanceID); attrs=bits&65535u;\n"
        "extent=cell_size; if((attrs&16u)!=0u)extent.x*=2u;\n"
        "vec2 q=vec2(gl_VertexID&1,gl_VertexID>>1); local=q*vec2(extent);\n"
        "vec2 p=vec2(uvec2(i%cols,i/cols)*cell_size)+local;\n"
        "gl_Position=vec4(p.x*2.0/surface_size.x-1.0,1.0-p.y*2.0/surface_size.y,0,1);\n"
        "image=cell.y==0xffffffffu ? uvec4(0) : texelFetch(images,int(cell.y));\n"
        "ivec3 fg=rgb(cell.z),bg=rgb(cell.w); if((attrs&8u)!=0u){ivec3 t=fg;fg=bg;bg=t;}\n"
        "delta=fg-bg; base=bg*255+127; }\n";
    const char *fragment =
        "#version 330 core\n"
        "uniform usampler2D atlas; uniform int atlas_mask; uniform int atlas_shift;\n"
        "in vec2 local; flat in uvec4 image; flat in uint attrs; flat in uvec2 extent; flat in ivec3 delta; flat in ivec3 base;\n"
        "out vec4 output_color;\n"
        "void main(){ if((attrs&32u)!=0u)discard; uvec2 p=uvec2(local); uint a=0u;\n"
        "if(p.x<image.z && p.y<image.w){uint i=image.x+p.y*image.y+p.x;\n"
        "a=texelFetch(atlas,ivec2(i&uint(atlas_mask),i>>uint(atlas_shift)),0).r;}\n"
        "if((attrs&4u)!=0u && p.y==extent.y-1u)a=255u;\n"
        "output_color=vec4(vec3(uvec3(base+delta*int(a))/255u)/255.0,1); }\n";
    GLuint vs = gl_shader(s, GL_VERTEX_SHADER, vertex);
    GLuint fs = gl_shader(s, GL_FRAGMENT_SHADER, fragment);
    if (vs == 0 || fs == 0) { if (vs) s->gDeleteShader(vs); if (fs) s->gDeleteShader(fs); return false; }
    s->program = s->gCreateProgram();
    s->gAttachShader(s->program, vs); s->gAttachShader(s->program, fs); s->gLinkProgram(s->program);
    s->gDeleteShader(vs); s->gDeleteShader(fs);
    GLint ok = 0; s->gGetProgramiv(s->program, GL_LINK_STATUS, &ok); if (!ok) return false;
    s->surface_uniform = s->gGetUniformLocation(s->program, "surface_size");
    s->atlas_uniform = s->gGetUniformLocation(s->program, "atlas_mask");
    s->shift_uniform = s->gGetUniformLocation(s->program, "atlas_shift");
    s->gUseProgram(s->program); s->gUniform1i(s->gGetUniformLocation(s->program, "atlas"), 0);
    s->gUniform1i(s->atlas_uniform, (GLint)(s->atlas_width-1));
    GLint shift=0; for (uint32_t n=s->atlas_width;n>1;n>>=1) shift++;
    s->gUniform1i(s->shift_uniform, shift);
    if (s->upload!=GL_UPLOAD_LEGACY) {
        s->gUniform1i(s->gGetUniformLocation(s->program,"images"),1);
        s->first_uniform=s->gGetUniformLocation(s->program,"first_cell");
        s->cols_uniform=s->gGetUniformLocation(s->program,"cols");
        s->cell_uniform=s->gGetUniformLocation(s->program,"cell_size");
    }
    return s->surface_uniform >= 0 && s->atlas_uniform >= 0;
}
static void gl_attributes(gl_state *s, size_t first)
{
    if (s->upload!=GL_UPLOAD_LEGACY) {
        size_t off=first*sizeof(render_cell);
        if (s->upload==GL_UPLOAD_PERSISTENT) off+=(size_t)s->ring_current*s->cell_bytes;
        s->gVertexAttribIPointer(0,4,GL_UNSIGNED_INT,(GLsizei)sizeof(render_cell),(const void *)(uintptr_t)off);
        s->gVertexAttribIPointer(1,1,GL_UNSIGNED_SHORT,(GLsizei)sizeof(render_cell),(const void *)(uintptr_t)(off+offsetof(render_cell,attrs)));
        return;
    }
    for (GLuint i = 0; i < 3; i++) {
        size_t off = first * sizeof(gl_instance) + (size_t)i * 4 * sizeof(uint32_t);
        s->gVertexAttribIPointer(i, 4, GL_UNSIGNED_INT, (GLsizei)sizeof(gl_instance), (const void *)(uintptr_t)off);
    }
}
static bool gl_storage_available(gl_state *s)
{
    if (s->gBufferStorage==NULL) return false;
    GLint extensions=0; s->gGetIntegerv(GL_NUM_EXTENSIONS,&extensions);
    for (GLint i=0;i<extensions;i++) {
        const char *ext=(const char *)s->gGetStringi(GL_EXTENSIONS,(GLuint)i);
        if (ext!=NULL && strcmp(ext,"GL_ARB_buffer_storage")==0) return true;
    }
    return false;
}
static bool gl_resources(gl_state *s, const render_config *cfg)
{
    GLint max_texture = 0; s->gGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    if (max_texture <= 0 || cfg->max_width > (uint32_t)max_texture || cfg->max_height > (uint32_t)max_texture) return false;
    size_t atlas_bytes = cfg->max_atlas_bytes != 0 ? cfg->max_atlas_bytes : 1;
    if (atlas_bytes > UINT32_MAX || atlas_bytes > SIZE_MAX-(size_t)max_texture) return false;
    s->atlas_width = 1;
    while (s->atlas_width < atlas_bytes && s->atlas_width <= (uint32_t)max_texture/2u) s->atlas_width *= 2;
    size_t height = (atlas_bytes + s->atlas_width - 1) / s->atlas_width;
    if (height > (size_t)max_texture || atlas_bytes > UINT32_MAX) return false;
    s->atlas_height = (uint32_t)height;
    size_t cache_bytes = (size_t)s->atlas_width * s->atlas_height;
    if (cfg->max_cells > (size_t)INT_MAX || cfg->max_cells > (size_t)PTRDIFF_MAX/sizeof(gl_instance) ||
        cfg->max_pages > SIZE_MAX/sizeof(size_t) || cfg->max_glyphs == SIZE_MAX) return false;
    s->vbo_bytes = cfg->max_cells * sizeof(gl_instance);
    s->cell_bytes=cfg->max_cells*sizeof(render_cell);
    if (s->upload!=GL_UPLOAD_LEGACY) {
        if (cfg->max_glyphs>(size_t)max_texture || cfg->max_glyphs>(size_t)PTRDIFF_MAX/16 ||
            s->cell_bytes>(size_t)PTRDIFF_MAX/GL_CELL_RING) return false;
        s->image_bytes=(cfg->max_glyphs!=0 ? cfg->max_glyphs : 1)*16;
    }
    size_t max_rows=cfg->max_cells < cfg->max_height ? cfg->max_cells : cfg->max_height;
    size_t strip_bytes = ((max_rows+1)/2) * sizeof(gl_strip);
    size_t page_bytes = cfg->max_pages * sizeof(size_t);
    if (strip_bytes > SIZE_MAX-s->vbo_bytes || cache_bytes > SIZE_MAX-strip_bytes-s->vbo_bytes ||
        page_bytes > SIZE_MAX-cache_bytes-strip_bytes-s->vbo_bytes) return false;
    size_t instance_bytes=s->upload==GL_UPLOAD_LEGACY ? s->vbo_bytes : 0;
    size_t reserve = instance_bytes + strip_bytes + cache_bytes + page_bytes;
    if (reserve > SIZE_MAX-256 || height > SIZE_MAX-reserve-256) return false;
    reserve += height;
    if (cfg->max_glyphs > SIZE_MAX-reserve-256) return false;
    reserve += cfg->max_glyphs + 256;
    if (s->image_bytes>SIZE_MAX-reserve || s->cell_bytes>SIZE_MAX-reserve-s->image_bytes) return false;
    if (s->upload!=GL_UPLOAD_LEGACY) reserve+=s->image_bytes+s->cell_bytes;
    edit_arena *arena = cfg->arena;
    if (arena == NULL) { if (edit_arena_init(&s->arena, reserve) != 0) return false; s->own_arena = true; arena = &s->arena; }
    if (s->upload==GL_UPLOAD_LEGACY) s->instances = edit_arena_alloc(arena, s->vbo_bytes, _Alignof(gl_instance));
    s->strips = edit_arena_alloc(arena, strip_bytes, _Alignof(gl_strip));
    s->atlas_cache = edit_arena_alloc(arena, cache_bytes, 1);
    s->atlas_dirty = edit_arena_alloc(arena, height, 1);
    s->page_offsets = edit_arena_alloc(arena, page_bytes != 0 ? page_bytes : 1, _Alignof(size_t));
    s->glyph_seen = edit_arena_alloc(arena, cfg->max_glyphs != 0 ? cfg->max_glyphs : 1, 1);
    if ((s->upload==GL_UPLOAD_LEGACY && !s->instances) || !s->strips || !s->atlas_cache || !s->atlas_dirty || !s->page_offsets || !s->glyph_seen) return false;
    if (s->upload!=GL_UPLOAD_LEGACY) {
        s->images=edit_arena_alloc(arena,s->image_bytes,_Alignof(uint32_t));
        if (s->upload!=GL_UPLOAD_PERSISTENT)
            s->cells=edit_arena_alloc(arena,s->cell_bytes,_Alignof(render_cell));
        if (s->images==NULL || (s->upload!=GL_UPLOAD_PERSISTENT && s->cells==NULL)) return false;
    }
    memset(s->atlas_cache, 0, cache_bytes);
    memset(s->atlas_dirty, 0, height);
    bool storage=gl_storage_available(s);
    if (s->upload==GL_UPLOAD_PERSISTENT && (!storage || s->gBufferStorage==NULL)) return false;
    s->persistent = s->requested_persistent && storage && s->gBufferStorage != NULL;
    s->gGenVertexArrays(1, &s->vao); s->gBindVertexArray(s->vao);
    s->gGenBuffers(1, &s->vbo); s->gBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    if (s->instances!=NULL) memset(s->instances, 0, s->vbo_bytes);
    if (s->upload==GL_UPLOAD_PERSISTENT) {
        /* Validation and partial-frame preservation read our CPU-written
         * cells too, so this lease must permit both reads and writes. */
        GLbitfield flags=GL_MAP_READ_BIT|GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
        size_t bytes=s->cell_bytes*GL_CELL_RING;
        s->gBufferStorage(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,NULL,flags);
        s->mapped=s->gMapBufferRange(GL_ARRAY_BUFFER,0,(GLsizeiptr)bytes,flags);
        if (s->mapped==NULL) return false;
        for (uint32_t i=0;i<GL_CELL_RING;i++) {
            s->ring[i].cells=(render_cell *)((uint8_t *)s->mapped+(size_t)i*s->cell_bytes);
            for (size_t j=0;j<cfg->max_cells;j++)
                s->ring[i].cells[j]=(render_cell){0,RENDER_NO_SLOT,0,0,0,0};
        }
    } else if (s->upload!=GL_UPLOAD_LEGACY) {
        s->gBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)s->cell_bytes,NULL,GL_STREAM_DRAW);
    } else if (s->persistent) {
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        s->gBufferStorage(GL_ARRAY_BUFFER, (GLsizeiptr)s->vbo_bytes, s->instances, flags);
        s->mapped = s->gMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)s->vbo_bytes, flags);
        if (s->mapped == NULL) return false;
    } else s->gBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s->vbo_bytes, s->instances, GL_STREAM_DRAW);
    gl_attributes(s, 0);
    GLuint attributes=s->upload==GL_UPLOAD_LEGACY ? 3u : 2u;
    for (GLuint i=0;i<attributes;i++) { s->gEnableVertexAttribArray(i); s->gVertexAttribDivisor(i, 1); }
    if (s->upload!=GL_UPLOAD_LEGACY) {
        s->gGenBuffers(1,&s->image_vbo); s->gBindBuffer(GL_TEXTURE_BUFFER,s->image_vbo);
        s->gBufferData(GL_TEXTURE_BUFFER,(GLsizeiptr)s->image_bytes,NULL,GL_STREAM_DRAW);
        s->gGenTextures(1,&s->image_texture); s->gBindTexture(GL_TEXTURE_BUFFER,s->image_texture);
        s->gTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32UI,s->image_vbo);
    }
    s->gGenTextures(1, &s->atlas_texture); s->gBindTexture(GL_TEXTURE_2D, s->atlas_texture);
    s->gTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s->gTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s->gPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    s->gTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, (GLsizei)s->atlas_width, (GLsizei)s->atlas_height, 0,
                  GL_RED_INTEGER, GL_UNSIGNED_BYTE, s->atlas_cache);
    s->gGenTextures(1, &s->target_texture); s->gBindTexture(GL_TEXTURE_2D, s->target_texture);
    s->gTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s->gTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s->gTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)cfg->max_width, (GLsizei)cfg->max_height,
                  0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    s->gGenFramebuffers(1, &s->fbo); s->gBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    s->gFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->target_texture, 0);
    if (s->gCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    s->gDisable(GL_DITHER); s->gDisable(GL_FRAMEBUFFER_SRGB); s->gDisable(GL_BLEND);
    s->gClearColor(0,0,0,1); s->gClear(GL_COLOR_BUFFER_BIT);
    return gl_program_init(s) && s->gGetError() == GL_NO_ERROR;
}
static bool gl_bind(gl_state *s)
{
    if (s->bound) return true;
    if (!s->eMakeCurrent(s->display,s->surface,s->surface,s->context)) return false;
    s->bound = true; return true;
}
static void gl_release(gl_state *s)
{
    if (s->present_special!=NULL) {
        xcb_connection_t *conn=s->platform->conn;
        if (s->complete_selected) xcb_present_select_input(conn,s->present_eid,s->platform->win,0);
        xcb_unregister_for_special_event(conn,s->present_special);
        (void)xcb_flush(conn);
    }
    if (s->context != EGL_NO_CONTEXT && s->eMakeCurrent != NULL && gl_bind(s)) {
        if (s->gFinish != NULL) s->gFinish();
        for (uint32_t i=0;i<GL_CELL_RING;i++)
            if (s->ring[i].fence!=NULL && s->gDeleteSync!=NULL) s->gDeleteSync(s->ring[i].fence);
        if (s->image_vbo && s->gDeleteBuffers) s->gDeleteBuffers(1,&s->image_vbo);
        if (s->image_texture && s->gDeleteTextures) s->gDeleteTextures(1,&s->image_texture);
        if (s->fence != NULL && s->gDeleteSync != NULL) s->gDeleteSync(s->fence);
        if (s->mapped != NULL && s->gUnmapBuffer != NULL) { s->gBindBuffer(GL_ARRAY_BUFFER,s->vbo); (void)s->gUnmapBuffer(GL_ARRAY_BUFFER); }
        if (s->gDeleteProgram != NULL && s->program) s->gDeleteProgram(s->program);
        if (s->gDeleteFramebuffers != NULL && s->fbo) s->gDeleteFramebuffers(1,&s->fbo);
        if (s->gDeleteTextures != NULL) {
            if (s->atlas_texture) s->gDeleteTextures(1,&s->atlas_texture);
            if (s->target_texture) s->gDeleteTextures(1,&s->target_texture);
        }
        if (s->gDeleteBuffers != NULL && s->vbo) s->gDeleteBuffers(1,&s->vbo);
        if (s->gDeleteVertexArrays != NULL && s->vao) s->gDeleteVertexArrays(1,&s->vao);
        (void)s->eMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    }
    if (s->context != EGL_NO_CONTEXT && s->eDestroyContext != NULL) (void)s->eDestroyContext(s->display,s->context);
    if (s->surface != EGL_NO_SURFACE && s->eDestroySurface != NULL) (void)s->eDestroySurface(s->display,s->surface);
    if (s->egl_live && s->eTerminate != NULL) (void)s->eTerminate(s->display);
    if (s->eReleaseThread != NULL) (void)s->eReleaseThread();
    if (s->gl_lib != NULL) (void)dlclose(s->gl_lib);
    if (s->egl_lib != NULL) (void)dlclose(s->egl_lib);
    if (s->own_arena) edit_arena_free(&s->arena);
    memset(s,0,sizeof *s);
}
/* Poll outside the input->submit boundary; acquire only inspects cached state. */
static int gl_ring_poll(gl_state *s)
{
    for (uint32_t i=0;i<GL_CELL_RING;i++) if (s->ring[i].fence!=NULL) {
        GLenum status=s->gClientWaitSync(s->ring[i].fence,0,0);
        if (status==GL_WAIT_FAILED) return RENDER_ERR_DEVICE;
        if (status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED) {
            s->gDeleteSync(s->ring[i].fence); s->ring[i].fence=NULL;
        }
    }
    return s->ring_poisoned ? RENDER_ERR_DEVICE : RENDER_OK;
}
static void gl_draw(gl_state *s)
{
    uint32_t w=s->dims.cols*s->dims.cell_w, h=s->dims.rows*s->dims.cell_h;
    s->gBindFramebuffer(GL_FRAMEBUFFER,s->fbo);
    s->gViewport(0,0,(GLsizei)w,(GLsizei)h);
    s->gUseProgram(s->program); s->gUniform2f(s->surface_uniform,(GLfloat)w,(GLfloat)h);
    s->gActiveTexture(GL_TEXTURE0); s->gBindTexture(GL_TEXTURE_2D,s->atlas_texture);
    s->gBindVertexArray(s->vao); s->gBindBuffer(GL_ARRAY_BUFFER,s->vbo);
    if (s->upload!=GL_UPLOAD_LEGACY) {
        s->gUniform1ui(s->cols_uniform,s->dims.cols);
        s->gUniform2ui(s->cell_uniform,s->dims.cell_w,s->dims.cell_h);
        s->gActiveTexture(GL_TEXTURE1); s->gBindTexture(GL_TEXTURE_BUFFER,s->image_texture);
    }
    s->gEnable(GL_SCISSOR_TEST);
    for (size_t i=0;i<s->strip_count;i++) {
        gl_strip *strip=&s->strips[i];
        uint32_t sh=strip->rows.row_count*s->dims.cell_h;
        uint32_t sy=h-(strip->rows.first_row+strip->rows.row_count)*s->dims.cell_h;
        s->gScissor(0,(GLint)sy,(GLsizei)w,(GLsizei)sh);
        gl_attributes(s,strip->first);
        if (s->upload!=GL_UPLOAD_LEGACY) s->gUniform1ui(s->first_uniform,(GLuint)strip->first);
        s->gDrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,(GLsizei)strip->count);
    }
    if (s->upload==GL_UPLOAD_PERSISTENT && s->instance_count!=0) {
        gl_cell_slot *slot=&s->ring[s->ring_current];
        if (slot->fence!=NULL) s->gDeleteSync(slot->fence);
        slot->fence=s->gFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if (slot->fence==NULL) s->ring_poisoned=true;
        s->gFlush();
    }
    s->gDisable(GL_SCISSOR_TEST);
    s->gBindFramebuffer(GL_READ_FRAMEBUFFER,s->fbo); s->gBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    /* A grid can leave a fractional-cell margin at the window's bottom.
     * Align its destination to native top-left, just like the cell contract. */
    GLint native_h=(GLint)s->platform->height;
    s->gBlitFramebuffer(0,0,(GLint)w,(GLint)h,0,native_h-(GLint)h,(GLint)w,native_h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
}
/* EGL has no portable Present serial API. Restrict to Mesa DRI3 and prove its
 * low-32-bit SBC serial convention with three real swaps before accepting init.
 * No NotifyMSC surrogate: only PIXMAP Complete events acknowledge our swaps. */
static bool gl_probe_present(gl_state *s)
{
    xcb_connection_t *conn=s->platform->conn;
    s->present_eid=xcb_generate_id(conn);
    s->present_special=xcb_register_for_special_xge(conn,&xcb_present_id,s->present_eid,&s->present_stamp);
    if (s->present_special==NULL) return false;
    xcb_void_cookie_t select=xcb_present_select_input_checked(conn,s->present_eid,s->platform->win,XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY);
    xcb_generic_error_t *err=xcb_request_check(conn,select);
    if (err!=NULL) { free(err); return false; }
    s->complete_selected=true;
    bool ok=true;
    for (uint32_t n=0;n<3 && ok;n++) {
        gl_draw(s);
        if (!s->eSwapBuffers(s->display,s->surface)) { ok=false; break; }
        s->gFinish();
        uint64_t deadline=trace_now_ns()+UINT64_C(1500000000);
        bool seen=false;
        while (!seen && trace_now_ns()<deadline) {
            xcb_generic_event_t *event=xcb_poll_for_special_event(conn,s->present_special);
            if (event==NULL) {
                struct pollfd fd={xcb_get_file_descriptor(conn),POLLIN,0}; (void)poll(&fd,1,1); continue;
            }
            xcb_present_complete_notify_event_t *ce=(xcb_present_complete_notify_event_t *)event;
            if (ce->event_type==XCB_PRESENT_COMPLETE_NOTIFY && ce->kind==XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                if (ce->serial!=n+1 || ce->msc==0) ok=false;
                s->swap_serial=ce->serial; s->msc=ce->msc; seen=true;
            }
            free(event);
        }
        if (!seen) ok=false;
    }
    return ok;
}
static int gl_context_init(gl_state *s,const render_config *cfg,EGLint swap_interval)
{
    int rc=RENDER_ERR_UNSUPPORTED;
    if (!gl_load(s)) goto fail;
    const char *client=s->eQueryString(EGL_NO_DISPLAY,EGL_EXTENSIONS);
    if (!gl_extension(client,"EGL_EXT_platform_xcb") && !gl_extension(client,"EGL_MESA_platform_xcb")) goto fail;
    (void)xcb_flush(s->platform->conn);
    s->display=s->eGetPlatformDisplay(EGL_PLATFORM_XCB_EXT,s->platform->conn,NULL);
    if (s->display==EGL_NO_DISPLAY || !s->eInitialize(s->display,NULL,NULL)) goto fail;
    s->egl_live=true;
    const char *extensions=s->eQueryString(s->display,EGL_EXTENSIONS);
    const char *vendor=s->eQueryString(s->display,EGL_VENDOR);
    if (!gl_extension(extensions,"EGL_KHR_create_context") || vendor==NULL || strstr(vendor,"Mesa")==NULL) goto fail;
    if (!s->eBindAPI(EGL_OPENGL_API)) goto fail;
    EGLConfig configs[256]; EGLint count=0;
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_STENCIL_SIZE,0,EGL_NONE};
    if (!s->eChooseConfig(s->display,attrs,configs,256,&count)) goto fail;
    EGLConfig chosen=NULL;
    for (EGLint i=0;i<count;i++) {
        EGLint visual=0;
        if (s->eGetConfigAttrib(s->display,configs[i],EGL_NATIVE_VISUAL_ID,&visual) && (uint32_t)visual==s->platform->visual) { chosen=configs[i]; break; }
    }
    if (chosen==NULL) goto fail;
    const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
    s->context=s->eCreateContext(s->display,chosen,EGL_NO_CONTEXT,ca);
    if (s->context==EGL_NO_CONTEXT) goto fail;
    s->surface=s->eCreatePlatformWindowSurface(s->display,chosen,&s->platform->win,NULL);
    if (s->surface==EGL_NO_SURFACE || !gl_bind(s)) goto fail;
    if (!s->eSwapInterval(s->display,swap_interval) || !gl_load_api(s)) goto fail;
    const char *renderer=(const char *)s->gGetString(GL_RENDERER);
    if (renderer==NULL) goto fail;
    size_t renderer_len=strlen(renderer); if (renderer_len>=sizeof s->device_name) renderer_len=sizeof s->device_name-1;
    memcpy(s->device_name,renderer,renderer_len); s->device_name[renderer_len]='\0';
    GLint major=0,minor=0,profile=0;
    s->gGetIntegerv(GL_MAJOR_VERSION,&major); s->gGetIntegerv(GL_MINOR_VERSION,&minor);
    s->gGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
    if (major<3 || (major==3 && minor<3) || !(profile&GL_CONTEXT_CORE_PROFILE_BIT)) goto fail;
    if (s->upload==GL_UPLOAD_PERSISTENT && !gl_storage_available(s)) goto fail;
    rc=RENDER_ERR_INIT;
    if (!gl_resources(s,cfg)) goto fail;
    return RENDER_OK;
fail:
    return rc;
}
static int gl_init(render_backend *b,const render_config *cfg)
{
    gl_state *s=b->state; memset(s,0,sizeof *s);
    if (cfg->platform==NULL || cfg->platform->conn==NULL || !cfg->platform->present_ok) return RENDER_ERR_UNSUPPORTED;
    s->platform=cfg->platform; s->dims=cfg->dims; s->max_width=cfg->max_width; s->max_height=cfg->max_height;
    if (gl_select_upload(s)!=RENDER_OK) return RENDER_ERR_ARG;
    const char *mode=getenv("EDIT_GL_VBO");
    if (mode!=NULL && strcmp(mode,"orphan")!=0 && strcmp(mode,"persistent")!=0) return RENDER_ERR_INIT;
    s->requested_persistent=s->upload==GL_UPLOAD_LEGACY && mode!=NULL && strcmp(mode,"persistent")==0;
    const char *interval=getenv("EDIT_GL_SWAP_INTERVAL"); EGLint swap_interval=0;
    if (interval!=NULL) {
        if (strcmp(interval,"1")==0) swap_interval=1;
        else if (strcmp(interval,"0")!=0) return RENDER_ERR_INIT;
    }
    edit_arena_mark_t mark=cfg->arena!=NULL ? edit_arena_mark(cfg->arena) : 0;
    int rc=gl_context_init(s,cfg,swap_interval);
    if (rc!=RENDER_OK) goto fail;
    /* Exercise the draw state/JIT on init's worker before timing/UI guard. */
    s->strip_count=1; s->strips[0]=(gl_strip){{0,cfg->dims.rows},0,1};
    if (s->upload!=GL_UPLOAD_LEGACY) {
        render_cell cell={0,RENDER_NO_SLOT,0xffffff,0,0,0};
        if (s->upload==GL_UPLOAD_PERSISTENT) s->ring[0].cells[0]=cell;
        else s->gBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)sizeof cell,&cell);
    }
    if (s->upload==GL_UPLOAD_LEGACY) s->instances[0]=(gl_instance){{0,0,cfg->dims.cell_w,cfg->dims.cell_h},{0,0,0,0},{0xffffff,0,0,0}};
    if (s->persistent) memcpy(s->mapped,s->instances,sizeof(gl_instance));
    else if (s->upload==GL_UPLOAD_LEGACY) s->gBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)sizeof(gl_instance),s->instances);
    rc=RENDER_ERR_UNSUPPORTED;
    if (!gl_probe_present(s)) goto fail;
    s->strip_count=0;
    if (gl_ring_poll(s)!=RENDER_OK) goto fail;
    if (!s->eMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) goto fail;
    s->bound=false;
    if (!s->eReleaseThread()) goto fail;
    return RENDER_OK;
fail:
    gl_release(s);
    if (cfg->arena!=NULL) edit_arena_reset_to_mark(cfg->arena,mark);
    return rc;
}
static int gl_resize(render_backend *b,render_dims dims)
{
    gl_state *s=b->state; s->dims=dims; return RENDER_OK;
}
/* All native calls belong to present, after the input->submit allocation
 * boundary. Changed atlas rows reference only our init-reserved pixel copy. */
static void gl_upload_pending(gl_state *s)
{
    s->gActiveTexture(GL_TEXTURE0); s->gBindTexture(GL_TEXTURE_2D,s->atlas_texture);
    for (uint32_t row=0;row<s->atlas_height;) {
        if (!s->atlas_dirty[row]) { row++; continue; }
        uint32_t first=row;
        while (row<s->atlas_height && s->atlas_dirty[row]) row++;
        s->gTexSubImage2D(GL_TEXTURE_2D,0,0,(GLint)first,(GLsizei)s->atlas_width,
            (GLsizei)(row-first),GL_RED_INTEGER,GL_UNSIGNED_BYTE,
            s->atlas_cache+(size_t)first*s->atlas_width);
        memset(s->atlas_dirty+first,0,row-first);
    }
    if (s->upload!=GL_UPLOAD_LEGACY) {
        if (s->image_count!=0) {
            s->gBindBuffer(GL_TEXTURE_BUFFER,s->image_vbo);
            s->gBufferSubData(GL_TEXTURE_BUFFER,0,(GLsizeiptr)(s->image_count*16),s->images);
        }
        if (s->upload!=GL_UPLOAD_PERSISTENT && s->instance_count!=0) {
            s->gBindBuffer(GL_ARRAY_BUFFER,s->vbo);
            if (s->upload==GL_UPLOAD_ORPHAN)
                s->gBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)s->cell_bytes,NULL,GL_STREAM_DRAW);
            for (size_t i=0;i<s->strip_count;i++) {
                const gl_strip *strip=&s->strips[i];
                s->gBufferSubData(GL_ARRAY_BUFFER,(GLintptr)(strip->first*sizeof(render_cell)),
                    (GLsizeiptr)(strip->count*sizeof(render_cell)),s->cells+strip->first);
            }
        }
    } else if (!s->persistent && s->instance_count!=0) {
        s->gBindBuffer(GL_ARRAY_BUFFER,s->vbo);
        s->gBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)s->vbo_bytes,NULL,GL_STREAM_DRAW);
        s->gBufferSubData(GL_ARRAY_BUFFER,0,
            (GLsizeiptr)(s->instance_count*sizeof(gl_instance)),s->instances);
    }
}
static int gl_submit(render_backend *b,const render_grid *g,const render_strip *strips,size_t count)
{
    gl_state *s=b->state;
    if (s->upload==GL_UPLOAD_PERSISTENT) {
        if (!s->leased) {
            if (s->ring_poisoned) return RENDER_ERR_DEVICE;
            if (s->ring[s->ring_next].fence!=NULL) return RENDER_ERR_BUSY;
            s->ring_current=s->ring_next;
        } else if (g->cells!=s->ring[s->ring_current].cells) return RENDER_ERR_STATE;
        if (g->cells!=s->ring[s->ring_current].cells)
            memcpy(s->ring[s->ring_current].cells,g->cells,(size_t)g->dims.cols*g->dims.rows*sizeof(render_cell));
    }
    size_t total=0;
    for (size_t i=0;i<g->page_count;i++) { s->page_offsets[i]=total; total+=(size_t)g->pages[i].width*g->pages[i].height; }
    if (s->upload!=GL_UPLOAD_LEGACY) {
        s->image_count=g->glyph_count;
        for (size_t i=0;i<g->glyph_count;i++) {
            const render_glyph *glyph=&g->glyphs[i];
            const render_atlas_page *page=&g->pages[glyph->page];
            s->images[i][0]=(uint32_t)(s->page_offsets[glyph->page]+(size_t)glyph->y*page->width+glyph->x);
            s->images[i][1]=page->width; s->images[i][2]=glyph->w; s->images[i][3]=glyph->h;
        }
    }
    memset(s->glyph_seen,0,g->glyph_count);
    size_t n=0;
    for (size_t i=0;i<count;i++) {
        s->strips[i]=(gl_strip){strips[i],n,(size_t)strips[i].row_count*g->dims.cols};
        if (s->upload!=GL_UPLOAD_LEGACY) {
            s->strips[i].first=(size_t)strips[i].first_row*g->dims.cols;
            if (s->upload!=GL_UPLOAD_PERSISTENT)
                memcpy(s->cells+s->strips[i].first,g->cells+s->strips[i].first,s->strips[i].count*sizeof(render_cell));
        }
        uint32_t end=strips[i].first_row+strips[i].row_count;
        for (uint32_t row=strips[i].first_row;row<end;row++) for (uint32_t col=0;col<g->dims.cols;col++) {
            const render_cell *c=&g->cells[(size_t)row*g->dims.cols+col];
            uint32_t image[4]={0};
            if (c->atlas_slot!=RENDER_NO_SLOT) {
                const render_glyph *glyph=&g->glyphs[c->atlas_slot];
                const render_atlas_page *page=&g->pages[glyph->page];
                size_t off=s->page_offsets[glyph->page]+(size_t)glyph->y*page->width+glyph->x;
                if (s->upload==GL_UPLOAD_LEGACY) {
                    image[0]=(uint32_t)off; image[1]=page->width; image[2]=glyph->w; image[3]=glyph->h;
                }
                if (!s->glyph_seen[c->atlas_slot]) {
                    s->glyph_seen[c->atlas_slot]=1;
                    for (uint32_t y=0;y<glyph->h;y++) {
                        size_t dst=off+(size_t)y*page->width;
                        const uint8_t *src=page->pixels+(size_t)(glyph->y+y)*page->stride+glyph->x;
                        if (memcmp(s->atlas_cache+dst,src,glyph->w)!=0) {
                            memcpy(s->atlas_cache+dst,src,glyph->w);
                            size_t first=dst/s->atlas_width;
                            size_t last=(dst+glyph->w-1)/s->atlas_width;
                            memset(s->atlas_dirty+first,1,last-first+1);
                        }
                    }
                }
            }
            if (s->upload==GL_UPLOAD_LEGACY) {
                uint32_t width=g->dims.cell_w*((c->attrs & RENDER_ATTR_WIDE_LEFT) ? 2u : 1u);
                s->instances[n]=(gl_instance){{col*g->dims.cell_w,row*g->dims.cell_h,width,g->dims.cell_h},
                    {image[0],image[1],image[2],image[3]},{c->fg,c->bg,c->attrs,0}};
            }
            n++;
        }
    }
    s->strip_count=count; s->instance_count=n;
    if (n!=0 && s->persistent) memcpy(s->mapped,s->instances,n*sizeof(gl_instance));
    if (s->upload==GL_UPLOAD_PERSISTENT) {
        s->ring_last=s->ring_current; s->ring_has_last=true;
        s->ring_next=(s->ring_current+1u)%GL_CELL_RING; s->leased=false;
    }
    return RENDER_OK;
}
static int gl_present(render_backend *b,uint32_t id)
{
    (void)id; gl_state *s=b->state;
    if (!gl_bind(s)) return RENDER_ERR_DEVICE;
    gl_upload_pending(s);
    gl_draw(s);
    if (s->ring_poisoned) return RENDER_ERR_DEVICE;
    if (!s->eSwapBuffers(s->display,s->surface)) return RENDER_ERR_DEVICE;
    s->pending_serial=++s->swap_serial; s->present_notice=false; s->present_verified=false;
    s->fence=s->gFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    s->gFlush();
    return s->fence!=NULL && s->gGetError()==GL_NO_ERROR ? RENDER_OK : RENDER_ERR_DEVICE;
}
static int gl_event(render_backend *b,const render_event *event)
{
    gl_state *s=b->state;
    if (event->work==NULL || event->work->kind!=GL_POLL_MESSAGE) return RENDER_ERR_UNSUPPORTED;
    int ring_rc=gl_ring_poll(s);
    if (ring_rc!=RENDER_OK) return ring_rc;
    if (!b->presented) return RENDER_OK;
    /* plat's frozen callback loses event kind; independently verify PIXMAP.
     * Eight nonblocking native polls bound work even on stale event bursts. */
    for (unsigned i=0;i<8 && !s->present_verified;i++) {
        xcb_generic_event_t *native=xcb_poll_for_special_event(s->platform->conn,s->present_special);
        if (native==NULL) break;
        const xcb_present_complete_notify_event_t *ce=(const void *)native;
        if (ce->event_type==XCB_PRESENT_COMPLETE_NOTIFY && ce->kind==XCB_PRESENT_COMPLETE_KIND_PIXMAP &&
            ce->serial==s->pending_serial && ce->window==s->platform->win && ce->msc!=0) {
            s->msc=ce->msc; s->present_verified=true;
        }
        free(native);
    }
    int rc=RENDER_OK;
    if (s->fence!=NULL && !b->device_seen) {
        GLenum status=s->gClientWaitSync(s->fence,0,0); /* exactly one zero-timeout poll */
        if (status==GL_WAIT_FAILED) return RENDER_ERR_DEVICE;
        if (status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED) {
            s->gDeleteSync(s->fence); s->fence=NULL;
            rc=render_backend_signal(b,RENDER_EVENT_DEVICE_DONE,event->frame_id,trace_now_ns());
        }
    }
    if (rc==RENDER_OK && b->device_seen && !b->complete_seen && s->present_notice &&
        s->present_verified && s->notice_msc==s->msc)
        rc=render_backend_signal(b,RENDER_EVENT_PRESENT_COMPLETE,event->frame_id,trace_now_ns());
    return rc;
}
static void gl_shutdown(render_backend *b) { gl_release(b->state); }
int render_gl_backend(render_backend *b)
{
    if (b==NULL) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    *b=(render_backend){.info={"egl",sizeof(gl_state),_Alignof(gl_state),
        RENDER_CAP_GPU|RENDER_CAP_DEVICE_TIMING|RENDER_CAP_PRESENT_TIMING},
        .ops={gl_init,gl_resize,gl_submit,gl_present,gl_event,gl_shutdown}};
    return RENDER_OK;
}
int gl_present_complete(render_backend *b,uint32_t serial,uint64_t ust,uint64_t msc)
{
    (void)ust;
    if (b==NULL || !b->initialized) return RENDER_ERR_STATE;
    gl_state *s=b->state;
    if (!b->active || !b->presented || serial!=s->pending_serial || b->complete_seen) return RENDER_ERR_FRAME;
    if (msc==0) return RENDER_ERR_DEVICE;
    s->notice_msc=msc; s->present_notice=true;
    /* Only a matching PIXMAP notification from our own selection may verify
     * this callback. Defer T6 until the fence is observed too. */
    work_msg msg={.kind=GL_POLL_MESSAGE};
    render_event ev={RENDER_EVENT_WORK,b->active_frame,0,&msg};
    return render_backend_event(b,&ev);
}
const char *gl_buffer_mode(const render_backend *b)
{
    if (b==NULL || !b->initialized) return "uninitialized";
    const gl_state *s=b->state;
    if (s->upload==GL_UPLOAD_SUBDATA) return "subdata";
    if (s->upload==GL_UPLOAD_ORPHAN) return "orphan";
    if (s->upload==GL_UPLOAD_PERSISTENT) return "persistent";
    return s->persistent ? "persistent" : (s->requested_persistent ? "orphan (ARB_buffer_storage unavailable)" : "orphan");
}
const char *gl_device_name(const render_backend *b)
{ return b!=NULL && b->initialized ? ((const gl_state *)b->state)->device_name : "uninitialized"; }
uint64_t gl_displayed_msc(const render_backend *b)
{ return b!=NULL && b->initialized ? ((const gl_state *)b->state)->msc : 0; }
int gl_read_pixels(render_backend *b,uint8_t *rgba,size_t bytes)
{
    if (b==NULL || !b->initialized || rgba==NULL) return RENDER_ERR_ARG;
    gl_state *s=b->state; uint32_t w=s->dims.cols*s->dims.cell_w,h=s->dims.rows*s->dims.cell_h;
    if (bytes<(size_t)w*h*4) return RENDER_ERR_CAPACITY;
    if (!gl_bind(s)) return RENDER_ERR_DEVICE;
    s->gBindFramebuffer(GL_FRAMEBUFFER,s->fbo); s->gPixelStorei(GL_PACK_ALIGNMENT,1);
    s->gReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    if (gl_ring_poll(s)!=RENDER_OK) return RENDER_ERR_DEVICE;
    /* Swap rows in caller storage, bounded stack scratch, no allocation. */
    size_t stride=(size_t)w*4;
    for (uint32_t y=0;y<h/2;y++) for (size_t x=0;x<stride;x++) {
        size_t a=(size_t)y*stride+x, z=(size_t)(h-1-y)*stride+x;
        uint8_t tmp=rgba[a]; rgba[a]=rgba[z]; rgba[z]=tmp;
    }
    return s->gGetError()==GL_NO_ERROR ? RENDER_OK : RENDER_ERR_DEVICE;
}

/* The experimental lease owns GL storage; ordinary caller-owned grids still
 * use the frozen submit-copy guarantee. This API never binds or polls GL. */
int gl_cells_acquire(render_backend *b,render_grid *g,bool preserve)
{
    if (b==NULL || g==NULL || !b->initialized || b->ops.submit!=gl_submit) return RENDER_ERR_ARG;
    gl_state *s=b->state;
    if (s->upload!=GL_UPLOAD_PERSISTENT) return RENDER_ERR_UNSUPPORTED;
    if (b->active || s->leased) return RENDER_ERR_BUSY;
    if (s->ring_poisoned) return RENDER_ERR_DEVICE;
    if (memcmp(&g->dims,&s->dims,sizeof s->dims)!=0) return RENDER_ERR_BOUNDS;
    if (g->cell_capacity<s->cell_bytes/sizeof(render_cell)) return RENDER_ERR_CAPACITY;
    gl_cell_slot *slot=&s->ring[s->ring_next];
    if (slot->fence!=NULL) return RENDER_ERR_BUSY;
    if (preserve && s->ring_has_last && s->ring_last!=s->ring_next)
        memcpy(slot->cells,s->ring[s->ring_last].cells,(size_t)g->dims.cols*g->dims.rows*sizeof(render_cell));
    s->ring_current=s->ring_next; s->leased=true; g->cells=slot->cells;
    return RENDER_OK;
}
int gl_cells_submit(render_backend *b,render_grid *g,const render_strip *strips,size_t count)
{
    if (b==NULL || g==NULL || !b->initialized || b->ops.submit!=gl_submit) return RENDER_ERR_ARG;
    gl_state *s=b->state;
    if (s->upload!=GL_UPLOAD_PERSISTENT || !s->leased || g->cells!=s->ring[s->ring_current].cells)
        return RENDER_ERR_STATE;
    int rc=render_backend_submit(b,g,strips,count);
    if (rc==RENDER_OK) g->cells=NULL; /* revoke writable access on successful transfer */
    return rc;
}
