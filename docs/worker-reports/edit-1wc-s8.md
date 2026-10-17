# edit-1wc-s8 worker report

Worktree: `/home/tobias/Projects/sublimite/.wt/edit-1wc-s8`.

Files authored:

- `tests/dataloss_test.c`: retain the competing-save race, accept only exact
  `BAbase` for reload-then-save, and require B's captured text to match with
  clean state and no save error or stale notice. Add an ordered variant and
  the `--two-process-reload` selector so this outcome is always exercised.
- `docs/decisions/edit-1wc.md`: hook limitation, accepted outcomes and ordered
  fixture protocol.
- `docs/worker-reports/edit-1wc-s8.md`: this report.

Generated evidence is under `build/edit-1wc/`: build/run logs, original and
red-stage source snapshots, and source checksum manifests. No `bd` or `git`
commands were run. No edits were made under `src/` or to the forbidden docs.

Derivations: expected byte lengths are `strlen` of the insertion-derived
literals (P); the ready marker is exactly `sizeof(char)` (P). The ordered
variant uses the existing editor watchdog and introduces no duration. The
requested repetition sample is taken from the bead brief (E); completed
runs and all numeric transcript values below are observations (M)[AC].
`BAT0/status = Not charging` and `AC/online = 1` establish the power stamp.

Red: the ordered fixture was written first, leaving `a_won || b_won`
unchanged. `make -j3 build/san/tests/dataloss_test` succeeded (M)[AC], exit
`0`, both for the initial build and the ordered-fixture rebuild. Then:

```sh
ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 build/san/tests/dataloss_test --two-process-reload
```

`task_runtime` was created with `mktemp -d` and removed by an exit trap.
Verbatim red output (M)[AC], `build/edit-1wc/red.log`:

```text
Not charging
1
 08:46:14 up 6 days, 13:34,  1 user,  load average: 57.72, 57.20, 46.83
dataloss_test: two_process_save reload_before_type=1 disk=[BAbase] A(stale=0 modified=0) B(err=0 stale=0 modified=0 text=[BAbase])
dataloss_test:561: FAIL a_won || b_won
==7==LeakSanitizer has encountered a fatal error.
==7==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==7==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
exit=1
```

The content assertion failed before LeakSanitizer's separate sandbox error.
The subsequent green attempt in the sandbox accepted the content but also
terminated with the ptrace error. Authorized untraced execution allowed
LeakSanitizer to complete with `ASAN_OPTIONS=detect_leaks=1`.

The initial full-suite attempt exposed a fixture dependency: the suite can
fork after the process-wide inotify descriptor is initialized, letting A's
poll consume B's notice. B's ordered variant now calls the existing
`file_check` stat probe after A saves, then pumps the editor until it has
reloaded `Abase`, verifies the clean text, and types B. The original race
variant is unchanged in its scheduling. The attempt is retained in
`build/edit-1wc/green-full-before-explicit-probe.log` (M)[AC], exit `1` at the
reload wait; the watchdog was not changed.

Green: rebuild with `make -j3 build/san/tests/dataloss_test`, then run the same
ordered selector and environment shown above, outside the ptrace sandbox.
Both commands exited `0` (M)[AC]. Final ordered-case output (M)[AC],
`build/edit-1wc/green-ordered-final.log`:

```text
Not charging
AC_online=1
 08:54:41 up 6 days, 13:42,  1 user,  load average: 68.58, 65.86, 55.33
dataloss_test: two_process_save reload_before_type=1 disk=[BAbase] A(stale=0 modified=0) B(err=0 stale=0 modified=0 text=[BAbase])
exit=0
```

Release verification: `make -j3 all` exited `0` (M)[AC], gcc with the repository's
C11 warning flags and `-Werror`; log `build/edit-1wc/make-all.log` and status
`build/edit-1wc/make-all.exit`. The sanitizer target was built by the existing
Makefile rule used by `check`, with clang ASan/UBSan and the same warning
flags; log `build/edit-1wc/build-san-final.log`, status
`build/edit-1wc/build-san-final.exit`.

Scope verification commands:

```sh
rg --files src | sort | xargs sha256sum > build/edit-1wc/src-after.sha256
cmp build/edit-1wc/src-before.sha256 build/edit-1wc/src-after.sha256
```

Exit `0` (M)[AC]: the source file list and hashes match the pre-edit manifest.

Full-suite verification used the same private runtime directory and untraced
execution, with the brief's requested repetition count (E):

```sh
for task_iteration in $(seq 1 10); do
    cat /sys/class/power_supply/BAT0/status
    cat /sys/class/power_supply/AC/online
    uptime
    ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 build/san/tests/dataloss_test
    # The actual runner records the status and stops on any failure.
done
```

Completed `10/10` runs (M)[AC], derived by counting the ordered iteration
exit records below; every exit was `0`. Every run also printed the exact
ordered `BAbase` oracle with B clean and without a save error or stale flag.
No sanitizer diagnostics occurred. The full command exited `0` (M)[AC].
Complete output: `build/edit-1wc/green-full-x10.log`. Power/load and exit
records copied verbatim (M)[AC]:

```text
iteration=1
Not charging
AC_online=1
 08:54:52 up 6 days, 13:42,  1 user,  load average: 66.76, 65.57, 55.40
iteration=1 exit=0
iteration=2
Not charging
AC_online=1
 08:55:26 up 6 days, 13:43,  1 user,  load average: 68.83, 66.16, 55.92
iteration=2 exit=0
iteration=3
Not charging
AC_online=1
 08:56:01 up 6 days, 13:44,  1 user,  load average: 67.93, 66.29, 56.35
iteration=3 exit=0
iteration=4
Not charging
AC_online=1
 08:56:37 up 6 days, 13:44,  1 user,  load average: 64.83, 65.69, 56.57
iteration=4 exit=0
iteration=5
Not charging
AC_online=1
 08:57:13 up 6 days, 13:45,  1 user,  load average: 65.18, 65.65, 56.89
iteration=5 exit=0
iteration=6
Not charging
AC_online=1
 08:57:50 up 6 days, 13:45,  1 user,  load average: 62.70, 64.98, 56.99
iteration=6 exit=0
iteration=7
Not charging
AC_online=1
 08:58:29 up 6 days, 13:46,  1 user,  load average: 61.43, 64.37, 57.12
iteration=7 exit=0
iteration=8
Not charging
AC_online=1
 08:59:04 up 6 days, 13:47,  1 user,  load average: 60.56, 63.83, 57.21
iteration=8 exit=0
iteration=9
Not charging
AC_online=1
 08:59:40 up 6 days, 13:47,  1 user,  load average: 60.32, 63.42, 57.32
iteration=9 exit=0
iteration=10
Not charging
AC_online=1
 09:00:17 up 6 days, 13:48,  1 user,  load average: 55.99, 61.86, 57.05
iteration=10 exit=0
passes=10/10
```

Remaining work: none for edit-1wc. No display suite was required by this
headless dataloss-only brief. No GB-scale fixture was used. The small fixture
leftovers from the deliberately failing red run and the intermediate fixture
failure were removed after the final run; generated build evidence remains.

Outside the bead: sandboxed LeakSanitizer reports that it cannot run under
ptrace, including after a clean content assertion. The required checks were
therefore run through authorized untraced execution with leak detection still
enabled. No production changes or other fixes were made.
