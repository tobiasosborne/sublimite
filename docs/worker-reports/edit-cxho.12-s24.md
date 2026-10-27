# edit-cxho.12 — session s24 worker report

Implemented incremental document find from the saved panel-opening cursor.
Query edits select and reveal the first match at/after that origin, wrapping
when necessary. Clearing the query restores the original selection and view;
no-match queries restore the origin. Enter/Shift+Enter advance from the
preview, including Enter queued before completion. Escape keeps the selection.
Refocusing retains the origin. The decision note records the supersession of
the supplied edit-g02 Enter-commits rule.

Selection by byte searches the existing immutable match plan on a worker,
including beyond the prefix cache. Scrolling retains the resident seek path,
centres distant results, handles visual rows inside a long wrapped line, and
keeps EOF-clipped matches visible. The mutation/highlight-cache regression is
fixed by refreshing visible ranges after layout and before decoration paint.
Find in Files retains its results marks and explicit search action.

## Files touched

- `src/editor/findpanel.c`: origin save/restore, incremental requests, worker
  selection, scroll positioning and visible-range refresh.
- `src/editor/private.h`: embedded origin/position fields and helper declarations.
- `src/editor/input.c`: centre/reveal after resident seek; preserve EOF clipping.
- `src/editor/paint.c`: refresh find ranges against the completed layout.
- `src/findui/findui.h`: byte-origin seek API.
- `src/findui/private.h`: prepaid seek request fields.
- `src/findui/findui.c`: asynchronous seek; keep highlight/cache identity on selection replies.
- `src/findui/worker.c`: binary search by match start in the existing plan.
- `tests/editor_test.c`: injected incremental, restoration, navigation, pending
  Enter, no-match, wrap, mmap, centring, EOF and mutation-highlight regressions;
  update superseded selection expectations.
- `tests/findui_test.c`: worker seek/cache/window/cancellation coverage and
  selection expectations for existing panel tests.
- `tests/config_find_test.c`: disabled bindings preserve the incremental preview.
- `docs/decisions/edit-cxho.12.md`: implementation choices and numeric derivations.
- `docs/worker-reports/edit-cxho.12-s24.md`: this report.

## Evidence conventions

Fixture inputs and structural constants are (P), as derived in the decision
note. Exit codes are process results (P: zero denotes success); diagnostic
locations and printed results below are captured measurements (M)[AC].
`Not charging` maps to [AC] in the existing benchmark's power-stamp policy.
Timing populations use the existing benchmark defaults or checked-in argument
file (E: inherited workload), with no new count or duration budget. Percentiles
are calculated from captured monotonic endpoint differences (M)[AC].

## Red before implementation

Built the new editor injection tests first with:

```sh
make -j4 build/san/tests/editor_test
```

Build exit: 0. Ran with `ASAN_OPTIONS=detect_leaks=1` and the assigned wrapper:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test --cxho12-incremental
```

Runtime directories were removed with shell exit traps. Exit: 1.

```text
Not charging
 12:20:25 up 1 day, 14:23,  1 user,  load average: 3.81, 3.24, 2.29
editor_test:4613: FAIL editor_view(e).selection.anchor == offsets[target]
editor_test:4649: FAIL find_incremental_case(false, false) == 0
==6==LeakSanitizer has encountered a fatal error.
==6==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==6==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

The additional mutation-highlight assertion also failed before implementation,
using the same wrapper and `--cxho3-focus`; exit: 1.

```text
Not charging
 12:20:30 up 1 day, 14:23,  1 user,  load average: 3.59, 3.20, 2.29
editor_test:4527: FAIL e->find_range_count == findui_get_state(&e->find).match_count
==6==LeakSanitizer has encountered a fatal error.
==6==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==6==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

The behavioral assertions failed before the sandbox's LeakSanitizer diagnostic.
Subsequent sanitizer executions ran outside its tracing environment with leak
detection still enabled. Display connectivity was verified with
`DISPLAY=:99 xdpyinfo` (exit: 0); the abstract socket is reachable there.

Additional red tests before their centring corrections (same wrapper and leak
detection):

```text
Not charging
 12:33:08 up 1 day, 14:36,  1 user,  load average: 1.12, 1.63, 2.00
editor_test:4856: FAIL match_row==e->v.config.rows/2u
wrapped centering regression exit=1
Not charging
 12:36:29 up 1 day, 14:39,  1 user,  load average: 0.59, 1.14, 1.74
editor_test:4643: FAIL editor_view(e).first_line<=lines-1u && lines-1u<editor_view(e).first_line+e->v.config.rows
editor_test:4663: FAIL find_incremental_case(false, false) == 0
EOF regression exit=1
```

## Green and verification

Built the sanitizer binaries through the Makefile's sanitizer targets and ran
all focused editor selectors with `ASAN_OPTIONS=detect_leaks=1`, a temporary
`XDG_RUNTIME_DIR`, `DISPLAY=:99`, and `tools/with_display_lock.sh`:

```text
Not charging
 12:39:10 up 1 day, 14:42,  1 user,  load average: 0.97, 1.22, 1.68
edit-cxho.12 incremental: mapped=0 wrapped=0 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
edit-cxho.12 incremental: mapped=0 wrapped=1 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
edit-cxho.12 incremental: mapped=1 wrapped=0 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
--cxho12-incremental sanitizer exit=0
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
--73j-find sanitizer exit=0
edit-cxho.3 focus: document/query typing, panel caret, Tab, F3/previous, Escape, Return, Ctrl+F/H passed; field mallocs=0 document mallocs=0 guard=ASan-inert
--cxho3-focus sanitizer exit=0
edit-g02 Return/keypad/previous/wrap/pending/empty/Escape passed
--g02-find sanitizer exit=0
edit-g02 uncached Shift+keypad Enter selects previous exactly once passed
--g02-previous sanitizer exit=0
```

The final release build's counting allocator was active:

```text
Not charging
 12:40:03 up 1 day, 14:43,  1 user,  load average: 2.09, 1.51, 1.75
editor_test: null 10000 keys mallocs=0 guard=active
null allocation exit=0
editor_test: raster 10000 keys mallocs=0 guard=active
raster allocation exit=0
```

The checked-in `bench/editor_bench.args` workload completed successfully on the
final build, with its inherited sample count and TRACK timing policy. Its
structural typing guards and allocation checks passed for every backend:

```text
Not charging
 12:39:22 up 1 day, 14:42,  1 user,  load average: 1.59, 1.35, 1.72
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 gl_fallback_raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
editor_bench smoke exit=0
Not charging
 12:39:28 up 1 day, 14:42,  1 user,  load average: 1.79, 1.39, 1.73
```

### Verification commands

| Command / execution | Exit | Result |
| --- | --- | --- |
| `make -j4 check` with `ASAN_OPTIONS=detect_leaks=1`, temporary runtime, `DISPLAY=:99` | 0 | Full sanitizer suite and both shell contracts passed. |
| Full sanitizer `editor_test` through the Makefile display wrapper | 0 | `editor_test: all passed`. |
| Full sanitizer `findui_test` in the final full check | 0 | `findui_test: all passed`. |
| `make -j4 all` | 0 | Release C11 warning-clean build, including `-Werror`, `-Wshadow`, `-Wconversion`. |
| `make -j4 build/san/tests/editor_test build/san/tests/findui_test build/san/tests/config_find_test` | 0 | Clang ASan/UBSan binaries. |
| Final focused editor sanitizer selectors shown above | 0 each | Leak detection enabled, assigned display wrapper. |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/findui_test` | 0 | Null-renderer fixtures; all cases passed. Repeated in the full check. |
| Release `editor_test --cxho12-incremental`, `--73j-find`, `--cxho3-focus` via assigned wrapper | 0 each | Final source, active malloc guard; centring/EOF/focus green. |
| Release `EDITOR_ALLOC_ONLY=null` and `EDITOR_ALLOC_ONLY=raster` editor tests via assigned wrapper | 0 each | Existing counting-allocator workloads, shown above. |
| `build/bench/editor_bench` using `bench/editor_bench.args` plus temporary `--file` via assigned wrapper | 0 | Final source; all structural typing guards green, timings TRACK. |
| `build/bench/findui_bench --track` | 0 | Existing cancellation workload; no caller-thread scans. |
| `DISPLAY=:99 xdpyinfo` | 0 | Actual display connectivity verified outside the sandbox. |

Compiler identities were captured with `--version`: gcc 13.3.0 and clang
18.1.3 (M)[AC]. All display suites used the repository's display lock on
`:99`; the full check exported `EDIT_REFUSE_REAL_DISPLAY=1` for production
children. No installed binary/profile or other checkout was changed.

Intermediate full checks exited 2 while legacy no-preview assertions and the
new EOF regression were being corrected; these are superseded by the final
full-check result below. The initial attempt to redirect the first build log
preceded creation of `build/`; no compiler ran in that attempt. The directory
was created and the actual red build completed successfully.

### Timing observations and scope limit

This tree exposes the G1 exercise through `bench/editor_bench`; `editor_test`
has no separate G1 selector. Its allocation modes were run independently.

The larger G1 run used the existing default population. It completed the null
and raster rows with zero allocations and correct structural accounting. The
null timing passed; the native raster timing missed the existing target.
Percentiles below are sorted `T4 - ingress` measurements in nanoseconds (M)[AC];
target values and population come from the existing benchmark (P/E), without
new budgets:

```text
Not charging
 12:29:29 up 1 day, 14:32,  1 user,  load average: 1.45, 2.44, 2.32
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=372590 p99=623913 ci95_p50=[372454,372746] ci95_p99=[609766,676432] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.45 verdict=PASS
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
BENCH name=editor_raster_serial_injection_T4_G1 n=10000 required_n=10000 p50=14678767 p99=15519810 ci95_p50=[14665261,14693117] ci95_p99=[15483365,15576026] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.45 verdict=MISS
G1 raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
editor_bench exit=130
Not charging
 12:36:18 up 1 day, 14:39,  1 user,  load average: 0.46, 1.15, 1.75
```

The default-population command was
`build/bench/editor_bench --no-idle --no-p4 --serial-only --file=<temporary fixture>`
through the display wrapper. Exit: 130 (P: intentional SIGINT). GL reported
`EGL/GL/Present unavailable` and fell back to the same raster backend; its
additional default-population sampling was stopped to release the display for
sanitizer suites. The final checked-in smoke workload above completed all
backend rows. GPU timing is not certified, and native G1 timing is not claimed
green. No rendering/performance fix or regression attribution was attempted
outside this find bead.

The touched findui module's existing cancellation benchmark also completed:

```text
Not charging
 12:30:29 up 1 day, 14:33,  1 user,  load average: 1.03, 2.16, 2.22
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  159G  1.6T  10% /
findui_bench: power=Not charging [AC] load1=1.03 fixture=/tmp/edit-corpus/log_1g.txt track=1
BENCH name=G6c_findui_keystroke_logical_ack n=10000 required_n=10000 p50=3060 p99=5144 ci95_p50=[3047,3072] ci95_p99=[4790,5916] gate_p50=1000000 gate_p99=5000000 dropped=0 (M)[AC] power=Not charging load1=1.03 verdict=TRACK
findui_scan_thread_check (M)[AC] load1=1.03 calling_thread_scans=0 expected=(G)0
findui_bench exit=0
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  159G  1.6T  10% /
```

No new GB-scale fixture was introduced. The added mapped fixture derives from
the supplied short-line acceptance source and is removed after editor close.
The existing benchmark corpus was only read and remains untouched. Disk stamps
for the full check are recorded with its final summary below.

### Final full check

Command (runtime directory removed by an exit trap):

```sh
XDG_RUNTIME_DIR="$run_dir" DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 ASAN_OPTIONS=detect_leaks=1 make -j4 check
```

Exit: 0. The binary count below is the Makefile's discovered test list (P);
all output is captured from the final run (M)[AC].

```text
Not charging
 12:39:12 up 1 day, 14:42,  1 user,  load average: 0.97, 1.22, 1.68
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  159G  1.6T  10% /
editor_test: all passed
findui_test: all passed
check: 112 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit=0
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  160G  1.6T  10% /
Not charging
 12:55:27 up 1 day, 14:58,  1 user,  load average: 1.52, 3.46, 3.78
```

The display-lock shell contract deliberately emits refusal/time-out errors,
including a nested make failure, while proving ownership and exit-status
propagation. Its own PASS and the outer check's zero exit are shown above.
The replay CLI contract passed as well. No C source changed after these final
builds and runs.

## What remains / outside scope

No functional implementation or functional verification remains in this bead.
The native G1 timing target was not green on this display; the measurements
and successful structural/allocation guards are recorded above for the
coordinator. GL was unavailable and used raster fallback, so this run supplies
no GPU timing certification. Rendering/performance follow-up is outside this
bead and was not changed. No other checkout, installed build/profile, protected
handoff/planning file, or worklog was touched; no tracking or version-control
command was run.
