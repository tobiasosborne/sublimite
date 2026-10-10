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
