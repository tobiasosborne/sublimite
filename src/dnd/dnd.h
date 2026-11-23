#ifndef DND_DND_H
#define DND_DND_H

#include "base/base.h"
#include "ipc/ipc.h"

typedef enum dnd_result {
    DND_OK = 0, /* (P) zero is the C success return convention. */
    DND_INVALID,
    DND_NO_SPACE
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
 * one-based origin (P). No allocation except base_arena_alloc. A capacity of
 * IPC_MAX_PATHS * IPC_PATH_CAP free bytes suffices (P: IPC bounds including NUL).
 * Input/hostname/output objects and the arena's free storage must not overlap.
 * Embedded raw NUL rejects the entire payload: DND_INVALID. Arena exhaustion
 * returns DND_NO_SPACE. On either error, outputs are cleared and the arena
 * mark is unchanged. Invalid entries alone return DND_OK with counts. Empty
 * success must not be dispatched as an editor open request: the editor
 * interprets it as an untitled-file launch.
 *
 * Preconditions are ASSERTed (os.h, A15); no drag payload can violate them,
 * only a caller bug can: arena, out and counts are non-NULL, the arena is
 * initialized (base non-NULL, used <= size), and data is NULL only when
 * size is zero. The non-overlap requirement above is also a precondition. */
dnd_result dnd_parse_uri_list(const uint8_t *data, size_t size,
                              const char *local_hostname, base_arena *arena,
                              ipc_request *out, dnd_counts *counts);

#endif
