/* Explicit command/focus checks for Windows sources without notifications. */
#if defined(_WIN32)
#include "editor/private.h"
#include <windows.h>

bool editor_refuse_wsl_save(editor_buffer *b)
{
    if (!os_path_is_wsl(b->save_path)) return false;
    /* Refuse before preparing a journal save transaction. There is no write
     * to reconcile, and Save As remains available immediately. */
    b->save_error = b->operation_error = FILE_ERR_UNSUPPORTED;
    b->save_errno = b->operation_errno = ERROR_NOT_SUPPORTED;
    return true;
}

bool editor_check_identity(editor_buffer *b)
{
    bool changed = b->source_stale;
    /* A Save As COPY no longer depends on the original pathname. */
    if (b->file && (!b->target_rebound || file_open_mode(b->file) == FILE_MODE_MMAP))
        changed |= file_check(b->file, NULL) != 0;
    if (b->target_rebound && b->save) {
        file_id now;
        if (file_id_stat_path(b->path, &now) || file_id_diff(&now, savectl_target_identity(b->save))) {
            b->operation_kind = FILE_MSG_OPEN_FAILED;
            b->operation_error = FILE_ERR_CHANGED;
            changed = true;
        }
    }
    return changed;
}
#endif
