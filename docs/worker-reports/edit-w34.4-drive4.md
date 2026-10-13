# Drive 4 finalisation — Astra

This file retains the interrupted driver’s execution/harness record below, followed by my finalisation notes. The finalisation is an offline evidence review; the prior execution bullets describe the earlier run, not actions taken now.

# edit-w34.4 drive 4 worker log

No bd/git, rebuilds, or source/test/tool edits. Evidence: build/astra4/video, frames, logs, traces, state.

## Start binary check
```text
2026-10-13T07:39:22+10:00
Not charging
1
0.64 0.42 0.21 2/1511 7
2026-10-13 05:36:44.598229658 +1000 9873912
ee77869520b159ca852f7baa53bd29d2a950cfb3c66b9c8c7e568cf97df7d169  build/astra4/sublimite
```

## Copied harness recursive diff
```diff
diff -r build/astra4/logs/harness-original/analysis_run.sh build/astra4/logs/harness-copied/analysis_run.sh
2,8c2,8
< python3 build/astra3/large_metrics.py sublimite > build/astra3/logs/large-analysis.txt 2>&1
< python3 build/astra3/analyze_new.py > build/astra3/logs/new-analysis.txt 2>&1
< python3 build/astra3/action_onsets.py > build/astra3/logs/action-analysis.txt 2>&1
< python3 build/astra3/font_video_metrics.py > build/astra3/logs/font-video-analysis.txt 2>&1
< python3 build/astra3/find_notice.py > build/astra3/logs/notice-analysis.txt 2>&1
< python3 build/astra3/large_gutters.py > build/astra3/logs/gutter-analysis.txt 2>&1
< python3 build/astra3/tab_metrics.py > build/astra3/logs/tab-analysis.txt 2>&1
---
> python3 build/astra4/large_metrics.py sublimite > build/astra4/logs/large-analysis.txt 2>&1
> python3 build/astra4/analyze_new.py > build/astra4/logs/new-analysis.txt 2>&1
> python3 build/astra4/action_onsets.py > build/astra4/logs/action-analysis.txt 2>&1
> python3 build/astra4/font_video_metrics.py > build/astra4/logs/font-video-analysis.txt 2>&1
> python3 build/astra4/find_notice.py > build/astra4/logs/notice-analysis.txt 2>&1
> python3 build/astra4/large_gutters.py > build/astra4/logs/gutter-analysis.txt 2>&1
> python3 build/astra4/tab_metrics.py > build/astra4/logs/tab-analysis.txt 2>&1
diff -r build/astra4/logs/harness-original/drive.py build/astra4/logs/harness-copied/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra3'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra4'
26c26
<   args=[str(ROOT/'build/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra4/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
57c57
<    p=sp.run([str(ROOT/'build/tools/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra4/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
diff -r build/astra4/logs/harness-original/execute.py build/astra4/logs/harness-copied/execute.py
14,15c14,15
<  with open('docs/reviews/astra-vs-sublime-3.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra3/logs/{self.stem}.json`. Visual analysis pending.\n')
<  with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
---
>  with open('docs/reviews/astra-vs-sublime-4.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra4/logs/{self.stem}.json`. Visual analysis pending.\n')
>  with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
diff -r build/astra4/logs/harness-original/final_audit.py build/astra4/logs/harness-copied/final_audit.py
3c3
< b=Path('build/astra3');r=[]
---
> b=Path('build/astra4');r=[]
diff -r build/astra4/logs/harness-original/gutter_validate.py build/astra4/logs/harness-copied/gutter_validate.py
3c3
< B=Path('build/astra3');out=[]
---
> B=Path('build/astra4');out=[]
diff -r build/astra4/logs/harness-original/metrics.py build/astra4/logs/harness-copied/metrics.py
5c5
< B=Path('build/astra3')
---
> B=Path('build/astra4')
diff -r build/astra4/logs/harness-original/new_tasks.py build/astra4/logs/harness-copied/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra4/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
108c108
<    with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
---
>    with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
diff -r build/astra4/logs/harness-original/report_final.py build/astra4/logs/harness-copied/report_final.py
3c3
< B=Path('build/astra3');P=Path('docs/reviews/astra-vs-sublime-3.md')
---
> B=Path('build/astra4');P=Path('docs/reviews/astra-vs-sublime-4.md')
66c66
< Driven binary: `stat -c '%y %s' build/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra3/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra4/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra3`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra4`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra3/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra3/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra4/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra4/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
diff -r build/astra4/logs/harness-original/scan_all.py build/astra4/logs/harness-copied/scan_all.py
3c3
< B=Path('build/astra3');r=[]
---
> B=Path('build/astra4');r=[]
diff -r build/astra4/logs/harness-original/scan_parallel.py build/astra4/logs/harness-copied/scan_parallel.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra4')
diff -r build/astra4/logs/harness-original/sheets.py build/astra4/logs/harness-copied/sheets.py
4c4
< B=Path('build/astra3')
---
> B=Path('build/astra4')
diff -r build/astra4/logs/harness-original/tasks.py build/astra4/logs/harness-copied/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra3/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra4/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra4/logs/harness-original/tasks_extra.py build/astra4/logs/harness-copied/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
diff -r build/astra4/logs/harness-original/timelines.py build/astra4/logs/harness-copied/timelines.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra4')
diff -r build/astra4/logs/harness-original/title_second.py build/astra4/logs/harness-copied/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
```

## Execution
- Captured `01-fresh-sublimite`; process endings recorded in task JSON.
- Captured `01-fresh-sublime`; process endings recorded in task JSON.
- Captured `02-typing-r2-sublimite`; process endings recorded in task JSON.
- Captured `02-typing-r2-sublime`; process endings recorded in task JSON.
- Captured `03-nav-sublimite`; process endings recorded in task JSON.
- Captured `03-nav-sublime`; process endings recorded in task JSON.
- Captured `03-selection-sublimite`; process endings recorded in task JSON.
- Captured `03-selection-sublime`; process endings recorded in task JSON.
- Captured `04-mouse-sublimite`; process endings recorded in task JSON.
- Captured `04-mouse-sublime`; process endings recorded in task JSON.
- Captured `05-edit-sublimite`; process endings recorded in task JSON.
- Captured `05-edit-sublime`; process endings recorded in task JSON.
- Captured `05-edit-r2-sublimite`; process endings recorded in task JSON.
- Captured `05-edit-r2-sublime`; process endings recorded in task JSON.
- Captured `06-clipboard-sublimite`; process endings recorded in task JSON.
- Captured `06-clipboard-sublime`; process endings recorded in task JSON.
- Captured `07-find-sublimite`; process endings recorded in task JSON.
- Captured `07-find-sublime`; process endings recorded in task JSON.
- Captured `08-save-sublimite`; process endings recorded in task JSON.
- Captured `08-save-sublime`; process endings recorded in task JSON.
- Captured `zoom-sublimite`; process endings recorded in task JSON.
- Captured `zoom-sublime`; process endings recorded in task JSON.
- Captured `font12-sublimite`; process endings recorded in task JSON.
- Captured `06-alternate-sublimite`; process endings recorded in task JSON.
- Captured `06-alternate-sublime`; process endings recorded in task JSON.

- Cross-editor source launch found no *new* window ID although the matching Sublime cross.txt window was visibly present. Preserved launch-only attempt; added path-title fallback to copied drive.py (permitted title matching adjustment) and resumed the unchanged sequence.
- Captured `06-cross-source-sublime`; process endings recorded in task JSON.
- Captured `06-cross-sublimite`; process endings recorded in task JSON.
- Captured `06-cross-back-sublime`; process endings recorded in task JSON.
- Captured `02-paced-sublimite`; process endings recorded in task JSON.
- Captured `02-paced-sublime`; process endings recorded in task JSON.
- Captured `02-paced-verified-sublimite`; process endings recorded in task JSON.
- Captured `02-paced-verified-sublime`; process endings recorded in task JSON.
- Captured `02-paced-verified-r2-sublime`; process endings recorded in task JSON.
- Captured `09-tabs-sublimite`; process endings recorded in task JSON.
- Captured `09-tabs-sublime`; process endings recorded in task JSON.
- Captured `10-large-sublimite`; process endings recorded in task JSON.
- Captured `10-large-sublime`; process endings recorded in task JSON.
- Captured `10-large-early-sublimite`; process endings recorded in task JSON.
- Captured `dense64-sublimite`; process endings recorded in task JSON.
- Captured `dense64-sublime`; process endings recorded in task JSON.
- Captured `11-resize-sublimite`; process endings recorded in task JSON.
- Captured `11-resize-sublime`; process endings recorded in task JSON.
- Captured `12-hotexit-sublimite`; process endings recorded in task JSON.
- Captured `12-hotexit-sublime`; process endings recorded in task JSON.
- Captured `12-recovery-conflict-sublimite`; process endings recorded in task JSON.
- Captured `13-minimap-sublimite`; process endings recorded in task JSON.
- Captured `13-minimap-sublime`; process endings recorded in task JSON.
- Captured `14-idle-sublimite`; process endings recorded in task JSON.
- Captured `14-idle-sublime`; process endings recorded in task JSON.
- Captured `utf8-sublimite`; process endings recorded in task JSON.
- Captured `utf8-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-wrapped-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-wrapped-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-calibrated-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-calibrated-sublime`; process endings recorded in task JSON.
- Captured `16-find-sublimite`; process endings recorded in task JSON.
- Captured `16-find-sublime`; process endings recorded in task JSON.
- Captured `17-recovery-a-sublimite`; process endings recorded in task JSON.

- INVALID Sublime captures using basename fallback: 06-cross-source (needs full-path recheck), 13-minimap, 14-idle, 15-mouse, 15-mouse-wrapped, 15-mouse-calibrated, 16-find, utf8, 17-recovery-a. Restored drive-3 windows can have the same basename. No save was sent during these affected sequences; old fixture byte hashes rechecked unchanged (logs/old-fixtures-integrity.json). The harness stopped itself at the task-17 Sublime relaunch timeout, before external-save or emoji-save tasks. Matcher now requires the complete requested absolute/tilde path and logs the actual title. Affected Sublime sequences are rerun under separately named recordings.
- Captured `06-cross-source-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `06-cross-fullpath-r2-sublimite`; process endings recorded in task JSON.
- Captured `06-cross-back-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `13-minimap-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `14-idle-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `utf8-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-wrapped-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-calibrated-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `16-find-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `17-recovery-a-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `17-recovery-b-sublimite`; process endings recorded in task JSON.
- Captured `17-recovery-c-sublimite`; process endings recorded in task JSON.
- Captured `17-recovery-d-sublimite`; process endings recorded in task JSON.
- Captured `17-profile-continuation-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-sublime`; process endings recorded in task JSON.
- Captured `18-tabtitle-second-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-second-sublime`; process endings recorded in task JSON.
- Captured `19-external-sublimite`; process endings recorded in task JSON.

- Task 17(e) second launch exceeded the inherited window discovery limit, then opened. Continuation recorded and WM_DELETE_WINDOW sent; original harness parent had exited, so that orphan process exit status is unavailable (not reported as zero). Its exit trace reports first_present elapsed_ms=68503.171250; larger profile contents differ from drive 3. A fresh cp -a audit with extended discovery follows.
- Task 19 Sublime original sequence had no visible progress after save in the recorded action window and empty clipboard reads; final xclip timed out. Its recorder PID 848187 was finalized with SIGINT. This is retained as an incomplete comparison, not a passing readback. Full-path title/focus verification is in its log.
- Captured `19-external-controlled-sublimite`; process endings recorded in task JSON.
- Captured `19-external-controlled-sublime`; process endings recorded in task JSON.
- Captured `19-external-repro-sublimite`; process endings recorded in task JSON.
- Captured `20-emoji-sublimite`; process endings recorded in task JSON.
- Captured `20-emoji-sublime`; process endings recorded in task JSON.
- Captured `20-emoji-carets-sublimite`; process endings recorded in task JSON.
- Captured `20-emoji-carets-sublime`; process endings recorded in task JSON.
- Captured `20-font-restore-sublime`; process endings recorded in task JSON.
- Captured `21-font-iosevka-sublimite`; process endings recorded in task JSON.
- Captured `21-font-liberation-mono-sublimite`; process endings recorded in task JSON.
- Captured `21-font-proportional-sublimite`; process endings recorded in task JSON.
- Captured `21-font-missing-sublimite`; process endings recorded in task JSON.
- Captured `21-font-file-sublimite`; process endings recorded in task JSON.
- Captured `22-common-commands-sublimite`; process endings recorded in task JSON.
- Captured `22-common-commands-fullpath-r2-sublime`; process endings recorded in task JSON.

- Complete-path matching was changed to a single X title query (including the optional dirty bullet), with explicit mapping of the exact match. A later inspection found the task-22 window already IsViewable; the cause of its earlier discovery timeout is not established, so “unmapped window” is not treated as a proven cause. Discovery now waits for the exact requested window while the launch is accepted, retaining the original polling interval and post-discovery key delays.
- Captured `15-mouse-target-ready-sublime`; process endings recorded in task JSON.
- Captured `22-sublime-font-baseline-sublime`; process endings recorded in task JSON.
- Captured `22-sublime-font-check-sublime`; process endings recorded in task JSON.
- Captured `16-original-sequence-sublimite`; process endings recorded in task JSON.
- Captured `16-original-sequence-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-keys-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-keys-sublime`; process endings recorded in task JSON.
- Captured `15-autoscroll-sublimite`; process endings recorded in task JSON.
- Captured `15-autoscroll-sublime`; process endings recorded in task JSON.
- Captured `24-files-sublimite`; process endings recorded in task JSON.
- Captured `24-files-sublime`; process endings recorded in task JSON.
- Captured `24-scroll-caret-sublimite`; process endings recorded in task JSON.
- Captured `24-scroll-caret-sublime`; process endings recorded in task JSON.
- Captured `25-findkeys-sublimite`; process endings recorded in task JSON.
- Captured `25-findkeys-sublime`; process endings recorded in task JSON.
- Captured `26-reopen-sublimite`; process endings recorded in task JSON.
- Captured `26-reopen-sublime`; process endings recorded in task JSON.
- Captured `27-syntax-c-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-rs-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-c-sublime`; process endings recorded in task JSON.
- Captured `27-syntax-rs-sublime`; process endings recorded in task JSON.
- Captured `27-tokens-c-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-rs-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-c-sublime`; process endings recorded in task JSON.
- Captured `27-tokens-rs-sublime`; process endings recorded in task JSON.
- Captured `27-light-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-sublime`; process endings recorded in task JSON.
- Captured `29-stall-sublimite`; process endings recorded in task JSON.
- Captured `29-stall-sublime`; process endings recorded in task JSON.
- Captured `30-external-x3-1-sublimite`; process endings recorded in task JSON.
- Captured `30-external-x3-2-sublimite`; process endings recorded in task JSON.
- Captured `30-external-x3-3-sublimite`; process endings recorded in task JSON.
- Captured `30-dirty-branch-sublimite`; process endings recorded in task JSON.
- Captured `30-external-x3-1-sublime`; process endings recorded in task JSON.
- Captured `30-dirty-branch-sublime`; process endings recorded in task JSON.
- Captured `31-large-index-sublimite`; process endings recorded in task JSON.
- Captured `31-large-queued-sublimite`; process endings recorded in task JSON.
- Captured `31-large-index-sublime`; process endings recorded in task JSON.
- Captured `32-crash-undo-0-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-undo-1-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-undo-2-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-undo-4-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-undo-8-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-untitled-sublimite`; process endings recorded in task JSON.
- Captured `32-crash-two-sublimite`; process endings recorded in task JSON.
- Captured `33-crlf-sublimite`; process endings recorded in task JSON.
- Captured `33-save-fail-sublimite`; process endings recorded in task JSON.
- Captured `33-glyphs-sublimite`; process endings recorded in task JSON.
- Captured `33-crlf-sublime`; process endings recorded in task JSON.
- Captured `33-save-fail-sublime`; process endings recorded in task JSON.
- Captured `33-glyphs-sublime`; process endings recorded in task JSON.
- Captured `17-profile-audit-sublimite`; process endings recorded in task JSON.
- Captured `16-find-large-sublimite`; process endings recorded in task JSON.
- Captured `16-find-large-wait-sublimite`; process endings recorded in task JSON.
- Captured `19-external-r2-sublime`; process endings recorded in task JSON.
- Captured `25-findkeys-continuous-sublimite`; process endings recorded in task JSON.
- Captured `25-findkeys-continuous-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-r2-sublime`; process endings recorded in task JSON.
- Captured `31-early-end-only-sublimite`; process endings recorded in task JSON.
- Captured `27-end-page-sublimite`; process endings recorded in task JSON.
- Captured `27-end-page-sublime`; process endings recorded in task JSON.
- Captured `10-large-loaded-sublime`; process endings recorded in task JSON.
- Captured `16-find-large-wait-sublime`; process endings recorded in task JSON.
- Captured `31-large-loaded-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-target-ready-r2-sublime`; process endings recorded in task JSON.
- Captured `26-reopen-r2-sublime`; process endings recorded in task JSON.
- Captured `28-gutter-r2-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-r2-sublime`; process endings recorded in task JSON.
- Captured `26-reopen-r3-sublime`; process endings recorded in task JSON.
- Captured `28-gutter-ready-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-late-sublime`; process endings recorded in task JSON.

- Automatic approval review timed out before starting followups4.py; permitted retry succeeded. No approval-related action remains blocked.
- One final flash-analysis execution session ended with status 143, sender/cause unknown. Retrying only missing scans in a separate process session; no editor failure inferred.

## End of captures binary check
```text
2026-10-13T10:09:08+10:00
Discharging
0
1.37 2.26 3.46 14/1719 6
2026-10-13 05:36:44.598229658 +1000 9873912
ee77869520b159ca852f7baa53bd29d2a950cfb3c66b9c8c7e568cf97df7d169  build/astra4/sublimite
```

- Clock audit found a backward realtime discontinuity inside 16-find-large-wait-sublime (logs/clock-discontinuities.json); ffmpeg stopped advancing media time and dropped later frames. Later F3 timing from this recording is invalid. No clock-setting command was run by this drive. Subsequent wrapper marks include monotonic_ns as well as realtime; repeat capture required for affected timings. Power switched to battery for late captures; report uses each task’s actual AC/online stamp, not analysis helpers’ inherited hard-coded AC field.
- Captured `16-find-large-stable-sublime`; process endings recorded in task JSON.
- Captured `28-gutter-cells-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-cells-sublime`; process endings recorded in task JSON.


## Finalisation review (this run)

Reviewed `build/astra/brief4.md`, the drive-3 report and its recorded task load stamps, the complete draft drive-4 report, BUILD and binary checkpoint logs, the capture/run inventory, per-capture JSON command/power/readback records, existing metrics, flash detector outputs, extracted video frames/contact sheets, and the saved text trace dumps. I did not launch either editor, capture new video/screenshots, run git or bd, rebuild, change the harness, or modify source/tests/bench/fuzz/tools or previous-drive evidence.

The directory inventory contains 179 MKVs: 177 have capture logs and two are derived moving-minimap videos. I reviewed the saved analytical products and sampled decisive frames rather than claiming a fresh frame-by-frame replay of every movie. Per-capture stamps come from AC/online and loadavg in the JSON, including late battery captures whose helper text wrongly says AC. Mtimes were not used to reconstruct order. The interrupted backwards-clock search recording is excluded from late timing/F3 claims; the already-recorded stable repeat supplies its rare-match measurement.

Completed the task tables, merged continued sections into numerical order, replaced every capture-progress placeholder with a disposition ledger, added syntax RGB/ROI measurements, valid onset inventories, raw early-index OCR samples with uncertainty, per-video flash counts, explicit drive-3 finding/blocker dispositions, missing features, defects, and literal latency/startup trace blocks. The report ends with the required verdict format.

Independently hashed the saved recovery readbacks: all five file-undo results and the untitled result are the complete fixture; all saved two-instance relaunch pairs contain both expected marker prefixes. Checked continuous replacement/undo hashes and all repeated external reload/keep hashes. Checked flash coverage against every sublimité video and searched saved editor stderr for renderer error -11/assertion/segmentation-fault text; none appeared in those saved logs. No mid-drain kill timing or unsampled cache glyph correctness is inferred.

Corrected substantive draft overclaims: original-sequence Sublime hash readback is empty; stale idle measurements are replaced by the full-path repeat; Sublime gutter drag copies only the clicked line in the final cell-centre capture; late inactive-file readback is a newline from the wrong visible tab; alias reopening is not identical in Sublime; blank final loaded-index frames do not prove paging completion. The task tables and ledger retain these as NOT OBSERVED or explicitly unequal, not passes. Other gaps: exact estimate-to-index transition, individual early queued-key paints, settled Sublime wrapped reversal, GL stall path, optional language probes, top-edge autoscroll, and demonstrable mid-journal-drain interruption.

The original worker’s harness diff, capture failures and process-status limits are preserved above. Finalisation used only temporary analysis scripts/contact sheets under `/tmp`; it wrote the two requested Markdown reports directly. `build/astra4/final-message.md` was not used as a report output. No approval remained blocked and no new recording was attempted.

Timing: finalisation started at 2026-10-13 05:48:48 UTC according to the clock tool. Completion timestamp and elapsed duration are appended after validation; the prior interrupted capture run’s elapsed duration is not reconstructed from unreliable mtimes.

Validation completed at 2026-10-13 06:03:12 UTC: approximately 14 minutes 24 seconds from the initial clock-tool stamp. This duration describes this finalisation only. Validation confirmed all task sections 1–33, an allowed disposition for every task row, no remaining “Analysis pending” placeholders, existence of every explicitly named video, complete saved sublimité flash-scan coverage, and one final verdict line. Final frame checks also confirmed matching Sublime/runtime find-position sequences and the runtime case/whole-word/regex totals. The final report and this worker log are the only workspace files written by this finalisation.

Final verdict: no; ranked remaining blockers are missing find in files (edit-xkd), then missing emoji artwork (edit-r8e). Previously recorded daily file-command, wrapped-wheel, and raster-exit blockers are resolved in the observed probes.
