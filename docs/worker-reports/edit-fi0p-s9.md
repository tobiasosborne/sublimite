# edit-fi0p worker report

Worktree: `/home/tobias/Projects/sublimite/.wt/edit-fi0p-s9`.
No `bd` or `git` commands used. Work remains within this bead.

Implemented F4, F7, F8, F9, F11 and F12 with red/green regressions. The
broader coverage-gap campaign remains as listed below. Final gcc build and
release editor malloc guard pass; the selected sanitizer suites pass except
for the CLI cleanup assertion, which failed on its rerun as well.

## Red/green evidence so far

The sanitizer builds use the Makefile's C11 warning flags, ASan and UBSan.
Runs use `ASAN_OPTIONS=detect_leaks=1`, `DISPLAY=:99`, a private temporary
`XDG_RUNTIME_DIR`, and `EDIT_DISPLAY_LOCK_TIMEOUT=3600` under
`tools/with_display_lock.sh`, outside the sandbox. Display number and lock
timeout are prescribed by the brief. Exit codes and source-line numbers
below are observed results (M)[AC], not budgets.

F4 red, `build/san/tests/editor_checkpoint_test fi0p-barrier`:

```text
editor_checkpoint_test:89 FAIL editor_flush(e) == 0
editor_checkpoint_test: 1 case(s) failed
exit=1
```

F4 green, same command:

```text
editor_checkpoint_test: fi0p flush repairs failed checkpoint name barrier
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed
exit=0
```

F7 red, `build/san/tests/editor_recovery_test`, original implementation with
the final deterministic split-read test:

```text
editor_recovery_test:69 FAIL restored == JOURNAL_BASE_CHANGED
editor_recovery_test:69 FAIL restored == JOURNAL_BASE_CHANGED
exit=1
```

F7 green, same command:

```text
editor_recovery_test: rejects BASE rewritten before bulk read
editor_recovery_test: rejects BASE rewritten during bulk read
exit=0
```

F8 red, `build/san/tests/dataloss_test --fi0p-closed-peer`:

```text
dataloss_test:814: FAIL refused
exit=1
```

F8 green, same command:

```text
dataloss_test: fi0p Save As refuses a modified closed peer before target write
exit=0
```

F9 red, `build/san/tests/dataloss_test --fi0p-retain-retry`:

```text
dataloss_test:1080 FAIL e->buffer->retain_state != EDITOR_RETAIN_FAILED
exit=1
```

Initial test-authoring compilation errors were corrected before the red
behaviour runs. F7's initial after-read rewrite was changed to a split read:
a completed old-generation read is itself safe when adjacent writes share
filesystem timestamps; the final test deterministically reads mixed bytes.

Final verification and scope accounting follow below.

F9 green, same command (the successful retry performs the copy and promotion
metadata barriers, so their count is derived from those transitions):

```text
dataloss_test: fi0p failed retention retries after mutation and protects BASE from rewrite
exit=0
```

F11 red, `build/san/tests/dataloss_test --fi0p-lazy-active`:

```text
dataloss_test:1662 FAIL tabs_count(&e->tabs) == 2 && tabs_active_index(&e->tabs) == 1
exit=1
```

F12 red, `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_ack_test`:

```text
journal_replay: error=2; source journal retained
journal_replay: error=2; source journal retained
journal_replay: error=2; source journal retained
journal_replay: error=2; source journal retained
journal_ack_test:65 FAIL acknowledge(source) == 0 && access(source, F_OK) != 0
exit=1
```

Initial `make -j3 all` caught gcc's format-truncation warning in the new
closed-peer notice. The notice was shortened to fit existing storage; the
next all build exited successfully. Final builds follow after all changes.

F11 green, same command:

```text
dataloss_test: fi0p lazy restore and relaunch preserve the second active tab
dataloss_test: fi0p lazy active id survives TABS deduplication and relaunch
exit=0
```

Power evidence: `cat /sys/class/power_supply/BAT0/status` returned `Full`;
AC adapter `online` returned `1` (M)[AC]. Long shared-display lock waits are
visible in the raw logs; their configured timeout comes from the brief.

F12 green, same command (the CLI parser is also compiled into the sanitizer
test, so a clean `make check` does not depend on a separately built tool):

```text
journal_replay: acknowledgement error=4; source retained unless unlink completed
journal_replay: acknowledgement error=4; source retained unless unlink completed
journal_replay: acknowledgement error=2; source retained unless unlink completed
ACKNOWLEDGED build/journal-fi0p-ack-CJd9lY/.sublimite-journal-!1-1-1.set-aside-1
journal_ack_test: explicit verified acknowledgement releases reserved source and orphan BASE cleanup
exit=0
```

The API additionally preserves symlinks and torn sources. The fixture name's
unit fields follow the reserved-name schema; the orphan length is the existing
artifact cap plus the smallest positive byte increment (P).

## Files touched

- `src/editor/editor.c`: off-path journal retry before complete checkpoint.
- `src/editor/recovery.c`: validate the descriptor and pathname across full
  BASE materialisation and compare the actual prefix with recorded BASE.
- `src/editor/commands.c`: refuse Save As over modified retained closed peers.
- `src/editor/private.h`, `src/editor/retain.c`: per-buffer failed revision and
  mutation-triggered copy retry.
- `src/editor/restore.c`: preserve and install the lazy active source ID.
- `src/journal/journal.h`, `src/journal/retire.c`: explicit verified reserved
  set-aside acknowledgement API.
- `tools/journal_replay.c`: standalone `--acknowledge` operation and usage.
- `tests/editor_checkpoint_test.c`, `tests/editor_recovery_test.c`,
  `tests/dataloss_test.c`, `tests/journal_ack_test.c`: regressions above.
- `docs/decisions/edit-fi0p.md`, this report.

Generated build artifacts and raw logs stay under this worktree. No protected
planning, handoff, worklog or repository-rule files were edited.

## Remaining scope

The listed behaviour fixes are implemented in the brief's order. The review's
broader coverage-gap campaign is larger than this implementation bead; it is
not claimed complete. Specifically, this worker added editor-flush barrier
failure/retry, eager mixed-generation reads, modified closed-peer Save As
refusal, asynchronous failure/mutation retry plus external-rewrite recovery,
lazy multi-buffer active selection/deduplication/relaunch, and explicit
acknowledgement/cleanup coverage. It did not add the following campaigns from
`docs/reviews/recovery-3.md`:

- Kill points throughout lease creation, mutation-to-stage, combined group
  framing/writeback repair, and every rotation-temp publication/adoption step.
- Post-rename checkpoint failure through lazy rollback, retention publication
  and close, plus kills at asynchronous RUNNING/READY/publication and save
  publication boundaries.
- Full asynchronous copy cancellation/fault matrices; PREPARE/FINISH and
  cancel namespace failures; release a held target write after an intermediate
  UI checkpoint and kill before FINISH.
- Broader Save As rename-to-checkpoint crashes, mapped-source final-checkpoint
  failure, actual SIGBUS during recovery/checkpoint/save followed by kill,
  failures after individual lazy swaps and source unlink/barrier kills.
- Ordinary artifact retirement and manual replay barrier/rename failure-kill
  matrices and final validation rewrite races. Acknowledgement's post-unlink
  parent-barrier failure/kill matrix also remains untested.

Coverage associated with the separately owned review findings remains owned
by the cited beads; their existing regression suites are part of validation
here, not a new review or an independent claim to close those beads. No new
review pass was performed. No out-of-scope production issue was fixed.

Additional original coverage gaps not newly exercised here include the
no-journal lazy-restoration byte assertion and the repeated mapped-save/Keep
combinations. Optimistic full-read validation uses the existing identity and
prefix model; this bead does not introduce filesystem snapshot isolation.

## Verification commands

`make -j3 all` exited `0` after the final source and test changes (M)[AC].
Raw output: `build-fi0p-all-final.log`. gcc release builds retain
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.

The check-style sanitizer target build exited `0` (M)[AC]:

```sh
make -j3 build/san/tests/editor_test build/san/tests/editor_scroll_test \
  build/san/tests/editor_large_test build/san/tests/editor_session_test \
  build/san/tests/editor_hot_exit_test build/san/tests/cli_test \
  build/san/tests/raster_test build/san/tests/editor_checkpoint_test \
  build/san/tests/editor_recovery_test build/san/tests/dataloss_test \
  build/san/tests/journal_test build/san/tests/journal_group_test \
  build/san/tests/journal_kill_test build/san/tests/journal_retire_test \
  build/san/tests/journal_ack_test build/san/tests/savectl_test
```

The final acknowledgement test harness was rebuilt alongside `all` after
removing its dependency on an external release tool; that build also exited
`0` (M)[AC]. Raw build logs: `build-fi0p-san-build.log`,
`build-fi0p-final-build.log`.

The suite runner is `sh build/fi0p-verify.sh`, outside the sandbox, with
`ASAN_OPTIONS=detect_leaks=1`, `DISPLAY=:99`,
`EDIT_DISPLAY_LOCK_TIMEOUT=3600`, and a private `mktemp -d` runtime directory.
It runs the display/editor/data-loss suites through
`sh tools/with_display_lock.sh build/san/tests/<name>`; headless journal and
save-controller suites run directly. It stamps power and uptime adjacent to
the suites' timing output, and `df -h /` before and after the sparse fixtures.
Raw results: `build-fi0p-verification.log`.

Tracking benchmarks exited `0` (M)[AC]:

```sh
build/bench/journal_bench --track
DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 sh tools/with_display_lock.sh \
  build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=64
```

The editor arguments are the existing `bench/editor_bench.args` configuration,
not a new sample count. Power/uptime stamps and tracked target misses are in
`build-fi0p-journal-bench.log` and `build-fi0p-editor-bench.log`. Correctness
and allocation accounting passed; latency misses are tracked on the shared
box.

## Finding outside the implementation scope

`cli_test` failed its cleanup assertion at `tests/cli_test.c:581` after the
native typing/relaunch text, view and window-geometry checks passed:

```text
cli_test:581: FAIL closed == 0 && rmdir(dir) == 0
cli_test exit=1
```

A separate rerun with the same mandated environment and display lock failed
the same assertion and exited `1` (M)[AC]; raw output is
`build-fi0p-cli-rerun.log`. The suite's earlier forced startup-timeout failure
is an intentional control and was reported as correctly failed. The final
cleanup assertion is the unresolved verification failure. No attribution to
this bead is established, and no unrelated cleanup fix or new review pass
was made. The suite's own cleanup removes its fixture.

## Final observed suite results

All exit codes in this table are command results (M)[AC].

| Command (runner environment above) | Exit |
|---|---|
| `build/san/tests/journal_test` | `0` |
| `build/san/tests/journal_group_test` | `0` |
| `build/san/tests/journal_kill_test` | `0` |
| `build/san/tests/journal_retire_test` | `0` |
| `build/san/tests/journal_ack_test` | `0` |
| `build/san/tests/savectl_test` | `0` |
| `build/san/tests/editor_recovery_test` | `0` |
| `build/san/tests/editor_checkpoint_test` | `0` |
| `build/san/tests/dataloss_test` | `0` |
| `build/san/tests/editor_session_test` | `0` |
| `build/san/tests/editor_hot_exit_test` | `0` |
| `build/san/tests/editor_scroll_test` | `0` |
| `build/san/tests/editor_large_test` | `0` |
| `build/san/tests/cli_test` | `1` |
| `build/san/tests/raster_test` | `0` |
| `build/san/tests/editor_test` | `0` |
| `sh tools/with_display_lock.sh build/tests/editor_test` | `0` |
| `build/san/tests/cli_test` (separate rerun) | `1` |

`sh build/fi0p-verify.sh` exited `1` because the CLI cleanup assertion failed;
it continued through the other suites and the release malloc guard. No
ASan, UBSan or LeakSanitizer error was reported. The display socket was
reachable, and the required display suites ran on the prescribed display.

Toolchain observations (M)[AC]: gcc reported `13.3.0`; clang reported
`18.1.3`. The release guard was active and its allocation checks passed.

`df -h /` before the existing sparse large-fixture suites (M)[AC]:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
```

`df -h /` after the suite runner and fixture cleanup (M)[AC]:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  394G   62G  87% /
```

A final generated-file check found no GB-scale fixture remaining in `build/`.
This bead added no GB-scale fixture; its orphan-cleanup fixture is sparse and
uses the existing artifact cap plus a byte. Its cleanup runs on both success
and failure.
