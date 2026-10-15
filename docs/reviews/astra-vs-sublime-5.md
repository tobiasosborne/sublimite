# Astra vs Sublime Text: comparison drive 5

Astra · edit-w34.4 / P5.1b. Captures: 2026-10-16; offline finalization continued on 2026-10-17. **Verdict: no.** The unchanged dense64 session closes normally but relaunch exits with `error -103` before a window. A copied journal reproduces the failure. The oversized-orphan launch fix and default whitespace fix work; the two M0 waivers are findings only.

## Conditions

Only DISPLAY=:98; the restricted shell cannot connect, while the authorized escalated check succeeds. Staged binary: build/astra5/sublimite. Tracedump: build/astra5/tracedump. BUILD is newer than drive 4; its SHA256 line is truncated, so the full measured digest below is authoritative. Claims in BUILD are not behavioural verification.

```text
BUILD build/astra5: commit 2c48f9c, 2026-10-16T01:55:42+11:00, release gcc make all on main.
Landed since 617fa43 (drive 4): edit-hgd 49ec4e3, edit-fm4 c98734c, edit-34l e76c9f9 (engine only, no UI), edit-v16 4fc09f2, edit-28u c62d560 (PRESENT), edit-6e2 0dd36db, edit-mmp 2c48f9c.
Not run on this build: full make check (only per-bead suites, each green with ASan/UBSan/LSan); no Astra drive yet (law 1: drive before hand-over).
sha256: 09087573ed0aebb5b537dcc12cfc58d9
make check on main 252d7ae (docs on 2c48f9c code): exit 0, 97 test binaries passed, 589 s (M)[AC] (BAT0 Full), start load1 0.30, 2026-10-16T05:15-05:25 box clock, log build/check15/make-check.log
```

Start binary / power / load (M)[AC]:

```text
2026-10-16T05:26:41+11:00
Full
1
0.57 0.68 0.73 2/2488 925887
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite
```

Find in files (edit-xkd) and colour emoji artwork (edit-r8e) are waived for M0 and will not be blockers. Timing rows with task-start load1 above 8 are provisional. Every measured value inherits the task BAT0/status, AC/online and load1 stamp, including pixel counts, readback hashes and return codes. Input counts, task IDs and analysis coordinates are prescribed parameters, not budgets. Video intervals are resampled at 60 Hz, 1000/60 ms per interval (P).

Harness copies redirect output, pinned executable, tracedump and report destinations. Tasks retain inherited keys/delays/coordinates/fixtures. Full recursive diffs against drives 4 and 3 are in the worker log. New instrumentation does not change inherited actions.


All evidence paths without a leading `build/` are relative to **build/astra5/**. Videos are in video/, stills and frame sequences in frames/, command/power/readback records in logs/, and literal trace dumps in traces/. All measured values in a row inherit its BAT0/AC/load stamp. Task IDs, key counts, prescribed dwell times, fixture dimensions and analysis rectangles are inputs/identifiers rather than performance budgets. A single observation is not a percentile. Every onset uses resampled 60 Hz intervals (P: 1000/60 ms per interval); command envelopes bound software injection, not physical key-to-photon time.

`xdpyinfo` confirms 2880×1800 pixels and 243×243 DPI (M)[AC], start stamp above. The verified Xvfb :98 owner and argv are in logs/xvfb-owner.txt; only that PID is stopped/continued in task 29. EGL is unavailable in the saved runtime launches, which use raster. The GL path is not verified. Cache was not flushed; fixture copying/hashing and shared-box work affect cache/load conditions.

Sublime build 4200 is recorded in logs/subl-help.txt. Initial User preferences specify font_size 11 and no colour override; installed Default preferences specify Mariana. Inherited zoom operations change individual views and Ctrl+0 does not reset Sublime’s font. The initial real-profile session restored prior-drive windows, some late/unpainted, and several native dialogs persisted into later captures. Invalid attempts remain in the ledger. Repeated/remaining comparisons use private XDG config/cache/data under state/sublime-clean, with copied User preferences; update_check was disabled only there after cancelling the update offer. Process environment and its session file verify isolation. The final small Unicode validation uses state/sublime-validation with the same baseline preferences. Safe-mode large-file attempts are identified separately; their startup dialogs invalidate their action windows. No private-profile timing is represented as accumulated real-session performance.

The monochrome Noto Emoji file is observed by fontconfig. logs/fonts.txt contains `fc-list | grep -i noto` followed by `fc-list ':charset=1f469'`; this environment change is distinct from a bead. The woman-technologist, family and flag render as monochrome artwork in the staged runtime. Colour artwork remains waived for M0.

The staged executable is never rebuilt. The only old UI binary use is the explicitly requested task-36 fixture producer, labelled separately; it produced no oversized journal. The permitted fallback fixture generator follows the session test’s construction and links the existing current library solely to create an orphan. That generator is not the build under UI test. UI verification always uses build/astra5/sublimite. Main/source work continued elsewhere; BUILD and source/library changes are not substituted for staged-binary behaviour.

Behaviour supports retained o20 file commands, zw9 reversal, g02 find keys, 97i identity reuse, 11v body colour, cee raster survival, 1um prompt placement, 48u bottom-edge autoscroll/panel resize, dr3 gutter selection, r1l sampled cache pages, pgr atomic recovery outcomes, rzj two-instance restoration, rfq retirement, bml CRLF and 1vo save retry. hgd’s none/all/selection and live fallback work, but a user-visible invalid-value diagnostic was not observed. fm4’s oversized-orphan startup/preservation works. 28u’s size objective is met in the sampled growth window, but the dense64 reopening workflow fails. v16 plus the installed font produces combined monochrome glyphs. The additional 34l engine, 6e2 fallback-chain ordering, mmp’s specifically typed-before-index case, 3u4/9u3 and the 848 GL path are not independently established by these probes. fi2 is not landed and changed-base recovery retains its documented limit.

No timing with task-start load1 above 8 is a regression claim or blocker by itself. Earlier and current loads differ unless an explicitly paired observation says otherwise. The clock audit found no wall/monotonic discontinuity larger than one recording interval in these captures; see logs/clock-discontinuities.json.

The oversized task-36 fixture/source and oversized copies in the two task-17 profiles were removed after their hash audits, as requested. Snapshots and cleanup manifests remain. The small failing dense-session journals are preserved. Earlier drive directories were never written by the driver; the saved fixture integrity audit reports no change. The original real sublimité profile was only read/copied, never launched against.

## 1. Launch, glyphs, syntax and zoom

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Fresh C first matching text | 29.31 intervals / 488.5 ms; command 0.778117–1.097465s → frame 1.266667s | 8.70 intervals / 145.0 ms; command 1.504970–1.608276s → frame 1.650000s | STILL PRESENT — functioning launch; edit-11v colour below | `video/01-fresh-sublimite.mkv` (painted 7.036s); (M)[AC], BAT0=Full, AC/online=1, load1=0.51; exit=0; `video/01-fresh-sublime.mkv` (painted 6.373s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; logs/startup-metrics.json, inherited matching ROIs |
| Default ink/window | 11 px; 800×600 | 26 px; 1728×1260 | STILL PRESENT — edit-1nm | `video/01-fresh-sublimite.mkv` (painted 7.036s); (M)[AC], BAT0=Full, AC/online=1, load1=0.51; exit=0; `video/01-fresh-sublime.mkv` (painted 6.373s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; logs/presentation-metrics.json; ink threshold max RGB >130; ROIs (91,60,41,20) and (140,42,95,40); geometry from xdotool |
| Sub-cell margins | Not applicable | Right/bottom RGB (32,36,44), equal to body background | FIXED — edit-u82 fix retained from drive 4 | `video/01-fresh-sublimite.mkv` (painted 7.036s); (M)[AC], BAT0=Full, AC/online=1, load1=0.51; exit=0; `video/01-fresh-sublime.mkv` (painted 6.373s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; right (1720,40,8,1200), bottom (0,1240,1728,20), background (1400,100,50,100) |
| C syntax onset | Colour visible with first text | Plain then coloured; 65.70 intervals / 1095.0 ms at 2.600000s | FIXED — edit-11v retained; startup colouring delay STILL PRESENT | `video/01-fresh-sublimite.mkv` (painted 7.036s); (M)[AC], BAT0=Full, AC/online=1, load1=0.51; exit=0; `video/01-fresh-sublime.mkv` (painted 6.373s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; colour ROI (120,40,800,120), >80 pixels with channel spread >40 and max >130 |

Drive 4 first matching text: sublimité 8.25 intervals / 137.5 ms (M)[AC], load1=0.39; Sublime 15.17 / 252.8 ms (M)[AC], load1=0.48. Drive 3: 10.18 / 169.7 ms and 29.16 / 486.0 ms (M)[AC], BAT0=Not charging, AC/online=1, load1=0.43 (sublimité) / 0.48 (Sublime). Loads differ; no regression claim. Drive 4 colour onset 66.25 / 1104.2 ms (M)[AC], load1=0.39. Zoom and font override are reported below.

### Zoom and point-size override

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+= twice, Ctrl+−, Ctrl+0 | Enlarges twice and shrinks; Ctrl+0 does not reset text size | Enlarges twice, shrinks, resets | STILL PRESENT — working override / retained shortcut difference; edit-1nm | `video/zoom-sublimite.mkv` (larger 3.306s; larger2 4.359s; smaller 5.531s; reset 6.643s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.87; exit=0; `video/zoom-sublime.mkv` (larger 3.407s; larger2 4.820s; smaller 6.032s; reset 7.323s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.36 |
| EDIT_FONT_SIZE_PT=12 | Not applicable | 31 px ink | STILL PRESENT — working override; edit-1nm | `video/font12-sublimite.mkv` (font 2.875s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.97; exit=0; max RGB >130 in ROI (180,50,120,42), rows [55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85] |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Fresh C first matching text | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-11v | `video/01-fresh-sublimite.mkv` 2.642–7.437s; (M)[AC], BAT0=Full, AC=1, load1=0.51; `video/01-fresh-sublime.mkv` 2.120–6.719s; (M)[AC], BAT0=Full, AC=1, load1=0.90; action-specific timestamps/limitations are in the measured rows above. |
| Default ink and window | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm. | `video/01-fresh-sublimite.mkv` 2.642–7.437s; (M)[AC], BAT0=Full, AC=1, load1=0.51; action-specific timestamps/limitations are in the measured rows above. |
| Sub-cell margin strips | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-u82 | `video/01-fresh-sublimite.mkv` 2.642–7.437s; (M)[AC], BAT0=Full, AC=1, load1=0.51; action-specific timestamps/limitations are in the measured rows above. |
| C syntax colour | FIXED — drive-4 fix retained in the stated runtime probe | edit-11v | `video/01-fresh-sublimite.mkv` 2.642–7.437s; (M)[AC], BAT0=Full, AC=1, load1=0.51; action-specific timestamps/limitations are in the measured rows above. |
| Zoom Ctrl+= twice, Ctrl+−, Ctrl+0 | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm. | `video/zoom-sublimite.mkv` 2.062–7.355s; (M)[AC], BAT0=Not charging, AC=1, load1=5.87; `video/zoom-sublime.mkv` 2.199–8.143s; (M)[AC], BAT0=Not charging, AC=1, load1=6.36; action-specific timestamps/limitations are in the measured rows above. |
| EDIT_FONT_SIZE_PT=12 | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm. | `video/font12-sublimite.mkv` 1.945–3.925s; (M)[AC], BAT0=Not charging, AC=1, load1=8.97 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 2. Typing and isolated glyph onset

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Isolated Z | 1.67 intervals / 27.8 ms; command 5.022215–5.028625s → frame 5.050000s | 2.85 intervals / 47.4 ms; command 4.919219–4.924621s → frame 4.966667s | STILL PRESENT — no bead; functioning typing | `video/02-typing-r2-sublimite.mkv` (probe 5.925s; slow 11.827s; burst 12.990s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; exit=0; `video/02-typing-r2-sublime.mkv` (probe 6.029s; slow 11.889s; burst 13.062s); (M)[AC], BAT0=Full, AC/online=1, load1=0.99; thirty cropped frames per editor; logs/typing-metrics.json |
| Paced paragraph / zero-delay burst | Paragraph and burst painted in order | Paragraph and burst painted in order | STILL PRESENT — no bead | `video/02-typing-r2-sublimite.mkv` (probe 5.925s; slow 11.827s; burst 12.990s); (M)[AC], BAT0=Full, AC/online=1, load1=0.90; exit=0; `video/02-typing-r2-sublime.mkv` (probe 6.029s; slow 11.889s; burst 13.062s); (M)[AC], BAT0=Full, AC/online=1, load1=0.99 |

Drive 4 Z: sublimité 3.03 intervals / 50.5 ms (M)[AC], load1=0.52; Sublime 1.98 / 33.0 ms (M)[AC], load1=0.77. Drive 3: 1.75 / 29.2 ms versus 2.01 / 33.4 ms (M)[AC], BAT0=Not charging, AC/online=1, load1=0.87 (sublimité) / 1.12 (Sublime). This drive pair ran in the same minute; loads differ from previous drives. Internal trace (counts include the complete typing/navigation sequence):

```text
G1 T4-T0     n=92 p50=1.571 ms p99=56.968 ms
G3 T5-T0     n=91 p50=1.747 ms p99=18.653 ms
T6-T0        n=91 p50=20.263 ms p99=33.851 ms
```

### Deadline-paced clipboard verification

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Paced paragraph readback | Both verification runs exactly match expected paragraph | Exactly matches expected paragraph | STILL PRESENT — passing; no bead | `video/02-paced-verified-sublimite.mkv` (selected 13.525s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.55; exit=0; `video/02-paced-verified-sublime.mkv` (selected 13.564s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.42; `video/02-paced-verified-r2-sublime.mkv` (selected 19.930s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.61; paragraph_match=true in all three JSON logs |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Isolated Z | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/02-typing-r2-sublimite.mkv` 1.914–13.885s; (M)[AC], BAT0=Full, AC=1, load1=0.90; action-specific timestamps/limitations are in the measured rows above. |
| Original paced paragraph and zero-delay burst | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/02-typing-r2-sublimite.mkv` 1.914–13.885s; (M)[AC], BAT0=Full, AC=1, load1=0.90; `video/02-typing-r2-sublime.mkv` 1.988–13.940s; (M)[AC], BAT0=Full, AC=1, load1=0.99; action-specific timestamps/limitations are in the measured rows above. |
| Deadline-paced clipboard verification | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/02-paced-verified-sublimite.mkv` 1.914–13.936s; (M)[AC], BAT0=Not charging, AC=1, load1=3.55; `video/02-paced-verified-sublime.mkv` 1.915–13.902s; (M)[AC], BAT0=Not charging, AC=1, load1=3.42; action-specific timestamps/limitations are in the measured rows above. |

## 3. Navigation and selection

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Arrows, word/line/document ends and Shift selections | Inherited sequence follows caret/selection | Inherited sequence follows caret/selection | STILL PRESENT — passing; no bead | `video/03-nav-sublimite.mkv` (20-shift+Down 19.149s); (M)[AC], BAT0=Full, AC/online=1, load1=1.15; exit=0; `video/03-nav-sublime.mkv` (20-shift+Down 17.775s); (M)[AC], BAT0=Full, AC/online=1, load1=1.25 |
| PageDown/PageUp placement | Top lines 28→55→28 | Top lines 33→34→33 | STILL PRESENT — placement divergence; no bead | `video/03-nav-sublimite.mkv` (13-Next 13.875s; 14-Next 14.607s; 15-Prior 15.388s); (M)[AC], BAT0=Full, AC/online=1, load1=1.15; exit=0; `video/03-nav-sublime.mkv` (13-Next 13.112s; 14-Next 13.763s; 15-Prior 14.414s); (M)[AC], BAT0=Full, AC/online=1, load1=1.25 |
| Held Shift | alp selected | alp selected | STILL PRESENT — passing; no bead | `video/03-selection-sublimite.mkv` (held-three 7.079s); (M)[AC], BAT0=Full, AC/online=1, load1=1.63; exit=0; `video/03-selection-sublime.mkv` (held-three 6.884s); (M)[AC], BAT0=Full, AC/online=1, load1=2.48 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Held Shift | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/03-selection-sublimite.mkv` 1.915–7.307s; (M)[AC], BAT0=Full, AC=1, load1=1.63; `video/03-selection-sublime.mkv` 1.973–7.118s; (M)[AC], BAT0=Full, AC=1, load1=2.48; action-specific timestamps/limitations are in the measured rows above. |
| Arrows, word/line/document ends and Shift selections | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/03-nav-sublimite.mkv` 1.915–19.500s; (M)[AC], BAT0=Full, AC=1, load1=1.15; `video/03-nav-sublime.mkv` 2.006–18.045s; (M)[AC], BAT0=Full, AC=1, load1=1.25; action-specific timestamps/limitations are in the measured rows above. |
| PageDown/PageUp view placement | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/03-nav-sublimite.mkv` 1.915–19.500s; (M)[AC], BAT0=Full, AC=1, load1=1.15; action-specific timestamps/limitations are in the measured rows above. |

## 4. Original mouse sequence

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Click/drag/double-click | Caret and selections respond | Caret and drag respond; original double-click coordinate targets punctuation | STILL PRESENT — no bead | `video/04-mouse-sublimite.mkv` (click 4.265s; drag 5.101s; double 6.147s); (M)[AC], BAT0=Full, AC/online=1, load1=2.36; exit=0; `video/04-mouse-sublime.mkv` (click 4.380s; drag 5.207s; double 6.213s); (M)[AC], BAT0=Full, AC/online=1, load1=2.48 |
| Wheel six down, three up | Scrolls | Top 1→19→10, as drive 4 | STILL PRESENT — passing; edit-zw9 wrapped case later | `video/04-mouse-sublimite.mkv` (launch 1.931s; wheel-down 7.568s; wheel-up 8.788s); (M)[AC], BAT0=Full, AC/online=1, load1=2.36; exit=0 |
| Minimap click/drag | Viewport moves | Viewport moves; drag top 6675, as drive 4 | STILL PRESENT — navigation; edit-1nm presentation | `video/04-mouse-sublimite.mkv` (minimap-click 9.732s; minimap-drag 10.640s); (M)[AC], BAT0=Full, AC/online=1, load1=2.36; exit=0; `video/04-mouse-sublime.mkv` (minimap-click 9.573s; minimap-drag 10.390s); (M)[AC], BAT0=Full, AC/online=1, load1=2.48 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Click / drag / double-click | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/04-mouse-sublimite.mkv` 1.931–10.997s; (M)[AC], BAT0=Full, AC=1, load1=2.36; `video/04-mouse-sublime.mkv` 2.034–10.701s; (M)[AC], BAT0=Full, AC=1, load1=2.48; action-specific timestamps/limitations are in the measured rows above. |
| Wheel down six / up three | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-zw9 | `video/04-mouse-sublimite.mkv` 1.931–10.997s; (M)[AC], BAT0=Full, AC=1, load1=2.36; action-specific timestamps/limitations are in the measured rows above. |
| Minimap click / drag | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm | `video/04-mouse-sublimite.mkv` 1.931–10.997s; (M)[AC], BAT0=Full, AC=1, load1=2.36; `video/04-mouse-sublime.mkv` 2.034–10.701s; (M)[AC], BAT0=Full, AC=1, load1=2.48; action-specific timestamps/limitations are in the measured rows above. |

## 5. Editing and undo/redo

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Inherited insert / undo / redo loop | Baseline returns after undo; digits restore after redo | Same baseline and digit restoration | STILL PRESENT — passing; no bead | `video/05-edit-sublimite.mkv` (50-inserts 16.510s; 50-undo 21.815s; 50-redo 27.344s); (M)[AC], BAT0=Full, AC/online=1, load1=2.40; exit=0; `video/05-edit-sublime.mkv` (50-inserts 16.391s; 50-undo 21.641s; 50-redo 27.126s); (M)[AC], BAT0=Full, AC/online=1, load1=2.22 |
| Select-all/delete, Ctrl+L, Ctrl+Backspace/Delete | Edits and restoration work | Same logical edits/restoration | STILL PRESENT — passing; no bead | `video/05-edit-r2-sublimite.mkv` (single-line 5.022s; line-deleted 5.661s; undo 6.322s; delete-word 8.637s); (M)[AC], BAT0=Full, AC/online=1, load1=2.82; exit=0; `video/05-edit-r2-sublime.mkv` (single-line 5.118s; line-deleted 5.730s; undo 6.356s; delete-word 8.825s); (M)[AC], BAT0=Full, AC/online=1, load1=2.76 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Inherited insert / undo / redo loop | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/05-edit-sublimite.mkv` 1.900–28.731s; (M)[AC], BAT0=Full, AC=1, load1=2.40; `video/05-edit-sublime.mkv` 2.006–28.428s; (M)[AC], BAT0=Full, AC=1, load1=2.22; action-specific timestamps/limitations are in the measured rows above. |
| Select-all/delete, Ctrl+L, Ctrl+Backspace/Delete | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/05-edit-r2-sublimite.mkv` 1.916–8.891s; (M)[AC], BAT0=Full, AC=1, load1=2.82; `video/05-edit-r2-sublime.mkv` 2.014–9.126s; (M)[AC], BAT0=Full, AC=1, load1=2.76; action-specific timestamps/limitations are in the measured rows above. |

## 6. Clipboard

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+C/X/V, Shift+Insert | COPYME then COPYMECOPYME readbacks | Same readbacks | STILL PRESENT — passing; edit-bml CRLF tested later | `video/06-clipboard-sublimite.mkv` (copy 4.648s; cut 7.164s; shift-insert 8.264s); (M)[AC], BAT0=Full, AC/online=1, load1=3.02; exit=0; `video/06-clipboard-sublime.mkv` (copy 4.687s; cut 7.256s; shift-insert 8.356s); (M)[AC], BAT0=Full, AC/online=1, load1=2.95 |
| PRIMARY middle-click | PRIMARY_PASTE inserted | PRIMARY_PASTE inserted at clicked text cell | STILL PRESENT — passing; no bead | `video/06-clipboard-sublimite.mkv` (middle 9.201s); (M)[AC], BAT0=Full, AC/online=1, load1=3.02; exit=0; `video/06-clipboard-sublime.mkv` (middle 9.294s); (M)[AC], BAT0=Full, AC/online=1, load1=2.95 |

### Alternate clipboard and round trip

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+Insert / Shift+Delete / Ctrl+V | Copy/cut/paste readbacks retained | Copy/cut/paste readbacks retained | STILL PRESENT — passing; no bead | `video/06-alternate-sublimite.mkv` (copied-pasted 4.934s; cut 6.882s; pasted 8.430s); (M)[AC], BAT0=Not charging, AC/online=1, load1=9.92; exit=0; `video/06-alternate-sublime.mkv` (copied-pasted 4.161s; cut 5.757s; pasted 6.894s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.28 |
| Sublime → sublimité → Sublime | Final paste-back contains FROM_SUBLIME twice and FROM_SUBLIMITE | Receives Sublime text and returns the augmented buffer | STILL PRESENT — passing; no bead | `video/06-cross-source-sublime.mkv` (final 42.184s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.08; `video/06-cross-sublimite.mkv` (copy-back 5.160s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.21; exit=0; `video/06-cross-back-sublime.mkv` (paste-back 1.579s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.11; literal xclip readbacks in each JSON |

Fixture audit: copied drive-4 fixture hashes match the current drive-3 copies except external.txt, which the inherited task regenerates before use. `logs/fixture-copy-hashes.json` and `logs/fixture-drive3-current-differences.json` retain the actual hashes. This compares saved copies, not a reconstruction of every historical pre-task hash. Corpus ascii/dense cmp succeeded; log_1g.txt hash matches the brief (logs/corpus-hash.txt).

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Ctrl+C/X/V, Shift+Insert | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-bml | `video/06-clipboard-sublimite.mkv` 1.877–9.525s; (M)[AC], BAT0=Full, AC=1, load1=3.02; `video/06-clipboard-sublime.mkv` 2.000–9.578s; (M)[AC], BAT0=Full, AC=1, load1=2.95; action-specific timestamps/limitations are in the measured rows above. |
| PRIMARY middle-click | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/06-clipboard-sublimite.mkv` 1.877–9.525s; (M)[AC], BAT0=Full, AC=1, load1=3.02; `video/06-clipboard-sublime.mkv` 2.000–9.578s; (M)[AC], BAT0=Full, AC=1, load1=2.95; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Insert / Shift+Insert / Shift+Delete | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-bml | `video/06-alternate-sublimite.mkv` 2.018–9.058s; (M)[AC], BAT0=Not charging, AC=1, load1=9.92 (timing provisional); `video/06-alternate-sublime.mkv` 1.906–7.284s; (M)[AC], BAT0=Not charging, AC=1, load1=5.28; action-specific timestamps/limitations are in the measured rows above. |
| Sublime → sublimité → Sublime | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/06-cross-sublimite.mkv` 1.899–5.428s; (M)[AC], BAT0=Not charging, AC=1, load1=5.21; `video/06-cross-back-sublime.mkv` 1.579–1.880s; (M)[AC], BAT0=Not charging, AC=1, load1=5.11; action-specific timestamps/limitations are in the measured rows above. |
| Full-path verified cross-editor round trip | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/06-cross-fullpath-r2-sublimite.mkv` 1.838–5.508s; (M)[AC], BAT0=Not charging, AC=1, load1=1.33; `video/06-cross-back-fullpath-r2-sublime.mkv` 1.550–1.834s; (M)[AC], BAT0=Not charging, AC=1, load1=1.43; action-specific timestamps/limitations are in the measured rows above. |

## 7. Original find sequence

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+F, alpha, Return, Escape, F3, Ctrl+H, Escape | Panels work; alpha selected | Panels work; alpha selected | STILL PRESENT — edit-g02; original single-match fixture does not distinguish first Return advance | `video/07-find-sublimite.mkv` (find-panel 4.050s; find-query 5.141s; next-match 6.625s; replace-panel 7.384s); (M)[AC], BAT0=Full, AC/online=1, load1=2.80; exit=0; `video/07-find-sublime.mkv` (find-panel 3.944s; find-query 5.133s; next-match 6.662s; replace-panel 7.474s); (M)[AC], BAT0=Full, AC/online=1, load1=2.75 |

The full-buffer original-sequence check is reported below.

### Full-buffer ownership check

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Exact inherited original find sequence | Unchanged full-buffer hash | Same unchanged full-buffer hash | STILL PRESENT — edit-x8i | `video/16-original-sequence-sublimite.mkv` (original-sequence 7.015s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.73; exit=0; `video/16-original-sequence-sublime.mkv` (original-sequence 7.429s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.96; timings PROVISIONAL (load1 > 8); 12898 bytes, SHA256 e50ddfdce3719854bda9cca11a40311ec9946634c11ec796a1c2ed1e7190022d |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Ctrl+F, alpha, Return, Escape, F3, Ctrl+H, Escape | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-g02 | `video/07-find-sublimite.mkv` 1.893–8.091s; (M)[AC], BAT0=Full, AC=1, load1=2.80; `video/07-find-sublime.mkv` 1.887–8.268s; (M)[AC], BAT0=Full, AC=1, load1=2.75; action-specific timestamps/limitations are in the measured rows above. |
| Full-buffer hash after exact inherited find sequence | CHANGED coverage where repaired below; otherwise STILL PRESENT as an explicit gap | edit-x8i. | `video/16-original-sequence-sublimite.mkv` 1.944–7.377s; (M)[AC], BAT0=Not charging, AC=1, load1=3.73; `video/16-original-sequence-sublime.mkv` 1.954–8.006s; (M)[AC], BAT0=Not charging, AC=1, load1=10.96 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 8. Save and external append

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+S and disk cat | EDITED_ saved | EDITED_ saved; title clean | STILL PRESENT — passing; no bead | `video/08-save-sublimite.mkv` (after-save 5.087s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.41; exit=0; `video/08-save-sublime.mkv` (after-save 5.004s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.25; literal cat readback in task JSON |
| External append on clean buffer | EXTERNAL_APPEND visible without prompt | EXTERNAL_APPEND visible without prompt | STILL PRESENT — passing; no bead | `video/08-save-sublimite.mkv` (external-focused 7.755s; external-refocus 10.083s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.41; exit=0; `video/08-save-sublime.mkv` (external-focused 7.245s; external-refocus 9.278s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.25 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Ctrl+S and disk cat | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/08-save-sublimite.mkv` 1.877–10.434s; (M)[AC], BAT0=Not charging, AC=1, load1=3.41; `video/08-save-sublime.mkv` 2.039–9.585s; (M)[AC], BAT0=Not charging, AC=1, load1=4.25; action-specific timestamps/limitations are in the measured rows above. |
| External append on a clean buffer | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-37a | `video/08-save-sublimite.mkv` 1.877–10.434s; (M)[AC], BAT0=Not charging, AC=1, load1=3.41; `video/08-save-sublime.mkv` 2.039–9.585s; (M)[AC], BAT0=Not charging, AC=1, load1=4.25; action-specific timestamps/limitations are in the measured rows above. |

### External append visibility

| Action | Observation | Status / bead | Evidence |
|---|---|---|---|
| 08-save-sublimite | 4.00 intervals / 66.6 ms from echo marker 5.750079s to visible text 5.816667s | STILL PRESENT — clean reload; no bead | `video/08-save-sublimite.mkv` (external-focused 7.755s; external-refocus 10.083s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.41; exit=0; logs/extra-onsets.json; ROI [40, 126, 320, 27] |
| 08-save-sublime | 39.44 intervals / 657.3 ms from echo marker 5.242650s to visible text 5.900000s | STILL PRESENT — clean reload; no bead | `video/08-save-sublime.mkv` (external-focused 7.245s; external-refocus 9.278s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.25; logs/extra-onsets.json; ROI [61, 98, 170, 14] |

The echo completion was not separately timestamped; these are marker-to-visible observations. Drive 3 runtime reference: 3.16 intervals / 52.7 ms (M)[AC], BAT0=Not charging, AC/online=1, load1=2.43 (build/astra3/logs/08-save-sublimite.json); loads differ.


## 9. Tabs and CLI burst

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| CLI opens and tab keys | Distinct files open; cycle/close/reopen exercised | Distinct files open; cycle/close/reopen exercised | STILL PRESENT — passing; edit-97i same-path reuse later | `video/09-tabs-sublimite.mkv` (five-opened 5.834s; hundred-reopen 41.935s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.39; exit=0; `video/09-tabs-sublime.mkv` (five-opened 5.958s; hundred-reopen 44.216s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.32 |
| CLI return codes / inotify | All 105 CLI calls return zero | All 105 CLI calls return zero; one inotify descriptor | STILL PRESENT — passing; no bead | `video/09-tabs-sublimite.mkv` (hundred 40.192s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.39; exit=0; `video/09-tabs-sublime.mkv` (hundred 42.381s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.32; command pairs and /proc fd-link count in JSON |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| CLI opens and tab keys | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-97i | `video/09-tabs-sublimite.mkv` 1.916–42.225s; (M)[AC], BAT0=Not charging, AC=1, load1=3.39; action-specific timestamps/limitations are in the measured rows above. |
| CLI burst and inotify | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/09-tabs-sublimite.mkv` 1.916–42.225s; (M)[AC], BAT0=Not charging, AC=1, load1=3.39; action-specific timestamps/limitations are in the measured rows above. |

## 10. Large file and dense short lines

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+End / PageUp sequence | Original action window stays blank/loading; ready navigation NOT OBSERVED | EOF 8,947,842; after PageUps top 8,947,542 | STILL PRESENT — passing sublimité settled navigation; inherited Sublime comparison gap; edit-91c / edit-pd6 / edit-jf9 | `video/10-large-sublimite.mkv` (end-before-edit 52.389s; 20-pageup 55.870s; end-after-wait 83.798s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; `video/10-large-sublime.mkv` (end-before-edit 52.945s; 20-pageup 56.740s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.90; timings PROVISIONAL (load1 > 8) |
| Y near end / Z at start | Neither glyph established in original blank/loading action window | Y at EOF and Z at start visible; normal exit | STILL PRESENT — passing sublimité edits; loaded Sublime timing reported below | `video/10-large-sublimite.mkv` (end-edit 60.984s; first-edit 65.808s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; `video/10-large-sublime.mkv` (end-edit 61.781s; first-edit 66.557s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.90; timings PROVISIONAL (load1 > 8) |

The separate loaded-session comparison below supersedes the blank attempt; blank frames are excluded from latency claims. Exact EOF reference is the brief’s 8,947,841 newline count plus one final logical line (arithmetic). Internal settled-run trace, with the capture stamp above:

```text
G1 T4-T0     n=27 p50=5.120 ms p99=56.763 ms
G3 T5-T0     n=26 p50=7.237 ms p99=15.226 ms
T6-T0        n=26 p50=26.303 ms p99=37.169 ms
```

### Short-dwell and dense-file results

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Short-dwell repeat | Original standard-session large comparison remained loading | Reaches EOF, pages, accepts Y/Z and closes normally | STILL PRESENT — passing; edit-91c / edit-pd6 / edit-jf9 | `video/10-large-early-sublimite.mkv` (end-before-edit 11.789s; 20-pageup 14.690s; end-edit 19.330s; first-edit 23.701s); (M)[AC], BAT0=Not charging, AC/online=1, load1=14.71; timings PROVISIONAL (load1 > 8); exit=0 |
| Dense short lines | Reaches line 26,845,451 and paints Z at line 1 | Reaches line 26,845,451 and paints Z at line 1 | STILL PRESENT — passing; no bead | `video/dense64-sublimite.mkv` (end 49.205s; typed 51.081s); (M)[AC], BAT0=Not charging, AC/online=1, load1=9.37; timings PROVISIONAL (load1 > 8); exit=0; `video/dense64-sublime.mkv` (end 49.840s; typed 51.953s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.47 |

### Recorded large-file onsets

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Y | Not measured in blank standard-session action window | 2.71 intervals / 45.2 ms; command 58.971478–58.979491s → frame 59.016666666666666 | STILL PRESENT — functioning workflow; no matched regression inference; edit-91c / edit-jf9 | `video/10-large-sublimite.mkv` (final 84.588s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; logs/large-metrics-sublimite.json; ROI [160, 46, 20, 27] |
| Z | Not measured in blank standard-session action window | 3.43 intervals / 57.1 ms; command 63.792857–63.799832s → frame 63.85 | STILL PRESENT — functioning workflow; no matched regression inference; edit-91c / edit-jf9 | `video/10-large-sublimite.mkv` (final 84.588s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; logs/large-metrics-sublimite.json; ROI [160, 46, 20, 27] |
| end-navigation | Not measured in blank standard-session action window | 4.12 intervals / 68.6 ms; command 47.348027–47.388707s → frame 47.416666666666664 | STILL PRESENT — functioning workflow; no matched regression inference; edit-91c / edit-jf9 | `video/10-large-sublimite.mkv` (final 84.588s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; logs/large-metrics-sublimite.json; ROI [0, 46, 140, 27] |
| startup | Not measured in blank standard-session action window | 8.33 intervals / 138.9 ms; command 0.761103–0.883118s → frame 0.9 | STILL PRESENT — functioning workflow; no matched regression inference; edit-91c / edit-jf9 | `video/10-large-sublimite.mkv` (final 84.588s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; logs/large-metrics-sublimite.json; ROI [180, 46, 100, 27] |

Drive 4: Y 3.30 intervals / 55.0 ms; Z 2.56 / 42.7 ms; settled End 3.30 / 55.1 ms; startup 9.29 / 154.8 ms, all (M)[AC], load1=3.29. Drive 3: Y 2.27 / 37.9 ms; Z 1.78 / 29.7 ms; settled End 16.04 / 267.4 ms, (M)[AC], load1=15.26. Loads differ; no controlled speed ranking is claimed.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Y | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 | `video/10-large-sublimite.mkv` 1.905–84.588s; (M)[AC], BAT0=Not charging, AC=1, load1=5.03; action-specific timestamps/limitations are in the measured rows above. |
| Z | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 | `video/10-large-sublimite.mkv` 1.905–84.588s; (M)[AC], BAT0=Not charging, AC=1, load1=5.03; action-specific timestamps/limitations are in the measured rows above. |
| end-navigation | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 | `video/10-large-sublimite.mkv` 1.905–84.588s; (M)[AC], BAT0=Not charging, AC=1, load1=5.03; action-specific timestamps/limitations are in the measured rows above. |
| startup | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 | `video/10-large-sublimite.mkv` 1.905–84.588s; (M)[AC], BAT0=Not charging, AC=1, load1=5.03; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+End / PageUp sequence | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 / edit-jf9 | `video/10-large-sublimite.mkv` 1.905–84.588s; (M)[AC], BAT0=Not charging, AC=1, load1=5.03; action-specific timestamps/limitations are in the measured rows above. |
| Short-dwell large run | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 / edit-jf9 | `video/10-large-early-sublimite.mkv` 1.998–41.469s; (M)[AC], BAT0=Not charging, AC=1, load1=14.71 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Dense short lines | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/dense64-sublimite.mkv` 1.889–51.646s; (M)[AC], BAT0=Not charging, AC=1, load1=9.37 (timing provisional); `video/dense64-sublime.mkv` 2.096–52.613s; (M)[AC], BAT0=Not charging, AC=1, load1=6.47; action-specific timestamps/limitations are in the measured rows above. |

### Per-PageUp gutter samples

Samples are named video frames, not predictions from a page-size formula. Runtime OCR is consistent with the inspected gutter images. Sublime cells below preserve raw first-visible-label OCR; smooth scrolling and wrapped continuation rows mean these are not asserted to be settled logical top lines.

| Prior input | Runtime settled run: time / label | Runtime short-dwell: time / label | Loaded Sublime: time / raw labels |
|---|---|---|---|
| 1 | 53.198266s / 8947827 | 12.224275s / 8947827 | 6.782495s / 8947829, 8947830, 8947831 |
| 2 | 53.336137s / 8947812 | 12.346353s / 8947812 | 6.912487s / 8947809, 8947810, 8947811 |
| 3 | 53.464070s / 8947797 | 12.473148s / 8947797 | 7.039762s / 8947800, 8947801 |
| 4 | 53.585173s / 8947782 | 12.596601s / 8947782 | 7.164102s / 8947782, 8947783, 8947784 |
| 5 | 53.710221s / 8947767 | 12.723261s / 8947767 | 7.290784s / 8947774, 8947776 |
| 6 | 53.863741s / 8947752 | 12.847020s / 8947752 | 7.415079s / 8947757, 8947758, 8947759 |
| 7 | 54.003851s / 8947737 | 12.973037s / 8947737 | 7.541970s / 8947748 |
| 8 | 54.152976s / 8947722 | 13.096511s / 8947722 | 7.668769s / 8947729, 8947730, 8947731 |
| 9 | 54.295100s / 8947707 | 13.226215s / 8947707 | 7.796488s / 8947724, 8947725, 8947726 |
| 10 | 54.434434s / 8947692 | 13.356959s / 8947692 | 7.923996s / 8947705, 8947706 |
| 11 | 54.590645s / 8947677 | 13.482182s / 8947677 | 8.048033s / 8947699, 8947700, 8947701 |
| 12 | 54.735224s / 8947662 | 13.613767s / 8947662 | 8.179106s / 8947679, 8947680, 8947681 |
| 13 | 54.865575s / 8947647 | 13.747908s / 8947647 | 8.306215s / 8947670, 8947671 |
| 14 | 55.010509s / 8947632 | 13.880769s / 8947632 | 8.430655s / 8947653, 8947654, 8947655 |
| 15 | 55.139859s / 8947617 | 14.013844s / 8947617 | 8.558415s / 8947644 |
| 16 | 55.284550s / 8947602 | 14.147150s / 8947602 | 8.689456s / 8947627, 8947628, 8947629 |
| 17 | 55.416088s / 8947587 | 14.280705s / 8947587 | 8.814250s / 8947622, 8947623, 8947624 |
| 18 | 55.552269s / 8947572 | 14.415064s / 8947572 | 8.941610s / 8947604, 8947605 |
| 19 | 55.703611s / 8947557 | 14.545771s / 8947557 | 9.075323s / 8947594, 8947595 |
| 20 | 55.850119s / 8947542 | 14.673222s / 8947542 | 9.206131s / 8947577, 8947578 |

`video/10-large-sublimite.mkv` (20-pageup 55.870s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; exit=0; `video/10-large-early-sublimite.mkv` (20-pageup 14.690s); (M)[AC], BAT0=Not charging, AC/online=1, load1=14.71; timings PROVISIONAL (load1 > 8); exit=0; `video/10-large-loaded-sublime.mkv` (20-pageup 9.223s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.78. Source crops and command envelopes: logs/large-gutters.tsv and .json.


### Loaded comparator result

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ready loaded Y/Z | Both glyphs visible; Undo restores the clean corpus buffer | Both visible in settled and short-dwell runs | STILL PRESENT — edit-91c / edit-pd6 | `video/10-large-loaded-sublime.mkv` (loaded-ready 1.113s; end-edit 13.576s; first-edit 17.987s; clean-before-find 19.129s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.78; glyph intervals in the measurement inventory |

The attached ready view initially sat near EOF; its first End is not used as a BOF-to-EOF benchmark. The optional paired run explicitly begins at Home and supplies that navigation measurement.

## 11. Resize

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Inherited size sequence and page keys | Resizes/redraws; shared C buffer still contains the prior paced paragraph | Resizes/redraws; no persistent garbage visible at sampled sizes | STILL PRESENT — passing; edit-48u panel variant later | `video/11-resize-sublimite.mkv` (1440x900 5.207s; 2880x1800 7.585s; 800x600 10.506s; 960x600 13.162s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.36; timings PROVISIONAL (load1 > 8); exit=0; `video/11-resize-sublime.mkv` (1440x900 5.101s; 2880x1800 7.596s; 800x600 10.366s; 960x600 12.718s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.50; timings PROVISIONAL (load1 > 8) |

Sublime reuses the earlier dirty ascii_code.c view, so this is a resize comparison, not a byte-identical text comparison. The inherited sequence is preserved; minimap motion is audited separately.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Inherited size sequence with PageDown/Up | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-48u | `video/11-resize-sublimite.mkv` 1.893–14.654s; (M)[AC], BAT0=Not charging, AC=1, load1=10.36 (timing provisional); `video/11-resize-sublime.mkv` 1.938–14.097s; (M)[AC], BAT0=Not charging, AC=1, load1=11.50 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 12. Hot exit

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| SIGKILL with unchanged base | Relaunch capture is black; text recovery NOT OBSERVED | UNSAVED_RECOVERY_MARKER_ and dirty title restore; disk remains baseline | CHANGED — Sublime comparison incomplete; sublimité recovery STILL PRESENT passing; edit-pgr / edit-rzj later | `video/12-hotexit-sublimite.mkv` (before-kill 4.869s; killed 18.633s; relaunch 25.687s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.75; timings PROVISIONAL (load1 > 8); exit=0; `video/12-hotexit-sublime.mkv` (before-kill 5.504s; killed 19.367s; relaunch 25.920s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.73; timings PROVISIONAL (load1 > 8) |
| SIGKILL with replaced base | Matched case deferred to task 17 | Disk replacement opens clean with set-aside notice | STILL PRESENT — edit-fi2 absent behaviour | `video/12-recovery-conflict-sublimite.mkv` (relaunch 8.139s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.47; timings PROVISIONAL (load1 > 8); exit=0 |

The inherited crash helper logs SIGKILL but does not reap/record the killed process before replacing its handle. The relaunch normal exit is recorded; the killed process return code is unavailable in these original captures. Future wrapper launches additionally record the prior child’s actual return code; no inferred value is substituted here.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| SIGKILL with unchanged base | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr / edit-rzj | `video/12-hotexit-sublimite.mkv` 1.964–26.696s; (M)[AC], BAT0=Not charging, AC=1, load1=10.75 (timing provisional); `video/12-hotexit-sublime.mkv` 2.317–26.293s; (M)[AC], BAT0=Not charging, AC=1, load1=12.73 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| SIGKILL with replaced base | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-fi2 | `video/12-recovery-conflict-sublimite.mkv` 1.789–8.546s; (M)[AC], BAT0=Not charging, AC=1, load1=12.47 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 13. Minimap and flash audit

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Minimap appearance and edits | Original capture stays blank/loading; fresh task-1 C capture supplies a coloured miniature | Monochrome minimap beside coloured body; navigation/edit/undo captured | STILL PRESENT — edit-1nm; edit-11v excludes minimap; original Sublime comparison gap | `video/13-minimap-sublimite.mkv` (Next 4.711s; Next 6.048s; ctrl+End 7.445s; ctrl+Home 10.145s; edited 12.660s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.47; timings PROVISIONAL (load1 > 8); exit=0; `video/13-minimap-sublime.mkv` (edited 25.014s); (M)[AC], BAT0=Not charging, AC/online=1, load1=10.32; timings PROVISIONAL (load1 > 8); task-1 painted frame |

Task 14 comparator validity: the Sublime launch/end root stills show stale sublimité pixels while its own window has not painted. Any detector counts from that comparator are excluded; a ready fresh-window repeat is required. This is a capture validity failure, not Sublime caret behaviour.

### Clean minimap comparison

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Ready C-file miniature | Coloured miniature and edited source visible | Monochrome stripes beside coloured source | STILL PRESENT — edit-1nm; edit-11v excludes minimap | `video/13-minimap-fullpath-r2-sublime.mkv` (edited 10.528s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.43; `video/13-minimap-sublimite.mkv` (edited 12.660s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.47; timings PROVISIONAL (load1 > 8); exit=0 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Minimap appearance and edits | CHANGED coverage where repaired below; otherwise STILL PRESENT as an explicit gap | edit-1nm / edit-11v | `video/13-minimap-sublimite.mkv` 1.857–13.146s; (M)[AC], BAT0=Not charging, AC=1, load1=11.47 (timing provisional); `video/13-minimap-fullpath-r2-sublime.mkv` 1.860–10.834s; (M)[AC], BAT0=Not charging, AC=1, load1=1.43; action-specific timestamps/limitations are in the measured rows above. |
| Three-colour flash scan | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm. | `video/13-minimap-sublimite.mkv` 1.857–13.146s; (M)[AC], BAT0=Not charging, AC=1, load1=11.47 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Other orange UI | FIXED — drive-4 fix retained in the stated runtime probe | edit-1um | `video/13-minimap-sublimite.mkv` 1.857–13.146s; (M)[AC], BAT0=Not charging, AC=1, load1=11.47 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Corrected minimap comparison | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm / edit-11v | `video/13-minimap-fullpath-r2-sublime.mkv` 1.860–10.834s; (M)[AC], BAT0=Not charging, AC=1, load1=1.43; `video/13-minimap-sublimite.mkv` 1.857–13.146s; (M)[AC], BAT0=Not charging, AC=1, load1=11.47 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

### Complete flash-scan inventory

117 runtime-named recordings × 3 prescribed colour scans = 351 detector runs (counted from logs/flash-summary.json; each row inherits the capture stamp). Fixed ROI is (1568,40,160,1220), with bf853d/tol40, ff8800/tol60 and 6b5030/tol24. The old-binary fixture-producer recording is included for completeness, not as staged-build evidence. No old minimap pulse is detected in these fixed-ROI scans.

| Recording | bf853d / ff8800 / 6b5030 events | Capture stamp |
|---|---|---|
| 01-fresh-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=0.51 |
| 02-paced-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.86 |
| 02-paced-verified-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.55 |
| 02-paired-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.66 |
| 02-typing-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=0.90 |
| 03-nav-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=1.15 |
| 03-selection-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=1.63 |
| 04-mouse-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=2.36 |
| 05-edit-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=2.82 |
| 05-edit-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=2.40 |
| 06-alternate-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=9.92 — timing PROVISIONAL |
| 06-clipboard-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=3.02 |
| 06-cross-fullpath-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33 |
| 06-cross-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.21 |
| 07-find-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Full, AC/online=1, load1=2.80 |
| 08-save-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.41 |
| 09-tabs-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.39 |
| 10-large-early-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=14.71 — timing PROVISIONAL |
| 10-large-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03 |
| 10-paired-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04 — timing PROVISIONAL |
| 11-resize-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.36 — timing PROVISIONAL |
| 12-hotexit-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.75 — timing PROVISIONAL |
| 12-recovery-conflict-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.47 — timing PROVISIONAL |
| 13-minimap-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.47 — timing PROVISIONAL |
| 14-idle-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.72 — timing PROVISIONAL |
| 15-autoscroll-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.19 |
| 15-autoscroll-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.66 |
| 15-mouse-calibrated-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.29 — timing PROVISIONAL |
| 15-mouse-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.03 |
| 15-mouse-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.39 — timing PROVISIONAL |
| 15-mouse-wrapped-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.88 |
| 15-mouse-wrapped-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=16.75 — timing PROVISIONAL |
| 16-find-large-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.14 |
| 16-find-large-wait-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.79 |
| 16-find-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70 |
| 16-original-sequence-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.73 |
| 17-profile-audit-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.34 |
| 17-recovery-a-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.20 |
| 17-recovery-b-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.80 |
| 17-recovery-c-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.22 |
| 17-recovery-d-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30 |
| 17-recovery-e-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30 |
| 18-tabtitle-second-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.34 |
| 18-tabtitle-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.53 |
| 19-external-controlled-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21 — timing PROVISIONAL |
| 19-external-repro-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.88 |
| 19-external-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01 |
| 20-emoji-carets-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.84 |
| 20-emoji-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.85 |
| 21-font-file-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.82 |
| 21-font-iosevka-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98 |
| 21-font-liberation-mono-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.07 |
| 21-font-missing-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.89 |
| 21-font-proportional-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.14 |
| 22-common-commands-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91 |
| 23-wrapped-keys-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.39 |
| 23-wrapped-keys-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.13 |
| 23-wrapped-reverse-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95 |
| 23-wrapped-reverse-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.99 |
| 24-files-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16 |
| 24-files-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| 24-scroll-caret-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.78 |
| 24-scroll-caret-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.38 |
| 25-findkeys-continuous-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.65 |
| 25-findkeys-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.20 |
| 25-findkeys-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.05 |
| 26-reopen-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.79 |
| 26-reopen-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.72 |
| 27-end-page-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15 |
| 27-light-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.98 |
| 27-light-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.54 |
| 27-syntax-c-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84 |
| 27-syntax-c-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98 |
| 27-syntax-rs-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL |
| 27-syntax-rs-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.19 |
| 27-tokens-c-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| 27-tokens-c-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.47 |
| 27-tokens-rs-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| 27-tokens-rs-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.27 |
| 28-gutter-cells-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.85 |
| 28-gutter-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.36 |
| 28-gutter-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.34 |
| 29-stall-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.53 |
| 30-dirty-branch-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.13 |
| 30-external-x3-1-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.67 |
| 30-external-x3-2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.16 |
| 30-external-x3-3-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.54 |
| 31-early-end-only-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15 |
| 31-large-index-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| 31-large-queued-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.20 |
| 32-crash-two-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.51 |
| 32-crash-undo-0-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.18 |
| 32-crash-undo-1-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.72 |
| 32-crash-undo-2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.36 |
| 32-crash-undo-4-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.19 |
| 32-crash-undo-8-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.29 |
| 32-crash-untitled-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.86 |
| 33-crlf-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.79 |
| 33-glyphs-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.84 |
| 33-save-fail-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.56 |
| 34-emoji-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25 |
| 34-emoji-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.85 |
| 34-fixture-r2-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.16 |
| 35-whitespace-all-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.57 |
| 35-whitespace-default-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.77 |
| 35-whitespace-lines-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.71 |
| 35-whitespace-selection-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48 |
| 36-orphan-fixture-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.71 |
| 36-orphan-repeat-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.30 |
| 36-orphan-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.24 |
| 37-checkpoint-growth-dense64-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.36 |
| 37-checkpoint-growth-log1g-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49 |
| 37-dense-restart-check-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49 |
| dense64-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=9.37 — timing PROVISIONAL |
| font12-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.97 — timing PROVISIONAL |
| utf8-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=14.08 — timing PROVISIONAL |
| zoom-sublimite.mkv | 0 / 0 / 0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.87 |

The moving resize tracker detects the same one-frame event in two colours: bf853d 788 pixels and 6b5030 269 pixels at 9.500–9.517 s; ff8800 has none. The immediately preceding action is the 800×600 resize command at 9.496701 s. The tracker moves its crop to x=640 before the old wide frame disappears, so it samples deliberate C syntax colours in the body rather than the minimap. The exact resampled frames 570/571 are frames/resize-event-01.png and -02.png; the first shows the old wide C view, the next clears for resize. This is a crop-tracking false positive, not the old pulse. (M)[AC], BAT0=Not charging, AC/online=1, load1=10.36 — timing PROVISIONAL. logs/11-resize-tracked.flash-*.txt and resize-tracking.json retain the detector/action evidence.

Whitespace-body scans separately cover (40,40,400,40), and full-video theme-colour counts supplement the prescribed orange signatures; see logs/35-whitespace-body.flash-*.txt and whitespace-full-video-colors.json. No marker event is detected in the default typing recording.

## 14. Idle

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Focused untouched interval | NOT OBSERVED: stale sublimité background pixels; detector counts excluded | 20 caret transitions; 0 outside changes; 4.623879–64.626383s | STILL PRESENT — expected quiet sublimité behaviour; no bead; comparator coverage gap | `video/14-idle-sublimite.mkv` (idle-end 64.627s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.72; timings PROVISIONAL (load1 > 8); exit=0; `video/14-idle-sublime.mkv` (idle-end 64.308s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.05; timings PROVISIONAL (load1 > 8); logs/idle-sublimite-metrics.json; caret ROI (75,35,45,50), gray delta >30 and >20 changed pixels |

Prescribed idle duration is an input parameter. The expected transition count follows the documented idle/blink constants: 10 / 0.5 = 20 (arithmetic). Drive 4 observed 20 caret / 0 outside events (M)[AC], load1=4.76. No timing regression is inferred from this higher-load capture.

### Clean comparator idle result

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Focused untouched interval | Detector reports 0 caret / 1 outside changes, 3.641529–63.642220s; outside change is in the status strip | Runtime result above: expected caret transitions then stable body | STILL PRESENT — quiet body; no bead | `video/14-idle-fullpath-r2-sublime.mkv` (idle-end 63.643s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.86; caret ROI (45,55,50,35), gray delta>30 and >20 pixels; logs/idle-fullpath-r2-sublime-metrics.json |

This detector counts frame-to-frame threshold crossings; zero detected crossings is not a claim about every subthreshold smooth-caret fade.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| sublimite focused idle | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/14-idle-sublimite.mkv` 2.189–65.737s; (M)[AC], BAT0=Not charging, AC=1, load1=8.72 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Sublime focused idle, corrected window | CHANGED coverage where repaired below; otherwise STILL PRESENT as an explicit gap | no bead | `video/14-idle-fullpath-r2-sublime.mkv` 1.940–64.064s; (M)[AC], BAT0=Not charging, AC=1, load1=1.86; action-specific timestamps/limitations are in the measured rows above. |

## 15. Mouse targets and wrapped/unwrapped wheel

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| C wheel bursts | Captured; comparator readiness still to review | Top lines 1→10→100 in initial down sequences; navigation continues | STILL PRESENT — passing initial unwrapped movement; edit-zw9 wrapped case separate | `video/15-mouse-sublimite.mkv` (wheel-before 12.967s; down3 14.861s; down30 19.623s; up30 24.838s; up3 27.142s; burst30 29.236s); (M)[AC], BAT0=Not charging, AC/online=1, load1=15.39; timings PROVISIONAL (load1 > 8); exit=0 |
| Wrapped three-notch movement | Earlier comparator not relied on | Exactly nine visual rows; zero-error registration at 9×40=360 px | FIXED — edit-zw9 initial movement retained | `video/15-mouse-wrapped-sublimite.mkv` (wheel-before 4.829s; down3 7.156s); (M)[AC], BAT0=Not charging, AC/online=1, load1=16.75; timings PROVISIONAL (load1 > 8); exit=0; logs/wrapped-original-registration.json; ROI (120,40,1400,1160) |
| Wrapped down30 / up30 reversal | Earlier comparator not relied on | Does not restore down3 view: 335,046 changed pixels; image registers exactly six visual rows farther down | CHANGED — invalidated comparison: delayed Sublime overlay; edit-zw9 repeat required | `video/15-mouse-wrapped-sublimite.mkv` (down3 7.156s; down30 12.190s; up30 17.268s); (M)[AC], BAT0=Not charging, AC/online=1, load1=16.75; timings PROVISIONAL (load1 > 8); exit=0; RGB delta >0, ROI (120,40,1400,1160); logs/wrapped-original-metrics.json and wrapped-original-registration.json |

Drive 4 recorded zero changed pixels after this same runtime sequence (M)[AC], load1=2.17. The apparent mismatch is invalidated by delayed comparator-window interference. The final focus check returns window 4282021 (Sublime navigation.txt), while the owned runtime window is 6291457; the left 800×600 pixels remain byte-identical from up30 through later runtime updates outside that rectangle. Wheel commands at (400,300) can therefore hit the overlaid, unpainted Sublime window. The independent single-notch sequence will establish runtime reversal; no product regression is claimed from this capture. IDs/rectangle and timestamps derive from the cited task JSON/stills; logs/wrapped-focus-interference.json retains the pixel audit.

### Isolated mouse comparison and repaired wrapped coverage

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Calibrated drag / Shift+click / underscore / triple click | Final isolated repeat matches runtime clipboard strings, including fn_0 and the complete line | Intended spans and complete line copied | STILL PRESENT — passing; no bead | `video/15-mouse-calibrated-sublimite.mkv` (drag 5.090s; shift-extend 6.322s; underscore-word 7.790s; triple-line 9.816s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.29; timings PROVISIONAL (load1 > 8); exit=0; `video/15-mouse-target-ready-r3-sublime.mkv` (drag 13.418s; shift-extend 15.390s; underscore-word 17.442s; triple-line 19.713s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.06 |
| Past EOL / gutter | Clamps past EOL; gutter selects whole line in final isolated repeat | Same clamp/whole-line behaviour | FIXED — edit-dr3; no bead for EOL clamp | `video/15-mouse-target-ready-r3-sublime.mkv` (past-eol 21.662s; gutter 23.332s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.06; `video/15-mouse-r2-sublimite.mkv` (past-eol 8.397s; gutter 8.831s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.03; exit=0 |
| Bottom-edge drag hold | Autoscroll and extended selection observed | Autoscroll and extended selection observed | FIXED — edit-48u retained | `video/15-autoscroll-r2-sublimite.mkv` (before 2.575s; after 4.933s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.19; exit=0; `video/15-autoscroll-sublime.mkv` (before 3.045s; after 5.733s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.02; timings PROVISIONAL (load1 > 8); clipboard selection lengths in JSON |
| Wrapped wheel/reversal | Clean comparator captures retained; registration in appendix | Isolated repeat restores exact prior body; no runtime regression | FIXED — edit-zw9 retained | `video/15-mouse-wrapped-r2-sublimite.mkv` (down3 4.881s; up30 13.529s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.88; exit=0; task 23 isolated registration |

The first ready-Sublime attempt returned empty clipboard strings. A further identical-grid repeat after native chooser cleanup matches all four text-selection readbacks; it supersedes that incomplete comparison. No older fixture was edited.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| C-file wheel bursts | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-zw9 | `video/15-mouse-sublimite.mkv` 1.941–35.435s; (M)[AC], BAT0=Not charging, AC=1, load1=15.39 (timing provisional); `video/15-mouse-fullpath-r2-sublime.mkv` 1.795–28.030s; (M)[AC], BAT0=Not charging, AC=1, load1=2.83; action-specific timestamps/limitations are in the measured rows above. |
| Wrapped PRD wheel distance and reversal | FIXED — drive-4 fix retained in the stated runtime probe | edit-zw9. | `video/15-mouse-wrapped-r2-sublimite.mkv` 1.827–20.141s; (M)[AC], BAT0=Not charging, AC=1, load1=3.88; action-specific timestamps/limitations are in the measured rows above. |
| Gutter click | FIXED — drive-4 fix retained in the stated runtime probe | edit-dr3 | `video/15-mouse-sublimite.mkv` 1.941–35.435s; (M)[AC], BAT0=Not charging, AC=1, load1=15.39 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Calibrated text drag / Shift+click / underscore / triple-click | CHANGED coverage where repaired below; otherwise STILL PRESENT as an explicit gap | no bead | `video/15-mouse-calibrated-sublimite.mkv` 1.928–31.173s; (M)[AC], BAT0=Not charging, AC=1, load1=8.29 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Past-EOL and gutter | FIXED — drive-4 fix retained in the stated runtime probe | edit-dr3 | `video/15-mouse-calibrated-sublimite.mkv` 1.928–31.173s; (M)[AC], BAT0=Not charging, AC=1, load1=8.29 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Wheel and click with find open | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-48u | `video/15-mouse-sublimite.mkv` 1.941–35.435s; (M)[AC], BAT0=Not charging, AC=1, load1=15.39 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Readiness-calibrated selection comparison | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/15-mouse-target-ready-r3-sublime.mkv` 1.907–24.935s; (M)[AC], BAT0=Not charging, AC=1, load1=6.06; `video/15-mouse-calibrated-sublimite.mkv` 1.928–31.173s; (M)[AC], BAT0=Not charging, AC=1, load1=8.29 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Hold drag below bottom text edge | FIXED — drive-4 fix retained in the stated runtime probe | edit-48u | `video/15-autoscroll-sublimite.mkv` 1.896–6.400s; (M)[AC], BAT0=Not charging, AC=1, load1=4.66; `video/15-autoscroll-sublime.mkv` 1.965–6.783s; (M)[AC], BAT0=Not charging, AC=1, load1=12.02 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 16. Find/replace and large search

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Fixture integrity after query/navigation | Full-buffer hash unchanged | Full-buffer hash unchanged | STILL PRESENT — passing; edit-x8i / edit-g02 | `video/16-find-sublimite.mkv` (unchanged 9.470s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (unchanged 9.566s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02; both SHA256 2e2fe191debe0b060f6733537ed08fc21db19bd75ceec351bb42795eac852e1d |
| First Return after typing | 1:1 → 1:12 | 1:1 → 1:12 | FIXED — edit-g02 retained | `video/16-find-sublimite.mkv` (typed 4.606s; nav-0 5.241s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (typed 4.648s; nav-0 5.372s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| Replace one / extra Ctrl+Shift+H | One replacement: 1 OMEGA / 29 alpha | Ctrl+Enter: 1/29; extra Ctrl+Shift+H: 2/28 | FIXED — edit-g02 retained | `video/16-find-sublimite.mkv` (one 29.661s; habit 32.320s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (one 29.180s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02; literal readback hashes/counts |
| Replace all / single undo | 30/0; undo returns 1/29 | 30/0; undo returns 2/28, preserving the additional prior replacement | FIXED — edit-g02 retained; correct differing pre-all state | `video/16-find-sublimite.mkv` (all 35.021s; undo-all 36.921s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (all 31.868s; undo-all 33.704s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02; runtime undo hash c25a0fd86cbbcc7553d836ab0d7d6259e90a2f3a821d79f04d5968e5bcfd7ab2 |
| Field editing / Alt+C/W/R | Panel and query changes captured | Document not mutated; totals 30 initially, 20 case, 10 whole-word with case, 30 reset/regex | STILL PRESENT — passing; edit-x8i | `video/16-find-sublimite.mkv` (field-0 12.206s; field-6 16.625s; case 18.563s; whole 19.709s; regex 22.841s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (field-0 12.222s; field-6 16.368s; case 18.273s; whole 19.345s; regex 22.449s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| Ctrl+Shift+F | Find in Files panel available | No project-search panel appears in sampled state | STILL PRESENT — edit-xkd; waived for M0, finding only | `video/16-find-sublimite.mkv` (find-in-files 37.577s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (find-in-files 34.475s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| Small-window panel / growth | Panel displayed at requested size | Panel remains intact after shrink/grow | FIXED — edit-48u retained | `video/16-find-sublimite.mkv` (small-panel 39.694s; restored-panel 41.049s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70; exit=0; `video/16-find-sublime.mkv` (small-panel 36.567s; restored-panel 37.934s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |

The uninterrupted replacement comparison is repeated in task 25. The original readback changes document selection between panel operations; it is not used to infer continuous current-match ordering. Large-search coverage is recorded separately.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Fixture integrity after query/navigation | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-x8i / edit-g02 | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| First Return after typing | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02. | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Enter, then Ctrl+Shift+H | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02 | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Replace all and single undo | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02. | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Field editing and Alt+C/W/R | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-x8i. | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Find in files | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-xkd | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Small-window panel then growth | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-48u. | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Large-file rare search / F3 / running query input | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/16-find-sublimite.mkv` 1.916–41.741s; (M)[AC], BAT0=Not charging, AC=1, load1=7.70; action-specific timestamps/limitations are in the measured rows above. |
| Corrected full-path fixture and replacement counts | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02 | `video/16-find-fullpath-r2-sublime.mkv` 1.946–47.581s; (M)[AC], BAT0=Not charging, AC=1, load1=5.23; action-specific timestamps/limitations are in the measured rows above. |

## 17. Recovery matrix

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| (a) changed base | Post-kill client relaunch exits 1: Failed to receive exit code from application; recovery text NOT OBSERVED | Opens clean disk replacement with set-aside notice; editing clears notice | STILL PRESENT — runtime changed-base limit, edit-fi2; CHANGED comparator coverage | `video/17-recovery-a-sublimite.mkv` (notice 20.667s; notice-after-edit 21.440s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.20; exit=0; `video/17-recovery-a-sublime.mkv` (before-kill 4.083s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.82; logs/17-recovery-a-sublime.editor.log and launch_failed event |
| (b) same-bytes rewrite | Not required | Disk baseline opens with set-aside notice | STILL PRESENT — edit-fi2 | `video/17-recovery-b-sublimite.mkv` (notice 20.594s; notice-after-edit 21.390s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.80; exit=0 |
| (c) normal unsaved close / repeated relaunch | Not required | UNSAVED_ORDINARY_ and dirty title restore on each relaunch | STILL PRESENT — passing; edit-rfq | `video/17-recovery-c-sublimite.mkv` (relaunch-0 5.464s; relaunch-1 7.802s; relaunch-2 10.146s; relaunch-3 12.518s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.22; exit=0 |
| (d) copied drive-2 conflict state | Not applicable | Disk replacement opens with set-aside notice; no save to old fixture | STILL PRESENT — edit-fi2 | `video/17-recovery-d-sublimite.mkv` (restored 2.098s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30; exit=0 |
| (e) fresh real-profile copy | Not applicable | Both launches map and close normally; untitled/test/save tabs restore; sampled notice is changed-base set-aside | CHANGED — edit-fm4 / edit-rfq / edit-rzj; artifact/hash audit reported below | `video/17-recovery-e-sublimite.mkv` (restored 7.877s; with-file 10.215s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30; exit=0; traces/17-recovery-e-sublimite.txt second-launch first_present=101.434001 ms; directory listings in JSON |

Space was checked before the fresh copy; it was not skipped (df/du in logs/sequence5-tail.txt). Drive 4’s second copied-profile launch trace was 68,503.171250 ms (M)[AC], load1=4.05. Current second-launch trace is quoted above; profile contents and load differ, so this is not a controlled performance comparison. The first-launch trace was overwritten by the inherited helper’s second launch; video retains both launches. Large deferred sessions are not automatically presented/restored: this is fm4’s documented limit, not a defect attributed to this build. Task 36 isolates an actual oversized orphan and the required deferred notice.

### Clean comparator and profile artifact audit

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Changed-base comparator repeat | Opens replaced on disk; valid ready frame | Opens replacement with set-aside notice | STILL PRESENT — edit-fi2 runtime limit | `video/17-recovery-a-fullpath-r2-sublime.mkv` (notice 23.962s); (M)[AC], BAT0=Not charging, AC/online=1, load1=9.04; timings PROVISIONAL (load1 > 8); `video/17-recovery-a-sublimite.mkv` (notice 20.667s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.20; exit=0 |
| Fresh cp -a profile audit | Not applicable | Small sessions retired/renamed; all six pre-existing artifacts above the startup bound stay name/size/mtime/hash identical | CHANGED — edit-fm4 preservation; edit-rfq / edit-rzj observed coverage | `video/17-profile-audit-sublimite.mkv` (restored 10.649s; with-file 13.098s; first-tabs 13.879s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.34; exit=0; logs/17-profile-before-hashes.json, 17-profile-after-hashes.json, profile-retirement-diff.json |

The artifact inventory changes from 57 to 52 entries (M, capture stamp above). Removed names: .session-8EdCZ1.lock, .session-92LPXQ.lock, .session-lmoqAw.lock, session-00gznk, session-3vYlMe, session-8EdCZ1, session-92LPXQ, session-B741KR, session-H6iulR, session-lmoqAw, session-rgU8bc. Added names: .session-LN29Ur.lock, session-3vYlMe.set-aside-1792092257438334025, session-B741KR.set-aside-1792092257534399081, session-H6iulR.set-aside-1792092257480880724, session-LN29Ur, session-rgU8bc.set-aside-1792092257468205219. Large unchanged names: .sublimite-journal-ba472-1c15c6dfb139-0, .sublimite-journal-bad68-1c248e7587d4-0, .sublimite-journal-fbc07-33d1f7f0e3bb-0, session-1aAWBt.set-aside-1791832535893165299, session-6pu6de.set-aside-1791832507193686771, session-BawHhJ.set-aside-1791872521144529829. Tab-strip frames show untitled buffers, test, save.txt and navigation-related tabs; ellipsized titles prevent an exhaustive full-path tab enumeration. Task 32 directly verifies restoration of both named crashed/closed session buffers. No claim that every retained set-aside/base file was evicted or that the retirement capacity limit was reached.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| (a) changed base | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-fi2 | `video/17-recovery-a-sublimite.mkv` 1.982–21.792s; (M)[AC], BAT0=Not charging, AC=1, load1=4.20; `video/17-recovery-a-fullpath-r2-sublime.mkv` 1.961–26.036s; (M)[AC], BAT0=Not charging, AC=1, load1=9.04 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| (b) same-bytes rewrite | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-fi2 | `video/17-recovery-b-sublimite.mkv` 1.857–21.666s; (M)[AC], BAT0=Not charging, AC=1, load1=1.80; action-specific timestamps/limitations are in the measured rows above. |
| (c) normal unsaved close and repeated relaunch | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-rfq | `video/17-recovery-c-sublimite.mkv` 1.808–12.850s; (M)[AC], BAT0=Not charging, AC=1, load1=2.22; action-specific timestamps/limitations are in the measured rows above. |
| (d) copied drive-2 conflict state | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-fi2 | `video/17-recovery-d-sublimite.mkv` 1.844–2.342s; (M)[AC], BAT0=Not charging, AC=1, load1=2.30; action-specific timestamps/limitations are in the measured rows above. |
| (e) fresh ordinary-profile copy | CHANGED — prompt copied-profile launch; oversized artifacts retained | edit-rfq / edit-rzj. | `video/17-recovery-e-sublimite.mkv` 7.611–10.547s; (M)[AC], BAT0=Not charging, AC=1, load1=2.30; `video/17-profile-audit-sublimite.mkv` 10.374–20.658s; (M)[AC], BAT0=Not charging, AC=1, load1=2.34; action-specific timestamps/limitations are in the measured rows above. |

### Copied-profile first frames and per-launch inventory

| Action | Observation | Status / bead | Evidence |
|---|---|---|---|
| Copied profile launch 1 | mapped-window discovery 172.891 ms; first painted title 11.76 intervals / 196.0 ms, at 6.616667s after launch marker 6.420672s | CHANGED — edit-fm4 / edit-rfq / edit-rzj | `video/17-recovery-e-sublimite.mkv` (restored 7.877s; with-file 10.215s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30; exit=0; logs/profile-onsets.json; title ROI [0, 0, 1000, 40] and background gate [1722, 100, 5, 5] |
| Copied profile launch 2 | mapped-window discovery 112.782 ms; first painted title 8.78 intervals / 146.3 ms, at 8.883333s after launch marker 8.737067s | CHANGED — edit-fm4 / edit-rfq / edit-rzj | `video/17-recovery-e-sublimite.mkv` (restored 7.877s; with-file 10.215s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30; exit=0; logs/profile-onsets.json; title ROI [0, 0, 1000, 40] and background gate [1722, 100, 5, 5] |

Window discovery is the first successful poll, not an exact MapNotify timestamp.

| Profile point | session-* names (includes set-asides) | Set-aside names | Retained base names |
|---|---|---|---|
| fresh-copy-before-first-launch | 26 | 18 | 0 |
| after-first-launch | 23 | 22 | 0 |
| after-first-close | 23 | 22 | 0 |
| after-second-launch | 23 | 22 | 0 |
| after-second-close | 23 | 22 | 0 |

`video/17-profile-audit-sublimite.mkv` (restored 10.649s; with-file 13.098s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.34; exit=0. Exact added/removed names per transition are in logs/profile-per-launch-diff.json. These inventories precede the requested oversized-copy cleanup.


## 18. Unicode titles and same-path reopen

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Unicode title / reopen / switch | Unicode title paints; CLI can activate a separate existing window | Unicode prefix paints; only Unicode and navigation tabs remain after reopen | FIXED — edit-97i retained; title paint STILL PRESENT | `video/18-tabtitle-sublimite.mkv` (unicode 2.083s; second-tab 4.333s; away 4.979s; back 5.740s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.53; exit=0; `video/18-tabtitle-sublime.mkv` (unicode 8.984s; second-tab 11.400s; away 12.165s; back 12.877s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48 |
| Unicode second tab beside unique ASCII fixture | Two tabs; title paints | Two tabs; long Unicode title ellipsized and survives switching | STILL PRESENT — passing; edit-97i | `video/18-tabtitle-second-sublimite.mkv` (second 3.144s; away 3.771s; back 4.564s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.34; exit=0; `video/18-tabtitle-second-sublime.mkv` (second 3.991s; away 5.061s; back 5.993s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.47 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Unicode title and reopen | FIXED — drive-4 fix retained in the stated runtime probe | edit-97i | `video/18-tabtitle-sublimite.mkv` 1.844–5.981s; (M)[AC], BAT0=Not charging, AC=1, load1=2.53; `video/18-tabtitle-sublime.mkv` 8.639–13.250s; (M)[AC], BAT0=Not charging, AC=1, load1=2.48; action-specific timestamps/limitations are in the measured rows above. |
| Unicode as second tab beside unique ASCII fixture | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-97i | `video/18-tabtitle-second-sublimite.mkv` 1.894–5.083s; (M)[AC], BAT0=Not charging, AC=1, load1=3.34; `video/18-tabtitle-second-sublime.mkv` 2.308–6.588s; (M)[AC], BAT0=Not charging, AC=1, load1=3.47; action-specific timestamps/limitations are in the measured rows above. |

## 19. External changes

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Clean append / Ctrl+Z after reload | Original comparison not ready; blank readback and clipboard timeout excluded | Silent reload; Undo retains external append (history reset) | STILL PRESENT — external-reload undo policy, no bead; comparator gap | `video/19-external-sublimite.mkv` (clean-reload 6.515s; undo-after-reload 8.520s; after-reload-undo 10.339s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01; exit=0; logs/19-external-sublime.json; incomplete capture |
| Dirty R/K / inactive tab | Original sequential comparison incomplete | R reloads, K retains KEEP_ text, inactive clean tab includes INACTIVE_APPEND | STILL PRESENT — passing; no bead | `video/19-external-sublimite.mkv` (dirty-prompt-R 16.800s; reloaded-R 17.659s; dirty-prompt-K 21.023s; kept 23.807s; inactive 30.945s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01; exit=0; literal readbacks |
| Controlled shrink / dirty reload / keep | Earlier comparison incomplete; clean comparison below | Reloaded and kept bytes match drive-3/4 hashes | STILL PRESENT — passing; edit-cee / edit-1um | `video/19-external-controlled-sublimite.mkv` (prompt-R 9.732s; reloaded 12.701s; prompt-K 16.133s; kept 18.775s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21; timings PROVISIONAL (load1 > 8); exit=0; reloaded SHA256 d880abcb5e564b731c4e1d2bf7c0869e4555d7b22cc7ca87dc0c2f68b0588219; kept 0074d511e872a0b722d07e4ad1d7130ef05f461b576c0731bb20fa702df402f0 |
| Prompt placement | Native modal comparison later | Bottom status row; line 1 remains visible | FIXED — edit-1um retained | `video/19-external-controlled-sublimite.mkv` (prompt-R 9.732s; prompt-K 16.133s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21; timings PROVISIONAL (load1 > 8); exit=0; bottom cell row y=1200–1239 from still |
| Normal ending / stderr | Comparator incomplete | Both runtime runs close zero; no error -11 / BadWindow | FIXED — edit-cee retained in these cases | `video/19-external-sublimite.mkv` (final 31.405s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01; exit=0; `video/19-external-controlled-sublimite.mkv` (final 19.295s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21; timings PROVISIONAL (load1 > 8); exit=0 |

### Controlled comparator result

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Inherited clicks for reload / keep | Reload dialog was not yet visible at prompt-R; status still says Reloading. Readbacks retain DIRTY_ and then KEEP_DIRTY_, without external append | Runtime readbacks contain the expected external reload then retained keep text | CHANGED — incomplete semantic comparator; runtime behaviour retained; edit-cee / edit-1um | `video/19-external-controlled-sublime.mkv` (prompt-R 99.516s; reloaded 102.000s; kept 107.843s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.60; timings PROVISIONAL (load1 > 8); `video/19-external-controlled-sublimite.mkv` (prompt-R 9.732s; reloaded 12.701s; kept 18.775s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21; timings PROVISIONAL (load1 > 8); exit=0 |

The fixed inherited click coordinates were replayed exactly. Since the expected Sublime dialog was absent, those clicks do not establish reload/keep parity; task 30 repeats the same controlled sequence.

### Clean external-change comparator

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Clean append / Undo | Append visible; Undo returns X-prefixed pre-append text and marks dirty | Undo retains the reloaded append | STILL PRESENT — external-reload undo policy, no bead | `video/19-external-r2-sublime.mkv` (clean-reload 6.731s; undo-after-reload 8.893s; after-reload-undo 10.806s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.67; timings PROVISIONAL (load1 > 8); `video/19-external-sublimite.mkv` (after-reload-undo 10.339s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01; exit=0 |
| Inactive clean tab | INACTIVE_APPEND appears on return | INACTIVE_APPEND appears on return | STILL PRESENT — passing; no bead | `video/19-external-r2-sublime.mkv` (inactive-return 30.828s; inactive 32.611s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.67; timings PROVISIONAL (load1 > 8); `video/19-external-sublimite.mkv` (inactive-return 29.329s; inactive 30.945s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01; exit=0 |

Subsequent sequential conflict steps start from different undo states, so the controlled task-30 readbacks supply the direct R/K comparison.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Clean append and Ctrl+Z after reload | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/19-external-sublimite.mkv` 2.160–31.405s; (M)[AC], BAT0=Not charging, AC=1, load1=4.01; action-specific timestamps/limitations are in the measured rows above. |
| Dirty R/K and inactive tab | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1um | `video/19-external-sublimite.mkv` 2.160–31.405s; (M)[AC], BAT0=Not charging, AC=1, load1=4.01; action-specific timestamps/limitations are in the measured rows above. |
| Controlled shrink → dirty/R → dirty/K | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-cee / edit-1um | `video/19-external-controlled-sublimite.mkv` 1.959–19.295s; (M)[AC], BAT0=Not charging, AC=1, load1=12.21 (timing provisional); `video/30-external-x3-1-sublime.mkv` 1.979–29.286s; (M)[AC], BAT0=Not charging, AC=1, load1=12.35 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Renderer error near ending | FIXED — drive-4 fix retained in the stated runtime probe | edit-cee | `video/19-external-repro-sublimite.mkv` 1.947–16.200s; (M)[AC], BAT0=Not charging, AC=1, load1=6.88; action-specific timestamps/limitations are in the measured rows above. |
| Original sequential Sublime repeat | CHANGED coverage where repaired below; otherwise STILL PRESENT as an explicit gap | no bead | `video/19-external-r2-sublime.mkv` 2.009–33.187s; (M)[AC], BAT0=Not charging, AC=1, load1=11.67 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 20. Original emoji fixture and UTF-8

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Default and zoomed fixture | Colour artwork for standard clusters; extra smile+ZWJ+laptop shows separate artwork | Monochrome woman-technologist, smile, thumbs-up and heart; extra smile+ZWJ+laptop shows smile only | CHANGED — edit-r8e waived; environment Noto Emoji plus BUILD edit-v16 | `video/20-emoji-sublimite.mkv` (default 2.341s; zoom2 3.528s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.85; exit=0; `video/20-emoji-sublime.mkv` (default 2.348s; zoom2 3.430s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.05 |
| Undo/save byte integrity | Original fixture hash retained | Original fixture hash retained | STILL PRESENT — passing; no bead | `video/20-emoji-sublimite.mkv` (final 33.745s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.85; exit=0; `video/20-emoji-sublime.mkv` (final 33.620s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.05; fixture/saved_hash both c47c6c8d60f8b1f6ca84114e49b6163e8eb955686d2678b14b4e56850b011ee0 |
| A-to-B cell spacing | Variable fallback advances; final byte/caret validation is below | B three cells after A: x=40 to x=100 at pitch20; (100−40)/20=3 | STILL PRESENT — grid policy; no bead | `video/20-emoji-sublimite.mkv` (default 2.341s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.85; exit=0; native default still row1 cells |
| Original mixed UTF-8 fixture | Original inherited capture retained; ready comparison repeat needed | Common scripts and inserted é中文😀 render; monochrome emoji artwork | CHANGED — artwork improvement; edit-r8e; ordinary script support retained | `video/utf8-sublimite.mkv` (unicode 4.498s; typed-unicode 9.896s); (M)[AC], BAT0=Not charging, AC/online=1, load1=14.08; timings PROVISIONAL (load1 > 8); exit=0; `video/utf8-sublime.mkv` (unicode 4.360s; typed-unicode 8.471s); (M)[AC], BAT0=Not charging, AC/online=1, load1=15.65; timings PROVISIONAL (load1 > 8) |

No missing-glyph box is visible for the woman-technologist in this sample. Monochrome glyphs satisfy the M0 waiver; full colour remains a missing feature, never a blocker. Expanded task 34 separately tests family/flag and variation/modifier sequences.

### Caret/deletion repeat

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Right movement / Backspace / Undo | Caret readback attempt returns empty clipboard for every line; comparison NOT OBSERVED | Backspace readbacks are AB on every tested line; Undo restores the complete original fixture | STILL PRESENT — whole-cluster deletion policy, no bead; CHANGED comparator coverage | `video/20-emoji-carets-sublimite.mkv` (l1-r3 4.362s; l1-backspace 8.097s; l5-deleted 44.538s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.84; exit=0; `video/20-emoji-carets-sublime.mkv` (l1-deleted 71.010s; l5-deleted 108.394s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.70; per-line text files in logs |

No last-code-point deletion result is inferred from this invalid Sublime readback. The expanded fresh task-34 probe retains zoomed text focus and provides separate coverage; the inherited task-20 keys were unchanged.

### Original fixture revalidation

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| A–emoji–B Backspace / Undo / save | Deletes the last code point: A👩‍B, A😀‍B, A👍B, AB, A❤B | Deletes the entire cluster: AB on every line | STILL PRESENT — deletion policy, no bead; artwork CHANGED | `video/34-fixture-r2-sublimite.mkv` (line-1-deleted 8.712s; line-5-deleted 35.816s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.16; exit=0; `video/34-fixture-r2-sublime.mkv` (line-1-deleted 10.172s; line-5-deleted 37.915s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.04; per-line sentinel-guarded readbacks; both final SHA256 c47c6c8d60f8b1f6ca84114e49b6163e8eb955686d2678b14b4e56850b011ee0 |

Runtime A/B cell positions remain x=40/100 with a 20-pixel ASCII advance, hence three cells on each line. Exact current Sublime cell ratios are not claimed from the occluded caret frames; its fallback artwork has variable advances. The original visible default/zoom stills and final byte readbacks are retained separately.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| ZWJ and modifier artwork | CHANGED — monochrome artwork now available; colour remains waived | edit-r8e | `video/20-emoji-sublimite.mkv` 1.934–33.745s; (M)[AC], BAT0=Not charging, AC=1, load1=5.85; `video/20-emoji-sublime.mkv` 2.071–33.620s; (M)[AC], BAT0=Not charging, AC=1, load1=5.05; action-specific timestamps/limitations are in the measured rows above. |
| Bytes after Backspace / undo / save | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | no bead | `video/20-emoji-sublimite.mkv` 1.934–33.745s; (M)[AC], BAT0=Not charging, AC=1, load1=5.85; `video/20-emoji-sublime.mkv` 2.071–33.620s; (M)[AC], BAT0=Not charging, AC=1, load1=5.05; action-specific timestamps/limitations are in the measured rows above. |
| Original accents / Greek / CJK / smile fixture | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-r8e | `video/utf8-sublimite.mkv` 1.985–11.175s; (M)[AC], BAT0=Not charging, AC=1, load1=14.08 (timing provisional); `video/utf8-fullpath-r2-sublime.mkv` 1.947–7.401s; (M)[AC], BAT0=Not charging, AC=1, load1=2.73; action-specific timestamps/limitations are in the measured rows above. |
| Caret steps and Backspace | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-r8e | `video/20-emoji-carets-sublimite.mkv` 1.984–45.781s; (M)[AC], BAT0=Not charging, AC=1, load1=4.84; `video/34-fixture-r2-sublime.mkv` 3.006–39.764s; (M)[AC], BAT0=Not charging, AC=1, load1=2.04; action-specific timestamps/limitations are in the measured rows above. |

## 21. Font selection

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Iosevka SemiBold / Liberation Mono | Not applicable to runtime font overrides | Requested faces visibly distinct; typed caret follows ABC_ | STILL PRESENT — edit-5ih | `video/21-font-iosevka-sublimite.mkv` (face 2.296s; typed 3.415s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98; exit=0; `video/21-font-liberation-mono-sublimite.mkv` (face 2.322s; typed 3.275s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.07; exit=0 |
| Proportional / missing family | Not applicable | Both fall back to embedded monospaced appearance | STILL PRESENT — edit-5ih | `video/21-font-proportional-sublimite.mkv` (face 2.325s; typed 3.456s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.14; exit=0; `video/21-font-missing-sublimite.mkv` (face 2.424s; typed 3.599s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.89; exit=0 |
| Explicit Liberation Mono file | Not applicable | Explicit file produces Liberation Mono appearance | STILL PRESENT — edit-5ih | `video/21-font-file-sublimite.mkv` (face 2.324s; typed 3.507s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.82; exit=0 |

BUILD additionally lists edit-6e2. These inherited family/file overrides do not independently verify the new configurable multi-font fallback-chain ordering.

### Font adoption intervals

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| iosevka | Not applicable | first text 0.900000s → requested/fallback face 0.983333s; 5 intervals / 83.3 ms | STILL PRESENT — font override; edit-5ih | `video/21-font-iosevka-sublimite.mkv` (face 2.296s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98; exit=0; logs/font-gated-metrics.json; ink ROI (70,40,280,36), window gate (1722,100,5,5) |
| liberation-mono | Not applicable | first text 0.916667s → requested/fallback face 0.950000s; 2 intervals / 33.3 ms | STILL PRESENT — font override; edit-5ih | `video/21-font-liberation-mono-sublimite.mkv` (face 2.322s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.07; exit=0; logs/font-gated-metrics.json; ink ROI (70,40,280,36), window gate (1722,100,5,5) |
| proportional | Not applicable | first text 0.883333s → requested/fallback face 0.883333s; 0 intervals / 0.0 ms | STILL PRESENT — font override; edit-5ih | `video/21-font-proportional-sublimite.mkv` (face 2.325s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.14; exit=0; logs/font-gated-metrics.json; ink ROI (70,40,280,36), window gate (1722,100,5,5) |
| missing | Not applicable | first text 0.916667s → requested/fallback face 0.916667s; 0 intervals / 0.0 ms | STILL PRESENT — font override; edit-5ih | `video/21-font-missing-sublimite.mkv` (face 2.424s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.89; exit=0; logs/font-gated-metrics.json; ink ROI (70,40,280,36), window gate (1722,100,5,5) |
| file | Not applicable | first text 0.866667s → requested/fallback face 0.883333s; 1 intervals / 16.7 ms | STILL PRESENT — font override; edit-5ih | `video/21-font-file-sublimite.mkv` (face 2.324s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.82; exit=0; logs/font-gated-metrics.json; ink ROI (70,40,280,36), window gate (1722,100,5,5) |

Drive 4 swap intervals: Iosevka 4 (load1=3.46), Liberation Mono 2 (3.42), proportional 0 (3.47), missing 0 (3.27), explicit file 1 (3.17), all (M)[AC]. Loads differ; these single observations are not a regression ranking.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Iosevka SemiBold / Liberation Mono | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-5ih | `video/21-font-iosevka-sublimite.mkv` 1.932–3.824s; (M)[AC], BAT0=Not charging, AC=1, load1=6.98; `video/21-font-liberation-mono-sublimite.mkv` 1.940–3.624s; (M)[AC], BAT0=Not charging, AC=1, load1=7.07; action-specific timestamps/limitations are in the measured rows above. |
| Proportional and missing family fallback | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-5ih. | `video/21-font-proportional-sublimite.mkv` 1.929–3.870s; (M)[AC], BAT0=Not charging, AC=1, load1=7.14; `video/21-font-missing-sublimite.mkv` 1.955–3.914s; (M)[AC], BAT0=Not charging, AC=1, load1=6.89; action-specific timestamps/limitations are in the measured rows above. |
| Explicit Liberation Mono file | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-5ih. | `video/21-font-file-sublimite.mkv` 1.915–3.900s; (M)[AC], BAT0=Not charging, AC=1, load1=6.82; action-specific timestamps/limitations are in the measured rows above. |

## 22. Common commands

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| new | Standard fixture window did not paint; supplemental attached sequence recorded, no immediate UI result certified | New untitled tab | FIXED — edit-o20 retained in runtime; comparator repeat required | `video/22-common-commands-sublimite.mkv` (new 10.835s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91; exit=0; logs/22-common-commands-sublime.json and 22-common-attached-sublime.json |
| open | Standard fixture window did not paint; supplemental attached sequence recorded, no immediate UI result certified | Inline Open file prompt; Escape cancels | FIXED — edit-o20 retained in runtime; comparator repeat required | `video/22-common-commands-sublimite.mkv` (open 12.209s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91; exit=0; logs/22-common-commands-sublime.json and 22-common-attached-sublime.json |
| save-as | Standard fixture window did not paint; supplemental attached sequence recorded, no immediate UI result certified | Inline Save as prompt; Escape cancels | FIXED — edit-o20 retained in runtime; comparator repeat required | `video/22-common-commands-sublimite.mkv` (save-as 13.642s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91; exit=0; logs/22-common-commands-sublime.json and 22-common-attached-sublime.json |
| quit | Standard fixture window did not paint; supplemental attached sequence recorded, no immediate UI result certified | Owned process exits zero; no runtime windows remain | FIXED — edit-o20 retained in runtime; comparator repeat required | `video/22-common-commands-sublimite.mkv` (quit 15.095s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91; exit=0; logs/22-common-commands-sublime.json and 22-common-attached-sublime.json |

The old Sublime process eventually disappeared after the supplemental normal Quit request; the host check preceding guard startup found it absent. No exact shutdown latency is inferred from those separated checks. Full file workflows are task 24.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Ctrl+N then Escape | FIXED — drive-4 fix retained in the stated runtime probe | edit-o20. | `video/22-common-commands-sublimite.mkv` 1.915–15.504s; (M)[AC], BAT0=Not charging, AC=1, load1=6.91; `video/22-common-commands-clean-sublime.mkv` 1.864–14.917s; (M)[AC], BAT0=Not charging, AC=1, load1=0.95; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+O then Escape | FIXED — drive-4 fix retained in the stated runtime probe | edit-o20. | `video/22-common-commands-sublimite.mkv` 1.915–15.504s; (M)[AC], BAT0=Not charging, AC=1, load1=6.91; `video/22-common-commands-clean-sublime.mkv` 1.864–14.917s; (M)[AC], BAT0=Not charging, AC=1, load1=0.95; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Shift+S then Escape | FIXED — drive-4 fix retained in the stated runtime probe | edit-o20. | `video/22-common-commands-sublimite.mkv` 1.915–15.504s; (M)[AC], BAT0=Not charging, AC=1, load1=6.91; `video/22-common-commands-clean-sublime.mkv` 1.864–14.917s; (M)[AC], BAT0=Not charging, AC=1, load1=0.95; action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Q | FIXED — drive-4 fix retained in the stated runtime probe | edit-o20 | `video/22-common-commands-sublimite.mkv` 1.915–15.504s; (M)[AC], BAT0=Not charging, AC=1, load1=6.91; `video/22-common-commands-clean-sublime.mkv` 1.864–14.917s; (M)[AC], BAT0=Not charging, AC=1, load1=0.95; action-specific timestamps/limitations are in the measured rows above. |

## 23. Wrapped reversal

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Three down notches | See clean comparison below | Exactly nine visual rows, with zero-error registration at 9×40=360 px | FIXED — edit-zw9 retained | `video/23-wrapped-reverse-r2-sublimite.mkv` (base-down3 3.917s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95; exit=0; logs/wrapped-reversal-analysis-r2.json |
| Thirty individual up notches / reversed body | See clean comparison below | All thirty move exactly three visual rows; reversed body has zero changed pixels | FIXED — edit-zw9 retained; earlier overlay-contaminated attempts excluded | `video/23-wrapped-reverse-r2-sublimite.mkv` (down30 8.613s; up-01 9.493s; up-30 33.183s; reversed 33.577s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95; exit=0; RGB delta >0, body ROI (120,40,1400,1160) |
| Wrapped Ctrl+Down / Ctrl+Up | See clean comparison below | One visual row per press; view restored, caret retained | STILL PRESENT — edit-o20 | `video/23-wrapped-keys-r2-sublimite.mkv` (before 2.572s; down-3 4.707s; up-3 6.905s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.39; exit=0 |

The repeated capture was made after the old Sublime process had disappeared. It resolves the apparent reversal failure in task 15 and the first task-23 capture; neither is classified as a product regression. Drive 4 also recorded zero runtime reversal differences and three visual rows per up-notch (M)[AC], load1=1.87.

### Clean comparator reversal

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Three down / thirty single up / reverse | Nine visual rows initially; all thirty up-notches register three rows; body returns with zero changed pixels | Same in isolated runtime repeat | FIXED — edit-zw9 retained; CHANGED improved Sublime coverage | `video/23-wrapped-reverse-sublime.mkv` (base-down3 4.336s; down30 9.034s; up-30 38.819s; reversed 39.541s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.06; timings PROVISIONAL (load1 > 8); `video/23-wrapped-reverse-r2-sublimite.mkv` (reversed 33.577s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95; exit=0; Sublime ROI (88,60,620,500), pitch18, delta>0; logs/wrapped-reversal-analysis.json (Sublime) and wrapped-reversal-analysis-r2.json (runtime) |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Three down notches | FIXED — drive-4 fix retained in the stated runtime probe | edit-zw9. | `video/23-wrapped-reverse-r2-sublimite.mkv` 1.834–33.916s; (M)[AC], BAT0=Not charging, AC=1, load1=3.95; action-specific timestamps/limitations are in the measured rows above. |
| Thirty individual up notches and reversal | FIXED — drive-4 fix retained in the stated runtime probe | edit-zw9. | `video/23-wrapped-reverse-r2-sublimite.mkv` 1.834–33.916s; (M)[AC], BAT0=Not charging, AC=1, load1=3.95; action-specific timestamps/limitations are in the measured rows above. |
| Wrapped Ctrl+Down / Ctrl+Up | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20. | `video/23-wrapped-keys-sublimite.mkv` 2.004–9.450s; (M)[AC], BAT0=Not charging, AC=1, load1=4.13; `video/23-wrapped-keys-sublime.mkv` 1.795–9.605s; (M)[AC], BAT0=Not charging, AC=1, load1=12.60 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

### First-notch movement and settle intervals

| Action | Observation | Status / bead | Evidence |
|---|---|---|---|
| 23-wrapped-reverse-r2-sublimite | first movement 3.31 intervals / 55.2 ms; settled 15.31 / 255.2 ms; command 2.611515–2.916546s | FIXED reversal retained — edit-zw9; new timing sample | `video/23-wrapped-reverse-r2-sublimite.mkv` (base-down3 3.917s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95; exit=0; logs/extra-onsets.json; ROI [120, 42, 1000, 78] |
| 23-wrapped-reverse-sublime | first movement 4.32 intervals / 72.1 ms; settled 23.32 / 388.7 ms; command 3.011252–3.335527s | FIXED reversal retained — edit-zw9; new timing sample | `video/23-wrapped-reverse-sublime.mkv` (base-down3 4.336s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.06; timings PROVISIONAL (load1 > 8); logs/extra-onsets.json; ROI [88, 61, 620, 38] |

Settling includes the prescribed spacing between the three notches; it is not an isolated per-notch latency budget.


## 24. Daily file workflows

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| New untitled / Open existing | See completed clean comparator below | NEW_BUFFER_TEXT accepted; opening files-b focuses beta file | FIXED — edit-o20 retained | `video/24-files-r2-sublimite.mkv` (new 2.521s; new-dirty 3.290s; open-b-prompt 3.861s; open-b-result 5.752s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0 |
| Missing path / cancel | See completed clean comparator below | Absent path rejected; Escape cancels prompt; no file created | STILL PRESENT — edit-o20 | `video/24-files-r2-sublimite.mkv` (open-missing-result 8.107s; cancelled 10.256s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0; disk_state_at_step |
| Save As untitled / existing copy / X save | See completed clean comparator below | Correct text saved; original stays unchanged; X/save updates only the copy; title rebinds | FIXED — edit-o20 retained | `video/24-files-r2-sublimite.mkv` (save-untitled-result 15.293s; save-copy-prompt 17.352s; save-copy-result 19.767s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0; saved-as b068880e964e2b3ac68cd821f21439f5fee145ab51d9ad9f26460e0b837d02db; original debd29241d0ff114ef5f5914063fa74abb97a77573e2e1c320eac1691f26445a; X-copy 6ba1ddf493a94b8c08a15e6c6a560ca356ad8192925e35269f4cf668a8a152ef |
| Save As absent directory | See completed clean comparator below | Failure notice; no file created | STILL PRESENT — edit-o20 | `video/24-files-r2-sublimite.mkv` (save-missing-result 23.972s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0; disk audit |
| Escape priority after failed Save As | See completed clean comparator below | Open prompt closes; selection clears on later Escape, but the lingering save-as-failed notice hides the Find row throughout | CHANGED assessment — edit-o20; pre-existing notice-priority defect, not a new regression | `video/24-files-r2-sublimite.mkv` (find 27.525s; open-over-find 28.441s; escape-1 29.345s; escape-2 30.286s; escape-3 31.178s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0; save-notice-baseline-compare.png also shows this in drive 4 (M)[AC], load1=4.65 |
| Ctrl+Down / Ctrl+Up | See completed clean comparator below | View scrolls by rows and returns; visible-caret repeat retains caret on line 11 | STILL PRESENT — edit-o20 | `video/24-scroll-caret-r2-sublimite.mkv` (caret-line11 3.570s; down-5 7.260s; up-5 10.944s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.78; exit=0 |
| Quit / hot exit on small files | See completed clean comparator below | Normal exit zero; tabs and DIRTY_HOT_EXIT_ text restore with dirty marker | FIXED — edit-o20 retained for this small-file workflow | `video/24-files-r2-sublimite.mkv` (before-quit 45.312s; quit 46.747s; hot-restored 50.806s; hot-restored 52.607s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0; restored clipboard and disk hashes |

The first runtime task-24 capture contained the old comparator’s stale rectangle. Its disk/readback evidence is independently valid, but the isolated -r2 recording supplies the visible workflow. Drive 4’s claim that the full prompt/panel Escape sequence was visually established is corrected: its saved Find frame is already obscured by the same lingering failure notice.

### Clean comparator workflows

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| New / Open / missing path / cancel | New buffer and GTK chooser work; missing path produces an empty named tab while the disk file remains absent; cancel closes chooser | Inline prompts work; missing path rejected per o20 decision | STILL PRESENT runtime; CHANGED comparator missing-path observation — edit-o20 | `video/24-files-sublime.mkv` (new 3.002s; open-b-result 7.703s; open-missing-result 11.361s; cancelled 14.510s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11; timings PROVISIONAL (load1 > 8); disk_state_at_step |
| Save As / copy / X save / absent directory | Writes expected bytes; only copy changes; absent-directory result creates no file | Same successful disk hashes and failure outcome | FIXED — edit-o20 retained | `video/24-files-sublime.mkv` (save-untitled-result 20.986s; save-copy-result 26.694s; save-missing-result 31.417s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11; timings PROVISIONAL (load1 > 8); disk hashes equal isolated runtime results |
| Escape priority | Chooser closes, Find row remains visible, then panel closes and selection clears | Lingering save-as failure notice obscures the Find row | CHANGED assessment — pre-existing edit-o20 notice-priority defect | `video/24-files-sublime.mkv` (escape-1 37.288s; escape-2 38.360s; escape-3 39.294s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11; timings PROVISIONAL (load1 > 8); `video/24-files-r2-sublimite.mkv` (escape-1 29.345s; escape-2 30.286s; escape-3 31.178s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; exit=0 |
| Scroll lines / Quit / hot exit | Caret preserved while view scrolls; actual server disappearance recorded; dirty text restored | Same small-file workflow | FIXED — edit-o20 retained | `video/24-scroll-caret-sublime.mkv` (caret-line11 3.678s; down-5 9.216s; up-5 14.791s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.70; timings PROVISIONAL (load1 > 8); `video/24-files-sublime.mkv` (quit 56.551s; hot-restored 61.824s; hot-restored 63.977s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11; timings PROVISIONAL (load1 > 8); shared restored SHA256 a399cae5a392b29949e23f51752139abe064b1c3ee42cf9727c99fb6945e92bd |

Prompt/new-tab onsets are measured individually in the timing appendix. Not every eventual file/tab result has a separately adjudicated first frame; command envelopes and sampled result states remain available, and no missing result-onset value is invented.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| New untitled and Open | FIXED — drive-4 fix retained in the stated runtime probe | edit-o20 | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Missing path and cancel | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20. | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Save As untitled / existing file | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20. | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Save As missing directory | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20. | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Escape priority | CHANGED — assessment corrected: old and new frames show the failure notice covering Find | edit-o20 / edit-48u | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Down / Ctrl+Up | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20. | `video/24-scroll-caret-sublimite.mkv` 1.949–14.907s; (M)[AC], BAT0=Not charging, AC=1, load1=4.38; `video/24-scroll-caret-sublime.mkv` 1.829–15.468s; (M)[AC], BAT0=Not charging, AC=1, load1=13.70 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Quit and hot exit | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-o20 | `video/24-files-r2-sublimite.mkv` 1.822–53.152s; (M)[AC], BAT0=Not charging, AC=1, load1=3.16; `video/24-files-sublime.mkv` 1.981–64.814s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 25. Find keys and replacement continuity

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Return / Shift+Return / F3 / Shift+F3 | 1:1 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12; panel stays open | Same measured sequence; unchanged fixture hash | FIXED — edit-g02 retained | `video/25-findkeys-r2-sublimite.mkv` (typed 4.381s; key-1 5.332s; key-7 10.803s; escaped 11.791s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.20; exit=0; `video/25-findkeys-sublime.mkv` (typed 4.554s; key-1 5.606s; key-7 11.880s; escaped 12.943s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.84; timings PROVISIONAL (load1 > 8); findkeys-drive5-contact.png; unchanged SHA256 2e2fe191debe0b060f6733537ed08fc21db19bd75ceec351bb42795eac852e1d |
| Escape retains last match | Panel closes with last match selected | Same | FIXED — edit-48u retained | `video/25-findkeys-r2-sublimite.mkv` (escaped 11.791s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.20; exit=0; `video/25-findkeys-sublime.mkv` (escaped 12.943s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.84; timings PROVISIONAL (load1 > 8) |
| Continuous Ctrl+Shift+H twice / all / Undo | 2 OMEGA / 28 alpha; 30/0; single Undo to 2/28 | Same counts and hashes | FIXED — edit-g02 retained | `video/25-findkeys-continuous-sublimite.mkv` (one 6.503s; two 7.229s; two-readback 8.732s; all 11.016s; undo-all 12.934s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.65; exit=0; `video/25-findkeys-continuous-sublime.mkv` (one 7.242s; two 8.188s; two-readback 10.045s; all 12.645s; undo-all 14.780s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.80; timings PROVISIONAL (load1 > 8); shared two/undo hash c25a0fd86cbbcc7553d836ab0d7d6259e90a2f3a821d79f04d5968e5bcfd7ab2 |
| Ctrl+Enter | No additional replacement in recorded state | Adds one replacement: 3 OMEGA / 27 alpha | STILL PRESENT — alternative binding; edit-g02 | `video/25-findkeys-r2-sublimite.mkv` (ctrl-enter 29.785s; ctrl-enter 31.611s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.20; exit=0; `video/25-findkeys-sublime.mkv` (ctrl-enter 31.499s; ctrl-enter 33.386s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.84; timings PROVISIONAL (load1 > 8) |

The original per-replacement readback changes document selection, and Sublime’s instrumented second replacement therefore does not advance. Only the uninterrupted repeat establishes continuous replacement parity; this is the same instrumentation caveat as drive 4.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Return / Shift+Return / F3 / Shift+F3 | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02 | `video/25-findkeys-r2-sublimite.mkv` 1.833–32.124s; (M)[AC], BAT0=Not charging, AC=1, load1=5.20; `video/25-findkeys-sublime.mkv` 1.890–34.042s; (M)[AC], BAT0=Not charging, AC=1, load1=13.84 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Escape and selection | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-48u | `video/25-findkeys-r2-sublimite.mkv` 1.833–32.124s; (M)[AC], BAT0=Not charging, AC=1, load1=5.20; `video/25-findkeys-sublime.mkv` 1.890–34.042s; (M)[AC], BAT0=Not charging, AC=1, load1=13.84 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Shift+H twice, all, single undo | FIXED — drive-4 fix retained in the stated runtime probe | edit-g02. | `video/25-findkeys-continuous-sublimite.mkv` 1.862–13.184s; (M)[AC], BAT0=Not charging, AC=1, load1=2.65; `video/25-findkeys-continuous-sublime.mkv` 1.939–15.500s; (M)[AC], BAT0=Not charging, AC=1, load1=12.80 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Ctrl+Enter | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-g02 | `video/25-findkeys-r2-sublimite.mkv` 1.833–32.124s; (M)[AC], BAT0=Not charging, AC=1, load1=5.20; `video/25-findkeys-sublime.mkv` 1.890–34.042s; (M)[AC], BAT0=Not charging, AC=1, load1=13.84 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 26. Reopening paths and aliases

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Absolute / relative / symlink / hard link | Absolute/relative reuse; alias views are separate in the captured strip | Existing edited view reused for all tested identities; undo/redo keeps the edit and view | FIXED — edit-97i retained | `video/26-reopen-r2-sublimite.mkv` (edited 5.605s; absolute 7.208s; relative 10.775s; symlink 14.196s; hardlink 17.728s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.79; exit=0; `video/26-reopen-sublime.mkv` (edited 5.721s; absolute 7.502s; relative 11.431s; symlink 15.268s; hardlink 19.005s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.59; timings PROVISIONAL (load1 > 8); per-alias undo/redo stills |
| Unicode path twice | Unicode title opens and reactivates | Same Unicode tab reactivated; no duplicate | FIXED — edit-97i retained | `video/26-reopen-r2-sublimite.mkv` (unicode-1 20.930s; unicode-2 22.375s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.79; exit=0; `video/26-reopen-sublime.mkv` (unicode-1 22.806s; unicode-2 24.586s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.59; timings PROVISIONAL (load1 > 8) |
| Ctrl+O existing path | Native chooser remains at final sampled state; activation NOT OBSERVED | Existing edited tab activated | FIXED runtime — edit-97i; STILL PRESENT comparator coverage gap | `video/26-reopen-r2-sublimite.mkv` (prompt-reopen 24.738s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.79; exit=0; `video/26-reopen-sublime.mkv` (prompt-reopen 27.461s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.59; timings PROVISIONAL (load1 > 8) |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Absolute / relative / symlink / hard-link / Unicode twice | FIXED — drive-4 fix retained in the stated runtime probe | edit-97i. | `video/26-reopen-r2-sublimite.mkv` 1.824–25.396s; (M)[AC], BAT0=Not charging, AC=1, load1=7.79; `video/26-reopen-sublime.mkv` 1.946–28.142s; (M)[AC], BAT0=Not charging, AC=1, load1=12.59 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 27. Syntax colour

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| C/Rust first text and colour | Coloured text appears; onsets in measurement appendix | Plain text then colour; onsets measured separately | FIXED — edit-11v retained | `video/27-syntax-c-r2-sublimite.mkv` (coloured 3.045s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; exit=0; `video/27-syntax-rs-r2-sublimite.mkv` (coloured 2.760s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43; timings PROVISIONAL (load1 > 8); exit=0; `video/27-syntax-c-sublime.mkv` (coloured 3.266s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97; timings PROVISIONAL (load1 > 8); `video/27-syntax-rs-sublime.mkv` (coloured 3.082s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14; timings PROVISIONAL (load1 > 8) |
| Token classes | Mariana samples measured from actual glyph pixels | C roles match stated theme roles; Rust number 1 remains #c695c6, assignment = remains #d8dee9 | STILL PRESENT — edit-11v Rust role defects | `video/27-tokens-rs-r2-sublimite.mkv` (top-1 3.691s; top-90 15.395s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13; exit=0; logs/token-specs.json, token-rgb.json; complete token table below |
| Open comment / Undo / wheel / isolated Z | Colour changes/undo and Z captured | Colour changes/undo and Z captured; no persistent spill past closing delimiter established | STILL PRESENT — edit-11v editing behaviour | `video/27-syntax-c-r2-sublimite.mkv` (comment 5.798s; undo 8.435s; wheel 14.150s; Z 16.140s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; exit=0; `video/27-syntax-c-sublime.mkv` (comment 5.821s; undo 8.634s; wheel 14.343s; Z 16.492s); (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97; timings PROVISIONAL (load1 > 8); logs/syntax-metrics.json |
| End / newly visible code | End/PageUp reveals coloured code | Literal End lands on blank final line; separate PageUp exposes coloured code | STILL PRESENT — edit-11v; no colour-onset claim for blank EOF | `video/27-end-page-r2-sublimite.mkv` (end 2.776s; page 5.306s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15; exit=0; `video/27-end-page-sublime.mkv` (end 2.965s; page 5.610s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.82; timings PROVISIONAL (load1 > 8) |
| Minimap / light theme | Coloured miniature; default Mariana | Monochrome minimap; isolated documented theme="light" renders light background and coloured text | STILL PRESENT — edit-1nm minimap; edit-11v light coverage retained | `video/27-light-r2-sublimite.mkv` (light 4.283s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.98; exit=0; token viewport stills |

Optional additional-language captures were not made; C and Rust are the measured languages. Token rectangles use the copied fixtures and measured grids; dominant non-background RGB can be an antialiasing fringe in Sublime, so a differing dominant RGB alone is not a wrong-class diagnosis. The two Rust mismatches also violate the runtime’s explicitly specified roles.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| C and Rust initial text / colour | FIXED — drive-4 fix retained in the stated runtime probe | edit-11v | `video/27-syntax-c-r2-sublimite.mkv` 1.876–17.129s; (M)[AC], BAT0=Not charging, AC=1, load1=7.84; `video/27-syntax-rs-r2-sublimite.mkv` 1.828–15.712s; (M)[AC], BAT0=Not charging, AC=1, load1=8.43 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Token classes | STILL PRESENT — Rust number/operator role defects | edit-11v | `video/27-syntax-c-r2-sublimite.mkv` 1.876–17.129s; (M)[AC], BAT0=Not charging, AC=1, load1=7.84; action-specific timestamps/limitations are in the measured rows above. |
| Open comment / undo / wheel / isolated Z | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-11v | `video/27-syntax-c-r2-sublimite.mkv` 1.876–17.129s; (M)[AC], BAT0=Not charging, AC=1, load1=7.84; `video/27-syntax-rs-r2-sublimite.mkv` 1.828–15.712s; (M)[AC], BAT0=Not charging, AC=1, load1=8.43 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Minimap and light theme | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1nm / edit-11v. | `video/27-light-sublimite.mkv` 1.833–4.519s; (M)[AC], BAT0=Not charging, AC=1, load1=4.54; action-specific timestamps/limitations are in the measured rows above. |

### Token RGB measurements

Most frequent non-background colour in the named cell rectangle; antialiasing can make a fringe colour dominant. Expected colours are the supplied theme-role values. “Match” means the runtime matches its expected role, not that Sublime must use the same scope.

| File / token / line:column | Sublime RGB | sublimité RGB | Expected / match | Rectangles and stamps |
|---|---|---|---|---|
| editor.c / preprocessor `#include` / 1:1 | #323841 | #c695c6 | — / not separately specified | [70, 60, 72, 18]; [80, 40, 160, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / string `"editor/private.h"` / 1:10 | #99c794 | #99c794 | #99c794 / match | [151, 60, 162, 18]; [260, 40, 360, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / keyword `static` / 13:1 | #30384a | #c695c6 | #c695c6 / match | [70, 276, 54, 18]; [80, 520, 120, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / type `bool` / 13:8 | #303842 | #c695c6 | #c695c6 / match | [133, 276, 36, 18]; [220, 520, 80, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / function declaration `runnable` / 13:13 | #3d83aa | #5fb4b4 | #5fb4b4 / match | [178, 276, 72, 18]; [320, 520, 160, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / function definition `min64` / 16:17 | #323841 | #5fb4b4 | #5fb4b4 / match | [214, 330, 45, 18]; [400, 640, 100, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / function call `trace_now_ns` / 20:24 | #3f72c0 | #5fb4b4 | #5fb4b4 / match | [277, 402, 108, 18]; [540, 800, 240, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / operator `=` / 20:22 | #f97b58 | #f97b58 | #f97b58 / match | [259, 402, 9, 18]; [500, 800, 20, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / punctuation `;` / 13:38 | #303842 | #a6acb9 | #a6acb9 / match | [403, 276, 9, 18]; [820, 520, 20, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / comment `Certify` / 25:8 | #a6acb9 | #a6acb9 | #a6acb9 / match | [133, 492, 63, 18]; [220, 1000, 140, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / number `4096` / 26:19 | #303842 | #f9ae58 | #f9ae58 / match | [232, 510, 36, 18]; [440, 1040, 80, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| editor.c / macro `LINEIDX_CHUNK` / 26:53 | #d8dee9 | #c695c6 | #c695c6 / match | [538, 510, 117, 18]; [1120, 1040, 260, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| config.rs / comment `Native` / 1:5 | #a6acb9 | #a6acb9 | #a6acb9 / match | [106, 60, 54, 18]; [160, 40, 120, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / keyword `use` / 6:1 | #c695c6 | #c695c6 | #c695c6 / match | [70, 150, 27, 18]; [80, 240, 60, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / type `Path` / 7:17 | #303843 | #c695c6 | #c695c6 / match | [214, 168, 36, 18]; [400, 280, 80, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / string `"font"` / 16:5 | #4d8e8d | #99c794 | #99c794 / match | [106, 330, 54, 18]; [160, 640, 120, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / constant `CONFIG_KEYS` / 15:11 | #f9ae58 | #c695c6 | #c695c6 / match | [160, 312, 99, 18]; [280, 600, 220, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / operator `=` / 15:32 | #f97b58 | #d8dee9 | #f97b58 / MISMATCH | [349, 312, 9, 18]; [700, 600, 20, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / punctuation `;` / 6:13 | #303842 | #a6acb9 | #a6acb9 / match | [178, 150, 9, 18]; [320, 240, 20, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / attribute `derive` / 34:3 | #6699cc | #c695c6 | — / not separately specified | [88, 60, 54, 18]; [120, 40, 120, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / function definition `fmt` / 56:8 | #303842 | #5fb4b4 | #5fb4b4 / match | [133, 456, 27, 18]; [220, 920, 60, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / lifetime `'_` / 56:42 | #334055 | #595e67 | — / not separately specified | [439, 456, 18, 18]; [900, 920, 40, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / function call `write_str` / 59:35 | #6699cc | #5fb4b4 | #5fb4b4 / match | [376, 510, 81, 18]; [760, 1040, 180, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| config.rs / number `1` / 96:11 | #e87f47 | #c695c6 | #f9ae58 / MISMATCH | [160, 168, 9, 18]; [280, 280, 20, 40]; (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL; (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |

## 28. Gutter selection

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Click / drag / Shift+click | Whole-line click copies 65 bytes; cell-centre drag and Shift+click retain that same readback in this attempt | 65-byte line, 260-byte drag range, 390-byte Shift extension | FIXED — edit-dr3 runtime selection retained; CHANGED comparator Shift result | `video/28-gutter-cells-r2-sublimite.mkv` (click 4.841s; drag 7.405s; shift 8.238s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.85; exit=0; `video/28-gutter-cells-sublime.mkv` (click 9.311s; drag 12.220s; shift 13.512s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.48; timings PROVISIONAL (load1 > 8); exact clipboard files and cell targets |
| Click after scrolling | Inherited result captured | Copied line matches source line 98 after scrolling; no stale-position failure | FIXED — edit-m15 retained | `video/28-gutter-r2-sublimite.mkv` (scrolled 12.080s; after-scroll 12.540s); (M)[AC], BAT0=Not charging, AC/online=1, load1=5.36; exit=0; readback equals ascii_code.c line 98 |

Do not infer identical gutter drag/Shift behaviour from the comparator’s unchanged clipboard. Runtime ranges are independently verified. The later isolated text-selection repeat restores ordinary drag/Shift parity (task 15), but it is not a replacement gutter-drag probe.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Click / drag / Shift+click / click after scroll | FIXED — drive-4 fix retained in the stated runtime probe | edit-dr3 / edit-m15 | `video/28-gutter-r2-sublimite.mkv` 1.842–13.266s; (M)[AC], BAT0=Not charging, AC=1, load1=5.36; `video/28-gutter-sublime.mkv` 1.801–15.710s; (M)[AC], BAT0=Not charging, AC=1, load1=12.36 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Gutter cell-centre repeat | CHANGED comparator result; FIXED runtime whole-line/range behaviour retained | edit-dr3 | `video/28-gutter-cells-r2-sublimite.mkv` 1.839–8.949s; (M)[AC], BAT0=Not charging, AC=1, load1=4.85; `video/28-gutter-cells-sublime.mkv` 1.848–14.600s; (M)[AC], BAT0=Not charging, AC=1, load1=13.48 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 29. Stalled X server

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Initial stall and C/D/E repeats | See completed comparison below | Survives all prescribed stops; AB saved after first, subsequent probe characters retained; normal exit zero | FIXED — edit-cee raster path retained; edit-848 GL path not exercised | `video/29-stall-sublimite.mkv` (launch 1.909s; final 32.891s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.53; exit=0; verified :98 PID/argv, X_STOP/X_CONT, alive_after_stall and disk events in JSON |

Only the verified Xvfb :98 owner was signalled. The prescribed stop interval is an input parameter; ffmpeg also pauses while X is stopped. Stop/continue timestamps and video mapping are retained in the task timeline. No error -11 / BadWindow appears in this run’s stderr.

### Sublime stalled-server comparison

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| One prescribed stop/continue | Remains usable; B/save produces ABstall fixture | Runtime survives all prescribed repeats | FIXED — edit-cee raster probe retained | `video/29-stall-sublime.mkv` (launch 1.901s; final 10.840s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.32; timings PROVISIONAL (load1 > 8); X_STOP/X_CONT and disk events |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Specified stopped-X sequence | FIXED — drive-4 fix retained in the stated runtime probe | edit-cee / edit-848 | `video/29-stall-sublimite.mkv` 1.909–32.891s; (M)[AC], BAT0=Not charging, AC=1, load1=3.53; `video/29-stall-sublime.mkv` 1.901–10.840s; (M)[AC], BAT0=Not charging, AC=1, load1=13.32 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 30. Repeated external changes and dirty branch

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Controlled sequence, each run plus post-sequence dwell | See completed comparison below | All repeated runs preserve expected reload/keep hashes, remain alive after dwell, and close zero | FIXED — edit-cee retained; edit-1um prompt placement retained | `video/30-external-x3-1-sublimite.mkv` (prompt-R 8.032s; prompt-K 13.415s; kept 15.452s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.67; exit=0; `video/30-external-x3-2-sublimite.mkv` (prompt-R 8.142s; prompt-K 13.374s; kept 15.493s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.16; exit=0; `video/30-external-x3-3-sublimite.mkv` (prompt-R 8.077s; prompt-K 13.366s; kept 15.467s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.54; exit=0; reloaded d880abcb5e564b731c4e1d2bf7c0869e4555d7b22cc7ca87dc0c2f68b0588219; kept 0074d511e872a0b722d07e4ad1d7130ef05f461b576c0731bb20fa702df402f0 |
| Save x, Undo, type y, external replacement | See completed comparison below | R/K prompt appears; K retains y plus original text, not the external replacement | STILL PRESENT — edit-37a passing | `video/30-dirty-branch-sublimite.mkv` (prompt 6.292s; kept 8.143s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.13; exit=0; literal kept readback |

### Clean controlled external comparator

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Controlled shrink / R / K | Reload/keep bytes match runtime and drive-3/4 hashes | All repeated runtime runs match | FIXED — edit-cee; edit-1um prompt retained | `video/30-external-x3-1-sublime.mkv` (prompt-R 9.146s; reloaded 12.305s; prompt-K 15.820s; kept 18.807s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.35; timings PROVISIONAL (load1 > 8); matched SHA256 values in task JSON |
| Save x / Undo / y / external replacement | Dirty text retained after cancelling/keeping conflict | Dirty R/K prompt; K preserves y | STILL PRESENT — edit-37a | `video/30-dirty-branch-sublime.mkv` (prompt 6.657s; kept 8.884s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14; timings PROVISIONAL (load1 > 8); `video/30-dirty-branch-sublimite.mkv` (prompt 6.292s; kept 8.143s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.13; exit=0 |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Controlled sequence with post-sequence dwell | FIXED — drive-4 fix retained in the stated runtime probe | edit-cee / edit-1um | `video/30-external-x3-1-sublimite.mkv` 1.834–25.756s; (M)[AC], BAT0=Not charging, AC=1, load1=2.67; `video/30-external-x3-2-sublimite.mkv` 1.839–25.863s; (M)[AC], BAT0=Not charging, AC=1, load1=2.16; action-specific timestamps/limitations are in the measured rows above. |
| Save x, undo, type y, external replacement | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-37a | `video/30-dirty-branch-sublimite.mkv` 1.839–8.444s; (M)[AC], BAT0=Not charging, AC=1, load1=2.13; `video/30-dirty-branch-sublime.mkv` 1.923–9.546s; (M)[AC], BAT0=Not charging, AC=1, load1=12.14 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |

## 31. Paging during indexing

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Immediate Ctrl+End / Prior sequence | Private first-launch and ready loaded comparisons are reported below | First near-end frame already includes queued PageUps: 24.49 intervals / 408.2 ms from End; 21.90 / 365.1 ms from first Prior; at 1.300000s | STILL PRESENT — edit-91c / edit-pd6 / edit-jf9; initial queued keys share paint | `video/31-large-index-sublimite.mkv` (labels-final 122.983s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19; exit=0; logs/index-metrics-verified.json; ROI (0,46,140,27) |
| Next burst / label watch | See clean comparison below | EOF label 8,947,842; burst settles at 3.016667s, 3.20 intervals / 53.4 ms after command | STILL PRESENT — correct net navigation; edit-jf9 / edit-xzw | `video/31-large-index-sublimite.mkv` (labels-final 122.983s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19; exit=0; logs/index-watch-samples.json, watch-20 frame |
| Estimate-to-exact switch | Not observed | No distinguishable estimate flag/transition; final raw label agrees with EOF | STILL PRESENT — coverage gap; edit-xzw / edit-xkd | `video/31-large-index-sublimite.mkv` (labels-final 122.983s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19; exit=0; no internal completion flag inferred |
| Trace sample count | No runtime trace | G1 n=20; G3/T6 n=19, versus prescribed 41 key events | CHANGED — sample counts; no claim that each key produced a separate frame; edit-pd6 / edit-jf9 | `video/31-large-index-sublimite.mkv` (labels-final 122.983s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19; exit=0; traces/31-large-index-sublimite.txt |
| Immediate wheel + Ctrl+Up after End | Not requested as separate comparator | Alive; movement lands near EOF with top line 8,947,836; normal close | STILL PRESENT — edit-pd6 passing | `video/31-large-queued-sublimite.mkv` (landed 3.346s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.20; exit=0 |

Prior samples (video timestamps and raw OCR retained in logs/index-metrics-verified.json): 1: 1, 2: 1, 3: 1, 4: 8947782, 5: 8947767, 6: 8947752, 7: 8947737, 8: 8947722, 9: 8947707, 10: 8947692, 11: 8947677, 12: 8947662, 13: 8947647, 14: 8947632, 15: 8947617, 16: 8947602, 17: 8947587, 18: 8947572, 19: 8947557, 20: 8947542. OCR corrections were read from index-prior-contact.png; they were not filled from the expected arithmetic. First samples remain at line 1 before the queued end view paints. Drive 4 G1/G3/T6 counts were 21/20/20 (M)[AC], load1=2.12. Drive 3’s settled End was 16.04 intervals / 267.4 ms (M)[AC], load1=15.26; different index state and load prevent a regression claim.

```text
G1 T4-T0     n=20 p50=2.646 ms p99=380.847 ms
G3 T5-T0     n=19 p50=4.254 ms p99=383.339 ms
T6-T0        n=19 p50=22.737 ms p99=393.110 ms
```

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| Immediate Ctrl+End and Prior sequence | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c / edit-pd6 / edit-jf9. | `video/31-large-index-sublimite.mkv` 122.983–123.253s; (M)[AC], BAT0=Not charging, AC=1, load1=2.19; `video/31-early-end-only-r2-sublimite.mkv` 2.871–3.158s; (M)[AC], BAT0=Not charging, AC=1, load1=4.15; action-specific timestamps/limitations are in the measured rows above. |
| Next burst and label watch | STILL PRESENT — transition coverage gap; final raw EOF label is correct | edit-xkd / edit-xzw. | `video/31-large-index-sublimite.mkv` 122.983–123.253s; (M)[AC], BAT0=Not charging, AC=1, load1=2.19; action-specific timestamps/limitations are in the measured rows above. |
| Trace event sample count | CHANGED — printed trace sample counts differ; net navigation verified | edit-pd6 / edit-jf9 | `video/31-large-index-sublimite.mkv` 122.983–123.253s; (M)[AC], BAT0=Not charging, AC=1, load1=2.19; action-specific timestamps/limitations are in the measured rows above. |
| Immediate wheel + Ctrl+Up after End | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pd6 | `video/31-large-queued-sublimite.mkv` 3.346–3.628s; (M)[AC], BAT0=Not charging, AC=1, load1=2.20; action-specific timestamps/limitations are in the measured rows above. |
| Isolated early Ctrl+End | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-91c | `video/31-early-end-only-r2-sublimite.mkv` 2.871–3.158s; (M)[AC], BAT0=Not charging, AC=1, load1=4.15; action-specific timestamps/limitations are in the measured rows above. |
| Estimate-to-exact switch | STILL PRESENT — transition coverage gap; final raw EOF label is correct | edit-xzw | `video/31-large-index-sublimite.mkv` 122.983–123.253s; (M)[AC], BAT0=Not charging, AC=1, load1=2.19; action-specific timestamps/limitations are in the measured rows above. |

### Isolated early End

| Action | Observation | Status / bead | Evidence |
|---|---|---|---|
| early-end | 22.62 intervals / 377.0 ms; command 0.839664–0.870922s → frame 1.216667s; EOF visible | STILL PRESENT — edit-91c / edit-jf9 | `video/31-early-end-only-r2-sublimite.mkv` (end 2.871s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15; exit=0; logs/extra-onsets.json; ROI [0, 46, 140, 27] |

Drive 4’s isolated early End was 49.84 intervals / 830.6 ms (M)[bat], load1=7.32. Power/load and index state differ; no controlled regression ranking. The current case is distinct from the queued-page case.


### Sublime first-launch and loaded queue comparison

| Action | Sublime did | Runtime comparison | Status / bead | Evidence |
|---|---|---|---|---|
| First-launch observer / final watch | First-text observer times out; later final frame shows EOF label 8,947,842. Exact text-readiness onset and individual early key results are not established | Early runtime net navigation and labels verified above | CHANGED comparator coverage; edit-91c / edit-pd6 / edit-jf9 | `video/31-large-index-sublime.mkv` (labels-final 243.181s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.89; timings PROVISIONAL (load1 > 8); first_text_timeout in JSON |
| Already-loaded sequence | Same End/Prior/Next keys; final EOF label 8,947,842 | Same net final EOF | STILL PRESENT — working net navigation; no individual-paint claim for zero-delay burst | `video/31-large-loaded-sublime.mkv` (ready 0.766s; labels-final 123.194s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.53 |

These comparator captures use the private profile described in Conditions. The standard accumulated-profile large-file attempt remains an excluded readiness limitation.

## 32. Crash atomicity and orphan recovery

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| File undo; prescribed delay 0 intervals | Not applicable: sublimité journal atomicity | 262144 bytes, complete fixture hash; measured kill offset 1.546 ms after command completion | STILL PRESENT — edit-pgr passing | `video/32-crash-undo-0-sublimite.mkv` (recovered 29.941s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.18; exit=0; binary readback a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; disk hash unchanged in JSON |
| File undo; prescribed delay 1 intervals | Not applicable: sublimité journal atomicity | 262144 bytes, complete fixture hash; measured kill offset 17.786 ms after command completion | STILL PRESENT — edit-pgr passing | `video/32-crash-undo-1-sublimite.mkv` (recovered 29.812s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.72; exit=0; binary readback a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; disk hash unchanged in JSON |
| File undo; prescribed delay 2 intervals | Not applicable: sublimité journal atomicity | 262144 bytes, complete fixture hash; measured kill offset 34.467 ms after command completion | STILL PRESENT — edit-pgr passing | `video/32-crash-undo-2-sublimite.mkv` (recovered 30.243s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.36; exit=0; binary readback a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; disk hash unchanged in JSON |
| File undo; prescribed delay 4 intervals | Not applicable: sublimité journal atomicity | 262144 bytes, complete fixture hash; measured kill offset 67.778 ms after command completion | STILL PRESENT — edit-pgr passing | `video/32-crash-undo-4-sublimite.mkv` (recovered 30.243s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.19; exit=0; binary readback a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; disk hash unchanged in JSON |
| File undo; prescribed delay 8 intervals | Not applicable: sublimité journal atomicity | 262144 bytes, complete fixture hash; measured kill offset 134.927 ms after command completion | STILL PRESENT — edit-pgr passing | `video/32-crash-undo-8-sublimite.mkv` (recovered 30.015s); (M)[AC], BAT0=Not charging, AC/online=1, load1=4.29; exit=0; binary readback a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; disk hash unchanged in JSON |
| Untitled undo variant | Not applicable: app-specific session journal | Target untitled tab restores full fixture, 262144 bytes, fixture hash | STILL PRESENT — edit-pgr passing | `video/32-crash-untitled-sublimite.mkv` (recovered 30.082s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.86; exit=0; recovered binary readback |
| Two instances: orphan crash + other normal close | Not applicable: shared sublimité profile format | Both CLEAN_TWO_two baseline and ORPHAN_ONE_one baseline restore on first and both later relaunches; disk baselines unchanged | STILL PRESENT — edit-rzj passing | `video/32-crash-two-sublimite.mkv` (restored-0 18.686s; restored-1 23.513s; restored-2 28.385s); (M)[AC], BAT0=Not charging, AC/online=1, load1=3.51; exit=0; per-tab binary files and profile listings |

Every sampled file/untitled target recovery is the complete fixture. The sweep does not observe the journal drain directly and does not prove any kill intersected it. The untitled helper also copies the empty companion tab: its clipboard readback is stale (copying an empty selection leaves the previous clipboard), so that other-tab value is excluded. The recovered target still shows the fixture’s final line and selected text; no duplicate full-text tab is inferred. Prescribed delays are inputs (P: 1/60 s per interval), not performance budgets.

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| File undo, delay 0 intervals (input) | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-undo-0-sublimite.mkv` 1.839–30.289s; (M)[AC], BAT0=Not charging, AC=1, load1=2.18; action-specific timestamps/limitations are in the measured rows above. |
| File undo, delay 1 intervals (input) | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-undo-1-sublimite.mkv` 1.850–30.128s; (M)[AC], BAT0=Not charging, AC=1, load1=1.72; action-specific timestamps/limitations are in the measured rows above. |
| File undo, delay 2 intervals (input) | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-undo-2-sublimite.mkv` 1.842–30.924s; (M)[AC], BAT0=Not charging, AC=1, load1=1.36; action-specific timestamps/limitations are in the measured rows above. |
| File undo, delay 4 intervals (input) | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-undo-4-sublimite.mkv` 1.891–30.560s; (M)[AC], BAT0=Not charging, AC=1, load1=3.19; action-specific timestamps/limitations are in the measured rows above. |
| File undo, delay 8 intervals (input) | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-undo-8-sublimite.mkv` 1.840–30.382s; (M)[AC], BAT0=Not charging, AC=1, load1=4.29; action-specific timestamps/limitations are in the measured rows above. |
| Untitled undo variant | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-pgr | `video/32-crash-untitled-sublimite.mkv` 1.815–31.739s; (M)[AC], BAT0=Not charging, AC=1, load1=2.86; action-specific timestamps/limitations are in the measured rows above. |
| Two instances, crashed dirty + clean close of other dirty instance | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-rzj | `video/32-crash-two-sublimite.mkv` 1.884–31.742s; (M)[AC], BAT0=Not charging, AC=1, load1=3.51; action-specific timestamps/limitations are in the measured rows above. |

## 33. Small landing probes

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| CRLF paste/save | See clean comparison below | Disk bytes are one\r\ntwo\r\nthree\r\nfour\r\n | STILL PRESENT — edit-bml passing | `video/33-crlf-sublimite.mkv` (saved 5.134s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.79; exit=0; hex 6f6e650d0a74776f0d0a74687265650d0a666f75720d0a and od in JSON |
| Save failure then recovery | See clean comparison below | First save leaves baseline; after permissions restored, AB prefix saves and title is clean | STILL PRESENT — edit-1vo passing | `video/33-save-fail-sublimite.mkv` (failed 3.495s; recovered 4.701s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.56; exit=0; disk hashes/bytes in JSON |
| Glyph cache pressure | See clean comparison below | Sampled CJK pages render glyphs; no # placeholder observed on reviewed pages | STILL PRESENT — edit-r1l; finite sampled coverage | `video/33-glyphs-sublimite.mkv` (cycle0-page0 2.655s; cycle0-page5 7.005s; cycle1-page0 12.375s; cycle1-page5 16.530s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.84; exit=0 |
| Prompt placement | See clean comparison below | Line 1 visible while R/K prompt occupies bottom row | FIXED — edit-1um retained | `video/30-external-x3-3-sublimite.mkv` (prompt-R 8.077s; prompt-K 13.366s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.54; exit=0 |

### Clean small-probe comparator

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| CRLF paste | Every saved line ends CRLF; bytes match runtime | CRLF preserved | STILL PRESENT — edit-bml | `video/33-crlf-sublime.mkv` (saved 5.647s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11; timings PROVISIONAL (load1 > 8); identical hex readback |
| Directory made non-writable / retry | First A save succeeds on the existing writable file; AB save also succeeds; this does not exercise a failed Sublime save | First save fails, then retry succeeds after directory permissions restored | STILL PRESENT runtime recovery — edit-1vo; differing save behaviour | `video/33-save-fail-sublime.mkv` (failed 4.060s; recovered 6.153s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.87; timings PROVISIONAL (load1 > 8); first disk 22 bytes, second 23; runtime first disk remains 21 bytes |
| CJK cache pressure | Sampled pages render CJK, no # observed | Same sampled outcome | STILL PRESENT — edit-r1l | `video/33-glyphs-sublime.mkv` (cycle0-page0 5.201s; cycle1-page0 20.471s; final 33.541s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.72; timings PROVISIONAL (load1 > 8); runtime matching fixture |

Drive-4 row traceability (status describes the runtime result unless a comparator qualification is explicit):

| Drive-4 action row | Drive-5 status | Bead | Current evidence |
|---|---|---|---|
| LF clipboard into CRLF buffer | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-bml | `video/33-crlf-sublimite.mkv` 1.826–5.472s; (M)[AC], BAT0=Not charging, AC=1, load1=2.79; `video/33-crlf-sublime.mkv` 1.984–6.323s; (M)[AC], BAT0=Not charging, AC=1, load1=12.11 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Directory mode 555 save / restore permissions / save | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-1vo | `video/33-save-fail-sublimite.mkv` 1.840–4.949s; (M)[AC], BAT0=Not charging, AC=1, load1=2.56; `video/33-save-fail-sublime.mkv` 2.001–6.924s; (M)[AC], BAT0=Not charging, AC=1, load1=12.87 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| CJK cache pressure and second pass | STILL PRESENT — retained result or qualified comparison; see this task’s measured rows | edit-r1l | `video/33-glyphs-sublimite.mkv` 1.833–21.314s; (M)[AC], BAT0=Not charging, AC=1, load1=2.84; `video/33-glyphs-sublime.mkv` 3.848–33.541s; (M)[AC], BAT0=Not charging, AC=1, load1=12.72 (timing provisional); action-specific timestamps/limitations are in the measured rows above. |
| Prompt placement | FIXED — drive-4 fix retained in the stated runtime probe | edit-1um. | `video/30-external-x3-1-sublimite.mkv` 1.834–25.756s; (M)[AC], BAT0=Not charging, AC=1, load1=2.67; action-specific timestamps/limitations are in the measured rows above. |

## 34. Expanded emoji

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Single emoji / heart variation selector / modifier / ZWJ / family / regional-indicator flag | See clean comparison below | All six lines draw monochrome glyphs at default and larger zoom; family group and outlined AU flag are recognizable; no missing box or # seen | NEW — environment + edit-v16; edit-r8e colour waived | `video/34-emoji-sublimite.mkv` (default 2.117s; zoom2 3.223s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.85; exit=0; fc-list charset=1f469 in JSON |
| Caret / Backspace / Undo / save | See clean comparison below | Cluster-step/deletion sequence captured; save preserves fixture hash | NEW — edit-r8e coverage; deletion policy no bead | `video/34-emoji-sublimite.mkv` (line-0-right-1 5.231s; line-3-right-1 24.224s; line-4-deleted 34.232s; final 43.618s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.85; exit=0; fixture/saved_hash in JSON |

### Per-line emoji results and corrected deletion attribution

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| U+1F600 | colour glyph; after Backspace: empty | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-1-right-1 5.440s; line-1-deleted 8.624s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-1-deleted 8.606s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| U+2764 U+FE0F | colour glyph; after Backspace: U+2764 | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-2-right-1 11.696s; line-2-deleted 14.956s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-2-deleted 14.823s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| U+1F44D U+1F3FD | colour glyph; after Backspace: U+1F44D | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-3-right-1 17.963s; line-3-deleted 21.123s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-3-deleted 21.030s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| U+1F469 U+200D U+1F4BB | colour glyph; after Backspace: U+1F469 U+200D | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-4-right-1 24.197s; line-4-deleted 27.524s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-4-deleted 27.547s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| U+1F468 U+200D U+1F469 U+200D U+1F467 | black/white family pictogram; after Backspace: U+1F468 U+200D U+1F469 U+200D | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-5-right-1 30.690s; line-5-deleted 34.132s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-5-deleted 34.082s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| U+1F1E6 U+1F1FA | colour glyph; after Backspace: U+1F1E6 | monochrome glyph, no box or #; after Backspace: empty | NEW coverage — edit-r8e waived / edit-v16 + installed font; deletion policy no bead | `video/34-emoji-sublime.mkv` (default 2.621s; zoom2 4.021s); (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59; timings PROVISIONAL (load1 > 8); `video/34-emoji-r2-sublimite.mkv` (line-6-right-1 37.484s; line-6-deleted 41.099s); (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25; exit=0; `video/34-emoji-r2-sublime.mkv` (line-6-deleted 40.892s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |

The final validation resets to the intended line before deletion and seeds the clipboard before copying. Every readback rejects the sentinel; both editors save the original expanded-fixture SHA256 bd45ecde1bb5bf2490caa99176134fea7d98c3b592e5d8e3da4e54374bb87daa. Earlier solo-emoji Right probes cross newlines, so their deletion labels are not used for per-line attribution. The final Sublime update offer occludes some upper-line caret pixels, but verified clipboard bytes establish the deletion result; appearance comes from the unobscured earlier task-34 recording. One Right crosses the emoji cluster, as confirmed by the resulting deletion location. No colour-artwork item is a blocker.

## 35. Trailing-whitespace marker

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| Default word + Space / more + spaces | See clean comparison below | No marker in the sampled Space sequence or settled continued typing | NEW — edit-hgd default behaviour passes | `video/35-whitespace-default-sublimite.mkv` (space 3.212s; more-spaces 4.972s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.77; exit=0; logs/whitespace-metrics.json; thirty resampled frames in space-sequence; ROI (40,40,400,40) |
| Default completed lines | No default marker (clean comparison below) | No markers on alpha spaces, beta tab or gamma | NEW — edit-hgd none passes | `video/35-whitespace-lines-sublimite.mkv` (default-none 2.159s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.71; exit=0 |
| all; completed lines / active caret run | Not applicable to the runtime preference | Completed alpha/beta trailing runs marked; line-1 run suppressed when caret ends there; typing space suppressed; prior line marked after Return | NEW — edit-hgd all/caret behaviour passes | `video/35-whitespace-all-sublimite.mkv` (completed 3.235s; caret-run 4.241s; typing-run 6.608s; previous-completed 7.973s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.57; exit=0; trailing-run crop (140,40,40,40): background RGB (73,48,56), underline (191,97,106) |
| selection / click away | Not applicable | Only selected trailing runs underlined; click away clears markers | NEW — edit-hgd selection passes | `video/35-whitespace-selection-sublimite.mkv` (selected 2.512s; away 3.464s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48; exit=0; selection preserves blue background and white underline |
| Live reload none / bogus | Not applicable | Markers turn off for none and bogus; process remains alive and closes zero | NEW — edit-hgd fallback/reload passes | `video/35-whitespace-selection-sublimite.mkv` (selected-again 4.139s; reload-none 6.475s; reload-bogus 8.814s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48; exit=0 |
| Invalid-value diagnostic | Not applicable | No user-visible diagnostic in the sampled status row or complete stderr log | NEW finding — edit-hgd diagnostic expectation not observed | `video/35-whitespace-selection-sublimite.mkv` (reload-bogus 8.814s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48; exit=0; logs/35-whitespace-selection-sublimite.editor.log |

All runtime preference files/data homes are private. Default Space analysis checks both prescribed orange colours and the actual dark-theme error colours measured from enabled markers; their frame maxima are recorded in whitespace-metrics.json. Selection underline is visible in the enlarged selected-whitespace-crop.png and absent after bogus reload. The invalid-value diagnostic gap does not prevent ordinary default typing. Literal typing trace follows with the default-run stamp:

```text
G1 T4-T0     n=12 p50=1.505 ms p99=38.828 ms
G3 T5-T0     n=11 p50=1.578 ms p99=6.977 ms
T6-T0        n=11 p50=17.837 ms p99=26.521 ms
```

### Sublime default and marker scan

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| word / Space / more / spaces | No marker; observed core default is draw_white_space=["selection"] | No marker under private no-config default | NEW — edit-hgd default passes | `video/35-whitespace-default-sublime.mkv` (space 3.786s; more-spaces 5.886s); (M)[AC], BAT0=Not charging, AC/online=1, load1=12.86; timings PROVISIONAL (load1 > 8); `video/35-whitespace-default-sublimite.mkv` (space 3.212s; more-spaces 4.972s); (M)[AC], BAT0=Not charging, AC/online=1, load1=2.77; exit=0; logs/whitespace-metrics.json |

The inspected Default preferences do not contain the brief’s literal trailing_spaces key. The actual core setting above is recorded in logs/sublime-default-settings.txt. The captured default typing shows no trailing marker; no plugin setting is assumed.

## 36. Oversized orphan startup

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| First launch | Not applicable: no sublimité session journals | Mapped in 68.443 ms; first painted frame at 1.600000s: 6.58 intervals / 109.7 ms from launch; required notice appears | NEW — edit-fm4 observed | `video/36-orphan-sublimite.mkv` (notice 2.817s; usable 3.581s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.24; exit=0; logs/orphan-metrics.json, title-ink ROI (0,0,400,40), >40 pixels above RGB max140 |
| Repeat launch | Not applicable: no sublimité session journals | Mapped in 65.786 ms; first painted frame at 0.866667s: 7.15 intervals / 119.2 ms from launch; required notice appears | NEW — edit-fm4 observed | `video/36-orphan-repeat-sublimite.mkv` (notice 2.138s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.30; exit=0; logs/orphan-metrics.json, title-ink ROI (0,0,400,40), >40 pixels above RGB max140 |
| Orphan preservation / disk growth | Not applicable | Same name, size, mtime and SHA256 after both launches; no new set-aside or second oversized journal; Q accepted; both closes zero | NEW — edit-fm4 passing | `video/36-orphan-sublimite.mkv` (notice 2.817s; usable 3.581s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.24; exit=0; `video/36-orphan-repeat-sublimite.mkv` (notice 2.138s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.30; exit=0; logs/36-before.txt, 36-after.txt, 36-after-second.txt, 36-comparison.json |

The prescribed old-binary GUI producer left only an 8192-byte journal, so it did not establish the fixture. The explicitly allowed fallback follows tests/editor_session_test.c fm4_orphan in a headless fixture generator linked to the existing current library, with origin invalidated before checkpoint. Its journal is 1,077,936,128 bytes (M; snapshot and capture stamps above), larger than the prescribed 105,000,000-byte threshold. The BASE was then changed. UI verification uses only the pinned build/astra5/sublimite. The artifact differs in byte size from the coordinator’s historical checkpoint; generator provenance is logged. Orphan SHA256 66bd6fafb23a4309b2459302d46568741f413ab2ddeebea5ed21178a64c48c6a remains identical. Each launch leaves only its own small session (8192 bytes) and lease, replacing the prior small session on repeat; the large orphan stays untouched. Generated large orphan and sparse source were deleted after verification. Deferred content is not automatically restored; that is the documented fm4 limit, not a defect of this build. The historical 10.06 s error -103 and warm-start aspiration are context, not pass/fail budgets.

## 37. Checkpoint growth and reopening

| Action | Sublime did | sublimité did | Drive-4 status / bead | Evidence |
|---|---|---|---|---|
| dense64 journal growth | Not applicable: sublimité checkpoints | 32 samples: start 4233 B; first post-start 8192 B; last before extra char 8192 B; after extra char 12288 B; largest step 4096 B | NEW — edit-28u growth behaviour observed | `video/37-checkpoint-growth-dense64-sublimite.mkv` (dirty 66.033s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.36; exit=0; logs/37-journal-sizes.txt, each sample carries power/load and stat |
| log1g journal growth | Not applicable: sublimité checkpoints | 32 samples: start 4233 B; first post-start 8192 B; last before extra char 8192 B; after extra char 12288 B; largest step 4096 B | NEW — edit-28u growth behaviour observed | `video/37-checkpoint-growth-log1g-sublimite.mkv` (dirty 66.140s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49; exit=0; logs/37-journal-sizes.txt, each sample carries power/load and stat |
| dense64 hot-exit relaunch | Not applicable | Original launch closes zero; no-argument relaunch exits 1 with error -103 before a window; copied journal reproduces exit 1 | NEW BLOCKER — edit-28u test scope; root cause unestablished | `video/37-checkpoint-growth-dense64-sublimite.mkv` (dirty 66.033s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.36; exit=0; `video/37-dense-restart-check-sublimite.mkv` (failed 0.989s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49; exit=1; editor stderr and logs/37-restart-fixture.json |
| log1g hot-exit relaunch | Not applicable | Window appears; large session is deferred instead of automatically restoring the dirty tab | NEW — documented edit-fm4 limit; not a defect attributed to this build | `video/37-checkpoint-growth-log1g-sublimite.mkv` (restored 68.960s; restored-home 69.835s); (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49; exit=0; preserved source session in private profile |

Neither recording has a whole-file-size step. The start sample is followed by small page-aligned growth; subsequent idle samples have stable size/mtime until the additional character. The sampling does not establish a periodic checkpoint interval when idle writes are skipped. The dense64 file was opened in place and never saved; its stat remains unchanged and its bytes are checked against the untouched copied corpus. Its failed-recovery journal is retained as session-oAHqSe.set-aside-1792092780488563967 in the original task state. An exact copy under a discoverable session name in a fresh private profile reproduces the startup failure. This is a new task-37 finding, not proof that drive 4 passed this same dense-file recovery case. The 1 GiB source exceeds fm4’s combined journal/BASE startup cost bound, so that session’s documented deferral is distinct from the dense64 startup error.

## Drive-4 findings disposition

Only find in files and colour emoji artwork carry Tobias’s M0 waivers. Other historical blockers are recorded as fixed, not waived.

| Drive-4 finding | Bead | Observed disposition | Evidence |
|---|---|---|---|
| Missing features: New/Open/Save As/Quit | edit-o20 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 22/24: prompts, disk hashes, rebound copy, process exit and dirty relaunch. |
| Missing features: predictable wrapped prose wheel | edit-zw9 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 15/23: visual-row registration and identical runtime body after reversal. |
| Missing features: complete emoji artwork | edit-r8e | CHANGED — missing box resolved by monochrome artwork; colour remains absent and waived | 20/34 |
| Missing features: find in files and code colour | edit-xkd / edit-11v / edit-1nm | STILL PRESENT project-search/minimap gaps (search waived); FIXED body colour retained | 16/27 |
| Missing features: search muscle-memory parity | edit-g02 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 16/25: first Return advances; Ctrl+Shift+H changes readback; continuous replace-all undo agrees. |
| Edge: wrapped-wheel distance/reversal | edit-zw9 | FIXED retained, with task-24 notice-priority assessment corrected separately | Task 23 runtime registration; no failed final Sublime frame used as proof. |
| Edge: renderer error near normal ending | edit-cee | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 19/29/30: stall survives, saves work, repeated normal endings; no error -11 in saved editor logs. GL NOT OBSERVED. |
| Edge: unsupported daily file commands | edit-o20 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 22/24 complete file workflow. |
| Edge: first-Return difference | edit-g02 | FIXED retained, with task-24 notice-priority assessment corrected separately | Task 25 position sequence. |
| Edge: Ctrl+Shift+H habit difference | edit-g02 | FIXED retained, with task-24 notice-priority assessment corrected separately | Continuous task-25 readback hashes agree. |
| Edge: gutter selection difference | edit-dr3 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 15/28: runtime whole-line click and newline copy; drag comparison is explicitly unequal. |
| Edge: duplicate path tabs | edit-97i | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 18/26: absolute, relative and aliases preserve existing runtime edited view/undo. |
| Edge: emoji font gap | edit-r8e | CHANGED — missing box resolved by monochrome artwork; colour remains absent and waived | 20/34 |
| Edge: comparison limits | no bead | CHANGED — repaired where labelled; remaining gaps explicit | capture ledger / task rows |
| Edge: estimate-to-exact label switch | edit-xzw / edit-xkd | STILL PRESENT coverage gap; no wrong final label observed | 31 |
| Verdict blocker: missing New/Open/Save As/Quit | edit-o20 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 22/24. |
| Verdict blocker: incorrect wrapped-wheel scrolling | edit-zw9 | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 15/23. |
| Verdict blocker: missing emoji artwork | edit-r8e | CHANGED — missing box resolved by monochrome artwork; colour remains absent and waived | 20/34 |
| Verdict blocker: unexplained renderer exit near close | edit-cee | FIXED retained, with task-24 notice-priority assessment corrected separately | Tasks 29/30 deliberately reproduce the relevant stall/close conditions. |
| New: Rust number/operator colour roles | edit-11v | STILL PRESENT — number/operator role mismatch | 27 |
| New coverage: journal atomicity / orphan session / small landings | edit-pgr / edit-rzj / edit-bml / edit-1vo / edit-r1l | STILL PRESENT passing finite probes | 32/33 |
| Missing feature: find in files | edit-xkd | STILL PRESENT; waived for M0 | 16 |
| Missing feature: complete emoji artwork | edit-r8e | CHANGED monochrome coverage; colour artwork still missing, waived | 20/34 |
| Missing feature: presentation parity | edit-1nm / edit-11v | STILL PRESENT minimap/geometry and Rust role gaps | 1/13/27 |
| Edge: missing artwork | edit-r8e | CHANGED: standard supplied clusters now have monochrome artwork; colour waived | 20/34 |
| Edge: unavailable project search | edit-xkd | STILL PRESENT; waived | 16 |
| Edge: Rust colour classification | edit-11v | STILL PRESENT | 27 token table |
| Edge: PageDown placement | no bead | STILL PRESENT | 3 |
| Edge: external reload undo policy | no bead | STILL PRESENT | 19 clean repeat |
| Edge: changed-base recovery limit | edit-fi2 | STILL PRESENT; disk text and set-aside notice | 17 |
| Edge: large copied-profile startup | edit-fm4 / edit-rfq / edit-rzj | CHANGED: prompt window; large artifacts retained, documented deferral | 17/36 |
| Edge: Sublime gutter drag comparison | no bead | CHANGED comparator range result; no runtime regression inferred | 28 |
| Edge: coverage gaps | no bead | STILL PRESENT where explicitly listed; repaired mouse/loaded comparisons use named repeats | ledger / limitations |
| Historical verdict blocker: find in files | edit-xkd | STILL PRESENT finding only — Tobias waiver 2026-10-13 | 16 |
| Historical verdict blocker: emoji artwork | edit-r8e | CHANGED monochrome coverage; colour finding only — Tobias waiver 2026-10-13 | 20/34 |
| New: unchanged dense-session relaunch exits -103 | edit-28u test scope; cause unestablished | NEW BLOCKER, reproduced from copied journal | 37 |
| Newly reported: Save As failure notice hides Find | edit-o20 | CHANGED assessment; also visible in drive-4 evidence | 24 |
| New: invalid whitespace value has no visible diagnostic | edit-hgd | NEW finding; fallback itself works | 35 |

## Missing features Tobias would hit in daily use

1. Find in files remains unavailable (edit-xkd). It is waived for M0.
2. Colour emoji artwork remains unavailable in sublimité (edit-r8e), while the supplied standard clusters now have monochrome glyphs. Colour artwork is waived for M0.
3. Presentation parity remains incomplete: monochrome minimap, larger default grid/window and delayed first syntax colour (edit-1nm / edit-11v). Rust number/operator colours still violate the stated roles.

Deferred oversized sessions require manual recovery in this staged build. Recoverable closed tabs/lazy restoration are a documented limit, not a defect asserted by this drive.

## Edge cases and defects

- **NEW blocker — unchanged dense64 hot exit cannot reopen (edit-28u test scope; root cause unestablished).** Open /tmp/edit-corpus/dense_short.txt in private state, type x, sample the prescribed interval, type y, close normally, then launch with no file and the same state: exit 1, error -103, no window. A fresh private directory containing an exact copy of the retained journal under its original discoverable name reproduces it. The corpus stat/bytes remain unchanged. The original journal is preserved at `state/37-checkpoint-growth-dense64-sublimite/sublimite/session-oAHqSe.set-aside-1792092780488563967`; `logs/37-restart-fixture.json` gives the copy/hash. To reproduce again, copy that artifact to a fresh profile’s sublimite/session-oAHqSe before launching; the failed launches have already moved their discoverable source aside. This is new coverage, not a demonstrated drive-4 regression on the same recovery case.
- **Pre-existing notice-priority defect — edit-o20.** Save As into a nonexistent directory, then select a word and open Find: the stale failure notice covers the query row. Opening/closing the path prompt reveals the notice again. The saved drive-4 frame contains the same defect, despite its earlier favourable assessment.
- **Rust role mismatch — edit-11v.** Open config.rs: literal 1 at 96:11 is keyword/constant purple; assignment at 15:32 is plain foreground. The exact rectangles/RGBs are in task 27.
- **External reload undo policy — no bead.** Save and externally append, then Undo: sublimité keeps reloaded text; the valid Sublime repeat undoes the reload.
- **Page placement — no bead.** Replay inherited PageDown/PageUp navigation: viewport tops differ while caret navigation remains functional.
- **Changed-base recovery limit — edit-fi2.** Kill with unsaved text, rewrite the base, relaunch: disk text opens with a set-aside notice; edits are not automatically reapplied.
- **Invalid whitespace diagnostic not visible — edit-hgd.** Reload trailing="bogus": markers turn off and the app survives, but neither the sampled UI nor stderr supplies the expected user-facing diagnostic.

Coverage limits remain: GL rendering/stalls, an identifiable estimate→exact flag transition, proof that a kill landed mid-drain, optional additional language grammars, every individual queued key’s own paint, some native-dialog result onsets, and exact Sublime emoji cell ratios in the occluded caret captures. Failed/blank/occluded attempts are not passes. All required task families were driven; each incomplete observation is qualified in its task rows.

## Latency and jank by feel plus numbers

The reviewed typing sequences produce complete text. The paired small Z is later in sublimité; paired large Y/Z responses are similar in these single observations. Initial C/Rust colouring visibly follows plain text. Early large-file navigation groups inputs before the first near-end paint; settled paging is more immediate. The original accumulated Sublime session and some native dialogs produced invalid/blank captures, so those are excluded from speed claims. No targeted old orange minimap pulse is detected. The dense-session startup failure, not a provisional timing or either waiver, determines the verdict.

### Video measurement inventory

| Capture / action | Command envelope → observed frame (s) | Intervals / ms | Stamp / source |
|---|---|---|---|
| 01-fresh-sublimite / startup | 1.5049703121185303–1.6082763671875 → 1.65 | 8.70 / 145.0 | (M)[AC], BAT0=Full, AC/online=1, load1=0.51; logs/startup-metrics.json; ROI [122, 48, 80, 29] |
| 01-fresh-sublime / startup | 0.7781171798706055–1.0974652767181396 → 1.2666666666666666 | 29.31 / 488.5 | (M)[AC], BAT0=Full, AC/online=1, load1=0.90; logs/startup-metrics.json; ROI [510, 64, 90, 11] |
| 02-typing-r2-sublimite / glyph | 4.91921854019165–4.924621343612671 → 4.966666666666667 | 2.85 / 47.4 | (M)[AC], BAT0=Full, AC/online=1, load1=0.90; logs/typing-metrics.json; ROI [40, 46, 20, 27] |
| 02-typing-r2-sublime / glyph | 5.022214651107788–5.028625249862671 → 5.05 | 1.67 / 27.8 | (M)[AC], BAT0=Full, AC/online=1, load1=0.99; logs/typing-metrics.json; ROI [59, 63, 7, 11] |
| 10-large-sublimite / Y | 58.97147822380066–58.97949147224426 → 59.016666666666666 | 2.71 / 45.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; logs/large-metrics-sublimite.json; ROI [160, 46, 20, 27] |
| 10-large-sublimite / Z | 63.7928569316864–63.79983162879944 → 63.85 | 3.43 / 57.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; logs/large-metrics-sublimite.json; ROI [160, 46, 20, 27] |
| 10-large-sublimite / end-navigation | 47.34802746772766–47.388707399368286 → 47.416666666666664 | 4.12 / 68.6 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; logs/large-metrics-sublimite.json; ROI [0, 46, 140, 27] |
| 10-large-sublimite / startup | 0.7611029148101807–0.8831179141998291 → 0.9 | 8.33 / 138.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03; logs/large-metrics-sublimite.json; ROI [180, 46, 100, 27] |
| 24-files-r2-sublimite / new | 2.1383092403411865–2.1702635288238525 → 2.2 | 3.70 / 61.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [300, 0, 360, 40] |
| 24-files-r2-sublimite / open-b-prompt | 3.627553701400757–3.659808874130249 → 3.6833333333333336 | 3.35 / 55.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / open-missing-prompt | 5.989698648452759–6.0210182666778564 → 6.05 | 3.62 / 60.3 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / cancel-prompt | 9.195879459381104–9.233247756958008 → 9.25 | 3.25 / 54.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / save-untitled-prompt | 12.609161138534546–12.655480146408081 → 12.666666666666666 | 3.45 / 57.5 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / save-copy-prompt | 17.08741855621338–17.150907516479492 → 17.166666666666668 | 4.75 / 79.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / save-missing-prompt | 21.109747886657715–21.161396741867065 → 21.166666666666668 | 3.42 / 56.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-r2-sublimite / open-over-find | 28.05308198928833–28.087043285369873 → 28.116666666666667 | 3.82 / 63.6 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16; logs/file-command-metrics.json; ROI [0, 1200, 1560, 40] |
| 24-files-sublime / new | 2.6081655025482178–2.648597240447998 → 2.95 | 20.51 / 341.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [200, 27, 210, 33] |
| 24-files-sublime / open-b-prompt | 4.818621635437012–4.853243589401245 → 5.0 | 10.88 / 181.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / open-missing-prompt | 8.356874704360962–8.40675663948059 → 8.616666666666667 | 15.59 / 259.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / cancel-prompt | 12.835839986801147–12.874009847640991 → 13.133333333333333 | 17.85 / 297.5 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / save-untitled-prompt | 17.879269123077393–17.94832468032837 → 18.266666666666666 | 23.24 / 387.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / save-copy-prompt | 23.45979595184326–23.546090841293335 → 23.766666666666666 | 18.41 / 306.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / save-missing-prompt | 28.50038456916809–28.566269874572754 → 28.783333333333335 | 16.98 / 282.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 24-files-sublime / open-over-find | 35.78597664833069–35.827261447906494 → 35.983333333333334 | 11.84 / 197.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL; logs/file-command-metrics.json; ROI [0, 80, 600, 60] |
| 27-syntax-c-r2-sublimite / first-text | 0.7150347232818604–0.8500924110412598 → 0.9 | 11.10 / 185.0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; logs/syntax-metrics.json; ROI [100, 48, 140, 28] |
| 27-syntax-c-r2-sublimite / first-colour | 0.7150347232818604–not separately logged → 1.85 | 68.10 / 1135.0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; logs/syntax-metrics.json; ROI [80, 40, 1400, 1160] |
| 27-syntax-c-r2-sublimite / comment-colour | 3.790332078933716–3.7968268394470215 → 3.85 | 3.58 / 59.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; logs/syntax-metrics.json; ROI [80, 520, 1000, 40] |
| 27-syntax-c-r2-sublimite / undo-colour | 6.383111953735352–6.43464207649231 → 6.466666666666667 | 5.01 / 83.6 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; logs/syntax-metrics.json; ROI [80, 520, 1000, 40] |
| 27-syntax-c-r2-sublimite / Z | 15.128955364227295–15.138793230056763 → 15.183333333333334 | 3.26 / 54.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84; logs/syntax-metrics.json; ROI [80, 46, 20, 27] |
| 27-syntax-rs-r2-sublimite / first-text | 0.7451424598693848–0.8122994899749756 → 0.85 | 6.29 / 104.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [160, 48, 220, 28] |
| 27-syntax-rs-r2-sublimite / first-colour | 0.7451424598693848–not separately logged → 1.9833333333333334 | 74.29 / 1238.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [80, 40, 1400, 1160] |
| 27-syntax-rs-r2-sublimite / comment-colour | 3.4029040336608887–3.4094078540802 → 3.45 | 2.83 / 47.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [80, 240, 1000, 80] |
| 27-syntax-rs-r2-sublimite / undo-colour | 5.789483308792114–5.820751190185547 → 5.85 | 3.63 / 60.5 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [80, 240, 1000, 80] |
| 27-syntax-rs-r2-sublimite / Z | 13.941561222076416–13.947237730026245 → 13.983333333333333 | 2.51 / 41.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [80, 46, 20, 27] |
| 27-syntax-c-sublime / first-text | 0.8675873279571533–1.1167614459991455 → 1.0833333333333333 | 12.94 / 215.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [80, 64, 110, 11] |
| 27-syntax-c-sublime / first-colour | 0.8675873279571533–not separately logged → 1.0833333333333333 | 12.94 / 215.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 60, 600, 500] |
| 27-syntax-c-sublime / comment-colour | 3.814603328704834–3.8201639652252197 → 3.8666666666666667 | 3.12 / 52.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 276, 600, 18] |
| 27-syntax-c-sublime / undo-colour | 6.582268953323364–6.632265329360962 → 6.633333333333333 | 3.06 / 51.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 276, 600, 18] |
| 27-syntax-c-sublime / Z | 15.475553512573242–15.490453004837036 → 15.5 | 1.47 / 24.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 64, 9, 11] |
| 27-syntax-rs-sublime / first-text | 0.7309684753417969–0.9510171413421631 → 0.9333333333333333 | 12.14 / 202.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [110, 64, 180, 11] |
| 27-syntax-rs-sublime / first-colour | 0.7309684753417969–not separately logged → 0.9333333333333333 | 12.14 / 202.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 60, 600, 500] |
| 27-syntax-rs-sublime / comment-colour | 3.823864459991455–3.8337597846984863 → 3.85 | 1.57 / 26.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 150, 600, 36] |
| 27-syntax-rs-sublime / undo-colour | 6.721161127090454–6.764071226119995 → 6.783333333333333 | 3.73 / 62.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 150, 600, 36] |
| 27-syntax-rs-sublime / Z | 16.306560516357422–16.318174362182617 → 16.35 | 2.61 / 43.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL; logs/syntax-metrics.json; ROI [70, 64, 9, 11] |
| 02-paired-sublimite / Z | 3.2459475994110107–3.257154703140259 → 3.3 | 3.24 / 54.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.66; logs/paired-metrics.json; ROI [40, 46, 20, 27] |
| 02-paired-sublime / Z | 3.0248143672943115–3.02937912940979 → 3.05 | 1.51 / 25.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.39 — timing PROVISIONAL; logs/paired-metrics.json; ROI [61, 64, 9, 12] |
| 10-paired-sublimite / Y | 5.843388080596924–5.8486549854278564 → 5.883333333333333 | 2.40 / 39.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04 — timing PROVISIONAL; logs/paired-metrics.json; ROI [160, 46, 20, 27] |
| 10-paired-sublimite / Z | 8.127720594406128–8.133096933364868 → 8.166666666666666 | 2.34 / 38.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04 — timing PROVISIONAL; logs/paired-metrics.json; ROI [160, 46, 20, 27] |
| 10-paired-sublime / Y | 4.44408917427063–4.44888973236084 → 4.483333333333333 | 2.35 / 39.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.03; logs/paired-metrics.json; ROI [106, 559, 9, 17] |
| 10-paired-sublime / Z | 6.794503688812256–6.804333925247192 → 6.833333333333333 | 2.33 / 38.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.03; logs/paired-metrics.json; ROI [106, 63, 9, 13] |
| 10-paired-sublimite / end-navigation | 3.5751285552978516–3.606401205062866 → 3.6333333333333333 | 3.49 / 58.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04 — timing PROVISIONAL; logs/paired-metrics.json; ROI [0, 46, 140, 27] |
| 10-paired-sublime / end-navigation | 2.1549153327941895–2.186703681945801 → 2.333333333333333 | 10.71 / 178.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.03; logs/paired-metrics.json; ROI [10, 559, 85, 18] |
| 10-large-loaded-sublime / Y | 11.566228–11.571902 → 11.6 | 2.03 / 33.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.78; logs/late-metrics.json; ROI [106, 559, 10, 18] |
| 10-large-loaded-sublime / Z | 15.977400–15.983484 → 16.016666666666666 | 2.36 / 39.3 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.78; logs/late-metrics.json; ROI [106, 63, 10, 13] |
| 16-find-large-wait-sublimite / rare-match | 8.368727–8.390480 → 12.3 | 235.88 / 3931.3 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.79; logs/late-metrics.json; ROI [0, 42, 1200, 80] |
| 16-find-large-wait-sublime / rare-match | 4.614101–4.633590 → 9.433333333333334 | 289.15 / 4819.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33; logs/late-metrics.json; ROI [140, 290, 150, 25] |

| Ready-response group | Valid samples / driven samples | Interval range / ms range | Stamp / source |
|---|---|---|---|
| 16-find-large-sublimite / query- | 22 / 22 | 2.64–4.26 / 43.9–71.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.14; logs/late-metrics.json, thirty-frame sequences |
| 16-find-large-wait-sublime / f3- | 20 / 20 | 1.47–9.73 / 24.5–162.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33; logs/late-metrics.json, thirty-frame sequences |
| 16-find-large-wait-sublime / query- | 22 / 22 | 1.36–2.39 / 22.7–39.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33; logs/late-metrics.json, thirty-frame sequences |
| 16-find-large-wait-sublimite / f3- | 20 / 20 | 0.31–4.46 / 5.1–74.3 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.79; logs/late-metrics.json, thirty-frame sequences |

Query analysis uses all 22 characters of the literal ABSENT_ASTRA_987654321 (count from that input string). An initial analysis accidentally paired the last 21 commands with the first 21 shots; its zero masks were rejected and preserved separately. The corrected mapping changes no driven input. The rare Sublime match is visible at the cited video onset even though the OCR stop observer missed a wrapped continuation at the top.

### Quoted astra4 reference measurements

Loads/profile/index state differ; these are context, not controlled regression rankings.

| Capture / action | Intervals / ms | Recorded stamp / source |
|---|---|---|
| 01-fresh-sublimite / startup | 8.25 / 137.5 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.39; build/astra4/logs/startup-metrics.json |
| 01-fresh-sublime / startup | 15.17 / 252.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.48; build/astra4/logs/startup-metrics.json |
| 02-typing-r2-sublimite / glyph | 3.03 / 50.5 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.52; build/astra4/logs/typing-metrics.json |
| 02-typing-r2-sublime / glyph | 1.98 / 33.0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.77; build/astra4/logs/typing-metrics.json |
| 10-large-sublimite / Y | 3.30 / 55.0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.29; build/astra4/logs/large-metrics-sublimite.json |
| 10-large-sublimite / Z | 2.56 / 42.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.29; build/astra4/logs/large-metrics-sublimite.json |
| 10-large-sublimite / end-navigation | 3.30 / 55.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.29; build/astra4/logs/large-metrics-sublimite.json |
| 10-large-sublimite / startup | 9.29 / 154.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.29; build/astra4/logs/large-metrics-sublimite.json |
| 16-find-large-wait-sublimite / rare-match | 655.85 / 10930.8 | (M)[bat], BAT0=Discharging, AC/online=0, load1=6.87; build/astra4/logs/late-metrics.json |
| 16-find-large-stable-sublime / rare-match | 1860.16 / 31002.7 | (M)[bat], BAT0=Discharging, AC/online=0, load1=10.28 — timing PROVISIONAL; build/astra4/logs/late-metrics.json |

### Quoted astra3 reference measurements

Loads/profile/index state differ; these are context, not controlled regression rankings.

| Capture / action | Intervals / ms | Recorded stamp / source |
|---|---|---|
| 01-fresh-sublimite / startup | 10.18 / 169.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.43; build/astra3/logs/startup-metrics.json |
| 01-fresh-sublime / startup | 29.16 / 486.0 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.48; build/astra3/logs/startup-metrics.json |
| 02-typing-r2-sublimite / glyph | 1.75 / 29.2 | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.87; build/astra3/logs/typing-metrics.json |
| 02-typing-r2-sublime / glyph | 2.01 / 33.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.12; build/astra3/logs/typing-metrics.json |
| 10-large-sublimite / Y | 2.27 / 37.9 | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.26 — timing PROVISIONAL; build/astra3/logs/large-metrics-sublimite.json |
| 10-large-sublimite / Z | 1.78 / 29.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.26 — timing PROVISIONAL; build/astra3/logs/large-metrics-sublimite.json |
| 10-large-sublimite / end-navigation | 16.04 / 267.4 | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.26 — timing PROVISIONAL; build/astra3/logs/large-metrics-sublimite.json |
| 10-large-sublimite / startup | 12.78 / 213.1 | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.26 — timing PROVISIONAL; build/astra3/logs/large-metrics-sublimite.json |
| 16-find-large-wait-sublimite / rare-match | 281.62 / 4693.7 | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.52; build/astra3/logs/late-metrics.json |
| 16-find-large-complete-sublime / rare-match | 577.13 / 9618.8 | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.50; build/astra3/logs/late-metrics.json |

### Paired observation timing

02-paired: capture starts 6.399 s apart (within one minute); `video/02-paired-sublimite.mkv` (probe 4.259s); (M)[AC], BAT0=Not charging, AC/online=1, load1=6.66; exit=0; `video/02-paired-sublime.mkv` (probe 4.030s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.39; timings PROVISIONAL (load1 > 8). Per-action intervals are in the measurement inventory.

10-paired: capture starts 11.777 s apart (within one minute); `video/10-paired-sublimite.mkv` (end-edit 6.849s; first-edit 9.134s); (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04; timings PROVISIONAL (load1 > 8); exit=0; `video/10-paired-sublime.mkv` (end-edit 5.450s; first-edit 7.805s); (M)[AC], BAT0=Not charging, AC/online=1, load1=7.03. Per-action intervals are in the measurement inventory.

### Literal trace blocks

Counts are printed as recorded, including the driven workload and startup sampling; they are not rewritten into isolated-character populations. G1 is the specified typing guard; G3/T6 are diagnostic. Internal first_present and video first-matching-text are different measurements.

`traces/02-typing-r2-sublimite.txt` — (M)[AC], BAT0=Full, AC/online=1, load1=0.90

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=27649762207898 elapsed_ms=0.000000
  display_connect      ns=27649810750789 elapsed_ms=48.542891
  egl_probe_start      ns=27649845103645 elapsed_ms=82.895747
  egl_probe_end        ns=27649845522982 elapsed_ms=83.315084
  raster_fallback      ns=27649819176602 elapsed_ms=56.968704
  font_atlas_ready     ns=27649806587952 elapsed_ms=44.380054
  file_prefix_read     ns=27649845363446 elapsed_ms=83.155548
  first_layout         ns=27649845124843 elapsed_ms=82.916945
  first_submit         ns=27649848160227 elapsed_ms=85.952329
  first_present        ns=27649848728696 elapsed_ms=86.520798
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=92 p50=1.571 ms p99=56.968 ms
G3 T5-T0     n=91 p50=1.747 ms p99=18.653 ms
T6-T0        n=91 p50=20.263 ms p99=33.851 ms
battery: Full
```

Quoted drive 4, `build/astra4/traces/02-typing-r2-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=0.52

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=39631175550417 elapsed_ms=0.000000
  display_connect      ns=39631216700890 elapsed_ms=41.150473
  egl_probe_start      ns=39631251354930 elapsed_ms=75.804513
  egl_probe_end        ns=39631251852060 elapsed_ms=76.301643
  raster_fallback      ns=39631221960623 elapsed_ms=46.410206
  font_atlas_ready     ns=39631213013313 elapsed_ms=37.462896
  file_prefix_read     ns=39631251705442 elapsed_ms=76.155025
  first_layout         ns=39631251439346 elapsed_ms=75.888929
  first_submit         ns=39631254774759 elapsed_ms=79.224342
  first_present        ns=39631255383654 elapsed_ms=79.833237
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=93 p50=1.597 ms p99=46.409 ms
G3 T5-T0     n=92 p50=1.777 ms p99=17.304 ms
T6-T0        n=92 p50=18.147 ms p99=32.190 ms
battery: Not charging
```

`traces/27-syntax-c-r2-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=32027573550179 elapsed_ms=0.000000
  display_connect      ns=32027644920532 elapsed_ms=71.370353
  egl_probe_start      ns=32027702238354 elapsed_ms=128.688175
  egl_probe_end        ns=32027702619056 elapsed_ms=129.068877
  raster_fallback      ns=32027661238956 elapsed_ms=87.688777
  font_atlas_ready     ns=32027640370337 elapsed_ms=66.820158
  file_prefix_read     ns=32027703122042 elapsed_ms=129.571863
  first_layout         ns=32027702475950 elapsed_ms=128.925771
  first_submit         ns=32027706602284 elapsed_ms=133.052105
  first_present        ns=32027709846015 elapsed_ms=136.295836
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=12 p50=2.898 ms p99=87.688 ms
G3 T5-T0     n=11 p50=4.914 ms p99=33.888 ms
T6-T0        n=11 p50=27.300 ms p99=63.207 ms
battery: Not charging
```

Quoted drive 4, `build/astra4/traces/27-syntax-c-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=3.22

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=43811899638808 elapsed_ms=0.000000
  display_connect      ns=43811942837073 elapsed_ms=43.198265
  egl_probe_start      ns=43811967805651 elapsed_ms=68.166843
  egl_probe_end        ns=43811970855044 elapsed_ms=71.216236
  raster_fallback      ns=43811947533258 elapsed_ms=47.894450
  font_atlas_ready     ns=43811939928332 elapsed_ms=40.289524
  file_prefix_read     ns=43811970941772 elapsed_ms=71.302964
  first_layout         ns=43811970691832 elapsed_ms=71.053024
  first_submit         ns=43811974080456 elapsed_ms=74.441648
  first_present        ns=43811975084042 elapsed_ms=75.445234
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=12 p50=2.318 ms p99=47.894 ms
G3 T5-T0     n=11 p50=3.647 ms p99=31.871 ms
T6-T0        n=11 p50=26.500 ms p99=44.205 ms
battery: Not charging
```

`traces/35-whitespace-default-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=2.77

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=31089410304784 elapsed_ms=0.000000
  display_connect      ns=31089443819109 elapsed_ms=33.514325
  egl_probe_start      ns=31089472574072 elapsed_ms=62.269288
  egl_probe_end        ns=31089472743384 elapsed_ms=62.438600
  raster_fallback      ns=31089449133254 elapsed_ms=38.828470
  font_atlas_ready     ns=31089440717393 elapsed_ms=30.412609
  file_prefix_read     ns=31089472767427 elapsed_ms=62.462643
  first_layout         ns=31089472642440 elapsed_ms=62.337656
  first_submit         ns=31089475768930 elapsed_ms=65.464146
  first_present        ns=31089476660293 elapsed_ms=66.355509
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=12 p50=1.505 ms p99=38.828 ms
G3 T5-T0     n=11 p50=1.578 ms p99=6.977 ms
T6-T0        n=11 p50=17.837 ms p99=26.521 ms
battery: Not charging
```

No drive-4 whitespace-typing trace counterpart.

`traces/10-large-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=28317840355765 elapsed_ms=0.000000
  display_connect      ns=28317888501409 elapsed_ms=48.145644
  egl_probe_start      ns=28317925886741 elapsed_ms=85.530976
  egl_probe_end        ns=28317926230756 elapsed_ms=85.874991
  raster_fallback      ns=28317897119409 elapsed_ms=56.763644
  font_atlas_ready     ns=28317885344889 elapsed_ms=44.989124
  file_prefix_read     ns=28317928112691 elapsed_ms=87.756926
  first_layout         ns=28317926010440 elapsed_ms=85.654675
  first_submit         ns=28317932860252 elapsed_ms=92.504487
  first_present        ns=28317933722344 elapsed_ms=93.366579
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=27 p50=5.120 ms p99=56.763 ms
G3 T5-T0     n=26 p50=7.237 ms p99=15.226 ms
T6-T0        n=26 p50=26.303 ms p99=37.169 ms
battery: Not charging
```

Quoted drive 4, `build/astra4/traces/10-large-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=3.29

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=40185714621221 elapsed_ms=0.000000
  display_connect      ns=40185771567791 elapsed_ms=56.946570
  egl_probe_start      ns=40185828592895 elapsed_ms=113.971674
  egl_probe_end        ns=40185828987482 elapsed_ms=114.366261
  raster_fallback      ns=40185779202594 elapsed_ms=64.581373
  font_atlas_ready     ns=40185764348921 elapsed_ms=49.727700
  file_prefix_read     ns=40185830621246 elapsed_ms=116.000025
  first_layout         ns=40185828617958 elapsed_ms=113.996737
  first_submit         ns=40185835232409 elapsed_ms=120.611188
  first_present        ns=40185836504189 elapsed_ms=121.882968
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=27 p50=2.477 ms p99=64.580 ms
G3 T5-T0     n=26 p50=3.925 ms p99=7.052 ms
T6-T0        n=26 p50=20.364 ms p99=30.634 ms
battery: Not charging
```

`traces/31-large-index-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=30628427475938 elapsed_ms=0.000000
  display_connect      ns=30628473976668 elapsed_ms=46.500730
  egl_probe_start      ns=30628512431135 elapsed_ms=84.955197
  egl_probe_end        ns=30628514535101 elapsed_ms=87.059163
  raster_fallback      ns=30628482434317 elapsed_ms=54.958379
  font_atlas_ready     ns=30628469949852 elapsed_ms=42.473914
  file_prefix_read     ns=30628516311633 elapsed_ms=88.835695
  first_layout         ns=30628514443785 elapsed_ms=86.967847
  first_submit         ns=30628519591999 elapsed_ms=92.116061
  first_present        ns=30628520490670 elapsed_ms=93.014732
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=20 p50=2.646 ms p99=380.847 ms
G3 T5-T0     n=19 p50=4.254 ms p99=383.339 ms
T6-T0        n=19 p50=22.737 ms p99=393.110 ms
battery: Not charging
```

Quoted drive 4, `build/astra4/traces/31-large-index-sublimite.txt` — (M)[AC], BAT0=Not charging, AC/online=1, load1=2.12

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=44127507567423 elapsed_ms=0.000000
  display_connect      ns=44127556960280 elapsed_ms=49.392857
  egl_probe_start      ns=44127589550493 elapsed_ms=81.983070
  egl_probe_end        ns=44127589880410 elapsed_ms=82.312987
  raster_fallback      ns=44127564019031 elapsed_ms=56.451608
  font_atlas_ready     ns=44127553796907 elapsed_ms=46.229484
  file_prefix_read     ns=44127591517564 elapsed_ms=83.950141
  first_layout         ns=44127589598197 elapsed_ms=82.030774
  first_submit         ns=44127595165546 elapsed_ms=87.598123
  first_present        ns=44127595990209 elapsed_ms=88.422786
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=21 p50=2.535 ms p99=295.195 ms
G3 T5-T0     n=20 p50=4.017 ms p99=296.787 ms
T6-T0        n=20 p50=19.899 ms p99=316.730 ms
battery: Not charging
```

## Capture and measurement audit

| Capture | Disposition / limits | Recorded endings | Stamp |
|---|---|---|---|
| 01-fresh-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=0.90 |
| 01-fresh-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=0.51 |
| 02-paced-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.37 |
| 02-paced-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.86 |
| 02-paced-verified-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.61 |
| 02-paced-verified-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.42 |
| 02-paced-verified-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.55 |
| 02-paired-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.39 — timing PROVISIONAL |
| 02-paired-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.66 |
| 02-typing-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=0.99 |
| 02-typing-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=0.90 |
| 03-nav-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=1.25 |
| 03-nav-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=1.15 |
| 03-selection-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.48 |
| 03-selection-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=1.63 |
| 04-mouse-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.48 |
| 04-mouse-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=2.36 |
| 05-edit-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.76 |
| 05-edit-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=2.82 |
| 05-edit-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.22 |
| 05-edit-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=2.40 |
| 06-alternate-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.28 |
| 06-alternate-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=9.92 — timing PROVISIONAL |
| 06-alternate-wait-sublime | No requested window; stopped wait | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.42 — timing PROVISIONAL |
| 06-clipboard-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.95 |
| 06-clipboard-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=3.02 |
| 06-cross-back-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.43 |
| 06-cross-back-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.11 |
| 06-cross-fullpath-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33 |
| 06-cross-source-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33 |
| 06-cross-source-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.08 |
| 06-cross-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.21 |
| 07-find-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Full, AC/online=1, load1=2.75 |
| 07-find-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Full, AC/online=1, load1=2.80 |
| 08-save-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.25 |
| 08-save-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.41 |
| 09-tabs-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.32 |
| 09-tabs-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.39 |
| 10-large-clean-sublime | Safe-mode/update dialogs; action result not certified | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.37 |
| 10-large-early-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=14.71 — timing PROVISIONAL |
| 10-large-loaded-noinput-sublime | stdin ended before navigation; no driven result | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.85 — timing PROVISIONAL |
| 10-large-loaded-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.78 |
| 10-large-sublime | Loading/blank action window; loaded repeat supersedes | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.90 — timing PROVISIONAL |
| 10-large-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.03 |
| 10-paired-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.03 |
| 10-paired-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.04 — timing PROVISIONAL |
| 11-resize-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.50 — timing PROVISIONAL |
| 11-resize-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.36 — timing PROVISIONAL |
| 12-hotexit-sublime | Black relaunch sample; recovery text not established | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.73 — timing PROVISIONAL |
| 12-hotexit-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.75 — timing PROVISIONAL |
| 12-recovery-conflict-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.47 — timing PROVISIONAL |
| 13-minimap-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.43 |
| 13-minimap-sublime | Loading/blank; clean repeat supersedes | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.32 — timing PROVISIONAL |
| 13-minimap-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.47 — timing PROVISIONAL |
| 14-idle-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.86 |
| 14-idle-sublime | Stale pixels; idle counts excluded | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.05 — timing PROVISIONAL |
| 14-idle-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.72 — timing PROVISIONAL |
| 15-autoscroll-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.19 |
| 15-autoscroll-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.02 — timing PROVISIONAL |
| 15-autoscroll-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.66 |
| 15-mouse-calibrated-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.27 |
| 15-mouse-calibrated-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.49 |
| 15-mouse-calibrated-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.29 — timing PROVISIONAL |
| 15-mouse-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.83 |
| 15-mouse-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.03 |
| 15-mouse-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=14.80 — timing PROVISIONAL |
| 15-mouse-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.39 — timing PROVISIONAL |
| 15-mouse-target-ready-r2-sublime | Empty selection readbacks; r3 supersedes | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.33 — timing PROVISIONAL |
| 15-mouse-target-ready-r3-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.06 |
| 15-mouse-wrapped-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.91 |
| 15-mouse-wrapped-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.88 |
| 15-mouse-wrapped-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.65 — timing PROVISIONAL |
| 15-mouse-wrapped-sublimite | Comparator overlay; isolated r2 supersedes | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=16.75 — timing PROVISIONAL |
| 16-find-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.23 |
| 16-find-large-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.14 |
| 16-find-large-wait-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.33 |
| 16-find-large-wait-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.79 |
| 16-find-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| 16-find-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.70 |
| 16-original-sequence-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=10.96 — timing PROVISIONAL |
| 16-original-sequence-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.73 |
| 17-profile-audit-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.34 |
| 17-recovery-a-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=9.04 — timing PROVISIONAL |
| 17-recovery-a-sublime | Client relaunch exits 1; clean repeat used | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.82 |
| 17-recovery-a-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.20 |
| 17-recovery-b-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.80 |
| 17-recovery-c-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.22 |
| 17-recovery-d-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30 |
| 17-recovery-e-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.30 |
| 18-tabtitle-second-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.47 |
| 18-tabtitle-second-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.34 |
| 18-tabtitle-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48 |
| 18-tabtitle-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.53 |
| 19-external-controlled-sublime | Reload prompt not ready; task-30 clean result used | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.60 — timing PROVISIONAL |
| 19-external-controlled-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.21 — timing PROVISIONAL |
| 19-external-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.67 — timing PROVISIONAL |
| 19-external-repro-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.88 |
| 19-external-sublime | Clipboard timeout/incomplete comparison | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.97 |
| 19-external-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.01 |
| 20-emoji-carets-sublime | Empty readbacks excluded; task-34 r2 bytes used | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.70 |
| 20-emoji-carets-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.84 |
| 20-emoji-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.05 |
| 20-emoji-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.85 |
| 20-font-restore-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98 |
| 21-font-file-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.82 |
| 21-font-iosevka-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98 |
| 21-font-liberation-mono-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.07 |
| 21-font-missing-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.89 |
| 21-font-proportional-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.14 |
| 22-common-attached-sublime | Supplemental normal quit attempt; immediate UI outcome not certified | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.42 |
| 22-common-commands-clean-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.95 |
| 22-common-commands-sublime | Fixture discovery incomplete; clean repeat used | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.85 |
| 22-common-commands-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.91 |
| 23-wrapped-keys-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.39 |
| 23-wrapped-keys-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.60 — timing PROVISIONAL |
| 23-wrapped-keys-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.13 |
| 23-wrapped-reverse-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.61 — timing PROVISIONAL |
| 23-wrapped-reverse-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.95 |
| 23-wrapped-reverse-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.06 — timing PROVISIONAL |
| 23-wrapped-reverse-sublimite | Comparator overlay; isolated r2 supersedes | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.99 |
| 24-files-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.16 |
| 24-files-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL |
| 24-files-sublimite | Rectangular overlay affects visual proof; disk data retained; r2 used | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.02 |
| 24-scroll-caret-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.78 |
| 24-scroll-caret-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.70 — timing PROVISIONAL |
| 24-scroll-caret-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.38 |
| 25-findkeys-continuous-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.80 — timing PROVISIONAL |
| 25-findkeys-continuous-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.65 |
| 25-findkeys-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.20 |
| 25-findkeys-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.84 — timing PROVISIONAL |
| 25-findkeys-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.05 |
| 26-reopen-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.79 |
| 26-reopen-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.59 — timing PROVISIONAL |
| 26-reopen-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.72 |
| 27-end-page-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15 |
| 27-end-page-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.82 — timing PROVISIONAL |
| 27-light-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.98 |
| 27-light-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.54 |
| 27-syntax-c-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.84 |
| 27-syntax-c-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=11.97 — timing PROVISIONAL |
| 27-syntax-c-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.98 |
| 27-syntax-rs-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.43 — timing PROVISIONAL |
| 27-syntax-rs-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL |
| 27-syntax-rs-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.19 |
| 27-tokens-c-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.32 |
| 27-tokens-c-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL |
| 27-tokens-c-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.47 |
| 27-tokens-rs-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=7.13 |
| 27-tokens-rs-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.63 — timing PROVISIONAL |
| 27-tokens-rs-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.27 |
| 28-gutter-cells-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.85 |
| 28-gutter-cells-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.48 — timing PROVISIONAL |
| 28-gutter-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.36 |
| 28-gutter-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.36 — timing PROVISIONAL |
| 28-gutter-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.34 |
| 29-stall-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.32 — timing PROVISIONAL |
| 29-stall-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.53 |
| 30-dirty-branch-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.14 — timing PROVISIONAL |
| 30-dirty-branch-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.13 |
| 30-external-x3-1-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.35 — timing PROVISIONAL |
| 30-external-x3-1-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.67 |
| 30-external-x3-2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.16 |
| 30-external-x3-3-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.54 |
| 31-early-end-only-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.15 |
| 31-large-index-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.89 — timing PROVISIONAL |
| 31-large-index-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| 31-large-loaded-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.53 |
| 31-large-queued-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.20 |
| 32-crash-two-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.51 |
| 32-crash-undo-0-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.18 |
| 32-crash-undo-1-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.72 |
| 32-crash-undo-2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.36 |
| 32-crash-undo-4-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=3.19 |
| 32-crash-undo-8-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=4.29 |
| 32-crash-untitled-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [-9, 0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.86 |
| 33-crlf-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.11 — timing PROVISIONAL |
| 33-crlf-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.79 |
| 33-glyphs-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.72 — timing PROVISIONAL |
| 33-glyphs-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.84 |
| 33-save-fail-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.87 — timing PROVISIONAL |
| 33-save-fail-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.56 |
| 34-emoji-r2-sublime | Sentinel-guarded byte results valid; update offer occludes some caret/artwork pixels | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.19 |
| 34-emoji-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=1.25 |
| 34-emoji-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=13.59 — timing PROVISIONAL |
| 34-emoji-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.85 |
| 34-fixture-r2-sublime | Sentinel-guarded byte results valid; update offer occludes some caret/artwork pixels | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.04 |
| 34-fixture-r2-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.16 |
| 35-whitespace-all-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.57 |
| 35-whitespace-default-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=12.86 — timing PROVISIONAL |
| 35-whitespace-default-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.77 |
| 35-whitespace-lines-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.71 |
| 35-whitespace-selection-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.48 |
| 36-orphan-fixture-sublimite | OLD-BINARY FIXTURE PRODUCER ONLY; small journal, fallback required | [-9] | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.71 |
| 36-orphan-repeat-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.30 |
| 36-orphan-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.24 |
| 37-checkpoint-growth-dense64-sublimite | NEW BLOCKER: normal close then relaunch error -103 | ['0 initial close', '1 relaunch (captured exception/stdio)'] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.36 |
| 37-checkpoint-growth-log1g-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49 |
| 37-dense-restart-check-sublimite | NEW BLOCKER reproduced from copied journal | [1] | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.49 |
| dense64-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.47 |
| dense64-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=9.37 — timing PROVISIONAL |
| font12-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=8.97 — timing PROVISIONAL |
| sublime-clean-dialog-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=0.99 |
| sublime-clean-setup-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.79 |
| utf8-fullpath-r2-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=2.73 |
| utf8-sublime | Earlier readiness limitation; full-path repeat used | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=15.65 — timing PROVISIONAL |
| utf8-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=14.08 — timing PROVISIONAL |
| zoom-sublime | Reviewed by task rows / measurement files; no broader claim than those rows | Client/shared process or uncollected kill status; see task log | (M)[AC], BAT0=Not charging, AC/online=1, load1=6.36 |
| zoom-sublimite | Reviewed by task rows / measurement files; no broader claim than those rows | [0] | (M)[AC], BAT0=Not charging, AC/online=1, load1=5.87 |

All completed recordings have ffmpeg trailers. Early inherited SIGKILL helpers did not reap every killed runtime child before replacing their handle; unavailable exit codes are stated rather than inferred. Later wrapper instrumentation records prior-child endings. All normal staged-runtime closes reviewed outside the new startup-failure case return zero; stderr tails are in the task JSON. No error -11 or BadWindow renderer ending is found.

Final offline binary/power/load verification (M)[AC], BAT0=Charging, AC/online=1, load1=3.31; no new UI capture at this time:

```text
2026-10-17T07:05:11+11:00
Charging
1
3.31 3.14 2.35 9/1726 7
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite
```

The full checkpoint log has the same staged size, mtime and SHA256 throughout. Final corpus hash still equals the brief’s log_1g hash; dense_short compares equal to the copied corpus. Oversized-copy cleanup and original-fixture audits are in logs/large-fixture-cleanup.json and old-fixtures-integrity.json.

VERDICT usable instead of Sublime for Tobias's daily edits: no; blockers: unchanged dense64 hot-exit session fails to reopen (error -103; task 37)
