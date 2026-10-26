#if defined(_WIN32)
/* W0 suites exercise piece-owned buffers only. The file module is a W2
 * dependency: refuse real file backings rather than simulate their behavior.
 * Corresponding integration cases print SKIP in their own suites. */
#include "base/base.h"
#include "file/file.h"
file_backing *file_snapshot_backing(const piece_snapshot *snapshot)
{
    const piece_map_hooks *hooks = piece_snapshot_mapping(snapshot);
    EDIT_ASSERT(!hooks || (!hooks->acquire && !hooks->release));
    return NULL;
}
void file_backing_acquire(file_backing *backing) { EDIT_ASSERT(!backing); }
void file_backing_release(file_backing *backing) { EDIT_ASSERT(!backing); }
int file_backing_faulted(const file_backing *backing) { EDIT_ASSERT(!backing); return 0; }
int file_snapshot_faulted(const piece_snapshot *snapshot)
{ return file_backing_faulted(file_snapshot_backing(snapshot)); }
#endif
