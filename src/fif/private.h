#ifndef EDIT_FIF_PRIVATE_H
#define EDIT_FIF_PRIVATE_H
#include "fif/fif.h"
#include "find/options.h"
#include <sys/types.h>

typedef struct fif_node { char path[PATH_MAX]; } fif_node;
typedef struct fif_identity { dev_t device; ino_t inode; } fif_identity;
typedef struct fif_frame { size_t first, count, next; } fif_frame;
typedef struct fif_file_record { const char *path; atomic_bool ready; } fif_file_record;
typedef struct fif_hit_record {
    fif_hit hit;
    size_t length;
    bool truncated;
    atomic_bool ready;
} fif_hit_record;
typedef struct fif_impl {
    edit_arena arena;
    fif_limits limits;
    fif_options options;
    fif_filter filter;
    char root[PATH_MAX];
    uint8_t query[FIND_MAX_PATTERN];
    size_t query_length, root_length;
    fif_node *nodes;
    fif_identity *seen;
    fif_file_record *files;
    fif_hit_record *hits;
    uint8_t *contexts, *bytes, *folded, *pattern;
    void *program, *scratch;
    find_regex *regex;
    fif_frame frames[FIF_DEFAULT_DEPTH + 1u]; /* (E) root plus capped nesting. */
    size_t node_count, seen_count, frames_count, selected;
    size_t length, offset, line_start, line_end;
    uint64_t line;
    uint32_t current_file;
    bool initialized, searching, exhausted, pending, search_complete;
    work_msg outbox;
    fif_done done;
    work_pool *pool;
    work_handle handles[2]; /* (E) search lease plus independent DONE lease. */
} fif_impl;
bool fif_filter_excludes(const fif_filter *filter, const char *path, const work_ctx *context);
bool fif_filter_matches_work(const fif_filter *filter, const char *path, const work_ctx *context);
void fif_worker(work_ctx *context);
void fif_notify(work_ctx *context);
#endif
