#ifndef EDIT_FINDUI_PRIVATE_H
#define EDIT_FINDUI_PRIVATE_H
#include "findui/findui.h"

#define FINDUI_SLOT_COUNT 2u
#define FINDUI_RETIRE_COUNT 4u
#define FINDUI_PATTERN_BYTES (FIND_MAX_PATTERN * 4u + 512u)
#define FINDUI_RESULT_RANGES (FIND_MAX_PROGRAM_BYTES / sizeof(findui_range))

/* Inputs stay immutable from work_submit to physical completion. Only the
 * work pool's busy/identity atomics are read to retire cancelled arguments. */
typedef struct findui_slot {
    piece_snapshot *snapshot;
    findui_source_lease lease;
    work_handle handle;
    uint64_t owner, window_start, window_end, desired_index;
    size_t query_length, cache_capacity, visible_capacity;
    uint8_t query[FINDUI_QUERY_BYTES];
    findui_options options;
    findui_scan_hook hook;
    void *hook_user;
    void *program, *scratch;
    uint8_t *pattern;
    work_msg *outbox;
    size_t outbox_capacity, outbox_count, outbox_next;
    bool publishing, window_request, page_request;
    size_t page_first, page_count;
} findui_slot;

typedef struct findui_impl {
    findui_config config;
    findui_state state;
    piece_snapshot *source;
    findui_source_lease source_lease;
    struct { piece_snapshot *snapshot; findui_source_lease lease; } retired[FINDUI_RETIRE_COUNT];
    size_t retired_count;
    uint8_t query[FINDUI_QUERY_BYTES], replacement[FINDUI_QUERY_BYTES];
    findui_range *cache, *visible, *replace_page;
    uint64_t window_start, window_end, desired_index;
    findui_slot slots[FINDUI_SLOT_COUNT];
    bool request_active, pending_submit, disposing, window_request, window_dirty;
    findui_mutation_host host;
    undo_log *undo;
    undo_state before, after;
    uint64_t replace_time;
    size_t replace_remaining;
    bool replace_single, replace_changed, replace_paged, page_request, page_ready;
    size_t page_first, page_count;
    find_code page_error;
    findui_range replace_selected, replace_range;
    uint64_t replace_delete_remaining;
    bool replace_match_active;
} findui_impl;

typedef struct findui_batch {
    uint64_t owner, ordinal;
    findui_range ranges[2];
} findui_batch;
typedef struct findui_done {
    uint64_t owner, count, error_offset;
    int32_t code;
    uint64_t visible; /* exact visitor count; UNSET for word-filter stream */
} findui_done;
_Static_assert(sizeof(findui_batch) <= WORK_MSG_DATA, "mailbox batch size");
_Static_assert(sizeof(findui_done) <= WORK_MSG_DATA, "mailbox completion size");
_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "owner token size");

struct find_visit;
find_code findui_literal_visit(findui_slot *slot, work_ctx *context, struct find_visit *visit);
void findui_worker_run(work_ctx *context);
#endif
