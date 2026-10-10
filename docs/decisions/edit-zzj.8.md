# edit-zzj.8 — DRAFT: predictive wake experiment collection

No variant is selected or promoted. This decision is **DRAFT**, awaiting an
announced coordinator run on a hardware display. Xvfb timings establish only
that the experimental protocol works. The historical P3.7 document describes
the previous session; this document and the s8 worker report supersede its
claims of an ongoing full run and its old source locations.

The existing A/B/C implementations are collected under `bench/prewake/` so a
normal source collection includes them. No additional variant was created.
Production source, public production APIs and the Makefile are unchanged.

| Candidate | Existing behavior after an eligible idle hint |
|---|---|
| A | No action |
| B | Bind retained render state, zero-instance GL draw, flush/fence, synchronous 200 us spin (E, configured) |
| C | B with a total synchronous 100.2 ms spin (E, configured); busy-wait alternative, no sched/uclamp implementation |

Eligible hints are focus-in, motion, and a non-repeated modifier press after
15 s (G, workload) of recorded inactivity. Successful dispatch, including a
failed warm-up, consumes the debounce interval. This existing policy was kept.
At the configured 50 ms hint lead (E), C blocks a scheduled key for at least
50.2 ms (E) before frame work, even with instantaneous GL warm-up.

The shared bench draws the existing cached ASCII grid through production GL
snapshot/upload/draw/swap code, ending at a device fence. It excludes document
mutation, layout, minimap, native matching Present completion and optical
latency. It therefore reports a G3i-style proxy, never a G3i or G2b gate pass.
Hint wall time and all-thread process CPU time are separate from frame time;
scheduled-key latency includes oversleep and C's synchronous blocking cost.

The added `I` protocol row gives initialization a 15 s app-idle interval
(G, workload), then runs the existing
platform loop with hint dispatch enabled and blink/repeat disabled for 11 s
(E, configured), and records UI-loop iterations, native hint dispatches,
warm-up calls and all-thread process CPU time. The external observation timeout
is excluded by the platform's iteration counter. Any native event invalidates
the no-event workload and fails the row; it is never silently filtered.
This checks the experiment's no-event UI wakeups, not the complete editor's
blink budget or every driver-thread wakeup.

Default execution still requires matching `DISPLAY=:99 EDIT_DISPLAY=:99` with
no real-display opt-in. The coordinator mode requires both the explicit
`--real-display` argument and `EDIT_ALLOW_REAL_DISPLAY=1`, plus matching nonempty
display variables. `--display-check` tests these guards without opening a
window. This worker exercises only Xvfb and window-free guard tests.

## Coordinator command and trial plan — DRAFT, not executed

Only after announcing the batch to Tobias, restoring notification settings on
exit, and placing the windows on the internal panel, the coordinator runs:

```sh
sh tools/prewake_bench.sh build
env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 \
  build/prewake/prewake-a --display-check --real-display
env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 \
  python3 bench/prewake/run.py --real-display --check-idle \
  --trials 200 --idle-seconds 15 \
  --out docs/worker-reports/edit-zzj.8-real-trials.log
python3 bench/prewake/summarize.py docs/worker-reports/edit-zzj.8-real-trials.log
```

The command assumes the coordinator's announced hardware X session is `:0`;
verify that before running. It is a future coordinator command, never worker
authorization to open a real-display window. The output file must not exist:
the runner refuses overwrites. The worker did not execute this block.

Use 200 trials per candidate **per condition** (E, requested): A/N, B/N, C/N,
A/H, B/H, C/H, repeated with a fresh 15 s sleep (G, workload) before every
sample. Keep all renderers persistent, variants serial and comparisons back to
back on the same loaded AC box. Stamp power/load for every sample; record the
display geometry/refresh, hardware renderer, governor/EPP and compositor
settings. Do not wait for a quiet box or filter failures/page faults. The sleep
budget alone is 5 h (E), so arrange a batch that can finish without killing it.

Confirm a hardware renderer rather than llvmpipe, verify the no-op/fence actually
wakes the intended device, and measure no-key-following-hint cost and pointer
false positives. Preserve the initial 50 ms lead (E, configured) for the exact
existing comparison. Lead sweeps, sched/uclamp and editor integration require
separate scope; they are not implemented here.

The command above still measures the swap/fence **proxy** on real hardware.
Matching Present/refresh and optical endpoints, C-state residency, energy with
baseline/wrap handling, actual blink CPU, and false-positive event rates need
additional coordinator instrumentation before an adoption decision. Do not
infer joules from process CPU or fabricate T6. Cold cache is not measured.

## Verification

Current red/green, smoke rows, interrupted historical counts, active allocator
evidence and remaining limitations are recorded in
`docs/worker-reports/edit-zzj.8-s8.md`. No Xvfb ranking or gate verdict is made.
