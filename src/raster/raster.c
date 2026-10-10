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
#include <limits.h>
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
#include <xcb/xfixes.h>

enum { MSG_STRIPS = 1, MSG_FENCE = 2, MSG_PRESENT = 3, MSG_FAIL = 4 };
typedef struct msg_payload { uint64_t ns, ust, msc; int32_t status; } msg_payload;
_Static_assert(sizeof(msg_payload) <= WORK_MSG_DATA, "raster message fits mailbox");

struct cpu_state;
typedef struct strip_job { struct cpu_state *st; uint32_t index, njobs; raster_palette palette; } strip_job;
/* Immutable inputs, initialized before work_submit's mailbox handoff. The
 * worker never reads subsequently written UI fields from cpu_state. */
typedef struct fence_job {
    xcb_connection_t *conn;
    xcb_special_event_t *special;
    xcb_pixmap_t pixmap;
    uint32_t frame;
    unsigned int sequence;
} fence_job;

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
    bool snapshot_valid, inline_frame, caret_hint;
    uint32_t inline_at[2], inline_count;
    bool inline_fence, inline_present, inline_idle;
    uint64_t inline_t6, completion_deadline;
    bool failed;
    /* jobs */
    strip_job jobs[RASTER_JOBS];
    work_handle handles[RASTER_JOBS + 1u];
    size_t nhandles;
    work_handle retired[4u * (RASTER_JOBS + 1u)];
    size_t nretired;
    uint32_t pending; /* UI only: decremented by strip work messages */
    /* fence job */
    bool strip_done[RASTER_JOBS], fence_done, present_done;
    bool present_issued; /* retain issued X requests on fence enqueue failure */
    fence_job fence;
    work_handle fence_handle;
    uint64_t last_ust, last_msc;   /* UI-thread copy of the latest Present completion */
    raster_metrics metrics;
} cpu_state;

/* Essential acknowledgements must outlive mailbox saturation. UI draining
 * makes progress; quiescent shutdown/cancel interrupts a waiting producer. */
static void publish_completion(work_ctx *c, const work_msg *m)
{
    while (!work_should_stop(c)) {
        if (work_publish(c, m)) return;
        struct timespec pause = {0, 50000};
        nanosleep(&pause, NULL);
    }
}

static void post(work_ctx *c, uint32_t kind, uint64_t ns, int32_t status, uint64_t ust, uint64_t msc)
{
    work_msg m = {0};
    msg_payload p = {.ns = ns, .ust = ust, .msc = msc, .status = status};
    m.kind = kind; m.generation = c->generation;
    memcpy(m.data, &p, sizeof p);
    publish_completion(c, &m);
}

typedef struct row_ctx { cpu_state *st; work_ctx *work; raster_palette *palette; } row_ctx;
static bool row_should_stop(void *u) { return work_should_stop(u); }
static void do_row(void *u, uint32_t row)
{
    row_ctx *ctx = u;
    if (work_should_stop(ctx->work)) return;
    cpu_state *st = ctx->st;
    (void)raster_row_cached_cancellable(&st->scene,
        st->pix + (size_t)row * st->scene.dims.cell_h * st->stride_px,
        st->stride_px, row, ctx->palette, row_should_stop, ctx->work);
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
    publish_completion(c, &m);
}

/* UI only. Atomic batch refusal starts no jobs; upload requires a validated
 * owned strip completion. */
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
    const fence_job *job = c->arg;
    int fd = xcb_get_file_descriptor(job->conn);
    uint64_t deadline = trace_now_ns() + UINT64_C(2000000000);
    const uint32_t frame = job->frame;
    const unsigned int seq = job->sequence;
    int32_t status = 0;
    uint64_t t5 = 0, t6 = 0, ust = 0, msc = 0;
    uint32_t present_info = 0;
    bool got_fence = false, got_present = false, got_idle = false;
    for (;;) {
        if (work_should_stop(c)) return;
        bool progressed = false;
        if (!got_fence) {
            void *reply = NULL; xcb_generic_error_t *err = NULL;
            if (xcb_poll_for_reply(job->conn, seq, &reply, &err)) {
                t5 = trace_now_ns(); got_fence = true; progressed = true;
                if (err || !reply || !((xcb_sync_query_fence_reply_t *)reply)->triggered) status = -1;
                free(reply); free(err);
                post(c, MSG_FENCE, t5, status, 0, 0);
            }
        }
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_special_event(job->conn, job->special)) != NULL) {
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
                if (ie->serial == frame && ie->pixmap == job->pixmap) got_idle = true;
            }
            free(e);
        }
        while ((e = xcb_poll_for_event(job->conn)) != NULL) {
            progressed = true;
            if (e->response_type == 0) status = -1;
            free(e);
        }
        if (status != 0 || xcb_connection_has_error(job->conn) || trace_now_ns() > deadline) {
            post(c, MSG_FAIL, 0, -1, 0, 0); return;
        }
        if (got_fence && got_present && got_idle) {
            /* Last access to state before publishing: UI may start N+1 as soon
             * as it drains this message. Never discard an early Present event. */
            post(c, MSG_PRESENT, t6, (int32_t)present_info, ust, msc);
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

/* The kernels emit exactly 0x00RRGGBB, with opaque alpha for depth 32.
 * Storage bpp alone says nothing about visual channel interpretation. */
static bool cpu_visual_rgb888(const xcb_setup_t *setup, uint32_t visual, uint8_t depth)
{
    if (depth != 24 && depth != 32) return false;
    for (xcb_screen_iterator_t screen = xcb_setup_roots_iterator(setup); screen.rem; xcb_screen_next(&screen))
        for (xcb_depth_iterator_t d = xcb_screen_allowed_depths_iterator(screen.data); d.rem; xcb_depth_next(&d)) {
            if (d.data->depth != depth) continue;
            for (xcb_visualtype_iterator_t v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
                if (v.data->visual_id == visual)
                    return v.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                        v.data->red_mask == 0x00ff0000u && v.data->green_mask == 0x0000ff00u &&
                        v.data->blue_mask == 0x000000ffu;
        }
    return false;
}

/* Present may copy fractional-cell margins and retained pixels after shrink.
 * Clear the whole admitted native surface, including pixels never uploaded
 * from SHM. One asynchronous native request keeps resize off a UI pixel scan. */
static void cpu_clear_pixmap(cpu_state *st)
{
    xcb_rectangle_t extent={0,0,(uint16_t)st->max_w,(uint16_t)st->max_h};
    xcb_poly_fill_rectangle(st->conn,st->pixmap,st->gc,1,&extent);
    xcb_flush(st->conn);
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
    if (!fmt_ok || !depth_ok || !cpu_visual_rgb888(setup, pl->visual, pl->depth)) goto fail;
    xcb_shm_query_version_reply_t *sv = xcb_shm_query_version_reply(st->conn, xcb_shm_query_version(st->conn), NULL);
    if (sv == NULL) goto fail;
    free(sv);
    const xcb_query_extension_reply_t *pe = xcb_get_extension_data(st->conn, &xcb_present_id);
    if (pe == NULL || !pe->present) goto fail;
    xcb_xfixes_query_version_reply_t *xv = xcb_xfixes_query_version_reply(st->conn,
        xcb_xfixes_query_version(st->conn, 2, 0), NULL);
    if (!xv) goto fail;
    bool regions_ok = xv->major_version >= 2;
    free(xv);
    if (!regions_ok) goto fail;
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
    uint32_t values[] = {st->alpha_or, 0};
    err = xcb_request_check(st->conn, xcb_create_gc_checked(st->conn, st->gc, st->pixmap,
        XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES, values));
    if (err) { free(err); st->gc = 0; goto fail; }
    cpu_clear_pixmap(st);
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
    if (st->failed) return RENDER_ERR_DEVICE;
    if ((uint64_t)dims.cols*dims.cell_w > st->max_w ||
        (uint64_t)dims.rows*dims.cell_h > st->max_h) return RENDER_ERR_CAPACITY;
    cpu_clear_pixmap(st);
    st->scene.dims = dims;
    st->snapshot_valid = false;
    return RENDER_OK;
}

/* Final authenticated publication is the last snapshot/native access in
 * each job. The worker may still be returning from publication: retain its
 * physical lease until return, without waiting or cancelling on submit. */
static int cpu_retire_jobs(cpu_state *st)
{
    size_t kept = 0;
    for (size_t k = 0; k < st->nretired; k++)
        if (!work_handle_finished(st->pool, st->retired[k]))
            st->retired[kept++] = st->retired[k];
    st->nretired = kept;
    size_t unfinished = 0;
    for (size_t k = 0; k < st->nhandles; k++)
        if (!work_handle_finished(st->pool, st->handles[k])) unfinished++;
    if (unfinished && (st->pending || !st->fence_done || !st->present_done))
        return RENDER_ERR_BUSY;
    if (unfinished > sizeof st->retired / sizeof st->retired[0] - kept)
        return RENDER_ERR_BUSY;
    for (size_t k = 0; k < st->nhandles; k++)
        if (!work_handle_finished(st->pool, st->handles[k]))
            st->retired[st->nretired++] = st->handles[k];
    st->nhandles = 0;
    return RENDER_OK;
}

/* Quiescent shutdown is the sole physical join; every outstanding lease,
 * including a worker delayed after publication, still belongs to this owner. */
static void cpu_join_jobs(cpu_state *st)
{
    for (size_t k = 0; k < st->nhandles; k++) work_cancel(st->pool, st->handles[k]);
    for (size_t k = 0; k < st->nretired; k++) work_cancel(st->pool, st->retired[k]);
    for (size_t k = 0; k < st->nhandles + st->nretired; k++) {
        work_handle h = k < st->nhandles ? st->handles[k] : st->retired[k - st->nhandles];
        while (!work_handle_finished(st->pool, h)) {
            struct timespec ts = {0, 50000}; nanosleep(&ts, NULL);
        }
    }
    st->nhandles = st->nretired = 0;
}

/* Only a retained cursor colour/attribute change may use cell comparison.
 * Cap inline work at two 4096-pixel cells; larger cells keep worker slicing.
 * General damage still rasterizes every marked cell (atlas pixel changes can
 * be in place). Validated wide pairs remain a bounded two-cell scene. */
static bool inline_cursor_damage(cpu_state *st, const render_grid *g,
                                 const render_strip *strips, size_t count)
{
    st->inline_count = 0;
    if (!st->caret_hint || !st->snapshot_valid ||
        (uint64_t)g->dims.cell_w * g->dims.cell_h > 4096u || count != 1 || strips[0].row_count != 1 ||
        memcmp(&st->scene.dims, &g->dims, sizeof g->dims) ||
        st->scene.glyph_count != g->glyph_count || st->scene.page_count != g->page_count ||
        (g->glyph_count && memcmp(st->glyphs, g->glyphs, g->glyph_count * sizeof *g->glyphs)) ||
        (g->page_count && memcmp(st->pages, g->pages, g->page_count * sizeof *g->pages))) return false;
    uint32_t start = strips[0].first_row * g->dims.cols;
    for (uint32_t col = 0; col < g->dims.cols; col++) {
        const render_cell *a = &st->cells[start + col], *z = &g->cells[start + col];
        if (!memcmp(a, z, sizeof *a)) continue;
        if ((uint64_t)col * g->dims.cell_w > INT16_MAX || st->inline_count == 2 || !((a->attrs | z->attrs) & RENDER_ATTR_CURSOR) ||
            ((a->attrs ^ z->attrs) & (uint16_t)~(RENDER_ATTR_CURSOR | RENDER_ATTR_SELECTION)) ||
            a->glyph_index != z->glyph_index || a->atlas_slot != z->atlas_slot ||
            a->reserved != z->reserved) return false;
        st->inline_at[st->inline_count++] = start + col;
    }
    return st->inline_count != 0;
}
static void upload_cursor_cells(cpu_state *st)
{
    const render_dims d = st->scene.dims;
    for (uint32_t i = 0; i < st->inline_count; i++) {
        uint32_t at = st->inline_at[i], row = at / d.cols, col = at % d.cols;
        uint32_t x = col * d.cell_w, y = row * d.cell_h;
        if (st->cells[at].attrs & RENDER_ATTR_WIDE_RIGHT) continue;
        /* A bounded cell scene keeps the existing pixel-exact kernel and avoids
         * drawing neighbouring cells beyond a validated wide pair. */
        raster_scene cell = st->scene;
        cell.dims.cols = (st->cells[at].attrs & RENDER_ATTR_WIDE_LEFT) ? 2u : 1u;
        cell.dims.rows = 1;
        cell.cells = st->cells + at;
        raster_row_sse2(&cell, st->pix + (size_t)y * st->stride_px + x, st->stride_px, 0);
        xcb_shm_put_image(st->conn, st->pixmap, st->gc,
            (uint16_t)st->max_w, (uint16_t)st->max_h, (uint16_t)x, (uint16_t)y,
            (uint16_t)(cell.dims.cols * d.cell_w), (uint16_t)d.cell_h, (int16_t)x, (int16_t)y,
            st->depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, st->seg, 0);
    }
    xcb_flush(st->conn);
}

static int cpu_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    cpu_state *st = b->state;
    if (!st->up) return RENDER_ERR_STATE;
    if (st->failed) return RENDER_ERR_DEVICE;
    if (st->have_frame) return RENDER_ERR_BUSY; /* previous frame not yet fully presented */
    if (count > st->max_strips || g->glyph_count > RASTER_FRAME_GLYPH_LIMIT ||
        g->page_count > RASTER_FRAME_PAGE_LIMIT ||
        (size_t)g->dims.cols * g->dims.rows > RASTER_FRAME_CELL_LIMIT) return RENDER_ERR_CAPACITY;
    int retire_rc = cpu_retire_jobs(st);
    if (retire_rc != RENDER_OK) return retire_rc;
    st->metrics = (raster_metrics){.frame_id = g->frame_id, .submit_ns = trace_now_ns()};
    st->inline_frame = inline_cursor_damage(st, g, strips, count);
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
    uint32_t njobs = st->inline_frame ? 0 : (total < RASTER_JOBS ? total : RASTER_JOBS);
    st->metrics.jobs = njobs;
    if (!njobs) st->metrics.ready_ns = trace_now_ns();
    memset(st->strip_done, 0, sizeof st->strip_done);
    st->fence_done = false; st->present_done = false; st->present_issued = false;
    st->fence_handle = (work_handle){0};
    st->pending = njobs;
    if (st->inline_frame) {
        st->inline_fence = st->inline_present = st->inline_idle = false;
        uint64_t start = trace_now_ns();
        upload_cursor_cells(st);
        st->metrics.inline_cells = st->inline_count;
        st->metrics.upload_issue_ns[0] = trace_now_ns() - start;
        st->metrics.upload_queued_ns = st->metrics.ready_ns = trace_now_ns();
        st->have_frame = true;
        return RENDER_OK;
    }
    work_job batch[RASTER_JOBS];
    for (uint32_t j = 0; j < njobs; j++) {
        st->jobs[j].st = st; st->jobs[j].index = j; st->jobs[j].njobs = njobs;
        batch[j] = (work_job){strip_job_fn, &st->jobs[j], g->frame_id, WORK_RASTER};
    }
    if (work_submit_batch(st->pool, batch, njobs, st->handles) != 0) {
        st->pending = 0;
        return RENDER_ERR_CAPACITY;
    }
    st->nhandles = njobs;
    st->have_frame = true;
    st->snapshot_valid = true;
    return RENDER_OK;
}

static int cpu_present(render_backend *b, uint32_t frame_id)
{
    cpu_state *st = b->state;
    if (st->failed) return RENDER_ERR_DEVICE;
    if (!st->up || !st->have_frame || frame_id != st->frame_id) return RENDER_ERR_STATE;
    if (st->pending != 0) return RENDER_ERR_BUSY;
    if (!st->present_issued) {
        st->metrics.present_ns = trace_now_ns();
        st->completion_deadline = st->metrics.present_ns + UINT64_C(2000000000);
        xcb_sync_reset_fence(st->conn, st->device_fence);
        xcb_sync_trigger_fence(st->conn, st->device_fence);
        xcb_sync_await_fence(st->conn, 1, &st->device_fence);
        xcb_sync_query_fence_cookie_t ck = xcb_sync_query_fence(st->conn, st->device_fence);
        st->metrics.fence_sequence = ck.sequence;
        xcb_xfixes_region_t update = 0;
        if (st->inline_frame) {
            xcb_rectangle_t rects[2];
            for (uint32_t i = 0; i < st->inline_count; i++) {
                uint32_t at = st->inline_at[i];
                rects[i] = (xcb_rectangle_t){
                    (int16_t)((at % st->scene.dims.cols) * st->scene.dims.cell_w),
                    (int16_t)((at / st->scene.dims.cols) * st->scene.dims.cell_h),
                    (uint16_t)st->scene.dims.cell_w, (uint16_t)st->scene.dims.cell_h};
            }
            update = xcb_generate_id(st->conn);
            xcb_xfixes_create_region(st->conn, update, st->inline_count, rects);
        }
        /* Query the uploaded drawable before the scheduled window copy. */
        st->metrics.present_sequence = xcb_present_pixmap(st->conn, st->win, st->pixmap, frame_id, 0, update, 0, 0, 0,
            st->device_fence, 0, XCB_PRESENT_OPTION_COPY, 0, 0, 0, 0, NULL).sequence;
        /* Present snapshots the update region before this ordered destroy. */
        if (update) xcb_xfixes_destroy_region(st->conn, update);
        xcb_flush(st->conn);
        st->metrics.issue_ns = trace_now_ns() - st->metrics.present_ns;
        st->fence = (fence_job){st->conn, st->special, st->pixmap, frame_id, ck.sequence};
        st->present_issued = true;
    }
    if (st->inline_frame) {
        st->have_frame = false;
        return RENDER_OK;
    }
    st->metrics.completion_jobs = 1;
    work_job fj = {fence_job_fn, &st->fence, frame_id, WORK_RASTER};
    work_handle h = work_submit(st->pool, fj);
    if (h.epoch == 0) return RENDER_ERR_BUSY;
    st->fence_handle = h;
    st->handles[st->nhandles++] = h;
    st->have_frame = false;
    return RENDER_OK;
}

static bool owns_message(work_handle handle, const work_msg *msg)
{
    return handle.epoch != 0 && handle.slot == msg->slot_ && handle.epoch == msg->epoch_;
}

static int cpu_event(render_backend *b, const render_event *event)
{
    cpu_state *st = b->state;
    const work_msg *m = event->work;
    msg_payload p;
    memcpy(&p, m->data, sizeof p);
    if (event->frame_id != st->metrics.frame_id || m->generation != st->metrics.frame_id)
        return RENDER_ERR_UNSUPPORTED;
    if (m->kind == MSG_STRIPS) {
        if (p.msc >= st->metrics.jobs || p.msc >= st->nhandles ||
            !owns_message(st->handles[p.msc], m)) return RENDER_ERR_UNSUPPORTED;
    } else if (m->kind == MSG_FENCE || m->kind == MSG_PRESENT || m->kind == MSG_FAIL) {
        if (!owns_message(st->fence_handle, m)) return RENDER_ERR_UNSUPPORTED;
    } else return RENDER_ERR_UNSUPPORTED;
    switch (m->kind) {
    case MSG_STRIPS: {
        if (!st->pending || st->strip_done[p.msc]) return RENDER_ERR_STATE;
        uint64_t upload_start = trace_now_ns();
        upload_strip(st, (uint32_t)p.msc);
        uint64_t queued = trace_now_ns();
        st->strip_done[p.msc] = true;
        st->pending--;
        st->metrics.strip_ns[p.msc] = p.ns - p.ust;
        st->metrics.queue_ns[p.msc] = p.ust - st->metrics.submit_ns;
        st->metrics.upload_issue_ns[p.msc] = queued - upload_start;
        if (queued > st->metrics.upload_queued_ns) st->metrics.upload_queued_ns = queued;
        if (p.ns > st->metrics.ready_ns) st->metrics.ready_ns = p.ns;
        return RENDER_OK;
    }
    case MSG_FENCE:
        if (st->fence_done) return RENDER_ERR_STATE;
        st->fence_done = true;
        st->metrics.server_ns = p.ns;
        if (p.status != 0) return RENDER_ERR_DEVICE;
        return render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, event->frame_id, p.ns);
    case MSG_PRESENT:
        if (st->present_done) return RENDER_ERR_STATE;
        st->present_done = true;
        st->metrics.present_kind = (uint32_t)p.status / 256u;
        st->metrics.present_mode = (uint32_t)p.status % 256u;
        st->last_ust = p.ust; st->last_msc = p.msc;
        return render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, event->frame_id, p.ns);
    case MSG_FAIL: st->failed = true; return RENDER_ERR_DEVICE;
    default: return RENDER_ERR_UNSUPPORTED;
    }
}

static void cpu_shutdown(render_backend *b)
{
    cpu_state *st = b->state;
    if (!st->up) return;
    cpu_join_jobs(st);
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

void raster_set_caret_only(render_backend *b, bool enabled)
{
    if (b && b->initialized && b->state && b->ops.init == cpu_init)
        ((cpu_state *)b->state)->caret_hint = enabled;
}

int raster_completion_fd(const render_backend *b)
{
    if (!b || !b->initialized || !b->state || b->ops.init != cpu_init) return -1;
    const cpu_state *st = b->state;
    /* The legacy full-frame fence worker owns its own connection reads. */
    return st->inline_frame && b->active && b->presented ? xcb_get_file_descriptor(st->conn) : -1;
}
int raster_poll_completions(render_backend *b)
{
    if (raster_completion_fd(b) < 0) return RENDER_OK;
    cpu_state *st = b->state;
    if (st->failed) return RENDER_ERR_DEVICE;
    bool progressed;
    do {
        progressed = false;
        if (!st->inline_fence) {
            void *reply = NULL; xcb_generic_error_t *err = NULL;
            if (xcb_poll_for_reply(st->conn, st->metrics.fence_sequence, &reply, &err)) {
                bool ok = !err && reply && ((xcb_sync_query_fence_reply_t *)reply)->triggered;
                free(reply); free(err);
                if (!ok) { st->failed = true; return RENDER_ERR_DEVICE; }
                st->inline_fence = true; progressed = true;
                st->metrics.server_ns = trace_now_ns();
                int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, st->frame_id, st->metrics.server_ns);
                if (rc) return rc;
            }
        }
        xcb_generic_event_t *ev;
        while ((ev = xcb_poll_for_special_event(st->conn, st->special)) != NULL) {
            progressed = true;
            const xcb_present_generic_event_t *ge = (const xcb_present_generic_event_t *)ev;
            if (ge->evtype == XCB_PRESENT_COMPLETE_NOTIFY) {
                const xcb_present_complete_notify_event_t *pe = (const xcb_present_complete_notify_event_t *)ev;
                if (pe->serial == st->frame_id && pe->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                    st->inline_present = true; st->inline_t6 = trace_now_ns();
                    st->last_ust = pe->ust; st->last_msc = pe->msc;
                    st->metrics.present_kind = pe->kind; st->metrics.present_mode = pe->mode;
                }
            } else if (ge->evtype == XCB_PRESENT_IDLE_NOTIFY) {
                const xcb_present_idle_notify_event_t *ie = (const xcb_present_idle_notify_event_t *)ev;
                if (ie->serial == st->frame_id && ie->pixmap == st->pixmap) st->inline_idle = true;
            }
            free(ev);
        }
        while ((ev = xcb_poll_for_event(st->conn)) != NULL) {
            bool error = ev->response_type == 0; free(ev); progressed = true;
            if (error) { st->failed = true; return RENDER_ERR_DEVICE; }
        }
    } while (progressed);
    if (xcb_connection_has_error(st->conn)) { st->failed = true; return RENDER_ERR_DEVICE; }
    if (st->inline_fence && st->inline_present && st->inline_idle) {
        st->present_done = true;
        return render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, st->frame_id, st->inline_t6);
    }
    if (st->completion_deadline && trace_now_ns() >= st->completion_deadline) {
        st->failed = true;
        return RENDER_ERR_DEVICE;
    }
    return RENDER_OK;
}

/* Waiting damage can clamp its existing wait to this deadline. Clean idle
 * frames need no periodic timer; poll drains ready replies before expiring. */
int raster_completion_timeout(const render_backend *b, int requested)
{
    if (raster_completion_fd(b) < 0) return requested;
    const cpu_state *st = b->state;
    if (st->failed) return 0;
    if (!st->completion_deadline) return requested;
    uint64_t now = trace_now_ns();
    uint64_t ms = now >= st->completion_deadline ? 0 :
        (st->completion_deadline - now + UINT64_C(999999)) / UINT64_C(1000000);
    int limit = ms > INT_MAX ? INT_MAX : (int)ms;
    return requested < 0 || requested > limit ? limit : requested;
}

bool raster_last_present(const render_backend *b, uint64_t *ust, uint64_t *msc)
{
    if (b == NULL || !b->initialized || b->state == NULL ||
        b->ops.init != cpu_init || b->ops.event != cpu_event) return false;
    const cpu_state *st = b->state;
    if (ust) *ust = st->last_ust;
    if (msc) *msc = st->last_msc;
    return true;
}

bool raster_frame_metrics(const render_backend *b, raster_metrics *out)
{
    if (!b || !b->initialized || !b->state || !out ||
        b->ops.init != cpu_init || b->ops.event != cpu_event) return false;
    *out = ((const cpu_state *)b->state)->metrics;
    return out->frame_id != 0;
}
