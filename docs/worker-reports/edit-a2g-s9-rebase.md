# edit-a2g session 9 rebase report

Rebased `wt/edit-a2g` onto main `046203a`; replayed `2a53038` as `de15011`.
`GIT_EDITOR=true git rebase --continue` exited 0. No other Git write operations
were used except `git add` (the initial sandbox attempt could not write the Git
index; the authorized escalated retry succeeded).

## Conflicts and resolution

Only `src/editor/buffers.c` was conflicted, in the source-choice Keep path.
Read `docs/worker-reports/edit-a2g-s9.md` and decisions `edit-a2g.md`,
`edit-ck4.md`, `edit-9lf.md`, and `edit-apl.md`.

Preserved main's pending-open and ID/generation guards, disk BASE capture,
journal flush and complete checkpoint, BASE rollback on failure, conservative
saved-history dirty state, prompt reset and `editor_modified`. Added the bead's
`editor_large_keep(b)` after successful reconciliation and suspension release,
before full layout. Failed journal reconciliation leaves the source suspended
and does not restart background work. Main's Reload tab/view/wait identity and
retirement implementation remain intact. The bead's automatic coordinate,
paint, worker, resident bridge and test changes were retained.

Minimal lifecycle interaction fix in the same file: `editor_retire_buffers`
now cancels `b->lg.find_retiring_h` and requires that lease's physical completion
before destroying a nonfile buffer. Main checked only active find/warm leases;
the bead adds a separate retiring find lease, and its destructor waits for it.
Including this handle in readiness preserves main's nonblocking UI retirement.
This was found by inspection before the build, not by a failing test.

## Validation

GCC 13.3.0 release build with C11 `-Wall -Wextra -Werror -Wshadow -Wconversion`:
`make -j4 all` exited **0**. Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/scroll_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/scroll_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/find_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/find_bench
```

Clang 18.1.3 ASan/UBSan full suite:
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check`.
Leak detection was **off**. Run escalated for the required local IPC/Xvfb access;
all display use is :99, including the repository's shared display lock.
`make -j4 check` exited **0**: all 63 sanitizer test binaries passed, followed
by the replay CLI and display-lock contracts. The lock timeout/error lines in
the tail are intentional negative cases in the passing display-lock contract.
GL native checks reported their documented unavailable-EGL skip on this Xvfb;
its pure snapshot/completion/trace checks passed. No new test interaction failure
occurred and no post-rebase source/test edit was needed. Tail:

```text
x11_stall_test: direct callback reply 0.086 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.077 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.140 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.112 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.012 ms (M)[AC], budget 5 ms (G)[AC]: ok
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
waiting for display :99 (held by 1136714)
true: display lock timeout: display :99 (held by 1136714)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 1136714)
refwin_test: display lock timeout: display :99 (held by 1136714)
make[1]: warning: jobserver unavailable: using -j1.  Add '+' to parent make rule.
== build/san/tests/x11_identity_test
waiting for display :99 (held by 1136714)
x11_identity_test: display lock timeout: display :99 (held by 1136714)
make[1]: *** [Makefile:105: check] Error 1
waiting for display :99 (held by 1136714)
sh: display lock timeout: display :99 (held by 1136714)
waiting for display :99 (held by 1136714)
sh: display lock timeout: display :99 (held by 1136714)
waiting for display :99 (held by 1136714)
sh: display lock timeout: display :99 (held by 1136714)
waiting for display :99 (held by 1136714)
sh: display lock timeout: display :99 (held by 1136714)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Full logs: `build/worker-logs/edit-a2g-s9-rebase-all.log` and
`build/worker-logs/edit-a2g-s9-rebase-check.log`.

## Reconciliation limits

No conflict intent was left unreconciled. The bead's previously reported
incomplete findings and opt-in red reproductions remain as documented in its
original report; this rebase does not claim to finish the whole bead.
`git diff --check` passed. `HANDOFF.md` and `docs/worklog/` were not touched.
