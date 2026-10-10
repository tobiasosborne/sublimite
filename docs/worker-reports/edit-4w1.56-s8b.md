# edit-4w1.56 — session s8b rebase conflict resolution

Resolved the stopped rebase of `980fb75` onto main in the supplied worktree.
Only read-only Git inspection was used; no add, commit, checkout, rebase,
reset, stash or bd command was run. The coordinator must stage the resolved
files and continue the rebase. All test execution uses `DISPLAY=:99` and
`EDIT_DISPLAY=:99`; sanitizer execution uses `ASAN_OPTIONS=detect_leaks=0`.
Power at verification: `Not charging` [AC]. Results below are (M)[AC].

## Conflict decisions

The seven `src/lineidx/lineidx.c` conflicts were combined as follows:

1. Job fields: retain main's `available`, rope adoption cursor, seek target,
   `cpu_begin` and `cancel_cpu_ns`; add the worker cursor/sealed prefix,
   partial span counts, cumulative newline count and pending seek answer.
   UI availability and worker continuation state remain separate.
2. Worker/publication helpers: retain `worker_stop` and its cancellation CPU
   accounting, `publish_range`, and main's validated staging receiver. Keep
   the branch's range-size assertion. A failed mailbox publication now yields
   through `work_continue` rather than sleeping on the execution resource.
3. Build function: combine main's stop-at-target worker seek and cancellation
   checks with persistent, bounded continuation scanning. Check cancellation
   before/after callbacks and after each 16 KiB scanner block. Retain partial
   chunk counts across invocations; publish only complete earlier chunks when
   a seek finds its target. Both ranges and the final seek answer retry after
   backpressure. Invocations yield at 1 MiB, 64 spans, 16 entries or a thread
   CPU deadline of 4 ms (G); callbacks retain the existing indivisible-source
   contract. Reset CPU checkpoints on entry because workers may change.
4. Start validation: retain main's logical cancellation-before-reap sequence,
   retiring-lease refusal and CPU diagnostic reset. Add foreground-enabled
   pool validation to the common start helper; do not poll/adopt in cancel.
5. Geometry setup: retain main's relative-length rope traversal and apply
   cursor setup, rather than reviving absolute starts in the UI entry table.
   Select `WORK_FOREGROUND` or `WORK_BULK` at submission without replacing
   source ownership, generation validation or refusal cleanup.
6. Owned build entry point: route the existing public API through the common
   helper as an ordinary bulk build, retaining its source-byte accounting.
7. Seek/foreground entry points: keep main's bounded seek-result polling and
   exact-result suppression. Add foreground start and same-lease prioritize
   wrappers; remove the obsolete absolute-entry `find_chunk` alternative.

Main's synchronous seek budget, sliced indexed queries, refresh, bounded
mailbox adoption, chunk rope, edit admission, logical cancellation and deferred
release implementation remain intact.

`src/editor/editor.c`: use the active buffer's `e->buffer->index`, file and
index-dirty fields. Preserve the publication prerequisite and exact piece-query
positioning from P4.I. Poll the index and prioritize a clean resident-source
build before returning `EDITOR_MORE`; mapping sources remain on bulk.

`src/editor/open.c`: use `work_pool_init_foreground` and preserve the explicit
`rc = EDITOR_ERR_MEMORY; goto fail;` failure path.

`src/lineidx/STATUS.md`: preserve the entire 4w1.47 section first and the
4w1.56 work-service integration section second.

## Build/test integration repairs

The foreground integration test used pre-P4.I editor fields; migrate it to
`e->buffer`. Main also replaced discovery's dlopen path with an isolated
fc-match child. The old dlopen interception produced two failed start barriers.
Replace it with the existing `font_fallback.discovery_program` seam and a
pipe-held test child. This still runs the real production discovery worker
while the foreground jump completes before the child is released.

Add fragmented worker-seek checks for zero, a target within a partial chunk,
and EOF, followed by cancellation suppression and physical source release.
The existing foreground yield test still observes at most 64 fragmented spans
before another request runs (G).

The first GCC build found `bench/prewake_bench.c` assigning the removed
`gl_state.bound` field. Remove only that obsolete assignment; retain EGL
unbind and release-thread error handling. This is a mechanical main-side
benchmark migration necessary for the requested `make all`.

## Verification

- `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: exit 0, GCC with `-Werror`.
- Full `build/san/tests/lineidx_test`: exit 0, `lineidx_test: ok`; includes
  main's seek budget, worker seek, bounded metadata, cancellation suppression,
  cancellation CPU accounting and queued destruction cases.
- `build/san/tests/work_foreground_test`: exit 0, all eight blocked-client
  schedules, bounded fragmented indexing, fragmented worker seek and
  multi-buffer editor resident-jump checks pass.
- Release `build/tests/lineidx_test --review=alloc`: exit 0; 10,000 keys
  with zero allocations and the allocator guard active (M)[AC].
- Release `build/tests/work_foreground_test`: exit 0, same acceptance cases.
- `build/fuzz/lineidx_fuzz -max_total_time=30 -timeout=15 -max_len=2048`:
  exit 0; 11,599 runs in 31 seconds (M)[AC].
- `build/fuzz/work_fuzz -max_total_time=30 -timeout=15 -max_len=2048`:
  exit 0; 62,566 runs in 31 seconds (M)[AC].
- `git diff --check`: exit 0; requested marker search over src/tests/bench
  finds no matches.

`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`:
exit 0; `check: 58 test binaries passed` and `test_replay_cli: all passed`.
The full run includes the updated foreground fixture and added fragmented
worker-seek cases, plus live raster conformance and the multi-buffer editor
suite. The deliberate find-fixture negative controls print FAIL diagnostics
but their parent self-checks pass; they are expected test output.
The first sandbox-only attempt could not connect to Xvfb; the full retry has
access to the existing :99 socket. Leak checking remains for the coordinator.

Final hygiene checks after documentation updates: `git diff --check` exits 0;
`grep -rn '^<<<<<<<\|^>>>>>>>' src tests bench` exits 1 with no output (no
markers). Git's index intentionally still lists the four paths as unmerged
because only the coordinator may run `git add` and `git rebase --continue`.
No requested implementation or verification work remains pending.
