# edit-ck4 — Ctrl+S and unsaved startup recovery

Implemented only the assigned save/startup bead in this worktree. No bd/git
commands, other-worktree edits, or other-worker implementation edits were made.

## Changes

- `src/editor/input.c`, `editor.c`, `buffers.c`, `checkpoint.c`, `open.c`,
  `editor.h`, `private.h`, and new `session.c` / `recovery.c`: route Ctrl+S to
  pinned immutable session snapshots and savectl bulk-worker preparation/write/
  finish; compose and acknowledge the actual Saving frame; preserve post-cutoff
  dirty history; reconcile shared targets; keep failure state per buffer.
  Setup failure, including an unavailable target parent, does not stop unrelated
  editing or prevent restoring the unsaved buffer. Small COPY attachment now
  waits for acquisition readiness, eliminating the prefix/completion race exposed
  by repeated opens. Large-file preview and paint implementations are untouched.
- `src/savectl/savectl.c`, `savectl.h`: worker-prepared CURRENT finish requests,
  shared-target baseline adoption and a test durability-step hook. UI journal
  append/pump/receive are excluded while the controller owns its journal lease.
- `src/journal/journal.c`, `journal.h`: an existing-journal checkpoint constructor
  preserves its prior inode and refuses append/pump until complete atomic
  CURRENT publication. Legacy empty-journal open behavior remains covered.
- `tests/editor_test.c`: save/startup probes are default; startup uses a headless
  abrupt-exit harness. Save asserts exact disk/buffer equality, cleared dirty
  state and a submitted Saving acknowledgement.
- New `tests/editor_session_test.c`: named/new/repeated saves, process-wide
  allocation guard isolated from legitimate worker allocation, post-cutoff edits,
  shared targets, inactive failure, unavailable target parents and their recovery,
  busy-backend single-frame acknowledgement, pre/post-replacement crash recovery,
  and reopening an explicit existing journal.
- Editor/savectl/journal STATUS, `docs/decisions/edit-ck4.md`, HANDOFF and the
  dated worklog are updated; this report is the acceptance/evidence record.

Startup restores associations, unsaved content, views and live tab order/active
selection before text submission. Sidecar locks survive journal inode replacement
and exclude live sessions. A recovered CURRENT checkpoint is durable before old
orphan artifacts are retired. Interrupted PREPARED saves remain dirty even if
replacement bytes already match disk. Conflict/failure preserves recovery sources.

## Evidence conventions

All commands below ran here. Power read from BAT0 was Discharging: current
measurements are (M)[bat]. Source-line values in failure transcripts are emitted
file-location indices; exit codes are process status values. Review/trace/phase
identifiers and wire widths come from their existing contracts (G). Marker sizes
are `strlen`/`sizeof` derivations in the tests. No new latency budget, deadline,
or sample population was invented. Raw inherited test output sometimes prints
its old hardcoded AC label; that is not a fresh AC measurement.

## Red, before implementation

The save/startup probes were made default first. The native startup probe was
replaced with a null-backend process-exit/relaunch oracle so it can fail on
recovery behavior inside this sandbox.

```
$ make build/tests/editor_test
$ build/tests/editor_test --ck4-save
editor_test:1706: FAIL !memcmp(saved, "xa", 2)
editor_test:1725: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
$ build/tests/editor_test --ck4-startup
editor_test:1763: FAIL editor_read(e, 0, bytes, sizeof bytes) == 0
editor_test:1770: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
```

Additional failing tests were run before their fixes:

```
$ build/tests/editor_session_test  # explicit existing journal
editor_session_test:229: FAIL editor_open(&e, &config, &backend) == 0
editor_session_test:241: FAIL existing_journal() == 0
$ build/tests/editor_session_test  # unavailable target parent
editor_session_test:215: FAIL editor_add_buffer(f.e, missing, NULL, 0, &id) == 0
editor_session_test:251: FAIL unavailable_target_parent() == 0
$ build/tests/editor_session_test  # recover that unsaved association
editor_session_test:222: FAIL editor_open(&f.e, &config, &f.backend) == 0
editor_session_test:258: FAIL unavailable_target_parent() == 0
```

## Green

Final-source runs (M)[bat]; exit statuses derive from each process, and the
release allocation count derives from the active counting interposer:

```
$ build/tests/editor_session_test
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_session_test
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --ck4-save
P4-3 section 3: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --ck4-startup
ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed
$ EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
$ ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=p4-40 build/san/tests/editor_test
P4-3 40: cursor/text frames omit unchanged final tab chrome passed
```

The existing save probe's inherited `choice=Keep` printer labels the save
branch, but that branch asserts disk equality and cleared modified state.
The release typing population derives from the existing allocation_test loop;
zero counted calls is the actual measured guard result, not an assumption.

Final required driver results:

```
$ make -j$(nproc) all
make all exit=0
$ ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j$(nproc) check
rm -f build/san/libedit.a; ar rcs build/san/libedit.a build/san/src/base/base.o build/san/src/base/malloc_guard.o build/san/src/editor/backend.o build/san/src/editor/buffers.o build/san/src/editor/checkpoint.o build/san/src/editor/editor.o build/san/src/editor/input.o build/san/src/editor/large.o build/san/src/editor/open.o build/san/src/editor/paint.o build/san/src/editor/recovery.o build/san/src/editor/runtime.o build/san/src/editor/session.o build/san/src/file/file.o build/san/src/find/find.o build/san/src/find/literal.o build/san/src/findui/findui.o build/san/src/findui/literal.o build/san/src/findui/paint.o build/san/src/findui/worker.o build/san/src/font/discovery.o build/san/src/font/fallback.o build/san/src/font/font.o build/san/src/font/runtime.o build/san/src/font/unicode.o build/san/src/gl/gl.o build/san/src/indent/indent.o build/san/src/ipc/args.o build/san/src/ipc/socket.o build/san/src/ipc/wire.o build/san/src/journal/journal.o build/san/src/journal/path.o build/san/src/keys/keys.o build/san/src/layout/checkpoint.o build/san/src/layout/layout.o build/san/src/layout/wrap.o build/san/src/lineidx/lineidx.o build/san/src/minimap/minimap.o build/san/src/piece/piece.o build/san/src/raster/raster.o build/san/src/raster/raster_kernel.o build/san/src/render/render.o build/san/src/render/render_null.o build/san/src/savectl/savectl.o build/san/src/scan/scan.o build/san/src/scroll/index.o build/san/src/scroll/resident.o build/san/src/scroll/scroll.o build/san/src/scroll/visual.o build/san/src/tabs/tabs.o build/san/src/trace/replay.o build/san/src/trace/trace.o build/san/src/trace/trace_load.o build/san/src/undo/undo.o build/san/src/utf8/utf8.o build/san/src/view/view.o build/san/src/work/work.o build/san/src/x11/clip.o build/san/src/x11/input.o build/san/src/x11/x11.o build/san/src/x11/xi2.o
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:105: check] Error 1
make check exit=2
```

The make parallelism is derived from nproc. Compiler flags are supplied by the
unchanged Makefile. LeakSanitizer is disabled only because this sandbox denies
its required ptrace operation; the coordinator's leak-enabled run remains due.


## Required build and full sanitizer sweep

`make all` uses gcc release with the repository's C11 strict-warning flags.
`make check` was run with clang ASan/UBSan and `ASAN_OPTIONS=detect_leaks=0`.
Its driver stops at cli_test because IPC bind is denied. After an external
shared-display lock delayed another invocation, the final invocation used a
nonblocking display-lock attempt (`EDIT_DISPLAY_LOCK_TIMEOUT=0`, G: immediately
attempt lock acquisition, not a time budget). It stopped at the externally held display lock before launching CLI; direct CLI execution independently confirms the IPC denial.
The lock-owning external process was not disturbed.

I continued all sanitizer executables individually on the safe display, with
socket access still denied. The full sweep is recorded in
`build/ck4-verification/suites.json` and its per-suite logs. Totals are
64 attempted = 47 passed + 17 failed (M)[bat], derived by counting those JSON
process results; the failures below need permitted display or IPC execution.

Display-dependent failures: `editor_close_test`, `editor_test`, `file_kill_test`
(the file bench viewport/queued presentation checks), `raster_test`,
`refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`,
`x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, and
`x11_stall_test`. Socket-dependent failures before display: `cli_test`,
`editor_large_test`, `editor_p4_test`, and `ipc_test`.

GL suites returned success for their pure checks and explicitly skipped native
EGL/Present diagnostics. Pure X11 protocol/guard suites likewise passed without
establishing a native-display verdict. All suites requiring neither display nor
IPC socket access passed. The final-source rerun also passed all previously
unblocked executables: 47 passed / 47 attempted (M)[bat], derived by counting
`final-headless.json` results, with no failing process status. IPC is an additional sandbox restriction, so a full
headless-IPC pass cannot be claimed from this environment.

A direct local AF_UNIX probe established both failures:

```
bind errno derived from syscall: 1 [Errno 1] Operation not permitted
X-connect errno derived from syscall: 1 [Errno 1] Operation not permitted
```

`1` here is EPERM (M)[bat], obtained from the actual bind/connect exceptions.
The coordinator must rerun display and IPC suites with permitted sockets on
Xvfb :99 (G, display supplied by the brief), with leaks enabled. No full native
or comparison-drive acceptance is claimed.

## Tracked benchmarks

Existing populations/fixtures and gates were used; conditions and raw outputs
are copied under `build/ck4-verification/`. The box was loaded and on battery.

- `journal_bench --track`: correctness passes; tracked component miss remains.
- `savectl_bench --track --guard --probe --row 4KiB`: fixture probe succeeds;
  returns its existing unavailable/underqualified status. A probe population is
  not a qualified p99 or whole-editor Saving-frame measurement.
- `editor_bench --track --serial-only --no-idle --no-p4`: null serial row passes
  structural checks and allocation guard. Raster initialization then fails on
  denied X access. The measured null row is pasted below; no native gate verdict.

```
STAMP (M)[bat] BAT0=Discharging load1=28.73 shared box
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=318915 p99=707882 ci95_p50=[316920,321409] ci95_p99=[618760,870556] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[bat] power=Discharging load1=28.73 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[bat]
STAMP (M)[bat] BAT0=Discharging load1=28.73 shared box
```

Derivation: the measured default population is the benchmark's configured
interaction population, with every completed key counted in the emitted row;
latency samples are T4 minus ingress timestamps, percentiles use the existing
harness order statistics, and allocations come from the counting interposer.
Gate values are existing G1 constants, not new bounds.

## Remaining and outside scope

- Coordinator native/IPC/leak-enabled rerun and the next Astra vs Sublime drive
  are still required before hand-over. This sandbox cannot establish those results.
- A retained failed save token preserves recovery and blocks fresh saves until
  resolved. A dedicated retry/cancel UI is not wired by this slice; unrelated
  buffers remain editable. Multiple orphan sessions are not merged.
- The wire schema stores live tab ordering, not precise closed-stack ordering.
  WINDOW state enters the existing resize queue; native window-state restoration
  and giant-session recovery memory/first-paint performance are not certified.
  The new prepaid session graph is not yet included in the existing benchmark
  ownership census. Large-session checkpoint serialization and typing during
  PREPARE have not been comparison-driven. Those follow-up areas were not
  expanded into other beads.
- The loaded full editor run saw the unchanged-chrome assertion fail at its
  emitted location. The isolated `EDITOR_REVIEW_ONLY=p4-40` regression passed in
  both release and sanitizer builds. This transient paint/index interaction is
  reported without changing paint.c/minimap or claiming it resolved.
- `x11_order_test` crashes in libxcb after denied display initialization; its
  fixture continues into XCB calls despite unavailable initialization. No X11
  implementation/fixture changes were made.
- Initial parallel compilation saw a clang frontend crash while sources were
  still being updated; subsequent source-frozen compilation and required builds
  succeeded. A ptrace diagnostic attempt was denied, and LeakSanitizer cannot run
  here. The corrected leaks-disabled replay CLI contract passed.

The worker stops after this assigned bead. No other backlog items were fixed.
