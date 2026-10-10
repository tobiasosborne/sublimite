# P1.6c — lineidx foreground and cancellation slices (edit-4w1.47)

Scope: lineidx review §6–11 and work-scan review §9 (lineidx only).
Builds on P1.6b and P1.8d. No frozen header or other-module production changes.
All functional evidence uses DISPLAY=:99 EDIT_DISPLAY=:99. Timed results are
TRACK; the coordinator supplies gate verdicts. Final verification follows below.

## §6 — supplied seek byte budget

Accepted. Resume partial chunk counts without marking the chunk built; find the
requested newline while consuming the bounded span. Return a proven line anchor
on exhaustion without an extra alignment scan. Invalidate continuation on edits.
Red (M)[AC], Not charging, load1=5.62, `--review=6`:
`tests/lineidx_test.c:769: FAIL s.calls <= budgets[i]`;
`lineidx_test: 10 failure(s)`.
Green (M)[AC], load1=6.60: `review 6: strict byte budget + partial continuation ok`;
`lineidx_test: ok`. The final slice API also limits callbacks/CPU; see §7.

## §7 — foreground bulk jump

Accepted. A separately bounded synchronous continuation and an immutable-source
worker seek now replace bulk synchronous continuation. No piece/render/find
header change is necessary. Red (M)[AC], load1=6.20, `--review=7`:
`tests/lineidx_test.c:802: FAIL s.calls <= 256`;
`tests/lineidx_test.c:803: FAIL !q.exact`; `lineidx_test: 2 failure(s)`.
Implemented and verified in session s8; focused ASan/UBSan green evidence is recorded in `docs/worker-reports/edit-4w1.47-s8.md`.

## §8 — whole-table maintenance

Accepted. Replace explicit absolute starts and suffix shifts with a chunk rope
whose block summaries are updated locally; receive callbacks only stage sealed
ranges, and poll adopts a bounded number of entries. Dirty/edited queries use
summaries, and prefix derivation starts from the first unbuilt entry.
Red (M)[AC], load1=6.20, `--review=8`:
`tests/lineidx_test.c:819: FAIL lineidx_foreground_work(x) - before <= 4096`;
`tests/lineidx_test.c:832: FAIL applied <= 16`;
`tests/lineidx_test.c:833: FAIL !lineidx_complete(x)`;
`lineidx_test: 3 failure(s)`. Implemented and verified in session s8; focused ASan/UBSan green evidence is recorded in `docs/worker-reports/edit-4w1.47-s8.md`.

## §9 — logical cancellation and publication suppression

Accepted. Capture/request cancellation before any receive or cleanup, invalidate
the application generation, unbind/retire the lease, and defer physical cleanup.
No source release or result adoption occurs inside logical cancellation.

diff --git a/docs/decisions/edit-4w1.47.md b/docs/decisions/edit-4w1.47.md
new file mode 100644
index 0000000000000000000000000000000000000000..db9d0f62b9308f16e0782fac64fe768072398f16
--- /dev/null
+++ b/docs/decisions/edit-4w1.47.md
@@ -0,0 +1,75 @@
+# edit-4w1.47 — lineidx UI slices and cancellation
+
+Continues P1.6c WIP commits `0a1abb4` and `c208ce7`, rebased onto main.
+Scope is `docs/reviews/lineidx-1.md` findings §6–11. Historical red evidence
+from those sessions remains in [P1.6c.md](P1.6c.md); the final per-finding
+evidence is in [the s8 report](../worker-reports/edit-4w1.47-s8.md).
+
+## Decisions retained and verified
+
+- Seek persists partial chunk counts and real line anchors. An unindexed
+  synchronous call consumes at most its byte budget, additionally capped at
+  64 KiB, 256 callbacks and 0.5 ms thread CPU between callbacks (G). A target
+  already indexed uses a separate, equally sliced chunk query. Changing the
+  target, editing, or independently advancing the prefix cannot reuse invalid
+  counts. Exact offsets can be returned before a complete chunk is adopted.
+- Bulk jump scanning runs on `WORK_BULK` over an immutable source. The worker
+  stops at the requested newline and publishes its offset through a validated
+  mailbox message. The request retains one source lease; cancellation, edit
+  and replacement suppress its result. Request allocation and geometry-copy
+  setup remain on the allocating path; this is not a typing-path API.
+- Relative chunk lengths in fixed-pool block treaps replace absolute starts
+  and suffix shifts. Summaries carry byte/newline, edited, non-ASCII and unbuilt
+  counts. Edits detach deleted subtrees without sweeping their entries; retired
+  blocks supply entries on demand. Block coalescing prevents capacity growing
+  with edit count. Replacement admission is capped at 512 chunks (G), with
+  refusal before cancellation/model mutation; larger replacements require an
+  index recreated on the allocating path.
+- Mailbox receivers only stage ranges. Poll examines at most four messages
+  and adopts/traverses at most 64 entries, checking a 0.5 ms CPU deadline (G).
+  Refresh finds the first edited chunk via summaries and resumes up to one
+  chunk, 256 callbacks and the same CPU deadline (G). The source callback is
+  indivisible and must honor the caller's CPU contract.
+- Logical cancellation requests `work_cancel`, invalidates the application
+  generation, unbinds the receiver and retires the lease before any adoption
+  or cleanup. Physical retirement and release hooks run during maintenance.
+  Cancellation never invokes a release hook. Worker checks occur before and
+  after each callback and between 16 KiB scanner blocks (G). Partial cancelled
+  counts are never adopted. CPU diagnostics are read only after the work
+  completion acknowledgement.
+- Queued destruction uses the already-landed work cancellation/removal and
+  completion acknowledgement. A running callback or blocking I/O still has
+  to return before synchronous destruction can release its source; the header
+  states this lifetime limit rather than promising preemption.
+
+## Session s8 test integration
+
+The release allocator test now types 10,000 keys (G) and demands exactly zero
+allocations. Its byte oracle and index storage are reserved before the guard;
+the oracle uses `memmove`/`memcpy` during typing. The preceding red run showed
+the old oracle's allocation inside the guard. This strengthens acceptance
+evidence without changing production allocation behavior or adding globals.
+
+`tests/minimap_test.c` and `tests/scroll_test.c` had setup assertions requiring
+one bulk synchronous seek or refresh to complete. Scroll also explicitly
+expected a one-byte seek to build a whole chunk, the behavior rejected by §6.
+Those test setups now resume slices with finite loop bounds, and the one-byte
+assertions require zero built chunks. All their minimap/scroll behavioral and
+allocator assertions remain. These are migrations of the lineidx contract,
+not other-module production fixes.
+
+## Verification limits
+
+The benchmark's new worker-seek row uses a controlled partial prefix and
+checks an independent target-byte and viewport-cell oracle through null
+viewport submit. Logical acknowledgement and worker CPU are separate rows
+with executable nonzero limits. Measurements are loaded-box TRACK evidence;
+their numerical gate comparisons do not establish a gate verdict. No variant
+speedup is inferred from differently prepared workloads.
+
+Real display/renderer integration, cold performance verdicts, legacy
+`bench/scroll_bench.c` bulk-seek migration, and slicing other modules' source
+loops are outside this bead. The legacy ordinary line-to-byte/byte-to-line
+queries retain their one-chunk content-scan contract; use the resumable seek
+API for fragmented UI seeks. No new production globals or other-module
+production changes were made. LeakSanitizer remains for the coordinator.
diff --git a/docs/worker-reports/edit-4w1.47-s8.md b/docs/worker-reports/edit-4w1.47-s8.md
new file mode 100644
index 0000000000000000000000000000000000000000..2df1c59401eaf80541bf72e3b267b05c4993952b
--- /dev/null
+++ b/docs/worker-reports/edit-4w1.47-s8.md
@@ -0,0 +1,254 @@
+# edit-4w1.47 — session s8 worker report
+
+Scope: P1.6c, `docs/reviews/lineidx-1.md` findings §6–11. Continued WIP
+`c208ce7` and `0a1abb4` after reading CLAUDE.md, the bead, review and module
+status. Only read-only git commands were used. No commits, tracking changes,
+new globals or other-module production fixes were made. Display was `:99`.
+
+The implementation was already substantially present in the WIP. This session
+verified it, strengthened the allocator test, migrated test setup to the sliced
+API, completed the decision record and updated status/handoff/worklog.
+
+## Per-finding completion and red/green evidence
+
+Red excerpts for §6–10 below are preserved verbatim from the previous WIP
+session's `docs/decisions/P1.6c.md`, rather than claimed as newly run against
+the already-fixed implementation. Their old source line numbers predate the
+completed suite. All fresh focused green runs used clang ASan/UBSan with
+`ASAN_OPTIONS=detect_leaks=0`; each exited zero. Evidence is (M)[AC].
+
+### §6 — strict seek budget: done
+
+Partial chunk counts persist without declaring the whole chunk built. Scanning
+is capped before consuming a span; a target within the available slice becomes
+exact, otherwise the result remains a proven line anchor. Tests cover zero,
+one-byte, chunk-minus-one and boundary budgets; target changes and independent
+refresh advancing the prefix are also covered.
+
+Prior red (M)[AC], load1=5.62:
+```text
+tests/lineidx_test.c:769: FAIL s.calls <= budgets[i]
+lineidx_test: 10 failure(s)
+```
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=6
+review 6: strict byte budget + partial continuation ok
+review 6: continuation skips independently refreshed chunks
+lineidx_test: ok
+exit=0
+```
+
+### §7 — partial jumps off the UI thread: done
+
+`lineidx_seek_start_owned` scans an immutable source on the bulk worker,
+publishes the requested exact offset and stops before building the full tail.
+The synchronous seek and indexed fragmented continuation yield between bounded
+slices. The benchmark verifies a controlled partial prefix, independently
+checked target byte and correct null viewport submission.
+
+Prior red (M)[AC], load1=6.20:
+```text
+tests/lineidx_test.c:802: FAIL s.calls <= 256
+tests/lineidx_test.c:803: FAIL !q.exact
+lineidx_test: 2 failure(s)
+```
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=7
+review 7: synchronous continuation yields at span limit
+review 7: worker seek publishes target before full index
+review 7: indexed fragmented seek resumes between slices
+lineidx_test: ok
+exit=0
+```
+
+### §8 — bounded foreground metadata: done
+
+Fixed-pool block treaps hold relative lengths and incremental summaries, avoiding
+suffix shifts and whole-table dirty/edited walks. Deleted subtrees retire without
+an entry sweep. Mailbox callbacks stage ranges; poll adopts a bounded batch.
+Refresh resumes partial counts and consistently clears edited state. Tests check
+large geometry with an active worker, accumulated publication backlog, edits at
+the beginning/middle/EOF and large subtree deletion.
+
+Prior red (M)[AC], load1=6.20:
+```text
+tests/lineidx_test.c:819: FAIL lineidx_foreground_work(x) - before <= 4096
+tests/lineidx_test.c:832: FAIL applied <= 16
+tests/lineidx_test.c:833: FAIL !lineidx_complete(x)
+lineidx_test: 3 failure(s)
+```
+The final adoption limit is 64 traversed entries with a 0.5 ms thread CPU
+deadline (G), instead of the earlier proposed 16-entry limit (G).
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=8
+review 8: 10 GB edit/query and backlog adoption bounded
+review 8: indexed 10 GB edits at start/middle/EOF + subtree deletion ok
+lineidx_test: ok
+exit=0
+```
+
+### §9 — cancellation acknowledgement/suppression: done
+
+Cancellation requests work cancellation and invalidates/unbinds the result
+identity before adoption or source cleanup. Retirement is deferred. The final
+publication awaiting adoption is dropped; cancellation invokes no release hook.
+Separate benchmark rows measure logical return and worker CPU, with nonzero
+limits and failure exits.
+
+Prior red (M)[AC], load1=6.20:
+```text
+tests/lineidx_test.c:890: FAIL atomic_load(&wp.slots[h.slot].epoch) != h.epoch
+tests/lineidx_test.c:891: FAIL !atomic_load(&s.released)
+tests/lineidx_test.c:894: FAIL delivered == 0 && !lineidx_complete(x)
+lineidx_test: 3 failure(s)
+```
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=9
+review 9: cancel invalidates before receive/release
+lineidx_test: ok
+exit=0
+```
+
+### §10 — fragmented worker sources: done
+
+Workers check cancellation on both sides of every source callback and between
+bounded scanner blocks. Tests cancel inside an actual CPU-cost callback and
+inside a one-byte fragmented source, proving no subsequent callback or partial
+count publication. An indivisible callback must itself honor the documented
+CPU limit; arbitrary callback/I/O preemption is not promised.
+
+Prior red (M)[AC], load1=6.20:
+```text
+tests/lineidx_test.c:915: FAIL atomic_load(&s.calls) == 1
+tests/lineidx_test.c:916: FAIL atomic_load(&s.cpu_ns) <= 5000000
+lineidx_test: 2 failure(s)
+```
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=10
+review 10: cancelled first span requests no further data
+review 10: one-byte source cancels inside partially counted chunk
+lineidx_test: ok
+exit=0
+```
+
+### §11 — queued destruction: verified inherited fix
+
+Main's P1.8c/d already removes cancelled queued jobs and acknowledges physical
+completion. The deterministic current test holds an unrelated job, queues the
+index and verifies destruction/release before that unrelated job is released,
+with no index source accesses. No new work-module fix was appropriate.
+
+To supply an actual red reproduction, this session extracted the original
+lineidx and work implementation/headers from commit `723e99f` using read-only
+`git show`, compiled them under the normal warning flags with a small queued
+destroy driver in ignored `build/s8-historical/`, and held the unrelated job.
+Historical red (M)[AC]:
+```text
+$ timeout 2 build/s8-historical/review11
+review 11 historical: destroy queued index with unrelated bulk job held
+exit=124
+```
+The watchdog is a deadlock witness, not a performance gate. This is an original
+implementation reproduction, not a claim that rebased main was still broken.
+The prerequisite fix predates this session's test; strict chronological
+red-first evidence for that prerequisite belongs to its original bead.
+
+Fresh green (M)[AC]:
+```text
+$ build/san/tests/lineidx_test --review=11
+review 11: queued destroy returns before unrelated bulk job
+lineidx_test: ok
+exit=0
+```
+
+## Additional acceptance and integration tests
+
+The old allocator test subtracted byte-model allocations. The stronger
+10,000-key zero-allocation assertion (G) was written and run before changing
+the byte-model storage. Red (M)[AC]:
+```text
+$ build/tests/lineidx_test --review=alloc
+tests/lineidx_test.c:656: FAIL mallocs == 0
+lineidx_test: 1 failure(s)
+exit=1
+```
+The fix reserves the byte model before the guard and mutates it in place; no
+production allocation change was needed. Green (M)[AC]:
+```text
+$ build/tests/lineidx_test --review=alloc
+lineidx typing: keys=10000 allocations=0 guard=active
+lineidx_test: ok
+exit=0
+```
+
+The full suite exposed caller test setups depending on bulk synchronous work:
+```text
+minimap_test:102: lineidx_complete(idx)
+make: *** [Makefile:101: check] Error 1
+scroll_test:101: FAIL lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes).exact
+scroll_test:251: FAIL lineidx_built_prefix(index) == 1
+scroll_test:186: FAIL lineidx_built_prefix(index) == 1
+```
+Minimap/scroll setup now resumes bounded slices, and scroll's one-byte-budget
+assertions require zero built chunks. Their behavioral assertions remain.
+Both release and sanitizer runs are green (M)[AC]:
+```text
+minimap_test: density, hit round trip, stale, malloc guard: ok
+scroll_test: bounded long-line/source errors/partial seeks PASS; allocations=0 guard=active
+scroll_test: PASS
+```
+
+## Final verification
+
+Final suite aggregation is pending at this report's first write and will be
+updated before the worker exits. Release build exits zero under gcc 13.3;
+clang 18.1.3 focused §6–11 runs and the full lineidx sanitizer suite exit zero.
+All required warnings are enabled. LeakSanitizer is disabled because it cannot
+run inside this sandbox; the coordinator must rerun with leaks enabled.
+The initial sandbox suite could not connect to Xvfb; the socket-capable suite
+uses the same `DISPLAY=:99 EDIT_DISPLAY=:99` and sanitizer settings.
+
+Fuzz runs (M)[AC], ASan/UBSan, each requested 60 seconds (G):
+```text
+$ build/fuzz/lineidx_fuzz -max_total_time=60 -timeout=15 -max_len=2048
+Done 5542 runs in 61 second(s)
+exit=0
+$ build/fuzz/lineidx_fuzz -max_total_time=60 -timeout=15 -max_len=2048 build/s8-fuzz-corpus
+Done 466 runs in 61 second(s)
+exit=0
+```
+The second run starts from generated edit/query/build/cancel/worker-seek
+sequences rather than relying on mutation from an empty corpus.
+
+Benchmark `--self-check` exits zero; `--track --reps=3` exits zero.
+One loaded-box run, BAT0 `Not charging`, [AC], final load1=26.75 (M)[AC]:
+```text
+BENCH name=TRACK_G6c_lineidx_cancel_logical n=3 p50=1156 p99=1546 ci95=[564,1546] ns (M)[AC] load=26.73 gate_p50=1000000 gate_p99=5000000 ns (G) pass=1
+BENCH name=TRACK_G6c_lineidx_cancel_cpu_slice n=3 p50=2010439 p99=2011749 ci95=[2010050,2011749] ns (M)[AC] load=26.73 gate_p50=5000000 gate_p99=5000000 ns (G) pass=1
+BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=3 p50=332099104 p99=363682901 ci95=[146035923,363682901] ns (M)[AC] load=26.75 gate_p50=0 gate_p99=0 ns (G) pass=1
+lineidx_bench: TRACK only; gates unvalidated
+```
+These are measurements only. Numerical pass fields are not gate verdicts on
+this loaded box. No speedup comparison is drawn between differently prepared
+workloads. Every worker-seek sample independently checked its target byte and
+viewport cells; its partial prefix stayed before the target.
+
+## Missing, limits and decisions
+
+Decisions are recorded in [edit-4w1.47.md](../decisions/edit-4w1.47.md).
+Request allocation/geometry copying is still an allocating-path operation;
+source release hooks and active blocking I/O cannot be preempted. Ordinary
+line-to-byte/byte-to-line queries retain their legacy single-chunk scans; the
+new seek API is the resumable UI entry point.
+
+Outside scope and left unchanged: `bench/scroll_bench.c` still assumes a bulk
+synchronous seek and needs migration to a worker/continuation request; other
+modules' independent fragmented-source loops need their own review. Actual
+renderer/display integration and cold performance verdicts remain unvalidated.
+The benchmark uses null viewport submission. The coordinator supplies the
+LeakSanitizer run, reviews the inherited §11 evidence, and commits this tree.
diff --git a/tests/lineidx_test.c b/tests/lineidx_test.c
index 190524593b90ac6fd9301eaedd4d56fe5c6aa341..b8feace554ca298d204ec6f215f0c6a7540279f1
--- a/tests/lineidx_test.c
+++ b/tests/lineidx_test.c
@@ -631,7 +631,11 @@
 {
     uint64_t n = 4u << 20;
     uint8_t *b = mkbuf(n, 0);
-    lineidx *x = lineidx_create_reserved(n, 256); /* reserve this test's typing burst */
+    const unsigned keys = 10000;
+    uint8_t *extended = realloc(b, (size_t)n + (size_t)keys * 64u);
+    REQUIRE(extended != NULL);
+    b = extended;
+    lineidx *x = lineidx_create_reserved(n, keys); /* reserve this test's typing burst */
     REQUIRE(x != NULL);
     flat f = { b, n, 0, 0 };
     lineidx_src s = mk(&f);
@@ -640,20 +644,25 @@
     uint8_t ins[64]; memset(ins, 'a', sizeof ins);
     edit_malloc_guard_begin();
     volatile uint64_t sink = 0;
-    for (int i = 0; i < 200; i++) {
+    for (unsigned i = 0; i < keys; i++) {
         uint64_t off = rnd() % (n + 1);
         sink += lineidx_line_to_byte(x, &s, rnd() % 50000).value;
         sink += lineidx_byte_to_line(x, &s, off).value;
         sink += lineidx_line_count(x).value;
         sink += lineidx_seek_line(x, &s, rnd() % 50000, 1u << 20).value;
         CHECK(lineidx_edit(x, off, 0, sizeof ins) == 0);
-        model_edit(&b, &n, off, 0, ins, sizeof ins);   /* model malloc is counted: compensate below */
+        /* The independent byte model also uses its open-path reserve, so the
+         * guard covers the whole burst without subtracting model allocations. */
+        memmove(b + off + sizeof ins, b + off, (size_t)(n - off));
+        memcpy(b + off, ins, sizeof ins);
+        n += sizeof ins;
         f.b = b; f.n = n; s.len = n;
         while (lineidx_refresh(x, &s)) { }
     }
     size_t mallocs = edit_malloc_guard_end();
-    /* model_edit mallocs once per iteration; the index itself must add none */
-    if (edit_malloc_guard_active()) CHECK(mallocs == 200);
+    if (edit_malloc_guard_active()) CHECK(mallocs == 0);
+    fprintf(stderr, "lineidx typing: keys=%u allocations=%zu guard=%s\n",
+            keys, mallocs, edit_malloc_guard_active() ? "active" : "sanitizer skipped");
     (void)sink;
     lineidx_destroy(x);
     free(b);
diff --git a/tests/minimap_test.c b/tests/minimap_test.c
index de15f8ffaf5993f6a384e61b514c7c81f922e0c1..289ea70b80f73c9ddcb7d59535ad33a6759c3a36
--- a/tests/minimap_test.c
+++ b/tests/minimap_test.c
@@ -99,7 +99,10 @@
     memset(cells,0,sizeof cells);
     minimap_input in = {src, lineidx_line_count(idx).value, 8, lineidx_complete(idx)};
     CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && m.stale && f.calls == 0);
-    (void)lineidx_seek_line(idx,&src,UINT64_MAX,n); CHECK(lineidx_complete(idx));
+    /* Index setup resumes the bounded lineidx API between UI slices. */
+    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(idx); slice++)
+        (void)lineidx_seek_line(idx,&src,UINT64_MAX,n);
+    CHECK(lineidx_complete(idx));
     in.lines = lineidx_line_count(idx).value; in.index_ready = true; f.calls = 0;
     edit_malloc_guard_begin();
     CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale && m.sampled);
@@ -138,7 +141,9 @@
     in.revision++; in.index_ready = false;
     CHECK(minimap_stale(&m,&in));
     CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && m.stale);
-    (void)lineidx_refresh(idx,&src); CHECK(lineidx_complete(idx));
+    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(idx); slice++)
+        (void)lineidx_refresh(idx,&src);
+    CHECK(lineidx_complete(idx));
     in.lines = lineidx_line_count(idx).value; in.index_ready = true;
     CHECK(minimap_stale(&m,&in));
     CHECK(minimap_fill(&m,&in,&g,6,3,0,1,&style) == 0 && !m.stale);
diff --git a/tests/scroll_test.c b/tests/scroll_test.c
index 5674af0a3b5ddb511720db2d4d6e7568f5b391a4..078d482d1d5058cbc240c69006156f3e14159422
--- a/tests/scroll_test.c
+++ b/tests/scroll_test.c
@@ -98,7 +98,9 @@
     uint64_t cursor = start + 11 * 7 + 3, row_before = (cursor - start) / 11;
     /* Publication can land BETWEEN an event transition and its resolution. */
     CHECK(scroll_wheel(&s, 256) == 0);
-    CHECK(lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes).exact);
+    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(index); slice++)
+        (void)lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes);
+    CHECK(lineidx_complete(index));
     CHECK(scroll_resolve(&s, index, &src, 0) == 0 && !s.approximate);
     CHECK(s.first_byte == start + 33 && s.first_line == start / 11 + 3 && s.subrow_q8 == 701);
     CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
@@ -181,7 +183,7 @@
     edit_malloc_guard_begin();
     guarding = true;
     CHECK(scroll_seek_line(&s, 1) == 0 && scroll_resolve(&s, index, &src, 1) == 0);
-    CHECK(lineidx_built_prefix(index) == 1); /* lineidx rounds budget to one chunk */
+    CHECK(lineidx_built_prefix(index) == 0); /* seek respects the one-byte budget */
     CHECK(scroll_wheel(&s, 1) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.subrow_q8 == 51);
     CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0 && s.subrow_q8 == 0);
     for (unsigned i = 0; i < 1000; i++) {
@@ -246,10 +248,15 @@
     CHECK(scroll_init(&s, (scroll_config){1, 17, 0}, (scroll_extent){sizeof bytes, 100, false}) == 0);
     before = s;
     CHECK(!lineidx_seek_line(index, &src, UINT64_MAX, 1).exact);
+    CHECK(lineidx_built_prefix(index) == 0); /* one byte cannot build a chunk */
+    for (unsigned slice = 0; slice < 20000 && lineidx_built_prefix(index) == 0; slice++)
+        (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
     CHECK(lineidx_built_prefix(index) == 1);
     CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE);
     CHECK(memcmp(&s, &before, sizeof s) == 0);
-    CHECK(lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes).exact);
+    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(index); slice++)
+        (void)lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes);
+    CHECK(lineidx_complete(index));
     CHECK(scroll_follow_cursor(&s, index, &src, 150000) == 0);
     CHECK(s.first_byte == 1024 && s.first_line == 1 && !s.approximate);
     size_t allocations = edit_malloc_guard_end();

---

## Final message (codex -o)

Completed the WIP. [Report with red/green evidence](/home/tobias/Projects/sublimite/.wt/edit-4w1.47/docs/worker-reports/edit-4w1.47-s8.md).

- `make all` and `make check` pass.
- Both fuzz runs finished cleanly in 61 seconds (M)[AC].
- 10,000 typing operations allocated zero times (M)[AC].
- LeakSanitizer disabled; coordinator reruns with leaks enabled.

Out-of-scope scroll benchmark migration is documented. No git writes; ready for coordinator review and commit.