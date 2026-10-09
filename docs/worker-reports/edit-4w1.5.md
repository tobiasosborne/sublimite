Implemented P1.5 and verified it. **Strict whole-group NOMEM atomicity remains unmet:** the frozen piece API lacks transactions. Replay reports partial progress and supports retry.

**1. Summary**

- [undo.h](/home/tobias/Projects/editor/.wt/edit-4w1.5/src/undo/undo.h): contract, grouping, cap, cursor state and batch APIs.
- [undo.c](/home/tobias/Projects/editor/.wt/edit-4w1.5/src/undo/undo.c): pooled inverse records and redo.
- [undo_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.5/tests/undo_test.c): adversarial tests, including allocation failures and eight-span capture.
- [undo_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.5/fuzz/undo_fuzz.c): independent full-copy history model.
- [undo_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.5/bench/undo_bench.c): TRACK, bookkeeping and optional B+ tree measurements.
- [P1.5.md](/home/tobias/Projects/editor/.wt/edit-4w1.5/docs/decisions/P1.5.md): decisions, limitations and complete outputs.

**2. RED run**

```text
undo_test:14: FAIL undo_init(u,*t,cap)==0
```

**3. GREEN run**

```text
undo_test: replay allocator failures=8 partial_groups=4 (resumed)
undo_test: 10000 keys mallocs=0 guard=active bytes/record=64
undo_test: all passed
```

Also passed under ASan/UBSan and against the read-only B+ tree variant.

**4. Verification**

```text
check: 15 test binaries passed
fuzz: 5 fuzzers built
Done 406855 runs in 311 second(s)
Done 151660 runs in 311 second(s)
```

Both fuzz runs exited zero without findings. Full check used `ASAN_OPTIONS=detect_leaks=0` and unset DISPLAY; optional live-X tests skipped.

Final bench output, power stamp `Discharging`:

```text
MEMORY (M)[bat] live=640000 records=10000 bytes/record=64 reserved=643072 fixed_scratch=512 gate=(G)64 B/record
BENCH name=undo_10k_in_tree_TRACK n=31 p50=334826948 p99=620263664 ci95=[176425351,402471452] gate_p50=0 gate_p99=0 pass=1 power=[bat]
BENCH name=undo_10k_bookkeeping_mock_G n=31 p50=1902921 p99=5285031 ci95=[1657909,2267942] gate_p50=6300000 gate_p99=6300000 pass=1 power=[bat]
BENCH name=undo_10k_bptree_INDICATIVE_M n=31 p50=8351321 p99=14083261 ci95=[8016022,8967023] gate_p50=0 gate_p99=0 pass=1 power=[bat]
```

**5. Numbers against gates**

| Measurement | Result | Gate/status |
|---|---:|---|
| Bookkeeping, 10k groups | (M)[bat] 1.903 / 5.285 ms p50/p99 | ≤6.3 ms (G): pass |
| Retained span record | (M)[bat] 64 B | ≤64 B (G10f): pass |
| 10k-key libc allocations | (M)[AC] 0 | Law 2: pass |
| Flat kernel | (M)[bat] 334.827 / 620.264 ms | TRACK |
| B+ tree variant | (M)[bat] 8.351 / 14.083 ms | Indicative; excludes rendering |

Measurements were under concurrent worker load; quiet-box synthesis reruns remain necessary.

**6. Decisions**

One contiguous add span per 64-byte record; fragmented edits use multiple records in one group. Base pool storage, stable piece refs, whole-group eviction, opaque 16-byte cursor blobs, and one merged dirty interval per batch. Documented in P1.5.md.

**7. Out-of-scope findings**

Strict group rollback requires a piece transaction/reservation API or coordinator acceptance of resumable replay. LeakSanitizer fails under ptrace; live-X initialization failed in this runner. Frozen modules were unchanged.

**8. Makefile changes**

None.