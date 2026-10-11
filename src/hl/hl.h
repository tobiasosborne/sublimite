#ifndef EDIT_HL_H
#define EDIT_HL_H

#include "work/work.h"
#include "piece/piece.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdint.h>

/* Mailbox kind is the ASCII bytes 'H', 'L' packed into an integer. No
 * pointers cross this boundary: syntax trees remain worker-owned. */
#define HL_RESULT_KIND (((uint32_t)'H' << 8u) | (uint32_t)'L')
enum hl_phase {
    HL_VIEWPORT_READY = 1, /* first valid phase; zero is unassigned */
    HL_DOCUMENT_READY,    /* follows viewport publication */
    HL_PARSE_FAILED
};
typedef struct hl_result {
    uint64_t first_byte, end_byte; /* half-open snapshot byte range */
    uint32_t phase;
} hl_result;

/* (P) one-frame slice = one caller-supplied refresh interval, in ns.
 * Zero means refresh unknown and is refused; no guessed rate or fallback.
 * Parsing cooperatively stops at the next upstream progress callback. */
bool hl_frame_budget_ns(uint64_t refresh_interval_ns, uint64_t *budget_ns);

/* Encode before work_publish; generation identifies the immutable snapshot.
 * Decode/readiness run on the UI, without allocation or worker-state access.
 * Plain text remains the fallback until a current viewport result covers the
 * requested range. A document result cannot substitute for that publication.
 * Future grammar/query code must also validate its own scope data. */
bool hl_result_encode(work_msg *message, uint32_t generation, const hl_result *result);
bool hl_result_decode(const work_msg *message, hl_result *result);
bool hl_viewport_ready(const work_msg *message, uint32_t generation,
                       uint64_t first_byte, uint64_t end_byte);

/* Initialize/destroy outside typing, with no active lease. Trees/parser are
 * exclusively worker-owned during a lease. Serial requests per worker: wait
 * for work_handle_finished before reusing worker/request storage, even after
 * cancel. No tree pointer is published. Tree fields may be inspected only by
 * worker code or after physical completion (e.g. tests / future queries). */
typedef struct hl_worker {
    TSParser *parser;
    TSTree *document_tree, *viewport_tree;
    uint32_t document_generation, viewport_generation;
    uint64_t document_length;
} hl_worker;
bool hl_worker_init(hl_worker *worker, const TSLanguage *language);
void hl_worker_destroy(hl_worker *worker);

/* Inject a monotonic clock and cancellation predicate for synchronous worker
 * clients/tests. NULL clock uses CLOCK_MONOTONIC; NULL stop never cancels.
 * Both callbacks must be bounded and allocation-free. */
typedef struct hl_slice_control {
    void *payload;
    uint64_t (*now_ns)(void *payload);
    bool (*should_stop)(void *payload);
} hl_slice_control;
typedef enum hl_step { HL_MORE, HL_RESULT, HL_DONE } hl_step;

typedef struct hl_request {
    hl_worker *worker;
    const piece_snapshot *snapshot;
    const TSInputEdit *edits;
    size_t edit_count, edit_next;
    TSRange viewport;
    uint32_t generation, base_generation, stage;
    uint64_t budget_ns, length, edited_length, slice_start_ns;
    TSTree *edited_tree;
    hl_slice_control control;
    hl_result pending;
    bool yielded, read_failed;
    /* A page-sized input cache, not a source-size or parse-work limit. */
    char input[4096];
} hl_request;

/* Typing-side setup: no allocation, parser access, tree edits or source scan.
 * Snapshot/edits are BORROWED and must remain immutable/alive until physical
 * lease completion. Caller supplies exact byte/point coordinates (columns
 * count UTF-8 bytes) and the ordered edit chain from base_generation. Missing
 * or mismatched prior generation falls back to a fresh parse. A zero edit
 * count permits reuse only for the same generation. Lengths above UINT32_MAX
 * are refused because tree-sitter's byte ABI cannot represent them.
 * Viewport coordinates must describe the snapshot; no context is guessed. */
bool hl_request_init(hl_request *request, hl_worker *worker,
    const piece_snapshot *snapshot, uint32_t generation, TSRange viewport,
    uint64_t refresh_interval_ns, uint32_t base_generation,
    const TSInputEdit *edits, size_t edit_count);

/* Worker-only resumable driver. RESULT repeats until publication is acked;
 * this retains viewport-before-document ordering on mailbox backpressure.
 * A cancelled slice resets suspended parsing, discards the edited copy and
 * returns DONE. The last completed document remains intact. */
hl_step hl_parse_slice(hl_request *request, hl_slice_control control);
void hl_result_published(hl_request *request);
/* Pass hl_request as work_job.arg, with its generation, WORK_BULK. Continues
 * on the existing lease; never blocks waiting for mailbox capacity. */
void hl_worker_run(work_ctx *context);
/* After a cancelled queued job (which may never invoke hl_worker_run), or
 * after physical completion, discard request-owned parser continuation/tree.
 * Call before reusing the worker, with no other lease using it. No malloc. */
void hl_request_discard(hl_request *request);

#endif
