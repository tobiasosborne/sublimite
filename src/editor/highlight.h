#ifndef EDIT_EDITOR_HIGHLIGHT_H
#define EDIT_EDITOR_HIGHLIGHT_H
#include "hl/hl.h"
#include "hl/query.h"
typedef struct editor_hl_stamp {
    uint64_t id, revision, source, first, visual, hscroll, geometry;
    uint32_t cols, rows;
    bool wrap;
} editor_hl_stamp;
typedef struct editor_highlight {
    hl_worker worker;
    hl_query query;
    hl_request request;
    piece_snapshot *snapshot;
    struct editor_buffer *owner;
    const TSLanguage *language, *prepared_language;
    work_handle handle;
    editor_hl_stamp wanted;
    hl_span *worker_cells, *staged, *published;
    size_t capacity, count, next, staged_count, published_count;
    uint64_t published_owner, refresh_interval_ns, requested_refresh_ns;
    uint32_t generation, first_row, end_row;
    int error;
    bool initialized, queried, terminal, cancelled, clock_checked;
} editor_highlight;
#endif
