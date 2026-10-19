# edit-3oew: cli_test's hot-exit block waits for the first present; its cleanup removes fi2 retention state

Bead edit-3oew (P2, src:main). Worker session s9, 2026-10-20. Test-only change:
tests/cli_test.c. No production code changed: hot exit did not regress.

## Findings (reproduced on :98, sanitized cli_test)

1. `cli_test:499` (after.count/active/len) and `:567`: the relaunch restored
   0 tabs, active -1, length 0, against 1/0/7 before. The killed journal's
   PAINT records were `1` with no following `0`: the test sent SIGKILL while
   the tab's first frame of that launch was still unpresented. edit-dzk1's
   crash-loop guard (3b23f0a, PAINT type 11) then restores that tab closed,
   by design (dzk1 §2: text kept, notice, Ctrl+Shift+T reopens). The test's
   readiness check reads only text, view and window size from the journal;
   all three can be durable before the first present. (M) In instrumented
   runs PAINT 0 followed within one test poll interval (10 ms, `pause_poll`)
   of readiness; about one SIGKILL run in three hit the window before the
   fix. Not a regression a user can meet: a user types into a window that
   has already been presented, and SIGTERM/SIGINT exits append PAINT 0 at a
   clean close.
2. `cli_test:582` (`rmdir(dir)`): the directory still held a
   `.sublimite-base-*` copy (edit-fi2, 664fd81, 2026-10-18). In every
   instrumented run it was the SIGKILLed launch's copy, made after the typed
   `x` and not yet referenced by its journal (BASE was still `file.txt`):
   fi2's documented crash window ("a crash mid-copy leaves a partial
   unreferenced temp"; startup housekeeping sweeps it). fi2 updated the
   editor_test and editor_session_test fixtures to remove such copies; it
   missed cli_test, which is why the cleanup failure dates from 2026-10-18
   (main f436eb3), before today's landings.
3. The `error -103; journal: ...` child is expected: it is `session(...,
   fail_dump=true)`, whose trace dump path is a directory, and the test
   asserts exit 1 there. Its journal is consumed by the next launch's
   recovery; it was never what blocked the rmdir. y315, 9fzc, 7cj8 and e3z5
   are not involved.

## Choice

- `hot_snapshot` records the last PAINT id (`UINT64_MAX` until a PAINT is
  seen, so "no PAINT yet" never counts as presented). Readiness additionally
  requires `paint == 0`: kill only after the first present, the state a user
  who typed into a visible window is in.
- The final cleanup unlinks `.sublimite-base-*` and `.sublimite-copy-*`
  like editor_session_test's fixtures (edit-fi2 / edit-7cj8 cleanup), before
  the existing `journals == 1` and `rmdir` assertions.

Not chosen: asserting that the leftover copy is named by the surviving
journal (it is not, by fi2's crash window; fixing that window is fi2's
"Remaining" item, outside this bead), or relaxing the restore assertions.
