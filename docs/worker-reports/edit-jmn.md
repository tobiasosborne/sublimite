# edit-jmn worker report — partial; do not close

The application tab reservation and IPC diagnostics are implemented. The
existing-file handoff defect remains: file setup exhausts the per-user inotify
instance quota. Its allocation is in `src/file/file.c`, outside the explicit
worker scope, and was left unchanged. No `bd` or `git` command was run; all
source changes are in this worktree.

## Changed files

- `src/main.c`: startup reserves the bead's 1000 live-tab target (G), instead
  of taking the editor library's smaller default. Handoff/startup/wait errors
  print descriptive IPC text.
- `src/ipc/ipc.h`, `src/ipc/result.c`: static, allocation-free descriptions for
  every IPC result and unknown results. The wire format and peer/wait bounds
  are unchanged.
- `tests/ipc_test.c`: real secondary-main/real-editor acceptance regression
  for 200 unique existing files (G: bead acceptance = twice Astra's 100-command
  workload); tab paths, selection and contents are checked. Listener-free
  tests verify main's reservation, admission/selection of the 1000 new-path
  target (G: bead), and error descriptions. `editor_files` is an explicit
  listener-free reproduction of the remaining watcher exhaustion.
- `tests/tabs_test.c`: extends the existing memory census with ABI-derived
  per-slot bytes and the target-sized reservation.
- `docs/decisions/edit-jmn.md`, this report, `src/ipc/STATUS.md`,
  `docs/worklog/2026-10-11.md` and `HANDOFF.md`:
  record the decision, evidence and incomplete acceptance.

No change to `src/editor/buffers.c` was needed for the reservation: its table
sizes already follow the supplied configuration. Main supplies the larger
bound at startup. No typing-path allocation, table relocation or extra frame
deferral was introduced. These tests certify table admission and ownership;
they do not certify rendered latency against Sublime.

## Red runs, before the corresponding implementation

Sanitizer tests use `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0`.
LeakSanitizer is disabled because the sandbox denies its ptrace requirement,
as documented in HANDOFF; the coordinator must rerun with leaks enabled.
The following are command outputs, hence (M)[bat]; BAT0 was Discharging.
Exit statuses are the statuses returned by the named commands, not budgets.

`./build/san/tests/ipc_test cli_capacity` failed. Load1 was 37.33
(M)[bat], read from `/proc/loadavg` before the run. The actual table capacity
comes from the child UI's initialized `tabs_set.capacity`; the expected bound
is the bead's target (G).

```text
sublimite: unsaved session journal: /tmp/sublimite-ipc-A0dDL1/cli-capacity/sublimite/session-6hxRF1
FAIL tests/ipc_test.c:456: state.tab_capacity >= 1000u
ipc_test: CLI tab capacity=128; expected>=1000 (G: edit-jmn target)
```

`./build/san/tests/ipc_test editor_files` failed. Load1 was 37.33
(M)[bat], read before the run. Accepted/attempted are the successful loop
iterations and the next loop index; result is `EDITOR_ERR_IO`, saved errno is
`EMFILE`.

```text
edit-jmn editor admission: accepted=49 attempted=50 result=-103 errno=24 (Too many open files)
FAIL tests/ipc_test.c:426: rc == 0 && editor_get_stats(e).tabs == i + 1u
```

An independent inotify census stamped BAT0=Discharging, load1=12.93
(M)[bat], counted successful `inotify_init1` calls until failure and then
closed every allocated descriptor:

```text
inotify available_instances=53 errno=24 (Too many open files) max_user_instances=128 (M): successful inotify_init1 calls until failure
```

The per-user ceiling comes from `/proc/sys/fs/inotify/max_user_instances`;
available instances vary with other processes. This reproduces a resource
ceiling at file setup and does not infer an exact total-tab count from Astra.
The source chain is `inotify_init1` failure, `editor_buffer_prepare` returning
`EDITOR_ERR_IO`, then the editor IPC callback returning `IPC_REJECTED`.
By contrast, a full tab table returns `IPC_LIMIT`. The old numeric rejection
therefore did not describe the editor's ordinary tab-capacity error.

`./build/san/tests/ipc_test many_opens` cannot reach its workload in this
sandbox. A separate Unix socket probe confirms `bind(AF_UNIX)` returns
`EPERM`; `socketpair` succeeds.

```text
FAIL tests/ipc_test.c:360: ipc_server_init_display(&server, rt, getenv("DISPLAY")) == IPC_OK
unix: [Errno 1] Operation not permitted
pair: allowed
```

The result-text tests were also written before their implementation. Their
initial build failed at link time, explicitly a compile/link red rather than
a runtime result:

```text
undefined reference to `ipc_result_string'
clang: error: linker command failed with exit code 1 (use -v to see invocation)
make: *** [Makefile:97: build/san/tests/ipc_test] Error 1
```

## Green runs for completed scope

After implementation, `./build/san/tests/ipc_test cli_capacity` passed.
BAT0=Discharging, load1=39.38 (M)[bat], read before the run:

```text
sublimite: unsaved session journal: /tmp/sublimite-ipc-dgcvlc/cli-capacity/sublimite/session-iYEcw0
ipc_test: CLI tab capacity=1000; expected>=1000 (G: edit-jmn target)
```

`./build/san/tests/ipc_test editor_tabs` passed with the same pre-run power
and load stamp. The count is its target-sized loop; each path and tab is
checked and selected:

```text
ipc_test: edit-jmn 1000 unique new paths accepted, all tabs selectable
```

`./build/san/tests/ipc_test result_text` passed:

```text
ipc_test: descriptive IPC result text passed
```

The existing release typing-allocation check
`EDITOR_ALLOC_ONLY=null ./build/tests/editor_test` passed. The key count is
the existing fixture's loop; allocation count is its active guard result:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

This is the pre-existing allocation check, not a new timing budget or a
target-sized tab latency measurement.

## Fixed-state memory evidence

`./build/tests/tabs_test` passed; the existing aggregate reserve row remains
a tracked MISS from the earlier undo-reserve bead. BAT0=Discharging,
load1=39.38 (M)[bat], read by its census. The added row is:

```text
tabs_test: edit-jmn fixed state (M)[bat] tab_record=4800 indices=8 per_slot=4808 live=1000 retained=16 slots=1016 arena=4886528 control=128 owned=4886656; per_slot=sizeof(tabs_tab)+2*sizeof(uint32_t), arena=page_round(slots*per_slot)
```

Derivation: `sizeof(tabs_tab) = 4800`, plus the order/free arrays' two
`sizeof(uint32_t) = 4` entries, is 4808 bytes per slot (M)[bat]. The census
retains its existing closed-tab allowance of 16. Thus
`(1000 + 16) * 4808 = 4884928`; page size is 4096 bytes (M)[bat], queried with
`getconf PAGESIZE`, so rounding gives 4886528. Add `sizeof(tabs_set) = 128`
for 4886656 bytes owned (M)[bat]. This excludes buffer contents, file objects,
indexes and editor-owned undo/history. No total memory budget is claimed.

## Full validation

`env DISPLAY=:99 EDIT_DISPLAY=:99 make -j"$(nproc)" all` passed with gcc release
and the requested strict C11 warnings. Build parallelism comes from the actual
`nproc` query, not an invented concurrency budget. The final release build log
is `build/edit-jmn/all-final.log`.

`env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j"$(nproc)" check`
built the clang ASan/UBSan tests, passed `base_test`, then failed in `cli_test`.
Make's reported failure is:

```text
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

That is listener-binding denial before any X connection, not a display-only
failure. The separately attempted IPC benchmark likewise stopped at
`ipc_server_init`: no latency measurement was obtained. Its pre-run stamp was
BAT0=Discharging, load1=45.66 (M)[bat], read from `/proc/loadavg`.

Since make stops at the first failed suite, the remaining binaries were run
individually with the same sanitizer/display environment. The individual
display probes ran directly after a shared-display-lock wait; the sandbox's
denied X socket prevents them from interacting with the display. Each suite's
log stamps its own BAT0 and pre-run load. There are 63 built binaries and
63 result records (M)[bat], counted from `build/san/tests/*_test` and
`build/edit-jmn/suites.tsv`; 46 returned success, 16 returned failure and
the editor suite was interrupted, so `46 + 16 + 1 = 63`. This is a record count,
not a claim that display acceptance passed. Logs are under
`build/edit-jmn/suites/`.

Failures requiring the coordinator's display rerun:

- `editor_close_test`: its native `close_case(true)` cannot initialize the
  editor; its headless close case passed.
- `editor_test`: native editor setup fails; a reload-launch test also fails
  because it needs IPC. After logging failures it hung in cleanup and was
  interrupted. The separate release null typing-allocation check passed.
- `file_kill_test`: its viewport and queued open/save fixtures use
  `bench/file_bench.c`'s `view_init`, which calls `plat_init`. Those fail without
  X access; its printed headless cancellation/self-check cases passed.
- `raster_test`: fails native `plat_init`; its preceding headless contract
  cases ran.
- `refproto_test`, `refwin_test`, `refwin_timeout_test`: report display
  connection errors.
- `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`,
  `x11_order_test`, `x11_stall_test`: fail display/private-Xvfb fixture or live
  display initialization.

`gl_test` returns success for its pure checks but explicitly skips native
EGL/Present diagnostics because X is unavailable; the native GL acceptance
still needs the coordinator. `gl_gate_test`, `x11_alignment_test`,
`x11_display_guard_test`, `x11_input_test` and `x11_xi2_test` returned success
for their runnable checks.

Additional non-display failures, explicitly not hidden as display failures:

- `cli_test`, `editor_p4_test`, `ipc_test`: listener initialization is denied.
  `ipc_test` prints `init rc=4 errno=1 (Operation not permitted)`; these are
  its returned `IPC_IO` and the OS `EPERM` (M)[bat], not budgets.
- `editor_large_test`: the headless cooperative-job test failed its existing
  deadline assertion. Its suite log stamped BAT0=Discharging, load1=61.95
  (M)[bat]. This is outside the bead's changed code and was not fixed. The
  standalone retry `P4_COOPERATIVE_JOBS=1 ./build/san/tests/editor_large_test`
  passed, with BAT0=Discharging, load1=13.51 (M)[bat], read before the run:

```text
P4-3 section 28: sealed find publication yields behind a saturated mailbox passed
P4-3 section 28: production find/warm yield bulk lane before completion passed
```

That retry establishes the named case passes on retry; it does not establish
the cause of the initial timeout or certify the rest of the interrupted
default large-editor run. Keep the initial failure in the coordinator evidence.

All other completed binaries returned success, including file, find, findui,
font, harness, indentation, journal kill/recovery, journal, keys, layout,
line-index, minimap, piece, prewake, render, replay, runtime, save-controller,
scan, scroll, tabs, trace, undo, Unicode, UTF-8, view, work and wrap suites.
The replay shell contract also passed when run separately with
`ASAN_OPTIONS=detect_leaks=0`. An initial shell invocation without that setting
failed with LeakSanitizer's explicit ptrace error, confirming the sandbox
restriction; the corrected invocation passed.

The requirement that every headless suite pass is **not met here**: IPC
listeners are forbidden, and the large-editor suite's original failure stopped
its full run despite the successful standalone retry. Full CLI/file acceptance
is also still red as detailed above. Do not close edit-jmn on these partial
green checks.

## Remaining acceptance and outside scope

The existing-file workload is not green. Resolve per-file inotify-instance
allocation before expecting the real CLI regression to pass. Shared watches
must preserve each file's external-change notifications and save refresh;
silently dropping watches or changing the user's kernel quota is not a fix.
The coordinator must run `ipc_test many_opens`, `ipc_test editor_files`, the
full checks with leaks enabled, and the display suites on Xvfb :99, then the
next Astra comparison drive. The display number is the coordinator-specified
test endpoint, not an invented workload.

Other unchanged limits found while tracing this defect: the mapped-file
SIGBUS registry uses `FILE_GUARD_SLOTS = 256` (source-defined bound in
`src/file/file.c`), so the tab target alone cannot promise that many mapped
files; retired file buffers are retained until shutdown by the existing
editor/file lifetime contract and can exhaust the configured buffer table.
These need coordinator triage and were not fixed. IPC named-test dispatch also
silently succeeds for unknown names; only runs with the actual expected
output are used as evidence here.

## 2026-10-11 continuation: reconcile with main's edit-er2

This section supersedes the earlier watcher-exhaustion status above. Main now
contains the process-wide inotify service, `t_astra_many_open`, and
`t_astra_watch_many`. No `git` or `bd` command was run during this continuation.

The actual compiler error in `build-check.log` was an unresolved conflict
marker in `tests/ipc_test.c`'s test dispatcher. Removed all three markers and
kept main's `t_astra_many_open` and both dispatcher entries (`astra_many_open`
and `astra_many_open_local`). Removed edit-jmn's overlapping `t_many_opens`
and existing-file probe; their old names `many_opens` and `editor_files` no
longer designate tests. The retained listener-free reservation probe is now
`t_editor_tabs`/`editor_tabs`, covering 1000 unique new paths and selection.
Use main's Astra cases for the 200 existing-file workload.

Main's local Astra branch initially failed at 118 accepted files, then at 46
on a diagnostic retry (`EDITOR_ERR_IO`; observed errno was `EEXIST`, which
alone does not identify the underlying error). Added failure diagnostics and
one `editor_poll_sources` turn after each successful local open, matching the
CLI branch's event pumping while it awaits client completion. This services
shared-watch events and worker completions between opens. The same 200-file,
tab-count, retirement, and source-change assertions remain. Three subsequent
runs passed; neither initial failure is represented as a passing run.

Retained the existing edit-jmn production changes without further edits:

- `src/main.c` reserves 1000 live tabs at startup. The editor library still
  defaults to 128, and er2's fixture reserves 200 only for its test, so the
  application reservation continues to add value.
- `src/ipc/ipc.h`, `src/ipc/result.c`, and main's handoff/startup/wait paths
  provide static, readable IPC diagnostics. Watch sharing does not replace
  these diagnostics; rejection text still does not claim a specific cause.
- `tests/tabs_test.c` retains its ABI-derived reservation census. The measured
  target storage is 4,886,656 bytes owned, including 1000 live and 16 retained
  slots; this is tab state rather than a total editor memory budget.

Validation used `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99`.
The measured census tagged power as Charging/AC. Logs are in this worktree.

- `make -j4 all`: **passed**, including the reconciled release IPC test.
  The final combined invocation, `make -j4 all build/san/tests/ipc_test
  build/san/tests/tabs_test build/san/tests/editor_test`, also **passed**
  (`build-final-reconcile.log`). This confirms the reported IPC sanitizer
  compilation failure is resolved; a complete `make check` pass is not claimed.
- ASan `tabs_test`: **passed** (`test-tabs-reconcile.log`), including sequences,
  rendering, retention, capacity errors, and 10,000 switches.
- ASan IPC `cli_capacity`, `result_text`, and `editor_tabs`: **passed**;
  main initialized a capacity of 1000, all IPC result descriptions were
  available, and all 1000 new-path tabs were selectable. Logs are
  `test-ipc-capacity-reconcile.log`, `test-ipc-results-reconcile.log`, and
  `test-ipc-tabs-reconcile.log`.
- ASan IPC `astra_many_open_local`: **passed** after the event-pump change,
  with `requested=200 accepted=200 tabs=200` and the shared-epoll retirement /
  Source changed assertions. See `test-ipc-local-reconcile.log` and
  `test-ipc-local-repeat-reconcile.log`; the final-source run also passed
  (`test-ipc-local-final-reconcile.log`).
- ASan `FT_CASE=astra-watch file_test`: **passed**, reporting
  `requested=200 watching=200 inotify_instances=1`
  (`test-file-watch-reconcile.log`). This verifies er2's service in this process.
- Full ASan `ipc_test`: **blocked**, exit 1 at its first listener initialization:
  `init rc=4 errno=1 (Operation not permitted)`. The sandbox denies Unix socket
  binding (`test-ipc-reconcile.log`), so the full CLI handoff case is unverified.
- Full ASan `editor_test`, launched with `tools/with_display_lock.sh`: acquired
  the shared display lock after waiting, then reported native editor-open and
  reload-launcher failures and a settle timeout. It stopped making progress
  after those failures and was interrupted (exit 130), not counted as a pass
  (`test-editor-reconcile.log`). An independent connection to the existing
  `/tmp/.X11-unix/X99` socket returned `EPERM`. The null-backend allocation
  case (`EDITOR_ALLOC_ONLY=null`) **passed**, reporting 10,000 keys and zero
  guarded allocations (`test-editor-null-reconcile.log`); it is partial coverage.

The source reconciliation and required builds are complete. The requirement
that all three full ASan test binaries pass is **not met in this restricted
sandbox**. Full `ipc_test` and `editor_test` still need an environment that
permits the private IPC listeners and Xvfb socket. No production watcher,
wire-protocol, editor-library default, or kernel-limit change was made here.
