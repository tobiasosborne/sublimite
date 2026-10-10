# edit-czn session 9 rebase report

This session replayed `4bb4ad8` onto main `f6fbce4` on `wt/edit-czn`.
`GIT_EDITOR=true git rebase --continue` exited **0** and produced `f500c4e`.
Git then reported `wt/edit-czn` with a clean working tree. This report
supersedes the older rebase evidence inherited in the replayed commit.
The bead intent and decisions for edit-czn, edit-zzj.12, edit-lez, and edit-mdv
were read. Only `git add` and `GIT_EDITOR=true git rebase --continue` are
used as Git write commands. HANDOFF.md and docs/worklog/ are untouched.

## Conflicted files and resolutions

- `bench/editor_bench.c`: preserve main's submitted-damage (T4) settle predicate,
  delayed-Present self-check, and separate readiness (T5) helper, including its
  `caret_only = false` and `do ... while (true)` loop. Preserve the bead's
  deadline diagnostics in both settle paths, quiescent trace export, and
  `--partial-index` setup in all typing scenarios.
- `src/editor/STATUS.md`: retain both the landed raster G11 caret-path section
  and the bead's deep-file line-start section with their original limitations.
- `tests/editor_test.c`: retain both `--raster-blink` and `--line-start-only`
  dispatches. Both regressions remain in the default suite.

The automatically merged editor/input/private-header changes retain the bead's
bounded certified line anchors, joined-newline repair and partial layout queries,
and main's caret rendering/event-driven Present integration. Main's findui and
savectl changes remain intact. All tests passed without a module implementation interaction fix. The only
additional benchmark adjustment during resolution was to put the bead
timeout diagnostic in both main settle helpers; their success predicates
and T4/T5 endpoints were preserved.

## Verification

Release compiler: GCC 13.3.0. Sanitizer compiler: Clang 18.1.3. Both use
the Makefile's C11 warning flags, including `-Werror`.

`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all` exited **0**. Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
```

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check`
exited **0**: **60 sanitizer test binaries plus replay CLI passed**.
Leak detection was **off** throughout the sanitizer run. Tail:

```text
x11_live_test: ok
== build/san/tests/x11_order_test
x11_order_test: all ok
== build/san/tests/x11_stall_test
x11_stall_test: key callback reply 0.060 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.076 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.041 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.067 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.058 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.009 ms (M)[AC], budget 5 ms (G)[AC]: ok
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

Both sides' editor regressions passed: partial-index deep typing and viewport
newline joining, clipped-line edit/undo, raster caret blink with zero worker
jobs, and the existing idle/unfocus behavior. Main's findui, savectl,
large-file/P4 editor, raster, and X11 tests also passed.

Supplemental `DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --self-check`
exited **0**, printing:

```text
editor_bench: settle permits undamaged submitted frame with delayed Present completion
```

Full logs: `/tmp/edit-czn-current-rebase-all.log` and
`/tmp/edit-czn-current-rebase-check.log`.

The sandbox could not connect to :99 or bind an Xvfb listener. An inspection
outside the sandbox confirmed the existing :99 server was accessible, so
`make check` ran outside the sandbox against that server. No test was skipped
because of this access issue, and no other display was used. The bead's
already merged X11 fixture honors explicit DISPLAY=:99 and EDIT_DISPLAY=:99.
Git staging and rebase continuation also required access outside the sandbox
because the worktree index resides in the parent repository's .git directory.

The final report update is staged after the successful rebase. No further
commit or amend was made, in accordance with the permitted Git write commands.

## Unreconciled items

No unresolved source conflicts. Performance gate certification and leak-enabled
testing are outside this task; the bead's existing G1/giant-line limitations remain.
