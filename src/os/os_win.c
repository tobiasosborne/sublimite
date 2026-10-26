/* os_win.c - Windows implementation of os.h (edit-177r.3, W0.3). Each entry
 * follows the Win32/NT recipe docs/decisions/edit-177r.2.md §3 gives per
 * family; the choices made while implementing are in
 * docs/decisions/edit-177r.3.md. Paths are UTF-8 at the API and converted on
 * the stack to NT object names (\??\C:\... or \??\UNC\server\share\...), so
 * nothing here allocates except the entries os.h lists (os_dir_iter_open,
 * os_proc_spawn, os_file_to_stdio, os_thread_start through the CRT).
 * Every open goes through NtCreateFile: path opens with a NULL root and the
 * NT name, *_at opens with the os_dir's handle as RootDirectory, so a renamed
 * or replaced parent path never redirects a save (P1.7c §7). The native code
 * os_native_error() reports is GetLastError(), set from the NTSTATUS with
 * RtlNtStatusToDosError. Wrapped whole: Linux compiles os_posix.c instead. */
#if defined(_WIN32)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#include "os/os.h"

#include <windows.h>
#include <winternl.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* ---- NT declarations the mingw headers lack ----------------------------- */
#define NT_FILE_RENAME_INFORMATION_EX 65
#define NT_FILE_DISPOSITION_INFORMATION_EX 64
#define NT_FILE_RENAME_REPLACE_IF_EXISTS 0x01u
#define NT_FILE_RENAME_POSIX_SEMANTICS 0x02u
#define NT_FILE_RENAME_IGNORE_READONLY_ATTRIBUTE 0x40u   /* ntifs.h; absent from winbase.h */
#define NT_FILE_DISPOSITION_DELETE 0x01u
#define NT_FILE_DISPOSITION_POSIX_SEMANTICS 0x02u
#define NT_FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x10u
typedef struct nt_rename_info {
    union { BOOLEAN ReplaceIfExists; ULONG Flags; } u;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} nt_rename_info;
typedef struct nt_disposition_info { ULONG Flags; } nt_disposition_info;
#ifndef STATUS_FILE_IS_A_DIRECTORY
#define STATUS_FILE_IS_A_DIRECTORY ((NTSTATUS)0xC00000BAL)
#endif
#ifndef STATUS_NOT_A_DIRECTORY
#define STATUS_NOT_A_DIRECTORY ((NTSTATUS)0xC0000103L)
#endif
#ifndef STATUS_DELETE_PENDING
#define STATUS_DELETE_PENDING ((NTSTATUS)0xC0000056L)
#endif
#ifndef STATUS_DEVICE_DATA_ERROR
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009CL)
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#ifndef IO_REPARSE_TAG_LX_FIFO
#define IO_REPARSE_TAG_LX_FIFO 0x80000024u
#define IO_REPARSE_TAG_LX_CHR 0x80000025u
#define IO_REPARSE_TAG_LX_BLK 0x80000026u
#endif
#ifndef ERROR_DIRECTORY_NOT_SUPPORTED
#define ERROR_DIRECTORY_NOT_SUPPORTED 336
#endif
/* Placeholder mapping (Windows 10 1803+): the headers declare these only for
 * NTDDI >= RS4 and libkernel32.a lacks them (they are api-set exports, in
 * libonecore.a). Resolved from kernelbase.dll at first use so the static link
 * keeps plain kernel32 imports. */
typedef PVOID (WINAPI *valloc2_fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *mapview3_fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef BOOL (WINAPI *unmap2_fn)(HANDLE, PVOID, ULONG);
typedef DWORD (WINAPI *temp2_fn)(DWORD, LPWSTR);
/* Write-once process-wide caches, one object (CLAUDE.md globals line; the
 * other mutable global here is os_fault_service, the sanctioned service). */
static struct os_win_state {
    LONGLONG freq;              /* QueryPerformanceFrequency */
    uint64_t cpu_res;           /* os_thread_cpu_resolution_ns, measured at first use */
    SYSTEM_INFO si;             /* page size, allocation granularity */
    valloc2_fn valloc2;         /* kernelbase placeholder APIs, probed once */
    mapview3_fn mapview3;
    unmap2_fn unmap2;
    INIT_ONCE immutable_once;
    INIT_ONCE cpu_once;
    temp2_fn temp2;             /* GetTempPath2W (build 20348+), NULL before */

} os_win_state;
#define kb os_win_state
/* Publish the immutable cache as a unit. CPU calibration has its own once:
 * it calls os_now_ns and must not recurse into the immutable initializer. */
static BOOL CALLBACK immutable_init(PINIT_ONCE once, PVOID arg, PVOID *ctx)
{
    (void)once; (void)arg; (void)ctx;
    HMODULE m = GetModuleHandleW(L"kernelbase.dll");
    if (m) {
        kb.valloc2 = (valloc2_fn)(void *)GetProcAddress(m, "VirtualAlloc2");
        kb.mapview3 = (mapview3_fn)(void *)GetProcAddress(m, "MapViewOfFile3");
        kb.unmap2 = (unmap2_fn)(void *)GetProcAddress(m, "UnmapViewOfFile2");
    }
    m = GetModuleHandleW(L"kernel32.dll");
    kb.temp2 = m ? (temp2_fn)(void *)GetProcAddress(m, "GetTempPath2W") : NULL;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    kb.freq = f.QuadPart;
    GetSystemInfo(&kb.si);
    return TRUE;
}
static void immutable_ready(void)
{
    (void)InitOnceExecuteOnce(&kb.immutable_once, immutable_init, NULL, NULL);
}
static bool placeholders_ok(void)
{
    immutable_ready();
    return kb.valloc2 && kb.mapview3 && kb.unmap2;
}
/* Expanded only by the white-box supplemental suite; no branch or pointer in
 * the normal implementation, including its release fault/clock paths. */
#ifndef OS_WIN_TEST_HOOK
#define OS_WIN_TEST_HOOK(event, context) ((void)0)
#endif
#define SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define NS_PER_S 1000000000ull
/* (P) FILETIME units of 100 ns between 1601-01-01 and 1970-01-01: 11644473600 s. */
#define EPOCH_1601_TO_1970 116444736000000000ll
/* (P) The byte os_file_try_lock / os_dir_try_lock lock: 2^62 = 4 EiB, above
 * NTFS's largest file (2^64 bytes nominal, 2^48 implemented) so the mandatory
 * byte-range lock never covers data. */
#define LOCK_BYTE_OFFSET (1ull << 62)

/* ---- errors ------------------------------------------------------------- */
static os_err map_win(DWORD e)
{
    switch (e) {
    case 0: return OS_E_IO;
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_BAD_NETPATH:
    case ERROR_INVALID_DRIVE: case ERROR_BAD_NET_NAME: return OS_E_NOENT;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: case ERROR_DIR_NOT_EMPTY: return OS_E_EXIST;
    case ERROR_ACCESS_DENIED: case ERROR_WRITE_PROTECT: return OS_E_ACCESS;
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: case ERROR_USER_MAPPED_FILE:
    case ERROR_BUSY: return OS_E_BUSY;
    case ERROR_STOPPED_ON_SYMLINK: case ERROR_CANT_ACCESS_FILE: return OS_E_LOOP;
    case ERROR_DIRECTORY: return OS_E_NOTDIR;
    case ERROR_DIRECTORY_NOT_SUPPORTED: return OS_E_ISDIR;
    case ERROR_NOT_SAME_DEVICE: return OS_E_XDEV;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return OS_E_NOSPC;
    case ERROR_FILENAME_EXCED_RANGE: case ERROR_BUFFER_OVERFLOW: return OS_E_NAMETOOLONG;
    case ERROR_INVALID_PARAMETER: case ERROR_INVALID_NAME: case ERROR_INVALID_HANDLE: return OS_E_INVAL;
    case ERROR_NOT_SUPPORTED: case ERROR_INVALID_FUNCTION: case ERROR_CALL_NOT_IMPLEMENTED: return OS_E_NOTSUP;
    case WAIT_TIMEOUT: return OS_E_TIMEDOUT;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: case ERROR_NO_SYSTEM_RESOURCES: return OS_E_NOMEM;
    case ERROR_INSUFFICIENT_BUFFER: return OS_E_RANGE;
    default: return OS_E_IO;
    }
}
static os_err fail(void) { return map_win(GetLastError()); }
/* An error the layer itself detects: no native code. */
static os_err fail_with(os_err e) { SetLastError(0); return e; }
/* NTSTATUS -> native code (GetLastError) and os_err. A few statuses are more
 * precise than their DOS translation. */
static os_err fail_nt(NTSTATUS s)
{
    DWORD e = RtlNtStatusToDosError(s);
    SetLastError(e);
    if (s == STATUS_FILE_IS_A_DIRECTORY) return OS_E_ISDIR;
    if (s == STATUS_NOT_A_DIRECTORY) return OS_E_NOTDIR;
    if (s == STATUS_DELETE_PENDING) return OS_E_NOENT;
    return map_win(e);
}

const char *os_err_name(os_err e)
{
    static const char names[][18] = {
        "OS_OK", "OS_E_NOENT", "OS_E_EXIST", "OS_E_ACCESS", "OS_E_BUSY", "OS_E_LOOP",
        "OS_E_NOTDIR", "OS_E_ISDIR", "OS_E_XDEV", "OS_E_NOSPC", "OS_E_NAMETOOLONG",
        "OS_E_INVAL", "OS_E_NOTSUP", "OS_E_AGAIN", "OS_E_TIMEDOUT", "OS_E_NOMEM",
        "OS_E_RANGE", "OS_E_IO", "OS_E_INTR"};
    unsigned i = (unsigned)e;
    return i < sizeof names / sizeof names[0] ? names[i] : "OS_E_?";
}
int os_native_error(void) { return (int)GetLastError(); }

/* ---- UTF-8 <-> UTF-16, NT paths ----------------------------------------- */
/* UTF-8 -> UTF-16 with NUL into out[cap]; *units = count without the NUL. */
static os_err to_wide(const char *s, WCHAR *out, size_t cap, size_t *units)
{
    if (!s) return fail_with(OS_E_INVAL);
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, (int)cap);
    if (n <= 0) return GetLastError() == ERROR_INSUFFICIENT_BUFFER ? fail_with(OS_E_NAMETOOLONG) : fail_with(OS_E_INVAL);
    if (units) *units = (size_t)n - 1;
    return OS_OK;
}
/* UTF-16 (wlen units, or -1 for NUL-terminated) -> UTF-8 into out[cap]. */
static os_err to_utf8(const WCHAR *w, int wlen, char *out, size_t cap)
{
    if (!out || !cap) return fail_with(OS_E_RANGE);
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, wlen, out, (int)cap, NULL, NULL);
    if (n <= 0 && !(wlen == 0)) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) return fail_with(OS_E_RANGE);
        return fail_with(OS_E_INVAL);
    }
    if (wlen >= 0) {
        if ((size_t)n >= cap) return fail_with(OS_E_RANGE);
        out[n] = 0;
    }
    return OS_OK;
}
static void set_ustr(UNICODE_STRING *us, WCHAR *w, size_t units)
{
    us->Buffer = w;
    us->Length = (USHORT)(units * sizeof(WCHAR));
    us->MaximumLength = us->Length;
}
/* UTF-8 path -> absolute NT object name in out[OS_PATH_MAX], normalised by
 * GetFullPathNameW (relative paths resolve against the current directory,
 * '/' becomes '\', . and .. collapse): \??\C:\x, \??\UNC\server\share\x. */
static os_err nt_path(const char *path, WCHAR *out, UNICODE_STRING *us)
{
    WCHAR in[OS_PATH_MAX], full[OS_PATH_MAX];
    os_err rc = to_wide(path, in, OS_PATH_MAX, NULL);
    if (rc) return rc;
    if (!in[0]) return fail_with(OS_E_NOENT);
    for (WCHAR *at = in; *at; at++) if (*at == L'/') *at = L'\\';
    WCHAR *leaf = wcsrchr(in, L'\\');
    leaf = leaf ? leaf + 1 : in + (in[0] && in[1] == L':' ? 2 : 0);
    WCHAR literal[OS_PATH_MAX];
    size_t units = wcslen(leaf);
    bool keep = units && wcscmp(leaf, L".") && wcscmp(leaf, L"..");
    if (keep) {
        memcpy(literal, leaf, (units + 1) * sizeof(WCHAR));
        /* Normalise a harmless final component, then append the literal name.
         * Win32 otherwise silently trims a final period or space. */
        leaf[0] = L'x'; leaf[1] = 0;
    }
    DWORD n = GetFullPathNameW(in, OS_PATH_MAX, full, NULL);
    if (!n) return fail();
    if (n >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    if (keep) {
        WCHAR *tail = wcsrchr(full, L'\\');
        if (!tail) return fail_with(OS_E_INVAL);
        size_t parent = (size_t)(tail + 1 - full);
        if (units >= OS_PATH_MAX - parent) return fail_with(OS_E_NAMETOOLONG);
        memcpy(full + parent, literal, (units + 1) * sizeof(WCHAR));
    }
    const WCHAR *rest = full, *pre = L"\\??\\";
    if ((full[0] == L'\\' && full[1] == L'\\') && (full[2] == L'?' || full[2] == L'.') && full[3] == L'\\')
        rest = full + 4;
    else if (full[0] == L'\\' && full[1] == L'\\') { rest = full + 2; pre = L"\\??\\UNC\\"; }
    size_t pl = wcslen(pre), rl = wcslen(rest);
    if (pl + rl >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    memcpy(out, pre, pl * sizeof(WCHAR));
    memcpy(out + pl, rest, (rl + 1) * sizeof(WCHAR));
    set_ustr(us, out, pl + rl);
    return OS_OK;
}
/* A single name component for *_at calls (os.h: never a path); "." is the
 * directory itself, which NT spells as an empty relative name. */
static os_err rel_name(const char *name, WCHAR *out, UNICODE_STRING *us)
{
    if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\')) return fail_with(OS_E_INVAL);
    size_t units = 0;
    if (strcmp(name, ".") == 0) { out[0] = 0; }
    else {
        os_err rc = to_wide(name, out, OS_NAME_MAX, &units);
        if (rc) return rc;
    }
    set_ustr(us, out, units);
    return OS_OK;
}
/* NtCreateFile: root NULL + NT name (path form) or a directory handle + a
 * relative name (*_at form). Handles are never inheritable. */
static NTSTATUS nt_open(HANDLE *out, HANDLE root, UNICODE_STRING *name, ACCESS_MASK access,
                        ULONG disposition, ULONG options, ULONG attrs)
{
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK io;
    InitializeObjectAttributes(&oa, name, OBJ_CASE_INSENSITIVE, root, NULL);
    *out = INVALID_HANDLE_VALUE;
    NTSTATUS s = NtCreateFile(out, access, &oa, &io, NULL, attrs, SHARE_ALL, disposition, options, NULL, 0);
    if (!NT_SUCCESS(s)) *out = INVALID_HANDLE_VALUE;
    return s;
}
static void close_keep(HANDLE h)
{
    DWORD e = GetLastError();
    if (h != INVALID_HANDLE_VALUE && h) CloseHandle(h);
    SetLastError(e);
}

/* ---- clocks, sleep ------------------------------------------------------ */
static uint64_t qpc_freq(void)
{
    immutable_ready();
    return (uint64_t)os_win_state.freq;
}
uint64_t os_now_ns(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    uint64_t f = qpc_freq(), t = (uint64_t)c.QuadPart;
    return (t / f) * NS_PER_S + (t % f) * NS_PER_S / f;   /* no 64-bit overflow */
}
static uint64_t filetime_100ns(FILETIME ft) { return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }
uint64_t os_thread_cpu_ns(void)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    return (filetime_100ns(k) + filetime_100ns(u)) * 100u;
}
/* Measured at first use: the smallest step GetThreadTimes moves by while this
 * thread spins, over 4 steps (P: 4 x 15.625 ms = 62.5 ms at the default
 * scheduler tick, the suite's own 40 ms spin sees at most 3). */
static BOOL CALLBACK cpu_resolution_init(PINIT_ONCE once, PVOID arg, PVOID *ctx)
{
    (void)once; (void)arg; (void)ctx;
    uint64_t best = UINT64_MAX, prev = os_thread_cpu_ns(), guard = os_now_ns() + NS_PER_S;
    for (int steps = 0; steps < 4 && os_now_ns() < guard;) {
        uint64_t c = os_thread_cpu_ns();
        if (c > prev) { if (c - prev < best) best = c - prev; prev = c; steps++; }
    }
    os_win_state.cpu_res = best == UINT64_MAX ? 100u : best;   /* 100 ns: one FILETIME unit, the floor */
    return TRUE;
}
uint64_t os_thread_cpu_resolution_ns(void)
{
    (void)InitOnceExecuteOnce(&kb.cpu_once, cpu_resolution_init, NULL, NULL);
    return kb.cpu_res;
}
uint64_t os_slice_ns(void) { return os_now_ns(); }   /* QPC: elapsed bounds CPU time from above (os.h) */
uint64_t os_wall_ns(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    int64_t units = (int64_t)filetime_100ns(ft) - EPOCH_1601_TO_1970;
    return units < 0 ? 0 : (uint64_t)units * 100u;
}
void os_sleep_ns(uint64_t ns)
{
    if (!ns) return;
    DWORD saved = GetLastError();
    uint64_t deadline = os_now_ns() + ns;
    HANDLE t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!t) t = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    /* Re-arm with the time left until QPC agrees the request has elapsed. */
    for (uint64_t now; (now = os_now_ns()) < deadline;) {
        uint64_t left = deadline - now;
        if (!t) { Sleep((DWORD)((left + 999999u) / 1000000u)); continue; }
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((left + 99u) / 100u);
        if (!SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE)) { Sleep(1); continue; }
        WaitForSingleObject(t, INFINITE);
    }
    if (t) CloseHandle(t);
    SetLastError(saved);
}
void os_yield(void) { (void)SwitchToThread(); }

/* ---- threads, mutex, condition, once ----------------------------------- */
static unsigned __stdcall thread_main(void *arg)
{
    os_thread *t = arg;
    t->fn(t->arg);
    return 0;
}
os_err os_thread_start(os_thread *t, void (*fn)(void *), void *arg)
{
    if (!t || !fn) return fail_with(OS_E_INVAL);
    t->fn = fn;
    t->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, thread_main, t, 0, NULL);   /* CRT-safe */
    t->handle = (void *)h;
    return h ? OS_OK : fail();
}
os_err os_thread_join(os_thread *t)
{
    if (!t->handle) return fail_with(OS_E_INVAL);
    DWORD w = WaitForSingleObject((HANDLE)t->handle, INFINITE);
    CloseHandle((HANDLE)t->handle);
    t->handle = NULL;
    return w == WAIT_OBJECT_0 ? OS_OK : fail();
}
uint64_t os_thread_self(void) { return GetCurrentThreadId(); }

_Static_assert(sizeof(SRWLOCK) <= sizeof(void *), "SRWLOCK fits os_mutex");
_Static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(void *), "CONDITION_VARIABLE fits os_cond");
_Static_assert(sizeof(INIT_ONCE) <= sizeof(void *), "INIT_ONCE fits os_once");
_Static_assert(sizeof(CRITICAL_SECTION) <= sizeof(((os_rmutex *)0)->cs), "CRITICAL_SECTION fits os_rmutex");
#define SRW(m) ((PSRWLOCK)&(m)->srw)
#define CV(c) ((PCONDITION_VARIABLE)&(c)->cv)
#define CS(m) ((CRITICAL_SECTION *)(m)->cs)

os_err os_mutex_init(os_mutex *m) { InitializeSRWLock(SRW(m)); return OS_OK; }
void os_mutex_destroy(os_mutex *m) { (void)m; }   /* SRW locks need no release */
void os_mutex_lock(os_mutex *m) { AcquireSRWLockExclusive(SRW(m)); }
void os_mutex_unlock(os_mutex *m) { ReleaseSRWLockExclusive(SRW(m)); }

/* No spin count: contention goes straight to the wait (no derived number). */
os_err os_rmutex_init(os_rmutex *m)
{
    return InitializeCriticalSectionEx(CS(m), 0, CRITICAL_SECTION_NO_DEBUG_INFO) ? OS_OK : fail();
}
void os_rmutex_destroy(os_rmutex *m) { DeleteCriticalSection(CS(m)); }
void os_rmutex_lock(os_rmutex *m) { EnterCriticalSection(CS(m)); }
void os_rmutex_unlock(os_rmutex *m) { LeaveCriticalSection(CS(m)); }

os_err os_cond_init(os_cond *c) { InitializeConditionVariable(CV(c)); return OS_OK; }
void os_cond_destroy(os_cond *c) { (void)c; }
void os_cond_wait(os_cond *c, os_mutex *m) { (void)SleepConditionVariableSRW(CV(c), SRW(m), INFINITE, 0); }
void os_cond_signal(os_cond *c) { WakeConditionVariable(CV(c)); }
void os_cond_broadcast(os_cond *c) { WakeAllConditionVariable(CV(c)); }

static BOOL CALLBACK once_tramp(PINIT_ONCE o, PVOID param, PVOID *ctx)
{
    (void)o; (void)ctx;
    ((void (*)(void))param)();
    return TRUE;
}
void os_once_run(os_once *o, void (*fn)(void))
{
    (void)InitOnceExecuteOnce((PINIT_ONCE)&o->init_once, once_tramp, (PVOID)fn, NULL);
}

/* ---- wake handles ------------------------------------------------------- */
os_err os_wake_init(os_wake *w)
{
    w->event = CreateEventW(NULL, TRUE, FALSE, NULL);   /* manual-reset, clear */
    return w->event ? OS_OK : fail();
}
void os_wake_close(os_wake *w)
{
    if (w->event) close_keep(w->event);
    w->event = NULL;
}
void os_wake_signal(const os_wake *w) { DWORD e = GetLastError(); SetEvent(w->event); SetLastError(e); }
void os_wake_drain(const os_wake *w) { DWORD e = GetLastError(); ResetEvent(w->event); SetLastError(e); }
/* One wait of at most timeout_ns on h; restarts with the time left. */
static os_err wait_handle(HANDLE h, int64_t timeout_ns)
{
    uint64_t deadline = timeout_ns > 0 ? os_now_ns() + (uint64_t)timeout_ns : 0;
    for (;;) {
        DWORD ms = INFINITE;
        if (timeout_ns == 0) ms = 0;
        else if (timeout_ns > 0) {
            uint64_t now = os_now_ns(), left = deadline > now ? deadline - now : 0;
            uint64_t up = (left + 999999u) / 1000000u;
            ms = up > (uint64_t)(INFINITE - 1) ? INFINITE - 1 : (DWORD)up;
        }
        DWORD r = WaitForSingleObject(h, ms);
        if (r == WAIT_OBJECT_0) return OS_OK;
        if (r != WAIT_TIMEOUT) return fail();
        if (timeout_ns == 0 || (timeout_ns > 0 && os_now_ns() >= deadline)) {
            SetLastError(WAIT_TIMEOUT);
            return OS_E_TIMEDOUT;
        }
    }
}
os_err os_wake_wait(const os_wake *w, int64_t timeout_ns) { return wait_handle(w->event, timeout_ns); }
void *os_wake_handle(const os_wake *w) { return w->event; }

/* ---- file metadata ------------------------------------------------------ */
bool os_identity_equal(const os_identity *a, const os_identity *b)
{
    return a->volume == b->volume && a->index[0] == b->index[0] && a->index[1] == b->index[1];
}
/* FILETIME (100 ns since 1601) -> signed ns since 1970, saturating (os.h). */
static int64_t ft_ns(LARGE_INTEGER t)
{
    int64_t units = t.QuadPart - EPOCH_1601_TO_1970;
    const int64_t lim = INT64_MAX / 100;
    if (units > lim) return INT64_MAX;
    if (units < -lim) return INT64_MIN;
    return units * 100;
}
static bool special_tag(DWORD tag)
{
    return tag == IO_REPARSE_TAG_AF_UNIX || tag == IO_REPARSE_TAG_LX_FIFO ||
           tag == IO_REPARSE_TAG_LX_CHR || tag == IO_REPARSE_TAG_LX_BLK;
}
static os_err identity_of(HANDLE h, os_identity *id)
{
    FILE_ID_INFO fi;
    memset(id, 0, sizeof *id);
    if (GetFileInformationByHandleEx(h, FileIdInfo, &fi, sizeof fi)) {
        id->volume = fi.VolumeSerialNumber;
        memcpy(&id->index[0], fi.FileId.Identifier, 8);
        memcpy(&id->index[1], fi.FileId.Identifier + 8, 8);
        return OS_OK;
    }
    /* Volumes without 128-bit IDs (FAT, some redirectors): the 64-bit index. */
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(h, &bi)) return fail();
    id->volume = bi.dwVolumeSerialNumber;
    id->index[0] = ((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
    return OS_OK;
}
/* Metadata of an open handle. A handle opened with FILE_OPEN_REPARSE_POINT
 * shows the reparse attribute of the name itself; one that followed shows
 * the target's. */
static os_err stat_handle(HANDLE h, os_stat *st)
{
    FILE_BASIC_INFO b;
    FILE_STANDARD_INFO s;
    FILE_ATTRIBUTE_TAG_INFO t;
    memset(st, 0, sizeof *st);
    if (!GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof b) ||
        !GetFileInformationByHandleEx(h, FileStandardInfo, &s, sizeof s)) return fail();
    os_err rc = identity_of(h, &st->id);
    if (rc) return rc;
    DWORD attrs = b.FileAttributes, tag = 0;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &t, sizeof t)) return fail();
        tag = t.ReparseTag;
    }
    if (tag && IsReparseTagNameSurrogate(tag)) st->kind = OS_KIND_LINK;
    else if (attrs & FILE_ATTRIBUTE_DIRECTORY) st->kind = OS_KIND_DIR;
    else if (tag && special_tag(tag)) st->kind = OS_KIND_OTHER;
    else if (GetFileType(h) != FILE_TYPE_DISK) st->kind = OS_KIND_OTHER;
    else st->kind = OS_KIND_FILE;
    st->size = s.EndOfFile.QuadPart < 0 ? 0 : (uint64_t)s.EndOfFile.QuadPart;
    st->mtime_ns = ft_ns(b.LastWriteTime);
    st->ctime_ns = ft_ns(b.ChangeTime);
    bool ro = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
    st->perm = st->kind == OS_KIND_DIR ? (ro ? 0555u : 0777u) : (ro ? 0444u : 0666u);
    return OS_OK;
}
/* The NOFOLLOW recipe (os.h): opened with FILE_OPEN_REPARSE_POINT, a name
 * surrogate is the answer (OS_E_LOOP for opens, OS_KIND_LINK for stats); any
 * other reparse tag is re-opened without the flag so its filter serves the
 * data, and the two opens must name the same file. Returns the handle to
 * use in *h (the second one when re-opened), or an error with *h closed. */
static os_err reparse_reopen(HANDLE *h, HANDLE root, UNICODE_STRING *name, ACCESS_MASK access,
                             ULONG options, bool for_open)
{
    FILE_ATTRIBUTE_TAG_INFO t;
    if (!GetFileInformationByHandleEx(*h, FileAttributeTagInfo, &t, sizeof t)) {
        os_err rc = fail(); close_keep(*h); *h = INVALID_HANDLE_VALUE; return rc;
    }
    if (!(t.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return OS_OK;
    if (IsReparseTagNameSurrogate(t.ReparseTag)) {
        if (!for_open) return OS_OK;   /* stat reports the link itself */
        close_keep(*h); *h = INVALID_HANDLE_VALUE;
        SetLastError(ERROR_STOPPED_ON_SYMLINK);
        return OS_E_LOOP;
    }
    /* (E) Three tries: a name replaced between the two opens is a race the
     * suite does not drive; after that the caller sees OS_E_BUSY. */
    for (int tries = 0; tries < 3; tries++) {
        os_identity a, b;
        HANDLE second;
        NTSTATUS s = nt_open(&second, root, name, access, FILE_OPEN,
                             options & ~(ULONG)FILE_OPEN_REPARSE_POINT, FILE_ATTRIBUTE_NORMAL);
        if (!NT_SUCCESS(s)) { os_err rc = fail_nt(s); close_keep(*h); *h = INVALID_HANDLE_VALUE; return rc; }
        if (identity_of(*h, &a) == OS_OK && identity_of(second, &b) == OS_OK && os_identity_equal(&a, &b)) {
            close_keep(*h);
            *h = second;
            return OS_OK;
        }
        close_keep(second);
    }
    close_keep(*h); *h = INVALID_HANDLE_VALUE;
    SetLastError(ERROR_SHARING_VIOLATION);
    return OS_E_BUSY;
}
static os_err stat_open(HANDLE root, UNICODE_STRING *name, unsigned flags, os_stat *st)
{
    ULONG options = FILE_SYNCHRONOUS_IO_NONALERT | ((flags & OS_STAT_NOFOLLOW) ? FILE_OPEN_REPARSE_POINT : 0);
    HANDLE h;
    NTSTATUS s = nt_open(&h, root, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN, options, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    os_err rc = OS_OK;
    if (flags & OS_STAT_NOFOLLOW)
        rc = reparse_reopen(&h, root, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE, options, false);
    if (rc == OS_OK) rc = stat_handle(h, st);
    close_keep(h);
    return rc;
}

/* ---- file handles ------------------------------------------------------- */
#define H(f) ((HANDLE)(f)->handle)
bool os_file_is_open(const os_file *f) { return f && f->handle && H(f) != INVALID_HANDLE_VALUE; }

static os_err open_file(os_file *out, HANDLE root, UNICODE_STRING *name, unsigned flags)
{
    out->handle = INVALID_HANDLE_VALUE;
    unsigned mode = flags & (OS_OPEN_READ | OS_OPEN_WRITE | OS_OPEN_RDWR);
    if (mode != OS_OPEN_READ && mode != OS_OPEN_WRITE && mode != OS_OPEN_RDWR) return fail_with(OS_E_INVAL);
    if ((flags & OS_OPEN_TRUNC) && mode == OS_OPEN_READ) return fail_with(OS_E_INVAL);
    ACCESS_MASK access = SYNCHRONIZE;
    if (mode != OS_OPEN_WRITE) access |= FILE_GENERIC_READ;
    if (mode != OS_OPEN_READ) access |= FILE_GENERIC_WRITE;
    bool nofollow = (flags & OS_OPEN_NOFOLLOW) != 0;
    if (nofollow) access |= FILE_READ_ATTRIBUTES;   /* required for tag inspection */
    ULONG disp;
    if (flags & OS_OPEN_CREATE) {
        if (flags & OS_OPEN_EXCL) disp = FILE_CREATE;
        else disp = !nofollow && (flags & OS_OPEN_TRUNC) ? FILE_OVERWRITE_IF : FILE_OPEN_IF;
    } else disp = !nofollow && (flags & OS_OPEN_TRUNC) ? FILE_OVERWRITE : FILE_OPEN;
    /* NOFOLLOW must open directories too: junctions are name surrogates,
     * and CREATE|EXCL must report a collision before any link inspection,
     * including when the caller otherwise allows following. */
    ULONG options = FILE_SYNCHRONOUS_IO_NONALERT |
                    (nofollow || disp == FILE_CREATE ? FILE_OPEN_REPARSE_POINT : FILE_NON_DIRECTORY_FILE);
    HANDLE h;
    NTSTATUS s = nt_open(&h, root, name, access, disp, options, FILE_ATTRIBUTE_NORMAL);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    /* FILE_CREATE success proves a new regular object, not a pre-existing
     * reparse point. Do not introduce post-create query/truncate failures that
     * would close the only owned handle and leave its name behind. */
    if (nofollow && disp != FILE_CREATE) {
        os_err rc = reparse_reopen(&h, root, name, access, options, true);
        if (rc) return rc;
        FILE_STANDARD_INFO info;
        if (!GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof info)) {
            rc = fail(); close_keep(h); return rc;
        }
        if (info.Directory) { close_keep(h); return fail_with(OS_E_ISDIR); }
        /* One EOF update on the validated data handle. No destructive bypass
         * open, and no second pathname resolution between validation/truncation. */
        if (flags & OS_OPEN_TRUNC) {
            FILE_END_OF_FILE_INFO eof;
            eof.EndOfFile.QuadPart = 0;
            if (!SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof eof)) {
                rc = fail(); close_keep(h); return rc;
            }
        }
    }
    out->handle = h;
    return OS_OK;
}
os_err os_file_open(os_file *out, const char *path, unsigned flags)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    out->handle = INVALID_HANDLE_VALUE;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : open_file(out, NULL, &us, flags);
}
os_err os_file_open_at(os_file *out, const os_dir *dir, const char *name, unsigned flags)
{
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    out->handle = INVALID_HANDLE_VALUE;
    os_err rc = rel_name(name, w, &us);
    return rc ? rc : open_file(out, dir->handle, &us, flags);
}
static os_err set_disposition(HANDLE h, ULONG flags)
{
    IO_STATUS_BLOCK io;
    nt_disposition_info di = {flags};
    NTSTATUS s = NtSetInformationFile(h, &io, &di, sizeof di, (FILE_INFORMATION_CLASS)NT_FILE_DISPOSITION_INFORMATION_EX);
    return NT_SUCCESS(s) ? OS_OK : fail_nt(s);
}
/* os.h recipe: data handle under a random name, then a second DELETE handle
 * takes POSIX disposition and closes, so the name is gone before return. */
os_err os_file_open_anon(os_file *out, const os_dir *dir)
{
    out->handle = INVALID_HANDLE_VALUE;
    uint64_t nonce = 0;
    os_err rc = os_random(&nonce, sizeof nonce);
    if (rc) return rc;
    char name[40];
    snprintf(name, sizeof name, ".anon-%016llx", (unsigned long long)nonce);
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    (void)rel_name(name, w, &us);
    HANDLE data, del;
    NTSTATUS s = nt_open(&data, dir->handle, &us, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE,
                         FILE_CREATE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, FILE_ATTRIBUTE_NORMAL);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    s = nt_open(&del, dir->handle, &us, DELETE, FILE_OPEN,
                 FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, 0);
    if (NT_SUCCESS(s)) {
        os_identity a, b;
        rc = identity_of(data, &a);
        if (!rc) rc = identity_of(del, &b);
        if (!rc && !os_identity_equal(&a, &b)) { SetLastError(ERROR_SHARING_VIOLATION); rc = OS_E_BUSY; }
        if (!rc) rc = set_disposition(del, NT_FILE_DISPOSITION_DELETE | NT_FILE_DISPOSITION_POSIX_SEMANTICS);
        close_keep(del);
    } else rc = fail_nt(s);
    if (rc) {
        /* Cleanup authority belongs to the created object even if its name
         * moved. Never unlink the entry found at the old random name. */
        DWORD e = GetLastError();
        (void)set_disposition(data, NT_FILE_DISPOSITION_DELETE | NT_FILE_DISPOSITION_POSIX_SEMANTICS);
        close_keep(data);
        SetLastError(e);
        return rc == OS_E_INVAL || rc == OS_E_NOTSUP ? OS_E_NOTSUP : rc;
    }
    out->handle = data;
    return OS_OK;
}
void os_file_close(os_file *f)
{
    if (os_file_is_open(f)) close_keep(H(f));
    if (f) f->handle = INVALID_HANDLE_VALUE;
}
os_err os_file_dup(os_file *out, const os_file *f)
{
    HANDLE h;
    out->handle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), H(f), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return fail();
    out->handle = h;
    return OS_OK;
}

/* One transfer on the synchronous handle; ov NULL = sequential position.
 * A pending completion (a filter answering ERROR_IO_PENDING) is waited for
 * before return (os.h: transfers are complete on return). */
static os_err transfer(HANDLE h, void *buf, size_t n, OVERLAPPED *ov, bool writing, size_t *moved)
{
    DWORD want = n > 0x7fffffffu ? 0x7fffffffu : (DWORD)n, got = 0;
    BOOL ok = writing ? WriteFile(h, buf, want, &got, ov) : ReadFile(h, buf, want, &got, ov);
    if (!ok) {
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) ok = GetOverlappedResult(h, ov, &got, TRUE);
        if (!ok) {
            e = GetLastError();
            if (!writing && (e == ERROR_HANDLE_EOF || e == ERROR_BROKEN_PIPE)) { *moved = 0; return OS_OK; }
            *moved = 0;
            return map_win(e);
        }
    }
    *moved = got;
    return OS_OK;
}
os_err os_file_read(os_file *f, void *buf, size_t n, size_t *got)
{
    *got = 0;
    return transfer(H(f), buf, n, NULL, false, got);
}
os_err os_file_write(os_file *f, const void *buf, size_t n, size_t *done)
{
    *done = 0;
    return transfer(H(f), (void *)(uintptr_t)buf, n, NULL, true, done);
}
static void at_offset(OVERLAPPED *ov, uint64_t offset)
{
    memset(ov, 0, sizeof *ov);
    ov->Offset = (DWORD)(offset & 0xffffffffu);
    ov->OffsetHigh = (DWORD)(offset >> 32);
}
os_err os_file_pread(os_file *f, void *buf, size_t n, uint64_t offset, size_t *got)
{
    OVERLAPPED ov;
    *got = 0;
    if (offset > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    at_offset(&ov, offset);
    return transfer(H(f), buf, n, &ov, false, got);
}
os_err os_file_pwrite(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done)
{
    OVERLAPPED ov;
    *done = 0;
    if (offset > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    at_offset(&ov, offset);
    return transfer(H(f), (void *)(uintptr_t)buf, n, &ov, true, done);
}
/* One WriteFile on the synchronous handle; Windows has no EINTR (os.h). */
os_err os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done)
{
    return os_file_pwrite(f, buf, n, offset, done);
}
/* Additional metadata access is requested only by the operation that needs
 * it, never as a precondition for a read/write-data open. ReOpenFile follows
 * the existing object rather than resolving its possibly replaced path. */
static os_err metadata_handle(HANDLE data, DWORD access, HANDLE *out)
{
    *out = ReOpenFile(data, access, SHARE_ALL, 0);
    if (*out == INVALID_HANDLE_VALUE) return fail();
    os_identity a, b;
    os_err rc = identity_of(data, &a);
    if (!rc) rc = identity_of(*out, &b);
    if (!rc && !os_identity_equal(&a, &b)) rc = fail_with(OS_E_BUSY);
    if (rc) { close_keep(*out); *out = INVALID_HANDLE_VALUE; }
    return rc;
}
os_err os_file_stat(const os_file *f, os_stat *st)
{
    os_err rc = stat_handle(H(f), st);
    if (rc != OS_E_ACCESS) return rc;
    HANDLE meta;
    rc = metadata_handle(H(f), FILE_READ_ATTRIBUTES, &meta);
    if (!rc) { rc = stat_handle(meta, st); close_keep(meta); }
    return rc;
}
os_err os_file_set_size(os_file *f, uint64_t size)
{
    if (size > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    FILE_END_OF_FILE_INFO eof;
    eof.EndOfFile.QuadPart = (LONGLONG)size;
    return SetFileInformationByHandle(H(f), FileEndOfFileInfo, &eof, sizeof eof) ? OS_OK : fail();
}
os_err os_file_set_sparse(os_file *f)
{
    DWORD n = 0;
    if (DeviceIoControl(H(f), FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &n, NULL)) return OS_OK;
    os_err rc = fail();
    return rc == OS_E_INVAL ? OS_E_NOTSUP : rc;   /* FAT, exFAT: no sparse files */
}
os_err os_file_sync(os_file *f) { return FlushFileBuffers(H(f)) ? OS_OK : fail(); }
os_err os_file_sync_data(os_file *f) { return FlushFileBuffers(H(f)) ? OS_OK : fail(); }
/* Only the owner-write bit has a Windows form: FILE_ATTRIBUTE_READONLY. */
os_err os_file_set_mode(os_file *f, uint32_t perm)
{
    HANDLE meta;
    os_err rc = metadata_handle(H(f), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, &meta);
    if (rc) return rc;
    FILE_BASIC_INFO b;
    if (!rc && !GetFileInformationByHandleEx(meta, FileBasicInfo, &b, sizeof b)) rc = fail();
    if (rc) { close_keep(meta); return rc; }
    DWORD attrs = b.FileAttributes;
    if (perm & 0200u) attrs &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    else attrs |= FILE_ATTRIBUTE_READONLY;
    attrs &= ~(DWORD)FILE_ATTRIBUTE_NORMAL;
    if (!attrs) attrs = FILE_ATTRIBUTE_NORMAL;
    memset(&b, 0, sizeof b);   /* zero times = unchanged */
    b.FileAttributes = attrs;
    rc = SetFileInformationByHandle(meta, FileBasicInfo, &b, sizeof b) ? OS_OK : fail();
    close_keep(meta);
    return rc;
}
/* No reflink on NTFS (os.h): the caller copies. */
os_err os_file_clone(os_file *dst, const os_file *src) { (void)dst; (void)src; return fail_with(OS_E_NOTSUP); }
static os_err fail_crt(int e)
{
    DWORD native;
    switch (e) {
    case ENOMEM: native = ERROR_NOT_ENOUGH_MEMORY; break;
    case EMFILE: native = ERROR_TOO_MANY_OPEN_FILES; break;
    case EACCES: native = ERROR_ACCESS_DENIED; break;
    case EBADF: native = ERROR_INVALID_HANDLE; break;
    case EINVAL: native = ERROR_INVALID_PARAMETER; break;
    case ENOSPC: native = ERROR_DISK_FULL; break;
    default: native = ERROR_GEN_FAILURE; break;
    }
    SetLastError(native);
    return map_win(native);
}
os_err os_file_to_stdio(os_file *f, const char *mode, FILE **out)
{
    *out = NULL;
    if (!mode || !os_file_is_open(f)) return fail_with(OS_E_INVAL);
    HANDLE dup;
    if (!DuplicateHandle(GetCurrentProcess(), H(f), GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) return fail();
    int oflags = _O_BINARY;
    if (mode[0] == 'r' && !strchr(mode, '+')) oflags |= _O_RDONLY;
    if (mode[0] == 'a') oflags |= _O_APPEND;
    int fd = _open_osfhandle((intptr_t)dup, oflags);
    if (fd < 0) { os_err rc = fail_crt(errno); close_keep(dup); return rc; }
    FILE *fp = _fdopen(fd, mode);
    if (!fp) {
        os_err rc = fail_crt(errno);
        DWORD e = GetLastError();
        (void)_close(fd);
        SetLastError(e);
        return rc;
    }
    *out = fp;
    os_file_close(f);
    return OS_OK;
}

/* Mandatory byte-range lock on one byte at LOCK_BYTE_OFFSET (os.h): it belongs
 * to the file object, which duplicates share, and the filesystem releases it
 * at the object's cleanup (last close) or process death. */
static os_err lock_handle(HANDLE h, os_lock kind)
{
    if (kind != OS_LOCK_SHARED && kind != OS_LOCK_EXCLUSIVE) return fail_with(OS_E_INVAL);
    OVERLAPPED ov;
    at_offset(&ov, LOCK_BYTE_OFFSET);
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY | (kind == OS_LOCK_EXCLUSIVE ? LOCKFILE_EXCLUSIVE_LOCK : 0);
    if (LockFileEx(h, flags, 0, 1, 0, &ov)) return OS_OK;
    DWORD e = GetLastError();
    return e == ERROR_LOCK_VIOLATION || e == ERROR_IO_PENDING ? (SetLastError(ERROR_LOCK_VIOLATION), OS_E_BUSY) : map_win(e);
}
static void unlock_handle(HANDLE h)
{
    DWORD e = GetLastError();
    OVERLAPPED ov;
    at_offset(&ov, LOCK_BYTE_OFFSET);
    (void)UnlockFileEx(h, 0, 1, 0, &ov);
    SetLastError(e);
}
os_err os_file_try_lock(os_file *f, os_lock kind) { return lock_handle(H(f), kind); }
void os_file_unlock(os_file *f) { unlock_handle(H(f)); }
void *os_file_handle(const os_file *f) { return f->handle; }

/* ---- directories -------------------------------------------------------- */
#define DH(d) ((HANDLE)(d)->handle)
bool os_dir_is_open(const os_dir *d) { return d && d->handle && DH(d) != INVALID_HANDLE_VALUE; }
static os_err open_dir(os_dir *out, HANDLE root, UNICODE_STRING *name)
{
    HANDLE h;
    NTSTATUS s = nt_open(&h, root, name, FILE_GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                         FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    out->handle = h;
    out->lock = INVALID_HANDLE_VALUE;
    return OS_OK;
}
os_err os_dir_open(os_dir *out, const char *path)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    out->handle = out->lock = INVALID_HANDLE_VALUE;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : open_dir(out, NULL, &us);
}
os_err os_dir_open_at(os_dir *out, const os_dir *dir, const char *name)
{
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    out->handle = out->lock = INVALID_HANDLE_VALUE;
    os_err rc = rel_name(name, w, &us);
    return rc ? rc : open_dir(out, dir->handle, &us);
}
void os_dir_close(os_dir *d)
{
    if (!d) return;
    if (d->lock && d->lock != INVALID_HANDLE_VALUE) close_keep(d->lock);   /* releases its lock */
    if (os_dir_is_open(d)) close_keep(DH(d));
    d->handle = d->lock = INVALID_HANDLE_VALUE;
}
/* Windows has no directory flush: the documented no-op (os.h, perf §2.8). */
os_err os_dir_sync(os_dir *d) { (void)d; return OS_OK; }
/* The identity-keyed lock file (os.h, decision doc §3.4/§8.2):
 * %LOCALAPPDATA%\sublimite\dirlocks\<volume>-<index1><index0>, created on
 * first use, shared READ|WRITE only (never renamed or deleted while held). */
static os_err dirlock_open(os_dir *d)
{
    os_identity id;
    os_err rc = identity_of(DH(d), &id);
    if (rc) return rc;
    char base[OS_PATH_MAX];
    rc = os_app_dir(OS_APP_DATA, base, sizeof base);
    if (rc) return rc;
    char path[OS_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s\\dirlocks", base);
    if (n < 0 || (size_t)n >= sizeof path) return fail_with(OS_E_NAMETOOLONG);
    /* Create both levels if missing (FILE_OPEN_IF on a directory). */
    for (int level = 0; level < 2; level++) {
        WCHAR w[OS_PATH_MAX];
        UNICODE_STRING us;
        rc = nt_path(level ? path : base, w, &us);
        if (rc) return rc;
        HANDLE h;
        NTSTATUS s = nt_open(&h, NULL, &us, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_IF,
                             FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, FILE_ATTRIBUTE_NORMAL);
        if (!NT_SUCCESS(s)) return fail_nt(s);
        close_keep(h);
    }
    n = snprintf(path, sizeof path, "%s\\dirlocks\\%016llx-%016llx%016llx", base,
                 (unsigned long long)id.volume, (unsigned long long)id.index[1],
                 (unsigned long long)id.index[0]);
    if (n < 0 || (size_t)n >= sizeof path) return fail_with(OS_E_NAMETOOLONG);
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    rc = nt_path(path, w, &us);
    if (rc) return rc;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK io;
    HANDLE h;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS s = NtCreateFile(&h, FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE, &oa, &io, NULL,
                              FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    d->lock = h;
    return OS_OK;
}
os_err os_dir_try_lock(os_dir *d, os_lock kind)
{
    if (kind != OS_LOCK_SHARED && kind != OS_LOCK_EXCLUSIVE) return fail_with(OS_E_INVAL);
    if (!d->lock || d->lock == INVALID_HANDLE_VALUE) {
        os_err rc = dirlock_open(d);
        if (rc) return rc;
    }
    return lock_handle(d->lock, kind);
}
void os_dir_unlock(os_dir *d)
{
    if (d->lock && d->lock != INVALID_HANDLE_VALUE) unlock_handle(d->lock);
}
void *os_dir_handle(const os_dir *d) { return d->handle; }

os_err os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st)
{
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    os_err rc = rel_name(name, w, &us);
    return rc ? rc : stat_open(dir->handle, &us, flags, st);
}
os_err os_stat_path(const char *path, unsigned flags, os_stat *st)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : stat_open(NULL, &us, flags, st);
}
static os_err mkdir_h(HANDLE root, UNICODE_STRING *name)
{
    HANDLE h;
    NTSTATUS s = nt_open(&h, root, name, FILE_LIST_DIRECTORY | SYNCHRONIZE, FILE_CREATE,
                         FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, FILE_ATTRIBUTE_NORMAL);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    close_keep(h);
    return OS_OK;
}
os_err os_mkdir_at(const os_dir *dir, const char *name)
{
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    os_err rc = rel_name(name, w, &us);
    return rc ? rc : mkdir_h(dir->handle, &us);
}
os_err os_mkdir(const char *path)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : mkdir_h(NULL, &us);
}
/* POSIX-semantics delete (os.h): the name leaves the namespace at once even
 * with the object open elsewhere; directory_file selects rmdir vs unlink. */
static os_err delete_h(HANDLE root, UNICODE_STRING *name, bool directory)
{
    HANDLE h;
    NTSTATUS s = nt_open(&h, root, name, DELETE | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
                         FILE_OPEN, FILE_OPEN_REPARSE_POINT, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    FILE_ATTRIBUTE_TAG_INFO info;
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof info)) {
        os_err rc = fail(); close_keep(h); return rc;
    }
    bool link = (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(info.ReparseTag);
    bool is_dir = !link && (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    if (directory != is_dir) { close_keep(h); return fail_with(directory ? OS_E_NOTDIR : OS_E_ISDIR); }
    ULONG flags = NT_FILE_DISPOSITION_DELETE | NT_FILE_DISPOSITION_POSIX_SEMANTICS |
                  (directory ? 0 : NT_FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE);
    os_err rc = set_disposition(h, flags);
    close_keep(h);
    return rc;
}
os_err os_rmdir(const char *path)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : delete_h(NULL, &us, true);
}
os_err os_unlink_at(const os_dir *dir, const char *name)
{
    WCHAR w[OS_NAME_MAX];
    UNICODE_STRING us;
    os_err rc = rel_name(name, w, &us);
    return rc ? rc : delete_h(dir->handle, &us, false);
}
os_err os_unlink(const char *path)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    os_err rc = nt_path(path, w, &us);
    return rc ? rc : delete_h(NULL, &us, false);
}
/* FileRenameInformationEx with POSIX semantics (os.h, decision doc §3.2). The
 * source is opened with DELETE on the name itself (reparse points are
 * renamed, not followed); the target is a name relative to to_root, or an
 * absolute NT name with a NULL root. */
static os_err rename_h(HANDLE from_root, UNICODE_STRING *from, HANDLE to_root, UNICODE_STRING *to,
                       os_rename_mode mode)
{
    if (mode != OS_RENAME_REPLACE && mode != OS_RENAME_NOREPLACE) return fail_with(OS_E_INVAL);
    HANDLE src;
    NTSTATUS s = nt_open(&src, from_root, from, DELETE | SYNCHRONIZE, FILE_OPEN,
                         FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    union { nt_rename_info ri; unsigned char raw[sizeof(nt_rename_info) + OS_PATH_MAX * sizeof(WCHAR)]; } u;
    memset(&u.ri, 0, sizeof u.ri);
    u.ri.u.Flags = NT_FILE_RENAME_POSIX_SEMANTICS;
    if (mode == OS_RENAME_REPLACE)
        u.ri.u.Flags |= NT_FILE_RENAME_REPLACE_IF_EXISTS | NT_FILE_RENAME_IGNORE_READONLY_ATTRIBUTE;
    u.ri.RootDirectory = to_root;
    u.ri.FileNameLength = to->Length;
    memcpy(u.ri.FileName, to->Buffer, to->Length);
    IO_STATUS_BLOCK io;
    /* The kernel rejects Length < sizeof(FILE_RENAME_INFORMATION) (24 on x64)
     * with STATUS_INFO_LENGTH_MISMATCH (M, probe: a one-unit name at
     * offsetof(FileName) + 2 = 22), so the struct's own size is the floor. */
    ULONG len = (ULONG)(sizeof(nt_rename_info) + to->Length);
    s = NtSetInformationFile(src, &io, &u.ri, len, (FILE_INFORMATION_CLASS)NT_FILE_RENAME_INFORMATION_EX);
    os_err rc = NT_SUCCESS(s) ? OS_OK : fail_nt(s);
    close_keep(src);
    return rc;
}
os_err os_rename_at(const os_dir *from_dir, const char *from, const os_dir *to_dir,
                    const char *to, os_rename_mode mode)
{
    WCHAR wf[OS_NAME_MAX], wt[OS_NAME_MAX];
    UNICODE_STRING uf, ut;
    os_err rc = rel_name(from, wf, &uf);
    if (!rc) rc = rel_name(to, wt, &ut);
    return rc ? rc : rename_h(from_dir->handle, &uf, to_dir->handle, &ut, mode);
}
os_err os_rename(const char *from, const char *to, os_rename_mode mode)
{
    WCHAR wf[OS_PATH_MAX], wt[OS_PATH_MAX];
    UNICODE_STRING uf, ut;
    os_err rc = nt_path(from, wf, &uf);
    if (!rc) rc = nt_path(to, wt, &ut);
    return rc ? rc : rename_h(NULL, &uf, NULL, &ut, mode);
}

/* Listing: a second handle to the same directory (relative open of the empty
 * name) so the cursor is the iterator's own. The record buffer is one heap
 * block (os.h lists os_dir_iter_open as allocating): (P) 64 KiB holds at
 * least 64 records of the largest size, 72 bytes of header + 255 UTF-16
 * units x 2 = 582 bytes, rounded to 8 = 584; 65536 / 584 = 112. */
#define ITER_BUF 65536u
os_err os_dir_iter_open(os_dir_iter *it, const os_dir *dir)
{
    WCHAR empty[1] = {0};
    UNICODE_STRING us;
    set_ustr(&us, empty, 0);
    memset(it, 0, sizeof *it);
    it->handle = INVALID_HANDLE_VALUE;
    HANDLE h;
    NTSTATUS s = nt_open(&h, dir->handle, &us, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                         FILE_OPEN, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    it->buffer = malloc(ITER_BUF);
    if (!it->buffer) { close_keep(h); return fail_with(OS_E_NOMEM); }
    it->handle = h;
    it->at = it->used = 0;
    return OS_OK;
}
os_err os_dir_next(os_dir_iter *it, const char **name)
{
    *name = NULL;
    for (;;) {
        if (it->at >= it->used) {
            /* used == SIZE_MAX: rewound, restart the enumeration. */
            FILE_INFO_BY_HANDLE_CLASS cls = it->used == SIZE_MAX ? FileFullDirectoryRestartInfo : FileFullDirectoryInfo;
            if (!GetFileInformationByHandleEx((HANDLE)it->handle, cls, it->buffer, ITER_BUF)) {
                it->at = it->used = 0;
                return GetLastError() == ERROR_NO_MORE_FILES ? OS_OK : fail();
            }
            it->at = 0;
            it->used = ITER_BUF;
        }
        FILE_FULL_DIR_INFO *e = (FILE_FULL_DIR_INFO *)((unsigned char *)it->buffer + it->at);
        it->at = e->NextEntryOffset ? it->at + e->NextEntryOffset : it->used;
        int units = (int)(e->FileNameLength / sizeof(WCHAR));
        if ((units == 1 && e->FileName[0] == L'.') ||
            (units == 2 && e->FileName[0] == L'.' && e->FileName[1] == L'.')) continue;
        os_err rc = to_utf8(e->FileName, units, it->name, sizeof it->name);
        if (rc) return rc;
        *name = it->name;
        return OS_OK;
    }
}
/* at == used == SIZE_MAX: the next os_dir_next restarts the enumeration. */
void os_dir_rewind(os_dir_iter *it) { it->at = SIZE_MAX; it->used = SIZE_MAX; }
void os_dir_iter_close(os_dir_iter *it)
{
    if (it->handle && it->handle != INVALID_HANDLE_VALUE) close_keep(it->handle);
    free(it->buffer);
    it->handle = INVALID_HANDLE_VALUE;
    it->buffer = NULL;
}

/* ---- paths and well-known directories ---------------------------------- */
static bool is_sep(char c) { return c == '\\' || c == '/'; }
bool os_path_is_absolute(const char *path)
{
    if (!path || !path[0]) return false;
    if (is_sep(path[0]) && is_sep(path[1])) return true;                 /* UNC, \\?\ */
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' && is_sep(path[2]);
}
/* GetFinalPathNameByHandleW gives \\?\C:\... or \\?\UNC\...: strip the prefix
 * back to the Win32 spelling (os.h). */
static os_err final_path(HANDLE h, char *out, size_t cap)
{
    WCHAR w[OS_PATH_MAX];
    DWORD n = GetFinalPathNameByHandleW(h, w, OS_PATH_MAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n) return fail();
    if (n >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    WCHAR *p = w;
    if (wcsncmp(p, L"\\\\?\\UNC\\", 8) == 0) { p += 6; p[0] = L'\\'; p[1] = L'\\'; }
    else if (wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;
    return to_utf8(p, -1, out, cap);
}
os_err os_path_canonical(const char *path, char *out, size_t cap)
{
    WCHAR w[OS_PATH_MAX];
    UNICODE_STRING us;
    os_err rc = nt_path(path, w, &us);
    if (rc) return rc;
    HANDLE h;
    NTSTATUS s = nt_open(&h, NULL, &us, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, 0);
    if (!NT_SUCCESS(s)) return fail_nt(s);
    rc = final_path(h, out, cap);
    close_keep(h);
    return rc;
}
os_err os_cwd(char *out, size_t cap)
{
    WCHAR w[OS_PATH_MAX];
    DWORD n = GetCurrentDirectoryW(OS_PATH_MAX, w);
    if (!n) return fail();
    if (n >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    return to_utf8(w, -1, out, cap);
}
/* Removes trailing separators (keeping a root) and appends suffix. */
static os_err root_join(char *out, size_t cap, const char *root, const char *suffix)
{
    size_t len = strlen(root), sl = strlen(suffix);
    while (len > 3 && is_sep(root[len - 1])) len--;
    if (!out || len >= cap || sl >= cap - len) return fail_with(OS_E_RANGE);
    memcpy(out, root, len);
    memcpy(out + len, suffix, sl + 1);
    return OS_OK;
}
static os_err known_folder(const GUID *id, const char *env, char *out, size_t cap)
{
    PWSTR w = NULL;
    if (SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, NULL, &w) == S_OK && w) {
        os_err rc = to_utf8(w, -1, out, cap);
        CoTaskMemFree(w);
        return rc;
    }
    return os_env_get(env, out, cap);   /* the same directories, from the environment */
}
os_err os_app_dir(os_app_dir_kind kind, char *out, size_t cap)
{
    /* FOLDERID_RoamingAppData and FOLDERID_LocalAppData (KnownFolders.h). */
    static const GUID roaming = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};
    static const GUID local = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
    char root[OS_PATH_MAX];
    os_err rc;
    if (kind == OS_APP_CONFIG) rc = known_folder(&roaming, "APPDATA", root, sizeof root);
    else if (kind == OS_APP_DATA) rc = known_folder(&local, "LOCALAPPDATA", root, sizeof root);
    else return fail_with(OS_E_INVAL);
    if (rc) return rc == OS_E_RANGE ? rc : fail_with(OS_E_NOENT);
    if (!os_path_is_absolute(root)) return fail_with(OS_E_NOENT);
    return root_join(out, cap, root, "\\sublimite");
}
os_err os_temp_dir(char *out, size_t cap)
{
    immutable_ready();
    WCHAR w[OS_PATH_MAX];
    DWORD n = os_win_state.temp2 ? os_win_state.temp2(OS_PATH_MAX, w) : GetTempPathW(OS_PATH_MAX, w);
    if (!n) return fail();
    if (n >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    while (n > 3 && (w[n - 1] == L'\\' || w[n - 1] == L'/')) w[--n] = 0;
    return to_utf8(w, -1, out, cap);
}
os_err os_env_get(const char *name, char *out, size_t cap)
{
    WCHAR wn[OS_NAME_MAX];
    os_err rc = to_wide(name, wn, OS_NAME_MAX, NULL);
    if (rc) return rc;
    /* (P) An environment value is at most 32767 UTF-16 units including the NUL. */
    WCHAR wv[32767];
    DWORD n = GetEnvironmentVariableW(wn, wv, 32767);
    if (!n) return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? fail_with(OS_E_NOENT) : fail();
    if (n >= 32767) return fail_with(OS_E_RANGE);
    return to_utf8(wv, -1, out, cap);
}
os_err os_env_set(const char *name, const char *value)
{
    WCHAR wn[OS_NAME_MAX], wv[32767];
    os_err rc = to_wide(name, wn, OS_NAME_MAX, NULL);
    if (rc) return rc;
    if (value) {
        rc = to_wide(value, wv, 32767, NULL);
        if (rc) return rc == OS_E_NAMETOOLONG ? fail_with(OS_E_RANGE) : rc;
    }
    if (SetEnvironmentVariableW(wn, value ? wv : NULL)) return OS_OK;
    if (!value && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return OS_OK;   /* unset of an unset name */
    return fail();
}

/* ---- memory ------------------------------------------------------------- */
static SYSTEM_INFO *sysinfo(void)
{
    immutable_ready();
    return &os_win_state.si;
}
size_t os_page_size(void) { return sysinfo()->dwPageSize; }
size_t os_map_granularity(void) { return sysinfo()->dwAllocationGranularity; }
uint64_t os_phys_mem_bytes(void)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    return GlobalMemoryStatusEx(&ms) ? ms.ullTotalPhys : 0;
}
static size_t round_page(size_t n) { size_t p = os_page_size(); return (n + p - 1) & ~(p - 1); }
/* The private section field points at this VM descriptor (no CRT heap).
 * The descriptor owns the native section and at most two address pieces:
 * a file prefix and a zero suffix, or their placeholders after a failed step.
 * Registration pins this descriptor; duplicate registrations share its lock
 * and epoch. It survives retirement until os_unmap, so cleanup needs no
 * registry lookup and never queries an address it has already released. */
typedef enum map_piece_kind { PIECE_NONE, PIECE_FILE, PIECE_ZERO, PIECE_PLACEHOLDER } map_piece_kind;
typedef struct map_piece {
    void *addr;
    size_t len;
    map_piece_kind kind;
} map_piece;
typedef struct map_owner {
    HANDLE section;
    uintptr_t addr;
    size_t len, extent;
    uint64_t offset;
    map_piece pieces[2];
    _Atomic int busy;
    _Atomic uint64_t epoch;
    bool failed;
} map_owner;

/* Preserve the native acquisition error unless the same handle proves that
 * the requested range disappeared after the first size query (§9.2). */
static os_err map_acquire_error(const os_file *f, uint64_t end)
{
    DWORD e = GetLastError();
    LARGE_INTEGER size;
    if (GetFileSizeEx(H(f), &size) && (size.QuadPart < 0 || end > (uint64_t)size.QuadPart)) {
        SetLastError(e);
        return OS_E_RANGE;
    }
    SetLastError(e);
    return map_win(e);
}
/* os.h recipe: section + page-rounded placeholder + read-only view. */
os_err os_map_file(os_map *out, const os_file *f, uint64_t offset, size_t len)
{
    out->addr = NULL;
    out->len = 0;
    out->section = NULL;
    out->offset = 0;
    if (!len || offset % os_map_granularity() || offset > (uint64_t)INT64_MAX - len)
        return fail_with(OS_E_INVAL);
    LARGE_INTEGER size;
    if (!GetFileSizeEx(H(f), &size)) return fail();
    if (size.QuadPart < 0 || offset + len > (uint64_t)size.QuadPart) return fail_with(OS_E_RANGE);
    if (!placeholders_ok()) return fail_with(OS_E_NOTSUP);
    HANDLE sec = CreateFileMappingW(H(f), NULL, PAGE_READONLY, 0, 0, NULL);
    if (!sec) return map_acquire_error(f, offset + len);
    size_t extent = round_page(len);
    map_owner *owner = VirtualAlloc(NULL, sizeof *owner, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!owner) { os_err rc = fail(); close_keep(sec); return rc; }
    void *base = kb.valloc2(GetCurrentProcess(), NULL, extent, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                           PAGE_NOACCESS, NULL, 0);
    if (!base) { os_err rc = fail(); VirtualFree(owner, 0, MEM_RELEASE); close_keep(sec); return rc; }
    /* Logical ViewSize can be shorter than a page at EOF (frozen map case). */
    void *view = kb.mapview3(sec, GetCurrentProcess(), base, offset, len, MEM_REPLACE_PLACEHOLDER,
                             PAGE_READONLY, NULL, 0);
    if (!view) {
        os_err rc = map_acquire_error(f, offset + len);
        VirtualFree(base, 0, MEM_RELEASE);
        VirtualFree(owner, 0, MEM_RELEASE);
        close_keep(sec);
        return rc;
    }
    owner->section = sec;
    owner->addr = (uintptr_t)view;
    owner->len = len;
    owner->extent = extent;
    owner->offset = offset;
    owner->pieces[0] = (map_piece){view, extent, PIECE_FILE};
    atomic_init(&owner->busy, 0);
    atomic_init(&owner->epoch, 0);
    out->addr = view;
    out->len = len;
    out->section = owner;
    out->offset = offset;
    return OS_OK;
}
void os_unmap(os_map *m)
{
    if (!m->addr) return;
    DWORD e = GetLastError();
    map_owner *owner = m->section;
    for (size_t i = 0; i < sizeof owner->pieces / sizeof owner->pieces[0]; i++) {
        map_piece piece = owner->pieces[i];
        if (piece.kind == PIECE_FILE) (void)UnmapViewOfFile(piece.addr);
        else if (piece.kind != PIECE_NONE) (void)VirtualFree(piece.addr, 0, MEM_RELEASE);
    }
    CloseHandle(owner->section);
    VirtualFree(owner, 0, MEM_RELEASE);
    SetLastError(e);
    m->addr = NULL;
    m->len = 0;
    m->section = NULL;
}
void os_map_prefetch(const os_map *m, size_t off, size_t len)
{
    if (!m->addr || off >= m->len) return;
    if (len > m->len - off) len = m->len - off;
    WIN32_MEMORY_RANGE_ENTRY r = {(PVOID)((uintptr_t)m->addr + off), len};
    DWORD e = GetLastError();
    (void)PrefetchVirtualMemory(GetCurrentProcess(), 1, &r, 0);
    SetLastError(e);
}
static bool page_range(const void *addr, size_t len)
{
    size_t page = os_page_size();
    return addr && len && ((uintptr_t)addr % page) == 0 && len % page == 0;
}
os_err os_vm_reserve(void **out, size_t len)
{
    *out = NULL;
    if (!len || len % os_page_size()) return fail_with(OS_E_INVAL);
    void *p = VirtualAlloc(NULL, len, MEM_RESERVE, PAGE_READWRITE);
    if (!p) return fail();
    *out = p;
    return OS_OK;
}
os_err os_vm_commit(void *addr, size_t len)
{
    if (!page_range(addr, len)) return fail_with(OS_E_INVAL);
    return VirtualAlloc(addr, len, MEM_COMMIT, PAGE_READWRITE) ? OS_OK : fail();
}
os_err os_vm_decommit(void *addr, size_t len)
{
    if (!page_range(addr, len)) return fail_with(OS_E_INVAL);
    return VirtualFree(addr, len, MEM_DECOMMIT) ? OS_OK : fail();
}
void os_vm_release(void *addr, size_t len)
{
    if (addr && len) (void)VirtualFree(addr, 0, MEM_RELEASE);
}

/* ---- fault service ------------------------------------------------------ */
/* The file module's slot algorithm (os_posix.c, P1.7c §§1, 2, 9) under a
 * vectored exception handler. EXCEPTION_IN_PAGE_ERROR inside a LIVE slot:
 * count the epoch, then split the view at the faulting page (the os.h
 * recipe) with the view marked busy; a read access violation is retried only
 * after serialization and verification of the current owned readable piece,
 * including dispatch delayed until after a split. The one mapped piece of a recovered view is always its
 * prefix, so a remap reads from the view's own offset. Registered LAST
 * (AddVectoredExceptionHandler first = 0): AddressSanitizer commits its
 * shadow pages from a vectored handler of its own, and this handler is
 * instrumented, so running ahead of it faults inside exception dispatch and
 * the process dies with the original code (M, edit-177r.3: the sanitizer
 * build crashed at exit with 0xC0000005 whenever the service was installed
 * first). ASan's fatal report is an unhandled-exception filter, after every
 * vectored handler, so recoverable faults still reach this one; faults
 * outside every slot continue the search. */
#define SLOT_FREE 0u
#define SLOT_RESERVED 1u
#define SLOT_LIVE 2u
#define SLOT_RETIRING 3u
#define SLOT_PHASE 3u
#define SLOT_READER 4u
typedef struct fault_slot {
    _Atomic uint32_t state;
    map_owner *owner;
} fault_slot;
typedef struct fault_service {
    fault_slot slots[OS_FAULT_SLOTS];
    INIT_ONCE once;
    uintptr_t page_size;
    void *handler;
    int installed;
} fault_service;
static fault_service os_fault_service;

static bool piece_readable(const map_piece *piece, uintptr_t a)
{
    if (piece->kind != PIECE_FILE && piece->kind != PIECE_ZERO) return false;
    if (a < (uintptr_t)piece->addr || a - (uintptr_t)piece->addr >= piece->len) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)a, &mbi, sizeof mbi) || mbi.AllocationBase != piece->addr ||
        mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    if (mbi.Type != (piece->kind == PIECE_FILE ? MEM_MAPPED : MEM_PRIVATE)) return false;
    return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
static bool owned_readable(map_owner *owner, uintptr_t a)
{
    return !owner->failed && (piece_readable(&owner->pieces[0], a) || piece_readable(&owner->pieces[1], a));
}
/* Convert one *owned* allocation to a placeholder and record every successful
 * topology change before attempting the next fallible native call. */
static bool preserve_piece(map_piece *piece)
{
    if (piece->kind == PIECE_NONE || piece->kind == PIECE_PLACEHOLDER) return true;
    BOOL ok = piece->kind == PIECE_FILE
        ? kb.unmap2(GetCurrentProcess(), piece->addr, MEM_PRESERVE_PLACEHOLDER)
        : VirtualFree(piece->addr, piece->len, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
    if (ok) piece->kind = PIECE_PLACEHOLDER;
    return ok != FALSE;
}
static bool split_view(map_owner *owner, uintptr_t from)
{
    if (owner->failed) return false;
    map_piece *prefix = &owner->pieces[0], *suffix = &owner->pieces[1];
    /* A pagefile failure in an existing zero range needs new pages, not an
     * endless retry of the page that failed. Keep the reservation owned. */
    map_piece *zero = prefix->kind == PIECE_ZERO ? prefix : suffix;
    if (zero->kind == PIECE_ZERO && from >= (uintptr_t)zero->addr && from - (uintptr_t)zero->addr < zero->len) {
        if (!piece_readable(zero, from)) goto failed;
        if (!VirtualFree(zero->addr, zero->len, MEM_DECOMMIT)) goto failed;
        if (VirtualAlloc(zero->addr, zero->len, MEM_COMMIT, PAGE_READONLY)) return true;
        /* The reservation still belongs to us even when commit failed. */
        owner->failed = true;
        return false;
    }
    if (prefix->kind != PIECE_FILE || !piece_readable(prefix, from)) goto failed;
    if (!preserve_piece(prefix) || !preserve_piece(suffix)) goto failed;
    if (suffix->kind != PIECE_NONE) {
        if (!VirtualFree(prefix->addr, owner->extent, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) goto failed;
        prefix->len = owner->extent;
        *suffix = (map_piece){0};
    }
    size_t before = from - owner->addr;
    if (before) {
        if (!VirtualFree(prefix->addr, before, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) goto failed;
        prefix->len = before;
        *suffix = (map_piece){(void *)from, owner->extent - before, PIECE_PLACEHOLDER};
        if (!kb.mapview3(owner->section, GetCurrentProcess(), prefix->addr, owner->offset, before,
                          MEM_REPLACE_PLACEHOLDER, PAGE_READONLY, NULL, 0)) goto failed;
        prefix->kind = PIECE_FILE;
        zero = suffix;
    } else zero = prefix;
    if (!kb.valloc2(GetCurrentProcess(), zero->addr, zero->len, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                     PAGE_READONLY, NULL, 0)) goto failed;
    zero->kind = PIECE_ZERO;
    return true;
failed:
    owner->failed = true;
    return false;
}
static int fault_recover(uintptr_t a, bool in_page)
{
    for (int i = 0; i < OS_FAULT_SLOTS; i++) {
        fault_slot *slot = &os_fault_service.slots[i];
        uint32_t state = atomic_load_explicit(&slot->state, memory_order_acquire);
        for (;;) {
            if ((state & SLOT_PHASE) != SLOT_LIVE || state > UINT32_MAX - SLOT_READER) break;
            if (atomic_compare_exchange_weak_explicit(&slot->state, &state, state + SLOT_READER,
                                                     memory_order_acquire, memory_order_relaxed)) {
                map_owner *owner = slot->owner;
                if (a < owner->addr || a - owner->addr >= owner->extent) {
                    atomic_fetch_sub_explicit(&slot->state, SLOT_READER, memory_order_release);
                    break;
                }
                OS_WIN_TEST_HOOK("admitted", owner);
                int zero = 0;
                while (!atomic_compare_exchange_weak_explicit(&owner->busy, &zero, 1, memory_order_acquire,
                                                               memory_order_relaxed)) {
                    zero = 0;
                    SwitchToThread();
                }
                int recovered;
                if (!in_page) {
                    /* Dispatch can be delayed until AFTER a split clears busy.
                     * Retry only a read whose address is now owned/readable. */
                    recovered = owned_readable(owner, a);
                } else {
                    uintptr_t from = a & ~(os_fault_service.page_size - 1u);
                    atomic_fetch_add_explicit(&owner->epoch, 1, memory_order_release);
                    recovered = split_view(owner, from);
                }
                atomic_store_explicit(&owner->busy, 0, memory_order_release);
                atomic_fetch_sub_explicit(&slot->state, SLOT_READER, memory_order_release);
                return recovered;
            }
        }
    }
    return 0;
}
static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if ((r->ExceptionCode != EXCEPTION_IN_PAGE_ERROR && r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) ||
        r->NumberParameters < 2 || r->ExceptionInformation[0] != 0) return EXCEPTION_CONTINUE_SEARCH;
    DWORD saved = GetLastError();
    int recovered = fault_recover((uintptr_t)r->ExceptionInformation[1], r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR);
    SetLastError(saved);
    return recovered ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
static BOOL CALLBACK fault_install(PINIT_ONCE o, PVOID param, PVOID *ctx)
{
    (void)o; (void)param; (void)ctx;
    uintptr_t page = os_page_size();
    if (!page || (page & (page - 1u))) return TRUE;
    for (int i = 0; i < OS_FAULT_SLOTS; i++) {
        if (!atomic_is_lock_free(&os_fault_service.slots[i].state)) return TRUE;
    }
    os_fault_service.page_size = page;
    os_fault_service.handler = AddVectoredExceptionHandler(0, fault_handler);
    os_fault_service.installed = os_fault_service.handler != NULL;
    return TRUE;
}
os_err os_fault_register(const os_map *m, int *slot)
{
    *slot = -1;
    (void)InitOnceExecuteOnce(&os_fault_service.once, fault_install, NULL, NULL);
    if (!os_fault_service.installed) return fail_with(OS_E_NOTSUP);
    uintptr_t addr = (uintptr_t)m->addr;
    if (!addr || !m->len || (addr & (os_fault_service.page_size - 1u)) != 0 ||
        m->len > UINTPTR_MAX - addr || !m->section) return fail_with(OS_E_INVAL);
    map_owner *owner = m->section;
    if (!atomic_is_lock_free(&owner->busy) || !atomic_is_lock_free(&owner->epoch)) return fail_with(OS_E_NOTSUP);
    for (int i = 0; i < OS_FAULT_SLOTS; i++) {
        fault_slot *s = &os_fault_service.slots[i];
        uint32_t free_state = SLOT_FREE;
        if (atomic_compare_exchange_strong_explicit(&s->state, &free_state, SLOT_RESERVED,
                                                    memory_order_acquire, memory_order_relaxed)) {
            s->owner = m->section;
            atomic_store_explicit(&s->state, SLOT_LIVE, memory_order_release);
            *slot = i;
            return OS_OK;
        }
    }
    return fail_with(OS_E_NOSPC);
}
uint64_t os_fault_epoch(int slot)
{
    if (slot < 0 || slot >= OS_FAULT_SLOTS) return 0;
    fault_slot *s = &os_fault_service.slots[slot];
    if ((atomic_load_explicit(&s->state, memory_order_acquire) & SLOT_PHASE) != SLOT_LIVE) return 0;
    return atomic_load_explicit(&s->owner->epoch, memory_order_acquire);
}
void os_fault_retire(int slot)
{
    if (slot < 0 || slot >= OS_FAULT_SLOTS) return;
    fault_slot *s = &os_fault_service.slots[slot];
    uint32_t state = atomic_load_explicit(&s->state, memory_order_acquire);
    for (;;) {
        if ((state & SLOT_PHASE) == SLOT_FREE) return;
        if ((state & SLOT_PHASE) == SLOT_RESERVED) break;
        uint32_t retiring = (state & ~SLOT_PHASE) | SLOT_RETIRING;
        if (atomic_compare_exchange_weak_explicit(&s->state, &state, retiring,
                                                  memory_order_acq_rel, memory_order_acquire)) {
            OS_WIN_TEST_HOOK("retiring", s);
            while (atomic_load_explicit(&s->state, memory_order_acquire) != SLOT_RETIRING) SwitchToThread();
            break;
        }
    }
    atomic_store_explicit(&s->state, SLOT_FREE, memory_order_release);
}
/* Test only: the native lost-backing exception for one byte, raised on this
 * thread into the vectored handler (os.h); continuable, so a recovered fault
 * returns here. Unhandled, it takes the chain like a real fault. */
os_err os_fault_test_trigger(const os_map *m, size_t off)
{
    if (!m->addr || off >= m->len) return fail_with(OS_E_INVAL);
    ULONG_PTR args[3] = {0, (ULONG_PTR)m->addr + off, (ULONG_PTR)(ULONG)STATUS_DEVICE_DATA_ERROR};
    RaiseException(EXCEPTION_IN_PAGE_ERROR, 0, 3, args);
    return OS_OK;
}

/* ---- random ------------------------------------------------------------- */
os_err os_random(void *buf, size_t n)
{
    while (n) {
        ULONG chunk = n > 0x7fffffffu ? 0x7fffffffu : (ULONG)n;
        NTSTATUS s = BCryptGenRandom(NULL, buf, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(s)) return fail_nt(s);
        buf = (unsigned char *)buf + chunk;
        n -= chunk;
    }
    return OS_OK;
}

/* ---- processes ---------------------------------------------------------- */
/* Appends arg to the command line by the CommandLineToArgvW rules: quoted
 * when empty or containing a blank, tab or quote; backslashes before a quote
 * (or the closing quote) doubled, a quote escaped. Returns units written, or
 * the units needed when out is NULL. */
static size_t quote_arg(const WCHAR *arg, WCHAR *out)
{
    size_t n = 0;
    bool need = arg[0] == 0 || wcspbrk(arg, L" \t\"") != NULL;
#define PUT(c) do { if (out) out[n] = (c); n++; } while (0)
    if (!need) { for (const WCHAR *p = arg; *p; p++) PUT(*p); return n; }
    PUT(L'"');
    for (const WCHAR *p = arg; *p;) {
        size_t bs = 0;
        while (*p == L'\\') { bs++; p++; }
        if (*p == 0) { for (size_t i = 0; i < 2 * bs; i++) PUT(L'\\'); break; }
        if (*p == L'"') { for (size_t i = 0; i < 2 * bs + 1; i++) PUT(L'\\'); PUT(L'"'); }
        else { for (size_t i = 0; i < bs; i++) PUT(L'\\'); PUT(*p); }
        p++;
    }
    PUT(L'"');
#undef PUT
    return n;
}
os_err os_proc_spawn(os_proc *out, const char *const argv[])
{
    out->process = NULL;
    out->id = 0;
    out->killed = false;
    if (!argv || !argv[0]) return fail_with(OS_E_INVAL);
    WCHAR app[OS_PATH_MAX];
    os_err rc = to_wide(argv[0], app, OS_PATH_MAX, NULL);
    if (rc) return rc;
    /* Command line: each argument converted on the stack, quoted into one
     * heap block (os.h: may allocate). */
    size_t argc = 0, units = 1;
    for (; argv[argc]; argc++) {
        WCHAR w[OS_PATH_MAX];
        rc = to_wide(argv[argc], w, OS_PATH_MAX, NULL);
        if (rc) return rc;
        units += quote_arg(w, NULL) + 1;
    }
    WCHAR *cmd = malloc(units * sizeof(WCHAR));
    if (!cmd) return fail_with(OS_E_NOMEM);
    size_t at = 0;
    for (size_t i = 0; i < argc; i++) {
        WCHAR w[OS_PATH_MAX];
        (void)to_wide(argv[i], w, OS_PATH_MAX, NULL);
        if (i) cmd[at++] = L' ';
        at += quote_arg(w, cmd + at);
    }
    cmd[at] = 0;
    /* Only inheritable duplicates of the three standard handles reach the
     * child (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), nothing else. */
    HANDLE std[3] = {GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
    HANDLE dup[3] = {NULL, NULL, NULL}, list[3];
    DWORD nlist = 0, error = ERROR_SUCCESS;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    bool initialized = false;
    BOOL ok = FALSE;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    for (int i = 0; i < 3; i++) {
        if (!std[i] || std[i] == INVALID_HANDLE_VALUE) continue;
        if (!DuplicateHandle(GetCurrentProcess(), std[i], GetCurrentProcess(), &dup[i], 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            error = GetLastError(); goto cleanup;
        }
        list[nlist++] = dup[i];
    }
    SIZE_T attr_size = 0;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    if (!attr_size) { error = GetLastError(); goto cleanup; }
    attrs = malloc(attr_size);
    if (!attrs) { error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) { error = GetLastError(); goto cleanup; }
    initialized = true;
    if (nlist && !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list,
                                           nlist * sizeof(HANDLE), NULL, NULL)) {
        error = GetLastError(); goto cleanup;
    }
    STARTUPINFOEXW si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = dup[0];
    si.StartupInfo.hStdOutput = dup[1];
    si.StartupInfo.hStdError = dup[2];
    si.lpAttributeList = attrs;
    ok = CreateProcessW(app, cmd, NULL, NULL, nlist > 0, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                         &si.StartupInfo, &pi);
    if (!ok) error = GetLastError();
cleanup:
    if (initialized) DeleteProcThreadAttributeList(attrs);
    free(attrs);
    for (int i = 0; i < 3; i++) if (dup[i]) CloseHandle(dup[i]);
    free(cmd);
    if (!ok) { SetLastError(error); return fail(); }
    CloseHandle(pi.hThread);
    out->process = pi.hProcess;
    out->id = pi.dwProcessId;
    return OS_OK;
}
os_err os_proc_kill(os_proc *p)
{
    if (!p->process) return fail_with(OS_E_INVAL);
    p->killed = true;
    /* STATUS_CONTROL_C_EXIT, an NTSTATUS exit code, so even a reader without
     * the killed flag sees an abnormal end. */
    if (TerminateProcess((HANDLE)p->process, 0xC000013Au)) return OS_OK;
    return GetLastError() == ERROR_ACCESS_DENIED ? OS_OK : fail();   /* already gone */
}
os_err os_proc_wait(os_proc *p, int64_t timeout_ns, os_proc_status *st)
{
    if (!p->process) return fail_with(OS_E_INVAL);
    os_err rc = wait_handle((HANDLE)p->process, timeout_ns);
    if (rc) return rc;
    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)p->process, &code)) return fail();
    CloseHandle((HANDLE)p->process);
    p->process = NULL;
    if (p->killed || code >= 0xC0000000u) { st->how = OS_PROC_ABNORMAL; st->code = (int)code; }
    else { st->how = OS_PROC_EXITED; st->code = (int)code; }
    return OS_OK;
}
uint64_t os_proc_id(void) { return GetCurrentProcessId(); }
os_err os_proc_self_path(char *out, size_t cap)
{
    WCHAR w[OS_PATH_MAX];
    DWORD n = GetModuleFileNameW(NULL, w, OS_PATH_MAX);
    if (!n) return fail();
    if (n >= OS_PATH_MAX) return fail_with(OS_E_NAMETOOLONG);
    return to_utf8(w, -1, out, cap);
}

#endif /* _WIN32 */
