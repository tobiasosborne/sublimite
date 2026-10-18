# edit-63p worker report (s8)

## Change

`editor_scroll_slice` refreshes the extent of a completed READY top with a
certified exact byte/label pair. Prefix publication can reuse that top if the
pure EOF policy keeps it READY, so retirement of the old window stays
opportunistic and never sets presentation pending for this case. Approximate
publication, a changed EOF top, source ownership, and the submit guard retain
their existing paths. No allocator or failure path was added.

The cancellation fixture deterministically dispatches the key, closes its
obsolete worker window, then publishes the edited index chunk before the next
scroll/submit turn. It requires a frame on that first turn, retains the BLOCKED
diagnostic, checks redraw while held, and compares all prepared text cells
after physical retirement and full redraw. It never skips this deterministic
case on the tracked timeout branch.

## Files touched

- `src/editor/scrollctl.c`
- `tests/editor_scroll_test.c`
- `docs/decisions/edit-63p.md`
- `docs/worker-reports/edit-edit-63p-s8.md`

Generated build output, verification logs, and the display-run wrapper are in
`build/edit-63p/`. No `bd` or `git` command was run. Protected documents and
other checkouts were untouched.

## Evidence derivation

Numeric test diagnostics below are verbatim (M)[AC] observations; BAT0 reported
`Not charging`, with raw `uptime` and disk stamps alongside each run. The red
elapsed milliseconds are the existing helper's monotonic elapsed nanoseconds
divided by the nanoseconds per millisecond (P). State flags, revision counters,
prefix counts and exit codes describe the observed state (M)[AC], not budgets.
The first-turn check is (P): the next editor scroll/layout/submit opportunity
after dispatch. PROBE_RETRIES retains the inherited fixture bound (E), and the
repeat count and display-lock allowance are reused directly from the brief
(E); no new duration, retry count, or latency budget was introduced.

## Red

Built the changed test before editing production code:
`make -j3 build/san/tests/editor_scroll_test` exited `0` on both the initial
build and final test-only rebuild (M: observed build statuses).
`red-build.log` and `red-rebuild.log` retain the
compiler commands.

Run: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/editor_scroll_test`
(the wrapper removes the private runtime directory on exit).
From `build/edit-63p/red-untraced.log`:

```text
Not charging
 10:29:40 up 6 days, 15:17,  1 user,  load average: 44.09, 47.98, 48.14
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
waiting for display :99 (held by 2305245)
editor_scroll_test:185: FAIL rc == 0 && (!edit_malloc_guard_active() || allocations == 0)
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: edit-08u cancellation keystroke frame not out after retries=10000: BLOCKED behind the held scroll window (real failure) elapsed=438.770 ms frames=1 baseline=1 render_active=0 dirty=1 scroll_waiting=1 closing=1 scroll_prefix=0 built_prefix=1 scroll_revision=0 revision=1 allocations=0 (M) power=Not charging load1=43.68
Not charging
 10:29:43 up 6 days, 15:17,  1 user,  load average: 43.68, 47.83, 48.09
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
exit=1
```

The initial sandbox attempt (`red.log`) also failed, but LeakSanitizer reported
that ptrace prevents leak checking and buffered stdout was lost. `xdpyinfo`
exited `1` because it could not connect to the private display inside that
sandbox (M: observed probe status). Required test
runs therefore used approved untraced execution, which resolved these
environment restrictions; leak detection remained enabled.

## Green regression

Built with the Makefile's check-style sanitizer recipes:
`make -j3 build/san/tests/editor_scroll_test build/san/tests/editor_test build/san/tests/editor_large_test`
exited successfully (`green-build.log`).

The requested scroll repetitions all exited successfully under the same
display-lock command as red, with leak detection enabled. Each printed:

```text
editor_scroll_test: edit-63p prefix-growth key submitted turns=1
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
```

Raw end stamps from `green-scroll-*.log` (M)[AC], in run order:

```text
Not charging
 10:30:40 up 6 days, 15:18,  1 user,  load average: 47.46, 48.02, 48.13
exit=0
Not charging
 10:30:59 up 6 days, 15:19,  1 user,  load average: 48.50, 48.20, 48.19
exit=0
Not charging
 10:31:06 up 6 days, 15:19,  1 user,  load average: 48.78, 48.27, 48.21
exit=0
Not charging
 10:31:12 up 6 days, 15:19,  1 user,  load average: 49.75, 48.48, 48.28
exit=0
Not charging
 10:31:18 up 6 days, 15:19,  1 user,  load average: 51.61, 48.92, 48.43
exit=0
```

## Broader verification and remaining work

- `make -j3 all`: exit `0` (M: observed process status), gcc release with the
  required C11 warning flags and `-Werror`; `build/edit-63p/make-all.log`.
- `make -j3 build/san/tests/editor_scroll_test build/san/tests/editor_test build/san/tests/editor_large_test`:
  exit `0` (M: observed build status), clang ASan/UBSan check-style recipes;
  `build/edit-63p/green-build.log`.
- `build/san/tests/editor_scroll_test`, requested repetitions: every exit `0`
  (M)[AC], as above; `build/edit-63p/green-scroll-*.log`.
- `build/san/tests/editor_test`: exit `0` (M)[AC], full suite including native
  X11 translation/editor-loop/WM-close case; `build/edit-63p/green-editor.log`.
- `build/san/tests/editor_large_test`: exit `1` (M)[AC], wrapped near-end
  deadline assertion, detailed below.

Every test above ran as `sh tools/with_display_lock.sh <binary>` with private
`XDG_RUNTIME_DIR`, `DISPLAY=:99`, `ASAN_OPTIONS=detect_leaks=1`, and the brief's
`EDIT_DISPLAY_LOCK_TIMEOUT=1800` allowance. Successful sanitizer runs emitted
no ASan/UBSan/LeakSanitizer report. Full `make check` was not run; the named
sanitizer targets were built and executed explicitly as requested.

Sanitizer editor end stamp and result (M)[AC]:

```text
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
Not charging
 10:38:30 up 6 days, 15:26,  1 user,  load average: 50.00, 50.63, 49.32
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
exit=0
```

Release verification (M)[AC]:

- `DISPLAY=:99 ... sh tools/with_display_lock.sh build/tests/editor_scroll_test`:
  exit `0`; the deterministic key submitted on its first turn and the
  allocation guard was active (`build/edit-63p/release-scroll.log`).
- `DISPLAY=:99 ... sh tools/with_display_lock.sh build/tests/editor_test`:
  canceled while still queued on the contended display lock, exit `143`;
  the binary had not started (`build/edit-63p/release-editor.log`). Only this
  worktree's queued lock-wrapper process was terminated; the lock owner was
  left alone. `DISPLAY=:97 xdpyinfo` outside the sandbox exited `0`, so the
  full release suite moved to the brief-approved fallback.
- `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/tests/editor_test`:
  exit `0`, full native suite, active malloc guard and G1 typing regression
  green (`build/edit-63p/release-editor-97.log`). The wrapper again removed
  its private runtime directory. The key count printed below reuses the
  existing allocation fixture (E control), not a new count for this bead.

```text
editor_scroll_test: edit-63p prefix-growth key submitted turns=1
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=active PASS
Not charging
 10:42:39 up 6 days, 15:30,  1 user,  load average: 40.88, 46.13, 47.87
exit=0

editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
Not charging
 10:45:17 up 6 days, 15:33,  1 user,  load average: 38.73, 43.38, 46.56
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  389G   67G  86% /
exit=0
```

No implementation work remains in this bead. A successful full large-file
suite remains unverified because of the outside-scope failure below; the
coordinator should rerun that suite. No further review or outside-scope fix
was undertaken.

## Outside scope

The full sanitizer `editor_large_test` run exited with a wrapped near-end
deadline failure, without a sanitizer error. This was not investigated or
fixed. The new scroll code is skipped while the view is wrapped. Earlier
relative-motion, wrapped placement/publication, held-bulk follow, jump/page,
and unwrapped near-end cases in that run passed. Full output is in
`build/edit-63p/green-large.log`; the failing assertions were:

```text
editor_large_test:990: FAIL trace_now_ns() < deadline
editor_large_test:1727: FAIL near_end_repro(true) == 0
```

Power/load and `df -h /` before and after that suite (M)[AC]:

```text
Not charging
 10:31:30 up 6 days, 15:19,  1 user,  load average: 49.44, 48.55, 48.31
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
Not charging
 10:31:50 up 6 days, 15:19,  1 user,  load average: 50.00, 48.74, 48.38
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
exit=1
```

No new large fixture was introduced. The suite's existing sparse-fixture
construction and cleanup were unchanged, and no `build/editor-large-*`
fixture remained after the run. Existing external corpus fixtures were read
in place. Minimap and wheel implementations were not changed.
