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

Remaining in strict order: §18 bounded immutable session save capture, off-path
checkpoint construction and actual submitted saving-frame G8s measurement; §19
prepare journal ownership handoff and finish reacquisition/crash test; §20
fresh-inode writeback recovery guidance and retained-token reconciliation test.
The editor still has no savectl save/status-frame integration. The journal
requires a distinct private pool, and the session lease still spans file writes.
Do not close the complete bead from this partial session.

Verification (private DISPLAY=:99 only):

```
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_reload_test
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64
```

Final release all and the complete sanitizer check exited zero; the check
passed 59 binaries (M)[AC] and its replay CLI shell suite. Final savectl fuzz
passed 24306 executions in 61 seconds (M)[AC]. Red/green and final logs are
recorded in the worker report. LeakSanitizer is disabled in the worker sandbox; coordinator reruns with
leaks enabled. Original bench scope and other review findings outside §§9–20
have not been silently changed.
