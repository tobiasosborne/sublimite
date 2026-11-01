# edit-177r.30-s28 worker report

Verdict: **partial verification**. All accepted editor findings are implemented
and their final release/sanitizer regressions pass. Linux builds/checks pass.
The full Windows check fails only in the unchanged journal test described below;
that failure also reproduces in isolation and needs coordinator triage.
Decision: [edit-177r.30](../decisions/edit-177r.30.md).

## Changes

- `src/win32/win32_clip_win.c`, `private.h`: reads and publications share the
  monotonic native operation ID space; added identified read submission.
- `src/editor/clip.c`: read deadline from admission, wait-set participation,
  terminal ID matching, expiry notice, and late-result rejection. Reads queued
  behind an expired publication expire independently. Detached worker lifetime
  remains responsible for native resources.
- `src/editor/editor.c`: a close-owned Save As prompt cancels the window-close
  boundary and reopens admission until the filename is answered.
- `src/editor/session.c`: an unshown Windows notice can compose after queued
  editing. `editor.c` acknowledges its first accepted CPU snapshot before
  applying the existing next-mutation lifetime; refused submission retains it.
- `src/editor/grid_win.c`, `font_size.c`, `private.h`, `open.c`: grow UI and backend
  grid reservations during font preparation before adoption, with quiescent old
  readers and lifecycle transfer. No allocation on ordinary typing/resize.
- `src/x11/plat.h`: clarify the operation-ID comment; no layout or token change.
- `tests/win/editor_win_test.c`: native read gate, queued read/close regression,
  late-read identity isolation, repeated window close during Save As (save and
  cancel), status-cell notice assertion, smaller-font column/row growth, reuse,
  and allocation-free typing after growth. Cases are included in the default
  release/sanitizer campaign.

## Red / green

All commands use the brief's (E) assigned `taskset -c 32-47 nice -n 10`; make uses
`-j16` (the assigned range's CPU count). Windows execution uses only
`tools/winrun.sh` on a private station. No live desktop or shared clipboard runs.

Build the initial tests before production edits:

```
taskset -c 32-47 nice -n 10 make -j16 TARGET=win build/win/rel/tests/editor_win_test.exe
taskset -c 32-47 nice -n 10 make -j16 TARGET=win build/win/tools/win_quiet_run.exe
```

Both successful builds exit 0. Run each selector with:

```
taskset -c 32-47 nice -n 10 tools/winrun.sh build/win/rel/tests/editor_win_test.exe <selector>
```

| Selector | Red exit and relevant assertion | Green exit and relevant output |
| --- | --- | --- |
| `--read-stuck` | 1: `FAIL editor_clip_busy(e) && editor_clip_deadline(e)>os_now_ns()` | 0: `foreign read expires with exact text, notice, and late-result isolation PASS` |
| `--read-queue` | 1: same missing read deadline | 0: `queued read expires with exact text, notice, and late-result isolation PASS` |
| `--saveas-close` | 1: `FAIL editor_step(e,0)!=EDITOR_CLOSED` | 0: `close Save As retains answer ownership, cancel completes PASS` |
| `--notice` | 1: `FAIL e->recovery_notice_shown` | 0: `queued typing cannot hide an unshown timeout notice PASS` |
| `--font-small` | 1: `FAIL e->grid.dims.cols==e->cfg.max_window_width/e->grid.dims.cell_w` | 0: `cols=731 rows=102 cell=7x14 PASS`; row growth/reuse and zero-allocation typing PASS |

The save-completion extension `--saveas-close-save` also exits 0 and verifies exact
saved disk bytes after repeated window close. The read fixture expires a first
read, admits another while it is still blocked, then releases both and verifies
that only the current operation inserts. Its queued variant closes and tears down
while the original publication stays blocked. The native gates inherit the
suite's existing WATCH_NS watchdog; no new timing gate is introduced.

The strengthened notice regression first refused native CPU snapshot admission:
`--notice` then exited 1 at `FAIL !e->recovery_notice_shown` (log
`notice-submit-red.log`). After deferring the shown acknowledgment until accepted
submission, the same command exited 0 with `queued typing cannot hide an unshown
timeout notice PASS` (`notice-submit-green.log`). It also mutates again while
submission is refused, then checks the actual status cells on the accepted frame
and clears the notice on the subsequent mutation. A standalone `--read-close`
extension covers detached teardown with the read itself blocked. The cancelled
Save As case additionally reopens its journal and checks exact recovered bytes.

Logs: `build/edit-177r.30/red--*.log`, `green--*.log`, and build logs in the same
worktree-local directory. A fixture compile typo was corrected before the red
runs. An initial run lacked the quiet runner and refused execution; after building
it, sandbox WSL interop sockets were denied. The successful isolated runs used
the automatically approved sandbox escalation. No isolation fallback was used.

## Verification

- `taskset -c 32-47 nice -n 10 make -j16 all`: **exit 0**.
- `taskset -c 32-47 nice -n 10 env DEBUGINFOD_URLS= make -j16 check`: **exit 0**;
  private Xvfb, complete sanitizer suite and auxiliary CLI/display-lock checks.
  The initial sandboxed invocation exited 2 because Xvfb could not create its
  local sockets (`linux-check.sandbox.log`); approved execution passed.
  Final source rerun also exits 0 (`linux-check-final.log`), as does the final
  release build (`make-all-final.log`).
- `taskset -c 32-47 nice -n 10 env WIN_CHECK_JOBS=4 make -j16 TARGET=win check`:
  initial full run exits 2. Its only failing suite is the unchanged sanitizer
  `journal_kill_test`, at `FAIL reached && st.how==OS_PROC_ABNORMAL native=258`.
  This is its random-writer progress/kill assertion under the inherited
  watchdog, not an editor assertion. Both editor configurations passed. The run
  overlapped Linux build/check work; causation has not been established.
  Original output: `windows-check-initial.log` and
  `journal-kill-initial-failure.log`. The exact failing executable also exits 1
  with that same assertion when run alone:
  `taskset -c 32-47 nice -n 10 tools/winrun.sh
  build/win/san/tests/journal_kill_test.exe` (`journal-kill-isolated.log`).
  Symbol inspection confirms no changed editor/clipboard functions are linked
  into that executable (`journal-kill-symbols.log`). No journal test code or
  timing bound was changed. After the last Windows notice-submission refinement,
  the complete editor release/sanitizer campaign is rerun separately; unrelated
  suites are not repeated after the isolated failure reproduces.
- Final Windows editor campaign: **release exit 0, sanitizer exit 0**. Commands:
  `taskset -c 32-47 nice -n 10 tools/winrun.sh
  build/win/rel/tests/editor_win_test.exe`, followed serially by the same command
  for `build/win/san/tests/editor_win_test.exe`. Logs:
  `final-editor-rel.log`, `final-editor-san.log`. Both contain all new regression
  PASS lines, including refused status submission, read-only blocked teardown,
  exact cancelled-Save-As journal recovery, and font row growth/reuse.
- Final Windows builds: **exit 0**, `taskset -c 32-47 nice -n 10 make -j16
  TARGET=win all build/win/san/sublimite.exe` (`windows-build-final.log`).
- Linux preprocessed token comparison: **exit 0**, unchanged `clip.c`, `editor.c`,
  `session.c`, `open.c`, `font_size.c`, `editor/private.h`, `x11/plat.h`, using `gcc -std=c11 -D_GNU_SOURCE -pthread
  -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND
  -DEDIT_HAVE_WAYLAND_SERVER -E -P`. Baselines were captured before edits, then
  rechecked against `git show HEAD`, mapping temporary baseline filenames back
  with `-fmacro-prefix-map` so assertion `__FILE__` diagnostics match. The
  whitespace-separated token streams match. Reproducer:
  `taskset -c 32-47 nice -n 10 python3 build/edit-177r.30/linux_tokens.py`;
  per-file hashes in `build/edit-177r.30/linux-tokens.log`.
  The new `grid_win.c` is guarded whole by `_WIN32`; new private fields and calls
  are also guarded. Linux notice behavior is unchanged.
- `git diff --check`: exit 0.

Measurements are [AC, desktop Spiral], no battery; Tobias is using the box.
(M) `uptime` before full verification: `load average: 0.31, 0.62, 0.93`.
Memory formulas and the measured ABI/reservation output are in the decision doc;
these are storage/accounting derivations, not performance gates.

(M) During the final Windows editor campaign, `uptime` reports
`load average: 0.53, 2.74, 4.63`. The release campaign uses the inherited Linux
allocation-case key count and reports 10,000 keys, exact journal/mutation
accounting, active guard and zero mallocs. Its occluded CPU-submit p50/p99 are
124,100/359,800 ns; sanitizer p50/p99 are 418,200/808,500 ns with the allocator
guard explicitly ASan-inert. Derivation is each `submit_ns-ingress_ns`, using
nearest rank `ceil(N*pct/100)` over the inherited N. These are [AC, desktop Spiral]
CPU-submit observations, not native T4/T6 or displayed latency claims. Required
live cases remain explicit SKIPs.

The tracked editor benchmark also exits 0 on a private Xvfb, using the checked-in
`bench/editor_bench.args` arguments:
`taskset -c 32-47 nice -n 10 env DEBUGINFOD_URLS= xvfb-run -a -s '-screen 0
1280x1024x24 -nolisten tcp' tools/with_display_lock.sh build/bench/editor_bench
--track --no-idle --no-p4 --serial-only --keys=64`. The key count is the inherited
benchmark smoke count. Its built-in battery reader prints unknown on this
no-battery desktop; all these measurements are [AC, desktop Spiral], Tobias using
the box. (M) Benchmark load1=17.87, from its own stamp. Requested GL falls back to
CPU raster on Xvfb. All completed keys have zero allocations and exact mutation/
journal accounting. This is TRACK, not a native display latency claim. Full output
is `build/edit-177r.30/editor-bench.log`.

## Found, not fixed

Linux's corresponding initial-window grid limit remains. For the brief's display
and default font example, (P) 384 startup columns at 8 pixels shrink to a
2688-pixel coverage at 7 pixels (`384*7`); see decision arithmetic. Live displayed
frame behavior remains the coordinator's explicit check-live work.

The existing sanitizer journal random-writer progress/kill assertion prevents a
green full Windows check, including in isolation. This needs coordinator triage
outside the accepted editor scope. The Windows full-check output reports one
failing suite; all other suite results passed. The failure is preserved above.

## Open questions

No implementation question for the accepted findings. Coordinator triage is
needed for the unchanged journal test failure; no journal change was made here.
