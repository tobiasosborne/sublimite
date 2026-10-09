# Journal status — P1.9f / edit-4w1.45

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
