# P3.7 experiment — edit-zzj.8 collection, session 8

Collected existing A/B/C under this nonignored directory. A is no action; B is
a zero-instance GL draw/fence and 200 us spin (E, configured); C uses the
existing 100.2 ms total synchronous spin (E, configured). No sched/uclamp
variant, production adoption, API change or additional candidate was made.

The common bench remains `bench/prewake_bench.c`, privately including the
production renderer with renamed exports. Frames stop at swap/fence, not
native matching Present or optical completion. Counting guards cover submit;
native GL/XCB operations retain the existing IO exemption.

`tests/prewake_test.c` collects all existing candidate contracts into make
all/check. `fuzz/prewake_fuzz.c` independently models hints, modifier variants,
repeats, activity, threshold boundaries, reversed clocks, warm/spin failures
and debounce for all candidates. No globals or allocations were added to the
candidate implementations.

New bench `I` protocol first settles for the required 15 s app idle (G, workload),
then measures an eleven-second no-event native UI-loop row
(E, configured), with hint dispatch enabled and blink/repeat disabled. It fails
on any native event, UI wakeup or warm-up; all-thread process CPU is also logged.
This is the experiment's idle row, not a complete editor/driver G11 verdict.

Verification commands from the repo root:

```sh
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
env DISPLAY=:99 EDIT_DISPLAY=:99 python3 bench/prewake/run.py \
  --trials 2 --idle-seconds 15 --out build/prewake/smoke-new.log
python3 bench/prewake/summarize.py --partial build/prewake/smoke-new.log
```

Socket access needs sandbox review. The display guard test is window-free;
every worker bench window is on :99. `--partial` prints a descriptive table
without full-run validation; the s8 smoke's count/order/idle/allocation checks
were also asserted separately. Output logs refuse overwrites.

Historical full trials remain in ignored `variants/P3.7/evidence/`: interrupted
at 559 samples (M)[AC], recorded count, no DONE. They are not a finished comparison.
Current smoke raw evidence is public in
`docs/worker-reports/edit-zzj.8-s8-smoke.log`; complete commands, current results
and red/green are in `docs/worker-reports/edit-zzj.8-s8.md`.
`docs/decisions/edit-zzj.8.md` is DRAFT with the coordinator's exact real-display
command and requested trial plan. No variant is selected from Xvfb.

Remaining: announced hardware run, native/optical endpoints, actual blink CPU,
driver-thread wakeups, false positives/no-key hints, power-state residency and
energy. Cold-cache work and new policies are outside this collection bead.
The coordinator must rerun leak detection; worker checks disable LSan.

Final s8 verification: make all / make fuzz exit 0; make check passes 49
sanitizer test binaries plus replay CLI (M)[AC], leaks disabled. Dedicated
prewake fuzzer passes the requested minute: 1314833 runs in 61 s (M)[AC].
All three post-idle G11 UI rows have zero wakeups, hint dispatches and warm-ups
(M)[AC][xvfb, indicative only]. Active release editor typing guard is zero.
