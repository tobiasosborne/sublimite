# edit-m3g — true em scaling and embedded primary

Added font_set_em_px alongside the retained pixel-height API, and embedded the
vendored immutable DejaVu outlines for editor worker atlas preparation. The
editor now supports dynamic ASCII sizes; its reserved atlas versions and frame
lifetime are owned in the editor module. Unicode primary/fallback integration
remains with its existing bead and must consistently use the true em scale.

Evidence: [worker report](../../docs/worker-reports/edit-m3g.md).
Derivation and resource choices: [decision](../../docs/decisions/edit-m3g.md).
# edit-73j editor runtime integration

The editor now prepares/adopts the runtime owner through the worker mailbox,
reserves the existing runtime/GL descriptor cap and atlas limits, binds body
and title lookup, and keeps owner storage through backend shutdown. The
existing descriptor cap is exported as `FONT_RUNTIME_MAX_GLYPHS` with unchanged
validation. Actual-editor Unicode body/title cell/pixel regressions pass;
native display acceptance remains with the coordinator. Evidence and limits:
[worker report](../../docs/worker-reports/edit-73j.md).

# Font status — P4.11b / edit-457.19

edit-08z production integration: the editor now owns font_async, using an
embedded copy of the primary face, worker-only exact-scalar fallback discovery,
cached face ownership and general cluster rasterisation. Cold UI requests use
reserved keys/descriptors and work mailboxes; stable pending slots adopt
append-only atlas rectangles and repaint only affected visible text rows.
Raster/GL INIT limits now include runtime pages/descriptors. No typing-path
allocation or font wait is added. Completed missing/resource refusals have
settled missing artwork; runtime question-mark placeholders indicate pending
rasterisation.

Headless initial/injected é, α, 日, 🙂 bitmap/width tests pass in release and
ASan/UBSan; combining marks preserve base ink and add ink in the same cell.
The release guard holds the bulk lane through matching input submission and
observes no heap calls, then verifies completion damage. Validation details,
sandbox display/IPC restrictions and fallback faces are in
docs/worker-reports/edit-08z.md; ownership/reservation derivations are in
docs/decisions/edit-08z.md. The existing font benchmark passes (TRACK, battery).
Native X/EGL, leak-enabled tests and Astra re-verification remain coordinator
work. Cache eviction/retirement and shaping/colour emoji remain unimplemented.

The historical missing-integration statements below describe the earlier tree;
edit-08z supersedes its baked-only editor path without changing the legacy
font_runtime API.

Review fixes: baked ASCII requires INIT SHA-256/face identity; full-advance
Mono marks stay cell-relative; zero-advance proportional marks use the base
pen; Unicode 15.1 default-ignorable controls preserve covered bases; full-span
UTF-8 validation precedes cache admission. The edit-tkw continuation also guards signed offsets and CFF conversions/errors; see docs/decisions/edit-tkw.md.

Cache: fixed-arena append-only pixels/slots through T5. Positive and separately
bounded negative tables have fixed probe limits and independent key storage.
Cold lookup composes bounded scalar slices, returns FONT_MORE and preserves
one exact pending image. Wrapped/unwrapped layout returns LAYOUT_MORE and
replays that key on its next slice; callers can check input between slices.
Different cold keys abandon pending work, hits preserve it. NOMEM records
sticky resource_error and layout_approximate; neither NOMEM nor MORE means
missing font content. No allocations or I/O in cache lookup.

Discovery: font_fallback_job uses an isolated installed /usr/bin/fc-match child,
with bounded parent work, cancellation, kill/reap and bounded output parsing.
Missing CLI gracefully yields embedded-only fallback. Results stay staged until
font_fallback_event adopts a live mailbox completion matching identity/generation;
full-mailbox publication retries cancellably. Reset before reuse, retain fb
through physical completion and message drain. Synchronous soname discovery is
exclusive INIT only; do not use it inside cancellable pooled jobs.

Tests independently compare ASCII, multiple marks, negative Mono/proportional
fallback marks, controls, covered ZWJ and VS15/VS16 pixels at both baked sizes.
A deferred render adapter borrows pixels through delayed T5 while UI rebinds
and appends; opposite-size cache replacement/arena retirement follows T5/T6.
Fuzzer exercises direct malformed spans, continuation work bounds, collisions,
exhaustion and small/normal-capacity caches. Bench has pure failure self-checks
and binding raster tail / cold-slice / pressure / cached budgets; TRACK only
on the shared box. Run it once, after verification, with power/load stamp.

Missing integration: the editor on main still binds only baked ASCII and has
no runtime font-cache owner. Wiring that owner and rotating exhausted cache
storage after T5 or quiescent shutdown with full damage/redraw remain outside
this font/layout review fix. The font API and deferred integration test provide
the status, correct-content retry and lifetime boundaries; no automatic editor
cache retirement is claimed. UI cache calls now admit bounded TT/CFF outlines, curve subdivision and bitmap work; excessive complexity returns FONT_ERR_INIT and may use a healthy fallback. General rasterization remains worker-only for larger workloads; scheduling cannot be given a hard wall-time guarantee. Full bidi/contextual shaping/color emoji remain out of scope.

Verify: make all; DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0
make check; make fuzz; FONT_FUZZ_UNICODE=1 ASAN_OPTIONS=detect_leaks=0
build/fuzz/font_fuzz -max_total_time=60 -max_len=16384. Regression selectors
and inherited per-finding red/green evidence: docs/decisions/P4.11b.md.
Session 8 results, limitations and bench observation:
docs/worker-reports/edit-457.19-s8.md. Coordinator must rerun with leaks on.
Final make all and make check exited zero; release allocation guards passed.
Unicode, seeded Unicode and parser fuzz campaigns completed cleanly. The
once-only loaded bench exited one on its long-cluster tail observation; this
is TRACK and does not establish a timing gate verdict.

edit-tkw / session 9: retained and verified the interrupted font changes.
Font admission caps the whole buffer at INT_MAX and proves supported cmap
relative additions before stb reads/narrows them. CFF preflight rejects
coordinates outside output-vertex range and distinguishes interpreter failure
from successful empty outlines; scaled endpoints/spans are checked before
conversion/allocation. Foreground raster has structural outline, subdivision,
point and scanline bounds. Isolated discovery owns a process group and bounds
pipe draining even after the direct child is reaped. Fuzz raw seeds always
select face zero; startup verifies every generated seed's A/B interpreter paths.

font_runtime provides INIT reservation, worker family/cache preparation,
authenticated mailbox adoption, grid binding and a layout callback. The editor
hook and cache retirement sequence are documented in docs/decisions/edit-tkw.md;
no src/editor changes or production editor Unicode integration are claimed.
Session evidence and remaining integration work: docs/worker-reports/edit-tkw-s9.md.

Session 9 final verification: make all and make check exited zero, clang
ASan/UBSan with detect_leaks=0 and UBSan halt enabled, normal user on :99.
Release font allocation guards and Unicode grid/pixel suite passed. Final
CFF-seeded font fuzz ran the requested 60 s clean, with UBSan halt enabled.
Leak-enabled rerun remains with the coordinator. Earlier shared-display
clipboard interference and the namespace-root permission-fixture failure
are recorded in the report; neither prompted out-of-scope source changes.

## edit-5ih: primary font selection

Worker-only Fontconfig resolution, file/TTC loading and raw ASCII advance
validation feed the editor's shared DPI/zoom atlas lifecycle. The selection
module owns primary file bytes for each retired/current version; it has no
separate atlas or mailbox path. The async Unicode owner borrows the prepared
primary descriptor and uses its size mode, with existing worker fallback for
missing scalars. Resolved paths are retained for zoom and bytes load directly
into an arena without stdio heap allocations. Oversized or unsupported faces
use embedded DejaVu within main's prepaid atlas/glyph/minimap caps.

The focused release and ASan/UBSan tests cover proportional widths hidden by
pixel rounding, installed family and file precedence, fallback, TTC geometry,
held frames, selected Unicode pixels at 96/243 DPI, zoom/reset, pending glyph
retirement, input allocation guards and outstanding-job teardown. Evidence and
native display/launcher exceptions are in
[the resolver report](../../docs/worker-reports/edit-5ih-resolve.md).

P6.4 is absent here: environment startup selection and editor_font_request
remain the interim source and loader hook.
