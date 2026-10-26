/* Windows-runnable file cases. Native scratch is created by this process. */
#include "file/file.h"
#include "file/file_test.h"
#include "base/base.h"
#include "trace/trace.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAIL %s (native %lu)\n", __FILE__, __LINE__, #c, GetLastError()); fails++; } } while (0)
#include "file_os_cases.inc"
static work_pool pool;
static char root[OS_PATH_MAX];
static void win_path(char *path, const char *name)
{ CHECK(snprintf(path, OS_PATH_MAX, "%s/%s", root, name) > 0); }
static void win_write(const char *path, const uint8_t *bytes, size_t n)
{
    os_file source = OS_FILE_INIT;
    CHECK(os_file_open(&source, path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC) == OS_OK);
    size_t off = 0;
    while (off < n) {
        size_t done = 0;
        os_err e = os_file_write(&source, bytes + off, n - off, &done);
        CHECK(e == OS_OK && done);
        if (e != OS_OK || !done) break;
        off += done;
    }
    os_file_close(&source);
}
static void win_read_equals(const char *path, const uint8_t *bytes, size_t n)
{
    os_file source = OS_FILE_INIT; os_stat st;
    CHECK(os_file_open(&source, path, OS_OPEN_READ) == OS_OK);
    CHECK(os_file_stat(&source, &st) == OS_OK && st.size == n);
    uint8_t *got = malloc(n ? n : 1); CHECK(got);
    size_t done = 0;
    CHECK(os_file_pread(&source, got, n, 0, &done) == OS_OK && done == n && !memcmp(got, bytes, n));
    free(got); os_file_close(&source);
}
static file *win_open(const char *path, uint64_t threshold)
{
    file_open_opts opts = {threshold, 7}; file *f = NULL; file_msg m = {0};
    CHECK(file_open_begin(&pool, path, &opts, &f) == FILE_OK);
    CHECK(file_os_terminal(&pool, &m) && m.kind == FILE_MSG_OPEN_READY && m.status == FILE_OK && m.generation == 7);
    return f;
}
static piece_tree *win_tree(file *f)
{
    piece_allocator allocator = piece_default_allocator(); piece_tree *t = piece_create(&allocator);
    CHECK(t && file_attach(f, t) == FILE_OK); return t;
}
static void win_eol_and_copy(void)
{
    file_prefix_info info;
    const uint8_t bytes[] = "\xef\xbb\xbfx\r\ny\nz\r";
    file_prefix_scan(bytes, sizeof bytes - 1, &info);
    CHECK(info.has_bom && info.lf == 1 && info.crlf == 1 && info.cr == 1 && info.kind == FILE_EOL_MIXED);
    char path[OS_PATH_MAX]; win_path(path, "copy-ä.txt");
    win_write(path, bytes, sizeof bytes - 1);
    file *f = win_open(path, UINT64_MAX);
    CHECK(file_prefix_ready(f) && file_open_mode(f) == FILE_MODE_COPY && file_size(f) == sizeof bytes - 1);
    size_t len = 0; const uint8_t *prefix = file_prefix(f, &len);
    CHECK(len == sizeof bytes - 1 && !memcmp(prefix, bytes, len));
    piece_tree *t = win_tree(f);
    piece_snapshot *snapshot = piece_snapshot_take(t); CHECK(snapshot);
    file_close(f); piece_destroy(t);
    uint8_t got[sizeof bytes];
    CHECK(piece_snapshot_read(snapshot, 0, got, sizeof bytes - 1) == PIECE_OK && !memcmp(got, bytes, sizeof bytes - 1));
    CHECK(!file_snapshot_faulted(snapshot)); piece_snapshot_release(snapshot);
    CHECK(os_unlink(path) == OS_OK);
    win_path(path, "empty"); win_write(path, bytes, 0);
    f = win_open(path, 1); t = win_tree(f);
    CHECK(file_size(f) == 0 && piece_len(t) == 0 && file_open_mode(f) == FILE_MODE_COPY);
    file_close(f); piece_destroy(t); CHECK(os_unlink(path) == OS_OK);
    puts("file_test: EOL/BOM, Unicode copy, empty file, retained snapshot: ok");
}
static void win_copy_suffix(void)
{
    char path[OS_PATH_MAX]; win_path(path, "copy-suffix");
    /* Two prefix windows exercise BULK pread beyond the prefix allocation. */
    size_t n = 2u * FILE_PREFIX_MAX;
    uint8_t *bytes = malloc(n), *got = malloc(n); CHECK(bytes && got);
    for (size_t i = 0; i < n; i++) bytes[i] = (uint8_t)i;
    win_write(path, bytes, n);
    file *f = win_open(path, n + 1u); CHECK(file_open_mode(f) == FILE_MODE_COPY);
    piece_tree *t = win_tree(f);
    CHECK(piece_read(t, 0, got, n) == PIECE_OK && !memcmp(got, bytes, n));
    file_close(f); piece_destroy(t);
    f = win_open(path, n); CHECK(file_open_mode(f) == FILE_MODE_MMAP);
    t = win_tree(f); CHECK(piece_read(t, 0, got, n) == PIECE_OK && !memcmp(got, bytes, n));
    file_close(f); piece_destroy(t); free(bytes); free(got); CHECK(os_unlink(path) == OS_OK);
    puts("file_test: copied suffix and exact mapping threshold: ok");
}
static void win_mapping(void)
{
    char path[OS_PATH_MAX]; win_path(path, "mapping");
    size_t len = 2u * FILE_PREFIX_MAX; uint8_t *bytes = malloc(len); CHECK(bytes);
    memset(bytes, 'M', len); win_write(path, bytes, len);
    file *f = win_open(path, 1); CHECK(file_open_mode(f) == FILE_MODE_MMAP);
    piece_tree *t = win_tree(f); piece_snapshot *snapshot = piece_snapshot_take(t); CHECK(snapshot);
    file_source *source = NULL; CHECK(file_source_acquire(f, &source) == FILE_OK);
    CHECK(file_source_validate(source) == FILE_OK);
    const uint8_t *mapped = NULL; size_t n = 0;
    CHECK(file_snapshot_bytes(snapshot, NULL, &mapped, &n) && n == len && !memcmp(mapped, bytes, len));
    file_backing *backing = file_snapshot_backing(snapshot); file_backing_acquire(backing);
    file_close(f); piece_destroy(t);
    CHECK(file_test_recover(mapped + os_page_size()) == 1);
    CHECK(file_backing_faulted(backing) && file_snapshot_faulted(snapshot) && file_source_validate(source) == FILE_ERR_CHANGED);
    uint8_t byte = 1;
    CHECK(piece_snapshot_read(snapshot, os_page_size(), &byte, 1) == PIECE_OK && byte == 0);
    CHECK(!file_snapshot_bytes(snapshot, NULL, &mapped, &n) && mapped == NULL && n == 0);
    file_backing_release(backing); piece_snapshot_release(snapshot); file_source_release(source);
    CHECK(os_unlink(path) == OS_OK); free(bytes);
    puts("file_test: mapped acquisition, retained backing, OS fault epoch: ok");
}
typedef struct win_save_messages { int seen; file_msg message; } win_save_messages;
static void win_save_receive(const work_msg *wm, void *ctx)
{
    win_save_messages *messages = ctx; file_msg m;
    if (!file_msg_decode(wm, &m) && m.kind == FILE_MSG_SAVE_DONE) { messages->seen = 1; messages->message = m; }
}
static file_msg win_wait_save(file *f)
{
    win_save_messages messages = {0};
    uint64_t until = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000); /* existing wait_msg allowance (E) */
    do {
        (void)file_save_busy(f); (void)work_mailbox_drain(&pool, win_save_receive, &messages);
        if (messages.seen) return messages.message;
        os_sleep_ns(1000000);
    } while (os_now_ns() < until);
    CHECK(0); return messages.message;
}
static void win_save_step(void *ctx, int step) { *(int *)ctx = step; }
static void win_save_roundtrip(void)
{
    char path[OS_PATH_MAX]; win_path(path, "roundtrip");
    const uint8_t before[] = "before\n", after[] = "Xbefore\n";
    win_write(path, before, sizeof before - 1);
    os_file attributes = OS_FILE_INIT;
    CHECK(os_file_open(&attributes, path, OS_OPEN_RDWR) == OS_OK);
    FILE_BASIC_INFO basic = {0}; basic.FileAttributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE;
    CHECK(SetFileInformationByHandle(os_file_handle(&attributes), FileBasicInfo, &basic, sizeof basic));
    PSECURITY_DESCRIPTOR old_sd = NULL; PSID old_owner = NULL, old_group = NULL; PACL old_dacl = NULL;
    SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    CHECK(GetSecurityInfo(os_file_handle(&attributes), SE_FILE_OBJECT, si, &old_owner, &old_group, &old_dacl, NULL, &old_sd) == ERROR_SUCCESS);
    HANDLE acl_handle = ReOpenFile(os_file_handle(&attributes), READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    CHECK(acl_handle != INVALID_HANDLE_VALUE);
    CHECK(SetSecurityInfo(acl_handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        NULL, NULL, old_dacl, NULL) == ERROR_SUCCESS);
    CloseHandle(acl_handle);
    LocalFree(old_sd); old_sd = NULL;
    CHECK(GetSecurityInfo(os_file_handle(&attributes), SE_FILE_OBJECT, si,
        &old_owner, &old_group, &old_dacl, NULL, &old_sd) == ERROR_SUCCESS);
    CHECK(os_file_set_mode(&attributes, 0444) == OS_OK);
    os_file_close(&attributes);
    file *f = win_open(path, 1); piece_tree *t = win_tree(f);
    CHECK(piece_insert(t, 0, (const uint8_t *)"X", 1) == PIECE_OK);
    int last_step = 0; file_set_step_hook(f, win_save_step, &last_step);
    CHECK(file_save_begin(f, t, 0, 9) == FILE_OK);
    file_msg m = win_wait_save(f);
    if (m.status) fprintf(stderr, "save status=%d native=%d step=%d\n", m.status, m.err_no, last_step);
    CHECK(m.status == FILE_OK && m.generation == 9 && m.size == sizeof after - 1);
    win_read_equals(path, after, sizeof after - 1);
    CHECK(os_file_open(&attributes, path, OS_OPEN_READ) == OS_OK);
    CHECK(GetFileInformationByHandleEx(os_file_handle(&attributes), FileBasicInfo, &basic, sizeof basic));
    CHECK((basic.FileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY)) ==
          (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY));
    PSECURITY_DESCRIPTOR new_sd = NULL; PSID new_owner = NULL, new_group = NULL; PACL new_dacl = NULL;
    CHECK(GetSecurityInfo(os_file_handle(&attributes), SE_FILE_OBJECT, si, &new_owner, &new_group, &new_dacl, NULL, &new_sd) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control; DWORD revision;
    CHECK(GetSecurityDescriptorControl(new_sd, &control, &revision) && (control & SE_DACL_PROTECTED));
    CHECK(EqualSid(old_owner, new_owner) && EqualSid(old_group, new_group));
    CHECK(old_dacl && new_dacl && old_dacl->AclSize == new_dacl->AclSize && !memcmp(old_dacl, new_dacl, old_dacl->AclSize));
    LocalFree(old_sd); LocalFree(new_sd); os_file_close(&attributes);
    file_close(f); piece_destroy(t); CHECK(os_unlink(path) == OS_OK);
    puts("file_test: async atomic save, security descriptor and attributes: ok");
}
static int win_cancel(void *ctx) { (void)ctx; return 1; }
static void win_save_errors(void)
{
    char path[OS_PATH_MAX]; win_path(path, "changed-save");
    const uint8_t bytes[] = "original", other[] = "external";
    win_write(path, bytes, sizeof bytes - 1);
    file *f = win_open(path, UINT64_MAX); piece_tree *t = win_tree(f);
    win_write(path, other, sizeof other - 1);
    CHECK(file_check(f, NULL) && file_save_begin(f, t, 0, 1) == FILE_ERR_CHANGED);
    CHECK(file_save_begin(f, t, FILE_SAVE_FORCE, 2) == FILE_OK);
    file_msg forced = win_wait_save(f);
    if (forced.status) fprintf(stderr, "forced save status=%d native=%d\n", forced.status, forced.err_no);
    CHECK(forced.status == FILE_OK); win_read_equals(path, bytes, sizeof bytes - 1);
    piece_snapshot *snapshot = piece_snapshot_take(t);
    file_save_args args = {.path = path, .snap = snapshot, .stop = win_cancel};
    CHECK(file_save_write(&args) == FILE_ERR_CANCELLED);
    piece_snapshot_release(snapshot); piece_destroy(t); file_close(f); CHECK(os_unlink(path) == OS_OK);
    win_path(path, "missing"); f = NULL; file_msg m = {0};
    CHECK(file_open_begin(&pool, path, NULL, &f) == FILE_OK);
    CHECK(file_os_terminal(&pool, &m) && m.status == FILE_ERR_IO && m.err_no == ERROR_FILE_NOT_FOUND);
    CHECK(file_errno(f) == (int)ERROR_FILE_NOT_FOUND); file_close(f);
    char oversized[OS_PATH_MAX + 1]; memset(oversized, 'x', OS_PATH_MAX); oversized[OS_PATH_MAX] = 0;
    SetLastError(ERROR_SUCCESS);
    CHECK(file_open_begin(&pool, oversized, NULL, &f) == FILE_ERR_IO && !f);
    CHECK(GetLastError() == ERROR_FILENAME_EXCED_RANGE);
    puts("file_test: changed refusal, FORCE, cancellation, native error: ok");
}
static int win_child_write(const char *path)
{
    os_file source = OS_FILE_INIT; size_t done = 0;
    if (os_file_open(&source, path, OS_OPEN_RDWR) != OS_OK) return 1;
    os_err e = os_file_pwrite(&source, "!", 1, 0, &done);
    os_file_close(&source); return e != OS_OK || done != 1;
}
static void win_notifications(void)
{
    char path[OS_PATH_MAX], peer_path[OS_PATH_MAX], absent[OS_PATH_MAX], exe[OS_PATH_MAX];
    win_path(path, "notification-ä"); win_path(peer_path, "peer"); win_path(absent, "absent-config");
    win_write(path, (const uint8_t *)"A", 1); win_write(peer_path, (const uint8_t *)"B", 1);
    file *f = win_open(path, UINT64_MAX), *peer = win_open(peer_path, UINT64_MAX);
    CHECK(file_watch_start(f) >= 0 && file_watch_start(peer) >= 0 && file_watch_handle());
    file_path_watch named = {0};
    CHECK(file_watch_path_start(&named, path) >= 0);
    CHECK(os_proc_self_path(exe, sizeof exe) == OS_OK);
    const char *argv[] = {exe, "--child-write", path, NULL}; os_proc child;
    CHECK(os_proc_spawn(&child, argv) == OS_OK);
    /* (E) os_test's existing native process guard, 120 s, includes cold ASan startup. */
    os_proc_status status;
    CHECK(os_proc_wait(&child, INT64_C(120) * INT64_C(1000000000), &status) == OS_OK && status.how == OS_PROC_EXITED && status.code == 0);
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0); /* existing wait_msg allowance (E) */
    uint64_t until = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000);
    do {
        (void)file_watch_poll(peer); (void)file_watch_poll(f);
        file_os_messages messages = {0}; (void)work_mailbox_drain(&pool, file_os_receive, &messages);
        if (file_changed(f)) break;
        os_sleep_ns(1000000);
    } while (os_now_ns() < until);
    CHECK(file_changed(f) && !file_changed(peer) && file_watch_path_take(&named));
    CHECK(!file_watch_path_take(&named));
    file_watch_path_stop(&named);
    CHECK(file_resolve_keep(f) == FILE_OK && !file_changed(f));
    file_close(f); /* Must leave the parent's request alive for peer/path users. */
    CHECK(file_watch_path_start(&named, absent) >= 0);
    win_write(absent, (const uint8_t *)"config", 6);
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0);
    file_watch_dispatch(); CHECK(file_watch_path_take(&named));
    file_close(peer); file_watch_path_stop(&named);
    CHECK(os_unlink(path) == OS_OK && os_unlink(peer_path) == OS_OK && os_unlink(absent) == OS_OK);
    puts("file_test: child modification -> ReadDirectoryChangesW, shared parent/name filtering, absent path: ok");
}
static void win_crash_step(void *ctx, int step)
{
    if (step == *(int *)ctx) ExitProcess((UINT)FILE_STEP_DIR_SYNCED + 1u);
}
static int win_child_save(const char *path, int step)
{
    piece_allocator allocator = piece_default_allocator(); piece_tree *t = piece_create(&allocator);
    if (!t) return 2;
    const uint8_t next[] = "new durable generation\n";
    if (piece_init_copy(t, next, sizeof next - 1) != PIECE_OK) return 2;
    piece_snapshot *snapshot = piece_snapshot_take(t);
    if (!snapshot) return 2;
    file_save_args args = {.path = path, .snap = snapshot, .step = win_crash_step, .step_ctx = &step};
    (void)file_save_write(&args);
    piece_snapshot_release(snapshot); piece_destroy(t);
    return 2; /* The selected crash hook must terminate before this point. */
}
static void win_durability(void)
{
    char path[OS_PATH_MAX], exe[OS_PATH_MAX]; win_path(path, "durability");
    const uint8_t old[] = "old durable generation\n", next[] = "new durable generation\n";
    CHECK(os_proc_self_path(exe, sizeof exe) == OS_OK);
    for (int step = FILE_STEP_TEMP_CREATED; step <= FILE_STEP_DIR_SYNCED; step++) {
        win_write(path, old, sizeof old - 1);
        char which[16]; CHECK(snprintf(which, sizeof which, "%d", step) > 0);
        const char *argv[] = {exe, "--child-save", path, which, NULL};
        os_proc child; os_proc_status status;
        CHECK(os_proc_spawn(&child, argv) == OS_OK);
        CHECK(os_proc_wait(&child, INT64_C(120) * INT64_C(1000000000), &status) == OS_OK);
        /* Next value after the step enum is reserved as the crash sentinel;
         * returning normally cannot satisfy this expected process outcome. */
        CHECK(status.how == OS_PROC_EXITED && status.code == FILE_STEP_DIR_SYNCED + 1);
        const uint8_t *expected = step >= FILE_STEP_RENAMED ? next : old;
        size_t n = step >= FILE_STEP_RENAMED ? sizeof next - 1 : sizeof old - 1;
        win_read_equals(path, expected, n);
        os_dir dir = OS_DIR_INIT; os_dir_iter it; const char *name;
        CHECK(os_dir_open(&dir, root) == OS_OK && os_dir_iter_open(&it, &dir) == OS_OK);
        for (;;) {
            CHECK(os_dir_next(&it, &name) == OS_OK);
            if (!name) break;
            CHECK(!strcmp(name, "durability") || !strncmp(name, ".durability.edit-", strlen(".durability.edit-")));
            if (strcmp(name, "durability")) CHECK(os_unlink_at(&dir, name) == OS_OK);
        }
        os_dir_iter_close(&it); os_dir_close(&dir);
    }
    CHECK(os_unlink(path) == OS_OK);
    puts("file_test: self-reexec crash at every atomic-save step: ok");
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--child-write")) return win_child_write(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "--child-save")) return win_child_save(argv[2], atoi(argv[3]));
    if (argc > 1) return 2;
    char temp[OS_PATH_MAX]; CHECK(os_temp_dir(temp, sizeof temp) == OS_OK);
    CHECK(snprintf(root, sizeof root, "%s/sublimite-test-file-%llu-%llu", temp,
        (unsigned long long)os_proc_id(), (unsigned long long)os_wall_ns()) > 0);
    CHECK(os_mkdir(root) == OS_OK); trace_init(); CHECK(work_pool_init(&pool, 1, 1) == 0);
    file_os_migration_cases(&pool, root);
    win_eol_and_copy(); win_copy_suffix(); win_mapping(); win_save_roundtrip(); win_save_errors(); win_notifications(); win_durability();
    CHECK(file_test_guard_registry() == 3);
    work_pool_shutdown(&pool); CHECK(os_rmdir(root) == OS_OK);
    puts("file_test: SKIP Linux SIGBUS signal-action chaining and real truncation: native Windows faults use the OS fault trigger; NTFS refuses mapped truncation");
    puts("file_test: SKIP POSIX FIFO, mode/uid/xattr/ACL and syscall-interposition cases: Windows metadata and native errors exercised above");
    puts("file_test: SKIP journal/findui integration and editor-dependent cases: those modules await their Windows beads");
    printf("file_test: Windows %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
