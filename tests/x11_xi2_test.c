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
    CHECK(x.ndev == 1 && x.dev[0].has[0] && x.dev[0].num[0] == 3 && x.dev[0].has[1] && x.dev[0].num[1] == 2, "classes recorded");
    CHECK(x.dev[0].inc[1] > 2.49 && x.dev[0].inc[1] < 2.51, "fraction increment");
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
        CHECK(z.ndev == 1 && z.dev[0].source == 9 && z.dev[0].has[0] && z.dev[0].num[0] == 3,
              "master numbering wins (order %d): num %u", order, z.ndev ? z.dev[0].num[0] : 0u);
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
      xi2 y = x; for (uint32_t i = 0; i < y.ndev; i++) y.dev[i].last_ok[0] = y.dev[i].last_ok[1] = false;
      CHECK(xi2_decode(&y, xb, n + 4, true, &r) && r.motion && r.x == 7 && r.y == 8 && r.buttons == 1 && (r.mods & 4), "xcb layout");
      for (size_t l = 0; l < n + 4; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, xb, l); xi2_decode(&y, c, l, true, &r); free(c); } }
    /* truncation at every length must not crash or over-read (ASan checks) */
    n = mk_motion(buf, 12, 0x0f, v3, 4, 5, 5);
    for (size_t l = 0; l < n; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, buf, l); xi2_decode(&x, c, l, false, &r); free(c); }
    n = mk_query(buf);
    for (size_t l = 0; l < n; l++) { uint8_t *c = malloc(l ? l : 1); memcpy(c, buf, l); xi2 y; memset(&y, 0, sizeof y); xi2_parse_query_device(&y, c, l); free(c); }
    if (g_fail) { puts("x11_xi2_test: FAILED"); return 1; }
    puts("x11_xi2_test: ok");
    return 0;
}
