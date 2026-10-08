/* input.c - key/button translation, time mapping, repeat, queue (P2.2). Pure: no X connection. */
#include "input.h"
#include <string.h>

#define NS_PER_MS UINT64_C(1000000)

uint64_t x11_clock_map(x11_clock *c, uint32_t x_ms, uint64_t now_ns) {
    if (!c->valid) {
        c->valid = true; c->ref_ms = x_ms; c->ref_ns = now_ns;
        return now_ns;
    }
    int32_t d = (int32_t)(x_ms - c->ref_ms);          /* wrap-safe signed ms distance */
    int64_t t = (int64_t)c->ref_ns + (int64_t)d * (int64_t)NS_PER_MS;
    /* The event cannot be in the future: if it is, our offset estimate was too loose (it carried the
     * first event's latency). Tighten it to this event, i.e. keep the minimum observed latency. */
    if (t < 0 || (uint64_t)t > now_ns) { c->ref_ms = x_ms; c->ref_ns = now_ns; return now_ns; }
    /* More than 5 s old: server clock jumped or we stalled; resync rather than trust it. */
    if (now_ns - (uint64_t)t > 5000 * NS_PER_MS) { c->ref_ms = x_ms; c->ref_ns = now_ns; return now_ns; }
    return (uint64_t)t;
}

static const char *const mod_names[7] = {
    XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CTRL, XKB_MOD_NAME_ALT, XKB_MOD_NAME_LOGO,
    XKB_MOD_NAME_CAPS, XKB_MOD_NAME_NUM, "Mod5"
};

static void cache_mods(x11_input *in) {
    for (int i = 0; i < 7; i++) in->mod_idx[i] = xkb_keymap_mod_get_index(in->keymap, mod_names[i]);
}

int x11_input_init(x11_input *in, struct xkb_keymap *keymap, struct xkb_compose_table *ctab) {
    memset(in, 0, sizeof *in);
    in->rep_delay_ms = 400; in->rep_rate_hz = 30;
    in->ctab = ctab;
    if (ctab) in->cstate = xkb_compose_state_new(ctab, XKB_COMPOSE_STATE_NO_FLAGS);
    if (x11_input_set_keymap(in, keymap) != 0) return -1;
    return 0;
}

int x11_input_set_keymap(x11_input *in, struct xkb_keymap *keymap) {
    struct xkb_state *st = keymap ? xkb_state_new(keymap) : NULL;
    if (!st) return -1;
    if (in->state) xkb_state_unref(in->state);
    if (in->keymap) xkb_keymap_unref(in->keymap);
    in->keymap = keymap; in->state = st;
    cache_mods(in);
    x11_repeat_cancel(in);
    if (in->cstate) xkb_compose_state_reset(in->cstate);
    return 0;
}

void x11_input_destroy(x11_input *in) {
    if (in->ctx) xkb_context_unref(in->ctx);
    if (in->cstate) xkb_compose_state_unref(in->cstate);
    if (in->ctab) xkb_compose_table_unref(in->ctab);
    if (in->state) xkb_state_unref(in->state);
    if (in->keymap) xkb_keymap_unref(in->keymap);
    memset(in, 0, sizeof *in);
}

/* Make in->state's effective modifiers/group equal those in a core event state word. */
static void sync_state(x11_input *in, uint32_t state) {
    xkb_state_update_mask(in->state, state & 0xffu, 0, 0, 0, 0, (state >> 13) & 3u);
}

static uint16_t mods_now(const x11_input *in) {
    uint16_t m = 0;
    for (int i = 0; i < 7; i++)
        if (in->mod_idx[i] != XKB_MOD_INVALID &&
            xkb_state_mod_index_is_active(in->state, in->mod_idx[i], XKB_STATE_MODS_EFFECTIVE) > 0)
            m = (uint16_t)(m | (1u << i));
    return m;
}

uint16_t x11_mods_from_state(x11_input *in, uint32_t state) {
    sync_state(in, state);
    return mods_now(in);
}

/* Keep only printable text: control characters (\r \t \x1b \x08 \x7f ...) are for keysym consumers. */
static void set_text(plat_event *out, const char *s, int n, uint16_t mods) {
    out->utf8_len = 0;
    if (n <= 0 || n > (int)sizeof out->utf8) return;
    if (mods & (PLAT_MOD_CTRL | PLAT_MOD_ALT | PLAT_MOD_SUPER)) return;
    if ((uint8_t)s[0] < 0x20 || (uint8_t)s[0] == 0x7f) return;
    memcpy(out->utf8, s, (size_t)n);
    out->utf8_len = (uint8_t)n;
}

void x11_translate(x11_input *in, uint32_t keycode, uint32_t state, plat_event *out) {
    char buf[16];
    sync_state(in, state);
    out->kind = PLAT_EV_KEY;
    out->code = keycode; out->state = state;
    out->mods = mods_now(in);
    out->keysym = (uint32_t)xkb_state_key_get_one_sym(in->state, keycode);
    int n = xkb_state_key_get_utf8(in->state, keycode, buf, sizeof buf);
    set_text(out, buf, n, out->mods);
}

static bool is_down(const x11_input *in, uint32_t k) { return (in->down[(k >> 3) & 31] >> (k & 7)) & 1; }
static void set_down(x11_input *in, uint32_t k, bool d) {
    uint8_t m = (uint8_t)(1u << (k & 7));
    if (d) in->down[(k >> 3) & 31] |= m; else in->down[(k >> 3) & 31] &= (uint8_t)~m;
}

bool x11_input_key(x11_input *in, const xcb_key_press_event_t *ev, bool press, uint64_t now_ns, plat_event *out) {
    uint32_t k = ev->detail;
    memset(out, 0, sizeof *out);
    if (press && is_down(in, k)) return false;     /* server autorepeat (detectable mode): ours replaces it */
    set_down(in, k, press);
    x11_translate(in, k, ev->state, out);
    out->press = press; out->time_ms = ev->time;
    out->x = ev->event_x; out->y = ev->event_y;
    out->buttons = (ev->state >> 8) & 0x1fu;
    out->t0_ns = x11_clock_map(&in->clock, ev->time, now_ns);
    if (!press) {
        if (in->rep_active && in->rep_key == k) x11_repeat_cancel(in);
        return true;
    }
    /* Compose / dead keys: feed the keysym; swallow while composing. */
    if (in->cstate && out->keysym != XKB_KEY_NoSymbol) {
        xkb_compose_state_feed(in->cstate, (xkb_keysym_t)out->keysym);
        switch (xkb_compose_state_get_status(in->cstate)) {
        case XKB_COMPOSE_COMPOSING:
            x11_repeat_cancel(in);
            return false;
        case XKB_COMPOSE_COMPOSED: {
            char b[16];
            int n = xkb_compose_state_get_utf8(in->cstate, b, sizeof b);
            xkb_keysym_t cs = xkb_compose_state_get_one_sym(in->cstate);
            xkb_compose_state_reset(in->cstate);
            if (cs != XKB_KEY_NoSymbol) out->keysym = (uint32_t)cs;
            set_text(out, b, n, out->mods);
            x11_repeat_cancel(in);
            return true;
        }
        case XKB_COMPOSE_CANCELLED:
            xkb_compose_state_reset(in->cstate);
            return false;                           /* the cancelling key is consumed (as Xlib/GTK do) */
        default: break;
        }
    }
    /* Arm repeat (modifiers and keys the keymap says don't repeat never do). */
    if (in->rep_rate_hz && xkb_keymap_key_repeats(in->keymap, k)) {
        in->rep_active = true; in->rep_key = k; in->rep_state = ev->state;
        in->rep_next_ns = now_ns + (uint64_t)in->rep_delay_ms * NS_PER_MS;
    } else {
        x11_repeat_cancel(in);
    }
    return true;
}

uint64_t x11_repeat_deadline(const x11_input *in) { return in->rep_active ? in->rep_next_ns : 0; }

void x11_repeat_cancel(x11_input *in) { in->rep_active = false; in->rep_next_ns = 0; }

bool x11_repeat_poll(x11_input *in, uint64_t now_ns, plat_event *out) {
    if (!in->rep_active || !in->rep_rate_hz || now_ns < in->rep_next_ns) return false;
    uint64_t iv = UINT64_C(1000000000) / in->rep_rate_hz;
    memset(out, 0, sizeof *out);
    x11_translate(in, in->rep_key, in->rep_state, out);
    out->press = true; out->repeat = true;
    out->t0_ns = now_ns;
    in->rep_next_ns += iv;
    if (in->rep_next_ns + 2 * iv <= now_ns) in->rep_next_ns = now_ns + iv;   /* >2 intervals behind (stall): skip, never burst */
    return true;
}

bool x11_input_button(x11_input *in, const xcb_button_press_event_t *ev, bool press, uint64_t now_ns,
                      bool core_wheel, plat_event *out) {
    memset(out, 0, sizeof *out);
    out->time_ms = ev->time; out->x = ev->event_x; out->y = ev->event_y;
    out->state = ev->state; out->mods = x11_mods_from_state(in, ev->state);
    out->buttons = (ev->state >> 8) & 0x1fu;
    out->t0_ns = x11_clock_map(&in->clock, ev->time, now_ns);
    uint32_t b = ev->detail;
    if (b >= 4 && b <= 7) {
        if (!press || !core_wheel) return false;   /* releases carry nothing; XI2 owns wheel if active */
        out->kind = PLAT_EV_WHEEL;
        if (b == 4) out->dy = -PLAT_WHEEL_UNIT;
        else if (b == 5) out->dy = PLAT_WHEEL_UNIT;
        else if (b == 6) out->dx = -PLAT_WHEEL_UNIT;
        else out->dx = PLAT_WHEEL_UNIT;
        return true;
    }
    out->kind = PLAT_EV_BUTTON; out->press = press; out->code = b;
    if (b >= 1 && b <= 5) {                        /* core state is pre-event; reflect the transition */
        if (press) out->buttons |= 1u << (b - 1); else out->buttons &= ~(1u << (b - 1));
    }
    return true;
}

bool x11_q_push(x11_input *in, const plat_event *ev) {
    if (in->qt - in->qh >= X11_QUEUE_CAP) { in->q_dropped++; return false; }
    in->q[in->qt & (X11_QUEUE_CAP - 1)] = *ev;
    in->qt++;
    return true;
}

bool x11_q_pop(x11_input *in, plat_event *out) {
    if (in->qh == in->qt) return false;
    *out = in->q[in->qh & (X11_QUEUE_CAP - 1)];
    in->qh++;
    return true;
}
