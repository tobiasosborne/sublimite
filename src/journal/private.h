#ifndef EDIT_JOURNAL_PRIVATE_H
#define EDIT_JOURNAL_PRIVATE_H
#include "journal/journal.h"
#include "os/os.h"
#include <string.h>

/* Platform bridge for the legacy POSIX fault-injection seam. Windows hooks
 * may inject a result; -1 is not a native Windows handle. */
int journal_hook_file(const os_file *file);
int journal_hook_dir(const os_dir *dir);
int journal_scan_open(os_file file, journal_visit visit, void *ctx,
                      journal_replay_result *result);
os_err journal_temp(os_file *out, char *path, size_t suffix);
char *journal_canonical(const char *path, char *out, size_t cap);
static inline void journal_slashes(char *path)
{
#if defined(_WIN32)
    for(char *p=path;*p;p++) if(*p=='\\') *p='/';
#else
    (void)path;
#endif
}
/* Keep filesystem roots when splitting an absolute leaf from its parent. */
static inline void journal_parent(char *path, char *slash)
{
    bool root=slash==path;
#if defined(_WIN32)
    if(slash==path+2 && path[1]==':') root=true;
#endif
    if(!slash) strcpy(path,".");
    else if(root) slash[1]=0;
    else *slash=0;
}
static inline bool journal_same_id(const os_stat *a, const os_stat *b)
{ return os_identity_equal(&a->id,&b->id); }
static inline bool journal_same_stat(const os_stat *a, const os_stat *b)
{ return journal_same_id(a,b) && a->size==b->size && a->mtime_ns==b->mtime_ns && a->ctime_ns==b->ctime_ns; }
#endif
