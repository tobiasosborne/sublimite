15 findings: **3 BLOCKER, 10 MAJOR, 2 MINOR**.

Read-only review; no files changed. Existing release and ASan/UBSan binaries for `font_test` and `unicode_render_test` passed, with leak detection disabled. Release tests reported zero libc allocations on their covered paths. No builds or performance campaigns were run. Findings below use code arguments and read-only inspection of font data; malformed-font parser internals were excluded.

## 1. BLOCKER — Metrics do not establish baked-atlas identity

**Location:** [src/font/font.c:730](/home/tobias/Projects/editor/src/font/font.c:730)

`atlas_matching()` compares cell dimensions and glyph metrics, but never compares font identity or outlines. Different glyph images can have identical metrics, so it can attach DejaVu’s baked pixels to a different valid font.

Concrete repro: swap the `A` and `W` mappings in the embedded font’s Unicode cmap subtables. At 30 px, both have `(advance, bearing_x, bearing_y, w, h) = (16, 0, -19, 16, 19)`. Every atlas comparison still passes. Live rasterization of `A` produces the modified font’s `W`, while ASCII layout uses the baked `A`. Adding an invisible selector can consequently change the rendered outline within the same font.

**Fix:** Associate each bake with a verified font-content digest and face index, checked during initialization. Add a valid, metrics-preserving cmap mutation test and pixel comparisons for all baked ASCII glyphs.

## 2. BLOCKER — Negative-bearing Mono marks are shifted outside their cell

**Location:** [src/font/unicode.c:141](/home/tobias/Projects/editor/src/font/unicode.c:141)

The sign of `bearing_x` does not determine whether a combining glyph expects positioning after the base advance. The embedded Mono font contains negative-bearing combining glyphs with a full monospace advance.

Concrete repro: render `e\u0361`, COMBINING DOUBLE INVERTED BREVE. At 15 px, its horizontal bitmap interval is `[-2, 10)` and its advance is 8 px. The implementation adds the base’s 8 px advance, moving it to `[6, 18)` inside an 8 px cell; only two columns remain. At 30 px, it moves `[-3, 19)` to `[13, 35)` inside a 16 px cell. Most of the covered mark disappears because of the positioning heuristic.

**Fix:** Distinguish cell-relative marks from zero-advance, pen-relative marks using advances or explicit placement metadata, rather than bearing sign. Add pixel tests for U+0361 at both sizes and for a proportional fallback mark.

## 3. BLOCKER — Invisible joining and variation controls can replace a covered base with `?`

**Location:** [src/font/unicode.c:6](/home/tobias/Projects/editor/src/font/unicode.c:6)

The ignorable-control list omits U+034F COMBINING GRAPHEME JOINER and U+180B/U+180C/U+180D/U+180F Mongolian free variation selectors. These join their preceding base in the UTF-8 grapheme path, but the composer requires font coverage for them.

Concrete repro: prepare an embedded-only family and request `e\u034f`, width 1. The embedded font covers `e` but lacks U+034F, so the whole cluster returns MISSING and layout draws `?`. This occurs in the supported fontconfig-absent configuration. `e\u180b` also fails with the currently discovered three-face family.

The test’s `covered()` helper uses the same incomplete `font_cluster_ignorable()` function, so it classifies these failures as legitimate uncovered clusters.

**Fix:** Include invisible grapheme and variation controls using a shared Unicode property definition. Keep visible combining marks distinct. Test these sequences with both embedded-only and discovered families.

## 4. MAJOR — One cold cluster can monopolize the UI thread

**Location:** [src/font/unicode.c:117](/home/tobias/Projects/editor/src/font/unicode.c:117)

Composition rasterizes every drawable scalar synchronously, without a work budget or continuation. Layout budgets segmentation, but then executes the entire glyph callback as one operation.

Concrete repro: `"e" + 8191 × U+0301` is one supported, width-1 cluster of 16,383 bytes, below `FONT_CLUSTER_MAX_BYTES`. One cold lookup performs 8,192 rasterizations before returning. Adding another mark to an already displayed combining sequence creates a new exact key and rerasterizes the entire sequence.

A scratch-capacity limit bounds storage, not execution time. This path has no mechanism to enforce G1’s 0.5 ms UI slices or 1/2 ms typing gates.

**Fix:** Make cold composition resumable with a bounded work budget, or prepare expensive images on workers with exclusively owned font state and mailbox handoff. Test long supported clusters and edits extending them.

## 5. MAJOR — A saturated negative cache causes capacity-sized scans per lookup

**Location:** [src/font/unicode.c:160](/home/tobias/Projects/editor/src/font/unicode.c:160)

Linear probing permits the table to become completely full. Every absent key then examines every entry before returning NOMEM. There is no probe limit or load-factor bound.

Concrete repro: initialize the normal 8,192-entry cache and insert 8,192 distinct, absent supplementary private-use scalars. They consume only 32 KiB of keys and no atlas rectangles. Every subsequent new cluster scans all 8,192 entries. A 360-cell row of new keys performs 2,949,120 probes, synchronously inside layout callbacks.

The fuzzer restricts capacity to at most 32 entries, and the benchmark keeps only three keys warm.

**Fix:** Enforce a fixed maximum probe distance during insertion and lookup, with bounded occupancy and admission failure. Bound the negative cache separately. Add saturated-table and collision-heavy work-count tests.

## 6. MAJOR — Cache exhaustion silently becomes permanently incorrect content

**Location:** [src/font/unicode.c:172](/home/tobias/Projects/editor/src/font/unicode.c:172), [src/layout/layout.c:383](/home/tobias/Projects/editor/src/layout/layout.c:383)

The cache’s documented NOMEM result is discarded by the supplied layout integration. Layout substitutes `?`, completes successfully, and does not mark the result approximate or expose a resource-recovery condition.

Concrete repro: fill a four-entry cache with four missing scalars, then lay out covered U+00E9 through `font_cache_glyph`. The atlas can be entirely unused, yet U+00E9 becomes `?`. Repeating layout cannot repair it because the append-only table remains full.

Bounded cache exhaustion is intentional; treating that exhaustion as final missing-font content is the gap. G5 explicitly says a placeholder does not satisfy its correct-content endpoint.

**Fix:** Propagate resource exhaustion to the editor separately from MISSING. Recover by rebuilding or rotating cache storage at the proper T5/quiescent boundary, fully redrawing and retrying. Prevent negative entries from consuming all positive-cache capacity.

## 7. MAJOR — Discovery publishes outside the mailbox’s cancellation boundary

**Location:** [src/font/fallback.c:153](/home/tobias/Projects/editor/src/font/fallback.c:153), [src/font/unicode.c:18](/home/tobias/Projects/editor/src/font/unicode.c:18)

Discovery exposes its result through `fb->done` before `work_publish()` applies cancellation and generation filtering. `font_family_load()` accepts that flag directly; the result contains no discovery generation.

Concrete argument: cancel a running discovery before publication. Discovery still writes the paths and release-stores `done = 1`. `work_publish()` rejects the stale job, but a reader following the documented acquire-on-`done` protocol can still adopt its result.

Release/acquire makes the fields readable; it does not make the cancelled result current. This also violates the rule that shared-state adoption crosses through `src/work` mailboxes.

**Fix:** Prepare results in worker-owned staging storage and adopt them only from a live mailbox completion carrying generation and result identity. Make cancellation suppress adoption of the staged result.

## 8. MAJOR — Failed completion publication is ignored

**Location:** [src/font/fallback.c:164](/home/tobias/Projects/editor/src/font/fallback.c:164)

`font_fallback_job()` discards the boolean result of `work_publish()`. A live job can finish without delivering its promised completion.

Concrete repro: fill the bulk worker’s mailbox with `WORK_MAILBOX_CAP` messages, then run discovery before UI drains it. The final publication returns false because the mailbox is full. Discovery returns, and the work pool does not independently publish job completion. Once earlier messages are drained, a UI waiting for `FONT_FALLBACK_MSG_KIND` has no event that can finish its handoff.

Existing tests use a fresh pool and never exercise publication pressure.

**Fix:** Guarantee terminal completion through `work`, using reserved completion capacity or cancellable retry, and handle publication failure explicitly. Add a mailbox-saturation test.

## 9. MAJOR — The raster benchmark passes failed rasterizations

**Location:** [bench/font_bench.c:89](/home/tobias/Projects/editor/bench/font_bench.c:89)

The primary timed loop discards `font_raster_glyph()`’s return code and never requires pixels or nonzero coverage. Final success depends on timing, dropped samples and allocation count.

Concrete argument: make U+00E9 return NOMEM immediately. All 20,000 primary samples become cheap failures and the benchmark can pass. The later CJK and cluster rows exercise different codepoints, so they need not detect this failure.

The allocation gate also initializes its count to zero when the guard is inactive, allowing an unenforced check to pass.

**Fix:** Accumulate raster failures and validate the expected nonempty bitmap outside timing. Fail the benchmark on either condition. Require an active guard when claiming that the zero-allocation gate passed.

## 10. MAJOR — Tail and cold-composition regressions cannot fail the benchmark

**Location:** [bench/font_bench.c:51](/home/tobias/Projects/editor/bench/font_bench.c:51), [bench/font_bench.c:155](/home/tobias/Projects/editor/bench/font_bench.c:155), [bench/font_bench.c:182](/home/tobias/Projects/editor/bench/font_bench.c:182)

The primary raster row prints p99 but checks only p50. CJK raster and both cluster timing rows have no timing limit. Cold composition samples just three short clusters; the guarded 20,000-call cluster loop measures cached hits.

Concrete argument: a successful raster’s slow tail can exceed the stated per-glyph budget while its median passes. Likewise, inserting a large delay into cold composition leaves all benchmark exit checks satisfied. The real-cache layout rows warm their caches before collecting samples and remain TRACK.

**Fix:** Enforce the raster tail budget and add binding cold-composition/typing regression checks. Exercise varied new clusters, long combining sequences and table pressure; reject timing misses through the normal benchmark reporting path.

## 11. MAJOR — An early missing scalar masks invalid trailing UTF-8

**Location:** [src/font/unicode.c:132](/home/tobias/Projects/editor/src/font/unicode.c:132)

The composer returns MISSING as soon as one scalar lacks coverage, without validating the remaining bytes. This violates `font.h`’s explicit “Invalid bytes return ARG” contract.

Concrete repro: pass `F4 8F BF BF FF`, length 5, width 1, to an otherwise empty cache. U+10FFFF decodes successfully and is missing, so the function returns and negatively caches MISSING without inspecting the invalid final `FF`.

The Unicode fuzzer cannot generate this API call: it segments with `utf8_cluster()` first, separating the invalid byte into another request.

**Fix:** Validate the complete supplied span before admitting a negative cache entry. Add direct API tests for malformed suffixes after missing and covered scalars, requiring ARG and unchanged storage.

## 12. MAJOR — Sequence pixel correctness is largely untested

**Location:** [tests/unicode_render_test.c:144](/home/tobias/Projects/editor/tests/unicode_render_test.c:144), [tests/unicode_render_test.c:234](/home/tobias/Projects/editor/tests/unicode_render_test.c:234), [fuzz/font_fuzz.c:64](/home/tobias/Projects/editor/fuzz/font_fuzz.c:64)

The screenshot checks one base-plus-mark sequence, `e + acute`, alongside individual CJK/emoji glyphs. Corpus validation mostly checks placeholder identity and wide-cell attributes. The fuzzer checks repeat stability and descriptor bounds, not expected pixels.

Concrete argument: omit compositing covered non-acute marks after successful coverage checks. `ñ` and `ü` become bare letters, but corpus checks still see non-`?` identities, distinct keys still receive distinct slots, and repeated fuzzer results remain stable. Omitting later covered ZWJ components has the same oracle gap.

**Fix:** Add independent pixel expectations for multiple marks, wider-than-cell marks, covered ZWJ sequences, and VS15/VS16 sequences. Check actual UTF-8 cluster widths against layout attributes and image extents. Use fixtures with deterministic coverage.

## 13. MAJOR — Atlas lifetime through T5 has no font integration test

**Location:** [tests/unicode_render_test.c:179](/home/tobias/Projects/editor/tests/unicode_render_test.c:179), [tests/unicode_render_test.c:253](/home/tobias/Projects/editor/tests/unicode_render_test.c:253)

The current append-only writes preserve retained rectangles in the inspected implementation. However, the claimed frames-in-flight guarantee is tested only with synchronous raster calls and saved-byte comparisons. No font-cache test submits a frame or delays T5.

Concrete argument: clearing runtime page pixels when rebinding a grid would evade the saved-rectangle test, which never binds its cache. Screenshot comparisons finish before later grid rebindings, and subsequent corpus checks do not compare pixels. Such a regression could corrupt an earlier pending frame while this suite remains green.

**Fix:** Add a deferred renderer test that copies metadata but borrows R8 pixels until T5. Submit frame N, bind another grid and append glyphs for N+1, then consume N’s pixels. Verify arena retirement and size/cache replacement only after the lifetime bound.

## 14. MINOR — A failed file read consumes arena capacity

**Location:** [src/font/fallback.c:175](/home/tobias/Projects/editor/src/font/fallback.c:175)

After allocating the reported file size, a short `fread()` sets `buf = NULL` without restoring the arena mark. Failure also leaves the caller’s length unchanged.

Concrete repro: truncate a font file between `ftell()` and `fread()`. The helper returns NULL while retaining the entire allocation. Repeated retries consume capacity despite producing no usable file. `font_family_load()` supplies its own rollback, but direct callers do not receive that protection.

**Fix:** Save the entry mark and restore it on read failure. Clear the output length on failure and explicitly validate or document the required `len` argument.

## 15. MINOR — Unknown power status can be labelled AC

**Location:** [bench/font_bench.c:45](/home/tobias/Projects/editor/bench/font_bench.c:45)

If the status file opens but `fgets()` fails, the buffer becomes empty. Every value other than exact `"Discharging"` is then classified as `"AC"`.

Concrete argument: an empty, unreadable-after-open or unrecognized status produces an `(M)[AC]` evidence tag even though AC power was not established. The shared harness already handles this case as unknown.

**Fix:** Use the harness’s normalized power-state and evidence-tag helpers.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/font/font.c:730` | Equal metrics can attach the wrong baked outlines. |
| 2 | BLOCKER | `src/font/unicode.c:141` | Negative-bearing Mono marks are shifted mostly outside the cell. |
| 3 | BLOCKER | `src/font/unicode.c:6` | Invisible joining/variation controls can turn covered bases into placeholders. |
| 4 | MAJOR | `src/font/unicode.c:117` | One accepted cluster can execute thousands of unsliced UI rasterizations. |
| 5 | MAJOR | `src/font/unicode.c:160` | A full negative cache forces whole-table scans on new keys. |
| 6 | MAJOR | `src/layout/layout.c:383` | Exhaustion becomes permanent placeholder content without recovery status. |
| 7 | MAJOR | `src/font/fallback.c:153` | The `done` side channel bypasses mailbox cancellation and generation filtering. |
| 8 | MAJOR | `src/font/fallback.c:164` | Mailbox-full completion loss is ignored. |
| 9 | MAJOR | `bench/font_bench.c:89` | Failed rasterizations can pass the benchmark. |
| 10 | MAJOR | `bench/font_bench.c:155` | Tail and cold-composition timing regressions cannot fail the bench. |
| 11 | MAJOR | `src/font/unicode.c:132` | Early MISSING masks malformed trailing UTF-8. |
| 12 | MAJOR | `tests/unicode_render_test.c:144` | Multi-mark, ZWJ and variation-sequence pixels lack independent coverage. |
| 13 | MAJOR | `tests/unicode_render_test.c:179` | Font atlas lifetime is never exercised through delayed T5. |
| 14 | MINOR | `src/font/fallback.c:175` | Short reads consume arena capacity and leave stale lengths. |
| 15 | MINOR | `bench/font_bench.c:45` | Unknown power status can be reported as AC. |