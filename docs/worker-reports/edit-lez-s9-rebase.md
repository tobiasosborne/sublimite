# edit-lez session 9 — rebase resolution

Rebased `wt/edit-lez` onto main successfully. The replayed bead was
`7eb32ab` (now `9c4cfe1`). `git add` and `GIT_EDITOR=true git rebase --continue`
both exited 0; the worktree returned to its branch with no unresolved files.
Only the authorized git write commands were used. HANDOFF.md and docs/worklog/
were not edited. All display-dependent validation used :99.

## Conflicts and resolutions

- `src/findui/findui.c`: retained main's `invalidate_source` calls before service
  and message adoption. Used the bead's `reap(impl, false)` so typing service
  does not release leased final owners. Retained replacement-page message
  routing and generation checks, while invalidated backing still rejects
  messages. The automatically merged stale-source check in replace_step is
  preserved alongside the bead's paged replacement, byte slices and host hooks.
- `src/findui/worker.c`: retained the bead's bounded outbox and `work_continue`
  delivery, replacing the sleeping mailbox retry. Kept main's backing-fault
  check in both initial publication and continuation delivery, so faulted
  snapshots cannot publish buffered results.
- `fuzz/findui_fuzz.c`: retained main's randomized independent first/visible
  capacities, overflow oracle, partial mailbox drains, cancellation and
  allocation-failure probes. Removed the obsolete expectation that replace-all
  beyond the first cache returns LIMIT. Combined its replacement fault/cancel
  schedule with the bead's service/drain/pause loop for asynchronous pages.
  Navigation retains main's partial scheduling; oracle checkpoints settle it.

Read the bead's worker report and decisions, plus main's edit-yqu and
edit-4w1.55 decisions. The nonconflicting benchmark changes retain main's
qualified sample requirements and the bead's exact first-offset/window checks.

## Minimal interaction fixes

1. Added `file_snapshot_faulted(slot->snapshot)` alongside cancellation polling
   throughout the new `src/findui/literal.c` visitor. This extends main's
   retained-backing validity rule to the bead's new literal kernels.
2. The existing lifecycle fuzzer failed with the one-byte input `80` (hex),
   exit 77, at the dropped_full assertion: bounded word publication cannot
   saturate the mailbox with a one-entry cache anymore. Added a worker that
   fills all WORK_MAILBOX_CAP entries before submitting the search. The same
   source/query/window cancellation and last-mapping-release assertions remain.
   The exact input then exited 0 (3 ms).
3. Fuzzing found input `00 20 9e a5 00 20 62 11 02 01` (hex), exit 77,
   at `state.match_index == m->selected`. A window change during pending
   uncached navigation now preserves the requested selection. Removed the
   oracle's old window-change reset to ordinal zero. The exact input exited 0.
4. In `tests/file_test.c`, changed only `p1_find_hook`'s injection threshold
   from its second invocation to its first. The persistent literal visitor has
   one scan hook per pass, so this preserves fault injection during search
   instead of silently falling back to injection after compute. The separate
   line-index span hook is unchanged. Clang ASan/UBSan `FT_CASE=p1-fault`
   exited 0, ending with `P1-1 section 2: ok` and `file_test: ok`.

The lifecycle/oracle fixes and updated file-test hook were made after rebase
completion and are staged with this report; no follow-up commit was made.

## Verification

Leak detection was OFF: all sanitizer and fuzz executions used
`ASAN_OPTIONS=detect_leaks=0`. GCC 13 release uses the Makefile's C11 -Werror
flags; Clang 18 check uses its ASan/UBSan configuration. Forced builds avoid
stale artifacts noted in the original report.

Release: `DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 all CC_RELEASE=gcc-13`,
exit 0. After the file-test hook update,
`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all CC_RELEASE=gcc-13` also exited 0.
Forced release tail:

```text
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
```

Final release tail:

```text
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
gcc-13 -O2 -g -msse2 -pthread build/rel/tests/file_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/file_test
```

Sanitizer command:
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 check CC_SAN=clang-18 CC_RELEASE=gcc-13`.
The file test was rebuilt with Clang 18 after its hook edit, before the running
suite reached that binary. Its full test and targeted fault test both passed.
Full-suite exit code: **0** (59 test binaries and replay CLI checks passed).
Tail:

```text
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

Additional verification: release findui suite exited 0, ending with
`findui_test: all passed`. Final Clang 18 ASan/UBSan findui fuzz campaign
(`-max_total_time=60 -max_len=384`) exited 0. Tail:

```text
"\001\000\000\000\000\000\000A" # Uses: 0
###### End of recommended dictionary. ######
Done 42151 runs in 61 second(s)
```

`git diff --check` exited 0. Logs for these runs are in
`/tmp/edit-lez-rebase-all.log`, `/tmp/edit-lez-rebase-all-final.log`,
`/tmp/edit-lez-rebase-check.log`, and `/tmp/edit-lez-rebase-fuzz-final.log`.
The sanitizer suite required access outside the sandbox to the existing :99
Xvfb; no display or CLI code was changed.

## Remaining limitations

No conflict intent was discarded. The bead's documented incomplete findings
remain: persistent whole-word regex visitation (§22), bounded multibyte/regex
window scans and immediate window priority (§25), real editor mutation binding
(§29), and editor/save storage-lease integration (§30). This rebase does not
claim those gaps are fixed or that loaded benchmarks passed a performance gate.
LeakSanitizer remains to be rerun with leak detection enabled.
