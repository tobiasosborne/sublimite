# v2-0fv.19 — Find over piece views

Find is an L1 client of the frozen piece reading API. The test and fuzz links select the reference model ahead of
`libtext.a` pending v2-0fv.7b. Find imports piece length, view backing and iterator functions. It also uses L0's
fault poll, CPU capability query and work-epoch cancellation. Searches, compilation, visitors and captures have
caller-owned state and stack temporaries. The rank table is immutable, and find introduces no process-wide state.

## Public contract

The v1 P1.10a header called its byte-search surface frozen. This bead changes the following clauses.

- A source is a flat byte range or a borrowed `piece_view`. A view descriptor has zero flat length and a null flat
  pointer. Live views stay on the UI thread at one revision, and callers own snapshots for the full read. This also
  applies to continuations. The same iterator handles both kinds of view.
- The literal entry points now take `find_literal_options`. The zero-initialized value preserves sensitive,
  leftmost non-overlapping matches. `fold_ascii` canonicalizes ASCII uppercase, and `whole_word` checks the bytes
  immediately outside the range against `[A-Za-z0-9_]`. The source edges qualify. The empty literal still produces
  zero matches, including with word checks. Rejecting a range leaves overlapping starts eligible.
- Place the visitor, regex continuation and request-budget declarations in `find.h`. The public surface is one
  header. Range and mask selection helpers remain private. The caller retains visitor effects until success.
  `find_regex_stream_scratch_bytes` sizes the resumable workspace while `find_regex_scratch_bytes` continues to
  size single-call regex search and capture workspace.
- Direct regex compilation preserves the v1 grammar, match ordering, capture tie rule, size bounds and byte
  semantics. `options.c` remains as the implementation of `find_regex_compile_options`. It validates the original
  grammar, then expands each foldable literal or class and compiles the result. The original query owns syntax
  offsets. An expansion limit reports the query end. Failed optional compilation leaves the output pointer null.
  `options.h` is removed, with classification inside find and the option declaration in `find.h`.
- Mapped read validation joins flag, generation and work cancellation. A tainted or retired fault ID cancels, and
  unsuccessful results, captures and visits clear their public summaries.

## Mechanisms and counterexamples

| Mechanism | Invariant | Simpler alternative and its counterexample |
|---|---|---|
| ASCII canonical byte equality | Comparison, maximal-suffix ordering and period checks use the same byte map. A canonical filter letter can only match either ASCII case. A nonletter gets an unchanged byte comparison. The plan stores its two filter keys folded. It reads the needle through a canonical projection, so even unbounded literals need constant auxiliary space. | Blindly set the case bit for all filter bytes. ASCII punctuation pairs such as `@` and the grave accent would alias. A folded copy of the needle needs storage proportional to a caller's unrestricted literal length. |
| Rare pair and verification budget | The v1 rank remains an estimate (E) from text, code and log frequency. Folded letters score the sum of both case ranks. The second position prefers a different canonical byte to avoid two copies of the same weak predicate. The production filter retires candidate starts in ascending order. | Use the first and last positions or track only lowercase rank. A periodic run can pass both endpoints and repeatedly fail late in verification. Lowercase rank alone omits uppercase-heavy text such as log tags. |
| Two-Way fallback and whole-word filtering | The budget stays across candidates. A rejected word in the filter continues at the next candidate. Once in Two-Way, a byte-equal word rejection uses the critical factorization's safe shift and keeps periodic overlap memory. A complete match rules out shifts shorter than the needle period, so the optimization preserves the next eligible start. | Restart the literal kernel at every rejected start. A source of alternating cases of one letter, with a long needle of that letter, rejects almost every alignment on a word byte and re-verifies the same overlap repeatedly. |
| View readers and stitching | Long-needle Two-Way compares with separate forward cursors for each side of the cut. Word matching has independent forward cursors for the left and right neighbours. The short-needle SIMD path uses runs directly, with a bounded stitch for starts crossing runs. | Compare with repeated random point lookups. A match over thousands of small runs repeats tree descent during every verification. Direct-only SIMD would miss a needle spanning an ADD/ORIGINAL boundary. |
| Caller-owned visitor | Result search and visit share a count/retirement sink. Regex sync and continuation share Thompson start, step and capture priority. Single-byte literal search can transfer a whole SIMD mask, or select only requested ranks. A bounded callback runs once per selected ordinal even when selection rules overlap. | Fill a result array for every visitor or enumerate each dense byte match. A requested ordinal beyond the bounded prefix would disappear, and an all-letter buffer would make callback costs depend on unrequested hits. |
| Resumable regex and work budget | A private regex prefix cursor keeps its KMP border state in a table bounded by the maximum compiled prefix length. A candidate crossing a quantum keeps its lists, best endpoint and input offset, with the same ordered closure and retry credit as synchronous enumeration. | Restart a candidate at every yield. A single long regex match can keep consuming the entire quantum before reaching a new byte. Resetting retry credit at each start lets expressions such as `a*b` scan repeated suffixes of a document containing only `a`. |
| Byte-sensitive NFA with fold expansion | Close class membership under ASCII case before applying negation. The ordinary compiler owns the grammar and tagging. Expansion uses caller storage and the existing compiled limits. | Lowercase the query and feed raw bytes to the existing compiler, or add case logic only to literal atoms. `[^a-z]` would then accept uppercase ASCII letters, and dynamically interpreting folding in the matcher would add work to every NFA byte predicate. |
| Backing polls | Initial polls validate before search. Work polls cover bounded reads. A closing validation precedes a selected callback, a bulk mask or the return of an exact summary. This holds for a short final batch and every continuation yield. A recovered mapping is still addressable, while its bytes are no longer trusted. | Poll only at the work quantum or final return. A short search could hand a recovered NUL match to the visitor before the next poll discovered the fault. |
| Fixed-work mask counts | Wide integer lane sums count set bits without walking discarded bits. The release object uses only its existing module and system imports. | Use the compiler builtin under the SSE baseline. GCC emitted `__popcountdi2`, which has no system-symbol grant. A per-bit loop would add work to dense counts with small output windows. |

### Literal work bound

Canonicalization is a constant-time function of one byte. Two-Way therefore operates on a virtual byte string in
the usual totally ordered alphabet. Its factorization, periodic overlap memory and candidate shifts carry over
directly. For a periodic word rejection, the shift and memoized overlap are exactly those of the folded string.
For a nonperiodic word rejection, the nonperiodic shift is a lower bound on the next possible overlapping
alignment. An accepted literal consumes its full length, so resetting overlap for the next match charges at most
the consumed length.

(G) The inherited filter retires work with credit `4 * scanned + 8 * needle_bytes`, from the v1
`P1.10-simd-filter-verify.md` decision. A verification charges the whole needle, even on mismatch. A rejected
candidate can overshoot by at most one more such charge before Two-Way takes over. Accepted verifications add at
most the length of the accepted disjoint ranges. Lookahead can revisit at most a fixed SIMD block per accepted
match. Thus verification plus scanning stays linear in source and needle length. The supported address space
bounds both counters and their credit arithmetic below `UINT64_MAX` (P, at most 56 low user address bits on the
x86-64 target with five-level paging). SIMD legality is unchanged by folding because each lane tests only its
candidate's selected byte and produces a candidate mask.

(P) The inherited 4 KiB stitch and 2 KiB needle limit make a full stitch retire at least half its start positions.
For dense accepted matches the maximum repeated lookahead per call is a fixed window. Longer needles use forward
Two-Way reads instead of copying potentially long spans. Its cursors walk each run at most once per monotone
search interval. A boundary query does constant byte work in flat storage and advances at most two piece cursors,
independently of the comparison cursors. The reference model proves span traversal through its public iterator.
It has no tree shape to measure.

## Suite census and release checks

- `find_test` preserves the v1 case-sensitive byte, regex, guard-page, capture and argument fixtures. It adds
  folded regex classes and expansion failures.
- `law2_find_test` checks a live release base malloc-guard window spanning compile, search, next, capture,
  visit and continuation. Find has no typing-path caller in this checkout.
- `find_bestof_test` ports binary, fragmented, long and periodic fixtures plus iterator and cancellation probes.
  The public wrappers observe actual seeks/spans and cancellation at every measured polling boundary. The static
  `gather` copy-count probe is cut, since it depended on a private stack buffer. The public stitched-reader
  cancellation test remains.
- `find_simd_filter_verify_test` keeps the v1 sensitive differential, vector boundaries and fallback fixtures.
  It adds randomized case pairs and explicit SSE/AVX folded fragment cases against forced Two-Way and an
  independent oracle. Direct mutators of Two-Way cut, period and meter locals are cut. Public poll sweeps and
  observable results cover cancellation. A test-only object supplies forced Two-Way to the suites that need it.
- `find_p1r_test` exercises dense masks through the public sink, tests regex retry budgets and sweeps visitor
  cancellation. The original private popcount/ctz and plan-initialization counters are cut. Bounded mask delivery,
  periodic word rejection scaling and stream/result/visit agreement are observed through public calls.
- `find_timeout_test` replaces the C-include supervisor fixture with a handshake at a real worker poll and a
  refused submission. The old normal-exit-marker and supervisor-verdict probes are cut with the supervisor.
- `find_bench_test` runs the built benchmark's generated fixture self-check. The benchmark has no external
  fixtures or private kernel probes. The old optional-file, artificial timing verdict and process-supervisor
  tests are cut with those paths. The benchmark reports timing observations independently of correctness checks.
- `find_options_test` uses the v1 `findui_test.c` binary and rejected-overlap expectations in an independent
  oracle. It compares flat, live and snapshot reads and checks result, range visitor and next equivalence.
  `find_fault_test` injects recovery with linker wraps around public piece iteration and the OS fault ID poll.
- `find_fuzz.c` keeps the independent regex endpoint model and long literal model. It adds both option bits,
  word boundary rejections, folded regex compilation and continuation comparisons. Seeds force descending
  physical ADD coordinates so the logical needle and neighbours can lie in distinct runs.
- `findui_test.c` and `config_find_test.c` stay with their higher-layer consumers. No new tool needs a registry entry.

## Red and green

The baseline was a view-adapted, case-sensitive harvest with the added options ignored and fault polling absent.
The test source preceded that baseline. The first `nice make -j2 build/san/tests/find_options_test` failed with
`fatal error: 'find/find.h' file not found`. After the baseline linked, the same option suite ran its oracles
and failed behaviorally. The commands below ran from the checkout with the corresponding test binary built by
`nice make -j2`.

| Invocation | Baseline | Implemented |
|---|---|---|
| `build/san/tests/find_options_test` | `fold=1 word=0 len=7 needle=2 actual=1 expected=2` and `fold=0 word=1 ... actual=1 expected=0`. The rejected-overlap fixture also failed its offset comparison. (P) Counts derive from the binary fixture and the immediate word neighbours. | `find_options_test: fold/word oracle and literal result/visit/next agree` |
| `build/san/tests/find_fault_test` | `recovered=1 pristine_polls=0 polls=0 code=0 emitted=6`. (P) One callback for each byte of the recovered fixture's six-byte suffix. Leak checking was disabled for this deliberately failing early exit. | `recovered=1 pristine_polls=1 polls=2 code=1 emitted=0`. (P) Entry and callback-frontier validation bracket the mapped read and cancel the visitor before its first delivery. |

The public poll probe doubles both subject and needle size in a periodic folded word rejection. (P) The input
sizes are the half and full fixture lengths and the needle is a quarter of each. (G) An independent oracle or
observed API state checks each correctness assertion. The growth check uses the v1 retry test's doubling
allowance. The measured poll counts are recorded in the acceptance log.

The release symbol lane initially rejected GCC's emitted `__popcountdi2`. The within-module lane-sum count
removes that import. The guard check follows the repository's dedicated Law-2 suite naming convention. For
the contract runner, the local acceptance invocation used a private mount namespace with `/tmp` bound to
`build/find-tmp`. The invoking user was kept and the mount capabilities were dropped before running Make,
so the unreadable-file lane fixture kept its normal permissions behavior.

## Line census

(M)[AC, load1 10.52] Counted physical lines in the scoped source groups with a multiset match of the exact
line text in v1 and v2. This credits moved identical lines and treats every rewritten line as both removed and
written. The fixture-only v1 status note and L4 suites are outside this count.

| Group | v1 | v2 | Harvested unchanged | Written | Cut or replaced |
|---|---:|---:|---:|---:|---:|
| Production | 1495 | 1783 | 1057 | 726 | 438 |
| Suites | 1372 | 1782 | 808 | 974 | 564 |
| Fuzzer | 290 | 341 | 208 | 133 | 82 |
| Benchmark | 444 | 144 | 32 | 112 | 412 |
| Total | 3601 | 4050 | 2105 | 1945 | 1496 |

(M)[AC, load1 10.52] Per-file counts from the same census. A dash marks a file absent from that version.

| File | v1 lines | v2 lines |
|---|---:|---:|
| `src/find/find.c` | 539 | 582 |
| `src/find/find.h` | 167 | 243 |
| `src/find/find_int.h` | 86 | 179 |
| `src/find/literal.c` | 455 | 622 |
| `src/find/literal.h` | 32 | 34 |
| `src/find/options.c` | 109 | 123 |
| `src/find/options.h` | 21 | — |
| `src/find/visit.h` | 86 | — |
| `tests/find_bench_test.c` | 56 | 33 |
| `tests/find_bestof_test.c` | 418 | 383 |
| `tests/find_fault_test.c` | — | 114 |
| `tests/find_fixture.h` | — | 57 |
| `tests/find_options_test.c` | — | 167 |
| `tests/find_p1r_test.c` | 179 | 240 |
| `tests/find_simd_filter_verify_test.c` | 343 | 304 |
| `tests/find_test.c` | 308 | 338 |
| `tests/find_timeout_test.c` | 68 | 78 |
| `tests/law2_find_test.c` | — | 68 |
| `fuzz/find_fuzz.c` | 290 | 341 |
| `bench/find_bench.c` | 339 | 144 |
| `bench/find_bench.args` | 1 | — |
| `bench/find_probe.h` | 48 | — |
| `bench/find_supervise.h` | 56 | — |

The census groups exclude the Makefile, test lane registry, decision note and binary seeds. The model,
wrap, hook, benchmark and lane registration changes are additions in the Makefile and `tests/lanes.mk`.

## Acceptance and benchmark

(G) Final gates all exited zero. Suite counts below come from the lane inventory and the commands' summaries.

| Command | Key output |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:150-:159` | `check_symbols: ok`, `check_dag: ok`, `check_no_c_includes: ok`, `check-layers: 6 isolated layer links passed`, `check-san: 26 test binaries passed`, `check-law2: 5 release suites passed`, `check: ok`. |
| `nice make -j2 check-tsan` | `check-tsan: 9 threaded suites passed`. The mapped recovery test and real work-epoch cancellation both passed. |
| `nice make -j2 fuzz-smoke` | `fuzz-smoke: 10 fuzzers replayed their seeds`. The find fuzzer replayed its entire corpus. |
| `nice make -j2 all build/rel/bench/find_bench` | Release executable and benchmark linked. |
| `nice build/rel/bench/find_bench` | All generated rows agreed with the independent byte oracle and printed latency percentiles. |

The sensitive and folded differential reported `sensitive and folded SIMD agree with Two-Way`. The release
allocation window reported `active=1 count=0` (G, live interposer and no allocations in the guarded calls).
The exported archive has no forced-Two-Way test hook.

(M)[AC, load1 6.73–6.75] Release benchmark on a warm buffer. The buffer is 7,471,104 bytes (P, generated
record width times record count). There are 20 observations per row (G, inherited from `scan_bench`).
`trace_fmt_pct` selects nearest-ranked samples, so the reported p99 is the last sample after sorting (P).

| Row | p50 ns | p99 ns |
|---|---:|---:|
| newline | 2,383,621 | 4,335,783 |
| fold_dense_byte | 2,346,534 | 2,487,237 |
| sparse_exact | 1,114,323 | 1,678,464 |
| sparse_fold | 1,063,568 | 1,336,208 |
| sparse_fold_word | 1,569,609 | 1,922,708 |
| periodic_exact_miss | 9,574,669 | 17,441,373 |
| periodic_fold_miss | 7,784,678 | 8,622,698 |

The sparse `ERROR` rows select the first `R` and the `O` in both modes. The inherited estimates rank the
uppercase pair below `E`, and the folded rank sums keep the same order (E, table entries and sums).
Folding returns both case spellings injected by the generator. These figures measure the full calls, including
their different match counts. The benchmark treats latency as an observation.

(M)[AC, load1 10.52] The Makefile has 386 physical lines and `tests/lanes.mk` has 46, counted with `wc -l`.
The binary corpus has 7 seeds and 80 bytes in total, from `wc -c` at the same census. Case selection is in
the high bits of the seed header. The seed bodies encode the literal and regex fixtures described above.

## Handoff

The real piece tree and the higher-layer find consumers are pending in their own beads. When the piece
implementation lands, remove find's model links alongside the piece suites and rerun the fragmented-view
and fault regressions with the real iterator. The reference project checkout has newer piece-specific
fault-wrap flags in the Makefile. Keep those alongside the new find prerequisites when integrating this
worktree. No piece Lines calls or changes were needed here.
