#ifndef EDIT_DEPENDENCY_WIN_FIXTURE_H
#define EDIT_DEPENDENCY_WIN_FIXTURE_H
#include "os/os.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>

/* Windows processes create all filesystem fixtures on NTFS, under TEMP. */
static inline int dep_temp(char *out, size_t cap)
{
    char root[OS_PATH_MAX]; uint64_t nonce;
    if (os_temp_dir(root, sizeof root) || os_random(&nonce, sizeof nonce)) return -1;
    int n = snprintf(out, cap, "%s/sublimite-test-deps-%llx", root, (unsigned long long)nonce);
    return n < 0 || (size_t)n >= cap || os_mkdir(out) ? -1 : 0;
}
static inline int dep_put(const char *path, const void *bytes, size_t length)
{
    os_file f = OS_FILE_INIT;
    if (os_file_open(&f, path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC)) return -1;
    size_t at = 0;
    while (at < length) {
        size_t done;
        if (os_file_write(&f, (const uint8_t *)bytes + at, length - at, &done) || !done) {
            os_file_close(&f); return -1;
        }
        at += done;
    }
    os_file_close(&f); return 0;
}
static inline int dep_remove(const char *path)
{
    os_stat st;
    if (os_stat_path(path, OS_STAT_NOFOLLOW, &st)) return -1;
    if (st.kind == OS_KIND_LINK) {
        WCHAR wide[OS_PATH_MAX];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, OS_PATH_MAX)) return -1;
        return RemoveDirectoryW(wide) ? 0 : -1; /* junction itself, never its target */
    }
    if (st.kind != OS_KIND_DIR) return os_unlink(path) ? -1 : 0;
    os_dir d = OS_DIR_INIT; os_dir_iter it;
    if (os_dir_open(&d, path)) return -1;
    if (os_dir_iter_open(&it, &d)) { os_dir_close(&d); return -1; }
    const char *name; os_err rc; int result = 0;
    while ((rc = os_dir_next(&it, &name)) == OS_OK && name) {
        char child[OS_PATH_MAX]; int n = snprintf(child, sizeof child, "%s/%s", path, name);
        if (n < 0 || (size_t)n >= sizeof child || dep_remove(child)) { result = -1; break; }
    }
    if (rc) result = -1;
    os_dir_iter_close(&it); os_dir_close(&d);
    if (os_rmdir(path)) result = -1;
    return result;
}
/* Junctions exercise name-surrogate reparse skipping without symlink privilege. */
static inline int dep_junction(const char *target, const char *link)
{
    WCHAR wide[OS_PATH_MAX], resolved[OS_PATH_MAX], target_w[OS_PATH_MAX];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, target_w, OS_PATH_MAX) ||
        !GetFullPathNameW(target_w, OS_PATH_MAX, resolved, NULL) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, link, -1, wide, OS_PATH_MAX)) return -1;
    if (!CreateDirectoryW(wide, NULL)) return -1;
    HANDLE h = CreateFileW(wide, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(wide); return -1; }
    struct {
        DWORD tag; WORD size, reserved;
        WORD sub_offset, sub_length, print_offset, print_length;
        WCHAR path[OS_PATH_MAX];
    } data = {0};
    size_t len = wcslen(resolved);
    /* (P) NT substitute prefix is four WCHARs; the empty print name follows NUL. */
    if (len + 6u > OS_PATH_MAX) { CloseHandle(h); RemoveDirectoryW(wide); return -1; }
    memcpy(data.path, L"\\??\\", 4u * sizeof(WCHAR));
    memcpy(data.path + 4, resolved, (len + 1u) * sizeof(WCHAR));
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.sub_length = (WORD)((len + 4u) * sizeof(WCHAR));
    data.print_offset = (WORD)(data.sub_length + sizeof(WCHAR));
    data.size = (WORD)(8u + data.print_offset + sizeof(WCHAR));
    DWORD done;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &data, (DWORD)data.size + 8u, NULL, 0, &done, NULL);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(wide);
    return ok ? 0 : -1;
}
#endif
