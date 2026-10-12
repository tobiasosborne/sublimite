# edit-u82 continuation report

The carried-over implementation is retained. The editor composes the theme's
`editor.background` into `render_grid.surface_background`; raster fills native
pixmap margins and GL clears scissored default-framebuffer margins before
blitting the retained grid. Stable typing frames do not clear the full surface.
The strategy and pixel-work derivation are in `docs/decisions/edit-u82.md`.

## Files touched by this bead

- `src/editor/paint.c`: publish the composed surface background.
- `src/render/render.h`, `src/render/render.c`: carry and validate opaque RGB.
- `src/raster/raster.c`: fill disjoint margins and include changed background in presentation damage.
- `src/gl/renderer.inc`: clear disjoint margins with the composed RGB.
- `tests/editor_test.c`: check composed background on allocation-free composition.
- `tests/raster_test.c`: model native fills/uploads, check fractional strips and transitions, mirror native resize geometry.
- `tests/render_test.c`: expect theme background in the shared native resize contract.
- `tests/gl_test.c`: supply current native geometry to the diagnostic draw.
- `docs/decisions/edit-u82.md`: retained strip strategy and cost derivation.
- `docs/worker-reports/edit-u82-s6.md`: this continuation report.

Production and test changes above were inherited. This continuation temporarily
disabled the raster margin-paint call, rebuilt and ran the regression, then
restored the source byte-for-byte against the saved fixed copy. It makes no
additional production changes. Verification logs are under `build/edit-u82/s6-*`.
No forbidden tracking commands or coordinator documents were changed.

## Red run

The existing regression was kept unchanged. Only its backend margin-paint call
was disabled at runtime, keeping the native fill mock referenced so the
negative control compiled with warnings treated as errors. The red build
succeeded. Captured process output below is (M)[AC]; exit status and assertion
location come directly from the run, not a budget.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review edit-u82
raster_test:1637: FAIL surface[y * WIDTH + x] == (x < CELL && y < CELL ? cells[0].bg : grid.surface_background)
exit: 1
```

## Green runs

After restoring the fix, both builds passed the same regression. Captured
outputs are (M)[AC]; the fixture geometry derives from the smallest positive
integer sub-cell remainder, as documented beside the test and in the decision.

```text
$ build/tests/raster_test --review edit-u82
edit-u82: fractional strips, stable frame, theme change, geometry shrink, no allocations passed
exit: 0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review edit-u82
edit-u82: fractional strips, stable frame, theme change, geometry shrink, no allocations passed
exit: 0
```

The typing allocator guard passed in release mode. The key count in this
inherited fixture comes from its existing outer loop times its inner batch;
it is not a new count or performance budget. Zero malloc calls is the
allocation law (P); the observed result below is (M)[AC]. `EDITOR_ALLOC_ONLY=null`
selected this run. Under sanitizers the guard is inert because ASan owns malloc.

```text
$ build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit: 0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
exit: 0
```

The editor's headless minimap/composition check passed in release and sanitizer
builds and includes the new composed-background assertion.

## Required build and suite verification

`make all` passed with the required GCC release toolchain. Every sanitizer test
binary also built with the required Clang toolchain. Compiler versions were
verified from the installed executables against the requested toolchains (M)[AC].
Logs: `build/edit-u82/s6-make-all.log`, `build/edit-u82/s6-san-build.log`.

All non-display binaries were exercised serially. The Makefile's display
classification was used, with `file_kill_test` additionally treated as display:
its inherited `view_init` calls `plat_init` and creates a raster backend.
The non-display `make check TEST_SAN=...` invocation stopped at IPC; a subsequent
invocation stopped at journal kill; the rest were run individually so failures
did not hide later results. Every non-display suite passed except `ipc_test` and
`journal_kill_test`. This is not a clean full `make check` result.

The full result inventory is `build/edit-u82/s6-nondisplay-results.txt`.
Run logs: `s6-nondisplay-check.log`, `s6-nondisplay-tail.log`, and
`s6-<suite>.log` under the same directory. `lineidx_test` initially failed because
its existing sparse corpus file was absent. Preparing that sparse fixture with
the exact size asserted by `test_combined_ownership` allowed the full suite to
pass; its rerun is `s6-lineidx-fixture-rerun.log`. This changed test setup only.
The replay CLI shell contract also passed (`s6-replay-cli.log`).

All sanitizer runs use `ASAN_OPTIONS=detect_leaks=0`: the first negative-control
run produced LeakSanitizer's ptrace fatal error in this sandbox. ASan and UBSan
remain enabled. Leak checking has not been certified here.

## Display limitations and remaining acceptance

Display suites fail inside this sandbox because the X socket cannot be used.
The full `make check` attempt stopped in the display-classified CLI suite at
local IPC initialization, before reaching its X checks. Its captured output is
`build/edit-u82/s6-display-check.log`. A local Unix socket bind probe separately
returned `EPERM` (M)[AC], explaining that early IPC failure. Targeted native
checks showed the following (M)[AC]; statuses and source locations are captured
process results.

```text
raster_test:605: FAIL render_test_prepare(&b,&cfg)==RENDER_OK
```

```text
gl_review: native diagnostics SKIP: Xvfb/EGL context unavailable (result=-10; :99 has no DRI3)
```

The GL command's success exit is an explicit native skip, not pixel validation.
The raster typing-path test could not open its editor either
(`s6-typing-raster.log`). The extra display-lock shell contract was interrupted
while waiting for another worker's display lane; it was not counted as passed.
Its holder diagnostic is
`build/edit-5cv-evidence/lock-contract-4olEQq/holder.log`.

The coordinator must rerun display suites on Xvfb :99, including native raster
resize (`build/san/tests/raster_test --review P2-1-9`), GL resize
(`build/san/tests/gl_test --review 13`), and the release raster allocation test
(`EDITOR_ALLOC_ONLY=raster build/tests/editor_test`). The display names and review
selectors are inherited fixture identifiers, not new numeric budgets. Native GL
pixels remain unverified until its diagnostic can actually run rather than skip.
The supplied Sublime screenshot remains the golden-master evidence; there was
no fresh native comparison in this sandbox.

## Findings outside this bead

`ipc_test` fails local socket setup in the sandbox. The authorized rerun with
binding enabled gets past that setup and fails the inherited main-launcher
acceptance assertion. The surface-background changes do not change launcher or
IPC behavior; no IPC or launcher source was edited. Its output is (M)[AC]:

```text
sublimite: unsaved session journal: /tmp/sublimite-ipc-bnzahQ/main-primary/sublimite/session-jZdUQc
sublimite: wait failed: IPC input/output operation failed
main launcher primary: split=0 token=0 live=0 tabs=4 remaining=0 server=1
FAIL tests/ipc_test.c:752: accepted
```

`journal_kill_test` fails replay. Inspection shows its callback requires the
legacy window payload while `journal_set_window` emits `JOURNAL_WINDOW_BYTES`.
That stale fixture expectation is outside the background-paint bead and was
left unchanged. The captured failure is (M)[AC]:

```text
== build/san/tests/journal_kill_test
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
make: *** [Makefile:131: check] Error 1
```

`file_kill_test` is missing from the Makefile's display classification even
though its viewport and queued open/save cases create a native raster window.
Its failures here are recorded in `s6-file-kill.log`; it needs the coordinator's
Xvfb rerun. The Makefile was not changed.

## Bench records and disposition

The render benchmark passed in release mode with its existing TRACK rows.
The raster and GL benchmark commands returned successfully with explicit native
skips because X11 is unavailable; they do not establish runtime performance of
the new clears. Logs: `s6-render-bench.log`, `s6-raster-bench.log`, and
`s6-gl-bench.log`. Power was checked before each run: BAT0 reported
`Not charging`, therefore (M)[AC]. Load and timing evidence are in the logs;
no new performance budget was introduced.

The bead's implementation and reproducible raster red/green verification are
finished. Full acceptance remains pending native display verification, leak
checking outside this ptrace sandbox, and coordinator triage of the unrelated
IPC and journal-kill failures. No additional bead was attempted.

