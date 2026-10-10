# P3.7 experiment — edit-zzj.8 collection, session 8

Collected existing A/B/C under this nonignored directory. A is no action; B is
a zero-instance GL draw/fence and 200 us spin (E, configured); C uses the
existing 100.2 ms total synchronous spin (E, configured). No sched/uclamp
variant, production adoption, API change or additional candidate was made.

The common bench remains "'`bench/prewake_bench.c`, privately including the
production renderer with renamed exports. Frames stop at swap/fence, not
native matching Present or optical completion. Counting guards cover submit;
native GL/XCB operations retain the existing IO exemption.

`tests/prewake_test.c` collects all existing candidate contracts into make
all/check. `fuzz/prewake_fuzz.c` independently models hints, modifier variants,
repeats, activity, threshold boundaries, reversed clocks, warm/spin failures
and debounce for all candidates. No globals or allocations were added to the
candidate implementations.

New bench `I` protocol measures an eleven-second no-event native UI-loop row
(E, configured), with hint dispatch enabled and blink/repeat disabled. It fails
on any native event, UI wakeup or warm-up; all-thread process CPU is also logged.
This is the experiment'"'s idle row, not a complete editor/driver G11 verdict.

Verification commands from the repo root:

"'```sh
env DISPLAY=:99 EDIT_DISPLAY=:99 make all
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/fuzz/prewake_fuzz -max_total_time=60 -max_len=4096
sh tools/prewake_bench.sh build
sh tools/prewake_bench.sh test
sh tools/prewake_bench.sh test-san
sh tools/test_prewake_collection.sh
sh tools/test_prewake_display.sh
sh tools/test_prewake_idle.sh
env DISPLAY=:99 EDIT_DISPLAY=:99 python3 bench/prewake/run.py '"\\
  --trials 2 --idle-seconds 15 --out build/prewake/smoke-new.log
python3 bench/prewake/summarize.py --partial build/prewake/smoke-new.log
"'```

Socket access needs sandbox review. The display guard test is window-free;
every worker bench window is on :99. `--partial` prints a descriptive table
without full-run validation; the s8 smoke'"'s count/order/idle/allocation checks
were also asserted separately. Output logs refuse overwrites.

Historical full trials remain in ignored "'`variants/P3.7/evidence/`: interrupted
at 559 samples (M, recorded count), no DONE. They are not a finished comparison.
Current smoke raw evidence is public in
`docs/worker-reports/edit-zzj.8-s8-smoke.log`; complete commands, current results
and red/green are in `docs/worker-reports/edit-zzj.8-s8.md`.
`docs/decisions/edit-zzj.8.md` is DRAFT with the coordinator'"'s exact real-display
command and requested trial plan. No variant is selected from Xvfb.

Remaining: announced hardware run, native/optical endpoints, actual blink CPU,
driver-thread wakeups, false positives/no-key hints, power-state residency and
energy. Cold-cache work and new policies are outside this collection bead.
The coordinator must rerun leak detection; worker checks disable LSan.
EOF
cat /sys/class/power_supply/BAT0/status
cat /proc/loadavg
tail -n 16 build/prewake/evidence-s8/idle-final.log" in /home/tobias/Projects/sublimite/.wt/edit-zzj.8
 succeeded in 0ms:
Not charging
15.36 14.23 9.11 11/2393 5

diff --git a/bench/prewake/contract.c b/bench/prewake/contract.c
index e474882e721f93512a5d4cce843a93d9fdd76342..7c691021e5e2ca4a4be5270e815411b740f64224
--- a/bench/prewake/contract.c
+++ b/bench/prewake/contract.c
@@ -7,11 +7,11 @@
 #include PREWAKE_IMPL
 #define CHECK(c) do { if (!(c)) { fprintf(stderr,"prewake_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
 typedef struct fake { unsigned warm; uint64_t spin; int error; } fake;
-static int warm(void *p) { fake *f=p; f->warm++; return f->error; }
-static int spin(void *p,uint64_t ns) { fake *f=p; f->spin+=ns; return 0; }
+static int prewake_contract_warm(void *p) { fake *f=p; f->warm++; return f->error; }
+static int prewake_contract_spin(void *p,uint64_t ns) { fake *f=p; f->spin+=ns; return 0; }
 int main(void)
 {
-    fake f={0}; prewake_ops ops={&f,warm,spin};
+    fake f={0}; prewake_ops ops={&f,prewake_contract_warm,prewake_contract_spin};
     prewake_state s={0}; uint64_t idle=UINT64_C(15000000000);
     plat_event focus={.kind=PLAT_EV_FOCUS,.focused=true};
     plat_event motion={.kind=PLAT_EV_MOTION};
diff --git a/bench/prewake_bench.c b/bench/prewake_bench.c
index 284c326a9fff1e4e9e46e730002fc5ecac044cd3..23bf960b6190e151ad853372a86155d66c075e24
--- a/bench/prewake_bench.c
+++ b/bench/prewake_bench.c
@@ -37,6 +37,8 @@
     uint64_t dirty[4];
     int init_result;
     uint32_t frame;
+    uint64_t warmups;
+    bool real_display;
 } prewake_fixture;
 static uint64_t prewake_clock(clockid_t id)
 {
@@ -71,6 +73,7 @@
 static int prewake_warm(void *ctx)
 {
     prewake_fixture *f=ctx; gl_state *s=&f->gl;
+    f->warmups++;
     if (!gl_bind(s)) return -1;
     /* Touch the retained production draw state without changing pixels. */
     s->gBindFramebuffer(GL_FRAMEBUFFER,s->fbo);
@@ -106,7 +109,7 @@
     f->page=(render_atlas_page){a->pixels,a->pixels_len,(size_t)dims.cell_w*95,dims.cell_w*95,dims.cell_h};
     f->grid.pages=&f->page; f->grid.page_count=1; f->grid.glyphs=f->glyphs; f->grid.glyph_count=95;
     f->config=(render_config){.dims=dims,.max_width=2880,.max_height=1800,.max_cells=count,.max_glyphs=95,.max_pages=1,.max_atlas_bytes=a->pixels_len};
-    plat_config pc={.title="P3.7 prewake TRACK on Xvfb",.width=dims.cols*dims.cell_w,.height=dims.rows*dims.cell_h,.work_eventfd=-1};
+    plat_config pc={.title="P3.7 prewake TRACK",.width=dims.cols*dims.cell_w,.height=dims.rows*dims.cell_h,.work_eventfd=-1};
     REQUIRE(plat_init(&f->platform,&pc)==0);
     plat_map(&f->platform); plat_set_blink(&f->platform,0); plat_set_repeat(&f->platform,0,0);
     pthread_t worker;
@@ -140,23 +143,73 @@
     int rc=fscanf(fp,"%lf",load); fclose(fp);
     return rc==1 ? 0 : -1;
 }
+typedef struct prewake_idle_sink {
+    prewake_fixture *fixture;
+    prewake_state state;
+    uint64_t events;
+    int error;
+} prewake_idle_sink;
+static void prewake_idle_event(void *ctx,const plat_event *event)
+{
+    prewake_idle_sink *sink=ctx;
+    prewake_ops ops={sink->fixture,prewake_warm,prewake_spin};
+    sink->events++;
+    int rc=prewake_hint(&sink->state,event,bench_now_ns(),&ops);
+    if (rc!=0) sink->error=rc;
+}
+static int prewake_idle(prewake_fixture *f)
+{
+    prewake_idle_sink sink={.fixture=f,.state={.last_activity_ns=bench_now_ns()}};
+    plat_callbacks cb={.ud=&sink,.on_event=prewake_idle_event};
+    /* Drain initialization's native events before the observation. No periodic
+     * timer is enabled; the sole timeout is the external measurement deadline.
+     * Every delivered event goes through the candidate's hint filter. */
+    REQUIRE(plat_run_for(&f->platform,&cb,100)==PLAT_OK && sink.error==0);
+    prewake_activity(&sink.state,bench_now_ns()); sink.events=0;
+    uint64_t before=f->platform.iterations,warmups=f->warmups;
+    char status[64],power[16]; double load;
+    REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
+    uint64_t cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID),start=bench_now_ns();
+    REQUIRE(plat_run_for(&f->platform,&cb,11000)==PLAT_OK && sink.error==0);
+    uint64_t elapsed=bench_now_ns()-start;
+    cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu;
+    uint64_t wakes=f->platform.iterations-before;
+    printf("G11 scope=UI_event_loop idle_ns=%llu process_cpu_ns=%llu wakeups=%llu hint_events=%llu warmups=%llu power=(M)[%s]%s load1=%.2f (G)wakeups=0\n",
+        (unsigned long long)elapsed,(unsigned long long)cpu,(unsigned long long)wakes,
+        (unsigned long long)sink.events,(unsigned long long)(f->warmups-warmups),power,
+        f->real_display ? "[real-display, proxy only]" : "[xvfb, indicative only]",load);
+    REQUIRE(wakes==0 && sink.events==0 && f->warmups==warmups);
+    return 0;
+}
 int main(int argc,char **argv)
 {
     (void)setvbuf(stdout,NULL,_IOLBF,0);
-    REQUIRE(getenv("DISPLAY")!=NULL && strcmp(getenv("DISPLAY"),":99")==0);
-    REQUIRE(getenv("EDIT_DISPLAY")!=NULL && strcmp(getenv("EDIT_DISPLAY"),":99")==0);
-    REQUIRE(getenv("EDIT_ALLOW_REAL_DISPLAY")==NULL);
-    if (argc!=2 || strcmp(argv[1],"--protocol")!=0) { fprintf(stderr,"usage: %s --protocol (commands N/H on stdin; caller supplies >=15s idle)\n",argv[0]); return 1; }
+    bool real=argc==3 && strcmp(argv[2],"--real-display")==0;
+    const char *display=getenv("DISPLAY"),*edit_display=getenv("EDIT_DISPLAY");
+    REQUIRE(display!=NULL && edit_display!=NULL && strcmp(display,edit_display)==0);
+    if (real) {
+        const char *allow=getenv("EDIT_ALLOW_REAL_DISPLAY");
+        REQUIRE(allow!=NULL && strcmp(allow,"1")==0 && display[0]!='\0');
+    } else {
+        REQUIRE(getenv("EDIT_ALLOW_REAL_DISPLAY")==NULL && strcmp(display,":99")==0);
+    }
+    if ((argc!=2 && !real) || (strcmp(argv[1],"--protocol")!=0 && strcmp(argv[1],"--display-check")!=0)) {
+        fprintf(stderr,"usage: %s --protocol|--display-check [--real-display] (commands N/H/I on stdin; caller supplies >=15s idle)\n",argv[0]); return 1;
+    }
+    if (strcmp(argv[1],"--display-check")==0) {
+        printf("DISPLAY_CHECK display=%s mode=%s PASS (no window)\n",display,real ? "coordinator-opt-in" : "xvfb"); return 0;
+    }
     char status[64],power[16]; double load;
     REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
-    prewake_fixture f={0}; REQUIRE(prewake_setup(&f)==0);
+    prewake_fixture f={.real_display=real}; REQUIRE(prewake_setup(&f)==0);
     uint64_t ns; size_t allocs;
     for (unsigned i=0;i<3;i++) REQUIRE(prewake_frame(&f,&ns,&allocs)==0);
-    printf("READY renderer=%s surface=%ux%u cells=%zu power=(M)[%s] load1=%.2f status=%s\n",f.gl.device_name,f.platform.width,f.platform.height,(size_t)f.grid.dims.cols*f.grid.dims.rows,power,load,status);
+    printf("READY renderer=%s surface=%ux%u cells=%zu power=(M)[%s] evidence=%s load1=%.2f status=%s\n",f.gl.device_name,f.platform.width,f.platform.height,(size_t)f.grid.dims.cols*f.grid.dims.rows,power,real ? "real-display-proxy" : "xvfb-indicative-only",load,status);
     uint64_t last=bench_now_ns();
     int ch;
     while ((ch=getchar())!=EOF) {
         if (ch=='\n') continue;
+        if (ch=='I') { REQUIRE(prewake_idle(&f)==0); last=bench_now_ns(); continue; }
         REQUIRE(ch=='N' || ch=='H');
         uint64_t idle=bench_now_ns()-last;
         REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
diff --git a/docs/decisions/P3.7.md b/docs/decisions/P3.7.md
index 331c5e08132ddb5be34024aa6a5ecf9043a8e718..c8e81ade50a2f894ada354525aeaa6e65f0d097e
--- a/docs/decisions/P3.7.md
+++ b/docs/decisions/P3.7.md
@@ -1,5 +1,10 @@
 # P3.7 — predictive wake after idle (edit-zzj.8)
 
+**Historical session document.** The prior full run was interrupted, not
+completed. Current sources are collected in `bench/prewake/`; current status,
+the guarded coordinator command and the pending decision are in
+`docs/decisions/edit-zzj.8.md` and `docs/worker-reports/edit-zzj.8-s8.md`.
+
 Decision pending the single full run: needs a real-display run to decide whether
 the GPU warm-up should be adopted. No production patch has been applied.
 
diff --git a/docs/decisions/edit-zzj.8.md b/docs/decisions/edit-zzj.8.md
new file mode 100644
index 0000000000000000000000000000000000000000..a8612ae11fb18b079ca421d46b05e8f00c7080f3
--- /dev/null
+++ b/docs/decisions/edit-zzj.8.md
@@ -0,0 +1,92 @@
+# edit-zzj.8 — DRAFT: predictive wake experiment collection
+
+No variant is selected or promoted. This decision is **DRAFT**, awaiting an
+announced coordinator run on a hardware display. Xvfb timings establish only
+that the experimental protocol works. The historical P3.7 document describes
+the previous session; this document and the s8 worker report supersede its
+claims of an ongoing full run and its old source locations.
+
+The existing A/B/C implementations are collected under `bench/prewake/` so a
+normal source collection includes them. No additional variant was created.
+Production source, public production APIs and the Makefile are unchanged.
+
+| Candidate | Existing behavior after an eligible idle hint |
+|---|---|
+| A | No action |
+| B | Bind retained render state, zero-instance GL draw, flush/fence, synchronous 200 us spin (E, configured) |
+| C | B with a total synchronous 100.2 ms spin (E, configured); busy-wait alternative, no sched/uclamp implementation |
+
+Eligible hints are focus-in, motion, and a non-repeated modifier press after
+15 s (G, workload) of recorded inactivity. Successful dispatch, including a
+failed warm-up, consumes the debounce interval. This existing policy was kept.
+At the configured 50 ms hint lead (E), C blocks a scheduled key for at least
+50.2 ms (E) before frame work, even with instantaneous GL warm-up.
+
+The shared bench draws the existing cached ASCII grid through production GL
+snapshot/upload/draw/swap code, ending at a device fence. It excludes document
+mutation, layout, minimap, native matching Present completion and optical
+latency. It therefore reports a G3i-style proxy, never a G3i or G2b gate pass.
+Hint wall time and all-thread process CPU time are separate from frame time;
+scheduled-key latency includes oversleep and C's synchronous blocking cost.
+
+The added `I` protocol row drains initialization events, runs the existing
+platform loop with hint dispatch enabled and blink/repeat disabled for 11 s
+(E, configured), and records UI-loop iterations, native hint dispatches,
+warm-up calls and all-thread process CPU time. The external observation timeout
+is excluded by the platform's iteration counter. Any native event invalidates
+the no-event workload and fails the row; it is never silently filtered.
+This checks the experiment's no-event UI wakeups, not the complete editor's
+blink budget or every driver-thread wakeup.
+
+Default execution still requires matching `DISPLAY=:99 EDIT_DISPLAY=:99` with
+no real-display opt-in. The coordinator mode requires both the explicit
+`--real-display` argument and `EDIT_ALLOW_REAL_DISPLAY=1`, plus matching nonempty
+display variables. `--display-check` tests these guards without opening a
+window. This worker exercises only Xvfb and window-free guard tests.
+
+## Coordinator command and trial plan — DRAFT, not executed
+
+Only after announcing the batch to Tobias, restoring notification settings on
+exit, and placing the windows on the internal panel, the coordinator runs:
+
+```sh
+sh tools/prewake_bench.sh build
+env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 \
+  build/prewake/prewake-a --display-check --real-display
+env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 \
+  python3 bench/prewake/run.py --real-display --check-idle \
+  --trials 200 --idle-seconds 15 \
+  --out docs/worker-reports/edit-zzj.8-real-trials.log
+python3 bench/prewake/summarize.py docs/worker-reports/edit-zzj.8-real-trials.log
+```
+
+The command assumes the coordinator's announced hardware X session is `:0`;
+verify that before running. It is a future coordinator command, never worker
+authorization to open a real-display window. The output file must not exist:
+the runner refuses overwrites. The worker did not execute this block.
+
+Use 200 trials per candidate **per condition** (E, requested): A/N, B/N, C/N,
+A/H, B/H, C/H, repeated with a fresh 15 s sleep (G, workload) before every
+sample. Keep all renderers persistent, variants serial and comparisons back to
+back on the same loaded AC box. Stamp power/load for every sample; record the
+display geometry/refresh, hardware renderer, governor/EPP and compositor
+settings. Do not wait for a quiet box or filter failures/page faults. The sleep
+budget alone is 5 h (E), so arrange a batch that can finish without killing it.
+
+Confirm a hardware renderer rather than llvmpipe, verify the no-op/fence actually
+wakes the intended device, and measure no-key-following-hint cost and pointer
+false positives. Preserve the initial 50 ms lead (E, configured) for the exact
+existing comparison. Lead sweeps, sched/uclamp and editor integration require
+separate scope; they are not implemented here.
+
+The command above still measures the swap/fence **proxy** on real hardware.
+Matching Present/refresh and optical endpoints, C-state residency, energy with
+baseline/wrap handling, actual blink CPU, and false-positive event rates need
+additional coordinator instrumentation before an adoption decision. Do not
+infer joules from process CPU or fabricate T6. Cold cache is not measured.
+
+## Verification
+
+Current red/green, smoke rows, interrupted historical counts, active allocator
+evidence and remaining limitations are recorded in
+`docs/worker-reports/edit-zzj.8-s8.md`. No Xvfb ranking or gate verdict is made.
diff --git a/fuzz/prewake_fuzz.c b/fuzz/prewake_fuzz.c
new file mode 100644
index 0000000000000000000000000000000000000000..3e1fc5f1e99e39408c7b3af94f4ce4f7db7ddf86
--- /dev/null
+++ b/fuzz/prewake_fuzz.c
@@ -0,0 +1,64 @@
+/* Existing A/B/C hint policies against an independent event/clock model. */
+#include "base/base.h"
+#include "../bench/prewake/prewake.h"
+#include <xkbcommon/xkbcommon-keysyms.h>
+#define prewake_hint prewake_hint_a
+#define prewake_activity prewake_activity_a
+#include "../bench/prewake/a/prewake.c"
+#undef prewake_hint
+#undef prewake_activity
+#define prewake_hint prewake_hint_b
+#define prewake_activity prewake_activity_b
+#define prewake_trigger prewake_trigger_b
+#include "../bench/prewake/b/prewake.c"
+#undef prewake_hint
+#undef prewake_activity
+#undef prewake_trigger
+#define prewake_hint prewake_hint_c
+#define prewake_activity prewake_activity_c
+#define prewake_trigger prewake_trigger_c
+#include "../bench/prewake/c/prewake.c"
+#undef prewake_hint
+#undef prewake_activity
+#undef prewake_trigger
+typedef struct prewake_probe { unsigned calls; uint64_t budget; int warm_error,spin_error; } prewake_probe;
+static int prewake_probe_warm(void *ctx)
+{ prewake_probe *p=ctx; p->calls++; return p->warm_error; }
+static int prewake_probe_spin(void *ctx,uint64_t ns)
+{ prewake_probe *p=ctx; p->budget=ns; return p->spin_error; }
+int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);
+int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
+{
+    int (*hint[3])(prewake_state *,const plat_event *,uint64_t,const prewake_ops *)={prewake_hint_a,prewake_hint_b,prewake_hint_c};
+    void (*activity[3])(prewake_state *,uint64_t)={prewake_activity_a,prewake_activity_b,prewake_activity_c};
+    prewake_state states[3]={{0},{0},{0}};
+    uint64_t model[3]={0},now=0;
+    const uint64_t deltas[]={0,1,UINT64_C(14999999999),UINT64_C(15000000000),UINT64_C(15000000001)};
+    const uint32_t keys[]={XKB_KEY_Shift_L,XKB_KEY_Shift_R,XKB_KEY_Control_L,XKB_KEY_Control_R,
+        XKB_KEY_Alt_L,XKB_KEY_Alt_R,XKB_KEY_Super_L,XKB_KEY_Super_R,XKB_KEY_ISO_Level3_Shift,XKB_KEY_a};
+    for (size_t i=0;size-i>=5;i+=5) {
+        uint64_t delta=deltas[data[i]%5u];
+        now=(data[i]&128u) ? (now>=delta ? now-delta : 0) : now+delta;
+        uint8_t kind=data[i+1]%5u,key=data[i+3]%10u;
+        plat_event event={.kind=kind==0 ? PLAT_EV_FOCUS : kind==1 ? PLAT_EV_MOTION :
+            kind==2 ? PLAT_EV_KEY : kind==3 ? PLAT_EV_BUTTON : PLAT_EV_EXPOSE,
+            .focused=(data[i+2]&1u)!=0,.press=(data[i+2]&2u)!=0,
+            .repeat=(data[i+2]&4u)!=0,.keysym=keys[key]};
+        bool trigger=(kind==0 && event.focused) || kind==1 ||
+            (kind==2 && event.press && !event.repeat && key<9);
+        for (unsigned v=0;v<3;v++) {
+            if (data[i+4]&1u) { activity[v](&states[v],now); model[v]=now; }
+            bool eligible=v!=0 && now>=model[v] && now-model[v]>=UINT64_C(15000000000) && trigger;
+            prewake_probe p={.warm_error=(data[i+4]&2u) ? -7 : 0,.spin_error=(data[i+4]&4u) ? -9 : 0};
+            prewake_ops ops={&p,prewake_probe_warm,prewake_probe_spin};
+            int expected=eligible ? (p.warm_error ? p.warm_error : p.spin_error) : 0;
+            EDIT_ASSERT(hint[v](&states[v],&event,now,&ops)==expected);
+            EDIT_ASSERT(p.calls==(eligible ? 1u : 0u));
+            uint64_t budget=eligible && !p.warm_error ? (v==1 ? UINT64_C(200000) : UINT64_C(100200000)) : 0;
+            EDIT_ASSERT(p.budget==budget);
+            if (eligible) model[v]=now;
+            EDIT_ASSERT(states[v].last_activity_ns==model[v]);
+        }
+    }
+    return 0;
+}
diff --git a/tests/prewake_test.c b/tests/prewake_test.c
new file mode 100644
index 0000000000000000000000000000000000000000..b0b993955ed61e13685119f452c99581290c3f85
--- /dev/null
+++ b/tests/prewake_test.c
@@ -0,0 +1,55 @@
+/* Collect the existing per-candidate contract into make all/check. */
+#define PREWAKE_VARIANT 0
+#define PREWAKE_IMPL "a/prewake.c"
+#define main prewake_test_a
+#define prewake_hint prewake_hint_a
+#define prewake_activity prewake_activity_a
+#define fake prewake_fake_a
+#define warm prewake_warm_a
+#define spin prewake_spin_a
+#include "../bench/prewake/contract.c"
+#undef PREWAKE_VARIANT
+#undef PREWAKE_IMPL
+#undef main
+#undef prewake_hint
+#undef prewake_activity
+#undef fake
+#undef warm
+#undef spin
+
+#define PREWAKE_VARIANT 1
+#define PREWAKE_IMPL "b/prewake.c"
+#define main prewake_test_b
+#define prewake_hint prewake_hint_b
+#define prewake_activity prewake_activity_b
+#define prewake_trigger prewake_trigger_b
+#define fake prewake_fake_b
+#define warm prewake_warm_b
+#define spin prewake_spin_b
+#include "../bench/prewake/contract.c"
+#undef PREWAKE_VARIANT
+#undef PREWAKE_IMPL
+#undef main
+#undef prewake_hint
+#undef prewake_activity
+#undef prewake_trigger
+#undef fake
+#undef warm
+#undef spin
+
+#define PREWAKE_VARIANT 2
+#define PREWAKE_IMPL "c/prewake.c"
+#define main prewake_test_c
+#define prewake_hint prewake_hint_c
+#define prewake_activity prewake_activity_c
+#define prewake_trigger prewake_trigger_c
+#define fake prewake_fake_c
+#define warm prewake_warm_c
+#define spin prewake_spin_c
+#include "../bench/prewake/contract.c"
+#undef main
+
+int main(void)
+{
+    return prewake_test_a() || prewake_test_b() || prewake_test_c();
+}
diff --git a/tools/test_prewake_collection.sh b/tools/test_prewake_collection.sh
new file mode 100644
index 0000000000000000000000000000000000000000..8ba1d7f63adf5282d18350b87d678d90298adbc8
--- /dev/null
+++ b/tools/test_prewake_collection.sh
@@ -0,0 +1,13 @@
+#!/bin/sh
+# The common bench must not require ignored, uncollected experiment sources.
+set -eu
+cd "$(dirname "$0")/.."
+for source in prewake.h contract.c run.py summarize.py a/prewake.c b/prewake.c c/prewake.c; do
+    if [ ! -f "bench/prewake/$source" ]; then
+        echo "FAIL: missing collectible bench/prewake/$source" >&2; exit 1
+    fi
+done
+if [ ! -f fuzz/prewake_fuzz.c ]; then
+    echo 'FAIL: missing prewake state-machine fuzzer' >&2; exit 1
+fi
+echo 'prewake collection: common API, contracts, runner, summarizer and A/B/C present'
diff --git a/tools/test_prewake_display.sh b/tools/test_prewake_display.sh
new file mode 100644
index 0000000000000000000000000000000000000000..46fa5d769246d7a4f244ec8ebcf04294ae61902a
--- /dev/null
+++ b/tools/test_prewake_display.sh
@@ -0,0 +1,17 @@
+#!/bin/sh
+# Exercise coordinator opt-in on :99 only; check mode opens no window.
+set -eu
+cd "$(dirname "$0")/.."
+exe=build/prewake/prewake-a
+env DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check --real-display
+if env -u EDIT_ALLOW_REAL_DISPLAY DISPLAY=:99 EDIT_DISPLAY=:99 "$exe" --display-check --real-display; then
+    echo 'FAIL: real-display mode accepted without opt-in' >&2; exit 1
+fi
+if env DISPLAY=:99 EDIT_DISPLAY=:98 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check --real-display; then
+    echo 'FAIL: real-display mode accepted mismatched displays' >&2; exit 1
+fi
+if env DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check; then
+    echo 'FAIL: default mode accepted real-display opt-in' >&2; exit 1
+fi
+env -u EDIT_ALLOW_REAL_DISPLAY DISPLAY=:99 EDIT_DISPLAY=:99 "$exe" --display-check
+echo 'prewake display: explicit opt-in, matching displays, safe default PASS (no windows)'
diff --git a/tools/test_prewake_idle.sh b/tools/test_prewake_idle.sh
new file mode 100644
index 0000000000000000000000000000000000000000..03d2a0446428c2f2803a84a81916bfb14afbbfd4
--- /dev/null
+++ b/tools/test_prewake_idle.sh
@@ -0,0 +1,14 @@
+#!/bin/sh
+# Regression: the experiment must measure no-hint idle rather than infer it.
+set -eu
+cd "$(dirname "$0")/.."
+export DISPLAY=:99 EDIT_DISPLAY=:99
+unset EDIT_ALLOW_REAL_DISPLAY
+out=build/prewake/evidence-s8
+mkdir -p "$out"
+for variant in a b c; do
+    printf 'I\n' | "build/prewake/prewake-$variant" --protocol > "$out/idle-$variant.log" 2>&1
+    cat "$out/idle-$variant.log"
+    rg '^G11 .*wakeups=0 hint_events=0 warmups=0 ' "$out/idle-$variant.log" > /dev/null
+done
+echo 'prewake idle: all variants zero no-hint UI wakeups'
diff --git a/variants/P3.7/run.py b/variants/P3.7/run.py
index 1379b6169a236dc187e98e91e85be86c6811d09d..19550c1bd36ee425e69d5248fec8f1e0cba3a428
--- a/variants/P3.7/run.py
+++ b/variants/P3.7/run.py
@@ -15,12 +15,19 @@
     parser.add_argument('--trials', type=int, default=200)
     parser.add_argument('--idle-seconds', type=float, default=15.0)
     parser.add_argument('--san', action='store_true', help='use build-san binaries; validation only')
+    parser.add_argument('--real-display', action='store_true', help='coordinator only: requires explicit matching display and EDIT_ALLOW_REAL_DISPLAY=1')
+    parser.add_argument('--check-idle', action='store_true', help='measure an eleven-second no-hint UI-loop/CPU row per variant before trials')
     parser.add_argument('--out', default='variants/P3.7/evidence/trials.log')
     args = parser.parse_args()
     if not 1 <= args.trials <= 200 or args.idle_seconds < 0:
         parser.error('trials must be 1..200 and idle nonnegative')
-    env = dict(os.environ, DISPLAY=':99', EDIT_DISPLAY=':99')
-    env.pop('EDIT_ALLOW_REAL_DISPLAY', None)
+    if args.real_display:
+        env = dict(os.environ)
+        if env.get('EDIT_ALLOW_REAL_DISPLAY') != '1' or not env.get('DISPLAY') or env.get('DISPLAY') != env.get('EDIT_DISPLAY'):
+            parser.error('real-display mode requires EDIT_ALLOW_REAL_DISPLAY=1 and matching DISPLAY/EDIT_DISPLAY')
+    else:
+        env = dict(os.environ, DISPLAY=':99', EDIT_DISPLAY=':99')
+        env.pop('EDIT_ALLOW_REAL_DISPLAY', None)
     processes = {}
     with open(args.out, 'x', buffering=1) as log:
         def emit(line):
@@ -32,7 +39,8 @@
         try:
             for variant in 'abc':
                 exe = Path(f'build/prewake/prewake-{variant}{"-san" if args.san else ""}')
-                proc = subprocess.Popen([str(exe), '--protocol'], env=env,
+                command = [str(exe), '--protocol'] + (['--real-display'] if args.real_display else [])
+                proc = subprocess.Popen(command, env=env,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=log, text=True, bufsize=1)
                 processes[variant] = proc
@@ -40,6 +48,14 @@
                 if not ready.startswith('READY '):
                     raise RuntimeError(f'{variant}: initialization failed ({ready!r})')
                 emit(f'variant={variant} binary_bytes={exe.stat().st_size} {ready}')
+            if args.check_idle:
+                for variant, proc in processes.items():
+                    proc.stdin.write('I\n')
+                    proc.stdin.flush()
+                    row = proc.stdout.readline().rstrip()
+                    if not row.startswith('G11 ') or 'wakeups=0 hint_events=0 warmups=0 ' not in row:
+                        raise RuntimeError(f'{variant}: no-hint idle failed ({row!r})')
+                    emit(f'variant={variant} {row}')
             for trial in range(1, args.trials + 1):
                 for condition in 'NH':
                     for variant in 'abc':
@@ -60,7 +76,7 @@
                 proc.stdin.close()
                 if proc.wait() != 0:
                     raise RuntimeError(f'{variant}: nonzero exit')
-            emit('DONE all samples collected; no gate verdict on shared Xvfb')
+            emit('DONE all samples collected; proxy only, no gate verdict or variant choice')
         finally:
             for proc in processes.values():
                 if proc.poll() is None:

---

## Final message (codex -o)

Collected the existing A/B/C variants; C uses busy-wait, with no uclamp implementation. Build, sanitizer suite, fuzz, and active allocator checks passed. No-hint UI wakeups stayed zero (M)[AC][xvfb, indicative only]. No variant selected.

[Report with inventory and red/green evidence](/home/tobias/Projects/sublimite/.wt/edit-zzj.8/docs/worker-reports/edit-zzj.8-s8.md) · [DRAFT real-display command and trial plan](/home/tobias/Projects/sublimite/.wt/edit-zzj.8/docs/decisions/edit-zzj.8.md)

LeakSanitizer was disabled. Coordinator must rerun leaks enabled and validate against the advanced main. No git writes.