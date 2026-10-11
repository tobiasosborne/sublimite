# edit-5ih: worker-prepared primary font selection

DejaVu Sans Mono remains the embedded default and the guaranteed fallback.
This worktree has no P6.4 config loader (`edit-v0o.7`) and no landed zoom
rebuild implementation. The interim startup sources are `EDIT_FONT_FAMILY`
and `EDIT_FONT_FILE`. `editor_font_request` is the allocation-free UI hook for
P6.4's future `font.family` / `font.file` parser and inotify callback. The
loader must retain/retry a request returning `EDITOR_MORE`; no request is
silently replaced while its predecessor prepares.

`font.file` wins when present. A trailing `#decimal` selects the physical TTC
face; named variation instances are unsupported, matching the existing font
loader. A family uses the existing dynamically loaded Fontconfig API with
name parsing, config substitution, default substitution and matching. The
requested concrete family must occur in the returned face's family names;
otherwise Fontconfig's automatic substitution would disguise a missing
family. The generic `monospace` alias is supported. Other family aliases that
resolve to a differently named family currently fall back. No subprocess,
font discovery or file I/O runs on the UI thread.

A bulk worker loads immutable bytes with `font_load_file`, selects the TTC
face, and compares **unrounded design-unit advances** over printable ASCII.
A proportional face can have equal rounded widths at a small pixel size;
the regression fixture demonstrates that this is still rejected. Every
printable ASCII glyph must exist and have the same positive advance.

The worker rasterises with stb_truetype into a row atlas. Its glyph count is
(P) `0x7e - 0x20 + 1 = 95`, the inclusive printable ASCII range. File storage
derives from `stat`'s file length. Atlas storage derives from glyph count times
cell width times cell height, plus a temporary cell. Raster scratch reuses
`tools/atlasbake`'s existing reservation; exhaustion restores the embedded
atlas. It is not a timing budget. Glyph wall-clock deltas include possible
OS preemption, and are measured diagnostics rather than admission limits.

The ownership count is (P) active plus preparing, giving separate owners.
The worker's last owner write precedes publication through `src/work`.
Mailbox receipt only marks readiness. The editor adopts at an idle backend
frame boundary, before processing the next input, with one full layout
request. It recomputes cell geometry and the text/minimap/view dimensions.
Published data may be adopted before the worker physically returns: waiting
for that return could miss the sole mailbox wakeup. Owners and arenas are
retained through backend retirement and physical work completion before
reuse or release. A regression holds each of those boundaries independently.

Pixel viewport dimensions survive swaps rather than accumulating truncation
from old cell counts. Grid dimensions are `min(reserved cell capacity,
floor(viewport pixels / new cell size))`, with the existing nonempty-grid
clamp. The fixed editor `max_cols` / `max_rows` contract still applies. A
narrower font can therefore leave unused pixel width when the configured
column reservation is exhausted, including the benchmark fixtures below.
The embedded atlas always fits. Selection admission bounds each cell axis by
requested pixel height plus a rounding pixel; height follows separate
ascent/descent rounding, and the same width bound sizes minimap/atlas
reservations. Unusually wide faces fall back. Backend pixel reservations
retain their original viewport bounds.

Any worker failure publishes the embedded atlas, including a failure after a
successful custom selection. A worker logs failure once per editor lifetime.
An invalid startup environment value is rejected during setup and its
diagnostic is queued on a worker. Parsing,
request submission, readiness, adoption and typing do not allocate. The
release test measures allocations from input ingress to submit before and
after selection; its (G) required count is zero, derived from CLAUDE Law 2.
ASan owns malloc, so that test explicitly labels its sanitizer guard inert.

Use **Iosevka Fixed or Iosevka Term** for one glyph per cell. There is no
HarfBuzz, ligature substitution or contextual-alternate shaping. This box
has `Iosevka` TTC faces, while the concrete family name `Iosevka Fixed` does
not resolve here; matching it correctly keeps embedded DejaVu. The benchmark
uses the actually installed `Iosevka:style=SemiBold` as scalar glyphs.

## NEW baseline

Command on the final build:

```sh
EDIT_TRACE_DUMP=build/edit-5ih-final-font.trc build/bench/editor_bench '--font-baseline=Iosevka:style=SemiBold'
```

The sample count (G) is the harness's existing `BENCH_INTERACTION_MIN_N`,
not a new budget. The G1 workload retains the existing mapped corpus and
alternating insert/backspace, with recovery journaling. G3 retains the
existing tab-switch fixture. All timings below are (M)[AC], `BAT0=Charging`;
load is read from `/proc/loadavg` before each row. Nanoseconds derive from
monotonic endpoint subtraction. Percentiles use the harness's nearest-rank
`ceil(q * sample_count)` order statistics; confidence intervals use its
existing binomial order-statistic calculation. Gate columns are inherited
aspirations from the existing benchmark; these runs explicitly use TRACK.
Counts derive from completed input records, submitted mutations, allocator
interposition, and minimap fill counters. Zero allocations follows the
active release allocator guard, not ASan's inert guard. Cell values derive
from the prepared face; grids are capped viewport division as described
above, and output pixel extents are grid times cell size.

```text
STAMP (M)[AC] BAT0=Charging load1=8.48 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
NEW_FONT_BASELINE family=Iosevka:style=SemiBold cell=13x30 grid=180x60 pixels=2340x1800 swaps=1 (M)[AC] derivation=worker_atlas_cell,viewport_div_cell,adopted_generations
FONT_RASTER atlas_ns=1188964 glyph_max_ns=33848 glyphs=95 (M)[AC] derivation=monotonic_end_minus_start,maximum_per_glyph,ASCII_last_minus_first_plus_one
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=10000 required_n=10000 p50=1882081 p99=4209455 ci95_p50=[1872512,1892470] ci95_p99=[4093964,4416128] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=8.48 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=10000 required_n=10000 p50=1856483 p99=4091621 ci95_p50=[1847163,1866023] ci95_p99=[3991302,4321357] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=8.48 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=1882698 p99=4211614 ci95_p50=[1873248,1893145] ci95_p99=[4096218,4418572] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Charging load1=8.48 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
TRACE null T2-T1 n=7281 p50_ns=81193 p99_ns=149978 (M)[AC]
TRACE null T3-T2 n=7282 p50_ns=1709721 p99_ns=2295700 (M)[AC]
TRACE null T4-T3 n=7282 p50_ns=62744 p99_ns=95517 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=7.50 shared box
NEW_FONT_BASELINE family=Iosevka:style=SemiBold cell=6x15 grid=360x120 pixels=2160x1800 swaps=1 (M)[AC] derivation=worker_atlas_cell,viewport_div_cell,adopted_generations
FONT_RASTER atlas_ns=502609 glyph_max_ns=15597 glyphs=95 (M)[AC] derivation=monotonic_end_minus_start,maximum_per_glyph,ASCII_last_minus_first_plus_one
BENCH name=editor_null_100tabs_ingress_T5_G3 n=10000 required_n=10000 p50=578208 p99=862915 ci95_p50=[577357,578959] ci95_p99=[790608,888343] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Charging load1=7.50 verdict=TRACK
BENCH name=editor_null_100tabs_minimap_inside_frame n=10000 required_n=10000 p50=77938 p99=95425 ci95_p50=[77923,77948] ci95_p99=[95258,96710] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Charging load1=7.50 verdict=TRACK
G3 null (M)[AC] load1=7.50 tabs=100 A=2880x1800 switches=10000 allocations=0 minimap_fills=10002 stale=0 mode=TRACK
```

The null backend measures editor scheduling/layout/submit only. These are
**NEW baselines** for the reported cell geometries; they are not regression
comparisons to old rows. Native raster/GL measurements and the Astra
comparison drive require the coordinator's display environment.
`--font-baseline=PATTERN` requests a post-startup swap for each existing row
fixture, then reuses its established workloads and sample counts. It does
not change the standard benchmark campaign.

The inherited `A=` label is the pre-swap target. `NEW_FONT_BASELINE` reports
the actual post-swap pixel extents, including the fixed cell reservation cap.
The glyph maxima are wall-clock spans, including possible OS preemption;
these particular measured spans fit the brief's estimate. That estimate is
not enforced as a typing gate. The trace is the retained G1 ring; its event
sample counts are lower than the full run because the fixed ring overwrites
old records. Pairing counts come from trace_fmt, not a proposed budget.
