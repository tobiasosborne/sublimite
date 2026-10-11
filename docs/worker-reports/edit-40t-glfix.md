# edit-40t glfix: gl_test failure on Xvfb :99

## Verdict
Neither side was simply wrong: the worker's classification (driver init errors fail, only
RENDER_ERR_UNSUPPORTED skips) is right, and the injected-fault contract holds. The failure
came from a latent bug in `src/gl/gl_driver.h` (introduced by de7fb22, review section 13)
that the old "any non-OK init is SKIP" classification had been hiding.

## Root cause
`gl_driver_init_job` published its completion message with `generation` 0 while
`gl_driver_init_for` receives with generation 1 (`work_mailbox_receive` matches slot, epoch AND
generation). The message was never delivered, the 10 s wait expired, and init returned
RENDER_ERR_INIT (-9) regardless of the backend's real result (the job itself returned -10,
UNSUPPORTED, from the Present probe on Xvfb). `gl_wake_job` in renderer.inc already sets
`.generation=ctx->generation`; the init job forgot. Evidence: temporary prints showed
`JOB result=-10`, `PUB=1`, `WAIT done=0 t=10.0 s`.

## Fix
1. `src/gl/gl_driver.h`: `work_msg msg = {.kind = GL_POLL_MESSAGE, .generation = c->generation};`
   Init results now arrive immediately and with the true code (Xvfb: -10 -> clean SKIP, ~1.6 s
   instead of a 10 s timeout). Bench code and the worker's classification are unchanged.
2. `tests/gl_test.c`: with init working, `gl_review_native_available` is true on Xvfb (readback
   init override skips the Present probe), so `gl_review_native_test` ran real window-pixel
   contracts and failed (`render_test:465 ... got=00000000`, plus leaked platform objects on the
   early return). Added `gl_review_present_available()` (production init, includes the Present
   probe) and gate `gl_review_native_test` (and argv 13/13-noop) on it, printing a SKIP line.
   Sections 1-6, 12 still run natively on Xvfb and pass.

## Runs ($DISPLAY=:99, ASan/UBSan, through with_display_lock.sh)
Red (before, worktree as handed over): gl_bench `FAIL driver_init result=-9`,
`gl_test:1356 FAIL rc==1 && injected`, `gl_test:1454 FAIL gl_bench_cleanup_test()==0`.
Intermediate (gl_driver.h fix only): sections 5 all PASS, then `render_test:465 FAIL`,
`gl_test:929`, `gl_test:1485`, plus LeakSanitizer reports.
Green (both fixes): gl_test rc=0, gl_bench_test rc=0, gl_gate_test rc=0; section 5 stages
arena/prepare PASS with injection evidence, init/bulk/frame/integrated SKIP (native
unavailable, explicitly allowed); `gl_bench --bulk-self-check/--integrated-self-check/
--pace-self-check` exit 0; `make all` (gcc -Werror) clean.
Not run: real-GL machine (Astra); with the fix the init path now completes there instead of
hitting the 10 s timeout, which should be confirmed by the coordinator.
Note: the worker's `xvfb_no_context` (-9 on :99) allowance in `gl_review_native_available` is
now dead but harmless; left untouched.
