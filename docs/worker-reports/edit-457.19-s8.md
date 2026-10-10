# P4.11b — font review fixes (edit-457.19)

Scope: `docs/reviews/font-1.md` §§1–15 and the font-discovery portion of
`docs/reviews/work-scan-1.md` §9. Parser/CFF internals, frozen headers, other
module implementations and build rules are unchanged. Layout has only the
§6 glyph-result block changed. All regression selectors are repeatable from
the repo root on `DISPLAY=:99 EDIT_DISPLAY=:99`.

Red checks below ran before each behavioral fix. §§12–13 are coverage-only
findings: current composition/append-only binding passed the new oracles;
their red lines come from explicit temporary mutations, subsequently removed.
These are mutation evidence, not fabricated failures of correct baseline code.
Bench `--self-check` exercises pure acceptance predicates without timings.

## Font §1

Verdict: Confirmed; fixed. INIT computes SHA-256 of immutable bytes, comparing the checked-in bake identity and face 0; metrics remain an additional consistency check. The valid same-length format-12 cmap swaps A/W with all ASCII metrics unchanged. Both bakes have every ASCII pixel compared to live rasterization. Re-baking requires updating src/font/identity.h and the pixel test.

```text
RED: tests/font_test.c:673: CHECK failed: modified.atlas == NULL
GREEN: review §1 GREEN: metrics-preserving cmap rejected; every baked ASCII pixel matches live outlines
```

## Font §2

Verdict: Confirmed; fixed. Zero-advance marks use the base advance; full-advance Mono marks remain cell-relative regardless of bearing sign. U+0361 is checked at both sizes; a deterministic DejaVu Sans zero-advance fallback U+0340 is independently positioned and compared.

```text
RED: review FAIL: memcmp(expected + (size_t)y * g.w, f->cache.pages[g.page].pixels + (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0
GREEN: review §2 GREEN: U+0361 independent pixels px=15
```

## Font §3

Verdict: Confirmed; fixed in font. The shared font_cluster_ignorable predicate now defines Unicode 15.1 Default_Ignorable_Code_Point, including CGJ and Mongolian selectors, while visible marks remain drawable. Independent sequences test embedded-only and discovered families. A future utf8 property accessor could eliminate cross-module property duplication; modifying utf8 is outside this bead.

```text
RED: review FAIL: font_cache_glyph(&cache, bytes, n, 1, &slot) == FONT_OK
GREEN: review §3 GREEN: invisible controls preserve embedded/discovered base pixels
```

## Font §4

Verdict: Confirmed; font continuation fixed; editor scheduling proposed. Cold composition reserves one pending exact key/image at INIT, processes at most 8 scalars/raster calls per lookup (G), and cooperatively checks 250 us between scalars (G). FONT_MORE preserves slot and scratch, and exact-key retries complete long clusters and extensions. Hits preserve pending work; another cold key abandons it. Layout marks this resource condition approximate under §6. Proposal outside scope: editor should keep a bounded font-preparation queue, check input between retries and relayout after completion. A single raster call is not preempted; hard bounds for unusually complex individual outlines require exclusively owned worker preparation, not changes to the frozen parser internals.

```text
RED: review FAIL: rc == FONT_MORE && slot == 123
GREEN: review §4 GREEN: long cluster and extension yield bounded slices, then exact pixels
```

## Font §5

Verdict: Confirmed; fixed. Each of the positive and negative tables has at most 32 probes per lookup (G). Positive occupancy is capped at 75%; negatives have a separate table capped at 64 buckets, half occupied, separate keys and 256-byte negative admission limit. Negative refusal returns uncached MISSING, preserving positive resources. Tests insert all 8192 missing private-use scalars and inject collision saturation, checking actual work counters.

```text
RED: review FAIL: cache.probes - before <= 2u * FONT_CACHE_MAX_PROBES
GREEN: review §5 GREEN: saturated negatives and primary collisions have fixed probe bounds
```

## Font §6

Verdict: Confirmed; scoped status/capacity fix; editor rotation proposed. Layout exposes NOMEM and MORE through layout_approximate rather than claiming exact placeholder content. Font records sticky resource_error=NOMEM; negatives cannot consume positive keys/buckets/rectangles. Test confirms approximate exhaustion, correct retry after resource restoration, and covered é after negative saturation. Proposal outside scope: editor must inspect approximation plus font resource_error, retain old pages through T5 (or quiescent shutdown), prepare a fresh cache, rebind it, fully damage/redraw and retry. No editor changes, automatic retirement, or new frozen renderer fields are made.

```text
RED: review FAIL: layout_approximate(&f->l)
GREEN: review §6 GREEN: exhaustion is approximate/retryable; negatives reserve positive capacity
```

## Font §7

Verdict: Confirmed; fixed. Async discovery prepares worker-owned staging; public done remains zero. Only font_fallback_event in a live work-mailbox callback adopts matching result identity, generation, slot and epoch, once. Reset invalidates previous identity; storage remains alive through physical completion and message drain. Synchronous discovery remains an explicitly exclusive INIT API and is forbidden for cancellable pooled jobs. Tests/bench use the actual adoption path.

```text
RED: tests/font_test.c:746: CHECK failed: atomic_load(&p.fb.done) == 0
GREEN: review §7 GREEN: cancelled discovery has no adoptable done side channel
```

## Font §8

Verdict: Confirmed; fixed without work changes. A full mailbox causes cancellable publication retries with a worker sleep, preserving the terminal completion until drain. Epoch cancellation suppresses publication/adoption. The saturation regression fills WORK_MAILBOX_CAP, lets discovery finish, drains and requires the terminal event. Work reserved-completion capacity is unnecessary for this job.

```text
RED: tests/font_test.c:758: CHECK failed: got == 1
GREEN: review §8 GREEN: full mailbox drains, terminal discovery completion arrives
```

## Font §9

Verdict: Confirmed; fixed. The timed loop counts every failed result and validates nonzero bitmap coverage outside timing. The warm expected raster also requires pixels/ink. Inactive allocation guard is a failure, not a zero-allocation claim. Pure self-check rejects failed rasters, empty pixels, inactive guards and accepts a valid guarded bitmap.

```text
RED: review §9 RED: failed/empty raster or inactive guard accepted
GREEN: review §9 GREEN: raster failures, empty pixels and inactive guard rejected
```

## Font §10

Verdict: Confirmed; fixed. Raster and CJK p50/p99 are both bound to 50 us (G); cold/cached lookup slices have a 0.5 ms p50/p99 regression budget (G), through bench_report plus the same checked predicate. Varied new keys, 8192-negative pressure, long supported clusters and extensions are exercised. Any timing/drop/content/guard miss yields nonzero exit. All shared-machine rows are TRACK; no loaded miss is a quiet-box gate verdict or full editor G1 certification.

```text
RED: review §10 RED: slow tail/cold slice/cached typing accepted
GREEN: review §10 GREEN: synthetic tail/cold/cached timing misses rejected
```

## Font §11

Verdict: Confirmed; fixed. The complete span is decoded and validated before hashing, coverage, negative admission or positive resource checks. Missing/covered prefixes followed by invalid and truncated suffixes return ARG, preserve slot/keys/glyphs/atlas and leave scratch empty. Unicode fuzz mode now makes direct malformed-span API calls in addition to segmented calls.

```text
RED: review FAIL: font_cache_glyph(&f->cache, bad[i], lengths[i], 1, &slot) == FONT_ERR_ARG
GREEN: review §11 GREEN: malformed suffixes return ARG without admission
```

## Font §12

Verdict: Coverage gap confirmed; test-only fix. Independent scalar-raster/placement coverage unions now test n+tilde, u+diaeresis, multiple marks, wider U+0361, covered sun-ZWJ-cloud and VS15/VS16 at both sizes. Actual utf8 cluster length/width, layout wide attributes and image extents are checked. Baseline passes; omitting non-acute U+0303 composition produces the red below. Mutation removed; no behavior change for this finding.

Red is a temporary mutation, not baseline failure:
```text
RED: review FAIL: memcmp(expected + (size_t)y * g.w, f->grid.pages[g.page].pixels + (size_t)(g.y + y) * FONT_ATLAS_PAGE_DIM + g.x, g.w) == 0
GREEN: review §12 GREEN: multi-mark/wider-mark/ZWJ/VS pixels and utf8 layout widths px=15
```

## Font §13

Verdict: Coverage gap confirmed; test-only fix. A deferred backend uses the real render adapter: submit copies metadata and borrows R8 bytes; present deliberately delays T5. While frame N is pending, another grid is bound and a glyph appended. The deferred consumer must still render N exactly. Resize is refused while busy; old arena retirement and actual opposite-size cache/grid replacement happen after T5/T6. Clearing runtime pages at bind produces the red below. Mutation removed; no renderer or lifetime behavior change required.

Red is a temporary mutation, not baseline failure:
```text
RED: review FAIL: memcmp(expected, state.pixels, (size_t)config.max_width * config.max_height * sizeof *expected) == 0
GREEN: review §13 GREEN: delayed backend borrows immutable pixels through T5, px=15
```

## Font §14

Verdict: Confirmed; fixed. font_load_file/stream validate required len, clear it on failure, and roll back the entry mark after a short read. A cookie stream deterministically advertises a font-size range then returns EOF; repeated attempts preserve a non-aligned arena prefix. Bad paths and NULL len are also checked. The stream seam is internal src/font/file.h, not another module.

```text
RED: tests/font_test.c:713: CHECK failed: arena.used == mark && len == 0
GREEN: review §14 GREEN: short reads roll back arena and clear lengths; NULL length rejected
```

## Font §15

Verdict: Confirmed; fixed. The bench uses harness normalization/evidence helpers. Empty, NULL, unreadable and unrecognized power states remain unknown; Charging/Full/Not charging map to AC and Discharging to bat. Pure self-check covers normalization and tags.

```text
RED: review §15 RED: unknown power classified AC
GREEN: review §15 GREEN: unknown power stays unknown
```

## Work-scan §9 (font portion)

Verdict: confirmed; fixed. Opaque fontconfig initialization/matching is executed
by posix_spawn of /usr/bin/fc-match, without a shell or multithreaded fork
callback. Parent CPU work reads/parses fixed 512-byte chunks with cancellation
polls, nonblocking pipe/waitpid and short polling sleeps. Cancellation kills
and reaps the owned child and suppresses adoption. TTC indices, file paths,
coverage ranges and monochrome status are validated; unavailable CLI yields
embedded-only fallback. Child output has bounded storage and a cleanup timeout.
Synchronous soname discovery is retained only for exclusively owned INIT use.
The production-job regression overrides the executable with a test child that
never returns, then cancels, requires physical completion/reaping and no event.
Lineidx’s portion of this review finding remains outside this bead.

```text
RED: tests/font_test.c:787: CHECK failed: pid != 0
GREEN: review work-scan §9 GREEN: production discovery cancels and reaps an unbounded helper; no adoption
```

## Verification

Pending full-build, sanitizer, fuzz and once-only final TRACK bench results.
Verification began (M)[AC], Not charging, one-minute load 3.86.

---

## Final message (codex -o)

Scoped WIP verified. [Report with per-finding red/green evidence](docs/worker-reports/edit-457.19-s8.md).

`make all`, `make check`, and fuzz passed. Release allocation guards measured zero allocations (M)[AC]. Leak detection was disabled; coordinator rerun required.

Loaded bench tail caused exit 1, recorded as TRACK. Automatic editor font-cache ownership/recovery remains separate wiring. No git writes.