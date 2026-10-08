/* xi2.c - XI2 wire format by hand (P2.2). Offsets are from the XInput 2 protocol spec (DeviceEvent). */
#include "xi2.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint16_t get16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int32_t geti32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

#define XI_QUERY_DEVICE 48
#define XI_SELECT_EVENTS 46
#define XI_QUERY_VERSION 47

size_t xi2_build_query_version(uint8_t opcode, uint8_t *out, uint16_t major, uint16_t minor) {
    memset(out, 0, 8);
    out[0] = opcode; out[1] = XI_QUERY_VERSION; put16(out + 2, 2);
    put16(out + 4, major); put16(out + 6, minor);
    return 8;
}

size_t xi2_build_query_device(uint8_t opcode, uint8_t *out, uint16_t deviceid) {
    memset(out, 0, 8);
    out[0] = opcode; out[1] = XI_QUERY_DEVICE; put16(out + 2, 2);
    put16(out + 4, deviceid);
    return 8;
}

size_t xi2_build_select_events(uint8_t opcode, uint8_t *out, uint32_t window) {
    memset(out, 0, 20);
    out[0] = opcode; out[1] = XI_SELECT_EVENTS; put16(out + 2, 5);
    put32(out + 4, window);
    put16(out + 8, 1);                 /* num_mask */
    put16(out + 12, 1);                /* deviceid XIAllMasterDevices */
    put16(out + 14, 1);                /* mask_len in 4-byte units */
    out[16] = (uint8_t)((1u << 1) | (1u << 6));   /* XI_DeviceChanged (1), XI_Motion (6) */
    return 20;
}

static xi2_dev *dev_for(xi2 *x, uint16_t source) {
    for (uint32_t i = 0; i < x->ndev; i++) if (x->dev[i].source == source) return &x->dev[i];
    if (x->ndev >= XI2_MAX_DEVS) return NULL;
    xi2_dev *d = &x->dev[x->ndev++];
    memset(d, 0, sizeof *d);
    d->source = source;
    return d;
}

int xi2_parse_query_device(xi2 *x, const uint8_t *r, size_t len) {
    if (len < 32 || r[0] != 1) return -1;
    size_t total = 32 + (size_t)get32(r + 4) * 4;
    if (total > len) return -1;
    len = total;
    uint16_t ninfo = get16(r + 8);
    size_t off = 32;
    int found = 0;
    x->ndev = 0;
    for (uint16_t i = 0; i < ninfo; i++) {
        if (off + 12 > len) return -1;
        uint16_t ncls = get16(r + off + 6), nlen = get16(r + off + 8);
        off += 12 + (((size_t)nlen + 3) & ~(size_t)3);
        for (uint16_t c = 0; c < ncls; c++) {
            if (off + 8 > len) return -1;
            uint16_t type = get16(r + off), clen = get16(r + off + 2);
            size_t bytes = (size_t)clen * 4;
            if (bytes < 8 || off + bytes > len) return -1;
            if (type == 3 && bytes >= 24) {
                uint16_t src = get16(r + off + 4), num = get16(r + off + 6), st = get16(r + off + 8);
                double inc = (double)geti32(r + off + 16) + (double)get32(r + off + 20) / 4294967296.0;
                if ((st == 1 || st == 2) && inc != 0.0) {
                    xi2_dev *d = dev_for(x, src);
                    if (d) {
                        int k = (st == 1) ? 0 : 1;
                        d->has[k] = true; d->num[k] = num; d->inc[k] = inc; d->last_ok[k] = false;
                        found++;
                    }
                }
            }
            off += bytes;
        }
    }
    return found;
}

bool xi2_has_scroll(const xi2 *x) { return x->ndev > 0; }

#define DEV_HDR 80u

/* Wire offset -> buffer offset. */
#define WO(off) ((off) < 32u ? (size_t)(off) : (size_t)(off) + sh)

bool xi2_decode(xi2 *x, const uint8_t *ev, size_t len, bool xcb_layout, xi2_result *r) {
    memset(r, 0, sizeof *r);
    size_t sh = xcb_layout ? 4 : 0;
    if (len < 12 || ev[0] != 35 || ev[1] != x->opcode) return false;   /* GenericEvent, our extension */
    uint16_t type = get16(ev + 8);
    if (type == 1) {                       /* XI_DeviceChanged */
        r->device_changed = true;
        for (uint32_t i = 0; i < x->ndev; i++) x->dev[i].last_ok[0] = x->dev[i].last_ok[1] = false;
        return true;
    }
    if (type != 6 || len < WO(DEV_HDR)) return false;
    uint32_t blen = get16(ev + WO(48)), vlen = get16(ev + WO(50));
    uint16_t source = get16(ev + WO(52));
    size_t moff = DEV_HDR + (size_t)blen * 4, voff = moff + (size_t)vlen * 4;   /* wire offsets */
    if (WO(voff) > len) return false;
    r->time_ms = get32(ev + 12);
    r->x = geti32(ev + WO(40)) >> 16;
    r->y = geti32(ev + WO(44)) >> 16;
    r->mods = (get32(ev + WO(72)) & 0xffu) | ((uint32_t)(ev[WO(79)] & 3u) << 13);
    if (blen) for (uint32_t b = 1; b <= 8 && b < blen * 32; b++)
        if (ev[WO(DEV_HDR + (b >> 3))] & (1u << (b & 7))) r->buttons |= 1u << (b - 1);
    r->mods |= r->buttons << 8;
    /* valuators: mask at moff, packed FP3232 values at voff in ascending valuator number */
    xi2_dev *d = NULL;
    for (uint32_t i = 0; i < x->ndev; i++) if (x->dev[i].source == source) d = &x->dev[i];
    size_t nbits = (size_t)vlen * 32, k = 0;
    for (size_t bit = 0; bit < nbits; bit++) {
        if (!(ev[WO(moff + (bit >> 3))] & (1u << (bit & 7)))) continue;
        size_t at = WO(voff + k * 8);
        k++;
        if (at + 8 > len) return false;
        if (bit < 2) { r->motion = true; continue; }       /* valuators 0/1: pointer X/Y */
        if (!d) continue;
        double v = (double)geti32(ev + at) + (double)get32(ev + at + 4) / 4294967296.0;
        for (int a = 0; a < 2; a++) {
            if (!d->has[a] || d->num[a] != bit) continue;
            if (d->last_ok[a]) {
                double u = (v - d->last[a]) / d->inc[a] * 256.0;
                if (u > 16777216.0) u = 16777216.0;
                if (u < -16777216.0) u = -16777216.0;
                int32_t iu = (int32_t)(u < 0 ? u - 0.5 : u + 0.5);
                if (iu) { if (a == 0) r->dy += iu; else r->dx += iu; r->wheel = true; }
            }
            d->last[a] = v; d->last_ok[a] = true;
        }
    }
    return true;
}
