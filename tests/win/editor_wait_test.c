/* Native wait integration: every fixture operation is outside typing. */
#if defined(_WIN32)
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base/base.h"
#include "editor/wait.h"
#include "x11/plat.h"
#include "work/work.h"
#include "ipc/ipc.h"
#include "file/file.h"
/* Exercise the context-free console callback without broadcasting a control
 * event to unrelated programs sharing the test runner's console. */
#include "../../src/win32/win32_close_win.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "editor_wait: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
typedef struct fixture {
    plat platform; editor_wait_set set; os_wake start;
    uint32_t expected; bool seen, input, close;
} fixture;
static size_t sources(void *ctx, void **handles, size_t capacity)
{
    fixture *f = ctx;
    size_t count = editor_wait_handles(&f->set, handles, capacity);
    os_wake_signal(&f->start); return count;
}
static void work_ready(void *ctx)
{
    fixture *f = ctx; uint32_t tags[EDITOR_WAIT_CAP]; size_t count = 0;
    CHECK(editor_wait_ready(&f->set, 0, tags, EDITOR_WAIT_CAP, &count) == OS_OK);
    for (size_t i = 0; i < count; i++) if (tags[i] == f->expected) f->seen = true;
    if (f->seen) plat_quit(&f->platform);
}
static void input_ready(void *ctx, const plat_event *event)
{
    fixture *f = ctx;
    if (event->kind == PLAT_EV_KEY) { f->input = true; plat_quit(&f->platform); }
    if (event->kind == PLAT_EV_CLOSE) { f->close = true; plat_quit(&f->platform); }
}
static void run_wake(fixture *f, uint32_t tag, const char *name)
{
    f->expected = tag; f->seen = false; f->platform.quit = false;
    plat_callbacks cb = {.ud = f, .on_event = input_ready, .on_work = work_ready};
    /* (G) reuse IPC's existing request deadline as a hang guard; correctness
     * is which source returned, with no new latency budget. */
    CHECK(plat_run_for(&f->platform, &cb, (int)IPC_REQUEST_DEADLINE_MS) == PLAT_OK);
    CHECK(f->seen); os_wake_drain(&f->start); printf("editor_wait: %s passed\n", name);
}
static void publish(work_ctx *ctx)
{
    fixture *f = ctx->arg; CHECK(os_wake_wait(&f->start, -1) == OS_OK);
    work_msg message = {0}; CHECK(work_publish(ctx, &message));
}
typedef struct client { fixture *f; const char *endpoint; ipc_request request; ipc_result result; } client;
static void send_request(void *ctx)
{
    client *c = ctx; CHECK(os_wake_wait(&c->f->start, -1) == OS_OK);
    c->result = ipc_client_send(c->endpoint, &c->request, (int)IPC_REQUEST_DEADLINE_MS);
}
static ipc_result accept_request(const ipc_request *request, ipc_token token, void *ctx)
{ (void)request; (void)token; *(bool *)ctx = true; return IPC_OK; }
typedef struct file_change { fixture *f; const char *path; } file_change;
static void change_file(void *ctx)
{
    file_change *change = ctx; CHECK(os_wake_wait(&change->f->start, -1) == OS_OK);
    os_file file; size_t done;
    CHECK(os_file_open(&file, change->path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC) == OS_OK);
    CHECK(os_file_write(&file, "x", 1, &done) == OS_OK && done == 1);
    os_file_close(&file);
}
typedef struct close_change { fixture *f; os_wake *wake; } close_change;
static void request_close(void *ctx)
{
    close_change *change = ctx; CHECK(os_wake_wait(&change->f->start, -1) == OS_OK);
    os_wake_signal(change->wake);
}
typedef struct console_change { fixture *f; DWORD control; } console_change;
static void request_console(void *ctx)
{
    console_change *change = ctx; CHECK(os_wake_wait(&change->f->start, -1) == OS_OK);
    CHECK(console_control(change->control));
}
static void console_case(fixture *f, DWORD control, const char *name)
{
    win32_close close_request; CHECK(win32_close_init(&close_request) == 0);
    CHECK(editor_wait_add(&f->set, &close_request.wake, EDITOR_WAIT_CLOSE) == OS_OK);
    console_change change = {f, control}; os_thread thread;
    CHECK(os_thread_start(&thread, request_console, &change) == OS_OK);
    run_wake(f, EDITOR_WAIT_CLOSE, name);
    CHECK(editor_wait_remove(&f->set, EDITOR_WAIT_CLOSE) == OS_OK);
    win32_close_finish(&close_request); CHECK(os_thread_join(&thread) == OS_OK);
}
static void capacity_case(void)
{
    editor_wait_set set; CHECK(editor_wait_init(&set) == OS_OK);
    os_wake wakes[EDITOR_WAIT_CAP + 1u];
    for (size_t i = 0; i < EDITOR_WAIT_CAP + 1u; i++) {
        CHECK(os_wake_init(&wakes[i]) == OS_OK);
        CHECK(editor_wait_add(&set, &wakes[i], (uint32_t)i + 1u) ==
              (i < EDITOR_WAIT_CAP ? OS_OK : OS_E_NOSPC));
    }
    CHECK(editor_wait_add(&set, &wakes[0], 1) == OS_OK); /* duplicate is idempotent even when full */
    CHECK(editor_wait_remove(&set, 1) == OS_OK);
    CHECK(editor_wait_add(&set, &wakes[EDITOR_WAIT_CAP], EDITOR_WAIT_CAP + 1u) == OS_OK);
    os_wake_signal(&wakes[EDITOR_WAIT_CAP]);
    uint32_t tags[EDITOR_WAIT_CAP]; size_t count = 0;
    edit_malloc_guard_begin();
    os_err result = editor_wait_ready(&set, 0, tags, EDITOR_WAIT_CAP, &count);
    size_t allocations = edit_malloc_guard_end();
    CHECK(result == OS_OK && count == 1 && tags[0] == EDITOR_WAIT_CAP + 1u);
    CHECK(allocations == 0);
    editor_wait_close(&set);
    /* Closing the set never closes borrowed sources. */
    CHECK(os_wake_wait(&wakes[EDITOR_WAIT_CAP], 0) == OS_OK);
    for (size_t i = 0; i < EDITOR_WAIT_CAP + 1u; i++) os_wake_close(&wakes[i]);
    puts("editor_wait: capacity, unregister, borrowed lifetime and allocation-free readiness passed");
}
int main(void)
{
    capacity_case();
    fixture f = {0}; CHECK(editor_wait_init(&f.set) == OS_OK);
    CHECK(os_wake_init(&f.start) == OS_OK);
    plat_config cfg = {.title = "sublimite editor wait test", .width = 320, .height = 180,
        .work_sources = sources, .work_sources_ctx = &f}; /* (E) reuse win32_plat_test's small window fixture. */
    CHECK(plat_init_backend(&f.platform, &cfg, PLAT_BACKEND_WIN32) == PLAT_OK);
    work_pool pool; CHECK(work_pool_init(&pool, 1, 0) == 0);
    CHECK(editor_wait_add(&f.set, work_pool_wake(&pool), EDITOR_WAIT_POOL) == OS_OK);
    work_handle job = work_submit(&pool, (work_job){publish, &f, 0, WORK_BULK}); CHECK(job.epoch);
    run_wake(&f, EDITOR_WAIT_POOL, "work pool");
    CHECK(editor_wait_remove(&f.set, EDITOR_WAIT_POOL) == OS_OK);
    work_pool_shutdown(&pool);
    os_wake close_request; CHECK(os_wake_init(&close_request) == OS_OK);
    CHECK(editor_wait_add(&f.set, &close_request, EDITOR_WAIT_CLOSE) == OS_OK);
    os_thread thread; close_change close = {&f, &close_request};
    CHECK(os_thread_start(&thread, request_close, &close) == OS_OK);
    run_wake(&f, EDITOR_WAIT_CLOSE, "close request");
    CHECK(os_thread_join(&thread) == OS_OK);
    os_wake_drain(&close_request);
    uint32_t tag; size_t count;
    CHECK(editor_wait_ready(&f.set, 0, &tag, 1, &count) == OS_OK && !count);
    CHECK(editor_wait_remove(&f.set, EDITOR_WAIT_CLOSE) == OS_OK);
    os_wake_close(&close_request);
    console_case(&f, CTRL_C_EVENT, "console Ctrl+C bridge");
    console_case(&f, CTRL_CLOSE_EVENT, "console close bridge");
    char endpoint[OS_PATH_MAX], cwd[OS_PATH_MAX];
    CHECK(snprintf(endpoint, sizeof endpoint, "editor-wait-%llu", (unsigned long long)os_proc_id()) > 0);
    ipc_server server = {0}; CHECK(ipc_server_init(&server, endpoint) == IPC_OK);
    CHECK(editor_wait_add(&f.set, ipc_server_wake(&server), EDITOR_WAIT_IPC) == OS_OK);
    CHECK(os_cwd(cwd, sizeof cwd) == OS_OK);
    client c = {.f = &f, .endpoint = endpoint, .request = {.cwd = cwd}};
    CHECK(os_thread_start(&thread, send_request, &c) == OS_OK);
    run_wake(&f, EDITOR_WAIT_IPC, "ipc");
    bool accepted = false;
    uint64_t deadline = os_now_ns() + (uint64_t)IPC_REQUEST_DEADLINE_MS * UINT64_C(1000000);
    while (!accepted && os_now_ns() < deadline) {
        CHECK(ipc_server_drain(&server, accept_request, &accepted) == IPC_OK);
        if (!accepted) (void)os_wake_wait(ipc_server_wake(&server), INT64_C(1000000));
    }
    CHECK(accepted && os_thread_join(&thread) == OS_OK && c.result == IPC_OK);
    CHECK(editor_wait_remove(&f.set, EDITOR_WAIT_IPC) == OS_OK); ipc_server_fini(&server);
    char temp[OS_PATH_MAX], directory[OS_PATH_MAX], path[OS_PATH_MAX];
    CHECK(os_temp_dir(temp, sizeof temp) == OS_OK);
    CHECK(snprintf(directory, sizeof directory, "%s\\sublimite-test-editor-wait-%llu", temp,
                   (unsigned long long)os_proc_id()) > 0);
    CHECK(os_mkdir(directory) == OS_OK);
    CHECK(snprintf(path, sizeof path, "%s\\changed.txt", directory) > 0);
    file_path_watch watch = {0}; int watch_token = file_watch_path_start(&watch, path); CHECK(watch_token >= 0);
    CHECK(editor_wait_file(&f.set, watch_token) == OS_OK);
    file_change change = {&f, path}; CHECK(os_thread_start(&thread, change_file, &change) == OS_OK);
    run_wake(&f, EDITOR_WAIT_FILE, "file watch"); CHECK(os_thread_join(&thread) == OS_OK);
    file_watch_dispatch(); CHECK(file_watch_path_take(&watch));
    CHECK(editor_wait_remove(&f.set, EDITOR_WAIT_FILE) == OS_OK);
    file_watch_path_stop(&watch); CHECK(os_unlink(path) == OS_OK); CHECK(os_rmdir(directory) == OS_OK);
    CHECK(PostMessageW(plat_native_win32(&f.platform), WM_KEYDOWN, VK_F2, 1));
    f.platform.quit = false;
    plat_callbacks cb = {.ud = &f, .on_event = input_ready, .on_work = work_ready};
    CHECK(plat_run_for(&f.platform, &cb, (int)IPC_REQUEST_DEADLINE_MS) == PLAT_OK && f.input);
    puts("editor_wait: input passed");
    CHECK(PostMessageW(plat_native_win32(&f.platform), WM_CLOSE, 0, 0));
    f.platform.quit = false;
    CHECK(plat_run_for(&f.platform, &cb, (int)IPC_REQUEST_DEADLINE_MS) == PLAT_OK && f.close);
    puts("editor_wait: WM_CLOSE passed");
    f.close = false; f.platform.quit = false;
    CHECK(SendMessageW(plat_native_win32(&f.platform), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF));
    CHECK(plat_run_for(&f.platform, &cb, (int)IPC_REQUEST_DEADLINE_MS) == PLAT_OK && f.close);
    puts("editor_wait: session close passed");
    plat_shutdown(&f.platform); editor_wait_close(&f.set); os_wake_close(&f.start);
    return 0;
}
#endif
