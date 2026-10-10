# lineidx status — edit-4w1.47 / P1.6c, session s8

Review findings §6–11 are implemented or covered by the landed work prerequisite.
The red/green record and final acceptance are in
`docs/worker-reports/edit-4w1.47-s8.md`; design choices are in
`docs/decisions/edit-4w1.47.md` and the earlier `P1.6c.md`.

- Strict seek byte budgets and resumable partial counts. Synchronous seek yields
  after at most 64 KiB, 256 spans or 0.5 ms thread CPU between callbacks (G).
  Indexed fragmented seek is resumable too; every approximate answer is a real
  line anchor. Target changes and independently refreshed prefixes are covered.
- Worker seek over an immutable leased source, exact offset through validated
  mailboxes, stop at target, and cancellation/edit suppression.
- Relative-length chunk rope with incremental block summaries, bounded edits,
  subtree deletion retirement, edited lookup and bounded publication adoption.
  Poll stages four examined messages and traverses at most 64 entries (G).
  Refresh resumes one edited chunk with the synchronous callback/CPU bounds.
- Cancellation invalidates and unbinds before any adoption or source cleanup.
  Worker checks surround each span and scanner block; deterministic CPU-cost
  and one-byte fragmented-source cancellation tests pass.
- Queued destroy returns while an unrelated bulk job remains held, using main's
  P1.8c/d queued removal/completion API. An actual historical red reproduction
  from the original module hangs under the same held-job schedule.
- Allocator test reserves both byte oracle and index before the typing guard:
  10,000 keys and zero allocations (M)[AC], release guard active.
- Minimap/scroll tests now resume the sliced API during setup and no longer
  depend on one-byte budget overrun. Their production modules were not edited.

Verified: `make all` exits zero (gcc 13); focused §6–11 sanitizer cases, full
lineidx release/sanitizer suites, minimap/scroll release/sanitizer cases and
bench self-check pass (M)[AC]. Both empty-corpus and seeded fuzz runs requested
60 seconds (G), completed 61 seconds (M)[AC] and exited zero. Final `make check` exits zero: 48 test binaries and replay CLI checks pass (M)[AC]. `ASAN_OPTIONS=detect_leaks=0` is required in this sandbox; coordinator
reruns with leaks enabled. All execution uses DISPLAY=:99 EDIT_DISPLAY=:99.

One loaded-box TRACK benchmark run exits zero (M)[AC]. Logical cancel p50/p99
1,156/1,546 ns; cancelled worker CPU p50/p99 2,010,439/2,011,749 ns; partial
worker seek plus null viewport submit p50/p99 332,099,104/363,682,901 ns,
final load1 26.75 (all M)[AC]. These are not gate verdicts or variant comparisons.

Limits/outside scope: allocating request setup copies geometry; indivisible
source callbacks and release hooks must honor their documented bounds; active
blocking I/O must return before synchronous destruction. Legacy ordinary
line-to-byte/byte-to-line queries retain single-chunk source scans; use seek for
resumable UI queries. `bench/scroll_bench.c` still assumes bulk synchronous seek
and needs a separate migration. Real renderer/display and cold performance
verdicts remain unvalidated. No new globals or other-module production fixes.

# edit-4w1.56 — work-service integration

Background and resident-source foreground builds now use bounded continuations.
Foreground start/prioritize retain the same immutable source/results lease and
yield on mailbox backpressure. The copied-file editor jump and G7j bench use the
new work service. Fragmented-span work-bound/source-release tests and the lineidx
fuzzer pass. Mapping/cold I/O sources stay on bulk; dirty-index maintenance is
unchanged. Full evidence and limits: `docs/worker-reports/edit-4w1.56-s8.md`.

Rebase integration and current verification: `docs/worker-reports/edit-4w1.56-s8b.md`.


# edit-4w1.57 — P1-1 §5 / §12 ownership and benchmark adoption

Combined mmap ownership is covered by a production file/piece lifecycle test
using the supplied sparse fixture, independent index/find/save snapshots,
conservative find state, an allocator census and transient piece-builder
sampling. Peak is 6,898,888 bytes (M)[AC] against 7,242,880 bytes (G). Worker
scratch uses compact lengths/results; mmap attachment retires the acquisition
prefix at a documented handoff. Active/retiring storage remains accounted.
The file.c change is confined to attachment; no fault/readiness edits.

The benchmark waiter continues bounded adoption while mailbox traffic remains.
A completed-worker/full-mailbox self-check fails before the fix and passes after
it; prior final-publication and missing-completion/deadline checks still pass.
No new globals or typing allocations: the release guard records zero allocations
for 10,000 keys (M)[AC]. Release and sanitizer lineidx suites pass, make all
passes, and full make check passes 59 binaries plus replay CLI (M)[AC]. Fuzz
requested 60 seconds (G), completed 17,146 executions in 61 seconds (M)[AC],
exit zero. Leak detection was disabled as required; coordinator reruns with
leaks enabled. All display execution used :99. Back-to-back old/compact TRACK
rows establish no latency verdict on the loaded box.

Complete red/green evidence, decisions and validation limitations:
`docs/worker-reports/edit-4w1.57-s9.md` and
`docs/decisions/edit-4w1.57.md`. Other P1-1 findings remain outside this bead.

## edit-zzj.16 session 9 — editor admission and prepaid restart

lineidx_edit_check shares edit's geometry/capacity planner without cancellation
or live-model mutation; edits repack adjacent short chunks. Optional open-path
build reservation retains one compact job/scratch slot, reused by ordinary
initial builds and post-edit restarts. Prepaid restarts copy at most 64 entries
per poll with the existing 0.5 ms CPU bound (G), then submit through work.
Cancellation defers source release and slot reuse until physical retirement;
UI readiness covers preparation, staged adoption and completed retirement.
Memory accounting counts reserved storage once. No new globals.

Supplemental lineidx_test covers bounded preparation, cancellation, retirement
ownership/refusal, repeated reuse, exact counts and the active release allocation
guard. Evidence and limits: ../../docs/worker-reports/edit-zzj.16-s9.md.
Design: ../../docs/decisions/edit-zzj.16.md.

Final verification (M)[AC]: gcc 13 strict make all exits zero; clang 18
ASan/UBSan make check passes 60 test binaries and replay CLI with leaks disabled.
Editor and lineidx fuzz each complete 61 seconds clean against 60 seconds (G).
The release editor suite and focused prepaid repair guard record zero allocations.
Paired TRACK benchmark timing/structural limits and the coordinator leak-on rerun
are fully recorded in docs/worker-reports/edit-zzj.16-s9.md.
