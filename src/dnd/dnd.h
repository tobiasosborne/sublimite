#ifndef DND_H
#define DND_H

#include "base/base.h"
#include "ipc/ipc.h"

typedef enum dnd_result {
    dnd_ok = 0, /* (P) zero is the C success return convention. */
    dnd_invalid,
    dnd_no_space
} dnd_result;

typedef struct dnd_counts {
    size_t skipped;  /* Rejected entries plus otherwise valid overflow entries. */
    size_t overflow; /* Valid entries beyond IPC_MAX_PATHS; included in skipped. */
} dnd_counts;

/* Parse LF/CRLF text/uri-list, skipping empty lines and '#' comment lines.
 * Accept file:///absolute, file://localhost/absolute, file://local_hostname/absolute,
 * and bare /absolute paths. Scheme and hostname comparisons ignore ASCII case;
 * NULL local_hostname accepts only empty authority and localhost. URI paths are
 * percent-decoded; bare paths are copied literally. Reject malformed escapes,
 * decoded NUL, invalid UTF-8, raw CR, URI queries/fragments and foreign hosts.
 * Decoded paths must fit IPC_PATH_CAP including the trailing NUL. Directories
 * are passed through without filesystem access.
 *
 * out borrows copied strings from arena until its next reset/free. Fields other
 * than paths/count are cleared (absolute paths need no cwd); line/col use IPC's
 * one-based origin (P). No allocation except edit_arena_alloc. A capacity of
 * IPC_MAX_PATHS * IPC_PATH_CAP free bytes suffices (P: IPC bounds including NUL).
 * data may be NULL only when size is zero; arena, out and counts are required.
 * Input/hostname/output objects and the arena's free storage must not overlap.
 * Embedded raw NUL rejects the entire payload. Bad arguments or raw NUL return
 * dnd_invalid; arena exhaustion/commit failure returns dnd_no_space. On either
 * error, outputs are cleared and the arena mark is unchanged. Invalid entries
 * alone return dnd_ok with counts. Empty success must not be dispatched as an
 * editor open request: the editor interprets it as an untitled-file launch. */
dnd_result dnd_parse_uri_list(const uint8_t *data, size_t size,
                             const char *local_hostname, edit_arena *arena,
                             ipc_request *out, dnd_counts *counts);

#endif
