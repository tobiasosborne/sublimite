# edit-6yom: test display hygiene (check18 triage)

Test files only; product behaviour unchanged.

1. `tests/wayland_startup_test.c`, unguarded child: `unsetenv("EDIT_REFUSE_REAL_DISPLAY")`
   in addition to the existing `EDIT_ALLOW_REAL_DISPLAY=1`. Since edit-chy, refusal in
   `src/main.c` is opt-in through `EDIT_REFUSE_REAL_DISPLAY`, which `tests/display_guard.h`
   and the Makefile's SAFE_DISPLAY both export; the child must clear it to reach the
   compositor connect. `EDIT_ALLOW_REAL_DISPLAY` stays because the force-included guard
   still reads it.
2. `tests/x11_identity_test.c`: instead of requiring `DISPLAY == ":99"`, require a non-empty
   DISPLAY that the production policy (`edit_display_is_real`, from the force-included
   guard) does not call real. The guard has already redirected a real display before
   main, so this is a sanity check, not a selector. The pass line names the display.
3. `tests/x11_clip_review_test.c`: `fixture_init` connects to `getenv("DISPLAY")`, `:99`
   when unset or empty, so it talks to the display `tools/with_display_lock.sh` locked.
4. `tests/x11_clip_test.c` (large snapshot released): pump one round at a time until
   `x11_clip_busy` is 0, watchdog 1000 ms. The 1000 ms is not a new budget: it is the
   watchdog the same test already uses for the notify wait in this function and for
   `test_serve_stalled_requester`; it bounds a hang, it gates no latency. The old fixed
   window was 3 rounds of `plat_run_for(4 ms)`, about 12 ms (E), shorter than the worst
   UI poll gap of 16.8 ms (M, check18 triage, load1 25-40, AC).
