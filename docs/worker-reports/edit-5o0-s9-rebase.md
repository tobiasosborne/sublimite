# edit-5o0 session 9 rebase report

Rebased `wt/edit-5o0` onto main `c9d1b25`, replaying `8465965` as `3c0122b`.
Read the original worker report and both sides' decisions: `edit-5o0.md`,
`edit-9jo.md`, and the landed editor integration decisions `edit-zzj.13.md`.

## Conflicts and resolution

Only `bench/editor_bench.c` was conflicted (two hunks in `idle_row`).

- Per-blink CPU append: removed main's direct append in the natural-cadence loop;
  the bead's `g11_cpu_turn` now owns that append and carries process CPU across
  later completion and timeout turns. Keeping both would double-count samples
  and refer to the bead's removed `start` variable.
- Post-loop accounting and synthetic null population: kept the bead's existing
  quiet-observation turn, nonblocking raster acknowledgement drain, final pending
  window retirement, and all-thread context-switch snapshot. These run before
  main's independent null-only synthetic population, so that population does not
  contaminate the natural-cadence wakeup/context-switch counters. Kept main's
  `BENCH_INTERACTION_MIN_N` storage, all 10000 synthetic due-now samples, checked
  appends, exactly-one-blink assertions, deadline disarming, and the
  `editor_null_G11_process_cpu_synthetic_due_now` name. Raster/GL retain their
  natural-cadence schedules. Kept the bead's TRACK scheduling proxy and explicit
  unmeasured qualifying wakeup status. Updated the nearby comment to describe
  the combined schedule. Main's enlarged tab/minimap populations remain intact.

All other replayed raster/render, tests, fuzz, decision and worker-report changes
applied without conflicts. `git add bench/editor_bench.c` and
`GIT_EDITOR=true git rebase --continue` succeeded; no other Git write command
was used. Git metadata required expanded sandbox access. No edits were made to
`HANDOFF.md` or `docs/worklog/`.

## Validation

Release compiler: GCC 13.3.0. Sanitizer compiler: Clang 18.1.3.
Both configurations use C11 `-Wall -Wextra -Werror -Wshadow -Wconversion`.
Native execution uses only `DISPLAY=:99 EDIT_DISPLAY=:99`.
Leak detection is OFF (`ASAN_OPTIONS=detect_leaks=0`). Display :99 was already
running and required expanded sandbox access; attempted Xvfb launches did not
start another server or change the existing one.

`CC_RELEASE=gcc-13 CC_SAN=clang-18 make -j4 all`: **exit 0**.
Tail:

```text
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
```

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 CC_RELEASE=gcc-13 CC_SAN=clang-18 make -j4 check`: first run **exit 2**; full retry **exit 0** (60 test binaries and replay CLI tests passed).
First-run tail:

```text
wrap_test: all passed
== build/san/tests/x11_clip_test
FAIL tests/x11_clip_test.c:798: late UTF8 refusal then timely STRING must succeed (ok 1, failed 0)
x11_clip_test: 64 MiB UTF8 INCR: 64 MiB in 1296 ms, UI bulk bytes 0, worst UI poll 0.878 ms wall / 0.333 ms CPU (G1 typing budget 1.0 ms p50 / 2.0 ms p99)
x11_clip_test: 8 MiB Latin-1 INCR: 16 MiB in 189 ms, UI bulk bytes 0, worst UI poll 0.316 ms wall / 0.251 ms CPU (G1 typing budget 1.0 ms p50 / 2.0 ms p99)
x11_clip_test: FAILED
make: *** [Makefile:101: check] Error 1
```

The first failure was the unchanged clipboard STRING-fallback data assertion
(`ok 1, failed 0`). Several other make/native test processes were observed
running concurrently on the host. Shared-display clipboard contention is an
inference, not an established cause; no clipboard source/test was modified.
Raster, render, reference-window and the merged harness population tests had
already passed. The harness emitted n=10000 for both null tab rows and the null
synthetic G11 row, with 20213 checks and zero failures.


Final full-retry tail:

```text
x11_order_test: all ok
== build/san/tests/x11_stall_test
x11_stall_test: key callback reply 0.207 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.043 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.127 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.125 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.100 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.016 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

The unchanged clipboard test passed on the full retry. No test failed because
of an identified interaction between the two changes, and no post-rebase source
or test fix was needed. The only source edits were the conflict resolutions
listed above. `git diff --check` passes.

Additional delayed-retirement accounting self-check:
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --self-check-g11`: **exit 0**.

```text
P2-1 section 16: delayed completion samples=1 total=600
G11 accounting: timeout CPU retained; process switches=5 UI=1 (M)[AC] worker sleeps visible
G11 accounting: delayed completion CPU retained through retirement

```

The same G11 self-check was also compiled and linked against the Clang 18
ASan/UBSan library and run with leak detection off: **exit 0**, identical
accounting assertions and output.

Full raw build/test logs are in ignored `build/edit-5o0-rebase-evidence/`.

## Unreconciled work

No source conflict remains. The original bead's explicitly incomplete acceptance
work is preserved, not claimed resolved by this rebase: G10 memory policy/census,
G1 superseding and refresh fixture, editor completion-deadline integration,
authenticated G11 wakeups and qualifying distributions, and GL lease refusal /
cancellation API. LeakSanitizer was not run.
