# Drive 6 worker log — edit-w34.4

Read drive-5 Conditions, tasks 34–37, disposition, defects and verdict; read HANDOFF, CLAUDE and PLAN workflow. No bd/git, builds, source/test/bench/fuzz/tool edits. Core order: 37, 38, 39, 40, 41, 36. Evidence root build/astra6.

Setup binary checks:
```text
START
2026-10-16T21:00:57+11:00
Not charging
1
1.17 1.95 1.87 5/1748 159
2026-10-16 07:44:42.898991813 +1100 10292616
7a9f09f00f261d297257302212f2ab9536df194c4e7a0d9fbceeb32f859b92e3  build/astra6/sublimite
START
2026-10-16T21:00:57+11:00
Not charging
1
1.17 1.95 1.87 5/1748 159
2026-10-16 01:55:42.034508383 +1100 10001384
09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621  build/astra5/sublimite
```

Harness diff (each copied Python/shell file, equivalent recursive file comparison; only output/binary/tracedump base changed):
```diff
--- build/astra5/drive4_wrapper.py	2026-10-16 05:27:26.621593461 +1100
+++ build/astra6/drive4_wrapper.py	2026-10-16 21:00:57.297923951 +1100
@@ -7,7 +7,7 @@
 EXPECTED='ee77869520b159ca852f7baa53bd29d2a950cfb3c66b9c8c7e568cf97df7d169'
 def check(label):
  p=B/'sublimite';digest=hashlib.sha256(p.read_bytes()).hexdigest()
- stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
+ stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra6/sublimite\nsha256sum build/astra6/sublimite"],capture_output=True,text=True).stdout
  with (B/'logs/binary.txt').open('a') as f:f.write('\n'+label+'\n'+stamp)
  actual_stat=sp.check_output(['stat','-c','%y %s',str(p)],text=True).strip()
  if digest!=EXPECTED or actual_stat!='2026-10-13 05:36:44.598229658 +1000 9873912':
@@ -69,7 +69,7 @@
  if self.editor=='sublimite' and self.proc and self.proc.poll() is not None:
   log=B/'logs'/f'{self.stem}.editor.log';self.mark('process_end_final',pid=self.proc.pid,returncode=self.proc.returncode,stderr_tail=log.read_text()[-4000:])
  stamp=next(e for e in self.events if e['action']=='power')
- with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
+ with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra6/logs/{self.stem}.json`.\n")
  with WORK.open('a') as f:f.write(f'- Captured `{self.stem}`; process endings recorded in task JSON.\n')
 drive.Drive.finish=finish
 # Preserve drive-3 wrapped bytes even though its script regenerates from current PRD.
--- build/astra5/hide_old_sublime5.py	2026-10-16 06:15:45.446684666 +1100
+++ build/astra6/hide_old_sublime5.py	2026-10-16 21:00:57.297923951 +1100
@@ -9,8 +9,8 @@
  s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect('\0/tmp/.X11-unix/X98');return s
 unix_connect._get_unix_socket=sock
 d=display.Display(':98');root=d.screen().root;pid_atom=d.intern_atom('_NET_WM_PID')
-Path('build/astra5/logs/window-guard.pid').write_text(str(os.getpid()))
-f=open('build/astra5/logs/window-guard.jsonl','a',buffering=1)
+Path('build/astra6/logs/window-guard.pid').write_text(str(os.getpid()))
+f=open('build/astra6/logs/window-guard.jsonl','a',buffering=1)
 try:
  while Path(f'/proc/{pid}').exists():
   for w in root.query_tree().children:
--- build/astra5/final_probes5.py	2026-10-16 07:29:56.193062365 +1100
+++ build/astra6/final_probes5.py	2026-10-16 21:00:57.297923951 +1100
@@ -3,7 +3,7 @@
 # Cancel any remaining native Open chooser from the completed inline-open probe.
 for w in sp.run(['xdotool','search','--name','^Open File$'],capture_output=True,text=True).stdout.split():
  pid=sp.run(['xdotool','getwindowpid',w],capture_output=True,text=True).stdout.strip()
- try:private=b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-clean/config' in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
+ try:private=b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-clean/config' in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
  except OSError:private=False
  if private:
   sp.run(['xdotool','windowmap',w,'windowraise',w,'windowfocus','--sync',w,'key','Escape'])
--- build/astra5/close_comparators_final5.py	2026-10-16 08:40:26.545288418 +1100
+++ build/astra6/close_comparators_final5.py	2026-10-16 21:00:57.298923957 +1100
@@ -5,9 +5,9 @@
  pid=sp.run(['xdotool','getwindowpid',w],capture_output=True,text=True).stdout.strip()
  try:env=Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
  except OSError:continue
- if not any(x.startswith(b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-') for x in env):continue
+ if not any(x.startswith(b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-') for x in env):continue
  title=sp.run(['xdotool','getwindowname',w],capture_output=True,text=True).stdout.strip()
- if '/tmp/edit-corpus/' in title or '/build/astra5/' in title:pids.setdefault(pid,w)
+ if '/tmp/edit-corpus/' in title or '/build/astra6/' in title:pids.setdefault(pid,w)
 for pid,w in pids.items():
  t=time.time();r=sp.run(['xdotool','windowmap',w,'windowraise',w,'windowfocus','--sync',w,'key','--clearmodifiers','Escape','Escape','Escape','ctrl+q'],capture_output=True,text=True)
  rows.append(dict(pid=pid,window=w,quit_requested=t,command_returncode=r.returncode,stderr=r.stderr))
@@ -15,4 +15,4 @@
  if all(not Path('/proc',pid).exists() for pid in pids):break
  time.sleep(1)
 for r in rows:r['still_present']=Path('/proc',r['pid']).exists()
-Path('build/astra5/logs/comparator-cleanup-final.json').write_text(json.dumps(rows,indent=2));print(rows)
+Path('build/astra6/logs/comparator-cleanup-final.json').write_text(json.dumps(rows,indent=2));print(rows)
--- build/astra5/timelines.py	2026-10-16 05:27:26.622593464 +1100
+++ build/astra6/timelines.py	2026-10-16 21:00:57.298923957 +1100
@@ -1,6 +1,6 @@
 import json,re
 from pathlib import Path
-B=Path('build/astra5')
+B=Path('build/astra6')
 for p in (B/'logs').glob('*.json'):
  f=p.with_suffix('.ffmpeg.log')
  if not f.exists():continue
--- build/astra5/tasks.py	2026-10-16 05:27:26.623593467 +1100
+++ build/astra6/tasks.py	2026-10-16 21:00:57.298923957 +1100
@@ -47,7 +47,7 @@
   d.key('ctrl+f');d.shot('find-panel');d.type('alpha',100);d.shot('find-query');d.key('Return');d.key('Escape');d.key('F3');d.shot('next-match');d.key('ctrl+h');d.shot('replace-panel');d.key('Escape')
  elif task=='08-save':
   d.type('EDITED_',100);d.key('ctrl+s');d.cmd(['cat',str(BASE/'files'/filename)]);d.shot('after-save')
-  d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
+  d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra6/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
   d.x('windowfocus',1);time.sleep(.3);d.focus();time.sleep(1);d.key('ctrl+End');d.shot('external-refocus');d.cmd(['cat',str(BASE/'files'/filename)])
  elif task=='11-resize':
   for w,h in [(1440,900),(2880,1800),(800,600),(960,600)]:
--- build/astra5/finish_report5.py	2026-10-17 07:10:45.657374463 +1100
+++ build/astra6/finish_report5.py	2026-10-16 21:00:57.298923957 +1100
@@ -1,8 +1,8 @@
 from pathlib import Path
 import json,sys,re
-sys.path.insert(0,str(Path('build/astra5').resolve()))
+sys.path.insert(0,str(Path('build/astra6').resolve()))
 from report5 import evidence,events
-B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md');s=P.read_text()
+B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md');s=P.read_text()
 def insert_before_task(n,text):
  global s
  marker=f'## {n}. ';i=s.index(marker);s=s[:i]+text+'\n\n'+s[i:]
--- build/astra5/final_report5.py	2026-10-16 08:54:44.691059449 +1100
+++ build/astra6/final_report5.py	2026-10-16 21:00:57.298923957 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re,hashlib
-B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
+B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md')
 raw=P.read_text();(B/'logs/report-sections-source.json').write_text(json.dumps({'text':raw},ensure_ascii=False))
 def ev(stem,base=B):return json.loads((base/'logs'/f'{stem}.json').read_text())
 def stamp(stem,base=B):
@@ -26,7 +26,7 @@
 start=start.replace('No verdict established yet.','').strip()
 # Definitive conditions, in addition to the literal original build/start stamp.
 extra='''
-All evidence paths without a leading `build/` are relative to **build/astra5/**. Videos are in video/, stills and frame sequences in frames/, command/power/readback records in logs/, and literal trace dumps in traces/. All measured values in a row inherit its BAT0/AC/load stamp. Task IDs, key counts, prescribed dwell times, fixture dimensions and analysis rectangles are inputs/identifiers rather than performance budgets. A single observation is not a percentile. Every onset uses resampled 60 Hz intervals (P: 1000/60 ms per interval); command envelopes bound software injection, not physical key-to-photon time.
+All evidence paths without a leading `build/` are relative to **build/astra6/**. Videos are in video/, stills and frame sequences in frames/, command/power/readback records in logs/, and literal trace dumps in traces/. All measured values in a row inherit its BAT0/AC/load stamp. Task IDs, key counts, prescribed dwell times, fixture dimensions and analysis rectangles are inputs/identifiers rather than performance budgets. A single observation is not a percentile. Every onset uses resampled 60 Hz intervals (P: 1000/60 ms per interval); command envelopes bound software injection, not physical key-to-photon time.
 
 `xdpyinfo` confirms 2880×1800 pixels and 243×243 DPI (M)[AC], start stamp above. The verified Xvfb :98 owner and argv are in logs/xvfb-owner.txt; only that PID is stopped/continued in task 29. EGL is unavailable in the saved runtime launches, which use raster. The GL path is not verified. Cache was not flushed; fixture copying/hashing and shared-box work affect cache/load conditions.
 
@@ -34,7 +34,7 @@
 
 The monochrome Noto Emoji file is observed by fontconfig. logs/fonts.txt contains `fc-list | grep -i noto` followed by `fc-list ':charset=1f469'`; this environment change is distinct from a bead. The woman-technologist, family and flag render as monochrome artwork in the staged runtime. Colour artwork remains waived for M0.
 
-The staged executable is never rebuilt. The only old UI binary use is the explicitly requested task-36 fixture producer, labelled separately; it produced no oversized journal. The permitted fallback fixture generator follows the session test’s construction and links the existing current library solely to create an orphan. That generator is not the build under UI test. UI verification always uses build/astra5/sublimite. Main/source work continued elsewhere; BUILD and source/library changes are not substituted for staged-binary behaviour.
+The staged executable is never rebuilt. The only old UI binary use is the explicitly requested task-36 fixture producer, labelled separately; it produced no oversized journal. The permitted fallback fixture generator follows the session test’s construction and links the existing current library solely to create an orphan. That generator is not the build under UI test. UI verification always uses build/astra6/sublimite. Main/source work continued elsewhere; BUILD and source/library changes are not substituted for staged-binary behaviour.
 
 Behaviour supports retained o20 file commands, zw9 reversal, g02 find keys, 97i identity reuse, 11v body colour, cee raster survival, 1um prompt placement, 48u bottom-edge autoscroll/panel resize, dr3 gutter selection, r1l sampled cache pages, pgr atomic recovery outcomes, rzj two-instance restoration, rfq retirement, bml CRLF and 1vo save retry. hgd’s none/all/selection and live fallback work, but a user-visible invalid-value diagnostic was not observed. fm4’s oversized-orphan startup/preservation works. 28u’s size objective is met in the sampled growth window, but the dense64 reopening workflow fails. v16 plus the installed font produces combined monochrome glyphs. The additional 34l engine, 6e2 fallback-chain ordering, mmp’s specifically typed-before-index case, 3u4/9u3 and the 848 GL path are not independently established by these probes. fi2 is not landed and changed-base recovery retains its documented limit.
 
--- build/astra5/report5.py	2026-10-16 05:38:35.925079436 +1100
+++ build/astra6/report5.py	2026-10-16 21:00:57.299923964 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re
-B=Path('build/astra5');REPORT=Path('docs/reviews/astra-vs-sublime-5.md')
+B=Path('build/astra6');REPORT=Path('docs/reviews/astra-vs-sublime-5.md')
 def events(stem):return json.loads((B/'logs'/f'{stem}.json').read_text())
 def evidence(stem,labels=[]):
  es=events(stem);p=next(x for x in es if x.get('action')=='power');stamp=f"(M)[{'AC' if p['AC']=='1' else 'bat'}], BAT0={p['BAT0']}, AC/online={p['AC']}, load1={p['loadavg'].split()[0]}"
--- build/astra5/final_audit.py	2026-10-16 05:27:26.626593476 +1100
+++ build/astra6/final_audit.py	2026-10-16 21:00:57.299923964 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re,collections,subprocess as sp,concurrent.futures,datetime
-b=Path('build/astra5');r=[]
+b=Path('build/astra6');r=[]
 for p in sorted((b/'video').glob('*sublimite.mkv')):
  for c,t in [('bf853d',40),('ff8800',60),('6b5030',24)]:
   out=b/'logs'/f'{p.stem}.flash-{c}.txt';txt=out.read_text();m=re.search(r'flashes .*: (\d+)',txt);assert m,(p,c)
--- build/astra5/scan_all.py	2026-10-16 05:27:26.627593480 +1100
+++ build/astra6/scan_all.py	2026-10-16 21:00:57.299923964 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import subprocess as sp,json,re
-B=Path('build/astra5');r=[]
+B=Path('build/astra6');r=[]
 for p in sorted((B/'video').glob('*sublimite.mkv')):
  for rgb,tol in [('bf853d',40),('ff8800',60),('6b5030',24)]:
   out=B/'logs'/f'{p.stem}.flash-{rgb}.txt'
--- build/astra5/drive.py	2026-10-16 05:27:26.627593480 +1100
+++ build/astra6/drive.py	2026-10-16 21:00:57.300923971 +1100
@@ -2,7 +2,7 @@
 from pathlib import Path
 from Xlib import X,display,protocol
 os.environ['DISPLAY']=':98'
-ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
+ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra6'
 class Drive:
  def __init__(self,task,editor):
   self.task=task;self.editor=editor;self.stem=f'{task}-{editor}';self.events=[];self.proc=None;self.w=None
@@ -23,7 +23,7 @@
  def launch(self,path,new=True):
   before=self.windows();self.mark('launch',path=str(path))
   f=open(BASE/'logs'/f'{self.stem}.editor.log','a')
-  args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
+  args=[str(ROOT/'build/astra6/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
   state=BASE/'state'/self.stem;state.mkdir(parents=True,exist_ok=True)
   env=dict(os.environ,EDIT_TRACE_DUMP=str(BASE/'traces'/f'{self.stem}.bin'))
   if self.editor=='sublimite':env['XDG_DATA_HOME']=str(state)
@@ -63,7 +63,7 @@
   self.rec.send_signal(signal.SIGINT);self.rec.wait(timeout=15);self.log.close();self.mark('record_stop')
   if close and self.editor=='sublimite': self.close()
   if self.editor=='sublimite' and (BASE/'traces'/f'{self.stem}.bin').exists():
-   p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
+   p=sp.run([str(ROOT/'build/astra6/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
   
   if self.w: self.x('windowunmap',self.w) if self.editor=='sublime' else None
   print(self.stem, 'done', flush=True)
--- build/astra5/execute.py	2026-10-16 05:27:26.627593480 +1100
+++ build/astra6/execute.py	2026-10-16 21:00:57.300923971 +1100
@@ -11,7 +11,7 @@
 finish=drive.Drive.finish
 def done(self,*a,**kw):
  finish(self,*a,**kw)
- with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
+ with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra6/logs/{self.stem}.json`. Visual analysis pending.\n')
  with open('docs/worker-reports/edit-w34.4-drive5.md','a') as f:f.write(f'- Completed harness `{self.stem}`.\n')
 drive.Drive.finish=done
 script=sys.argv[1];sys.argv=sys.argv[1:];runpy.run_path(script,run_name='__main__')
--- build/astra5/new_tasks.py	2026-10-16 05:27:26.627593480 +1100
+++ build/astra6/new_tasks.py	2026-10-16 21:00:57.300923971 +1100
@@ -18,7 +18,7 @@
 def noarg_launch(d):
  before=d.windows();d.mark('launch',path=None)
  env=dict(os.environ,XDG_DATA_HOME=str(BASE/'state'/d.stem),EDIT_TRACE_DUMP=str(BASE/'traces'/f'{d.stem}.bin'))
- f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
+ f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra6/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
  for _ in range(200):
   found=[w for w in d.windows() if w not in before]
   if found:d.w=found[-1];break
@@ -75,14 +75,14 @@
  d.finish()
 
 def title(ed):
- p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
+ p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
 
 def external(ed):
  p=BASE/'files/external.txt';p.write_text(''.join(f'{i:02} numbered line\n' for i in range(1,21)));d=Drive('19-external',ed);d.launch(p,new=False);d.key('ctrl+Home');d.type('X',0);d.key('ctrl+s');d.cmd(['cat',str(p)]);d.shot('saved');d.mark('external-append');p.open('a').write('EXTERNAL_APPEND\n');time.sleep(2);d.shot('clean-reload');d.key('ctrl+End');d.shot('append-end');d.key('ctrl+z');d.shot('undo-after-reload');readback(d,'after-reload-undo')
  d.key('ctrl+End');d.mark('external-shrink');p.write_text(''.join(p.read_text().splitlines(True)[:5]));time.sleep(2);d.shot('shrunk')
  d.key('ctrl+Home');d.type('DIRTY_',0);d.mark('dirty-external-R');p.open('a').write('AFTER_DIRTY_R\n');time.sleep(2);d.shot('dirty-prompt-R');d.key('r' if ed=='sublimite' else 'Return');d.shot('reloaded-R');d.key('ctrl+Home');d.type('KEEP_',0);d.mark('dirty-external-K');p.open('a').write('AFTER_DIRTY_K\n');time.sleep(2);d.shot('dirty-prompt-K');d.key('k' if ed=='sublimite' else 'Escape');d.shot('kept-K');readback(d,'kept')
  # Save our task fixture so the inactive-tab probe starts clean.
- d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
+ d.key('ctrl+s');d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
 
 def emoji(ed):
  p=BASE/'files/emoji.txt';p.write_text('A👩‍💻B\nA😀‍💻B\nA👍🏽B\nA😀B\nA❤️B\n');d=Drive('20-emoji',ed);d.mark('fixture',sha256=digest(p));d.launch(p);d.shot('default');d.key('ctrl+equal');d.key('ctrl+equal');d.shot('zoom2');d.key('ctrl+0');d.key('ctrl+Home')
--- build/astra5/sheets.py	2026-10-16 05:27:26.628593483 +1100
+++ build/astra6/sheets.py	2026-10-16 21:00:57.300923971 +1100
@@ -1,7 +1,7 @@
 from pathlib import Path
 from PIL import Image,ImageDraw
 import sys
-B=Path('build/astra5')
+B=Path('build/astra6')
 for task in sys.argv[1:]:
  ps=sorted((B/'frames').glob(task+'*.png'))
  for start in range(0,len(ps),8):
--- build/astra5/title_second.py	2026-10-16 05:27:26.628593483 +1100
+++ build/astra6/title_second.py	2026-10-16 21:00:57.300923971 +1100
@@ -2,4 +2,4 @@
 for ed in ['sublimite','sublime']:
  folder=BASE/'files'/('title-second-'+ed);folder.mkdir(exist_ok=True)
  a=folder/'ascii.txt';a.write_text('ASCII tab\n');p=folder/'naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n')
- d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
+ d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
--- build/astra5/scan_parallel.py	2026-10-16 05:27:26.629593486 +1100
+++ build/astra6/scan_parallel.py	2026-10-16 21:00:57.300923971 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import subprocess as sp,json,re,concurrent.futures
-B=Path('build/astra5')
+B=Path('build/astra6')
 def scan(job):
  p,rgb,tol=job;out=B/'logs'/f'{p.stem}.flash-{rgb}.txt'
  if not out.exists():
--- build/astra5/new4.py	2026-10-16 05:27:26.629593486 +1100
+++ build/astra6/new4.py	2026-10-16 21:00:57.300923971 +1100
@@ -1,6 +1,6 @@
 from new_tasks import *
 from PIL import Image
-BIN=ROOT/'build/astra5/sublimite'
+BIN=ROOT/'build/astra6/sublimite'
 
 def focus_path(d,path):
  if d.editor=='sublime':
--- build/astra5/drive5_wrapper.py	2026-10-16 05:59:39.183565045 +1100
+++ build/astra6/drive5_wrapper.py	2026-10-16 21:00:57.300923971 +1100
@@ -7,7 +7,7 @@
 EXPECTED='09087573ed0aebb5b537dcc12cfc58d946986b1088ba2d950110c82993ea8621'
 def check(label):
  p=B/'sublimite';digest=hashlib.sha256(p.read_bytes()).hexdigest()
- stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
+ stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra6/sublimite\nsha256sum build/astra6/sublimite"],capture_output=True,text=True).stdout
  with (B/'logs/binary.txt').open('a') as f:f.write('\n'+label+'\n'+stamp)
  actual_stat=sp.check_output(['stat','-c','%y %s',str(p)],text=True).strip()
  if digest!=EXPECTED or actual_stat!='2026-10-16 01:55:42.034508383 +1100 10001384':
@@ -72,7 +72,7 @@
  if self.editor=='sublimite' and self.proc and self.proc.poll() is not None:
   log=B/'logs'/f'{self.stem}.editor.log';self.mark('process_end_final',pid=self.proc.pid,returncode=self.proc.returncode,stderr_tail=log.read_text()[-4000:])
  stamp=next(e for e in self.events if e['action']=='power')
- with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
+ with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra6/logs/{self.stem}.json`.\n")
  with WORK.open('a') as f:f.write(f'- Captured `{self.stem}`; process endings recorded in task JSON.\n')
 drive.Drive.finish=finish
 # Preserve drive-3 wrapped bytes even though its script regenerates from current PRD.
--- build/astra5/metrics.py	2026-10-16 05:27:26.630593489 +1100
+++ build/astra6/metrics.py	2026-10-16 21:00:57.300923971 +1100
@@ -2,7 +2,7 @@
 from pathlib import Path
 import numpy as np
 from PIL import Image
-B=Path('build/astra5')
+B=Path('build/astra6')
 def events(stem):
  e=json.loads((B/'logs'/f'{stem}.json').read_text());t=float(re.search(r'start: ([\d.]+)',(B/'logs'/f'{stem}.ffmpeg.log').read_text())[1]);return e,t
 
--- build/astra5/contact5.py	2026-10-16 05:33:03.857668470 +1100
+++ build/astra6/contact5.py	2026-10-16 21:00:57.301923977 +1100
@@ -1,7 +1,7 @@
 from pathlib import Path
 from PIL import Image,ImageDraw
 import sys
-b=Path('build/astra5');ps=[]
+b=Path('build/astra6');ps=[]
 for pat in sys.argv[2:]:ps.extend(sorted((b/'frames').glob(pat)))
 w=840;h=400;out=Image.new('RGB',(w*2,h*((len(ps)+1)//2)), 'white');draw=ImageDraw.Draw(out)
 for i,p in enumerate(ps):
--- build/astra5/unicode_cells5.py	2026-10-16 08:08:42.475871492 +1100
+++ build/astra6/unicode_cells5.py	2026-10-16 21:00:57.301923977 +1100
@@ -1,7 +1,7 @@
 from pathlib import Path
 from PIL import Image
 import numpy as np,json
-b=Path('build/astra5');out=[]
+b=Path('build/astra6');out=[]
 for ed in ['sublimite','sublime']:
  for line in range(1,6):
   pos={}
--- build/astra5/report_final.py	2026-10-16 05:27:26.631593492 +1100
+++ build/astra6/report_final.py	2026-10-16 21:00:57.301923977 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re
-B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
+B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md')
 saved=json.loads((B/'logs/report-observations.json').read_text());old=saved['rows'];sections={i:[] for i in range(1,23)}
 route={1:[37,13,12,14],2:[15,38],3:[11],4:[0,1,2],5:[3],6:[8,9,10],7:[4,5],8:[6,7],9:[16,17],12:[18],15:[27,28,29],16:[19,20,21,30,31],17:[22,23,24,25,26],18:[34],19:[32,33],20:[35,36]}
 for n,ids in route.items():sections[n]=[old[i] for i in ids]
@@ -63,15 +63,15 @@
 
 All editor interaction and capture used **DISPLAY=:98 only**, never :0 or :99. The prior virtual display was unavailable. A restricted Xvfb launch failed to bind sockets; an escalated launch on :98 succeeded. `xdpyinfo` measured **2880×1800 pixels, 243×243 DPI** (M)[AC], `logs/xdpyinfo-escalated.txt`. No existing recorder was stopped. Renderer stderr reports EGL failure followed by raster fallback with no retry.
 
-Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
+Driven binary: `stat -c '%y %s' build/astra6/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra6/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
 
 Each captured task logs BAT0 status, AC/online and /proc/loadavg before recording. All capture stamps are **(M)[AC]**, AC/online=1; battery status is Not charging. Recorded task-start load1 ranges from **0.43 to 31.80** (M)[AC], aggregated in `logs/run-index.json`. External clang/cc1/utf8_bench jobs were visible during high load; none were signalled. Late single observations are not a controlled regression comparison. Drive-2 quoted measurements have **unstamped power** and are reference points, not budgets. Counts/dwells/key-repeat sizes specified by the brief are input parameters, not pass/fail budgets.
 
-The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
+The copied drive-2 scripts differ only by `build/astra2`→`build/astra6`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
 
 The /tmp corpus was absent. Existing `build/tools/mkcorpus` regenerated it; C and dense files compare byte-identical with drive-2 copies. Matched PRD/editor/UTF-8/navigation fixtures were copied from drive 2 so their contents remain identical. The new wrapped test uses a fresh PRD copy. C/dense/log sizes are **1,048,576 / 67,108,864 / 1,073,741,824 bytes** (M)[AC], stat/cmp in `logs/corpus.txt`. The log was opened in place, never copied or saved. Its SHA256 is `a259640b05b70487cd68a641913d8f2d8023d77f9bd1a5ba63beb555ceaae875`.
 
-All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
+All video names below are relative to **build/astra6/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra6/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
 
 Capture uses the inherited x11grab/H.264 ultrafast/CRF23/yuv420p setup with four encoder threads. All interval counts are **60 Hz resampled intervals** (P: 1000/60 = 16.666… ms, displayed as 16.7 ms). They are single observations from logged command start to visible matching pixels, not population percentiles or physical keyboard-to-photon latency. Injection can occur anywhere inside the command envelope; capture gaps and scheduling limit rankings. Tracedump percentiles are separate. Stills establish text/layout, never absence of flashing or jank.
 
--- build/astra5/token_specs.py	2026-10-16 05:27:26.632593495 +1100
+++ build/astra6/token_specs.py	2026-10-16 21:00:57.301923977 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json
-B=Path('build/astra5');out=[]
+B=Path('build/astra6');out=[]
 for filename,items in [('editor.c',[
  ('preprocessor',1,'#include',None),('string',1,'"editor/private.h"','#99c794'),('keyword',13,'static','#c695c6'),('type',13,'bool','#c695c6'),('function declaration',13,'runnable','#5fb4b4'),('function definition',16,'min64','#5fb4b4'),('function call',20,'trace_now_ns','#5fb4b4'),('operator',20,'=','#f97b58'),('punctuation',13,';','#a6acb9'),('comment',25,'Certify','#a6acb9'),('number',26,'4096','#f9ae58'),('macro',26,'LINEIDX_CHUNK','#c695c6')]),('config.rs',[
  ('comment',1,'Native','#a6acb9'),('keyword',6,'use','#c695c6'),('type',7,'Path','#c695c6'),('string',16,'"font"','#99c794'),('constant',15,'CONFIG_KEYS','#c695c6'),('operator',15,'=','#f97b58'),('punctuation',6,';','#a6acb9'),('attribute',34,'derive',None),('function definition',56,'fmt', '#5fb4b4'),('lifetime',55,"'_",None),('function call',58,'write_str','#5fb4b4'),('number',98,'1','#f9ae58')])]:
--- build/astra5/tasks_extra.py	2026-10-16 05:27:26.632593495 +1100
+++ build/astra6/tasks_extra.py	2026-10-16 21:00:57.301923977 +1100
@@ -24,12 +24,12 @@
  for name in ['ascii_code.c','PRD.md','editor.c','utf8.txt','dense_short.txt','navigation.txt']:shutil.copyfile(BASE/'files'/name,folder/name)
  d=Drive('09-tabs',editor);d.launch(folder/'ascii_code.c',new=False if editor=='sublimite' else True);time.sleep(2)
  for name in ['PRD.md','editor.c','utf8.txt','dense_short.txt','navigation.txt']:
-  d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
+  d.cmd([str(ROOT/'build/astra6/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
  d.focus();d.shot('five-opened')
  for label,k in [('tab1','ctrl+Tab'),('tab2','ctrl+Tab'),('close','ctrl+w'),('reopen','ctrl+shift+t')]:d.key(k);d.shot(label)
  d.mark('100_tabs_start')
  for i in range(100):
-  p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
+  p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra6/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
  d.mark('100_tabs_end')
  if editor=='sublimite':
   links=[]
--- build/astra5/close_comparators5.py	2026-10-16 07:33:30.075333102 +1100
+++ build/astra6/close_comparators5.py	2026-10-16 21:00:57.301923977 +1100
@@ -5,9 +5,9 @@
  pid=sp.run(['xdotool','getwindowpid',w],capture_output=True,text=True).stdout.strip()
  try:env=Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
  except OSError:continue
- if b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-clean/config' not in env:continue
+ if b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-clean/config' not in env:continue
  title=sp.run(['xdotool','getwindowname',w],capture_output=True,text=True).stdout.strip()
- if '/tmp/edit-corpus/' in title or '/build/astra5/' in title:pids.setdefault(pid,w)
+ if '/tmp/edit-corpus/' in title or '/build/astra6/' in title:pids.setdefault(pid,w)
 for pid,w in pids.items():
  t=time.time();r=sp.run(['xdotool','windowmap',w,'windowraise',w,'windowfocus','--sync',w,'key','--clearmodifiers','Escape','Escape','Escape','ctrl+q'],capture_output=True,text=True)
  rows.append(dict(pid=pid,window=w,quit_requested=t,command_returncode=r.returncode,stderr=r.stderr))
@@ -15,4 +15,4 @@
  if all(not Path('/proc',pid).exists() for pid in pids):break
  time.sleep(1)
 for r in rows:r['still_present']=Path('/proc',r['pid']).exists()
-Path('build/astra5/logs/comparator-cleanup.json').write_text(json.dumps(rows,indent=2));print(rows)
+Path('build/astra6/logs/comparator-cleanup.json').write_text(json.dumps(rows,indent=2));print(rows)
--- build/astra5/profile_continue4.py	2026-10-16 05:27:26.633593498 +1100
+++ build/astra6/profile_continue4.py	2026-10-16 21:00:57.302923984 +1100
@@ -16,4 +16,4 @@
 for i in range(16):d.key('ctrl+Tab');d.shot(f'tab-{i:02}')
 d.finish();listing(d,BASE/'state/17e','second-launch-after-close')
 d.cmd(['tail','-30',str(BASE/'logs/17-recovery-e-sublimite.editor.log')])
-p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
+p=sp.run([str(ROOT/'build/astra6/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
--- build/astra5/report_rows.py	2026-10-16 05:27:26.634593501 +1100
+++ build/astra6/report_rows.py	2026-10-16 21:00:57.302923984 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re
-B=Path('build/astra5'); R=Path('docs/reviews/astra-vs-sublime-5.md')
+B=Path('build/astra6'); R=Path('docs/reviews/astra-vs-sublime-5.md')
 
 def stamp(stem):
  es=json.loads((B/'logs'/f'{stem}.json').read_text());p=next(e for e in es if e['action']=='power');return f"(M)[{'AC' if p['AC']=='1' else 'bat'}], load1={p['loadavg'].split()[0]}"
--- build/astra5/gutter_validate.py	2026-10-16 05:27:26.634593501 +1100
+++ build/astra6/gutter_validate.py	2026-10-16 21:00:57.302923984 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import subprocess as sp,json,csv,io
-B=Path('build/astra5');out=[]
+B=Path('build/astra6');out=[]
 for r in json.loads((B/'logs/large-gutters.json').read_text()):
  if r['key']!='Prior' or r['video'] not in ['10-large-sublimite.mkv','10-large-early-sublimite.mkv','10-large-loaded-sublime.mkv']:continue
  q=dict(r)
--- build/astra5/report_traceability5.py	2026-10-17 07:22:41.813462344 +1100
+++ build/astra6/report_traceability5.py	2026-10-16 21:00:57.302923984 +1100
@@ -1,8 +1,8 @@
 from pathlib import Path
 import json,re,sys
-sys.path.insert(0,str(Path('build/astra5').resolve()))
+sys.path.insert(0,str(Path('build/astra6').resolve()))
 from report5 import evidence,events
-b=Path('build/astra5');p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text()
+b=Path('build/astra6');p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text()
 s=s.replace('Historical blockers in this table are explicitly waived for M0; they are not current blockers.','Only find in files and colour emoji artwork carry Tobias’s M0 waivers. Other historical blockers are recorded as fixed, not waived.')
 s=s.replace('Variable fallback advances; detailed caret repeat follows','Variable fallback advances; final byte/caret validation is below')
 s=s.replace('Standard and loaded comparisons scheduled separately','Private first-launch and ready loaded comparisons are reported below')
--- build/astra5/audit5.py	2026-10-16 05:42:06.352425123 +1100
+++ build/astra6/audit5.py	2026-10-16 21:00:57.302923984 +1100
@@ -1,6 +1,6 @@
 from pathlib import Path
 import json,re,hashlib
-B=Path('build/astra5');runs=[];errors=[]
+B=Path('build/astra6');runs=[];errors=[]
 for p in sorted((B/'logs').glob('*.json')):
  try:e=json.loads(p.read_text())
  except Exception:continue
--- build/astra5/analysis_run.sh	2026-10-16 05:27:26.635593504 +1100
+++ build/astra6/analysis_run.sh	2026-10-16 21:00:57.302923984 +1100
@@ -1,8 +1,8 @@
 export DISPLAY=:98
-python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
-python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
-python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
-python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
-python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
-python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
-python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
+python3 build/astra6/large_metrics.py sublimite > build/astra6/logs/large-analysis.txt 2>&1
+python3 build/astra6/analyze_new.py > build/astra6/logs/new-analysis.txt 2>&1
+python3 build/astra6/action_onsets.py > build/astra6/logs/action-analysis.txt 2>&1
+python3 build/astra6/font_video_metrics.py > build/astra6/logs/font-video-analysis.txt 2>&1
+python3 build/astra6/find_notice.py > build/astra6/logs/notice-analysis.txt 2>&1
+python3 build/astra6/large_gutters.py > build/astra6/logs/gutter-analysis.txt 2>&1
+python3 build/astra6/tab_metrics.py > build/astra6/logs/tab-analysis.txt 2>&1
```

Core additions will live only in build/astra6; copied scripts are preserved separately from that instrumentation.

## Core task 37 complete
Both builds driven in separate fresh profiles. astra5 reproduced -103; astra6 restored xy and then zxy; every candidate close returned zero. Corpus stat/hash unchanged. Videos `build/astra6/video/37-hotexit-dense64-astra{5,6}-sublimite.mkv`. Core-only code: core6.py, measure6.py, scan6.py, report6.py. Copied inherited scripts unchanged after setup. report section filed before task 38. The initial corpus manifest unnecessarily hashed other corpus files read-only, adding idle video time; changed future helper to hash only the target. No source/fixture mutation.

## Core task 38 complete
Generator command: python3 build/astra6/fif6.py (fixture function retained). All candidate tests, including immediate cancel repeat, completed; matching lines equal Sublime after explicit normalization; raw sorted diff saved. Initial nonpersistent harness ended before collecting return status and was rerun fresh. Real-profile Sublime search succeeded but subsequent navigation/clipboard stalled; comparison limitations filed, no false pass. Explicit candidate lifecycle returns zero. Videos 38-fif-r2-sublimite, 38-cancel-early-sublimite, 38-fif-compare-sublime; original attempts retained. Tree unchanged. Task section filed before task 39.

## Core task 39 complete
Five prescribed fresh-profile sequences plus observation-driven (a) repeat, Down display-check repeat and authorized astra5 (a) before run. QQQ appears at intended locations; no saves/copies of log_1g. Corpus digest matches. All closes zero. Initial (a) screenshot overlaps pending navigation, so its long timing is excluded; clean repeat recorded. Root still partial-body anomaly was not corroborated by video and is not called a defect. Default view is wrapped, so zero line_query / unwrapped dz0 code path not independently proven. All timing/trace limitations filed before task 40. Scripts: task39.py, task39repeat.py, task39before.py, task39down.py, measure39.py, blank39.py.

## Core task 40 complete
Private 64 MiB copy only; five phases sampled to ten stable consecutive samples each. Largest observed growth 4096 B, cmp offset comparison zero, saved abc and restored dirty abcd. Both closes zero; videos/traces/scan logs filed. Deleted build/astra6/files/dense-f3i.txt after verification. No corpus save. Task section filed before task 41.

## Core task 41 complete
Logged all required directories; no reserved/historical rotation temps, double-dot locks or set-asides. Absence-only limitation filed; optional kill-during-save sweep not run. No UI/video for this logs-only task. Section filed before task 36.

## Core task 36 complete
Copied drive-5 deferred log state, restored via Ctrl+Shift+T, observed xyR and source retirement; untouched control preserved deferred journal exactly. Small-orphan-copy and fresh startup controls complete. All normal endings zero. GB whole-text changed-base orphan unavailable as brief anticipated; limitation filed first. Copies 36-deferred, 36-control, 36-small-copy deleted after evidence capture. Source manifests unchanged. Videos build/astra6/video/36-{deferred,control,small-copy,fresh}-sublimite.mkv.

Core-end binary stat/digests unchanged; original dense and log corpus digests match. Required core sections complete and filed before verdict.

VERDICT filed: yes; blockers: none observed in the core set. No appendix task ran before it.

## Appendix task 1 provisional override row complete
Ran inherited more.override() without changes to its fixture/actions, through append6.py instrumentation. Same 31 px ink at clean load; final font adoption timing qualified separately from first fallback text. Video and three flash scans complete, normal close zero. Section complete before any further appendix task.

## Appendix task 2 paired probe and closeout
Inherited paired-Z segment executed verbatim; candidate onset valid and normal close zero. Sublime video is occluded by native update/save dialogs and is excluded from latency comparison. Private-config attempt did not establish a clean separate process. Complete appendix table filed. Remaining provisional/inherited appendix task groups not run; listed in report, no pass inferred. No VERDICT AMENDMENT.

Literal `diff -r` of script-only snapshots from drive 5 and the copied inherited drive-6 scripts (core additions excluded):
```diff
diff -r build/astra6/logs/harness-before/analysis_run.sh build/astra6/logs/harness-after/analysis_run.sh
2,8c2,8
< python3 build/astra5/large_metrics.py sublimite > build/astra5/logs/large-analysis.txt 2>&1
< python3 build/astra5/analyze_new.py > build/astra5/logs/new-analysis.txt 2>&1
< python3 build/astra5/action_onsets.py > build/astra5/logs/action-analysis.txt 2>&1
< python3 build/astra5/font_video_metrics.py > build/astra5/logs/font-video-analysis.txt 2>&1
< python3 build/astra5/find_notice.py > build/astra5/logs/notice-analysis.txt 2>&1
< python3 build/astra5/large_gutters.py > build/astra5/logs/gutter-analysis.txt 2>&1
< python3 build/astra5/tab_metrics.py > build/astra5/logs/tab-analysis.txt 2>&1
---
> python3 build/astra6/large_metrics.py sublimite > build/astra6/logs/large-analysis.txt 2>&1
> python3 build/astra6/analyze_new.py > build/astra6/logs/new-analysis.txt 2>&1
> python3 build/astra6/action_onsets.py > build/astra6/logs/action-analysis.txt 2>&1
> python3 build/astra6/font_video_metrics.py > build/astra6/logs/font-video-analysis.txt 2>&1
> python3 build/astra6/find_notice.py > build/astra6/logs/notice-analysis.txt 2>&1
> python3 build/astra6/large_gutters.py > build/astra6/logs/gutter-analysis.txt 2>&1
> python3 build/astra6/tab_metrics.py > build/astra6/logs/tab-analysis.txt 2>&1
diff -r build/astra6/logs/harness-before/audit5.py build/astra6/logs/harness-after/audit5.py
3c3
< B=Path('build/astra5');runs=[];errors=[]
---
> B=Path('build/astra6');runs=[];errors=[]
diff -r build/astra6/logs/harness-before/close_comparators5.py build/astra6/logs/harness-after/close_comparators5.py
8c8
<  if b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-clean/config' not in env:continue
---
>  if b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-clean/config' not in env:continue
10c10
<  if '/tmp/edit-corpus/' in title or '/build/astra5/' in title:pids.setdefault(pid,w)
---
>  if '/tmp/edit-corpus/' in title or '/build/astra6/' in title:pids.setdefault(pid,w)
18c18
< Path('build/astra5/logs/comparator-cleanup.json').write_text(json.dumps(rows,indent=2));print(rows)
---
> Path('build/astra6/logs/comparator-cleanup.json').write_text(json.dumps(rows,indent=2));print(rows)
diff -r build/astra6/logs/harness-before/close_comparators_final5.py build/astra6/logs/harness-after/close_comparators_final5.py
8c8
<  if not any(x.startswith(b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-') for x in env):continue
---
>  if not any(x.startswith(b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-') for x in env):continue
10c10
<  if '/tmp/edit-corpus/' in title or '/build/astra5/' in title:pids.setdefault(pid,w)
---
>  if '/tmp/edit-corpus/' in title or '/build/astra6/' in title:pids.setdefault(pid,w)
18c18
< Path('build/astra5/logs/comparator-cleanup-final.json').write_text(json.dumps(rows,indent=2));print(rows)
---
> Path('build/astra6/logs/comparator-cleanup-final.json').write_text(json.dumps(rows,indent=2));print(rows)
diff -r build/astra6/logs/harness-before/contact5.py build/astra6/logs/harness-after/contact5.py
4c4
< b=Path('build/astra5');ps=[]
---
> b=Path('build/astra6');ps=[]
diff -r build/astra6/logs/harness-before/drive.py build/astra6/logs/harness-after/drive.py
5c5
< ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra5'
---
> ROOT=Path('/home/tobias/Projects/sublimite'); BASE=ROOT/'build/astra6'
26c26
<   args=[str(ROOT/'build/astra5/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
---
>   args=[str(ROOT/'build/astra6/sublimite')]+(['--new-instance'] if new else [])+[str(path)] if self.editor=='sublimite' else ['subl','--new-window',str(path)]
66c66
<    p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
---
>    p=sp.run([str(ROOT/'build/astra6/tracedump'),str(BASE/'traces'/f'{self.stem}.bin')],capture_output=True,text=True);(BASE/'traces'/f'{self.stem}.txt').write_text(p.stdout+p.stderr)
diff -r build/astra6/logs/harness-before/drive4_wrapper.py build/astra6/logs/harness-after/drive4_wrapper.py
10c10
<  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
---
>  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra6/sublimite\nsha256sum build/astra6/sublimite"],capture_output=True,text=True).stdout
72c72
<  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
---
>  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra6/logs/{self.stem}.json`.\n")
diff -r build/astra6/logs/harness-before/drive5_wrapper.py build/astra6/logs/harness-after/drive5_wrapper.py
10c10
<  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra5/sublimite\nsha256sum build/astra5/sublimite"],capture_output=True,text=True).stdout
---
>  stamp=sp.run(['bash','-c',"export DISPLAY=:98\ndate --iso-8601=seconds\ncat /sys/class/power_supply/BAT0/status /sys/class/power_supply/AC/online /proc/loadavg\nstat -c '%y %s' build/astra6/sublimite\nsha256sum build/astra6/sublimite"],capture_output=True,text=True).stdout
75c75
<  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra5/logs/{self.stem}.json`.\n")
---
>  with REPORT.open('a') as f:f.write(f"- Captured `{self.stem}.mkv`, (M)[{'AC' if stamp['AC']=='1' else 'bat'}], load1={stamp['loadavg'].split()[0]}. Analysis pending; command envelopes/readbacks: `build/astra6/logs/{self.stem}.json`.\n")
diff -r build/astra6/logs/harness-before/execute.py build/astra6/logs/harness-after/execute.py
14c14
<  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra5/logs/{self.stem}.json`. Visual analysis pending.\n')
---
>  with open('docs/reviews/astra-vs-sublime-5.md','a') as f:f.write(f'- Captured `{self.stem}.mkv`; command, power and readback log `build/astra6/logs/{self.stem}.json`. Visual analysis pending.\n')
diff -r build/astra6/logs/harness-before/final_audit.py build/astra6/logs/harness-after/final_audit.py
3c3
< b=Path('build/astra5');r=[]
---
> b=Path('build/astra6');r=[]
diff -r build/astra6/logs/harness-before/final_probes5.py build/astra6/logs/harness-after/final_probes5.py
6c6
<  try:private=b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra5/state/sublime-clean/config' in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
---
>  try:private=b'XDG_CONFIG_HOME=/home/tobias/Projects/sublimite/build/astra6/state/sublime-clean/config' in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
diff -r build/astra6/logs/harness-before/final_report5.py build/astra6/logs/harness-after/final_report5.py
3c3
< B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
---
> B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md')
29c29
< All evidence paths without a leading `build/` are relative to **build/astra5/**. Videos are in video/, stills and frame sequences in frames/, command/power/readback records in logs/, and literal trace dumps in traces/. All measured values in a row inherit its BAT0/AC/load stamp. Task IDs, key counts, prescribed dwell times, fixture dimensions and analysis rectangles are inputs/identifiers rather than performance budgets. A single observation is not a percentile. Every onset uses resampled 60 Hz intervals (P: 1000/60 ms per interval); command envelopes bound software injection, not physical key-to-photon time.
---
> All evidence paths without a leading `build/` are relative to **build/astra6/**. Videos are in video/, stills and frame sequences in frames/, command/power/readback records in logs/, and literal trace dumps in traces/. All measured values in a row inherit its BAT0/AC/load stamp. Task IDs, key counts, prescribed dwell times, fixture dimensions and analysis rectangles are inputs/identifiers rather than performance budgets. A single observation is not a percentile. Every onset uses resampled 60 Hz intervals (P: 1000/60 ms per interval); command envelopes bound software injection, not physical key-to-photon time.
37c37
< The staged executable is never rebuilt. The only old UI binary use is the explicitly requested task-36 fixture producer, labelled separately; it produced no oversized journal. The permitted fallback fixture generator follows the session test’s construction and links the existing current library solely to create an orphan. That generator is not the build under UI test. UI verification always uses build/astra5/sublimite. Main/source work continued elsewhere; BUILD and source/library changes are not substituted for staged-binary behaviour.
---
> The staged executable is never rebuilt. The only old UI binary use is the explicitly requested task-36 fixture producer, labelled separately; it produced no oversized journal. The permitted fallback fixture generator follows the session test’s construction and links the existing current library solely to create an orphan. That generator is not the build under UI test. UI verification always uses build/astra6/sublimite. Main/source work continued elsewhere; BUILD and source/library changes are not substituted for staged-binary behaviour.
diff -r build/astra6/logs/harness-before/finish_report5.py build/astra6/logs/harness-after/finish_report5.py
3c3
< sys.path.insert(0,str(Path('build/astra5').resolve()))
---
> sys.path.insert(0,str(Path('build/astra6').resolve()))
5c5
< B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md');s=P.read_text()
---
> B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md');s=P.read_text()
diff -r build/astra6/logs/harness-before/gutter_validate.py build/astra6/logs/harness-after/gutter_validate.py
3c3
< B=Path('build/astra5');out=[]
---
> B=Path('build/astra6');out=[]
diff -r build/astra6/logs/harness-before/hide_old_sublime5.py build/astra6/logs/harness-after/hide_old_sublime5.py
12,13c12,13
< Path('build/astra5/logs/window-guard.pid').write_text(str(os.getpid()))
< f=open('build/astra5/logs/window-guard.jsonl','a',buffering=1)
---
> Path('build/astra6/logs/window-guard.pid').write_text(str(os.getpid()))
> f=open('build/astra6/logs/window-guard.jsonl','a',buffering=1)
diff -r build/astra6/logs/harness-before/metrics.py build/astra6/logs/harness-after/metrics.py
5c5
< B=Path('build/astra5')
---
> B=Path('build/astra6')
diff -r build/astra6/logs/harness-before/new4.py build/astra6/logs/harness-after/new4.py
3c3
< BIN=ROOT/'build/astra5/sublimite'
---
> BIN=ROOT/'build/astra6/sublimite'
diff -r build/astra6/logs/harness-before/new_tasks.py build/astra6/logs/harness-after/new_tasks.py
21c21
<  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra5/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
---
>  f=open(BASE/'logs'/f'{d.stem}.editor.log','a');d.proc=sp.Popen([str(ROOT/'build/astra6/sublimite'),'--new-instance'],env=env,stdout=f,stderr=f)
78c78
<  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  p=BASE/'files/naïve-中文-Ελλάδα.txt';p.write_text('Unicode title\n');d=Drive('18-tabtitle',ed);d.launch(p,new=False);d.shot('unicode');d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second-tab');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
85c85
<  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
---
>  d.key('ctrl+s');d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(BASE/'files/navigation.txt')]);time.sleep(1);d.focus();d.shot('other-tab');d.mark('inactive-external');p.open('a').write('INACTIVE_APPEND\n');time.sleep(2);d.key('ctrl+Tab');d.key('ctrl+End');d.shot('inactive-return');readback(d,'inactive');d.finish()
diff -r build/astra6/logs/harness-before/profile_continue4.py build/astra6/logs/harness-after/profile_continue4.py
19c19
< p=sp.run([str(ROOT/'build/astra5/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
---
> p=sp.run([str(ROOT/'build/astra6/tracedump'),str(BASE/'traces/17-recovery-e-sublimite.bin')],capture_output=True,text=True);(BASE/'traces/17-recovery-e-sublimite.txt').write_text(p.stdout+p.stderr)
diff -r build/astra6/logs/harness-before/report5.py build/astra6/logs/harness-after/report5.py
3c3
< B=Path('build/astra5');REPORT=Path('docs/reviews/astra-vs-sublime-5.md')
---
> B=Path('build/astra6');REPORT=Path('docs/reviews/astra-vs-sublime-5.md')
diff -r build/astra6/logs/harness-before/report_final.py build/astra6/logs/harness-after/report_final.py
3c3
< B=Path('build/astra5');P=Path('docs/reviews/astra-vs-sublime-5.md')
---
> B=Path('build/astra6');P=Path('docs/reviews/astra-vs-sublime-5.md')
66c66
< Driven binary: `stat -c '%y %s' build/astra5/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra5/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
---
> Driven binary: `stat -c '%y %s' build/astra6/sublimite` returned **2026-10-12 00:05:47.058168432 +1000 5131640** (M)[AC]. The coordinator’s `build/astra6/BUILD` identifies **11baa63d861aef2003fdaae25c6fccbf26d6320d**, built 2026-10-12T00:07:46+10:00. Mtime/size were rechecked during the drive. Sublime reports **build 4200** (M)[AC], `subl --help`. No bd/git command or rebuild was run, and nothing under src/, tests/, bench/, fuzz/, or tools/ was modified by this drive. Earlier evidence directories were read/copied only.
70c70
< The copied drive-2 scripts differ only by `build/astra2`→`build/astra5`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
---
> The copied drive-2 scripts differ only by `build/astra2`→`build/astra6`. Full literal recursive diff of script-only snapshots is in the worker log and `logs/harness-diff-r.txt`. A separate wrapper adds power stamps and incremental report/log entries without changing copied input sequences. Tasks use isolated sublimité XDG_DATA_HOME directories; crash pairs retain them. Tobias’s profile and drive-2 conflict state were copied before launch. Normal sublimité endings use WM_DELETE_WINDOW; SIGKILL is confined to the specified crash probes.
74c74
< All video names below are relative to **build/astra5/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra5/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
---
> All video names below are relative to **build/astra6/video/**; stills/latency sequences are in frames/, with readable contact sheets directly under build/astra6/. `logs/<stem>.json` contains command start/end, focus checks, power, geometry and readbacks; `.timeline.txt` gives video-relative timestamps. Full traces are in traces/. Each table’s `FIXED`, `STILL PRESENT`, `CHANGED`, or `NEW` compares with the corresponding **drive-2** row. STILL PRESENT can mean a passing behavior retained; the text distinguishes that from a retained defect.
diff -r build/astra6/logs/harness-before/report_rows.py build/astra6/logs/harness-after/report_rows.py
3c3
< B=Path('build/astra5'); R=Path('docs/reviews/astra-vs-sublime-5.md')
---
> B=Path('build/astra6'); R=Path('docs/reviews/astra-vs-sublime-5.md')
diff -r build/astra6/logs/harness-before/report_traceability5.py build/astra6/logs/harness-after/report_traceability5.py
3c3
< sys.path.insert(0,str(Path('build/astra5').resolve()))
---
> sys.path.insert(0,str(Path('build/astra6').resolve()))
5c5
< b=Path('build/astra5');p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text()
---
> b=Path('build/astra6');p=Path('docs/reviews/astra-vs-sublime-5.md');s=p.read_text()
diff -r build/astra6/logs/harness-before/scan_all.py build/astra6/logs/harness-after/scan_all.py
3c3
< B=Path('build/astra5');r=[]
---
> B=Path('build/astra6');r=[]
diff -r build/astra6/logs/harness-before/scan_parallel.py build/astra6/logs/harness-after/scan_parallel.py
3c3
< B=Path('build/astra5')
---
> B=Path('build/astra6')
diff -r build/astra6/logs/harness-before/sheets.py build/astra6/logs/harness-after/sheets.py
4c4
< B=Path('build/astra5')
---
> B=Path('build/astra6')
diff -r build/astra6/logs/harness-before/tasks.py build/astra6/logs/harness-after/tasks.py
50c50
<   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra5/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
---
>   d.mark('external-append');sp.run(['bash','-c','export DISPLAY=:98\necho EXTERNAL_APPEND >> build/astra6/files/save.txt'],check=True);time.sleep(2);d.shot('external-focused')
diff -r build/astra6/logs/harness-before/tasks_extra.py build/astra6/logs/harness-after/tasks_extra.py
27c27
<   d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
---
>   d.cmd([str(ROOT/'build/astra6/sublimite') if editor=='sublimite' else 'subl',str(folder/name)],timeout=15);time.sleep(.3)
32c32
<   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra5/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
---
>   p=folder/f'tab-{i:03}.txt';p.write_text(f'tab fixture {i:03}\n');d.cmd([str(ROOT/'build/astra6/sublimite') if editor=='sublimite' else 'subl',str(p)],timeout=10)
diff -r build/astra6/logs/harness-before/timelines.py build/astra6/logs/harness-after/timelines.py
3c3
< B=Path('build/astra5')
---
> B=Path('build/astra6')
diff -r build/astra6/logs/harness-before/title_second.py build/astra6/logs/harness-after/title_second.py
5c5
<  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra5/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
---
>  d=Drive('18-tabtitle-second',ed);d.launch(a,new=False);d.cmd([str(ROOT/'build/astra6/sublimite') if ed=='sublimite' else 'subl',str(p)]);time.sleep(1);d.focus();d.shot('second');d.key('ctrl+Tab');d.shot('away');d.key('ctrl+Tab');d.shot('back');d.finish()
diff -r build/astra6/logs/harness-before/token_specs.py build/astra6/logs/harness-after/token_specs.py
3c3
< B=Path('build/astra5');out=[]
---
> B=Path('build/astra6');out=[]
diff -r build/astra6/logs/harness-before/unicode_cells5.py build/astra6/logs/harness-after/unicode_cells5.py
4c4
< b=Path('build/astra5');out=[]
---
> b=Path('build/astra6');out=[]
```

## Final audit
Both END binary hashes and stat tuples match start. Final integrity assertion initially failed because the generator used absolute filenames in its hash pipeline and the post-task command used relative filenames. Recomputed both forms: each matches its corresponding earlier form; fixture text also matches the generator. Corrected report wording and saved logs/38-tree-hash-normalization.txt, preserving raw original hashes. No binary change and no file damage. Video ffprobe inventory saved. Original VERDICT line remains exactly once and unchanged. Remaining appendix coverage is explicitly not run. Final chat output is separate from the report.
