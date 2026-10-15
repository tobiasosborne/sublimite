# edit-w34.4 drive 5 worker log

No bd/git or product rebuilds; no edits under src/tests/bench/fuzz/tools. Older evidence is read-only. Evidence: build/astra5/video, frames, logs, traces, files, state. Report written by direct file writes, independent of final-message.md.

## Binary start
```text
2026-10-16T05:26:41+11:00
Full
1
0.57 0.68 0.73 2/2488 925887
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite
```

## Harness diff against drive 4
```diff
diff -r build/astra5/logs/harness-original/analysis_run.sh build/astra5/logs/harness-copied/analysis_run.sh
2,8c2,8
< python3 build/astra4/large_metrics.py sublimite > build/astra4/logs/large-analysis.txt 2>&1
< python3 build/astra4/analyze_new.py > build/astra4/logs/new-analysis.txt 2>&1
< python3 build/astra4/action_onsets.py > build/astra4/logs/action-analysis.txt 2>&1
< python3 build/astra4/font_video_metrics.py > build/astra4/logs/font-video-analysis.txt 2>&1
< python3 build/astra4/find_notice.py > build/astra4/logs/notice-analysis.txt 2>&1
< python3 build/astra4/large_gutters.py > build/astra4/logs/gutter-analysis.txt 2>&1
< python3 build/astra4/tab_metrics.py > build/astra4/logs/tab-analysis.txt 2>&1
---
> python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
> python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
> python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
> python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
> python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
> python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
> python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
diff -r build/astra5/logs/harness-original/drive.py build/astra5/logs/harness-copied/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra4'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
26c26
<   args=[str(ROOT/'build/astra4/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
66c66
<    p=sp.run([str(ROOT/'build/astra4/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
diff -r build/astra5/logs/harness-original/drive4_wrapper.py build/astra5/logs/harness-copied/drive4_wrapper.py
6c6
< REPORT=Path('docs/reviews/astra-vs-sublime-4.md'); WORK=Path('docs/worker-reports/edit-w34.4-drive4.md')
---
> REPORT=Path('docs/reviews/astra-vs-sublime-5.md'); WORK=Path('docs/worker-reports/edit-w34.4-drive5.md')
10c10
<  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra4/sublimite\nsha256sum build/astra4/sublimite"],capture_output=True,text=True).stdout
---
>  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
72c72
<  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra4/logs/{self.stem}.json`.\n")
---
>  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
diff -r build/astra5/logs/harness-original/execute.py build/astra5/logs/harness-copied/execute.py
14,15c14,15
<  with open('docs/reviews/astra-vs-sublime-4.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra4/logs/{self.stem}.json`. Visual analysis pending.\n')
<  with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
---
>  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
>  with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
diff -r build/astra5/logs/harness-original/final_audit.py build/astra5/logs/harness-copied/final_audit.py
3c3
< b=Path('build/astra4');r=[]
---
> b=Path('build/astra5');r=[]
12c12
< p=Path('docs/reviews/astra-vs-sublime-4.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
---
> p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
39c39
< audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive4.md'],missing_video_references=missing)
---
> audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive5.md'],missing_video_references=missing)
41c41
< wp=Path('docs/worker-reports/edit-w34.4-drive4.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
---
> wp=Path('docs/worker-reports/edit-w34.4-drive5.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
diff -r build/astra5/logs/harness-original/gutter_validate.py build/astra5/logs/harness-copied/gutter_validate.py
3c3
< B=Path('build/astra4');out=[]
---
> B=Path('build/astra5');out=[]
diff -r build/astra5/logs/harness-original/metrics.py build/astra5/logs/harness-copied/metrics.py
5c5
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/new4.py build/astra5/logs/harness-copied/new4.py
3c3
< BIN=ROOT/'build/astra4/sublimite'
---
> BIN=ROOT/'build/astra5/sublimite'
diff -r build/astra5/logs/harness-original/new_tasks.py build/astra5/logs/harness-copied/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra4/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
108c108
<    with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
---
>    with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
diff -r build/astra5/logs/harness-original/profile_continue4.py build/astra5/logs/harness-copied/profile_continue4.py
19c19
< p=sp.run([str(ROOT/'build/astra4/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
---
> p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
diff -r build/astra5/logs/harness-original/report_final.py build/astra5/logs/harness-copied/report_final.py
3c3
< B=Path('build/astra4');P=Path('docs/reviews/astra-vs-sublime-4.md')
---
> B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
66c66
< Driven binary: `stat -c '%y %s' build/astra4/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra4/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra4`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra4/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra4/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
diff -r build/astra5/logs/harness-original/report_rows.py build/astra5/logs/harness-copied/report_rows.py
3c3
< B=Path('build/astra4'); R=Path('docs/reviews/astra-vs-sublime-4.md')
---
> B=Path('build/astra5'); R=Path('docs/reviews/astra-vs-sublime-5.md')
diff -r build/astra5/logs/harness-original/scan_all.py build/astra5/logs/harness-copied/scan_all.py
3c3
< B=Path('build/astra4');r=[]
---
> B=Path('build/astra5');r=[]
diff -r build/astra5/logs/harness-original/scan_parallel.py build/astra5/logs/harness-copied/scan_parallel.py
3c3
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/sheets.py build/astra5/logs/harness-copied/sheets.py
4c4
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/tasks.py build/astra5/logs/harness-copied/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra4/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra5/logs/harness-original/tasks_extra.py build/astra5/logs/harness-copied/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
diff -r build/astra5/logs/harness-original/timelines.py build/astra5/logs/harness-copied/timelines.py
3c3
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/title_second.py build/astra5/logs/harness-copied/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
diff -r build/astra5/logs/harness-original/token_specs.py build/astra5/logs/harness-copied/token_specs.py
3c3
< B=Path('build/astra4');out=[]
---
> B=Path('build/astra5');out=[]
```

## Harness diff against drive 3
```diff
diff -r build/astra5/logs/harness-drive3/analysis_run.sh build/astra5/logs/harness-copied/analysis_run.sh
2,8c2,8
< python3 build/astra3/large_metrics.py sublimite > build/astra3/logs/large-analysis.txt 2>&1
< python3 build/astra3/analyze_new.py > build/astra3/logs/new-analysis.txt 2>&1
< python3 build/astra3/action_onsets.py > build/astra3/logs/action-analysis.txt 2>&1
< python3 build/astra3/font_video_metrics.py > build/astra3/logs/font-video-analysis.txt 2>&1
< python3 build/astra3/find_notice.py > build/astra3/logs/notice-analysis.txt 2>&1
< python3 build/astra3/large_gutters.py > build/astra3/logs/gutter-analysis.txt 2>&1
< python3 build/astra3/tab_metrics.py > build/astra3/logs/tab-analysis.txt 2>&1
---
> python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
> python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
> python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
> python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
> python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
> python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
> python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
Only in build/astra5/logs/harness-copied: analyze4.py
Only in build/astra5/logs/harness-copied: common_retry4.py
Only in build/astra5/logs/harness-copied: crash4.py
diff -r build/astra5/logs/harness-drive3/drive.py build/astra5/logs/harness-copied/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra3'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
26c26
<   args=[str(ROOT/'build/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
31c31
<   for i in range(200):
---
>   while True:
32a33,40
>    if self.editor=='sublime':
>     wanted=[str(Path(path).resolve()),str(Path(path).resolve()).replace(str(Path.home()),'~',1)]
>     pattern='^('+'|'.join(re.escape(q) for q in wanted)+')( •)? - Sublime Text'
>     found=sp.run(['xdotool','search','--name',pattern],capture_output=True,text=True).stdout.split()
>     if found:
>      title=sp.run(['xdotool','getwindowname',found[-1]],capture_output=True,text=True).stdout.strip()
>      self.mark('verified_full_path_title',window=found[-1],title=title)
>      self.x('windowmap',found[-1])
33a42
>    if self.proc.poll() not in (None,0):break
57c66
<    p=sp.run([str(ROOT/'build/tools/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
Only in build/astra5/logs/harness-copied: drive4_wrapper.py
Only in build/astra5/logs/harness-copied: end_only4.py
diff -r build/astra5/logs/harness-drive3/execute.py build/astra5/logs/harness-copied/execute.py
14,15c14,15
<  with open('docs/reviews/astra-vs-sublime-3.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra3/logs/{self.stem}.json`. Visual analysis pending.\n')
<  with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
---
>  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
>  with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
Only in build/astra5/logs/harness-copied: extra_metrics4.py
Only in build/astra5/logs/harness-copied: file_metrics4.py
Only in build/astra5/logs/harness-copied: files4.py
diff -r build/astra5/logs/harness-drive3/final_audit.py build/astra5/logs/harness-copied/final_audit.py
3c3
< b=Path('build/astra3');r=[]
---
> b=Path('build/astra5');r=[]
12c12
< p=Path('docs/reviews/astra-vs-sublime-2.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
---
> p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
39c39
< audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive2.md'],missing_video_references=missing)
---
> audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive5.md'],missing_video_references=missing)
41c41
< wp=Path('docs/worker-reports/edit-w34.4-drive2.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
---
> wp=Path('docs/worker-reports/edit-w34.4-drive5.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
Only in build/astra5/logs/harness-copied: finish_original.py
Only in build/astra5/logs/harness-copied: finish_original_rest.py
Only in build/astra5/logs/harness-copied: finish_original_tail.py
Only in build/astra5/logs/harness-copied: first_batch.py
Only in build/astra5/logs/harness-copied: followups4.py
Only in build/astra5/logs/harness-copied: font_metrics_gated4.py
Only in build/astra5/logs/harness-copied: font_normalize4.py
Only in build/astra5/logs/harness-copied: gutter_cells4.py
Only in build/astra5/logs/harness-copied: gutter_retry4.py
diff -r build/astra5/logs/harness-drive3/gutter_validate.py build/astra5/logs/harness-copied/gutter_validate.py
3c3
< B=Path('build/astra3');out=[]
---
> B=Path('build/astra5');out=[]
Only in build/astra5/logs/harness-copied: idle_r2_metrics.py
Only in build/astra5/logs/harness-copied: index_frames4.py
Only in build/astra5/logs/harness-copied: index_metrics4.py
Only in build/astra5/logs/harness-copied: large4.py
Only in build/astra5/logs/harness-copied: last_ready4.py
Only in build/astra5/logs/harness-copied: last_ui4.py
Only in build/astra5/logs/harness-copied: late_valid4.py
Only in build/astra5/logs/harness-copied: loaded_follow4.py
Only in build/astra5/logs/harness-copied: loaded_index4.py
diff -r build/astra5/logs/harness-drive3/metrics.py build/astra5/logs/harness-copied/metrics.py
5c5
< B=Path('build/astra3')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-copied: new4.py
Only in build/astra5/logs/harness-copied: new_batch.py
diff -r build/astra5/logs/harness-drive3/new_tasks.py build/astra5/logs/harness-copied/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
108c108
<    with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
---
>    with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
Only in build/astra5/logs/harness-copied: original_readback4.py
Only in build/astra5/logs/harness-copied: profile4.py
Only in build/astra5/logs/harness-copied: profile_continue4.py
Only in build/astra5/logs/harness-copied: rare_stable_metrics4.py
Only in build/astra5/logs/harness-copied: remaining_batch.py
Only in build/astra5/logs/harness-copied: remaining_resume.py
Only in build/astra5/logs/harness-copied: reopen_retry3_4.py
Only in build/astra5/logs/harness-copied: reopen_retry4.py
Only in build/astra5/logs/harness-copied: repair_comparisons.py
diff -r build/astra5/logs/harness-drive3/report_final.py build/astra5/logs/harness-copied/report_final.py
3c3
< B=Path('build/astra3');P=Path('docs/reviews/astra-vs-sublime-3.md')
---
> B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
66c66
< Driven binary: `stat -c '%y %s' build/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra3/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra3`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra3/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra3/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
Only in build/astra5/logs/harness-copied: report_new_rows.py
Only in build/astra5/logs/harness-copied: report_rows.py
diff -r build/astra5/logs/harness-drive3/scan_all.py build/astra5/logs/harness-copied/scan_all.py
3c3
< B=Path('build/astra3');r=[]
---
> B=Path('build/astra5');r=[]
diff -r build/astra5/logs/harness-drive3/scan_parallel.py build/astra5/logs/harness-copied/scan_parallel.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-copied: scroll_caret4.py
Only in build/astra5/logs/harness-copied: search_stable4.py
diff -r build/astra5/logs/harness-drive3/sheets.py build/astra5/logs/harness-copied/sheets.py
4c4
< B=Path('build/astra3')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-drive3/sublime_loaded.py build/astra5/logs/harness-copied/sublime_loaded.py
5c5
<  wins=d.windows()
---
>  wins=[w for w in d.windows() if '/tmp/edit-corpus/log_1g.txt - Sublime Text' in sp.run(['xdotool','getwindowname',w],capture_output=True,text=True).stdout]
Only in build/astra5/logs/harness-copied: syntax_metrics4.py
Only in build/astra5/logs/harness-copied: target_ready_r2.py
diff -r build/astra5/logs/harness-drive3/tasks.py build/astra5/logs/harness-copied/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra3/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra5/logs/harness-drive3/tasks_extra.py build/astra5/logs/harness-copied/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
diff -r build/astra5/logs/harness-drive3/timelines.py build/astra5/logs/harness-copied/timelines.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-drive3/title_second.py build/astra5/logs/harness-copied/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
Only in build/astra5/logs/harness-copied: token_rgb4.py
Only in build/astra5/logs/harness-copied: token_specs.py
Only in build/astra5/logs/harness-copied: tokens4.py
Only in build/astra5/logs/harness-copied: wrap_metrics4.py
Only in build/astra5/logs/harness-copied: wrap_onsets4.py
Only in build/astra5/logs/harness-copied: wrapped_reverse4.py
```

## Runs
New drive5_wrapper.py updates the pinned digest/stat and adds astra4 to the old-evidence focus guard; inherited wrapper is retained. Power stamps, monotonic command timestamps and per-run process endings are inherited.
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

Sublime standard-session launch of 06-alternate did not produce the requested window; harness and recorder stopped with SIGINT, editor left alone. This capture is NOT OBSERVED, not a product regression. Subsequent comparisons will explicitly identify clean-session use.

The pending Sublime window appeared during diagnosis. A --safe-mode request was issued, but a distinct clean server/profile was not established; do not label subsequent standard launches as clean. Failed waiting capture retained as 06-alternate-wait-sublime. Resume uses the unchanged standard launch and inherited alternate-clipboard keys.
- Captured `06-alternate-sublime`; process endings recorded in task JSON.
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

Offline large_metrics.py first attempt ended with status 143 and no output; cause unestablished. This was an analysis process, not an editor exit. Retrying against the same retained video; no new input capture.
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

Task 15 wrapped reversal candidate invalidated: delayed Sublime navigation window 4282021 overlaid the left test area; final runtime focus check disagrees with owned 6291457. Left rectangle is frozen through later updates. Keep raw evidence, repeat isolated; do not file a runtime regression from this capture.
- Captured `17-recovery-a-sublimite`; process endings recorded in task JSON.

Original task 17(a) Sublime relaunch failed to discover a window; batch stopped. Its completed pre-kill and failed relaunch capture/log retained. Runtime 17(a) completed. Resume from runtime 17(b), preserving all completed evidence.
- Captured `17-recovery-b-sublimite`; process endings recorded in task JSON.
- Captured `17-recovery-c-sublimite`; process endings recorded in task JSON.
- Captured `17-recovery-d-sublimite`; process endings recorded in task JSON.
- Captured `17-recovery-e-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-sublime`; process endings recorded in task JSON.
- Captured `18-tabtitle-second-sublimite`; process endings recorded in task JSON.
- Captured `18-tabtitle-second-sublime`; process endings recorded in task JSON.
- Captured `19-external-sublimite`; process endings recorded in task JSON.

Task 19 Sublime external readback timed out in xclip after the inherited timeout. Incomplete recorder stopped with SIGINT. Added new-wrapper finally cleanup for future incomplete captures (no changes to task keys/delays). Resume begins with external_controlled.py.
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
- Captured `22-common-attached-sublime`; process endings recorded in task JSON.

Task 22 standard Sublime fixture remained unpainted during discovery. Waiting harness interrupted with SIGINT; wrapper finally stopped recorder. Supplemental 22-common-attached replays the same command keys on identified visible Sublime window then normal Ctrl+Q. Remaining runtime probes are sequenced separately to avoid delayed comparator windows intercepting input. No editor kill used for this cleanup.
- Captured `16-original-sequence-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-keys-sublimite`; process endings recorded in task JSON.
- Captured `15-autoscroll-sublimite`; process endings recorded in task JSON.

Prepared (not yet launched) a private clean Sublime XDG profile under build/astra5/state/sublime-clean, copying the real User Preferences byte-for-byte. A task-local subl launcher scopes config/cache/data homes and preserves arguments. Intended fallback: retain build 4200/font-size 11/Mariana and exact action keys without restoring prior-drive sessions. Actual process/profile isolation must be verified before using its evidence. Real Sublime profile is not modified by this preparation.
- Captured `24-files-sublimite`; process endings recorded in task JSON.
- Captured `24-scroll-caret-sublimite`; process endings recorded in task JSON.
- Captured `25-findkeys-sublimite`; process endings recorded in task JSON.
- Captured `26-reopen-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-c-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-rs-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-c-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-rs-sublimite`; process endings recorded in task JSON.
- Captured `27-light-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-sublimite`; process endings recorded in task JSON.

Added hide_old_sublime5.py: a :98-only Xlib guard unmaps visible top-level windows only when _NET_WM_PID equals the identified old comparator PID 998510. No process signals or other displays. Guard actions logged in logs/window-guard.jsonl. Required because late unpainted restored windows froze/intercepted the left capture rectangle even without changing keyboard focus. Task-23 reversal before guard is invalid and will be repeated; no runtime regression claimed from it.
- Captured `29-stall-sublimite`; process endings recorded in task JSON.

Guard startup found PID 998510 already absent: the pending normal Quit had completed. The guard exited before opening the display and made NO window changes. No old Sublime process remains (host pgrep check). Repeat the affected wrapped captures after this point; prepared private comparator profile will avoid restoring old drive windows.
- Captured `30-external-x3-1-sublimite`; process endings recorded in task JSON.
- Captured `30-external-x3-2-sublimite`; process endings recorded in task JSON.
- Captured `30-external-x3-3-sublimite`; process endings recorded in task JSON.
- Captured `30-dirty-branch-sublimite`; process endings recorded in task JSON.

Prepared runtime_repeat5.py for tasks 23–28 plus affected wrapped/mouse probes after the old comparator exited. The same inherited functions/keys/delays are replayed with an -r2 task/output/state suffix; original evidence retained. Needed because root stills in task 24 also contain a stale rectangular overlay, while disk hashes and readbacks remain independently valid.
- Captured `31-large-index-sublimite`; process endings recorded in task JSON.
- Captured `31-large-queued-sublimite`; process endings recorded in task JSON.
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
- Captured `17-profile-audit-sublimite`; process endings recorded in task JSON.
- Captured `34-emoji-sublimite`; process endings recorded in task JSON.
- Captured `35-whitespace-default-sublimite`; process endings recorded in task JSON.
- Captured `35-whitespace-lines-sublimite`; process endings recorded in task JSON.
- Captured `35-whitespace-all-sublimite`; process endings recorded in task JSON.
- Captured `35-whitespace-selection-sublimite`; process endings recorded in task JSON.
- Captured `36-orphan-fixture-sublimite`; process endings recorded in task JSON.

Task 36 original old-binary recipe produced an 8192-byte journal after the prescribed dwells, so it could not exercise the oversized gate. The old binary hash still matches drive 4. Followed tests/editor_session_test.c fm4_orphan construction in a build/astra5-only headless helper: pending open settles, inject x, invalidate origin, flush/checkpoint, exit without normal close. Linked existing current build/libedit.a (provenance logged); this is fixture generation only, not a substitute for driving build/astra5/sublimite. Staged UI binary is unchanged. Fallback produced a journal above the threshold; private BASE then changed.
- Captured `36-orphan-sublimite`; process endings recorded in task JSON.
- Captured `36-orphan-repeat-sublimite`; process endings recorded in task JSON.

Task 36 fallback completed; oversized generated orphan and sparse source deleted after repeated-launch hash verification. Before each new-task-36 recording, root background was set black to exclude stale old-window text from first-painted-frame detection.

**NEW BLOCKER (task 37; edit-28u test scope, cause not established):** after the dense64 growth run closes normally, no-argument relaunch with the same private profile exits 1 with error -103 and no window. Corpus stat is unchanged; the journal is retained under a set-aside name. Preserving and independently relaunching a copy. Growth itself remains small. This is new coverage, not yet proof of a regression from drive 4.
- Captured `37-dense-restart-check-sublimite`; process endings recorded in task JSON.
- Captured `37-checkpoint-growth-log1g-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-r2-sublimite`; process endings recorded in task JSON.
- Captured `23-wrapped-keys-r2-sublimite`; process endings recorded in task JSON.
- Captured `15-autoscroll-r2-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-r2-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-wrapped-r2-sublimite`; process endings recorded in task JSON.
- Captured `24-files-r2-sublimite`; process endings recorded in task JSON.
- Captured `24-scroll-caret-r2-sublimite`; process endings recorded in task JSON.
- Captured `25-findkeys-r2-sublimite`; process endings recorded in task JSON.
- Captured `26-reopen-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-c-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-syntax-rs-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-c-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-tokens-rs-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-light-r2-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-r2-sublimite`; process endings recorded in task JSON.
- Captured `28-gutter-cells-r2-sublimite`; process endings recorded in task JSON.
- Captured `31-early-end-only-r2-sublimite`; process endings recorded in task JSON.
- Captured `27-end-page-r2-sublimite`; process endings recorded in task JSON.
- Captured `sublime-clean-setup-sublime`; process endings recorded in task JSON.
- Captured `sublime-clean-dialog-sublime`; process endings recorded in task JSON.

**Clean comparator deviation:** after the prescribed standard-profile runs repeatedly restored old drive windows and produced blank/intercepted captures, remaining/repeated Sublime comparisons use a private XDG config/cache/data profile under build/astra5/state/sublime-clean. User Preferences were copied byte-for-byte (font_size 11; default Mariana). Process environment and locally created session file verify isolation. Build remains 4200; update offer cancelled. Action keys/delays/coordinates remain the inherited functions. Failed originals are retained and excluded where invalid; private-profile evidence is labelled in Conditions, not represented as Tobias’s accumulated real-session performance.
- Captured `22-common-commands-clean-sublime`; process endings recorded in task JSON.
- Captured `06-cross-source-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `06-cross-fullpath-r2-sublimite`; process endings recorded in task JSON.
- Captured `06-cross-back-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `13-minimap-fullpath-r2-sublime`; process endings recorded in task JSON.

Private comparator only: disabled update_check after cancelling the visible offer, to retain build 4200 across the required quit/relaunch probes. Font size and colour-scheme choice remain the copied User/default settings; Tobias’s real Preferences file is unchanged.
- Captured `14-idle-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `utf8-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-wrapped-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-calibrated-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `16-find-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `17-recovery-a-fullpath-r2-sublime`; process endings recorded in task JSON.
- Captured `15-mouse-target-ready-r2-sublime`; process endings recorded in task JSON.
- Captured `16-original-sequence-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-keys-sublime`; process endings recorded in task JSON.
- Captured `15-autoscroll-sublime`; process endings recorded in task JSON.
- Captured `24-files-sublime`; process endings recorded in task JSON.
- Captured `24-scroll-caret-sublime`; process endings recorded in task JSON.
- Captured `25-findkeys-sublime`; process endings recorded in task JSON.
- Captured `26-reopen-sublime`; process endings recorded in task JSON.
- Captured `27-syntax-c-sublime`; process endings recorded in task JSON.
- Captured `27-syntax-rs-sublime`; process endings recorded in task JSON.
- Captured `27-tokens-c-sublime`; process endings recorded in task JSON.
- Captured `27-tokens-rs-sublime`; process endings recorded in task JSON.
- Captured `28-gutter-sublime`; process endings recorded in task JSON.
- Captured `28-gutter-cells-sublime`; process endings recorded in task JSON.
- Captured `29-stall-sublime`; process endings recorded in task JSON.
- Captured `30-external-x3-1-sublime`; process endings recorded in task JSON.
- Captured `30-dirty-branch-sublime`; process endings recorded in task JSON.
- Captured `33-crlf-sublime`; process endings recorded in task JSON.
- Captured `33-save-fail-sublime`; process endings recorded in task JSON.
- Captured `33-glyphs-sublime`; process endings recorded in task JSON.
- Captured `19-external-r2-sublime`; process endings recorded in task JSON.
- Captured `25-findkeys-continuous-sublime`; process endings recorded in task JSON.
- Captured `23-wrapped-reverse-r2-sublime`; process endings recorded in task JSON.
- Captured `27-end-page-sublime`; process endings recorded in task JSON.
- Captured `34-emoji-sublime`; process endings recorded in task JSON.
- Captured `35-whitespace-default-sublime`; process endings recorded in task JSON.
- Captured `31-large-index-sublime`; process endings recorded in task JSON.

After new task 34 left the private comparator at font_size 13, restored its private setting to the initial 11 before the loaded large-file comparison. The preceding private task-31 first-launch attempt is retained at its actual geometry and marked text-readiness timeout; no per-key result inferred. Real User Preferences remain at 11.
- Captured `10-large-clean-sublime`; process endings recorded in task JSON.
- Captured `10-large-loaded-sublime`; process endings recorded in task JSON.
- Captured `16-find-large-wait-sublime`; process endings recorded in task JSON.
- Captured `31-large-loaded-sublime`; process endings recorded in task JSON.
- Captured `16-find-large-sublimite`; process endings recorded in task JSON.
- Captured `16-find-large-wait-sublimite`; process endings recorded in task JSON.
- Captured `25-findkeys-continuous-sublimite`; process endings recorded in task JSON.
- Captured `15-mouse-target-ready-r3-sublime`; process endings recorded in task JSON.
- Captured `02-paired-sublimite`; process endings recorded in task JSON.
- Captured `02-paired-sublime`; process endings recorded in task JSON.
- Captured `10-paired-sublimite`; process endings recorded in task JSON.
- Captured `10-paired-sublime`; process endings recorded in task JSON.
- Captured `34-fixture-r2-sublimite`; process endings recorded in task JSON.
- Captured `34-emoji-r2-sublimite`; process endings recorded in task JSON.
- Captured `34-fixture-r2-sublime`; process endings recorded in task JSON.
- Captured `34-emoji-r2-sublime`; process endings recorded in task JSON.

## Final harness audit

Inherited task scripts retain keys, delays, coordinates and fixture contents. New wrappers/runners add binary checks, process cleanup, explicit capture suffixes, fixture generation and analysis; private comparator profiles and all corrected/failed attempts are documented above. The final script snapshot includes the additional drive-5 helpers.

### Final recursive diff against drive 4

```diff
diff -r build/astra5/logs/harness-original/analysis_run.sh build/astra5/logs/harness-final/analysis_run.sh
2,8c2,8
< python3 build/astra4/large_metrics.py sublimite > build/astra4/logs/large-analysis.txt 2>&1
< python3 build/astra4/analyze_new.py > build/astra4/logs/new-analysis.txt 2>&1
< python3 build/astra4/action_onsets.py > build/astra4/logs/action-analysis.txt 2>&1
< python3 build/astra4/font_video_metrics.py > build/astra4/logs/font-video-analysis.txt 2>&1
< python3 build/astra4/find_notice.py > build/astra4/logs/notice-analysis.txt 2>&1
< python3 build/astra4/large_gutters.py > build/astra4/logs/gutter-analysis.txt 2>&1
< python3 build/astra4/tab_metrics.py > build/astra4/logs/tab-analysis.txt 2>&1
---
> python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
> python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
> python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
> python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
> python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
> python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
> python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
Only in build/astra5/logs/harness-final: audit5.py
Only in build/astra5/logs/harness-final: close_comparators5.py
Only in build/astra5/logs/harness-final: close_comparators_final5.py
Only in build/astra5/logs/harness-final: common_attached5.py
Only in build/astra5/logs/harness-final: contact5.py
diff -r build/astra5/logs/harness-original/drive.py build/astra5/logs/harness-final/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra4'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
26c26
<   args=[str(ROOT/'build/astra4/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
66c66
<    p=sp.run([str(ROOT/'build/astra4/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
diff -r build/astra5/logs/harness-original/drive4_wrapper.py build/astra5/logs/harness-final/drive4_wrapper.py
6c6
< REPORT=Path('docs/reviews/astra-vs-sublime-4.md'); WORK=Path('docs/worker-reports/edit-w34.4-drive4.md')
---
> REPORT=Path('docs/reviews/astra-vs-sublime-5.md'); WORK=Path('docs/worker-reports/edit-w34.4-drive5.md')
10c10
<  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra4/sublimite\nsha256sum build/astra4/sublimite"],capture_output=True,text=True).stdout
---
>  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
72c72
<  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra4/logs/{self.stem}.json`.\n")
---
>  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
Only in build/astra5/logs/harness-final: drive5_wrapper.py
diff -r build/astra5/logs/harness-original/execute.py build/astra5/logs/harness-final/execute.py
14,15c14,15
<  with open('docs/reviews/astra-vs-sublime-4.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra4/logs/{self.stem}.json`. Visual analysis pending.\n')
<  with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
---
>  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
>  with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
Only in build/astra5/logs/harness-final: extra_onsets5.py
Only in build/astra5/logs/harness-final: file_metrics5.py
diff -r build/astra5/logs/harness-original/final_audit.py build/astra5/logs/harness-final/final_audit.py
3c3
< b=Path('build/astra4');r=[]
---
> b=Path('build/astra5');r=[]
12c12
< p=Path('docs/reviews/astra-vs-sublime-4.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
---
> p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
39c39
< audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive4.md'],missing_video_references=missing)
---
> audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive5.md'],missing_video_references=missing)
41c41
< wp=Path('docs/worker-reports/edit-w34.4-drive4.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
---
> wp=Path('docs/worker-reports/edit-w34.4-drive5.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
Only in build/astra5/logs/harness-final: final_probes5.py
Only in build/astra5/logs/harness-final: final_report5.py
Only in build/astra5/logs/harness-final: finish_runtime5.py
diff -r build/astra5/logs/harness-original/gutter_validate.py build/astra5/logs/harness-final/gutter_validate.py
3c3
< B=Path('build/astra4');out=[]
---
> B=Path('build/astra5');out=[]
Only in build/astra5/logs/harness-final: hide_old_sublime5.py
Only in build/astra5/logs/harness-final: large_comparisons5.py
Only in build/astra5/logs/harness-final: late_measure5.py
Only in build/astra5/logs/harness-final: loaded_attach5.py
Only in build/astra5/logs/harness-final: loaded_index5.py
diff -r build/astra5/logs/harness-original/metrics.py build/astra5/logs/harness-final/metrics.py
5c5
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/new4.py build/astra5/logs/harness-final/new4.py
3c3
< BIN=ROOT/'build/astra4/sublimite'
---
> BIN=ROOT/'build/astra5/sublimite'
diff -r build/astra5/logs/harness-original/new_tasks.py build/astra5/logs/harness-final/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra4/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
108c108
<    with open('docs/worker-reports/edit-w34.4-drive4.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
---
>    with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
Only in build/astra5/logs/harness-final: orphan_metrics5.py
Only in build/astra5/logs/harness-final: paired_metrics5.py
Only in build/astra5/logs/harness-final: profile_audit5.py
diff -r build/astra5/logs/harness-original/profile_continue4.py build/astra5/logs/harness-final/profile_continue4.py
19c19
< p=sp.run([str(ROOT/'build/astra4/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
---
> p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
Only in build/astra5/logs/harness-final: profile_onsets5.py
Only in build/astra5/logs/harness-final: query_metrics5.py
Only in build/astra5/logs/harness-final: report19_5.py
Only in build/astra5/logs/harness-final: report5.py
Only in build/astra5/logs/harness-final: report_core_final5.py
diff -r build/astra5/logs/harness-original/report_final.py build/astra5/logs/harness-final/report_final.py
3c3
< B=Path('build/astra4');P=Path('docs/reviews/astra-vs-sublime-4.md')
---
> B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
66c66
< Driven binary: `stat -c '%y %s' build/astra4/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra4/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra4`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra4/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra4/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
diff -r build/astra5/logs/harness-original/report_rows.py build/astra5/logs/harness-final/report_rows.py
3c3
< B=Path('build/astra4'); R=Path('docs/reviews/astra-vs-sublime-4.md')
---
> B=Path('build/astra5'); R=Path('docs/reviews/astra-vs-sublime-5.md')
Only in build/astra5/logs/harness-final: report_supplement5.py
Only in build/astra5/logs/harness-final: restart_check5.py
Only in build/astra5/logs/harness-final: runtime_repeat5.py
Only in build/astra5/logs/harness-final: runtime_stage5.py
diff -r build/astra5/logs/harness-original/scan_all.py build/astra5/logs/harness-final/scan_all.py
3c3
< B=Path('build/astra4');r=[]
---
> B=Path('build/astra5');r=[]
diff -r build/astra5/logs/harness-original/scan_parallel.py build/astra5/logs/harness-final/scan_parallel.py
3c3
< B=Path('build/astra4')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-final: sequence5.py
Only in build/astra5/logs/harness-final: sequence5_resume.py
Only in build/astra5/logs/harness-final: sequence5_tail.py
Only in build/astra5/logs/harness-final: sequence5_tail2.py
Only in build/astra5/logs/harness-final: sequence_new5.py
diff -r build/astra5/logs/harness-original/sheets.py build/astra5/logs/harness-final/sheets.py
4c4
< B=Path('build/astra4')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-final: sublime_dismiss5.py
Only in build/astra5/logs/harness-final: sublime_find_queries5.py
Only in build/astra5/logs/harness-final: sublime_setup5.py
Only in build/astra5/logs/harness-final: sublime_stage5.py
Only in build/astra5/logs/harness-final: syntax_metrics5.py
Only in build/astra5/logs/harness-final: syntax_runtime5.py
diff -r build/astra5/logs/harness-original/tasks.py build/astra5/logs/harness-final/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra4/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra5/logs/harness-original/tasks_extra.py build/astra5/logs/harness-final/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra4/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
Only in build/astra5/logs/harness-final: tasks_new5.py
diff -r build/astra5/logs/harness-original/timelines.py build/astra5/logs/harness-final/timelines.py
3c3
< B=Path('build/astra4')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-original/title_second.py build/astra5/logs/harness-final/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra4/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
Only in build/astra5/logs/harness-final: token_rgb5.py
diff -r build/astra5/logs/harness-original/token_specs.py build/astra5/logs/harness-final/token_specs.py
3c3
< B=Path('build/astra4');out=[]
---
> B=Path('build/astra5');out=[]
Only in build/astra5/logs/harness-final: unicode_cells5.py
Only in build/astra5/logs/harness-final: unicode_validate5.py
Only in build/astra5/logs/harness-final: whitespace_metrics5.py
Only in build/astra5/logs/harness-final: whitespace_runtime5.py
Only in build/astra5/logs/harness-final: wrap_runtime5.py
Only in build/astra5/logs/harness-final: wrap_runtime_r2_5.py
```

### Final recursive diff against drive 3

```diff
diff -r build/astra5/logs/harness-drive3/analysis_run.sh build/astra5/logs/harness-final/analysis_run.sh
2,8c2,8
< python3 build/astra3/large_metrics.py sublimite > build/astra3/logs/large-analysis.txt 2>&1
< python3 build/astra3/analyze_new.py > build/astra3/logs/new-analysis.txt 2>&1
< python3 build/astra3/action_onsets.py > build/astra3/logs/action-analysis.txt 2>&1
< python3 build/astra3/font_video_metrics.py > build/astra3/logs/font-video-analysis.txt 2>&1
< python3 build/astra3/find_notice.py > build/astra3/logs/notice-analysis.txt 2>&1
< python3 build/astra3/large_gutters.py > build/astra3/logs/gutter-analysis.txt 2>&1
< python3 build/astra3/tab_metrics.py > build/astra3/logs/tab-analysis.txt 2>&1
---
> python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
> python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
> python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
> python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
> python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
> python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
> python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
Only in build/astra5/logs/harness-final: analyze4.py
Only in build/astra5/logs/harness-final: audit5.py
Only in build/astra5/logs/harness-final: close_comparators5.py
Only in build/astra5/logs/harness-final: close_comparators_final5.py
Only in build/astra5/logs/harness-final: common_attached5.py
Only in build/astra5/logs/harness-final: common_retry4.py
Only in build/astra5/logs/harness-final: contact5.py
Only in build/astra5/logs/harness-final: crash4.py
diff -r build/astra5/logs/harness-drive3/drive.py build/astra5/logs/harness-final/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra3'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
26c26
<   args=[str(ROOT/'build/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
31c31
<   for i in range(200):
---
>   while True:
32a33,40
>    if self.editor=='sublime':
>     wanted=[str(Path(path).resolve()),str(Path(path).resolve()).replace(str(Path.home()),'~',1)]
>     pattern='^('+'|'.join(re.escape(q) for q in wanted)+')( •)? - Sublime Text'
>     found=sp.run(['xdotool','search','--name',pattern],capture_output=True,text=True).stdout.split()
>     if found:
>      title=sp.run(['xdotool','getwindowname',found[-1]],capture_output=True,text=True).stdout.strip()
>      self.mark('verified_full_path_title',window=found[-1],title=title)
>      self.x('windowmap',found[-1])
33a42
>    if self.proc.poll() not in (None,0):break
57c66
<    p=sp.run([str(ROOT/'build/tools/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
Only in build/astra5/logs/harness-final: drive4_wrapper.py
Only in build/astra5/logs/harness-final: drive5_wrapper.py
Only in build/astra5/logs/harness-final: end_only4.py
diff -r build/astra5/logs/harness-drive3/execute.py build/astra5/logs/harness-final/execute.py
14,15c14,15
<  with open('docs/reviews/astra-vs-sublime-3.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra3/logs/{self.stem}.json`. Visual analysis pending.\n')
<  with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
---
>  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
>  with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
Only in build/astra5/logs/harness-final: extra_metrics4.py
Only in build/astra5/logs/harness-final: extra_onsets5.py
Only in build/astra5/logs/harness-final: file_metrics4.py
Only in build/astra5/logs/harness-final: file_metrics5.py
Only in build/astra5/logs/harness-final: files4.py
diff -r build/astra5/logs/harness-drive3/final_audit.py build/astra5/logs/harness-final/final_audit.py
3c3
< b=Path('build/astra3');r=[]
---
> b=Path('build/astra5');r=[]
12c12
< p=Path('docs/reviews/astra-vs-sublime-2.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
---
> p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text();start=s.index('| Recording |');end=s.index('## 14. Idle',start);s=s[:start]+'\n'.join(lines)+f'\n\nAll **{len(rows)} original sublimité recordings** have all three completed scans (**{len(r)} detector runs**, counted from `logs/flash-summary.json`), including failed attempts. The separate moving-resize strip adds the three scans described above.\n\n'+s[end:];p.write_text(s)
39c39
< audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive2.md'],missing_video_references=missing)
---
> audit=dict(finished=now,original_sublimite_videos=len(rows),flash_runs=len(r),nonzero_flashes=[x for x in r if x['flashes']],total_video_files=len(inventory),reports=[str(p),'docs/worker-reports/edit-w34.4-drive5.md'],missing_video_references=missing)
41c41
< wp=Path('docs/worker-reports/edit-w34.4-drive2.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
---
> wp=Path('docs/worker-reports/edit-w34.4-drive5.md');ws=wp.read_text().replace('<!-- FINAL_AUDIT -->',f"Final audit at **{now}**: all fourteen task headings and the exact final verdict line checked; all literal video references resolve. `{len(inventory)}` MKVs probed successfully (including the derived resize strip); original captures are 2880×1800. All `{len(rows)}` sublimité recordings have all three completed color scans (`{len(r)}` detector runs), with zero flashes, plus the three zero-result moving-resize scans. `logs/video-inventory.json` and `logs/final-audit.json` retain this audit. The supplementary paragraph readback passes in both editors; the short-dwell 1 GiB repeat also passes. Preflight timestamp was 2026-10-11T21:18:29+10:00; completion is within the requested eighty-minute budget. Closed the explicitly created safe-mode Sublime test process after its recordings; no per-task recorder was intentionally left running.")
Only in build/astra5/logs/harness-final: final_probes5.py
Only in build/astra5/logs/harness-final: final_report5.py
Only in build/astra5/logs/harness-final: finish_original.py
Only in build/astra5/logs/harness-final: finish_original_rest.py
Only in build/astra5/logs/harness-final: finish_original_tail.py
Only in build/astra5/logs/harness-final: finish_runtime5.py
Only in build/astra5/logs/harness-final: first_batch.py
Only in build/astra5/logs/harness-final: followups4.py
Only in build/astra5/logs/harness-final: font_metrics_gated4.py
Only in build/astra5/logs/harness-final: font_normalize4.py
Only in build/astra5/logs/harness-final: gutter_cells4.py
Only in build/astra5/logs/harness-final: gutter_retry4.py
diff -r build/astra5/logs/harness-drive3/gutter_validate.py build/astra5/logs/harness-final/gutter_validate.py
3c3
< B=Path('build/astra3');out=[]
---
> B=Path('build/astra5');out=[]
Only in build/astra5/logs/harness-final: hide_old_sublime5.py
Only in build/astra5/logs/harness-final: idle_r2_metrics.py
Only in build/astra5/logs/harness-final: index_frames4.py
Only in build/astra5/logs/harness-final: index_metrics4.py
Only in build/astra5/logs/harness-final: large4.py
Only in build/astra5/logs/harness-final: large_comparisons5.py
Only in build/astra5/logs/harness-final: last_ready4.py
Only in build/astra5/logs/harness-final: last_ui4.py
Only in build/astra5/logs/harness-final: late_measure5.py
Only in build/astra5/logs/harness-final: late_valid4.py
Only in build/astra5/logs/harness-final: loaded_attach5.py
Only in build/astra5/logs/harness-final: loaded_follow4.py
Only in build/astra5/logs/harness-final: loaded_index4.py
Only in build/astra5/logs/harness-final: loaded_index5.py
diff -r build/astra5/logs/harness-drive3/metrics.py build/astra5/logs/harness-final/metrics.py
5c5
< B=Path('build/astra3')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-final: new4.py
Only in build/astra5/logs/harness-final: new_batch.py
diff -r build/astra5/logs/harness-drive3/new_tasks.py build/astra5/logs/harness-final/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
108c108
<    with open('docs/worker-reports/edit-w34.4-drive3.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
---
>    with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- FAILED {arg}: {ex}\n')
Only in build/astra5/logs/harness-final: original_readback4.py
Only in build/astra5/logs/harness-final: orphan_metrics5.py
Only in build/astra5/logs/harness-final: paired_metrics5.py
Only in build/astra5/logs/harness-final: profile4.py
Only in build/astra5/logs/harness-final: profile_audit5.py
Only in build/astra5/logs/harness-final: profile_continue4.py
Only in build/astra5/logs/harness-final: profile_onsets5.py
Only in build/astra5/logs/harness-final: query_metrics5.py
Only in build/astra5/logs/harness-final: rare_stable_metrics4.py
Only in build/astra5/logs/harness-final: remaining_batch.py
Only in build/astra5/logs/harness-final: remaining_resume.py
Only in build/astra5/logs/harness-final: reopen_retry3_4.py
Only in build/astra5/logs/harness-final: reopen_retry4.py
Only in build/astra5/logs/harness-final: repair_comparisons.py
Only in build/astra5/logs/harness-final: report19_5.py
Only in build/astra5/logs/harness-final: report5.py
Only in build/astra5/logs/harness-final: report_core_final5.py
diff -r build/astra5/logs/harness-drive3/report_final.py build/astra5/logs/harness-final/report_final.py
3c3
< B=Path('build/astra3');P=Path('docs/reviews/astra-vs-sublime-3.md')
---
> B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
66c66
< Driven binary: `stat -c '%y %s' build/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra3/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra3`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra3/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra3/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
Only in build/astra5/logs/harness-final: report_new_rows.py
Only in build/astra5/logs/harness-final: report_rows.py
Only in build/astra5/logs/harness-final: report_supplement5.py
Only in build/astra5/logs/harness-final: restart_check5.py
Only in build/astra5/logs/harness-final: runtime_repeat5.py
Only in build/astra5/logs/harness-final: runtime_stage5.py
diff -r build/astra5/logs/harness-drive3/scan_all.py build/astra5/logs/harness-final/scan_all.py
3c3
< B=Path('build/astra3');r=[]
---
> B=Path('build/astra5');r=[]
diff -r build/astra5/logs/harness-drive3/scan_parallel.py build/astra5/logs/harness-final/scan_parallel.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-final: scroll_caret4.py
Only in build/astra5/logs/harness-final: search_stable4.py
Only in build/astra5/logs/harness-final: sequence5.py
Only in build/astra5/logs/harness-final: sequence5_resume.py
Only in build/astra5/logs/harness-final: sequence5_tail.py
Only in build/astra5/logs/harness-final: sequence5_tail2.py
Only in build/astra5/logs/harness-final: sequence_new5.py
diff -r build/astra5/logs/harness-drive3/sheets.py build/astra5/logs/harness-final/sheets.py
4c4
< B=Path('build/astra3')
---
> B=Path('build/astra5')
Only in build/astra5/logs/harness-final: sublime_dismiss5.py
Only in build/astra5/logs/harness-final: sublime_find_queries5.py
diff -r build/astra5/logs/harness-drive3/sublime_loaded.py build/astra5/logs/harness-final/sublime_loaded.py
5c5
<  wins=d.windows()
---
>  wins=[w for w in d.windows() if '/tmp/edit-corpus/log_1g.txt - Sublime Text' in sp.run(['xdotool','getwindowname',w],capture_output=True,text=True).stdout]
Only in build/astra5/logs/harness-final: sublime_setup5.py
Only in build/astra5/logs/harness-final: sublime_stage5.py
Only in build/astra5/logs/harness-final: syntax_metrics4.py
Only in build/astra5/logs/harness-final: syntax_metrics5.py
Only in build/astra5/logs/harness-final: syntax_runtime5.py
Only in build/astra5/logs/harness-final: target_ready_r2.py
diff -r build/astra5/logs/harness-drive3/tasks.py build/astra5/logs/harness-final/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra3/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra5/logs/harness-drive3/tasks_extra.py build/astra5/logs/harness-final/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
Only in build/astra5/logs/harness-final: tasks_new5.py
diff -r build/astra5/logs/harness-drive3/timelines.py build/astra5/logs/harness-final/timelines.py
3c3
< B=Path('build/astra3')
---
> B=Path('build/astra5')
diff -r build/astra5/logs/harness-drive3/title_second.py build/astra5/logs/harness-final/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
Only in build/astra5/logs/harness-final: token_rgb4.py
Only in build/astra5/logs/harness-final: token_rgb5.py
Only in build/astra5/logs/harness-final: token_specs.py
Only in build/astra5/logs/harness-final: tokens4.py
Only in build/astra5/logs/harness-final: unicode_cells5.py
Only in build/astra5/logs/harness-final: unicode_validate5.py
Only in build/astra5/logs/harness-final: whitespace_metrics5.py
Only in build/astra5/logs/harness-final: whitespace_runtime5.py
Only in build/astra5/logs/harness-final: wrap_metrics4.py
Only in build/astra5/logs/harness-final: wrap_onsets4.py
Only in build/astra5/logs/harness-final: wrap_runtime5.py
Only in build/astra5/logs/harness-final: wrap_runtime_r2_5.py
Only in build/astra5/logs/harness-final: wrapped_reverse4.py
```

## Final execution and limits

All required task families were driven on :98, with failed/blank/occluded attempts preserved and qualified in the report. Clean-profile repeats repaired the main comparator failures. No product code was changed. The only compile was the explicitly allowed task-36 fixture generator under build/astra5. The new dense64 relaunch failure is retained and reproduced; no fix was attempted. Optional paired typing captures completed. Video evidence is under build/astra5/video; readbacks, raw command envelopes, scan results and trace blocks are retained beside it.

The first final offline-analysis request did not execute because automatic permission approval review timed out. The same file-only work was retried within workspace permissions; no user approval was required. Captures are from 2026-10-16; final report work continued offline after the environment advanced to 2026-10-17. No new UI drive was performed during that finalization.

Task-36 oversized fixture/source and twelve oversized copied-profile artifacts were removed only after hash verification. Original real-profile data and prior-drive evidence were not deleted. The small failing task-37 journal and its copied repro remain. Original fixture audit reports no differences; the corpus end hash matches the brief.
