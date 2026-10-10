# Font status — P4.11b / edit-457.19

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
