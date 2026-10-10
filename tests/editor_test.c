#include "editor/editor.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "gl/gl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "journal/journal.h"
#include "x11/input.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct counted { bool active, suspended; size_t allocations, frames; } counted;
static void ingress(void *ctx, uint64_t seq, uint64_t ns)
{
    counted *c = ctx; (void)seq; (void)ns;
    if (!c->active) { edit_malloc_guard_begin(); c->active = true; }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    counted *c = ctx; (void)frame;
    if (c->active) { c->allocations += edit_malloc_guard_end(); c->active = false; }
    c->frames++;
}
static void io_boundary(void *ctx, bool entering)
{
    counted *c = ctx;
    if (entering && c->active) {
        c->allocations += edit_malloc_guard_end(); c->active = false; c->suspended = true;
    } else if (!entering && c->suspended) {
        edit_malloc_guard_begin(); c->active = true; c->suspended = false;
    }
}
static plat_event key(uint32_t sym, uint16_t mods, const char *text)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods};
    if (text) { size_t n = strlen(text); EDIT_ASSERT(n <= PLAT_UTF8_MAX); ev.utf8_len = (uint8_t)n; memcpy(ev.utf8, text, n); }
    return ev;
}
static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    do {
        int rc = editor_step(e, 20); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    } while (trace_now_ns() < deadline);
    T(false); return 1;
}
static int press(editor *e, plat_event ev)
{
    T(editor_inject(e, &ev) == EDITOR_OK); return settle(e);
}
static int expect(editor *e, const char *text, uint64_t cursor)
{
    uint8_t bytes[256]; size_t n = strlen(text);
    T(editor_length(e) == n); T(editor_read(e, 0, bytes, n) == 0);
    T(memcmp(bytes, text, n) == 0); T(editor_view(e).selection.cursor == cursor);
    return 0;
}
typedef struct replay_model { uint8_t bytes[256]; size_t len, records; } replay_model;
static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8u * i); return v;
}
static int replay(void *ctx, const journal_record *r)
{
    replay_model *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        if (r->size < 8) return 1;
        uint64_t off = le64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > sizeof m->bytes - m->len) return 1;
        size_t pos = (size_t)off; memmove(m->bytes + pos + n, m->bytes + pos, m->len - pos);
        memcpy(m->bytes + pos, r->data + 8, n); m->len += n; m->records++;
    } else if (r->type == JOURNAL_DELETE) {
        if (r->size != 16) return 1;
        uint64_t off = le64(r->data), n = le64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        size_t pos = (size_t)off, count = (size_t)n;
        memmove(m->bytes + pos, m->bytes + pos + count, m->len - pos - count);
        m->len -= count; m->records++;
    }
    return 0;
}
static int script(bool raster)
{
    char path[] = "/tmp/editor-test-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    editor_config cfg = {.cols = 30, .rows = 6, .journal_path = path};
    editor *e = NULL; int opened = editor_open(&e, &cfg, &b);
    if (opened) fprintf(stderr, "editor_test: open %s returned %d\n", raster ? "raster" : "null", opened);
    T(opened == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "ab")) == 0); T(expect(e, "ab", 2) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0); T(expect(e, "ab\n", 3) == 0);
    T(press(e, key('c', 0, "cd")) == 0); T(expect(e, "ab\ncd", 5) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(expect(e, "ab\ncd", 3) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "ab\ncd", 3) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    T(press(e, key(XKB_KEY_Delete, 0, NULL)) == 0); T(expect(e, "abd", 2) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    editor_stats s = editor_get_stats(e); T(s.mutations == 8); T(s.journal_records == 8); T(s.journal_error == 0);
    T(editor_flush(e) == 0); editor_close(e);
    replay_model m = {0}; journal_replay_result result;
    T(journal_replay_file(path, replay, &m, &result) == JOURNAL_OK);
    T(m.len == 4 && memcmp(m.bytes, "abcd", 4) == 0 && m.records == 8); unlink(path);
    printf("editor_test: %s script and journal byte model passed\n", raster ? "raster" : "null");
    return 0;
}
static int allocation_test(bool raster)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    counted c = {0}; char path[] = "/tmp/editor-alloc-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    editor_config cfg = {.cols = 40, .rows = 8, .journal_path = path,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    for (unsigned i = 0; i < 10000; i += 100) {
        for (unsigned j = 0; j < 100; j++) {
            plat_event ev = j % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
            T(editor_inject(e, &ev) == 0);
        }
        T(settle(e) == 0);
    }
    T(!c.active && !c.suspended);
    if (c.allocations) fprintf(stderr, "editor allocation diagnostic: backend=%s allocations=%zu\n", raster ? "raster" : "null", c.allocations);
    T(c.allocations == 0); T(editor_length(e) == 0);
    editor_stats s = editor_get_stats(e); T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    T(editor_flush(e) == 0); editor_close(e); unlink(path);
    printf("editor_test: %s 10000 keys mallocs=%zu guard=%s\n", raster ? "raster" : "null", c.allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert");
    return 0;
}
/* G11: one blink must leave the strip/fence pool asleep. Force its deadline
 * rather than wait for a noisy wall-clock interval. */
static int raster_blink_damage(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        render_backend b = {0}; T(render_cpu_backend(&b) == 0);
        uint8_t wrapped[320]; memset(wrapped, 'a', sizeof wrapped);
        const uint8_t *text = mode == 1 ? (const uint8_t *)"abc \nxyz" :
            mode == 2 ? (const uint8_t *)"a\xe4\xb8\xad" : mode == 3 ? wrapped : NULL;
        size_t len = mode == 1 ? 8u : mode == 2 ? 4u : mode == 3 ? sizeof wrapped : 0u;
        editor_config cfg = {.cols = 160, .rows = 60, .initial = text, .initial_len = len,
            .wrap_mode = mode == 3 ? 1 : -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        if (mode) { T(editor_set_cursor(e, mode == 3 ? 200u : 1u) == 0); T(settle(e) == 0); }
        if (mode == 1) T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
        uint64_t fills = e->stats.minimap_fills;
        for (unsigned toggle = 0; toggle < 2; toggle++) {
            uint32_t epochs[WORK_MAX_JOBS];
            for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++)
                epochs[slot] = atomic_load_explicit(&e->pool.slots[slot].epoch, memory_order_acquire);
            render_cell before[160 * 60]; memcpy(before, e->grid.cells, sizeof before);
            e->next_blink = trace_now_ns();
            T(editor_step(e, 0) >= 0);
            raster_metrics m; T(raster_frame_metrics(&b, &m));
            size_t changed = 0;
            for (size_t i = 0; i < 160u * 60u; i++)
                if (memcmp(&before[i], &e->grid.cells[i], sizeof before[i])) changed++;
            printf("G11 blink: mode=%u jobs=%u cells=%zu minimap_fills=%llu\n", mode, m.jobs, changed,
                (unsigned long long)(e->stats.minimap_fills - fills));
            T(m.jobs == 0);
            T(changed > 0 && changed <= 2 && m.inline_cells == changed && m.completion_jobs == 0);
            for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++) {
                const work_slot *job = &e->pool.slots[slot];
                uint32_t epoch = atomic_load_explicit(&job->epoch, memory_order_acquire);
                /* The first caret submit retires the preceding full-frame
                 * leases. Cancellation bumps an epoch and leaves cancel_ns;
                 * a new submission clears cancel_ns and must still fail. */
                T(epoch == epochs[slot] || (toggle == 0 && epoch == epochs[slot] + 1u &&
                    atomic_load_explicit(&job->cancel_ns, memory_order_acquire) != 0 &&
                    work_handle_finished(&e->pool, (work_handle){slot, epochs[slot]}) &&
                    !atomic_load_explicit(&job->busy, memory_order_acquire)));
            }
            T(e->stats.minimap_fills == fills);
            e->caret_only = false; /* Explicit test drain, rather than deferred idle observation. */
            T(settle(e) == 0);
        }
        plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
        T(editor_inject(e, &focus) == 0); T(editor_step(e, 0) >= 0);
        raster_metrics m; T(raster_frame_metrics(&b, &m));
        T(m.jobs == 0 && m.completion_jobs == 0);
        T(!editor_get_stats(e).blinking && !editor_get_stats(e).pending);
        e->caret_only = false; T(settle(e) == 0);
        editor_close(e);
    }
    puts("editor_test: G11 inline caret damage and sleeping worker pool passed");
    return 0;
}
static int idle_policy(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(12000000000);
    while (editor_get_stats(e).blinking && trace_now_ns() < deadline) {
        int rc = editor_step(e, -1); T(rc == EDITOR_OK || rc == EDITOR_MORE);
    }
    editor_stats s = editor_get_stats(e);
    T(!s.blinking && s.cursor_visible); T(s.blinks <= 19 && s.poll_returns <= 20);
    uint64_t returns = s.poll_returns;
    T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == returns + 1); /* test deadline only */
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    s = editor_get_stats(e); T(!s.focused && !s.blinking);
    returns = s.poll_returns; T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == returns + 1);
    focus.focused = true; T(press(e, focus) == 0); T(editor_get_stats(e).blinking);
    editor_close(e); puts("editor_test: idle blink timeout and unfocused timer disarm passed"); return 0;
}
typedef struct delayed { bool ready; } delayed;
static int delayed_init(render_backend *b, const render_config *cfg) { (void)cfg; ((delayed *)b->state)->ready = false; return 0; }
static int delayed_resize(render_backend *b, render_dims d) { (void)b; (void)d; return 0; }
static int delayed_submit(render_backend *b, const render_grid *g, const render_strip *s, size_t n)
{ (void)b; (void)g; (void)s; (void)n; return 0; }
static int delayed_present(render_backend *b, uint32_t id)
{
    if (!((delayed *)b->state)->ready) return RENDER_ERR_BUSY;
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, id, 0);
    return rc ? rc : render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, id, 0);
}
static int delayed_event(render_backend *b, const render_event *ev) { (void)b; (void)ev; return RENDER_ERR_UNSUPPORTED; }
static void delayed_close(render_backend *b) { (void)b; }
static int queue_depth(void)
{
    render_backend b = {.info = {"editor delayed test", sizeof(delayed), 16, RENDER_CAP_HEADLESS},
        .ops = {delayed_init, delayed_resize, delayed_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    for (unsigned i = 0; i < 100 && !b.active; i++) T(editor_step(e, 0) >= 0);
    T(b.active && b.stats.submitted_frames == 1);
    for (unsigned i = 0; i < 5; i++) { plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0); }
    for (unsigned i = 0; i < 100; i++) T(editor_step(e, 0) >= 0);
    T(editor_length(e) == 5 && editor_get_stats(e).mutations == 5);
    T(b.stats.submitted_frames == 1 && b.active && editor_get_stats(e).pending);
    ((delayed *)b.state)->ready = true; T(settle(e) == 0);
    T(b.stats.submitted_frames == 2 && editor_get_stats(e).submitted_sequence == 5);
    editor_close(e); puts("editor_test: queue depth one and coalesced edits passed"); return 0;
}
static int native_key(editor *e, plat *p, const char *name, uint16_t state)
{
    x11_input *in = p->in;
    xkb_keycode_t code = xkb_keymap_key_by_name(in->keymap, name); T(code <= UINT8_MAX);
    xcb_key_press_event_t ev = {.response_type = XCB_KEY_PRESS, .detail = (uint8_t)code,
        .event = p->win, .state = state, .same_screen = 1};
    xcb_connection_t *conn = p->conn;
    (void)xcb_send_event(conn, 0, p->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&ev);
    ev.response_type = XCB_KEY_RELEASE;
    (void)xcb_send_event(conn, 0, p->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&ev);
    (void)xcb_flush(conn);
    uint64_t sequence = editor_get_stats(e).input_sequence;
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (editor_get_stats(e).input_sequence == sequence) { T(editor_step(e, 20) >= 0); T(trace_now_ns() < deadline); }
    return settle(e);
}
static int native_input(void)
{
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
    xcb_get_property_reply_t *title = xcb_get_property_reply(p->conn,
        xcb_get_property(p->conn, 0, p->win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 64), NULL);
    T(title != NULL && xcb_get_property_value_length(title) == 9);
    T(memcmp(xcb_get_property_value(title), "sublimite", 9) == 0); free(title);
    T(native_key(e, p, "AC01", 0) == 0); T(expect(e, "a", 1) == 0);
    T(native_key(e, p, "RTRN", 0) == 0); T(expect(e, "a\n", 2) == 0);
    T(native_key(e, p, "BKSP", 0) == 0); T(expect(e, "a", 1) == 0);
    T(native_key(e, p, "AB01", XCB_MOD_MASK_CONTROL) == 0); T(expect(e, "a\n", 2) == 0);
    T(native_key(e, p, "AB01", XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT) == 0); T(expect(e, "a", 1) == 0);
    xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = p->win,
        .type = p->wm_protocols, .data.data32 = {p->wm_delete, 0, 0, 0, 0}};
    (void)xcb_send_event(p->conn, 0, p->win, 0, (const char *)&close_event); (void)xcb_flush(p->conn);
    int rc = EDITOR_OK; uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (rc != EDITOR_CLOSED && trace_now_ns() < deadline) rc = editor_step(e, 20);
    T(rc == EDITOR_CLOSED); editor_close(e);
    puts("editor_test: native X11 translation, editor loop and WM close passed"); return 0;
}
static int scrolled_undo(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "a\n")) == 0); T(expect(e, "a\n", 2) == 0);
    T(editor_view(e).first_byte == 2);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "", 0) == 0);
    T(editor_view(e).first_byte == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "a\n", 2) == 0);
    editor_close(e); puts("editor_test: scrolled undo/redo viewport repair passed"); return 0;
}
static int stopped_error(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 1, .max_cols = 16, .max_rows = 1, .history_keys = 1};
    editor *e = NULL;
    /* Find the first open-time reserve that fits metadata. Its slack is less
     * than an add chunk; the first key then fails the piece allocator. */
    for (size_t reserve = 65536; reserve <= 2u * 1024u * 1024u; reserve += 65536) {
        cfg.arena_bytes = reserve;
        int rc = editor_open(&e, &cfg, &b);
        if (!rc) break;
        T(rc == EDITOR_ERR_MEMORY || rc == PIECE_ERR_NOMEM);
    }
    T(e != NULL); T(settle(e) == 0);
    plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
    int rc = editor_step(e, 0);
    T(rc < 0 && editor_get_stats(e).error_cause == PIECE_ERR_NOMEM);
    T(editor_length(e) == 0 && editor_step(e, 0) == rc);
    editor_close(e); puts("editor_test: mutation error is negative and stops the loop passed"); return 0;
}

/* Review regressions run separately so every finding keeps its own red line. */
static void raw_key(plat *p, const char *name)
{
    x11_input *in = p->in;
    xkb_keycode_t code = xkb_keymap_key_by_name(in->keymap, name);
    EDIT_ASSERT(code <= UINT8_MAX);
    xcb_key_press_event_t ev = {.response_type = XCB_KEY_PRESS, .detail = (uint8_t)code,
        .event = p->win, .same_screen = 1};
    (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&ev);
    ev.response_type = XCB_KEY_RELEASE;
    (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&ev);
}
static void raw_close(plat *p)
{
    xcb_client_message_event_t ev = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = p->win, .type = p->wm_protocols, .data.data32 = {p->wm_delete, 0, 0, 0, 0}};
    (void)xcb_send_event(p->conn, 0, p->win, 0, (const char *)&ev); (void)xcb_flush(p->conn);
}
static void raw_barrier(plat *p)
{
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(p->conn, xcb_get_input_focus(p->conn), NULL);
    free(r);
}
static int review_native_order(void)
{
    for (unsigned kind = 0; kind < 3; kind++) {
        render_backend b = {0}; T(render_cpu_backend(&b) == 0);
        editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
        raw_key(p, "AC01");
        if (kind == 0) raw_close(p);
        else if (kind == 1) {
            xcb_focus_out_event_t ev = {.response_type = XCB_FOCUS_OUT, .event = p->win,
                .mode = XCB_NOTIFY_MODE_NORMAL, .detail = XCB_NOTIFY_DETAIL_NONLINEAR};
            (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_FOCUS_CHANGE, (const char *)&ev);
        } else {
            xcb_configure_notify_event_t ev = {.response_type = XCB_CONFIGURE_NOTIFY, .event = p->win,
                .window = p->win, .width = 264, .height = 120};
            (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&ev);
        }
        raw_barrier(p);
        if (kind == 0) {
            int rc = 0;
            for (unsigned i = 0; i < 200 && rc != EDITOR_CLOSED; i++) rc = editor_step(e, 0);
            T(rc == EDITOR_CLOSED);
        } else T(settle(e) == 0);
        T(expect(e, "a", 1) == 0); editor_close(e);
    }
    puts("review 1: native batched key/close, key/focus, key/resize ordered passed"); return 0;
}
static int review_native_burst(void)
{
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
    for (unsigned i = 0; i < 1300; i++) {
        raw_key(p, "AC01");
        xcb_expose_event_t ev = {.response_type = XCB_EXPOSE, .window = p->win, .width = 256, .height = 120};
        (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_EXPOSURE, (const char *)&ev);
    }
    raw_barrier(p);
    for (unsigned i = 0; i < 10000 && editor_length(e) < 1300; i++) T(editor_step(e, 1) >= 0);
    T(editor_length(e) == 1300); T(settle(e) == 0); editor_close(e);
    puts("review 2: native burst beyond both queue capacities preserves all keys passed"); return 0;
}
static void kill_submit(void *ctx, const editor_frame *f)
{ (void)ctx; if (f->last_sequence) (void)kill(getpid(), SIGKILL); }
static int review_crash_at(unsigned point)
{
    char path[] = "/tmp/editor-crash-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend b = {0}; if (render_null_backend(&b)) _exit(10);
        editor_config cfg = {.cols = 32, .rows = 8, .journal_path = path,
            .on_submit = point == 0 ? kill_submit : NULL, .on_present = point == 1 ? kill_submit : NULL};
        editor *e = NULL; if (editor_open(&e, &cfg, &b) || settle(e) || editor_flush(e)) _exit(11);
        plat_event ev = key('x', 0, "x"); if (editor_inject(e, &ev)) _exit(12);
        if (point == 2) {
            /* Force a continuation after mutation, before layout/submission. */
            e->lay.cfg.slice_clusters = 1;
            if (editor_step(e, 0) < 0 || editor_length(e) != 1) _exit(13);
            (void)kill(getpid(), SIGKILL);
        } else if (point == 3) {
            if (settle(e) || editor_flush(e)) _exit(14);
            (void)kill(getpid(), SIGKILL);
        } else if (settle(e)) _exit(15);
        _exit(16);
    }
    int status = 0; T(waitpid(child, &status, 0) == child);
    T(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0); unlink(path);
    T(m.len == 1 && m.bytes[0] == 'x');
    printf("review 3/P1.9-2.2: SIGKILL point=%u recovery=x passed\n", point); return 0;
}
static int review_crash(void)
{ int rc = 0; for (unsigned p = 0; p < 4; p++) rc |= review_crash_at(p); return rc; }
static ssize_t reject_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{ (void)ctx; (void)fd; (void)p; (void)n; (void)off; errno = EIO; return -1; }
static int review_suffix_at(unsigned mode)
{
    {
        char path[] = "/tmp/editor-suffix-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
        if (mode) { journal_io io = {.append_write = reject_append}; T(journal_set_io(e->journal, &io) == 0); }
        else {
            /* Exhaust admission before staging the suffix. */
            uint8_t *large = malloc(JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
            memset(large, 'q', JOURNAL_DEFAULT_BATCH_BYTES);
            T(journal_insert(e->journal, e->buffer->id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
        }
        e->stage[0] = 'a'; e->stage[1] = 'b'; e->stage_used = 2; e->op_count = 2;
        e->ops[0] = (editor_jop){0, 1, 0, true, e->buffer->id};
        e->ops[1] = (editor_jop){1, 1, 1, true, e->buffer->id};
        uint64_t accepted = journal_get_stats(e->journal).accepted_sequence;
        editor_journal_staged(e); T(e->stats.journal_error == (mode ? JOURNAL_IO : JOURNAL_FULL));
        /* IO accepted the first record into the journal's retained batch;
         * FULL accepted nothing. Preserve only the unaccepted editor suffix. */
        T(e->op_count == (mode ? 1u : 2u)); T(e->stage_used == 2);
        if (mode) {
            T(e->ops[0].off == 1 && journal_get_stats(e->journal).accepted_sequence == accepted + 1);
            T(journal_set_io(e->journal, NULL) == 0);
        }
        editor_close(e); unlink(path);
    }
    puts("review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed"); return 0;
}
static int review_suffix(void)
{ int a = review_suffix_at(0), b = review_suffix_at(1); return a | b; }
static int reject_sync(void *ctx, int fd, bool directory)
{ (void)ctx; (void)fd; (void)directory; errno = EIO; return -1; }
static int review_journal_exit_at(unsigned mode)
{
    char path[] = "/tmp/editor-journal-exit-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    if (!mode) {
        uint8_t *large = calloc(1, JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
        T(journal_insert(e->journal, e->buffer->id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
    } else if (mode == 1) {
        journal_io io = {.append_write = reject_append}; T(journal_set_io(e->journal, &io) == 0);
    }
    plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
    if (mode < 2) T(editor_step(e, 0) == EDITOR_ERR_IO);
    else {
        T(settle(e) == 0);
        journal_io io = {.sync = reject_sync}; T(journal_set_io(e->journal, &io) == 0);
        T(journal_flush(e->journal) == JOURNAL_IO);
        T(journal_get_stats(e->journal).durable_sequence < journal_get_stats(e->journal).accepted_sequence);
    }
    T(editor_length(e) == 1); T(journal_set_io(e->journal, NULL) == 0);
    T(editor_flush(e) == 0); T(e->op_count == 0 && e->stage_used == 0);
    editor_close(e); replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0); T(m.len == 1 && m.bytes[0] == 'x'); unlink(path);
    printf("review 4: exit checkpoint after FULL/append IO/sync IO mode=%u restores current tree passed\n", mode); return 0;
}
static int review_journal_exit(void)
{ int a = review_journal_exit_at(0), b = review_journal_exit_at(1), c = review_journal_exit_at(2); return a | b | c; }
typedef struct checkpoint_model { replay_model buffers[2]; size_t views; bool tabs, closed; } checkpoint_model;
static int checkpoint_replay(void *ctx, const journal_record *r)
{
    checkpoint_model *m = ctx;
    if (r->type == JOURNAL_TABS) {
        if (r->size != (m->closed ? 24u : 32u) || le64(r->data) != (m->closed ? 1u : 2u) ||
            le64(r->data + 8) != (m->closed ? 0u : 1u) || le64(r->data + 16) != (m->closed ? 2u : 1u) ||
            (!m->closed && le64(r->data + 24) != 2)) return 1;
        m->tabs = true; return 0;
    }
    if (r->type == JOURNAL_WINDOW) return 0;
    if (!r->buffer_id || r->buffer_id > 2) return 1;
    replay_model *b = &m->buffers[r->buffer_id - 1];
    if (r->type == JOURNAL_BASE) {
        journal_base base; char name[4097];
        if (journal_decode_base(r, &base, name, sizeof name)) return 1;
        if (*name) {
            int fd = open(name, O_RDONLY); if (fd < 0) return 1;
            ssize_t n = read(fd, b->bytes, sizeof b->bytes); close(fd); if (n < 0) return 1;
            b->len = (size_t)n;
        }
        return 0;
    }
    if (r->type == JOURNAL_VIEW) {
        if (r->size != 32 || le64(r->data) != 1 || le64(r->data + 8) != 1 ||
            le64(r->data + 16) != 0 || le64(r->data + 24) != 0) return 1;
        m->views++; return 0;
    }
    return replay(b, r);
}
static int review_checkpoint_session_at(bool closed)
{
    char path[] = "/tmp/editor-checkpoint-XXXXXX", source[] = "/tmp/editor-named-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0); close(fd);
    fd = mkstemp(source); T(fd >= 0); T(write(fd, "aaaa", 4) == 4); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.path = source, .journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(press(e, key('x', 0, "x")) == 0);
    uint64_t id = 0; T(editor_add_buffer(e, NULL, (const uint8_t *)"two", 3, &id) == 0 && id == 2);
    T(settle(e) == 0); T(press(e, key('y', 0, "y")) == 0); T(editor_flush(e) == 0);
    if (closed) { T(editor_close_tab(e, 0) == 0); T(settle(e) == 0); }
    uint8_t *large = calloc(1, JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
    T(journal_insert(e->journal, id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
    T(editor_flush(e) == 0); editor_close(e);
    checkpoint_model model = {.closed = closed}; journal_replay_result result;
    T(journal_replay_file(path, checkpoint_replay, &model, &result) == 0);
    T(model.buffers[0].len == 5 && !memcmp(model.buffers[0].bytes, "xaaaa", 5));
    T(model.buffers[1].len == 4 && !memcmp(model.buffers[1].bytes, "ytwo", 4));
    T(model.views == 2 && model.tabs); unlink(path); unlink(source);
    puts("review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed"); return 0;
}
static int review_checkpoint_session(void)
{ int a = review_checkpoint_session_at(false), b = review_checkpoint_session_at(true); return a | b; }
static int review_base_race(void)
{
    char source[] = "/tmp/editor-base-race-XXXXXX", path[] = "/tmp/editor-base-log-XXXXXX";
    int fd = mkstemp(source); T(fd >= 0); T(write(fd, "aaaa", 4) == 4); close(fd);
    fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.journal_path = path, .start_empty = true, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0);
    editor_buffer *b = NULL; T(editor_buffer_prepare(e, source, NULL, 0, &b) == 0);
    uint8_t bytes[4]; T(piece_read(b->tree, 0, bytes, 4) == 0 && !memcmp(bytes, "aaaa", 4));
    fd = open(source, O_WRONLY | O_TRUNC); T(fd >= 0); T(write(fd, "bbbb", 4) == 4); close(fd);
    T(editor_register_journal(e, b, 1) == EDITOR_ERR_IO);
    T(journal_get_stats(e->journal).accepted_sequence == 0);
    editor_buffer_destroy(b); editor_close(e); unlink(source); unlink(path);
    puts("review 5/P1.9-2.3: source changed between attachment and BASE registration rejected passed"); return 0;
}
static int review_style(void)
{
    const char *initial = "abcdefghijklmnopqrstuv\nnext";
    for (unsigned mode = 0; mode < 4; mode++) {
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.cols = 32, .rows = 4, .initial = (const uint8_t *)initial,
            .initial_len = strlen(initial), .wrap_mode = -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(editor_full_layout(e) == 0); e->lay.cfg.slice_clusters = 3;
        T(layout_run(&e->lay) == LAYOUT_MORE); T(e->lay.row == 0 && e->lay.phase == 1);
        /* Style changes can also arrive during the final decoration pass. */
        e->paint_ready = true;
        if (mode < 2) {
            e->old_selection = e->v.state.selection; e->v.state.selection.cursor = 1;
            if (!mode) e->v.state.selection.anchor = 1;
        } else if (mode == 2) e->visible = false;
        else e->focused = false;
        T(editor_refresh_cursor(e, 0) == 0); T(settle(e) == 0);
        const render_grid *g = editor_grid(e); uint32_t col = layout_gutter_width(&e->lay);
        const render_cell *a = &g->cells[g->dims.cols + col], *z = a + 1;
        T(!(a->attrs & RENDER_ATTR_CURSOR));
        if (mode < 2) T(z->attrs & RENDER_ATTR_CURSOR);
        if (mode == 1) T(a->bg == e->layout_cfg.sel_bg);
        editor_close(e);
    }
    puts("review 6: submitted mid-row caret/selection/blink/focus cells passed"); return 0;
}
static int review_long_line(void)
{
    size_t n = 70000, len = 2 * n + 16; uint8_t *bytes = malloc(len); T(bytes != NULL);
    memset(bytes, 'a', n); memcpy(bytes + n, "\nvisible\n", 9);
    memset(bytes + n + 9, 'b', n); memcpy(bytes + 2 * n + 9, "\nagain\n", 7);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 7, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    const render_grid *g = editor_grid(e); uint32_t col = layout_gutter_width(&e->lay);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[2u * g->dims.cols + col + 6].glyph_index == 'e');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    T(b.stats.submitted_frames == 1);
    T(press(e, key('x', 0, "x")) == 0);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    editor_close(e); puts("review 7: multiple long clipped lines preserve subsequent rows in first frame and edit/undo passed"); return 0;
}
static int review_large_undo(void)
{
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes != NULL); memset(bytes, 'a', n);
    char path[] = "/tmp/editor-large-undo-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .journal_path = path,
        .cols = 32, .rows = 4, .wrap_mode = -1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(editor_length(e) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == n);
    T(editor_flush(e) == 0);
    uint8_t tail[8]; T(editor_read(e, n - 8, tail, 8) == 0 && !memcmp(tail, "aaaaaaaa", 8));
    editor_close(e); unlink(path); puts("review 12: journaled large deletion and undo passed"); return 0;
}
static int review_bursts(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "a")) == 0); T(press(e, key('b', 0, "b")) == 0);
    T(press(e, key('c', 0, "c")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "abc", 3) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    plat_event repeat = key(XKB_KEY_BackSpace, 0, NULL); repeat.repeat = true;
    T(press(e, repeat) == 0); T(press(e, repeat) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abc", 3) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(press(e, key('x', 0, "x")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abc", 2) == 0);
    editor_close(e); puts("review 13: typing and repeat deletion bursts, movement boundary passed"); return 0;
}
typedef struct review_hold { _Atomic bool release, expired, entered; } review_hold;
static void review_hold_job(work_ctx *job)
{
    review_hold *h = job->arg; atomic_store(&h->entered, true);
    while (!atomic_load(&h->release) && !work_should_stop(job)) {
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
}
static void *review_release(void *ctx)
{
    review_hold *h = ctx;
    struct timespec pause = {0, 250000000}; (void)nanosleep(&pause, NULL);
    atomic_store(&h->expired, true); atomic_store(&h->release, true); return NULL;
}
static int review_prefix_open(void)
{
    char path[] = "build/s9-prefix-fixture-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "prefix\n", 7) == 7); T(ftruncate(fd, 2 * FILE_PREFIX_MAX) == 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    b.info.capabilities |= RENDER_CAP_RASTER_POOL; /* independent prefix lane */
    editor_config cfg = {.cols = 32, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    review_hold hold = {0}; work_handle handle = work_submit(&e->pool,
        (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(handle.epoch);
    while (!atomic_load(&hold.entered)) { struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL); }
    pthread_t timer; T(pthread_create(&timer, NULL, review_release, &hold) == 0);
    uint64_t id; int rc = editor_add_buffer(e, path, NULL, 0, &id);
    if (!rc) rc = settle(e);
    bool first_before_remainder = !atomic_load(&hold.expired);
    T(pthread_join(timer, NULL) == 0); editor_close(e); unlink(path);
    T(rc == 0); T(first_before_remainder);
    puts("review 8: first viewport precedes stalled remainder work passed"); return 0;
}
static int review_index_backlog(void)
{
    size_t n = 16u * 1024u * 1024u; uint8_t *bytes = malloc(n); T(bytes);
    memset(bytes, 0xff, n);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(n); T(e->buffer->index);
    lineidx_src src = editor_source(e->buffer);
    T(lineidx_build_start(e->buffer->index, &e->pool, &src) == 0);
    /* Let a whole completed backlog arrive without an editor maintenance turn. */
    for (;;) {
        bool busy = false;
        for (size_t i = 0; i < WORK_MAX_JOBS; i++) busy |= atomic_load(&e->pool.slots[i].busy) != 0;
        if (!busy) break;
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
    T(!lineidx_any_nonascii(e->buffer->index));
    plat_event ev = key('x', 0, "x"); T(editor_handle_key(e, &ev) == 0);
    T(!lineidx_any_nonascii(e->buffer->index));
    T(settle(e) == 0); editor_close(e);
    /* Exercise the editor's own metadata update at the maximum gated size.
     * No content reads: an unbuilt table can model the sparse suffix without
     * mapping/touching it. Each action changes only byte zero of the tree. */
    b = (render_backend){0}; T(render_null_backend(&b) == 0);
    cfg = (editor_config){.cols = 16, .rows = 2, .wrap_mode = -1}; e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(UINT64_C(10) * 1024 * 1024 * 1024); T(e->buffer->index);
    uint64_t worst = 0;
    for (unsigned i = 0; i < 128; i++) {
        uint64_t before = lineidx_foreground_work(e->buffer->index);
        ev = i % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
        T(editor_handle_key(e, &ev) == 0);
        uint64_t visits = lineidx_foreground_work(e->buffer->index) - before;
        if (visits > worst) worst = visits;
    }
    editor_close(e);
    printf("review 10: 10 GiB beginning edits metadata visits max=%llu (M)[AC], bound=4096 (G)\n",
        (unsigned long long)worst); T(worst < 4096);
    puts("review 10: mutation cancels completed backlog without adopting it passed"); return 0;
}
static int review_piece_recycling(void)
{
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    for (unsigned i = 0; i < 4; i++) {
        T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    }
    size_t warm = e->buffer->arena.used;
    for (unsigned i = 0; i < 256; i++) {
        T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == n);
        T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_length(e) == 1);
    }
    size_t growth = e->buffer->arena.used - warm; editor_close(e);
    printf("review 15: replay arena growth=%zu bytes (M)[AC], bound=65536 bytes (G)\n", growth);
    T(growth <= 65536); return 0;
}
typedef struct review_submit_state { delayed device; uint64_t elapsed; } review_submit_state;
static int review_slow_submit(render_backend *b, const render_grid *g, const render_strip *s, size_t n)
{
    uint64_t start = trace_now_ns(); struct timespec pause = {0, 2000000};
    (void)nanosleep(&pause, NULL);
    ((review_submit_state *)b->state)->elapsed = trace_now_ns() - start;
    return delayed_submit(b, g, s, n);
}
static int review_slice_accounting(void)
{
    render_backend b = {.info = {"editor submit accounting", sizeof(review_submit_state), 16, RENDER_CAP_HEADLESS},
        .ops = {delayed_init, delayed_resize, review_slow_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); ((review_submit_state *)b.state)->device.ready = true;
    T(settle(e) == 0);
    uint64_t observed = ((review_submit_state *)b.state)->elapsed;
    uint64_t reported = editor_get_stats(e).longest_slice_ns; editor_close(e);
    printf("review 11: indivisible submit observed=%llu reported=%llu ns (M)[AC]\n",
        (unsigned long long)observed, (unsigned long long)reported);
    T(reported >= observed); return 0;
}
static int review_held_sync(void *ctx, int fd, bool directory)
{
    review_hold *h = ctx;
    if (!directory) {
        atomic_store(&h->entered, true);
        while (!atomic_load(&h->release)) {
            struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
        }
    }
    return directory ? fsync(fd) : fdatasync(fd);
}
static int review_journal_idle_at(bool queued)
{
    char path[] = "build/s9-idle-journal-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 2, .journal_path = path}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    review_hold hold = {0}; journal_io io = {.ctx = &hold, .sync = review_held_sync};
    if (!queued) T(journal_set_io(e->journal, &io) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    if (queued) {
        work_handle handle = work_submit(&e->pool, (work_job){review_hold_job, &hold, 0, WORK_BULK});
        T(handle.epoch);
        while (!atomic_load(&hold.entered)) {
            struct timespec ready = {0, 1000000}; (void)nanosleep(&ready, NULL);
        }
    }
    struct timespec pause = {1, 10000000}; (void)nanosleep(&pause, NULL);
    T(editor_step(e, 0) >= 0);
    while (!atomic_load(&hold.entered)) { pause = (struct timespec){0, 1000000}; (void)nanosleep(&pause, NULL); }
    pthread_t timer; T(pthread_create(&timer, NULL, review_release, &hold) == 0);
    uint64_t before = editor_get_stats(e).poll_returns, deadline = trace_now_ns() + UINT64_C(150000000);
    while (trace_now_ns() < deadline) T(editor_step(e, 80) >= 0);
    uint64_t polls = editor_get_stats(e).poll_returns - before;
    T(pthread_join(timer, NULL) == 0); T(editor_flush(e) == 0); editor_close(e); unlink(path);
    printf("review 17: %s job idle polls=%llu (M)[AC], requested-timeout bound=4 (G)\n",
        queued ? "queued" : "active", (unsigned long long)polls);
    T(polls <= 4); return 0;
}
static int review_journal_idle(void)
{ int active = review_journal_idle_at(false), queued = review_journal_idle_at(true); return active | queued; }
typedef struct review_bytes { uint8_t *bytes; size_t len, cap; } review_bytes;
static int review_replay_bytes(void *ctx, const journal_record *r)
{
    review_bytes *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        if (r->size < 8) return 1;
        uint64_t off = le64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > m->cap - m->len) return 1;
        size_t at = (size_t)off; memmove(m->bytes + at + n, m->bytes + at, m->len - at);
        memcpy(m->bytes + at, r->data + 8, n); m->len += n;
    } else if (r->type == JOURNAL_DELETE) {
        if (r->size != 16) return 1;
        uint64_t off = le64(r->data), n = le64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        size_t at = (size_t)off, count = (size_t)n;
        memmove(m->bytes + at, m->bytes + at + count, m->len - at - count); m->len -= count;
    }
    return 0;
}
static int review_replay_atomic(void)
{
    char path[] = "build/s9-atomic-journal-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    size_t requests = 0, failures = 0;
    for (size_t position = 0; position <= requests; position++) {
        unlink(path); render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2,
            .journal_path = path, .wrap_mode = -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key('x', 0, "x")) == 0);
        T(editor_flush(e) == 0);
        editor_piece_storage *s = &e->buffer->piece_storage;
        size_t before = s->requests, cursor = e->buffer->history_cursor;
        uint64_t records = editor_get_stats(e).journal_records;
        view_state view_before = editor_view(e);
        render_cell visible[32]; memcpy(visible, editor_grid(e)->cells, sizeof visible);
        if (position) s->fail_request = before + position;
        plat_event ev = key('z', PLAT_MOD_CTRL, NULL); T(editor_inject(e, &ev) == 0);
        int rc = editor_step(e, 0);
        if (!position) { requests = s->requests - before; T(requests > 0); T(rc >= 0); }
        else if (rc < 0) {
            failures++; uint8_t current = 0;
            T(editor_length(e) == 1 && editor_read(e, 0, &current, 1) == 0 && current == 'x');
            T(e->buffer->history_cursor == cursor && e->buffer->lines == 1);
            T(editor_view(e).selection.cursor == view_before.selection.cursor &&
                editor_view(e).selection.anchor == view_before.selection.anchor);
            T(editor_view(e).first_byte == view_before.first_byte && editor_view(e).first_line == view_before.first_line &&
                editor_view(e).hscroll == view_before.hscroll && editor_view(e).visual_byte == view_before.visual_byte);
            T(!memcmp(visible, editor_grid(e)->cells, sizeof visible));
            T(editor_get_stats(e).journal_records == records && e->op_count == 0);
        }
        s->fail_request = 0;
        if (rc >= 0) T(settle(e) == 0);
        T(editor_flush(e) == 0); editor_close(e);
        review_bytes model = {malloc(n + 8), 0, n + 8}; T(model.bytes);
        journal_replay_result result; T(journal_replay_file(path, review_replay_bytes, &model, &result) == 0);
        if (rc < 0) T(model.len == 1 && model.bytes[0] == 'x');
        else T(model.len == n && !memcmp(model.bytes, bytes, n));
        free(model.bytes); unlink(path);
    }
    free(bytes); T(failures > 0);
    printf("review 22: allocator positions=%zu failures=%zu; tree/view/history/journal rollback passed (M)[AC]\n", requests, failures);
    return 0;
}
static int review_fairness(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    uint64_t before = b.stats.submitted_frames;
    for (unsigned turn = 0; turn < 64; turn++) {
        while (e->queue_count < EDITOR_INPUT_CAP) {
            plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
        }
        T(editor_step(e, 0) >= 0);
    }
    T(b.stats.submitted_frames > before && editor_get_stats(e).submitted_sequence > 0);
    editor_close(e); puts("review 14: sustained arrivals make containing-frame progress passed"); return 0;
}
static int worker_owned_init(render_backend *b, const render_config *cfg)
{
    (void)b;
    return pthread_equal(pthread_self(), cfg->workers->threads[0]) ? 0 : RENDER_ERR_INIT;
}
static int worker_failed_init(render_backend *b, const render_config *cfg)
{ (void)b; return pthread_equal(pthread_self(), cfg->workers->threads[0]) ? RENDER_ERR_INIT : RENDER_ERR_ARG; }
static int review_init(void)
{
    render_backend b = {.info = {"mailbox init test", sizeof(delayed), 16, RENDER_CAP_HEADLESS},
        .ops = {worker_owned_init, delayed_resize, delayed_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(b.initialized); editor_close(e);
    b.ops.init = worker_failed_init; e = NULL;
    T(editor_open(&e, &cfg, &b) == RENDER_ERR_INIT); T(e == NULL && !b.initialized && b.state == NULL);
    puts("review 23: backend init success/failure executes on work pool and adopts mailbox result passed"); return 0;
}
static int review_close_flush(void)
{
    char path[] = "/tmp/editor-close-flush-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.journal_path = path, .cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(native_key(e, b.config.platform, "AC01", 0) == 0);
    raw_close(b.config.platform); raw_barrier(b.config.platform);
    /* Production run uses an indefinite wait. CLOSE must bypass frame finishing
     * and return a flushed acknowledgement on this very turn. */
    int rc = editor_step(e, -1); T(rc == EDITOR_CLOSED);
    journal_stats js = journal_get_stats(e->journal); T(js.durable_sequence == js.accepted_sequence);
    editor_close(e); replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0 && m.len == 1 && m.bytes[0] == 'a'); unlink(path);
    puts("required WM_DELETE_WINDOW: one turn, flushed recovery and shutdown passed"); return 0;
}
static int review_suite(const char *only)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        {"1", review_native_order}, {"2", review_native_burst}, {"3", review_crash}, {"4", review_suffix}, {"4-exit", review_journal_exit}, {"4-session", review_checkpoint_session}, {"5", review_base_race},
        {"6", review_style}, {"7", review_long_line}, {"8", review_prefix_open}, {"10", review_index_backlog}, {"11-accounting", review_slice_accounting}, {"12", review_large_undo}, {"15", review_piece_recycling},
        {"13", review_bursts}, {"14", review_fairness}, {"17", review_journal_idle}, {"22", review_replay_atomic}, {"23", review_init}, {"close", review_close_flush}
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        if ((!only && (atoi(cases[i].name) <= 7 || !strcmp(cases[i].name, "10") ||
            !strcmp(cases[i].name, "11-accounting") || !strcmp(cases[i].name, "12") ||
            !strcmp(cases[i].name, "15") || !strcmp(cases[i].name, "17") ||
            !strcmp(cases[i].name, "22") || !strcmp(cases[i].name, "23") || !strcmp(cases[i].name, "close"))) ||
            (only && (!strcmp(only, "all") || !strcmp(only, cases[i].name)))) failed |= cases[i].fn();
    return failed;
}
static int backend_selection(void)
{
    render_backend b = {0};
    T(editor_backend_select(&b, NULL) == 0);
    T((b.info.capabilities & RENDER_CAP_GPU) != 0);
    T(editor_backend_select(&b, "gl") == 0);
    T((b.info.capabilities & RENDER_CAP_GPU) != 0);
    T(editor_backend_select(&b, "raster") == 0);
    T((b.info.capabilities & RENDER_CAP_RASTER_POOL) != 0);
    render_backend saved = b;
    T(editor_backend_select(&b, "typo") == EDITOR_ERR_ARG);
    T(memcmp(&b, &saved, sizeof b) == 0);
    puts("editor_test: default EGL and gl/raster override passed"); return 0;
}
static int injected_gpu_init_failure(render_backend *b, const render_config *cfg)
{ return worker_failed_init(b, cfg); }
static int backend_fallback_case(bool injected)
{
    render_backend b = {0}; T(editor_backend_select(&b, NULL) == 0);
    editor_config cfg = {.cols = 32, .rows = 8, .raster_fallback = true};
    /* A deterministic init return tests the selected default independently
     * of native capabilities; the loader seam also exercises real cleanup. */
    if (injected) b.ops.init = injected_gpu_init_failure;
    else T(setenv("EDIT_GL_EGL_LIBRARY", "/tmp/edit-zzj.15-no-such-EGL.so", 1) == 0);
    editor *e = NULL;
    FILE *log = tmpfile(); T(log != NULL);
    int saved_stderr = dup(STDERR_FILENO); T(saved_stderr >= 0);
    T(dup2(fileno(log), STDERR_FILENO) >= 0);
    int opened = editor_open(&e, &cfg, &b);
    T(dup2(saved_stderr, STDERR_FILENO) >= 0);
    T(opened == 0);
    T(editor_get_stats(e).backend_init_error == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED));
    T((b.info.capabilities & RENDER_CAP_RASTER_POOL) != 0);
    if (!injected) T(unsetenv("EDIT_GL_EGL_LIBRARY") == 0);
    /* Capture through typing/close too: a later retry/log must fail the
     * one-line assertion, even after the loader fault has been removed. */
    T(dup2(fileno(log), STDERR_FILENO) >= 0);
    T(settle(e) == 0); T(press(e, key('x', 0, "x")) == 0);
    T(expect(e, "x", 1) == 0);
    T((b.info.capabilities & RENDER_CAP_RASTER_POOL) != 0);
    editor_close(e);
    T(dup2(saved_stderr, STDERR_FILENO) >= 0); close(saved_stderr);
    rewind(log); char line[256]; T(fgets(line, sizeof line, log) != NULL);
    T(strstr(line, "EGL init failed:") && strstr(line, "code=") && strstr(line, "using raster; no retry"));
    T(fgets(line, sizeof line, log) == NULL); fclose(log);
    T(!b.initialized && b.state == NULL);
    /* With fallback disabled a GPU init error remains an error. */
    T(editor_backend_select(&b, NULL) == 0); cfg.raster_fallback = false;
    if (injected) b.ops.init = injected_gpu_init_failure;
    else T(setenv("EDIT_GL_EGL_LIBRARY", "/tmp/edit-zzj.15-no-such-EGL.so", 1) == 0);
    T(editor_open(&e, &cfg, &b) == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED));
    T(e == NULL && !b.initialized);
    if (!injected) T(unsetenv("EDIT_GL_EGL_LIBRARY") == 0);
    printf("editor_test: default EGL %s failure -> raster, one log through typing/close passed\n",
        injected ? "injected init" : "dlopen"); return 0;
}
static int backend_fallback(void)
{
    T(backend_fallback_case(true) == 0);
    T(backend_fallback_case(false) == 0);
    puts("editor_test: failed EGL init falls back once and stays raster passed"); return 0;
}
static int gpu_polled_event(render_backend *b, const render_event *ev)
{
    if (!ev->work || ev->work->kind != GL_POLL_MESSAGE) return RENDER_ERR_UNSUPPORTED;
    ((delayed *)b->state)->ready = true;
    return delayed_present(b, b->active_frame);
}
static int gpu_polled_present(render_backend *b, uint32_t id)
{ (void)b; (void)id; return 0; }
static int backend_gpu_completion(void)
{
    /* Native EGL fails on :99. This seam models a fence observed only after
     * present, with no new input/mailbox traffic to wake the editor. */
    render_backend b = {.info = {"editor GPU poll test", sizeof(delayed), 16, RENDER_CAP_HEADLESS | RENDER_CAP_GPU},
        .ops = {delayed_init, delayed_resize, delayed_submit, gpu_polled_present, gpu_polled_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0);
    T(settle(e) == 0);
    T(b.device_seen && b.complete_seen && !b.active);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    uint64_t before = editor_get_stats(e).poll_returns;
    T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == before + 1);
    T(b.stats.submitted_frames == 2);
    editor_close(e);
    puts("editor_test: GPU completion progresses and idle disarms polling passed"); return 0;
}
static int selected_backend_test(const char *name, bool require_gl)
{
    editor *e = NULL;
    int result = 1;
    bool path_created = false;
    char path[] = "/tmp/editor-selected-XXXXXX";
    counted c = {0};
#define SELECT_T(c) do { if (!(c)) { fprintf(stderr, "editor_test:%d: FAIL %s\n", __LINE__, #c); goto cleanup; } } while (0)
    render_backend b = {0}; SELECT_T(editor_backend_select(&b, name) == 0);
    int fd = mkstemp(path); SELECT_T(fd >= 0); path_created = true; close(fd);
    editor_config cfg = {.cols = 40, .rows = 8, .journal_path = path, .raster_fallback = true,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    SELECT_T(editor_open(&e, &cfg, &b) == 0); SELECT_T(settle(e) == 0);
    SELECT_T(!require_gl || (b.info.capabilities & RENDER_CAP_GPU));
    for (unsigned i = 0; i < 10000; i += 100) {
        for (unsigned j = 0; j < 100; j++) {
            plat_event ev = j % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
            SELECT_T(editor_inject(e, &ev) == 0);
        }
        SELECT_T(settle(e) == 0);
    }
    SELECT_T(!c.active && !c.suspended && c.allocations == 0 && editor_length(e) == 0);
    editor_stats s = editor_get_stats(e);
    SELECT_T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    printf("editor_test: requested=%s actual=%s 10000 keys mallocs=%zu guard=%s\n",
        name ? name : "default", b.info.name, c.allocations, edit_malloc_guard_active() ? "active" : "ASan-inert");
    SELECT_T(editor_flush(e) == 0); editor_close(e); e = NULL; unlink(path); path_created = false;
    /* Same idle fixture as idle_policy, without a journal deadline. Test both
     * natural Xvfb EGL failure and the explicit raster override. */
    SELECT_T(editor_backend_select(&b, name) == 0); cfg.journal_path = NULL;
    SELECT_T(editor_open(&e, &cfg, &b) == 0); SELECT_T(settle(e) == 0);
    SELECT_T(!require_gl || (b.info.capabilities & RENDER_CAP_GPU));
    uint64_t deadline = trace_now_ns() + UINT64_C(15000000000);
    while (editor_get_stats(e).blinking && trace_now_ns() < deadline) SELECT_T(editor_step(e, -1) >= 0);
    SELECT_T(settle(e) == 0);
    s = editor_get_stats(e); SELECT_T(!s.blinking && s.cursor_visible);
    uint64_t before = s.poll_returns;
    SELECT_T(editor_step(e, 100) == EDITOR_OK);
    SELECT_T(editor_get_stats(e).poll_returns == before + 1);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; SELECT_T(press(e, focus) == 0);
    before = editor_get_stats(e).poll_returns;
    SELECT_T(editor_step(e, 100) == EDITOR_OK);
    SELECT_T(editor_get_stats(e).poll_returns == before + 1);
    printf("editor_test: requested=%s actual=%s idle/unfocused background wakeups=0 passed\n", name ? name : "default", b.info.name);
    result = 0;
cleanup:
    /* Assertions retain their strength, but failure must still release the
     * live backend's worker allocations before returning to main/LSan. */
    if (c.active) { (void)edit_malloc_guard_end(); c.active = false; }
    c.suspended = false;
    editor_close(e);
    if (path_created) unlink(path);
    return result;
#undef SELECT_T
}
static int partial_line_start(void)
{
    const size_t len = 8u * 1024u * 1024u;
    uint8_t *bytes = malloc(len); T(bytes != NULL);
    for (size_t i = 0; i < len; i++) bytes[i] = i % 128u == 127u ? '\n' : 'a';
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    const uint64_t starts[] = {0, (uint64_t)(len / 128u * 9u / 10u) * 128u, len - 128u};
    for (size_t at = 0; at < sizeof starts / sizeof starts[0]; at++) {
        uint64_t start = starts[at];
        T(editor_set_cursor(e, start + 10u) == 0); T(settle(e) == 0);
        lineidx_destroy(e->buffer->index);
        e->buffer->index = lineidx_create(len); T(e->buffer->index != NULL);
        e->buffer->index_dirty = false;
        for (unsigned i = 0; i < 12; i++) {
            plat_event ev = i % 2u ? key(XKB_KEY_BackSpace, 0, NULL) :
                (i % 4u == 2u ? key(XKB_KEY_Return, 0, NULL) : key('x', 0, "x"));
            uint64_t frames = b.stats.submitted_frames;
            T(editor_inject(e, &ev) == 0);
            unsigned turns = 0;
            do {
                T(editor_step(e, 0) >= 0); turns++;
                if (view_busy(&e->v) && e->v.query_kind && e->v.query_pos < start)
                    fprintf(stderr, "partial line-start: query_byte=%llu below viewport_byte=%llu\n",
                        (unsigned long long)e->v.query_pos, (unsigned long long)start);
                T(!view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= start);
            }
            while (editor_get_stats(e).pending && turns < 16u);
            if (editor_get_stats(e).pending)
                fprintf(stderr, "partial line-start: turns=%u query_byte=%llu viewport_byte=%llu\n", turns,
                    (unsigned long long)e->v.query_pos, (unsigned long long)start);
            T(!editor_get_stats(e).pending);
            T(b.stats.submitted_frames > frames);
            T(editor_view(e).first_byte == start);
            T(e->row_byte[1] == (i % 4u == 2u ? start + 11u : start + 128u + (i % 2u ? 0u : 1u)));
            T(!editor_index_complete(e));
        }
    }
    uint64_t join = starts[1];
    T(editor_set_cursor(e, join) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(len); T(e->buffer->index != NULL);
    e->buffer->index_dirty = false;
    plat_event back = key(XKB_KEY_BackSpace, 0, NULL); T(editor_inject(e, &back) == 0);
    unsigned turns = 0;
    do {
        T(editor_step(e, 0) >= 0); turns++;
        if (view_busy(&e->v) && e->v.query_kind && e->v.query_pos < join - 128u)
            fprintf(stderr, "partial joined line-start: query_byte=%llu below anchor_byte=%llu\n",
                (unsigned long long)e->v.query_pos, (unsigned long long)(join - 128u));
        T(!view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= join - 128u);
    } while (editor_get_stats(e).pending && turns < 16u);
    T(!editor_get_stats(e).pending && editor_view(e).first_byte == join - 128u);
    T(editor_length(e) == len - 1u);
    editor_close(e);
    puts("editor_test: partial index deep typing reuses viewport line starts passed"); return 0;
}
int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    if (argc == 2) {
        if (!strcmp(argv[1], "--raster-blink")) return raster_blink_damage();
        if (!strcmp(argv[1], "--line-start-only")) return partial_line_start();
        if (!strcmp(argv[1], "--selection")) return backend_selection();
        if (!strcmp(argv[1], "--fallback")) return backend_fallback();
        if (!strcmp(argv[1], "--gpu-completion")) return backend_gpu_completion();
        if (!strcmp(argv[1], "--backend-only")) return selected_backend_test(getenv("EDIT_BACKEND"), false);
        if (!strcmp(argv[1], "--require-gl")) return selected_backend_test("gl", true);
        return 2;
    }
    const char *allocation = getenv("EDITOR_ALLOC_ONLY");
    if (allocation) return allocation_test(!strcmp(allocation, "raster"));
    const char *only = getenv("EDITOR_REVIEW_ONLY");
    if (only) return review_suite(only);
    T(review_suite(NULL) == 0);
    T(partial_line_start() == 0);
    T(backend_selection() == 0); T(backend_fallback() == 0); T(backend_gpu_completion() == 0);
    const char *name = getenv("EDIT_BACKEND");
    if (name) T(selected_backend_test(name, false) == 0);
    else { T(selected_backend_test("gl", false) == 0); T(selected_backend_test("raster", false) == 0); }
    T(scrolled_undo() == 0);
    T(stopped_error() == 0);
    T(script(false) == 0); T(allocation_test(false) == 0);
    T(script(true) == 0); T(allocation_test(true) == 0);
    T(raster_blink_damage() == 0);
    T(idle_policy() == 0);
    T(queue_depth() == 0);
    T(native_input() == 0);
    puts("editor_test: all passed"); return 0;
}
