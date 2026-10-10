/* XTest injection scheduled from measured per-window Present NotifyMSC UST. */
#include "refwin_protocol.h"

typedef struct keyinject_state {
    refproto_display display;
    uint8_t keycode;
    bool send_event;
    xcb_atom_t injection, complete, notify, consumed;
} keyinject_state;

static int keyinject_sleep(uint64_t ns)
{
    struct timespec ts = {(time_t)(ns/UINT64_C(1000000000)),(long)(ns%UINT64_C(1000000000))};
    int rc;
    do { rc = clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&ts,NULL); } while (rc == EINTR);
    return rc;
}
static int keyinject_anchor(keyinject_state *s, refproto_target *t, uint64_t target, refproto_clock *anchor)
{
    refproto_display *d = &s->display;
    if (refproto_msc(d,t->window,target,anchor) || refproto_clock_valid(d,t->window,*anchor)) return -1;
    if (anchor->msc < t->last.msc || anchor->ns < t->last.ns)
        return refproto_fail(d,t->window,"MSC/UST went backwards since calibration (output changed)");
    if (anchor->msc > t->last.msc) {
        uint64_t period;
        if (refproto_period(t->last,*anchor,&period) || (!d->synthetic_clock && !refproto_period_matches(t->period,period)))
            return refproto_fail(d,t->window,"MSC stream period changed since calibration; calibrated=%" PRIu64 " ns",t->period);
    }
    t->last = *anchor; return 0;
}
static int keyinject_phase(keyinject_state *s, refproto_target *t, uint64_t phase,
                            refproto_clock *anchor, uint64_t *deadline)
{
    refproto_display *d = &s->display;
    if (keyinject_anchor(s,t,0,anchor)) return -1;
    uint64_t now = trace_now_ns();
    if (anchor->ns > UINT64_MAX-phase) return refproto_fail(d,t->window,"phase deadline overflow");
    uint64_t when = anchor->ns+phase;
    if (when <= now) {
        uint64_t cycles = (now-when)/t->period+1u;
        if (cycles > UINT64_MAX-anchor->msc) return refproto_fail(d,t->window,"phase MSC overflow");
        if (keyinject_anchor(s,t,anchor->msc+cycles,anchor)) return -1;
        if (anchor->ns > UINT64_MAX-phase) return refproto_fail(d,t->window,"phase deadline overflow");
        when = anchor->ns+phase;
    }
    /* Keep notification/scheduler lateness visible. Never retry a sampled key. */
    *deadline = when; return 0;
}
static int keyinject_settle(keyinject_state *s, refproto_target *t)
{
    refproto_display *d = &s->display;
    uint64_t limit = trace_now_ns()+UINT64_C(10000000000);
    uint64_t quiet = trace_now_ns()+t->period*2u;
    while (trace_now_ns()<quiet) {
        xcb_generic_event_t *e;
        while ((e = xcb_poll_for_event(d->conn))) {
            if (!e->response_type) { free(e); return refproto_fail(d,t->window,"focus settle: asynchronous X error"); }
            if ((e->response_type & 0x7fu) == XCB_GE_GENERIC) quiet = trace_now_ns()+t->period*2u;
            free(e);
        }
        if (trace_now_ns()>limit) return refproto_fail(d,t->window,"focus settle timeout (Present stream never quiet)");
        if (xcb_connection_has_error(d->conn)) return refproto_fail(d,t->window,"focus settle: connection lost");
        struct pollfd fd = {xcb_get_file_descriptor(d->conn),POLLIN,0};
        if (poll(&fd,1,1) < 0 && errno != EINTR) return refproto_fail(d,t->window,"focus settle poll: %s",strerror(errno));
    }
    return 0;
}
static xcb_void_cookie_t keyinject_send(keyinject_state *s, xcb_window_t window, uint8_t type)
{
    refproto_display *d = &s->display;
    if (!s->send_event) return d->fake(d->conn,type,s->keycode,XCB_CURRENT_TIME,d->root,0,0,0);
    xcb_key_press_event_t e = {.response_type=type,.detail=s->keycode,.time=XCB_CURRENT_TIME,
        .root=d->root,.event=window,.same_screen=1};
    return xcb_send_event_checked(d->conn,0,window,
        type == XCB_KEY_PRESS ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE,(const char *)&e);
}
static int keyinject_sample(keyinject_state *s, FILE *csv, refproto_target *t,
                             const char *target, uint32_t pair, uint64_t phase, bool reference)
{
    refproto_display *d = &s->display;
    xcb_window_t window = t->window;
    if (refproto_keycode(d,window,s->keycode) || refproto_window(d,t,true) || refproto_focus(d,window,false)) return -1;
    if (!reference && keyinject_settle(s,t)) return -1;
    refproto_clock anchor = {0}; uint64_t deadline = 0;
    if (keyinject_phase(s,t,phase,&anchor,&deadline)) return -1;
    int sleep_rc = keyinject_sleep(deadline);
    if (sleep_rc) return refproto_fail(d,window,"phase clock_nanosleep: %s",strerror(sleep_rc));
    refproto_row row = {.pair=pair,.phase=phase,.period=t->period,.msc=anchor.msc};
    row.inject = trace_now_ns();
    if (row.inject < anchor.ns) return refproto_fail(d,window,"injection time precedes MSC anchor");
    row.actual = row.inject-anchor.ns;
    xcb_void_cookie_t press = keyinject_send(s,window,XCB_KEY_PRESS);
    xcb_void_cookie_t release = keyinject_send(s,window,XCB_KEY_RELEASE);
    if (xcb_flush(d->conn) <= 0) return refproto_fail(d,window,"press/release flush failed");
    if (refproto_request(d,window,press,"key press") || refproto_request(d,window,release,"key release")) return -1;
    if (reference) {
        if (refproto_set(d->conn,window,s->injection,&row)) return refproto_fail(d,window,"write injection metadata property failed");
        xcb_client_message_event_t msg = {.response_type=XCB_CLIENT_MESSAGE,.format=32,.window=window,.type=s->notify};
        msg.data.data32[0] = pair;
        if (refproto_request(d,window,xcb_send_event_checked(d->conn,0,window,0,(const char *)&msg),"reference metadata ClientMessage")) return -1;
        uint64_t limit = trace_now_ns()+UINT64_C(10000000000);
        for (;;) {
            refproto_row ack;
            int rc = refproto_get(d->conn,window,s->complete,&ack);
            if (rc < 0) return refproto_fail(d,window,"read reference completion property failed");
            if (!rc && ack.pair == pair) {
                if (ack.inject != row.inject || ack.msc != row.msc || ack.phase != phase ||
                    ack.period != row.period || ack.actual != row.actual)
                    return refproto_fail(d,window,"reference acknowledgement injection metadata mismatch");
                if (!ack.frame || ack.t4 < row.inject || ack.t5 < ack.t4 || ack.t6 < ack.t4)
                    return refproto_fail(d,window,"reference acknowledgement missing frame/nonmonotonic endpoints");
                row = ack; break;
            }
            if (trace_now_ns()>limit) return refproto_fail(d,window,"reference acknowledgement timeout: pair=%" PRIu32,pair);
            sleep_rc = keyinject_sleep(trace_now_ns()+UINT64_C(1000000));
            if (sleep_rc) return refproto_fail(d,window,"acknowledgement clock_nanosleep: %s",strerror(sleep_rc));
        }
        if (refproto_csv(csv,target,&row)) return refproto_fail(d,window,"write/flush injection CSV: %s",strerror(errno));
        if (refproto_set(d->conn,window,s->consumed,&row)) return refproto_fail(d,window,"write consumed acknowledgement failed");
    } else {
        refproto_clock present;
        if (refproto_wait(d,window,XCB_PRESENT_COMPLETE_KIND_PIXMAP,0,&present,&row.frame)) return -1;
        if (refproto_clock_valid(d,window,present)) return -1;
        /* Joiner verifies the exact traced key frame and replaces T4/T5/T6. */
        row.t6 = trace_now_ns();
        if (refproto_csv(csv,target,&row)) return refproto_fail(d,window,"write/flush injection CSV: %s",strerror(errno));
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t window = 0, editor_window = 0, pairs = 1, first = 1, expected_period = 0, phase = 0, seed = 1, keycode = 38;
    bool fixed_phase = false, reference = false, send_event = false, dry_run = false, synthetic_clock = false;
    const char *target = "editor", *csv_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"--help")) {
            puts("usage: keyinject --window ID [--dry-run | --csv PATH] [--pairs N] [--first-pair N]\n"
                 "  [--target editor|reference] [--wait-reference] [--editor-window ID]\n"
                 "  [--keycode N] [--period-ns N] [--phase-ns N | --seed N]\n"
                 "  [--send-event] (explicit synthetic fallback; TRACK only)\n"
                 "  [--synthetic-clock] (Xvfb only: skip rate stability/comparison checks; TRACK only)\n"
                 "Period is measured per target. --period-ns asserts expected rate within 1%, never configures it.\n"
                 "--dry-run verifies XTest, mapping, focus and advancing Present UST/MSC; sends no keys/writes no CSV.\n"
                 "Editor endpoints require tools/refwin_pairs.py with EDIT_TRACE_DUMP."); return 0;
        }
        if (!strcmp(argv[i],"--synthetic-clock")) { synthetic_clock = true; continue; }
        if (!strcmp(argv[i],"--dry-run")) { dry_run = true; continue; }
        if (!strcmp(argv[i],"--wait-reference")) { reference = true; continue; }
        if (!strcmp(argv[i],"--send-event")) { send_event = true; continue; }
        if (i+1 >= argc) { fprintf(stderr,"keyinject: missing value for %s\n",argv[i]); return 2; }
        const char *option = argv[i++], *value = argv[i];
        uint64_t *dest = NULL, max = UINT32_MAX;
        if (!strcmp(option,"--csv")) csv_path = value;
        else if (!strcmp(option,"--target")) target = value;
        else if (!strcmp(option,"--window")) dest = &window;
        else if (!strcmp(option,"--editor-window")) dest = &editor_window;
        else if (!strcmp(option,"--pairs")) { dest = &pairs; max = 1000000; }
        else if (!strcmp(option,"--first-pair")) dest = &first;
        else if (!strcmp(option,"--keycode")) { dest = &keycode; max = 255; }
        else if (!strcmp(option,"--period-ns")) { dest = &expected_period; max = UINT64_C(1000000000); }
        else if (!strcmp(option,"--phase-ns")) { dest = &phase; max = UINT64_C(1000000000); fixed_phase = true; }
        else if (!strcmp(option,"--seed")) { dest = &seed; max = UINT64_MAX; }
        else { fprintf(stderr,"keyinject: unknown option %s\n",option); return 2; }
        if (dest && (refproto_number(value,max,dest) || (dest == &expected_period && !expected_period))) { fprintf(stderr,"keyinject: invalid %s=%s\n",option,value); return 2; }
    }
    const char *argument_error = !window ? "--window must be nonzero" :
        !pairs ? "--pairs must be nonzero" : !first || first+pairs-1u > UINT32_MAX ? "pair id range is invalid" :
        keycode < 8 ? "--keycode must be at least 8" : !seed ? "--seed must be nonzero" :
        !dry_run && !csv_path ? "--csv is required without --dry-run" :
        strcmp(target,"reference") && strcmp(target,"editor") ? "--target must be editor or reference" :
        reference && strcmp(target,"reference") ? "--wait-reference requires --target reference" :
        editor_window && window == editor_window ? "reference/editor window IDs must differ" :
        editor_window && !dry_run && !reference ? "paired injection requires --wait-reference" : NULL;
    if (argument_error) { fprintf(stderr,"keyinject: %s\n",argument_error); return 2; }
    keyinject_state s = {.keycode=(uint8_t)keycode,.send_event=send_event};
    int rc = 1;
    FILE *csv = NULL;
    refproto_target ref = {.window=(xcb_window_t)window}, ed_target = {.window=(xcb_window_t)editor_window};
    if (refproto_display_open(&s.display,"keyinject",dry_run || !send_event)) goto done;
    s.display.synthetic_clock = synthetic_clock;
    if (refproto_keycode(&s.display,ref.window,s.keycode) ||
        ((reference || editor_window) && refproto_reference(&s.display,ref.window,s.keycode))) goto done;
    if (refproto_preflight(&s.display,&ref) || (editor_window && refproto_preflight(&s.display,&ed_target))) goto done;
    if (editor_window && !synthetic_clock && !refproto_period_matches(ref.period,ed_target.period)) {
        refproto_fail(&s.display,ed_target.window,"paired target refresh mismatch: reference=%" PRIu64 " ns editor=%" PRIu64
            " ns; place both on the same output and re-run",ref.period,ed_target.period); goto done;
    }
    if (expected_period && (!refproto_period_matches(ref.period,expected_period) ||
        (editor_window && !refproto_period_matches(ed_target.period,expected_period)))) {
        refproto_fail(&s.display,ref.window,"--period-ns assertion failed: expected=%" PRIu64 " ns measured=%" PRIu64 " ns",
            expected_period,ref.period); goto done;
    }
    uint64_t period = editor_window && ed_target.period < ref.period ? ed_target.period : ref.period;
    if (fixed_phase && phase >= period) { refproto_fail(&s.display,ref.window,"--phase-ns exceeds measured refresh period"); goto done; }
    if (dry_run) { fprintf(stderr,"keyinject: dry-run PASS (no injection, no CSV)\n"); rc = 0; goto done; }
    s.injection = refproto_atom(s.display.conn,"_EDIT_REF_INJECT"); s.complete = refproto_atom(s.display.conn,"_EDIT_REF_COMPLETE");
    s.notify = refproto_atom(s.display.conn,"_EDIT_REF_NOTIFY"); s.consumed = refproto_atom(s.display.conn,"_EDIT_REF_CONSUMED");
    if (!s.injection || !s.complete || !s.notify || !s.consumed) { refproto_fail(&s.display,ref.window,"intern reference protocol atoms failed"); goto done; }
    fprintf(stderr,"keyinject: %s; periods measured from target NotifyMSC streams\n",send_event ?
        "XSendEvent FALLBACK (explicit synthetic input, TRACK only)" : "XTest (dlopen libxcb-xtest.so.0)");
    csv = !strcmp(csv_path,"-") ? stdout : fopen(csv_path,"w");
    if (!csv) { refproto_fail(&s.display,ref.window,"open CSV %s: %s",csv_path,strerror(errno)); goto done; }
    if (fputs(REFPROTO_HEADER,csv) == EOF || fflush(csv)) { refproto_fail(&s.display,ref.window,"write/flush CSV header: %s",strerror(errno)); goto done; }
    for (uint64_t i = 0; i < pairs; i++) {
        uint64_t chosen = phase;
        if (!fixed_phase) { seed ^= seed<<13; seed ^= seed>>7; seed ^= seed<<17; chosen = seed%period; }
        uint32_t pair = (uint32_t)(first+i);
        if (editor_window && (i&1u) && keyinject_sample(&s,csv,&ed_target,"editor",pair,chosen,false)) goto sample_failed;
        if (keyinject_sample(&s,csv,&ref,target,pair,chosen,reference)) goto sample_failed;
        if (editor_window && !(i&1u) && keyinject_sample(&s,csv,&ed_target,"editor",pair,chosen,false)) goto sample_failed;
        continue;
    sample_failed:
        fprintf(stderr,"keyinject: stopped at pair=%" PRIu32 "; retain incomplete CSV %s\n",pair,csv_path); goto done;
    }
    rc = 0;
done:
    if (csv && fflush(csv)) { refproto_fail(&s.display,ref.window,"final CSV flush: %s",strerror(errno)); rc = 1; }
    if (csv && csv != stdout && fclose(csv)) { refproto_fail(&s.display,ref.window,"close CSV: %s",strerror(errno)); rc = 1; }
    refproto_display_close(&s.display); return rc;
}
