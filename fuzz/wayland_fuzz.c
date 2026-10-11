/* Real headless protocol transport. Every byte becomes an arbitrary evdev
 * key/state plus pointer axis value, through server marshalling, client
 * dispatch, xkbcommon and the fixed queue. Population comes from input size,
 * clipped to existing platform queue capacity / events-per-byte (not a budget).
 * Native authentication is mocked exactly as in tests/wayland_mock.h. */
#include "wayland/plat.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#ifdef EDIT_HAVE_WAYLAND_SERVER
#include <wayland-client.h>
#include "../tests/wayland_mock.h"
static void fuzz_event(void *user, const plat_event *event)
{ if (event->utf8_len > sizeof event->utf8) __builtin_trap();
  if (event->kind == PLAT_EV_CLIPBOARD && event->clip_ok) *(bool *)user = true; }
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
#ifdef EDIT_HAVE_WAYLAND_SERVER
    /* Leave capacity for initial configure/key/pointer/focus fixture events.
     * Half of X11_QUEUE_CAP leaves room for those and two events per byte. */
    size_t bound = 256u / 2u / 2u;
    size_t len = size < bound ? size : bound;
    pid_t child = mock_start_payload(true, data, len);
    plat p; plat_config cfg = {.width = 1, .height = 1, .work_eventfd = -1};
    int rc = plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND);
    if (rc == PLAT_OK) {
        bool received = false; plat_callbacks cb = {.ud = &received, .on_event = fuzz_event};
        (void)plat_run_for(&p, &cb, 0);
        if (plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) != PLAT_OK) __builtin_trap();
        while (!received) if (plat_run_for(&p, &cb, 0) != PLAT_OK) __builtin_trap();
        size_t bytes_len; const uint8_t *bytes = plat_clip_data(&p, &bytes_len);
        const uint8_t *expected = len ? data : (const uint8_t *)"mock clipboard";
        size_t expected_len = len ? len : sizeof "mock clipboard" - 1;
        if (bytes_len != expected_len || memcmp(bytes, expected, bytes_len)) __builtin_trap();
        received = false;
        if (plat_clip_request(&p, PLAT_CLIP_CLIPBOARD) != PLAT_OK) __builtin_trap();
        size_t retained_len; const uint8_t *retained = plat_clip_data(&p, &retained_len);
        if (retained != bytes || retained_len != bytes_len || memcmp(retained, expected, bytes_len)) __builtin_trap();
        while (!received) if (plat_run_for(&p, &cb, 0) != PLAT_OK) __builtin_trap();
    }
    plat_shutdown(&p); mock_join(child);
#else
    (void)data; (void)size;
#endif
    return 0;
}
