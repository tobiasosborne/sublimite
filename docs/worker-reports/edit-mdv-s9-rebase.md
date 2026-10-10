# edit-mdv session 9 rebase report

Replayed `cb77208` onto main `6f40d16`; `GIT_EDITOR=true git rebase --continue`
exited 0. Rebase completed on `wt/edit-mdv` at `ec10169`.

## Conflicts and resolution

- `src/file/file.c`: retained main's `file_snapshot_backing`, backing reference
  management and sticky fault accessors, then retained the bead's independent
  `file_source` identity/mapping lease, retain/release and worker validator.
  Both share the existing refcounted `file_map` and SIGBUS service; neither API
  replaces the other. Main's derived find/index invalidation and the bead's
  actual-original save validation both remain available.
- `tests/savectl_test.c`: retained main's `creation_permissions` regression and
  always-on syscall I/O/publication instrumentation; retained every new bead
  regression and its command-line selector. Used the bead's argc/argv main and
  ran both sets in the default suite. Removed the incoming obsolete
  `SAVECTL_IO_WRAP` conditional so main's completion-order assertion continues
  to run in ordinary builds. Changed the incoming `journal_base_conflict`
  work-pool allocation from `calloc` to `aligned_alloc(_Alignof(work_pool),
  sizeof *jp)`, with the same alignment assertion as main's other fixtures.

Intent references: `docs/worker-reports/edit-mdv-s9.md` and decisions
`edit-mdv.md`, `edit-yqu.md`, `edit-4w1.55.md`, `edit-zzj.13.md`, `edit-2vs.md`.
Main's automatically merged creation mode forwarding remains in savectl; main's
editor and raster changes remain inherited unchanged.

## Minimal interaction fix after rebase

The first GCC release build exited 2: main's test includes the production
savectl translation unit, whose new bead typedef `notification` collided with
an existing test parameter under `-Werror=shadow`:

```text
tests/savectl_test.c:185:23: error: declaration of 'notification' shadows a global declaration [-Werror=shadow]
```

Renamed only `race`'s boolean parameter from `notification` to `notified`,
including its single use. No behavior, assertion, production API or warning
flag changed. This two-line follow-up and this report are staged after the
completed rebase; no amend or additional commit was made.

## Validation

GCC 13.3.0 release, C11 repository flags including `-Werror`:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -j8 all
```

Final exit code: **0**. Log: `build/edit-mdv-s9-rebase-all.log`.
Initial shadow-error log: `build/edit-mdv-s9-rebase-all-initial.log`.
Tail of successful run:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/scroll_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/scroll_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/raster_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/raster_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
```

The combined release `build/tests/savectl_test` also exited **0**; log:
`build/edit-mdv-s9-rebase-savectl-release.log`. Its default suite executed both
sides' regressions, including ordinary I/O/publication and permission checks.

Clang 18.1.3 ASan/UBSan, **leak detection off**:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j8 check
```

Final exit code: **0**. All **59 test binaries passed** and the replay CLI
checks passed. Log: `build/edit-mdv-s9-rebase-check.log`. The final sanitizer
suite includes both sides' savectl checks, retained file-backing invalidation,
piece memory bounds, editor allocator regressions, and the raster fence-lifetime
regression. No runtime interaction fix was required.
Tail of successful run:

```text
The XKEYBOARD keymap compiler (xkbcomp) reports:
> Warning:          Could not resolve keysym XF86CameraAccessEnable
> Warning:          Could not resolve keysym XF86CameraAccessDisable
> Warning:          Could not resolve keysym XF86CameraAccessToggle
> Warning:          Could not resolve keysym XF86NextElement
> Warning:          Could not resolve keysym XF86PreviousElement
> Warning:          Could not resolve keysym XF86AutopilotEngageToggle
> Warning:          Could not resolve keysym XF86MarkWaypoint
> Warning:          Could not resolve keysym XF86Sos
> Warning:          Could not resolve keysym XF86NavChart
> Warning:          Could not resolve keysym XF86FishingChart
> Warning:          Could not resolve keysym XF86SingleRangeRadar
> Warning:          Could not resolve keysym XF86DualRangeRadar
> Warning:          Could not resolve keysym XF86RadarOverlay
> Warning:          Could not resolve keysym XF86TraditionalSonar
> Warning:          Could not resolve keysym XF86ClearvuSonar
> Warning:          Could not resolve keysym XF86SidevuSonar
> Warning:          Could not resolve keysym XF86NavInfo
Errors from xkbcomp are not fatal to the X server
x11_stall_test: key callback reply 0.071 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.075 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.094 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.069 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.029 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.013 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

The initial sandboxed check exited **2** at `cli_test` because its XCB connection
could not reach private display :99. Log:
`build/edit-mdv-s9-rebase-check-sandbox.log`. The final check uses approved
outside-sandbox access to the already running private Xvfb :99. Attempts to
start Xvfb failed; no new display server was started, and the existing one was
not modified or stopped. No command or test used display :0.

## Remaining scope

No unresolved merge conflicts or unreconciled intent. Original bead findings
18–20 remain incomplete exactly as recorded in its session 9 report; this
rebase does not claim their implementation. `HANDOFF.md`, `docs/worklog/` and
the Makefile were not edited. Only `git add` and the requested
`GIT_EDITOR=true git rebase --continue` were used as Git write commands.
