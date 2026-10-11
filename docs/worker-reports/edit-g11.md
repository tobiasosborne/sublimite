# edit-g11 — edit-time minimap flash

Implemented in this worktree without running `bd` or `git`. The typing frame
keeps the previous ready minimap while its index catches up. Fresh publication
uses the existing scheduling; no frame delay, timer or typing allocation was
added. Glyph lookup in `paint.c` was left unchanged.

## Changed files

- `src/editor/paint.c`: remove the stale tab-strip exclamation mark; choose the
  pending edge-bar colour with the existing half-contrast theme blend.
- `src/minimap/minimap.c`, `src/minimap/minimap.h`: retain ready summaries and
  their viewport during pending fills, including the cached worker path. A
  shared grid reconstructs the returning buffer's ready image. Missing or
  incompatible caches use a pending scrollbar with an edge bar, without tint.
  Internal stale checks still reject exact navigation and request refills.
- `tests/editor_test.c`: add `--minimap-flash` and include it in the default
  suite. Inspect every submitted null-backend typing frame, verify final
  revision/length, and count typing allocations.
- `tests/minimap_test.c`: require pending cells/viewport to remain identical
  without damage or source reads; check reconstruction in a shared grid,
  cached publication and the pending-resize edge bar.
- `src/editor/STATUS.md`, `src/minimap/STATUS.md`,
  `docs/decisions/edit-g11.md`, this report, `HANDOFF.md`, and the dated worklog:
  describe the decision, verification and coordinator handoff.

## Numeric derivations and power

External `BAT0/status` was `Discharging`: all measurements here are (M)[bat].
The fixture's N=95 (G scenario) comes directly from the supplied Astra typing
repro. Its grid geometry is inherited from the existing editor allocation
fixture; storage is grid rows multiplied by the editor's minimap column count.
No timing or frame-deferral threshold was invented.

In the pasted regression output, `frames` is the submit-hook count;
`orange_frames` counts frames with any minimap cell carrying the original
stale colour or its observed background blend; `stale_frames` counts submitted
internal-stale maps; `changed_pending` counts those maps differing from the
saved prior image; `chrome_markers` counts the last tab cell containing `!`.
All are (M)[bat] counters incremented at submit, not video estimates.
`mallocs` sums the allocation guard results. The sanitizer guard is inert;
the release result exercises the active guard.

The raw colour `0xbf853d` comes from the original painter. Checking only that
value would miss the bug: the existing minimap blend yields `0x6b5030` from
background `0x171b22`. Derivation (P, existing channel arithmetic):
`floor((background_channel * (255 - 128) + stale_channel * 128 + 127) / 255)`;
255 is the maximum unsigned RGB channel, `2^8 - 1`, 128 is the original tint
weight, and 127 is its half-scale rounding term. The test checks raw foreground,
raw background and this rendered background, then independently requires
unchanged pending cells so substituting another transient tint cannot pass.

The quiet bar uses (P) one column, the smallest indivisible existing cell-grid
unit. Its colour is derived per channel by the existing theme half-contrast
blend of minimap density and background. `fill_max_ns` is (M)[bat] the maximum
observed `editor_stats.minimap_ns`, itself `trace_now_ns()` after fill minus
`trace_now_ns()` before fill, over submitted frames in this fixture. It is an
observation, not a bound or a performance comparison between sanitizer/release.
Line numbers, section labels and exit statuses in verbatim tool output are
source/tool identifiers and observed return values, respectively.

## Red, before production changes

Built the added tests with the unchanged painter/minimap, then ran:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash
```

```text
editor_test:137: FAIL probe.orange_frames == 0
edit-g11 null typing: keys=95 frames=96 orange_frames=93 stale_frames=93 changed_pending=3 chrome_markers=93 mallocs=0 fill_max_ns=17535
```

Observed failure exit status; counts and timing are derived by the hooks above.
The frame count includes the explicit maintenance turn after the typed keys.
The independent cached-retention test also failed before implementation:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
```

```text
minimap_test:314: memcmp(previous,cells,sizeof previous) == 0 && dirty[0] == 0
```

## Green

Final submitted-frame sanitizer run, with the same fixture and counters:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash
```

```text
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=12445
```

Final minimap run includes the shared-grid reconstruction assertion:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
```

```text
review 9: UI deferred fill reads no dispersed samples: ok
review 8: pending index paints current byte scrollbar and drag works: ok
review 5: equal-size tab switch exact byte=1: ok
review 7: cached fill preserves typing/blink damage: ok
minimap_test: density, hit round trip, stale, malloc guard: ok
```

Release regression, with the allocation guard active:

```sh
build/tests/editor_test --minimap-flash
```

```text
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=94 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=5056
```

These commands and the release minimap suite returned success. The internal
stale-frame counter proves the test exercises pending maps; no stale paint is
submitted. The test also asserts the final map has the current revision.
The existing isolated chrome-damage test and null allocation suite passed.

## Paint audit and timing evidence

The removed minimap tint and tab `!` were the unconditional edit-time warning
paints. Bracket and trailing-whitespace decorations are prepared before submit;
modified-tab markers follow persistent document state. The source-change
reload/keep text persists until resolution. The estimated gutter can change
its quiet half-contrast treatment when index certainty changes; it remains
semantic index feedback, not an amber strip overlay. Caret-only blink behaviour
was not changed. The minimap suite's existing backend damage tests pass.

Raw `build/astra/` traces are absent from this worktree, and the main repository
was not read. The checked-in Astra review provides video transition timestamps,
not a minimap computation span. No deferral budget can honestly be derived from
those timestamps. Instead this fix retains the ready image until readiness,
without postponing input or clearing the internal stale flag. The measured
fill observations above use the editor's existing trace clock instrumentation.
The suite sweep also runs the existing harness that measures minimap fill
inside an editor frame; raw evidence is in `build/edit-g11/suites/harness_test.log`.

## Builds and full-check constraints

`make all` passed with gcc release, including the final minimap test addition,
under the repository's C11 strict warning flags. `ASAN_OPTIONS=detect_leaks=0
DISPLAY=:99 EDIT_DISPLAY=:99 make check` was run as required and stopped at the
CLI suite:

```text
== build/san/tests/base_test
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

A local socket probe confirms the sandbox permits Unix socket creation but
denies bind, and also denies connection to the coordinator's X socket. Thus
the restriction extends beyond display suites to IPC setup. The standalone
IPC suite reports:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

The standalone sanitizer inventory sweep and failed-suite classification are
recorded below. The mixed editor suite was interrupted after native-open and
headless-settle failures rather than waiting indefinitely after its earlier
failures. Isolated headless replay and cooperative large-file cases passed on
their final unmodified runs. A separate reconstructed pre-fix painter/minimap
also reproduces the orange regression and reaches the same IPC restriction in
the large-editor suite; its replay and cooperative checks passed on their run.
Deadline failures were observed under shared load and preserved in the logs;
they are not claimed to be proven performance regressions or proven harmless.

Replay CLI shell checks and the display-lock contract passed independently.
LeakSanitizer is disabled because the documented sandbox cannot support its
process inspection; coordinator verification must enable leak detection.

### Standalone sanitizer results

The inventory contains (M)[bat] 63 suites, derived by enumerating
`tests/*_test.c` and matching the manifest rows in
`build/edit-g11/suite-results.tsv`: 46 returned success, 16 returned failure,
and 1 mixed editor run was interrupted after failures. Derivation: count the
manifest's success return values, failure return values, and interruption
marker, respectively; `46 + 16 + 1 = 63`. No full-check pass is claimed.

All standalone headless suites that can run without denied socket operations
passed, including lineidx, minimap, piece, work, journal, layout and the harness.
The lineidx edit-during-build case also passed in isolation. The final full
current large-editor run passed its early headless checks, then failed at
`ipc_server_init` in mapped startup, matching the socket restriction. Evidence:
`build/edit-g11/editor-large-final.log`. The earlier cooperative/replay deadline
failures remain recorded, alongside their successful final unmodified isolated
runs; no unrelated deadline or test code was changed.

Display-dependent failures (X socket unavailable in this sandbox):

| Suite | Observed reason |
|---|---|
| `editor_close_test` | Native raster editor open fails; null close case passes. |
| `file_kill_test` | Its included file-bench viewport helper opens a display; viewport and queued child cases fail initialization. |
| `raster_test` | `plat_init` fails in the live completion case; its pure checks run. |
| `refproto_test` | Connection to the assigned display fails. |
| `refwin_test` | Native diagnostic window connection fails. |
| `refwin_timeout_test` | Expected input-stage diagnosis cannot occur because the window connection fails first. |
| `x11_clip_review_test` | Display connection / fixture initialization fails. |
| `x11_clip_test` | Platform initialization fails even with the attempted private Xvfb. |
| `x11_identity_test` | Platform initialization fails. |
| `x11_live_test` | Private-display connection / live fixtures fail. |
| `x11_order_test` | Display fixture initialization fails; the suite later continues into a libxcb SEGV. |
| `x11_stall_test` | Live display initialization fails. |

IPC-dependent failures (Unix bind denied, including otherwise headless setup):
`cli_test`, `ipc_test`, `editor_p4_test`, and the final `editor_large_test` run.
The mixed `editor_test` run additionally reported native-open / IPC launcher
failures and the subsequently passing replay deadline case before interruption.
It is recorded as incomplete, not passed.

`gl_test` and `gl_gate_test` returned success for their pure checks. Native EGL
and integration stages in `gl_test` reported SKIP because X was unavailable;
they still require coordinator verification. The pure X alignment/input/XI
and display-guard suites returned success. Logs for every inventoried suite
are in `build/edit-g11/suites/`.

## Outside scope and remaining handoff

The existing minimap TRACK benchmark's correctness, stale/refill reference and
cold-sample checks passed. It returned failure for warm timing tails on the
loaded battery run; its original return-on-miss behaviour conflicts with the
current tracked-bench policy and is assigned elsewhere. Neither the timing
miss nor that unrelated exit policy was fixed. The raw stamped benchmark is
`build/edit-g11/minimap-bench.log`; its existing sample counts come from
`bench/minimap_bench.c`, percentiles from sorted measured trace-clock deltas,
and its planning row from the existing minimap performance reference.

Legacy replay/large-editor diagnostics print an AC tag literally even though
the external battery stamp was Discharging. This report uses the external
stamp; those unrelated diagnostics were not edited. The editor's existing
synchronous fresh minimap fill can still read nonresident source pages; wiring
worker preparation into that path remains outside this flash bead.

The X-order suite's libxcb SEGV after failed display fixtures is outside scope
and was not fixed. Some suites beyond the obvious X/refwin families also open
a display, especially the file-kill helper above. The harness returned success
for its structural checks but recorded a loaded-box TRACK miss for ingress to
device completion; that recorded timing row is not silently treated as green.

Coordinator: rerun the complete sanitizer check with leak detection and socket
access on the assigned Xvfb display, then rerun the Astra video comparison and
corrected-colour flashscan on the landed build. No display recording or claim
of a new comparison-drive verdict is made here. No other bead was taken.
