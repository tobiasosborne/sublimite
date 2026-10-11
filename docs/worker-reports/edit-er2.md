# edit-er2 — shared inotify service (src:astra)

Implemented the file-service fix. GCC release builds and the targeted sanitizer
and release regressions pass. Full `make check` cannot pass in this sandbox:
X display access, Unix endpoint binding and socketpair sends are denied. The
real CLI handoff regression is written but remains coordinator verification.
No `bd` or `git` command was run; all repository work stayed in this worktree.

## Changed files

- `src/file/file.c`: install `file_watches` once; keep its descriptor for the
  process lifetime; register files in a fixed table; demultiplex directory/name,
  overflow and invalidated-watch events; enqueue affected files' existing
  identity checks; release shared directory watches only after their last
  member leaves. Dispatch allocates no storage and takes no locks.
- `src/file/file.h`: document the borrowed shared fd, single UI ownership,
  registry bound and mailbox semantics. No new init/fini API is needed.
- `src/file/check_signal_globals.py`: allow only the sanctioned names
  `file_bus` and `file_watches`, rather than allowing an arbitrary mutable symbol.
- `tests/file_test.c`: the requested open/census regression, shared-parent
  close/refresh, another-parent dispatch, mailbox-only state installation and
  descriptor survival after closing the last file.
- `tests/ipc_test.c`: real CLI handoffs to a configured null-renderer primary,
  plus a local request companion that runs without socket binding. Both use
  the requested fixture and exercise retained-tab eviction followed by a
  surviving tab's Source changed state.
- `src/editor/buffers.c` **forced by the file API change**: accept shared-fd
  epoll EEXIST and preserve registration while peers use it. Last-member
  retirement, reload and failed preparation release registration correctly.
  No `src/ipc` or `src/main.c` implementation changed.
- `docs/decisions/edit-er2.md`, `src/file/STATUS.md`, `HANDOFF.md`,
  `docs/worklog/2026-10-11.md`, and this report document the result.

The registry bound is **8192 (P)** = existing maximum live reservation
**4096 (G)** + maximum retained reservation **4096 (G)**, enforced in
`src/editor/open.c`. These are existing limits, not new planning estimates.
The acceptance count is **200 (G)**, directly requested in the bead; the
expected inotify census is **1 (G)**, from its single-process-service requirement.
Each file keeps its own watch descriptor/name association; Linux shares a
parent-directory descriptor among members of that directory.

The prompt literal is byte-identical in the untouched `src/editor/paint.c`:
`Source changed: R reload / K keep`. Its sticky change flag, identity comparison,
generation validation, keep/reload handling and CHECK_DONE payload/install path
retain the existing semantics. Affected peers are queued during the same poll;
there is no added frame wait. Native comparison latency remains unmeasured.

## Red run — before implementation

Power stamp: `Discharging`, **[bat]**. Counts are **(M)** from successful watch
returns and `readlinkat` on `/proc/self/fd`; the failures are the watcher-count
and single-instance assertions. The host allowance in this run differs from
the earlier bead census because other processes share that allowance.

```text
$ ASAN_OPTIONS=detect_leaks=0 FT_CASE=astra-watch build/san/tests/file_test
astra_watch_many: requested=200 watching=58 inotify_instances=58
tests/file_test.c:277: FAIL watching == ASTRA_WATCH_FILES
tests/file_test.c:278: FAIL instances == 1u
file_test: 2 FAILED
```

The requested value is the bead fixture (G); **58 (M)[bat]** is the actual
successful watch/census count; **2 (M)[bat]** is the sum of those failed CHECKs.
The command returned failure, as recorded by the tool's exit status.

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/ipc_test astra_many_open_local
astra_many_open: mode=local requested=200 accepted=58 tabs=58
FAIL tests/ipc_test.c:838: accepted == ASTRA_OPEN_FILES
```

The editor's **58 (M)[bat]** accepted opens and tabs come from its successful
request counter and `editor_get_stats`. The display name is the user-supplied
safe-display identifier, not a measured quantity.

The real CLI regression also ran before implementation, but could not reach
an open because endpoint initialization was denied:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/ipc_test astra_many_open
FAIL tests/ipc_test.c:800: ipc_server_init_display(&server, rt, display) == IPC_OK
```

## Green run — final implementation

The regression launches were stamped `Discharging`, **[bat]**. Successful-open,
tab and fd-census values below are **(M)**, derived by the same counters as red.
The requested count remains the bead fixture (G); the expected service count
remains its single-instance requirement (G).

```text
$ ASAN_OPTIONS=detect_leaks=0 FT_CASE=astra-watch build/san/tests/file_test
astra_watch_many: requested=200 watching=200 inotify_instances=1
file_test: ok

$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/ipc_test astra_many_open_local
astra_many_open: mode=local requested=200 accepted=200 tabs=200
astra_many_open: shared epoll survives retirement; Source changed state delivered
```

Both commands returned success. Release `build/tests/ipc_test
astra_many_open_local` prints the same successful result. The full release and
ASan/UBSan `file_test` suites also end with:

```text
finding23 FORCE: ok
file_test: ok
```

`make all` returned success under GCC and the required C11 strict warning flags.
The final build also rebuilt the final file-test changes in both configurations.
The separately linked syscall-wrapped release watcher regression, built using
the existing recipe in `docs/decisions/P1.7d.md`, returned success:

```text
$ FT_CASE=32 build/edit-er2-evidence/file_wrapped
finding32: ok
file_test: ok
```

Its case identifier is the existing bounded-read/syscall oracle, not a new
budget. The release counting allocator also passes:

```text
$ EDITOR_ALLOC_ONLY=null DISPLAY=:99 build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
```

**10000 (G)** is the existing `allocation_test` fixture in `tests/editor_test.c`;
**0 (M)[bat]** is its active allocator-hook count. This bead adds no typing-path
allocation.

The final symbol census was stamped `Charging`, **[AC]**:

```text
$ python3 src/file/check_signal_globals.py build/rel/src/file/file.o
finding10: ok mutable globals=2 file_bus,file_watches
```

**2 (M)[AC]** is the `nm` enumeration of the explicitly allowed service symbols.

## Required check and complete suite census

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j "$(nproc)" check
== build/san/tests/base_test
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

`nproc` supplies the build parallelism; no job budget was invented. `make check`
built the sanitizer binaries, then stopped at denied IPC binding. All sanitizer
binaries were subsequently run individually, with per-suite logs and exit
statuses in `build/edit-er2-evidence/suites.json`.

**46 passed + 17 failed = 63 suites (M)[bat→AC]**: derived by counting zero/nonzero exit statuses in that JSON, whose entries come from the sorted `tests/*_test.c` glob. Failed suite names are listed below.

The individual batch began on **[bat]** and the later final power probe reported
`Charging` **[AC]**; the exact per-suite transition was not recorded. These are
correctness runs, not timing comparisons.

Display-dependent failures, from their saved initialization/connection logs:
`editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`,
`refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`,
`x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test`.
`file_kill_test` embeds the live viewport/queued-open/queued-save renderer checks;
its cancellation and pooled-save checks passed before the display failure.

Unix endpoint binding failures: `cli_test`, `editor_large_test`,
`editor_p4_test`, `ipc_test`. `editor_test` has both display failures and an
isolated launcher socketpair-send failure. Direct probes confirm the policy:

```text
AF_UNIX bind denied: [Errno 1] Operation not permitted
socketpair send denied: [Errno 1] Operation not permitted
```

The errno is EPERM from the actual syscalls. Thus the headless full IPC suite
also needs the coordinator's run outside this sandbox. Its parser and wire
subtests and the local acceptance regression pass here.

`gl_test` and `gl_gate_test` returned success; `gl_test` explicitly skipped native
EGL/Present diagnostics for unavailable X access. The pure/mocked X11 alignment,
input, display-guard and XI2 suites also passed. All remaining sanitizer suites
returned success, including file, journal, lineidx, savectl and their state/lifetime
regressions. The saved census identifies each suite individually.

The replay CLI shell contract and display-lock shell contract pass with
`ASAN_OPTIONS=detect_leaks=0`. An initial replay-shell attempt omitted that setting
and failed because LSan cannot operate under sandbox ptrace; the corrected run
ends `test_replay_cli: all passed`.

The required file module TRACK benchmark was attempted, stamped
`Discharging [bat]`, and failed at its required live viewport renderer:
`FAIL required viewport renderer initialization`. No latency row was produced.

## Remaining coordinator work and outside scope

Run full `make check` outside the sandbox with leak checking and the user-supplied
Xvfb `:99`, including the real `ipc_test astra_many_open` fixture. Re-run the
native GL paths and file benchmark there. Re-verify this build in the next Astra
vs Sublime drive; there is no native latency or visual-drive claim from this
worker. Keep `src:astra` on the bead; the worker did not use the tracker.

The edit-jmn open regression/report is absent from this worktree, so the CLI
regression was written here without accessing another worktree. Main's existing
default live-tab capacity is **128 (G)**, directly from `editor_open`; the fixture
sets **200 (G)** from the bead. Increasing the production default is outside this
bead and was not done. The fixed process registry is shared across editor
instances and uses the documented existing-reservation sum.

Existing file status proposals and broader editor/IPC issues were not expanded
or fixed. No requested implementation work remains within this bead; full
socket/display/LSan acceptance and the next drive remain unverified here.
