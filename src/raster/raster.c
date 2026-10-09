/* P2.5: backend-owned cell snapshots, MT4 WORK_RASTER strips, XShm staging
 * into a retained off-screen pixmap. present submits that pixmap exactly once.
 * T5: TriggerFence + AwaitFence + QueryFence after the upload/presentation.
 * T6: matching serial/kind PresentCompleteNotify, with pixmap idle before reuse.
 * Workers publish all timing/completion state through the work mailboxes. */
#include "raster/raster.h"
#include "base/base.h"
#include "trace/trace.h"
#include "work/work.h"
#include "x11/plat.h"
#include <poll.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/present.h>
#include <xcb/shm.h>
#include <xcb/sync.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

enum { MSG_STRIPS = 1, MSG_FENCE = 2, MSG_PRESENT = 3, MSG_FAIL = 4 };
typedef struct msg_payload { uint64_t ns, ust, msc; int32_t status; } msg_payload;
_Static_assert(sizeof(msg_payload) <= WORK_MSG_DATA, "raster message fits mailbox");

struct cpu_state;
typedef struct strip_job { struct cpu_state *st; uint32_t index, njobs; raster_palette palette; } strip_job;

typedef struct cpu_state {
    xcb_connection_t *conn;
    xcb_window_t win;
    xcb_gcontext_t gc;
    xcb_pixmap_t pixmap;
    xcb_sync_fence_t device_fence;
    xcb_shm_seg_t seg;
    xcb_special_event_t *special;
    int shmid;
    uint32_t *pix;
    size_t stride_px;
    uint32_t max_w, max_h, alpha_or;
    uint8_t depth;
    struct work_pool *pool;
    bool up;
    /* backend-owned snapshot */
    render_cell *cells;
    render_glyph *glyphs;
    render_atlas_page *pages;
    render_strip *strips;
    size_t max_cells, max_glyphs, max_pages, max_strips;
    raster_scene scene;
    size_t nstrips;
    uint32_t total_rows, frame_id;
    bool have_frame;
    /* jobs */
    strip_job jobs[RASTER_JOBS];
    work_handle handles[RASTER_JOBS + 1u];
    size_t nhandles;
    uint32_t pending; /* UI only: decremented by strip work messages */
    /* fence job */
    _Atomic uint32_t armed;
    unsigned int fence_seq;
    uint64_t last_ust, last_msc;   /* UI-thread copy of the latest Present completion */
    uint32_t fence_frame;
    raster_metrics metrics;
} cpu_state;

static void post(work_ctx *c, uint32_t kind, uint64_t ns, int32_t status, uint64_t ust, uint64_t msc)
{
    work_msg m = {0};
    msg_payload p = {.ns = ns, .ust = ust, .msc = msc, .status = status};
    m.kind = kind; m.generation = c->generation;
    memcpy(m.data, &p, sizeof p);
    (void)work_publish(c, &m);
}

typedef struct row_ctx { cpu_state *st; work_ctx *work; raster_palette *palette; } row_ctx;
static void do_row(void *u, uint32_t row)
{
    row_ctx *ctx = u;
    if (work_should_stop(ctx->work)) return;
    cpu_state *st = ctx->st;
    raster_row_cached(&st->scene, st->pix + (size_t)row * st->scene.dims.cell_h * st->stride_px,
                    st->stride_px, row, ctx->palette);
}

static void strip_job_fn(work_ctx *c)
{
    strip_job *j = c->arg;
    cpu_state *st = j->st;
    uint32_t lo, hi;
    raster_partition(st->total_rows, j->njobs, j->index, &lo, &hi);
    uint64_t start = trace_now_ns();
    j->palette.rebuilds = 0;
    row_ctx rows = {st, c, &j->palette};
    raster_for_rows(st->strips, st->nstrips, lo, hi, do_row, &rows);
    uint64_t end = trace_now_ns();
    if (work_should_stop(c)) return;
    work_msg m = {.kind = MSG_STRIPS, .generation = c->generation};
    msg_payload payload = {.ns = end, .ust = start, .msc = j->index};
    memcpy(m.data, &payload, sizeof payload);
    (void)work_publish(c, &m);
}

/* UI only. A failed submit cancels its jobs before returning and their stale
 * messages are dropped by work; the retained pixmap is never touched. */
static void upload_strip(cpu_state *st, uint32_t index)
{
    /* Stage only off-screen pixels. Disjoint job row runs allow the server's
     * upload to overlap later raster jobs, without exposing rows before T4. */
    uint32_t lo, hi;
    raster_partition(st->total_rows, st->metrics.jobs, index, &lo, &hi);
    const render_dims d = st->scene.dims;
    uint32_t ord = 0;
    for (size_t i = 0; i < st->nstrips && ord < hi; i++) {
        uint32_t n = st->strips[i].row_count, next = ord + n;
        if (next > lo) {
            uint32_t first = st->strips[i].first_row + (lo > ord ? lo - ord : 0);
            uint32_t count = (hi < next ? hi : next) - (lo > ord ? lo : ord);
            uint16_t y = (uint16_t)(first * d.cell_h);
            xcb_shm_put_image(st->conn, st->pixmap, st->gc, (uint16_t)st->max_w, (uint16_t)st->max_h,
                0, y, (uint16_t)(d.cols * d.cell_w), (uint16_t)(count * d.cell_h), 0, (int16_t)y,
                st->depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, st->seg, 0);
        }
        ord = next;
    }
    xcb_flush(st->conn);
}

static void fence_job_fn(work_ctx *c)
{
    cpu_state *st = c->arg;
    int fd = xcb_get_file_descriptor(st->conn);
    uint64_t deadline = trace_now_ns() + UINT64_C(2000000000);
    while (!atomic_load_explicit(&st->armed, memory_order_acquire)) {
        if (work_should_stop(c)) return;
        struct timespec ts = {0, 20000};
        nanosleep(&ts, NULL);
    }
    const uint32_t frame = st->fence_frame;
    const unsigned int seq = st->fence_seq;
    int32_t status = 0;
    uint64_t t5 = 0, t6 = 0, ust = 0, msc = 0;
    uint32_t present_info = 0;
    bool got_fence = false, got_present = false, got_idle = false;
    for (;;) {
        if (work_should_stop(c)) return;
        bool progressed = false;
        if (!got_fence) {
            void *reply = NULL; xcb_generic_error_t *err = NULL;
            if (xcb_poll_for_reply(st->conn, seq, &reply, &err)) {
                t5 = trace_now_ns(); got_fence = true; progressed = true;
                if (err || !reply || !((xcb_sync_query_fence_reply_t *)reply)->triggered) status = -1;
                free(reply); free(err);
                post(c, MSG_FENCE, t5, status, 0, 0);
            }
        }
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_special_event(st->conn, st->special)) != NULL) {
            progressed = true;
            const xcb_present_generic_event_t *ge = (const xcb_present_generic_event_t *)e;
            if (ge->evtype == XCB_PRESENT_COMPLETE_NOTIFY) {
                const xcb_present_complete_notify_event_t *pe = (const xcb_present_complete_notify_event_t *)e;
                if (pe->serial == frame && pe->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                    t6 = trace_now_ns(); ust = pe->ust; msc = pe->msc;
                    present_info = (uint32_t)pe->kind * 256u + pe->mode;
                    got_present = true;
                }
            } else if (ge->evtype == XCB_PRESENT_IDLE_NOTIFY) {
                const xcb_present_idle_notify_event_t *ie = (const xcb_present_idle_notify_event_t *)e;
                if (ie->serial == frame && ie->pixmap == st->pixmap) got_idle = true;
            }
            free(e);
        }
        while ((e = xcb_poll_for_event(st->conn)) != NULL) {
            progressed = true;
            if (e->response_type == 0) status = -1;
            free(e);
        }
        if (status != 0 || xcb_connection_has_error(st->conn) || trace_now_ns() > deadline) {
            post(c, MSG_FAIL, 0, -1, 0, 0); return;
        }
        if (got_fence && got_present && got_idle) {
            /* Last access to state before publishing: UI may start N+1 as soon
             * as it drains this message. Never discard an early Present event. */
            post(c, MSG_PRESENT, t6 > t5 ? t6 : t5, (int32_t)present_info, ust, msc);
            return;
        }
        /* Event polling may have read the fence reply into XCB's reply queue
         * while draining the fd. Check that queue again before sleeping. */
        if (progressed) continue;
        struct pollfd pfd = {fd, POLLIN, 0};
        (void)poll(&pfd, 1, 1);
    }
}

/* ---- ops ---- */
static void cpu_release(cpu_state *st)
{
    if (st->special) xcb_unregister_for_special_event(st->conn, st->special);
    if (st->conn) {
        if (st->seg) xcb_shm_detach(st->conn, st->seg);
        if (st->device_fence) xcb_sync_destroy_fence(st->conn, st->device_fence);
        if (st->gc) xcb_free_gc(st->conn, st->gc);
        if (st->pixmap) xcb_free_pixmap(st->conn, st->pixmap);
        xcb_flush(st->conn);
        xcb_disconnect(st->conn);
    }
    if (st->pix) shmdt(st->pix);
    if (st->shmid >= 0) shmctl(st->shmid, IPC_RMID, NULL);
    free(st->cells); free(st->glyphs); free(st->pages); free(st->strips);
    memset(st, 0, sizeof *st);
    st->shmid = -1;
}

static int cpu_init(render_backend *b, const render_config *cfg)
{
    cpu_state *st = b->state;
    memset(st, 0, sizeof *st);
    st->shmid = -1;
    if (cfg->platform == NULL || cfg->workers == NULL || cfg->platform->conn == NULL)
        return RENDER_ERR_INIT;
    if (cfg->max_width > UINT16_MAX || cfg->max_height > INT16_MAX ||
        cfg->workers->n_workers <= cfg->workers->n_bulk) return RENDER_ERR_UNSUPPORTED;
    const plat *pl = cfg->platform;
    st->win = pl->win; st->depth = pl->depth; st->pool = cfg->workers;
    st->alpha_or = pl->depth == 32 ? 0xff000000u : 0u;
    st->max_w = cfg->max_width; st->max_h = cfg->max_height;
    st->stride_px = cfg->max_width;
    int scr = 0;
    st->conn = xcb_connect(NULL, &scr);
    if (st->conn == NULL || xcb_connection_has_error(st->conn)) {
        if (st->conn) xcb_disconnect(st->conn);
        st->conn = NULL; cpu_release(st); return RENDER_ERR_INIT;
    }
    int rc = RENDER_ERR_UNSUPPORTED;
    /* 32 bpp little-endian ZPixmap at the window depth */
    const xcb_setup_t *setup = xcb_get_setup(st->conn);
    bool fmt_ok = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST;
    bool depth_ok = false;
    for (xcb_format_iterator_t it = xcb_setup_pixmap_formats_iterator(setup); it.rem; xcb_format_next(&it))
        if (it.data->depth == st->depth && it.data->bits_per_pixel == 32) depth_ok = true;
    if (!fmt_ok || !depth_ok) goto fail;
    xcb_shm_query_version_reply_t *sv = xcb_shm_query_version_reply(st->conn, xcb_shm_query_version(st->conn), NULL);
    if (sv == NULL) goto fail;
    free(sv);
    const xcb_query_extension_reply_t *pe = xcb_get_extension_data(st->conn, &xcb_present_id);
    if (pe == NULL || !pe->present) goto fail;
    xcb_present_query_version_reply_t *pv = xcb_present_query_version_reply(
        st->conn, xcb_present_query_version(st->conn, 1, 0), NULL);
    if (pv == NULL) goto fail;
    free(pv);
    const xcb_query_extension_reply_t *se = xcb_get_extension_data(st->conn, &xcb_sync_id);
    if (!se || !se->present) goto fail;
    xcb_sync_initialize_reply_t *si = xcb_sync_initialize_reply(st->conn,
        xcb_sync_initialize(st->conn, 3, 1), NULL);
    if (!si) goto fail;
    bool fences_ok = si->major_version > 3 || (si->major_version == 3 && si->minor_version >= 1);
    free(si);
    if (!fences_ok) goto fail;
    uint32_t eid = xcb_generate_id(st->conn);
    st->special = xcb_register_for_special_xge(st->conn, &xcb_present_id, eid, NULL);
    if (!st->special) { rc = RENDER_ERR_INIT; goto fail; }
    xcb_generic_error_t *err = xcb_request_check(st->conn, xcb_present_select_input_checked(st->conn,
        eid, st->win, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY));
    if (err) { free(err); goto fail; }
    st->pixmap = xcb_generate_id(st->conn);
    err = xcb_request_check(st->conn, xcb_create_pixmap_checked(st->conn, st->depth, st->pixmap,
        st->win, (uint16_t)st->max_w, (uint16_t)st->max_h));
    if (err) { free(err); st->pixmap = 0; goto fail; }
    st->gc = xcb_generate_id(st->conn);
    uint32_t values[] = {0};
    err = xcb_request_check(st->conn, xcb_create_gc_checked(st->conn, st->gc, st->pixmap,
        XCB_GC_GRAPHICS_EXPOSURES, values));
    if (err) { free(err); st->gc = 0; goto fail; }
    st->device_fence = xcb_generate_id(st->conn);
    err = xcb_request_check(st->conn, xcb_sync_create_fence_checked(st->conn, st->pixmap, st->device_fence, 1));
    if (err) { free(err); st->device_fence = 0; goto fail; }
    /* shm framebuffer */
    size_t bytes = (size_t)st->max_w * st->max_h * 4u;
    st->shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
    if (st->shmid < 0) { rc = RENDER_ERR_INIT; goto fail; }
    void *mem = shmat(st->shmid, NULL, 0);
    if (mem == (void *)-1) { rc = RENDER_ERR_INIT; goto fail; }
    st->pix = mem;
    st->seg = xcb_generate_id(st->conn);
    err = xcb_request_check(st->conn,
        xcb_shm_attach_checked(st->conn, st->seg, (uint32_t)st->shmid, 0));
    if (err) { free(err); st->seg = 0; rc = RENDER_ERR_UNSUPPORTED; goto fail; }
    shmctl(st->shmid, IPC_RMID, NULL); /* removed once attached everywhere */
    st->shmid = -1;
    for (size_t i = 0; i < (size_t)st->max_w * st->max_h; i++) st->pix[i] = st->alpha_or; /* fault pages in */
    /* snapshot storage */
    st->max_cells = cfg->max_cells; st->max_glyphs = cfg->max_glyphs; st->max_pages = cfg->max_pages;
    st->max_strips = (size_t)(cfg->max_cells < cfg->max_height ? cfg->max_cells : cfg->max_height) / 2u + 1u;
    st->cells = calloc(st->max_cells ? st->max_cells : 1, sizeof *st->cells);
    st->glyphs = calloc(st->max_glyphs ? st->max_glyphs : 1, sizeof *st->glyphs);
    st->pages = calloc(st->max_pages ? st->max_pages : 1, sizeof *st->pages);
    st->strips = calloc(st->max_strips, sizeof *st->strips);
    if (!st->cells || !st->glyphs || !st->pages || !st->strips) { rc = RENDER_ERR_INIT; goto fail; }
    st->scene = (raster_scene){cfg->dims, st->cells, st->glyphs, 0, st->pages, 0, st->alpha_or};
    st->up = true;
    return RENDER_OK;
fail:
    cpu_release(st);
    return rc;
}

static int cpu_resize(render_backend *b, render_dims dims)
{
    cpu_state *st = b->state;
    if (!st->up) return RENDER_ERR_STATE;
    st->scene.dims = dims;
    return RENDER_OK;
}

static int cpu_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    cpu_state *st = b->state;
    if (!st->up) return RENDER_ERR_STATE;
    if (st->have_frame) return RENDER_ERR_BUSY; /* previous frame not yet fully presented */
    if (count > st->max_strips) return RENDER_ERR_CAPACITY;
    st->metrics = (raster_metrics){.frame_id = g->frame_id, .submit_ns = trace_now_ns()};
    uint32_t cols = g->dims.cols, total = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(&st->cells[(size_t)strips[i].first_row * cols], &g->cells[(size_t)strips[i].first_row * cols],
               (size_t)strips[i].row_count * cols * sizeof(render_cell));
        st->strips[i] = strips[i];
        total += strips[i].row_count;
    }
    if (g->glyph_count) memcpy(st->glyphs, g->glyphs, g->glyph_count * sizeof *g->glyphs);
    if (g->page_count) memcpy(st->pages, g->pages, g->page_count * sizeof *g->pages);
    st->scene.dims = g->dims;
    st->scene.glyph_count = g->glyph_count; st->scene.page_count = g->page_count;
    st->nstrips = count; st->total_rows = total; st->frame_id = g->frame_id;
    uint32_t njobs = total < RASTER_JOBS ? total : RASTER_JOBS;
    st->metrics.jobs = njobs;
    if (!njobs) st->metrics.ready_ns = trace_now_ns();
    st->nhandles = 0;
    st->pending = njobs;
    for (uint32_t j = 0; j < njobs; j++) {
        st->jobs[j].st = st; st->jobs[j].index = j; st->jobs[j].njobs = njobs;
        work_job job = {strip_job_fn, &st->jobs[j], g->frame_id, WORK_RASTER};
        work_handle h = work_submit(st->pool, job);
        if (h.epoch == 0) {
            for (size_t k = 0; k < st->nhandles; k++) work_cancel(st->pool, st->handles[k]);
            /* wait for jobs already started: they only touch backend-owned memory */
            for (size_t k = 0; k < st->nhandles; k++)
                while (atomic_load_explicit(&st->pool->slots[st->handles[k].slot].busy, memory_order_acquire) &&
                       atomic_load_explicit(&st->pool->slots[st->handles[k].slot].epoch, memory_order_acquire) == st->handles[k].epoch + 1u) { struct timespec ts = {0, 50000}; nanosleep(&ts, NULL); }
            st->nhandles = 0;
            st->pending = 0;
            return RENDER_ERR_CAPACITY;
        }
        st->handles[st->nhandles++] = h;
    }
    st->have_frame = true;
    return RENDER_OK;
}

static int cpu_present(render_backend *b, uint32_t frame_id)
{
    cpu_state *st = b->state;
    if (!st->up || !st->have_frame || frame_id != st->frame_id) return RENDER_ERR_STATE;
    if (st->pending != 0) return RENDER_ERR_BUSY;
    atomic_store_explicit(&st->armed, 0, memory_order_relaxed);
    work_job fj = {fence_job_fn, st, frame_id, WORK_RASTER};
    work_handle h = work_submit(st->pool, fj);
    if (h.epoch == 0) return RENDER_ERR_BUSY;
    st->handles[st->nhandles++] = h;
    st->metrics.present_ns = trace_now_ns();
    xcb_sync_reset_fence(st->conn, st->device_fence);
    xcb_sync_trigger_fence(st->conn, st->device_fence);
    xcb_sync_await_fence(st->conn, 1, &st->device_fence);
    xcb_sync_query_fence_cookie_t ck = xcb_sync_query_fence(st->conn, st->device_fence);
    st->metrics.fence_sequence = ck.sequence;
    st->fence_seq = ck.sequence; st->fence_frame = frame_id;
    /* Query the uploaded drawable before the scheduled window copy. T5 is
     * upload device completion plus T4, not T6/display completion. */
    st->metrics.present_sequence = xcb_present_pixmap(st->conn, st->win, st->pixmap, frame_id, 0, 0, 0, 0, 0,
        st->device_fence, 0, XCB_PRESENT_OPTION_COPY, 0, 0, 0, 0, NULL).sequence;
    xcb_flush(st->conn);
    st->metrics.issue_ns = trace_now_ns() - st->metrics.present_ns;
    atomic_store_explicit(&st->armed, 1, memory_order_release);
    st->have_frame = false;
    return RENDER_OK;
}

static int cpu_event(render_backend *b, const render_event *event)
{
    msg_payload p;
    memcpy(&p, event->work->data, sizeof p);
    switch (event->work->kind) {
    case MSG_STRIPS: {
        cpu_state *st = b->state;
        if (event->frame_id != st->metrics.frame_id || p.msc >= RASTER_JOBS) return RENDER_ERR_FRAME;
        if (!st->pending || st->metrics.strip_ns[p.msc]) return RENDER_ERR_STATE;
        uint64_t upload_start = trace_now_ns();
        upload_strip(st, (uint32_t)p.msc);
        uint64_t queued = trace_now_ns();
        st->pending--;
        st->metrics.strip_ns[p.msc] = p.ns - p.ust;
        st->metrics.queue_ns[p.msc] = p.ust - st->metrics.submit_ns;
        st->metrics.upload_issue_ns[p.msc] = queued - upload_start;
        if (queued > st->metrics.upload_queued_ns) st->metrics.upload_queued_ns = queued;
        if (p.ns > st->metrics.ready_ns) st->metrics.ready_ns = p.ns;
        return RENDER_OK;
    }
    case MSG_FENCE:
        ((cpu_state *)b->state)->metrics.server_ns = p.ns;
        if (p.status != 0) return RENDER_ERR_DEVICE;
        return render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, event->frame_id, p.ns);
    case MSG_PRESENT:
        ((cpu_state *)b->state)->metrics.present_kind = (uint32_t)p.status / 256u;
        ((cpu_state *)b->state)->metrics.present_mode = (uint32_t)p.status % 256u;
        ((cpu_state *)b->state)->last_ust = p.ust; ((cpu_state *)b->state)->last_msc = p.msc;
        return render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, event->frame_id, p.ns);
    case MSG_FAIL: return RENDER_ERR_DEVICE;
    default: return RENDER_ERR_UNSUPPORTED;
    }
}

static void cpu_shutdown(render_backend *b)
{
    cpu_state *st = b->state;
    if (!st->up) return;
    for (size_t k = 0; k < st->nhandles; k++) work_cancel(st->pool, st->handles[k]);
    for (size_t k = 0; k < st->nhandles; k++)
        while (atomic_load_explicit(&st->pool->slots[st->handles[k].slot].busy, memory_order_acquire) &&
               atomic_load_explicit(&st->pool->slots[st->handles[k].slot].epoch, memory_order_acquire) ==
                   st->handles[k].epoch + 1u) {
            struct timespec ts = {0, 50000}; nanosleep(&ts, NULL);
        }
    cpu_release(st);
}

int render_cpu_backend(render_backend *b)
{
    if (b == NULL) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    *b = (render_backend){
        .info = {"cpu-raster", sizeof(cpu_state), _Alignof(cpu_state),
                 RENDER_CAP_DEVICE_TIMING | RENDER_CAP_PRESENT_TIMING | RENDER_CAP_RASTER_POOL},
        .ops = {cpu_init, cpu_resize, cpu_submit, cpu_present, cpu_event, cpu_shutdown}
    };
    return RENDER_OK;
}

bool raster_last_present(const render_backend *b, uint64_t *ust, uint64_t *msc)
{
    if (b == NULL || !b->initialized || b->state == NULL) return false;
    const cpu_state *st = b->state;
    if (ust) *ust = st->last_ust;
    if (msc) *msc = st->last_msc;
    return true;
}

bool raster_frame_metrics(const render_backend *b, raster_metrics *out)
{
    if (!b || !b->initialized || !b->state || !out) return false;
    *out = ((const cpu_state *)b->state)->metrics;
    return out->frame_id != 0;
}
