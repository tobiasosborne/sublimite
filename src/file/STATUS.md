# File module status

P1.7/P1.7b implemented; P1.7c review fixes cover file-1 §§1–5, 7–10.
Published registry generations are pinned through recovery and retired before
unmap/reuse. Previous SIGBUS delivery semantics are preserved; page size and
lock-free atomic checks happen outside the handler. The one process signal
service needs the narrow binding-rule exception proposed in
`docs/decisions/P1.7c.md` (§10), pending coordinator adoption.

Workers publish immutable open/save results; UI queries/mailbox decode install
them. UI owns watches. Saves validate original backing identity, recovery epoch,
and invalidation generation immediately before rename. Temp/target operations
and directory fsync share a retained parent descriptor. Close cancels all
pending file-job completions, including older undrained saves.

Keep refuses changed/faulted mapped backing rather than accepting stale cached
newline counts. Full keep/rebase and immutable retained-generation support need
the piece/consumer design proposal in P1.7c §5; no frozen header was changed.
Other file-1 findings (§6, §§11–32) remain outside this bead. In particular close
still waits, I/O/slices and storage limits retain their earlier limitations,
terminal mailbox saturation and save errno are separate fixes.

Verify with `DISPLAY=:99 EDIT_DISPLAY=:99 make all`,
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check`, `make fuzz`,
and `build/fuzz/file_fuzz -max_total_time=120`. The fuzzer currently checks the
prefix scanner only (stateful file fuzzing is later §30). Run the wrapped tests
from P1.7c for watch ownership, handler sysconf exclusion, and directory-fsync
identity; `FT_CASE=1,2,3,4,5,7,8,9` selects individual regressions. Check the
signal-service symbol limit with `src/file/check_signal_globals.py` via Python.
TSan polling/watch tests pass in a standalone GCC `-fsanitize=thread -no-pie`
build (`FT_CASE=3`); the PIE executable aborted at startup with “unexpected
memory mapping”. See P1.7c for the build sources/flags.

Release and ASan/UBSan file tests and all wrapped regressions pass. `make all`
passes; `make fuzz` builds 19 fuzzers. File fuzz: 9,941,893 executions in 121 s,
(M)[AC, Not charging; load1=7.58], exit 0, requested 120 s minimum.
The full check suite passes outside the local-socket sandbox restriction:
`check: 33 test binaries passed`; `test_replay_cli: all passed`. LSan rerun
remains coordinator work because this sandbox uses `detect_leaks=0`.
The module bench ran once, exit 0, TRACK (M)[AC, Not charging; load1=5.46].
Its stamped rows are in P1.7c; loaded-box values are not gate verdicts.
P1.7f (edit-4w1.44) addresses review `docs/reviews/file-1.md` §26–§30 within benchmark/test/fuzz scope. Production `src/file` and frozen headers are unchanged; P1.7c owns production fixes.

Done: request-to-viewport CPU raster/XShm benchmark with byte/cell oracle, fixture validation and residency checks for warm/manual cold scenarios; fresh fragmented save acknowledgement with a submitted status row that preserves document cells and queue-pressure conditions; matched successful save completions, exact output bytes, distributions and gates; separate cached enqueue primitive; independent kill/cancellation tests with exact target/temp assertions; arena-backed release acknowledgement malloc guard; stateful scanner/file fuzzer with bounded byte/lifetime/generation/error oracles. See `docs/decisions/P1.7f.md` for red/green evidence and measurement limits.

Missing: process-kill tests establish live-kernel visibility, not power-loss durability. Syscall order/durable-image testing, injected I/O failure matrix, journal prepare/finish recovery composition and deterministic thread races need the proposed per-instance `file_io` seam. Complete journal-to-status acknowledgement and actual index/find/save concurrent-workload integration remain application-level proposals. Quiet AC gate/cold acceptance and leak-enabled sanitizer verification belong to the coordinator.

Verify with `DISPLAY=:99 EDIT_DISPLAY=:99 make all`, `ASAN_OPTIONS=detect_leaks=0 make check`, `make fuzz`; run release `build/tests/file_kill_test` for its active allocation guard. The new test also runs bench adversarial self-checks 26/27/28 and real renderer submission without measuring latency. Run `build/fuzz/file_fuzz -max_total_time=120 -max_len=4096` on the safe display. Stamp power/load before one `build/bench/file_bench --track`; default gate mode requires verified warm and cold suites, and `--cold` is the manual cold-only invocation. Required unavailable scenarios fail.

Verification: release `make all` and file test pass (active pooled allocation guard); ASan/UBSan `make check` passes 34 binaries plus replay CLI with local sockets enabled, `detect_leaks=0`; `make fuzz` builds 19 fuzzers; file fuzzer completed 14,497 executions in 121 seconds under a 120-second budget, (M)[AC], start load1=11.65, no sanitizer failure. The sandbox-only IPC EPERM was resolved by running verification with authorized local sockets on :99.

Single TRACK attempt: start Not charging [AC], load1=7.11; terminated with exit 143 after prolonged silent/buffered output, with no usable latency rows. A bounded repeated-trial probe was progressing when its too-short watchdog expired; this does not establish a production deadlock. Added line buffering and off-interval progress, cleaned the abandoned small fixture, and did not rerun the bench. Coordinator must measure final rows once on a quiet box. Queue correctness tests default to three repetitions per condition; `FILE_KILL_STRESS=1 FT_V=1 build/tests/file_kill_test` enables the larger, watchdog-bounded diagnostic without recording latency samples.
