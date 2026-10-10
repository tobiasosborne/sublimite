# edit-wvf — retain compact lineidx scratch; add paired scan comparison

Keep uint16 length-minus-one geometry and uint32 sealed results. The paired
measurements do not establish that edit-4w1.57 regressed the initial-build
kernel. Original prefaulted medians were current 81.367 ms and historical
81.439 ms (M)[AC]; later same-fixture rounds reverse relative ordering as raw
scan throughput changes. The deterministic G10f ownership reduction remains
binding; no scratch enlargement or production-kernel rewrite is justified.

Add `lineidx_bench --compare-scan` as a diagnostic TRACK variant. It alternates
build/raw-scanner ordering per round on one prefaulted mapping, verifies both
newline counts against the independent scalar oracle, and reports each sample's
power/load. It can be linked against the git-show historical object before the
current library without changing the scanner/support code. Creation, mmap and
cleanup are excluded; build request, worker and bounded UI adoption are charged.
Cold comparison is refused. Ordinary gate workload and verdicts are unchanged.

Fresh G7j's 30/50 ms (G) requires scanning beyond what the observed
15.65–16.75 GB/s (M)[AC] rates permit for its target: 61.75–57.69 ms (E) for
966,366,840 bytes. That is conditional on measured throughput, not a universal
hardware impossibility. G7 and partial seek have tighter margins but their raw
scan floors do not consistently prove them unreachable. No gate is changed;
Tobias owns that decision. Scan/editor and other worker-service optimization
remain outside this bead. Full transcripts and acceptance are in
`docs/worker-reports/edit-wvf-s9.md`.
