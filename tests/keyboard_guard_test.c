/* edit-cuh: server-free rebuild probes. (P) Each probe allocates sizeof(char)
 * once; expected counts are the observed own-code probe calls plus the final
 * balance probe. (P) Zero denotes empty state/false or the first array index;
 * increments count individual calls, and enum values identify distinct cases.
 * No timing budget or synthetic keyboard count is needed. */
#include "base/base.h"
#include "x11/input.h"
#include "work/work.h"
#include <xkbcommon/xkbcommon-x11.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned exempt_depth;
static size_t own_calls;
static bool scope_failed, probe_failed;
static struct xkb_context *fixture_ctx;
static struct xkb_keymap *fixture_keymap;
enum fixture_case { SUCCESS, CANCELLED, NO_CONTEXT, NO_KEYMAP, NO_STATE };
static enum fixture_case fixture;

static void probe(bool library) {
    if ((exempt_depth != 0) != library) scope_failed = true;
    if (!library) own_calls++;
    /* Volatile dispatch keeps the deliberate allocation observable at -O2. */
    void *(*volatile allocate)(size_t) = malloc;
    void *p = allocate(sizeof(char));
    if (!p) probe_failed = true;
    free(p);
}
static void exempt_begin(void) { exempt_depth++; edit_malloc_guard_exempt_begin(); }
static void exempt_end(void) {
    if (!exempt_depth) scope_failed = true;
    else exempt_depth--;
    edit_malloc_guard_exempt_end();
}
#define edit_malloc_guard_exempt_begin exempt_begin
#define edit_malloc_guard_exempt_end exempt_end
#include "../src/x11/input.c"

static bool stop_probe(const work_ctx *job) {
    (void)job; probe(false); return fixture == CANCELLED;
}
static bool publish_probe(work_ctx *job, const work_msg *msg) {
    (void)job; (void)msg; probe(false); return true;
}
static struct xkb_context *context_probe(enum xkb_context_flags flags) {
    (void)flags; probe(true);
    return fixture == NO_CONTEXT ? NULL : xkb_context_ref(fixture_ctx);
}
static int32_t device_probe(xcb_connection_t *conn) {
    (void)conn; probe(true); return 0; /* (P) nonnegative synthetic device ID. */
}
static struct xkb_keymap *keymap_probe(struct xkb_context *ctx, xcb_connection_t *conn,
                                      int32_t device, enum xkb_keymap_compile_flags flags) {
    (void)ctx; (void)conn; (void)device; (void)flags; probe(true);
    return fixture == NO_KEYMAP ? NULL : xkb_keymap_ref(fixture_keymap);
}
static struct xkb_state *prepare_probe(struct xkb_keymap *keymap) {
    probe(false);
    return fixture == NO_STATE ? NULL : x11_input_prepare_state(keymap);
}
#define work_should_stop stop_probe
#define work_publish publish_probe
#define xkb_context_new context_probe
#define xkb_x11_get_core_keyboard_device_id device_probe
#define xkb_x11_keymap_new_from_device keymap_probe
#define x11_input_prepare_state prepare_probe
#include "../src/x11/x11.c"
#undef work_should_stop
#undef work_publish
#undef xkb_context_new
#undef xkb_x11_get_core_keyboard_device_id
#undef xkb_x11_keymap_new_from_device
#undef x11_input_prepare_state
#undef edit_malloc_guard_exempt_begin
#undef edit_malloc_guard_exempt_end

int main(void) {
    fixture_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!fixture_ctx) return EXIT_FAILURE;
    fixture_keymap = xkb_keymap_new_from_names(fixture_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!fixture_keymap) { xkb_context_unref(fixture_ctx); return EXIT_FAILURE; }
    const enum fixture_case cases[] = { SUCCESS, CANCELLED, NO_CONTEXT, NO_KEYMAP, NO_STATE };
    bool failed = false;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fixture = cases[i]; own_calls = 0; scope_failed = false; probe_failed = false;
        x11_runtime rt = {0};
        work_ctx job = { .arg = &rt };
        edit_malloc_guard_begin();
        rebuild_keyboard(&job);
        probe(false); /* Exemption must be balanced on success/cancel/failure. */
        size_t counted = edit_malloc_guard_end();
        bool ok = !scope_failed && !probe_failed && !exempt_depth &&
            (!edit_malloc_guard_active() || counted == own_calls) &&
            ((rt.result.state != NULL) == (fixture == SUCCESS));
        printf("keyboard_guard_test: case=%zu own_probes=%zu counted=%zu guard=%s %s\n",
               i, own_calls, counted, edit_malloc_guard_active() ? "active" : "ASan-inert",
               ok ? "PASS" : "FAIL");
        if (!ok) failed = true;
        keyboard_result_destroy(&rt.result);
    }
    xkb_keymap_unref(fixture_keymap); xkb_context_unref(fixture_ctx);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
