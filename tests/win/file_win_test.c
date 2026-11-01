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
    int stream_track, stream_ops, stream_fail_at, stream_cancel_at, stream_cancelled;
    int stream_mode, stream_reads, stream_writes, stream_malformed;
    NTSTATUS stream_status;
    const WCHAR *stream_name;
    ULONG stream_name_bytes;
    int stream_opens;
    HANDLE stream_source, stream_target;
} r2;
static bool r2_stream_hit(void)
{
    if (!r2.stream_track) return false;
    r2.stream_ops++;
    if (r2.stream_ops == r2.stream_cancel_at) r2.stream_cancelled = 1;
    if (r2.stream_ops != r2.stream_fail_at) return false;
    SetLastError(ERROR_ACCESS_DENIED); return true;
}
static NTSTATUS NTAPI r2_query(HANDLE h, PIO_STATUS_BLOCK io, PVOID out, ULONG n, FILE_INFORMATION_CLASS kind)
{
    if (kind == FileStreamInformation && r2.stream_status) return r2.stream_status;
    if (kind == FileStreamInformation && (r2.fail_streams || r2_stream_hit())) return (NTSTATUS)0xc0000022u;
    NTSTATUS status;
    if (kind == FileStreamInformation && r2.stream_name) {
        size_t length = offsetof(FILE_STREAM_INFO, StreamName) + r2.stream_name_bytes;
        if (n < length) return (NTSTATUS)0x80000005u; /* STATUS_BUFFER_OVERFLOW */
        memset(out, 0, n);
        FILE_STREAM_INFO *entry = out;
        entry->StreamNameLength = r2.stream_name_bytes;
        entry->StreamSize.QuadPart = sizeof "original" - 1;
        memcpy(entry->StreamName, r2.stream_name, r2.stream_name_bytes);
        io->Information = length; io->Status = status = 0;
    } else status = NtQueryInformationFile(h, io, out, n, kind);
    if (status >= 0 && kind == FileStreamInformation && r2.stream_malformed) {
        FILE_STREAM_INFO *entry = out;
        switch (r2.stream_malformed) {
        case 1: entry->StreamNameLength = n; break;
        case 2: entry->NextEntryOffset = 1; break;
        case 3: io->Information = offsetof(FILE_STREAM_INFO, StreamName) - 1; break;
        case 4: entry->StreamNameLength |= 1; break;
        case 5: entry->StreamSize.QuadPart = -1; break;
        case 6: io->Information = (ULONG_PTR)n + 1; break;
        case 7: entry->StreamName[0] = L'\\'; break;
        }
    }
    return status;
}
static void *r2_malloc(size_t n) { return r2_stream_hit() ? NULL : malloc(n); }
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
    if (os_file_handle(f) == r2.stream_source) r2.stream_source = NULL;
    if (os_file_handle(f) == r2.stream_target) r2.stream_target = NULL;
    os_file_close(f);
}
static os_err r2_stat(const os_file *f, os_stat *st)
{
    if (os_file_handle(f) == r2.stream_source && r2_stream_hit()) return OS_E_ACCESS;
    os_err e = os_file_stat(f, st);
    if (e == OS_OK && r2.delay_close && !r2.closed) st->mtime_ns = 1;
    return e;
}
static __attribute__((unused)) NTSTATUS NTAPI r2_nt_create(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io,
    PLARGE_INTEGER size, ULONG attrs, ULONG share, ULONG disp, ULONG opts, PVOID ea, ULONG n)
{
    bool stream = oa->ObjectName->Length && oa->ObjectName->Buffer[0] == L':';
    /* Every stream_copy starts with a native open, including an empty name. */
    if (stream || (r2.stream_name && !oa->ObjectName->Length)) r2.stream_opens++;
    if (stream && r2_stream_hit()) return (NTSTATUS)0xc0000022u;
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
    NTSTATUS status = NtCreateFile(h, access, oa, io, size, attrs, share, disp, opts, ea, n);
    if (status >= 0 && stream) {
        if (disp == FILE_CREATE) r2.stream_target = *h;
        else r2.stream_source = *h;
    }
    return status;
}
static os_err r2_pread(os_file *f, void *p, size_t n, uint64_t offset, size_t *got)
{
    if (os_file_handle(f) == r2.stream_source) {
        r2.stream_reads++;
        if (r2_stream_hit()) return OS_E_ACCESS;
        if (r2.stream_mode == 1 && n > 4093u) n = 4093u; /* prime-sized short transfers */
        if (r2.stream_mode == 3) { *got = 0; return OS_OK; }
        if (r2.stream_mode == 4 && r2.stream_reads == 1) return OS_E_INTR;
        if ((r2.stream_mode == 5 || r2.stream_mode == 8) && r2.stream_reads == 2) {
            os_file other = OS_FILE_INIT; size_t done = 0;
            if (os_file_open(&other, r2.source, OS_OPEN_RDWR) == OS_OK)
                (void)os_file_pwrite_once(&other, "!", 1, 0, &done);
            os_file_close(&other);
            if (r2.stream_mode == 8) { SetLastError(ERROR_READ_FAULT); return OS_E_IO; }
        }
        if (r2.stream_mode == 6 && offset) ExitProcess(77); /* death during a large ADS */
        if (r2.stream_mode == 7 && r2.stream_reads == 2)
            (void)os_rename(r2.source, r2.move_to, OS_RENAME_REPLACE);
    }
    return os_file_pread(f, p, n, offset, got);
}
static os_err r2_sync(os_file *f)
{
    if (os_file_handle(f) == r2.stream_target && r2_stream_hit()) return OS_E_ACCESS;
    return os_file_sync(f);
}
static os_err r2_pwrite(os_file *f, const void *p, size_t n, uint64_t offset, size_t *written)
{
    if (os_file_handle(f) == r2.stream_target) {
        r2.stream_writes++;
        if (r2_stream_hit()) return OS_E_ACCESS;
        if (r2.stream_mode == 1 && n > 4091u) n = 4091u;
        if (r2.stream_mode == 2) { *written = 0; return OS_OK; }
        if (r2.stream_mode == 4 && r2.stream_writes == 1) return OS_E_INTR;
    }
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
#define os_file_pread r2_pread
#define os_file_sync r2_sync
#define NtQueryInformationFile r2_query
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
#define malloc r2_malloc
#include FILE_R2_WIN_SOURCE
#undef malloc
#undef os_file_pwrite_once
#undef os_file_pread
#undef os_file_sync
#undef NtQueryInformationFile
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
static int r2_stream_stop(void *ctx) { (void)ctx; return r2.stream_cancelled; }
static int r2_save(const char *path, void (*step)(void *, int))
{
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(t && piece_init_copy(t, (const uint8_t *)"replacement", 11) == PIECE_OK);
    piece_snapshot *s = piece_snapshot_take(t); CHECK(s);
    file_save_args args = {.path = path, .snap = s, .step = step, .stop = r2_stream_stop};
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
    char path[OS_PATH_MAX]; win_path(path, "streams");
    win_write(path, (const uint8_t *)"original", 8);
    win_streams(path, true);
    os_file attributes = OS_FILE_INIT;
    CHECK(os_file_open(&attributes, path, OS_OPEN_RDWR) == OS_OK);
    CHECK(os_file_set_mode(&attributes, 0444) == OS_OK); os_file_close(&attributes);
    CHECK(r2_save(path, NULL) == FILE_OK);
    win_read_equals(path, (const uint8_t *)"replacement", 11);
    win_streams(path, false); file_os_no_temps(root);
    os_stat st; CHECK(os_stat_path(path, 0, &st) == OS_OK && !(st.perm & 0200u));
    CHECK(os_unlink(path) == OS_OK);
}
static void r2_stream_unsupported(void)
{
    char path[OS_PATH_MAX], stream[OS_PATH_MAX]; win_path(path, "stream-unsupported");
    CHECK(snprintf(stream, sizeof stream, "%s:retained", path) > 0);
    const struct { NTSTATUS status; const char *name; bool unsupported; } cases[] = {
        {(NTSTATUS)0xc000000du, "STATUS_INVALID_PARAMETER", true},
        {(NTSTATUS)0xc0000002u, "STATUS_NOT_IMPLEMENTED", true},
        {(NTSTATUS)0xc0000003u, "STATUS_INVALID_INFO_CLASS", true},
        {(NTSTATUS)0xc0000022u, "STATUS_ACCESS_DENIED", false},
        {(NTSTATUS)0xc0000185u, "STATUS_IO_DEVICE_ERROR", false},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        win_write(path, (const uint8_t *)"original", 8);
        if (!cases[i].unsupported) win_write(stream, (const uint8_t *)"\0retained\xff", sizeof "\0retained\xff" - 1);
        r2.stream_status = cases[i].status;
        int rc = r2_save(path, NULL); r2.stream_status = 0;
        printf("file_win_test: %s: rc=%d native=%d\n", cases[i].name, rc, r2.last_error);
        CHECK(rc == (cases[i].unsupported ? FILE_OK : FILE_ERR_IO));
        if (!cases[i].unsupported) CHECK(r2.last_error == (int)RtlNtStatusToDosError(cases[i].status));
        win_read_equals(path, (const uint8_t *)(cases[i].unsupported ? "replacement" : "original"),
                        cases[i].unsupported ? 11 : 8);
        if (!cases[i].unsupported) win_read_equals(stream, (const uint8_t *)"\0retained\xff", sizeof "\0retained\xff" - 1);
        file_os_no_temps(root); CHECK(os_unlink(path) == OS_OK);
    }
}
static void r2_stream_default(void)
{
    char path[OS_PATH_MAX]; win_path(path, "stream-default");
    const struct { const WCHAR *name; ULONG bytes; } cases[] = {
        {L"", 0},
        {L"::$data", sizeof L"::$data" - sizeof(WCHAR)},
        {L"::$DATA", sizeof L"::$DATA" - sizeof(WCHAR)},
        {L"::$DaTa", sizeof L"::$DaTa" - sizeof(WCHAR)},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        win_write(path, (const uint8_t *)"original", 8);
        r2.stream_name = cases[i].name; r2.stream_name_bytes = cases[i].bytes; r2.stream_opens = 0;
        CHECK(r2_save(path, NULL) == FILE_OK);
        CHECK(r2.stream_opens == 0);
        win_read_equals(path, (const uint8_t *)"replacement", 11); file_os_no_temps(root);
        /* Default records still require valid offsets and a complete header. */
        for (int malformed = 2; malformed <= 3; malformed++) {
            win_write(path, (const uint8_t *)"original", 8); r2.stream_malformed = malformed;
            CHECK(r2_save(path, NULL) == FILE_ERR_IO && r2.last_error == ERROR_INVALID_DATA);
            CHECK(r2.stream_opens == 0);
            win_read_equals(path, (const uint8_t *)"original", 8); file_os_no_temps(root);
        }
        r2.stream_malformed = 0; r2.stream_name = NULL;
        CHECK(os_unlink(path) == OS_OK);
    }
}
static void r2_stream_failures(void)
{
    char path[OS_PATH_MAX]; win_path(path, "stream-failures");
    win_write(path, (const uint8_t *)"original", 8); win_streams(path, true);
    r2.stream_track = 1;
    CHECK(r2_save(path, NULL) == FILE_OK);
    int steps = r2.stream_ops;
    CHECK(steps > 0);
    for (int step = 1; step <= steps; step++) {
        win_write(path, (const uint8_t *)"original", 8); win_streams(path, true);
        r2.stream_ops = 0; r2.stream_fail_at = step;
        int rc = r2_save(path, NULL);
        if (rc != FILE_ERR_IO) fprintf(stderr, "stream failure step=%d/%d rc=%d native=%d\n", step, steps, rc, r2.last_error);
        CHECK(rc == FILE_ERR_IO && r2.last_error != 0);
        CHECK(!r2.stream_source && !r2.stream_target);
        win_read_equals(path, (const uint8_t *)"original", 8); win_streams(path, false); file_os_no_temps(root);
    }
    r2.stream_fail_at = 0;
    for (int step = 1; step <= steps; step++) {
        r2.stream_ops = 0; r2.stream_cancel_at = step; r2.stream_cancelled = 0;
        CHECK(r2_save(path, NULL) == FILE_ERR_CANCELLED);
        CHECK(!r2.stream_source && !r2.stream_target);
        win_read_equals(path, (const uint8_t *)"original", 8); win_streams(path, false); file_os_no_temps(root);
    }
    r2.stream_cancel_at = 0; r2.stream_cancelled = 0;
    for (int mode = 1; mode <= 5; mode++) {
        r2.stream_mode = mode; r2.stream_reads = 0; r2.stream_writes = 0;
        int expected = mode == 2 ? FILE_ERR_IO : mode == 3 || mode == 5 ? FILE_ERR_CHANGED : FILE_OK;
        CHECK(r2_save(path, NULL) == expected);
        win_read_equals(path, (const uint8_t *)(mode == 5 ? "!riginal" : expected == FILE_OK ? "replacement" : "original"),
                        mode == 5 || expected != FILE_OK ? 8 : 11);
        win_streams(path, false); file_os_no_temps(root);
        win_write(path, (const uint8_t *)"original", 8); win_streams(path, true);
    }
    r2.stream_mode = 0;
    r2.stream_reads = 0; r2.stream_mode = 8;
    CHECK(r2_save(path, NULL) == FILE_ERR_CHANGED);
    win_read_equals(path, (const uint8_t *)"!riginal", 8); win_streams(path, false); file_os_no_temps(root);
    win_write(path, (const uint8_t *)"original", 8); win_streams(path, true); r2.stream_mode = 0;
    for (int malformed = 1; malformed <= 7; malformed++) {
        r2.stream_malformed = malformed;
        CHECK(r2_save(path, NULL) == FILE_ERR_IO && r2.last_error == ERROR_INVALID_DATA);
        win_read_equals(path, (const uint8_t *)"original", 8); win_streams(path, false); file_os_no_temps(root);
    }
    r2.stream_malformed = 0;
    /* A rename of the target during a read still opens streams on the pinned
     * original, then refuses at the existing pathname identity check. */
    win_path(r2.move_to, "stream-moved"); r2.stream_mode = 7; r2.stream_reads = 0;
    CHECK(r2_save(path, NULL) == FILE_ERR_CHANGED);
    win_read_equals(r2.move_to, (const uint8_t *)"original", 8); win_streams(r2.move_to, false);
    CHECK(os_unlink(r2.move_to) == OS_OK); file_os_no_temps(root);
    r2.stream_mode = 0; r2.stream_track = 0;
    printf("file_win_test: stream copy failure and cancellation at all %d observed operations: ok\n", steps);
}
static void r2_stream_crash(void)
{
    char path[OS_PATH_MAX], exe[OS_PATH_MAX]; win_path(path, "stream-crash");
    win_write(path, (const uint8_t *)"original", 8); win_streams(path, true);
    CHECK(os_proc_self_path(exe, sizeof exe) == OS_OK);
    const char *args[] = {exe, "--child-stream-copy", path, NULL}; os_proc child; os_proc_status status;
    CHECK(os_proc_spawn(&child, args) == OS_OK);
    CHECK(os_proc_wait(&child, INT64_C(120) * INT64_C(1000000000), &status) == OS_OK);
    CHECK(status.how == OS_PROC_EXITED && status.code == 77);
    win_read_equals(path, (const uint8_t *)"original", 8); win_streams(path, false);
    /* Process death may leave its private temporary; verify it is never the
     * public target, then remove the test-owned artifact like win_durability. */
    CHECK(os_unlink(path) == OS_OK);
    os_dir dir = OS_DIR_INIT; os_dir_iter it; const char *name; bool seen = false;
    CHECK(os_dir_open(&dir, root) == OS_OK && os_dir_iter_open(&it, &dir) == OS_OK);
    for (;;) {
        CHECK(os_dir_next(&it, &name) == OS_OK); if (!name) break;
        CHECK(!strncmp(name, ".edit-", strlen(".edit-"))); seen = true;
        CHECK(os_unlink_at(&dir, name) == OS_OK);
    }
    CHECK(seen); os_dir_iter_close(&it); os_dir_close(&dir);
}
static void r2_stream_many(void)
{
    char path[OS_PATH_MAX]; win_path(path, "stream-many"); win_write(path, (const uint8_t *)"original", 8);
    /* Exceed the removed 65536-byte enumeration cap using near-component-limit
     * names. Count derives from the API entry header and counted UTF-16 name. */
    size_t units = 240, count = 65536u / (offsetof(FILE_STREAM_INFO, StreamName) + (240u + 7u) * sizeof(WCHAR)) + 1u;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < count; i++) {
            char name[256], stream[OS_PATH_MAX]; memset(name, 'x', units); name[units] = 0;
            CHECK(snprintf(name, 9, "%08zu", i) == 8); name[8] = 'x';
            CHECK(snprintf(stream, sizeof stream, "%s:%s", path, name) > 0);
            if (!pass) win_write(stream, (const uint8_t *)name, units);
            else win_read_equals(stream, (const uint8_t *)name, units);
        }
        if (!pass) CHECK(r2_save(path, NULL) == FILE_OK);
    }
    win_read_equals(path, (const uint8_t *)"replacement", 11); CHECK(os_unlink(path) == OS_OK);
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
    if (argc == 3 && !strcmp(argv[1], "--child-stream-copy")) {
        r2.stream_mode = 6; return r2_save(argv[2], NULL);
    }
    if (argc == 1) {
        const char *cases[] = {"security", "efs", "streams", "stream-unsupported", "stream-default", "stream-failures", "stream-crash", "stream-many", "descriptor", "rights", "label", "identity", "wake", "parent", "cache", "unicode", "failures", "watch-errors"};
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
    else if (!strcmp(argv[1], "stream-unsupported")) r2_stream_unsupported();
    else if (!strcmp(argv[1], "stream-default")) r2_stream_default();
    else if (!strcmp(argv[1], "stream-failures")) r2_stream_failures();
    else if (!strcmp(argv[1], "stream-crash")) r2_stream_crash();
    else if (!strcmp(argv[1], "stream-many")) r2_stream_many();
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
