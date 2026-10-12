#include "editor/private.h"
#include "gl/gl.h"
#include "raster/raster.h"
#include "trace/trace.h"

static int observe(editor *e, uint32_t id, uint64_t seq, uint64_t ns,
                   bool synthetic, bool opportunities)
{
    if (!e || !seq || !ns || seq <= e->refresh_sequence || ns < e->refresh_ns)
        return EDITOR_ERR_ARG;
    editor_refresh observation = {.displayed_frame_id = id, .refresh_sequence = seq,
        .completion_ns = ns, .synthetic = synthetic, .opportunity_stream = opportunities};
    for (size_t i = 0; i < sizeof e->refresh_frames / sizeof e->refresh_frames[0]; i++) {
        const editor_frame *frame = &e->refresh_frames[i];
        if (id && frame->id == id) { observation.frame = *frame; observation.metadata_valid = true; break; }
    }
    /* (P) elapsed time / elapsed physical refresh opportunities. Synthetic
     * submit completions cannot measure a display interval. */
    if (!synthetic && e->refresh_sequence && ns > e->refresh_ns)
        e->hl.refresh_interval_ns = (ns-e->refresh_ns)/(seq-e->refresh_sequence);
    e->refresh_sequence = seq; e->refresh_ns = ns;
    if (e->cfg.on_refresh) e->cfg.on_refresh(e->cfg.hook_ctx, &observation);
    return 0;
}
int editor_on_refresh(editor *e, uint32_t id, uint64_t seq, uint64_t ns, bool synthetic)
{ return observe(e, id, seq, ns, synthetic, true); }
void editor_refresh_complete(void *ctx, uint32_t id, uint64_t ns)
{
    editor *e = ctx;
    render_trace_present_complete(NULL, id, ns);
    /* Null signals during present. Deliver after on_present and after the
     * immutable submission record has its present timestamp. */
    e->completion_id = id; e->completion_ns = ns;
}
void editor_refresh_flush(editor *e)
{
    if (!e->completion_id) return;
    bool synthetic = (e->backend->info.capabilities & RENDER_CAP_HEADLESS) != 0;
    uint64_t sequence = 0, ust = 0;
    if (synthetic) sequence = e->refresh_sequence + 1u;
    else if (!raster_last_present(e->backend, &ust, &sequence)) sequence = gl_displayed_msc(e->backend);
    /* Absence of MSC is explicitly unsupported. Do not invent a physical
     * refresh counter from the number of submissions. */
    if (!sequence) {
        editor_refresh observation = {.displayed_frame_id = e->completion_id,
            .completion_ns = e->completion_ns};
        for (size_t i = 0; i < sizeof e->refresh_frames / sizeof e->refresh_frames[0]; i++)
            if (e->refresh_frames[i].id == e->completion_id) {
                observation.frame = e->refresh_frames[i]; observation.metadata_valid = true;
            }
        if (e->cfg.on_refresh) e->cfg.on_refresh(e->cfg.hook_ctx, &observation);
    } else (void)observe(e, e->completion_id, sequence, e->completion_ns, synthetic, synthetic);
    e->completion_id = 0;
}
