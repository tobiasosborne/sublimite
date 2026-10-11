/* Headless test: real Wayland protocol, explicitly mocked GL device fence.
 * No claim of real EGL/GL hardware coverage. Frame callback must not become T6.
 * Literals derive from consecutive frame IDs, the minimum surface dimensions,
 * and the paired device/presentation hooks in render.h. */
#include "wayland/plat.h"
#include "gl/gl.h"
#include <stdio.h>
#include <assert.h>
#ifdef EDIT_HAVE_WAYLAND_SERVER
#define render_gl_backend wayland_test_factory
#define gl_present_complete wayland_test_present_complete
#define gl_completion_status wayland_test_completion_status
#define gl_buffer_mode wayland_test_buffer_mode
#define gl_device_name wayland_test_device_name
#define gl_displayed_msc wayland_test_displayed_msc
#define gl_read_pixels wayland_test_read_pixels
#define gl_cells_acquire wayland_test_cells_acquire
#define gl_cells_submit wayland_test_cells_submit
#include "../src/gl/gl.c"
#undef render_gl_backend
#undef gl_present_complete
#undef gl_completion_status
#undef gl_buffer_mode
#undef gl_device_name
#undef gl_displayed_msc
#undef gl_read_pixels
#undef gl_cells_acquire
#undef gl_cells_submit
#include <wayland-client.h>
#include <limits.h>
#include "wayland_mock.h"
typedef struct hook_log { uint32_t id; unsigned device, displayed; uint64_t t5, t6; } hook_log;
static void device_done(void *user, uint32_t id, uint64_t ns)
{ hook_log *log = user; assert(log->id == id); log->device++; log->t5 = ns; }
static void displayed(void *user, uint32_t id, uint64_t ns)
{ hook_log *log = user; assert(log->id == id); log->displayed++; log->t6 = ns; }
static EGLBoolean accept_bind(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context)
{ (void)display; (void)draw; (void)read; (void)context; return EGL_TRUE; }
static GLenum fence_ready(GLsync fence, GLbitfield flags, GLuint64 timeout)
{ (void)fence; assert(!flags && !timeout); return GL_ALREADY_SIGNALED; }
static void fence_delete(GLsync fence) { (void)fence; }
static void pending(render_backend *b, gl_state *s, plat *p, hook_log *log)
{
    memset(s, 0, sizeof *s); *b = (render_backend){0};
    assert(wayland_test_factory(b) == RENDER_OK);
    b->state = s; b->initialized = b->active = b->presented = true;
    b->active_frame = log->id; b->submitted_ns = trace_now_ns();
    b->config.hooks = (render_hooks){device_done, displayed, log};
    s->platform = p; s->pending_serial = log->id;
    s->eMakeCurrent = accept_bind; s->gClientWaitSync = fence_ready; s->gDeleteSync = fence_delete;
    s->fence = (GLsync)s; /* opaque token interpreted only by mocked functions */
}
static void gl_protocol_test(void)
{
    pid_t child = mock_start_ex(true, true, NULL, 0);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = -1};
    assert(plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_OK);
    gl_state state; render_backend b; hook_log log = {.id = 1}; pending(&b, &state, &p, &log);
    assert(wayland_frame_arm(&p, log.id) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_roundtrip(p.conn) >= 0);
    assert(wayland_test_present_complete(&b, log.id + 1, UINT64_MAX, UINT64_MAX) == RENDER_ERR_FRAME);
    assert(wayland_test_present_complete(&b, log.id, UINT64_MAX, UINT64_MAX) == RENDER_OK);
    assert(b.active && log.device == 1 && !log.displayed); /* frame hint + fence, no presentation yet */
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_roundtrip(p.conn) >= 0);
    assert(wayland_test_present_complete(&b, log.id, UINT64_MAX, UINT64_MAX) == RENDER_OK);
    assert(!b.active && log.displayed == 1 && log.t6 >= log.t5 && state.msc == 1);
    assert(wayland_test_present_complete(&b, log.id, 0, 0) == RENDER_ERR_FRAME);
    log = (hook_log){.id = log.id + 1}; pending(&b, &state, &p, &log);
    assert(wayland_frame_arm(&p, log.id) == PLAT_OK);
    wl_surface_commit(wayland_surface(&p)); assert(wl_display_roundtrip(p.conn) >= 0);
    assert(wayland_test_present_complete(&b, log.id, 0, 0) == RENDER_ERR_DEVICE);
    assert(!log.displayed); /* discarded surface update never emits T6 */
    plat_shutdown(&p); mock_join(child);
    puts("wayland_gl_test: frame callback is only a hint; mocked fence T5 and matching presentation T6, stale/duplicate/discard checks passed");
}
#endif
int main(void)
{
#ifdef EDIT_HAVE_WAYLAND_SERVER
    gl_protocol_test();
#else
    puts("wayland_gl_test: protocol mock unavailable (optional server headers absent)");
#endif
    return 0;
}
