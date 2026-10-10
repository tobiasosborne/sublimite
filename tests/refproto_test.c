/* Deterministic hardware-clock fixtures around the exact tool preflight.
 * Window/focus/version checks are real :99; only NotifyMSC replies are scripted. */
#include <xcb/present.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
static xcb_void_cookie_t fixture_notify(void *, xcb_connection_t *, xcb_window_t, uint32_t,
    uint64_t, uint64_t, uint64_t);
static xcb_generic_event_t *fixture_poll(void *, xcb_connection_t *);
#define xcb_present_notify_msc_checked(c,w,s,t,v,r) fixture_notify(d,c,w,s,t,v,r)
#define xcb_poll_for_event(c) fixture_poll(d,c)
#include "../tools/refwin_protocol.h"
#undef xcb_present_notify_msc_checked
#undef xcb_poll_for_event

typedef struct fixture {
    /* First member lets the compile-time NotifyMSC seam recover this fixture
     * from the display argument; all state belongs to this test invocation. */
    refproto_display display;
    refproto_clock clocks[8];
    unsigned clock_count, clock_at;
    uint32_t pending_serial;
    xcb_window_t pending_window;
} fixture;
static xcb_void_cookie_t fixture_notify(void *ctx, xcb_connection_t *c, xcb_window_t w, uint32_t serial,
    uint64_t target, uint64_t divisor, uint64_t remainder)
{
    fixture *f = ctx;
    (void)divisor; (void)remainder;
    if (f->clock_at >= f->clock_count || (f->clocks[f->clock_at].msc && f->clocks[f->clock_at].msc < target)) exit(2);
    f->pending_window = w; f->pending_serial = serial;
    /* Use a real checked cookie so request errors/connection checks stay live. */
    return xcb_no_operation_checked(c);
}
static xcb_generic_event_t *fixture_poll(void *ctx, xcb_connection_t *c)
{
    fixture *f = ctx;
    if (!f->pending_serial) return xcb_poll_for_event(c);
    void *memory = calloc(1,sizeof(xcb_present_complete_notify_event_t));
    if (!memory) exit(2);
    xcb_present_complete_notify_event_t *e = memory;
    e->response_type = XCB_GE_GENERIC; e->extension = f->display.opcode; e->event_type = XCB_PRESENT_COMPLETE_NOTIFY;
    e->kind = XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC; e->window = f->pending_window; e->serial = f->pending_serial;
    e->ust = f->clocks[f->clock_at].ns/1000u; e->msc = f->clocks[f->clock_at++].msc;
    f->pending_serial = 0; return memory;
}

#define T(c) do { if (!(c)) { fprintf(stderr,"refproto_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
static void stream(fixture *f, uint64_t period)
{
    uint64_t base = trace_now_ns()-period*32u;
    f->clock_count = 6; f->clock_at = 0; f->pending_serial = 0;
    f->clocks[0].ns = f->clocks[0].msc = 0;
    for (unsigned i = 1; i < f->clock_count; i++) {
        f->clocks[i].ns = base+(uint64_t)(i-1u)*4u*period;
        f->clocks[i].msc = 100u+(uint64_t)(i-1u)*4u;
    }
}
int main(void)
{
    fixture state = {0}; fixture *f = &state;
    refproto_display *d = &f->display; T(refproto_display_open(d,"refproto_test",true) == 0);
    xcb_window_t w = xcb_generate_id(d->conn);
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(d->conn)).data;
    T(refproto_checked(d->conn,xcb_create_window_checked(d->conn,XCB_COPY_FROM_PARENT,w,d->root,0,0,16,16,0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,XCB_CW_OVERRIDE_REDIRECT,(uint32_t[]){1})));
    T(refproto_checked(d->conn,xcb_map_window_checked(d->conn,w)));
    refproto_target t = {.window=w};
    stream(f,16666667); T(refproto_preflight(d,&t) == 0); T(refproto_period_matches(t.period,16666667));
    T(f->clock_at == 6 && t.last.msc == 116);
    stream(f,11111111); T(refproto_preflight(d,&t) == 0); T(refproto_period_matches(t.period,11111111));
    stream(f,16666667);
    for (unsigned i = 3; i < f->clock_count; i++) f->clocks[i].ns = f->clocks[2].ns+(uint64_t)(i-2u)*4u*11111111u;
    T(refproto_preflight(d,&t) < 0);
    stream(f,16666667); f->clocks[1].ns = trace_now_ns()+UINT64_C(1000000000); T(refproto_preflight(d,&t) < 0);
    stream(f,16666667); f->clocks[1].ns = trace_now_ns()-UINT64_C(3000000000); T(refproto_preflight(d,&t) < 0);
    stream(f,16666667); for (unsigned i = 0; i < f->clock_count; i++) f->clocks[i].ns = f->clocks[i].msc = 0;
    T(refproto_preflight(d,&t) < 0); T(f->clock_at == 5);
    xcb_destroy_window(d->conn,w); refproto_display_close(d);
    puts("refproto_test: zero-clock bootstrap, stable 60/90 Hz, rate change, future/stale/zero clocks PASS");
    return 0;
}
