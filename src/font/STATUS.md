# Font status — P4.11 / edit-457.11

Implemented: fixed-arena, append-only Unicode cluster cache and layout glyph
callback; embedded + discovered CJK/monochrome emoji family loading with TTC
face indices; common baseline, coverage-preserving combining composition;
two-cell images using utf8 widths; immutable fallback atlas pages; exact keys
including width; negative missing-glyph caching; bounded exhaustion with old
entries usable; no I/O/fontconfig/libc allocation on glyph lookup.

The headless Unicode suite enumerates the actual corpus-covered scalar set,
checks the full corpus's cell grids, compares computed pixel expectations to
scalar/SSE2 raster at both baked sizes, checks invalid bytes (one inverse '?'
per byte), clipping, cache exhaustion/page growth and no allocations on cold
and cached paths. Font parser/malformed-font fuzz behavior is unchanged.

Integration: prepare font_fallback on WORK_BULK; acquire its publication, load
font_family on INIT worker, hand off through work mailbox. Reserve cache on
INIT, bind grid with font_cache_bind, set layout_config.glyph=font_cache_glyph
and glyph_ctx=&cache. Keep fonts sized and immutable and keep all arenas alive
until quiescent renderer shutdown. Cache/grid ownership is UI-exclusive; one
bound grid at a time. No caller integration outside this bead's allowed files
was attempted.

Scope limits: only the discovered CJK/emoji fallback faces are searched, not
all installed fonts. Missing visible components use layout's '?'. Invisible
sequence controls need no glyph. No bidi/IME/contextual script shaping/emoji
ligature engine; covered ZWJ spacing outlines overlay in their shared image.
Standalone zero-width clusters and >layout-window composition retain P3.1's
existing behavior. Cache eviction, concurrent worker raster/cache append and
runtime size changes are outside this implementation.

Verify with the commands and campaign evidence in docs/decisions/P4.11.md.
Existing bench gates were not loosened. Shared-machine measurements are TRACK;
the coordinator must establish quiet-box font/layout/G1 verdicts and re-run
LSan enabled (this sandbox run uses detect_leaks=0).

Final verification: GCC make all PASS; release font/layout/Unicode PASS;
Clang ASan/UBSan make check PASS (26 binaries and replay CLI) using :99 outside
socket restrictions, with LSan disabled. make fuzz builds 14 fuzzers; Unicode
font campaign: 35,689 runs in 61 s, no findings (M)[AC], load 4.14.
Full corpus: 529,389 covered / 19,307 uncovered visible clusters; no covered
placeholders, cold-layout allocation guard zero (M)[AC], preceding load 5.98.
Computed pixel expectation equals scalar and SSE2 at 15/30 px.

Final benches (M)[AC], load 4.19, ns p50/p99: font raster 4912/5322 (gate p50
50000 unchanged); cold composed cluster 24215/24599; cached 45/48; real-cache
Unicode full viewport 345021/403526; Unicode typing row 2154/4080. Font gates
pass. Layout bench exits with pre-existing ASCII/log gate misses; 10% TRACK
comparison unresolved (log p50 +39.13%, malformed p99 +16.08%, other rows
improve). Layout source is unchanged; no repeated quiet-seeking runs. Quiet
coordinator verdict remains necessary. Large cold clusters have linear
composition cost; these benches are not whole-path G1 certification.
