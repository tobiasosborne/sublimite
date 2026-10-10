#include "editor/editor.h"
#include "editor/private.h" /* arrange an incomplete index without a worker race */
#include "raster/raster.h"
#include "trace/trace.h"
#include "base/base.h"
#include <errno.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_p4_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(10000000000);
    do {
        int rc = editor_step(e, 20); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    } while (trace_now_ns() < deadline);
    T(false); return 1;
}
static plat_event key(uint32_t sym, uint16_t mods, const char *text)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods};
    if (text) { size_t n = strlen(text); EDIT_ASSERT(n <= PLAT_UTF8_MAX); ev.utf8_len = (uint8_t)n; memcpy(ev.utf8, text, n); }
    return ev;
}
static int press(editor *e, plat_event ev) { T(editor_inject(e, &ev) == 0); return settle(e); }
static int expect(editor *e, const char *text, uint64_t cursor)
{
    uint8_t bytes[256]; size_t n = strlen(text);
    T(editor_length(e) == n); T(editor_read(e, 0, bytes, n) == 0);
    T(!memcmp(bytes, text, n)); T(editor_view(e).selection.cursor == cursor); return 0;
}
static int new_editor(editor **e, render_backend *b, bool raster, const char *text)
{
    T((raster ? render_cpu_backend(b) : render_null_backend(b)) == 0);
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = strlen(text), .cols = 64, .rows = 10};
    T(editor_open(e, &cfg, b) == 0); return settle(*e);
}
static int indentation(bool raster)
{
    editor *e = NULL; render_backend b = {0}; T(new_editor(&e, &b, raster, "    x") == 0);
    T(editor_set_cursor(e, 5) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(editor_length(e) == 10); T(expect(e, "    x\n    ", 10) == 0);
    T(press(e, key('}', 0, "}")) == 0); T(expect(e, "    x\n}", 7) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "    x\n    ", 10) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "    x", 5) == 0);
    T(!editor_tab(e, 0)->modified);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "    x\n}", 7) == 0);
    editor_close(e);
    memset(&b, 0, sizeof b); T(new_editor(&e, &b, raster, "    XX\n") == 0);
    T(editor_set_cursor(e, 4) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
    T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
    T(press(e, key('}', 0, "}")) == 0); T(expect(e, "}\n", 1) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "    XX\n", 6) == 0);
    T(editor_view(e).selection.anchor == 4);
    editor_close(e);
    memset(&b, 0, sizeof b); T(new_editor(&e, &b, raster, "\t x\r\n") == 0);
    T(editor_set_cursor(e, 3) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0); T(expect(e, "\t x\r\n\t \r\n", 7) == 0);
    editor_close(e);
    printf("editor_p4_test: %s Enter/brace/selection/CRLF atomic undo passed\n", raster ? "raster" : "null"); return 0;
}
static int queued_groups(bool raster)
{
    char path[] = "/tmp/editor-p4-groups-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    editor_config cfg = {.initial = (const uint8_t *)"    X", .initial_len = 5, .journal_path = path, .cols = 40, .rows = 8};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, 4) == 0); T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
    plat_event ev = key('Y', 0, "Y"); T(editor_inject(e, &ev) == 0);
    ev = key('z', PLAT_MOD_CTRL, NULL); T(editor_inject(e, &ev) == 0);
    for (unsigned i = 0; i < 511; i++) {
        ev = key('}', 0, "}"); T(editor_inject(e, &ev) == 0);
        ev = key('z', PLAT_MOD_CTRL, NULL); T(editor_inject(e, &ev) == 0);
    }
    T(settle(e) == 0); T(editor_get_stats(e).rejected_commands == 0);
    T(expect(e, "    X", 5) == 0); T(editor_view(e).selection.anchor == 4);
    T(editor_get_stats(e).journal_records == 2559); T(editor_flush(e) == 0);
    editor_close(e); unlink(path); printf("editor_p4_test: %s full input queue journal groups preserve every key passed\n", raster ? "raster" : "null"); return 0;
}
static const render_cell *cell(editor *e, uint32_t row, uint32_t col)
{ const render_grid *g = editor_grid(e); return &g->cells[(size_t)row * g->dims.cols + col]; }
static int decorations(bool raster)
{
    editor *e = NULL; render_backend b = {0}; T(new_editor(&e, &b, raster, "(x)  ") == 0);
    T(cell(e, 1, 4)->attrs & RENDER_ATTR_UNDERLINE);
    T(cell(e, 1, 5)->bg == 0x493038);
    editor_close(e); memset(&b, 0, sizeof b);
    T(new_editor(&e, &b, raster, "x\t\n") == 0);
    T(editor_set_cursor(e, 1) == 0); T(settle(e) == 0);
    T(cell(e, 1, 3)->attrs & RENDER_ATTR_CURSOR);
    T(cell(e, 1, 4)->bg == 0x493038); /* remaining cells of the cursor tab still paint */
    T(cell(e, 1, 5)->attrs & RENDER_ATTR_UNDERLINE);
    editor_close(e); memset(&b, 0, sizeof b);
    T(new_editor(&e, &b, raster, "(x)  \nabc\n") == 0);
    T(cell(e, 1, 2)->attrs & RENDER_ATTR_UNDERLINE); T(cell(e, 1, 4)->attrs & RENDER_ATTR_UNDERLINE);
    T(cell(e, 1, 5)->bg == 0x493038); T(cell(e, 1, 6)->bg == 0x493038);
    T(editor_set_cursor(e, 7) == 0); T(settle(e) == 0); /* start abc: old bracket decorations vanish */
    T(!(cell(e, 1, 4)->attrs & RENDER_ATTR_UNDERLINE));
    T(press(e, key('k', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('x', 0, "x")) == 0); T(editor_length(e) == 10); /* mismatched chord is consumed */
    uint64_t count = editor_get_stats(e).unimplemented_actions;
    T(press(e, key('f', PLAT_MOD_CTRL, NULL)) == 0); T(editor_get_stats(e).unimplemented_actions == count + 1);
    T(press(e, key('y', PLAT_MOD_CTRL, NULL)) == 0); T(editor_get_stats(e).unimplemented_actions == count + 2);
    T(press(e, key('k', PLAT_MOD_CTRL, NULL)) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    T(press(e, key('a', 0, "a")) == 0); T(editor_length(e) == 11);
    editor_close(e); memset(&b, 0, sizeof b);
    T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    const char text[] = "(xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx)\n";
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1, .cols = 32, .rows = 4, .wrap_mode = -1};
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(!(cell(e, 1, 2)->attrs & RENDER_ATTR_UNDERLINE)); /* mate clipped by the right edge */
    editor_close(e); printf("editor_p4_test: %s bracket/whitespace/chord paint passed\n", raster ? "raster" : "null"); return 0;
}
static int tabs_script(bool raster)
{
    char directory[] = "/tmp/editor-p4-tabs-XXXXXX", code_path[128], text_path[128];
    T(mkdtemp(directory) != NULL);
    (void)snprintf(code_path, sizeof code_path, "%s/new.c", directory);
    (void)snprintf(text_path, sizeof text_path, "%s/new.md", directory);
    editor *e = NULL; render_backend b = {0}; T(new_editor(&e, &b, raster, "alpha") == 0);
    uint64_t a = editor_tab(e, 0)->id, beta, gamma;
    errno = EACCES; /* Worker ENOENT must not depend on UI-thread errno. */
    T(editor_add_buffer(e, code_path, (const uint8_t *)"beta", 4, &beta) == 0);
    T(expect(e, "beta", 0) == 0);
    T(!editor_view(e).wrap);
    errno = EACCES;
    T(editor_add_buffer(e, text_path, (const uint8_t *)"gamma", 5, &gamma) == 0);
    T(expect(e, "gamma", 0) == 0);
    T(editor_view(e).wrap); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL)) == 0); T(editor_tab(e, editor_get_stats(e).active_tab)->id == beta);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL)) == 0); T(editor_tab(e, editor_get_stats(e).active_tab)->id == a);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_tab(e, editor_get_stats(e).active_tab)->id == beta);
    plat_event release = key(XKB_KEY_Control_L, 0, NULL); release.press = false; T(press(e, release) == 0);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL)) == 0); T(editor_tab(e, editor_get_stats(e).active_tab)->id == gamma);
    T(press(e, release) == 0);
    T(editor_set_cursor(e, 3) == 0); T(settle(e) == 0);
    T(press(e, key('x', 0, "x")) == 0); T(editor_tab(e, 2)->modified);
    T(press(e, key('w', PLAT_MOD_CTRL, NULL)) == 0); T(editor_get_stats(e).tabs == 2);
    T(press(e, key('T', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_get_stats(e).tabs == 3);
    T(editor_tab(e, 2)->id == gamma); T(expect(e, "gamxma", 4) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "gamma", 3) == 0); T(!editor_tab(e, 2)->modified);
    const render_grid *g = editor_grid(e);
    plat_event pointer = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true, .x = (int32_t)(g->dims.cell_w * 2), .y = 1};
    T(press(e, pointer) == 0);
    pointer.kind = PLAT_EV_MOTION; pointer.x = (int32_t)(g->dims.cell_w * 34); T(press(e, pointer) == 0);
    pointer.kind = PLAT_EV_BUTTON; pointer.press = false; T(press(e, pointer) == 0);
    T(editor_tab(e, 2)->id == a);
    while (editor_get_stats(e).tabs) T(editor_close_tab(e, 0) == 0);
    T(settle(e) == 0); T(editor_length(e) == 0);
    T(press(e, key('T', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_get_stats(e).tabs == 1);
    editor_close(e); T(rmdir(directory) == 0);
    printf("editor_p4_test: %s missing-path/MRU/reopen/reorder/wrap/modified passed\n", raster ? "raster" : "null"); return 0;
}
static int wrap_navigation(bool raster)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    const char text[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst";
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1, .cols = 32, .rows = 4};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_End, 0, NULL)) == 0); T(editor_view(e).selection.cursor == 22 && editor_view(e).visual_end);
    T(cell(e, 1, 23)->attrs & RENDER_ATTR_CURSOR);
    uint64_t id; T(editor_add_buffer(e, "/tmp/edit-457.16-wrap.c", cfg.initial, cfg.initial_len, &id) == 0);
    T(!editor_view(e).wrap); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL)) == 0);
    T(editor_view(e).wrap && editor_view(e).visual_end); T(cell(e, 1, 23)->attrs & RENDER_ATTR_CURSOR);
    T(press(e, key(XKB_KEY_Home, 0, NULL)) == 0); T(editor_view(e).selection.cursor == 0);
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0); T(editor_view(e).selection.cursor == 22);
    T(press(e, key(XKB_KEY_Up, 0, NULL)) == 0); T(editor_view(e).selection.cursor == 0);
    editor_close(e); printf("editor_p4_test: %s visual wrap motion/affinity/tab restore passed\n", raster ? "raster" : "null"); return 0;
}
static int wrap_geometry(bool raster)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    uint8_t text[120], lines[2000];
    for (unsigned i = 0; i < sizeof text; i++) text[i] = (uint8_t)('a' + i % 26u);
    for (unsigned i = 0; i < sizeof lines; i++) lines[i] = i % 2 ? '\n' : 'x';
    editor_config cfg = {.initial = text, .initial_len = sizeof text, .cols = 32, .rows = 4};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, 80) == 0); T(settle(e) == 0); T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
    T(editor_view(e).visual_byte == 22);
    uint64_t id; T(editor_add_buffer(e, NULL, lines, sizeof lines, &id) == 0); T(settle(e) == 0);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    T(cell(e, 1, 2)->glyph_index == text[22]); /* incoming gutter width, not the outgoing one */
    plat_event resize = {.kind = PLAT_EV_RESIZE, .w = 40u * editor_grid(e)->dims.cell_w,
        .h = 4u * editor_grid(e)->dims.cell_h};
    T(press(e, resize) == 0); T(editor_view(e).visual_byte == 0);
    T(cell(e, 1, 2)->glyph_index == text[0]);
    editor_close(e); printf("editor_p4_test: %s wrapped tab gutter/resize geometry passed\n", raster ? "raster" : "null"); return 0;
}
static int minimap_script(bool raster)
{
    char text[400]; size_t used = 0;
    for (unsigned i = 0; i < 40; i++) { text[used++] = 'a'; text[used++] = '\n'; } text[used] = 0;
    editor *e = NULL; render_backend b = {0}; T(new_editor(&e, &b, raster, text) == 0);
    T(!editor_get_stats(e).minimap_stale); T(editor_get_stats(e).minimap_fills > 0);
    const render_grid *g = editor_grid(e);
    plat_event pointer = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true,
        .x = (int32_t)((g->dims.cols - 2) * g->dims.cell_w), .y = (int32_t)(5 * g->dims.cell_h)};
    T(press(e, pointer) == 0); T(editor_view(e).first_line > 0);
    pointer.kind = PLAT_EV_MOTION; pointer.y = -10; T(press(e, pointer) == 0); T(editor_view(e).first_line == 0);
    pointer.y = (int32_t)(50 * g->dims.cell_h); T(press(e, pointer) == 0); T(editor_view(e).first_line > 30);
    pointer.kind = PLAT_EV_BUTTON; pointer.press = false; T(press(e, pointer) == 0);
    editor_close(e); printf("editor_p4_test: %s minimap same-frame/click/drag passed\n", raster ? "raster" : "null"); return 0;
}
typedef struct map_frames { editor *e; bool coherent; size_t stale; } map_frames;
static void map_submitted(void *ctx, const editor_frame *frame)
{
    map_frames *f = ctx; (void)frame;
    const render_grid *g = editor_grid(f->e);
    bool stale = editor_get_stats(f->e).minimap_stale;
    if (stale) f->stale++;
    if ((g->cells[g->dims.cols - 1].glyph_index == '!') != stale) f->coherent = false;
}
static int index_recovery(bool raster)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    size_t n = 4u * LINEIDX_CHUNK; uint8_t *bytes = malloc(n); T(bytes != NULL);
    for (size_t i = 0; i < n; i++) bytes[i] = i % 2 ? '\n' : 'x';
    map_frames frames = {.coherent = true};
    editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 40, .rows = 8,
        .on_submit = map_submitted, .hook_ctx = &frames};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); frames.e = e; free(bytes); T(settle(e) == 0);
    /* An edit cancels a partially published initial build. Arrange that public
     * index state deterministically, then drive its owner solely via the loop. */
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(editor_length(e)); T(e->buffer->index != NULL);
    T(!editor_index_complete(e)); T(press(e, key('x', 0, "x")) == 0);
    for (unsigned i = 0; i < 128; i++) T(editor_step(e, 0) >= 0);
    T(editor_index_complete(e)); T(settle(e) == 0); T(!editor_get_stats(e).minimap_stale && frames.coherent);
    editor_close(e); printf("editor_p4_test: %s interrupted index recovery/stale indicator passed (stale frames=%zu)\n", raster ? "raster" : "null", frames.stale); return 0;
}
typedef struct counted { bool active, suspended; size_t allocations; } counted;
static void ingress(void *ctx, uint64_t seq, uint64_t ns)
{ counted *c = ctx; (void)seq; (void)ns; if (!c->active) { edit_malloc_guard_begin(); c->active = true; } }
static void submitted(void *ctx, const editor_frame *f)
{ counted *c = ctx; (void)f; if (c->active) { c->allocations += edit_malloc_guard_end(); c->active = false; } }
static void io(void *ctx, bool entering)
{
    counted *c = ctx;
    if (entering && c->active) { c->allocations += edit_malloc_guard_end(); c->active = false; c->suspended = true; }
    else if (!entering && c->suspended) { edit_malloc_guard_begin(); c->active = true; c->suspended = false; }
}
typedef struct blocked { bool ready; } blocked;
static int block_init(render_backend *b, const render_config *cfg) { (void)cfg; ((blocked *)b->state)->ready = false; return 0; }
static int block_resize(render_backend *b, render_dims dims) { (void)b; (void)dims; return 0; }
static int block_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t n)
{ (void)b; (void)g; (void)strips; (void)n; return 0; }
static int block_present(render_backend *b, uint32_t id)
{
    if (!((blocked *)b->state)->ready) return RENDER_ERR_BUSY;
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, id, 0);
    return rc ? rc : render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, id, 0);
}
static int block_event(render_backend *b, const render_event *event) { (void)b; (void)event; return RENDER_ERR_UNSUPPORTED; }
static void block_close(render_backend *b) { (void)b; }
static int ipc_pending_input(ipc_server *server, const char *runtime)
{
    render_backend b = {.info = {"P4 delayed submit", sizeof(blocked), 16, RENDER_CAP_HEADLESS},
        .ops = {block_init, block_resize, block_submit, block_present, block_event, block_close}};
    counted count = {0}; editor_config cfg = {.cols = 32, .rows = 4, .server = server,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted, .on_io = io};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    for (unsigned i = 0; i < 128 && !b.active; i++) T(editor_step(e, 0) >= 0);
    T(b.active); plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
    T(editor_step(e, 0) >= 0); T(editor_get_stats(e).input_sequence == 1 && editor_get_stats(e).submitted_sequence == 0);
    uint8_t wire[1024]; size_t size = 0; ipc_request request = {.cwd = runtime, .has_stdin = true,
        .stdin_data = (const uint8_t *)"ipc", .stdin_size = 3};
    T(ipc_wire_encode(&request, wire, sizeof wire, &size) == IPC_OK);
    struct sockaddr_un address; socklen_t length = sizeof address;
    T(getsockname(server->listener, (struct sockaddr *)&address, &length) == 0);
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); T(client >= 0);
    T(connect(client, (struct sockaddr *)&address, length) == 0); T(write(client, wire, size) == (ssize_t)size);
    for (unsigned i = 0; i < 32; i++) T(editor_step(e, 0) >= 0);
    T(editor_get_stats(e).tabs == 1); /* startup allocation cannot overlap input -> submit */
    ((blocked *)b.state)->ready = true;
    uint64_t deadline = trace_now_ns() + UINT64_C(10000000000);
    while (editor_get_stats(e).submitted_sequence == 0) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(editor_get_stats(e).tabs == 1 && editor_length(e) == 1);
    while (editor_get_stats(e).tabs != 2) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(settle(e) == 0);
    T(count.allocations == 0 && !count.active && !count.suspended);
    uint8_t ack[8]; T(read(client, ack, sizeof ack) == (ssize_t)sizeof ack); T(!memcmp(ack, "EDIRA", 5)); close(client);
    editor_close(e); puts("editor_p4_test: IPC allocation deferred until pending key submission passed"); return 0;
}
static int allocation_test(bool raster, ipc_server *server)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    counted c = {0}; editor_config cfg = {.initial = (const uint8_t *)"    ()  \n", .initial_len = 9,
        .cols = 40, .rows = 8, .server = server, .history_keys = 4096,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    for (unsigned i = 1; i < 100; i++) { uint64_t id; T(editor_add_buffer(e, NULL, cfg.initial, cfg.initial_len, &id) == 0); }
    T(settle(e) == 0);
    for (unsigned i = 0; i < 10000; i++) {
        plat_event ev;
        switch (i % 8) {
        case 0: ev = key(XKB_KEY_Return, 0, NULL); break;
        case 1: ev = key('}', 0, "}"); break;
        case 2: case 3: ev = key('z', PLAT_MOD_CTRL, NULL); break;
        case 4: ev = key('x', 0, "x"); break;
        case 5: ev = key(XKB_KEY_BackSpace, 0, NULL); break;
        case 6: ev = key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL); break;
        default: ev = key(XKB_KEY_Home, 0, NULL); break;
        }
        T(editor_inject(e, &ev) == 0);
        if (i % 8 == 7) {
            ev = key(XKB_KEY_Control_L, 0, NULL); ev.press = false;
            T(editor_inject(e, &ev) == 0); T(settle(e) == 0);
        }
    }
    T(c.allocations == 0 && !c.active && !c.suspended);
    T(editor_get_stats(e).tabs == 100); editor_close(e);
    printf("editor_p4_test: %s 100 tabs 10000 mixed keys all modules mallocs=%zu guard=%s\n", raster ? "raster" : "null", c.allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert"); return 0;
}
static int ipc_script(bool raster, ipc_server *server, const char *runtime)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    editor_config cfg = {.cols = 40, .rows = 8, .server = server}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    char path[4096]; (void)snprintf(path, sizeof path, "%s/a.txt", runtime);
    FILE *stream = fopen(path, "w"); T(stream != NULL); T(fputs("abc\ndef\n", stream) >= 0); T(fclose(stream) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        ipc_request request = {.cwd = runtime, .count = 1, .wait = true, .has_stdin = true,
            .stdin_data = (const uint8_t *)"\0x", .stdin_size = 2};
        request.paths[0] = (ipc_path){path, 2, 2};
        _exit(ipc_client_send(runtime, &request, 30000) == IPC_OK ? 0 : 3);
    }
    uint64_t deadline = trace_now_ns() + UINT64_C(30000000000);
    while (editor_get_stats(e).tabs != 3) { T(editor_step(e, 20) >= 0); T(trace_now_ns() < deadline); }
    T(settle(e) == 0); uint8_t binary[2]; T(editor_length(e) == 2); T(editor_read(e, 0, binary, 2) == 0); T(binary[0] == 0 && binary[1] == 'x');
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0); T(editor_view(e).selection.cursor == 5);
    T(editor_close_tab(e, 1) == 0); T(settle(e) == 0);
    int status; T(waitpid(child, &status, WNOHANG) == 0); /* stdin still belongs to the request */
    T(editor_close_tab(e, 1) == 0); T(settle(e) == 0);
    pid_t reaped = 0;
    while (!reaped) { T(editor_step(e, 1) >= 0); reaped = waitpid(child, &status, WNOHANG); T(reaped >= 0); T(trace_now_ns() < deadline); }
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ipc_request bad = {.cwd = runtime, .count = 2}; bad.paths[0] = (ipc_path){path, 1, 1}; bad.paths[1] = (ipc_path){"/dev/null", 1, 1};
    errno = ENOENT; /* A different worker failure must not become a new file. */
    size_t before = editor_get_stats(e).tabs; T(editor_open_request(e, &bad, 0) != 0); T(editor_get_stats(e).tabs == before);
    char missing[4096]; (void)snprintf(missing, sizeof missing, "%s/new.txt", runtime);
    ipc_request fresh = {.cwd = runtime, .count = 1}; fresh.paths[0] = (ipc_path){missing, 1, 1};
    errno = EACCES;
    T(editor_open_request(e, &fresh, 0) == 0); T(editor_get_stats(e).tabs == before + 1);
    T(settle(e) == 0); T(editor_length(e) == 0); /* Failed requests do not poison later opens. */
    editor_close(e); unlink(path);
    printf("editor_p4_test: %s IPC poll/path positions/binary stdin/all-tab wait/rollback/new-path passed\n", raster ? "raster" : "null"); return 0;
}
static int ipc_positions(bool raster)
{
    char path[] = "/tmp/editor-p4-pos-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    FILE *stream = fopen(path, "wb"); T(stream != NULL);
    for (unsigned i = 0; i < 120; i++) T(fputc('a' + (int)(i % 26u), stream) != EOF);
    T(fclose(stream) == 0);
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    editor_config cfg = {.start_empty = true, .cols = 32, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0);
    ipc_request request = {.count = 1}; request.paths[0] = (ipc_path){path, 1, 45};
    T(editor_open_request(e, &request, 0) == 0); T(settle(e) == 0);
    T(editor_view(e).selection.cursor == 44 && editor_view(e).hscroll > 0);
    T(cell(e, 1, 23)->attrs & RENDER_ATTR_CURSOR);
    editor_close(e);
    size_t n = VIEW_SCAN_BOUND + 3u; uint8_t *bytes = malloc(n); T(bytes != NULL);
    memset(bytes, 'a', n); memcpy(bytes + VIEW_SCAN_BOUND - 1, "\xe4\xb8\xad", 3);
    stream = fopen(path, "wb"); T(stream != NULL); T(fwrite(bytes, 1, n, stream) == n); T(fclose(stream) == 0); free(bytes);
    memset(&b, 0, sizeof b); T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    T(editor_open(&e, &cfg, &b) == 0); request.paths[0].col = 100000;
    int rc = editor_open_request(e, &request, 0);
    T(rc == EDITOR_ERR_CAPACITY); T(editor_get_stats(e).tabs == 0); /* never publish an interior UTF-8 cursor */
    T(editor_step(e, 0) >= 0); editor_close(e); unlink(path);
    printf("editor_p4_test: %s IPC columns visible/bounded at certified clusters passed\n", raster ? "raster" : "null"); return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    char runtime[] = "/tmp/editor-p4-ipc-XXXXXX"; T(mkdtemp(runtime) != NULL);
    ipc_server server = {0}; T(ipc_server_init(&server, runtime) == IPC_OK);
    T(ipc_pending_input(&server, runtime) == 0);
    for (unsigned i = 0; i < 2; i++) {
        bool raster = i != 0;
        T(indentation(raster) == 0); T(queued_groups(raster) == 0); T(decorations(raster) == 0); T(tabs_script(raster) == 0);
        T(wrap_navigation(raster) == 0); T(wrap_geometry(raster) == 0); T(minimap_script(raster) == 0); T(ipc_script(raster, &server, runtime) == 0);
        T(ipc_positions(raster) == 0);
        T(index_recovery(raster) == 0);
        T(allocation_test(raster, &server) == 0);
    }
    ipc_server_fini(&server); char lock[4096]; (void)snprintf(lock, sizeof lock, "%s/sublimite-%lu.lock", runtime, (unsigned long)getuid());
    unlink(lock); T(rmdir(runtime) == 0);
    puts("editor_p4_test: all passed"); return 0;
}
