# Layout status — P3.1c / edit-zzj.11, 2026-10-09

Review §§5–6 of docs/reviews/view-1.md are fixed. Wrap-off end-of-line
cursor rendering resolves the logical stop before CRLF, including exact
checkpoint seeks that start at LF. LF scanner/checkpoint and next-row offsets
are unchanged. Public viewport and runtime hscroll are now uint64_t, matching
view and absolute column checkpoints; existing callers must rebuild.
Wrap-on still ignores horizontal scroll. No wrap implementation, view/editor
source or frozen header was changed. See docs/decisions/P3.1c.md for red/green.
P4.I integration adds optional `layout_set_paint` byte decorations before cursor/
selection colours in both wrap modes, including cells where repeated cursor
styling is suppressed. NULL preserves the original layout behavior. The callback
is UI-owned, allocation-free and stable for a run. Editor loop tests cover bracket,
EOF/clipped windows and trailing-space/tab paint with null and raster backends.
No wrapping/CRLF/4 GiB algorithm fix is included. Evidence: ../../docs/decisions/P4.I.md.

Implemented: opt-in streaming word wrap at text-area width; capped leading-indent
continuations; blank continuation gutters; intact Unicode clusters and wide
pairs; CRLF-aware endpoints; visual-row descriptors and scrolling seeds;
wrapped cursor/selection rendering and soft-End affinity; visual navigation
adapter in view; per-buffer flag and .md/.txt/.tex/untitled open-time defaults.
No font or frozen-header changes. Wrap-off uses the existing P3.1 path.

Regressions cover actual view End/Right/document-End stops for LF, CRLF, lone
CR and EOF in both wrap modes and slice configurations, indexed CRLF endpoints,
partial relayout and snapshots. A compact tab fixture with worker-built exact
columns checks view-to-layout scroll preservation beyond UINT32_MAX, visible
EOF cursor, partial relayout and large-column wrapped descriptors.
Release `make all`, `make fuzz` (22 fuzzers), both release/ASan/UBSan module suites
and full `make check` (42 test binaries plus replay CLI checks) pass.
Layout fuzzer: 1,192 runs in 122 seconds, no failures or artifacts (M)[AC],
BAT0=Not charging, load1=5.58 before launch; seed=311, max_total_time=120,
max_len=4096, timeout=30. The full sanitizer suite required outside-sandbox
Xvfb :99 socket access; ASAN_OPTIONS=detect_leaks=0, coordinator reruns leaks on.
One full layout bench ran at 2026-10-09 08:02:29 UTC, (M)[AC], BAT0=Not charging,
load1=6.07 (wrapped subcampaign load1=6.91). Exit 1 for shared-box timing misses;
indexed wrap-off rows passed. All rows are TRACK; no retry/tuning/gate change.
Raw rows are in the worker report, selected gated rows in P3.1c.md. No scoped
correctness work remains. Existing P4.1 limitations below remain; coordinator
owns gate verdicts and leak-enabled verification.

## P4.1 implementation and earlier verification

Module implementation complete: opt-in streaming word wrap at text-area width,
capped leading-indent continuations, blank continuation gutters, intact Unicode
clusters/wide pairs, CRLF-aware endpoints, visual-row descriptors/scroll seeds,
wrapped cursor/selection rendering, soft-End affinity and visual Up/Down/Home/End.
Per-buffer defaults enable .md/.txt/.tex/untitled and leave code wrap-off.
No frozen headers changed. Existing default wrap-off scan/render behavior remains.

Wrap is isolated in wrap.c/private wrap.h; layout.c has dispatch/geometry hooks
and shares its bounded decoder/window/cache. Storage remains two caller-arena
descriptor arrays, reserved at open. Newline-free edits plan the affected logical
line, shift its cached suffix, and paint it; shrinkage decodes newly exposed
bottom rows. Busy layouts and geometry changes restart. Grid-width changes now
clear the entire repacked active extent in both wrap modes, including growth into
unused caller storage. No caller-side cell clearing is required on width changes.

Home's whitespace-row affinity fix was already on main. Its strengthened test
checks endpoint rendering, Shift-Down/Up, repeated Home and continuation Home.
Additional finishing-run red-green fixes cover wrap-off resize and edits inside
word lookahead discarded by a soft-break rewind. The bottom scratch descriptor
retains that examined endpoint while idle; repaint republishes it and suffix
byte shifts adjust it. This adds no storage/public fields/allocation. Edits in
the unused long-line tail still produce no damage. Both previous fuzz artifacts
now replay successfully. Exact red/green output is in docs/decisions/P4.1.md.

Tests cover widths growing/shrinking, toggling wrap, gutters, wide clusters,
poisoned inactive storage, sliced vs fresh one-shot layout, indentation/tabs,
visual motion, file defaults, newline edits, row shifts, checkpoints, snapshots,
hidden separators and grapheme/word lookahead. Final gcc `make all` and clang
ASan/UBSan `make check` passed, including 39 test binaries and replay CLI checks.
Release guards measured zero allocations over 10,000 relayouts and 10,000 typing
edits (M)[AC], BAT0=Not charging, load1=7.07; zero is the law-2 gate (G).

`make fuzz`: 20 fuzzers built. Layout campaign: 5,043 runs in 601 seconds,
no sanitizer/property failure (M)[AC], Not charging, load1=4.25 before launch;
seed=4571, max_total_time=600, max_len=4096, timeout=30. Width fuzzing compares a
reused sliced layout with freshly initialized one-shot output, with no manual
stale-cell clearing, and includes both growth/shrinkage and wrap toggles.

One full layout bench campaign at 2026-10-09 07:03:02 UTC: (M)[AC], Not charging,
load1=6.97 before launch; wrapped subcampaign load1=8.28. Shared-box TRACK only;
binary exit 1 correctly reports observed gate misses. Gates remain 150 us
p50/p99 for original wrap-off rows (G), 300 us for wrapped rows (G).

| Wrapped row | p50 / p99, us (M)[AC], load1=8.28 | Gate / observed outcome |
|---|---:|---|
| Unicode full viewport | 735.419 / 1006.498 | 300 / 300 (G), miss |
| ASCII prose full viewport | 731.102 / 3742.199 | 300 / 300 (G), miss |
| Indexed ASCII byte 100,000,000 | 705.048 / 764.190 | 300 / 300 (G), miss |
| Unicode typing logical row | 11.114 / 23.875 | TRACK |
| ASCII typing logical row filling viewport | 832.359 / 3833.118 | TRACK |

Original ASCII/log viewport rows also observed misses; indexed wrap-off column
rows passed. Full raw rows and stamps are in docs/decisions/P4.1.md. No retries
for quietness, gate relaxation or load-driven tuning. Deep wrapped layout scans
107,441 bytes and reads 131,072 bytes; descriptors use 43,200 bytes for this
viewport (M)[AC], Not charging, load1=8.28. No unused GiB-tail scan is performed.

Deep uncached queries preserve certified grapheme boundaries/checkpoint columns
but explicitly estimate width-dependent global word-wrap phase. An exact global
phase needs a visual-break index. Existing checkpoint lifecycle is unchanged.
The coordinator still owns the gate verdict, leak-enabled rerun and full editor
G1 integration. Wire view_wrap_file at open, visual_byte via layout_visual_row /
layout_begin_visual when scrolling, and visual_end via layout_set_cursor_visual.
Typing bench excludes mutation/submit; it cannot certify input-to-frame latency.
Config.toml overrides remain P6.4. No new unresolved module failure was found.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99: `make all`;
`ASAN_OPTIONS=detect_leaks=0 make check`; `make fuzz`;
`build/tests/{layout,view,wrap}_test`; `build/fuzz/layout_fuzz` with the recorded
campaign flags and an existing seed corpus; and one
`build/bench/layout_bench /tmp/edit-corpus` (or `'' --wrap-only` for wrapped rows).
Full X11 tests need socket access outside the sandbox; all commands target :99,
with existing X11 tests using private virtual servers. LSan is disabled only for
this sandbox; the coordinator reruns with leaks enabled.
