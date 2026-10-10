# edit-9lf session 9 rebase report

Rebased `wt/edit-9lf` onto main (`bcbffb5`); replayed bead is `45f8b41`.
Read `edit-9lf-s9.md` and decisions for edit-9lf, edit-ck4, edit-dnc and edit-apl.
Only Git writes used: `git add` and `GIT_EDITOR=true git rebase --continue`.
Git metadata required sandbox escalation; the authorized retry succeeded.
No HANDOFF.md or docs/worklog/ files were changed. Display :99 only.

## Conflicts and resolutions

- `src/editor/STATUS.md`: retained both workers' status sections and references.
  They describe their original sessions; integrated behavior is described here.
- `src/editor/private.h`: retained both `editor_journal_metadata` and
  `editor_event_control` declarations, plus all nonconflicting state additions.
- `tests/editor_test.c`: combined headers (`dirent.h`, `stdarg.h`, `sys/syscall.h`,
  `sys/stat.h`, `sys/socket.h`), deduplicating `sys/stat.h`; retained both test sets.
- `src/editor/editor.c`: retained owner/generation/serial queue selection and
  bypass dispatch. Park every queued event while journal operations are staged,
  preserving main's borrowed-tree lifetime/immutability rule. Controls can still
  bypass pending COPY edits when journal staging is clear. Frame and journal
  publication guards, metadata and dedicated journal pool remain intact.
- `src/editor/buffers.c`: adoption refuses retired/replaying owners and keeps
  failure local to an installed owner. Retained main's immutable prefix BASE
  presence check rather than reintroducing synchronous `journal_check_base` at
  adoption; file identity is still checked. Keep retains main's complete durable
  checkpoint and dirty marking, plus pending-open refusal and prompt reset.
  Reload retains owner/generation admission, waits/adopts a pending full source,
  clamps/recomputes the view, and keeps tab ID/order/MRU and wait membership.
  It checkpoints the replacement before calling `tabs_replace`; a failed
  checkpoint restores the suspended original. Old storage is retired instead
  of synchronously destroyed. Save-busy/staged operations block replacement;
  fixed reservation exhaustion refuses it safely. Retained main's staged-range
  retirement guard and the bead's worker cancellation/readiness retirement.

## Minimal interaction adjustment

`src/editor/checkpoint.c` skips retired buffers in both record sizing and encoding.
Reload now retains the old lease nonblockingly; including it in a checkpoint
would publish duplicate logical IDs or reject the session on its obsolete BASE.
Live and closed tabs still retain their checkpoint records because their buffers
are not retired. This is the only production file changed beyond the original
conflict/nonconflict bead files.

## Validation

GCC: 13.3.0. Clang: 18.1.3. `make all` uses the repository release flags,
including `-Werror`. `make check` uses ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99`.
**Leak detection is off.**

`make all`: exit 0. Full log: `/tmp/edit-9lf-s9-rebase-all.log`.

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
```

The first sandboxed `make check` compiled all sanitizer binaries, passed
`base_test`, then waited at `cli_test` for the shared :99 lock. It was stopped
with exit 130 and restarted with authorized local IPC/X11 access. Its log is
`/tmp/edit-9lf-s9-rebase-check.log`.

The restarted `make check`: **exit 0**. All 63 sanitizer test binaries,
replay CLI contract and display-lock contract passed. Leak detection was off;
only display :99 was used. Log: `/tmp/edit-9lf-s9-rebase-check-ipc.log`.
The final contract deliberately holds the display lock and expects child
commands/nested make to fail with lock timeouts; those errors in the tail are
negative-test evidence, not failure of the outer `make check`.

Final tail:

```text
x11_stall_test: clipboard set return 0.016 ms (M)[AC], budget 5 ms (G)[AC]: ok
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
waiting for display :99 (held by 1039396)
true: display lock timeout: display :99 (held by 1039396)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 1039396)
refwin_test: display lock timeout: display :99 (held by 1039396)
== build/san/tests/x11_identity_test
waiting for display :99 (held by 1039396)
x11_identity_test: display lock timeout: display :99 (held by 1039396)
make[1]: *** [Makefile:105: check] Error 1
waiting for display :99 (held by 1039396)
sh: display lock timeout: display :99 (held by 1039396)
waiting for display :99 (held by 1039396)
sh: display lock timeout: display :99 (held by 1039396)
waiting for display :99 (held by 1039396)
sh: display lock timeout: display :99 (held by 1039396)
waiting for display :99 (held by 1039396)
sh: display lock timeout: display :99 (held by 1039396)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
MAKE_CHECK_EXIT=0
```

Focused existing release regressions both exit 0: `--ck4-reload` verifies abrupt
exit recovery of the selected, unrelated and closed buffers; `p4-16` verifies
tab/view identity and real launcher wait completion. Log:
`/tmp/edit-9lf-s9-rebase-focused.log`.

A build-only copy of `editor_test.c` adds a second complete checkpoint after
Reload, then retains the existing abrupt-exit/exact-session recovery assertion.
Both COPY and mapped variants pass under Clang ASan/UBSan (exit 0, leaks off).
The first diagnostic omitted the journal API's required flush and was refused;
the corrected diagnostic flushes first, as production Reload does. No production
or checked-in test changes resulted. Source/binary:
`build/s9-rebase-checkpoint-test.c`, `build/s9-rebase-checkpoint-test`.
Log: `/tmp/edit-9lf-s9-rebase-repeat-checkpoint.log`.

```text
repeat checkpoint rc=0
P4-3 section 4: mode=COPY choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
repeat checkpoint rc=0
P4-3 section 4: mode=mapped choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
COPY_EXIT=0 MAPPED_EXIT=0
```

The default suite passed without changing or weakening any checked-in test.
No test from either side failed due to the integration. `git diff --check` is
clean. The rebase is finished and the only subsequent tracked artifact is this
report (staged separately because no additional commit/amend is authorized).

## Unreconciled requirements

No source conflict was left unresolved. The bead's pre-existing incomplete
requirements remain: bounded undo record/memory accounting (opt-in `p4-27` is
intentionally red), prompt physical file reclamation/retirement API, and complete
controller/silent reload behavior. They were not broadened into this rebase task.
The main raster capacity finding remains unchanged; no gate exemption is claimed.
