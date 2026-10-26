/* Native notifications and save metadata; deliberately outside os.h. */
#if defined(_WIN32)
#include "file/file_internal.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <stdlib.h>
#include <string.h>

/* Same member bound as Linux: existing editor reservations, live + retained. */
#define FILE_WATCH_SLOTS (4096u + 4096u)
typedef struct file_parent_watch {
    HANDLE handle;
    OVERLAPPED io;
    os_identity identity;
    /* Same bounded dispatch buffer as the Linux service. */
    DWORD buffer[4096 / sizeof(DWORD)];
    bool armed;
} file_parent_watch;
typedef struct file_watch_service {
    os_once once;
    HANDLE event;
    DWORD error;
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
static bool watch_arm(file_parent_watch *parent)
{
    memset(&parent->io, 0, sizeof parent->io);
    parent->io.hEvent = file_watches.event;
    parent->armed = ReadDirectoryChangesW(parent->handle, parent->buffer, sizeof parent->buffer,
        FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SECURITY, NULL, &parent->io, NULL) != 0;
    return parent->armed;
}
static void watch_parent_close(file_parent_watch *parent)
{
    if (parent->armed) {
        (void)CancelIoEx(parent->handle, &parent->io);
        /* All requests share an event, so wait on this request's completion
         * state rather than treating another directory's wake as completion. */
        while (!HasOverlappedIoCompleted(&parent->io)) os_yield();
    }
    CloseHandle(parent->handle); free(parent);
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
    /* Ask the pinned directory for its native, long-path-qualified name. */
    wchar_t native[OS_PATH_MAX];
    DWORD n = GetFinalPathNameByHandleW(os_dir_handle(&dir), native, OS_PATH_MAX, FILE_NAME_NORMALIZED);
    DWORD error = GetLastError(); os_dir_close(&dir);
    if (!n || n >= OS_PATH_MAX) return watch_error(n ? ERROR_FILENAME_EXCED_RANGE : error);
    file_parent_watch *parent = calloc(1, sizeof *parent);
    if (!parent) return watch_error(ERROR_NOT_ENOUGH_MEMORY);
    parent->identity = st.id;
    parent->handle = CreateFileW(native, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (parent->handle == INVALID_HANDLE_VALUE) { error = GetLastError(); free(parent); return watch_error(error); }
    if (!watch_arm(parent)) { error = GetLastError(); watch_parent_close(parent); return watch_error(error); }
    file_watches.parents[vacant] = parent;
    return (int)vacant;
}
int file_watch_start(file *f)
{
    if (!file_prefix_ready(f)) return watch_error(ERROR_INVALID_PARAMETER);
    if (f->ifd >= 0 && f->iwd >= 0 && !f->watch_refresh) return f->ifd;
    if (f->watch_slot == SIZE_MAX && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS)
        return watch_error(ERROR_TOO_MANY_OPEN_FILES);
    char dir[OS_PATH_MAX];
    file_split_path(f->path, dir, sizeof dir, f->watch_name, sizeof f->watch_name);
    int wd = watch_parent(dir);
    f->watch_refresh = wd < 0;
    if (wd < 0) return -1;
    if (f->iwd != wd) watch_release(f, f->iwd);
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
            CloseHandle(parent->handle); free(parent); file_watches.parents[wd] = NULL;
        }
    }
    for (size_t i = 0; i < file_watches.count; i++) file_watch_check_pump(file_watches.files[i]);
}
int file_watch_poll(file *f)
{
    file_watch_dispatch(); file_watch_check_pump(f); return file_changed(f);
}
int file_watch_path_start(file_path_watch *member, const char *path)
{
    if (!member || !path || !os_path_is_absolute(path) || strlen(path) >= OS_PATH_MAX ||
        (member->registered && member->path != path)) return watch_error(ERROR_INVALID_PARAMETER);
    if (member->registered && member->wd >= 0) return 0;
    if (!member->registered && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS)
        return watch_error(ERROR_TOO_MANY_OPEN_FILES);
    char dir[OS_PATH_MAX], name[OS_NAME_MAX];
    file_split_path(path, dir, sizeof dir, name, sizeof name);
    if (!*name) return watch_error(ERROR_INVALID_NAME);
    int wd = watch_parent(dir);
    if (wd < 0) return -1;
    member->wd = wd; member->path = path;
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
}
bool file_watch_path_take(file_path_watch *member)
{
    if (!member || !member->registered) return false;
    bool pending = member->pending; member->pending = false; return pending;
}

int file_native_error(os_err error)
{
    DWORD native = error == OS_E_NAMETOOLONG ? ERROR_FILENAME_EXCED_RANGE
                 : error == OS_E_RANGE ? ERROR_FILE_TOO_LARGE : ERROR_WRITE_FAULT;
    SetLastError(native);
    return (int)native;
}

int file_metadata_copy(const os_file *source, os_file *target, const file_id *identity)
{
    (void)identity;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PSID owner = NULL, group = NULL; PACL dacl = NULL;
    SECURITY_INFORMATION info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    DWORD error = GetSecurityInfo(os_file_handle(source), SE_FILE_OBJECT, info,
                                  &owner, &group, &dacl, NULL, &descriptor);
    if (error != ERROR_SUCCESS) return watch_error(error);
    /* GENERIC_WRITE alone (the save data handle) does not grant WRITE_DAC or
     * WRITE_OWNER. Reopen this same object with metadata rights; never widen
     * the data open or resolve the target pathname again. */
    HANDLE metadata = ReOpenFile(os_file_handle(target), READ_CONTROL | WRITE_DAC | WRITE_OWNER | FILE_WRITE_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (metadata == INVALID_HANDLE_VALUE) { error = GetLastError(); LocalFree(descriptor); return watch_error(error); }
    SECURITY_DESCRIPTOR_CONTROL control; DWORD revision;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) error = GetLastError();
    else {
        info |= control & SE_DACL_PROTECTED ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
        error = SetSecurityInfo(metadata, SE_FILE_OBJECT, info, owner, group, dacl, NULL);
    }
    LocalFree(descriptor);
    FILE_BASIC_INFO basic;
    if (error == ERROR_SUCCESS && !GetFileInformationByHandleEx(os_file_handle(source), FileBasicInfo, &basic, sizeof basic))
        error = GetLastError();
    if (error == ERROR_SUCCESS) {
        /* Data timestamps belong to the newly written generation. Attributes
         * writable through FileBasicInfo are copied; compression/encryption
         * are filesystem operations, not assignable DOS attributes. */
        FILE_BASIC_INFO attributes = {0};
        attributes.FileAttributes = basic.FileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE |
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        if (!attributes.FileAttributes) attributes.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileInformationByHandle(metadata, FileBasicInfo, &attributes, sizeof attributes)) error = GetLastError();
    }
    CloseHandle(metadata);
    return error == ERROR_SUCCESS ? 0 : watch_error(error);
}
#endif
