# Layout status — P4.1 / edit-457.1, finishing run 2026-10-09

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
