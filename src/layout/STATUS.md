# Layout status — P4.1 / edit-457.1

Implemented: opt-in streaming word wrap at text-area width; capped leading-indent
continuations; blank continuation gutters; intact Unicode clusters and wide
pairs; CRLF-aware endpoints; visual-row descriptors and scrolling seeds;
wrapped cursor/selection rendering and soft-End affinity; visual navigation
adapter in view; per-buffer flag and .md/.txt/.tex/untitled open-time defaults.
No font or frozen-header changes. Wrap-off uses the existing P3.1 path.

Wrap implementation is isolated in wrap.c / private wrap.h. layout.c contains
hooks and reuses its existing cluster decoder/window/cache. Storage is two
caller-arena descriptor arrays, reserved at open. Newline-free edits plan only
the affected logical line's visible rows, shift cached suffix cells/metadata,
then repaint that line. Shrinking exposes bottom rows; those alone are decoded.
Busy layouts/geometry changes retain full-restart behavior. Right-context edits
within a decoded unit's lookahead are included even just beyond the viewport.

Long-line visible layout never scans the unused tail. Deep uncached queries use
P3.1b's exact column checkpoints and certified boundaries, with an explicitly
estimated word-wrap phase. Cached visual seeds keep subsequent layout local.
Column checkpoints alone cannot give exact global word-wrap phase for arbitrary
widths; a width-dependent visual-break index would be needed for that guarantee.
No arbitrary UTF-8 window edge is a cursor boundary. Existing cluster scratch /
bounded oversized-grapheme behavior remains. P3.1b checkpoint.c is unchanged;
its lifecycle/publication instructions are in docs/decisions/P3.1b.md.

Verification: tests/wrap_test.c covers word/hard wrap, indent/tabs, wide/combining
clusters and motion, CRLF, narrow/gutter-only geometry, snapshots, file defaults,
soft-End affinity, hidden separators, row shifts vs fresh layouts, newline edits,
indent removal, deep checkpoints, and lookahead changes outside the viewport.
Release malloc guard reports zero over 10,000 relayouts and 10,000 typing edits
(M)[AC], Not charging, load1=6.34; zero allocations is the law-2 gate (G).
Existing layout_test/view_test also pass with their active guards.

Red evidence: undefined new wrap APIs before implementation; soft End incorrectly
stopped on the last character; hidden separator cursor missing; and an edit to
the next unit changed the final visible cluster while incremental output stayed
stale. All regression cases are green. One fuzzer artifact exposed the same
right-context issue and replays cleanly after the fix.

Gates stay unchanged: P3.1 wrap-off 150 us p50/p99 (G); added wrapped 300 x 360
rows 300 us p50/p99 (G); typing rows TRACK. Latest wrapped bench (M)[AC], Full,
load1=6.63, shared-box TRACK evidence only:

| Row | p50 / p99, us (M)[AC], load1=6.63 | Gate / outcome |
|---|---:|---|
| unicode.txt wrapped | 415.566 / 743.131 | 300 / 300 (G), observed miss |
| oneline_1g.txt ASCII prose wrapped | 404.459 / 715.078 | 300 / 300 (G), observed miss |
| indexed ASCII byte 100,000,000 | 352.654 / 609.868 | 300 / 300 (G), observed miss |
| Unicode typing logical row | 6.053 / 15.583 | TRACK |
| ASCII typing logical row (fills viewport) | 449.859 / 819.165 | TRACK |

Deep ASCII scan consumes 107,441 bytes and reads 131,072 bytes (M)[AC], load1=6.63,
independent of the unused GiB tail. Descriptor storage for this viewport is
43,200 bytes (M)[AC], load1=6.63. The deep query flags estimated wrap phase while
preserving exact checkpoint columns and grapheme boundaries. Typing bench times
exclude mutation/render submit; they do not certify the full G1 1.0/2.0 ms (G)
input-to-frame budget. The existing off rows also miss under load; no quiet
verdict is claimed and no limits were loosened. No bench retries for quietness.

Missing: coordinator's quiet-box gate verdict and full editor-loop G1 integration.
Wire view_wrap_file at open; pass visual_byte through layout_visual_row /
layout_begin_visual on visual scroll, and visual_end to layout_set_cursor_visual.
Config.toml overrides remain P6.4. Exact uncached global deep wrap phase remains
explicitly approximate with the current column-only index.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99: make all; ASAN_OPTIONS=detect_leaks=0
make check; make fuzz; build/tests/{layout,view,wrap}_test; and
build/bench/layout_bench /tmp/edit-corpus '' --wrap-only (or the complete bench).
Full X11 tests need socket access outside the sandbox; final verification used
private virtual Xvfb servers, never :0. LSan remains disabled per the sandbox
restriction; coordinator reruns with leak detection enabled.
