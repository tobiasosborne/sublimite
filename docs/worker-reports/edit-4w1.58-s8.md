# edit-4w1.58 — P1-1 §§7–11, session 8

Completed bounded dense popcount counting, core visitor integration for the
panel, shared regex retry limits and rejecting/supervised find benchmarks.
Full details and runtime red/green evidence:
[worker report](../../docs/worker-reports/edit-4w1.58-s8.md);
[design decisions](../../docs/decisions/edit-4w1.58.md).

Both module fuzz targets completed cleanly for the requested duration. Release
module tests and the active editor typing allocator guard passed. Final GCC 13 make all and Clang 18 ASan/UBSan make check both exited 0:
51 sanitizer binaries plus replay CLI passed. Invocations used Xvfb :99 and
ASAN_OPTIONS=detect_leaks=0. Successful supervised fixtures preserve normal
exit/leak-check hooks; timed-out fixtures retain arguments until termination.
Coordinator: rerun with leaks enabled; supply the absent official all-a corpus
fixture for the official G6 matrix. Loaded measurements are TRACK comparisons.
The frozen find public header and its semantic assertions remain intact; test
fixture execution is now supervised. The regex engine returns FIND_ERR_LIMIT
on its documented request-wide state/candidate budget; a streaming replacement
is deferred. Whole-word filtering keeps the existing candidate/overlap policy.

# Find status — P1.10b best-of fold-in (edit-4w1.52)

The P1.10 pick is the SIMD filter/verify kernel. Its uncovered losing-variant
tests and verifier edges are folded in by this bead. Comparison, every port,
design argument and red-green evidence:
[P1.10b decision](../../docs/decisions/P1.10b.md). Historical pick and design:
[P1.10](../../docs/decisions/P1.10.md),
[SIMD filter/verify](../../docs/decisions/P1.10-simd-filter-verify.md).

# HANDOFF — sublimité, session 7 wound down (updated 2026-10-09 21:35)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
**Public repo (17:15, 2026-10-09):** https://github.com/tobiasosborne/sublimite (AGPL-3.0-or-later), remote `origin`, main tracks origin/main. `git push origin main` after each coordinator status check-in and at session end (not per bead; the beads pre-push hook runs). Only main goes up; `wt/*` stay local. Nothing in the tree may contain secrets or private data (worker reports and decision docs are public). `bd` stays local: no federation/sync daemon.
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Worker continuation — edit-4w1.58, session 8 (2026-10-10)

P1-1 §§7–11 find/findui WIP completed: bounded popcount/visitor output,
request-wide regex work limits, rejecting G6c bench verdicts and supervised
fixture lifetimes. Report: `docs/worker-reports/edit-4w1.58-s8.md`; decisions:
`docs/decisions/edit-4w1.58.md`. Release build and final full sanitizer check
pass (51 binaries and replay CLI); both module fuzzers ran clean for the
requested duration. Invocations used Xvfb :99 with leaks disabled; the
coordinator owns the leak-enabled rerun and commits. The shared all-a corpus fixture was missing;
supplemental measurements used a worktree-local fixture. No noisy timing gate
verdict is claimed. Prior coordinator session history below is preserved.
find fixture FAIL: frozen cancellation missing start deadline; terminating fixture (no shutdown join)
find_timeout_test: ok (normal exit hooks; frozen missing start terminated with live storage)
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; terminating fixture (no shutdown join)
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded)
find_test: ok (frozen P1.10a)
```

## Validation and limits

- GCC 13 release `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: exit 0 on the
  final source tree, including the added frozen timeout test.
- Clang 18 ASan/UBSan `make check`: first full run exited 0 (M)[AC], with
  `check: 50 test binaries passed` and replay CLI passing. The final run after
  the exit-hook repair also exited 0: `check: 51 test binaries passed`, with
  replay CLI passing. This is the final source tree.
  `ASAN_OPTIONS=detect_leaks=0` is required in this sandbox. The coordinator must
  rerun with LeakSanitizer enabled. The initial sandboxed check could not connect
  to Xvfb; the full run uses approved access to the existing Xvfb `:99`.
- `find_fuzz`: (M)[AC] 27844 runs in 61 s, exit 0; launch load1=19.72.
- `findui_fuzz`: (M)[AC] 23985 runs in 61 s, exit 0; same launch conditions.
- Release flags include `-Wall -Wextra -Werror -Wshadow -Wconversion`.

The official `/tmp/edit-corpus/all_a_1g.txt` was missing. No external fixture was
created. A supplemental all-a fixture was made only at
`build/corpus/all_a_1g.txt`, and both benches accept an explicit `--fixture`.
Removed the core benchmark's external fixture-writing fallback. Official corpus
verification can be repeated when the coordinator supplies that fixture.

Back-to-back dense variants, fresh mappings and identical independent oracle,
are recorded below. The box was loaded; timing comparisons are TRACK evidence,
and neither an acceptance gate pass nor a gate failure verdict. The panel row
verifies all first 4096 offsets through its 4095 prefix slots and one visible slot,
respecting the existing shared range cap. Cancellation numbers are also TRACK
observations here; the benchmark itself can reject misses as required.

## Missing / out of scope

No in-scope implementation or validation remains missing. Final release build,
full sanitizer check and active editor counting-allocator tests are green. Leak-enabled rerun and official missing corpus
fixture belong to the coordinator. Whole-word searches still inspect candidate
boundaries and retain overlapping-retry semantics; regex work limits are shared
across that path. No claim is made that every option can use byte-mask popcount.

P1-1 findings 1–6 and 12–13 are outside this bead and were not repaired.
The existing X11 clip/live/order/stall suites create their own private Xvfb
servers through `tests/x11_xvfb.h` despite the invocation display being `:99`;
that harness is outside this bead and was left unchanged. No command in this
worker targeted the real display. Decisions
are in `docs/decisions/edit-4w1.58.md`. No git writes, commits or bead-tool writes.

## Reproduction commands

Run with the existing Xvfb :99 only. For the shared fixtures, omit `--fixture`
once the coordinator supplies `/tmp/edit-corpus/all_a_1g.txt`. Supplemental
commands below read the existing worktree-local all-a fixture.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_p1r_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/findui_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_bench_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_timeout_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/editor_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/find_fuzz -max_total_time=60 -max_len=4096 -print_final_stats=1
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/findui_fuzz -max_total_time=60 -max_len=384 -print_final_stats=1
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/find_bench --row G6_dense_a --fixture build/corpus/all_a_1g.txt --samples 3
 M HANDOFF.md
 M bench/find_bench.c
 M bench/find_supervise.h
 M bench/findui_bench.c
 M docs/worklog/2026-10-09.md
 M src/find/STATUS.md
 M src/findui/STATUS.md
 M tests/find_test.c
?? bench/findui_count.h
?? docs/decisions/edit-4w1.58.md
?? docs/worker-reports/edit-4w1.58-s8.md
?? docs/worklog/2026-10-10.md
?? tests/find_timeout_test.c

diff --git a/bench/find_bench.c b/bench/find_bench.c
index 860a9db215616d4cc5e87473dc215b946226a385..3e9f6746d63a1ada31da1dcbca9bb6cab1318c45
--- a/bench/find_bench.c
+++ b/bench/find_bench.c
@@ -32,7 +32,7 @@
     const find_regex *regex; void *scratch; size_t sn;
     find_result result;
     find_code code;
-    atomic_bool begun,done;
+    atomic_bool begun,done,polling;
     uint64_t published_ns,returned_ns;
     bool published;
     uint64_t cpu_previous, cpu_max, cpu_polls, cancel_cpu;
@@ -54,7 +54,12 @@
 static bool find_bench_should_stop(const work_ctx *ctx)
 {
     bool stopped=work_should_stop(ctx);
-    cpu_record(ctx->arg,cpu_now(),stopped);
+    job *j=ctx->arg;
+    cpu_record(j,cpu_now(),stopped);
+    /* Do not let a cancel arriving before the entry poll masquerade as a
+     * measurement of the production loop's consecutive CPU slices. */
+    if (!stopped && j->cpu_polls>=2)
+        atomic_store_explicit(&j->polling,true,memory_order_release);
     return stopped;
 }
 #include "find_probe.h"
@@ -180,7 +185,7 @@
     }
     size_t n=quick?QUICK_BYTES:r->full_size;
     find_result want;
-    bool require_a=strcmp(r->file,"/tmp/edit-corpus/all_a_1g.txt")==0;
+    bool require_a=strcmp(r->name,"G6_dense_a")==0 || strncmp(r->name,"G6v_",4)==0;
     if(!warm_oracle(fd,n,r->needle,r->nn,&want,require_a)) { fprintf(stderr,"warming/oracle failed\n"); (void)close(fd); return 1; }
     if(!quick && strcmp(r->name,"G6_newline")==0 && want.total!=8947841) { fprintf(stderr,"log newline fixture mismatch\n"); (void)close(fd); return 1; }
     if(!quick && strcmp(r->name,"periodic_dense")==0 && (want.total!=8388607 || want.offsets[0]!=6)) { fprintf(stderr,"periodic fixture mismatch\n"); (void)close(fd); return 1; }
@@ -207,7 +212,7 @@
     bool correct=true;
     for(size_t i=0;i<samples;i++) {
         job j={0}; j.fd=fd; j.n=n; j.row=r; j.regex=regex; j.scratch=scratch; j.sn=sn;
-        atomic_init(&j.begun,false); atomic_init(&j.done,false);
+        atomic_init(&j.begun,false); atomic_init(&j.done,false); atomic_init(&j.polling,false);
         uint64_t start=bench_now_ns();
         work_handle h=work_submit(pool,(work_job){run_job,&j,1,WORK_BULK});
         if(!h.epoch) { correct=false; break; }
@@ -238,10 +243,11 @@
     bool correct=true;
     for(size_t i=0;i<samples;i++) {
         job j={0}; j.fd=fd; j.n=GIB; j.row=r; j.meter_cpu=true;
-        atomic_init(&j.begun,false); atomic_init(&j.done,false);
+        atomic_init(&j.begun,false); atomic_init(&j.done,false); atomic_init(&j.polling,false);
         work_handle h=work_submit(pool,(work_job){run_job,&j,2,WORK_BULK}); if(!h.epoch) { correct=false; break; }
         uint64_t deadline=bench_now_ns()+WAIT_NS;
         wait_flag(&j.begun,deadline,"worker start");
+        wait_flag(&j.polling,deadline,"production scan polling");
         uint64_t t=bench_now_ns(); work_cancel(pool,h); (void)bench_add(&ack,bench_now_ns()-t);
         wait_flag(&j.done,deadline,"cancellation completion");
         retire(pool,h,deadline);
@@ -264,6 +270,7 @@
     arguments *args=arg; int argc=args->argc; char **argv=args->argv;
     bool quick=false; size_t samples=31;
     const char *only=NULL;
+    const char *all_a="/tmp/edit-corpus/all_a_1g.txt";
     for(int i=1;i<argc;i++) {
         if(strcmp(argv[i],"--quick")==0) { quick=true; samples=3; }
         else if(strcmp(argv[i],"--samples")==0 && i+1<argc) {
@@ -271,7 +278,8 @@
             if(!end || *end || value==0 || value>SAMPLE_CAP) return 2;
             samples=(size_t)value;
         } else if(strcmp(argv[i],"--row")==0 && i+1<argc) only=argv[++i];
-        else { fprintf(stderr,"usage: find_bench [--quick] [--samples 1..1000] [--row NAME]\n"); return 2; }
+        else if(strcmp(argv[i],"--fixture")==0 && i+1<argc) all_a=argv[++i];
+        else { fprintf(stderr,"usage: find_bench [--quick] [--samples 1..1000] [--row NAME] [--fixture ALL_A]\n"); return 2; }
     }
     char power[32]; printf("POWER status=%s (M)%s indicative: concurrent workers; default full gates, quick scaled TRACK\n",bench_battery_status(power,sizeof power),bench_evidence_tag());
     uint8_t near[32],middle[33],early[33],periodic[32];
@@ -284,10 +292,10 @@
     const row rows[]={
         {"G6_ERROR","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR",5,GIB,NULL,80000000,125000000},
         {"G6_newline","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"\n",1,GIB,NULL,80000000,125000000},
-        {"G6_dense_a","/tmp/edit-corpus/all_a_1g.txt",(const uint8_t *)"a",1,GIB,NULL,80000000,125000000},
-        {"G6v_a31b","/tmp/edit-corpus/all_a_1g.txt",near,sizeof near,GIB,NULL,160000000,250000000},
-        {"G6v_first_last_middle","/tmp/edit-corpus/all_a_1g.txt",middle,sizeof middle,GIB,NULL,160000000,250000000},
-        {"G6v_first_last_early","/tmp/edit-corpus/all_a_1g.txt",early,sizeof early,GIB,NULL,160000000,250000000},
+        {"G6_dense_a",all_a,(const uint8_t *)"a",1,GIB,NULL,80000000,125000000},
+        {"G6v_a31b",all_a,near,sizeof near,GIB,NULL,160000000,250000000},
+        {"G6v_first_last_middle",all_a,middle,sizeof middle,GIB,NULL,160000000,250000000},
+        {"G6v_first_last_early",all_a,early,sizeof early,GIB,NULL,160000000,250000000},
         {"periodic_dense","/tmp/edit-corpus/periodic.txt",periodic,sizeof periodic,PERIODIC_BYTES,NULL,0,0},
         {"regex_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR ",6,GIB,"ERROR +",0,0},
         {"regex_no_prefix","/tmp/edit-corpus/log_1g.txt",(const uint8_t *)"ERROR",5,GIB,"[EW]RROR",0,0}
diff --git a/bench/find_supervise.h b/bench/find_supervise.h
index 76c02920f9251e9118525f52f92e393a8d471870..6ff634c2520cdd03cd38f70f008f1fd880b03eb1
--- a/bench/find_supervise.h
+++ b/bench/find_supervise.h
@@ -5,6 +5,7 @@
 #include "harness.h"
 #include <errno.h>
 #include <signal.h>
+#include <stdlib.h>
 #include <sys/wait.h>
 #include <unistd.h>
 static int find_fixture_supervise(int (*run)(void *), void *arg,
@@ -13,7 +14,10 @@
     fflush(NULL);
     pid_t child = fork();
     if (child < 0) { perror("fork"); return 2; }
-    if (!child) { int rc = run(arg); fflush(NULL); _exit(rc); }
+    /* Successful fixtures have retired their workers and released storage.
+     * Normal exit preserves registered cleanup/leak-check hooks; only timeout
+     * paths terminate without attempting cleanup of live worker arguments. */
+    if (!child) { int rc = run(arg); fflush(NULL); exit(rc); }
     uint64_t deadline = bench_now_ns() + budget;
     for (;;) {
         int status;
diff --git a/bench/findui_bench.c b/bench/findui_bench.c
index 2558cea77732131740f8417f98746d08b5715429..8ffe15dd8b9dd2d49c9adfe4cb2b2bdb1fca642b
--- a/bench/findui_bench.c
+++ b/bench/findui_bench.c
@@ -1,6 +1,9 @@
 #include "findui/findui.h"
+#include "find/find.h"
 #include "../bench/harness.h"
+#include "find_supervise.h"
 #include <fcntl.h>
+#include <stdlib.h>
 #include <pthread.h>
 #include <stdatomic.h>
 #include <stdio.h>
@@ -47,8 +50,12 @@
             return (work_handle){i, atomic_load(&pool->slots[i].epoch)};
     return (work_handle){0, 0};
 }
-int main(int argc, char **argv)
+#include "findui_count.h"
+typedef struct cancel_arguments { int argc; char **argv; } cancel_arguments;
+static int cancel_worker(void *argument)
 {
+    cancel_arguments *args=argument;
+    int argc=args->argc; char **argv=args->argv;
     bool gate_mode = argc == 2 && strcmp(argv[1], "--gate") == 0;
     if (argc > 2 || (argc == 2 && !gate_mode && strcmp(argv[1], "--track") != 0)) return 2;
     char power[64], load[64];
@@ -129,3 +136,28 @@
     edit_arena_free(&arena); (void)munmap(mapping, length);
     return result;
 }
+int main(int argc,char **argv)
+{
+    if (argc>1 && strcmp(argv[1],"--count")==0) {
+        count_arguments args={"/tmp/edit-corpus/all_a_1g.txt",3,false};
+        for (int i=2;i<argc;i++) {
+            if (strcmp(argv[i],"--gate")==0) args.gate=true;
+            else if (strcmp(argv[i],"--track")==0) args.gate=false;
+            else if (strcmp(argv[i],"--fixture")==0 && i+1<argc) args.path=argv[++i];
+            else if (strcmp(argv[i],"--samples")==0 && i+1<argc) {
+                char *end=NULL; unsigned long n=strtoul(argv[++i],&end,10);
+                if (!end || *end || !n || n>64) return 2;
+                args.samples=(size_t)n;
+            } else return 2;
+        }
+        char power[64],load[64];
+        if (!stamp(power,sizeof power,load,sizeof load)) return 2;
+        printf("findui_count: (M)%s power=%s load1=%s fixture=%s\n",
+               bench_evidence_tag(),power,load,args.path);
+        return find_fixture_supervise(count_worker,&args,UINT64_C(600000000000),
+                                      "panel count through shutdown");
+    }
+    cancel_arguments args={argc,argv};
+    return find_fixture_supervise(cancel_worker,&args,UINT64_C(600000000000),
+                                  "panel cancellation through shutdown");
+}
diff --git a/bench/findui_count.h b/bench/findui_count.h
index dd140ad3739fc9a7a00dc868ffc3ed447337403c..8f749d1f9cd85817896e041a101b586aa9d667d3
--- a/bench/findui_count.h
+++ b/bench/findui_count.h
@@ -33,7 +33,9 @@
         work_pool *pool=edit_arena_alloc(&arena,sizeof *pool,_Alignof(work_pool));
         findui_panel panel={0};
         if (!tree || !pool || work_pool_init(pool,1,0)) _exit(2);
-        findui_config config={&arena,pool,FIND_MAX_OFFSETS,8,NULL,NULL};
+        /* The panel's shared range budget reserves at least one visible slot.
+         * Publish offsets 0..4094 in its prefix and offset 4095 in that slot. */
+        findui_config config={&arena,pool,FIND_MAX_OFFSETS-1,1,NULL,NULL};
         if (findui_init(&panel,&config)!=FINDUI_OK) _exit(2);
         uint64_t start=bench_now_ns(), deadline=start+UINT64_C(30000000000);
         void *mapping=mmap(NULL,length,PROT_READ,MAP_PRIVATE,fd,0);
@@ -42,27 +44,35 @@
         if (!snapshot) _exit(2);
         findui_code code=findui_set_source(&panel,snapshot,1);
         piece_snapshot_release(snapshot);
-        if (code!=FINDUI_OK || findui_show(&panel,true,false)!=FINDUI_OK ||
+        if (code!=FINDUI_OK ||
+            findui_set_window(&panel,FIND_MAX_OFFSETS-1,FIND_MAX_OFFSETS)!=FINDUI_OK ||
+            findui_show(&panel,true,false)!=FINDUI_OK ||
             findui_set_options(&panel,(findui_options){false,true,false})!=FINDUI_OK ||
             findui_set_query(&panel,(const uint8_t *)"a",1)!=FINDUI_OK) _exit(2);
         findui_state state;
         do {
-            if (bench_now_ns()>=deadline || findui_service(&panel)>FINDUI_MORE) _exit(2);
+            if (bench_now_ns()>=deadline || findui_service(&panel)>FINDUI_MORE) {
+                fputs("findui_count: search completion deadline/error\n",stderr); _exit(2);
+            }
             (void)work_mailbox_drain(pool,route,&panel);
             state=findui_get_state(&panel);
             if (state.searching) pause_worker();
         } while (state.searching);
         (void)bench_add(&times,bench_now_ns()-start);
-        if (!state.complete || state.match_count!=length || state.cached_matches!=FIND_MAX_OFFSETS ||
+        if (!state.complete || state.match_count!=length || state.cached_matches!=FIND_MAX_OFFSETS-1 ||
             state.selected.start!=0 || state.selected.end!=1) _exit(2);
-        for (size_t i=1;i<FIND_MAX_OFFSETS;i++) {
+        for (size_t i=1;i<FIND_MAX_OFFSETS-1;i++) {
             findui_range range;
             if (findui_next(&panel,1,&range)!=FINDUI_OK || range.start!=i || range.end!=i+1) _exit(2);
         }
-        while (findui_dispose(&panel)==FINDUI_MORE) {
+        findui_range last; size_t shown=0;
+        if (findui_highlights(&panel,FIND_MAX_OFFSETS-1,FIND_MAX_OFFSETS,&last,1,&shown)!=FINDUI_OK ||
+            shown!=1 || last.start!=FIND_MAX_OFFSETS-1 || last.end!=FIND_MAX_OFFSETS) _exit(2);
+        while ((code=findui_dispose(&panel))==FINDUI_MORE) {
             if (bench_now_ns()>=deadline) _exit(2);
             (void)work_mailbox_drain(pool,route,&panel); pause_worker();
         }
+        if (code!=FINDUI_OK) _exit(2);
         work_pool_shutdown(pool); piece_destroy(tree); edit_arena_free(&arena);
         (void)munmap(mapping,length);
     }
diff --git a/tests/find_test.c b/tests/find_test.c
index d5e31f73d805215672c8c18cea7cf393169a7736..71b1c91c70b76104c8d036a09c26c71dea4bcc47
--- a/tests/find_test.c
+++ b/tests/find_test.c
@@ -1,6 +1,7 @@
 #include "find/find.h"
 #include "base/base.h"
 #include "trace/trace.h"
+#include "../bench/find_supervise.h"
 #include <stdio.h>
 #include <string.h>
 #include <pthread.h>
@@ -285,9 +286,18 @@
     CHECK(find_literal(&j.s,NULL,0,&ctl,&j.r)==FIND_CANCELLED);
     edit_arena_free(&a); return 0;
 }
-int main(void)
+static int run_find_tests(void *unused)
 {
+    (void)unused;
     CHECK(sizeof(find_result)<=64*1024);
     CHECK(literal_cases()==0); CHECK(literal_edges()==0); CHECK(regex_contract()==0); CHECK(snapshot_cases()==0); CHECK(regex_cases()==0); CHECK(cancellation()==0);
     puts("find_test: ok (frozen P1.10a)"); return 0;
 }
+int main(void)
+{
+    /* Keep the frozen assertions intact, but bound lost starts, worker returns
+     * and shutdown as one fixture. Timeout kills the child before its storage
+     * can be released; the parent never joins an unresponsive worker. */
+    return find_fixture_supervise(run_find_tests, NULL, UINT64_C(30000000000),
+                                  "frozen find tests through shutdown");
+}
diff --git a/tests/find_timeout_test.c b/tests/find_timeout_test.c
index e7ca402664f2bf7c2cfafb69b8c18a6f64ec0509..8243a604164bdb8616c181d5ee34c9fd8f9f87d5
--- a/tests/find_timeout_test.c
+++ b/tests/find_timeout_test.c
@@ -11,6 +11,18 @@
 #include "find_test.c"
 #undef main
 #undef work_submit
+static void normal_exit_marker(void)
+{
+    char path[80];
+    (void)snprintf(path,sizeof path,"build/find-exit-%ld",(long)getppid());
+    FILE *file=fopen(path,"w");
+    if (file) { (void)fputc('1',file); (void)fclose(file); }
+}
+static int normal_fixture(void *unused)
+{
+    (void)unused;
+    return atexit(normal_exit_marker)==0 ? 0 : 1;
+}
 static int missing_start_fixture(void *unused)
 {
     (void)unused;
@@ -18,9 +30,19 @@
 }
 int main(void)
 {
+    /* Successful fixtures must run normal exit hooks, including LSan when the
+     * coordinator enables it. Only timed-out fixtures skip those hooks. */
+    char path[80];
+    (void)snprintf(path,sizeof path,"build/find-exit-%ld",(long)getpid());
+    (void)unlink(path);
+    CHECK(find_fixture_supervise(normal_fixture,NULL,UINT64_C(1000000000),"normal exit")==0);
+    FILE *file=fopen(path,"r");
+    CHECK(file!=NULL);
+    CHECK(fgetc(file)=='1');
+    CHECK(fclose(file)==0 && unlink(path)==0);
     int code=find_fixture_supervise(missing_start_fixture,NULL,UINT64_C(100000000),
                                     "frozen cancellation missing start");
     CHECK(code==2);
-    puts("find_timeout_test: ok (frozen missing-start fixture terminated with live storage)");
+    puts("find_timeout_test: ok (normal exit hooks; frozen missing start terminated with live storage)");
     return 0;
 }

---

## Final message (codex -o)

Completed findings 7–11. Release build, sanitizer suite, both fuzzers, and typing allocation guard passed.

[Worker report with red/green evidence](/home/tobias/Projects/sublimite/.wt/edit-4w1.58/docs/worker-reports/edit-4w1.58-s8.md).

Leak detection was disabled; coordinator rerun required. The missing official all-a fixture was covered with a worktree-local fixture. No git writes.