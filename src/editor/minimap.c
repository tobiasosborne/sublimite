#include "editor/private.h"
#include "file/file.h"
#include "trace/trace.h"
#include <string.h>
#define EDITOR_MAP_MESSAGE UINT32_C(0x454d4150) /* ASCII EMAP */
static size_t snapshot_span(void *ctx, uint64_t off, const uint8_t **bytes)
{
    piece_iter it; size_t n=0;
    piece_iter_begin_snapshot(&it,ctx,off);
    return piece_iter_next(&it,bytes,&n) ? n : 0;
}
static void prepare_map(work_ctx *ctx)
{
    editor_buffer *b=ctx->arg;
    if (work_should_stop(ctx)) return;
    if (!b->map_job_sealed) {
        lineidx_src source={b->map_snapshot,piece_snapshot_len(b->map_snapshot),snapshot_span,NULL};
        b->map_job_error=b->map_job_windowed ?
            minimap_prepare_window(&b->map_worker,&b->map_job_input,&source,&b->map_job_anchor,b->map_job_height) :
            minimap_prepare_source(&b->map_worker,&b->map_job_input,&source,b->map_job_height);
        if (file_snapshot_faulted(b->map_snapshot)) b->map_job_error=MINIMAP_ERR_SOURCE;
        b->map_job_sealed=true;
    }
    work_msg msg={.kind=EDITOR_MAP_MESSAGE};
    uintptr_t identity=(uintptr_t)b; memcpy(msg.data,&identity,sizeof identity);
    if (!work_publish(ctx,&msg)) (void)work_continue(ctx);
}
/* edit-cxho.1: slot buffer_capacity is the zero-tab fallback e->empty, which
 * is never installed in e->buffers (large.c's receiver covers it the same
 * way); its map job must publish and release its snapshot like any tab's. */
static editor_buffer *map_buffer(const editor *e, size_t i)
{
    return i<e->buffer_capacity ? (e->buffers ? e->buffers[i] : NULL) : e->empty;
}
bool editor_map_receive(editor *e, const work_msg *msg)
{
    if (msg->kind!=EDITOR_MAP_MESSAGE) return false;
    uintptr_t identity=0; memcpy(&identity,msg->data,sizeof identity);
    for (size_t i=0;i<=e->buffer_capacity;i++) {
        editor_buffer *b=map_buffer(e,i);
        if (!b || (uintptr_t)b!=identity || b->map_handle.epoch!=msg->epoch_ ||
            b->map_handle.slot!=msg->slot_) continue;
        b->map_received=true;
        minimap_input input=editor_map_input(b);
        if (!e->queue_count && !e->dirty && !b->retired && !b->source_stale && !b->map_job_error &&
            b->map_job_height==e->map_grid.dims.rows &&
            minimap_publish(&b->map,&input,&b->map_worker)==MINIMAP_OK && b==e->buffer) {
            if (editor_begin_frame(e)) e->error=EDITOR_ERR_HISTORY;
        }
        break;
    }
    return true;
}
/* edit-ci6i: a refused map snapshot is no editor failure (the edit-bt1
 * pattern). The old map stays on screen; the take is retried on the next
 * tick, edit-dr9's retry interval (E: JOURNAL_DEFAULT_SYNC_NS, the existing
 * durability tick), and wait_timeout wakes the step for it. A refusal that
 * persists past one retry is shown on the status row (the edit-btj notice). */
static const char refused_notice[] = "minimap deferred: buffer memory exhausted";
/* edit-cxho.9: a large file's minimap is the window of real lines around
 * the viewport (minimap_prepare_window), the same size predicate as
 * minimap_prepare's large branch. While the index builds only the byte
 * length is known, and the window anchors at the viewport's byte. */
static bool map_windowed(const minimap_input *in)
{
    return in->source.len > MINIMAP_SMALL_BYTES || (in->index_ready && in->lines > MINIMAP_SMALL_LINES);
}
/* The window for the current viewport. visible counts the logical lines on
 * screen, as compose passes to minimap_fill. Geometry is the painter's:
 * MINIMAP_LINE_PX (2) pixels per summary row over the map grid's rows,
 * unchanged until edit-cxho.10, so row i paints at y = 2i and the band over
 * the viewport's rows falls at the window's box_top (+ phase < 2 px). */
static void map_anchor(const editor *e, const minimap_input *in, minimap_anchor *a)
{
    uint64_t first=e->v.state.first_line, visible=0;
    for (uint32_t r=0;r<e->v.config.rows;r++) if (e->row_byte[r]!=LAYOUT_VOID_ROW) {
        uint64_t line=e->lay.wrap ? e->lay.wrap_rows[r].line : first+r;
        if (line>=first && line-first+1u>visible) visible=line-first+1u;
    }
    *a=(minimap_anchor){.first_line=first,.first_byte=e->v.state.first_byte};
    uint32_t height=e->map_grid.dims.rows*MINIMAP_LINE_PX;
    if (in->index_ready) (void)minimap_window_lines(in->lines,visible,first,MINIMAP_LINE_PX,height,&a->window);
    else (void)minimap_window_bytes(in->source.len,visible,a->first_byte,MINIMAP_LINE_PX,height,&a->window);
}
static bool map_due(const editor *e, editor_buffer *b)
{
    if (!b || !e->map_cols || b->map_snapshot || b->map_sync_fill || b->pending_open || b->source_stale) return false;
    minimap_input input=editor_map_input(b);
    bool windowed=map_windowed(&input);
    if (!input.index_ready && (!windowed || (b->map_hold_unindexed && b->map.filled))) return false;
    if (minimap_stale(&b->map,&input) || b->map.height!=e->map_grid.dims.rows) return true;
    if (!windowed || b!=e->buffer) return false;
    minimap_anchor a; map_anchor(e,&input,&a);
    return minimap_anchor_moved(&b->map,&a); /* the viewport scrolled: a new window */
}
int editor_map_fill_sync(editor *e, const minimap_input *input, uint64_t first,
                         uint64_t visible, const minimap_style *colors)
{
    editor_buffer *b=e->buffer; uint32_t rows=e->map_grid.dims.rows;
    if (map_windowed(input)) {
        minimap_anchor a; map_anchor(e,input,&a);
        if (minimap_stale(&b->map,input) || b->map.height!=rows || minimap_anchor_moved(&b->map,&a)) {
            int rc=minimap_prepare_window(&b->map,input,NULL,&a,rows);
            if (rc) return rc;
        }
    }
    return minimap_fill(&b->map,input,&e->map_grid,0,8,first,visible,colors);
}
uint64_t editor_map_retry_ns(const editor *e)
{
    editor_buffer *b=e->buffer;
    return b && b->map_retry_ns && map_due(e,b) ? b->map_retry_ns : 0;
}
static int defer(editor *e, editor_buffer *b)
{
    b->map_retry_ns=trace_now_ns()+JOURNAL_DEFAULT_SYNC_NS;
    if (b->map_refusals<UINT32_MAX) b->map_refusals++;
    if (b->map_refusals>1u && e->recovery_notice!=refused_notice) {
        e->recovery_notice=refused_notice; e->recovery_notice_mutations=e->stats.mutations;
        (void)editor_begin_frame(e);
    }
    return 0;
}
int editor_map_service(editor *e)
{
    for (size_t i=0;i<=e->buffer_capacity;i++) {
        editor_buffer *b=map_buffer(e,i);
        if (!b) continue;
        if (b->map_snapshot && (b->map_received || b->retired || b->source_stale) &&
            work_handle_finished(&e->pool,b->map_handle)) {
            (void)work_mailbox_bind(&e->pool,b->map_handle,0,NULL,NULL);
            piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL;
        }
    }
    editor_buffer *b=e->buffer;
    if (!map_due(e,b)) return 0;
    if (b->map_retry_ns && trace_now_ns()<b->map_retry_ns) return 0; /* edit-ci6i: keep the old map */
    minimap_input input=editor_map_input(b);
    b->map_snapshot=piece_snapshot_take(b->tree);
    if (!b->map_snapshot) return defer(e,b);
    b->map_retry_ns=0; b->map_refusals=0;
    /* Lane (landing decision, docs/decisions/edit-73j.md): the bulk lane can
     * be monopolised by a large file's index/warm/find jobs, so a summary must
     * not wait behind them. A memory-resident snapshot is CPU-only work of at
     * most 64 KiB scanned (G: MINIMAP_SMALL_BYTES) for a small file, or the
     * window's own lines (edit-cxho.9: the bound in minimap.h, about
     * (2 x walk back + rows) x 81 bytes for summary-width lines) plus
     * map_capacity rows: the foreground lane's contract. File-backed reads
     * can fault (blocking I/O, which that contract excludes): compose
     * prepares their first map synchronously, as before this bead, and only
     * later refreshes use the bulk lane. */
    bool backed=file_snapshot_backing(b->map_snapshot)!=NULL;
    if (backed && !b->map.filled) {
        piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL;
        b->map_sync_fill=true; return editor_begin_frame(e);
    }
    if (backed && !input.index_ready) {
        /* edit-cxho.9: a file-backed refresh runs on the bulk lane, behind
         * the index build that occupies it, and would land stale once the
         * index completes. Keep the first window, retained as pending (its
         * last painted box), until the exact window replaces it. */
        piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL;
        b->map_hold_unindexed=true; b->map.stale=true; return 0;
    }
    b->map_hold_unindexed=false;
    b->map_job_input=input; b->map_job_height=e->map_grid.dims.rows;
    b->map_job_windowed=map_windowed(&input);
    if (b->map_job_windowed) map_anchor(e,&input,&b->map_job_anchor);
    b->map_received=false; b->map_job_sealed=false;
    b->map_handle=work_submit(&e->pool,(work_job){prepare_map,b,0,backed ? WORK_BULK : WORK_FOREGROUND});
    if (!b->map_handle.epoch && !backed) b->map_handle=work_submit(&e->pool,(work_job){prepare_map,b,0,WORK_BULK});
    if (!b->map_handle.epoch) { piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL; }
    return 0;
}
