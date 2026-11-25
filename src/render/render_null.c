#include "render/render_internal.h"
#ifdef RENDER_TEST_HOOKS
#include "render/render_test.h"
#endif

typedef enum render_null_phase { NULL_READY, NULL_ISSUE, NULL_FENCE, NULL_DISPLAY } render_null_phase;
typedef struct render_null {
    render_driver_port *port;
    uint8_t *atlas;
    size_t stride;
    render_frame_id id;
    render_null_phase phase;
} render_null;

static render_backend_caps null_caps(const render_setup *s)
{
    return (render_backend_caps){s->limits, true};
}
static render_rc null_open(void **state, base_arena *a, const render_setup *s, render_driver_port *port)
{
    render_null *n = RENDER_STORAGE(a, 1, render_null);
    if (!n || !(n->atlas = RENDER_STORAGE(a, s->limits.atlas_bytes, uint8_t))) return RENDER_CAPACITY;
    n->port = port;
    *state = n;
    return RENDER_OK;
}
static render_rc null_bind(void *state, const render_binding *b, const render_sheet *sheet)
{
    (void)sheet;
    render_null *n = state;
    n->stride = (size_t)b->cell_width * b->cell_height * RENDER_TILE_MAX_SPAN;
    n->phase = NULL_READY;
    return RENDER_OK;
}
static render_rc null_install(void *state, size_t slot, render_delta d, const uint8_t *r8)
{
    render_null *n = state;
    memcpy(n->atlas + slot * n->stride, r8, (size_t)d.width * d.height);
    return RENDER_OK;
}
static render_rc null_consume(void *state, const render_packet *p)
{
    render_null *n = state;
    render_scene_view view;
    render_rc rc = n->port->apply(n->port->scene, p, null_install, n, &view);
    if (rc == RENDER_OK) {
        n->id = p->id;
        n->phase = NULL_ISSUE;
    }
    return rc;
}
static render_rc null_poll(void *state, render_notice *out)
{
    render_null *n = state;
    render_notice_kind kind = RENDER_NOTICE_LOST;
    switch (n->phase) {
    case NULL_READY: return RENDER_WAIT;
    case NULL_ISSUE: kind = RENDER_NOTICE_ISSUED; n->phase = NULL_FENCE; break;
    case NULL_FENCE: kind = RENDER_NOTICE_DEVICE_DONE; n->phase = NULL_DISPLAY; break;
    case NULL_DISPLAY: kind = RENDER_NOTICE_PRESENTED; n->phase = NULL_READY; break;
    }
    *out = (render_notice){kind, n->id, RENDER_SYNTHETIC, os_now_ns()};
    return RENDER_OK;
}
/* Null owner calls are synchronous. No jobs survive consume/poll. */
static render_rc null_quiesce(void *state, render_quiescence mode)
{
    (void)state;
    (void)mode;
    return RENDER_OK;
}
static void null_close(void *state) { (void)state; }

render_driver render_driver_null(void)
{
    static const render_driver_ops ops = {
        null_caps, null_open, null_bind, null_consume, null_poll, null_quiesce, null_close
    };
    return (render_driver){&ops};
}
#ifdef RENDER_TEST_HOOKS
const uint8_t *render_test_atlas(void *state) { return ((render_null *)state)->atlas; }
#endif
