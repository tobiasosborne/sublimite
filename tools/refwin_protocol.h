/* Tool-only wire/CSV contract. No application or frozen-header changes. */
#ifndef EDIT_REFWIN_PROTOCOL_H
#define EDIT_REFWIN_PROTOCOL_H
#include <errno.h>
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
#endif
