# edit-zzj.15 — session 8b correction

Scope: the coordinator's raster idle assertion failures and accompanying
LeakSanitizer report. No git mutations, no bd, no Makefile changes.

## What was wrong

The poll counter was accurately counting an extra readiness wake. Platform
initialization always selected Present CompleteNotify on its connection.
Raster separately selected CompleteNotify/IdleNotify on its private connection,
and its fence worker published completion through the work mailbox. The editor
ignored the platform completion callback for non-GPU backends, but the duplicate
notification could still wake the platform poll after raster's completion had
made `settle` return. This accounts for the timing-sensitive idle failure with
explicit raster and with the new default's EGL-to-raster fallback. The exact
`poll_returns == before + 1` checks are retained; accounting is unchanged.
This causal diagnosis is from source inspection; display execution remains
blocked in this sandbox, as recorded below.

The reported 2,160,000-byte allocation is **raster**, not an EGL allocation:
`cpu_init` allocates 360 * 300 cells at 20 bytes per cell. Its glyph/page/strip
snapshots are likewise owned by `cpu_state`. `cpu_release` frees all four, and
successful backend shutdown invokes it. The failing selected-backend test used
the generic early-return assertion macro: the idle assertion returned before
`editor_close`, leaving the initialized raster backend's allocations alive.
The shared `init_worker` stack frame does not distinguish the first EGL attempt
from the second raster attempt.

Failed real GL init already calls `gl_release`, including EGL/GL objects,
libraries, special event subscription and its owned arena. The common adapter
clears state/config on init error. Calling shutdown on a failed descriptor
would be ineffective (`initialized == false`) and is not the missing release.

## Fix and ownership

* Store the platform Present subscription ID and expose a checked subscription
  setter. Once raster initializes, the editor disables the platform subscription
  before starting document rendering. Raster retains its own completion/fence
  connection and work messages. EGL retains the existing platform subscription.
* Mark the editor arena before backend state allocation. On failed EGL init,
  reclaim that state reservation before installing and allocating raster state.
  Document the ownership contract in render.h: init rolls back native/heap
  resources on error; shutdown owns them after success; callers own state memory.
  On editor_close, backend shutdown precedes worker/platform shutdown and arena
  free, so live raster snapshots are released before their owner disappears.
* Keep every selected-backend assertion, but route assertion failures through
  cleanup. Close the live editor, disarm an active allocation guard, and remove
  the temporary journal. Null the owner after the first successful close.
  A future failed assertion therefore cannot generate this same raster leak.

## Red evidence

Coordinator's supplied leaks-on `make check` run:

```
editor_test:373: FAIL editor_get_stats(e).poll_returns == before + 1
editor_test:395: FAIL selected_backend_test("raster", false) == 0
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 2160000 byte(s) ... cpu_init ... raster.c:331
Direct leak of 21176 byte(s) ... init_worker ... open.c:15
```

Before edits, locally ran:

```
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test
editor_test:283: FAIL opened == 0
editor_test:310: FAIL backend_fallback_case(true) == 0
editor_test:392: FAIL backend_fallback() == 0
```

This is an environment failure, **not** a reproduction of lines 373/395.
Only `/tmp/.X11-unix/X0` exists; real display :0 was not used. Starting Xvfb :99
failed, including requested escalated attempts. A minimal AF_UNIX socket bind
also returned `PermissionError: [Errno 1] Operation not permitted`. Requested
that :99 be started outside the sandbox while continuing independent work.
Local red log: `/tmp/edit-zzj.15-s8b-red.log`.

## Verification

Power status was `Not charging`; local execution results below are (M)[bat].

* `make -j4 all`: exit 0, including the final fixture cleanup edits.
* Sanitizer editor rebuild: exit 0 (clang ASan/UBSan).
* `ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --selection`: exit 0.
* `ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --gpu-completion`: exit 0;
  its exact idle poll increment check passes.
* `ASAN_OPTIONS=detect_leaks=0 build/san/tests/render_test`: exit 0;
  lifecycle, ownership, bounds and hooks pass.
* `git diff --check`: exit 0 (read-only git).
* `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`:
  builds succeed, then exit 2 at cli_test because the test display is absent:

```
== build/san/tests/cli_test
cli_test:40: FAIL !xcb_connection_has_error(c)
cli_test:78: FAIL session(file, dump, false) == 0
make: *** [Makefile:101: check] Error 1
```

A post-fix full editor attempt likewise cannot open the display. These are
not green full-suite results. LeakSanitizer cannot run in this sandbox.
Logs: `/tmp/edit-zzj.15-s8b-all.log`,
`/tmp/edit-zzj.15-s8b-final-build.log`, `/tmp/edit-zzj.15-s8b-check.log`,
`/tmp/edit-zzj.15-s8b-editor-green-attempt.log`.

Coordinator must rerun the exact selected-backend idle checks and full
`make check` on :99 with leaks enabled. Implementation is ready for that
verification; this session cannot claim the required full red/green acceptance.
