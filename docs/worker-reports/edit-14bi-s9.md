# edit-14bi worker report (s9)

Fixed COPY-mode lazy recovery after replay changes the text length. Pending
adoption now recreates the line index when its length differs from the
replayed tree, retaining the existing MMAP recreation path. The restored COPY
buffer contains its insertion, stays modified, retains the separate TARGET
binding, and survives checkpoint relaunch.

## Files touched

- `src/editor/buffers.c`: recreate a mismatched pending index before snapshot binding.
- `tests/editor_session_test.c`: preserve existing MMAP and same-length COPY
  cases; add growing COPY lazy restore and checkpoint relaunch, verify the
  acquired mode and adopted index length, and add `--edit-14bi`.
- `docs/decisions/edit-14bi.md`: record the index recreation choice and fixture derivation.
- `docs/worker-reports/edit-14bi-s9.md`: this report.
- Ignored evidence: `build/verification-edit-14bi/` contains build/run logs,
  the verification wrappers, and the original production-file snapshot.

## Evidence conventions

(P) The existing `JOURNAL_STARTUP_SCAN_BYTES` bound is 105,000,000 bytes.
The sparse fixture is that bound plus the smallest positive byte increment:
105,000,000 + 1 = 105,000,001 bytes. Inserting `strlen("x")` bytes yields
105,000,002 bytes. The preserved same-length case deletes that many trailing
zero bytes. `UINT64_MAX` forces COPY; the smallest positive byte threshold
forces MMAP for this nonempty fixture. Prefix lengths derive from string
sizes. Zero denotes success or the insertion offset, and the sole journal
buffer has id one. Selector arity follows the existing executable-plus-option
convention. No workload size, duration, or allocation budget was invented.

(P) Build parallelism, display, lock timeout and leak setting below come from
the brief. Exit zero means process/API success; the red exit one is the test
assertion macro's failure return. The release guard workload and benchmark
arguments are the existing test and `bench/editor_bench.args` presets.
Compiler versions, source lines, bead names and paths identify existing
artifacts. All captured power, uptime, load, disk and runtime observations
below are (M)[AC], with BAT0 reporting Charging. Benchmark values are copied
from its measured output without conversion or a baseline comparison.

## Red run

The test was added before any production change. The completed red run used
the original `src/editor/buffers.c` and reproduced the reported empty-buffer
fallback, retained-journal notice and absence of set-aside.

Build: `make -j3 build/san/tests/editor_session_test` — exit 0.

```sh
sh build/verification-edit-14bi/run.sh build/san/tests/editor_session_test --edit-14bi
# exit=1 (expected assertion failure)
```

```text
Charging
 02:17:45 up 42 min,  1 user,  load average: 23.07, 11.99, 5.27
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  379G   77G  84% /
edit-4jhi: deferred retained BASE reopen text=no modified=no set-aside=no target=no
edit-4jhi: len=0 expected=105000002 prefix= path= save_path= notice=large session journal could not be restored; journal retained
editor_session_test:1392: FAIL text && dirty && !aside && bound && original
exit=1
Charging
 02:17:46 up 42 min,  1 user,  load average: 23.07, 11.99, 5.27
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  379G   77G  84% /
```

## Green run

After the adoption condition changed, the same regression passed with leak
detection enabled, including checkpoint relaunch.

Rebuild: `make -j3 build/san/tests/editor_session_test` — exit 0.

```sh
sh build/verification-edit-14bi/run.sh build/san/tests/editor_session_test --edit-14bi
# exit=0
```

```text
Charging
 02:18:24 up 43 min,  1 user,  load average: 23.00, 13.32, 6.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  379G   77G  84% /
edit-4jhi: deferred retained BASE reopen text=yes modified=yes set-aside=no target=yes
edit-4jhi: target=rewritten acquisition=COPY length=grown checkpoint relaunch preserved BASE edits and TARGET
exit=0
Charging
 02:18:26 up 43 min,  1 user,  load average: 23.00, 13.32, 6.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  379G   77G  84% /
```

## Verification commands and exit codes

(M)[AC] Installed compilers reported GCC 13.3.0 and clang 18.1.3. Project source
compiled as C11 with `-Wall -Wextra -Werror -Wshadow -Wconversion`; the sanitizer
binaries use ASan and UBSan.

```sh
make -j3 all
# exit=0
make -j3 build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/editor_checkpoint_test build/san/tests/dataloss_test build/san/tests/journal_test
# exit=0
```

The check-style binary targets above use the same sanitizer rules as
`make check`. The full suites ran as follows, each with exit 0:

```sh
sh build/verification-edit-14bi/run.sh build/san/tests/editor_test
sh build/verification-edit-14bi/run.sh build/san/tests/editor_session_test
sh build/verification-edit-14bi/run.sh build/san/tests/editor_checkpoint_test
sh build/verification-edit-14bi/run.sh build/san/tests/dataloss_test
sh build/verification-edit-14bi/run.sh build/san/tests/journal_test
```

The sequential remaining-suite runner also exited 0:

```text
editor_test exit=0
editor_checkpoint_test exit=0
dataloss_test exit=0
journal_test exit=0
```

The wrapper supplies the brief's settings, creates a private runtime directory,
removes it on exit, and stamps power/uptime/disk before and after each run:

```sh
export DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1
XDG_RUNTIME_DIR=$(mktemp -d)
export XDG_RUNTIME_DIR
trap 'rm -rf -- "$XDG_RUNTIME_DIR"' EXIT
sh tools/with_display_lock.sh "$@"
```

The initial sandboxed red attempt reproduced the regression but LeakSanitizer
then reported its process-inspection/ptrace restriction (exit 1). The completed
red/green and full sanitizer runs used approved execution outside that
restriction; they had no sanitizer or leak diagnostics. A sandboxed release
raster guard also failed at `editor_open` (exit 1); its approved rerun passed.
The assigned display was reachable in the approved runs. Shared display lock
waits are preserved in the logs.

## Release editor allocation guards

```sh
sh build/verification-edit-14bi/run.sh env EDITOR_ALLOC_ONLY=null build/tests/editor_test
# exit=0
sh build/verification-edit-14bi/run.sh env EDITOR_ALLOC_ONLY=raster build/tests/editor_test
# exit=0 (approved rerun)
```

(M)[AC] Both guards reported zero allocations with the counting hook active:

```text
Charging
 02:26:07 up 51 min,  1 user,  load average: 38.73, 34.43, 19.26
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  381G   75G  84% /
waiting for display :99 (held by unknown)
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
Charging
 02:45:40 up  1:10,  1 user,  load average: 17.02, 21.92, 23.27
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```
```text
Charging
 02:44:57 up  1:10,  1 user,  load average: 18.33, 22.80, 23.60
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
waiting for display :99 (held by 118112)
editor_test: raster 10000 keys mallocs=0 guard=active
exit=0
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```

An additional approved null-guard run was queued while the initial run was
still pending; it also exited 0:

```text
Charging
 02:45:31 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
waiting for display :99 (held by 131333)
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
Charging
 02:56:42 up  1:21,  1 user,  load average: 28.95, 22.63, 21.35
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
```

## Sparse fixtures and disk-space captures

The new fixture uses `ftruncate` and a literal prefix, and its outer helper
removes the root after every body return, including the red assertion failure.
The existing GB fixtures in the full session/checkpoint suites are sparse and
use their existing cleanup wrappers. The final fixture-directory check exited
0 and printed nothing:

```sh
find build -maxdepth 1 -type d \( -name 'editor-*' -o -name 'dataloss-*' -o -name 'journal-*' \) -print
# exit=0; no output
```

(P) The depth selects fixture roots directly under `build`. A small worktree
source fixture left by the sandboxed raster startup failure was removed after
all editor/guard runs (cleanup command exit 0):

```text
build/editor-alloc-kbI20K.c
```

Exact (M)[AC] before/after `df -h /` output for the full suites follows, with
the neighboring power and uptime stamps. Shared-box activity and the concurrent
release build also affect disk usage.

`editor_test`:

```text
Charging
 02:27:37 up 52 min,  1 user,  load average: 35.00, 34.55, 20.72
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  381G   75G  84% /
exit=0
Charging
 02:44:27 up  1:09,  1 user,  load average: 17.63, 23.18, 23.74
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```

`editor_session_test`:

```text
Charging
 02:20:40 up 45 min,  1 user,  load average: 33.29, 20.58, 9.68
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
exit=0
Charging
 02:22:05 up 47 min,  1 user,  load average: 42.07, 26.78, 12.83
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
```

`editor_checkpoint_test`:

```text
Charging
 02:44:27 up  1:09,  1 user,  load average: 17.63, 23.18, 23.74
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
exit=0
Charging
 02:45:24 up  1:10,  1 user,  load average: 18.45, 22.43, 23.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```

`dataloss_test`:

```text
Charging
 02:45:24 up  1:10,  1 user,  load average: 18.45, 22.43, 23.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
exit=0
Charging
 02:54:02 up  1:19,  1 user,  load average: 24.07, 18.03, 19.88
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
```

`journal_test`:

```text
Charging
 02:54:02 up  1:19,  1 user,  load average: 24.07, 18.03, 19.88
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
exit=0
Charging
 02:57:08 up  1:22,  1 user,  load average: 29.52, 23.35, 21.63
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
```

## Existing editor benchmark

```sh
sh build/verification-edit-14bi/run.sh build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=64
# exit=0
```

(P) Arguments are copied from `bench/editor_bench.args`. (M)[AC] The existing
TRACK rows exceed their existing latency goals on this loaded box; this is a
tracked observation, without a before/after comparison. GL initialization
reported unavailable and used raster fallback. Exact output:

```text
Charging
 02:30:06 up 55 min,  1 user,  load average: 30.11, 33.14, 22.28
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
waiting for display :99 (held by 66196)
STAMP (M)[AC] BAT0=Charging load1=28.77 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=0 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=64 required_n=10000 p50=6533527 p99=15405009 ci95_p50=[6353618,6841511] ci95_p99=[10198830,15405009] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=64 required_n=10000 p50=4953532 p99=12059017 ci95_p50=[3302215,6066698] ci95_p99=[7019220,12059017] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=7272082 p99=15988247 ci95_p50=[6988028,7960988] ci95_p99=[10831491,15988247] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=28.77 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION raster byte=0 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_raster_serial_injection_submit n=64 required_n=10000 p50=43113793 p99=65667347 ci95_p50=[39030556,48148046] ci95_p99=[61134792,65667347] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
BENCH name=editor_raster_serial_dequeue_submit n=64 required_n=10000 p50=43092585 p99=65611693 ci95_p50=[39007405,48123043] ci95_p99=[61096190,65611693] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=44624498 p99=68271130 ci95_p50=[41614725,49093466] ci95_p99=[64532545,68271130] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Charging load1=28.77 verdict=TRACK
G1 raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=28.79 shared box
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION gl_fallback_raster byte=0 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_serial_injection_submit n=64 required_n=10000 p50=48998321 p99=70529235 ci95_p50=[47642861,52230732] ci95_p99=[67144059,70529235] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.79 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_dequeue_submit n=64 required_n=10000 p50=48934674 p99=70501714 ci95_p50=[47621000,52196534] ci95_p99=[67111219,70501714] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=28.79 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=51504981 p99=74534229 ci95_p50=[49946243,54393808] ci95_p99=[68840706,74534229] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Charging load1=28.79 verdict=TRACK
G1 gl_fallback_raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
exit=0
Charging
 02:31:28 up 56 min,  1 user,  load average: 29.52, 31.98, 22.81
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
```

## What remains / outside scope

No implementation or required verification remains for edit-14bi. No unrelated
production fix, refactor or additional review pass was made. The benchmark
latency observations and GL fallback above are outside this bead and were
recorded without a fix. The existing allocation test returns before its normal
fixture cleanup when `editor_open` fails; its leftover worktree fixture was
removed, and the test's failure-cleanup behavior was not changed.

All source/document writes were confined to this worktree. No `bd` or `git`
commands were run. `HANDOFF.md`, `docs/worklog`, `CLAUDE.md`, `PLAN.md`, and
`PRD.md` were not edited. Stop after this bead.
