#include "hl/hl.h"
#include "trace/trace.h"
#include "file/file.h"
#include <string.h>

/* Stages are worker-private; request init leaves zero for a never-started job. */
enum {
    INITIAL, EDITING, VIEWPORT_SETUP, VIEWPORT_PARSE, VIEWPORT_PUBLISH,
    DOCUMENT_SETUP, DOCUMENT_PARSE, DOCUMENT_PUBLISH, FAILED_PUBLISH, FINISHED
};

static bool point_le(TSPoint a, TSPoint b)
{
    return a.row < b.row || (a.row == b.row && a.column <= b.column);
}

bool hl_worker_init(hl_worker *worker, const TSLanguage *language)
{
    if (!worker || !language) return false;
    memset(worker, 0, sizeof *worker);
    worker->parser = ts_parser_new();
    if (!worker->parser || !ts_parser_set_language(worker->parser, language)) {
        hl_worker_destroy(worker);
        return false;
    }
    return true;
}

void hl_worker_destroy(hl_worker *worker)
{
    if (!worker) return;
    ts_tree_delete(worker->document_tree);
    ts_tree_delete(worker->viewport_tree);
    ts_parser_delete(worker->parser);
    memset(worker, 0, sizeof *worker);
}

bool hl_request_init(hl_request *request, hl_worker *worker,
    const piece_snapshot *snapshot, uint32_t generation, TSRange viewport,
    uint64_t refresh_interval_ns, uint32_t base_generation,
    const TSInputEdit *edits, size_t edit_count)
{
    uint64_t budget;
    if (!request || !worker || !snapshot || (edit_count && !edits) ||
        !hl_frame_budget_ns(refresh_interval_ns, &budget)) return false;
    uint64_t length = piece_snapshot_len(snapshot);
    if (length > UINT32_MAX || viewport.start_byte > viewport.end_byte ||
        viewport.end_byte > length ||
        !point_le(viewport.start_point, viewport.end_point)) return false;
    *request = (hl_request){
        .worker = worker, .snapshot = snapshot, .edits = edits,
        .edit_count = edit_count, .viewport = viewport,
        .generation = generation, .base_generation = base_generation,
        .budget_ns = budget, .length = length
    };
    return true;
}

static uint64_t now(const hl_request *request)
{
    return request->control.now_ns ?
        request->control.now_ns(request->control.payload) : trace_now_ns();
}

static bool stopped(const hl_request *request)
{
    return request->control.should_stop &&
        request->control.should_stop(request->control.payload);
}

static bool expired(const hl_request *request)
{
    return now(request) - request->slice_start_ns >= request->budget_ns;
}

static bool progress(TSParseState *state)
{
    hl_request *request = state->payload;
    if (file_snapshot_faulted(request->snapshot)) request->read_failed = true;
    if (stopped(request) || request->read_failed) return true;
    if (!expired(request)) return false;
    request->yielded = true;
    return true;
}

static const char *read_snapshot(void *payload, uint32_t offset,
                                 TSPoint position, uint32_t *bytes_read)
{
    (void)position;
    hl_request *request = payload;
    *bytes_read = 0;
    if (file_snapshot_faulted(request->snapshot)) {
        request->read_failed = true;
        return request->input;
    }
    if (offset >= request->length) return request->input;
    uint64_t remaining = request->length - offset;
    size_t count = remaining < sizeof request->input ? (size_t)remaining : sizeof request->input;
    if (piece_snapshot_read(request->snapshot, offset,
                            (uint8_t *)request->input, count) != PIECE_OK ||
        file_snapshot_faulted(request->snapshot)) {
        request->read_failed = true;
        return request->input;
    }
    *bytes_read = (uint32_t)count;
    return request->input;
}

void hl_request_discard(hl_request *request)
{
    if (!request) return;
    if (request->stage != INITIAL && request->worker && request->worker->parser)
        ts_parser_reset(request->worker->parser);
    ts_tree_delete(request->edited_tree);
    request->edited_tree = NULL;
    request->stage = FINISHED;
}

static hl_step failed(hl_request *request)
{
    hl_request_discard(request);
    request->pending = (hl_result){request->viewport.start_byte,
                                  request->viewport.end_byte, HL_PARSE_FAILED};
    request->stage = FAILED_PUBLISH;
    return HL_RESULT;
}

static bool apply_edit(hl_request *request, const TSInputEdit *edit)
{
    if (edit->start_byte > edit->old_end_byte ||
        edit->start_byte > edit->new_end_byte ||
        edit->old_end_byte > request->edited_length ||
        !point_le(edit->start_point, edit->old_end_point) ||
        !point_le(edit->start_point, edit->new_end_point)) return false;
    uint64_t length = request->edited_length - (edit->old_end_byte - edit->start_byte) +
                      (edit->new_end_byte - edit->start_byte);
    if (length > UINT32_MAX) return false;
    ts_tree_edit(request->edited_tree, edit);
    request->edited_length = length;
    return true;
}

hl_step hl_parse_slice(hl_request *request, hl_slice_control control)
{
    if (!request || request->stage == FINISHED) return HL_DONE;
    request->control = control;
    request->slice_start_ns = now(request);
    if (stopped(request)) { hl_request_discard(request); return HL_DONE; }
    if (file_snapshot_faulted(request->snapshot)) return failed(request);
    if (request->stage == VIEWPORT_PUBLISH ||
        request->stage == DOCUMENT_PUBLISH || request->stage == FAILED_PUBLISH)
        return HL_RESULT;
    hl_worker *worker = request->worker;
    if (!worker || !worker->parser) return failed(request);
    if (request->stage == INITIAL) {
        ts_parser_reset(worker->parser);
        /* Copy before editing: cancellation keeps the last completed tree
         * unchanged, and the parser resumes against this same copy/input. */
        if (worker->document_tree &&
            worker->document_generation == request->base_generation &&
            (request->edit_count || request->generation == request->base_generation)) {
            request->edited_tree = ts_tree_copy(worker->document_tree);
            request->edited_length = worker->document_length;
        }
        request->stage = EDITING;
        if (expired(request)) return HL_MORE;
    }
    if (request->stage == EDITING) {
        if (request->edited_tree) {
            while (request->edit_next < request->edit_count) {
                if (stopped(request)) { hl_request_discard(request); return HL_DONE; }
                if (!apply_edit(request, &request->edits[request->edit_next])) return failed(request);
                ++request->edit_next;
                if (expired(request)) return HL_MORE;
            }
            if (request->edited_length != request->length) return failed(request);
        }
        request->stage = VIEWPORT_SETUP;
    }
    if (request->stage == VIEWPORT_SETUP || request->stage == DOCUMENT_SETUP) {
        bool viewport = request->stage == VIEWPORT_SETUP;
        ts_parser_reset(worker->parser);
        if (!ts_parser_set_included_ranges(worker->parser,
                viewport ? &request->viewport : NULL, viewport ? 1u : 0u)) return failed(request);
        request->stage = viewport ? VIEWPORT_PARSE : DOCUMENT_PARSE;
        if (expired(request)) return HL_MORE;
    }
    if (stopped(request)) { hl_request_discard(request); return HL_DONE; }
    request->yielded = false;
    TSInput input = {.payload = request, .read = read_snapshot, .encoding = TSInputEncodingUTF8};
    TSParseOptions options = {.payload = request, .progress_callback = progress};
    TSTree *tree = ts_parser_parse_with_options(worker->parser, request->edited_tree, input, options);
    if (stopped(request)) { ts_tree_delete(tree); hl_request_discard(request); return HL_DONE; }
    if (request->read_failed || file_snapshot_faulted(request->snapshot)) {
        ts_tree_delete(tree); return failed(request);
    }
    if (!tree) return request->yielded ? HL_MORE : failed(request);
    if (request->stage == VIEWPORT_PARSE) {
        ts_tree_delete(worker->viewport_tree);
        worker->viewport_tree = tree;
        worker->viewport_generation = request->generation;
        request->pending = (hl_result){request->viewport.start_byte,
                                      request->viewport.end_byte, HL_VIEWPORT_READY};
        request->stage = VIEWPORT_PUBLISH;
    } else {
        ts_tree_delete(worker->document_tree);
        worker->document_tree = tree;
        worker->document_generation = request->generation;
        worker->document_length = request->length;
        ts_tree_delete(request->edited_tree);
        request->edited_tree = NULL;
        request->pending = (hl_result){0, request->length, HL_DOCUMENT_READY};
        request->stage = DOCUMENT_PUBLISH;
    }
    return HL_RESULT;
}

void hl_result_published(hl_request *request)
{
    if (!request) return;
    if (request->stage == VIEWPORT_PUBLISH) request->stage = DOCUMENT_SETUP;
    else if (request->stage == DOCUMENT_PUBLISH || request->stage == FAILED_PUBLISH)
        request->stage = FINISHED;
}

static bool job_stopped(void *payload)
{
    return work_should_stop(payload);
}

void hl_worker_run(work_ctx *context)
{
    hl_request *request = context->arg;
    if (!request) return;
    if (context->generation != request->generation) { hl_request_discard(request); return; }
    hl_step step = hl_parse_slice(request, (hl_slice_control){
        .payload = context, .should_stop = job_stopped
    });
    if (step == HL_DONE) return;
    if (step == HL_RESULT) {
        work_msg message;
        if (!hl_result_encode(&message, request->generation, &request->pending)) {
            hl_request_discard(request);
            return;
        }
        if (work_publish(context, &message)) hl_result_published(request);
    }
    if (request->stage != FINISHED) (void)work_continue(context);
}
