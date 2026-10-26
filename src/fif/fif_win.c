#if defined(_WIN32)
#include "fif/private.h"
int fif_iter_open(fif_iterator *it, const os_dir *dir)
{ return os_dir_iter_open(&it->it, dir) == OS_OK ? 0 : -1; }
int fif_iter_next(fif_iterator *it, const char **name)
{ return os_dir_next(&it->it, name) == OS_OK ? 0 : -1; }
int fif_iter_close(fif_iterator *it) { os_dir_iter_close(&it->it); return 0; }
int fif_close_file(os_file *file) { os_file_close(file); return 0; }
os_err fif_stat_dir(const os_dir *dir, os_stat *st) { return os_stat_at(dir, ".", 0, st); }
#endif
