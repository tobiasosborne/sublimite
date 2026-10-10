#ifndef EDIT_EDITOR_LARGE_PRIVATE_H
#define EDIT_EDITOR_LARGE_PRIVATE_H
#include "editor/large.h"
#include <stdatomic.h>
typedef struct editor_large_find_job {
    piece_snapshot *snap; _Atomic bool released;
    uint8_t needle[EDITOR_LARGE_NEEDLE_MAX];
    size_t needle_len;
    find_code rc;
    find_result result;
} editor_large_find_job;
typedef struct editor_large_warm_job { piece_snapshot *snap; _Atomic bool released; } editor_large_warm_job;
typedef struct editor_large {
    bool mapped, lazy, lines_exact, top_estimated, warm_done;
    uint64_t prefix_lines, prefix_bytes;
    editor_large_warm_job *warm; work_handle warm_h;
    editor_large_find_job *find; work_handle find_h; bool find_running, find_done;
    uint32_t find_gen;
    bool save_running, save_done; int save_status; uint32_t save_gen;
} editor_large;
#endif
