# edit-zzj.12 session 9 rebase

Rebased `e66356d` onto main `6f40d16`; replayed commit is `daece6c` on
`wt/edit-zzj.12`. `git add` and `GIT_EDITOR=true git rebase --continue`
completed successfully. No other git write commands were used.

Read the bead report and decisions `edit-zzj.12`, `edit-zzj.13`,
`edit-zzj.14`, `edit-2vs`, and `edit-yqu` before resolving.

## Conflicted files and resolutions

- `bench/editor_bench.c`: retained main's shared `target_a_config()` builder,
  runtime geometry assertions/output, injection-to-T4 attribution, contention
  workloads and shared gate/structural verdict policy. Kept the bead's delayed
  Present T4 settle/self-check, explicit T5 ready settle, caret frame metrics and
  zero-worker requirement. Added `worker_jobs` to the existing structural
  predicate and kept `row_result(miss, structural, track)`, so wake/idle/unfocus
  and worker-job failures remain fatal in TRACK while timing/refusal semantics
  remain those of main's shared gate judge.
- `src/editor/editor.c`: cleared `caret_only` when input is queued, as in the
  bead, then retained main's blink/resize control flow and `record_slice()`
  accounting. Kept the bead's caret refresh and completion-fd integration;
  main's journal mailbox idle policy and action/submit accounting remain.
- `src/raster/raster.c`: kept both helpers: main's `cpu_join_jobs()` and the
  bead's `inline_cursor_damage()` / `upload_cursor_cells()`. Joining remains
  before snapshot/fence reuse at submit and before shutdown resource release;
  the caret path still submits zero strip/fence jobs and drains genuine fence,
  Present and Idle acknowledgements on the UI thread. The obsolete unconditional
  handle-count reset remains removed as required by main's ownership fix.

The native sanitizer run exposed one interaction: the new G11 test asserted
unchanged epochs for every work slot, but main's `cpu_join_jobs()` intentionally
cancels the preceding full-frame leases on the first caret submit, incrementing
their epochs. It failed at `editor_test:174` (epoch equality); make exited **2**.

Minimal post-rebase fix: in `tests/editor_test.c:raster_blink_damage`, retain
the unchanged-epoch check, but allow a single increment on the first toggle
only when `cancel_ns` is nonzero, the old handle has physically finished, and
the slot is no longer busy. New submissions clear `cancel_ns`, so they still
fail. Later toggles still require unchanged epochs; zero strip/completion-job
metrics and one-/two-cell pixel assertions remain unchanged. No production
code was changed to accommodate the test. This fix and this report are staged
separately from the replayed commit; no additional commit command is authorized.

No changes were made to `HANDOFF.md` or `docs/worklog/`.

## Verification

GCC is Ubuntu 13.3.0; clang is Ubuntu 18.1.3. Release compilation uses the
Makefile's C11 strict warnings including `-Werror`; sanitizer compilation uses
ASan/UBSan with the same warnings. Every display invocation specifies `:99`.
LeakSanitizer is **off**: `ASAN_OPTIONS=detect_leaks=0`.

`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all` exited **0**.
Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
```

The first sandboxed sanitizer invocation compiled successfully but could not
connect to the display: `cli_test` failed `!xcb_connection_has_error(c)` and
make exited **2**. `xdpyinfo` also failed in that sandbox and succeeded with
local socket access enabled. This is an environment failure; no test or source
was changed for it. The native rerun uses the same required environment with
local X11/IPC socket access enabled. That run reached and failed the G11 epoch assertion described above.
The complete final rerun after the test fix exited **0**:
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check`.
All **59** sanitizer test binaries and the replay CLI tests passed.
Tail:

```text
> Warning:          Could not resolve keysym XF86NavInfo
Errors from xkbcomp are not fatal to the X server
x11_stall_test: key callback reply 0.042 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.056 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.156 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.036 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.021 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.017 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

Relevant combined-change checks in that successful run:

```text
review 17: active job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
PROBE structural_failure TRACK exit=1 expected=1 (G)
PROBE active_frame keys=4 completed=4 coalesced=3 (M)[AC]
PROBE structural counts, allocation, missing/duplicate frame failures remain fatal in TRACK
P2.5b section G11: GREEN
raster pixels: PASS inline caret, full-window scalar comparison and zero worker jobs
raster fence: PASS (queued fence job joined before backend release)
```

Both required final-source make invocations exited zero. `git diff --check`
is clean. Logs are retained under ignored `build/rebase-zzj12/`; the tails are
embedded here so the report remains reviewable without build artifacts.

Focused `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99
build/san/tests/editor_test --raster-blink` exited **0**, exercising all four
modes twice (plain caret, selection, wide glyph, wrapped row):

```text
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
editor_test: G11 inline caret damage and sleeping worker pool passed
```

Final-source `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all` exited **0**.
Tail:

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c tests/editor_test.c -o build/rel/tests/editor_test.o
gcc -O2 -g -msse2 -pthread build/rel/tests/editor_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/editor_test
```

The release `build/bench/editor_bench --self-check` exited **0**:

```text
editor_bench: settle permits undamaged submitted frame with delayed Present completion
```

## Unreconciled items

No unresolved conflicts. This rebase does not close the original G11 CPU
median acceptance gap documented by the bead; no performance acceptance,
real-display or leak-enabled run is claimed.
