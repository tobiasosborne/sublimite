# edit-4w1.55 — P1-1 §§2–3, session 9

Detected SIGBUS recovery now invalidates derived find/index results through a
retained backing token. Find polls it and the panel rejects publication/adoption,
cancels leases, and clears already adopted metadata on use. Line indexes bind
before scanning, retain the mapping after source retirement, cancel faulted work,
and refuse exact queries/completion/seek after recovery. The editor binds both
synchronous and worker indexes. A tiny piece snapshot lifetime-hook accessor is
the only piece change; mutation/storage algorithms are unchanged.

Readiness getters were already mailbox-only in this main. Deferred file decode
now also validates the file's recorded work slot/epoch/application generation,
so cancellation after receipt cannot install readiness. Queued and pending
prefix/map cancellation, getter-only polling, faults during scanning, before
adoption, after adoption and after file/tree/snapshot destruction have coverage.
No new mutable globals: the symbol self-check still reports only file_bus.

Red-first outputs, all final evidence and limits:
[worker report](../../docs/worker-reports/edit-4w1.55-s9.md).
Lifetime/adoption choices and loaded back-to-back TRACK comparisons:
[decision](../../docs/decisions/edit-4w1.55.md).

Final make all and make check exit 0 (M)[AC], GCC 13 release and Clang 18
ASan/UBSan with the normal strict warning flags. All 59 sanitizer binaries and
replay CLI passed (M)[AC]. DISPLAY and EDIT_DISPLAY were :99; final check used
an isolated worktree runtime directory with local socket access. Final file fuzz
requested 60 s (G), completed 8410 executions in 61 s (M)[AC], exit 0.
Release file suites and active save/lineidx/editor allocation guards pass;
10,000 typing keys allocate zero (M)[AC]. ASAN_OPTIONS=detect_leaks=0 is required
in the sandbox; coordinator reruns with leaks enabled. Undetected mapping races,
original rebasing and the older unrelated findings below remain outside scope.

# File module status

P1.7e / edit-4w1.43 session-eight continuation: §§17–18, 20–23 have targeted
red/main and green/WIP coverage. §19 private-original keep and intact mapped
deletion/replacement work; damaged mapped keep remains refused under P1.7c §5.
The explicit `FT_CASE=19mapped` requested-keep oracle remains red and is excluded
from ordinary passing suites. Do not close full §19 without the coordinated
stable-original/rebase design described in docs/decisions/edit-4w1.43.md.
Session eight fixes acquisition state when canonicalization-to-open races lose
the entry or replace it with a symlink, and covers fortified prefix reads in
the syscall-injection harness. A red-first restored-mtime rewrite regression
also makes keep conservatively refuse an unvalidated ctime/metadata generation
on the still-named mapped inode; intact deletion/replacement keep still works.
Final evidence is in
docs/worker-reports/edit-4w1.43-s8.md. No unrelated module implementation changed.
Final s8 verification: make all and make check exit 0 (M)[AC], 48 sanitizer
binaries plus replay CLI passed with detect_leaks=0; final-production file fuzz
completed 6077 runs in 61 seconds (M)[AC] under the requested 60-second budget
(G). Full wrapped release/sanitizer file suites and active release typing/save
allocation guards pass (M)[AC]. Only file_bus is mutable in the production file
object. Leak-enabled verification remains coordinator work; §19mapped remains
explicitly incomplete.

P1.7/P1.7b and P1.7c safety fixes remain. P1.7d (edit-4w1.42) implements
file-1 §§11–13, 24–25, 31–32 and copy/scan elimination in §16.
See docs/decisions/P1.7d.md for each verdict and exact red/green lines.

Latest integration (second P1.7d fix-up): the rebased P1.7f test/bench/fuzzer
callers now drive asynchronous prefix/open/failure and save continuations to
completion through mailbox decode. Allocation and queued-save fixtures await
OPEN_READY before attachment. Queued opens verify exact prefix bytes and
viewport cells with BULK pressure held through renderer submission; G5 ends
there without awaiting full BULK open. Phase-aware collectors preserve message
identity/generation/kind, reject duplicates and check prefix/open and
prepare/replace order. Save retirement waits are outside G8d timing. G8s still
ends at saving-row submission; G8d still ends at successful durable receipt.
The stateful P1.7f fuzzer retains its byte/error/lifetime oracles and now awaits
OPEN_FAILED for nonregular sources, including close/stale-drain cleanup.
No production source or header changed. The older missing-P1.7f/scanner-only
observations below describe the previous snapshot; P1.7f is present now.
This follow-up's current verification and TRACK rows are recorded at the end
of docs/decisions/P1.7d.md. Production proposals and P1.7f's power-loss testing
limitations remain open; gate verdicts belong to the coordinator.

Second fix-up verification: make all and make fuzz exit=0 (23 fuzzers built),
(M)[AC, Not charging; launch load1=7.50]. Release file_kill_test exit=0 with
the active fresh-save malloc guard, (M)[AC, Not charging; load1=11.21]. Full
ASan/UBSan/LSan make check exit=0 outside the sandbox, 45 test binaries plus
replay CLI, (M)[AC, Not charging; load1=14.74]. Stateful file fuzz completed
9,289 executions in 121 seconds, exit=0, (M)[AC, Not charging; load1=11.21],
using detect_leaks=0 inside the sandbox. Both display variables were :99.
The single TRACK bench completed all warm rows and fixture cleanup, exit=0,
(M)[AC, Not charging; launch load1=12.79]. Latency exceeds several gates on
the shared box; cold acceptance and coordinator gate verdicts remain unclaimed.
Exact row stamps and preserved endpoints are in P1.7d's second-fix-up section.

Open captures/enqueues only on UI. A worker canonicalizes, opens NONBLOCK,
rejects non-regular sources, reads and scans cancellable prefix chunks, then
publishes PREFIX_READY before BULK copy/mapping. Small/empty opens also send
OPEN_READY. Content/error/identity become UI-owned only through mailbox decode;
readiness/status getters never install unreceived worker output.

Save ack uses cached state + snapshot/enqueue. Preparation writes/fsyncs off UI;
SAVE_PREPARED transfers ownership for UI authorization and commit enqueue.
Workers get immutable inputs, including backing identity/fault epoch. Detected
invalidation cancels a queued/running commit through work and queues independent
worker cleanup/completion. Keep cannot revive that save. No shared file mutex
remains. REPLACED transfers identity before directory sync; SAVE_DONE installs
errno before notification. Phase/terminal publication retries while cancellable
under mailbox saturation. Zero writes are EIO; all retries check cancellation.

COPY attaches its private worker bytes through existing piece lifetime hooks,
with no whole-original allocation/copy/newline scan. Snapshot backing survives
file/tree destruction. Inotify poll reads one nonblocking buffer per call and
coalesces an asynchronous identity check; status installation never refreshes
watches. Explicit off-path file_watch_start refreshes after replacement.

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
Still missing (scope proposals, not fixed):
- §14: journal preparation/cutoff transaction needs immutable checkpoint and
  post-cutoff batches plus caller integration. No journal file was changed.
- §15: close still waits for physical jobs. Work needs finalizers for skipped
  cancellation and shutdown, plus deferred lifetime/terminal retirement.
- §16: mapped initializer still builds metadata proportional to original size
  on UI. Needs lazy piece initialization or reviewed builder/adoption contract.
- §11/G5: a BULK-only pool cannot publish behind stalled unrelated BULK I/O;
  an existing RASTER lane is used when present. Dedicated interactive work and
  correct first-viewport integration/benchmark are proposed. G5 is unclaimed.

No frozen header, other module implementation or bench was edited. P1.7c's
SIGBUS registry/chaining/lifetime fixes and its one-service global-exception
proposal remain; the symbol self-check still reports one mutable file_bus.
Other review findings remain outside this bead.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99 make all, ASAN_OPTIONS=detect_leaks=1
DISPLAY=:99 EDIT_DISPLAY=:99 make check (outside sandbox for LSan/local sockets;
detect_leaks=0 only if sandbox ptrace prevents LSan), make fuzz, and
build/fuzz/file_fuzz -max_total_time=120. New syscall checks require the wrapped
link command in P1.7d; FT_CASE=11,12,13,16,24,25,31,32 select individual cases.
FT_CASE=14 and FT_CASE=15 intentionally reproduce the unresolved proposals and
are excluded from the normal suite. Existing fixtures now await asynchronous
receipt. The fuzzer exercises the prefix scanner; worker lifecycle/IO is covered
by file tests, not by this existing scanner-only fuzzer.

Release and wrapped release/ASan/UBSan file suites passed in the initial run,
including P1.7c cases. Its full make check failed at journal_test's immediate
file_open_ready assertion; those historical results are retained in P1.7d.
The authorized fix-up updates tests/journal_test.c to await decoded open and
save messages, independently of journal_flush, and to clean up failed fixture
assertions. Added coverage includes copy/mmap/empty opens, open failure,
unread-result close, mixed journal/file delivery and independent save waits.
No file/journal implementation or header changed in the fix-up.

Final fix-up: make all exit=0 and make fuzz exit=0 (21 fuzzers built),
(M)[AC, Not charging; launch load1=20.50]. Full ASan/UBSan/LSan make check passed
outside the sandbox, 39 test binaries plus replay CLI, exit=0,
(M)[AC, Not charging; launch load1=19.98]. Both display variables were :99.
An LSan-enabled temporary paused-save assertion-failure probe exited as expected
without leaks, (M)[AC, Not charging; load1=13.87]. No benchmark was rerun for this
fixture-only fix. The advertised P1.9g/P1.7f rebase inputs are absent from the
supplied source/docs/bench; coordinator reconciliation remains outside scope.

Fix-up file fuzz: Done 11486605 runs in 121 second(s), exit 0,
(M)[AC, Not charging; launch load1=18.14], no findings. Scanner-only fuzzer;
mailbox/lifetime coverage is in the fixtures and the LSan-enabled full suite.
Initial-run file fuzz: Done 6915080 runs in 121 second(s), exit 0,
(M)[AC, Not charging; load1=28.32], requested 120 s. An earlier pre-refinement
run also passed in 121 s; the final run covers the final library build.
The unmodified module bench ran once as TRACK, exit 134,
(M)[AC, Not charging; load1=14.37]. Its open rows now sample enqueue before
asynchronous prefix receipt (invalid G5 endpoint), then immediate-attach assert
fails before save rows. No bench rerun/edit; edit-4w1.44 owns harness adaptation.
Exact rows, stamps and verification results are in P1.7d. G5/G8s whole-editor
verdicts remain unclaimed; proposals §14/15/16 must land before completion.
