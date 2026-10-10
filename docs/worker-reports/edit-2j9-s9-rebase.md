# edit-2j9 session 9 rebase report

Resolved replay of `b416ffe` onto main `54b0e26`; completed rebase commit
`a9ac75e` on `wt/edit-2j9`.

## Conflict resolution

The only conflicted file was `bench/editor_large_bench.c`, in `open_ed`.
Read `docs/worker-reports/edit-2j9-s9.md` and the decisions in
`docs/decisions/edit-2j9.md`, `edit-dnc.md`, and `edit-dnc2.md`.

Kept the bead's canonical source path, private journal fixture and journal-enabled
editor configuration, so shared per-key accounting can verify mutation/journal
counts. Kept main's conditional recording of the first submitted frame after
`editor_open` returns and the file's trace identity is available. This preserves
attribution for main's mapped-prefix viewport, which now submits inside open
before full attachment. Kept both viewport dimension checks and all main's
viewport-before-attachment diagnostics, which merged without conflict.
All other bead files merged without conflicts; the restored workloads, structural
verdicts, supervised save deadlines, fuzz invariants and semantic scroll intent
comparison remain present. No additional interaction fix has been needed.

Ran `git add bench/editor_large_bench.c` and `GIT_EDITOR=true git rebase --continue`.
The first sandboxed continue returned 1 because Git metadata outside this
worktree's writable root was read-only; the identical authorized command with
host access returned 0. No other Git write command was used.

## Verification

`make all CC_RELEASE=gcc-13`: exit **0**. GCC 13.3.0, C11 release flags include
`-Wall -Wextra -Werror -Wshadow -Wconversion`.
Full log: `/tmp/edit-2j9-s9-rebase-make-all.log`.

Tail:

```text
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/savectl_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/savectl_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/scan_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/scan_bench
gcc-13 -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c bench/scroll_bench.c -o build/rel/bench/scroll_bench.o
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/scroll_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/scroll_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
MAKE_ALL_EXIT=0
```

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check CC_SAN=clang-18`:
**exit 0**. Clang 18.1.3, ASan/UBSan. Leak detection is off.
All 63 sanitizer test binaries, replay CLI and display-lock contract passed.
The first sandboxed attempt compiled the sanitizer binaries, passed base_test,
and waited for the shared display lock. It was stopped deliberately and
restarted with host display access (initial attempt exit **143**, deliberate
termination); no source was changed for this environment adjustment. Final log: `/tmp/edit-2j9-s9-rebase-make-check-host.log`.

Final run tail:

```text
x11_stall_test: clipboard set return 0.019 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 63 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 1020272)
true: display lock timeout: display :99 (held by 1020272)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 1020272)
refwin_test: display lock timeout: display :99 (held by 1020272)
== build/san/tests/x11_identity_test
waiting for display :99 (held by 1020272)
x11_identity_test: display lock timeout: display :99 (held by 1020272)
make[1]: *** [Makefile:105: check] Error 1
waiting for display :99 (held by 1020272)
sh: display lock timeout: display :99 (held by 1020272)
waiting for display :99 (held by 1020272)
sh: display lock timeout: display :99 (held by 1020272)
waiting for display :99 (held by 1020272)
sh: display lock timeout: display :99 (held by 1020272)
waiting for display :99 (held by 1020272)
sh: display lock timeout: display :99 (held by 1020272)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
MAKE_CHECK_EXIT=0
```

Initial attempt tail (`/tmp/edit-2j9-s9-rebase-make-check.log`):

```text
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
make: *** [Makefile:105: check] Terminated
MAKE_CHECK_EXIT=143
```

Main's mapped-preview/IPC/find/index regressions passed in `editor_large_test`.
The bead's harness completed 20,253 checks with zero failures, including phase
and retirement deadlines and logical-padding continuation (513 slices).
The large raster conformance test passed. GL pure checks passed; native EGL/
Present diagnostics were skipped because that context is unavailable on :99.
Expected injected CLI startup timeout, GL allocation faults and display-lock
contract timeout/nested-make errors are negative checks, not suite failures.

Additional `DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_large_bench --self-check`:
exit **0**. It verified TRACK structural refusal, delayed injection-to-T4
accounting and allocation/content policy. The diagnostic about unavailable
fixtures is intentional in this self-check's missing-fixture test.

## Outstanding

No reconciliation issues remain. Both required final commands exited 0; no
additional source/test interaction fixes were required. Shared display waits
delayed final suite completion beyond the 20-minute target, but it finished
before the 25-minute cutoff. Existing scope limits from both sides
(full benchmark certification, newline-free settle limitation, and pre-attachment
full-index API gap) are unchanged; this rebase makes no performance gate claim.
`HANDOFF.md` and `docs/worklog/` were not touched. This report was written after
the rebase and is intentionally left as an untracked file.
