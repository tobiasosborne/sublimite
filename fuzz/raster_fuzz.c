/* P2.5: arbitrary dims/cells/attrs/atlas bytes -> SSE2 kernel == scalar
 * reference, with an overrun canary around the surface; plus partition
 * coverage. Cells are made valid by construction (wide pairs matched). */
#include "raster/raster.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct rd { const uint8_t *p; size_t n, i; } rd;
static uint32_t take(rd *r) { return r->n ? r->p[r->i++ % r->n] : 0; }

typedef struct stop_state {uint32_t checks, limit;} stop_state;
static bool stop_batch(void *u)
{
    stop_state *s = u;
    return ++s->checks >= s->limit;
}

static int kernel_fuzz(const uint8_t *data, size_t size)
{
    if (size < 8) return 0;
    rd r = {data, size, 0};
    render_dims d = {1 + take(&r) % 40, 1 + take(&r) % 3, 1 + take(&r) % 32, 1 + take(&r) % 32};
    if (take(&r) & 1u) d.cell_w = (take(&r) & 1u) ? 8u : 16u;
    /* Cross 256-pixel cancellation batches as well as ordinary font sizes. */
    if ((take(&r) & 15u) == 0) d.cell_w = 255u + take(&r);
    size_t n = (size_t)d.cols * d.rows;
    render_cell cells[120];
    uint8_t page0[32 * 32];
    render_atlas_page pg = {page0, sizeof page0, 32, 32, 32};
    render_glyph gl[8];
    for (size_t i = 0; i < sizeof page0; i++) page0[i] = data[(8 + i) % size] ^ (uint8_t)(i * (take(&r) & 1u));
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t w = 1 + take(&r) % 32, h = 1 + take(&r) % 32;
        gl[i] = (render_glyph){i, 0, take(&r) % (33 - w), take(&r) % (33 - h), w, h};
    }
    for (uint32_t row = 0; row < d.rows; row++)
        for (uint32_t c = 0; c < d.cols; c++) {
            render_cell *cell = &cells[(size_t)row * d.cols + c];
            if (c > 0 && (cell - 1)->attrs & RENDER_ATTR_WIDE_LEFT) {
                render_cell *l = cell - 1;
                *cell = (render_cell){0, RENDER_NO_SLOT, l->fg, l->bg,
                    (uint16_t)((l->attrs & ~RENDER_ATTR_WIDE_LEFT) | RENDER_ATTR_WIDE_RIGHT), 0};
                continue;
            }
            uint32_t b = take(&r) | (take(&r) << 8);
            uint16_t at = (uint16_t)(b & (RENDER_ATTR_BOLD | RENDER_ATTR_ITALIC | RENDER_ATTR_UNDERLINE |
                                           RENDER_ATTR_INVERSE | RENDER_ATTR_CURSOR | RENDER_ATTR_SELECTION));
            uint32_t slot = take(&r) % 9;
            uint32_t fg = take(&r) | take(&r) << 8 | take(&r) << 16, bg = take(&r) | take(&r) << 8 | take(&r) << 16;
            *cell = (render_cell){slot == 8 ? 0 : slot, slot == 8 ? RENDER_NO_SLOT : slot, fg, bg, at, 0};
            if (c + 1 < d.cols && (b & 0x8000u)) cell->attrs |= RENDER_ATTR_WIDE_LEFT;
        }
    raster_scene s = {d, cells, gl, 8, &pg, 1, (take(&r) & 1u) ? 0xff000000u : 0u};
    size_t stride = (size_t)d.cols * d.cell_w + take(&r) % 4;
    if (take(&r) & 1u) stride = (stride + 15u) & ~(size_t)15u;
    size_t px = stride * d.cell_h;
    uint32_t *a = malloc(px * 4), *b2 = malloc(px * 4);
    if (!a || !b2) abort();
    raster_palette palette = {0};
    for (uint32_t row = 0; row < d.rows; row++) {
        memset(a, 0xa5, px * 4); memset(b2, 0xa5, px * 4);
        raster_row_scalar(&s, a, stride, row);
        raster_row_sse2(&s, b2, stride, row);
        if (memcmp(a, b2, px * 4) != 0) abort();
        memset(b2, 0xa5, px * 4);
        raster_row_sse2_stream(&s, b2, stride, row);
        if (memcmp(a, b2, px * 4) != 0) abort();
        memset(b2, 0xa5, px * 4);
        raster_row_cached(&s, b2, stride, row, &palette);
        if (memcmp(a, b2, px * 4) != 0 || palette.rebuilds > RASTER_PALETTE_SLOTS) abort();
        memset(b2, 0xa5, px * 4);
        if (!raster_row_cached_cancellable(&s,b2,stride,row,&palette,NULL,NULL) ||
            memcmp(a,b2,px*4) != 0) abort();
        memset(b2,0xa5,px*4);
        stop_state stop = {0,1u+take(&r)%32u};
        bool done = raster_row_cached_cancellable(&s,b2,stride,row,&palette,stop_batch,&stop);
        if (done) { if (memcmp(a,b2,px*4) != 0) abort(); }
        else {
            if (stop.checks != stop.limit) abort();
            /* Independent work bound: between checks at most 256 pixel stores.
             * Guard padding must stay untouched even on an interrupted row. */
            size_t written = 0;
            for (size_t i=0;i<px;i++) if (b2[i] != 0xa5a5a5a5u) written++;
            if (written > (size_t)(stop.checks-1u)*256u) abort();
            for (uint32_t y=0;y<d.cell_h;y++)
                for (size_t x=(size_t)d.cols*d.cell_w;x<stride;x++)
                    if (b2[(size_t)y*stride+x] != 0xa5a5a5a5u) abort();
        }
    }
    free(a); free(b2);
    (void)n;
    /* partition: every ordinal exactly once */
    uint32_t total = take(&r) % 200, njobs = 1 + take(&r) % 8, prev = 0;
    for (uint32_t j = 0; j < njobs; j++) {
        uint32_t lo, hi;
        raster_partition(total, njobs, j, &lo, &hi);
        if (lo != prev || hi < lo) abort();
        prev = hi;
    }
    if (prev != total) abort();
    return 0;
}

/* Deterministic ownership transport. Every input starts with publication
 * followed by delayed physical return, then mutates that schedule. No globals,
 * native server, worker threads, timing gate or test-only product exports. */
#include "base/base.h"
#include "work/work.h"
#include "trace/trace.h"
#include <poll.h>
#include <xcb/present.h>
#include <xcb/sync.h>
#include <xcb/xcbext.h>
#include <xcb/xfixes.h>
#include <xcb/shm.h>

enum { FUZZ_LEASES=16, FUZZ_MESSAGES=8 };
typedef struct ownership_transport {
    work_pool pool; /* first member: scheduling operations recover this owner */
    work_job jobs[FUZZ_LEASES];
    uint32_t epochs[FUZZ_LEASES], phase[FUZZ_LEASES];
    bool cancelled[FUZZ_LEASES];
    work_msg messages[FUZZ_MESSAGES];
    size_t messages_count, capacity;
    unsigned checks, attempts, native_ready, native_seen;
    uint32_t native_frame;
    bool shutdown, destroyed, server_owned;
} ownership_transport;
static ownership_transport *transport_pool(work_pool *p) { return (ownership_transport *)p; }
static ownership_transport *transport_conn(xcb_connection_t *p) { return (ownership_transport *)p; }
static void transport_live(ownership_transport *t) { EDIT_ASSERT(!t->destroyed); }
static bool transport_stop(const work_ctx *c)
{
    ownership_transport *t=transport_pool(c->pool);
    size_t slot=(size_t)(c->slot-t->pool.slots);
    return t->cancelled[slot] || ++t->checks>32u;
}
static bool transport_publish(work_ctx *c,const work_msg *m)
{
    ownership_transport *t=transport_pool(c->pool); transport_live(t);
    size_t slot=(size_t)(c->slot-t->pool.slots);
    if (t->messages_count>=t->capacity) {
        if (++t->attempts>2u) t->cancelled[slot]=true;
        return false;
    }
    work_msg value=*m; value.slot_=(uint32_t)slot; value.epoch_=c->epoch;
    t->messages[t->messages_count++]=value;
    return true;
}
static int transport_batch(work_pool *p,const work_job *jobs,size_t count,work_handle *handles)
{
    ownership_transport *t=transport_pool(p); transport_live(t);
    size_t available=0;
    for (size_t i=0;i<FUZZ_LEASES;i++) available+=t->phase[i]==0 || t->phase[i]==3;
    if (count>available) return -1; /* atomic refusal */
    size_t next=0;
    for (size_t i=0;i<FUZZ_LEASES && next<count;i++) if (t->phase[i]==0 || t->phase[i]==3) {
        t->jobs[i]=jobs[next]; t->phase[i]=1; t->cancelled[i]=false;
        handles[next++]=(work_handle){(uint32_t)i,++t->epochs[i]};
    }
    return 0;
}
static work_handle transport_submit(work_pool *p,work_job job)
{
    work_handle h={0}; (void)transport_batch(p,&job,1,&h); return h;
}
static bool transport_finished(const work_pool *p,work_handle h)
{
    ownership_transport *t=(ownership_transport *)(uintptr_t)p;
    if (!h.epoch || h.slot>=FUZZ_LEASES || h.epoch!=t->epochs[h.slot]) return true;
    /* Deterministic worker return while shutdown joins a cancelled lease. */
    if (t->shutdown && t->cancelled[h.slot]) t->phase[h.slot]=3;
    return t->phase[h.slot]==0 || t->phase[h.slot]==3;
}
static void transport_cancel(work_pool *p,work_handle h)
{
    ownership_transport *t=transport_pool(p);
    if (!h.epoch || h.slot>=FUZZ_LEASES || h.epoch!=t->epochs[h.slot]) return;
    /* Authenticated publication does not mean physical return. Submit must
     * retain that lease; cancellation/join is confined to shutdown. */
    EDIT_ASSERT(t->shutdown || t->phase[h.slot]!=2);
    t->cancelled[h.slot]=true;
    if (t->phase[h.slot]==1) t->phase[h.slot]=3;
}
static int transport_sleep(const struct timespec *pause,struct timespec *remaining)
{ (void)pause; (void)remaining; return 0; }
static int transport_flush(xcb_connection_t *conn) { transport_live(transport_conn(conn)); return 1; }
static int transport_fd(xcb_connection_t *conn) { transport_live(transport_conn(conn)); return 42; }
static int transport_error(xcb_connection_t *conn) { transport_live(transport_conn(conn)); return 0; }
static int transport_poll(struct pollfd *fds,nfds_t count,int timeout)
{ (void)fds; (void)count; (void)timeout; return 0; }
static int transport_reply(xcb_connection_t *conn,unsigned sequence,void **reply,xcb_generic_error_t **error)
{
    ownership_transport *t=transport_conn(conn); transport_live(t); (void)sequence;
    *error=NULL; *reply=NULL;
    if (!(t->native_ready&1u) || (t->native_seen&1u)) return 0;
    xcb_sync_query_fence_reply_t *r=calloc(1,sizeof *r); EDIT_ASSERT(r);
    r->triggered=1; *reply=r; t->native_seen|=1u; return 1;
}
static xcb_generic_event_t *transport_special(xcb_connection_t *conn,xcb_special_event_t *special)
{
    ownership_transport *t=transport_conn(conn); transport_live(t); (void)special;
    if ((t->native_ready&2u) && !(t->native_seen&2u)) {
        xcb_present_complete_notify_event_t *e=calloc(1,sizeof *e); EDIT_ASSERT(e);
        e->event_type=XCB_PRESENT_COMPLETE_NOTIFY; e->serial=t->native_frame;
        e->kind=XCB_PRESENT_COMPLETE_KIND_PIXMAP; e->msc=1; e->ust=1;
        t->native_seen|=2u; return (xcb_generic_event_t *)e;
    }
    if ((t->native_ready&4u) && !(t->native_seen&4u)) {
        xcb_present_idle_notify_event_t *e=calloc(1,sizeof *e); EDIT_ASSERT(e);
        e->event_type=XCB_PRESENT_IDLE_NOTIFY; e->serial=t->native_frame; e->pixmap=42;
        t->native_seen|=4u; t->server_owned=false; return (xcb_generic_event_t *)e;
    }
    return NULL;
}
static xcb_generic_event_t *transport_event(xcb_connection_t *conn)
{ transport_live(transport_conn(conn)); return NULL; }
static xcb_void_cookie_t transport_put(xcb_connection_t *conn,xcb_drawable_t drawable,
    xcb_gcontext_t gc,uint16_t tw,uint16_t th,uint16_t sx,uint16_t sy,uint16_t sw,uint16_t sh,
    int16_t dx,int16_t dy,uint8_t depth,uint8_t format,uint8_t send,xcb_shm_seg_t seg,uint32_t offset)
{
    ownership_transport *t=transport_conn(conn); transport_live(t); EDIT_ASSERT(!t->server_owned);
    (void)drawable; (void)gc; (void)tw; (void)th; (void)sx; (void)sy; (void)sw; (void)sh;
    (void)dx; (void)dy; (void)depth; (void)format; (void)send; (void)seg; (void)offset;
    return (xcb_void_cookie_t){1};
}
static xcb_void_cookie_t transport_fence(xcb_connection_t *conn,xcb_sync_fence_t fence)
{ transport_live(transport_conn(conn)); (void)fence; return (xcb_void_cookie_t){1}; }
static xcb_void_cookie_t transport_await(xcb_connection_t *conn,uint32_t count,const xcb_sync_fence_t *fences)
{ transport_live(transport_conn(conn)); (void)count; (void)fences; return (xcb_void_cookie_t){1}; }
static xcb_sync_query_fence_cookie_t transport_query(xcb_connection_t *conn,xcb_sync_fence_t fence)
{ transport_live(transport_conn(conn)); (void)fence; return (xcb_sync_query_fence_cookie_t){1}; }
static xcb_void_cookie_t transport_present(xcb_connection_t *conn,xcb_window_t window,xcb_pixmap_t pixmap,
    uint32_t serial,xcb_xfixes_region_t valid,xcb_xfixes_region_t update,int16_t x,int16_t y,
    xcb_randr_crtc_t crtc,xcb_sync_fence_t wait,xcb_sync_fence_t idle,uint32_t options,
    uint64_t msc,uint64_t divisor,uint64_t remainder,uint32_t count,const xcb_present_notify_t *notify)
{
    ownership_transport *t=transport_conn(conn); transport_live(t); EDIT_ASSERT(!t->server_owned);
    t->server_owned=true; t->native_frame=serial; t->native_ready=t->native_seen=0;
    (void)window; (void)pixmap; (void)valid; (void)update; (void)x; (void)y; (void)crtc;
    (void)wait; (void)idle; (void)options; (void)msc; (void)divisor; (void)remainder; (void)count; (void)notify;
    return (xcb_void_cookie_t){1};
}
static void transport_unregister(xcb_connection_t *conn,xcb_special_event_t *special)
{ transport_live(transport_conn(conn)); (void)special; }
static void transport_disconnect(xcb_connection_t *conn)
{
    ownership_transport *t=transport_conn(conn); transport_live(t);
    for (size_t i=0;i<FUZZ_LEASES;i++) EDIT_ASSERT(t->phase[i]!=1 && t->phase[i]!=2);
    t->destroyed=true;
}
static xcb_void_cookie_t transport_free(xcb_connection_t *conn,uint32_t id)
{ transport_live(transport_conn(conn)); (void)id; return (xcb_void_cookie_t){1}; }
static int transport_detach(const void *mem) { free((void *)(uintptr_t)mem); return 0; }
static xcb_void_cookie_t transport_clear(xcb_connection_t *conn,xcb_drawable_t drawable,
    xcb_gcontext_t gc,uint32_t count,const xcb_rectangle_t *rectangles)
{
    ownership_transport *t=transport_conn(conn); transport_live(t); EDIT_ASSERT(!t->server_owned);
    (void)drawable; (void)gc; (void)count; (void)rectangles; return (xcb_void_cookie_t){1};
}
static uint32_t transport_id(xcb_connection_t *conn)
{ transport_live(transport_conn(conn)); return 1; }
static xcb_void_cookie_t transport_region(xcb_connection_t *conn,xcb_xfixes_region_t region,
    uint32_t count,const xcb_rectangle_t *rectangles)
{ transport_live(transport_conn(conn)); (void)region; (void)count; (void)rectangles; return (xcb_void_cookie_t){1}; }
#define render_cpu_backend ownership_cpu_backend
#define raster_last_present ownership_last_present
#define raster_frame_metrics ownership_frame_metrics
#define raster_completion_fd ownership_completion_fd
#define raster_poll_completions ownership_poll_completions
#define raster_completion_timeout ownership_completion_timeout
#define raster_set_caret_only ownership_set_caret_only
#define work_submit transport_submit
#define work_submit_batch transport_batch
#define work_publish transport_publish
#define work_should_stop transport_stop
#define work_handle_finished transport_finished
#define work_cancel transport_cancel
#define nanosleep transport_sleep
#define poll transport_poll
#define xcb_shm_put_image transport_put
#define xcb_flush transport_flush
#define xcb_get_file_descriptor transport_fd
#define xcb_connection_has_error transport_error
#define xcb_poll_for_reply transport_reply
#define xcb_poll_for_special_event transport_special
#define xcb_poll_for_event transport_event
#define xcb_sync_reset_fence transport_fence
#define xcb_sync_trigger_fence transport_fence
#define xcb_sync_await_fence transport_await
#define xcb_sync_query_fence transport_query
#define xcb_present_pixmap transport_present
#define xcb_unregister_for_special_event transport_unregister
#define xcb_disconnect transport_disconnect
#define xcb_shm_detach transport_free
#define xcb_sync_destroy_fence transport_free
#define xcb_free_gc transport_free
#define xcb_free_pixmap transport_free
#define shmdt transport_detach
#define xcb_poly_fill_rectangle transport_clear
#define xcb_generate_id transport_id
#define xcb_xfixes_create_region transport_region
#define xcb_xfixes_destroy_region transport_free
#ifdef RASTER_FUZZ_OLD
#include "../build/raster-before-s9.c"
#else
#include "../src/raster/raster.c"
#endif
#undef work_should_stop
#undef work_handle_finished
#undef work_cancel
#undef nanosleep
#undef poll

static void ownership_hook(void *user,uint32_t frame,uint64_t ns)
{ transport_live(user); EDIT_ASSERT(frame && ns); }
static void ownership_run(ownership_transport *t)
{
    for (size_t i=0;i<FUZZ_LEASES;i++) if (t->phase[i]==1) {
        transport_live(t); t->checks=t->attempts=0;
        work_ctx c={.pool=&t->pool,.slot=&t->pool.slots[i],.epoch=t->epochs[i],
            .generation=t->jobs[i].generation,.arg=t->jobs[i].arg};
        t->jobs[i].fn(&c);
        t->phase[i]=t->cancelled[i] || t->checks>32u ? 3u : 2u;
    }
}
static void ownership_drain(ownership_transport *t,render_backend *b)
{
    for (size_t i=0;i<t->messages_count;i++) {
        work_msg *m=&t->messages[i]; render_event ev={RENDER_EVENT_WORK,m->generation,0,m};
        (void)render_backend_event(b,&ev);
    }
    t->messages_count=0;
}
static void ownership_open(ownership_transport *t,cpu_state *st,render_backend *b)
{
    t->shutdown=t->destroyed=t->server_owned=false; t->capacity=FUZZ_MESSAGES;
    *st=(cpu_state){.up=true,.shmid=-1,.conn=(xcb_connection_t *)t,.pool=&t->pool,
        .pixmap=42,.gc=1,.max_w=16,.max_h=16,.stride_px=16,.max_cells=4,.max_strips=2};
    st->cells=calloc(4,sizeof *st->cells); st->strips=calloc(2,sizeof *st->strips);
    st->glyphs=calloc(1,sizeof *st->glyphs); st->pages=calloc(1,sizeof *st->pages);
    st->pix=calloc(256,sizeof *st->pix);
    EDIT_ASSERT(st->cells && st->strips && st->glyphs && st->pages && st->pix);
    st->scene=(raster_scene){.dims={2,2,4,4},.cells=st->cells,.glyphs=st->glyphs,.pages=st->pages};
    *b=(render_backend){0}; EDIT_ASSERT(ownership_cpu_backend(b)==RENDER_OK);
    b->state=st; b->initialized=true; b->full_required=true;
    b->config=(render_config){.dims={2,2,4,4},.max_cells=4,.max_width=16,.max_height=16,
        .hooks={ownership_hook,ownership_hook,t}};
}
static void ownership_close(ownership_transport *t,render_backend *b)
{
    t->shutdown=true; render_backend_shutdown(b); EDIT_ASSERT(t->destroyed);
    render_event ev={RENDER_EVENT_PRESENT_COMPLETE,1,0,NULL};
    EDIT_ASSERT(render_backend_event(b,&ev)==RENDER_ERR_STATE);
    t->messages_count=0;
}
static void ownership_fuzz(const uint8_t *data,size_t size)
{
    (void)transport_clear;
    ownership_transport *t=calloc(1,sizeof *t); EDIT_ASSERT(t);
    cpu_state st; render_backend b; ownership_open(t,&st,&b);
    render_cell cells[4]; for (size_t i=0;i<4;i++) cells[i]=(render_cell){0,RENDER_NO_SLOT,0,0x123456,0,0};
    uint64_t bits=0; render_grid g; EDIT_ASSERT(render_grid_init(&g,b.config.dims,cells,4,&bits,1)==RENDER_OK);
    render_strip full={0,2}; uint32_t frame=1;
    /* Guaranteed regression schedule: every job has published but none has
     * returned when the next typing frame is accepted. */
    EDIT_ASSERT(render_frame_begin(&g,frame++)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
    EDIT_ASSERT(render_backend_submit(&b,&g,&full,1)==RENDER_OK);
    ownership_run(t); ownership_drain(t,&b);
    EDIT_ASSERT(render_backend_present(&b,g.frame_id)==RENDER_OK);
    t->native_ready=7; ownership_run(t); ownership_drain(t,&b); EDIT_ASSERT(!b.active && !t->server_owned);
    EDIT_ASSERT(render_frame_begin(&g,frame++)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
    EDIT_ASSERT(render_backend_submit(&b,&g,&full,1)==RENDER_OK);
    for (size_t i=0;i<size && i<256;i++) {
        switch (data[i]&15u) {
        case 0: ownership_run(t); break;
        case 1: ownership_drain(t,&b); break;
        case 2: if (b.active && !b.presented) (void)render_backend_present(&b,g.frame_id); break;
        case 3: t->native_ready=7; break;
        case 4:
            if (!b.active) {
                EDIT_ASSERT(render_frame_begin(&g,frame++)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
                (void)render_backend_submit(&b,&g,&full,1);
            }
            break;
        case 5:
            for (size_t k=0;k<FUZZ_LEASES;k++) if (t->phase[k]==2) { t->phase[k]=3; break; }
            break;
        case 6: {
            bool active=b.active; render_event ev={RENDER_EVENT_PRESENT_COMPLETE,b.active_frame,0,NULL};
            EDIT_ASSERT(render_backend_event(&b,&ev)==RENDER_ERR_UNSUPPORTED && b.active==active);
            break;
        }
        case 7: {
            bool active=b.active;
            work_msg m={.kind=3,.generation=b.active_frame,.slot_=15,.epoch_=UINT32_MAX};
            render_event ev={RENDER_EVENT_WORK,b.active_frame,0,&m};
            (void)render_backend_event(&b,&ev); EDIT_ASSERT(b.active==active); break;
        }
        case 8: t->capacity=0; ownership_run(t); t->capacity=FUZZ_MESSAGES; break;
        case 9: case 10:
            ownership_close(t,&b);
            if ((data[i]&15u)==10) {
                render_config invalid={0};
                b.state=&st; EDIT_ASSERT(cpu_init(&b,&invalid)==RENDER_ERR_INIT);
            }
            ownership_open(t,&st,&b); break;
        case 11:
            for (size_t k=0;k<FUZZ_LEASES;k++) if (t->phase[k]==1) { t->cancelled[k]=true; t->phase[k]=3; }
            break;
        case 12: t->native_ready=(unsigned)data[i]>>4u & 7u; break; /* missing idle/fence/PIXMAP */
        case 13: if (!b.active) (void)render_backend_resize(&b,b.config.dims); break;
        case 14:
            if (b.active) EDIT_ASSERT(render_backend_submit(&b,&g,&full,1)==RENDER_ERR_BUSY);
            break;
        default:
#ifndef RASTER_FUZZ_OLD
            if (!b.active && st.snapshot_valid) {
                EDIT_ASSERT(render_frame_begin(&g,frame++)==RENDER_OK && render_mark_rows(&g,0,1)==RENDER_OK);
                cells[0].attrs ^= RENDER_ATTR_CURSOR; cells[0].bg ^= 1u;
                render_strip one={0,1}; ownership_set_caret_only(&b,true);
                int rc=render_backend_submit(&b,&g,&one,1);
                if (rc==RENDER_OK) {
                    EDIT_ASSERT(st.inline_frame && render_backend_present(&b,g.frame_id)==RENDER_OK);
                    t->native_ready=(unsigned)data[i]>>4u & 7u;
                    st.completion_deadline=1; /* deterministic clock advance */
                    rc=ownership_poll_completions(&b);
                    EDIT_ASSERT(t->native_ready==7 ? rc==RENDER_OK && !b.active : rc==RENDER_ERR_DEVICE && b.active);
                }
                ownership_set_caret_only(&b,false);
            }
#else
            ownership_drain(t,&b);
#endif
            break;
        }
    }
    ownership_close(t,&b); free(t);
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    ownership_fuzz(data,size);
    return kernel_fuzz(data,size);
}
