# edit-hw1 — worker s8

Implemented the coordinator's open-peer Save As policy. A modified peer
refuses the operation before writing, with the requested named notice. A
clean peer reloads through the existing source replacement/checkpoint path.
The saving tab retains its ordinary Save As path.

No bd or git commands were run. All work stays in this worktree. Protected
handoff, worklog, rules, plan and requirements files were not edited.

Exit codes and diagnostic values below are observed (M)[AC]. `Not charging`
is the repository's AC convention. Line numbers, compiler flags, signal
names and bead names are identifiers. Fixture lengths derive from literal
text; capacities reuse existing constants; structural sentinels and tab/pipe
indexes are (P), as documented beside the tests and notice storage.

## Files touched

- `src/editor/commands.c`: pre-write modified-peer admission, named refusal
  notice, and clean peer reload after Save As.
- `src/editor/buffers.c`: share the existing path comparison and clean reload
  helpers with command maintenance.
- `src/editor/private.h`: private helper declarations and prepaid notice storage.
- `tests/dataloss_test.c`: dirty/clean peer kill-and-relaunch regressions,
  individually selectable and included in the normal suite.
- `docs/decisions/edit-hw1.md`: coordinator policy and implementation choices.
- `docs/worker-reports/edit-hw1-s8.md`: this report.

## Red run

The tests were written and run before production changes. The dirty case kills
at the existing journal rename seam, after the target write and before
checkpoint publication. The clean case kills after the command returns.
Both print recovery results before asserting, so the red evidence includes
the actual loss/conflict rather than just an earlier UI assertion.

```text
$ make -j3 build/san/tests/dataloss_test
exit=0
$ make -j1 build/san/tests/dataloss_test
exit=0
$ cat /sys/class/power_supply/BAT0/status
Not charging
$ uptime
 08:52:49 up 6 days, 13:40,  1 user,  load average: 68.40, 64.49, 53.61
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --shared-save-dirty
dataloss_test: shared_save_as(dirty) restored build/dataloss-G9l3dd/source modified=1 text=[Asource]
dataloss_test: shared_save_as(dirty) restored build/dataloss-G9l3dd/target modified=0 text=[Asource]
dataloss_test: shared_save_as(dirty) refused=0 untouched=0 peer_clean=0 disk=[Asource] set_aside=1 notice=[file changed on disk since this session; its unsaved edits were set aside]
dataloss_test:719: FAIL ok
exit=1
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --shared-save-clean
dataloss_test: shared_save_as(clean) restored /home/tobias/Projects/sublimite/.wt/edit-hw1-s8/build/dataloss-wTTBn2/target modified=0 text=[Asource]
dataloss_test: shared_save_as(clean) restored build/dataloss-wTTBn2/target modified=1 text=[base]
dataloss_test: shared_save_as(clean) refused=0 untouched=0 peer_clean=0 disk=[Asource] set_aside=0 notice=[]
dataloss_test:719: FAIL ok
exit=1
```

The initial sandboxed red run also produced LeakSanitizer's ptrace fatal
error after the intended assertions. The red runs pasted above were repeated
outside that restriction with leak detection enabled and no sanitizer errors.

## Green run and verification

```text
$ cat /sys/class/power_supply/BAT0/status
Not charging
$ uptime
 08:57:36 up 6 days, 13:45,  1 user,  load average: 63.40, 65.22, 56.94
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --shared-save-dirty
dataloss_test: shared_save_as(dirty) restored /home/tobias/Projects/sublimite/.wt/edit-hw1-s8/build/dataloss-LXZZAQ/source modified=1 text=[Asource]
dataloss_test: shared_save_as(dirty) restored /home/tobias/Projects/sublimite/.wt/edit-hw1-s8/build/dataloss-LXZZAQ/target modified=1 text=[Bbase]
dataloss_test: shared_save_as(dirty) refused=1 untouched=1 peer_clean=0 disk=[base] set_aside=0 notice=[]
exit=0
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --shared-save-clean
dataloss_test: shared_save_as(clean) restored /home/tobias/Projects/sublimite/.wt/edit-hw1-s8/build/dataloss-iAEFUj/target modified=0 text=[Asource]
dataloss_test: shared_save_as(clean) restored /home/tobias/Projects/sublimite/.wt/edit-hw1-s8/build/dataloss-iAEFUj/target modified=0 text=[Asource]
dataloss_test: shared_save_as(clean) refused=0 untouched=0 peer_clean=1 disk=[Asource] set_aside=0 notice=[]
exit=0
```

An initial `make -j2 all`
exited with code `2` because gcc's warn_unused_result rejected an unchecked
write in the new crash hook. The hook now checks that return value; no
production behavior changed for that compiler correction.


## Verification commands

The release compiler is gcc 13.3.0; the sanitizer compiler is clang 18.1.3
(observed `--version` identifiers). The named sanitizer targets use the
Makefile's C11/check configuration with the required warning flags and
ASan/UBSan; leak detection remains enabled outside the ptrace restriction.
All make invocations used the requested concurrency cap or less.

| Command | Exit code (M)[AC] |
| --- | --- |
| `make -j2 all` (initial attempt; crash-hook warning described above) | `2` |
| `make -j2 all` (after checking the hook write) | `0` |
| `make -j3 all` | `0` |
| `make -j1 build/san/tests/dataloss_test build/san/tests/editor_session_test build/san/tests/editor_test build/san/tests/journal_test` | `0` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test` | `0` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test` | `0` |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test` | `0` |
| `DISPLAY=:99 xdpyinfo` (read-only reachability check) | `0` |
| locked sanitizer `editor_session_test`, initial attempt (lock timeout; test not entered) | `1` |
| locked sanitizer `editor_session_test`, retry (lock timeout; test not entered) | `1` |
| locked sanitizer `editor_test`, initial attempt (lock timeout; test not entered) | `1` |
| locked sanitizer `editor_test`, retry (lock timeout; test not entered) | `1` |
| `ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_FD="$hw1_display_fd" XDG_RUNTIME_DIR="$hw1_runtime" DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test` (after acquiring the same lock with `flock -x`) | `0` |
| equivalent inherited-lock sanitizer `editor_test`, first entered run (assertion below; then terminated) | `143` |
| `ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_FD="$hw1_display_fd" XDG_RUNTIME_DIR="$hw1_runtime" DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test` (fresh full run after acquiring the same lock with `flock -x`) | `0` |

The full data-loss suite passed on its initial run; the known edit-1wc
`two_process_save` race did not require a rerun. Its new dirty and clean
cases reported the same successful outcomes as the green runs above.

Release editor verification passed with the active allocation guard:

```text
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
editor_test: all passed
release editor_test exit=0
```

The observed allocation counts come from the existing native translation and
mutation-to-submit guard; the expected zero derives from the binding
no-allocation rule (P).

The initial sanitizer editor/session attempts and their polling retries timed
out in the shared lock before entering the tests. Xvfb itself was reachable.
The final session attempt passed: Bash allocated a lock descriptor, blocking
`flock -x` acquired the same `/tmp/edit-xvfb-99.lock`, and the unchanged helper
validated/reused it through its supported `EDIT_DISPLAY_LOCK_FD` mechanism.
This retained exclusive display ownership. The sanitizer editor's entered run
hit the existing replay-input settle watchdog below. Its clean-process repeat
passed with unchanged code and leak detection enabled.

Each display command uses a private `XDG_RUNTIME_DIR` made with `mktemp -d`,
`DISPLAY=:99`, and `sh tools/with_display_lock.sh`; temporary runtime
directories are removed on exit. Power/load and `df -h /` excerpts from the
successful session run follow (M)[AC]:

```text
Waiting for the shared display lock using blocking flock; the test helper inherits the same descriptor.
Not charging
 09:23:08 up 6 days, 14:11,  1 user,  load average: 55.57, 44.30, 44.92
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   73G  85% /
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed
editor_session_test inherited lock exit=0
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   73G  85% /
Not charging
 09:23:56 up 6 days, 14:12,  1 user,  load average: 55.95, 45.83, 45.40
```

The existing large session sources are sparse files made with `ftruncate`.
The existing legacy-orphan scenario intentionally materializes whole-text
journals; that pre-existing fixture behavior was not changed in this bead.
Its cleanup wrappers ran, and post-run glob checks found no `editor-qct-*`,
`editor-fm4-*`, or `editor-c26-*` fixture directories remaining in `build`.

## Out-of-scope verification finding

The entered sanitizer editor run hit `settle(e)` in the existing
`review_replay_slices` typing batch (`tests/editor_test.c:2028`, watchdog
assertion at `tests/editor_test.c:67`). That function does not perform Save As.
The load and power stamps are below; causation was not investigated. The run
continued without completing after the assertion, so only its identified
editor_test process in this worktree was terminated with SIGTERM to release
the display lane. No other worker's process was signalled. No existing test,
watchdog, or unrelated implementation was changed. The fresh full-suite run
passed with unchanged code and watchdogs. The synthetic journal left by the
failed fixture was removed after all editor test processes exited.

```text
Not charging
 09:27:07 up 6 days, 14:15,  1 user,  load average: 65.14, 56.53, 49.80
editor_test:67: FAIL false
editor_test:2028: FAIL settle(e) == 0
Terminated
san editor_test inherited lock exit=143
Not charging
 09:32:08 up 6 days, 14:20,  1 user,  load average: 72.12, 65.35, 55.45
```

The successful fresh sanitizer editor run completed with no sanitizer
errors. Its final power/load and result excerpt is:

```text
Not charging
 09:32:08 up 6 days, 14:20,  1 user,  load average: 72.12, 65.35, 55.45
editor_test: all passed
san editor_test repeat exit=0
Not charging
 09:39:15 up 6 days, 14:27,  1 user,  load average: 49.76, 54.89, 54.65
```

## Remaining scope

Implementation and all required verification are complete. No required work remains.

No new PREPARE protocol is introduced. The edit-o20 synchronous-maintenance
note and transactional Save As publication remain outside this bead.
Edit-fi2 (immutable recovery original) is also outside scope and untouched.
Retained closed tabs keep the preceding reconciliation policy; the requested
policy applies to open peers. The existing edit-1wc two_process_save timing
race was not encountered; that fixture was not changed.
