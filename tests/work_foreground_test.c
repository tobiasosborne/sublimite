/* P1-1 MAJOR 4: real bulk clients cannot own the interactive worker. */
#include "base/base.h"
#include "editor/private.h"
#include "findui/findui.h"
#include "font/font.h"
#include "lineidx/lineidx.h"
#include "savectl/savectl.h"
#include "trace/trace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Atomic int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "work_foreground_test:%d: FAIL %s\n", __LINE__, #x); failures++; } } while (0)
static work_pool pool;
static void nap(void) { (void)nanosleep(&(struct timespec){0, 100000}, NULL); }
typedef struct pause_state { _Atomic bool entered, completed; bool enabled; int fd; } pause_state;
static void pause_io(pause_state *s)
{
    if (!s->enabled || atomic_exchange(&s->entered, true)) return;
    uint8_t byte; ssize_t n;
    do { n = read(s->fd, &byte, 1); } while (n < 0 && errno == EINTR);
    CHECK(n == 1);
    atomic_store(&s->completed, true);
}
typedef struct source { const uint8_t *bytes; size_t size; pause_state *pause; } source;
static size_t span(void *ctx, uint64_t off, const uint8_t **bytes)
{
    source *s = ctx;
    if (off >= s->size) return 0;
    if (s->pause) pause_io(s->pause);
    *bytes = s->bytes + off;
    return s->size - (size_t)off;
}
static void find_hook(void *ctx, uint32_t generation) { (void)generation; pause_io(ctx); }
static void save_hook(void *ctx, int step) { if (step == FILE_STEP_TEMP_CREATED) pause_io(ctx); }
typedef struct clients {
    edit_arena arena;
    piece_tree *tree;
    lineidx *index;
    source src;
    lineidx_src input;
    findui_panel find;
    savectl *save;
    font_fallback font;
    pause_state pauses[4];
    char path[128];
} clients;
static void route(const work_msg *msg, void *ctx)
{
    clients *c = ctx;
    (void)findui_accept(&c->find, msg);
    (void)savectl_receive(c->save, msg);
}
static void start(clients *c, uint32_t kind)
{
    if (kind == 0) CHECK(lineidx_build_start(c->index, &pool, &c->input) == 0);
    if (kind == 1) CHECK(findui_set_query(&c->find, (const uint8_t *)"a", 1) == FINDUI_OK);
    if (kind == 2) CHECK(savectl_save(c->save, c->tree, NULL, NULL, 0) == SAVECTL_OK);
    if (kind == 3) CHECK(work_submit(&pool, (work_job){font_fallback_job, &c->font, 42, WORK_BULK}).epoch != 0);
}
static void run_case(uint32_t active, uint32_t count)
{
    static const char *names[] = {"index", "find", "save", "discovery"};
    static uint8_t bytes[65536];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (i & 1u) ? '\n' : 'a';
    clients c = {0}; int io[2];
    CHECK(pipe(io) == 0);
    CHECK(work_pool_init_foreground(&pool, 1, 0) == 0);
    for (uint32_t i = 0; i < 4; i++) c.pauses[i] = (pause_state){.enabled = i == active, .fd = io[0]};
    CHECK(edit_arena_init(&c.arena, 16u * 1024u * 1024u) == 0);
    piece_allocator a = piece_default_allocator();
    c.tree = piece_create(&a); CHECK(c.tree);
    CHECK(piece_init_copy(c.tree, bytes, sizeof bytes) == PIECE_OK);
    c.index = lineidx_create(sizeof bytes); CHECK(c.index);
    c.src = (source){bytes, sizeof bytes, &c.pauses[0]};
    c.input = (lineidx_src){&c.src, sizeof bytes, span, NULL};
    findui_config config = {&c.arena, &pool, 64, 64, find_hook, &c.pauses[1]};
    CHECK(findui_init(&c.find, &config) == FINDUI_OK);
    piece_snapshot *snap = piece_snapshot_take(c.tree); CHECK(snap);
    CHECK(findui_set_source(&c.find, snap, 1) == FINDUI_OK);
    piece_snapshot_release(snap);
    CHECK(findui_show(&c.find, true, false) == FINDUI_OK);
    CHECK(snprintf(c.path, sizeof c.path, "/tmp/edit-corpus/work-foreground-%ld-%u-%u", (long)getpid(), active, count) > 0);
    savectl_options options = {.pool = &pool, .path = c.path, .source_mode = FILE_MODE_COPY,
        .reload_allocator = a, .step = save_hook, .step_ctx = &c.pauses[2]};
    CHECK(savectl_create(&c.save, &options, true) == SAVECTL_OK);
    /* Main now isolates fontconfig in a child. Hold its actual source wait
     * through the public discovery-program seam instead of intercepting dlopen. */
    c.font.discovery_program = "/proc/self/exe";
    char discovery_fd[32];
    CHECK(snprintf(discovery_fd, sizeof discovery_fd, "%d", io[0]) > 0);
    CHECK(setenv("WORK_FOREGROUND_DISCOVERY_FD", discovery_fd, 1) == 0);
    for (uint32_t i = 0; i < count; i++)
        start(&c, active == 3 ? (i == 0 ? 3 : i - 1) : (active + i) % 3u);
    uint64_t deadline = trace_now_ns() + 2000000000ull;
    while (!(active == 3 ? atomic_load(&c.font.child_pid) != 0 :
        atomic_load(&c.pauses[active].entered)) && trace_now_ns() < deadline) nap();
    CHECK(active == 3 ? atomic_load(&c.font.child_pid) != 0 :
        atomic_load(&c.pauses[active].entered));

    source resident = {bytes, sizeof bytes, NULL};
    lineidx_src input = {&resident, sizeof bytes, span, NULL};
    lineidx *jump = lineidx_create(sizeof bytes); CHECK(jump);
    uint64_t request = trace_now_ns();
    if (count == 1) CHECK(lineidx_build_start_foreground(jump, &pool, &input) == 0);
    else {
        CHECK(lineidx_build_start(jump, &pool, &input) == 0);
        CHECK(lineidx_build_prioritize(jump) == 0);
    }
    const uint64_t deadline_ns = 2000000000ull; /* (G) fixture watchdog, not a timing verdict */
    while (!lineidx_complete(jump) && trace_now_ns() - request < deadline_ns) {
        (void)lineidx_poll(jump); nap();
    }
    uint64_t elapsed = trace_now_ns() - request;
    bool exact = lineidx_complete(jump);
    CHECK(exact);
    CHECK(active == 3 ? atomic_load(&c.font.child_pid) != 0 :
        !atomic_load(&c.pauses[active].completed));
    if (exact) {
        lineidx_result answer = lineidx_line_to_byte(jump, &input, 1234);
        CHECK(answer.exact && answer.value == 2468);
    }
    printf("work_foreground_test: active=%s real_bulk_jobs=%u exact_jump=%d elapsed_ns=%llu (M)[AC] watchdog_ns=%llu (G)\n",
        names[active], count, exact ? 1 : 0, (unsigned long long)elapsed, (unsigned long long)deadline_ns);
    uint8_t byte = 1; CHECK(write(io[1], &byte, 1) == 1);
    deadline = trace_now_ns() + 5000000000ull;
    while (trace_now_ns() < deadline) {
        (void)work_mailbox_drain(&pool, route, &c);
        (void)lineidx_poll(c.index); (void)lineidx_poll(jump);
        (void)findui_service(&c.find); savectl_tick(c.save);
        if (!lineidx_building(c.index) && !lineidx_building(jump) &&
            !findui_get_state(&c.find).searching && !savectl_get_model(c.save).busy) break;
        nap();
    }
    CHECK(trace_now_ns() < deadline);
    lineidx_destroy(jump); lineidx_destroy(c.index);
    findui_code code;
    do { code = findui_dispose(&c.find); if (code == FINDUI_MORE) nap(); } while (code == FINDUI_MORE);
    CHECK(code == FINDUI_OK);
    int destroy;
    do { savectl_tick(c.save); destroy = savectl_destroy(c.save); if (destroy == SAVECTL_BUSY) nap(); }
    while (destroy == SAVECTL_BUSY && trace_now_ns() < deadline);
    CHECK(destroy == SAVECTL_OK);
    work_pool_shutdown(&pool); CHECK(unsetenv("WORK_FOREGROUND_DISCOVERY_FD") == 0);
    piece_destroy(c.tree); edit_arena_free(&c.arena);
    if (unlink(c.path) != 0) CHECK(errno == ENOENT);
    CHECK(close(io[0]) == 0 && close(io[1]) == 0);
}
typedef struct fragmented_source {
    uint8_t bytes[131072];
    pause_state pause;
    _Atomic uint32_t calls;
    _Atomic bool released;
} fragmented_source;
static size_t fragmented_span(void *ctx, uint64_t off, const uint8_t **bytes)
{
    fragmented_source *f = ctx;
    if (off >= sizeof f->bytes) return 0;
    pause_io(&f->pause);
    atomic_fetch_add(&f->calls, 1);
    *bytes = f->bytes + off;
    size_t left = sizeof f->bytes - (size_t)off;
    return left < 37u ? left : 37u;
}
static void fragmented_release(void *ctx) { atomic_store(&((fragmented_source *)ctx)->released, true); }
static void observe_slice(work_ctx *ctx)
{
    fragmented_source *f = ctx->arg;
    work_msg msg = {.kind = atomic_load(&f->calls), .generation = ctx->generation};
    CHECK(work_publish(ctx, &msg));
}
typedef struct slice_observation { uint32_t calls; bool received; } slice_observation;
static void slice_reply(const work_msg *msg, void *ctx)
{
    slice_observation *seen = ctx;
    seen->calls = msg->kind; seen->received = true;
}
static void run_slice_case(void)
{
    fragmented_source f = {0}; int io[2];
    CHECK(pipe(io) == 0);
    CHECK(work_pool_init_foreground(&pool, 1, 0) == 0);
    for (size_t i = 0; i < sizeof f.bytes; i++) f.bytes[i] = (i & 1u) ? '\n' : 'a';
    f.pause = (pause_state){.enabled = true, .fd = io[0]};
    lineidx_src input = {&f, sizeof f.bytes, fragmented_span, fragmented_release};
    lineidx *index = lineidx_create(sizeof f.bytes); CHECK(index);
    CHECK(lineidx_build_start_foreground(index, &pool, &input) == 0);
    uint64_t deadline = trace_now_ns() + 2000000000ull;
    while (!atomic_load(&f.pause.entered) && trace_now_ns() < deadline) nap();
    CHECK(atomic_load(&f.pause.entered));
    work_handle reply = work_submit(&pool, (work_job){observe_slice, &f, 99, WORK_FOREGROUND});
    CHECK(reply.epoch);
    uint8_t byte = 1; CHECK(write(io[1], &byte, 1) == 1);
    slice_observation seen = {0};
    while (!seen.received && trace_now_ns() < deadline) {
        (void)work_mailbox_receive(&pool, reply, 99, slice_reply, &seen);
        nap();
    }
    CHECK(seen.received && seen.calls <= 64u);
    CHECK(!atomic_load(&f.released));
    while (!lineidx_complete(index) && trace_now_ns() < deadline) { (void)lineidx_poll(index); nap(); }
    CHECK(lineidx_complete(index));
    lineidx_result count = lineidx_line_count(index);
    CHECK(count.exact && count.value == sizeof f.bytes / 2u + 1u);
    lineidx_destroy(index);
    CHECK(atomic_load(&f.released));
    /* Seek progress and its terminal answer must also survive invocations.
     * Exercise zero, a partial chunk target, and EOF on the fragmented lease. */
    const uint64_t targets[] = {0, 5000, UINT64_MAX};
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        atomic_store(&f.released, false);
        index = lineidx_create(sizeof f.bytes); CHECK(index);
        CHECK(lineidx_seek_start_owned(index, &pool, &input, targets[i], 0) == 0);
        (void)lineidx_build_prioritize(index);
        lineidx_result result = {0}; bool ready = false;
        deadline = trace_now_ns() + 2000000000ull;
        while (!ready && trace_now_ns() < deadline) {
            ready = lineidx_seek_result(index, &result); nap();
        }
        uint64_t expected = i == 0 ? 0 : i == 1 ? 10000 : sizeof f.bytes;
        CHECK(ready && result.exact && result.value == expected);
        if (i == 1) CHECK(!lineidx_complete(index));
        lineidx_build_cancel(index);
        CHECK(!lineidx_seek_result(index, &result));
        lineidx_destroy(index); CHECK(atomic_load(&f.released));
    }
    puts("work_foreground_test: fragmented worker seeks zero/partial/EOF + cancellation suppression checked");
    work_pool_shutdown(&pool);
    close(io[0]); close(io[1]);
    printf("work_foreground_test: running_index_yields fragment_spans_before_reply=%u bound=64 (G) lease_released=%d\n",
           seen.calls, atomic_load(&f.released));
}
static void hold_bulk(work_ctx *ctx) { pause_io(ctx->arg); }
static void run_editor_case(void)
{
    uint8_t bytes[65536];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (i & 1u) ? '\n' : 'a';
    char path[] = "/tmp/edit-corpus/work-editor-XXXXXX";
    int fd = mkstemp(path); CHECK(fd >= 0);
    CHECK(write(fd, bytes, sizeof bytes) == (ssize_t)sizeof bytes); close(fd);
    render_backend backend = {0}; CHECK(render_null_backend(&backend) == 0);
    editor_config config = {.path = path, .cols = 20, .rows = 4};
    editor *e = NULL; CHECK(editor_open(&e, &config, &backend) == 0);
    CHECK(file_open_mode(e->buffer->file) == FILE_MODE_COPY);
    lineidx_destroy(e->buffer->index); e->buffer->index = NULL;
    int io[2]; CHECK(pipe(io) == 0);
    pause_state paused = {.enabled = true, .fd = io[0]};
    work_handle bulk = work_submit(&e->pool, (work_job){hold_bulk, &paused, 1, WORK_BULK});
    CHECK(bulk.epoch);
    uint64_t deadline = trace_now_ns() + 2000000000ull;
    while (!atomic_load(&paused.entered) && trace_now_ns() < deadline) nap();
    CHECK(atomic_load(&paused.entered));
    /* Attach the ordinary opening build behind an already blocked bulk job.
     * The initial copy is resident; no test-only foreground submission is used. */
    source resident = {bytes, sizeof bytes, NULL};
    lineidx_src input = {&resident, sizeof bytes, span, NULL};
    e->buffer->index = lineidx_create(sizeof bytes); CHECK(e->buffer->index);
    CHECK(lineidx_build_start(e->buffer->index, &e->pool, &input) == 0);
    int rc = editor_jump_line(e, 1234);
    while (rc == EDITOR_MORE && trace_now_ns() < deadline) {
        CHECK(editor_step(e, 0) >= 0);
        rc = editor_jump_line(e, 1234); nap();
    }
    CHECK(rc == EDITOR_OK && editor_view(e).selection.cursor == 2468);
    CHECK(!atomic_load(&paused.completed));
    uint8_t byte = 1; CHECK(write(io[1], &byte, 1) == 1);
    editor_close(e); close(io[0]); close(io[1]); CHECK(unlink(path) == 0);
    puts("work_foreground_test: editor_resident_jump_before_bulk_release checked");
}
int main(int argc, char **argv)
{
    if (argc > 3 && strcmp(argv[1], "-f") == 0) {
        const char *value = getenv("WORK_FOREGROUND_DISCOVERY_FD");
        if (value && strstr(argv[3], "charset=4e2d")) {
            int fd = atoi(value); uint8_t byte; ssize_t n;
            do { n = read(fd, &byte, 1); } while (n < 0 && errno == EINTR);
            return n == 1 ? 0 : 1;
        }
        return 0;
    }
    trace_init();
    for (uint32_t count = 1; count <= 3; count += 2)
        for (uint32_t active = 0; active < 4; active++) run_case(active, count);
    run_slice_case();
    run_editor_case();
    printf("work_foreground_test: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
