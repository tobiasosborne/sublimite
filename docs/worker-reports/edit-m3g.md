# edit-m3g worker report

Implemented the assigned font-size bead in this worktree. No bd or git commands
were run. The release tree builds; native acceptance and the next Astra drive
remain with the coordinator because this sandbox cannot use X sockets.

## Changes and derivation

The fixed default was selected in `src/editor/open.c`: the baked pixel-height
atlas, fixed columns/rows, and a window derived only from cell counts. `main.c`
supplied no display sizing. `runtime.c` previously supplied display safety and
directory setup; `theme.h` and `backend.c` do not own font sizing.

Launch now reads Xft DPI from the root X resource database, with RANDR primary
output physical DPI as the fallback. EWMH work-area geometry supplies the window
size. Xft values take precedence over physical dimensions, so configured desktop
scale is respected. `EDIT_FONT_SIZE_PT=<points>` overrides the launch default.

The default is 10 pt (E: the bead's Sublime reference). The supplied panel gives
`hypot(2880,1800)/14 inches ~= 243 DPI` (P), and
`10 pt * 243 px/inch / 72 pt/inch = 33.75`, rounded to **34 em pixels** (P).
The em API uses units-per-em scaling, rather than the old pixel-height API.
Window policy is .60 of work width and .70 of work height (E: the bead's policy):
`2880*.60=1728`, `1800*.70=1260` pixels without work-area exclusions.

Ctrl+= / Ctrl++ and Ctrl+- change the size by one point (P: existing binding
arguments); Ctrl+0 restores the launch default, including an environment override.
The foreground worker rasterizes the embedded vendored font into prepaid atlas
storage. The UI authenticates the mailbox completion, retains the old atlas
through backend retirement, then updates cells/layout and redraws. Running jobs
coalesce to the latest requested size. There is no added frame timer or UI wait
for zoom preparation. The screenshot drive must establish actual parity with
Sublime; this report does not infer it from the unit tests.

Maximum atlas size derives from GL's existing referenced-pixel bound, and extreme
zoom-out respects CPU's existing cell-count bound. Native surfaces reserve work
area pixels independently of font size. Full numeric/resource derivations and
legacy explicit pixel-size compatibility are in
[the decision](../decisions/edit-m3g.md).

Changed files:

- `src/main.c`.
- `src/editor/{LDLIBS,display_size.c,font_size.c,editor.c,editor.h,input.c,open.c,private.h,runtime.h}`.
- `src/font/{font.c,font.h,embedded.c,embedded.inc}`.
- `src/keys/{keys.c,keys.h}`.
- `tests/{font_size_test.c,keys_test.c,x11_font_size_test.c}`.
- `fuzz/font_size_fuzz.c`, `tools/embed_font.py`.
- `src/editor/STATUS.md`, `src/font/STATUS.md`, `HANDOFF.md`,
  `docs/worklog/2026-10-11.md`, `docs/decisions/edit-m3g.md`, and this report.

## Red run

The headless test was written and executed before production changes. It used
the existing baked size and required Ctrl+= to grow the cell height. The log's
line numbers are source positions from that test revision (M)[bat]; power was
read as `Discharging` before the run.

```sh
make build/san/tests/font_size_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_size_test
```

```text
font_size_test:28: FAIL editor_grid(e)->dims.cell_h > before.cell_h
```

The test's FAIL macro returns failure on the unchanged cell height. The exact
initial build and run are retained in
[red build](../../build/edit-m3g/red-build.log) and
[red output](../../build/edit-m3g/red.log).

An additional red regression caught an atlas larger than GL's existing coverage
bound (G: `GL_FRAME_PIXEL_LIMIT`, not a new budget):

```text
font_size_test:97: FAIL e->font.capacity <= UINT64_C(262144)
font_size_test:153: FAIL display_default() == 0
font_size_test: zoom changes cells and reset restores default; document unchanged
```

[Capacity red output](../../build/edit-m3g/red-cap.log).

A minimum-size regression also ran red before correcting the lower bound to
the font API's smallest positive whole em (P: `72/DPI` points maps to one pixel):

```text
font_size_test:78: FAIL editor_runtime_em_px(96.0, 72.0 / 96.0) == 1u
font_size_test:154: FAIL display_default() == 0
font_size_test: zoom changes cells and reset restores default; document unchanged
```

[Minimum red output](../../build/edit-m3g/red-min.log). Its source positions and
failure output are from that test run (M)[bat]. The final green run includes this
assertion and the complete zoom-down capacity sweep.

## Green run

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/font_size_test
build/tests/font_size_test
```

Sanitizer output (M)[bat], `Discharging`; exit status is the test's return value:

```text
font_size_test: zoom changes cells and reset restores default; document unchanged
font_size_test: display DPI to em, Xft precedence, work-area geometry, worker zoom and allocation guard passed
font_size_test: retained frame atlas, coalesced reset and outstanding-job shutdown passed
font_size_test: zoom bounds respect prepaid CPU/GL frame capacities passed
sanitizer font-size exit=0
```

Release output (M)[bat], with the counting allocator active:

```text
font_size_test: zoom changes cells and reset restores default; document unchanged
font_size_test: display DPI to em, Xft precedence, work-area geometry, worker zoom and allocation guard passed
font_size_test: retained frame atlas, coalesced reset and outstanding-job shutdown passed
font_size_test: zoom bounds respect prepaid CPU/GL frame capacities passed
release font-size exit=0
```

The suite covers supplied DPI to em conversion, resource precedence and invalid
values, physical millimetres, work-area fractions, actual cell width/height changes,
point reset, unchanged document content, worker-thread rasterization, retained
atlas lifetime during a held frame, coalesced reset, shutdown with a pending job,
and renderer capacity bounds. The allocation assertions use law 2's zero heap
allocation requirement (G), across zoom ingress through layout/submit.

Fixed parser seed replay also passed ASan/UBSan:
[output](../../build/edit-m3g/fuzz-seeds.log). This executed the supplied resource,
physical-size, nonfinite and extreme-dimension fixtures; it was a seed replay,
not a timed fuzz campaign.

## Build and suite verification

`make all` passed with gcc release and the required C11 warning flags:
[final build log](../../build/edit-m3g/make-all-final.log).

`make check` was run as required with clang ASan/UBSan. Its first execution
stopped in CLI IPC setup:
[initial full check](../../build/edit-m3g/make-check.log).
The final execution stopped at the occupied display lock:
[final full check](../../build/edit-m3g/make-check-final.log).
The final invocation selected immediate failure for an occupied display lock
(`EDIT_DISPLAY_LOCK_TIMEOUT=0`, meaning no wait); it did not replace the suite
results. Leak detection was disabled following this tree's documented sandbox
ptrace restriction; the coordinator must rerun with leaks enabled.

Every suite was additionally attempted individually on the safe display, continuing
after failures so later headless suites were reached. Exact return values are in
[the suite manifest](../../build/edit-m3g/suites.tsv), with per-suite logs in
`build/edit-m3g/suites/`. The final outcomes are recorded below.

| Outcome | Suites / evidence |
|---|---|
| Passed headless/pure checks | All remaining suites in the manifest, including font sizing, font, runtime, keys, layout, line index, journal, piece, view, work, and the standalone replay CLI contract. |
| Display initialization failed | [editor_close_test](../../build/edit-m3g/suites/editor_close_test.log), [file_kill_test](../../build/edit-m3g/suites/file_kill_test.log), [raster_test](../../build/edit-m3g/suites/raster_test.log), [refproto_test](../../build/edit-m3g/suites/refproto_test.log), [refwin_test](../../build/edit-m3g/suites/refwin_test.log), [refwin_timeout_test](../../build/edit-m3g/suites/refwin_timeout_test.log), [x11_clip_review_test](../../build/edit-m3g/suites/x11_clip_review_test.log), [x11_clip_test](../../build/edit-m3g/suites/x11_clip_test.log), [x11_font_size_test](../../build/edit-m3g/suites/x11_font_size_test.log), [x11_identity_test](../../build/edit-m3g/suites/x11_identity_test.log), [x11_live_test](../../build/edit-m3g/suites/x11_live_test.log), [x11_order_test](../../build/edit-m3g/suites/x11_order_test.log), [x11_stall_test](../../build/edit-m3g/suites/x11_stall_test.log) |
| IPC setup denied | [cli_test](../../build/edit-m3g/suites/cli_test.log), [editor_large_test](../../build/edit-m3g/suites/editor_large_test.log), [editor_p4_test](../../build/edit-m3g/suites/editor_p4_test.log), [ipc_test](../../build/edit-m3g/suites/ipc_test.log) |
| Mixed editor suite failed | [editor_test](../../build/edit-m3g/suites/editor_test.log): native editor-open failures and a launcher IPC_IO result. Its default review aggregate stopped before the later native/typing matrix; the separate release allocation regression was run successfully. |
| GL native checks skipped | `gl_gate_test` and `gl_test` returned success for their pure contracts; `gl_test` explicitly reports native diagnostics SKIP. |

The non-display IPC suite is the explicit exception to the requested headless
pass condition: its failing bind is sandbox-denied, independently reproduced by
the socket probe. It remains for coordinator rerun, rather than a claimed pass.
No sanitizer findings appeared in the successful headless suites.


## Display acceptance and ink height

The direct native font-sizing test could not open the existing display:
[native output](../../build/edit-m3g/native-display.log). The requested private
Xvfb attempt used the bead's panel geometry and DPI:

```sh
xvfb-run --server-num=98 --server-args='-screen 0 2880x1800x24 -dpi 243 -nolisten tcp' build/tests/x11_font_size_test
```

Here display/screen identifiers are test endpoints; resolution/DPI are the
supplied panel inputs, and depth is `RGB channels * bits/channel = 3*8 = 24`
(P: X pixel format). Xvfb failed to establish listening sockets:
[server log](../../build/edit-m3g/xvfb.log). No after screenshot was obtainable.

The before result is inherited from [Astra's font row](../reviews/astra-vs-sublime-1.md):
reported ink rows `17..26`, hence `26-17+1 = 10 px` (M, historical Astra evidence;
that row did not carry its own power stamp, so no new power tag is invented).
It used the text “tatic”, excluded the caret, and thresholded max RGB above 130
(E: Astra's documented detection method). The changed geometry makes the old ROI
inapplicable; the coordinator should choose an equivalent tight ROI around the
same letters, move/exclude the caret, apply the same threshold, and record
`last_ink_row - first_ink_row + 1` with DPI, window geometry and power stamp.
**After ink height remains unmeasured; 34 is em size, not an ink-height claim.**

Coordinator verification:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/x11_font_size_test
DISPLAY=:99 EDIT_DISPLAY=:99 make check
```

Use configured DPI 243 for the panel-default check, or the actual output on the
coordinator-authorized real display. Ordinary Xvfb at 96 DPI should instead give
`round(10*96/72)=13` em pixels (P), so its unconfigured result cannot certify the
physical-panel default. Re-run the Astra comparison on the same build, including
zoom/reset and typing; record the measured before/after ink and frame behavior.

## Remaining and outside scope

Native X/GL font adoption, real screenshots, leak-enabled full check and the
next Astra drive remain unverified. Persistent `font.size` belongs to the config
bead. Unicode runtime cache/fallback integration remains with its existing bead;
it should adopt the same true-em scale. Runtime DPI changes after moving between
outputs are outside this startup sizing scope.

The independent IPC suite cannot pass here: AF_UNIX bind is denied with EPERM,
confirmed with a fresh socket inside a writable temporary directory:
[socket probe](../../build/edit-m3g/socket-probe.log). CLI/editor IPC setup and an
editor launcher-wait check also fail. These are reported without source fixes.

The once-only module benchmarks were stamped `Discharging` and loaded:
[power/load capture](../../build/edit-m3g/bench-power.log).
Font's existing long-cluster extension timing row missed its tracked target and
returned failure; its raster/cached allocation guards passed:
[font benchmark](../../build/edit-m3g/font-bench.log). The editor benchmark's null
serial row completed, with its existing tracked latency miss; the native row
could not open the display:
[editor benchmark](../../build/edit-m3g/editor-bench.log).
Benchmark sample counts, percentile arithmetic, gates and raw durations in those
logs come from the existing benchmark/harness fixtures, not worker-created budgets.

The existing release typing allocation regression passed:
[allocation output](../../build/edit-m3g/editor-allocation.log).
Its workload count comes from `allocation_test` in `tests/editor_test.c`; the
required allocation count is law 2's zero (G). Existing editor review diagnostics
print hardcoded AC tags even though the worker observed `Discharging`; this
labeling discrepancy is reported without editing the unrelated diagnostics.

## Post-rebase continuation — 2026-10-11

Reproduced the requested whole-suite invocation:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test
```

Its first failure here was the sandbox-denied launcher IPC at line 1031,
followed by native-open failures at lines 422 and 448. The requested null
regression also reproduced, before the review aggregate returned failure:

```text
editor_test:64: FAIL false
editor_test:770: FAIL settle(e) == 0
editor_test:2000: FAIL review_suite(NULL) == 0
```

The actual timeout was inside `review_replay_slices`' four batches of 1024
typed characters, rather than its initial empty frame. Temporary diagnostics
showed queued input still making progress, dirty/incomplete layout, and font
state `requested_px=applied_px=15`, `handle.epoch=0`, `received=false`.
There was no outstanding atlas job or font completion to drain. CPU stack
sampling located the repeated work in `layout_visual_row_fresh`,
`layout_wrap_run`, `layout_refill` and `piece_read`.

**Cause:** `scan_row` in `src/layout/wrap.c` called `reset_work` between every
soft-wrapped query row. That reset discarded the query's source window.
Each row then recopied the following source bytes into another window,
although the tree is immutable for the duration of this UI-owned query.
Repeated fresh cursor queries during typing multiplied those overlapping
reads. The five-second settle deadline expired with hundreds of input events
still queued; this was excessive work rather than a lost worker notification.

**Fix:** reset the local query's row/phase/cluster continuation while preserving
its input window between adjacent rows. `position` already retains or refills
that window according to the next row's byte position, including word-wrap
rewinds. Each new query still starts with an empty window and reads the current
tree; fresh queries still ignore stale published row/checkpoint descriptors.
No deadline, font policy, worker/epoll protocol, public API, allocation or typing
reservation changed. Temporary test diagnostics were removed.

The same four-batch diagnostic, using the existing five-second deadline,
previously timed out in batch four at length 3478 with 618 events queued.
After the fix it reached length 4096 and settled batch four in 1079525123 ns.
These are loaded-host correctness diagnostics, not benchmark acceptance
measurements. Raw before/after outputs are
[before](../../build/edit-m3g/probe-batches.log) and
[after](../../build/edit-m3g/probe-batches-green.log).

Final verification:

| Command / result | Evidence |
|---|---|
| `make all`: exit 0; C11 `-Wall -Wextra -Werror -Wshadow -Wconversion` | [build](../../build/edit-m3g/make-all-rebase.log) |
| Whole `ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test`: completed, exit 1 solely from launcher/native-open failures; all headless review cases passed and no line-64/770 failure | [whole suite](../../build/edit-m3g/editor-whole-rebase.log) |
| Sanitizer `EDITOR_REVIEW_ONLY=11-replay`: exit 0 | [focused replay](../../build/edit-m3g/replay-final.log) |
| Sanitizer and release `font_size_test`: exit 0 | [sanitizer](../../build/edit-m3g/font-size-final.log), [release](../../build/edit-m3g/font-size-release.log) |
| Sanitizer `font_test`: exit 0 | [font](../../build/edit-m3g/font-san.log) |
| Sanitizer and release `wrap_test`: exit 0, including cursor, CRLF, Unicode/wide navigation, word-wrap, resize and allocation checks | [sanitizer](../../build/edit-m3g/wrap-san.log), [release](../../build/edit-m3g/wrap-release.log) |
| Release `EDITOR_ALLOC_ONLY=null build/tests/editor_test`: exit 0, 10000 keys, mallocs=0, guard active | [typing allocation](../../build/edit-m3g/typing-alloc-release.log) |
| Sanitizer `editor_p4_test`: exit 1 at line 540, IPC endpoint initialization denied before the raster scenario | [P4](../../build/edit-m3g/editor-p4-rebase.log) |
| Sanitizer `x11_font_size_test`: exit 1 at display initialization | [native font](../../build/edit-m3g/x11-font-rebase.log) |

Whole-suite remaining failure output:

```text
reload launcher result=4
editor_test:1031: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:422: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:448: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:730: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1386: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2000: FAIL review_suite(NULL) == 0
```

The same whole run prints successful multi-record replay, zero-growth replay
recycling, mapped watch/focus/source recovery, sleeping journal workers, and
all 22 injected allocation-failure rollback cases. Its 2000 aggregate failure
therefore remains a sandbox/native result, rather than the reproduced settle
regression. This checkout has standalone `font_size_test`/`font_test` and
`x11_font_size_test`; its editor test defines no `--font-*` modes.

The coordinator must rerun the raster/display `editor_p4_test` line-432
scenario and native font sizing on its authorized display. This sandbox cannot
reach that scenario, so the P4 timeout's cause or resolution is not claimed.
No `git` or `bd` command was run during this continuation.
