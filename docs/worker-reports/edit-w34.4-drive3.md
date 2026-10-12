# edit-w34.4 drive 3 worker log

Read drive-2 report, HANDOFF, CLAUDE and PLAN workflow. No bd/git commands; no edits under src/tests/bench/fuzz/tools; prior evidence untouched. Only :98 used. Initial xdpyinfo failed even in escalation because no server was available. Restricted Xvfb could not bind; escalated Xvfb succeeded. Logs: build/astra3/logs/xvfb*.log and xdpyinfo*.txt. Active Xvfb owned by this drive.

Videos will be in build/astra3/video; logs, traces, frames and state alongside. Power stamping is implemented in separate execute.py; original copied harness key sequences remain unchanged. /tmp/edit-corpus was absent, so matched small/dense fixtures were copied from drive 2. Corpus regeneration is needed for the missing large log.

## Copied harness diff

The following is the concatenation of `diff -u` for each copied Python script (equivalent recursive comparison restricted to the copied script set). Only output-base strings change; new drive-3 scripts are separate.

```diff
--- build/astra2/drive.py	2026-10-11 21:22:11.253005261 +1000
+++ build/astra3/drive.py	2026-10-12 23:21:18.785713170 +1000
@@ -2,7 +2,7 @@
 from pathlib import Path
 from Xlib import X,display,protocol
 os.environ['DISPLAY']=':98'
-ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra2'
+ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra3'
 class Drive:
  def __init__(self,task,editor):
   self.task=task;self.editor=editor;self.stem=f'{task}-{editor}';self.events=[];self.proc=None;self.w=None
--- build/astra2/final_audit.py	2026-10-11 22:02:49.866101688 +1000
+++ build/astra3/final_audit.py	2026-10-12 23:21:18.785713170 +1000
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re,collections,subprocess as sp,concurrent.futures,datetime
-b=Path('build/astra2');r=[]
+b=Path('build/astra3');r=[]
 for p in sorted((b/'video').glob('*sublimite.mkv')):
  for c,t in [('bf853d',40),('ff8800',60),('6b5030',24)]:
   out=b/'logs'/f'{p.stem}.flash-{c}.txt';txt=out.read_text();m=re.search(r'flashes .*: (\d+)',txt);assert m,(p,c)
--- build/astra2/metrics.py	2026-10-11 21:21:08.374977660 +1000
+++ build/astra3/metrics.py	2026-10-12 23:21:18.786713159 +1000
@@ -2,7 +2,7 @@
 from pathlib import Path
 import numpy as np
 from PIL import Image
-B=Path('build/astra2')
+B=Path('build/astra3')
 def events(stem):
  e=json.loads((B/'logs'/f'{stem}.json').read_text());t=float(re.search(r'start: ([\d.]+)',(B/'logs'/f'{stem}.ffmpeg.log').read_text())[1]);return e,t
 
--- build/astra2/scan_all.py	2026-10-11 21:27:32.497140163 +1000
+++ build/astra3/scan_all.py	2026-10-12 23:21:18.785713170 +1000
@@ -1,6 +1,6 @@
 from pathlib import Path
 import subprocess as sp,json,re
-B=Path('build/astra2');r=[]
+B=Path('build/astra3');r=[]
 for p in sorted((B/'video').glob('*sublimite.mkv')):
  for rgb,tol in [('bf853d',40),('ff8800',60),('6b5030',24)]:
   out=B/'logs'/f'{p.stem}.flash-{rgb}.txt'
--- build/astra2/scan_parallel.py	2026-10-11 21:29:38.569050890 +1000
+++ build/astra3/scan_parallel.py	2026-10-12 23:21:18.786713159 +1000
@@ -1,6 +1,6 @@
 from pathlib import Path
 import subprocess as sp,json,re,concurrent.futures
-B=Path('build/astra2')
+B=Path('build/astra3')
 def scan(job):
  p,rgb,tol=job;out=B/'logs'/f'{p.stem}.flash-{rgb}.txt'
  if not out.exists():
--- build/astra2/sheets.py	2026-10-11 21:22:33.320106646 +1000
+++ build/astra3/sheets.py	2026-10-12 23:21:18.785713170 +1000
@@ -1,7 +1,7 @@
 from pathlib import Path
 from PIL import Image,ImageDraw
 import sys
-B=Path('build/astra2')
+B=Path('build/astra3')
 for task in sys.argv[1:]:
  ps=sorted((B/'frames').glob(task+'*.png'))
  for start in range(0,len(ps),8):
--- build/astra2/tasks.py	2026-10-11 21:19:57.968939034 +1000
+++ build/astra3/tasks.py	2026-10-12 23:21:18.785713170 +1000
@@ -47,7 +47,7 @@
   d.key('ctrl+f');d.shot('find-panel');d.type('alpha',100);d.shot('find-query');d.key('Return');d.key('Escape');d.key('F3');d.shot('next-match');d.key('ctrl+h');d.shot('replace-panel');d.key('Escape')
  elif task=='08-save':
   d.type('EDITED_',100);d.key('ctrl+s');d.cmd(['cat',str(BASE/'files'/filename)]);d.shot('after-save')
-  d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra2/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
+  d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra3/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
   d.x('windowfocus',1);time.sleep(.3);d.focus();time.sleep(1);d.key('ctrl+End');d.shot('external-refocus');d.cmd(['cat',str(BASE/'files'/filename)])
  elif task=='11-resize':
   for w,h in [(1440,900),(2880,1800),(800,600),(960,600)]:
--- build/astra2/timelines.py	2026-10-11 21:23:24.623737260 +1000
+++ build/astra3/timelines.py	2026-10-12 23:21:18.785713170 +1000
@@ -1,6 +1,6 @@
 import json,re
 from pathlib import Path
-B=Path('build/astra2')
+B=Path('build/astra3')
 for p in (B/'logs').glob('*.json'):
  f=p.with_suffix('.ffmpeg.log')
  if not f.exists():continue

```

## Runs
- Completed harness `01-fresh-sublimite`.
- Completed harness `01-fresh-sublime`.
- Completed harness `02-typing-r2-sublimite`.
- Completed harness `02-typing-r2-sublime`.
- Completed harness `03-nav-sublimite`.
- Completed harness `03-nav-sublime`.
- Completed harness `03-selection-sublimite`.
- Completed harness `03-selection-sublime`.
- Completed harness `04-mouse-sublimite`.
- Completed harness `04-mouse-sublime`.
- Completed harness `05-edit-sublimite`.
- Completed harness `05-edit-sublime`.
- Completed harness `05-edit-r2-sublimite`.
- Completed harness `05-edit-r2-sublime`.
- Completed harness `06-clipboard-sublimite`.
- Completed harness `06-clipboard-sublime`.
- Completed harness `07-find-sublimite`.
- Completed harness `07-find-sublime`.
- Completed harness `08-save-sublimite`.
- Completed harness `08-save-sublime`.
- Completed harness `zoom-sublimite`.
- Completed harness `zoom-sublime`.
- Completed harness `font12-sublimite`.
- Completed harness `06-alternate-sublimite`.

- Restored missing /tmp/edit-corpus with the existing build/tools/mkcorpus; no rebuild or source modification. C and dense compare byte-identical with drive 2; large-log stat/wc/hash in logs/corpus.txt. The generator also creates its standard ancillary corpus files in /tmp. Drive only opens log_1g in place, without saving.
- First fresh Sublime recording contains an Update Available dialog over the text. Subsequent matched windows are unobstructed. Keep initial evidence; do not claim a clean startup metric from the dialog crop.
- Completed harness `06-alternate-sublime`.
- Completed harness `06-cross-source-sublime`.
- Completed harness `06-cross-sublimite`.
- Completed harness `06-cross-back-sublime`.
- Completed harness `02-paced-sublimite`.
- Completed harness `02-paced-sublime`.
- Completed harness `02-paced-verified-sublimite`.
- Completed harness `02-paced-verified-sublime`.

## Literal recursive harness diff

Command: `diff -r build/astra3/logs/harness-drive2 build/astra3/logs/harness-drive3`; these directories contain byte copies of the original and copied Python scripts only. Exit 1 means expected base-path differences.

```diff
diff -r build/astra3/logs/harness-drive2/drive.py build/astra3/logs/harness-drive3/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra2'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra3'
diff -r build/astra3/logs/harness-drive2/final_audit.py build/astra3/logs/harness-drive3/final_audit.py
3c3
< b=Path('build/astra2');r=[]
---
> b=Path('build/astra3');r=[]
diff -r build/astra3/logs/harness-drive2/metrics.py build/astra3/logs/harness-drive3/metrics.py
5c5
< B=Path('build/astra2')
---
> B=Path('build/astra3')
diff -r build/astra3/logs/harness-drive2/scan_all.py build/astra3/logs/harness-drive3/scan_all.py
3c3
< B=Path('build/astra2');r=[]
---
> B=Path('build/astra3');r=[]
diff -r build/astra3/logs/harness-drive2/scan_parallel.py build/astra3/logs/harness-drive3/scan_parallel.py
3c3
< B=Path('build/astra2')
---
> B=Path('build/astra3')
diff -r build/astra3/logs/harness-drive2/sheets.py build/astra3/logs/harness-drive3/sheets.py
4c4
< B=Path('build/astra2')
---
> B=Path('build/astra3')
diff -r build/astra3/logs/harness-drive2/tasks.py build/astra3/logs/harness-drive3/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra2/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra3/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra3/logs/harness-drive2/timelines.py build/astra3/logs/harness-drive3/timelines.py
3c3
< B=Path('build/astra2')
---
> B=Path('build/astra3')

```
- Completed harness `02-paced-verified-r2-sublime`.
- Completed harness `09-tabs-sublimite`.
- Completed harness `09-tabs-sublime`.
- Completed harness `11-resize-sublimite`.
- Completed harness `11-resize-sublime`.
- Completed harness `12-hotexit-sublimite`.
- Completed harness `12-hotexit-sublime`.
- Completed harness `13-minimap-sublimite`.
- Completed harness `13-minimap-sublime`.
- Completed harness `14-idle-sublimite`.
- Completed harness `14-idle-sublime`.
- Completed harness `utf8-sublimite`.
- Completed harness `utf8-sublime`.
- Completed harness `15-mouse-sublimite`.
- Completed harness `15-mouse-wrapped-sublimite`.
- Completed harness `15-mouse-sublime`.
- Completed harness `15-mouse-wrapped-sublime`.
- Completed harness `16-find-sublimite`.
- Completed harness `16-find-sublime`.

- Task 15 initial Sublime targeting grid was wrong (assumed cell 8 px / row 18 px); its underscore target selected `int`, not `fn_0`. Preserved that recording. Native crop `sublime-grid.png` establishes cell 10 px / row 19 px, origin x=92/y=60 for this run (M)[AC]. Separate calibrated repeat uses quarter-cell targets and logs both editors' actual grid/coordinates. Original tasks 1–14 remain untouched.
- Completed harness `17-recovery-a-sublimite`.
- Completed harness `17-recovery-b-sublimite`.
- Completed harness `17-recovery-c-sublimite`.
- Completed harness `17-recovery-d-sublimite`.
- Completed harness `17-recovery-e-sublimite`.
- Completed harness `17-recovery-a-sublime`.
- Completed harness `18-tabtitle-sublimite`.
- Completed harness `18-tabtitle-sublime`.
- Completed harness `19-external-sublimite`.
- Completed harness `19-external-sublime`.
- Completed harness `20-emoji-sublimite`.
- Completed harness `20-emoji-sublime`.
- Completed harness `12-recovery-conflict-sublimite`.
- Completed harness `16-original-sequence-sublimite`.
- Completed harness `16-original-sequence-sublime`.
- Completed harness `01-fresh-repeat-sublimite`.
- Completed harness `01-fresh-repeat-sublime`.
- Completed harness `15-wrapped-reverse-sublimite`.
- Completed harness `15-wrapped-reverse-sublime`.
- Completed harness `15-mouse-calibrated-sublimite`.

- Native zoom comparison corrected an interim reading: Sublime enlarges for Ctrl+= and shrinks for Ctrl+minus here; Ctrl+0 does not reset. New emoji zoom therefore changes subsequent Sublime cell size. The calibrated mouse repeat derives its then-current grid from `sublime-grid-after-emoji.png` and glyph boxes (cell 11 px, row 23 px, origin about x=98/y=60, M[AC]). A separately recorded font restoration sends three Ctrl+minus steps before the remaining original large-file tasks, restoring the original font size without changing their copied key sequences.
- Supplemental clean external-shrink and explicit Sublime Reload/Cancel-button probes are separated from the original task-19 sequence, where Sublime Ctrl+Z undoes the reload and thus makes the buffer dirty before the shrink. Initial evidence remains intact.
- Completed harness `15-mouse-calibrated-sublime`.
- Completed harness `18-tabtitle-second-sublimite`.
- Completed harness `18-tabtitle-second-sublime`.
- Completed harness `19-external-controlled-sublimite`.
- Completed harness `19-external-controlled-sublime`.

- First calibrated Sublime repeat was blank/loading during click/drag (`15-mouse-calibrated-sublime-grid/click-mid/drag.png`), becoming readable before underscore/triple-click probes. Those early actions are invalid input comparisons, not editor mouse failures. A readiness-gated target-only repeat is required; preserve failed evidence. The sublimité calibrated selection readbacks start at the intended column and retain the anchor.
- Completed harness `20-emoji-carets-sublimite`.
- Completed harness `20-emoji-carets-sublime`.
- Completed harness `20-font-restore-sublime`.
- Completed harness `10-large-sublimite`.
- Completed harness `10-large-sublime`.
- Completed harness `dense64-sublimite`.
- Completed harness `dense64-sublime`.
- Completed harness `10-large-early-sublimite`.

- Copied standard Sublime large run remained blank throughout. Copied safe-mode run has the Safe Mode modal (`10-large-clean-sublime-initial.png`). Preserved both with their original sequences; a separately recorded loaded-session continuation will dismiss dialogs before comparing navigation/search. Automatic new large-find pass was restricted to sublimité so no blank/modal Sublime run is silently reported as successful.
- Completed harness `10-large-clean-sublime`.

- The long batch exited 143 (SIGTERM reported by the execution tool) after `10-large-clean-sublime done`, before it created the large-find/font logs. No SIGTERM was requested by this drive; cause is unknown. Completed MKVs/logs are intact. Remaining large-find/font/external-repro tasks restarted in a separate invocation. This resembles drive 2's unexplained harness interruption but is not attributed to a proven editor defect.
- Completed harness `16-find-large-sublimite`.
- Completed harness `21-font-iosevka-sublimite`.
- Completed harness `21-font-liberation-mono-sublimite`.
- Completed harness `21-font-proportional-sublimite`.
- Completed harness `21-font-missing-sublimite`.
- Completed harness `21-font-file-sublimite`.
- Completed harness `19-external-repro-sublimite`.
- Completed harness `10-large-resume-sublime`.
- Completed harness `10-large-loaded-sublime`.
- Completed harness `16-find-large-wait-sublimite`.

- Extended clean Sublime search visibly reaches the rare near-end match. Its automated stop observer sampled the partially clipped first gutter row and failed to recognize the full line number, so the capture continues through its observation loop even after success. The final functional result and latency use the actual highlighted-match video frames, not that OCR loop's `far_match_not_observed` flag. Preserve the observer output as a harness limitation.
- Completed harness `16-find-large-complete-sublime`.
- Completed harness `22-common-commands-sublimite`.
- Completed harness `22-common-commands-sublime`.
- Completed harness `15-mouse-target-ready-sublime`.

- The inherited mean-error detector with a very sparse gutter-only ROI yielded negative or premature matches for some new wheel probes. Those numbers are rejected. `action_onsets.py` uses a wider text ROI and a changed-pixel mask; raw rejected outputs remain in new-onset-metrics.json for audit. Original glyph/startup measurements retain drive 2's method and are checked against extracted frames.
- Idle's Sublime outside-caret event is the bottom Detect Indentation status text clearing. The grayscale detector counted 785 changed pixels; a separate RGB comparison at the same 4.85 s event counted 887 and locates (123,585)–(394,596) (M)[AC]. No editor text changed. Sublimité reports the expected twenty caret transitions and zero outside-caret changes.

- New action-onset analysis initially failed reshaping an odd-height crop: ffmpeg cropped the YUV420 input to an even height before grayscale conversion. Fixed only the new analysis scripts by converting to gray before cropping and reran them. This is an analysis failure, not an editor failure; drive-2 copied scripts were not edited.

- Final focused comparisons: calibrated sublimité and readiness-gated Sublime drag/Shift+click byte selections match exactly; both select fn_0. Some normal Sublime launches report a different focused window/child ID than the launcher's first ID; `logs/focus-audit.json` preserves this. Visible requested-file titles and readbacks establish the valid comparisons. No claim is based on numeric XID equality alone.
- Corrected an analysis indexing error: ABSENT_ASTRA_987654321 has 22 characters, not 21. Initial shifted target pairing produced zero masks; rejected results are in late-metrics-query-index-rejected.json. Corrected per-character results are in query-metrics.json and merged into late-metrics.json.
- The whole-word onset crop in Sublime was obscured by x11grab's X cursor (ImageMagick stills omit it). `word-pointer-comparison.png` shows the cause. The unoccluded first f cell gives the validated onset in sublime-word-cursor-excluded.json; the failed whole-word detector is retained.
- Corpus final SHA256 remains a259640b05b70487cd68a641913d8f2d8023d77f9bd1a5ba63beb555ceaae875; the large log was not saved.
- Important: another build changed build/sublimite AFTER all sublimité captures. Last recorded sublimité stop: 2026-10-13T00:27:13.499340+10:00. New executable mtime: 2026-10-13T01:02:21.395697+10:00, size 5,137,984 bytes (M)[AC]. BUILD still names 11baa63. No new executable was driven. This report applies to the initial 5,131,640-byte executable, mtime 2026-10-12 00:05:47.058168432 +1000, not the post-capture replacement. Evidence: logs/post-capture-binary-change.json and final-environment.txt.

- Startup validation found a caret-phase artifact in the inherited crop: the later sublimité repeat matched at 685.8 ms with the caret included, but text excluding the caret matched at 202.5 ms (M)[AC]. The original 169.7 ms value is unchanged by the same check. The phase-sensitive repeat is rejected as first-text latency; both raw and corrected JSON remain. Drive-2 quoted startup values were not recomputed or altered.
- Dense comparison correction: sublimité visibly reaches 26,845,451 and inserts Z; the matched Sublime dense recording stays at line 1 and does not show Z despite the inherited commands. The final report records this as incomplete comparison behavior, not a pass. Its cause is not established.

## Final execution summary

- Report: docs/reviews/astra-vs-sublime-3.md, written directly and incrementally, then organized into per-task tables. The launch's build/astra3/final-message.md was never used as a report destination.
- Completed 99 per-task/continuation recordings (M)[AC], from record_stop events in logs/run-index.json. All 53 sublimité MKVs have all three requested colour scans: 159 nonempty, decoder-error-free results, all zero flashes and zero raw colour-change events (M)[AC]. Moving resize and find-resize strips add six zero results, for 165 scans total (arithmetic on counted outputs).
- Legacy execution: execute.py wrapped run.py, batch2.py (unchanged imported tasks/more definitions), large_early.py, recovery_conflict.py, paced_verified*.py and sublime_clean_large.py. New coverage and readiness/repro supplements are separate scripts under build/astra3, named for their tasks. No original keystroke/delay/coordinate sequence was edited.
- Evidence: build/astra3/video/*.mkv; frames/ for named stills and thirty-frame sequences; logs/ for full command envelopes, power/load stamps, focus checks, clipboard/disk hashes, metric JSON, detector stdout and timeline files; traces/ for binary rings and literal tracedump text; state/ retains all isolated journals and copied profile/conflict state. Representative native stills and contact sheets are directly under build/astra3/.
- Validated readbacks: paced paragraphs; original find sequence hash unchanged; find fixture hash unchanged before intentional replacement; one/all/undo replacement counts and hashes; clipboard round trip; emoji save hashes; retained set-aside INSERT records with valid CRC32C.
- Remaining concerns on the tested build: wrapped wheel distance/reversal; unsupported New/Open/Save As/Quit and Find in Files; color-only emoji artwork missing; one unexplained renderer error near closing, not reproduced by the follow-up. Code remains monochrome; minor shortcut/gutter/duplicate-tab differences are documented separately.
- Invalid/limited evidence was not promoted to passes: initial Sublime update/safe-mode dialogs; standard large-file blank sequence; dense reference remaining at line 1; stale-window continuation; early wrong-grid/loading mouse attempts; early unestablished Open/Save-As/Quit reference dialogs; initial short rare-search observations while pending. Successful loaded/targeted repeats have distinct names.
- The analysis helpers' odd-crop, sparse-ROI, query-index, cursor-overlay and caret-phase issues were corrected or rejected as documented above. Copied harness integrity was rechecked: every copied script differs only in the output base (logs/harness-integrity.json).
- No source/tests/bench/fuzz/tools edits, no bd/git, no new build, no interaction on :0/:99, and no launch against the real sublimité profile. Only the requested virtual display and task/copied state were driven. The newer executable detected after capture is outside this verdict.
