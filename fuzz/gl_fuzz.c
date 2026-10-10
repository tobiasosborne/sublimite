/* Typed Present/fence state model. No window, GL calls, threads or global
 * fixture state: fuzz the production completion code through its dispatch. */
#include "gl/gl.h"
#include "base/base.h"
#include <xcb/xcb.h>
#include <xcb/present.h>
#include <xcb/xcbext.h>
#include <stdlib.h>
#include <string.h>
typedef struct gl_fuzz_queue {
    xcb_present_complete_notify_event_t event;
    bool pending;
} gl_fuzz_queue;
static xcb_generic_event_t *gl_fuzz_poll(xcb_connection_t *conn,xcb_special_event_t *special)
{
    (void)conn;
    gl_fuzz_queue *q=(void *)special;
    if (!q->pending) return NULL;
    q->pending=false;
    xcb_generic_event_t *event=malloc(sizeof q->event);
    EDIT_ASSERT(event!=NULL);
    memcpy(event,&q->event,sizeof q->event);
    return event;
}
#define xcb_poll_for_special_event gl_fuzz_poll
#include "../src/gl/gl.c"
#undef xcb_poll_for_special_event
static EGLBoolean gl_fuzz_bind(EGLDisplay display,EGLSurface draw,EGLSurface read,EGLContext context)
{ (void)display; (void)draw; (void)read; (void)context; return EGL_TRUE; }
static GLenum gl_fuzz_wait(GLsync sync,GLbitfield flags,GLuint64 timeout)
{
    EDIT_ASSERT(flags==0 && timeout==0);
    uintptr_t state=(uintptr_t)sync;
    return state==1 ? GL_TIMEOUT_EXPIRED : state==2 ? GL_ALREADY_SIGNALED : GL_WAIT_FAILED;
}
static void gl_fuzz_delete(GLsync sync) { (void)sync; }
typedef struct gl_fuzz_hooks { uint32_t frame; unsigned device,complete; uint64_t device_ns; } gl_fuzz_hooks;
static void gl_fuzz_device(void *user,uint32_t frame,uint64_t ns)
{
    gl_fuzz_hooks *hooks=user;
    EDIT_ASSERT(frame==hooks->frame && ns!=0 && hooks->device==0);
    hooks->device++; hooks->device_ns=ns;
}
static void gl_fuzz_complete(void *user,uint32_t frame,uint64_t ns)
{
    gl_fuzz_hooks *hooks=user;
    EDIT_ASSERT(frame==hooks->frame && hooks->device==1 && hooks->complete==0 && ns>=hooks->device_ns);
    hooks->complete++;
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    if (size<3) return 0;
    gl_state s={0}; render_backend b={0}; gl_fuzz_queue q={0}; plat platform={.win=44,.height=8};
    EDIT_ASSERT(render_gl_backend(&b)==RENDER_OK);
    b.state=&s; b.initialized=true; b.active=true; b.presented=true;
    b.active_frame=1u+data[0]; b.submitted_ns=1;
    gl_fuzz_hooks hooks={.frame=b.active_frame};
    b.config.hooks=(render_hooks){gl_fuzz_device,gl_fuzz_complete,&hooks};
    s.pending_serial=3u+data[1]; s.native_win=44; s.platform=&platform;
    s.eMakeCurrent=gl_fuzz_bind; s.gClientWaitSync=gl_fuzz_wait; s.gDeleteSync=gl_fuzz_delete;
    s.fence=(void *)(uintptr_t)1; s.present_special=(void *)&q;
    bool verified=false,failed=false; uint64_t authoritative_msc=0;
    size_t limit=size<128 ? size : 128;
    for (size_t i=2;i<limit;i++) {
        uint8_t op=data[i];
        uint8_t mode=(uint8_t)((op>>3)%5u);
        uint32_t notice_serial=s.pending_serial+((op&1u) ? 0u : 1u);
        uint64_t notice_msc=UINT64_C(1000)+op;
        q.event=(xcb_present_complete_notify_event_t){.event_type=XCB_PRESENT_COMPLETE_NOTIFY,
            .kind=(op&2u) ? XCB_PRESENT_COMPLETE_KIND_PIXMAP : XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC,
            .mode=mode,.serial=s.pending_serial+((op&4u) ? 0u : 1u),
            .window=(op&64u) ? 44u : 45u,.msc=(op&128u) ? UINT64_C(100)+op : 0};
        q.pending=true;
        uintptr_t fence_state=1u+(uintptr_t)(op%3u);
        if (!b.device_seen) s.fence=(void *)fence_state;
        bool active=b.active;
        if (active && !failed && notice_serial==s.pending_serial) {
            if (!verified && q.event.kind==XCB_PRESENT_COMPLETE_KIND_PIXMAP &&
                q.event.serial==s.pending_serial && q.event.window==44) {
                if (q.event.msc==0 || !gl_display_mode(mode)) failed=true;
                else { verified=true; authoritative_msc=q.event.msc; }
            }
            if (!failed && !b.device_seen && fence_state==3) failed=true;
        }
        int rc=gl_present_complete(&b,notice_serial,0,notice_msc);
        EDIT_ASSERT(rc==(!active || notice_serial!=s.pending_serial ? RENDER_ERR_FRAME :
            failed ? RENDER_ERR_DEVICE : RENDER_OK));
        EDIT_ASSERT(s.failed==failed && s.present_verified==verified && s.msc==authoritative_msc);
        EDIT_ASSERT(hooks.complete==(b.t6_sent ? 1u : 0u));
        EDIT_ASSERT(!b.t6_sent || (verified && b.device_seen && !failed));
        EDIT_ASSERT(b.active || (hooks.device==1 && hooks.complete==1));
    }
    return 0;
}
