# edit-1t2 — s8 worker report

Completed: the release build and required leak-enabled repetition pass; the
held-lock contract prints its permitted skip and succeeds.

Work is confined to this worktree and this bead. No `bd` or `git` command was
run. The protected handoff, worklog, rules, plan and requirements files were
not edited. The file implementation and the shared display-lock wrapper were
not changed.

Files touched:

- `tests/file_test.c`: an unrelated-event backlog regression and a bounded
  notification wait that continues dispatching and decoding completion.
- `tools/test_display_lock.sh`: a derived initial acquisition bound, a successful
  contention skip, and inheritance checks under continuous holder ownership.
- `docs/decisions/edit-1t2.md`: derivations and design choices.
- This report. Build logs and reproduction scripts are in `build/edit-1t2/`.

Evidence convention: captured iteration indices, source locations, exit codes,
load samples and test output numbers below are (M)[AC]. Run population and
compiler/build options come from the binding brief; no new performance gate
is introduced. Power and uptime stamps accompany the runs. Fixture size and
watchdog derivations are recorded next to the test and in the decision.

The shared service consumes a bounded inotify batch per dispatch, coalesces
worker identity checks, and installs results only when its mailbox is decoded.
The old test dispatched once and then only drained the mailbox. That can leave
the target event behind unrelated parent-directory events, or leave a deferred
check without its required later pump. The regression puts unrelated events
ahead of the target write without sleeping. The replacement wait polls both
the inotify descriptor and worker eventfd, decodes results and retries dispatch
until the expected cached flags are installed. It does not call `file_check`.
Both fixtures append bytes and close the writer before waiting, so identity
changes include size regardless of mtime granularity. Readiness alone neither
identifies the target event nor completes the asynchronous identity check.

The contract's acquisition failure was reproduced before its change:

```text
$ EDIT_DISPLAY_LOCK_TIMEOUT=0.1 sh tools/test_display_lock.sh contract
Not charging
 09:13:27 up 6 days, 14:01,  1 user,  load average: 39.76, 37.38, 45.67
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
contract red exit=1
```

The red probe reused the existing contract's short timeout (E); it did not wait
out the wrapper's operational default. The green acquisition bound is
(E) ceil(107.69 s) = 108 s, from the longest recorded passing display suite,
`editor_test`, (M)[AC] in `docs/worker-reports/edit-qct-s7.md`, “Other suites”.
The green run used that bound while another process retained the real shared
lock:

```text
$ sh tools/test_display_lock.sh contract
Not charging
 09:14:36 up 6 days, 14:02,  1 user,  load average: 43.33, 38.59, 45.51
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
display lock contract: SKIP shared display :99 held longer than 108s
contract exit=0
Not charging
 09:16:24 up 6 days, 14:04,  1 user,  load average: 49.18, 41.65, 45.80
```

The original, unmodified sanitizer binary was preserved before rebuilding the
fixture. `sh build/edit-1t2/run-baseline.sh` ran it with
`ASAN_OPTIONS=detect_leaks=1`, printing the iteration and load before each run
and stopping at the first failure. The failing iteration is pasted below;
prior iterations passed. This is the requested spontaneous reproduction,
separate from the deterministic fixture.

```text
Not charging
 09:24:38 up 6 days, 14:12,  1 user,  load average: 66.16, 49.78, 46.75
baseline iteration=10 load1=66.16
astra_watch_many: requested=200 watching=200 inotify_instances=1
tests/file_test.c:308: FAIL file_changed(files[targets[0]]) && file_changed(files[targets[1]])
P1-1 section 2: ok
P1-1 section 3: ok
finding1: ok
file_test: ok
file_test: ok
file_test: ok
file_test: ok
finding2: ok
finding3: ok
finding4: ok
finding4 source identity: ok
finding4 invalidated generation: ok
finding4 forced late fault: ok
finding5: ok
finding7: ok
finding8: ok
finding9: ok
finding11: ok
finding11 scan boundaries: ok
finding12: ok
finding13: ok
finding13 queued commit invalidation: ok
finding16 copy: ok
finding24: ok
finding25: ok
finding31: ok
finding32: ok
finding17: ok
finding18: ok
finding18 injected reads: ok
finding18 entry race: ok
finding19 missing: ok
finding19 I/O: ok
finding19 intact generations: ok
finding19 backing I/O: ok
finding19 restored version: ok
finding20: ok
finding20 exact baseline: ok
finding21: ok
finding21 refresh failure: ok
finding22: ok
finding22 explicit 0000: ok
finding22 ACL: ok
finding22 preservation error: ok
finding23: ok
finding23 FORCE: ok
file_test: 1 FAILED
baseline iteration=10 exit=1
```

The regression was written before changing either notification wait. Rebuilding
with `make -j3 build/san/tests/file_test` succeeded, then the leak-enabled red
loop stopped at its first iteration:

```text
Not charging
 09:22:56 up 6 days, 14:11,  1 user,  load average: 52.11, 43.08, 44.54
regression red iteration=1 load1=52.11
tests/file_test.c:369: FAIL file_changed(f)
tests/file_test.c:308: FAIL file_changed(files[targets[0]]) && file_changed(files[targets[1]])
file_test: 2 FAILED
regression red iteration=1 exit=1
```

The complete deterministic red run is `build/edit-1t2/file-red.log`. The first
wait implementation still failed the shared-watch case: retrying only the
unchanged dispatcher did not retry deferred checks for its peers after the fd
was drained. The final helper polls every expected member as well. This follows
the per-file retry API and keeps all status installation in `file_msg_decode`.
The unsuccessful intermediate run is retained in `build/edit-1t2/green-first.log`.

Sandbox-only attempts encountered LeakSanitizer's process-inspection restriction
and failed the signal-child checks. Those are retained in
`build/edit-1t2/sandbox-baseline.log` and `file-regression-red.log`; they are not
counted as notification red/green evidence. Required leak-enabled runs were
repeated outside that sandbox. No source workaround was made.

Data-path observation, left unchanged: `file_watch_dispatch` pumps all peers
when it reads a batch; after the descriptor is drained, `file_watch_poll(f)`
retries the specified member. The test must poll the members whose deferred
checks it awaits. A change to global retry policy belongs outside this bead;
no such file-module change was made. No other product defect was found during
this scoped work. There are no GB-scale fixtures in these changes or runs.

The contract's own retained artifacts are under `build/edit-5cv-evidence/`.
Its permitted contention skip validates the busy-lane behavior; the full
uncontended contract assertions were not reached while the external holder
retained the lane.

Final contract run after the shell comment/quoting correction, using the same
derived bound:

```text
$ sh -n tools/test_display_lock.sh
exit=0
$ sh tools/test_display_lock.sh contract
Not charging
 09:24:34 up 6 days, 14:12,  1 user,  load average: 66.08, 49.49, 46.64
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
display lock contract: SKIP shared display :99 held longer than 108s
contract exit=0
Not charging
 09:26:22 up 6 days, 14:14,  1 user,  load average: 69.26, 55.66, 49.19
```

Final green: `make -j3 build/san/tests/file_test` returned success after the
member-specific retry change. `sh build/edit-1t2/run-green.sh` then ran the
required population with `ASAN_OPTIONS=detect_leaks=1`. The start power/uptime
stamps and exit lines are pasted below; complete case output is in
`build/edit-1t2/green.log`.

```text
Not charging
 09:36:49 up 6 days, 14:24,  1 user,  load average: 48.37, 59.02, 55.77
green iteration=1 load1=48.37
green iteration=1 exit=0
Not charging
 09:37:01 up 6 days, 14:25,  1 user,  load average: 47.77, 58.54, 55.65
green iteration=2 load1=47.77
green iteration=2 exit=0
Not charging
 09:37:12 up 6 days, 14:25,  1 user,  load average: 47.16, 57.87, 55.48
green iteration=3 load1=47.16
green iteration=3 exit=0
Not charging
 09:37:25 up 6 days, 14:25,  1 user,  load average: 47.38, 57.56, 55.40
green iteration=4 load1=47.38
green iteration=4 exit=0
Not charging
 09:37:37 up 6 days, 14:25,  1 user,  load average: 47.00, 56.98, 55.25
green iteration=5 load1=47.00
green iteration=5 exit=0
Not charging
 09:37:51 up 6 days, 14:25,  1 user,  load average: 46.92, 56.64, 55.15
green iteration=6 load1=46.92
green iteration=6 exit=0
Not charging
 09:38:01 up 6 days, 14:26,  1 user,  load average: 47.43, 56.42, 55.10
green iteration=7 load1=47.43
green iteration=7 exit=0
Not charging
 09:38:16 up 6 days, 14:26,  1 user,  load average: 46.90, 55.86, 54.93
green iteration=8 load1=46.90
green iteration=8 exit=0
Not charging
 09:38:29 up 6 days, 14:26,  1 user,  load average: 47.18, 55.47, 54.82
green iteration=9 load1=47.18
green iteration=9 exit=0
Not charging
 09:38:41 up 6 days, 14:26,  1 user,  load average: 46.84, 55.13, 54.72
green iteration=10 load1=46.84
green iteration=10 exit=0
Not charging
 09:38:51 up 6 days, 14:26,  1 user,  load average: 47.08, 54.78, 54.61
green iteration=11 load1=47.08
green iteration=11 exit=0
Not charging
 09:39:04 up 6 days, 14:27,  1 user,  load average: 48.15, 54.75, 54.60
green iteration=12 load1=48.15
green iteration=12 exit=0
Not charging
 09:39:18 up 6 days, 14:27,  1 user,  load average: 50.42, 54.94, 54.67
green iteration=13 load1=50.42
green iteration=13 exit=0
Not charging
 09:39:31 up 6 days, 14:27,  1 user,  load average: 50.13, 54.73, 54.60
green iteration=14 load1=50.13
green iteration=14 exit=0
Not charging
 09:39:46 up 6 days, 14:27,  1 user,  load average: 50.59, 54.60, 54.56
green iteration=15 load1=50.59
green iteration=15 exit=0
Not charging
 09:40:04 up 6 days, 14:28,  1 user,  load average: 50.48, 54.30, 54.46
green iteration=16 load1=50.48
green iteration=16 exit=0
Not charging
 09:40:22 up 6 days, 14:28,  1 user,  load average: 49.69, 53.89, 54.32
green iteration=17 load1=49.69
green iteration=17 exit=0
Not charging
 09:40:35 up 6 days, 14:28,  1 user,  load average: 49.91, 53.79, 54.29
green iteration=18 load1=49.91
green iteration=18 exit=0
Not charging
 09:40:46 up 6 days, 14:28,  1 user,  load average: 49.57, 53.59, 54.21
green iteration=19 load1=49.57
green iteration=19 exit=0
Not charging
 09:40:57 up 6 days, 14:29,  1 user,  load average: 47.16, 52.86, 53.96
green iteration=20 load1=47.16
green iteration=20 exit=0
final twenty-run campaign exit=0
```

All 20 required runs passed (M)[AC]. Load1 ranged from 46.84 to 50.59 (M)[AC], continuing on the heavily loaded host; the spontaneous red iteration started at load1 66.16 (M)[AC].

Verification commands and exit codes (M)[AC]:

| Command | Exit/result |
|---|---|
| `make -j3 all` | `0`, GCC release with the required warnings; `build/edit-1t2/make-all.log` |
| `make -j3 build/san/tests/file_test build/san/tests/refwin_test` | `0`, initial Clang ASan/UBSan build |
| `make -j3 build/san/tests/file_test` | `0`, final-source Clang ASan/UBSan build; `build-green-final.log` |
| `sh build/edit-1t2/run-green.sh` | `0`; each required `ASAN_OPTIONS=detect_leaks=1 build/san/tests/file_test` invocation returned `0` |
| `sh -n tools/test_display_lock.sh` | `0` |
| `sh tools/test_display_lock.sh contract` with the shared lock held externally | `0`, explicit bounded skip; `display-green-final.log` |
| `sha256sum -c build/edit-1t2/unchanged.sha256` | `0`; `src/file/file.c` and `tools/with_display_lock.sh` match their initial hashes |

Bare log names in this table are under `build/edit-1t2/`. Required sanitizer
runs used execution outside the process-inspection sandbox, with leak detection
on. No display suite was used as a substitute for the file checks. The busy
contract self-test does not connect to Xvfb.

Remaining within this bead: none. The permitted skip leaves the uncontended
contract assertions unexecuted during this session, as recorded above. No
additional review pass, source-module fix or other bead was undertaken.
