# edit-457.8 — hot-exit startup fix

Fixed in this worktree. `make all`, ASan/UBSan `editor_hot_exit_test`,
`journal_test`, `editor_session_test`, and `editor_test --headless` passed.
The release typing allocation guard also passed. No git or bd commands were
run; no native display connection was attempted.

## Root cause

This is a backend initialization ordering regression, not EMFILE in the
reproduced run. `editor_open` restores the session before platform/backend
creation. `install_session` applies the journal's WINDOW record through
`editor_resize_now`, which now uses the shared `editor_resize_grid` helper.
That helper unconditionally called `render_backend_resize`. The render adapter
rejects an uninitialized backend with `RENDER_ERR_STATE`; recovery maps the
failure to `EDITOR_ERR_IO`, so `editor_open` fails.

Temporary diagnostic instrumentation captured the failing call and was removed:

```text
recovery diagnostic: resize rc=-4 backend_initialized=0 saved=292x97 grid=33x3 cell=8x15
editor_hot_exit_test:174: FAIL editor_open(&e, &cfg, &backend) == 0
```

The return values are the existing enum encodings (G, API schema):
`RENDER_ERR_STATE = -4`, `EDITOR_ERR_IO = -103`. The dimensions are (M)[AC]
fixture state: default columns are `strlen("Source changed: R reload / K keep")
= 33`; rows and live tabs are the existing `LIVE_TABS = 3`. With the measured
legacy atlas cell `8 x 15`, the injected half-cell resize is
`(33 + 3) * 8 + floor(8 / 2) = 292` pixels wide and
`(3 + 3) * 15 + floor(15 / 2) = 97` pixels high. Thus recovery must change the
grid to `floor(292 / 8) = 36` columns and `floor(97 / 15) = 6` rows. This reaches
the invalid backend call; unchanged grid sizes could avoid it through the
early return in `editor_resize_now`. The field rename itself does not change
the WINDOW wire format or pixel-to-cell conversion.

## Diff summary

- `src/editor/editor.c`: call `render_backend_resize` only when the backend is
  initialized. Startup still applies all grid/layout sizing before publishing
  the replacement checkpoint; backend initialization receives those dimensions
  later in `editor_open`. Runtime resize retains its backend checks.
- `tests/editor_hot_exit_test.c`: additionally compare initialized backend
  configuration with the captured dimensions, and print trial/open return
  diagnostics on startup failure. Existing pixel-edge and full-session
  comparisons remain intact.
- `tests/editor_test.c`: add `--headless` to run the normal suite's headless
  cases, including null typing/journal/allocation tests and injected backend
  fallback checks. Omit native X cases and the launcher-socket case. Default
  invocation and individual native test modes retain their assertions.
- Update the decision record, HANDOFF and daily worklog.

No allocation was added. The production change only guards an existing backend
call; startup and runtime use the already reserved grids and layout storage.

## Red run

Evidence: [original reproduction](../../build/edit-457.8-fix-evidence/red-hot-exit.log),
[diagnostic](../../build/edit-457.8-fix-evidence/red-diagnostic.log), and
[strengthened regression before the fix](../../build/edit-457.8-fix-evidence/red-regression.log).

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_hot_exit_test
editor_hot_exit_test:174: FAIL editor_open(&e, &cfg, &backend) == 0
exit: 1

# After strengthening the fixture, before the production fix:
trial=0 kill_point=38 recovery open returned -103
editor_hot_exit_test:179: FAIL opened == 0
exit: 1
```

Source locations are diagnostic line labels. The trial label is the loop's
initial index; the kill point is (G, seeded fixture)
`((1103515245 + 12345) >> 16) % TRIALS = 38`, with `TRIALS = 100` from the
bead's acceptance requirement. Exit codes are observed process statuses
(M)[AC], interpreted by the shell's existing success/failure convention.

## Green runs

Measurements are (M)[AC]: power supply reported `BAT0=Not charging`,
`AC/online=1` (the kernel's online flag), checked during diagnosis and after
verification. ASan and UBSan remained enabled. Only LeakSanitizer was disabled
for the sandbox's documented ptrace restriction.

```text
$ make all
exit: 0

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_hot_exit_test
editor_hot_exit_test: ok trials=100 (G: bead acceptance), seeded random typing kill points; text/tabs/order/active/cursor/selection/scroll/window restored before first frame
exit: 0

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --headless
editor_test: headless passed
exit: 0

$ EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit: 0
```

The completed hot-exit count is the loop count against the requested (G)
acceptance count. The allocation run's key count derives from the existing
fixture's outer batches times inner keys: `100 * 100 = 10000`; the zero is the
active interposer's measured counter, with the law's zero-allocation assertion.

The Makefile's sanitizer binaries were built and run through its check driver:

```sh
ASAN_OPTIONS=detect_leaks=0 make check \
  TEST_SAN='build/san/tests/editor_hot_exit_test build/san/tests/journal_test build/san/tests/editor_session_test build/san/tests/replay_test' \
  DISPLAY_TEST_SAN=
```

All selected binaries passed, including journal full-VIEW roundtrip/rotation,
legacy defaults and invalid payload checks. The replay CLI shell contract also
passed. The driver's final display-lock shell contract waited on an externally
held shared display lock (`held by unknown` in this PID namespace); I
interrupted that unrelated wait. Therefore this is **not** a claim that the
whole check driver exited successfully or that native/leak acceptance passed.
The requested headless binaries themselves are green.

Logs: [release build](../../build/edit-457.8-fix-evidence/make-all.log),
[hot-exit](../../build/edit-457.8-fix-evidence/green-hot-exit.log),
[selected check](../../build/edit-457.8-fix-evidence/green-check.log),
[headless editor](../../build/edit-457.8-fix-evidence/green-editor-headless.log),
[release guard](../../build/edit-457.8-fix-evidence/green-release-allocation.log).
Baseline-relative source changes are saved in
[changes.diff](../../build/edit-457.8-fix-evidence/changes.diff).

## Outside scope

`src/editor/open.c` still overwrites configured raw window pixels with
`cols * cell_w` / `rows * cell_h` after initially recording the explicit pixel
dimensions. By inspection, a fresh explicit-pixel startup can lose fractional
cell edges. Restored geometry is applied after that assignment and passes this
bead's edge comparisons. Left unchanged, per the scope restriction.

Native display acceptance and normal LeakSanitizer inspection remain for the
coordinator's permitted environment. The display-lock wait above and ptrace
denial during attempted strace diagnosis were environmental limitations; no
lock, inotify or render-adapter contracts were changed.
