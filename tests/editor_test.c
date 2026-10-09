#include "editor/editor.h"
#include "raster/raster.h"
#include "base/base.h"
#include "trace/trace.h"
#include "journal/journal.h"
#include "x11/input.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    T(!c.active && !c.suspended); T(c.allocations == 0); T(editor_length(e) == 0);
    editor_stats s = editor_get_stats(e); T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    T(editor_flush(e) == 0); editor_close(e); unlink(path);
    printf("editor_test: %s 10000 keys mallocs=%zu guard=%s\n", raster ? "raster" : "null", c.allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert");
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
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(scrolled_undo() == 0);
    T(stopped_error() == 0);
    T(script(false) == 0); T(allocation_test(false) == 0);
    T(script(true) == 0); T(allocation_test(true) == 0);
    T(idle_policy() == 0);
    T(queue_depth() == 0);
    T(native_input() == 0);
    puts("editor_test: all passed"); return 0;
}
