/* large.h -- large-file path through the editor loop (edit-457.10, P4.10).
 *
 * Mapped (non-copy) files never run a foreground newline scan: open publishes
 * the first viewport from the bounded prefix, shows an ESTIMATED line total and
 * estimated line numbers (prefix newline density), and the lineidx build runs on
 * the bulk worker. When the index is published the editor swaps to exact: the
 * line total and the viewport's first line are recomputed from the index, and a
 * worker "warm" job resolves the piece tree's lazy newline counts so later tree
 * line queries are O(log n). Find (literal) and save run as worker jobs over an
 * immutable piece snapshot; the UI thread only polls their status. All entry
 * points are UI-thread only and may allocate (they are not the typing path). */
#ifndef EDIT_EDITOR_LARGE_H
#define EDIT_EDITOR_LARGE_H
#include "editor/editor.h"
#include "work/work.h"
#include "piece/piece.h"
#include "find/find.h"

#define EDITOR_LARGE_MSG_FIND UINT32_C(0x45463101)
#define EDITOR_LARGE_MSG_WARM UINT32_C(0x45463102)
#define EDITOR_LARGE_PROBE_BYTES (64u * 1024u)   /* backward search for a line start */
#define EDITOR_LARGE_WARM_STRIDE (64u * 1024u * 1024u)
#define EDITOR_LARGE_NEEDLE_MAX 256u

typedef struct editor_large_status {
    bool mapped;         /* file is mmap-backed: estimate-then-exact applies */
    bool lines_exact;    /* line total and top line come from the published index */
    bool top_estimated;  /* the viewport's first line number is still an estimate */
    bool warm_done;      /* piece line counts resolved by the worker */
    bool find_running, find_done;
    int find_rc;         /* find_code */
    uint64_t find_total, find_first;
    size_t find_stored;
    bool save_running, save_done;
    int save_status;     /* 0 or FILE_ERR_* */
} editor_large_status;

editor_large_status editor_large_status_get(const editor *e);
/* View the line containing `byte` at the top; its number is exact if the index
 * already answers, else an estimate that editor_large_index_progress corrects. */
int editor_large_goto_byte(editor *e, uint64_t byte);
/* Byte offset of a 0-based line; *exact says whether the index answered. */
uint64_t editor_large_line_to_byte(editor *e, uint64_t line, bool *exact);
/* Literal find over a snapshot on the bulk worker. ERR_ARG while one runs. */
int editor_large_find_begin(editor *e, const uint8_t *needle, size_t len);
/* Save to the file's own path over a snapshot (atomic temp+rename). */
int editor_large_save_begin(editor *e);
#endif
