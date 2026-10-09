/* Standalone startup experiment. Fork/exec happens before any driver threads.
 * Never fork an initialized renderer. Warm servers retain one unmapped window. */
#include "protocol.h"
#include "editor/private.h"
#include "gl/gl.h"
#include "ipc/ipc.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include <xcb/xcb.h>

typedef struct zygote_state {
    editor *e;
    render_backend backend;
    zygote_record record;
    ipc_server server;
    ipc_token token;
    uint32_t first_frame;
    bool warm, pending, stop;
} zygote_state;

static int zygote_write(const zygote_record *r)
{
    const uint8_t *p = (const uint8_t *)r;
    size_t left = sizeof *r;
    while (left) {
        ssize_t n = write(STDOUT_FILENO, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        left -= (size_t)n; p += (size_t)n;
    }
    return 0;
}
void zygote_platform_map(plat *p, void *context)
{
    zygote_state *s = context;
    if (s->warm) return;
    plat_map(p); s->record.map_ns = trace_now_ns();
}
static void zygote_present(void *context, const editor_frame *frame)
{
    zygote_state *s = context;
    if (frame->id == s->first_frame) { s->record.present_ns = frame->present_ns; s->record.present_frame = frame->id; }
}
static void zygote_submit(void *context, const editor_frame *frame)
{
    zygote_state *s = context;
    if (!s->first_frame) s->first_frame = frame->id;
}
static void zygote_device(void *context, uint32_t frame, uint64_t ns)
{
    zygote_state *s = context;
    if (frame != s->first_frame) return;
    raster_metrics metrics;
    s->record.ready_frame = frame;
    s->record.ready_ns = raster_frame_metrics(&s->backend, &metrics) ? metrics.ready_ns : ns;
}
static void zygote_complete(void *context, uint32_t frame, uint64_t ns)
{
    zygote_state *s = context;
    if (frame == s->first_frame) { s->record.complete_ns = ns; s->record.complete_frame = frame; }
}
render_hooks zygote_backend_hooks(void *context)
{
    return (render_hooks){zygote_device, zygote_complete, context};
}
static void zygote_memory(zygote_record *r)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned long long value;
            if (sscanf(line, "VmRSS: %llu kB", &value) == 1) r->rss_kib = (uint64_t)value;
            if (sscanf(line, "VmHWM: %llu kB", &value) == 1) r->hwm_kib = (uint64_t)value;
        }
        fclose(f);
    }
    struct rusage ru;
    if (!getrusage(RUSAGE_SELF, &ru)) {
        r->minor_faults = (uint64_t)ru.ru_minflt;
        r->major_faults = (uint64_t)ru.ru_majflt;
    }
}
static int zygote_map_state(plat *p, uint8_t expected)
{
    xcb_get_window_attributes_reply_t *r = xcb_get_window_attributes_reply(p->conn,
        xcb_get_window_attributes(p->conn, p->win), NULL);
    int ok = r && r->map_state == expected;
    free(r); return ok;
}
static int zygote_open(zygote_state *s)
{
    /* Same embedded ASCII startup and default allocation limits as editor_open.
     * Empty-file startup isolates this bead from corpus/open/index hot paths. */
    editor_config cfg = {.hook_ctx = s, .on_submit = zygote_submit, .on_present = zygote_present};
    int rc;
    if (ZYGOTE_MODE == 1) {
        rc = render_gl_backend(&s->backend);
        if (!rc) rc = editor_open(&s->e, &cfg, &s->backend);
        s->record.gl_result = rc;
        if (!rc) return 0;
        s->backend = (render_backend){0};
    }
    rc = render_cpu_backend(&s->backend);
    if (!rc) rc = editor_open(&s->e, &cfg, &s->backend);
    return rc;
}
static int zygote_frame(zygote_state *s)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(15000000000);
    while ((!s->record.complete_ns || s->backend.active) && trace_now_ns() < deadline) {
        if ((s->backend.info.capabilities & RENDER_CAP_GPU) && s->backend.active && s->backend.presented) {
            work_msg message = {.kind = GL_POLL_MESSAGE};
            render_event event = {RENDER_EVENT_WORK, s->backend.active_frame, 0, &message};
            int status = render_backend_event(&s->backend, &event);
            if (status) return status;
        }
        int rc = editor_step(s->e, 0);
        if (rc < 0 || rc == EDITOR_CLOSED) return rc ? rc : -1;
        if (s->backend.active && !s->backend.presented) {
            struct pollfd fd = {work_pool_eventfd(&s->e->pool), POLLIN, 0};
            (void)poll(&fd, 1, 10);
        }
        /* Completions come through work eventfd for CPU, platform for GL.
         * A short bounded wait avoids monopolizing the shared box. */
        if (s->backend.active && s->backend.presented) {
            struct pollfd fd = {work_pool_eventfd(&s->e->pool), POLLIN, 0};
            (void)poll(&fd, 1, 1);
        }
    }
    if (!s->record.complete_ns || s->backend.active) return -1;
    s->record.mapped_verified = zygote_map_state(&s->e->platform, XCB_MAP_STATE_VIEWABLE);
    xcb_unmap_window(s->e->platform.conn, s->e->platform.win);
    xcb_flush(s->e->platform.conn);
    s->record.unmapped_verified = zygote_map_state(&s->e->platform, XCB_MAP_STATE_UNMAPPED);
    zygote_memory(&s->record);
    return s->record.mapped_verified && s->record.unmapped_verified ? 0 : -1;
}
static ipc_result zygote_request(const ipc_request *r, ipc_token token, void *context)
{
    zygote_state *s = context;
    if (r->count == 1 && strcmp(r->paths[0].path, "/stop") == 0) { s->stop = true; return IPC_OK; }
    if (s->pending || r->count || !r->wait || !s->e) return IPC_REJECTED;
    int gl_rc = s->record.gl_result;
    s->record = (zygote_record){.gl_result = gl_rc};
    s->first_frame = 0;
    if (editor_full_layout(s->e) < 0) return IPC_REJECTED;
    plat_map(&s->e->platform); s->record.map_ns = trace_now_ns();
    s->token = token; s->pending = true;
    return IPC_OK;
}
int main(int argc, char **argv)
{
    const char *d = getenv("DISPLAY"), *ed = getenv("EDIT_DISPLAY");
    if (!d || !ed || strcmp(d, ":99") || strcmp(ed, ":99")) {
        fprintf(stderr, "zygote: DISPLAY and EDIT_DISPLAY must both be :99\n"); return 2;
    }
    if (argc == 3 && (!strcmp(argv[1], "--launch") || !strcmp(argv[1], "--stop"))) {
        ipc_request r = {.cwd = "/", .wait = !strcmp(argv[1], "--launch")};
        if (!r.wait) { r.count = 1; r.paths[0].path = "/stop"; }
        return ipc_client_send(argv[2], &r, 15000) == IPC_OK ? 0 : 1;
    }
    bool serve = argc == 3 && !strcmp(argv[1], "--serve");
    if (!serve && (argc != 2 || strcmp(argv[1], "--normal"))) return 2;
    trace_init(); if (trace_thread_register() < 0) return 1;
    zygote_state *s = calloc(1, sizeof *s); if (!s) return 1;
    s->warm = serve;
    s->record.result = zygote_open(s);
    if (serve && !s->record.result && !zygote_map_state(&s->e->platform, XCB_MAP_STATE_UNMAPPED)) s->record.result = -1;
    if (serve && !s->record.result) s->record.result = (int)ipc_server_init(&s->server, argv[2]);
    zygote_memory(&s->record);
    if (serve || s->record.result) {
        if (zygote_write(&s->record) || s->record.result) { if (s->e) editor_close(s->e); free(s); return 1; }
    }
    if (!serve) {
        s->record.result = zygote_frame(s);
        if (zygote_write(&s->record)) s->record.result = -1;
    } else {
        while (!s->stop && !s->record.result) {
            struct pollfd fd = {ipc_server_fd(&s->server), POLLIN, 0};
            int polled = poll(&fd, 1, -1);
            if (polled < 0 && errno == EINTR) continue;
            if (polled < 0 || ipc_server_drain(&s->server, zygote_request, s) != IPC_OK) { s->record.result = -1; break; }
            if (s->pending) {
                s->record.result = zygote_frame(s);
                if (zygote_write(&s->record) || ipc_server_report_closed(&s->server, s->token) != IPC_OK) { s->record.result = -1; break; }
                (void)ipc_server_drain(&s->server, zygote_request, s);
                s->pending = false;
            }
        }
        /* Flush the stop ACK before closing the endpoint. */
        (void)ipc_server_drain(&s->server, zygote_request, s);
        ipc_server_fini(&s->server);
    }
    int rc = s->record.result;
    if (s->e) editor_close(s->e);
    free(s); return rc ? 1 : 0;
}
