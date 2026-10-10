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
    const char *error;
    int init_result, last_result;
    size_t allocations;
    refproto_row metadata;
    xcb_atom_t injection, complete, notify;
} refwin_state;

static void refwin_done(void *u, uint32_t id, uint64_t ns)
{
    refwin_state *s = u;
    if (id != s->grid.frame_id) s->error = "T5 hook frame identity mismatch";
    else if (s->t5) s->error = "duplicate T5 hook";
    else s->t5 = ns;
}
static void refwin_complete(void *u, uint32_t id, uint64_t ns)
{
    refwin_state *s = u;
    if (id != s->grid.frame_id) s->error = "T6 hook frame identity mismatch";
    else if (s->t6) s->error = "duplicate T6 hook";
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
    s->last_result = render_backend_event(&s->backend, &e);
    if (s->last_result != RENDER_OK) s->error = "render backend work event failed";
}
static int refwin_submit(refwin_state *s, bool changed)
{
    if (s->grid.frame_id == UINT32_MAX) { s->error = "frame serial exhausted"; return -1; }
    if (s->backend.active || s->key_pending) { s->error = "key received while preceding frame/pair is pending"; return -1; }
    edit_malloc_guard_begin();
    int rc = render_frame_begin(&s->grid, s->grid.frame_id + 1u);
    if (rc) s->error = "render_frame_begin failed";
    /* Same key toggles foreground/background, so every injection changes pixels. */
    if (changed) {
        uint32_t color = s->cell.fg; s->cell.fg = s->cell.bg; s->cell.bg = color;
    }
    if (!rc) { rc = render_mark_full(&s->grid); if (rc) s->error = "render_mark_full failed"; }
    render_strip strip = {0,1};
    s->t4 = s->t5 = s->t6 = s->platform_t6 = 0;
    if (!rc) { rc = render_backend_submit(&s->backend, &s->grid, &strip, 1); if (rc) s->error = "render_backend_submit failed"; }
    size_t allocations = edit_malloc_guard_end();
    s->last_result = rc; s->allocations = allocations;
    if (allocations) { s->error = "typing allocation guard failed"; return -1; }
    if (rc) return -1;
    s->key_pending = changed;
    return 0;
}
static int refwin_pump(refwin_state *s)
{
    xcb_connection_t *c = s->platform.conn;
    xcb_generic_event_t *e;
    while ((e = xcb_poll_for_event(c)) != NULL) {
        uint8_t kind = e->response_type & UINT8_C(0x7f);
        if (!kind) {
            xcb_generic_error_t *xe = (xcb_generic_error_t *)e;
            s->last_result = xe->error_code; s->error = "asynchronous X request error";
        }
        else if (kind == XCB_KEY_PRESS) {
            xcb_key_press_event_t *key = (xcb_key_press_event_t *)e;
            if (key->detail != s->keycode) { s->last_result = key->detail; s->error = "unexpected keycode"; }
            else (void)refwin_submit(s,true);
        } else if (kind == XCB_CLIENT_MESSAGE) {
            xcb_client_message_event_t *msg = (xcb_client_message_event_t *)e;
            if (msg->type == s->notify && msg->format == 32) {
                if (!s->key_pending || s->notified_pair || !msg->data.data32[0] ||
                    msg->data.data32[0] <= s->last_pair) s->error = "metadata ClientMessage missing key/pair, duplicate or backwards pair";
                else s->notified_pair = msg->data.data32[0];
            } else if (msg->type == s->platform.wm_protocols &&
                       msg->data.data32[0] == s->platform.wm_delete) s->error = "WM_DELETE_WINDOW before pairs completed";
        } else if (kind == XCB_GE_GENERIC) {
            xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)e;
            if (ge->extension == s->platform.present_opcode && ge->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
                xcb_present_complete_notify_event_t *complete = (xcb_present_complete_notify_event_t *)e;
                if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP && complete->serial == s->grid.frame_id && !s->platform_t6)
                    s->platform_t6 = trace_now_ns();
            }
        } else if (kind == XCB_DESTROY_NOTIFY) s->error = "target window destroyed";
        free(e);
        if (s->error) return -1;
    }
    (void)work_mailbox_drain(&s->pool, refwin_work, s);
    if (xcb_connection_has_error(c)) { s->last_result = xcb_connection_has_error(c); s->error = "X connection lost"; }
    if (s->error) return -1;
    if (s->backend.active && !s->backend.presented) {
        int rc = render_backend_present(&s->backend, s->grid.frame_id);
        if (rc != RENDER_OK && rc != RENDER_ERR_BUSY) { s->last_result = rc; s->error = "render backend present failed"; return -1; }
        if (rc == RENDER_OK) s->t4 = s->backend.submitted_ns;
    }
    struct pollfd fds[2] = {{xcb_get_file_descriptor(c),POLLIN,0},{work_pool_eventfd(&s->pool),POLLIN,0}};
    if (poll(fds, 2, 10) < 0 && errno != EINTR) { s->last_result = errno; s->error = "X/work poll failed"; return -1; }
    return 0;
}
static int refwin_finish(refwin_state *s, FILE *csv)
{
    if (!s->key_pending || s->backend.active || !s->notified_pair) return 0;
    refproto_row row;
    if (refproto_get(s->platform.conn,s->platform.win,s->injection,&row)) {
        s->error = "injection metadata property missing/malformed"; return -1;
    }
    s->metadata = row;
    if (row.pair != s->notified_pair || !row.inject || !row.msc || !row.period) {
        s->error = "injection metadata pair/identity/clock mismatch"; return -1;
    }
    if (row.phase >= row.period || row.actual >= row.period) {
        s->error = "injection phase slipped outside measured refresh"; return -1;
    }
    if (s->t4 < row.inject || s->t5 < s->t4 || s->t6 < s->t4) {
        s->error = "reference frame missing/nonmonotonic T4/T5/T6"; return -1;
    }
    row.frame = s->grid.frame_id; row.t4 = s->t4; row.t5 = s->t5; row.t6 = s->t6;
    if (s->platform_t6 && s->platform_t6 < row.t6) row.t6 = s->platform_t6;
    if (row.t6 < row.t4) { s->error = "platform Present completion precedes T4"; return -1; }
    if (refproto_csv(csv,"reference",&row)) { s->last_result = errno; s->error = "reference CSV write/flush failed"; return -1; }
    if (refproto_set(s->platform.conn,s->platform.win,s->complete,&row)) {
        s->error = "write reference completion acknowledgement failed"; return -1;
    }
    s->last_pair = row.pair; s->completed++; s->notified_pair = 0; s->key_pending = false;
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t pairs = 0, keycode = 38, glyph = 97, ready_fd = 1, window = 0, timeout_ms = 10000;
    bool dry_run = false, managed = false, synthetic_clock = false;
    const char *csv_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("usage: refwin [--csv PATH | --dry-run [--window ID]] [--pairs N]\n"
                 "  [--keycode N] [--glyph ASCII] [--ready-fd FD] [--timeout-ms N] [--managed]\n"
                 "  [--synthetic-clock] (Xvfb only; rate stability checks disabled, TRACK only)\n"
                 "Default bare window uses override-redirect; --managed delegates mapping/placement to the WM.\n"
                 "READY follows preflight and startup Present. --dry-run sends no keys/writes no CSV.\n"
                 "--dry-run --window ID checks an existing placed window; otherwise checks a temporary reference window."); return 0;
        }
        if (!strcmp(argv[i],"--synthetic-clock")) { synthetic_clock = true; continue; }
        if (!strcmp(argv[i],"--dry-run")) { dry_run = true; continue; }
        if (!strcmp(argv[i],"--managed")) { managed = true; continue; }
        if (i+1 >= argc) { fprintf(stderr,"refwin: missing value for %s\n",argv[i]); return 2; }
        const char *option = argv[i++], *value = argv[i];
        uint64_t *dest = NULL, max = UINT32_MAX, min = 0;
        if (!strcmp(option,"--csv")) csv_path = value;
        else if (!strcmp(option,"--pairs")) { dest = &pairs; max = UINT32_MAX-1u; }
        else if (!strcmp(option,"--keycode")) { dest = &keycode; min = 8; max = 255; }
        else if (!strcmp(option,"--glyph")) { dest = &glyph; min = 33; max = 126; }
        else if (!strcmp(option,"--ready-fd")) { dest = &ready_fd; max = INT32_MAX; }
        else if (!strcmp(option,"--window")) { dest = &window; min = 1; }
        else if (!strcmp(option,"--timeout-ms")) { dest = &timeout_ms; min = 1; max = 600000; }
        else { fprintf(stderr,"refwin: unknown option %s\n",option); return 2; }
        if (dest && (refproto_number(value,max,dest) || *dest < min)) {
            fprintf(stderr,"refwin: invalid %s=%s\n",option,value); return 2;
        }
    }
    if ((!dry_run && (!csv_path || !strcmp(csv_path,"-"))) || (window && !dry_run)) {
        fprintf(stderr,"refwin: --csv PATH required; --window is only valid with --dry-run\n"); return 2;
    }
    refproto_display probe;
    if (refproto_display_open(&probe,"refwin",true)) { refproto_display_close(&probe); return 1; }
    probe.synthetic_clock = synthetic_clock;
    probe.timeout_ns = timeout_ms*UINT64_C(1000000);
    if (refproto_keycode(&probe,(xcb_window_t)window,(uint8_t)keycode)) { refproto_display_close(&probe); return 1; }
    if (window) {
        refproto_target target = {.window=(xcb_window_t)window};
        int preflight = refproto_preflight(&probe,&target);
        refproto_display_close(&probe);
        if (!preflight) fprintf(stderr,"refwin: dry-run PASS (no injection, no CSV)\n");
        return preflight ? 1 : 0;
    }
    FILE *csv = NULL;
    int rc = 1; bool pool_ready = false, platform_ready = false;
    refwin_state s = {0}; s.keycode = (uint32_t)keycode;
    trace_init(); (void)trace_thread_register();
    font_cell dims = font_ascii_cell();
    plat_config pc = {"P2.6 reference",dims.cell_w,dims.cell_h,false,-1,0};
    s.last_result = plat_init(&s.platform,&pc);
    if (s.last_result) { s.error = "plat_init failed (see platform return code)"; goto done; }
    platform_ready = true;
    if (!s.platform.present_ok) { s.error = "platform Present unavailable"; goto done; }
    if (!refproto_checked(s.platform.conn,xcb_change_window_attributes_checked(s.platform.conn,s.platform.win,
        XCB_CW_OVERRIDE_REDIRECT,(uint32_t[]){managed ? 0u : 1u}))) {
        s.error = "set reference override-redirect attribute failed"; goto done;
    }
    if (!refproto_checked(s.platform.conn,xcb_map_window_checked(s.platform.conn,s.platform.win))) {
        s.error = "map reference window request failed"; goto done;
    }
    /* Managed windows can be reparented/mapped asynchronously by the WM. */
    uint64_t map_deadline = trace_now_ns()+timeout_ms*UINT64_C(1000000);
    for (;;) {
        xcb_get_window_attributes_reply_t *attrs = xcb_get_window_attributes_reply(s.platform.conn,
            xcb_get_window_attributes(s.platform.conn,s.platform.win),NULL);
        if (!attrs) { s.error = "GetWindowAttributes after MapWindow failed"; goto done; }
        bool viewable = attrs->map_state == XCB_MAP_STATE_VIEWABLE; free(attrs);
        if (viewable) break;
        if (trace_now_ns() >= map_deadline) { s.error = "map timeout: WM has not made reference viewable"; goto done; }
        struct timespec delay = {0,1000000}; (void)nanosleep(&delay,NULL);
    }
    refproto_target target = {.window=s.platform.win};
    if (refproto_preflight(&probe,&target)) goto done;
    refproto_display_close(&probe);
    s.last_result = work_pool_init(&s.pool,1,4);
    if (s.last_result) { s.error = "work pool initialization failed"; goto done; }
    pool_ready = true;
    s.injection = refproto_atom(s.platform.conn,"_EDIT_REF_INJECT");
    s.complete = refproto_atom(s.platform.conn,"_EDIT_REF_COMPLETE");
    s.notify = refproto_atom(s.platform.conn,"_EDIT_REF_NOTIFY");
    xcb_atom_t bypass = refproto_atom(s.platform.conn,"_NET_WM_BYPASS_COMPOSITOR");
    uint32_t one = 1;
    if (!s.injection) { s.error = "intern _EDIT_REF_INJECT failed"; goto done; }
    if (!s.complete) { s.error = "intern _EDIT_REF_COMPLETE failed"; goto done; }
    if (!s.notify) { s.error = "intern _EDIT_REF_NOTIFY failed"; goto done; }
    if (!bypass) { s.error = "intern _NET_WM_BYPASS_COMPOSITOR failed"; goto done; }
    if (!refproto_checked(s.platform.conn,xcb_change_property_checked(s.platform.conn,
            XCB_PROP_MODE_REPLACE,s.platform.win,bypass,XCB_ATOM_CARDINAL,32,1,&one))) { s.error = "write _NET_WM_BYPASS_COMPOSITOR failed"; goto done; }
    size_t pixel_len = 0; const uint8_t *pixels = font_ascii_pixels(&pixel_len);
    s.page = (render_atlas_page){pixels,pixel_len,(size_t)dims.cell_w*95u,dims.cell_w*95u,dims.cell_h};
    for (uint32_t i = 0; i < 95; i++) s.glyphs[i] = (render_glyph){i+32u,0,i*dims.cell_w,0,dims.cell_w,dims.cell_h};
    s.cell = (render_cell){(uint32_t)glyph,(uint32_t)glyph-32u,0xffffff,0,0,0};
    render_dims rd = {1,1,dims.cell_w,dims.cell_h};
    s.last_result = render_grid_init(&s.grid,rd,&s.cell,1,&s.dirty,1);
    if (s.last_result) { s.error = "render grid initialization failed"; goto done; }
    s.last_result = render_cpu_backend(&s.backend);
    if (s.last_result) { s.error = "CPU backend selection failed"; goto done; }
    s.grid.pages = &s.page; s.grid.page_count = 1; s.grid.glyphs = s.glyphs; s.grid.glyph_count = 95;
    if (edit_arena_init(&s.arena,s.backend.info.state_size+s.backend.info.state_align)) { s.error = "backend arena reservation failed"; goto done; }
    s.backend_state = edit_arena_alloc(&s.arena,s.backend.info.state_size,s.backend.info.state_align);
    if (!s.backend_state) { s.error = "backend arena allocation failed"; goto done; }
    s.config = (render_config){.dims=rd,.max_width=dims.cell_w,.max_height=dims.cell_h,.max_cells=1,
        .max_glyphs=95,.max_pages=1,.max_atlas_bytes=pixel_len,.platform=&s.platform,.workers=&s.pool,
        .hooks={refwin_done,refwin_complete,&s}};
    pthread_t thread;
    s.last_result = pthread_create(&thread,NULL,refwin_init_worker,&s);
    if (s.last_result) { s.error = "backend init pthread_create failed"; goto done; }
    s.last_result = pthread_join(thread,NULL);
    if (s.last_result) { s.error = "backend init pthread_join failed"; goto done; }
    if (s.init_result) { s.error = "render_backend_init worker failed"; goto done; }
    if (refwin_submit(&s,false)) goto done;
    uint64_t deadline = trace_now_ns()+timeout_ms*UINT64_C(1000000);
    while (s.backend.active) {
        if (refwin_pump(&s)) goto done;
        if (trace_now_ns()>deadline) { s.error = "startup frame timeout"; goto done; }
    }
    if (dry_run) { fprintf(stderr,"refwin: dry-run PASS (startup raster/Present complete; no injection, no CSV)\n"); rc = 0; goto done; }
    csv = fopen(csv_path,"w");
    if (!csv) { s.last_result = errno; s.error = "open reference CSV failed"; goto done; }
    if (fputs(REFPROTO_HEADER,csv) == EOF || fflush(csv)) { s.error = "CSV header write/flush failed"; s.last_result = errno; goto done; }
    xcb_atom_t ready = refproto_atom(s.platform.conn,"_EDIT_REF_READY");
    uint32_t ready_words[] = {1,s.keycode};
    if (!ready || !refproto_checked(s.platform.conn,xcb_change_property_checked(s.platform.conn,XCB_PROP_MODE_REPLACE,
        s.platform.win,ready,XCB_ATOM_CARDINAL,32,2,ready_words))) { s.error = "write reference readiness property failed"; goto done; }
    if (dprintf((int)ready_fd,"%" PRIu32 "\n",s.platform.win) < 0) { s.error = "READY fd write failed"; s.last_result = errno; goto done; }
    fprintf(stderr,"refwin: cpu-raster, one glyph; compositor bypass requested (not guaranteed)\n");
    deadline = trace_now_ns()+timeout_ms*UINT64_C(1000000);
    while (!pairs || s.completed < pairs) {
        uint32_t before = s.completed;
        if (refwin_pump(&s) || refwin_finish(&s,csv)) goto done;
        if (s.completed != before) deadline = trace_now_ns()+timeout_ms*UINT64_C(1000000);
        if (trace_now_ns()>deadline) {
            s.error = !s.key_pending ? "input timeout: no matching KeyPress received" :
                !s.notified_pair ? "metadata timeout: key received, no injection ClientMessage" :
                "frame timeout: key and metadata received, backend completion pending";
            goto done;
        }
    }
    /* Keep the acknowledgement available until the injector consumes it. */
    xcb_atom_t consumed = refproto_atom(s.platform.conn,"_EDIT_REF_CONSUMED");
    if (!consumed) { s.error = "intern consumed acknowledgement atom failed"; goto done; }
    for (;;) {
        refproto_row ack;
        int property = refproto_get(s.platform.conn,s.platform.win,consumed,&ack);
        if (property < 0) { s.error = "read consumed acknowledgement failed"; goto done; }
        if (!property && ack.pair == s.last_pair) break;
        if (refwin_pump(&s)) goto done;
        if (trace_now_ns()>deadline) { s.error = "consumed acknowledgement timeout"; goto done; }
    }
    rc = 0;
done:
    if (s.error) fprintf(stderr,"refwin: window=0x%08" PRIx32 " %s; result=%d init_result=%d frame=%" PRIu32
        " pair=%" PRIu32 " completed=%" PRIu32 " key_pending=%u active=%u presented=%u allocations=%zu"
        " T4=%" PRIu64 " T5=%" PRIu64 " T6=%" PRIu64 " inject=%" PRIu64 " MSC=%" PRIu64
        " phase=%" PRIu64 " period=%" PRIu64 " actual_phase=%" PRIu64 "\n",s.platform.win,s.error,s.last_result,s.init_result,
        s.grid.frame_id,s.notified_pair,s.completed,s.key_pending,s.backend.active,s.backend.presented,s.allocations,s.t4,s.t5,s.t6,s.metadata.inject,s.metadata.msc,s.metadata.phase,s.metadata.period,s.metadata.actual);
    render_backend_shutdown(&s.backend);
    if (pool_ready) work_pool_shutdown(&s.pool);
    edit_arena_free(&s.arena);
    if (platform_ready) plat_shutdown(&s.platform);
    if (csv && fclose(csv)) { fprintf(stderr,"refwin: close CSV %s: %s\n",csv_path,strerror(errno)); rc = 1; }
    refproto_display_close(&probe);
    return rc;
}
