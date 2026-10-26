#include "editor/private.h"
#include "hl/lang.h"
#include "file/file.h"
#include <string.h>
#include <stdlib.h>
#include <sched.h>

/* All ordinals are (P): zero is empty/success/first, one advances a byte,
 * row or identity. Storage derives from the existing reserved cell geometry.
 * No source-size, capture-count or guessed time budget is introduced. */
static editor_hl_stamp stamp(const editor *e)
{
    return (editor_hl_stamp){e->buffer->id,e->buffer->revision,e->buffer->source_generation,
        e->v.state.first_byte,e->v.state.visual_byte,e->v.state.hscroll,e->geometry_generation,
        e->text_grid.dims.cols,e->text_grid.dims.rows,e->v.state.wrap};
}
static bool same(editor_hl_stamp a, editor_hl_stamp b)
{
    return a.id==b.id && a.revision==b.revision && a.source==b.source &&
        a.first==b.first && a.visual==b.visual && a.hscroll==b.hscroll &&
        a.geometry==b.geometry && a.cols==b.cols && a.rows==b.rows && a.wrap==b.wrap;
}
int editor_hl_init(editor *e, size_t cells)
{
    editor_highlight *h=&e->hl;
    h->capacity=cells;
    h->worker_cells=edit_arena_alloc(&e->arena,cells*sizeof(hl_span),_Alignof(hl_span));
    h->staged=edit_arena_alloc(&e->arena,cells*sizeof(hl_span),_Alignof(hl_span));
    h->published=edit_arena_alloc(&e->arena,cells*sizeof(hl_span),_Alignof(hl_span));
    return h->worker_cells && h->staged && h->published ? 0 : EDITOR_ERR_MEMORY;
}
static void release(editor *e)
{
    editor_highlight *h=&e->hl;
    (void)work_mailbox_bind(&e->pool,h->handle,0,NULL,NULL);
    hl_request_discard(&h->request);
    piece_snapshot_release(h->snapshot); h->snapshot=NULL;
    if (h->owner) h->owner->hl_leases--;
    h->owner=NULL; h->handle=(work_handle){0};
    if (h->cancelled) memset(&h->wanted,0,sizeof h->wanted);
}
void editor_hl_close(editor *e)
{
    editor_highlight *h=&e->hl;
    if (h->snapshot) {
        work_cancel(&e->pool,h->handle);
        while (!work_handle_finished(&e->pool,h->handle)) sched_yield(); /* quiescent close only */
        release(e);
    }
    hl_query_destroy(&h->query); hl_worker_destroy(&h->worker);
}
static bool stopped(void *payload) { return work_should_stop(payload); }
static int capture(void *payload, const hl_span *span)
{
    work_ctx *ctx=payload;
    editor_highlight *h=ctx->arg;
    if (work_should_stop(ctx)) return HL_QUERY_STOP;
    size_t lo=0,hi=h->count;
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2u;
        if (h->worker_cells[mid].first_byte<span->first_byte) lo=mid+1u; else hi=mid;
    }
    for (size_t i=lo;i<h->count && h->worker_cells[i].first_byte<span->end_byte;i++)
        if (span->priority>=h->worker_cells[i].priority) {
            h->worker_cells[i].scope=span->scope;
            h->worker_cells[i].priority=span->priority;
        }
    return HL_QUERY_OK;
}
static bool prepare(editor_highlight *h)
{
    if (h->prepared_language!=h->language) {
        hl_query_destroy(&h->query); hl_worker_destroy(&h->worker);
        h->prepared_language=h->language;
        h->error=hl_worker_init(&h->worker,h->language) ? hl_query_init(&h->query,h->language,NULL) : HL_QUERY_MEMORY;
    }
    h->error=h->query.query && h->worker.parser ? HL_QUERY_OK : HL_QUERY_ARG;
    if (h->error) return false;
    uint64_t length=piece_snapshot_len(h->snapshot);
    uint64_t first=piece_snapshot_byte_to_line(h->snapshot,h->wanted.first);
    uint64_t last=h->count ? piece_snapshot_byte_to_line(h->snapshot,h->worker_cells[h->count-1u].first_byte) : first;
    if (first>UINT32_MAX || last>=UINT32_MAX) return false;
    h->first_row=(uint32_t)first; h->end_row=(uint32_t)last+1u;
    uint64_t begin=piece_snapshot_line_to_byte(h->snapshot,first);
    uint64_t end=piece_snapshot_line_to_byte(h->snapshot,last+1u);
    uint64_t end_line=piece_snapshot_byte_to_line(h->snapshot,end);
    uint64_t end_start=piece_snapshot_line_to_byte(h->snapshot,end_line);
    TSRange range={{(uint32_t)first,0},{(uint32_t)end_line,(uint32_t)(end-end_start)},(uint32_t)begin,(uint32_t)end};
    /* Missing edit chain intentionally forces a fresh parse after mutation.
     * A scroll-only request may reuse the completed same-generation document.
     * Request identities include viewport geometry, so use fresh parsing here. */
    return length<=UINT32_MAX && hl_request_init(&h->request,&h->worker,h->snapshot,h->generation,
        range,h->requested_refresh_ns,0,NULL,0);
}
static void run_inner(work_ctx *ctx)
{
    editor_highlight *h=ctx->arg;
    if (work_should_stop(ctx)) return;
    if (!h->initialized) {
        h->initialized=true;
        if (!prepare(h)) {
            h->request.pending=(hl_result){0,piece_snapshot_len(h->snapshot),HL_PARSE_FAILED};
            h->terminal=true;
        }
    }
    hl_step step=h->terminal ? HL_RESULT : hl_parse_slice(&h->request,(hl_slice_control){ctx,NULL,stopped});
    if (step==HL_DONE) return;
    if (step==HL_RESULT) {
        hl_result result=h->request.pending;
        if (result.phase==HL_VIEWPORT_READY || result.phase==HL_DOCUMENT_READY) {
            if (!h->queried) {
                for (size_t i=0;i<h->count;i++) {
                    h->worker_cells[i].scope=HL_SCOPE_DEFAULT; h->worker_cells[i].priority=0;
                }
                const TSTree *tree=result.phase==HL_VIEWPORT_READY ? h->worker.viewport_tree : h->worker.document_tree;
                h->error=file_snapshot_faulted(h->snapshot) ? HL_QUERY_READ :
                    hl_query_run(&h->query,tree,h->snapshot,h->first_row,h->end_row,capture,ctx);
                if (file_snapshot_faulted(h->snapshot)) h->error=HL_QUERY_READ;
                h->queried=true;
                if (h->error) { h->terminal=true; result.phase=HL_PARSE_FAILED; h->request.pending=result; }
            }
            if (!h->error) while (h->next<h->count) {
                work_msg msg;
                if (hl_span_encode(&msg,h->generation,&h->worker_cells[h->next])!=HL_QUERY_OK) return;
                if (!work_publish(ctx,&msg)) { (void)work_continue(ctx); return; }
                h->next++;
            }
        }
        work_msg msg;
        hl_result publication=result;
        /* Full-document completion can improve context (e.g. multiline
         * comments). Its query still publishes only the requested viewport. */
        if (result.phase==HL_DOCUMENT_READY)
            publication=(hl_result){h->request.viewport.start_byte,h->request.viewport.end_byte,HL_VIEWPORT_READY};
        if (!hl_result_encode(&msg,h->generation,&publication)) return;
        if (!work_publish(ctx,&msg)) { (void)work_continue(ctx); return; }
        if (h->terminal) return;
        hl_result_published(&h->request);
        if (result.phase!=HL_VIEWPORT_READY) return;
        h->queried=false; h->next=0;
    }
    (void)work_continue(ctx);
}
static void run(work_ctx *ctx)
{
    /* Only the parser/query worker is exempt: upstream worker allocations are
     * permitted by law. UI snapshot/request/paint remain fully counted. */
    edit_malloc_guard_exempt_begin(); run_inner(ctx); edit_malloc_guard_exempt_end();
}
static void receive(const work_msg *msg, void *payload)
{
    editor *e=payload; editor_highlight *h=&e->hl;
    if (msg->generation!=h->generation || h->cancelled) return;
    hl_span span;
    if (hl_span_decode(msg,&span)==HL_QUERY_OK) {
        if (h->staged_count<h->capacity) h->staged[h->staged_count++]=span;
        return;
    }
    hl_result result;
    if (!hl_result_decode(msg,&result) || result.phase!=HL_VIEWPORT_READY || h->staged_count!=h->count ||
        e->buffer->source_stale || !same(h->wanted,stamp(e))) return;
    hl_span *old=h->published; h->published=h->staged; h->staged=old;
    h->published_count=h->staged_count; h->published_owner=e->buffer->id;
    h->staged_count=0;
    int rc=editor_begin_frame(e);
    if (rc) e->error=rc;
    e->paint_ready=false; e->full_pending=true;
}
void editor_hl_service(editor *e)
{
    editor_highlight *h=&e->hl;
    if (!h->clock_checked) {
        h->clock_checked=true;
        if (!h->refresh_interval_ns) h->refresh_interval_ns=(e->has_platform ? plat_display_interval_ns(&e->platform) : 0);
    }
    if (!h->snapshot) return;
    if (!same(h->wanted,stamp(e)) || e->buffer->source_stale || e->buffer->pending_open) {
        work_cancel(&e->pool,h->handle); h->cancelled=true;
    }
    /* Drain before releasing the bound receiver, including a physically
     * completed job with pending messages. The shared pump routes its spans. */
    if (work_handle_finished(&e->pool,h->handle) &&
        !atomic_load_explicit(&e->pool.slots[h->handle.slot].pending,memory_order_acquire)) release(e);
}
void editor_hl_request(editor *e)
{
    editor_highlight *h=&e->hl;
    const TSLanguage *language=hl_language_path(e->buffer->path);
    if (h->published_owner!=e->buffer->id) { h->published_count=0; h->published_owner=0; }
    if (!language) { h->published_count=0; h->published_owner=0; }
    editor_hl_stamp current=stamp(e);
    if (h->snapshot) {
        if (!same(h->wanted,current) || h->language!=language || e->buffer->source_stale) {
            work_cancel(&e->pool,h->handle); h->cancelled=true;
        }
        return;
    }
    if (!language || !h->refresh_interval_ns || e->buffer->pending_open || e->buffer->source_stale ||
        !e->paint_ready || layout_busy(&e->lay) || view_busy(&e->v) || e->full_pending || e->extra_rows ||
        (same(current,h->wanted) && h->language==language) || h->generation==UINT32_MAX) return;
    h->snapshot=piece_snapshot_take(e->tree);
    if (!h->snapshot) return; /* highlight failure leaves usable plain/stale text */
    if (piece_snapshot_len(h->snapshot)>UINT32_MAX) { piece_snapshot_release(h->snapshot); h->snapshot=NULL; return; }
    h->requested_refresh_ns=h->refresh_interval_ns;
    h->wanted=current; h->language=language; h->generation++;
    h->owner=e->buffer; h->owner->hl_leases++;
    h->initialized=h->queried=h->terminal=h->cancelled=false;
    h->count=h->next=h->staged_count=0;
    uint32_t rows=e->text_grid.dims.rows;
    if (!e->lay.wrap) rows++; /* existing smooth-scroll overscan */
    for (uint32_t row=0;row<rows;row++) {
        if (e->row_byte[row]==LAYOUT_VOID_ROW) continue;
        for (uint32_t col=e->lay.gw;col<e->row_used[row] && col<e->text_grid.dims.cols;col++) {
            editor_hit hit=e->hits[(size_t)row*e->max_cols+col];
            if (hit.after<=hit.before || hit.after>piece_snapshot_len(h->snapshot)) continue;
            if (h->count && h->worker_cells[h->count-1u].first_byte>=hit.before) continue;
            h->worker_cells[h->count++]=(hl_span){hit.before,hit.after,HL_SCOPE_DEFAULT,0};
        }
    }
    h->handle=work_submit(&e->pool,(work_job){run,h,h->generation,WORK_BULK});
    if (!h->handle.epoch) { release(e); memset(&h->wanted,0,sizeof h->wanted); return; }
    (void)work_mailbox_bind(&e->pool,h->handle,h->generation,receive,e);
}
void editor_hl_paint(editor *e, uint64_t byte, uint32_t *fg)
{
    editor_highlight *h=&e->hl;
    if (h->published_owner!=e->buffer->id) return;
    size_t lo=0,hi=h->published_count;
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2u;
        if (h->published[mid].end_byte<=byte) lo=mid+1u; else hi=mid;
    }
    if (lo<h->published_count && h->published[lo].first_byte<=byte)
        *fg=config_theme_color(e->preferences.theme,hl_scope_key(h->published[lo].scope));
}
