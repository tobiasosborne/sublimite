/* Windows-only regressions for edit-177r.3 round 2. Include the implementation
 * in this test TU so deterministic native-API shims never enter normal builds.
 * Every case runs in a fresh supervised process, including cold-cache races.
 * Guards match tests/os_test.c's estimated hang guards; no timing is a gate. */
#if defined(_WIN32)
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winternl.h>
#include <winioctl.h>
#include <sddl.h>
#include <aclapi.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "os/os.h"

static void os_win_test_hook(const char *event, void *context);
static BOOL WINAPI os_win_test_unmap(LPCVOID p);
static BOOL WINAPI os_win_test_free(LPVOID p, SIZE_T n, DWORD flags);
static LPVOID WINAPI os_win_test_vm_alloc(LPVOID p, SIZE_T n, DWORD flags, DWORD prot);
static void WINAPI os_win_test_sysinfo(LPSYSTEM_INFO s);
static BOOL WINAPI os_win_test_freq(LARGE_INTEGER *f);
static HANDLE WINAPI os_win_test_section(HANDLE f, LPSECURITY_ATTRIBUTES a, DWORD prot, DWORD hi, DWORD lo, LPCWSTR name);
static FARPROC WINAPI os_win_test_proc(HMODULE m, LPCSTR name);
static NTSTATUS NTAPI os_win_test_open(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io,
                                     PLARGE_INTEGER size, ULONG attrs, ULONG share, ULONG disp, ULONG opts, PVOID ea, ULONG ealen);
static BOOL WINAPI os_win_test_info(HANDLE h, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID out, DWORD n);
static FILE *os_win_test_fdopen(int fd, const char *mode);
static void *os_win_test_malloc(size_t n);
static BOOL WINAPI os_win_test_dup(HANDLE a, HANDLE b, HANDLE c, LPHANDLE d, DWORD e, BOOL f, DWORD g);
static BOOL WINAPI os_win_test_attrs(LPPROC_THREAD_ATTRIBUTE_LIST a, DWORD b, DWORD c, PSIZE_T d);
static void WINAPI os_win_test_delattrs(LPPROC_THREAD_ATTRIBUTE_LIST a);
static BOOL WINAPI os_win_test_update(LPPROC_THREAD_ATTRIBUTE_LIST a, DWORD b, DWORD_PTR c, PVOID d, SIZE_T e, PVOID f, PSIZE_T g);
static BOOL WINAPI os_win_test_create(LPCWSTR a, LPWSTR b, LPSECURITY_ATTRIBUTES c, LPSECURITY_ATTRIBUTES d,
                                    BOOL e, DWORD f, LPVOID g, LPCWSTR h, LPSTARTUPINFOW i, LPPROCESS_INFORMATION j);
#define OS_WIN_TEST_HOOK(event, context) os_win_test_hook(event, context)
#define UnmapViewOfFile os_win_test_unmap
#define VirtualFree os_win_test_free
#define VirtualAlloc os_win_test_vm_alloc
#define GetSystemInfo os_win_test_sysinfo
#define QueryPerformanceFrequency os_win_test_freq
#define CreateFileMappingW os_win_test_section
#define GetProcAddress os_win_test_proc
#define NtCreateFile os_win_test_open
#define GetFileInformationByHandleEx os_win_test_info
#define _fdopen os_win_test_fdopen
#define malloc os_win_test_malloc
#define DuplicateHandle os_win_test_dup
#define InitializeProcThreadAttributeList os_win_test_attrs
#define DeleteProcThreadAttributeList os_win_test_delattrs
#define UpdateProcThreadAttribute os_win_test_update
#define CreateProcessW os_win_test_create
#ifndef OS_WIN_TEST_SOURCE
#define OS_WIN_TEST_SOURCE "../../src/os/os_win.c"
#endif
#include OS_WIN_TEST_SOURCE
#undef UnmapViewOfFile
#undef VirtualFree
#undef VirtualAlloc
#undef GetSystemInfo
#undef QueryPerformanceFrequency
#undef CreateFileMappingW
#undef GetProcAddress
#undef NtCreateFile
#undef GetFileInformationByHandleEx
#undef _fdopen
#undef malloc
#undef DuplicateHandle
#undef InitializeProcThreadAttributeList
#undef DeleteProcThreadAttributeList
#undef UpdateProcThreadAttribute
#undef CreateProcessW

/* One test object; inter-thread control uses events or atomics. */
static struct {
    _Atomic int failures;
    char root[OS_PATH_MAX];
    os_dir dir;
    HANDLE entered, resume, done, split_entered, split_resume;
    void *view, *interloper;
    size_t extent;
    _Atomic int release_hook, sys_calls, freq_calls, pause_sys;
    _Atomic int admitted, retire_started, retire_done, pause_split, delay_av;
    int pause_admitted, split_fail, map_shrink, spawn_fail, dup_index, attrs_init, bad_delete;
    _Atomic int malloc_index;
    int fdopen_fail, anon_replace, anon_replaced, tag_fail, fake_tag, destructive, unavailable;
    WCHAR anon_name[OS_NAME_MAX];
    HANDLE shrink_file;
    valloc2_fn real_alloc;
    mapview3_fn real_map;
    unmap2_fn real_unmap;
} os_win_test_state;
#define ts os_win_test_state
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s (native %lu)\n", __LINE__, #x, GetLastError()); ts.failures++; } } while (0)
#define REQUIRE(x) do { if (!(x)) { CHECK(x); return 1; } } while (0)
#define OK(x) CHECK((x) == OS_OK)
/* (E) Frozen suite's 10 s inner / 120 s process hang guards, reused. */
#define GUARD_MS 10000u
#define CASE_NS (120ll * 1000000000ll)
static void wait_event(HANDLE h) { CHECK(WaitForSingleObject(h, GUARD_MS) == WAIT_OBJECT_0); }
static void events(void)
{
    ts.entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    ts.resume = CreateEventW(NULL, TRUE, FALSE, NULL);
    ts.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ts.entered && ts.resume && ts.done);
}
static void close_events(void) { CloseHandle(ts.entered); CloseHandle(ts.resume); CloseHandle(ts.done); }
static void released(void *p, BOOL ok)
{
    if (ok && p == ts.view && atomic_exchange(&ts.release_hook, 0)) {
        SetEvent(ts.entered); wait_event(ts.done);
    }
}
static BOOL WINAPI os_win_test_unmap(LPCVOID p)
{
    BOOL ok = UnmapViewOfFile(p); released((void *)p, ok); return ok;
}
static BOOL WINAPI os_win_test_free(LPVOID p, SIZE_T n, DWORD flags)
{
    MEMORY_BASIC_INFORMATION bi;
    bool private_preserve = ts.split_fail == 5 && (flags & MEM_PRESERVE_PLACEHOLDER) &&
                            VirtualQuery(p, &bi, sizeof bi) && bi.Type == MEM_PRIVATE;
    if ((ts.split_fail == 2 && (flags & MEM_PRESERVE_PLACEHOLDER)) || private_preserve ||
        (ts.split_fail == 6 && (flags & MEM_COALESCE_PLACEHOLDERS)) ||
        (ts.split_fail == 7 && flags == MEM_DECOMMIT)) {
        ts.split_fail = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE;
    }
    BOOL ok = VirtualFree(p, n, flags);
    if (flags == MEM_RELEASE) released(p, ok);
    return ok;
}
static LPVOID WINAPI os_win_test_vm_alloc(LPVOID p, SIZE_T n, DWORD flags, DWORD prot)
{
    if (ts.split_fail == 8 && flags == MEM_COMMIT) { ts.split_fail = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return VirtualAlloc(p, n, flags, prot);
}
static void WINAPI os_win_test_sysinfo(LPSYSTEM_INFO s)
{
    atomic_fetch_add(&ts.sys_calls, 1);
    SYSTEM_INFO real; GetSystemInfo(&real);
    if (atomic_exchange(&ts.pause_sys, 0)) {
        s->dwPageSize = real.dwPageSize; SetEvent(ts.entered); wait_event(ts.resume);
    }
    *s = real;
}
static BOOL WINAPI os_win_test_freq(LARGE_INTEGER *f) { atomic_fetch_add(&ts.freq_calls, 1); return QueryPerformanceFrequency(f); }
static void shrink_now(uint64_t bytes)
{
    LARGE_INTEGER z; z.QuadPart = (LONGLONG)bytes;
    CHECK(SetFilePointerEx(ts.shrink_file, z, NULL, FILE_BEGIN)); CHECK(SetEndOfFile(ts.shrink_file));
}
static HANDLE WINAPI os_win_test_section(HANDLE f, LPSECURITY_ATTRIBUTES a, DWORD prot, DWORD hi, DWORD lo, LPCWSTR name)
{
    if (ts.map_shrink == 1) { ts.map_shrink = 0; shrink_now(0); }
    HANDLE h = CreateFileMappingW(f, a, prot, hi, lo, name);
    if (ts.map_shrink == 2 && h) {
        /* NTFS pins EOF as soon as a section exists. Emulate a provider that
         * invalidates that section: release it, shrink, acquire the smaller
         * section, then exercise the real failed MapViewOfFile3. */
        ts.map_shrink = 0; CloseHandle(h); shrink_now(os_map_granularity());
        h = CreateFileMappingW(f, a, prot, hi, lo, name);
    }
    return h;
}
static PVOID WINAPI test_alloc(HANDLE proc, PVOID base, SIZE_T n, ULONG flags, ULONG prot, MEM_EXTENDED_PARAMETER *ep, ULONG ec)
{
    if (ts.split_fail == 4 && (flags & MEM_REPLACE_PLACEHOLDER)) {
        ts.split_fail = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL;
    }
    return ts.real_alloc(proc, base, n, flags, prot, ep, ec);
}
static PVOID WINAPI test_map(HANDLE sec, HANDLE proc, PVOID base, ULONG64 off, SIZE_T n, ULONG flags, ULONG prot, MEM_EXTENDED_PARAMETER *ep, ULONG ec)
{
    if (ts.split_fail == 3) { ts.split_fail = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return ts.real_map(sec, proc, base, off, n, flags, prot, ep, ec);
}
static BOOL WINAPI test_unmap2(HANDLE proc, PVOID base, ULONG flags)
{
    if (ts.split_fail == 1) { ts.split_fail = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    BOOL ok = ts.real_unmap(proc, base, flags);
    if (ok && atomic_exchange(&ts.pause_split, 0)) { SetEvent(ts.split_entered); wait_event(ts.split_resume); }
    return ok;
}
static FARPROC WINAPI os_win_test_proc(HMODULE m, LPCSTR name)
{
    FARPROC p = GetProcAddress(m, name);
    if (!strcmp(name, "VirtualAlloc2")) { ts.real_alloc = (valloc2_fn)(void *)p; return (FARPROC)(void *)test_alloc; }
    if (!strcmp(name, "MapViewOfFile3")) { ts.real_map = (mapview3_fn)(void *)p; return (FARPROC)(void *)test_map; }
    if (!strcmp(name, "UnmapViewOfFile2")) { ts.real_unmap = (unmap2_fn)(void *)p; return (FARPROC)(void *)test_unmap2; }
    return p;
}
static NTSTATUS NTAPI os_win_test_open(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io,
                                     PLARGE_INTEGER size, ULONG attrs, ULONG share, ULONG disp, ULONG opts, PVOID ea, ULONG ealen)
{
    if (ts.fake_tag && (opts & FILE_OPEN_REPARSE_POINT) && (disp == FILE_OVERWRITE || disp == FILE_OVERWRITE_IF)) ts.destructive++;
    if (ts.anon_replace && access == DELETE && oa->RootDirectory == ts.dir.handle) {
        ts.anon_replace = 0;
        size_t units = oa->ObjectName->Length / sizeof(WCHAR);
        memcpy(ts.anon_name, oa->ObjectName->Buffer, units * sizeof(WCHAR)); ts.anon_name[units] = 0;
        char name[OS_NAME_MAX]; CHECK(to_utf8(ts.anon_name, -1, name, sizeof name) == OS_OK);
        OK(os_rename_at(&ts.dir, name, &ts.dir, "moved-anon", OS_RENAME_NOREPLACE));
        os_file replacement = OS_FILE_INIT;
        OK(os_file_open_at(&replacement, &ts.dir, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
        os_file_close(&replacement); ts.anon_replaced = 1;
    }
    return NtCreateFile(h, access, oa, io, size, attrs, share, disp, opts, ea, ealen);
}
static BOOL WINAPI os_win_test_info(HANDLE h, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID out, DWORD n)
{
    if (kind == FileAttributeTagInfo && ts.tag_fail) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    BOOL ok = GetFileInformationByHandleEx(h, kind, out, n);
    if (ok && ts.fake_tag && kind == FileAttributeTagInfo) {
        FILE_ATTRIBUTE_TAG_INFO *t = out;
        t->FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        t->ReparseTag = ts.fake_tag == 1 ? IO_REPARSE_TAG_SYMLINK : IO_REPARSE_TAG_CLOUD;
    }
    return ok;
}
static FILE *os_win_test_fdopen(int fd, const char *mode)
{
    if (ts.fdopen_fail) { errno = ENOMEM; return NULL; }
    return _fdopen(fd, mode);
}
static void *os_win_test_malloc(size_t n)
{
    int which = atomic_fetch_add(&ts.malloc_index, 1) + 1;
    if ((ts.spawn_fail == 7 && which == 1) || (ts.spawn_fail == 9 && which == 2)) return NULL;
    return malloc(n);
}
static BOOL WINAPI os_win_test_dup(HANDLE a, HANDLE b, HANDLE c, LPHANDLE d, DWORD e, BOOL f, DWORD g)
{
    if (ts.spawn_fail >= 1 && ts.spawn_fail <= 3 && ++ts.dup_index == ts.spawn_fail) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE;
    }
    return DuplicateHandle(a, b, c, d, e, f, g);
}
static BOOL WINAPI os_win_test_attrs(LPPROC_THREAD_ATTRIBUTE_LIST a, DWORD b, DWORD c, PSIZE_T d)
{
    if (!a && ts.spawn_fail == 8) { *d = 0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    if (a && ts.spawn_fail == 4) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    BOOL ok = InitializeProcThreadAttributeList(a, b, c, d);
    if (a && ok) ts.attrs_init = 1;
    return ok;
}
static void WINAPI os_win_test_delattrs(LPPROC_THREAD_ATTRIBUTE_LIST a)
{
    if (!ts.attrs_init) ts.bad_delete++;
    else DeleteProcThreadAttributeList(a);
    ts.attrs_init = 0;
}
static BOOL WINAPI os_win_test_update(LPPROC_THREAD_ATTRIBUTE_LIST a, DWORD b, DWORD_PTR c, PVOID d, SIZE_T e, PVOID f, PSIZE_T g)
{
    if (ts.spawn_fail == 5) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return UpdateProcThreadAttribute(a, b, c, d, e, f, g);
}
static BOOL WINAPI os_win_test_create(LPCWSTR a, LPWSTR b, LPSECURITY_ATTRIBUTES c, LPSECURITY_ATTRIBUTES d,
                                    BOOL e, DWORD f, LPVOID g, LPCWSTR h, LPSTARTUPINFOW i, LPPROCESS_INFORMATION j)
{
    if (ts.spawn_fail == 6) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    return CreateProcessW(a, b, c, d, e, f, g, h, i, j);
}
static void os_win_test_hook(const char *event, void *context)
{
    (void)context;
    if (!strcmp(event, "admitted") && ts.pause_admitted && !atomic_exchange(&ts.admitted, 1)) {
        SetEvent(ts.entered); wait_event(ts.resume);
    }
    if (!strcmp(event, "retiring")) { atomic_store(&ts.retire_started, 1); SetEvent(ts.done); }
}
static LONG dispatch(const os_map *m, size_t off, DWORD code, ULONG_PTR access)
{
    EXCEPTION_RECORD r; memset(&r, 0, sizeof r);
    r.ExceptionCode = code; r.NumberParameters = 3; r.ExceptionInformation[0] = access;
    r.ExceptionInformation[1] = (ULONG_PTR)m->addr + off;
    r.ExceptionInformation[2] = (ULONG_PTR)(ULONG)STATUS_DEVICE_DATA_ERROR;
    CONTEXT c; memset(&c, 0, sizeof c);
    EXCEPTION_POINTERS p = {&r, &c}; return fault_handler(&p);
}
static void path(char *out, const char *name) { int n = snprintf(out, OS_PATH_MAX, "%s\\%s", ts.root, name); CHECK(n > 0 && (unsigned)n < OS_PATH_MAX); }
static void put(const char *name, const char *text)
{
    os_file f = OS_FILE_INIT; OK(os_file_open_at(&f, &ts.dir, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
    size_t done = 0; OK(os_file_write(&f, text, strlen(text), &done)); CHECK(done == strlen(text)); os_file_close(&f);
}
static int mapped(os_file *f, os_map *m)
{
    REQUIRE(os_file_open_at(f, &ts.dir, "mapped", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    /* (P) Four allocation-granularity spans permit aligned prefix/suffix races. */
    size_t n = os_map_granularity() * 4; REQUIRE(os_file_set_size(f, n) == OS_OK);
    unsigned char byte = 0x5a; size_t done = 0;
    for (size_t at = 0; at < n; at += os_page_size()) REQUIRE(os_file_pwrite(f, &byte, 1, at, &done) == OS_OK && done == 1);
    REQUIRE(os_map_file(m, f, 0, n) == OS_OK); return 0;
}
static void allocator(void *unused)
{
    (void)unused; wait_event(ts.entered);
    ts.interloper = VirtualAlloc(ts.view, ts.extent, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (ts.interloper) *(volatile unsigned char *)ts.interloper = 0xa5;
    SetEvent(ts.done);
}
static int case_unmap(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m));
    int slot; REQUIRE(os_fault_register(&m, &slot) == OS_OK);
    OK(os_fault_test_trigger(&m, os_map_granularity() * 2)); os_fault_retire(slot);
    ts.view = (void *)m.addr; ts.extent = os_map_granularity() * 2; events(); atomic_store(&ts.release_hook, 1);
    os_thread t; REQUIRE(os_thread_start(&t, allocator, NULL) == OS_OK); os_unmap(&m); OK(os_thread_join(&t));
    CHECK(ts.interloper == ts.view);
    MEMORY_BASIC_INFORMATION bi; CHECK(VirtualQuery(ts.view, &bi, sizeof bi) != 0);
    CHECK(bi.State == MEM_COMMIT);
    if (bi.State == MEM_COMMIT) CHECK(*(volatile unsigned char *)ts.interloper == 0xa5);
    if (ts.interloper) VirtualFree(ts.interloper, 0, MEM_RELEASE);
    close_events(); os_file_close(&f); return ts.failures != 0;
}
/* This interceptor intentionally runs before ASan's VEH. It is uninstrumented,
 * matches only the exact test read, and pauses a real AV before os's dispatch. */
static LONG CALLBACK __attribute__((no_sanitize("address", "undefined"))) delay_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 &&
        r->ExceptionInformation[0] == 0 && r->ExceptionInformation[1] == (ULONG_PTR)ts.view &&
        atomic_exchange(&ts.delay_av, 0)) {
        SetEvent(ts.entered);
        WaitForSingleObject(ts.resume, GUARD_MS);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void prefix_reader(void *arg)
{
    os_map *m = arg;
    CHECK(*(const volatile unsigned char *)m->addr == 0x5a);
}
static void split_thread(void *arg)
{
    os_map *m = arg;
    OK(os_fault_test_trigger(m, os_map_granularity() * 2));
}
static int case_delayed_av(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int slot;
    REQUIRE(os_fault_register(&m, &slot) == OS_OK); events();
    ts.split_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    ts.split_resume = CreateEventW(NULL, TRUE, FALSE, NULL);
    REQUIRE(ts.split_entered && ts.split_resume);
    ts.view = (void *)m.addr; atomic_store(&ts.pause_split, 1); atomic_store(&ts.delay_av, 1);
    PVOID veh = AddVectoredExceptionHandler(1, delay_handler); REQUIRE(veh);
    os_thread split, read;
    REQUIRE(os_thread_start(&split, split_thread, &m) == OS_OK); wait_event(ts.split_entered);
    REQUIRE(os_thread_start(&read, prefix_reader, &m) == OS_OK); wait_event(ts.entered);
    SetEvent(ts.split_resume); OK(os_thread_join(&split));
    /* The read's native AV is already raised, but os's dispatch has not run.
     * Complete the split and clear busy before releasing that dispatch. */
    SetEvent(ts.resume); OK(os_thread_join(&read)); CHECK(!atomic_load(&ts.delay_av));
    CHECK(RemoveVectoredExceptionHandler(veh));
    CHECK(dispatch(&m, 0, EXCEPTION_ACCESS_VIOLATION, 1) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(dispatch(&m, 0, EXCEPTION_IN_PAGE_ERROR, 1) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(dispatch(&m, 0, EXCEPTION_ACCESS_VIOLATION, 8) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(*(const volatile unsigned char *)m.addr == 0x5a);
    CloseHandle(ts.split_entered); CloseHandle(ts.split_resume); close_events();
    os_fault_retire(slot); os_unmap(&m); os_file_close(&f); return ts.failures != 0;
}
static void cold_page(void *unused) { (void)unused; CHECK(os_page_size() != 0); }
static void cold_others(void *unused)
{
    (void)unused; size_t g = os_map_granularity(); CHECK(g && g % os_page_size() == 0);
    CHECK(os_now_ns() > 0); CHECK(os_thread_cpu_resolution_ns() > 0);
    char tmp[OS_PATH_MAX]; OK(os_temp_dir(tmp, sizeof tmp));
    CHECK(placeholders_ok());
    if (g) {
        char name[64]; snprintf(name, sizeof name, "cold-%lu", GetCurrentThreadId());
        os_file f = OS_FILE_INIT; os_map m = {0};
        OK(os_file_open_at(&f, &ts.dir, name, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL));
        OK(os_file_set_size(&f, g)); OK(os_map_file(&m, &f, 0, g));
        if (m.addr) CHECK(*(const volatile unsigned char *)m.addr == 0);
        os_unmap(&m); os_file_close(&f); OK(os_unlink_at(&ts.dir, name));
    }
    SetEvent(ts.done);
}
static int case_cold(void)
{
    events(); atomic_store(&ts.pause_sys, 1); os_thread a, b;
    REQUIRE(os_thread_start(&a, cold_page, NULL) == OS_OK); wait_event(ts.entered);
    REQUIRE(os_thread_start(&b, cold_others, NULL) == OS_OK);
    os_thread peers[3];
    for (size_t i = 0; i < sizeof peers / sizeof peers[0]; i++) REQUIRE(os_thread_start(&peers[i], cold_others, NULL) == OS_OK);
    /* (E) Observation window only: broken publication lets b finish immediately. */
    CHECK(WaitForSingleObject(ts.done, 100u) == WAIT_TIMEOUT);
    SetEvent(ts.resume); OK(os_thread_join(&a)); OK(os_thread_join(&b));
    for (size_t i = 0; i < sizeof peers / sizeof peers[0]; i++) OK(os_thread_join(&peers[i]));
    CHECK(atomic_load(&ts.sys_calls) == 1); CHECK(atomic_load(&ts.freq_calls) == 1);
    close_events(); return ts.failures != 0;
}
typedef struct reader_arg { os_map *m; int slot; size_t off; _Atomic int *go; } reader_arg;
static void fault_reader(void *arg);
static void retiring(void *arg);
static int case_duplicate(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int a, b;
    REQUIRE(os_fault_register(&m, &a) == OS_OK); REQUIRE(os_fault_register(&m, &b) == OS_OK);
    OK(os_fault_test_trigger(&m, os_map_granularity() * 2));
    CHECK(os_fault_epoch(a) == 1); CHECK(os_fault_epoch(b) == 1);
    events(); ts.pause_admitted = 1;
    reader_arg first = {&m, a, os_map_granularity(), NULL};
    reader_arg second = {&m, b, 0, NULL};
    os_thread reader, retire, other;
    REQUIRE(os_thread_start(&reader, fault_reader, &first) == OS_OK);
    DWORD entered = WaitForSingleObject(ts.entered, GUARD_MS); CHECK(entered == WAIT_OBJECT_0);
    REQUIRE(os_thread_start(&retire, retiring, &a) == OS_OK);
    if (entered == WAIT_OBJECT_0) { wait_event(ts.done); CHECK(!atomic_load(&ts.retire_done)); }
    REQUIRE(os_thread_start(&other, fault_reader, &second) == OS_OK); OK(os_thread_join(&other));
    SetEvent(ts.resume); OK(os_thread_join(&reader)); OK(os_thread_join(&retire));
    CHECK(os_fault_epoch(b) == 3); close_events();
    os_fault_retire(b); os_unmap(&m); os_file_close(&f); return ts.failures != 0;
}
static int case_zero_fault(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int s;
    REQUIRE(os_fault_register(&m, &s) == OS_OK); size_t off = os_map_granularity() * 2;
    OK(os_fault_test_trigger(&m, off));
    /* Poison the synthetic pagefile-backed fault to prove recreation, not retry. */
    DWORD old; void *p = (char *)m.addr + off;
    REQUIRE(VirtualProtect(p, os_page_size(), PAGE_READWRITE, &old)); *(volatile unsigned char *)p = 0x7b;
    REQUIRE(VirtualProtect(p, os_page_size(), old, &old));
    CHECK(dispatch(&m, off, EXCEPTION_IN_PAGE_ERROR, 0) == EXCEPTION_CONTINUE_EXECUTION);
    CHECK(*(const volatile unsigned char *)p == 0); CHECK(os_fault_epoch(s) == 2);
    REQUIRE(VirtualProtect(p, os_page_size(), PAGE_NOACCESS, &old));
    CHECK(dispatch(&m, off, EXCEPTION_ACCESS_VIOLATION, 0) == EXCEPTION_CONTINUE_SEARCH);
    REQUIRE(VirtualProtect(p, os_page_size(), old, &old));
    os_fault_retire(s); os_unmap(&m); os_file_close(&f); return ts.failures != 0;
}
static int case_split_fail(void)
{
    /* (P) Four initial split operations plus preserve/coalesce of an existing
     * suffix and decommit/recommit of private zeros = eight failure sites. */
    for (int stage = 1; stage <= 8; stage++) {
        os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int s;
        REQUIRE(os_fault_register(&m, &s) == OS_OK); void *base = (void *)m.addr; size_t n = m.len;
        if (stage >= 5) OK(os_fault_test_trigger(&m, os_map_granularity() * 2));
        ts.split_fail = stage;
        size_t off = os_map_granularity() * (stage == 5 || stage == 6 ? 1u : 2u);
        CHECK(dispatch(&m, off, EXCEPTION_IN_PAGE_ERROR, 0) == EXCEPTION_CONTINUE_SEARCH);
        CHECK(ts.split_fail == 0);
        ts.split_fail = 0; os_fault_retire(s); os_unmap(&m);
        for (size_t at = 0; at < n; at += os_page_size()) { MEMORY_BASIC_INFORMATION bi; CHECK(VirtualQuery((char *)base + at, &bi, sizeof bi)); CHECK(bi.State == MEM_FREE); }
        os_file_close(&f); OK(os_unlink_at(&ts.dir, "mapped"));
    }
    return ts.failures != 0;
}
static void fault_reader(void *arg)
{
    reader_arg *r = arg; if (r->go) while (!atomic_load(r->go)) SwitchToThread();
    OK(os_fault_test_trigger(r->m, r->off));
    CHECK(*(const volatile unsigned char *)((const char *)r->m->addr + r->off) == 0);
}
static void retiring(void *arg) { int s = *(int *)arg; os_fault_retire(s); atomic_store(&ts.retire_done, 1); }
static int case_retire(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int s;
    REQUIRE(os_fault_register(&m, &s) == OS_OK); events(); ts.pause_admitted = 1;
    reader_arg r = {&m, s, os_map_granularity(), NULL}; os_thread a, b;
    REQUIRE(os_thread_start(&a, fault_reader, &r) == OS_OK);
    DWORD entered = WaitForSingleObject(ts.entered, GUARD_MS);
    CHECK(entered == WAIT_OBJECT_0);
    REQUIRE(os_thread_start(&b, retiring, &s) == OS_OK);
    if (entered == WAIT_OBJECT_0) { wait_event(ts.done); CHECK(!atomic_load(&ts.retire_done)); }
    SetEvent(ts.resume); OK(os_thread_join(&a)); OK(os_thread_join(&b)); CHECK(atomic_load(&ts.retire_done));
    close_events(); os_unmap(&m); os_file_close(&f); return ts.failures != 0;
}
typedef struct scan_arg { os_map *map; _Atomic int *go, *stop; } scan_arg;
static void scan_reader(void *arg)
{
    scan_arg *r = arg;
    while (!atomic_load(r->go)) SwitchToThread();
    const volatile unsigned char *p = r->map->addr;
    do {
        unsigned char prefix = p[0], suffix = p[r->map->len - os_page_size()];
        CHECK(prefix == 0x5a); CHECK(suffix == 0x5a || suffix == 0);
    } while (!atomic_load(r->stop));
}
static int case_readers(void)
{
    os_file f = OS_FILE_INIT; os_map m = {0}; REQUIRE(!mapped(&f, &m)); int s;
    REQUIRE(os_fault_register(&m, &s) == OS_OK);
    /* (P) Four faults, one at each successively earlier page of this view. */
    os_thread t[4], scans[2]; reader_arg r[4]; _Atomic int go = 0, stop = 0;
    scan_arg scan = {&m, &go, &stop};
    for (size_t i = 0; i < sizeof scans / sizeof scans[0]; i++) REQUIRE(os_thread_start(&scans[i], scan_reader, &scan) == OS_OK);
    for (int i = 0; i < 4; i++) { r[i] = (reader_arg){&m, s, (size_t)(4 - i) * os_page_size(), &go}; REQUIRE(os_thread_start(&t[i], fault_reader, &r[i]) == OS_OK); }
    atomic_store(&go, 1); for (int i = 0; i < 4; i++) OK(os_thread_join(&t[i]));
    atomic_store(&stop, 1);
    for (size_t i = 0; i < sizeof scans / sizeof scans[0]; i++) OK(os_thread_join(&scans[i]));
    CHECK(os_fault_epoch(s) == 4);
    const volatile unsigned char *p = m.addr; CHECK(p[0] == 0x5a); CHECK(p[os_page_size()] == 0);
    os_fault_retire(s); os_unmap(&m); os_file_close(&f); return ts.failures != 0;
}
static int case_nofollow_trunc(void)
{
    put("tagged", "keep"); os_file f = OS_FILE_INIT;
    /* Shim represents a visible surrogate/data tag, independent of privileges. */
    ts.fake_tag = 1;
    CHECK(os_file_open_at(&f, &ts.dir, "tagged", OS_OPEN_WRITE | OS_OPEN_TRUNC | OS_OPEN_NOFOLLOW) == OS_E_LOOP);
    os_file_close(&f); CHECK(ts.destructive == 0);
    CHECK(os_file_open_at(&f, &ts.dir, "tagged", OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC | OS_OPEN_NOFOLLOW) == OS_E_LOOP);
    os_file_close(&f); CHECK(ts.destructive == 0); ts.fake_tag = 0;
    os_stat st; OK(os_stat_at(&ts.dir, "tagged", 0, &st)); CHECK(st.size == 4);
    ts.fake_tag = 2;
    OK(os_file_open_at(&f, &ts.dir, "tagged", OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC | OS_OPEN_NOFOLLOW));
    CHECK(ts.destructive == 0); os_file_close(&f); ts.fake_tag = 0;
    OK(os_stat_at(&ts.dir, "tagged", 0, &st)); CHECK(st.size == 0); return ts.failures != 0;
}
static int case_tag_error(void)
{
    put("tagged", "x"); ts.fake_tag = 1; ts.tag_fail = 1; os_stat st;
    CHECK(os_stat_at(&ts.dir, "tagged", OS_STAT_NOFOLLOW, &st) == OS_E_ACCESS);
    CHECK(os_unlink_at(&ts.dir, "tagged") == OS_E_ACCESS);
    ts.tag_fail = 0; ts.fake_tag = 0; return ts.failures != 0;
}
static int case_anon(void)
{
    ts.anon_replace = 1; os_file f = OS_FILE_INIT;
    CHECK(os_file_open_anon(&f, &ts.dir) == OS_E_BUSY); CHECK(ts.anon_replaced);
    char name[OS_NAME_MAX]; OK(to_utf8(ts.anon_name, -1, name, sizeof name));
    os_stat st; OK(os_stat_at(&ts.dir, name, 0, &st)); CHECK(!os_file_is_open(&f));
    CHECK(os_stat_at(&ts.dir, "moved-anon", 0, &st) == OS_E_NOENT);
    os_file_close(&f); return ts.failures != 0;
}
static int case_literal(void)
{
    const char *names[] = {"note", "note.", "note "};
    for (size_t i = 0; i < 3; i++) put(names[i], names[i]);
    os_dir_iter it; REQUIRE(os_dir_iter_open(&it, &ts.dir) == OS_OK); unsigned seen = 0;
    for (;;) { const char *name; REQUIRE(os_dir_next(&it, &name) == OS_OK); if (!name) break;
        for (unsigned i = 0; i < 3; i++) if (!strcmp(name, names[i])) seen |= 1u << i; }
    os_dir_iter_close(&it); CHECK(seen == 7u);
    WCHAR previous[OS_PATH_MAX], work[OS_PATH_MAX]; REQUIRE(GetCurrentDirectoryW(OS_PATH_MAX, previous));
    OK(to_wide(ts.root, work, OS_PATH_MAX, NULL)); REQUIRE(SetCurrentDirectoryW(work));
    for (size_t i = 0; i < 3; i++) {
        char p[OS_PATH_MAX], q[OS_PATH_MAX], renamed[32]; path(p, names[i]);
        os_file relative = OS_FILE_INIT; OK(os_file_open(&relative, names[i], OS_OPEN_READ));
        os_stat rel, at; OK(os_file_stat(&relative, &rel)); OK(os_stat_at(&ts.dir, names[i], 0, &at));
        CHECK(os_identity_equal(&rel.id, &at.id)); os_file_close(&relative);
        os_file f = OS_FILE_INIT; OK(os_file_open(&f, p, OS_OPEN_READ)); char data[16] = {0}; size_t got;
        OK(os_file_read(&f, data, sizeof data - 1, &got)); CHECK(!strcmp(data, names[i])); os_file_close(&f);
        snprintf(renamed, sizeof renamed, "renamed%s", names[i] + 4); path(q, renamed);
        OK(os_rename(p, q, OS_RENAME_NOREPLACE)); os_stat st;
        OK(os_stat_at(&ts.dir, renamed, 0, &st)); CHECK(st.size == strlen(names[i]));
        OK(os_unlink(q)); CHECK(os_stat_at(&ts.dir, renamed, 0, &st) == OS_E_NOENT);
    }
    CHECK(SetCurrentDirectoryW(previous));
    return ts.failures != 0;
}
static int case_surrogate(void)
{
    WCHAR bad[] = {L'n', 0xd800u, 0}, replacement[] = {L'n', 0xfffdu, 0};
    for (int i = 0; i < 2; i++) {
        UNICODE_STRING us; WCHAR *w = i ? replacement : bad; set_ustr(&us, w, 2); HANDLE h;
        REQUIRE(NT_SUCCESS(nt_open(&h, ts.dir.handle, &us, FILE_GENERIC_WRITE, FILE_CREATE, FILE_NON_DIRECTORY_FILE, 0))); CloseHandle(h);
    }
    os_dir_iter it; REQUIRE(os_dir_iter_open(&it, &ts.dir) == OS_OK); int invalid = 0, rep = 0;
    for (;;) { const char *name = NULL; os_err rc = os_dir_next(&it, &name);
        if (rc) { CHECK(rc == OS_E_INVAL); invalid++; continue; }
        if (!name) break; if (!strcmp(name, "n\xef\xbf\xbd")) rep++; }
    CHECK(invalid == 1); CHECK(rep == 1); os_dir_iter_close(&it); return ts.failures != 0;
}
static int case_acl(void)
{
    put("acl", "readable"); char p[OS_PATH_MAX]; path(p, "acl"); WCHAR w[OS_PATH_MAX]; OK(to_wide(p, w, OS_PATH_MAX, NULL));
    /* (P) Deny only WRITE_ATTRIBUTES to Everyone; retain read/write-data rights. */
    PSECURITY_DESCRIPTOR sd = NULL; REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x100;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &sd, NULL));
    BOOL present, def; PACL dacl; REQUIRE(GetSecurityDescriptorDacl(sd, &present, &dacl, &def));
    REQUIRE(SetNamedSecurityInfoW(w, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, dacl, NULL) == ERROR_SUCCESS);
    os_file f = OS_FILE_INIT; CHECK(os_file_open_at(&f, &ts.dir, "acl", OS_OPEN_READ) == OS_OK);
    if (os_file_is_open(&f)) { char b[8]; size_t done; OK(os_file_read(&f, b, sizeof b, &done)); CHECK(done == sizeof b); CHECK(os_file_set_mode(&f, 0444) == OS_E_ACCESS); }
    os_file_close(&f);
    CHECK(SetNamedSecurityInfoW(w, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
    LocalFree(sd); sd = NULL;
    REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x81;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &sd, NULL));
    REQUIRE(GetSecurityDescriptorDacl(sd, &present, &dacl, &def));
    REQUIRE(SetNamedSecurityInfoW(w, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, dacl, NULL) == ERROR_SUCCESS);
    CHECK(os_file_open_at(&f, &ts.dir, "acl", OS_OPEN_WRITE) == OS_OK);
    if (os_file_is_open(&f)) { size_t done; OK(os_file_pwrite(&f, "x", 1, 0, &done)); CHECK(done == 1); }
    os_file_close(&f);
    CHECK(SetNamedSecurityInfoW(w, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
    LocalFree(sd); return ts.failures != 0;
}
static int case_stdio(void)
{
    put("stdio", "x"); os_file f = OS_FILE_INIT, other = OS_FILE_INIT;
    REQUIRE(os_file_open_at(&f, &ts.dir, "stdio", OS_OPEN_RDWR) == OS_OK); OK(os_file_try_lock(&f, OS_LOCK_EXCLUSIVE));
    ts.fdopen_fail = 1; FILE *fp = NULL; CHECK(os_file_to_stdio(&f, "r+b", &fp) == OS_E_NOMEM);
    CHECK(fp == NULL); CHECK(os_file_is_open(&f)); ts.fdopen_fail = 0;
    size_t done = 0; CHECK(os_file_pwrite(&f, "z", 1, 0, &done) == OS_OK); CHECK(done == 1);
    REQUIRE(os_file_open_at(&other, &ts.dir, "stdio", OS_OPEN_RDWR) == OS_OK); CHECK(os_file_try_lock(&other, OS_LOCK_EXCLUSIVE) == OS_E_BUSY);
    os_file_close(&f); OK(os_file_try_lock(&other, OS_LOCK_EXCLUSIVE)); os_file_close(&other); return ts.failures != 0;
}
static int case_shrink(void)
{
    os_file f = OS_FILE_INIT; REQUIRE(os_file_open_at(&f, &ts.dir, "shrink", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    for (int step = 1; step <= 2; step++) {
        size_t g = os_map_granularity(); OK(os_file_set_size(&f, g * 3)); ts.shrink_file = f.handle; ts.map_shrink = step;
        os_map m = {0}; CHECK(os_map_file(&m, &f, g, g) == OS_E_RANGE); CHECK(!m.addr); os_unmap(&m);
    }
    OK(os_file_set_size(&f, os_map_granularity() * 3));
    ts.split_fail = 3; os_map unchanged = {0};
    CHECK(os_map_file(&unchanged, &f, os_map_granularity(), os_map_granularity()) == OS_E_NOMEM);
    CHECK(!unchanged.addr); os_unmap(&unchanged);
    os_file_close(&f); return ts.failures != 0;
}
static int case_spawn(void)
{
    char self[OS_PATH_MAX]; OK(os_proc_self_path(self, sizeof self)); const char *args[] = {self, "--noop", NULL};
    HANDLE saved[3], streams[3]; DWORD ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 3; i++) { saved[i] = GetStdHandle(ids[i]); streams[i] = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL); REQUIRE(streams[i] != INVALID_HANDLE_VALUE); REQUIRE(SetStdHandle(ids[i], streams[i])); }
    /* (P) Three standard duplicates + command allocation + attribute sizing,
     * allocation, initialization, update + process creation = nine sites. */
    for (int stage = 1; stage <= 9; stage++) {
        DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
        ts.spawn_fail = stage; ts.dup_index = 0; ts.bad_delete = 0; ts.malloc_index = 0;
        os_proc child; os_err rc = os_proc_spawn(&child, args);
        CHECK(rc == (stage <= 4 || stage >= 7 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT));
        CHECK(ts.bad_delete == 0); ts.spawn_fail = 0;
        if (rc == OS_OK) { os_proc_status status; OK(os_proc_wait(&child, CASE_NS, &status)); }
        CHECK(child.process == NULL); CHECK(GetProcessHandleCount(GetCurrentProcess(), &after)); CHECK(after == before);
    }
    for (int i = 0; i < 3; i++) { CHECK(SetStdHandle(ids[i], saved[i])); CloseHandle(streams[i]); }
    return ts.failures != 0;
}
static int case_junction(void)
{
    char target[OS_PATH_MAX], link[OS_PATH_MAX], comspec[OS_PATH_MAX];
    path(target, "target"); path(link, "junction"); OK(os_mkdir(target));
    OK(os_env_get("COMSPEC", comspec, sizeof comspec));
    WCHAR app[OS_PATH_MAX], wt[OS_PATH_MAX], wl[OS_PATH_MAX], cmd[OS_PATH_MAX * 3];
    OK(to_wide(comspec, app, OS_PATH_MAX, NULL)); OK(to_wide(target, wt, OS_PATH_MAX, NULL)); OK(to_wide(link, wl, OS_PATH_MAX, NULL));
    swprintf(cmd, OS_PATH_MAX * 3, L"\"%ls\" /d /c mklink /J \"%ls\" \"%ls\"", app, wl, wt);
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    REQUIRE(CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, wt, &si, &pi));
    REQUIRE(WaitForSingleObject(pi.hProcess, GUARD_MS) == WAIT_OBJECT_0);
    DWORD code; REQUIRE(GetExitCodeProcess(pi.hProcess, &code)); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); REQUIRE(code == 0);
    os_stat st; OK(os_stat_path(link, OS_STAT_NOFOLLOW, &st)); CHECK(st.kind == OS_KIND_LINK);
    os_file f = OS_FILE_INIT; CHECK(os_file_open(&f, link, OS_OPEN_READ | OS_OPEN_NOFOLLOW) == OS_E_LOOP); os_file_close(&f);
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_NOFOLLOW) == OS_E_EXIST); os_file_close(&f);
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_E_EXIST); os_file_close(&f);
    CHECK(os_rmdir(link) == OS_E_NOTDIR); OK(os_unlink(link)); OK(os_stat_path(target, 0, &st)); return ts.failures != 0;
}
static int case_symlink(void)
{
    put("target", "keep"); char target[OS_PATH_MAX], link[OS_PATH_MAX]; path(target, "target"); path(link, "symlink");
    WCHAR wt[OS_PATH_MAX], wl[OS_PATH_MAX]; OK(to_wide(target, wt, OS_PATH_MAX, NULL)); OK(to_wide(link, wl, OS_PATH_MAX, NULL));
    if (!CreateSymbolicLinkW(wl, wt, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        printf("fixture unavailable: file symlink native %lu (requires Developer Mode/elevation)\n", GetLastError()); ts.unavailable = 1; return 0;
    }
    os_stat st; OK(os_stat_path(link, OS_STAT_NOFOLLOW, &st)); CHECK(st.kind == OS_KIND_LINK);
    os_file f = OS_FILE_INIT;
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_TRUNC | OS_OPEN_NOFOLLOW) == OS_E_LOOP); os_file_close(&f);
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC | OS_OPEN_NOFOLLOW) == OS_E_LOOP); os_file_close(&f);
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_NOFOLLOW) == OS_E_EXIST); os_file_close(&f);
    CHECK(os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_E_EXIST); os_file_close(&f);
    OK(os_stat_path(target, 0, &st)); CHECK(st.size == 4); CHECK(os_rmdir(link) == OS_E_NOTDIR);
    OK(os_unlink(target)); OK(os_stat_path(link, OS_STAT_NOFOLLOW, &st)); CHECK(st.kind == OS_KIND_LINK);
    CHECK(os_file_open(&f, link, OS_OPEN_READ | OS_OPEN_NOFOLLOW) == OS_E_LOOP); os_file_close(&f); OK(os_unlink(link));
    return ts.failures != 0;
}
/* Native cleanup preserves literal names and removes surrogate names themselves. */
static void cleanup(const WCHAR *root)
{
    WCHAR pattern[OS_PATH_MAX]; swprintf(pattern, OS_PATH_MAX, L"%ls\\*", root);
    WIN32_FIND_DATAW e; HANDLE h = FindFirstFileW(pattern, &e);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!wcscmp(e.cFileName, L".") || !wcscmp(e.cFileName, L"..")) continue;
            WCHAR p[OS_PATH_MAX]; swprintf(p, OS_PATH_MAX, L"%ls\\%ls", root, e.cFileName);
            if (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(e.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) cleanup(p);
                CHECK(RemoveDirectoryW(p));
            } else { SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL); CHECK(DeleteFileW(p)); }
        } while (FindNextFileW(h, &e)); FindClose(h);
    }
}
typedef struct test_case { const char *name; int (*run)(void); } test_case;
static const test_case cases[] = {
    {"unmap_ownership", case_unmap}, {"delayed_read_av", case_delayed_av}, {"cold_caches", case_cold},
    {"duplicate_epochs", case_duplicate}, {"zero_suffix_fault", case_zero_fault}, {"split_stage_failures", case_split_fail},
    {"same_view_readers", case_readers}, {"retire_admitted", case_retire}, {"nofollow_truncation", case_nofollow_trunc},
    {"junction", case_junction}, {"file_symlink", case_symlink}, {"tag_query_error", case_tag_error},
    {"anon_replacement", case_anon}, {"literal_names", case_literal}, {"strict_listing", case_surrogate},
    {"read_acl", case_acl}, {"stdio_failure", case_stdio}, {"map_shrink", case_shrink}, {"spawn_failures", case_spawn}
};
int main(int argc, char **argv)
{
    (void)os_win_test_hook;
    if (argc > 1 && !strcmp(argv[1], "--noop")) return 0;
    if (argc > 2 && !strcmp(argv[1], "--case")) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) if (!strcmp(argv[2], cases[i].name)) {
            /* Native setup leaves the OS-layer caches cold. */
            WCHAR temp[OS_PATH_MAX], scratch[OS_PATH_MAX]; REQUIRE(GetTempPathW(OS_PATH_MAX, temp));
            swprintf(scratch, OS_PATH_MAX, L"%lssublimite-test-r2-%lu", temp, GetCurrentProcessId()); REQUIRE(CreateDirectoryW(scratch, NULL));
            REQUIRE(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, scratch, -1, ts.root, sizeof ts.root, NULL, NULL));
            REQUIRE(os_dir_open(&ts.dir, ts.root) == OS_OK); int rc = cases[i].run(); os_dir_close(&ts.dir);
            WCHAR extended[OS_PATH_MAX]; swprintf(extended, OS_PATH_MAX, L"\\\\?\\%ls", scratch); cleanup(extended); CHECK(RemoveDirectoryW(extended));
            return rc || ts.failures ? 1 : ts.unavailable ? 77 : 0;
        }
        return 2;
    }
    char self[OS_PATH_MAX]; REQUIRE(os_proc_self_path(self, sizeof self) == OS_OK); size_t passed = 0, unavailable = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const char *args[] = {self, "--case", cases[i].name, NULL}; os_proc p; os_proc_status st;
        os_err rc = os_proc_spawn(&p, args); bool good = false, skipped = false;
        if (rc == OS_OK) { rc = os_proc_wait(&p, CASE_NS, &st); if (rc == OS_E_TIMEDOUT) { os_proc_kill(&p); os_proc_wait(&p, -1, &st); } else { skipped = rc == OS_OK && st.how == OS_PROC_EXITED && st.code == 77; good = rc == OS_OK && st.how == OS_PROC_EXITED && st.code == 0; } }
        if (p.id) {
            WCHAR temp[OS_PATH_MAX], scratch[OS_PATH_MAX];
            if (GetTempPathW(OS_PATH_MAX, temp)) {
                swprintf(scratch, OS_PATH_MAX, L"\\\\?\\%lssublimite-test-r2-%lu", temp, p.id);
                if (GetFileAttributesW(scratch) != INVALID_FILE_ATTRIBUTES) { cleanup(scratch); CHECK(RemoveDirectoryW(scratch)); }
            }
        }
        printf("os_win_test %s: %s\n", cases[i].name, good ? "PASS" : skipped ? "UNAVAILABLE (file symlink privilege)" : "FAIL"); fflush(stdout); passed += good ? 1u : 0u; unavailable += skipped ? 1u : 0u;
    }
    printf("os_win_test: %zu/%zu passed, %zu unavailable\n", passed, sizeof cases / sizeof cases[0], unavailable); return passed + unavailable == sizeof cases / sizeof cases[0] && !ts.failures ? 0 : 1;
}
#endif
