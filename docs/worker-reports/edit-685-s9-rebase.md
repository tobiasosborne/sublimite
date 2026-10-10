# edit-685 session 9 — current rebase onto main

This report records the current replay of `f397d73` onto `e80bd51` as
`d323ac1` on `wt/edit-685`. It supersedes the older rebase evidence included
in the replayed commit, including `edit-685-s9-rebase-final.md`.

## Conflict resolution

The only conflicted file was `tests/editor_p4_test.c`, with one conflict hunk
covering the end of the test helpers and the start of `main`.
Read `docs/worker-reports/edit-685-s9.md` and decisions `edit-685.md`,
`edit-k60.md`, `edit-og2.md`, `edit-fff.md`, and `edit-o4o.md`; inspected
`REBASE_HEAD`, main's latest three commits, and both sides of the conflict.

- Retained main's `wait_count`, `wait_client`, and `ipc_wait_sweep` helpers
  and the call testing disconnected waits, retained/reopened buffers,
  live-token preservation, association reuse, and the allocation guard.
- Used the bead's `main(int argc, char **argv)`, both focused command-line
  modes (`--indent-limit-enter` and `--indent-limit-brace`), and both default
  selection LIMIT/retry regression calls.
- Retained main's `test_ipc_isolate()` runtime and private namespace instead
  of the bead's old local `mkdtemp` setup, preserving concurrent-suite IPC
  isolation and process-owned cleanup.
- The production input/indent changes and other tests merged without conflict.
  Preserved main's capacity admission and successful mutation-error prefix
  policy together with pure post-selection indent decisions before mutation.
  Main's display-specific routing and disconnected-wait sweep remain intact.

The only Git write commands were `git add tests/editor_p4_test.c` and
`GIT_EDITOR=true git rebase --continue`. The first continuation returned 1
because parent-repository Git metadata was read-only in the sandbox. The same
command succeeded with elevated filesystem access, returning 0. `git status`
confirmed the completed rebase and a clean worktree before this report update.
No HANDOFF.md or docs/worklog/ files were touched.

## Validation

Release compiler: GCC 13.3.0. Sanitizer compiler: Clang 18.1.3.
C11 warnings include `-Wall -Wextra -Werror -Wshadow -Wconversion`.
Every display setting used `DISPLAY=:99 EDIT_DISPLAY=:99`; :0 was never used.
Leak detection is **off**, as requested: `ASAN_OPTIONS=detect_leaks=0`.
The sanitizer configuration enables both ASan and UBSan.

`DISPLAY=:99 EDIT_DISPLAY=:99 make -j6 all`: **exit 0**.
Full log: `/tmp/edit-685-current-rebase-make-all.log`.
Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
gcc -O2 -g -msse2 -pthread build/rel/tests/gl_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/gl_test
```

Supplementary release `indent_test` and the two focused `editor_p4_test`
selection LIMIT modes each exited 0. All three report active allocation guards
with zero allocations. The Enter/brace modes retain all three LIMIT scenarios,
synchronous retries, and atomic undo/redo after a successful giant selection.

## Sanitizer check

`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j6 check`:
**exit 0**. Ran with access to the existing Xvfb :99 and local IPC sockets.
Full log: `/tmp/edit-685-current-rebase-make-check.log`.
All **61 ASan/UBSan test binaries passed**, along with replay CLI and the
complete display-lock shell contract. In particular, `editor_p4_test` passed
both the bead's selection LIMIT/retry tests and main's disconnected-wait sweep,
retained-buffer, live-token, and association-reuse regressions. `ipc_test`
passed the private/display-specific routing and launcher wait tests.

The original unmodified make check command completed in one run. There were
shared-display waits before several binaries, including raster_test; all were
allowed to complete. No process was terminated, no display lock bypassed, no
assertion changed, and no test skipped. The timeout/error lines in the final
tail are expected negative probes inside the display-lock contract; the outer
make check returned 0. The GL test likewise printed its deliberate injected
arena/prepare failures before passing.

Tail:

```text
x11_stall_test: direct callback reply 0.052 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.036 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.218 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.102 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.010 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 61 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 4046225)
true: display lock timeout: display :99 (held by 4046225)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 4046225)
refwin_test: display lock timeout: display :99 (held by 4046225)
make[1]: warning: jobserver unavailable: using -j1.  Add '+' to parent make rule.
== build/san/tests/x11_identity_test
waiting for display :99 (held by 4046225)
x11_identity_test: display lock timeout: display :99 (held by 4046225)
make[1]: *** [Makefile:105: check] Error 1
waiting for display :99 (held by 4046225)
sh: display lock timeout: display :99 (held by 4046225)
waiting for display :99 (held by 4046225)
sh: display lock timeout: display :99 (held by 4046225)
waiting for display :99 (held by 4046225)
sh: display lock timeout: display :99 (held by 4046225)
waiting for display :99 (held by 4046225)
sh: display lock timeout: display :99 (held by 4046225)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

## Reconciliation status

Both sides' changes have been reconciled and the rebase is complete at
`d323ac1`, with `e80bd51` as its parent. Nothing remains unreconciled.
No additional interaction fix or production-source edit was needed: the only
conflict-resolution edit combined the test entry point and retained all main
helpers and IPC isolation. Both required commands exited 0.
Leak detection was off throughout (`ASAN_OPTIONS=detect_leaks=0`).

This report update is left uncommitted after the completed rebase; no extra
commit, amendment, branch change, reset, or push was performed.
