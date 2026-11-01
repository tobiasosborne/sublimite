# edit-177r.28 — safe WSL degraded mode

Round 2 verdict: **done**. The landed Windows editor now opens WSL copied
buffers without notifications, checks identity at the requested boundaries,
refuses share saves safely, and supports Windows Save As. Latest required exit
codes (M): **all 0; Linux check 0; Windows check 0**. Full evidence follows in
the round-2 section below; historical round-1 status is superseded.

## Historical round-1 report

File-layer implementation and acceptance are complete. Whole-editor signoff is
**partial pending the sibling's Windows editor integration** described below.
No Windows editor executable is built by this baseline's mk/win.mk, so this
report does not claim an end-to-end Windows editor drive.

All runs used `taskset -c 48-63 nice -n 10`; every make used `-j16` (G, brief's
assigned lane and parallelism). Windows executables ran only through
`tools/winrun.sh`, including every child inherited from its private station.
Sandbox escalation enabled WSL interop/LeakSanitizer, without changing runner
isolation. No live desktop, clipboard, registry, real profile, WSL lifecycle,
state-changing git command or other worktree was touched.

Measurements below are (M) **[AC, desktop Spiral], Tobias using the box**.
BAT0 is absent; AC is the binding brief's desktop power context. Start `uptime`:
`12:35:12 up 4:09, load average: 0.89, 2.04, 3.24` (M). Final real-share run:
`12:51:26 ... load average: 0.70, 3.13, 3.46` to
`12:51:49 ... load average: 4.68, 3.77, 3.66` (M, wrapper uptime).

## Changes and boundaries

- `src/os/os_win.c`: one exported CPU predicate, `os_path_is_wsl`, with its
  Windows-only declaration in `src/os/os.h`. Exact `wsl.localhost` / `wsl$`
  server matching ignores server/prefix case and accepts ordinary UNC,
  extended UNC, device UNC (accepted by the existing converter), and slash
  spellings. Lookalikes and other SMB shares retain existing behavior. No
  existing OS function was reshaped. The OS header addition is a declaration,
  not a change to any frozen operation's semantics or layout.
- `src/file/file.c`: WSL originals use the existing read-copy pipeline with
  descriptor and path validation before/after prefix and full reads. Original
  mappings are never selected. **Above an injected map-selection threshold,
  the copy owns a full-file allocation**. Its hard bound is the existing default
  `min(256 MiB, physical RAM / 32)` (P, file_default_copy_threshold's existing
  arithmetic). Above that bound, OPEN_FAILED reports `FILE_ERR_NOMEM` and native
  `ERROR_FILE_TOO_LARGE` before byte allocation or prefix publication. Allocation
  failure inside the bound fails normally; neither failure maps. This bounds
  each original, not aggregate tab memory or existing editor reservations.
- Both `file_save_begin` and the synchronous transaction refuse WSL destinations
  with `FILE_ERR_UNSUPPORTED` / `ERROR_NOT_SUPPORTED`. The transaction refuses
  before directory/metadata access and temporary creation. FORCE does not
  bypass it. Existing, absent, extended and colon-named share destinations all
  refuse. Save As to NTFS uses the ordinary transaction on the edited snapshot.
  No stream sibling, overwrite, unlink/rename or metadata-skipping fallback was
  added.
- `src/file/file_win.c`: native watch error `ERROR_INVALID_FUNCTION` on an exact
  WSL server becomes `FILE_WATCH_UNSUPPORTED`, with no registered watch, timer,
  or user error diagnostic. Other errors and servers retain ordinary failure.
  Explicit `file_watch_poll` on an unregistered WSL original now coalesces an
  authoritative worker identity check through CHECK_DONE; it works without a
  native event and adds no timer. Stream enumeration treats measured
  `STATUS_NOT_SUPPORTED` as an empty list alongside the established unsupported
  statuses. Access denial and real I/O errors still fail.
- `src/file/file.h`: documents errors, watch result and bounded copy policy.
- The only editor change is **`editor_save_overlay` in src/editor/session.c**.
  It renders: “Cannot save to a WSL path from Windows yet. Use Save As to a
  Windows path, or edit it in the Linux build.” Failed WSL Save As uses the same
  notice; a fresh Windows Save As failure keeps its own diagnostic. The
  full text has a headless status-grid regression in editor_session_test.
- Tests: native provider shims/cases in file_win_test and the new classifier /
  degraded .inc tables; explicit real acceptance in
  `tests/win/wsl_share_degraded.c` and its Python fixture. `mk/win.mk` adds only
  the explicit fixture target and included-test dependencies; its editor rules
  are untouched. The fixture reuses wsl_share_probe's marker helper, station
  checks, watchdogs and Known Folder fallback shim for a private NTFS profile.
- Design choices appended to `docs/decisions/edit-177r.21.md`.

## Red → green evidence

Commands below omit the common CPU/nice prefix only for readability. Exit codes
are captured process results (M). Logs are under build/worker-logs/.

| Case | Red command and relevant output | Green |
|---|---|---|
| Exported classifier | `make TARGET=win -j16 build/win/rel/tests/file_win_test.exe`: exit 2, undefined `os_path_is_wsl` after adding the calls (`edit-177r.28-red-classifier.log`). | `tools/winrun.sh build/win/rel/tests/file_win_test.exe wsl-classifier`: exit 0, classifier table ok (`edit-177r.28-green-classifier.log`; final Windows suite also covers it). |
| Device UNC bypass | Same native classifier selector after adding the device-UNC case: exit 1, `FAIL os_path_is_wsl(yes[i])` (`edit-177r.28-red-prefix.log`). | Final classifier/native suite exit 0 after recognizing the existing converter's device form. |
| Measured stream status | `tools/winrun.sh build/win/rel/tests/file_win_test.exe stream-unsupported`: exit 1, `STATUS_NOT_SUPPORTED: rc=1 native=50`, with original bytes instead of the expected new snapshot (`edit-177r.28-red-streams.log`). | Complete native suite exit 0; status case prints rc=0. Other injected statuses retain IO/native diagnostics (`edit-177r.28-native-green.log`). |
| Copy/cap/watch/save policy | `tools/winrun.sh build/win/rel/tests/file_win_test.exe wsl-degraded`: exit 1; selected MMAP, returned watch -1/native 1, reached injected rename native 87 and temporary creation, failed oversize refusal (`edit-177r.28-red-policy.log`). | Same selector exit 0 (`edit-177r.28-green-policy.log`), no directory opens/temp/rename in refused saves; NTFS target succeeds; bounded oversize and native 1921 error pass. |
| Poll without registration | Same selector with a new asynchronous polling assertion: exit 1, `FAIL file_changed(f) && file_check(f, NULL)` (`edit-177r.28-red-poll.log`). | Same selector exit 0 (`edit-177r.28-green-poll.log`), through the production mailbox/check job. |
| Exact notice | `build/san/tests/editor_session_test --wsl-save-notice`: exit 1, status glyph mismatch (`edit-177r.28-red-notice.log`). That first sandboxed failing run also reported unavailable LeakSanitizer. | Final rebuilt selector outside the sandbox: exit 0 with LeakSanitizer enabled (`edit-177r.28-green-notice-final.log`). |
| Real provider | Original HEAD file.c/file_win.c were compiled into build/win/red objects and linked ahead of the current archive (read-only git show; no source rollback). `env WSL_DEGRADED_EXE=build/win/red/wsl_share_degraded.exe python3 tests/win/wsl_share_degraded.py`: exit 1, real-share acquire selected MMAP (`edit-177r.28-red-real-share.log`). | `make TARGET=win -j16 win-wsl-share-degraded`: exit 0 (`edit-177r.28-final-real-share.log`). |

The native check fixture requires no WSL mount. It injects measured native 1 at
ReadDirectoryChangesW, native 87 at the common rename boundary (which refusal
must never reach), native 1921 at open, and NTSTATUS 0xc00000bb at stream query.
The already-correct unresolved-link behavior is coverage, not an invented
behavioral red failure.

## Real-share acceptance

The final explicit target passed ordinary and extended spellings for both
servers (M: two servers × two prefix spellings = four fixture runs). Every run:

- crosses the injected map-selection threshold but acquires COPY;
- edits, saves through the production **journal-backed savectl prepare / handoff
  / file write / completion path**, observes FAILED / UNSUPPORTED / native 50,
  keeps modified true and creates no temporary;
- refuses Save As over existing, missing and literal colon share targets;
- uses Linux lstat/read and a directory listing to assert the original bytes,
  mode, owner/group, inode and entry set are unchanged at save refusal;
- saves the actual edited snapshot to a fresh private NTFS destination;
- performs Linux write, atomic replace and truncate via the marker helper;
  explicit file_check detects each while the private edited tree stays intact;
- reads distinct `a` / `A` bytes and identities and literal trailing dot/space
  names; unresolved Linux symlink acquisition fails with native 1921 and the
  link remains intact;
- publishes the actual edited tree to a whole-text NTFS journal **before** the
  failed save; exits without closing it, then the supervisor replays the journal
  after process exit and verifies the edits. This tests the file/controller/
  journal path, not the sibling's as-yet-unbuilt Windows editor session UI.

Representative final output (M):

```text
save refused: FILE_ERR_UNSUPPORTED, dirty kept, no temporary
inspect-save ... OK mode=0751 uid=1000 gid=1000 ... size=9 bytes_hex=6f726967696e616c0a
inspect-streams ... entries=['A', 'a', 'link', 'source', 'space ', 'trailing.']
Save As to NTFS: edited bytes saved
Linux write: file_check detected, private edited bytes intact
Linux replace: file_check detected, private edited bytes intact
Linux truncate: file_check detected, private edited bytes intact
hot exit: private NTFS journal replay retained edited bytes
real-share degraded: PASS
```

The permission fixture is the probe's existing octal 0751 choice (E,
distinguishes executable bits and permission classes). Reported sizes, identities
and permission values above are emitted Linux observations, not Windows synthetic
mode estimates. Exit 77 reuses the native file suite's stream-crash exit sentinel (E),
not an unexpected crash. The supervisor checks it and never terminates a process
it did not spawn. Existing probe watchdog/helper bounds are reused unchanged.

## Verification

All statuses below are (M), from the prefixed commands' captured exits:

| Command | Exit / evidence |
|---|---|
| `make -j16 all` | 0, including the final notice source (`edit-177r.28-linux-all-final.log`) |
| `make -j16 check` | 0, emitted 117 binaries passed (`edit-177r.28-linux-check.log`) |
| `make TARGET=win -j16 check` | 2: journal_kill_test release and sanitizer fail their randomized-writer progress deadline; all file suites pass. See below (`edit-177r.28-win-check.log`). |
| `make TARGET=win -j16 win-wsl-share-degraded` | 0, final journal-backed fixture (`edit-177r.28-final-real-share.log`) |
| Full native file suite | 0, emitted 20 cases (`edit-177r.28-native-green.log`; final Windows suite includes the later polling test) |
| Final headless exact notice | 0, rebuilt selector with LeakSanitizer |
| Mutable-global audits, Linux common / Windows watch object | 0 / 0; only the existing file_watches service on Windows |
| Linux file.c preprocessor comparison against HEAD | 0: byte-identical output from `gcc -E -P`, current headers, cmp |
| `git diff --check` | 0 |

Windows full-check result (M): emitted `binaries=122 desktop-passes=6 jobs=8
failures=2`, with both failures in journal_kill_test. Both report
`journal_hot_exit_test:115 FAIL reached && st.how==OS_PROC_ABNORMAL native=258`
from the included randomized campaign; the boundary process-death cases pass.
The campaign's existing progress deadline expires before the requested edit count
is reached. No journal source was changed in this bead. The isolated release
rerun passed the full campaign (exit 0,
`edit-177r.28-journal-kill-isolated-rel.log`), consistent with load-sensitive
progress deadlines. The isolated sanitizer rerun also passed the full campaign (exit 0,
`edit-177r.28-journal-kill-isolated-san.log`). The original
full-check exit remains 2, not green. Exact file
results in the same full run:

```text
RESULT rel.file_win_test.station exit=0 PASS
RESULT san.file_win_test.station exit=0 PASS
RESULT rel.file_test.station exit=0 PASS
RESULT san.file_test.station exit=0 PASS
```

The Linux harness initially failed retired_named_release(true) in parallel,
then passed it alone and reported FLAKY. Its final full-check exit is 0. This
pre-existing retired-buffer timing path is outside this bead; see
build/check-flaky.log and build/check-logs/editor_session_test.rerun.log.
Both isolated journal reruns emitted their full 50-trial campaign success (M,
existing test policy; no reduced trial count or widened deadline). The full
parallel run's failures are therefore load-sensitive observations outside this
change's WSL/file paths. Coordinator: triage that harness deadline/concurrency
separately; do not call the recorded full Windows check green. Failed campaigns
can leave their private per-PID NTFS scratch; no broad prefix cleanup was
attempted because other workers may own similarly named scratch.

Harness counts above are emitted suite/case enumerations, not estimated totals.
The explicit share target is outside all/check; a Linux-only ThinkPad needs
neither WSL paths nor the Windows toolchain for Linux all/check. The explicit
fixture itself prints SKIP if wslpath is absent.

File benchmark ran in TRACK mode through the existing private-Xvfb harness,
using the same CPU lane. Exit 0 (`edit-177r.28-file-bench.log`), raw samples in
build/check-logs/file-bench-track.sh.log. The tool self-tags power unknown when
BAT0 is absent; the applicable context is [AC, desktop Spiral] from the brief.
Representative (M) warm rows under the emitted loads:

| Row | Samples / load1 | p50 / p99, ms |
|---|---|---|
| sparse original, idle | 100 / 1.49 | 3.614 / 4.833 |
| Unicode copy original, idle | 100 / 1.33 | 3.875 / 5.035 |
| fresh fragmented save status, 1 MiB | 100 / 1.33 | 0.485 / 0.647 |
| durable completion, 1 MiB | 100 / 1.33 | 18.817 / 29.701 |
| durable completion, 1 GiB | 10 / 1.22 | 992.568 / 1847.432 |

Derivation: existing bench sample policy; harness nearest-rank percentiles over
sorted measured nanoseconds, converted to ms by division by 1,000,000 (P).
The 1 MiB completion p50 is above the tool's old 10 ms aspiration; this is
tracked, not a regression gate or a Windows-share performance measurement.
No cold-cache or live-desktop performance result is claimed.

## Found, not fixed / coordinator integration

**The coordinator must integrate these with the sibling's editor port before
calling the whole-editor WSL goal done.** These are observed call-path gaps,
not hypothetical follow-on hardening:

1. `editor_buffer_make` in buffers.c currently treats every negative
   file_watch_start result as fatal. Its Windows branch must accept
   FILE_WATCH_UNSUPPORTED, keep watch_fd invalid, omit native event registration,
   and still open the copied buffer. Apply the same rule to reload/target-watch
   setup. Returning -2 must not become a user-facing “open failed”. I did not
   edit those sibling-owned functions under the one-editor-function allowance.
2. Focus tracing: editor.c nonkey_dispatch → focused PLAT_EV_FOCUS → direct
   file_check(active file) → editor_check_sources. This does **not** require
   registration. Retained/inactive tabs should be checked when activated by the
   port, as appropriate to its existing activation rules.
3. Save tracing: editor session → savectl worker → file_save_write now refuses
   WSL before target I/O; there is no rename needing authorization. **This early
   refusal does not refresh the disk-conflict state.** To meet the brief's
   separate pre-save change-detection requirement, the sibling should retain a
   watch-independent file_check (or complete an explicit file_watch_poll check)
   before dispatching a save. Cached epoch/source validation alone does not stat
   a copied original's pathname.
4. Bulk tracing: prefix_job/open_job validate the provider descriptor and name
   before/after actual provider reads. Editor large.c warm/find snapshot work
   checks file_snapshot_faulted, which is always false for intact private
   copies; it does **not** detect an external pathname update. Before starting
   new bulk commands the sibling must request/adopt a watch-independent identity
   check, then respect changed state. file_watch_poll now supports that without
   a native watch. I added no UI timer or filesystem I/O on the typing path.
5. Tab dedupe audit: current editor_same_path uses exact canonical strcmp then
   actual dev/ino identity; no case fold. The real provider fixture proves
   `a`/`A` have different bytes and IDs on this host. A running **two-tab Windows
   editor** assertion remains for the sibling once that executable exists; this
   report does not pass off the read/ID test as that end-to-end assertion. Keep
   case and trailing characters intact in the port's canonicalization/dedupe.
6. The status overlay change is deliberately the sole touched editor function.
   Reconcile it with the sibling's session.c changes and run the Windows UI
   notice/Save As tests once the editor link lands. The exact text's rendering
   is already covered headlessly on Linux.

While focused with no focus transitions or explicit commands, there is no
notification-driven discovery on WSL. A future policy should coalesce worker
checks for active copied sources, off the typing path, selecting the interval
from measured cost and desired discovery latency. No arbitrary interval or new
timer is added here, per brief.

Provider outage/reconnect, cross-distro identity namespace and Linux metadata
preserving writes remain unproved/deferred, as in the measurement decision.
They were not provoked or weakened. No write architecture decision is needed to
land the refusal policy; a future Linux-aware transaction requires separate
preservation/atomicity evidence.

Final status (M): all 0; Linux check 0; Windows check 2; real-share target 0;
standalone Windows release/sanitizer journal reruns 0/0. Final `uptime`:
`13:07:22 up 4:41, load average: 0.01, 0.44, 1.90` [AC, desktop Spiral].
No worker processes remain running.

## Round 2 — editor integration (2026-11-01, complete)

The earlier report above records the pre-editor baseline. This round supersedes
its coordinator integration checklist and will record final verification below.
Binding lane: `taskset -c 16-31 nice -n 10`, make `-j16`, Windows check
`WIN_CHECK_JOBS=4` (G, current brief). All Windows execution remains through
`tools/winrun.sh` on a private station. WSL interop requires sandbox escalation;
the first unprivileged attempt failed before running the test with
`UtilBindVsockAnyPort: socket failed 1`. No live desktop or real profile used.

(M) [AC, desktop Spiral], Tobias using the box, start `uptime`:
`16:06:44 up 7:38, 1 user, load average: 0.28, 0.52, 1.07`. BAT0 absent;
AC is the binding desktop context. No new performance measurements claimed.

Red: prefixed `tools/winrun.sh build/win/rel/tests/editor_win_test.exe --wsl-open`
exited 1 at `editor_add_buffer(e,path,NULL,0,&id)==0` after the real file layer
returned FILE_WATCH_UNSUPPORTED (`build/worker-logs/r2-red-open.log`). Tests stay
in the existing editor_win_test.c; no fixture extraction or helper write path.

### Round 2 implementation and red/green evidence

- Source/reload setup accepts FILE_WATCH_UNSUPPORTED, normalizes watch_fd to
  invalid, and skips native wait registration. Save As adoption accepts the
  same capability without claiming a write failure. The native error branch
  remains unchanged for other negative results.
- New Windows-only `src/editor/source_win.c` compares identity at focus,
  activation, explicit save and warm/find boundaries. A rebound COPY compares
  its new target, never its retired original pathname. Existing source
  maintenance owns clean reload versus edited-buffer conflict choices.
- Session save refreshes identity before dispatch and refuses WSL before
  preparing the journal transaction. This reconciles the earlier session
  notice with the landed controller: its failure status is “save or reload
  failed”, and a prepared failure can retain cancellation ownership. Early
  refusal displays the established WSL notice and keeps Save As available.
  File-layer refusal remains unchanged and authoritative for other callers.
- Real find commands also refresh identity before binding/searching a copied
  snapshot. Ordinary document/query typing only reads resident state. The
  test asserts no UI-thread provider stat during document insertion.
- Tests remain in `tests/win/editor_win_test.c`. The existing real editor,
  journal, view, command and D3D fixture drives all cases; test-only classifier,
  canonicalization, acquisition and native-watch seams replace no production
  editor behavior. `a` and `A` aliases in the same modeled case-sensitive
  provider resolve to distinct NTFS-backed identities. No new fixture module.
- `mk/win.mk` was audited after the mechanical merge: full editor source
  enumeration/application links, standalone WSL targets, and release/sanitizer
  editor fixture links coexist. Updated fixture link filters omit only editor
  implementation objects now included by its provider seams; core file symbols
  are supplied by the fixture TU before archive extraction. Dependency files
  track the included sources. No Linux build rule changed.

Every command below uses the current CPU/nice prefix. Selector commands are
`tools/winrun.sh build/win/rel/tests/editor_win_test.exe --wsl-<selector>`;
rebuilds use `make TARGET=win -j16 build/win/rel/tests/editor_win_test.exe`.
All exits and output below are (M), captured logs in `build/worker-logs/`.

| Behavior | Red | Green |
|---|---|---|
| Open/reload without notifications | `--wsl-open`, exit 1, `FAIL editor_add_buffer(e,path,NULL,0,&id)==0`, `r2-red-open.log` | exit 0, `unsupported watch opens and reloads, no native registration PASS`, `r2-green-open.log` |
| Activation identity | `--wsl-activate`, exit 1, `FAIL file_changed(b->file) && b->source_stale...`, `r2-red-activate.log` | exit 0, detects external identity, `r2-stage-activate.log` |
| Pre-save conflict | `--wsl-presave`, exit 1, `FAIL !editor_save_active(e)`, `r2-red-presave.log` | exit 0, conflict present before save dispatch, `r2-stage-presave.log` |
| Direct bulk-save conflict | `--wsl-large-save`, exit 1, `FAIL file_changed(b->file) && !b->lg.save_running`, `r2-red-large-save.log` | exit 0, `r2-green-large-save.log` |
| Warm snapshot | `--wsl-warm`, exit 1, `FAIL !b->lg.warm && file_changed(b->file)`, `r2-red-warm.log` | exit 0, no stale warm snapshot, `r2-stage-warm.log` |
| Find snapshot | `--wsl-find`, exit 1, `FAIL editor_large_find_begin(...)==EDITOR_ERR_IO`, `r2-red-find.log` | exit 0, no stale find job, `r2-stage-find.log` |
| Real find command | `--wsl-findpanel`, exit 1, `FAIL e->find_id!=b->id && !b->find_leases`, `r2-red-findpanel.log` | exit 0, `r2-stage-findpanel.log` |
| Notice/Save As | `--wsl-save`, exit 1, exact notice glyph mismatch after completed controller failure, `r2-stage2-save.log` | exit 0, notice, dirty text/journal, Windows Save As and subsequent Ctrl+S all pass, `r2-stage3-save.log` |
| Unsupported target watch | `--wsl-target-watch`, exit 1, `FAIL !e->recovery_notice || strcmp(...,"save as failed")`, `r2-stage4-target-watch.log` | exit 0, `r2-green-target-watch.log` |

Focus and case-distinct tabs were already correct after unsupported open was
fixed; their baseline selectors both exited 0 (`r2-red-focus.log` and
`r2-red-case.log`, filenames denote the baseline campaign, not false red claims).
The journal selector reopens the real editor after refusal and restores exact
`xoriginal\n` dirty text, exit 0 (`r2-stage4-journal.log`). The optional real
UNC selector read this worktree's `tests/win/editor_win_test.c` as COPY, exit 0
(`r2-stage4-real.log`); it performs no Windows share write and skips if absent.

Test development corrections are not product reds: first save assertions had
not waited for the worker, and a later sequence-monotonic assertion did not
account for checkpoint rotation resetting journal counters. The final tests
wait for transaction completion and prove recovery by reopening the editor.

Linux preprocessor comparison: exit 0 (`r2-linux-preprocess.log`), all touched
production TUs have identical Linux tokens after normalizing whitespace and
assertion source-line metadata. `git diff --check`: exit 0 before verification.

Interim whole-check diagnosis (M): the first Windows run reports existing
editor_exe_test failures at `wait_contents(path,"typed äλ中🙂base\n")` in both
configurations. A release countercheck rebuilt **every touched editor TU from
unmodified HEAD**, excluded the new Windows helper, linked the normal untouched
objects/library/main, and ran the identical executable fixture on its private
station. It failed at the same line with unchanged `base\n` bytes, exit 1
(`r2-baseline-build.log`, `r2-baseline-editor-exe.log`). No source rollback or
state-changing git operation. This is a baseline/coordinator finding, not
attributed to the WSL integration.

The first whole Windows run also caught the new journal selector's parent
completion watchdog. Its isolated sanitizer selector passed, exit 0
(`r2-san-journal-isolated.log`). The test reopens the editor: its parent now
accounts for **two lifecycles**, using the fixture's existing one-WATCH-per-
startup/test/teardown derivation: `2 * 3 * WATCH_NS` (E, inherited test policy),
matching the pre-existing crash-guard's lifecycle arithmetic. The per-operation
WATCH_NS and every product assertion/deadline remain unchanged. Added a named
child/wait/exit diagnostic. Final whole Windows recheck passed below.

Linux required check exited 0 (`r2-linux-check.log`): emitted
`check: 117 test binaries passed in 183.8 s (parallel, 16 jobs, 6 displays)`
(M, suite enumeration and runner monotonic elapsed; no timing gate). Display-lock
contract output deliberately includes nested timeout/failure cases before PASS.
Final `make -j16 all` also exited 0 (`r2-linux-all-final.log`).

Editor TRACK bench exited 0 on a fresh locked private Xvfb, reusing
`bench/editor_bench.args`: `--track --no-idle --no-p4 --serial-only --keys=64`
(E, existing module sample policy). Raw log `r2-editor-bench.log`. Context (M)
[AC, desktop Spiral], Tobias using the box; wrapper uptime recorded in that log.
BAT0 absent makes the tool print unknown power; desktop AC comes from the brief.
All emitted rows report structural OK, allocations=0, exact mutations/journal;
GL uses the observed raster fallback on this Xvfb. TRACK percentiles are not a
Windows/live-desktop performance claim. The Windows editor's complete release
suite also reports its existing exact journaled-typing/zero-malloc campaign;
final full-check results follow below.

First full Windows check completed with exit 2 (`r2-win-check.log`):
`binaries=130 desktop-passes=6 jobs=4 seconds=647.682 failures=5` (M, runner
input enumeration/result count and elapsed). Failures: editor_exe_test in both
configurations; journal_kill_test in both configurations at the previously
reported randomized-writer `reached && st.how==OS_PROC_ABNORMAL` / native 258
boundary; and the new sanitizer journal selector's parent watchdog. Release
editor_win_test completed the entire expanded suite, exit 0. The final recheck
includes the watchdog correction; no journal or executable fixture was changed.

Final recheck progress (M): complete editor_win_test, editor_exe_test and
journal_kill_test now all pass in both release and sanitizer configurations.
The unchanged executable and journal failures did not recur; their earlier
failures and the HEAD countercheck remain recorded above. The final aggregate full-run exit is 0, as recorded below.

### Round 2 final verification — done

Latest required commands and captured exits (M):

| Command | Exit | Log |
|---|---|---|
| `taskset -c 16-31 nice -n 10 make -j16 all` | **0** | `build/worker-logs/r2-linux-all-final.log` |
| `taskset -c 16-31 nice -n 10 env DEBUGINFOD_URLS= make -j16 check` | **0** | `build/worker-logs/r2-linux-check.log` |
| `taskset -c 16-31 nice -n 10 env WIN_CHECK_JOBS=4 make TARGET=win -j16 check` | **0** | `build/worker-logs/r2-win-check-final.log` |
| Existing editor TRACK bench arguments, same CPU prefix, fresh private Xvfb | **0** | `build/worker-logs/r2-editor-bench.log` |
| Linux preprocessor comparison, normalized whitespace/assertion source lines | **0** | `build/worker-logs/r2-linux-preprocess.log` |
| `git diff --check` | **0** | final read-only check |

Final Windows checker output prefix (M):
`Windows check: binaries=130 desktop-passes=6 jobs=4 seconds=637.638 failures=0`.
Derivation: checker enumerates release/sanitizer inputs, extra private-desktop
passes and compares actual exits to expected exits; elapsed is monotonic run
wall time, not a budget. It emits the existing explicit SKIP census. Both
complete editor configurations pass all added WSL selectors, including exact
dirty journal restore, Windows Save As/re-save, case-distinct tabs, real find,
no provider stat during typing, and the optional **real UNC read**. Native
presentation remains an explicit live-desktop SKIP; none was attempted.

Changed production files: `src/editor/buffers.c`, `commands.c`, `editor.c`,
`findpanel.c`, `large.c`, `session.c`, `source_win.c`, and `private.h`.
Other changes: `tests/win/editor_win_test.c`, `mk/win.mk`, this report, and
`docs/decisions/edit-177r.28.md`. No file/OS/save-controller implementation,
Linux behavior, external service, real profile, or WSL write route changed.
No state-changing git command; coordinator owns the commit.

### Round 2 Found, not fixed / open questions

- Initial editor_exe_test failures reproduced with unmodified HEAD editor
  implementations and disappeared on the final full run. Root cause is not
  established. The initial failure and baseline evidence remain above; no
  executable fixture or product parking/input workaround was added.
- Initial journal_kill_test randomized progress deadlines reproduced the
  earlier report's failure boundary; final full release and sanitizer runs
  both pass. No journal implementation or campaign/deadline change.
- The Windows checker still prints old integration SKIP labels claiming the
  editor/file modules are unported. Those unrelated labels were left alone;
  they do not describe the real-editor WSL assertions added here.
- No product decision is outstanding. WSL saving remains out of scope; active
  copied sources use the requested focus/activation/explicit-command checks,
  with no recurring polling timer and no Linux-side save helper.

All started build/test/benchmark sessions have completed. Final context (M)
[AC, desktop Spiral], Tobias using the box, is recorded by the final uptime
below. No live desktop interaction or Windows write to a WSL share occurred.
 16:45:55 up  8:17,  1 user,  load average: 1.88, 0.95, 2.41
