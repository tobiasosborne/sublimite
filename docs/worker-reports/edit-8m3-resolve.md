# edit-8m3 resolver onto main after edit-g11

Resolved the conflict markers in `src/editor/paint.c`,
`src/minimap/minimap.c`, and `tests/minimap_test.c`. No `git` or `bd`
commands were run. Only those files and this report were edited; builds and
tests produced their usual generated artifacts. The rebase remains for the
coordinator to stage and continue.

## Resolution by hunk

- `paint.c`, colour initializer: take edit-8m3's background, character ink,
  and viewport colours (`0x20242c`, `0x78808e`, `0xe5e9f0`). Keep edit-g11's
  stale slot exactly as `editor_theme_estimated_gutter(0x8091a8, 0x171b22)`.
  The removal of the tab-strip `!` remains intact.
- `minimap_fill`, first hunk: keep edit-g11's saved ready viewport and restore
  it when compatible per-buffer summaries are retained. Compose edit-8m3's
  character masks and translucent wash/perimeter from those summaries, even
  while internally stale. Remove the incoming stale colour tint and its
  stale-state viewport suppression. This also preserves returning-tab
  reconstruction into a shared grid rather than leaving another tab's cells.
- `minimap_fill`, second hunk: keep the approximate viewport calculation when
  no compatible summaries exist, and use edit-8m3's wash/perimeter composition.
  Ready summaries and their viewport remain unchanged during pending edits.
- `minimap_fill`, third hunk: use the per-character mark mask instead of a
  density prefix for ready/retained summaries. Keep edit-g11's fallback density
  and last-column pending bar exclusively for missing/incompatible summaries.
  Retained pending images receive no indicator. The existing cached-worker
  adapter delegates to this same fill and was left intact.
- `minimap_test.c`, pending resize hunk: keep edit-g11's assertions for the
  fallback density and last-column bar; discard the incoming stale-tint
  assertion. Keep both branches' remaining tests, including pending-cell
  equality, shared-grid reconstruction, worker publication, character positions,
  expanded tabs, clipping, and caller-owned pixel output with source/allocation
  guards. `minimap_pixels` and character preparation were left intact.

The colours and mask/tab/clipping geometry above are inherited source values,
not newly chosen constants. Wash coverage and perimeter coverage retain
edit-8m3's documented derivation: existing coverage divided by four,
`128 / 4 = 32`, and `3 * 32 = 96`, normalized to `255`. Fallback density
retains edit-g11's existing `64`; the bar is one existing strip cell.

## Build and targeted tests

`make -j4 all` returned success using gcc release with
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.
`make -j4 build/san/tests/minimap_test build/san/tests/editor_test` also
returned success with clang ASan/UBSan. Build logs:
`/tmp/edit-8m3-resolve-make-all.log` and
`/tmp/edit-8m3-resolve-san-build.log`.

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test` returned success:

```text
edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok
edit-8m3: translucent viewport perimeter and half-open geometry: ok
edit-8m3: pixel marks, line spacing, viewport wash/edge, stride, source/allocation guards: ok
review 9: UI deferred fill reads no dispersed samples: ok
review 8: pending index paints current byte scrollbar and drag works: ok
review 5: equal-size tab switch exact byte=1: ok
review 7: cached fill preserves typing/blink damage: ok
minimap_test: density, hit round trip, stale, malloc guard: ok
```

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash`
returned success:

```text
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=10413
```

The release minimap suite also returned success. Release
`build/tests/editor_test --minimap-flash` exercises the active allocation guard
and returned success:

```text
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=1289
```

Runtime outputs are (M)[AC]; `BAT0/status` read `Charging` before the tests.
Counters and maximum fill duration are measured by the existing test hooks,
as defined in edit-g11's report. They are observations, not performance bounds.
Diagnostic line numbers, review identifiers, and process exit codes identify
existing sources/tests and observed results. Build parallelism is a runner
choice. The sanitizer allocation guard is inert; the release guard is active.

## Full editor suite limitation

Ran `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99
build/san/tests/editor_test`. It reported these failures and then stopped
progressing; the run was interrupted. It did not pass or complete:

```text
reload launcher result=4
editor_test:1031: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:422: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:448: FAIL editor_open(&e, &cfg, &b) == 0
```

Direct socket probes in this sandbox reported:

```text
X99 connect: errno=1 Operation not permitted
Unix bind: errno=1 Operation not permitted
```

The native-open failures are consistent with denied X access. The launcher
failure and lack of full completion are recorded without claiming every cause
has been established. The previous branch reports describe similar mixed-suite
failures/incomplete runs under the sandbox. Full editor acceptance requires a
rerun where X and local IPC access are permitted. Raw output:
`/tmp/edit-8m3-resolve-editor-full.log`.

## Non-display make check

Ran `make -j4 check` with `ASAN_OPTIONS=detect_leaks=0`, `DISPLAY=:99`,
`EDIT_DISPLAY=:99`, and a command-line `TEST_SAN` inventory of the non-display
suites. Excluded native/display suites (`cli`, mixed `editor`, native GL,
`raster`, `ref*`, live X tests, and the display-opening `file_kill` helper);
included pure X alignment, guard, input, and XI2 tests. Used
`DISPLAY_TEST_SAN=` for this non-display inventory. No Makefile was edited.
The exact inventory is `/tmp/edit-8m3-resolve-nondisplay-suites.txt`.

The initial check returned make exit `2`, after successful suites preceding
`ipc_test`, at the sandbox socket restriction:

```text
== build/san/tests/ipc_test
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
make: *** [Makefile:105: check] Error 1
```

Continued `make check` with only suites after `ipc_test`, without repeating
the preceding successful suites. Every remaining binary passed, including
`view_test`, line-index, minimap, piece, work, layout, render, and Unicode
tests. Its replay CLI shell check also passed:

```text
check: 32 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

The continuation's display-lock shell contract returned failure because the
command-line `DISPLAY_TEST_SAN=` override propagated to its nested make and
bypassed the lock that the contract expects. This was a runner configuration
issue, not a minimap failure. Ran the contract independently without the
override; it returned success and printed:

```text
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The independent run initially waited for an external display holder before
completing. A further bounded lock probe used the contract's existing timeout
value and returned failure on external contention:

```text
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
```

Built and separately ran the remaining pure `gl_gate_test` under the same
sanitizer options; it returned success:

```text
gl_gate_test: ok
```

(M)[AC] Non-display binary results: `45` passed and `1` failed (`ipc_test`).
Derivation from each make recipe's sequential test markers/stop point:
`12` successes before IPC plus `32` in the continuation plus the separately
run pure GL gate equals `45`; adding IPC gives `46` inventoried binaries.
This is not an unrestricted `make check` pass. Logs:
`/tmp/edit-8m3-resolve-check.log`,
`/tmp/edit-8m3-resolve-check-cont-1.log`,
`/tmp/edit-8m3-resolve-gl-gate.log`,
`/tmp/edit-8m3-resolve-display-contract.log`, and
`/tmp/edit-8m3-resolve-display-contract-bounded.log`.

## Final state

A literal scan of all three conflicted files found none of `<<<<<<<`,
`=======`, or `>>>>>>>`. Builds and the combined minimap/typing-flash
regressions pass. Full editor and unrestricted non-display acceptance remain
unverified due to the failures above; the coordinator must rerun with X and
local IPC permitted. No unrelated source or test fixes were made.
