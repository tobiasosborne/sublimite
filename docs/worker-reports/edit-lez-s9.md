# edit-lez session 9 — worker report

This bead is **incomplete**: §§22, 25, 29 and 30 retain the gaps identified below.
Final verification is green: GCC 13 make all, Clang 18 ASan/UBSan make check,
and the requested findui fuzz campaign all exited 0. LeakSanitizer was disabled
and requires the coordinator rerun. No git mutations, bd, HANDOFF/worklog edits, editor-internal edits, frozen find-header edits, Makefile edits or new globals. All windows/tests use DISPLAY=:99.

## Finding outcomes and red/green evidence

All correctness transcripts below are (M)[AC]; configured capacities and byte bounds are (G). Each original regression was added and run before its corresponding fix.

### §21: Done

Default literals no longer compile regex atoms. Maximum-length ASCII and binary queries pass in both case modes.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S21 maximum literal: binary=0 case=0 complete=0 error=6 count=0
tests/findui_test.c:437: assertion failed: state.complete && state.match_count == 2 && state.cached_matches == 2
```

Green run: same targeted commands, exit 0.
```text
S21 maximum literal: binary=0 case=0 complete=1 error=0 count=2
S21 maximum literal: binary=0 case=1 complete=1 error=0 count=2
S21 maximum literal: binary=1 case=0 complete=1 error=0 count=2
S21 maximum literal: binary=1 case=1 complete=1 error=0 count=2
S21: PASS maximum ASCII/binary literals in both case modes
findui_test: all passed
```

### §22: Partial

Literal whole-word search uses persistent KMP state and one-byte SIMD counting. Rejected overlaps are tested. Whole-word regex still enumerates through next_budget; its persistent filtered visitor remains missing.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S22 word worker: count=524288 scans=1572864
tests/findui_test.c:435: assertion failed: __extension__ ({ __auto_type __atomic_load_ptr = (&f.hook.scans); __typeof__ ((void)0, *__atomic_load_ptr) __atomic_load_tmp; __atomic_load (__atomic_load_ptr, &__atomic_load_tmp, (5)); __atomic_load_tmp; }) <= 16
```

Green run: same targeted commands, exit 0.
```text
S22 word worker: count=524288 scans=1
S22: PASS bounded literal whole-word visitor and rejected overlap
findui_test: all passed
```

### §23: Done

Both literal and regex word searches publish only the bounded union of first cache, visible prefix and selected ordinal. Completion reports exact visible cardinality.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S23 word publication: regex=0 count=600 visible=600 ranges=600
tests/findui_test.c:436: assertion failed: f.published_ranges <= 8 + 1 + 1
```

Green run: same targeted commands, exit 0.
```text
S23 word publication: regex=0 count=600 visible=600 ranges=8
S23 word publication: regex=1 count=600 visible=600 ranges=8
S23: PASS bounded word publication with exact visible overflow
findui_test: all passed
```

### §24: Done at the findui endpoint

Bounded publication state retries with work_continue. A bulk probe starts with a saturated find mailbox, without cancellation or draining. This tests worker service rather than a fully integrated editor save/jump command; counting itself remains non-preemptible.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S24 saturated mailbox: next_bulk_started=0
tests/findui_test.c:437: assertion failed: __extension__ ({ __auto_type __atomic_load_ptr = (&serviced); __typeof__ ((void)0, *__atomic_load_ptr) __atomic_load_tmp; __atomic_load (__atomic_load_ptr, &__atomic_load_tmp, (5)); __atomic_load_tmp; })
```

Green run: same targeted commands, exit 0.
```text
S24 saturated mailbox: next_bulk_started=1
S24: PASS full mailbox yields sole bulk lane without cancellation/draining
findui_test: all passed
```

### §25: Partial

Completed counts, first cache and selection survive viewport changes. In-flight global counts are coalesced rather than restarted. One-byte literal window scans are local. Multi-byte literal/regex windows still scan source start, and window refresh waits for a running count.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S25 viewport: complete=1->0 count=512->0 selected=0->18446744073709551615
tests/findui_test.c:435: assertion failed: after.complete && after.match_count == before.match_count && after.match_index == before.match_index
```

Green run: same targeted commands, exit 0.
```text
S25 viewport: complete=1->1 count=512->512 selected=0->0
S25: PASS viewport retains exact count and selection
findui_test: all passed
```

### §26: Done

First and visible caches have independent limits. Tests and count bench verify the first required offsets under initial, disjoint late and empty windows.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
tests/findui_test.c:82: assertion failed: findui_init(&f->panel, &config) == FINDUI_OK
```

Green run: same targeted commands, exit 0.
```text
S26: PASS first 4096 offsets with initial, late and empty windows
findui_test: all passed
```

### §27: Done, with performance limitation

Paged immutable plans remove the cache refusal. Tests cover beyond-cache completion, cancellation, allocation failure and exact undo/redo. Pages rescan the source; a later no-op regression now has a failing unit test and separate page storage.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S27 beyond cache: mode=0 begin=5 count=5000
tests/findui_test.c:435: assertion failed: code == FINDUI_OK
```

Green run: same targeted commands, exit 0.
```text
S27 beyond cache: mode=0 begin=0 count=5000
S27 beyond cache: mode=1 begin=0 count=5000
S27 beyond cache: mode=2 begin=0 count=5000
S27: PASS paged replacement beyond cache, cancellation, fault and exact undo/redo
findui_test: all passed
```

### §28: Done for bounded byte work

Individual matches delete in bounded byte slices with resumable state and deadline checks before insertion. Slice-aware record admission precedes mutation. Large-match cancellation and complete undo/redo pass. OS wall-time overshoot remains possible.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S28 large match: code=0 replaced=1 remaining_bytes=1
tests/findui_test.c:437: assertion failed: code == FINDUI_MORE && replaced == 0 && piece_len(f.tree) >= sizeof original - 8192
```

Green run: same targeted commands, exit 0.
```text
S28 large match: code=1 replaced=0 remaining_bytes=1040384
S28: PASS bounded large-match deletion, admission, cancellation and exact undo/redo
findui_test: all passed
```

### §29: Partial

Public host mutation adapter supplies preflight and exact successful-operation callbacks, including partial host failure. Tests cover newline bookkeeping, journal/recovery and undo. Real editor binding and coordinated lineidx/layout/history/tab/journal integration remain missing: editor public API has no external mutation operation.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S29 host mutation: revision=0 records=0 lines=1
tests/findui_test.c:436: assertion failed: revision == 4 && records == 4
```

Green run: same targeted commands, exit 0.
```text
S29 host mutation: mode=0 revision=4 records=4 lines=1
S29 host mutation: mode=1 revision=1 records=1 lines=2
S29 host mutation: mode=2 revision=0 records=0 lines=3
S29: PASS host preflight, newline bookkeeping, partial edit journal/recovery and undo
findui_test: all passed
```

### §30: Partial

Full storage leases and per-source retirement acknowledgement are implemented for find snapshots, with final cleanup in explicit maintenance/disposal. A parked cancelled worker survives old-tree destruction and reload until its arena retirement is acknowledged. Actual editor eviction/reload and independent save leases remain missing.

Red run: `make -j4 build/tests/findui_test` then `DISPLAY=:99 build/tests/findui_test` (runtime assertion failure).
```text
S30 cancelled source: retained_storage_leases=0
tests/findui_test.c:448: assertion failed: leases > 0
```

Green run: same targeted commands, exit 0.
```text
S30 cancelled source: retained_storage_leases=2
S30: PASS full arena lease, cancelled eviction/reload, maintenance-only retirement acknowledgement
findui_test: all passed
```

### §27 no-op paging fuzz regression

The expanded fuzzer uses a small first cache so replacement/navigation exceed it. It found an empty-match/no-replacement operation clearing unchanged results. The exact fuzz input was reproduced, then a unit test was run against the old library before fixing page storage.

Red (M)[AC]:
```text
S27 noop pages: complete=0 count=0 cached=0 total=33
tests/findui_test.c:602: assertion failed: code == FINDUI_OK && result.complete && result.match_count == 33 && result.cached_matches == 8 && total == 33
```

Green (M)[AC], fresh release suite exit 0:
```text
S27 noop pages: complete=1 count=33 cached=8 total=33
findui_test: all passed
```
The exact formerly crashing fuzz input also exits 0 after the fix.

## Decisions and limitations

Design details: [edit-lez decision](../decisions/edit-lez.md). The fixes are confined to findui and its tests, bench and fuzzer. The new literal implementation is src/findui/literal.c. No unrelated issue was repaired.

The coordinator should keep the bead open for the remaining generic regex word visitor, bounded multi-byte/regex window scans, editor mutation binding and full editor/save storage leases. The API-only adapters do not claim those integrations are fixed.

The environment clock moved backwards during validation. Ordinary make reported clock skew and reused stale objects. Final release, sanitizer and fuzz builds use make -B to ensure the final sources are tested.

## Verification

Initial targeted GCC and Clang ASan/UBSan findui suites passed. Final forced release build and the final requested fuzz campaign passed; their evidence is below. Sandbox full make check failed at the CLI XCB connection to :99; the fresh full sanitizer retry with Xvfb access passed as recorded below.

LeakSanitizer cannot run in this sandbox. Every sanitizer/fuzz invocation uses ASAN_OPTIONS=detect_leaks=0; coordinator must rerun with leaks enabled.

Reproducible commands:
```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -B -j4 check
make -B -j4 build/fuzz/findui_fuzz
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/findui_fuzz -max_total_time=60 -max_len=384
```

## Loaded AC benchmark evidence

Consecutive pre/post polling variants and ordinary/whole-word pairs were run on the loaded AC box. These are TRACK comparisons, not gate verdicts. The required statistical sample gate remains unmet. Synthetic word data is anonymous a-space repetition; the named corpus fixture is read-only and unchanged. The dense fixture count bench checks all required first offsets without an artificial viewport.

Pasted measured output (M)[AC]; gate values in BENCH rows are (G):
```text
Power: Charging
Load: 5.12 8.47 7.17 3/1866 7
Variant: findui_bench_before_opt
findui_count: (M)[AC] power=Charging load1=5.12 fixture=/tmp/edit-corpus/all_a_1g.txt
BENCH name=G6_findui_plain_a_space n=3 required_n=10000 p50=199149934 p99=240665175 ci95_p50=[181575678,240665175] ci95_p99=[199149934,240665175] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
BENCH name=G6_findui_word_a_space n=3 required_n=10000 p50=255321194 p99=476928459 ci95_p50=[232484811,476928459] ci95_p99=[255321194,476928459] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
findui_count: exact total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
exit=0
Variant: findui_bench
findui_count: (M)[AC] power=Not charging load1=5.19 fixture=/tmp/edit-corpus/all_a_1g.txt
BENCH name=G6_findui_plain_a_space n=3 required_n=10000 p50=166069199 p99=209826101 ci95_p50=[135961738,209826101] ci95_p99=[166069199,209826101] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
BENCH name=G6_findui_word_a_space n=3 required_n=10000 p50=235706664 p99=453621443 ci95_p50=[215103202,453621443] ci95_p99=[235706664,453621443] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
findui_count: exact total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
exit=0
Load after: 5.70 8.43 7.18 7/1937 23
findui_count: (M)[AC] power=Not charging load1=5.12 fixture=/tmp/edit-corpus/all_a_1g.txt
BENCH name=G6_findui_dense_a n=3 required_n=10000 p50=300519163 p99=308398759 ci95_p50=[292861567,308398759] ci95_p99=[300519163,308398759] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
findui_count: exact total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
```

## Final-source checkpoint at scope cutoff

Forced GCC 13 release `make -B -j4 all`: exit 0. Final release findui suite:
exit 0, including the active no-malloc guard. `git diff --check`: exit 0.
Forced Clang 18 fuzz build and the exact original failing fuzz input: exit 0.
Final requested campaign, ASan/UBSan, `detect_leaks=0`: exit 0, (M)[AC]:
```text
Done 14992 runs in 61 second(s)
```
The box was loaded, with (M)[AC] load1=30.84 at a campaign checkpoint.

The fresh full sanitizer suite initially failed its out-of-scope CLI window
readiness timeout under concurrent compilation/load. No CLI/display code was
changed. The suite was retried successfully using the already freshly compiled binaries,
with only Xvfb :99 access. Generated artifact timestamps were normalized past
source timestamps after the environment clock jump, so this retry does not
reuse pre-fix objects or repeat unnecessary compilation. Further code changes in
this clock-skewed worktree must use make -B or discard generated build outputs. Final suite exit/status is recorded below. No new scope is being added after this checkpoint.

The final-source full sanitizer retry has passed findui, including the paging
no-op regression and every new finding test. Pasted module completion:
```text
S27 noop pages: complete=1 count=33 cached=8 total=33
S30: PASS full arena lease, cancelled eviction/reload, maintenance-only retirement acknowledgement
S29: PASS host preflight, newline bookkeeping, partial edit journal/recovery and undo
findui_test: all passed
```
`make all` using these fresh artifacts also exited 0:
```text
make: Nothing to be done for 'all'.
```
An obsolete earlier sanitizer process group was stopped after identifying that
it was still in raster_test while the final-source retry was progressing. Only
this worktree's obsolete validation group was stopped; no implementation or
other worker process was changed.

## Final verification outcome

All required executable checks exited 0 on the final sources:

- GCC 13 release `make -B -j4 all`, then ordinary `make -j4 all`: exit 0.
- Release findui suite, including the active counting allocator guard: exit 0.
- Clang 18 ASan/UBSan `make -j4 check`, Xvfb :99 only, `detect_leaks=0`: exit 0.
- Final findui fuzz campaign: exit 0, (M)[AC]14992 runs in (M)[AC]61 seconds.
- Exact earlier crashing fuzz input: exit 0 after the separately red-tested fix.
- `git diff --check`: exit 0.

Pasted full-suite completion (M)[AC]:
```text
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```
The earlier raster wait completed without any raster change. The remaining
sanitizer binaries also passed in a separate bounded verification run. No
out-of-scope defect was repaired or retained as a claimed fix.

Coordinator actions: keep this bead open for the four partial findings; integrate
the mutation and storage-lease interfaces through an editor public API; provide
independent save leases; rerun LeakSanitizer enabled. Loaded benchmark TRACK
measurements are not gate passes. No commits were made.
