# edit-457.8 — hot exit session integration

Implemented the remaining session journal integration in this worktree. No `bd`
or `git` commands were run. This report also covers the marker-loss case named
edit-9ma. The native CLI trial is built but awaits the coordinator's permitted
Xvfb run; this is not a claim that the complete display acceptance passed.

## Changes

- `src/journal/journal.h`, `src/journal/journal.c`: full VIEW encoding/decoding,
  validation of its flag bits, and legacy VIEW compatibility. The full payload
  is (G, schema) `(four legacy fields + four additional fields) * sizeof(uint64_t)
  = eight * eight = 64 bytes`; the existing legacy payload remains (G, schema)
  `four * eight = 32 bytes`. Added fields preserve preferred column, logical
  first line, wrapped visual byte, wrap mode, approximation and cursor affinity.
- `src/editor/checkpoint.c`, `src/editor/session.c`, `src/editor/private.h`:
  ordinary records, failure checkpoints and worker save captures share that
  encoding. Retain raw window pixels separately from whole-cell grid geometry.
  Keep staged text before metadata, preserve the save controller's journal
  lease, coalesce against accepted state, and park later input while refused
  metadata awaits existing durability credit. Failed startup before BASE
  registration still leaves an empty placeholder.
- `src/editor/editor.c`, `src/editor/buffers.c`: journal applied input/view
  changes and successful cursor/tab operations before waiting for submission.
  The existing frame fence remains. Layout-only continuation slices avoid the
  added full metadata walk; no timer, extra frame, foreground sync or typing
  allocation was added.
- `src/editor/open.c`, `src/editor/recovery.c`: install recovered live order,
  active tab and full view states before native window/backend creation. Apply
  recovered dimensions before publishing the replacement checkpoint; create
  the window at its original pixel dimensions, including partial-cell edges.
- `tests/editor_hot_exit_test.c`: the bead's requested (G) `100` null-backend
  SIGKILL trials, each with a seeded kill point. Compare independent in-memory
  captures of tab text/order/active/dirty state, every view field and dimensions
  immediately after startup and after layout settles. Cover pointer reorder,
  close/reopen, inactive views, wrapped/unwrapped scrolling and selection,
  fractional window edges, immediate API changes and typing while an earlier
  Present is held. Crash boundaries are completed actions; no teardown or flush
  is allowed before SIGKILL. The existing journal kill suite also verifies
  complete-record recovery when a kill overlaps append progress.
- `tests/cli_test.c`: production CLI display trial sends a native key and resize,
  waits for the read-only journal image to contain the marker and view/window
  state, SIGKILLs the editor, relaunches, and verifies recovered text and actual
  X window geometry. Startup snapshot capture and waits are fixture-only; their
  watchdog reuses `EDITOR_IDLE_NS`, with no invented duration.
- `tests/journal_test.c`, `tests/editor_test.c`, `fuzz/journal_fuzz.c`: full VIEW
  roundtrip/rotation and malformed payload checks, updated checkpoint schema
  expectation, and legacy/full schema fuzz oracles including extended fields.
- Documentation: `docs/decisions/edit-457.8.md`, this report,
  `src/editor/STATUS.md`, `src/journal/STATUS.md`, `HANDOFF.md` and the existing
  `docs/worklog/2026-10-11.md`.

## Red run

The regression was written and run before production changes. The sanitizer
build completed, then the assertion below reproduced loss of startup geometry.
The default run additionally failed LeakSanitizer's process inspection; disabling
only leak detection reproduced the same assertion. ASan and UBSan remained on.

(M)[AC], power checked as `BAT0=Charging`. Source line numbers are source
locations in the red fixture. Trial labels derive from the test's loop index;
its kill point derives from the seeded LCG modulo the bead's trial count. For
this initial trial, the code computes
`((1103515245 + 12345) >> 16) % 100 = 38` (G, seeded fixture arithmetic, not a
performance bound).

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_hot_exit_test
editor_hot_exit_test:57: FAIL got.dims.cols == want->dims.cols && got.dims.rows == want->dims.rows
trial=0 kill_point=38
exit: 1
```

## Green run

(M)[AC], the completed trial count is the successful loop count against the
bead's requested (G) `100` trials. The final fixture includes the held-Present
case as well as wrapped/unwrapped views and half-cell window edges. Final fixture
review also added an explicit logical-ID assertion proving the pointer drag
actually reorders the tab; its coordinate accounts for the scrolled tab strip.
That strengthened fixture was rerun through the complete crash acceptance.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_hot_exit_test
editor_hot_exit_test: ok trials=100 (G: bead acceptance), seeded random typing kill points; text/tabs/order/active/cursor/selection/scroll/window restored before first frame
exit: 0
```

Existing save/recovery integration, same sanitizer configuration:

```text
editor_session_test: three launches leave one nonempty 0600 journal; failed restore removes empty destination and preserves source
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
```

Journal schema regression:

```text
journal_test: full VIEW roundtrip/rotation, legacy defaults, invalid lengths/flags passed
```

The release allocation guard is active; key count is the existing allocation
suite's script length and allocation count is its interposer counter (M)[AC].
No new count or allocation limit was introduced.

```text
$ EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
```

The gcc journal suite also passed with `malloc_guard=active append_allocations=0`
(M)[AC], derived from that suite's guarded append calls.

## Builds and complete suite audit

`make all` passed with gcc release and the required C11 warning flags.
`make check` was run as requested. Its default invocation stopped in `base_test`
when LeakSanitizer reported that it cannot run under this sandbox's ptrace
restriction. Re-running with `ASAN_OPTIONS=detect_leaks=0` retained clang
ASan/UBSan and stopped at the CLI's forbidden local socket setup:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

Every built sanitizer suite was then run individually so make's early exit
could not hide headless suites. Totals (M)[AC], counted from
[the complete status list](../../build/edit-457.8-evidence/suites.txt):
`67 executable statuses = 50 exit-success statuses + 17 environment-blocked
statuses`. The separately run final crash suite is included in these totals.
Native portions of passing `gl_test` and `gl_bench_test` explicitly SKIP;
their pure contract checks passed. Pure X11 input/alignment/guard checks and
synthetic scroll-display checks also passed; these are not native display
verification.

Display-dependent failures:

- `editor_close_test` (injected headless close passed; native open denied).
- `file_kill_test` (file-only checks passed; its viewport/queued cases open X).
- `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`.
- `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`,
  `x11_order_test`, `x11_stall_test`.

Local Unix-socket restrictions also block `cli_test`, `editor_large_test`,
`editor_p4_test`, and `ipc_test`. `editor_test` contains both display cases and
an IPC launcher case; those fail for the same environment restrictions. Direct
probes returned EPERM for AF_UNIX bind, connection to the X socket, and send on
a socketpair. Thus even the IPC suite that does not open a display cannot pass
inside this sandbox. The final editor suite's other checks passed, including
protected journal credit, session metadata, failure checkpoint contents and
rollback. The focused BASE-race regression is independently green.

The replay CLI shell contract and display-lock shell contract passed when run
individually. All observed failures are recorded without changing unrelated
modules or weakening their assertions.

Fuzz verification used a structural corpus (G, schema coverage):
`two VIEW schemas * two input boundary lengths * (one << three defined flag
bits) = 32 seed files`. The boundary lengths come from the existing session
fuzzer's minimum accepted input length and its successor. Running libFuzzer
with `-runs=0` executes the seed corpus without an invented time/run budget;
the final ASan/UBSan seed run passed. This is a seed smoke check, not a long
fuzz campaign.

## What remains

The coordinator must rebase the shared editor files as needed, run the complete
`make all` / `make check` with normal leak inspection and permitted local sockets
on Xvfb `:99`, and exercise the new `cli_test` native SIGKILL trial. Native GL
coverage remains unavailable here. The next Astra comparison drive on the same
build is still required before a Tobias handoff. Preserve recovery-before-window
initialization and the savectl lease behavior when resolving editor conflicts.

Recovery uses the existing journal contract: fully written complete records are
process-crash protected, while refused/error records retain pending state and
prevent acknowledgement of a containing frame. The crash oracle compares
completed actions; it does not claim that a kill in the middle of an unfinished
keypress must restore future, unapplied state or certify power-loss durability.

## Findings outside scope

The touched-module benches were run with their own power/load stamps. The
editor bench's null serial path passed structural/zero-allocation checks but
missed its tracked typing-latency row under the default large-file workload;
the next native row could not initialize X. No same-build Sublime timing
comparison was possible here, and no claim of matching its latency is made.
The journal bench reported correctness OK and a tracked append-tail miss,
but its existing default GATE mode still returns a nonzero exit on that miss,
contrary to the current tracking-only bench policy. Those behaviors were
reported and left unchanged. Their measured endpoints and existing goal
inputs are retained verbatim in the evidence logs, with no new limits.

The existing unwrapped view can retain an unused `visual_byte` beyond EOF after
selection deletions. Full recovery preserves that inert field for exact state
comparison; view normalization semantics were not changed.

Evidence under [build/edit-457.8-evidence](../../build/edit-457.8-evidence/): red,
green, build/check logs, per-suite output/statuses, release guards, seed fuzz,
shell contracts and stamped editor/journal benches. Design choices are in the
[decision record](../decisions/edit-457.8.md).
