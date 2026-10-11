#include "editor/private.h"
#include "file/file.h"
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
        b->map_job_error=minimap_prepare_source(&b->map_worker,&b->map_job_input,&source,b->map_job_height);
        if (file_snapshot_faulted(b->map_snapshot)) b->map_job_error=MINIMAP_ERR_SOURCE;
        b->map_job_sealed=true;
    }
    work_msg msg={.kind=EDITOR_MAP_MESSAGE};
    uintptr_t identity=(uintptr_t)b; memcpy(msg.data,&identity,sizeof identity);
    if (!work_publish(ctx,&msg)) (void)work_continue(ctx);
}
bool editor_map_receive(editor *e, const work_msg *msg)
{
    if (msg->kind!=EDITOR_MAP_MESSAGE) return false;
    uintptr_t identity=0; memcpy(&identity,msg->data,sizeof identity);
    for (size_t i=0;i<e->buffer_capacity;i++) {
        editor_buffer *b=e->buffers ? e->buffers[i] : NULL;
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
int editor_map_service(editor *e)
{
    for (size_t i=0;i<e->buffer_capacity;i++) {
        editor_buffer *b=e->buffers ? e->buffers[i] : NULL;
        if (!b) continue;
        if (b->map_snapshot && (b->map_received || b->retired || b->source_stale) &&
            work_handle_finished(&e->pool,b->map_handle)) {
            (void)work_mailbox_bind(&e->pool,b->map_handle,0,NULL,NULL);
            piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL;
        }
    }
    editor_buffer *b=e->buffer;
    if (!b || !e->map_cols || b->map_snapshot || b->map_sync_fill || b->pending_open || b->source_stale) return 0;
    minimap_input input=editor_map_input(b);
    if (!input.index_ready || (!minimap_stale(&b->map,&input) && b->map.height==e->map_grid.dims.rows)) return 0;
    b->map_snapshot=piece_snapshot_take(b->tree);
    if (!b->map_snapshot) return EDITOR_ERR_MEMORY;
    /* Lane (landing decision, docs/decisions/edit-73j.md): the bulk lane can
     * be monopolised by a large file's index/warm/find jobs, so a summary must
     * not wait behind them. A memory-resident snapshot is CPU-only work of at
     * most 64 KiB scanned (G: MINIMAP_SMALL_BYTES, or MINIMAP_MAX_SAMPLES x
     * MINIMAP_SAMPLE_BYTES = 256 x 256) plus map_capacity rows: the foreground
     * lane's contract. File-backed sample reads can fault (blocking I/O, which
     * that contract excludes): compose prepares their first map synchronously,
     * as before this bead, and only later refreshes use the bulk lane. */
    bool backed=file_snapshot_backing(b->map_snapshot)!=NULL;
    if (backed && !b->map.filled) {
        piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL;
        b->map_sync_fill=true; return editor_begin_frame(e);
    }
    b->map_job_input=input; b->map_job_height=e->map_grid.dims.rows;
    b->map_received=false; b->map_job_sealed=false;
    b->map_handle=work_submit(&e->pool,(work_job){prepare_map,b,0,backed ? WORK_BULK : WORK_FOREGROUND});
    if (!b->map_handle.epoch && !backed) b->map_handle=work_submit(&e->pool,(work_job){prepare_map,b,0,WORK_BULK});
    if (!b->map_handle.epoch) { piece_snapshot_release(b->map_snapshot); b->map_snapshot=NULL; }
    return 0;
}
