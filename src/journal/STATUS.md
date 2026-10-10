# Journal status — P1.9h / edit-4w1.54

Session 8 adds `journal_insert_step`: one protected INSERT prefix per caller
slice, with a 16 KiB (G) payload ceiling reduced for smaller batch/sync budgets.
No allocation/lock/sync/submission occurs in the step. A caller-owned progress
cursor distinguishes BUSY after a protected prefix from BUSY awaiting credit;
IO includes the RAM-retained record and must not be reappended. Existing
`journal_insert` keeps its whole-input contract. Complete durable-prefix byte
stats now include actual UI cache writes; step admission reserves PAD and
refuses to exceed configured sync_bytes beyond the received durable fence.
Partial-record fences and failed namespace barriers release no premature credit.
New unit and fuzz oracles check prefix content, blocked-bulk volume, resumption,
small sync budgets, sticky FULL/IO and zero typing allocation.

This partially addresses §§4 and 7; legacy append volume, independent worker
durability scheduling, pwrite wall time and editor/save integration remain open.
The new standalone savectl already runs journal preparation on a worker but
leases the journal and defers subsequent journal edits; editor wiring and
concurrent protected deltas still require integration for §8. Do not close the
bead. Current session results/remaining work and exact red/green output:
[worker report](../../docs/worker-reports/edit-4w1.54-s8.md). New design choices:
[edit-4w1.54.md](../../docs/decisions/edit-4w1.54.md). Session 8 journal fuzz ran
12824 inputs in 61 s (M)[AC], launch load1=14.83, requested 60 s (G), clean.
The release journal suite passed with the malloc guard active and append
allocations=0 (M)[AC], launch load1=13.53. GCC make all exits 0; final clang
ASan/UBSan make check passes 48 test binaries plus replay CLI checks (M)[AC],
launch load1=13.53, with LSan disabled and approved local :99 socket access.
Coordinator must repeat with LSan enabled. The paired
variant benchmark is TRACK only; bounded calls introduce durability-fence
completion costs that still need event-loop integration. Session 7 evidence
and the retained fixes follow; their older verification numbers are historical.

Review P1.9-2 §§1,6,9–14 are addressed. Failed fdatasync poisons the complete
live generation; journal_retry preserves IO until a complete CURRENT checkpoint
is synced into a fresh inode and durably published. Partial-write and directory
barrier retries remain supported. Retained BASEs have a full fsync metadata
barrier via a distinct syscall seam. Retention results cross routed work
mailboxes, with storage alive through receipt and no shared done polling.
PAD IO outranks submission BUSY; a failed PAD is retried only by the worker.
Retention uses the journal recovery directory or its nearest bounded-path
ancestor, including when BASE and journal paths approach the pathname limit.

New semantic oracles compare every session field in unit/power-image, SIGKILL,
fuzz and bench replay. Overlapping save tests preserve both target identities,
cutoffs, post-save deltas and retained availability in both finish orders,
including preparation/publication/retirement failures. Default bench GATE mode
returns nonzero on local append or paste component misses; explicit --track
reports misses while retaining correctness failures. A deterministic
--gate-self-check verifies that policy without another timing run.

UNRESOLVED at the full contract level: review §§4,7,8. Shared bulk work
can still delay durability; actual unsynced cache volume can exceed PRD §7's
64 KiB bound, including inside one large synchronous append. Synchronous
regular-file pwrite has no wall-clock UI bound, and save preparation still
blocks before the file save acknowledgement. The complete fixes require work
and editor/file integration. Concrete proposals and intentionally failing
opt-in --bulk-loss-repro, --append-bound-repro, --save-ack-repro tests are in
[P1.9h.md](../../docs/decisions/P1.9h.md). These probes are outside the passing
regression suite. The PRD maximum is not reinterpreted as a cadence target;
this bead does not close those three findings or claim the whole editor gates.

Verification: DISPLAY=:99 EDIT_DISPLAY=:99 make all; ASAN_OPTIONS=detect_leaks=0
make check; make fuzz; ASAN_OPTIONS=detect_leaks=0 build/fuzz/journal_fuzz
-max_total_time=120 -max_len=16384 -timeout=10 -artifact_prefix=/tmp/edit-4w1.54-fuzz-
/tmp/journal-fuzz-corpus. Run build/bench/journal_bench --track once at the end,
with fresh BAT0/status and load stamps. Coordinator repeats LSan outside this
sandbox. Historical session 7 results: final GCC make all exit=0 ([AC] load1=6.25); make fuzz
exit=0, 23 fuzzers built (M)[AC] load1=6.25; journal fuzzer completed 11262 runs
in 121 s (M)[AC], launch load1=6.84, requested 120 s (G), no findings.
Final full release journal suite passed with active malloc guard and zero append
allocations (M)[AC] load1=6.36. Final make all passed ([AC] load1=7.16); full
clang ASan/UBSan make check passed 45 test binaries and replay CLI checks
(M)[AC] load1=7.06, including 50 SIGKILL trials with exact session values.
The check used approved local Unix/Xvfb socket access after sandbox Xvfb
connection refusal, and disabled only LSan. The one final --track bench passed
correctness checks and reported one local 1 KiB append miss: p99=31847 ns
(M)[AC] load1=3.59 versus 20000 ns (G). Single-byte append p99=14271 ns and paste
p50/p99=3.510/3.836 ms (M)[AC] load1=3.46. No timing rerun or whole-editor gate
verdict. Complete red/green and bench evidence: P1.9h.md.

Historical status below is superseded by the P1.9h rules above, especially
sync-only retry, fdatasync retention, and the qualified-cadence statements.

# Journal status — P1.9g / edit-4w1.53

Successful append now encodes/checksums and synchronously pwrite()s each bounded
record to the kernel page cache on the UI owner. Process crash protects every
successful append before pump. fdatasync remains on the worker at the existing
configurable cadence (defaults 1 s / 64 KiB). Pump caches PAD before permitting
the next batch, and completion preserves newer UI written/file progress.

EAGAIN/ENOSPC/EIO/EINTR/zero/short writes make one attempt, return sticky IO
with append_errno, and retain all encoded bytes/partial progress in the fixed
queue. The worker drains missing tails; IO remains sticky until off-path retry
or complete checkpoint. Later edits cannot cross an unwritten prefix gap.
Logical INSERT keeps every preflighted chunk on IO; callers must not reappend
that queued mutation. No typing-path allocation, sync, wait or job submission.
pending_bytes exposes queue occupancy for benchmark delivery pacing.

The kill oracle requires replay >= the successful page-cache prefix, includes
a deterministic kill-before-pump case and applies to a piece tree against an
independent byte oracle. Fault tests cover UI/worker thread ownership, blocked
workers, PAD, sticky failure and precise short-write retry. Fuzzing checks
successful prefixes before pump and injected UI/worker failures.

Open constraint: regular-file pwrite may block inside Linux; bounded size and
one attempt cannot enforce the brief's absolute no-stall condition. O_NONBLOCK
does not fix that. Failed/refused edits suspend protection; worker scheduling
and I/O still qualify the power-loss cadence. Full explanation and current
red/green/verification evidence: [P1.9.md](../../docs/decisions/P1.9.md), P1.9g.
PRD §7 uses Tobias's exact process/power/kernel crash wording.

journal_save_prepare/finish are unchanged. P1.9d's recoverable save and P1.9f's
private reflink/copy bases remain tested and intact.

Current measured evidence: make all passed ([AC], load1=23.93); make fuzz built
21 fuzzers (M)[AC], load1=25.59; journal fuzz completed 5872 runs / 121 s (M)[AC],
launch load1=26.14, no findings. The single benchmark invocation passed its
content/fixture checks but reported append p99 36.103 / 114.236 us for 1 B /
1 KiB (M)[AC], row load1=29.72 / 29.26, above 20 us (G). Paste enqueue p99 was
36.239 ms (M)[AC], load1=29.72. These are TRACK only; no reruns or quiet-box
verdict. The decision records complete rows and the Linux syscall limitation.
Full release SIGKILL recovery passed 1000 trials in 580.64 s (M)[AC], launch
load1=27.00, protecting every published page-cache prefix. The final full release
journal suite passed with the active malloc guard reporting 0 append allocations
(M)[AC], launch load1=11.26.
Full clang ASan/UBSan make check passed 41 test binaries and replay CLI checks
(M)[AC], launch load1=24.95, using approved local Unix/Xvfb socket access after
the sandboxed CLI connection failed. LSan alone was disabled for this runner;
coordinator rechecks with leaks enabled. No other implementation problem was
found or fixed in this bead.

Historical P1.9f/P1.9e status follows; RAM-only enqueue and worker CRC statements
below are superseded by P1.9g.

P1.7d file-contract fix-up (edit-4w1.42): the composed save fixture now awaits
file open by draining the worker mailbox and decoding every live file message,
including internal save continuations. `file_open_begin` acknowledges enqueue;
small/empty files need receipt before attachment too. `journal_flush` protects
journal durability and does not wait for independent file saves: callers keep
draining until SAVE_DONE is decoded and the file is no longer busy. No journal
implementation/header or write/durability policy changed in this fix-up.
`journal_test --file-mailbox` covers copy/mmap/empty readiness, mixed delivery,
open failure, unread-result close and independent save waits; `--save` retains
the recovery/fault cases and cleans up after a failed assertion. Contract,
ownership audit and red/green evidence: ../../docs/decisions/P1.7d.md.
Final fix-up verification: make all and make fuzz pass,
(M)[AC, Not charging; launch load1=20.50]; full ASan/UBSan/LSan make check passes
outside sandbox on :99, 39 test binaries plus replay CLI,
(M)[AC, Not charging; launch load1=19.98]. The temporary paused-save failure
cleanup probe also passes with LSan enabled,
(M)[AC, Not charging; load1=13.87]. Supplied docs/source lack the advertised
P1.9g section/synchronous per-edit append; transport changes are outside this
fix-up and coordinator reconciliation is required.

File review §6 is fixed: save preparation retains a private inode via reflink
or a bounded WORK_BULK copy, rather than a hard link to the writable original.
The source is validated around retention, the private identity is captured,
and snapshot data plus its parent directory are synced before PREPARED is
published. Every matching BASE uses that private identity; the SAVE marker
still carries the intended target and cutoff. A write seam forces the copy
fallback for deterministic transfer-failure tests. Prepublication retention
failures remove incomplete snapshots; ambiguous checkpoint failures preserve
complete snapshots for recovery under the existing transaction protocol.

New regressions restore acknowledged post-save edits after writes through an
old descriptor and a hard-link alias. Forced-copy tests verify exact bytes
across chunks, worker execution, source mutation rejection (outside the prefix
with restored mtime), short writes, and data/directory failures before publish.
Red/green evidence and stamped verification are in
[P1.9f.md](../../docs/decisions/P1.9f.md). Verified 2026-10-09: GCC make all
passed ([AC], launch load1=4.71); ASan/UBSan make check passed 33 test binaries
and replay CLI checks (M)[AC], launch load1=3.00. The successful check used
approved local socket access after sandbox IPC creation returned EPERM; both
display variables stayed :99. Release journal tests passed with an active
malloc guard and 0 append allocations (M)[AC], launch load1=5.42. make fuzz
built 19 fuzzers (M)[AC], launch load1=3.00; journal fuzz completed 18779 runs
in 121 s without findings (M)[AC], launch load1=4.83, requested limit 120 s (G).
LeakSanitizer was disabled only for the sandbox runner.

The final journal benchmark ran once, TRACK only: paste enqueue p50/p99
0.218/0.400 ms (M)[AC], load1=11.62; single-byte/1 KiB append p99
668/904 ns (M)[AC], load1=11.62/11.49. Exact-content, disk-exhaustion and idle
sync self-checks passed. No quiet-box or whole-editor gate verdict is claimed.
No unresolved implementation finding remains in this scope. Fallback retention
needs disk space for one previous generation; startup cleanup remains owned by
the application. P1.9d/P1.9e semantics and fsync defaults remain intact.

P1.9d's recoverable save transaction, retained generations, retryable failed
batches and deterministic crash oracle remain implemented and covered.

P1.9e implements review MAJOR 4–7/9 and MINOR 10–13. Flush drains accepted
records and returns sticky FULL; complete checkpoints (including save prepare/
finish) resolve that suspension. Default touched queue data is 2 MiB + 8 KiB
(E), shared by the session. Logical INSERT calls preflight all chunks before
accepting any portion and admit idle default 1 MB/1 MiB pastes with no typing
allocation, syscall, wait or submission. The worker completes wire CRCs before
writing, with cancellation polling and exact checksummed retry bytes.

Complete checkpoints are preflighted and get their exact wire size plus the
original configured log budget as their file bound. This admits large untitled
snapshots; append never grows the bound. Stats expose checkpoint, file-limit
and queue bytes. G10/G10f integration must account for this fixed session pool
in addition to per-file tree/undo/content memory; full-editor gates are not
established by this module benchmark.

Captured BASE paths are canonical absolute paths in caller-owned struct
storage (copies borrow the captured object's path); decoded paths use caller
storage. Manual BASE/SAVE records require canonical absolute paths. Journal
setup owns its canonical path and parent-directory fd; rotation uses a short
independent temporary leaf and fd-relative operations, including directory
barrier retry. Tests cover cwd/parent rename and maximal leaf/full-path lengths.
Unreadable/short/missing/changed bases are conflicts, with a BASE-only read fault
seam. TABS/WINDOW IDs must be zero. Unit/fuzz corruption and truncation oracles
require the exact independently scripted prefix, sequence and contents.

The starting P1.9d branch already removed the worker's post-publication hold;
this work preserves that return and verifies it against a temporary restored
wait variant and stamped CPU-per-sync TRACK rows. No src/work changes needed.

The default-option benchmark covers verified 1 MB pastes, 100000 mixed edits,
BASE loading/two buffers/session restoration, normal timer pumping, explicit
stand-in bulk contention, reported pre-delivery pacing, default disk exhaustion,
and tiny/zero-data idle sync. Component enqueue measurements exclude the
reported delivery waits; whole-editor G1/G9 and actual index/find workloads
remain integration checks for the coordinator. Replay labels wire and payload
throughput separately. Shared-box performance measurements are TRACK only.

P1.9e verification (historical): verified 2026-10-09. Paired red/green transcripts and full stamped TRACK rows
are in [P1.9.md](../../docs/decisions/P1.9.md), P1.9e. GCC make all passed
([AC], launch load 9.59); ASan/UBSan make check passed 24 test binaries and replay
CLI checks ([AC], launch load 5.30), with X11 tests executed using approved local
socket access on safe Xvfb surfaces. make fuzz built 12 fuzzers ([AC], load 9.59).
Final journal fuzz completed 51079 runs / 301 s (M)[AC], launch load 5.30, without
findings. Final SIGKILL recovery passed 1000 trials / 211.77 s (M)[AC], launch
load 5.30. Release malloc guard was active: 0 append allocations (M)[AC], load
12.54, with all guarded appends and paste chunks successful.

The final default-option benchmark passed. Paste enqueue p50/p99 was
0.168 / 0.229 ms (M)[AC], load 5.84; worker CPU per sync was 144.213 us, with
0.000 us during the undrained-completion interval (M)[AC], load 5.84. The restored
wait variant measured 641.824 / 474.407 us respectively (M)[AC], load 11.70;
loads differ, so this is TRACK rather than a controlled performance ratio.
Both 100000-edit sessions restored exact contents and session state. The
1 KiB fixture reported 288 pre-delivery pauses, max 26.843 ms (M)[AC], load
7.11; enqueue measurements exclude those reported waits. Default disk exhaustion
returned FULL after protecting its accepted prefix. Tiny/zero-data idle sync
rows passed. Full-editor frame/memory and quiet-box battery verdicts remain
coordinator integration work. LSan is disabled only for this sandbox runner.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99: make all; ASAN_OPTIONS=detect_leaks=0
make check (needs local socket access); release build/tests/journal_test;
make fuzz; ASAN_OPTIONS=detect_leaks=0 build/fuzz/journal_fuzz -max_total_time=120
-max_len=16384 -timeout=10 -artifact_prefix=/tmp/ /tmp/journal-fuzz-corpus;
build/tests/journal_kill_test --trials=1000; build/bench/journal_bench. Take fresh
BAT0/status and loadavg stamps before each measured run; do not regenerate the
existing /tmp/edit-corpus.

The application still owns complete UI checkpoints/restoration, save-completion
routing, recovery conflicts and retained-generation cleanup. Configured cadence
and its defaults remain unchanged; the loss-window decision is edit-4w1.34.

Runtime rename (edit-457.18):
journal_default_dir resolves XDG_DATA_HOME/sublimite or
HOME/.local/share/sublimite without creating directories; CLI startup uses it.
Checkpoint/backup temporary names use sublimite. Verify runtime_test, cli_test
and journal_test with DISPLAY=:99 EDIT_DISPLAY=:99; make check uses ASan/UBSan.
Decision/evidence: ../../docs/decisions/rename-sublimite.md.
Final rename verification: gcc make all passed; clang ASan/UBSan make check
passed with leaks disabled; make fuzz built all fuzzers. Focused identity and
isolated desktop-install contracts passed. Complete red/green and stamped
fuzz results are in the decision document; no hot-path bench was rerun.
