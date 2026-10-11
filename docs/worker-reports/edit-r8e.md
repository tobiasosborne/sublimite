# edit-r8e — worker report

Implemented the accepted unshaped emoji fallback in document cell assignment.
Both wrap paths request the first emoji's artwork for a ZWJ grapheme, preserve
the complete grapheme's cells and byte boundaries, omit the specified invisible
presentation scalars, and leave genuine missing coverage visible. The async
font owner and stb rasteriser are unchanged. No git or bd commands were run,
and no display was opened.

## Where the box comes from

The editor sends a complete grapheme to the async font owner, whose cache
resolves individual scalars and overlays their outlines. It does not ask a
font for one unknown ligature representing the sequence. The cache already
ignores U+200D and variation selectors before resolving/rasterising them.
Missing coverage for any retained scalar aborts composition and publishes the
reserved replacement image for the whole request.

For the reported `👩‍💻`, the first scalar itself has no supported monochrome
coverage on this machine: `fc-list ':charset=1f469' -f '%{file}\n'` reports only
`/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf`. The monochrome discovery
query for U+1F469 selects a face whose charset lacks that scalar, so the runtime
correctly rejects it. U+200D is not the cause. The new layout rule nevertheless
fixes the separate cluster problem: when the first emoji is covered, later
joined scalars cannot overlay it or turn it into a missing image. The exact
reported sequence is tested with a deterministic covering callback, and its
production slot is checked against a standalone first-scalar request. The
covered `😀‍💻` production case exercises actual resident emoji artwork.

`👩‍💻` can still legitimately show the missing image here because `👩` itself
is unavailable to the settled monochrome runtime. This worker did not add a
different fallback emoji, colour-font support, a shaper, or a rasteriser exception.

## Red run

Before production edits, built and ran the new layout, Unicode-width and
production-editor regressions with clang ASan/UBSan:

```sh
make -j4 build/san/tests/layout_test build/san/tests/utf8_test build/san/tests/unicode_render_test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/layout_test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/utf8_test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/unicode_render_test
```

All returned failure. Captured evidence is in `build/edit-r8e/red-build.log`,
`red-layout.log`, `red-utf8.log` and `red-unicode.log`:

- Layout failed the exact callback-byte assertion: the full cluster reached
  the glyph callback instead of the first emoji. Invisible-only skin-tone
  cases also produced cells and callback requests. Its footer reports
  `layout_test: 304 FAILED` (M)[AC], derived by the suite's failed-assertion
  counter across both wrap modes and sliced/unsliced/revisited cases.
- The width suite reports `utf8_test: 11 FAILURES` (M)[AC], derived by its
  assertion counter: the exhaustive width comparison, scalar skin-tone
  assertion loop, and standalone skin-tone cluster assertions disagreed with
  the new zero-width policy.
- The editor failed `cell->atlas_slot == base` for the original sequence,
  followed by `emoji_fallback(false, false) == 0`: the cluster occupied its
  own async key/slot instead of requesting the first emoji's key.

An initial leaks-on run could not complete LeakSanitizer: it reported
`LeakSanitizer does not work under ptrace`. Subsequent sanitizer runs disable
only leak detection; ASan and UBSan remain enabled. The recorded red runs above
are assertion failures, independent of that sandbox limitation.

The initial genuine-missing fixture used U+10FFFF. Its green pixel comparison
exposed actual coverage in installed `Asana-Math.otf`, confirmed with
`fc-list ':charset=10ffff'`. The fixture now uses unassigned U+0378, for which
`fc-list ':charset=378'` reports no face. Its final image is compared to the
runtime's replacement artwork, not merely checked for nonempty ink.

## Change and green run

`utf8_cluster_artwork` removes the specified presentation scalars and truncates
an Extended_Pictographic-based cluster's artwork at its first ZWJ. Shared scalar
width now treats all skin-tone modifiers as zero; generated Unicode tables and
grapheme segmentation remain unchanged. `layout_cluster_glyph` reuses layout's
existing saved-cluster buffer and calls the glyph adapter with this artwork key
and the **original** display width. It serves both wrap paths. Ordinary combining
ink is preserved; non-emoji ZWJ conjuncts retain their visible scalars. Deferred
callbacks replay the original source bytes.

The final release build passed:

```sh
make -j4 all
```

The combined final build also built the named sanitizer binaries and
`build/fuzz/utf8_fuzz`; it returned success. Build evidence:
`build/edit-r8e/make-all.log` and `final-build.log`.

With `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`, all of these
headless runs passed:

- `font_test`, `font_size_test`
- `layout_test`, `wrap_test`
- `utf8_test`, `utf8_tables_test`
- `unicode_render_test`
- `editor_test --headless`
- `editor_scroll_test`, `editor_session_test`, `editor_hot_exit_test`
- `config_editor_test`

The editor command used an isolated runtime directory:

```sh
XDG_RUNTIME_DIR="$PWD/build/edit-r8e/runtime" \
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  build/san/tests/editor_test --headless
```

Per-suite logs are `build/edit-r8e/green-<suite>.log`. The main editor footer
is `editor_test: headless passed`. The Unicode regression passes initial and
scalar-by-scalar injected input in both wrap modes, all specified modifiers,
invisible-only input, exact first-scalar slots, wide-cell ownership, full
document-byte preservation, and genuine missing-glyph pixels. Existing accented
Latin, Greek, CJK, emoji, combining overlay, scalar/SIMD and async completion
tests also pass.

Release `build/tests/layout_test` and `build/tests/unicode_render_test` passed
with the counting allocator active. The new injected-input cases hold the bulk
worker and assert no heap calls from each scalar's input through submit (G: law
2). The saved-cluster test also guards in-place filtering and a deferred glyph
retry across decode slices. Release evidence is in
`release-layout_test.log` and `release-unicode_render_test.log`.

The extended Unicode fuzzer passed a seeded corpus smoke:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  build/fuzz/utf8_fuzz -runs=0 build/edit-r8e/artwork-seeds
```

The seeds derive from the regression strings, a cluster longer than the existing
decode budget, and malformed UTF-8. This runs the supplied corpus without a
random mutation campaign. `fuzz-artwork.log` records success. The fuzzer checks
exact-span/in-place equivalence, bounded output and omission of presentation
scalars from artwork keys.

## Bench observations

Ran `build/bench/utf8_bench`, `build/bench/layout_bench /tmp/edit-corpus unicode.txt`
and `build/bench/font_bench`; all returned success. Before measuring, read
`/sys/class/power_supply/BAT0/status` and `/proc/loadavg`; power was
`Not charging`, classified [AC] by the existing harness. This was a loaded box,
including overlap with related headless checks. The harness logs stamp power
and load. These are observations, without a before/after attribution.

`utf8-bench.log` reports a timing MISS on the long Prepend resumable-width row;
`layout-bench.log` reports a MISS on full wrapped Unicode layout. These remain
tracked observations under law 1. No timing gate, structural limit, or benchmark
fixture was changed. `font-bench.log` reports active allocation guards with no
heap calls in its raster/cluster loops. Sample counts, fixture sizes, units and
percentile derivations come from each existing harness row; the captured logs
retain the complete (M)[AC] evidence.

## Files changed

- `src/utf8/utf8.c`, `src/utf8/utf8.h`: shared width tailoring and artwork projection.
- `src/layout/layout.c`, `src/layout/wrap.c`, `src/layout/wrap.h`: allocation-free
  artwork requests in both document layout paths.
- `src/layout/layout.h`, `src/font/font.h`: clarify callback/cache contracts.
- `tests/layout_test.c`: deterministic first-emoji/control/missing cases,
  decode-slice scratch reuse, deferred replay and allocation guards.
- `tests/utf8_test.c`: shared-width oracle tailoring and standalone modifiers.
- `tests/unicode_render_test.c`: production initial/injected wrap-mode checks,
  original bytes, async slot identity and genuine replacement pixels.
- `fuzz/utf8_fuzz.c`: artwork projection properties on arbitrary graphemes.
- `docs/decisions/edit-r8e.md`, this report, `HANDOFF.md`, and
  `docs/worklog/2026-10-11.md`: decision, evidence and worker handoff.

## Out-of-scope findings and limits

- The only installed covering face for `👩` is a colour bitmap font. Supporting
  it is a font capability/discovery decision outside this layout bead. Genuine
  missing coverage must remain visible under the user's acceptance rule.
- Chrome/tab titles call the glyph adapter directly with original clusters;
  their artwork projection has not been changed. The shared scalar-width
  policy applies there, but the document's first-emoji request rule is confined
  to document layout. Raw font-cache callers still get scalar compositing.
- The optional `editor_large_test` passed its preceding headless cases but
  stopped at `mapped_startup_ipc`, on
  `ipc_server_init(&server, runtime) == IPC_OK`. A separate AF_UNIX socket probe
  returned `errno=1 Operation not permitted`, confirming the sandbox restriction.
  Its evidence is `green-editor_large_test.log`; no IPC code was changed.
- LeakSanitizer, native display suites, the full display-dependent `make check`
  and an Astra replay were not available in this sandbox. The requested
  headless `editor_test --headless` passed; native validation remains with the
  coordinator. No X access was attempted.

Worker scope is complete; stop here for coordinator review.
