/* libFuzzer: arbitrary bytes into the X-server-free input decoders (P2.2): core key/button events, XI2 generic
 * events (wire and xcb layout), QueryDevice replies, and a repeat/clock schedule. Must never crash or trip ASan/UBSan. */
#include "x11/input.h"
#include "x11/xi2.h"
#include "x11/clip.h"
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
        xcb_selection_notify_event_t n; memcpy(&n, data, sizeof n);
        x11_clip_notify_matches(&n, 9, 10, 11, 12, 13);
        n.response_type = XCB_SELECTION_NOTIFY;
        if (!x11_clip_notify_matches(&n, n.requestor, n.selection, n.target, n.property, n.time) ||
            x11_clip_notify_matches(&n, n.requestor ^ 1u, n.selection, n.target, n.property, n.time) ||
            x11_clip_notify_matches(&n, n.requestor, n.selection, n.target ^ 1u, n.property, n.time)) __builtin_trap();
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
    x11_clip_property v;
    xcb_atom_t pairs[128]; size_t npairs;
    if (x11_clip_decode_property(copy, size, &v)) {
        x11_clip_decode_pairs(&v, 103, pairs, 64, &npairs);
        x11_clip_decode_transfer(&v, 101, 102, 100, 0, true);
        x11_clip_decode_transfer(&v, 101, 102, 100, XCB_ATOM_STRING, false);
    }
    /* Also construct well-shaped replies so the fuzzer explores payload validation,
     * INCR lower bounds, type changes, partial direct-property reads and MULTIPLE lists. */
    size_t wire_size = 32 + ((size + 3u) & ~(size_t)3u);
    uint8_t *wire = calloc(1, wire_size);
    if (wire) {
        xcb_get_property_reply_t h; memset(&h, 0, sizeof h);
        h.response_type = 1; h.type = size ? 100u + data[0] % 4u : 100u;
        h.format = size && (data[0] & 4u) ? 32 : 8;
        h.value_len = (uint32_t)(h.format == 32 ? size / 4u : size);
        h.length = (uint32_t)((size + 3u) / 4u);
        if (size > 1 && (data[1] & 1u)) h.bytes_after = data[1];
        memcpy(wire, &h, 32); memcpy(wire + 32, data, size);
        if (x11_clip_decode_property(wire, wire_size, &v)) {
            x11_clip_decode_pairs(&v, 103, pairs, 64, &npairs);
            x11_clip_decode_transfer(&v, 101, 102, 100, 0, true);
            x11_clip_decode_transfer(&v, 101, 102, 100, 101, false);
        }
        free(wire);
    }
    if (size) {
        uint8_t *converted = malloc(size * 2u);
        if (converted) { x11_utf8_to_latin1(copy, size, converted); x11_latin1_to_utf8(copy, size, converted); free(converted); }
    }
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
