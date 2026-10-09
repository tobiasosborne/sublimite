/* Reference backend: common adapter validates/counts; no retained frame data.
 * All caller data is consumed before submit returns. No allocation at all. */
#include "render/render.h"
#include "trace/trace.h"

typedef struct render_null_state { uint32_t initialized; } render_null_state;

static int render_null_init(render_backend *b, const render_config *config)
{
    (void)config;
    render_null_state *s = b->state; s->initialized = 1;
    return RENDER_OK;
}
static int render_null_resize(render_backend *b, render_dims dims)
{
    (void)dims; render_null_state *s = b->state;
    return s->initialized ? RENDER_OK : RENDER_ERR_STATE;
}
static int render_null_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{
    (void)g; (void)strips; (void)count;
    render_null_state *s = b->state;
    return s->initialized ? RENDER_OK : RENDER_ERR_STATE;
}
static int render_null_present(render_backend *b, uint32_t frame_id)
{
    /* Adapter withholds both notifications until T4. Null has no device or
     * display: synchronous observation is an artificial completion timestamp. */
    uint64_t ns = trace_now_ns();
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, frame_id, ns);
    if (rc != RENDER_OK) return rc;
    return render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, frame_id, ns);
}
static int render_null_event(render_backend *b, const render_event *event)
{ (void)b; (void)event; return RENDER_ERR_UNSUPPORTED; }
static void render_null_shutdown(render_backend *b)
{
    render_null_state *s = b->state; s->initialized = 0;
}
int render_null_backend(render_backend *b)
{
    if (b == NULL) return RENDER_ERR_ARG;
    if (b->initialized) return RENDER_ERR_STATE;
    *b = (render_backend){
        .info = {"null",sizeof(render_null_state),_Alignof(render_null_state),
                 RENDER_CAP_DEVICE_TIMING | RENDER_CAP_PRESENT_TIMING | RENDER_CAP_HEADLESS},
        .ops = {render_null_init,render_null_resize,render_null_submit,
                render_null_present,render_null_event,render_null_shutdown}
    };
    return RENDER_OK;
}
