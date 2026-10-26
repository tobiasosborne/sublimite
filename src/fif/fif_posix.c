#if !defined(_WIN32)
#include "fif/private.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int fif_iter_open(fif_iterator *it, const os_dir *dir)
{
    int fd = openat(os_dir_fd(dir), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    it->dir = fdopendir(fd);
    if (!it->dir) { int saved = errno; (void)close(fd); errno = saved; return -1; }
    return 0;
}
int fif_iter_next(fif_iterator *it, const char **name)
{
    errno = 0;
    struct dirent *entry = readdir(it->dir);
    *name = entry ? entry->d_name : NULL;
    return !entry && errno ? -1 : 0;
}
int fif_iter_close(fif_iterator *it) { return closedir(it->dir); }
int fif_close_file(os_file *file)
{
    int fd = os_file_fd(file);
    *file = (os_file)OS_FILE_INIT;
    return close(fd);
}
os_err fif_stat_dir(const os_dir *dir, os_stat *st)
{
    /* Borrow only for fstat, preserving the old dirfd/fstat acquisition. */
    os_file borrowed = os_file_from_fd(os_dir_fd(dir));
    return os_file_stat(&borrowed, st);
}
#endif
