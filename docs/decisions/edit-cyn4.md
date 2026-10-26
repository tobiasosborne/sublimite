# edit-cyn4 — Windows fonts

## OS inventory before implementation

(M) Inspected all source/header files in `src/font` before editing. Compile red
is recorded in `build/worker-logs/win-compile-red.log`.

| File | Calls | Disposition |
|---|---|---|
| font.h, runtime.c | pthread_t, pthread_self | Diagnostic identity only (no owned thread): uint64_t and os_thread_self, as work_pool_worker_id does. An os_thread would incorrectly imply ownership. |
| runtime.c, fallback.c, async.c | nanosleep publication retry | os_sleep_ns, same inherited interval |
| unicode.c | clock_gettime MONOTONIC | os_now_ns |
| fallback.c | fopen/fread/fseek/ftell/fclose, stat | Production file loading via os_file_open/stat/read/close; preserve internal stream seam for Linux short-read test; os_stat_path |
| selection.c | open/fstat/read/close | os_file_open/stat/read/close; preserve cancellation and regular-file checks |
| async.c | stat | os_stat_path |
| discovery.c | pipe2/fcntl/posix_spawn/read/poll/kill/waitpid/close, clock_gettime, nanosleep | Entire implementation moves to discovery_posix.c; frozen os.h has no streaming child-output API. Discovery remains platform-specific. Clock, sleep and identity use os.h. |
| fallback.c | dlopen/dlsym fontconfig, strcasecmp | Synchronous discovery and family resolution move into discovery_posix.c, whole-file platform guard; Windows provides DirectWrite equivalents. |

## Protocol

Existing fallback results already carry face indices; retain result shape,
mailbox publication/adoption and cancellation validation. `have_fontconfig`
retains its ABI name and means the platform discovery service is available.

## DirectWrite lifetime and C interfaces

DirectWrite is loaded dynamically with `LoadLibraryExW`/`GetProcAddress`, with
`LOAD_LIBRARY_SEARCH_SYSTEM32` restricting DLL resolution; there is
no dwrite import-library dependency. The required interface is Factory2. COM
initialization is balanced on the calling worker; an existing apartment is
accepted without uninitializing it. Missing DLL/interface or any failed raster
operation returns to the existing stb implementation. Missing discovery gives
empty paths through the existing successful completion protocol.

The face lives in a module-private, call-scoped side object, rather than adding
an owning pointer to `font_t` or a global side table. A C COM font-file loader
and stream borrow the validated immutable bytes already loaded through os.h.
This also covers the embedded primary and avoids reopening a path that might
now identify different bytes. Each worker raster call constructs an isolated
factory and face; it releases analysis, face and file, unregisters the loader,
and releases the factory before the stack objects disappear. `font_t` remains
copyable by value, and arena reset/free cannot leak a COM reference. The cost
is repeated worker-side factory/face creation; a persistent owner would need
an explicit lifetime contract across all descriptor copies. No COM operation
runs in `font_raster_glyph_ui` or on the asynchronous owner's typing path.

All COM calls and implementations use C vtables; no C++ is introduced. Windows
thread fields are diagnostic `os_thread_self` identities, not owned native
threads. This is why the requested pthread migration uses `uint64_t`, rather
than an `os_thread` that nobody could correctly join.

## R8 coverage and placement

Factory2 `CreateGlyphRunAnalysis` uses `NATURAL_SYMMETRIC`, natural measuring,
disabled grid fitting and `DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE`. The alpha
texture uses `DWRITE_TEXTURE_ALIASED_1x1`: with this Factory2 antialias mode it
contains grayscale coverage, despite the historical texture enum name. The
native test requires intermediate coverage values, not just binary endpoints.
There is no ClearType texture, channel averaging, gamma conversion, RGB atlas
or colour-layer enumeration.

(P) DirectWrite em size = designUnitsPerEm * the existing stb design-unit-to-pixel
scale. This handles both hhea-height sizing and true-em sizing without changing
`font_set_px` or `font_set_em_px`. Natural design advances use that same scale
and the existing nearest-pixel rounding. For TrueType the analysis baseline X
is `(raw_glyf_xMin - native_leftSideBearing) * scale`: stb uses raw contour
coordinates whereas DirectWrite uses the horizontal writing origin. Applying
the translation before analysis preserves fractional phase. The translated
analysis bounds' left/top become bearing_x/bearing_y directly. Existing
cell placement/composition stays unchanged. Native bitmap bounds inherit the
stb worker's existing dimension/area limits.

ASCII always takes stb, including ASCII glyph IDs; checked-in atlas bytes are
unchanged. UI-bounded raster calls retain stb. Worker non-ASCII and GSUB/fallback
glyph IDs take DirectWrite and transparently retry stb on failure. The private
`font_raster_glyph_stb` reference backend and `SUBLIMITE_TEST_NO_DWRITE`
environment seam let conformance tests prove the fallback bytes, not merely a
successful return code. The GSUB regression suite selects this reference path
on Windows because its existing hashes describe stb output.

Round 2 withdraws the round-1 origin/extent tolerance and rectangle-perimeter
coverage argument. Neither follows from DirectWrite's filter contract; a
coverage sum also loses spatial information. Installed-font coverage and boxes
are observations only; advances still match exactly. The independent exact
spatial oracle and the limited measured embedded-font regression are described
below. Test sizing retains the existing large ASCII bake size.

## Font-owned Windows arena adaptations

The cache borrows a scratch subarena from its storage arena. Windows base
allocation commits pages relative to the arena's base, so that borrowed base
must itself be page-aligned. Only Windows changes its scratch alignment; the
async owner reserves one additional page as the worst-case alignment allowance.
The async pixel atlas accesses its reservation directly instead of allocating
through the arena, so Windows explicitly commits that entire existing extent
before the inherited INIT pre-touch. Neither fix changes src/base or os.h.

## Remaining integration boundaries

Windows family selection accepts exact installed family names, with the
internal `:color=false` suffix stripped; it does not implement fontconfig's
pattern language, generic-family aliases or style grammar. `file:PATH#INDEX`
continues to use the common parser and regular-file validation. Windows emoji
fallback accepts a font with monochrome outlines even if it also contains
colour tables; those layers remain outside this bead.

DirectWrite calls have no cancellation API. The worker checks cancellation
before/between queries and before staging/publication; cancellation suppresses
adoption, but physical completion waits for the current opaque COM call.
Linux retains its cancellable process-group/pipe implementation.

The Windows check list includes the standalone font and GSUB suites and the
new headless DirectWrite/async atlas suite. Editor/IPC/display-dependent font
suites require their owning modules' Windows migrations; this worker does not
stub them or silently claim that they ran. The report identifies the acceptance
gap for the coordinator.

## Coordinator decisions (session 22, 2026-10-26)

1. **Editor-dependent Windows font suites** (those that link the editor or layout integration) stay with their owning W2 migrations; the module-level coverage here (font_test, font_gsub_test, font_dwrite_test as Windows exes) is the W1.3 acceptance. Recorded on the bead.
2. **Call-scoped DirectWrite faces over the same immutable bytes, no owning pointer in the copyable `font_t`:** accepted; the cost is repeated face creation on the worker, outside the typing path. The Codex review (docs/reviews/edit-cyn4.md) scrutinises the COM lifetime.
3. **`tools/with_display_lock.sh`'s own `detect_leaks=0` for its shell-contract probes** is outside this bead and stays; it does not touch the sanitizer test binaries.
4. Landing after the review's triaged fixes, rebased over W0.5 (both widen `mk/win.mk`: keep both module and test lists).

## Round 2: review findings → change → test

| # | Finding | Change | Test |
|---|---|---|---|
| 1 | Different TrueType horizontal origins | Translate analysis by `(raw_xMin - native_lsb) * scale` before rasterizing; report translated analysis bounds to cell placement | Select negative/positive lsb glyphs from embedded metrics; alter only hmtx bearings and require identical native and stb artwork before/after, including placed cells. Compare native/stb placed ink edges using explicitly measured per-edge deltas. |
| 2 | Covering fallback need not have monochrome artwork; no retry | Load candidate bytes and selected face through the common initializer, render grayscale and require ink for visible scalars; preserve known blank/ignorable scalars. Retry the installed family list below on rejection. | First-candidate path seam supplies a covering empty-outline face or unsupported file; both must fall through to a usable installed face. Space remains accepted; real U+1F600 integration retains Segoe UI Emoji with monochrome ink. |
| 3 | General Han lookup forced to Simplified Chinese | Call-scoped locale from `GetUserDefaultLocaleName`, empty neutral locale on API failure; `zh-CN` retained only for the representative CJK probe | Same general Han request with injected `de-DE` and `ja-JP`; inspect locale returned by the actual analysis source passed to MapCharacters. Async integration allows its user-locale Han face to differ from the representative probe. |
| 4 | DLL search includes unsafe locations | `LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)` | Observe actual flags; fail library load and require exact public stb artwork plus empty native owner. |
| 5 | Aggregate coverage accepts misplaced/corrupt artwork; unsupported bound | Remove general cross-filter tolerances; exact analytic integer-edge L fixture with independent cell placement | Native and stb equal analytic origin, advance, extents and every pixel; oracle rejects row flip, ink shift with unchanged box/sum, and bearing corruption. |
| 7 | Early failures bypass real resource acquisitions | Test-only compile-time failure points after successful acquisition; observe Release returns, unregister HRESULT, stack loader/stream final refs and DLL/COM calls. Production objects have no test indirection. | Fail after registration, file, face, analysis, bounds, pixel allocation and texture creation; nonzero arena mark restored, scalar/id public fallback exact. Repeat in uninitialized/MTA/STA threads. Missing export/interface and unavailable discovery/async delivery also checked; UI lookup/pump/adoption make no native calls. |

`MapCharacters` provides no next-candidate cursor. The documented retry order is
Segoe UI Symbol, Segoe UI Emoji, Segoe UI, Arial, Microsoft YaHei, MS Gothic,
Consolas. Every candidate independently has to cover, initialize and render the
scalar; color tables neither qualify nor disqualify it. The test's empty base
outline reproduces the monochrome failure of color-only artwork. (M) On Spiral
U+1F600's first mapped face is `SEGUIEMJ.TTF`, face zero, with nonzero grayscale
ink, so it is not a real color-only example here. This does not claim exhaustive
coverage of all installed Unicode fallback choices.

For a German user, general Han lookup supplies `de-DE` to the system fallback
service. The chosen Han variant follows Windows' policy for that locale and the
installed fonts; it is no longer explicitly forced to a Simplified Chinese
variant. A German locale does not encode the document's language, so selecting
Japanese/Traditional Chinese document-specific variants would require a future
language preference. Only the representative U+4E2D probe remains `zh-CN`.

(P) Analytic fixture derivation: embedded units-per-em is 2048; em size 32 gives
scale 1/64. The axis-aligned L is the disjoint union of `[2,5) × [-10,0)` and
`[2,10) × [0,2)` in baseline pixel coordinates. Its advance is 1024/64 = 16,
left/top is (2,-10), width/height is (8,12). All edges fall on pixel boundaries,
so floor/ceil rounding error is zero for each quantity. Pixel integration gives
area `3*10 + 8*2 = 46`; total ideal coverage is `46*255 = 11730`. The test checks
each sample and its cell position exactly, not just that sum. This is a fixture
oracle, not a universal bound on DirectWrite's antialiasing filter.

(M) Embedded-font placement regression [AC, desktop Spiral]: the metric-selected
glyphs are U+0132 (lsb=-1) and U+0021 (lsb=516 design units). At the inherited
30-pixel bake size, R8 half-coverage threshold is floor(255/2)=127. Placed
native/stb boxes are `(0,5,16,24)/(0,5,15,24)` and
`(6,5,9,24)/(6,5,9,24)` respectively. Only the first glyph's right threshold
crossing differs: `16-15=1`; every other edge delta is zero. These are exact
measured expectations for these immutable bytes, size and filters, not physical
tolerances for other glyphs. Fractional contour positioning and different AA
filters can change threshold crossings. The bearing mutation is a quarter em,
2048/4=512 design units, much larger than those observed filter differences;
native pixels and placed cells must remain byte-identical after normalization.
Load sample during verification: `12:11:02 ... load average: 11.14, 6.06, 4.01`.

The candidate probe uses the existing large atlas size and reserves the existing
native bitmap maximum `4096*4096` R8 bytes; immutable file reservation is its
measured file size. Blank acceptance uses explicit Unicode spaces, braille blank
and existing default-ignorable artwork, never zero cell width alone.

Findings 6, 8, 9 and 10 remain with the coordinator's backlog. The analytic fixture
uses true-em sizing only to make its geometry exact; it does not claim to close
the deferred GSUB/mark/size coverage finding. Native teardown has a final
factory Release count of zero and stack loader/stream counts of one (their
initial owner reference); these are ownership invariants, not leak estimates.
