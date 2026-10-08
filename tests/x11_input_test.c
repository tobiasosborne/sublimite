/* x11_input_test.c - key table / repeat / time mapping / buttons, replayed without an X server (P2.2).
 * Keymaps come from xkbcommon's rules (evdev; us and de); events are synthesised xcb structs.
 * Keycodes are evdev+8. */
#include "x11/input.h"
#include "base/base.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail = 1; } } while (0)

enum { K_ESC = 9, K_1 = 10, K_2 = 11, K_7 = 16, K_MINUS = 20, K_EQUAL = 21, K_BKSP = 22, K_Q = 24, K_E = 26,
       K_Y = 29, K_RET = 36, K_LCTRL = 37, K_A = 38, K_LSHIFT = 50, K_SPACE = 65, K_KP1 = 87, K_X = 53 };
#define S_SHIFT 0x01u
#define S_CAPS  0x02u
#define S_CTRL  0x04u
#define S_ALT   0x08u
#define S_NUM   0x10u
#define S_SUPER 0x40u
#define S_ALTGR 0x80u

static struct xkb_keymap *mk(struct xkb_context *ctx, const char *layout) {
    struct xkb_rule_names rn = { "evdev", "pc105", layout, "", "" };
    return xkb_keymap_new_from_names(ctx, &rn, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

static xcb_key_press_event_t kev(uint8_t code, uint16_t state, uint32_t time) {
    xcb_key_press_event_t e;
    memset(&e, 0, sizeof e);
    e.response_type = XCB_KEY_PRESS; e.detail = code; e.state = state; e.time = time;
    e.event_x = 10; e.event_y = 20;
    return e;
}

typedef struct row {
    const char *name; uint8_t code; uint16_t state;
    uint32_t sym; const char *text; uint16_t mods;
} row;

static void run_table(x11_input *in, const row *t, size_t n, const char *layout) {
    for (size_t i = 0; i < n; i++) {
        plat_event e;
        xcb_key_press_event_t x = kev(t[i].code, t[i].state, 1000);
        bool ok = x11_input_key(in, &x, true, 5000000000ull, &e);
        CHECK(ok, "%s/%s: delivered", layout, t[i].name);
        CHECK(e.keysym == t[i].sym, "%s/%s: keysym 0x%x want 0x%x", layout, t[i].name, e.keysym, t[i].sym);
        CHECK(e.utf8_len == strlen(t[i].text) && memcmp(e.utf8, t[i].text, e.utf8_len) == 0,
              "%s/%s: utf8 len %u want '%s'", layout, t[i].name, e.utf8_len, t[i].text);
        CHECK((e.mods & 0x4f) == t[i].mods, "%s/%s: mods 0x%x want 0x%x", layout, t[i].name, e.mods, t[i].mods);
        CHECK(e.kind == PLAT_EV_KEY && e.press && !e.repeat && e.code == t[i].code, "%s/%s: shape", layout, t[i].name);
        xcb_key_press_event_t r = kev(t[i].code, t[i].state, 1001);
        x11_input_key(in, &r, false, 5000000001ull, &e);
        CHECK(!e.press, "%s/%s: release", layout, t[i].name);
    }
}

static void test_tables(struct xkb_context *ctx) {
    static const row us[] = {
        { "a", K_A, 0, XKB_KEY_a, "a", 0 },
        { "shift-a", K_A, S_SHIFT, XKB_KEY_A, "A", PLAT_MOD_SHIFT },
        { "caps-a", K_A, S_CAPS, XKB_KEY_A, "A", 0 },
        { "ctrl-a", K_A, S_CTRL, XKB_KEY_a, "", PLAT_MOD_CTRL },
        { "ctrl-shift-a", K_A, S_CTRL | S_SHIFT, XKB_KEY_A, "", PLAT_MOD_CTRL | PLAT_MOD_SHIFT },
        { "alt-a", K_A, S_ALT, XKB_KEY_a, "", PLAT_MOD_ALT },
        { "super-a", K_A, S_SUPER, XKB_KEY_a, "", PLAT_MOD_SUPER },
        { "shift-1", K_1, S_SHIFT, XKB_KEY_exclam, "!", PLAT_MOD_SHIFT },
        { "space", K_SPACE, 0, XKB_KEY_space, " ", 0 },
        { "return", K_RET, 0, XKB_KEY_Return, "", 0 },
        { "backspace", K_BKSP, 0, XKB_KEY_BackSpace, "", 0 },
        { "escape", K_ESC, 0, XKB_KEY_Escape, "", 0 },
        { "ctrl-escape", K_ESC, S_CTRL, XKB_KEY_Escape, "", PLAT_MOD_CTRL },
        { "lshift", K_LSHIFT, 0, XKB_KEY_Shift_L, "", 0 },
        { "kp1 numlock off", K_KP1, 0, XKB_KEY_KP_End, "", 0 },
        { "kp1 numlock on", K_KP1, S_NUM, XKB_KEY_KP_1, "1", 0 },
    };
    static const row de[] = {
        { "y key is z", K_Y, 0, XKB_KEY_z, "z", 0 },
        { "altgr-q is @", K_Q, S_ALTGR, XKB_KEY_at, "@", PLAT_MOD_ALTGR },
        { "shift-7 is /", K_7, S_SHIFT, XKB_KEY_slash, "/", PLAT_MOD_SHIFT },
        { "altgr-7 is {", K_7, S_ALTGR, XKB_KEY_braceleft, "{", PLAT_MOD_ALTGR },
        { "sz", K_MINUS, 0, XKB_KEY_ssharp, "\xc3\x9f", 0 },
        { "ctrl-z (y key)", K_Y, S_CTRL, XKB_KEY_z, "", PLAT_MOD_CTRL },
    };
    x11_input in;
    struct xkb_keymap *km = mk(ctx, "us");
    CHECK(km && x11_input_init(&in, km, NULL) == 0, "us keymap");
    if (!km) return;
    in.rep_rate_hz = 0;
    run_table(&in, us, sizeof us / sizeof us[0], "us");
    x11_input_destroy(&in);
    km = mk(ctx, "de");
    CHECK(km && x11_input_init(&in, km, NULL) == 0, "de keymap");
    if (!km) return;
    in.rep_rate_hz = 0;
    run_table(&in, de, sizeof de / sizeof de[0], "de");
    x11_input_destroy(&in);
}

static void test_dead_keys(struct xkb_context *ctx) {
    static const char compose[] =
        "<dead_acute> <e> : \"\xc3\xa9\" eacute\n"
        "<dead_acute> <space> : \"\xc2\xb4\" acute\n";
    struct xkb_compose_table *ct = xkb_compose_table_new_from_buffer(ctx, compose, sizeof compose - 1, "C",
        XKB_COMPOSE_FORMAT_TEXT_V1, XKB_COMPOSE_COMPILE_NO_FLAGS);
    CHECK(ct != NULL, "compose table");
    struct xkb_keymap *km = mk(ctx, "de");
    x11_input in;
    if (!ct || !km || x11_input_init(&in, km, ct) != 0) return;
    in.rep_rate_hz = 0;
    plat_event e;
    xcb_key_press_event_t d = kev(K_EQUAL, 0, 100), x = kev(K_E, 0, 101), sp = kev(K_SPACE, 0, 102), z = kev(K_X, 0, 103);
    CHECK(!x11_input_key(&in, &d, true, 1000000000ull, &e), "dead_acute swallowed");
    x11_input_key(&in, &d, false, 1000000001ull, &e);
    CHECK(x11_input_key(&in, &x, true, 1000000002ull, &e), "e composes");
    CHECK(e.keysym == XKB_KEY_eacute && e.utf8_len == 2 && memcmp(e.utf8, "\xc3\xa9", 2) == 0, "dead+e = e-acute (sym 0x%x)", e.keysym);
    x11_input_key(&in, &x, false, 1000000003ull, &e);
    x11_input_key(&in, &d, true, 1000000004ull, &e);
    x11_input_key(&in, &d, false, 1000000005ull, &e);
    CHECK(x11_input_key(&in, &sp, true, 1000000006ull, &e) && e.utf8_len == 2 && memcmp(e.utf8, "\xc2\xb4", 2) == 0, "dead+space = acute");
    x11_input_key(&in, &sp, false, 1000000007ull, &e);
    x11_input_key(&in, &d, true, 1000000008ull, &e);
    x11_input_key(&in, &d, false, 1000000009ull, &e);
    CHECK(!x11_input_key(&in, &z, true, 1000000010ull, &e), "dead+x cancelled, consumed");
    x11_input_key(&in, &z, false, 1000000011ull, &e);
    CHECK(x11_input_key(&in, &z, true, 1000000012ull, &e) && e.utf8_len == 1 && e.utf8[0] == 'x', "compose state reset after cancel");
    x11_input_destroy(&in);
}

static void test_repeat(struct xkb_context *ctx) {
    const uint64_t S = 1000000000ull, MS = 1000000ull;
    x11_input in;
    struct xkb_keymap *km = mk(ctx, "us");
    if (!km || x11_input_init(&in, km, NULL) != 0) { CHECK(0, "keymap"); return; }
    plat_event e;
    xcb_key_press_event_t a = kev(K_A, 0, 1000), q = kev(K_Q, 0, 1100), sh = kev(K_LSHIFT, 0, 1200);
    CHECK(x11_repeat_deadline(&in) == 0, "no deadline initially");
    CHECK(x11_input_key(&in, &a, true, 10 * S, &e), "press a");
    CHECK(x11_repeat_deadline(&in) == 10 * S + 400 * MS, "deadline = press + delay");
    CHECK(!x11_repeat_poll(&in, 10 * S + 399 * MS, &e), "no repeat before delay");
    CHECK(x11_repeat_poll(&in, 10 * S + 400 * MS, &e) && e.repeat && e.press && e.keysym == XKB_KEY_a
          && e.utf8_len == 1 && e.t0_ns == 10 * S + 400 * MS, "first repeat");
    CHECK(!x11_repeat_poll(&in, 10 * S + 400 * MS, &e), "one per deadline");
    int n = 1;
    while (x11_repeat_poll(&in, 10 * S + 500 * MS, &e)) n++;
    CHECK(n == 4, "30 Hz: 4 repeats by +500 ms, got %d", n);
    CHECK(!x11_input_key(&in, &a, true, 10 * S + 510 * MS, &e), "server autorepeat dup dropped");
    /* stalled UI: far behind yields one event, not a burst */
    n = 0;
    while (x11_repeat_poll(&in, 20 * S, &e)) n++;
    CHECK(n == 1, "stall gives 1 repeat, got %d", n);
    /* second key takes over */
    CHECK(x11_input_key(&in, &q, true, 21 * S, &e), "press q");
    x11_repeat_poll(&in, 21 * S + 400 * MS, &e);
    CHECK(e.keysym == XKB_KEY_q, "repeat follows newest key");
    /* releasing the old key does not cancel */
    x11_input_key(&in, &a, false, 21 * S + 410 * MS, &e);
    CHECK(x11_repeat_deadline(&in) != 0, "release of other key keeps repeat");
    x11_input_key(&in, &q, false, 21 * S + 420 * MS, &e);
    CHECK(x11_repeat_deadline(&in) == 0 && !x11_repeat_poll(&in, 30 * S, &e), "release cancels");
    /* modifiers never repeat */
    x11_input_key(&in, &sh, true, 30 * S, &e);
    CHECK(x11_repeat_deadline(&in) == 0, "shift does not repeat");
    x11_input_key(&in, &sh, false, 30 * S + MS, &e);
    /* configurable */
    in.rep_delay_ms = 250; in.rep_rate_hz = 10;
    x11_input_key(&in, &a, true, 40 * S, &e);
    x11_repeat_poll(&in, 40 * S + 250 * MS, &e);
    CHECK(x11_repeat_deadline(&in) == 40 * S + 350 * MS, "10 Hz interval, got %llu", (unsigned long long)x11_repeat_deadline(&in));
    in.rep_rate_hz = 0;
    x11_input_key(&in, &a, false, 41 * S, &e);
    x11_input_key(&in, &a, true, 42 * S, &e);
    CHECK(x11_repeat_deadline(&in) == 0, "rate 0 disables repeat");
    x11_input_destroy(&in);
}

static void test_clock(void) {
    const uint64_t S = 1000000000ull, MS = 1000000ull;
    x11_clock c;
    memset(&c, 0, sizeof c);
    CHECK(x11_clock_map(&c, 1000, 5 * S) == 5 * S, "first event = now");
    CHECK(x11_clock_map(&c, 1010, 5 * S + 12 * MS) == 5 * S + 10 * MS, "second keeps X spacing");
    /* event that would land in the future tightens the offset */
    CHECK(x11_clock_map(&c, 1100, 5 * S + 50 * MS) == 5 * S + 50 * MS, "future clamps to now");
    CHECK(x11_clock_map(&c, 1110, 5 * S + 62 * MS) == 5 * S + 60 * MS, "offset tightened");
    /* 32-bit wrap */
    memset(&c, 0, sizeof c);
    x11_clock_map(&c, 0xfffffff0u, 100 * S);
    CHECK(x11_clock_map(&c, 0x10u, 100 * S + 40 * MS) == 100 * S + 32 * MS, "wrap-safe");
    /* stale: server clock jumped back 10 s relative to us */
    CHECK(x11_clock_map(&c, 0x10u + 100u, 200 * S) == 200 * S, "stale resync");
    /* never later than now */
    memset(&c, 0, sizeof c);
    x11_clock_map(&c, 5, 3 * S);
    CHECK(x11_clock_map(&c, 5000, 3 * S + MS) <= 3 * S + MS, "never future");
}

static void test_buttons_queue(struct xkb_context *ctx) {
    x11_input in;
    struct xkb_keymap *km = mk(ctx, "us");
    if (!km || x11_input_init(&in, km, NULL) != 0) { CHECK(0, "keymap"); return; }
    plat_event e;
    xcb_button_press_event_t b;
    memset(&b, 0, sizeof b);
    b.detail = 5; b.event_x = 3; b.event_y = 4; b.time = 50;
    CHECK(x11_input_button(&in, &b, true, 1000000000ull, true, &e) && e.kind == PLAT_EV_WHEEL && e.dy == PLAT_WHEEL_UNIT && !e.smooth, "button 5");
    b.detail = 4;
    CHECK(x11_input_button(&in, &b, true, 1000000000ull, true, &e) && e.dy == -PLAT_WHEEL_UNIT, "button 4");
    b.detail = 6;
    CHECK(x11_input_button(&in, &b, true, 1000000000ull, true, &e) && e.dx == -PLAT_WHEEL_UNIT, "button 6");
    b.detail = 7;
    CHECK(x11_input_button(&in, &b, true, 1000000000ull, true, &e) && e.dx == PLAT_WHEEL_UNIT, "button 7");
    CHECK(!x11_input_button(&in, &b, false, 1000000000ull, true, &e), "wheel release dropped");
    CHECK(!x11_input_button(&in, &b, true, 1000000000ull, false, &e), "core wheel off (XI2 owns it)");
    b.detail = 1; b.state = S_CTRL;
    CHECK(x11_input_button(&in, &b, true, 1000000000ull, true, &e) && e.kind == PLAT_EV_BUTTON && e.press
          && e.code == 1 && e.buttons == 1 && (e.mods & PLAT_MOD_CTRL) && e.x == 3 && e.y == 4, "button 1 ctrl");
    /* queue */
    plat_event q;
    memset(&q, 0, sizeof q);
    for (uint32_t i = 0; i < X11_QUEUE_CAP + 5; i++) { q.code = i; x11_q_push(&in, &q); }
    CHECK(in.q_dropped == 5, "dropped %u", in.q_dropped);
    for (uint32_t i = 0; i < X11_QUEUE_CAP; i++) { CHECK(x11_q_pop(&in, &q) && q.code == i, "fifo %u", i); }
    CHECK(!x11_q_pop(&in, &q), "empty");
    x11_input_destroy(&in);
}

/* Law 2: translation, repeat, wheel and queueing must not malloc (counted only where the guard is real, i.e. not ASan). */
static void test_no_malloc(struct xkb_context *ctx) {
    static const char compose[] = "<dead_acute> <e> : \"\xc3\xa9\" eacute\n";
    struct xkb_compose_table *ct = xkb_compose_table_new_from_buffer(ctx, compose, sizeof compose - 1, "C",
        XKB_COMPOSE_FORMAT_TEXT_V1, XKB_COMPOSE_COMPILE_NO_FLAGS);
    x11_input in;
    struct xkb_keymap *km = mk(ctx, "de");
    if (!km || x11_input_init(&in, km, ct) != 0) { CHECK(0, "keymap"); return; }
    plat_event e;
    xcb_key_press_event_t a = kev(K_A, 0, 1000), d = kev(K_EQUAL, 0, 1000), sh = kev(K_A, S_SHIFT, 1000);
    xcb_button_press_event_t b;
    memset(&b, 0, sizeof b);
    b.detail = 5;
    edit_malloc_guard_begin();
    uint64_t now = 5000000000ull;
    for (int i = 0; i < 200; i++) {
        now += 1000000ull;
        x11_input_key(&in, &a, true, now, &e);
        while (x11_repeat_poll(&in, now + 700000000ull, &e)) x11_q_push(&in, &e);
        x11_input_key(&in, &a, false, now, &e);
        x11_input_key(&in, &sh, true, now, &e); x11_input_key(&in, &sh, false, now, &e);
        x11_input_key(&in, &d, true, now, &e); x11_input_key(&in, &d, false, now, &e);
        x11_input_button(&in, &b, true, now, true, &e);
        while (x11_q_pop(&in, &e)) {}
    }
    size_t n = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) CHECK(n == 0, "typing path made %zu allocations", n);
    else puts("x11_input_test: malloc guard inactive (ASan), allocation count not checked here");
    x11_input_destroy(&in);
}

int main(void) {
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) { fprintf(stderr, "no xkb context\n"); return 1; }
    test_tables(ctx);
    test_dead_keys(ctx);
    test_repeat(ctx);
    test_clock();
    test_buttons_queue(ctx);
    test_no_malloc(ctx);
    xkb_context_unref(ctx);
    if (g_fail) { puts("x11_input_test: FAILED"); return 1; }
    puts("x11_input_test: ok");
    return 0;
}
