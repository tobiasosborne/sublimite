# edit-cxho.24 — round 3: margins, panel surfaces and pointer states

Candidate on `wt/design-s26`, 2026-10-28. The coordinator owns the commit.
The standard is `docs/design/rubric.md`; the round 2 palette and space-only
separation remain the base. All geometry is in whole cells (P: the render
contract has no fractional grid origin).

## Part A

- Layout owns the gutter: two blank inset cells, right-aligned decimal digits,
  two blank gap cells. Width is digits(line count) + 4 (P: 2 + 2); wrap, view
  columns, horizontal scroll, hit records and gutter line selection use it.
- The minimap ends one cell before the window edge and starts below the
  chrome spacer. Hidden maps retain the same outer right margin. The first
  tab label, text and panel input share the layout gutter's text column.
- The strip keeps its space padding and tone-only active state. A page-colour
  spacer row belongs to chrome and ignores clicks. Cost: one fewer text row
  (P: available rows − strip − spacer, rather than available rows − strip).
  Tiny windows retain a text row before reserving a spacer.
- Find/Replace and Command/Line number use muted placeholders, without prompt
  labels. Empty-field carets underline the first placeholder cell, leaving the
  placeholder readable; populated find/palette fields retain their caret.
  Go-to input/preview/colon semantics are unchanged.
- Find options are `Aa`, underlined `ab`, `.*`, with single-cell gaps; off uses
  gutter tone, on uses text on selection tint. Painting and half-open hits
  share `findui_get_geometry`. Counts use `n of total`, `No results` and
  `Invalid pattern`; error offsets stay in state. `Searching` and
  `Limited results` replace the old punctuation flags with stack-only text.
  Hints use muted ink and resident middle-dot glyphs. Small widths omit the
  summary as a unit to retain a usable input field.
- Palette and menu labels share the text column; chords end at the panel's
  right content edge in chrome tone. Selected rows use the selection tint.
  Menu surfaces span the row; outer margins do not execute menu items.
- All panel row backgrounds, including margins, use `panel.background`.
  Its dark/light OKLCH L is 0.285/0.968 (P: requested rungs). Hover tokens are
  generated now for Part B: tab hover L 0.800/0.370; hover wash is 6%/5% text
  into page in OKLab (P: requested fractions). Generated WCAG ratios:

  | Surface | Dark body / chrome | Light body / chrome |
  |---|---|---|
  | panel | 11.08 / 4.98 | 15.33 / 5.45 |
  | hover | 10.78 / 4.84 | 14.53 / 5.17 |

  Ratios are (P): linearised sRGB luminance, (lighter + 0.05)/(darker + 0.05),
  computed by `tools/mkpalette.py` after channel rounding. Tests enforce AAA
  body >= 7 and AA chrome >= 4.5 on both surfaces (P: WCAG thresholds).

### Cost lines

- Margins and spacer: 0 B of extra grid storage (P: existing prepaid grids
  and chrome rows are reused). Text loses three columns to the gutter and
  one to the outer margin (P: new digit + 4 vs digit + 1; outer margin 1).
  Spacer initialisation is restricted to full frames, with no row scan on
  ordinary typing frames. No extra dirty pixels per keystroke: unchanged
  chrome stays clean; changed text uses the existing row damage mechanism.
- Panels: no heap allocation. `findui_style` grows 40 → 64 B (P: new bounds
  2 × uint32 + three colours + one glyph slot = 24 B on the Linux ABI).
  Summary buffers remain fixed-size stack arrays; geometry uses another
  bounded summary scratch. Existing panel rows are repainted, adding no
  rows/pixels per field keystroke. No animation.
- Theme caches: eleven uint32 colours plus twelve syntax-scope uint32 values
  = 44 + 48 = 92 B of fields (P). Net editor size grows 840000 → 840064 B
  (M, compiler `sizeof`, same header with/without the fields; 64 B net after
  alignment/padding). Syntax captures and bracket/whitespace decorations now
  use cached colours too: no per-cell theme string lookup. Tokens add 96 B
  of constant tables (P: 3 tokens × 2 themes × sizeof(config_token)=16 B),
  plus 55 B of unique NUL-terminated names before compiler/linker pooling
  (P: lengths 17 + 21 + 17). Colours resolve when the theme applies.

### Red/green evidence

- Initial new `config_theme_test`: exit 1, missing surfaces/hover rung;
  after generation: exit 0. Logs `/tmp/r3-theme-{red,green}.log`.
- Initial `editor_design_test`: exit 1 for gutter, right margin, tab/spacer,
  panel prompts/surface. After implementation: exit 0. Additional empty
  placeholder tone assertion: red in `/tmp/r3-empty-red.log`, then green.
  Log `/tmp/r3-focused-a2.log` covers the spacer click with a moved caret.
- `findui_test`: updated field clicks and option offsets; exhaustive hit
  checks for every summary cell and its gaps; render validation and the
  existing field-typing allocation guard pass (`/tmp/r3-findui.log`).
- `layout_test`: updated decimal-gutter expectations, including width growth
  and mid-file right alignment, exit 0 (`/tmp/r3-layout.log`).
- Palette/menu/gutter/wrap/go-to/count contracts pass individually; the old
  palette fixture first failed because its height omitted the spacer, then
  passed after adding the requested chrome row. Modal matrix: 307 PASS,
  1 existing XFAIL, 0 FAIL (M: enum/action cross product and witnesses),
  `/tmp/r3-modal-a.log`.
- Full checkpoint attempt A1: exit 2, 09:36:51–09:39:46 +01:00,
  BAT0 Charging → Not charging [AC] (M, `/tmp/r3-check-a.log`).
  `editor_hl_test` inspected the minimap after the text measure narrowed;
  its deliberately blocked worker then reported a stack lifetime error on
  assertion-return and required terminating that test process. The fixture
  now reserves gutter/map/margin width, and its manual theme change refreshes
  the newly cached scope colours. This is a test-fixture change.

- Wrapped gutter evidence: `wrap_test` first failed its old width/number
  position assertion; after the wrapped painter and width-change detector
  adopted the shared geometry, it passed (red `/tmp/r3-wrap-red.log`,
  intermediate red `/tmp/r3-wrap-green.log`, green wrap section in
  `/tmp/r3-p4unicodewrap-a.log`; the other two suites in that combined log
  still failed and passed later as recorded below).
  Incoming-buffer IPC horizontal scroll now uses the same layout function.
- Render fixture evidence: `editor_p4_test` first failed its old tab-drag hit,
  then passed with hits relative to the strip. `unicode_render_test` first
  ran its last glyph into the missing spacer row, then passed after reserving
  that row. `/tmp/r3-p4unicode-a4.log` has both complete green suites.
- All default-command selectors pass (`/tmp/r3-defaults-a.log`); center and
  wrapped-line command fixtures preserve their previous text measure and
  text-row count while reserving the new chrome geometry.
- Additional `findui_test` checks cover `1 of 2`, `No results`, error-coloured
  `Invalid pattern` and selected underlined whole-word state;
  `/tmp/r3-findui-a2.log` exits 0.

Tracked module benches (M [AC], BAT0 Charging, shared load in log):
`/tmp/r3-benches-a.log`. Tabs visible-strip p50/p99 5989/7212 ns, all-strip
63012/80483 ns (M: sorted timing samples of the existing bench fixtures).
Find logical acknowledgement p50/p99 4157/40726 ns, caller-thread scans 0
(M: existing supervised interaction samples). Layout's complete tracked rows
are in the same log; these are shared-box observations, not a before/after
latency claim.

Release law-2 verification: null and raster each type 10000 keys with
`mallocs=0 guard=active` (M [AC], BAT0 Charging, 09:49:44 +01:00;
`/tmp/r3-alloc-a.log`; sample count is the existing allocation regression
fixture, not a new budget). ASan's allocator guard is compiled out, so these
release runs are separate evidence.

Full checkpoint attempt A2: exit 2, 09:49:41–09:51:08 +01:00,
BAT0 Charging → Charging [AC] (M, `/tmp/r3-check-a2.log`). The hot-exit
fixture's old width left only one complete inset tab slot, so its reorder
witness could not move a tab. Its width now preserves two complete slots,
and drag targets use the composed strip's first column (P: old test intent,
with the new inset/right-margin allowance).

The standard tracked editor benchmark completed null/raster/GL-request lanes
(`/tmp/r3-editor-bench-a.log`, M [AC]); all report `structural=OK`, allocations 0.
The GL-request lane fell back to raster on this Xvfb. Timing rows are TRACK,
with their existing short-run sample count; no new latency claim is made.

Checkpoint A GREEN: full `make -j4 check` exit 0; 115 test binaries plus
replay-CLI and display-lock contracts (M: make's final count).
Start 2026-10-28 10:00:33 +01:00, BAT0 Charging [AC]; finish 10:16:52,
BAT0 Not charging [AC]. Elapsed 979 s (M: timestamp difference,
16 × 60 + 19). Log `/tmp/r3-check-a3.log`. Source was held stable for this
run. Part B starts only after this green result.

Further focused evidence before the next full checkpoint:
`/tmp/r3-remaining-a2.log` has hot-exit and large-file suites green;
`/tmp/r3-remaining-a4.log` has scroll and session suites green.
Scroll metadata fixtures now derive layout rows as surface rows − chrome rows
+ overscan (P). The old fixed row assertions went red first.
`/tmp/r3-review-a2.log` has the panel-notice and headless review cases green;
their old tab-click coordinates and two-text-row reservation went red first.
The closed-drag witness still sends motion into the first visible tab after
its owner closes, so it continues to exercise capture cancellation.

Part A item map (indices are the brief's acceptance items):

| Item | Red evidence | Focused green evidence | Specific cost |
|---|---|---|---|
| 1 gutter | design geometry assertions; wrapped gutter assertion | layout, wrap, gutter click/growth/wrap selectors, IPC positions | 0 B storage; digits + 4 columns; no extra damaged rows |
| 2 right margin | design width assertion; old minimap origin/click test | design, minimap pixel reference and live minimap fixtures | 0 B; one fewer text column; no extra damaged rows |
| 3 tabs/spacer | design alignment/spacer assertions; old tab hits | design spacer click, palette/menu, hot-exit and capture cases | 0 B; one fewer text row |
| 4 find/replace | prompt/surface assertions; empty placeholder tone | exhaustive hit cells/gaps, count/error/state rendering, field clicks | bounded stack style/scratch; existing panel row damage |
| 5 other panels | Command/Go-to prompt assertions; old menu geometry | palette, go-to, menu, modal matrix | existing grid rows; cached colour reads |
| 6 panel surface | design background assertion | panel span check and both theme contrast checks | shared token/cache cost below; same row span |
| 7 tokens/cache | missing-token and hover-rung assertions | config_theme_test, syntax theme-change case | 151 B constant tables/names before pooling; 64 B net editor growth |

The zero-extra-damage statements refer to ordinary keystrokes after geometry
has settled (P: margins use existing cells; full resize/open/theme frames
already damage the whole surface). No allocation or keyboard animation is
introduced by any Part A item.

## Part B

The editor keeps one target (kind, physical row, logical index) and the wanted
pointer shape. Unbuttoned motion classifies the current region; an unchanged
target returns before starting a frame. A shape change alone changes no grid
cells. Captured drags retain their existing routes. X11 LeaveNotify and Wayland
wl_pointer.leave both produce PLAT_EV_LEAVE and clear the target.

An isolated hover frame bypasses text layout, find decoration preparation,
map preparation/source reads and ordinary text composition. It paints the
old/new affected chrome rows. An inactive tab uses tab.hover.foreground;
active tabs retain their tone. Find toggles use hover.background on their
cells. Palette/menu rows use that surface unless selected; selection wins.

The minimap wash moves from 32/255 to 48/255 on hover (P: one step = half
the existing base wash, 32/2 = 16; 32 + 16 = 48). The perimeter remains
96/255. Enter/leave reuses retained summaries and the existing independent
pixel region. No text cell row is marked dirty for that transition; the
current render contract uploads the whole map region, not a clipped band.

The find-hit cache records the last painted bounds and geometry. A native
capture exposed why this matters: a visible-highlight window refresh sets
state.searching while state.complete still describes a known total. Using
that unpainted word width moved the hit targets away from the visible toggles.
Hits now use the painted geometry; a hover paint touches only the toggle
cells and preserves the field/count. `Searching` describes an unfinished
count search (searching && !complete); refreshing highlights keeps the known
count, including `No results`. Overflow still reads `Limited results`.

### Pointer shapes and cursor files

Text/document and panel input regions request an I-beam; other regions request
the default arrow. X11 resolves the environment, then RESOURCE_MANAGER, then
default theme/size settings. The fallback size is 24 × Xft.dpi/96, rounded to
a pixel (P: requested 24 px reference at 96 dpi). Environment/resource explicit
sizes remain explicit. Theme lookup searches the requested user/system roots
and follows Inherits with a visited breadth-first queue, including cycles.

The allocation-free decoder checks file/TOC/chunk extents, little-endian
CARD32 values, matching image type/subtype/version, dimensions, hotspot and
pixel extent. It selects the nearest nominal size, retaining the first frame
on a tie. The image-header and dimension constants follow the upstream
[Xcursor format description](https://www.x.org/releases/current/doc/man/man3/Xcursor.3.xhtml).
Files are read only at window creation. ARGB32 is uploaded through xcb-render;
both themed shapes must load before either is installed. Failure is silent
and retains inherited cursor behavior. Pixmaps/pictures/GCs and temporary file
buffers are released; cursor IDs remain until platform shutdown. Cursor
attributes change only when the wanted shape changes.

Wayland leave is implemented and covered by the mock compositor. The
cursor-shape-v1 protocol header is absent from the vendored protocols, so its
shape setter uses the explicitly permitted no-op. Win32 uses cached shared
system arrow/I-beam cursors and WM_SETCURSOR routing; pointer leave remains
unchanged (the permitted stub). No Windows toolchain is installed here, so
that branch was not compiled or run on this box.

### Cost lines

- Hover model and shape: 16 B target + 4 B shape + two bool flags = 22 B of
  fields (P: Linux ABI). Net editor size 840064 → 840128 B, +64 B after
  alignment (M, compiler sizeof). Total growth over round 2 is 128 B
  (P: 840128 − 840000). No new grids, timers, jobs or typing-path allocation.
- Find painted geometry: bounds 8 B + geometry 12 B + valid bool 1 B,
  padded to 24 B (P). `sizeof(findui_impl)` 19952 → 19976 B (M). These are
  used bytes in the existing prepaid arena; its reservation is unchanged.
  Hover classification reads this cache, without a query-byte scroll scan
  or formatting the count. The style's new hover values/flag raise its stack
  size 64 → 80 B (M; fields 4 + 4 + 1 B, then ABI alignment).
- Tab/panel hover damage: at most the old and new affected rows, each
  cols × cell_w × cell_h pixels (P: full-width row strips are the existing
  primitive). Same-row transitions damage one row. Selected rows and active
  tabs keep their appearance. Repeated motion in the same target submits
  no frame. Find hover writes only the six toggle cells before row-strip
  submission (P: three labels × two cells).
- Map hover damage: map_cols × cell_w × visible_text_rows × cell_h pixels
  on enter/leave (P: the existing region's width × height). Only that region
  is dirty; no text rows or source pages are touched. The style adds one bool,
  taking sizeof(minimap_style) 16 → 20 B on the stack (M). Its wash weight is
  selected before the pixel loops, not by a per-pixel theme lookup.
- X11 retained client state: two uint32 cursor IDs + one shape enum = 12 B
  of fields (P), net x11_plat 104 → 112 B after padding, +8 B (M).
  Server ARGB payload is 4 × (arrow_width × arrow_height + text_width ×
  text_height) B plus server cursor metadata (P). Startup temporary payload
  is the current file length + 1, its 4 × width × height upload bytes, and
  the visited theme nodes (sizeof node + name length + 1 each); all are freed
  before typing. Path scratch is two PATH_MAX arrays; theme components use
  NAME_MAX + 1 (P: filesystem ABI). No cursor file I/O, allocation or reload
  occurs on hover or typing. Win64 adds two borrowed HCURSOR handles and an
  enum (P: 2 × 8 + 4 = 20 B before padding; not measured here).
- Typing damage: +0 pixels per key from Part B (P: only pointer transitions
  invoke the isolated path; ordinary frames retain their existing row/region
  damage). No keyboard animation. Theme colours stay cached. The tab scratch
  initialisation now touches only margins and slice boundaries before the
  strip fills its span, removing the extra full-row initialisation pass.
- Shots: no editor runtime storage or typing work. The script adds three
  scenes per theme (P: hover-tab, hover-find, empty-find), for 11 × 2 = 22
  captures. It derives cell coordinates from the embedded face metrics and
  DPI, waits for the base frame before pointer movement, then requires a
  changed, settled frame. The final-row target excludes the fractional
  bottom margin. The existing settle/idle rule is retained.
  The existing screenshot regression script's expected manifest count changes
  from 16 to 22 (P: old 8 × 2, new 11 × 2); this changes test work only.

### Evidence

- `/tmp/r3-hover-red.log`: new tab/toggle/palette hover assertions fail on
  Part A. `/tmp/r3-b-focused2.log`: green hover/native/theme/Wayland checks
  and modal matrix (307 PASS, one existing XFAIL, no FAIL; M: enum cross
  product and witnesses). Dirty-row assertions cover tabs, toggles,
  palette/menu rows and region-only minimap hover; repeated target motion
  preserves the submitted-frame count.
- `/tmp/r3-cursor-red.log`: decoder/loader stub rejects the generated valid
  fixture. `/tmp/r3-cursor-green.log`: size/tie/first-frame, all truncated
  prefixes through the selected image, malformed TOCs/chunks/dimensions/
  hotspots, inheritance cycle and fallback settings pass. Native
  `x11_xcursor_test` checks actual themed cursor creation, aliases, request
  sequence counts for unchanged shapes, LeaveNotify and load-failure no-op.
  These native checks were added after the parser implementation.
- `/tmp/r3-native-hover.log` and `/tmp/r3-native-find.log`: real X11 pointer
  movement reaches text/tab/find regions in both themes. The full-app shot
  exposed the unpainted search-state geometry issue described above.
  `/tmp/r3-painted-hit-red.log` then fails a targeted painted-cell hit
  assertion; `/tmp/r3-painted-hit-green.log` passes after the cache fix.
- `/tmp/r3-cursor-fuzz-seeded.log`: ASan/UBSan libFuzzer smoke run completes
  65536 executions, seeded with the generated image and X resources (M:
  libFuzzer count; E smoke budget 2^16, the uint16 nominal-size space, not a
  correctness gate or coverage claim). The first sandbox attempt hit the
  environment's ptrace/LeakSanitizer restriction; the outside-sandbox run
  exits 0. No sanitizer issue was found in cursor code.
- `/tmp/r3-b-release-guards.log`: release hover driver, null/raster typing
  (10000 keys each, the existing regression fixture) and field-editing suite
  pass with active allocation counting, mallocs=0 (M [AC], BAT0 Not charging,
  11:06:27 +01:00). Native setup allocations occur before that bracket.
- Checkpoint B attempt B1: exit 2 while linking, 11:07:42–11:08:26 +01:00,
  BAT0 Not charging → Not charging [AC] (M, `/tmp/r3-check-b.log`). The GL
  white-box fixture needed to rename the new x11_plat_set_cursor symbol with
  its other X11 entry points. `/tmp/r3-gl-adapter-green.log` then passes
  (`gl_test` native lifecycle keeps its existing Xvfb Present-MSC skip).

Tracked touched-module benches: `/tmp/r3-b-benches.log`, 11:11:45 +01:00,
BAT0 Not charging [AC], one-minute load 0.77 (M). X11 key translation
p50/p99 182/297 ns and startup subset 5.934/8.381 ms; allocation count 0.
Minimap small-file p99 56598 ns, after edit 63082 ns; sampled large-file
p99 62417 ns, after edit 68063 ns (M: existing sorted-sample fixtures).
All existing minimap tracked gates pass; foreground cold-source calls and
major faults are 0. These shared-box samples do not establish a before/after
latency improvement. New release object text is 1747 B for hover and 5817 B
for Xcursor (M [AC], GNU size .text; relocatable objects, excluding shared
dependencies and final-link pooling), measured 11:16:43 +01:00,
BAT0 Not charging.

Part B item map:

| Item | Red evidence | Green evidence | Specific cost |
|---|---|---|---|
| 8 hover/leave | tab/toggle/palette assertions fail before motion handling | precise row damage, unchanged-target frame count, native X11 and mock Wayland leave | net editor +64 B, old/new affected rows only |
| 9 tab hover | inactive-tab foreground assertion | active/inactive tone and native pointer tests | same tab row; cached colour |
| 10 panel hover | find/palette surface assertions; painted-hit regression | toggle cells, palette/menu rows, selected-row priority, exact hits | find cache +24 B used arena bytes; existing row submission |
| 11 map hover | no isolated red run; pixel/damage assertions added with implementation | stronger wash reference, map-only damage, repeated-target no frame | one existing map region upload on transition; zero text rows |
| 12 pointer | valid generated cursor rejected by stub decoder/loader | parser/unit/native tests, fuzz smoke, native text/tab/find pointer routing | startup-only cursor/file memory above; cached setter |
| 13 shots | full-app find-hover target stayed off the painted Aa cells before geometry fix | all scenes settle; new six captures repeat byte-for-byte | capture script only; zero editor runtime cost |

### Design captures

`make build/sublimite` succeeded, then
`sh tools/design_shots.sh build/sublimite <directory>` exited 0 (M [AC],
BAT0 Not charging; `/tmp/r3-final-shots.log`). Directory:

`/tmp/claude-1000/-home-tobias-Projects-sublimite/85a23efe-08d5-4b61-b7ac-6f526f14f57f/scratchpad/shots-r3`

There are 22 scene images (P: 11 scenes × dark/light), a contact sheet and
manifest. New scenes are `09-hover-tab`, `10-hover-find`, `11-find-empty`.
All scene SHA-256 values match the manifest; the current release binary
matches its recorded hash (M, Python hashlib verification):
`680dbabf1c42edebab3229d4f05be31f678a1becf2301fc135133547093c48e1`.
The six new scene files are byte-identical to the independent preceding
capture in `/tmp/r3-hover-preview-final` (M: direct file comparison).
The contact sheet was inspected, and the find-hover background was also
checked at the Aa cell against the generated dark/light hover colours.

The existing `tools/test_design_shots.sh` expected only 16 entries. Its
count predicate fails against the final manifest (`/tmp/r3-shot-contract-red.log`).
After updating that expectation to 22, the complete script exits 0: both
fresh captures contain all scenes with more than one colour, and every
scene image compares byte-for-byte identical. Start 11:21:19, finish
11:24:36 +01:00, BAT0 Not charging at both ends [AC] (M,
`/tmp/r3-shot-contract-green.log`). Its printed hashes also match the final
requested shot directory. This is the full-scene determinism check, in
addition to the new-scene preview comparison.

### Files changed

- Layout: `src/layout/{layout.c,layout.h,wrap.c}`.
- Editor: `src/editor/{buffers.c,editor.c,fif.c,findpanel.c,highlight.c,hover.c,
  menubar.c,open.c,paint.c,palettepanel.c,preferences.c,private.h,theme.h}`.
- Find: `src/findui/{findui.h,paint.c,private.h}`.
- Surfaces/rendering: `src/config/theme.c`, `src/minimap/{minimap.c,minimap.h}`,
  `src/render/{render.c,render.h}`.
- Platforms: `src/x11/{LDLIBS,plat.h,private.h,x11.c,xcursor.c,xcursor.h}`,
  `src/wayland/{dispatch.c,private.h,wayland.c}`,
  `src/win32/{private.h,win32_plat.c}`.
- Tests: `tests/{config_theme_test.c,editor_design_test.c,editor_hl_test.c,
  editor_hot_exit_test.c,editor_large_test.c,editor_p4_test.c,
  editor_scroll_test.c,editor_test.c,findui_test.c,gl_test.c,layout_test.c,
  minimap_test.c,modal_matrix_test.c,unicode_render_test.c,wayland_mock.h,
  wayland_test.c,wrap_test.c,x11_xcursor_test.c,xcursor_fixture.h,xcursor_test.c}`.
- Tooling: `tools/{design_shots.sh,mkpalette.py}`, `bench/minimap_bench.c`,
  `tools/test_design_shots.sh`, `fuzz/{minimap_fuzz.c,xcursor_fuzz.c}`.
- Records: this decision, `docs/worklog/2026-10-28.md`, `HANDOFF.md`.

### Checkpoint B

Full `make -j4 check` exits 0; 117 test binaries plus replay-CLI and
display-lock contracts pass (M: make's final count).
Start 2026-10-28 11:11:31 +01:00, BAT0 Not charging [AC]; finish 11:22:44,
BAT0 Not charging [AC]. Elapsed 673 s (M: timestamp difference,
11 × 60 + 13). Log `/tmp/r3-check-b2.log`. Application source remained
unchanged during the run. The screenshot regression script's stale expected
count was corrected while this ran; it is checked separately below.

The existing modal matrix's `edit-cxho.18` XFAIL and Xvfb native GL
Present-MSC skip remain explicitly reported by their suites; they are not
round-3 omissions. No `bd`, commit, install or publish is part of this
worker's hand-over; the coordinator owns the commit.

Requested work is complete except the explicitly permitted platform gaps:
Wayland cursor shapes (missing vendored protocol header), Win32 leave
(stub retained), and unverified Windows compilation (toolchain absent).
All other brief items are implemented and checked as recorded above.
