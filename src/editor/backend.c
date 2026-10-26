#include "editor/private.h"
#include "raster/raster.h"
#if defined(_WIN32)
#include "d3d11/d3d11.h"
#else
#include "gl/gl.h"
#endif
#include "trace/trace.h"
#include <string.h>
#include <stdio.h>

int editor_backend_select(render_backend *backend, const char *name)
{
#if defined(_WIN32)
    if (!name || !strcmp(name, "d3d11")) return render_d3d11_backend(backend);
#else
    if (!name || !strcmp(name, "gl")) return render_gl_backend(backend);
    if (!strcmp(name, "raster")) return render_cpu_backend(backend);
#endif
    return EDITOR_ERR_ARG;
}

/* Existing message tag, a discriminator rather than a numeric capacity. */
#define EDITOR_INIT_MSG UINT32_C(0x4544494e)
static void log_probe_failure(int rc)
{
    const char *reason = rc == RENDER_ERR_UNSUPPORTED ? "EGL/GL/Present unavailable" :
        rc == RENDER_ERR_INIT ? "EGL/GL initialization failed" :
        rc == RENDER_ERR_ARG ? "invalid GL configuration" : "backend initialization error";
    fprintf(stderr, "sublimite: EGL init failed: %s (code=%d); using raster; no retry\n", reason, rc);
}
static void init_worker(work_ctx *job)
{
    editor_backend_init *a = job->arg;
    bool gpu = (a->backend.info.capabilities & RENDER_CAP_GPU) != 0;
    if (gpu && !a->finishing) trace_record(TRACE_STARTUP_EGL_PROBE_START, 0);
    int result = work_should_stop(job) ? RENDER_ERR_INIT : a->finishing ?
        a->backend.finish_init(&a->backend) :
        render_backend_init(&a->backend, &a->config, a->state, a->backend.info.state_size);
    bool needs_finish = a->pending && a->backend.finish_init != NULL && !a->finishing && !result;
    if (gpu && !needs_finish) trace_record(TRACE_STARTUP_EGL_PROBE_END, 0);
    if (gpu && a->pending && result) log_probe_failure(result);
    work_msg msg = {.kind = EDITOR_INIT_MSG};
    memcpy(msg.data, &result, sizeof result);
    /* This dedicated job has an empty mailbox and publishes only its result. */
    (void)work_publish(job, &msg);
}
static void init_result(const work_msg *msg, void *ctx)
{
    editor_backend_init *a = ctx;
    if (msg->kind != EDITOR_INIT_MSG) return;
    memcpy(&a->result, msg->data, sizeof a->result); a->received = true;
}
#if defined(_WIN32)
static void init_event(void *ctx, const plat_event *event)
{
    editor *e = ctx;
    if (event->kind == PLAT_EV_CLOSE) e->quit = true;
    else if (e->buffer) (void)editor_inject(e, event);
    else if (e->queue_count < EDITOR_INPUT_CAP) {
        size_t at = (e->queue_head + e->queue_count++) % EDITOR_INPUT_CAP;
        e->queue[at] = *event; e->queue_serial[at] = ++e->event_serial;
    }
}
#endif
int editor_backend_init_sync(editor *e, render_config config, void *state)
{
    editor_backend_init arg = {.backend = *e->backend, .config = config, .state = state, .result = RENDER_ERR_INIT};
#if defined(_WIN32)
    if (!strcmp(arg.backend.info.name, "d3d11")) {
        memset(state, 0, arg.backend.info.state_size); arg.backend.state = state;
        int attached = render_d3d11_backend_for_window(&arg.backend, plat_native_win32(&e->platform));
        if (attached) return attached;
    }
#endif
    work_handle handle = work_submit(&e->pool, (work_job){init_worker, &arg, 0, WORK_BULK});
    if (!handle.epoch) return EDITOR_ERR_MEMORY;
    /* Only strict GPU selection and bootstrap init use this synchronous path.
     * The requested fallback-capable GPU never occupies this pool. */
    while (!arg.received || !work_handle_finished(&e->pool, handle)) {
        (void)work_mailbox_receive(&e->pool, handle, 0, init_result, &arg);
        if (arg.received && work_handle_finished(&e->pool, handle)) break;
#if defined(_WIN32)
        if (e->has_platform) {
            plat_callbacks cb = {.ud = e, .on_event = init_event};
            e->platform.quit = false; (void)plat_run_for(&e->platform, &cb, 1);
        } else
#endif
        (void)os_wake_wait(work_pool_wake(&e->pool), INT64_C(1000000)); /* same quiescent cadence */
    }
    *e->backend = arg.backend;
    return arg.result;
}
#if defined(_WIN32)
static void shutdown_worker(work_ctx *job) { render_backend_shutdown(job->arg); }
#endif
void editor_backend_shutdown_sync(editor *e, render_backend *backend)
{
#if defined(_WIN32)
    if (!backend || !backend->initialized) return;
    if (e->pool_ready && e->has_platform) {
        work_handle handle = {0};
        do {
            if (!handle.epoch) handle = work_submit(&e->pool, (work_job){shutdown_worker, backend, 0, WORK_BULK});
            if (handle.epoch && work_handle_finished(&e->pool, handle)) break;
            plat_callbacks cb = {.ud = e, .on_event = init_event};
            e->platform.quit = false; (void)plat_run_for(&e->platform, &cb, 1);
        } while (true);
        return;
    }
#else
    (void)e;
#endif
    render_backend_shutdown(backend);
}
int editor_backend_start_probe(editor *e, render_backend backend, render_config config, void *state)
{
    /* Derived topology: discovery needs its own bulk lane because the ordinary
     * bulk lane owns file-prefix I/O; no raster jobs run on the discovery pool. */
    if (work_pool_init(&e->startup_pool, 1, 0)) return EDITOR_ERR_MEMORY;
    e->startup_pool_ready = true;
    if (editor_wait_add(&e->poll_set, work_pool_wake(&e->startup_pool), EDITOR_WAIT_STARTUP) != OS_OK)
        return EDITOR_ERR_IO;
    config.background_init = true;
    e->startup = (editor_backend_init){.backend = backend, .config = config, .state = state,
        .result = RENDER_ERR_INIT, .pending = true};
    e->startup.handle = work_submit(&e->startup_pool, (work_job){init_worker, &e->startup, 0, WORK_BULK});
    return e->startup.handle.epoch ? 0 : EDITOR_ERR_MEMORY;
}
static void probe_failure(editor *e)
{
    int rc = e->startup.result;
    e->stats.backend_init_error = rc;
    e->startup.pending = false;
}
int editor_backend_poll_probe(editor *e)
{
    editor_backend_init *a = &e->startup;
    if (!e->startup_pool_ready) return 0;
    /* Publication exposes the payload before writing eventfd. Even after
     * final selection, drain a late notification so epoll cannot spin at idle.
     * This pool has no subsequent jobs or foreign messages. */
    (void)work_mailbox_receive(&e->startup_pool, a->handle, 0, init_result, a);
    if (!a->pending) return 0;
    if (!a->received) return 0;
    if (a->result) { probe_failure(e); return 0; }
    /* No descriptor/state access before the worker physically returns. Never
     * join it, or the raster jobs, on a turn that may be serving input. */
    if (!work_handle_finished(&e->startup_pool, a->handle) || !e->startup_presented || e->backend->active) return 0;
    if (a->backend.finish_init != NULL && !a->finishing) {
        /* The first raster serial has retired before GL subscribes. Later
         * raster serials cannot collide with GL's first swap serial. Keep
         * native subscription/round trips on the discovery worker as well. */
        a->finishing = true; a->received = false; a->result = RENDER_ERR_INIT;
        a->handle = work_submit(&e->startup_pool, (work_job){init_worker, a, 0, WORK_BULK});
        return a->handle.epoch ? 0 : EDITOR_ERR_MEMORY;
    }
    int rc = render_backend_resize(&a->backend, e->grid.dims);
    if (rc) return rc;
    if (e->raster_poll_fd >= 0) {
        if (editor_wait_remove(&e->poll_set, EDITOR_WAIT_RASTER) != OS_OK) return EDITOR_ERR_IO;
        e->raster_poll_fd = -1;
    }
    e->retired_backend = *e->backend;
    *e->backend = a->backend;
    memset(&a->backend, 0, sizeof a->backend); /* exclusive ownership moved to UI */
    a->pending = false;
    return editor_full_layout(e);
}
void editor_backend_close_probe(editor *e)
{
    if (!e->startup_pool_ready) return;
    /* Explicit close is quiescent. Keep arguments, platform and arenas alive
     * through an external driver call even if it ignores cancellation. */
    while (e->startup.handle.epoch && (!e->startup.received ||
           !work_handle_finished(&e->startup_pool, e->startup.handle))) {
        (void)work_mailbox_receive(&e->startup_pool, e->startup.handle, 0, init_result, &e->startup);
        if (e->startup.received && work_handle_finished(&e->startup_pool, e->startup.handle)) break;
        (void)os_wake_wait(work_pool_wake(&e->startup_pool), INT64_C(1000000));
    }
    work_pool_shutdown(&e->startup_pool);
    if (e->startup.pending && e->startup.received && e->startup.result) probe_failure(e);
    editor_backend_shutdown_sync(e, &e->startup.backend);
}
