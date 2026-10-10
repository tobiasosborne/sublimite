# edit-2j9 fix: savectl_fuzz multichunk_reload model failure after rebase

## Cause
`fuzz/savectl_fuzz.c:416` (`block[i] == first`) failed on input `0xaf` (1 byte, chunks=3, no 0x10 bit,
so the "deterministic coherent rewrite" branch). It is NOT a v2 BASE producer problem and not a
savectl defect: the model was wrong about what is detectable.

That branch rewrites every chunk 'a'->'b' after the reload has started reading and restores mtime with
futimens, so the only evidence of the rewrite is ctime. savectl (savectl.c:286) and file_id compare
ctime, which is correct. But ctime comes from the kernel coarse (jiffy) clock, so when the fuzzer
creates the file and rewrites it within the same tick, ctime is bit-identical, the rewrite is truly
undetectable, and the reload legitimately returns a mixed a/b replacement. Pre-migration the
oracle did not depend on ctime in the same way; with ctime in the identity the model must guarantee
that ctime moves. The failure is a timing flake (single replay passes; `-runs=1500` on the saved
input reproduced it:
`reload mismatch: input=af off=1048576 i=0 first=a actual=b chunks=3` then
`fuzz/savectl_fuzz.c:428: assertion failed: block[i] == first`).

## Change (fuzz/savectl_fuzz.c only; src/savectl untouched)
- Discarded the previous attempt's uncommitted edit (the `intermediate` writer generation plus a
  stderr debug print): it never ran for the failing input (size 1, so `data[3]` was not even
  available) and did not address the cause. File restored from HEAD, then one change applied.
- In the deterministic-rewrite branch, before the first rewrite, spin (sched_yield) until
  `CLOCK_REALTIME_COARSE` is strictly past the captured `st.st_ctim`, so every rewrite moves ctime
  and the reload must report FILE_ERR_CHANGED or read a coherent snapshot.

## Runs
Failing (before, from build/coord/savectl_fuzz_failure.log):
`fuzz/savectl_fuzz.c:416: assertion failed: block[i] == first` / `ERROR: libFuzzer: deadly signal`
(replay with -runs=1500 of crash-e27bd510...: the mismatch line above).

After:
- `make all`: exit 0 (gcc, -Werror). `make build/fuzz/savectl_fuzz`: ok.
- `-runs=3000 crash-e27bd5104a595844730f44df571de7da07951ffb`: `Executed ... in 51554 ms`, clean.
- `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64`:
  `Done 2506 runs in 61 second(s)`, exit 0.
- `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test`:
  `savectl_test: ok`, exit 0.
- `build/bench/savectl_bench` (no args file exists; the full 1GiB rows need n=10000 each, so it was
  cut by `timeout 200` after the 1MiB rows; exit 124; log build/coord/edit-2j9-s9-fix-bench.log).
  Loaded box, power=Not charging, load1=2.69 (M), not gate evidence; bench code is unchanged here:
  - savectl_snapshot_enqueue_1MiB_regression p50=8846 ns verdict=PASS
  - G8d_1MiB_durable p50=12085254 ns gate_p50=10000000 verdict=MISS (loaded box, fsync-dominated;
    fuzz-only change cannot affect it; re-measure on the coordinator's schedule)
  - journal_saved_1MiB p50=18563772 ns verdict=PASS
  Earlier n=1 TRACK-only run of all nine rows is in build/coord/savectl_fix_bench.log.

## Summary for the coordinator
1. Cause: fuzz model flaw, not a savectl defect; mtime-restoring rewrite is only visible via ctime, which is jiffy-coarse, so same-tick create+rewrite is undetectable.
2. Fix: wait for the coarse clock to pass the captured ctime before the rewrite (fuzz/savectl_fuzz.c only); previous uncommitted edit discarded.
3. savectl_fuzz 60 s full run exit 0 (2506 runs); saved crash input replays clean 3000x.
4. make all exit 0; savectl_test ok on :99.
5. savectl_bench: 1MiB rows ran (G8d_1MiB_durable MISS 12.1 vs 10 ms on a loaded box, unrelated to this change); 1GiB rows cut by timeout.
