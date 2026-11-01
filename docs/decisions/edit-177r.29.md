# edit-177r.29 — separate native child guards from editor settle policy

The reproduced Windows timeouts are harness deadlines. Continuing the same
close children beyond the old parent deadline completes the original close,
reopen and exact recovery assertions. Waiting for the executable's existing
`SublimiteTestReady` condition likewise completes startup and exact save/replay.
Seeded journal timeout samples show no issued edit or child error; completed
targets under the longer guard retain the independent byte oracle.

Use the existing OS conformance suite's whole-case guard for a native child
phase. The shared constant and derivation live in `tests/win/timeout_diag.h`.
(M) [AC, desktop Spiral], eight concurrent copies per affected suite: observed
readiness reached 17.134 seconds and the complete close/recovery child campaign
39.536 seconds. (E) Reusing 120 seconds gives more than three times that full
lifecycle measurement. The runner deliberately selects below-normal priority;
neither CPU scheduling nor native image/driver/file I/O promises the old
five-second settle deadline. This is a bounded hang guard, not a performance
gate. Later contention measurements are recorded in the worker report.

The executable crash fixture must make its edit/no-PAINT check atomic with
the kill. A journal scan followed by TerminateProcess permits UI composition
to arm PAINT in between. (M) The probe observed the same source journal
change from sequence 26 / PAINT buffer 0 to sequence 27 / PAINT buffer 1 after
the parent had accepted the marker. Recovery correctly quarantines a tab
killed inside that guarded first paint. A longer save wait cannot change it.
Suspend only the test-created UI thread, synchronize with GetThreadContext,
rescan, then either kill while paused or resume/retry. Keep exact recovery,
undo and saved-byte assertions. No production change follows from that race.

The journal's full existing script is bounded at 100000 edits with two native
progress writes per edit and a flush every 4096. (M) The slowest completed red
target was 83865 edits in 3.718 seconds. (E) Scaling that sample to the script
gives less than 4.434 seconds; the guard has more than 27 times that allowance.
The guard includes native startup. A parent still cannot kill for replay until
the exact target condition holds; expiration is a failure.

A normal editor child receives one native-phase guard. The nested crash-guard
campaign receives three: its own phase and two serial supervised children.
Keep editor-local settle loops and the deliberate occlusion-observation sleep
separate. Executable native completion/death waits use the child guard:
(M) the original five-second death wait also timed out after TerminateProcess.
Linux retains its previous waits and behavior.

Retain test-only timeout evidence: journal progress, editor stage/queue state,
process priority/CPU/I/O and native wait chains, scoped to test-owned PIDs.
Native wait chains alone cannot rule out a lost wakeup; completion of the
unchanged byte and ordering assertions after the old deadline is the decisive
evidence here. No production or journal-format change is warranted.
