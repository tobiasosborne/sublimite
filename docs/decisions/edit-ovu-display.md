# edit-ovu — displayed scroll evidence consumer

Scope: the remaining displayed-cadence finding in
`docs/reviews/P4-modules-2.md`, alongside the integration contract in
`docs/decisions/P3.4.md`. The prior scroll adapters stay unchanged; this
session adds the consumer and its regression, not a backend refresh producer.

`bench/scroll_display.h` is a caller-owned, allocation-free refresh-stream
validator. `scroll_display_run` accepts a driver callback that yields each
refresh opportunity; `scroll_display_observe` also permits direct use from a
future editor observer. The state holds only accumulated diagnostics and the
preceding identity/timestamps. It adds no editor scheduling or frame waits.
Reporting is benchmark-side I/O, outside the typing path.

The required population is `SCROLL_DISPLAY_REFRESHES`: **(G) ten thousand
refreshes**, copied from the G3z row in `perf/01-perf-target.md` and the assigned
review finding. Allowed misses are **(G) zero**, from the same row. These are
evidence eligibility and regression-fixture requirements, not a new timing
budget or milestone blocker. The detector has no fixed half-period cutoff.
Each missed sequence contributes `next_sequence - previous_sequence - 1`;
each invalid observed opportunity contributes one miss (G: a frame identity
must be correct at every opportunity). Multiple failed checks on the same
opportunity contribute only that opportunity's miss.

A qualifying opportunity must have an actual completed frame ID that matches
the oracle's expected frame. It must carry immutable input range, source and
geometry generations, the complete fractional scroll plan, wrapped seed,
sidebar revision/content identity, and original submit timestamp. The actual
completion must follow submission and fall no later than the opportunity's
deadline. Refresh sequence, opportunity time and completion time must advance;
displayed identity must change during this active-motion test. Captured text
and sidebar pixels must match independent expected output, including the
translated lower edge. At least one valid subpixel plan is required (G:
the review explicitly requires fractional-motion coverage). Comparisons are
fieldwise and do not depend on C struct padding.
Whole-pixel subrow translation cannot satisfy that coverage: its Q8 origin
has no remainder modulo `SCROLL_PIXEL_UNIT`.

Unsupported capability is UNAVAILABLE. Synthetic observations can demonstrate
a MISS, but a clean synthetic stream is UNAVAILABLE for displayed G3z.
Incomplete physical populations or missing fractional coverage are REFUSED.
A null driver is explicitly unsupported. Neither null counters nor submitted
cell contents can supply physical completion or captured-pixel evidence.

## Handoff to edit-3kk

Implement the public observer specified in `docs/decisions/P3.4.md` and bind
its observations to `scroll_display_observation`. The frame metadata must be
captured when submitted and retained in setup-reserved storage until actual
completion. Look it up by the actual displayed ID, including repeated old IDs;
do not relabel completions with the latest viewport. Maintain the expected
input/source/geometry/sidebar state independently from that lookup. Supply
pixel verification from captured backend output, not the submitted grid.
Record missing completions and failed presents as opportunities, and report
terminal unsupported capability explicitly. A sequence gap detects a missing
callback, but cannot recover unreported terminal opportunities; termination
before the required observed population refuses qualification.

The editor owner also retains fractional origin/overscan/hit-test wiring and
the resident exact wrap-metadata producer described in the prior worker report.
No editor source is edited here. No edit-3kk agent is available in this session;
this checked-in contract and HANDOFF entry are the coordination artifact.

`scroll_bench --display-check` prints capability without requiring the large
log corpus. It is currently UNAVAILABLE because this tree has no public
refresh hook or immutable scroll/sidebar frame metadata. Normal scroll bench
output retains the CPU/synthetic-null proxy and prints the same explicit
unsupported displayed result. Its existing sampling/exit policy is unchanged;
the separate bench-policy bead owns that policy.

`tests/scroll_display_test.c` drives the required synthetic population, repeats
every second displayed frame, and proves unchanged submit/present counters
cannot conceal the failure. Expected repeated misses derive as
`SCROLL_DISPLAY_REFRESHES / (1 + 1)` (G: review's every-second counterexample).
Additional fixtures independently corrupt the observation contract fields,
omit callbacks/completions, reverse ordering, check synthetic/unsupported
handling, and exercise the driver/reporting path. Synthetic timestamps are
ordinal fixture ticks, not performance measurements.

Real displayed G3z remains unverified until the editor/backend producer and
captured-output driver exist and the coordinator runs the integrated test.
The sandbox cannot access the X socket. The subsequent Astra comparison drive
must verify the integrated editor build before handover to Tobias.
