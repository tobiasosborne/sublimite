/* Measurement only: real redirector + production os/file entry points.
 * Run tools/wsl_share_probe.sh, never this executable on the live desktop.
 * Each case runs in its own bounded child, on the runner's private station.
 * Counts/timeouts below are (E) probe policy, not capability/performance gates.
 */
#if defined(_WIN32)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000002
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <shlobj.h>
#include <aclapi.h>
NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os/os.h"
#include "file/file.h"
#include "base/base.h"
#include "trace/trace.h"
#include "test_isolation.h"

/* (E) Three independent trials expose repeat disagreement; no tail estimate.
 * (E) Observe notifications/coherence for two seconds, not a success budget.
 * (E) Five seconds for the marker helper, fifteen seconds per child: enough
 * for a helper timeout plus an observation window and teardown; never hang.
 */
#define TRIALS 3
#define OBSERVE_MS 2000u
#define HELPER_MS 5000u
#define CHILD_MS 15000u
#define SHARES (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
static const char *probe_root, *ipc, *relative, *backend, *case_name;
static unsigned trial;
static DWORD observed_exception, observed_status;
static int probe_fault_slot = -1;
static int harness_failed;
static char request_response[OS_PATH_MAX];

static void join(char *out, const char *base, const char *name)
{
    int n = snprintf(out, OS_PATH_MAX, "%s\\%s", base, name);
    if (n < 0 || (size_t)n >= OS_PATH_MAX) ExitProcess(2);
}
static void wide(const char *path, WCHAR *out)
{
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, OS_PATH_MAX)) ExitProcess(2);
}
static void cleanup_wide(const char *path, WCHAR *out)
{
    char extended[OS_PATH_MAX];
    if (path[0] == '\\' && path[1] == '\\') snprintf(extended, sizeof extended, "\\\\?\\UNC\\%s", path + 2);
    else snprintf(extended, sizeof extended, "\\\\?\\%s", path);
    wide(extended, out);
}
static void row(const char *operation, int result, DWORD error, const char *detail)
{
    printf("%s | %s | %u | %s | result=%d native=%lu | %s\n",
        backend, case_name, trial, operation, result, error, detail ? detail : "");
    fflush(stdout);
}
static void osrow(const char *operation, os_err result)
{
    DWORD error = result == OS_OK ? 0 : (DWORD)os_native_error();
    row(operation, (int)result, error, os_err_name(result));
}
static HANDLE native_open(const char *path, DWORD access, DWORD shares, DWORD disposition, DWORD flags)
{
    WCHAR w[OS_PATH_MAX]; wide(path, w);
    SetLastError(0);
    return CreateFileW(w, access, shares, NULL, disposition, flags, NULL);
}
static int write_bytes(const char *path, const char *bytes, size_t size)
{
    os_file f = OS_FILE_INIT;
    os_err e = os_file_open(&f, path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC);
    if (e) { osrow("fixture.open", e); return 0; }
    size_t n = 0;
    e = os_file_write(&f, bytes, size, &n);
    DWORD error = e ? (DWORD)os_native_error() : 0;
    os_file_close(&f);
    if (e || n != size) { row("fixture.write", (int)e, error, "short/error"); return 0; }
    return 1;
}
static int fixture(const char *name, size_t size)
{
    char path[OS_PATH_MAX]; join(path, probe_root, name);
    char *bytes = malloc(size ? size : 1u);
    if (!bytes) return 0;
    memset(bytes, 'A', size);
    int ok = write_bytes(path, bytes, size); free(bytes); return ok;
}
static void disk_bytes(const char *operation, const char *path)
{
    os_file f = OS_FILE_INIT; os_err e = os_file_open(&f, path, OS_OPEN_READ);
    if (e) { osrow(operation, e); return; }
    char bytes[65] = {0}, detail[256]; size_t got = 0;
    os_stat st = {0}; e = os_file_stat(&f, &st);
    if (!e) e = os_file_pread(&f, bytes, sizeof bytes - 1u, 0, &got);
    DWORD error = e ? (DWORD)os_native_error() : 0;
    snprintf(detail, sizeof detail, "size=%llu read=%zu bytes=%s",
        (unsigned long long)st.size, got, bytes);
    row(operation, (int)e, error, detail); os_file_close(&f);
}
/* Marker directory is outside every watched parent. Publish ready only after
 * closing request data; the handshake does not depend on the rename under test.
 * Helper validates the relative path stays inside its mkdtemp.
 */
static int request_start(const char *action, const char *name)
{
    char req[OS_PATH_MAX], ready[OS_PATH_MAX], token[128], namebuf[160], text[OS_PATH_MAX];
    snprintf(token, sizeof token, "%llu-%llu", (unsigned long long)os_proc_id(), (unsigned long long)os_now_ns());
    snprintf(namebuf, sizeof namebuf, "%s.request", token); join(req, ipc, namebuf);
    snprintf(namebuf, sizeof namebuf, "%s.ready", token); join(ready, ipc, namebuf);
    snprintf(namebuf, sizeof namebuf, "%s.response", token); join(request_response, ipc, namebuf);
    snprintf(text, sizeof text, "%s\n%s/%s\n", action, relative, name);
    HANDLE h = native_open(req, GENERIC_WRITE, SHARES, CREATE_ALWAYS, 0);
    if (h == INVALID_HANDLE_VALUE) { row("helper.request", 0, GetLastError(), "native marker open"); harness_failed = 1; return 0; }
    DWORD written = 0;
    BOOL ok = WriteFile(h, text, (DWORD)strlen(text), &written, NULL);
    DWORD error = ok ? 0 : GetLastError(); CloseHandle(h);
    if (!ok || written != strlen(text)) { row("helper.request", 0, error, "native marker write"); harness_failed = 1; return 0; }
    h = native_open(ready, GENERIC_WRITE, SHARES, CREATE_ALWAYS, 0);
    if (h == INVALID_HANDLE_VALUE) { row("helper.publish", 0, GetLastError(), "native ready open"); harness_failed = 1; return 0; }
    CloseHandle(h);
    return 1;
}
static int request_done(const char *action)
{
    uint64_t end = os_now_ns() + (uint64_t)HELPER_MS * 1000000u;
    do {
        os_file f = OS_FILE_INIT;
        if (os_file_open(&f, request_response, OS_OPEN_READ) == OS_OK) {
            char bytes[2048] = {0}; size_t got = 0;
            os_err e = os_file_read(&f, bytes, sizeof bytes - 1u, &got);
            DWORD error = e ? (DWORD)os_native_error() : 0; os_file_close(&f);
            row(action, e ? (int)e : (strncmp(bytes, "OK", 2) ? -1 : 0), error, bytes);
            return !e && !strncmp(bytes, "OK", 2);
        }
        Sleep(1);
    } while (os_now_ns() < end);
    row(action, -1, WAIT_TIMEOUT, "helper timeout; dependent observation invalid");
    harness_failed = 1;
    return 0;
}
static int is_share(void) { return strcmp(backend, "ntfs") != 0; }
static int linux_action(const char *action, const char *name)
{ return request_start(action, name) && request_done(action); }

static LONG CALLBACK observe_fault(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR || r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        observed_exception = r->ExceptionCode;
        observed_status = r->NumberParameters >= 3 ? (DWORD)r->ExceptionInformation[2] : 0;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG CALLBACK contain_fault(EXCEPTION_POINTERS *ep)
{
    /* Last VEH, after os_fault_service. Print even if the service cannot
     * recover, then end this case alone. No crash dialog or silent omission.
     * Parent additionally contains hangs and every other child exit code.
     */
    char text[512]; DWORD written;
    int n = snprintf(text, sizeof text,
        "%s | %s | %u | contained-exception | result=-1 native=0x%08lx | backing_status=0x%08lx epoch=%llu\n",
        backend, case_name, trial, ep->ExceptionRecord->ExceptionCode, observed_status,
        (unsigned long long)(probe_fault_slot >= 0 ? os_fault_epoch(probe_fault_slot) : 0));
    if (n > 0) (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)n, &written, NULL);
    ExitProcess(0);
}

static void identity_sample(const char *stage, const char *name)
{
    char path[OS_PATH_MAX], detail[512], operation[128]; join(path, probe_root, name);
    HANDLE h = native_open(path, GENERIC_READ, SHARES, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) { row(stage, -1, GetLastError(), "CreateFileW"); return; }
    FILE_ID_INFO id = {0}; SetLastError(0);
    BOOL ok = GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof id);
    DWORD error = ok ? 0 : GetLastError(); uint64_t lo = 0, hi = 0;
    memcpy(&lo, id.FileId.Identifier, sizeof lo); memcpy(&hi, id.FileId.Identifier + sizeof lo, sizeof hi);
    snprintf(detail, sizeof detail, "volume=%016llx ino=%016llx ino_hi=%016llx",
        (unsigned long long)id.VolumeSerialNumber, (unsigned long long)lo, (unsigned long long)hi);
    snprintf(operation, sizeof operation, "%s.FileIdInfo", stage); row(operation, ok, error, detail);
    BY_HANDLE_FILE_INFORMATION bi = {0}; SetLastError(0); ok = GetFileInformationByHandle(h, &bi);
    error = ok ? 0 : GetLastError();
    snprintf(detail, sizeof detail, "volume=%08lx ino=%08lx%08lx links=%lu", bi.dwVolumeSerialNumber,
        bi.nFileIndexHigh, bi.nFileIndexLow, bi.nNumberOfLinks);
    snprintf(operation, sizeof operation, "%s.legacy", stage); row(operation, ok, error, detail);
    CloseHandle(h);
    os_file f = OS_FILE_INIT; os_stat st = {0}; os_err e = os_file_open(&f, path, OS_OPEN_READ);
    if (!e) e = os_file_stat(&f, &st);
    error = e ? (DWORD)os_native_error() : 0;
    file_id fid = {0}; if (!e) file_id_from_stat(&fid, &st);
    snprintf(detail, sizeof detail, "volume=%016llx ino=%016llx ino_hi=%016llx file.ino_hi=%016llx size=%llu mode=%o uid=%u gid=%u",
        (unsigned long long)st.id.volume, (unsigned long long)st.id.index[0],
        (unsigned long long)st.id.index[1], (unsigned long long)fid.ino_hi,
        (unsigned long long)st.size, st.perm, st.uid, st.gid);
    snprintf(operation, sizeof operation, "%s.os_file_stat", stage); row(operation, (int)e, error, detail);
    os_file_close(&f);
}
static void identity_probe(void)
{
    if (!fixture("id", 32) || !fixture("other", 32)) return;
    char p[OS_PATH_MAX], q[OS_PATH_MAX]; join(p, probe_root, "id"); join(q, probe_root, "hard");
    if (is_share()) (void)linux_action("hardlink", "id");
    else { WCHAR wp[OS_PATH_MAX], wq[OS_PATH_MAX]; wide(p, wp); wide(q, wq);
        BOOL ok = CreateHardLinkW(wq, wp, NULL); row("hardlink", ok, ok ? 0 : GetLastError(), "Windows control"); }
    identity_sample("initial", "id"); identity_sample("reopen", "id");
    identity_sample("hardlink", "hard"); identity_sample("independent", "other");
    join(q, probe_root, "renamed"); os_err e = os_rename(p, q, OS_RENAME_NOREPLACE); osrow("rename", e);
    bool renamed = e == OS_OK;
    if (!renamed && is_share()) renamed = linux_action("identity-rename", "id") != 0;
    identity_sample("after-rename", renamed ? "renamed" : "id");
    if (is_share()) (void)linux_action("write", renamed ? "renamed" : "id");
    else (void)write_bytes(renamed ? q : p, "LINUX-EDIT", 10);
    identity_sample("after-edit", renamed ? "renamed" : "id");
    identity_sample("hardlink-after-edit", "hard");
}

static int native_mutation(const char *action, const char *path)
{
    char next[OS_PATH_MAX]; join(next, probe_root, "mapped-moved");
    WCHAR wp[OS_PATH_MAX], wn[OS_PATH_MAX]; wide(path, wp); wide(next, wn);
    BOOL ok = FALSE; DWORD error = 0;
    if (!strcmp(action, "truncate")) {
        HANDLE h = native_open(path, GENERIC_WRITE, SHARES, OPEN_EXISTING, 0);
        if (h != INVALID_HANDLE_VALUE) { ok = SetEndOfFile(h); error = ok ? 0 : GetLastError(); CloseHandle(h); }
        else error = GetLastError();
    } else if (!strcmp(action, "delete")) { ok = DeleteFileW(wp); error = ok ? 0 : GetLastError(); }
    else if (!strcmp(action, "rename")) { ok = MoveFileExW(wp, wn, MOVEFILE_REPLACE_EXISTING); error = ok ? 0 : GetLastError(); }
    else if (!strcmp(action, "replace")) {
        char temp[OS_PATH_MAX]; join(temp, probe_root, "replacement"); (void)write_bytes(temp, "BBBBBBBB", 8);
        WCHAR wt[OS_PATH_MAX]; wide(temp, wt); ok = MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING); error = ok ? 0 : GetLastError();
    } else {
        HANDLE h = native_open(path, GENERIC_WRITE, SHARES, OPEN_EXISTING, 0);
        if (h != INVALID_HANDLE_VALUE) { DWORD n; ok = WriteFile(h, "B", 1, &n, NULL); error = ok ? 0 : GetLastError(); CloseHandle(h); }
        else error = GetLastError();
    }
    row(action, ok, error, "Windows control mutation"); return ok != 0;
}
static void mapping_probe(void)
{
    /* Three allocation units: first page warm, last page not explicitly read
     * before the destructive action; extent derives from the OS ABI.
     */
    size_t length = 3u * os_map_granularity();
    if (!fixture("mapped", length)) return;
    char path[OS_PATH_MAX]; join(path, probe_root, "mapped");
    const char *action = strrchr(case_name, '-') + 1;
    bool use_os = strstr(case_name, "map-os-") != NULL;
    os_file f = OS_FILE_INIT; os_map m = {0}; HANDLE mapping = NULL, file_handle = INVALID_HANDLE_VALUE;
    volatile const unsigned char *view = NULL;
    if (use_os) {
        os_err e = os_file_open(&f, path, OS_OPEN_READ); osrow("os_file_open", e); if (e) return;
        e = os_map_file(&m, &f, 0, length); osrow("os_map_file", e); if (e) { os_file_close(&f); return; }
        e = os_fault_register(&m, &probe_fault_slot); osrow("os_fault_register", e); view = m.addr;
    } else {
        file_handle = native_open(path, GENERIC_READ, SHARES, OPEN_EXISTING, 0);
        if (file_handle == INVALID_HANDLE_VALUE) { row("CreateFileW", 0, GetLastError(), "mapping input"); return; }
        mapping = CreateFileMappingW(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
        row("CreateFileMapping", mapping != NULL, mapping ? 0 : GetLastError(), "PAGE_READONLY");
        if (mapping) view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, length);
        row("MapViewOfFile", view != NULL, view ? 0 : GetLastError(), "FILE_MAP_READ");
    }
    (void)AddVectoredExceptionHandler(0, contain_fault);
    if (view) {
        unsigned before = view[0];
        int changed;
        if (!strcmp(action, "trigger")) {
            row("synthetic-safety-control", 0, 0, "injected IN_PAGE_ERROR; not a filesystem capability observation");
            ULONG_PTR args[3] = {0, (ULONG_PTR)view + length - 1u, (ULONG_PTR)0xC000009Cu};
            RaiseException(EXCEPTION_IN_PAGE_ERROR, 0, 3, args); changed = 1;
        } else changed = is_share() && strcmp(action, "winrename")
            ? linux_action(action, "mapped") : native_mutation(!strcmp(action, "winrename") ? "rename" : action, path);
        if (changed) {
            uint64_t begin = os_now_ns(); unsigned first = view[0], last = view[length - 1u];
            unsigned immediate = first;
            if (!strcmp(action, "write")) {
                uint64_t end = begin + (uint64_t)OBSERVE_MS * 1000000u;
                while (first == before && os_now_ns() < end) { Sleep(1); first = view[0]; }
            }
            char detail[384];
            snprintf(detail, sizeof detail,
                "length=%zu before=%u immediate=%u final=%u tail=%u exception=0x%08lx backing_status=0x%08lx epoch=%llu observation_ns=%llu (M)",
                length, before, immediate, first, last, observed_exception, observed_status,
                (unsigned long long)(probe_fault_slot >= 0 ? os_fault_epoch(probe_fault_slot) : 0),
                (unsigned long long)(os_now_ns() - begin));
            row("mapped-read", 0, 0, detail); disk_bytes("disk-after-action", path);
        } else row("mapped-read", -1, 0, "SKIP mutation failed, view retained");
    }
    if (probe_fault_slot >= 0) os_fault_retire(probe_fault_slot);
    if (use_os) { os_unmap(&m); os_file_close(&f); }
    else { if (view) UnmapViewOfFile((const void *)view); if (mapping) CloseHandle(mapping); if (file_handle != INVALID_HANDLE_VALUE) CloseHandle(file_handle); }
}

static void notification_probe(void)
{
    if (!fixture("notice", 8)) return;
    char path[OS_PATH_MAX]; join(path, probe_root, "notice"); WCHAR wr[OS_PATH_MAX]; wide(probe_root, wr);
    HANDLE dir = CreateFileW(wr, FILE_LIST_DIRECTORY, SHARES, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (dir == INVALID_HANDLE_VALUE) { row("watch.directory", 0, GetLastError(), ""); return; }
    file_path_watch watch = {0}; SetLastError(0); int start = file_watch_path_start(&watch, path);
    row("file_watch_path_start", start, start < 0 ? GetLastError() : 0, "production shared watch service");
    const char *origins[] = {"windows-write", "linux-write", "linux-replace"};
    for (size_t source = 0; source < sizeof origins / sizeof origins[0]; source++) {
        if (source == 1 && !is_share()) { row(origins[source], -1, 0, "SKIP Linux cannot write read-only /mnt/c; native control above"); continue; }
        const char *origin = source == 2 && !is_share() ? "windows-replace-control" : origins[source];
        for (unsigned sample = 0; sample < TRIALS; sample++) {
            DWORD buffer[4096u / sizeof(DWORD)] = {0}, count = 0;
            OVERLAPPED ov = {0}; ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            SetLastError(0);
            BOOL armed = ReadDirectoryChangesW(dir, buffer, sizeof buffer, FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL, &ov, NULL);
            DWORD arm_error = armed ? 0 : GetLastError();
            file_watch_dispatch(); (void)file_watch_path_take(&watch);
            uint64_t begin = os_now_ns();
            int mutation;
            if (source && is_share()) mutation = request_start(source == 1 ? "write" : "replace", "notice");
            else if (source == 2) {
                char replacement[OS_PATH_MAX]; join(replacement, probe_root, "replacement");
                mutation = write_bytes(replacement, "BBBBBBBB", 8) && os_rename(replacement, path, OS_RENAME_REPLACE) == OS_OK;
            } else mutation = write_bytes(path, sample & 1u ? "WINDOWS1" : "WINDOWS2", 8);
            uint64_t native_time = 0, file_time = 0;
            DWORD completion_error = 0; bool native_done = false;
            uint64_t end = begin + (uint64_t)OBSERVE_MS * 1000000u;
            while (os_now_ns() < end && (!native_done || (start >= 0 && !file_time))) {
                if (armed && !native_done && WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0) {
                    native_done = true; native_time = os_now_ns() - begin;
                    if (!GetOverlappedResult(dir, &ov, &count, FALSE)) completion_error = GetLastError();
                }
                if (start >= 0 && !file_time) {
                    file_watch_dispatch(); if (file_watch_path_take(&watch)) file_time = os_now_ns() - begin;
                }
                if ((!armed || native_done) && (start < 0 || file_time)) break;
                Sleep(1);
            }
            char detail[768], events[400] = {0}; size_t used = 0, at = 0;
            while (native_done && !completion_error && at + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= count) {
                const FILE_NOTIFY_INFORMATION *notice = (const void *)((const unsigned char *)buffer + at);
                size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
                if (notice->FileNameLength > count - at - header) break;
                char name[128] = {0}; int chars = (int)(notice->FileNameLength / sizeof(WCHAR));
                int n = WideCharToMultiByte(CP_UTF8, 0, notice->FileName, chars, name, sizeof name - 1, NULL, NULL);
                if (n > 0 && used < sizeof events - 1u) {
                    int added = snprintf(events + used, sizeof events - used, "%s%lu:%s", used ? "," : "", notice->Action, name);
                    if (added > 0 && (size_t)added < sizeof events - used) used += (size_t)added;
                }
                if (!notice->NextEntryOffset || notice->NextEntryOffset > count - at) break;
                at += notice->NextEntryOffset;
            }
            snprintf(detail, sizeof detail,
                "sample=%u mutation=%d armed=%d arm_error=%lu completed=%d completion_error=%lu bytes=%lu native_delay_ns=%llu file_notice=%d file_delay_ns=%llu (M) window_ms=%u (E) events=%s",
                sample, mutation, armed, arm_error, native_done, completion_error, count,
                (unsigned long long)native_time, file_time != 0, (unsigned long long)file_time, OBSERVE_MS, events);
            row(origin, native_done ? 1 : 0, armed ? completion_error : arm_error, detail);
            if (source && is_share() && mutation) (void)request_done("linux-mutation");
            if (armed && !native_done) { CancelIoEx(dir, &ov); (void)GetOverlappedResult(dir, &ov, &count, TRUE); }
            CloseHandle(ov.hEvent);
        }
    }
    file_watch_path_stop(&watch); CloseHandle(dir);
}

static void atomic_probe(void)
{
    const char *methods[] = {"FileRenameInfoEx", "FileRenameInfo", "MoveFileEx", "os_rename"};
    const char *conditions[] = {"closed", "open-share-delete", "mapped", "deny-share-delete"};
    for (size_t method = 0; method < sizeof methods / sizeof methods[0]; method++) {
        for (size_t condition = 0; condition < sizeof conditions / sizeof conditions[0]; condition++) {
            char from[OS_PATH_MAX], to[OS_PATH_MAX], operation[128];
            join(from, probe_root, "source"); join(to, probe_root, "target");
            (void)os_unlink(from); (void)os_unlink(to);
            if (!write_bytes(from, "new", 3) || !write_bytes(to, "old", 3)) return;
            HANDLE held = INVALID_HANDLE_VALUE, map = NULL; void *view = NULL;
            if (condition) held = native_open(to, GENERIC_READ,
                condition == 3 ? FILE_SHARE_READ | FILE_SHARE_WRITE : SHARES, OPEN_EXISTING, 0);
            if (condition == 2 && held != INVALID_HANDLE_VALUE) {
                map = CreateFileMappingW(held, NULL, PAGE_READONLY, 0, 0, NULL);
                if (map) view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
            }
            HANDLE h = native_open(from, GENERIC_WRITE | DELETE, SHARES, OPEN_EXISTING, 0);
            if (h == INVALID_HANDLE_VALUE) { row("rename.source", 0, GetLastError(), ""); return; }
            BOOL flushed = FlushFileBuffers(h); row("FlushFileBuffers", flushed, flushed ? 0 : GetLastError(), "source data before rename; not a power-loss test");
            BOOL ok = FALSE; DWORD error = 0; WCHAR wt[OS_PATH_MAX]; wide(to, wt);
            if (method < 2) {
                union { FILE_RENAME_INFO info; unsigned char bytes[sizeof(FILE_RENAME_INFO) + OS_PATH_MAX * sizeof(WCHAR)]; } rename;
                memset(&rename, 0, sizeof rename);
                size_t len = wcslen(wt) * sizeof(WCHAR);
                rename.info.Flags = method == 0 ? 3u : 1u; /* P: REPLACE=1 | POSIX=2; fallback BOOL TRUE. */
                rename.info.FileNameLength = (DWORD)len; memcpy(rename.info.FileName, wt, len);
                ok = SetFileInformationByHandle(h, method == 0 ? FileRenameInfoEx : FileRenameInfo,
                    &rename, (DWORD)(sizeof(FILE_RENAME_INFO) + len)); error = ok ? 0 : GetLastError();
            } else if (method == 2) {
                WCHAR wf[OS_PATH_MAX]; wide(from, wf);
                ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING); error = ok ? 0 : GetLastError();
            } else {
                os_err e = os_rename(from, to, OS_RENAME_REPLACE); error = e ? (DWORD)os_native_error() : 0; ok = e == OS_OK;
            }
            char detail[256]; snprintf(detail, sizeof detail, "target_open=%d mapped=%d", held != INVALID_HANDLE_VALUE, view != NULL);
            snprintf(operation, sizeof operation, "%s.%s", methods[method], conditions[condition]); row(operation, ok, error, detail);
            disk_bytes("rename.target-bytes", to);
            if (view) { row("rename.retained-view", 0, 0, (const char *)view); UnmapViewOfFile(view); }
            if (map) CloseHandle(map);
            CloseHandle(h); if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
        }
    }
}

static void streams_probe(void)
{
    if (!fixture("stream", 8)) return;
    char path[OS_PATH_MAX], zone[OS_PATH_MAX]; join(path, probe_root, "stream"); join(zone, probe_root, "stream:Zone.Identifier");
    HANDLE h = native_open(path, GENERIC_READ, SHARES, OPEN_EXISTING, 0);
    if (h == INVALID_HANDLE_VALUE) { row("stream.open", 0, GetLastError(), ""); return; }
    unsigned char buffer[4096]; IO_STATUS_BLOCK io = {0};
    NTSTATUS status = NtQueryInformationFile(h, &io, buffer, sizeof buffer, (FILE_INFORMATION_CLASS)22);
    char detail[256]; snprintf(detail, sizeof detail, "NTSTATUS=0x%08lx information=%llu", (DWORD)status, (unsigned long long)io.Information);
    row("FileStreamInformation", status >= 0, status < 0 ? RtlNtStatusToDosError(status) : 0, detail);
    BOOL ok = GetFileInformationByHandleEx(h, FileStreamInfo, buffer, sizeof buffer);
    row("FileStreamInfo", ok, ok ? 0 : GetLastError(), "Win32 information class"); CloseHandle(h);
    h = native_open(zone, GENERIC_WRITE, SHARES, CREATE_NEW, 0);
    DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    row("Zone.Identifier.create", h != INVALID_HANDLE_VALUE, error, "CREATE_NEW, scratch only");
    if (h != INVALID_HANDLE_VALUE) {
        const char bytes[] = "[ZoneTransfer]\r\nZoneId=3\r\n"; DWORD n = 0;
        ok = WriteFile(h, bytes, sizeof bytes - 1u, &n, NULL);
        row("Zone.Identifier.write", ok, ok ? 0 : GetLastError(), "scratch MOTW stream"); CloseHandle(h);
    }
    if (is_share()) (void)linux_action("inspect-streams", "stream");
}

typedef struct received { file_msg message; bool done; } received;
static void receive_message(const work_msg *message, void *context)
{
    received *r = context; file_msg decoded;
    if (!file_msg_decode(message, &decoded) && (decoded.kind == FILE_MSG_OPEN_READY ||
        decoded.kind == FILE_MSG_OPEN_FAILED || decoded.kind == FILE_MSG_SAVE_DONE)) {
        r->message = decoded; r->done = true;
    }
}
static bool await_message(work_pool *pool, file *f, received *r)
{
    uint64_t end = os_now_ns() + (uint64_t)HELPER_MS * 1000000u;
    while (!r->done && os_now_ns() < end) {
        if (f) (void)file_save_busy(f);
        (void)work_mailbox_drain(pool, receive_message, r); if (!r->done) Sleep(1);
    }
    return r->done;
}
static void save_step(void *context, int step)
{ (void)context; row("file.save-step", step, 0, "production transaction hook"); }
static void metadata_probe(const char *path)
{
    HANDLE h = native_open(path, GENERIC_READ | READ_CONTROL, SHARES, OPEN_EXISTING, 0);
    if (h == INVALID_HANDLE_VALUE) { row("metadata.open", 0, GetLastError(), ""); return; }
    SECURITY_INFORMATION flags[] = {OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        LABEL_SECURITY_INFORMATION, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
        LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION |
        PROCESS_TRUST_LABEL_SECURITY_INFORMATION | ACCESS_FILTER_SECURITY_INFORMATION};
    const char *operations[] = {"GetSecurityInfo.owner-group-dacl", "GetSecurityInfo.label", "GetSecurityInfo.production-mask"};
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        PSECURITY_DESCRIPTOR descriptor = NULL;
        DWORD error = GetSecurityInfo(h, SE_FILE_OBJECT, flags[i], NULL, NULL, NULL, NULL, &descriptor);
        row(operations[i], error == ERROR_SUCCESS, error, "read-only native metadata query");
        if (descriptor) LocalFree(descriptor);
    }
    CloseHandle(h);
}
static void save_probe(void)
{
    char path[OS_PATH_MAX], target[OS_PATH_MAX]; join(target, probe_root, "save"); join(path, probe_root, "save");
    bool link = strstr(case_name, "symlink") != NULL, stream = strstr(case_name, "stream") != NULL;
    if (!write_bytes(target, "original\n", 9)) return;
    if (is_share()) {
        if (!linux_action(link ? "prepare-symlink" : "prepare-save", "save")) return;
        if (link) join(path, probe_root, "save-link");
    } else if (link) {
        WCHAR wt[OS_PATH_MAX], wl[OS_PATH_MAX]; wide(target, wt); join(path, probe_root, "save-link"); wide(path, wl);
        BOOL ok = CreateSymbolicLinkW(wl, wt, 2u); /* P: ALLOW_UNPRIVILEGED_CREATE. */
        row("prepare-symlink", ok, ok ? 0 : GetLastError(), "Windows control"); if (!ok) return;
    }
    if (stream) {
        char zone[OS_PATH_MAX]; join(zone, probe_root, "save:Zone.Identifier");
        HANDLE h = native_open(zone, GENERIC_WRITE, SHARES, CREATE_NEW, 0);
        row("save.Zone.Identifier.create", h != INVALID_HANDLE_VALUE, h == INVALID_HANDLE_VALUE ? GetLastError() : 0, "");
        if (h != INVALID_HANDLE_VALUE) { DWORD n; (void)WriteFile(h, "zone", 4, &n, NULL); CloseHandle(h); }
    }
    metadata_probe(target);
    trace_init(); work_pool pool;
    int status = work_pool_init(&pool, 1, 1); row("work_pool_init", status, 0, "one acquire lane plus one bulk lane (E)"); if (status) return;
    file_open_opts opts = {UINT64_MAX, 1}; file *f = NULL; received r = {0};
    status = file_open_begin(&pool, path, &opts, &f); row("file_open_begin", status, 0, "copy mode for metadata/save isolation");
    if (!status && await_message(&pool, NULL, &r)) {
        row("file.open-completion", r.message.status, (DWORD)r.message.err_no, file_path(f));
        if (r.message.kind == FILE_MSG_OPEN_READY && !r.message.status) {
            piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
            if (tree && file_attach(f, tree) == FILE_OK && piece_insert(tree, 0, (const uint8_t *)"saved-", 6) == PIECE_OK) {
                SetLastError(0); int watch = file_watch_start(f);
                row("file_watch_start", watch, watch < 0 ? GetLastError() : 0, "real file member");
                file_set_step_hook(f, save_step, NULL);
                memset(&r, 0, sizeof r); status = file_save_begin(f, tree, 0, 2);
                row("file_save_begin", status, 0, "ordinary non-FORCE existing-target save");
                if (!status && await_message(&pool, f, &r)) row("file.save-completion", r.message.status, (DWORD)r.message.err_no, "production prepare/commit transaction");
                else if (!status) row("file.save-completion", -1, WAIT_TIMEOUT, "no terminal message");
            } else row("file_attach/insert", -1, 0, "cannot construct edited buffer");
            if (tree) piece_destroy(tree);
        }
    } else row("file.open-completion", -1, WAIT_TIMEOUT, "no terminal message");
    if (f) file_close(f);
    work_pool_shutdown(&pool); disk_bytes("save.target-bytes", target);
    if (is_share()) (void)linux_action(link ? "inspect-symlink" : "inspect-save", "save");
    else if (link) { os_stat st = {0}; os_err e = os_stat_path(path, OS_STAT_NOFOLLOW, &st); osrow("symlink.nofollow", e); row("symlink.kind", (int)st.kind, 0, "OS_KIND_LINK expected"); }
    if (stream) {
        char zone[OS_PATH_MAX]; join(zone, probe_root, "save:Zone.Identifier"); disk_bytes("save.stream-after", zone);
    }
}

static void file_watch_probe(void)
{
    if (!fixture("watched", 8)) return;
    char path[OS_PATH_MAX]; join(path, probe_root, "watched");
    trace_init(); work_pool pool;
    if (work_pool_init(&pool, 1, 1)) { row("file-watch.pool", -1, 0, "initialization failed"); return; }
    file_open_opts opts = {UINT64_MAX, 1}; file *f = NULL; received r = {0};
    int status = file_open_begin(&pool, path, &opts, &f);
    if (status || !await_message(&pool, NULL, &r) || r.message.kind != FILE_MSG_OPEN_READY || r.message.status) {
        row("file-watch.open", status ? status : -1, (DWORD)r.message.err_no, "could not acquire");
    } else {
        SetLastError(0); int watch = file_watch_start(f); DWORD error = watch < 0 ? GetLastError() : 0;
        row("file_watch_start", watch, error, "actual file member + worker identity checks");
        const char *origins[] = {"windows-write", "linux-write", "linux-replace"};
        for (size_t origin = 0; origin < sizeof origins / sizeof origins[0]; origin++) {
            if (origin && !is_share()) continue;
            for (unsigned sample = 0; sample < TRIALS; sample++) {
                status = file_resolve_keep(f); row("file_resolve_keep", status, status ? (DWORD)file_errno(f) : 0, "reset COPY baseline between samples");
                uint64_t begin = os_now_ns();
                int mutation = origin ? request_start(origin == 1 ? "write" : "replace", "watched")
                    : write_bytes(path, sample & 1u ? "WINDOWS1" : "WINDOWS2", 8);
                uint64_t end = begin + (uint64_t)OBSERVE_MS * 1000000u;
                int noticed = 0;
                if (watch >= 0) do {
                    (void)file_watch_poll(f); (void)work_mailbox_drain(&pool, receive_message, &r);
                    noticed = file_changed(f); if (!noticed) Sleep(1);
                } while (!noticed && os_now_ns() < end);
                uint64_t elapsed = os_now_ns() - begin;
                if (origin && mutation) (void)request_done("linux-mutation");
                uint32_t reasons = 0; int polled = file_check(f, &reasons);
                char detail[384]; snprintf(detail, sizeof detail,
                    "sample=%u mutation=%d start=%d start_error=%lu changed_via_watch=%d delay_ns=%llu (M) explicit_file_check=%d reasons=%u",
                    sample, mutation, watch, error, noticed, (unsigned long long)elapsed, polled, reasons);
                row(origins[origin], noticed, error, detail);
            }
        }
    }
    if (f) file_close(f);
    work_pool_shutdown(&pool);
}

static void fallback_metadata_probe(void)
{
    char from[OS_PATH_MAX], to[OS_PATH_MAX]; join(from, probe_root, "replacement"); join(to, probe_root, "save");
    if (!write_bytes(from, "saved\n", 6) || !write_bytes(to, "original\n", 9)) return;
    if (is_share() && !linux_action("prepare-save", "save")) return;
    WCHAR wf[OS_PATH_MAX], wt[OS_PATH_MAX]; wide(from, wf); wide(to, wt);
    BOOL ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
    row("naive-fallback.MoveFileEx", ok, ok ? 0 : GetLastError(), "native experiment only; production save is unchanged");
    if (is_share()) (void)linux_action("inspect-save", "save");
    os_file f = OS_FILE_INIT; os_err e = os_file_open(&f, to, OS_OPEN_RDWR);
    if (!e) e = os_file_set_mode(&f, 0751u);
    osrow("os_file_set_mode.0751", e); os_file_close(&f);
    if (is_share()) (void)linux_action("inspect-save", "save");
    disk_bytes("fallback.bytes", to);
}

static void locks_probe(void)
{
    if (!fixture("locked", 8)) return;
    char path[OS_PATH_MAX]; join(path, probe_root, "locked");
    HANDLE a = native_open(path, GENERIC_READ | GENERIC_WRITE, SHARES, OPEN_EXISTING, 0);
    HANDLE b = native_open(path, GENERIC_READ | GENERIC_WRITE, SHARES, OPEN_EXISTING, 0);
    if (a == INVALID_HANDLE_VALUE || b == INVALID_HANDLE_VALUE) { row("LockFileEx.open", 0, GetLastError(), ""); if (a != INVALID_HANDLE_VALUE) CloseHandle(a); if (b != INVALID_HANDLE_VALUE) CloseHandle(b); return; }
    OVERLAPPED ov = {0}; BOOL ok = LockFileEx(a, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    row("LockFileEx.first", ok, ok ? 0 : GetLastError(), "byte zero");
    BOOL second = LockFileEx(b, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    row("LockFileEx.contender", second, second ? 0 : GetLastError(), "independent handle, same byte");
    if (is_share()) (void)linux_action("linux-lock", "locked");
    if (second) UnlockFileEx(b, 0, 1, 0, &ov);
    if (ok) UnlockFileEx(a, 0, 1, 0, &ov);
    CloseHandle(a); CloseHandle(b);
    os_dir da = OS_DIR_INIT, db = OS_DIR_INIT;
    os_err e = os_dir_open(&da, probe_root); osrow("os_dir_open.first", e);
    if (!e) { e = os_dir_try_lock(&da, OS_LOCK_EXCLUSIVE); osrow("os_dir_try_lock.first", e); }
    e = os_dir_open(&db, probe_root); osrow("os_dir_open.second", e);
    if (!e) { e = os_dir_try_lock(&db, OS_LOCK_EXCLUSIVE); osrow("os_dir_try_lock.contender", e); }
    os_dir_close(&db); os_dir_close(&da);
}
static void name_open(const char *name, const char *path)
{
    os_file f = OS_FILE_INIT; char bytes[32] = {0}, detail[256]; size_t n = 0;
    os_err e = os_file_open(&f, path, OS_OPEN_READ); DWORD error = e ? (DWORD)os_native_error() : 0;
    if (!e) { e = os_file_read(&f, bytes, sizeof bytes - 1u, &n); error = e ? (DWORD)os_native_error() : 0; }
    snprintf(detail, sizeof detail, "read=%zu bytes=%s", n, bytes); row(name, (int)e, error, detail); os_file_close(&f);
}
static void names_probe(void)
{
    if (is_share()) { if (!linux_action("prepare-names", "names")) return; }
    else {
        (void)fixture("a", 1); char p[OS_PATH_MAX]; join(p, probe_root, "A"); (void)write_bytes(p, "UPPER", 5);
        row("case-fixture", 0, 0, "default NTFS directory: A names same object as a");
    }
    const char *names[] = {"a", "A", "a:b", "quote\"", "trailing.", "space "};
    if (!is_share()) for (size_t i = 2; i < sizeof names / sizeof names[0]; i++) {
        char p[OS_PATH_MAX], operation[128]; join(p, probe_root, names[i]);
        os_file f = OS_FILE_INIT; os_err e = os_file_open(&f, p, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL);
        snprintf(operation, sizeof operation, "control-create.%s", names[i]); osrow(operation, e);
        if (!e) { size_t n; (void)os_file_write(&f, "control", 7, &n); }
        os_file_close(&f);
    }
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char p[OS_PATH_MAX]; join(p, probe_root, names[i]); name_open(names[i], p);
    }
    if (is_share()) {
        char map_path[OS_PATH_MAX], paths[6u * OS_PATH_MAX] = {0}; join(map_path, probe_root, "name-map");
        os_file map_file = OS_FILE_INIT;
        os_err e = os_file_open(&map_file, map_path, OS_OPEN_READ); size_t got = 0;
        if (!e) e = os_file_read(&map_file, paths, sizeof paths - 1u, &got);
        if (e) osrow("wslpath.name-map", e);
        else {
            char *line = paths;
            for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
                char *end = strchr(line, '\n'); if (!end) break; *end = 0;
                char operation[128]; snprintf(operation, sizeof operation, "wslpath.%s", names[i]);
                name_open(operation, line); line = end + 1;
            }
        }
        os_file_close(&map_file);
    }
    char base[OS_PATH_MAX], path[OS_PATH_MAX], extended[OS_PATH_MAX]; snprintf(base, sizeof base, "%s", probe_root);
    /* (E) Add components until above MAX_PATH (P: 260 UTF-16 units).
     * Five 48-byte components exceed 260 even with the shortest probe_root here.
     */
    for (unsigned i = 0; i < 5; i++) {
        join(path, base, "long-component-abcdefghijklmnopqrstuvwxyz-0123456");
        os_err e = os_mkdir(path); if (e) { osrow("long.mkdir", e); return; }
        snprintf(base, sizeof base, "%s", path);
    }
    join(path, base, "long-file"); (void)write_bytes(path, "long", 4); name_open("long.os_file_open", path);
    char detail[64]; snprintf(detail, sizeof detail, "utf8_bytes=%zu (M)", strlen(path)); row("long.length", 0, 0, detail);
    if (path[0] == '\\' && path[1] == '\\') snprintf(extended, sizeof extended, "\\\\?\\UNC\\%s", path + 2);
    else snprintf(extended, sizeof extended, "\\\\?\\%s", path);
    name_open("extended-prefix.os_file_open", extended);
    os_file f = OS_FILE_INIT; os_err e = os_file_open(&f, extended, OS_OPEN_READ); os_stat a = {0}, b = {0};
    if (!e) e = os_file_stat(&f, &a);
    os_file_close(&f);
    os_err other = os_stat_path(path, 0, &b);
    row("extended-prefix.same-identity", !e && !other && os_identity_equal(&a.id, &b.id), 0, "normal versus explicit extended path");
}

static const char *cases[] = {"identity", "map-native-trigger", "map-os-trigger", "map-native-truncate", "map-os-truncate",
    "map-native-replace", "map-os-replace", "map-native-delete", "map-os-delete",
    "map-native-write", "map-os-write", "map-native-rename", "map-os-rename",
    "map-native-winrename", "map-os-winrename", "notifications", "file-watch", "atomic", "streams",
    "save", "save-symlink", "save-stream", "fallback-metadata", "locks", "names"};

static int child(int argc, char **argv)
{
    if (argc != 9) return 2;
    backend = argv[2]; case_name = argv[3]; trial = (unsigned)strtoul(argv[4], NULL, 10);
    probe_root = argv[5]; ipc = argv[6]; relative = argv[7];
    /* dirlock service must never touch the real user's LOCALAPPDATA. */
    if (os_env_set("LOCALAPPDATA", argv[8]) != OS_OK) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (test_isolation_query() != TEST_ISOLATION_STATION) return 2;
    (void)AddVectoredExceptionHandler(1, observe_fault);
    os_err e = os_mkdir(probe_root); if (e) { osrow("scratch.mkdir", e); return 2; }
    if (!strcmp(case_name, "identity")) identity_probe();
    else if (!strncmp(case_name, "map-", 4)) mapping_probe();
    else if (!strcmp(case_name, "notifications")) notification_probe();
    else if (!strcmp(case_name, "file-watch")) file_watch_probe();
    else if (!strcmp(case_name, "fallback-metadata")) fallback_metadata_probe();
    else if (!strcmp(case_name, "atomic")) atomic_probe();
    else if (!strcmp(case_name, "streams")) streams_probe();
    else if (!strncmp(case_name, "save", 4)) save_probe();
    else if (!strcmp(case_name, "locks")) locks_probe();
    else if (!strcmp(case_name, "names")) names_probe();
    else return 2;
    return harness_failed ? 3 : 0;
}
static void remove_tree(const char *path)
{
    /* Controller owns this NTFS mkdtemp analogue. Never follow reparse points. */
    char pattern[OS_PATH_MAX]; join(pattern, path, "*"); WCHAR wp[OS_PATH_MAX]; cleanup_wide(pattern, wp);
    WIN32_FIND_DATAW entry; HANDLE find = FindFirstFileW(wp, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            char name[OS_NAME_MAX], item[OS_PATH_MAX];
            if (!WideCharToMultiByte(CP_UTF8, 0, entry.cFileName, -1, name, sizeof name, NULL, NULL)) continue;
            join(item, path, name);
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(item);
            else { WCHAR wi[OS_PATH_MAX]; cleanup_wide(item, wi); SetFileAttributesW(wi, FILE_ATTRIBUTE_NORMAL);
                if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(wi); else DeleteFileW(wi); }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    WCHAR w[OS_PATH_MAX]; cleanup_wide(path, w); if (!RemoveDirectoryW(w)) printf("cleanup | %s | native=%lu\n", path, GetLastError());
}
static int controller(int argc, char **argv)
{
    if (argc != 5 || test_isolation_query() != TEST_ISOLATION_STATION) return 2;
    char temp[OS_PATH_MAX], ntfs[OS_PATH_MAX], profile[OS_PATH_MAX];
    if (os_temp_dir(temp, sizeof temp)) return 2;
    snprintf(ntfs, sizeof ntfs, "%s\\sublimite-test-wsl-probe-%llu-%llu", temp,
        (unsigned long long)os_proc_id(), (unsigned long long)os_wall_ns());
    if (os_mkdir(ntfs)) return 2;
    join(profile, ntfs, "profile"); if (os_mkdir(profile)) { remove_tree(ntfs); return 2; }
    char local[OS_PATH_MAX], dollar[OS_PATH_MAX]; join(local, argv[2], "localhost"); join(dollar, argv[3], "dollar");
    if (os_mkdir(local) || os_mkdir(dollar)) { remove_tree(ntfs); return 2; }
    const char *roots[] = {local, dollar, ntfs}; const char *backends[] = {"localhost", "dollar", "ntfs"};
    WCHAR self[OS_PATH_MAX]; if (!GetModuleFileNameW(NULL, self, OS_PATH_MAX)) return 2;
    printf("protocol | trials=%u (E) observe_ms=%u (E) helper_ms=%u (E) child_ms=%u (E)\n", TRIALS, OBSERVE_MS, HELPER_MS, CHILD_MS);
    printf("paths | localhost=%s | dollar=%s | ntfs=%s | profile=%s\n", local, dollar, ntfs, profile); fflush(stdout);
    RTL_OSVERSIONINFOW version = {0}; version.dwOSVersionInfoSize = sizeof version;
    if (RtlGetVersion(&version) >= 0) printf("windows | %lu.%lu build=%lu (M)\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    char anchor[OS_PATH_MAX], alias[OS_PATH_MAX]; join(anchor, argv[2], "alias-anchor"); join(alias, argv[3], "alias-anchor");
    if (write_bytes(anchor, "alias", 5)) {
        os_stat a = {0}, b = {0}; os_err ea = os_stat_path(anchor, 0, &a), eb = os_stat_path(alias, 0, &b);
        printf("aliases | localhost_error=%d dollar_error=%d same_identity=%d (M)\n", (int)ea, (int)eb,
            !ea && !eb && os_identity_equal(&a.id, &b.id));
    }
    int failed = 0;
    for (size_t b = 0; b < sizeof roots / sizeof roots[0]; b++) {
        for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
            for (unsigned t = 0; t < TRIALS; t++) {
                char leaf[128], dir[OS_PATH_MAX], rel[256];
                snprintf(leaf, sizeof leaf, "%s-%u", cases[c], t); join(dir, roots[b], leaf);
                snprintf(rel, sizeof rel, "%s/%s", backends[b], leaf);
                WCHAR wr[OS_PATH_MAX], wi[OS_PATH_MAX], wp[OS_PATH_MAX], command[4u * OS_PATH_MAX];
                wide(dir, wr); wide(argv[4], wi); wide(profile, wp);
                int n = swprintf(command, sizeof command / sizeof command[0],
                    L"\"%ls\" --case %hs %hs %u \"%ls\" \"%ls\" %hs \"%ls\"", self, backends[b], cases[c], t, wr, wi, rel, wp);
                if (n < 0) { failed++; continue; }
                STARTUPINFOW startup = {0}; startup.cb = sizeof startup; PROCESS_INFORMATION process;
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                (void)SetHandleInformation(startup.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                (void)SetHandleInformation(startup.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                BOOL ok = CreateProcessW(self, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
                if (!ok) { printf("supervisor | %s | %s | %u | spawn native=%lu\n", backends[b], cases[c], t, GetLastError()); failed++; continue; }
                DWORD wait = WaitForSingleObject(process.hProcess, CHILD_MS), code = 0;
                if (wait != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, WAIT_TIMEOUT); (void)WaitForSingleObject(process.hProcess, HELPER_MS); }
                GetExitCodeProcess(process.hProcess, &code);
                printf("supervisor | %s | %s | %u | wait=%lu exit=%lu\n", backends[b], cases[c], t, wait, code); fflush(stdout);
                if (wait != WAIT_OBJECT_0 || code) failed++;
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
            }
        }
    }
    remove_tree(ntfs);
    printf("availability | UNKNOWN | distro shutdown/restart forbidden; settle on a disposable second host/distro from Windows\n");
    printf("supervisor | failed=%d\n", failed); return failed ? 1 : 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc > 1 && !strcmp(argv[1], "--case")) return child(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--run")) return controller(argc, argv);
    fprintf(stderr, "Run tools/wsl_share_probe.sh (private station + Linux helper).\n"); return 2;
}

/* Profile isolation only. os_app_dir prefers the native Known Folder over
 * LOCALAPPDATA, so setting that environment variable alone is insufficient.
 * Compile the unchanged OS implementation here, with just this lookup made
 * unavailable, taking its real environment fallback to the private profile.
 * Every filesystem/mapping/fault/lock call remains the production call.
 */
static HRESULT WINAPI probe_known_folder(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR *out)
{ (void)id; (void)flags; (void)token; *out = NULL; return E_FAIL; }
#define SHGetKnownFolderPath probe_known_folder
#include "os/os_win.c"
#undef SHGetKnownFolderPath
#endif
