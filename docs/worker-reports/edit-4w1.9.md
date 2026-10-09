1. **Summary — implemented P1.9.** Files touched:

   - [journal.h](/home/tobias/Projects/editor/.wt/edit-4w1.9/src/journal/journal.h): public API and ownership contract.
   - [journal.c](/home/tobias/Projects/editor/.wt/edit-4w1.9/src/journal/journal.c): bounded appends, worker sync, replay, base checks, atomic rotation.
   - [journal_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.9/tests/journal_test.c): corruption, torn-page, rotation, allocation and back-pressure tests.
   - [journal_kill_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.9/tests/journal_kill_test.c): seeded SIGKILL/model comparison.
   - [journal_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.9/fuzz/journal_fuzz.c): raw parsing, arbitrary schemas and differential replay.
   - [journal_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.9/bench/journal_bench.c): append gates, full piece-tree replay and sync cadence.
   - [P1.9.md](/home/tobias/Projects/editor/.wt/edit-4w1.9/docs/decisions/P1.9.md): design, integration contract and evidence.

2. **RED run:**

   ```text
   journal_test: FAIL round_trip: journal_open == JOURNAL_OK (got 2)
   ```

3. **GREEN runs:**

   ```text
   journal_test: malloc_guard=active append_allocations=0
   journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
   journal_kill_test: ok trials=1000 script_edits=100000 issued=49483074 replayed=47362458 acknowledged=47175064 (all replay prefixes within [ack,issued])
   elapsed_seconds=231.63 (M)[AC]
   ```

   Default `make check` runs 50 trials; the documented long mode is `--trials=1000`.

4. **Verification output:**

   Full ASan/UBSan check used isolated Xvfb with `-noreset`, local socket access, and `ASAN_OPTIONS=detect_leaks=0`.

   ```text
   check: 16 test binaries passed
   ```

   ```text
   power=Charging [AC]; indicative: concurrent workers may be compiling
   BENCH name=journal_append_1B_(M)_ns n=100000 p50=297 p99=362 ci95=[297,297] gate_p50=0 gate_p99=20000 pass=1 power=[AC]
   TRACK journal_replay mode=piece_tree payload=1 records=100000 MB_s=15.57 records_s=353850 (M)[AC]
   TRACK journal_sync payload=1 syncs=98 max_bytes=45056 last_bytes=28672 max_interval_ms=3.595 (M)[AC] threshold=64KiB_or_1s
   BENCH name=journal_append_1KiB_(M)_ns n=100000 p50=5468 p99=6709 ci95=[5468,5469] gate_p50=0 gate_p99=20000 pass=1 power=[AC]
   TRACK journal_replay mode=piece_tree payload=1024 records=100000 MB_s=116.21 records_s=106807 (M)[AC]
   TRACK journal_sync payload=1024 syncs=2344 max_bytes=65536 last_bytes=36864 max_interval_ms=10.199 (M)[AC] threshold=64KiB_or_1s
   TRACK journal_idle_sync interval_ms=1001.415 bytes=4096 (M)[AC]
   ```

   ```text
   fuzz: 5 fuzzers built
   #69189 DONE cov: 1431 ft: 5219 corp: 154/10826b lim: 701 exec/s: 229 rss: 152Mb
   Done 69189 runs in 301 second(s)
   ```

   No fuzz sanitizer or assertion findings.

5. **Numbers against gates:** Append p99 was **0.362 µs / 6.709 µs (M)[AC]**, both below **20 µs (G)**. Allocator hook counted **zero append allocations (M)[AC]**. Replay throughput and sync cadence are TRACK measurements. Bench numbers remain indicative; coordinator should rerun on a quiet box.

6. **Decisions:** CRC32C protects headers, payloads and padding. Two arena-backed buffers feed immutable worker snapshots; partial pages wait until deadline or force. Changed bases return an explicit conflict. Saturation returns sticky `FULL`, preserving accepted records and refusing later ops until a complete checkpoint. Rotation replaces the whole session atomically after save. Details and alternatives are in [P1.9.md](/home/tobias/Projects/editor/.wt/edit-4w1.9/docs/decisions/P1.9.md).

7. **Out of scope findings:** Finished `work` slots can invalidate unread messages when reused; journal jobs reserve their slots until consumption. No work-module changes made. Display access and Xvfb reset behavior required test-environment adjustments. LeakSanitizer failed under ptrace; ASan/UBSan remained enabled. No unresolved permission or hardware dependency remains.

8. **Makefile changes needed:** None.