# Undo status — P1.5e / edit-4w1.40

Findings 2–8 in `docs/reviews/P1.5-1.md` are implemented:

- One-slot insert admission / seven-slot capture scratch, with initialization-time
  virtual capacity for expansion of every admitted unknown record.
- Packed 64 B span storage; bounded relocation, tail-page decommit, explicit
  committed/retired/virtual accounting and combined piece+undo G10f checks.
- Constant-time redo detachment; bounded whole-group cap detachment and <=16
  reclaimed slots per key. `undo_maintain` amortises the remainder. Very large
  cap reductions may discard extra oldest closed groups to bound traversal.
- Sliced replay with piece-operation budgets, absolute deadlines, UNDO_MORE,
  separate operation/span counts and a borrowed pre-group snapshot. Legacy
  entry points still compile. Boundary cursor state is returned on completion.
- In-tree synthesised-kernel 10k undo gate: (G) p50 <=58 ms / p99 <=78 ms;
  real misses propagate to exit. Mock gate unchanged; label-only option removed.
- Transient/persistent undo+redo failure tests, batch state/dirty checks, physical
  admission/capture regressions, and small-capacity/failure/sliced replay fuzzing.

**Finding 1 is implemented.** Undo and redo begin one piece checkpoint per
group, commit at its boundary, and abort on failure. Undo-owned capture backups
restore the original record bytes, links, boundary indexes, cursor/counts and
packed-storage suffix; refs first produced in the failed transaction are
discarded. Previously completed batch groups remain completed. Errors report
only those completed groups and release the partial replay lock/view. Retry
starts the entire uncompleted group again. `review_1` runs by default; its
allocator sweep includes every observed allocation index at every operation/
slice index, both directions, transient/persistent failure and repeated retry.

One checkpoint spans all slices of its group. UNDO_MORE and the borrowed
pre-group rendering view remain necessary for normal yields. Maintenance pauses
while checkpointed so saved handles cannot relocate. clear/destroy abort an
unfinished group. No other code may mutate the tree between slices; undo asserts
retained current-snapshot identity on resume/clear/destroy. This assertion is
coupled to the linked kernel's snapshot cache, not a portable piece.h promise.
Queries and immutable worker snapshots are permitted. See P1.5e.md for the
identity guard and provisional change-counter contract. No editor caller change
was required; src/editor is untouched.

Capture backups use an init-reserved lazy pool. `undo_stats.replay_records`
counts these unpublished span copies, and all live/committed/virtual accounting
includes both pools. Completion/abort resets and decommits scratch. The typing
path still allocates nothing after init. The API checks deadlines between piece
operations; it cannot interrupt an individual mutation/snapshot/checkpoint
terminal call or the synchronous metadata restoration on failure.

Verify from this worktree (use only the supplied Xvfb :99):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/undo_test 1
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/editor_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  ./build/fuzz/undo_fuzz -max_total_time=300 -max_len=2048
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/undo_bench
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/undo_bench --review-gate-check
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/undo_bench --review-label-check
```

LeakSanitizer is disabled in the sandbox; coordinator rechecks with leaks on.
The sandbox cannot connect to Xvfb's socket: full sanitizer and live editor
checks require execution with that socket accessible, always with both display
variables set to :99. The release editor retry passes, including native X11,
undo/redo viewport repair and both typing allocation checks. See
`docs/decisions/P1.5e.md` for the red transcript and final build/sanitizer/fuzz/
single shared-box TRACK benchmark evidence. Coordinator owns quiet-box verdicts,
integrated rendering and leaks-on QA.

Final evidence, all **(M)[AC], Not charging, TRACK**:

- `make all` and default release undo suite pass, load1=10.27. The atomic
  sweep reports 212 failures, including 84 after an earlier slice; all restore
  exact bytes/queries/record storage/cursors. Both batch directions preserve
  completed groups. The expected child ownership assertion passes.
- `make check` passes all 42 sanitizer binaries and replay CLI checks with
  access to Xvfb :99, load1=7.27. Release `editor_test` passes, load1=6.31;
  both null and raster typing guards report zero allocations.
- `make fuzz` builds 22 targets, load1=10.27. Undo fuzz passes 303127
  executions / 301 s, load1=7.55; the rebuilt final-accounting follow-up passes
  101604 executions / 61 s, load1=12.75. No crash artifacts.
- The single benchmark, 2026-10-09T16:10:23+08:00, load1=6.39, exits zero:
  real tree p50/p99 31.433541 / 43.063527 ms against **(G)** 58 / 78 ms;
  mock 0.378502 / 0.400175 ms against **(G)** 6.3 / 6.3 ms. Combined ownership
  checks pass; cap reclamation drains committed undo bytes to zero.

No undo implementation blocker remains. The snapshot guard's kernel coupling
and non-preemptible checkpoint/rollback work remain explicit API limits.
Inherited piece checkpoint memory-funding limitations in P1.4e.md are outside
this bead. Coordinator still owns leaks-on and integrated frame verdicts.
