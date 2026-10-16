# Astra vs Sublime Text: comparison drive 6

Astra · edit-w34.4 / P5.1b. Baseline: drive 5. Core tasks are filed in the prescribed order before the verdict. No appendix task has run.

## Conditions

All editor input and capture use DISPLAY=:98. xdpyinfo confirms 2880×1800 pixels and 243×243 DPI (M)[AC], at the setup stamp below. Xvfb owner is PID 917702 (M, process listing); argv is recorded in `build/astra6/logs/xvfb-owner.txt`. The sandbox cannot connect to X; authorized escalated commands can. No operation targets :0 or :99.

Pinned build under test: `build/astra6/sublimite`, code commit 9b5ee41 per BUILD. BUILD names edit-52o ba2beeb, edit-qct f09cfa3, edit-dz0 5ebf309, edit-n95 87b0f5f and edit-f3i 9b5ee41. edit-c26 is a test pin; behaviour is established below, not inferred from BUILD. BUILD also records a subsequent successful full make check on docs commit 8f8a21e; that is coordinator evidence, not a drive test.

Start checks (M)[AC], BAT0=Not charging, AC/online=1; load and exact stat/digest follow:

```text
START
2026-10-16T21:00:57+11:00
Not charging
1
1.17 1.95 1.87 5/1748 159
2026-10-16 07:44:42.898991813 +1100 10292616
7a9f09f00f261d297257302212f2ab9536df194c4e7a0d9fbceeb32f859b92e3  build/astra6/sublimite

BEFORE BUILD
START
2026-10-16T21:00:57+11:00
Not charging
1
1.17 1.95 1.87 5/1748 159
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite

Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  319G  137G  71% /home/tobias/Projects/sublimite
```

Tracedump: `build/astra6/tracedump`. Sublime help reports build 4200 (M); User preferences specify font_size 11 and no colour_scheme override. Default scheme confirmation will be recorded with the comparator. Real sublimité data is never launched or modified. Each run has a private data home; the before and after builds never share a profile.

Harness: drive-5 Python/shell scripts copied with only build/astra5 → build/astra6 path substitution. Full per-file unified diff is in the worker log and `logs/harness.diff`. Core-only instrumentation is separate; inherited tasks are not edited. Fixtures copied from drive 5; hashes in `logs/fixtures-sha256.txt`. No inherited task has run yet, so fixture comparisons against its task-specific drive-3 baseline are pending.

All evidence paths below are relative to `build/astra6/` unless absolute or starting with build/. Every measurement in a row inherits its explicitly cited BAT0/status, AC/online and task-start load1. Task numbers, filenames, key counts, coordinates and prescribed delays are input parameters/identifiers. Recorded intervals are resampled at 60 Hz: one interval = 1000/60 = 16.7 ms (P). Command envelopes measure injection, not physical key-to-photon latency. Timing at load1 > 8 is provisional, never a regression or blocker by itself. Structural results remain valid at that load. Cache is not flushed.

Colour emoji artwork remains waived (edit-r8e); find in files is judged as a daily feature. No budgets are inferred from decision-document measurements.

Core-end binary verification (M, exact stamps in logs/binary.txt and logs/binary-astra5.txt):
```text
2026-10-16T21:27:03+11:00
Not charging
1
0.84 1.83 2.60 4/1640 5
2026-10-16 07:44:42.898991813 +1100 10292616
7a9f09f00f261d297257302212f2ab9536df194c4e7a0d9fbceeb32f859b92e3  build/astra6/sublimite
2026-10-16T21:27:03+11:00
Not charging
1
0.84 1.83 2.60 4/1640 5
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite
```
Both stat tuples and full digests equal the start. Behaviour observed: f3i/c26 dense relaunch fixed, f3i post-save page-scale checkpointing, n95 search/navigation/read-only/cancel, qct closed-tab lazy restore and preservation. 52o clean-directory absence observed, sweeping a dead temp not proven. dz0 zero-scan unwrapped path not independently established by the default wrapped GUI cases.

## 37. Dense hot-exit relaunch — both pinned builds

| Action | Sublime did | sublimité did | Drive-5 status and bead | Evidence |
|---|---|---|---|---|
| Before: astra5 dirty close / no-file relaunch | Not applicable: sublimité session format | xy visible and dirty; close returns 0; relaunch returns 1, error -103, no window | STILL PRESENT in before build — edit-c26 / edit-f3i; matches drive-5 exit 0 then 1 at load1 0.36/0.49 (M)[AC] | Before recording/audit below |
| After: astra6 dirty close / no-file relaunch | Not applicable | Window, dirty dense_short tab, xy on first line; close returns 0; no error -103 | FIXED — edit-c26 / edit-f3i | restored-xy still and after recording/audit below |
| End navigation / edit restored buffer / reopen again | Not applicable | Ctrl+End shows gutter 26845451, matching drive 5; Ctrl+Home then z yields zxy, still dirty after another relaunch; every close returns 0 | FIXED restore; NEW second-relaunch coverage — edit-f3i / edit-c26 | restored-end, dirty-z, restored-z stills; after audit |
| Checkpoint observation | Not applicable | Both builds: before x 374 B, first changed sample 4096 B (stat); observed after 1.017 s before / 1.015 s after from poll start. This is a polling bound, not checkpoint duration. No file-size step observed | STILL PRESENT small checkpoint — edit-28u / edit-f3i. Drive 5: 4233 → 8192 → 12288 B; largest step 4096 B over 32 samples (M)[AC] load1 0.36. Different capture window, not interval equivalence | journal_sample events and listings; task 40 supplies extended sampling |
| Corpus preservation / notices | Not applicable | Both before/after comparisons match size, mtime and SHA256; never saved. No status notice visible on successful restores; stderr notes unsaved journal and raster fallback | STILL PRESENT preservation; FIXED failed restore — edit-c26 / edit-f3i | corpus_before/after; below |

Corpus stat (M, each run stamp below): 67108864 B = 2^26 (P); mtime_ns 1792069514945642686; SHA256 `05faa43b31b2299237367624a326302692fc0658527b925c9637ac6c9c182669`. Poll change is size/mtime change after x, sampled at the prescribed second; it does not assume a periodic idle checkpoint. The first close leaves a referencing journal and the before failure reproduces despite its different sampled starting size.

Startup measurements (M; first title ink >40 pixels with RGB max>140 in ROI 0,0,400,40; fps=60 decode; launch-to-mapped from monotonic events):

| Build/run | load1 / power | launch video time → first painted time | intervals / ms (P: intervals × 1000/60) | mapped ms |
|---|---|---|---|---|
| 37-hotexit-dense64-astra5-sublimite, successful launch 1 | 1.87, BAT0=Not charging, AC=1 | 18.454608s → 18.766667s | 18.72 / 312.1 ms | 227.368 |
| 37-hotexit-dense64-astra6-sublimite, successful launch 1 | 2.27, BAT0=Not charging, AC=1 | 13.553794s → 13.716667s | 9.77 / 162.9 ms | 55.675 |
| 37-hotexit-dense64-astra6-sublimite, successful launch 2 | 2.49, BAT0=Not charging, AC=1 | 16.309859s → 16.600000s | 17.41 / 290.1 ms | 231.033 |
| 37-hotexit-dense64-astra6-sublimite, successful launch 3 | 2.49, BAT0=Not charging, AC=1 | 18.967422s → 19.216667s | 14.95 / 249.2 ms | 172.824 |

Evidence `37-hotexit-dense64-astra5-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=0.87. Video `video/37-hotexit-dense64-astra5-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/37-hotexit-dense64-astra5-sublimite.json`.

Still/video positions (M, capture-command starts): first-text 18.929s; dirty-xy 20.661s.

Process return codes in launch order (M, wait/poll): 0, 1.

`traces/37-hotexit-dense64-astra5-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56601794889333 elapsed_ms=0.000000
  display_connect      ns=56601832841683 elapsed_ms=37.952350
  egl_probe_start      ns=56602027681297 elapsed_ms=232.791964
  egl_probe_end        ns=56602031081431 elapsed_ms=236.192098
  raster_fallback      ns=56601844047585 elapsed_ms=49.158252
  font_atlas_ready     ns=56601829956049 elapsed_ms=35.066716
  file_prefix_read     ns=56602039951294 elapsed_ms=245.061961
  first_layout         ns=56602030592010 elapsed_ms=235.702677
  first_submit         ns=56602048443396 elapsed_ms=253.554063
  first_present        ns=56602052177399 elapsed_ms=257.288066
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=3 p50=1.159 ms p99=49.157 ms
G3 T5-T0     n=2 p50=0.777 ms p99=1.215 ms
T6-T0        n=2 p50=12.090 ms p99=16.340 ms
battery: Not charging
```

`logs/37-hotexit-dense64-astra5-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2197 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/37-hotexit-dense64-astra5-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2197 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/37-hotexit-dense64-astra5-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2197 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `37-hotexit-dense64-astra6-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=2.25. Video `video/37-hotexit-dense64-astra6-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/37-hotexit-dense64-astra6-sublimite.json`.

Still/video positions (M, capture-command starts): first-text 13.779s; dirty-xy 15.423s; restored-xy 16.805s; restored-end 17.245s; dirty-z 17.959s; restored-z 19.420s.

Process return codes in launch order (M, wait/poll): 0, 0, 0.

`traces/37-hotexit-dense64-astra6-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56633775858709 elapsed_ms=0.000000
  display_connect      ns=56633812839342 elapsed_ms=36.980633
  egl_probe_start      ns=56633844403143 elapsed_ms=68.544434
  egl_probe_end        ns=56633844635800 elapsed_ms=68.777091
  raster_fallback      ns=56633817527262 elapsed_ms=41.668553
  font_atlas_ready     ns=56633809837038 elapsed_ms=33.978329
  file_prefix_read     ns=56633852238849 elapsed_ms=76.380140
  first_layout         ns=56633844500703 elapsed_ms=68.641994
  first_submit         ns=56633866793920 elapsed_ms=90.935211
  first_present        ns=56633870260165 elapsed_ms=94.401456
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=3 p50=1.761 ms p99=41.668 ms
G3 T5-T0     n=2 p50=1.471 ms p99=1.866 ms
T6-T0        n=2 p50=17.130 ms p99=24.080 ms
battery: Not charging
```

`traces/37-hotexit-dense64-astra6-sublimite-run2.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56636531741557 elapsed_ms=0.000000
  display_connect      ns=56636737495624 elapsed_ms=205.754067
  egl_probe_start      ns=56636780134825 elapsed_ms=248.393268
  egl_probe_end        ns=56636780427857 elapsed_ms=248.686300
  raster_fallback      ns=56636744498803 elapsed_ms=212.757246
  font_atlas_ready     ns=56636580911009 elapsed_ms=49.169452
  file_prefix_read     ns=56636667386158 elapsed_ms=135.644601
  first_layout         ns=56636669139067 elapsed_ms=137.397510
  first_submit         ns=56636783709618 elapsed_ms=251.968061
  first_present        ns=56636784293590 elapsed_ms=252.552033
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=1.512 ms p99=212.756 ms
G3 T5-T0     n=4 p50=1.579 ms p99=3.602 ms
T6-T0        n=4 p50=13.427 ms p99=28.672 ms
battery: Not charging
```

`traces/37-hotexit-dense64-astra6-sublimite-run3.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56639186822057 elapsed_ms=0.000000
  display_connect      ns=56639342339373 elapsed_ms=155.517316
  egl_probe_start      ns=56639385794901 elapsed_ms=198.972844
  egl_probe_end        ns=56639389416793 elapsed_ms=202.594736
  raster_fallback      ns=56639346038745 elapsed_ms=159.216688
  font_atlas_ready     ns=56639215728341 elapsed_ms=28.906284
  file_prefix_read     ns=56639288718346 elapsed_ms=101.896289
  first_layout         ns=56639289962143 elapsed_ms=103.140086
  first_submit         ns=56639393927692 elapsed_ms=207.105635
  first_present        ns=56639395791375 elapsed_ms=208.969318
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=2 p50=9.529 ms p99=159.216 ms
G3 T5-T0     n=1 p50=13.462 ms p99=13.462 ms
T6-T0        n=1 p50=24.257 ms p99=24.257 ms
battery: Not charging
```

`logs/37-hotexit-dense64-astra6-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2930 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/37-hotexit-dense64-astra6-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2930 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/37-hotexit-dense64-astra6-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=2930 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

All prescribed orange scans report no events; no minimap pulse observed. Captures contain idle lead/tail time from fixture hashing; only named action windows are used. ffmpeg dropped some input frames, so reported onset precision is limited to the resampled recording. No whole-buffer clipboard read was used.

## 38. Find in files — edit-n95

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| Ctrl+Shift+F / defaults / Tab | Find/Where/Replace panel; Where says Open files and folders | Find/Where/hints panel; Where defaults to active file directory a; Tab selects Where and Tab returns to Find. Hints: Enter: find in files; Tab: field; Esc: close | CHANGED from drive-5 unsupported shortcut; NEW feature coverage — edit-n95 | panel-default, panel-filled stills; videos/audits below |
| Query NEEDLE_ALPHA | 24 matches across 20 files | Same 24 matches across 20 files, 22 matching lines; multiple hits share one line; highlighted | NEW passing — edit-n95 | logs/38-results-{sublime,sublimite}.txt; fixture arithmetic: 20 line-three hits + 2×2 line-five hits = 24, 20+2=22 lines (P) |
| Result formatting | Five-column line number, original tab, home abbreviated as ~, Searching 23 files header, surrounding context lines | Same hit-line bytes and column width; absolute home path; blank separators and same summary. No Searching header or surrounding context | NEW documented differences — edit-n95, findings only | Sorted per-file-block diff logs/38-results-sorted.diff. Separate normalized hit-line comparison expands ~ and excludes context/header; all 22 matching lines equal (M, comparison script) |
| Read-only mutations / save / replace | Initial result copy succeeded; later typing/navigation probes did not update the sampled UI and clipboard timed out. Not certified as read-only behaviour | q, BackSpace, Delete, Enter at summary, paste, selected cut, undo/redo, Ctrl+S all leave exact result bytes; find-panel replace-all also refuses | NEW passing candidate; comparator limitation — edit-n95 | logs/38-readonly.txt and 38-replace-refused.txt equal initial copy byte for byte; command JSON |
| Enter f07 line-three hit / byte position | Later navigation did not repaint; incomplete comparator | f07 opens at line 3 with caret on N after tab; Shift+Home/copy yields exactly one tab. Thus byte column 2 (P: one leading byte + one-based origin); display begins after tab width | NEW passing — edit-n95 | f07-open; prefix clipboard command in JSON; 38-navigation.png |
| F4 ×3 / Shift+F4 | Incomplete in restored comparator session | f08 line 3 → f09 line 3 → f10 line 3 → f09 line 3, in copied-result order | NEW passing — edit-n95 | nav-0…nav-3, 38-navigation.png |
| Wrap at ends | Incomplete comparator | F4 from final f20 hit opens f01 line 3; Shift+F4 returns f20 line 3 | NEW passing — edit-n95 | wrap-next / wrap-previous stills |
| Double-click hit / Enter header | Incomplete comparator | Double-click opens f01 line 3; header Enter opens f01 line 1 | NEW passing — edit-n95 | double-click-valid / header-enter. Earlier double-click-open clicked a shifted tab and is excluded |
| Escape / empty query / reopen panel | Initial panel works; later Escape outcome not certified | Escape closes panel; empty Return keeps panel open with empty query; reopening preserves Where and query state | NEW — edit-n95 | escape-closed / empty-query / panel-after-empty / panel-retained |
| Case toggle | Not repeated separately | lowercase query with case off: 24; Alt+C case on: 0 | NEW passing — edit-n95 | logs/38-lower-{default,case}.txt |
| Whole word / regex | Not repeated separately | whole-word NEEDLE: 0; regex NEEDLE_[A-Z]+: 24 | NEW passing — edit-n95 | logs/38-word.txt, 38-regex.txt |
| Where filters | Not repeated separately | *f01*: 1 match in 1 file; -*/b/*: all 24/20 remain | NEW grammar observation, not wrong filtering — edit-n95 | logs/38-filter{1,2}.txt. fif.h: slash globs anchor to root-relative paths; b/f11.txt lacks the leading component required by */b/*, so that exclusion matches none |
| lowercase beta / missing directory | Not repeated separately | beta gives 3 matches across 3 files; missing root: 0 matches, Search failed: the Where folder cannot be read | NEW passing — edit-n95 | logs/38-beta.txt / 38-missing.txt, missing still |
| Escape during /usr/include search | Not applicable to candidate cancellation probe | Delayed Escape arrives after cap: 4096 matches across 15 files, Search truncated: result limits reached. Immediate Escape repeat: 0 matches across 0 files, Search cancelled. Ctrl+Shift+F immediate repeat reopens panel; fresh untitled Q works | NEW passing early cancellation; prescribed delayed case completed before cancellation — edit-n95 | logs/38-cancel.txt / 38-cancel-early.txt, cancel-early / cancel-reopen / usable stills. Counts are copied result text (M); actual command envelopes in JSON |
| Restart / tree preservation | Search readback obtained; normal window-close request sent, client exit 0; process-session shutdown not established | Q untitled survives; visible tab strip after restart lacks Find Results; candidate closes all return 0. Search fixture contents unchanged; aggregate-hash pathname normalization documented below | NEW — edit-n95; results nonpersistence is documented | logs/38-tree-before.txt / 38-tree-after.txt; after-hotexit-no-results still; no fixture saves |

All counts above are (M) from clipboard/stills under the following task stamps unless labelled fixture arithmetic (P). File-tree pipeline is `find <tree> -type f | sort | xargs sha256sum | sha256sum`. The generator used an absolute tree path (digest `aedb87dc2c290f3d8d99bd33ef4d927ce9c12f861d517587c79026af235a0f15`); the post-task shell used a relative path (digest `3b87e0d50771b21b6a8d91ec1a2ba057c3beeb8edcc8241255f3e932a6672eb8`). Final checks reproduce both digests with their respective path spellings and every file equals its generated bytes; see logs/38-tree-hash-normalization.txt. The raw aggregate digests differ because sha256sum includes filenames, not because search changed file contents. Generator is `fif6.py:fixture`: f01–f10 in a, f11–f20 in b, each five specified lines; g01/g02 in a and g03 in b contain lowercase beta. No corpus used for this task.

Sublime uses build 4200, User font_size 11 and installed Default Mariana (`logs/sublime-default-settings.txt`); same default zoom, comparator window enlarged to candidate geometry. The old real-profile session restored many prior windows and later stopped repainting these navigation actions despite focus checks; no candidate regression is inferred from that comparator limitation. Its successful search readback remains usable evidence. Original `38-fif-sublimite` ended when nonpersistent harness stdin closed; exit status unavailable, not a successful lifecycle claim. Candidate sequence was repeated in fresh `38-fif-r2` with explicit waits/return codes. The first comparator driver was interrupted while idle after its initial panel; `38-fif-compare` is the completed search recording. These attempts remain on disk.

Documented limits, not defects: no replace-in-files, folder sidebar, ~ expansion, surrounding context or Searching header; derived results do not survive restart. No candidate crash, changed source file, wrong hit position or editable result text was observed.

Evidence `38-fif-r2-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.03. Video `video/38-fif-r2-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/38-fif-r2-sublimite.json`.

Still/video positions (M, capture-command starts): panel-default 1.136s; results 2.635s; f07-result-caret 15.491s; f07-open 15.964s; back-results 16.375s; nav-0 16.913s; nav-1 17.398s; nav-2 17.880s; nav-3 18.376s; wrap-next 45.158s; wrap-previous 45.620s; double-click-open 46.383s; panel-retained 46.858s; escape-closed 47.380s; empty-query 48.481s; panel-after-empty 48.998s; missing 75.395s; double-click-valid 91.370s; header-enter 92.239s; cancelled 98.450s; usable-after-cancel 99.122s; after-hotexit-no-results 100.229s.

Process return codes in launch order (M, wait/poll): 0, 0.

`traces/38-fif-r2-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56822660584359 elapsed_ms=0.000000
  display_connect      ns=56822696637644 elapsed_ms=36.053285
  egl_probe_start      ns=56822734494349 elapsed_ms=73.909990
  egl_probe_end        ns=56822737407922 elapsed_ms=76.823563
  raster_fallback      ns=56822705162208 elapsed_ms=44.577849
  font_atlas_ready     ns=56822693685483 elapsed_ms=33.101124
  file_prefix_read     ns=56822737574229 elapsed_ms=76.989870
  first_layout         ns=56822737278873 elapsed_ms=76.694514
  first_submit         ns=56822748037692 elapsed_ms=87.453333
  first_present        ns=56822751737768 elapsed_ms=91.153409
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=50 p50=2.085 ms p99=44.577 ms
G3 T5-T0     n=49 p50=2.622 ms p99=24.065 ms
T6-T0        n=49 p50=21.353 ms p99=36.719 ms
battery: Not charging
```

`traces/38-fif-r2-sublimite-run2.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56921999368271 elapsed_ms=0.000000
  display_connect      ns=56922048447428 elapsed_ms=49.079157
  egl_probe_start      ns=56922081497212 elapsed_ms=82.128941
  egl_probe_end        ns=56922081775998 elapsed_ms=82.407727
  raster_fallback      ns=56922056895647 elapsed_ms=57.527376
  font_atlas_ready     ns=56922036812413 elapsed_ms=37.444142
  file_prefix_read     ns=56922040647943 elapsed_ms=41.279672
  first_layout         ns=56922040806255 elapsed_ms=41.437984
  first_submit         ns=56922084832335 elapsed_ms=85.464064
  first_present        ns=56922085457522 elapsed_ms=86.089251
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=57.526 ms p99=57.526 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

`logs/38-fif-r2-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=6067 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/38-fif-r2-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=6067 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/38-fif-r2-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=6067 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `38-cancel-early-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=2.72. Video `video/38-cancel-early-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/38-cancel-early-sublimite.json`.

Still/video positions (M, capture-command starts): cancel-early 2.721s; cancel-reopen 3.504s; usable 4.731s.

Process return codes in launch order (M, wait/poll): 0.

`traces/38-cancel-early-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=56946064700369 elapsed_ms=0.000000
  display_connect      ns=56946111849974 elapsed_ms=47.149605
  egl_probe_start      ns=56946158040371 elapsed_ms=93.340002
  egl_probe_end        ns=56946158344691 elapsed_ms=93.644322
  raster_fallback      ns=56946120522290 elapsed_ms=55.821921
  font_atlas_ready     ns=56946109408338 elapsed_ms=44.707969
  file_prefix_read     ns=56946155553922 elapsed_ms=90.853553
  first_layout         ns=56946155333809 elapsed_ms=90.633440
  first_submit         ns=56946159048896 elapsed_ms=94.348527
  first_present        ns=56946159924961 elapsed_ms=95.224592
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=1.965 ms p99=55.821 ms
G3 T5-T0     n=4 p50=1.996 ms p99=2.053 ms
T6-T0        n=4 p50=15.336 ms p99=21.281 ms
battery: Not charging
```

`logs/38-cancel-early-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=352 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/38-cancel-early-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=352 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/38-cancel-early-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=352 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `38-fif-compare-sublime`: (M)[AC], BAT0=Not charging, AC/online=1, load1=5.76. Video `video/38-fif-compare-sublime.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/38-fif-compare-sublime.json`.

Still/video positions (M, capture-command starts): launch 30.934s; panel-ready 32.593s; panel-filled 63.338s; results 65.497s; enter-hit 105.693s; f4 106.862s; shift-f4 107.762s; results-return 108.657s; typed-results 109.823s; enter-refocused 144.404s; f4-refocused 145.559s; prev-refocused 146.597s; results-r3 156.302s; typed-r3 157.348s; reopen-panel 158.586s; escaped 159.416s.

Process return codes in launch order (M, wait/poll): .

Return onset `38-fif-r2-sublimite`: (M, task stamp above), command 2.113969–2.133909s; ROI [60, 48, 1100, 28]; first matching header frame 2.183333333333333; intervals 4.161883354186999. 69.4 ms (P: intervals×1000/60). Thirty cropped frames in frames/38-fif-r2-sublimite-return-sequence; foreground-template error threshold <20. Summary is below viewport: first visible summary onset was not captured in either editor; clipboard completion is not substituted for a painted timestamp.

Return onset `38-fif-compare-sublime`: (M, task stamp above), command 64.267665–64.289808s; ROI [40, 98, 700, 18]; first matching header frame None; intervals None. No qualifying matching header in the sampled window; no invented onset. Thirty cropped frames in frames/38-fif-compare-sublime-return-sequence; foreground-template error threshold <20. Summary is below viewport: first visible summary onset was not captured in either editor; clipboard completion is not substituted for a painted timestamp.

Candidate minimap scans found no events in the prescribed colours; deliberate match highlights and panel changes are visible outside that ROI.

## 39. Early-index typing — edit-dz0

All primary sequences were injected immediately after text detection, before the large index could complete. Q-probe delay from text detection was 0.028–0.073 s (M, JSON monotonic/wall event differences; individual command envelopes below), within the brief's first-second condition. The default file view visibly wraps each log line; dz0's decision routes the **unwrapped** inexact-line case to its new byte-relative path. Thus this GUI drive cannot claim the decision's zero-line_query structural property: the staged trace has no byte-scan counter and these default wrapped views do not isolate that path. No source instrumentation or build substitution was made.

| Sequence / action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| (a) Ctrl+End, Q then Q Q | Not required for this task | First capture's early still precedes the text; video later shows Q at EOF. Fresh observation-driven repeat shows QQQ at gutter 8947842, caret visible | CHANGED measurement; STILL PRESENT visible navigation/queued-input delay — edit-dz0 / edit-mmp. No wrong final text | a and a-repeat videos/metrics below |
| (b) Right, Q then Q Q | Not applicable | 2QQQ026 at start; Q after first byte, caret on following original byte; gutter 1 retained | NEW sequence, passing text — edit-dz0 | b video, after-QQQ still |
| (c) Down, Q then Q Q | Not applicable | QQQ before lat= on second wrapped row of logical line 1; caret stays visible. Root still sometimes incomplete; video body retains text; fresh repeat's settled view is complete | NEW sequence; no confirmed blank-view defect — edit-dz0 | c, c-repeat video; 39-blank-runs.json, ROI 160,160,800,1000 counts >100 bright pixels throughout after Q; still alone excluded |
| (d) PageDown, Q then Q Q | Not applicable | QQQ at logical line 16, bottom of viewport; top is second wrapped row of line 1, caret visible | NEW sequence, passing text — edit-dz0 | d video, after-QQQ still |
| (e) Ctrl+End, PageUp, Q then Q Q | Not applicable | QQQ at logical line 8947827, top of end-page view; lower visible labels through 8947841; caret visible | NEW sequence, passing text with queued-navigation delay — edit-dz0 | e video, after-QQQ still |
| Optional before build, sequence (a) | Not applicable | astra5 also shows QQQ at EOF; similar queued-navigation time to candidate repeat | STILL PRESENT default wrapped-path behaviour — edit-dz0 | a-before video/profile separate from all candidate profiles |
| File / process integrity | Not applicable | No save/copy of corpus; SHA256 matches drive-3 fixture; all explicit normal closes returned 0, no error -103 | STILL PRESENT — edit-dz0 task coverage | corpus-after and complete stderr/trace audits below |

Gutter labels are reported as displayed; the UI does not visibly mark estimate versus exact in these captures. Starting top label is 1 in every run. The final EOF number is the brief's 8947841 newlines + 1 = 8947842 (P). No index-completion wait was used. All repeated Qs are visible in the completed observations. The first (a) still is not a settled result; the first (a) timing is contaminated by a root screenshot while navigation was still pending and is excluded from regression/blocker judgement. The repeat has no root screenshot before the glyph and supplies the usable observation.

Key-to-first-Q observations (M, software command envelope → first template-matching frame, fps=60; first-Q cell compared against the visibly verified Q glyph, mean RGB error <12). Thirty cropped frames per action are in `frames/<stem>-Q-sequence`; extended decode is used where onset exceeds that extraction. The measured interval need not be an integer because injection begins between resampled frames. One interval is 16.7 ms (P: 1000/60).

| Run | BAT0 / AC / load1 | Q command envelope (video s) | first Q (video s) | intervals / ms | ROI |
|---|---|---|---|---|---|
| 39-dz0-a-sublimite | Not charging / 1 / 3.32 | 1.216136–1.236345 | 2.383333 | 70.03 / 1167.2 ms | [160, 46, 20, 28] |
| 39-dz0-a-repeat-sublimite | Not charging / 1 / 5.08 | 1.011037–1.027706 | 1.283333 | 16.34 / 272.3 ms | [160, 46, 20, 28] |
| 39-dz0-a-before-sublimite | Not charging / 1 / 1.40 | 0.971558–0.988317 | 1.250000 | 16.71 / 278.4 ms | [160, 46, 20, 28] |
| 39-dz0-b-sublimite | Not charging / 1 / 3.32 | 0.961206–0.988584 | 1.000000 | 2.33 / 38.8 ms | [180, 46, 20, 28] |
| 39-dz0-c-sublimite | Not charging / 1 / 3.37 | 0.949925–0.975086 | 0.983333 | 2.00 / 33.4 ms | [160, 86, 20, 28] |
| 39-dz0-d-sublimite | Not charging / 1 / 3.37 | 1.023250–1.057435 | 1.066667 | 2.60 / 43.4 ms | [160, 1206, 20, 28] |
| 39-dz0-e-sublimite | Not charging / 1 / 3.90 | 1.097074–1.121060 | 1.333333 | 14.18 / 236.3 ms | [160, 46, 20, 28] |

The unobstructed candidate Ctrl+End repeat is 16.34 intervals / 272.3 ms and the old-build run 16.71 / 278.4 ms (M, table derivation). They are not same-minute paired runs: task-start loads 5.08 and 1.40 differ by 3.68 (P subtraction), below the brief's difference-of-four flag but still not a controlled speed ranking. This default wrapped sequence does not demonstrate dz0's unwrapped improvement. Reference red-test figure is 910.662 ms (M)[AC], BAT0 Not charging, load1 8.66; edit-mmp reported 0.8–2.1 s (M, decision documents, different fixture conditions). No frame-count budget is inferred.

Right/Down/PageDown first-Q observations are a few intervals rather than the earlier seconds-scale symptom. End sequences visibly wait for navigation before applying queued keys; no key is missing in completed observations. The table reports these waits, not a pure mutation-path benchmark. Exact settled-frame intervals were not independently isolated from the following two Qs/caret blink; final viewport/text are established by the named stills and recordings, and no invented settled latency is supplied. No regression is claimed from the contaminated initial (a) observation.

Corpus after prescribed runs (M, task stamps above), stat/SHA256:
```text
2026-10-16 00:05:19.374668992 +1100 1073741824
a259640b05b70487cd68a641913d8f2d8023d77f9bd1a5ba63beb555ceaae875  /tmp/edit-corpus/log_1g.txt
```

Evidence `39-dz0-a-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=3.32. Video `video/39-dz0-a-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-a-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.895s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-a-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57338645398439 elapsed_ms=0.000000
  display_connect      ns=57338689738096 elapsed_ms=44.339657
  egl_probe_start      ns=57338747628544 elapsed_ms=102.230105
  egl_probe_end        ns=57338753022355 elapsed_ms=107.623916
  raster_fallback      ns=57338699999325 elapsed_ms=54.600886
  font_atlas_ready     ns=57338685482807 elapsed_ms=40.084368
  file_prefix_read     ns=57338756178529 elapsed_ms=110.780090
  first_layout         ns=57338752845960 elapsed_ms=107.447521
  first_submit         ns=57338762338932 elapsed_ms=116.940493
  first_present        ns=57338763755060 elapsed_ms=118.356621
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=2 p50=54.600 ms p99=1146.792 ms
G3 T5-T0     n=1 p50=1150.155 ms p99=1150.155 ms
T6-T0        n=1 p50=1173.079 ms p99=1173.079 ms
battery: Not charging
```

`logs/39-dz0-a-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=181 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=181 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=181 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-a-repeat-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=5.08. Video `video/39-dz0-a-repeat-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-a-repeat-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.579s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-a-repeat-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57423716030319 elapsed_ms=0.000000
  display_connect      ns=57423764520311 elapsed_ms=48.489992
  egl_probe_start      ns=57423803905545 elapsed_ms=87.875226
  egl_probe_end        ns=57423804202906 elapsed_ms=88.172587
  raster_fallback      ns=57423771424484 elapsed_ms=55.394165
  font_atlas_ready     ns=57423760401625 elapsed_ms=44.371306
  file_prefix_read     ns=57423805668050 elapsed_ms=89.637731
  first_layout         ns=57423803946866 elapsed_ms=87.916547
  first_submit         ns=57423809524315 elapsed_ms=93.493996
  first_present        ns=57423810183317 elapsed_ms=94.152998
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=3 p50=55.393 ms p99=253.661 ms
G3 T5-T0     n=2 p50=1.435 ms p99=254.668 ms
T6-T0        n=2 p50=22.870 ms p99=273.366 ms
battery: Not charging
```

`logs/39-dz0-a-repeat-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=163 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-repeat-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=163 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-repeat-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=163 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-a-before-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.40. Video `video/39-dz0-a-before-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-a-before-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.454s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-a-before-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57529407041201 elapsed_ms=0.000000
  display_connect      ns=57529449419438 elapsed_ms=42.378237
  egl_probe_start      ns=57529479248425 elapsed_ms=72.207224
  egl_probe_end        ns=57529482401928 elapsed_ms=75.360727
  raster_fallback      ns=57529454007550 elapsed_ms=46.966349
  font_atlas_ready     ns=57529445953631 elapsed_ms=38.912430
  file_prefix_read     ns=57529484231514 elapsed_ms=77.190313
  first_layout         ns=57529482344493 elapsed_ms=75.303292
  first_submit         ns=57529487865729 elapsed_ms=80.824528
  first_present        ns=57529488530792 elapsed_ms=81.489591
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=2 p50=46.959 ms p99=285.182 ms
G3 T5-T0     n=1 p50=286.909 ms p99=286.909 ms
T6-T0        n=1 p50=300.033 ms p99=300.033 ms
battery: Not charging
```

`logs/39-dz0-a-before-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=147 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-before-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=147 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-a-before-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=147 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-b-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=3.32. Video `video/39-dz0-b-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-b-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.647s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-b-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57341722892491 elapsed_ms=0.000000
  display_connect      ns=57341777816870 elapsed_ms=54.924379
  egl_probe_start      ns=57341815411770 elapsed_ms=92.519279
  egl_probe_end        ns=57341815590086 elapsed_ms=92.697595
  raster_fallback      ns=57341784937910 elapsed_ms=62.045419
  font_atlas_ready     ns=57341770274354 elapsed_ms=47.381863
  file_prefix_read     ns=57341818426288 elapsed_ms=95.533797
  first_layout         ns=57341815454186 elapsed_ms=92.561695
  first_submit         ns=57341826203923 elapsed_ms=103.311432
  first_present        ns=57341826941300 elapsed_ms=104.048809
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=1.966 ms p99=62.044 ms
G3 T5-T0     n=4 p50=1.964 ms p99=4.664 ms
T6-T0        n=4 p50=21.683 ms p99=22.871 ms
battery: Not charging
```

`logs/39-dz0-b-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=160 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-b-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=160 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-b-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=160 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-c-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=3.37. Video `video/39-dz0-c-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-c-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.633s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-c-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57344520694358 elapsed_ms=0.000000
  display_connect      ns=57344558237268 elapsed_ms=37.542910
  egl_probe_start      ns=57344597608169 elapsed_ms=76.913811
  egl_probe_end        ns=57344597892781 elapsed_ms=77.198423
  raster_fallback      ns=57344564033678 elapsed_ms=43.339320
  font_atlas_ready     ns=57344555306025 elapsed_ms=34.611667
  file_prefix_read     ns=57344599471637 elapsed_ms=78.777279
  first_layout         ns=57344597554795 elapsed_ms=76.860437
  first_submit         ns=57344605825745 elapsed_ms=85.131387
  first_present        ns=57344606871456 elapsed_ms=86.177098
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=2.702 ms p99=43.338 ms
G3 T5-T0     n=4 p50=2.440 ms p99=6.581 ms
T6-T0        n=4 p50=25.932 ms p99=29.847 ms
battery: Not charging
```

`logs/39-dz0-c-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=157 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-c-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=157 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-c-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=157 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-d-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=3.37. Video `video/39-dz0-d-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-d-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.722s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-d-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57347232706382 elapsed_ms=0.000000
  display_connect      ns=57347292845086 elapsed_ms=60.138704
  egl_probe_start      ns=57347340575379 elapsed_ms=107.868997
  egl_probe_end        ns=57347343115978 elapsed_ms=110.409596
  raster_fallback      ns=57347304106411 elapsed_ms=71.400029
  font_atlas_ready     ns=57347286847694 elapsed_ms=54.141312
  file_prefix_read     ns=57347344903049 elapsed_ms=112.196667
  first_layout         ns=57347342999823 elapsed_ms=110.293441
  first_submit         ns=57347352009011 elapsed_ms=119.302629
  first_present        ns=57347352858917 elapsed_ms=120.152535
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=17.778 ms p99=71.398 ms
G3 T5-T0     n=4 p50=7.406 ms p99=20.555 ms
T6-T0        n=4 p50=29.788 ms p99=33.496 ms
battery: Not charging
```

`logs/39-dz0-d-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=168 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-d-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=168 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-d-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=168 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-e-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=3.90. Video `video/39-dz0-e-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-e-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 1.783s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-e-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57350130971683 elapsed_ms=0.000000
  display_connect      ns=57350187288537 elapsed_ms=56.316854
  egl_probe_start      ns=57350233949435 elapsed_ms=102.977752
  egl_probe_end        ns=57350235094433 elapsed_ms=104.122750
  raster_fallback      ns=57350194473810 elapsed_ms=63.502127
  font_atlas_ready     ns=57350184326988 elapsed_ms=53.355305
  file_prefix_read     ns=57350236950953 elapsed_ms=105.979270
  first_layout         ns=57350235026071 elapsed_ms=104.054388
  first_submit         ns=57350241894001 elapsed_ms=110.922318
  first_present        ns=57350243199759 elapsed_ms=112.228076
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=3 p50=63.500 ms p99=261.217 ms
G3 T5-T0     n=2 p50=15.908 ms p99=262.447 ms
T6-T0        n=2 p50=29.113 ms p99=287.757 ms
battery: Not charging
```

`logs/39-dz0-e-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=170 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-e-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=170 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-e-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=170 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `39-dz0-c-repeat-sublimite`: (M)[bat], BAT0=Discharging, AC/online=0, load1=2.09. Video `video/39-dz0-c-repeat-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/39-dz0-c-repeat-sublimite.json`.

Still/video positions (M, capture-command starts): after-QQQ 3.768s; after-right 4.795s; after-next 5.761s.

Process return codes in launch order (M, wait/poll): 0.

`traces/39-dz0-c-repeat-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57621709377017 elapsed_ms=0.000000
  display_connect      ns=57621796252160 elapsed_ms=86.875143
  egl_probe_start      ns=57621840314707 elapsed_ms=130.937690
  egl_probe_end        ns=57621840628533 elapsed_ms=131.251516
  raster_fallback      ns=57621807269512 elapsed_ms=97.892495
  font_atlas_ready     ns=57621792115564 elapsed_ms=82.738547
  file_prefix_read     ns=57621842181041 elapsed_ms=132.804024
  first_layout         ns=57621840633590 elapsed_ms=131.256573
  first_submit         ns=57621849304847 elapsed_ms=139.927830
  first_present        ns=57621850729347 elapsed_ms=141.352330
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=7 p50=2.250 ms p99=97.891 ms
G3 T5-T0     n=6 p50=1.985 ms p99=14.379 ms
T6-T0        n=6 p50=17.804 ms p99=31.263 ms
battery: Discharging
```

`logs/39-dz0-c-repeat-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=433 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-c-repeat-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=433 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/39-dz0-c-repeat-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=433 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

All prescribed orange minimap scans report no events. Zero scan bytes is a documented test result, not a measurement made by this GUI drive. The candidate remains subject to its known wrapped-path navigation cost.

## 40. Checkpoint growth after save — edit-f3i

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| Copy and initial caret / type a | Not applicable: sublimité journals | Private dense-f3i.txt copy; caret at line 1 column 1; a inserted at byte zero | NEW — edit-f3i | caret-origin still; fixture_before hash equals corpus hash in task 37; df checked in logs/40-df.txt |
| Save a / type b / type c and save / type d | Not applicable | Saves clear dirty marker; subsequent edits grow journal by 4096 B, no file-size step | NEW task; FIXED documented post-save symptom — edit-f3i | phase table and logs/40-journal-sizes.txt below |
| Disk content | Not applicable | Prefix abc; remaining bytes match original exactly; size 67108867 B = 67108864 + 3 (P) | NEW passing — edit-f3i | cmp -i 3:0 private-copy corpus returns 0 (M); disk_prefix hex starts 616263 |
| Close / no-file relaunch | Not applicable | Normal close 0; window restored, abcd on first line with dirty marker; second close 0; no error -103 | FIXED drive-5 task-37 failure on new saved-file shape — edit-f3i / edit-c26 | restored-abcd still; process logs below |
| Corpus unchanged / cleanup | Not applicable | Original corpus stat/hash unchanged; only private copy saved; private copy deleted after verification | STILL PRESENT preservation — edit-f3i scope | logs/40-corpus-after.txt; logs/40-copy-deleted.txt |

Measurements below are (M)[AC], BAT0=Not charging, AC/online=1, task-start load1=1.45, with each sample's own power/load also in the journal log. Samples use `stat -c '%y %s'` every prescribed second; each phase ends after ten consecutive unchanged size/mtime samples. Phase entry below is the preceding sample before the action; first sample is the first available post-action observation, so a faster rotation/save can occur between samples. No unobserved checkpoint interval is inferred.

| Phase | Before action B | First post-action / first changed B | Last B | Largest observed consecutive growth B | Samples in phase |
|---|---|---|---|---|---|
| a | 438 | 575 / 4096 | 4096 | 3521 = 4096−575 (P) | 12 |
| save a | 4096 | 4096 / no size change | 4096 | 0 | 11 |
| b after save | 4096 | 8192 | 8192 | 4096 = 8192−4096 (P) | 11 |
| c then save abc | 8192 | 4096 (rotation reduction) | 4096 | 0 positive; −4096 net | 11 |
| d after save | 4096 | 8192 | 8192 | 4096 = 8192−4096 (P) | 11 |

These are observed sample differences, not a count of individual internal checkpoints. The largest positive step is 4096 B, exactly one page, compared with the decision's 4096 B checkpoint measurement on its sparse fixture. No 67108864 B step occurs. Drive 5 task 37's unsaved-file reference was 4233 → 8192 → 12288 B, largest step 4096 B across 32 samples (M)[AC], load1 0.36; it did not contain this save-in-between coverage. The old f3i red-test result 1077936128 B on a 1 GiB fixture (M)[AC], load1 4.52, is decision-document context, not a measured before-run here. Shared-target buffers retaining whole-text checkpoints remain the documented f3i limit.

Save-to-clean-title polling: 166.021 ms after a and 196.765 ms after abc (M, monotonic elapsed from save action; title ROI 270,0,40,40 equals its clean initial bytes). Polling bounds the visible title change, not storage-device durability. Save screenshots confirm clean tab titles.

Post-save typing, first matching glyph in fps=60 recording; ROI/template mean error <12, thirty extracted cells per action, logs/40-onsets.json:

| Key | Command envelope s → first glyph s | intervals / ms |
|---|---|---|
| b | 23.724089–23.728979 → 23.783333 | 3.55 / 59.2 ms (P: intervals×1000/60) |
| c | 34.201800–34.208292 → 34.233333 | 1.89 / 31.5 ms (P: intervals×1000/60) |
| d | 45.258628–45.263360 → 45.316667 | 3.48 / 58.0 ms (P: intervals×1000/60) |

Longest observed post-save typed-key gap is b, 3.55 intervals / 59.2 ms (M, same stamp); no time budget applied. Glyph cell ROIs are [200,46,20,28], [220,46,20,28], [240,46,20,28] for b/c/d respectively.

Evidence `40-f3i-growth-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.45. Video `video/40-f3i-growth-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/40-f3i-growth-sublimite.json`.

Still/video positions (M, capture-command starts): caret-origin 1.128s; save-a-clean 13.075s; save-abc-clean 34.561s; abcd-dirty 55.756s; restored-abcd 57.364s.

Process return codes in launch order (M, wait/poll): 0, 0.

`traces/40-f3i-growth-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57771196997080 elapsed_ms=0.000000
  display_connect      ns=57771234468258 elapsed_ms=37.471178
  egl_probe_start      ns=57771264780318 elapsed_ms=67.783238
  egl_probe_end        ns=57771264985582 elapsed_ms=67.988502
  raster_fallback      ns=57771239680958 elapsed_ms=42.683878
  font_atlas_ready     ns=57771231516785 elapsed_ms=34.519705
  file_prefix_read     ns=57771267776777 elapsed_ms=70.779697
  first_layout         ns=57771264841766 elapsed_ms=67.844686
  first_submit         ns=57771274804557 elapsed_ms=77.807477
  first_present        ns=57771275770822 elapsed_ms=78.773742
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=5 p50=1.318 ms p99=42.683 ms
G3 T5-T0     n=4 p50=1.256 ms p99=1.887 ms
T6-T0        n=4 p50=16.851 ms p99=26.385 ms
battery: Not charging
```

`traces/40-f3i-growth-sublimite-run2.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57827200977989 elapsed_ms=0.000000
  display_connect      ns=57827391054071 elapsed_ms=190.076082
  egl_probe_start      ns=57827436217584 elapsed_ms=235.239595
  egl_probe_end        ns=57827436456767 elapsed_ms=235.478778
  raster_fallback      ns=57827397197084 elapsed_ms=196.219095
  font_atlas_ready     ns=57827240070647 elapsed_ms=39.092658
  file_prefix_read     ns=57827322906537 elapsed_ms=121.928548
  first_layout         ns=57827324747576 elapsed_ms=123.769587
  first_submit         ns=57827439606540 elapsed_ms=238.628551
  first_present        ns=57827440269228 elapsed_ms=239.291239
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=2 p50=1.594 ms p99=196.218 ms
G3 T5-T0     n=1 p50=1.755 ms p99=1.755 ms
T6-T0        n=1 p50=13.629 ms p99=13.629 ms
battery: Not charging
```

`logs/40-f3i-growth-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=3496 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/40-f3i-growth-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=3496 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/40-f3i-growth-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=3496 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Minimap flash scans report no events in the prescribed colours. The private saved copy has been deleted after the final restored-text observation; its private journal remains for task 41 directory inspection.

## 41. Rotation-temporary inventory — edit-52o

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| Inspect task 37 after-build, task 40 and task 39 private journal directories | Not applicable | No reserved-prefix rotation temporary, historical rotation temporary, or double-dot temporary lock; no set-aside file in these profiles | NEW bounded inventory — edit-52o | logs/41-journal-dirs.txt, full-ISO names/sizes/mtimes; logs/41-summary.json |

(M)[AC], BAT0=Not charging, AC/online=1, load1=1.02. Directory listing commands returned zero (M). Ordinary .session-*.lock files remain with their session journals and are not rotation-temp litter. Task 37 after and task 40 have completed clean relaunches; task 39 profiles have clean closes only.

No crash-during-save injection was performed (optional). No run observed a temporary while it existed, so absence establishes only that none was left; it does **not** independently prove startup sweeping of a pre-existing dead temporary. edit-52o's sweep behaviour is therefore not fully established by this clean inventory. No fabricated temporary or old-drive state was modified.

## 36. Deferred large session — edit-fm4 / edit-qct

**Honest limit:** the drive-5 GB-scale orphan with a changed BASE cannot be reconstructed by this drive. build/astra4/sublimite has been deleted; the supplied astra5/astra6 builds write differences-only checkpoints for this workflow, and constructing a GB checkpoint by hand is outside the brief. Therefore this is not a repeat of the old 1077936128-byte journal/changed-BASE fixture. It tests the available small journal referencing a large BASE, plus the prescribed small-journal and empty-profile controls.

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| Copy drive-5 log1g state | Not applicable: no sublimité session journals | Source present with session-Ojr0W3 12288 B and session-sAhalc 4096 B plus locks, matching brief inventory; copied privately, original never launched | STILL PRESENT available fixture — edit-fm4 / edit-qct | logs/36-source-before.json and source-after.json identical |
| First launch / deferred notice | Not applicable | Live untitled tab; notice: large session journal kept as a closed tab; reopen it to restore. No live log tab before reopening | CHANGED from drive-5 not replayed at startup notice — edit-qct | startup-notice still/video; startup table below |
| Type Q in live tab / reopen closed tab | Not applicable | Q accepted in live tab; Ctrl+Shift+T opens log_1g.txt and restores xy at start | CHANGED / FIXED accessible deferred recovery — edit-qct | live-Q and restored-R; no-file launch in copied state |
| R immediately after reopen | Not applicable | Restored first line begins xyR2026…; R is not lost; dirty log tab and separate dirty untitled tab remain | NEW passing input-during-restore probe — edit-qct | reopen/R envelopes and video metrics below. Internal queue ownership is not instrumented |
| Source retirement / resulting journal | Not applicable | session-Ojr0W3 and its lock gone after restore; new session-En7BHO is 4233 B at post-restore sample and 8192 B after close, not GB-sized | CHANGED — edit-qct / edit-f3i | before / after-restore / after-close manifests, full-ISO directory listings |
| Second launch | Not applicable | Live empty untitled tab plus same deferred closed-tab notice: the newly combined session again exceeds startup cost bound and is deferred; no error | CHANGED — edit-qct | run2-tabs still. Reopening this second-generation placeholder was not part of this run; its source remained in private copy until requested cleanup |
| Untouched-placeholder control | Not applicable | Deferred session-Ojr0W3 and its lock retain name, size, mtime_ns and SHA256 after normal close. The small empty session is retired/replaced separately | STILL PRESENT source preservation with CHANGED closed-tab mechanism — edit-fm4 / edit-qct | logs/36-control-before.json / after.json |
| Drive-5 small-journal copy / fresh profile | Not applicable | Both show a prompt window without large-session notice; small-copy shows untitled, fresh shows untitled | CHANGED fixture context, retained usable startup — edit-fm4 | startup table, named recordings |
| Endings / cleanup | Not applicable | Every run returns 0; no -103; all copied state directories deleted after manifests and videos completed | STILL PRESENT normal exit / NEW lazy-restore coverage — edit-qct | stderr audits; logs/36-cleanup.txt |

Combined startup cost is 12288 + 1073741824 = 1073754112 B (P), above the documented 105000000 B bound. The deferred-source journal hash is `90fcb0f19f6bd4ffee13d482e37d95d6213d25b9b2aec63b4af52eafa3ee6ddd`; its mtime_ns is 1792093019195445412 and size 12288 B (M, manifests, per-run stamps below). The untouched control preserves these exactly. Its small 4096-byte session-sAhalc is normally replaced; preservation claim applies to the deferred source, not every small session artifact.

Post-restore 4233 B is close to the decision's 4096 B checkpoint, with subsequent metadata/edit appends; it is not represented as an exactly sampled 4096-byte checkpoint. After close 8192 B remains page-scale. No Ctrl+S was sent on the corpus. Final corpus digest still matches the prescribed original (logs/corpus-end.txt).

Startup observations (M, polling window discovery and video title-template match in ROI 0,0,270,40, mean RGB error <12; fps=60). The initial generic bright-pixel detector was rejected because a background Sublime title already contained bright pixels; these final template measurements identify the actual candidate title. Window-found is a polling upper bound and may trail the first painted frame.

| Run | BAT0 / AC / load1 | launch → first painted video s | intervals / ms | window-found ms |
|---|---|---|---|---|
| 36-deferred-sublimite, launch 1 | Not charging / 1 / 1.02 | 0.798560 → 0.916667 | 7.09 / 118.1 ms (P) | 143.793 |
| 36-deferred-sublimite, launch 2 | Not charging / 1 / 1.42 | 2.946286 → 3.083333 | 8.22 / 137.0 ms (P) | 110.603 |
| 36-control-sublimite, launch 1 | Not charging / 1 / 1.42 | 0.797294 → 0.916667 | 7.16 / 119.4 ms (P) | 109.709 |
| 36-small-copy-sublimite, launch 1 | Not charging / 1 / 1.42 | 0.800095 → 0.916667 | 6.99 / 116.6 ms (P) | 125.526 |
| 36-fresh-sublimite, launch 1 | Not charging / 1 / 1.71 | 0.780422 → 0.950000 | 10.17 / 169.6 ms (P) | 174.498 |

Drive-5 task-36 reference: mapped 68.443 ms / first frame 109.7 ms, repeat 65.786 ms / 119.2 ms (M)[AC], BAT0 Not charging, AC=1, load1 0.24 / 0.30. Those launches had the GB orphan; the small-copy and fresh controls here have none. These are context, not pass/fail or a controlled ranking.

Lazy restore / injected R (M, first matching glyph cells in fps=60 video, mean RGB error <12; task-start BAT0 Not charging, AC=1, load1 1.02; action stamp also in JSON):

| Action | command envelope s → matching frame s | intervals / ms | ROI |
|---|---|---|---|
| reopen | 1.655825–1.707522 → 1.750000 | 5.65 / 94.2 ms (P: intervals×1000/60) | [160, 46, 40, 28] |
| parked-R | 1.713480–1.738693 → 1.766667 | 3.19 / 53.2 ms (P: intervals×1000/60) | [200, 46, 20, 28] |

The reopened window progresses from empty placeholder to recovered text; R is issued before the first recovered-text frame and is present in the resulting text. The source retires only in the successful-restore run; the untouched control keeps it. Closed tabs are not visibly listed in the live tab strip; existence is established by the notice and successful Ctrl+Shift+T reopening. Documented limits remain: no idle restore trigger, whole-text journals differing at both ends may copy their middle, and large deferred untitled text is inserted on the UI thread. Those shapes were not manufactured here.

Evidence `36-deferred-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.02. Video `video/36-deferred-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/36-deferred-sublimite.json`.

Still/video positions (M, capture-command starts): startup-notice 0.974s; live-Q 1.394s; restored-R 1.901s; run2-tabs 3.081s.

Process return codes in launch order (M, wait/poll): 0, 0.

`traces/36-deferred-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57965367662401 elapsed_ms=0.000000
  display_connect      ns=57965414390127 elapsed_ms=46.727726
  egl_probe_start      ns=57965446103558 elapsed_ms=78.441157
  egl_probe_end        ns=57965448946657 elapsed_ms=81.284256
  raster_fallback      ns=57965420598985 elapsed_ms=52.936584
  font_atlas_ready     ns=57965405690561 elapsed_ms=38.028160
  file_prefix_read     ns=57966242688278 elapsed_ms=875.025877
  first_layout         ns=57965409046430 elapsed_ms=41.384029
  first_submit         ns=57965451361250 elapsed_ms=83.698849
  first_present        ns=57965451895303 elapsed_ms=84.232902
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=4 p50=5.444 ms p99=52.935 ms
G3 T5-T0     n=3 p50=5.634 ms p99=17.169 ms
T6-T0        n=3 p50=27.576 ms p99=29.503 ms
battery: Not charging
```

`traces/36-deferred-sublimite-run2.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57967515639688 elapsed_ms=0.000000
  display_connect      ns=57967557025094 elapsed_ms=41.385406
  egl_probe_start      ns=57967590789839 elapsed_ms=75.150151
  egl_probe_end        ns=57967594553866 elapsed_ms=78.914178
  raster_fallback      ns=57967562540380 elapsed_ms=46.900692
  font_atlas_ready     ns=57967553300691 elapsed_ms=37.661003
  file_prefix_read     not recorded
  first_layout         ns=57967594335796 elapsed_ms=78.696108
  first_submit         ns=57967597219277 elapsed_ms=81.579589
  first_present        ns=57967597714402 elapsed_ms=82.074714
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=46.900 ms p99=46.900 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

`logs/36-deferred-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=240 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-deferred-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=240 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-deferred-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=240 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `36-control-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.42. Video `video/36-control-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/36-control-sublimite.json`.

Still/video positions (M, capture-command starts): untouched-placeholder 0.927s.

Process return codes in launch order (M, wait/poll): 0.

`traces/36-control-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57969455505444 elapsed_ms=0.000000
  display_connect      ns=57969505143031 elapsed_ms=49.637587
  egl_probe_start      ns=57969545025573 elapsed_ms=89.520129
  egl_probe_end        ns=57969545289104 elapsed_ms=89.783660
  raster_fallback      ns=57969514722431 elapsed_ms=59.216987
  font_atlas_ready     ns=57969496614425 elapsed_ms=41.108981
  file_prefix_read     not recorded
  first_layout         ns=57969499863233 elapsed_ms=44.357789
  first_submit         ns=57969548069071 elapsed_ms=92.563627
  first_present        ns=57969548764279 elapsed_ms=93.258835
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=59.216 ms p99=59.216 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

`logs/36-control-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=115 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-control-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=115 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-control-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=115 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `36-small-copy-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.42. Video `video/36-small-copy-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/36-small-copy-sublimite.json`.

Still/video positions (M, capture-command starts): startup 0.947s.

Process return codes in launch order (M, wait/poll): 0.

`traces/36-small-copy-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57971469593053 elapsed_ms=0.000000
  display_connect      ns=57971520629448 elapsed_ms=51.036395
  egl_probe_start      ns=57971555086764 elapsed_ms=85.493711
  egl_probe_end        ns=57971557577799 elapsed_ms=87.984746
  raster_fallback      ns=57971527286985 elapsed_ms=57.693932
  font_atlas_ready     ns=57971515580840 elapsed_ms=45.987787
  file_prefix_read     not recorded
  first_layout         ns=57971557435716 elapsed_ms=87.842663
  first_submit         ns=57971560404224 elapsed_ms=90.811171
  first_present        ns=57971561498043 elapsed_ms=91.904990
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=57.692 ms p99=57.692 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

`logs/36-small-copy-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-small-copy-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-small-copy-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Evidence `36-fresh-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.71. Video `video/36-fresh-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/36-fresh-sublimite.json`.

Still/video positions (M, capture-command starts): startup 0.982s.

Process return codes in launch order (M, wait/poll): 0.

`traces/36-fresh-sublimite-run1.txt` (M, trace timestamps/percentiles, same run stamp):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=57973439786414 elapsed_ms=0.000000
  display_connect      ns=57973489570665 elapsed_ms=49.784251
  egl_probe_start      ns=57973561557731 elapsed_ms=121.771317
  egl_probe_end        ns=57973562920442 elapsed_ms=123.134028
  raster_fallback      ns=57973500422749 elapsed_ms=60.636335
  font_atlas_ready     ns=57973486657546 elapsed_ms=46.871132
  file_prefix_read     not recorded
  first_layout         ns=57973562753704 elapsed_ms=122.967290
  first_submit         ns=57973568275246 elapsed_ms=128.488832
  first_present        ns=57973569270061 elapsed_ms=129.483647
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=60.634 ms p99=60.634 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

`logs/36-fresh-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-fresh-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/36-fresh-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=113 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

All prescribed minimap-colour scans report no events; the blue recovery notice is deliberate UI. All task-36 state copies were deleted after their before/after manifests; original drive-5 sources are untouched.

## Verdict

The drive-5 blocker is fixed on the pinned candidate: dirty dense-file sessions reopen repeatedly with their edits, including after saves. Find in files returns the same matching text as Sublime and passes candidate navigation, read-only, cancellation and source-integrity probes. Deferred recovery restores the preserved edit and newly queued input, and an unopened placeholder preserves its source journal. No candidate crash, lost text, wrong hit position or corrupted file was established in the core set.

The judgement is limited to these driven cases. Sublime's successful search text was compared, but later comparator navigation stalled in its restored real-profile session. Task 39 retains a visible initial End-navigation/queued-input wait; its clean before/after observations are similar, while near-caret typing completes in a few recording intervals. This is a bounded large-file navigation finding, not demonstrated sustained typing lag or a regression. The apparent blank root still was not supported by the video. GB whole-text changed-BASE recovery, dead-temp sweeping and the unwrapped zero-scan property remain explicitly unverified; no failure of those paths is claimed. Colour emoji artwork remains waived. No appendix timing is needed to reach this core-set judgement.

VERDICT usable instead of Sublime for Tobias's daily edits: yes; blockers: none observed in the core set

## Appendix: provisional rows re-run

Only completed appendix task sections below are reverified. The original core VERDICT line remains unchanged.

### 1. Point-size override provisional row

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| EDIT_FONT_SIZE_PT=12, inherited override() sequence | Not applicable: candidate environment override | 31 px ink, rows 55–85 inclusive, matching drive 5; normal close 0 | STILL PRESENT passing — edit-1nm | frames/font12-sublimite-font.png, logs/font12-ink.json, video/font12-sublimite.mkv |

(M)[AC], BAT0=Not charging, AC/online=1, load1=1.17. Ink is counted with max RGB>130 in inherited ROI (180,50,120,42); 85−55+1=31 px (P). Drive 5 measured the same 31 px at load1 8.97 (M)[AC], BAT0 Not charging, AC=1. Pixel height itself is structural, so high load did not make that value provisional; this repeat supplies a clean-load observation. The inherited file and actions are unchanged.

The final 31-row glyph appearance is first matched at 1.866667s from launch command at 0.789028s: 64.66 intervals / 1077.6 ms (M; P conversion ×1000/60), logs/font12-onset.json. This measures final override adoption, not the first provisional/fallback text frame. Drive 5 did not publish a corresponding adoption interval for this row; no invented before value or speed ranking is supplied.


Evidence `font12-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.17. Video `video/font12-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/font12-sublimite.json`.

Still/video positions (M, capture-command starts): launch 1.931s; font 2.359s; final 2.803s.

Process return codes in launch order (M, wait/poll): 0.

`logs/font12-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=212 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/font12-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=212 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/font12-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=212 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Literal trace (M, task stamp above):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=58249402992427 elapsed_ms=0.000000
  display_connect      ns=58249439027710 elapsed_ms=36.035283
  egl_probe_start      ns=58249468193520 elapsed_ms=65.201093
  egl_probe_end        ns=58249468496861 elapsed_ms=65.504434
  raster_fallback      ns=58249443906489 elapsed_ms=40.914062
  font_atlas_ready     ns=58249436387903 elapsed_ms=33.395476
  file_prefix_read     ns=58249471366463 elapsed_ms=68.374036
  first_layout         ns=58249468301741 elapsed_ms=65.309314
  first_submit         ns=58249474298244 elapsed_ms=71.305817
  first_present        ns=58249474976425 elapsed_ms=71.983998
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=1 p50=40.913 ms p99=40.913 ms
G3 T5-T0     n=0
T6-T0        n=0
battery: Not charging
```

### 2. Paired isolated-Z provisional row

| Action | Sublime did | sublimité did | Drive-5 status / bead | Evidence |
|---|---|---|---|---|
| Identical fresh paired fixture, Ctrl+Home, isolated Z | Capture obscured by update/save dialogs; no valid glyph onset; not a successful remeasurement | Z visible at beginning; normal close 0; onset below | STILL PRESENT functioning candidate typing; comparator rerun incomplete — no bead | video/02-paired-{sublime,sublimite}.mkv, corresponding probe stills and logs/02-paired-metrics.json |


(M)[AC], BAT0=Not charging, AC/online=1, load1=1.04; `02-paired-sublimite`.
Command 2.879402–2.888410s → first glyph 2.933333s, 3.24 intervals / 53.9 ms (M; P conversion ×1000/60). Inherited ROI [40, 46, 20, 27]; changed-pixel template mask 185 pixels, mean gray error <18; thirty-frame extraction in frames/02-paired-sublimite-Z-sequence.

(M)[AC], BAT0=Not charging, AC/online=1, load1=1.52; `02-paired-sublime`.
No qualifying glyph change; occluding dialogs make this observation invalid, not slow or a failure of text insertion.

Drive-5 `02-paired-sublimite`: 3.243144035339345 intervals / 54.1 ms (M, inherited video/template measurement); candidate load1 6.66, Sublime load1 8.39 (Sublime provisional), both BAT0 Not charging, AC=1.

Drive-5 `02-paired-sublime`: 1.511137962341298 intervals / 25.2 ms (M, inherited video/template measurement); candidate load1 6.66, Sublime load1 8.39 (Sublime provisional), both BAT0 Not charging, AC=1.

The private-config attempt did not yield a clean isolated comparator: the existing Sublime session/dialogs still affected the capture. No successful private-profile isolation is claimed. The copied preference file is retained under state/sublime-clean; update checking was disabled only in that copy. A later normal-quit attempt exposed a save prompt for the drive-created f08 fixture; test changes were not saved. Candidate corpus/search-fixture integrity was rechecked at the end. The complete paragraph/burst and other task-2 rows were not rerun in this appendix section.

Evidence `02-paired-sublimite`: (M)[AC], BAT0=Not charging, AC/online=1, load1=1.04. Video `video/02-paired-sublimite.mkv`; commands, focus checks, full-ISO directory listings and stderr tails in `logs/02-paired-sublimite.json`.

Still/video positions (M, capture-command starts): launch 1.930s; before 2.607s; probe 3.889s; final 4.133s.

Process return codes in launch order (M, wait/poll): 0.

`logs/02-paired-sublimite-flash-6b5030.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=290 fps=60 colour=#6b5030 tol=24 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/02-paired-sublimite-flash-bf853d.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=290 fps=60 colour=#bf853d tol=40 min_px=200
flashes (appear then vanish within 30 frames): 0
```

`logs/02-paired-sublimite-flash-ff8800.txt` (M, prescribed minimap ROI 1568,40,160,1220):
```text
frames=290 fps=60 colour=#ff8800 tol=60 min_px=200
flashes (appear then vanish within 30 frames): 0
```

Literal trace (M, task stamp above):
```text
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=58425479335216 elapsed_ms=0.000000
  display_connect      ns=58425513315046 elapsed_ms=33.979830
  egl_probe_start      ns=58425544555540 elapsed_ms=65.220324
  egl_probe_end        ns=58425546232401 elapsed_ms=66.897185
  raster_fallback      ns=58425518200714 elapsed_ms=38.865498
  font_atlas_ready     ns=58425510439078 elapsed_ms=31.103862
  file_prefix_read     ns=58425546258603 elapsed_ms=66.923387
  first_layout         ns=58425546064159 elapsed_ms=66.728943
  first_submit         ns=58425548310333 elapsed_ms=68.975117
  first_present        ns=58425548787585 elapsed_ms=69.452369
  egl_load_start       not recorded
  egl_load_end         not recorded
G1 T4-T0     n=3 p50=1.088 ms p99=38.864 ms
G3 T5-T0     n=2 p50=0.986 ms p99=1.186 ms
T6-T0        n=2 p50=21.472 ms p99=21.567 ms
battery: Not charging
```

### Remaining appendix coverage

Not run in this drive: the remaining provisional task groups 6, 8, 10–17, 19–20, 23–31 and 33–35, and the remaining inherited tasks/optional paired probes. The task-1 appendix covers only the point-size override; task 2 covers only the paired isolated-Z attempt. They are **not** marked passing, fixed or unchanged by inheritance. Core task 39 does not substitute for the inherited task-10/31 sequences. No appendix regression requiring a VERDICT AMENDMENT was established. The original verdict remains scoped to the core set and the explicitly reviewed observations.

### Final artifact audit

Both pinned binary stat tuples and full SHA256 digests still match the start after the appendix (END stamps in logs/binary.txt and logs/binary-astra5.txt). (M)[AC], BAT0=Not charging, AC/online=1, load1=1.05. The final tree-integrity assertion initially compared aggregate hashes made with different pathname spellings; this audit corrected that measurement error. Absolute-path end hash equals the absolute-path initial hash, and relative-path end hash equals the relative-path post-task hash. Every fixture file also equals its generated bytes. This is a harness hash-normalization correction, not file damage or a product regression. The original VERDICT line is unchanged. Video probe results, including retained incomplete attempts, are in logs/video-final-audit.json. Xvfb owner argv verified in logs/xvfb-owner-verified.txt; no X server signal was sent.
