> Note: recovered from the Codex rollout after -o overwrote it (replayed every write to this file in rollout-2026-10-11T21-18-13-01a12aaf-4a74-7be2-b337-71e48e6e4075.jsonl; result is 33,562 bytes, equal to the size the drive's own `ls -l` reported at 22:02).

# Astra vs Sublime Text: comparison drive 2

2026-10-11 · edit-w34.4 / P5.1b · Astra

**Not yet a replacement for Tobias's daily editor.** This build makes substantial progress: save, ordinary clipboard operations, Ctrl+Y, readable default text, and basic crash recovery work. Text-area mouse editing and the wheel still do nothing. Find/replace still has no panel, and typing a search query after Ctrl+F still edits the document. Unicode support is much better but the emoji sequence in the fixture is not fully rendered.

## Conditions and measurement limits

Drive 1 was read first. All interaction and capture in this drive used **DISPLAY=:98**, never :0 or :99. `xdpyinfo` measured **2880×1800 pixels, 243×243 DPI**. Drive 1 used :99; its different display configuration means the font comparison is a comparison of the supplied environments, not a controlled software-only DPI experiment. The supplied `build/sublimite` binary had filesystem modification time **2026-10-11 21:09:29 +1000**. Sublime reported **build 4200** through `subl --help`. Sublimité logged EGL failure followed by raster fallback, with “no retry.” No build, `git`, or `bd` commands were run, and no files under `src/`, `tests/`, `bench/`, `fuzz/`, or `tools/` were modified. Drive 1's `build/astra/` was left untouched.

Editable corpus files were copied into `build/astra2/files/`; the log was opened at `/tmp/edit-corpus/log_1g.txt` and never saved. Filesystem `stat` measured **1,048,576**, **67,108,864**, and **1,073,741,824 bytes** for the C, dense-short, and log fixtures. Input sequences come from drive 1's harness, with the requested additional bindings. Mouse coordinates were adjusted for the different chrome and DPI. Windows were raised and focused directly; key logs include focus checks. The two crash fixtures have editor-specific names and identical starting contents.

Evidence is under **`build/astra2/`**:

- Every video name below is relative to **`build/astra2/video/`**. Timestamps are seconds in that video.
- `logs/<stem>.json` contains wall-clock command envelopes, clipboard/disk reads, window geometry, and screenshot labels. `logs/<stem>.timeline.txt` translates these to video time using ffmpeg's capture start timestamp.
- `frames/` contains text-reading snapshots and extracted latency sequences. Stills establish text/layout, not absence of flashing or stutter.
- `traces/<stem>.txt` contains literal `build/tools/tracedump` output; `.bin` preserves the trace.
- `logs/environment.txt`, `font-metrics.json`, `startup-metrics.json`, `typing-metrics.json`, and `find-notice.json` preserve measurement details and image regions.

Videos use the requested X11 grab, **60 fps**, H.264 ultrafast, CRF 23, yuv420p; the recorder uses four encoder threads. Encoded timestamps contain capture gaps. Frame results below are **60 Hz resampled video intervals**, approximately **16.7 ms each**, measured from the logged command start to the first matching glyph crop. The actual key arrives somewhere within the command envelope. These are individual observations, not a hardware latency benchmark or population percentiles. Internal tracedump stages are reported separately.

The first ordinary launch restored an existing `shot-probe.md` session. After the first unsaved typing session, subsequent launches exited with **`error -103`** before opening a window. Failed recordings were preserved as `failed-02-paced-sublimite.mkv` and `failed-03-nav-sublimite.mkv`; an interrupted Sublime navigation recording is also retained. Later tests use an isolated **XDG_DATA_HOME per task under `build/astra2/state/`**; a crash/relaunch pair retains the same directory. This isolates test histories without deleting the user's recovery data. The ordinary-profile failure is a real observed limitation, not silently counted as a successful launch.

`FIXED`, `STILL PRESENT`, and `CHANGED` below classify the corresponding drive-1 divergence. Rows without an earlier divergence say so explicitly.

## 1. Open the C file; default presentation

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Launch a fresh path containing the same C fixture | First matching text at **1.267 s** after launch at **0.745 s**: **521 ms / 31.3 intervals**. | First matching text at **1.400 s** after launch at **0.773 s**: **627 ms / 37.6 intervals**. | **FIXED:** the conspicuous multi-second startup wait is gone in this recording; sublimité is below the requested one-second target. Sublime uses an already-running process; this is not a cold-process equivalence claim. | `01-fresh-{sublime,sublimite}.mkv` 0.70–1.45; `logs/startup-metrics.json`, crop matching at 60 Hz. |
| Read default text and measure initial window | **800×600** window; sampled Latin ink **11 px** high. | **1728×1260** window; sampled Latin ink **26 px** high, versus drive 1's **10 px**. | **FIXED** for tiny presentation on the supplied display: **2.6×** the prior ink height, not the anticipated 3–4×. **STILL PRESENT:** C text is monochrome rather than syntax colored. | Same videos; geometry from xdotool. `logs/font-metrics.json`: max RGB >130, excluding caret; sublimité rows 46–71 in ROI `(140,42)-(235,82)`, Sublime rows 63–73 in `(91,60)-(132,80)`. |
| Ctrl+= twice, Ctrl+−, Ctrl+0; launch with font override | No visible font-size change with these exact chords in this setup. | Enlarges twice, shrinks, resets. `EDIT_FONT_SIZE_PT=12` gives **32 px** sampled Latin ink instead of 26. | **FIXED:** a usable zoom/override workflow is now demonstrated. | `zoom-{sublime,sublimite}.mkv`, named larger/smaller/reset steps in timelines; `font12-sublimite.mkv`. Override ink rows 55–86 in `(180,50)-(300,92)`, same threshold, `font-metrics.json`. |

The startup trace independently reports `first_present elapsed_ms=94.441930`, with `egl_probe_start=87.462800` and `egl_probe_end=87.815236` milliseconds from process start (`traces/01-fresh-sublimite.txt`). Those internal markers are not substituted for the slower captured first-text measurement.

## 2. Slow and burst typing

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Same paragraph with `xdotool type --delay 100`; same burst with `--delay 0`, explicit Return keys | Complete paragraph and burst in order. | Complete paragraph and burst in order; caret reaches the following line. | No drive-1 insertion-order divergence; retained. **FIXED** edit-triggered orange behavior in the initial typing scans, including the darker color that exposed drive 1's false negative. | `02-typing-r2-sublime.mkv` 7–15.4; `02-typing-r2-sublimite.mkv` 7.18–15.3. Final/home text frames; flash evidence in task 13. |
| Isolated `Z`, extract thirty frames around the command | Glyph at **5.367 s**; command **5.337–5.344 s**: **29.8 ms / 1.8 intervals** from command start. | Glyph at **5.317 s**; command **5.264–5.275 s**: **52.8 ms / 3.2 intervals**. | **STILL PRESENT:** a small sampled onset difference, approximately the same qualitative result as drive 1. Capture and injection uncertainty preclude a precise speed ranking. | `02-typing-r2-*.mkv`; `logs/typing-metrics.json`; thirty cropped frames in `frames/02-typing-r2-*-glyph-sequence/`. |
| Repeat with actual one-character deadlines at nominal ten characters per second | Visible prefix preserved; readiness-delayed repeat copies the exact complete paragraph. | Exact complete paragraph verified by clipboard readback in the supplemental repeat. | No missed/reordered character found in the verified paragraph. Original C viewports clip the final words, so full-text claims use the readback rather than those stills. | `02-paced-{sublime,sublimite}.mkv` approximately 6–17; `02-paced-verified-sublimite.mkv` 5.121–14.555; `02-paced-verified-r2-sublime.mkv` 12.463–22.019. `paragraph_match: true` and literal expected/actual strings in each JSON. |

The first supplementary Sublime readback (`02-paced-verified-sublime.mkv`) began setup keys while its fresh process was still blank/loading and returned a mismatched line. It is retained as an invalid steady-state typing probe. The separately labeled repeat waits for readable text and passes.

## 3. Keyboard navigation and selection

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Arrows, Ctrl+arrows, Home/End, indented Home | Expected word and line positions. | Corresponding positions on the same fixture. | No drive-1 failure; retained. | `03-nav-sublime.mkv` 4.7–18.5; `03-nav-sublimite.mkv` 4.2–19.8; named per-key frames. |
| Page Down twice, Page Up, Ctrl+End/Home | Paged through the fixture and reached both ends. | Also paged and reached both ends. | Viewport-dependent page distance remains an expected difference, not a defect. | Same videos, Sublime 19–24.1, sublimité 20.5–25.9. |
| Separate Shift+Right, Ctrl+Shift+Right, Shift+Down; minimal repeated/held Shift probe | Retained the selection anchor. | Retained the selection anchor. | No drive-1 selection-anchor divergence; retained. | Same videos, Sublime 24.8–28.5, sublimité 26.7–31; `03-selection-*.mkv` 4.7–9.1. |

## 4. Mouse

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Text click, drag, double-click | Placed caret, selected a range, selected a word. | Caret stayed at the initial text position; no corresponding range or word selection appeared. | **STILL PRESENT:** ordinary text-area mouse editing is absent. | `04-mouse-sublime.mkv` 4.1–7.2; `04-mouse-sublimite.mkv` 4.17–7.9. |
| Wheel down, then up | Scrolled the document. | Text viewport remained at the beginning. | **STILL PRESENT:** wheel scrolling is absent. | Same videos, Sublime 7.5–10.5, sublimité 8.64–11.4. |
| Minimap click/drag | Moved the viewport. | Moved the viewport, showing regions beginning around lines **4,806** and **6,821** (line-number reads). | No drive-1 navigation failure; retained. The visual representation is assessed separately. | Same videos, Sublime 11.3–14.3, sublimité 12.30–15.2. |

## 5. Editing and undo/redo

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Ctrl+A, Delete, Ctrl+Z, Ctrl+Y | Deleted, restored, redid deletion. | Deleted, restored, redid deletion. | **FIXED:** Ctrl+Y now works. | `05-edit-sublime.mkv` 4–8.5; `05-edit-sublimite.mkv` 3.7–9.2. |
| Insert fifty digits with Left/Right between edits, fifty Ctrl+Z, fifty Ctrl+Y | Undo returned to baseline; redo restored the digits. | Same resulting baseline and digit sequence. | **FIXED:** the previous failed redo loop now succeeds. Counts are actual loop/input-command counts, not inferred history depth. | Same videos, Sublime 18.5–46, sublimité 22.5–49; `50-inserts`, `50-undo`, `50-redo` text frames. |
| Collapsed-caret Ctrl+L, delete, undo, redo; word/character deletion | Selected/deleted a line; adjacent word/character deletions worked. | Corresponding line and word results; Ctrl+Y redoes the line deletion. | **FIXED** redo mismatch; remaining probes retain drive-1 behavior. | `05-edit-r2-sublime.mkv` 5–12.9; `05-edit-r2-sublimite.mkv` 5–13.5; original edit videos' word/character steps. |

## 6. Clipboard and interoperability

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Seed CLIPBOARD sentinel; Ctrl+C/X/V | Copied, pasted, cut. | Replaced sentinel with `COPYME`; pasted; cut the doubled word and placed `COPYMECOPYME` on CLIPBOARD. | **FIXED:** copy/cut/paste are connected to the UI and use the actual selection. | `06-clipboard-sublime.mkv` 4.6–11; `06-clipboard-sublimite.mkv` 3.8–10.2; exact `xclip -o -selection clipboard` output in timelines. |
| Shift+Insert; seed PRIMARY, middle-click | Inserted clipboard and PRIMARY text. | Inserted both; middle-click put `PRIMARY_PASTE` at the clicked text location, despite inert left-click editing. | **FIXED:** alternate paste paths work. | Same videos, Sublime 11–14.5, sublimité 10.3–13.3. |
| Ctrl+Insert, Shift+Delete, paste | Copied/cut/pasted. | Copied/cut/pasted, with logged clipboard reads and visible deletion. | **FIXED** relative to the former disconnected clipboard; additional required bindings pass. | `06-alternate-{sublime,sublimite}.mkv` 2.5–9.3; copied-pasted/cut/pasted frames. |
| Sublime → sublimité → Sublime through X CLIPBOARD | Supplied `FROM_SUBLIME`; received the complete returned selection including `FROM_SUBLIMITE`. | Pasted Sublime's text, then exported the full edited selection. | **FIXED:** successful round trip, not stale sentinel data. Source process remained alive during the return paste. | `06-cross-source-sublime.mkv` 2.5–4.1; `06-cross-sublimite.mkv` 3–10.2; `06-cross-back-sublime.mkv` 0.7–2.7. Both final clipboard reads equal `FROM_SUBLIME\nFROM_SUBLIME\nFROM_SUBLIMITE`. |

## 7. Find/replace

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Ctrl+F, type `alpha`, Enter/Escape, F3 | Opened find panel and searched. | No find panel. Shortcut itself is consumed and displays “not implemented”; subsequent `alpha` and Return still modify the document. F3 gives a notice, not a search result. | **CHANGED** shortcut feedback; **STILL PRESENT** missing search and the destructive habitual search sequence. Do not call this workflow fixed just because Ctrl+F itself does not insert `f`. | `07-find-sublime.mkv` 4–8; `07-find-sublimite.mkv` 4.3–8.7; query/final text frames. |
| Ctrl+H | Opened replace panel. | No replace panel; “not implemented” notice. | **STILL PRESENT:** replace absent; feedback changed. | Same videos around 8.7–10.6; `logs/find-notice.json`. |

The video captures the bottom notice after Ctrl+F at **4.383–4.450 s (4 resampled frames)**, F3 at **8.283–8.500 s (13 frames)**, and Ctrl+H at **9.750–10.017 s (16 frames)**. These are visible-video durations measured by amber pixels in the bottom status strip and verified by reading extracted frames. “One frame” in the implementation does not mean it is visible for exactly one recorded frame.

## 8. Save and external changes

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Insert `EDITED_`, Ctrl+S, cat file | Persisted `EDITED_original on disk`; clean indicator. | Same disk contents; dirty asterisk clears. | **FIXED:** save is no longer inert. This verifies disk readback and UI state, not power-loss behavior. | `08-save-sublime.mkv` 3.5–5.5; `08-save-sublimite.mkv` 3.5–5.3; cat output at 5.215 s in sublimité timeline. |
| External `echo EXTERNAL_APPEND >> file`, wait/refocus/Ctrl+End | Reloaded appended text. | Shows `Source changed: R reload / K keep`, even though the preceding save cleared dirty state. Ctrl+End leaves the prompt. | **CHANGED:** the same prompt now follows a clean saved buffer, whereas drive 1's buffer was unsaved. Extra manual reload choice remains. No claim that R/K themselves fail. | Same videos, Sublime 5.5–11.6, sublimité 5.99–11.6; disk read confirms both saved prefix and external append. |

## 9. Tabs and CLI handoff

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Open PRD, editor source, UTF-8, dense-short and navigation via CLI | Accepted the five requests. | Opened the five additional tabs in the running IPC-owning instance. | No drive-1 small-handoff failure; retained. As in drive 1, this test launches sublimité **without `--new-instance`** so the process owns the CLI endpoint. | `09-tabs-sublime.mkv` 0.7–6.6; `09-tabs-sublimite.mkv` 0.7–6.3; command paths/status in JSON. |
| Ctrl+Tab twice, Ctrl+W, Ctrl+Shift+T | Switched, closed, reopened in the small-tab probe. | Switched, closed, reopened; navigation fixture returned. | No drive-1 failure; retained. | Same videos, Sublime 7–11.7, sublimité 7–12.3. |
| One hundred additional unique CLI opens | All **100 returned 0**. Harness later received SIGTERM during the post-loop dwell, so the recording lacks final visible-tab readback. | All **100 returned 0**; `tab-099` visibly opened. Subsequent cycling/close/reopen worked. | **FIXED:** previous 64-success/36-failure stress ceiling. `/proc/<pid>/fd` contained **one inotify descriptor** after the loop. Counts come from command/result pairs and descriptor symlinks, not assumed tab totals. | Sublime CLI loop `09-tabs-sublime.mkv` 11.681–17.150; sublimité loop 12.252–13.409, `hundred` view 43.454, cycle/reopen 44.747–47.419; `logs/interruption.txt` records the unexplained harness termination. |

## 10. Large files

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Open the in-place 1 GiB log | Standard-session recording never reached a usable text view in its observation window. A clean `--safe-mode` repeat initially had startup dialogs; after dismissing them, a loaded-buffer comparison was possible. | First matching text at **0.867 s**, after launch at **0.709 s**: **157 ms / 9.4 intervals**. | **CHANGED:** sublimité's initial view is much faster than drive 1. No uncontaminated initial-load completion time is claimed for Sublime. Safe mode is an explicitly different environment for the follow-up, not silently substituted for the standard launch. | `10-large-sublimite.mkv` 0.71–0.89; `logs/large-metrics-sublimite.json`. Standard `10-large-sublime.mkv` 0.74–85.1; clean attempts `10-large-clean-sublime.mkv` and `10-large-clean-r2-sublime.mkv`, latter's ready text at 148.584 after dialog dismissal. |
| Ctrl+End, Page Up ×20, return to end | Once loaded, reached final line and paged away successfully. End viewport matched after **159 ms / 9.5 intervals** from the command. | Reached final line **8,947,842**, paged back to a viewport beginning at **8,947,542**, and returned successfully (visible line-number reads). End crop matched after **66.5 ms / 4.0 intervals**. | **FIXED:** drive 1's post-End/Page-Up stall is absent in the completed run. Page distances depend on viewport and wrapping. | `10-large-sublimite.mkv`: Ctrl+End 47.833, final-line crop 47.900, page-up result 56.402. Loaded clean-session `10-large-resume-sublime.mkv`: Ctrl+End 1.691, final-line crop 1.850, page-up result 19.475. Both metric JSON files and extracted sequences retained. |
| Type `Y` near end, then `Z` at line 1 | Both appeared: **Y 27.2 ms / 1.6 intervals**, **Z 23.7 ms / 1.4 intervals** from command start. | Both appeared: **Y 61.3 ms / 3.7 intervals**, **Z 61.9 ms / 3.7 intervals**. Later end navigation still worked. | **FIXED:** both requested edits now have confirmed glyph onsets in sublimité; drive 1 had none near the end. Sublime's loaded clean session has faster sampled typing onsets here. | `10-large-sublimite.mkv`: Y command 59.539–59.552, glyph 59.600; Z command 64.288–64.304, glyph 64.350. `10-large-resume-sublime.mkv`: Y command 22.373–22.380, glyph 22.400; Z command 27.226–27.235, glyph 27.250. Thirty-frame sequences per glyph; `logs/large-metrics-*.json`. |
| Also open the 64 MiB dense-short fixture, Ctrl+End, Page Up, Ctrl+Home, `Z` | After its initial blank launch view, reached the end and showed the first-line `Z`. | Reached final line **26,845,451** (UI read), paged, returned home and inserted `Z`. | **CHANGED:** drive 1's incomplete end-navigation check is now completed in both. | `dense64-sublime.mkv` end 57.792, typed 59.734; `dense64-sublimite.mkv` end 49.889, typed 51.971; named frames and key logs. |

The first large-file pass deliberately allowed a longer initial dwell before navigation so both editors had a chance to load. A second sublimité recording restores drive 1's short pre-navigation dwell: `10-large-early-sublimite.mkv` sends Ctrl+End at **6.779 s**, shows the twenty-page-up result at **15.378 s**, the end edit at **20.427 s**, the beginning edit at **25.185 s**, and successful later end navigation at **43.124 s** (logged actions/frame labels). It also passes, so the longer dwell did not hide the old stall. `traces/10-large-early-sublimite.txt` reports G1 `n=27 p50=4.078 ms p99=53.779 ms`, G3 `n=26 p50=6.358 ms p99=13.046 ms`, and T6 `n=26 p50=25.836 ms p99=40.518 ms`. Trace percentiles are quoted in the final latency section, rather than used as a substitute for visible completion.


## 11. Resize

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Resize to 1440×900, 2880×1800, 800×600, then 960×600; Page Down/Up at each | Eventually laid out text at the requested sizes; early views in this post-crash launch were blank. | Redrew text/minimap at each size. No persistent garbage or stale cells in settled active-window views; glyph size stays constant. | No prior persistent-resize failure; retained. This C-file test does not certify all wrapping behavior. “Back” is the same final 960×600 request as drive 1, rather than the new default geometry. | `11-resize-sublimite.mkv` 4.392–16.2; `11-resize-sublime.mkv`, named size steps in timeline and frames. Dimensions are xdotool requests. Moving-minimap flash evidence is reported in task 13. |

## 12. SIGKILL and recovery

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Type `UNSAVED_RECOVERY_MARKER_`, dwell, SIGKILL, relaunch same file | Relaunched fixture showed `disk baseline`; marker absent in the specifically refocused fixture window. | Marker was restored and the buffer remained dirty; disk still contained only `disk baseline`. | **FIXED** for sublimité's ordinary crash replay. Sublime still did not recover this particular session; this is not a general claim about its hot-exit feature. | `12-hotexit-sublimite.mkv`: edit 3.154–4.701, kill 18.309, relaunch 20.448, restored view 26.542. Sublime: edit 3.180–4.604, kill 18.058, relaunch 19.695, fixture view 26.055. Dwell markers show approximately twelve seconds untouched before kill. Disk readbacks are in the timelines. |
| Additional isolated repro: unsaved edit, SIGKILL, replace file externally, relaunch | Not repeated as a matched Sublime edge case; ordinary crash result above is the comparison. | Exits **1** with **`error -103`**, no window; preserved journal remains in the isolated state directory. | **CHANGED / new blocker:** recovery works for an unchanged base file but a changed base can prevent startup. This also provides a controlled failure independent of the earlier ordinary-profile launch failures. No claim that journal data was deleted. | `12-recovery-conflict-sublimite.mkv`: edit 4.333–4.979, kill 9.071, external replacement 9.572, relaunch 9.665; failure observed by 27.529. `.editor.log` and `.json` give stderr/exit code. |

## 13. Minimap and orange flash

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Same C file; page, jump end/home, edit | Syntax-colored miniature preserves recognizable source structure. | Fine per-character marks and a translucent viewport band replace the solid coarse bars. On the large C fixture it still looks like a compressed monochrome striped texture, with less recognizable detail than Sublime. | **CHANGED:** substantially improved composition and useful navigation, but still a presentation-quality gap. | `13-minimap-sublime.mkv` 4–13.7; `13-minimap-sublimite.mkv` 4.3–16.4; also mouse/minimap navigation videos. |
| Scan every sublimité recording for the requested and drive-1 rendered colors | No matched whole-suite color scan requested for Sublime; its colored source/minimap would need a separate interpretation. | No minimap appear/vanish flash pairs in the completed scans, including typing, edits, large-file navigation and idle. | **FIXED** in these recordings: drive 1's edit-triggered minimap pulse is not reproduced. | Per-video counts below; full detector stdout in `logs/<stem>.flash-<color>.txt`. |
| Look for other orange UI after actions | Find/replace panels provide persistent controls. | Ctrl+F/F3/Ctrl+H produce the bottom “not implemented” notice; external append produces the top reload/keep prompt. | Orange still appears as deliberate text feedback after identifiable actions; it is not the old minimap overlay. | `07-find-sublimite.mkv` 4.383–4.450, 8.283–8.500, 9.750–10.017; `08-save-sublimite.mkv` after 5.992. |

`tools/flashscan.py` ran on the actual per-task MKVs, with **#bf853d/tolerance 40**, **#ff8800/tolerance 60**, and drive 1's corrected **#6b5030/tolerance 24**, minimum jump **200 pixels**. Counts are appearances followed by disappearance within **30 resampled frames**, as defined by that script. The routine minimap ROI is **(1568,40,160,1220)** within the default window. The initial typing checks used the wider **(1472,0,256,1260)** ROI; their saved outputs are retained. Zoom can change the minimap's left edge; the scanned strip remains within it. No claim is made that these color tests detect every conceivable visual anomaly.

Resize also has a **moving-minimap** scan. `video/11-resize-minimap-track.mkv` is a lossless derived strip following each recorded window-width command; `logs/resize-tracking.json` retains the exact filter and transition times. All three scans report **zero flashes over 1,048 resampled frames** (`logs/11-resize-tracked.flash-*.txt`). That supplements, rather than relies on, the incomplete fixed-position resize scan.

| Recording | #bf853d / tol40 | #ff8800 / tol60 | #6b5030 / tol24 |
|---|---:|---:|---:|
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
| `dense64-sublimite.mkv` | 0 | 0 | 0 |
| `failed-02-paced-sublimite.mkv` | 0 | 0 | 0 |
| `failed-03-nav-sublimite.mkv` | 0 | 0 | 0 |
| `font12-sublimite.mkv` | 0 | 0 | 0 |
| `utf8-sublimite.mkv` | 0 | 0 | 0 |
| `zoom-sublimite.mkv` | 0 | 0 | 0 |

All **28 original sublimité recordings** have all three completed scans (**84 detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.

## 14. Idle

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Focus and leave untouched for a measured minute | Text and caret location stayed stable; no detected changes in the sampled caret neighborhood. | No non-caret image changes. **20 caret-region transitions**, then blinking stops; caret position and text do not drift. | No drive-1 idle defect; retained. Video cannot distinguish identical-pixel repaints. | `14-idle-sublime.mkv` **5.798–65.800 s**, **60.002 s** by logged marker subtraction; `14-idle-sublimite.mkv` **4.484–64.501 s**, **60.017 s**. `logs/idle-*-metrics.json`: grayscale delta >30 and >20 changed pixels, excluding the specified caret region; **zero outside-region events** in both. |

## Additional UTF-8 comparison

| Action | Sublime did | sublimité did | Divergence / drive-1 status | Evidence |
|---|---|---|---|---|
| Open accents, Greek, CJK, emoji; End/Left/Backspace, undo; inject `é中文😀` | Rendered the full original fixture, including the woman-technologist emoji; inserted Unicode. | Accents, Greek, CJK and the smiling emoji now render; injected text appears. `👩‍💻` still displays as a missing-glyph symbol. | **CHANGED:** the blanket non-ASCII failure is fixed for common scripts, but this emoji sequence still fails visually. Byte preservation of the original sequence is not inferred from its replacement glyph. | `utf8-sublime.mkv` 4.25–8.8; `utf8-sublimite.mkv` 4.03–9.4; original/unicode/typed-unicode frames. |

## Missing features Tobias would hit in daily use

Ranked by impact in this drive:

1. **Text mouse controls and wheel scrolling.** Ordinary caret placement, dragging, word selection and scrolling remain inert. A working minimap and middle-click paste do not provide these controls.
2. **Find/replace UI that owns subsequent typing.** The notice gives feedback, but the expected Ctrl+F/query/Enter workflow still changes the document.
3. **Recovery that survives an externally changed file.** The controlled crash/change/relaunch sequence prevents startup with `error -103`; ordinary unchanged-file recovery passing is not enough.
4. **Complete Unicode presentation.** Common scripts are now readable, but the fixture's woman-technologist emoji remains a missing-glyph symbol.
5. **Sublime-like code presentation and minimap detail.** Source text remains monochrome; the minimap is much quieter and finer, but still looks like a compressed striped texture rather than Sublime's recognizable syntax-colored miniature.

Save, the tested clipboard paths, Ctrl+Y, readable DPI-aware presentation, zoom, ordinary crash replay, the CLI stress ceiling, and the recorded 1 GiB interaction are no longer blockers established by this drive.

## Edge cases and defects

- **Mouse editing inert:** open the C fixture, click/drag/double-click text, then wheel down/up; the text caret/selection/viewport do not respond, while minimap navigation does.
- **Search sequence edits text:** Ctrl+F, type `alpha`, Return; the notice appears briefly, then the query and newline become document content.
- **Recovery blocks launch on changed base:** type unsaved text, SIGKILL, replace that file externally, relaunch using the same recovery directory; exit 1 / `error -103`, no window.
- **Emoji sequence fallback incomplete:** open `utf8.txt` containing `👩‍💻`; sublimité displays a missing-glyph symbol where Sublime displays the emoji.
- **Clean external changes still interrupt editing:** save, append externally, Ctrl+End; the `R reload / K keep` prompt remains instead of showing the append automatically. The choices themselves were not exercised here.
- **Sublime comparison limits:** the ordinary crash probe did not recover the marker; the standard 1 GiB run remained blank, and its process was absent when checked after the harness ended. The dense-short test did complete, and the later loaded clean-session 1 GiB comparison also completed. The cause of that disappearance is unknown and is not attributed to a proven Sublime defect.

## Latency and jank by feel plus numbers

Small ASCII edits feel usable, and the orange edit pulse that made drive 1 look unstable is no longer reproduced. The remaining immediate frustration is functional: clicks and wheel do nothing, and a habitual search sequence types into the buffer. The successful large-file run is a substantial improvement over drive 1's frozen near-end interaction.

Literal small-file trace (`traces/02-typing-r2-sublimite.txt`):

```text
G1 T4-T0     n=93 p50=1.782 ms p99=66.562 ms
G3 T5-T0     n=92 p50=1.940 ms p99=17.049 ms
T6-T0        n=92 p50=17.750 ms p99=30.953 ms
```

Literal large-file trace (`traces/10-large-sublimite.txt`):

```text
G1 T4-T0     n=27 p50=5.711 ms p99=59.721 ms
G3 T5-T0     n=26 p50=7.171 ms p99=18.738 ms
T6-T0        n=26 p50=29.580 ms p99=40.543 ms
```

These are tracedump percentiles over the stated samples, not end-to-end hardware measurements. Unlike drive 1's tiny incomplete large-file trace, this recording also visibly reaches both ends and paints both requested edits. Externally, the isolated small glyph takes about **3.2 resampled intervals / 52.8 ms** in sublimité versus **1.8 / 29.8 ms** in Sublime; sublimité's large-file end and beginning glyphs take **3.7 intervals / about 61–62 ms**. Methods, command envelopes, crop coordinates and full errors are retained in the metric JSON files. Capture gaps and workload scheduling limit fine comparisons.

The deadline-paced paragraph contains **83 injected characters**; **82 command-start intervals** span **8.200089 s** in sublimité and **8.200038 s** in Sublime, both approximately **10 intervals/s**, calculated in `logs/pacing-metrics.json`. The separate `--delay 100` command is retained exactly as requested; its flag is not mistaken for a measured ten-character-per-second rate.

This is a comparison drive, not an exhaustive wrap, font coverage, power-loss durability, memory, or backend benchmark. The failed attempts and standard-session Sublime 1 GiB limits remain in the evidence rather than being replaced with invented completion times.

VERDICT usable instead of Sublime for Tobias's daily edits: no; blockers: text-area mouse editing and wheel scrolling, find/replace and query typing into the document, startup failure when a recovery base file changes, incomplete emoji rendering.

## Astra's original final message (the 411-byte `-o` output that overwrote this file)

Written: [comparison report](/home/tobias/Projects/sublimite/docs/reviews/astra-vs-sublime-2.md) and [worker log](/home/tobias/Projects/sublimite/docs/worker-reports/edit-w34.4-drive2.md).

Verdict: **no**. Remaining blockers: mouse/wheel, find/replace, recovery after external file changes, and incomplete emoji rendering.

Videos are in `build/astra2/video/`. All 84 minimap color scans reported zero flashes.
