#if defined(_WIN32)
#include "journal/private.h"
int journal_hook_file(const os_file *file) { (void)file; return -1; }
int journal_hook_dir(const os_dir *dir) { (void)dir; return -1; }
#endif
