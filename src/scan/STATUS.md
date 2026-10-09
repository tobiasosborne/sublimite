# Scan status

P1.2 SSE2 implementation remains unchanged. P1.2c (edit-4w1.50) fixes review
coverage and benchmark failures in work-scan-1 §§10–13,16; design and red/green
lines are in docs/decisions/P1.2c.md.

Done: exact heap bounds at all 16 alignments; protected-page differential tests
for lengths 0..8192 and empty inaccessible ranges; 8 MiB+17 correctness cases;
scalar differential scan fuzzer with guarded ranges, split/chunk checks, all byte
values, and extreme ranks. No implementation overread found. Scratch-only
one-byte-overread mutants are caught by guarded tests and the fuzzer.

Benchmark: independent generation-time oracle and validation of every timed
result; >=12 GB/s p50/p99 primitive gates and a >=16 GB/s p50 scan_count G6-basis
row (p99 >=12 GB/s). Rows can fail and affect exit status. TRACK on loaded boxes;
primitive cached-buffer results do not close the end-to-end G6/G7 gates.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99: make all; make check (clang ASan/UBSan;
sandbox LSan needs ASAN_OPTIONS=detect_leaks=0); make fuzz; then
build/fuzz/scan_fuzz <seed-directory> -max_total_time=120 -max_len=131089.
Bench contract checks: build/bench/scan_bench --self-check-tail and
--self-check-oracle. Performance runs only once as authorized, with power/load
stamp. Verification: make all passed; make check passed all 34 binaries and replay CLI
checks on :99 outside the socket-restricted sandbox (detect_leaks=0); make fuzz
built 21 fuzzers. Seeded scan campaign completed 33,993 cases in 121 s for the
requested 120 s budget, no findings, (M)[AC] load1=5.91. Full evidence: P1.2c.md.

Single scan TRACK run: scan_count p50/p99 4.36/3.29 GB/s and nth 5.25/4.93 GB/s
(M)[AC], load1=8.35. Primitive and 16 GB/s basis thresholds all missed; exit=1.
No loaded-box gate verdict or rerun. Single work TRACK run passed its logical,
CPU-slice and suppression checks (M)[AC], load1=7.79.

Missing/outside scope: quiet-box gate verdicts and cached-file/new-mapping
end-to-end G6/G7 verification. Work-test deadline proposal belongs to P1.8c;
no files under src/work are modified by this bead.
