/* Tool-only wire/CSV contract. No application or frozen-header changes. */
#ifndef EDIT_REFWIN_PROTOCOL_H
#define EDIT_REFWIN_PROTOCOL_H
#include <errno.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdarg.h>
#include <time.h>
#include <xcb/present.h>
#include "trace/trace.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#define REFPROTO_WORDS 20u
#define REFPROTO_HEADER "pair_id,target,inject_ns,msc,t4_ns,t5_ns,t6_ns,frame_id,phase_ns,period_ns,actual_phase_ns\n"
typedef struct refproto_row {
    uint32_t pair, frame;
    uint64_t inject, msc, t4, t5, t6, phase, period, actual;
} refproto_row;

static inline int refproto_number(const char *s, uint64_t max, uint64_t *out)
{
    if (!s || *s < '0' || *s > '9') return -1;
    errno = 0; char *end = NULL;
    unsigned long long n = strtoull(s, &end, 0);
    if (errno || !end || *end || n > max) return -1;
    *out = (uint64_t)n; return 0;
}
static inline xcb_atom_t refproto_atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    if (!r) return XCB_ATOM_NONE;
    xcb_atom_t a = r->atom; free(r); return a;
}
static inline bool refproto_checked(xcb_connection_t *c, xcb_void_cookie_t ck)
{
    xcb_generic_error_t *e = xcb_request_check(c, ck);
    bool ok = !e && !xcb_connection_has_error(c); free(e); return ok;
}
static inline void refproto_pack(const refproto_row *r, uint32_t words[REFPROTO_WORDS])
{
    const uint64_t values[9] = {r->inject,r->msc,r->t4,r->t5,r->t6,r->phase,r->period,r->actual,0};
    words[0] = r->pair; words[1] = r->frame;
    for (size_t i = 0; i < 9; i++) {
        words[2+i*2] = (uint32_t)values[i]; words[3+i*2] = (uint32_t)(values[i] >> 32);
    }
}
static inline void refproto_unpack(const uint32_t words[REFPROTO_WORDS], refproto_row *r)
{
    uint64_t values[9];
    for (size_t i = 0; i < 9; i++) values[i] = words[2+i*2] | ((uint64_t)words[3+i*2] << 32);
    *r = (refproto_row){words[0],words[1],values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]};
}
static inline int refproto_get(xcb_connection_t *c, xcb_window_t w, xcb_atom_t a, refproto_row *row)
{
    xcb_get_property_reply_t *r = xcb_get_property_reply(c,
        xcb_get_property(c, 0, w, a, XCB_ATOM_CARDINAL, 0, REFPROTO_WORDS), NULL);
    if (!r) return -1;
    int rc = 1;
    if (r->type == XCB_ATOM_CARDINAL && r->format == 32 && !r->bytes_after &&
        xcb_get_property_value_length(r) == (int)(REFPROTO_WORDS * sizeof(uint32_t))) {
        uint32_t words[REFPROTO_WORDS]; memcpy(words, xcb_get_property_value(r), sizeof words);
        refproto_unpack(words, row); rc = 0;
    }
    free(r); return rc;
}
static inline int refproto_set(xcb_connection_t *c, xcb_window_t w, xcb_atom_t a, const refproto_row *row)
{
    uint32_t words[REFPROTO_WORDS]; refproto_pack(row, words);
    return refproto_checked(c, xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE,
        w, a, XCB_ATOM_CARDINAL, 32, REFPROTO_WORDS, words)) ? 0 : -1;
}
static inline int refproto_csv(FILE *f, const char *target, const refproto_row *r)
{
    return fprintf(f, "%" PRIu32 ",%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
        ",%" PRIu32 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", r->pair,target,r->inject,r->msc,
        r->t4,r->t5,r->t6,r->frame,r->phase,r->period,r->actual) < 0 || fflush(f) ? -1 : 0;
}
/* Setup/protocol IO only: never called inside the typing allocation guard. */
typedef xcb_void_cookie_t (*refproto_fake_fn)(xcb_connection_t *, uint8_t, uint8_t,
    uint32_t, xcb_window_t, int16_t, int16_t, uint8_t);
typedef struct refproto_clock { uint64_t ns, msc; } refproto_clock;
typedef struct refproto_target {
    xcb_window_t window;
    uint64_t period;
    refproto_clock last;
    int16_t x, y;
    uint16_t width, height;
    bool override_redirect;
} refproto_target;
typedef struct refproto_display {
    xcb_connection_t *conn;
    xcb_window_t root;
    const char *tool;
    uint8_t opcode;
    uint32_t serial;
    uint64_t timeout_ns;
    void *library;
    refproto_fake_fn fake;
    bool synthetic_clock;
    char power[32], load[32];
} refproto_display;

static inline int refproto_fail(refproto_display *d, xcb_window_t window, const char *format, ...)
{
    fprintf(stderr,"%s: window=0x%08" PRIx32 " ",d->tool,window);
    va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args);
    fputc('\n',stderr); return -1;
}
static inline int refproto_request(refproto_display *d, xcb_window_t window,
                                    xcb_void_cookie_t cookie, const char *stage)
{
    xcb_generic_error_t *e = xcb_request_check(d->conn,cookie);
    int rc = 0;
    if (e) rc = refproto_fail(d,window,"%s: X error=%u major=%u minor=%u resource=0x%08" PRIx32 " sequence=%u",
        stage,e->error_code,e->major_code,e->minor_code,e->resource_id,e->full_sequence);
    else if (xcb_connection_has_error(d->conn)) rc = refproto_fail(d,window,"%s: connection error=%d",
        stage,xcb_connection_has_error(d->conn));
    free(e); return rc;
}
static inline void refproto_display_close(refproto_display *d)
{
    if (d->conn) xcb_disconnect(d->conn);
    if (d->library) dlclose(d->library);
    d->conn = NULL; d->library = NULL;
}
static inline int refproto_display_open(refproto_display *d, const char *tool, bool require_xtest)
{
    *d = (refproto_display){.tool=tool,.serial=UINT32_C(0x80000000),.timeout_ns=UINT64_C(10000000000)};
    FILE *stamp = fopen("/sys/class/power_supply/BAT0/status","r");
    if (stamp) { if (!fgets(d->power,sizeof d->power,stamp)) d->power[0] = 0; fclose(stamp); }
    d->power[strcspn(d->power,"\r\n")] = 0;
    stamp = fopen("/proc/loadavg","r");
    if (stamp) { if (fscanf(stamp,"%31s",d->load) != 1) d->load[0] = 0; fclose(stamp); }
    int screen = 0;
    d->conn = xcb_connect(NULL,&screen);
    if (!d->conn || xcb_connection_has_error(d->conn))
        return refproto_fail(d,0,"connect DISPLAY=%s: connection error=%d",getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)",
            d->conn ? xcb_connection_has_error(d->conn) : -1);
    xcb_screen_iterator_t si = xcb_setup_roots_iterator(xcb_get_setup(d->conn));
    while (screen-- > 0 && si.rem) xcb_screen_next(&si);
    if (!si.rem) return refproto_fail(d,0,"selected X screen does not exist");
    d->root = si.data->root;
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(d->conn,&xcb_present_id);
    if (!ext || !ext->present) return refproto_fail(d,0,"Present extension unavailable");
    d->opcode = ext->major_opcode;
    xcb_generic_error_t *error = NULL;
    xcb_present_query_version_reply_t *version = xcb_present_query_version_reply(d->conn,
        xcb_present_query_version(d->conn,1,0),&error);
    if (!version || error) {
        int rc = refproto_fail(d,0,"Present QueryVersion 1.0 reply failed: X error=%u",error ? error->error_code : 0u);
        free(version); free(error); return rc;
    }
    uint32_t major = version->major_version, minor = version->minor_version;
    free(version);
    if (major != 1) return refproto_fail(d,0,"Present version incompatible: server=%" PRIu32 ".%" PRIu32,major,minor);
    xcb_query_extension_reply_t *xtest = xcb_query_extension_reply(d->conn,xcb_query_extension(d->conn,5,"XTEST"),NULL);
    bool available = xtest && xtest->present; free(xtest);
    if (available) {
        d->library = dlopen("libxcb-xtest.so.0",RTLD_NOW|RTLD_LOCAL);
        if (d->library) {
            void *symbol = dlsym(d->library,"xcb_test_fake_input_checked");
            _Static_assert(sizeof symbol == sizeof d->fake,"POSIX function pointer representation");
            memcpy(&d->fake,&symbol,sizeof d->fake);
        }
    }
    if (require_xtest && !available) return refproto_fail(d,0,"XTEST extension unavailable (no automatic XSendEvent fallback)");
    if (require_xtest && !d->library) return refproto_fail(d,0,"XTEST runtime dlopen libxcb-xtest.so.0: %s",dlerror());
    if (require_xtest && !d->fake) return refproto_fail(d,0,"XTEST runtime symbol xcb_test_fake_input_checked unavailable");
    return 0;
}
static inline int refproto_window(refproto_display *d, refproto_target *t, bool unchanged)
{
    xcb_get_window_attributes_reply_t *a = xcb_get_window_attributes_reply(d->conn,
        xcb_get_window_attributes(d->conn,t->window),NULL);
    if (!a) return refproto_fail(d,t->window,"GetWindowAttributes failed (window missing or inaccessible)");
    bool viewable = a->map_state == XCB_MAP_STATE_VIEWABLE;
    uint16_t window_class = a->_class;
    bool override = a->override_redirect != 0;
    uint8_t map = a->map_state; free(a);
    if (!viewable) return refproto_fail(d,t->window,"window not viewable: map_state=%u",map);
    if (window_class != XCB_WINDOW_CLASS_INPUT_OUTPUT) return refproto_fail(d,t->window,"window is not InputOutput: class=%u",window_class);
    xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(d->conn,xcb_get_geometry(d->conn,t->window),NULL);
    xcb_translate_coordinates_reply_t *xy = xcb_translate_coordinates_reply(d->conn,
        xcb_translate_coordinates(d->conn,t->window,d->root,0,0),NULL);
    if (!g || !xy || !xy->same_screen) {
        free(g); free(xy); return refproto_fail(d,t->window,"window geometry/root translation failed");
    }
    bool moved = unchanged && (t->x != xy->dst_x || t->y != xy->dst_y || t->width != g->width || t->height != g->height);
    t->x = xy->dst_x; t->y = xy->dst_y; t->width = g->width; t->height = g->height; t->override_redirect = override;
    free(g); free(xy);
    return moved ? refproto_fail(d,t->window,"window moved/resized since calibration; re-run preflight on its new output") : 0;
}
static inline int refproto_keycode(refproto_display *d, xcb_window_t window, uint8_t keycode)
{
    const xcb_setup_t *setup = xcb_get_setup(d->conn);
    if (keycode < setup->min_keycode || keycode > setup->max_keycode)
        return refproto_fail(d,window,"keycode outside server range %u..%u",setup->min_keycode,setup->max_keycode);
    xcb_get_keyboard_mapping_reply_t *map = xcb_get_keyboard_mapping_reply(d->conn,
        xcb_get_keyboard_mapping(d->conn,keycode,1),NULL);
    if (!map) return refproto_fail(d,window,"GetKeyboardMapping failed: keycode=%u",keycode);
    const xcb_keysym_t *syms = xcb_get_keyboard_mapping_keysyms(map);
    bool mapped = false;
    for (int i = 0; i < xcb_get_keyboard_mapping_keysyms_length(map); i++) if (syms[i]) mapped = true;
    free(map);
    if (!mapped) return refproto_fail(d,window,"keycode has no server mapping: keycode=%u",keycode);
    xcb_query_keymap_reply_t *keys = xcb_query_keymap_reply(d->conn,xcb_query_keymap(d->conn),NULL);
    if (!keys) return refproto_fail(d,window,"QueryKeymap failed");
    int down = -1;
    /* Held modifiers alter the editor's translated key; a held sample key can
     * suppress XTest's press entirely. Never release somebody else's keys. */
    for (unsigned i = 0; i < 256u; i++) {
        if ((uint8_t)keys->keys[i/8u] & (1u << (i%8u))) { down = (int)i; break; }
    }
    free(keys);
    return down < 0 ? 0 : refproto_fail(d,window,"keyboard is not idle: keycode=%d down (release keys before preflight)",down);
}
/* A successful startup Present publishes version/keycode before READY. This
 * prevents a dry run from approving an arbitrary window as a reference. */
static inline int refproto_reference(refproto_display *d, xcb_window_t window, uint8_t keycode)
{
    xcb_atom_t atom = refproto_atom(d->conn,"_EDIT_REF_READY");
    if (!atom) return refproto_fail(d,window,"intern reference readiness atom failed");
    xcb_get_property_reply_t *r = xcb_get_property_reply(d->conn,
        xcb_get_property(d->conn,0,window,atom,XCB_ATOM_CARDINAL,0,2),NULL);
    bool valid = r && r->type == XCB_ATOM_CARDINAL && r->format == 32 && !r->bytes_after &&
        xcb_get_property_value_length(r) == 8;
    uint32_t words[2] = {0};
    if (valid) memcpy(words,xcb_get_property_value(r),sizeof words);
    free(r);
    if (!valid) return refproto_fail(d,window,"reference readiness property missing/malformed (wait for refwin READY)");
    if (words[0] != 1 || words[1] != keycode) return refproto_fail(d,window,
        "reference readiness version/keycode mismatch: version=%" PRIu32 " reference_keycode=%" PRIu32 " injector_keycode=%u",
        words[0],words[1],keycode);
    return 0;
}
static inline int refproto_focus(refproto_display *d, xcb_window_t window, bool restore)
{
    xcb_get_input_focus_reply_t *old = xcb_get_input_focus_reply(d->conn,xcb_get_input_focus(d->conn),NULL);
    if (!old) return refproto_fail(d,window,"GetInputFocus before focus failed");
    int rc = refproto_request(d,window,xcb_set_input_focus_checked(d->conn,XCB_INPUT_FOCUS_PARENT,window,XCB_CURRENT_TIME),
        "SetInputFocus (target must be viewable)");
    xcb_get_input_focus_reply_t *now = xcb_get_input_focus_reply(d->conn,xcb_get_input_focus(d->conn),NULL);
    if (!rc && (!now || now->focus != window)) rc = refproto_fail(d,window,"focus verification failed: actual=0x%08" PRIx32,
        now ? now->focus : 0u);
    free(now);
    if (restore && refproto_request(d,window,xcb_set_input_focus_checked(d->conn,old->revert_to,old->focus,XCB_CURRENT_TIME),
        "restore previous focus")) rc = -1;
    free(old); return rc;
}
static inline int refproto_wait(refproto_display *d, xcb_window_t window, uint8_t kind,
                                 uint32_t serial, refproto_clock *clock, uint32_t *frame)
{
    uint64_t deadline = trace_now_ns()+d->timeout_ns;
    for (;;) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(d->conn))) {
            if (!event->response_type) {
                xcb_generic_error_t *e = (xcb_generic_error_t *)event;
                int rc = refproto_fail(d,window,"event wait: X error=%u major=%u minor=%u resource=0x%08" PRIx32,
                    e->error_code,e->major_code,e->minor_code,e->resource_id);
                free(event); return rc;
            }
            if ((event->response_type & 0x7fu) == XCB_GE_GENERIC) {
                xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)event;
                if (ge->extension == d->opcode && ge->event_type == XCB_PRESENT_COMPLETE_NOTIFY) {
                    xcb_present_complete_notify_event_t *p = (xcb_present_complete_notify_event_t *)event;
                    if (p->window == window && p->kind == kind && (!serial || serial == p->serial)) {
                        if (p->ust > UINT64_MAX/1000u) {
                            free(event); return refproto_fail(d,window,"Present UST overflows nanoseconds");
                        }
                        *clock = (refproto_clock){p->ust*1000u,p->msc};
                        if (frame) *frame = p->serial;
                        free(event); return 0;
                    }
                }
            }
            free(event);
        }
        if (xcb_connection_has_error(d->conn)) return refproto_fail(d,window,"Present event wait: connection error=%d",
            xcb_connection_has_error(d->conn));
        if (trace_now_ns() >= deadline) return refproto_fail(d,window,
            "Present event timeout: expected kind=%u serial=%" PRIu32 " (NotifyMSC=1, Pixmap=0)",kind,serial);
        struct pollfd fd = {xcb_get_file_descriptor(d->conn),POLLIN,0};
        if (poll(&fd,1,10) < 0 && errno != EINTR) return refproto_fail(d,window,"Present event poll: %s",strerror(errno));
    }
}
static inline int refproto_msc(refproto_display *d, xcb_window_t window, uint64_t target, refproto_clock *out)
{
    if (d->serial == UINT32_MAX) return refproto_fail(d,window,"NotifyMSC serial exhausted");
    uint32_t serial = ++d->serial;
    if (refproto_request(d,window,xcb_present_notify_msc_checked(d->conn,window,serial,target,0,0),"Present NotifyMSC")) return -1;
    return refproto_wait(d,window,XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC,serial,out,NULL);
}
static inline int refproto_clock_valid(refproto_display *d, xcb_window_t window, refproto_clock clock)
{
    uint64_t now = trace_now_ns();
    const char *reason = !clock.ns ? "UST is zero" : !clock.msc ? "MSC is zero" :
        clock.ns > now ? "UST is in the future (monotonic clock domain mismatch)" :
        now-clock.ns > UINT64_C(2000000000) ? "UST is stale (monotonic clock domain/delivery delay)" : NULL;
    if (reason) return refproto_fail(d,window,"Present %s: UST_ns=%" PRIu64 " MSC=%" PRIu64
        " monotonic_ns=%" PRIu64,reason,clock.ns,clock.msc,now);
    return 0;
}
/* Use MSC deltas, not notification arrival times. Missed fields remain valid. */
static inline int refproto_period(refproto_clock a, refproto_clock b, uint64_t *period)
{
    if (b.ns <= a.ns || b.msc <= a.msc) return -1;
    uint64_t delta = b.msc-a.msc;
    *period = (b.ns-a.ns)/delta;
    return *period < UINT64_C(1000000) || *period > UINT64_C(1000000000) ? -1 : 0;
}
static inline bool refproto_period_matches(uint64_t a, uint64_t b)
{
    if (!a || !b) return false;
    uint64_t small = a < b ? a : b, difference = a > b ? a-b : b-a;
    return difference <= small/100u; /* 1% setup tolerance (E), never a latency gate. */
}
static inline int refproto_preflight(refproto_display *d, refproto_target *t)
{
    if (refproto_window(d,t,false) || refproto_focus(d,t->window,true) ||
        refproto_request(d,t->window,xcb_present_select_input_checked(d->conn,xcb_generate_id(d->conn),t->window,
            XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY),"Present SelectInput")) return -1;
    refproto_clock first;
    if (refproto_msc(d,t->window,0,&first)) return -1;
    /* A newly mapped real window may initially report MSC/UST zero. Ask for
     * a future field before requiring a usable clock. Never inject on zero. */
    for (unsigned i = 0; (!first.ns || !first.msc) && i < 4u; i++) {
        if (first.msc == UINT64_MAX || refproto_msc(d,t->window,first.msc+1u,&first)) return -1;
    }
    if (refproto_clock_valid(d,t->window,first)) return -1;
    refproto_clock last = first;
    uint64_t period = 0, minimum = UINT64_MAX, maximum = 0;
    for (unsigned i = 0; i < 4u; i++) {
        refproto_clock next;
        if (last.msc > UINT64_MAX-4u) return refproto_fail(d,t->window,"calibration MSC overflow");
        if (refproto_msc(d,t->window,last.msc+4u,&next) || refproto_clock_valid(d,t->window,next)) return -1;
        uint64_t interval = 0;
        if (refproto_period(last,next,&interval)) return refproto_fail(d,t->window,
            "MSC stream did not advance at a usable rate: previous UST_ns=%" PRIu64 " MSC=%" PRIu64
            " next UST_ns=%" PRIu64 " MSC=%" PRIu64,last.ns,last.msc,next.ns,next.msc);
        if (!d->synthetic_clock && period && !refproto_period_matches(period,interval)) return refproto_fail(d,t->window,
            "MSC period unstable/output changed: previous=%" PRIu64 " ns next=%" PRIu64 " ns",period,interval);
        if (interval < minimum) minimum = interval;
        if (interval > maximum) maximum = interval;
        period = interval; last = next;
    }
    if (refproto_period(first,last,&t->period) || refproto_window(d,t,true)) return -1;
    t->last = last;
    fprintf(stderr,"%s: window=0x%08" PRIx32 " viewable focusable override_redirect=%u root_xy=%d,%d size=%u,%u; "
        "NotifyMSC UST_ns=%" PRIu64 " MSC=%" PRIu64 " period=%" PRIu64 " ns Hz=%.3f (M, target MSC stream); "
        "interval_min/max=%" PRIu64 "/%" PRIu64 " ns [%s] power=%s load1=%s; XTEST=%s%s; "
        "Xvfb MSC is synthetic, cannot prove real vblank/G2c\n",d->tool,t->window,t->override_redirect,
        t->x,t->y,t->width,t->height,last.ns,last.msc,t->period,1e9/(double)t->period,minimum,maximum,
        !strcmp(d->power,"Discharging") ? "bat" : (!strcmp(d->power,"Charging") || !strcmp(d->power,"Full") ||
        !strcmp(d->power,"Not charging")) ? "AC" : "unknown",d->power,d->load,d->fake ? "available" : "unavailable",
        d->synthetic_clock ? "; SYNTHETIC-CLOCK TRACK: rate stability checks disabled" : "");
    return 0;
}
#endif
