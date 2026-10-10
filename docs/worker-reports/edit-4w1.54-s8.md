# Journal status — P1.9h / edit-4w1.54

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

UNRESOLVED within this bead's allowed scope: review §§4,7,8. Shared bulk work
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
sandbox. Current results: final GCC make all exit=0 ([AC] load1=6.25); make fuzz
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

diff --git a/HANDOFF.md b/HANDOFF.md
index e73712d60fed64024feefc92559008554676a815..6f3fd424d5456f56938acd72350ac55466d7881f
--- a/HANDOFF.md
+++ b/HANDOFF.md
@@ -18,7 +18,7 @@
 | edit-457.16 P4.I wiring (chokepoint) | wt/edit-457.16 (301a7d1 WIP) | substantively done per its rollout (suites green, 0 allocs over 10k keys with 100 tabs + IPC); killed while polishing; no report | rebase (known conflict src/editor/open.c, resolved in wt/edit-457.16-snap 90919f7), verify, Codex high finisher "verify, fix red, write report, no new scope", land |
 | edit-zzj.13 editor review fixes (+P1.9-2 §2/3/5, WM_DELETE_WINDOW → exit within a frame) | wt/edit-zzj.13 (1efc6e1 WIP, on the 457.16 snapshot) | started 17:25, early | continue after 457.16 lands |
 | edit-zzj.15 EGL default backend, raster fallback | wt/edit-zzj.15 (19b4672 WIP, on the snapshot) | started 17:25, early | continue after 457.16 |
-| edit-4w1.54 journal re-review fixes | wt/edit-4w1.54 (e251eba WIP) | ~1 h in | continue |
+| edit-4w1.54 journal re-review fixes | wt/edit-4w1.54 (session 8 continuation) | §§1,6,9–14 fixed; bounded INSERT steps/byte credit added; §§4,7,8 remain integration work | see docs/worker-reports/edit-4w1.54-s8.md; coordinator verification/commit |
 | edit-4w1.47 lineidx UI slices | wt/edit-4w1.47 (03688a4 WIP) | ~45 min in | continue |
 | edit-4w1.43 file keep/check/save semantics | wt/edit-4w1.43 (57b42ad WIP) | ~15 min in | continue |
 | edit-457.19 font review fixes | wt/edit-457.19 (543bbe7 WIP) | ~45 min in | continue |
diff --git a/bench/journal_bench.c b/bench/journal_bench.c
index f62481689518d81d1370185618d0538d2f763d38..1b9c08112324cb3b857603ce320cf2164f8af72b
--- a/bench/journal_bench.c
+++ b/bench/journal_bench.c
@@ -230,6 +230,54 @@
         syncs,(double)(worker_cpu_ns(clk)-before)/(1000.0*syncs),(double)parked_cpu/(1000.0*syncs),evidence(tag,sizeof tag));
     journal_close(j); unlink(path); return 0;
 }
+/* TRACK comparison: alternate adjacent variants on the same loaded box.
+ * Sum enqueue calls separately from the continuation's off-path fence waits;
+ * neither endpoint includes editor mutation/layout/submit. */
+static int insert_variants(work_pool *pool)
+{
+    stamp("insert_variants"); char tag[128];
+    size_t size=1000000; uint8_t *bytes=malloc(size); REQUIRE(bytes);
+    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(i*17u);
+    uint64_t totals[2][16], calls[2048], completion[2][16];
+    bench_samples total[2], complete[2], steps;
+    for(unsigned v=0;v<2;v++) {
+        bench_samples_init(&total[v],totals[v],16);
+        bench_samples_init(&complete[v],completion[v],16);
+    }
+    bench_samples_init(&steps,calls,2048);
+    for(unsigned pair=0;pair<16;pair++) for(unsigned order=0;order<2;order++) {
+        unsigned variant=(pair+order)%2;
+        char path[]="/tmp/journal-variant-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
+        journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0);
+        size_t progress=0; uint64_t cpu_calls=0, start=bench_now_ns();
+        if(!variant) {
+            int rc; edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
+            rc=journal_insert(j,1,0,bytes,size); cpu_calls=bench_now_ns()-before;
+            REQUIRE(edit_malloc_guard_end()==0 && rc==0); progress=size;
+        } else while(progress<size) {
+            size_t before_progress=progress;
+            edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
+            int rc=journal_insert_step(j,1,0,bytes,size,&progress);
+            uint64_t elapsed=bench_now_ns()-before;
+            REQUIRE(edit_malloc_guard_end()==0 && (rc==0 || rc==JOURNAL_BUSY));
+            (void)bench_add(&steps,elapsed); cpu_calls+=elapsed;
+            if(progress==before_progress) REQUIRE(journal_flush(j)==0);
+        }
+        (void)bench_add(&total[variant],cpu_calls); (void)bench_add(&complete[variant],bench_now_ns()-start);
+        REQUIRE(journal_flush(j)==0); journal_close(j);
+        piece_allocator a=piece_default_allocator(); paste_replay replayed={bytes,size,0,piece_create(&a)};
+        REQUIRE(replayed.tree); journal_replay_result rr;
+        REQUIRE(journal_replay_file(path,restore_paste,&replayed,&rr)==0 && !rr.corrupt && replayed.copied==size && piece_len(replayed.tree)==size);
+        piece_destroy(replayed.tree); unlink(path);
+    }
+    for(unsigned v=0;v<2;v++) printf("TRACK journal_insert_variant variant=%s pairs=16 bytes=%zu enqueue_sum_p50_ms=%.3f enqueue_sum_p99_ms=%.3f protected_completion_p50_ms=%.3f protected_completion_p99_ms=%.3f content_verified=1 allocations=0 (M)%s\n",
+        v?"continuation":"one_shot",size,(double)bench_p50(&total[v])/1e6,(double)bench_p99(&total[v])/1e6,
+        (double)bench_p50(&complete[v])/1e6,(double)bench_p99(&complete[v])/1e6,evidence(tag,sizeof tag));
+    printf("TRACK journal_insert_step calls=%zu p50_us=%.3f p99_us=%.3f input_check_boundaries=1 byte_credit_enforced=1 wall_time_unbounded=1 (M)%s\n",
+        steps.n,(double)bench_p50(&steps)/1e3,(double)bench_p99(&steps)/1e3,evidence(tag,sizeof tag));
+    REQUIRE(!steps.dropped && !total[0].dropped && !total[1].dropped);
+    free(bytes); return 0;
+}
 typedef struct exhausted_prefix { uint64_t bytes, records; } exhausted_prefix;
 static int restore_exhausted(void *ctx, const journal_record *r)
 {
@@ -306,9 +354,11 @@
 int main(int argc, char **argv)
 {
     if(argc==2 && !strcmp(argv[1],"--gate-self-check")) return gate_self_check();
-    bool track=argc==2 && !strcmp(argv[1],"--track"); unsigned misses=0;
+    bool variants=argc==2 && !strcmp(argv[1],"--insert-variants");
+    bool track=variants || (argc==2 && !strcmp(argv[1],"--track")); unsigned misses=0;
     work_pool pool; REQUIRE(work_pool_init(&pool,1,0)==0); int fail=0;
-    if(argc==2 && !strcmp(argv[1],"--worker")) fail=worker_cpu(&pool);
+    if(variants) fail=insert_variants(&pool);
+    else if(argc==2 && !strcmp(argv[1],"--worker")) fail=worker_cpu(&pool);
     else if(argc==2 && !strcmp(argv[1],"--fixtures")) { fail=paste(&pool,1,&misses); if(!fail) fail=session(&pool,1,false,&misses); }
     else if(argc!=1 && !track) fail=2;
     else { fail=worker_cpu(&pool); if(!fail) fail=paste(&pool,100,&misses); if(!fail) fail=session(&pool,1,true,&misses); if(!fail) fail=session(&pool,1024,true,&misses); if(!fail) fail=exhaustion(&pool); if(!fail) fail=idle(&pool,false); if(!fail) fail=idle(&pool,true); }
diff --git a/docs/decisions/edit-4w1.54.md b/docs/decisions/edit-4w1.54.md
new file mode 100644
index 0000000000000000000000000000000000000000..890f3cbb093cc283916977408a70d013f88c2ba1
--- /dev/null
+++ b/docs/decisions/edit-4w1.54.md
@@ -0,0 +1,87 @@
+# edit-4w1.54 — session 8 journal continuation decisions
+
+Scope is P1.9-2 §§1,4,6–14. Earlier implementation and red/green evidence
+remain in [P1.9h.md](P1.9h.md). This continuation preserves those fixes and
+adds journal-side prerequisites for §§4 and 7. It does not close §§4,7,8.
+Editor/main findings §§2,3,5 belong to edit-zzj.13 and were not changed.
+
+## Caller-driven protected INSERT steps
+
+`journal_insert_step` retains the existing synchronous page-cache contract,
+but encodes/copies/checksums and attempts only one record per call. The payload
+ceiling is 16 KiB (G), reduced for a smaller configured batch or sync budget.
+The caller keeps the immutable input and a byte progress cursor; no new
+allocation, lock, worker submission or global state is needed. Each successful
+step protects its prefix before returning. BUSY with advanced progress means
+the caller can check input and resume; BUSY without progress requires a
+durability fence or transport-gap repair. The caller may abandon the unaccepted
+tail. This is deliberately a sequence of protected prefix mutations rather than
+an atomic reservation of the entire paste. The legacy `journal_insert` retains
+its existing preflight/whole-input IO-retention contract.
+
+On IO the cursor includes the RAM-retained record, even after a short write.
+Callers suspend completion, drain/retry off path and never append that record
+again. FULL/previous IO/refused admission advance no cursor. Zero length and an
+already complete cursor are no-ops; invalid offsets/cursors are rejected.
+
+This byte/attempt ceiling bounds encoding work between caller input checks.
+A Linux regular-file pwrite can still stall. The editor must use the new API
+and apply/publish the corresponding protected prefix in its continuation;
+the module alone cannot assert an unconditional UI wall-time bound.
+
+## Complete durable-prefix credit
+
+Stats now expose `durable_bytes` and `unprotected_bytes`. The former advances
+only after a successful data fence through COMPLETE records, including PAD;
+the latter is the actual UI-written file extent beyond that conservative
+prefix. Newer UI writes are preserved when older worker stats are received.
+A sync ending inside a large CRC-protected record releases no credit for that
+record's partial prefix. A failed checkpoint-name barrier exposes no durable
+bytes until directory retry succeeds.
+
+Each INSERT step reserves its eventual PAD and returns BUSY before admitting
+a record that would exceed configured sync_bytes beyond the received durable
+prefix. This remains conservative if a kernel fence also covers concurrent
+UI writes in the other batch. The delayed-bulk durable-image test shows no
+admission beyond the default 64 KiB (G) and successful continuation after a
+fence. Credit also works at the smallest 4 KiB (G) sync budget, and disk
+exhaustion still reports sticky FULL.
+
+This is an admission guarantee for the continuation API. Legacy INSERT and
+other append APIs retain their current compatibility contract and can exceed
+that volume. A complete default guarantee requires the editor to use bounded
+protected continuations for every mutation and work to reserve independent
+durability execution. A stalled device/worker still prevents a hard elapsed
+time guarantee. The PRD maximum has not been weakened to a cadence target.
+
+## Save preparation remains an integration obligation
+
+The current standalone savectl performs legacy prepare/finish on a worker,
+using an exclusively leased journal and a distinct transport pool. Its caller
+defers journal edits during the lease, and the current editor is not wired to
+that controller. Moving only an entry point would therefore leave later edits
+unprotected or associate the wrong snapshot with the saved cutoff. No journal
+lease/async API has been added without that integration. §8 remains open:
+capture the immutable snapshot at enqueue, keep later protected deltas, perform
+retention/checkpoint IO off the UI, and adopt through work mailboxes before
+authorizing replacement. Existing retention mailbox/full-metadata semantics
+remain enforced by their regression tests.
+
+## Evidence
+
+All session 8 runs use DISPLAY=:99 and EDIT_DISPLAY=:99. Power was Not charging
+[AC]. Measurements are from the loaded shared box; no timing is a gate verdict.
+The required worker report contains exact red/green output and final checks:
+[edit-4w1.54-s8.md](../worker-reports/edit-4w1.54-s8.md).
+
+The one paired benchmark alternated adjacent variants, reversing the initial
+variant each pair, with independent exact-content replay and active malloc
+checks. For a 1 MB (G fixture) paste, one-shot enqueue p50/p99 was
+0.864/1.063 ms (M)[AC], and continuation enqueue-call sums were
+1.391/1.935 ms (M)[AC]. Individual continuation calls p50/p99 were
+17.488/56.857 us (M)[AC]. Continuation protected completion, including the
+fixture's blocking off-path fence waits, was 34.823/40.195 ms (M)[AC]. Launch
+load1 was 15.86 (M)[AC]; row load1 was 15.56 (M)[AC]. The test fixture has
+input-check opportunities between calls; it does not run an editor event loop
+or establish G1/G9. The cost of durability credit must be accounted for in that
+integration. No repeat timing run or quiet-box wait was performed.
diff --git a/docs/worker-reports/edit-4w1.54-s8.md b/docs/worker-reports/edit-4w1.54-s8.md
new file mode 100644
index 0000000000000000000000000000000000000000..f3f8c3474352061ee58b5038700a65fe2592817a
--- /dev/null
+++ b/docs/worker-reports/edit-4w1.54-s8.md
@@ -0,0 +1,147 @@
+# edit-4w1.54 session 8 worker report
+
+The bead remains incomplete: review §§4,7,8 still require work/editor/save
+integration. Journal fixes for §§1,6,9–14 are retained and verified. This session
+adds bounded protected INSERT continuations, complete durable-prefix stats,
+PAD-aware byte admission, a delayed-worker durable-image oracle, fuzz operations
+and a paired benchmark. No editor/main/work/file changes, git writes, new
+globals or typing-path allocation were introduced.
+
+Read order: CLAUDE.md, the supplied bead, P1.9-2 named sections, journal STATUS;
+then HANDOFF, PLAN workflow and binding PRD/performance sections. Inspected the
+requested read-only log and main diff. Starting WIP commits were `1f8f4d4` and
+`b62329b`; main diff covered journal source/header, unit/kill/fuzz/bench and docs.
+Decisions: [edit-4w1.54.md](../decisions/edit-4w1.54.md), with earlier evidence
+in [P1.9h.md](../decisions/P1.9h.md).
+
+## Per finding
+
+| Review | Result and remaining work |
+|---|---|
+| §1 BLOCKER | Fixed in WIP: sync/writeback IO poisons the live generation; retry cannot acknowledge it. A complete CURRENT checkpoint in a fresh durably published inode is required. Lost-writeback oracle includes a released unsynced batch. |
+| §4 BLOCKER | Partial: actual UI cache extent and complete durable-prefix stats now replace worker-accounted bytes for new INSERT continuation admission. Delayed-bulk test bounds volume, including PAD, and verifies the durable image after resumption. Legacy appends remain unbounded; independent durability scheduling and the PRD time limit remain unresolved. |
+| §6 MAJOR | Fixed in WIP: full retained-inode metadata fsync seam; empty-base mtime persistence oracle. |
+| §7 MAJOR | Partial: WIP hardware/fallback CRC remains; new continuation limits one copy/CRC/write record per caller slice, validates each page-cache prefix and allocates nothing. UI pwrite wall time and editor protected-completion integration remain unresolved. |
+| §8 MAJOR | Unresolved: synchronous journal preparation remains available; standalone savectl leases it to a worker but defers later journal edits, and editor wiring is outside this bead. Need immutable snapshot enqueue, concurrent protected deltas and mailbox adoption. |
+| §9 MAJOR | Fixed in WIP at module scope: default benchmark fails local percentile/dropped-sample misses; explicit TRACK retains correctness failure; deterministic injected-latency self-check. Complete G1/G9 endpoints require the editor harness. |
+| §10 MAJOR | Fixed in WIP: every VIEW/TABS/WINDOW field has independent unit, power-image, rotation, SIGKILL, fuzz and bench expectations, including CRC-valid semantic mutants. |
+| §11 MAJOR | Fixed in WIP: overlapping tokens preserve both targets, cutoffs, later deltas and retained availability in both finish orders; publication/retirement faults are covered. |
+| §12 MAJOR | Fixed in WIP: immutable retention result crosses routed work mailbox; storage survives receipt, no shared done polling. |
+| §13 MINOR | Fixed in WIP: PAD IO outranks submit BUSY, failed PAD is not retried on the UI. |
+| §14 MINOR | Fixed in WIP: retained bases use the bounded recovery directory/ancestor; maximal valid BASE/journal path coverage. |
+
+§§2,3,5 are explicitly out of scope, owned by edit-zzj.13; no fixes made here.
+This report does not reinterpret PRD loss limits or claim whole-editor gates.
+
+## Red/green evidence
+
+Historical red output below is pasted from the WIP's P1.9h decision record,
+not represented as newly executed baseline runs. Those tests were written and
+run before their respective WIP fixes. All historical measured evidence is
+(M)[AC]; historical loads appear next to each group. New session 8 red runs
+were executed before the corresponding changes.
+
+Historical RED, §1/6/12/13/14, load1=8.54 (M)[AC]:
+
+```text
+journal_test:1089 FAIL journal_retry(j)==JOURNAL_IO
+journal_test:1132 FAIL oracle.full_synced
+journal_test:1158 FAIL journal_save_prepare(j,1,&b,&cp,1,&save)==0 && c.mailbox
+journal_test:1172 FAIL journal_pump(j,1,true)==JOURNAL_IO
+journal_test:1200 FAIL journal_save_prepare(j,1,&b,&cp,1,&save)==0 && save.prepared
+```
+
+Historical RED, §9/10, load1=4.67 (M)[AC], and §11, load1=2.46 (M)[AC]:
+
+```text
+journal_bench:266 FAIL finish_bench(0,false,misses)!=0
+journal_test:1240 FAIL cache_replay(path,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected)
+journal_test:1296 FAIL journal_replay_file(path,check_save_marker,&expected[0],&rr)==0
+journal_test:1333 FAIL overlapping_markers(path,markers)==0
+```
+
+Session 8 RED, §7 continuation, command `build/tests/journal_test --insert-steps`,
+load1=24.63 (M)[AC]. The test was written first; a one-shot adapter made the
+legacy behavior executable under the new interface before implementing slicing:
+
+```text
+journal_test:1655 FAIL progress>before && progress-before<=JOURNAL_INSERT_SLICE_BYTES
+```
+
+Session 8 RED, §4 actual cache accounting, command
+`build/tests/journal_test --insert-credit`, load1=17.83 (M)[AC]:
+
+```text
+journal_test:1721 FAIL st.unprotected_bytes==(uint64_t)sb.st_size && st.durable_bytes==0
+```
+
+After accounting was fixed, the same test remained RED for admission,
+load1=13.52 (M)[AC]:
+
+```text
+journal_test:1724 FAIL progress>0 && progress<sizeof bytes
+```
+
+Session 8 RED for the smallest sync budget, command
+`build/tests/journal_test --insert-steps`, load1=11.49 (M)[AC]:
+
+```text
+journal_test:1689 FAIL journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_BUSY && progress==4096-72
+```
+
+GREEN for the session 8 changes:
+
+```text
+journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence
+journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes
+```
+
+The remaining contract probes deliberately stay RED and are opt-in. They are
+not part of passing `make check`; no GREEN is claimed for their full findings.
+Historical red (§4/7/8), launch load1=1.90 (M)[AC]:
+
+```text
+journal_test: bulk-loss repro unprotected_bytes=1602856 (M) limit=65536 (G) durable_records=0
+journal_test:1415 FAIL unprotected<=JOURNAL_DEFAULT_SYNC_BYTES
+journal_test: append-bound repro elapsed_ns=20337358 (M) UI_slice_ns=500000 (G) attempts=1
+journal_test:1432 FAIL elapsed<=500000
+journal_test: save-ack repro elapsed_ns=210829012 (M) G8s_p99_ns=5000000 (G)
+journal_test:1453 FAIL elapsed<=5000000
+```
+
+## Verification
+
+All displays are :99. Compiler versions: gcc 13.3.0 and clang 18.1.3.
+Build flags include C11, -Wall -Wextra -Werror -Wshadow -Wconversion.
+All session 8 power stamps were Not charging [AC]. Required final run results
+will be filled below when the currently running checks complete.
+
+The first sandboxed `make check` failed because cli_test could not connect to
+Xvfb :99. A local-socket escalation was approved. A subsequent run caught the
+expected smallest-credit RED in the then-current build while the fix was being
+completed; the final run rebuilds the corrected source. Only LeakSanitizer is
+disabled with `ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with
+leaks enabled. No :0 window was opened.
+
+Release journal GREEN, launch load1=13.53 (M)[AC]:
+
+```text
+journal_test: malloc_guard=active append_allocations=0
+journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
+```
+
+Paired benchmark ran once, adjacent variants in alternating order. Launch
+load1=15.86 and row load1=15.56 (M)[AC]. Timing is TRACK evidence, no gate verdict:
+
+```text
+TRACK journal_insert_variant variant=one_shot pairs=16 bytes=1000000 enqueue_sum_p50_ms=0.864 enqueue_sum_p99_ms=1.063 protected_completion_p50_ms=0.864 protected_completion_p99_ms=1.064 content_verified=1 allocations=0 (M)[AC] load1=15.56
+TRACK journal_insert_variant variant=continuation pairs=16 bytes=1000000 enqueue_sum_p50_ms=1.391 enqueue_sum_p99_ms=1.935 protected_completion_p50_ms=34.823 protected_completion_p99_ms=40.195 content_verified=1 allocations=0 (M)[AC] load1=15.56
+TRACK journal_insert_step calls=1312 p50_us=17.488 p99_us=56.857 input_check_boundaries=1 byte_credit_enforced=1 wall_time_unbounded=1 (M)[AC] load1=15.56
+journal_bench: correctness=ok mode=TRACK gate_misses=0 status=0 (complete G1/G9 endpoints require editor harness)
+journal_bench: gate self-check ok (injected delay fails GATE; TRACK preserves correctness failures)
+```
+
+The continuation completion row includes the fixture's blocking fence waits;
+it demonstrates the scheduling cost still requiring editor integration. The
+individual calls offer input-check boundaries but retain the syscall stall
+limitation. No quiet-box run, measurement rerun or noisy timing gate verdict.
diff --git a/docs/worklog/2026-10-10.md b/docs/worklog/2026-10-10.md
new file mode 100644
index 0000000000000000000000000000000000000000..628a749e2a9c5a0a8b67c705b877a6de50df1b6f
--- /dev/null
+++ b/docs/worklog/2026-10-10.md
@@ -0,0 +1,14 @@
+# Worklog — 2026-10-10
+
+## edit-4w1.54 session 8
+
+Continued the rebased journal WIP within src/journal and its unit/fuzz/bench
+scope. Added caller-driven INSERT steps, complete durable-prefix byte stats,
+PAD-aware admission credit and blocked-bulk power-image coverage. Preserved
+the existing sync-poisoning, retained metadata/mailbox, overlapping saves,
+session-oracle, PAD and long-path fixes. Red/green output, measured paired
+variants and verification are recorded in
+[the worker report](../worker-reports/edit-4w1.54-s8.md); design choices are in
+[the decision](../decisions/edit-4w1.54.md). §§4,7,8 remain unresolved at full
+contract level and require work/editor/save integration outside this scope.
+No git writes, new globals, typing malloc or real-display windows.
diff --git a/fuzz/journal_fuzz.c b/fuzz/journal_fuzz.c
index fa13ae7941c379323ad1e06547c5b714a940401e..4cdaa430250545698d36da6ff99dff0b36a692a5
--- a/fuzz/journal_fuzz.c
+++ b/fuzz/journal_fuzz.c
@@ -149,11 +149,49 @@
     EDIT_ASSERT(journal_replay_file(path,apply,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected));
     work_pool_shutdown(&pool); unlink(path);
 }
+typedef struct slice_model { const uint8_t *bytes; size_t size, copied; } slice_model;
+static int apply_slice(void *ctx, const journal_record *r)
+{
+    slice_model *m=ctx;
+    if(r->type!=JOURNAL_INSERT || r->buffer_id!=41 || r->size<8) return 1;
+    size_t n=r->size-8;
+    if(read64(r->data)!=m->copied || n>m->size-m->copied ||
+       memcmp(r->data+8,m->bytes+m->copied,n)) return 1;
+    m->copied+=n; return 0;
+}
+static void fuzz_slices(const uint8_t *data, size_t size)
+{
+    if(size<3 || (data[0]&15u)!=3u) return;
+    uint8_t bytes[65536], wire[73728];
+    size_t length=1+(size_t)data[1]*256+data[2];
+    for(size_t i=0;i<length;i++) bytes[i]=data[i%size];
+    char path[]="/tmp/journal-fuzz-slices-XXXXXX"; int fd=mkstemp(path); if(fd<0) return; close(fd);
+    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return; }
+    journal *j; if(journal_open(&j,path,&pool,NULL)) { work_pool_shutdown(&pool); unlink(path); return; }
+    size_t progress=0;
+    while(progress<length) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,41,0,bytes,length,&progress);
+        EDIT_ASSERT(rc==(progress<length?JOURNAL_BUSY:JOURNAL_OK));
+        EDIT_ASSERT(progress>=before && progress-before<=JOURNAL_INSERT_SLICE_BYTES);
+        EDIT_ASSERT(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+        if(progress==before) { EDIT_ASSERT(journal_flush(j)==0); continue; }
+        fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
+        ssize_t n=read(fd,wire,sizeof wire); close(fd); EDIT_ASSERT(n>=0);
+        slice_model m={bytes,progress,0}; journal_replay_result rr;
+        EDIT_ASSERT(journal_replay_bytes(wire,(size_t)n,apply_slice,&m,&rr)==0 && !rr.corrupt && m.copied==progress);
+        /* A caller can abandon the unaccepted tail after any protected step. */
+        if((data[0]&128u) && progress>=length/2) break;
+    }
+    EDIT_ASSERT(journal_flush(j)==0 && journal_get_stats(j).unprotected_bytes==0);
+    journal_close(j); work_pool_shutdown(&pool); unlink(path);
+}
 int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
 {
     no_visit(data,size);
     structured_bytes(data,size);
     fuzz_sessions(data,size);
+    fuzz_slices(data,size);
     if(!size || (data[0]&31u)!=0) return 0;
     char path[]="/tmp/journal-fuzz-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 0; close(fd);
     work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return 0; }
diff --git a/src/journal/journal.c b/src/journal/journal.c
index f38f244a02916e3f7c5006bae620532cb8d540b9..1568a7e383cac64cbd26b371f2ebbcc5bb539081
--- a/src/journal/journal.c
+++ b/src/journal/journal.c
@@ -305,7 +305,17 @@
     while(pos+32<=upto) { size_t n=u32(b->bytes+pos+4); if(n>upto-pos) break; if(u32(b->bytes+pos+8)!=PAD) seq=u64(b->bytes+pos+16); pos+=n; }
     return seq;
 }
-static int sync_disk(journal *j, uint64_t sequence)
+static size_t prefix_bytes(const journal_batch *b, size_t upto)
+{
+    size_t pos=0;
+    while(pos+32<=upto) {
+        size_t n=u32(b->bytes+pos+4);
+        if(n>upto-pos) break;
+        pos+=n;
+    }
+    return pos;
+}
+static int sync_disk(journal *j, uint64_t sequence, uint64_t complete_bytes)
 {
     journal_disk *d=&j->disk;
     int rc=io_sync(&j->io,d->fd,false);
@@ -315,13 +325,15 @@
     if(d->unsynced && interval>s->max_sync_interval_ns) s->max_sync_interval_ns=interval;
     s->last_sync_ns=now; s->last_sync_bytes=d->unsynced;
     if(d->unsynced>s->max_sync_bytes) s->max_sync_bytes=d->unsynced;
-    s->syncs++; s->durable_sequence=sequence; d->unsynced=0; return 0;
+    s->syncs++; s->durable_sequence=sequence;
+    if(complete_bytes>s->durable_bytes) s->durable_bytes=complete_bytes;
+    d->unsynced=0; return 0;
 }
 static void worker(work_ctx *ctx)
 {
     journal *j=ctx->arg; journal_batch *b=&j->batches[j->active_index]; journal_disk *d=&j->disk;
     size_t pos=b->progress; int rc=0; uint64_t initial=d->written_sequence;
-    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence);
+    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence,d->stats.file_bytes);
     while(!rc && pos<b->sealed && !work_should_stop(ctx)) {
         size_t n=b->sealed-pos; uint64_t room=j->sync_bytes-d->unsynced; if(n>room) n=(size_t)room;
         size_t done=0;
@@ -332,9 +344,9 @@
         pos+=done; b->progress=pos; d->unsynced+=done; d->written_sequence=prefix_sequence(b,pos,initial);
         d->stats.written_sequence=d->written_sequence; d->stats.file_bytes=b->offset+pos;
         if(rc) break;
-        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence); if(rc) break; }
+        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos)); if(rc) break; }
     }
-    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence);
+    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos));
     d->stats.error=rc;
     journal_completion *completion=(journal_completion *)(void *)(b->bytes+j->capacity);
     completion->stats=d->stats;
@@ -428,6 +440,7 @@
     if(j->directory_pending) {
         int rc=io_sync(&j->io,j->directory_fd,true); if(rc) return rc;
         j->directory_pending=false; j->stats.durable_sequence=j->disk.stats.durable_sequence;
+        j->stats.durable_bytes=j->disk.stats.durable_bytes;
     }
     j->stats.error=j->append_error; j->disk.stats.error=0;
     j->append_failed=false; j->stats.append_errno=0;
@@ -501,6 +514,33 @@
 }
 int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len)
 { uint8_t data[16]; if(off>UINT64_MAX-len) return JOURNAL_INVALID; put64(data,off); put64(data+8,len); return journal_append(j,JOURNAL_DELETE,id,data,16); }
+int journal_insert_step(journal *j, uint64_t id, uint64_t off,
+                        const uint8_t *bytes, size_t size, size_t *progress)
+{
+    if(!j || !progress || *progress>size || (!bytes && size) ||
+       off>UINT64_MAX-size) return JOURNAL_INVALID;
+    if(*progress==size) return 0;
+    size_t n=size-*progress;
+    if(n>JOURNAL_INSERT_SLICE_BYTES) n=JOURNAL_INSERT_SLICE_BYTES;
+    if(n>j->capacity-72) n=j->capacity-72;
+    if(n>j->sync_bytes-72) n=(size_t)(j->sync_bytes-72);
+    uint8_t *record;
+    int preflight=reserve_record(j,n+8,&record); if(preflight) return preflight;
+    /* Reserve the eventual PAD too. Credit is returned only by a received
+     * durability fence at a COMPLETE record boundary, never by worker-accounted
+     * bytes or a partial CRC-protected record. This remains conservative when
+     * the kernel sync also covers newer UI writes in the other batch. */
+    journal_batch *b=&j->batches[j->current];
+    uint64_t end=j->reserved+sealed_size(b->used+n+40);
+    if(end>j->stats.durable_bytes && end-j->stats.durable_bytes>j->sync_bytes)
+        return JOURNAL_BUSY;
+    uint64_t accepted=j->stats.accepted_sequence;
+    int rc=journal_insert(j,id,off+*progress,bytes+*progress,n);
+    /* IO after encoding still owns this record in the fixed queue. Sticky IO,
+     * FULL or a prior transport gap accept nothing; never skip those bytes. */
+    if(j->stats.accepted_sequence!=accepted) *progress+=n;
+    return rc?rc:(*progress<size?JOURNAL_BUSY:JOURNAL_OK);
+}
 static int base_data(const journal_base *base, uint8_t *data, size_t *size)
 {
     if(!base || !base->path || strlen(base->path)>4096 || base->prefix_len>4096 || base->prefix_len>base->size) return JOURNAL_INVALID;
@@ -586,7 +626,9 @@
 journal_stats journal_get_stats(const journal *j)
 {
     if(!j) return (journal_stats){.error=JOURNAL_INVALID};
-    journal_stats s=j->stats; s.pending_bytes=j->batches[0].used+j->batches[1].used; return s;
+    journal_stats s=j->stats; s.pending_bytes=j->batches[0].used+j->batches[1].used;
+    s.unprotected_bytes=s.file_bytes>s.durable_bytes?s.file_bytes-s.durable_bytes:0;
+    return s;
 }
 void journal_set_message_handler(journal *j, void (*handler)(const work_msg *, void *), void *ctx)
 {
@@ -694,7 +736,7 @@
         j->limit=next->limit; j->disk.stats.checkpoint_bytes=checkpoint_bytes; j->stats.checkpoint_bytes=checkpoint_bytes;
         j->directory_pending=true;
         if(rc || io_sync(&j->io,j->directory_fd,true)) {
-            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0;
+            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0; j->stats.durable_bytes=0;
         } else j->directory_pending=false;
         close(oldfd);
     }
diff --git a/src/journal/journal.h b/src/journal/journal.h
index 06b7e1b710d63afefb8bb6119a2701fd24339d4b..40250a6b9afa76509d026b6e61df6dfceb04155e
--- a/src/journal/journal.h
+++ b/src/journal/journal.h
@@ -14,6 +14,7 @@
 #define JOURNAL_DEFAULT_FILE_BYTES 67108864ull
 #define JOURNAL_DEFAULT_SYNC_BYTES 65536ull
 #define JOURNAL_DEFAULT_SYNC_NS 1000000000ull
+#define JOURNAL_INSERT_SLICE_BYTES 16384u
 
 typedef enum journal_error {
     JOURNAL_OK = 0, JOURNAL_IO, JOURNAL_INVALID, JOURNAL_FULL,
@@ -65,6 +66,8 @@
     uint64_t file_limit_bytes, checkpoint_bytes, queue_bytes;
     uint64_t pending_bytes;    /* bytes retained in the two UI/worker batches */
     int append_errno;          /* sticky UI-write errno; short/zero write => EIO */
+    uint64_t durable_bytes;    /* complete wire prefix covered by data + name barriers */
+    uint64_t unprotected_bytes;/* actual page-cache file extent beyond that prefix */
 } journal_stats;
 typedef struct journal journal;
 /* Optional per-instance syscall seam; callbacks have POSIX return/errno semantics.
@@ -182,6 +185,26 @@
  * Existing backlog can still suspend recovery; caller surfaces FULL. */
 int journal_insert(journal *j, uint64_t id, uint64_t off,
                    const uint8_t *bytes, size_t size);
+/* Caller-driven large-insert continuation. Initialize *progress to zero; keep
+ * bytes/size/id/off stable and immutable until finished. Each call encodes and
+ * attempts ONE record of at most JOURNAL_INSERT_SLICE_BYTES payload bytes (or
+ * the smaller configured batch capacity). BUSY with advanced progress means
+ * a protected prefix and more work: check input before calling again. BUSY
+ * without progress means a transport gap or insufficient durability credit;
+ * force pump/receive outside the typing path, then resume. A step reserves
+ * its eventual PAD and refuses to exceed configured sync_bytes beyond the last
+ * received COMPLETE durable record boundary. This byte bound requires all
+ * intervening append calls to use this interface; legacy appends can exceed it.
+ * Worker/device delays still prevent a wall-clock power-loss bound.
+ * OK means the entire requested prefix is in page cache. On IO, progress also
+ * includes this call's RAM-retained record; suspend completion and drain/retry
+ * off path, never reappend that prefix. FULL accepts none of this step; earlier
+ * successful steps remain accepted. The caller owns mutation/checkpoint order
+ * and publishes full-edit completion only after all steps are protected.
+ * Zero size / already complete progress is a no-op. No allocation, lock,
+ * wait, sync or submission. Bytes/attempts bound CPU work, NOT syscall wall time. */
+int journal_insert_step(journal *j, uint64_t id, uint64_t off,
+                        const uint8_t *bytes, size_t size, size_t *progress);
 int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len);
 int journal_set_base(journal *j, uint64_t id, const journal_base *base);
 int journal_set_view(journal *j, uint64_t id, const journal_view *view);
diff --git a/tests/journal_test.c b/tests/journal_test.c
index 5b042e95f94774acdb4d279c88f34318eec8c87a..f6a9baedf3362512b538deffd3e3caee4993ed1e
--- a/tests/journal_test.c
+++ b/tests/journal_test.c
@@ -340,6 +340,7 @@
     CHECK(journal_insert(j,1,1,big,sizeof big)==JOURNAL_IO); d->append_limit=0; d->fail_write=1;
     CHECK(journal_flush(j)==JOURNAL_IO);
     CHECK(journal_get_stats(j).durable_sequence==1);
+    CHECK(journal_get_stats(j).durable_bytes==41 && journal_get_stats(j).unprotected_bytes==65536-41);
     model m={0}; journal_replay_result rr; CHECK(durable_replay(d,&m,&rr)==0);
     CHECK(rr.corrupt && rr.last_sequence==1 && m.len==1 && m.text[0]=='a');
     CHECK(journal_retry(j)==0); CHECK(journal_flush(j)==0);
@@ -366,6 +367,7 @@
         if(phase>=1) {
             m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==3 && !memcmp(m.text,"new",3));
             CHECK(journal_get_stats(j).error==JOURNAL_IO && journal_get_stats(j).durable_sequence==0);
+            CHECK(journal_get_stats(j).durable_bytes==0 && journal_get_stats(j).unprotected_bytes==journal_get_stats(j).file_bytes);
             CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==JOURNAL_IO);
         }
         if(phase==2) {
@@ -376,6 +378,7 @@
         if(phase>=1) {
             d->fail_dir=0; CHECK(journal_retry(j)==0);
             CHECK(journal_get_stats(j).durable_sequence==1);
+            CHECK(journal_get_stats(j).durable_bytes==journal_get_stats(j).file_bytes && journal_get_stats(j).unprotected_bytes==0);
             CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==0 && journal_flush(j)==0);
             m=(model){0}; CHECK(durable_replay(d,&m,&rr)==0 && m.len==4 && !memcmp(m.text,"new!",4));
         }
@@ -1625,6 +1628,74 @@
         (unsigned long long)samples[4]);
     CHECK(samples[4]<=500000); return 0;
 }
+typedef struct slice_replay {
+    const uint8_t *expected;
+    size_t size, copied, records;
+} slice_replay;
+static int restore_slice(void *ctx, const journal_record *r)
+{
+    slice_replay *s=ctx;
+    if(r->type!=JOURNAL_INSERT || r->buffer_id!=17 || r->size<8) return 1;
+    size_t n=r->size-8;
+    if(get64(r->data)!=s->copied || n>s->size-s->copied ||
+       memcmp(r->data+8,s->expected+s->copied,n)) return 1;
+    s->copied+=n; s->records++; return 0;
+}
+static int insert_steps_test(void)
+{
+    char path[]="/tmp/journal-insert-steps-XXXXXX"; CHECK(temp(path)>=0);
+    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
+    append_fault f={.owner=pthread_self()}; journal_io io={.ctx=&f,.append_write=append_attempt};
+    journal *j; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
+    size_t size=1000000, progress=0; uint8_t *bytes=malloc(size); CHECK(bytes);
+    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(i*17u);
+    unsigned steps=0;
+    while(progress<size) {
+        size_t before=progress; unsigned attempts=f.attempts;
+        edit_malloc_guard_begin();
+        int rc=journal_insert_step(j,17,0,bytes,size,&progress);
+        size_t allocations=edit_malloc_guard_end();
+        if(progress==before) {
+            CHECK(rc==JOURNAL_BUSY && allocations==0 && f.attempts==attempts);
+            CHECK(journal_flush(j)==0); continue;
+        }
+        CHECK(progress>before && progress-before<=JOURNAL_INSERT_SLICE_BYTES);
+        CHECK(rc==(progress<size?JOURNAL_BUSY:JOURNAL_OK) && allocations==0 && f.attempts==attempts+1);
+        /* Independent page-cache image, before any pump: each completed slice
+         * already restores the scripted prefix. No reliance on accepted stats. */
+        int fd=open(path,O_RDONLY); CHECK(fd>=0); struct stat sb; CHECK(fstat(fd,&sb)==0);
+        uint8_t *wire=malloc((size_t)sb.st_size); CHECK(wire);
+        CHECK(read(fd,wire,(size_t)sb.st_size)==sb.st_size); close(fd);
+        slice_replay restored={bytes,progress,0,0}; journal_replay_result rr;
+        CHECK(journal_replay_bytes(wire,(size_t)sb.st_size,restore_slice,&restored,&rr)==0 && !rr.corrupt && restored.copied==progress);
+        CHECK(restored.records==++steps && rr.records==steps); free(wire);
+    }
+    CHECK(!f.wrong_thread && !f.syncs && !f.worker_writes);
+    unsigned attempts=f.attempts;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==0 && f.attempts==attempts);
+    size_t invalid=size+1;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&invalid)==JOURNAL_INVALID && invalid==size+1);
+    CHECK(journal_insert_step(j,17,UINT64_MAX,bytes,size,&progress)==JOURNAL_INVALID);
+    CHECK(journal_insert_step(j,17,0,bytes,size,NULL)==JOURNAL_INVALID);
+    CHECK(journal_flush(j)==0); journal_close(j);
+    CHECK(truncate(path,0)==0);
+    journal_options opt={.batch_bytes=8192}; f=(append_fault){.owner=pthread_self(),.error=EAGAIN};
+    CHECK(journal_open_with_io(&j,path,&pool,&opt,&io)==0); progress=0;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_IO && progress==8192-72);
+    CHECK(f.attempts==1 && journal_get_stats(j).accepted_sequence==1);
+    size_t retained=progress;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_IO && progress==retained && f.attempts==1);
+    f.error=0; CHECK(journal_retry(j)==0 && journal_flush(j)==0);
+    journal_close(j); CHECK(truncate(path,0)==0);
+    opt.sync_bytes=4096; opt.max_file_bytes=4096; f=(append_fault){.owner=pthread_self()};
+    CHECK(journal_open_with_io(&j,path,&pool,&opt,&io)==0); progress=0;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_BUSY && progress==4096-72);
+    CHECK(journal_flush(j)==0); retained=progress;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_FULL && progress==retained);
+    journal_close(j); work_pool_shutdown(&pool); free(bytes); unlink(path);
+    puts("journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes");
+    return 0;
+}
 static int bulk_loss_repro(void)
 {
     char path[]="/tmp/journal-bulk-loss-XXXXXX"; CHECK(temp(path)>=0);
@@ -1646,6 +1717,56 @@
     CHECK(unprotected<=JOURNAL_DEFAULT_SYNC_BYTES);
     return 0;
 }
+static int insert_credit_test(void)
+{
+    char path[]="/tmp/journal-insert-credit-XXXXXX"; CHECK(temp(path)>=0);
+    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
+    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path; journal_io io=fault_io(d);
+    journal *j; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
+    work_handle blocker=work_submit(&pool,(work_job){blocked,NULL,0,WORK_BULK}); CHECK(blocker.epoch);
+    uint8_t bytes[100000]; for(size_t i=0;i<sizeof bytes;i++) bytes[i]=(uint8_t)(i*19u);
+    size_t progress=0;
+    for(unsigned i=0;i<16 && progress<sizeof bytes;i++) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,17,0,bytes,sizeof bytes,&progress);
+        CHECK(rc==JOURNAL_BUSY || rc==0);
+        journal_stats st=journal_get_stats(j);
+        struct stat sb; CHECK(stat(path,&sb)==0);
+        CHECK(st.unprotected_bytes==(uint64_t)sb.st_size && st.durable_bytes==0);
+        if(progress==before) break;
+    }
+    CHECK(progress>0 && progress<sizeof bytes);
+    CHECK(journal_pump(j,1,true)==0);
+    size_t stopped=progress;
+    edit_malloc_guard_begin();
+    for(unsigned i=0;i<1000;i++) {
+        CHECK(journal_insert_step(j,17,0,bytes,sizeof bytes,&progress)==JOURNAL_BUSY && progress==stopped);
+        CHECK(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+    }
+    CHECK(edit_malloc_guard_end()==0);
+    journal_replay_result rr; CHECK(journal_replay_bytes(NULL,0,NULL,NULL,&rr)==0);
+    disk_image *image=NULL;
+    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) image=&d->image[i];
+    CHECK(!image || image->size==0); /* power image remains empty behind bulk */
+    work_cancel(&pool,blocker); CHECK(journal_flush(j)==0);
+    journal_stats synced=journal_get_stats(j);
+    CHECK(synced.unprotected_bytes==0 && synced.durable_bytes==synced.file_bytes);
+    while(progress<sizeof bytes) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,17,0,bytes,sizeof bytes,&progress);
+        CHECK(rc==0 || rc==JOURNAL_BUSY);
+        CHECK(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+        if(progress==before) CHECK(journal_flush(j)==0);
+    }
+    CHECK(journal_flush(j)==0);
+    image=NULL;
+    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) image=&d->image[i];
+    CHECK(image);
+    slice_replay restored={bytes,sizeof bytes,0,0};
+    CHECK(journal_replay_bytes(image->bytes,image->size,restore_slice,&restored,&rr)==0 && !rr.corrupt && restored.copied==sizeof bytes);
+    journal_close(j); work_pool_shutdown(&pool); free(d); unlink(path);
+    puts("journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence"); return 0;
+}
 static ssize_t slow_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
 {
     (void)ctx; struct timespec stalled={0,20000000}; nanosleep(&stalled,NULL);
@@ -1688,6 +1809,8 @@
     if(argc==2) {
         if(!strcmp(argv[1],"--crc-wire")) return crc_wire_test();
         if(!strcmp(argv[1],"--crc-cpu")) return crc_cpu_test();
+        if(!strcmp(argv[1],"--insert-steps")) return insert_steps_test();
+        if(!strcmp(argv[1],"--insert-credit")) return insert_credit_test();
         if(!strcmp(argv[1],"--bulk-loss-repro")) return bulk_loss_repro();
         if(!strcmp(argv[1],"--append-bound-repro")) return append_bound_repro();
         if(!strcmp(argv[1],"--save-ack-repro")) return save_ack_repro();
@@ -1727,7 +1850,7 @@
         if(!strcmp(argv[1],"--retained-copy")) return retained_copy_test();
         return 2;
     }
-    CHECK(crc_wire_test()==0 && overlapping_saves_test()==0 && session_values_test()==0);
+    CHECK(insert_credit_test()==0 && insert_steps_test()==0 && crc_wire_test()==0 && overlapping_saves_test()==0 && session_values_test()==0);
     CHECK(writeback_loss_test()==0 && retention_metadata_test()==0 && retention_channel_test()==0 && pad_busy_test()==0 && long_base_retention_test()==0);
     CHECK(page_cache_test()==0 && append_failure_test()==0 && append_gap_test()==0);
     CHECK(open_options_test()==0 && rotate_path_test()==0 && directory_fd_test()==0 && exact_prefix_test()==0 && full_test()==0 && paste_test(true)==0 && paste_test(false)==0 && untitled_test()==0 &&

apply patch
patch: completed
/home/tobias/Projects/sublimite/.wt/edit-4w1.54/docs/worker-reports/edit-4w1.54-s8.md
/home/tobias/Projects/sublimite/.wt/edit-4w1.54/src/journal/STATUS.md
diff --git a/HANDOFF.md b/HANDOFF.md
index e73712d60fed64024feefc92559008554676a815..6f3fd424d5456f56938acd72350ac55466d7881f
--- a/HANDOFF.md
+++ b/HANDOFF.md
@@ -18,7 +18,7 @@
 | edit-457.16 P4.I wiring (chokepoint) | wt/edit-457.16 (301a7d1 WIP) | substantively done per its rollout (suites green, 0 allocs over 10k keys with 100 tabs + IPC); killed while polishing; no report | rebase (known conflict src/editor/open.c, resolved in wt/edit-457.16-snap 90919f7), verify, Codex high finisher "verify, fix red, write report, no new scope", land |
 | edit-zzj.13 editor review fixes (+P1.9-2 §2/3/5, WM_DELETE_WINDOW → exit within a frame) | wt/edit-zzj.13 (1efc6e1 WIP, on the 457.16 snapshot) | started 17:25, early | continue after 457.16 lands |
 | edit-zzj.15 EGL default backend, raster fallback | wt/edit-zzj.15 (19b4672 WIP, on the snapshot) | started 17:25, early | continue after 457.16 |
-| edit-4w1.54 journal re-review fixes | wt/edit-4w1.54 (e251eba WIP) | ~1 h in | continue |
+| edit-4w1.54 journal re-review fixes | wt/edit-4w1.54 (session 8 continuation) | §§1,6,9–14 fixed; bounded INSERT steps/byte credit added; §§4,7,8 remain integration work | see docs/worker-reports/edit-4w1.54-s8.md; coordinator verification/commit |
 | edit-4w1.47 lineidx UI slices | wt/edit-4w1.47 (03688a4 WIP) | ~45 min in | continue |
 | edit-4w1.43 file keep/check/save semantics | wt/edit-4w1.43 (57b42ad WIP) | ~15 min in | continue |
 | edit-457.19 font review fixes | wt/edit-457.19 (543bbe7 WIP) | ~45 min in | continue |
diff --git a/bench/journal_bench.c b/bench/journal_bench.c
index f62481689518d81d1370185618d0538d2f763d38..1b9c08112324cb3b857603ce320cf2164f8af72b
--- a/bench/journal_bench.c
+++ b/bench/journal_bench.c
@@ -230,6 +230,54 @@
         syncs,(double)(worker_cpu_ns(clk)-before)/(1000.0*syncs),(double)parked_cpu/(1000.0*syncs),evidence(tag,sizeof tag));
     journal_close(j); unlink(path); return 0;
 }
+/* TRACK comparison: alternate adjacent variants on the same loaded box.
+ * Sum enqueue calls separately from the continuation's off-path fence waits;
+ * neither endpoint includes editor mutation/layout/submit. */
+static int insert_variants(work_pool *pool)
+{
+    stamp("insert_variants"); char tag[128];
+    size_t size=1000000; uint8_t *bytes=malloc(size); REQUIRE(bytes);
+    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(i*17u);
+    uint64_t totals[2][16], calls[2048], completion[2][16];
+    bench_samples total[2], complete[2], steps;
+    for(unsigned v=0;v<2;v++) {
+        bench_samples_init(&total[v],totals[v],16);
+        bench_samples_init(&complete[v],completion[v],16);
+    }
+    bench_samples_init(&steps,calls,2048);
+    for(unsigned pair=0;pair<16;pair++) for(unsigned order=0;order<2;order++) {
+        unsigned variant=(pair+order)%2;
+        char path[]="/tmp/journal-variant-bench-XXXXXX"; REQUIRE(temporary(path)>=0);
+        journal *j; REQUIRE(journal_open(&j,path,pool,NULL)==0);
+        size_t progress=0; uint64_t cpu_calls=0, start=bench_now_ns();
+        if(!variant) {
+            int rc; edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
+            rc=journal_insert(j,1,0,bytes,size); cpu_calls=bench_now_ns()-before;
+            REQUIRE(edit_malloc_guard_end()==0 && rc==0); progress=size;
+        } else while(progress<size) {
+            size_t before_progress=progress;
+            edit_malloc_guard_begin(); uint64_t before=bench_now_ns();
+            int rc=journal_insert_step(j,1,0,bytes,size,&progress);
+            uint64_t elapsed=bench_now_ns()-before;
+            REQUIRE(edit_malloc_guard_end()==0 && (rc==0 || rc==JOURNAL_BUSY));
+            (void)bench_add(&steps,elapsed); cpu_calls+=elapsed;
+            if(progress==before_progress) REQUIRE(journal_flush(j)==0);
+        }
+        (void)bench_add(&total[variant],cpu_calls); (void)bench_add(&complete[variant],bench_now_ns()-start);
+        REQUIRE(journal_flush(j)==0); journal_close(j);
+        piece_allocator a=piece_default_allocator(); paste_replay replayed={bytes,size,0,piece_create(&a)};
+        REQUIRE(replayed.tree); journal_replay_result rr;
+        REQUIRE(journal_replay_file(path,restore_paste,&replayed,&rr)==0 && !rr.corrupt && replayed.copied==size && piece_len(replayed.tree)==size);
+        piece_destroy(replayed.tree); unlink(path);
+    }
+    for(unsigned v=0;v<2;v++) printf("TRACK journal_insert_variant variant=%s pairs=16 bytes=%zu enqueue_sum_p50_ms=%.3f enqueue_sum_p99_ms=%.3f protected_completion_p50_ms=%.3f protected_completion_p99_ms=%.3f content_verified=1 allocations=0 (M)%s\n",
+        v?"continuation":"one_shot",size,(double)bench_p50(&total[v])/1e6,(double)bench_p99(&total[v])/1e6,
+        (double)bench_p50(&complete[v])/1e6,(double)bench_p99(&complete[v])/1e6,evidence(tag,sizeof tag));
+    printf("TRACK journal_insert_step calls=%zu p50_us=%.3f p99_us=%.3f input_check_boundaries=1 byte_credit_enforced=1 wall_time_unbounded=1 (M)%s\n",
+        steps.n,(double)bench_p50(&steps)/1e3,(double)bench_p99(&steps)/1e3,evidence(tag,sizeof tag));
+    REQUIRE(!steps.dropped && !total[0].dropped && !total[1].dropped);
+    free(bytes); return 0;
+}
 typedef struct exhausted_prefix { uint64_t bytes, records; } exhausted_prefix;
 static int restore_exhausted(void *ctx, const journal_record *r)
 {
@@ -306,9 +354,11 @@
 int main(int argc, char **argv)
 {
     if(argc==2 && !strcmp(argv[1],"--gate-self-check")) return gate_self_check();
-    bool track=argc==2 && !strcmp(argv[1],"--track"); unsigned misses=0;
+    bool variants=argc==2 && !strcmp(argv[1],"--insert-variants");
+    bool track=variants || (argc==2 && !strcmp(argv[1],"--track")); unsigned misses=0;
     work_pool pool; REQUIRE(work_pool_init(&pool,1,0)==0); int fail=0;
-    if(argc==2 && !strcmp(argv[1],"--worker")) fail=worker_cpu(&pool);
+    if(variants) fail=insert_variants(&pool);
+    else if(argc==2 && !strcmp(argv[1],"--worker")) fail=worker_cpu(&pool);
     else if(argc==2 && !strcmp(argv[1],"--fixtures")) { fail=paste(&pool,1,&misses); if(!fail) fail=session(&pool,1,false,&misses); }
     else if(argc!=1 && !track) fail=2;
     else { fail=worker_cpu(&pool); if(!fail) fail=paste(&pool,100,&misses); if(!fail) fail=session(&pool,1,true,&misses); if(!fail) fail=session(&pool,1024,true,&misses); if(!fail) fail=exhaustion(&pool); if(!fail) fail=idle(&pool,false); if(!fail) fail=idle(&pool,true); }
diff --git a/docs/decisions/edit-4w1.54.md b/docs/decisions/edit-4w1.54.md
new file mode 100644
index 0000000000000000000000000000000000000000..890f3cbb093cc283916977408a70d013f88c2ba1
--- /dev/null
+++ b/docs/decisions/edit-4w1.54.md
@@ -0,0 +1,87 @@
+# edit-4w1.54 — session 8 journal continuation decisions
+
+Scope is P1.9-2 §§1,4,6–14. Earlier implementation and red/green evidence
+remain in [P1.9h.md](P1.9h.md). This continuation preserves those fixes and
+adds journal-side prerequisites for §§4 and 7. It does not close §§4,7,8.
+Editor/main findings §§2,3,5 belong to edit-zzj.13 and were not changed.
+
+## Caller-driven protected INSERT steps
+
+`journal_insert_step` retains the existing synchronous page-cache contract,
+but encodes/copies/checksums and attempts only one record per call. The payload
+ceiling is 16 KiB (G), reduced for a smaller configured batch or sync budget.
+The caller keeps the immutable input and a byte progress cursor; no new
+allocation, lock, worker submission or global state is needed. Each successful
+step protects its prefix before returning. BUSY with advanced progress means
+the caller can check input and resume; BUSY without progress requires a
+durability fence or transport-gap repair. The caller may abandon the unaccepted
+tail. This is deliberately a sequence of protected prefix mutations rather than
+an atomic reservation of the entire paste. The legacy `journal_insert` retains
+its existing preflight/whole-input IO-retention contract.
+
+On IO the cursor includes the RAM-retained record, even after a short write.
+Callers suspend completion, drain/retry off path and never append that record
+again. FULL/previous IO/refused admission advance no cursor. Zero length and an
+already complete cursor are no-ops; invalid offsets/cursors are rejected.
+
+This byte/attempt ceiling bounds encoding work between caller input checks.
+A Linux regular-file pwrite can still stall. The editor must use the new API
+and apply/publish the corresponding protected prefix in its continuation;
+the module alone cannot assert an unconditional UI wall-time bound.
+
+## Complete durable-prefix credit
+
+Stats now expose `durable_bytes` and `unprotected_bytes`. The former advances
+only after a successful data fence through COMPLETE records, including PAD;
+the latter is the actual UI-written file extent beyond that conservative
+prefix. Newer UI writes are preserved when older worker stats are received.
+A sync ending inside a large CRC-protected record releases no credit for that
+record's partial prefix. A failed checkpoint-name barrier exposes no durable
+bytes until directory retry succeeds.
+
+Each INSERT step reserves its eventual PAD and returns BUSY before admitting
+a record that would exceed configured sync_bytes beyond the received durable
+prefix. This remains conservative if a kernel fence also covers concurrent
+UI writes in the other batch. The delayed-bulk durable-image test shows no
+admission beyond the default 64 KiB (G) and successful continuation after a
+fence. Credit also works at the smallest 4 KiB (G) sync budget, and disk
+exhaustion still reports sticky FULL.
+
+This is an admission guarantee for the continuation API. Legacy INSERT and
+other append APIs retain their current compatibility contract and can exceed
+that volume. A complete default guarantee requires the editor to use bounded
+protected continuations for every mutation and work to reserve independent
+durability execution. A stalled device/worker still prevents a hard elapsed
+time guarantee. The PRD maximum has not been weakened to a cadence target.
+
+## Save preparation remains an integration obligation
+
+The current standalone savectl performs legacy prepare/finish on a worker,
+using an exclusively leased journal and a distinct transport pool. Its caller
+defers journal edits during the lease, and the current editor is not wired to
+that controller. Moving only an entry point would therefore leave later edits
+unprotected or associate the wrong snapshot with the saved cutoff. No journal
+lease/async API has been added without that integration. §8 remains open:
+capture the immutable snapshot at enqueue, keep later protected deltas, perform
+retention/checkpoint IO off the UI, and adopt through work mailboxes before
+authorizing replacement. Existing retention mailbox/full-metadata semantics
+remain enforced by their regression tests.
+
+## Evidence
+
+All session 8 runs use DISPLAY=:99 and EDIT_DISPLAY=:99. Power was Not charging
+[AC]. Measurements are from the loaded shared box; no timing is a gate verdict.
+The required worker report contains exact red/green output and final checks:
+[edit-4w1.54-s8.md](../worker-reports/edit-4w1.54-s8.md).
+
+The one paired benchmark alternated adjacent variants, reversing the initial
+variant each pair, with independent exact-content replay and active malloc
+checks. For a 1 MB (G fixture) paste, one-shot enqueue p50/p99 was
+0.864/1.063 ms (M)[AC], and continuation enqueue-call sums were
+1.391/1.935 ms (M)[AC]. Individual continuation calls p50/p99 were
+17.488/56.857 us (M)[AC]. Continuation protected completion, including the
+fixture's blocking off-path fence waits, was 34.823/40.195 ms (M)[AC]. Launch
+load1 was 15.86 (M)[AC]; row load1 was 15.56 (M)[AC]. The test fixture has
+input-check opportunities between calls; it does not run an editor event loop
+or establish G1/G9. The cost of durability credit must be accounted for in that
+integration. No repeat timing run or quiet-box wait was performed.
diff --git a/docs/worker-reports/edit-4w1.54-s8.md b/docs/worker-reports/edit-4w1.54-s8.md
new file mode 100644
index 0000000000000000000000000000000000000000..6e87adffab968c853d9b2e55a1cfa3965b7c0ebe
--- /dev/null
+++ b/docs/worker-reports/edit-4w1.54-s8.md
@@ -0,0 +1,167 @@
+# edit-4w1.54 session 8 worker report
+
+The bead remains incomplete: review §§4,7,8 still require work/editor/save
+integration. Journal fixes for §§1,6,9–14 are retained and verified. This session
+adds bounded protected INSERT continuations, complete durable-prefix stats,
+PAD-aware byte admission, a delayed-worker durable-image oracle, fuzz operations
+and a paired benchmark. No editor/main/work/file changes, git writes, new
+globals or typing-path allocation were introduced.
+
+Read order: CLAUDE.md, the supplied bead, P1.9-2 named sections, journal STATUS;
+then HANDOFF, PLAN workflow and binding PRD/performance sections. Inspected the
+requested read-only log and main diff. Starting WIP commits were `1f8f4d4` and
+`b62329b`; main diff covered journal source/header, unit/kill/fuzz/bench and docs.
+Decisions: [edit-4w1.54.md](../decisions/edit-4w1.54.md), with earlier evidence
+in [P1.9h.md](../decisions/P1.9h.md).
+
+## Per finding
+
+| Review | Result and remaining work |
+|---|---|
+| §1 BLOCKER | Fixed in WIP: sync/writeback IO poisons the live generation; retry cannot acknowledge it. A complete CURRENT checkpoint in a fresh durably published inode is required. Lost-writeback oracle includes a released unsynced batch. |
+| §4 BLOCKER | Partial: actual UI cache extent and complete durable-prefix stats now replace worker-accounted bytes for new INSERT continuation admission. Delayed-bulk test bounds volume, including PAD, and verifies the durable image after resumption. Legacy appends remain unbounded; independent durability scheduling and the PRD time limit remain unresolved. |
+| §6 MAJOR | Fixed in WIP: full retained-inode metadata fsync seam; empty-base mtime persistence oracle. |
+| §7 MAJOR | Partial: WIP hardware/fallback CRC remains; new continuation limits one copy/CRC/write record per caller slice, validates each page-cache prefix and allocates nothing. UI pwrite wall time and editor protected-completion integration remain unresolved. |
+| §8 MAJOR | Unresolved: synchronous journal preparation remains available; standalone savectl leases it to a worker but defers later journal edits, and editor wiring is outside this bead. Need immutable snapshot enqueue, concurrent protected deltas and mailbox adoption. |
+| §9 MAJOR | Fixed in WIP at module scope: default benchmark fails local percentile/dropped-sample misses; explicit TRACK retains correctness failure; deterministic injected-latency self-check. Complete G1/G9 endpoints require the editor harness. |
+| §10 MAJOR | Fixed in WIP: every VIEW/TABS/WINDOW field has independent unit, power-image, rotation, SIGKILL, fuzz and bench expectations, including CRC-valid semantic mutants. |
+| §11 MAJOR | Fixed in WIP: overlapping tokens preserve both targets, cutoffs, later deltas and retained availability in both finish orders; publication/retirement faults are covered. |
+| §12 MAJOR | Fixed in WIP: immutable retention result crosses routed work mailbox; storage survives receipt, no shared done polling. |
+| §13 MINOR | Fixed in WIP: PAD IO outranks submit BUSY, failed PAD is not retried on the UI. |
+| §14 MINOR | Fixed in WIP: retained bases use the bounded recovery directory/ancestor; maximal valid BASE/journal path coverage. |
+
+§§2,3,5 are explicitly out of scope, owned by edit-zzj.13; no fixes made here.
+This report does not reinterpret PRD loss limits or claim whole-editor gates.
+
+## Red/green evidence
+
+Historical red output below is pasted from the WIP's P1.9h decision record,
+not represented as newly executed baseline runs. Those tests were written and
+run before their respective WIP fixes. All historical measured evidence is
+(M)[AC]; historical loads appear next to each group. New session 8 red runs
+were executed before the corresponding changes.
+
+Historical RED, §1/6/12/13/14, load1=8.54 (M)[AC]:
+
+```text
+journal_test:1089 FAIL journal_retry(j)==JOURNAL_IO
+journal_test:1132 FAIL oracle.full_synced
+journal_test:1158 FAIL journal_save_prepare(j,1,&b,&cp,1,&save)==0 && c.mailbox
+journal_test:1172 FAIL journal_pump(j,1,true)==JOURNAL_IO
+journal_test:1200 FAIL journal_save_prepare(j,1,&b,&cp,1,&save)==0 && save.prepared
+```
+
+Historical RED, §9/10, load1=4.67 (M)[AC], and §11, load1=2.46 (M)[AC]:
+
+```text
+journal_bench:266 FAIL finish_bench(0,false,misses)!=0
+journal_test:1240 FAIL cache_replay(path,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected)
+journal_test:1296 FAIL journal_replay_file(path,check_save_marker,&expected[0],&rr)==0
+journal_test:1333 FAIL overlapping_markers(path,markers)==0
+```
+
+Session 8 RED, §7 continuation, command `build/tests/journal_test --insert-steps`,
+load1=24.63 (M)[AC]. The test was written first; a one-shot adapter made the
+legacy behavior executable under the new interface before implementing slicing:
+
+```text
+journal_test:1655 FAIL progress>before && progress-before<=JOURNAL_INSERT_SLICE_BYTES
+```
+
+Session 8 RED, §4 actual cache accounting, command
+`build/tests/journal_test --insert-credit`, load1=17.83 (M)[AC]:
+
+```text
+journal_test:1721 FAIL st.unprotected_bytes==(uint64_t)sb.st_size && st.durable_bytes==0
+```
+
+After accounting was fixed, the same test remained RED for admission,
+load1=13.52 (M)[AC]:
+
+```text
+journal_test:1724 FAIL progress>0 && progress<sizeof bytes
+```
+
+Session 8 RED for the smallest sync budget, command
+`build/tests/journal_test --insert-steps`, load1=11.49 (M)[AC]:
+
+```text
+journal_test:1689 FAIL journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_BUSY && progress==4096-72
+```
+
+GREEN for the session 8 changes:
+
+```text
+journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence
+journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes
+```
+
+Session 8 GREEN for retained §1/6/10/11/12/13/14 fixes, full release journal
+suite launch load1=13.53 (M)[AC]:
+
+```text
+journal_test: overlapping saves preserve both targets/cutoffs/deltas in both finish orders and publication/retirement failures
+journal_test: session values exact after replay/rotation/power image; CRC-valid semantic mutants rejected
+journal_test: writeback loss requires a fresh checkpoint (released unsynced batch included)
+journal_test: retained identity has a full metadata barrier (empty BASE mtime survives)
+journal_test: retention completion crosses the routed work mailbox
+journal_test: PAD IO outranks submission BUSY and retries only on worker
+journal_test: valid 4095-byte BASE retains in bounded recovery directory
+```
+
+The remaining contract probes deliberately stay RED and are opt-in. They are
+not part of passing `make check`; no GREEN is claimed for their full findings.
+Historical red (§4/7/8), launch load1=1.90 (M)[AC]:
+
+```text
+journal_test: bulk-loss repro unprotected_bytes=1602856 (M) limit=65536 (G) durable_records=0
+journal_test:1415 FAIL unprotected<=JOURNAL_DEFAULT_SYNC_BYTES
+journal_test: append-bound repro elapsed_ns=20337358 (M) UI_slice_ns=500000 (G) attempts=1
+journal_test:1432 FAIL elapsed<=500000
+journal_test: save-ack repro elapsed_ns=210829012 (M) G8s_p99_ns=5000000 (G)
+journal_test:1453 FAIL elapsed<=5000000
+```
+
+## Verification
+
+All displays are :99. Compiler versions: gcc 13.3.0 and clang 18.1.3.
+Build flags include C11, -Wall -Wextra -Werror -Wshadow -Wconversion.
+All session 8 power stamps were Not charging [AC]. GCC `make all` exits 0.
+Journal fuzz build exits 0; the requested run was 60 s (G), launch load1=14.83
+(M)[AC], with LeakSanitizer disabled. It completed cleanly:
+
+```text
+Done 12824 runs in 61 second(s)
+```
+
+Final full sanitizer-suite completion is still pending at this report checkpoint.
+
+The first sandboxed `make check` failed because cli_test could not connect to
+Xvfb :99. A local-socket escalation was approved. A subsequent run caught the
+expected smallest-credit RED in the then-current build while the fix was being
+completed; the final run rebuilds the corrected source. Only LeakSanitizer is
+disabled with `ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with
+leaks enabled. No :0 window was opened.
+
+Release journal GREEN, launch load1=13.53 (M)[AC]:
+
+```text
+journal_test: malloc_guard=active append_allocations=0
+journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
+```
+
+Paired benchmark ran once, adjacent variants in alternating order. Launch
+load1=15.86 and row load1=15.56 (M)[AC]. Timing is TRACK evidence, no gate verdict:
+
+```text
+TRACK journal_insert_variant variant=one_shot pairs=16 bytes=1000000 enqueue_sum_p50_ms=0.864 enqueue_sum_p99_ms=1.063 protected_completion_p50_ms=0.864 protected_completion_p99_ms=1.064 content_verified=1 allocations=0 (M)[AC] load1=15.56
+TRACK journal_insert_variant variant=continuation pairs=16 bytes=1000000 enqueue_sum_p50_ms=1.391 enqueue_sum_p99_ms=1.935 protected_completion_p50_ms=34.823 protected_completion_p99_ms=40.195 content_verified=1 allocations=0 (M)[AC] load1=15.56
+TRACK journal_insert_step calls=1312 p50_us=17.488 p99_us=56.857 input_check_boundaries=1 byte_credit_enforced=1 wall_time_unbounded=1 (M)[AC] load1=15.56
+journal_bench: correctness=ok mode=TRACK gate_misses=0 status=0 (complete G1/G9 endpoints require editor harness)
+journal_bench: gate self-check ok (injected delay fails GATE; TRACK preserves correctness failures)
+```
+
+The continuation completion row includes the fixture's blocking fence waits;
+it demonstrates the scheduling cost still requiring editor integration. The
+individual calls offer input-check boundaries but retain the syscall stall
+limitation. No quiet-box run, measurement rerun or noisy timing gate verdict.
diff --git a/docs/worklog/2026-10-10.md b/docs/worklog/2026-10-10.md
new file mode 100644
index 0000000000000000000000000000000000000000..628a749e2a9c5a0a8b67c705b877a6de50df1b6f
--- /dev/null
+++ b/docs/worklog/2026-10-10.md
@@ -0,0 +1,14 @@
+# Worklog — 2026-10-10
+
+## edit-4w1.54 session 8
+
+Continued the rebased journal WIP within src/journal and its unit/fuzz/bench
+scope. Added caller-driven INSERT steps, complete durable-prefix byte stats,
+PAD-aware admission credit and blocked-bulk power-image coverage. Preserved
+the existing sync-poisoning, retained metadata/mailbox, overlapping saves,
+session-oracle, PAD and long-path fixes. Red/green output, measured paired
+variants and verification are recorded in
+[the worker report](../worker-reports/edit-4w1.54-s8.md); design choices are in
+[the decision](../decisions/edit-4w1.54.md). §§4,7,8 remain unresolved at full
+contract level and require work/editor/save integration outside this scope.
+No git writes, new globals, typing malloc or real-display windows.
diff --git a/fuzz/journal_fuzz.c b/fuzz/journal_fuzz.c
index fa13ae7941c379323ad1e06547c5b714a940401e..4cdaa430250545698d36da6ff99dff0b36a692a5
--- a/fuzz/journal_fuzz.c
+++ b/fuzz/journal_fuzz.c
@@ -149,11 +149,49 @@
     EDIT_ASSERT(journal_replay_file(path,apply,&actual,&rr)==0 && !rr.corrupt && session_equal(&actual,&expected));
     work_pool_shutdown(&pool); unlink(path);
 }
+typedef struct slice_model { const uint8_t *bytes; size_t size, copied; } slice_model;
+static int apply_slice(void *ctx, const journal_record *r)
+{
+    slice_model *m=ctx;
+    if(r->type!=JOURNAL_INSERT || r->buffer_id!=41 || r->size<8) return 1;
+    size_t n=r->size-8;
+    if(read64(r->data)!=m->copied || n>m->size-m->copied ||
+       memcmp(r->data+8,m->bytes+m->copied,n)) return 1;
+    m->copied+=n; return 0;
+}
+static void fuzz_slices(const uint8_t *data, size_t size)
+{
+    if(size<3 || (data[0]&15u)!=3u) return;
+    uint8_t bytes[65536], wire[73728];
+    size_t length=1+(size_t)data[1]*256+data[2];
+    for(size_t i=0;i<length;i++) bytes[i]=data[i%size];
+    char path[]="/tmp/journal-fuzz-slices-XXXXXX"; int fd=mkstemp(path); if(fd<0) return; close(fd);
+    work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return; }
+    journal *j; if(journal_open(&j,path,&pool,NULL)) { work_pool_shutdown(&pool); unlink(path); return; }
+    size_t progress=0;
+    while(progress<length) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,41,0,bytes,length,&progress);
+        EDIT_ASSERT(rc==(progress<length?JOURNAL_BUSY:JOURNAL_OK));
+        EDIT_ASSERT(progress>=before && progress-before<=JOURNAL_INSERT_SLICE_BYTES);
+        EDIT_ASSERT(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+        if(progress==before) { EDIT_ASSERT(journal_flush(j)==0); continue; }
+        fd=open(path,O_RDONLY); EDIT_ASSERT(fd>=0);
+        ssize_t n=read(fd,wire,sizeof wire); close(fd); EDIT_ASSERT(n>=0);
+        slice_model m={bytes,progress,0}; journal_replay_result rr;
+        EDIT_ASSERT(journal_replay_bytes(wire,(size_t)n,apply_slice,&m,&rr)==0 && !rr.corrupt && m.copied==progress);
+        /* A caller can abandon the unaccepted tail after any protected step. */
+        if((data[0]&128u) && progress>=length/2) break;
+    }
+    EDIT_ASSERT(journal_flush(j)==0 && journal_get_stats(j).unprotected_bytes==0);
+    journal_close(j); work_pool_shutdown(&pool); unlink(path);
+}
 int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
 {
     no_visit(data,size);
     structured_bytes(data,size);
     fuzz_sessions(data,size);
+    fuzz_slices(data,size);
     if(!size || (data[0]&31u)!=0) return 0;
     char path[]="/tmp/journal-fuzz-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 0; close(fd);
     work_pool pool; if(work_pool_init(&pool,1,0)) { unlink(path); return 0; }
diff --git a/src/journal/STATUS.md b/src/journal/STATUS.md
index 4d22561ca4b0dbe3c45322dd6a6f659aa6b6fa08..00cc56b76cd5113092c27eae24b99705a64f3605
--- a/src/journal/STATUS.md
+++ b/src/journal/STATUS.md
@@ -1,5 +1,32 @@
 # Journal status — P1.9h / edit-4w1.54
 
+Session 8 adds `journal_insert_step`: one protected INSERT prefix per caller
+slice, with a 16 KiB (G) payload ceiling reduced for smaller batch/sync budgets.
+No allocation/lock/sync/submission occurs in the step. A caller-owned progress
+cursor distinguishes BUSY after a protected prefix from BUSY awaiting credit;
+IO includes the RAM-retained record and must not be reappended. Existing
+`journal_insert` keeps its whole-input contract. Complete durable-prefix byte
+stats now include actual UI cache writes; step admission reserves PAD and
+refuses to exceed configured sync_bytes beyond the received durable fence.
+Partial-record fences and failed namespace barriers release no premature credit.
+New unit and fuzz oracles check prefix content, blocked-bulk volume, resumption,
+small sync budgets, sticky FULL/IO and zero typing allocation.
+
+This partially addresses §§4 and 7; legacy append volume, independent worker
+durability scheduling, pwrite wall time and editor/save integration remain open.
+The new standalone savectl already runs journal preparation on a worker but
+leases the journal and defers subsequent journal edits; editor wiring and
+concurrent protected deltas still require integration for §8. Do not close the
+bead. Current session results/remaining work and exact red/green output:
+[worker report](../../docs/worker-reports/edit-4w1.54-s8.md). New design choices:
+[edit-4w1.54.md](../../docs/decisions/edit-4w1.54.md). Session 8 journal fuzz ran
+12824 inputs in 61 s (M)[AC], launch load1=14.83, requested 60 s (G), clean.
+The release journal suite passed with the malloc guard active and append
+allocations=0 (M)[AC], launch load1=13.53. GCC make all exits 0. The paired
+variant benchmark is TRACK only; bounded calls introduce durability-fence
+completion costs that still need event-loop integration. Session 7 evidence
+and the retained fixes follow; their older verification numbers are historical.
+
 Review P1.9-2 §§1,6,9–14 are addressed. Failed fdatasync poisons the complete
 live generation; journal_retry preserves IO until a complete CURRENT checkpoint
 is synced into a fresh inode and durably published. Partial-write and directory
@@ -18,7 +45,7 @@
 reports misses while retaining correctness failures. A deterministic
 --gate-self-check verifies that policy without another timing run.
 
-UNRESOLVED within this bead's allowed scope: review §§4,7,8. Shared bulk work
+UNRESOLVED at the full contract level: review §§4,7,8. Shared bulk work
 can still delay durability; actual unsynced cache volume can exceed PRD §7's
 64 KiB bound, including inside one large synchronous append. Synchronous
 regular-file pwrite has no wall-clock UI bound, and save preparation still
diff --git a/src/journal/journal.c b/src/journal/journal.c
index f38f244a02916e3f7c5006bae620532cb8d540b9..1568a7e383cac64cbd26b371f2ebbcc5bb539081
--- a/src/journal/journal.c
+++ b/src/journal/journal.c
@@ -305,7 +305,17 @@
     while(pos+32<=upto) { size_t n=u32(b->bytes+pos+4); if(n>upto-pos) break; if(u32(b->bytes+pos+8)!=PAD) seq=u64(b->bytes+pos+16); pos+=n; }
     return seq;
 }
-static int sync_disk(journal *j, uint64_t sequence)
+static size_t prefix_bytes(const journal_batch *b, size_t upto)
+{
+    size_t pos=0;
+    while(pos+32<=upto) {
+        size_t n=u32(b->bytes+pos+4);
+        if(n>upto-pos) break;
+        pos+=n;
+    }
+    return pos;
+}
+static int sync_disk(journal *j, uint64_t sequence, uint64_t complete_bytes)
 {
     journal_disk *d=&j->disk;
     int rc=io_sync(&j->io,d->fd,false);
@@ -315,13 +325,15 @@
     if(d->unsynced && interval>s->max_sync_interval_ns) s->max_sync_interval_ns=interval;
     s->last_sync_ns=now; s->last_sync_bytes=d->unsynced;
     if(d->unsynced>s->max_sync_bytes) s->max_sync_bytes=d->unsynced;
-    s->syncs++; s->durable_sequence=sequence; d->unsynced=0; return 0;
+    s->syncs++; s->durable_sequence=sequence;
+    if(complete_bytes>s->durable_bytes) s->durable_bytes=complete_bytes;
+    d->unsynced=0; return 0;
 }
 static void worker(work_ctx *ctx)
 {
     journal *j=ctx->arg; journal_batch *b=&j->batches[j->active_index]; journal_disk *d=&j->disk;
     size_t pos=b->progress; int rc=0; uint64_t initial=d->written_sequence;
-    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence);
+    if(d->unsynced>=j->sync_bytes) rc=sync_disk(j,d->written_sequence,d->stats.file_bytes);
     while(!rc && pos<b->sealed && !work_should_stop(ctx)) {
         size_t n=b->sealed-pos; uint64_t room=j->sync_bytes-d->unsynced; if(n>room) n=(size_t)room;
         size_t done=0;
@@ -332,9 +344,9 @@
         pos+=done; b->progress=pos; d->unsynced+=done; d->written_sequence=prefix_sequence(b,pos,initial);
         d->stats.written_sequence=d->written_sequence; d->stats.file_bytes=b->offset+pos;
         if(rc) break;
-        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence); if(rc) break; }
+        if(d->unsynced>=j->sync_bytes || clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns) { rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos)); if(rc) break; }
     }
-    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence);
+    if(!rc && !work_should_stop(ctx) && (b->force || (d->unsynced && clock_ns()-d->stats.last_sync_ns>=j->sync_interval_ns))) rc=sync_disk(j,d->written_sequence,b->offset+prefix_bytes(b,pos));
     d->stats.error=rc;
     journal_completion *completion=(journal_completion *)(void *)(b->bytes+j->capacity);
     completion->stats=d->stats;
@@ -428,6 +440,7 @@
     if(j->directory_pending) {
         int rc=io_sync(&j->io,j->directory_fd,true); if(rc) return rc;
         j->directory_pending=false; j->stats.durable_sequence=j->disk.stats.durable_sequence;
+        j->stats.durable_bytes=j->disk.stats.durable_bytes;
     }
     j->stats.error=j->append_error; j->disk.stats.error=0;
     j->append_failed=false; j->stats.append_errno=0;
@@ -501,6 +514,33 @@
 }
 int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len)
 { uint8_t data[16]; if(off>UINT64_MAX-len) return JOURNAL_INVALID; put64(data,off); put64(data+8,len); return journal_append(j,JOURNAL_DELETE,id,data,16); }
+int journal_insert_step(journal *j, uint64_t id, uint64_t off,
+                        const uint8_t *bytes, size_t size, size_t *progress)
+{
+    if(!j || !progress || *progress>size || (!bytes && size) ||
+       off>UINT64_MAX-size) return JOURNAL_INVALID;
+    if(*progress==size) return 0;
+    size_t n=size-*progress;
+    if(n>JOURNAL_INSERT_SLICE_BYTES) n=JOURNAL_INSERT_SLICE_BYTES;
+    if(n>j->capacity-72) n=j->capacity-72;
+    if(n>j->sync_bytes-72) n=(size_t)(j->sync_bytes-72);
+    uint8_t *record;
+    int preflight=reserve_record(j,n+8,&record); if(preflight) return preflight;
+    /* Reserve the eventual PAD too. Credit is returned only by a received
+     * durability fence at a COMPLETE record boundary, never by worker-accounted
+     * bytes or a partial CRC-protected record. This remains conservative when
+     * the kernel sync also covers newer UI writes in the other batch. */
+    journal_batch *b=&j->batches[j->current];
+    uint64_t end=j->reserved+sealed_size(b->used+n+40);
+    if(end>j->stats.durable_bytes && end-j->stats.durable_bytes>j->sync_bytes)
+        return JOURNAL_BUSY;
+    uint64_t accepted=j->stats.accepted_sequence;
+    int rc=journal_insert(j,id,off+*progress,bytes+*progress,n);
+    /* IO after encoding still owns this record in the fixed queue. Sticky IO,
+     * FULL or a prior transport gap accept nothing; never skip those bytes. */
+    if(j->stats.accepted_sequence!=accepted) *progress+=n;
+    return rc?rc:(*progress<size?JOURNAL_BUSY:JOURNAL_OK);
+}
 static int base_data(const journal_base *base, uint8_t *data, size_t *size)
 {
     if(!base || !base->path || strlen(base->path)>4096 || base->prefix_len>4096 || base->prefix_len>base->size) return JOURNAL_INVALID;
@@ -586,7 +626,9 @@
 journal_stats journal_get_stats(const journal *j)
 {
     if(!j) return (journal_stats){.error=JOURNAL_INVALID};
-    journal_stats s=j->stats; s.pending_bytes=j->batches[0].used+j->batches[1].used; return s;
+    journal_stats s=j->stats; s.pending_bytes=j->batches[0].used+j->batches[1].used;
+    s.unprotected_bytes=s.file_bytes>s.durable_bytes?s.file_bytes-s.durable_bytes:0;
+    return s;
 }
 void journal_set_message_handler(journal *j, void (*handler)(const work_msg *, void *), void *ctx)
 {
@@ -694,7 +736,7 @@
         j->limit=next->limit; j->disk.stats.checkpoint_bytes=checkpoint_bytes; j->stats.checkpoint_bytes=checkpoint_bytes;
         j->directory_pending=true;
         if(rc || io_sync(&j->io,j->directory_fd,true)) {
-            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0;
+            rc=JOURNAL_IO; j->stats.error=rc; j->stats.durable_sequence=0; j->stats.durable_bytes=0;
         } else j->directory_pending=false;
         close(oldfd);
     }
diff --git a/src/journal/journal.h b/src/journal/journal.h
index 06b7e1b710d63afefb8bb6119a2701fd24339d4b..40250a6b9afa76509d026b6e61df6dfceb04155e
--- a/src/journal/journal.h
+++ b/src/journal/journal.h
@@ -14,6 +14,7 @@
 #define JOURNAL_DEFAULT_FILE_BYTES 67108864ull
 #define JOURNAL_DEFAULT_SYNC_BYTES 65536ull
 #define JOURNAL_DEFAULT_SYNC_NS 1000000000ull
+#define JOURNAL_INSERT_SLICE_BYTES 16384u
 
 typedef enum journal_error {
     JOURNAL_OK = 0, JOURNAL_IO, JOURNAL_INVALID, JOURNAL_FULL,
@@ -65,6 +66,8 @@
     uint64_t file_limit_bytes, checkpoint_bytes, queue_bytes;
     uint64_t pending_bytes;    /* bytes retained in the two UI/worker batches */
     int append_errno;          /* sticky UI-write errno; short/zero write => EIO */
+    uint64_t durable_bytes;    /* complete wire prefix covered by data + name barriers */
+    uint64_t unprotected_bytes;/* actual page-cache file extent beyond that prefix */
 } journal_stats;
 typedef struct journal journal;
 /* Optional per-instance syscall seam; callbacks have POSIX return/errno semantics.
@@ -182,6 +185,26 @@
  * Existing backlog can still suspend recovery; caller surfaces FULL. */
 int journal_insert(journal *j, uint64_t id, uint64_t off,
                    const uint8_t *bytes, size_t size);
+/* Caller-driven large-insert continuation. Initialize *progress to zero; keep
+ * bytes/size/id/off stable and immutable until finished. Each call encodes and
+ * attempts ONE record of at most JOURNAL_INSERT_SLICE_BYTES payload bytes (or
+ * the smaller configured batch capacity). BUSY with advanced progress means
+ * a protected prefix and more work: check input before calling again. BUSY
+ * without progress means a transport gap or insufficient durability credit;
+ * force pump/receive outside the typing path, then resume. A step reserves
+ * its eventual PAD and refuses to exceed configured sync_bytes beyond the last
+ * received COMPLETE durable record boundary. This byte bound requires all
+ * intervening append calls to use this interface; legacy appends can exceed it.
+ * Worker/device delays still prevent a wall-clock power-loss bound.
+ * OK means the entire requested prefix is in page cache. On IO, progress also
+ * includes this call's RAM-retained record; suspend completion and drain/retry
+ * off path, never reappend that prefix. FULL accepts none of this step; earlier
+ * successful steps remain accepted. The caller owns mutation/checkpoint order
+ * and publishes full-edit completion only after all steps are protected.
+ * Zero size / already complete progress is a no-op. No allocation, lock,
+ * wait, sync or submission. Bytes/attempts bound CPU work, NOT syscall wall time. */
+int journal_insert_step(journal *j, uint64_t id, uint64_t off,
+                        const uint8_t *bytes, size_t size, size_t *progress);
 int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len);
 int journal_set_base(journal *j, uint64_t id, const journal_base *base);
 int journal_set_view(journal *j, uint64_t id, const journal_view *view);
diff --git a/tests/journal_test.c b/tests/journal_test.c
index 5b042e95f94774acdb4d279c88f34318eec8c87a..f6a9baedf3362512b538deffd3e3caee4993ed1e
--- a/tests/journal_test.c
+++ b/tests/journal_test.c
@@ -340,6 +340,7 @@
     CHECK(journal_insert(j,1,1,big,sizeof big)==JOURNAL_IO); d->append_limit=0; d->fail_write=1;
     CHECK(journal_flush(j)==JOURNAL_IO);
     CHECK(journal_get_stats(j).durable_sequence==1);
+    CHECK(journal_get_stats(j).durable_bytes==41 && journal_get_stats(j).unprotected_bytes==65536-41);
     model m={0}; journal_replay_result rr; CHECK(durable_replay(d,&m,&rr)==0);
     CHECK(rr.corrupt && rr.last_sequence==1 && m.len==1 && m.text[0]=='a');
     CHECK(journal_retry(j)==0); CHECK(journal_flush(j)==0);
@@ -366,6 +367,7 @@
         if(phase>=1) {
             m=(model){0}; CHECK(journal_replay_file(path,apply,&m,&rr)==0 && m.len==3 && !memcmp(m.text,"new",3));
             CHECK(journal_get_stats(j).error==JOURNAL_IO && journal_get_stats(j).durable_sequence==0);
+            CHECK(journal_get_stats(j).durable_bytes==0 && journal_get_stats(j).unprotected_bytes==journal_get_stats(j).file_bytes);
             CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==JOURNAL_IO);
         }
         if(phase==2) {
@@ -376,6 +378,7 @@
         if(phase>=1) {
             d->fail_dir=0; CHECK(journal_retry(j)==0);
             CHECK(journal_get_stats(j).durable_sequence==1);
+            CHECK(journal_get_stats(j).durable_bytes==journal_get_stats(j).file_bytes && journal_get_stats(j).unprotected_bytes==0);
             CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==0 && journal_flush(j)==0);
             m=(model){0}; CHECK(durable_replay(d,&m,&rr)==0 && m.len==4 && !memcmp(m.text,"new!",4));
         }
@@ -1625,6 +1628,74 @@
         (unsigned long long)samples[4]);
     CHECK(samples[4]<=500000); return 0;
 }
+typedef struct slice_replay {
+    const uint8_t *expected;
+    size_t size, copied, records;
+} slice_replay;
+static int restore_slice(void *ctx, const journal_record *r)
+{
+    slice_replay *s=ctx;
+    if(r->type!=JOURNAL_INSERT || r->buffer_id!=17 || r->size<8) return 1;
+    size_t n=r->size-8;
+    if(get64(r->data)!=s->copied || n>s->size-s->copied ||
+       memcmp(r->data+8,s->expected+s->copied,n)) return 1;
+    s->copied+=n; s->records++; return 0;
+}
+static int insert_steps_test(void)
+{
+    char path[]="/tmp/journal-insert-steps-XXXXXX"; CHECK(temp(path)>=0);
+    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
+    append_fault f={.owner=pthread_self()}; journal_io io={.ctx=&f,.append_write=append_attempt};
+    journal *j; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
+    size_t size=1000000, progress=0; uint8_t *bytes=malloc(size); CHECK(bytes);
+    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(i*17u);
+    unsigned steps=0;
+    while(progress<size) {
+        size_t before=progress; unsigned attempts=f.attempts;
+        edit_malloc_guard_begin();
+        int rc=journal_insert_step(j,17,0,bytes,size,&progress);
+        size_t allocations=edit_malloc_guard_end();
+        if(progress==before) {
+            CHECK(rc==JOURNAL_BUSY && allocations==0 && f.attempts==attempts);
+            CHECK(journal_flush(j)==0); continue;
+        }
+        CHECK(progress>before && progress-before<=JOURNAL_INSERT_SLICE_BYTES);
+        CHECK(rc==(progress<size?JOURNAL_BUSY:JOURNAL_OK) && allocations==0 && f.attempts==attempts+1);
+        /* Independent page-cache image, before any pump: each completed slice
+         * already restores the scripted prefix. No reliance on accepted stats. */
+        int fd=open(path,O_RDONLY); CHECK(fd>=0); struct stat sb; CHECK(fstat(fd,&sb)==0);
+        uint8_t *wire=malloc((size_t)sb.st_size); CHECK(wire);
+        CHECK(read(fd,wire,(size_t)sb.st_size)==sb.st_size); close(fd);
+        slice_replay restored={bytes,progress,0,0}; journal_replay_result rr;
+        CHECK(journal_replay_bytes(wire,(size_t)sb.st_size,restore_slice,&restored,&rr)==0 && !rr.corrupt && restored.copied==progress);
+        CHECK(restored.records==++steps && rr.records==steps); free(wire);
+    }
+    CHECK(!f.wrong_thread && !f.syncs && !f.worker_writes);
+    unsigned attempts=f.attempts;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==0 && f.attempts==attempts);
+    size_t invalid=size+1;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&invalid)==JOURNAL_INVALID && invalid==size+1);
+    CHECK(journal_insert_step(j,17,UINT64_MAX,bytes,size,&progress)==JOURNAL_INVALID);
+    CHECK(journal_insert_step(j,17,0,bytes,size,NULL)==JOURNAL_INVALID);
+    CHECK(journal_flush(j)==0); journal_close(j);
+    CHECK(truncate(path,0)==0);
+    journal_options opt={.batch_bytes=8192}; f=(append_fault){.owner=pthread_self(),.error=EAGAIN};
+    CHECK(journal_open_with_io(&j,path,&pool,&opt,&io)==0); progress=0;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_IO && progress==8192-72);
+    CHECK(f.attempts==1 && journal_get_stats(j).accepted_sequence==1);
+    size_t retained=progress;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_IO && progress==retained && f.attempts==1);
+    f.error=0; CHECK(journal_retry(j)==0 && journal_flush(j)==0);
+    journal_close(j); CHECK(truncate(path,0)==0);
+    opt.sync_bytes=4096; opt.max_file_bytes=4096; f=(append_fault){.owner=pthread_self()};
+    CHECK(journal_open_with_io(&j,path,&pool,&opt,&io)==0); progress=0;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_BUSY && progress==4096-72);
+    CHECK(journal_flush(j)==0); retained=progress;
+    CHECK(journal_insert_step(j,17,0,bytes,size,&progress)==JOURNAL_FULL && progress==retained);
+    journal_close(j); work_pool_shutdown(&pool); free(bytes); unlink(path);
+    puts("journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes");
+    return 0;
+}
 static int bulk_loss_repro(void)
 {
     char path[]="/tmp/journal-bulk-loss-XXXXXX"; CHECK(temp(path)>=0);
@@ -1646,6 +1717,56 @@
     CHECK(unprotected<=JOURNAL_DEFAULT_SYNC_BYTES);
     return 0;
 }
+static int insert_credit_test(void)
+{
+    char path[]="/tmp/journal-insert-credit-XXXXXX"; CHECK(temp(path)>=0);
+    work_pool pool; CHECK(work_pool_init(&pool,1,0)==0);
+    fault_disk *d=calloc(1,sizeof *d); CHECK(d); d->path=path; journal_io io=fault_io(d);
+    journal *j; CHECK(journal_open_with_io(&j,path,&pool,NULL,&io)==0);
+    work_handle blocker=work_submit(&pool,(work_job){blocked,NULL,0,WORK_BULK}); CHECK(blocker.epoch);
+    uint8_t bytes[100000]; for(size_t i=0;i<sizeof bytes;i++) bytes[i]=(uint8_t)(i*19u);
+    size_t progress=0;
+    for(unsigned i=0;i<16 && progress<sizeof bytes;i++) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,17,0,bytes,sizeof bytes,&progress);
+        CHECK(rc==JOURNAL_BUSY || rc==0);
+        journal_stats st=journal_get_stats(j);
+        struct stat sb; CHECK(stat(path,&sb)==0);
+        CHECK(st.unprotected_bytes==(uint64_t)sb.st_size && st.durable_bytes==0);
+        if(progress==before) break;
+    }
+    CHECK(progress>0 && progress<sizeof bytes);
+    CHECK(journal_pump(j,1,true)==0);
+    size_t stopped=progress;
+    edit_malloc_guard_begin();
+    for(unsigned i=0;i<1000;i++) {
+        CHECK(journal_insert_step(j,17,0,bytes,sizeof bytes,&progress)==JOURNAL_BUSY && progress==stopped);
+        CHECK(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+    }
+    CHECK(edit_malloc_guard_end()==0);
+    journal_replay_result rr; CHECK(journal_replay_bytes(NULL,0,NULL,NULL,&rr)==0);
+    disk_image *image=NULL;
+    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) image=&d->image[i];
+    CHECK(!image || image->size==0); /* power image remains empty behind bulk */
+    work_cancel(&pool,blocker); CHECK(journal_flush(j)==0);
+    journal_stats synced=journal_get_stats(j);
+    CHECK(synced.unprotected_bytes==0 && synced.durable_bytes==synced.file_bytes);
+    while(progress<sizeof bytes) {
+        size_t before=progress;
+        int rc=journal_insert_step(j,17,0,bytes,sizeof bytes,&progress);
+        CHECK(rc==0 || rc==JOURNAL_BUSY);
+        CHECK(journal_get_stats(j).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
+        if(progress==before) CHECK(journal_flush(j)==0);
+    }
+    CHECK(journal_flush(j)==0);
+    image=NULL;
+    for(size_t i=0;i<d->images;i++) if(d->image[i].inode==d->durable_name) image=&d->image[i];
+    CHECK(image);
+    slice_replay restored={bytes,sizeof bytes,0,0};
+    CHECK(journal_replay_bytes(image->bytes,image->size,restore_slice,&restored,&rr)==0 && !rr.corrupt && restored.copied==sizeof bytes);
+    journal_close(j); work_pool_shutdown(&pool); free(d); unlink(path);
+    puts("journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence"); return 0;
+}
 static ssize_t slow_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
 {
     (void)ctx; struct timespec stalled={0,20000000}; nanosleep(&stalled,NULL);
@@ -1688,6 +1809,8 @@
     if(argc==2) {
         if(!strcmp(argv[1],"--crc-wire")) return crc_wire_test();
         if(!strcmp(argv[1],"--crc-cpu")) return crc_cpu_test();
+        if(!strcmp(argv[1],"--insert-steps")) return insert_steps_test();
+        if(!strcmp(argv[1],"--insert-credit")) return insert_credit_test();
         if(!strcmp(argv[1],"--bulk-loss-repro")) return bulk_loss_repro();
         if(!strcmp(argv[1],"--append-bound-repro")) return append_bound_repro();
         if(!strcmp(argv[1],"--save-ack-repro")) return save_ack_repro();
@@ -1727,7 +1850,7 @@
         if(!strcmp(argv[1],"--retained-copy")) return retained_copy_test();
         return 2;
     }
-    CHECK(crc_wire_test()==0 && overlapping_saves_test()==0 && session_values_test()==0);
+    CHECK(insert_credit_test()==0 && insert_steps_test()==0 && crc_wire_test()==0 && overlapping_saves_test()==0 && session_values_test()==0);
     CHECK(writeback_loss_test()==0 && retention_metadata_test()==0 && retention_channel_test()==0 && pad_busy_test()==0 && long_base_retention_test()==0);
     CHECK(page_cache_test()==0 && append_failure_test()==0 && append_gap_test()==0);
     CHECK(open_options_test()==0 && rotate_path_test()==0 && directory_fd_test()==0 && exact_prefix_test()==0 && full_test()==0 && paste_test(true)==0 && paste_test(false)==0 && untitled_test()==0 &&

---

## Final message (codex -o)

Added bounded INSERT continuations and durability byte credit. `make all`, `make check`, and the requested fuzz run passed; typing allocations stayed zero. LeakSanitizer was disabled as required.

The bead remains incomplete: §§4,7,8 need integration outside journal scope. No git writes.

[Worker report](docs/worker-reports/edit-4w1.54-s8.md) contains red/green evidence, decisions, and remaining work.