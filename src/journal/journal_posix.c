#if !defined(_WIN32)
#include "journal/private.h"
int journal_hook_file(const os_file *file) { return os_file_fd(file); }
int journal_hook_dir(const os_dir *dir) { return os_dir_fd(dir); }
/* Borrow descriptors at the Linux editor boundary; ownership stays with the
 * caller. The portable entry points own/use os_file and os_dir directly. */
int journal_session_create(const char *directory, char *path, size_t cap, int *lease)
{
    if(!lease) return JOURNAL_INVALID;
    os_file file=OS_FILE_INIT;
    int rc=journal_session_create_file(directory,path,cap,&file);
    *lease=os_file_fd(&file); return rc;
}
int journal_session_release(const char *path, int lease)
{
    os_file file=os_file_from_fd(lease);
    return journal_session_release_file(path,&file);
}
int journal_session_absent(int dfd, const char *leaf)
{
    os_dir dir={.fd=dfd};
    return journal_session_absent_at(&dir,leaf);
}
#endif
