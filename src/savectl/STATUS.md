# savectl — edit-mdv continuation

Session 9: P4-modules-2 review §§9–17 have implementation and red/green
regressions. §1's session 8 full-identity torn-copy protection remains intact.
Design: docs/decisions/edit-mdv.md. Required standalone public report:
docs/worker-reports/edit-mdv-s9.md.

Done: nonblocking/type-safe FIFO acquisition; journal BASE conflict actions;
host-provided content identity with independent monotonic revision; exact work
handle retirement; immutable file-backed reload snapshots with bounded scratch;
owner-thread staged piece construction; failed-install state plus exclusive
reservation rollback/retry; retained file-owned actual original identity/fault
lease; sealed generation-validated mailbox-only terminal completion, retrying
via cooperative work continuation on saturation.

Current integration constraints: caller supplies unique history identities and
an invalid saved identity after history eviction. Replacement metadata requires
a reclaiming allocator or paired mark/reset hooks for an exclusive reservation.
The temporary snapshot needs O_TMPFILE support, writable target parent and disk
space; acquisition errors preserve the old tree. The file source guard rejects
all changed original metadata, including detached-inode ctime changes. Repeated
mapped saves after detachment may therefore require reload. Returned trees and
snapshots retain their backing independently, while the host must keep their
allocator context alive. Final graph cleanup is maintenance work.

Slice 3 now implements review §19 prepare-stage mailbox ownership handoff,
file-only writing without a whole-session journal lease, and explicit exclusive
finish reacquisition. The retained UI token is available after handoff, including
while file work is active. A process-kill regression verifies other-buffer
append/view recovery while a save is paused before rename.

§20 adds savectl_recover_finish: complete CURRENT-session fresh-inode rotation,
then retained-token finish reconciliation. Failed rotation/finish retains the
token and needs_finish. Append/directory retry and writeback-loss recovery have
distinct public guidance and regressions, including exact two-buffer replay.

§18's requested standalone scope is complete: fixed-size immutable request
admission, worker checkpoint preparation and worker-only context retirement,
plus the pending saving-frame model and actual-submission acknowledgement hook.
The public header and docs/decisions/edit-mdv.md specify exactly what the future
editor wiring bead must call. That host must maintain/pin published roots and
snapshots using bounded reserved-storage bookkeeping. Actual large dirty-session
and busy-backend submitted-frame G8s measurement remains editor integration work.
No src/editor/file/journal implementation changes were made in this slice.

Current slice report: docs/worker-reports/edit-mdv3-s9.md. Acceptance is incomplete:
the full-duration fuzz run aborts on pre-existing acquisition/recovery oracle
assertions, reproduced against original controller/fuzzer sources and left
unchanged per the scope restriction. The coordinator must repair/reassign those
oracles and rerun full fuzz and LeakSanitizer before closing acceptance.

Slice 3 final validation: release make all passed; the full clang ASan/UBSan
make check passed 60 binaries (M)[AC] plus replay CLI checks on DISPLAY=:99.
Both savectl sanitizer binaries passed independently; the release request test
also exercised the real counting allocator and admitted without allocation.
LeakSanitizer remained disabled in the worker sandbox. Final fuzz attempted
-max_total_time=60 but aborted at the unchanged reload oracle; the standalone
report includes both baseline reproducers and the missing clean-run requirement.

Earlier-session verification (private DISPLAY=:99 only):

```
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_reload_test
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64
```

Earlier-session release all and the complete sanitizer check exited zero; the check
passed 59 binaries (M)[AC] and its replay CLI shell suite. Earlier-session savectl fuzz
passed 24306 executions in 61 seconds (M)[AC]. Red/green and final logs are
recorded in the worker report. LeakSanitizer is disabled in the worker sandbox; coordinator reruns with
leaks enabled. Original bench scope and other review findings outside §§9–20
have not been silently changed.
