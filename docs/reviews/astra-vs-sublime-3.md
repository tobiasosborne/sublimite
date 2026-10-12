# Astra vs Sublime Text: comparison drive 3

2026-10-12–13 · edit-w34.4 / P5.1b · Astra

**Not ready to replace Sublime for Tobias’s daily edits.** The drive-2 mouse, ordinary wheel, find/replace, and changed-base startup failures are fixed in the tested workflows. New tests expose incorrect wrapped-wheel movement, missing everyday file commands, and one unexplained renderer exit. The emoji cluster layout is repaired, but artwork available only through the color font remains missing.

**Build scope:** after all sublimité recordings ended, the executable changed to mtime **2026-10-13 01:02:21.395697174 +1000**, size **5,137,984 bytes** (M)[AC]. The last sublimité recording stopped at **2026-10-13T00:27:13.499340+10:00** (M)[AC]. BUILD was not updated. The verdict below applies to the original recorded executable, not this later replacement; see `logs/post-capture-binary-change.json`.

## Conditions and evidence conventions

All editor interaction and capture used **DISPLAY=:98 only**, never :0 or :99. The prior virtual display was unavailable. A restricted Xvfb launch failed to bind sockets; an escalated launch on :98 succeeded. `xdpyinfo` measured **2880×1800 pixels, 243×243 DPI** (M)[AC], `logs/xdpyinfo-escalated.txt`. No existing recorder was stopped. Renderer stderr reports EGL failure followed by raster fallback with no retry.

Driven binary: `stat -c '%y %s' build/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra3/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.

Each captured task logs BAT0 status, AC/online and /proc/loadavg before recording. All capture stamps are **(M)[AC]**, AC/online=1; battery status is Not charging. Recorded task-start load1 ranges from **0.43 to 31.80** (M)[AC], aggregated in `logs/run-index.json`. External clang/cc1/utf8_bench jobs were visible during high load; none were signalled. Late single observations are not a controlled regression comparison. Drive-2 quoted measurements have **unstamped power** and are reference points, not budgets. Counts/dwells/key-repeat sizes specified by the brief are input parameters, not pass/fail budgets.

The copied drive-2 scripts differ only by `build/astra2`→`build/astra3`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.

The /tmp corpus was absent. Existing `build/tools/mkcorpus` regenerated it; C and dense files compare byte-identical with drive-2 copies. Matched PRD/editor/UTF-8/navigation fixtures were copied from drive 2 so their contents remain identical. The new wrapped test uses a fresh PRD copy. C/dense/log sizes are **1,048,576 / 67,108,864 / 1,073,741,824 bytes** (M)[AC], stat/cmp in `logs/corpus.txt`. The log was opened in place, never copied or saved. Its SHA256 is `a259640b05b70487cd68a641913d8f2d8023d77f9bd1a5ba63beb555ceaae875`.

All video names below are relative to **build/astra3/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra3/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.

Capture uses the inherited x11grab/H.264 ultrafast/CRF23/yuv420p setup with four encoder threads. All interval counts are **60 Hz resampled intervals** (P: 1000/60 = 16.666… ms, displayed as 16.7 ms). They are single observations from logged command start to visible matching pixels, not population percentiles or physical keyboard-to-photon latency. Injection can occur anywhere inside the command envelope; capture gaps and scheduling limit rankings. Tracedump percentiles are separate. Stills establish text/layout, never absence of flashing or jank.

Measurement scripts retain crop coordinates, thresholds and sequences. Sparse gutter-only mean-error results in `new-onset-metrics.json` that matched before an action are **rejected**; wheel results use `action-onsets.json`’s changed-pixel mask. Panel results use `validated-metrics.json`. An odd-size YUV crop analysis error was corrected by converting to gray before cropping; it was not an editor failure. A no-movement-at-BOF wheel action has no response latency; a later action must not be attributed to it.

The standard Sublime large-file run stayed blank, and the first safe-mode run had startup dialogs. A fresh, explicitly labelled loaded clean session provides the successful comparison after dialog dismissal. Two execution sessions reported status 143 after completed Sublime captures; the sender/cause is unknown. A stale-window continuation is retained as invalid evidence, not counted as a pass. The loaded-session rare-search OCR stop observer missed a clipped gutter digit even after the highlighted match appeared; latency uses the actual video pixels.

## 1. Launch, glyphs, syntax and zoom

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Fresh C first matching text | 29.16 intervals / 29.16×16.7 ≈ 486.0 ms (M)[AC]. | 10.18 intervals / 10.18×16.7 ≈ 169.7 ms (M)[AC]. | **CHANGED** measured observations versus drive-2 quotes 521 / 627 ms (M, power unstamped). This fresh Sublime process displayed an update dialog; matching uses visible text to its right, not the dialog. No cold-process equivalence claim. | `01-fresh-sublime.mkv`: launch 0.747377 s, window found 1.026802 s, matching text 1.233333 s; ROI (510,64,90,11). `01-fresh-sublimite.mkv`: launch 0.763584 s, window found 0.889102 s, matching text 0.933333 s; ROI (122,48,80,29). All current timestamps (M)[AC]; `logs/startup-metrics.json`. |
| Default ink and window geometry | 11 px ink; 800×600 window (M)[AC]. | 26 px ink; 1728×1260 window (M)[AC]. | **STILL PRESENT** drive-2 presentation. C remains monochrome in sublimité versus syntax-colored Sublime. | `logs/font-metrics.json`: max RGB >130; sublimité ROI (140,42)–(235,82), ink rows 46–71; Sublime unobstructed task-4 launch ROI (91,60)–(132,80), rows 64–74 (M)[AC]. Geometry is xdotool output in task logs. Original task-1 Sublime view had an update dialog, so the unobstructed font sample is named explicitly. |
| Zoom chords | Enlarges twice, shrinks once; Ctrl+0 leaves that size. | Enlarges, shrinks and resets. | **CHANGED** from drive-2 Sublime observation; sublimité behavior retained. Native-size image comparison corrected the initial scaled-sheet interpretation. | `zoom-*.mkv`, larger/larger2/smaller/reset stills supplement video. |
| Font size override | Not applicable. | 32 px ink (M)[AC] under requested `EDIT_FONT_SIZE_PT=12`. | **STILL PRESENT** working override. | `font12-sublimite.mkv`; font still ROI (180,50)–(300,92), ink rows 55–86 (M)[AC], threshold as above. |
| C syntax colour | Syntax-coloured source. | Monochrome source. | **STILL PRESENT** presentation gap. | `01-fresh-sublimite.mkv` 6.193 s (M)[AC], `painted` frame; matched Sublime C frames. |

## 2. Typing and isolated glyph onset

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Verified paced paragraph | Exact expected paragraph in both repeats. | Exact expected paragraph. | **STILL PRESENT** ordered, complete input. | `02-paced-verified-*.mkv` and Sublime readiness-delayed `02-paced-verified-r2-sublime.mkv`; all `paragraph_match` fields true (M)[AC]. |
| Isolated Z onset | 2.01 intervals / 2.01×16.7 ≈ 33.4 ms (M)[AC]. | 1.75 intervals / 1.75×16.7 ≈ 29.2 ms (M)[AC]. | **CHANGED** individual observations versus drive-2 quotes 29.8 / 52.8 ms (M, power unstamped). The small difference is not a reliable speed ranking. | `02-typing-r2-sublime.mkv`: command 4.799905–4.805715 s, glyph 4.833333 s. sublimité: 4.737483–4.741780 s, glyph 4.766667 s (M)[AC]. `logs/typing-metrics.json`, thirty-frame sequences and `typing-30frames.png`. |
| Original --delay 100 and --delay 0 paragraph/burst | Complete text in order. | Complete text in order; deadline-paced full paragraph also matches clipboard. | **STILL PRESENT**, passing behavior retained. | `02-typing-r2-*.mkv`, slow/burst/home frames; `02-paced-verified-*.json` literal expected/actual paragraphs. The flags are inherited inputs, not measured throughput. |

## 3. Keyboard navigation and selection

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Held Shift selection | Selects `alp`. | Selects `alp`. | **STILL PRESENT** anchor retention. | `03-selection-*.mkv`, held-three frames; inherited three Right presses. |
| Arrows, Ctrl+arrows, Home/End, indented Home | Moves by character/word/line and indentation. | Corresponding caret stops and retained Shift anchor. | **STILL PRESENT**, passing keyboard behavior. | `03-nav-*.mkv`, numbered per-key frames; `navigation-contact.png`. |
| Page Down/Up and document ends | Pages and reaches both ends. | Pages and reaches both ends. View placement differs: sampled tops 33→34→33 versus Sublime 28→55→28 (M)[AC]. | **STILL PRESENT**: the same sublimité placements exist in the drive-2 raw frames; do not describe identical viewport motion. | `03-nav-*.mkv`, 13-Next/14-Next/15-Prior and end/home frames; `page-nav-compare.png` compares old/new frames without modifying old evidence. |

## 4. Original mouse sequence

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Original text click / drag / double-click | Caret/range/word respond. | Caret moves and drag selects. The original double-click target lands on punctuation in its larger cell grid. | **FIXED** inert mouse; finer word-rule comparison remains in task 15. | `video/04-mouse-{sublime,sublimite}.mkv`, named click/drag/double frames and command envelopes in logs. |
| Original wheel bursts | Scrolls. | First visible logical line 1 → 19 → 10 (M)[AC], read from wheel-before/down/up views: movement +18 and −9 rows (arithmetic). | **FIXED** inert wheel. Three rows per notch (M)[AC], from 18/6 and 9/3; these counts are input parameters, not budgets. | `video/04-mouse-sublimite.mkv`; `frames/04-mouse-sublimite-{launch,wheel-down,wheel-up}.png`. |
| Minimap click / drag | Moves viewport; first read gutter labels 141 and 15,678 (M)[AC], from the named native gutter crops. | First visible lines 4,739 and 6,675 (M)[AC], gutter reads. | **CHANGED** destinations versus drive-2 approximate 4,806 / 6,821 (quoted M, power unstamped); navigation works. | `video/04-mouse-sublimite.mkv`, minimap-click/drag frames. |

## 5. Editing and undo/redo

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Insert / undo / redo loop | Returns to baseline and restores digits. | Returns to baseline and restores digits. | **STILL PRESENT** passing behavior; this label does not imply a defect. Loop counts are inherited input parameters. | `video/05-edit-*.mkv`, `50-inserts`, `50-undo`, `50-redo` frames. |
| Select-all/delete, undo/redo, Ctrl+L, Ctrl+Backspace/Delete | Selection, deletion and restoration work. | Same logical results, including Ctrl+Y and the line-deletion repeat. | **STILL PRESENT**, passing edit/redo behavior. | `05-edit-*.mkv` and `05-edit-r2-*.mkv`, all/delete/undo/redo/line/word frames. |

## 6. Clipboard and interoperability

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Ctrl+C/X/V and alternate Insert/Delete bindings | Clipboard reads `COPYME`, then `COPYMECOPYME`. | Same exact clipboard strings. | **STILL PRESENT** successful ordinary/alternate clipboard paths. | `06-clipboard-*` and `06-alternate-*` videos; literal xclip output in task JSON. |
| PRIMARY middle-click at inherited pixel target | Inserts PRIMARY at its clicked position. | `secondPRIMARY_PASTE line` appears at the clicked position in the second line. | **STILL PRESENT** middle paste; left-click support has not broken it. The shared pixel is a different text location with the different font/chrome. | `06-clipboard-*.mkv`, middle frames; screenshot-derived text (M)[AC]. |
| Cross-editor round trip | Supplies and receives complete text. | Receives and exports complete text. | **STILL PRESENT** successful interoperability. | Both final xclip reads equal `FROM_SUBLIME\nFROM_SUBLIME\nFROM_SUBLIMITE` in `06-cross-sublimite.json` / `06-cross-back-sublime.json` (M)[AC]. |

## 7. Original find/replace sequence

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Original Ctrl+F → alpha → Return → Escape → F3 | Find panel and navigation. | Query is visibly in the Find row, document alpha selected; title remains clean. | **FIXED** missing find panel and visibly destructive query flow; full-buffer hash verification follows in task 16. | `video/07-find-sublimite.mkv`: panel shot 3.975 s, query shot 5.158 s, F3 result 6.724 s (M)[AC], log-derived timestamps. |
| Ctrl+H | Replace panel. | Persistent query/replacement/hints rows. | **FIXED** missing replace panel; replacement semantics checked separately. | Same video at 7.541 s (M)[AC], named replace-panel shot. |
| Full readback after the exact original search sequence | Hash equals untouched fixture. | Hash equals untouched fixture; no dirty title. | **FIXED** document mutation. | `16-original-sequence-*.mkv`; SHA256 `e50ddfdce3719854bda9cca11a40311ec9946634c11ec796a1c2ed1e7190022d` before/after, 12,898 bytes (M)[AC] from xclip readback. |
| Former Ctrl+F/F3/Ctrl+H notices | Panels/search controls. | No not-implemented feedback for these supported keys; panel replaces the old notice flow. | **FIXED** old notice-only behavior. | `07-find-*.mkv`; `logs/find-notice.json` and bottom-strip scan. Drive-2 notice durations were 4/13/16 frames (quoted M, power unstamped); current scan results are discussed below. |

## 8. Save and external append

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Ctrl+S then disk read | Saved prefix and clean title. | `EDITED_original on disk\nsecond line\n` read from disk. | **STILL PRESENT**, passing save behavior retained. | `08-save-sublimite.mkv`, cat at 4.843–4.846 s (M)[AC]. |
| External append after clean save | Reloads appended text. | Appended line visible without prompt, clean title. | **FIXED** clean-buffer prompt in drive 2; **ret behavior present** in this probe. | `08-save-sublimite.mkv`: append marker 5.114 s, settled screenshot 7.116 s (M)[AC]. These screenshot times bound observation; onset will use frames. |

## 9. CLI tabs and descriptor count

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| CLI stress and descriptor sharing | Captured; final read pending. | All 100 CLI opens return 0; one inotify descriptor (M)[AC]. | **STILL PRESENT** resolved drive-1 ceiling; no regression from drive 2. | `09-tabs-sublimite.mkv`, command/result pairs and `inotify_instances` event; count derived directly from logged commands and /proc descriptor symlinks. |
| CLI loop completion and cycle/close/reopen | All 100 commands returned 0 (M)[AC]; final reopened `tab-098` visible. | Same return codes and final tab text. | **STILL PRESENT** passing behavior; **CHANGED** Sublime comparison completeness: this drive has its final visible readback, unlike drive 2's interrupted dwell. | `09-tabs-*.mkv`, hundred / hundred-cycle / hundred-reopen frames, CLI result pairs. |
| Five CLI opens, Ctrl+Tab, Ctrl+W, Ctrl+Shift+T | Opens, cycles, closes and reopens. | Same; task owns the CLI endpoint by omitting --new-instance. | **STILL PRESENT**, passing small-tab workflow. | `09-tabs-*.mkv`, five-opened/tab1/tab2/close/reopen frames and command return codes. |

## 10. Large files

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Open log to first text | Standard session remained blank during its copied run; clean session eventually loaded after dialogs. | Paints file prefix; latency inventory below. | **STILL PRESENT** standard Sublime comparison limitation; **CHANGED** current sublimité latency sample. | `10-large-*.mkv`; separate `10-large-clean-sublime.mkv` and successful `10-large-loaded-sublime.mkv`. Internal first_present is in tracedump. |
| Ctrl+End, Page Up ×20, return to end | Loaded clean session reaches final line 8,947,842; first fully visible line after paging is 8,947,530, caret status 8,947,544 (M)[AC]. | Final line 8,947,842; paged view starts 8,947,542; subsequent end navigation still works (M)[AC]. | **STILL PRESENT**, successful navigation retained. Page distances/view placement differ. | `10-large-sublimite.mkv` and `10-large-loaded-sublime.mkv`; `logs/large-gutters.tsv` preserves per-step sample times and OCR, with source PNGs. |
| Y near end, Z at beginning; short-dwell repeat | Both glyphs appear in the loaded clean session. | Both glyphs appear in standard and short-dwell runs. | **STILL PRESENT** working edits; **CHANGED** latency samples. | Thirty-frame sequences and latency inventory; `10-large-early-sublimite.mkv`, trace retained separately. |
| Estimated labels and exact index | Exact final label once loaded. | Sampled end labels agree with wc-derived final logical line; no snap to a different EOF observed. | **STILL PRESENT** correct sampled labels. The once-only estimate→exact switch itself was not caught: even the short-dwell end view was already exact. | `logs/large-gutters.tsv`; corpus `wc -l` gives 8,947,841 newlines (M)[AC], plus final logical line = 8,947,842 (arithmetic). |
| Dense-short fixture end/home/Z | The inherited dense run shows text but stays at line 1; the requested end and Z are not visible. Cause is not established. | Reaches final line 26,845,451 and paints Z at the beginning (M)[AC]. | **STILL PRESENT** sublimité success; **CHANGED** Sublime comparison did not complete this time. | `dense64-*.mkv`, end/typed frames; fixture size 67,108,864 bytes (M)[AC], stat/cmp corpus log. |

## 11. Resize

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Inherited resize sizes and page keys | Reflows/repaints at requested sizes. | No persistent garbage/stale cells in settled views; source/minimap stay within the resized window. | **STILL PRESENT**, passing resize behavior. | `11-resize-*.mkv`, named 1440×900 / 2880×1800 / 800×600 / 960×600 requested-size frames; `logs/resize-tracking.json` and moving-strip scans. Sizes are inherited input parameters. |

## 12. Crash recovery and ordinary relaunch

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| SIGKILL, unchanged base, inherited dwell | Fixture relaunch shows `disk baseline`, no marker. | `UNSAVED_RECOVERY_MARKER_disk baseline` restored; dirty asterisk; on-disk baseline unchanged. | **STILL PRESENT** successful sublimité recovery and this Sublime test's non-recovery. | `12-hotexit-*.mkv`, before-kill / killed / relaunch frames and disk cat. The twelve-second dwell is an inherited input parameter; actual command times remain in each log. |
| SIGKILL with replaced base; ordinary-profile relaunch | Matched replaced-base test opens disk replacement. | Opens disk text with a set-aside notice instead of exit -103; unchanged ordinary unsaved relaunches restore dirty text. | **FIXED** both drive-2 startup blockers. | Original `12-recovery-conflict-sublimite.mkv` repeat plus complete cases in task 17; no launch-failure code inferred from successful window creation. |

## 13. Minimap and orange

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Minimap appearance, navigation and edits | Recognizable syntax-coloured miniature. | Finer monochrome striped representation; navigation works. No persistent stale map observed after the recorded edits. | **STILL PRESENT** presentation gap; cached publication did not introduce a visible persistent stale-map regression here. | `13-minimap-*.mkv`, page/end/home/edited frames. Tiny edits in the large C summary are not a pixel-perfect minimap correctness test. |
| Three-colour flash scan of every sublimité recording | Whole-suite scan not requested for Sublime. | Completed detector results listed below, including failed/interrupted app runs whose videos completed. | **STILL PRESENT** quiet behavior in the completed scans; see the literal per-video table. | `logs/<stem>.flash-<colour>.txt`; fixed minimap ROI and separately tracked resize/find-resize strips. |
| Other orange UI | Search and file dialogs/status messages. | R/K source-change prompt and unsupported-command notices are deliberate action feedback. Find query/replacement rows themselves are predominantly grey/white here. | **CHANGED** supported Ctrl+F/H no longer produce unsupported notices; deliberate notices remain for unsupported commands. | `19-external-*.mkv`, `16-find-sublimite.mkv` Ctrl+Shift+F, and `22-common-commands-sublimite.mkv`. These are not the old minimap pulse. |

## 14. Idle

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Focused untouched idle interval | No caret transitions; one status-text clearing event. | Twenty caret transitions, then stable; no non-caret changes. | **STILL PRESENT** expected sublimité idle behavior; **CHANGED** Sublime has a benign startup-status clearing event versus drive 2’s zero outside events. | `14-idle-*.mkv`; detailed metrics below. `idle-sublime-change.png` identifies Detect Indentation status clearing, not an editor-text anomaly. |

`14-idle-sublimite.mkv` idle markers **3.660619–63.661021 s**, difference **60.000401 s** (M)[AC]. Caret ROI `[75, 35, 45, 50]`; grayscale delta >30 and >20 changed pixels are the detector parameters. Results: **20 caret / 0 outside-region events** (M)[AC], `logs/idle-sublimite-metrics.json`.

`14-idle-sublime.mkv` idle markers **3.571372–63.572025 s**, difference **60.000654 s** (M)[AC]. Caret ROI `[45, 55, 50, 35]`; grayscale delta >30 and >20 changed pixels are the detector parameters. Results: **0 caret / 1 outside-region events** (M)[AC], `logs/idle-sublime-metrics.json`.

Sublimité’s expected twenty transitions are derived from EDITOR_IDLE_NS=10 s and a 500 ms phase (G, editor.h): 10/0.5=20 (arithmetic). Its recording agrees; identical-pixel repaints cannot be detected by video. Sublime’s outside event at **4.850 s** (M)[AC] is the Detect Indentation status clearing, located by `logs/idle-sublime-change.json`.

## 15. Mouse targets and wrapped/unwrapped wheel

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| C-file wheel bursts | Top lines 1 → 10 → 100 → 10 → 1; zero-delay burst reaches 91 (M)[AC]. | Same top lines (M)[AC]. | **FIXED** original inert wheel; **NEW** high-rate coverage. Three rows/notch from (10−1)/3 and (100−10)/30; burst movement 91−1=90 rows (arithmetic). | `15-mouse-*.mkv`, down3/down30/up30/up3/burst30 frames. |
| Wrapped PRD wheel, then reverse | Returns exactly to its prior view; the compared body ROI is pixel-identical (M)[AC]. | Initial three downward notches put logical line 4 at top, moving only five of the initially visible visual rows (M)[AC]: line 4 was at y=240, top was y=40, row pitch 40, so (240−40)/40=5. After thirty further down and thirty up, returns to line 1 instead of the prior line-4 view (M)[AC]. | **NEW defect:** wrapped wheel distance/reversal is inconsistent with three visual rows per notch. Reproduced with separate single-notch upward commands. | `15-mouse-wrapped-sublimite.mkv`, `15-wrapped-reverse-sublimite.mkv`; `wrapped-grid-detail.png`, base-down3/down30/up-01…up-30/reversed frames. |
| Gutter click | Selects a line. | Places caret at line start. | **NEW** mouse-rule divergence; ordinary text clicks work. | `15-mouse-*.mkv`, gutter frames; exact coordinates in target logs. |
| Calibrated click/drag/Shift+click, underscore word, triple click | Ready repeat selects exactly the same strings as sublimité, including fn_0 and the full line. | Caret at target line 3 column 10; drag to line 5 column 20, Shift+click to line 7 column 24 retains the original start (M)[AC]. | **FIXED** absent mouse operations; **NEW** exact target/readback evidence. | `15-mouse-calibrated-sublimite.mkv` and readiness-gated `15-mouse-target-ready-sublime.mkv`. Targets: sublimité (305,140), (505,220), (585,300); Sublime (171,105), (261,141), (297,177), from each logged cell grid (M)[AC]. Earlier wrong-grid/blank Sublime attempts are excluded from range comparisons. |
| Click past short line; gutter | Past-end click clamps to line end; gutter selects line. | Past-end click clamps to line end; gutter places caret at its beginning. | **NEW** gutter selection-policy divergence, not inert input. | Task-15 past-eol/gutter frames and clipboard range readbacks. Calibrated target line 2 contains only `{`; caret lands after it. |
| Wheel/click with find panel open | Panel remains open; text click changes caret. | Panel remains open; wheel scrolls and text click places caret at clicked text location. | **NEW** panel/mouse coexistence coverage. | `15-mouse-*.mkv` and wrapped variants, panel-wheel/panel-click frames, including bottom Find strip. |

The first new Sublime target grid was wrong, and its first calibrated repeat was blank/loading during click/drag. Neither is counted as a valid range comparison. The final readiness-gated repeat uses a grid read from its own still and gives byte-identical drag/Shift+click selections to sublimité. The separate wrapped reversal is conclusive: Sublime’s before/after text ROI has zero changed pixels; sublimité returns to a different view (M)[AC], `15-wrapped-reverse-*` frames.

## 16. Find/replace panel and large search

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Find fixture: query and navigation, then full clipboard hash | Hash equals fixture. | Hash equals fixture. | **FIXED** query-in-document blocker; **NEW** full-buffer proof. | `16-find-*.mkv`; initial/readback SHA256 `2e2fe191debe0b060f6733537ed08fc21db19bd75ceec351bb42795eac852e1d`. 30 case-insensitive alpha occurrences (M)[AC], counted from the fixture and readback. |
| Replace-one, replace-all, single undo | Counts 1 OMEGA / 29 alpha, then 30 / 0, then 1 / 29 (M)[AC]. | Same counts (M)[AC]. | **FIXED** absent replacement; **NEW** single undo-group proof. | `16-find-*.json` readback events; all hash `c6330b39904c9fa54b23759310df87074667994779833eb9de5b2af331f73b03`; one and undone-all hash `912b130f064c682399d95cf39f4cb9f30c5e730b5452c483740afac106396b01`. |
| Sublime replacement habit Ctrl+Shift+H | Replaces next. | No replacement; identical before/after hash. | **NEW** shortcut divergence; documented Ctrl+Enter works. | `16-find-sublimite.mkv`, sublime-habit frame and habit readback. |
| First Return after typing query | Advances from first to second occurrence. | Selects/commits first occurrence; next Return advances. | **NEW** navigation habit divergence. Subsequent F3/Shift+F3 navigate. | `16-find-*.mkv`, typed/nav-0/nav-1/nav-2/nav-3 frames. Sublimite ordinals 1→1→2→3→2 versus Sublime 1→2→3→4→3 (M)[AC], panel/selection reads. |
| Alt+C / Alt+W / Alt+R | Corresponding case, word and regex matches. | Counts change from 30 to 20, then 10; reset toggles plus regex `al.ha` restores 30 (M)[AC]. | **NEW**, working toggles; first-Return ordinal difference remains. | `16-find-*.mkv`, case/whole/regex frames. Counts are visible panel totals and fixture-derived. |
| Query editing, field ownership, small-window panel | Field edits/toggles work; no document mutation before replacement. | Backspace, arrows, Home/End and Ctrl+A operate on query; resizing keeps panel and text rows intact. | **NEW**, passing panel editing/layout coverage. | `16-find-*.mkv`, field-0…field-6, small-panel/restored-panel; matching post-replace-one hashes also rule out stray prior field edits in the document. |
| Ctrl+Shift+F | Find-in-files UI. | No find-in-files workflow; unsupported feedback. | **NEW** missing daily feature (no corresponding drive-2 row). | `16-find-*.mkv`, find-in-files action; video rather than the delayed still determines any brief notice. |
| Rare term from final log bytes; ready F3 navigation | Extended clean session visibly finds ef0581d638af781d near EOF; ready navigation recorded separately from initial pending searches. | Extended run finds the same term at line 8,947,839; ready F3 loop advances through the common matches (M)[AC]. | **NEW**, large-search functionality demonstrated. The initial five-second observations were pending, not failures. | `16-find-large-wait-sublimite.mkv` and `16-find-large-complete-sublime.mkv`; latency/per-step inventory below. Common literal count 8,947,840 (M)[AC], grep-count log; `!` is cache/visible overflow, `+` is searching, per read-only src/findui/paint.c schema. |
| Type query while search runs | Visible query accepts successive characters. | Visible query accepts successive characters; document unchanged by query entry. | **NEW**, responsive field interaction while workers search. | `16-find-large-sublimite.mkv` and `10-large-loaded-sublime.mkv`, query-00…query-21 frames and per-character onset inventory. Early F3 presses during pending work are not treated as a reliable queued navigation count. |

Current match starts after typing / Return / Return / F3 / Shift+F3: sublimité **1:1 → 1:1 → 1:12 → 1:24 → 1:12**; Sublime **1:1 → 1:12 → 1:24 → 2:1 → 1:24** (M)[AC], from selection frames and ASCII fixture columns. Escape preserves those current selections. The first Return distinction is therefore explicit, not hidden by a generic search-passes label.

## 17. Recovery conflict matrix

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Changed recovery base | Opens replacement text. | Opens clean replacement text and the specified set-aside notice; first edit clears it. | **FIXED** launch-blocking changed base. | `17-recovery-a-*.mkv`; sublimité notice screenshot; retained journal name below. |
| Same-bytes rewrite | Not a required matched Sublime row. | Opens clean original disk bytes with the same notice. | **FIXED** drive-2 ordinary-launch trigger. | `17-recovery-b-sublimite.mkv`; notice and notice-after-edit frames. |
| Normal close of unsaved run; repeated relaunch | Not repeated for Sublime. | Marker restored, dirty, all requested relaunches open. | **FIXED** ordinary relaunch blocker. | `17-recovery-c-sublimite.mkv`, relaunch-0 through relaunch-3 frames; window_found events. |
| Copied drive-2 conflict journal | Not applicable. | Opens `external replacement`, clean, with set-aside notice. | **FIXED** exact drive-2 conflict state. | `17-recovery-d-sublimite.mkv`; old fixture opened only, never saved. |
| Copy of Tobias's ordinary profile, no argument then navigation file | Not applicable. | Restores `shot-probe.md`, `open-fresh.c`, `typing.txt`; notice on first launch; second launch adds `navigation.txt` and opens. | **FIXED** tested ordinary-profile startup; real profile was never launched. | `17-recovery-e-sublimite.mkv`, restored/with-file tab strips and directory listings. |

Retained journals (M)[AC], names/sizes from directory reads: `17a/sublimite/session-26kvh7.set-aside-1791812282797492550` and `17b/sublimite/session-fHPsDA.set-aside-1791812306168771726`, each **12,288 bytes**. `strings` does not contain one contiguous marker because individual keystrokes are individual INSERT records. Read-only wire inspection reconstructs **UNSAVED_MARKER_** at consecutive offsets; CRC32C verifies all **38 records** in each file (M)[AC], `logs/set-aside-inserts.json`. This verifies retained bytes, not power-loss durability or automatic replay of the set-aside file. Copied drive-2 conflict: `session-fA3dZw.set-aside-1791812328216311056`, **12,288 bytes**; copied ordinary profile: `session-tXZIzU.set-aside-1791812332466129014`, **1,130,496 bytes** (M)[AC]. Successful window creation is logged; the inherited launcher did not sample successful process exit codes for these cases.

## 18. Unicode tab titles

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Unicode tab name and CLI reopen | Unicode name painted; file activation can reuse a different existing window. | Visible `naïve-中文-Ελ…` prefix paints and survives tab cycling; full name is ellipsized. Reopening the same path adds a duplicate tab. | **NEW** Unicode title coverage passes for visible glyphs; duplicate-path tabs are a separate divergence. | `18-tabtitle-*.mkv`, unicode/second-tab/back frames. Separate same-window two-tab repeat follows. |
| Unicode file as a second tab beside a new ASCII path | Two-tab repeat paints the Unicode title. | Visible non-ASCII prefix paints and survives cycling; long name is ellipsized. | **NEW**, Unicode-title adoption present, with ordinary width truncation. | `18-tabtitle-second-*.mkv`, second/away/back frames. Initial Sublime CLI probe reused an existing window; this unique-path repeat establishes same-window tabs. |

## 19. External changes on clean and dirty buffers

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Clean external append; undo after reload | Ctrl+Z removes the external append and leaves a dirty buffer. | Ctrl+Z leaves the reloaded text unchanged: undo history reset. | **FIXED** clean prompt; **NEW** undo-policy divergence, consistent with ret. | `19-external-*.mkv`; after-reload-undo readbacks in logs. |
| Dirty external append, R then K | Modal Reload/Cancel UI; controlled repeat follows because earlier undo made the shrink stage dirty. | R reloads disk, K preserves `KEEP_` edits and excludes the conflicting appended line. Inactive clean tab picks up `INACTIVE_APPEND`. | **NEW**, ret's dirty/clean split behaves as specified in sublimité. | `19-external-sublimite.mkv`, prompt-R/reloaded-R/kept-K/inactive-return frames and exact readbacks. |
| Clean shrink at EOF; controlled dirty Reload/Keep | Clean shrink clamps; explicit Reload/Cancel selections produce expected text. | Clean shrink clamps; R reloads and K keeps. | **NEW**, ret present for clean/dirty cases. | `19-external-controlled-*.mkv`; reloaded SHA256 d880abcb5e564b731c4e1d2bf7c0869e4555d7b22cc7ca87dc0c2f68b0588219, kept 0074d511e872a0b722d07e4ad1d7130ef05f461b576c0731bb20fa702df402f0 (M)[AC]. |
| End of controlled external-change run | No corresponding recorded renderer error. | One error -11 and subsequent BadWindow at close; final recorded buffer still visible. Fresh repetition stays alive and exits 0 normally (M)[AC]. | **NEW**, unexplained non-reproduced renderer exit. Cause is not attributed to the reload code. | `19-external-controlled-sublimite.editor.log`, video, trace/journal; `19-external-repro-sublimite.json` pre/post-dwell returncode null and after-close returncode 0. |

The original Sublime Ctrl+Z undoes its automatic reload, leaving the buffer dirty before the next external shrink. The controlled repeat starts that stage clean and clicks the observed Reload/Cancel buttons; it is not silently substituted for the original sequential outcome. One sublimité renderer exit remains unexplained; a repeat passes. No code was changed.

## 20. ZWJ emoji and bytes

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Emoji fixture render | Color emoji; the nonstandard smile+ZWJ+laptop sequence visibly has separate smile/laptop artwork. | One missing-glyph symbol for woman-technologist; first smile only for smile+ZWJ+laptop; thumb only without skin tone; standalone smile and heart render. | **CHANGED** from drive 2: **r8e behavior present**, font gap remains. No overlay or extra cells for joiners/modifiers in sublimité. The missing woman-technologist artwork is still a presentation blocker on Tobias's same-font box. | `20-emoji-*.mkv`, default/zoom2 frames; fontconfig coverage in `logs/environment.txt`. |
| Emoji bytes after Backspace/undo/save | Original hash restored. | Original hash restored. | **NEW** byte-preservation proof. | Before/after SHA256 `c47c6c8d60f8b1f6ca84114e49b6163e8eb955686d2678b14b4e56850b011ee0` in both task logs (M)[AC]. |
| Grapheme caret steps; Backspace; undo | Three Right presses reach past B on each line; Backspace removes the final code point of the emoji cluster. | Three Right presses reach past B; Backspace removes the whole cluster (M)[AC]. B is three grid cells after A on all lines. | **CHANGED** ZWJ layout and **NEW** deletion-policy comparison. | `20-emoji-carets-*.mkv`; deletion readbacks: sublimité AB throughout; Sublime A👩‍B / A😀‍B / A👍B / AB / A❤B. Undo/save preserves the original hash. A-to-B 60 px / cell 20 px = 3 cells (M)[AC], default sublimité still grid. |
| Original UTF-8 fixture: accents, Greek, CJK, smile; injected é中文😀 | Renders the common-script fixture and inserted text. | Renders the common-script fixture and inserted text; woman-technologist artwork remains missing. | **STILL PRESENT** common-script support; ZWJ details below are **CHANGED**. | `utf8-*.mkv`, unicode/emoji-backspace/typed-unicode frames; unchanged copied drive-2 fixture. |

Fontconfig on this box lists only Noto Color Emoji for U+1F469, whereas U+1F600 and U+1F44D have monochrome coverage (`logs/environment.txt`). The observed first-artwork rendering establishes **r8e present**; it does not remove the color-font coverage gap. The woman-technologist symbol remains a blocker for the requested Sublime replacement on this same-font box. Sublime is not a strict emoji-cell grid: in the zoomed caret probe its ASCII advance is 11 px, A starts at x=64 and B at x=98 for the ordinary clusters or x=121 for the smile+ZWJ+laptop line. Thus B is about 3.09 or 5.18 ASCII advances from A (34/11 and 57/11; M[AC]), `logs/emoji-sublime-cell-positions.json`. At the initial zoom the corresponding spans are approximately three and five ASCII advances; the color fallback has its own advance. Sublimité keeps all five B positions at three cells at both tested zooms. The focus-valid separate caret run supplies the step/deletion proof; Ctrl+0 in Sublime is not assumed to reset its font or preserve text focus.

## 21. Optional primary-font selection

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| iosevka | Not requested. | Cell 17 px; sampled M ink 25 px (M)[AC]. Requested face visibly changes shaping; family/file Liberation results agree. First visible text→final sampled face: 9 intervals (M)[AC]. | **NEW**, environment font selection/fallback demonstrated. | `21-font-iosevka-sublimite.mkv`; `logs/font-selection-metrics.json` supplies exact M-component ROI/rows; `logs/font-video-metrics.json` times 0.950→1.100 s (M)[AC]. Typed frames retain grid alignment. |
| liberation-mono | Not requested. | Cell 20 px; sampled M ink 22 px (M)[AC]. Requested face visibly changes shaping; family/file Liberation results agree. First visible text→final sampled face: 2 intervals (M)[AC]. | **NEW**, environment font selection/fallback demonstrated. | `21-font-liberation-mono-sublimite.mkv`; `logs/font-selection-metrics.json` supplies exact M-component ROI/rows; `logs/font-video-metrics.json` times 0.917→0.950 s (M)[AC]. Typed frames retain grid alignment. |
| proportional | Not requested. | Cell 20 px; sampled M ink 25 px (M)[AC]. Embedded fallback; one font-selection failure line. First visible text→final sampled face: 0 intervals (M)[AC]. | **NEW**, environment font selection/fallback demonstrated. | `21-font-proportional-sublimite.mkv`; `logs/font-selection-metrics.json` supplies exact M-component ROI/rows; `logs/font-video-metrics.json` times 0.883→0.883 s (M)[AC]. Typed frames retain grid alignment. |
| missing | Not requested. | Cell 20 px; sampled M ink 25 px (M)[AC]. Embedded fallback; one font-selection failure line. First visible text→final sampled face: 0 intervals (M)[AC]. | **NEW**, environment font selection/fallback demonstrated. | `21-font-missing-sublimite.mkv`; `logs/font-selection-metrics.json` supplies exact M-component ROI/rows; `logs/font-video-metrics.json` times 0.883→0.883 s (M)[AC]. Typed frames retain grid alignment. |
| file | Not requested. | Cell 20 px; sampled M ink 22 px (M)[AC]. Requested face visibly changes shaping; family/file Liberation results agree. First visible text→final sampled face: 1 intervals (M)[AC]. | **NEW**, environment font selection/fallback demonstrated. | `21-font-file-sublimite.mkv`; `logs/font-selection-metrics.json` supplies exact M-component ROI/rows; `logs/font-video-metrics.json` times 0.917→0.933 s (M)[AC]. Typed frames retain grid alignment. |

Ink is measured on M here, so it is not equated with task 1’s mixed Latin ink sample. Column spacing comes from repeated M component starts. First/final-face detection uses the saved face target and foreground mask in the named video ROI; those frame sequences are retained. The config-file font.family setting was not exercised. Fontconfig resolves Iosevka Semibold to /usr/local/share/fonts/iosevka/SGr-Iosevka-SemiBold.ttc and Liberation Mono Regular to /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf (logs/fontconfig-matches.txt). The post-type video samples in logs/font-caret-video.json show the caret aligned with the next M in each requested/fallback face.

## 22. Supplemental daily file commands

| Action | Sublime did | sublimité did | Divergence and drive-2 status | Evidence |
|---|---|---|---|---|
| Ctrl+N, Ctrl+O, Ctrl+Shift+S, Ctrl+Q | Ctrl+N creates untitled. The brief early-dialog/Escape probe does not establish Open/Save-As/Q completion in this environment. | All four show not implemented; original buffer remains. | **NEW**, missing basic file commands and Quit shortcut. | `22-common-commands-sublimite.mkv`, explicit notice stills/video. Sublime’s early screenshots are not falsely described as successful dialogs; no file dialog was submitted. |

## Video timestamps and latency inventory

All numbers in the following inventories are (M)[AC], from the named recording and its command log. Interval→ms uses 1000/60; “N×16.7 ms” is its rounded display. These are observations, never budgets. Full crop/error arrays remain in the named JSON files.

| Video / action | Command envelope, s | First matching frame, s | Intervals / milliseconds | Evidence / power |
|---|---|---|---|---|
| `01-fresh-sublimite.mkv` / startup | 0.763584–0.889102 | 0.933333 | 10.18 intervals / 169.7 ms (≈10.18×16.7) | ROI `[122, 48, 80, 29]`; (M)[AC] |
| `01-fresh-sublime.mkv` / startup | 0.747377–1.026802 | 1.233333 | 29.16 intervals / 486.0 ms (≈29.16×16.7) | ROI `[510, 64, 90, 11]`; (M)[AC] |
| `02-typing-r2-sublimite.mkv` / glyph | 4.737483–4.741780 | 4.766667 | 1.75 intervals / 29.2 ms (≈1.75×16.7) | ROI `[40, 46, 20, 27]`; (M)[AC] |
| `02-typing-r2-sublime.mkv` / glyph | 4.799905–4.805715 | 4.833333 | 2.01 intervals / 33.4 ms (≈2.01×16.7) | ROI `[59, 63, 7, 11]`; (M)[AC] |
| `10-large-sublimite.mkv` / Y | 59.728814–59.746785 | 59.766667 | 2.27 intervals / 37.9 ms (≈2.27×16.7) | ROI `[160, 46, 20, 27]`; (M)[AC] |
| `10-large-sublimite.mkv` / Z | 64.436976–64.446337 | 64.466667 | 1.78 intervals / 29.7 ms (≈1.78×16.7) | ROI `[160, 46, 20, 27]`; (M)[AC] |
| `10-large-sublimite.mkv` / end-navigation | 47.899311–48.092264 | 48.166667 | 16.04 intervals / 267.4 ms (≈16.04×16.7) | ROI `[0, 46, 140, 27]`; (M)[AC] |
| `10-large-sublimite.mkv` / startup | 0.753612–0.868076 | 0.966667 | 12.78 intervals / 213.1 ms (≈12.78×16.7) | ROI `[180, 46, 100, 27]`; (M)[AC] |
| `08-save-sublimite.mkv` / append | 5.113932; echo completion not logged | 5.166667 | 3.16 intervals / 52.7 ms (≈3.16×16.7) | ROI `[40, 125, 280, 30]`; (M)[AC] |
| `01-fresh-repeat-sublime.mkv` / startup | 0.761703–0.916379 | 1.366667 | 36.30 intervals / 605.0 ms (≈36.30×16.7) | ROI `[140, 64, 90, 15]`; (M)[AC] |
| `08-save-sublime.mkv` / append | 5.068874; echo completion not logged | 5.716667 | 38.87 intervals / 647.8 ms (≈38.87×16.7) | ROI `[60, 98, 135, 17]`; (M)[AC] |
| `17-recovery-a-sublimite.mkv` / relaunch | 17.344023–17.425729 | 17.450000 | 6.36 intervals / 106.0 ms (≈6.36×16.7) | ROI `[40, 46, 300, 28]`; (M)[AC] |
| `17-recovery-b-sublimite.mkv` / relaunch | 17.463229–17.544636 | 17.583333 | 7.21 intervals / 120.1 ms (≈7.21×16.7) | ROI `[40, 46, 300, 28]`; (M)[AC] |
| `01-fresh-repeat-sublimite.mkv` / startup-caret-excluded | 1.030882–1.167450 | 1.233333 | 12.15 intervals / 202.5 ms (≈12.15×16.7) | ROI `[145, 48, 80, 29]`; (M)[AC] |
| `07-find-sublimite.mkv` / panel | 3.589672–3.624223 | 3.650000 | 3.62 intervals / 60.3 ms (≈3.62×16.7) | ROI `[0, 1220, 220, 40]`; (M)[AC] |
| `15-mouse-calibrated-sublimite.mkv` / underscore-word | 16.439351–16.654306 | 16.583333 | 8.64 intervals / 144.0 ms (≈8.64×16.7) | ROI `[340, 40, 80, 40]`; (M)[AC] |
| `15-mouse-calibrated-sublimite.mkv` / triple-line | 19.315154–19.658409 | 19.633333 | 19.09 intervals / 318.2 ms (≈19.09×16.7) | ROI `[120, 120, 1200, 40]`; (M)[AC] |
| `07-find-sublime.mkv` / panel | 3.539481–3.569867 | 3.583333 | 2.63 intervals / 43.9 ms (≈2.63×16.7) | ROI `[10, 540, 180, 33]`; (M)[AC] |
| `15-mouse-target-ready-sublime.mkv` / triple-line | 119.788036–120.150934 | 120.166667 | 22.72 intervals / 378.6 ms (≈22.72×16.7) | ROI `[88, 96, 600, 18]`; (M)[AC] |
| `15-mouse-target-ready-sublime.mkv` / word-cursor-excluded | 115.764248–116.028999 | 115.983333 | 13.15 intervals / 219.1 ms (≈13.15×16.7) | ROI `[187, 60, 8, 18]`; (M)[AC] |
| `10-large-loaded-sublime.mkv` / Y | 246.255929–246.263419 | 246.300000 | 2.64 intervals / 44.1 ms (≈2.64×16.7) | ROI `[99, 559, 10, 17]`; (M)[AC] |
| `10-large-loaded-sublime.mkv` / Z | 251.063208–251.090951 | 251.100000 | 2.21 intervals / 36.8 ms (≈2.21×16.7) | ROI `[99, 62, 10, 17]`; (M)[AC] |
| `10-large-loaded-sublime.mkv` / end-navigation | 235.008873–235.054552 | 235.200000 | 11.47 intervals / 191.1 ms (≈11.47×16.7) | ROI `[10, 557, 75, 20]`; (M)[AC] |
| `16-find-large-wait-sublimite.mkv` / rare-match | 8.506266–8.522758 | 13.200000 | 281.62 intervals / 4693.7 ms (≈281.62×16.7) | ROI `[0, 42, 1200, 80]`; (M)[AC] |
| `16-find-large-complete-sublime.mkv` / rare-match | 131.281162–131.305037 | 140.900000 | 577.13 intervals / 9618.8 ms (≈577.13×16.7) | ROI `[450, 285, 240, 42]`; (M)[AC] |

The later fresh-path repeats occurred under different host load and Sublime font/session state. The drive-2-style startup ROI includes the initial caret: it produced a 685.8 ms phase-sensitive match in the sublimité repeat, rejected as first-text latency. A caret-excluded ROI finds text at 202.5 ms instead (M)[AC], startup-caret-excluded.json; the original 169.7 ms observation is unchanged by that check. The quoted drive-2 startup values are not recalculated, and no causal speedup is inferred from them alone. The original Sublime startup crop is unobstructed text to the right of the update dialog. Relaunch envelopes end at window discovery, not at completion of a synthetic key. The append marker is immediately before echo, but the inherited harness does not log echo’s return time.

### Mouse response and wheel settling

First means first visible changed text/caret/viewport frame. Settling includes the prescribed entire wheel burst and is not a per-notch processing latency. For the original double-click’s punctuation target, first response is not proof of a completed word selection; the calibrated word/line results above supply that. A click’s later caret blink is not reported as completion latency.

| Video / action | Command envelope, s | First moved / settled, s | First / settled intervals | Evidence / power |
|---|---|---|---|---|
| `04-mouse-sublimite.mkv` / click | 3.571698–3.677076 | 3.600 / caret phase excluded | 1.698 / N/A; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / drag | 4.493178–4.497470 | 4.517 / 4.550 | 1.409 / 3.409; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / double | 5.329341–5.534030 | 5.350 / 5.383 | 1.240 / 3.240; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / wheel-down | 6.331930–6.942333 | 6.383 / 6.883 | 3.084 / 33.084; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / wheel-up | 7.744635–8.049630 | 7.800 / 8.000 | 3.322 / 15.322; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / minimap-click | 8.833539–8.938663 | 8.867 / 8.883 | 1.988 / 2.988; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublimite.mkv` / minimap-drag | 9.761378–9.765373 | 9.817 / 9.817 | 3.317 / 3.317; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublimite.mkv` / down3 | 10.008472–10.314129 | 10.050 / 10.250 | 2.492 / 14.492; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublimite.mkv` / down30 | 11.718786–14.737351 | 11.767 / 14.667 | 2.873 / 176.873; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublimite.mkv` / up30 | 16.077010–19.095719 | 16.117 / 19.033 | 2.379 / 177.379; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublimite.mkv` / up3 | 20.416596–20.725031 | 20.467 / 20.667 | 3.004 / 15.004; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublimite.mkv` / burst30 | 22.135001–22.148950 | 22.183 / 22.200 | 2.900 / 3.900; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` / down3 | 3.583624–3.891984 | 3.633 / 3.833 | 2.983 / 14.983; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` / down30 | 5.199478–8.215104 | 5.250 / 8.150 | 3.031 / 177.031; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` / up30 | 9.536871–12.551240 | 9.583 / 12.200 | 2.788 / 159.788; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` / up3 | 13.835010–14.144827 | No movement at document start | N/A; later burst excluded | (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` / burst30 | 15.444727–15.454564 | 15.483 / 15.500 | 2.316 / 3.316; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / click | 3.505229–3.610270 | 3.533 / caret phase excluded | 1.686 / N/A; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / drag | 4.382820–4.388595 | 4.417 / 4.417 | 2.031 / 2.031; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / double | 5.192073–5.396901 | 5.217 / 5.233 | 1.476 / 2.476; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / wheel-down | 6.164714–6.772821 | 6.217 / 6.783 | 3.117 / 37.117; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / wheel-up | 7.547277–7.855216 | 7.600 / 7.867 | 3.163 / 19.163; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / minimap-click | 8.643113–8.747685 | 8.667 / 8.700 | 1.413 / 3.413; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `04-mouse-sublime.mkv` / minimap-drag | 9.579421–9.583442 | 9.617 / 9.617 | 2.235 / 2.235; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublime.mkv` / down3 | 10.219264–10.528394 | 10.267 / 10.550 | 2.844 / 19.844; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublime.mkv` / down30 | 11.883132–14.896887 | 11.933 / 14.967 | 3.012 / 185.012; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublime.mkv` / up30 | 16.193512–19.211009 | 16.250 / 19.250 | 3.389 / 183.389; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublime.mkv` / up3 | 20.500229–20.807892 | 20.550 / 20.833 | 2.986 / 19.986; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-sublime.mkv` / burst30 | 22.203768–22.215313 | 22.250 / 22.383 | 2.774 / 10.774; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` / down3 | 3.798568–4.108504 | 3.850 / 4.117 | 3.086 / 19.086; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` / down30 | 5.417458–8.434115 | 5.467 / 8.467 | 2.953 / 182.953; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` / up30 | 9.700009–12.712271 | 9.750 / 12.783 | 2.999 / 184.999; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` / up3 | 14.003719–14.308896 | 14.050 / 14.317 | 2.777 / 18.777; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` / burst30 | 15.608845–15.620472 | 15.667 / 15.783 | 3.469 / 10.469; multiply by 16.7 ms | `logs/action-onsets.json`; (M)[AC] |

The zero-delay unwrapped bursts account for the full ninety rows in both editors; repainting coalesces intermediate states. The wrapped sublimité reversal is a functional distance error, not a slow-number failure. Whole-word timing excludes the recorded X cursor overlay from the sampled f cell; `word-pointer-comparison.png` and the rejected full-word crop document why.

### Ready large-file F3 steps

`16-find-large-wait-sublimite.mkv` versus `16-find-large-complete-sublime.mkv`. Cells show **command begin–end → first matching frame (seconds); intervals / ms**. All (M)[AC]; `logs/late-metrics.json` retains ROIs and masks.

| Step | sublimité | Sublime |
|---|---|---|
| 1 | 38.207–38.228 → 38.250; 2.57 / 42.8 | 501.949–502.005 → 502.050; 6.04 / 100.7 |
| 2 | 39.108–39.129 → 39.150; 2.52 / 42.1 | 504.429–504.484 → 504.517; 5.25 / 87.5 |
| 3 | 40.032–40.055 → 40.083; 3.05 / 50.9 | 506.011–506.042 → 506.067; 3.34 / 55.6 |
| 4 | 40.724–40.746 → 40.767; 2.53 / 42.2 | 508.579–508.622 → 508.717; 8.23 / 137.2 |
| 5 | 41.516–41.539 → 41.567; 3.04 / 50.7 | 510.787–510.865 → 510.950; 9.78 / 163.1 |
| 6 | 42.416–42.438 → 42.467; 3.07 / 51.1 | 512.132–512.176 → 512.250; 7.09 / 118.2 |
| 7 | 43.310–43.329 → 43.367; 3.41 / 56.8 | 514.275–514.336 → 514.383; 6.48 / 108.0 |
| 8 | 44.109–44.129 → 44.150; 2.44 / 40.7 | 515.916–515.961 → 516.017; 6.04 / 100.7 |
| 9 | 44.991–45.012 → 45.033; 2.55 / 42.5 | 517.569–517.623 → 517.633; 3.84 / 63.9 |
| 10 | 45.936–45.959 → 45.983; 2.84 / 47.4 | 519.462–519.532 → 519.533; 4.28 / 71.4 |
| 11 | 46.793–46.812 → 46.850; 3.44 / 57.3 | 521.013–521.075 → 521.083; 4.21 / 70.2 |
| 12 | 47.685–47.705 → 47.733; 2.89 / 48.1 | 522.233–522.287 → 522.317; 5.03 / 83.8 |
| 13 | 48.558–48.579 → 48.600; 2.53 / 42.1 | 524.066–524.133 → 524.183; 7.05 / 117.5 |
| 14 | 49.242–49.262 → 49.283; 2.51 / 41.8 | 525.562–525.636 → 525.800; 14.29 / 238.2 |
| 15 | 50.058–50.078 → 50.117; 3.53 / 58.9 | 527.541–527.612 → 527.667; 7.53 / 125.5 |
| 16 | 50.764–50.789 → 50.817; 3.18 / 52.9 | 529.149–529.295 → 529.267; 7.06 / 117.7 |
| 17 | 51.759–51.779 → 51.817; 3.45 / 57.5 | 530.784–530.824 → 530.850; 3.96 / 65.9 |
| 18 | 52.742–52.763 → 52.800; 3.47 / 57.8 | 532.088–532.143 → 532.117; 1.73 / 28.8 |
| 19 | 53.731–53.760 → 53.783; 3.13 / 52.2 | 533.426–533.463 → 533.483; 3.45 / 57.5 |
| 20 | 54.709–54.729 → 54.767; 3.46 / 57.6 | 534.641–534.675 → 534.700; 3.52 / 58.7 |

### Query characters while a large-file search runs

`16-find-large-sublimite.mkv` versus `10-large-loaded-sublime.mkv`. Cells show **command begin–end → first matching frame (seconds); intervals / ms**. All (M)[AC]; `logs/late-metrics.json` retains ROIs and masks.

| Step | sublimité | Sublime |
|---|---|---|
| 1 'A' | 88.500–88.564 → 88.567; 4.01 / 66.8 | 325.269–325.337 → 325.317; 2.86 / 47.6 |
| 2 'B' | 89.502–89.564 → 89.550; 2.85 / 47.5 | 325.932–325.991 → 325.983; 3.10 / 51.6 |
| 3 'S' | 90.451–90.507 → 90.517; 3.97 / 66.1 | 326.646–326.706 → 326.683; 2.27 / 37.8 |
| 4 'E' | 91.408–91.483 → 91.483; 4.53 / 75.5 | 327.372–327.445 → 327.433; 3.71 / 61.8 |
| 5 'N' | 92.404–92.480 → 92.483; 4.76 / 79.3 | 328.170–328.230 → 328.200; 1.83 / 30.4 |
| 6 'T' | 93.396–93.458 → 93.467; 4.22 / 70.3 | 328.902–328.967 → 328.933; 1.88 / 31.3 |
| 7 '_' | 94.383–94.450 → 94.417; 2.04 / 34.0 | 329.438–329.501 → 329.483; 2.71 / 45.2 |
| 8 'A' | 95.403–95.472 → 95.467; 3.79 / 63.2 | 330.185–330.250 → 330.217; 1.87 / 31.2 |
| 9 'S' | 96.517–96.587 → 96.567; 2.96 / 49.4 | 330.917–330.983 → 330.950; 1.98 / 33.0 |
| 10 'T' | 97.659–97.728 → 97.717; 3.49 / 58.2 | 331.481–331.546 → 331.517; 2.16 / 36.0 |
| 11 'R' | 98.808–98.878 → 98.883; 4.50 / 75.0 | 332.209–332.268 → 332.250; 2.48 / 41.4 |
| 12 'A' | 99.954–100.026 → 100.017; 3.73 / 62.2 | 332.895–332.959 → 332.950; 3.28 / 54.7 |
| 13 '_' | 100.860–100.960 → 100.967; 6.38 / 106.3 | 333.631–333.693 → 333.667; 2.15 / 35.8 |
| 14 '9' | 102.269–102.343 → 102.333; 3.86 / 64.3 | 334.425–334.485 → 334.467; 2.53 / 42.2 |
| 15 '8' | 103.625–103.699 → 103.683; 3.51 / 58.5 | 335.150–335.212 → 335.183; 1.98 / 33.0 |
| 16 '7' | 104.957–105.022 → 105.017; 3.58 / 59.6 | 335.685–335.747 → 335.717; 1.93 / 32.1 |
| 17 '6' | 106.224–106.288 → 106.267; 2.54 / 42.4 | 336.230–336.294 → 336.267; 2.22 / 36.9 |
| 18 '5' | 107.328–107.416 → 107.400; 4.29 / 71.5 | 336.960–337.022 → 337.000; 2.42 / 40.4 |
| 19 '4' | 108.637–108.706 → 108.683; 2.80 / 46.6 | 337.664–337.726 → 337.700; 2.18 / 36.3 |
| 20 '3' | 109.881–109.959 → 109.933; 3.16 / 52.6 | 338.398–338.465 → 338.433; 2.12 / 35.3 |
| 21 '2' | 111.242–111.315 → 111.300; 3.48 / 58.0 | 339.130–339.191 → 339.183; 3.21 / 53.4 |
| 22 '1' | 112.717–112.777 → 112.750; 1.99 / 33.2 | 339.855–339.917 → 339.900; 2.72 / 45.3 |

The initial large-search runs include pending work; later ready F3 steps are a separate comparison. The search text has twenty-two characters, counted from the literal input. An initial off-by-one analysis pairing was rejected and replaced by `query-metrics.json`.

### Gutter labels at each Page Up

These are first complete readable labels in samples taken shortly after each command; Sublime may still be animating. The final named still gives its settled view. All (M)[AC], from `logs/large-gutters-validated.json`, its per-step timestamps and retained cropped frames. Blank/popup runs have no invented label.

| Page Up input | sublimité standard | sublimité short dwell | Sublime loaded clean |
|---|---:|---:|---:|
| 1 | 8947827 | 8947827 | 8947814 |
| 2 | 8947812 | 8947812 | 8947802 |
| 3 | 8947797 | 8947797 | 8947783 |
| 4 | 8947782 | 8947782 | 8947770 |
| 5 | 8947767 | 8947767 | 8947753 |
| 6 | 8947752 | 8947752 | 8947740 |
| 7 | 8947737 | 8947737 | 8947725 |
| 8 | 8947722 | 8947722 | 8947713 |
| 9 | 8947707 | 8947707 | 8947697 |
| 10 | 8947692 | 8947692 | 8947684 |
| 11 | 8947677 | 8947677 | 8947667 |
| 12 | 8947662 | 8947662 | 8947653 |
| 13 | 8947647 | 8947647 | 8947636 |
| 14 | 8947632 | 8947632 | 8947626 |
| 15 | 8947617 | 8947617 | 8947610 |
| 16 | 8947602 | 8947602 | 8947590 |
| 17 | 8947587 | 8947587 | 8947578 |
| 18 | 8947572 | 8947572 | 8947560 |
| 19 | 8947557 | 8947557 | 8947548 |
| 20 | 8947542 | 8947542 | 8947530 |

### Unicode title adoption

`18-tabtitle-sublimite.mkv`: sampled title-prefix pixels at **1.150000 s**, non-ASCII target at **1.233333 s**; difference **5 intervals / 83.3 ms** (M)[AC]. `logs/tab-title-metrics.json` and prefix/Unicode frame sequences retain the ROIs.

`18-tabtitle-sublime.mkv`: sampled title-prefix pixels at **0.866667 s**, non-ASCII target at **0.866667 s**; difference **0 intervals / 0.0 ms** (M)[AC]. `logs/tab-title-metrics.json` and prefix/Unicode frame sequences retain the ROIs.

For sublimité, the separate ASCII-only na crop confirms the same prefix time (`logs/tab-ascii-prefix.json`). This is title-pixel adoption, not a substitute for the startup trace’s first_present. Long names remain ellipsized.

## Flashscan results

All **53 original sublimité recordings × 3 colours = 159 detector runs** (M)[AC], counted from `logs/flash-summary.json`, report **zero flashes and zero raw colour-change events**. Every output has a nonzero frame count and no decoder error. Parameters are inherited: #bf853d/tol40, #ff8800/tol60, #6b5030/tol24, minimum jump 200 pixels, appearance→vanish window 30 resampled frames (60/2, arithmetic). The default ROI is (1568,40,160,1220), from the requested default window/right strip. Zoom can shift the minimap’s left edge; the fixed-strip claim is limited to that strip. Resize has separate moving strips.

| Recording | bf853d / 40 | ff8800 / 60 | 6b5030 / 24 |
|---|---:|---:|---:|
| `01-fresh-repeat-sublimite.mkv` | 0 | 0 | 0 |
| `01-fresh-sublimite.mkv` | 0 | 0 | 0 |
| `02-paced-sublimite.mkv` | 0 | 0 | 0 |
| `02-paced-verified-sublimite.mkv` | 0 | 0 | 0 |
| `02-typing-r2-sublimite.mkv` | 0 | 0 | 0 |
| `03-nav-sublimite.mkv` | 0 | 0 | 0 |
| `03-selection-sublimite.mkv` | 0 | 0 | 0 |
| `04-mouse-sublimite.mkv` | 0 | 0 | 0 |
| `05-edit-r2-sublimite.mkv` | 0 | 0 | 0 |
| `05-edit-sublimite.mkv` | 0 | 0 | 0 |
| `06-alternate-sublimite.mkv` | 0 | 0 | 0 |
| `06-clipboard-sublimite.mkv` | 0 | 0 | 0 |
| `06-cross-sublimite.mkv` | 0 | 0 | 0 |
| `07-find-sublimite.mkv` | 0 | 0 | 0 |
| `08-save-sublimite.mkv` | 0 | 0 | 0 |
| `09-tabs-sublimite.mkv` | 0 | 0 | 0 |
| `10-large-early-sublimite.mkv` | 0 | 0 | 0 |
| `10-large-sublimite.mkv` | 0 | 0 | 0 |
| `11-resize-sublimite.mkv` | 0 | 0 | 0 |
| `12-hotexit-sublimite.mkv` | 0 | 0 | 0 |
| `12-recovery-conflict-sublimite.mkv` | 0 | 0 | 0 |
| `13-minimap-sublimite.mkv` | 0 | 0 | 0 |
| `14-idle-sublimite.mkv` | 0 | 0 | 0 |
| `15-mouse-calibrated-sublimite.mkv` | 0 | 0 | 0 |
| `15-mouse-sublimite.mkv` | 0 | 0 | 0 |
| `15-mouse-wrapped-sublimite.mkv` | 0 | 0 | 0 |
| `15-wrapped-reverse-sublimite.mkv` | 0 | 0 | 0 |
| `16-find-large-sublimite.mkv` | 0 | 0 | 0 |
| `16-find-large-wait-sublimite.mkv` | 0 | 0 | 0 |
| `16-find-sublimite.mkv` | 0 | 0 | 0 |
| `16-original-sequence-sublimite.mkv` | 0 | 0 | 0 |
| `17-recovery-a-sublimite.mkv` | 0 | 0 | 0 |
| `17-recovery-b-sublimite.mkv` | 0 | 0 | 0 |
| `17-recovery-c-sublimite.mkv` | 0 | 0 | 0 |
| `17-recovery-d-sublimite.mkv` | 0 | 0 | 0 |
| `17-recovery-e-sublimite.mkv` | 0 | 0 | 0 |
| `18-tabtitle-second-sublimite.mkv` | 0 | 0 | 0 |
| `18-tabtitle-sublimite.mkv` | 0 | 0 | 0 |
| `19-external-controlled-sublimite.mkv` | 0 | 0 | 0 |
| `19-external-repro-sublimite.mkv` | 0 | 0 | 0 |
| `19-external-sublimite.mkv` | 0 | 0 | 0 |
| `20-emoji-carets-sublimite.mkv` | 0 | 0 | 0 |
| `20-emoji-sublimite.mkv` | 0 | 0 | 0 |
| `21-font-file-sublimite.mkv` | 0 | 0 | 0 |
| `21-font-iosevka-sublimite.mkv` | 0 | 0 | 0 |
| `21-font-liberation-mono-sublimite.mkv` | 0 | 0 | 0 |
| `21-font-missing-sublimite.mkv` | 0 | 0 | 0 |
| `21-font-proportional-sublimite.mkv` | 0 | 0 | 0 |
| `22-common-commands-sublimite.mkv` | 0 | 0 | 0 |
| `dense64-sublimite.mkv` | 0 | 0 | 0 |
| `font12-sublimite.mkv` | 0 | 0 | 0 |
| `utf8-sublimite.mkv` | 0 | 0 | 0 |
| `zoom-sublimite.mkv` | 0 | 0 | 0 |

The moving task-11 strip reports **zero flashes over 754 resampled frames per colour**, and the moving find-resize strip **zero over 2,623 frames per colour** (M)[AC], `logs/11-resize-tracked.flash-*.txt` and `logs/16-find-tracked.flash-*.txt`. Thus the two moving variants add **6 runs**, for **165 completed scans** total (arithmetic on measured output counts). Their filters and width-transition times are retained. The supported-key bottom notice scan has **zero amber frames** (M)[AC], `logs/find-notice.json`; old drive-2 Ctrl+F/F3/Ctrl+H durations were 4/13/16 frames (quoted M, power unstamped). Deliberate R/K and unsupported-command notices are identified in the task tables, not called minimap flashes.

## Recording timestamp index

Ranges below run from logged launch (or first command for continuations) to the final captured still. All (M)[AC]. They locate the task-table evidence; precise action envelopes and first-result frames are in the latency tables and per-video timelines.

| Recording | Task envelope, video seconds |
|---|---:|
| `01-fresh-repeat-sublime.mkv` | 0.762–7.311 (M)[AC] |
| `01-fresh-repeat-sublimite.mkv` | 1.031–8.157 (M)[AC] |
| `01-fresh-sublime.mkv` | 0.747–6.610 (M)[AC] |
| `01-fresh-sublimite.mkv` | 0.764–6.557 (M)[AC] |
| `02-paced-sublime.mkv` | 0.748–15.369 (M)[AC] |
| `02-paced-sublimite.mkv` | 0.748–15.535 (M)[AC] |
| `02-paced-verified-r2-sublime.mkv` | 0.756–19.900 (M)[AC] |
| `02-paced-verified-sublime.mkv` | 0.756–13.643 (M)[AC] |
| `02-paced-verified-sublimite.mkv` | 0.756–13.719 (M)[AC] |
| `02-typing-r2-sublime.mkv` | 0.758–13.719 (M)[AC] |
| `02-typing-r2-sublimite.mkv` | 0.750–13.602 (M)[AC] |
| `03-nav-sublime.mkv` | 0.757–17.783 (M)[AC] |
| `03-nav-sublimite.mkv` | 0.747–19.604 (M)[AC] |
| `03-selection-sublime.mkv` | 0.747–7.171 (M)[AC] |
| `03-selection-sublimite.mkv` | 0.755–7.197 (M)[AC] |
| `04-mouse-sublime.mkv` | 0.757–10.378 (M)[AC] |
| `04-mouse-sublimite.mkv` | 0.757–10.561 (M)[AC] |
| `05-edit-r2-sublime.mkv` | 0.757–9.112 (M)[AC] |
| `05-edit-r2-sublimite.mkv` | 0.761–8.723 (M)[AC] |
| `05-edit-sublime.mkv` | 0.743–28.204 (M)[AC] |
| `05-edit-sublimite.mkv` | 0.758–29.869 (M)[AC] |
| `06-alternate-sublime.mkv` | 0.748–6.968 (M)[AC] |
| `06-alternate-sublimite.mkv` | 0.754–6.779 (M)[AC] |
| `06-clipboard-sublime.mkv` | 0.754–9.246 (M)[AC] |
| `06-clipboard-sublimite.mkv` | 0.757–9.475 (M)[AC] |
| `06-cross-back-sublime.mkv` | 0.741–1.790 (M)[AC] |
| `06-cross-source-sublime.mkv` | 0.758–2.924 (M)[AC] |
| `06-cross-sublimite.mkv` | 0.748–5.381 (M)[AC] |
| `07-find-sublime.mkv` | 0.755–7.679 (M)[AC] |
| `07-find-sublimite.mkv` | 0.744–8.439 (M)[AC] |
| `08-save-sublime.mkv` | 0.745–9.424 (M)[AC] |
| `08-save-sublimite.mkv` | 0.743–9.387 (M)[AC] |
| `09-tabs-sublime.mkv` | 0.760–43.731 (M)[AC] |
| `09-tabs-sublimite.mkv` | 0.756–41.788 (M)[AC] |
| `10-large-clean-sublime.mkv` | 0.566–88.698 (M)[AC] |
| `10-large-early-sublimite.mkv` | 0.697–46.915 (M)[AC] |
| `10-large-loaded-sublime.mkv` | 0.684–340.799 (M)[AC] |
| `10-large-resume-sublime.mkv` | 0.711–57.195 (M)[AC] |
| `10-large-sublime.mkv` | 0.756–96.548 (M)[AC] |
| `10-large-sublimite.mkv` | 0.754–84.880 (M)[AC] |
| `11-resize-sublime.mkv` | 0.815–11.874 (M)[AC] |
| `11-resize-sublimite.mkv` | 0.756–11.991 (M)[AC] |
| `12-hotexit-sublime.mkv` | 0.756–24.200 (M)[AC] |
| `12-hotexit-sublimite.mkv` | 0.761–23.349 (M)[AC] |
| `12-recovery-conflict-sublimite.mkv` | 0.751–9.117 (M)[AC] |
| `13-minimap-sublime.mkv` | 0.748–10.908 (M)[AC] |
| `13-minimap-sublimite.mkv` | 0.750–11.325 (M)[AC] |
| `14-idle-sublime.mkv` | 0.757–63.848 (M)[AC] |
| `14-idle-sublimite.mkv` | 0.761–64.026 (M)[AC] |
| `15-mouse-calibrated-sublime.mkv` | 0.715–42.576 (M)[AC] |
| `15-mouse-calibrated-sublimite.mkv` | 0.590–49.579 (M)[AC] |
| `15-mouse-sublime.mkv` | 0.745–26.597 (M)[AC] |
| `15-mouse-sublimite.mkv` | 0.733–26.408 (M)[AC] |
| `15-mouse-target-ready-sublime.mkv` | 0.728–133.753 (M)[AC] |
| `15-mouse-wrapped-sublime.mkv` | 0.749–19.991 (M)[AC] |
| `15-mouse-wrapped-sublimite.mkv` | 0.745–19.878 (M)[AC] |
| `15-wrapped-reverse-sublime.mkv` | 0.727–70.495 (M)[AC] |
| `15-wrapped-reverse-sublimite.mkv` | 0.698–52.637 (M)[AC] |
| `16-find-large-complete-sublime.mkv` | 0.751–536.449 (M)[AC] |
| `16-find-large-sublimite.mkv` | 0.675–114.581 (M)[AC] |
| `16-find-large-wait-sublimite.mkv` | 0.724–55.958 (M)[AC] |
| `16-find-sublime.mkv` | 0.763–40.298 (M)[AC] |
| `16-find-sublimite.mkv` | 0.778–43.121 (M)[AC] |
| `16-original-sequence-sublime.mkv` | 0.768–8.154 (M)[AC] |
| `16-original-sequence-sublimite.mkv` | 0.755–7.878 (M)[AC] |
| `17-recovery-a-sublime.mkv` | 0.760–22.666 (M)[AC] |
| `17-recovery-a-sublimite.mkv` | 0.758–21.864 (M)[AC] |
| `17-recovery-b-sublimite.mkv` | 0.761–22.227 (M)[AC] |
| `17-recovery-c-sublimite.mkv` | 0.757–13.559 (M)[AC] |
| `17-recovery-d-sublimite.mkv` | 0.781–2.728 (M)[AC] |
| `17-recovery-e-sublimite.mkv` | 0.777–5.000 (M)[AC] |
| `18-tabtitle-second-sublime.mkv` | 0.732–8.362 (M)[AC] |
| `18-tabtitle-second-sublimite.mkv` | 0.726–6.791 (M)[AC] |
| `18-tabtitle-sublime.mkv` | 0.744–7.371 (M)[AC] |
| `18-tabtitle-sublimite.mkv` | 0.744–9.109 (M)[AC] |
| `19-external-controlled-sublime.mkv` | 0.680–41.392 (M)[AC] |
| `19-external-controlled-sublimite.mkv` | 0.634–35.394 (M)[AC] |
| `19-external-repro-sublimite.mkv` | 0.735–17.134 (M)[AC] |
| `19-external-sublime.mkv` | 0.750–28.112 (M)[AC] |
| `19-external-sublimite.mkv` | 0.739–29.004 (M)[AC] |
| `20-emoji-carets-sublime.mkv` | 0.707–62.188 (M)[AC] |
| `20-emoji-carets-sublimite.mkv` | 0.385–99.466 (M)[AC] |
| `20-emoji-sublime.mkv` | 0.767–40.084 (M)[AC] |
| `20-emoji-sublimite.mkv` | 0.760–34.615 (M)[AC] |
| `20-font-restore-sublime.mkv` | 0.697–2.861 (M)[AC] |
| `21-font-file-sublimite.mkv` | 0.689–5.068 (M)[AC] |
| `21-font-iosevka-sublimite.mkv` | 0.683–4.870 (M)[AC] |
| `21-font-liberation-mono-sublimite.mkv` | 0.708–4.798 (M)[AC] |
| `21-font-missing-sublimite.mkv` | 0.681–4.958 (M)[AC] |
| `21-font-proportional-sublimite.mkv` | 0.689–4.825 (M)[AC] |
| `22-common-commands-sublime.mkv` | 0.667–20.780 (M)[AC] |
| `22-common-commands-sublimite.mkv` | 0.690–19.493 (M)[AC] |
| `dense64-sublime.mkv` | 0.669–52.693 (M)[AC] |
| `dense64-sublimite.mkv` | 0.707–55.069 (M)[AC] |
| `font12-sublimite.mkv` | 0.762–2.466 (M)[AC] |
| `utf8-sublime.mkv` | 0.759–7.186 (M)[AC] |
| `utf8-sublimite.mkv` | 0.750–6.986 (M)[AC] |
| `zoom-sublime.mkv` | 0.748–4.961 (M)[AC] |
| `zoom-sublimite.mkv` | 0.753–4.820 (M)[AC] |

## Missing features Tobias would hit in daily use

1. **New/Open/Save As/Quit shortcuts:** the recorded sublimité responses are not implemented. A CLI workaround does not supply those daily file operations.
2. **Predictable wheel movement in wrapped prose:** the PRD reversal does not return to the preceding view, and the initial downward burst moves fewer visual rows than the configured policy.
3. **Complete emoji artwork on this font installation:** the woman-technologist sequence still shows one missing-glyph symbol. Layout/byte preservation are improved, but the visual gap remains.
4. **Find in files and code colour:** Ctrl+Shift+F has no working workflow, and C text/minimap remain monochrome.
5. **Search muscle-memory parity:** first Return and Ctrl+Shift+H differ; the supported replacement chords do work.

## Edge cases and defects

- **Wrapped wheel distance/reversal:** open the copied PRD, go home, wheel down three then thirty notches and up thirty; sublimité returns to line 1 rather than the prior line-4 view. Reproduced with individual upward notches.
- **One renderer error near normal ending:** in the controlled shrink→dirty/R→dirty/K→copy sequence, stderr records error -11 and the later close sees BadWindow. Last captured text is intact. The same fresh sequence passes and exits 0; cause and whether the failure is editing- or shutdown-specific remain unknown. Preserve its trace/journal for coordinator triage.
- **Unsupported daily file commands:** Ctrl+N/O/Shift+S/Q leave the original sublimité buffer and show not implemented.
- **Search first-Return difference:** Ctrl+F, type alpha, Return commits the first match in sublimité while Sublime advances.
- **Replacement habit difference:** Ctrl+Shift+H in sublimité does not replace; Ctrl+Enter does.
- **Gutter selection difference:** click a gutter line; Sublime selects the line while sublimité places its caret at line start.
- **Duplicate path tabs:** open the Unicode file, another file, then the same Unicode path via CLI; sublimité adds another tab.
- **Emoji font gap:** open A👩‍💻B; one missing-glyph symbol occupies the repaired cluster span. Covered-first-emoji sequences draw the first artwork only.
- **Comparison limits retained:** standard Sublime large-file frames remain blank; its inherited dense run shows no requested end/Z result; early new mouse attempts had wrong geometry or loading frames. Successful follow-ups are separately named. No blank or stale-window result is counted as a completed action.
- **Coverage limit:** estimate→exact index switching was not visible at the specified sample times. Correct final labels do not prove that unobserved transition.

## Latency and jank by feel plus numbers

Ordinary typing, selection and clipboard operations are visibly responsive in the completed runs. The former inert mouse and destructive query flow are gone. The remaining frustration is functional: wrapped-wheel movement is inconsistent, daily file commands are absent, and one renderer ending failed unexpectedly. No old orange minimap pulse was reproduced. Large-file editing works once the requested view is available; the rare searches complete without preventing query-field updates. Larger late samples are notes under the recorded load, not budget failures.

Small-file trace, literal `traces/02-typing-r2-sublimite.txt` values, all (M)[AC]:

```text
G1 T4-T0     n=92 p50=1.575 ms p99=40.395 ms
G3 T5-T0     n=91 p50=1.753 ms p99=18.295 ms
T6-T0        n=91 p50=16.961 ms p99=32.870 ms
```

Drive-2 small-file reference, quoted (M), power unstamped:

```text
G1 T4-T0     n=93 p50=1.782 ms p99=66.562 ms
G3 T5-T0     n=92 p50=1.940 ms p99=17.049 ms
T6-T0        n=92 p50=17.750 ms p99=30.953 ms
```

Large-file trace, literal `traces/10-large-sublimite.txt` values, all (M)[AC]:

```text
G1 T4-T0     n=27 p50=4.719 ms p99=65.024 ms
G3 T5-T0     n=26 p50=6.781 ms p99=110.772 ms
T6-T0        n=26 p50=25.107 ms p99=124.275 ms
```

Drive-2 large-file reference, quoted (M), power unstamped:

```text
G1 T4-T0     n=27 p50=5.711 ms p99=59.721 ms
G3 T5-T0     n=26 p50=7.171 ms p99=18.738 ms
T6-T0        n=26 p50=29.580 ms p99=40.543 ms
```

Startup first_present markers (M)[AC], literal trace lines, not substituted for video first text:

```text
01-fresh-sublimite: first_present        ns=9714681254497 elapsed_ms=133.145605
10-large-sublimite: first_present        ns=11613564043018 elapsed_ms=144.726091
```

The trace sample counts are exactly those printed by tracedump; the large run includes navigation as driven, not only character insertion. Individual external glyph timings and these internal percentiles measure different things. The post-capture replacement executable has not been assessed by this report.

VERDICT usable instead of Sublime for Tobias's daily edits: no; blockers: missing New/Open/Save As/Quit commands, incorrect wrapped-wheel scrolling, missing emoji artwork, unexplained renderer exit near close.
