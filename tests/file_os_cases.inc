/* Shared Linux/Windows migration regressions; no editor or native API. */
#include "os/os.h"

typedef struct file_os_messages { int seen; file_msg terminal; } file_os_messages;
static void file_os_receive(const work_msg *wm, void *ctx)
{
    file_os_messages *messages = ctx;
    file_msg m;
    if (!file_msg_decode(wm, &m) &&
        (m.kind == FILE_MSG_OPEN_READY || m.kind == FILE_MSG_OPEN_FAILED)) {
        messages->terminal = m; messages->seen = 1;
    }
}
static int file_os_terminal(work_pool *workers, file_msg *m)
{
    /* (E) Same allowance as the pre-existing file_test wait_msg:
     * 5000 attempts * 1 ms = 5 s. (P) 1 ms = 1000000 ns. */
    uint64_t until = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000);
    file_os_messages messages = {0};
    do {
        (void)work_mailbox_drain(workers, file_os_receive, &messages);
        if (messages.seen) { *m = messages.terminal; return 1; }
        os_sleep_ns(1000000);
    } while (os_now_ns() < until);
    return 0;
}
typedef struct file_os_shrink { const char *path; int called; os_err result; } file_os_shrink;
static void file_os_before_map(void *ctx)
{
    file_os_shrink *hook = ctx;
    os_file source = OS_FILE_INIT;
    hook->called++;
    hook->result = os_file_open(&source, hook->path, OS_OPEN_RDWR);
    if (hook->result == OS_OK) hook->result = os_file_set_size(&source, 0);
    os_file_close(&source);
}
static void file_os_migration_cases(work_pool *workers, const char *root)
{
    file *f = NULL;
    file_msg m = {0};
    CHECK(file_open_begin(workers, root, NULL, &f) == FILE_OK);
    CHECK(file_os_terminal(workers, &m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_NOTREG);
    file_close(f);
    puts("file_test: directory -> FILE_ERR_NOTREG: ok");

    char path[OS_PATH_MAX];
    CHECK(snprintf(path, sizeof path, "%s/shrink-before-map", root) > 0);
    os_file source = OS_FILE_INIT;
    CHECK(os_file_open(&source, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    /* (P) Two pages ensure a page beyond the shrunken EOF exists. */
    CHECK(os_file_set_size(&source, 2u * os_page_size()) == OS_OK);
    os_file_close(&source);
    file_os_shrink hook = {path, 0, OS_E_IO};
    file_open_opts opts = {1, 0};
    CHECK(file_test_open_begin(workers, path, &opts, file_os_before_map, &hook, &f) == FILE_OK);
    CHECK(file_os_terminal(workers, &m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_CHANGED);
    file_close(f);
    CHECK(hook.called == 1 && hook.result == OS_OK);
    CHECK(os_unlink(path) == OS_OK);
    puts("file_test: shrink between identity validation and map -> FILE_ERR_CHANGED: ok");
}
