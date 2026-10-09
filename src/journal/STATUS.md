# Journal status — P1.9d / edit-4w1.32

Verified 2026-10-09, Session 6, after rebase onto the synthesised piece kernel.
This continuation changes documentation only. Implementation, tests, fuzzer,
benchmark, and protected files match their starting source hashes.

The recoverable save transaction is implemented: prepare retains and syncs the
previous named base generation and publishes a complete PREPARED checkpoint
before file replacement. Finish publishes the saved identity/cutoff and a
complete current checkpoint before retiring that retained generation. Recovery
selects its content source through BASE. Shared-base buffers retain their source
as well; callers preserve other in-flight saves in complete checkpoints.

Failed worker writes retain the sealed batch, exact progress, offset, and force
request. After completion receipt, `journal_retry` retries it before the second
accepted batch. A complete checkpoint can also resolve worker I/O failure.
Installed replacements with a failed directory barrier suspend I/O and expose
no new-generation durable acknowledgement until that barrier is retried.

Deterministic fault tests cover short write/EIO, sync-only retry, a record
straddling the sync boundary, creation and checkpoint directory barriers,
pre/post-rename failures, and real file saves with post-snapshot edits to two
buffers. Separate synced inode images and directory-name selection model power
loss. SIGKILL recovery applies records through `journal_apply_piece` and compares
the resulting piece bytes to an independent seeded byte model; its predicate is
acknowledged <= recovered <= issued. Fault fuzzing also exercises write/sync
retry against byte-model and piece-tree recovery.

Session 6 reproduced a failing assertion for each of review BLOCKER 2, MAJOR 3,
and MAJOR 8 by temporarily reverting its relevant behavior, then restored it
and observed green. Exact transcripts and fresh verification measurements are
in [P1.9.md](../../docs/decisions/P1.9.md).

Fresh results: GCC `make all` exited 0; ASan/UBSan `make check` passed 23 test
binaries plus the replay CLI checks on :99; `make fuzz` built 12 fuzzers. Release
append allocations were 0 (M)[AC], load 5.73. The journal fuzzer completed
76830 runs / 301 s (M)[AC], launch load 4.12, with no findings. The full kill
test passed 1000 trials / 167.04 s (M)[AC], launch load 4.10. One benchmark run
exited 0: append p99 0.264 us / 5.193 us (M)[AC], load 3.56, TRACK only. Full
stamped benchmark lines and independent recovery counts are in the decision doc.

Verify from the worktree (Xvfb :99; local socket access is needed for the full
check; LeakSanitizer is disabled only for this sandbox runner):

```sh
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/journal_test
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
mkdir -p /tmp/journal-fuzz-corpus
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/journal_fuzz -max_total_time=300 -max_len=16384 -timeout=10 -artifact_prefix=/tmp/ /tmp/journal-fuzz-corpus
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/journal_kill_test --trials=1000
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/journal_bench
```

Take a fresh power/load stamp before each run. Bench measurements on this shared
box are TRACK; printed pass fields do not supply the coordinator's quiet-box
gate verdict. The release journal test checks the active malloc guard; the
ASan build intentionally uses the inert guard.

The editor startup/save UI still owns complete checkpoint construction, buffer
and session restoration, file-save completion routing, conflict handling, and
retained-generation cleanup after recovery. This module supplies the protocol.
No unresolved in-scope recovery/retry defect was found in this continuation.

The default sync cadence remains configurable and unchanged; the loss-window
decision is edit-4w1.34. Review MAJOR 4–7/9 and MINOR findings remain
edit-4w1.33, including FULL/flush semantics, large paste/untitled limits,
generic path handling, rotation temporary-name expansion, parser/schema issues,
and benchmark workload coverage. The coordinator still needs a quiet-box
performance verdict and a LeakSanitizer-enabled check outside this runner.
