/* Bare reference renderer: one baked glyph, editor's CPU backend/Present path.
 * Metadata and CSV are handled only after the key's submit has returned. */
#include "refwin_protocol.h"
#include "base/base.h"
#include "font/font.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include "work/work.h"
#include "x11/plat.h"
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <xcb/present.h>
#include <xcb/xcb.h>

typedef struct refwin_state {
    plat platform;
    work_pool pool;
    render_backend backend;
    render_config config;
    edit_arena arena;
    void *backend_state;
    render_grid grid;
    render_cell cell;
    uint64_t dirty;
    render_glyph glyphs[95];
    render_atlas_page page;
    uint64_t t4, t5, t6, platform_t6;
    uint32_t keycode, notified_pair, last_pair, completed;
    bool key_pending;
    int error, init_result;
    xcb_atom_t injection, complete, notify;
} refwin_state;

static void refwin_done(void *u, uint32_t id, uint64_t ns)
{
    refwin_state *s = u;
    if (id != s->grid.frame_id || s->t5) s->error = 1;
    else s->t5 = ns;
}
static void refwin_complete(void *u, uint32_t id, uint64_t ns)
{
    refwin_state *s = u;
    if (id != s->grid.frame_id || s->t6) s->error = 1;
    else s->t6 = ns;
}
static void *refwin_init_worker(void *u)
{
    refwin_state *s = u;
    (void)trace_thread_register();
    s->init_result = render_backend_init(&s->backend, &s->config, s->backend_state, s->backend.info.state_size);
    return NULL;
}
static void refwin_work(const work_msg *m, void *u)
{
    refwin_state *s = u;
    render_event e = {RENDER_EVENT_WORK,m->generation,0,m};
    if (render_backend_event(&s->backend, &e) != RENDER_OK) s->error = 1;
}
static int refwin_submit(refwin_state *s, bool changed)
{
    if (s->grid.frame_id == UINT32_MAX || s->backend.active || s->key_pending) return -1;
    edit_malloc_guard_begin();
    int rc = render_frame_begin(&s->grid, s->grid.frame_id + 1u);
    /* Same key toggles foreground/background, so every injection changes pixels. */
    if (changed) {
        uint32_t color = s->cell.fg; s->cell.fg = s->cell.bg; s->cell.bg = color;
    }
    if (!rc) rc = render_mark_full(&s->grid);
    render_strip strip = {0,1};
    s->t4 = s->t5 = s->t6 = s->platform_t6 = 0;
    if (!rc) rc = render_backend_submit(&s->backend, &s->grid, &strip, 1);
    size_t allocations = edit_malloc_guard_end();
    if (rc || allocations) return -1;
    s->key_pending = changed;
    return 0;
}
static int refwin_pump(refwin_state *s)
{
    xcb_connection_t *c = s->platform.conn;
    xcb_generic_event_t *e;
    while ((e = xcb_poll_for_event(c)) != NULL) {
        uint8_t kind = e->response_type & UINT8_C(0x7f);
        if (!kind) s->error = 1;
        else if (kind == XCB_KEY_PRESS) {
            xcb_key_press_event_t *key = (xcb_key_press_event_t *)e;
            if (key->detail != s->keycode || refwin_submit(s, true)) s->error = 1;
        } else if (kind == XCB_CLIENT_MESSAGE) {
            xcb_client_message_event_t *msg = (xcb_client_message_event_t *)e;
            if (msg->type == s->notify && msg->format == 32) {
                if (!s->key_pending || s->notified_pair || !msg->data.data32[0] ||
                    msg->data.data32[0] <= s->last_pair) s->error = 1;
                else s->notified_pair = msg->data.data32[0];
            } else if (msg->type == s->platform.wm_protocols &&
                       msg->data.data32[0] == s->platform.wm_delete) s->error = 1;
        } else if (kind == XCB_GE_GENERIC) {
            xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)e;
            if (ge->extension == s->platform.present_opcode && ge->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
                xcb_present_complete_notify_event_t *complete = (xcb_present_complete_notify_event_t *)e;
                if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP && complete->serial == s->grid.frame_id && !s->platform_t6)
                    s->platform_t6 = trace_now_ns();
            }
        } else if (kind == XCB_DESTROY_NOTIFY) s->error = 1;
        free(e);
        if (s->error) return -1;
    }
    (void)work_mailbox_drain(&s->pool, refwin_work, s);
    if (s->error || xcb_connection_has_error(c)) return -1;
    if (s->backend.active && !s->backend.presented) {
        int rc = render_backend_present(&s->backend, s->grid.frame_id);
        if (rc != RENDER_OK && rc != RENDER_ERR_BUSY) return -1;
        if (rc == RENDER_OK) s->t4 = s->backend.submitted_ns;
    }
    struct pollfd fds[2] = {{xcb_get_file_descriptor(c),POLLIN,0},{work_pool_eventfd(&s->pool),POLLIN,0}};
    if (poll(fds, 2, 10) < 0 && errno != EINTR) return -1;
    return 0;
}
static int refwin_finish(refwin_state *s, FILE *csv)
{
    if (!s->key_pending || s->backend.active || !s->notified_pair) return 0;
    refproto_row row;
    if (refproto_get(s->platform.conn, s->platform.win, s->injection, &row) ||
        row.pair != s->notified_pair || !row.inject || !row.msc || !row.period ||
        row.phase >= row.period || row.actual >= row.period ||
        s->t4 < row.inject || s->t5 < s->t4 || s->t6 < s->t4) return -1;
    row.frame = s->grid.frame_id; row.t4 = s->t4; row.t5 = s->t5; row.t6 = s->t6;
    /* The editor trace's existing first-occurrence rule takes the earliest
     * matching platform/backend observation. Apply the same rule here. */
    if (s->platform_t6 && s->platform_t6 < row.t6) row.t6 = s->platform_t6;
    if (row.t6 < row.t4) return -1;
    if (refproto_csv(csv, "reference", &row) ||
        refproto_set(s->platform.conn, s->platform.win, s->complete, &row)) return -1;
    s->last_pair = row.pair; s->completed++; s->notified_pair = 0; s->key_pending = false;
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t pairs = 0, keycode = 38, glyph = 97, ready_fd = 1;
    const char *csv_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("usage: refwin --csv PATH [--pairs N] [--keycode N] [--glyph ASCII] [--ready-fd FD]\n"
                 "READY window id is written to ready-fd after the startup Present completes."); return 0;
        }
        if (i+1 >= argc) return 2;
        const char *option = argv[i++], *value = argv[i];
        if (!strcmp(option, "--csv")) csv_path = value;
        else if (!strcmp(option, "--pairs")) { if (refproto_number(value,UINT32_MAX-1u,&pairs)) return 2; }
        else if (!strcmp(option, "--keycode")) { if (refproto_number(value,255,&keycode) || keycode < 8) return 2; }
        else if (!strcmp(option, "--glyph")) { if (refproto_number(value,126,&glyph) || glyph < 33) return 2; }
        else if (!strcmp(option, "--ready-fd")) { if (refproto_number(value,INT32_MAX,&ready_fd)) return 2; }
        else return 2;
    }
    if (!csv_path || !strcmp(csv_path,"-")) return 2;
    FILE *csv = fopen(csv_path,"w");
    if (!csv) return 1;
    int rc = 1; bool pool_ready = false, platform_ready = false;
    refwin_state s = {0}; s.keycode = (uint32_t)keycode;
    trace_init(); (void)trace_thread_register();
    font_cell dims = font_ascii_cell();
    plat_config pc = {"P2.6 reference",dims.cell_w,dims.cell_h,false,-1,0};
    if (plat_init(&s.platform,&pc)) goto done;
    platform_ready = true;
    if (!s.platform.present_ok || work_pool_init(&s.pool,1,4)) goto done;
    pool_ready = true;
    s.injection = refproto_atom(s.platform.conn,"_EDIT_REF_INJECT");
    s.complete = refproto_atom(s.platform.conn,"_EDIT_REF_COMPLETE");
    s.notify = refproto_atom(s.platform.conn,"_EDIT_REF_NOTIFY");
    xcb_atom_t bypass = refproto_atom(s.platform.conn,"_NET_WM_BYPASS_COMPOSITOR");
    uint32_t one = 1;
    if (!s.injection || !s.complete || !s.notify || !bypass ||
        !refproto_checked(s.platform.conn,xcb_change_property_checked(s.platform.conn,
            XCB_PROP_MODE_REPLACE,s.platform.win,bypass,XCB_ATOM_CARDINAL,32,1,&one))) goto done;
    size_t pixel_len = 0; const uint8_t *pixels = font_ascii_pixels(&pixel_len);
    s.page = (render_atlas_page){pixels,pixel_len,(size_t)dims.cell_w*95u,dims.cell_w*95u,dims.cell_h};
    for (uint32_t i = 0; i < 95; i++) s.glyphs[i] = (render_glyph){i+32u,0,i*dims.cell_w,0,dims.cell_w,dims.cell_h};
    s.cell = (render_cell){(uint32_t)glyph,(uint32_t)glyph-32u,0xffffff,0,0,0};
    render_dims rd = {1,1,dims.cell_w,dims.cell_h};
    if (render_grid_init(&s.grid,rd,&s.cell,1,&s.dirty,1) || render_cpu_backend(&s.backend)) goto done;
    s.grid.pages = &s.page; s.grid.page_count = 1; s.grid.glyphs = s.glyphs; s.grid.glyph_count = 95;
    if (edit_arena_init(&s.arena,s.backend.info.state_size+s.backend.info.state_align)) goto done;
    s.backend_state = edit_arena_alloc(&s.arena,s.backend.info.state_size,s.backend.info.state_align);
    if (!s.backend_state) goto done;
    s.config = (render_config){.dims=rd,.max_width=dims.cell_w,.max_height=dims.cell_h,.max_cells=1,
        .max_glyphs=95,.max_pages=1,.max_atlas_bytes=pixel_len,.platform=&s.platform,.workers=&s.pool,
        .hooks={refwin_done,refwin_complete,&s}};
    pthread_t thread;
    if (pthread_create(&thread,NULL,refwin_init_worker,&s) || pthread_join(thread,NULL) || s.init_result) goto done;
    plat_map(&s.platform);
    if (refwin_submit(&s,false)) goto done;
    uint64_t deadline = trace_now_ns()+UINT64_C(10000000000);
    while (s.backend.active) if (refwin_pump(&s) || trace_now_ns()>deadline) goto done;
    if (fputs(REFPROTO_HEADER,csv) == EOF || fflush(csv) ||
        dprintf((int)ready_fd,"%" PRIu32 "\n",s.platform.win) < 0) goto done;
    fprintf(stderr,"refwin: cpu-raster, one glyph; compositor bypass requested (not guaranteed)\n");
    deadline = trace_now_ns()+UINT64_C(10000000000);
    while (!pairs || s.completed < pairs) {
        uint32_t before = s.completed;
        if (refwin_pump(&s) || refwin_finish(&s,csv)) goto done;
        if (s.completed != before) deadline = trace_now_ns()+UINT64_C(10000000000);
        if (trace_now_ns()>deadline) { fprintf(stderr,"refwin: input/frame timeout\n"); goto done; }
    }
    /* Keep the acknowledgement available until the injector consumes it. */
    xcb_atom_t consumed = refproto_atom(s.platform.conn,"_EDIT_REF_CONSUMED");
    if (!consumed) goto done;
    for (;;) {
        refproto_row ack;
        if (!refproto_get(s.platform.conn,s.platform.win,consumed,&ack) && ack.pair == s.last_pair) break;
        if (trace_now_ns()>deadline || refwin_pump(&s)) goto done;
    }
    rc = 0;
done:
    render_backend_shutdown(&s.backend);
    if (pool_ready) work_pool_shutdown(&s.pool);
    edit_arena_free(&s.arena);
    if (platform_ready) plat_shutdown(&s.platform);
    if (fclose(csv)) rc = 1;
    if (rc) fprintf(stderr,"refwin: failed (initialization, protocol, allocation guard, or frame)\n");
    return rc;
}
