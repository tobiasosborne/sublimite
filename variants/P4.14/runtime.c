/* Standalone startup experiment. Fork/exec happens before any driver threads.
 * Never fork an initialized renderer. Warm servers retain one unmapped window. */
#include "protocol.h"
#include "editor/private.h"
#include "font/font.h"
#include "gl/gl.h"
#include "ipc/ipc.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <errno.h>
#include <dlfcn.h>
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
    bool warm, pending, stop, real;
    void *gl_library;
    const unsigned char *(*get_string)(unsigned int);
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
    /* Native Present probing needs a viewable window. This initial map belongs
     * to service startup, before the listener; the server is hidden afterwards. */
    if (s->warm && !(s->real && (s->backend.info.capabilities & RENDER_CAP_GPU))) return;
    plat_map(p); s->record.map_ns = trace_now_ns();
}
static void zygote_present(void *context, const editor_frame *frame)
{
    zygote_state *s = context;
    if (frame->id != s->first_frame) return;
    s->record.present_ns = frame->present_ns; s->record.present_frame = frame->id;
    /* Present has bound the context to UI; submit itself makes no native calls.
     * Query once per process, outside input->submit. Loader work was done at open. */
    if (s->record.native_gl && !s->record.driver[0] && s->get_string) {
        const char *version = (const char *)s->get_string(0x1f02u); /* GL_VERSION */
        const char *vendor = (const char *)s->get_string(0x1f00u); /* GL_VENDOR */
        if (version && vendor) (void)snprintf(s->record.driver, sizeof s->record.driver,
                                             "%s; %s", vendor, version);
    }
}
static void zygote_submit(void *context, const editor_frame *frame)
{
    zygote_state *s = context;
    if (!s->first_frame) {
        s->first_frame = frame->id;
        if (s->record.native_gl) {
            s->record.ready_ns = frame->submit_ns;
            s->record.ready_frame = frame->id;
        }
    }
}
static void zygote_device(void *context, uint32_t frame, uint64_t ns)
{
    zygote_state *s = context;
    if (frame != s->first_frame) return;
    raster_metrics metrics;
    s->record.device_frame = frame; s->record.device_ns = ns;
    if (!s->record.native_gl && raster_frame_metrics(&s->backend, &metrics)) {
        s->record.ready_frame = frame;
        s->record.ready_ns = metrics.ready_ns;
    }
}
static void zygote_complete(void *context, uint32_t frame, uint64_t ns)
{
    zygote_state *s = context;
    if (frame == s->first_frame) {
        s->record.complete_ns = ns; s->record.complete_frame = frame;
        if (s->record.native_gl) s->record.displayed_msc = gl_displayed_msc(&s->backend);
    }
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
static int zygote_panel_config(editor_config *cfg)
{
    /* Use the selected display's root size rather than a hard-coded laptop
     * resolution. The setup connection is open-time work, never input->submit. */
    int screen_index = 0;
    xcb_connection_t *connection = xcb_connect(NULL, &screen_index);
    if (!connection || xcb_connection_has_error(connection)) {
        if (connection) xcb_disconnect(connection);
        return EDITOR_ERR_IO;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
    while (screen_index > 0 && screens.rem) { xcb_screen_next(&screens); screen_index--; }
    const font_ascii_atlas *atlas = font_ascii_atlas_for_px(15);
    int rc = EDITOR_ERR_ARG;
    if (screens.rem && atlas) {
        cfg->cols = screens.data->width_in_pixels / atlas->cell.cell_w;
        cfg->rows = screens.data->height_in_pixels / atlas->cell.cell_h;
        if (cfg->cols && cfg->rows && cfg->cols <= 8192 && cfg->rows <= 8192) rc = 0;
    }
    xcb_disconnect(connection);
    return rc;
}
static int zygote_open(zygote_state *s)
{
    /* Same embedded ASCII startup and default allocation limits as editor_open.
     * Empty-file startup isolates this bead from corpus/open/index hot paths. */
    editor_config cfg = {.hook_ctx = s, .on_submit = zygote_submit, .on_present = zygote_present};
    if (s->real) {
        int panel_rc = zygote_panel_config(&cfg);
        if (panel_rc) return panel_rc;
    }
    int rc;
    if (ZYGOTE_MODE == 1) {
        rc = render_gl_backend(&s->backend);
        if (!rc) rc = editor_open(&s->e, &cfg, &s->backend);
        s->record.gl_result = rc;
        if (!rc) {
            s->record.native_gl = true;
            (void)snprintf(s->record.device, sizeof s->record.device, "%s", gl_device_name(&s->backend));
            const char *library = getenv("EDIT_GL_GL_LIBRARY");
            s->gl_library = dlopen(library ? library : "libGL.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
            if (!s->gl_library) return RENDER_ERR_INIT;
            void *symbol = dlsym(s->gl_library, "glGetString");
            _Static_assert(sizeof symbol == sizeof s->get_string, "POSIX function pointer size");
            memcpy(&s->get_string, &symbol, sizeof symbol);
            return s->get_string ? 0 : RENDER_ERR_INIT;
        }
        /* Xvfb smoke keeps the old probe/fallback path explicit. Authorized
         * real-display rows must fail rather than silently measure CPU. */
        if (s->real) return rc;
        s->backend = (render_backend){0};
    }
    rc = render_cpu_backend(&s->backend);
    if (!rc) rc = editor_open(&s->e, &cfg, &s->backend);
    (void)snprintf(s->record.device, sizeof s->record.device, "CPU raster");
    (void)snprintf(s->record.driver, sizeof s->record.driver, "XShm/Present");
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
    s->record.cols = s->e->grid.dims.cols; s->record.rows = s->e->grid.dims.rows;
    s->record.width = s->record.cols * s->e->grid.dims.cell_w;
    s->record.height = s->record.rows * s->e->grid.dims.cell_h;
    zygote_memory(&s->record);
    return s->record.mapped_verified && s->record.unmapped_verified ? 0 : -1;
}
static ipc_result zygote_request(const ipc_request *r, ipc_token token, void *context)
{
    zygote_state *s = context;
    if (r->count == 1 && strcmp(r->paths[0].path, "/stop") == 0) { s->stop = true; return IPC_OK; }
    if (s->pending || r->count || !r->wait || !s->e) return IPC_REJECTED;
    zygote_record identity = s->record;
    s->record = (zygote_record){.gl_result = identity.gl_result, .native_gl = identity.native_gl};
    memcpy(s->record.device, identity.device, sizeof identity.device);
    memcpy(s->record.driver, identity.driver, sizeof identity.driver);
    s->first_frame = 0;
    if (editor_full_layout(s->e) < 0) return IPC_REJECTED;
    plat_map(&s->e->platform); s->record.map_ns = trace_now_ns();
    s->token = token; s->pending = true;
    return IPC_OK;
}
int main(int argc, char **argv)
{
    bool real = argc > 1 && !strcmp(argv[argc - 1], "--real-display");
    if (real) argc--;
    if (!zygote_display_check(real)) {
        fprintf(stderr, "zygote: display refused (real display needs --real-display, EDIT_ALLOW_REAL_DISPLAY=1 and matching DISPLAY/EDIT_DISPLAY)\n"); return 2;
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
    s->warm = serve; s->real = real;
    uint64_t init_start = trace_now_ns();
    s->record.result = zygote_open(s);
    if (serve && !s->record.result) {
        xcb_unmap_window(s->e->platform.conn, s->e->platform.win);
        xcb_flush(s->e->platform.conn);
    }
    if (serve && !s->record.result && !zygote_map_state(&s->e->platform, XCB_MAP_STATE_UNMAPPED)) s->record.result = -1;
    if (serve && !s->record.result) s->record.result = (int)ipc_server_init(&s->server, argv[2]);
    s->record.init_ns = trace_now_ns() - init_start;
    zygote_memory(&s->record);
    if (serve || s->record.result) {
        if (zygote_write(&s->record) || s->record.result) {
            if (s->e) editor_close(s->e);
            if (s->gl_library) (void)dlclose(s->gl_library);
            free(s); return 1;
        }
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
    if (s->gl_library) (void)dlclose(s->gl_library);
    free(s); return rc ? 1 : 0;
}
