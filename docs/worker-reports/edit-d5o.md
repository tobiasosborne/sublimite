# edit-d5o worker report

Scope: the supplied bead only, in this worktree. No `bd` or `git` was run.
Prerequisite decisions read: [edit-g11](../decisions/edit-g11.md) and
[edit-8m3](../decisions/edit-8m3.md). Design: [edit-d5o](../decisions/edit-d5o.md).

## Changes

- `src/editor/paint.c`: compose the miniature into an opaque RGB region over
  the reserved strip; independent region damage, retained pending viewport,
  matching click/drag coordinates, compose trace endpoints.
- `src/editor/private.h`, `open.c`, `buffers.c`, `editor.c`: reserve pixel,
  scratch and summary storage at open, and derive resize geometry.
- `src/render/render.h`, `render.c`: optional caller-owned pixel-region contract,
  bounds/capacity checks and independent damage reset. Cell format is unchanged.
- `src/raster/pixels.c`, `raster.h`, `raster.c`: backend-owned snapshot, worker
  blit into the back buffer, restore retained pixels after damaged cell rows,
  region-only upload and native Present damage for map-only frames.
- `src/gl/renderer.inc`: reserve a small texture and snapshot at initialization;
  a shared EGL context uploads through a work job. An authenticated mailbox
  completion and GPU fence order the region blit before the same frame's swap.
- `src/minimap/minimap.c`, `minimap.h`: permit retained summaries in pixel
  rendering while stale, retaining their ready viewport.
- `src/trace/trace.h`: named compose endpoints appended after existing event IDs.
- `tests/editor_test.c`, `minimap_test.c`, `raster_test.c`, `gl_test.c`: compose
  scanline regression, stale pixel retention, navigation/damage/allocation
  assertions, backend snapshot/upload tests and optional native readback probes.
- `docs/decisions/edit-d5o.md`, `docs/worker-reports/edit-d5o.md`,
  `src/editor/STATUS.md`, `src/raster/STATUS.md`, `src/gl/STATUS.md`,
  `src/minimap/STATUS.md`, `docs/worklog/2026-10-11.md`, `HANDOFF.md`:
  design, results and coordinator handoff in lockstep.

Geometry (G): summary spacing is two pixels per line, from the bead and the
existing minimap pixel API. Summary capacity = ceil(maximum strip pixel
height / spacing). Image capacity = existing strip-column reservation × font
cell width × maximum pixel height. Pixel/scratch storage is reserved before
input. The translucent wash/perimeter uses the existing minimap blend
arithmetic documented in edit-8m3; no new timing budget or frame deferral.

## Red run

Observed before production changes, clang ASan/UBSan, leaks disabled for the
sandbox. Exit failure (M)[AC], BAT0 `Charging`; the assertion compares each
composed strip scanline against an independent summary preparation and
`minimap_pixels` render of the known file. Source line numbers below are
compiler/test diagnostics, not budgets.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-pixels
editor_test:174: FAIL !memcmp(frame + (size_t)(y + e->tab_rows * e->grid.dims.cell_h) * w + w - mw, expected + (size_t)y * mw, (size_t)mw * sizeof *expected)
exit=1

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
minimap_test:198: minimap_pixels(&m, pixels, HEIGHT * STRIDE, WIDTH, HEIGHT, STRIDE, 0, 0, &style) == 0
edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok
edit-8m3: translucent viewport perimeter and half-open geometry: ok
exit=1
```

Exit codes (M) are the actual shell statuses: nonzero means the assertions
failed. Raw logs: `build/d5o/red-editor.log`, `red-minimap.log`.

## Green run and measurements

Final focused runs (M)[AC], BAT0 `Charging`. All shown commands exited zero
(the actual shell statuses). Both sanitizer and release versions passed; the
release allocation guard is active.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-pixels
edit-d5o compose: minimap pixel rows match independent minimap_pixels reference
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok
edit-8m3: translucent viewport perimeter and half-open geometry: ok
edit-8m3: pixel marks, line spacing, viewport wash/edge, stride, source/allocation guards: ok
review 9: UI deferred fill reads no dispersed samples: ok
review 8: pending index paints current byte scrollbar and drag works: ok
review 5: equal-size tab switch exact byte=1: ok
review 7: cached fill preserves typing/blink damage: ok
minimap_test: density, hit round trip, stale, malloc guard: ok
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --pixels-only
edit-d5o raster: owned region snapshot, map-only worker blit, UI upload, untouched text/margins passed
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/gl_test --pixel-region
edit-d5o GL: map-only owned snapshot, texture upload off UI, opaque BGRA, mailbox completion passed
$ EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
```

Allocation fixture count (G) is the unchanged typing regression's supplied
count: its loop injects the existing alternating insert/backspace workload;
(M) zero is the counter's observed allocation total. Source identifiers and
review case numbers in pasted output identify existing tests, not new budgets.
The region tests also exercise active release guards. Their output is in
`build/d5o/release-{editor,raster,gl}.log`; sanitizer output is in
`build/d5o/green-*-final.log`.

`make all` (GCC release, strict C11 warnings) passed, actual exit zero (M),
log `build/d5o/make-all-verified.log`. GCC caught a shadowing variable in the
new test; it was renamed before this successful run. An overlapping initial
check/all invocation also damaged a generated archive; it was removed and
regenerated, and final build drivers ran sequentially. No Makefile changes.

Compose measurement uses the trace ring, not an invented duration bound:

```text
edit-d5o STAMP (M)[AC] BAT0=Charging load1=33.38 shared box
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=21465
edit-d5o compose (M)[AC] samples=97 p50_ns=38674 p99_ns=6170911 max_ns=6170911 derivation=COMPOSE_END-COMPOSE_BEGIN; nearest-rank=ceil(samples*pct/100)
Typing compose (M)[AC]: n=95; p50_ns=38663; p99_ns=6170911; max_ns=6170911; longest_frame_id=29
```

Derivation: first compose-end timestamp minus first compose-begin timestamp
for the same frame ID, sorted with nearest rank `ceil(n*pct/100)`. The typing
population intersects those pairs with frame IDs carrying ingress events;
its count matches the supplied drive's keystroke fixture. The full population
also includes the non-input compose pairs. Probe frame counts and zero-change
counts are measured callback/counter totals. `fill_max_ns` is the maximum
existing fill timer delta, narrower than whole compose. Power/load come from
BAT0 and `/proc/loadavg` before the run. Raw ring: `build/d5o/compose.trace`;
summary: `compose-cost.log` and `trace-summary.txt`. The wall-clock tail on
this shared host is reported without attributing its cause or claiming a
Sublime frame-latency result.

Touched-module benches were attempted, all TRACK. Their raw logs are in
`build/d5o/{minimap,editor,raster,gl}-bench.log`.

- Minimap warm/after-edit p99 (M)[AC] ns: small file `3193364 / 3695314`,
  large log `9189892 / 15189024`; measured load stamps `32.25 / 32.47`.
  Derivation: bench timed fill (and after-edit publication) deltas, sorted
  nearest rank by the unchanged harness. Source refresh/worker preparation
  are outside its after-edit timer. Its printed target is the existing
  (G) TRACK aspiration: `500 µs × 1000 ns/µs = 500000 ns` from the minimap
  goal row. The misses are notes, not new bead blockers. Existing bench exits
  nonzero on these TRACK misses; that exit policy was left outside scope.
- Editor serial null fixture (M)[AC], load `29.40`: ingress-to-present p50
  `5965571 ns`, p99 `33435846 ns`; dequeue-to-submit p50 `5266984 ns`, p99
  `30755959 ns`. Derivation: existing ring endpoint subtraction and nearest
  ranks over the existing qualified typing fixture; structural checks and
  the allocation counter pass. The later raster open fails without X.
- Raster bench cannot open X before its rows and skips measurement. GL bench
  reports X unavailable and skips native initialization. No backend speed
  result is fabricated from these attempts.

No native-display or golden-master verdict is claimed from headless tests.

## Required sanitizer check outcome

`make check` was run with clang ASan/UBSan, safe display `:99` and
`ASAN_OPTIONS=detect_leaks=0`. Its final actual make exit is failure (M):

```text
== build/san/tests/base_test
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

Because the driver stops at the first failure, all test binaries were then
run individually with the same sanitizer options. Raw status table:
`build/d5o/suites.tsv`; per-suite logs: `build/d5o/suites/*.log`. Initial totals
(M)[AC] derive by counting test rows in that table, excluding the replay shell
row: `63 = 45 successful + 18 failed` binaries. The failed file suite passes
on isolated rerun, giving `46 = 45 + 1` successful binaries and `17 = 18 - 1`
remaining failed binaries. Passing binaries with native sections skipped are
not claimed as native passes.

Suites that fail on native X/display setup and need the coordinator's Xvfb
rerun:

- `editor_close_test`, `file_kill_test`, `raster_test`.
- `refproto_test`, `refwin_test`, `refwin_timeout_test`.
- `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`,
  `x11_live_test`, `x11_order_test`, `x11_stall_test`.
- `editor_test` also reaches native raster open failures on its isolated
  rerun; its new compose test and retained-pixel typing test pass first.

`gl_test` passes its pure snapshot/completion/trace checks and explicitly
skips native diagnostics. The new standalone raster and GL readback probes
both fail at editor native initialization here, as expected with the denied
X socket; their logs are `native-raster.log` and `native-gl.log`.

The sandbox also denies **Unix socket bind**, independently of X. A minimal
AF_UNIX probe creates a socket but binding it inside this worktree returns
`EPERM`. `ipc_test` prints `init rc=4 errno=1 (Operation not permitted)`;
these values (M) are its actual enum result and the kernel errno diagnostic.
The affected non-display/IPC-dependent suite entry points are `ipc_test`,
`cli_test`, `editor_p4_test` and `editor_large_test` (the latter on isolated
rerun). Thus the requested blanket non-display pass cannot be established
in this sandbox; this is an additional environment restriction beyond the
supplied X-only exception, not a change made to IPC.

Transient initial failures were retried without product changes:

- `file_test` initially fails its shared-watch delivery assertion; the
  isolated complete rerun exits successfully (`file-test-rerun.log`).
- `editor_large_test` initially expires its existing worker-lane watchdog;
  the isolated rerun passes that check and subsequent headless checks, then
  fails at IPC server creation (`editor-large-rerun.log`).
- `editor_test` initially fails a null recovery child open. Standalone
  `--ck4-keep` passes, and the complete rerun passes recovery and the new
  minimap checks, then reports native opens plus `reload launcher result=4`.
  The reload-launcher failure's cause is not established here; it is left
  unchanged and reported for the coordinator (`editor-test-rerun.log`).

`tools/test_replay_cli.sh` and the display-lock contract pass (M: observed
successful shell exits). The latter prints `display lock contract: owner PID,
alias/timeout, make/scripts, nested inheritance, exit status PASS`; its
intentional timeout probes are retained in `display-lock-contract.log`. All failures listed above remain visible in the logs; no
suite was rewritten to skip an assertion. Leak-on validation, native
backend readback and the comparison drive remain coordinator work.

## Remaining coordinator verification

The sandbox denies the X socket. Re-run the complete sanitizer suite with
LeakSanitizer enabled and Xvfb at the supplied display `:99`, plus:

```sh
DISPLAY=:99 build/san/tests/editor_test --minimap-raster
DISPLAY=:99 build/san/tests/editor_test --minimap-gl
```

The explicit GL probe requires an EGL/DRI3-capable display; an unavailable
context cannot establish the GL readback result. Re-run the Astra minimap
comparison against `build/astra/video/13-minimap-sublime.mkv` on the same built
editor. The raw Astra video/log/trace directory is absent from this worktree;
the checked-in comparison report was read. The headless measurements do not
establish that displayed latency matches Sublime.

## Outside scope, left unchanged

The brief says glyph lookup landed first, but this worktree's chrome glyph
lookup still has its ASCII replacement path and its editor atlas binding is
ASCII-only. There was also no existing GL raster pixel-region upload adapter
in this tree; the region uploader above is additive and uses the existing
work mailbox/present flow. The existing synchronous minimap summary-fill
policy remains: ready fills can read the source, while the new pixel renderer
and pending retained composition do not. These discrepancies are reported
without changing glyph lookup or indexing policy.
