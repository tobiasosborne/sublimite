# Astra vs Sublime Text: comparison drive 1

2026-10-11 · edit-w34.4 / P5.1b · Tobias's first-build complaints

**No: this build cannot replace Sublime for daily edits.** Saving, clipboard operations, text-area mouse editing, find/replace, and Unicode display are immediate blockers. Small-file typing and much of keyboard navigation work, but the orange flash is real and accompanies edits. The minimap is a coarse block diagram rather than a miniature rendering of the document.

## Conditions and evidence conventions

All launches, input, screenshots and recordings used `DISPLAY=:99`, the supplied **2880×1800** virtual display (verified with `xdotool getdisplaygeometry`). No interaction with `:0`; no `bd` or `git` commands; no changes under `src/`, `tests/`, `bench/`, `fuzz/` or `tools/`. The existing whole-session recorder was left running. Sublime identified itself as **build 4200**, measured with `subl --help`, with “LICENSE UPGRADE REQUIRED” in its title. Its initial update dialog was dismissed. Every sublimité launch logged EGL/DRI3 failure and **raster fallback**. These results describe that supplied build and rendering path.

There is no EWMH window manager on this display (`_NET_SUPPORTING_WM_CHECK` absent). The driver used `windowraise` and `windowfocus` instead of assuming `windowactivate` would work. Both editors were tested with identical fixture contents and key sequences; mouse targets were adjusted for their different chrome and minimap positions. All editable fixtures were copied under `build/astra/files/`; `/tmp/edit-corpus/log_1g.txt` was opened in place and never saved. Actual source sizes were **1,048,576 bytes**, **67,108,864 bytes**, and **1,073,741,824 bytes** for the C, dense-short, and large-log fixtures (filesystem sizes: 1 MiB, 64 MiB, 1 GiB).

Evidence directories, relative to the repository root:

- `build/astra/video/`: per-task MKV recordings; filenames below are relative to this directory.
- `build/astra/logs/<video-stem>.json`: wall-clock timestamps before/after each input command, screenshots, CLI exit codes, clipboard reads and disk reads. Matching `.timeline.txt` files translate these to video-relative seconds.
- `build/astra/frames/`: screenshots for reading exact text, plus thirty-frame latency sequences. Stills are **not** used to establish flashing, latency or stutter.
- `build/astra/traces/`: binary trace rings and the literal output of `build/tools/tracedump` after graceful exits.

Recordings used the requested X11 grab, H.264 ultrafast, CRF 23, yuv420p, configured at **60 fps**; the per-task encoder used **4 threads**. The whole-session recorder ran concurrently. Capture timestamps have gaps: this is not a lossless hardware latency benchmark. Frame measurements below use **60 Hz resampled video slots**, one slot ≈ **16.7 ms**, anchored to ffmpeg's logged input start time and the input-command timestamps. Actual ingress lies within the command's start/end envelope; capture sampling adds uncertainty. Native frame timestamps were checked around the principal startup and single-glyph onsets. `capture-quality.json` records encoder-reported drops, which do not account for every timestamp gap. Do not interpret these observations as sub-frame precision or stable population percentiles.

The early `01-open` attempt failed title matching and its recording ended prematurely; `01-open-r2` was too short for a reliable startup assessment. They are retained as failed attempts. Initial `xdotool type` strings containing literal newlines did not produce the intended line breaks in either editor; `02-typing-r2` repeats the comparison with explicit `Return` keys. No newline-injection failure is attributed to either editor.

## 1. Open the C fixture; default presentation

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Launch the same C fixture using the supplied commands | With a fresh fixture path not previously opened in Sublime, text matched the painted reference at video **0.883 s**, after launch at **0.775 s**: about **6.5 60 Hz intervals / 108 ms**. | Text appeared at **3.117 s**, after launch at **1.034 s**: about **125.0 intervals / 2,083 ms**. The preceding frames are black. | A conspicuous startup wait in sublimité. This fresh-path repeat has a warm filesystem and an already-running Sublime, matching the supplied launch setup; it is not a cold-process comparison. | `01-fresh-sublime.mkv` 0.775–0.90; `01-fresh-sublimite.mkv` 1.034–3.13; `logs/startup-fresh-metrics.json`, reference-crop matching. Earlier warm repeat in `01-open-r3-*.mkv` gave the same qualitative result. |
| Inspect default font and initial window geometry | Initial window **800×600**; sampled “tatic” ink occupies rows **63–73**, **11 px** high. | Initial window **960×600**; same letters occupy rows **17–26**, **10 px** high. Monochrome C text. | Both are small on this display; sublimité is smaller, with less syntax distinction. Its size does not grow with the screen. | `01-open-sublime.mkv`, `01-open-r2-sublimite.mkv`; geometry in matching JSON. Font measured from `frames/01-open-r3-*-painted.png`, excluding the caret, threshold max RGB >130; ROIs `(91,60)-(132,80)` and `(56,15)-(95,30)`. These are ink heights, not inferred font point sizes. Later windows inherit geometry changed by the resize test. |

## 2. Slow typing and burst input

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Type the paragraph with `--delay 100`, then `BURST_0123456789_alpha_beta_gamma_END` with `--delay 0`; explicit Return between lines | Full paragraph and burst visible in order; caret on the following line. Wraps the paragraph in its narrower text viewport. | Full paragraph and burst visible in order; caret on the following line. No missed/reordered characters found in this ASCII sample. | Basic insertion works. The minimap repeatedly turns orange during sublimité typing. | `02-typing-r2-sublime.mkv` 7.70–14.9; `02-typing-r2-sublimite.mkv` 6.58–13.5. Final text in matching `*-burst.png`/`*-home.png`; flash results in task 13. Original C-file pass: `02-typing-{sublime,sublimite}.mkv`. |
| Inject isolated `Z`, inspect thirty extracted frames | First glyph at **5.667 s**, command envelope **5.629–5.670 s**; about **2.3 60 Hz intervals / 38 ms** from command start. | First glyph at **4.917 s**, command envelope **4.864–4.872 s**; about **3.2 intervals / 53 ms** from command start. | A small recorded difference; command/capture uncertainty prevents a firm fine-grained ranking. | `02-typing-r2-*.mkv`; `logs/latency.json`; `frames/02-typing-r2-*-latency/001.png` through `030.png`. Glyph ROIs and matching error are logged. |
| Inspect slow-first-letter / burst-first-letter onset | `A`: **0.8 intervals / 13 ms**; `B`: **7.5 intervals / 125 ms**, from command start. | `A`: **2.9 intervals / 48 ms**; `B`: **3.1 intervals / 51 ms**. | Burst scheduling and capture variation matter; neither editor wins every recorded onset. These measure first glyph, not entire-burst completion. | `logs/more-latency.json`, corresponding thirty-frame `*-slow-latency/` and `*-burst-latency/` sequences. Sublime burst command itself spans **78 ms**, measured in its JSON; sublimité's spans **32 ms**. |

The requested `--delay 100` flag was used exactly. It did **not** yield a measured ten characters per second in this xdotool run: the **83-character** paragraph command lasted **4.678 s** in sublimité and **4.952 s** in Sublime (command timestamp differences). The requested nominal pace and the actual injection pace must not be conflated.

A further run enforced actual pacing with one character command on successive **100 ms** deadlines. In `02-paced-sublimite.mkv` 5.99–15.4 and `02-paced-sublime.mkv` 6.39–16.0, all **83 characters** were injected. The **82 inter-character intervals** span **8.20048 s** and **8.20463 s**, respectively: **9.999** and **9.994 intervals/s**, calculated from the logged first/last command starts. Sublimité shows the complete paragraph in its wider viewport. Sublime clips its right end in C mode; `02-paced-check-sublime.mkv` selects/copies the line, and the logged clipboard value exactly matches the full paragraph. Both preserve the input order. This supplies the requested genuine ten-character-per-second drive in addition to the exact `--delay 100` command comparison.

## 3. Keyboard navigation and selection

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Right, Ctrl+Right/Left, Down/Up, End/Home including an indented line | Reached word ends/beginnings and line ends; Home on the indented line reached first non-whitespace. | Same visible positions on these probes. | The basic keyboard-navigation surface is useful. Plain Left/Right were also injected between every digit in the edit-history loop (task 5). | `03-nav-sublime.mkv` 3.6–14.2; `03-nav-sublimite.mkv` 3.5–12.7. Per-step named frames and key sequence in JSON. |
| Page Down twice, Page Up, Ctrl+End, Ctrl+Home | Paged by its viewport, reached final empty line, returned to start. | Paged by its taller text viewport, reached end and returned to start. | Different page distances are expected from different line height/chrome, not by themselves a defect. | Same videos: Sublime 14.5–18.9; sublimité 12.9–16.5. |
| Shift+Right, Ctrl+Shift+Right, Shift+Down, each sent as a separate chord | Extended selection from the original start; final status showed **2 lines, 29 characters selected** (UI read). | Also extended from the original anchor. Pixel inspection confirms that all earlier characters remain selected. | No selection-anchor defect established. A misleading scaled preview was corrected by the minimal repeat and exact pixel checks. | Same videos: Sublime 19.4–22.1; sublimité 16.7–19.0; `frames/03-nav-sublimite-{18-shift+Right,19-ctrl+shift+Right,20-shift+Down}.png`. |

The minimal repeat `03-selection-{sublime,sublimite}.mkv` also passed: two separate Shift+Right chords select the first two characters; holding Shift for three Right presses selects the first three. Selection-background pixels in the original sublimité frames are RGB(66,84,113) across the entire expected range, including the initial character.

## 4. Mouse

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Click text, drag, double-click a word | Placed caret, selected a range, selected a word. | Text caret remained at the initial position; no corresponding range or word selection appeared. | Text-area mouse editing absent in these probes. | `04-mouse-sublime.mkv` 3.8–7.5; `04-mouse-sublimite.mkv` 3.6–7.0. |
| Wheel down then up | Scrolled text. | View stayed at the beginning. | Wheel scrolling absent. | Same videos: Sublime 7.5–10.8; sublimité 7.0–10.1. Wheel event counts and directions in JSON. |
| Minimap click and drag | Moved the viewport using the miniature document. | **Did navigate**: click showed the region around line **10,297**, drag around **14,509** (visible line-number reads). | Minimap navigation is implemented, despite the unusably coarse visual representation. It must not be grouped with the non-working text-area mouse controls. | Same videos: Sublime 10.8–13.5; sublimité 10.1–12.6. |

## 5. Editing and undo/redo

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Ctrl+A, Delete, Ctrl+Z, Ctrl+Y | Selected/deleted all, restored it, then redid deletion. | Selected/deleted all and restored it; Ctrl+Y left restored text and selection unchanged. | Ctrl+Y is not a working redo binding here. | `05-edit-sublime.mkv` 3.7–8.3; `05-edit-sublimite.mkv` 3.5–7.0. |
| Insert **50 digits** with intervening cursor moves; **50 Ctrl+Z**, then **50 Ctrl+Y** | Undid and redid the edits. | Undid to the baseline; redo loop left the baseline unchanged. | The exact requested redo sequence fails. Counts are input-command counts in JSON, not assumed history depth. | Same videos: Sublime 15.3–37.8; sublimité 12.5–29.5; `*-50-inserts`, `*-50-undo`, `*-50-redo` frames. |
| Ctrl+L from a collapsed caret; delete line, undo, try both redo chords | Selected/deleted one line; redo worked. | Selected/deleted one line; Ctrl+Y still did nothing; **Ctrl+Shift+Z redid deletion**. | Redo machinery exists; the expected binding differs. The earlier Ctrl+L probe retained a pre-existing all-selection, so this repeat isolates it. | `05-edit-r2-sublime.mkv` 5.0–10.0; `05-edit-r2-sublimite.mkv` 4.7–9.5. |
| Ctrl+Backspace, Ctrl+Delete, Backspace/Delete | Deleted the adjacent word/character as applicable. | Corresponding word/character deletion visible on the small fixture. | No additional divergence established for these isolated deletions. | `05-edit-*.mkv`, word-delete and final steps; `05-edit-r2-*.mkv` delete-word step. |

## 6. Clipboard and cross-application paste

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Seed X CLIPBOARD with `EXTERNAL_CLIPBOARD`; select text, Ctrl+C/X/V | Clipboard read changed to `COPYME`; paste inserted it; cut removed text and changed clipboard. | Clipboard reads remained exactly `EXTERNAL_CLIPBOARD`; no insertion/removal from C/X/V. | Copy, cut and paste are not connected to the editing UI. | `06-clipboard-sublime.mkv` 4.0–9.0; `06-clipboard-sublimite.mkv` 3.8–8.9; literal `xclip -o -selection clipboard` output in JSON. |
| Shift+Insert; seed PRIMARY with `PRIMARY_PASTE`, middle-click | Inserted clipboard/PRIMARY content. | Neither method inserted content. | Alternate Linux paste paths do not rescue the workflow. | Same videos, Sublime 9.0–11.8; sublimité 8.9–11.5. |
| Copy `FROM_SUBLIME` in Sublime; paste into sublimité; type/select/copy `FROM_SUBLIMITE`, paste back | Source copy worked; paste-back inserted another `FROM_SUBLIME`, the unchanged old clipboard. | Paste did nothing; attempted copy-back did not replace the clipboard. | Interoperability fails in both directions; the apparent paste-back is stale data, not a successful round trip. | `06-cross-source-sublime.mkv`; `06-cross-sublimite.mkv` 4.5–8.0; `06-cross-back-sublime.mkv`; associated clipboard reads and final text. |

## 7. Find/replace

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Ctrl+F, type `alpha`, Enter/Escape, F3 | Opened find UI and searched; F3 operated on that search. | No find panel. `alpha` was inserted into the document; Return added a line. F3 made no visible search movement. | A habitual search sequence silently edits the document instead. | `07-find-sublime.mkv` 3.7–7.1; `07-find-sublimite.mkv` 3.5–6.8. |
| Ctrl+H | Opened replace UI. | No replace UI or visible action. | Replace absent. Replacement execution was not tested beyond opening the panel. | Same videos around 7.3/7.7 s; `*-replace-panel.png`. |

## 8. Save and external modification

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Insert `EDITED_`, Ctrl+S, then `cat` the fixture | Disk began `EDITED_original on disk`. | Buffer showed `EDITED_original on disk`, but disk still began `original on disk`; dirty marker remained. | Save does not persist the edit. | `08-save-sublime.mkv` 3.7–5.6; `08-save-sublimite.mkv` 3.6–5.2; exact `cat` output in JSON. |
| `echo EXTERNAL_APPEND >> file`, wait, refocus, Ctrl+End | Reloaded and showed appended line with the saved edit retained. | Displayed **“Source changed: R reload / K keep”** in the text area; Ctrl+End did not dismiss the prompt. | External-change detection works, but the unsaved buffer requires a choice. This test did not exercise the R/K choices and does not claim they fail. | Same videos, Sublime 5.6–11.5; sublimité 5.2–10.0. Disk reads verify the append. |

## 9. Tabs and CLI handoff

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Open five additional fixtures via CLI into a running window | Opened PRD, editor source, UTF-8, dense-short and navigation fixtures. | Opened them as tabs; text of navigation/dense-short was shown when switched. | Basic handoff works when sublimité is launched **without `--new-instance`** to own the IPC endpoint. All other isolated tests use the requested flag. | `09-tabs-sublime.mkv` 0–6.6; `09-tabs-sublimite.mkv` 0–6.1; CLI paths/exit statuses in JSON. |
| Ctrl+Tab twice, Ctrl+W, Ctrl+Shift+T | Switched recent tabs, closed, reopened. | Also switched, closed and reopened. | No failure on the small clean-tab probe. | Same videos: Sublime 6.6–10.3; sublimité 6.1–9.2. |
| Loop over **100 additional unique files** | All **100** CLI commands returned **0**; final `tab-099` opened. | First **64** returned **0**; **36** returned **2** with `sublimite: handoff error 8`, beginning at `tab-064`; last visible newly opened fixture was `tab-063`. | Repeatable handoff/stress ceiling. This establishes rejected requests, not a proven internal “64 total tabs” limit: earlier tabs were already open. | Same videos: Sublime 10.36–21.4; sublimité 9.22–14.5. Counts calculated from the 100 command/result pairs in `logs/09-tabs-*.json`. |

## 10. Large files

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Open the in-place 1 GiB log | Still blank throughout the initial **24.0 s after launch** observation window; initial keys had no visible effect. Subsequent CLI requests were also delayed while large fixtures loaded. | Painted the beginning. In the focus-checked repeat, first matching text at **2.750 s**, launch **0.757 s**: **119.6 60 Hz intervals / 1,993 ms**. | Sublimité reaches readable initial text sooner here. Sublime's complete initial load time was **not established** by the per-task video, so no invented completion time. | `10-large-sublime.mkv` 0.74–24.75; `10-large-r3-sublimite.mkv` 0.76–2.77; `logs/large-metrics.json`. |
| Ctrl+End before editing, Page Up ×20 | Once loaded, Ctrl+End reached the final line; Page Up moved away from the end; end and beginning edits subsequently appeared. | Ctrl+End eventually reached final empty line **8,947,842** (UI read). After the page-up sequence, viewport stayed around **8,947,823**; subsequent Home/End/typing did not give the requested usable editing state. | Large-file navigation stalls despite correct focus. The small-file navigation result does not generalize. | `10-large-resume-sublime.mkv` 2.58–11.7; `10-large-r3-sublimite.mkv` 6.07–43; all focus checks in the latter match the intended window. |
| Type at the beginning and near the end | Resumed, loaded-buffer video visibly inserts `Y` near the end and `Z` at the beginning. | First attempt inserted `Z` at line 1, then failed to navigate to the end. In the navigation-first repeats, later `Y`/`Z` were not visibly inserted and no dirty marker appeared. An additional **15 s** dwell did not resolve the recorded state. | Near-end typing latency in sublimité is **not measurable from these attempts**: no confirmed glyph onset. Do not treat the tiny sample trace as proof that the whole interaction worked. | `10-large-sublimite.mkv`, first edit then failed navigation; `10-large-r2-sublimite.mkv`; focus-checked `10-large-r3-sublimite.mkv`, later edits and wait; `10-large-resume-sublime.mkv` 8.57–11.7. |
| Also open and navigate the 64 MiB dense-short fixture | Initially slow/blank; this contributed to the delayed UTF-8 launch. | Initial content and a first-line `Z` appeared; the short observation did not establish successful end navigation. | Supplemental coverage, not a completed large-file performance benchmark. | `dense64-{sublime,sublimite}.mkv`; associated snapshots/logs. |

Large-file video onset measurements (`logs/large-typing-latency.json`, thirty extracted frames per probe): sublimité's first-line `Z` appears at **9.350 s**, **1.2 60 Hz intervals / 19 ms** after command start; loaded Sublime's first-line `Z` at **10.333 s**, **2.0 intervals / 33 ms**; its near-end `Y` at **8.633 s**, **3.9 intervals / 65 ms**. There is no corresponding successful sublimité near-end glyph measurement. In `logs/large-metrics.json`, the guarded sublimité Ctrl+End reaches the final-line-number crop after **84.6 intervals / 1,410 ms**; loaded Sublime's end viewport matches after **11.0 intervals / 183 ms**. As with the small-file probe, these are video sampling/command-start measurements, not precise hardware ingress latencies.

Focus-checked large-file trace, copied verbatim from `traces/10-large-r3-sublimite.txt`:

```text
G1 T4-T0     n=3 p50=21.126 ms p99=1860.846 ms
G3 T5-T0     n=2 p50=3.020 ms p99=1861.267 ms
T6-T0        n=2 p50=26.109 ms p99=1875.712 ms
```

The previous navigation-first attempt reported `G1 n=3 p50=21.649 ms p99=5249.999 ms` and `G3 n=2 p50=7.108 ms p99=5255.719 ms`. These are literal tracedump percentiles over very few completed samples, not statistically meaningful workload percentiles. Focus was not independently logged in that earlier repeat; the guarded repeat removes that ambiguity. No claim is made that queued/unpainted inputs completed.

## 11. Resize

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| `windowsize` to 1440×900, 2880×1800, 800×600, then 960×600; Page Down/Up at each | Resized and laid out text; font stayed the same physical pixel size. | Resized and redrew the text/minimap; font also stayed unchanged. | No persistent garbage inside the active editor window was found in the sampled settled views. Exposed pixels from older windows outside the active window are not attributed to the resized editor. Long prose wrap was not comprehensively tested by this C-file probe. | `11-resize-sublime.mkv` 4.1–14.5; `11-resize-sublimite.mkv` 4.0–15.4. Dimensions are xdotool requests and named resulting screenshots. |

## 12. SIGKILL and relaunch

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Type `UNSAVED_RECOVERY_MARKER_`, wait after the visible edit, SIGKILL the tested editor process, relaunch the same file | Relaunched file showed `disk baseline`; marker absent in the inspected window. | Relaunched file also showed `disk baseline`; marker absent. | **Neither recovered the tested marker in this short-delay crash test.** This is not a claim that Sublime never supports session recovery. The normal Sublime test process had many prior fixture windows, including large files, which affected restoration. The owner's original Sublime process was not killed. | `12-hotexit-sublime.mkv` edit 7.67, kill 12.62, relaunch view 22.70; `12-hotexit-sublimite.mkv` edit 7.89, kill 13.00, relaunch view 20.79. Event times are JSON-to-video offsets; disk reads remained `disk baseline\n`. |
| Repeat with a longer pre-kill dwell | Longer-dwell comparison initially could not launch within the harness's window-discovery deadline while the restored large session was busy. Retained as an incomplete attempt. | Longer-dwell repeat also relaunched without the marker. | No automatic replay was observed for sublimité. A journal file being written is not equivalent to restored user-visible edits. | `12-hotexit-dwell-sublimite.mkv`; failed `12-hotexit-dwell-sublime.mkv` and logs. See worker report for the launch timeout. |

## 13. Minimap and orange flashing

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| View the same C file; page, jump end/home, edit | Minimap resembles miniature syntax-colored text and document structure. | Minimap consists of large grey horizontal rectangular bands; changes shape with navigation and edits. | Poor document recognizability even though click/drag navigation works. | `13-minimap-sublime.mkv` 4–15.5; `13-minimap-sublimite.mkv` 3.8–14.5; also task 4. |
| Scan requested orange colors | Clean typing minimap had no corrected-color flash detections in its own minimap ROI. | Requested `#bf853d`/tol40 and `#ff8800`/tol60 scans miss the visible flashes. Pixel sampling finds rendered browns such as **RGB(107,80,48)** and **(120,95,64)**. | A zero from the supplied color settings is a **false negative**, not evidence that Tobias imagined the flash. | Requested-color outputs and `02-typing-r2-sublime.flash-6b5030.txt`; on-screen RGB measured from lossless screenshots of the visible overlay, then validated against video frames. |
| Scan the rendered color `#6b5030`, tolerance 24, minimum jump 200 pixels | **0 flashes** in the clean typing minimap ROI `(707,60,80,520)`. | **88 flashes** in `02-typing-r2`; **84** in the original C-file typing run; **81** in the genuinely paced ten-character-per-second C-file run. Repeated appearances last only a frame or a few frames. | The flashes accompany edits, particularly individual typed characters; they are not random idle activity. | `02-typing-r2-sublimite.mkv`: isolated Z appears/flash at **4.917 s**, vanishes **4.933 s**; first paragraph A/flash **6.633–6.650 s**; burst flash **12.583–12.600 s**. `logs/*.flash-6b5030.txt` contains every measured event. |

Example literal corrected scan output:

```text
frames=950 fps=60 colour=#6b5030 tol=24 min_px=200
t=   4.917s frame=   295 appear       0 ->   37440
t=   4.933s frame=   296 vanish   37440 ->       0
t=   6.633s frame=   398 appear       0 ->   37440
t=   6.650s frame=   399 vanish   37440 ->       0
flashes (appear then vanish within 30 frames): 88
```

The displayed events are excerpts; the total is from the complete scan. The detector defines a flash as an appearance followed by disappearance within **30 resampled frames**, not a physiological measure. A read-only source check supports the observed explanation: `src/editor/paint.c` renders an `!` when `map.stale` is set and supplies `0xbf853d` as the stale minimap color. The evidence is consistent with briefly showing the stale-map overlay on edits, then replacing it. This is an explanation grounded in the video and implementation, not proof of a particular race.

Per-video detector totals (counts computed by `flashscan.py`; complete events in the matching `logs/<stem>.flash-<color>.txt`):

| Video under `build/astra/video/` | #bf853d / tol40 | #ff8800 / tol60 | #6b5030 / tol24 |
|---|---:|---:|---:|
| `01-fresh-sublimite.mkv` | 0 | 0 | 1 |
| `01-open-r2-sublimite.mkv` | 0 | 0 | 0 |
| `01-open-r3-sublimite.mkv` | 0 | 0 | 0 |
| `01-open-sublimite.mkv` | 0 | 0 | 1 |
| `02-paced-sublimite.mkv` | 0 | 0 | 81 |
| `02-typing-r2-sublimite.mkv` | 1 | 0 | 88 |
| `02-typing-sublimite.mkv` | 0 | 0 | 84 |
| `03-nav-sublimite.mkv` | 0 | 0 | 0 |
| `03-selection-sublimite.mkv` | 0 | 0 | 0 |
| `04-mouse-sublimite.mkv` | 0 | 0 | 0 |
| `05-edit-r2-sublimite.mkv` | 0 | 0 | 2 |
| `05-edit-sublimite.mkv` | 0 | 0 | 74 |
| `06-clipboard-sublimite.mkv` | 0 | 0 | 0 |
| `06-cross-sublimite.mkv` | 0 | 0 | 14 |
| `07-find-sublimite.mkv` | 0 | 0 | 5 |
| `08-save-sublimite.mkv` | 0 | 0 | 7 |
| `09-tabs-sublimite.mkv` | 0 | 0 | 2 |
| `10-large-r2-sublimite.mkv` | 0 | 0 | 0 |
| `10-large-r3-sublimite.mkv` | 0 | 0 | 0 |
| `10-large-sublimite.mkv` | 0 | 0 | 0 |
| `11-resize-sublimite.mkv` | 0 | 0 | 0 |
| `12-hotexit-dwell-sublimite.mkv` | 0 | 0 | 19 |
| `12-hotexit-sublimite.mkv` | 0 | 0 | 8 |
| `13-minimap-sublimite.mkv` | 2 | 0 | 9 |
| `14-idle-sublimite.mkv` | 0 | 0 | 0 |
| `dense64-sublimite.mkv` | 0 | 0 | 2 |
| `utf8-sublimite.mkv` | 0 | 0 | 2 |

All sublimité per-task recordings, including failed attempts and repeats, are passed through `tools/flashscan.py`. The routine ROI is `(896,0,64,600)`, the minimap of the standard window. Counts in the resize recording are **not a complete moving-minimap scan**: that ROI no longer follows the minimap at other widths, and can expose older background windows. Startup/close/background transitions are not automatically interpreted as editor flashes. A supplemental **moving-minimap** scan follows the four recorded width-change commands: `video/11-resize-minimap-track.mkv` is a lossless derived strip, with crop expressions/times in `logs/resize-tracking.json`. Its requested-color totals are **0/0**, corrected-color total **1**, measured by `flashscan.py`; the short event is **12.733–12.767 s**, following the Page Down/Up sequence at the small window size. A longer amber interval **2.967–3.683 s** is logged but exceeds the detector’s half-second flash definition. Thus the fixed-ROI zero for resize is not a complete negative result.

## 14. Idle

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Leave focused and untouched for a measured minute | Recorded unchanged text; no visible orange minimap anomaly established. | No visible non-caret changes during **60.002 s**, measured between logged idle markers. Detected **20** caret-region transitions, approximately half-second apart, then blinking stopped; text/caret position did not drift. | Stable idle appearance; the edit-triggered flash is not reproduced by simply waiting. Identical-pixel repaints cannot be ruled out by video. | `14-idle-sublimite.mkv` **3.665–63.667 s**; `14-idle-sublime.mkv` **4.055–64.058 s**, **60.004 s** by marker subtraction. `logs/idle-metrics.json`: grayscale difference >30, >20 changed pixels, caret neighborhood excluded; zero non-caret events. Flash outputs supply the orange check. |

## Additional UTF-8 edge case

| Action | Sublime did | sublimité did | Divergence | Evidence |
|---|---|---|---|---|
| Open accents, Greek, CJK and emoji; move/delete/undo; inject Unicode | Rendered the accented Latin, Greek, CJK and emoji fixture readably. Unicode insertion also appears in the text. | Existing non-ASCII characters render as `?`, including ordinary accented Latin letters. Injected Unicode also is not usefully rendered. | A daily prose-editing blocker on this raster build. The test cannot prove byte preservation from the display, especially since save/copy are unavailable. | `utf8-sublimite.mkv`; delayed initial `utf8-sublime.mkv` is a failed launch; completed comparison is `utf8-r2-sublime.mkv`, named text-result frames. Fixture: `build/astra/files/utf8.txt`. |

## Missing features Tobias would hit in daily use

Ranked by practical impact in this drive:

1. **Save that changes the file on disk.** Visible edits plus an inert Ctrl+S are a fundamental workflow failure.
2. **Working clipboard in both directions**, including cut and alternate paste methods. Stale clipboard data can masquerade as a successful paste-back.
3. **Mouse caret placement, selection, word selection and wheel scrolling.** Minimap navigation is not a substitute.
4. **Find and replace UI.** The expected search workflow currently inserts the search query into the document.
5. **Readable Unicode and configurable/readable presentation.** This build shows common accented text as question marks; default glyphs are physically small at the supplied resolution. No working zoom/preferences workflow was established by this drive.
6. **Expected redo binding.** Selection extension passed; Ctrl+Shift+Z is a discovered redo workaround, but Ctrl+Y fails.
7. **Usable large-file navigation and editing after opening.** Painting an initial viewport does not mean the file is interactively usable.
8. **A recognizable, visually quiet minimap.** Editing repeatedly flashes an amber overlay; the stable representation is coarse blocks.
9. **Scalable CLI handoff and user-visible crash recovery.** Additional opens were rejected in the stress loop; unsaved edits were not restored in the tested crash/relaunch path.

## Edge cases and defects

- **Redo binding mismatch:** Ctrl+L, Backspace, Ctrl+Z, Ctrl+Y leaves the line restored; Ctrl+Shift+Z redoes deletion.
- **Search edits the buffer:** Ctrl+F, type `alpha`, Return; sublimité inserts a new `alpha` line.
- **Save is inert:** insert `EDITED_`, Ctrl+S, inspect the file with `cat`; disk remains unchanged.
- **Copy/cut do not claim CLIPBOARD:** seed a sentinel, select text, Ctrl+C/Ctrl+X, read `xclip -o`; sentinel remains and cut does not remove text.
- **Mouse text controls are inert:** click/drag/double-click text or scroll the wheel after opening the C fixture; viewport/caret stay put. Minimap click/drag is the tested exception.
- **Edit-triggered orange pulse:** type ordinary ASCII while watching the minimap; corrected-color scan sees brief appear/vanish pairs coincident with glyph onset.
- **Color detector false negative:** run the supplied RGB/tolerance on that same recording; it reports zero despite the darker rendered overlay.
- **CLI handoff rejects further requests:** run the logged unique-file loop; `tab-064` through `tab-099` return status 2 / handoff error 8.
- **Large-file interaction stalls:** open `log_1g.txt`, Ctrl+End, Page Up repeatedly, then try Home and typing; the focus-checked run stays in the near-end viewport without confirmed edits.
- **UTF-8 display is not readable:** open the provided accented/CJK/emoji fixture; non-ASCII characters display as question marks on the raster path.
- **Crash replay not visible:** edit the hotexit fixture, SIGKILL, relaunch; the tested marker is absent. Sublime also failed the short-delay observation, so this row is not a claimed unique regression against Sublime.

## Latency and jank by feel plus numbers

Small ASCII insertion generally responds, and the final paragraph/burst text is correct. It does not compensate for the absent editing features. The minimap flicker makes steady typing look unstable even when glyph order is right. Large-file navigation is qualitatively different: long waits followed by a viewport that stops responding usefully. Sublime itself is slow to load the very large fixtures and temporarily blocks new CLI windows, but the later loaded-buffer comparison successfully navigates and edits.

The clean small-file typing trace is:

```text
G1 T4-T0     n=93 p50=1.201 ms p99=44.914 ms
G3 T5-T0     n=92 p50=1.864 ms p99=16.602 ms
T6-T0        n=92 p50=17.304 ms p99=34.287 ms
```

Source: `traces/02-typing-r2-sublimite.txt`, literal tracedump output. The original C-file typing run reported `G1 n=88 p50=1.436 ms p99=108.245 ms`, `G3 n=87 p50=1.942 ms p99=111.117 ms`, and `T6 n=87 p50=19.868 ms p99=134.899 ms`. These internal stages do not include every component of external input-to-captured-glyph time and must not be substituted for it. The video probe found approximately three 60 Hz intervals to sublimité's isolated glyph and two to Sublime's, with the measurement limitations above. The large-file trace has completed-frame tails measured in seconds and too few samples for a general percentile claim.

No exhaustive performance distribution, memory benchmark, backend comparison, comprehensive wrap test, successful sublimité near-end edit latency, or completed long-dwell Sublime crash-recovery comparison was established. These limits do not weaken the directly verified save, clipboard, find, Unicode and mouse blockers.

VERDICT usable instead of Sublime for Tobias's daily edits: no; blockers: save, clipboard, text-area mouse and wheel, find/replace, redo-binding mismatch, Unicode rendering, tiny default presentation, orange edit flashing and blocky minimap, stalled large-file interaction, CLI handoff failures, unverified automatic crash recovery.
