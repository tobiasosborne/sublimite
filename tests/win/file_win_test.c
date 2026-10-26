/* Deterministic file regressions. Shims live only in this translation unit. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <aclapi.h>
NTSYSAPI NTSTATUS NTAPI NtSetSecurityObject(HANDLE, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR);
#include "file/file_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int no_write_owner, encrypted, fail_security, unprotect;
    int created, checked;
    HANDLE writer;
    int delay_close, closed, callback_closed, moving;
    int secure_create, write_mode, write_calls, fail_streams, fail_set, overflow, fail_arm, trigger_arm;
    int owner_mismatch, label_set_fail, last_error;
    const char *wake_path;
    HANDLE wake_event;
    char move_from[OS_PATH_MAX], move_to[OS_PATH_MAX];
    const char *source;
} r2;
static __attribute__((unused)) HANDLE WINAPI r2_reopen(HANDLE h, DWORD access, DWORD share, DWORD flags)
{
    if (r2.no_write_owner && (access & WRITE_OWNER)) {
        SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
    }
    if (r2.moving) {
        r2.moving = 0;
        (void)os_rename(r2.move_from, r2.move_to, OS_RENAME_NOREPLACE); (void)os_mkdir(r2.move_from);
    }
    return ReOpenFile(h, access, share, flags);
}
static BOOL WINAPI r2_info(HANDLE h, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID out, DWORD n)
{
    if (r2.fail_streams && kind == FileStreamInfo) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    BOOL ok = GetFileInformationByHandleEx(h, kind, out, n);
    if (ok && r2.encrypted && kind == FileBasicInfo)
        ((FILE_BASIC_INFO *)out)->FileAttributes |= FILE_ATTRIBUTE_ENCRYPTED;
    return ok;
}
static DWORD WINAPI r2_get_security(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info,
                                  PSID *owner, PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (r2.fail_security && (info & LABEL_SECURITY_INFORMATION)) return ERROR_ACCESS_DENIED;
    os_stat observed, source;
    os_file borrowed = {.handle = h};
    if (r2.owner_mismatch && sd && r2.source && os_file_stat(&borrowed, &observed) == OS_OK &&
        os_stat_path(r2.source, 0, &source) == OS_OK && os_identity_equal(&observed.id, &source.id)) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;FA;;;OW)", SDDL_REVISION_1, sd, NULL))
            return GetLastError();
        return ERROR_SUCCESS;
    }
    return GetSecurityInfo(h, type, info, owner, group, dacl, sacl, sd);
}
static __attribute__((unused)) DWORD WINAPI r2_set_security(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info,
                                  PSID owner, PSID group, PACL dacl, PACL sacl)
{
    if (r2.unprotect && (info & UNPROTECTED_DACL_SECURITY_INFORMATION)) r2.unprotect++;
    return SetSecurityInfo(h, type, info, owner, group, dacl, sacl);
}
static __attribute__((unused)) DWORD WINAPI r2_final_name(HANDLE h, LPWSTR out, DWORD n, DWORD flags)
{
    DWORD got = GetFinalPathNameByHandleW(h, out, n, flags);
    if (r2.moving) {
        r2.moving = 0;
        (void)os_rename(r2.move_from, r2.move_to, OS_RENAME_NOREPLACE); (void)os_mkdir(r2.move_from);
    }
    return got;
}
static void r2_close(os_file *f)
{
    if (r2.delay_close && os_file_handle(f) == r2.writer) r2.closed = 1;
    os_file_close(f);
}
static os_err r2_stat(const os_file *f, os_stat *st)
{
    os_err e = os_file_stat(f, st);
    if (e == OS_OK && r2.delay_close && !r2.closed) st->mtime_ns = 1;
    return e;
}
static __attribute__((unused)) NTSTATUS NTAPI r2_nt_create(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io,
    PLARGE_INTEGER size, ULONG attrs, ULONG share, ULONG disp, ULONG opts, PVOID ea, ULONG n)
{
    if (r2.moving) {
        r2.moving = 0;
        (void)os_rename(r2.move_from, r2.move_to, OS_RENAME_NOREPLACE); (void)os_mkdir(r2.move_from);
    }
    if (disp == FILE_CREATE && oa->SecurityDescriptor) {
        SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision;
        PACL acl = NULL; BOOL present, def;
        BYTE world[SECURITY_MAX_SID_SIZE]; DWORD sid_size = sizeof world;
        ACCESS_MASK rights = FILE_READ_DATA | FILE_WRITE_DATA;
        TRUSTEE_W trustee;
        if (GetSecurityDescriptorDacl(oa->SecurityDescriptor, &present, &acl, &def) && acl &&
            CreateWellKnownSid(WinWorldSid, NULL, world, &sid_size)) {
            BuildTrusteeWithSidW(&trustee, world);
            (void)GetEffectiveRightsFromAclW(acl, &trustee, &rights);
        }
        if (GetSecurityDescriptorControl(oa->SecurityDescriptor, &control, &revision) &&
            (control & SE_DACL_PROTECTED) && !(rights & (FILE_READ_DATA | FILE_WRITE_DATA))) r2.secure_create++;
    }
    return NtCreateFile(h, access, oa, io, size, attrs, share, disp, opts, ea, n);
}
static os_err r2_pwrite(os_file *f, const void *p, size_t n, uint64_t offset, size_t *written)
{
    r2.write_calls++;
    if (r2.write_mode == 1 && r2.write_calls <= 3) { *written = 0; SetLastError(ERROR_OPERATION_ABORTED); return OS_E_INTR; }
    if (r2.write_mode == 2 && r2.write_calls == 2) { *written = 0; SetLastError(ERROR_DISK_FULL); return OS_E_NOSPC; }
    if (r2.write_mode && n > 3) n = 3;
    return os_file_pwrite_once(f, p, n, offset, written);
}
static __attribute__((unused)) NTSTATUS NTAPI r2_set_object(HANDLE h, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (r2.label_set_fail && (info & LABEL_SECURITY_INFORMATION)) return (NTSTATUS)0xc0000061u; /* STATUS_PRIVILEGE_NOT_HELD */
    if (r2.fail_set) return (NTSTATUS)0xc0000022u; /* STATUS_ACCESS_DENIED (ntstatus.h) */
    return NtSetSecurityObject(h, info, sd);
}
static BOOL WINAPI r2_result(HANDLE h, LPOVERLAPPED io, LPDWORD n, BOOL wait)
{
    BOOL ok = GetOverlappedResult(h, io, n, wait);
    if (ok && r2.overflow) { *n = 0; r2.overflow = 0; }
    return ok;
}
static BOOL WINAPI r2_read_directory(HANDLE h, LPVOID p, DWORD n, BOOL subtree, DWORD filter,
                                    LPDWORD got, LPOVERLAPPED io, LPOVERLAPPED_COMPLETION_ROUTINE done)
{
    if (r2.trigger_arm) {
        r2.trigger_arm = 0;
        os_file file = OS_FILE_INIT; size_t transferred = 0;
        if (os_file_open(&file, r2.wake_path, OS_OPEN_RDWR) == OS_OK)
            (void)os_file_pwrite_once(&file, "!", 1, 0, &transferred);
        os_file_close(&file);
        (void)WaitForSingleObject(r2.wake_event, 5000);
    }
    if (r2.fail_arm) { r2.fail_arm = 0; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ReadDirectoryChangesW(h, p, n, subtree, filter, got, io, done);
}
#define os_file_pwrite_once r2_pwrite
#define NtSetSecurityObject r2_set_object
#define GetOverlappedResult r2_result
#define ReadDirectoryChangesW r2_read_directory
#define NtCreateFile r2_nt_create
#define os_file_close r2_close
#define os_file_stat r2_stat
#define GetFinalPathNameByHandleW r2_final_name
#define SetSecurityInfo r2_set_security
#define ReOpenFile r2_reopen
#define GetFileInformationByHandleEx r2_info
#define GetSecurityInfo r2_get_security
#ifndef FILE_R2_COMMON_SOURCE
#define FILE_R2_COMMON_SOURCE "../../src/file/file.c"
#define FILE_R2_WIN_SOURCE "../../src/file/file_win.c"
#endif
#include FILE_R2_COMMON_SOURCE
#include FILE_R2_WIN_SOURCE
#undef os_file_pwrite_once
#undef NtSetSecurityObject
#undef GetOverlappedResult
#undef ReadDirectoryChangesW
#undef NtCreateFile
#undef os_file_close
#undef os_file_stat
#undef GetFinalPathNameByHandleW
#undef SetSecurityInfo
#undef ReOpenFile
#undef GetFileInformationByHandleEx
#undef GetSecurityInfo
#define main file_original_main
#include "../file_win_cases.inc"
#undef main

static void r2_wide(const char *s, WCHAR *out)
{ CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, OS_PATH_MAX)); }
static void r2_acl(const char *path, const char *sddl, SECURITY_INFORMATION info)
{
    PSECURITY_DESCRIPTOR sd = NULL; PACL dacl = NULL, sacl = NULL; BOOL present, def;
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &sd, NULL));
    CHECK(GetSecurityDescriptorDacl(sd, &present, &dacl, &def));
    CHECK(GetSecurityDescriptorSacl(sd, &present, &sacl, &def));
    WCHAR wide[OS_PATH_MAX]; r2_wide(path, wide);
    CHECK(SetNamedSecurityInfoW(wide, SE_FILE_OBJECT, info, NULL, NULL, dacl, sacl) == ERROR_SUCCESS);
    LocalFree(sd);
}
static void r2_temp_step(void *ctx, int step)
{
    (void)ctx;
    if (step != FILE_STEP_TEMP_CREATED) return;
    r2.created++;
    os_dir d = OS_DIR_INIT; os_dir_iter it; const char *name;
    CHECK(os_dir_open(&d, root) == OS_OK && os_dir_iter_open(&it, &d) == OS_OK);
    for (;;) {
        CHECK(os_dir_next(&it, &name) == OS_OK); if (!name) break;
        if (name[0] != '.') continue;
        os_file f = OS_FILE_INIT; CHECK(os_file_open_at(&f, &d, name, OS_OPEN_READ) == OS_OK);
        PSECURITY_DESCRIPTOR sd = NULL; PACL acl = NULL;
        CHECK(GetSecurityInfo(os_file_handle(&f), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              NULL, NULL, &acl, NULL, &sd) == ERROR_SUCCESS);
        BYTE sid[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof sid;
        CHECK(CreateWellKnownSid(WinWorldSid, NULL, sid, &size));
        TRUSTEE_W trustee; BuildTrusteeWithSidW(&trustee, sid); ACCESS_MASK rights = 0;
        CHECK(GetEffectiveRightsFromAclW(acl, &trustee, &rights) == ERROR_SUCCESS);
        CHECK(!(rights & (FILE_READ_DATA | FILE_WRITE_DATA)));
        HANDLE token = NULL, restricted = NULL;
        CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &token));
        SID_AND_ATTRIBUTES restriction = {sid, 0};
        CHECK(CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 1, &restriction, &restricted));
        WCHAR native[OS_PATH_MAX]; CHECK(GetFinalPathNameByHandleW(os_file_handle(&f), native, OS_PATH_MAX, FILE_NAME_NORMALIZED));
        CHECK(ImpersonateLoggedOnUser(restricted));
        HANDLE intrusion = CreateFileW(native, FILE_READ_DATA | FILE_WRITE_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        DWORD denied = GetLastError(); CHECK(RevertToSelf());
        CHECK(intrusion == INVALID_HANDLE_VALUE && denied == ERROR_ACCESS_DENIED);
        if (intrusion != INVALID_HANDLE_VALUE) CloseHandle(intrusion);
        CloseHandle(restricted); CloseHandle(token);
        r2.checked++; LocalFree(sd); os_file_close(&f);
    }
    os_dir_iter_close(&it); os_dir_close(&d);
}
static int r2_save(const char *path, void (*step)(void *, int))
{
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(t && piece_init_copy(t, (const uint8_t *)"replacement", 11) == PIECE_OK);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    file_save_args args = {.path = path, .snap = s, .step = step};
    r2.source = path;
    int rc = file_save_write(&args); r2.last_error = args.err_no;
    piece_snapshot_release(s); piece_destroy(t); return rc;
}
static void r2_security(void)
{
    char path[OS_PATH_MAX]; win_path(path, "security");
    /* Broad parent, restricted source; default inherited temporary is unsafe. */
    r2_acl(root, "D:(A;OICI;FA;;;WD)", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION);
    win_write(path, (const uint8_t *)"secret", 6);
    r2_acl(path, "D:P(A;;FA;;;OW)", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION);
    CHECK(r2_save(path, r2_temp_step) == FILE_OK);
    CHECK(r2.created == 1 && r2.checked == 1 && r2.secure_create == 1);
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_efs(void)
{
    char path[OS_PATH_MAX]; win_path(path, "efs"); win_write(path, (const uint8_t *)"secret", 6);
    WCHAR wide[OS_PATH_MAX]; r2_wide(path, wide);
    BOOL native = EncryptFileW(wide);
    if (!native) printf("file_win_test: EFS native fixture unavailable (%lu); deterministic attribute fixture used\n", GetLastError());
    r2.encrypted = 1;
    CHECK(r2_save(path, r2_temp_step) == FILE_ERR_IO && r2.last_error == ERROR_FILE_ENCRYPTED);
    CHECK(r2.created == 0); win_read_equals(path, (const uint8_t *)"secret", 6);
    if (native) {
        char exe[OS_PATH_MAX]; CHECK(os_proc_self_path(exe, sizeof exe) == OS_OK);
        const char *args[] = {exe, "--child-save", path, "1", NULL}; os_proc child; os_proc_status status;
        CHECK(os_proc_spawn(&child, args) == OS_OK);
        CHECK(os_proc_wait(&child, INT64_C(120) * INT64_C(1000000000), &status) == OS_OK);
        CHECK(status.how == OS_PROC_EXITED && status.code == 2); /* refusal before TEMP_CREATED */
        CHECK(GetFileAttributesW(wide) & FILE_ATTRIBUTE_ENCRYPTED);
        win_read_equals(path, (const uint8_t *)"secret", 6); file_os_no_temps(root);
    }
    r2.encrypted = 0;
    if (native) CHECK(DecryptFileW(wide, 0));
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_streams(void)
{
    char path[OS_PATH_MAX], stream[OS_PATH_MAX]; win_path(path, "streams");
    win_write(path, (const uint8_t *)"original", 8);
    CHECK(snprintf(stream, sizeof stream, "%s:first", path) > 0); win_write(stream, (const uint8_t *)"one", 3);
    CHECK(snprintf(stream, sizeof stream, "%s:Zone.Identifier", path) > 0); win_write(stream, (const uint8_t *)"two", 3);
    CHECK(r2_save(path, r2_temp_step) == FILE_ERR_IO && r2.last_error == ERROR_NOT_SUPPORTED);
    CHECK(r2.created == 0); win_read_equals(path, (const uint8_t *)"original", 8);
    win_read_equals(stream, (const uint8_t *)"two", 3);
    CHECK(snprintf(stream, sizeof stream, "%s:first", path) > 0); win_read_equals(stream, (const uint8_t *)"one", 3);
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_descriptor(void)
{
    char first[OS_PATH_MAX], second[OS_PATH_MAX], old[OS_PATH_MAX], path[OS_PATH_MAX];
    win_path(first, "acl-first"); win_path(second, "acl-second");
    CHECK(os_mkdir(first) == OS_OK && os_mkdir(second) == OS_OK);
    r2_acl(first, "D:P(A;OICI;FA;;;OW)(A;OICI;FR;;;WD)", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION);
    r2_acl(second, "D:P(A;OICI;FA;;;OW)(A;OICI;FW;;;WD)", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION);
    CHECK(snprintf(old, sizeof old, "%s/document", first) > 0 && snprintf(path, sizeof path, "%s/document", second) > 0);
    win_write(old, (const uint8_t *)"original", 8);
    /* A same-volume move retains the original parent's inherited ACEs. */
    CHECK(os_rename(old, path, OS_RENAME_NOREPLACE) == OS_OK);
    os_file f = OS_FILE_INIT; CHECK(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    PSECURITY_DESCRIPTOR before = NULL; PACL acl = NULL;
    CHECK(GetSecurityInfo(os_file_handle(&f), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &acl, NULL, &before) == ERROR_SUCCESS);
    os_file_close(&f);
    r2.unprotect = 1; CHECK(r2_save(path, NULL) == FILE_OK && r2.unprotect == 1); r2.unprotect = 0;
    CHECK(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    PSECURITY_DESCRIPTOR after = NULL; PACL next = NULL;
    CHECK(GetSecurityInfo(os_file_handle(&f), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &next, NULL, &after) == ERROR_SUCCESS);
    CHECK(acl && next && acl->AclSize == next->AclSize && !memcmp(acl, next, acl->AclSize));
    SECURITY_DESCRIPTOR_CONTROL a = 0, b = 0; DWORD rev;
    CHECK(GetSecurityDescriptorControl(before, &a, &rev) && GetSecurityDescriptorControl(after, &b, &rev));
    CHECK(!(a & SE_DACL_PROTECTED) && (a & SE_DACL_PROTECTED) == (b & SE_DACL_PROTECTED));
    LocalFree(before); LocalFree(after); os_file_close(&f); CHECK(os_unlink(path) == OS_OK);
    CHECK(os_rmdir(first) == OS_OK && os_rmdir(second) == OS_OK);
}
static void r2_rights(void)
{
    char path[OS_PATH_MAX]; win_path(path, "rights"); win_write(path, (const uint8_t *)"original", 8);
    HANDLE token = NULL; CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD length = 0; (void)GetTokenInformation(token, TokenUser, NULL, 0, &length);
    TOKEN_USER *user = malloc(length); CHECK(user && GetTokenInformation(token, TokenUser, user, length, &length));
    EXPLICIT_ACCESS_W entry = {0}; entry.grfAccessPermissions = 0x1301bfu; /* FILE_GENERIC_READ/WRITE/EXECUTE + DELETE: Modify */
    entry.grfAccessMode = SET_ACCESS; entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    BuildTrusteeWithSidW(&entry.Trustee, user->User.Sid);
    PACL modify = NULL; CHECK(SetEntriesInAclW(1, &entry, NULL, &modify) == ERROR_SUCCESS);
    WCHAR wide[OS_PATH_MAX]; r2_wide(path, wide);
    CHECK(SetNamedSecurityInfoW(wide, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              NULL, NULL, modify, NULL) == ERROR_SUCCESS);
    os_file probe = OS_FILE_INIT; CHECK(os_file_open(&probe, path, OS_OPEN_READ) == OS_OK);
    HANDLE owner = ReOpenFile(os_file_handle(&probe), WRITE_OWNER, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    CHECK(owner == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
    if (owner != INVALID_HANDLE_VALUE) CloseHandle(owner);
    os_file_close(&probe); LocalFree(modify); free(user); CloseHandle(token);
    r2.no_write_owner = 1; CHECK(r2_save(path, NULL) == FILE_OK); r2.no_write_owner = 0;
    win_read_equals(path, (const uint8_t *)"replacement", 11); CHECK(os_unlink(path) == OS_OK);
}
static void r2_label(void)
{
    char path[OS_PATH_MAX]; win_path(path, "label"); win_write(path, (const uint8_t *)"original", 8);
    r2_acl(path, "S:(ML;;NW;;;LW)", LABEL_SECURITY_INFORMATION);
    CHECK(r2_save(path, NULL) == FILE_OK);
    os_file f = OS_FILE_INIT; CHECK(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    PACL label = NULL; PSECURITY_DESCRIPTOR sd = NULL;
    CHECK(GetSecurityInfo(os_file_handle(&f), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &label, &sd) == ERROR_SUCCESS);
    CHECK(label && label->AceCount == 1);
    if (label && label->AceCount) {
        SYSTEM_MANDATORY_LABEL_ACE *ace = NULL; CHECK(GetAce(label, 0, (void **)&ace));
        CHECK(*GetSidSubAuthority(&ace->SidStart, 0) == SECURITY_MANDATORY_LOW_RID);
    }
    LocalFree(sd); os_file_close(&f);
    r2.label_set_fail = 1; CHECK(r2_save(path, r2_temp_step) == FILE_ERR_IO); CHECK(!r2.created); r2.label_set_fail = 0;
    r2.fail_security = 1; CHECK(r2_save(path, r2_temp_step) == FILE_ERR_IO); CHECK(!r2.created); r2.fail_security = 0;
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_replaced(void *ctx)
{ (void)ctx; r2.callback_closed = r2.closed; }
static void r2_identity(void)
{
    char path[OS_PATH_MAX]; win_path(path, "identity"); win_write(path, (const uint8_t *)"old", 3);
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(piece_init_copy(t, (const uint8_t *)"next", 4) == PIECE_OK);
    piece_snapshot *snap = piece_snapshot_take(t); file_id id = {0};
    save_transaction tx = {.args = {.path = path, .snap = snap, .out_id = &id, .replaced = r2_replaced}};
    CHECK(transaction_prepare(&tx) == FILE_OK);
    r2.writer = os_file_handle(&tx.fd); r2.delay_close = 1;
    CHECK(transaction_commit(&tx) == FILE_OK && r2.callback_closed);
    r2.delay_close = 0;
    os_stat st; CHECK(os_stat_path(path, 0, &st) == OS_OK);
    file_id actual; file_id_from_stat(&actual, &st); CHECK(!file_id_diff(&id, &actual));
    piece_snapshot_release(snap); piece_destroy(t); CHECK(os_unlink(path) == OS_OK);
}
static void r2_wake(void)
{
    char a[OS_PATH_MAX], b[OS_PATH_MAX], path[OS_PATH_MAX]; win_path(a, "a"); win_path(b, "b");
    CHECK(os_mkdir(a) == OS_OK && os_mkdir(b) == OS_OK);
    char an[OS_PATH_MAX], bn[OS_PATH_MAX];
    CHECK(snprintf(an, sizeof an, "%s/config", a) > 0 && snprintf(bn, sizeof bn, "%s/config", b) > 0);
    file_path_watch aw = {0}, bw = {0}; CHECK(file_watch_path_start(&aw, an) == 0);
    CHECK(snprintf(path, sizeof path, "%s/config", a) > 0); win_write(path, (const uint8_t *)"A", 1);
    file_parent_watch *ap = file_watches.parents[(size_t)aw.wd];
    CHECK(WaitForSingleObject(ap->io.hEvent, 5000) == WAIT_OBJECT_0);
    CHECK(file_watch_path_start(&bw, bn) == 0);
    file_parent_watch *bp = file_watches.parents[(size_t)bw.wd];
    CHECK(ap->io.hEvent != bp->io.hEvent);
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0);
    file_watch_dispatch(); CHECK(file_watch_path_take(&aw));
    win_write(bn, (const uint8_t *)"B", 1);
    CHECK(WaitForSingleObject(bp->io.hEvent, 5000) == WAIT_OBJECT_0);
    r2.wake_path = an; r2.wake_event = ap->io.hEvent; r2.trigger_arm = 1;
    /* A completes after its scan, immediately before B's native rearm. */
    file_watch_dispatch(); CHECK(file_watch_path_take(&bw));
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0);
    file_watch_dispatch(); CHECK(file_watch_path_take(&aw));
    file_watch_path_stop(&aw); file_watch_path_stop(&bw);
    CHECK(os_unlink(path) == OS_OK && os_unlink(bn) == OS_OK && os_rmdir(a) == OS_OK && os_rmdir(b) == OS_OK);
}
static bool r2_has_timer(void)
{
#if defined(FILE_R2_ROUND1)
    return false;
#else
    return file_watches.timer != NULL;
#endif
}
static void r2_parent(void)
{
    char path[OS_PATH_MAX], next[OS_PATH_MAX]; win_path(r2.move_from, "parent"); win_path(r2.move_to, "moved");
    CHECK(os_mkdir(r2.move_from) == OS_OK);
    CHECK(snprintf(path, sizeof path, "%s/document", r2.move_from) > 0);
    CHECK(snprintf(next, sizeof next, "%s/document", r2.move_to) > 0);
    win_write(path, (const uint8_t *)"old", 3); file *f = win_open(path, UINT64_MAX);
    file_path_watch named = {0}; CHECK(file_watch_start(f) >= 0 && file_watch_path_start(&named, path) == 0);
    /* Windows refuses moving a directory with an open descendant. COPY has
     * private bytes; release that acquisition handle for this parent fixture. */
    os_file_close(&f->fd); f->fd = (os_file)OS_FILE_INIT;
    CHECK(os_rename(r2.move_from, r2.move_to, OS_RENAME_NOREPLACE) == OS_OK);
    ResetEvent(file_watch_handle());
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0);
    file_watch_dispatch(); CHECK(named.wd < 0 && f->iwd < 0 && file_watch_path_take(&named));
    CHECK(r2_has_timer());
    CHECK(os_mkdir(r2.move_from) == OS_OK);
    win_write(path, (const uint8_t *)"new", 3);
    ResetEvent(file_watch_handle());
    CHECK(WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0);
    file_watch_dispatch(); CHECK(file_watch_path_take(&named));
    os_stat current; CHECK(os_stat_path(r2.move_from, 0, &current) == OS_OK);
    CHECK(named.wd >= 0 && os_identity_equal(&file_watches.parents[(size_t)named.wd]->identity, &current.id));
    CHECK(f->iwd == named.wd);
    file_close(f); file_watch_path_stop(&named);
    CHECK(os_unlink(path) == OS_OK && os_unlink(next) == OS_OK);
    CHECK(os_rmdir(r2.move_from) == OS_OK && os_rmdir(r2.move_to) == OS_OK);
}
static void r2_cache(void)
{
    win_path(r2.move_from, "cache"); win_path(r2.move_to, "cache-moved"); CHECK(os_mkdir(r2.move_from) == OS_OK);
    os_stat old; CHECK(os_stat_path(r2.move_from, 0, &old) == OS_OK);
    r2.moving = 1; int wd = watch_parent(r2.move_from); CHECK(wd >= 0);
    if (wd >= 0) {
        file_parent_watch *p = file_watches.parents[(size_t)wd];
        os_file borrowed = {.handle = p->handle}; os_stat actual; CHECK(os_file_stat(&borrowed, &actual) == OS_OK);
        CHECK(os_identity_equal(&p->identity, &actual.id) && os_identity_equal(&old.id, &actual.id));
        int second = watch_parent(r2.move_from); CHECK(second >= 0 && second != wd);
        if (second >= 0) watch_release(NULL, second);
        watch_release(NULL, wd);
    }
    CHECK(os_rmdir(r2.move_from) == OS_OK && os_rmdir(r2.move_to) == OS_OK);
}
static void r2_unicode(void)
{
    const char *units[] = {"ä", "界", "😀", "界"};
    /* Boundary at the old 100-byte truncation and NTFS's 255 UTF-16 units. */
    const size_t counts[] = {51, 34, 26, 255};
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        char name[OS_NAME_MAX + 1] = ""; size_t n = 0;
        if (i == 0 || i == 2) name[n++] = 'a';
        for (size_t j = 0; j < counts[i]; j++) { size_t len = strlen(units[i]); memcpy(name + n, units[i], len); n += len; }
        name[n] = 0; char path[OS_PATH_MAX]; win_path(path, name);
        win_write(path, (const uint8_t *)"old", 3); CHECK(r2_save(path, NULL) == FILE_OK);
        win_read_equals(path, (const uint8_t *)"replacement", 11); CHECK(os_unlink(path) == OS_OK);
    }
}
static void r2_failures(void)
{
    char path[OS_PATH_MAX]; win_path(path, "failures");
    for (int mode = 1; mode <= 2; mode++) {
        win_write(path, (const uint8_t *)"original", 8); r2.write_calls = 0; r2.write_mode = mode;
        CHECK(r2_save(path, NULL) == (mode == 1 ? FILE_OK : FILE_ERR_IO)); r2.write_mode = 0;
        file_os_equals(path, (const uint8_t *)(mode == 1 ? "replacement" : "original"), mode == 1 ? 11 : 8);
        file_os_no_temps(root);
    }
    r2.owner_mismatch = 1; CHECK(r2_save(path, NULL) == FILE_ERR_IO); r2.owner_mismatch = 0;
    file_os_equals(path, (const uint8_t *)"original", 8); file_os_no_temps(root);
    r2.fail_streams = 1; CHECK(r2_save(path, NULL) == FILE_ERR_IO); r2.fail_streams = 0;
    file_os_equals(path, (const uint8_t *)"original", 8); file_os_no_temps(root);
    r2.fail_set = 1; CHECK(r2_save(path, NULL) == FILE_ERR_IO); r2.fail_set = 0;
    file_os_equals(path, (const uint8_t *)"original", 8); file_os_no_temps(root);
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_watch_errors(void)
{
    char path[OS_PATH_MAX], noise[OS_PATH_MAX]; win_path(path, "watched"); win_path(noise, "noise");
    file_path_watch member = {0}; CHECK(file_watch_path_start(&member, path) == 0);
    file_parent_watch *p = file_watches.parents[(size_t)member.wd];
    win_write(noise, (const uint8_t *)"noise", 5);
    CHECK(WaitForSingleObject(p->io.hEvent, 5000) == WAIT_OBJECT_0);
    r2.overflow = 1; file_watch_dispatch(); CHECK(file_watch_path_take(&member));
    win_write(path, (const uint8_t *)"target", 6);
    CHECK(WaitForSingleObject(p->io.hEvent, 5000) == WAIT_OBJECT_0);
    r2.fail_arm = 1; file_watch_dispatch(); CHECK(member.wd < 0 && file_watch_path_take(&member));
    file_watch_path_stop(&member); CHECK(!r2_has_timer());
    CHECK(file_watch_path_start(&member, path) == 0 && member.wd >= 0);
    file_watch_path_stop(&member); CHECK(os_unlink(path) == OS_OK && os_unlink(noise) == OS_OK);
}
static void r2_cleanup(const char *directory)
{
    os_dir dir = OS_DIR_INIT; os_dir_iter it; const char *name;
    CHECK(os_dir_open(&dir, directory) == OS_OK && os_dir_iter_open(&it, &dir) == OS_OK);
    for (;;) {
        CHECK(os_dir_next(&it, &name) == OS_OK); if (!name) break;
        os_stat st; CHECK(os_stat_at(&dir, name, OS_STAT_NOFOLLOW, &st) == OS_OK);
        if (st.kind == OS_KIND_DIR) {
            char child[OS_PATH_MAX]; CHECK(snprintf(child, sizeof child, "%s/%s", directory, name) > 0); r2_cleanup(child);
        } else CHECK(os_unlink_at(&dir, name) == OS_OK);
    }
    os_dir_iter_close(&it); os_dir_close(&dir); CHECK(os_rmdir(directory) == OS_OK);
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        const char *cases[] = {"security", "efs", "streams", "descriptor", "rights", "label", "identity", "wake", "parent", "cache", "unicode", "failures", "watch-errors"};
        char exe[OS_PATH_MAX]; CHECK(os_proc_self_path(exe, sizeof exe) == OS_OK);
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            const char *args[] = {exe, cases[i], NULL}; os_proc child; os_proc_status status;
            CHECK(os_proc_spawn(&child, args) == OS_OK);
            CHECK(os_proc_wait(&child, INT64_C(120) * INT64_C(1000000000), &status) == OS_OK);
            CHECK(status.how == OS_PROC_EXITED && status.code == 0);
        }
        printf("file_win_test: %zu cases, %s\n", sizeof cases / sizeof cases[0], fails ? "FAILED" : "ok"); return fails ? 1 : 0;
    }
    if (argc != 2) return file_original_main(argc, argv);
    char temp[OS_PATH_MAX]; CHECK(os_temp_dir(temp, sizeof temp) == OS_OK);
    CHECK(snprintf(root, sizeof root, "%s/sublimite-test-file-r2-%llu-%llu", temp,
          (unsigned long long)os_proc_id(), (unsigned long long)os_wall_ns()) > 0);
    CHECK(os_mkdir(root) == OS_OK); trace_init(); CHECK(work_pool_init(&pool, 1, 1) == 0);
    if (!strcmp(argv[1], "security")) r2_security();
    else if (!strcmp(argv[1], "efs")) r2_efs();
    else if (!strcmp(argv[1], "streams")) r2_streams();
    else if (!strcmp(argv[1], "descriptor")) r2_descriptor();
    else if (!strcmp(argv[1], "rights")) r2_rights();
    else if (!strcmp(argv[1], "label")) r2_label();
    else if (!strcmp(argv[1], "identity")) r2_identity();
    else if (!strcmp(argv[1], "wake")) r2_wake();
    else if (!strcmp(argv[1], "parent")) r2_parent();
    else if (!strcmp(argv[1], "cache")) r2_cache();
    else if (!strcmp(argv[1], "unicode")) r2_unicode();
    else if (!strcmp(argv[1], "shared")) file_os_round2_cases(&pool, root);
    else if (!strcmp(argv[1], "failures")) r2_failures();
    else if (!strcmp(argv[1], "watch-errors")) r2_watch_errors();
    else CHECK(0);
    work_pool_shutdown(&pool);
    if (os_rmdir(root) != OS_OK) { CHECK(0); r2_cleanup(root); }
    printf("file_win_test: %s: %s\n", argv[1], fails ? "FAILED" : "ok"); return fails ? 1 : 0;
}
#endif
