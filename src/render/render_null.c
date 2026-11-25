#include "render/render_internal.h"
#ifdef RENDER_TEST_HOOKS
#include "render/render_test.h"
#endif

typedef enum render_null_phase { NULL_READY, NULL_ISSUE, NULL_FENCE, NULL_DISPLAY } render_null_phase;
/* Exactly issue, fence and display for the sole flight, or one LOST. */
enum { NULL_FEEDBACK_CAP = 3 };
typedef struct render_null {
    render_driver_port *port;
    uint8_t *atlas;
    size_t stride;
    render_frame_id id;
    render_null_phase phase;
    os_thread thread;
    os_waitset waits;
    _Atomic bool closing, cancelling, ready;
    render_notice notices[NULL_FEEDBACK_CAP];
    size_t count, next;
#ifdef RENDER_TEST_HOOKS
    void (*on_take)(void *);
    void *take_ctx;
#endif
} render_null;

static render_backend_caps null_caps(const render_setup *s)
{
    return (render_backend_caps){s->limits, true};
}
static render_null *null_reserve(base_arena *a, const render_setup *s, render_driver_port *port)
{
    render_null *n = RENDER_STORAGE(a, 1, render_null);
    if (!n || !(n->atlas = RENDER_STORAGE(a, s->limits.atlas_bytes, uint8_t))) return NULL;
    n->port = port;
    atomic_init(&n->ready, false);
    atomic_init(&n->closing, false);
    atomic_init(&n->cancelling, false);
    return n;
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
static bool null_receive(void *state, render_notice *out)
{
    render_null *n = state;
    if (!atomic_load_explicit(&n->ready, memory_order_acquire) ||
        render_driver_borrowed(n->port)) return false;
    *out = n->notices[n->next++];
    if (n->next == n->count) {
        n->next = 0;
        atomic_store_explicit(&n->ready, false, memory_order_release);
    }
    return true;
}

static void null_run(render_null *n, const render_packet *packet)
{
#ifdef RENDER_TEST_HOOKS
    if (n->on_take) n->on_take(n->take_ctx);
#endif
    if (!atomic_load_explicit(&n->cancelling, memory_order_acquire)) {
        render_rc rc = packet->sheet ? null_bind(n, &packet->binding, packet->sheet) : RENDER_OK;
        if (rc == RENDER_OK) rc = null_consume(n, packet);
        n->count = 0;
        if (rc == RENDER_OK) {
            while (n->count < NULL_FEEDBACK_CAP && null_poll(n, &n->notices[n->count]) == RENDER_OK)
                ++n->count;
        } else {
            n->notices[n->count++] = (render_notice){.kind = RENDER_NOTICE_LOST, .id = packet->id};
        }
        atomic_store_explicit(&n->ready, true, memory_order_release);
    }
    render_driver_release(n->port);
}

static void null_owner(void *state)
{
    render_null *n = state;
    while (!atomic_load_explicit(&n->closing, memory_order_acquire)) {
        os_wait_event event;
        size_t count;
        ASSERT(os_waitset_wait(&n->waits, OS_DEADLINE_NEVER, &event, 1, &count) == OS_OK);
        os_wake_drain(n->port->wake);
        const render_packet *packet = render_driver_take(n->port);
        if (packet) null_run(n, packet);
    }
}

static render_rc null_open(void **state, base_arena *a, const render_setup *s, render_driver_port *port)
{
    render_null *n = null_reserve(a, s, port);
    if (!n || os_waitset_init(&n->waits) != OS_OK) return RENDER_CAPACITY;
    if (os_waitset_add(&n->waits, os_wake_fd(port->wake), OS_WAIT_IN, 0) != OS_OK ||
        os_thread_start(&n->thread, null_owner, n) != OS_OK) {
        os_waitset_close(&n->waits);
        return RENDER_CAPACITY;
    }
    port->feedback = (render_feedback){n, null_receive};
    *state = n;
    return RENDER_OK;
}

static render_rc null_quiesce(void *state, render_quiescence mode)
{
    render_null *n = state;
    if (mode == RENDER_STOP_EPOCH)
        atomic_store_explicit(&n->cancelling, true, memory_order_release);
    if (render_driver_borrowed(n->port)) return RENDER_WAIT;
    if (mode == RENDER_STOP_EPOCH) {
        /* READY has been retracted and any taken packet has been released.
         * Only UI can drain or discard the old batch before the next submit. */
        n->next = 0;
        atomic_store_explicit(&n->ready, false, memory_order_relaxed);
        atomic_store_explicit(&n->cancelling, false, memory_order_release);
    }
    return RENDER_OK;
}

static void null_close(void *state)
{
    render_null *n = state;
    atomic_store_explicit(&n->closing, true, memory_order_release);
    os_wake_signal(n->port->wake);
    ASSERT(os_thread_join(&n->thread) == OS_OK);
    os_waitset_close(&n->waits);
}

render_driver render_driver_null(void)
{
    static const render_driver_ops ops = {
        null_caps, null_open, null_bind, null_consume, null_poll, null_quiesce, null_close
    };
    return (render_driver){&ops};
}

#ifdef RENDER_TEST_HOOKS
/* The model tests schedule owner actions explicitly to inject evidence in
 * both orders. Use the same backing, consume, poll and quiesce functions. */
static render_rc null_manual_open(void **state, base_arena *a, const render_setup *s, render_driver_port *port)
{
    *state = null_reserve(a, s, port);
    return *state ? RENDER_OK : RENDER_CAPACITY;
}
static void null_manual_close(void *state) { (void)state; }
render_driver render_test_manual_null(void)
{
    static const render_driver_ops ops = {
        null_caps, null_manual_open, null_bind, null_consume, null_poll, null_quiesce, null_manual_close
    };
    return (render_driver){&ops};
}
void render_test_null_on_take(void *state, void (*fn)(void *), void *ctx)
{
    render_null *n = state;
    n->on_take = fn;
    n->take_ctx = ctx;
}
const uint8_t *render_test_atlas(void *state) { return ((render_null *)state)->atlas; }
#endif
