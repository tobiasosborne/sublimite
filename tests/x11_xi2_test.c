/* x11_xi2_test.c - XI2 request bytes, QueryDevice parsing and smooth-scroll decoding from synthesised wire
 * bytes (no X server), plus malformed-input resilience (P2.2). */
#include "x11/xi2.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail = 1; } } while (0)

static void p16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void p32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* QueryDevice reply: one master pointer (id 12) with 3 classes: button, scroll V (valuator 3, inc 1.0),
 * scroll H (valuator 2, inc 2.5). */
static size_t mk_query(uint8_t *b) {
    memset(b, 0, 512);
    b[0] = 1;
    p16(b + 8, 1);
    size_t o = 32;
    p16(b + o, 12); p16(b + o + 2, 1); p16(b + o + 6, 3); p16(b + o + 8, 4);   /* id, type master ptr, 3 classes, name_len 4 */
    memcpy(b + o + 12, "mous", 4);
    o += 16;
    p16(b + o, 1); p16(b + o + 2, 2); p16(b + o + 4, 12);                  /* button class, len 2 (8 bytes) */
    o += 8;
    p16(b + o, 3); p16(b + o + 2, 6); p16(b + o + 4, 12); p16(b + o + 6, 3); p16(b + o + 8, 1);
    p32(b + o + 16, 1); p32(b + o + 20, 0);                                /* inc 1.0 */
    o += 24;
    p16(b + o, 3); p16(b + o + 2, 6); p16(b + o + 4, 12); p16(b + o + 6, 2); p16(b + o + 8, 2);
    p32(b + o + 16, 2); p32(b + o + 20, 0x80000000u);                      /* inc 2.5 */
    o += 24;
    p32(b + 4, (uint32_t)((o - 32) / 4));
    return o;
}


/* Review MINOR #4: master (type 1) and slave (type 3) infos for the same source with different valuator numbers.
 * Events come from masters, so the master's numbering must win regardless of order. */
static size_t mk_master_slave(uint8_t *b, bool master_first) {
    memset(b, 0, 512);
    b[0] = 1;
    p16(b + 8, 2);
    size_t o = 32;
    for (int i = 0; i < 2; i++) {
        bool master = (i == 0) == master_first;
        p16(b + o, master ? 2 : 9); p16(b + o + 2, master ? 1 : 3); p16(b + o + 6, 1); p16(b + o + 8, 4);
        memcpy(b + o + 12, "dev0", 4);
        o += 16;
        p16(b + o, 3); p16(b + o + 2, 6); p16(b + o + 4, 9); p16(b + o + 6, master ? 3 : 5); p16(b + o + 8, 1);
        p32(b + o + 16, 1); p32(b + o + 20, 0);
        o += 24;
    }
    p32(b + 4, (uint32_t)((o - 32) / 4));
    return o;
}

/* XI_Motion with valuators in mask (bits 0..3), values vals[] for set bits. */
static size_t mk_motion(uint8_t *b, uint16_t src, uint8_t mask, const double *vals, int nv, int px, int py) {
    memset(b, 0, 256);
    b[0] = 35; b[1] = 131;
    p16(b + 8, 6); p16(b + 10, 2); p32(b + 12, 777);
    p32(b + 40, (uint32_t)(px << 16)); p32(b + 44, (uint32_t)(py << 16));
    p16(b + 48, 1); p16(b + 50, 1); p16(b + 52, src);
    p32(b + 72, 4);                                   /* effective mods: control */
    b[79] = 0;
    b[80] = 0x02;                                     /* button 1 held */
    size_t o = 80 + 4;
    b[o] = mask; o += 4;
    for (int i = 0; i < nv; i++) {
        int32_t ip = (int32_t)floor(vals[i]);
        double fr = vals[i] - (double)ip;
        p32(b + o, (uint32_t)ip); p32(b + o + 4, (uint32_t)(fr * 4294967296.0));
        o += 8;
    }
    p32(b + 4, (uint32_t)((o - 32) / 4));
    return o;
}


/* ---- edit-e6x.20 (review x11-1 #16 #17 #20 #27) ---- */
typedef struct cls { int kind; uint16_t num; int vert; uint32_t flags; double val; double inc; } cls;   /* kind 2 = valuator, 3 = scroll */

static size_t mk_query_cls(uint8_t *b, const cls *c, int n, int name_len, int truncate_last) {
    memset(b, 0, 512);
    b[0] = 1;
    p16(b + 8, 1);
    size_t o = 32;
    p16(b + o, 12); p16(b + o + 2, 1); p16(b + o + 6, (uint16_t)n); p16(b + o + 8, (uint16_t)name_len);
    o += 12 + (((size_t)name_len + 3) & ~(size_t)3);
    for (int i = 0; i < n; i++) {
        if (c[i].kind == 2) {
            p16(b + o, 2); p16(b + o + 2, 11); p16(b + o + 4, 12); p16(b + o + 6, c[i].num);
            int32_t ip = (int32_t)floor(c[i].val);
            p32(b + o + 28, (uint32_t)ip); p32(b + o + 32, (uint32_t)((c[i].val - (double)ip) * 4294967296.0));
            o += 44;
        } else {
            double inc = c[i].inc;
            p16(b + o, 3); p16(b + o + 2, 6); p16(b + o + 4, 12); p16(b + o + 6, c[i].num);
            p16(b + o + 8, c[i].vert ? 1 : 2); p32(b + o + 12, c[i].flags);
            p32(b + o + 16, (uint32_t)(int32_t)floor(inc));
            p32(b + o + 20, (uint32_t)((inc - floor(inc)) * 4294967296.0));
            o += 24;
        }
    }
    (void)truncate_last;
    p32(b + 4, (uint32_t)((o - 32) / 4));
    return o;
}

static int sum_dy(xi2 *x, uint16_t src, uint8_t mask, const double *v, int nv, int *dxo) {
    uint8_t b[256]; xi2_result r;
    size_t n = mk_motion(b, src, mask, v, nv, 0, 0);
    if (!xi2_decode(x, b, n, false, &r)) return -99999;
    if (dxo) *dxo = r.dx;
    return r.dy;
}

static void test_e6x20(void) {
    uint8_t buf[512];
    xi2 x; xi2_result r;
    /* #16: first fractional scroll after QueryDevice is delivered (valuator class seeds history). */
    for (int order = 0; order < 2; order++) {
        cls c[2] = { { 2, 3, 0, 0, 10.0, 0 }, { 3, 3, 1, 0, 0, 1.0 } };
        if (order) { cls t = c[0]; c[0] = c[1]; c[1] = t; }
        memset(&x, 0, sizeof x); x.opcode = 131;
        size_t n = mk_query_cls(buf, c, 2, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 1, "#16 parse (order %d)", order);
        double v = 10.25;
        int dy = sum_dy(&x, 12, 0x08, &v, 1, NULL);
        CHECK(dy == 64, "#16 first scroll after query: dy %d want 64 (order %d)", dy, order);
        /* device changed + rescan seeds again */
        memset(buf, 0, 32); buf[0] = 35; buf[1] = 131; p16(buf + 8, 1);
        CHECK(xi2_decode(&x, buf, 32, false, &r) && r.device_changed, "#16 devchg");
        c[0].val = c[0].kind == 2 ? 20.0 : 0; if (c[1].kind == 2) c[1].val = 20.0;
        n = mk_query_cls(buf, c, 2, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 1, "#16 reparse");
        v = 20.5;
        dy = sum_dy(&x, 12, 0x08, &v, 1, NULL);
        CHECK(dy == 128, "#16 first scroll after rescan: dy %d want 128", dy);
    }
    /* #17: remainder survives: 1024 x (1/1024) = one full notch (256 units). */
    {
        cls c[2] = { { 2, 3, 0, 0, 0.0, 0 }, { 3, 3, 1, 0, 0, 1.0 } };
        memset(&x, 0, sizeof x); x.opcode = 131;
        size_t n = mk_query_cls(buf, c, 2, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 1, "#17 parse");
        long total = 0;
        for (int i = 1; i <= 1024; i++) { double v = (double)i / 1024.0; int d = sum_dy(&x, 12, 0x08, &v, 1, NULL); total += d; }
        CHECK(total == 256, "#17 cumulative total %ld want 256", total);
        /* negative, then reversal: net movement back to 0 must conserve */
        for (int i = 1023; i >= 0; i--) { double v = (double)i / 1024.0; total += sum_dy(&x, 12, 0x08, &v, 1, NULL); }
        CHECK(total == 0, "#17 after reversal total %ld want 0", total);
        /* 3 steps of 0.4 notch: 102.4 units each: 102, 102, 103 (remainder carried) */
        double v = 0.4; long t2 = sum_dy(&x, 12, 0x08, &v, 1, NULL);
        v = 0.8; t2 += sum_dy(&x, 12, 0x08, &v, 1, NULL);
        v = 1.2; t2 += sum_dy(&x, 12, 0x08, &v, 1, NULL);
        CHECK(t2 == 307, "#17 3x0.4 notch total %ld want 307", t2);
    }
    /* #20a: QueryDevice with name_len=4 but the name bytes missing (44-byte reply, zero classes). */
    {
        memset(&x, 0, sizeof x);
        memset(buf, 0, 64); buf[0] = 1; p16(buf + 8, 1); p32(buf + 4, 3);
        p16(buf + 32, 12); p16(buf + 34, 1); p16(buf + 40, 4);
        CHECK(xi2_parse_query_device(&x, buf, 44) == -1, "#20 missing name rejected");
    }
    /* #20b: valid scroll class then truncated class: -1 and the previous table is intact. */
    {
        cls c[2] = { { 2, 3, 0, 0, 0.0, 0 }, { 3, 3, 1, 0, 0, 1.0 } };
        memset(&x, 0, sizeof x); x.opcode = 131;
        size_t n = mk_query_cls(buf, c, 2, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 1, "#20 seed table");
        xi2 before = x;
        cls d[3] = { { 3, 7, 1, 0, 0, 2.0 }, { 3, 8, 0, 0, 0, 2.0 }, { 3, 9, 0, 0, 0, 2.0 } };
        n = mk_query_cls(buf, d, 3, 4, 0);
        p16(buf + 32 + 16 + 24 + 2, 200);                    /* second class claims 800 bytes: runs past the reply */
        CHECK(xi2_parse_query_device(&x, buf, n) == -1, "#20 truncated class rejected");
        CHECK(memcmp(&x, &before, sizeof x) == 0, "#20 table unchanged after rejected QueryDevice");
    }
    /* #20c: motion with one complete scroll value then a missing one: false and history untouched. */
    {
        cls c[3] = { { 2, 2, 0, 0, 0.0, 0 }, { 3, 3, 1, 0, 0, 1.0 }, { 3, 2, 0, 0, 0, 1.0 } };
        memset(&x, 0, sizeof x); x.opcode = 131;
        size_t n = mk_query_cls(buf, c, 3, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 2, "#20 parse 2 axes");
        double two[2] = { 0.5, 0.5 };
        uint8_t b[256];
        n = mk_motion(b, 12, 0x0c, two, 2, 0, 0);
        xi2 before = x;
        CHECK(!xi2_decode(&x, b, n - 8, false, &r), "#20 missing value rejected");
        CHECK(memcmp(&x, &before, sizeof x) == 0, "#20 history unchanged after rejected motion");
        CHECK(r.dx == 0 && r.dy == 0 && !r.wheel, "#20 rejected motion reports nothing");
    }
    /* #27: two vertical axes; events carrying only one of them still scroll; preferred wins when both move. */
    for (int perm = 0; perm < 2; perm++) {
        cls c[3] = { { 2, 3, 0, 0, 0.0, 0 }, { 2, 4, 0, 0, 0.0, 0 }, { 3, 3, 1, 2u, 0, 1.0 } };
        cls e = { 3, 4, 1, 0u, 0, 1.0 };
        cls all[4];
        if (perm == 0) { all[0] = c[0]; all[1] = c[1]; all[2] = c[2]; all[3] = e; }
        else { all[0] = e; all[1] = c[2]; all[2] = c[1]; all[3] = c[0]; }
        memset(&x, 0, sizeof x); x.opcode = 131;
        size_t n = mk_query_cls(buf, all, 4, 4, 0);
        CHECK(xi2_parse_query_device(&x, buf, n) == 2, "#27 parse two vertical axes (perm %d)", perm);
        double v = 0.5;
        CHECK(sum_dy(&x, 12, 0x08, &v, 1, NULL) == 128, "#27 first axis alone (perm %d)", perm);
        CHECK(sum_dy(&x, 12, 0x10, &v, 1, NULL) == 128, "#27 second axis alone (perm %d)", perm);
        double both[2] = { 1.0, 1.0 };                          /* each moved +0.5 notch: counted once (preferred) */
        int dy = sum_dy(&x, 12, 0x18, both, 2, NULL);
        CHECK(dy == 128, "#27 both axes in one event: dy %d want 128 (perm %d)", dy, perm);
    }
}

int main(void) {
    uint8_t buf[512], req[20];
    xi2 x;
    memset(&x, 0, sizeof x);
    x.opcode = 131;
    /* request bytes */
    CHECK(xi2_build_query_version(131, req, 2, 1) == 8 && req[0] == 131 && req[1] == 47 && req[2] == 2 && req[4] == 2 && req[6] == 1, "QueryVersion");
    CHECK(xi2_build_query_device(131, req, 0) == 8 && req[1] == 48, "QueryDevice");
    CHECK(xi2_build_select_events(131, req, 0x1234) == 20 && req[1] == 46 && req[2] == 5 && req[16] == 0x42, "SelectEvents");
    /* parse */
    size_t n = mk_query(buf);
    CHECK(xi2_parse_query_device(&x, buf, n) == 2, "two scroll classes");
    CHECK(x.ndev == 1 && x.dev[0].naxes == 2 && x.dev[0].axis[0].num == 3 && x.dev[0].axis[0].dir == 0 && x.dev[0].axis[1].num == 2 && x.dev[0].axis[1].dir == 1, "classes recorded");
    CHECK(x.dev[0].axis[1].inc > 2.49 && x.dev[0].axis[1].inc < 2.51, "fraction increment");
    CHECK(xi2_has_scroll(&x), "has scroll");
    /* decode: first event primes, second yields deltas (vertical +2 notches => +512; horizontal -2.5 => -256) */
    xi2_result r;
    double v1[3] = { 100, 5.0, 10.0 };       /* valuators 0/1 absent here: mask has bits 2,3 only */
    n = mk_motion(buf, 12, 0x0c, v1 + 1, 2, 50, 60);
    CHECK(xi2_decode(&x, buf, n, false, &r) && !r.wheel && r.time_ms == 777 && r.x == 50 && r.y == 60, "prime");
    CHECK(r.buttons == 1 && (r.mods & 4) && (r.mods >> 8) == 1, "buttons/mods");
    CHECK(!r.motion, "no x/y valuators => not a motion");
    double v2[2] = { 2.5, 12.0 };      /* valuator 2 (H) 5.0 -> 2.5 = -1 notch; valuator 3 (V) 10 -> 12 = +2 notches */
    n = mk_motion(buf, 12, 0x0c, v2, 2, 51, 61);
    CHECK(xi2_decode(&x, buf, n, false, &r) && r.wheel, "wheel decoded");
    CHECK(r.dy == 512, "dy %d want 512", r.dy);
    CHECK(r.dx == -256, "dx %d want -256", r.dx);
    /* with x/y valuators present */
    double v3[4] = { 1.0, 2.0, -2.5, 12.0 };
    n = mk_motion(buf, 12, 0x0f, v3, 4, 52, 62);
    CHECK(xi2_decode(&x, buf, n, false, &r) && r.motion && r.wheel && r.dx == -512, "motion+wheel dx %d", r.dx);
    /* unknown source device: motion only */
    n = mk_motion(buf, 99, 0x0f, v3, 4, 1, 2);
    CHECK(xi2_decode(&x, buf, n, false, &r) && r.motion && !r.wheel, "unknown source");
    /* device changed resets history */
    memset(buf, 0, 32); buf[0] = 35; buf[1] = 131; p16(buf + 8, 1);
    CHECK(xi2_decode(&x, buf, 32, false, &r) && r.device_changed, "device changed");
    n = mk_motion(buf, 12, 0x0c, v2, 2, 51, 61);
    CHECK(xi2_decode(&x, buf, n, false, &r) && !r.wheel, "history reset: first sample after change gives no delta");
    for (int order = 0; order < 2; order++) {
        xi2 z;
        memset(&z, 0, sizeof z);
        n = mk_master_slave(buf, order == 0);
        CHECK(xi2_parse_query_device(&z, buf, n) == 1, "only the master's class counts (order %d)", order);
        CHECK(z.ndev == 1 && z.dev[0].source == 9 && z.dev[0].naxes == 1 && z.dev[0].axis[0].num == 3,
              "master numbering wins (order %d): num %u", order, z.ndev ? z.dev[0].axis[0].num : 0u);
    }
    /* DeviceChanged carries its server time */
    memset(buf, 0, 32); buf[0] = 35; buf[1] = 131; p16(buf + 8, 1); p32(buf + 12, 4242);
    CHECK(xi2_decode(&x, buf, 32, false, &r) && r.device_changed && r.time_ms == 4242, "device changed time %u", r.time_ms);
    /* MINOR #3 time parsing must also reject every truncated DeviceChanged header. */
    for (size_t l = 0; l < 32; l++) {
        uint8_t *short_event = malloc(l ? l : 1); memcpy(short_event, buf, l);
        CHECK(!xi2_decode(&x, short_event, l, false, &r), "truncated DeviceChanged %zu rejected", l);
        free(short_event);
    }
    { uint8_t xb[36] = {0}; memcpy(xb, buf, 32);
      for (size_t l = 0; l < sizeof xb; l++) {
          uint8_t *short_event = malloc(l ? l : 1); memcpy(short_event, xb, l);
          CHECK(!xi2_decode(&x, short_event, l, true, &r), "truncated xcb DeviceChanged %zu rejected", l);
          free(short_event);
      } }
    /* wrong extension / not generic */
    n = mk_motion(buf, 12, 0x0c, v2, 2, 0, 0);
    buf[1] = 7;
    CHECK(!xi2_decode(&x, buf, n, false, &r), "other extension ignored");
    /* xcb layout: same event with the 4-byte full_sequence inserted after byte 32 */
    n = mk_motion(buf, 12, 0x0f, v3, 4, 7, 8);
    { uint8_t xb[512]; memcpy(xb, buf, 32); memset(xb + 32, 0xee, 4); memcpy(xb + 36, buf + 32, n - 32);
      xi2 y = x; for (uint32_t i = 0; i < y.ndev; i++) for (uint32_t q = 0; q < y.dev[i].naxes; q++) y.dev[i].axis[q].last_ok = false;
      CHECK(xi2_decode(&y, xb, n + 4, true, &r) && r.motion && r.x == 7 && r.y == 8 && r.buttons == 1 && (r.mods & 4), "xcb layout");
      for (size_t l = 0; l < n + 4; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, xb, l); xi2_decode(&y, c, l, true, &r); free(c); } }
    /* truncation at every length must not crash or over-read (ASan checks) */
    n = mk_motion(buf, 12, 0x0f, v3, 4, 5, 5);
    for (size_t l = 0; l < n; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, buf, l); xi2_decode(&x, c, l, false, &r); free(c); }
    n = mk_query(buf);
    for (size_t l = 0; l < n; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, buf, l); xi2 y; memset(&y, 0, sizeof y); xi2_parse_query_device(&y, c, l); free(c); }
    test_e6x20();
    if (g_fail) { puts("x11_xi2_test: FAILED"); return 1; }
    puts("x11_xi2_test: ok");
    return 0;
}
