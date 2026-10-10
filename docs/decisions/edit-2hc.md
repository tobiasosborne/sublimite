# edit-2hc — load-qualified scan benchmark verdicts

The scan throughput gates remain unchanged: primitive count and nth-newline
p50/p99 >=12/12 GB/s (G); count's G6 basis >=16/12 GB/s (G).
`perf/00-hardware-profile.md` records SSE2 at 16.7–17.3 GB/s (M)[AC], single
thread, warmed memory, with the powersave governor. `P1.2.md` selected the SSE2
implementation; `P1.2b.md`'s AC rerun with no agent workers and load1=1.06 (M)[AC]
measured count at 16.06/15.87 GB/s (M)[AC]. `P1.2c.md` added the tail and basis
guards. These are primitive regression guards, not end-to-end G6/G7 acceptance.

Use the bead's explicit absolute cutoff, load1 >4 (G), rather than inventing a
core-count fraction absent from the hardware profile. Read load1 immediately
before and after each kernel's sample loop, outside timed samples. Print both
values on every row. The G6 basis row reuses count's observations, so it also
reuses count's load interval and power stamp.

Add `BENCH_LOADED` to the existing verdict enum without changing its old values.
Load qualification is opt-in: legacy harness callers retain their gate policy.
Either endpoint above the cutoff makes valid timing observations LOADED, whether
the throughput would otherwise pass or miss. It returns exit 3, like REFUSED.
Unknown load evidence returns REFUSED, also exit 3. Exactly 4 remains eligible
for the existing throughput comparison. Existing explicit TRACK opt-out semantics
are preserved in `bench_exit_code`; scan uses the default nonzero exit policy.

Correctness failures and invalid throughput samples remain failures under load.
Merge row exits with `bench_merge_exit`, preserving exit 3 rather than coercing
it to a boolean or combining codes with bitwise OR. Measurement results and
red/green evidence are in `docs/worker-reports/edit-2hc-s9.md`.

No scan hot path, typing-path allocation, or global storage changes. No quiet
window was sought, and no loaded timing is used to tune code or waive a gate.
