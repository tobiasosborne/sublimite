/* XTest injection scheduled from Present NotifyMSC UST, CLOCK_MONOTONIC ns.
 * Runtime-load xcb-xtest: the global Makefile libraries need no changes. */
#include "refwin_protocol.h"
#include "trace/trace.h"
#include <dlfcn.h>
#include <poll.h>
#include <time.h>
#include <xcb/present.h>

typedef xcb_void_cookie_t (*keyinject_fake_fn)(xcb_connection_t *, uint8_t, uint8_t,
    uint32_t, xcb_window_t, int16_t, int16_t, uint8_t);
typedef struct keyinject_state {
    xcb_connection_t *conn;
    xcb_window_t root;
    uint8_t opcode, keycode;
    uint32_t serial;
    uint64_t period;
    void *library;
    keyinject_fake_fn fake;
    xcb_atom_t injection, complete, notify, consumed;
} keyinject_state;
typedef struct keyinject_clock { uint64_t ns, msc; } keyinject_clock;

static int keyinject_wait_event(keyinject_state *s, xcb_window_t window, uint8_t kind,
                                uint32_t serial, keyinject_clock *clock, uint32_t *frame)
{
    uint64_t deadline = trace_now_ns()+UINT64_C(10000000000);
    for (;;) {
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_event(s->conn)) != NULL) {
            if (!e->response_type) { free(e); return -1; }
            if ((e->response_type & 0x7fu) == XCB_GE_GENERIC) {
                xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)e;
                if (ge->extension == s->opcode && ge->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
                    xcb_present_complete_notify_event_t *p = (xcb_present_complete_notify_event_t *)e;
                    if (p->window == window && p->kind == kind && (!serial || serial == p->serial)) {
                        if (!p->ust || !p->msc || p->ust > UINT64_MAX/1000u) { free(e); return -1; }
                        *clock = (keyinject_clock){p->ust*1000u,p->msc};
                        if (frame) *frame = p->serial;
                        free(e); return 0;
                    }
                }
            }
            free(e);
        }
        uint64_t now = trace_now_ns();
        if (now > deadline || xcb_connection_has_error(s->conn)) return -1;
        struct pollfd fd = {xcb_get_file_descriptor(s->conn),POLLIN,0};
        if (poll(&fd,1,10) < 0 && errno != EINTR) return -1;
    }
}
static int keyinject_msc(keyinject_state *s, xcb_window_t window, uint64_t target, keyinject_clock *out)
{
    if (s->serial == UINT32_MAX) return -1;
    uint32_t serial = ++s->serial;
    xcb_present_notify_msc(s->conn,window,serial,target,0,0);
    if (xcb_flush(s->conn) <= 0 || keyinject_wait_event(s,window,XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC,serial,out,NULL)) return -1;
    uint64_t now = trace_now_ns();
    /* Xorg/Xvfb UST is monotonic microseconds here. Refuse a different clock
     * domain instead of silently treating realtime or raw ticks as monotonic. */
    return out->ns > now || now-out->ns > UINT64_C(2000000000) ? -1 : 0;
}
static int keyinject_phase(keyinject_state *s, xcb_window_t window, uint64_t phase,
                           keyinject_clock *anchor, uint64_t *deadline)
{
    if (keyinject_msc(s,window,0,anchor)) return -1;
    uint64_t now = trace_now_ns();
    if (anchor->ns > UINT64_MAX-phase) return -1;
    uint64_t when = anchor->ns+phase;
    if (when <= now) {
        uint64_t cycles = (now-when)/s->period+1u;
        if (cycles > (UINT64_MAX-anchor->msc) || cycles > (UINT64_MAX-when)/s->period) return -1;
        uint64_t target = anchor->msc+cycles;
        if (keyinject_msc(s,window,target,anchor)) return -1;
        if (anchor->ns > UINT64_MAX-phase) return -1;
        when = anchor->ns+phase;
    }
    /* A late notification is visible in actual_phase_ns. No quiet retries. */
    *deadline = when;
    return 0;
}
static int keyinject_sleep(uint64_t ns)
{
    struct timespec ts = {(time_t)(ns/UINT64_C(1000000000)),(long)(ns%UINT64_C(1000000000))};
    int rc;
    do { rc = clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&ts,NULL); } while (rc == EINTR);
    return rc ? -1 : 0;
}
static int keyinject_settle(keyinject_state *s)
{
    /* Focus can submit a cursor/focus frame. Drain it before the measured
     * phase so it cannot be mistaken for the injected-key frame. This is a
     * bounded setup wait; the offline join still verifies exact identity. */
    uint64_t limit = trace_now_ns()+UINT64_C(10000000000);
    uint64_t quiet = trace_now_ns()+s->period*2u;
    while (trace_now_ns()<quiet) {
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_event(s->conn)) != NULL) {
            if (!e->response_type) { free(e); return -1; }
            if ((e->response_type & 0x7fu) == XCB_GE_GENERIC) quiet = trace_now_ns()+s->period*2u;
            free(e);
        }
        if (trace_now_ns()>limit || xcb_connection_has_error(s->conn)) return -1;
        struct pollfd fd = {xcb_get_file_descriptor(s->conn),POLLIN,0};
        if (poll(&fd,1,1) < 0 && errno != EINTR) return -1;
    }
    return 0;
}
static xcb_void_cookie_t keyinject_send(keyinject_state *s, xcb_window_t window, uint8_t type)
{
    if (s->fake) {
        /* Check after press+release, outside the injection instant. */
        return s->fake(s->conn,type,s->keycode,XCB_CURRENT_TIME,s->root,0,0,0);
    }
    xcb_key_press_event_t e = {.response_type=type,.detail=s->keycode,.time=XCB_CURRENT_TIME,
        .root=s->root,.event=window,.same_screen=1};
    return xcb_send_event_checked(s->conn,0,window,type == XCB_KEY_PRESS ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE,(const char *)&e);
}
static int keyinject_sample(keyinject_state *s, FILE *csv, xcb_window_t window,
                            const char *target, uint32_t pair, uint64_t phase, bool reference)
{
    /* Focus and its roundtrip precede phase sampling; no focus requests in
     * the measured injection-to-change interval. */
    if (!refproto_checked(s->conn,xcb_set_input_focus_checked(s->conn,XCB_INPUT_FOCUS_PARENT,window,XCB_CURRENT_TIME))) return -1;
    if (!reference && keyinject_settle(s)) return -1;
    keyinject_clock anchor; uint64_t deadline;
    if (keyinject_phase(s,window,phase,&anchor,&deadline) || keyinject_sleep(deadline)) return -1;
    refproto_row row = {.pair=pair,.phase=phase,.period=s->period};
    row.inject = trace_now_ns();
    if (row.inject < anchor.ns) return -1;
    uint64_t elapsed = row.inject-anchor.ns;
    /* MSC is the measured phase anchor, not an extrapolated counter. Keep
     * lateness unwrapped: actual >= period exposes a missed refresh. */
    row.msc = anchor.msc;
    row.actual = elapsed;
    xcb_void_cookie_t press = keyinject_send(s,window,XCB_KEY_PRESS);
    xcb_void_cookie_t release = keyinject_send(s,window,XCB_KEY_RELEASE);
    if (xcb_flush(s->conn) <= 0 || !refproto_checked(s->conn,press) || !refproto_checked(s->conn,release)) return -1;
    if (reference) {
        if (refproto_set(s->conn,window,s->injection,&row)) return -1;
        xcb_client_message_event_t msg = {.response_type=XCB_CLIENT_MESSAGE,.format=32,.window=window,.type=s->notify};
        msg.data.data32[0] = pair;
        if (!refproto_checked(s->conn,xcb_send_event_checked(s->conn,0,window,0,(const char *)&msg))) return -1;
        uint64_t limit = trace_now_ns()+UINT64_C(10000000000);
        for (;;) {
            refproto_row ack;
            int rc = refproto_get(s->conn,window,s->complete,&ack);
            if (rc < 0) return -1;
            if (!rc && ack.pair == pair) {
                if (ack.inject != row.inject || ack.msc != row.msc || ack.phase != phase ||
                    ack.period != row.period || ack.actual != row.actual || !ack.frame ||
                    ack.t4 < row.inject || ack.t5 < ack.t4 || ack.t6 < ack.t4) return -1;
                row = ack; break;
            }
            if (trace_now_ns()>limit || keyinject_sleep(trace_now_ns()+UINT64_C(1000000))) return -1;
        }
        if (refproto_csv(csv,target,&row) || refproto_set(s->conn,window,s->consumed,&row)) return -1;
    } else {
        keyinject_clock present;
        if (keyinject_wait_event(s,window,XCB_PRESENT_COMPLETE_KIND_PIXMAP,0,&present,&row.frame)) return -1;
        /* Observation only. The joiner replaces this with the matching trace
         * hook's T6 and supplies T4/T5. Zeros explicitly mean unavailable. */
        row.t6 = trace_now_ns();
        if (refproto_csv(csv,target,&row)) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t window = 0, editor_window = 0, pairs = 1, first = 1, period = 16666667, phase = 0, seed = 1, keycode = 38;
    bool fixed_phase = false, reference = false, send_event = false;
    const char *target = "editor", *csv_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"--help")) {
            puts("usage: keyinject --window ID --csv PATH [--pairs N] [--first-pair N]\n"
                 "  [--target editor|reference] [--wait-reference] [--editor-window ID]\n"
                 "  [--keycode N] [--period-ns N] [--phase-ns N | --seed N]\n"
                 "  [--send-event] (force the synthetic fallback; TRACK only)\n"
                 "Without phase-ns, a seeded phase is chosen once per pair; both targets use it.\n"
                 "Editor endpoints require tools/refwin_pairs.py with EDIT_TRACE_DUMP."); return 0;
        }
        if (!strcmp(argv[i],"--wait-reference")) { reference = true; continue; }
        if (!strcmp(argv[i],"--send-event")) { send_event = true; continue; }
        if (i+1 >= argc) return 2;
        const char *option = argv[i++], *value = argv[i];
        uint64_t *dest = NULL, max = UINT32_MAX;
        if (!strcmp(option,"--csv")) csv_path = value;
        else if (!strcmp(option,"--target")) target = value;
        else if (!strcmp(option,"--window")) dest = &window;
        else if (!strcmp(option,"--editor-window")) dest = &editor_window;
        else if (!strcmp(option,"--pairs")) { dest = &pairs; max = 1000000; }
        else if (!strcmp(option,"--first-pair")) dest = &first;
        else if (!strcmp(option,"--keycode")) { dest = &keycode; max = 255; }
        else if (!strcmp(option,"--period-ns")) { dest = &period; max = UINT64_C(1000000000); }
        else if (!strcmp(option,"--phase-ns")) { dest = &phase; max = UINT64_C(1000000000); fixed_phase = true; }
        else if (!strcmp(option,"--seed")) { dest = &seed; max = UINT64_MAX; }
        else return 2;
        if (dest && refproto_number(value,max,dest)) return 2;
    }
    if (!window || !pairs || !first || first+pairs-1u > UINT32_MAX || !period ||
        phase >= period || keycode < 8 || !seed || !csv_path ||
        (strcmp(target,"reference") && strcmp(target,"editor")) ||
        (reference && strcmp(target,"reference")) || (editor_window && (!reference || window == editor_window))) return 2;
    /* NotifyMSC serials occupy a separate namespace from app frame IDs. The
     * platform's trace listener also sees these notifications. */
    keyinject_state s = {.keycode=(uint8_t)keycode,.period=period,.serial=UINT32_C(0x80000000)};
    int screen = 0, rc = 1;
    s.conn = xcb_connect(NULL,&screen);
    FILE *csv = NULL;
    if (!s.conn || xcb_connection_has_error(s.conn)) goto done;
    xcb_screen_iterator_t si = xcb_setup_roots_iterator(xcb_get_setup(s.conn));
    while (screen-- > 0 && si.rem) xcb_screen_next(&si);
    if (!si.rem) goto done;
    s.root = si.data->root;
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(s.conn,&xcb_present_id);
    if (!ext || !ext->present) goto done;
    s.opcode = ext->major_opcode;
    if (!refproto_checked(s.conn,xcb_present_select_input_checked(s.conn,xcb_generate_id(s.conn),(xcb_window_t)window,
        XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY))) goto done;
    if (editor_window && !refproto_checked(s.conn,xcb_present_select_input_checked(s.conn,xcb_generate_id(s.conn),(xcb_window_t)editor_window,
        XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY))) goto done;
    s.injection = refproto_atom(s.conn,"_EDIT_REF_INJECT"); s.complete = refproto_atom(s.conn,"_EDIT_REF_COMPLETE");
    s.notify = refproto_atom(s.conn,"_EDIT_REF_NOTIFY"); s.consumed = refproto_atom(s.conn,"_EDIT_REF_CONSUMED");
    if (!s.injection || !s.complete || !s.notify || !s.consumed) goto done;
    xcb_query_extension_reply_t *xtest = xcb_query_extension_reply(s.conn,xcb_query_extension(s.conn,5,"XTEST"),NULL);
    if (!send_event && xtest && xtest->present) {
        s.library = dlopen("libxcb-xtest.so.0",RTLD_NOW|RTLD_LOCAL);
        if (s.library) {
            void *symbol = dlsym(s.library,"xcb_test_fake_input_checked");
            _Static_assert(sizeof symbol == sizeof s.fake,"POSIX function pointer representation");
            memcpy(&s.fake,&symbol,sizeof s.fake);
        }
    }
    free(xtest);
    fprintf(stderr,"keyinject: %s; Present UST phase, period=%" PRIu64 " ns (configured, E); Xvfb has no real vblank\n",
        s.fake ? "XTest (dlopen libxcb-xtest.so.0)" : "XSendEvent FALLBACK: synthetic flag; editor may ignore it; not physical input",period);
    csv = !strcmp(csv_path,"-") ? stdout : fopen(csv_path,"w");
    if (!csv || fputs(REFPROTO_HEADER,csv) == EOF || fflush(csv)) goto done;
    for (uint64_t i = 0; i < pairs; i++) {
        uint64_t chosen = phase;
        if (!fixed_phase) { seed ^= seed<<13; seed ^= seed>>7; seed ^= seed<<17; chosen = seed%period; }
        uint32_t pair = (uint32_t)(first+i);
        if (editor_window && (i&1u) && keyinject_sample(&s,csv,(xcb_window_t)editor_window,"editor",pair,chosen,false)) goto done;
        if (keyinject_sample(&s,csv,(xcb_window_t)window,target,pair,chosen,reference)) goto done;
        if (editor_window && !(i&1u) && keyinject_sample(&s,csv,(xcb_window_t)editor_window,"editor",pair,chosen,false)) goto done;
    }
    rc = 0;
done:
    if (csv && csv != stdout && fclose(csv)) rc = 1;
    if (s.conn) xcb_disconnect(s.conn);
    if (s.library) dlclose(s.library);
    if (rc) fprintf(stderr,"keyinject: failed (display, Present clock/frame, injection, or CSV); retain incomplete CSV\n");
    return rc;
}
