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

DirectWrite is loaded dynamically with `LoadLibraryW`/`GetProcAddress`; there is
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
and the existing nearest-pixel rounding. Texture bounds are relative to pen
origin at the baseline; left/top become bearing_x/bearing_y directly. Existing
cell placement/composition stays unchanged. Native bitmap bounds inherit the
stb worker's existing dimension/area limits.

ASCII always takes stb, including ASCII glyph IDs; checked-in atlas bytes are
unchanged. UI-bounded raster calls retain stb. Worker non-ASCII and GSUB/fallback
glyph IDs take DirectWrite and transparently retry stb on failure. The private
`font_raster_glyph_stb` reference backend and `SUBLIMITE_TEST_NO_DWRITE`
environment seam let conformance tests prove the fallback bytes, not merely a
successful return code. The GSUB regression suite selects this reference path
on Windows because its existing hashes describe stb output.

(G) Same-face comparison tolerances: advances are exact; each bitmap origin may
differ by one pixel, each extent by two pixels (one edge pixel on either side).
Coverage-sum difference is bounded by `2 * (stb_width + stb_height) * 255`:
(P) a one-pixel full-coverage perimeter around the reference rectangle. This is
a conservative edge-filter bound, not an empirical percentage tuned to pass.
The report records actual differences. Test sizing uses the existing large
ASCII bake size; discovery probes are the specified CJK and emoji scalars.

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
