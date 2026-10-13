# Astra vs Sublime Text: comparison drive 4

Final evidence review · Astra · edit-w34.4 / P5.1b. Daily file commands, wrapped scrolling, find keys and raster stall handling now pass the recorded probes. Missing emoji artwork and find in files still prevent a full daily replacement.

## Conditions

Only DISPLAY=:98. Restricted-shell connection failed; escalated xdpyinfo confirmed the requested display. Binary is build/astra4/sublimite; coordinator-supplied build/astra4/tracedump is used. BUILD still identifies 617fa430a40808df89b13c968e1a04096f93b45f as requested; continuing. BUILD claims are not behavioral verification.

```text
commit 617fa430a40808df89b13c968e1a04096f93b45f
built 2026-10-13T05:36:44+10:00
BAT0 Not charging
beads landed in session 12: 91c 2cs 2b8 u82 pgr rfq o20 x8i v0o.3 v0o.4 v0o.5 v0o.6 pd6 37a 1vo bml 3u4 9u3 5ih jf9 xzw 48u 1um vak tn2 rzj cuh 1zm(partial) 2bl 379 zw9 g02 97i 11v cee 9n7 r1l
not landed: fi2 (design)
also landed after the first staging: dr3 228ef3b, 5u2 eeda977, 848 617fa43, cxo b699bba+dfdb6b4, cdc ff9d7de
```

Start binary and power/load measurement (M)[AC]:

```text
2026-10-13T07:39:22+10:00
Not charging
1
0.64 0.42 0.21 2/1511 7
2026-10-13 05:36:44.598229658 +1000 9873912
ee77869520b159ca852f7baa53bd29d2a950cfb3c66b9c8c7e568cf97df7d169  build/astra4/sublimite
```

Harness copies preserve input sequences. Output paths, pinned binary and tracedump paths, and report destinations were redirected; literal recursive diff is in the worker log. Older evidence remains read-only. Fixture copy hashes: build/astra4/logs/fixture-copy-hashes.json.



Finalisation used existing files only: neither editor was launched, and no new screen capture was made. All paths below are relative to `build/astra4/`: videos in `video/`, stills and extracted sequences in `frames/`, measurements and command/readback logs in `logs/`, internal trace dumps in `traces/`. A still proves a sampled state, not absence of transient jank. The capture review ledger replaces the draft's unanalysed capture bullets, including failed and superseded attempts.

All measured values in a table row inherit its explicit **(M)[AC] or (M)[bat], load1=…** evidence stamp, including pixel/RGB/line/byte counts, hashes, timestamps and return codes. Coordinates and thresholds are analysis parameters; task numbers, bead/build identifiers, prescribed key counts and fixture sizes are identifiers or inputs, not timing budgets. Per-capture AC/online decides the power label even when an old analysis helper hard-coded AC. A blank or stale comparison is **NOT OBSERVED**, never a product failure or a pass. **STILL PRESENT** also labels successful behavior retained from drive 3. **NEW** means new coverage or a newly observed divergence, not necessarily newly introduced code.

Realtime and file mtimes in the interrupted interval are not ordering evidence. Use monotonic timestamps where present and within-capture envelopes otherwise. `clock-discontinuities.json` identifies the backward jump in `16-find-large-wait-sublime`; later timing and F3 results from that video are excluded. `16-find-large-stable-sublime` supplies a separate rare-match result. No timing is inferred across the jump. The header date of a file does not override its capture power stamp.

`logs/binary.txt` records the same size, mtime and SHA256 at all saved checkpoints. The final recorded checkpoint is (M)[bat], load1=1.37: size 9,873,912 bytes; mtime `2026-10-13 05:36:44.598229658 +1000`; SHA256 `ee77869520b159ca852f7baa53bd29d2a950cfb3c66b9c8c7e568cf97df7d169`. This is recorded binary consistency, not a reconstruction from unreliable evidence-file mtimes. Some follow-up captures occurred after the checkpoint labelled END; there is no separate final binary checkpoint for those follow-ups. The executable was not rebuilt in finalisation.

BUILD plus behavior establishes edit-11v (C/Rust colour), edit-zw9 (wrapped registration/reversal), edit-g02 (first Return/replacement), edit-97i (identity reuse), edit-cee (raster stall survival), edit-1um (bottom prompt), edit-48u (bottom-edge autoscroll and panel resize), edit-r1l (sampled cache-pressure pages), edit-rzj (both orphan-session buffers), edit-dr3 (gutter selection), and edit-rfq (journal retirement). Early large-file movement supports edit-91c/pd6/jf9 at the user-visible level; internal index completion and edit-xzw's estimate flag remain unobserved. edit-fi2 is absent in BUILD and changed-base recovery still sets edits aside. edit-3u4/9u3 and the GL stall path have no behavioral verification here. Syntax of other optional/default languages beyond recorded C/Rust is NOT OBSERVED.

Measurement convention: every video onset is a single observation at resampled 60 Hz (P: 1000/60 = 16.666… ms per interval). Command envelopes bound injection, not physical key-to-photon time. Stills support text/layout only. Values inherit the power/load stamp printed beside their evidence. Task identifiers, key counts and fixture sizes specified by the brief are input parameters, not budgets. Sublime build 4200 is confirmed by logs/subl-help.txt; User settings select font_size 11 without a colour override, and the installed Default preferences select Mariana.sublime-color-scheme (logs/sublime-default-scheme.txt).

## 1. Launch, glyphs, syntax and zoom

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Fresh C first matching text | 15.17 intervals / 252.8 ms; command 0.763875–0.841061 s → frame 1.016667 s. | 8.25 intervals / 137.5 ms; command 0.945824–1.022425 s → frame 1.083333 s. | STILL PRESENT — functioning workflow; new measurement; edit-11v colour separately below. Loads differ from drive 3; no regression claim. | `01-fresh-sublimite.mkv`: painted 6.430s; (M)[AC], load1=0.39; `01-fresh-sublime.mkv`: painted 6.165s; (M)[AC], load1=0.48; logs/startup-metrics.json; inherited ROIs |
| Default ink and window | 11 px ink; 800×600. | 26 px ink; 1728×1260. | STILL PRESENT — edit-1nm. | logs/presentation-metrics.json: threshold max RGB >130; ROI sublimité (140,42,95,40), rows 46–71; Sublime (91,60,41,20), rows 64–74; geometry from xdotool. (M)[AC], load1=0.39; (M)[AC], load1=0.48 |
| Sub-cell margin strips | Not applicable to the fixed cell-grid strip. | Right and bottom strips uniformly RGB (32,36,44), equal to text background. | NEW — edit-u82 passing. | 01-fresh-sublimite-painted.png; right ROI (1720,40,8,1200), bottom (0,1240,1728,20), background (1400,100,50,100); logs/presentation-metrics.json; (M)[AC], load1=0.39 |
| C syntax colour | Colour appears with first matched text: 15.17 intervals / 252.8 ms. | Plain text first; colour begins 66.25 intervals / 1104.2 ms after launch, at video 2.050 s. | FIXED since drive 3 — edit-11v text colouring present; first parse is visibly delayed. Task 27 covers classification. | logs/presentation-metrics.json; ROI sublimité (120,40,800,120), Sublime (59,60,600,54), >80 pixels with RGB channel spread >40 and max >130. (M)[AC], load1=0.39; (M)[AC], load1=0.48 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Zoom Ctrl+= twice, Ctrl+−, Ctrl+0 | Enlarges twice, shrinks once; Ctrl+0 does not reset font size. | Enlarges twice, shrinks, resets. | STILL PRESENT — passing / retained shortcut difference; edit-1nm. | `zoom-sublimite.mkv`: larger 2.529s, larger2 3.172s, smaller 3.814s, reset 4.438s; (M)[AC], load1=2.52; `zoom-sublime.mkv`: larger 2.551s, larger2 3.188s, smaller 3.895s, reset 4.605s; (M)[AC], load1=2.40 |
| EDIT_FONT_SIZE_PT=12 | Not applicable. | Override renders larger text: 31 px ink (M)[AC], load1 from the cited font12 capture; ROI (180,50,120,42), rows 55–85, max RGB >130. | STILL PRESENT — working override; measured ink threshold gives 31 px versus drive 3’s 32; coloured C changes the glyph RGB. Font override still works; edit-1nm. | `font12-sublimite.mkv`: font 2.309s; (M)[AC], load1=2.77 |

## 2. Typing and isolated glyph onset

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Isolated Z | 1.98 intervals / 33.0 ms; command 4.733695–4.739630 s → frame 4.766667 s. | 3.03 intervals / 50.5 ms; command 4.866161–4.872511 s → frame 4.916667 s. | STILL PRESENT — functioning workflow; new measurement; no bead. Compared back-to-back in the same minute; drive-3 loads differ. | logs/typing-metrics.json; thirty cropped frames per editor; inherited ROIs (59,63,7,11) and (40,46,20,27). (M)[AC], load1=0.52; (M)[AC], load1=0.77 |
| Original paced paragraph and zero-delay burst | Complete paragraph and burst in order. | Complete paragraph and burst in order. | STILL PRESENT — passing; no bead. | `02-typing-r2-sublimite.mkv`: slow 11.759s, burst 12.955s; (M)[AC], load1=0.52; `02-typing-r2-sublime.mkv`: slow 11.546s, burst 12.745s; (M)[AC], load1=0.77 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Deadline-paced clipboard verification | Both verification runs exactly match expected paragraph. | Exactly matches expected paragraph. | STILL PRESENT — passing; no bead. | `02-paced-verified-sublimite.mkv`: selected 13.589s; (M)[AC], load1=8.48; `02-paced-verified-sublime.mkv`: selected 13.434s; (M)[AC], load1=8.00; `02-paced-verified-r2-sublime.mkv`: selected 19.598s; (M)[AC], load1=6.98; paragraph_match=true in all task logs |

## 3. Navigation and selection

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Held Shift | Selects alp. | Selects alp. | STILL PRESENT — passing; no bead. | `03-selection-sublimite.mkv`: held-three 6.851s; (M)[AC], load1=1.72; `03-selection-sublime.mkv`: held-three 6.941s; (M)[AC], load1=1.74 |
| Arrows, word/line/document ends and Shift selections | Caret/selection follow the inherited sequence. | Caret/selection follow the inherited sequence. | STILL PRESENT — passing; no bead. | `03-nav-sublimite.mkv`: 20-shift+Down 18.776s; (M)[AC], load1=1.18; `03-nav-sublime.mkv`: 20-shift+Down 17.125s; (M)[AC], load1=1.13 |
| PageDown/PageUp view placement | Original sequence captured; different viewport placement. | Sampled tops remain 33→34→33. | STILL PRESENT — placement divergence; no bead. | `03-nav-sublimite.mkv`: 13-Next 13.622s, 14-Next 14.379s, 15-Prior 15.106s; (M)[AC], load1=1.18; corresponding Sublime video and frames |

## 4. Original mouse sequence

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Click / drag / double-click | Caret, selection, word response. | Caret and drag respond; inherited double-click target is punctuation at this grid. | STILL PRESENT — passing; no bead. | `04-mouse-sublimite.mkv`: click 4.231s, drag 5.247s, double 6.348s; (M)[AC], load1=2.02; `04-mouse-sublime.mkv`: click 4.125s, drag 4.909s, double 5.875s; (M)[AC], load1=2.58 |
| Wheel down six / up three | Scrolls. | Top 1→19→10: +18 and −9 rows from gutter reads; three rows per notch. | STILL PRESENT — passing; edit-zw9 is wrapped coverage only. | `04-mouse-sublimite.mkv`: launch 1.872s, wheel-down 7.847s, wheel-up 9.047s; (M)[AC], load1=2.02 |
| Minimap click / drag | Viewport moves. | Viewport moves; gutter tops are 4,739 after click and 6,675 after drag, read from the named stills. | STILL PRESENT — navigation passes; edit-1nm presentation. | `04-mouse-sublimite.mkv`: minimap-click 10.063s, minimap-drag 10.889s; (M)[AC], load1=2.02; `04-mouse-sublime.mkv`: minimap-click 9.291s, minimap-drag 10.063s; (M)[AC], load1=2.58 |

## 5. Editing and undo/redo

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Inherited insert / undo / redo loop | Baseline returns and digits restore. | Baseline returns and digits restore. | STILL PRESENT — passing; no bead (edit-pgr crash atomicity is task 32). | `05-edit-sublimite.mkv`: 50-inserts 15.490s, 50-undo 20.471s, 50-redo 25.704s; (M)[AC], load1=2.31; `05-edit-sublime.mkv`: 50-inserts 15.730s, 50-undo 20.715s, 50-redo 25.869s; (M)[AC], load1=2.44 |
| Select-all/delete, Ctrl+L, Ctrl+Backspace/Delete | Edits and restoration work. | Edits and restoration work. | STILL PRESENT — passing; no bead. | `05-edit-r2-sublimite.mkv`: single-line 4.922s, line-deleted 5.564s, undo 6.198s, delete-word 8.638s; (M)[AC], load1=3.03; `05-edit-r2-sublime.mkv`: single-line 4.922s, line-deleted 5.523s, undo 6.185s, delete-word 8.513s; (M)[AC], load1=3.26 |

## 6. Clipboard

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Ctrl+C/X/V, Shift+Insert | Readbacks COPYME then COPYMECOPYME. | Same readbacks. | STILL PRESENT — passing; edit-bml CRLF is task 33. | `06-clipboard-sublimite.mkv`: copy 4.764s, cut 7.301s, shift-insert 8.368s; (M)[AC], load1=2.91; `06-clipboard-sublime.mkv`: copy 4.587s, cut 7.026s, shift-insert 8.125s; (M)[AC], load1=2.92 |
| PRIMARY middle-click | Inserts PRIMARY_PASTE at inherited pixel location. | Inserts into secondPRIMARY_PASTE line; different grid makes the same pixel a different position. | STILL PRESENT — passing; no bead. | `06-clipboard-sublimite.mkv`: middle 9.211s; (M)[AC], load1=2.91; `06-clipboard-sublime.mkv`: middle 8.962s; (M)[AC], load1=2.92 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Ctrl+Insert / Shift+Insert / Shift+Delete | COPYME and COPYMECOPYME readbacks. | Same readbacks. | STILL PRESENT — passing; edit-bml CRLF is separate. | `06-alternate-sublimite.mkv`: copied-pasted 4.172s, cut 5.720s, pasted 6.756s; (M)[AC], load1=2.78; `06-alternate-sublime.mkv`: copied-pasted 11.452s, cut 13.005s, pasted 14.152s; (M)[AC], load1=2.74 |
| Sublime → sublimité → Sublime | Receives final round-trip text. | Receives Sublime text and supplies appended FROM_SUBLIMITE. | STILL PRESENT — passing; no bead. | `06-cross-sublimite.mkv`: from-sublime 3.573s, copy-back 6.456s; (M)[AC], load1=5.59; `06-cross-back-sublime.mkv`: paste-back 1.576s; (M)[AC], load1=6.24; xclip command output in logs; launch-only first Sublime attempt excluded |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Full-path verified cross-editor round trip | Final clipboard contains FROM_SUBLIME, FROM_SUBLIME, FROM_SUBLIMITE on successive lines. | Same exact clipboard bytes. | STILL PRESENT — clipboard interoperability; no bead. | `06-cross-fullpath-r2-sublimite.json` xclip stdout, (M)[AC], load1=1.70; `06-cross-back-fullpath-r2-sublime.json`, (M)[AC], load1=2.05 |

## 7. Original find sequence

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Ctrl+F, alpha, Return, Escape, F3, Ctrl+H, Escape | Find/replace panels work; final alpha selected. | Panels work; final alpha selected in this single-match fixture. | STILL PRESENT — this fixture cannot distinguish first-Return advance; edit-g02 is tested with multiple matches in tasks 16/25. | `07-find-sublimite.mkv`: find-panel 3.995s, find-query 5.150s, next-match 6.638s, replace-panel 7.423s; (M)[AC], load1=3.17; `07-find-sublime.mkv`: find-panel 3.918s, next-match 6.440s; (M)[AC], load1=2.83 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Full-buffer hash after exact inherited find sequence | NOT OBSERVED: original-sequence readback is empty; cannot certify unchanged contents from this attempt. | Readback matches the original fixture: 12,898 bytes, SHA256 e50ddfdce3719854bda9cca11a40311ec9946634c11ec796a1c2ed1e7190022d. | STILL PRESENT — sublimité query ownership; NOT OBSERVED for matched Sublime hash; edit-x8i. | `16-original-sequence-sublimite.mkv`: launch 1.851s; original-sequence 6.895s; final 7.165s; (M)[AC], load1=0.81; `16-original-sequence-sublime.mkv`: launch 6.864s; original-sequence 11.952s; final 12.400s; (M)[AC], load1=1.07 |

## 8. Save and external append

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Ctrl+S and disk cat | EDITED_ saved. | EDITED_ saved, clean. | STILL PRESENT — passing; no bead. | `08-save-sublimite.mkv`: after-save 4.787s; (M)[AC], load1=2.79; `08-save-sublime.mkv`: after-save 4.816s; (M)[AC], load1=2.99; literal cat output in task JSON |
| External append on a clean buffer | Append appears without prompt. | Append appears without prompt. | STILL PRESENT — passing; no bead (edit-37a dirty branch is task 30). | `08-save-sublimite.mkv`: external-focused 7.070s, external-refocus 9.006s; (M)[AC], load1=2.79; `08-save-sublime.mkv`: external-focused 7.050s, external-refocus 9.021s; (M)[AC], load1=2.99 |



## 9. Tabs and CLI burst

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| CLI opens and tab keys | Distinct opens and tab keys captured. | Distinct CLI opens accepted; cycling, close and reopen exercised. | STILL PRESENT — passing; edit-97i same-file reuse tested separately. | `09-tabs-sublimite.mkv`: five-opened 5.844s, tab1 6.657s, tab2 7.373s, close 8.190s, reopen 8.856s, hundred 40.162s, hundred-cycle 40.876s, hundred-reopen 41.974s; (M)[AC], load1=5.80 |
| CLI burst and inotify | All 105 CLI calls returned zero (five initial + hundred burst, counted from logged command pairs). (M)[AC], load1=3.94 | All 105 CLI calls return zero (five initial + hundred burst, counts from logged command pairs); one inotify descriptor. | STILL PRESENT — passing; no bead. | 09-tabs-sublimite.json: inotify_instances count=1, /proc fd-link enumeration; (M)[AC], load1=5.80 |

## 10. Large file (settled run)

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Y | Loaded-session result is in the latency inventory; original action window did not establish this result. | 3.30 intervals / 55.0 ms; command 57.595006–57.599718 s → frame 57.650000 s. | STILL PRESENT — functioning workflow; new measurement; edit-91c / edit-pd6; task 31 isolates early indexing. | 10-large-sublimite.mkv; ROI [160, 46, 20, 27]; logs/large-metrics-sublimite.json; (M)[AC], load1=3.29 |
| Z | Loaded-session result is in the latency inventory; original action window did not establish this result. | 2.56 intervals / 42.7 ms; command 61.907312–61.914434 s → frame 61.950000 s. | STILL PRESENT — functioning workflow; new measurement; edit-91c / edit-pd6; task 31 isolates early indexing. | 10-large-sublimite.mkv; ROI [160, 46, 20, 27]; logs/large-metrics-sublimite.json; (M)[AC], load1=3.29 |
| end-navigation | Loaded-session result is in the latency inventory; original action window did not establish this result. | 3.30 intervals / 55.1 ms; command 47.261604–47.290633 s → frame 47.316667 s. | STILL PRESENT — functioning workflow; new measurement; edit-91c / edit-pd6; task 31 isolates early indexing. | 10-large-sublimite.mkv; ROI [0, 46, 140, 27]; logs/large-metrics-sublimite.json; (M)[AC], load1=3.29 |
| startup | Loaded-session result is in the latency inventory; original action window did not establish this result. | 9.29 intervals / 154.8 ms; command 0.761865–0.880591 s → frame 0.916667 s. | STILL PRESENT — functioning workflow; new measurement; edit-91c / edit-pd6; task 31 isolates early indexing. | 10-large-sublimite.mkv; ROI [180, 46, 100, 27]; logs/large-metrics-sublimite.json; (M)[AC], load1=3.29 |
| Ctrl+End / PageUp sequence | Original input window mostly blank; loaded-session capture shows EOF and edits. No early-input success inferred. | EOF label 8,947,842; after the inherited PageUps the top is 8,947,542; Y at EOF and Z at start are visible. | STILL PRESENT — passing settled navigation; edit-91c / edit-pd6 / edit-jf9 early behaviour separate. | `10-large-sublimite.mkv`: end-before-edit 52.291s, 20-pageup 55.148s, end-edit 59.604s, first-edit 63.917s, end-after-wait 81.295s; (M)[AC], load1=3.29 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Short-dwell large run | Loaded-session comparison is `10-large-loaded-sublime.mkv`; input preceded readiness in the original run. | Reaches EOF, pages back, accepts Y and Z; closes normally. | STILL PRESENT — passing; edit-91c / edit-pd6 / edit-jf9 early-index isolation is task 31. | `10-large-early-sublimite.mkv`: end-before-edit 11.278s, 20-pageup 14.098s, end-edit 18.565s, first-edit 22.844s; (M)[AC], load1=5.06 |
| Dense short lines | Reaches last line 26,845,451 and shows Z at line 1. This result was not visible in drive 3’s inherited run. | Reaches last line 26,845,451 and shows Z at line 1. | STILL PRESENT — Sublime comparison now completes; sublimité retained passing behavior; no bead. | `dense64-sublimite.mkv`: end 49.135s, typed 50.648s; (M)[AC], load1=3.42; `dense64-sublime.mkv`: end 49.246s, typed 50.798s; (M)[AC], load1=3.58 |

## 11. Resize

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Inherited size sequence with PageDown/Up | Resizes and redraws. | Resizes and redraws; no persistent garbage visible at the named sizes. | STILL PRESENT — passing; edit-48u find-panel variant is task 16. | `11-resize-sublimite.mkv`: 1440x900 4.720s, 2880x1800 6.798s, 800x600 9.081s, 960x600 11.298s; (M)[AC], load1=2.70; `11-resize-sublime.mkv`: 1440x900 4.569s, 2880x1800 6.637s, 800x600 8.765s, 960x600 10.848s; (M)[AC], load1=3.40 |

## 12. Hot exit

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| SIGKILL with unchanged base | Relaunch shows disk baseline without the unsaved marker. | Relaunch restores UNSAVED_RECOVERY_MARKER_ and dirty title; disk remains baseline. | STILL PRESENT — passing sublimité recovery and retained Sublime result; edit-pgr / edit-rzj add crash shapes in task 32. | `12-hotexit-sublimite.mkv`: before-kill 4.323s, killed 17.685s, relaunch 23.280s; (M)[AC], load1=3.16; `12-hotexit-sublime.mkv`: before-kill 4.266s, killed 17.542s, relaunch 23.296s; (M)[AC], load1=3.12 |
| SIGKILL with replaced base | Full matched case in task 17. | Opens external replacement cleanly with the set-aside notice. | STILL PRESENT — passing; edit-fi2 absent by BUILD and behaviour. | `12-recovery-conflict-sublimite.mkv`: relaunch 8.067s; (M)[AC], load1=2.48 |

## 13. Minimap and orange

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Minimap appearance and edits | Inherited run is blank/loading; no completed minimap comparison claimed from it. Fresh C task 1 shows the coloured miniature. | Monochrome stripes remain next to coloured main text; navigation and edit/undo captured. | STILL PRESENT — edit-1nm; edit-11v explicitly excludes minimap. NOT OBSERVED for the original blank Sublime run; corrected repeat below. | `13-minimap-sublimite.mkv`: Next 4.656s, Next 5.785s, ctrl+End 6.955s, ctrl+Home 9.592s, edited 12.193s; (M)[AC], load1=3.25; `13-minimap-sublime.mkv`: edited 12.631s; (M)[AC], load1=4.13; 01-fresh-sublime-painted.png |
| Three-colour flash scan | Not requested as a whole-suite scan. | Every saved sublimité video has completed fixed-ROI scans; per-video results are in the flash audit. | STILL PRESENT — no old minimap pulse detected; edit-1nm. Fixed ROI alone does not cover a moving minimap. | logs/*.flash-*.txt; fixed and tracked scan coverage is audited below. |
| Other orange UI | Native panels/dialogs where applicable. | Status notices remain deliberate; R/K placement is tested in tasks 19/30. | FIXED since drive 3 — edit-1um bottom placement; deliberate action feedback, not a minimap pulse. | Task 19/30 prompt frames and scan audit. |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Corrected minimap comparison | Full-path repeat displays source, coloured miniature and ORANGE_PROBE edit. | Coloured body next to monochrome minimap. | STILL PRESENT — edit-1nm presentation gap, outside edit-11v scope. | `13-minimap-fullpath-r2-sublime.mkv`: edited 11.677s; (M)[AC], load1=2.52; `13-minimap-sublimite.mkv`: edited 12.193s; (M)[AC], load1=3.25 |

## 14. Idle

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| sublimite focused idle | See corresponding measurement | 20 caret transitions; 0 outside changes. | STILL PRESENT — expected quiet behavior; no bead. | 14-idle-sublimite.mkv; 4.072436–64.073167 s; duration 60.000731 s; ROI [75, 35, 45, 50]; grayscale delta >30, >20 changed pixels; logs/idle-sublimite-metrics.json; (M)[AC], load1=4.76 |
| Sublime focused idle, corrected window | No caret transitions; one outside status-strip change. | See preceding sublimité measurement. | STILL PRESENT — quiet text area. Original stale-window measurement NOT OBSERVED. | `14-idle-fullpath-r2-sublime.mkv`, 4.324585–64.325380s; duration 60.000796s; caret ROI (45,55,50,35), grayscale delta >30 and >20 changed pixels; outside event at 5.116667s; logs/idle-fullpath-r2-sublime-metrics.json; (M)[AC], load1=3.05 |

The moving resize detector reports one event at 8.083–8.100 s in `11-resize-sublimite.mkv` (M)[AC], load1=2.70. The immediately preceding action is the 800×600 resize command at 8.072 s. The inherited tracker moves its crop to x=640 on command time while the old full-width frame is still displayed: it samples orange syntax glyphs in the text body for one frame. This is a tracking false positive from deliberate syntax colours, not a minimap pulse. `resize-flash-8.083333.png`, `logs/resize-tracking.json` and `logs/11-resize-tracked.flash-6b5030.txt` retain the evidence (268 matching pixels; tol24; >200-pixel jump). The other two tracked colours report no flashes.

**Comparison correction:** basename fallback selected restored older Sublime windows in tasks 13–16, UTF-8 and 17(a), and could also affect the clipboard-source retry. These recordings are retained but excluded from behavioral/latency conclusions. The separately named full-path repeats supply the replacement evidence. In particular, the dirty OMEGA found before task 16 input was stale buffer state, not an edit caused by query entry. Old fixture bytes remain unchanged (logs/old-fixtures-integrity.json).

## 15. Mouse targets and wrapped/unwrapped wheel

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| C-file wheel bursts | Full-path repeat supplies the intended C-file view; inherited stale-window attempt excluded. | Gutter tops are 1 → 10 → 100 → 10 → 1 → 91 across before/down3/down30/up30/up3/burst30: three rows per notch, including the zero-delay burst. | STILL PRESENT — passing unwrapped wheel; edit-zw9 wrapped case below. | `15-mouse-sublimite.mkv`: wheel-before 9.805s, down3 11.479s, down30 15.915s, up30 20.264s, up3 21.907s, burst30 23.230s; (M)[AC], load1=2.57; 15-mouse-fullpath-r2-sublime.mkv |
| Wrapped PRD wheel distance and reversal | Full-path matched repeat replaces stale-window attempt. | Three notches move nine visual rows: logical line 7, second wrapped row at top. Reversal restores the prior body with zero changed pixels. | FIXED since drive 3 — edit-zw9. | `15-mouse-wrapped-sublimite.mkv`: wheel-before 4.355s, down3 6.476s, up30 16.412s; (M)[AC], load1=2.17; logs/wrapped-original-metrics.json: RGB delta >0, ROI (120,40,1400,1160); unique zero-error registration at 9×40=360 px. Task 23 adds individual-notch proof. |
| Gutter click | Whole-line comparison is task 28. | Selects the whole line, including its newline (next-row start selected in still). | FIXED since drive 3 — edit-dr3 observed present. | `15-mouse-sublimite.mkv`: gutter 9.033s; (M)[AC], load1=2.57 |
| Calibrated text drag / Shift+click / underscore / triple-click | Inherited calibrated grid is wrong for this Sublime view; retained as an invalid target comparison. Readiness-calibrated repeats give matching drag/extension/word/line clipboard strings (supplement below). | Drag/extension readbacks match the intended spans; word is fn_0; triple click copies the full intended line. | STILL PRESENT — passing sublimité behavior; no bead. NOT OBSERVED for wrong-grid Sublime attempt. | `15-mouse-calibrated-sublimite.mkv`: click-mid 3.417s, drag 4.688s, shift-extend 5.834s, underscore-word 7.054s, triple-line 9.041s; (M)[AC], load1=2.88; clipboard literals in task JSON |
| Past-EOL and gutter | Ready repeat also clamps the past-EOL click; gutter copies a full line. | Past-EOL clamps; gutter now selects line. | FIXED since drive 3 — gutter fix edit-dr3; past-EOL retained. | `15-mouse-calibrated-sublimite.mkv`: past-eol 10.545s, gutter 10.957s; (M)[AC], load1=2.88 |
| Wheel and click with find open | Full-path repeat retains the panel during wheel/click input. | Panel remains; view scrolls and text click places caret. | STILL PRESENT — passing; edit-48u supplementary autoscroll later. | `15-mouse-sublimite.mkv`: panel-wheel 25.384s, panel-click 26.048s; (M)[AC], load1=2.57 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Readiness-calibrated selection comparison | Drag and Shift+click clipboard strings equal sublimité; fn_0 and the complete triple-click line also agree. | Same intended spans; original calibrated runtime run is valid. | STILL PRESENT — passing text selection; no bead. | `15-mouse-target-ready-r2-sublime.mkv`: past-eol 41.403s; gutter 43.495s; final 45.658s; (M)[bat], load1=1.03; `15-mouse-calibrated-sublimite.mkv`: panel-wheel 27.224s; panel-click 27.676s; final 28.379s; (M)[AC], load1=2.88 |
| Hold drag below bottom text edge | View scrolls and selection extends; clipboard reports 14,938 bytes. | View scrolls and selection extends; clipboard reports 2,439 bytes. Different rates/geometries, no speed equivalence claimed. | FIXED since drive 3 — edit-48u bottom-edge autoscroll. Top-edge variant NOT OBSERVED. | `15-autoscroll-sublimite.mkv`: before 2.962s; after 5.644s; (M)[AC], load1=4.03; `15-autoscroll-sublime.mkv`: before 2.541s; after 4.815s; (M)[AC], load1=4.51 |

## 16. Find/replace panel (sublimité results)

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Fixture integrity after query/navigation | Original capture invalid: dirty old file before any input. Full-path fresh repeat has the untouched fixture hash. | Full-buffer SHA256 remains 2e2fe191debe0b060f6733537ed08fc21db19bd75ceec351bb42795eac852e1d. | STILL PRESENT — passing query ownership; edit-x8i routing, edit-g02 navigation. | `16-find-sublimite.mkv`: unchanged 9.222s; (M)[AC], load1=3.23; readback hash in JSON |
| First Return after typing | Task 25 demonstrates matching first-Return advance. | First Return advances to the second match at 1:12. | FIXED since drive 3 — edit-g02. | `16-find-sublimite.mkv`: typed 4.380s, nav-0 5.061s, nav-1 5.733s, nav-2 6.411s, nav-3 7.099s; (M)[AC], load1=3.23 |
| Ctrl+Enter, then Ctrl+Shift+H | Full-path repeat reviewed; task 25 supplies uninterrupted replacement comparison. | Counts progress 1 OMEGA/29 alpha, then 2/28. | FIXED since drive 3 — edit-g02 Ctrl+Shift+H now acts. | `16-find-sublimite.mkv`: one 28.436s, habit 31.031s; (M)[AC], load1=3.23; readback counts and hashes |
| Replace all and single undo | Full-path repeat reviewed; task 25 supplies uninterrupted replacement comparison. | 30/0 after all; one undo restores 2/28, hash c25a0fd86cbbcc7553d836ab0d7d6259e90a2f3a821d79f04d5968e5bcfd7ab2. | FIXED since drive 3 — expected consequence of the newly working extra replacement, not broken undo; edit-g02. | `16-find-sublimite.mkv`: all 33.587s, undo-all 35.480s; (M)[AC], load1=3.23 |
| Field editing and Alt+C/W/R | Full-path repeat reviewed; task 25 supplies uninterrupted replacement comparison. | Query-field operations do not mutate the document. Match totals read 30 initially, 20 with case enabled, 10 with whole-word also enabled, and 30 after reset plus regex al.ha. | STILL PRESENT — passing; edit-x8i. | `16-find-sublimite.mkv`: field-0 11.856s, field-6 15.692s, case 17.523s, whole 18.598s, regex 21.608s; (M)[AC], load1=3.23 |
| Find in files | Full-path repeat reviewed; task 25 supplies uninterrupted replacement comparison. | Unsupported workflow remains. | STILL PRESENT — edit-xkd (M2). | `16-find-sublimite.mkv`: find-in-files 36.216s; (M)[AC], load1=3.23 |
| Small-window panel then growth | Full-path repeat shows the panel at the requested restored size. | Find row remains intact after shrink and grow. | STILL PRESENT — passing at the inherited sizes; edit-48u. | `16-find-sublimite.mkv`: small-panel 38.409s, restored-panel 40.119s; (M)[AC], load1=3.23 |
| Large-file rare search / F3 / running query input | Stable-clock loaded-session rare match is visible; see latency inventory. | Rare match and ready F3 steps are visible; query characters continue to update during search. | STILL PRESENT — rare search and ready navigation work; different loads/power preclude a regression ranking; no bead. | `16-find-large-wait-sublimite`, `16-find-large-stable-sublime`, and query/late metrics inventory below. |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Corrected full-path fixture and replacement counts | Untouched hash matches; one replacement gives 1/29, all gives 30/0, undo returns 1/29. | Extra working Ctrl+Shift+H in runtime sequence yields 2/28 before all and after undo. | FIXED since drive 3 — edit-g02; different pre-all counts follow the key sequence, not undo corruption. | `16-find-fullpath-r2-sublime.mkv`: unchanged 9.700s; one 28.547s; undo-all 33.032s; (M)[AC], load1=2.46; (M)[AC], load1=3.23 |

## 17. Recovery matrix

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| (a) changed base | Full-path matched repeat opens replacement; original launch attempt excluded. | Opens clean disk replacement with set-aside notice; next edit clears notice. | STILL PRESENT — passing; edit-fi2 absent as BUILD states. | `17-recovery-a-sublimite.mkv`: notice 21.399s, notice-after-edit 22.466s; (M)[AC], load1=5.36; `17-recovery-a-fullpath-r2-sublime.mkv`: notice 26.427s; (M)[AC], load1=2.12; listings and set_aside records in logs |
| (b) same-bytes rewrite | Not required in Sublime. | Opens disk bytes with set-aside notice. | STILL PRESENT — passing; edit-fi2 absent. | `17-recovery-b-sublimite.mkv`: notice 20.721s, notice-after-edit 21.546s; (M)[AC], load1=2.41 |
| (c) normal unsaved close and repeated relaunch | Not required in Sublime. | Dirty marker/text restore on each requested relaunch; normal exits recorded. | STILL PRESENT — passing; edit-rfq retirement does not prevent these relaunches. | `17-recovery-c-sublimite.mkv`: relaunch-0 5.697s, relaunch-1 8.463s, relaunch-2 11.631s, relaunch-3 14.965s; (M)[AC], load1=2.65 |
| (d) copied drive-2 conflict state | Not applicable. | Launch succeeds with set-aside handling. Original drive-2 file only opened, never saved. | STILL PRESENT — passing; edit-fi2 absent. | `17-recovery-d-sublimite.mkv`: restored 2.842s; (M)[AC], load1=3.79 |
| (e) fresh ordinary-profile copy | Not applicable. | First no-argument launch opens and exits zero. Second launch exceeds discovery timeout, then opens with more restored tabs and navigation.txt. | NEW — larger-profile recovery coverage; edit-rfq / edit-rzj. Fresh-copy audit below confirms retirement, with a long restoration delay. | `17-recovery-e-sublimite.mkv`: restored 17.590s; (M)[AC], load1=4.05; `17-profile-continuation-sublimite.mkv`: eventual-window 0.769s; (M)[AC], load1=2.71; traces/17-recovery-e-sublimite.txt: first_present elapsed_ms=68503.171250. This onset was outside the first video; no video interval latency is invented. Orphan-process exit status unavailable after harness parent exited; WM_DELETE_WINDOW and disappearance recorded. |

Fresh profile audit: `17-profile-audit-sublimite.json` and `profile-retirement-diff.json` show old active journals removed or converted to set-aside files, and a new session journal created. This supports retirement, but not a claim that every old set-aside/base artifact was evicted or that the oldest-first limit was reached. Both audit launches close with return code 0; (M)[AC], load1=5.97. The copied profile contains large prior sessions, so its slow restoration is not comparable with the fresh-file startup or drive 3’s smaller profile. Recovery opens disk text when the base changed; retained set-aside files are not automatic recovery of those edits.

## 18. Unicode tab titles and same-path CLI reopen

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Unicode title and reopen | Title glyphs paint; CLI may activate an already existing window. | Unicode prefix paints; reopening the same path retains only the original Unicode tab plus navigation.txt, and activates it. | FIXED since drive 3 — duplicate tab edit-97i; title painting STILL PRESENT. | `18-tabtitle-sublimite.mkv`: unicode 2.158s, second-tab 4.480s, away 5.226s, back 5.976s; (M)[AC], load1=2.39; `18-tabtitle-sublime.mkv`: unicode 8.208s, second-tab 10.531s, away 11.162s, back 11.908s; (M)[AC], load1=2.68 |
| Unicode as second tab beside unique ASCII fixture | Two tabs; Unicode name paints and survives switching. | Same visible Unicode prefix; long name is ellipsized. | STILL PRESENT — passing; edit-97i identity coverage expands in task 26. | `18-tabtitle-second-sublimite.mkv`: second 3.118s, away 3.914s, back 4.555s; (M)[AC], load1=3.13; `18-tabtitle-second-sublime.mkv`: second 5.491s, away 6.400s, back 7.365s; (M)[AC], load1=3.28 |

## 19. External changes

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Clean append and Ctrl+Z after reload | Original run became unresponsive during its action window; clipboard results invalid. Fresh repeat recovers the pre-append text on Undo; its later inactive-tab readback is invalid (supplement below). | Silent reload; Ctrl+Z leaves the reloaded text including EXTERNAL_APPEND. | STILL PRESENT — retained history-reset policy; no bead. Sublime undo/reload policy differs; later sequential steps are not equivalent. | `19-external-sublimite.mkv`: clean-reload 6.330s, undo-after-reload 8.226s, after-reload-undo 9.962s; (M)[AC], load1=3.61; literal readback logs |
| Dirty R/K and inactive tab | Original sequential comparison incomplete; controlled case below passes. | R reloads, K preserves KEEP_; inactive clean tab shows INACTIVE_APPEND when revisited. | STILL PRESENT — passing; edit-1um prompt placement separately below. | `19-external-sublimite.mkv`: dirty-prompt-R 16.345s, reloaded-R 17.223s, dirty-prompt-K 20.635s, kept-K 21.532s, inactive-return 28.546s; (M)[AC], load1=3.61 |
| Controlled shrink → dirty/R → dirty/K | Reload/keep hashes equal drive 3’s controlled hashes. | Same exact hashes; normal exit zero. | STILL PRESENT — passing; edit-cee close and edit-1um placement. | `19-external-controlled-sublimite.mkv`: prompt-R 7.998s, reloaded 10.012s, prompt-K 13.201s, kept 15.361s; (M)[AC], load1=0.50; `19-external-controlled-sublime.mkv`: prompt-R 8.780s, reloaded 11.117s, prompt-K 14.332s, kept 16.666s; (M)[AC], load1=1.54; reload d880abcb5e564b731c4e1d2bf7c0869e4555d7b22cc7ca87dc0c2f68b0588219, keep 0074d511e872a0b722d07e4ad1d7130ef05f461b576c0731bb20fa702df402f0 |
| Renderer error near ending | No corresponding failure in controlled run. | Controlled and fresh-repeat runs close normally without error -11. | FIXED since drive 3 in these repetitions — edit-cee; forced stall proof is task 29. | `19-external-repro-sublimite.mkv`: kept 13.682s; (M)[AC], load1=1.64; process_end returncode=0 and stderr tails in both logs |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Original sequential Sublime repeat | Undo removes EXTERNAL_APPEND and makes pre-append text dirty; later kept readback is KEEP_X plus the original numbered lines. Inactive-tab readback is only a newline and its still remains on external-r2.txt: NOT OBSERVED for the intended inactive file. | Undo after clean reload retains appended bytes; controlled R/K runs give matched results. | STILL PRESENT — reload undo-policy difference; NOT OBSERVED for Sublime inactive-file comparison; no bead. | `19-external-r2-sublime.mkv`: after-reload-undo 23.420s; kept 45.306s; inactive 62.858s; (M)[bat], load1=0.18; `19-external-r2-sublime-*.txt`; (M)[AC], load1=3.61 |

## 20. Emoji and UTF-8

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| ZWJ and modifier artwork | Colour emoji; nonstandard smile/ZWJ/laptop shows separate artwork. | Woman-technologist is still a missing-glyph symbol; smile-first and thumb-first covered sequences render first artwork only; standalone smile and heart render. | STILL PRESENT — edit-r8e font gap. | `20-emoji-sublimite.mkv`: default 2.185s, zoom2 3.255s; (M)[AC], load1=1.52; `20-emoji-sublime.mkv`: default 2.896s, zoom2 3.923s; (M)[AC], load1=2.26; logs/font-environment.txt confirms U+1F469 face coverage |
| Bytes after Backspace / undo / save | Original fixture hash restored. | Original fixture hash restored. | STILL PRESENT — passing; no bead for deletion policy. | `20-emoji-sublimite.mkv`: final 30.190s; (M)[AC], load1=1.52; `20-emoji-sublime.mkv`: final 32.288s; (M)[AC], load1=2.26; saved_hash=c47c6c8d60f8b1f6ca84114e49b6163e8eb955686d2678b14b4e56850b011ee0 |
| Original accents / Greek / CJK / smile fixture | Full-path matched repeat replaces stale-window attempt. | Common scripts and injected text render. | STILL PRESENT — passing; edit-r8e exception above. | `utf8-sublimite.mkv`: unicode 3.507s, typed-unicode 6.665s; (M)[AC], load1=2.98; `utf8-fullpath-r2-sublime.mkv`: unicode 4.155s, typed-unicode 7.338s; (M)[AC], load1=3.04 |

Task 19 prompt placement is **FIXED since drive 3 — edit-1um**: `19-external-controlled-sublimite-prompt-R.png` shows Source changed: R reload / K keep in the bottom row while the first text line stays visible. The bottom text occupies the cell row at y=1200–1239 in the default window (M)[AC], load1=0.50.

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Caret steps and Backspace | Deletion readbacks retain A👩‍B, A😀‍B, A👍B, AB, A❤B on the respective lines. | Deletion readbacks are AB on every line; undo/save restores original bytes. B occupies the cell three advances after A. | STILL PRESENT — whole-cluster vs last-code-point deletion policy; no bead. Artwork gap edit-r8e remains. | `20-emoji-carets-sublimite.mkv`: l1-r3 3.940s, l1-backspace 7.105s, l5-deleted 42.152s; (M)[AC], load1=2.43; `20-emoji-carets-sublime.mkv`: l1-r3 77.889s, l1-backspace 81.090s, l5-deleted 111.930s; (M)[AC], load1=3.49; full clipboard text/hashes in logs. Runtime default A/B x=40/100, cell pitch20: (100−40)/20=3, from 20-emoji-sublimite-default.png; (M)[AC], load1=1.52 |

## 21. Font selection

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Iosevka SemiBold / Liberation Mono | Not required for these runtime override cases. | Requested faces are visibly distinct and align with the caret after ABC_ typing. | STILL PRESENT — passing; edit-5ih listed in BUILD. | `21-font-iosevka-sublimite.mkv`: face 2.257s, typed 3.292s; (M)[AC], load1=3.46; `21-font-liberation-mono-sublimite.mkv`: face 2.172s, typed 3.167s; (M)[AC], load1=3.42 |
| Proportional and missing family fallback | Not applicable. | Both fall back to the embedded monospaced appearance. | STILL PRESENT — passing; edit-5ih. | `21-font-proportional-sublimite.mkv`: face 2.284s, typed 3.242s; (M)[AC], load1=3.47; `21-font-missing-sublimite.mkv`: face 2.175s, typed 3.116s; (M)[AC], load1=3.27 |
| Explicit Liberation Mono file | Not applicable. | Explicit file produces the requested monospaced face. | STILL PRESENT — passing; edit-5ih. | `21-font-file-sublimite.mkv`: face 2.272s, typed 3.307s; (M)[AC], load1=3.17; logs/font-video-metrics.json for text/face adoption |


Font adoption timing, `logs/font-gated-metrics.json` (the window gate rejects old-window pixels; sampled ink ROI (70,40,280,36), window gate (1722,100,5,5)):

| Face request | First text → requested/fallback face, video seconds | Swap intervals | Evidence / stamp |
|---|---|---|---|
| iosevka | 0.900000 → 0.966667 | 4 | `21-font-iosevka-sublimite.mkv`; (M)[AC], load1=3.46 |
| liberation-mono | 0.883333 → 0.916667 | 2 | `21-font-liberation-mono-sublimite.mkv`; (M)[AC], load1=3.42 |
| proportional | 0.883333 → 0.883333 | 0 | `21-font-proportional-sublimite.mkv`; (M)[AC], load1=3.47 |
| missing | 0.883333 → 0.883333 | 0 | `21-font-missing-sublimite.mkv`; (M)[AC], load1=3.27 |
| file | 0.883333 → 0.900000 | 1 | `21-font-file-sublimite.mkv`; (M)[AC], load1=3.17 |

## 22. Common commands, inherited short sequence

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Ctrl+N then Escape | New untitled buffer becomes visible after the first early still. | New untitled tab. | FIXED since drive 3 — edit-o20. | `22-common-commands-sublimite.mkv`: new 10.893s; (M)[AC], load1=3.32; `22-common-commands-fullpath-r2-sublime.mkv`: new 10.571s, open 11.921s; (M)[AC], load1=0.80 |
| Ctrl+O then Escape | Native chooser response is checked from video; inherited early still precedes its appearance. | In-window Open file prompt; Escape cancels. | FIXED since drive 3 — edit-o20. | `22-common-commands-sublimite.mkv`: open 12.426s; (M)[AC], load1=3.32; `22-common-commands-fullpath-r2-sublime.mkv`: open 11.921s; (M)[AC], load1=0.80 |
| Ctrl+Shift+S then Escape | Native save chooser with Name: untitled. | In-window Save as prompt; Escape cancels. | FIXED since drive 3 — edit-o20. | `22-common-commands-sublimite.mkv`: save-as 13.943s; (M)[AC], load1=3.32; `22-common-commands-fullpath-r2-sublime.mkv`: save-as 13.255s; (M)[AC], load1=0.80 |
| Ctrl+Q | Immediate inherited probe still lists windows; client return code zero. Later restart is separate evidence; no immediate server-exit claim. | No runtime windows remain; owned process exits zero. | FIXED since drive 3 — edit-o20 runtime exit. Full hot-exit workflow and explicit process-disappearance wait in task 24. | `22-common-commands-sublimite.mkv`: quit 15.542s; (M)[AC], load1=3.32; `22-common-commands-fullpath-r2-sublime.mkv`: quit 14.680s; (M)[AC], load1=0.80; window_after_quit events |

## 23. Wrapped reversal and visual-row scroll

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Three down notches | Initial movement registers as nine visual rows. | Nine visual rows, logical line 7 wrapped row 2 at top. | FIXED since drive 3 — edit-zw9. | `23-wrapped-reverse-sublimite.mkv`: launch 1.895s, base-down3 4.057s; (M)[AC], load1=1.87; logs/wrapped-reversal-analysis.json: exact zero-error registration at 9×40=360px; body ROI (120,40,1400,1160) |
| Thirty individual up notches and reversal | Original capture contains delayed paints; fresh repeat shows three rows per step until the final notch, which is not painted by its final still. No pixel-identical settled reversal is claimed from these captures. | Every up notch moves exactly three rows; final body equals base-down3 with zero changed pixels. | FIXED since drive 3 — edit-zw9. Sublime timing/settling limitation retained. | `23-wrapped-reverse-sublimite.mkv`: down30 8.522s, up-01 9.168s, up-30 31.616s, reversed 32.103s; (M)[AC], load1=1.87; logs/wrapped-reversal-analysis.json, logs/wrapped-r2-single-steps.json; RGB delta >0 |
| Wrapped Ctrl+Down / Ctrl+Up | Matched keys captured. | One visual row per press and view returns after the reverse sequence. | NEW — edit-o20. | `23-wrapped-keys-sublimite.mkv`: before 2.828s, down-3 5.580s, up-3 8.356s; (M)[AC], load1=4.05; `23-wrapped-keys-sublime.mkv`: before 2.745s, down-3 5.172s, up-3 7.622s; (M)[AC], load1=4.05 |

No notch in sublimité’s recorded single-up sequence differs from the expected movement: registration reports three visual rows for each of the thirty notches, with zero body changes after reversal; (M)[AC], load1=1.87. Sublime’s later `23-wrapped-late-sublime` shows a different mid-document view; it does not establish a settled pixel-identical reversal. Mark that exact Sublime comparison NOT OBSERVED rather than treating missing paint as an editor regression. Initial wheel onset measurements are preserved in the latency inventory.

## 24. Daily file workflows

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| New untitled and Open | New buffer accepts NEW_BUFFER_TEXT; GTK Open File chooser opens files-b. | New tab accepts NEW_BUFFER_TEXT; inline Open file prompt opens files-b. | NEW — edit-o20 full workflow; missing-command blocker FIXED. | `24-files-sublimite.mkv`: new 2.525s, new-dirty 3.332s, open-b-prompt 3.872s, open-b-result 5.705s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: new 2.721s, new-dirty 3.543s, open-b-prompt 4.105s, open-b-result 6.356s; (M)[AC], load1=3.06 |
| Missing path and cancel | Chooser rejects absent path; cancellation returns to editor. No file created. | Notice rejects absent path; Escape cancels prompt. No file created. | NEW — edit-o20. | `24-files-sublimite.mkv`: open-missing-result 8.027s, cancelled 10.162s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: open-missing-result 9.239s, cancelled 11.479s; (M)[AC], load1=3.06; disk_state_at_step records |
| Save As untitled / existing file | Writes NEW_BUFFER_TEXT, then copies alpha file to the new path. X/save changes only the copy. | Same disk bytes; title rebinds and clears dirty state after save. Original file remains unchanged. | NEW — edit-o20. | `24-files-sublimite.mkv`: save-untitled-result 14.910s, save-copy-prompt 16.440s, save-copy-result 18.676s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: save-untitled-result 16.228s, save-copy-prompt 17.941s, save-copy-result 20.163s; (M)[AC], load1=3.06; saved-as SHA b068880e964e2b3ac68cd821f21439f5fee145ab51d9ad9f26460e0b837d02db; original debd29241d0ff114ef5f5914063fa74abb97a77573e2e1c320eac1691f26445a; X-copy 6ba1ddf493a94b8c08a15e6c6a560ca356ad8192925e35269f4cf668a8a152ef |
| Save As missing directory | No file created; chooser error/cancel captured. | Failure notice; no file created. | NEW — edit-o20. | `24-files-sublimite.mkv`: save-missing-result 22.496s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: save-missing-result 24.043s; (M)[AC], load1=3.06; disk_state_at_step |
| Escape priority | Native chooser / find / selection sequence captured. | Prompt closes first, then find panel, then selection. | NEW — edit-o20; edit-48u intentionally preserves match on the panel-closing press. | `24-files-sublimite.mkv`: word 24.916s, find 25.623s, open-over-find 26.262s, escape-1 26.875s, escape-2 27.498s, escape-3 28.125s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: escape-1 28.496s, escape-2 29.133s, escape-3 29.783s; (M)[AC], load1=3.06 |
| Ctrl+Down / Ctrl+Up | View scrolls with caret retained. | View scrolls by rows with caret retained. Extra visible-caret probe keeps line 11 in view to verify this directly. | NEW — edit-o20. | `24-scroll-caret-sublimite.mkv`: caret-line11 3.770s, down-5 9.875s, up-5 15.255s; (M)[AC], load1=3.02; `24-scroll-caret-sublime.mkv`: caret-line11 3.862s, down-5 8.741s, up-5 13.449s; (M)[AC], load1=3.72; original line-1 sequence in 24-files videos |
| Quit and hot exit | Actual server disappearance observed after a long accumulated-session shutdown; relaunch restores dirty text. Client return code zero. | Owned process exits zero; relaunch restores dirty text. | NEW — edit-o20 hot exit passes. Session workloads differ; no shutdown speed ranking. | `24-files-sublimite.mkv`: before-quit 38.815s, quit 40.215s, hot-restored 43.878s, hot-restored 45.330s; (M)[AC], load1=4.65; `24-files-sublime.mkv`: before-quit 39.910s, quit 41.289s, hot-restored 98.545s, hot-restored 100.272s; (M)[AC], load1=3.06; restored readback hash a399cae5a392b29949e23f51752139abe064b1c3ee42cf9727c99fb6945e92bd in both; disk stays Xalpha file |

## 25. Find keys and replacement continuity

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Return / Shift+Return / F3 / Shift+F3 | Requested panel-open sequence captured; fixture remains unchanged. | Requested sequence advances/reverses current match with panel open; first Return advances. Fixture remains unchanged. | FIXED since drive 3 — edit-g02; unchanged hash is STILL PRESENT passing. | `25-findkeys-sublimite.mkv`: typed 4.635s, key-1 5.546s, key-2 6.481s, key-3 7.425s, key-4 8.340s, key-5 9.379s, key-6 10.174s, key-7 11.016s, escaped 11.930s; (M)[AC], load1=4.79; `25-findkeys-sublime.mkv`: typed 64.197s, key-1 64.828s, key-7 68.868s, escaped 69.506s; (M)[AC], load1=5.50; unchanged hash 2e2fe191debe0b060f6733537ed08fc21db19bd75ceec351bb42795eac852e1d |
| Escape and selection | Panel closes; last match remains selected. | Panel closes; last match remains selected. | STILL PRESENT — deliberate edit-48u policy. | `25-findkeys-sublimite.mkv`: escaped 11.930s; (M)[AC], load1=4.79; `25-findkeys-sublime.mkv`: escaped 69.506s; (M)[AC], load1=5.50 |
| Ctrl+Shift+H twice, all, single undo | Continuous panel sequence gives 2 OMEGA/28 alpha, then 30/0, undo 2/28. | Same counts and hashes. | FIXED since drive 3 — edit-g02. Initial instrumented Sublime attempt is excluded for the second replacement because full-buffer readback changed document selection between keys. | `25-findkeys-continuous-sublimite.mkv`: one 8.793s, two 10.471s, two-readback 12.968s, all 16.359s, undo-all 19.176s; (M)[bat], load1=4.97; `25-findkeys-continuous-sublime.mkv`: one 8.925s, two 10.597s, two-readback 13.155s, all 16.517s, undo-all 19.175s; (M)[bat], load1=6.59; shared two/undo hash c25a0fd86cbbcc7553d836ab0d7d6259e90a2f3a821d79f04d5968e5bcfd7ab2 |
| Ctrl+Enter | Does not add a replacement in this panel-state readback. | Adds one replacement, leaving 3 OMEGA/27 alpha in the original task-25 run. | STILL PRESENT — documented alternative binding; edit-g02 leaves it intact. | `25-findkeys-sublimite.mkv`: ctrl-enter 29.042s, ctrl-enter 30.661s; (M)[AC], load1=4.79; `25-findkeys-sublime.mkv`: ctrl-enter 84.779s, ctrl-enter 86.235s; (M)[AC], load1=5.50 |

Measured sublimité match starts after typed / Return / Return / Shift+Return / Return / Shift+Return / F3 / Shift+F3 are **1:1 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12**, from `findkeys-positions.png` and the labelled frames; (M)[AC], load1=4.79. The panel remains open; Escape retains the current match. The matching Sublime typed/key-1 through key-7 frames show the same **1:1 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12 → 1:24 → 1:12** selection sequence; (M)[AC], load1=5.50. This is a direct read of the highlighted match starts in those frames. Continuous replacement readbacks, rather than the selection-changing instrumented run, establish the matched replacement counts.

## 26. Reopening paths and aliases

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Absolute / relative / symlink / hard-link / Unicode twice | Readiness-checked repeat reuses absolute/relative spelling; symlink and hard-link create separate views. Unicode CLI can activate another existing window. Inline-open comparison is NOT OBSERVED at its final still. | Existing tabs activate with edits and undo retained; no duplicate for the tested identities. | FIXED since drive 3 — edit-97i. | `26-reopen-sublimite.mkv`: edited 4.830s, absolute 6.133s, relative 8.829s, symlink 11.645s, hardlink 14.408s, unicode-1 17.088s, unicode-2 18.355s, prompt-reopen 20.618s; (M)[AC], load1=4.38; `26-reopen-sublime.mkv`: absolute 6.164s, relative 8.839s, symlink 11.564s, hardlink 14.307s, unicode-1 17.199s, unicode-2 18.581s, prompt-reopen 21.203s; (M)[AC], load1=4.16; undo/redo still pairs per spelling in logs |

## 27. Syntax colour and editing

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| C and Rust initial text / colour | Both show syntax colour; measured onsets in the latency inventory. | Both initially show plain text, then syntax colours; measured separately in the latency inventory. | FIXED since drive 3 — edit-11v main text colouring. | `27-syntax-c-sublimite.mkv`: coloured 3.253s; (M)[AC], load1=3.22; `27-syntax-rs-sublimite.mkv`: coloured 2.674s; (M)[AC], load1=3.24; matched Sublime videos and logs/syntax-metrics.json |
| Token classes | Mariana token samples measured; LCD antialiasing affects dominant per-pixel colours. | C samples match the specified roles. Rust literal 1 at 96:11 is #c695c6 instead of number #f9ae58; assignment = at 15:32 is plain #d8dee9 instead of operator #f97b58. | NEW — edit-11v Rust colour-role gaps. | 27-tokens-* videos/stills; logs/token-specs.json and logs/token-rgb.json retain every line:column, rectangle and colour count. Token measurement table below. |
| Open comment / undo / wheel / isolated Z | Colour changes and undo are visible; Z onset measured. | Colour changes and undo are visible; no persistent colour spill beyond the first C closing comment delimiter. Z onset measured. | NEW — edit-11v; literal EOF lands on a blank line in sublimité, so added End/PageUp capture exposes code near EOF. | `27-syntax-c-sublimite.mkv`: comment 5.628s, undo 7.962s, end 10.330s, wheel 13.047s, Z 14.792s; (M)[AC], load1=3.22; `27-syntax-rs-sublimite.mkv`: comment 5.049s, undo 7.443s; (M)[AC], load1=3.24; 27-end-page-*; logs/syntax-metrics.json |
| Minimap and light theme | Coloured minimap. | Minimap remains monochrome as scoped. Isolated documented theme="light" config renders light theme. | STILL PRESENT — edit-1nm minimap; NEW light coverage edit-11v. | `27-light-sublimite.mkv`: light 4.257s; (M)[AC], load1=3.21; token viewport minimaps; config location/key from docs/decisions/edit-v0o.7.md and src/config/settings.c (read-only) |

The original Ctrl+End lands on a blank final line in the runtime C fixture: a first-colour onset for that blank view is **NOT OBSERVED**. The saved End/PageUp follow-up exposes coloured code, with its measured page response in the inventory. The retained onset metrics do not establish a separate duration for every plain-to-coloured row during the wheel burst; no zero-jank claim is made from the settled still.

## 28. Gutter selection

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Click / drag / Shift+click / click after scroll | Click copies whole line; drag readback stays that line; Shift+click copies the extended range. | Click, drag and Shift+click copy the intended whole-line ranges; post-scroll selection matches the clicked line. | FIXED since drive 3 — edit-dr3; edit-m15 stale-position regression probe. | `28-gutter-sublimite.mkv`: click 5.073s, drag 6.426s, shift 7.289s, after-scroll 12.580s; (M)[AC], load1=3.28; `28-gutter-sublime.mkv`: click 4.950s, drag 6.256s, shift 7.068s, after-scroll 12.173s; (M)[AC], load1=3.88; exact clipboard text and hashes in logs |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Gutter cell-centre repeat | Click: 65 bytes; drag: still 65 bytes; Shift+click: 390 bytes. | Click: 65 bytes; drag: 260 bytes; Shift+click: 390 bytes. Click/Shift hashes agree across editors; drag differs in the recorded result. | FIXED since drive 3 — edit-dr3 whole-line click and runtime range extension. NEW observed Sublime drag difference; cause not established. | `28-gutter-cells-sublimite.json`, targets/readbacks and matching .txt files; (M)[bat], load1=5.15; `28-gutter-cells-sublime.json`; (M)[bat], load1=7.04 |

Cell-centre targets are recorded explicitly in the logs, so this final result supersedes claims of identical drag behavior. Earlier Sublime r2/ready readbacks are empty and NOT OBSERVED. The original post-scroll readbacks match the whole clicked line in both editors; no stale-position failure is visible in that sampled probe (edit-m15).

## 29. Stalled X server

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Specified stopped-X sequence | Window survives the single stall and typing/save works. | Survives initial stall plus the three C/D/E repeats in the same process; ABstall fixture is saved after the first. Later saves retain added probes; close exits zero. | FIXED since drive 3 — edit-cee raster path. No GL-path claim (edit-848). | `29-stall-sublimite.mkv`: launch 1.888s, final 32.872s; (M)[AC], load1=3.52; `29-stall-sublime.mkv`: launch 1.977s, final 10.598s; (M)[AC], load1=2.57; X_STOP/X_CONT, verified :98 PID/argv, alive_after_stall and disk events; stderr has no error -11. Recording also pauses during each server stop. |

## 30. Controlled external changes and dirty branch

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Controlled sequence with post-sequence dwell | Matched run returns expected reload/keep bytes. | All repeated runs remain alive after the prescribed dwell and close zero, with exact expected hashes. | FIXED since drive 3 — edit-cee ending; STILL PRESENT passing reload/keep; edit-1um bottom prompt. | `30-external-x3-1-sublimite.mkv`: prompt-R 7.948s, prompt-K 13.192s, kept 15.320s; (M)[AC], load1=2.52; `30-external-x3-2-sublimite.mkv`: prompt-R 7.924s, prompt-K 13.264s, kept 15.499s; (M)[AC], load1=2.01; `30-external-x3-3-sublimite.mkv`: prompt-R 8.127s, prompt-K 13.332s, kept 15.369s; (M)[AC], load1=2.69; 30-external-x3-1-sublime.mkv; readback hashes match task 19 |
| Save x, undo, type y, external replacement | Dirty conflict is retained and cancelled/kept. | Dirty R/K prompt appears; K preserves y and does not silently replace the buffer. | NEW — edit-37a passing. | `30-dirty-branch-sublimite.mkv`: prompt 6.394s, kept 8.262s; (M)[AC], load1=3.04; `30-dirty-branch-sublime.mkv`: prompt 6.472s, kept 8.318s; (M)[AC], load1=2.23 |

## 31. Paging during index construction

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Immediate Ctrl+End and Prior sequence | Standard-session input is not a valid ready-view comparison; loaded capture ends blank. NOT OBSERVED for per-key paging. | First visible near-end view already includes queued Prior inputs; all requested PageUps yield net top 8,947,542 before the Next burst. | NEW — edit-91c / edit-pd6 / edit-jf9. No isolated EOF frame is claimed before the queued PageUps land. | 31-large-index-sublimite.mkv; logs/index-metrics.json; command envelopes and per-step raw gutter samples in appendix. Separate 31-early-end-only-sublimite.mkv isolates Ctrl+End. (M)[AC], load1=2.12 |
| Next burst and label watch | NOT OBSERVED for loaded-run final labels: final frame is blank. | Burst restores raw EOF label 8,947,842, retained through the full watch. No estimate→exact switch is distinguishable in the captured raw labels. | NOT OBSERVED — estimate-to-exact transition; edit-xkd; no stale incorrect label observed for edit-xzw. No internal estimate-flag claim. | `31-large-index-sublimite.mkv`: labels-final 123.039s; (M)[AC], load1=2.12; logs/index-watch-samples.json / index-watch-contact.png |
| Trace event sample count | No editor trace available. | G1 prints n=21, G3/T6 n=20 versus the requested key count 41. Net navigation is correct; coalescing is consistent with the lower count, but the count alone cannot identify which inputs shared a submission. | NEW — edit-pd6 / edit-jf9; printed counts are retained, not rewritten to the requested count. | traces/31-large-index-sublimite.txt; (M)[AC], load1=2.12 |
| Immediate wheel + Ctrl+Up after End | Not requested as separate Sublime case. | Editor stays alive and applies movement to the landed end view; closes normally. | NEW — edit-pd6 passing. | `31-large-queued-sublimite.mkv`: landed 3.344s; (M)[AC], load1=1.90; process_end=0 |

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| Isolated early Ctrl+End | No equivalent isolated early settled sample. | EOF first matches at 1.900000s after command 1.069387–1.125613s: 49.84 intervals / 830.6ms. | NEW — slow early-view sample under battery/load; edit-91c/jf9. No controlled regression inference. | `31-early-end-only-sublimite.mkv`; logs/extra-metrics.json; (M)[bat], load1=7.32 |
| Estimate-to-exact switch | NOT OBSERVED. | NOT OBSERVED: raw final label already agrees with EOF, and the UI samples do not identify an estimate flag or index-complete event. | NOT OBSERVED — edit-xzw transition coverage; correct final number does not prove the transition. | `31-large-index-sublimite.mkv`, index-watch-samples.json; (M)[AC], load1=2.12 |

## 32. Crash atomicity and orphan-session recovery

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| File undo, delay 0 intervals (input) | Not applicable: sublimité journal atomic-group probe. | Whole fixture restored: 262144 bytes, hash a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; measured kill offset 1.112 ms after Ctrl+Z command completion. | NEW — edit-pgr passing outcome. | `32-crash-undo-0-sublimite.mkv`: recovered 30.048s; (M)[AC], load1=2.54; disk hash remains fixture hash; binary readback retained |
| File undo, delay 1 intervals (input) | Not applicable: sublimité journal atomic-group probe. | Whole fixture restored: 262144 bytes, hash a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; measured kill offset 17.745 ms after Ctrl+Z command completion. | NEW — edit-pgr passing outcome. | `32-crash-undo-1-sublimite.mkv`: recovered 30.081s; (M)[AC], load1=2.00; disk hash remains fixture hash; binary readback retained |
| File undo, delay 2 intervals (input) | Not applicable: sublimité journal atomic-group probe. | Whole fixture restored: 262144 bytes, hash a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; measured kill offset 34.338 ms after Ctrl+Z command completion. | NEW — edit-pgr passing outcome. | `32-crash-undo-2-sublimite.mkv`: recovered 30.414s; (M)[AC], load1=2.06; disk hash remains fixture hash; binary readback retained |
| File undo, delay 4 intervals (input) | Not applicable: sublimité journal atomic-group probe. | Whole fixture restored: 262144 bytes, hash a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; measured kill offset 67.882 ms after Ctrl+Z command completion. | NEW — edit-pgr passing outcome. | `32-crash-undo-4-sublimite.mkv`: recovered 30.615s; (M)[AC], load1=3.44; disk hash remains fixture hash; binary readback retained |
| File undo, delay 8 intervals (input) | Not applicable: sublimité journal atomic-group probe. | Whole fixture restored: 262144 bytes, hash a4492ab68cbde7d9e61d483104fca3a2b954416df2a09eefb9b2a7ed1d1e0c9e; measured kill offset 136.762 ms after Ctrl+Z command completion. | NEW — edit-pgr passing outcome. | `32-crash-undo-8-sublimite.mkv`: recovered 30.797s; (M)[AC], load1=4.70; disk hash remains fixture hash; binary readback retained |
| Untitled undo variant | Not applicable: app-specific journal recovery. | Whole fixture restored; same full-fixture hash as file cases. | NEW — edit-pgr passing outcome. | `32-crash-untitled-sublimite.mkv`: recovered 30.789s; (M)[AC], load1=5.27 |
| Two instances, crashed dirty + clean close of other dirty instance | Not applicable: shared sublimité profile/session format. | Both ORPHAN_ONE_ and CLEAN_TWO_ buffers restore on first launch and persist through the two additional relaunches; disk baselines unchanged. | NEW — edit-rzj observed present. | `32-crash-two-sublimite.mkv`: restored-0 18.647s, restored-1 23.476s, restored-2 28.316s; (M)[AC], load1=5.99; per-launch binary clipboard files and directory listings. |

The kill sweep cannot observe the journal drain itself. All observed recoveries returned the entire fixture, so no partial-replay failure was reproduced; these outcomes do not prove that any kill landed mid-drain. Delay intervals are prescribed inputs (P: 1/60 s each), not latency budgets.

Independent finalisation check of the saved binary readbacks confirms the full-fixture SHA256 and byte count in every file-undo and untitled recovery result. The saved two-instance tab readbacks contain both marker prefixes on every logged relaunch. These are finite successful outcomes; no claim that SIGKILL actually intersected a multi-record journal drain.

## 33. Small landing probes

| Action | Sublime did | sublimité did | Divergence versus drive 3 / bead | Evidence |
|---|---|---|---|---|
| LF clipboard into CRLF buffer | All saved line endings are CRLF. | All saved line endings are CRLF. | NEW — edit-bml passing. | `33-crlf-sublimite.mkv`: saved 5.128s; (M)[AC], load1=3.69; `33-crlf-sublime.mkv`: saved 6.484s; (M)[AC], load1=4.95; shared saved hex 6f6e650d0a74776f0d0a74687265650d0a666f75720d0a and od output |
| Directory mode 555 save / restore permissions / save | First save succeeds despite the unwritable directory because the existing file is writable; this does not exercise a Sublime save failure. Later AB save succeeds. | First save fails and disk stays baseline; after mode 755, AB saves successfully and title clears dirty. | NEW — edit-1vo passing recovery; save strategies differ. | `33-save-fail-sublimite.mkv`: failed 3.410s, recovered 4.572s; (M)[AC], load1=3.47; `33-save-fail-sublime.mkv`: failed 4.339s, recovered 6.388s; (M)[AC], load1=5.04; disk/sha records, final hash 9993067d8b582142975f0e4b16e46b37f5753512233ae647631fa037f9e09738 |
| CJK cache pressure and second pass | Page captures retained for comparison. | Distinct CJK glyphs remain visible beyond the old capacity and on return; no # substitution observed in inspected page captures. | NEW — edit-r1l passing visible probe. | `33-glyphs-sublimite.mkv`: cycle0-page4 6.090s, cycle0-page5 7.184s, cycle1-page0 15.068s, cycle1-page4 22.386s, cycle1-page5 24.211s; (M)[AC], load1=3.36; 33-glyphs-sublime.mkv; requested range arithmetic 0x6200−0x4e00=5120 code points, 32 per line=160 lines |
| Prompt placement | Native modal feedback in matched case. | Bottom status row, first text line unobscured. | FIXED since drive 3 — edit-1um. | `30-external-x3-1-sublimite.mkv`: prompt-R 7.948s, prompt-K 13.192s; (M)[AC], load1=2.52 |

## Measurement inventory

All intervals are resampled video intervals at 60 Hz (P: milliseconds = intervals × 1000/60). Each row gives the input envelope and first matching frame; these are single observations, not physical key-to-photon percentiles. ROI and threshold details remain in the named JSON and the existing extraction scripts. Colour-change detectors measure their sampled region, not completion of every viewport token. No cross-power or cross-load speed ranking is justified.

`logs/startup-metrics.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `01-fresh-sublimite` / startup | 0.945824–1.022425 → 1.083333 | 8.25 / 137.5 | (M)[AC], load1=0.39 |
| `01-fresh-sublime` / startup | 0.763875–0.841061 → 1.016667 | 15.17 / 252.8 | (M)[AC], load1=0.48 |

`logs/typing-metrics.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `02-typing-r2-sublimite` / glyph | 4.866161–4.872511 → 4.916667 | 3.03 / 50.5 | (M)[AC], load1=0.52 |
| `02-typing-r2-sublime` / glyph | 4.733695–4.739630 → 4.766667 | 1.98 / 33.0 | (M)[AC], load1=0.77 |

`logs/large-metrics-sublimite.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `10-large-sublimite` / Y | 57.595006–57.599718 → 57.650000 | 3.30 / 55.0 | (M)[AC], load1=3.29 |
| `10-large-sublimite` / Z | 61.907312–61.914434 → 61.950000 | 2.56 / 42.7 | (M)[AC], load1=3.29 |
| `10-large-sublimite` / end-navigation | 47.261604–47.290633 → 47.316667 | 3.30 / 55.1 | (M)[AC], load1=3.29 |
| `10-large-sublimite` / startup | 0.761865–0.880591 → 0.916667 | 9.29 / 154.8 | (M)[AC], load1=3.29 |

`logs/file-command-metrics.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `24-files-sublimite` / new | 2.142713–2.173922 → 2.216667 | 4.44 / 74.0 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / open-b-prompt | 3.638914–3.671219 → 3.700000 | 3.67 / 61.1 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / open-missing-prompt | 6.011673–6.043784 → 6.066667 | 3.30 / 55.0 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / cancel-prompt | 9.097631–9.139200 → 9.150000 | 3.14 / 52.4 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / save-untitled-prompt | 12.367661–12.417372 → 12.433333 | 3.94 / 65.7 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / save-copy-prompt | 16.183167–16.237616 → 16.250000 | 4.01 / 66.8 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / save-missing-prompt | 19.952520–20.000932 → 20.016667 | 3.85 / 64.1 | (M)[AC], load1=4.65 |
| `24-files-sublimite` / open-over-find | 25.878360–25.910773 → 25.933333 | 3.30 / 55.0 | (M)[AC], load1=4.65 |
| `24-files-sublime` / new | 2.338480–2.370367 → 2.700000 | 21.69 / 361.5 | (M)[AC], load1=3.06 |
| `24-files-sublime` / open-b-prompt | 3.813031–3.846463 → 4.100000 | 17.22 / 287.0 | (M)[AC], load1=3.06 |
| `24-files-sublime` / open-missing-prompt | 6.674162–6.707129 → 6.866667 | 11.55 / 192.5 | (M)[AC], load1=3.06 |
| `24-files-sublime` / cancel-prompt | 10.328556–10.360954 → 10.516667 | 11.29 / 188.1 | (M)[AC], load1=3.06 |
| `24-files-sublime` / save-untitled-prompt | 13.637756–13.683806 → 13.833333 | 11.73 / 195.6 | (M)[AC], load1=3.06 |
| `24-files-sublime` / save-copy-prompt | 17.628090–17.677624 → 17.866667 | 14.31 / 238.6 | (M)[AC], load1=3.06 |
| `24-files-sublime` / save-missing-prompt | 21.408074–21.461088 → 21.616667 | 12.52 / 208.6 | (M)[AC], load1=3.06 |
| `24-files-sublime` / open-over-find | 27.404024–27.437984 → 27.583333 | 10.76 / 179.3 | (M)[AC], load1=3.06 |

`logs/syntax-metrics.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `27-syntax-c-sublimite` / first-text | 1.395091–1.494475 → 1.500000 | 6.29 / 104.9 | (M)[AC], load1=3.22 |
| `27-syntax-c-sublimite` / first-colour | 1.395091 → 2.466667 | 64.29 / 1071.6 | (M)[AC], load1=3.22 |
| `27-syntax-c-sublimite` / comment-colour | 3.619571–3.625954 → 3.683333 | 3.83 / 63.8 | (M)[AC], load1=3.22 |
| `27-syntax-c-sublimite` / undo-colour | 5.923246–5.960403 → 6.000000 | 4.61 / 76.8 | (M)[AC], load1=3.22 |
| `27-syntax-c-sublimite` / Z | 13.779435–13.789140 → 13.833333 | 3.23 / 53.9 | (M)[AC], load1=3.22 |
| `27-syntax-rs-sublimite` / first-text | 0.791173–0.895271 → 0.900000 | 6.53 / 108.8 | (M)[AC], load1=3.24 |
| `27-syntax-rs-sublimite` / first-colour | 0.791173 → 1.900000 | 66.53 / 1108.8 | (M)[AC], load1=3.24 |
| `27-syntax-rs-sublimite` / comment-colour | 3.041417–3.047315 → 3.100000 | 3.51 / 58.6 | (M)[AC], load1=3.24 |
| `27-syntax-rs-sublimite` / undo-colour | 5.405088–5.441981 → 5.483333 | 4.69 / 78.2 | (M)[AC], load1=3.24 |
| `27-syntax-rs-sublimite` / Z | 13.248761–13.256500 → 13.283333 | 2.07 / 34.6 | (M)[AC], load1=3.24 |
| `27-syntax-c-sublime` / first-text | 0.791188–0.971043 → 0.916667 | 7.53 / 125.5 | (M)[AC], load1=2.66 |
| `27-syntax-c-sublime` / first-colour | 0.791188 → 0.916667 | 7.53 / 125.5 | (M)[AC], load1=2.66 |
| `27-syntax-c-sublime` / comment-colour | 2.890493–2.894768 → 2.933333 | 2.57 / 42.8 | (M)[AC], load1=2.66 |
| `27-syntax-c-sublime` / undo-colour | 5.237954–5.271872 → 5.283333 | 2.72 / 45.4 | (M)[AC], load1=2.66 |
| `27-syntax-c-sublime` / Z | 12.942747–12.947215 → 12.983333 | 2.44 / 40.6 | (M)[AC], load1=2.66 |
| `27-syntax-rs-sublime` / first-text | 0.797557–0.975812 → 0.950000 | 9.15 / 152.4 | (M)[AC], load1=2.40 |
| `27-syntax-rs-sublime` / first-colour | 0.797557 → 0.950000 | 9.15 / 152.4 | (M)[AC], load1=2.40 |
| `27-syntax-rs-sublime` / comment-colour | 2.915465–2.922512 → 2.950000 | 2.07 / 34.5 | (M)[AC], load1=2.40 |
| `27-syntax-rs-sublime` / undo-colour | 5.160995–5.190586 → 5.200000 | 2.34 / 39.0 | (M)[AC], load1=2.40 |
| `27-syntax-rs-sublime` / Z | 12.998952–13.005763 → 13.033333 | 2.06 / 34.4 | (M)[AC], load1=2.40 |

`logs/extra-metrics.json`:

| Video stem / action | Command seconds → visible seconds | Intervals / ms | Stamp |
|---|---|---|---|
| `31-early-end-only-sublimite` / end | 1.069387–1.125613 → 1.900000 | 49.84 / 830.6 | (M)[bat], load1=7.32 |
| `27-end-page-sublimite` / page | 4.281729–4.317450 → 4.333333 | 3.10 / 51.6 | (M)[bat], load1=7.94 |
| `27-end-page-sublime` / page | 19.297437–19.344538 → 20.283333 | 59.15 / 985.9 | (M)[bat], load1=8.03 |

Late large-file samples (`logs/late-metrics.json`):

| Video / action | Command seconds → matching frame | Intervals / ms | Stamp |
|---|---|---|---|
| `10-large-loaded-sublime.mkv` / Y | 524.912669–524.928096 → 524.966667 | 3.24 / 54.0 | (M)[bat], load1=1.51 |
| `10-large-loaded-sublime.mkv` / Z | 530.334138–530.348095 → 530.383333 | 2.95 / 49.2 | (M)[bat], load1=1.51 |
| `10-large-loaded-sublime.mkv` / end-navigation | 512.293696–512.348269 → 512.483333 | 11.38 / 189.6 | (M)[bat], load1=1.51 |
| `16-find-large-wait-sublimite.mkv` / rare-match | 9.269184–9.298262 → 20.200000 | 655.85 / 10930.8 | (M)[bat], load1=6.87 |
| `16-find-large-stable-sublime.mkv` / rare-match | 388.013981–388.074730 → 419.016667 | 1860.16 / 31002.7 | (M)[bat], load1=10.28 |

Ready F3 and query-character onsets (ranges summarize all successful samples in the specified JSON; individual envelopes/ROIs remain there):

| Video / operation | Measured interval range / ms range | Evidence and stamp |
|---|---|---|
| `16-find-large-wait-sublimite.mkv` / F3 | 1.48–4.11 / 24.6–68.6 | logs/late-metrics.json; (M)[bat], load1=6.87 |
| `16-find-large-stable-sublime.mkv` / F3 | 2.03–10.34 / 33.8–172.3 | logs/late-metrics.json; (M)[bat], load1=10.28 |
| `16-find-large-sublimite.mkv` / query | 1.69–4.99 / 28.2–83.2 | logs/query-metrics.json; (M)[bat], load1=6.94 |
| `10-large-loaded-sublime.mkv` / query | 0.96–3.50 / 16.0–58.3 | logs/query-metrics.json; (M)[bat], load1=1.51 |

Wrapped first burst (`logs/wrap-onsets.json`; settled means matching the saved burst result, so includes the prescribed spacing between notches):

| Video | Command envelope; first moved; settled seconds | Moved / settled intervals | Stamp |
|---|---|---|---|
| `23-wrapped-reverse-sublimite.mkv` | 2.748855–3.056415; 2.800000; 3.000000 | 3.07 / 15.07 | (M)[AC], load1=1.87 |
| `23-wrapped-reverse-sublime.mkv` | 2.889656–3.195725; 2.950000; 3.200000 | 3.62 / 18.62 | (M)[AC], load1=3.17 |

Early indexing raw gutter samples (`logs/index-metrics.json`), all (M)[AC], load1=2.12. OCR occasionally drops or substitutes digits; malformed strings are retained as uncertain transcription, not called wrong UI line labels. Source cropped PNG per sample is named in the JSON.

| Prior input ordinal (prescribed) | Sample seconds | Raw OCR label |
|---|---|---|
| 1 | 1.083333 | `1` |
| 2 | 1.183333 | `1` |
| 3 | 1.283333 | `8947797` |
| 4 | 1.383333 | `8947782` |
| 5 | 1.483333 | `8947767` |
| 6 | 1.583333 | `8947752` |
| 7 | 1.683333 | `8947737` |
| 8 | 1.783333 | `8947272` |
| 9 | 1.883333 | `8947707` |
| 10 | 1.983333 | `8947692` |
| 11 | 2.083333 | `89467` |
| 12 | 2.183333 | `8947662` |
| 13 | 2.283333 | `894647` |
| 14 | 2.383333 | `894632` |
| 15 | 2.483333 | `894617` |
| 16 | 2.583333 | `8947602` |
| 17 | 2.683333 | `8947587` |
| 18 | 2.783333 | `894752` |
| 19 | 2.883333 | `8947557` |
| 20 | 2.983333 | `8947542` |

First near-end paint is at 1.283333s, 20.20 intervals after Ctrl+End and 18.18 after the first Prior; the first sampled near-end label includes already-queued paging. Next-burst command 3.017292–3.038377s reaches its final matching label at 3.066667s, 2.96 intervals / 49.4ms; (M)[AC], load1=2.12. The initial Prior samples still show the beginning; individual paint acknowledgement for each queued key is NOT OBSERVED. The final logical line agrees with the fixture EOF.

## Syntax token measurements

`logs/token-rgb.json` contains the exact rectangles, background and colour histograms; `token-specs.json` gives source positions. RGB is encoded here as hexadecimal. Dominant non-background pixels can be antialiased almost-background pixels, especially in Sublime; the supplementary strong-ink value is not silently substituted for that requested measurement. Theme targets are (G) constants from the brief. No explicit target is specified there for attribute, lifetime or preprocessor as a separate class.

| File / class / line:column / token | Sublime dominant (strong ink) | sublimité dominant | Theme target / result | Rectangles: Sublime; sublimité; stamps |
|---|---|---|---|---|
| editor.c / preprocessor / 1:1 / `#include` | #323841 (#f9ae58) | #c695c6 | unspecified class | [70, 60, 72, 18]; [80, 40, 160, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / string / 1:10 / `"editor/private.h"` | #99c794 (#99c794) | #99c794 | #99c794 — match | [151, 60, 162, 18]; [260, 40, 360, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / keyword / 13:1 / `static` | #30384a (#ec5f66) | #c695c6 | #c695c6 — match | [70, 276, 54, 18]; [80, 520, 120, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / type / 13:8 / `bool` | #303842 (#a16051) | #c695c6 | #c695c6 — match | [133, 276, 36, 18]; [220, 520, 80, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / function declaration / 13:13 / `runnable` | #3d83aa (#3d83aa) | #5fb4b4 | #5fb4b4 — match | [178, 276, 72, 18]; [320, 520, 160, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / function definition / 16:17 / `min64` | #323841 (#5d8e6b) | #5fb4b4 | #5fb4b4 — match | [214, 330, 45, 18]; [400, 640, 100, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / function call / 20:24 / `trace_now_ns` | #3f72c0 (#3f72c0) | #5fb4b4 | #5fb4b4 — match | [277, 402, 108, 18]; [540, 800, 240, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / operator / 20:22 / `=` | #f97b58 (#f97b58) | #f97b58 | #f97b58 — match | [259, 402, 9, 18]; [500, 800, 20, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / punctuation / 13:38 / `;` | #303842 (#4571a2) | #a6acb9 | #a6acb9 — match | [403, 276, 9, 18]; [820, 520, 20, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / comment / 25:8 / `Certify` | #a6acb9 (#a6acb9) | #a6acb9 | #a6acb9 — match | [133, 492, 63, 18]; [220, 1000, 140, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / number / 26:19 / `4096` | #303842 (#f28a49) | #f9ae58 | #f9ae58 — match | [232, 510, 36, 18]; [440, 1040, 80, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| editor.c / macro / 26:53 / `LINEIDX_CHUNK` | #d8dee9 (#d8dee9) | #c695c6 | #c695c6 — match | [538, 510, 117, 18]; [1120, 1040, 260, 40]; (M)[AC], load1=2.23; (M)[AC], load1=2.43 |
| config.rs / comment / 1:5 / `Native` | #a6acb9 (#a6acb9) | #a6acb9 | #a6acb9 — match | [106, 60, 54, 18]; [160, 40, 120, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / keyword / 6:1 / `use` | #c695c6 (#c695c6) | #c695c6 | #c695c6 — match | [70, 150, 27, 18]; [80, 240, 60, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / type / 7:17 / `Path` | #303843 (#c695c6) | #c695c6 | #c695c6 — match | [214, 168, 36, 18]; [400, 280, 80, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / string / 16:5 / `"font"` | #4d8e8d (#4d8e8d) | #99c794 | #99c794 — match | [106, 330, 54, 18]; [160, 640, 120, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / constant / 15:11 / `CONFIG_KEYS` | #f9ae58 (#f9ae58) | #c695c6 | #c695c6 — match | [160, 312, 99, 18]; [280, 600, 220, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / operator / 15:32 / `=` | #f97b58 (#f97b58) | #d8dee9 | #f97b58 — differs | [349, 312, 9, 18]; [700, 600, 20, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / punctuation / 6:13 / `;` | #303842 (#4571a2) | #a6acb9 | #a6acb9 — match | [178, 150, 9, 18]; [320, 240, 20, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / attribute / 34:3 / `derive` | #6699cc (#6699cc) | #c695c6 | unspecified class | [88, 60, 54, 18]; [120, 40, 120, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / function definition / 56:8 / `fmt` | #303842 (#5fb4b4) | #5fb4b4 | #5fb4b4 — match | [133, 456, 27, 18]; [220, 920, 60, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / lifetime / 56:42 / `'_` | #334055 (#d25d5a) | #595e67 | unspecified class | [439, 456, 18, 18]; [900, 920, 40, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / function call / 59:35 / `write_str` | #6699cc (#6699cc) | #5fb4b4 | #5fb4b4 — match | [376, 510, 81, 18]; [760, 1040, 180, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |
| config.rs / number / 96:11 / `1` | #e87f47 (#e87f47) | #c695c6 | #f9ae58 — differs | [160, 168, 9, 18]; [280, 280, 20, 40]; (M)[AC], load1=2.37; (M)[AC], load1=2.39 |

## Flash audit

Fixed region (1568,40,160,1220), colour/tolerance pairs #bf853d/40, #ff8800/60, #6b5030/24; detector minimum 200 pixels and appear/vanish within 30 resampled frames (analysis parameters). Counts below come from each literal `logs/<stem>.flash-<colour>.txt`, not still-image inference. The fixed strip covers the default window; resized windows require the separate moving-strip scans.

| sublimité video stem | bf853d / ff8800 / 6b5030 events | Stamp |
|---|---|---|
| `01-fresh-sublimite` | 0 / 0 / 0 | (M)[AC], load1=0.39 |
| `02-paced-sublimite` | 0 / 0 / 0 | (M)[AC], load1=6.94 |
| `02-paced-verified-sublimite` | 0 / 0 / 0 | (M)[AC], load1=8.48 |
| `02-typing-r2-sublimite` | 0 / 0 / 0 | (M)[AC], load1=0.52 |
| `03-nav-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.18 |
| `03-selection-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.72 |
| `04-mouse-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.02 |
| `05-edit-r2-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.03 |
| `05-edit-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.31 |
| `06-alternate-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.78 |
| `06-clipboard-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.91 |
| `06-cross-fullpath-r2-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.70 |
| `06-cross-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.59 |
| `07-find-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.17 |
| `08-save-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.79 |
| `09-tabs-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.80 |
| `10-large-early-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.06 |
| `10-large-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.29 |
| `11-resize-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.70 |
| `12-hotexit-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.16 |
| `12-recovery-conflict-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.48 |
| `13-minimap-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.25 |
| `14-idle-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.76 |
| `15-autoscroll-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.03 |
| `15-mouse-calibrated-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.88 |
| `15-mouse-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.57 |
| `15-mouse-wrapped-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.17 |
| `16-find-large-sublimite` | 0 / 0 / 0 | (M)[bat], load1=6.94 |
| `16-find-large-wait-sublimite` | 0 / 0 / 0 | (M)[bat], load1=6.87 |
| `16-find-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.23 |
| `16-original-sequence-sublimite` | 0 / 0 / 0 | (M)[AC], load1=0.81 |
| `17-profile-audit-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.97 |
| `17-profile-continuation-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.71 |
| `17-recovery-a-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.36 |
| `17-recovery-b-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.41 |
| `17-recovery-c-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.65 |
| `17-recovery-d-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.79 |
| `17-recovery-e-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.05 |
| `18-tabtitle-second-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.13 |
| `18-tabtitle-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.39 |
| `19-external-controlled-sublimite` | 0 / 0 / 0 | (M)[AC], load1=0.50 |
| `19-external-repro-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.64 |
| `19-external-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.61 |
| `20-emoji-carets-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.43 |
| `20-emoji-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.52 |
| `21-font-file-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.17 |
| `21-font-iosevka-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.46 |
| `21-font-liberation-mono-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.42 |
| `21-font-missing-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.27 |
| `21-font-proportional-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.47 |
| `22-common-commands-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.32 |
| `23-wrapped-keys-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.05 |
| `23-wrapped-reverse-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.87 |
| `24-files-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.65 |
| `24-scroll-caret-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.02 |
| `25-findkeys-continuous-sublimite` | 0 / 0 / 0 | (M)[bat], load1=4.97 |
| `25-findkeys-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.79 |
| `26-reopen-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.38 |
| `27-end-page-sublimite` | 0 / 0 / 0 | (M)[bat], load1=7.94 |
| `27-light-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.21 |
| `27-syntax-c-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.22 |
| `27-syntax-rs-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.24 |
| `27-tokens-c-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.43 |
| `27-tokens-rs-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.39 |
| `28-gutter-cells-sublimite` | 0 / 0 / 0 | (M)[bat], load1=5.15 |
| `28-gutter-r2-sublimite` | 0 / 0 / 0 | (M)[bat], load1=6.25 |
| `28-gutter-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.28 |
| `29-stall-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.52 |
| `30-dirty-branch-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.04 |
| `30-external-x3-1-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.52 |
| `30-external-x3-2-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.01 |
| `30-external-x3-3-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.69 |
| `31-early-end-only-sublimite` | 0 / 0 / 0 | (M)[bat], load1=7.32 |
| `31-large-index-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.12 |
| `31-large-queued-sublimite` | 0 / 0 / 0 | (M)[AC], load1=1.90 |
| `32-crash-two-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.99 |
| `32-crash-undo-0-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.54 |
| `32-crash-undo-1-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.00 |
| `32-crash-undo-2-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.06 |
| `32-crash-undo-4-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.44 |
| `32-crash-undo-8-sublimite` | 0 / 0 / 0 | (M)[AC], load1=4.70 |
| `32-crash-untitled-sublimite` | 0 / 0 / 0 | (M)[AC], load1=5.27 |
| `33-crlf-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.69 |
| `33-glyphs-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.36 |
| `33-save-fail-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.47 |
| `dense64-sublimite` | 0 / 0 / 0 | (M)[AC], load1=3.42 |
| `font12-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.77 |
| `utf8-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.98 |
| `zoom-sublimite` | 0 / 0 / 0 | (M)[AC], load1=2.52 |

Each saved sublimité video is represented above. The tracked resize scan reports the single syntax-colour/crop-movement false positive already described in task 14; the find-resize tracked scans have no events (`logs/16-find-tracked.flash-*.txt`, (M)[AC], load1=3.23). The deliberate source-change prompt, save/path errors, recovery notices and unsupported find-in-files notice are action feedback outside the old minimap pulse. No unexplained orange minimap pulse is established. Zero detector counts do not certify every pixel or the untested GL path.

## Capture review ledger

This is the completed replacement for every draft capture-progress entry. Each `.mkv` has a same-stem command/power/readback `.json` and `.timeline.txt` in `logs/`, unless marked as a derived video. The task table gives the substantive result; exclusions here constrain that result and never count as a pass. Timestamps in task tables and the measurement inventory locate the decisive states.

| Existing video stem | Analysis / disposition | Capture stamp |
|---|---|---|
| `01-fresh-sublime` | STILL PRESENT — First text and presentation measured; C body colour present. | (M)[AC], load1=0.48 |
| `01-fresh-sublimite` | STILL PRESENT — First text and presentation measured; C body colour present. | (M)[AC], load1=0.39 |
| `02-paced-sublime` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=8.11 |
| `02-paced-sublimite` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=6.94 |
| `02-paced-verified-r2-sublime` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=6.98 |
| `02-paced-verified-sublime` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=8.00 |
| `02-paced-verified-sublimite` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=8.48 |
| `02-typing-r2-sublime` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=0.77 |
| `02-typing-r2-sublimite` | STILL PRESENT — Typing sequence visible; verified variants match the expected paragraph. | (M)[AC], load1=0.52 |
| `03-nav-sublime` | STILL PRESENT — Caret/selection respond; paging placement differs between editors. | (M)[AC], load1=1.13 |
| `03-nav-sublimite` | STILL PRESENT — Caret/selection respond; paging placement differs between editors. | (M)[AC], load1=1.18 |
| `03-selection-sublime` | STILL PRESENT — Caret/selection respond; paging placement differs between editors. | (M)[AC], load1=1.74 |
| `03-selection-sublimite` | STILL PRESENT — Caret/selection respond; paging placement differs between editors. | (M)[AC], load1=1.72 |
| `04-mouse-sublime` | STILL PRESENT — Mouse/wheel/minimap navigate; same pixel is not the same text target. | (M)[AC], load1=2.58 |
| `04-mouse-sublimite` | STILL PRESENT — Mouse/wheel/minimap navigate; same pixel is not the same text target. | (M)[AC], load1=2.02 |
| `05-edit-r2-sublime` | STILL PRESENT — Edit/undo/redo restore expected text. | (M)[AC], load1=3.26 |
| `05-edit-r2-sublimite` | STILL PRESENT — Edit/undo/redo restore expected text. | (M)[AC], load1=3.03 |
| `05-edit-sublime` | STILL PRESENT — Edit/undo/redo restore expected text. | (M)[AC], load1=2.44 |
| `05-edit-sublimite` | STILL PRESENT — Edit/undo/redo restore expected text. | (M)[AC], load1=2.31 |
| `06-alternate-sublime` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=2.74 |
| `06-alternate-sublimite` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=2.78 |
| `06-clipboard-sublime` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=2.92 |
| `06-clipboard-sublimite` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=2.91 |
| `06-cross-back-fullpath-r2-sublime` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=2.05 |
| `06-cross-back-sublime` | NOT OBSERVED — source identity not secure in this chain; use full-path round trip. | (M)[AC], load1=6.24 |
| `06-cross-fullpath-r2-sublimite` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=1.70 |
| `06-cross-source-attempt1-sublime` | NOT OBSERVED — launch-only attempt; no discovered new window or completed round trip. | (M)[AC], load1=2.87 |
| `06-cross-source-fullpath-r2-sublime` | STILL PRESENT — Clipboard operations and round trip preserved in command stdout. | (M)[AC], load1=1.27 |
| `06-cross-source-sublime` | NOT OBSERVED — basename-selected source may be a restored older window; full-path repeat supersedes it. | (M)[AC], load1=4.77 |
| `06-cross-sublimite` | STILL PRESENT — clipboard text recorded, but uncertain original source identity; full-path repeat supplies comparison. | (M)[AC], load1=5.59 |
| `07-find-sublime` | STILL PRESENT — Find/replace panels respond; single-match fixture cannot test first-Return advance. | (M)[AC], load1=2.83 |
| `07-find-sublimite` | STILL PRESENT — Find/replace panels respond; single-match fixture cannot test first-Return advance. | (M)[AC], load1=3.17 |
| `08-save-sublime` | STILL PRESENT — Save bytes and clean external append verified. | (M)[AC], load1=2.99 |
| `08-save-sublimite` | STILL PRESENT — Save bytes and clean external append verified. | (M)[AC], load1=2.79 |
| `09-tabs-sublime` | STILL PRESENT — CLI burst accepted; runtime retains one inotify descriptor. | (M)[AC], load1=3.94 |
| `09-tabs-sublimite` | STILL PRESENT — CLI burst accepted; runtime retains one inotify descriptor. | (M)[AC], load1=5.80 |
| `10-large-early-sublimite` | STILL PRESENT — Large-file navigation/edit sample; use only loaded frames for onsets. | (M)[AC], load1=5.06 |
| `10-large-loaded-sublime` | STILL PRESENT — Large-file navigation/edit sample; use only loaded frames for onsets. | (M)[bat], load1=1.51 |
| `10-large-sublime` | NOT OBSERVED for Y/Z and per-key paging in the early blank action window; later end frame loads. Use separately loaded comparison. | (M)[AC], load1=2.34 |
| `10-large-sublimite` | STILL PRESENT — Large-file navigation/edit sample; use only loaded frames for onsets. | (M)[AC], load1=3.29 |
| `11-resize-minimap-track` | NEW — derived moving-minimap strip, not an independent capture; scan results in flash audit. | Inherits (M)[AC], load1=2.70 |
| `11-resize-sublime` | STILL PRESENT — Settled resize frames show no persistent garbage. | (M)[AC], load1=3.40 |
| `11-resize-sublimite` | STILL PRESENT — Settled resize frames show no persistent garbage. | (M)[AC], load1=2.70 |
| `12-hotexit-sublime` | STILL PRESENT — Runtime unchanged-base dirty recovery works; replaced base is set aside. | (M)[AC], load1=3.12 |
| `12-hotexit-sublimite` | STILL PRESENT — Runtime unchanged-base dirty recovery works; replaced base is set aside. | (M)[AC], load1=3.16 |
| `12-recovery-conflict-sublimite` | STILL PRESENT — Runtime unchanged-base dirty recovery works; replaced base is set aside. | (M)[AC], load1=2.48 |
| `13-minimap-fullpath-r2-sublime` | STILL PRESENT — Runtime minimap remains monochrome; corrected Sublime view is coloured. | (M)[AC], load1=2.52 |
| `13-minimap-sublime` | NOT OBSERVED — blank/loading and stale basename match; full-path repeat supplies minimap comparison. | (M)[AC], load1=4.13 |
| `13-minimap-sublimite` | STILL PRESENT — Runtime minimap remains monochrome; corrected Sublime view is coloured. | (M)[AC], load1=3.25 |
| `14-idle-fullpath-r2-sublime` | STILL PRESENT — Focused idle detector inspected; quiet body after caret phase. | (M)[AC], load1=3.05 |
| `14-idle-sublime` | NOT OBSERVED — stale-window match; corrected idle log supersedes its detector result. | (M)[AC], load1=4.90 |
| `14-idle-sublimite` | STILL PRESENT — Focused idle detector inspected; quiet body after caret phase. | (M)[AC], load1=4.76 |
| `15-autoscroll-sublime` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=4.51 |
| `15-autoscroll-sublimite` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=4.03 |
| `15-mouse-calibrated-fullpath-r2-sublime` | NOT OBSERVED for intended calibrated ranges — wrong inherited grid; readiness-calibrated repeat supplies ranges. | (M)[AC], load1=3.02 |
| `15-mouse-calibrated-sublime` | NOT OBSERVED — stale/wrong-grid target; readiness-calibrated repeat supplies ranges. | (M)[AC], load1=2.86 |
| `15-mouse-calibrated-sublimite` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=2.88 |
| `15-mouse-fullpath-r2-sublime` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=2.80 |
| `15-mouse-sublime` | NOT OBSERVED — stale-window basename match; full-path repeat supersedes it. | (M)[AC], load1=4.05 |
| `15-mouse-sublimite` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=2.57 |
| `15-mouse-target-ready-r2-sublime` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[bat], load1=1.03 |
| `15-mouse-target-ready-sublime` | STILL PRESENT — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=0.60 |
| `15-mouse-wrapped-fullpath-r2-sublime` | FIXED since drive 3 — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=2.25 |
| `15-mouse-wrapped-sublime` | NOT OBSERVED — stale-window basename match; full-path repeat supersedes it. | (M)[AC], load1=3.84 |
| `15-mouse-wrapped-sublimite` | FIXED since drive 3 — Mouse selection and wheel probes reviewed; exact claims in task table. | (M)[AC], load1=2.17 |
| `16-find-fullpath-r2-sublime` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[AC], load1=2.46 |
| `16-find-large-stable-sublime` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[bat], load1=10.28 |
| `16-find-large-sublimite` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[bat], load1=6.94 |
| `16-find-large-wait-sublime` | NOT OBSERVED for late latency/F3 — backward realtime jump drops recording frames. Stable-clock repeat supplies rare-match onset. | (M)[bat], load1=3.33 |
| `16-find-large-wait-sublimite` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[bat], load1=6.87 |
| `16-find-minimap-track` | NEW — derived moving-minimap strip, not an independent capture; scan results in flash audit. | Inherits (M)[AC], load1=3.23 |
| `16-find-sublime` | NOT OBSERVED — stale dirty buffer already contains OMEGA before inputs; not query corruption. | (M)[AC], load1=3.89 |
| `16-find-sublimite` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[AC], load1=3.23 |
| `16-original-sequence-sublime` | NOT OBSERVED — empty full-buffer readback cannot certify original-sequence hash. | (M)[AC], load1=1.07 |
| `16-original-sequence-sublimite` | STILL PRESENT — Find/readback/search evidence reviewed; invalid attempts explicitly excluded. | (M)[AC], load1=0.81 |
| `17-profile-audit-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=5.97 |
| `17-profile-continuation-sublimite` | NEW — eventual restored window visible; process status unavailable after harness parent exited, not return code zero. | (M)[AC], load1=2.71 |
| `17-recovery-a-fullpath-r2-sublime` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=2.12 |
| `17-recovery-a-sublime` | NOT OBSERVED — stale-window match and relaunch discovery timeout; full-path repeat supersedes it. | (M)[AC], load1=6.39 |
| `17-recovery-a-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=5.36 |
| `17-recovery-b-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=2.41 |
| `17-recovery-c-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=2.65 |
| `17-recovery-d-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=3.79 |
| `17-recovery-e-sublimite` | STILL PRESENT — Recovery opens; changed-base edits set aside, normal relaunch retains dirty buffers. | (M)[AC], load1=4.05 |
| `18-tabtitle-second-sublime` | FIXED since drive 3 — Unicode title renders; runtime same-path open reuses existing tab. | (M)[AC], load1=3.28 |
| `18-tabtitle-second-sublimite` | FIXED since drive 3 — Unicode title renders; runtime same-path open reuses existing tab. | (M)[AC], load1=3.13 |
| `18-tabtitle-sublime` | FIXED since drive 3 — Unicode title renders; runtime same-path open reuses existing tab. | (M)[AC], load1=2.68 |
| `18-tabtitle-sublimite` | FIXED since drive 3 — Unicode title renders; runtime same-path open reuses existing tab. | (M)[AC], load1=2.39 |
| `19-external-controlled-sublime` | STILL PRESENT — Reload/keep reviewed against readbacks; controlled cases match. | (M)[AC], load1=1.54 |
| `19-external-controlled-sublimite` | STILL PRESENT — Reload/keep reviewed against readbacks; controlled cases match. | (M)[AC], load1=0.50 |
| `19-external-r2-sublime` | STILL PRESENT — Undo removes clean reload; keep has pre-append contents. Inactive-file step NOT OBSERVED: wrong visible tab/newline-only readback. | (M)[bat], load1=0.18 |
| `19-external-repro-sublimite` | STILL PRESENT — Reload/keep reviewed against readbacks; controlled cases match. | (M)[AC], load1=1.64 |
| `19-external-sublime` | NOT OBSERVED — incomplete/unresponsive action window with empty reads and xclip timeout. | (M)[AC], load1=4.97 |
| `19-external-sublimite` | STILL PRESENT — Reload/keep reviewed against readbacks; controlled cases match. | (M)[AC], load1=3.61 |
| `20-emoji-carets-sublime` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=3.49 |
| `20-emoji-carets-sublimite` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=2.43 |
| `20-emoji-sublime` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=2.26 |
| `20-emoji-sublimite` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=1.52 |
| `20-font-restore-sublime` | NEW — calibration/reset evidence only, not a new emoji pass. | (M)[AC], load1=3.67 |
| `21-font-file-sublimite` | STILL PRESENT — Requested mono face or embedded fallback appears; typed grid aligned. | (M)[AC], load1=3.17 |
| `21-font-iosevka-sublimite` | STILL PRESENT — Requested mono face or embedded fallback appears; typed grid aligned. | (M)[AC], load1=3.46 |
| `21-font-liberation-mono-sublimite` | STILL PRESENT — Requested mono face or embedded fallback appears; typed grid aligned. | (M)[AC], load1=3.42 |
| `21-font-missing-sublimite` | STILL PRESENT — Requested mono face or embedded fallback appears; typed grid aligned. | (M)[AC], load1=3.27 |
| `21-font-proportional-sublimite` | STILL PRESENT — Requested mono face or embedded fallback appears; typed grid aligned. | (M)[AC], load1=3.47 |
| `22-common-commands-fullpath-r2-sublime` | FIXED since drive 3 — Runtime New/Open/Save As/Quit present; full workflow in task 24. | (M)[AC], load1=0.80 |
| `22-common-commands-sublime` | NOT OBSERVED — discovery failure; full-path repeat and task 24 supply command evidence. | (M)[AC], load1=3.09 |
| `22-common-commands-sublimite` | FIXED since drive 3 — Runtime New/Open/Save As/Quit present; full workflow in task 24. | (M)[AC], load1=3.32 |
| `22-sublime-font-baseline-sublime` | NEW — font baseline calibration, not a file-command test. | (M)[AC], load1=0.77 |
| `22-sublime-font-check-sublime` | NEW — font check calibration, not a file-command test. | (M)[AC], load1=0.97 |
| `23-wrapped-keys-sublime` | FIXED since drive 3 — Runtime wrapped notches/reversal pass; settled Sublime reversal not established. | (M)[AC], load1=4.05 |
| `23-wrapped-keys-sublimite` | FIXED since drive 3 — Runtime wrapped notches/reversal pass; settled Sublime reversal not established. | (M)[AC], load1=4.05 |
| `23-wrapped-late-sublime` | NOT OBSERVED for reversal — late frame shows a mid-document view, not a verified return to base. | (M)[bat], load1=7.90 |
| `23-wrapped-reverse-r2-sublime` | STILL PRESENT for observed three-row upward steps; NOT OBSERVED for final settled reversal (last notch not painted by final still). | (M)[bat], load1=7.23 |
| `23-wrapped-reverse-sublime` | STILL PRESENT for initial movement; NOT OBSERVED for settled reversal because upward paints are delayed. | (M)[AC], load1=3.17 |
| `23-wrapped-reverse-sublimite` | FIXED since drive 3 — Runtime wrapped notches/reversal pass; settled Sublime reversal not established. | (M)[AC], load1=1.87 |
| `24-files-sublime` | NEW — File workflows, prompt cancellation, Save As rebinding and hot exit pass. | (M)[AC], load1=3.06 |
| `24-files-sublimite` | NEW — File workflows, prompt cancellation, Save As rebinding and hot exit pass. | (M)[AC], load1=4.65 |
| `24-scroll-caret-sublime` | NEW — File workflows, prompt cancellation, Save As rebinding and hot exit pass. | (M)[AC], load1=3.72 |
| `24-scroll-caret-sublimite` | NEW — File workflows, prompt cancellation, Save As rebinding and hot exit pass. | (M)[AC], load1=3.02 |
| `25-findkeys-continuous-sublime` | FIXED since drive 3 — First-Return advance and continuous replacement work; hashes agree. | (M)[bat], load1=6.59 |
| `25-findkeys-continuous-sublimite` | FIXED since drive 3 — First-Return advance and continuous replacement work; hashes agree. | (M)[bat], load1=4.97 |
| `25-findkeys-sublime` | FIXED since drive 3 comparison supports navigation; second-replacement continuity NOT OBSERVED because intervening readback changed selection; continuous repeat supersedes it. | (M)[AC], load1=5.50 |
| `25-findkeys-sublimite` | FIXED since drive 3 — First-Return advance and continuous replacement work; hashes agree. | (M)[AC], load1=4.79 |
| `26-reopen-r2-sublime` | NOT OBSERVED for complete alias sequence; r3 readiness repeat supplies clearer tab/view evidence. | (M)[bat], load1=4.60 |
| `26-reopen-r3-sublime` | NEW — absolute/relative preserve edited view; aliases create separate tabs; final Open-prompt result NOT OBSERVED at captured still. | (M)[bat], load1=0.40 |
| `26-reopen-sublime` | NOT OBSERVED for complete alias/edit/undo comparison in early delayed frames; r3 supplies partial ready-window evidence. | (M)[AC], load1=4.16 |
| `26-reopen-sublimite` | FIXED since drive 3 — Runtime aliases reuse identity with edits/view/undo; Sublime alias views differ. | (M)[AC], load1=4.38 |
| `27-end-page-sublime` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[bat], load1=8.03 |
| `27-end-page-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[bat], load1=7.94 |
| `27-light-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=3.21 |
| `27-syntax-c-sublime` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.66 |
| `27-syntax-c-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=3.22 |
| `27-syntax-rs-sublime` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.40 |
| `27-syntax-rs-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=3.24 |
| `27-tokens-c-sublime` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.23 |
| `27-tokens-c-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.43 |
| `27-tokens-rs-sublime` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.37 |
| `27-tokens-rs-sublimite` | NEW — C/Rust colour and edit transitions measured; Rust number/operator gaps. | (M)[AC], load1=2.39 |
| `28-gutter-cells-sublime` | FIXED since drive 3 — Whole-line click fixed; runtime drag extends; recorded Sublime drag stays one line. | (M)[bat], load1=7.04 |
| `28-gutter-cells-sublimite` | FIXED since drive 3 — Whole-line click fixed; runtime drag extends; recorded Sublime drag stays one line. | (M)[bat], load1=5.15 |
| `28-gutter-r2-sublime` | NOT OBSERVED — empty clipboard results do not establish target selections. | (M)[bat], load1=6.40 |
| `28-gutter-r2-sublimite` | FIXED since drive 3 — Whole-line click fixed; runtime drag extends; recorded Sublime drag stays one line. | (M)[bat], load1=6.25 |
| `28-gutter-ready-sublime` | NOT OBSERVED — empty clipboard results; cell-centre capture supersedes it. | (M)[bat], load1=6.85 |
| `28-gutter-sublime` | FIXED since drive 3 — Whole-line click fixed; runtime drag extends; recorded Sublime drag stays one line. | (M)[AC], load1=3.88 |
| `28-gutter-sublimite` | FIXED since drive 3 — Whole-line click fixed; runtime drag extends; recorded Sublime drag stays one line. | (M)[AC], load1=3.28 |
| `29-stall-sublime` | FIXED since drive 3 — Stall survival and subsequent save pass; raster only. | (M)[AC], load1=2.57 |
| `29-stall-sublimite` | FIXED since drive 3 — Stall survival and subsequent save pass; raster only. | (M)[AC], load1=3.52 |
| `30-dirty-branch-sublime` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=2.23 |
| `30-dirty-branch-sublimite` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=3.04 |
| `30-external-x3-1-sublime` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=2.89 |
| `30-external-x3-1-sublimite` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=2.52 |
| `30-external-x3-2-sublimite` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=2.01 |
| `30-external-x3-3-sublimite` | NEW — Repeated reload/keep and dirty branch preserve expected bytes; normal runtime exits. | (M)[AC], load1=2.69 |
| `31-early-end-only-sublimite` | NEW — Early runtime movement lands; exact-label transition not observable. | (M)[bat], load1=7.32 |
| `31-large-index-sublime` | NOT OBSERVED for ready per-key navigation — standard-session action window does not establish it. | (M)[AC], load1=1.83 |
| `31-large-index-sublimite` | NEW — Early runtime movement lands; exact-label transition not observable. | (M)[AC], load1=2.12 |
| `31-large-loaded-sublime` | NOT OBSERVED for completed paging/labels — final saved view is blank. | (M)[bat], load1=4.39 |
| `31-large-queued-sublimite` | NEW — Early runtime movement lands; exact-label transition not observable. | (M)[AC], load1=1.90 |
| `32-crash-two-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=5.99 |
| `32-crash-undo-0-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=2.54 |
| `32-crash-undo-1-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=2.00 |
| `32-crash-undo-2-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=2.06 |
| `32-crash-undo-4-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=3.44 |
| `32-crash-undo-8-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=4.70 |
| `32-crash-untitled-sublimite` | NEW — Saved crash readbacks are whole fixture or both expected session buffers. | (M)[AC], load1=5.27 |
| `33-crlf-sublime` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=4.95 |
| `33-crlf-sublimite` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=3.69 |
| `33-glyphs-sublime` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=5.28 |
| `33-glyphs-sublimite` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=3.36 |
| `33-save-fail-sublime` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=5.04 |
| `33-save-fail-sublimite` | NEW — CRLF save, runtime save-failure retry, sampled glyph-pressure pages pass. | (M)[AC], load1=3.47 |
| `dense64-sublime` | STILL PRESENT — Large-file navigation/edit sample; use only loaded frames for onsets. | (M)[AC], load1=3.58 |
| `dense64-sublimite` | STILL PRESENT — Large-file navigation/edit sample; use only loaded frames for onsets. | (M)[AC], load1=3.42 |
| `font12-sublimite` | STILL PRESENT — First text and presentation measured; C body colour present. | (M)[AC], load1=2.77 |
| `utf8-fullpath-r2-sublime` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=3.04 |
| `utf8-sublime` | NOT OBSERVED — stale basename window; full-path repeat supplies valid Unicode comparison. | (M)[AC], load1=2.76 |
| `utf8-sublimite` | STILL PRESENT — Runtime emoji artwork gap remains; undo/save preserve original bytes. | (M)[AC], load1=2.98 |
| `zoom-sublime` | STILL PRESENT — First text and presentation measured; C body colour present. | (M)[AC], load1=2.40 |
| `zoom-sublimite` | STILL PRESENT — First text and presentation measured; C body colour present. | (M)[AC], load1=2.52 |

## Drive-3 findings disposition

Each entry below corresponds to a drive-3 Missing features item, Edge cases item, or verdict blocker. Repeated entries are intentional so no previous blocker disappears through regrouping. Evidence references point to stamped task rows above.

| Drive-3 finding | Bead | Disposition | Evidence / reason |
|---|---|---|---|
| Missing features: New/Open/Save As/Quit | edit-o20 | FIXED since drive 3 | Tasks 22/24: prompts, disk hashes, rebound copy, process exit and dirty relaunch. |
| Missing features: predictable wrapped prose wheel | edit-zw9 | FIXED since drive 3 | Tasks 15/23: visual-row registration and identical runtime body after reversal. |
| Missing features: complete emoji artwork | edit-r8e | STILL PRESENT | Task 20: woman-technologist missing-glyph symbol; byte restoration passes. |
| Missing features: find in files and code colour | edit-xkd / edit-11v / edit-1nm | STILL PRESENT for find in files/minimap; FIXED since drive 3 for C/Rust body colour | Tasks 16/27: unsupported project search; coloured body, monochrome minimap. |
| Missing features: search muscle-memory parity | edit-g02 | FIXED since drive 3 | Tasks 16/25: first Return advances; Ctrl+Shift+H changes readback; continuous replace-all undo agrees. |
| Edge: wrapped-wheel distance/reversal | edit-zw9 | FIXED since drive 3 | Task 23 runtime registration; no failed final Sublime frame used as proof. |
| Edge: renderer error near normal ending | edit-cee | FIXED since drive 3 in raster probes | Tasks 19/29/30: stall survives, saves work, repeated normal endings; no error -11 in saved editor logs. GL NOT OBSERVED. |
| Edge: unsupported daily file commands | edit-o20 | FIXED since drive 3 | Tasks 22/24 complete file workflow. |
| Edge: first-Return difference | edit-g02 | FIXED since drive 3 | Task 25 position sequence. |
| Edge: Ctrl+Shift+H habit difference | edit-g02 | FIXED since drive 3 | Continuous task-25 readback hashes agree. |
| Edge: gutter selection difference | edit-dr3 | FIXED since drive 3 | Tasks 15/28: runtime whole-line click and newline copy; drag comparison is explicitly unequal. |
| Edge: duplicate path tabs | edit-97i | FIXED since drive 3 | Tasks 18/26: absolute, relative and aliases preserve existing runtime edited view/undo. |
| Edge: emoji font gap | edit-r8e | STILL PRESENT | Task 20 sampled artwork and font environment. |
| Edge: comparison limits | no bead | NOT OBSERVED where blank/stale; partially resolved by named repeats | Capture ledger: corrected full-path/ready windows support specific rows only; dense run now reaches end/Z. |
| Edge: estimate-to-exact label switch | edit-xzw / edit-xkd | NOT OBSERVED | Task 31: sampled raw label is correct, but no identifiable estimate-flag transition. |
| Verdict blocker: missing New/Open/Save As/Quit | edit-o20 | FIXED since drive 3 | Tasks 22/24. |
| Verdict blocker: incorrect wrapped-wheel scrolling | edit-zw9 | FIXED since drive 3 | Tasks 15/23. |
| Verdict blocker: missing emoji artwork | edit-r8e | STILL PRESENT | Task 20. |
| Verdict blocker: unexplained renderer exit near close | edit-cee | FIXED since drive 3 in observed raster cases | Tasks 29/30 deliberately reproduce the relevant stall/close conditions. |
| New: Rust number/operator colour roles | edit-11v | NEW | Task 27 token rectangles differ from stated theme roles. |
| New coverage: journal atomicity / orphan session / small landings | edit-pgr / edit-rzj / edit-bml / edit-1vo / edit-r1l | NEW — recorded probes pass | Tasks 32/33; finite sampled outcomes, not proof of every crash timing or cache glyph. |

## Missing features Tobias would hit in daily use

1. **Find in files (edit-xkd, M2).** Ctrl+Shift+F still has no working project-search workflow. Single-buffer find is now useful, but cannot supply this daily capability (task 16).
2. **Complete emoji artwork on this installed font set (edit-r8e).** The woman-technologist cluster is still a missing-glyph symbol; covered-first-emoji clusters show only the first artwork. Correct bytes, caret movement and whole-cluster deletion do not restore the missing picture (task 20).
3. **Presentation parity (edit-1nm; edit-11v for token roles).** The minimap remains monochrome, default text/window geometry differs, initial syntax colour arrives after plain text, and sampled Rust number/operator roles are wrong. These are visible gaps, with the minimap explicitly outside the syntax bead’s scope (tasks 1/13/27).

New/Open/Save As/Quit, wrapped-wheel reversal, first-Return advance, Ctrl+Shift+H, gutter click, and reopening an existing file are no longer missing in the recorded runtime workflows.

## Edge cases and defects

- **Missing artwork — edit-r8e, STILL PRESENT.** Open the emoji fixture containing A👩‍💻B with the recorded fonts: the cluster shows a missing-glyph symbol. Undo/save returns the original byte hash (task 20).
- **Unavailable project search — edit-xkd, STILL PRESENT.** Press Ctrl+Shift+F: unsupported workflow remains (task 16).
- **Rust colour classification — edit-11v, NEW observation.** Open config.rs and inspect the number and assignment samples in the token table: number is keyword/constant colour and assignment is plain foreground. No persistent comment/string spill was established; this is a role mismatch, not document corruption (task 27).
- **Page placement — no bead, STILL PRESENT.** Use the inherited navigation PageDown/PageUp sequence: runtime view placement differs from Sublime despite valid caret movement. Do not describe the two as identical scrolling (task 3).
- **External reload undo policy — no bead, STILL PRESENT.** Save, externally append, then Undo: runtime retains the reloaded contents while the valid Sublime repeat restores pre-append contents and becomes dirty. Subsequent conflict actions therefore start in different states (task 19).
- **Changed-base crash recovery limit — edit-fi2 absent, STILL PRESENT.** Edit unsaved text, kill, rewrite the base (even with the same bytes), relaunch: disk content opens with a set-aside notice; unsaved edits are not automatically reapplied. The retained journal/notice avoids a silent startup failure, but automatic recovery remains limited (task 17).
- **Large copied-profile startup — edit-rfq/edit-rzj, NEW coverage.** No-argument relaunch of the recorded ordinary-profile copy eventually restores, but can exceed window discovery timeout. The workload contains large prior sessions and differs from drive 3; record the delay without attributing a regression or a renderer crash (task 17).
- **Sublime gutter drag comparison — no bead, NEW observation.** The final cell-centre click/drag/Shift+click capture returns whole-line, unchanged whole-line, extended-range readbacks respectively. Runtime returns the intended drag range. Cause is unestablished; neither an assumed parity nor a runtime defect (task 28).
- **Coverage gaps — NOT OBSERVED.** Estimate-to-exact label switching, each early queued key’s individual paint, GL stall handling, optional additional language coverage, top-edge autoscroll, and kills demonstrably landing mid-journal-drain are unproved. Blank/stale/incomplete Sublime comparisons are enumerated in the capture ledger. No new recordings were made to fill them.

## Latency and jank by feel plus numbers

The completed typing, selection, clipboard and file workflows look usable at the sampled states. Wrapped scrolling now returns to the expected view; the forced raster stalls no longer end the process, and the external-change close probes finish normally. The old orange minimap pulse was not detected. Syntax startup visibly progresses from plain text to colour; this delay is more noticeable than ordinary isolated typing. Rare large-file search and large-profile restoration can take a substantial wait. Their loads, power states and session contents differ from earlier runs, so these observations do not establish a speed regression or a universal speed advantage over Sublime.

The external onset inventory above supplies video timings. Internal trace percentiles below are copied literally from the existing tracedump text files; their counts include the driven navigation/edit workload and are not reinterpreted as just isolated glyphs. G1 is the specified typing guard; G3/T6 are diagnostic. No null-backend/ASan worker reference from the brief is treated as a video budget.

`traces/02-typing-r2-sublimite.txt`, all (M)[AC], load1=0.52:

```text
G1 T4-T0     n=93 p50=1.597 ms p99=46.409 ms
G3 T5-T0     n=92 p50=1.777 ms p99=17.304 ms
T6-T0        n=92 p50=18.147 ms p99=32.190 ms
```

Drive-3 reference, `build/astra3/traces/02-typing-r2-sublimite.txt`, (M)[AC], load1=0.87; loads differ:

```text
G1 T4-T0     n=92 p50=1.575 ms p99=40.395 ms
G3 T5-T0     n=91 p50=1.753 ms p99=18.295 ms
T6-T0        n=91 p50=16.961 ms p99=32.870 ms
```

`traces/27-syntax-c-sublimite.txt`, all (M)[AC], load1=3.22:

```text
G1 T4-T0     n=12 p50=2.318 ms p99=47.894 ms
G3 T5-T0     n=11 p50=3.647 ms p99=31.871 ms
T6-T0        n=11 p50=26.500 ms p99=44.205 ms
```

`traces/10-large-sublimite.txt`, all (M)[AC], load1=3.29:

```text
G1 T4-T0     n=27 p50=2.477 ms p99=64.580 ms
G3 T5-T0     n=26 p50=3.925 ms p99=7.052 ms
T6-T0        n=26 p50=20.364 ms p99=30.634 ms
```

Drive-3 reference, `build/astra3/traces/10-large-sublimite.txt`, (M)[AC], load1=15.26; loads differ:

```text
G1 T4-T0     n=27 p50=4.719 ms p99=65.024 ms
G3 T5-T0     n=26 p50=6.781 ms p99=110.772 ms
T6-T0        n=26 p50=25.107 ms p99=124.275 ms
```

`traces/31-large-index-sublimite.txt`, all (M)[AC], load1=2.12:

```text
G1 T4-T0     n=21 p50=2.535 ms p99=295.195 ms
G3 T5-T0     n=20 p50=4.017 ms p99=296.787 ms
T6-T0        n=20 p50=19.899 ms p99=316.730 ms
```

`traces/31-early-end-only-sublimite.txt`, all (M)[bat], load1=7.32:

```text
G1 T4-T0     n=2 p50=153.755 ms p99=767.956 ms
G3 T5-T0     n=1 p50=778.872 ms p99=778.872 ms
T6-T0        n=1 p50=802.248 ms p99=802.248 ms
```

Task 31’s printed counts differ from the prescribed input count. The correct net final view supports queued navigation, but the trace is not rewritten to make each key appear as a distinct presented frame. The isolated early-End battery sample is visibly slower than the settled large-file End sample; different index state/load/power prevent a matched regression claim. No drive-3 coloured-C or early-index trace counterpart exists.

Literal startup block, `traces/01-fresh-sublimite.txt`, all (M)[AC], load1=0.39 (internal first_present is not first matching video text):

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=39616074284249 elapsed_ms=0.000000
  display_connect      ns=39616114756567 elapsed_ms=40.472318
  egl_probe_start      ns=39616149551997 elapsed_ms=75.267748
  egl_probe_end        ns=39616149863296 elapsed_ms=75.579047
  raster_fallback      ns=39616118846898 elapsed_ms=44.562649
  font_atlas_ready     ns=39616111611330 elapsed_ms=37.327081
  file_prefix_read     ns=39616153100333 elapsed_ms=78.816084
  first_layout         ns=39616149668966 elapsed_ms=75.384717
  first_submit         ns=39616156040772 elapsed_ms=81.756523
  first_present        ns=39616156755597 elapsed_ms=82.471348
  egl_load_start       not recorded
  egl_load_end         not recorded
```

Literal startup block, `traces/10-large-sublimite.txt`, all (M)[AC], load1=3.29 (internal first_present is not first matching video text):

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
```

Literal startup block, `traces/27-syntax-c-sublimite.txt`, all (M)[AC], load1=3.22 (internal first_present is not first matching video text):

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
```

Literal startup block, `traces/31-large-index-sublimite.txt`, all (M)[AC], load1=2.12 (internal first_present is not first matching video text):

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
```

Literal startup block, `traces/31-early-end-only-sublimite.txt`, all (M)[bat], load1=7.32 (internal first_present is not first matching video text):

```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=45948897087198 elapsed_ms=0.000000
  display_connect      ns=45949019991998 elapsed_ms=122.904800
  egl_probe_start      ns=45949148269443 elapsed_ms=251.182245
  egl_probe_end        ns=45949161265464 elapsed_ms=264.178266
  raster_fallback      ns=45949050845084 elapsed_ms=153.757886
  font_atlas_ready     ns=45949000885572 elapsed_ms=103.798374
  file_prefix_read     ns=45949154096211 elapsed_ms=257.009013
  first_layout         ns=45949148331370 elapsed_ms=251.244172
  first_submit         ns=45949168030119 elapsed_ms=270.942921
  first_present        ns=45949172929291 elapsed_ms=275.842093
  egl_load_start       not recorded
  egl_load_end         not recorded
```

The copied-profile continuation trace reports first_present elapsed_ms=68503.171250 (M)[AC], load1=4.05 for its original launch (`traces/17-recovery-e-sublimite.txt`); the continuation capture itself has load1=2.71. This is internal launch timing, outside the first video, not an invented media onset.

VERDICT usable instead of Sublime for Tobias's daily edits: no; blockers: 1. missing find in files (edit-xkd); 2. missing emoji artwork (edit-r8e)
