/* Find in files engine. UI-thread lifecycle, WORK_BULK filesystem/search work.
 * All request strings/bytes are copied during init. No editor buffer is used.
 * See docs/decisions/edit-34l.md for capacity arithmetic and part-two duties. */
#ifndef EDIT_FIF_H
#define EDIT_FIF_H
#include "base/base.h"
#include "find/find.h"
#include "work/work.h"
#include <limits.h>

/* (P) POSIX path interface limit, including NUL. (E) Rule/depth defaults reuse
 * the core's nesting capacity; result/file caps reuse its offset capacity.
 * (E) Entry allowance = one file plus one directory per admitted file.
 * (E) Context cap = one core polling quantum of byte columns.
 * (E) File copy cap = perf §1 RAM/32 copy threshold on target A, 256 MiB.
 * (E) 1 MiB poll/read span: 2^20 / (16e9 B/s G6 (M)[AC]) = 65.536 us;
 * scalar preprocessing also polls at FIND_POLL_UNITS, search polls internally.
 * (E) Binary probe = requested 8 KiB = eight core polling quanta. */
#define FIF_PATH_BYTES ((size_t)PATH_MAX)
#define FIF_MAX_RULES FIND_MAX_DEPTH
#define FIF_DEFAULT_DEPTH FIND_MAX_DEPTH
#define FIF_DEFAULT_FILES FIND_MAX_OFFSETS
#define FIF_DEFAULT_ENTRIES (FIF_DEFAULT_FILES * 2u)
#define FIF_DEFAULT_HITS FIND_MAX_OFFSETS
#define FIF_DEFAULT_CONTEXT FIND_POLL_UNITS
#define FIF_DEFAULT_FILE_BYTES ((size_t)FIND_MAX_SCRATCH_BYTES * FIND_POLL_UNITS)
#define FIF_POLL_BYTES ((size_t)FIND_POLL_UNITS * FIND_POLL_UNITS)
#define FIF_BINARY_BYTES ((size_t)FIND_POLL_UNITS * FIND_MAX_GROUPS)

typedef enum fif_code {
    FIF_OK = 0, FIF_CANCELLED, FIF_ERR_ARGUMENT, FIF_ERR_MEMORY,
    FIF_ERR_FILTER, FIF_ERR_SEARCH, FIF_ERR_IO, FIF_ERR_BUSY
} fif_code;
typedef struct fif_options { bool regex, match_case, whole_word; } fif_options;
typedef struct fif_limits {
    /* Zero selects the named default. Nonzero limits may lower or raise the
     * capacities; depth is at most FIF_DEFAULT_DEPTH to bound the C stack.
     * Entries include directories and symlinks, excluding .git and dot entries.
     * Files count selected regular files, including binary/oversized files.
     * max_file_bytes bounds each read; larger files are skipped, truncated=true.
     * All indices fit uint32_t; capacities are checked for arithmetic overflow. */
    size_t max_files, max_entries, max_hits, max_file_bytes, context_columns;
    uint32_t max_depth;
} fif_limits;
typedef struct fif_request {
    const char *root;
    const uint8_t *query;
    size_t query_length;
    fif_options options;
    const char *include; /* comma-separated Where paths/globs and -excludes */
    const char *exclude; /* comma-separated paths/globs, all exclusions */
    fif_limits limits;
} fif_request;

/* Filter grammar: trim ASCII space/tab around comma-separated tokens, optional
 * '+' or '-' prefix. Exclusions always win; no includes means all files.
 * Plain paths select that file/directory subtree. Basename globs (*.ext) match
 * at any depth; slash globs match root-relative paths. Absolute Where paths
 * inside root are normalized at init; out-of-root paths select nothing.
 * Glob syntax: *, ?, [a-z], [!a-z], backslash escape, ** across separators.
 * A double-star followed by slash also matches no directory. No shell expansion, braces, or quoted commas.
 * Paths/filter matching are case sensitive independently of search options.
 * Parser storage <= PATH_MAX, rules <= FIF_MAX_RULES; overflow returns FILTER.
 * Standalone matcher is anchored (it does not implicitly strip directories). */
typedef struct fif_rule { size_t offset; bool exclude, glob, slash; } fif_rule;
typedef struct fif_filter {
    char text[PATH_MAX];
    fif_rule rules[FIF_MAX_RULES];
    size_t count, used, includes;
} fif_filter;
fif_code fif_filter_parse(fif_filter *filter, const char *include, const char *exclude);
bool fif_glob_match(const char *pattern, const char *path);
bool fif_filter_matches(const fif_filter *filter, const char *relative_path);

/* (P) Each layout fits WORK_MSG_DATA, statically checked by the implementation.
 * Values are tags only, derived successively from the ASCII 'FIF' namespace. */
enum { FIF_MSG_HIT = 0x46494600u, FIF_MSG_FILE, FIF_MSG_DONE };
typedef struct fif_hit {
    uint64_t line, column, byte_offset, byte_length; /* one-based LF line/byte column */
    uint32_t file_index, index;
} fif_hit;
typedef struct fif_file_msg { uint64_t bytes; uint32_t file_index; } fif_file_msg;
/* errors counts unreadable entries below the root; the walk continues and
 * code stays OK. code == FIF_ERR_IO only when the root itself could not be
 * walked (missing, not a directory, unreadable): no usable result. */
typedef struct fif_done {
    uint64_t hits, bytes;
    uint32_t files, binary, symlinks, errors;
    int32_t code, search_code;
    bool truncated, cancelled;
} fif_done;
typedef struct fif_result_view {
    fif_hit hit;
    const char *path; /* root-prefixed path */
    const uint8_t *context; /* original line bytes, no LF; trailing CR removed */
    size_t context_length;
    bool context_truncated;
} fif_result_view;
struct fif_impl;
typedef struct fif_job { struct fif_impl *impl; } fif_job;
/* init requires a zero/previously disposed job; owns a base arena until dispose.
 * Every work array is prepaid here. A failed init leaves the job empty.
 * Search syntax errors arrive in DONE.search_code on the bulk worker. */
fif_code fif_job_init(fif_job *job, const fif_request *request);
/* One-shot submission. Returns the search/cancellation handle, epoch=0 on
 * refusal (retry allowed). A companion completion lease is submitted atomically
 * and survives cancellation of the search. Both share caller generation.
 * Drain the shared pool normally; selective receivers must bind BOTH handles.
 * No service callback or UI filesystem work is required. */
work_handle fif_submit(work_pool *pool, fif_job *job, uint32_t generation);
work_handle fif_completion_handle(const fif_job *job);
/* True after BOTH physical leases finish. Does not imply mailboxes drained.
 * Dispose only after finish and after draining all messages; BUSY otherwise.
 * Pool shutdown must precede disposal of a job whose pool has been shut down.
 * Caller owns pool and keeps it alive through dispose. */
bool fif_job_finished(const fif_job *job);
fif_code fif_job_dispose(fif_job *job);
/* Allocation-free borrowed immutable results. Use an index only AFTER receiving
 * its HIT/FILE (mailbox release/acquire), or after physical completion. Paths
 * and context remain stable until dispose, including cancellation. Do not read
 * unpublished indices while the worker runs. Returned text is byte-oriented;
 * context_columns is a byte cap, not a Unicode display width. */
fif_code fif_results(const fif_job *job, uint32_t hit_index, fif_result_view *view);
const char *fif_results_path(const fif_job *job, uint32_t file_index);
/* edit-n95: byte length of a line's display context under cap. Lines no longer
 * than cap are kept whole; a longer line is cut at cap, moved back to the
 * preceding UTF-8 scalar boundary (at most three bytes). The engine stores
 * contexts cut this way; context_truncated marks the cut. */
size_t fif_context_cut(const uint8_t *line, size_t length, size_t cap);
#endif
