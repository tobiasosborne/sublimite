# edit-52o worker report, session s7

Implemented the rotation-temp sweep and covering rotation lease. Design:
[edit-52o](../decisions/edit-52o.md). Work was confined to this worktree;
no `bd` or `git` commands were run, and the prohibited coordination files
were not edited.

A dead temp is deleted without reading its content when its exact original
basename still names the recorded device/inode/ctime generation. A missing
original is preserved once by a non-replacing set-aside rename. Historical
names do not record an original, so those ambiguous temps are also set aside
once. This deliberately preserves their disk bytes: the historical profile's
ambiguous debris needs independent confirmation before manual deletion.
Very long original names use that same conservative fallback. No real profile
was accessed.

## Files touched

- `src/journal/journal.c`: directory-inode shared flock covers temp creation,
  publication, sync and cleanup. Names include the original identity and hex
  basename, with capacity reserved for set-aside naming; long names retain the
  independent legacy form.
- `src/journal/retire.c`: sweep before inventory; strict name parsing; exact
  original lookup; content-free unlink/set-aside; sweep existing stray temp
  locks without creating any; preserve unscanned references for surviving temps.
- `src/journal/journal.h`: document temp exceptions to ordinary session retirement.
- `tests/journal_retire_test.c`: actual paused rotation, live/dead lease,
  missing-original preservation/idempotence, stray lock removal, legacy live
  owner protection and fresh-destination exclusion. New fixtures use one sealed
  JOURNAL_PAGE or a deliberately incomplete JNL1 page. The observed red and
  green runs left no rotation fixtures behind.
- `fuzz/journal_fuzz.c`: directory-state retirement oracle for surviving,
  missing/changed/wrong-name originals, legacy, live, set-aside, lexical and
  overflow cases, including invalid encoded basenames.
- `docs/decisions/edit-52o.md`.
- This report.

No new duration, storage budget or collision count was introduced. Numeric
protocol/exit values and source coordinates below are (P) structural; fixture
sizes derive from JOURNAL_PAGE, payload types and literal lengths. Run counts
and benchmark numbers are (M)[AC], taken from the captured outputs; BAT0 was
`Not charging` during final verification. Build logs are under `build/edit-52o-*`.

## Red, before implementation

```
$ make -j4 build/tests/journal_retire_test
exit 0
$ build/tests/journal_retire_test
rotation_temps: live temp gained a lock or set-aside
journal_retire_test:430 FAIL journal_retire_directory(root,NULL)==0 && access(temp,F_OK)<0 && access(stray,F_OK)<0
rotation_temps: live temp gained a lock or set-aside
journal_retire_test:427 FAIL unlink(path)==0
legacy_rotation_temp: not set aside without a temp lock
journal_retire_test:474 FAIL rotations==0
exit 1
```

The old live sweep created temp locks and could retire the empty original
while the actual rotation was paused. Dead temps remained. Legacy temp content
was incorrectly treated as a journal instead of receiving name-only handling.

## Green, final implementation

```
$ build/tests/journal_retire_test
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=1190174 discovery_after_ns=8145 load1=10.1
journal_retire_test: passed
exit 0
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=1628433 discovery_after_ns=16478 load1=9.01
journal_retire_test: passed
exit 0
```

The sanitizer commands below ran with process-inspection restrictions lifted,
with `ASAN_OPTIONS=detect_leaks=1` throughout. No leaks were suppressed.
The initial sandboxed `ASAN_OPTIONS=detect_leaks=1 make -j4 check` stopped at
`base_test` with LeakSanitizer's process-inspection/ptrace fatal error, so it
could not verify the suite inside the sandbox.

The final build command was `make -j4 all build/fuzz/journal_fuzz
build/san/tests/journal_retire_test build/san/tests/journal_test
build/san/tests/journal_kill_test build/san/tests/editor_session_test
build/san/tests/dataloss_test`: exit 0, GCC release and Clang ASan/UBSan,
C11 with Wall/Wextra/Werror/Wshadow/Wconversion. Full output:
`build/edit-52o-final-build.log`.

| Final required sanitizer suite | Result, leaks enabled |
| --- | --- |
| journal_retire_test | PASS |
| journal_test | PASS |
| journal_kill_test | PASS |
| editor_session_test | PASS |
| dataloss_test | Known edit-1wc two-process-save race, then PASS on the permitted retry |

The data-loss suite also passed in the earlier broad run. The final first run
and the single retry were retained separately:

```
dataloss_test: two_process_save disk=[BAbase] A(stale=0 modified=0) B(err=0 stale=0 modified=0 text=[BAbase])
dataloss_test:546: FAIL a_won || b_won
dataloss_test:643: FAIL two_process_save() == 0
# Retry:
dataloss_test: two_process_save disk=[Abase] A(stale=0 modified=0) B(err=5 stale=0 modified=1 text=[Bbase])
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery passed
```

## Fuzz evidence

The initial retirement seed found an implementation edge before finalization:
a temp could reuse its unlinked original's inode within a filesystem timestamp
tick, and an unrestricted metadata search could mistake the temp for its own
surviving copy. Red output (`build/edit-52o-fuzz-red.log`):

```
fuzz/journal_fuzz.c:298: assertion failed: copies==(preserve?1u:0u)
0x8,0x1,
exit 77
```

(P) Those input bytes select JOURNAL_TARGET dispatch and MISSING in the fuzz
protocol enum. The fix records and checks the exact original basename; the
reserved temp namespace cannot name an original. No content scan was added.
The final mutation campaign used ASan/UBSan/LSan, no new run/time budget, and
was stopped with SIGINT before the full headless editor suite ran. The last
logged input ordinal was 13142 (M)[AC], directly from libFuzzer's counter;
its interrupt exit was 72. No assertion or sanitizer finding occurred after
the fix. A normal-exit replay of the complete retained corpus followed:

```
$ ASAN_OPTIONS=detect_leaks=1 build/fuzz/journal_fuzz -runs=0 -print_final_stats=1 build/edit-52o-fuzz-corpus
#258	INITED cov: 4037 ft: 11243 corp: 196/1774b exec/s: 129 rss: 60Mb
#258	DONE   cov: 4037 ft: 11243 corp: 196/1774b lim: 21 exec/s: 129 rss: 60Mb
Done 258 runs in 2 second(s)
stat::number_of_executed_units: 258
stat::average_exec_per_sec:     129
stat::new_units_added:          0
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              60
exit 0
```

(P) `-runs=0` selects corpus replay without additional mutation; the executed
unit count above is measured output, not a new workload target. Fuzz fixtures
were small and removed; no sparse GB rotation temp was needed.

## Broad verification and remaining work

The non-display `make check` selection stopped at the mapping assertion below.
All remaining suites were then run individually with leaks enabled, including
native-free GL checks and `editor_test --headless`. (M)[AC] The executed-suite
count is 11 entries in the initial make log plus 73 entries in the continuation
JSON = 84; subtract the two failures below = 82 passing non-display suites.
Logs: `build/edit-52o-headless-check.log`, `build/edit-52o-san-results.json`,
`build/edit-52o-san-<suite>.log`. The final required suites were rerun after the
last implementation change as shown above.

**Full make check is not green.** These failures are outside the bead and were
reported without changing their code:

```
editor_large_test:802: FAIL file_snapshot_bytes(snap, NULL, &mapped, &n) == 1 && n == length
editor_large_test:1651: FAIL cold_bracket_paint(false) == 0
editor_scroll_test:117: FAIL rc == 0 && (!edit_malloc_guard_active() || allocations == 0)
```

The mapping fixture and held-worker cancellation fixture use null backends
without journal configuration. Their failure points are outside rotation or
retirement; no clean baseline was established for these failures in this
worktree. The failed mapping fixture was removed after recording its result.

Native display verification remains for the coordinator on Xvfb :99. The
sandbox lacks the X socket; a representative native run produced:

```
x11_identity_test:25: FAIL plat_init(&p, &cfg) == PLAT_OK
```

The sandboxed GL run reported `reason=X11_unavailable result=-10`; both native
attempts also encountered the sandbox's LeakSanitizer inspection restriction.
Native portions of editor_test and native CLI/GL/raster/refwin/X11 suites
therefore remain unverified here.

The check recipe's replay CLI and display-lock shell contracts passed.
The latter used the temporary leak-enabled copy described below; its shared
lock wait completed, and the external holder was not signalled.

Final journal benchmark (existing `--track` workload; final release build):

```
$ build/bench/journal_bench --track
TRACK journal_append payload=1 edits=100000 p50_ns=1328 p99_ns=17618 gate_p99_ns=20000_(G) within_gate=1 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=4.10
TRACK journal_append payload=1024 edits=100000 p50_ns=1641 p99_ns=15506 gate_p99_ns=20000_(G) within_gate=1 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=4.34
journal_bench: correctness=ok mode=TRACK gate_misses=0 status=0 (complete G1/G9 endpoints require editor harness)
exit 0
```

(M)[AC] BAT0 was `Not charging`; the benchmark prints its load and inherited
workload/ceiling derivations. This is a tracked module measurement, not a claim
about the complete editor typing endpoint. Full log:
`build/edit-52o-bench-final.log`.

Implementation for this bead is complete. Remaining verification is the
coordinator's native-display run and resolution/recheck of the out-of-scope
non-display assertions. The possible-sole-copy legacy bytes remain preserved
as documented above; no real-profile cleanup is claimed.

## Outside scope

- The existing pinned/unscanned-artifact conservatism still blocks BASE
  retirement. Possible-sole-copy temps and their set-asides also preserve their
  unknown references; no attempt was made to relax that safety rule.
- edit-qct and edit-f3i remain untouched.
- The editor mapping and scroll assertions above remain for their owners.
- `tools/test_display_lock.sh` hardcodes leak detection off. Its repository
  source was left untouched; verification uses a temporary build-directory
  copy changing only that option to keep leaks enabled.
- Legacy and overlong-name set-asides retain their bytes until an operator can
  establish redundancy. This is the possible-sole-copy exception, not an
  automatic deletion claim for the historical profile.
