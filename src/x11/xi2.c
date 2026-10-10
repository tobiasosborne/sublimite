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
    out[16] = (uint8_t)((1u << 1) | (1u << 4) | (1u << 5) | (1u << 6));   /* changed, buttons, motion */
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

#define QD_MAX_VALS 64                 /* ValuatorClass current values remembered while parsing one reply */

typedef struct axis_values {
    struct { uint16_t src, num; double value; } val[QD_MAX_VALS];
    uint32_t count;
} axis_values;

static bool parse_classes(xi2 *table, axis_values *values, const uint8_t *r,
                          size_t len, size_t *offset, uint16_t count, bool master) {
    size_t off = *offset;
    for (uint16_t c = 0; c < count; c++) {
        if (off > len || len - off < 8) return false;
        uint16_t type = get16(r + off), clen = get16(r + off + 2);
        size_t bytes = (size_t)clen * 4;
        if (bytes < 8 || bytes > len - off) return false;
        if (type == 2 && bytes >= 44 && master && values->count < QD_MAX_VALS) {
            uint32_t v = values->count++;
            values->val[v].src = get16(r + off + 4);
            values->val[v].num = get16(r + off + 6);
            values->val[v].value = (double)geti32(r + off + 28) + (double)get32(r + off + 32) / 4294967296.0;
        } else if (type == 3 && bytes >= 24 && master) {
            uint16_t source = get16(r + off + 4), number = get16(r + off + 6), direction = get16(r + off + 8);
            double inc = (double)geti32(r + off + 16) + (double)get32(r + off + 20) / 4294967296.0;
            if ((direction == 1 || direction == 2) && inc != 0) {
                xi2_dev *d = dev_for(table, source);
                if (d && d->naxes < XI2_MAX_AXES) {
                    xi2_axis *axis = &d->axis[d->naxes++];
                    axis->num = number; axis->dir = direction == 1 ? 0 : 1;
                    axis->inc = inc; axis->preferred = (get32(r + off + 12) & 2u) != 0;
                }
            }
        }
        off += bytes;
    }
    *offset = off;
    return true;
}

static void seed_axes(xi2 *table, const axis_values *values) {
    for (uint32_t i = 0; i < table->ndev; i++)
        for (uint32_t k = 0; k < table->dev[i].naxes; k++)
            for (uint32_t v = 0; v < values->count; v++)
                if (values->val[v].src == table->dev[i].source && values->val[v].num == table->dev[i].axis[k].num) {
                    table->dev[i].axis[k].last = values->val[v].value;
                    table->dev[i].axis[k].last_ok = true;
                }
}

/* All-or-nothing parse; setup snapshots have no live history to merge. */
int xi2_parse_query_device(xi2 *x, const uint8_t *r, size_t len) {
    if (len < 32 || r[0] != 1) return -1;
    size_t total = 32 + (size_t)get32(r + 4) * 4;
    if (total > len) return -1;
    len = total;
    xi2 table = { .opcode = x->opcode, .active = x->active };
    axis_values values = {0};
    size_t off = 32;
    uint16_t count = get16(r + 8);
    for (uint16_t i = 0; i < count; i++) {
        if (off > len || len - off < 12) return -1;
        bool master = get16(r + off + 2) == 1;
        uint16_t classes = get16(r + off + 6), name = get16(r + off + 8);
        off += 12 + (((size_t)name + 3) & ~(size_t)3);
        if (off > len || !parse_classes(&table, &values, r, len, &off, classes, master)) return -1;
    }
    seed_axes(&table, &values);
    int found = 0;
    for (uint32_t i = 0; i < table.ndev; i++) found += (int)table.dev[i].naxes;
    *x = table;
    return found;
}

int xi2_adopt_query_device(xi2 *x, const uint8_t *r, size_t len, uint64_t boundary) {
    xi2 table = { .opcode = x->opcode, .active = x->active };
    int found = xi2_parse_query_device(&table, r, len);
    if (found < 0) return found;
    table.sample_sequence = x->sample_sequence;
    for (uint32_t i = 0; i < table.ndev; i++)
        for (uint32_t k = 0; k < table.dev[i].naxes; k++)
            for (uint32_t j = 0; j < x->ndev; j++) {
                if (table.dev[i].source != x->dev[j].source) continue;
                for (uint32_t a = 0; a < x->dev[j].naxes; a++) {
                    xi2_axis *next = &table.dev[i].axis[k];
                    const xi2_axis *live = &x->dev[j].axis[a];
                    if (next->num == live->num && next->dir == live->dir && next->inc == live->inc &&
                        live->sample_sequence > boundary) {
                        next->last = live->last; next->last_ok = live->last_ok;
                        next->rem = live->rem; next->sample_sequence = live->sample_sequence;
                    }
                }
            }
    *x = table;
    return found;
}

bool xi2_has_scroll(const xi2 *x) { return x->ndev > 0; }

#define DEV_HDR 80u

/* Wire offset -> buffer offset. */
#define WO(off) ((off) < 32u ? (size_t)(off) : (size_t)(off) + sh)

bool xi2_decode(xi2 *x, const uint8_t *ev, size_t len, bool xcb_layout, xi2_result *r) {
    memset(r, 0, sizeof *r);
    size_t sh = xcb_layout ? 4 : 0;
    if (len < 32u + sh || ev[0] != 35 || ev[1] != x->opcode) return false;   /* GenericEvent, our extension */
    uint16_t type = get16(ev + 8);
    if (type == 1) {                       /* XI_DeviceChanged: an ordered class snapshot */
        uint16_t source = get16(ev + 18), count = get16(ev + 16);
        size_t bytes = (size_t)get32(ev + 4) * 4;
        if (bytes > len - (32u + sh)) return false;
        xi2 changed = { .opcode = x->opcode, .active = x->active };
        axis_values values = {0};
        size_t offset = 0;
        if (!parse_classes(&changed, &values, ev + 32 + sh, bytes, &offset, count, true)) return false;
        seed_axes(&changed, &values);
        xi2 table = *x;
        /* Only this source is invalidated when class values are unavailable. */
        for (uint32_t i = 0; i < table.ndev; i++)
            if (table.dev[i].source == source)
                for (uint32_t k = 0; k < table.dev[i].naxes; k++) {
                    table.dev[i].axis[k].last_ok = false; table.dev[i].axis[k].rem = 0;
                    table.dev[i].axis[k].sample_sequence = table.sample_sequence + 1;
                }
        table.sample_sequence++;
        for (uint32_t i = 0; i < changed.ndev; i++) {
            xi2_dev *dest = dev_for(&table, changed.dev[i].source);
            if (!dest) continue;
            *dest = changed.dev[i];
            for (uint32_t k = 0; k < dest->naxes; k++) dest->axis[k].sample_sequence = table.sample_sequence;
        }
        *x = table;
        r->device_changed = true; r->source = source; r->time_ms = get32(ev + 12);
        return true;
    }
    if ((type != 4 && type != 5 && type != 6) || len < WO(DEV_HDR)) return false;
    uint32_t blen = get16(ev + WO(48)), vlen = get16(ev + WO(50));
    uint16_t source = get16(ev + WO(52));
    size_t moff = DEV_HDR + (size_t)blen * 4, voff = moff + (size_t)vlen * 4;   /* wire offsets */
    if (WO(voff) > len) return false;
    r->source = source;
    r->button = type == 4 || type == 5;
    r->press = type == 4;
    r->detail = get32(ev + 16);
    r->pointer_emulated = (get32(ev + WO(56)) & (1u << 16)) != 0;
    if (r->button && (r->detail == 0 || r->detail > 32)) return false;
    r->time_ms = get32(ev + 12);
    r->x = geti32(ev + WO(40)) >> 16;
    r->y = geti32(ev + WO(44)) >> 16;
    r->mods = (get32(ev + WO(72)) & 0xffu) | ((uint32_t)(ev[WO(79)] & 3u) << 13);
    if (blen) for (uint32_t b = 1; b <= 32 && b < blen * 32; b++)
        if (ev[WO(DEV_HDR + (b >> 3))] & (1u << (b & 7))) r->buttons |= 1u << (b - 1);
    r->mods |= (r->buttons & 0x1fu) << 8;
    /* valuators: mask at moff, packed FP3232 values at voff in ascending valuator number */
    xi2_dev *d = NULL;
    for (uint32_t i = 0; i < x->ndev; i++) if (x->dev[i].source == source) d = &x->dev[i];
    size_t nbits = (size_t)vlen * 32, k = 0;
    double nv[XI2_MAX_AXES];           /* new value per axis of d, committed only after the whole event validated */
    bool seen[XI2_MAX_AXES] = { false };
    for (size_t bit = 0; bit < nbits; bit++) {
        if (!(ev[WO(moff + (bit >> 3))] & (1u << (bit & 7)))) continue;
        size_t at = WO(voff + k * 8);
        k++;
        if (at + 8 > len) { memset(r, 0, sizeof *r); return false; }
        if (bit < 2) { r->motion = !r->button; continue; }       /* valuators 0/1: pointer X/Y */
        if (!d) continue;
        double v = (double)geti32(ev + at) + (double)get32(ev + at + 4) / 4294967296.0;
        for (uint32_t a = 0; a < d->naxes; a++)
            if (d->axis[a].num == bit) { nv[a] = v; seen[a] = true; }
    }
    if (r->button) {
        /* Smooth-scroll emulation belongs only to that source. Independent
         * discrete input is never suppressed by another device's timestamp. */
        if (r->detail >= 4 && r->detail <= 7 && r->pointer_emulated && d && d->naxes)
            r->button = false;
        return true;
    }
    /* commit: per axis delta with the sub-unit remainder carried; per direction the preferred axis wins */
    x->sample_sequence++;
    int32_t delta[XI2_MAX_AXES] = { 0 };
    bool moved[XI2_MAX_AXES] = { false };
    for (uint32_t a = 0; d && a < d->naxes; a++) {
        if (!seen[a]) continue;
        xi2_axis *ax = &d->axis[a];
        if (ax->last_ok) {
            double u = (nv[a] - ax->last) / ax->inc * 256.0 + ax->rem;
            if (u > 16777216.0) u = 16777216.0;
            if (u < -16777216.0) u = -16777216.0;
            int32_t iu = (int32_t)(u < 0 ? u - 0.5 : u + 0.5);
            ax->rem = u - (double)iu;
            delta[a] = iu; moved[a] = true;
        }
        ax->last = nv[a]; ax->last_ok = true; ax->sample_sequence = x->sample_sequence;
    }
    for (int dir = 0; dir < 2; dir++) {
        int pick = -1;
        for (uint32_t a = 0; d && a < d->naxes; a++) {
            if (!moved[a] || d->axis[a].dir != dir) continue;
            if (d->axis[a].preferred) { pick = (int)a; break; }
            if (pick < 0) pick = (int)a;
        }
        if (pick >= 0 && delta[pick]) { if (dir == 0) r->dy += delta[pick]; else r->dx += delta[pick]; r->wheel = true; }
    }
    return true;
}
