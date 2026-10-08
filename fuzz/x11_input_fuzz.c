/* libFuzzer: arbitrary bytes into the X-server-free input decoders (P2.2): core key/button events, XI2 generic
 * events (wire and xcb layout), QueryDevice replies, and a repeat/clock schedule. Must never crash or trip ASan/UBSan. */
#include "x11/input.h"
#include "x11/xi2.h"
#include <stdlib.h>
#include <string.h>

static x11_input g_in;
static int g_ready;

static void setup(void) {
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names rn = { "evdev", "pc105", "de", "", "" };
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &rn, XKB_KEYMAP_COMPILE_NO_FLAGS);
    static const char compose[] = "<dead_acute> <e> : \"\xc3\xa9\" eacute\n";
    struct xkb_compose_table *ct = xkb_compose_table_new_from_buffer(ctx, compose, sizeof compose - 1, "C",
        XKB_COMPOSE_FORMAT_TEXT_V1, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!km || x11_input_init(&g_in, km, ct) != 0) __builtin_trap();
    g_in.ctx = ctx;
    g_ready = 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!g_ready) setup();
    plat_event e;
    uint64_t now = 1000000000ull;
    if (size >= 32) {                                   /* 32 raw bytes as a core key event */
        xcb_key_press_event_t k;
        memcpy(&k, data, sizeof k);
        for (int i = 0; i < 2; i++) {
            now += 1000000ull * (data[1] % 50);
            x11_input_key(&g_in, &k, (data[0] & 1) != 0, now, &e);
            while (x11_repeat_poll(&g_in, now + 600000000ull, &e)) { now += 5000000ull; if (e.utf8_len > 8) __builtin_trap(); }
            x11_input_button(&g_in, (const xcb_button_press_event_t *)&k, (data[0] & 2) != 0, now, (data[0] & 4) != 0, &e);
        }
        if (g_in.rep_active == 0 && x11_repeat_deadline(&g_in) != 0) __builtin_trap();
    }
    xi2 x;
    memset(&x, 0, sizeof x);
    x.opcode = size ? data[0] : 0;
    xi2_parse_query_device(&x, data, size);             /* may populate a scroll table from fuzz bytes */
    xi2_result r;
    uint8_t *copy = malloc(size ? size : 1);            /* exact-size heap copy so ASan sees any over-read */
    if (!copy) return 0;
    memcpy(copy, data, size);
    xi2_decode(&x, copy, size, false, &r);
    xi2_decode(&x, copy, size, true, &r);
    free(copy);
    x11_clock c;
    memset(&c, 0, sizeof c);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t ms; memcpy(&ms, data + i, 4);
        now += 1000000ull;
        if (x11_clock_map(&c, ms, now) > now) __builtin_trap();
    }
    return 0;
}
