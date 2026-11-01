/* Native notifications and save metadata; deliberately outside os.h. */
#if defined(_WIN32)
#include "file/file_internal.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <winternl.h>
#include <sddl.h>
#ifndef NtSetSecurityObject
NTSYSAPI NTSTATUS NTAPI NtSetSecurityObject(HANDLE, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR);
#endif
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Same member bound as Linux: existing editor reservations, live + retained. */
#define FILE_WATCH_SLOTS (4096u + 4096u)
typedef struct file_parent_watch {
    HANDLE handle, event, wait;
    OVERLAPPED io;
    os_identity identity;
    /* Same bounded dispatch buffer as the Linux service. */
    DWORD buffer[4096 / sizeof(DWORD)];
    bool armed;
} file_parent_watch;
typedef struct file_watch_service {
    os_once once;
    HANDLE event, timer;
    DWORD error;
    _Atomic bool refresh;
    size_t count, path_count;
    file *files[FILE_WATCH_SLOTS];
    file_path_watch *paths[FILE_WATCH_SLOTS];
    file_parent_watch *parents[FILE_WATCH_SLOTS];
} file_watch_service;
static file_watch_service file_watches = {.once = OS_ONCE_INIT};

static int watch_error(DWORD error) { SetLastError(error); return -1; }
static void watch_install(void)
{
    file_watches.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!file_watches.event) file_watches.error = GetLastError();
}
void *file_watch_handle(void) { return file_watches.event; }
/* Wait callbacks announce state only; membership, buffers and rearming stay
 * on the UI thread. Each outstanding I/O owns its own completion event. */
static VOID CALLBACK watch_signal(PVOID context, BOOLEAN timed_out)
{ (void)timed_out; (void)SetEvent(context); }
static VOID CALLBACK watch_refresh_signal(PVOID context, BOOLEAN timed_out)
{
    (void)timed_out;
    atomic_store_explicit(&file_watches.refresh, true, memory_order_release);
    (void)SetEvent(context);
}
static bool watch_timer_start(void)
{
    if (file_watches.timer) return true;
    /* (E) Parent pathname lifetime fallback: one second = 1000 milliseconds.
     * Contents remain event driven; this polls only parent object identity. */
    return CreateTimerQueueTimer(&file_watches.timer, NULL, watch_refresh_signal, file_watches.event,
                                 1000, 1000, WT_EXECUTEDEFAULT) != 0;
}
static bool watch_arm(file_parent_watch *parent)
{
    if (parent->wait) {
        (void)UnregisterWaitEx(parent->wait, INVALID_HANDLE_VALUE); parent->wait = NULL;
    }
    ResetEvent(parent->event);
    memset(&parent->io, 0, sizeof parent->io);
    parent->io.hEvent = parent->event;
    parent->armed = ReadDirectoryChangesW(parent->handle, parent->buffer, sizeof parent->buffer,
        FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SECURITY, NULL, &parent->io, NULL) != 0;
    if (!parent->armed) return false;
    return RegisterWaitForSingleObject(&parent->wait, parent->event, watch_signal,
        file_watches.event, INFINITE, WT_EXECUTEONLYONCE) != 0;
}
static void watch_parent_close(file_parent_watch *parent)
{
    if (parent->wait) (void)UnregisterWaitEx(parent->wait, INVALID_HANDLE_VALUE);
    if (parent->armed) {
        DWORD count;
        (void)CancelIoEx(parent->handle, &parent->io);
        (void)GetOverlappedResult(parent->handle, &parent->io, &count, TRUE);
    }
    CloseHandle(parent->event); CloseHandle(parent->handle); free(parent);
}
static void watch_timer_stop_if_unused(void)
{
    if (file_watches.count || file_watches.path_count) return;
    for (size_t i = 0; i < FILE_WATCH_SLOTS; i++) if (file_watches.parents[i]) return;
    if (file_watches.timer) {
        (void)DeleteTimerQueueTimer(NULL, file_watches.timer, INVALID_HANDLE_VALUE); file_watches.timer = NULL;
    }
}
static void watch_release(file *f, int wd)
{
    if (wd < 0) return;
    for (size_t i = 0; i < file_watches.count; i++)
        if (file_watches.files[i] != f && file_watches.files[i]->iwd == wd) return;
    for (size_t i = 0; i < file_watches.path_count; i++)
        if (file_watches.paths[i]->wd == wd) return;
    file_parent_watch *parent = file_watches.parents[(size_t)wd];
    if (parent) watch_parent_close(parent);
    file_watches.parents[(size_t)wd] = NULL;
    watch_timer_stop_if_unused();
}
void file_watch_detach(file *f)
{
    if (f->watch_slot == SIZE_MAX) return;
    watch_release(f, f->iwd);
    size_t last = --file_watches.count;
    if (f->watch_slot != last) {
        file *moved = file_watches.files[last];
        file_watches.files[f->watch_slot] = moved; moved->watch_slot = f->watch_slot;
    }
    file_watches.files[last] = NULL;
    f->watch_slot = SIZE_MAX; f->ifd = -1; f->iwd = -1;
    watch_timer_stop_if_unused();
}
static int watch_parent(const char *path)
{
    os_once_run(&file_watches.once, watch_install);
    if (!file_watches.event) return watch_error(file_watches.error);
    os_dir dir = OS_DIR_INIT; os_stat st;
    if (os_dir_open(&dir, path) != OS_OK) return -1;
    if (os_stat_at(&dir, ".", 0, &st) != OS_OK) {
        DWORD error = GetLastError(); os_dir_close(&dir); return watch_error(error);
    }
    size_t vacant = FILE_WATCH_SLOTS;
    for (size_t i = 0; i < FILE_WATCH_SLOTS; i++) {
        file_parent_watch *parent = file_watches.parents[i];
        if (!parent) { if (vacant == FILE_WATCH_SLOTS) vacant = i; continue; }
        if (os_identity_equal(&parent->identity, &st.id) && parent->armed) {
            os_dir_close(&dir); return (int)i;
        }
    }
    if (vacant == FILE_WATCH_SLOTS) { os_dir_close(&dir); return watch_error(ERROR_TOO_MANY_OPEN_FILES); }
    file_parent_watch *parent = calloc(1, sizeof *parent);
    if (!parent) { os_dir_close(&dir); return watch_error(ERROR_NOT_ENOUGH_MEMORY); }
    /* Reopen the pinned object, never its replaceable final pathname. */
    WCHAR empty[] = L""; UNICODE_STRING name = {0, sizeof empty, empty};
    OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK io;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, os_dir_handle(&dir), NULL);
    NTSTATUS status = NtCreateFile(&parent->handle, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &oa, &io, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, FILE_DIRECTORY_FILE, NULL, 0);
    if (status < 0) { parent->handle = INVALID_HANDLE_VALUE; SetLastError(RtlNtStatusToDosError(status)); }
    DWORD error = GetLastError();
    if (parent->handle == INVALID_HANDLE_VALUE) { os_dir_close(&dir); free(parent); return watch_error(error); }
    os_file borrowed = {.handle = parent->handle}; os_stat actual;
    if (os_file_stat(&borrowed, &actual) != OS_OK || !os_identity_equal(&st.id, &actual.id)) {
        os_dir_close(&dir); CloseHandle(parent->handle); free(parent); return watch_error(ERROR_FILE_INVALID);
    }
    parent->identity = actual.id;
    os_dir_close(&dir);
    parent->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!parent->event || !watch_arm(parent) || !watch_timer_start()) {
        error = GetLastError(); watch_parent_close(parent); return watch_error(error);
    }
    file_watches.parents[vacant] = parent;
    return (int)vacant;
}
int file_watch_start(file *f)
{
    if (!file_prefix_ready(f)) return watch_error(ERROR_INVALID_PARAMETER);
    if (f->watch_slot == SIZE_MAX && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS)
        return watch_error(ERROR_TOO_MANY_OPEN_FILES);
    char dir[OS_PATH_MAX];
    file_split_path(f->path, dir, sizeof dir, f->watch_name, sizeof f->watch_name);
    int wd = watch_parent(dir);
    f->watch_refresh = wd < 0;
    if (wd < 0) {
        DWORD error = GetLastError();
        int old = f->iwd; f->iwd = -1;
        if (old >= 0) { f->check_requested = 1; watch_release(f, old); }
        if (error == ERROR_INVALID_FUNCTION && os_path_is_wsl(f->path)) {
            /* No registration and no timer: focus/save/bulk checks remain
             * authoritative. Distinguish this capability from setup errors. */
            f->watch_refresh = 0;
            SetLastError(ERROR_SUCCESS); return FILE_WATCH_UNSUPPORTED;
        }
        return watch_error(error);
    }
    if (f->iwd != wd) {
        if (f->iwd >= 0) f->check_requested = 1;
        watch_release(f, f->iwd);
    }
    f->iwd = wd; f->ifd = 0; /* borrowed service token; native event via accessor */
    if (f->watch_slot == SIZE_MAX) {
        f->watch_slot = file_watches.count;
        file_watches.files[file_watches.count++] = f;
    }
    return f->ifd;
}
static bool watch_matches(const FILE_NOTIFY_INFORMATION *notice, const char *name)
{
    wchar_t wide[OS_NAME_MAX];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, OS_NAME_MAX);
    return n && CompareStringOrdinal(wide, n - 1, notice->FileName,
        (int)(notice->FileNameLength / sizeof(wchar_t)), TRUE) == CSTR_EQUAL;
}
static bool watch_noticed(const void *buffer, DWORD length, const char *name)
{
    size_t at = 0;
    while (length - at >= offsetof(FILE_NOTIFY_INFORMATION, FileName)) {
        const FILE_NOTIFY_INFORMATION *notice = (const void *)((const uint8_t *)buffer + at);
        size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (notice->FileNameLength > length - at - header || notice->FileNameLength % sizeof(wchar_t)) return true;
        if (watch_matches(notice, name)) return true;
        if (!notice->NextEntryOffset) return false;
        if (notice->NextEntryOffset < header || notice->NextEntryOffset > length - at) return true;
        at += notice->NextEntryOffset;
    }
    return at != length;
}
void file_watch_dispatch(void)
{
    if (!file_watches.event) return;
    /* Reset before observing completions; a concurrent completion re-signals. */
    ResetEvent(file_watches.event);
    /* Timer/focus dispatch rebinds every member whose parent pathname now
     * denotes another object, including path-only configuration watches. */
    if (atomic_exchange_explicit(&file_watches.refresh, false, memory_order_acquire)) {
        for (size_t i = 0; i < file_watches.count; i++) (void)file_watch_start(file_watches.files[i]);
        for (size_t i = 0; i < file_watches.path_count; i++)
            (void)file_watch_path_start(file_watches.paths[i], file_watches.paths[i]->path);
    }
    for (size_t wd = 0; wd < FILE_WATCH_SLOTS; wd++) {
        file_parent_watch *parent = file_watches.parents[wd];
        if (!parent || !parent->armed || !HasOverlappedIoCompleted(&parent->io)) continue;
        DWORD count = 0;
        bool completed = GetOverlappedResult(parent->handle, &parent->io, &count, FALSE) != 0;
        parent->armed = false;
        /* An overflow (zero bytes) or failed request invalidates all members. */
        bool all = !completed || !count;
        for (size_t i = 0; i < file_watches.count; i++) {
            file *member = file_watches.files[i];
            if (member->iwd != (int)wd) continue;
            if (all || watch_noticed(parent->buffer, count, member->watch_name)) member->check_requested = 1;
        }
        for (size_t i = 0; i < file_watches.path_count; i++) {
            file_path_watch *member = file_watches.paths[i];
            if (member->wd != (int)wd) continue;
            char dir[OS_PATH_MAX], name[OS_NAME_MAX];
            file_split_path(member->path, dir, sizeof dir, name, sizeof name);
            if (all || watch_noticed(parent->buffer, count, name)) member->pending = true;
        }
        bool rearmed = watch_arm(parent);
        if (!rearmed) {
            for (size_t i = 0; i < file_watches.count; i++)
                if (file_watches.files[i]->iwd == (int)wd) {
                    file_watches.files[i]->watch_refresh = 1;
                    file_watches.files[i]->check_requested = 1;
                    file_watches.files[i]->iwd = -1;
                }
            for (size_t i = 0; i < file_watches.path_count; i++)
                if (file_watches.paths[i]->wd == (int)wd) {
                    file_watches.paths[i]->wd = -1; file_watches.paths[i]->pending = true;
                }
            /* No requests remain; release the failed native watch. */
            watch_parent_close(parent); file_watches.parents[wd] = NULL;
        }
    }
    for (size_t i = 0; i < file_watches.count; i++) file_watch_check_pump(file_watches.files[i]);
}
int file_watch_poll(file *f)
{
    file_watch_dispatch();
    /* Explicit focus/bulk polling must work without a notification member.
     * Coalesce through the existing worker/mailbox path; no focused timer. */
    if (f->iwd < 0 && os_path_is_wsl(f->path) && !file_changed(f)) f->check_requested = 1;
    file_watch_check_pump(f); return file_changed(f);
}
int file_watch_path_start(file_path_watch *member, const char *path)
{
    if (!member || !path || !os_path_is_absolute(path) || strlen(path) >= OS_PATH_MAX ||
        (member->registered && member->path != path)) return watch_error(ERROR_INVALID_PARAMETER);
    if (!member->registered && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS)
        return watch_error(ERROR_TOO_MANY_OPEN_FILES);
    char dir[OS_PATH_MAX], name[OS_NAME_MAX];
    file_split_path(path, dir, sizeof dir, name, sizeof name);
    if (!*name) return watch_error(ERROR_INVALID_NAME);
    int wd = watch_parent(dir);
    if (wd < 0) {
        DWORD error = GetLastError();
        if (member->registered && member->wd >= 0) {
            int old = member->wd; member->wd = -1; member->pending = true; watch_release(NULL, old);
        }
        if (error == ERROR_INVALID_FUNCTION && os_path_is_wsl(path)) {
            SetLastError(ERROR_SUCCESS); return FILE_WATCH_UNSUPPORTED;
        }
        return watch_error(error);
    }
    int old = member->registered ? member->wd : -1;
    member->wd = wd; member->path = path;
    if (old != wd && member->registered) member->pending = true;
    if (old != wd) watch_release(NULL, old);
    if (!member->registered) {
        member->slot = file_watches.path_count;
        file_watches.paths[file_watches.path_count++] = member; member->registered = true;
    }
    return 0;
}
void file_watch_path_stop(file_path_watch *member)
{
    if (!member || !member->registered) return;
    int wd = member->wd;
    size_t last = --file_watches.path_count;
    if (member->slot != last) {
        file_path_watch *moved = file_watches.paths[last];
        file_watches.paths[member->slot] = moved; moved->slot = member->slot;
    }
    file_watches.paths[last] = NULL;
    memset(member, 0, sizeof *member); member->wd = -1;
    watch_release(NULL, wd);
    watch_timer_stop_if_unused();
}
bool file_watch_path_take(file_path_watch *member)
{
    if (!member || !member->registered) return false;
    bool pending = member->pending; member->pending = false; return pending;
}

int file_native_error(os_err error)
{
    DWORD native = error == OS_E_NAMETOOLONG ? ERROR_FILENAME_EXCED_RANGE
                 : error == OS_E_RANGE ? ERROR_FILE_TOO_LARGE
                 : error == OS_E_NOTSUP ? ERROR_NOT_SUPPORTED : ERROR_WRITE_FAULT;
    SetLastError(native);
    return (int)native;
}

/* EFS needs a separate encryption-preservation transaction. */
int file_metadata_check(const os_file *source)
{
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(os_file_handle(source), FileBasicInfo, &basic, sizeof basic)) return -1;
    if (basic.FileAttributes & FILE_ATTRIBUTE_ENCRYPTED) return watch_error(ERROR_FILE_ENCRYPTED);
    return 0;
}

/* Query the retained object, not a pathname that could now name another file.
 * NtQueryInformationFile supplies the actual returned byte length. A truncated
 * list is retried with a larger buffer, never treated as a complete list. */
static int streams_query(save_transaction *tx, void **out, size_t *length)
{
    ULONG capacity = sizeof(FILE_STREAM_INFO);
    for (;;) {
        if (tx->args.stop && tx->args.stop(tx->args.stop_ctx)) return FILE_ERR_CANCELLED;
        void *buffer = malloc(capacity);
        if (!buffer) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FILE_ERR_IO; }
        IO_STATUS_BLOCK io = {0};
        NTSTATUS status = NtQueryInformationFile(os_file_handle(&tx->metadata_fd), &io,
            buffer, capacity, FileStreamInformation);
        if (status == (NTSTATUS)0xc000000du || status == (NTSTATUS)0xc0000002u ||
            status == (NTSTATUS)0xc0000003u || status == (NTSTATUS)0xc00000bbu) {
            /* INVALID_PARAMETER, NOT_IMPLEMENTED, INVALID_INFO_CLASS, NOT_SUPPORTED */
            /* These volumes report no streams; ordinary overwrites still work. */
            free(buffer); *out = NULL; *length = 0;
            return FILE_OK;
        }
        if (status == (NTSTATUS)0x80000005u || status == (NTSTATUS)0xc0000023u ||
            status == (NTSTATUS)0xc0000004u) { /* OVERFLOW, TOO_SMALL, LENGTH_MISMATCH */
            free(buffer);
            if (capacity > MAXDWORD / 2u) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FILE_ERR_IO; }
            capacity *= 2u;
            continue;
        }
        if (status < 0 || io.Information > capacity) {
            free(buffer);
            SetLastError(status < 0 ? RtlNtStatusToDosError(status) : ERROR_INVALID_DATA);
            return FILE_ERR_IO;
        }
        *out = buffer; *length = (size_t)io.Information;
        return FILE_OK;
    }
}
static int stream_open(const os_file *base, const FILE_STREAM_INFO *entry, bool write, os_file *out)
{
    /* The API supplies a counted UTF-16 name; no terminator or fixed name cap.
     * UNICODE_STRING's USHORT length is the native open boundary. */
    UNICODE_STRING name = {(USHORT)entry->StreamNameLength, (USHORT)entry->StreamNameLength,
                           (WCHAR *)entry->StreamName};
    OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK io;
    InitializeObjectAttributes(&oa, &name, 0, os_file_handle(base), NULL);
    HANDLE handle = INVALID_HANDLE_VALUE;
    NTSTATUS status = NtCreateFile(&handle, (write ? FILE_GENERIC_WRITE : FILE_GENERIC_READ),
        &oa, &io, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_DELETE,
        write ? FILE_CREATE : FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (status < 0) return watch_error(RtlNtStatusToDosError(status));
    out->handle = handle;
    return 0;
}
static int stream_copy(save_transaction *tx, const FILE_STREAM_INFO *entry, uint8_t *buffer, size_t capacity)
{
    os_file source = OS_FILE_INIT, target = OS_FILE_INIT;
    int rc = FILE_ERR_IO;
    DWORD error = ERROR_SUCCESS;
    if (stream_open(&tx->metadata_fd, entry, false, &source) != 0 ||
        stream_open(&tx->fd, entry, true, &target) != 0) goto done;
    /* Stream opens are relative to the captured file objects, including when
     * the parent or target pathname moves during the copy. */
    uint64_t offset = 0, size = (uint64_t)entry->StreamSize.QuadPart;
    while (offset < size) {
        if (tx->args.stop && tx->args.stop(tx->args.stop_ctx)) { rc = FILE_ERR_CANCELLED; goto done; }
        size_t n = size - offset < capacity ? (size_t)(size - offset) : capacity, got = 0;
        os_err result = os_file_pread(&source, buffer, n, offset, &got);
        if (result == OS_E_INTR) continue;
        if (result != OS_OK) goto done;
        if (!got) { rc = FILE_ERR_CHANGED; goto done; }
        size_t at = 0;
        while (at < got) {
            if (tx->args.stop && tx->args.stop(tx->args.stop_ctx)) { rc = FILE_ERR_CANCELLED; goto done; }
            size_t written = 0;
            result = os_file_pwrite_once(&target, buffer + at, got - at, offset + at, &written);
            if (result == OS_E_INTR) continue;
            if (result != OS_OK) goto done;
            if (!written) { SetLastError(ERROR_WRITE_FAULT); goto done; }
            at += written;
        }
        offset += got;
    }
    /* Detect growth/truncation even if default-stream timestamps are deferred. */
    os_stat st;
    if (os_file_stat(&source, &st) != OS_OK) goto done;
    if (st.size != size) { rc = FILE_ERR_CHANGED; goto done; }
    if (tx->args.stop && tx->args.stop(tx->args.stop_ctx)) { rc = FILE_ERR_CANCELLED; goto done; }
    if (os_file_sync(&target) != OS_OK) goto done;
    rc = FILE_OK;
done:
    error = GetLastError();
    os_file_close(&target); os_file_close(&source);
    SetLastError(error);
    return rc;
}
int file_streams_copy(save_transaction *tx)
{
    if (!os_file_is_open(&tx->metadata_fd)) return FILE_OK;
    void *list = NULL; size_t length = 0;
    uint8_t *buffer = NULL;
    int rc = streams_query(tx, &list, &length);
    if (rc != FILE_OK) goto done;
    size_t at = 0, head = offsetof(FILE_STREAM_INFO, StreamName);
    while (at < length) {
        if (tx->args.stop && tx->args.stop(tx->args.stop_ctx)) { rc = FILE_ERR_CANCELLED; goto done; }
        if (length - at < head) goto invalid;
        FILE_STREAM_INFO *entry = (void *)((BYTE *)list + at);
        size_t n = entry->StreamNameLength;
        if (n > length - at - head || n % sizeof(WCHAR) || n > USHRT_MAX ||
            entry->StreamSize.QuadPart < 0) goto invalid;
        size_t units = n / sizeof(WCHAR);
        /* Both forms name the default stream already written from the snapshot. */
        bool default_stream = !units || (units == 7 &&
            CompareStringOrdinal(entry->StreamName, 7, L"::$DATA", 7, TRUE) == CSTR_EQUAL);
        if (!default_stream) {
            if (units < 7 || entry->StreamName[0] != L':' ||
                memcmp(entry->StreamName + units - 6, L":$DATA", 6 * sizeof(WCHAR))) goto invalid;
            /* Refuse malformed/foreign stream types rather than lose them. */
            for (size_t i = 1; i < units - 6; i++)
                if (!entry->StreamName[i] || entry->StreamName[i] == L':' ||
                    entry->StreamName[i] == L'/' || entry->StreamName[i] == L'\\') goto invalid;
            /* Same transfer bound as default-stream FILE_WRITE_CHUNK:
             * FILE_PREFIX_MAX / 4 = 256 KiB. Save worker only. */
            size_t capacity = FILE_PREFIX_MAX / 4u;
            if (!buffer) buffer = malloc(capacity);
            if (!buffer) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); rc = FILE_ERR_IO; goto done; }
            rc = stream_copy(tx, entry, buffer, capacity);
            if (rc != FILE_OK) goto done;
        }
        if (!entry->NextEntryOffset) break;
        if (entry->NextEntryOffset < head + n || entry->NextEntryOffset % _Alignof(FILE_STREAM_INFO) ||
            entry->NextEntryOffset > length - at - head) goto invalid;
        at += entry->NextEntryOffset;
    }
    rc = FILE_OK;
    goto done;
invalid:
    SetLastError(ERROR_INVALID_DATA); rc = FILE_ERR_IO;
done:;
    DWORD error = GetLastError();
    /* A vanished/changed stream can make an open/read fail. Give the caller
     * the transaction's normal CHANGED result if the captured target changed;
     * genuine copy errors retain their native diagnostic. Success is checked
     * again by metadata_apply and the existing pre-rename pathname check. */
    if (rc == FILE_ERR_IO) {
        os_stat st; file_id observed;
        if (os_file_stat(&tx->metadata_fd, &st) == OS_OK) {
            file_id_from_stat(&observed, &st);
            if (file_id_diff(&tx->metadata_id, &observed)) rc = FILE_ERR_CHANGED;
        }
        os_err result = os_stat_at(&tx->dfd, tx->base, OS_STAT_NOFOLLOW, &st);
        if (result == OS_E_NOENT || result == OS_E_NOTDIR) rc = FILE_ERR_CHANGED;
        else if (result == OS_OK) {
            file_id_from_stat(&observed, &st);
            if (file_id_diff(&tx->metadata_id, &observed)) rc = FILE_ERR_CHANGED;
        }
    }
    free(buffer); free(list);
    if (rc == FILE_ERR_IO) tx->args.err_no = (int)error;
    SetLastError(error);
    return rc;
}

/* Non-audit access-control state is queryable with READ_CONTROL. Unknown
 * resource/policy ACEs are refused; labels are copied with the descriptor. */
#define FILE_SECURITY_INFO (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | \
                            LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION | \
                            PROCESS_TRUST_LABEL_SECURITY_INFORMATION | ACCESS_FILTER_SECURITY_INFORMATION)
static DWORD security_capture(HANDLE handle, PSECURITY_DESCRIPTOR *sd)
{
    PACL sacl = NULL;
    DWORD error = GetSecurityInfo(handle, SE_FILE_OBJECT, FILE_SECURITY_INFO, NULL, NULL, NULL, &sacl, sd);
    if (error != ERROR_SUCCESS) return error;
    if (sacl) for (DWORD i = 0; i < sacl->AceCount; i++) {
        ACE_HEADER *ace = NULL;
        if (!GetAce(sacl, i, (void **)&ace)) return GetLastError();
        if (ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) return ERROR_NOT_SUPPORTED;
    }
    return ERROR_SUCCESS;
}
static bool security_acl_equal(PACL a, PACL b)
{ return (!a && !b) || (a && b && a->AclSize == b->AclSize && !memcmp(a, b, a->AclSize)); }
static bool security_equal(PSECURITY_DESCRIPTOR a, PSECURITY_DESCRIPTOR b)
{
    PSID ao = NULL, bo = NULL, ag = NULL, bg = NULL; PACL ad = NULL, bd = NULL, as = NULL, bs = NULL;
    BOOL present, def; SECURITY_DESCRIPTOR_CONTROL ac = 0, bc = 0; DWORD rev;
    if (!GetSecurityDescriptorOwner(a, &ao, &def) || !GetSecurityDescriptorOwner(b, &bo, &def) ||
        !GetSecurityDescriptorGroup(a, &ag, &def) || !GetSecurityDescriptorGroup(b, &bg, &def) ||
        !GetSecurityDescriptorDacl(a, &present, &ad, &def) || !GetSecurityDescriptorDacl(b, &present, &bd, &def) ||
        !GetSecurityDescriptorSacl(a, &present, &as, &def) || !GetSecurityDescriptorSacl(b, &present, &bs, &def) ||
        !GetSecurityDescriptorControl(a, &ac, &rev) || !GetSecurityDescriptorControl(b, &bc, &rev)) return false;
    SECURITY_DESCRIPTOR_CONTROL mask = SE_DACL_PROTECTED | SE_DACL_PRESENT | SE_SACL_PROTECTED;
    return ao && bo && ag && bg && EqualSid(ao, bo) && EqualSid(ag, bg) &&
           security_acl_equal(ad, bd) && security_acl_equal(as, bs) && (ac & mask) == (bc & mask);
}
os_err file_pread_once(os_file *source, void *bytes, size_t n, uint64_t offset, size_t *got)
{ return os_file_pread(source, bytes, n, offset, got); }
int file_save_observer(const os_file *writer, os_file *observer)
{
    HANDLE handle = ReOpenFile(os_file_handle(writer), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    observer->handle = handle; return 0;
}
int file_save_temp(save_transaction *tx)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    /* No inherited or creator-group ACE can expose an empty creation and
     * leave a retained handle with broader rights when bytes arrive. */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor, NULL)) return -1;
    DWORD error = ERROR_SUCCESS;
    WCHAR name[OS_NAME_MAX + 1];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tx->tmp, -1, name, OS_NAME_MAX + 1);
    if (!n) { error = GetLastError(); LocalFree(descriptor); return watch_error(error); }
    UNICODE_STRING us = {(USHORT)((size_t)(n - 1) * sizeof(WCHAR)), (USHORT)((size_t)n * sizeof(WCHAR)), name};
    OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK io;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, os_dir_handle(&tx->dfd), descriptor);
    HANDLE handle = INVALID_HANDLE_VALUE;
    ACCESS_MASK access = FILE_GENERIC_WRITE | READ_CONTROL | WRITE_DAC;
    NTSTATUS status = NtCreateFile(&handle, access, &oa, &io, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_CREATE,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (status < 0) error = RtlNtStatusToDosError(status);
    LocalFree(descriptor);
    if (error != ERROR_SUCCESS) return watch_error(error);
    tx->fd.handle = handle;
    /* Keep ownership of this name even if subsequent descriptor validation
     * fails, so transaction_discard can remove only our own creation. */
    return 0;
}

static int metadata_copy(const os_file *source, os_file *target, const file_id *identity, bool final)
{
    (void)identity;
    PSECURITY_DESCRIPTOR original = NULL, current = NULL;
    DWORD error = security_capture(os_file_handle(source), &original);
    if (error == ERROR_SUCCESS) error = security_capture(os_file_handle(target), &current);
    if (error == ERROR_SUCCESS && !security_equal(original, current)) {
        PSID old_owner = NULL, owner = NULL, old_group = NULL, group = NULL;
        PACL old_label = NULL, label = NULL; BOOL present, def;
        if (!GetSecurityDescriptorOwner(original, &old_owner, &def) || !GetSecurityDescriptorOwner(current, &owner, &def) ||
            !GetSecurityDescriptorGroup(original, &old_group, &def) || !GetSecurityDescriptorGroup(current, &group, &def) ||
            !GetSecurityDescriptorSacl(original, &present, &old_label, &def) || !GetSecurityDescriptorSacl(current, &present, &label, &def))
            error = GetLastError();
        SECURITY_INFORMATION changes = 0;
        if (error == ERROR_SUCCESS) {
            if (!EqualSid(old_owner, owner)) changes |= OWNER_SECURITY_INFORMATION;
            if (!EqualSid(old_group, group)) changes |= GROUP_SECURITY_INFORMATION;
            if (!security_acl_equal(old_label, label)) changes |= LABEL_SECURITY_INFORMATION;
        }
        /* Owner/group/label need WRITE_OWNER only when they actually differ.
         * Reopen the actual object while its owner-only ACL still applies. */
        if (error == ERROR_SUCCESS && changes) {
            HANDLE metadata = ReOpenFile(os_file_handle(target), WRITE_OWNER,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
            if (metadata == INVALID_HANDLE_VALUE) error = GetLastError();
            else {
                NTSTATUS status = NtSetSecurityObject(metadata, changes, original);
                if (status < 0) error = RtlNtStatusToDosError(status);
                CloseHandle(metadata);
            }
        }
        /* Copy the captured ACL/control without requesting parent inheritance. */
        if (error == ERROR_SUCCESS) {
            NTSTATUS status = NtSetSecurityObject(os_file_handle(target), DACL_SECURITY_INFORMATION, original);
            if (status < 0) error = RtlNtStatusToDosError(status);
        }
        LocalFree(current); current = NULL;
        if (error == ERROR_SUCCESS) error = security_capture(os_file_handle(target), &current);
        if (error == ERROR_SUCCESS && !security_equal(original, current)) error = ERROR_NOT_SUPPORTED;
    }
    LocalFree(original); LocalFree(current);
    FILE_BASIC_INFO basic;
    if (error == ERROR_SUCCESS && !GetFileInformationByHandleEx(os_file_handle(source), FileBasicInfo, &basic, sizeof basic))
        error = GetLastError();
    if (error == ERROR_SUCCESS) {
        FILE_BASIC_INFO attributes = {0};
        attributes.FileAttributes = basic.FileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE |
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        /* New stream handles must still be creatable on a read-only target.
         * The final application restores READONLY before flush/replacement. */
        if (!final) attributes.FileAttributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
        if (!attributes.FileAttributes) attributes.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileInformationByHandle(os_file_handle(target), FileBasicInfo, &attributes, sizeof attributes)) error = GetLastError();
    }
    return error == ERROR_SUCCESS ? 0 : watch_error(error);
}
int file_metadata_copy(const os_file *source, os_file *target, const file_id *identity)
{ return metadata_copy(source, target, identity, true); }
int file_metadata_prepare(save_transaction *tx)
{
    if (!os_file_is_open(&tx->metadata_fd)) return 0;
    return metadata_copy(&tx->metadata_fd, &tx->fd, &tx->metadata_id, false);
}

#endif
