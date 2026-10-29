The journal ownership change is sound. I found one existing command-gate deadlock that the new wake rationale overlooks, plus regression-test and runnable issues.

BLOCKER: none.

- **MAJOR — existing permanent gate cycle**, [commands.c:278](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/src/editor/commands.c:278), [retain.c:96](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/src/editor/retain.c:96). While Save As writes, press Ctrl+N and leave another event queued—even its key release suffices. Adoption sets `publish_owed`. The file command waits for that checkpoint; `queue_pick()` refuses queued input while a file command exists; retention refuses publication while `queue_count` is nonzero. Neither side can progress after all workers finish. Previously this state spun; this commit lets it eventually sleep indefinitely with input blocked. **Fix:** permit the owed checkpoint when input is parked behind the pending file command, retaining the journal, mutation and lease guards. Add a regression covering Save As → queued Ctrl+N plus trailing input. The decision document’s claim that every gate has a clearing wake is therefore incomplete.

- **MINOR — the burst regression does not guarantee its triggering condition**, [editor_session_test.c:2693](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/tests/editor_session_test.c:2693). Cumulative estimated TABS bytes crossing the sync window does not prove outstanding durability credit crossed it, or that `protected_record()` flushed an already-submitted pump. On a sufficiently slow run, periodic syncs can return credit throughout the burst, allowing unfixed code to pass. **Fix:** control the journal worker, exhaust credit, submit through `editor_step`, then perform an open that must flush before another editor drain. Assert that overlap explicitly. Also correct the comment’s `n = 126`: this fixture’s sum is `4n² + 60n`, crossing at 121 opens.

The worker’s three runnable observations are real, pre-existing spins:

- **MINOR — pointer scrolling**, [editor.c:834](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/src/editor/editor.c:834). An edge drag with input parked behind a save lease satisfies `runnable()`, while `editor_pointer_tick()` refuses `queue_count`. Once rendering settles, turns repeatedly do no useful work until the lease ends. **Fix:** share the pointer service’s readiness predicate, preserving independent layout/view continuations and worker wakes.

- **MINOR — Save As adoption**, [commands.c:260](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/src/editor/commands.c:260). The write can finish before a raster/GPU frame retires. `editor_save_as_runnable()` stays true while adoption refuses the active backend, forcing zero-timeout polling. **Fix:** share adoption’s readiness gates; retain the necessary retry for terminal delivery preceding physical job retirement.

- **MINOR — dirty frames waiting for journal progress**, [editor.c:844](/home/tobias/Projects/sublimite/.wt/edit-duuw-s27/src/editor/editor.c:844). After durability credit is exhausted, staged operations or refused metadata prevent submission, but `dirty` still forces immediate turns throughout the journal worker’s delay. **Fix:** distinguish runnable frame preparation from submission waiting on journal progress. Keep preparation continuations runnable and use mailbox/deadline wakes for the blocked submission.

A completion wake does not prevent these spins: `runnable()` makes `wait_timeout()` return zero before waiting. Their duration also has no guaranteed bound when I/O stalls.

For the central fix:

- `j->active` is the correct predicate at every replaced reader. The batch worker does not write it; receipt clears it, including receipt inside blocking flush.
- `editor_journal_waiting()` short-circuits before touching journal state during save or publish leases. Pump admission, deadlines, file commands, retention and save start/finish respect that ownership.
- I found no new double-pump, ordinary journal-completion wake loss, or additional stale flush/rotation bookkeeping. A consumed completion now immediately changes subsequent gate decisions.
- `duuw_gated` convincingly pins the runnable regression. The burst checks command completion, idle status, durability catch-up and close, subject to the triggering weakness above. Neither editor regression directly tests indefinite blocking with the mailbox as its sole wake.

No new typing-path allocation, G1 alteration, or exception-style error handling appears. GCC and Clang strict C11 syntax checks passed for every changed C file; `git diff --check` passed. Runtime suites were not rerun because their fixtures write files. Windows compilation remains unverified: its toolchain is absent, although the added journal API is platform-neutral.

LAND AFTER FIXES