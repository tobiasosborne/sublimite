/* Internal shared real-window driver for tests/bench; no renderer shortcuts. */
#ifndef EDIT_GL_DRIVER_H
#define EDIT_GL_DRIVER_H
#include "gl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "work/work.h"
#include "x11/plat.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct gl_driver {
    plat platform;
    work_pool workers;
    edit_arena state_arena;
    render_backend *backend;
    render_config config;
    void *state;
    size_t state_bytes;
    int init_result;
    bool pool_live, plat_live, arena_live, init_done;
    uint64_t init_ns;
} gl_driver;
static void gl_driver_init_job(work_ctx *c)
{
    gl_driver *d = c->arg;
    uint64_t start = trace_now_ns();
    int result = render_backend_init(d->backend, &d->config, d->state, d->state_bytes);
    uint64_t elapsed = trace_now_ns() - start;
    work_msg msg = {.kind = GL_POLL_MESSAGE};
    memcpy(msg.data, &result, sizeof result);
    memcpy(msg.data + sizeof result, &elapsed, sizeof elapsed);
    (void)work_publish(c, &msg);
}
static void gl_driver_init_message(const work_msg *m, void *u)
{
    gl_driver *d = u;
    memcpy(&d->init_result, m->data, sizeof d->init_result);
    memcpy(&d->init_ns, m->data + sizeof d->init_result, sizeof d->init_ns);
    d->init_done = true;
}
static void gl_driver_complete(void *u, uint32_t serial, uint64_t ust, uint64_t msc)
{
    gl_driver *d = u;
    if (d->backend->initialized) (void)gl_present_complete(d->backend, serial, ust, msc);
}
static void gl_driver_event(void *u, const plat_event *ev) { (void)u; (void)ev; }
static int gl_driver_pump(gl_driver *d)
{
    render_backend *b = d->backend;
    if (b->active && b->presented && (!b->device_seen || !b->complete_seen)) {
        work_msg msg = {.kind = GL_POLL_MESSAGE};
        render_event ev = {RENDER_EVENT_WORK, b->active_frame, 0, &msg};
        int rc = render_backend_event(b, &ev);
        if (rc != RENDER_OK) return rc;
    }
    plat_callbacks cb = {.ud = d, .on_event = gl_driver_event, .on_present_complete = gl_driver_complete};
    return plat_run_for(&d->platform, &cb, 0) == PLAT_OK ? RENDER_OK : RENDER_ERR_DEVICE;
}
static void gl_driver_cleanup(gl_driver *d)
{
    if (d->pool_live) work_pool_shutdown(&d->workers);
    if (d->backend != NULL) render_backend_shutdown(d->backend);
    if (d->plat_live) plat_shutdown(&d->platform);
    if (d->arena_live) edit_arena_free(&d->state_arena);
    memset(d, 0, sizeof *d);
}
static int gl_driver_prepare(gl_driver *d, render_backend *b, render_config *cfg)
{
    memset(d, 0, sizeof *d); d->backend = b;
    int rc = render_gl_backend(b);
    if (rc != RENDER_OK) return rc;
    plat_config pc = {.title = "editor EGL test/bench", .width = cfg->dims.cols * cfg->dims.cell_w,
        .height = cfg->dims.rows * cfg->dims.cell_h, .work_eventfd = -1};
    if (plat_init(&d->platform, &pc) != PLAT_OK) return RENDER_ERR_UNSUPPORTED;
    d->plat_live = true; plat_map(&d->platform);
    if (work_pool_init(&d->workers, 1, 0) != 0) return RENDER_ERR_INIT;
    d->pool_live = true;
    cfg->platform = &d->platform; cfg->workers = &d->workers;
    d->config = *cfg;
    return RENDER_OK;
}
static int gl_driver_init(gl_driver *d)
{
    render_backend_info info;
    if (render_backend_query(d->backend, &info) != RENDER_OK) return RENDER_ERR_INIT;
    if (edit_arena_init(&d->state_arena, info.state_size + info.state_align) != 0) return RENDER_ERR_INIT;
    d->arena_live = true;
    d->state = edit_arena_alloc(&d->state_arena, info.state_size, info.state_align);
    d->state_bytes = info.state_size;
    work_handle h = work_submit(&d->workers, (work_job){gl_driver_init_job, d, 1, WORK_BULK});
    if (h.epoch == 0) return RENDER_ERR_INIT;
    uint64_t deadline = trace_now_ns() + UINT64_C(10000000000);
    while (!d->init_done && trace_now_ns() < deadline) {
        (void)work_mailbox_drain(&d->workers, gl_driver_init_message, d);
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
    return d->init_done ? d->init_result : RENDER_ERR_INIT;
}
static int gl_driver_finish(gl_driver *d, uint32_t id)
{
    int rc = render_backend_present(d->backend, id);
    if (rc != RENDER_OK) return rc;
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (d->backend->active) {
        rc = gl_driver_pump(d);
        if (rc != RENDER_OK) return rc;
        if (trace_now_ns() > deadline) return RENDER_ERR_DEVICE;
    }
    return RENDER_OK;
}
#endif
