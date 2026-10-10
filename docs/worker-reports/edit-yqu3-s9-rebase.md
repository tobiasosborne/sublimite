# edit-yqu3 session 9 rebase report

Rebased `wt/edit-yqu3` onto main `ab20118`; replayed `086a26f` as `8c9b803`.
`git add` and `GIT_EDITOR=true git rebase --continue` completed successfully.
No other Git write commands were used. HANDOFF.md and docs/worklog/ were untouched.

## Conflicts and resolutions

- `bench/findui_bench.c`: retained main's `--count` and `--word` dispatch,
  word-pair flag, fixture options and supervision. Both default to the shared
  10000-sample minimum and use the bead's strict optional-count parser.
- `bench/findui_count.h`: retained main's plain/whole-word pair, independent
  sample populations, exact total/first-4096 offset checks and rotating
  initial/late/empty windows. Replaced fixed 64-entry arrays with dynamically
  allocated storage for every requested sample in each variant. Applied the
  bead's repeated 64 KiB workload to both variants and labelled every row with
  its size; no full-GiB throughput claim. The original 1 GiB all-a fixture is
  still validated before sampling. Sample storage is freed after reporting.
- `bench/scroll_bench.c`: combined the bead's shared work-proxy verdict with
  main's precise displayed-verdict=UNAVAILABLE explanation and required editor
  refresh-hook reference. Retained owned asynchronous seek correctness and
  cancellation checks, repeated indexed/null samples, and the maximum/outlier
  failure rule. The extra correctness frame is included in the frame count.
- `fuzz/scroll_fuzz.c`: kept main's diagnostic REQUIRE macro and wrapped visual
  model, and added the bead's fully indexed delegated-source error model.
  Both models run before the existing difficult/core/index models. Preserved
  resumed resolution/following helpers and added positive-count/NULL faults.
- `tests/findui_test.c`: retained all main regressions and their calls, and
  added the bead's opt-in default folded long-query regression. Updated its
  comment to acknowledge main's fix for #21; kept the historical environment
  switch for compatibility.
- `tests/savectl_test.c`: retained main's argc/argv entry point, every focused
  CLI mode and all normal regression calls. Added the bead's running/queued
  slot-reuse regression and historical environment switch. Updated its comment
  to acknowledge main's exact-work-handle fix for #12.

No production code changes or test weakening were required during resolution.
The bead's original decision/report statements that #12 and #21 remain known
failures are historical: both were fixed on the main commits now incorporated.
The opt-in expected-behavior fixtures both passed after the rebase.

## Verification

Release compiler: GCC 13.3.0. Sanitizer compiler: Clang 18.1.3.
Sanitizer and regression runs use `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99
EDIT_DISPLAY=:99`. Leak detection was OFF. Only display :99 was used.
A private Xvfb :99 uses a filesystem Unix socket (`-nolisten tcp -nolisten
local`) because the abstract socket was occupied. Sandbox socket restrictions
require the displayed suite to run with escalated socket access.

`make all`: exit 0. Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c tests/editor_large_test.c -o build/rel/tests/editor_large_test.o
gcc -O2 -g -msse2 -pthread build/rel/tests/editor_large_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/editor_large_test
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c bench/editor_large_bench.c -o build/rel/bench/editor_large_bench.o
gcc -O2 -g -msse2 -pthread build/rel/bench/editor_large_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/editor_large_bench
```

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check`:
initial sandbox run exit 2; XCB could not connect to the private display socket.
No source change was made for this environment failure. Initial tail:

```text
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
cli_test:61: FAIL c && !xcb_connection_has_error(c)
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1
```

The same command, rerun with socket access: exit 0. All 60 sanitizer test
binaries and the replay CLI shell checks passed. Final tail:

```text
x11_input_test: malloc guard inactive (ASan), allocation count not checked here
x11_input_test: ok
== build/san/tests/x11_live_test
§22 startup errors: ok
§25 window/Present/idle: ok
xi2 active, core wheel fallback 1
x11_live_test: ok
== build/san/tests/x11_order_test
x11_order_test: all ok
== build/san/tests/x11_stall_test
x11_stall_test: key callback reply 0.083 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.101 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.058 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.108 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.169 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.013 ms (M)[AC], budget 5 ms (G)[AC]: ok
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

Full logs: `/tmp/edit-yqu3-rebase-make-all.log`,
`/tmp/edit-yqu3-rebase-make-check.log` (initial sandbox run), and
`/tmp/edit-yqu3-rebase-make-check-display.log` (successful run).
No post-rebase code interaction fix was necessary.

Additional validation:

- Clang 18 syntax checks with C11, -Wall/-Wextra/-Werror/-Wshadow/-Wconversion
  passed for all three changed fuzzers (exit 0).
- Release `EDIT_YQU_KNOWN_FAILURES=1 build/tests/findui_test`: exit 0;
  `KNOWN default folded query: length=300 complete=1 error=0 count=1`.
- Release `EDIT_YQU_KNOWN_FAILURES=1 build/tests/savectl_test`: exit 0;
  `KNOWN unrelated slot reuse: destroy=0 expected=0 queued=64`.

## Reconciliation limits

No unresolved source conflicts. Large-file save/search throughput and actual
editor displayed-frame gates remain outside these smaller repeated benchmark
workloads, as both sides' decisions require. No benchmark gate qualification or
long fuzz campaign is claimed. Both required checks passed; nothing remains
unreconciled. This report is staged as a new file after the completed rebase;
no extra commit or amendment was made.
